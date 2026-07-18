#!/usr/bin/env python3
"""Tune product input-enhancement recipes on the protected tuning split.

The normal quality plan deliberately distributes controls over scenes so it can
cover the complete product surface.  That coverage is not a fair recipe
selection experiment: a control must be compared on the same source audio as
every other control.  This tool builds that missing crossed matrix from an
already validated/rendered *tuning* plan, runs the production benchmark and the
pinned objective scorer, and selects only candidates that satisfy the clean,
catastrophe and noisy gates.

Runs are hash-bound and resumable.  Validation and holdout plans are rejected.
The generated WAV files are private local evidence and must not be uploaded.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import importlib.util
import json
import math
import os
import shutil
import statistics
import subprocess
import sys
from pathlib import Path, PurePosixPath
from typing import Any, Callable, Mapping, MutableMapping, Sequence


SCRIPT_DIR = Path(__file__).resolve().parent
SCHEMA_VERSION = 1
CAMPAIGN_ID = "mumble-input-enhancement-recipe-tuning-v1"
PROFILES = ("Light", "Balanced", "Quality", "VoiceFocus")
CONDITIONS = ("clean", "noisy", "severe")
HEX64 = frozenset("0123456789abcdef")
PROFILE_NOISY_GATES = {
	"Light": {"dnsmos_ovrl": 0.10, "dnsmos_bak": 0.20},
	"Balanced": {"dnsmos_ovrl": 0.15, "dnsmos_bak": 0.30},
	"Quality": {"dnsmos_ovrl": 0.20, "dnsmos_bak": 0.40},
	"VoiceFocus": {"dnsmos_ovrl": 0.20, "dnsmos_bak": 0.50},
}
FREEZE_MINIMUM_SCENES = {"clean": 6, "noisy": 12, "severe": 12}


class TuningError(ValueError):
	"""Raised when tuning evidence is incomplete, unsafe or inconsistent."""


def _load_script(name: str, module_name: str) -> Any:
	path = SCRIPT_DIR / name
	spec = importlib.util.spec_from_file_location(module_name, path)
	if spec is None or spec.loader is None:
		raise TuningError(f"unable to load required module: {path}")
	module = importlib.util.module_from_spec(spec)
	spec.loader.exec_module(module)
	return module


PLAN = _load_script("generate-mixture-plan.py", "mumble_recipe_tuning_plan")
OFFLINE = _load_script("run-offline-quality-campaign.py", "mumble_recipe_tuning_offline")
OBJECTIVE = _load_script("objective_quality_score.py", "mumble_recipe_tuning_objective")


def _expect(condition: bool, path: str, message: str) -> None:
	if not condition:
		raise TuningError(f"{path}: {message}")


def _canonical_bytes(value: Any) -> bytes:
	return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")


def _canonical_sha256(value: Any) -> str:
	return hashlib.sha256(_canonical_bytes(value)).hexdigest()


def _sha256(path: Path) -> str:
	digest = hashlib.sha256()
	with path.open("rb") as stream:
		for block in iter(lambda: stream.read(1024 * 1024), b""):
			digest.update(block)
	return digest.hexdigest()


def _load_json(path: Path, label: str) -> Any:
	def reject_duplicates(pairs: Sequence[tuple[str, Any]]) -> MutableMapping[str, Any]:
		value: MutableMapping[str, Any] = {}
		for key, item in pairs:
			if key in value:
				raise TuningError(f"{label}: duplicate key {key!r}")
			value[key] = item
		return value

	try:
		return json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=reject_duplicates)
	except (OSError, UnicodeError, json.JSONDecodeError) as error:
		raise TuningError(f"{label}: unable to read {path}: {error}") from error


def _write_json_atomic(path: Path, value: Any) -> None:
	path.parent.mkdir(parents=True, exist_ok=True)
	temporary = path.with_name(f".{path.name}.{os.getpid()}.tmp")
	temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
	os.replace(temporary, path)


def _file_record(path: Path, *, relative_to: Path | None = None) -> dict[str, Any]:
	path = path.resolve()
	_expect(path.is_file() and not OFFLINE._is_reparse(path), str(path), "missing regular non-reparse file")
	record: dict[str, Any] = {"sha256": _sha256(path), "size_bytes": path.stat().st_size}
	if relative_to is not None:
		record["relative_path"] = path.relative_to(relative_to.resolve()).as_posix()
	return record


def _grid_axis(minimum: int, maximum: int, step: int) -> tuple[int, ...]:
	_expect(step >= 5 and step % 5 == 0, "grid step", "must be a positive multiple of five")
	_expect(minimum % 5 == 0 and maximum % 5 == 0 and minimum <= maximum, "recipe range", "must use five-point coordinates")
	values = list(range(minimum, maximum + 1, step))
	if values[-1] != maximum:
		values.append(maximum)
	return tuple(values)


def candidate_grid(profile: str, step: int) -> list[dict[str, Any]]:
	_expect(profile in PROFILES, "profile", "unsupported profile")
	ranges = PLAN.PROFILE_RECIPE_CONTROL_RANGES[profile]
	reductions = _grid_axis(*ranges["noise_reduction"], step)
	characters = _grid_axis(*ranges["natural_clear"], step)
	candidates = []
	for reduction in reductions:
		for character in characters:
			ui = {
				"noise_reduction": PLAN.nearest_ui_control_for_recipe(profile, "noise_reduction", reduction),
				"natural_clear": PLAN.nearest_ui_control_for_recipe(profile, "natural_clear", character),
			}
			validated = PLAN.validated_recipe_controls(profile, ui)
			_expect(validated == {"noise_reduction": reduction, "natural_clear": character}, profile, "UI inverse did not round-trip")
			candidates.append({
				"profile": profile,
				"recipe_controls": validated,
				"ui_controls": ui,
				"candidate_id": f"{profile.lower()}-r{reduction:03d}-c{character:03d}",
			})
	return candidates


def _condition(case: Mapping[str, Any]) -> str:
	if case["noise"] is None:
		return "clean"
	return "severe" if float(case["mix"]["snr_db"]) <= 0.0 else "noisy"


def _stable_order(seed: str, profile: str, condition: str, case: Mapping[str, Any]) -> str:
	return hashlib.sha256(f"{seed}\0{profile}\0{condition}\0{case['case_id']}".encode("utf-8")).hexdigest()


def _round_robin_languages(cases: Sequence[Mapping[str, Any]], limit: int, seed: str, profile: str, condition: str) -> list[Mapping[str, Any]]:
	by_language: dict[str, list[Mapping[str, Any]]] = {}
	for case in cases:
		language = OFFLINE.LANGUAGE_MAP[str(case["speech"]["language"])]
		by_language.setdefault(language, []).append(case)
	for values in by_language.values():
		values.sort(key=lambda case: _stable_order(seed, profile, condition, case))
	languages = sorted(by_language)
	selected: list[Mapping[str, Any]] = []
	while len(selected) < min(limit, len(cases)):
		progress = False
		for language in languages:
			values = by_language[language]
			if values:
				selected.append(values.pop(0))
				progress = True
				if len(selected) == min(limit, len(cases)):
					break
		if not progress:
			break
	return selected


def select_scenes(plan: Mapping[str, Any], seed: str, limits: Mapping[str, int]) -> dict[str, list[Mapping[str, Any]]]:
	selected: dict[str, list[Mapping[str, Any]]] = {}
	for profile in ("Light", "Balanced"):
		profile_cases = [case for case in plan["cases"] if case["profile"] == profile]
		result: list[Mapping[str, Any]] = []
		for condition in CONDITIONS:
			cohort = [case for case in profile_cases if _condition(case) == condition]
			_expect(bool(cohort), f"{profile}.{condition}", "tuning plan has no scenes")
			result.extend(_round_robin_languages(cohort, limits[condition], seed, profile, condition))
		selected[profile] = sorted(result, key=lambda case: str(case["case_id"]))

	# Quality and Voice Focus must be selected as exact paired scenes. Selecting
	# the two profiles independently would make the severe BAK comparison depend
	# on unrelated speech/noise even though the plan provides protected pairs.
	pairs: dict[str, dict[str, Mapping[str, Any]]] = {}
	for case in plan["cases"]:
		if case["profile"] not in ("Quality", "VoiceFocus"):
			continue
		comparison_scene_id = case.get("comparison_scene_id")
		_expect(isinstance(comparison_scene_id, str) and bool(comparison_scene_id), str(case["case_id"]), "missing paired scene id")
		pairs.setdefault(comparison_scene_id, {})[str(case["profile"])] = case
	_expect(bool(pairs) and all(set(pair) == {"Quality", "VoiceFocus"} for pair in pairs.values()), "Quality/VoiceFocus pairs", "plan is incomplete")
	quality_result: list[Mapping[str, Any]] = []
	voice_result: list[Mapping[str, Any]] = []
	representatives = [pair["Quality"] for pair in pairs.values()]
	for condition in CONDITIONS:
		cohort = [case for case in representatives if _condition(case) == condition]
		_expect(bool(cohort), f"Quality/VoiceFocus.{condition}", "tuning plan has no paired scenes")
		chosen = _round_robin_languages(cohort, limits[condition], seed, "QualityVoiceFocus", condition)
		for quality in chosen:
			comparison_scene_id = str(quality["comparison_scene_id"])
			quality_result.append(quality)
			voice_result.append(pairs[comparison_scene_id]["VoiceFocus"])
	selected["Quality"] = sorted(quality_result, key=lambda case: str(case["case_id"]))
	selected["VoiceFocus"] = sorted(voice_result, key=lambda case: str(case["case_id"]))
	_expect(
		{str(case["comparison_scene_id"]) for case in selected["Quality"]}
		== {str(case["comparison_scene_id"]) for case in selected["VoiceFocus"]},
		"Quality/VoiceFocus selection", "paired selection drifted",
	)
	return selected


def _task_document(candidate: Mapping[str, Any], case: Mapping[str, Any]) -> dict[str, Any]:
	return {
		"kind": "product-recipe",
		"task_id": f"{candidate['candidate_id']}--{case['case_id']}",
		"candidate_id": candidate["candidate_id"],
		"profile": candidate["profile"],
		"recipe_controls": candidate["recipe_controls"],
		"ui_controls": candidate["ui_controls"],
		"source_case_id": case["case_id"],
		"comparison_scene_id": case.get("comparison_scene_id"),
		"condition": _condition(case),
		"language": OFFLINE.LANGUAGE_MAP[str(case["speech"]["language"])],
		"noise_class": case["noise"]["class"] if case["noise"] is not None else "clean",
		"microphone_family": case["mix"]["microphone_response"]["device_family"],
	}


def _candidate_from_recipe_controls(profile: str, reduction: int, character: int) -> dict[str, Any]:
	_expect(
		reduction in PLAN.PROFILE_RECIPE_CONTROL_GRID[profile]["noise_reduction"]
		and character in PLAN.PROFILE_RECIPE_CONTROL_GRID[profile]["natural_clear"],
		f"candidate set.{profile}", "controls must belong to the qualified five-point recipe grid",
	)
	ui = {
		"noise_reduction": PLAN.nearest_ui_control_for_recipe(profile, "noise_reduction", reduction),
		"natural_clear": PLAN.nearest_ui_control_for_recipe(profile, "natural_clear", character),
	}
	_expect(
		PLAN.validated_recipe_controls(profile, ui) == {"noise_reduction": reduction, "natural_clear": character},
		f"candidate set.{profile}", "controls do not round-trip through persisted UI space",
	)
	return {
		"profile": profile, "recipe_controls": {"noise_reduction": reduction, "natural_clear": character},
		"ui_controls": ui, "candidate_id": f"{profile.lower()}-r{reduction:03d}-c{character:03d}",
	}


def load_candidate_set(path: Path) -> tuple[Mapping[str, Any], dict[str, list[dict[str, Any]]]]:
	document = _load_json(path.resolve(), "candidate set")
	_expect(isinstance(document, dict) and set(document) == {"schema_version", "dataset_split", "candidates"}, "candidate set", "invalid keys")
	_expect(document["schema_version"] == 1 and document["dataset_split"] == "tuning", "candidate set", "must be schema 1 on tuning")
	value = document["candidates"]
	_expect(isinstance(value, dict) and set(value) == set(PROFILES), "candidate set.candidates", "must define every core enhanced profile")
	result: dict[str, list[dict[str, Any]]] = {}
	for profile in PROFILES:
		rows = value[profile]
		_expect(isinstance(rows, list) and bool(rows), f"candidate set.{profile}", "must be a non-empty array")
		candidates = []
		seen: set[tuple[int, int]] = set()
		for index, row in enumerate(rows):
			_expect(isinstance(row, dict) and set(row) == {"noise_reduction", "natural_clear"}, f"candidate set.{profile}[{index}]", "invalid controls")
			reduction = row["noise_reduction"]
			character = row["natural_clear"]
			_expect(isinstance(reduction, int) and not isinstance(reduction, bool) and isinstance(character, int) and not isinstance(character, bool), f"candidate set.{profile}[{index}]", "controls must be integers")
			_expect((reduction, character) not in seen, f"candidate set.{profile}[{index}]", "duplicate controls")
			seen.add((reduction, character))
			candidates.append(_candidate_from_recipe_controls(profile, reduction, character))
		result[profile] = candidates
	return document, result


def build_tasks(
	plan: Mapping[str, Any], scenes: Mapping[str, Sequence[Mapping[str, Any]]], step: int,
	candidates_by_profile: Mapping[str, Sequence[Mapping[str, Any]]] | None = None,
) -> list[dict[str, Any]]:
	tasks = []
	for profile in PROFILES:
		candidates = candidates_by_profile[profile] if candidates_by_profile is not None else candidate_grid(profile, step)
		for candidate in candidates:
			for case in scenes[profile]:
				tasks.append(_task_document(candidate, case))
	return tasks


def _artifact_record(path: Path, attempt_root: Path) -> dict[str, Any]:
	return _file_record(path, relative_to=attempt_root)


def _validate_resumable(result_path: Path, attempt_root: Path, task_binding: Mapping[str, Any], task_binding_sha256: str) -> Mapping[str, Any] | None:
	try:
		document = _load_json(result_path, "task result")
		if document.get("schema_version") != SCHEMA_VERSION or document.get("campaign") != CAMPAIGN_ID or document.get("status") != "passed":
			return None
		if document.get("task_binding_sha256") != task_binding_sha256 or document.get("task_binding") != task_binding:
			return None
		artifacts = document.get("artifacts")
		if not isinstance(artifacts, dict):
			return None
		for name, record in artifacts.items():
			if not isinstance(record, dict) or set(record) != {"relative_path", "sha256", "size_bytes"}:
				return None
			relative = PurePosixPath(str(record["relative_path"]))
			if relative.is_absolute() or ".." in relative.parts or "." in relative.parts:
				return None
			path = attempt_root.joinpath(*relative.parts)
			if not path.is_file() or OFFLINE._is_reparse(path) or _sha256(path) != record["sha256"] or path.stat().st_size != record["size_bytes"]:
				return None
		return document
	except (KeyError, OSError, TuningError, TypeError, ValueError):
		return None


def _find_or_create_attempt(output_root: Path, task_id: str, binding: Mapping[str, Any], binding_sha256: str) -> tuple[Mapping[str, Any] | None, Path, int]:
	task_root = output_root / "tasks" / task_id
	task_root.mkdir(parents=True, exist_ok=True)
	attempts = sorted(path for path in task_root.glob("attempt-*") if path.is_dir())
	for attempt_root in reversed(attempts):
		result_path = attempt_root / "result.json"
		if result_path.is_file():
			resumable = _validate_resumable(result_path, attempt_root, binding, binding_sha256)
			if resumable is not None:
				return resumable, attempt_root, int(attempt_root.name.split("-")[-1])
	number = max((int(path.name.split("-")[-1]) for path in attempts), default=0) + 1
	attempt_root = task_root / f"attempt-{number:04d}"
	attempt_root.mkdir()
	return None, attempt_root, number


def _direct_model_arguments(model: Mapping[str, Any], model_path: Path, mix: float, input_path: Path, clean_path: Path, output_path: Path, report_path: Path) -> list[str]:
	return [
		"--backend", "DeepFilterNet", "--model-id", str(model["id"]),
		"--custom-model-path", str(model_path), "--mix-factor", f"{mix:.6f}",
		"--input", str(input_path), "--clean-reference", str(clean_path),
		"--output", str(output_path), "--report", str(report_path),
	]


def _validate_direct_intrinsic_latency(model: Mapping[str, Any], direct_latency_samples: int) -> int:
	descriptor_intrinsic_latency = OFFLINE._milliseconds_to_samples(
		model["algorithmicLatencyMs"], f"model {model['id']}.algorithmicLatencyMs",
	)
	_expect(direct_latency_samples >= OFFLINE.FRAME_SAMPLES, "model comparison.latency", "missing direct-adapter collection frame")
	measured_intrinsic_latency = direct_latency_samples - OFFLINE.FRAME_SAMPLES
	_expect(
		measured_intrinsic_latency == descriptor_intrinsic_latency,
		"model comparison.latency",
		f"descriptor intrinsic {descriptor_intrinsic_latency} samples does not match direct report intrinsic {measured_intrinsic_latency}",
	)
	return descriptor_intrinsic_latency


def _model_product_projection(
	model: Mapping[str, Any], projection_recipe: Mapping[str, Any], direct_latency_samples: int,
) -> Mapping[str, Any]:
	intrinsic_samples = _validate_direct_intrinsic_latency(model, direct_latency_samples)
	execution_semantics_version = int(projection_recipe["executionSemanticsVersion"])
	_expect(
		execution_semantics_version >= OFFLINE.MIN_SUPPORTED_EXECUTION_SEMANTICS_VERSION,
		"model comparison execution semantics", "predates the causal-tail contract",
	)
	worker_frames = 2 if execution_semantics_version >= 5 else 1
	worker_samples = worker_frames * OFFLINE.FRAME_SAMPLES
	projected_samples = intrinsic_samples + worker_samples
	budget_samples = OFFLINE._milliseconds_to_samples(
		projection_recipe["latencyBudgetMs"], f"recipe {projection_recipe['id']}.latencyBudgetMs",
	)
	return {
		"execution_semantics_version": execution_semantics_version,
		"direct_reported_latency_samples": direct_latency_samples,
		"direct_collection_latency_samples": OFFLINE.FRAME_SAMPLES,
		"descriptor_intrinsic_latency_samples": intrinsic_samples,
		"worker_latency_frames": worker_frames,
		"worker_latency_samples": worker_samples,
		"projected_latency_samples": projected_samples,
		"projected_latency_ms": projected_samples * 1000.0 / OFFLINE.SAMPLE_RATE_HZ,
		"budget_latency_samples": budget_samples,
		"within_product_latency_budget": projected_samples <= budget_samples,
	}


def _validate_direct_report(report_path: Path, output_path: Path, input_path: Path, model: Mapping[str, Any], model_path: Path, mix: float) -> Mapping[str, Any]:
	report = _load_json(report_path, "DeepFilterNet model comparison report")
	_expect(report.get("processing_mode") == "expert-backend" and report.get("backend") == "DeepFilterNet", "model comparison", "did not use direct DeepFilterNet")
	_expect(report.get("model_id") == model["id"], "model comparison.model_id", "model drift")
	_expect(math.isclose(float(report.get("mix_factor")), mix, rel_tol=0.0, abs_tol=1e-6), "model comparison.mix_factor", "mix drift")
	_expect(report.get("used_fallback") is False and int(report.get("fallback_count", -1)) == 0, "model comparison", "fallback")
	_expect(int(report.get("deadline_misses", -1)) == 0, "model comparison", "deadline miss")
	_expect(int(report.get("non_finite_sample_count", -1)) == 0 and int(report.get("out_of_range_sample_count", -1)) == 0, "model comparison", "invalid output")
	_expect(int(report.get("saturated_sample_count", 0)) <= int(report.get("input_saturated_sample_count", 0)), "model comparison", "new clipping")
	latency = int(report.get("reported_latency_samples", -1))
	_expect(latency >= 0 and latency % OFFLINE.FRAME_SAMPLES == 0, "model comparison.latency", "invalid latency")
	_validate_direct_intrinsic_latency(model, latency)
	_expect(int(report.get("output_sample_count", -1)) == int(report.get("input_sample_count", -1)) + latency, "model comparison", "tail mismatch")
	_expect(float(report.get("rtf", math.inf)) <= 0.35, "model comparison.rtf", "exceeds 0.35")
	_expect(float(report.get("callback_p99_ms", math.inf)) <= 8.0, "model comparison.callback_p99_ms", "exceeds 8 ms")
	_expect(_sha256(model_path) == model["sha256"], "model comparison model", "hash changed")
	_expect(OFFLINE._read_wav_fingerprint(output_path)["frames"] == OFFLINE._read_wav_fingerprint(input_path)["frames"] + latency, "model comparison WAV", "tail mismatch")
	return report


def _execute_task(context: Mapping[str, Any], args: argparse.Namespace, execution: Mapping[str, Any], output_root: Path, task: Mapping[str, Any], case: Mapping[str, Any]) -> Mapping[str, Any]:
	render_entry = context["render_entries"][str(case["case_id"])]
	task_binding = {
		"run_binding_sha256": context["run_binding_sha256"],
		"tuner_sha256": _sha256(Path(__file__).resolve()),
		"task": task,
		"plan_case_sha256": OFFLINE._canonical_sha256(case),
		"render_entry_sha256": OFFLINE._canonical_sha256(render_entry),
	}
	task_binding_sha256 = _canonical_sha256(task_binding)
	resumable, attempt_root, attempt_number = _find_or_create_attempt(output_root, str(task["task_id"]), task_binding, task_binding_sha256)
	if resumable is not None:
		return resumable

	input_path = OFFLINE._below(context["render_root"], render_entry["input"]["path"], f"{task['task_id']} input")
	clean_path = OFFLINE._below(context["render_root"], render_entry["clean_reference"]["path"], f"{task['task_id']} clean")
	_expect(_sha256(input_path) == render_entry["input"]["sha256"], str(task["task_id"]), "input hash drift")
	_expect(_sha256(clean_path) == render_entry["clean_reference"]["sha256"], str(task["task_id"]), "clean hash drift")
	output_path = attempt_root / "candidate.wav"
	report_path = attempt_root / "benchmark.json"
	stdout_path = attempt_root / "benchmark.stdout.txt"
	stderr_path = attempt_root / "benchmark.stderr.txt"
	edge_path = attempt_root / "fixed-timeline.json"
	edge_stdout = attempt_root / "fixed-timeline.stdout.txt"
	edge_stderr = attempt_root / "fixed-timeline.stderr.txt"
	objective_path = attempt_root / "objective-quality.json"
	objective_stdout = attempt_root / "objective.stdout.txt"
	objective_stderr = attempt_root / "objective.stderr.txt"
	# Every crossed candidate reuses source cases. Keep the scorer's private
	# clean-ASR reference task-local so concurrent candidates never share a
	# writer, while still leaving the reference outside the immutable attempt.
	private_reference = output_root / "private-references" / f"{task['task_id']}.json"
	private_reference.parent.mkdir(parents=True, exist_ok=True)
	environment = OFFLINE._sanitized_environment(execution["product_root"])

	try:
		if task["kind"] == "product-recipe":
			recipe = OFFLINE._public_recipe(str(task["profile"]), context["recipes"])
			model = context["models"][str(recipe["modelIds"][0])] if recipe["modelIds"] else None
			model_path = OFFLINE._below(execution["product_root"], str(model["path"]), "candidate model") if model is not None else None
			arguments = OFFLINE._benchmark_arguments(
				str(task["profile"]), task["ui_controls"], str(recipe["minimumCpuClass"]), input_path,
				clean_path, output_path, report_path, model, model_path,
			)
			OFFLINE._run(OFFLINE._tool_command(execution["benchmark"], arguments), execution["product_root"], environment, args.timeout_seconds, stdout_path, stderr_path, str(task["task_id"]))
			report = OFFLINE._validate_benchmark_report(
				report_path, output_path, input_path, clean_path, str(task["profile"]), recipe, model, model_path,
				task["ui_controls"], task["recipe_controls"],
			)
		else:
			model = context["models"][str(task["model_id"])]
			model_path = OFFLINE._below(execution["product_root"], str(model["path"]), "comparison model")
			arguments = _direct_model_arguments(model, model_path, float(task["mix_factor"]), input_path, clean_path, output_path, report_path)
			OFFLINE._run(OFFLINE._tool_command(execution["benchmark"], arguments), execution["product_root"], environment, args.timeout_seconds, stdout_path, stderr_path, str(task["task_id"]))
			report = _validate_direct_report(report_path, output_path, input_path, model, model_path, float(task["mix_factor"]))

		latency = int(report["reported_latency_samples"])
		OFFLINE._run(
			OFFLINE._tool_command(execution["fixed_timeline_scorer"], OFFLINE._fixed_timeline_arguments(clean_path, output_path, latency, edge_path)),
			execution["scorer_root"], environment, args.timeout_seconds, edge_stdout, edge_stderr, f"{task['task_id']} fixed timeline",
		)
		OFFLINE._validate_edge_fixed_timeline(edge_path, clean_path, output_path, latency)

		pseudo_case = dict(case)
		pseudo_case["case_id"] = str(task["task_id"])
		pseudo_case["profile"] = str(task["profile"])
		binding = {"condition": task["condition"], "language": task["language"]}
		objective_arguments = OFFLINE._objective_arguments(
			context, pseudo_case, binding, clean_path, input_path, output_path, 0, latency,
			private_reference, objective_path, execution["scorer"], execution["metrics_root"], execution["metrics_manifest"],
		)
		OFFLINE._run(
			OFFLINE._tool_command(context["metrics_python"], objective_arguments), execution["scorer_root"], environment,
			args.timeout_seconds, objective_stdout, objective_stderr, f"{task['task_id']} objective score",
		)
		objective = OFFLINE._validate_objective(
			objective_path, context, pseudo_case, binding, clean_path, input_path, output_path, 0, latency, private_reference,
		)
		artifacts = {
			"benchmark_report": _artifact_record(report_path, attempt_root),
			"benchmark_stdout": _artifact_record(stdout_path, attempt_root),
			"benchmark_stderr": _artifact_record(stderr_path, attempt_root),
			"candidate_wav": _artifact_record(output_path, attempt_root),
			"fixed_timeline": _artifact_record(edge_path, attempt_root),
			"fixed_timeline_stdout": _artifact_record(edge_stdout, attempt_root),
			"fixed_timeline_stderr": _artifact_record(edge_stderr, attempt_root),
			"objective_score": _artifact_record(objective_path, attempt_root),
			"objective_stdout": _artifact_record(objective_stdout, attempt_root),
			"objective_stderr": _artifact_record(objective_stderr, attempt_root),
		}
		result = {
			"schema_version": SCHEMA_VERSION, "campaign": CAMPAIGN_ID, "status": "passed",
			"attempt": attempt_number, "task_binding": task_binding, "task_binding_sha256": task_binding_sha256,
			"task": task, "latency_samples": latency,
			"performance": {
				"rtf": float(report.get("rtf", 0.0)),
				"callback_p99_ms": float(report.get("callback_p99_ms", 0.0)),
				"worker_p99_ms": float(report.get("worker_processing_p99_ms", 0.0)),
			},
			"metrics": objective["metrics"], "candidate_minus_original": objective["candidate_minus_original"],
			"artifacts": artifacts,
		}
		if task["kind"] == "model-comparison":
			# The direct expert adapter contributes one collection frame which is
			# not intrinsic model latency. Product latency is derived independently
			# from the descriptor's intrinsic value and the signed public Quality
			# recipe's execution semantics.
			projection_recipe = OFFLINE._public_recipe("Quality", context["recipes"])
			result["model_product_projection"] = _model_product_projection(model, projection_recipe, latency)
		_write_json_atomic(attempt_root / "result.json", result)
		return result
	except Exception as error:
		failure = {
			"schema_version": SCHEMA_VERSION, "campaign": CAMPAIGN_ID, "status": "failed",
			"attempt": attempt_number, "task_binding": task_binding, "task_binding_sha256": task_binding_sha256,
			"task": task, "error": str(error),
		}
		_write_json_atomic(attempt_root / "result.json", failure)
		raise


def _median(rows: Sequence[Mapping[str, Any]], key: str) -> float:
	_expect(bool(rows), key, "no rows")
	return float(statistics.median(float(row["candidate_minus_original"][key]) for row in rows))


def _candidate_summary(profile: str, candidate_id: str, rows: Sequence[Mapping[str, Any]]) -> dict[str, Any]:
	by_condition = {condition: [row for row in rows if row["task"]["condition"] == condition] for condition in CONDITIONS}
	_expect(all(by_condition.values()), candidate_id, "missing a required condition")
	noisy = by_condition["noisy"] + by_condition["severe"]
	clean_language_gates: dict[str, Any] = {}
	clean_pass = True
	for language in sorted({str(row["task"]["language"]) for row in by_condition["clean"]}):
		language_rows = [row for row in by_condition["clean"] if row["task"]["language"] == language]
		metrics = {
			"estoi": _median(language_rows, "estoi"),
			"dnsmos_sig": _median(language_rows, "dnsmos_sig"),
			"wer_delta_percentage_points": _median(language_rows, "wer_delta_percentage_points"),
		}
		passed = metrics["estoi"] >= -0.01 and metrics["dnsmos_sig"] >= -0.05 and metrics["wer_delta_percentage_points"] <= 1.0
		clean_language_gates[language] = {**metrics, "passed": passed, "case_count": len(language_rows)}
		clean_pass = clean_pass and passed

	catastrophes = sum(
		1 for row in rows
		if float(row["candidate_minus_original"]["estoi"]) < -0.05
		or float(row["candidate_minus_original"]["dnsmos_sig"]) < -0.5
	)
	catastrophe_rate = catastrophes / len(rows)
	gate = PROFILE_NOISY_GATES[profile]
	noisy_metrics = {key: _median(noisy, key) for key in ("dnsmos_ovrl", "dnsmos_sig", "dnsmos_bak", "estoi", "wer_delta_percentage_points")}
	noisy_pass = noisy_metrics["dnsmos_ovrl"] >= gate["dnsmos_ovrl"] and noisy_metrics["dnsmos_bak"] >= gate["dnsmos_bak"]

	cohort_failures = []
	for dimension in ("condition", "language", "noise_class", "microphone_family"):
		values = sorted({str(row["task"][dimension]) for row in rows})
		for value in values:
			cohort = [row for row in rows if str(row["task"][dimension]) == value]
			if len(cohort) >= 3 and _median(cohort, "dnsmos_ovrl") < -0.10:
				cohort_failures.append({"dimension": dimension, "value": value, "case_count": len(cohort), "median_dnsmos_ovrl": _median(cohort, "dnsmos_ovrl")})

	average_rtf = statistics.fmean(float(row["performance"]["rtf"]) for row in rows)
	callback_p99_max = max(float(row["performance"]["callback_p99_ms"]) for row in rows)
	worker_p99_max = max(float(row["performance"]["worker_p99_ms"]) for row in rows)
	if profile == "Balanced":
		performance_pass = average_rtf <= 0.15 and callback_p99_max <= 5.0
	elif profile in ("Quality", "VoiceFocus"):
		performance_pass = average_rtf <= 0.35 and callback_p99_max <= 8.0 and worker_p99_max <= 8.0
	else:
		performance_pass = True

	if profile in ("Light", "Balanced"):
		objective = 0.55 * noisy_metrics["dnsmos_ovrl"] + 0.45 * noisy_metrics["dnsmos_bak"]
	elif profile == "Quality":
		objective = 0.60 * noisy_metrics["dnsmos_ovrl"] + 0.40 * noisy_metrics["dnsmos_sig"]
	else:
		objective = (
			0.65 * noisy_metrics["dnsmos_bak"] + 0.20 * noisy_metrics["estoi"]
			+ 0.15 * noisy_metrics["dnsmos_ovrl"] - 0.01 * max(0.0, noisy_metrics["wer_delta_percentage_points"])
		)
	passed = clean_pass and noisy_pass and catastrophe_rate <= 0.005 and not cohort_failures and performance_pass
	return {
		"profile": profile, "candidate_id": candidate_id, "case_count": len(rows),
		"controls": rows[0]["task"].get("recipe_controls"), "objective": objective,
		"clean_language_gates": clean_language_gates, "noisy_medians": noisy_metrics,
		"catastrophe_count": catastrophes, "catastrophe_rate": catastrophe_rate,
		"cohort_failures": cohort_failures,
		"performance": {
			"average_rtf": average_rtf, "callback_p99_ms_max": callback_p99_max,
			"worker_p99_ms_max": worker_p99_max, "passed": performance_pass,
		},
		"passed_common_gates": passed,
	}


def select_winners(
	results: Sequence[Mapping[str, Any]], step: int, scene_counts: Mapping[str, Mapping[str, int]],
	evidence_label: str, complete: bool, exhaustive_grid: bool,
) -> dict[str, Any]:
	product = [result for result in results if result["task"]["kind"] == "product-recipe"]
	summaries: dict[str, list[dict[str, Any]]] = {}
	winners: dict[str, Any] = {}
	for profile in PROFILES:
		profile_rows = [row for row in product if row["task"]["profile"] == profile]
		candidate_ids = sorted({str(row["task"]["candidate_id"]) for row in profile_rows})
		ranked = []
		for candidate_id in candidate_ids:
			rows = [row for row in profile_rows if row["task"]["candidate_id"] == candidate_id]
			expected = sum(scene_counts[profile].values())
			if len(rows) == expected:
				ranked.append(_candidate_summary(profile, candidate_id, rows))
		ranked.sort(key=lambda item: (-float(item["objective"]), str(item["candidate_id"])))
		summaries[profile] = ranked
		passing = [item for item in ranked if item["passed_common_gates"]]
		winners[profile] = passing[0] if passing else None

	voice_comparison = None
	if winners.get("Quality") is not None and winners.get("VoiceFocus") is not None:
		quality_id = winners["Quality"]["candidate_id"]
		voice_id = winners["VoiceFocus"]["candidate_id"]
		quality_rows = {
			str(row["task"]["comparison_scene_id"]): row for row in product
			if row["task"]["candidate_id"] == quality_id and row["task"]["condition"] == "severe"
			and row["task"]["comparison_scene_id"] is not None
		}
		voice_rows = {
			str(row["task"]["comparison_scene_id"]): row for row in product
			if row["task"]["candidate_id"] == voice_id and row["task"]["condition"] == "severe"
			and row["task"]["comparison_scene_id"] is not None
		}
		paired = sorted(set(quality_rows) & set(voice_rows))
		if paired:
			bak_delta = statistics.median(
				float(voice_rows[key]["metrics"]["candidate"]["dnsmos_bak"])
				- float(quality_rows[key]["metrics"]["candidate"]["dnsmos_bak"])
				for key in paired
			)
			voice_estoi = statistics.median(float(voice_rows[key]["metrics"]["candidate"]["estoi"]) for key in paired)
			quality_estoi = statistics.median(float(quality_rows[key]["metrics"]["candidate"]["estoi"]) for key in paired)
			voice_comparison = {
				"pair_count": len(paired), "bak_improvement_over_quality_median": bak_delta,
				"voice_focus_estoi_median": voice_estoi, "quality_estoi_median": quality_estoi,
				"passed": bak_delta >= 0.10 and voice_estoi >= quality_estoi,
			}
			if not voice_comparison["passed"]:
				winners["VoiceFocus"] = None

	coverage_ok = all(
		all(scene_counts[profile][condition] >= FREEZE_MINIMUM_SCENES[condition] for condition in CONDITIONS)
		for profile in PROFILES
	)
	all_winners = all(winners.get(profile) is not None for profile in PROFILES)
	eligible = complete and evidence_label == "candidate" and step == 5 and exhaustive_grid and coverage_ok and all_winners
	return {
		"schema_version": SCHEMA_VERSION, "selection": "mumble-input-enhancement-recipe-selection-v1",
		"status": "passed" if all_winners else "no-qualified-winner",
		"dataset_split": "tuning", "validation_used_for_selection": False, "holdout_used_for_selection": False,
		"evidence_label": evidence_label, "grid_step": step, "complete_matrix": complete,
		"exhaustive_profile_grid": exhaustive_grid,
		"eligible_for_recipe_freeze": eligible, "scene_counts": scene_counts,
		"winners": winners, "voice_focus_over_quality": voice_comparison,
		"rankings": summaries,
	}


def _model_comparison_tasks(context: Mapping[str, Any], scenes: Mapping[str, Sequence[Mapping[str, Any]]], mixes: Sequence[float]) -> list[dict[str, Any]]:
	for model_id in ("deepfilternet:low-latency", "deepfilternet:balanced"):
		_expect(model_id in context["models"], "model comparison", f"packaged model {model_id} is missing")
	tasks = []
	for case in scenes["Quality"]:
		for model_id in ("deepfilternet:low-latency", "deepfilternet:balanced"):
			for mix in mixes:
				tasks.append({
					"kind": "model-comparison", "profile": "Quality", "model_id": model_id,
					"mix_factor": mix, "candidate_id": f"{model_id}-m{round(mix * 100):03d}",
					"task_id": f"model-{model_id.split(':')[-1]}-m{round(mix * 100):03d}--{case['case_id']}",
					"source_case_id": case["case_id"], "comparison_scene_id": case.get("comparison_scene_id"),
					"condition": _condition(case), "language": OFFLINE.LANGUAGE_MAP[str(case["speech"]["language"])],
					"noise_class": case["noise"]["class"] if case["noise"] is not None else "clean",
					"microphone_family": case["mix"]["microphone_response"]["device_family"],
				})
	return tasks


def summarize_model_comparison(results: Sequence[Mapping[str, Any]]) -> Mapping[str, Any] | None:
	rows = [row for row in results if row["task"]["kind"] == "model-comparison"]
	if not rows:
		return None
	summaries = []
	for candidate_id in sorted({str(row["task"]["candidate_id"]) for row in rows}):
		candidate = [row for row in rows if row["task"]["candidate_id"] == candidate_id]
		summaries.append({
			"candidate_id": candidate_id, "model_id": candidate[0]["task"]["model_id"],
			"mix_factor": candidate[0]["task"]["mix_factor"], "case_count": len(candidate),
			"median_dnsmos_ovrl": _median(candidate, "dnsmos_ovrl"),
			"median_dnsmos_sig": _median(candidate, "dnsmos_sig"),
			"median_dnsmos_bak": _median(candidate, "dnsmos_bak"),
			"median_estoi": _median(candidate, "estoi"),
			"median_wer_delta_percentage_points": _median(candidate, "wer_delta_percentage_points"),
			"median_rtf": statistics.median(float(row["performance"]["rtf"]) for row in candidate),
			"p99_callback_ms_max": max(float(row["performance"]["callback_p99_ms"]) for row in candidate),
			"projected_product_latency_ms": candidate[0]["model_product_projection"]["projected_latency_ms"],
			"within_product_latency_budget": candidate[0]["model_product_projection"]["within_product_latency_budget"],
		})
	standard = [row for row in summaries if row["model_id"] == "deepfilternet:balanced"]
	low_latency = [row for row in summaries if row["model_id"] == "deepfilternet:low-latency"]
	standard_eligible = bool(standard) and all(row["within_product_latency_budget"] for row in standard)
	return {
		"summaries": summaries,
		"standard_model_product_eligible": standard_eligible,
		"recommendation": (
			"consider-standard-product-recipe" if standard_eligible
			and statistics.median(row["median_dnsmos_ovrl"] for row in standard) > statistics.median(row["median_dnsmos_ovrl"] for row in low_latency)
			else "retain-low-latency-product-model"
		),
		"latency_contract": "descriptor intrinsic latency plus worker frames selected by the signed public Quality recipe execution semantics",
	}


def _build_context(args: argparse.Namespace) -> Mapping[str, Any]:
	proxy = argparse.Namespace(
		plan=args.plan, case_set=None, render_manifest=args.render_manifest, render_root=args.render_root,
		inventory=args.inventory, corpus_lock=args.corpus_lock, transformation_manifest=args.transformation_manifest,
		benchmark=args.benchmark, runtime_root=args.runtime_root, model_manifest=args.model_manifest,
		recipe_manifest=args.recipe_manifest, metrics_python=args.metrics_python,
		metrics_runtime_root=args.metrics_runtime_root, metrics_manifest=args.metrics_manifest,
		scorer=args.scorer, output_root=args.output_root, timeout_seconds=args.timeout_seconds,
	)
	context = OFFLINE._build_run_context(proxy)
	_expect(context["plan"]["split"] == "tuning", "plan.split", "recipe selection may only consume tuning")
	return context


def _run_task_matrix(
	tasks: Sequence[Mapping[str, Any]],
	jobs: int,
	worker: Callable[[Mapping[str, Any]], Mapping[str, Any]],
	progress: Callable[[int, int, int], None] | None = None,
) -> tuple[list[Mapping[str, Any]], list[dict[str, Any]]]:
	"""Run independent tasks concurrently and return canonical plan ordering.

	Workers never write the campaign manifest. Completion order is deliberately
	discarded so scheduler timing cannot change selection evidence or its hash.
	"""
	_expect(1 <= jobs <= 4, "--jobs", "must be within 1..4")
	results_by_index: dict[int, Mapping[str, Any]] = {}
	rejections_by_index: dict[int, dict[str, Any]] = {}
	completed = 0

	def record(index: int, task: Mapping[str, Any], future: concurrent.futures.Future[Mapping[str, Any]]) -> None:
		nonlocal completed
		try:
			results_by_index[index] = future.result()
		except Exception as error:
			# A quality/timeline failure rejects this candidate on this exact
			# scene. It is evidence, not an excuse to abandon unrelated
			# candidates. The failed attempt remains immutable below tasks/.
			rejections_by_index[index] = {
				"task_id": task["task_id"], "candidate_id": task["candidate_id"], "error": str(error),
			}
		completed += 1
		if progress is not None:
			progress(completed, len(results_by_index), len(rejections_by_index))

	with concurrent.futures.ThreadPoolExecutor(max_workers=jobs, thread_name_prefix="recipe-tuning") as executor:
		future_work = {
			executor.submit(worker, task): (index, task)
			for index, task in enumerate(tasks)
		}
		for future in concurrent.futures.as_completed(future_work):
			index, task = future_work[future]
			record(index, task, future)

	results = [results_by_index[index] for index in sorted(results_by_index)]
	rejections = [rejections_by_index[index] for index in sorted(rejections_by_index)]
	return results, rejections


def run_campaign(args: argparse.Namespace) -> Mapping[str, Any]:
	_expect(args.grid_step >= 5 and args.grid_step % 5 == 0, "--grid-step", "must be a multiple of five")
	_expect(1 <= args.jobs <= 4, "--jobs", "must be within 1..4")
	_expect(all(getattr(args, f"{condition}_scenes") > 0 for condition in CONDITIONS), "scene limits", "must be positive")
	context = _build_context(args)
	limits = {condition: int(getattr(args, f"{condition}_scenes")) for condition in CONDITIONS}
	scenes = select_scenes(context["plan"], args.selection_seed, limits)
	scene_counts = {
		profile: {condition: sum(_condition(case) == condition for case in scenes[profile]) for condition in CONDITIONS}
		for profile in PROFILES
	}
	candidate_set_document = None
	candidates_by_profile = None
	if args.candidate_set is not None:
		candidate_set_document, candidates_by_profile = load_candidate_set(args.candidate_set)
	tasks = build_tasks(context["plan"], scenes, args.grid_step, candidates_by_profile)
	if args.compare_deepfilter_models:
		tasks.extend(_model_comparison_tasks(context, scenes, args.model_comparison_mix))
	if args.max_tasks is not None:
		_expect(args.max_tasks > 0, "--max-tasks", "must be positive")
		tasks = tasks[: args.max_tasks]

	output_root = args.output_root.resolve()
	output_root.mkdir(parents=True, exist_ok=True)
	campaign_path = output_root / "campaign-manifest.json"
	run_binding = {
		"schema_version": SCHEMA_VERSION, "campaign": CAMPAIGN_ID,
		"base_offline_run_binding_sha256": context["run_binding_sha256"],
		"tuner": _file_record(Path(__file__).resolve()), "dataset_split": "tuning",
		"selection_seed": args.selection_seed, "grid_step": args.grid_step,
		"jobs": args.jobs,
		"evidence_label": args.evidence_label, "scene_counts": scene_counts,
		"task_count": len(tasks), "task_plan_sha256": _canonical_sha256(tasks),
		"max_tasks": args.max_tasks, "compare_deepfilter_models": args.compare_deepfilter_models,
		"model_comparison_mix": list(args.model_comparison_mix),
		"candidate_set": (
			{**_file_record(args.candidate_set.resolve()), "canonical_sha256": _canonical_sha256(candidate_set_document)}
			if args.candidate_set is not None else None
		),
	}
	run_binding_sha256 = _canonical_sha256(run_binding)
	if campaign_path.exists():
		previous = _load_json(campaign_path, "campaign manifest")
		_expect(previous.get("run_binding_sha256") == run_binding_sha256 and previous.get("run_binding") == run_binding, "campaign manifest", "binding changed; use a new output root")
	elif any(output_root.iterdir()):
		raise TuningError("non-empty output root has no matching campaign manifest")

	manifest = {
		"schema_version": SCHEMA_VERSION, "campaign": CAMPAIGN_ID, "status": "running",
		"private_audio_do_not_upload": True, "run_binding": run_binding, "run_binding_sha256": run_binding_sha256,
		"summary": {"task_count": len(tasks), "passed": 0, "rejected": 0},
	}
	_write_json_atomic(campaign_path, manifest)
	execution = None
	case_by_id = {str(case["case_id"]): case for case in context["plan"]["cases"]}
	try:
		execution = OFFLINE._prepare_execution_runtime(context, output_root)

		def execute(task: Mapping[str, Any]) -> Mapping[str, Any]:
			return _execute_task(
				context, args, execution, output_root, task, case_by_id[str(task["source_case_id"])],
			)

		def update_progress(completed: int, passed: int, rejected: int) -> None:
			manifest["summary"] = {"task_count": len(tasks), "passed": passed, "rejected": rejected}
			manifest["progress_completed_count"] = completed
			manifest["progress_pending_count"] = len(tasks) - completed
			_write_json_atomic(campaign_path, manifest)

		results, rejections = _run_task_matrix(tasks, args.jobs, execute, update_progress)
		OFFLINE._critical_files_stable(context, execution["product_root"])
		OFFLINE._execution_environment_stable(execution)
		complete = len(results) + len(rejections) == len(tasks) and args.max_tasks is None
		selection = select_winners(
			results, args.grid_step, scene_counts, args.evidence_label, complete,
			exhaustive_grid=args.candidate_set is None,
		)
		selection["model_comparison"] = summarize_model_comparison(results)
		selection["rejected_tasks"] = rejections
		selection["run_binding_sha256"] = run_binding_sha256
		_write_json_atomic(output_root / "selection.json", selection)
		manifest.update({
			"status": "passed-with-rejections" if complete and rejections else "passed" if complete else "diagnostic-incomplete",
			"selection": _file_record(output_root / "selection.json", relative_to=output_root),
			"summary": {"task_count": len(tasks), "passed": len(results), "rejected": len(rejections), "complete": complete},
		})
		manifest.pop("progress_completed_count", None)
		manifest.pop("progress_pending_count", None)
		_write_json_atomic(campaign_path, manifest)
		return manifest
	finally:
		for name, label in (
			("_private-execution-runtime", "private product runtime"),
			("_private-metrics-runtime", "private metrics runtime"),
			("_private-objective-scorer", "private objective scorer"),
		):
			OFFLINE._remove_private_tree(output_root, output_root / name, label)


def run_self_test() -> None:
	quality_recipe = {
		"id": "input.quality.self-test", "executionSemanticsVersion": 5, "latencyBudgetMs": 50,
	}
	standard_projection = _model_product_projection(
		{"id": "deepfilternet:balanced", "algorithmicLatencyMs": 30}, quality_recipe, 1920,
	)
	_expect(
		standard_projection["descriptor_intrinsic_latency_samples"] == 1440
		and standard_projection["worker_latency_samples"] == 960
		and standard_projection["projected_latency_samples"] == 2400
		and standard_projection["projected_latency_ms"] == 50.0
		and standard_projection["within_product_latency_budget"] is True,
		"model latency self-test", "standard DeepFilterNet projection drift",
	)
	low_latency_projection = _model_product_projection(
		{"id": "deepfilternet:low-latency", "algorithmicLatencyMs": 10}, quality_recipe, 960,
	)
	_expect(
		low_latency_projection["projected_latency_samples"] == 1440
		and low_latency_projection["projected_latency_ms"] == 30.0,
		"model latency self-test", "low-latency DeepFilterNet projection drift",
	)
	try:
		_model_product_projection(
			{"id": "deepfilternet:stale-descriptor", "algorithmicLatencyMs": 40}, quality_recipe, 1920,
		)
	except TuningError:
		pass
	else:
		raise TuningError("model latency self-test: stale descriptor was accepted")

	scheduler_tasks = [
		{"task_id": f"task-{value}", "candidate_id": f"candidate-{value}", "value": value}
		for value in (2, 1, 0)
	]
	progress_events: list[tuple[int, int, int]] = []

	def scheduler_worker(task: Mapping[str, Any]) -> Mapping[str, Any]:
		if task["value"] == 1:
			raise TuningError("intentional scheduler rejection")
		return {"value": task["value"]}

	scheduler_results, scheduler_rejections = _run_task_matrix(
		scheduler_tasks, 3, scheduler_worker,
		lambda completed, passed, rejected: progress_events.append((completed, passed, rejected)),
	)
	_expect([row["value"] for row in scheduler_results] == [2, 0], "parallel scheduler self-test", "result order drift")
	_expect([row["task_id"] for row in scheduler_rejections] == ["task-1"], "parallel scheduler self-test", "rejection order drift")
	_expect(progress_events[-1] == (3, 2, 1), "parallel scheduler self-test", "progress accounting drift")

	for profile in PROFILES:
		full = candidate_grid(profile, 5)
		ranges = PLAN.PROFILE_RECIPE_CONTROL_RANGES[profile]
		expected = ((ranges["noise_reduction"][1] - ranges["noise_reduction"][0]) // 5 + 1) * ((ranges["natural_clear"][1] - ranges["natural_clear"][0]) // 5 + 1)
		_expect(len(full) == expected, profile, "full five-point grid is incomplete")
		_expect(len({item["candidate_id"] for item in full}) == len(full), profile, "duplicate candidate")
		coarse = candidate_grid(profile, 20)
		_expect(all(value % 5 == 0 for item in coarse for value in item["recipe_controls"].values()), profile, "coarse search escaped five-point coordinates")

	fixture = []
	for profile in PROFILES:
		for condition in CONDITIONS:
			for index in range(12 if condition != "clean" else 6):
				candidate = {
					"dnsmos_ovrl": 0.30 if condition != "clean" else 0.0,
					"dnsmos_sig": 0.10 if condition != "clean" else 0.0,
					"dnsmos_bak": 0.60 if condition != "clean" else 0.0,
					"estoi": 0.0, "wer_delta_percentage_points": 0.0,
				}
				fixture.append({
					"task": {"kind": "product-recipe", "profile": profile, "candidate_id": f"{profile}-candidate", "condition": condition,
						"language": "en" if index % 2 == 0 else "sv", "noise_class": condition, "microphone_family": "headset",
						"comparison_scene_id": f"pair-{condition}-{index}" if profile in ("Quality", "VoiceFocus") else None,
						"recipe_controls": {"noise_reduction": 50, "natural_clear": 50}},
					"candidate_minus_original": candidate,
					"metrics": {"candidate": {"dnsmos_bak": 4.2 if profile == "VoiceFocus" else 4.0, "estoi": 0.9}},
					"performance": {"rtf": 0.10, "callback_p99_ms": 2.0, "worker_p99_ms": 2.0},
				})
	counts = {profile: {"clean": 6, "noisy": 12, "severe": 12} for profile in PROFILES}
	selection = select_winners(fixture, 5, counts, "candidate", True, True)
	_expect(selection["eligible_for_recipe_freeze"] is True, "selection self-test", "valid candidate was not freeze-eligible")
	tampered = json.loads(json.dumps(fixture))
	for row in tampered:
		if row["task"]["profile"] == "Balanced" and row["task"]["condition"] == "clean":
			row["candidate_minus_original"]["dnsmos_sig"] = -0.1
	failed = select_winners(tampered, 5, counts, "candidate", True, True)
	_expect(failed["winners"]["Balanced"] is None and not failed["eligible_for_recipe_freeze"], "selection self-test", "clean regression was accepted")


def _parse_mixes(value: str) -> tuple[float, ...]:
	try:
		mixes = tuple(float(item.strip()) for item in value.split(",") if item.strip())
	except ValueError as error:
		raise argparse.ArgumentTypeError("mixes must be comma-separated finite numbers") from error
	if not mixes or any(not math.isfinite(item) or item < 0.0 or item > 1.0 for item in mixes):
		raise argparse.ArgumentTypeError("mixes must be within 0.0..1.0")
	return mixes


def main(argv: Sequence[str] | None = None) -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--plan", type=Path)
	parser.add_argument("--render-manifest", type=Path)
	parser.add_argument("--render-root", type=Path)
	parser.add_argument("--inventory", type=Path)
	parser.add_argument("--corpus-lock", type=Path)
	parser.add_argument("--transformation-manifest", type=Path)
	parser.add_argument("--benchmark", type=Path)
	parser.add_argument("--runtime-root", type=Path)
	parser.add_argument("--model-manifest", type=Path)
	parser.add_argument("--recipe-manifest", type=Path)
	parser.add_argument("--metrics-python", type=Path)
	parser.add_argument("--metrics-runtime-root", type=Path)
	parser.add_argument("--metrics-manifest", type=Path)
	parser.add_argument("--scorer", type=Path)
	parser.add_argument("--output-root", type=Path)
	parser.add_argument("--grid-step", type=int, default=5, help="Recipe-space grid step; must be a multiple of five")
	parser.add_argument("--jobs", type=int, default=1, help="Concurrent independent tasks (1..4)")
	parser.add_argument("--candidate-set", type=Path, help="Optional tuning-only five-point candidate-set JSON; prevents automatic freeze")
	parser.add_argument("--clean-scenes", type=int, default=6)
	parser.add_argument("--noisy-scenes", type=int, default=12)
	parser.add_argument("--severe-scenes", type=int, default=12)
	parser.add_argument("--selection-seed", default="mumble-recipe-tuning-v1")
	parser.add_argument("--evidence-label", choices=("diagnostic", "candidate"), default="diagnostic")
	parser.add_argument("--max-tasks", type=int, help="Diagnostic interruption bound; prevents recipe freeze")
	parser.add_argument("--compare-deepfilter-models", action="store_true")
	parser.add_argument("--model-comparison-mix", type=_parse_mixes, default=(0.75, 0.90), metavar="MIXES")
	parser.add_argument("--timeout-seconds", type=int, default=900)
	parser.add_argument("--self-test", action="store_true")
	args = parser.parse_args(argv)
	try:
		if args.self_test:
			run_self_test()
			print("input enhancement recipe tuning self-test: ok")
			return 0
		required = (
			"plan", "render_manifest", "render_root", "inventory", "corpus_lock", "transformation_manifest",
			"benchmark", "runtime_root", "metrics_python", "metrics_runtime_root", "metrics_manifest", "scorer", "output_root",
		)
		missing = [f"--{name.replace('_', '-')}" for name in required if getattr(args, name) is None]
		if missing:
			raise TuningError(f"missing required arguments: {', '.join(missing)}")
		result = run_campaign(args)
		print(json.dumps({"status": result["status"], "summary": result["summary"]}, sort_keys=True, separators=(",", ":")))
		return 0
	except (AssertionError, KeyError, OSError, TuningError, TypeError, ValueError, OFFLINE.CampaignError) as error:
		print(f"recipe tuning: error: {error}", file=sys.stderr)
		return 1


if __name__ == "__main__":
	raise SystemExit(main())
