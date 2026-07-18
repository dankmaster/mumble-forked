#!/usr/bin/env python3
"""Validate hash-bound runtime measurements used by audio quality gates.

The case-evidence JSONL is a portable summary, not a measurement authority.
This module joins every case to the exact benchmark or two-client reports that
produced its hard-gate values and derives those values again from the reports.
Only audio-free JSON evidence is accepted below the qualification artifact
namespace.
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import math
import os
import re
import stat
import sys
import tempfile
from pathlib import Path, PurePosixPath
from typing import Any, Mapping, MutableMapping, Sequence


class MeasurementEvidenceError(ValueError):
	"""Raised when measurement provenance is incomplete or inconsistent."""


SCHEMA_VERSION = 1
INDEX_KIND = "mumble-input-enhancement-measurement-index-v1"
SOAK_KIND = "mumble-input-enhancement-soak-v2"
TRANSITION_KIND = "mumble-input-enhancement-auto-transition-v1"
METRICS_RUNTIME_KIND = "mumble-audio-metrics-runtime-attestation-v1"
CASE_BINDING_KIND = "mumble-input-enhancement-case-binding-v1"
BENCHMARK_MEASUREMENT_KIND = "mumble-input-enhancement-benchmark-measurement-v1"
HOLDOUT_ATTESTATION_KIND = "mumble-input-enhancement-release-holdout-opening-v1"
HOLDOUT_OPENING_REPORT_KIND = "mumble-input-enhancement-release-holdout-opening-report-v1"
HOLDOUT_RECEIPT_KIND = "mumble-input-enhancement-release-holdout-receipt-v1"
HOLDOUT_PURPOSE = "input-enhancement-community-release-final-qualification"
SAMPLE_RATE_HZ = 48_000
FRAME_SAMPLES = 480
ORIGINAL_ROUTE_FIXED_STARTUP_FRAMES = 4
BITRATES = (8_000, 16_000, 40_000, 64_000, 128_000)
PACKET_FRAMES = (1, 2, 4)
TRANSMIT_MODES = ("Continuous", "PTT", "VAD")
HEX64 = re.compile(r"^[0-9a-f]{64}$")
CORE_PROFILES = ("Original", "Light", "Balanced", "Quality", "VoiceFocus")
AUTO_PROFILES = ("Auto",)
PROFILES_BY_SCOPE = {"core": CORE_PROFILES, "auto": AUTO_PROFILES}
AUTO_DIRECTED_PAIRS = (
	"Light->Balanced",
	"Balanced->Light",
	"Light->Quality",
	"Quality->Light",
	"Balanced->Quality",
	"Quality->Balanced",
)
REPORT_REFERENCE_KEYS = {"contains_audio_samples", "path", "sha256", "size_bytes"}
AUDIO_SUFFIXES = {
	".aac", ".aiff", ".alac", ".au", ".flac", ".m4a", ".mp3", ".ogg", ".opus", ".pcm", ".raw", ".wav",
}
E2E_REPORT_KEYS = {
	"candidate_adapter_result",
	"control_adapter_result",
	"control_fixed_timeline_score",
	"control_pre_opus_fixed_timeline_score",
	"e2e_manifest",
	"edge_adapter_result",
	"edge_fixed_timeline_score",
	"objective_score",
	"original_adapter_result",
	"route_fixed_timeline_score",
}
OFFLINE_REPORT_KEYS = {
	"candidate_benchmark_report",
	"case_binding_report",
	"edge_fixed_timeline_score",
	"objective_score",
	"original_benchmark_report",
}
STABLE_EXECUTION_IDENTITY = {
	"client_binary_sha256": "tested_binary_sha256",
	"model_manifest_sha256": "model_manifest_sha256",
	"recipe_manifest_sha256": "recipe_manifest_sha256",
	"runtime_payload_sha256": "staged_payload_sha256",
	"server_binary_sha256": "server_binary_sha256",
}


def _expect(condition: bool, path: str, message: str) -> None:
	if not condition:
		raise MeasurementEvidenceError(f"{path}: {message}")


def _mapping(value: Any, path: str) -> Mapping[str, Any]:
	_expect(isinstance(value, dict), path, "expected an object")
	return value


def _exact_keys(value: Mapping[str, Any], expected: set[str], path: str) -> None:
	missing = sorted(expected - set(value))
	unknown = sorted(set(value) - expected)
	_expect(not missing, path, f"missing keys: {', '.join(missing)}")
	_expect(not unknown, path, f"unknown keys: {', '.join(unknown)}")


def _integer(value: Any, path: str, minimum: int | None = None) -> int:
	_expect(isinstance(value, int) and not isinstance(value, bool), path, "expected an integer")
	if minimum is not None:
		_expect(value >= minimum, path, f"must be >= {minimum}")
	return value


def _number(value: Any, path: str, minimum: float | None = None) -> float:
	_expect(isinstance(value, (int, float)) and not isinstance(value, bool), path, "expected a number")
	result = float(value)
	_expect(math.isfinite(result), path, "must be finite")
	if minimum is not None:
		_expect(result >= minimum, path, f"must be >= {minimum}")
	return result


def _hash(value: Any, path: str) -> str:
	_expect(isinstance(value, str) and bool(HEX64.fullmatch(value)), path, "invalid lowercase SHA-256")
	return value


def canonical_json_bytes(value: Any) -> bytes:
	return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")


def canonical_json_sha256(value: Any) -> str:
	return hashlib.sha256(canonical_json_bytes(value)).hexdigest()


def _sha256(path: Path) -> str:
	digest = hashlib.sha256()
	with path.open("rb") as stream:
		for block in iter(lambda: stream.read(1024 * 1024), b""):
			digest.update(block)
	return digest.hexdigest()


def _load_json_bytes(raw: bytes, path: str) -> Mapping[str, Any]:
	def reject_duplicates(pairs: Sequence[tuple[str, Any]]) -> MutableMapping[str, Any]:
		result: MutableMapping[str, Any] = {}
		for key, value in pairs:
			if key in result:
				raise MeasurementEvidenceError(f"{path}: duplicate JSON key {key!r}")
			result[key] = value
		return result

	try:
		value = json.loads(raw.decode("utf-8"), object_pairs_hook=reject_duplicates)
	except (UnicodeDecodeError, json.JSONDecodeError) as error:
		raise MeasurementEvidenceError(f"{path}: invalid UTF-8 JSON: {error}") from error
	return _mapping(value, path)


def _is_reparse(path: Path) -> bool:
	try:
		metadata = os.lstat(path)
	except OSError as error:
		raise MeasurementEvidenceError(f"unable to inspect {path}: {error}") from error
	attributes = getattr(metadata, "st_file_attributes", 0)
	reparse_flag = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
	return path.is_symlink() or bool(attributes & reparse_flag)


def _safe_relative(value: Any, path: str, suffix: str | None = None, *, allow_audio: bool = False) -> str:
	_expect(isinstance(value, str) and bool(value), path, "expected a non-empty relative path")
	parsed = PurePosixPath(value)
	_expect(value == parsed.as_posix() and "\\" not in value, path, "must use normalized forward slashes")
	_expect(not parsed.is_absolute() and "." not in parsed.parts and ".." not in parsed.parts, path, "unsafe path")
	if not allow_audio:
		_expect(parsed.suffix.lower() not in AUDIO_SUFFIXES, path, "audio artifacts are forbidden")
	if suffix is not None:
		_expect(parsed.suffix.lower() == suffix, path, f"must use the {suffix} suffix")
	return value


def _resolve_regular_file(root: Path, relative: str, path: str) -> Path:
	resolved_root = root.resolve()
	candidate = resolved_root.joinpath(*PurePosixPath(relative).parts)
	current = resolved_root
	for part in PurePosixPath(relative).parts:
		current = current / part
		_expect(current.exists(), path, f"missing artifact: {relative}")
		_expect(not _is_reparse(current), path, "symlinks/reparse points are forbidden")
	actual = candidate.resolve()
	try:
		actual.relative_to(resolved_root)
	except ValueError as error:
		raise MeasurementEvidenceError(f"{path}: artifact escapes its root") from error
	_expect(actual.is_file(), path, f"missing regular artifact: {relative}")
	return actual


def _reference(value: Any, path: str, prefix: str, suffix: str = ".json") -> Mapping[str, Any]:
	reference = _mapping(value, path)
	_exact_keys(reference, REPORT_REFERENCE_KEYS, path)
	relative = _safe_relative(reference["path"], f"{path}.path", suffix)
	_expect(relative.startswith(prefix), f"{path}.path", f"must be below {prefix}")
	_hash(reference["sha256"], f"{path}.sha256")
	_integer(reference["size_bytes"], f"{path}.size_bytes", 1)
	_expect(reference["contains_audio_samples"] is False, f"{path}.contains_audio_samples", "must be explicitly audio-free")
	return reference


def _resolve_reference(root: Path, reference: Mapping[str, Any], path: str) -> Path:
	actual = _resolve_regular_file(root, str(reference["path"]), path)
	_expect(actual.stat().st_size == reference["size_bytes"], f"{path}.size_bytes", "artifact size mismatch")
	_expect(_sha256(actual) == reference["sha256"], f"{path}.sha256", "artifact hash mismatch")
	return actual


def _load_reference_json(root: Path, reference: Mapping[str, Any], path: str) -> Mapping[str, Any]:
	actual = _resolve_reference(root, reference, path)
	return _load_json_bytes(actual.read_bytes(), path)


def _same_reference(left: Mapping[str, Any], right: Mapping[str, Any], path: str) -> None:
	for key in REPORT_REFERENCE_KEYS:
		_expect(left[key] == right[key], f"{path}.{key}", "reference does not identify the same artifact")


def _same_number(actual: Any, expected: Any, path: str) -> None:
	left = _number(actual, path)
	right = _number(expected, f"{path} (derived)")
	_expect(math.isclose(left, right, rel_tol=1e-9, abs_tol=1e-9), path, f"does not match runtime report ({right!r})")


def _validate_execution_identity(
	identity_value: Any, build: Mapping[str, Any], path: str, *, allow_contract_file: bool = False,
) -> Mapping[str, Any]:
	identity = _mapping(identity_value, path)
	expected_keys = {*STABLE_EXECUTION_IDENTITY, "run_provenance_sha256"}
	if allow_contract_file:
		expected_keys.add("contract_file_sha256")
	_exact_keys(identity, expected_keys, path)
	for identity_key, build_key in STABLE_EXECUTION_IDENTITY.items():
		_hash(identity.get(identity_key), f"{path}.{identity_key}")
		_expect(identity[identity_key] == build[build_key], f"{path}.{identity_key}", f"does not match qualification.build.{build_key}")
	_hash(identity.get("run_provenance_sha256"), f"{path}.run_provenance_sha256")
	return identity


def _validate_profile_bindings(value: Any, qualification_scope: str, build: Mapping[str, Any]) -> Mapping[str, Sequence[Mapping[str, Any]]]:
	bindings = value
	_expect(isinstance(bindings, list) and bool(bindings), "measurement index.profile_bindings", "expected a non-empty array")
	binding_profiles = CORE_PROFILES if qualification_scope == "core" else ("Original", "Auto")
	by_profile: dict[str, list[Mapping[str, Any]]] = {profile: [] for profile in binding_profiles}
	validated: list[Mapping[str, Any]] = []
	expected_engines = {
		"Original": {"None"}, "Light": {"Speex"}, "Balanced": {"RNNoise"},
		"Quality": {"DeepFilterNet"}, "VoiceFocus": {"DeepFilterNet"},
		"Auto": {"Speex", "RNNoise", "DeepFilterNet"},
	}
	for position, binding_value in enumerate(bindings):
		path = f"measurement index.profile_bindings[{position}]"
		binding = _mapping(binding_value, path)
		_exact_keys(binding, {"engine", "models", "profile", "recipe"}, path)
		profile = binding["profile"]
		_expect(profile in by_profile, f"{path}.profile", "profile is outside qualification scope")
		engine = binding["engine"]
		_expect(engine in expected_engines[str(profile)], f"{path}.engine", "engine is not authorized for the profile")
		recipe = _mapping(binding["recipe"], f"{path}.recipe")
		_exact_keys(recipe, {"catalog_revision", "id", "manifest_sha256", "revision"}, f"{path}.recipe")
		_expect(isinstance(recipe["catalog_revision"], str) and bool(recipe["catalog_revision"]), f"{path}.recipe.catalog_revision", "required")
		_expect(isinstance(recipe["id"], str) and bool(recipe["id"]), f"{path}.recipe.id", "required")
		_expect(recipe["manifest_sha256"] == build["recipe_manifest_sha256"], f"{path}.recipe.manifest_sha256", "does not match qualification build")
		_integer(recipe["revision"], f"{path}.recipe.revision", 1)
		models = binding["models"]
		_expect(isinstance(models, list), f"{path}.models", "expected an array")
		validated_models = []
		for model_position, model_value in enumerate(models):
			model_path = f"{path}.models[{model_position}]"
			model = _mapping(model_value, model_path)
			_exact_keys(model, {"id", "sha256", "version"}, model_path)
			_expect(isinstance(model["id"], str) and bool(model["id"]), f"{model_path}.id", "required")
			_expect(isinstance(model["version"], str) and bool(model["version"]), f"{model_path}.version", "required")
			digest = _hash(model["sha256"], f"{model_path}.sha256")
			_expect(digest in build["model_hashes"], f"{model_path}.sha256", "model is outside the qualified product payload")
			validated_models.append(model)
		_expect(validated_models == sorted(validated_models, key=lambda item: str(item["id"])), f"{path}.models", "must be sorted by model ID")
		_expect(len({str(model["id"]) for model in validated_models}) == len(validated_models), f"{path}.models", "model IDs must be unique")
		if engine in ("None", "Speex"):
			_expect(not validated_models, f"{path}.models", "non-neural recipe must not bind models")
		else:
			_expect(len(validated_models) == 1, f"{path}.models", "qualified core neural recipes bind exactly one model")
		by_profile[str(profile)].append(binding)
		validated.append(binding)
	expected_order = sorted(
		validated,
		key=lambda item: (binding_profiles.index(str(item["profile"])), str(item["recipe"]["id"])),
	)
	_expect(validated == expected_order, "measurement index.profile_bindings", "must use canonical profile/recipe order")
	_expect(
		len({str(binding["recipe"]["id"]) for binding in validated}) == len(validated),
		"measurement index.profile_bindings",
		"recipe IDs must be unique",
	)
	if qualification_scope == "core":
		for profile in CORE_PROFILES:
			_expect(len(by_profile[profile]) == 1, "measurement index.profile_bindings", f"core profile {profile} must have exactly one product binding")
	else:
		_expect(len(by_profile["Original"]) == 1, "measurement index.profile_bindings", "Auto qualification must bind the exact Original control recipe")
		engines = [str(binding["engine"]) for binding in by_profile["Auto"]]
		_expect(sorted(engines) == sorted(expected_engines["Auto"]), "measurement index.profile_bindings", "Auto must bind exactly one Speex, RNNoise, and DeepFilterNet recipe")
	return by_profile


def _runtime_counters() -> dict[str, int]:
	return {
		"deadline_misses": 0,
		"latency_attestation_failures": 0,
		"model_hash_errors": 0,
		"nan_or_inf_count": 0,
		"new_clipping_cases": 0,
		"tail_drain_failures": 0,
		"unexplained_fallbacks": 0,
	}


def _merge_counters(target: MutableMapping[str, int], source: Mapping[str, int]) -> None:
	for key, value in source.items():
		target[key] += int(value)


def _validated_control_ranges(profile: str, engine: str) -> tuple[tuple[int, int], tuple[int, int]]:
	ranges = {
		"Original": ((0, 0), (0, 0)),
		"Light": ((0, 100), (0, 100)),
		"Balanced": ((20, 55), (10, 90)),
		"Quality": ((25, 90), (25, 100)),
		"VoiceFocus": ((70, 100), (40, 100)),
	}
	effective_profile = profile
	if profile == "Auto":
		effective_profile = {
			"Speex": "Light", "RNNoise": "Balanced", "DeepFilterNet": "Quality",
		}[engine]
	return ranges[effective_profile]


def _map_ui_control_to_recipe(requested: int, interval: tuple[int, int]) -> int:
	minimum, maximum = interval
	return minimum + ((requested * (maximum - minimum) + 50) // 100)


def _adapter_measurement(
	document: Mapping[str, Any], *, role: str, profile: str, input_sha256: str, build: Mapping[str, Any],
	profile_bindings: Mapping[str, Sequence[Mapping[str, Any]]], path: str,
) -> Mapping[str, Any]:
	required = {
		"capture", "diagnostics", "execution_identity", "input_sha256", "profile", "receiver_cleanup",
		"role", "schema_version", "sender_pre_opus", "status", "transport",
	}
	_exact_keys(document, required, path)
	_expect(document["schema_version"] == 3 and document["status"] == "passed", path, "must be a passing adapter-result schema v3")
	_expect(document["role"] == role and document["profile"] == profile, path, "role/profile mismatch")
	_expect(document["receiver_cleanup"] is False, f"{path}.receiver_cleanup", "must be disabled")
	_expect(document["input_sha256"] == input_sha256, f"{path}.input_sha256", "does not bind the expected rendered input")
	identity = _validate_execution_identity(document["execution_identity"], build, f"{path}.execution_identity", allow_contract_file=True)
	_hash(identity.get("contract_file_sha256"), f"{path}.execution_identity.contract_file_sha256")
	transport = _mapping(document["transport"], f"{path}.transport")
	_exact_keys(transport, {"frames_per_packet", "opus_bitrate_bps", "transmit_mode"}, f"{path}.transport")
	_expect(transport["opus_bitrate_bps"] in BITRATES, f"{path}.transport.opus_bitrate_bps", "unsupported bitrate")
	_expect(transport["frames_per_packet"] in PACKET_FRAMES, f"{path}.transport.frames_per_packet", "unsupported packet size")
	_expect(transport["transmit_mode"] in TRANSMIT_MODES, f"{path}.transport.transmit_mode", "unsupported transmit mode")
	diagnostics = _mapping(document["diagnostics"], f"{path}.diagnostics")
	required_diagnostics = {
		"active_engine", "active_models", "active_profile", "active_recipe", "callback_frame_count",
		"callback_p99_ms", "deadline_miss_count", "declared_latency_samples", "fallback_count",
		"invalid_output_count", "mean_rtf", "model_initialization_attempts", "tail_drained",
		"worker_frame_count", "worker_p99_ms",
	}
	_exact_keys(diagnostics, required_diagnostics, f"{path}.diagnostics")
	_expect(diagnostics["active_profile"] == profile, f"{path}.diagnostics.active_profile", "profile mismatch")
	latency = _integer(diagnostics["declared_latency_samples"], f"{path}.diagnostics.declared_latency_samples", 0)
	callback_frames = _integer(diagnostics["callback_frame_count"], f"{path}.diagnostics.callback_frame_count", 1)
	callback_p99 = _number(diagnostics["callback_p99_ms"], f"{path}.diagnostics.callback_p99_ms", 0)
	worker_frames = _integer(diagnostics["worker_frame_count"], f"{path}.diagnostics.worker_frame_count", 0)
	worker_p99 = _number(diagnostics["worker_p99_ms"], f"{path}.diagnostics.worker_p99_ms", 0)
	_expect(worker_frames > 0 or worker_p99 == 0.0, f"{path}.diagnostics.worker_p99_ms", "worker timing without worker frames")
	mean_rtf = _number(diagnostics["mean_rtf"], f"{path}.diagnostics.mean_rtf", 0)
	models = diagnostics["active_models"]
	_expect(isinstance(models, list), f"{path}.diagnostics.active_models", "expected an array")
	model_hash_errors = 0
	for index, model_value in enumerate(models):
		model = _mapping(model_value, f"{path}.diagnostics.active_models[{index}]")
		_exact_keys(model, {"id", "sha256", "version"}, f"{path}.diagnostics.active_models[{index}]")
		digest = _hash(model["sha256"], f"{path}.diagnostics.active_models[{index}].sha256")
		model_hash_errors += 0 if digest in build["model_hashes"] else 1
	_expect(models == sorted(models, key=lambda item: str(item["id"])), f"{path}.diagnostics.active_models", "must be sorted by model ID")
	recipe = _mapping(diagnostics["active_recipe"], f"{path}.diagnostics.active_recipe")
	_exact_keys(recipe, {"catalog_revision", "id", "manifest_sha256", "revision"}, f"{path}.diagnostics.active_recipe")
	model_hash_errors += 0 if recipe["manifest_sha256"] == build["recipe_manifest_sha256"] else 1
	observed_binding = {
		"profile": profile,
		"engine": diagnostics["active_engine"],
		"recipe": recipe,
		"models": models,
	}
	_expect(observed_binding in profile_bindings[profile], f"{path}.diagnostics", "active recipe/model binding is not authorized for the profile")
	capture = _mapping(document["capture"], f"{path}.capture")
	sender = _mapping(document["sender_pre_opus"], f"{path}.sender_pre_opus")
	for name, record in (("capture", capture), ("sender_pre_opus", sender)):
		_exact_keys(record, {"relative_path", "sha256", "size_bytes"}, f"{path}.{name}")
		_safe_relative(record["relative_path"], f"{path}.{name}.relative_path", ".wav", allow_audio=True)
		_hash(record["sha256"], f"{path}.{name}.sha256")
		_integer(record["size_bytes"], f"{path}.{name}.size_bytes", 1)
	counters = _runtime_counters()
	counters["deadline_misses"] = _integer(diagnostics["deadline_miss_count"], f"{path}.diagnostics.deadline_miss_count", 0)
	counters["unexplained_fallbacks"] = _integer(diagnostics["fallback_count"], f"{path}.diagnostics.fallback_count", 0)
	counters["nan_or_inf_count"] = _integer(diagnostics["invalid_output_count"], f"{path}.diagnostics.invalid_output_count", 0)
	counters["model_hash_errors"] = model_hash_errors
	counters["tail_drain_failures"] = 0 if diagnostics["tail_drained"] is True else 1
	audio_seconds = callback_frames * 0.01
	return {
		"identity": identity,
		"transport": dict(transport),
		"latency_samples": latency,
		"capture_sha256": capture["sha256"],
		"sender_pre_opus_sha256": sender["sha256"],
		"counters": counters,
		"audio_duration_seconds": audio_seconds,
		"processing_duration_seconds": audio_seconds * mean_rtf,
		"callback_p99_ms": callback_p99,
		"worker_p99_ms": worker_p99,
		"mean_rtf": mean_rtf,
	}


def _benchmark_measurement(
	document: Mapping[str, Any], *, profile: str, build: Mapping[str, Any],
	profile_bindings: Mapping[str, Sequence[Mapping[str, Any]]], path: str,
) -> Mapping[str, Any]:
	required = {
		"active_engine", "active_model_id", "active_model_sha256", "active_profile", "audio_ms", "callback_p99_ms",
		"clean_reference_sha256", "deadline_misses", "drain_sample_count", "fallback_count", "input_sample_count",
		"input_sha256",
		"input_saturated_sample_count", "maximum_processing_ms", "non_finite_sample_count",
		"out_of_range_sample_count", "output_sample_count", "processing_mode", "processing_padding_sample_count",
		"output_sha256", "processing_wall_ms", "reported_latency_samples", "requested_profile", "rtf", "sample_count",
		"kind", "requested_recipe_id", "recipe_revision", "sample_rate", "saturated_sample_count", "schema_version",
		"source_report_sha256", "used_fallback", "requested_ui_noise_reduction", "requested_ui_natural_clear",
		"validated_recipe_noise_reduction", "validated_recipe_natural_clear",
		"worker_processing_p99_ms",
	}
	_exact_keys(document, required, path)
	_expect(document["schema_version"] == 1 and document["kind"] == BENCHMARK_MEASUREMENT_KIND, path, "unsupported sanitized benchmark measurement")
	_hash(document["source_report_sha256"], f"{path}.source_report_sha256")
	_expect(document["processing_mode"] == "product-profile", f"{path}.processing_mode", "must use the product pipeline")
	_expect(document["requested_profile"] == profile and document["active_profile"] == profile, path, "profile mismatch")
	engine = document["active_engine"]
	expected_engines = {
		"Original": {"None"}, "Light": {"Speex"}, "Balanced": {"RNNoise"},
		"Quality": {"DeepFilterNet"}, "VoiceFocus": {"DeepFilterNet"},
		"Auto": {"Speex", "RNNoise", "DeepFilterNet"},
	}
	_expect(engine in expected_engines[profile], f"{path}.active_engine", "engine is not authorized for the profile")
	_expect(isinstance(document["requested_recipe_id"], str) and bool(document["requested_recipe_id"]), f"{path}.requested_recipe_id", "required")
	_integer(document["recipe_revision"], f"{path}.recipe_revision", 1)
	for field in (
		"requested_ui_noise_reduction", "requested_ui_natural_clear",
		"validated_recipe_noise_reduction", "validated_recipe_natural_clear",
	):
		value = _integer(document[field], f"{path}.{field}", 0)
		_expect(value <= 100, f"{path}.{field}", "must be an integer from 0 to 100")
	requested_noise = int(document["requested_ui_noise_reduction"])
	requested_character = int(document["requested_ui_natural_clear"])
	noise_range, character_range = _validated_control_ranges(profile, str(engine))
	expected_noise = _map_ui_control_to_recipe(requested_noise, noise_range)
	expected_character = _map_ui_control_to_recipe(requested_character, character_range)
	_expect(
		document["validated_recipe_noise_reduction"] == expected_noise,
		f"{path}.validated_recipe_noise_reduction",
		"does not equal the one-time profile-range mapping of the requested UI control",
	)
	_expect(
		document["validated_recipe_natural_clear"] == expected_character,
		f"{path}.validated_recipe_natural_clear",
		"does not equal the one-time profile-range mapping of the requested UI control",
	)
	_expect(document["sample_rate"] == SAMPLE_RATE_HZ, f"{path}.sample_rate", "must be 48 kHz")
	for field in ("input_sha256", "clean_reference_sha256", "output_sha256"):
		_hash(document[field], f"{path}.{field}")
	latency = _integer(document["reported_latency_samples"], f"{path}.reported_latency_samples", 0)
	drain = _integer(document["drain_sample_count"], f"{path}.drain_sample_count", 0)
	input_samples = _integer(document["input_sample_count"], f"{path}.input_sample_count", 1)
	output_samples = _integer(document["output_sample_count"], f"{path}.output_sample_count", 1)
	padding = _integer(document["processing_padding_sample_count"], f"{path}.processing_padding_sample_count", 0)
	timeline_failure = int(drain != latency or padding != 0 or output_samples != input_samples + drain or document["sample_count"] != output_samples)
	input_clipped = _integer(document["input_saturated_sample_count"], f"{path}.input_saturated_sample_count", 0)
	output_clipped = _integer(document["saturated_sample_count"], f"{path}.saturated_sample_count", 0)
	model_id = document["active_model_id"]
	model_digest = document["active_model_sha256"]
	model_hash_errors = 0
	if engine in ("RNNoise", "DeepFilterNet"):
		model_hash_errors = 0 if isinstance(model_id, str) and bool(model_id) and isinstance(model_digest, str) and model_digest in build["model_hashes"] else 1
	else:
		model_hash_errors = 0 if model_id == "" and model_digest == "" else 1
	matching_bindings = []
	for binding in profile_bindings[profile]:
		models = binding["models"]
		model_matches = (
			(not models and model_id == "" and model_digest == "")
			or (len(models) == 1 and models[0]["id"] == model_id and models[0]["sha256"] == model_digest)
		)
		if (
			binding["engine"] == engine
			and binding["recipe"]["id"] == document["requested_recipe_id"]
			and binding["recipe"]["revision"] == document["recipe_revision"]
			and model_matches
		):
			matching_bindings.append(binding)
	_expect(len(matching_bindings) == 1, path, "benchmark recipe/model identity is not one exact authorized profile binding")
	counters = _runtime_counters()
	_expect(isinstance(document["used_fallback"], bool), f"{path}.used_fallback", "expected a boolean")
	counters["deadline_misses"] = _integer(document["deadline_misses"], f"{path}.deadline_misses", 0)
	counters["unexplained_fallbacks"] = _integer(document["fallback_count"], f"{path}.fallback_count", 0) + (1 if document["used_fallback"] is True else 0)
	counters["nan_or_inf_count"] = _integer(document["non_finite_sample_count"], f"{path}.non_finite_sample_count", 0) + _integer(document["out_of_range_sample_count"], f"{path}.out_of_range_sample_count", 0)
	counters["new_clipping_cases"] = int(output_clipped > input_clipped)
	counters["tail_drain_failures"] = timeline_failure
	counters["model_hash_errors"] = model_hash_errors
	audio_seconds = _number(document["audio_ms"], f"{path}.audio_ms", 0.000001) / 1000.0
	processing_seconds = _number(document["processing_wall_ms"], f"{path}.processing_wall_ms", 0) / 1000.0
	expected_audio_seconds = input_samples / SAMPLE_RATE_HZ
	_expect(math.isclose(audio_seconds, expected_audio_seconds, rel_tol=1e-9, abs_tol=1e-9), f"{path}.audio_ms", "does not match input_sample_count/sample_rate")
	reported_rtf = _number(document["rtf"], f"{path}.rtf", 0)
	derived_rtf = processing_seconds / audio_seconds
	_expect(math.isclose(reported_rtf, derived_rtf, rel_tol=1e-9, abs_tol=1e-9), f"{path}.rtf", "does not match processing_wall_ms/audio_ms")
	return {
		"latency_samples": latency,
		"counters": counters,
		"audio_duration_seconds": audio_seconds,
		"processing_duration_seconds": processing_seconds,
		"callback_p99_ms": _number(document["callback_p99_ms"], f"{path}.callback_p99_ms", 0),
		"worker_p99_ms": _number(document["worker_processing_p99_ms"], f"{path}.worker_processing_p99_ms", 0),
		"maximum_processing_ms": _number(document["maximum_processing_ms"], f"{path}.maximum_processing_ms", 0),
		"recipe_id": document["requested_recipe_id"],
		"recipe_revision": int(document["recipe_revision"]),
		"engine": engine,
		"model_id": model_id,
		"model_sha256": model_digest,
		"requested_ui_controls": {
			"noise_reduction": document["requested_ui_noise_reduction"],
			"natural_clear": document["requested_ui_natural_clear"],
		},
		"validated_recipe_controls": {
			"noise_reduction": document["validated_recipe_noise_reduction"],
			"natural_clear": document["validated_recipe_natural_clear"],
		},
		"input_sha256": document["input_sha256"],
		"clean_reference_sha256": document["clean_reference_sha256"],
		"output_sha256": document["output_sha256"],
	}


def _fixed_timeline_measurement(
	document: Mapping[str, Any], *, reference_sha256: str, received_sha256: str,
	declared_latency_samples: int, path: str, require_complete_tail: bool,
	maximum_limit_samples: int | None = None, exact_limit_samples: int | None = None,
	expected_alignment: str = "fixed", maximum_observed_edge_samples: int | None = None,
) -> Mapping[str, Any]:
	allowed_keys = {
		"compared_samples", "declared_latency_samples", "end_loss_samples", "expected_end_samples",
		"expected_onset_samples", "fixed_timeline_sdr_db", "frame_samples", "loudness_match_gain",
		"missing_tail_samples", "onset_loss_samples", "passed", "qualification_limits",
		"received_clipped_samples", "received_end_samples", "received_onset_samples", "received_samples",
		"received_sha256", "reference_clipped_samples", "reference_end_samples", "reference_onset_samples",
		"reference_samples", "reference_sha256", "sample_rate_hz", "schema_version", "scorer",
		"timeline_alignment", "transport_baseline",
	}
	_expect(set(document).issubset(allowed_keys), path, f"unknown fields: {', '.join(sorted(set(document) - allowed_keys))}")
	_expect(document.get("schema_version") == 3 and document.get("scorer") == "mumble-fixed-timeline-v3", path, "unsupported fixed-timeline score")
	_expect(document.get("sample_rate_hz") == SAMPLE_RATE_HZ and document.get("frame_samples") == FRAME_SAMPLES, path, "invalid sample/frame rate")
	_expect(document.get("timeline_alignment") == expected_alignment, f"{path}.timeline_alignment", "unexpected alignment mode")
	_expect(document.get("reference_sha256") == reference_sha256, f"{path}.reference_sha256", "clean reference mismatch")
	_expect(document.get("received_sha256") == received_sha256, f"{path}.received_sha256", "measured signal mismatch")
	_expect(document.get("declared_latency_samples") == declared_latency_samples, f"{path}.declared_latency_samples", "runtime latency mismatch")
	limits = _mapping(document.get("qualification_limits"), f"{path}.qualification_limits")
	_exact_keys(limits, {"fail_on_new_clipping", "max_end_loss_samples", "max_onset_loss_samples", "require_complete_tail"}, f"{path}.qualification_limits")
	_expect(limits["require_complete_tail"] is require_complete_tail, f"{path}.qualification_limits.require_complete_tail", "does not match the route tail contract")
	_expect(limits["fail_on_new_clipping"] is True, f"{path}.qualification_limits.fail_on_new_clipping", "strict clipping gate is required")
	baseline = document.get("transport_baseline")
	if baseline is not None:
		baseline = _mapping(baseline, f"{path}.transport_baseline")
		_exact_keys(baseline, {
			"applied_end_adjustment_samples", "applied_onset_adjustment_samples", "clipped_samples",
			"declared_latency_samples", "qualification", "raw_end_offset_samples", "raw_onset_offset_samples",
			"received_end_samples", "received_onset_samples", "received_samples", "sha256",
		}, f"{path}.transport_baseline")
		_hash(baseline["sha256"], f"{path}.transport_baseline.sha256")
	for edge in ("onset", "end"):
		limit = _integer(limits[f"max_{edge}_loss_samples"], f"{path}.qualification_limits.max_{edge}_loss_samples", 0)
		if maximum_limit_samples is not None:
			_expect(limit <= maximum_limit_samples, f"{path}.qualification_limits.max_{edge}_loss_samples", f"may not exceed {maximum_limit_samples} samples")
		if exact_limit_samples is not None:
			_expect(limit == exact_limit_samples, f"{path}.qualification_limits.max_{edge}_loss_samples", f"must equal {exact_limit_samples} samples")
	onset = _integer(document.get("onset_loss_samples"), f"{path}.onset_loss_samples", 0)
	end = _integer(document.get("end_loss_samples"), f"{path}.end_loss_samples", 0)
	missing_tail = _integer(document.get("missing_tail_samples"), f"{path}.missing_tail_samples", 0)
	reference_clipped = _integer(document.get("reference_clipped_samples"), f"{path}.reference_clipped_samples", 0)
	received_clipped = _integer(document.get("received_clipped_samples"), f"{path}.received_clipped_samples", 0)
	computed_pass = (
		onset <= int(limits["max_onset_loss_samples"])
		and end <= int(limits["max_end_loss_samples"])
		and (not require_complete_tail or missing_tail == 0)
		and received_clipped <= reference_clipped
	)
	_expect(document.get("passed") is computed_pass, f"{path}.passed", "does not match the recorded measurements and strict limits")
	_expect(computed_pass, path, "fixed-timeline qualification failed")
	if maximum_observed_edge_samples is not None:
		_expect(max(onset, end) <= maximum_observed_edge_samples, path, f"observed speech-edge loss exceeds {maximum_observed_edge_samples} samples")
	return {
		"passed": computed_pass,
		"speech_edge_loss_ms": max(onset, end) * 1000.0 / SAMPLE_RATE_HZ,
		"tail_drain_failures": int(require_complete_tail and missing_tail != 0),
		"new_clipping_cases": int(received_clipped > reference_clipped),
	}


def _report_registry(
	root: Path, prefix: str, reports_value: Any, expected_keys: set[str], path: str,
	all_references: MutableMapping[str, Mapping[str, Any]],
) -> tuple[Mapping[str, Mapping[str, Any]], Mapping[str, Mapping[str, Any]]]:
	reports = _mapping(reports_value, path)
	_exact_keys(reports, expected_keys, path)
	references: dict[str, Mapping[str, Any]] = {}
	documents: dict[str, Mapping[str, Any]] = {}
	for name in sorted(expected_keys):
		reference = _reference(reports[name], f"{path}.{name}", prefix)
		previous = all_references.get(str(reference["path"]))
		if previous is None:
			all_references[str(reference["path"])] = reference
		else:
			_same_reference(reference, previous, f"{path}.{name}")
		references[name] = reference
		documents[name] = _load_reference_json(root, reference, f"{path}.{name}")
	return references, documents


def _verify_metrics_runtime_attestation(
	root: Path, prefix: str, reference_value: Any, build: Mapping[str, Any],
	objective_runtime_binding_sha256: str, all_references: MutableMapping[str, Mapping[str, Any]],
) -> None:
	reference = _reference(reference_value, "measurement index.metrics_runtime_attestation", prefix)
	all_references[str(reference["path"])] = reference
	document = _load_reference_json(root, reference, "measurement index.metrics_runtime_attestation")
	_exact_keys(document, {
		"files", "kind", "objective_runtime_binding_sha256", "payload_kind", "payload_sha256", "schema_version",
	}, "measurement index.metrics_runtime_attestation.report")
	_expect(
		document["schema_version"] == 1 and document["kind"] == METRICS_RUNTIME_KIND
		and document["payload_kind"] == "directory",
		"measurement index.metrics_runtime_attestation.report",
		"unsupported metrics-runtime attestation",
	)
	files = document["files"]
	_expect(isinstance(files, list) and bool(files), "measurement index.metrics_runtime_attestation.report.files", "expected a non-empty file inventory")
	validated_files = []
	for position, item_value in enumerate(files):
		path = f"measurement index.metrics_runtime_attestation.report.files[{position}]"
		item = _mapping(item_value, path)
		_exact_keys(item, {"path", "sha256", "size_bytes"}, path)
		validated_files.append({
			"path": _safe_relative(item["path"], f"{path}.path"),
			"sha256": _hash(item["sha256"], f"{path}.sha256"),
			"size_bytes": _integer(item["size_bytes"], f"{path}.size_bytes", 1),
		})
	_expect(
		validated_files == sorted(validated_files, key=lambda item: item["path"])
		and len({str(item["path"]) for item in validated_files}) == len(validated_files),
		"measurement index.metrics_runtime_attestation.report.files",
		"must be sorted by unique path",
	)
	payload_sha256 = canonical_json_sha256(validated_files)
	_expect(document["payload_sha256"] == payload_sha256, "measurement index.metrics_runtime_attestation.report.payload_sha256", "does not match the canonical file inventory")
	_expect(payload_sha256 == build["metrics_runtime_sha256"], "measurement index.metrics_runtime_attestation.report.payload_sha256", "does not match qualification.build.metrics_runtime_sha256")
	_expect(
		document["objective_runtime_binding_sha256"] == objective_runtime_binding_sha256,
		"measurement index.metrics_runtime_attestation.report.objective_runtime_binding_sha256",
		"does not bind the scorer/runtime metadata used by every objective score",
	)


def _verify_release_holdout_openings(
	root: Path, prefix: str, entries_value: Any, suite: str, build: Mapping[str, Any],
	approval_public_key_sha256_value: Any,
	cases: Sequence[Mapping[str, Any]], objective_scores: Sequence[Mapping[str, Any]],
	all_references: MutableMapping[str, Mapping[str, Any]],
) -> None:
	entries = entries_value
	_expect(isinstance(entries, list), "measurement index.release_holdout_openings", "expected an array")
	holdout_cases = [case for case in cases if case["dataset_split"] == "release-holdout"]
	holdout_scores = [score for score in objective_scores if score["dataset_split"] == "release-holdout"]
	if suite == "release":
		approval_public_key_sha256 = _hash(approval_public_key_sha256_value, "measurement index.release_holdout_approval_public_key_sha256")
		_expect(build.get("release_holdout_approval_public_key_sha256") == approval_public_key_sha256, "qualification.build.release_holdout_approval_public_key_sha256", "does not match the measurement-index trust root")
		_expect(len(holdout_cases) == len(cases), "release case evidence", "every final release case must use the protected release-holdout split")
		_expect(len(holdout_scores) == len(objective_scores), "release objective scores", "every final release score must use the protected release-holdout split")
	else:
		_expect(approval_public_key_sha256_value is None, "measurement index.release_holdout_approval_public_key_sha256", "must be null outside the final release suite")
		approval_public_key_sha256 = None
		_expect(not holdout_cases and not holdout_scores, "release-holdout evidence", "is only permitted in the final release suite")
	_expect(len(entries) == len(holdout_scores), "measurement index.release_holdout_openings", "must identify every release-holdout score exactly once")
	if not entries:
		return

	score_registry = {(str(score["profile"]), str(score["case_id"])): score for score in holdout_scores}
	seen_keys: set[tuple[str, str]] = set()
	seen_run_ids: set[str] = set()
	entry_order: list[tuple[str, str]] = []
	artifact_suffixes = {
		"attestation": ".json", "detached_signature": ".sig", "approval_public_key": ".pub",
		"receipt": ".json", "opening_report": ".json",
	}
	for position, entry_value in enumerate(entries):
		path = f"measurement index.release_holdout_openings[{position}]"
		entry = _mapping(entry_value, path)
		_exact_keys(entry, {
			"approval_public_key", "attestation", "case_id", "detached_signature",
			"opening_report", "profile", "receipt", "run_id",
		}, path)
		key = (str(entry["profile"]), str(entry["case_id"]))
		_expect(key in score_registry and key not in seen_keys, path, "does not identify one unique release-holdout score")
		seen_keys.add(key)
		entry_order.append(key)
		run_id = entry["run_id"]
		_expect(isinstance(run_id, str) and bool(re.fullmatch(r"[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}", run_id)), f"{path}.run_id", "must be a canonical lowercase UUIDv4")
		_expect(run_id not in seen_run_ids, f"{path}.run_id", "one-shot run id is reused")
		seen_run_ids.add(run_id)

		references: dict[str, Mapping[str, Any]] = {}
		for name, suffix in artifact_suffixes.items():
			reference = _reference(entry[name], f"{path}.{name}", prefix, suffix)
			previous = all_references.get(str(reference["path"]))
			if previous is None:
				all_references[str(reference["path"])] = reference
			else:
				_same_reference(reference, previous, f"{path}.{name}")
			references[name] = reference

		score = score_registry[key]
		opening = _mapping(score.get("release_holdout_opening"), f"{path}.objective_score.release_holdout_opening")
		_expect(opening["run_id"] == run_id and opening["purpose"] == HOLDOUT_PURPOSE, f"{path}.objective_score.release_holdout_opening", "run or purpose mismatch")
		_expect(opening["corpus_inventory_sha256"] == build["corpus_inventory_sha256"], f"{path}.objective_score.release_holdout_opening.corpus_inventory_sha256", "does not match qualification build")
		_expect(opening["mixture_plan_sha256"] == build["mixture_plan_sha256"], f"{path}.objective_score.release_holdout_opening.mixture_plan_sha256", "does not match qualification build")
		_expect(opening["release_build"]["sha256"] == build["tested_binary_sha256"], f"{path}.objective_score.release_holdout_opening.release_build.sha256", "does not identify the tested client")
		for name in artifact_suffixes:
			expected_record = {"sha256": references[name]["sha256"], "size_bytes": references[name]["size_bytes"]}
			_expect(opening[name] == expected_record, f"{path}.objective_score.release_holdout_opening.{name}", "does not identify the indexed artifact")

		attestation = _load_reference_json(root, references["attestation"], f"{path}.attestation")
		_exact_keys(attestation, {
			"attestation", "authorization", "authorized_at_utc", "dataset_split", "expected",
			"opening_report_relative_path", "purpose", "receipt_relative_path", "run_id",
			"schema_version", "valid_until_utc",
		}, f"{path}.attestation")
		_expect(
			attestation["schema_version"] == 1 and attestation["attestation"] == HOLDOUT_ATTESTATION_KIND
			and attestation["dataset_split"] == "release-holdout" and attestation["purpose"] == HOLDOUT_PURPOSE
			and attestation["run_id"] == run_id and attestation["authorized_at_utc"] == opening["authorized_at_utc"]
			and attestation["valid_until_utc"] == opening["valid_until_utc"],
			f"{path}.attestation", "does not match the consumed release-holdout opening",
		)
		_expect(attestation["receipt_relative_path"] == f"receipts/{run_id}.json", f"{path}.attestation.receipt_relative_path", "does not bind the one-shot receipt")
		_expect(attestation["opening_report_relative_path"] == f"reports/{run_id}.json", f"{path}.attestation.opening_report_relative_path", "does not bind the opening report")
		expected = _mapping(attestation["expected"], f"{path}.attestation.expected")
		_exact_keys(expected, {"corpus_inventory_sha256", "mixture_plan_sha256", "release_build"}, f"{path}.attestation.expected")
		_expect(expected == {
			"corpus_inventory_sha256": build["corpus_inventory_sha256"],
			"mixture_plan_sha256": build["mixture_plan_sha256"],
			"release_build": opening["release_build"],
		}, f"{path}.attestation.expected", "does not bind the exact corpus, plan, and tested client")
		authorization = _mapping(attestation["authorization"], f"{path}.attestation.authorization")
		_exact_keys(authorization, {"key_id", "kind", "public_key_sha256", "signature_encoding"}, f"{path}.attestation.authorization")
		_expect(
			authorization["kind"] == "detached-ed25519-release-owner-approval-v1"
			and authorization["signature_encoding"] == "raw-ed25519-64-byte-file"
			and authorization["public_key_sha256"] == references["approval_public_key"]["sha256"],
			f"{path}.attestation.authorization", "unsupported or mismatched release-owner authorization",
		)
		_expect(references["approval_public_key"]["sha256"] == approval_public_key_sha256, f"{path}.approval_public_key.sha256", "does not match the externally pinned release-owner trust root")
		public_key = _resolve_reference(root, references["approval_public_key"], f"{path}.approval_public_key").read_bytes()
		signature = _resolve_reference(root, references["detached_signature"], f"{path}.detached_signature").read_bytes()
		_expect(len(public_key) == 32, f"{path}.approval_public_key", "must contain exactly 32 raw Ed25519 bytes")
		_expect(len(signature) == 64, f"{path}.detached_signature", "must contain exactly 64 raw Ed25519 bytes")
		try:
			from objective_quality_score import ObjectiveScoreError, _verify_ed25519
			_verify_ed25519(public_key, canonical_json_bytes(attestation), signature)
		except (ImportError, ObjectiveScoreError) as error:
			raise MeasurementEvidenceError(f"{path}.detached_signature: verification failed: {error}") from error

		common = {
			"run_id": run_id, "purpose": HOLDOUT_PURPOSE,
			"corpus_inventory_sha256": build["corpus_inventory_sha256"],
			"mixture_plan_sha256": build["mixture_plan_sha256"],
			"release_build": opening["release_build"], "attestation": opening["attestation"],
			"detached_signature": opening["detached_signature"], "approval_public_key": opening["approval_public_key"],
		}
		receipt = _load_reference_json(root, references["receipt"], f"{path}.receipt")
		_exact_keys(receipt, {
			"approval_public_key", "attestation", "consumed_at_utc", "corpus_inventory_sha256",
			"detached_signature", "mixture_plan_sha256", "purpose", "receipt", "release_build",
			"run_id", "schema_version", "status",
		}, f"{path}.receipt")
		_expect(receipt == {
			"schema_version": 1, "receipt": HOLDOUT_RECEIPT_KIND,
			"status": "opening-consumed-before-audio-read", "consumed_at_utc": receipt["consumed_at_utc"], **common,
		}, f"{path}.receipt", "does not match the consumed one-shot opening")
		report = _load_reference_json(root, references["opening_report"], f"{path}.opening_report")
		_exact_keys(report, {
			"approval_public_key", "attestation", "authorized_at_utc", "consumed_at_utc",
			"corpus_inventory_sha256", "detached_signature", "mixture_plan_sha256", "opening_report",
			"purpose", "receipt", "release_build", "run_id", "schema_version", "status", "valid_until_utc",
		}, f"{path}.opening_report")
		_expect(report == {
			"schema_version": 1, "opening_report": HOLDOUT_OPENING_REPORT_KIND,
			"status": "opening-consumed-before-audio-read", "authorized_at_utc": opening["authorized_at_utc"],
			"valid_until_utc": opening["valid_until_utc"], "consumed_at_utc": receipt["consumed_at_utc"],
			"receipt": opening["receipt"], **common,
		}, f"{path}.opening_report", "does not match the signed opening and receipt")

	expected_order = sorted(score_registry, key=lambda key: (PROFILES_BY_SCOPE["core"].index(key[0]) if key[0] in CORE_PROFILES else len(CORE_PROFILES), key[1]))
	_expect(entry_order == expected_order, "measurement index.release_holdout_openings", "must use canonical profile/case order")


def _offline_case_binding(
	document: Mapping[str, Any], case: Mapping[str, Any], build: Mapping[str, Any], path: str,
) -> Mapping[str, Any]:
	_exact_keys(document, {
		"build_binding", "case_id", "clean_reference_sha256", "condition", "dataset_split", "kind",
		"measurement_mode", "plan_case_sha256", "profile", "render_entry_sha256",
		"render_manifest_sha256", "schema_version", "source_input_sha256",
	}, path)
	_expect(
		document["schema_version"] == 1 and document["kind"] == CASE_BINDING_KIND
		and document["measurement_mode"] == "offline",
		path,
		"unsupported offline case-binding report",
	)
	_expect(
		document["case_id"] == case["case_id"] and document["profile"] == case["profile"]
		and document["condition"] == case["condition"] and document["dataset_split"] == case["dataset_split"],
		path,
		"case/profile/condition/split mismatch",
	)
	build_binding = _mapping(document["build_binding"], f"{path}.build_binding")
	fields = {"case_set_sha256", "corpus_inventory_sha256", "corpus_lock_sha256", "mixture_plan_sha256"}
	_exact_keys(build_binding, fields, f"{path}.build_binding")
	for field in fields:
		_hash(build_binding[field], f"{path}.build_binding.{field}")
		_expect(build_binding[field] == build[field], f"{path}.build_binding.{field}", f"does not match qualification.build.{field}")
	for field in (
		"clean_reference_sha256", "plan_case_sha256", "render_entry_sha256",
		"render_manifest_sha256", "source_input_sha256",
	):
		_hash(document[field], f"{path}.{field}")
	return document


def _derive_offline_case(
	case: Mapping[str, Any], score: Mapping[str, Any], documents: Mapping[str, Mapping[str, Any]], build: Mapping[str, Any],
	profile_bindings: Mapping[str, Sequence[Mapping[str, Any]]], path: str,
) -> Mapping[str, Any]:
	profile = str(case["profile"])
	binding = _offline_case_binding(documents["case_binding_report"], case, build, f"{path}.reports.case_binding_report")
	original = _benchmark_measurement(documents["original_benchmark_report"], profile="Original", build=build, profile_bindings=profile_bindings, path=f"{path}.reports.original_benchmark_report")
	candidate = _benchmark_measurement(documents["candidate_benchmark_report"], profile=profile, build=build, profile_bindings=profile_bindings, path=f"{path}.reports.candidate_benchmark_report")
	_expect(original["latency_samples"] == 0, f"{path}.reports.original_benchmark_report", "Original must have zero latency")
	clean_sha = score["inputs"]["clean_reference"]["sha256"]
	source_sha = binding["source_input_sha256"]
	_expect(original["input_sha256"] == source_sha and candidate["input_sha256"] == source_sha, path, "benchmark inputs do not bind the rendered source")
	_expect(original["clean_reference_sha256"] == clean_sha and candidate["clean_reference_sha256"] == clean_sha and binding["clean_reference_sha256"] == clean_sha, path, "benchmark/objective inputs do not bind one clean reference")
	_expect(original["output_sha256"] == score["inputs"]["noisy_original"]["sha256"], path, "Original benchmark output does not bind the objective Original input")
	_expect(candidate["output_sha256"] == score["inputs"]["candidate"]["sha256"], path, "candidate benchmark output does not bind the objective candidate input")
	edge = _fixed_timeline_measurement(
		documents["edge_fixed_timeline_score"], reference_sha256=clean_sha,
		received_sha256=score["inputs"]["candidate"]["sha256"],
		declared_latency_samples=int(candidate["latency_samples"]), path=f"{path}.reports.edge_fixed_timeline_score",
		require_complete_tail=True, maximum_limit_samples=FRAME_SAMPLES,
		expected_alignment="fixed", maximum_observed_edge_samples=FRAME_SAMPLES,
	)
	counters = _runtime_counters()
	_merge_counters(counters, candidate["counters"])
	counters["new_clipping_cases"] += int(edge["new_clipping_cases"])
	counters["tail_drain_failures"] += int(edge["tail_drain_failures"])
	counters["latency_attestation_failures"] += int(score["alignment"]["candidate_latency_samples"] != candidate["latency_samples"])
	return {
		"clean_reference_sha256": clean_sha,
		"source_input_sha256": source_sha,
		"plan_case_sha256": binding["plan_case_sha256"],
		"render_entry_sha256": binding["render_entry_sha256"],
		"algorithmic_latency_ms": int(candidate["latency_samples"]) * 1000.0 / SAMPLE_RATE_HZ,
		"speech_edge_loss_ms": edge["speech_edge_loss_ms"],
		"counters": counters,
		"performance": {
			"audio_duration_seconds": candidate["audio_duration_seconds"],
			"processing_duration_seconds": candidate["processing_duration_seconds"],
			"callback_durations_ms": [candidate["callback_p99_ms"]],
			"worker_durations_ms": [candidate["worker_p99_ms"]],
			"max_internal_processing_ms": max(candidate["maximum_processing_ms"], candidate["worker_p99_ms"]),
			"memory_growth_bytes": 0,
			"soak_duration_seconds": 0,
		},
	}


def _derive_e2e_case(
	case: Mapping[str, Any], score: Mapping[str, Any], references: Mapping[str, Mapping[str, Any]],
	documents: Mapping[str, Mapping[str, Any]], build: Mapping[str, Any],
	profile_bindings: Mapping[str, Sequence[Mapping[str, Any]]], path: str,
) -> Mapping[str, Any]:
	profile = str(case["profile"])
	clean_sha = score["inputs"]["clean_reference"]["sha256"]
	source_sha = _hash(_mapping(documents["candidate_adapter_result"], f"{path}.reports.candidate_adapter_result").get("input_sha256"), f"{path}.source_input_sha256")
	candidate_role = "candidate"
	edge_role = "candidate_edge"
	candidate = _adapter_measurement(documents["candidate_adapter_result"], role=candidate_role, profile=profile, input_sha256=source_sha, build=build, profile_bindings=profile_bindings, path=f"{path}.reports.candidate_adapter_result")
	edge_runtime = _adapter_measurement(documents["edge_adapter_result"], role=edge_role, profile=profile, input_sha256=clean_sha, build=build, profile_bindings=profile_bindings, path=f"{path}.reports.edge_adapter_result")
	original = _adapter_measurement(documents["original_adapter_result"], role="original_comparison", profile="Original", input_sha256=source_sha, build=build, profile_bindings=profile_bindings, path=f"{path}.reports.original_adapter_result")
	control = _adapter_measurement(documents["control_adapter_result"], role="control", profile="Original", input_sha256=clean_sha, build=build, profile_bindings=profile_bindings, path=f"{path}.reports.control_adapter_result")
	for label, runtime in (("edge", edge_runtime), ("original", original), ("control", control)):
		_expect(runtime["transport"] == candidate["transport"], f"{path}.{label}.transport", "E2E roles do not share one transport configuration")
	for label, runtime in (("edge", edge_runtime),):
		_expect(runtime["latency_samples"] == candidate["latency_samples"], f"{path}.{label}.latency_samples", "candidate runtime roles disagree")
	_expect(original["latency_samples"] == 0 and control["latency_samples"] == 0, path, "Original roles must declare zero enhancement latency")
	manifest = documents["e2e_manifest"]
	_exact_keys(manifest, {
		"case_id", "input_timeline_gate", "private_audio_do_not_upload", "profile", "receiver_cleanup",
		"qualification_binding", "results", "route_control", "run_provenance_sha256", "schema_version", "status",
	}, f"{path}.reports.e2e_manifest")
	_expect(manifest.get("schema_version") == 3 and manifest.get("status") == "passed", f"{path}.reports.e2e_manifest", "must be a passing schema-v3 E2E manifest")
	_expect(manifest.get("case_id") == case["case_id"] and manifest.get("profile") == profile, f"{path}.reports.e2e_manifest", "case/profile mismatch")
	_expect(manifest.get("receiver_cleanup") is False and manifest.get("private_audio_do_not_upload") is True, f"{path}.reports.e2e_manifest", "receiver cleanup/privacy mismatch")
	qualification_binding = _mapping(manifest["qualification_binding"], f"{path}.reports.e2e_manifest.qualification_binding")
	_exact_keys(qualification_binding, {
		"case_id", "case_set_sha256", "clean_reference_sha256", "corpus_inventory_sha256",
		"input_enhancement_policy_manifest_sha256", "input_enhancement_policy_signature_sha256",
		"corpus_lock_sha256", "dataset_split", "mixture_plan_sha256", "plan_case_sha256",
		"profile", "render_entry_sha256", "render_manifest_sha256", "source_input_sha256",
	}, f"{path}.reports.e2e_manifest.qualification_binding")
	for field in ("case_set_sha256", "corpus_inventory_sha256", "corpus_lock_sha256", "mixture_plan_sha256"):
		_hash(qualification_binding[field], f"{path}.reports.e2e_manifest.qualification_binding.{field}")
		_expect(qualification_binding[field] == build[field], f"{path}.reports.e2e_manifest.qualification_binding.{field}", f"does not match qualification.build.{field}")
	for field in ("plan_case_sha256", "render_entry_sha256", "render_manifest_sha256", "source_input_sha256", "clean_reference_sha256"):
		_hash(qualification_binding[field], f"{path}.reports.e2e_manifest.qualification_binding.{field}")
	policy_manifest_sha256 = qualification_binding["input_enhancement_policy_manifest_sha256"]
	policy_signature_sha256 = qualification_binding["input_enhancement_policy_signature_sha256"]
	_expect(
		(policy_manifest_sha256 is None) == (policy_signature_sha256 is None),
		f"{path}.reports.e2e_manifest.qualification_binding.input_enhancement_policy",
		"manifest and signature hashes must be present together",
	)
	if profile != "Original":
		_hash(policy_manifest_sha256, f"{path}.reports.e2e_manifest.qualification_binding.input_enhancement_policy_manifest_sha256")
		_hash(policy_signature_sha256, f"{path}.reports.e2e_manifest.qualification_binding.input_enhancement_policy_signature_sha256")
	elif policy_manifest_sha256 is not None:
		_hash(policy_manifest_sha256, f"{path}.reports.e2e_manifest.qualification_binding.input_enhancement_policy_manifest_sha256")
		_hash(policy_signature_sha256, f"{path}.reports.e2e_manifest.qualification_binding.input_enhancement_policy_signature_sha256")
	_expect(qualification_binding["source_input_sha256"] == source_sha, f"{path}.reports.e2e_manifest.qualification_binding.source_input_sha256", "does not bind the adapter source input")
	_expect(qualification_binding["clean_reference_sha256"] == clean_sha, f"{path}.reports.e2e_manifest.qualification_binding.clean_reference_sha256", "does not bind the objective clean reference")
	_expect(
		qualification_binding["case_id"] == case["case_id"]
		and qualification_binding["profile"] == profile
		and qualification_binding["dataset_split"] == case["dataset_split"],
		f"{path}.reports.e2e_manifest.qualification_binding",
		"case/profile/split binding mismatch",
	)
	input_gate = _mapping(manifest["input_timeline_gate"], f"{path}.reports.e2e_manifest.input_timeline_gate")
	_exact_keys(input_gate, {
		"alignment", "artifact", "complete_tail_required", "max_end_loss_samples",
		"max_onset_loss_samples", "roles",
	}, f"{path}.reports.e2e_manifest.input_timeline_gate")
	expected_edge_roles = ["control", "candidate_edge"]
	_expect(
		input_gate == {
			"artifact": "sender_pre_opus", "alignment": "fixed-declared-latency",
			"roles": expected_edge_roles, "max_onset_loss_samples": FRAME_SAMPLES,
			"max_end_loss_samples": FRAME_SAMPLES, "complete_tail_required": True,
		},
		f"{path}.reports.e2e_manifest.input_timeline_gate",
		"does not describe the strict sender-input edge/tail gate",
	)
	transport = candidate["transport"]
	route_budget = (ORIGINAL_ROUTE_FIXED_STARTUP_FRAMES + int(transport["frames_per_packet"])) * FRAME_SAMPLES
	require_route_tail = transport["transmit_mode"] != "VAD"
	route_contract = _mapping(manifest["route_control"], f"{path}.reports.e2e_manifest.route_control")
	_exact_keys(route_contract, {
		"capture_tail_rule", "causal_tail_drain_required", "end_loss_budget_samples",
		"legacy_original_parity_required", "onset_budget_samples", "receiver_edge_gate",
	}, f"{path}.reports.e2e_manifest.route_control")
	_expect(
		route_contract == {
			"onset_budget_samples": route_budget, "end_loss_budget_samples": route_budget,
			"receiver_edge_gate": "route-bounded-not-input-latency",
			"capture_tail_rule": "complete" if require_route_tail else "vad-speech-edge",
			"causal_tail_drain_required": True, "legacy_original_parity_required": True,
		},
		f"{path}.reports.e2e_manifest.route_control",
		"does not match the attested OG transport route contract",
	)
	results = _mapping(manifest.get("results"), f"{path}.reports.e2e_manifest.results")
	role_bindings = {
		"candidate": "candidate_adapter_result", "candidate_edge": "edge_adapter_result",
		"original_comparison": "original_adapter_result", "control": "control_adapter_result",
	}
	_exact_keys(results, set(role_bindings), f"{path}.reports.e2e_manifest.results")
	qualification_purposes = {
		"control": "clean-original-route-control",
		"original_comparison": "noisy-original-quality-comparison",
		"candidate_edge": "clean-enhanced-input-edge-probe",
		"candidate": "noisy-enhanced-candidate",
	}
	for role, report_name in role_bindings.items():
		result = _mapping(results.get(role), f"{path}.reports.e2e_manifest.results.{role}")
		_exact_keys(result, {
			"active_models", "active_recipe", "adapter_contract_sha256", "adapter_result_sha256",
			"capture_sha256", "execution_identity", "fixed_timeline_score_sha256", "performance",
			"pre_opus_fixed_timeline_score_sha256", "qualification_purpose", "sender_pre_opus_sha256",
		}, f"{path}.reports.e2e_manifest.results.{role}")
		_expect(result.get("adapter_result_sha256") == references[report_name]["sha256"], f"{path}.reports.e2e_manifest.results.{role}.adapter_result_sha256", "adapter-result hash mismatch")
		report_document = documents[report_name]
		report_diagnostics = _mapping(report_document["diagnostics"], f"{path}.reports.{report_name}.diagnostics")
		_expect(result["adapter_contract_sha256"] == report_document["execution_identity"]["contract_file_sha256"], f"{path}.reports.e2e_manifest.results.{role}.adapter_contract_sha256", "adapter-contract hash mismatch")
		_expect(result["execution_identity"] == report_document["execution_identity"], f"{path}.reports.e2e_manifest.results.{role}.execution_identity", "adapter identity mismatch")
		_expect(result["capture_sha256"] == report_document["capture"]["sha256"], f"{path}.reports.e2e_manifest.results.{role}.capture_sha256", "capture hash mismatch")
		_expect(result["sender_pre_opus_sha256"] == report_document["sender_pre_opus"]["sha256"], f"{path}.reports.e2e_manifest.results.{role}.sender_pre_opus_sha256", "sender pre-Opus hash mismatch")
		_expect(result["active_recipe"] == report_diagnostics["active_recipe"], f"{path}.reports.e2e_manifest.results.{role}.active_recipe", "active recipe mismatch")
		_expect(result["active_models"] == report_diagnostics["active_models"], f"{path}.reports.e2e_manifest.results.{role}.active_models", "active models mismatch")
		expected_performance = {
			"callback_frame_count": report_diagnostics["callback_frame_count"],
			"callback_p99_ms": report_diagnostics["callback_p99_ms"],
			"model_initialization_attempts": report_diagnostics["model_initialization_attempts"],
			"worker_frame_count": report_diagnostics["worker_frame_count"],
			"worker_p99_ms": report_diagnostics["worker_p99_ms"],
			"mean_rtf": report_diagnostics["mean_rtf"],
		}
		_expect(result["performance"] == expected_performance, f"{path}.reports.e2e_manifest.results.{role}.performance", "runtime performance mismatch")
		_expect(result["qualification_purpose"] == qualification_purposes[role], f"{path}.reports.e2e_manifest.results.{role}.qualification_purpose", "unexpected role purpose")
		if role in ("control", "candidate"):
			_hash(result["fixed_timeline_score_sha256"], f"{path}.reports.e2e_manifest.results.{role}.fixed_timeline_score_sha256")
		else:
			_expect(result["fixed_timeline_score_sha256"] is None, f"{path}.reports.e2e_manifest.results.{role}.fixed_timeline_score_sha256", "role must not publish a receiver route score")
		if role in ("control", "candidate_edge"):
			_hash(result["pre_opus_fixed_timeline_score_sha256"], f"{path}.reports.e2e_manifest.results.{role}.pre_opus_fixed_timeline_score_sha256")
		else:
			_expect(result["pre_opus_fixed_timeline_score_sha256"] is None, f"{path}.reports.e2e_manifest.results.{role}.pre_opus_fixed_timeline_score_sha256", "role must not publish a sender edge score")
	_expect(manifest.get("run_provenance_sha256") == candidate["identity"]["run_provenance_sha256"], f"{path}.reports.e2e_manifest.run_provenance_sha256", "execution provenance mismatch")
	for label, runtime in (("edge", edge_runtime), ("original", original), ("control", control)):
		_expect(runtime["identity"]["run_provenance_sha256"] == candidate["identity"]["run_provenance_sha256"], f"{path}.{label}.execution_identity", "roles do not share run provenance")
	edge_result = _mapping(results[edge_role], f"{path}.reports.e2e_manifest.results.{edge_role}")
	route_result = _mapping(results[candidate_role], f"{path}.reports.e2e_manifest.results.route")
	control_result = _mapping(results["control"], f"{path}.reports.e2e_manifest.results.control")
	_expect(edge_result.get("pre_opus_fixed_timeline_score_sha256") == references["edge_fixed_timeline_score"]["sha256"], f"{path}.reports.edge_fixed_timeline_score", "E2E manifest hash mismatch")
	_expect(route_result.get("fixed_timeline_score_sha256") == references["route_fixed_timeline_score"]["sha256"], f"{path}.reports.route_fixed_timeline_score", "E2E manifest hash mismatch")
	_expect(control_result.get("fixed_timeline_score_sha256") == references["control_fixed_timeline_score"]["sha256"], f"{path}.reports.control_fixed_timeline_score", "E2E manifest hash mismatch")
	_expect(control_result.get("pre_opus_fixed_timeline_score_sha256") == references["control_pre_opus_fixed_timeline_score"]["sha256"], f"{path}.reports.control_pre_opus_fixed_timeline_score", "E2E manifest hash mismatch")
	edge = _fixed_timeline_measurement(
		documents["edge_fixed_timeline_score"], reference_sha256=clean_sha,
		received_sha256=edge_runtime["sender_pre_opus_sha256"], declared_latency_samples=int(edge_runtime["latency_samples"]),
		path=f"{path}.reports.edge_fixed_timeline_score", require_complete_tail=True,
		maximum_limit_samples=FRAME_SAMPLES, expected_alignment="fixed",
		maximum_observed_edge_samples=FRAME_SAMPLES,
	)
	route = _fixed_timeline_measurement(
		documents["route_fixed_timeline_score"], reference_sha256=clean_sha,
		received_sha256=candidate["capture_sha256"],
		declared_latency_samples=int(candidate["latency_samples"]), path=f"{path}.reports.route_fixed_timeline_score",
		require_complete_tail=require_route_tail, exact_limit_samples=route_budget,
		expected_alignment="fixed-paired-original-route",
		maximum_observed_edge_samples=FRAME_SAMPLES,
	)
	control_score = _fixed_timeline_measurement(
		documents["control_fixed_timeline_score"], reference_sha256=clean_sha,
		received_sha256=control["capture_sha256"], declared_latency_samples=0,
		path=f"{path}.reports.control_fixed_timeline_score", require_complete_tail=require_route_tail,
		exact_limit_samples=route_budget, expected_alignment="fixed",
	)
	control_pre_opus = _fixed_timeline_measurement(
		documents["control_pre_opus_fixed_timeline_score"], reference_sha256=clean_sha,
		received_sha256=control["sender_pre_opus_sha256"], declared_latency_samples=0,
		path=f"{path}.reports.control_pre_opus_fixed_timeline_score", require_complete_tail=True,
		maximum_limit_samples=FRAME_SAMPLES, expected_alignment="fixed",
		maximum_observed_edge_samples=FRAME_SAMPLES,
	)
	_expect(score["inputs"]["noisy_original"]["sha256"] == original["capture_sha256"], f"{path}.objective_score.inputs.noisy_original", "does not bind the Original E2E capture")
	_expect(score["inputs"]["candidate"]["sha256"] == candidate["capture_sha256"], f"{path}.objective_score.inputs.candidate", "does not bind the candidate E2E capture")
	binding = _mapping(score["alignment"]["qualified_route_binding"], f"{path}.objective_score.alignment.qualified_route_binding")
	_expect(binding["e2e_manifest"]["sha256"] == references["e2e_manifest"]["sha256"], f"{path}.objective_score.alignment.qualified_route_binding.e2e_manifest", "manifest hash mismatch")
	_expect(binding["candidate_fixed_timeline_score"]["sha256"] == references["route_fixed_timeline_score"]["sha256"], f"{path}.objective_score.alignment.qualified_route_binding.candidate_fixed_timeline_score", "route-score hash mismatch")
	_expect(binding["control_fixed_timeline_score"]["sha256"] == references["control_fixed_timeline_score"]["sha256"], f"{path}.objective_score.alignment.qualified_route_binding.control_fixed_timeline_score", "control-score hash mismatch")
	counters = _runtime_counters()
	seen_runtime_hashes: set[str] = set()
	for report_name, runtime in (("candidate_adapter_result", candidate), ("edge_adapter_result", edge_runtime)):
		digest = str(references[report_name]["sha256"])
		if digest not in seen_runtime_hashes:
			_merge_counters(counters, runtime["counters"])
			seen_runtime_hashes.add(digest)
	counters["new_clipping_cases"] += int(edge["new_clipping_cases"] or route["new_clipping_cases"] or control_score["new_clipping_cases"] or control_pre_opus["new_clipping_cases"])
	counters["tail_drain_failures"] += int(edge["tail_drain_failures"] or route["tail_drain_failures"] or control_score["tail_drain_failures"] or control_pre_opus["tail_drain_failures"])
	counters["latency_attestation_failures"] += int(score["alignment"]["candidate_latency_samples"] != candidate["latency_samples"])
	return {
		"clean_reference_sha256": clean_sha,
		"source_input_sha256": source_sha,
		"plan_case_sha256": qualification_binding["plan_case_sha256"],
		"render_entry_sha256": qualification_binding["render_entry_sha256"],
		"algorithmic_latency_ms": int(candidate["latency_samples"]) * 1000.0 / SAMPLE_RATE_HZ,
		"speech_edge_loss_ms": max(edge["speech_edge_loss_ms"], route["speech_edge_loss_ms"]),
		"counters": counters,
		"performance": {
			"audio_duration_seconds": candidate["audio_duration_seconds"],
			"processing_duration_seconds": candidate["processing_duration_seconds"],
			"callback_durations_ms": [candidate["callback_p99_ms"]],
			"worker_durations_ms": [candidate["worker_p99_ms"]],
			"max_internal_processing_ms": max(candidate["callback_p99_ms"], candidate["worker_p99_ms"]),
			"memory_growth_bytes": 0,
			"soak_duration_seconds": 0,
		},
	}


def _compare_case_measurement(case: Mapping[str, Any], derived: Mapping[str, Any], path: str) -> Mapping[str, Any]:
	metrics = _mapping(case["metrics"], f"{path}.metrics")
	_same_number(metrics["algorithmic_latency_ms"], derived["algorithmic_latency_ms"], f"{path}.metrics.algorithmic_latency_ms")
	_same_number(metrics["speech_edge_loss_ms"], derived["speech_edge_loss_ms"], f"{path}.metrics.speech_edge_loss_ms")
	_expect(case["counters"] == derived["counters"], f"{path}.counters", f"does not match runtime reports ({derived['counters']!r})")
	performance = _mapping(case["performance"], f"{path}.performance")
	_expect(set(performance) == set(derived["performance"]), f"{path}.performance", "field set does not match derived runtime performance")
	for key, expected in derived["performance"].items():
		actual = performance[key]
		if isinstance(expected, list):
			_expect(isinstance(actual, list) and len(actual) == len(expected), f"{path}.performance.{key}", "sample count mismatch")
			for index, value in enumerate(expected):
				_same_number(actual[index], value, f"{path}.performance.{key}[{index}]")
		elif isinstance(expected, int):
			_expect(actual == expected, f"{path}.performance.{key}", f"does not match runtime report ({expected!r})")
		else:
			_same_number(actual, expected, f"{path}.performance.{key}")
	return case


def _apply_soak_reports(
	root: Path, prefix: str, entries_value: Any, suite: str, scope: str, build: Mapping[str, Any],
	profile_bindings: Mapping[str, Sequence[Mapping[str, Any]]],
	derived_by_key: MutableMapping[tuple[str, str], MutableMapping[str, Any]],
	all_references: MutableMapping[str, Mapping[str, Any]],
) -> None:
	entries = entries_value
	_expect(isinstance(entries, list), "measurement index.soak_reports", "expected an array")
	required_profiles = (
		("Balanced", "Quality", "VoiceFocus") if scope == "core" else ("Auto",)
	) if suite == "nightly" else ()
	profiles = []
	for index, entry_value in enumerate(entries):
		path = f"measurement index.soak_reports[{index}]"
		entry = _mapping(entry_value, path)
		_exact_keys(entry, {"profile", "report"}, path)
		profile = entry["profile"]
		_expect(profile in PROFILES_BY_SCOPE[scope], f"{path}.profile", "outside qualification scope")
		profiles.append(profile)
		reference = _reference(entry["report"], f"{path}.report", prefix)
		previous = all_references.get(str(reference["path"]))
		if previous is None:
			all_references[str(reference["path"])] = reference
		else:
			_same_reference(reference, previous, f"{path}.report")
		report = _load_reference_json(root, reference, f"{path}.report")
		required = {
			"active_bindings", "audio_duration_seconds", "callback_p99_ms", "deadline_miss_count",
			"declared_latency_samples",
			"execution_identity", "fallback_count", "invalid_output_count", "kind",
			"maximum_internal_processing_ms", "mean_rtf", "memory_growth_bytes_after_warmup",
			"new_clipping_count", "profile", "schema_version", "status", "tail_drain_failure_count",
			"rss_end_bytes", "rss_peak_bytes", "rss_warmup_bytes", "wall_duration_seconds", "worker_p99_ms",
		}
		_exact_keys(report, required, f"{path}.report")
		_expect(report["schema_version"] == 2 and report["kind"] == SOAK_KIND and report["status"] == "completed", f"{path}.report", "unsupported soak report")
		_expect(report["profile"] == profile, f"{path}.report.profile", "profile mismatch")
		active_bindings = report["active_bindings"]
		_expect(isinstance(active_bindings, list) and active_bindings == list(profile_bindings[profile]), f"{path}.report.active_bindings", "must equal the complete authorized profile binding set in canonical order")
		_validate_execution_identity(report["execution_identity"], build, f"{path}.report.execution_identity")
		audio_duration = _number(report["audio_duration_seconds"], f"{path}.report.audio_duration_seconds", 1)
		wall_duration = _number(report["wall_duration_seconds"], f"{path}.report.wall_duration_seconds", 1)
		_expect(
			wall_duration >= audio_duration,
			f"{path}.report.wall_duration_seconds",
			"must be at least the declared audio duration for a realtime soak",
		)
		rss_warmup = _integer(report["rss_warmup_bytes"], f"{path}.report.rss_warmup_bytes", 0)
		rss_end = _integer(report["rss_end_bytes"], f"{path}.report.rss_end_bytes", 0)
		rss_peak = _integer(report["rss_peak_bytes"], f"{path}.report.rss_peak_bytes", 0)
		memory_growth = _integer(
			report["memory_growth_bytes_after_warmup"],
			f"{path}.report.memory_growth_bytes_after_warmup",
		)
		_expect(rss_peak >= max(rss_warmup, rss_end), f"{path}.report.rss_peak_bytes", "must cover warmup and end RSS")
		_expect(
			memory_growth == rss_end - rss_warmup,
			f"{path}.report.memory_growth_bytes_after_warmup",
			"must equal rss_end_bytes - rss_warmup_bytes",
		)
		mean_rtf = _number(report["mean_rtf"], f"{path}.report.mean_rtf", 0)
		callback = _number(report["callback_p99_ms"], f"{path}.report.callback_p99_ms", 0)
		worker = _number(report["worker_p99_ms"], f"{path}.report.worker_p99_ms", 0)
		maximum = _number(report["maximum_internal_processing_ms"], f"{path}.report.maximum_internal_processing_ms", 0)
		profile_keys = sorted(key for key in derived_by_key if key[0] == profile)
		_expect(bool(profile_keys), path, "profile has no measured cases")
		target = derived_by_key[profile_keys[0]]
		target["performance"]["audio_duration_seconds"] += audio_duration
		target["performance"]["processing_duration_seconds"] += audio_duration * mean_rtf
		target["performance"]["callback_durations_ms"].append(callback)
		target["performance"]["worker_durations_ms"].append(worker)
		target["performance"]["max_internal_processing_ms"] = max(target["performance"]["max_internal_processing_ms"], maximum)
		target["performance"]["memory_growth_bytes"] = memory_growth
		# Qualification is earned by audio actually processed, never by time the
		# process merely remained alive. Wall time is retained only to prove that
		# the audio duration was not produced faster than realtime.
		target["performance"]["soak_duration_seconds"] = math.floor(audio_duration)
		target["counters"]["deadline_misses"] += _integer(report["deadline_miss_count"], f"{path}.report.deadline_miss_count", 0)
		target["counters"]["unexplained_fallbacks"] += _integer(report["fallback_count"], f"{path}.report.fallback_count", 0)
		target["counters"]["nan_or_inf_count"] += _integer(report["invalid_output_count"], f"{path}.report.invalid_output_count", 0)
		target["counters"]["new_clipping_cases"] += _integer(report["new_clipping_count"], f"{path}.report.new_clipping_count", 0)
		target["counters"]["tail_drain_failures"] += _integer(report["tail_drain_failure_count"], f"{path}.report.tail_drain_failure_count", 0)
		expected_latency = round(float(target["algorithmic_latency_ms"]) * SAMPLE_RATE_HZ / 1000.0)
		target["counters"]["latency_attestation_failures"] += int(_integer(report["declared_latency_samples"], f"{path}.report.declared_latency_samples", 0) != expected_latency)
	_expect(profiles == list(required_profiles), "measurement index.soak_reports", f"must contain {list(required_profiles)!r} in profile order")


def _derive_transitions(
	root: Path, prefix: str, entries_value: Any, transitions: Sequence[Mapping[str, Any]], scope: str,
	build: Mapping[str, Any], all_references: MutableMapping[str, Mapping[str, Any]],
) -> list[Mapping[str, Any]]:
	entries = entries_value
	_expect(isinstance(entries, list), "measurement index.transitions", "expected an array")
	if scope == "core":
		_expect(not entries and not transitions, "measurement index.transitions", "core qualification must not contain transitions")
		return []
	registry = {(str(item["directed_pair"]), str(item["case_id"])): item for item in transitions}
	_expect(len(entries) == len(registry), "measurement index.transitions", "must cover every transition exactly once")
	derived: list[Mapping[str, Any]] = []
	for index, entry_value in enumerate(entries):
		path = f"measurement index.transitions[{index}]"
		entry = _mapping(entry_value, path)
		_exact_keys(entry, {"case_id", "directed_pair", "report"}, path)
		key = (str(entry["directed_pair"]), str(entry["case_id"]))
		_expect(key in registry, path, "does not identify a case-evidence transition")
		reference = _reference(entry["report"], f"{path}.report", prefix)
		previous = all_references.get(str(reference["path"]))
		if previous is None:
			all_references[str(reference["path"])] = reference
		else:
			_same_reference(reference, previous, f"{path}.report")
		report = _load_reference_json(root, reference, f"{path}.report")
		required = {
			"case_id", "deadline_miss_count", "directed_pair", "execution_identity", "fallback_count",
			"fixed_timeline", "invalid_output_count", "kind", "memory_growth_bytes_after_warmup",
			"new_clipping_count", "receiver_cleanup", "schema_version", "soak_duration_seconds",
			"speech_edge_loss_samples", "startup_preroll_ms", "status", "transition_processing_ms",
		}
		_exact_keys(report, required, f"{path}.report")
		_expect(report["schema_version"] == 1 and report["kind"] == TRANSITION_KIND and report["status"] == "completed", f"{path}.report", "unsupported transition report")
		_expect(report["case_id"] == key[1] and report["directed_pair"] == key[0], f"{path}.report", "case binding mismatch")
		_validate_execution_identity(report["execution_identity"], build, f"{path}.report.execution_identity")
		record = registry[key]
		derived_record = copy.deepcopy(record)
		derived_record["fixed_timeline"] = report["fixed_timeline"]
		derived_record["receiver_cleanup_enabled"] = report["receiver_cleanup"]
		derived_record["startup_preroll_ms"] = report["startup_preroll_ms"]
		derived_record["metrics"] = {
			"speech_edge_loss_ms": _integer(report["speech_edge_loss_samples"], f"{path}.report.speech_edge_loss_samples", 0) * 1000.0 / SAMPLE_RATE_HZ,
			"transition_processing_ms": _number(report["transition_processing_ms"], f"{path}.report.transition_processing_ms", 0),
		}
		derived_record["counters"] = {
			"deadline_misses": _integer(report["deadline_miss_count"], f"{path}.report.deadline_miss_count", 0),
			"nan_or_inf_count": _integer(report["invalid_output_count"], f"{path}.report.invalid_output_count", 0),
			"new_clipping_cases": _integer(report["new_clipping_count"], f"{path}.report.new_clipping_count", 0),
			"unexplained_fallbacks": _integer(report["fallback_count"], f"{path}.report.fallback_count", 0),
		}
		derived_record["performance"] = {
			"memory_growth_bytes": _integer(report["memory_growth_bytes_after_warmup"], f"{path}.report.memory_growth_bytes_after_warmup"),
			"soak_duration_seconds": _integer(report["soak_duration_seconds"], f"{path}.report.soak_duration_seconds", 0),
		}
		for field in ("fixed_timeline", "receiver_cleanup_enabled", "startup_preroll_ms", "metrics", "counters", "performance"):
			_expect(record[field] == derived_record[field], f"transition {key}.{field}", "does not match transition runtime report")
		derived.append(record)
	expected_order = sorted(derived, key=lambda item: (AUTO_DIRECTED_PAIRS.index(str(item["directed_pair"])), str(item["case_id"])))
	_expect(derived == expected_order, "measurement index.transitions", "must use canonical directed-pair/case order")
	return derived


def load_and_verify_measurement_index(
	index_path: Path,
	artifact_root: Path,
	build: Mapping[str, Any],
	qualification_scope: str,
	suite: str,
	qualification_binding_sha256: str,
	qualification_artifacts: Mapping[str, Any],
	cases: Sequence[Mapping[str, Any]],
	transitions: Sequence[Mapping[str, Any]],
	objective_scores: Sequence[Mapping[str, Any]],
) -> tuple[list[Mapping[str, Any]], list[Mapping[str, Any]], Mapping[str, Any]]:
	"""Verify the complete measurement graph and return report-derived records."""

	try:
		raw = index_path.read_bytes()
	except OSError as error:
		raise MeasurementEvidenceError(f"unable to read measurement index {index_path}: {error}") from error
	index = _load_json_bytes(raw, "measurement index")
	_expect(raw == canonical_json_bytes(index) + b"\n", "measurement index", "must be canonical sorted-key UTF-8 JSON with one LF")
	root_keys = {
		"build", "cases", "kind", "metrics_runtime_attestation", "objective_runtime_binding_sha256", "plan_binding",
		"profile_bindings", "published_artifacts", "qualification_binding_sha256", "qualification_scope", "schema_version",
		"release_holdout_approval_public_key_sha256", "release_holdout_openings", "soak_reports", "suite", "transitions",
	}
	_exact_keys(index, root_keys, "measurement index")
	_expect(index["schema_version"] == SCHEMA_VERSION and index["kind"] == INDEX_KIND, "measurement index", "unsupported schema/kind")
	_expect(index["qualification_scope"] == qualification_scope and index["suite"] == suite, "measurement index", "scope/suite mismatch")
	_expect(index["qualification_binding_sha256"] == qualification_binding_sha256, "measurement index.qualification_binding_sha256", "does not bind the exact qualification")
	_expect(index["build"] == build, "measurement index.build", "does not exactly match qualification.build")
	prefix = f"artifacts/{suite}-{build['runner_class']}/"
	plan_binding = _mapping(index["plan_binding"], "measurement index.plan_binding")
	plan_fields = {"case_set_sha256", "corpus_inventory_sha256", "corpus_lock_sha256", "mixture_plan_sha256"}
	_exact_keys(plan_binding, plan_fields, "measurement index.plan_binding")
	for field in plan_fields:
		_expect(plan_binding[field] == build[field], f"measurement index.plan_binding.{field}", f"does not match qualification.build.{field}")
	profile_bindings = _validate_profile_bindings(index["profile_bindings"], qualification_scope, build)
	_expect(len(objective_scores) == len(cases), "objective scores", "must cover every case")
	runtime_bindings = {
		canonical_json_sha256({"runtime": score["runtime"], "scorer_files": score["scorer_files"]})
		for score in objective_scores
	}
	_expect(len(runtime_bindings) == 1, "objective scores", "cases do not share one exact metrics runtime/scorer binding")
	objective_runtime_binding_sha256 = next(iter(runtime_bindings))
	_expect(index["objective_runtime_binding_sha256"] == objective_runtime_binding_sha256, "measurement index.objective_runtime_binding_sha256", "does not bind the scorer/runtime used by every objective score")

	all_references: dict[str, Mapping[str, Any]] = {}
	_verify_metrics_runtime_attestation(
		artifact_root, prefix, index["metrics_runtime_attestation"], build,
		objective_runtime_binding_sha256, all_references,
	)
	_verify_release_holdout_openings(
		artifact_root, prefix, index["release_holdout_openings"], suite, build,
		index["release_holdout_approval_public_key_sha256"],
		cases, objective_scores, all_references,
	)
	published = index["published_artifacts"]
	_expect(isinstance(published, list), "measurement index.published_artifacts", "expected an array")
	expected_names = sorted(name for name in qualification_artifacts if name != "measurement_index_json")
	actual_names = []
	for position, item_value in enumerate(published):
		path = f"measurement index.published_artifacts[{position}]"
		item = _mapping(item_value, path)
		_exact_keys(item, {"artifact", "name"}, path)
		name = item["name"]
		_expect(isinstance(name, str), f"{path}.name", "expected a string")
		actual_names.append(name)
		artifact = _mapping(item["artifact"], f"{path}.artifact")
		_expect(name in qualification_artifacts, f"{path}.name", "unknown qualification artifact")
		_same_reference(artifact, qualification_artifacts[name], f"{path}.artifact")
		reference = _reference(artifact, f"{path}.artifact", prefix, PurePosixPath(str(artifact["path"])).suffix.lower())
		all_references[str(reference["path"])] = reference
	_expect(actual_names == expected_names, "measurement index.published_artifacts", "must list every non-index qualification artifact once in name order")

	case_entries = index["cases"]
	_expect(isinstance(case_entries, list) and len(case_entries) == len(cases), "measurement index.cases", "must cover every case exactly once")
	case_registry = {(str(case["profile"]), str(case["case_id"])): case for case in cases}
	score_registry = {(str(score["profile"]), str(score["case_id"])): score for score in objective_scores}
	derived_by_key: dict[tuple[str, str], MutableMapping[str, Any]] = {}
	seen_keys: set[tuple[str, str]] = set()
	for position, entry_value in enumerate(case_entries):
		path = f"measurement index.cases[{position}]"
		entry = _mapping(entry_value, path)
		entry_keys = {
			"case_id", "clean_reference_sha256", "condition", "dataset_split", "measurement_mode",
			"plan_case_sha256", "profile", "render_entry_sha256", "reports", "source_input_sha256",
		}
		_exact_keys(entry, entry_keys, path)
		key = (str(entry["profile"]), str(entry["case_id"]))
		_expect(key in case_registry and key not in seen_keys, path, "does not identify one unique case-evidence record")
		seen_keys.add(key)
		case = case_registry[key]
		score = score_registry.get(key)
		_expect(score is not None, path, "objective score is missing")
		for field in ("condition", "dataset_split"):
			_expect(entry[field] == case[field], f"{path}.{field}", "does not match case evidence")
		for field in ("plan_case_sha256", "render_entry_sha256", "source_input_sha256", "clean_reference_sha256"):
			_hash(entry[field], f"{path}.{field}")
		mode = entry["measurement_mode"]
		_expect(mode in ("offline", "e2e"), f"{path}.measurement_mode", "must be offline or e2e")
		if suite in ("master_quality", "nightly", "release"):
			_expect(mode == "e2e", f"{path}.measurement_mode", "this suite requires the real two-client route")
		expected_report_keys = E2E_REPORT_KEYS if mode == "e2e" else OFFLINE_REPORT_KEYS
		references, documents = _report_registry(artifact_root, prefix, entry["reports"], expected_report_keys, f"{path}.reports", all_references)
		for field in ("path", "sha256", "size_bytes"):
			_expect(references["objective_score"][field] == case["objective_score"][field], f"{path}.reports.objective_score.{field}", "does not identify the case objective score")
		derived = (
			_derive_e2e_case(case, score, references, documents, build, profile_bindings, path)
			if mode == "e2e" else _derive_offline_case(case, score, documents, build, profile_bindings, path)
		)
		_expect(entry["source_input_sha256"] == derived["source_input_sha256"], f"{path}.source_input_sha256", "does not bind the measured source input")
		_expect(entry["clean_reference_sha256"] == derived["clean_reference_sha256"], f"{path}.clean_reference_sha256", "does not bind the measured clean reference")
		_expect(entry["plan_case_sha256"] == derived["plan_case_sha256"], f"{path}.plan_case_sha256", "does not bind the measured plan case")
		_expect(entry["render_entry_sha256"] == derived["render_entry_sha256"], f"{path}.render_entry_sha256", "does not bind the measured render entry")
		derived_by_key[key] = copy.deepcopy(derived)
	expected_order = sorted(seen_keys, key=lambda key: (PROFILES_BY_SCOPE[qualification_scope].index(key[0]), key[1]))
	_expect([(str(entry["profile"]), str(entry["case_id"])) for entry in case_entries] == expected_order, "measurement index.cases", "must use canonical profile/case order")

	_apply_soak_reports(artifact_root, prefix, index["soak_reports"], suite, qualification_scope, build, profile_bindings, derived_by_key, all_references)
	derived_cases = []
	for key in expected_order:
		case = case_registry[key]
		_compare_case_measurement(case, derived_by_key[key], f"case {key[0]}/{key[1]}")
		derived_cases.append(case)
	derived_transitions = _derive_transitions(
		artifact_root, prefix, index["transitions"], transitions, qualification_scope, build, all_references,
	)
	index_reference = qualification_artifacts["measurement_index_json"]
	_expect(index_reference["path"] == index_path.resolve().relative_to(artifact_root.resolve()).as_posix(), "qualification.artifacts.measurement_index_json.path", "does not identify the loaded index")
	return derived_cases, derived_transitions, {"index": index, "artifact_references": all_references}


def indexed_artifact_references(index_value: Any, prefix: str) -> Mapping[str, Mapping[str, Any]]:
	"""Return the exact transitive, audio-free file allowlist from an index.

	This deliberately performs structural path checks without loading report
	contents; the semantic validator must already have verified the graph.
	"""

	index = _mapping(index_value, "measurement index")
	_exact_keys(index, {
		"build", "cases", "kind", "metrics_runtime_attestation", "objective_runtime_binding_sha256", "plan_binding", "published_artifacts",
		"profile_bindings", "qualification_binding_sha256", "qualification_scope", "release_holdout_approval_public_key_sha256", "release_holdout_openings", "schema_version", "soak_reports", "suite", "transitions",
	}, "measurement index")
	references: dict[str, Mapping[str, Any]] = {}

	def add(value: Any, path: str, suffix: str | None = None) -> None:
		reference = _reference(value, path, prefix, suffix or ".json")
		previous = references.get(str(reference["path"]))
		if previous is None:
			references[str(reference["path"])] = reference
		else:
			_same_reference(reference, previous, path)

	for index_position, item_value in enumerate(index["published_artifacts"]):
		item = _mapping(item_value, f"measurement index.published_artifacts[{index_position}]")
		_exact_keys(item, {"artifact", "name"}, f"measurement index.published_artifacts[{index_position}]")
		artifact = _mapping(item["artifact"], f"measurement index.published_artifacts[{index_position}].artifact")
		add(artifact, f"measurement index.published_artifacts[{index_position}].artifact", PurePosixPath(str(artifact.get("path", ""))).suffix.lower())
	add(index["metrics_runtime_attestation"], "measurement index.metrics_runtime_attestation")
	for opening_position, opening_value in enumerate(index["release_holdout_openings"]):
		opening = _mapping(opening_value, f"measurement index.release_holdout_openings[{opening_position}]")
		for name in ("approval_public_key", "attestation", "detached_signature", "opening_report", "receipt"):
			reference = _mapping(opening.get(name), f"measurement index.release_holdout_openings[{opening_position}].{name}")
			add(reference, f"measurement index.release_holdout_openings[{opening_position}].{name}", PurePosixPath(str(reference.get("path", ""))).suffix.lower())
	for case_position, case_value in enumerate(index["cases"]):
		case = _mapping(case_value, f"measurement index.cases[{case_position}]")
		reports = _mapping(case.get("reports"), f"measurement index.cases[{case_position}].reports")
		for name, reference in reports.items():
			add(reference, f"measurement index.cases[{case_position}].reports.{name}")
	for soak_position, soak_value in enumerate(index["soak_reports"]):
		soak = _mapping(soak_value, f"measurement index.soak_reports[{soak_position}]")
		add(soak.get("report"), f"measurement index.soak_reports[{soak_position}].report")
	for transition_position, transition_value in enumerate(index["transitions"]):
		transition = _mapping(transition_value, f"measurement index.transitions[{transition_position}]")
		add(transition.get("report"), f"measurement index.transitions[{transition_position}].report")
	return references


def run_self_test() -> None:
	with tempfile.TemporaryDirectory(prefix="mumble-measurement-evidence-") as directory:
		root = Path(directory)
		value = {
			"schema_version": 1,
			"kind": INDEX_KIND,
			"qualification_scope": "core",
			"suite": "pr_smoke",
			"qualification_binding_sha256": "1" * 64,
			"build": {},
			"plan_binding": {},
			"metrics_runtime_attestation": {
				"contains_audio_samples": False, "path": "artifacts/pr_smoke-local-development/metrics-runtime-attestation.json",
				"sha256": "3" * 64, "size_bytes": 1,
			},
			"objective_runtime_binding_sha256": "2" * 64,
			"profile_bindings": [],
			"published_artifacts": [],
			"cases": [],
			"release_holdout_approval_public_key_sha256": None,
			"release_holdout_openings": [],
			"soak_reports": [],
			"transitions": [],
		}
		path = root / "index.json"
		path.write_bytes(canonical_json_bytes(value) + b"\n")
		loaded = _load_json_bytes(path.read_bytes(), "self-test index")
		if loaded != value:
			raise AssertionError("canonical measurement index did not round-trip")
		unsafe = {"contains_audio_samples": False, "path": "artifacts/x/private.wav", "sha256": "3" * 64, "size_bytes": 1}
		try:
			_reference(unsafe, "self-test audio", "artifacts/x/", ".wav")
		except MeasurementEvidenceError:
			pass
		else:
			raise AssertionError("measurement index accepted an audio artifact")

		prefix = "artifacts/nightly-low-performance/"
		build = {
			"tested_binary_sha256": "4" * 64,
			"model_manifest_sha256": "5" * 64,
			"recipe_manifest_sha256": "6" * 64,
			"staged_payload_sha256": "7" * 64,
			"server_binary_sha256": "8" * 64,
			"model_hashes": ["a" * 64],
			"case_set_sha256": "1" * 64,
			"corpus_inventory_sha256": "2" * 64,
			"corpus_lock_sha256": "3" * 64,
			"mixture_plan_sha256": "4" * 64,
		}
		identity = {
			"client_binary_sha256": build["tested_binary_sha256"],
			"model_manifest_sha256": build["model_manifest_sha256"],
			"recipe_manifest_sha256": build["recipe_manifest_sha256"],
			"run_provenance_sha256": "9" * 64,
			"runtime_payload_sha256": build["staged_payload_sha256"],
			"server_binary_sha256": build["server_binary_sha256"],
		}
		adapter_document = {
			"schema_version": 3,
			"status": "passed",
			"role": "candidate",
			"profile": "Quality",
			"receiver_cleanup": False,
			"input_sha256": "b" * 64,
			"execution_identity": {**identity, "contract_file_sha256": "c" * 64},
			"transport": {"opus_bitrate_bps": 40_000, "frames_per_packet": 2, "transmit_mode": "VAD"},
			"capture": {"relative_path": "capture.wav", "sha256": "d" * 64, "size_bytes": 100},
			"sender_pre_opus": {"relative_path": "sender-pre-opus.wav", "sha256": "e" * 64, "size_bytes": 100},
			"diagnostics": {
				"active_engine": "DeepFilterNet",
				"active_models": [{"id": "deepfilternet:self-test", "sha256": "a" * 64, "version": "1"}],
				"active_profile": "Quality",
				"active_recipe": {
					"catalog_revision": "self-test-v2", "id": "input.quality.self-test",
					"manifest_sha256": build["recipe_manifest_sha256"], "revision": 1,
				},
				"callback_frame_count": 100,
				"callback_p99_ms": 2.0,
				"deadline_miss_count": 0,
				"declared_latency_samples": 2400,
				"fallback_count": 0,
				"invalid_output_count": 0,
				"mean_rtf": 0.1,
				"model_initialization_attempts": 1,
				"tail_drained": True,
				"worker_frame_count": 100,
				"worker_p99_ms": 3.0,
			},
		}
		adapter_profile_bindings = {
			"Quality": [{
				"profile": "Quality", "engine": "DeepFilterNet",
				"recipe": copy.deepcopy(adapter_document["diagnostics"]["active_recipe"]),
				"models": copy.deepcopy(adapter_document["diagnostics"]["active_models"]),
			}],
		}
		adapter = _adapter_measurement(
			adapter_document, role="candidate", profile="Quality", input_sha256="b" * 64,
			build=build, profile_bindings=adapter_profile_bindings, path="self-test adapter",
		)
		if adapter["capture_sha256"] != "d" * 64 or adapter["transport"]["transmit_mode"] != "VAD":
			raise AssertionError("valid private WAV metadata did not survive adapter derivation")
		unsafe_adapter = copy.deepcopy(adapter_document)
		unsafe_adapter["capture"]["relative_path"] = "../capture.wav"
		try:
			_adapter_measurement(
				unsafe_adapter, role="candidate", profile="Quality", input_sha256="b" * 64,
				build=build, profile_bindings=adapter_profile_bindings, path="self-test unsafe adapter",
			)
		except MeasurementEvidenceError:
			pass
		else:
			raise AssertionError("adapter accepted an escaping private WAV path")

		vad_route_score = {
			"schema_version": 3, "scorer": "mumble-fixed-timeline-v3",
			"timeline_alignment": "fixed-paired-original-route",
			"sample_rate_hz": SAMPLE_RATE_HZ, "frame_samples": FRAME_SAMPLES,
			"declared_latency_samples": 2400, "reference_sha256": "f" * 64,
			"received_sha256": "0" * 64, "onset_loss_samples": 0, "end_loss_samples": 0,
			"missing_tail_samples": FRAME_SAMPLES, "reference_clipped_samples": 0,
			"received_clipped_samples": 0,
			"qualification_limits": {
				"max_onset_loss_samples": 2880, "max_end_loss_samples": 2880,
				"require_complete_tail": False, "fail_on_new_clipping": True,
			},
			"passed": True,
		}
		vad_route = _fixed_timeline_measurement(
			vad_route_score, reference_sha256="f" * 64, received_sha256="0" * 64,
			declared_latency_samples=2400, path="self-test VAD route", require_complete_tail=False,
			exact_limit_samples=2880, expected_alignment="fixed-paired-original-route",
			maximum_observed_edge_samples=FRAME_SAMPLES,
		)
		if vad_route["tail_drain_failures"] != 0:
			raise AssertionError("VAD receiver-room-silence truncation was mistaken for causal-tail loss")
		leaky_route_score = copy.deepcopy(vad_route_score)
		leaky_route_score["private_audio_path"] = r"C:\protected-audio\capture.wav"
		try:
			_fixed_timeline_measurement(
				leaky_route_score, reference_sha256="f" * 64, received_sha256="0" * 64,
				declared_latency_samples=2400, path="self-test leaky fixed score", require_complete_tail=False,
				exact_limit_samples=2880, expected_alignment="fixed-paired-original-route",
				maximum_observed_edge_samples=FRAME_SAMPLES,
			)
		except MeasurementEvidenceError:
			pass
		else:
			raise AssertionError("fixed-timeline evidence accepted an unrecognized private path field")

		original_binding = {
			"profile": "Original", "engine": "None",
			"recipe": {
				"catalog_revision": "self-test-v2", "id": "input.original",
				"manifest_sha256": build["recipe_manifest_sha256"], "revision": 1,
			},
			"models": [],
		}
		quality_binding = adapter_profile_bindings["Quality"][0]
		control_mapping_expectations = {
			("Original", "None"): ((0, 0, 0), (0, 0, 0)),
			("Light", "Speex"): ((0, 50, 100), (0, 50, 100)),
			("Balanced", "RNNoise"): ((20, 38, 55), (10, 50, 90)),
			("Quality", "DeepFilterNet"): ((25, 58, 90), (25, 63, 100)),
			("VoiceFocus", "DeepFilterNet"): ((70, 85, 100), (40, 70, 100)),
			("Auto", "Speex"): ((0, 50, 100), (0, 50, 100)),
			("Auto", "RNNoise"): ((20, 38, 55), (10, 50, 90)),
			("Auto", "DeepFilterNet"): ((25, 58, 90), (25, 63, 100)),
		}
		for (mapping_profile, mapping_engine), (noise_expected, character_expected) in control_mapping_expectations.items():
			noise_range, character_range = _validated_control_ranges(mapping_profile, mapping_engine)
			noise_actual = tuple(_map_ui_control_to_recipe(value, noise_range) for value in (0, 50, 100))
			character_actual = tuple(_map_ui_control_to_recipe(value, character_range) for value in (0, 50, 100))
			if noise_actual != noise_expected or character_actual != character_expected:
				raise AssertionError(f"control mapping drift for {mapping_profile}/{mapping_engine}")
		benchmark_document = {
			"schema_version": 1, "kind": BENCHMARK_MEASUREMENT_KIND,
			"source_report_sha256": "1" * 64,
			"processing_mode": "product-profile", "requested_profile": "Quality", "active_profile": "Quality",
			"active_engine": "DeepFilterNet", "requested_recipe_id": quality_binding["recipe"]["id"],
			"recipe_revision": quality_binding["recipe"]["revision"],
			"requested_ui_noise_reduction": 10, "requested_ui_natural_clear": 10,
			"validated_recipe_noise_reduction": 32, "validated_recipe_natural_clear": 33,
			"active_model_id": quality_binding["models"][0]["id"],
			"active_model_sha256": quality_binding["models"][0]["sha256"],
			"sample_rate": SAMPLE_RATE_HZ, "input_sha256": "2" * 64,
			"clean_reference_sha256": "3" * 64, "output_sha256": "4" * 64,
			"reported_latency_samples": 2400, "drain_sample_count": 2400,
			"input_sample_count": SAMPLE_RATE_HZ, "output_sample_count": SAMPLE_RATE_HZ + 2400,
			"sample_count": SAMPLE_RATE_HZ + 2400, "processing_padding_sample_count": 0,
			"input_saturated_sample_count": 0, "saturated_sample_count": 0,
			"used_fallback": False, "fallback_count": 0, "deadline_misses": 0,
			"non_finite_sample_count": 0, "out_of_range_sample_count": 0,
			"audio_ms": 1000.0, "processing_wall_ms": 100.0, "rtf": 0.1,
			"callback_p99_ms": 2.0, "worker_processing_p99_ms": 3.0,
			"maximum_processing_ms": 4.0,
		}
		_benchmark_measurement(
			benchmark_document, profile="Quality", build=build,
			profile_bindings={"Quality": [quality_binding]}, path="self-test benchmark controls",
		)
		bad_controls = copy.deepcopy(benchmark_document)
		bad_controls["validated_recipe_noise_reduction"] = 33
		try:
			_benchmark_measurement(
				bad_controls, profile="Quality", build=build,
				profile_bindings={"Quality": [quality_binding]}, path="self-test tampered benchmark controls",
			)
		except MeasurementEvidenceError:
			pass
		else:
			raise AssertionError("benchmark evidence accepted a validated control not derived from the UI request")
		e2e_bindings = {"Original": [original_binding], "Quality": [quality_binding]}
		clean_sha256 = "f" * 64
		source_sha256 = "b" * 64

		def adapter_result(
			role: str, profile: str, input_sha256: str, capture_sha256: str,
			sender_sha256: str, latency_samples: int, binding: Mapping[str, Any], contract_digit: str,
		) -> Mapping[str, Any]:
			return {
				"schema_version": 3, "status": "passed", "role": role, "profile": profile,
				"receiver_cleanup": False, "input_sha256": input_sha256,
				"execution_identity": {**identity, "contract_file_sha256": contract_digit * 64},
				"transport": {"opus_bitrate_bps": 40_000, "frames_per_packet": 2, "transmit_mode": "VAD"},
				"capture": {"relative_path": "capture.wav", "sha256": capture_sha256, "size_bytes": 100},
				"sender_pre_opus": {"relative_path": "sender-pre-opus.wav", "sha256": sender_sha256, "size_bytes": 100},
				"diagnostics": {
					"active_engine": binding["engine"], "active_models": copy.deepcopy(binding["models"]),
					"active_profile": profile, "active_recipe": copy.deepcopy(binding["recipe"]),
					"callback_frame_count": 100, "callback_p99_ms": 2.0,
					"deadline_miss_count": 0, "declared_latency_samples": latency_samples,
					"fallback_count": 0, "invalid_output_count": 0, "mean_rtf": 0.1,
					"model_initialization_attempts": 1 if binding["models"] else 0,
					"tail_drained": True,
					"worker_frame_count": 100 if binding["engine"] == "DeepFilterNet" else 0,
					"worker_p99_ms": 3.0 if binding["engine"] == "DeepFilterNet" else 0.0,
				},
			}

		candidate_document = adapter_result("candidate", "Quality", source_sha256, "d" * 64, "e" * 64, 2400, quality_binding, "1")
		edge_document = adapter_result("candidate_edge", "Quality", clean_sha256, "7" * 64, "6" * 64, 2400, quality_binding, "2")
		original_document = adapter_result("original_comparison", "Original", source_sha256, "5" * 64, "4" * 64, 0, original_binding, "3")
		control_document = adapter_result("control", "Original", clean_sha256, "3" * 64, "2" * 64, 0, original_binding, "4")
		reference_hashes = {
			"candidate_adapter_result": "1" * 64, "edge_adapter_result": "2" * 64,
			"original_adapter_result": "3" * 64, "control_adapter_result": "4" * 64,
			"e2e_manifest": "5" * 64, "edge_fixed_timeline_score": "6" * 64,
			"route_fixed_timeline_score": "7" * 64, "control_fixed_timeline_score": "8" * 64,
			"control_pre_opus_fixed_timeline_score": "a" * 64, "objective_score": "9" * 64,
		}
		references = {
			name: {"contains_audio_samples": False, "path": f"artifacts/release-low-performance/measurements/{name}.json", "sha256": digest, "size_bytes": 100}
			for name, digest in reference_hashes.items()
		}

		def manifest_result(role: str, report: Mapping[str, Any]) -> Mapping[str, Any]:
			diagnostics = report["diagnostics"]
			return {
				"adapter_contract_sha256": report["execution_identity"]["contract_file_sha256"],
				"adapter_result_sha256": references[{
					"candidate": "candidate_adapter_result", "candidate_edge": "edge_adapter_result",
					"original_comparison": "original_adapter_result", "control": "control_adapter_result",
				}[role]]["sha256"],
				"capture_sha256": report["capture"]["sha256"],
				"sender_pre_opus_sha256": report["sender_pre_opus"]["sha256"],
				"execution_identity": copy.deepcopy(report["execution_identity"]),
				"active_recipe": copy.deepcopy(diagnostics["active_recipe"]),
				"active_models": copy.deepcopy(diagnostics["active_models"]),
				"performance": {
					"callback_frame_count": diagnostics["callback_frame_count"],
					"callback_p99_ms": diagnostics["callback_p99_ms"],
					"model_initialization_attempts": diagnostics["model_initialization_attempts"],
					"worker_frame_count": diagnostics["worker_frame_count"],
					"worker_p99_ms": diagnostics["worker_p99_ms"], "mean_rtf": diagnostics["mean_rtf"],
				},
				"fixed_timeline_score_sha256": (
					references["route_fixed_timeline_score"]["sha256"] if role == "candidate"
					else references["control_fixed_timeline_score"]["sha256"] if role == "control" else None
				),
				"pre_opus_fixed_timeline_score_sha256": (
					references["edge_fixed_timeline_score"]["sha256"] if role == "candidate_edge"
					else references["control_pre_opus_fixed_timeline_score"]["sha256"] if role == "control" else None
				),
				"qualification_purpose": {
					"candidate": "noisy-enhanced-candidate", "candidate_edge": "clean-enhanced-input-edge-probe",
					"original_comparison": "noisy-original-quality-comparison", "control": "clean-original-route-control",
				}[role],
			}

		e2e_manifest = {
			"schema_version": 3, "status": "passed", "case_id": "case-e2e", "profile": "Quality",
			"run_provenance_sha256": identity["run_provenance_sha256"], "receiver_cleanup": False,
			"qualification_binding": {
				"mixture_plan_sha256": build["mixture_plan_sha256"], "case_set_sha256": build["case_set_sha256"],
				"corpus_inventory_sha256": build["corpus_inventory_sha256"], "corpus_lock_sha256": build["corpus_lock_sha256"],
				"case_id": "case-e2e", "profile": "Quality", "dataset_split": "validation",
				"plan_case_sha256": "a" * 64, "render_manifest_sha256": "b" * 64,
				"render_entry_sha256": "c" * 64, "source_input_sha256": source_sha256,
				"clean_reference_sha256": clean_sha256,
				"input_enhancement_policy_manifest_sha256": "d" * 64,
				"input_enhancement_policy_signature_sha256": "e" * 64,
			},
			"input_timeline_gate": {
				"artifact": "sender_pre_opus", "alignment": "fixed-declared-latency",
				"roles": ["control", "candidate_edge"], "max_onset_loss_samples": FRAME_SAMPLES,
				"max_end_loss_samples": FRAME_SAMPLES, "complete_tail_required": True,
			},
			"route_control": {
				"onset_budget_samples": 2880, "end_loss_budget_samples": 2880,
				"receiver_edge_gate": "route-bounded-not-input-latency", "capture_tail_rule": "vad-speech-edge",
				"causal_tail_drain_required": True, "legacy_original_parity_required": True,
			},
			"results": {
				"candidate": manifest_result("candidate", candidate_document),
				"candidate_edge": manifest_result("candidate_edge", edge_document),
				"original_comparison": manifest_result("original_comparison", original_document),
				"control": manifest_result("control", control_document),
			},
			"private_audio_do_not_upload": True,
		}

		def fixed_score(received_sha256: str, latency: int, alignment: str, complete_tail: bool, limit: int) -> Mapping[str, Any]:
			return {
				"schema_version": 3, "scorer": "mumble-fixed-timeline-v3", "timeline_alignment": alignment,
				"sample_rate_hz": SAMPLE_RATE_HZ, "frame_samples": FRAME_SAMPLES,
				"declared_latency_samples": latency, "reference_sha256": clean_sha256,
				"received_sha256": received_sha256, "onset_loss_samples": 0, "end_loss_samples": 0,
				"missing_tail_samples": FRAME_SAMPLES if not complete_tail else 0,
				"reference_clipped_samples": 0, "received_clipped_samples": 0,
				"qualification_limits": {
					"max_onset_loss_samples": limit, "max_end_loss_samples": limit,
					"require_complete_tail": complete_tail, "fail_on_new_clipping": True,
				},
				"passed": True,
			}

		documents = {
			"candidate_adapter_result": candidate_document, "edge_adapter_result": edge_document,
			"original_adapter_result": original_document, "control_adapter_result": control_document,
			"e2e_manifest": e2e_manifest,
			"edge_fixed_timeline_score": fixed_score(edge_document["sender_pre_opus"]["sha256"], 2400, "fixed", True, FRAME_SAMPLES),
			"route_fixed_timeline_score": fixed_score(candidate_document["capture"]["sha256"], 2400, "fixed-paired-original-route", False, 2880),
			"control_fixed_timeline_score": fixed_score(control_document["capture"]["sha256"], 0, "fixed", False, 2880),
			"control_pre_opus_fixed_timeline_score": fixed_score(control_document["sender_pre_opus"]["sha256"], 0, "fixed", True, FRAME_SAMPLES),
		}
		score = {
			"inputs": {
				"clean_reference": {"sha256": clean_sha256},
				"noisy_original": {"sha256": original_document["capture"]["sha256"]},
				"candidate": {"sha256": candidate_document["capture"]["sha256"]},
			},
			"alignment": {
				"candidate_latency_samples": 2400,
				"qualified_route_binding": {
					"e2e_manifest": {"sha256": references["e2e_manifest"]["sha256"]},
					"candidate_fixed_timeline_score": {"sha256": references["route_fixed_timeline_score"]["sha256"]},
					"control_fixed_timeline_score": {"sha256": references["control_fixed_timeline_score"]["sha256"]},
				},
			},
		}
		case = {"case_id": "case-e2e", "profile": "Quality", "dataset_split": "validation"}
		derived_e2e = _derive_e2e_case(case, score, references, documents, build, e2e_bindings, "self-test E2E")
		if derived_e2e["plan_case_sha256"] != "a" * 64 or derived_e2e["render_entry_sha256"] != "c" * 64:
			raise AssertionError("E2E derivation lost the protected plan/render binding")
		bad_route_documents = copy.deepcopy(documents)
		bad_route_documents["route_fixed_timeline_score"]["onset_loss_samples"] = FRAME_SAMPLES + 1
		try:
			_derive_e2e_case(case, score, references, bad_route_documents, build, e2e_bindings, "self-test bad E2E edge")
		except MeasurementEvidenceError:
			pass
		else:
			raise AssertionError("paired E2E route hid more than one frame of candidate speech-edge loss")
		original_candidate_document = adapter_result(
			"candidate", "Original", source_sha256, "8" * 64, "9" * 64, 0, original_binding, "5"
		)
		original_edge_document = adapter_result(
			"candidate_edge", "Original", clean_sha256, "c" * 64, "d" * 64, 0, original_binding, "6"
		)
		original_documents = copy.deepcopy(documents)
		original_documents["candidate_adapter_result"] = original_candidate_document
		original_documents["edge_adapter_result"] = original_edge_document
		original_documents["edge_fixed_timeline_score"] = fixed_score(
			original_edge_document["sender_pre_opus"]["sha256"], 0, "fixed", True, FRAME_SAMPLES
		)
		original_documents["route_fixed_timeline_score"] = fixed_score(
			original_candidate_document["capture"]["sha256"], 0,
			"fixed-paired-original-route", False, 2880,
		)
		original_manifest = copy.deepcopy(e2e_manifest)
		original_manifest["profile"] = "Original"
		original_manifest["qualification_binding"]["profile"] = "Original"
		original_manifest["results"] = {
			"candidate": manifest_result("candidate", original_candidate_document),
			"candidate_edge": manifest_result("candidate_edge", original_edge_document),
			"original_comparison": manifest_result("original_comparison", original_document),
			"control": manifest_result("control", control_document),
		}
		original_documents["e2e_manifest"] = original_manifest
		original_score = copy.deepcopy(score)
		original_score["inputs"]["candidate"]["sha256"] = original_candidate_document["capture"]["sha256"]
		original_score["alignment"]["candidate_latency_samples"] = 0
		original_case = {"case_id": "case-e2e", "profile": "Original", "dataset_split": "validation"}
		derived_original = _derive_e2e_case(
			original_case, original_score, references, original_documents, build, e2e_bindings,
			"self-test explicit Original candidate",
		)
		if derived_original["algorithmic_latency_ms"] != 0.0:
			raise AssertionError("explicit Original candidate did not retain zero enhancement latency")
		legacy_alias_documents = copy.deepcopy(original_documents)
		legacy_alias_documents["e2e_manifest"]["results"].pop("candidate")
		legacy_alias_documents["e2e_manifest"]["results"].pop("candidate_edge")
		legacy_alias_documents["e2e_manifest"]["input_timeline_gate"]["roles"] = ["control"]
		try:
			_derive_e2e_case(
				original_case, original_score, references, legacy_alias_documents, build, e2e_bindings,
				"self-test legacy Original aliases",
			)
		except MeasurementEvidenceError:
			pass
		else:
			raise AssertionError("legacy Original candidate/control aliases remained qualification-eligible")
		auto_soak_bindings = [
			{
				"profile": "Light", "engine": "Speex",
				"recipe": {
					"catalog_revision": "self-test-v2", "id": "input.auto.light.self-test",
					"manifest_sha256": build["recipe_manifest_sha256"], "revision": 1,
				},
				"models": [],
			},
			{
				"profile": "Balanced", "engine": "RNNoise",
				"recipe": {
					"catalog_revision": "self-test-v2", "id": "input.auto.balanced.self-test",
					"manifest_sha256": build["recipe_manifest_sha256"], "revision": 1,
				},
				"models": [{"id": "rnnoise:self-test", "sha256": "a" * 64, "version": "1"}],
			},
			{
				"profile": "Quality", "engine": "DeepFilterNet",
				"recipe": {
					"catalog_revision": "self-test-v2", "id": "input.auto.quality.self-test",
					"manifest_sha256": build["recipe_manifest_sha256"], "revision": 1,
				},
				"models": copy.deepcopy(quality_binding["models"]),
			},
		]
		soak_document = {
			"schema_version": 2,
			"kind": SOAK_KIND,
			"status": "completed",
			"profile": "Auto",
			"active_bindings": auto_soak_bindings,
			"execution_identity": identity,
			"audio_duration_seconds": 3599,
			"wall_duration_seconds": 3599.75,
			"declared_latency_samples": 2400,
			"mean_rtf": 0.1,
			"callback_p99_ms": 4.0,
			"worker_p99_ms": 5.0,
			"maximum_internal_processing_ms": 9.0,
			"memory_growth_bytes_after_warmup": 0,
			"rss_warmup_bytes": 100_000,
			"rss_end_bytes": 100_000,
			"rss_peak_bytes": 110_000,
			"deadline_miss_count": 0,
			"fallback_count": 0,
			"invalid_output_count": 0,
			"new_clipping_count": 0,
			"tail_drain_failure_count": 0,
		}
		soak_path = root.joinpath(*PurePosixPath(prefix + "measurements/auto-soak.json").parts)
		soak_path.parent.mkdir(parents=True)

		def write_soak_report(document: Mapping[str, Any]) -> Mapping[str, Any]:
			payload = canonical_json_bytes(document) + b"\n"
			soak_path.write_bytes(payload)
			return {
				"contains_audio_samples": False,
				"path": soak_path.relative_to(root).as_posix(),
				"sha256": hashlib.sha256(payload).hexdigest(),
				"size_bytes": len(payload),
			}

		soak_reference = write_soak_report(soak_document)
		derived_case: MutableMapping[str, Any] = {
			"algorithmic_latency_ms": 50.0,
			"speech_edge_loss_ms": 0.0,
			"counters": _runtime_counters(),
			"performance": {
				"audio_duration_seconds": 1.0,
				"processing_duration_seconds": 0.1,
				"callback_durations_ms": [1.0],
				"worker_durations_ms": [1.0],
				"max_internal_processing_ms": 1.0,
				"memory_growth_bytes": 0,
				"soak_duration_seconds": 0,
			},
		}
		_apply_soak_reports(
			root,
			prefix,
			[{"profile": "Auto", "report": soak_reference}],
			"nightly",
			"auto",
			build,
			{"Auto": auto_soak_bindings},
			{("Auto", "case-001"): derived_case},
			{},
		)
		accelerated_soak = copy.deepcopy(soak_document)
		accelerated_soak["audio_duration_seconds"] = 3600
		accelerated_soak["wall_duration_seconds"] = 300
		try:
			_apply_soak_reports(
				root, prefix, [{"profile": "Auto", "report": write_soak_report(accelerated_soak)}],
				"nightly", "auto", build, {"Auto": auto_soak_bindings},
				{("Auto", "case-001"): copy.deepcopy(derived_case)}, {},
			)
		except MeasurementEvidenceError:
			pass
		else:
			raise AssertionError("3600 seconds of audio processed in 300 wall seconds was accepted as a realtime soak")
		idle_inflated_soak = copy.deepcopy(soak_document)
		idle_inflated_soak["audio_duration_seconds"] = 1
		idle_inflated_soak["wall_duration_seconds"] = 3600
		idle_target = copy.deepcopy(derived_case)
		_apply_soak_reports(
			root, prefix, [{"profile": "Auto", "report": write_soak_report(idle_inflated_soak)}],
			"nightly", "auto", build, {"Auto": auto_soak_bindings},
			{("Auto", "case-001"): idle_target}, {},
		)
		if idle_target["performance"]["soak_duration_seconds"] != 1:
			raise AssertionError("idle wall time was incorrectly counted as processed soak audio")
		rss_tamper = copy.deepcopy(soak_document)
		rss_tamper["rss_end_bytes"] += 1
		try:
			_apply_soak_reports(
				root, prefix, [{"profile": "Auto", "report": write_soak_report(rss_tamper)}],
				"nightly", "auto", build, {"Auto": auto_soak_bindings},
				{("Auto", "case-001"): copy.deepcopy(derived_case)}, {},
			)
		except MeasurementEvidenceError:
			pass
		else:
			raise AssertionError("RSS growth inconsistent with warmup/end samples was accepted")
		caller_case = {
			"metrics": {"algorithmic_latency_ms": 50.0, "speech_edge_loss_ms": 0.0},
			"counters": copy.deepcopy(derived_case["counters"]),
			"performance": copy.deepcopy(derived_case["performance"]),
		}
		caller_case["performance"]["soak_duration_seconds"] = 3600
		try:
			_compare_case_measurement(caller_case, derived_case, "self-test soak")
		except MeasurementEvidenceError:
			pass
		else:
			raise AssertionError("caller-reported 3600-second soak hid a 3599-second runtime report")

		holdout_prefix = "artifacts/release-low-performance/"
		holdout_root = root / "release-holdout"
		run_id = "12345678-1234-4234-8234-123456789abc"
		build_record = {"sha256": build["tested_binary_sha256"], "size_bytes": 123}
		attestation = {
			"schema_version": 1, "attestation": HOLDOUT_ATTESTATION_KIND,
			"dataset_split": "release-holdout", "purpose": HOLDOUT_PURPOSE, "run_id": run_id,
			"authorized_at_utc": "2026-07-16T10:00:00Z", "valid_until_utc": "2026-07-16T11:00:00Z",
			"receipt_relative_path": f"receipts/{run_id}.json",
			"opening_report_relative_path": f"reports/{run_id}.json",
			"expected": {
				"corpus_inventory_sha256": build["corpus_inventory_sha256"],
				"mixture_plan_sha256": build["mixture_plan_sha256"], "release_build": build_record,
			},
			"authorization": {
				"kind": "detached-ed25519-release-owner-approval-v1", "key_id": "self-test",
				"public_key_sha256": "0" * 64, "signature_encoding": "raw-ed25519-64-byte-file",
			},
		}
		from objective_quality_score import _ed25519_sign_for_self_test
		public_key, _ = _ed25519_sign_for_self_test(b"h" * 32, b"placeholder")
		attestation["authorization"]["public_key_sha256"] = hashlib.sha256(public_key).hexdigest()
		public_key, signature = _ed25519_sign_for_self_test(b"h" * 32, canonical_json_bytes(attestation))

		def write_holdout(relative: str, payload: bytes) -> Mapping[str, Any]:
			actual = holdout_root.joinpath(*PurePosixPath(holdout_prefix + relative).parts)
			actual.parent.mkdir(parents=True, exist_ok=True)
			actual.write_bytes(payload)
			return {
				"contains_audio_samples": False, "path": actual.relative_to(holdout_root).as_posix(),
				"sha256": hashlib.sha256(payload).hexdigest(), "size_bytes": len(payload),
			}

		attestation_reference = write_holdout("holdout/opening.json", canonical_json_bytes(attestation) + b"\n")
		signature_reference = write_holdout("holdout/opening.sig", signature)
		public_key_reference = write_holdout("holdout/release-owner.pub", public_key)
		path_free = lambda reference: {"sha256": reference["sha256"], "size_bytes": reference["size_bytes"]}
		common = {
			"run_id": run_id, "purpose": HOLDOUT_PURPOSE,
			"corpus_inventory_sha256": build["corpus_inventory_sha256"],
			"mixture_plan_sha256": build["mixture_plan_sha256"], "release_build": build_record,
			"attestation": path_free(attestation_reference), "detached_signature": path_free(signature_reference),
			"approval_public_key": path_free(public_key_reference),
		}
		receipt = {
			"schema_version": 1, "receipt": HOLDOUT_RECEIPT_KIND,
			"status": "opening-consumed-before-audio-read", "consumed_at_utc": "2026-07-16T10:01:00Z", **common,
		}
		receipt_reference = write_holdout("holdout/receipt.json", canonical_json_bytes(receipt) + b"\n")
		report = {
			"schema_version": 1, "opening_report": HOLDOUT_OPENING_REPORT_KIND,
			"status": "opening-consumed-before-audio-read", "authorized_at_utc": attestation["authorized_at_utc"],
			"valid_until_utc": attestation["valid_until_utc"], "consumed_at_utc": receipt["consumed_at_utc"],
			"receipt": path_free(receipt_reference), **common,
		}
		report_reference = write_holdout("holdout/report.json", canonical_json_bytes(report) + b"\n")
		opening = {
			"authorized_at_utc": attestation["authorized_at_utc"], "valid_until_utc": attestation["valid_until_utc"],
			"receipt": path_free(receipt_reference), "opening_report": path_free(report_reference), **common,
		}
		holdout_entry = {
			"case_id": "case-holdout", "profile": "Quality", "run_id": run_id,
			"attestation": attestation_reference, "detached_signature": signature_reference,
			"approval_public_key": public_key_reference, "receipt": receipt_reference,
			"opening_report": report_reference,
		}
		holdout_case = {"case_id": "case-holdout", "profile": "Quality", "dataset_split": "release-holdout"}
		holdout_score = {**holdout_case, "release_holdout_opening": opening}
		holdout_build = {**build, "release_holdout_approval_public_key_sha256": public_key_reference["sha256"]}
		_verify_release_holdout_openings(
			holdout_root, holdout_prefix, [holdout_entry], "release", holdout_build,
			public_key_reference["sha256"],
			[holdout_case], [holdout_score], {},
		)
		wrong_trust_build = {**holdout_build, "release_holdout_approval_public_key_sha256": "0" * 64}
		try:
			_verify_release_holdout_openings(
				holdout_root, holdout_prefix, [holdout_entry], "release", wrong_trust_build,
				"0" * 64, [holdout_case], [holdout_score], {},
			)
		except MeasurementEvidenceError:
			pass
		else:
			raise AssertionError("release holdout accepted a self-declared key outside the pinned trust root")
		bad_holdout_entry = copy.deepcopy(holdout_entry)
		bad_holdout_entry["run_id"] = "22345678-1234-4234-8234-123456789abc"
		try:
			_verify_release_holdout_openings(
				holdout_root, holdout_prefix, [bad_holdout_entry], "release", holdout_build,
				public_key_reference["sha256"],
				[holdout_case], [holdout_score], {},
			)
		except MeasurementEvidenceError:
			pass
		else:
			raise AssertionError("release holdout index accepted a run id not bound by the objective score")


def main(argv: Sequence[str] | None = None) -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--self-test", action="store_true")
	args = parser.parse_args(argv)
	try:
		if args.self_test:
			run_self_test()
			print("measurement evidence self-test: ok")
			return 0
		raise MeasurementEvidenceError("only --self-test is supported; use validate-quality-qualification.py for qualification validation")
	except (AssertionError, MeasurementEvidenceError, OSError, ValueError) as error:
		print(f"measurement evidence: error: {error}", file=sys.stderr)
		return 1


if __name__ == "__main__":
	raise SystemExit(main())
