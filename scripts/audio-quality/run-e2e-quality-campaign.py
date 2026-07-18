#!/usr/bin/env python3
"""Run the resumable, trusted two-client master-quality campaign.

The campaign is intentionally orchestration-only.  Every logical case uses
the production ``InputEnhancementPipeline`` through client 1, the supplied OG
Mumble server on 127.0.0.1, and client 2 with receiver cleanup disabled.  Raw
WAVs and copied app payloads stay below a private state root.  A case is only
marked resumable after the ten audio-free schema-v3 reports have been copied,
reopened, hash-checked, and independently derived by ``measurement_evidence``.

The trusted external harness invoked by ``run-ci-quality-gate.py`` is expected
to call this file and the separate Original-parity campaign.  This file owns
``qualification.json`` and the quality artifact namespace; it never fabricates
or weakens Original voice evidence.
"""

from __future__ import annotations

import argparse
import copy
import csv
import datetime as dt
import hashlib
import html
import importlib.util
import json
import math
import os
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path, PurePosixPath
from types import ModuleType
from typing import Any, Iterable, Mapping, MutableMapping, Sequence


class CampaignError(RuntimeError):
	pass


SCRIPT_DIR = Path(__file__).resolve().parent
CAMPAIGN_KIND = "mumble-input-enhancement-e2e-quality-campaign-v1"
CONFIG_KIND = "mumble-input-enhancement-e2e-campaign-config-v1"
RECEIPT_KIND = "mumble-input-enhancement-e2e-case-receipt-v1"
METRICS_RUNTIME_KIND = "mumble-audio-metrics-runtime-attestation-v1"
SAMPLE_RATE_HZ = 48_000
MASTER_CASE_COUNT = 500
CORE_PROFILES = ("Original", "Light", "Balanced", "Quality", "VoiceFocus")
REPORT_FILENAMES = {
	"candidate_adapter_result": "candidate-adapter-result.json",
	"control_adapter_result": "control-adapter-result.json",
	"control_fixed_timeline_score": "control-fixed-timeline-score.json",
	"control_pre_opus_fixed_timeline_score": "control-pre-opus-fixed-timeline-score.json",
	"e2e_manifest": "e2e-manifest.json",
	"edge_adapter_result": "edge-adapter-result.json",
	"edge_fixed_timeline_score": "edge-fixed-timeline-score.json",
	"objective_score": "objective-quality.json",
	"original_adapter_result": "original-adapter-result.json",
	"route_fixed_timeline_score": "route-fixed-timeline-score.json",
}
PUBLIC_ARTIFACT_FILENAMES = {
	"case_evidence_jsonl": "case-evidence.jsonl",
	"failure_spectrogram_index": "failure-spectrogram-index.json",
	"junit": "quality-results.xml",
	"measurement_index_json": "measurement-index.json",
	"per_case_csv": "per-case.csv",
	"per_case_parquet": "per-case.parquet",
	"summary_html": "summary.html",
	"summary_json": "summary.json",
}
HASH_RE = re.compile(r"^[0-9a-f]{64}$")
IDENTIFIER_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._:-]{0,127}$")
AUDIO_SUFFIXES = {
	".aac", ".aiff", ".alac", ".au", ".flac", ".m4a", ".mp3", ".ogg", ".opus", ".pcm", ".raw", ".wav",
}


def _module(name: str, filename: str) -> ModuleType:
	path = SCRIPT_DIR / filename
	spec = importlib.util.spec_from_file_location(name, path)
	if spec is None or spec.loader is None:
		raise CampaignError(f"unable to load required module {path}")
	module = importlib.util.module_from_spec(spec)
	sys.modules[name] = module
	spec.loader.exec_module(module)
	return module


# Add the tracked directory for ordinary imports performed by dynamically
# loaded modules (for example objective_quality_score from measurement_evidence).
sys.path.insert(0, str(SCRIPT_DIR))
PAYLOAD = _module("e2e_campaign_payload_identity", "payload_identity.py")
LOCK = _module("e2e_campaign_corpus_lock", "validate-corpus-lock.py")
INVENTORY = _module("e2e_campaign_inventory", "corpus-inventory-v3.py")
PLAN = _module("e2e_campaign_plan", "generate-mixture-plan.py")
RENDER = _module("e2e_campaign_renderer", "render-mixture-plan.py")
OBJECTIVE = _module("e2e_campaign_objective", "objective_quality_score.py")
MEASUREMENT = _module("e2e_campaign_measurement", "measurement_evidence.py")
CASE_EVIDENCE = _module("e2e_campaign_case_evidence", "quality_case_evidence.py")
QUALIFICATION = _module("e2e_campaign_qualification", "validate-quality-qualification.py")
OFFLINE = _module("e2e_campaign_offline_helpers", "run-offline-quality-campaign.py")


def _expect(condition: bool, path: str, message: str) -> None:
	if not condition:
		raise CampaignError(f"{path}: {message}")


def _mapping(value: Any, path: str) -> Mapping[str, Any]:
	_expect(isinstance(value, dict), path, "expected an object")
	return value


def _exact_keys(value: Mapping[str, Any], required: set[str], optional: set[str], path: str) -> None:
	missing = sorted(required - set(value))
	unknown = sorted(set(value) - required - optional)
	_expect(not missing, path, f"missing keys: {', '.join(missing)}")
	_expect(not unknown, path, f"unknown keys: {', '.join(unknown)}")


def _integer(value: Any, path: str, minimum: int | None = None) -> int:
	_expect(isinstance(value, int) and not isinstance(value, bool), path, "expected an integer")
	if minimum is not None:
		_expect(value >= minimum, path, f"must be >= {minimum}")
	return value


def _hash(value: Any, path: str) -> str:
	_expect(isinstance(value, str) and bool(HASH_RE.fullmatch(value)), path, "invalid lowercase SHA-256")
	return value


def _load_json(path: Path, label: str) -> Any:
	def reject_duplicates(pairs: Sequence[tuple[str, Any]]) -> MutableMapping[str, Any]:
		result: MutableMapping[str, Any] = {}
		for key, value in pairs:
			if key in result:
				raise CampaignError(f"{label}: duplicate JSON key {key!r}")
			result[key] = value
		return result

	try:
		return json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=reject_duplicates)
	except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
		raise CampaignError(f"unable to load {label} {path}: {error}") from error


def _canonical_bytes(value: Any) -> bytes:
	try:
		return json.dumps(
			value, ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=False,
		).encode("utf-8")
	except (TypeError, ValueError) as error:
		raise CampaignError(f"value cannot be represented as canonical finite JSON: {error}") from error


def _canonical_sha256(value: Any) -> str:
	return hashlib.sha256(_canonical_bytes(value)).hexdigest()


def _write_canonical(path: Path, value: Any, *, replace: bool = False) -> None:
	payload = _canonical_bytes(value) + b"\n"
	path.parent.mkdir(parents=True, exist_ok=True)
	if path.exists() and not replace:
		raise CampaignError(f"refusing to replace existing evidence: {path}")
	temporary = path.with_name(f".{path.name}.{os.getpid()}.tmp")
	try:
		temporary.write_bytes(payload)
		os.replace(temporary, path)
	finally:
		if temporary.exists():
			temporary.unlink()


def _file_sha256(path: Path) -> str:
	return str(PAYLOAD.file_sha256(path))


def _is_reparse(path: Path) -> bool:
	try:
		metadata = os.lstat(path)
	except OSError as error:
		raise CampaignError(f"unable to inspect path {path}: {error}") from error
	return path.is_symlink() or bool(
		getattr(metadata, "st_file_attributes", 0)
		& getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
	)


def _regular_file(path: Path, label: str) -> Path:
	resolved = Path(os.path.abspath(os.fspath(path)))
	_expect(resolved.is_file() and not _is_reparse(resolved), label, f"missing regular non-reparse file: {resolved}")
	return resolved


def _real_directory(path: Path, label: str) -> Path:
	resolved = Path(os.path.abspath(os.fspath(path)))
	_expect(resolved.is_dir() and not _is_reparse(resolved), label, f"missing real directory: {resolved}")
	return resolved


def _safe_relative(value: Any, path: str, *, allow_dot: bool = False) -> PurePosixPath:
	_expect(isinstance(value, str) and bool(value), path, "expected a non-empty relative path")
	parsed = PurePosixPath(value)
	_expect(value == parsed.as_posix() and not parsed.is_absolute(), path, "must be normalized and relative")
	_expect(".." not in parsed.parts, path, "parent traversal is forbidden")
	if not allow_dot:
		_expect("." not in parsed.parts, path, "dot path segments are forbidden")
	return parsed


def _below(root: Path, relative: str | PurePosixPath, label: str, *, require_exists: bool = True) -> Path:
	parsed = relative if isinstance(relative, PurePosixPath) else _safe_relative(relative, label, allow_dot=True)
	candidate = Path(os.path.abspath(os.fspath(root.joinpath(*parsed.parts))))
	try:
		candidate.relative_to(root)
	except ValueError as error:
		raise CampaignError(f"{label}: path escapes protected root") from error
	if require_exists:
		_expect(candidate.exists(), label, f"missing path: {candidate}")
		current = root
		for part in candidate.relative_to(root).parts:
			current /= part
			_expect(not _is_reparse(current), label, f"path traverses reparse point: {current}")
	return candidate


def _reference(path: Path, artifact_root: Path) -> Mapping[str, Any]:
	resolved = _regular_file(path, "audio-free artifact")
	try:
		relative = resolved.relative_to(artifact_root).as_posix()
	except ValueError as error:
		raise CampaignError(f"audio-free artifact escapes output root: {resolved}") from error
	_expect(resolved.suffix.lower() not in AUDIO_SUFFIXES, "audio-free artifact", f"audio suffix is forbidden: {relative}")
	return {
		"contains_audio_samples": False,
		"path": relative,
		"sha256": _file_sha256(resolved),
		"size_bytes": resolved.stat().st_size,
	}


def _safe_remove_tree(root: Path, target: Path, label: str) -> None:
	root = Path(os.path.abspath(os.fspath(root)))
	target = Path(os.path.abspath(os.fspath(target)))
	try:
		relative = target.relative_to(root)
	except ValueError as error:
		raise CampaignError(f"{label}: deletion target escapes private root") from error
	_expect(bool(relative.parts), label, "refusing to remove the private root itself")
	if not target.exists():
		return
	_expect(target.is_dir() and not _is_reparse(target), label, "target must be a real directory")
	for current, directories, files in os.walk(target):
		base = Path(current)
		for name in [*directories, *files]:
			_expect(not _is_reparse(base / name), label, f"target contains a reparse point: {base / name}")
	shutil.rmtree(target)
	_expect(not target.exists(), label, "private tree remains after deletion")


def _stable_file_record(path: Path, expected: Mapping[str, Any] | None = None, label: str = "file") -> Mapping[str, Any]:
	resolved = _regular_file(path, label)
	record = {"path": str(resolved), "sha256": _file_sha256(resolved), "size_bytes": resolved.stat().st_size}
	if expected is not None:
		_expect(record["sha256"] == expected.get("sha256"), f"{label}.sha256", "changed or does not match pin")
		_expect(record["size_bytes"] == expected.get("size_bytes"), f"{label}.size_bytes", "changed or does not match pin")
	return record


def _git_head(source_root: Path) -> str:
	completed = subprocess.run(
		["git", "rev-parse", "HEAD"], cwd=source_root, check=False, text=True,
		stdout=subprocess.PIPE, stderr=subprocess.PIPE,
	)
	_expect(completed.returncode == 0, "source root", f"git rev-parse failed: {completed.stderr.strip()}")
	value = completed.stdout.strip().lower()
	_expect(bool(re.fullmatch(r"[0-9a-f]{40}", value)), "source root", "invalid Git HEAD")
	return value


def _config_file_record(root: Path, value: Any, label: str) -> tuple[Path, Mapping[str, Any]]:
	record = _mapping(value, label)
	_exact_keys(record, {"relative_path", "sha256", "size_bytes"}, set(), label)
	relative = _safe_relative(record["relative_path"], f"{label}.relative_path")
	path = _below(root, relative, label)
	_expect(path.is_file(), label, "must identify a file")
	_hash(record["sha256"], f"{label}.sha256")
	_integer(record["size_bytes"], f"{label}.size_bytes", 1)
	_stable_file_record(path, record, label)
	return path, dict(record)


def _expand_adapter_arguments(
	value: Any, roots: Mapping[str, Path], release_fixtures: Path,
) -> list[str]:
	_expect(isinstance(value, list), "campaign config.adapter.arguments", "expected an array")
	arguments: list[str] = []
	for index, raw in enumerate(value):
		path = f"campaign config.adapter.arguments[{index}]"
		spec = _mapping(raw, path)
		kind = spec.get("kind")
		if kind == "literal":
			_exact_keys(spec, {"kind", "value"}, set(), path)
			literal = spec["value"]
			_expect(
				isinstance(literal, str) and bool(literal)
				and not any(character in literal for character in ("/", "\\", ":", "{", "}")),
				f"{path}.value", "literal arguments may not encode paths or placeholders",
			)
			arguments.append(literal)
		elif kind == "protected_path":
			_exact_keys(spec, {"kind", "root", "relative_path"}, set(), path)
			root_name = spec["root"]
			_expect(root_name in roots, f"{path}.root", "unknown protected root")
			relative = _safe_relative(spec["relative_path"], f"{path}.relative_path", allow_dot=True)
			candidate = _below(roots[str(root_name)], relative, path)
			arguments.append(str(candidate))
		else:
			raise CampaignError(f"{path}.kind: must be literal or protected_path")
	_expect(len(arguments) <= 64, "campaign config.adapter.arguments", "too many arguments")
	# Every runner-local executable/wrapper/cache path must resolve below one of
	# the already hash-protected roots.  In particular there is no free-form
	# absolute path escape hidden in this list.
	return arguments


def _load_campaign_config(
	release_fixtures: Path, metrics_runtime: Path, protected_roots: Mapping[str, Path], *, self_test: bool = False,
) -> Mapping[str, Any]:
	config_path = _regular_file(release_fixtures / "quality-campaign-config.json", "quality campaign config")
	config = _mapping(_load_json(config_path, "quality campaign config"), "quality campaign config")
	_exact_keys(
		config,
		{
			"adapter", "case_timeout_seconds", "corpus_root_relative_path", "kind", "metrics_manifest",
			"metrics_python", "orchestrator_python", "render_jobs", "schema_version",
			"input_enhancement_policy_manifest", "input_enhancement_policy_signature",
		},
		set(),
		"quality campaign config",
	)
	_expect(config["schema_version"] == 1 and config["kind"] == CONFIG_KIND, "quality campaign config", "unsupported schema/kind")
	jobs = _integer(config["render_jobs"], "quality campaign config.render_jobs", 1)
	_expect(jobs <= 32, "quality campaign config.render_jobs", "must be <= 32")
	timeout = _integer(config["case_timeout_seconds"], "quality campaign config.case_timeout_seconds", 60)
	_expect(timeout <= 3600, "quality campaign config.case_timeout_seconds", "must be <= 3600")
	corpus_relative = _safe_relative(
		config["corpus_root_relative_path"], "quality campaign config.corpus_root_relative_path", allow_dot=True,
	)

	orchestrator_python, orchestrator_pin = _config_file_record(
		metrics_runtime, config["orchestrator_python"], "quality campaign config.orchestrator_python",
	)
	metrics_python, metrics_python_pin = _config_file_record(
		metrics_runtime, config["metrics_python"], "quality campaign config.metrics_python",
	)
	metrics_manifest, metrics_manifest_pin = _config_file_record(
		metrics_runtime, config["metrics_manifest"], "quality campaign config.metrics_manifest",
	)
	policy_manifest, policy_manifest_pin = _config_file_record(
		release_fixtures, config["input_enhancement_policy_manifest"],
		"quality campaign config.input_enhancement_policy_manifest",
	)
	policy_signature, policy_signature_pin = _config_file_record(
		release_fixtures, config["input_enhancement_policy_signature"],
		"quality campaign config.input_enhancement_policy_signature",
	)
	_expect(
		policy_signature.stat().st_size == 64,
		"quality campaign config.input_enhancement_policy_signature",
		"must be a raw 64-byte Ed25519 signature",
	)
	_expect(
		isinstance(_load_json(policy_manifest, "input-enhancement policy manifest"), dict),
		"quality campaign config.input_enhancement_policy_manifest",
		"must be a JSON object",
	)
	if not self_test:
		_expect(
			Path(sys.executable).resolve() == orchestrator_python.resolve(),
			"quality campaign config.orchestrator_python",
			"the trusted external harness must launch this campaign with the pinned interpreter",
		)

	adapter_value = _mapping(config["adapter"], "quality campaign config.adapter")
	_exact_keys(adapter_value, {"arguments", "relative_path", "sha256", "size_bytes"}, set(), "quality campaign config.adapter")
	adapter, adapter_pin = _config_file_record(release_fixtures, {
		"relative_path": adapter_value["relative_path"],
		"sha256": adapter_value["sha256"],
		"size_bytes": adapter_value["size_bytes"],
	}, "quality campaign config.adapter")
	arguments = _expand_adapter_arguments(adapter_value["arguments"], protected_roots, release_fixtures)
	return {
		"path": config_path,
		"document": config,
		"render_jobs": jobs,
		"case_timeout_seconds": timeout,
		"corpus_root_relative_path": corpus_relative,
		"orchestrator_python": orchestrator_python,
		"orchestrator_python_pin": orchestrator_pin,
		"metrics_python": metrics_python,
		"metrics_python_pin": metrics_python_pin,
		"metrics_manifest": metrics_manifest,
		"metrics_manifest_pin": metrics_manifest_pin,
		"input_enhancement_policy_manifest": policy_manifest,
		"input_enhancement_policy_manifest_pin": policy_manifest_pin,
		"input_enhancement_policy_signature": policy_signature,
		"input_enhancement_policy_signature_pin": policy_signature_pin,
		"adapter": adapter,
		"adapter_pin": adapter_pin,
		"adapter_arguments": arguments,
	}


def _source_critical_records(source_root: Path) -> Mapping[str, Mapping[str, Any]]:
	names = (
		"corpus-inventory-v3.py",
		"generate-mixture-plan.py",
		"measurement_evidence.py",
		"objective_quality_score.py",
		"payload_identity.py",
		"quality_case_evidence.py",
		"render-mixture-plan.py",
		"run-e2e-quality-campaign.py",
		"run-offline-quality-campaign.py",
		"run-two-client-e2e.py",
		"score-fixed-timeline.py",
		"score-objective-quality.py",
		"validate-corpus-lock.py",
		"validate-quality-qualification.py",
		"write-quality-parquet.py",
	)
	root = source_root / "scripts" / "audio-quality"
	return {name: _stable_file_record(root / name, label=f"tracked tool {name}") for name in names}


def _critical_files_stable(context: Mapping[str, Any]) -> None:
	_expect(_git_head(context["source_root"]) == context["source_sha"], "source root", "Git HEAD changed during campaign")
	for name, record in context["source_critical_records"].items():
		_stable_file_record(
			context["source_root"] / "scripts" / "audio-quality" / name,
			record,
			f"tracked tool {name}",
		)
	for label in (
		"adapter", "metrics_python", "metrics_manifest", "orchestrator_python",
		"input_enhancement_policy_manifest", "input_enhancement_policy_signature",
	):
		_stable_file_record(context["config"][label], context["config"][f"{label}_pin"], label)


def _assert_protected_payloads_stable(context: Mapping[str, Any]) -> None:
	for label, path, expected in (
		("staged client payload", context["staged_client_root"], context["build"]["staged_payload_sha256"]),
		("release fixtures", context["release_fixtures"], context["build"]["release_fixtures_sha256"]),
		("metrics runtime", context["metrics_runtime"], context["build"]["metrics_runtime_sha256"]),
	):
		actual = str(PAYLOAD.payload_sha256(path))
		_expect(actual == expected, label, "protected directory changed during campaign")
	for label, path, field in (
		("tested binary", context["tested_binary"], "tested_binary_sha256"),
		("legacy binary", context["legacy_binary"], "legacy_binary_sha256"),
		("server binary", context["server_binary"], "server_binary_sha256"),
		("corpus inventory", context["inventory_path"], "corpus_inventory_sha256"),
		("case set", context["case_set_path"], "case_set_sha256"),
		("mixture plan", context["plan_path"], "mixture_plan_sha256"),
	):
		_expect(_file_sha256(path) == context["build"][field], label, "protected file changed during campaign")


def _condition(case: Mapping[str, Any]) -> str:
	if case["noise"] is None:
		return "clean"
	return "severe" if float(case["mix"]["snr_db"]) <= 0.0 else "noisy"


def _safe_identifier(value: Any, prefix: str) -> str:
	text = str(value)
	if IDENTIFIER_RE.fullmatch(text):
		return text
	digest = hashlib.sha256(text.encode("utf-8")).hexdigest()[:20]
	return f"{prefix}-{digest}"


def _metadata(case: Mapping[str, Any]) -> Mapping[str, Any]:
	condition = _condition(case)
	noise = case["noise"]
	noise_class = None if noise is None else _safe_identifier(noise["class"], "noise-class")
	cohort_parts = [
		condition,
		str(case["speech"]["language"]),
		"clean" if noise is None else str(noise["class"]),
		"none" if noise is None else str(case["mix"]["snr_db"]),
		str(case["mix"]["microphone_response"]["device_family"]),
	]
	return {
		"condition": condition,
		"cohort_id": _safe_identifier("-".join(cohort_parts), "cohort"),
		"speaker_group_id": _safe_identifier(case["speech"]["group_id"], "speaker"),
		"noise_group_id": None if noise is None else _safe_identifier(noise["group_id"], "noise"),
		"noise_class": noise_class,
		"rir_group_id": _safe_identifier(case["mix"]["rir"]["group_id"], "rir"),
		"device_group_id": _safe_identifier(case["mix"]["microphone_response"]["group_id"], "device"),
		"language": str(case["speech"]["language"]),
		"startup_preroll_ms": int(case["startup"]["preroll_ms"]),
	}


def _build_context(args: argparse.Namespace) -> Mapping[str, Any]:
	_expect(args.suite == "master_quality", "--suite", "this campaign currently qualifies only the 500-case master_quality suite")
	source_root = _real_directory(args.source_root, "source root")
	output_root = Path(os.path.abspath(os.fspath(args.output_root)))
	output_root.mkdir(parents=True, exist_ok=True)
	_expect(output_root.is_dir() and not _is_reparse(output_root), "output root", "must be a real directory")
	source_sha = str(args.source_sha).lower()
	_expect(bool(re.fullmatch(r"[0-9a-f]{40}", source_sha)), "--source-sha", "invalid Git SHA")
	_expect(_git_head(source_root) == source_sha, "source root", "HEAD does not match --source-sha")

	corpus_lock_path = _regular_file(args.corpus_lock, "corpus lock")
	tested_binary = _regular_file(args.tested_binary, "tested binary")
	legacy_binary = _regular_file(args.legacy_binary, "legacy binary")
	staged_client_root = _real_directory(args.staged_client_root, "staged client root")
	server_binary = _regular_file(args.server_binary, "server binary")
	inventory_path = _regular_file(args.corpus_inventory, "corpus inventory")
	case_set_path = _regular_file(args.case_set, "protected case set")
	plan_path = _regular_file(args.mixture_plan, "mixture plan")
	release_fixtures = _real_directory(args.release_fixtures, "release fixtures")
	metrics_runtime = _real_directory(args.metrics_runtime, "metrics runtime")
	model_manifest_input = _regular_file(args.model_manifest, "supplied model manifest")
	recipe_manifest_input = _regular_file(args.recipe_manifest, "supplied recipe manifest")
	try:
		tested_binary.relative_to(staged_client_root)
	except ValueError as error:
		raise CampaignError("tested binary must be below the staged client root") from error
	_expect(tested_binary == staged_client_root / "mumble.exe", "tested binary", "must be the packaged root mumble.exe")
	stage_model_manifest = _regular_file(staged_client_root / "input-models.json", "staged input-models.json")
	stage_recipe_manifest = _regular_file(staged_client_root / "input-recipes.json", "staged input-recipes.json")
	_expect(_file_sha256(stage_model_manifest) == _file_sha256(model_manifest_input), "model manifest", "supplied and staged bytes differ")
	_expect(_file_sha256(stage_recipe_manifest) == _file_sha256(recipe_manifest_input), "recipe manifest", "supplied and staged bytes differ")

	protected_roots = {
		"source_root": source_root,
		"staged_client_root": staged_client_root,
		"server_binary_parent": server_binary.parent,
		"legacy_binary_parent": legacy_binary.parent,
		"release_fixtures": release_fixtures,
		"metrics_runtime": metrics_runtime,
		"corpus_inventory_parent": inventory_path.parent,
	}
	config = _load_campaign_config(release_fixtures, metrics_runtime, protected_roots)
	corpus_root = _below(
		inventory_path.parent,
		config["corpus_root_relative_path"],
		"quality campaign config.corpus_root_relative_path",
	)
	_expect(corpus_root.is_dir(), "corpus root", "must identify a directory")

	lock = LOCK.load_validated_manifest(corpus_lock_path)
	inventory = _mapping(_load_json(inventory_path, "corpus inventory"), "corpus inventory")
	INVENTORY.validate_inventory(inventory, lock, require_release=True)
	plan = PLAN.validate_plan(_load_json(plan_path, "mixture plan"))
	_expect(plan["suite"] == "master_quality", "mixture plan.suite", "must be master_quality")
	_expect(plan["split"] == "validation", "mixture plan.split", "master qualification must use the frozen validation split")
	_expect(len(plan["cases"]) == MASTER_CASE_COUNT, "mixture plan.cases", f"requires exactly {MASTER_CASE_COUNT} cases")
	_expect(plan["corpus_lock_sha256"] == LOCK.canonical_manifest_sha256(lock), "mixture plan.corpus_lock_sha256", "lock mismatch")
	_expect(plan["corpus_inventory_sha256"] == INVENTORY.canonical_sha256(inventory), "mixture plan.corpus_inventory_sha256", "inventory mismatch")
	_expect({str(case["profile"]) for case in plan["cases"]} == set(CORE_PROFILES), "mixture plan.profiles", "must cover all five core profiles")

	models_manifest, recipes_manifest, models, recipes = OFFLINE._validate_package(
		staged_client_root, stage_model_manifest, stage_recipe_manifest,
	)
	try:
		verified_metrics_runtime = dict(
			OBJECTIVE.verify_metrics_runtime(metrics_runtime, config["metrics_manifest"], verify_environment=False)
		)
	except Exception as error:
		raise CampaignError(f"pinned metrics runtime failed verification: {error}") from error
	verified_metrics_runtime.pop("runtime_root", None)

	staged_payload_sha256 = str(PAYLOAD.payload_sha256(staged_client_root))
	release_fixtures_sha256 = str(PAYLOAD.payload_sha256(release_fixtures))
	metrics_runtime_sha256 = str(PAYLOAD.payload_sha256(metrics_runtime))
	_expect(metrics_runtime_sha256 == _canonical_sha256(PAYLOAD.payload_tree_records(metrics_runtime)), "metrics runtime", "payload identity mismatch")
	harness_sha256 = _hash(args.harness_sha256, "--harness-sha256")
	hardware_sha256 = _hash(args.hardware_fingerprint_sha256, "--hardware-fingerprint-sha256")
	_expect(args.runner_class in ("low-performance", "mainstream", "local-development"), "--runner-class", "unsupported class")
	product_model_ids = sorted({
		str(model_id)
		for profile in CORE_PROFILES
		for model_id in OFFLINE._public_recipe(profile, recipes)["modelIds"]
	})
	model_hashes = sorted({str(models[model_id]["sha256"]) for model_id in product_model_ids})
	_expect(bool(model_hashes), "product model hashes", "at least one packaged model is required")
	build = {
		"git_sha": source_sha,
		"tested_binary_sha256": _file_sha256(tested_binary),
		"staged_payload_sha256": staged_payload_sha256,
		"legacy_binary_sha256": _file_sha256(legacy_binary),
		"server_binary_sha256": _file_sha256(server_binary),
		"harness_sha256": harness_sha256,
		"hardware_fingerprint_sha256": hardware_sha256,
		"runner_class": args.runner_class,
		"corpus_lock_sha256": LOCK.canonical_manifest_sha256(lock),
		"corpus_inventory_sha256": _file_sha256(inventory_path),
		"mixture_plan_sha256": _file_sha256(plan_path),
		"case_set_sha256": _file_sha256(case_set_path),
		"release_fixtures_sha256": release_fixtures_sha256,
		"metrics_runtime_sha256": metrics_runtime_sha256,
		"model_manifest_sha256": _file_sha256(stage_model_manifest),
		"recipe_manifest_sha256": _file_sha256(stage_recipe_manifest),
		"recipe_set_version": str(recipes_manifest["catalogRevision"]),
		"model_hashes": model_hashes,
	}
	profile_bindings = OFFLINE._profile_bindings({
		"recipe_manifest_path": stage_recipe_manifest,
		"recipes_manifest": recipes_manifest,
		"recipes": recipes,
		"models": models,
	})
	profile_binding_map = MEASUREMENT._validate_profile_bindings(profile_bindings, "core", build)

	state_root = output_root / "_quality-campaign-state" / f"master_quality-{args.runner_class}"
	public_root = output_root / "artifacts" / f"master_quality-{args.runner_class}"
	state_root.mkdir(parents=True, exist_ok=True)
	_expect(not _is_reparse(state_root), "campaign state root", "must not be a reparse point")
	manifest_path = state_root / "campaign-manifest.json"
	source_records = _source_critical_records(source_root)
	run_binding = {
		"schema_version": 1,
		"kind": CAMPAIGN_KIND,
		"suite": "master_quality",
		"build": build,
		"inputs": {
			"corpus_lock_path": str(corpus_lock_path),
			"corpus_inventory_path": str(inventory_path),
			"case_set_path": str(case_set_path),
			"mixture_plan_path": str(plan_path),
			"staged_client_root": str(staged_client_root),
			"server_binary_path": str(server_binary),
			"release_fixtures_path": str(release_fixtures),
			"metrics_runtime_path": str(metrics_runtime),
		},
		"config_sha256": _file_sha256(config["path"]),
		"source_critical_files": source_records,
	}
	run_binding_sha256 = _canonical_sha256(run_binding)
	if manifest_path.exists():
		previous = _mapping(_load_json(manifest_path, "campaign manifest"), "campaign manifest")
		_expect(previous.get("run_binding_sha256") == run_binding_sha256, "campaign manifest", "run binding changed; use a new output root")
		_expect(previous.get("run_binding") == run_binding, "campaign manifest", "run binding bytes changed")
	else:
		_write_canonical(manifest_path, {
			"schema_version": 1,
			"kind": CAMPAIGN_KIND,
			"status": "initialized",
			"completed_cases": 0,
			"run_binding_sha256": run_binding_sha256,
			"run_binding": run_binding,
		})
	context = {
		"source_root": source_root,
		"source_sha": source_sha,
		"source_critical_records": source_records,
		"output_root": output_root,
		"state_root": state_root,
		"public_root": public_root,
		"manifest_path": manifest_path,
		"run_binding": run_binding,
		"run_binding_sha256": run_binding_sha256,
		"build": build,
		"corpus_lock_path": corpus_lock_path,
		"inventory_path": inventory_path,
		"case_set_path": case_set_path,
		"plan_path": plan_path,
		"tested_binary": tested_binary,
		"legacy_binary": legacy_binary,
		"staged_client_root": staged_client_root,
		"server_binary": server_binary,
		"release_fixtures": release_fixtures,
		"metrics_runtime": metrics_runtime,
		"model_manifest_path": stage_model_manifest,
		"recipe_manifest_path": stage_recipe_manifest,
		"config": config,
		"corpus_root": corpus_root,
		"lock": lock,
		"inventory": inventory,
		"plan": plan,
		"models_manifest": models_manifest,
		"recipes_manifest": recipes_manifest,
		"models": models,
		"recipes": recipes,
		"profile_bindings": profile_bindings,
		"profile_binding_map": profile_binding_map,
		"verified_metrics_runtime": verified_metrics_runtime,
	}
	_critical_files_stable(context)
	_assert_protected_payloads_stable(context)
	return context


def _prepare_render(context: Mapping[str, Any]) -> tuple[Mapping[str, Any], Mapping[str, Mapping[str, Any]]]:
	render_root = context["state_root"] / "rendered"
	manifest_path = render_root / "render-manifest.json"
	if render_root.exists() and not manifest_path.is_file():
		_safe_remove_tree(context["state_root"], render_root, "incomplete private render")
	if not render_root.exists():
		RENDER.render(
			context["plan"], context["corpus_root"], render_root,
			jobs=int(context["config"]["render_jobs"]),
		)
	manifest, entries, _ = OFFLINE._validate_render_manifest(
		context["plan"], manifest_path, render_root,
	)
	return manifest, entries


def _execution_units(plan: Mapping[str, Any]) -> list[tuple[str, ...]]:
	by_scene: dict[str, list[Mapping[str, Any]]] = {}
	paired_ids: set[str] = set()
	for case in plan["cases"]:
		if case.get("comparison_scene_id") is not None:
			by_scene.setdefault(str(case["comparison_scene_id"]), []).append(case)
	units: list[tuple[str, ...]] = []
	for scene in sorted(by_scene):
		rows = by_scene[scene]
		_expect(len(rows) == 2 and {row["profile"] for row in rows} == {"Quality", "VoiceFocus"}, f"paired scene {scene}", "invalid pair")
		quality = next(row for row in rows if row["profile"] == "Quality")
		voice = next(row for row in rows if row["profile"] == "VoiceFocus")
		units.append((str(quality["case_id"]), str(voice["case_id"])))
		paired_ids.update((str(quality["case_id"]), str(voice["case_id"])))
	for case in plan["cases"]:
		case_id = str(case["case_id"])
		if case_id not in paired_ids:
			units.append((case_id,))
	case_by_id = {str(case["case_id"]): case for case in plan["cases"]}
	return sorted(units, key=lambda unit: (min(CORE_PROFILES.index(str(case_by_id[item]["profile"])) for item in unit), unit))


def _next_attempt_root(context: Mapping[str, Any], unit: tuple[str, ...]) -> tuple[int, Path]:
	unit_id = "__".join(unit)
	root = context["state_root"] / "runs" / unit_id
	root.mkdir(parents=True, exist_ok=True)
	_expect(not _is_reparse(root), f"unit {unit_id}", "run root is a reparse point")
	numbers = []
	for child in root.iterdir():
		if child.is_dir() and re.fullmatch(r"attempt-[0-9]{4}", child.name):
			numbers.append(int(child.name.split("-", 1)[1]))
	number = max(numbers, default=0) + 1
	attempt = root / f"attempt-{number:04d}"
	attempt.mkdir()
	return number, attempt


def _subprocess_environment() -> Mapping[str, str]:
	blocked = {"MUMBLE_DISABLE_INPUT_ENHANCEMENT", "PYTHONPATH", "PYTHONHOME"}
	environment = {key: value for key, value in os.environ.items() if key.upper() not in blocked}
	environment.update({
		"PYTHONDONTWRITEBYTECODE": "1",
		"PYTHONNOUSERSITE": "1",
		"HF_HUB_OFFLINE": "1",
		"TRANSFORMERS_OFFLINE": "1",
		"HF_DATASETS_OFFLINE": "1",
	})
	return environment


def _run_command(
	command: Sequence[str], cwd: Path, timeout_seconds: int, stdout_path: Path, label: str,
) -> None:
	stdout_path.parent.mkdir(parents=True, exist_ok=True)
	try:
		completed = subprocess.run(
			list(command), cwd=cwd, env=dict(_subprocess_environment()), check=False,
			text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
			timeout=timeout_seconds,
		)
	except subprocess.TimeoutExpired as error:
		stdout_path.write_text(str(error.stdout or ""), encoding="utf-8")
		raise CampaignError(f"{label}: timed out after {timeout_seconds} seconds") from error
	stdout_path.write_text(completed.stdout, encoding="utf-8")
	if completed.returncode != 0:
		tail = completed.stdout[-4000:].strip()
		raise CampaignError(f"{label}: exited {completed.returncode}: {tail}")


def _run_e2e_unit(
	context: Mapping[str, Any], unit: tuple[str, ...], render_root: Path, render_manifest_path: Path,
) -> tuple[int, Path]:
	number, attempt = _next_attempt_root(context, unit)
	e2e_root = attempt / "e2e"
	command = [
		str(context["config"]["orchestrator_python"]),
		str(context["source_root"] / "scripts" / "audio-quality" / "run-two-client-e2e.py"),
		"--plan", str(context["plan_path"]),
		"--case-id", unit[0],
		"--render-manifest", str(render_manifest_path),
		"--render-root", str(render_root),
		"--runtime-root", str(context["staged_client_root"]),
		"--client-binary", str(context["tested_binary"]),
		"--server-binary", str(context["server_binary"]),
		"--model-manifest", str(context["model_manifest_path"]),
		"--recipe-manifest", str(context["recipe_manifest_path"]),
		"--inventory", str(context["inventory_path"]),
		"--qualification-case-set", str(context["case_set_path"]),
		"--corpus-lock", str(context["corpus_lock_path"]),
		"--metrics-manifest", str(context["config"]["metrics_manifest"]),
		"--input-enhancement-policy-manifest", str(context["config"]["input_enhancement_policy_manifest"]),
		"--input-enhancement-policy-signature", str(context["config"]["input_enhancement_policy_signature"]),
		"--adapter", str(context["config"]["adapter"]),
		"--output-root", str(e2e_root),
	]
	if len(unit) == 2:
		command.extend(["--paired-case-id", unit[1]])
	for argument in context["config"]["adapter_arguments"]:
		command.extend(["--adapter-arg", str(argument)])
	_run_command(
		command,
		context["source_root"] / "scripts" / "audio-quality",
		int(context["config"]["case_timeout_seconds"]) * (2 if len(unit) == 2 else 1),
		attempt / "two-client-e2e.stdout.txt",
		f"two-client E2E unit {'/'.join(unit)}",
	)
	return number, attempt


def _case_private_paths(e2e_root: Path, case: Mapping[str, Any], paired: bool) -> Mapping[str, Path]:
	case_id = str(case["case_id"])
	if paired:
		shared = e2e_root / "shared"
		case_root = e2e_root / "cases" / case_id
		return {
			"candidate_root": case_root / "candidate",
			"edge_root": case_root / "candidate_edge",
			"original_root": shared / "original_comparison",
			"control_root": shared / "control",
			"manifest": case_root / "e2e-manifest.json",
		}
	return {
		"candidate_root": e2e_root / "candidate",
		"edge_root": e2e_root / "candidate_edge",
		"original_root": e2e_root / "original_comparison",
		"control_root": e2e_root / "control",
		"manifest": e2e_root / "e2e-manifest.json",
	}


def _adapter_capture(role_root: Path, document: Mapping[str, Any], label: str) -> Path:
	capture = _mapping(document.get("capture"), f"{label}.capture")
	_exact_keys(capture, {"relative_path", "sha256", "size_bytes"}, set(), f"{label}.capture")
	relative = _safe_relative(capture["relative_path"], f"{label}.capture.relative_path")
	path = _below(role_root, relative, f"{label}.capture")
	_expect(path.is_file() and path.suffix.lower() == ".wav", f"{label}.capture", "must be a private WAV")
	_expect(_file_sha256(path) == _hash(capture["sha256"], f"{label}.capture.sha256"), f"{label}.capture", "hash mismatch")
	_expect(path.stat().st_size == _integer(capture["size_bytes"], f"{label}.capture.size_bytes", 1), f"{label}.capture", "size mismatch")
	return path


def _render_audio_path(render_root: Path, entry: Mapping[str, Any], role: str, label: str) -> Path:
	record = _mapping(entry[role], f"{label}.{role}")
	path = _below(render_root, _safe_relative(record["path"], f"{label}.{role}.path"), f"{label}.{role}")
	_expect(_file_sha256(path) == record["sha256"], f"{label}.{role}", "rendered hash mismatch")
	return path


def _run_objective_score(
	context: Mapping[str, Any], case: Mapping[str, Any], render_entry: Mapping[str, Any],
	render_root: Path, private: Mapping[str, Path], attempt: Path,
) -> Path:
	case_id = str(case["case_id"])
	candidate_document = _mapping(_load_json(private["candidate_root"] / "adapter-result.json", f"{case_id} candidate adapter"), "candidate adapter")
	original_document = _mapping(_load_json(private["original_root"] / "adapter-result.json", f"{case_id} Original adapter"), "Original adapter")
	control_document = _mapping(_load_json(private["control_root"] / "adapter-result.json", f"{case_id} control adapter"), "control adapter")
	candidate_capture = _adapter_capture(private["candidate_root"], candidate_document, f"{case_id} candidate")
	original_capture = _adapter_capture(private["original_root"], original_document, f"{case_id} Original")
	control_capture = _adapter_capture(private["control_root"], control_document, f"{case_id} control")
	diagnostics = _mapping(candidate_document.get("diagnostics"), f"{case_id} candidate diagnostics")
	latency = _integer(diagnostics.get("declared_latency_samples"), f"{case_id} declared latency", 0)
	clean_path = _render_audio_path(render_root, render_entry, "clean_reference", case_id)
	metadata = _metadata(case)
	language = str(metadata["language"]).split("-", 1)[0].casefold()
	_expect(language in ("en", "sv"), f"{case_id}.language", "objective WER supports only English/Swedish")
	reference_key = str(case.get("comparison_scene_id") or case_id)
	reference_key = _safe_identifier(reference_key, "reference")
	private_reference = context["state_root"] / "private-references" / f"{reference_key}.json"
	private_reference.parent.mkdir(parents=True, exist_ok=True)
	objective_path = attempt / "objective" / case_id / "objective-quality.json"
	command = [
		str(context["config"]["metrics_python"]),
		str(context["source_root"] / "scripts" / "audio-quality" / "score-objective-quality.py"),
		"--case-id", case_id,
		"--profile", str(case["profile"]),
		"--condition", str(metadata["condition"]),
		"--dataset-split", str(context["plan"]["split"]),
		"--signal-stage", "receiver-capture",
		"--clean-reference", str(clean_path),
		"--noisy-original", str(original_capture),
		"--candidate", str(candidate_capture),
		"--original-latency-samples", "0",
		"--candidate-latency-samples", str(latency),
		"--route-control-wav", str(control_capture),
		"--route-control-score", str(private["control_root"] / "fixed-timeline-score.json"),
		"--candidate-fixed-timeline-score", str(private["candidate_root"] / "fixed-timeline-score.json"),
		"--route-e2e-manifest", str(private["manifest"]),
		"--metrics-runtime-root", str(context["metrics_runtime"]),
		"--metrics-manifest", str(context["config"]["metrics_manifest"]),
		"--language", language,
		"--wer-reference-kind", "clean-asr-consistency",
		"--clean-asr-reference", str(private_reference),
		"--output", str(objective_path),
	]
	_run_command(
		command,
		context["source_root"] / "scripts" / "audio-quality",
		int(context["config"]["case_timeout_seconds"]),
		attempt / "objective" / case_id / "objective.stdout.txt",
		f"objective receiver score {case_id}",
	)
	objective = _mapping(_load_json(objective_path, f"{case_id} objective score"), f"{case_id} objective score")
	try:
		OBJECTIVE.validate_score_document(objective)
	except Exception as error:
		raise CampaignError(f"{case_id} objective score failed strict validation: {error}") from error
	_expect(objective.get("status") == "passed", f"{case_id} objective score.status", "must pass")
	_expect(objective["alignment"]["signal_stage"] == "receiver-capture", f"{case_id} objective score", "wrong stage")
	_expect(objective["alignment"]["correlation_search_used"] is False, f"{case_id} objective score", "correlation alignment is forbidden")
	return objective_path


def _report_sources(private: Mapping[str, Path], objective_path: Path) -> Mapping[str, Path]:
	return {
		"candidate_adapter_result": private["candidate_root"] / "adapter-result.json",
		"control_adapter_result": private["control_root"] / "adapter-result.json",
		"control_fixed_timeline_score": private["control_root"] / "fixed-timeline-score.json",
		"control_pre_opus_fixed_timeline_score": private["control_root"] / "pre-opus-fixed-timeline-score.json",
		"e2e_manifest": private["manifest"],
		"edge_adapter_result": private["edge_root"] / "adapter-result.json",
		"edge_fixed_timeline_score": private["edge_root"] / "pre-opus-fixed-timeline-score.json",
		"objective_score": objective_path,
		"original_adapter_result": private["original_root"] / "adapter-result.json",
		"route_fixed_timeline_score": private["candidate_root"] / "fixed-timeline-score.json",
	}


def _reject_public_json_leaks(value: Any, path: str = "report") -> None:
	if isinstance(value, dict):
		for key, item in value.items():
			_reject_public_json_leaks(item, f"{path}.{key}")
	elif isinstance(value, list):
		for index, item in enumerate(value):
			_reject_public_json_leaks(item, f"{path}[{index}]")
	elif isinstance(value, str):
		# Relative private WAV locators inside adapter-result.json are required by
		# the semantic verifier, but host paths, UNC paths, and file URIs are not.
		windows_absolute = bool(re.match(r"^[A-Za-z]:[\\/]", value)) or value.startswith("\\\\")
		posix_absolute = value.startswith("/")
		_expect(not windows_absolute and not posix_absolute and not value.casefold().startswith("file:"), path, "absolute/private host path leaked into public evidence")


def _case_stub(case: Mapping[str, Any]) -> Mapping[str, Any]:
	metadata = _metadata(case)
	return {
		"case_id": str(case["case_id"]),
		"profile": str(case["profile"]),
		"condition": metadata["condition"],
		"dataset_split": "validation",
	}


def _documents_and_references(
	context: Mapping[str, Any], case: Mapping[str, Any], case_root: Path,
) -> tuple[Mapping[str, Mapping[str, Any]], Mapping[str, Mapping[str, Any]], Mapping[str, Any]]:
	documents: dict[str, Mapping[str, Any]] = {}
	references: dict[str, Mapping[str, Any]] = {}
	for name, filename in REPORT_FILENAMES.items():
		path = _regular_file(case_root / filename, f"{case['case_id']} retained {name}")
		document = _mapping(_load_json(path, f"{case['case_id']} retained {name}"), f"{case['case_id']} retained {name}")
		_reject_public_json_leaks(document, f"{case['case_id']}.{name}")
		documents[name] = document
		references[name] = _reference(path, context["output_root"])
	manifest_binding = _mapping(documents["e2e_manifest"].get("qualification_binding"), f"{case['case_id']} E2E qualification binding")
	_expect(
		manifest_binding.get("input_enhancement_policy_manifest_sha256")
		== context["config"]["input_enhancement_policy_manifest_pin"]["sha256"],
		f"{case['case_id']} E2E policy manifest",
		"does not bind the trusted release-fixture policy",
	)
	_expect(
		manifest_binding.get("input_enhancement_policy_signature_sha256")
		== context["config"]["input_enhancement_policy_signature_pin"]["sha256"],
		f"{case['case_id']} E2E policy signature",
		"does not bind the trusted release-fixture signature",
	)
	objective = documents["objective_score"]
	derived = MEASUREMENT._derive_e2e_case(
		_case_stub(case), objective, references, documents, context["build"],
		context["profile_binding_map"], f"campaign case {case['case_id']}",
	)
	return references, documents, derived


def _unit_receipt_path(context: Mapping[str, Any], unit: tuple[str, ...]) -> Path:
	digest = hashlib.sha256("\0".join(unit).encode("utf-8")).hexdigest()[:24]
	return context["state_root"] / "receipts" / f"{digest}.json"


def _public_case_root(context: Mapping[str, Any], case: Mapping[str, Any]) -> Path:
	return context["public_root"] / "measurements" / str(case["profile"]) / str(case["case_id"])


def _load_unit_receipt(
	context: Mapping[str, Any], unit: tuple[str, ...], case_by_id: Mapping[str, Mapping[str, Any]],
) -> Mapping[str, Mapping[str, Any]] | None:
	path = _unit_receipt_path(context, unit)
	if not path.is_file():
		return None
	receipt = _mapping(_load_json(path, "unit receipt"), "unit receipt")
	_exact_keys(
		receipt,
		{"cases", "kind", "run_binding_sha256", "schema_version", "unit", "unit_execution_sha256"},
		set(),
		"unit receipt",
	)
	_expect(receipt["schema_version"] == 1 and receipt["kind"] == RECEIPT_KIND, "unit receipt", "unsupported schema/kind")
	_expect(receipt["run_binding_sha256"] == context["run_binding_sha256"], "unit receipt", "campaign binding mismatch")
	_expect(receipt["unit"] == list(unit), "unit receipt.unit", "unit mismatch")
	_hash(receipt["unit_execution_sha256"], "unit receipt.unit_execution_sha256")
	rows = receipt["cases"]
	_expect(isinstance(rows, list) and len(rows) == len(unit), "unit receipt.cases", "case count mismatch")
	result: dict[str, Mapping[str, Any]] = {}
	for index, row_value in enumerate(rows):
		row = _mapping(row_value, f"unit receipt.cases[{index}]")
		_exact_keys(
			row,
			{"case_id", "derived", "objective_runtime_binding_sha256", "plan_case_sha256", "profile", "render_entry_sha256", "reports"},
			set(),
			f"unit receipt.cases[{index}]",
		)
		case_id = str(row["case_id"])
		_expect(case_id in unit and case_id not in result, f"unit receipt.cases[{index}].case_id", "unknown or duplicate")
		case = case_by_id[case_id]
		_expect(row["profile"] == case["profile"], f"unit receipt.cases[{index}].profile", "profile mismatch")
		_expect(row["plan_case_sha256"] == _canonical_sha256(case), f"unit receipt.cases[{index}].plan_case_sha256", "plan changed")
		case_root = _public_case_root(context, case)
		_expect(case_root.is_dir() and not _is_reparse(case_root), f"unit receipt {case_id}", "public report directory is missing")
		observed_names = sorted(path.name for path in case_root.iterdir() if path.is_file())
		_expect(observed_names == sorted(REPORT_FILENAMES.values()), f"unit receipt {case_id}", "public report set is incomplete or contains extras")
		references, documents, derived = _documents_and_references(context, case, case_root)
		_expect(row["plan_case_sha256"] == derived["plan_case_sha256"], f"unit receipt {case_id}", "E2E plan binding differs")
		_expect(row["render_entry_sha256"] == derived["render_entry_sha256"], f"unit receipt {case_id}", "E2E render binding differs")
		_expect(row["reports"] == references, f"unit receipt {case_id}.reports", "retained report bytes changed")
		_expect(row["derived"] == derived, f"unit receipt {case_id}.derived", "report-derived measurement changed")
		objective_runtime = MEASUREMENT.canonical_json_sha256({
			"runtime": documents["objective_score"]["runtime"],
			"scorer_files": documents["objective_score"]["scorer_files"],
		})
		_expect(row["objective_runtime_binding_sha256"] == objective_runtime, f"unit receipt {case_id}", "objective runtime binding changed")
		manifest_execution = str(documents["e2e_manifest"]["run_provenance_sha256"])
		_expect(manifest_execution == receipt["unit_execution_sha256"], f"unit receipt {case_id}", "unit execution provenance differs")
		result[case_id] = {
			"case": case,
			"references": references,
			"documents": documents,
			"derived": derived,
			"render_entry_sha256": row["render_entry_sha256"],
			"objective_runtime_binding_sha256": objective_runtime,
		}
	_expect(list(result) == list(unit), "unit receipt.cases", "must use unit order")
	return result


def _remove_uncommitted_public_unit(
	context: Mapping[str, Any], unit: tuple[str, ...], case_by_id: Mapping[str, Mapping[str, Any]],
) -> None:
	for case_id in unit:
		case_root = _public_case_root(context, case_by_id[case_id])
		if case_root.exists():
			_safe_remove_tree(context["public_root"], case_root, f"uncommitted public case {case_id}")


def _retain_unit(
	context: Mapping[str, Any], unit: tuple[str, ...], case_by_id: Mapping[str, Mapping[str, Any]],
	render_entries: Mapping[str, Mapping[str, Any]], sources_by_case: Mapping[str, Mapping[str, Path]],
) -> Mapping[str, Mapping[str, Any]]:
	receipt_path = _unit_receipt_path(context, unit)
	_expect(not receipt_path.exists(), "unit receipt", "refusing to replace an existing receipt")
	_remove_uncommitted_public_unit(context, unit, case_by_id)
	staging = context["state_root"] / "retention-staging" / hashlib.sha256("\0".join(unit).encode("utf-8")).hexdigest()[:24]
	if staging.exists():
		_safe_remove_tree(context["state_root"], staging, "stale retention staging")
	staging.mkdir(parents=True)
	staged_case_roots: dict[str, Path] = {}
	try:
		for case_id in unit:
			case = case_by_id[case_id]
			sources = sources_by_case[case_id]
			_expect(set(sources) == set(REPORT_FILENAMES), f"{case_id} report sources", "wrong report set")
			case_staging = staging / str(case["profile"]) / case_id
			case_staging.mkdir(parents=True)
			for name, filename in REPORT_FILENAMES.items():
				source = _regular_file(sources[name], f"{case_id} source {name}")
				_expect(source.suffix.lower() == ".json", f"{case_id} source {name}", "only JSON reports are publishable")
				document = _mapping(_load_json(source, f"{case_id} source {name}"), f"{case_id} source {name}")
				_reject_public_json_leaks(document, f"{case_id}.{name}")
				target = case_staging / filename
				shutil.copy2(source, target)
				_expect(_file_sha256(target) == _file_sha256(source) and target.stat().st_size == source.stat().st_size, f"{case_id} retained {name}", "copy mismatch")
			staged_case_roots[case_id] = case_staging
			# Derive once before publication. References currently point below the
			# private staging root, but all semantic report bytes are final.
			_documents_and_references(context, case, case_staging)

		for case_id in unit:
			case = case_by_id[case_id]
			target = _public_case_root(context, case)
			target.parent.mkdir(parents=True, exist_ok=True)
			_expect(not target.exists(), f"public case {case_id}", "target appeared concurrently")
			os.replace(staged_case_roots[case_id], target)

		rows = []
		unit_executions: set[str] = set()
		for case_id in unit:
			case = case_by_id[case_id]
			references, documents, derived = _documents_and_references(context, case, _public_case_root(context, case))
			unit_executions.add(str(documents["e2e_manifest"]["run_provenance_sha256"]))
			objective_runtime_binding = MEASUREMENT.canonical_json_sha256({
				"runtime": documents["objective_score"]["runtime"],
				"scorer_files": documents["objective_score"]["scorer_files"],
			})
			rows.append({
				"case_id": case_id,
				"profile": case["profile"],
				"plan_case_sha256": _canonical_sha256(case),
				"render_entry_sha256": _canonical_sha256(render_entries[case_id]),
				"objective_runtime_binding_sha256": objective_runtime_binding,
				"reports": references,
				"derived": derived,
			})
		_expect(len(unit_executions) == 1, "unit execution", "paired cases do not share one E2E provenance")
		receipt = {
			"schema_version": 1,
			"kind": RECEIPT_KIND,
			"run_binding_sha256": context["run_binding_sha256"],
			"unit": list(unit),
			"unit_execution_sha256": next(iter(unit_executions)),
			"cases": rows,
		}
		_write_canonical(receipt_path, receipt)
		verified = _load_unit_receipt(context, unit, case_by_id)
		_expect(verified is not None, "unit receipt", "could not reopen committed unit")
		return verified
	except Exception:
		# No receipt means none of these audio-free directories can be used for
		# resume. Remove them now or on the next invocation; raw attempt evidence
		# is deliberately retained for diagnosis.
		if not receipt_path.exists():
			_remove_uncommitted_public_unit(context, unit, case_by_id)
		raise
	finally:
		if staging.exists():
			_safe_remove_tree(context["state_root"], staging, "retention staging cleanup")


def _quality_pair_registry(plan: Mapping[str, Any]) -> Mapping[str, str]:
	result: dict[str, str] = {}
	by_scene: dict[str, list[Mapping[str, Any]]] = {}
	for case in plan["cases"]:
		if case.get("comparison_scene_id") is not None:
			by_scene.setdefault(str(case["comparison_scene_id"]), []).append(case)
	for rows in by_scene.values():
		quality = next(case for case in rows if case["profile"] == "Quality")
		voice = next(case for case in rows if case["profile"] == "VoiceFocus")
		result[str(voice["case_id"])] = str(quality["case_id"])
	return result


def _case_record(
	case: Mapping[str, Any], retained: Mapping[str, Any], pair_registry: Mapping[str, str],
	retained_by_case: Mapping[str, Mapping[str, Any]],
) -> Mapping[str, Any]:
	metadata = _metadata(case)
	objective = retained["documents"]["objective_score"]
	derived = retained["derived"]
	deltas = objective["candidate_minus_original"]
	severe_pair_improvement = 0.0
	quality_pair_case_id: str | None = None
	if case["profile"] == "VoiceFocus" and metadata["condition"] == "severe":
		quality_pair_case_id = pair_registry.get(str(case["case_id"]))
		_expect(quality_pair_case_id is not None, f"{case['case_id']} Quality pair", "missing")
		quality_objective = retained_by_case[quality_pair_case_id]["documents"]["objective_score"]
		severe_pair_improvement = (
			float(objective["metrics"]["candidate"]["dnsmos_bak"])
			- float(quality_objective["metrics"]["candidate"]["dnsmos_bak"])
		)
	objective_reference = retained["references"]["objective_score"]
	counters = dict(derived["counters"])
	failed = any(int(value) != 0 for value in counters.values())
	return {
		"record_type": "case",
		"case_id": str(case["case_id"]),
		"profile": str(case["profile"]),
		"condition": metadata["condition"],
		"dataset_split": "validation",
		"cohort_id": metadata["cohort_id"],
		"speaker_group_id": metadata["speaker_group_id"],
		"noise_group_id": metadata["noise_group_id"],
		"noise_class": metadata["noise_class"],
		"rir_group_id": metadata["rir_group_id"],
		"device_group_id": metadata["device_group_id"],
		"language": metadata["language"],
		"startup_preroll_ms": metadata["startup_preroll_ms"],
		"fixed_timeline": True,
		"receiver_cleanup_enabled": False,
		"failed": failed,
		"quality_pair_case_id": quality_pair_case_id,
		"objective_score": {
			"path": objective_reference["path"],
			"sha256": objective_reference["sha256"],
			"size_bytes": objective_reference["size_bytes"],
			"signal_stage": "receiver-capture",
			"wer_reference_kind": objective["wer_reference"]["kind"],
			"wer_reference_text_sha256": objective["wer_reference"]["text_sha256"],
		},
		"metrics": {
			"algorithmic_latency_ms": float(derived["algorithmic_latency_ms"]),
			"dnsmos_bak_improvement": float(deltas["dnsmos_bak"]),
			"dnsmos_ovrl_improvement": float(deltas["dnsmos_ovrl"]),
			"dnsmos_sig_loss": -float(deltas["dnsmos_sig"]),
			"estoi_loss": -float(deltas["estoi"]),
			"severe_noise_bak_improvement_over_quality": severe_pair_improvement,
			"speech_edge_loss_ms": float(derived["speech_edge_loss_ms"]),
			"wer_loss_percentage_points": float(deltas["wer_delta_percentage_points"]),
		},
		"counters": counters,
		"performance": copy.deepcopy(derived["performance"]),
	}


def _profile_pass(name: str, summary: Mapping[str, Any]) -> bool:
	metrics = summary["metrics"]
	performance = summary["performance"]
	passed = (
		float(metrics["worst_language_clean_estoi_loss_median"]) <= 0.01
		and float(metrics["worst_language_clean_dnsmos_sig_loss_median"]) <= 0.05
		and float(metrics["worst_language_wer_loss_percentage_points"]) <= 1.0
		and float(metrics["worst_cohort_ovrl_loss_median"]) <= 0.10
		and float(metrics["catastrophe_rate_percent"]) <= 0.5
		and float(metrics["max_speech_edge_loss_ms"]) <= 10.0
		and float(metrics["algorithmic_latency_ms_max"]) <= float(QUALIFICATION.LATENCY_GATES_MS[name])
		and int(summary["failed_case_count"]) == 0
	)
	for counter in CASE_EVIDENCE.COUNTER_NAMES:
		passed = passed and int(metrics[counter]) == 0
	if name in QUALIFICATION.NOISY_GATES:
		minimum_ovrl, minimum_bak = QUALIFICATION.NOISY_GATES[name]
		passed = (
			passed
			and float(metrics["noisy_dnsmos_ovrl_improvement_median"]) >= float(minimum_ovrl)
			and float(metrics["noisy_dnsmos_bak_improvement_median"]) >= float(minimum_bak)
		)
	if name == "VoiceFocus":
		passed = passed and float(metrics["severe_noise_bak_improvement_over_quality_median"]) >= 0.10
	if name == "Balanced":
		passed = passed and float(performance["average_rtf"]) <= 0.15 and float(performance["p99_callback_ms"]) <= 5.0
	elif name in ("Quality", "VoiceFocus"):
		passed = (
			passed
			and float(performance["average_rtf"]) <= 0.35
			and float(performance["p99_callback_ms"]) <= 8.0
			and float(performance["p99_worker_ms"]) <= 8.0
		)
	return bool(passed)


def _flat_rows(records: Sequence[Mapping[str, Any]]) -> list[Mapping[str, Any]]:
	rows = []
	for record in records:
		row: dict[str, Any] = {
			"case_id": record["case_id"],
			"profile": record["profile"],
			"condition": record["condition"],
			"dataset_split": record["dataset_split"],
			"cohort_id": record["cohort_id"],
			"language": record["language"],
			"noise_class": record["noise_class"],
			"startup_preroll_ms": record["startup_preroll_ms"],
			"failed": record["failed"],
			"quality_pair_case_id": record["quality_pair_case_id"],
			"objective_score_sha256": record["objective_score"]["sha256"],
		}
		for name, value in record["metrics"].items():
			row[f"metric_{name}"] = value
		for name, value in record["counters"].items():
			row[f"counter_{name}"] = value
		performance = record["performance"]
		for name in (
			"audio_duration_seconds", "processing_duration_seconds", "max_internal_processing_ms",
			"memory_growth_bytes", "soak_duration_seconds",
		):
			row[f"performance_{name}"] = performance[name]
		row["performance_callback_p99_ms"] = max(float(value) for value in performance["callback_durations_ms"])
		row["performance_worker_p99_ms"] = max(float(value) for value in performance["worker_durations_ms"])
		rows.append({key: row[key] for key in sorted(row)})
	return rows


def _write_csv(path: Path, rows: Sequence[Mapping[str, Any]]) -> None:
	_expect(bool(rows), "per-case CSV", "no rows")
	path.parent.mkdir(parents=True, exist_ok=True)
	with path.open("x", encoding="utf-8", newline="") as stream:
		writer = csv.DictWriter(stream, fieldnames=list(rows[0]), lineterminator="\n", extrasaction="raise")
		writer.writeheader()
		writer.writerows(rows)


def _write_junit(
	path: Path, records: Sequence[Mapping[str, Any]], profile_results: Sequence[Mapping[str, Any]],
) -> None:
	failures = sum(bool(record["failed"]) for record in records) + sum(not bool(profile["passed"]) for profile in profile_results)
	suite = ET.Element("testsuite", {
		"name": "mumble-input-enhancement-master-quality",
		"tests": str(len(records) + len(profile_results)),
		"failures": str(failures),
		"errors": "0",
	})
	for record in records:
		case = ET.SubElement(suite, "testcase", {
			"classname": f"input-enhancement.{record['profile']}",
			"name": str(record["case_id"]),
		})
		if record["failed"]:
			ET.SubElement(case, "failure", {"message": "runtime safety counter failed"}).text = json.dumps(record["counters"], sort_keys=True)
	for profile in profile_results:
		case = ET.SubElement(suite, "testcase", {
			"classname": "input-enhancement.aggregate",
			"name": str(profile["profile"]),
		})
		if not profile["passed"]:
			ET.SubElement(case, "failure", {"message": "aggregate quality/performance gate failed"}).text = json.dumps({
				"metrics": profile["metrics"], "performance": profile["performance"],
			}, sort_keys=True)
	payload = ET.tostring(suite, encoding="utf-8", xml_declaration=True) + b"\n"
	path.parent.mkdir(parents=True, exist_ok=True)
	path.write_bytes(payload)


def _write_summary_html(
	path: Path, status: str, coverage: Mapping[str, Any], profile_results: Sequence[Mapping[str, Any]],
) -> None:
	rows = []
	for profile in profile_results:
		metrics = profile["metrics"]
		performance = profile["performance"]
		rows.append(
			"<tr>"
			f"<td>{html.escape(str(profile['profile']))}</td>"
			f"<td>{profile['case_count']}</td>"
			f"<td>{'PASS' if profile['passed'] else 'FAIL'}</td>"
			f"<td>{float(metrics['noisy_dnsmos_ovrl_improvement_median']):.4f}</td>"
			f"<td>{float(metrics['noisy_dnsmos_bak_improvement_median']):.4f}</td>"
			f"<td>{float(metrics['worst_language_clean_estoi_loss_median']):.4f}</td>"
			f"<td>{float(performance['average_rtf']):.4f}</td>"
			f"<td>{float(performance['p99_callback_ms']):.3f}</td>"
			"</tr>"
		)
	document = (
		"<!doctype html><meta charset=\"utf-8\"><title>Mumble input enhancement qualification</title>"
		"<style>body{font-family:system-ui;margin:2rem}table{border-collapse:collapse}td,th{border:1px solid #bbb;padding:.4rem}</style>"
		f"<h1>Master quality: {html.escape(status.upper())}</h1>"
		f"<p>{coverage['case_count']} receiver-capture cases; {coverage['failed_case_count']} runtime failures.</p>"
		"<table><thead><tr><th>Profile</th><th>Cases</th><th>Gate</th><th>OVRL Δ</th><th>BAK Δ</th><th>clean eSTOI loss</th><th>RTF</th><th>callback p99 ms</th></tr></thead>"
		f"<tbody>{''.join(rows)}</tbody></table>\n"
	)
	path.write_text(document, encoding="utf-8", newline="\n")


def _clean_aggregate_outputs(context: Mapping[str, Any]) -> None:
	root = context["public_root"]
	root.mkdir(parents=True, exist_ok=True)
	known = [*PUBLIC_ARTIFACT_FILENAMES.values(), "metrics-runtime-attestation.json"]
	for filename in known:
		path = root / filename
		if path.exists():
			_expect(path.is_file() and not _is_reparse(path), f"stale aggregate {filename}", "unsafe path")
			path.unlink()


def _artifact_result(path: Path, output_root: Path) -> Mapping[str, Any]:
	return _reference(path, output_root)


def _assemble_qualification(
	context: Mapping[str, Any], retained_by_case: Mapping[str, Mapping[str, Any]],
	render_entries: Mapping[str, Mapping[str, Any]],
) -> Mapping[str, Any]:
	_clean_aggregate_outputs(context)
	public_root = context["public_root"]
	output_root = context["output_root"]
	pair_registry = _quality_pair_registry(context["plan"])
	case_by_id = {str(case["case_id"]): case for case in context["plan"]["cases"]}
	records = [
		_case_record(case_by_id[case_id], retained_by_case[case_id], pair_registry, retained_by_case)
		for case_id in sorted(case_by_id, key=lambda value: (CORE_PROFILES.index(str(case_by_id[value]["profile"])), value))
	]
	_expect(len(records) == MASTER_CASE_COUNT, "case evidence", f"requires {MASTER_CASE_COUNT} cases")
	# This verifies pair metadata and all ordinary record invariants before any
	# aggregate is emitted. Objective cross-reference verification follows after
	# the canonical JSONL has been written.
	CASE_EVIDENCE.validate_records(records, [], "core")
	CASE_EVIDENCE.validate_suite_splits(records, "master_quality")
	CASE_EVIDENCE.validate_source_diversity(records, "core", "master_quality")

	case_evidence_path = public_root / PUBLIC_ARTIFACT_FILENAMES["case_evidence_jsonl"]
	CASE_EVIDENCE.write_case_evidence(
		case_evidence_path, context["build"], "core", "master_quality", records, [],
	)
	loaded_records, loaded_transitions = CASE_EVIDENCE.load_case_evidence(
		case_evidence_path, context["build"], "core", "master_quality",
	)
	_expect(not loaded_transitions and loaded_records == records, "case evidence", "canonical reload differs")
	objective_scores = CASE_EVIDENCE.verify_objective_score_references(
		loaded_records, output_root, required_signal_stage="receiver-capture",
	)
	summary = CASE_EVIDENCE.summarize_case_evidence(loaded_records, [], "core", "master_quality")
	profile_results = []
	for profile in CORE_PROFILES:
		profile_summary = summary["profiles"][profile]
		profile_results.append({
			"profile": profile,
			"case_count": profile_summary["case_count"],
			"passed": _profile_pass(profile, profile_summary),
			"metrics": copy.deepcopy(profile_summary["metrics"]),
			"performance": copy.deepcopy(profile_summary["performance"]),
		})
	violations = [
		f"{profile['profile']} did not satisfy the locked aggregate quality/performance gates"
		for profile in profile_results if not profile["passed"]
	]
	status = "passed" if not violations and summary["coverage"]["failed_case_count"] == 0 else "failed"

	flat_rows = _flat_rows(loaded_records)
	csv_path = public_root / PUBLIC_ARTIFACT_FILENAMES["per_case_csv"]
	_write_csv(csv_path, flat_rows)
	rows_path = context["state_root"] / "aggregate" / "per-case-rows.json"
	rows_path.parent.mkdir(parents=True, exist_ok=True)
	_write_canonical(rows_path, flat_rows, replace=True)
	parquet_path = public_root / PUBLIC_ARTIFACT_FILENAMES["per_case_parquet"]
	_run_command(
		[
			str(context["config"]["metrics_python"]),
			str(context["source_root"] / "scripts" / "audio-quality" / "write-quality-parquet.py"),
			"--rows-json", str(rows_path),
			"--output", str(parquet_path),
		],
		context["source_root"] / "scripts" / "audio-quality",
		int(context["config"]["case_timeout_seconds"]),
		context["state_root"] / "aggregate" / "parquet-writer.stdout.txt",
		"per-case Parquet writer",
	)

	junit_path = public_root / PUBLIC_ARTIFACT_FILENAMES["junit"]
	_write_junit(junit_path, loaded_records, profile_results)
	failure_index_path = public_root / PUBLIC_ARTIFACT_FILENAMES["failure_spectrogram_index"]
	failure_rows = []
	for record in loaded_records:
		catastrophe = (
			float(record["metrics"]["estoi_loss"]) > 0.05
			or float(record["metrics"]["dnsmos_sig_loss"]) > 0.5
		)
		if record["failed"] or catastrophe:
			failure_rows.append({
				"case_id": record["case_id"],
				"profile": record["profile"],
				"reason": "runtime-safety" if record["failed"] else "catastrophe-threshold",
				"private_spectrogram_generated": False,
				"public_audio_or_spectrogram_path": None,
			})
	_write_canonical(failure_index_path, {
		"schema_version": 1,
		"kind": "mumble-input-enhancement-failure-spectrogram-index-v1",
		"contains_audio_samples": False,
		"note": "Raw audio and voice-derived spectrograms remain private; this index is the audio-free failure locator.",
		"failures": failure_rows,
	})
	summary_json_path = public_root / PUBLIC_ARTIFACT_FILENAMES["summary_json"]
	_write_canonical(summary_json_path, {
		"schema_version": 1,
		"kind": "mumble-input-enhancement-quality-summary-v1",
		"suite": "master_quality",
		"qualification_scope": "core",
		"status": status,
		"build": context["build"],
		"coverage": summary["coverage"],
		"profiles": profile_results,
		"violations": violations,
	})
	summary_html_path = public_root / PUBLIC_ARTIFACT_FILENAMES["summary_html"]
	_write_summary_html(summary_html_path, status, summary["coverage"], profile_results)

	objective_runtime_bindings = {
		MEASUREMENT.canonical_json_sha256({
			"runtime": score["runtime"], "scorer_files": score["scorer_files"],
		})
		for score in objective_scores
	}
	_expect(len(objective_runtime_bindings) == 1, "objective runtime", "cases do not share one exact pinned runtime/scorer binding")
	objective_runtime_binding_sha256 = next(iter(objective_runtime_bindings))
	metrics_files = list(PAYLOAD.payload_tree_records(context["metrics_runtime"]))
	_expect(_canonical_sha256(metrics_files) == context["build"]["metrics_runtime_sha256"], "metrics runtime", "file inventory changed")
	metrics_attestation_path = public_root / "metrics-runtime-attestation.json"
	_write_canonical(metrics_attestation_path, {
		"schema_version": 1,
		"kind": METRICS_RUNTIME_KIND,
		"payload_kind": "directory",
		"payload_sha256": context["build"]["metrics_runtime_sha256"],
		"objective_runtime_binding_sha256": objective_runtime_binding_sha256,
		"files": metrics_files,
	})

	artifact_paths = {
		"case_evidence_jsonl": case_evidence_path,
		"failure_spectrogram_index": failure_index_path,
		"junit": junit_path,
		"per_case_csv": csv_path,
		"per_case_parquet": parquet_path,
		"summary_html": summary_html_path,
		"summary_json": summary_json_path,
	}
	artifact_references = {name: _artifact_result(path, output_root) for name, path in artifact_paths.items()}
	case_index_entries = []
	for record in loaded_records:
		case_id = str(record["case_id"])
		retained = retained_by_case[case_id]
		derived = retained["derived"]
		case_index_entries.append({
			"case_id": case_id,
			"profile": record["profile"],
			"condition": record["condition"],
			"dataset_split": record["dataset_split"],
			"measurement_mode": "e2e",
			"plan_case_sha256": derived["plan_case_sha256"],
			"render_entry_sha256": derived["render_entry_sha256"],
			"source_input_sha256": derived["source_input_sha256"],
			"clean_reference_sha256": derived["clean_reference_sha256"],
			"reports": retained["references"],
		})
	measurement_index_path = public_root / PUBLIC_ARTIFACT_FILENAMES["measurement_index_json"]
	measurement_index = {
		"schema_version": 1,
		"kind": MEASUREMENT.INDEX_KIND,
		"qualification_scope": "core",
		"suite": "master_quality",
		"qualification_binding_sha256": CASE_EVIDENCE.qualification_binding_sha256(
			context["build"], "core", "master_quality",
		),
		"objective_runtime_binding_sha256": objective_runtime_binding_sha256,
		"release_holdout_approval_public_key_sha256": None,
		"metrics_runtime_attestation": _artifact_result(metrics_attestation_path, output_root),
		"build": context["build"],
		"plan_binding": {
			field: context["build"][field]
			for field in ("case_set_sha256", "corpus_inventory_sha256", "corpus_lock_sha256", "mixture_plan_sha256")
		},
		"profile_bindings": context["profile_bindings"],
		"published_artifacts": [
			{"name": name, "artifact": artifact_references[name]}
			for name in sorted(artifact_references)
		],
		"release_holdout_openings": [],
		"cases": case_index_entries,
		"soak_reports": [],
		"transitions": [],
	}
	_write_canonical(measurement_index_path, measurement_index)
	all_artifacts = {
		**artifact_references,
		"measurement_index_json": _artifact_result(measurement_index_path, output_root),
	}
	qualification = {
		"schema_version": 3,
		"qualification_scope": "core",
		"suite": "master_quality",
		"status": status,
		"generated_at_utc": dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z"),
		"build": context["build"],
		"coverage": summary["coverage"],
		"profiles": profile_results,
		"auto_transitions": None,
		"artifacts": {name: all_artifacts[name] for name in QUALIFICATION.ARTIFACT_NAMES},
		"violations": violations,
	}
	try:
		QUALIFICATION.validate_qualification(qualification, output_root)
	except Exception as error:
		raise CampaignError(f"assembled qualification failed independent semantic validation: {error}") from error
	qualification_path = output_root / "qualification.json"
	_write_canonical(qualification_path, qualification)
	# Reopen the root evidence after publication. The gate will repeat this and
	# independently derive the entire transitive measurement graph once more.
	reopened = QUALIFICATION.validate_qualification(
		_load_json(qualification_path, "qualification.json"), output_root,
	)
	_expect(reopened == qualification, "qualification.json", "reopened qualification differs")
	return qualification


def _campaign_progress(
	context: Mapping[str, Any], status: str, completed_units: int, completed_cases: int,
	last_unit: tuple[str, ...] | None = None, error: str | None = None,
) -> Mapping[str, Any]:
	value: dict[str, Any] = {
		"schema_version": 1,
		"kind": CAMPAIGN_KIND,
		"status": status,
		"completed_units": completed_units,
		"completed_cases": completed_cases,
		"run_binding_sha256": context["run_binding_sha256"],
		"run_binding": context["run_binding"],
		"last_unit": list(last_unit) if last_unit is not None else None,
		"error": error,
	}
	return value


def run_campaign(args: argparse.Namespace) -> Mapping[str, Any]:
	context = _build_context(args)
	qualification_path = context["output_root"] / "qualification.json"
	if qualification_path.is_file():
		qualification = QUALIFICATION.validate_qualification(
			_load_json(qualification_path, "existing qualification.json"), context["output_root"],
		)
		_expect(qualification["build"] == context["build"], "existing qualification.json", "belongs to another run binding")
		return qualification

	render_manifest, render_entries = _prepare_render(context)
	render_root = context["state_root"] / "rendered"
	render_manifest_path = render_root / "render-manifest.json"
	_expect(render_manifest["plan_sha256"] == PLAN.canonical_sha256(context["plan"]), "render manifest", "plan mismatch")
	case_by_id = {str(case["case_id"]): case for case in context["plan"]["cases"]}
	units = _execution_units(context["plan"])
	_expect(sum(len(unit) for unit in units) == MASTER_CASE_COUNT, "execution units", "do not cover all cases")
	retained_by_case: dict[str, Mapping[str, Any]] = {}
	completed_units = 0
	try:
		for unit in units:
			resumed = _load_unit_receipt(context, unit, case_by_id)
			if resumed is None:
				_remove_uncommitted_public_unit(context, unit, case_by_id)
				_critical_files_stable(context)
				attempt_number, attempt = _run_e2e_unit(context, unit, render_root, render_manifest_path)
				try:
					sources_by_case: dict[str, Mapping[str, Path]] = {}
					for case_id in unit:
						case = case_by_id[case_id]
						private = _case_private_paths(attempt / "e2e", case, len(unit) == 2)
						objective_path = _run_objective_score(
							context, case, render_entries[case_id], render_root, private, attempt,
						)
						sources_by_case[case_id] = _report_sources(private, objective_path)
					resumed = _retain_unit(
						context, unit, case_by_id, render_entries, sources_by_case,
					)
					_critical_files_stable(context)
					# Only this point authorizes deletion of the large private app/WAV
					# snapshots: every report was reopened and derived after commit.
					_safe_remove_tree(context["state_root"], attempt, f"qualified attempt {attempt_number}")
				except Exception as error:
					_write_canonical(attempt / "failure.json", {
						"schema_version": 1,
						"kind": "mumble-input-enhancement-e2e-attempt-failure-v1",
						"unit": list(unit),
						"attempt": attempt_number,
						"error": str(error),
					}, replace=True)
					raise
			assert resumed is not None
			retained_by_case.update(resumed)
			completed_units += 1
			_write_canonical(
				context["manifest_path"],
				_campaign_progress(context, "running", completed_units, len(retained_by_case), unit),
				replace=True,
			)
			if completed_units % 25 == 0:
				_assert_protected_payloads_stable(context)
		_expect(len(retained_by_case) == MASTER_CASE_COUNT, "campaign", "not every case has a verified receipt")
		_critical_files_stable(context)
		_assert_protected_payloads_stable(context)
		qualification = _assemble_qualification(context, retained_by_case, render_entries)
		_critical_files_stable(context)
		_assert_protected_payloads_stable(context)
		_write_canonical(
			context["manifest_path"],
			_campaign_progress(context, "completed", len(units), len(retained_by_case)),
			replace=True,
		)
		return qualification
	except Exception as error:
		_write_canonical(
			context["manifest_path"],
			_campaign_progress(context, "failed", completed_units, len(retained_by_case), error=str(error)),
			replace=True,
		)
		raise


def run_self_test() -> None:
	with tempfile.TemporaryDirectory(prefix="mumble-e2e-quality-campaign-self-test-") as raw:
		root = Path(raw)
		release = root / "release-fixtures"; release.mkdir()
		metrics = root / "metrics-runtime"; metrics.mkdir()
		adapter = release / "adapter.py"; adapter.write_text("print('adapter')\n", encoding="utf-8")
		policy = release / "policy.json"; policy.write_text("{}\n", encoding="utf-8")
		signature = release / "policy.sig"; signature.write_bytes(bytes(range(64)))
		python_copy = metrics / "python.exe"; shutil.copy2(sys.executable, python_copy)
		metrics_manifest = metrics / "metrics.json"; metrics_manifest.write_text("{}\n", encoding="utf-8")

		def pin(path: Path, base: Path) -> Mapping[str, Any]:
			return {
				"relative_path": path.relative_to(base).as_posix(),
				"sha256": _file_sha256(path),
				"size_bytes": path.stat().st_size,
			}

		config = {
			"schema_version": 1,
			"kind": CONFIG_KIND,
			"render_jobs": 2,
			"case_timeout_seconds": 120,
			"corpus_root_relative_path": ".",
			"orchestrator_python": pin(python_copy, metrics),
			"metrics_python": pin(python_copy, metrics),
			"metrics_manifest": pin(metrics_manifest, metrics),
			"input_enhancement_policy_manifest": pin(policy, release),
			"input_enhancement_policy_signature": pin(signature, release),
			"adapter": {
				**pin(adapter, release),
				"arguments": [
					{"kind": "literal", "value": "--client-stage-dir"},
					{"kind": "protected_path", "root": "release_fixtures", "relative_path": "."},
				],
			},
		}
		_write_canonical(release / "quality-campaign-config.json", config)
		loaded = _load_campaign_config(
			release, metrics, {"release_fixtures": release}, self_test=True,
		)
		if loaded["adapter"] != adapter or loaded["adapter_arguments"][0] != "--client-stage-dir":
			raise AssertionError("trusted campaign config did not preserve pinned adapter inputs")
		broken = copy.deepcopy(config)
		broken["adapter"]["arguments"][0]["value"] = r"C:\untrusted\adapter-arg"
		_write_canonical(release / "quality-campaign-config.json", broken, replace=True)
		try:
			_load_campaign_config(release, metrics, {"release_fixtures": release}, self_test=True)
		except CampaignError:
			pass
		else:
			raise AssertionError("campaign config accepted an unprotected absolute adapter path")

		plan = {
			"cases": [
				{"case_id": "o", "profile": "Original"},
				{"case_id": "q", "profile": "Quality", "comparison_scene_id": "scene"},
				{"case_id": "v", "profile": "VoiceFocus", "comparison_scene_id": "scene"},
			],
		}
		if _execution_units(plan) != [("o",), ("q", "v")]:
			raise AssertionError("execution-unit scheduler did not preserve a transactional Q/VF pair")
		try:
			_reject_public_json_leaks({"path": r"C:\private\capture.wav"})
		except CampaignError:
			pass
		else:
			raise AssertionError("public report sanitizer accepted a private absolute path")

		private = root / "private"; nested = private / "attempt"; nested.mkdir(parents=True)
		(nested / "large.bin").write_bytes(b"private")
		_safe_remove_tree(private, nested, "self-test private cleanup")
		if nested.exists():
			raise AssertionError("safe private cleanup did not remove the attested child")
		try:
			_safe_remove_tree(private, private, "self-test root cleanup")
		except CampaignError:
			pass
		else:
			raise AssertionError("safe cleanup accepted deletion of its own root")


def main(argv: Sequence[str] | None = None) -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--suite", choices=("master_quality",))
	parser.add_argument("--source-root", type=Path)
	parser.add_argument("--output-root", type=Path)
	parser.add_argument("--source-sha")
	parser.add_argument("--corpus-lock", type=Path)
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
	parser.add_argument("--harness-sha256")
	parser.add_argument("--release-holdout-approval-public-key-sha256")
	parser.add_argument(
		"--validate-only", action="store_true",
		help="validate and seal the complete campaign/config binding without rendering or starting clients",
	)
	parser.add_argument("--self-test", action="store_true")
	args = parser.parse_args(argv)
	try:
		if args.self_test:
			run_self_test()
			print("E2E quality campaign self-test: ok")
			if args.suite is None:
				return 0
		required = (
			"suite", "source_root", "output_root", "source_sha", "corpus_lock", "tested_binary", "legacy_binary",
			"staged_client_root", "model_manifest", "recipe_manifest", "server_binary", "corpus_inventory",
			"case_set", "mixture_plan", "release_fixtures", "metrics_runtime", "runner_class",
			"hardware_fingerprint_sha256", "harness_sha256",
		)
		missing = [f"--{name.replace('_', '-')}" for name in required if getattr(args, name) is None]
		if missing:
			raise CampaignError(f"missing required arguments: {', '.join(missing)}")
		if args.release_holdout_approval_public_key_sha256 is not None:
			raise CampaignError("release-holdout approval keys are forbidden in master_quality")
		if args.validate_only:
			context = _build_context(args)
			print(
				"E2E quality campaign config: valid; "
				f"binding={context['run_binding_sha256']}; output={args.output_root}"
			)
			return 0
		qualification = run_campaign(args)
		print(
			f"E2E quality campaign: {qualification['status']}; "
			f"cases={qualification['coverage']['case_count']}; output={args.output_root}"
		)
		return 0 if qualification["status"] == "passed" else 2
	except (AssertionError, CampaignError, OSError, ValueError) as error:
		print(f"E2E quality campaign: error: {error}", file=sys.stderr)
		return 1


if __name__ == "__main__":
	raise SystemExit(main())
