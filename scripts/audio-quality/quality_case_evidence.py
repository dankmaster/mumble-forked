#!/usr/bin/env python3
"""Canonical, audio-free per-case evidence primitives for quality qualification."""

from __future__ import annotations

import hashlib
import json
import math
import re
import statistics
from pathlib import Path, PurePosixPath
from typing import Any, Mapping, MutableMapping, Sequence

from objective_quality_score import ALLOWED_DATASET_SPLITS, SIGNAL_STAGES, ObjectiveScoreError, validate_score_document


class CaseEvidenceError(ValueError):
	"""Raised when case evidence is incomplete, ambiguous, or non-canonical."""


CASE_EVIDENCE_SCHEMA_VERSION = 3
CORE_PROFILES = ("Original", "Light", "Balanced", "Quality", "VoiceFocus")
AUTO_PROFILES = ("Auto",)
PROFILES_BY_SCOPE = { "core": CORE_PROFILES, "auto": AUTO_PROFILES }
AUTO_DIRECTED_PAIRS = (
	"Light->Balanced",
	"Balanced->Light",
	"Light->Quality",
	"Quality->Light",
	"Balanced->Quality",
	"Quality->Balanced",
)
COUNTER_NAMES = (
	"deadline_misses",
	"latency_attestation_failures",
	"model_hash_errors",
	"nan_or_inf_count",
	"new_clipping_cases",
	"tail_drain_failures",
	"unexplained_fallbacks",
)
TRANSITION_COUNTER_NAMES = (
	"deadline_misses",
	"nan_or_inf_count",
	"new_clipping_cases",
	"unexplained_fallbacks",
)
CASE_METRIC_NAMES = (
	"algorithmic_latency_ms",
	"dnsmos_bak_improvement",
	"dnsmos_ovrl_improvement",
	"dnsmos_sig_loss",
	"estoi_loss",
	"severe_noise_bak_improvement_over_quality",
	"speech_edge_loss_ms",
	"wer_loss_percentage_points",
)
CASE_PERFORMANCE_NAMES = (
	"audio_duration_seconds",
	"callback_durations_ms",
	"max_internal_processing_ms",
	"memory_growth_bytes",
	"processing_duration_seconds",
	"soak_duration_seconds",
	"worker_durations_ms",
)
CASE_KEYS = {
	"case_id", "cohort_id", "condition", "counters", "dataset_split", "failed", "fixed_timeline", "language",
	"metrics", "noise_class", "objective_score", "performance", "profile", "receiver_cleanup_enabled",
	"quality_pair_case_id", "record_type", "startup_preroll_ms", "speaker_group_id", "noise_group_id", "rir_group_id", "device_group_id",
}
TRANSITION_KEYS = {
	"case_id", "counters", "directed_pair", "failed", "fixed_timeline", "metrics", "performance",
	"receiver_cleanup_enabled", "record_type", "startup_preroll_ms",
}
HEADER_KEYS = {
	"case_count", "qualification_binding_sha256", "record_type", "schema_version", "transition_case_count",
}
IDENTIFIER = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._:-]{0,127}$")
SOURCE_DIVERSITY_REQUIREMENTS = {
	"pr_smoke": { "speaker_groups": 2, "languages": 1, "noise_groups": 2, "noise_classes": 2, "rir_groups": 2, "device_groups": 2 },
	"release": { "speaker_groups": 2, "languages": 1, "noise_groups": 2, "noise_classes": 3, "rir_groups": 2, "device_groups": 2 },
	"master_quality": { "speaker_groups": 8, "languages": 2, "noise_groups": 8, "noise_classes": 6, "rir_groups": 6, "device_groups": 4 },
	"nightly": { "speaker_groups": 16, "languages": 2, "noise_groups": 16, "noise_classes": 10, "rir_groups": 12, "device_groups": 8 },
}


def _expect(condition: bool, path: str, message: str) -> None:
	if not condition:
		raise CaseEvidenceError(f"{path}: {message}")


def _mapping(value: Any, path: str) -> Mapping[str, Any]:
	_expect(isinstance(value, dict), path, "expected an object")
	return value


def _exact_keys(value: Mapping[str, Any], expected: set[str], path: str) -> None:
	missing = sorted(expected - set(value))
	unknown = sorted(set(value) - expected)
	_expect(not missing, path, f"missing keys: {', '.join(missing)}")
	_expect(not unknown, path, f"unknown keys: {', '.join(unknown)}")


def _number(value: Any, path: str, minimum: float | None = None) -> float:
	_expect(isinstance(value, (int, float)) and not isinstance(value, bool), path, "expected a number")
	number = float(value)
	_expect(math.isfinite(number), path, "must be finite")
	if minimum is not None:
		_expect(number >= minimum, path, f"must be >= {minimum}")
	return number


def _integer(value: Any, path: str, minimum: int | None = None) -> int:
	_expect(isinstance(value, int) and not isinstance(value, bool), path, "expected an integer")
	if minimum is not None:
		_expect(value >= minimum, path, f"must be >= {minimum}")
	return value


def _identifier(value: Any, path: str) -> str:
	_expect(isinstance(value, str) and bool(IDENTIFIER.fullmatch(value)), path, "invalid stable identifier")
	return value


def _hash(value: Any, path: str) -> str:
	_expect(isinstance(value, str) and bool(re.fullmatch(r"[0-9a-f]{64}", value)), path, "invalid lowercase SHA-256")
	return value


def _safe_relative_path(value: Any, path: str) -> str:
	_expect(isinstance(value, str) and bool(value), path, "expected a non-empty path")
	parsed = PurePosixPath(value)
	_expect(value == parsed.as_posix(), path, "must use normalized forward slashes")
	_expect(not parsed.is_absolute() and "." not in parsed.parts and ".." not in parsed.parts, path, "unsafe path")
	_expect(parsed.suffix.lower() == ".json", path, "objective score must be JSON")
	return value


def canonical_json_bytes(value: Any) -> bytes:
	return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")


def qualification_binding_sha256(
	build: Mapping[str, Any], qualification_scope: str, suite: str
) -> str:
	bound = { "build": build, "qualification_scope": qualification_scope, "suite": suite }
	return hashlib.sha256(canonical_json_bytes(bound)).hexdigest()


def make_header(
	build: Mapping[str, Any], qualification_scope: str, suite: str, case_count: int, transition_case_count: int
) -> Mapping[str, Any]:
	return {
		"record_type": "header",
		"schema_version": CASE_EVIDENCE_SCHEMA_VERSION,
		"qualification_binding_sha256": qualification_binding_sha256(build, qualification_scope, suite),
		"case_count": case_count,
		"transition_case_count": transition_case_count,
	}


def _load_json_line(raw: bytes, line_number: int) -> Mapping[str, Any]:
	def reject_duplicates(pairs: Sequence[tuple[str, Any]]) -> MutableMapping[str, Any]:
		result: MutableMapping[str, Any] = {}
		for key, value in pairs:
			if key in result:
				raise CaseEvidenceError(f"case evidence line {line_number}: duplicate JSON key {key!r}")
			result[key] = value
		return result

	try:
		value = json.loads(raw.decode("utf-8"), object_pairs_hook=reject_duplicates)
	except (UnicodeDecodeError, json.JSONDecodeError) as error:
		raise CaseEvidenceError(f"case evidence line {line_number}: invalid UTF-8 JSON: {error}") from error
	return _mapping(value, f"case evidence line {line_number}")


def validate_case_record(value: Any, path: str, allowed_profiles: Sequence[str]) -> Mapping[str, Any]:
	record = _mapping(value, path)
	_exact_keys(record, CASE_KEYS, path)
	_expect(record["record_type"] == "case", f"{path}.record_type", "must be case")
	_identifier(record["case_id"], f"{path}.case_id")
	_identifier(record["cohort_id"], f"{path}.cohort_id")
	_identifier(record["speaker_group_id"], f"{path}.speaker_group_id")
	_identifier(record["rir_group_id"], f"{path}.rir_group_id")
	_identifier(record["device_group_id"], f"{path}.device_group_id")
	_expect(record["profile"] in allowed_profiles, f"{path}.profile", "profile is outside qualification scope")
	_expect(record["condition"] in ("clean", "noisy", "severe"), f"{path}.condition", "unsupported condition")
	_expect(record["dataset_split"] in ALLOWED_DATASET_SPLITS, f"{path}.dataset_split", "holdout and unknown splits are forbidden")
	if record["condition"] == "clean":
		_expect(record["noise_group_id"] is None, f"{path}.noise_group_id", "clean cases must use null")
		_expect(record["noise_class"] is None, f"{path}.noise_class", "clean cases must use null")
	else:
		_identifier(record["noise_group_id"], f"{path}.noise_group_id")
		_identifier(record["noise_class"], f"{path}.noise_class")
	_expect(isinstance(record["language"], str) and bool(record["language"]) and "\n" not in record["language"], f"{path}.language", "invalid language")
	_expect(record["startup_preroll_ms"] in (0, 300), f"{path}.startup_preroll_ms", "must be 0 or 300")
	_expect(isinstance(record["fixed_timeline"], bool), f"{path}.fixed_timeline", "expected a boolean")
	_expect(isinstance(record["receiver_cleanup_enabled"], bool), f"{path}.receiver_cleanup_enabled", "expected a boolean")
	_expect(isinstance(record["failed"], bool), f"{path}.failed", "expected a boolean")
	objective = _mapping(record["objective_score"], f"{path}.objective_score")
	_exact_keys(objective, {"path", "sha256", "signal_stage", "size_bytes", "wer_reference_kind", "wer_reference_text_sha256"}, f"{path}.objective_score")
	_safe_relative_path(objective["path"], f"{path}.objective_score.path")
	_hash(objective["sha256"], f"{path}.objective_score.sha256")
	_integer(objective["size_bytes"], f"{path}.objective_score.size_bytes", 1)
	_expect(objective["signal_stage"] in SIGNAL_STAGES, f"{path}.objective_score.signal_stage", "unsupported signal stage")
	_expect(objective["wer_reference_kind"] in ("clean-asr-consistency", "segment-ground-truth"), f"{path}.objective_score.wer_reference_kind", "unsupported WER reference")
	_hash(objective["wer_reference_text_sha256"], f"{path}.objective_score.wer_reference_text_sha256")
	if record["profile"] == "VoiceFocus" and record["condition"] == "severe":
		_identifier(record["quality_pair_case_id"], f"{path}.quality_pair_case_id")
	else:
		_expect(record["quality_pair_case_id"] is None, f"{path}.quality_pair_case_id", "must be null outside severe VoiceFocus cases")

	metrics = _mapping(record["metrics"], f"{path}.metrics")
	_exact_keys(metrics, set(CASE_METRIC_NAMES), f"{path}.metrics")
	for name in CASE_METRIC_NAMES:
		minimum = 0.0 if name in ("algorithmic_latency_ms", "speech_edge_loss_ms") else None
		_number(metrics[name], f"{path}.metrics.{name}", minimum)
	if record["profile"] != "VoiceFocus" or record["condition"] != "severe":
		_expect(
			_number(metrics["severe_noise_bak_improvement_over_quality"], f"{path}.metrics.severe_noise_bak_improvement_over_quality") == 0.0,
			f"{path}.metrics.severe_noise_bak_improvement_over_quality",
			"must be zero outside severe VoiceFocus cases",
		)

	counters = _mapping(record["counters"], f"{path}.counters")
	_exact_keys(counters, set(COUNTER_NAMES), f"{path}.counters")
	for name in COUNTER_NAMES:
		_integer(counters[name], f"{path}.counters.{name}", 0)

	performance = _mapping(record["performance"], f"{path}.performance")
	_exact_keys(performance, set(CASE_PERFORMANCE_NAMES), f"{path}.performance")
	_number(performance["audio_duration_seconds"], f"{path}.performance.audio_duration_seconds", 0.000000001)
	_number(performance["processing_duration_seconds"], f"{path}.performance.processing_duration_seconds", 0)
	callbacks = performance["callback_durations_ms"]
	_expect(isinstance(callbacks, list) and callbacks, f"{path}.performance.callback_durations_ms", "expected a non-empty array")
	for index, duration in enumerate(callbacks):
		_number(duration, f"{path}.performance.callback_durations_ms[{index}]", 0)
	workers = performance["worker_durations_ms"]
	_expect(isinstance(workers, list) and workers, f"{path}.performance.worker_durations_ms", "expected a non-empty array")
	for index, duration in enumerate(workers):
		_number(duration, f"{path}.performance.worker_durations_ms[{index}]", 0)
	_number(performance["max_internal_processing_ms"], f"{path}.performance.max_internal_processing_ms", 0)
	_integer(performance["memory_growth_bytes"], f"{path}.performance.memory_growth_bytes")
	_integer(performance["soak_duration_seconds"], f"{path}.performance.soak_duration_seconds", 0)
	return record


def validate_transition_record(value: Any, path: str) -> Mapping[str, Any]:
	record = _mapping(value, path)
	_exact_keys(record, TRANSITION_KEYS, path)
	_expect(record["record_type"] == "auto_transition", f"{path}.record_type", "must be auto_transition")
	_identifier(record["case_id"], f"{path}.case_id")
	_expect(record["directed_pair"] in AUTO_DIRECTED_PAIRS, f"{path}.directed_pair", "unsupported directed pair")
	_expect(record["startup_preroll_ms"] in (0, 300), f"{path}.startup_preroll_ms", "must be 0 or 300")
	_expect(isinstance(record["fixed_timeline"], bool), f"{path}.fixed_timeline", "expected a boolean")
	_expect(isinstance(record["receiver_cleanup_enabled"], bool), f"{path}.receiver_cleanup_enabled", "expected a boolean")
	_expect(isinstance(record["failed"], bool), f"{path}.failed", "expected a boolean")
	counters = _mapping(record["counters"], f"{path}.counters")
	_exact_keys(counters, set(TRANSITION_COUNTER_NAMES), f"{path}.counters")
	for name in TRANSITION_COUNTER_NAMES:
		_integer(counters[name], f"{path}.counters.{name}", 0)
	metrics = _mapping(record["metrics"], f"{path}.metrics")
	_exact_keys(metrics, { "speech_edge_loss_ms", "transition_processing_ms" }, f"{path}.metrics")
	_number(metrics["speech_edge_loss_ms"], f"{path}.metrics.speech_edge_loss_ms", 0)
	_number(metrics["transition_processing_ms"], f"{path}.metrics.transition_processing_ms", 0)
	performance = _mapping(record["performance"], f"{path}.performance")
	_exact_keys(performance, { "memory_growth_bytes", "soak_duration_seconds" }, f"{path}.performance")
	_integer(performance["memory_growth_bytes"], f"{path}.performance.memory_growth_bytes")
	_integer(performance["soak_duration_seconds"], f"{path}.performance.soak_duration_seconds", 0)
	return record


def _record_sort_key(record: Mapping[str, Any], qualification_scope: str) -> tuple[int, int, str]:
	if record["record_type"] == "case":
		return (0, PROFILES_BY_SCOPE[qualification_scope].index(record["profile"]), record["case_id"])
	return (1, AUTO_DIRECTED_PAIRS.index(record["directed_pair"]), record["case_id"])


def validate_records(
	cases: Sequence[Any], transitions: Sequence[Any], qualification_scope: str
) -> tuple[list[Mapping[str, Any]], list[Mapping[str, Any]]]:
	_expect(qualification_scope in PROFILES_BY_SCOPE, "qualification_scope", "must be core or auto")
	validated_cases = [
		validate_case_record(value, f"cases[{index}]", PROFILES_BY_SCOPE[qualification_scope])
		for index, value in enumerate(cases)
	]
	validated_transitions = [
		validate_transition_record(value, f"auto_transitions[{index}]")
		for index, value in enumerate(transitions)
	]
	_expect(bool(validated_cases), "cases", "must contain at least one case")
	case_keys = [ (record["profile"], record["case_id"]) for record in validated_cases ]
	_expect(len(case_keys) == len(set(case_keys)), "cases", "duplicate profile/case_id")
	transition_keys = [ (record["directed_pair"], record["case_id"]) for record in validated_transitions ]
	_expect(len(transition_keys) == len(set(transition_keys)), "auto_transitions", "duplicate directed_pair/case_id")
	if qualification_scope == "core":
		_expect(not validated_transitions, "auto_transitions", "core evidence must not contain Auto transitions")
	else:
		_expect(bool(validated_transitions), "auto_transitions", "Auto evidence requires transition cases")
	case_registry = {(record["profile"], record["case_id"]): record for record in validated_cases}
	pair_fields = (
		"cohort_id", "condition", "dataset_split", "device_group_id", "language", "noise_class",
		"noise_group_id", "rir_group_id", "speaker_group_id", "startup_preroll_ms",
	)
	for index, record in enumerate(validated_cases):
		if record["profile"] != "VoiceFocus" or record["condition"] != "severe":
			continue
		quality = case_registry.get(("Quality", record["quality_pair_case_id"]))
		_expect(quality is not None, f"cases[{index}].quality_pair_case_id", "does not identify a Quality case")
		for field in pair_fields:
			_expect(quality[field] == record[field], f"cases[{index}].quality_pair_case_id", f"Quality pair differs in {field}")
	return validated_cases, validated_transitions


def validate_suite_splits(cases: Sequence[Mapping[str, Any]], suite: str) -> None:
	"""Keep the one-shot protected holdout exclusive to final release evidence."""

	_expect(suite in SOURCE_DIVERSITY_REQUIREMENTS, "suite", "unknown qualification suite")
	holdout_count = sum(record["dataset_split"] == "release-holdout" for record in cases)
	if suite == "release":
		_expect(holdout_count == len(cases), "case evidence dataset_split", "every final release case must use release-holdout")
	else:
		_expect(holdout_count == 0, "case evidence dataset_split", "release-holdout is forbidden outside the final release suite")


def source_diversity(cases: Sequence[Mapping[str, Any]]) -> Mapping[str, int]:
	return {
		"speaker_groups": len({str(record["speaker_group_id"]) for record in cases}),
		"languages": len({str(record["language"]) for record in cases}),
		"noise_groups": len({str(record["noise_group_id"]) for record in cases if record["noise_group_id"] is not None}),
		"noise_classes": len({str(record["noise_class"]) for record in cases if record["noise_class"] is not None}),
		"rir_groups": len({str(record["rir_group_id"]) for record in cases}),
		"device_groups": len({str(record["device_group_id"]) for record in cases}),
	}


def validate_source_diversity(cases: Sequence[Mapping[str, Any]], qualification_scope: str, suite: str) -> Mapping[str, int]:
	_expect(suite in SOURCE_DIVERSITY_REQUIREMENTS, "suite", "unknown qualification suite")
	requirements = SOURCE_DIVERSITY_REQUIREMENTS[suite]
	global_summary = source_diversity(cases)
	for key, minimum in requirements.items():
		_expect(global_summary[key] >= minimum, f"case evidence source diversity.{key}", f"requires at least {minimum}; found {global_summary[key]}")
	for profile in PROFILES_BY_SCOPE[qualification_scope]:
		rows = [record for record in cases if record["profile"] == profile]
		profile_summary = source_diversity(rows)
		for key, minimum in requirements.items():
			_expect(profile_summary[key] >= minimum, f"case evidence profile {profile} source diversity.{key}", f"requires at least {minimum}; found {profile_summary[key]}")
	return global_summary


def _sha256(path: Path) -> str:
	digest = hashlib.sha256()
	with path.open("rb") as stream:
		for block in iter(lambda: stream.read(1024 * 1024), b""):
			digest.update(block)
	return digest.hexdigest()


def verify_objective_score_references(
	cases: Sequence[Mapping[str, Any]], artifact_root: Path, required_signal_stage: str | None = None
) -> list[Mapping[str, Any]]:
	"""Recompute case metrics from separately hash-attested objective scores."""

	resolved_root = artifact_root.resolve()
	validated_scores: list[Mapping[str, Any]] = []
	score_registry: dict[tuple[str, str], Mapping[str, Any]] = {}
	for index, case in enumerate(cases):
		path = f"cases[{index}].objective_score"
		reference = _mapping(case["objective_score"], path)
		relative = _safe_relative_path(reference["path"], f"{path}.path")
		actual = resolved_root.joinpath(*PurePosixPath(relative).parts).resolve()
		try:
			actual.relative_to(resolved_root)
		except ValueError as error:
			raise CaseEvidenceError(f"{path}.path: escapes artifact root") from error
		_expect(actual.is_file() and not actual.is_symlink(), f"{path}.path", f"missing regular score artifact: {actual}")
		_expect(actual.stat().st_size == reference["size_bytes"], f"{path}.size_bytes", "artifact size mismatch")
		_expect(_sha256(actual) == reference["sha256"], f"{path}.sha256", "artifact hash mismatch")
		try:
			score = validate_score_document(_load_json_line(actual.read_bytes(), index + 1))
		except (OSError, ObjectiveScoreError) as error:
			raise CaseEvidenceError(f"{path}: invalid objective score: {error}") from error
		_expect(score["case_id"] == case["case_id"], f"{path}.case_id", "does not match case evidence")
		_expect(score["profile"] == case["profile"], f"{path}.profile", "does not match case evidence")
		_expect(score["condition"] == case["condition"], f"{path}.condition", "does not match case evidence")
		_expect(score["dataset_split"] == case["dataset_split"], f"{path}.dataset_split", "does not match case evidence")
		_expect(score["alignment"]["signal_stage"] == reference["signal_stage"], f"{path}.signal_stage", "does not match score")
		if required_signal_stage is not None:
			_expect(reference["signal_stage"] == required_signal_stage, f"{path}.signal_stage", f"{required_signal_stage} evidence is required")
		wer_reference = score["wer_reference"]
		expected_wer_language = str(case["language"]).split("-", 1)[0].split("_", 1)[0].casefold()
		_expect(expected_wer_language in ("en", "sv"), f"{path}.language", "objective WER currently supports only English or Swedish")
		_expect(wer_reference["language"] == expected_wer_language, f"{path}.language", "score WER language does not match the case")
		_expect(wer_reference["kind"] == reference["wer_reference_kind"], f"{path}.wer_reference_kind", "does not match score")
		_expect(wer_reference["text_sha256"] == reference["wer_reference_text_sha256"], f"{path}.wer_reference_text_sha256", "does not match score")
		deltas = score["candidate_minus_original"]
		expected_metrics = {
			"algorithmic_latency_ms": score["alignment"]["candidate_latency_samples"] / 48.0,
			"dnsmos_bak_improvement": deltas["dnsmos_bak"],
			"dnsmos_ovrl_improvement": deltas["dnsmos_ovrl"],
			"dnsmos_sig_loss": -deltas["dnsmos_sig"],
			"estoi_loss": -deltas["estoi"],
			"wer_loss_percentage_points": deltas["wer_delta_percentage_points"],
		}
		for metric_name, expected in expected_metrics.items():
			actual_metric = float(case["metrics"][metric_name])
			_expect(math.isclose(actual_metric, float(expected), rel_tol=1e-9, abs_tol=1e-9), f"{path}.{metric_name}", f"does not match objective score ({expected!r})")
		validated_scores.append(score)
		score_registry[(str(case["profile"]), str(case["case_id"]))] = score
	for index, case in enumerate(cases):
		if case["profile"] != "VoiceFocus" or case["condition"] != "severe":
			continue
		voice_focus = score_registry[("VoiceFocus", str(case["case_id"]))]
		quality = score_registry.get(("Quality", str(case["quality_pair_case_id"])))
		_expect(quality is not None, f"cases[{index}].quality_pair_case_id", "paired Quality objective score is missing")
		_expect(voice_focus["inputs"]["clean_reference"] == quality["inputs"]["clean_reference"], f"cases[{index}].quality_pair_case_id", "clean reference differs from Quality")
		_expect(voice_focus["inputs"]["noisy_original"] == quality["inputs"]["noisy_original"], f"cases[{index}].quality_pair_case_id", "noisy input scene differs from Quality")
		_expect(voice_focus["runtime"] == quality["runtime"], f"cases[{index}].quality_pair_case_id", "metrics runtime differs from Quality")
		_expect(voice_focus["wer_reference"] == quality["wer_reference"], f"cases[{index}].quality_pair_case_id", "language/reference differs from Quality")
		for field in ("signal_stage", "original_latency_samples", "original_window_start_samples"):
			_expect(voice_focus["alignment"][field] == quality["alignment"][field], f"cases[{index}].quality_pair_case_id", f"alignment differs in {field}")
		if voice_focus["alignment"]["signal_stage"] == "receiver-capture":
			voice_route = voice_focus["alignment"]["qualified_route_binding"]
			quality_route = quality["alignment"]["qualified_route_binding"]
			for field in ("control_wav", "control_fixed_timeline_score", "route_offset_samples", "stable_execution_identity"):
				_expect(voice_route[field] == quality_route[field], f"cases[{index}].quality_pair_case_id", f"qualified route differs in {field}")
		expected_pair_bak = float(voice_focus["metrics"]["candidate"]["dnsmos_bak"]) - float(quality["metrics"]["candidate"]["dnsmos_bak"])
		actual_pair_bak = float(case["metrics"]["severe_noise_bak_improvement_over_quality"])
		_expect(math.isclose(actual_pair_bak, expected_pair_bak, rel_tol=1e-9, abs_tol=1e-9), f"cases[{index}].metrics.severe_noise_bak_improvement_over_quality", f"does not match paired VoiceFocus minus Quality objective BAK ({expected_pair_bak!r})")
	return validated_scores


def write_case_evidence(
	path: Path,
	build: Mapping[str, Any],
	qualification_scope: str,
	suite: str,
	cases: Sequence[Any],
	transitions: Sequence[Any],
) -> None:
	validated_cases, validated_transitions = validate_records(cases, transitions, qualification_scope)
	validate_suite_splits(validated_cases, suite)
	validate_source_diversity(validated_cases, qualification_scope, suite)
	header = make_header(build, qualification_scope, suite, len(validated_cases), len(validated_transitions))
	records = [
		header,
		*sorted(validated_cases, key=lambda record: _record_sort_key(record, qualification_scope)),
		*sorted(validated_transitions, key=lambda record: _record_sort_key(record, qualification_scope)),
	]
	payload = b"".join(canonical_json_bytes(record) + b"\n" for record in records)
	path.parent.mkdir(parents=True, exist_ok=True)
	try:
		with path.open("xb") as stream:
			stream.write(payload)
	except FileExistsError as error:
		raise CaseEvidenceError(f"refusing to replace existing case evidence: {path}") from error


def load_case_evidence(
	path: Path,
	build: Mapping[str, Any],
	qualification_scope: str,
	suite: str,
) -> tuple[list[Mapping[str, Any]], list[Mapping[str, Any]]]:
	try:
		payload = path.read_bytes()
	except OSError as error:
		raise CaseEvidenceError(f"unable to read case evidence {path}: {error}") from error
	_expect(bool(payload), "case evidence", "file is empty")
	_expect(payload.endswith(b"\n") and b"\r" not in payload, "case evidence", "must use canonical LF lines with a final newline")
	raw_lines = payload.splitlines()
	records: list[Mapping[str, Any]] = []
	for index, raw in enumerate(raw_lines, start=1):
		_expect(bool(raw), f"case evidence line {index}", "blank lines are forbidden")
		record = _load_json_line(raw, index)
		_expect(raw == canonical_json_bytes(record), f"case evidence line {index}", "JSON is not canonical")
		records.append(record)
	header = records[0]
	_exact_keys(header, HEADER_KEYS, "case evidence header")
	_expect(header["record_type"] == "header", "case evidence header.record_type", "must be header")
	_expect(header["schema_version"] == CASE_EVIDENCE_SCHEMA_VERSION, "case evidence header.schema_version", "unsupported version")
	_expect(
		header["qualification_binding_sha256"] == qualification_binding_sha256(build, qualification_scope, suite),
		"case evidence header.qualification_binding_sha256",
		"does not bind the exact qualification scope, suite, and build",
	)
	case_count = _integer(header["case_count"], "case evidence header.case_count", 1)
	transition_count = _integer(header["transition_case_count"], "case evidence header.transition_case_count", 0)
	cases: list[Mapping[str, Any]] = []
	transitions: list[Mapping[str, Any]] = []
	for index, record in enumerate(records[1:], start=2):
		if record.get("record_type") == "case":
			cases.append(record)
		elif record.get("record_type") == "auto_transition":
			transitions.append(record)
		else:
			raise CaseEvidenceError(f"case evidence line {index}.record_type: unsupported record")
	validated_cases, validated_transitions = validate_records(cases, transitions, qualification_scope)
	validate_suite_splits(validated_cases, suite)
	validate_source_diversity(validated_cases, qualification_scope, suite)
	_expect(len(validated_cases) == case_count, "case evidence header.case_count", "does not match record count")
	_expect(len(validated_transitions) == transition_count, "case evidence header.transition_case_count", "does not match record count")
	expected_order = [
		*sorted(validated_cases, key=lambda record: _record_sort_key(record, qualification_scope)),
		*sorted(validated_transitions, key=lambda record: _record_sort_key(record, qualification_scope)),
	]
	_expect(records[1:] == expected_order, "case evidence", "records are not in canonical profile/case order")
	return validated_cases, validated_transitions


def _median(values: Sequence[float], path: str) -> float:
	_expect(bool(values), path, "no qualifying case values")
	return float(statistics.median(values))


def _nearest_rank_percentile(values: Sequence[float], percentile: float) -> float:
	_expect(bool(values), "performance.callback_durations_ms", "no callback samples")
	ordered = sorted(values)
	index = max(0, math.ceil(percentile * len(ordered)) - 1)
	return float(ordered[index])


def summarize_case_evidence(
	cases: Sequence[Mapping[str, Any]], transitions: Sequence[Mapping[str, Any]], qualification_scope: str, suite: str
) -> Mapping[str, Any]:
	profiles = PROFILES_BY_SCOPE[qualification_scope]
	diversity = validate_source_diversity(cases, qualification_scope, suite)
	coverage = {
		"case_count": len(cases),
		"failed_case_count": sum(1 for record in cases if record["failed"]),
		"cold_start_cases": sum(1 for record in cases if record["startup_preroll_ms"] == 0),
		"warm_start_cases": sum(1 for record in cases if record["startup_preroll_ms"] == 300),
		"fixed_timeline_cases": sum(1 for record in cases if record["fixed_timeline"]),
		"receiver_cleanup_cases": sum(1 for record in cases if record["receiver_cleanup_enabled"]),
		"languages": sorted({ str(record["language"]) for record in cases }),
		"wer_reference_kinds": sorted({ str(record["objective_score"]["wer_reference_kind"]) for record in cases }),
		"objective_signal_stages": sorted({ str(record["objective_score"]["signal_stage"]) for record in cases }),
		"source_diversity": diversity,
	}
	profile_summaries: dict[str, Mapping[str, Any]] = {}
	for profile in profiles:
		rows = [ record for record in cases if record["profile"] == profile ]
		_expect(bool(rows), f"case evidence profile {profile}", "has no cases")
		profile_languages = { str(record["language"]) for record in rows }
		_expect(profile_languages == set(coverage["languages"]), f"case evidence profile {profile}", "does not cover every qualification language")
		clean = [ record for record in rows if record["condition"] == "clean" ]
		noisy = [ record for record in rows if record["condition"] in ("noisy", "severe") ]
		severe = [ record for record in rows if record["condition"] == "severe" ]
		_expect(bool(clean), f"case evidence profile {profile}", "requires clean cases")
		_expect(bool(noisy), f"case evidence profile {profile}", "requires noisy cases")
		if profile == "VoiceFocus":
			_expect(bool(severe), "case evidence profile VoiceFocus", "requires severe-noise cases")
		language_wer_medians = [
			_median(
				[ float(record["metrics"]["wer_loss_percentage_points"]) for record in clean if record["language"] == language ],
				f"case evidence profile {profile} language {language} clean WER",
			)
			for language in coverage["languages"]
		]
		language_clean_estoi_medians = [
			_median(
				[float(record["metrics"]["estoi_loss"]) for record in clean if record["language"] == language],
				f"case evidence profile {profile} language {language} clean eSTOI",
			)
			for language in coverage["languages"]
		]
		language_clean_sig_medians = [
			_median(
				[float(record["metrics"]["dnsmos_sig_loss"]) for record in clean if record["language"] == language],
				f"case evidence profile {profile} language {language} clean SIG",
			)
			for language in coverage["languages"]
		]
		cohort_losses = []
		for cohort in sorted({ str(record["cohort_id"]) for record in rows }):
			median_delta = _median(
				[ float(record["metrics"]["dnsmos_ovrl_improvement"]) for record in rows if record["cohort_id"] == cohort ],
				f"case evidence profile {profile} cohort {cohort} OVRL",
			)
			cohort_losses.append(max(0.0, -median_delta))
		catastrophes = sum(
			1 for record in rows
			if float(record["metrics"]["estoi_loss"]) > 0.05 or float(record["metrics"]["dnsmos_sig_loss"]) > 0.5
		)
		metrics = {
			"algorithmic_latency_ms_max": max(float(record["metrics"]["algorithmic_latency_ms"]) for record in rows),
			"worst_language_clean_estoi_loss_median": max(language_clean_estoi_medians),
			"worst_language_clean_dnsmos_sig_loss_median": max(language_clean_sig_medians),
			"worst_language_wer_loss_percentage_points": max(language_wer_medians),
			"noisy_dnsmos_ovrl_improvement_median": _median([ float(record["metrics"]["dnsmos_ovrl_improvement"]) for record in noisy ], f"{profile} noisy OVRL"),
			"noisy_dnsmos_bak_improvement_median": _median([ float(record["metrics"]["dnsmos_bak_improvement"]) for record in noisy ], f"{profile} noisy BAK"),
			"severe_noise_bak_improvement_over_quality_median": (
				_median([ float(record["metrics"]["severe_noise_bak_improvement_over_quality"]) for record in severe ], "VoiceFocus severe BAK")
				if profile == "VoiceFocus" else 0.0
			),
			"worst_cohort_ovrl_loss_median": max(cohort_losses),
			"catastrophe_rate_percent": catastrophes * 100.0 / len(rows),
			"max_speech_edge_loss_ms": max(float(record["metrics"]["speech_edge_loss_ms"]) for record in rows),
			**{
				name: sum(int(record["counters"][name]) for record in rows)
				for name in COUNTER_NAMES
			},
		}
		total_audio = sum(float(record["performance"]["audio_duration_seconds"]) for record in rows)
		callbacks = [
			float(duration)
			for record in rows
			for duration in record["performance"]["callback_durations_ms"]
		]
		workers = [
			float(duration)
			for record in rows
			for duration in record["performance"]["worker_durations_ms"]
		]
		performance = {
			"average_rtf": sum(float(record["performance"]["processing_duration_seconds"]) for record in rows) / total_audio,
			# Each runtime report publishes its own frame-level p99.  The release
			# gate uses the worst case p99 instead of taking a percentile of
			# percentiles, which could hide one callback/worker regression.
			"p99_callback_ms": max(callbacks),
			"p99_worker_ms": max(workers),
			"max_internal_processing_ms": max(float(record["performance"]["max_internal_processing_ms"]) for record in rows),
			"memory_growth_bytes": max(int(record["performance"]["memory_growth_bytes"]) for record in rows),
			"soak_duration_seconds": sum(int(record["performance"]["soak_duration_seconds"]) for record in rows),
		}
		profile_summaries[profile] = {
			"case_count": len(rows),
			"failed_case_count": sum(1 for record in rows if record["failed"]),
			"metrics": metrics,
			"performance": performance,
		}

	transition_summary: Mapping[str, Any] | None = None
	if qualification_scope == "auto":
		_expect(bool(transitions), "auto transition evidence", "requires cases")
		transition_summary = {
			"case_count": len(transitions),
			"failed_case_count": sum(1 for record in transitions if record["failed"]),
			"cold_start_cases": sum(1 for record in transitions if record["startup_preroll_ms"] == 0),
			"warm_start_cases": sum(1 for record in transitions if record["startup_preroll_ms"] == 300),
			"fixed_timeline_cases": sum(1 for record in transitions if record["fixed_timeline"]),
			"receiver_cleanup_cases": sum(1 for record in transitions if record["receiver_cleanup_enabled"]),
			"directed_pairs": [ pair for pair in AUTO_DIRECTED_PAIRS if any(record["directed_pair"] == pair for record in transitions) ],
			**{
				name: sum(int(record["counters"][name]) for record in transitions)
				for name in TRANSITION_COUNTER_NAMES
			},
			"max_speech_edge_loss_ms": max(float(record["metrics"]["speech_edge_loss_ms"]) for record in transitions),
			"max_transition_processing_ms": max(float(record["metrics"]["transition_processing_ms"]) for record in transitions),
			"memory_growth_bytes": max(int(record["performance"]["memory_growth_bytes"]) for record in transitions),
			"soak_duration_seconds": sum(int(record["performance"]["soak_duration_seconds"]) for record in transitions),
		}
	return { "coverage": coverage, "profiles": profile_summaries, "auto_transitions": transition_summary }
