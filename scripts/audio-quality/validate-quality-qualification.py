#!/usr/bin/env python3
"""Validate semantic release gates in an input-enhancement qualification.json."""

from __future__ import annotations

import argparse
import copy
import datetime as dt
import hashlib
import json
import math
import re
import sys
import tempfile
from pathlib import Path, PurePosixPath
from typing import Any, Mapping, MutableMapping, Sequence

from objective_quality_score import _sample_score

from measurement_evidence import (
	INDEX_KIND,
	MeasurementEvidenceError,
	canonical_json_bytes as canonical_measurement_json_bytes,
	canonical_json_sha256 as canonical_measurement_json_sha256,
	load_and_verify_measurement_index,
)

from quality_case_evidence import (
	AUTO_DIRECTED_PAIRS,
	CORE_PROFILES,
	AUTO_PROFILES,
	CaseEvidenceError,
	load_case_evidence,
	qualification_binding_sha256,
	summarize_case_evidence,
	verify_objective_score_references,
	write_case_evidence,
)


class QualificationError(ValueError):
	"""Raised when qualification evidence is incomplete, unsafe, or below gate."""


PROFILES_BY_SCOPE = { "core": CORE_PROFILES, "auto": AUTO_PROFILES }
LATENCY_GATES_MS = {
	"Original": 0.0,
	"Light": 10.0,
	"Balanced": 30.0,
	"Quality": 50.0,
	"VoiceFocus": 50.0,
	"Auto": 50.0,
}
SUITE_CASES = {
	"core": {
		"pr_smoke": (30, 30),
		"master_quality": (500, None),
		"nightly": (5000, None),
		"release": (30, 30),
	},
	"auto": {
		"pr_smoke": (12, 12),
		"master_quality": (120, None),
		"nightly": (1200, None),
		"release": (12, 12),
	},
}
NOISY_GATES = {
	"Light": (0.10, 0.20),
	"Balanced": (0.15, 0.30),
	"Quality": (0.20, 0.40),
	"VoiceFocus": (0.20, 0.50),
	"Auto": (0.10, 0.20),
}
ARTIFACT_NAMES = (
	"case_evidence_jsonl",
	"failure_spectrogram_index",
	"junit",
	"measurement_index_json",
	"per_case_csv",
	"per_case_parquet",
	"summary_html",
	"summary_json",
)
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


def _expect(condition: bool, path: str, message: str) -> None:
	if not condition:
		raise QualificationError(f"{path}: {message}")


def _mapping(value: Any, path: str) -> Mapping[str, Any]:
	_expect(isinstance(value, dict), path, "expected an object")
	return value


def _exact_keys(value: Mapping[str, Any], required: set[str], path: str) -> None:
	missing = sorted(required - set(value))
	unknown = sorted(set(value) - required)
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


def _hash(value: Any, path: str, length: int) -> str:
	_expect(isinstance(value, str) and bool(re.fullmatch(rf"[0-9a-f]{{{length}}}", value)), path, "invalid lowercase hash")
	return value


def _safe_relative_path(value: Any, path: str) -> str:
	_expect(isinstance(value, str) and bool(value), path, "expected a non-empty path")
	parsed = PurePosixPath(value)
	_expect(value == parsed.as_posix(), path, "must use normalized forward slashes")
	_expect(not parsed.is_absolute() and ".." not in parsed.parts and "." not in parsed.parts, path, "unsafe path")
	return value


def _load_json(path: Path) -> Any:
	def reject_duplicates(pairs: Sequence[tuple[str, Any]]) -> MutableMapping[str, Any]:
		result: MutableMapping[str, Any] = {}
		for key, value in pairs:
			if key in result:
				raise QualificationError(f"duplicate JSON key: {key}")
			result[key] = value
		return result

	try:
		return json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=reject_duplicates)
	except (OSError, json.JSONDecodeError) as error:
		raise QualificationError(f"unable to read {path}: {error}") from error


def _sha256(path: Path) -> str:
	digest = hashlib.sha256()
	with path.open("rb") as stream:
		for chunk in iter(lambda: stream.read(1024 * 1024), b""):
			digest.update(chunk)
	return digest.hexdigest()


def _resolve_artifact(
	artifact_root: Path,
	artifact_value: Any,
	path: str,
) -> Path:
	artifact = _mapping(artifact_value, path)
	_exact_keys(artifact, { "contains_audio_samples", "path", "sha256", "size_bytes" }, path)
	relative = _safe_relative_path(artifact["path"], f"{path}.path")
	digest = _hash(artifact["sha256"], f"{path}.sha256", 64)
	size = _integer(artifact["size_bytes"], f"{path}.size_bytes", 0)
	_expect(artifact["contains_audio_samples"] is False, f"{path}.contains_audio_samples", "raw or encoded audio must not be published")
	resolved_root = artifact_root.resolve()
	actual = resolved_root.joinpath(*PurePosixPath(relative).parts).resolve()
	try:
		actual.relative_to(resolved_root)
	except ValueError as error:
		raise QualificationError(f"{path}.path: escapes artifact root") from error
	_expect(actual.is_file(), f"{path}.path", f"artifact is missing: {actual}")
	_expect(actual.stat().st_size == size, f"{path}.size_bytes", "artifact size mismatch")
	_expect(_sha256(actual) == digest, f"{path}.sha256", "artifact hash mismatch")
	return actual


def _same_number(actual: Any, expected: Any, path: str) -> float:
	actual_number = _number(actual, path)
	expected_number = _number(expected, f"{path} (recomputed)")
	_expect(
		math.isclose(actual_number, expected_number, rel_tol=1e-9, abs_tol=1e-9),
		path,
		f"does not match recomputed case evidence ({expected_number!r})",
	)
	return expected_number


def validate_qualification(value: Any, artifact_root: Path | None = None) -> Mapping[str, Any]:
	root = _mapping(value, "qualification")
	_exact_keys(
		root,
		{
			"artifacts", "auto_transitions", "build", "coverage", "generated_at_utc", "profiles",
			"qualification_scope", "schema_version", "status", "suite", "violations",
		},
		"qualification",
	)
	_expect(root["schema_version"] == 3, "qualification.schema_version", "unsupported version; schema v3 case evidence is required")
	_expect(artifact_root is not None, "qualification.artifacts", "an artifact root is required to verify canonical case evidence")
	scope = root["qualification_scope"]
	_expect(scope in PROFILES_BY_SCOPE, "qualification.qualification_scope", "must be core or auto")
	_expect(root["suite"] in SUITE_CASES[scope], "qualification.suite", "unknown suite")
	_expect(root["status"] in ("passed", "failed"), "qualification.status", "unknown status")
	try:
		stamp = dt.datetime.fromisoformat(str(root["generated_at_utc"]).replace("Z", "+00:00"))
	except ValueError as error:
		raise QualificationError("qualification.generated_at_utc: invalid ISO-8601 timestamp") from error
	_expect(stamp.tzinfo is not None and stamp.utcoffset() == dt.timedelta(0), "qualification.generated_at_utc", "must be UTC")

	build = _mapping(root["build"], "qualification.build")
	build_keys = {
		"case_set_sha256", "corpus_inventory_sha256", "corpus_lock_sha256", "git_sha",
		"hardware_fingerprint_sha256", "harness_sha256", "legacy_binary_sha256",
		"metrics_runtime_sha256", "mixture_plan_sha256", "model_hashes", "model_manifest_sha256",
		"recipe_manifest_sha256", "recipe_set_version", "release_fixtures_sha256", "runner_class",
		"server_binary_sha256", "staged_payload_sha256", "tested_binary_sha256",
	}
	if root["suite"] == "release":
		build_keys.add("release_holdout_approval_public_key_sha256")
	_exact_keys(
		build,
		build_keys,
		"qualification.build",
	)
	_hash(build["git_sha"], "qualification.build.git_sha", 40)
	for key in (
		"case_set_sha256", "corpus_inventory_sha256", "corpus_lock_sha256", "hardware_fingerprint_sha256",
		"harness_sha256", "legacy_binary_sha256", "metrics_runtime_sha256", "mixture_plan_sha256",
		"model_manifest_sha256", "recipe_manifest_sha256", "release_fixtures_sha256",
		"server_binary_sha256", "staged_payload_sha256", "tested_binary_sha256",
	):
		_hash(build[key], f"qualification.build.{key}", 64)
	if root["suite"] == "release":
		_hash(build["release_holdout_approval_public_key_sha256"], "qualification.build.release_holdout_approval_public_key_sha256", 64)
	_expect(
		build["runner_class"] in ("low-performance", "mainstream", "local-development"),
		"qualification.build.runner_class",
		"unsupported runner class",
	)
	_expect(isinstance(build["recipe_set_version"], str) and bool(build["recipe_set_version"]), "qualification.build.recipe_set_version", "required")
	_expect(isinstance(build["model_hashes"], list) and build["model_hashes"], "qualification.build.model_hashes", "expected a non-empty array")
	for index, digest in enumerate(build["model_hashes"]):
		_hash(digest, f"qualification.build.model_hashes[{index}]", 64)
	_expect(build["model_hashes"] == sorted(set(build["model_hashes"])), "qualification.build.model_hashes", "must be sorted and unique")

	artifacts = _mapping(root["artifacts"], "qualification.artifacts")
	_exact_keys(artifacts, set(ARTIFACT_NAMES), "qualification.artifacts")
	artifact_prefix = f"artifacts/{root['suite']}-{build['runner_class']}/"
	artifact_paths: list[str] = []
	for name in ARTIFACT_NAMES:
		relative = _safe_relative_path(artifacts[name].get("path") if isinstance(artifacts[name], dict) else None, f"qualification.artifacts.{name}.path")
		_expect(relative.startswith(artifact_prefix), f"qualification.artifacts.{name}.path", f"must be namespaced below {artifact_prefix}")
		_expect(PurePosixPath(relative).suffix.lower() == ARTIFACT_SUFFIXES[name], f"qualification.artifacts.{name}.path", f"must use the {ARTIFACT_SUFFIXES[name]} suffix")
		artifact_paths.append(relative)
	_expect(len(artifact_paths) == len(set(artifact_paths)), "qualification.artifacts", "artifact paths must be unique")
	assert artifact_root is not None
	resolved_artifacts = {
		name: _resolve_artifact(artifact_root, artifacts[name], f"qualification.artifacts.{name}")
		for name in ARTIFACT_NAMES
	}
	try:
		case_records, transition_records = load_case_evidence(
			resolved_artifacts["case_evidence_jsonl"], build, scope, root["suite"]
		)
		objective_scores = verify_objective_score_references(
			case_records,
			artifact_root,
			"receiver-capture" if root["suite"] in ("master_quality", "nightly", "release") else None,
		)
		derived_cases, derived_transitions, _ = load_and_verify_measurement_index(
			resolved_artifacts["measurement_index_json"],
			artifact_root,
			build,
			scope,
			root["suite"],
			qualification_binding_sha256(build, scope, root["suite"]),
			artifacts,
			case_records,
			transition_records,
			objective_scores,
		)
		case_summary = summarize_case_evidence(derived_cases, derived_transitions, scope, root["suite"])
	except CaseEvidenceError as error:
		raise QualificationError(f"qualification.artifacts.case_evidence_jsonl: {error}") from error
	except MeasurementEvidenceError as error:
		raise QualificationError(f"qualification.artifacts.measurement_index_json: {error}") from error

	coverage = _mapping(root["coverage"], "qualification.coverage")
	_exact_keys(
		coverage,
		{
			"case_count", "cold_start_cases", "failed_case_count", "fixed_timeline_cases", "languages",
			"objective_signal_stages", "receiver_cleanup_cases", "source_diversity", "wer_reference_kinds", "warm_start_cases",
		},
		"qualification.coverage",
	)
	case_count = _integer(coverage["case_count"], "qualification.coverage.case_count", 1)
	minimum, maximum = SUITE_CASES[scope][root["suite"]]
	_expect(case_count >= minimum, "qualification.coverage.case_count", f"{root['suite']} requires at least {minimum}")
	if maximum is not None:
		_expect(case_count == maximum, "qualification.coverage.case_count", f"{root['suite']} requires exactly {maximum}")
	failed_cases = _integer(coverage["failed_case_count"], "qualification.coverage.failed_case_count", 0)
	cold_cases = _integer(coverage["cold_start_cases"], "qualification.coverage.cold_start_cases", 0)
	warm_cases = _integer(coverage["warm_start_cases"], "qualification.coverage.warm_start_cases", 0)
	fixed_cases = _integer(coverage["fixed_timeline_cases"], "qualification.coverage.fixed_timeline_cases", 0)
	receiver_cases = _integer(coverage["receiver_cleanup_cases"], "qualification.coverage.receiver_cleanup_cases", 0)
	_expect(cold_cases > 0 and warm_cases > 0 and cold_cases + warm_cases == case_count, "qualification.coverage", "must partition cases into 0 ms cold and 300 ms warm start")
	_expect(fixed_cases == case_count, "qualification.coverage.fixed_timeline_cases", "all gates require fixed-timeline scoring")
	_expect(receiver_cases == 0, "qualification.coverage.receiver_cleanup_cases", "receiver cleanup must be disabled")
	_expect(isinstance(coverage["languages"], list) and coverage["languages"], "qualification.coverage.languages", "expected languages")
	_expect(
		all(isinstance(language, str) and bool(language) for language in coverage["languages"])
		and coverage["languages"] == sorted(set(coverage["languages"])),
		"qualification.coverage.languages",
		"must be sorted unique non-empty strings",
	)
	_expect(
		coverage == case_summary["coverage"],
		"qualification.coverage",
		f"does not match canonical case evidence: expected {case_summary['coverage']!r}",
	)

	profiles = root["profiles"]
	_expect(isinstance(profiles, list), "qualification.profiles", "expected an array")
	profile_names = [ profile.get("profile") if isinstance(profile, dict) else None for profile in profiles ]
	expected_profiles = PROFILES_BY_SCOPE[scope]
	_expect(
		profile_names == list(expected_profiles),
		"qualification.profiles",
		f"must contain {', '.join(expected_profiles)} in order for {scope} qualification",
	)
	all_profiles_passed = True
	profile_case_total = 0
	for index, profile_value in enumerate(profiles):
		path = f"qualification.profiles[{index}]"
		profile = _mapping(profile_value, path)
		_exact_keys(profile, { "case_count", "metrics", "passed", "performance", "profile" }, path)
		name = profile["profile"]
		computed_profile = case_summary["profiles"][name]
		declared_profile_cases = _integer(profile["case_count"], f"{path}.case_count", 1)
		_expect(declared_profile_cases == computed_profile["case_count"], f"{path}.case_count", "does not match canonical case evidence")
		profile_case_total += declared_profile_cases
		_expect(isinstance(profile["passed"], bool), f"{path}.passed", "expected boolean")
		metrics = _mapping(profile["metrics"], f"{path}.metrics")
		metric_keys = {
			"algorithmic_latency_ms_max", "catastrophe_rate_percent", "worst_language_clean_dnsmos_sig_loss_median",
			"worst_language_clean_estoi_loss_median", "deadline_misses", "latency_attestation_failures",
			"max_speech_edge_loss_ms", "model_hash_errors", "nan_or_inf_count", "new_clipping_cases",
			"noisy_dnsmos_bak_improvement_median", "noisy_dnsmos_ovrl_improvement_median",
			"severe_noise_bak_improvement_over_quality_median",
			"tail_drain_failures", "unexplained_fallbacks", "worst_cohort_ovrl_loss_median",
			"worst_language_wer_loss_percentage_points",
		}
		_exact_keys(metrics, metric_keys, f"{path}.metrics")
		computed_metrics = computed_profile["metrics"]
		clean_estoi = _same_number(metrics["worst_language_clean_estoi_loss_median"], computed_metrics["worst_language_clean_estoi_loss_median"], f"{path}.metrics.worst_language_clean_estoi_loss_median")
		clean_sig = _same_number(metrics["worst_language_clean_dnsmos_sig_loss_median"], computed_metrics["worst_language_clean_dnsmos_sig_loss_median"], f"{path}.metrics.worst_language_clean_dnsmos_sig_loss_median")
		wer_loss = _same_number(metrics["worst_language_wer_loss_percentage_points"], computed_metrics["worst_language_wer_loss_percentage_points"], f"{path}.metrics.worst_language_wer_loss_percentage_points")
		ovrl = _same_number(metrics["noisy_dnsmos_ovrl_improvement_median"], computed_metrics["noisy_dnsmos_ovrl_improvement_median"], f"{path}.metrics.noisy_dnsmos_ovrl_improvement_median")
		bak = _same_number(metrics["noisy_dnsmos_bak_improvement_median"], computed_metrics["noisy_dnsmos_bak_improvement_median"], f"{path}.metrics.noisy_dnsmos_bak_improvement_median")
		severe_bak_over_quality = _same_number(
			metrics["severe_noise_bak_improvement_over_quality_median"],
			computed_metrics["severe_noise_bak_improvement_over_quality_median"],
			f"{path}.metrics.severe_noise_bak_improvement_over_quality_median",
		)
		cohort_loss = _same_number(metrics["worst_cohort_ovrl_loss_median"], computed_metrics["worst_cohort_ovrl_loss_median"], f"{path}.metrics.worst_cohort_ovrl_loss_median")
		catastrophe = _same_number(metrics["catastrophe_rate_percent"], computed_metrics["catastrophe_rate_percent"], f"{path}.metrics.catastrophe_rate_percent")
		edge_loss = _same_number(metrics["max_speech_edge_loss_ms"], computed_metrics["max_speech_edge_loss_ms"], f"{path}.metrics.max_speech_edge_loss_ms")
		latency = _same_number(metrics["algorithmic_latency_ms_max"], computed_metrics["algorithmic_latency_ms_max"], f"{path}.metrics.algorithmic_latency_ms_max")
		metric_passed = clean_estoi <= 0.01 and clean_sig <= 0.05 and wer_loss <= 1.0
		metric_passed = (
			metric_passed and cohort_loss <= 0.10 and catastrophe <= 0.5 and edge_loss <= 10.0
			and latency <= LATENCY_GATES_MS[name]
		)
		for key in (
			"deadline_misses", "latency_attestation_failures", "model_hash_errors", "nan_or_inf_count",
			"new_clipping_cases", "tail_drain_failures", "unexplained_fallbacks",
		):
			declared_counter = _integer(metrics[key], f"{path}.metrics.{key}", 0)
			_expect(declared_counter == computed_metrics[key], f"{path}.metrics.{key}", "does not match canonical case evidence")
			metric_passed = metric_passed and computed_metrics[key] == 0
		if name in NOISY_GATES:
			minimum_ovrl, minimum_bak = NOISY_GATES[name]
			metric_passed = metric_passed and ovrl >= minimum_ovrl and bak >= minimum_bak
		if name == "VoiceFocus":
			metric_passed = metric_passed and severe_bak_over_quality >= 0.10

		performance = _mapping(profile["performance"], f"{path}.performance")
		performance_keys = {
			"average_rtf", "max_internal_processing_ms", "memory_growth_bytes", "p99_callback_ms",
			"p99_worker_ms", "soak_duration_seconds",
		}
		_exact_keys(performance, performance_keys, f"{path}.performance")
		computed_performance = computed_profile["performance"]
		average_rtf = _same_number(performance["average_rtf"], computed_performance["average_rtf"], f"{path}.performance.average_rtf")
		p99 = _same_number(performance["p99_callback_ms"], computed_performance["p99_callback_ms"], f"{path}.performance.p99_callback_ms")
		worker_p99 = _same_number(performance["p99_worker_ms"], computed_performance["p99_worker_ms"], f"{path}.performance.p99_worker_ms")
		max_internal = _same_number(performance["max_internal_processing_ms"], computed_performance["max_internal_processing_ms"], f"{path}.performance.max_internal_processing_ms")
		memory_growth = _integer(performance["memory_growth_bytes"], f"{path}.performance.memory_growth_bytes")
		soak_duration = _integer(performance["soak_duration_seconds"], f"{path}.performance.soak_duration_seconds", 0)
		_expect(memory_growth == computed_performance["memory_growth_bytes"], f"{path}.performance.memory_growth_bytes", "does not match canonical case evidence")
		_expect(soak_duration == computed_performance["soak_duration_seconds"], f"{path}.performance.soak_duration_seconds", "does not match canonical case evidence")
		performance_passed = True
		if name == "Balanced":
			performance_passed = average_rtf <= 0.15 and p99 <= 5.0
		elif name in ("Quality", "VoiceFocus", "Auto"):
			performance_passed = average_rtf <= 0.35 and p99 <= 8.0 and worker_p99 <= 8.0
		if root["suite"] == "nightly" and name in ("Balanced", "Quality", "VoiceFocus", "Auto"):
			performance_passed = performance_passed and soak_duration >= 3600 and max_internal <= 10.0 and memory_growth <= 0
		expected_profile_pass = metric_passed and performance_passed and computed_profile["failed_case_count"] == 0
		_expect(profile["passed"] == expected_profile_pass, f"{path}.passed", "does not match semantic gates")
		all_profiles_passed = all_profiles_passed and profile["passed"]
	_expect(profile_case_total == case_count, "qualification.profiles", "profile case counts must sum to coverage.case_count")

	transition_passed = True
	if scope == "core":
		_expect(root["auto_transitions"] is None, "qualification.auto_transitions", "must be null for core qualification")
	else:
		transitions = _mapping(root["auto_transitions"], "qualification.auto_transitions")
		computed_transitions = case_summary["auto_transitions"]
		_expect(isinstance(computed_transitions, dict), "qualification.auto_transitions", "canonical transition evidence is missing")
		transition_keys = {
			"case_count", "cold_start_cases", "deadline_misses", "directed_pairs", "max_speech_edge_loss_ms",
			"max_transition_processing_ms", "memory_growth_bytes", "nan_or_inf_count", "new_clipping_cases",
			"passed", "soak_duration_seconds", "unexplained_fallbacks", "warm_start_cases",
		}
		_exact_keys(transitions, transition_keys, "qualification.auto_transitions")
		transition_cases = _integer(transitions["case_count"], "qualification.auto_transitions.case_count", 1)
		transition_cold = _integer(transitions["cold_start_cases"], "qualification.auto_transitions.cold_start_cases", 0)
		transition_warm = _integer(transitions["warm_start_cases"], "qualification.auto_transitions.warm_start_cases", 0)
		_expect(transition_cases == computed_transitions["case_count"], "qualification.auto_transitions.case_count", "does not match canonical case evidence")
		_expect(transition_cold == computed_transitions["cold_start_cases"], "qualification.auto_transitions.cold_start_cases", "does not match canonical case evidence")
		_expect(transition_warm == computed_transitions["warm_start_cases"], "qualification.auto_transitions.warm_start_cases", "does not match canonical case evidence")
		pairs = transitions["directed_pairs"]
		_expect(
			isinstance(pairs, list) and pairs == list(AUTO_DIRECTED_PAIRS) and pairs == computed_transitions["directed_pairs"],
			"qualification.auto_transitions.directed_pairs",
			"must contain all six directed Light/Balanced/Quality pairs in canonical order",
		)
		transition_passed = (
			transition_cases == case_count
			and computed_transitions["failed_case_count"] == 0
			and computed_transitions["fixed_timeline_cases"] == transition_cases
			and computed_transitions["receiver_cleanup_cases"] == 0
			and transition_cold > 0
			and transition_warm > 0
			and transition_cold + transition_warm == transition_cases
			and all(
				_integer(transitions[name], f"qualification.auto_transitions.{name}", 0) == computed_transitions[name] == 0
				for name in ("deadline_misses", "nan_or_inf_count", "new_clipping_cases", "unexplained_fallbacks")
			)
			and _same_number(transitions["max_speech_edge_loss_ms"], computed_transitions["max_speech_edge_loss_ms"], "qualification.auto_transitions.max_speech_edge_loss_ms") <= 10.0
			and _same_number(transitions["max_transition_processing_ms"], computed_transitions["max_transition_processing_ms"], "qualification.auto_transitions.max_transition_processing_ms") <= 10.0
		)
		transition_memory = _integer(transitions["memory_growth_bytes"], "qualification.auto_transitions.memory_growth_bytes")
		transition_soak = _integer(transitions["soak_duration_seconds"], "qualification.auto_transitions.soak_duration_seconds", 0)
		_expect(transition_memory == computed_transitions["memory_growth_bytes"], "qualification.auto_transitions.memory_growth_bytes", "does not match canonical case evidence")
		_expect(transition_soak == computed_transitions["soak_duration_seconds"], "qualification.auto_transitions.soak_duration_seconds", "does not match canonical case evidence")
		if root["suite"] == "nightly":
			transition_passed = transition_passed and transition_memory <= 0 and transition_soak >= 3600
		_expect(isinstance(transitions["passed"], bool), "qualification.auto_transitions.passed", "expected boolean")
		_expect(transitions["passed"] == transition_passed, "qualification.auto_transitions.passed", "does not match semantic gates")

	_expect(isinstance(root["violations"], list), "qualification.violations", "expected an array")
	_expect(all(isinstance(item, str) and bool(item) for item in root["violations"]), "qualification.violations", "entries must be non-empty strings")
	semantic_pass = failed_cases == 0 and all_profiles_passed and transition_passed and not root["violations"]
	_expect(root["status"] == ("passed" if semantic_pass else "failed"), "qualification.status", "does not match semantic gates")
	return root


def _build_fixture() -> Mapping[str, Any]:
	return {
		"git_sha": "1" * 40,
		"tested_binary_sha256": "2" * 64,
		"staged_payload_sha256": "6" * 64,
		"legacy_binary_sha256": "7" * 64,
		"server_binary_sha256": "8" * 64,
		"harness_sha256": "9" * 64,
		"hardware_fingerprint_sha256": "a" * 64,
		"runner_class": "local-development",
		"corpus_lock_sha256": "3" * 64,
		"corpus_inventory_sha256": "b" * 64,
		"mixture_plan_sha256": "4" * 64,
		"case_set_sha256": "c" * 64,
		"release_fixtures_sha256": "d" * 64,
		"metrics_runtime_sha256": "e" * 64,
		"model_manifest_sha256": "f" * 64,
		"recipe_manifest_sha256": "0" * 64,
		"recipe_set_version": "recipes-v1",
		"model_hashes": [ "5" * 64 ],
	}


def _case_record(profile: str, index: int) -> Mapping[str, Any]:
	conditions = ("clean", "clean", "noisy", "noisy", "severe", "severe")
	condition = conditions[index % len(conditions)]
	latency = { "Original": 0.0, "Light": 10.0, "Balanced": 30.0, "Quality": 50.0, "VoiceFocus": 50.0, "Auto": 50.0 }[profile]
	return {
		"record_type": "case",
		"case_id": f"case-{index:03d}",
		"profile": profile,
		"condition": condition,
		"dataset_split": "pr-smoke",
		"cohort_id": { "clean": "clean-room", "noisy": "noisy-hvac", "severe": "severe-babble" }[condition],
		"speaker_group_id": f"speaker-{index % 8:03d}",
		"noise_group_id": None if condition == "clean" else f"noise-{index % 8:03d}",
		"noise_class": None if condition == "clean" else ("hvac", "keyboard", "traffic", "babble")[index % 4],
		"rir_group_id": f"room-{index % 8:03d}",
		"device_group_id": f"device-{index % 4:03d}",
		"language": ("en-US", "sv-SE")[index % 2],
		"startup_preroll_ms": 0 if index % 2 == 0 else 300,
		"fixed_timeline": True,
		"receiver_cleanup_enabled": False,
		"failed": False,
		"quality_pair_case_id": f"case-{index:03d}" if profile == "VoiceFocus" and condition == "severe" else None,
		"objective_score": {
			"path": f"artifacts/pr_smoke-local-development/measurements/{profile}/{index:03d}/objective-quality.json",
			"sha256": "0" * 64,
			"signal_stage": "sender-pre-opus",
			"size_bytes": 1,
			"wer_reference_kind": "clean-asr-consistency",
			"wer_reference_text_sha256": "1" * 64,
		},
		"metrics": {
			"algorithmic_latency_ms": latency,
			"dnsmos_bak_improvement": 0.65 if profile == "VoiceFocus" and condition == "severe" else 0.55,
			"dnsmos_ovrl_improvement": -0.05 if condition == "clean" else 0.25,
			"dnsmos_sig_loss": 0.02,
			"estoi_loss": 0.005,
			"severe_noise_bak_improvement_over_quality": 0.10 if profile == "VoiceFocus" and condition == "severe" else 0.0,
			"speech_edge_loss_ms": 10.0,
			"wer_loss_percentage_points": 0.5,
		},
		"counters": {
			"deadline_misses": 0,
			"latency_attestation_failures": 0,
			"model_hash_errors": 0,
			"nan_or_inf_count": 0,
			"new_clipping_cases": 0,
			"tail_drain_failures": 0,
			"unexplained_fallbacks": 0,
		},
		"performance": {
			"audio_duration_seconds": 10.0,
			"processing_duration_seconds": 1.0,
			"callback_durations_ms": [ 4.0 ],
			"worker_durations_ms": [ 2.0 if profile in ("Quality", "VoiceFocus", "Auto") else 0.0 ],
			"max_internal_processing_ms": 9.0,
			"memory_growth_bytes": 0,
			"soak_duration_seconds": 0,
		},
	}


def _transition_record(index: int) -> Mapping[str, Any]:
	return {
		"record_type": "auto_transition",
		"case_id": f"transition-{index:03d}",
		"directed_pair": AUTO_DIRECTED_PAIRS[index % len(AUTO_DIRECTED_PAIRS)],
		"startup_preroll_ms": 0 if index % 2 == 0 else 300,
		"fixed_timeline": True,
		"receiver_cleanup_enabled": False,
		"failed": False,
		"counters": {
			"deadline_misses": 0,
			"nan_or_inf_count": 0,
			"new_clipping_cases": 0,
			"unexplained_fallbacks": 0,
		},
		"metrics": { "speech_edge_loss_ms": 10.0, "transition_processing_ms": 9.0 },
		"performance": { "memory_growth_bytes": 0, "soak_duration_seconds": 0 },
	}


def _passing_fixture(scope: str) -> tuple[Mapping[str, Any], list[Mapping[str, Any]], list[Mapping[str, Any]]]:
	profiles = PROFILES_BY_SCOPE[scope]
	per_profile = 6 if scope == "core" else 12
	cases = [ _case_record(profile, index) for profile in profiles for index in range(per_profile) ]
	transitions = [ _transition_record(index) for index in range(12) ] if scope == "auto" else []
	summary = summarize_case_evidence(cases, transitions, scope, "pr_smoke")
	profile_results = [
		{
			"profile": profile,
			"case_count": summary["profiles"][profile]["case_count"],
			"passed": True,
			"metrics": copy.deepcopy(summary["profiles"][profile]["metrics"]),
			"performance": copy.deepcopy(summary["profiles"][profile]["performance"]),
		}
		for profile in profiles
	]
	transition_result = None
	if scope == "auto":
		transition_summary = summary["auto_transitions"]
		assert isinstance(transition_summary, dict)
		transition_result = {
			key: copy.deepcopy(value)
			for key, value in transition_summary.items()
			if key not in ("failed_case_count", "fixed_timeline_cases", "receiver_cleanup_cases")
		}
		transition_result["passed"] = True
	artifacts = {
		name: {
			"path": (
				"artifacts/pr_smoke-local-development/case-evidence.jsonl"
				if name == "case_evidence_jsonl"
				else "artifacts/pr_smoke-local-development/measurement-index.json"
				if name == "measurement_index_json"
				else f"artifacts/pr_smoke-local-development/{name}{ARTIFACT_SUFFIXES[name]}"
			),
			"sha256": "3" * 64,
			"size_bytes": 1,
			"contains_audio_samples": False,
		}
		for name in ARTIFACT_NAMES
	}
	fixture = {
		"schema_version": 3,
		"qualification_scope": scope,
		"suite": "pr_smoke",
		"status": "passed",
		"generated_at_utc": "2026-07-14T20:00:00Z",
		"build": _build_fixture(),
		"coverage": copy.deepcopy(summary["coverage"]),
		"profiles": profile_results,
		"auto_transitions": transition_result,
		"artifacts": artifacts,
		"violations": [],
	}
	return fixture, cases, transitions


def _materialize_fixture(
	root: Path,
	fixture: Mapping[str, Any],
	cases: Sequence[Mapping[str, Any]],
	transitions: Sequence[Mapping[str, Any]],
) -> None:
	artifact_prefix = f"artifacts/{fixture['suite']}-{fixture['build']['runner_class']}"

	def write_json(path: Path, value: Mapping[str, Any], canonical: bool = False) -> Mapping[str, Any]:
		path.parent.mkdir(parents=True, exist_ok=True)
		payload = (
			canonical_measurement_json_bytes(value) + b"\n"
			if canonical
			else (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode("utf-8")
		)
		path.write_bytes(payload)
		return {
			"contains_audio_samples": False,
			"path": path.relative_to(root).as_posix(),
			"sha256": hashlib.sha256(payload).hexdigest(),
			"size_bytes": len(payload),
		}

	def execution_identity() -> Mapping[str, Any]:
		build = fixture["build"]
		return {
			"client_binary_sha256": build["tested_binary_sha256"],
			"model_manifest_sha256": build["model_manifest_sha256"],
			"recipe_manifest_sha256": build["recipe_manifest_sha256"],
			"run_provenance_sha256": "a" * 64,
			"runtime_payload_sha256": build["staged_payload_sha256"],
			"server_binary_sha256": build["server_binary_sha256"],
		}

	case_index_entries: list[Mapping[str, Any]] = []
	objective_documents: list[Mapping[str, Any]] = []
	for case in cases:
		original_metric = {
			"dnsmos_bak": 2.0,
			"dnsmos_ovrl": 2.0,
			"dnsmos_sig": 2.0,
			"estoi": 0.8,
			"wer": {"errors": 0, "reference_words": 200, "rate": 0.0, "hypothesis_sha256": "2" * 64},
		}
		deltas = {
			"dnsmos_bak": case["metrics"]["dnsmos_bak_improvement"],
			"dnsmos_ovrl": case["metrics"]["dnsmos_ovrl_improvement"],
			"dnsmos_sig": -case["metrics"]["dnsmos_sig_loss"],
			"estoi": -case["metrics"]["estoi_loss"],
			"wer_delta_kind": "clean-asr-consistency",
			"wer_delta_percentage_points": case["metrics"]["wer_loss_percentage_points"],
		}
		candidate_metric = {
			"dnsmos_bak": original_metric["dnsmos_bak"] + deltas["dnsmos_bak"],
			"dnsmos_ovrl": original_metric["dnsmos_ovrl"] + deltas["dnsmos_ovrl"],
			"dnsmos_sig": original_metric["dnsmos_sig"] + deltas["dnsmos_sig"],
			"estoi": original_metric["estoi"] + deltas["estoi"],
			"wer": {"errors": 1, "reference_words": 200, "rate": 0.005, "hypothesis_sha256": "4" * 64},
		}
		document = copy.deepcopy(_sample_score())
		document.update({
			"case_id": case["case_id"],
			"profile": case["profile"],
			"condition": case["condition"],
			"dataset_split": case["dataset_split"],
			"metrics": {"original": original_metric, "candidate": candidate_metric},
			"candidate_minus_original": deltas,
		})
		latency_samples = int(round(float(case["metrics"]["algorithmic_latency_ms"]) * 48.0))
		document["alignment"].update({"candidate_latency_samples": latency_samples, "candidate_window_start_samples": latency_samples})
		document["wer_reference"].update({"text_sha256": "1" * 64, "word_count": 200, "language": str(case["language"]).split("-", 1)[0].casefold()})
		objective_path = root.joinpath(*PurePosixPath(case["objective_score"]["path"]).parts)
		objective_reference = write_json(objective_path, document)
		case["objective_score"]["sha256"] = objective_reference["sha256"]
		case["objective_score"]["size_bytes"] = objective_reference["size_bytes"]
		objective_documents.append(document)

		case_root = root / artifact_prefix / "measurements" / str(case["profile"]) / str(case["case_id"])
		input_samples = 480_000
		def benchmark_report(profile: str, latency: int, output_sha256: str) -> Mapping[str, Any]:
			worker_p99 = 2.0 if profile in ("Quality", "VoiceFocus", "Auto") else 0.0
			engine = "DeepFilterNet" if profile in ("Quality", "VoiceFocus") else "RNNoise" if profile in ("Balanced", "Auto") else "Speex" if profile == "Light" else "None"
			recipe_ids = {
				"Original": "input.original", "Light": "input.light.speex",
				"Balanced": "input.balanced.self-test", "Quality": "input.quality.self-test",
				"VoiceFocus": "input.voice-focus.self-test", "Auto": "input.auto.balanced.self-test",
			}
			return {
				"schema_version": 1,
				"kind": "mumble-input-enhancement-benchmark-measurement-v1",
				"source_report_sha256": "8" * 64,
				"processing_mode": "product-profile",
				"requested_profile": profile,
				"active_profile": profile,
				"active_engine": engine,
				"active_model_id": f"{engine.casefold()}:self-test" if engine in ("RNNoise", "DeepFilterNet") else "",
				"active_model_sha256": fixture["build"]["model_hashes"][0] if engine in ("RNNoise", "DeepFilterNet") else "",
				"input_sha256": document["inputs"]["noisy_original"]["sha256"],
				"clean_reference_sha256": document["inputs"]["clean_reference"]["sha256"],
				"output_sha256": output_sha256,
				"requested_recipe_id": recipe_ids[profile],
				"recipe_revision": 1,
				"used_fallback": False,
				"fallback_count": 0,
				"deadline_misses": 0,
				"reported_latency_samples": latency,
				"input_sample_count": input_samples,
				"output_sample_count": input_samples + latency,
				"drain_sample_count": latency,
				"processing_padding_sample_count": 0,
				"sample_count": input_samples + latency,
				"sample_rate": 48_000,
				"non_finite_sample_count": 0,
				"input_saturated_sample_count": 0,
				"saturated_sample_count": 0,
				"out_of_range_sample_count": 0,
				"audio_ms": 10_000.0,
				"processing_wall_ms": 1_000.0,
				"rtf": 0.1,
				"callback_p99_ms": 4.0,
				"worker_processing_p99_ms": worker_p99,
				"maximum_processing_ms": 9.0,
			}
		original_reference = write_json(
			case_root / "original-benchmark.json",
			benchmark_report("Original", 0, document["inputs"]["noisy_original"]["sha256"]),
		)
		candidate_reference = write_json(
			case_root / "candidate-benchmark.json",
			benchmark_report(str(case["profile"]), latency_samples, document["inputs"]["candidate"]["sha256"]),
		)
		edge_document = {
			"schema_version": 3,
			"scorer": "mumble-fixed-timeline-v3",
			"timeline_alignment": "fixed",
			"sample_rate_hz": 48_000,
			"frame_samples": 480,
			"declared_latency_samples": latency_samples,
			"reference_sha256": document["inputs"]["clean_reference"]["sha256"],
			"received_sha256": document["inputs"]["candidate"]["sha256"],
			"onset_loss_samples": 480,
			"end_loss_samples": 0,
			"missing_tail_samples": 0,
			"reference_clipped_samples": 0,
			"received_clipped_samples": 0,
			"qualification_limits": {
				"max_onset_loss_samples": 480,
				"max_end_loss_samples": 480,
				"require_complete_tail": True,
				"fail_on_new_clipping": True,
			},
			"passed": True,
		}
		edge_reference = write_json(case_root / "edge-fixed-timeline-score.json", edge_document)
		plan_case_sha256 = hashlib.sha256(f"plan:{case['profile']}:{case['case_id']}".encode("utf-8")).hexdigest()
		render_entry_sha256 = hashlib.sha256(f"render:{case['profile']}:{case['case_id']}".encode("utf-8")).hexdigest()
		case_binding_reference = write_json(case_root / "case-binding.json", {
			"schema_version": 1,
			"kind": "mumble-input-enhancement-case-binding-v1",
			"measurement_mode": "offline",
			"case_id": case["case_id"],
			"profile": case["profile"],
			"condition": case["condition"],
			"dataset_split": case["dataset_split"],
			"build_binding": {
				field: fixture["build"][field]
				for field in ("case_set_sha256", "corpus_inventory_sha256", "corpus_lock_sha256", "mixture_plan_sha256")
			},
			"plan_case_sha256": plan_case_sha256,
			"render_manifest_sha256": "9" * 64,
			"render_entry_sha256": render_entry_sha256,
			"source_input_sha256": document["inputs"]["noisy_original"]["sha256"],
			"clean_reference_sha256": document["inputs"]["clean_reference"]["sha256"],
		})
		case_index_entries.append({
			"case_id": case["case_id"],
			"profile": case["profile"],
			"condition": case["condition"],
			"dataset_split": case["dataset_split"],
			"measurement_mode": "offline",
			"plan_case_sha256": plan_case_sha256,
			"render_entry_sha256": render_entry_sha256,
			"source_input_sha256": document["inputs"]["noisy_original"]["sha256"],
			"clean_reference_sha256": document["inputs"]["clean_reference"]["sha256"],
			"reports": {
				"objective_score": objective_reference,
				"case_binding_report": case_binding_reference,
				"original_benchmark_report": original_reference,
				"candidate_benchmark_report": candidate_reference,
				"edge_fixed_timeline_score": edge_reference,
			},
		})

	objective_runtime_binding_sha256 = canonical_measurement_json_sha256({
		"runtime": objective_documents[0]["runtime"],
		"scorer_files": objective_documents[0]["scorer_files"],
	})
	runtime_manifest = objective_documents[0]["runtime"]["manifest"]
	metrics_runtime_files = [{
		"path": runtime_manifest["relative_path"],
		"sha256": runtime_manifest["sha256"],
		"size_bytes": runtime_manifest["size_bytes"],
	}]
	fixture["build"]["metrics_runtime_sha256"] = canonical_measurement_json_sha256(metrics_runtime_files)
	metrics_attestation_document = {
		"schema_version": 1,
		"kind": "mumble-audio-metrics-runtime-attestation-v1",
		"payload_kind": "directory",
		"payload_sha256": fixture["build"]["metrics_runtime_sha256"],
		"objective_runtime_binding_sha256": objective_runtime_binding_sha256,
		"files": metrics_runtime_files,
	}
	metrics_attestation_reference = write_json(
		root / artifact_prefix / "measurements" / "metrics-runtime-attestation.json",
		metrics_attestation_document,
	)
	def profile_binding(profile: str, engine: str, recipe_id: str) -> Mapping[str, Any]:
		models = []
		if engine in ("RNNoise", "DeepFilterNet"):
			models = [{
				"id": f"{engine.casefold()}:self-test",
				"sha256": fixture["build"]["model_hashes"][0],
				"version": "1",
			}]
		return {
			"profile": profile,
			"engine": engine,
			"recipe": {
				"catalog_revision": "self-test-v2", "id": recipe_id,
				"manifest_sha256": fixture["build"]["recipe_manifest_sha256"], "revision": 1,
			},
			"models": models,
		}
	if fixture["qualification_scope"] == "core":
		profile_bindings = [
			profile_binding("Original", "None", "input.original"),
			profile_binding("Light", "Speex", "input.light.speex"),
			profile_binding("Balanced", "RNNoise", "input.balanced.self-test"),
			profile_binding("Quality", "DeepFilterNet", "input.quality.self-test"),
			profile_binding("VoiceFocus", "DeepFilterNet", "input.voice-focus.self-test"),
		]
	else:
		profile_bindings = [
			profile_binding("Original", "None", "input.original"),
			profile_binding("Auto", "RNNoise", "input.auto.balanced.self-test"),
			profile_binding("Auto", "Speex", "input.auto.light.self-test"),
			profile_binding("Auto", "DeepFilterNet", "input.auto.quality.self-test"),
		]

	transition_index_entries = []
	for transition in sorted(transitions, key=lambda item: (AUTO_DIRECTED_PAIRS.index(str(item["directed_pair"])), str(item["case_id"]))):
		transition_path = root / artifact_prefix / "measurements" / "auto-transitions" / str(transition["case_id"]) / "transition-report.json"
		report = {
			"schema_version": 1,
			"kind": "mumble-input-enhancement-auto-transition-v1",
			"status": "completed",
			"case_id": transition["case_id"],
			"directed_pair": transition["directed_pair"],
			"execution_identity": execution_identity(),
			"fixed_timeline": transition["fixed_timeline"],
			"receiver_cleanup": transition["receiver_cleanup_enabled"],
			"startup_preroll_ms": transition["startup_preroll_ms"],
			"speech_edge_loss_samples": round(float(transition["metrics"]["speech_edge_loss_ms"]) * 48.0),
			"transition_processing_ms": transition["metrics"]["transition_processing_ms"],
			"deadline_miss_count": transition["counters"]["deadline_misses"],
			"fallback_count": transition["counters"]["unexplained_fallbacks"],
			"invalid_output_count": transition["counters"]["nan_or_inf_count"],
			"new_clipping_count": transition["counters"]["new_clipping_cases"],
			"memory_growth_bytes_after_warmup": transition["performance"]["memory_growth_bytes"],
			"soak_duration_seconds": transition["performance"]["soak_duration_seconds"],
		}
		transition_index_entries.append({
			"case_id": transition["case_id"],
			"directed_pair": transition["directed_pair"],
			"report": write_json(transition_path, report),
		})

	for name, artifact in fixture["artifacts"].items():
		if name == "measurement_index_json":
			continue
		path = root.joinpath(*PurePosixPath(artifact["path"]).parts)
		path.parent.mkdir(parents=True, exist_ok=True)
		if name == "case_evidence_jsonl":
			write_case_evidence(path, fixture["build"], fixture["qualification_scope"], fixture["suite"], cases, transitions)
		else:
			path.write_bytes(b"x")
		payload = path.read_bytes()
		artifact["sha256"] = hashlib.sha256(payload).hexdigest()
		artifact["size_bytes"] = len(payload)

	index = {
		"schema_version": 1,
		"kind": INDEX_KIND,
		"qualification_scope": fixture["qualification_scope"],
		"suite": fixture["suite"],
		"qualification_binding_sha256": qualification_binding_sha256(fixture["build"], fixture["qualification_scope"], fixture["suite"]),
		"build": copy.deepcopy(fixture["build"]),
		"plan_binding": {
			field: fixture["build"][field]
			for field in ("case_set_sha256", "corpus_inventory_sha256", "corpus_lock_sha256", "mixture_plan_sha256")
		},
		"objective_runtime_binding_sha256": objective_runtime_binding_sha256,
		"metrics_runtime_attestation": metrics_attestation_reference,
		"profile_bindings": profile_bindings,
		"published_artifacts": [
			{"name": name, "artifact": copy.deepcopy(fixture["artifacts"][name])}
			for name in sorted(fixture["artifacts"])
			if name != "measurement_index_json"
		],
		"cases": case_index_entries,
		"release_holdout_approval_public_key_sha256": None,
		"release_holdout_openings": [],
		"soak_reports": [],
		"transitions": transition_index_entries,
	}
	index_artifact = fixture["artifacts"]["measurement_index_json"]
	index_path = root.joinpath(*PurePosixPath(index_artifact["path"]).parts)
	index_reference = write_json(index_path, index, canonical=True)
	index_artifact["sha256"] = index_reference["sha256"]
	index_artifact["size_bytes"] = index_reference["size_bytes"]


def _expect_rejected(label: str, fixture: Mapping[str, Any], root: Path | None) -> None:
	try:
		validate_qualification(fixture, root)
	except QualificationError:
		return
	raise AssertionError(f"self-test accepted {label}")


def run_self_test() -> None:
	with tempfile.TemporaryDirectory() as directory:
		root = Path(directory) / "core"
		fixture, cases, transitions = _passing_fixture("core")
		language_skew = copy.deepcopy(cases)
		for record in language_skew:
			if record["profile"] == "Balanced" and record["condition"] == "clean":
				record["metrics"]["estoi_loss"] = 0.02 if record["language"] == "sv-SE" else 0.0
				record["metrics"]["dnsmos_sig_loss"] = 0.10 if record["language"] == "sv-SE" else 0.0
		language_summary = summarize_case_evidence(language_skew, transitions, "core", "pr_smoke")
		balanced_language_metrics = language_summary["profiles"]["Balanced"]["metrics"]
		if balanced_language_metrics["worst_language_clean_estoi_loss_median"] != 0.02 or balanced_language_metrics["worst_language_clean_dnsmos_sig_loss_median"] != 0.10:
			raise AssertionError("one failing clean-speech language was hidden by an aggregate median")
		wer_skew = copy.deepcopy(cases)
		for record in wer_skew:
			if record["profile"] != "Balanced":
				continue
			if record["condition"] == "clean":
				record["metrics"]["wer_loss_percentage_points"] = 2.0 if record["language"] == "sv-SE" else 0.0
			else:
				record["metrics"]["wer_loss_percentage_points"] = -10.0
		wer_summary = summarize_case_evidence(wer_skew, transitions, "core", "pr_smoke")
		if wer_summary["profiles"]["Balanced"]["metrics"]["worst_language_wer_loss_percentage_points"] != 2.0:
			raise AssertionError("noisy WER improvement hid a failing clean-speech language")
		collapsed = copy.deepcopy(cases)
		for record in collapsed:
			record["speaker_group_id"] = "speaker-repeated"
			record["rir_group_id"] = "room-repeated"
			record["device_group_id"] = "device-repeated"
			if record["condition"] != "clean":
				record["noise_group_id"] = "noise-repeated"
				record["noise_class"] = "noise-class-repeated"
		try:
			summarize_case_evidence(collapsed, transitions, "core", "pr_smoke")
		except CaseEvidenceError:
			pass
		else:
			raise AssertionError("repeating one source group was accepted as diverse qualification evidence")
		_materialize_fixture(root, fixture, cases, transitions)
		validate_qualification(fixture, root)

		def rewrite_indexed_report(
			test_root: Path,
			test_fixture: Mapping[str, Any],
			report_name: str,
			mutate: Any,
		) -> None:
			index_artifact = test_fixture["artifacts"]["measurement_index_json"]
			index_path = test_root.joinpath(*PurePosixPath(index_artifact["path"]).parts)
			index = json.loads(index_path.read_text(encoding="utf-8"))
			entry = next(item for item in index["cases"] if item["profile"] == "Balanced")
			reference = entry["reports"][report_name]
			report_path = test_root.joinpath(*PurePosixPath(reference["path"]).parts)
			report = json.loads(report_path.read_text(encoding="utf-8"))
			mutate(report)
			report_payload = (json.dumps(report, sort_keys=True, separators=(",", ":")) + "\n").encode("utf-8")
			report_path.write_bytes(report_payload)
			reference["sha256"] = hashlib.sha256(report_payload).hexdigest()
			reference["size_bytes"] = len(report_payload)
			index_payload = canonical_measurement_json_bytes(index) + b"\n"
			index_path.write_bytes(index_payload)
			index_artifact["sha256"] = hashlib.sha256(index_payload).hexdigest()
			index_artifact["size_bytes"] = len(index_payload)

		deadline_root = Path(directory) / "runtime-deadline-mismatch"
		deadline_fixture, deadline_cases, deadline_transitions = _passing_fixture("core")
		_materialize_fixture(deadline_root, deadline_fixture, deadline_cases, deadline_transitions)
		rewrite_indexed_report(
			deadline_root,
			deadline_fixture,
			"candidate_benchmark_report",
			lambda report: report.__setitem__("deadline_misses", 1),
		)
		_expect_rejected("case counters hiding a runtime deadline miss", deadline_fixture, deadline_root)

		latency_root = Path(directory) / "runtime-latency-mismatch"
		latency_fixture, latency_cases, latency_transitions = _passing_fixture("core")
		_materialize_fixture(latency_root, latency_fixture, latency_cases, latency_transitions)
		rewrite_indexed_report(
			latency_root,
			latency_fixture,
			"candidate_benchmark_report",
			lambda report: report.__setitem__("reported_latency_samples", int(report["reported_latency_samples"]) + 480),
		)
		_expect_rejected("case latency differing from the runtime report", latency_fixture, latency_root)

		private_path_root = Path(directory) / "private-path-report"
		private_path_fixture, private_path_cases, private_path_transitions = _passing_fixture("core")
		_materialize_fixture(private_path_root, private_path_fixture, private_path_cases, private_path_transitions)
		rewrite_indexed_report(
			private_path_root,
			private_path_fixture,
			"candidate_benchmark_report",
			lambda report: report.__setitem__("input_path", r"C:\\protected-audio\\private.wav"),
		)
		_expect_rejected("offline benchmark report leaking a private path", private_path_fixture, private_path_root)

		rtf_root = Path(directory) / "runtime-rtf-mismatch"
		rtf_fixture, rtf_cases, rtf_transitions = _passing_fixture("core")
		_materialize_fixture(rtf_root, rtf_fixture, rtf_cases, rtf_transitions)
		rewrite_indexed_report(
			rtf_root,
			rtf_fixture,
			"candidate_benchmark_report",
			lambda report: report.__setitem__("audio_ms", float(report["audio_ms"]) * 10.0),
		)
		_expect_rejected("benchmark audio duration understating RTF", rtf_fixture, rtf_root)

		recipe_root = Path(directory) / "runtime-recipe-mismatch"
		recipe_fixture, recipe_cases, recipe_transitions = _passing_fixture("core")
		_materialize_fixture(recipe_root, recipe_fixture, recipe_cases, recipe_transitions)
		rewrite_indexed_report(
			recipe_root,
			recipe_fixture,
			"candidate_benchmark_report",
			lambda report: report.__setitem__("requested_recipe_id", "input.balanced.wrong-model"),
		)
		_expect_rejected("profile measured with an unauthorized recipe/model binding", recipe_fixture, recipe_root)

		case_binding_root = Path(directory) / "case-plan-binding-mismatch"
		case_binding_fixture, case_binding_cases, case_binding_transitions = _passing_fixture("core")
		_materialize_fixture(case_binding_root, case_binding_fixture, case_binding_cases, case_binding_transitions)
		rewrite_indexed_report(
			case_binding_root,
			case_binding_fixture,
			"case_binding_report",
			lambda report: report.__setitem__("plan_case_sha256", "0" * 64),
		)
		_expect_rejected("offline report differing from the indexed plan case", case_binding_fixture, case_binding_root)

		metrics_root = Path(directory) / "metrics-runtime-mismatch"
		metrics_fixture, metrics_cases, metrics_transitions = _passing_fixture("core")
		_materialize_fixture(metrics_root, metrics_fixture, metrics_cases, metrics_transitions)
		metrics_index_artifact = metrics_fixture["artifacts"]["measurement_index_json"]
		metrics_index_path = metrics_root.joinpath(*PurePosixPath(metrics_index_artifact["path"]).parts)
		metrics_index = json.loads(metrics_index_path.read_text(encoding="utf-8"))
		metrics_reference = metrics_index["metrics_runtime_attestation"]
		metrics_report_path = metrics_root.joinpath(*PurePosixPath(metrics_reference["path"]).parts)
		metrics_report = json.loads(metrics_report_path.read_text(encoding="utf-8"))
		metrics_report["payload_sha256"] = "0" * 64
		metrics_report_payload = (json.dumps(metrics_report, sort_keys=True, separators=(",", ":")) + "\n").encode("utf-8")
		metrics_report_path.write_bytes(metrics_report_payload)
		metrics_reference["sha256"] = hashlib.sha256(metrics_report_payload).hexdigest()
		metrics_reference["size_bytes"] = len(metrics_report_payload)
		metrics_index_payload = canonical_measurement_json_bytes(metrics_index) + b"\n"
		metrics_index_path.write_bytes(metrics_index_payload)
		metrics_index_artifact["sha256"] = hashlib.sha256(metrics_index_payload).hexdigest()
		metrics_index_artifact["size_bytes"] = len(metrics_index_payload)
		_expect_rejected("metrics runtime attestation differing from the qualified build", metrics_fixture, metrics_root)

		_expect_rejected("missing artifact root", fixture, None)
		legacy = copy.deepcopy(fixture)
		legacy["schema_version"] = 2
		_expect_rejected("self-reported schema v2", legacy, root)
		for label, mutate in (
			("receiver cleanup summary", lambda value: value["coverage"].__setitem__("receiver_cleanup_cases", 1)),
			("clean median summary", lambda value: value["profiles"][2]["metrics"].__setitem__("worst_language_clean_estoi_loss_median", 0.02)),
			("catastrophe summary", lambda value: value["profiles"][2]["metrics"].__setitem__("catastrophe_rate_percent", 0.4)),
			("counter summary", lambda value: value["profiles"][2]["metrics"].__setitem__("deadline_misses", 1)),
			("performance summary", lambda value: value["profiles"][2]["performance"].__setitem__("average_rtf", 0.01)),
			("Original latency summary", lambda value: value["profiles"][0]["metrics"].__setitem__("algorithmic_latency_ms_max", 0.1)),
			("fixed alignment summary", lambda value: value["coverage"].__setitem__("fixed_timeline_cases", 23)),
			("legacy Crisp profile", lambda value: value["profiles"][3].__setitem__("profile", "Crisp")),
			("core Auto transitions", lambda value: value.__setitem__("auto_transitions", {})),
			("protected audio", lambda value: value["artifacts"]["junit"].__setitem__("contains_audio_samples", True)),
		):
			unsafe = copy.deepcopy(fixture)
			mutate(unsafe)
			_expect_rejected(label, unsafe, root)

		tampered = copy.deepcopy(fixture)
		evidence_path = root / "artifacts" / "pr_smoke-local-development" / "case-evidence.jsonl"
		evidence_path.write_bytes(evidence_path.read_bytes().replace(b'":', b'": ', 1))
		payload = evidence_path.read_bytes()
		tampered["artifacts"]["case_evidence_jsonl"]["sha256"] = hashlib.sha256(payload).hexdigest()
		tampered["artifacts"]["case_evidence_jsonl"]["size_bytes"] = len(payload)
		_expect_rejected("non-canonical rehashed JSONL", tampered, root)

		case_root = Path(directory) / "case-mismatch"
		case_fixture, altered_cases, altered_transitions = _passing_fixture("core")
		altered_cases = copy.deepcopy(altered_cases)
		altered_cases[12]["metrics"]["dnsmos_ovrl_improvement"] = -1.0
		_materialize_fixture(case_root, case_fixture, altered_cases, altered_transitions)
		_expect_rejected("rehashed case values with stale good summaries", case_fixture, case_root)

		auto_root = Path(directory) / "auto"
		auto_fixture, auto_cases, auto_transitions = _passing_fixture("auto")
		_materialize_fixture(auto_root, auto_fixture, auto_cases, auto_transitions)
		validate_qualification(auto_fixture, auto_root)
		for label, mutate in (
			("missing Auto transition summary", lambda value: value["auto_transitions"]["directed_pairs"].pop()),
			("Auto transition counter summary", lambda value: value["auto_transitions"].__setitem__("deadline_misses", 1)),
			("Auto transition performance summary", lambda value: value["auto_transitions"].__setitem__("max_transition_processing_ms", 1.0)),
			("Auto transition self-pass mismatch", lambda value: value["auto_transitions"].__setitem__("passed", False)),
		):
			unsafe = copy.deepcopy(auto_fixture)
			mutate(unsafe)
			_expect_rejected(label, unsafe, auto_root)


def main(argv: Sequence[str] | None = None) -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("qualification", nargs="?", type=Path)
	parser.add_argument("--artifact-root", type=Path)
	parser.add_argument("--self-test", action="store_true")
	args = parser.parse_args(argv)
	try:
		if args.self_test:
			run_self_test()
			print("quality qualification validator self-test: ok")
			if args.qualification is None:
				return 0
		if args.qualification is None:
			raise QualificationError("qualification path is required")
		artifact_root = args.artifact_root if args.artifact_root is not None else args.qualification.parent
		qualification = validate_qualification(_load_json(args.qualification), artifact_root)
		print(
			f"quality qualification: ok; suite={qualification['suite']}; status={qualification['status']}; "
			f"cases={qualification['coverage']['case_count']}"
		)
		return 0
	except (QualificationError, AssertionError) as error:
		print(f"quality qualification: error: {error}", file=sys.stderr)
		return 1


if __name__ == "__main__":
	sys.exit(main())
