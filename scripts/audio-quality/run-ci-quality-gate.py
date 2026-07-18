#!/usr/bin/env python3
"""Run a trusted audio harness and fail closed while validating its evidence."""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import math
import os
import shutil
import stat
import subprocess
import sys
import tempfile
from pathlib import Path, PurePosixPath
from typing import Any, Mapping, MutableMapping, Sequence

from measurement_evidence import (
	MeasurementEvidenceError,
	canonical_json_bytes as canonical_measurement_json_bytes,
	indexed_artifact_references,
)
from payload_identity import (
	PayloadIdentityError,
	canonical_json_sha256 as canonical_payload_json_sha256,
	payload_file_attestation,
	payload_sha256 as canonical_payload_sha256,
	payload_tree_attestation,
)


SUITES = ("master_quality", "nightly", "release")
CORE_PROFILES = ("Original", "Light", "Balanced", "Quality", "VoiceFocus")
QUALITY_QUALIFICATION = "qualification.json"
ORIGINAL_QUALIFICATION = "original-voice-qualification.json"
ORIGINAL_PROVENANCE = "original-voice-provenance.json"
ORIGINAL_PROVENANCE_KIND = "mumble-original-voice-qualification-provenance-v1"
UPLOAD_DIRECTORY = "upload"
ARTIFACT_SUFFIXES = {
	"case_evidence_jsonl": ".jsonl",
	"failure_spectrogram_index": ".json",
	"junit": ".xml",
	"measurement_index_json": ".json",
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
	try:
		return canonical_payload_json_sha256(value)
	except PayloadIdentityError as error:
		raise GateError(str(error)) from error


def file_sha256(path: Path) -> str:
	try:
		return str(payload_file_attestation(path)["sha256"])
	except PayloadIdentityError as error:
		raise GateError(str(error)) from error


def payload_sha256(path: Path) -> str:
	"""Hash a file or tree with the shared gate-compatible payload contract."""
	try:
		return canonical_payload_sha256(path)
	except PayloadIdentityError as error:
		raise GateError(str(error)) from error


def payload_tree_sha256(path: Path) -> str:
	try:
		return str(payload_tree_attestation(path)["sha256"])
	except PayloadIdentityError as error:
		raise GateError(str(error)) from error


def lexical_absolute(path: Path) -> Path:
	"""Make a path absolute without following a junction or symlink."""
	return Path(os.path.abspath(os.fspath(path)))


def lowercase_sha256(value: str, label: str) -> str:
	if len(value) != 64 or value.lower() != value or any(character not in "0123456789abcdef" for character in value):
		raise GateError(f"{label} must be a lowercase SHA-256")
	return value


def _load_json_bytes(raw: bytes, label: str) -> Any:
	def reject_duplicates(pairs: Sequence[tuple[str, Any]]) -> MutableMapping[str, Any]:
		result: MutableMapping[str, Any] = {}
		for key, value in pairs:
			if key in result:
				raise GateError(f"{label}: duplicate JSON key {key!r}")
			result[key] = value
		return result
	def reject_constant(value: str) -> None:
		raise GateError(f"{label}: non-finite JSON number {value!r} is forbidden")
	def finite_float(value: str) -> float:
		result = float(value)
		if not math.isfinite(result):
			raise GateError(f"{label}: non-finite JSON number {value!r} is forbidden")
		return result
	try:
		return json.loads(
			raw.decode("utf-8"), object_pairs_hook=reject_duplicates,
			parse_constant=reject_constant, parse_float=finite_float,
		)
	except (UnicodeDecodeError, json.JSONDecodeError) as error:
		raise GateError(f"{label}: invalid UTF-8 JSON: {error}") from error


def load_json(path: Path) -> Mapping[str, Any]:
	try:
		value = _load_json_bytes(path.read_bytes(), str(path))
	except OSError as error:
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
	resolved = lexical_absolute(path)
	# Hash the lexical path first. The shared identity walker rejects every
	# reparse component; resolving before this call would erase that evidence.
	expected = lowercase_sha256(expected_sha256, f"expected {label} hash")
	actual = payload_sha256(resolved)
	try:
		resolved.relative_to(source_root)
	except ValueError:
		pass
	else:
		raise GateError(f"{label} must live outside the checked-out repository")
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
	mixture_plan: Path,
	release_fixtures: Path,
	metrics_runtime: Path,
	runner_class: str,
	hardware_fingerprint_sha256: str,
	harness_sha256: str,
	release_holdout_approval_public_key_sha256: str | None = None,
) -> list[str]:
	common_values = (suite, str(source_root), str(output_root), source_sha, str(corpus_lock))
	if harness.suffix.lower() == ".ps1":
		pwsh = shutil.which("pwsh")
		if not pwsh:
			raise GateError("pwsh is required to execute the configured PowerShell quality harness")
		command = [
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
			"-MixturePlanPath",
			str(mixture_plan),
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
		if release_holdout_approval_public_key_sha256 is not None:
			command.extend([
				"-ReleaseHoldoutApprovalPublicKeySha256",
				release_holdout_approval_public_key_sha256,
			])
		return command
	if harness.suffix.lower() == ".py":
		command = [ sys.executable, str(harness) ]
	elif os.name == "nt" and harness.suffix.lower() in (".bat", ".cmd"):
		command = [ os.environ.get("COMSPEC", "cmd.exe"), "/d", "/s", "/c", str(harness) ]
	else:
		command = [ str(harness) ]
	command += [
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
		"--mixture-plan",
		str(mixture_plan),
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
	if release_holdout_approval_public_key_sha256 is not None:
		command.extend([
			"--release-holdout-approval-public-key-sha256",
			release_holdout_approval_public_key_sha256,
		])
	return command


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
	if original.get("corpus_sha256") != expected_corpus_sha:
		raise GateError("Original voice evidence does not attest the checked-in corpus lock")
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


def validate_original_provenance(
	provenance: Mapping[str, Any],
	original: Mapping[str, Any],
	qualification_snapshot: Mapping[str, Any],
	expected_sha: str,
	expected_build: Mapping[str, str] | None,
	expected_assembler_sha256: str | None = None,
	expected_scorer_sha256: str | None = None,
) -> None:
	required_root = {
		"assembler", "campaign_bindings", "campaign_id", "candidate_revision_contract", "commits", "corpus",
		"identities", "kind", "legacy_revision_contract", "matrix", "qualification", "schema_version",
		"transport",
	}
	if set(provenance) != required_root or provenance.get("schema_version") != 1 or provenance.get("kind") != ORIGINAL_PROVENANCE_KIND:
		raise GateError("Original voice provenance has an unsupported or incomplete root contract")

	def mapping(value: Any, label: str) -> Mapping[str, Any]:
		if not isinstance(value, dict):
			raise GateError(f"Original voice provenance {label} must be an object")
		return value

	def reference(value: Any, label: str, *, expected_hash: str | None = None) -> Mapping[str, Any]:
		item = mapping(value, label)
		if set(item) != {"path", "sha256", "size_bytes"}:
			raise GateError(f"Original voice provenance {label} is not an exact file reference")
		if not isinstance(item.get("path"), str) or not item["path"]:
			raise GateError(f"Original voice provenance {label} has no path")
		sha = lowercase_sha256(str(item.get("sha256")), f"Original voice provenance {label}")
		if not isinstance(item.get("size_bytes"), int) or isinstance(item["size_bytes"], bool) or item["size_bytes"] <= 0:
			raise GateError(f"Original voice provenance {label} has an invalid size")
		if expected_hash is not None and sha != expected_hash:
			raise GateError(f"Original voice provenance {label} hash differs from the protected identity")
		return item

	qualification = reference(provenance["qualification"], "qualification", expected_hash=str(qualification_snapshot["sha256"]))
	if qualification["path"] != ORIGINAL_QUALIFICATION or qualification["size_bytes"] != qualification_snapshot["size_bytes"]:
		raise GateError("Original voice provenance does not bind the exact qualification bytes")
	reference(provenance["campaign_bindings"], "campaign_bindings")
	reference(provenance["assembler"], "assembler", expected_hash=expected_assembler_sha256)

	commits = mapping(provenance["commits"], "commits")
	if set(commits) != {"candidate", "legacy_build", "legacy_instrumentation_base", "server"}:
		raise GateError("Original voice provenance commit set is incomplete")
	if commits.get("candidate") != expected_sha or commits.get("candidate") != original.get("candidate_build_sha"):
		raise GateError("Original voice provenance does not bind the checked-out candidate commit")
	if commits.get("legacy_build") != original.get("legacy_build_sha"):
		raise GateError("Original voice provenance legacy commit differs from the qualification")

	legacy_revision = mapping(provenance["legacy_revision_contract"], "legacy_revision_contract")
	if (
		legacy_revision.get("build_commit") != commits["legacy_build"]
		or legacy_revision.get("instrumentation_base_commit") != commits["legacy_instrumentation_base"]
		or legacy_revision.get("two_step_parent_chain_verified") is not True
		or legacy_revision.get("protected_voice_path_changes") != []
		or not isinstance(legacy_revision.get("protected_source_regions"), list)
		or len(legacy_revision["protected_source_regions"]) != 4
	):
		raise GateError("Original voice provenance legacy revision contract is incomplete")
	candidate_revision = mapping(provenance["candidate_revision_contract"], "candidate_revision_contract")
	if (
		candidate_revision.get("legacy_build_commit") != commits["legacy_build"]
		or candidate_revision.get("candidate_commit") != commits["candidate"]
		or candidate_revision.get("protected_voice_path_changes") != []
		or not isinstance(candidate_revision.get("protected_source_regions"), list)
		or len(candidate_revision["protected_source_regions"]) != 4
	):
		raise GateError("Original voice provenance candidate protected-source contract is incomplete")

	identities = mapping(provenance["identities"], "identities")
	if set(identities) != {"candidate", "legacy", "server", "tools"}:
		raise GateError("Original voice provenance identity set is incomplete")
	for role, expected_hash in (
		("legacy", str(original.get("legacy_executable_sha256"))),
		("candidate", str(original.get("candidate_executable_sha256"))),
	):
		identity = mapping(identities[role], f"identities.{role}")
		for field in ("build_executable", "stage_executable", "qualified_executable"):
			reference(identity.get(field), f"identities.{role}.{field}", expected_hash=expected_hash)
		if identity.get("commit") != commits["legacy_build" if role == "legacy" else "candidate"]:
			raise GateError(f"Original voice provenance {role} commit mismatch")
	stage_payload = mapping(mapping(identities["candidate"], "identities.candidate").get("stage_payload"), "identities.candidate.stage_payload")
	if set(stage_payload) != {"file_count", "path", "sha256"}:
		raise GateError("Original voice provenance candidate stage payload reference is incomplete")
	lowercase_sha256(str(stage_payload.get("sha256")), "Original voice provenance candidate stage payload")
	if expected_build is not None and stage_payload["sha256"] != expected_build.get("staged_payload_sha256"):
		raise GateError("Original voice provenance candidate payload differs from the protected staged payload")
	server = mapping(identities["server"], "identities.server")
	server_expected = expected_build.get("server_binary_sha256") if expected_build is not None else None
	reference(server.get("executable"), "identities.server.executable", expected_hash=server_expected)
	tools = mapping(identities["tools"], "identities.tools")
	if set(tools) != {"scorer", "wrapper"}:
		raise GateError("Original voice provenance tool set is incomplete")
	reference(tools["scorer"], "identities.tools.scorer", expected_hash=expected_scorer_sha256)
	reference(tools["wrapper"], "identities.tools.wrapper")

	corpus = mapping(provenance["corpus"], "corpus")
	if corpus.get("corpus_lock_canonical_sha256") != original.get("corpus_sha256"):
		raise GateError("Original voice provenance corpus lock differs from the qualification")
	for field in ("corpus_lock", "corpus_inventory", "mixture_plan", "render_manifest", "fixture_attestation", "input_wav", "clean_reference_wav"):
		reference(corpus.get(field), f"corpus.{field}")
	if expected_build is not None:
		if corpus["corpus_inventory"]["sha256"] != expected_build.get("corpus_inventory_sha256"):
			raise GateError("Original voice provenance uses a different protected corpus inventory")
		if corpus["mixture_plan"]["sha256"] != expected_build.get("mixture_plan_sha256"):
			raise GateError("Original voice provenance uses a different protected mixture plan")

	matrix = mapping(provenance["matrix"], "matrix")
	manifests = matrix.get("manifests")
	if matrix.get("required_pair_count") != 45 or matrix.get("completed_pair_count") != 45 or not isinstance(manifests, list) or len(manifests) != 45:
		raise GateError("Original voice provenance does not contain the complete 45-pair campaign")
	cases = original.get("cases")
	assert isinstance(cases, list)
	if len(cases) != 45:
		raise GateError("Original voice provenance qualification does not contain exactly 45 cases")
	required_matrix = [
		{"bitrate_bps": case["bitrate_bps"], "frames_per_packet": case["frames_per_packet"], "transmit_mode": case["transmit_mode"]}
		for case in cases
	]
	if matrix.get("required_matrix_sha256") != canonical_json_sha256(required_matrix):
		raise GateError("Original voice provenance matrix differs from the qualification")
	for index, manifest in enumerate(manifests):
		entry = mapping(manifest, f"matrix.manifests[{index}]")
		if entry.get("receiver_timeline") is None or entry.get("sender_pre_opus_timeline") is None:
			raise GateError("Original voice provenance does not keep receiver and pre-Opus timelines separate")


def validate_recipe_and_models(
	quality: Mapping[str, Any], model_manifest: Mapping[str, Any], recipe_manifest: Mapping[str, Any],
	measurement_index: Mapping[str, Any],
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
	model_by_id = {
		str(model["id"]): model
		for model in models
		if isinstance(model, dict) and isinstance(model.get("id"), str)
	}
	manifest_sha256 = build.get("recipe_manifest_sha256")
	expected_bindings = []
	for profile in CORE_PROFILES:
		matches = [
			recipe for recipe in recipes
			if isinstance(recipe, dict) and recipe.get("profile") == profile and recipe.get("advancedOnly") is not True
		]
		if len(matches) != 1:
			raise GateError(f"unsigned recipe manifest must expose exactly one product binding for {profile}")
		recipe = matches[0]
		model_ids = recipe.get("modelIds")
		if not isinstance(model_ids, list) or any(not isinstance(model_id, str) for model_id in model_ids):
			raise GateError(f"unsigned recipe manifest has invalid model IDs for {profile}")
		try:
			binding_models = [
				{
					"id": model_id,
					"sha256": model_by_id[model_id]["sha256"],
					"version": model_by_id[model_id]["version"],
				}
				for model_id in model_ids
			]
		except (KeyError, TypeError) as error:
			raise GateError(f"unsigned model manifest cannot resolve the exact {profile} model binding") from error
		expected_bindings.append({
			"profile": profile,
			"engine": recipe.get("engine"),
			"recipe": {
				"catalog_revision": catalog_revision,
				"id": recipe.get("id"),
				"manifest_sha256": manifest_sha256,
				"revision": recipe.get("revision"),
			},
			"models": sorted(binding_models, key=lambda model: str(model["id"])),
		})
	if measurement_index.get("profile_bindings") != expected_bindings:
		raise GateError("measurement index profile bindings do not exactly match the unsigned product manifests")


def validate_release_holdout_trust_root(
	quality: Mapping[str, Any], measurement_index: Mapping[str, Any], expected_public_key_sha256: str | None,
) -> None:
	suite = quality.get("suite")
	build = quality.get("build")
	if not isinstance(build, dict):
		raise GateError("quality evidence build is missing")
	build_value = build.get("release_holdout_approval_public_key_sha256")
	index_value = measurement_index.get("release_holdout_approval_public_key_sha256")
	if suite == "release":
		if expected_public_key_sha256 is None:
			raise GateError("release qualification requires an externally pinned release-owner public-key SHA-256")
		if build_value != expected_public_key_sha256 or index_value != expected_public_key_sha256:
			raise GateError("release holdout approval key does not match the externally pinned trust root")
	else:
		if expected_public_key_sha256 is not None or build_value is not None or index_value is not None:
			raise GateError("release holdout approval key is forbidden outside the release suite")


def _is_reparse(path: Path) -> bool:
	try:
		metadata = os.lstat(path)
	except OSError as error:
		raise GateError(f"unable to inspect upload source {path}: {error}") from error
	attributes = getattr(metadata, "st_file_attributes", 0)
	reparse_flag = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
	return path.is_symlink() or bool(attributes & reparse_flag)


def _safe_existing_file(output_root: Path, relative: str, expected: Mapping[str, Any]) -> Path:
	resolved_output = output_root.resolve()
	current = resolved_output
	for part in PurePosixPath(relative).parts:
		current = current / part
		if not current.exists():
			raise GateError(f"validated upload source is missing: {relative}")
		if _is_reparse(current):
			raise GateError(f"validated upload source is a symlink/reparse point: {relative}")
	source = current.resolve()
	try:
		source.relative_to(resolved_output)
	except ValueError as error:
		raise GateError(f"upload source escapes output root: {relative}") from error
	if not source.is_file():
		raise GateError(f"validated upload source is not a regular file: {relative}")
	if source.stat().st_size != expected.get("size_bytes") or file_sha256(source) != expected.get("sha256"):
		raise GateError(f"validated upload source bytes do not match their measurement index: {relative}")
	return source


def safe_artifact_paths(output_root: Path, quality: Mapping[str, Any]) -> list[str]:
	artifacts = quality.get("artifacts")
	if not isinstance(artifacts, dict):
		raise GateError("qualification artifacts are missing")
	paths: list[str] = []
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
	measurement_value = artifacts.get("measurement_index_json")
	if not isinstance(measurement_value, dict):
		raise GateError("qualification does not contain measurement_index_json")
	measurement_path = str(measurement_value["path"])
	measurement_file = _safe_existing_file(output_root, measurement_path, measurement_value)
	try:
		raw = measurement_file.read_bytes()
		index = _load_json_bytes(raw, "measurement index")
	except OSError as error:
		raise GateError(f"unable to load measurement index: {error}") from error
	if not isinstance(index, dict) or raw != canonical_measurement_json_bytes(index) + b"\n":
		raise GateError("measurement index must be canonical sorted-key UTF-8 JSON with one LF")
	build = quality.get("build")
	runner_class = build.get("runner_class") if isinstance(build, dict) else None
	suite = quality.get("suite")
	if not isinstance(runner_class, str) or not isinstance(suite, str):
		raise GateError("qualification suite/runner identity is missing")
	prefix = f"artifacts/{suite}-{runner_class}/"
	try:
		indexed = indexed_artifact_references(index, prefix)
	except MeasurementEvidenceError as error:
		raise GateError(f"measurement index upload allowlist is invalid: {error}") from error
	for name, value in artifacts.items():
		if name == "measurement_index_json":
			continue
		path = str(value["path"])
		if path not in indexed:
			raise GateError(f"qualification artifact {name!r} is not transitively listed by measurement-index.json")
		for field in ("contains_audio_samples", "path", "sha256", "size_bytes"):
			if indexed[path].get(field) != value.get(field):
				raise GateError(f"qualification artifact {name!r} differs from its measurement-index reference")
	for relative, reference in indexed.items():
		_safe_existing_file(output_root, relative, reference)
	paths.extend(indexed)
	paths = sorted(set(paths))

	namespace_root = output_root.joinpath(*PurePosixPath(prefix.rstrip("/")).parts)
	if not namespace_root.is_dir() or _is_reparse(namespace_root):
		raise GateError(f"measurement artifact namespace is missing or unsafe: {prefix}")
	observed: list[str] = []
	for current, directories, names in os.walk(namespace_root):
		current_path = Path(current)
		for directory in directories:
			if _is_reparse(current_path / directory):
				raise GateError(f"measurement artifact namespace contains a reparse directory: {current_path / directory}")
		for name in names:
			candidate = current_path / name
			if _is_reparse(candidate) or not candidate.is_file():
				raise GateError(f"measurement artifact namespace contains an unsafe entry: {candidate}")
			observed.append(candidate.relative_to(output_root).as_posix())
	if sorted(observed) != paths:
		missing = sorted(set(paths) - set(observed))
		extra = sorted(set(observed) - set(paths))
		raise GateError(f"measurement artifact namespace differs from the exact index; missing={missing!r}; unindexed={extra!r}")
	return paths


def prepare_safe_upload(
	output_root: Path, quality: Mapping[str, Any],
	root_evidence_snapshot: Mapping[str, Mapping[str, Any]] | None = None,
) -> None:
	upload_root = output_root / UPLOAD_DIRECTORY
	if upload_root.exists():
		raise GateError(f"safe upload directory already exists: {upload_root}")
	validated_paths = safe_artifact_paths(output_root, quality)
	artifacts = quality.get("artifacts")
	if not isinstance(artifacts, dict):
		raise GateError("qualification artifacts are missing")
	measurement_reference = artifacts.get("measurement_index_json")
	if not isinstance(measurement_reference, dict):
		raise GateError("qualification does not contain measurement_index_json")
	measurement_file = _safe_existing_file(output_root, str(measurement_reference["path"]), measurement_reference)
	try:
		measurement_raw = measurement_file.read_bytes()
		measurement_index = _load_json_bytes(measurement_raw, "measurement index snapshot")
	except OSError as error:
		raise GateError(f"unable to snapshot measurement index before upload: {error}") from error
	if not isinstance(measurement_index, dict) or measurement_raw != canonical_measurement_json_bytes(measurement_index) + b"\n":
		raise GateError("measurement index changed after validation")
	build = quality.get("build")
	runner_class = build.get("runner_class") if isinstance(build, dict) else None
	suite = quality.get("suite")
	if not isinstance(runner_class, str) or not isinstance(suite, str):
		raise GateError("qualification suite/runner identity is missing")
	try:
		indexed = indexed_artifact_references(measurement_index, f"artifacts/{suite}-{runner_class}/")
	except MeasurementEvidenceError as error:
		raise GateError(f"measurement index upload allowlist changed after validation: {error}") from error
	expected: dict[str, Mapping[str, Any]] = {str(measurement_reference["path"]): measurement_reference, **indexed}
	for relative in (QUALITY_QUALIFICATION, ORIGINAL_QUALIFICATION, ORIGINAL_PROVENANCE):
		source = output_root.joinpath(*PurePosixPath(relative).parts)
		if not source.is_file() or _is_reparse(source):
			raise GateError(f"validated upload source is missing or unsafe: {relative}")
		if root_evidence_snapshot is None:
			expected[relative] = {"size_bytes": source.stat().st_size, "sha256": file_sha256(source)}
		else:
			reference = root_evidence_snapshot.get(relative)
			if not isinstance(reference, dict):
				raise GateError(f"validated root evidence has no immutable pre-validation snapshot: {relative}")
			expected[relative] = reference
	paths = list(dict.fromkeys([QUALITY_QUALIFICATION, ORIGINAL_QUALIFICATION, ORIGINAL_PROVENANCE, *validated_paths]))
	upload_root.mkdir(parents=True)
	for relative in paths:
		reference = expected.get(relative)
		if reference is None:
			raise GateError(f"validated upload source has no immutable hash snapshot: {relative}")
		resolved_source = _safe_existing_file(output_root, relative, reference)
		destination = upload_root.joinpath(*PurePosixPath(relative).parts)
		destination.parent.mkdir(parents=True, exist_ok=True)
		shutil.copy2(resolved_source, destination)
		if destination.stat().st_size != reference["size_bytes"] or file_sha256(destination) != reference["sha256"]:
			raise GateError(f"copied upload artifact differs from its immutable pre-copy hash: {relative}")
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
	if "release" not in SUITES:
		raise AssertionError("protected CI gate cannot parse the final release suite")
	if canonical_json_sha256({ "b": 2, "a": 1 }) != canonical_json_sha256({ "a": 1, "b": 2 }):
		raise AssertionError("canonical JSON hashing is not order-independent")
	command = harness_command(
		Path("trusted-harness.py"), "master_quality", Path("source"), Path("output"), "a" * 40,
		Path("corpus-lock.json"), Path("candidate.exe"), Path("legacy.exe"), Path("stage"),
		Path("models.json"), Path("recipes.json"), Path("server.exe"), Path("inventory.json"),
		Path("cases.json"), Path("mixture-plan.json"), Path("release-fixtures"), Path("metrics-runtime"), "low-performance",
		"b" * 64, "c" * 64,
	)
	if command[command.index("--legacy-binary") + 1] != "legacy.exe":
		raise AssertionError("trusted harness command is not bound to the supplied legacy executable")
	if command[command.index("--runner-class") + 1] != "low-performance":
		raise AssertionError("trusted harness command is not bound to the supplied runner class")
	if command[command.index("--mixture-plan") + 1] != "mixture-plan.json":
		raise AssertionError("trusted harness command is not bound to the supplied mixture plan")
	release_command = harness_command(
		Path("trusted-harness.py"), "release", Path("source"), Path("output"), "a" * 40,
		Path("corpus-lock.json"), Path("candidate.exe"), Path("legacy.exe"), Path("stage"),
		Path("models.json"), Path("recipes.json"), Path("server.exe"), Path("inventory.json"),
		Path("cases.json"), Path("mixture-plan.json"), Path("release-fixtures"), Path("metrics-runtime"), "low-performance",
		"b" * 64, "c" * 64, "d" * 64,
	)
	if release_command[release_command.index("--release-holdout-approval-public-key-sha256") + 1] != "d" * 64:
		raise AssertionError("trusted release harness command is not bound to the externally pinned release-owner key")
	validate_release_holdout_trust_root(
		{"suite": "release", "build": {"release_holdout_approval_public_key_sha256": "d" * 64}},
		{"release_holdout_approval_public_key_sha256": "d" * 64}, "d" * 64,
	)
	try:
		validate_release_holdout_trust_root(
			{"suite": "release", "build": {"release_holdout_approval_public_key_sha256": "e" * 64}},
			{"release_holdout_approval_public_key_sha256": "e" * 64}, "d" * 64,
		)
	except GateError:
		pass
	else:
		raise AssertionError("self-declared release-owner key bypassed the external CI trust root")
	with tempfile.TemporaryDirectory(prefix="mumble-safe-quality-upload-") as directory:
		output = Path(directory)
		protected_target = output / "protected-target.bin"
		protected_target.write_bytes(b"protected")
		protected_link = output / "protected-link.bin"
		try:
			protected_link.symlink_to(protected_target)
		except OSError:
			pass
		else:
			try:
				protected_path_identity(
					protected_link, output / "source", hashlib.sha256(b"protected").hexdigest(), "self-test protected link",
				)
			except GateError:
				pass
			else:
				raise AssertionError("protected path identity resolved a symlink before rejecting it")
			protected_link.unlink()
		prefix = "artifacts/master_quality-low-performance"
		artifact_root = output / prefix
		artifact_root.mkdir(parents=True)

		def write(relative: str, payload: bytes) -> Mapping[str, Any]:
			path = output.joinpath(*PurePosixPath(relative).parts)
			path.parent.mkdir(parents=True, exist_ok=True)
			path.write_bytes(payload)
			return {
				"contains_audio_samples": False,
				"path": relative,
				"sha256": hashlib.sha256(payload).hexdigest(),
				"size_bytes": len(payload),
			}

		junit = write(f"{prefix}/junit.xml", b"<testsuite/>\n")
		objective = write(f"{prefix}/measurements/case-001/objective-quality.json", b"{}\n")
		metrics_attestation = write(f"{prefix}/measurements/metrics-runtime-attestation.json", b"{}\n")
		index = {
			"schema_version": 1,
			"kind": "mumble-input-enhancement-measurement-index-v1",
			"qualification_scope": "core",
			"suite": "master_quality",
			"qualification_binding_sha256": "1" * 64,
			"objective_runtime_binding_sha256": "2" * 64,
			"metrics_runtime_attestation": metrics_attestation,
			"build": {},
			"plan_binding": {},
			"profile_bindings": [],
			"published_artifacts": [{"name": "junit", "artifact": junit}],
			"cases": [{"reports": {"objective_score": objective}}],
			"release_holdout_approval_public_key_sha256": None,
			"release_holdout_openings": [],
			"soak_reports": [],
			"transitions": [],
		}
		index_payload = canonical_measurement_json_bytes(index) + b"\n"
		measurement_index = write(f"{prefix}/measurement-index.json", index_payload)
		quality_upload = {
			"suite": "master_quality",
			"build": {"runner_class": "low-performance"},
			"artifacts": {"junit": junit, "measurement_index_json": measurement_index},
		}
		(output / QUALITY_QUALIFICATION).write_text("{}\n", encoding="utf-8")
		(output / ORIGINAL_QUALIFICATION).write_text("{}\n", encoding="utf-8")
		(output / ORIGINAL_PROVENANCE).write_text("{}\n", encoding="utf-8")
		expected_paths = sorted((junit["path"], metrics_attestation["path"], objective["path"], measurement_index["path"]))
		if safe_artifact_paths(output, quality_upload) != expected_paths:
			raise AssertionError("measurement index did not produce the exact transitive upload closure")

		unindexed = artifact_root / "unindexed.json"
		unindexed.write_text("{}\n", encoding="utf-8")
		try:
			safe_artifact_paths(output, quality_upload)
		except GateError:
			pass
		else:
			raise AssertionError("unindexed evidence inside the public namespace was accepted")
		unindexed.unlink()

		original_copy2 = shutil.copy2
		objective_path = output.joinpath(*PurePosixPath(str(objective["path"])).parts).resolve()
		raced = False
		def racing_copy2(source: Any, destination: Any, *args: Any, **kwargs: Any) -> Any:
			nonlocal raced
			if not raced and Path(source).resolve() == objective_path:
				objective_path.write_bytes(b'{"tampered":true}\n')
				raced = True
			return original_copy2(source, destination, *args, **kwargs)
		shutil.copy2 = racing_copy2
		try:
			prepare_safe_upload(output, quality_upload)
		except GateError:
			pass
		else:
			raise AssertionError("source mutation between validation and copy was accepted")
		finally:
			shutil.copy2 = original_copy2
			objective_path.write_bytes(b"{}\n")
			shutil.rmtree(output / UPLOAD_DIRECTORY, ignore_errors=True)

		prepare_safe_upload(output, quality_upload)
		upload = output / UPLOAD_DIRECTORY
		for relative in (QUALITY_QUALIFICATION, ORIGINAL_QUALIFICATION, ORIGINAL_PROVENANCE):
			if (upload / relative).read_bytes() != (output / relative).read_bytes():
				raise AssertionError(f"safe upload omitted or changed mandatory root evidence: {relative}")
		for relative in expected_paths:
			output.joinpath(*PurePosixPath(relative).parts).unlink()
		if safe_artifact_paths(upload, quality_upload) != expected_paths:
			raise AssertionError("the copied upload closure was not independently self-contained")
		if any(PurePosixPath(path).suffix.lower() in {".wav", ".flac", ".opus"} for path in expected_paths):
			raise AssertionError("audio leaked into the safe upload allowlist")

	catalog_revision = "self-test-v2"
	rnnoise_hash = "1" * 64
	deepfilter_hash = "2" * 64
	model_manifest = {
		"catalogRevision": catalog_revision,
		"models": [
			{"id": "rnnoise:embedded", "sha256": rnnoise_hash, "version": "1"},
			{"id": "deepfilternet:low-latency", "sha256": deepfilter_hash, "version": "1"},
		],
	}
	recipe_rows = [
		("Original", "None", "input.original", []),
		("Light", "Speex", "input.light.speex", []),
		("Balanced", "RNNoise", "input.balanced", ["rnnoise:embedded"]),
		("Quality", "DeepFilterNet", "input.quality", ["deepfilternet:low-latency"]),
		("VoiceFocus", "DeepFilterNet", "input.voice-focus", ["deepfilternet:low-latency"]),
		("Auto", "Speex", "input.auto.light", []),
		("Auto", "RNNoise", "input.auto.balanced", ["rnnoise:embedded"]),
		("Auto", "DeepFilterNet", "input.auto.quality", ["deepfilternet:low-latency"]),
	]
	recipe_manifest = {
		"catalogRevision": catalog_revision,
		"recipes": [
			{"profile": profile, "engine": engine, "id": recipe_id, "revision": 1, "modelIds": model_ids}
			for profile, engine, recipe_id, model_ids in recipe_rows
		],
	}
	recipe_manifest_hash = "3" * 64
	model_lookup = {model["id"]: model for model in model_manifest["models"]}
	profile_bindings = []
	for profile, engine, recipe_id, model_ids in recipe_rows[:5]:
		profile_bindings.append({
			"profile": profile,
			"engine": engine,
			"recipe": {
				"catalog_revision": catalog_revision, "id": recipe_id,
				"manifest_sha256": recipe_manifest_hash, "revision": 1,
			},
			"models": [
				{"id": model_id, "sha256": model_lookup[model_id]["sha256"], "version": model_lookup[model_id]["version"]}
				for model_id in model_ids
			],
		})
	product_quality = {
		"build": {
			"recipe_set_version": catalog_revision,
			"recipe_manifest_sha256": recipe_manifest_hash,
			"model_hashes": sorted([rnnoise_hash, deepfilter_hash]),
		},
	}
	validate_recipe_and_models(product_quality, model_manifest, recipe_manifest, {"profile_bindings": profile_bindings})
	wrong_bindings = json.loads(json.dumps(profile_bindings))
	wrong_bindings[3]["recipe"]["id"] = "input.quality.not-in-product-manifest"
	try:
		validate_recipe_and_models(product_quality, model_manifest, recipe_manifest, {"profile_bindings": wrong_bindings})
	except GateError:
		pass
	else:
		raise AssertionError("measurement profile bindings were not tied to the exact product manifests")

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
			"corpus_sha256": digest,
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

	def provenance_ref(sha256: str = digest) -> Mapping[str, Any]:
		return {"path": "pinned.bin", "sha256": sha256, "size_bytes": 1}

	provenance_cases = [
		{"bitrate_bps": 8_000 + index, "frames_per_packet": 1, "transmit_mode": "continuous"}
		for index in range(45)
	]
	provenance_original = {
		"candidate_build_sha": source_sha,
		"legacy_build_sha": "f" * 40,
		"candidate_executable_sha256": digest,
		"legacy_executable_sha256": "d" * 64,
		"corpus_sha256": digest,
		"cases": provenance_cases,
	}
	client_identity = lambda commit, executable: {
		"commit": commit,
		"worktree_receipt": provenance_ref(),
		"build_executable": provenance_ref(executable),
		"stage_executable": provenance_ref(executable),
		"qualified_executable": provenance_ref(executable),
		"stage_payload": {"path": "stage", "sha256": "9" * 64, "file_count": 1},
	}
	provenance = {
		"schema_version": 1,
		"kind": ORIGINAL_PROVENANCE_KIND,
		"campaign_id": "self-test",
		"campaign_bindings": provenance_ref(),
		"assembler": provenance_ref(),
		"commits": {
			"candidate": source_sha, "legacy_build": "f" * 40,
			"legacy_instrumentation_base": "a" * 40, "server": "b" * 40,
		},
		"legacy_revision_contract": {
			"build_commit": "f" * 40, "instrumentation_base_commit": "a" * 40,
			"two_step_parent_chain_verified": True, "protected_voice_path_changes": [],
			"protected_source_regions": [{"label": str(index), "sha256": digest} for index in range(4)],
		},
		"candidate_revision_contract": {
			"legacy_build_commit": "f" * 40, "candidate_commit": source_sha,
			"protected_voice_path_changes": [],
			"protected_source_regions": [{"label": str(index), "sha256": digest} for index in range(4)],
		},
		"identities": {
			"legacy": client_identity("f" * 40, "d" * 64),
			"candidate": client_identity(source_sha, digest),
			"server": {"commit": "b" * 40, "worktree_receipt": provenance_ref(), "executable": provenance_ref("8" * 64)},
			"tools": {"scorer": provenance_ref("7" * 64), "wrapper": provenance_ref("6" * 64)},
		},
		"transport": {},
		"corpus": {
			"corpus_lock": provenance_ref(), "corpus_lock_canonical_sha256": digest,
			"corpus_inventory": provenance_ref("5" * 64), "corpus_inventory_canonical_sha256": "4" * 64,
			"mixture_plan": provenance_ref("3" * 64), "mixture_plan_canonical_sha256": "2" * 64,
			"render_manifest": provenance_ref(), "render_entry_sha256": "1" * 64,
			"selected_case_id": "case", "fixture_attestation": provenance_ref(),
			"input_wav": provenance_ref(), "clean_reference_wav": provenance_ref(),
		},
		"matrix": {
			"required_pair_count": 45, "completed_pair_count": 45,
			"required_matrix_sha256": canonical_json_sha256(provenance_cases),
			"case_bindings_sha256": digest,
			"manifests": [
				{"sender_pre_opus_timeline": {}, "receiver_timeline": {}} for _ in range(45)
			],
		},
		"qualification": {"path": ORIGINAL_QUALIFICATION, "sha256": "0" * 64, "size_bytes": 123},
	}
	expected_provenance_build = {
		"staged_payload_sha256": "9" * 64,
		"server_binary_sha256": "8" * 64,
		"corpus_inventory_sha256": "5" * 64,
		"mixture_plan_sha256": "3" * 64,
	}
	validate_original_provenance(
		provenance, provenance_original, {"sha256": "0" * 64, "size_bytes": 123},
		source_sha, expected_provenance_build,
	)
	for label, mutate in (
		("missing receiver timeline", lambda value: value["matrix"]["manifests"][0].pop("receiver_timeline")),
		("candidate protected change", lambda value: value["candidate_revision_contract"].__setitem__("protected_voice_path_changes", ["src/Mumble.proto"])),
		("qualification byte mismatch", lambda value: value["qualification"].__setitem__("sha256", "1" * 64)),
	):
		broken = copy.deepcopy(provenance)
		mutate(broken)
		try:
			validate_original_provenance(
				broken, provenance_original, {"sha256": "0" * 64, "size_bytes": 123},
				source_sha, expected_provenance_build,
			)
		except GateError:
			pass
		else:
			raise AssertionError(f"Original provenance validator accepted {label}")


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
	parser.add_argument("--mixture-plan", type=Path)
	parser.add_argument("--release-fixtures", type=Path)
	parser.add_argument("--metrics-runtime", type=Path)
	parser.add_argument("--runner-class", choices=("low-performance", "mainstream", "local-development"))
	parser.add_argument("--hardware-fingerprint-sha256")
	parser.add_argument("--expected-harness-sha256")
	parser.add_argument("--expected-legacy-binary-sha256")
	parser.add_argument("--expected-server-binary-sha256")
	parser.add_argument("--expected-corpus-inventory-sha256")
	parser.add_argument("--expected-case-set-sha256")
	parser.add_argument("--expected-mixture-plan-sha256")
	parser.add_argument("--expected-release-fixtures-sha256")
	parser.add_argument("--expected-metrics-runtime-sha256")
	parser.add_argument("--expected-release-holdout-approval-public-key-sha256")
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
		expected_release_holdout_key_sha256: str | None = None
		if args.suite == "release":
			if args.expected_release_holdout_approval_public_key_sha256 is None:
				raise GateError("release suite requires --expected-release-holdout-approval-public-key-sha256")
			expected_release_holdout_key_sha256 = lowercase_sha256(
				args.expected_release_holdout_approval_public_key_sha256,
				"expected release holdout approval public key",
			)
		elif args.expected_release_holdout_approval_public_key_sha256 is not None:
			raise GateError("release holdout approval key pin is forbidden outside --suite release")
		if not args.validate_only and any(value is None for value in (
			args.tested_binary, args.legacy_binary, args.staged_client_root, args.model_manifest, args.recipe_manifest,
			args.server_binary, args.corpus_inventory, args.case_set, args.mixture_plan, args.release_fixtures, args.metrics_runtime,
			args.runner_class, args.hardware_fingerprint_sha256, args.expected_harness_sha256,
			args.expected_legacy_binary_sha256, args.expected_server_binary_sha256,
			args.expected_corpus_inventory_sha256, args.expected_case_set_sha256, args.expected_mixture_plan_sha256,
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
		mixture_plan: Path | None = None
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
			tested_binary = lexical_absolute(args.tested_binary)
			staged_client_root = lexical_absolute(args.staged_client_root)
			model_manifest_path = lexical_absolute(args.model_manifest)
			recipe_manifest_path = lexical_absolute(args.recipe_manifest)
			# These calls both prove existence/type and reject lexical reparse roots.
			tested_binary_sha = file_sha256(tested_binary)
			staged_payload_sha = payload_tree_sha256(staged_client_root)
			model_manifest_sha = file_sha256(model_manifest_path)
			recipe_manifest_sha = file_sha256(recipe_manifest_path)
			try:
				tested_binary.relative_to(staged_client_root)
			except ValueError as error:
				raise GateError("the tested binary must live below the supplied staged client root") from error
			model_manifest = load_json(model_manifest_path)
			recipe_manifest = load_json(recipe_manifest_path)
		if args.legacy_binary is not None:
			if args.expected_legacy_binary_sha256 is not None:
				legacy_binary, legacy_binary_sha = protected_path_identity(
					args.legacy_binary, source_root, args.expected_legacy_binary_sha256, "trusted legacy client"
				)
			else:
				legacy_binary = lexical_absolute(args.legacy_binary)
				legacy_binary_sha = file_sha256(legacy_binary)
				try:
					legacy_binary.relative_to(source_root)
				except ValueError:
					pass
				else:
					raise GateError("the trusted legacy binary must live outside the checked-out repository")

		protected_specs = (
			("server_binary_sha256", args.server_binary, args.expected_server_binary_sha256, "trusted OG server"),
			("corpus_inventory_sha256", args.corpus_inventory, args.expected_corpus_inventory_sha256, "frozen corpus inventory"),
			("case_set_sha256", args.case_set, args.expected_case_set_sha256, "frozen qualification case set"),
			("mixture_plan_sha256", args.mixture_plan, args.expected_mixture_plan_sha256, "frozen mixture plan"),
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
		mixture_plan = resolved_protected.get("mixture_plan_sha256")
		release_fixtures = resolved_protected.get("release_fixtures_sha256")
		metrics_runtime = resolved_protected.get("metrics_runtime_sha256")
		if metrics_runtime is not None and not metrics_runtime.is_dir():
			raise GateError("pinned metrics runtime must be a directory so its complete file inventory can be attested")
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
			harness = lexical_absolute(args.harness)
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
			assert server_binary is not None and corpus_inventory is not None and case_set is not None and mixture_plan is not None
			assert release_fixtures is not None and metrics_runtime is not None
			assert args.runner_class is not None and hardware_fingerprint_sha is not None
			command = harness_command(
				harness, args.suite, source_root, output_root, source_sha, corpus_lock,
				tested_binary, legacy_binary, staged_client_root, model_manifest_path, recipe_manifest_path,
				server_binary, corpus_inventory, case_set, mixture_plan, release_fixtures, metrics_runtime, args.runner_class,
				hardware_fingerprint_sha, harness_sha,
				expected_release_holdout_key_sha256,
			)
			print("Running trusted quality harness: " + subprocess.list2cmdline(command))
			harness_return_code = subprocess.run(command, check=False).returncode
			post_run_checks = {
				"trusted harness": (harness, harness_sha),
				"tested client": (tested_binary, tested_binary_sha),
				"legacy client": (legacy_binary, legacy_binary_sha),
				"staged payload": (staged_client_root, staged_payload_sha),
				"model manifest": (model_manifest_path, model_manifest_sha),
				"recipe manifest": (recipe_manifest_path, recipe_manifest_sha),
			}
			for label, (protected_path, before_sha256) in post_run_checks.items():
				if before_sha256 is None or payload_sha256(protected_path) != before_sha256:
					raise GateError(f"{label} changed while the trusted quality harness was running")
			for field, protected_path in resolved_protected.items():
				if payload_sha256(protected_path) != protected_hashes[field]:
					raise GateError(f"protected {field} changed while the trusted quality harness was running")
			if canonical_json_sha256(load_json(corpus_lock)) != corpus_sha or git_head(source_root) != source_sha:
				raise GateError("checked-out source identity changed while the trusted quality harness was running")

		quality_path = output_root / QUALITY_QUALIFICATION
		original_path = output_root / ORIGINAL_QUALIFICATION
		original_provenance_path = output_root / ORIGINAL_PROVENANCE
		if not quality_path.is_file() or not original_path.is_file() or not original_provenance_path.is_file():
			raise GateError(
				"harness did not produce qualification.json, original-voice-qualification.json, and "
				"original-voice-provenance.json; no gate can be claimed"
			)
		root_evidence_snapshot = {
			relative: {
				"size_bytes": (output_root / relative).stat().st_size,
				"sha256": file_sha256(output_root / relative),
			}
			for relative in (QUALITY_QUALIFICATION, ORIGINAL_QUALIFICATION, ORIGINAL_PROVENANCE)
		}
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
		for relative, reference in root_evidence_snapshot.items():
			_safe_existing_file(output_root, relative, reference)
		quality = load_json(quality_path)
		original = load_json(original_path)
		original_provenance = load_json(original_provenance_path)
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
			if expected_release_holdout_key_sha256 is not None:
				expected_build["release_holdout_approval_public_key_sha256"] = expected_release_holdout_key_sha256
			if harness_sha is not None:
				expected_build["harness_sha256"] = harness_sha
			if args.runner_class is not None:
				expected_build["runner_class"] = args.runner_class
			if hardware_fingerprint_sha is not None:
				expected_build["hardware_fingerprint_sha256"] = hardware_fingerprint_sha
		validate_identity(quality, original, args.suite, source_sha, corpus_sha, expected_build)
		validate_original_provenance(
			original_provenance, original, root_evidence_snapshot[ORIGINAL_QUALIFICATION], source_sha, expected_build,
			file_sha256(source_root / "scripts" / "audio-quality" / "assemble-original-voice-qualification.py"),
			file_sha256(source_root / "scripts" / "audio-quality" / "score-fixed-timeline.py"),
		)
		artifacts = quality.get("artifacts")
		measurement_reference = artifacts.get("measurement_index_json") if isinstance(artifacts, dict) else None
		if not isinstance(measurement_reference, dict) or not isinstance(measurement_reference.get("path"), str):
			raise GateError("quality evidence has no measurement index for trust-root verification")
		measurement_file = _safe_existing_file(output_root, str(measurement_reference["path"]), measurement_reference)
		measurement_index = load_json(measurement_file)
		validate_release_holdout_trust_root(quality, measurement_index, expected_release_holdout_key_sha256)
		if model_manifest is not None and recipe_manifest is not None:
			validate_recipe_and_models(quality, model_manifest, recipe_manifest, measurement_index)
		if not args.validate_only:
			for label, (protected_path, before_sha256) in post_run_checks.items():
				if before_sha256 is None or payload_sha256(protected_path) != before_sha256:
					raise GateError(f"{label} changed after the trusted quality harness completed")
			for field, protected_path in resolved_protected.items():
				if payload_sha256(protected_path) != protected_hashes[field]:
					raise GateError(f"protected {field} changed after the trusted quality harness completed")
			if canonical_json_sha256(load_json(corpus_lock)) != corpus_sha or git_head(source_root) != source_sha:
				raise GateError("checked-out source identity changed after the trusted quality harness completed")
		for relative, reference in root_evidence_snapshot.items():
			_safe_existing_file(output_root, relative, reference)
		prepare_safe_upload(output_root, quality, root_evidence_snapshot)
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
