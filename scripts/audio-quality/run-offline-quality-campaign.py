#!/usr/bin/env python3
"""Run a hash-bound, sender-pre-Opus input-enhancement quality campaign.

This is the protected offline stage before the two-client Mumble transport
campaign.  It consumes already rendered private WAVs, runs the same product
profile recipes as the client through ``speech_cleanup_benchmark``, and invokes
the pinned objective scorer without permitting receiver-side artifacts or
correlation alignment.

The output is intentionally resumable but fail closed: a passed case is reused
only when its whole-run binding, case binding, manifest, and every output byte
still match.  Holdout plans are never accepted by this tool.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import math
import os
import re
import shutil
import stat
import struct
import subprocess
import sys
import tempfile
import wave
from pathlib import Path, PurePosixPath
from typing import Any, Mapping, MutableMapping, Sequence


SCRIPT_DIR = Path(__file__).resolve().parent
CAMPAIGN_ID = "mumble-offline-input-enhancement-v1"
SCHEMA_VERSION = 1
SAMPLE_RATE_HZ = 48_000
FRAME_SAMPLES = 480
MIN_SUPPORTED_EXECUTION_SEMANTICS_VERSION = 3
SELF_TEST_EXECUTION_SEMANTICS_VERSION = 5
SELF_TEST_MIX_CURVE_VERSION = 5
SUPPORTED_CORPUS_GENERATOR_VERSIONS = frozenset({"2", "3", "4"})
HEX64 = re.compile(r"^[0-9a-f]{64}$")
IDENTIFIER = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._:-]{0,127}$")
CORE_PROFILES = ("Original", "Light", "Balanced", "Quality", "VoiceFocus")
CPU_CLASSES = ("Low", "Standard", "High")
LANGUAGE_MAP = {"en-US": "en", "sv-SE": "sv"}
NIGHTLY_EXPANSION_SOURCE_IDS = frozenset({
	"fsd50k-eval-cc0-subset", "openslr12-librispeech-test-clean", "rixvox-v1-dev-0", "rixvox-v1-test-0",
})
CASE_ARTIFACT_FILENAMES = {
	"original_wav": "original-sender-pre-opus.wav",
	"original_report": "original-benchmark.json",
	"original_stdout": "original-benchmark.stdout.txt",
	"original_stderr": "original-benchmark.stderr.txt",
	"candidate_wav": "candidate-sender-pre-opus.wav",
	"candidate_report": "candidate-benchmark.json",
	"candidate_stdout": "candidate-benchmark.stdout.txt",
	"candidate_stderr": "candidate-benchmark.stderr.txt",
	"edge_fixed_timeline_score": "edge-fixed-timeline-score.json",
	"edge_fixed_timeline_stdout": "edge-fixed-timeline-score.stdout.txt",
	"edge_fixed_timeline_stderr": "edge-fixed-timeline-score.stderr.txt",
	"objective_score": "objective-quality.json",
	"objective_stdout": "objective-score.stdout.txt",
	"objective_stderr": "objective-score.stderr.txt",
}
UNSAFE_ENVIRONMENT_PREFIXES = ("PYTHON", "MUMBLE_", "HF_", "HUGGINGFACE_", "TRANSFORMERS_")
METRICS_PYTHON_PROBE_SOURCE = (
	"import json,sys;"
	"print(json.dumps({"
	"'executable':str(__import__('pathlib').Path(sys.executable).resolve()),"
	"'implementation':sys.implementation.name,"
	"'prefix':str(__import__('pathlib').Path(sys.prefix).resolve()),"
	"'version':sys.version.split()[0]"
	"},sort_keys=True,separators=(',',':')))"
)


class CampaignError(ValueError):
	"""Raised when campaign provenance or an execution result is unsafe."""


def _load_script(name: str, module_name: str) -> Any:
	path = SCRIPT_DIR / name
	spec = importlib.util.spec_from_file_location(module_name, path)
	if spec is None or spec.loader is None:
		raise CampaignError(f"unable to load required validator: {path}")
	module = importlib.util.module_from_spec(spec)
	spec.loader.exec_module(module)
	return module


PLAN = _load_script("generate-mixture-plan.py", "mumble_offline_campaign_plan")
LOCK = PLAN.LOCK
INVENTORY = PLAN.INVENTORY
OBJECTIVE = _load_script("objective_quality_score.py", "mumble_offline_campaign_objective")
MEASUREMENT = _load_script("measurement_evidence.py", "mumble_offline_campaign_measurement")
CASE_EVIDENCE = _load_script("quality_case_evidence.py", "mumble_offline_campaign_case_evidence")

MEASUREMENT_FRAGMENT_KIND = "mumble-offline-measurement-fragments-v1"
MEASUREMENT_CASE_FRAGMENT_KIND = "mumble-offline-measurement-case-fragment-v1"
METRICS_RUNTIME_ATTESTATION_KIND = "mumble-audio-metrics-runtime-attestation-v1"
QUALIFICATION_BUILD_KEYS = {
	"case_set_sha256", "corpus_inventory_sha256", "corpus_lock_sha256", "git_sha",
	"hardware_fingerprint_sha256", "harness_sha256", "legacy_binary_sha256",
	"metrics_runtime_sha256", "mixture_plan_sha256", "model_hashes", "model_manifest_sha256",
	"recipe_manifest_sha256", "recipe_set_version", "release_fixtures_sha256", "runner_class",
	"server_binary_sha256", "staged_payload_sha256", "tested_binary_sha256",
}
PUBLIC_BENCHMARK_KEYS = {
	"active_engine", "active_model_id", "active_model_sha256", "active_profile", "audio_ms",
	"callback_p99_ms", "deadline_misses", "drain_sample_count", "fallback_count",
	"clean_reference_sha256", "input_sample_count", "input_sha256", "input_saturated_sample_count", "maximum_processing_ms",
	"kind", "non_finite_sample_count", "out_of_range_sample_count", "output_sample_count",
	"output_sha256", "processing_mode", "processing_padding_sample_count", "processing_wall_ms",
	"reported_latency_samples", "requested_profile", "requested_recipe_id", "recipe_revision",
	"requested_ui_natural_clear", "requested_ui_noise_reduction",
	"rtf", "sample_count", "sample_rate", "saturated_sample_count", "schema_version", "source_report_sha256", "used_fallback",
	"validated_recipe_natural_clear", "validated_recipe_noise_reduction",
	"worker_processing_p99_ms",
}


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


def _integer(value: Any, path: str, minimum: int = 0) -> int:
	_expect(isinstance(value, int) and not isinstance(value, bool), path, "expected an integer")
	_expect(value >= minimum, path, f"must be >= {minimum}")
	return value


def _number(value: Any, path: str) -> float:
	_expect(isinstance(value, (int, float)) and not isinstance(value, bool), path, "expected a number")
	number = float(value)
	_expect(math.isfinite(number), path, "must be finite")
	return number


def _hash(value: Any, path: str) -> str:
	_expect(isinstance(value, str) and bool(HEX64.fullmatch(value)), path, "invalid lowercase SHA-256")
	return value


def _canonical_bytes(value: Any) -> bytes:
	return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")


def _canonical_sha256(value: Any) -> str:
	return hashlib.sha256(_canonical_bytes(value)).hexdigest()


def _sha256(path: Path) -> str:
	digest = hashlib.sha256()
	with path.open("rb") as stream:
		for chunk in iter(lambda: stream.read(1024 * 1024), b""):
			digest.update(chunk)
	return digest.hexdigest()


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
	except (OSError, UnicodeError, json.JSONDecodeError) as error:
		raise CampaignError(f"{label}: unable to read {path}: {error}") from error


def _write_json_atomic(path: Path, value: Mapping[str, Any]) -> None:
	path.parent.mkdir(parents=True, exist_ok=True)
	temporary = path.with_name(f".{path.name}.{os.getpid()}.tmp")
	temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
	os.replace(temporary, path)


def _write_canonical_json_atomic(path: Path, value: Any) -> None:
	path.parent.mkdir(parents=True, exist_ok=True)
	temporary = path.with_name(f".{path.name}.{os.getpid()}.tmp")
	temporary.write_bytes(_canonical_bytes(value) + b"\n")
	os.replace(temporary, path)


def _audio_free_reference(path: Path, root: Path) -> dict[str, Any]:
	path = _regular_file(path, str(path))
	root = root.resolve()
	try:
		relative = path.relative_to(root).as_posix()
	except ValueError as error:
		raise CampaignError(f"audio-free artifact escapes its root: {path}") from error
	_expect(PurePosixPath(relative).suffix.lower() not in MEASUREMENT.AUDIO_SUFFIXES, relative, "audio artifact is forbidden")
	return {
		"contains_audio_samples": False,
		"path": relative,
		"sha256": _sha256(path),
		"size_bytes": path.stat().st_size,
	}


def _tree_file_inventory(root: Path) -> list[dict[str, Any]]:
	root = _directory(root, "inventory root")
	files: list[dict[str, Any]] = []
	for current, directories, names in os.walk(root):
		current_path = Path(current)
		for directory in directories:
			_expect(not _is_reparse(current_path / directory), str(current_path / directory), "inventory contains a symlink/reparse directory")
		for name in names:
			path = _regular_file(current_path / name, f"inventory file {name}")
			files.append({
				"path": path.relative_to(root).as_posix(),
				"sha256": _sha256(path),
				"size_bytes": path.stat().st_size,
			})
	files.sort(key=lambda item: item["path"])
	_expect(bool(files), "inventory", "must contain at least one file")
	return files


def _is_reparse(path: Path) -> bool:
	try:
		metadata = os.lstat(path)
	except OSError as error:
		raise CampaignError(f"unable to inspect path {path}: {error}") from error
	attributes = getattr(metadata, "st_file_attributes", 0)
	reparse_flag = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
	return path.is_symlink() or bool(attributes & reparse_flag)


def _regular_file(path: Path, label: str) -> Path:
	path = path.resolve()
	_expect(path.is_file(), label, f"missing regular file: {path}")
	_expect(not _is_reparse(path), label, "symlinks/reparse points are forbidden")
	return path


def _directory(path: Path, label: str) -> Path:
	path = path.resolve()
	_expect(path.is_dir(), label, f"missing directory: {path}")
	_expect(not _is_reparse(path), label, "symlinks/reparse points are forbidden")
	return path


def _file_record(path: Path, *, include_path: bool = False) -> dict[str, Any]:
	path = _regular_file(path, str(path))
	record: dict[str, Any] = {"sha256": _sha256(path), "size_bytes": path.stat().st_size}
	if include_path:
		record["path"] = str(path)
	return record


def _json_record(path: Path, value: Any) -> dict[str, Any]:
	return {**_file_record(path, include_path=True), "canonical_sha256": _canonical_sha256(value)}


def _safe_relative(value: Any, path: str) -> str:
	_expect(isinstance(value, str) and bool(value), path, "expected a non-empty relative path")
	parsed = PurePosixPath(value)
	_expect(value == parsed.as_posix() and "\\" not in value, path, "must use normalized forward slashes")
	_expect(not parsed.is_absolute() and "." not in parsed.parts and ".." not in parsed.parts, path, "unsafe path")
	return value


def _below(root: Path, relative: str, label: str) -> Path:
	root = _directory(root, f"{label} root")
	path = root.joinpath(*PurePosixPath(_safe_relative(relative, label)).parts).resolve()
	try:
		path.relative_to(root)
	except ValueError as error:
		raise CampaignError(f"{label}: path escapes its root") from error
	return _regular_file(path, label)


def _relative_record(path: Path, root: Path) -> dict[str, Any]:
	path = _regular_file(path, str(path))
	root = root.resolve()
	try:
		relative = path.relative_to(root).as_posix()
	except ValueError as error:
		raise CampaignError(f"output artifact escapes campaign root: {path}") from error
	return {"relative_path": relative, **_file_record(path)}


def _tree_record(root: Path) -> dict[str, Any]:
	root = _directory(root, "tree root")
	files: list[dict[str, Any]] = []
	for current, directories, names in os.walk(root):
		current_path = Path(current)
		for directory in directories:
			path = current_path / directory
			_expect(not _is_reparse(path), str(path), "tree contains a symlink/reparse directory")
		for name in names:
			path = current_path / name
			_expect(not _is_reparse(path), str(path), "tree contains a symlink/reparse file")
			_expect(path.is_file(), str(path), "tree entry is not a regular file")
			files.append({
				"relative_path": path.relative_to(root).as_posix(),
				"sha256": _sha256(path),
				"size_bytes": path.stat().st_size,
			})
	files.sort(key=lambda item: item["relative_path"])
	return {
		"file_count": len(files),
		"size_bytes": sum(int(item["size_bytes"]) for item in files),
		"tree_sha256": _canonical_sha256(files),
	}


def _same_file_record(path: Path, expected: Mapping[str, Any], label: str) -> None:
	actual = _file_record(path)
	_expect(actual == {"sha256": expected["sha256"], "size_bytes": expected["size_bytes"]}, label, "file bytes changed")


def _read_wav_fingerprint(path: Path) -> dict[str, Any]:
	"""Return a format-independent float32 sample hash for PCM/float mono WAV."""

	path = _regular_file(path, "WAV")
	data = path.read_bytes()
	_expect(len(data) >= 12 and data[:4] == b"RIFF" and data[8:12] == b"WAVE", str(path), "not a RIFF/WAVE file")
	offset = 12
	fmt: bytes | None = None
	audio: bytes | None = None
	while offset + 8 <= len(data):
		chunk_id = data[offset : offset + 4]
		length = struct.unpack_from("<I", data, offset + 4)[0]
		start = offset + 8
		end = start + length
		_expect(end <= len(data), str(path), "truncated WAV chunk")
		if chunk_id == b"fmt ":
			fmt = data[start:end]
		elif chunk_id == b"data":
			audio = data[start:end]
		offset = end + (length & 1)
	_expect(fmt is not None and len(fmt) >= 16 and audio is not None, str(path), "missing fmt/data chunk")
	format_tag, channels, rate, _, block_align, bits = struct.unpack_from("<HHIIHH", fmt, 0)
	if format_tag == 0xFFFE:
		_expect(len(fmt) >= 40, str(path), "truncated extensible WAV format")
		format_tag = struct.unpack_from("<H", fmt, 24)[0]
	_expect(format_tag in (1, 3), str(path), "only PCM or IEEE-float WAV is supported")
	_expect(channels == 1 and rate == SAMPLE_RATE_HZ, str(path), "must be mono 48 kHz")
	bytes_per_sample = bits // 8
	_expect(bits in ((8, 16, 24, 32) if format_tag == 1 else (32, 64)), str(path), "unsupported WAV sample width")
	_expect(block_align == channels * bytes_per_sample and len(audio) % block_align == 0, str(path), "invalid WAV block alignment")
	digest = hashlib.sha256()
	frames = len(audio) // block_align
	for index in range(frames):
		raw = audio[index * bytes_per_sample : (index + 1) * bytes_per_sample]
		if format_tag == 3:
			value = struct.unpack("<f" if bits == 32 else "<d", raw)[0]
		elif bits == 8:
			value = (raw[0] - 128) / 128.0
		elif bits == 16:
			value = struct.unpack("<h", raw)[0] / 32768.0
		elif bits == 24:
			integer = int.from_bytes(raw, "little", signed=False)
			if integer & 0x800000:
				integer -= 1 << 24
			value = integer / 8_388_608.0
		else:
			value = struct.unpack("<i", raw)[0] / 2_147_483_648.0
		_expect(math.isfinite(value), str(path), f"non-finite sample at frame {index}")
		digest.update(struct.pack("<f", float(value)))
	return {
		"channels": channels,
		"frames": frames,
		"sample_rate_hz": rate,
		"sample_sha256_float32le": digest.hexdigest(),
	}


def _assert_same_samples(left: Path, right: Path, label: str) -> None:
	_expect(_read_wav_fingerprint(left) == _read_wav_fingerprint(right), label, "WAV sample timelines are not exact")


def _tool_command(path: Path, arguments: Sequence[str]) -> list[str]:
	if path.suffix.lower() == ".py":
		return [sys.executable, str(path), *arguments]
	return [str(path), *arguments]


def _run(command: Sequence[str], cwd: Path, environment: Mapping[str, str], timeout: int, stdout_path: Path, stderr_path: Path, label: str) -> None:
	try:
		completed = subprocess.run(
			list(command), cwd=str(cwd), env=dict(environment), capture_output=True, text=True,
			encoding="utf-8", errors="replace", timeout=timeout, check=False,
		)
	except (OSError, subprocess.TimeoutExpired) as error:
		raise CampaignError(f"{label}: unable to complete: {error}") from error
	stdout_path.write_text(completed.stdout, encoding="utf-8")
	stderr_path.write_text(completed.stderr, encoding="utf-8")
	_expect(completed.returncode == 0, label, f"failed with exit code {completed.returncode}; see {stderr_path}")


def _validate_transformation_manifest(
	path: Path, inventory: Mapping[str, Any], lock: Mapping[str, Any], expected_corpus_suite: str
) -> Mapping[str, Any]:
	path = _regular_file(path, "corpus transformation manifest")
	manifest = _mapping(_load_json(path, "corpus transformation manifest"), "corpus transformation manifest")
	_exact_keys(
		manifest,
		{
			"corpus_lock_sha256", "corpus_state_sha256", "demand_preparation", "ffmpeg",
			"fleurs_swedish_selection", "generator", "generator_version", "mcgill_archive_observation",
			"nightly_materialized_splits", "nightly_sealed_splits", "nightly_selection_sha256",
			"qualification_suite", "schema_version", "sources", "split_algorithm", "split_seed",
			"split_seed_selection_basis", "transforms",
		},
		set(),
		"corpus transformation manifest",
	)
	_expect(
		manifest["schema_version"] == 1 and manifest["generator"] == "mumble-corpus-builder"
		and manifest["generator_version"] in SUPPORTED_CORPUS_GENERATOR_VERSIONS,
		"corpus transformation manifest",
		"unsupported generator schema",
	)
	_expect(
		manifest["split_algorithm"]
		== "sha256-v1 by kind/group: tuning=0..59, validation=60..79, holdout=80..99",
		"corpus transformation manifest.split_algorithm",
		"unsupported split algorithm",
	)
	_expect(
		isinstance(manifest["split_seed"], str) and bool(IDENTIFIER.fullmatch(manifest["split_seed"])),
		"corpus transformation manifest.split_seed",
		"must be a stable identifier",
	)
	_expect(
		expected_corpus_suite in ("master_quality", "nightly"),
		"corpus qualification suite",
		"unsupported by the corpus-builder contract",
	)
	_expect(
		manifest["qualification_suite"] == expected_corpus_suite,
		"corpus transformation manifest.qualification_suite",
		"does not match the suite-compatible corpus family",
	)
	provenance = _mapping(inventory["provenance"], "inventory.provenance")
	_expect(
		_sha256(path) == provenance["transformation_manifest_sha256"],
		"corpus transformation manifest",
		"file hash does not match inventory provenance",
	)
	_expect(
		manifest["corpus_state_sha256"] == provenance["generated_from_state_sha256"],
		"corpus transformation manifest.corpus_state_sha256",
		"does not match inventory provenance",
	)
	_expect(
		manifest["corpus_lock_sha256"] == LOCK.canonical_manifest_sha256(lock),
		"corpus transformation manifest.corpus_lock_sha256",
		"does not bind the supplied corpus lock",
	)
	if expected_corpus_suite == "nightly":
		_expect(
			inventory.get("eligibility") == "nightly-partial",
			"inventory.eligibility",
			"nightly qualification requires a holdout-sealed nightly-partial inventory",
		)
		_expect(
			manifest["nightly_materialized_splits"] == ["tuning", "validation"],
			"corpus transformation manifest.nightly_materialized_splits",
			"must contain exactly tuning and validation",
		)
		_expect(
			manifest["nightly_sealed_splits"] == ["holdout"],
			"corpus transformation manifest.nightly_sealed_splits",
			"must seal holdout",
		)
		_expect(
			inventory.get("sealed_splits") == manifest["nightly_sealed_splits"],
			"inventory.sealed_splits",
			"does not match the transformation manifest",
		)
		_expect(
			inventory.get("selection_sha256") == manifest["nightly_selection_sha256"],
			"inventory.selection_sha256",
			"does not match the transformation manifest",
		)
		selection_path = _regular_file(SCRIPT_DIR / "nightly-corpus-selection-v1.json", "frozen nightly selection")
		_expect(
			_sha256(selection_path) == manifest["nightly_selection_sha256"],
			"corpus transformation manifest.nightly_selection_sha256",
			"does not bind the frozen nightly selection",
		)
		selection = _mapping(_load_json(selection_path, "frozen nightly selection"), "frozen nightly selection")
		_expect(
			selection.get("split_seed") == manifest["split_seed"],
			"frozen nightly selection.split_seed",
			"does not match the transformation manifest",
		)
		items = inventory.get("items")
		_expect(isinstance(items, list), "inventory.items", "must be an array")
		for index, item in enumerate(items):
			_expect(isinstance(item, dict), f"inventory.items[{index}]", "must be an object")
			if item.get("source_id") not in NIGHTLY_EXPANSION_SOURCE_IDS:
				# The fully verified base corpus has mechanically prepared holdout
				# members. The nightly seal applies to the frozen expansion only.
				continue
			assigned = INVENTORY.assigned_split(
				manifest["split_seed"], str(item.get("kind")), str(item.get("group_id"))
			)
			_expect(
				assigned in manifest["nightly_materialized_splits"],
				f"inventory.items[{index}]",
				"nightly-partial inventory contains material assigned to sealed holdout",
			)
	else:
		_expect(
			inventory.get("eligibility") == "release",
			"inventory.eligibility",
			"non-nightly qualification requires a release inventory",
		)
		for field in ("nightly_selection_sha256", "nightly_materialized_splits", "nightly_sealed_splits"):
			_expect(
				manifest[field] is None,
				f"corpus transformation manifest.{field}",
				"must be null outside the nightly suite",
			)
		_expect(
			"sealed_splits" not in inventory and "selection_sha256" not in inventory,
			"inventory",
			"release inventory must not carry nightly seal metadata",
		)
	return manifest


def _validate_plan_inventory_lock(
	plan_path: Path, inventory_path: Path, lock_path: Path, transformation_manifest_path: Path
) -> tuple[Mapping[str, Any], Mapping[str, Any], Mapping[str, Any], Mapping[str, Any]]:
	lock_path = _regular_file(lock_path, "corpus lock")
	try:
		lock = LOCK.load_validated_manifest(lock_path)
	except Exception as error:
		raise CampaignError(f"corpus lock failed validation: {error}") from error
	inventory = _mapping(_load_json(_regular_file(inventory_path, "corpus inventory"), "corpus inventory"), "corpus inventory")
	plan = _mapping(_load_json(_regular_file(plan_path, "mixture plan"), "mixture plan"), "mixture plan")
	try:
		PLAN.validate_plan(plan)
		plan_suite = str(plan["suite"])
		items = PLAN.validate_inventory(
			inventory, lock, LOCK.canonical_manifest_sha256(lock), plan_suite, str(plan["seed"]),
			str(plan["split"]),
		)
	except Exception as error:
		raise CampaignError(f"plan/inventory/lock validation failed: {error}") from error
	_expect(plan["split"] in ("tuning", "validation"), "plan.split", "offline campaign forbids holdout and non-development splits")
	expected_corpus_suite = "nightly" if plan_suite == "nightly" else "master_quality"
	transformation_manifest = _validate_transformation_manifest(
		transformation_manifest_path, inventory, lock, expected_corpus_suite
	)
	if plan_suite == "nightly":
		_expect(
			plan["split"] in transformation_manifest["nightly_materialized_splits"],
			"plan.split",
			"is not materialized by the holdout-sealed nightly corpus",
		)
	_expect(
		plan["seed"] == transformation_manifest["split_seed"],
		"plan.seed",
		"does not match the inventory-bound transformation split seed",
	)
	_expect(
		plan["split_algorithm"] == transformation_manifest["split_algorithm"],
		"plan.split_algorithm",
		"does not match the inventory-bound transformation manifest",
	)
	lock_canonical = LOCK.canonical_manifest_sha256(lock)
	inventory_canonical = INVENTORY.canonical_sha256(inventory)
	_expect(plan["corpus_lock_sha256"] == lock_canonical, "plan.corpus_lock_sha256", "does not bind the supplied lock")
	_expect(plan["corpus_inventory_sha256"] == inventory_canonical, "plan.corpus_inventory_sha256", "does not bind the supplied inventory")

	items_by_id = {str(item["id"]): item for item in items}
	_expect(len(items_by_id) == len(items), "inventory.items", "duplicate item IDs")
	for index, case in enumerate(plan["cases"]):
		case_path = f"plan.cases[{index}]"
		_expect(
			isinstance(case.get("case_id"), str) and bool(IDENTIFIER.fullmatch(case["case_id"])),
			f"{case_path}.case_id",
			"must be a path-safe stable identifier",
		)
		_expect(case["profile"] in CORE_PROFILES, f"{case_path}.profile", "Auto/unknown profiles are forbidden in core offline qualification")
		language = case["speech"].get("language")
		_expect(language in LANGUAGE_MAP, f"{case_path}.speech.language", "only pinned English/Swedish scoring is supported")
		for label, source in (
			("speech", case["speech"]),
			("noise", case["noise"]),
			("rir", case["mix"]["rir"]),
			("microphone_response", case["mix"]["microphone_response"]),
		):
			if source is None:
				continue
			item_id = source.get("item_id")
			_expect(item_id in items_by_id, f"{case_path}.{label}.item_id", "does not exist in the supplied inventory")
			item = items_by_id[str(item_id)]
			expected_kind = "microphone_response" if label == "microphone_response" else label
			_expect(item["kind"] == expected_kind, f"{case_path}.{label}.item_id", "inventory kind mismatch")
			_expect(
				PLAN.assigned_split(str(transformation_manifest["split_seed"]), expected_kind, str(item["group_id"]))
				== plan["split"],
				f"{case_path}.{label}.item_id",
				"source group belongs to a different protected corpus split",
			)
			common_pairs = {
				"relative_path": "relative_path", "group_id": "group_id", "input_sample_rate_hz": "sample_rate_hz",
				"input_channels": "channels", "sha256": "sha256", "size_bytes": "size_bytes",
				"source_id": "source_id", "source_artifact_sha256": "source_artifact_sha256",
			}
			for plan_key, inventory_key in common_pairs.items():
				_expect(source.get(plan_key) == item.get(inventory_key), f"{case_path}.{label}.{plan_key}", "inventory binding mismatch")
			if label == "speech":
				for key in ("language", "speaker_id"):
					_expect(source.get(key) == item.get(key), f"{case_path}.speech.{key}", "inventory binding mismatch")
				_expect(source.get("transcript_sha256") == item["transcript"]["sha256"], f"{case_path}.speech.transcript_sha256", "inventory binding mismatch")
			elif label == "noise":
				_expect(source.get("class") == item.get("noise_class"), f"{case_path}.noise.class", "inventory binding mismatch")
			elif label == "rir":
				for key, value in item["rir"].items():
					_expect(source.get(key) == value, f"{case_path}.mix.rir.{key}", "inventory binding mismatch")
			elif label == "microphone_response":
				for key, value in item["microphone_response"].items():
					_expect(source.get(key) == value, f"{case_path}.mix.microphone_response.{key}", "inventory binding mismatch")
			if "window" in source:
				start = _integer(source["window"].get("start_sample"), f"{case_path}.{label}.window.start_sample")
				length = _integer(source["window"].get("length_samples"), f"{case_path}.{label}.window.length_samples", 1)
				_expect(start + length <= item["duration_samples"], f"{case_path}.{label}.window", "exceeds inventory duration")
	return plan, inventory, lock, transformation_manifest


def _validate_render_manifest(
	plan: Mapping[str, Any], render_manifest_path: Path, render_root: Path
) -> tuple[Mapping[str, Any], Mapping[str, Mapping[str, Any]], list[dict[str, Any]]]:
	render_root = _directory(render_root, "render root")
	render_manifest_path = _regular_file(render_manifest_path, "render manifest")
	try:
		render_manifest_path.relative_to(render_root)
	except ValueError as error:
		raise CampaignError("render manifest must be below --render-root") from error
	manifest = _mapping(_load_json(render_manifest_path, "render manifest"), "render manifest")
	_exact_keys(
		manifest,
		{"cases", "channels", "corpus_inventory_sha256", "corpus_lock_sha256", "plan_sha256", "private_audio_do_not_upload", "renderer", "sample_rate_hz", "schema_version"},
		set(),
		"render manifest",
	)
	_expect(manifest["schema_version"] == 2 and manifest["renderer"] == "mumble-audio-mixture-renderer-v2", "render manifest", "unsupported renderer schema")
	_expect(manifest["private_audio_do_not_upload"] is True, "render manifest.private_audio_do_not_upload", "privacy marker is required")
	_expect(manifest["sample_rate_hz"] == SAMPLE_RATE_HZ and manifest["channels"] == 1, "render manifest", "must be mono 48 kHz")
	_expect(manifest["plan_sha256"] == PLAN.canonical_sha256(plan), "render manifest.plan_sha256", "plan binding mismatch")
	_expect(manifest["corpus_lock_sha256"] == plan["corpus_lock_sha256"], "render manifest.corpus_lock_sha256", "lock binding mismatch")
	_expect(manifest["corpus_inventory_sha256"] == plan["corpus_inventory_sha256"], "render manifest.corpus_inventory_sha256", "inventory binding mismatch")
	_expect(isinstance(manifest["cases"], list), "render manifest.cases", "expected an array")
	plan_by_id = {str(case["case_id"]): case for case in plan["cases"]}
	entries: dict[str, Mapping[str, Any]] = {}
	audio_tree: list[dict[str, Any]] = []
	audio_paths: dict[str, dict[str, Path]] = {}
	seen_paths: set[str] = set()
	for index, value in enumerate(manifest["cases"]):
		entry = _mapping(value, f"render manifest.cases[{index}]")
		_exact_keys(
			entry,
			{"case_id", "clean_reference", "input", "microphone_response_source_sha256", "noise_source_sha256", "profile", "rendered_samples", "rir_source_sha256", "speech_source_sha256", "startup_preroll_ms"},
			set(),
			f"render manifest.cases[{index}]",
		)
		case_id = str(entry["case_id"])
		_expect(case_id in plan_by_id and case_id not in entries, f"render manifest.cases[{index}].case_id", "unknown or duplicate case")
		case = plan_by_id[case_id]
		_expect(entry["profile"] == case["profile"], f"render manifest.{case_id}.profile", "plan mismatch")
		_expect(entry["startup_preroll_ms"] == case["startup"]["preroll_ms"], f"render manifest.{case_id}.startup_preroll_ms", "plan mismatch")
		expected_samples = int(plan["format"]["duration_ms"]) * SAMPLE_RATE_HZ // 1000
		_expect(entry["rendered_samples"] == expected_samples, f"render manifest.{case_id}.rendered_samples", "duration mismatch")
		_expect(entry["speech_source_sha256"] == case["speech"]["sha256"], f"render manifest.{case_id}.speech_source_sha256", "plan mismatch")
		_expect(entry["noise_source_sha256"] == (case["noise"]["sha256"] if case["noise"] is not None else None), f"render manifest.{case_id}.noise_source_sha256", "plan mismatch")
		_expect(entry["rir_source_sha256"] == case["mix"]["rir"]["sha256"], f"render manifest.{case_id}.rir_source_sha256", "plan mismatch")
		_expect(entry["microphone_response_source_sha256"] == case["mix"]["microphone_response"]["sha256"], f"render manifest.{case_id}.microphone_response_source_sha256", "plan mismatch")
		for role in ("input", "clean_reference"):
			record = _mapping(entry[role], f"render manifest.{case_id}.{role}")
			_exact_keys(record, {"path", "sha256"}, set(), f"render manifest.{case_id}.{role}")
			relative = _safe_relative(record["path"], f"render manifest.{case_id}.{role}.path")
			_expect(relative not in seen_paths, f"render manifest.{case_id}.{role}.path", "rendered audio path is reused")
			seen_paths.add(relative)
			path = _below(render_root, relative, f"render manifest.{case_id}.{role}")
			_expect(_sha256(path) == _hash(record["sha256"], f"render manifest.{case_id}.{role}.sha256"), f"render manifest.{case_id}.{role}", "rendered WAV hash mismatch")
			fingerprint = _read_wav_fingerprint(path)
			_expect(fingerprint["frames"] == expected_samples, f"render manifest.{case_id}.{role}", "WAV frame count mismatch")
			audio_tree.append({"case_id": case_id, "role": role, "relative_path": relative, **_file_record(path), **fingerprint})
			audio_paths.setdefault(case_id, {})[role] = path
		entries[case_id] = entry
	_expect(set(entries) == set(plan_by_id), "render manifest.cases", "does not contain exactly the plan cases")
	_expect([entry["case_id"] for entry in manifest["cases"]] == [case["case_id"] for case in plan["cases"]], "render manifest.cases", "case order must match the canonical plan")
	paired_scenes: dict[str, list[str]] = {}
	for case in plan["cases"]:
		comparison_scene_id = case.get("comparison_scene_id")
		if comparison_scene_id is not None:
			paired_scenes.setdefault(str(comparison_scene_id), []).append(str(case["case_id"]))
	for comparison_scene_id, case_ids in paired_scenes.items():
		_expect(len(case_ids) == 2, f"render manifest.paired scene {comparison_scene_id}", "requires exactly two cases")
		for role in ("input", "clean_reference"):
			_assert_same_samples(
				audio_paths[case_ids[0]][role], audio_paths[case_ids[1]][role],
				f"render manifest.paired scene {comparison_scene_id}.{role}",
			)
	audio_tree.sort(key=lambda item: (item["case_id"], item["role"]))
	return manifest, entries, audio_tree


def _validate_package(
	runtime_root: Path, model_manifest_path: Path, recipe_manifest_path: Path
) -> tuple[Mapping[str, Any], Mapping[str, Any], dict[str, Mapping[str, Any]], dict[str, Mapping[str, Any]]]:
	runtime_root = _directory(runtime_root, "packaged runtime root")
	model_manifest_path = _regular_file(model_manifest_path, "input-models.json")
	recipe_manifest_path = _regular_file(recipe_manifest_path, "input-recipes.json")
	_expect(model_manifest_path == runtime_root / "input-models.json", "input-models.json", "must be the packaged runtime-root manifest")
	_expect(recipe_manifest_path == runtime_root / "input-recipes.json", "input-recipes.json", "must be the packaged runtime-root manifest")
	models_manifest = _mapping(_load_json(model_manifest_path, "input-models.json"), "input-models.json")
	recipes_manifest = _mapping(_load_json(recipe_manifest_path, "input-recipes.json"), "input-recipes.json")
	_exact_keys(models_manifest, {"catalogRevision", "generatedFromAssets", "models", "schemaVersion"}, set(), "input-models.json")
	_expect(models_manifest["schemaVersion"] == 1 and models_manifest["generatedFromAssets"] is True, "input-models.json", "unsupported or non-generated model catalog")
	_expect(isinstance(models_manifest["catalogRevision"], str) and bool(models_manifest["catalogRevision"]), "input-models.json.catalogRevision", "required")
	_expect(isinstance(models_manifest["models"], list), "input-models.json.models", "expected an array")
	models: dict[str, Mapping[str, Any]] = {}
	for index, value in enumerate(models_manifest["models"]):
		model = _mapping(value, f"input-models.json.models[{index}]")
		_exact_keys(model, {"algorithmicLatencyMs", "backend", "id", "licenseSpdx", "path", "recipeCompatibility", "sampleRateHz", "sha256", "size", "version"}, set(), f"input-models.json.models[{index}]")
		model_id = str(model["id"])
		_expect(model_id and model_id not in models, f"input-models.json.models[{index}].id", "empty or duplicate")
		relative = _safe_relative(model["path"], f"input-models.json.models[{index}].path")
		asset = _below(runtime_root, relative, f"model {model_id}")
		_expect(asset.stat().st_size == _integer(model["size"], f"model {model_id}.size", 1), f"model {model_id}", "asset size mismatch")
		_expect(_sha256(asset) == _hash(model["sha256"], f"model {model_id}.sha256"), f"model {model_id}", "asset hash mismatch")
		_expect(model["sampleRateHz"] in (16_000, SAMPLE_RATE_HZ), f"model {model_id}.sampleRateHz", "unsupported rate")
		_number(model["algorithmicLatencyMs"], f"model {model_id}.algorithmicLatencyMs")
		_expect(isinstance(model["recipeCompatibility"], list), f"model {model_id}.recipeCompatibility", "expected an array")
		models[model_id] = model

	_exact_keys(recipes_manifest, {"catalogRevision", "modelManifestSha256", "recipes", "schemaVersion"}, set(), "input-recipes.json")
	_expect(recipes_manifest["schemaVersion"] == 2, "input-recipes.json.schemaVersion", "unsupported version")
	_expect(recipes_manifest["catalogRevision"] == models_manifest["catalogRevision"], "input-recipes.json.catalogRevision", "model catalog mismatch")
	_expect(recipes_manifest["modelManifestSha256"] == _sha256(model_manifest_path), "input-recipes.json.modelManifestSha256", "does not bind exact input-models.json bytes")
	_expect(isinstance(recipes_manifest["recipes"], list), "input-recipes.json.recipes", "expected an array")
	recipes: dict[str, Mapping[str, Any]] = {}
	for index, value in enumerate(recipes_manifest["recipes"]):
		recipe = _mapping(value, f"input-recipes.json.recipes[{index}]")
		_exact_keys(
			recipe,
			{"adaptationPolicyVersion", "engine", "executionSemanticsVersion", "id", "latencyBudgetMs", "minimumCpuClass", "mixCurveVersion", "modelIds", "naturalCrispRange", "noiseReductionRange", "profile", "revision"},
			{"advancedOnly"},
			f"input-recipes.json.recipes[{index}]",
		)
		recipe_id = str(recipe["id"])
		_expect(recipe_id and recipe_id not in recipes, f"input-recipes.json.recipes[{index}].id", "empty or duplicate")
		_expect(recipe["profile"] in (*CORE_PROFILES, "Auto"), f"recipe {recipe_id}.profile", "unsupported profile")
		_expect(recipe["minimumCpuClass"] in CPU_CLASSES, f"recipe {recipe_id}.minimumCpuClass", "unsupported CPU class")
		_integer(recipe["revision"], f"recipe {recipe_id}.revision", 1)
		_number(recipe["latencyBudgetMs"], f"recipe {recipe_id}.latencyBudgetMs")
		execution_semantics_version = _integer(
			recipe["executionSemanticsVersion"], f"recipe {recipe_id}.executionSemanticsVersion", 1,
		)
		_expect(
			execution_semantics_version >= MIN_SUPPORTED_EXECUTION_SEMANTICS_VERSION,
			f"recipe {recipe_id}.executionSemanticsVersion",
			"predates the explicit causal-tail contract",
		)
		_integer(recipe["mixCurveVersion"], f"recipe {recipe_id}.mixCurveVersion", 1)
		for range_name in ("noiseReductionRange", "naturalCrispRange"):
			control_range = recipe[range_name]
			_expect(isinstance(control_range, list) and len(control_range) == 2, f"recipe {recipe_id}.{range_name}", "expected [minimum, maximum]")
			minimum = _integer(control_range[0], f"recipe {recipe_id}.{range_name}[0]")
			maximum = _integer(control_range[1], f"recipe {recipe_id}.{range_name}[1]")
			_expect(maximum <= 100 and minimum <= maximum, f"recipe {recipe_id}.{range_name}", "invalid control interval")
		_expect(isinstance(recipe["modelIds"], list), f"recipe {recipe_id}.modelIds", "expected an array")
		for model_id in recipe["modelIds"]:
			_expect(model_id in models, f"recipe {recipe_id}.modelIds", f"unknown model {model_id!r}")
			_expect(recipe_id in models[str(model_id)]["recipeCompatibility"], f"model {model_id}.recipeCompatibility", f"missing {recipe_id}")
		recipes[recipe_id] = recipe
	expected_engines = {
		"Original": "None", "Light": "Speex", "Balanced": "RNNoise",
		"Quality": "DeepFilterNet", "VoiceFocus": "DeepFilterNet",
	}
	for profile in CORE_PROFILES:
		matches = [recipe for recipe in recipes.values() if recipe["profile"] == profile and recipe.get("advancedOnly") is not True]
		_expect(len(matches) == 1, f"input-recipes.json core profile {profile}", f"requires exactly one public recipe; found {len(matches)}")
		_expect(matches[0]["engine"] == expected_engines[profile], f"input-recipes.json core profile {profile}", "unexpected engine")
	return models_manifest, recipes_manifest, models, recipes


def _public_recipe(profile: str, recipes: Mapping[str, Mapping[str, Any]]) -> Mapping[str, Any]:
	matches = [recipe for recipe in recipes.values() if recipe["profile"] == profile and recipe.get("advancedOnly") is not True]
	_expect(len(matches) == 1, f"recipe {profile}", "requires exactly one public recipe")
	return matches[0]


def _milliseconds_to_samples(value: Any, path: str) -> int:
	milliseconds = _number(value, path)
	_expect(milliseconds >= 0.0, path, "must be non-negative")
	exact = milliseconds * SAMPLE_RATE_HZ / 1000.0
	samples = round(exact)
	_expect(math.isclose(exact, samples, rel_tol=0.0, abs_tol=1e-9), path, "must resolve to an exact 48 kHz sample count")
	return samples


def _is_embedded_rnnoise_model(recipe: Mapping[str, Any], model: Mapping[str, Any]) -> bool:
	return (
		recipe["engine"] == "RNNoise"
		and model.get("id") == "rnnoise:embedded"
		and model.get("backend") == "RNNoise"
		and model.get("path") == "rnnoise.dll"
	)


def _expected_latency_samples(recipe: Mapping[str, Any], model: Mapping[str, Any] | None) -> int:
	"""Derive exact latency from the recipe's signed execution contract."""

	execution_semantics_version = _integer(
		recipe["executionSemanticsVersion"], f"recipe {recipe['id']}.executionSemanticsVersion", 1,
	)
	_expect(
		execution_semantics_version >= MIN_SUPPORTED_EXECUTION_SEMANTICS_VERSION,
		f"recipe {recipe['id']}.executionSemanticsVersion",
		"predates the explicit causal-tail contract",
	)
	engine = str(recipe["engine"])
	if engine == "None":
		_expect(model is None and not recipe["modelIds"], f"recipe {recipe['id']}", "Original must not bind a model")
		expected = 0
	elif engine == "Speex":
		_expect(model is None and not recipe["modelIds"], f"recipe {recipe['id']}", "Speex must not bind a model")
		# Every supported semantics version publishes the causal preceding-frame delay.
		expected = FRAME_SAMPLES
	elif engine == "RNNoise":
		_expect(model is not None and model["backend"] == "RNNoise", f"recipe {recipe['id']}", "requires one RNNoise model")
		expected = _milliseconds_to_samples(model["algorithmicLatencyMs"], f"model {model['id']}.algorithmicLatencyMs")
	elif engine == "DeepFilterNet":
		_expect(model is not None and model["backend"] == "DeepFilterNet", f"recipe {recipe['id']}", "requires one DeepFilterNet model")
		# The catalog records the model's intrinsic latency. Semantics v5 uses the
		# realtime worker, which emits frame N at N+2 so inference has two complete
		# callback periods. Older signed recipes recorded the synchronous adapter's
		# single collection frame instead.
		adapter_frames = 2 if execution_semantics_version >= 5 else 1
		expected = (
			_milliseconds_to_samples(model["algorithmicLatencyMs"], f"model {model['id']}.algorithmicLatencyMs")
			+ adapter_frames * FRAME_SAMPLES
		)
	else:
		raise CampaignError(f"recipe {recipe['id']}: unsupported core engine {engine!r}")
	budget = _milliseconds_to_samples(recipe["latencyBudgetMs"], f"recipe {recipe['id']}.latencyBudgetMs")
	_expect(expected <= budget, f"recipe {recipe['id']}.latencyBudgetMs", "is smaller than the exact execution latency")
	_expect(expected % FRAME_SAMPLES == 0, f"recipe {recipe['id']}", "execution latency must be 10 ms frame aligned")
	return expected


def _verify_metrics_python_preflight(
	metrics_python: Path,
	metrics_runtime_root: Path,
	python_binding_value: Any,
	timeout_seconds: int,
) -> Mapping[str, Any]:
	"""Fail before runtime snapshots if ``--metrics-python`` is not the pinned venv."""

	python_binding = _mapping(python_binding_value, "metrics runtime inventory.python")
	_exact_keys(
		python_binding,
		{"executable", "implementation", "venv_root", "version"},
		set(),
		"metrics runtime inventory.python",
	)
	executable_binding = _mapping(
		python_binding["executable"], "metrics runtime inventory.python.executable"
	)
	_exact_keys(
		executable_binding,
		{"sha256", "size_bytes"},
		{"relative_path"},
		"metrics runtime inventory.python.executable",
	)
	expected_executable = {
		"sha256": _hash(
			executable_binding["sha256"], "metrics runtime inventory.python.executable.sha256"
		),
		"size_bytes": _integer(
			executable_binding["size_bytes"], "metrics runtime inventory.python.executable.size_bytes", 1
		),
	}
	actual_executable = _file_record(metrics_python)
	_expect(
		actual_executable == expected_executable,
		"--metrics-python",
		"executable hash/size does not match the pinned metrics runtime",
	)

	probe_timeout = max(1, min(int(timeout_seconds), 30))
	try:
		completed = subprocess.run(
			[str(metrics_python), "-I", "-c", METRICS_PYTHON_PROBE_SOURCE],
			cwd=str(SCRIPT_DIR),
			env=_sanitized_environment(metrics_runtime_root),
			capture_output=True,
			text=True,
			encoding="utf-8",
			errors="replace",
			timeout=probe_timeout,
			check=False,
		)
	except (OSError, subprocess.SubprocessError) as error:
		raise CampaignError(
			f"--metrics-python: unable to execute pinned-runtime preflight before runtime snapshot: {error}"
		) from error
	if completed.returncode != 0:
		detail = (completed.stderr.strip() or completed.stdout.strip() or "no diagnostic output")[:1000]
		raise CampaignError(
			f"--metrics-python: pinned-runtime preflight exited {completed.returncode} before runtime snapshot: {detail}"
		)
	try:
		probe = _mapping(json.loads(completed.stdout), "metrics Python preflight output")
	except (json.JSONDecodeError, TypeError) as error:
		raise CampaignError("--metrics-python: pinned-runtime preflight returned invalid JSON") from error
	_exact_keys(
		probe,
		{"executable", "implementation", "prefix", "version"},
		set(),
		"metrics Python preflight output",
	)
	reported_executable = Path(str(probe["executable"])).resolve()
	reported_prefix = Path(str(probe["prefix"])).resolve()
	expected_prefix = Path(str(python_binding["venv_root"])).resolve()
	_expect(
		os.path.normcase(str(reported_executable)) == os.path.normcase(str(metrics_python.resolve())),
		"--metrics-python",
		"the executed interpreter reported a different sys.executable",
	)
	_expect(
		os.path.normcase(str(reported_prefix)) == os.path.normcase(str(expected_prefix)),
		"--metrics-python",
		"the interpreter is not running in the venv attested by the pinned metrics runtime",
	)
	_expect(
		probe["implementation"] == python_binding["implementation"]
		and probe["version"] == python_binding["version"],
		"--metrics-python",
		"implementation or version does not match the pinned metrics runtime",
	)
	return {
		"status": "passed",
		"executable": _file_record(metrics_python, include_path=True),
		"implementation": str(probe["implementation"]),
		"venv_root": str(reported_prefix),
		"version": str(probe["version"]),
	}


def _build_run_context(args: argparse.Namespace) -> dict[str, Any]:
	plan_path = _regular_file(args.plan, "mixture plan")
	case_set_path = _regular_file(args.case_set, "protected qualification case set") if getattr(args, "case_set", None) is not None else None
	inventory_path = _regular_file(args.inventory, "corpus inventory")
	lock_path = _regular_file(args.corpus_lock, "corpus lock")
	transformation_manifest_path = _regular_file(args.transformation_manifest, "corpus transformation manifest")
	render_root = _directory(args.render_root, "render root")
	render_manifest_path = _regular_file(args.render_manifest, "render manifest")
	runtime_root = _directory(args.runtime_root, "packaged runtime root")
	benchmark = _regular_file(args.benchmark, "speech_cleanup_benchmark")
	metrics_python = _regular_file(args.metrics_python, "pinned metrics Python")
	metrics_runtime_root = _directory(args.metrics_runtime_root, "metrics runtime root")
	metrics_manifest = _regular_file(args.metrics_manifest, "metrics manifest")
	scorer = _regular_file(args.scorer, "objective scorer CLI")
	if not getattr(args, "_allow_fake_tools", False):
		_expect(benchmark.name.lower() == "speech_cleanup_benchmark.exe", "speech_cleanup_benchmark", "release runs require the built benchmark executable")
		_expect(scorer == (SCRIPT_DIR / "score-objective-quality.py").resolve(), "objective scorer CLI", "release runs require the tracked score-objective-quality.py")
	model_manifest_path = _regular_file(args.model_manifest or runtime_root / "input-models.json", "input-models.json")
	recipe_manifest_path = _regular_file(args.recipe_manifest or runtime_root / "input-recipes.json", "input-recipes.json")
	client = _regular_file(runtime_root / "mumble.exe", "packaged mumble.exe")

	for left_name, left, right_name, right in (
		("output root", args.output_root.resolve(), "runtime root", runtime_root),
		("output root", args.output_root.resolve(), "render root", render_root),
		("output root", args.output_root.resolve(), "metrics runtime root", metrics_runtime_root),
	):
		try:
			left.relative_to(right)
		except ValueError:
			try:
				right.relative_to(left)
			except ValueError:
				pass
			else:
				raise CampaignError(f"{left_name} must not contain {right_name}")
		else:
			raise CampaignError(f"{left_name} must not be inside {right_name}")

	plan, inventory, lock, transformation_manifest = _validate_plan_inventory_lock(
		plan_path, inventory_path, lock_path, transformation_manifest_path
	)
	render_manifest, render_entries, render_audio_tree = _validate_render_manifest(plan, render_manifest_path, render_root)
	models_manifest, recipes_manifest, models, recipes = _validate_package(runtime_root, model_manifest_path, recipe_manifest_path)

	try:
		metrics_manifest.relative_to(metrics_runtime_root)
	except ValueError as error:
		raise CampaignError("metrics manifest must be below the pinned metrics runtime root") from error
	try:
		verified_metrics_runtime = dict(
			OBJECTIVE.verify_metrics_runtime(metrics_runtime_root, metrics_manifest, verify_environment=False)
		)
	except Exception as error:
		raise CampaignError(f"pinned metrics runtime failed independent verification: {error}") from error
	verified_metrics_runtime.pop("runtime_root", None)
	metrics_inventory_path = _below(
		metrics_runtime_root,
		str(verified_metrics_runtime["inventory"]["relative_path"]),
		"verified metrics runtime inventory",
	)
	metrics_inventory = _mapping(
		_load_json(metrics_inventory_path, "verified metrics runtime inventory"),
		"verified metrics runtime inventory",
	)
	metrics_python_preflight = (
		{"status": "skipped-for-synthetic-self-test"}
		if getattr(args, "_allow_fake_tools", False)
		else _verify_metrics_python_preflight(
			metrics_python,
			metrics_runtime_root,
			metrics_inventory.get("python"),
			int(args.timeout_seconds),
		)
	)

	toolchain_names = (
		"build-corpus-inventory-v3.py", "generate-mixture-plan.py", "corpus-inventory-v3.py",
		"validate-corpus-lock.py", "render-mixture-plan.py", "score-fixed-timeline.py",
	)
	toolchain = {name: _file_record(SCRIPT_DIR / name, include_path=True) for name in toolchain_names}
	implementation = _regular_file(scorer.with_name("objective_quality_score.py"), "objective scorer implementation")
	fixed_timeline_scorer = _regular_file(SCRIPT_DIR / "score-fixed-timeline.py", "fixed-timeline scorer")
	scorer_files = {
		"cli": _file_record(scorer, include_path=True),
		"implementation": _file_record(implementation, include_path=True),
	}

	model_assets: dict[str, Any] = {}
	for model_id, model in models.items():
		asset = _below(runtime_root, str(model["path"]), f"model {model_id}")
		model_assets[model_id] = {"relative_path": model["path"], **_file_record(asset)}

	runtime_tree = _tree_record(runtime_root)
	metrics_tree = _tree_record(metrics_runtime_root)
	run_binding = {
		"schema_version": 1,
		"campaign": CAMPAIGN_ID,
		"signal_stage": "sender-pre-opus",
		"receiver_capture_forbidden": True,
		"inputs": {
			"plan": _json_record(plan_path, plan),
			"case_set": _file_record(case_set_path, include_path=True) if case_set_path is not None else None,
			"inventory": _json_record(inventory_path, inventory),
			"corpus_lock": _json_record(lock_path, lock),
			"transformation_manifest": _json_record(transformation_manifest_path, transformation_manifest),
			"render_manifest": _json_record(render_manifest_path, render_manifest),
			"render_audio": {
				"file_count": len(render_audio_tree),
				"size_bytes": sum(int(item["size_bytes"]) for item in render_audio_tree),
				"tree_sha256": _canonical_sha256(render_audio_tree),
			},
		},
		"product_runtime": {
			"root": str(runtime_root),
			"tree": runtime_tree,
			"client": _file_record(client, include_path=True),
			"model_manifest": _json_record(model_manifest_path, models_manifest),
			"recipe_manifest": _json_record(recipe_manifest_path, recipes_manifest),
			"model_assets": model_assets,
		},
		"benchmark": _file_record(benchmark, include_path=True),
		"metrics": {
			"python": _file_record(metrics_python, include_path=True),
			"python_preflight": metrics_python_preflight,
			"runtime_root": str(metrics_runtime_root),
			"runtime_tree": metrics_tree,
			"manifest": _json_record(metrics_manifest, _load_json(metrics_manifest, "metrics manifest")),
			"verified_runtime": verified_metrics_runtime,
			"scorer_files": scorer_files,
		},
		"harness": {
			"orchestrator": _file_record(Path(__file__).resolve(), include_path=True),
			"validators": toolchain,
		},
	}
	run_binding_sha256 = _canonical_sha256(run_binding)
	return {
		"plan_path": plan_path,
		"case_set_path": case_set_path,
		"inventory_path": inventory_path,
		"lock_path": lock_path,
		"transformation_manifest_path": transformation_manifest_path,
		"render_root": render_root,
		"render_manifest_path": render_manifest_path,
		"runtime_root": runtime_root,
		"benchmark": benchmark,
		"metrics_python": metrics_python,
		"metrics_runtime_root": metrics_runtime_root,
		"metrics_manifest": metrics_manifest,
		"scorer": scorer,
		"scorer_implementation": implementation,
		"fixed_timeline_scorer": fixed_timeline_scorer,
		"verified_metrics_runtime": verified_metrics_runtime,
		"model_manifest_path": model_manifest_path,
		"recipe_manifest_path": recipe_manifest_path,
		"client": client,
		"plan": plan,
		"inventory": inventory,
		"lock": lock,
		"transformation_manifest": transformation_manifest,
		"render_manifest": render_manifest,
		"render_entries": render_entries,
		"render_audio_tree": render_audio_tree,
		"models_manifest": models_manifest,
		"recipes_manifest": recipes_manifest,
		"models": models,
		"recipes": recipes,
		"run_binding": run_binding,
		"run_binding_sha256": run_binding_sha256,
		"run_id": f"offline-{PLAN.canonical_sha256(plan)[:16]}-{runtime_tree['tree_sha256'][:16]}",
	}


def _case_binding(
	case: Mapping[str, Any], render_entry: Mapping[str, Any], recipe: Mapping[str, Any],
	models: Mapping[str, Mapping[str, Any]], dataset_split: str,
) -> dict[str, Any]:
	controls = _mapping(case["controls"], f"case {case['case_id']}.controls")
	reduction = _integer(controls.get("noise_reduction"), f"case {case['case_id']}.controls.noise_reduction")
	character = _integer(controls.get("natural_clear"), f"case {case['case_id']}.controls.natural_clear")
	_expect(reduction <= 100 and character <= 100, f"case {case['case_id']}.controls", "controls must be <= 100")
	ui_controls = {"noise_reduction": reduction, "natural_clear": character}
	try:
		validated_controls = PLAN.validated_recipe_controls(str(case["profile"]), ui_controls)
	except PLAN.PlanError as error:
		raise CampaignError(f"case {case['case_id']}.controls: {error}") from error
	case_cpu = case.get("cpu_class")
	control_cpu = controls.get("cpu_class")
	_expect(case_cpu is None or control_cpu is None or case_cpu == control_cpu, f"case {case['case_id']}.cpu_class", "conflicting plan CPU classes")
	cpu_class = case_cpu if case_cpu is not None else control_cpu
	cpu_source = "plan" if cpu_class is not None else "public-recipe-minimum"
	if cpu_class is None:
		cpu_class = recipe["minimumCpuClass"]
	_expect(cpu_class in CPU_CLASSES, f"case {case['case_id']}.cpu_class", "unsupported CPU class")
	_expect(CPU_CLASSES.index(str(cpu_class)) >= CPU_CLASSES.index(str(recipe["minimumCpuClass"])), f"case {case['case_id']}.cpu_class", "below the recipe minimum")
	_expect(len(recipe["modelIds"]) <= 1, f"case {case['case_id']}.recipe.modelIds", "core recipes support at most one model")
	recipe_model = models[str(recipe["modelIds"][0])] if recipe["modelIds"] else None
	expected_latency_samples = _expected_latency_samples(recipe, recipe_model)
	model_bindings = []
	for model_id in recipe["modelIds"]:
		model = models[str(model_id)]
		model_bindings.append({
			"id": model_id,
			"relative_path": model["path"],
			"sha256": model["sha256"],
			"size_bytes": model["size"],
		})
	return {
		"case_id": case["case_id"],
		"profile": case["profile"],
		"plan_case_sha256": _canonical_sha256(case),
		"render_entry_sha256": _canonical_sha256(render_entry),
		"controls": ui_controls,
		"validated_recipe_controls": validated_controls,
		"cpu_class": cpu_class,
		"cpu_class_source": cpu_source,
		"recipe": {
			"id": recipe["id"], "revision": recipe["revision"], "engine": recipe["engine"],
			"latency_budget_ms": recipe["latencyBudgetMs"],
			"expected_latency_samples": expected_latency_samples,
			"execution_semantics_version": recipe["executionSemanticsVersion"],
			"mix_curve_version": recipe["mixCurveVersion"],
		},
		"models": model_bindings,
		"condition": "clean" if case["noise"] is None else ("severe" if float(case["mix"]["snr_db"]) <= 0.0 else "noisy"),
		"language": LANGUAGE_MAP[str(case["speech"]["language"])],
		"dataset_split": dataset_split,
	}


def _profile_bindings(context: Mapping[str, Any]) -> list[dict[str, Any]]:
	bindings: list[dict[str, Any]] = []
	recipe_manifest_sha256 = _sha256(context["recipe_manifest_path"])
	catalog_revision = str(context["recipes_manifest"]["catalogRevision"])
	for profile in CORE_PROFILES:
		recipe = _public_recipe(profile, context["recipes"])
		models = []
		for model_id in sorted(str(value) for value in recipe["modelIds"]):
			model = context["models"][model_id]
			version = str(model["version"])
			_expect(bool(version), f"model {model_id}.version", "required for qualification binding")
			models.append({"id": model_id, "sha256": model["sha256"], "version": version})
		bindings.append({
			"profile": profile,
			"engine": recipe["engine"],
			"recipe": {
				"catalog_revision": catalog_revision,
				"id": recipe["id"],
				"manifest_sha256": recipe_manifest_sha256,
				"revision": recipe["revision"],
			},
			"models": models,
		})
	return bindings


def _sanitized_benchmark_report(
	document: Mapping[str, Any], label: str, source_report_path: Path,
	input_path: Path, clean_path: Path, output_path: Path,
) -> dict[str, Any]:
	derived = {
		"schema_version": 1,
		"kind": MEASUREMENT.BENCHMARK_MEASUREMENT_KIND,
		"source_report_sha256": _sha256(source_report_path),
		"input_sha256": _sha256(input_path),
		"clean_reference_sha256": _sha256(clean_path),
		"output_sha256": _sha256(output_path),
	}
	missing = sorted(PUBLIC_BENCHMARK_KEYS - set(document) - set(derived))
	_expect(not missing, label, f"benchmark cannot be published; missing sanitized fields: {', '.join(missing)}")
	return {key: derived[key] if key in derived else document[key] for key in sorted(PUBLIC_BENCHMARK_KEYS)}


def _objective_runtime_binding(document: Mapping[str, Any]) -> str:
	return MEASUREMENT.canonical_json_sha256({
		"runtime": document["runtime"],
		"scorer_files": document["scorer_files"],
	})


def _benchmark_arguments(
	profile: str, controls: Mapping[str, int], cpu_class: str, input_path: Path, clean_path: Path,
	output_path: Path, report_path: Path, model: Mapping[str, Any] | None, model_path: Path | None,
) -> list[str]:
	arguments = [
		"--profile", profile,
		"--noise-reduction", str(controls["noise_reduction"]),
		"--natural-clear", str(controls["natural_clear"]),
		"--cpu-class", cpu_class,
		"--input", str(input_path),
		"--clean-reference", str(clean_path),
		"--output", str(output_path),
		"--report", str(report_path),
	]
	if model is not None:
		assert model_path is not None
		arguments.extend(["--authorized-model-sha256", str(model["sha256"]), "--authorized-model-path", str(model_path)])
	return arguments


def _validate_benchmark_report(
	report_path: Path, output_path: Path, input_path: Path, clean_path: Path, profile: str,
	recipe: Mapping[str, Any], model: Mapping[str, Any] | None, expected_model_path: Path | None,
	expected_ui_controls: Mapping[str, Any], expected_recipe_controls: Mapping[str, Any],
) -> Mapping[str, Any]:
	report = _mapping(_load_json(report_path, f"{profile} benchmark report"), f"{profile} benchmark report")
	for key in (
		"processing_mode", "requested_profile", "active_profile", "requested_recipe_id", "recipe_revision", "active_engine",
		"requested_ui_noise_reduction", "requested_ui_natural_clear",
		"validated_recipe_noise_reduction", "validated_recipe_natural_clear",
		"active_model_id", "active_model_path", "active_model_sha256", "used_fallback", "fallback_reason", "fallback_count",
		"deadline_misses", "reported_latency_ms", "reported_latency_samples", "input_sample_count",
		"clean_reference_sample_count", "output_sample_count", "drain_sample_count",
		"processing_padding_sample_count", "sample_count", "sample_rate", "non_finite_sample_count", "input_saturated_sample_count",
		"saturated_sample_count", "out_of_range_sample_count", "input_path", "clean_reference_path", "output_path", "report_path",
	):
		_expect(key in report, f"{profile} benchmark report", f"missing {key}")
	_expect(report["processing_mode"] == "product-profile", f"{profile} report.processing_mode", "must use the product pipeline")
	_expect(report["requested_profile"] == profile and report["active_profile"] == profile, f"{profile} report.profile", "requested/active profile mismatch")
	_expect(report["requested_recipe_id"] == recipe["id"] and report["recipe_revision"] == recipe["revision"], f"{profile} report.recipe", "recipe mismatch")
	_expect(report["active_engine"] == recipe["engine"], f"{profile} report.active_engine", "engine mismatch")
	for report_key, controls, control_key in (
		("requested_ui_noise_reduction", expected_ui_controls, "noise_reduction"),
		("requested_ui_natural_clear", expected_ui_controls, "natural_clear"),
		("validated_recipe_noise_reduction", expected_recipe_controls, "noise_reduction"),
		("validated_recipe_natural_clear", expected_recipe_controls, "natural_clear"),
	):
		actual_control = _integer(report[report_key], f"{profile} report.{report_key}")
		expected_control = _integer(controls.get(control_key), f"{profile} expected {report_key}")
		_expect(actual_control == expected_control, f"{profile} report.{report_key}", "UI-to-recipe control mapping mismatch")
	_expect(report["used_fallback"] is False and report["fallback_reason"] == "None" and report["fallback_count"] == 0, f"{profile} report.fallback", "fallback is forbidden")
	_expect(report["deadline_misses"] == 0, f"{profile} report.deadline_misses", "deadline misses are forbidden")
	_expect(report["non_finite_sample_count"] == 0 and report["out_of_range_sample_count"] == 0, f"{profile} report.output", "invalid samples are forbidden")
	_expect(_integer(report["saturated_sample_count"], f"{profile} report.saturated_sample_count") <= _integer(report["input_saturated_sample_count"], f"{profile} report.input_saturated_sample_count"), f"{profile} report.clipping", "new clipping is forbidden")
	_expect(report["sample_rate"] == SAMPLE_RATE_HZ, f"{profile} report.sample_rate", "must be 48 kHz")
	expected_latency = _expected_latency_samples(recipe, model)
	latency = _integer(report["reported_latency_samples"], f"{profile} report.reported_latency_samples")
	_expect(latency == expected_latency, f"{profile} report.reported_latency_samples", "does not match the signed recipe execution contract")
	_expect(
		math.isclose(_number(report["reported_latency_ms"], f"{profile} report.reported_latency_ms"), expected_latency * 1000.0 / SAMPLE_RATE_HZ, rel_tol=0.0, abs_tol=1e-9),
		f"{profile} report.reported_latency_ms",
		"does not match the signed recipe execution contract",
	)
	drain = _integer(report["drain_sample_count"], f"{profile} report.drain_sample_count")
	_expect(drain == expected_latency, f"{profile} report.drain_sample_count", "must exactly publish the contract-defined causal latency tail")
	input_wav = _read_wav_fingerprint(input_path)
	clean_wav = _read_wav_fingerprint(clean_path)
	input_samples = _integer(report["input_sample_count"], f"{profile} report.input_sample_count", 1)
	clean_samples = _integer(report["clean_reference_sample_count"], f"{profile} report.clean_reference_sample_count", 1)
	output_samples = _integer(report["output_sample_count"], f"{profile} report.output_sample_count", 1)
	_expect(input_samples == input_wav["frames"], f"{profile} report.input_sample_count", "does not match the actual input WAV")
	_expect(clean_samples == clean_wav["frames"], f"{profile} report.clean_reference_sample_count", "does not match the actual clean-reference WAV")
	_expect(report["processing_padding_sample_count"] == 0, f"{profile} report.processing_padding_sample_count", "rendered 10 ms frames must not need hidden padding")
	_expect(output_samples == input_samples + expected_latency and report["sample_count"] == output_samples, f"{profile} report.output_sample_count", "output/tail timeline mismatch")
	wav = _read_wav_fingerprint(output_path)
	_expect(wav["frames"] == output_samples, f"{profile} output WAV", "report frame count mismatch")
	_expect(wav["frames"] - input_wav["frames"] == expected_latency, f"{profile} output WAV", "actual timeline does not contain the contract-defined latency tail")
	for report_key, expected_path in (("input_path", input_path), ("clean_reference_path", clean_path), ("output_path", output_path), ("report_path", report_path)):
		_expect(Path(str(report[report_key])).resolve() == expected_path.resolve(), f"{profile} report.{report_key}", "path mismatch")
	if model is None:
		_expect(report["active_model_id"] == "" and report["active_model_sha256"] == "" and report["active_model_path"] == "", f"{profile} report.model", "non-neural recipe published a model")
	else:
		assert expected_model_path is not None
		_expect(report["active_model_id"] == model["id"], f"{profile} report.active_model_id", "model mismatch")
		_expect(report["active_model_sha256"] == model["sha256"], f"{profile} report.active_model_sha256", "model hash mismatch")
		reported_model_path = str(report["active_model_path"])
		if not reported_model_path:
			_expect(
				_is_embedded_rnnoise_model(recipe, model),
				f"{profile} report.active_model_path",
				"may only be empty for the manifest-authorized embedded RNNoise model",
			)
		else:
			_expect(Path(reported_model_path).resolve() == expected_model_path.resolve(), f"{profile} report.active_model_path", "model path mismatch")
		_same_file_record(expected_model_path, {"sha256": model["sha256"], "size_bytes": model["size"]}, f"{profile} authorized model")
	return report


def _objective_arguments(
	context: Mapping[str, Any], case: Mapping[str, Any], binding: Mapping[str, Any], clean_path: Path,
	original_path: Path, candidate_path: Path, original_latency: int, candidate_latency: int,
	private_reference: Path, objective_path: Path, execution_scorer: Path,
	execution_metrics_runtime_root: Path, execution_metrics_manifest: Path,
) -> list[str]:
	return [
		str(execution_scorer),
		"--case-id", str(case["case_id"]),
		"--profile", str(case["profile"]),
		"--condition", str(binding["condition"]),
		"--dataset-split", str(context["plan"]["split"]),
		"--signal-stage", "sender-pre-opus",
		"--clean-reference", str(clean_path),
		"--noisy-original", str(original_path),
		"--candidate", str(candidate_path),
		"--original-latency-samples", str(original_latency),
		"--candidate-latency-samples", str(candidate_latency),
		"--metrics-runtime-root", str(execution_metrics_runtime_root),
		"--metrics-manifest", str(execution_metrics_manifest),
		"--language", str(binding["language"]),
		"--wer-reference-kind", "clean-asr-consistency",
		"--clean-asr-reference", str(private_reference),
		"--output", str(objective_path),
	]


def _fixed_timeline_arguments(
	clean_path: Path, candidate_path: Path, latency_samples: int, output_path: Path,
) -> list[str]:
	return [
		"--reference", str(clean_path),
		"--received", str(candidate_path),
		"--latency-samples", str(latency_samples),
		"--output", str(output_path),
		"--max-onset-loss-samples", str(FRAME_SAMPLES),
		"--max-end-loss-samples", str(FRAME_SAMPLES),
		"--require-complete-tail",
		"--fail-on-new-clipping",
	]


def _validate_edge_fixed_timeline(
	path: Path, clean_path: Path, candidate_path: Path, latency_samples: int,
) -> Mapping[str, Any]:
	document = _mapping(_load_json(path, "edge fixed-timeline score"), "edge fixed-timeline score")
	_expect(
		document.get("schema_version") == 3 and document.get("scorer") == "mumble-fixed-timeline-v3",
		"edge fixed-timeline score",
		"unsupported scorer",
	)
	_expect(document.get("timeline_alignment") == "fixed", "edge fixed-timeline score.timeline_alignment", "correlation/route alignment is forbidden")
	_expect(document.get("sample_rate_hz") == SAMPLE_RATE_HZ and document.get("frame_samples") == FRAME_SAMPLES, "edge fixed-timeline score", "invalid sample/frame rate")
	_expect(document.get("declared_latency_samples") == latency_samples, "edge fixed-timeline score.declared_latency_samples", "does not match the independently derived recipe latency")
	_expect(document.get("reference_sha256") == _sha256(clean_path), "edge fixed-timeline score.reference_sha256", "does not bind the exact clean reference")
	_expect(document.get("received_sha256") == _sha256(candidate_path), "edge fixed-timeline score.received_sha256", "does not bind the exact candidate output")
	limits = _mapping(document.get("qualification_limits"), "edge fixed-timeline score.qualification_limits")
	_exact_keys(
		limits,
		{"fail_on_new_clipping", "max_end_loss_samples", "max_onset_loss_samples", "require_complete_tail"},
		set(),
		"edge fixed-timeline score.qualification_limits",
	)
	_expect(
		limits == {
			"max_onset_loss_samples": FRAME_SAMPLES,
			"max_end_loss_samples": FRAME_SAMPLES,
			"require_complete_tail": True,
			"fail_on_new_clipping": True,
		},
		"edge fixed-timeline score.qualification_limits",
		"strict one-frame edge, complete-tail, and clipping limits are required",
	)
	onset = _integer(document.get("onset_loss_samples"), "edge fixed-timeline score.onset_loss_samples")
	end = _integer(document.get("end_loss_samples"), "edge fixed-timeline score.end_loss_samples")
	missing_tail = _integer(document.get("missing_tail_samples"), "edge fixed-timeline score.missing_tail_samples")
	reference_clipped = _integer(document.get("reference_clipped_samples"), "edge fixed-timeline score.reference_clipped_samples")
	received_clipped = _integer(document.get("received_clipped_samples"), "edge fixed-timeline score.received_clipped_samples")
	computed_pass = (
		onset <= FRAME_SAMPLES and end <= FRAME_SAMPLES and missing_tail == 0
		and received_clipped <= reference_clipped
	)
	_expect(document.get("passed") is computed_pass and computed_pass, "edge fixed-timeline score.passed", "strict edge/tail/clipping gate failed")
	clean_frames = _read_wav_fingerprint(clean_path)["frames"]
	candidate_frames = _read_wav_fingerprint(candidate_path)["frames"]
	_expect(document.get("reference_samples") == clean_frames, "edge fixed-timeline score.reference_samples", "does not match clean WAV")
	_expect(document.get("received_samples") == candidate_frames, "edge fixed-timeline score.received_samples", "does not match candidate WAV")
	return document


def _validate_audio_record(record: Mapping[str, Any], path: Path, label: str) -> None:
	expected = {**_file_record(path), **{key: value for key, value in _read_wav_fingerprint(path).items() if key != "sample_sha256_float32le"}}
	for key, value in expected.items():
		_expect(record.get(key) == value, f"objective score.inputs.{label}.{key}", "artifact mismatch")


def _validate_objective(
	objective_path: Path, context: Mapping[str, Any], case: Mapping[str, Any], binding: Mapping[str, Any],
	clean_path: Path, original_path: Path, candidate_path: Path, original_latency: int, candidate_latency: int,
	private_reference: Path,
) -> Mapping[str, Any]:
	document = _mapping(_load_json(objective_path, "objective score"), "objective score")
	try:
		OBJECTIVE.validate_score_document(document)
	except Exception as error:
		raise CampaignError(f"objective score failed strict schema validation: {error}") from error
	_expect(document["status"] == "passed", "objective score.status", "score did not pass")
	_expect(document["case_id"] == case["case_id"] and document["profile"] == case["profile"], "objective score identity", "case/profile mismatch")
	_expect(document["condition"] == binding["condition"] and document["dataset_split"] == context["plan"]["split"], "objective score cohort", "condition/split mismatch")
	alignment = _mapping(document["alignment"], "objective score.alignment")
	_expect(alignment.get("method") == "caller-declared-fixed-latency", "objective score.alignment.method", "fixed declared latency is required")
	_expect(alignment.get("correlation_search_used") is False, "objective score.alignment.correlation_search_used", "correlation alignment is forbidden")
	_expect(alignment.get("signal_stage") == "sender-pre-opus", "objective score.alignment.signal_stage", "receiver capture is forbidden")
	_expect(alignment.get("qualified_route_binding") is None, "objective score.alignment.qualified_route_binding", "route/receiver binding is forbidden")
	_expect(alignment.get("sample_rate_hz") == SAMPLE_RATE_HZ, "objective score.alignment.sample_rate_hz", "must be 48 kHz")
	_expect(alignment.get("original_latency_samples") == original_latency and alignment.get("candidate_latency_samples") == candidate_latency, "objective score.alignment", "declared latency mismatch")
	_expect(alignment.get("original_window_start_samples") == original_latency and alignment.get("candidate_window_start_samples") == candidate_latency, "objective score.alignment", "sender-pre-Opus windows must start at declared latency")
	inputs = _mapping(document["inputs"], "objective score.inputs")
	_validate_audio_record(_mapping(inputs.get("clean_reference"), "objective score.inputs.clean_reference"), clean_path, "clean_reference")
	_validate_audio_record(_mapping(inputs.get("noisy_original"), "objective score.inputs.noisy_original"), original_path, "noisy_original")
	_validate_audio_record(_mapping(inputs.get("candidate"), "objective score.inputs.candidate"), candidate_path, "candidate")
	reference = _mapping(document["wer_reference"], "objective score.wer_reference")
	_expect(reference.get("kind") == "clean-asr-consistency" and reference.get("label") == "clean-ASR-consistency WER", "objective score.wer_reference", "must be explicitly labelled clean-ASR consistency")
	_expect(reference.get("language") == binding["language"], "objective score.wer_reference.language", "language mismatch")
	artifact = _mapping(reference.get("artifact"), "objective score.wer_reference.artifact")
	_expect(artifact == _file_record(private_reference), "objective score.wer_reference.artifact", "private reference mismatch")
	_expect(
		document["runtime"] == context["verified_metrics_runtime"],
		"objective score.runtime",
		"does not match the independently verified pinned metrics runtime",
	)
	scorer_files = _mapping(document["scorer_files"], "objective score.scorer_files")
	expected_scorer_files = {
		name: {
			"name": Path(str(record["path"])).name,
			"sha256": record["sha256"],
			"size_bytes": record["size_bytes"],
		}
		for name, record in context["run_binding"]["metrics"]["scorer_files"].items()
	}
	_expect(scorer_files == expected_scorer_files, "objective score.scorer_files", "scorer implementation or CLI drift")
	deltas = _mapping(document["candidate_minus_original"], "objective score.candidate_minus_original")
	for key in ("dnsmos_bak", "dnsmos_ovrl", "dnsmos_sig", "estoi", "wer_delta_percentage_points"):
		_number(deltas.get(key), f"objective score.candidate_minus_original.{key}")
	_expect(deltas.get("wer_delta_kind") == "clean-asr-consistency", "objective score.candidate_minus_original.wer_delta_kind", "reference mismatch")
	if case["profile"] == "Original":
		_assert_same_samples(original_path, candidate_path, "Original candidate")
		for key in ("dnsmos_bak", "dnsmos_ovrl", "dnsmos_sig", "estoi", "wer_delta_percentage_points"):
			_expect(abs(float(deltas[key])) <= 1e-12, f"Original objective delta {key}", "must be exactly zero")
		if "metrics" in document:
			_expect(document["metrics"].get("original") == document["metrics"].get("candidate"), "Original objective metrics", "must be identical")
	return document


def _attempt_artifacts_valid(
	manifest_path: Path, output_root: Path, run_id: str, run_binding_sha256: str, case_id: str,
	attempt_number: int, case_binding: Mapping[str, Any], case_binding_sha256: str,
) -> Mapping[str, Any] | None:
	try:
		manifest = _mapping(_load_json(manifest_path, "case manifest"), "case manifest")
		_exact_keys(
			manifest,
			{
				"artifacts", "attempt", "campaign", "case_binding", "case_binding_sha256", "case_id",
				"execution", "inputs", "receiver_capture_used", "run_binding_sha256", "run_id",
				"schema_version", "signal_stage", "status",
			},
			set(),
			"case manifest",
		)
		_expect(
			manifest["schema_version"] == 1 and manifest["campaign"] == CAMPAIGN_ID
			and manifest["status"] == "passed" and manifest["run_id"] == run_id
			and manifest["run_binding_sha256"] == run_binding_sha256 and manifest["case_id"] == case_id
			and manifest["attempt"] == attempt_number and manifest["case_binding_sha256"] == case_binding_sha256
			and manifest["signal_stage"] == "sender-pre-opus" and manifest["receiver_capture_used"] is False,
			"case manifest",
			"identity or execution-stage mismatch",
		)
		_expect(manifest["case_binding"] == case_binding, "case manifest.case_binding", "binding bytes changed")
		_expect(_canonical_sha256(manifest["case_binding"]) == case_binding_sha256, "case manifest.case_binding", "canonical hash mismatch")

		inputs = _mapping(manifest["inputs"], "case manifest.inputs")
		_exact_keys(inputs, {"clean_reference", "rendered_input"}, set(), "case manifest.inputs")
		for name, value in inputs.items():
			record = _mapping(value, f"case manifest.inputs.{name}")
			_exact_keys(
				record,
				{"channels", "frames", "sample_rate_hz", "sample_sha256_float32le", "sha256", "size_bytes"},
				set(),
				f"case manifest.inputs.{name}",
			)
			_expect(record["channels"] == 1 and record["sample_rate_hz"] == SAMPLE_RATE_HZ, f"case manifest.inputs.{name}", "must be mono 48 kHz")
			_hash(record["sha256"], f"case manifest.inputs.{name}.sha256")
			_hash(record["sample_sha256_float32le"], f"case manifest.inputs.{name}.sample_sha256_float32le")
			_integer(record["frames"], f"case manifest.inputs.{name}.frames", 1)
			_integer(record["size_bytes"], f"case manifest.inputs.{name}.size_bytes", 1)

		execution = _mapping(manifest["execution"], "case manifest.execution")
		_exact_keys(
			execution,
			{
				"candidate_active_engine", "candidate_active_model_id", "candidate_active_model_sha256",
				"candidate_active_profile", "candidate_declared_latency_samples",
				"original_declared_latency_samples",
			},
			set(),
			"case manifest.execution",
		)
		expected_model = case_binding["models"][0] if case_binding["models"] else None
		_expect(
			execution
			== {
				"original_declared_latency_samples": 0,
				"candidate_declared_latency_samples": case_binding["recipe"]["expected_latency_samples"],
				"candidate_active_profile": case_binding["profile"],
				"candidate_active_engine": case_binding["recipe"]["engine"],
				"candidate_active_model_id": expected_model["id"] if expected_model is not None else "",
				"candidate_active_model_sha256": expected_model["sha256"] if expected_model is not None else "",
			},
			"case manifest.execution",
			"does not match the signed case binding",
		)

		artifacts = _mapping(manifest.get("artifacts"), "case manifest.artifacts")
		_exact_keys(
			artifacts,
			set(CASE_ARTIFACT_FILENAMES) | {"private_clean_asr_reference"},
			set(),
			"case manifest.artifacts",
		)
		for name, value in artifacts.items():
			record = _mapping(value, f"case manifest.artifacts.{name}")
			_exact_keys(record, {"relative_path", "sha256", "size_bytes"}, set(), f"case manifest.artifacts.{name}")
			relative = _safe_relative(record.get("relative_path"), f"case manifest.artifacts.{name}.relative_path")
			expected_relative = (
				f"private-references/{case_id}.json"
				if name == "private_clean_asr_reference"
				else f"cases/{case_id}/attempt-{attempt_number:03d}/{CASE_ARTIFACT_FILENAMES[name]}"
			)
			_expect(relative == expected_relative, f"case manifest.artifacts.{name}.relative_path", "unexpected artifact path")
			path = _below(output_root, relative, f"case artifact {name}")
			if _file_record(path) != {"sha256": record.get("sha256"), "size_bytes": record.get("size_bytes")}:
				return None
		return manifest
	except (CampaignError, KeyError, TypeError, ValueError):
		return None


def _find_resumable(
	output_root: Path, run_id: str, case_id: str, run_binding_sha256: str,
	case_binding: Mapping[str, Any], case_binding_sha256: str,
) -> tuple[int, Path, Mapping[str, Any]] | None:
	case_root = output_root / "cases" / case_id
	if not case_root.is_dir() or _is_reparse(case_root):
		return None
	for attempt in sorted(case_root.glob("attempt-*"), reverse=True):
		if not attempt.is_dir() or _is_reparse(attempt):
			continue
		match = re.fullmatch(r"attempt-(\d{3})", attempt.name)
		if match is None:
			continue
		manifest_path = attempt / "case-manifest.json"
		if not manifest_path.is_file() or _is_reparse(manifest_path):
			continue
		attempt_number = int(match.group(1))
		manifest = _attempt_artifacts_valid(
			manifest_path, output_root, run_id, run_binding_sha256, case_id,
			attempt_number, case_binding, case_binding_sha256,
		)
		if manifest is not None:
			return attempt_number, manifest_path, manifest
	return None


def _next_attempt_root(output_root: Path, case_id: str) -> tuple[int, Path]:
	case_root = output_root / "cases" / case_id
	case_root.mkdir(parents=True, exist_ok=True)
	_expect(not _is_reparse(case_root), str(case_root), "case root cannot be a symlink/reparse point")
	numbers = []
	for path in case_root.glob("attempt-*"):
		match = re.fullmatch(r"attempt-(\d{3})", path.name)
		if match:
			numbers.append(int(match.group(1)))
	number = max(numbers, default=0) + 1
	_expect(number <= 999, f"case {case_id}", "too many attempts")
	root = case_root / f"attempt-{number:03d}"
	root.mkdir()
	return number, root


def _campaign_document(
	context: Mapping[str, Any], case_results: Sequence[Mapping[str, Any]], status: str,
	executed: int, resumed: int, error: str | None = None,
) -> dict[str, Any]:
	document: dict[str, Any] = {
		"schema_version": SCHEMA_VERSION,
		"campaign": CAMPAIGN_ID,
		"status": status,
		"run_id": context["run_id"],
		"run_binding_sha256": context["run_binding_sha256"],
		"run_binding": context["run_binding"],
		"privacy": {
			"private_audio_do_not_upload": True,
			"private_clean_asr_references_do_not_publish": True,
			"signal_stage": "sender-pre-opus",
			"receiver_capture_forbidden": True,
			"correlation_alignment_forbidden": True,
		},
		"plan": {
			"canonical_sha256": PLAN.canonical_sha256(context["plan"]),
			"suite": context["plan"]["suite"],
			"split": context["plan"]["split"],
			"case_count": len(context["plan"]["cases"]),
		},
		"case_results": list(case_results),
		"summary": {
			"total": len(context["plan"]["cases"]),
			"passed": sum(result.get("status") == "passed" for result in case_results),
			"failed": sum(result.get("status") == "failed" for result in case_results),
			"executed_this_invocation": executed,
			"resumed_this_invocation": resumed,
		},
	}
	if error is not None:
		document["error"] = error
	return document


def _reject_public_path_leaks(value: Any, path: str = "public report") -> None:
	if isinstance(value, dict):
		for key, child in value.items():
			_reject_public_path_leaks(child, f"{path}.{key}")
	elif isinstance(value, list):
		for index, child in enumerate(value):
			_reject_public_path_leaks(child, f"{path}[{index}]")
	elif isinstance(value, str):
		_expect(
			not re.match(r"^[A-Za-z]:[\\/]", value) and not value.startswith(("/", "\\\\")),
			path,
			"absolute/local path is forbidden in public measurement evidence",
		)


def _materialize_measurement_fragments(
	context: Mapping[str, Any], output_root: Path, execution_root: Path,
	case_work: Sequence[tuple[Mapping[str, Any], Mapping[str, Any], Mapping[str, Any], str, Any]],
	case_results: Sequence[Mapping[str, Any]],
) -> Mapping[str, Any]:
	fragment_root = output_root / "measurement-fragments"
	_remove_private_tree(output_root, fragment_root, "stale measurement fragments")
	fragment_root.mkdir()
	profile_bindings = _profile_bindings(context)
	profile_binding_by_name = {str(binding["profile"]): binding for binding in profile_bindings}
	result_by_case = {str(result["case_id"]): result for result in case_results}
	work_by_case = {str(case["case_id"]): (case, render_entry, binding, binding_sha) for case, render_entry, binding, binding_sha, _ in case_work}
	_expect(set(result_by_case) == set(work_by_case), "measurement fragments", "case result/work set mismatch")

	metrics_files = _tree_file_inventory(context["metrics_runtime_root"])
	metrics_runtime_sha256 = MEASUREMENT.canonical_json_sha256(metrics_files)
	product_model_ids = {
		str(model_id)
		for recipe in context["recipes"].values()
		if recipe.get("advancedOnly") is not True
		and recipe.get("profile") in ("Balanced", "Quality", "VoiceFocus", "Auto")
		for model_id in recipe["modelIds"]
	}
	try:
		benchmark_relative_path = context["benchmark"].relative_to(context["runtime_root"]).as_posix()
	except ValueError:
		benchmark_relative_path = None
	build_binding = {
		"tested_binary_sha256": context["run_binding"]["product_runtime"]["client"]["sha256"],
		"staged_payload_sha256": MEASUREMENT.canonical_json_sha256(_tree_file_inventory(context["runtime_root"])),
		"harness_sha256": context["run_binding"]["harness"]["orchestrator"]["sha256"],
		"corpus_lock_sha256": context["run_binding"]["inputs"]["corpus_lock"]["canonical_sha256"],
		"corpus_inventory_sha256": context["run_binding"]["inputs"]["inventory"]["sha256"],
		"mixture_plan_sha256": context["run_binding"]["inputs"]["plan"]["sha256"],
		"case_set_sha256": (
			context["run_binding"]["inputs"]["case_set"]["sha256"]
			if context["run_binding"]["inputs"]["case_set"] is not None else None
		),
		"metrics_runtime_sha256": metrics_runtime_sha256,
		"model_manifest_sha256": _sha256(context["model_manifest_path"]),
		"recipe_manifest_sha256": _sha256(context["recipe_manifest_path"]),
		"recipe_set_version": str(context["recipes_manifest"]["catalogRevision"]),
		"model_hashes": sorted(str(context["models"][model_id]["sha256"]) for model_id in product_model_ids),
		"benchmark_in_staged_payload": benchmark_relative_path is not None,
		"benchmark_relative_path": benchmark_relative_path,
		"benchmark_sha256": context["run_binding"]["benchmark"]["sha256"],
	}
	_expect(bool(build_binding["model_hashes"]), "measurement fragments.build_binding.model_hashes", "at least one packaged model is required")

	case_fragments: list[dict[str, Any]] = []
	allowlist: dict[str, Mapping[str, Any]] = {}
	objective_runtime_bindings: set[str] = set()
	for case_id in sorted(work_by_case, key=lambda value: (CORE_PROFILES.index(str(work_by_case[value][0]["profile"])), value)):
		case, render_entry, binding, binding_sha = work_by_case[case_id]
		result = result_by_case[case_id]
		manifest_path = _below(output_root, result["manifest"]["relative_path"], f"{case_id} passed case manifest")
		manifest = _attempt_artifacts_valid(
			manifest_path, output_root, context["run_id"], context["run_binding_sha256"], case_id,
			int(result["attempt"]), binding, binding_sha,
		)
		_expect(manifest is not None, f"measurement fragment {case_id}", "case artifacts no longer match their passed manifest")
		assert manifest is not None
		artifacts = manifest["artifacts"]

		def raw_artifact(name: str) -> Path:
			return _below(output_root, artifacts[name]["relative_path"], f"{case_id} raw artifact {name}")

		input_path = _below(context["render_root"], render_entry["input"]["path"], f"{case_id} rendered input")
		clean_path = _below(context["render_root"], render_entry["clean_reference"]["path"], f"{case_id} clean reference")
		original_wav = raw_artifact("original_wav")
		candidate_wav = raw_artifact("candidate_wav")
		original_report_path = raw_artifact("original_report")
		candidate_report_path = raw_artifact("candidate_report")
		edge_score_path = raw_artifact("edge_fixed_timeline_score")
		objective_path = raw_artifact("objective_score")
		private_reference = raw_artifact("private_clean_asr_reference")
		original_recipe = _public_recipe("Original", context["recipes"])
		candidate_recipe = _public_recipe(str(case["profile"]), context["recipes"])
		candidate_model = context["models"][str(candidate_recipe["modelIds"][0])] if candidate_recipe["modelIds"] else None
		candidate_model_path = (
			_below(execution_root, str(candidate_model["path"]), f"{case_id} execution model")
			if candidate_model is not None else None
		)
		ui_controls = binding["controls"]
		original_recipe_controls = PLAN.validated_recipe_controls("Original", ui_controls)
		original_report = _validate_benchmark_report(
			original_report_path, original_wav, input_path, clean_path, "Original", original_recipe, None, None,
			ui_controls, original_recipe_controls,
		)
		candidate_report = _validate_benchmark_report(
			candidate_report_path, candidate_wav, input_path, clean_path, str(case["profile"]),
			candidate_recipe, candidate_model, candidate_model_path,
			ui_controls, binding["validated_recipe_controls"],
		)
		edge_score = _validate_edge_fixed_timeline(
			edge_score_path, clean_path, candidate_wav, int(candidate_report["reported_latency_samples"]),
		)
		objective = _validate_objective(
			objective_path, context, case, binding, clean_path, original_wav, candidate_wav,
			int(original_report["reported_latency_samples"]), int(candidate_report["reported_latency_samples"]),
			private_reference,
		)
		objective_runtime_bindings.add(_objective_runtime_binding(objective))

		public_root = fragment_root / "cases" / str(case["profile"]) / case_id
		case_binding_report = {
			"schema_version": 1,
			"kind": "mumble-input-enhancement-case-binding-v1",
			"measurement_mode": "offline",
			"case_id": case_id,
			"profile": case["profile"],
			"condition": binding["condition"],
			"dataset_split": context["plan"]["split"],
			"build_binding": {
				field: build_binding[field]
				for field in ("case_set_sha256", "corpus_inventory_sha256", "corpus_lock_sha256", "mixture_plan_sha256")
			},
			"plan_case_sha256": binding["plan_case_sha256"],
			"render_manifest_sha256": _sha256(context["render_manifest_path"]),
			"render_entry_sha256": binding["render_entry_sha256"],
			"source_input_sha256": render_entry["input"]["sha256"],
			"clean_reference_sha256": render_entry["clean_reference"]["sha256"],
		}
		public_documents = {
			"original_benchmark_report": (
				"original-benchmark.json",
				_sanitized_benchmark_report(
					original_report, f"{case_id} Original report", original_report_path,
					input_path, clean_path, original_wav,
				),
			),
			"candidate_benchmark_report": (
				"candidate-benchmark.json",
				_sanitized_benchmark_report(
					candidate_report, f"{case_id} candidate report", candidate_report_path,
					input_path, clean_path, candidate_wav,
				),
			),
			"case_binding_report": ("case-binding.json", case_binding_report),
			"edge_fixed_timeline_score": ("edge-fixed-timeline-score.json", edge_score),
			"objective_score": ("objective-quality.json", objective),
		}
		report_references: dict[str, Mapping[str, Any]] = {}
		for name, (filename, document) in public_documents.items():
			_reject_public_path_leaks(document, f"measurement fragment {case_id}.{name}")
			public_path = public_root / filename
			_write_canonical_json_atomic(public_path, document)
			reference = _audio_free_reference(public_path, fragment_root)
			report_references[name] = reference
			allowlist[str(reference["path"])] = reference

		index_entry = {
			"case_id": case_id,
			"profile": case["profile"],
			"condition": binding["condition"],
			"dataset_split": context["plan"]["split"],
			"measurement_mode": "offline",
			"plan_case_sha256": binding["plan_case_sha256"],
			"render_entry_sha256": binding["render_entry_sha256"],
			"source_input_sha256": render_entry["input"]["sha256"],
			"clean_reference_sha256": render_entry["clean_reference"]["sha256"],
			"reports": report_references,
		}
		case_fragment = {
			"schema_version": 1,
			"kind": MEASUREMENT_CASE_FRAGMENT_KIND,
			"campaign": CAMPAIGN_ID,
			"run_id": context["run_id"],
			"run_binding_sha256": context["run_binding_sha256"],
			"case_binding_sha256": binding_sha,
			"case_binding": binding,
			"profile_binding": profile_binding_by_name[str(case["profile"])],
			"index_entry": index_entry,
		}
		case_fragment_path = public_root / "measurement-fragment.json"
		_write_canonical_json_atomic(case_fragment_path, case_fragment)
		fragment_reference = _audio_free_reference(case_fragment_path, fragment_root)
		allowlist[str(fragment_reference["path"])] = fragment_reference
		case_fragments.append({"case_id": case_id, "profile": case["profile"], "fragment": fragment_reference})

	_expect(len(objective_runtime_bindings) == 1, "measurement fragments", "objective cases do not share one exact scorer/runtime binding")
	objective_runtime_binding_sha256 = next(iter(objective_runtime_bindings))
	metrics_attestation = {
		"schema_version": 1,
		"kind": METRICS_RUNTIME_ATTESTATION_KIND,
		"payload_kind": "directory",
		"payload_sha256": metrics_runtime_sha256,
		"objective_runtime_binding_sha256": objective_runtime_binding_sha256,
		"files": metrics_files,
	}
	metrics_attestation_path = fragment_root / "metrics-runtime-attestation.json"
	_write_canonical_json_atomic(metrics_attestation_path, metrics_attestation)
	metrics_attestation_reference = _audio_free_reference(metrics_attestation_path, fragment_root)
	allowlist[str(metrics_attestation_reference["path"])] = metrics_attestation_reference

	fragment_manifest = {
		"schema_version": 1,
		"kind": MEASUREMENT_FRAGMENT_KIND,
		"status": "passed",
		"campaign": CAMPAIGN_ID,
		"run_id": context["run_id"],
		"run_binding_sha256": context["run_binding_sha256"],
		"qualification_contract": {
			"qualification_scope": "core",
			"allowed_suite": "pr_smoke",
			"measurement_mode": "offline",
			"holdout_forbidden": True,
		},
		"source_plan_suite": context["plan"]["suite"],
		"dataset_split": context["plan"]["split"],
		"render_manifest_sha256": _sha256(context["render_manifest_path"]),
		"plan_binding": {
			"case_set_sha256": build_binding["case_set_sha256"],
			"corpus_inventory_sha256": build_binding["corpus_inventory_sha256"],
			"corpus_lock_sha256": build_binding["corpus_lock_sha256"],
			"mixture_plan_sha256": build_binding["mixture_plan_sha256"],
		},
		"build_binding": build_binding,
		"profile_bindings": profile_bindings,
		"objective_runtime_binding_sha256": objective_runtime_binding_sha256,
		"metrics_runtime_attestation": metrics_attestation_reference,
		"cases": case_fragments,
		"artifact_allowlist": [allowlist[path] for path in sorted(allowlist)],
	}
	manifest_path = fragment_root / "measurement-fragments.json"
	_write_canonical_json_atomic(manifest_path, fragment_manifest)
	return {
		"manifest": _relative_record(manifest_path, output_root),
		"artifact_count": len(allowlist),
		"case_count": len(case_fragments),
		"objective_runtime_binding_sha256": objective_runtime_binding_sha256,
		"metrics_runtime_sha256": metrics_runtime_sha256,
	}


ASSEMBLY_KIND = "mumble-offline-measurement-index-assembly-v1"
PUBLISHED_ARTIFACT_SUFFIXES = {
	"case_evidence_jsonl": ".jsonl",
	"failure_spectrogram_index": ".json",
	"junit": ".xml",
	"per_case_csv": ".csv",
	"per_case_parquet": ".parquet",
	"summary_html": ".html",
	"summary_json": ".json",
}


def _load_canonical_json(path: Path, label: str) -> Mapping[str, Any]:
	path = _regular_file(path, label)
	raw = path.read_bytes()
	value = _mapping(_load_json(path, label), label)
	_expect(raw == _canonical_bytes(value) + b"\n", label, "must be canonical sorted-key UTF-8 JSON with one LF")
	return value


def _validate_qualification_build(build_value: Any, fragment: Mapping[str, Any]) -> Mapping[str, Any]:
	build = _mapping(build_value, "assembly.build")
	_exact_keys(build, QUALIFICATION_BUILD_KEYS, set(), "assembly.build")
	_expect(isinstance(build["git_sha"], str) and bool(re.fullmatch(r"[0-9a-f]{40}", build["git_sha"])), "assembly.build.git_sha", "invalid Git commit")
	_expect(build["runner_class"] in ("low-performance", "mainstream", "local-development"), "assembly.build.runner_class", "unsupported runner class")
	_expect(isinstance(build["recipe_set_version"], str) and bool(build["recipe_set_version"]), "assembly.build.recipe_set_version", "required")
	for field in QUALIFICATION_BUILD_KEYS - {"git_sha", "model_hashes", "recipe_set_version", "runner_class"}:
		_hash(build[field], f"assembly.build.{field}")
	models = build["model_hashes"]
	_expect(isinstance(models, list) and bool(models), "assembly.build.model_hashes", "expected a non-empty array")
	validated_models = [_hash(value, f"assembly.build.model_hashes[{index}]") for index, value in enumerate(models)]
	_expect(validated_models == sorted(set(validated_models)), "assembly.build.model_hashes", "must be sorted and unique")
	derived = _mapping(fragment["build_binding"], "measurement fragments.build_binding")
	_expect(
		derived.get("benchmark_in_staged_payload") is True
		and isinstance(derived.get("benchmark_relative_path"), str),
		"measurement fragments.build_binding.benchmark_in_staged_payload",
		"benchmark executable is outside the staged payload; campaign is tuning-only",
	)
	_hash(derived.get("benchmark_sha256"), "measurement fragments.build_binding.benchmark_sha256")
	_expect(
		isinstance(derived.get("case_set_sha256"), str),
		"measurement fragments.build_binding.case_set_sha256",
		"campaign was not bound to --case-set and is tuning-only",
	)
	for field in (
		"tested_binary_sha256", "staged_payload_sha256", "harness_sha256", "corpus_lock_sha256",
		"corpus_inventory_sha256", "mixture_plan_sha256", "case_set_sha256", "metrics_runtime_sha256",
		"model_manifest_sha256", "recipe_manifest_sha256", "recipe_set_version", "model_hashes",
	):
		_expect(build[field] == derived[field], f"assembly.build.{field}", "does not match the hash-attested campaign fragment")
	return build


def _resolve_audio_free_reference(root: Path, value: Any, label: str, prefix: str | None = None) -> tuple[Mapping[str, Any], Path]:
	reference = _mapping(value, label)
	_exact_keys(reference, {"contains_audio_samples", "path", "sha256", "size_bytes"}, set(), label)
	_expect(reference["contains_audio_samples"] is False, f"{label}.contains_audio_samples", "must be false")
	relative = _safe_relative(reference["path"], f"{label}.path")
	if prefix is not None:
		_expect(relative.startswith(prefix), f"{label}.path", f"must be below {prefix}")
	_expect(PurePosixPath(relative).suffix.lower() not in MEASUREMENT.AUDIO_SUFFIXES, f"{label}.path", "audio artifact is forbidden")
	path = _below(root, relative, label)
	_expect(path.stat().st_size == _integer(reference["size_bytes"], f"{label}.size_bytes", 1), f"{label}.size_bytes", "artifact size mismatch")
	_expect(_sha256(path) == _hash(reference["sha256"], f"{label}.sha256"), f"{label}.sha256", "artifact hash mismatch")
	return reference, path


def _immutable_copy(source: Path, target: Path, artifact_root: Path, label: str) -> Mapping[str, Any]:
	artifact_root = _directory(artifact_root, "qualification artifact root")
	target = target.resolve()
	try:
		target.relative_to(artifact_root)
	except ValueError as error:
		raise CampaignError(f"{label}: target escapes qualification artifact root") from error
	current = artifact_root
	for part in target.relative_to(artifact_root).parts[:-1]:
		current = current / part
		if current.exists():
			_expect(current.is_dir() and not _is_reparse(current), label, "target parent is not a regular directory")
		else:
			current.mkdir()
	payload = source.read_bytes()
	if target.exists():
		_expect(target.is_file() and not _is_reparse(target), label, "existing target is not a regular file")
		_expect(target.read_bytes() == payload, label, "immutable target already exists with different bytes")
	else:
		temporary = target.with_name(f".{target.name}.{os.getpid()}.tmp")
		temporary.write_bytes(payload)
		os.replace(temporary, target)
	return _audio_free_reference(target, artifact_root)


def _immutable_write_canonical_json(value: Any, target: Path, artifact_root: Path, label: str) -> Mapping[str, Any]:
	artifact_root = _directory(artifact_root, "qualification artifact root")
	target = target.resolve()
	try:
		target.relative_to(artifact_root)
	except ValueError as error:
		raise CampaignError(f"{label}: target escapes qualification artifact root") from error
	current = artifact_root
	for part in target.relative_to(artifact_root).parts[:-1]:
		current = current / part
		if current.exists():
			_expect(current.is_dir() and not _is_reparse(current), label, "target parent is not a regular directory")
		else:
			current.mkdir()
	payload = _canonical_bytes(value) + b"\n"
	if target.exists():
		_expect(target.is_file() and not _is_reparse(target), label, "existing target is not a regular file")
		_expect(target.read_bytes() == payload, label, "immutable target already exists with different bytes")
	else:
		temporary = target.with_name(f".{target.name}.{os.getpid()}.tmp")
		temporary.write_bytes(payload)
		os.replace(temporary, target)
	return _audio_free_reference(target, artifact_root)


def assemble_measurement_index(
	campaign_root: Path, artifact_root: Path, envelope_path: Path, output_path: Path | None,
) -> Mapping[str, Any]:
	campaign_root = _directory(campaign_root, "campaign root")
	artifact_root = _directory(artifact_root, "qualification artifact root")
	campaign = _mapping(_load_json(campaign_root / "campaign-manifest.json", "campaign manifest"), "campaign manifest")
	_expect(campaign.get("campaign") == CAMPAIGN_ID and campaign.get("status") == "passed", "campaign manifest", "must be a passing offline campaign")
	fragment_record = _mapping(campaign.get("measurement_fragments"), "campaign manifest.measurement_fragments")
	fragment_manifest_record = _mapping(fragment_record.get("manifest"), "campaign manifest.measurement_fragments.manifest")
	fragment_manifest_path = _below(campaign_root, fragment_manifest_record["relative_path"], "measurement fragment manifest")
	_expect(_sha256(fragment_manifest_path) == fragment_manifest_record["sha256"] and fragment_manifest_path.stat().st_size == fragment_manifest_record["size_bytes"], "measurement fragment manifest", "campaign reference mismatch")
	fragment = _load_canonical_json(fragment_manifest_path, "measurement fragment manifest")
	_exact_keys(fragment, {
		"artifact_allowlist", "build_binding", "campaign", "cases", "dataset_split", "kind",
		"metrics_runtime_attestation", "objective_runtime_binding_sha256", "plan_binding", "profile_bindings",
		"qualification_contract", "render_manifest_sha256", "run_binding_sha256", "run_id", "schema_version",
		"source_plan_suite", "status",
	}, set(), "measurement fragment manifest")
	_expect(fragment["schema_version"] == 1 and fragment["kind"] == MEASUREMENT_FRAGMENT_KIND and fragment["status"] == "passed", "measurement fragment manifest", "unsupported fragment")
	_expect(fragment["campaign"] == CAMPAIGN_ID and fragment["run_id"] == campaign["run_id"] and fragment["run_binding_sha256"] == campaign["run_binding_sha256"], "measurement fragment manifest", "campaign binding mismatch")
	_expect(fragment["source_plan_suite"] == "pr_smoke", "measurement fragment manifest.source_plan_suite", "offline measurement index may only be assembled from a pr_smoke plan")
	_expect(fragment["dataset_split"] in ("tuning", "validation"), "measurement fragment manifest.dataset_split", "holdout/non-development split is forbidden")
	fragment_root = fragment_manifest_path.parent

	envelope = _mapping(_load_json(envelope_path, "measurement index assembly envelope"), "measurement index assembly envelope")
	_exact_keys(envelope, {"build", "kind", "published_artifacts", "qualification_scope", "schema_version", "suite"}, set(), "assembly")
	_expect(envelope["schema_version"] == 1 and envelope["kind"] == ASSEMBLY_KIND, "assembly", "unsupported envelope")
	_expect(envelope["qualification_scope"] == "core" and envelope["suite"] == "pr_smoke", "assembly", "offline assembler is restricted to core/pr_smoke")
	build = _validate_qualification_build(envelope["build"], fragment)
	profile_bindings = fragment["profile_bindings"]
	validated_profile_bindings = MEASUREMENT._validate_profile_bindings(profile_bindings, "core", build)
	prefix = f"artifacts/pr_smoke-{build['runner_class']}/"

	published_value = _mapping(envelope["published_artifacts"], "assembly.published_artifacts")
	_exact_keys(published_value, set(PUBLISHED_ARTIFACT_SUFFIXES), set(), "assembly.published_artifacts")
	published = []
	for name in sorted(PUBLISHED_ARTIFACT_SUFFIXES):
		reference, _ = _resolve_audio_free_reference(artifact_root, published_value[name], f"assembly.published_artifacts.{name}", prefix)
		_expect(PurePosixPath(str(reference["path"])).suffix.lower() == PUBLISHED_ARTIFACT_SUFFIXES[name], f"assembly.published_artifacts.{name}.path", "unexpected suffix")
		published.append({"name": name, "artifact": dict(reference)})

	case_entries = []
	objective_runtime_bindings: set[str] = set()
	fragment_allowlist = {}
	used_fragment_paths: set[str] = set()
	for position, value in enumerate(fragment["artifact_allowlist"]):
		reference, _ = _resolve_audio_free_reference(fragment_root, value, f"measurement fragments.artifact_allowlist[{position}]")
		path = str(reference["path"])
		_expect(path not in fragment_allowlist, f"measurement fragments.artifact_allowlist[{position}]", "duplicate path")
		fragment_allowlist[path] = reference
	_expect(list(fragment_allowlist) == sorted(fragment_allowlist), "measurement fragments.artifact_allowlist", "must be sorted")

	for position, summary_value in enumerate(fragment["cases"]):
		summary = _mapping(summary_value, f"measurement fragments.cases[{position}]")
		_exact_keys(summary, {"case_id", "fragment", "profile"}, set(), f"measurement fragments.cases[{position}]")
		case_reference, case_path = _resolve_audio_free_reference(fragment_root, summary["fragment"], f"measurement fragments.cases[{position}].fragment")
		_expect(fragment_allowlist.get(str(case_reference["path"])) == case_reference, f"measurement fragments.cases[{position}].fragment", "not present in exact fragment allowlist")
		used_fragment_paths.add(str(case_reference["path"]))
		case_fragment = _load_canonical_json(case_path, f"measurement case fragment {summary['case_id']}")
		_exact_keys(case_fragment, {
			"campaign", "case_binding", "case_binding_sha256", "index_entry", "kind", "profile_binding",
			"run_binding_sha256", "run_id", "schema_version",
		}, set(), f"measurement case fragment {summary['case_id']}")
		_expect(case_fragment["schema_version"] == 1 and case_fragment["kind"] == MEASUREMENT_CASE_FRAGMENT_KIND, f"measurement case fragment {summary['case_id']}", "unsupported fragment")
		_expect(case_fragment["campaign"] == CAMPAIGN_ID and case_fragment["run_id"] == fragment["run_id"] and case_fragment["run_binding_sha256"] == fragment["run_binding_sha256"], f"measurement case fragment {summary['case_id']}", "campaign binding mismatch")
		_expect(_canonical_sha256(case_fragment["case_binding"]) == case_fragment["case_binding_sha256"], f"measurement case fragment {summary['case_id']}.case_binding", "hash mismatch")
		entry = _mapping(case_fragment["index_entry"], f"measurement case fragment {summary['case_id']}.index_entry")
		_exact_keys(entry, {
			"case_id", "clean_reference_sha256", "condition", "dataset_split", "measurement_mode",
			"plan_case_sha256", "profile", "render_entry_sha256", "reports", "source_input_sha256",
		}, set(), f"measurement case fragment {summary['case_id']}.index_entry")
		_expect(entry["case_id"] == summary["case_id"] and entry["profile"] == summary["profile"], f"measurement case fragment {summary['case_id']}", "summary mismatch")
		_expect(entry["measurement_mode"] == "offline", f"measurement case fragment {summary['case_id']}.measurement_mode", "must be offline")
		_expect(case_fragment["profile_binding"] in profile_bindings, f"measurement case fragment {summary['case_id']}.profile_binding", "not in campaign profile bindings")

		target_root = artifact_root.joinpath(*PurePosixPath(prefix).parts) / "measurements" / str(entry["profile"]) / str(entry["case_id"])
		final_reports = {}
		reports = _mapping(entry["reports"], f"measurement case fragment {summary['case_id']}.reports")
		_expect(set(reports) == MEASUREMENT.OFFLINE_REPORT_KEYS, f"measurement case fragment {summary['case_id']}.reports", "offline report map is not exact")
		for report_name, report_value in reports.items():
			source_reference, source_path = _resolve_audio_free_reference(fragment_root, report_value, f"measurement case fragment {summary['case_id']}.reports.{report_name}")
			_expect(fragment_allowlist.get(str(source_reference["path"])) == source_reference, f"measurement case fragment {summary['case_id']}.reports.{report_name}", "not present in exact fragment allowlist")
			used_fragment_paths.add(str(source_reference["path"]))
			filename = PurePosixPath(str(source_reference["path"])).name
			if report_name == "case_binding_report":
				document = _mapping(_load_canonical_json(source_path, f"measurement case fragment {summary['case_id']}.case_binding_report"), "case binding report")
				document = {
					**document,
					"build_binding": {
						field: build[field]
						for field in ("case_set_sha256", "corpus_inventory_sha256", "corpus_lock_sha256", "mixture_plan_sha256")
					},
				}
				final_reports[report_name] = _immutable_write_canonical_json(
					document, target_root / filename, artifact_root,
					f"assembly case {entry['case_id']} report {report_name}",
				)
			else:
				final_reports[report_name] = _immutable_copy(
					source_path, target_root / filename, artifact_root,
					f"assembly case {entry['case_id']} report {report_name}",
				)
		objective_document = _load_canonical_json(
			artifact_root.joinpath(*PurePosixPath(str(final_reports["objective_score"]["path"])).parts),
			f"assembly case {entry['case_id']} objective score",
		)
		objective_runtime_bindings.add(_objective_runtime_binding(objective_document))
		final_documents = {
			name: _load_canonical_json(
				artifact_root.joinpath(*PurePosixPath(str(reference["path"])).parts),
				f"assembly case {entry['case_id']} report {name}",
			)
			for name, reference in final_reports.items()
		}
		derived = MEASUREMENT._derive_offline_case(
			entry, objective_document, final_documents, build, validated_profile_bindings,
			f"assembly case {entry['case_id']}",
		)
		_expect(
			final_documents["case_binding_report"]["render_manifest_sha256"] == fragment["render_manifest_sha256"],
			f"assembly case {entry['case_id']}.render_manifest_sha256",
			"does not match the campaign render manifest",
		)
		for field in ("clean_reference_sha256", "source_input_sha256", "plan_case_sha256", "render_entry_sha256"):
			_expect(entry[field] == derived[field], f"assembly case {entry['case_id']}.{field}", "does not match report-derived binding")
		case_entries.append({**{key: value for key, value in entry.items() if key != "reports"}, "reports": final_reports})

	_expect(len(case_entries) == len(fragment["cases"]), "assembly cases", "case count mismatch")
	_expect(
		[(str(entry["profile"]), str(entry["case_id"])) for entry in case_entries]
		== sorted(
			[(str(entry["profile"]), str(entry["case_id"])) for entry in case_entries],
			key=lambda item: (CORE_PROFILES.index(item[0]), item[1]),
		),
		"assembly cases",
		"must use canonical unique profile/case order",
	)
	_expect(len({(str(entry["profile"]), str(entry["case_id"])) for entry in case_entries}) == len(case_entries), "assembly cases", "duplicate profile/case")
	_expect(len(objective_runtime_bindings) == 1, "assembly objective scores", "runtime/scorer binding differs across cases")
	objective_runtime_binding_sha256 = next(iter(objective_runtime_bindings))
	_expect(objective_runtime_binding_sha256 == fragment["objective_runtime_binding_sha256"], "assembly objective runtime binding", "fragment mismatch")
	metrics_reference, metrics_path = _resolve_audio_free_reference(fragment_root, fragment["metrics_runtime_attestation"], "measurement fragments.metrics_runtime_attestation")
	used_fragment_paths.add(str(metrics_reference["path"]))
	_expect(set(fragment_allowlist) == used_fragment_paths, "measurement fragments.artifact_allowlist", "contains missing or unused artifacts")
	metrics_document = _load_canonical_json(metrics_path, "measurement fragments.metrics_runtime_attestation")
	_expect(metrics_document["payload_sha256"] == build["metrics_runtime_sha256"] and metrics_document["objective_runtime_binding_sha256"] == objective_runtime_binding_sha256, "measurement fragments.metrics_runtime_attestation", "build/objective binding mismatch")
	metrics_target = artifact_root.joinpath(*PurePosixPath(prefix).parts) / "measurements" / "metrics-runtime-attestation.json"
	final_metrics_reference = _immutable_copy(metrics_path, metrics_target, artifact_root, "assembly metrics runtime attestation")
	metrics_references: dict[str, Mapping[str, Any]] = {}
	MEASUREMENT._verify_metrics_runtime_attestation(
		artifact_root, prefix, final_metrics_reference, build,
		objective_runtime_binding_sha256, metrics_references,
	)
	_expect(
		metrics_references == {str(final_metrics_reference["path"]): final_metrics_reference},
		"assembly metrics runtime attestation",
		"semantic validation produced an unexpected artifact reference",
	)

	index = {
		"schema_version": 1,
		"kind": MEASUREMENT.INDEX_KIND,
		"qualification_scope": "core",
		"suite": "pr_smoke",
		"qualification_binding_sha256": CASE_EVIDENCE.qualification_binding_sha256(build, "core", "pr_smoke"),
		"build": dict(build),
		"plan_binding": {
			field: build[field]
			for field in ("case_set_sha256", "corpus_inventory_sha256", "corpus_lock_sha256", "mixture_plan_sha256")
		},
		"profile_bindings": profile_bindings,
		"objective_runtime_binding_sha256": objective_runtime_binding_sha256,
		"metrics_runtime_attestation": final_metrics_reference,
		"published_artifacts": published,
		"cases": case_entries,
		"release_holdout_approval_public_key_sha256": None,
		"release_holdout_openings": [],
		"soak_reports": [],
		"transitions": [],
	}
	expected_output = artifact_root.joinpath(*PurePosixPath(prefix).parts) / "measurement-index.json"
	if output_path is None:
		output_path = expected_output
	else:
		output_path = output_path.resolve()
	_expect(output_path == expected_output.resolve(), "measurement index output", f"must be {expected_output}")
	if output_path.exists():
		_expect(output_path.is_file() and not _is_reparse(output_path), "measurement index output", "existing target is not a regular file")
		_expect(output_path.read_bytes() == _canonical_bytes(index) + b"\n", "measurement index output", "immutable index target already exists with different bytes")
	else:
		_write_canonical_json_atomic(output_path, index)
	allowlist = MEASUREMENT.indexed_artifact_references(index, prefix)
	return {
		"measurement_index": _audio_free_reference(output_path, artifact_root),
		"qualification_binding_sha256": index["qualification_binding_sha256"],
		"case_count": len(case_entries),
		"transitive_artifact_count": len(allowlist),
		"transitive_artifact_allowlist": [allowlist[path] for path in sorted(allowlist)],
	}


def _critical_files_stable(context: Mapping[str, Any], execution_root: Path | None = None) -> None:
	binding = context["run_binding"]
	for name, path in (
		("plan", context["plan_path"]),
		("inventory", context["inventory_path"]),
		("corpus_lock", context["lock_path"]),
		("transformation_manifest", context["transformation_manifest_path"]),
		("render_manifest", context["render_manifest_path"]),
	):
		_same_file_record(path, binding["inputs"][name], name.replace("_", " "))
	if context["case_set_path"] is not None:
		_same_file_record(context["case_set_path"], binding["inputs"]["case_set"], "protected qualification case set")
	_same_file_record(context["benchmark"], binding["benchmark"], "benchmark")
	_same_file_record(context["client"], binding["product_runtime"]["client"], "packaged client")
	_same_file_record(context["model_manifest_path"], binding["product_runtime"]["model_manifest"], "model manifest")
	_same_file_record(context["recipe_manifest_path"], binding["product_runtime"]["recipe_manifest"], "recipe manifest")
	_same_file_record(context["metrics_python"], binding["metrics"]["python"], "metrics Python")
	_same_file_record(context["metrics_manifest"], binding["metrics"]["manifest"], "metrics manifest")
	for name, record in binding["metrics"]["scorer_files"].items():
		_same_file_record(Path(str(record["path"])), record, f"objective scorer {name}")
	_same_file_record(Path(__file__).resolve(), binding["harness"]["orchestrator"], "offline campaign orchestrator")
	for name, record in binding["harness"]["validators"].items():
		_same_file_record(Path(str(record["path"])), record, f"harness validator {name}")
	for model_id, record in binding["product_runtime"]["model_assets"].items():
		_same_file_record(_below(context["runtime_root"], record["relative_path"], f"source model {model_id}"), record, f"source model {model_id}")
		if execution_root is not None:
			_same_file_record(_below(execution_root, record["relative_path"], f"execution model {model_id}"), record, f"execution model {model_id}")


def _remove_private_tree(output_root: Path, path: Path, label: str) -> None:
	if not path.exists():
		return
	_expect(path.resolve().parent == output_root.resolve(), label, "unsafe cleanup target")
	_expect(not _is_reparse(path), label, "refusing to remove a reparse point")
	_expect(path.is_dir(), label, "cleanup target is not a directory")
	shutil.rmtree(path)


def _execution_environment_stable(execution: Mapping[str, Any]) -> None:
	_expect(
		_tree_record(execution["product_root"]) == execution["product_tree"],
		"private product execution runtime",
		"payload mutated during campaign",
	)
	_expect(
		_tree_record(execution["metrics_root"]) == execution["metrics_tree"],
		"private metrics execution runtime",
		"payload mutated during campaign",
	)
	_expect(
		_tree_record(execution["scorer_root"]) == execution["scorer_tree"],
		"private objective scorer",
		"payload mutated during campaign",
	)


def _prepare_execution_runtime(context: Mapping[str, Any], output_root: Path) -> Mapping[str, Any]:
	_critical_files_stable(context)
	_expect(
		_tree_record(context["runtime_root"]) == context["run_binding"]["product_runtime"]["tree"],
		"packaged runtime",
		"payload changed before private snapshot",
	)
	_expect(
		_tree_record(context["metrics_runtime_root"]) == context["run_binding"]["metrics"]["runtime_tree"],
		"metrics runtime",
		"payload changed before private snapshot",
	)

	execution_root = output_root / "_private-execution-runtime"
	shutil.copytree(context["runtime_root"], execution_root, symlinks=False)
	_expect(
		_tree_record(execution_root) == context["run_binding"]["product_runtime"]["tree"],
		"execution runtime",
		"copy does not match the packaged payload",
	)
	execution_benchmark = execution_root / context["benchmark"].name
	if execution_benchmark.exists():
		_same_file_record(execution_benchmark, context["run_binding"]["benchmark"], "packaged benchmark copy")
	else:
		shutil.copy2(context["benchmark"], execution_benchmark)
		_same_file_record(execution_benchmark, context["run_binding"]["benchmark"], "execution benchmark copy")

	execution_metrics_root = output_root / "_private-metrics-runtime"
	shutil.copytree(context["metrics_runtime_root"], execution_metrics_root, symlinks=False)
	execution_metrics_tree = _tree_record(execution_metrics_root)
	_expect(
		execution_metrics_tree == context["run_binding"]["metrics"]["runtime_tree"],
		"private metrics execution runtime",
		"copy does not match the pinned metrics runtime",
	)
	metrics_manifest_relative = context["metrics_manifest"].relative_to(context["metrics_runtime_root"]).as_posix()
	execution_metrics_manifest = _below(
		execution_metrics_root, metrics_manifest_relative, "private metrics manifest"
	)
	_same_file_record(
		execution_metrics_manifest, context["run_binding"]["metrics"]["manifest"], "private metrics manifest"
	)

	execution_scorer_root = output_root / "_private-objective-scorer"
	execution_scorer_root.mkdir()
	execution_scorer = execution_scorer_root / context["scorer"].name
	execution_scorer_implementation = execution_scorer_root / context["scorer_implementation"].name
	execution_fixed_timeline_scorer = execution_scorer_root / context["fixed_timeline_scorer"].name
	shutil.copy2(context["scorer"], execution_scorer)
	shutil.copy2(context["scorer_implementation"], execution_scorer_implementation)
	shutil.copy2(context["fixed_timeline_scorer"], execution_fixed_timeline_scorer)
	_same_file_record(execution_scorer, context["run_binding"]["metrics"]["scorer_files"]["cli"], "private scorer CLI")
	_same_file_record(
		execution_scorer_implementation,
		context["run_binding"]["metrics"]["scorer_files"]["implementation"],
		"private scorer implementation",
	)
	_same_file_record(
		execution_fixed_timeline_scorer,
		context["run_binding"]["harness"]["validators"]["score-fixed-timeline.py"],
		"private fixed-timeline scorer",
	)

	_critical_files_stable(context, execution_root)
	execution = {
		"product_root": execution_root,
		"benchmark": execution_benchmark,
		"product_tree": _tree_record(execution_root),
		"metrics_root": execution_metrics_root,
		"metrics_manifest": execution_metrics_manifest,
		"metrics_tree": execution_metrics_tree,
		"scorer_root": execution_scorer_root,
		"scorer": execution_scorer,
		"scorer_implementation": execution_scorer_implementation,
		"fixed_timeline_scorer": execution_fixed_timeline_scorer,
		"scorer_tree": _tree_record(execution_scorer_root),
	}
	_execution_environment_stable(execution)
	return execution


def _sanitized_environment(execution_root: Path) -> dict[str, str]:
	environment = {
		name: value
		for name, value in os.environ.items()
		if not any(name.upper().startswith(prefix) for prefix in UNSAFE_ENVIRONMENT_PREFIXES)
	}
	environment["PATH"] = str(execution_root) + os.pathsep + environment.get("PATH", "")
	environment["PYTHONDONTWRITEBYTECODE"] = "1"
	environment["PYTHONNOUSERSITE"] = "1"
	environment["HF_HUB_OFFLINE"] = "1"
	environment["TRANSFORMERS_OFFLINE"] = "1"
	environment["HF_DATASETS_OFFLINE"] = "1"
	return environment


def _execute_case(
	context: Mapping[str, Any], args: argparse.Namespace, output_root: Path, execution: Mapping[str, Any],
	case: Mapping[str, Any], render_entry: Mapping[str, Any],
	binding: Mapping[str, Any], case_binding_sha256: str,
) -> tuple[int, Path, Mapping[str, Any]]:
	case_id = str(case["case_id"])
	execution_root = execution["product_root"]
	execution_benchmark = execution["benchmark"]
	attempt_number, attempt_root = _next_attempt_root(output_root, case_id)
	manifest_path = attempt_root / "case-manifest.json"
	input_path = _below(context["render_root"], render_entry["input"]["path"], f"{case_id} rendered input")
	clean_path = _below(context["render_root"], render_entry["clean_reference"]["path"], f"{case_id} clean reference")
	_expect(_sha256(input_path) == render_entry["input"]["sha256"], f"{case_id} rendered input", "hash changed after run binding")
	_expect(_sha256(clean_path) == render_entry["clean_reference"]["sha256"], f"{case_id} clean reference", "hash changed after run binding")
	original_wav = attempt_root / "original-sender-pre-opus.wav"
	original_report = attempt_root / "original-benchmark.json"
	original_stdout = attempt_root / "original-benchmark.stdout.txt"
	original_stderr = attempt_root / "original-benchmark.stderr.txt"
	candidate_wav = attempt_root / "candidate-sender-pre-opus.wav"
	candidate_report = attempt_root / "candidate-benchmark.json"
	candidate_stdout = attempt_root / "candidate-benchmark.stdout.txt"
	candidate_stderr = attempt_root / "candidate-benchmark.stderr.txt"
	edge_fixed_timeline_score = attempt_root / "edge-fixed-timeline-score.json"
	edge_fixed_timeline_stdout = attempt_root / "edge-fixed-timeline-score.stdout.txt"
	edge_fixed_timeline_stderr = attempt_root / "edge-fixed-timeline-score.stderr.txt"
	objective_path = attempt_root / "objective-quality.json"
	objective_stdout = attempt_root / "objective-score.stdout.txt"
	objective_stderr = attempt_root / "objective-score.stderr.txt"
	private_reference = output_root / "private-references" / f"{case_id}.json"
	private_reference.parent.mkdir(parents=True, exist_ok=True)

	original_recipe = _public_recipe("Original", context["recipes"])
	candidate_recipe = _public_recipe(str(case["profile"]), context["recipes"])
	_expect(len(original_recipe["modelIds"]) == 0, "Original recipe", "must not use a model")
	_expect(len(candidate_recipe["modelIds"]) <= 1, f"{case_id} candidate recipe", "offline core harness supports one exact authorized model")
	candidate_model = context["models"][str(candidate_recipe["modelIds"][0])] if candidate_recipe["modelIds"] else None
	candidate_model_path = (
		_below(execution_root, str(candidate_model["path"]), f"{case_id} candidate model") if candidate_model is not None else None
	)

	environment = _sanitized_environment(execution_root)
	controls = binding["controls"]
	try:
		_critical_files_stable(context, execution_root)
		original_arguments = _benchmark_arguments(
			"Original", controls, str(original_recipe["minimumCpuClass"]), input_path, clean_path,
			original_wav, original_report, None, None,
		)
		_run(
			_tool_command(execution_benchmark, original_arguments), execution_root, environment, args.timeout_seconds,
			original_stdout, original_stderr, f"{case_id} Original benchmark",
		)
		original_document = _validate_benchmark_report(
			original_report, original_wav, input_path, clean_path, "Original", original_recipe, None, None,
			controls, PLAN.validated_recipe_controls("Original", controls),
		)
		_expect(original_document["reported_latency_samples"] == 0 and original_document["drain_sample_count"] == 0, f"{case_id} Original timeline", "must add zero latency/tail")
		_assert_same_samples(input_path, original_wav, f"{case_id} Original exact input")

		candidate_arguments = _benchmark_arguments(
			str(case["profile"]), controls, str(binding["cpu_class"]), input_path, clean_path,
			candidate_wav, candidate_report, candidate_model, candidate_model_path,
		)
		_run(
			_tool_command(execution_benchmark, candidate_arguments), execution_root, environment, args.timeout_seconds,
			candidate_stdout, candidate_stderr, f"{case_id} candidate benchmark",
		)
		candidate_document = _validate_benchmark_report(
			candidate_report, candidate_wav, input_path, clean_path, str(case["profile"]), candidate_recipe,
			candidate_model, candidate_model_path, controls, binding["validated_recipe_controls"],
		)
		if case["profile"] == "Original":
			_assert_same_samples(original_wav, candidate_wav, f"{case_id} Original repeat")

		_run(
			[
				str(context["metrics_python"]), str(execution["fixed_timeline_scorer"]),
				*_fixed_timeline_arguments(
					clean_path, candidate_wav, int(candidate_document["reported_latency_samples"]),
					edge_fixed_timeline_score,
				),
			],
			execution["scorer_root"], environment, args.timeout_seconds,
			edge_fixed_timeline_stdout, edge_fixed_timeline_stderr,
			f"{case_id} sender edge fixed-timeline scorer",
		)
		_validate_edge_fixed_timeline(
			edge_fixed_timeline_score, clean_path, candidate_wav,
			int(candidate_document["reported_latency_samples"]),
		)

		objective_arguments = _objective_arguments(
			context, case, binding, clean_path, original_wav, candidate_wav,
			int(original_document["reported_latency_samples"]), int(candidate_document["reported_latency_samples"]),
			private_reference, objective_path, execution["scorer"], execution["metrics_root"],
			execution["metrics_manifest"],
		)
		_run(
			[str(context["metrics_python"]), *objective_arguments], execution["scorer_root"], environment,
			args.timeout_seconds, objective_stdout, objective_stderr, f"{case_id} objective scorer",
		)
		_validate_objective(
			objective_path, context, case, binding, clean_path, original_wav, candidate_wav,
			int(original_document["reported_latency_samples"]), int(candidate_document["reported_latency_samples"]),
			private_reference,
		)
		_critical_files_stable(context, execution_root)
		_execution_environment_stable(execution)
		artifacts = {
			"original_wav": _relative_record(original_wav, output_root),
			"original_report": _relative_record(original_report, output_root),
			"original_stdout": _relative_record(original_stdout, output_root),
			"original_stderr": _relative_record(original_stderr, output_root),
			"candidate_wav": _relative_record(candidate_wav, output_root),
			"candidate_report": _relative_record(candidate_report, output_root),
			"candidate_stdout": _relative_record(candidate_stdout, output_root),
			"candidate_stderr": _relative_record(candidate_stderr, output_root),
			"edge_fixed_timeline_score": _relative_record(edge_fixed_timeline_score, output_root),
			"edge_fixed_timeline_stdout": _relative_record(edge_fixed_timeline_stdout, output_root),
			"edge_fixed_timeline_stderr": _relative_record(edge_fixed_timeline_stderr, output_root),
			"objective_score": _relative_record(objective_path, output_root),
			"objective_stdout": _relative_record(objective_stdout, output_root),
			"objective_stderr": _relative_record(objective_stderr, output_root),
			"private_clean_asr_reference": _relative_record(private_reference, output_root),
		}
		manifest = {
			"schema_version": 1,
			"campaign": CAMPAIGN_ID,
			"status": "passed",
			"run_id": context["run_id"],
			"run_binding_sha256": context["run_binding_sha256"],
			"case_id": case_id,
			"attempt": attempt_number,
			"case_binding_sha256": case_binding_sha256,
			"case_binding": binding,
			"signal_stage": "sender-pre-opus",
			"receiver_capture_used": False,
			"inputs": {
				"rendered_input": {**_file_record(input_path), **_read_wav_fingerprint(input_path)},
				"clean_reference": {**_file_record(clean_path), **_read_wav_fingerprint(clean_path)},
			},
			"execution": {
				"original_declared_latency_samples": int(original_document["reported_latency_samples"]),
				"candidate_declared_latency_samples": int(candidate_document["reported_latency_samples"]),
				"candidate_active_profile": candidate_document["active_profile"],
				"candidate_active_engine": candidate_document["active_engine"],
				"candidate_active_model_id": candidate_document["active_model_id"],
				"candidate_active_model_sha256": candidate_document["active_model_sha256"],
			},
			"artifacts": artifacts,
		}
		_write_json_atomic(manifest_path, manifest)
		return attempt_number, manifest_path, manifest
	except Exception as error:
		available: dict[str, Any] = {}
		for name, path in (
			("original_wav", original_wav), ("original_report", original_report), ("original_stdout", original_stdout),
			("original_stderr", original_stderr), ("candidate_wav", candidate_wav), ("candidate_report", candidate_report),
			("candidate_stdout", candidate_stdout), ("candidate_stderr", candidate_stderr),
			("edge_fixed_timeline_score", edge_fixed_timeline_score),
			("edge_fixed_timeline_stdout", edge_fixed_timeline_stdout),
			("edge_fixed_timeline_stderr", edge_fixed_timeline_stderr), ("objective_score", objective_path),
			("objective_stdout", objective_stdout), ("objective_stderr", objective_stderr),
		):
			if path.is_file() and not _is_reparse(path):
				available[name] = _relative_record(path, output_root)
		failed = {
			"schema_version": 1, "campaign": CAMPAIGN_ID, "status": "failed", "run_id": context["run_id"],
			"run_binding_sha256": context["run_binding_sha256"], "case_id": case_id, "attempt": attempt_number,
			"case_binding_sha256": case_binding_sha256, "case_binding": binding,
			"signal_stage": "sender-pre-opus", "receiver_capture_used": False,
			"error": str(error), "artifacts": available,
		}
		_write_json_atomic(manifest_path, failed)
		raise


def run_campaign(args: argparse.Namespace) -> Mapping[str, Any]:
	_expect(args.timeout_seconds > 0, "--timeout-seconds", "must be positive")
	context = _build_run_context(args)
	output_root = args.output_root.resolve()
	if output_root.exists():
		_expect(output_root.is_dir() and not _is_reparse(output_root), "output root", "must be a regular directory")
	else:
		output_root.mkdir(parents=True)
	campaign_path = output_root / "campaign-manifest.json"
	entries = list(output_root.iterdir())
	if entries and not campaign_path.is_file():
		raise CampaignError("non-empty output root has no campaign-manifest.json; use a new output root")
	if campaign_path.is_file():
		previous = _mapping(_load_json(campaign_path, "existing campaign manifest"), "existing campaign manifest")
		_expect(previous.get("campaign") == CAMPAIGN_ID, "existing campaign manifest.campaign", "wrong campaign type")
		_expect(previous.get("run_binding_sha256") == context["run_binding_sha256"], "existing campaign manifest.run_binding_sha256", "whole-run binding changed; use a new output root")
		_expect(previous.get("run_binding") == context["run_binding"], "existing campaign manifest.run_binding", "whole-run binding bytes changed; use a new output root")
	for private_name, label in (
		("_private-execution-runtime", "stale product execution runtime"),
		("_private-metrics-runtime", "stale metrics execution runtime"),
		("_private-objective-scorer", "stale objective scorer"),
	):
		_remove_private_tree(output_root, output_root / private_name, label)

	case_work: list[tuple[Mapping[str, Any], Mapping[str, Any], Mapping[str, Any], str, tuple[int, Path, Mapping[str, Any]] | None]] = []
	for case in context["plan"]["cases"]:
		case_id = str(case["case_id"])
		render_entry = context["render_entries"][case_id]
		recipe = _public_recipe(str(case["profile"]), context["recipes"])
		binding = _case_binding(case, render_entry, recipe, context["models"], str(context["plan"]["split"]))
		binding_sha = _canonical_sha256(binding)
		resumable = _find_resumable(
			output_root, context["run_id"], case_id, context["run_binding_sha256"], binding, binding_sha
		)
		case_work.append((case, render_entry, binding, binding_sha, resumable))

	case_results: list[dict[str, Any]] = []
	executed = 0
	resumed = 0
	for case, _, _, binding_sha, resumable in case_work:
		if resumable is None:
			continue
		attempt, manifest_path, _ = resumable
		case_results.append({
			"case_id": case["case_id"], "profile": case["profile"], "status": "passed", "source": "resumed",
			"attempt": attempt, "case_binding_sha256": binding_sha,
			"manifest": _relative_record(manifest_path, output_root),
		})
		resumed += 1
	case_results.sort(key=lambda result: result["case_id"])
	_write_json_atomic(campaign_path, _campaign_document(context, case_results, "running", executed, resumed))

	missing = [work for work in case_work if work[4] is None]
	execution: Mapping[str, Any] | None = None
	try:
		if missing:
			execution = _prepare_execution_runtime(context, output_root)
			for case, render_entry, binding, binding_sha, _ in missing:
				try:
					attempt, manifest_path, _ = _execute_case(
						context, args, output_root, execution, case, render_entry, binding, binding_sha,
					)
					executed += 1
					case_results = [result for result in case_results if result["case_id"] != case["case_id"]]
					case_results.append({
						"case_id": case["case_id"], "profile": case["profile"], "status": "passed", "source": "executed",
						"attempt": attempt, "case_binding_sha256": binding_sha,
						"manifest": _relative_record(manifest_path, output_root),
					})
					case_results.sort(key=lambda result: result["case_id"])
					_write_json_atomic(campaign_path, _campaign_document(context, case_results, "running", executed, resumed))
				except Exception as error:
					case_results = [result for result in case_results if result["case_id"] != case["case_id"]]
					case_results.append({
						"case_id": case["case_id"], "profile": case["profile"], "status": "failed", "source": "executed",
						"case_binding_sha256": binding_sha, "error": str(error),
					})
					case_results.sort(key=lambda result: result["case_id"])
					_write_json_atomic(campaign_path, _campaign_document(context, case_results, "failed", executed, resumed, str(error)))
					raise
		_critical_files_stable(context, execution["product_root"] if execution is not None else None)
		_expect(_tree_record(context["runtime_root"]) == context["run_binding"]["product_runtime"]["tree"], "packaged runtime", "payload changed during campaign")
		_expect(_tree_record(context["metrics_runtime_root"]) == context["run_binding"]["metrics"]["runtime_tree"], "metrics runtime", "payload changed during campaign")
		if execution is not None:
			_execution_environment_stable(execution)
		_expect(len(case_results) == len(context["plan"]["cases"]) and all(result["status"] == "passed" for result in case_results), "campaign", "not every case passed")
		if execution is None:
			execution = _prepare_execution_runtime(context, output_root)
		measurement_fragments = _materialize_measurement_fragments(
			context, output_root, execution["product_root"], case_work, case_results,
		)
		final = _campaign_document(context, case_results, "passed", executed, resumed)
		final["measurement_fragments"] = measurement_fragments
		_write_json_atomic(campaign_path, final)
		return final
	finally:
		for private_name, label in (
			("_private-execution-runtime", "product execution runtime cleanup"),
			("_private-metrics-runtime", "metrics execution runtime cleanup"),
			("_private-objective-scorer", "objective scorer cleanup"),
		):
			_remove_private_tree(output_root, output_root / private_name, label)


def _write_pcm16_fixture(path: Path, frequency: float, noisy: bool) -> None:
	path.parent.mkdir(parents=True, exist_ok=True)
	samples = []
	for index in range(SAMPLE_RATE_HZ):
		value = 0.18 * math.sin(2.0 * math.pi * frequency * index / SAMPLE_RATE_HZ)
		if noisy:
			value += 0.04 * math.sin(2.0 * math.pi * 997.0 * index / SAMPLE_RATE_HZ)
		samples.append(max(-32768, min(32767, round(value * 32767.0))))
	with wave.open(str(path), "wb") as stream:
		stream.setnchannels(1)
		stream.setsampwidth(2)
		stream.setframerate(SAMPLE_RATE_HZ)
		stream.writeframes(struct.pack(f"<{len(samples)}h", *samples))


def _fake_benchmark_source() -> str:
	return r'''#!/usr/bin/env python3
import argparse, hashlib, json, os, shutil, struct, wave
from pathlib import Path

p=argparse.ArgumentParser()
for name in ('profile','noise-reduction','natural-clear','cpu-class','input','clean-reference','output','report','authorized-model-sha256','authorized-model-path'):
    p.add_argument('--'+name)
a=p.parse_args()
assert 'MUMBLE_DISABLE_INPUT_ENHANCEMENT' not in os.environ and 'PYTHONPATH' not in os.environ
assert 'PYTHONPYCACHEPREFIX' not in os.environ
assert Path(__file__).resolve().parent.name == '_private-execution-runtime'
counter=Path(os.environ['OFFLINE_CAMPAIGN_SELFTEST_COUNTER'])
value=int(counter.read_text()) if counter.exists() else 0
counter.write_text(str(value+1))
source=Path(a.input); output=Path(a.output)
with wave.open(str(source),'rb') as w:
    params=w.getparams(); frames=w.readframes(w.getnframes()); count=w.getnframes()
with wave.open(str(a.clean_reference),'rb') as w:
    clean_count=w.getnframes()
latency={'Original':0,'Light':480,'Balanced':1440,'Quality':1440,'VoiceFocus':1440}[a.profile]
if latency == 0:
    shutil.copyfile(source,output)
else:
    with wave.open(str(output),'wb') as w:
        w.setparams(params)
        w.writeframes(b'\0\0'*latency+frames)
recipe={'Original':'input.original','Light':'input.light.speex','Balanced':'input.balanced.fake','Quality':'input.quality.fake','VoiceFocus':'input.voice-focus.fake'}[a.profile]
engine={'Original':'None','Light':'Speex','Balanced':'RNNoise','Quality':'DeepFilterNet','VoiceFocus':'DeepFilterNet'}[a.profile]
model={'Balanced':'rnnoise:embedded','Quality':'deepfilternet:fake','VoiceFocus':'deepfilternet:fake'}.get(a.profile,'')
active_model_path='' if a.profile == 'Balanced' else (a.authorized_model_path or '')
ranges={
 'Light':{'noise_reduction':(0,100),'natural_clear':(0,100)},
 'Balanced':{'noise_reduction':(20,90),'natural_clear':(10,90)},
 'Quality':{'noise_reduction':(25,90),'natural_clear':(25,100)},
 'VoiceFocus':{'noise_reduction':(70,100),'natural_clear':(40,100)},
}
ui_reduction=int(a.noise_reduction); ui_natural_clear=int(a.natural_clear)
def validated(dimension,value):
    if a.profile == 'Original': return 0
    minimum,maximum=ranges[a.profile][dimension]
    return minimum+((value*(maximum-minimum)+50)//100)
report={
 'processing_mode':'product-profile','requested_profile':a.profile,'active_profile':a.profile,
 'requested_recipe_id':recipe,'recipe_revision':1,'active_engine':engine,'active_model_id':model,
 'requested_ui_noise_reduction':ui_reduction,'requested_ui_natural_clear':ui_natural_clear,
 'validated_recipe_noise_reduction':validated('noise_reduction',ui_reduction),
 'validated_recipe_natural_clear':validated('natural_clear',ui_natural_clear),
 'active_model_path':active_model_path,'active_model_sha256':a.authorized_model_sha256 or '',
 'used_fallback':False,'fallback_reason':'None','fallback_count':0,'deadline_misses':0,
 'reported_latency_ms':latency*1000.0/48000.0,'reported_latency_samples':latency,
 'input_sample_count':count,'clean_reference_sample_count':clean_count,'output_sample_count':count+latency,
 'drain_sample_count':latency,'processing_padding_sample_count':0,'sample_count':count+latency,
 'sample_rate':48000,'non_finite_sample_count':0,'input_saturated_sample_count':0,'saturated_sample_count':0,
 'out_of_range_sample_count':0,'input_path':str(source),'clean_reference_path':a.clean_reference,
 'output_path':str(output),'report_path':a.report,
 'audio_ms':count*1000.0/48000.0,'processing_wall_ms':100.0,'rtf':100.0/(count*1000.0/48000.0),
 'callback_p99_ms':1.0,'worker_processing_p99_ms':2.0 if a.profile in ('Quality','VoiceFocus') else 0.0,
 'maximum_processing_ms':2.0 if a.profile in ('Quality','VoiceFocus') else 1.0,
}
Path(a.report).write_text(json.dumps(report,sort_keys=True),encoding='utf-8')
'''


def _fake_scorer_source() -> str:
	return r'''#!/usr/bin/env python3
import argparse, hashlib, json, os, wave
from pathlib import Path
from objective_quality_score import NORMALIZATION_ID, SCORER_ID, verify_metrics_runtime

p=argparse.ArgumentParser()
for name in ('case-id','profile','condition','dataset-split','signal-stage','clean-reference','noisy-original','candidate','original-latency-samples','candidate-latency-samples','metrics-runtime-root','metrics-manifest','language','wer-reference-kind','clean-asr-reference','output'):
    p.add_argument('--'+name)
a=p.parse_args()
assert a.signal_stage == 'sender-pre-opus' and a.wer_reference_kind == 'clean-asr-consistency'
assert 'MUMBLE_DISABLE_INPUT_ENHANCEMENT' not in os.environ and 'PYTHONPATH' not in os.environ
assert 'PYTHONPYCACHEPREFIX' not in os.environ
assert Path(__file__).resolve().parent.name == '_private-objective-scorer'
assert Path(a.metrics_runtime_root).resolve().name == '_private-metrics-runtime'
assert Path(a.metrics_manifest).resolve().parent == Path(a.metrics_runtime_root).resolve()
counter=Path(os.environ['OFFLINE_CAMPAIGN_SELFTEST_COUNTER'])
counter.write_text(str(int(counter.read_text())+1))
def rec(path):
    path=Path(path); data=path.read_bytes()
    with wave.open(str(path),'rb') as w: frames=w.getnframes(); channels=w.getnchannels(); rate=w.getframerate()
    return {'sha256':hashlib.sha256(data).hexdigest(),'size_bytes':len(data),'channels':channels,'frames':frames,'sample_rate_hz':rate}
reference=Path(a.clean_asr_reference)
if not reference.exists():
    reference.parent.mkdir(parents=True,exist_ok=True)
    reference.write_text(json.dumps({'kind':'clean-asr-consistency','private_text_do_not_publish':True,'normalized_transcript':'self test words'}),encoding='utf-8')
reference_bytes=reference.read_bytes()
artifact={'sha256':hashlib.sha256(reference_bytes).hexdigest(),'size_bytes':len(reference_bytes)}
cli=Path(__file__); cli_bytes=cli.read_bytes()
implementation=cli.with_name('objective_quality_score.py'); implementation_bytes=implementation.read_bytes()
runtime=dict(verify_metrics_runtime(Path(a.metrics_runtime_root),Path(a.metrics_manifest),verify_environment=False))
runtime.pop('runtime_root',None)
zero=a.profile == 'Original'
base={'dnsmos_bak':2.0,'dnsmos_ovrl':2.0,'dnsmos_sig':2.0,'estoi':0.8,'wer':{'errors':0,'reference_words':3,'rate':0.0,'hypothesis_sha256':'1'*64}}
candidate=dict(base) if zero else {'dnsmos_bak':2.2,'dnsmos_ovrl':2.1,'dnsmos_sig':2.05,'estoi':0.82,'wer':{'errors':0,'reference_words':3,'rate':0.0,'hypothesis_sha256':'2'*64}}
document={
 'schema_version':2,'scorer':SCORER_ID,'status':'passed','case_id':a.case_id,'profile':a.profile,
 'condition':a.condition,'dataset_split':a.dataset_split,
 'alignment':{'method':'caller-declared-fixed-latency','correlation_search_used':False,'signal_stage':'sender-pre-opus',
  'sample_rate_hz':48000,'reference_samples':rec(a.clean_reference)['frames'],
  'original_latency_samples':int(a.original_latency_samples),'candidate_latency_samples':int(a.candidate_latency_samples),
  'original_window_start_samples':int(a.original_latency_samples),'candidate_window_start_samples':int(a.candidate_latency_samples),
  'qualified_route_binding':None},
 'inputs':{'clean_reference':rec(a.clean_reference),'noisy_original':rec(a.noisy_original),'candidate':rec(a.candidate)},
 'wer_reference':{'kind':'clean-asr-consistency','label':'clean-ASR-consistency WER','language':a.language,
  'normalization':NORMALIZATION_ID,'text_sha256':hashlib.sha256(b'self test words').hexdigest(),'word_count':3,'artifact':artifact,'attestation':None},
 'metrics':{'original':base,'candidate':candidate},
 'candidate_minus_original':{'dnsmos_bak':0.0 if zero else 0.2,'dnsmos_ovrl':0.0 if zero else 0.1,
  'dnsmos_sig':0.0 if zero else 0.05,'estoi':0.0 if zero else 0.02,
  'wer_delta_percentage_points':0.0,'wer_delta_kind':'clean-asr-consistency'},
 'runtime':runtime,
 'scorer_files':{
  'cli':{'name':cli.name,'sha256':hashlib.sha256(cli_bytes).hexdigest(),'size_bytes':len(cli_bytes)},
  'implementation':{'name':implementation.name,'sha256':hashlib.sha256(implementation_bytes).hexdigest(),'size_bytes':len(implementation_bytes)}},
}
Path(a.output).write_text(json.dumps(document,sort_keys=True),encoding='utf-8')
'''


def _self_test_recipe(recipe_id: str, profile: str, engine: str, model_ids: Sequence[str], cpu: str, latency: int) -> dict[str, Any]:
	return {
		"id": recipe_id, "revision": 1, "profile": profile, "engine": engine, "modelIds": list(model_ids),
		"noiseReductionRange": [0, 100], "naturalCrispRange": [0, 100], "latencyBudgetMs": latency,
		"minimumCpuClass": cpu, "executionSemanticsVersion": SELF_TEST_EXECUTION_SEMANTICS_VERSION,
		"mixCurveVersion": SELF_TEST_MIX_CURVE_VERSION,
		"adaptationPolicyVersion": 1,
	}


def run_self_test() -> None:
	with tempfile.TemporaryDirectory(prefix="mumble-offline-quality-campaign-") as directory:
		root = Path(directory)
		lock_path = SCRIPT_DIR / "corpus-lock.json"
		lock = LOCK.load_validated_manifest(lock_path)
		seed = "mumble-offline-campaign-self-test"
		inventory = PLAN._self_test_inventory(lock, seed)
		plan_path = root / "plan.json"
		inventory_path = root / "inventory.json"
		local_lock_path = root / "corpus-lock.json"
		transformation_manifest_path = root / "transformation-manifest.json"
		local_lock_path.write_bytes(lock_path.read_bytes())
		transformation_manifest = {
			"schema_version": 1,
			"generator": "mumble-corpus-builder",
			"generator_version": "4",
			"corpus_lock_sha256": LOCK.canonical_manifest_sha256(lock),
			"corpus_state_sha256": inventory["provenance"]["generated_from_state_sha256"],
			"split_seed": seed,
			"split_algorithm": "sha256-v1 by kind/group: tuning=0..59, validation=60..79, holdout=80..99",
			"split_seed_selection_basis": "self-test identifier-only split fixture",
			"qualification_suite": "master_quality",
			"nightly_selection_sha256": None,
			"nightly_materialized_splits": None,
			"nightly_sealed_splits": None,
			"demand_preparation": {},
			"ffmpeg": {},
			"fleurs_swedish_selection": {},
			"mcgill_archive_observation": {},
			"sources": [],
			"transforms": [],
		}
		_write_json_atomic(transformation_manifest_path, transformation_manifest)
		inventory["provenance"]["transformation_manifest_sha256"] = _sha256(transformation_manifest_path)
		# Generator v4 is the current hash-bound corpus builder. Unknown older or
		# future versions remain rejected even if an attacker also rewrites the
		# inventory's transformation-manifest hash.
		_validate_transformation_manifest(transformation_manifest_path, inventory, lock, "master_quality")
		for unsupported_version in ("1", "5"):
			unsupported_path = root / f"unsupported-transformation-manifest-v{unsupported_version}.json"
			unsupported = dict(transformation_manifest)
			unsupported["generator_version"] = unsupported_version
			_write_json_atomic(unsupported_path, unsupported)
			unsupported_inventory = json.loads(json.dumps(inventory))
			unsupported_inventory["provenance"]["transformation_manifest_sha256"] = _sha256(unsupported_path)
			try:
				_validate_transformation_manifest(unsupported_path, unsupported_inventory, lock, "master_quality")
			except CampaignError as error:
				_expect(
					"unsupported generator schema" in str(error),
					"self-test corpus generator version",
					f"unexpected error: {error}",
				)
			else:
				raise AssertionError(f"campaign accepted unsupported corpus generator v{unsupported_version}")

		def expect_transformation_rejection(
			label: str, candidate_manifest: Mapping[str, Any], candidate_inventory: Mapping[str, Any],
			expected_corpus_suite: str, expected_error: str,
		) -> None:
			candidate_path = root / f"rejected-{label}.json"
			_write_json_atomic(candidate_path, candidate_manifest)
			bound_inventory = json.loads(json.dumps(candidate_inventory))
			bound_inventory["provenance"]["transformation_manifest_sha256"] = _sha256(candidate_path)
			try:
				_validate_transformation_manifest(candidate_path, bound_inventory, lock, expected_corpus_suite)
			except CampaignError as error:
				_expect(expected_error in str(error), f"self-test {label}", f"unexpected error: {error}")
			else:
				raise AssertionError(f"campaign accepted invalid corpus transformation metadata: {label}")

		wrong_suite = dict(transformation_manifest)
		wrong_suite["qualification_suite"] = "nightly"
		expect_transformation_rejection(
			"suite-mismatch", wrong_suite, inventory, "master_quality",
			"does not match the suite-compatible corpus family",
		)
		missing_suite = dict(transformation_manifest)
		missing_suite.pop("qualification_suite")
		expect_transformation_rejection(
			"schema-missing-suite", missing_suite, inventory, "master_quality", "missing keys: qualification_suite",
		)
		unknown_seal_field = dict(transformation_manifest)
		unknown_seal_field["nightly_holdout_materialized"] = False
		expect_transformation_rejection(
			"schema-unknown-seal-field", unknown_seal_field, inventory, "master_quality",
			"unknown keys: nightly_holdout_materialized",
		)
		draft_master_inventory = json.loads(json.dumps(inventory))
		draft_master_inventory["eligibility"] = "draft"
		expect_transformation_rejection(
			"master-draft-inventory", transformation_manifest, draft_master_inventory, "master_quality",
			"non-nightly qualification requires a release inventory",
		)
		master_with_seal = dict(transformation_manifest)
		master_with_seal["nightly_sealed_splits"] = ["holdout"]
		expect_transformation_rejection(
			"master-with-nightly-seal", master_with_seal, inventory, "master_quality",
			"must be null outside the nightly suite",
		)

		frozen_nightly_selection = _mapping(
			_load_json(SCRIPT_DIR / "nightly-corpus-selection-v1.json", "self-test frozen nightly selection"),
			"self-test frozen nightly selection",
		)
		nightly_seed = str(frozen_nightly_selection["split_seed"])
		selection_sha256 = INVENTORY.file_sha256(SCRIPT_DIR / "nightly-corpus-selection-v1.json")
		nightly_inventory = json.loads(json.dumps(inventory))
		holdout_items = [
			item for item in nightly_inventory["items"]
			if item["kind"] == "speech"
			and INVENTORY.assigned_split(nightly_seed, str(item["kind"]), str(item["group_id"])) == "holdout"
		]
		_expect(bool(holdout_items), "self-test nightly seal", "fixture requires holdout-assigned material")
		nightly_inventory["items"] = [
			item for item in nightly_inventory["items"]
			if INVENTORY.assigned_split(nightly_seed, str(item["kind"]), str(item["group_id"])) in ("tuning", "validation")
		]
		nightly_inventory["eligibility"] = "nightly-partial"
		nightly_inventory["sealed_splits"] = ["holdout"]
		nightly_inventory["selection_sha256"] = selection_sha256
		nightly_manifest = dict(transformation_manifest)
		nightly_manifest.update({
			"qualification_suite": "nightly",
			"split_seed": nightly_seed,
			"nightly_selection_sha256": selection_sha256,
			"nightly_materialized_splits": ["tuning", "validation"],
			"nightly_sealed_splits": ["holdout"],
		})
		nightly_manifest_path = root / "nightly-transformation-manifest.json"
		_write_json_atomic(nightly_manifest_path, nightly_manifest)
		nightly_inventory["provenance"]["transformation_manifest_sha256"] = _sha256(nightly_manifest_path)
		_validate_transformation_manifest(nightly_manifest_path, nightly_inventory, lock, "nightly")

		nightly_bad_seal = dict(nightly_manifest)
		nightly_bad_seal["nightly_sealed_splits"] = []
		expect_transformation_rejection(
			"nightly-missing-seal", nightly_bad_seal, nightly_inventory, "nightly", "must seal holdout"
		)
		nightly_inventory_bad_seal = json.loads(json.dumps(nightly_inventory))
		nightly_inventory_bad_seal["sealed_splits"] = []
		expect_transformation_rejection(
			"nightly-inventory-missing-seal", nightly_manifest, nightly_inventory_bad_seal, "nightly",
			"does not match the transformation manifest",
		)
		nightly_inventory_bad_selection = json.loads(json.dumps(nightly_inventory))
		nightly_inventory_bad_selection["selection_sha256"] = "0" * 64
		expect_transformation_rejection(
			"nightly-inventory-selection-drift", nightly_manifest, nightly_inventory_bad_selection, "nightly",
			"does not match the transformation manifest",
		)
		nightly_bad_materialization = dict(nightly_manifest)
		nightly_bad_materialization["nightly_materialized_splits"] = ["tuning", "validation", "holdout"]
		expect_transformation_rejection(
			"nightly-materializes-holdout", nightly_bad_materialization, nightly_inventory, "nightly",
			"must contain exactly tuning and validation",
		)
		nightly_inventory_with_holdout = json.loads(json.dumps(nightly_inventory))
		forged_holdout_item = json.loads(json.dumps(holdout_items[0]))
		forged_holdout_item["id"] = "speech-forged-nightly-holdout"
		forged_holdout_item["source_id"] = "openslr12-librispeech-test-clean"
		nightly_inventory_with_holdout["items"].append(forged_holdout_item)
		nightly_inventory_with_holdout["items"] = sorted(
			nightly_inventory_with_holdout["items"], key=lambda item: str(item["id"])
		)
		expect_transformation_rejection(
			"nightly-inventory-contains-holdout", nightly_manifest, nightly_inventory_with_holdout, "nightly",
			"contains material assigned to sealed holdout",
		)
		plan = PLAN.generate_plan(lock, inventory, "release", "validation", seed, 30, 1000)
		_write_json_atomic(plan_path, plan)
		_write_json_atomic(inventory_path, inventory)

		render_root = root / "rendered"
		entries = []
		paired_fixture_indices: dict[str, int] = {}
		for index, case in enumerate(plan["cases"]):
			case_root = render_root / str(case["case_id"])
			clean_path = case_root / "clean-reference.wav"
			input_path = case_root / "client1-input.wav"
			comparison_scene_id = case.get("comparison_scene_id")
			fixture_index = index
			if comparison_scene_id is not None:
				fixture_index = paired_fixture_indices.setdefault(str(comparison_scene_id), index)
			_write_pcm16_fixture(clean_path, 180.0 + fixture_index, False)
			_write_pcm16_fixture(input_path, 180.0 + fixture_index, case["noise"] is not None)
			entries.append({
				"case_id": case["case_id"], "profile": case["profile"],
				"startup_preroll_ms": case["startup"]["preroll_ms"],
				"input": {"path": input_path.relative_to(render_root).as_posix(), "sha256": _sha256(input_path)},
				"clean_reference": {"path": clean_path.relative_to(render_root).as_posix(), "sha256": _sha256(clean_path)},
				"speech_source_sha256": case["speech"]["sha256"],
				"noise_source_sha256": case["noise"]["sha256"] if case["noise"] is not None else None,
				"rir_source_sha256": case["mix"]["rir"]["sha256"],
				"microphone_response_source_sha256": case["mix"]["microphone_response"]["sha256"],
				"rendered_samples": SAMPLE_RATE_HZ,
			})
		render_manifest = {
			"schema_version": 2, "renderer": "mumble-audio-mixture-renderer-v2",
			"plan_sha256": PLAN.canonical_sha256(plan), "corpus_lock_sha256": plan["corpus_lock_sha256"],
			"corpus_inventory_sha256": plan["corpus_inventory_sha256"], "sample_rate_hz": SAMPLE_RATE_HZ,
			"channels": 1, "private_audio_do_not_upload": True, "cases": entries,
		}
		render_manifest_path = render_root / "render-manifest.json"
		_write_json_atomic(render_manifest_path, render_manifest)

		runtime = root / "runtime"
		runtime.mkdir()
		(runtime / "mumble.exe").write_bytes(b"fake packaged client")
		(runtime / "rnnoise.dll").write_bytes(b"fake embedded rnnoise module")
		(runtime / "deepfilter.bin").write_bytes(b"fake deepfilter model")
		models = [
			{
				"id": "rnnoise:embedded", "version": "1", "backend": "RNNoise", "path": "rnnoise.dll",
				"sha256": _sha256(runtime / "rnnoise.dll"), "size": (runtime / "rnnoise.dll").stat().st_size,
				"licenseSpdx": "BSD-3-Clause", "sampleRateHz": SAMPLE_RATE_HZ, "algorithmicLatencyMs": 30.0,
				"recipeCompatibility": ["input.balanced.fake"],
			},
			{
				"id": "deepfilternet:fake", "version": "1", "backend": "DeepFilterNet", "path": "deepfilter.bin",
				"sha256": _sha256(runtime / "deepfilter.bin"), "size": (runtime / "deepfilter.bin").stat().st_size,
				"licenseSpdx": "MIT", "sampleRateHz": SAMPLE_RATE_HZ, "algorithmicLatencyMs": 10.0,
				"recipeCompatibility": ["input.quality.fake", "input.voice-focus.fake"],
			},
		]
		models_manifest = {"schemaVersion": 1, "catalogRevision": "self-test-v2", "generatedFromAssets": True, "models": models}
		model_manifest_path = runtime / "input-models.json"
		_write_json_atomic(model_manifest_path, models_manifest)
		recipes = [
			_self_test_recipe("input.original", "Original", "None", [], "Low", 0),
			_self_test_recipe("input.light.speex", "Light", "Speex", [], "Low", 10),
			_self_test_recipe("input.balanced.fake", "Balanced", "RNNoise", ["rnnoise:embedded"], "Standard", 30),
			_self_test_recipe("input.quality.fake", "Quality", "DeepFilterNet", ["deepfilternet:fake"], "High", 50),
			_self_test_recipe("input.voice-focus.fake", "VoiceFocus", "DeepFilterNet", ["deepfilternet:fake"], "High", 50),
		]
		recipe_manifest = {
			"schemaVersion": 2, "catalogRevision": "self-test-v2", "modelManifestSha256": _sha256(model_manifest_path),
			"recipes": recipes,
		}
		recipe_manifest_path = runtime / "input-recipes.json"
		_write_json_atomic(recipe_manifest_path, recipe_manifest)

		benchmark = runtime / "fake-benchmark.py"
		scorer = root / "fake-scorer.py"
		benchmark.write_text(_fake_benchmark_source(), encoding="utf-8")
		scorer.write_text(_fake_scorer_source(), encoding="utf-8")
		(scorer.parent / "objective_quality_score.py").write_bytes((SCRIPT_DIR / "objective_quality_score.py").read_bytes())
		metrics_runtime = root / "metrics-runtime"
		metrics_runtime.mkdir()
		metric_asset = metrics_runtime / "asset.bin"
		metric_model = metrics_runtime / "metric-model.bin"
		metric_asset.write_bytes(b"pinned metric asset")
		metric_model.write_bytes(b"pinned metric model")
		legacy_record = {
			"relative_path": "external/scorer/score-input-enhancement-objective.py",
			"sha256": "a" * 64,
			"size_bytes": 1,
		}
		assets = [
			{"relative_path": "asset.bin", **_file_record(metric_asset)},
			legacy_record,
		]
		distributions: list[dict[str, Any]] = []
		whisper_tree_sha256 = "b" * 64
		metrics_inventory = {
			"schema_version": 1,
			"assets": assets,
			"assets_tree_sha256": _canonical_sha256(assets),
			"distributions": distributions,
			"distributions_tree_sha256": _canonical_sha256(distributions),
			"python": {},
			"whisper_snapshot": {"file_count": 0, "size_bytes": 0, "tree_sha256": whisper_tree_sha256},
		}
		metrics_inventory_path = metrics_runtime / "metrics-runtime-files.json"
		_write_json_atomic(metrics_inventory_path, metrics_inventory)
		metrics_lock = {
			"schema_version": 1,
			"id": "mumble-audio-quality-metrics-python",
			"version": "self-test-v1",
			"inventory": {
				"relative_path": metrics_inventory_path.name,
				**_file_record(metrics_inventory_path),
				"canonical_sha256": _canonical_sha256(metrics_inventory),
				"assets_tree_sha256": metrics_inventory["assets_tree_sha256"],
				"distributions_tree_sha256": metrics_inventory["distributions_tree_sha256"],
				"whisper_tree_sha256": whisper_tree_sha256,
			},
			"sources": {
				"dnsmos": {"repository": "self-test/dnsmos", "revision": "c" * 40},
				"wer": {"repository": "self-test/wer", "revision": "d" * 40},
			},
		}
		metrics_lock_path = metrics_runtime / "metrics-runtime.lock.json"
		_write_json_atomic(metrics_lock_path, metrics_lock)
		metrics_manifest = metrics_runtime / "metrics-manifest.json"
		metric_models = [
			{"id": model_id, "relative_path": metric_model.name, **_file_record(metric_model)}
			for model_id in ("dnsmos", "estoi", "wer-en", "wer-sv")
		]
		_write_json_atomic(metrics_manifest, {
			"schema_version": 1,
			"runtime": {
				"id": metrics_lock["id"], "version": metrics_lock["version"],
				"relative_path": metrics_lock_path.name, **_file_record(metrics_lock_path),
			},
			"models": metric_models,
		})
		self_test_metrics_python = Path(sys.executable).resolve()
		self_test_python_binding = {
			"executable": {
				"relative_path": "venv/python.exe",
				**_file_record(self_test_metrics_python),
			},
			"implementation": sys.implementation.name,
			"venv_root": str(Path(sys.prefix).resolve()),
			"version": sys.version.split()[0],
		}
		preflight = _verify_metrics_python_preflight(
			self_test_metrics_python, metrics_runtime, self_test_python_binding, 30
		)
		_expect(
			preflight["status"] == "passed"
			and Path(str(preflight["executable"]["path"])).resolve() == self_test_metrics_python,
			"self-test metrics Python preflight",
			"the exact attested interpreter was not accepted",
		)
		wrong_prefix_binding = {
			**self_test_python_binding,
			"venv_root": str(root / "definitely-not-the-running-venv"),
		}
		try:
			_verify_metrics_python_preflight(
				self_test_metrics_python, metrics_runtime, wrong_prefix_binding, 30
			)
		except CampaignError as error:
			_expect(
				"not running in the venv attested" in str(error),
				"self-test metrics Python preflight",
				f"unexpected wrong-venv failure: {error}",
			)
		else:
			raise AssertionError("metrics Python preflight accepted an interpreter from the wrong venv")
		wrong_executable_binding = {
			**self_test_python_binding,
			"executable": {
				**self_test_python_binding["executable"],
				"sha256": "f" * 64,
			},
		}
		try:
			_verify_metrics_python_preflight(
				self_test_metrics_python, metrics_runtime, wrong_executable_binding, 30
			)
		except CampaignError as error:
			_expect(
				"executable hash/size does not match" in str(error),
				"self-test metrics Python preflight",
				f"unexpected executable-pin failure: {error}",
			)
		else:
			raise AssertionError("metrics Python preflight accepted an unpinned executable")
		counter = root / "counter.txt"
		test_environment = {
			"OFFLINE_CAMPAIGN_SELFTEST_COUNTER": str(counter),
			"MUMBLE_DISABLE_INPUT_ENHANCEMENT": "must-be-sanitized",
			"PYTHONPATH": "must-be-sanitized",
			"PYTHONPYCACHEPREFIX": "must-be-sanitized",
		}
		old_environment = {name: os.environ.get(name) for name in test_environment}
		os.environ.update(test_environment)
		output = root / "output"
		args = argparse.Namespace(
			plan=plan_path, case_set=plan_path, render_manifest=render_manifest_path, render_root=render_root, inventory=inventory_path,
			corpus_lock=local_lock_path, transformation_manifest=transformation_manifest_path,
			benchmark=benchmark, runtime_root=runtime, model_manifest=None,
			recipe_manifest=None, metrics_python=Path(sys.executable), metrics_runtime_root=metrics_runtime,
			metrics_manifest=metrics_manifest, scorer=scorer, output_root=output, timeout_seconds=30,
			_allow_fake_tools=True,
		)
		try:
			first = run_campaign(args)
			_expect(first["status"] == "passed" and first["summary"]["executed_this_invocation"] == 30, "self-test success", "campaign did not execute all cases")
			_expect(int(counter.read_text()) == 90, "self-test invocation count", "expected two benchmarks and one scorer per case")
			for private_name in ("_private-execution-runtime", "_private-metrics-runtime", "_private-objective-scorer"):
				_expect(not (output / private_name).exists(), "self-test private cleanup", f"left stale {private_name}")
			second = run_campaign(args)
			_expect(second["status"] == "passed" and second["summary"]["resumed_this_invocation"] == 30, "self-test resume", "campaign did not resume all cases")
			_expect(int(counter.read_text()) == 90, "self-test resume", "resume re-executed tools")

			first_result = second["case_results"][0]
			case_manifest_path = _below(output, first_result["manifest"]["relative_path"], "self-test case manifest")
			case_manifest = _load_json(case_manifest_path, "self-test case manifest")
			candidate_path = _below(output, case_manifest["artifacts"]["candidate_wav"]["relative_path"], "self-test candidate")
			tampered = bytearray(candidate_path.read_bytes())
			tampered[-1] ^= 1
			candidate_path.write_bytes(tampered)
			third = run_campaign(args)
			_expect(third["status"] == "passed" and third["summary"]["executed_this_invocation"] == 1 and third["summary"]["resumed_this_invocation"] == 29, "self-test artifact tamper", "tampered output was not selectively rerun")
			_expect(int(counter.read_text()) == 93, "self-test artifact tamper", "unexpected rerun count")

			empty_artifact_result = third["case_results"][1]
			empty_artifact_manifest_path = _below(
				output, empty_artifact_result["manifest"]["relative_path"], "self-test empty-artifact manifest"
			)
			empty_artifact_manifest = _load_json(empty_artifact_manifest_path, "self-test empty-artifact manifest")
			empty_artifact_manifest["artifacts"] = {}
			_write_json_atomic(empty_artifact_manifest_path, empty_artifact_manifest)
			fourth = run_campaign(args)
			_expect(
				fourth["status"] == "passed" and fourth["summary"]["executed_this_invocation"] == 1
				and fourth["summary"]["resumed_this_invocation"] == 29,
				"self-test empty artifact map",
				"resume accepted an incomplete artifact map",
			)
			_expect(int(counter.read_text()) == 96, "self-test empty artifact map", "unexpected rerun count")

			changed_binding_result = fourth["case_results"][2]
			changed_binding_manifest_path = _below(
				output, changed_binding_result["manifest"]["relative_path"], "self-test changed-binding manifest"
			)
			changed_binding_manifest = _load_json(changed_binding_manifest_path, "self-test changed-binding manifest")
			changed_binding_manifest["case_binding"]["profile"] = "Light"
			_write_json_atomic(changed_binding_manifest_path, changed_binding_manifest)
			fifth = run_campaign(args)
			_expect(
				fifth["status"] == "passed" and fifth["summary"]["executed_this_invocation"] == 1
				and fifth["summary"]["resumed_this_invocation"] == 29,
				"self-test changed case binding",
				"resume accepted changed case-binding bytes",
			)
			_expect(int(counter.read_text()) == 99, "self-test changed case binding", "unexpected rerun count")

			self_context = _build_run_context(args)
			result_by_case = {str(result["case_id"]): result for result in fifth["case_results"]}
			render_entry_by_id = {str(entry["case_id"]): entry for entry in entries}

			def self_test_case(profile: str) -> tuple[Mapping[str, Any], Mapping[str, Any], Mapping[str, Any], Path, Mapping[str, Any]]:
				case = next(value for value in plan["cases"] if value["profile"] == profile)
				case_id = str(case["case_id"])
				entry = render_entry_by_id[case_id]
				recipe = _public_recipe(profile, self_context["recipes"])
				binding = _case_binding(case, entry, recipe, self_context["models"], str(plan["split"]))
				manifest_path = _below(
					output, result_by_case[case_id]["manifest"]["relative_path"], f"self-test {profile} manifest"
				)
				manifest = _load_json(manifest_path, f"self-test {profile} manifest")
				return case, entry, recipe, manifest_path, {"binding": binding, "manifest": manifest}

			light_case, light_entry, light_recipe, _, light_state = self_test_case("Light")
			light_manifest = light_state["manifest"]
			light_report_path = _below(output, light_manifest["artifacts"]["candidate_report"]["relative_path"], "self-test Light report")
			light_output_path = _below(output, light_manifest["artifacts"]["candidate_wav"]["relative_path"], "self-test Light output")
			light_input_path = _below(render_root, light_entry["input"]["path"], "self-test Light input")
			light_clean_path = _below(render_root, light_entry["clean_reference"]["path"], "self-test Light clean")
			light_report_bytes = light_report_path.read_bytes()
			forged_latency_report = _load_json(light_report_path, "self-test forged latency report")
			forged_latency_report["reported_latency_samples"] = 960
			forged_latency_report["reported_latency_ms"] = 20.0
			forged_latency_report["drain_sample_count"] = 960
			forged_latency_report["input_sample_count"] -= FRAME_SAMPLES
			_write_json_atomic(light_report_path, forged_latency_report)
			try:
				_validate_benchmark_report(
					light_report_path, light_output_path, light_input_path, light_clean_path,
					"Light", light_recipe, None, None,
					light_state["binding"]["controls"], light_state["binding"]["validated_recipe_controls"],
				)
			except CampaignError as error:
				_expect("signed recipe execution contract" in str(error), "self-test forged latency", f"unexpected error: {error}")
			else:
				raise AssertionError("benchmark validation accepted caller-forged latency")
			light_report_path.write_bytes(light_report_bytes)

			quality_case, quality_entry, quality_recipe, _, quality_state = self_test_case("Quality")
			quality_manifest = quality_state["manifest"]
			quality_report_path = _below(output, quality_manifest["artifacts"]["candidate_report"]["relative_path"], "self-test Quality report")
			quality_output_path = _below(output, quality_manifest["artifacts"]["candidate_wav"]["relative_path"], "self-test Quality output")
			quality_input_path = _below(render_root, quality_entry["input"]["path"], "self-test Quality input")
			quality_clean_path = _below(render_root, quality_entry["clean_reference"]["path"], "self-test Quality clean")
			quality_model = self_context["models"][str(quality_recipe["modelIds"][0])]
			_expect(
				quality_state["binding"]["recipe"]["expected_latency_samples"] == 1440,
				"self-test Quality semantics-v5 latency",
				"10 ms model latency plus two callback frames must be 1440 samples",
			)
			quality_report_bytes = quality_report_path.read_bytes()
			quality_report = _load_json(quality_report_path, "self-test Quality report")
			quality_report["validated_recipe_noise_reduction"] += 1
			_write_json_atomic(quality_report_path, quality_report)
			try:
				_validate_benchmark_report(
					quality_report_path, quality_output_path, quality_input_path, quality_clean_path,
					"Quality", quality_recipe, quality_model, runtime / str(quality_model["path"]),
					quality_state["binding"]["controls"], quality_state["binding"]["validated_recipe_controls"],
				)
			except CampaignError as error:
				_expect("control mapping mismatch" in str(error), "self-test control mapping", f"unexpected error: {error}")
			else:
				raise AssertionError("benchmark validation accepted Python/C++ control-mapping drift")
			quality_report_path.write_bytes(quality_report_bytes)

			quality_report = _load_json(quality_report_path, "self-test Quality report")
			quality_report["active_model_path"] = ""
			_write_json_atomic(quality_report_path, quality_report)
			try:
				_validate_benchmark_report(
					quality_report_path, quality_output_path, quality_input_path, quality_clean_path,
					"Quality", quality_recipe, quality_model, runtime / str(quality_model["path"]),
					quality_state["binding"]["controls"], quality_state["binding"]["validated_recipe_controls"],
				)
			except CampaignError as error:
				_expect("may only be empty" in str(error), "self-test external empty model path", f"unexpected error: {error}")
			else:
				raise AssertionError("benchmark validation accepted an empty external-model path")
			quality_report_path.write_bytes(quality_report_bytes)

			forged_quality_latency_report = _load_json(quality_report_path, "self-test forged Quality latency report")
			forged_quality_latency_report["reported_latency_samples"] = 960
			forged_quality_latency_report["reported_latency_ms"] = 20.0
			forged_quality_latency_report["drain_sample_count"] = 960
			_write_json_atomic(quality_report_path, forged_quality_latency_report)
			try:
				_validate_benchmark_report(
					quality_report_path, quality_output_path, quality_input_path, quality_clean_path,
					"Quality", quality_recipe, quality_model, runtime / str(quality_model["path"]),
					quality_state["binding"]["controls"], quality_state["binding"]["validated_recipe_controls"],
				)
			except CampaignError as error:
				_expect(
					"signed recipe execution contract" in str(error),
					"self-test forged Quality latency",
					f"unexpected error: {error}",
				)
			else:
				raise AssertionError("benchmark validation accepted a 960-sample semantics-v5 Quality latency")
			quality_report_path.write_bytes(quality_report_bytes)

			objective_path = _below(output, quality_manifest["artifacts"]["objective_score"]["relative_path"], "self-test objective score")
			original_path = _below(output, quality_manifest["artifacts"]["original_wav"]["relative_path"], "self-test objective Original")
			candidate_path = quality_output_path
			private_reference = _below(
				output, quality_manifest["artifacts"]["private_clean_asr_reference"]["relative_path"],
				"self-test private reference",
			)
			objective_bytes = objective_path.read_bytes()
			objective_document = _load_json(objective_path, "self-test objective score")
			objective_document["runtime"]["manifest"]["sha256"] = "e" * 64
			_write_json_atomic(objective_path, objective_document)
			try:
				_validate_objective(
					objective_path, self_context, quality_case, quality_state["binding"], quality_clean_path,
					original_path, candidate_path, 0, quality_state["binding"]["recipe"]["expected_latency_samples"],
					private_reference,
				)
			except CampaignError as error:
				_expect("independently verified pinned metrics runtime" in str(error), "self-test runtime attestation", f"unexpected error: {error}")
			else:
				raise AssertionError("objective validation accepted a forged runtime hash")
			objective_path.write_bytes(objective_bytes)

			objective_document = _load_json(objective_path, "self-test objective scorer binding")
			objective_document["scorer_files"]["implementation"]["sha256"] = "f" * 64
			_write_json_atomic(objective_path, objective_document)
			try:
				_validate_objective(
					objective_path, self_context, quality_case, quality_state["binding"], quality_clean_path,
					original_path, candidate_path, 0, quality_state["binding"]["recipe"]["expected_latency_samples"],
					private_reference,
				)
			except CampaignError as error:
				_expect("scorer implementation or CLI drift" in str(error), "self-test scorer attestation", f"unexpected error: {error}")
			else:
				raise AssertionError("objective validation accepted a forged scorer implementation hash")
			objective_path.write_bytes(objective_bytes)

			balanced_case, _, _, _, balanced_state = self_test_case("Balanced")
			balanced_report_path = _below(
				output, balanced_state["manifest"]["artifacts"]["candidate_report"]["relative_path"],
				"self-test embedded RNNoise report",
			)
			balanced_report = _load_json(balanced_report_path, "self-test embedded RNNoise report")
			_expect(
				balanced_report["active_model_id"] == "rnnoise:embedded"
				and balanced_report["active_model_path"] == "",
				"self-test embedded RNNoise report",
				"fixture did not exercise the exact empty-path embedded-model exception",
			)

			paired_scene_id = next(
				str(case["comparison_scene_id"])
				for case in plan["cases"]
				if case.get("comparison_scene_id") is not None
			)
			paired_case_ids = [
				str(case["case_id"])
				for case in plan["cases"]
				if str(case.get("comparison_scene_id")) == paired_scene_id
			]
			_expect(len(paired_case_ids) == 2, "self-test paired scene", "fixture did not create one exact profile pair")
			paired_case_id = paired_case_ids[1]
			paired_case = next(case for case in plan["cases"] if str(case["case_id"]) == paired_case_id)
			paired_entry = render_entry_by_id[paired_case_id]
			paired_input_path = _below(render_root, paired_entry["input"]["path"], "self-test paired input")
			paired_input_bytes = paired_input_path.read_bytes()
			render_manifest_bytes = render_manifest_path.read_bytes()
			try:
				_write_pcm16_fixture(paired_input_path, 999.0, paired_case["noise"] is not None)
				mismatched_render_manifest = json.loads(json.dumps(render_manifest))
				mismatched_entry = next(
					entry for entry in mismatched_render_manifest["cases"] if str(entry["case_id"]) == paired_case_id
				)
				mismatched_entry["input"]["sha256"] = _sha256(paired_input_path)
				_write_json_atomic(render_manifest_path, mismatched_render_manifest)
				try:
					_validate_render_manifest(plan, render_manifest_path, render_root)
				except CampaignError as error:
					_expect("paired scene" in str(error), "self-test paired sample identity", f"unexpected error: {error}")
				else:
					raise AssertionError("render validation accepted different samples for a paired Quality/Voice Focus scene")
			finally:
				paired_input_path.write_bytes(paired_input_bytes)
				render_manifest_path.write_bytes(render_manifest_bytes)

			render_input = _below(render_root, entries[0]["input"]["path"], "self-test rendered input")
			original_input_bytes = render_input.read_bytes()
			bad_input = bytearray(original_input_bytes)
			bad_input[-1] ^= 1
			render_input.write_bytes(bad_input)
			try:
				run_campaign(args)
			except CampaignError as error:
				_expect("hash mismatch" in str(error), "self-test input tamper", f"unexpected error: {error}")
			else:
				raise AssertionError("campaign accepted a tampered rendered input")
			render_input.write_bytes(original_input_bytes)

			holdout = json.loads(json.dumps(plan))
			holdout["split"] = "holdout"
			holdout_path = root / "holdout-plan.json"
			_write_json_atomic(holdout_path, holdout)
			holdout_args = argparse.Namespace(**vars(args))
			holdout_args.plan = holdout_path
			holdout_args.output_root = root / "holdout-output"
			try:
				run_campaign(holdout_args)
			except CampaignError as error:
				_expect("forbids holdout" in str(error), "self-test holdout", f"unexpected error: {error}")
			else:
				raise AssertionError("campaign accepted a holdout plan")

			relabelled = json.loads(json.dumps(plan))
			relabelled["split"] = "tuning"
			relabelled_path = root / "relabelled-plan.json"
			_write_json_atomic(relabelled_path, relabelled)
			relabelled_args = argparse.Namespace(**vars(args))
			relabelled_args.plan = relabelled_path
			relabelled_args.output_root = root / "relabelled-output"
			try:
				run_campaign(relabelled_args)
			except CampaignError as error:
				_expect("different protected corpus split" in str(error), "self-test protected split", f"unexpected error: {error}")
			else:
				raise AssertionError("campaign accepted source groups relabelled to a different protected split")

			unsafe_case_ids = json.loads(json.dumps(plan))
			for index, case in enumerate(unsafe_case_ids["cases"]):
				case["case_id"] = f"{index:02d}/escape"
			unsafe_case_ids_path = root / "unsafe-case-ids-plan.json"
			_write_json_atomic(unsafe_case_ids_path, unsafe_case_ids)
			unsafe_case_ids_args = argparse.Namespace(**vars(args))
			unsafe_case_ids_args.plan = unsafe_case_ids_path
			unsafe_case_ids_args.output_root = root / "unsafe-case-ids-output"
			try:
				run_campaign(unsafe_case_ids_args)
			except CampaignError as error:
				_expect("path-safe stable identifier" in str(error), "self-test path traversal", f"unexpected error: {error}")
			else:
				raise AssertionError("campaign accepted path traversal in case IDs")

			transformation_manifest_bytes = transformation_manifest_path.read_bytes()
			try:
				tampered_transformation_manifest = json.loads(transformation_manifest_bytes)
				tampered_transformation_manifest["split_seed"] = "tampered-self-test-split-seed"
				_write_json_atomic(transformation_manifest_path, tampered_transformation_manifest)
				transformation_args = argparse.Namespace(**vars(args))
				transformation_args.output_root = root / "transformation-tamper-output"
				try:
					run_campaign(transformation_args)
				except CampaignError as error:
					_expect(
						"file hash does not match inventory provenance" in str(error),
						"self-test transformation manifest binding",
						f"unexpected error: {error}",
					)
				else:
					raise AssertionError("campaign accepted a transformation manifest not bound by the inventory")
			finally:
				transformation_manifest_path.write_bytes(transformation_manifest_bytes)

			assembly_campaign = root / "assembly-campaign"
			shutil.copytree(output, assembly_campaign)
			assembly_campaign_document_path = assembly_campaign / "campaign-manifest.json"
			assembly_campaign_document = _mapping(
				_load_json(assembly_campaign_document_path, "self-test assembly campaign"),
				"self-test assembly campaign",
			)
			assembly_fragment_record = assembly_campaign_document["measurement_fragments"]["manifest"]
			assembly_fragment_path = _below(
				assembly_campaign, assembly_fragment_record["relative_path"], "self-test assembly fragment",
			)
			assembly_fragment = dict(_load_canonical_json(assembly_fragment_path, "self-test assembly fragment"))
			# The main campaign fixture intentionally uses the fixed release-pair layout.
			# Relabel only this private assembler fixture to exercise the pr_smoke-only
			# finalization contract; production assembly reads the real plan suite.
			assembly_fragment["source_plan_suite"] = "pr_smoke"
			_write_canonical_json_atomic(assembly_fragment_path, assembly_fragment)
			assembly_campaign_document["measurement_fragments"]["manifest"] = _relative_record(
				assembly_fragment_path, assembly_campaign,
			)
			_write_json_atomic(assembly_campaign_document_path, assembly_campaign_document)

			artifact_root = root / "qualification-artifacts"
			artifact_root.mkdir()
			build = {
				**{
					key: value
					for key, value in assembly_fragment["build_binding"].items()
					if key in QUALIFICATION_BUILD_KEYS
				},
				"git_sha": "1" * 40,
				"legacy_binary_sha256": "2" * 64,
				"server_binary_sha256": "3" * 64,
				"hardware_fingerprint_sha256": "4" * 64,
				"release_fixtures_sha256": "5" * 64,
				"runner_class": "local-development",
			}
			prefix = "artifacts/pr_smoke-local-development/"
			published_artifacts = {}
			for artifact_name, suffix in PUBLISHED_ARTIFACT_SUFFIXES.items():
				artifact_path = artifact_root.joinpath(*PurePosixPath(prefix).parts) / f"{artifact_name}{suffix}"
				artifact_path.parent.mkdir(parents=True, exist_ok=True)
				artifact_path.write_bytes(f"self-test {artifact_name}\n".encode("utf-8"))
				published_artifacts[artifact_name] = _audio_free_reference(artifact_path, artifact_root)
			assembly_envelope = {
				"schema_version": 1,
				"kind": ASSEMBLY_KIND,
				"qualification_scope": "core",
				"suite": "pr_smoke",
				"build": build,
				"published_artifacts": published_artifacts,
			}
			assembly_envelope_path = root / "assembly-envelope.json"
			_write_json_atomic(assembly_envelope_path, assembly_envelope)
			assembled = assemble_measurement_index(
				assembly_campaign, artifact_root, assembly_envelope_path, None,
			)
			_expect(assembled["case_count"] == 30 and assembled["transitive_artifact_count"] > 30, "self-test measurement assembly", "incomplete index/allowlist")
			index_path = artifact_root.joinpath(*PurePosixPath(assembled["measurement_index"]["path"]).parts)
			index_document = _load_canonical_json(index_path, "self-test measurement index")
			_expect(len(index_document["cases"]) == 30 and len(index_document["profile_bindings"]) == 5, "self-test measurement index", "profile/case coverage mismatch")
			_expect(all(set(case["reports"]) == MEASUREMENT.OFFLINE_REPORT_KEYS for case in index_document["cases"]), "self-test measurement index", "offline report map is not exact")

			tampered_target = artifact_root.joinpath(
				*PurePosixPath(index_document["cases"][0]["reports"]["candidate_benchmark_report"]["path"]).parts
			)
			tampered_bytes = tampered_target.read_bytes()
			tampered_target.write_bytes(tampered_bytes + b"tampered")
			try:
				assemble_measurement_index(assembly_campaign, artifact_root, assembly_envelope_path, None)
			except CampaignError as error:
				_expect("different bytes" in str(error), "self-test measurement report tamper", f"unexpected error: {error}")
			else:
				raise AssertionError("measurement assembler accepted a changed published report")
			tampered_target.write_bytes(tampered_bytes)

			wrong_suite = json.loads(json.dumps(assembly_envelope))
			wrong_suite["suite"] = "release"
			wrong_suite_path = root / "assembly-envelope-release.json"
			_write_json_atomic(wrong_suite_path, wrong_suite)
			try:
				assemble_measurement_index(assembly_campaign, artifact_root, wrong_suite_path, None)
			except CampaignError as error:
				_expect("core/pr_smoke" in str(error), "self-test offline suite restriction", f"unexpected error: {error}")
			else:
				raise AssertionError("measurement assembler accepted offline release evidence")

			wrong_build = json.loads(json.dumps(assembly_envelope))
			wrong_build["build"]["recipe_manifest_sha256"] = "f" * 64
			wrong_build_path = root / "assembly-envelope-wrong-build.json"
			_write_json_atomic(wrong_build_path, wrong_build)
			try:
				assemble_measurement_index(assembly_campaign, artifact_root, wrong_build_path, None)
			except CampaignError as error:
				_expect("hash-attested campaign fragment" in str(error), "self-test build mismatch", f"unexpected error: {error}")
			else:
				raise AssertionError("measurement assembler accepted a different recipe manifest")

			scorer_implementation = scorer.parent / "objective_quality_score.py"
			scorer_implementation_bytes = scorer_implementation.read_bytes()
			try:
				scorer_implementation.write_bytes(scorer_implementation_bytes + b"\n")
				try:
					run_campaign(args)
				except CampaignError as error:
					_expect("whole-run binding changed" in str(error), "self-test scorer implementation binding", f"unexpected error: {error}")
				else:
					raise AssertionError("campaign resumed after its scorer implementation binding changed")
			finally:
				scorer_implementation.write_bytes(scorer_implementation_bytes)

			benchmark.write_text(benchmark.read_text(encoding="utf-8") + "\n", encoding="utf-8")
			try:
				run_campaign(args)
			except CampaignError as error:
				_expect("whole-run binding changed" in str(error), "self-test whole-run binding", f"unexpected error: {error}")
			else:
				raise AssertionError("campaign resumed after its benchmark binding changed")
		finally:
			for name, old_value in old_environment.items():
				if old_value is None:
					os.environ.pop(name, None)
				else:
					os.environ[name] = old_value


def main(argv: Sequence[str] | None = None) -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--plan", type=Path)
	parser.add_argument("--case-set", type=Path, help="Protected case-set payload; required for final measurement-index assembly eligibility")
	parser.add_argument("--render-manifest", type=Path)
	parser.add_argument("--render-root", type=Path)
	parser.add_argument("--inventory", type=Path)
	parser.add_argument("--corpus-lock", type=Path)
	parser.add_argument("--transformation-manifest", type=Path)
	parser.add_argument("--benchmark", type=Path)
	parser.add_argument("--runtime-root", type=Path)
	parser.add_argument("--model-manifest", type=Path, help="Defaults to <runtime-root>/input-models.json")
	parser.add_argument("--recipe-manifest", type=Path, help="Defaults to <runtime-root>/input-recipes.json")
	parser.add_argument("--metrics-python", type=Path)
	parser.add_argument("--metrics-runtime-root", type=Path)
	parser.add_argument("--metrics-manifest", type=Path)
	parser.add_argument("--scorer", type=Path)
	parser.add_argument("--output-root", type=Path)
	parser.add_argument("--assemble-measurement-index", action="store_true")
	parser.add_argument("--campaign-root", type=Path)
	parser.add_argument("--artifact-root", type=Path)
	parser.add_argument("--qualification-envelope", type=Path)
	parser.add_argument("--measurement-index-output", type=Path)
	parser.add_argument("--timeout-seconds", type=int, default=900)
	parser.add_argument("--self-test", action="store_true")
	args = parser.parse_args(argv)
	try:
		if args.self_test:
			run_self_test()
			print("offline quality campaign self-test: ok")
			return 0
		if args.assemble_measurement_index:
			missing = [
				name for name, value in (
					("--campaign-root", args.campaign_root),
					("--artifact-root", args.artifact_root),
					("--qualification-envelope", args.qualification_envelope),
				)
				if value is None
			]
			if missing:
				raise CampaignError(f"measurement-index assembly is missing: {', '.join(missing)}")
			assembled = assemble_measurement_index(
				args.campaign_root, args.artifact_root, args.qualification_envelope,
				args.measurement_index_output,
			)
			print(json.dumps(assembled, sort_keys=True, separators=(",", ":")))
			return 0
		required = (
			"plan", "render_manifest", "render_root", "inventory", "corpus_lock", "transformation_manifest",
			"benchmark", "runtime_root",
			"metrics_python", "metrics_runtime_root", "metrics_manifest", "scorer", "output_root",
		)
		missing = [f"--{name.replace('_', '-')}" for name in required if getattr(args, name) is None]
		if missing:
			raise CampaignError(f"missing required arguments: {', '.join(missing)}")
		result = run_campaign(args)
		print(json.dumps({
			"status": result["status"], "run_id": result["run_id"],
			"run_binding_sha256": result["run_binding_sha256"], "summary": result["summary"],
		}, sort_keys=True, separators=(",", ":")))
		return 0
	except (AssertionError, CampaignError, KeyError, OSError, TypeError, ValueError) as error:
		print(f"offline quality campaign: error: {error}", file=sys.stderr)
		return 1


if __name__ == "__main__":
	raise SystemExit(main())
