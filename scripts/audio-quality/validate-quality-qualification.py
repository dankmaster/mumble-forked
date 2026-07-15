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


class QualificationError(ValueError):
	"""Raised when qualification evidence is incomplete, unsafe, or below gate."""


PROFILES = ("Original", "Light", "Balanced", "Crisp", "Auto")
LATENCY_GATES_MS = { "Original": 0.0, "Light": 10.0, "Balanced": 30.0, "Crisp": 50.0, "Auto": 50.0 }
SUITE_CASES = { "pr_smoke": (24, 24), "master_quality": (500, None), "nightly": (5000, None), "release": (12, 12) }
NOISY_GATES = {
	"Light": (0.10, 0.20),
	"Balanced": (0.15, 0.30),
	"Crisp": (0.20, 0.40),
	"Auto": (0.10, 0.20),
}
ARTIFACT_NAMES = (
	"failure_spectrogram_index",
	"junit",
	"per_case_csv",
	"per_case_parquet",
	"summary_html",
	"summary_json",
)


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


def validate_qualification(value: Any, artifact_root: Path | None = None) -> Mapping[str, Any]:
	root = _mapping(value, "qualification")
	_exact_keys(
		root,
		{
			"artifacts", "build", "coverage", "generated_at_utc", "profiles", "schema_version", "status",
			"suite", "violations",
		},
		"qualification",
	)
	_expect(root["schema_version"] == 1, "qualification.schema_version", "unsupported version")
	_expect(root["suite"] in SUITE_CASES, "qualification.suite", "unknown suite")
	_expect(root["status"] in ("passed", "failed"), "qualification.status", "unknown status")
	try:
		stamp = dt.datetime.fromisoformat(str(root["generated_at_utc"]).replace("Z", "+00:00"))
	except ValueError as error:
		raise QualificationError("qualification.generated_at_utc: invalid ISO-8601 timestamp") from error
	_expect(stamp.tzinfo is not None and stamp.utcoffset() == dt.timedelta(0), "qualification.generated_at_utc", "must be UTC")

	build = _mapping(root["build"], "qualification.build")
	_exact_keys(
		build,
		{
			"corpus_lock_sha256", "git_sha", "mixture_plan_sha256", "model_hashes", "recipe_set_version",
			"tested_binary_sha256",
		},
		"qualification.build",
	)
	_hash(build["git_sha"], "qualification.build.git_sha", 40)
	for key in ("tested_binary_sha256", "corpus_lock_sha256", "mixture_plan_sha256"):
		_hash(build[key], f"qualification.build.{key}", 64)
	_expect(isinstance(build["recipe_set_version"], str) and bool(build["recipe_set_version"]), "qualification.build.recipe_set_version", "required")
	_expect(isinstance(build["model_hashes"], list) and build["model_hashes"], "qualification.build.model_hashes", "expected a non-empty array")
	for index, digest in enumerate(build["model_hashes"]):
		_hash(digest, f"qualification.build.model_hashes[{index}]", 64)
	_expect(build["model_hashes"] == sorted(set(build["model_hashes"])), "qualification.build.model_hashes", "must be sorted and unique")

	coverage = _mapping(root["coverage"], "qualification.coverage")
	_exact_keys(
		coverage,
		{
			"case_count", "cold_start_cases", "failed_case_count", "fixed_timeline_cases", "languages",
			"receiver_cleanup_cases", "warm_start_cases",
		},
		"qualification.coverage",
	)
	case_count = _integer(coverage["case_count"], "qualification.coverage.case_count", 1)
	minimum, maximum = SUITE_CASES[root["suite"]]
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

	profiles = root["profiles"]
	_expect(isinstance(profiles, list), "qualification.profiles", "expected an array")
	profile_names = [ profile.get("profile") if isinstance(profile, dict) else None for profile in profiles ]
	_expect(profile_names == list(PROFILES), "qualification.profiles", "must contain Original, Light, Balanced, Crisp, Auto in order")
	all_profiles_passed = True
	profile_case_total = 0
	for index, profile_value in enumerate(profiles):
		path = f"qualification.profiles[{index}]"
		profile = _mapping(profile_value, path)
		_exact_keys(profile, { "case_count", "metrics", "passed", "performance", "profile" }, path)
		name = profile["profile"]
		profile_case_total += _integer(profile["case_count"], f"{path}.case_count", 1)
		_expect(isinstance(profile["passed"], bool), f"{path}.passed", "expected boolean")
		metrics = _mapping(profile["metrics"], f"{path}.metrics")
		metric_keys = {
			"algorithmic_latency_ms_max", "catastrophe_rate_percent", "clean_dnsmos_sig_loss_median",
			"clean_estoi_loss_median", "deadline_misses", "latency_attestation_failures",
			"max_speech_edge_loss_ms", "model_hash_errors", "nan_or_inf_count", "new_clipping_cases",
			"noisy_dnsmos_bak_improvement_median", "noisy_dnsmos_ovrl_improvement_median",
			"tail_drain_failures", "unexplained_fallbacks", "worst_cohort_ovrl_loss_median",
			"worst_language_wer_loss_percentage_points",
		}
		_exact_keys(metrics, metric_keys, f"{path}.metrics")
		clean_estoi = _number(metrics["clean_estoi_loss_median"], f"{path}.metrics.clean_estoi_loss_median")
		clean_sig = _number(metrics["clean_dnsmos_sig_loss_median"], f"{path}.metrics.clean_dnsmos_sig_loss_median")
		wer_loss = _number(metrics["worst_language_wer_loss_percentage_points"], f"{path}.metrics.worst_language_wer_loss_percentage_points")
		ovrl = _number(metrics["noisy_dnsmos_ovrl_improvement_median"], f"{path}.metrics.noisy_dnsmos_ovrl_improvement_median")
		bak = _number(metrics["noisy_dnsmos_bak_improvement_median"], f"{path}.metrics.noisy_dnsmos_bak_improvement_median")
		cohort_loss = _number(metrics["worst_cohort_ovrl_loss_median"], f"{path}.metrics.worst_cohort_ovrl_loss_median")
		catastrophe = _number(metrics["catastrophe_rate_percent"], f"{path}.metrics.catastrophe_rate_percent", 0)
		edge_loss = _number(metrics["max_speech_edge_loss_ms"], f"{path}.metrics.max_speech_edge_loss_ms", 0)
		latency = _number(metrics["algorithmic_latency_ms_max"], f"{path}.metrics.algorithmic_latency_ms_max", 0)
		metric_passed = clean_estoi <= 0.01 and clean_sig <= 0.05 and wer_loss <= 1.0
		metric_passed = (
			metric_passed and cohort_loss <= 0.10 and catastrophe <= 0.5 and edge_loss <= 10.0
			and latency <= LATENCY_GATES_MS[name]
		)
		for key in (
			"deadline_misses", "latency_attestation_failures", "model_hash_errors", "nan_or_inf_count",
			"new_clipping_cases", "tail_drain_failures", "unexplained_fallbacks",
		):
			metric_passed = metric_passed and _integer(metrics[key], f"{path}.metrics.{key}", 0) == 0
		if name in NOISY_GATES:
			minimum_ovrl, minimum_bak = NOISY_GATES[name]
			metric_passed = metric_passed and ovrl >= minimum_ovrl and bak >= minimum_bak

		performance = _mapping(profile["performance"], f"{path}.performance")
		performance_keys = {
			"average_rtf", "max_internal_processing_ms", "memory_growth_bytes", "p99_callback_ms",
			"soak_duration_seconds",
		}
		_exact_keys(performance, performance_keys, f"{path}.performance")
		average_rtf = _number(performance["average_rtf"], f"{path}.performance.average_rtf", 0)
		p99 = _number(performance["p99_callback_ms"], f"{path}.performance.p99_callback_ms", 0)
		max_internal = _number(performance["max_internal_processing_ms"], f"{path}.performance.max_internal_processing_ms", 0)
		memory_growth = _integer(performance["memory_growth_bytes"], f"{path}.performance.memory_growth_bytes")
		soak_duration = _integer(performance["soak_duration_seconds"], f"{path}.performance.soak_duration_seconds", 0)
		performance_passed = True
		if name == "Balanced":
			performance_passed = average_rtf <= 0.15 and p99 <= 5.0
		elif name == "Crisp":
			performance_passed = average_rtf <= 0.35 and p99 <= 8.0
		if root["suite"] == "nightly" and name in ("Balanced", "Crisp"):
			performance_passed = performance_passed and soak_duration >= 3600 and max_internal <= 10.0 and memory_growth <= 0
		expected_profile_pass = metric_passed and performance_passed
		_expect(profile["passed"] == expected_profile_pass, f"{path}.passed", "does not match semantic gates")
		all_profiles_passed = all_profiles_passed and profile["passed"]
	_expect(profile_case_total == case_count, "qualification.profiles", "profile case counts must sum to coverage.case_count")

	artifacts = _mapping(root["artifacts"], "qualification.artifacts")
	_exact_keys(artifacts, set(ARTIFACT_NAMES), "qualification.artifacts")
	resolved_root = artifact_root.resolve() if artifact_root is not None else None
	for name in ARTIFACT_NAMES:
		path = f"qualification.artifacts.{name}"
		artifact = _mapping(artifacts[name], path)
		_exact_keys(artifact, { "contains_audio_samples", "path", "sha256", "size_bytes" }, path)
		relative = _safe_relative_path(artifact["path"], f"{path}.path")
		digest = _hash(artifact["sha256"], f"{path}.sha256", 64)
		size = _integer(artifact["size_bytes"], f"{path}.size_bytes", 0)
		_expect(artifact["contains_audio_samples"] is False, f"{path}.contains_audio_samples", "raw or encoded audio must not be published")
		if resolved_root is not None:
			actual = resolved_root.joinpath(*PurePosixPath(relative).parts).resolve()
			try:
				actual.relative_to(resolved_root)
			except ValueError as error:
				raise QualificationError(f"{path}.path: escapes artifact root") from error
			_expect(actual.is_file(), f"{path}.path", f"artifact is missing: {actual}")
			_expect(actual.stat().st_size == size, f"{path}.size_bytes", "artifact size mismatch")
			_expect(_sha256(actual) == digest, f"{path}.sha256", "artifact hash mismatch")

	_expect(isinstance(root["violations"], list), "qualification.violations", "expected an array")
	_expect(all(isinstance(item, str) and bool(item) for item in root["violations"]), "qualification.violations", "entries must be non-empty strings")
	semantic_pass = failed_cases == 0 and all_profiles_passed and not root["violations"]
	_expect(root["status"] == ("passed" if semantic_pass else "failed"), "qualification.status", "does not match semantic gates")
	return root


def _passing_fixture() -> Mapping[str, Any]:
	metrics = {
		"algorithmic_latency_ms_max": 0.0,
		"clean_estoi_loss_median": 0.005,
		"clean_dnsmos_sig_loss_median": 0.02,
		"worst_language_wer_loss_percentage_points": 0.5,
		"noisy_dnsmos_ovrl_improvement_median": 0.25,
		"noisy_dnsmos_bak_improvement_median": 0.45,
		"worst_cohort_ovrl_loss_median": 0.05,
		"catastrophe_rate_percent": 0.1,
		"max_speech_edge_loss_ms": 10.0,
		"nan_or_inf_count": 0,
		"new_clipping_cases": 0,
		"unexplained_fallbacks": 0,
		"model_hash_errors": 0,
		"deadline_misses": 0,
		"latency_attestation_failures": 0,
		"tail_drain_failures": 0,
	}
	performance = {
		"average_rtf": 0.10,
		"p99_callback_ms": 4.0,
		"max_internal_processing_ms": 9.0,
		"memory_growth_bytes": 0,
		"soak_duration_seconds": 0,
	}
	artifacts = {
		name: {
			"path": f"artifacts/{name}.dat",
			"sha256": "3" * 64,
			"size_bytes": 1,
			"contains_audio_samples": False,
		}
		for name in ARTIFACT_NAMES
	}
	return {
		"schema_version": 1,
		"suite": "pr_smoke",
		"status": "passed",
		"generated_at_utc": "2026-07-14T20:00:00Z",
		"build": {
			"git_sha": "1" * 40,
			"tested_binary_sha256": "2" * 64,
			"corpus_lock_sha256": "3" * 64,
			"mixture_plan_sha256": "4" * 64,
			"recipe_set_version": "recipes-v1",
			"model_hashes": [ "5" * 64 ],
		},
		"coverage": {
			"case_count": 24,
			"failed_case_count": 0,
			"cold_start_cases": 12,
			"warm_start_cases": 12,
			"fixed_timeline_cases": 24,
			"receiver_cleanup_cases": 0,
			"languages": [ "en-US", "sv-SE" ],
		},
		"profiles": [
			{
				"profile": name,
				"case_count": 5 if index < 4 else 4,
				"passed": True,
				"metrics": {
					**copy.deepcopy(metrics),
					"algorithmic_latency_ms_max": (0.0, 10.0, 30.0, 50.0, 50.0)[index],
				},
				"performance": copy.deepcopy(performance),
			}
			for index, name in enumerate(PROFILES)
		],
		"artifacts": artifacts,
		"violations": [],
	}


def run_self_test() -> None:
	fixture = _passing_fixture()
	validate_qualification(fixture)
	for label, mutate in (
		("receiver cleanup", lambda value: value["coverage"].__setitem__("receiver_cleanup_cases", 1)),
		("clean speech loss", lambda value: value["profiles"][2]["metrics"].__setitem__("clean_estoi_loss_median", 0.02)),
		("Original latency", lambda value: value["profiles"][0]["metrics"].__setitem__("algorithmic_latency_ms_max", 0.1)),
		("deadline miss", lambda value: value["profiles"][2]["metrics"].__setitem__("deadline_misses", 1)),
		("fixed alignment", lambda value: value["coverage"].__setitem__("fixed_timeline_cases", 23)),
		("protected audio", lambda value: value["artifacts"]["junit"].__setitem__("contains_audio_samples", True)),
	):
		unsafe = copy.deepcopy(fixture)
		mutate(unsafe)
		try:
			validate_qualification(unsafe)
		except QualificationError:
			pass
		else:
			raise AssertionError(f"self-test accepted {label}")
	with tempfile.TemporaryDirectory() as directory:
		root = Path(directory)
		with_artifacts = copy.deepcopy(fixture)
		for artifact in with_artifacts["artifacts"].values():
			path = root.joinpath(*PurePosixPath(artifact["path"]).parts)
			path.parent.mkdir(parents=True, exist_ok=True)
			path.write_bytes(b"x")
			artifact["sha256"] = hashlib.sha256(b"x").hexdigest()
		validate_qualification(with_artifacts, root)


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
		qualification = validate_qualification(_load_json(args.qualification), args.artifact_root)
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
