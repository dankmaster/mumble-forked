#!/usr/bin/env python3
"""Canonical, audio-free per-case evidence primitives for quality qualification."""

from __future__ import annotations

import hashlib
import json
import math
import re
import statistics
from pathlib import Path
from typing import Any, Mapping, MutableMapping, Sequence


class CaseEvidenceError(ValueError):
	"""Raised when case evidence is incomplete, ambiguous, or non-canonical."""


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
)
CASE_KEYS = {
	"case_id", "cohort_id", "condition", "counters", "failed", "fixed_timeline", "language",
	"metrics", "performance", "profile", "receiver_cleanup_enabled", "record_type", "startup_preroll_ms",
}
TRANSITION_KEYS = {
	"case_id", "counters", "directed_pair", "failed", "fixed_timeline", "metrics", "performance",
	"receiver_cleanup_enabled", "record_type", "startup_preroll_ms",
}
HEADER_KEYS = {
	"case_count", "qualification_binding_sha256", "record_type", "schema_version", "transition_case_count",
}
IDENTIFIER = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._:-]{0,127}$")


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
		"schema_version": 1,
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
	_expect(record["profile"] in allowed_profiles, f"{path}.profile", "profile is outside qualification scope")
	_expect(record["condition"] in ("clean", "noisy", "severe"), f"{path}.condition", "unsupported condition")
	_expect(isinstance(record["language"], str) and bool(record["language"]) and "\n" not in record["language"], f"{path}.language", "invalid language")
	_expect(record["startup_preroll_ms"] in (0, 300), f"{path}.startup_preroll_ms", "must be 0 or 300")
	_expect(isinstance(record["fixed_timeline"], bool), f"{path}.fixed_timeline", "expected a boolean")
	_expect(isinstance(record["receiver_cleanup_enabled"], bool), f"{path}.receiver_cleanup_enabled", "expected a boolean")
	_expect(isinstance(record["failed"], bool), f"{path}.failed", "expected a boolean")

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
	return validated_cases, validated_transitions


def write_case_evidence(
	path: Path,
	build: Mapping[str, Any],
	qualification_scope: str,
	suite: str,
	cases: Sequence[Any],
	transitions: Sequence[Any],
) -> None:
	validated_cases, validated_transitions = validate_records(cases, transitions, qualification_scope)
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
	_expect(header["schema_version"] == 1, "case evidence header.schema_version", "unsupported version")
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
	cases: Sequence[Mapping[str, Any]], transitions: Sequence[Mapping[str, Any]], qualification_scope: str
) -> Mapping[str, Any]:
	profiles = PROFILES_BY_SCOPE[qualification_scope]
	coverage = {
		"case_count": len(cases),
		"failed_case_count": sum(1 for record in cases if record["failed"]),
		"cold_start_cases": sum(1 for record in cases if record["startup_preroll_ms"] == 0),
		"warm_start_cases": sum(1 for record in cases if record["startup_preroll_ms"] == 300),
		"fixed_timeline_cases": sum(1 for record in cases if record["fixed_timeline"]),
		"receiver_cleanup_cases": sum(1 for record in cases if record["receiver_cleanup_enabled"]),
		"languages": sorted({ str(record["language"]) for record in cases }),
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
				[ float(record["metrics"]["wer_loss_percentage_points"]) for record in rows if record["language"] == language ],
				f"case evidence profile {profile} language {language} WER",
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
			"clean_estoi_loss_median": _median([ float(record["metrics"]["estoi_loss"]) for record in clean ], f"{profile} clean eSTOI"),
			"clean_dnsmos_sig_loss_median": _median([ float(record["metrics"]["dnsmos_sig_loss"]) for record in clean ], f"{profile} clean SIG"),
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
		performance = {
			"average_rtf": sum(float(record["performance"]["processing_duration_seconds"]) for record in rows) / total_audio,
			"p99_callback_ms": _nearest_rank_percentile(callbacks, 0.99),
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
