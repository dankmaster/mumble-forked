#!/usr/bin/env python3
"""Run a trusted audio harness and fail closed while validating its evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path, PurePosixPath
from typing import Any, Mapping, Sequence


SUITES = ("master_quality", "nightly")
QUALITY_QUALIFICATION = "qualification.json"
ORIGINAL_QUALIFICATION = "original-voice-qualification.json"
UPLOAD_DIRECTORY = "upload"
ARTIFACT_SUFFIXES = {
	"case_evidence_jsonl": ".jsonl",
	"failure_spectrogram_index": ".json",
	"junit": ".xml",
	"per_case_csv": ".csv",
	"per_case_parquet": ".parquet",
	"summary_html": ".html",
	"summary_json": ".json",
}
ORIGINAL_ROOT_KEYS = {
	"candidate_build_sha",
	"candidate_executable_sha256",
	"cases",
	"corpus_sha256",
	"legacy_build_sha",
	"legacy_executable_sha256",
	"profile",
	"receiver_cleanup_enabled",
	"schema_version",
	"server_host",
	"transport_path",
}
ORIGINAL_CASE_KEYS = {
	"algorithmic_latency_samples",
	"bitrate_bps",
	"candidate_executable_sha256",
	"deadline_miss_count",
	"enhancement_profile",
	"fallback_count",
	"frames_per_packet",
	"input_pcm_sha256",
	"legacy_input_pcm_sha256",
	"legacy_executable_sha256",
	"legacy_opus_packets_sha256",
	"legacy_packet_count",
	"legacy_pcm_sha256",
	"legacy_received_pcm_sha256",
	"legacy_received_sample_count",
	"legacy_receiver_clipped_samples",
	"legacy_receiver_end_loss_samples",
	"legacy_receiver_fixed_timeline_passed",
	"legacy_receiver_missing_tail_samples",
	"legacy_receiver_onset_loss_samples",
	"legacy_terminator_count",
	"model_initialization_attempts",
	"original_input_pcm_sha256",
	"original_opus_packets_sha256",
	"original_packet_count",
	"original_pcm_sha256",
	"original_received_pcm_sha256",
	"original_received_sample_count",
	"original_receiver_clipped_samples",
	"original_receiver_end_loss_samples",
	"original_receiver_fixed_timeline_passed",
	"original_receiver_missing_tail_samples",
	"original_receiver_onset_loss_samples",
	"original_terminator_count",
	"receiver_jitter_delta_samples",
	"transmit_mode",
}


class GateError(RuntimeError):
	"""Raised when the harness or its evidence cannot qualify the tested build."""


def canonical_json_sha256(value: Any) -> str:
	payload = json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")
	return hashlib.sha256(payload).hexdigest()


def file_sha256(path: Path) -> str:
	digest = hashlib.sha256()
	with path.open("rb") as stream:
		for block in iter(lambda: stream.read(1024 * 1024), b""):
			digest.update(block)
	return digest.hexdigest()


def payload_sha256(path: Path) -> str:
	"""Hash a file directly or a directory as a canonical, path-sensitive file inventory."""
	if path.is_file():
		return file_sha256(path)
	if not path.is_dir():
		raise GateError(f"protected provenance path does not exist: {path}")
	records: list[Mapping[str, Any]] = []
	for candidate in sorted(path.rglob("*"), key=lambda value: value.relative_to(path).as_posix()):
		if candidate.is_symlink():
			raise GateError(f"protected provenance trees must not contain symlinks: {candidate}")
		if not candidate.is_file():
			continue
		records.append(
			{
				"path": candidate.relative_to(path).as_posix(),
				"sha256": file_sha256(candidate),
				"size_bytes": candidate.stat().st_size,
			}
		)
	if not records:
		raise GateError(f"protected provenance directory contains no files: {path}")
	return canonical_json_sha256(records)


def lowercase_sha256(value: str, label: str) -> str:
	if len(value) != 64 or value.lower() != value or any(character not in "0123456789abcdef" for character in value):
		raise GateError(f"{label} must be a lowercase SHA-256")
	return value


def load_json(path: Path) -> Mapping[str, Any]:
	try:
		value = json.loads(path.read_text(encoding="utf-8"))
	except (OSError, json.JSONDecodeError) as error:
		raise GateError(f"unable to load {path}: {error}") from error
	if not isinstance(value, dict):
		raise GateError(f"{path}: expected a JSON object")
	return value


def git_head(source_root: Path) -> str:
	try:
		completed = subprocess.run(
			[ "git", "rev-parse", "HEAD" ],
			cwd=source_root,
			check=True,
			capture_output=True,
			text=True,
			encoding="utf-8",
		)
	except (OSError, subprocess.CalledProcessError) as error:
		details = error.stderr.strip() if isinstance(error, subprocess.CalledProcessError) and error.stderr else str(error)
		raise GateError(f"unable to resolve tested Git commit: {details}") from error
	return completed.stdout.strip().lower()


def protected_path_identity(path: Path, source_root: Path, expected_sha256: str, label: str) -> tuple[Path, str]:
	resolved = path.resolve()
	if not resolved.exists():
		raise GateError(f"{label} does not exist: {resolved}")
	try:
		resolved.relative_to(source_root)
	except ValueError:
		pass
	else:
		raise GateError(f"{label} must live outside the checked-out repository")
	expected = lowercase_sha256(expected_sha256, f"expected {label} hash")
	actual = payload_sha256(resolved)
	if actual != expected:
		raise GateError(f"{label} hash {actual!r} does not match configured {expected!r}")
	return resolved, actual


def harness_command(
	harness: Path,
	suite: str,
	source_root: Path,
	output_root: Path,
	source_sha: str,
	corpus_lock: Path,
	tested_binary: Path,
	legacy_binary: Path,
	staged_client_root: Path,
	model_manifest: Path,
	recipe_manifest: Path,
	server_binary: Path,
	corpus_inventory: Path,
	case_set: Path,
	release_fixtures: Path,
	metrics_runtime: Path,
	runner_class: str,
	hardware_fingerprint_sha256: str,
	harness_sha256: str,
) -> list[str]:
	common_values = (suite, str(source_root), str(output_root), source_sha, str(corpus_lock))
	if harness.suffix.lower() == ".ps1":
		pwsh = shutil.which("pwsh")
		if not pwsh:
			raise GateError("pwsh is required to execute the configured PowerShell quality harness")
		return [
			pwsh,
			"-NoLogo",
			"-NoProfile",
			"-NonInteractive",
			"-File",
			str(harness),
			"-Suite",
			common_values[0],
			"-SourceRoot",
			common_values[1],
			"-OutputRoot",
			common_values[2],
			"-SourceSha",
			common_values[3],
			"-CorpusLock",
			common_values[4],
			"-TestedBinaryPath",
			str(tested_binary),
			"-LegacyBinaryPath",
			str(legacy_binary),
			"-StagedClientRoot",
			str(staged_client_root),
			"-ModelManifestPath",
			str(model_manifest),
			"-RecipeManifestPath",
			str(recipe_manifest),
			"-ServerBinaryPath",
			str(server_binary),
			"-CorpusInventoryPath",
			str(corpus_inventory),
			"-CaseSetPath",
			str(case_set),
			"-ReleaseFixturesPath",
			str(release_fixtures),
			"-MetricsRuntimePath",
			str(metrics_runtime),
			"-RunnerClass",
			runner_class,
			"-HardwareFingerprintSha256",
			hardware_fingerprint_sha256,
			"-HarnessSha256",
			harness_sha256,
		]
	if harness.suffix.lower() == ".py":
		command = [ sys.executable, str(harness) ]
	elif os.name == "nt" and harness.suffix.lower() in (".bat", ".cmd"):
		command = [ os.environ.get("COMSPEC", "cmd.exe"), "/d", "/s", "/c", str(harness) ]
	else:
		command = [ str(harness) ]
	return command + [
		"--suite",
		common_values[0],
		"--source-root",
		common_values[1],
		"--output-root",
		common_values[2],
		"--source-sha",
		common_values[3],
		"--corpus-lock",
		common_values[4],
		"--tested-binary",
		str(tested_binary),
		"--legacy-binary",
		str(legacy_binary),
		"--staged-client-root",
		str(staged_client_root),
		"--model-manifest",
		str(model_manifest),
		"--recipe-manifest",
		str(recipe_manifest),
		"--server-binary",
		str(server_binary),
		"--corpus-inventory",
		str(corpus_inventory),
		"--case-set",
		str(case_set),
		"--release-fixtures",
		str(release_fixtures),
		"--metrics-runtime",
		str(metrics_runtime),
		"--runner-class",
		runner_class,
		"--hardware-fingerprint-sha256",
		hardware_fingerprint_sha256,
		"--harness-sha256",
		harness_sha256,
	]


def run_validator(command: Sequence[str], label: str) -> None:
	completed = subprocess.run(command, check=False)
	if completed.returncode != 0:
		raise GateError(f"{label} rejected the harness evidence with exit code {completed.returncode}")


def validate_identity(
	quality: Mapping[str, Any],
	original: Mapping[str, Any],
	expected_suite: str,
	expected_sha: str,
	expected_corpus_sha: str,
	expected_build: Mapping[str, str] | None,
) -> None:
	if quality.get("suite") != expected_suite:
		raise GateError(f"qualification suite is {quality.get('suite')!r}, expected {expected_suite!r}")
	if quality.get("qualification_scope") != "core":
		raise GateError("protected master/nightly qualification must attest the core profile set")
	if quality.get("schema_version") != 3 or quality.get("status") != "passed":
		raise GateError("protected quality evidence must be a passing schema-v3 qualification")
	build = quality.get("build")
	if not isinstance(build, dict) or build.get("git_sha") != expected_sha:
		raise GateError("quality evidence does not attest the checked-out Git SHA")
	if build.get("corpus_lock_sha256") != expected_corpus_sha:
		raise GateError("quality evidence does not attest the checked-in corpus lock")
	if expected_build is not None:
		for field, expected in expected_build.items():
			if build.get(field) != expected:
				raise GateError(f"quality evidence does not attest the exact {field}")
	if original.get("candidate_build_sha") != expected_sha:
		raise GateError("Original voice evidence does not attest the checked-out Git SHA")
	expected_binary_sha = expected_build.get("tested_binary_sha256") if expected_build is not None else None
	expected_legacy_binary_sha = expected_build.get("legacy_binary_sha256") if expected_build is not None else None
	if expected_binary_sha is not None and original.get("candidate_executable_sha256") != expected_binary_sha:
		raise GateError("Original voice evidence does not attest the exact supplied staged client binary")
	if expected_legacy_binary_sha is not None and original.get("legacy_executable_sha256") != expected_legacy_binary_sha:
		raise GateError("Original voice evidence does not attest the exact supplied legacy client binary")
	if set(original) != ORIGINAL_ROOT_KEYS:
		raise GateError("Original voice evidence contains missing or unexpected root fields")
	cases = original.get("cases")
	if not isinstance(cases, list) or any(not isinstance(case, dict) or set(case) != ORIGINAL_CASE_KEYS for case in cases):
		raise GateError("Original voice evidence contains missing or unexpected case fields")


def validate_recipe_and_models(
	quality: Mapping[str, Any], model_manifest: Mapping[str, Any], recipe_manifest: Mapping[str, Any]
) -> None:
	catalog_revision = model_manifest.get("catalogRevision")
	if not isinstance(catalog_revision, str) or not catalog_revision or recipe_manifest.get("catalogRevision") != catalog_revision:
		raise GateError("unsigned model and recipe manifests do not share a catalog revision")
	build = quality.get("build")
	if not isinstance(build, dict) or build.get("recipe_set_version") != catalog_revision:
		raise GateError("quality evidence does not attest the exact unsigned recipe catalog")
	models = model_manifest.get("models")
	recipes = recipe_manifest.get("recipes")
	if not isinstance(models, list) or not isinstance(recipes, list):
		raise GateError("unsigned package manifests are incomplete")
	product_model_ids: set[str] = set()
	for recipe in recipes:
		if not isinstance(recipe, dict) or recipe.get("advancedOnly") is True:
			continue
		if recipe.get("profile") in ("Balanced", "Quality", "VoiceFocus", "Auto"):
			ids = recipe.get("modelIds")
			if not isinstance(ids, list) or any(not isinstance(value, str) for value in ids):
				raise GateError("product recipe has invalid model IDs")
			product_model_ids.update(ids)
	model_hash_by_id: dict[str, str] = {}
	for model in models:
		if not isinstance(model, dict) or not isinstance(model.get("id"), str) or not isinstance(model.get("sha256"), str):
			raise GateError("unsigned model manifest contains an invalid model record")
		model_hash_by_id[model["id"]] = model["sha256"]
	try:
		expected_hashes = sorted(model_hash_by_id[model_id] for model_id in product_model_ids)
	except KeyError as error:
		raise GateError(f"product recipe references missing model {error.args[0]!r}") from error
	reported_hashes = build.get("model_hashes") if isinstance(build, dict) else None
	if not isinstance(reported_hashes, list) or sorted(reported_hashes) != expected_hashes:
		raise GateError("quality evidence model hashes do not match the exact unsigned product model assets")


def safe_artifact_paths(quality: Mapping[str, Any]) -> list[str]:
	artifacts = quality.get("artifacts")
	if not isinstance(artifacts, dict):
		raise GateError("qualification artifacts are missing")
	paths = []
	for name, value in artifacts.items():
		if not isinstance(value, dict) or value.get("contains_audio_samples") is not False:
			raise GateError(f"artifact {name!r} is not explicitly marked audio-free")
		path = value.get("path")
		if not isinstance(path, str):
			raise GateError(f"artifact {name!r} has no path")
		parsed = PurePosixPath(path)
		if parsed.is_absolute() or ".." in parsed.parts or path != parsed.as_posix():
			raise GateError(f"artifact {name!r} has an unsafe path")
		expected_suffix = ARTIFACT_SUFFIXES.get(name)
		if expected_suffix is None or parsed.suffix.lower() != expected_suffix:
			raise GateError(f"artifact {name!r} does not use the required {expected_suffix!r} suffix")
		paths.append(path)
	return sorted(set(paths))


def prepare_safe_upload(output_root: Path, quality: Mapping[str, Any]) -> None:
	upload_root = output_root / UPLOAD_DIRECTORY
	if upload_root.exists():
		raise GateError(f"safe upload directory already exists: {upload_root}")
	upload_root.mkdir(parents=True)
	paths = [ QUALITY_QUALIFICATION, ORIGINAL_QUALIFICATION, *safe_artifact_paths(quality) ]
	resolved_output = output_root.resolve()
	for relative in paths:
		source = output_root.joinpath(*PurePosixPath(relative).parts).resolve()
		try:
			source.relative_to(resolved_output)
		except ValueError as error:
			raise GateError(f"upload source escapes output root: {relative}") from error
		if not source.is_file():
			raise GateError(f"validated upload source is missing: {relative}")
		destination = upload_root.joinpath(*PurePosixPath(relative).parts)
		destination.parent.mkdir(parents=True, exist_ok=True)
		shutil.copy2(source, destination)
	(upload_root / ".safe-to-upload").write_text("validated audio-free evidence\n", encoding="utf-8")


def append_github_output(path: Path | None, safe_upload: bool) -> None:
	if path is None:
		return
	try:
		with path.open("a", encoding="utf-8", newline="\n") as stream:
			stream.write(f"safe_upload={'true' if safe_upload else 'false'}\n")
	except OSError as error:
		raise GateError(f"unable to write GitHub output {path}: {error}") from error


def run_self_test() -> None:
	if canonical_json_sha256({ "b": 2, "a": 1 }) != canonical_json_sha256({ "a": 1, "b": 2 }):
		raise AssertionError("canonical JSON hashing is not order-independent")
	command = harness_command(
		Path("trusted-harness.py"), "master_quality", Path("source"), Path("output"), "a" * 40,
		Path("corpus-lock.json"), Path("candidate.exe"), Path("legacy.exe"), Path("stage"),
		Path("models.json"), Path("recipes.json"), Path("server.exe"), Path("inventory.json"),
		Path("cases.json"), Path("release-fixtures"), Path("metrics-runtime"), "low-performance",
		"b" * 64, "c" * 64,
	)
	if command[command.index("--legacy-binary") + 1] != "legacy.exe":
		raise AssertionError("trusted harness command is not bound to the supplied legacy executable")
	if command[command.index("--runner-class") + 1] != "low-performance":
		raise AssertionError("trusted harness command is not bound to the supplied runner class")
	if safe_artifact_paths(
		{
			"artifacts": {
				"case_evidence_jsonl": {
					"contains_audio_samples": False,
					"path": "artifacts/master_quality-low-performance/case-evidence.jsonl",
				},
				"junit": {
					"contains_audio_samples": False,
					"path": "reports/junit.xml",
				}
			}
		}
	) != [ "artifacts/master_quality-low-performance/case-evidence.jsonl", "reports/junit.xml" ]:
		raise AssertionError("safe artifact path classification regression")
	try:
		safe_artifact_paths(
			{
				"artifacts": {
					"capture": {
						"contains_audio_samples": True,
						"path": "private/capture.wav",
					}
				}
			}
		)
	except GateError:
		pass
	else:
		raise AssertionError("audio-bearing artifact was accepted")

	digest = "a" * 64
	source_sha = "b" * 40
	quality = {
		"schema_version": 3,
		"status": "passed",
		"suite": "master_quality",
		"qualification_scope": "core",
		"build": {
			"git_sha": source_sha,
			"corpus_lock_sha256": digest,
			"tested_binary_sha256": digest,
			"legacy_binary_sha256": "d" * 64,
		},
	}
	original = { key: None for key in ORIGINAL_ROOT_KEYS }
	original.update(
		{
			"candidate_build_sha": source_sha,
			"candidate_executable_sha256": digest,
			"cases": [ { key: None for key in ORIGINAL_CASE_KEYS } ],
		}
	)
	validate_identity(
		quality, original, "master_quality", source_sha, digest,
		{ "tested_binary_sha256": digest },
	)
	original["candidate_executable_sha256"] = "c" * 64
	try:
		validate_identity(
			quality, original, "master_quality", source_sha, digest,
			{ "tested_binary_sha256": digest },
		)
	except GateError:
		pass
	else:
		raise AssertionError("Original evidence candidate executable was not bound to the staged client")
	original["candidate_executable_sha256"] = digest
	original["legacy_executable_sha256"] = "d" * 64
	validate_identity(
		quality, original, "master_quality", source_sha, digest,
		{ "tested_binary_sha256": digest, "legacy_binary_sha256": "d" * 64 },
	)
	original["legacy_executable_sha256"] = "e" * 64
	try:
		validate_identity(
			quality, original, "master_quality", source_sha, digest,
			{ "tested_binary_sha256": digest, "legacy_binary_sha256": "d" * 64 },
		)
	except GateError:
		pass
	else:
		raise AssertionError("Original evidence legacy executable was not bound to the trusted legacy client")


def main(argv: Sequence[str] | None = None) -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--suite", choices=SUITES)
	parser.add_argument("--harness", type=Path)
	parser.add_argument("--source-root", type=Path)
	parser.add_argument("--output-root", type=Path)
	parser.add_argument("--tested-binary", type=Path)
	parser.add_argument("--legacy-binary", type=Path)
	parser.add_argument("--staged-client-root", type=Path)
	parser.add_argument("--model-manifest", type=Path)
	parser.add_argument("--recipe-manifest", type=Path)
	parser.add_argument("--server-binary", type=Path)
	parser.add_argument("--corpus-inventory", type=Path)
	parser.add_argument("--case-set", type=Path)
	parser.add_argument("--release-fixtures", type=Path)
	parser.add_argument("--metrics-runtime", type=Path)
	parser.add_argument("--runner-class", choices=("low-performance", "mainstream", "local-development"))
	parser.add_argument("--hardware-fingerprint-sha256")
	parser.add_argument("--expected-harness-sha256")
	parser.add_argument("--expected-legacy-binary-sha256")
	parser.add_argument("--expected-server-binary-sha256")
	parser.add_argument("--expected-corpus-inventory-sha256")
	parser.add_argument("--expected-case-set-sha256")
	parser.add_argument("--expected-release-fixtures-sha256")
	parser.add_argument("--expected-metrics-runtime-sha256")
	parser.add_argument("--github-output", type=Path)
	parser.add_argument("--validate-only", action="store_true", help="validate pre-existing output without invoking a harness")
	parser.add_argument("--self-test", action="store_true")
	args = parser.parse_args(argv)

	safe_upload = False
	try:
		if args.self_test:
			run_self_test()
			print("CI audio-quality gate self-test: ok")
			if args.suite is None:
				return 0
		if args.suite is None or args.source_root is None or args.output_root is None:
			raise GateError("--suite, --source-root, and --output-root are required")
		if not args.validate_only and args.harness is None:
			raise GateError("--harness is required unless --validate-only is used")
		if not args.validate_only and any(value is None for value in (
			args.tested_binary, args.legacy_binary, args.staged_client_root, args.model_manifest, args.recipe_manifest,
			args.server_binary, args.corpus_inventory, args.case_set, args.release_fixtures, args.metrics_runtime,
			args.runner_class, args.hardware_fingerprint_sha256, args.expected_harness_sha256,
			args.expected_legacy_binary_sha256, args.expected_server_binary_sha256,
			args.expected_corpus_inventory_sha256, args.expected_case_set_sha256,
			args.expected_release_fixtures_sha256, args.expected_metrics_runtime_sha256,
		)):
			raise GateError("measured runs require every protected binary, fixture, metric runtime, runner identity, and expected hash")

		source_root = args.source_root.resolve()
		output_root = args.output_root.resolve()
		corpus_lock = source_root / "scripts" / "audio-quality" / "corpus-lock.json"
		if not source_root.is_dir() or not corpus_lock.is_file():
			raise GateError("source root or corpus lock is missing")
		if output_root.exists() and not output_root.is_dir():
			raise GateError(f"output root is not a directory: {output_root}")
		output_root.mkdir(parents=True, exist_ok=True)
		if any(output_root.iterdir()) and not args.validate_only:
			raise GateError(f"quality harness output root is not empty: {output_root}")

		source_sha = git_head(source_root)
		tested_binary: Path | None = None
		staged_client_root: Path | None = None
		tested_binary_sha: str | None = None
		legacy_binary: Path | None = None
		legacy_binary_sha: str | None = None
		server_binary: Path | None = None
		corpus_inventory: Path | None = None
		case_set: Path | None = None
		release_fixtures: Path | None = None
		metrics_runtime: Path | None = None
		harness_sha: str | None = None
		hardware_fingerprint_sha: str | None = None
		model_manifest: Mapping[str, Any] | None = None
		recipe_manifest: Mapping[str, Any] | None = None
		model_manifest_sha: str | None = None
		recipe_manifest_sha: str | None = None
		staged_payload_sha: str | None = None
		protected_hashes: dict[str, str] = {}
		identity_inputs = (args.tested_binary, args.staged_client_root, args.model_manifest, args.recipe_manifest)
		if any(value is not None for value in identity_inputs):
			if any(value is None for value in identity_inputs):
				raise GateError("staged binary/root and unsigned model/recipe manifests must be supplied together")
			tested_binary = args.tested_binary.resolve()
			staged_client_root = args.staged_client_root.resolve()
			model_manifest_path = args.model_manifest.resolve()
			recipe_manifest_path = args.recipe_manifest.resolve()
			if not tested_binary.is_file() or not staged_client_root.is_dir():
				raise GateError("the supplied staged client root or tested binary is missing")
			if not model_manifest_path.is_file() or not recipe_manifest_path.is_file():
				raise GateError("the supplied unsigned model or recipe manifest is missing")
			try:
				tested_binary.relative_to(staged_client_root)
			except ValueError as error:
				raise GateError("the tested binary must live below the supplied staged client root") from error
			tested_binary_sha = file_sha256(tested_binary)
			staged_payload_sha = payload_sha256(staged_client_root)
			model_manifest_sha = file_sha256(model_manifest_path)
			recipe_manifest_sha = file_sha256(recipe_manifest_path)
			model_manifest = load_json(model_manifest_path)
			recipe_manifest = load_json(recipe_manifest_path)
		if args.legacy_binary is not None:
			if args.expected_legacy_binary_sha256 is not None:
				legacy_binary, legacy_binary_sha = protected_path_identity(
					args.legacy_binary, source_root, args.expected_legacy_binary_sha256, "trusted legacy client"
				)
			else:
				legacy_binary = args.legacy_binary.resolve()
				if not legacy_binary.is_file():
					raise GateError("the supplied legacy client binary is missing")
				try:
					legacy_binary.relative_to(source_root)
				except ValueError:
					pass
				else:
					raise GateError("the trusted legacy binary must live outside the checked-out repository")
				legacy_binary_sha = file_sha256(legacy_binary)

		protected_specs = (
			("server_binary_sha256", args.server_binary, args.expected_server_binary_sha256, "trusted OG server"),
			("corpus_inventory_sha256", args.corpus_inventory, args.expected_corpus_inventory_sha256, "frozen corpus inventory"),
			("case_set_sha256", args.case_set, args.expected_case_set_sha256, "frozen qualification case set"),
			("release_fixtures_sha256", args.release_fixtures, args.expected_release_fixtures_sha256, "release fixture set"),
			("metrics_runtime_sha256", args.metrics_runtime, args.expected_metrics_runtime_sha256, "pinned metrics runtime"),
		)
		resolved_protected: dict[str, Path] = {}
		for field, path, expected_hash, label in protected_specs:
			if path is None and expected_hash is None:
				continue
			if path is None or expected_hash is None:
				raise GateError(f"{label} path and expected hash must be supplied together")
			resolved, digest = protected_path_identity(path, source_root, expected_hash, label)
			resolved_protected[field] = resolved
			protected_hashes[field] = digest
		server_binary = resolved_protected.get("server_binary_sha256")
		corpus_inventory = resolved_protected.get("corpus_inventory_sha256")
		case_set = resolved_protected.get("case_set_sha256")
		release_fixtures = resolved_protected.get("release_fixtures_sha256")
		metrics_runtime = resolved_protected.get("metrics_runtime_sha256")
		if args.hardware_fingerprint_sha256 is not None:
			hardware_fingerprint_sha = lowercase_sha256(
				args.hardware_fingerprint_sha256, "hardware fingerprint"
			)
		manifest = load_json(corpus_lock)
		corpus_sha = canonical_json_sha256(manifest)
		run_validator(
			[
				sys.executable,
				str(source_root / "scripts" / "audio-quality" / "validate-corpus-lock.py"),
				str(corpus_lock),
			],
			"corpus lock validator",
		)
		harness_return_code = 0
		if not args.validate_only:
			if not args.harness.is_absolute():
				raise GateError("configured quality harness path must be absolute")
			harness = args.harness.resolve()
			if not harness.is_file():
				raise GateError(f"configured quality harness does not exist: {harness}")
			try:
				harness.relative_to(source_root)
			except ValueError:
				pass
			else:
				raise GateError("trusted quality harness must live outside the checked-out repository")
			harness_sha = file_sha256(harness)
			if harness_sha != lowercase_sha256(args.expected_harness_sha256, "expected harness hash"):
				raise GateError("trusted quality harness does not match its configured SHA-256")
			assert tested_binary is not None and legacy_binary is not None and staged_client_root is not None
			assert args.model_manifest is not None and args.recipe_manifest is not None
			assert server_binary is not None and corpus_inventory is not None and case_set is not None
			assert release_fixtures is not None and metrics_runtime is not None
			assert args.runner_class is not None and hardware_fingerprint_sha is not None
			command = harness_command(
				harness, args.suite, source_root, output_root, source_sha, corpus_lock,
				tested_binary, legacy_binary, staged_client_root, args.model_manifest.resolve(), args.recipe_manifest.resolve(),
				server_binary, corpus_inventory, case_set, release_fixtures, metrics_runtime, args.runner_class,
				hardware_fingerprint_sha, harness_sha,
			)
			print("Running trusted quality harness: " + subprocess.list2cmdline(command))
			harness_return_code = subprocess.run(command, check=False).returncode

		quality_path = output_root / QUALITY_QUALIFICATION
		original_path = output_root / ORIGINAL_QUALIFICATION
		if not quality_path.is_file() or not original_path.is_file():
			raise GateError(
				"harness did not produce qualification.json and original-voice-qualification.json; no gate can be claimed"
			)
		run_validator(
			[
				sys.executable,
				str(source_root / "scripts" / "audio-quality" / "validate-quality-qualification.py"),
				str(quality_path),
				"--artifact-root",
				str(output_root),
			],
			"quality qualification validator",
		)
		run_validator(
			[
				sys.executable,
				str(source_root / "scripts" / "audio-quality" / "check-original-voice-contract.py"),
				"--qualification",
				str(original_path),
			],
			"Original voice qualification validator",
		)
		quality = load_json(quality_path)
		original = load_json(original_path)
		expected_build: dict[str, str] | None = None
		if tested_binary_sha is not None:
			assert staged_payload_sha is not None and legacy_binary_sha is not None
			assert model_manifest_sha is not None and recipe_manifest_sha is not None
			expected_build = {
				"tested_binary_sha256": tested_binary_sha,
				"staged_payload_sha256": staged_payload_sha,
				"legacy_binary_sha256": legacy_binary_sha,
				"model_manifest_sha256": model_manifest_sha,
				"recipe_manifest_sha256": recipe_manifest_sha,
				**protected_hashes,
			}
			if harness_sha is not None:
				expected_build["harness_sha256"] = harness_sha
			if args.runner_class is not None:
				expected_build["runner_class"] = args.runner_class
			if hardware_fingerprint_sha is not None:
				expected_build["hardware_fingerprint_sha256"] = hardware_fingerprint_sha
		validate_identity(quality, original, args.suite, source_sha, corpus_sha, expected_build)
		if model_manifest is not None and recipe_manifest is not None:
			validate_recipe_and_models(quality, model_manifest, recipe_manifest)
		prepare_safe_upload(output_root, quality)
		safe_upload = True

		if harness_return_code != 0:
			raise GateError(f"quality harness exited with {harness_return_code}")
		if quality.get("status") != "passed":
			raise GateError("quality evidence is structurally valid but the semantic gate failed")
		append_github_output(args.github_output, safe_upload)
		print(f"CI audio-quality gate: passed; suite={args.suite}; source={source_sha}")
		return 0
	except (AssertionError, GateError) as error:
		try:
			append_github_output(args.github_output, safe_upload)
		except GateError as output_error:
			print(f"CI audio-quality gate: error: {output_error}", file=sys.stderr)
		print(f"CI audio-quality gate: error: {error}", file=sys.stderr)
		return 1


if __name__ == "__main__":
	sys.exit(main())
