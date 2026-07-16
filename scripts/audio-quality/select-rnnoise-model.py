#!/usr/bin/env python3
"""Select an RNNoise candidate on validation, then open holdout exactly once.

The first stage ranks only validation evidence and seals one candidate.  The
second stage accepts evidence for that candidate only, compares paired holdout
OVRL scores against the embedded model, evaluates hard safety/performance
gates, and consumes an exclusive local receipt.  Holdout never re-ranks the
five training candidates.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import math
import os
import statistics
import sys
import tempfile
from pathlib import Path, PurePosixPath
from typing import Any, Mapping, MutableMapping, Sequence


class SelectionError(ValueError):
	"""Raised when model selection evidence is incomplete or inconsistent."""


HEX64 = __import__("re").compile(r"[0-9a-f]{64}")
IDENTIFIER = __import__("re").compile(r"[A-Za-z0-9][A-Za-z0-9._:-]*")
BOOTSTRAP_ITERATIONS = 20_000
MINIMUM_PAIRED_HOLDOUT_CASES = 30


def _load_campaign() -> Any:
	path = Path(__file__).with_name("freeze-rnnoise-training-plan.py")
	spec = importlib.util.spec_from_file_location("mumble_rnnoise_training_campaign", path)
	if spec is None or spec.loader is None:
		raise SelectionError(f"unable to load training campaign tool: {path}")
	module = importlib.util.module_from_spec(spec)
	spec.loader.exec_module(module)
	return module


CAMPAIGN = _load_campaign()


def _expect(condition: bool, path: str, message: str) -> None:
	if not condition:
		raise SelectionError(f"{path}: {message}")


def _exact_keys(value: Any, required: set[str], optional: set[str], path: str) -> Mapping[str, Any]:
	_expect(isinstance(value, dict), path, "must be an object")
	missing = sorted(required - set(value))
	unknown = sorted(set(value) - required - optional)
	_expect(not missing, path, f"missing keys: {', '.join(missing)}")
	_expect(not unknown, path, f"unknown keys: {', '.join(unknown)}")
	return value


def _identifier(value: Any, path: str) -> str:
	_expect(isinstance(value, str) and bool(IDENTIFIER.fullmatch(value)), path, "must be a stable identifier")
	return value


def _hash(value: Any, path: str) -> str:
	_expect(isinstance(value, str) and bool(HEX64.fullmatch(value)), path, "must be lowercase SHA-256")
	return value


def _safe_relative_path(value: Any, path: str) -> str:
	_expect(isinstance(value, str) and bool(value), path, "must be a non-empty path")
	_expect("\\" not in value and ":" not in value, path, "must be a portable POSIX relative path")
	parsed = PurePosixPath(value)
	_expect(value == parsed.as_posix(), path, "must use normalized forward slashes")
	_expect(not parsed.is_absolute() and "." not in parsed.parts and ".." not in parsed.parts, path, "unsafe path")
	return value


def _finite_number(value: Any, path: str) -> float:
	_expect(isinstance(value, (int, float)) and not isinstance(value, bool), path, "must be numeric")
	result = float(value)
	_expect(math.isfinite(result), path, "must be finite")
	return result


def _artifact(root: Path, relative: str, expected_hash: str, expected_size: int, path: str) -> Path:
	relative = _safe_relative_path(relative, f"{path}.relative_path")
	_expect(root.is_dir() and not root.is_symlink(), "model_root", "must be a real directory, not a symlink")
	current = root
	for component in PurePosixPath(relative).parts:
		current = current / component
		_expect(not current.is_symlink(), path, "symlink components are forbidden")
	artifact = current.resolve()
	try:
		artifact.relative_to(root.resolve())
	except ValueError as error:
		raise SelectionError(f"{path}.relative_path: escapes model root") from error
	_expect(artifact.is_file(), path, "artifact is missing")
	_expect(isinstance(expected_size, int) and not isinstance(expected_size, bool) and expected_size > 0,
		f"{path}.size_bytes", "must be positive")
	_expect(artifact.stat().st_size == expected_size, f"{path}.size_bytes", "artifact size mismatch")
	_expect(CAMPAIGN.file_sha256(artifact) == _hash(expected_hash, f"{path}.sha256"), f"{path}.sha256", "artifact hash mismatch")
	return artifact


def _write_json(path: Path, value: Mapping[str, Any]) -> None:
	path.parent.mkdir(parents=True, exist_ok=True)
	temporary = path.with_name(f".{path.name}.{os.getpid()}.tmp")
	temporary.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")
	os.replace(temporary, path)


def _validate_validation_metrics(value: Any, path: str) -> Mapping[str, float | int]:
	metrics = _exact_keys(value, {"bak_median", "case_count", "estoi_median", "ovrl_median", "sig_median", "wer_percent"}, set(), path)
	_expect(isinstance(metrics["case_count"], int) and not isinstance(metrics["case_count"], bool) and metrics["case_count"] >= 30,
		f"{path}.case_count", "at least 30 validation cases are required")
	result: dict[str, float | int] = {"case_count": metrics["case_count"]}
	for key in ("bak_median", "estoi_median", "ovrl_median", "sig_median", "wer_percent"):
		result[key] = _finite_number(metrics[key], f"{path}.{key}")
	_expect(1.0 <= result["ovrl_median"] <= 5.0, f"{path}.ovrl_median", "must be in [1, 5]")
	_expect(1.0 <= result["bak_median"] <= 5.0, f"{path}.bak_median", "must be in [1, 5]")
	_expect(1.0 <= result["sig_median"] <= 5.0, f"{path}.sig_median", "must be in [1, 5]")
	_expect(0.0 <= result["estoi_median"] <= 1.0, f"{path}.estoi_median", "must be in [0, 1]")
	_expect(result["wer_percent"] >= 0.0, f"{path}.wer_percent", "must be non-negative")
	return result


def select_on_validation(
	training_plan: Mapping[str, Any],
	validation: Mapping[str, Any],
	*,
	training_plan_sha256: str,
	validation_results_sha256: str,
	model_root: Path,
) -> Mapping[str, Any]:
	try:
		CAMPAIGN.validate_frozen_campaign(training_plan)
	except ValueError as error:
		raise SelectionError(str(error)) from error
	_hash(training_plan_sha256, "training_plan_sha256")
	_hash(validation_results_sha256, "validation_results_sha256")
	evidence = _exact_keys(validation, {
		"candidates", "schema_version", "stage", "training_plan_sha256", "validation_case_ids_sha256",
		"validation_mixture_plan_sha256",
	}, set(), "validation")
	_expect(evidence["schema_version"] == 1, "validation.schema_version", "unsupported version")
	_expect(evidence["stage"] == "validation", "validation.stage", "must be validation")
	_expect(evidence["training_plan_sha256"] == training_plan_sha256, "validation.training_plan_sha256", "plan mismatch")
	_expect(evidence["validation_mixture_plan_sha256"] == training_plan["inputs"]["validation_mixture_plan"]["file_sha256"],
		"validation.validation_mixture_plan_sha256", "validation split was not the pre-frozen plan")
	_expect(evidence["validation_case_ids_sha256"] == training_plan["inputs"]["validation_mixture_plan"]["case_ids_sha256"],
		"validation.validation_case_ids_sha256", "validation case set was not the pre-frozen plan")
	jobs = {job["candidate_id"]: job for job in training_plan["training_jobs"]}
	_expect(isinstance(evidence["candidates"], list), "validation.candidates", "must be an array")
	validated: list[dict[str, Any]] = []
	for index, candidate_value in enumerate(evidence["candidates"]):
		path = f"validation.candidates[{index}]"
		candidate = _exact_keys(candidate_value, {"candidate_id", "gates", "metrics", "model", "seed"}, set(), path)
		candidate_id = _identifier(candidate["candidate_id"], f"{path}.candidate_id")
		_expect(candidate_id in jobs, f"{path}.candidate_id", "not in frozen training plan")
		job = jobs[candidate_id]
		_expect(candidate["seed"] == job["seed"], f"{path}.seed", "does not match frozen training seed")
		model = _exact_keys(candidate["model"], {"relative_path", "sha256", "size_bytes"}, set(), f"{path}.model")
		_expect(model["relative_path"] == job["output_manifest_relative_path"], f"{path}.model.relative_path",
			"does not match frozen output path")
		_artifact(model_root, model["relative_path"], model["sha256"], model["size_bytes"], f"{path}.model")
		gates = _exact_keys(candidate["gates"], {"model_hash_error_count", "nan_inf_count", "unexplained_fallback_count"}, set(), f"{path}.gates")
		for key in gates:
			_expect(isinstance(gates[key], int) and not isinstance(gates[key], bool) and gates[key] >= 0, f"{path}.gates.{key}",
				"must be a non-negative integer")
		metrics = _validate_validation_metrics(candidate["metrics"], f"{path}.metrics")
		_expect(metrics["case_count"] == training_plan["inputs"]["validation_mixture_plan"]["case_count"],
			f"{path}.metrics.case_count", "does not cover the complete pre-frozen validation plan")
		validated.append({
			"candidate_id": candidate_id,
			"eligible": all(gates[key] == 0 for key in gates),
			"gates": dict(gates),
			"metrics": dict(metrics),
			"model": dict(model),
			"seed": candidate["seed"],
		})
	ids = [candidate["candidate_id"] for candidate in validated]
	_expect(ids == sorted(set(ids)), "validation.candidates", "must contain unique candidates sorted by ID")
	_expect(set(ids) == set(jobs), "validation.candidates", "must contain exactly every frozen training candidate")
	eligible = [candidate for candidate in validated if candidate["eligible"]]
	_expect(eligible, "validation.candidates", "no candidate passed validation integrity gates")
	ranked = sorted(eligible, key=lambda candidate: (
		-candidate["metrics"]["ovrl_median"],
		-candidate["metrics"]["bak_median"],
		-candidate["metrics"]["sig_median"],
		-candidate["metrics"]["estoi_median"],
		candidate["metrics"]["wer_percent"],
		candidate["candidate_id"],
	))
	selected = ranked[0]
	return {
		"ranking": [{"candidate_id": candidate["candidate_id"], "metrics": candidate["metrics"]} for candidate in ranked],
		"schema_version": 1,
		"selected": {
			"candidate_id": selected["candidate_id"],
			"metrics": selected["metrics"],
			"model": selected["model"],
			"seed": selected["seed"],
		},
		"selection_method": "validation-only lexicographic: OVRL, BAK, SIG, eSTOI, inverse WER, candidate ID",
		"training_plan_sha256": training_plan_sha256,
		"validation_case_ids_sha256": evidence["validation_case_ids_sha256"],
		"validation_mixture_plan_sha256": evidence["validation_mixture_plan_sha256"],
		"validation_results_sha256": validation_results_sha256,
	}


def validate_selection(value: Any) -> Mapping[str, Any]:
	selection = _exact_keys(value, {
		"ranking", "schema_version", "selected", "selection_method", "training_plan_sha256", "validation_mixture_plan_sha256",
		"validation_case_ids_sha256", "validation_results_sha256",
	}, set(), "selection")
	_expect(selection["schema_version"] == 1, "selection.schema_version", "unsupported version")
	_hash(selection["training_plan_sha256"], "selection.training_plan_sha256")
	_hash(selection["validation_results_sha256"], "selection.validation_results_sha256")
	_hash(selection["validation_mixture_plan_sha256"], "selection.validation_mixture_plan_sha256")
	_hash(selection["validation_case_ids_sha256"], "selection.validation_case_ids_sha256")
	selected = _exact_keys(selection["selected"], {"candidate_id", "metrics", "model", "seed"}, set(), "selection.selected")
	_identifier(selected["candidate_id"], "selection.selected.candidate_id")
	model = _exact_keys(selected["model"], {"relative_path", "sha256", "size_bytes"}, set(), "selection.selected.model")
	_safe_relative_path(model["relative_path"], "selection.selected.model.relative_path")
	_hash(model["sha256"], "selection.selected.model.sha256")
	return selection


def _bootstrap_lower_bound(differences: Sequence[float], seed_material: str) -> Mapping[str, Any]:
	seed_hash = hashlib.sha256(seed_material.encode("utf-8")).hexdigest()
	state = int(seed_hash[:16], 16)
	mask = (1 << 64) - 1

	def next_index(count: int) -> int:
		nonlocal state
		state = (state + 0x9E3779B97F4A7C15) & mask
		value = state
		value = ((value ^ (value >> 30)) * 0xBF58476D1CE4E5B9) & mask
		value = ((value ^ (value >> 27)) * 0x94D049BB133111EB) & mask
		value ^= value >> 31
		return value % count

	medians = []
	count = len(differences)
	for _ in range(BOOTSTRAP_ITERATIONS):
		medians.append(statistics.median(differences[next_index(count)] for _ in range(count)))
	medians.sort()
	lower_index = math.floor(0.025 * (BOOTSTRAP_ITERATIONS - 1))
	return {
		"confidence": 0.95,
		"iterations": BOOTSTRAP_ITERATIONS,
		"lower_bound": medians[lower_index],
		"median_improvement": statistics.median(differences),
		"metric": "paired per-case OVRL improvement, median bootstrap",
		"sampler": "splitmix64-v1",
		"seed_sha256": seed_hash,
	}


def _evaluate_gates(safety: Mapping[str, Any], performance: Mapping[str, Any]) -> list[str]:
	reasons: list[str] = []
	safety_limits = {
		"catastrophe_rate_percent": 0.5,
		"clean_dnsmos_sig_loss": 0.05,
		"clean_estoi_loss": 0.01,
		"clean_wer_loss_percentage_points": 1.0,
		"max_edge_loss_ms": 10.0,
	}
	zero_safety = ("model_hash_error_count", "nan_inf_count", "new_clipping_count", "tail_loss_count", "unexplained_fallback_count")
	for key in zero_safety:
		value = safety[key]
		_expect(isinstance(value, int) and not isinstance(value, bool) and value >= 0, f"holdout.safety.{key}", "must be non-negative")
		if value != 0:
			reasons.append(f"safety.{key}")
	for key, maximum in safety_limits.items():
		value = _finite_number(safety[key], f"holdout.safety.{key}")
		_expect(value >= 0.0, f"holdout.safety.{key}", "must be non-negative")
		if value > maximum:
			reasons.append(f"safety.{key}")
	for key, maximum in (("rtf_mean", 0.15), ("callback_p99_ms", 5.0), ("max_processing_ms", 10.0)):
		value = _finite_number(performance[key], f"holdout.performance.{key}")
		_expect(value >= 0.0, f"holdout.performance.{key}", "must be non-negative")
		if value > maximum:
			reasons.append(f"performance.{key}")
	soak = _finite_number(performance["soak_duration_seconds"], "holdout.performance.soak_duration_seconds")
	if soak < 3600.0:
		reasons.append("performance.soak_duration_seconds")
	deadline = performance["deadline_miss_count"]
	memory = performance["memory_growth_bytes_after_warmup"]
	_expect(isinstance(deadline, int) and not isinstance(deadline, bool) and deadline >= 0,
		"holdout.performance.deadline_miss_count", "must be non-negative")
	_expect(isinstance(memory, int) and not isinstance(memory, bool), "holdout.performance.memory_growth_bytes_after_warmup", "must be integer")
	if deadline != 0:
		reasons.append("performance.deadline_miss_count")
	if memory > 0:
		reasons.append("performance.memory_growth_bytes_after_warmup")
	return reasons


def evaluate_holdout(
	training_plan: Mapping[str, Any],
	selection: Mapping[str, Any],
	holdout: Mapping[str, Any],
	*,
	training_plan_sha256: str,
	selection_sha256: str,
	holdout_results_sha256: str,
	model_root: Path,
	embedded_reference_path: Path,
) -> tuple[Mapping[str, Any], Mapping[str, Any], Mapping[str, Any] | None]:
	CAMPAIGN.validate_frozen_campaign(training_plan)
	validate_selection(selection)
	_expect(selection["training_plan_sha256"] == training_plan_sha256, "selection.training_plan_sha256", "plan mismatch")
	evidence = _exact_keys(holdout, {
		"embedded_reference", "holdout_mixture_plan_sha256", "paired_cases", "performance", "safety", "schema_version", "selected_candidate_id",
		"selected_model_sha256", "stage", "training_plan_sha256", "validation_selection_sha256",
	}, set(), "holdout")
	_expect(evidence["schema_version"] == 1, "holdout.schema_version", "unsupported version")
	_expect(evidence["stage"] == "holdout", "holdout.stage", "must be holdout")
	_expect(evidence["training_plan_sha256"] == training_plan_sha256, "holdout.training_plan_sha256", "plan mismatch")
	_expect(evidence["holdout_mixture_plan_sha256"] == training_plan["inputs"]["holdout_mixture_plan"]["file_sha256"],
		"holdout.holdout_mixture_plan_sha256", "holdout split was not the pre-frozen plan")
	_expect(evidence["validation_selection_sha256"] == selection_sha256, "holdout.validation_selection_sha256", "selection mismatch")
	selected = selection["selected"]
	_expect(evidence["selected_candidate_id"] == selected["candidate_id"], "holdout.selected_candidate_id",
		"holdout may evaluate only the validation-selected candidate")
	_expect(evidence["selected_model_sha256"] == selected["model"]["sha256"], "holdout.selected_model_sha256", "model mismatch")
	_artifact(model_root, selected["model"]["relative_path"], selected["model"]["sha256"], selected["model"]["size_bytes"],
		"selection.selected.model")
	embedded = _exact_keys(evidence["embedded_reference"], {"license_spdx", "model_id", "sha256", "size_bytes"}, set(), "holdout.embedded_reference")
	_expect(embedded["model_id"] == "rnnoise:embedded", "holdout.embedded_reference.model_id", "must be rnnoise:embedded")
	_expect(isinstance(embedded["license_spdx"], str) and bool(embedded["license_spdx"].strip()),
		"holdout.embedded_reference.license_spdx", "required")
	_hash(embedded["sha256"], "holdout.embedded_reference.sha256")
	_expect(isinstance(embedded["size_bytes"], int) and not isinstance(embedded["size_bytes"], bool) and embedded["size_bytes"] > 0,
		"holdout.embedded_reference.size_bytes", "must be positive")
	_expect(embedded_reference_path.is_file() and not embedded_reference_path.is_symlink(), "embedded_reference_path", "missing or symlink")
	_expect(embedded_reference_path.stat().st_size == embedded["size_bytes"], "holdout.embedded_reference.size_bytes", "artifact size mismatch")
	_expect(CAMPAIGN.file_sha256(embedded_reference_path) == embedded["sha256"], "holdout.embedded_reference.sha256", "artifact hash mismatch")
	_expect(isinstance(evidence["paired_cases"], list) and len(evidence["paired_cases"]) >= MINIMUM_PAIRED_HOLDOUT_CASES,
		"holdout.paired_cases", f"at least {MINIMUM_PAIRED_HOLDOUT_CASES} paired cases are required")
	case_ids: list[str] = []
	differences: list[float] = []
	for index, case_value in enumerate(evidence["paired_cases"]):
		case = _exact_keys(case_value, {"candidate_ovrl", "case_id", "embedded_ovrl"}, set(), f"holdout.paired_cases[{index}]")
		case_ids.append(_identifier(case["case_id"], f"holdout.paired_cases[{index}].case_id"))
		candidate_ovrl = _finite_number(case["candidate_ovrl"], f"holdout.paired_cases[{index}].candidate_ovrl")
		embedded_ovrl = _finite_number(case["embedded_ovrl"], f"holdout.paired_cases[{index}].embedded_ovrl")
		_expect(1.0 <= candidate_ovrl <= 5.0 and 1.0 <= embedded_ovrl <= 5.0, f"holdout.paired_cases[{index}]",
			"OVRL values must be in [1, 5]")
		differences.append(candidate_ovrl - embedded_ovrl)
	_expect(case_ids == sorted(set(case_ids)), "holdout.paired_cases", "case IDs must be unique and sorted")
	_expect(len(case_ids) == training_plan["inputs"]["holdout_mixture_plan"]["case_count"], "holdout.paired_cases",
		"does not cover the complete pre-frozen holdout plan")
	_expect(CAMPAIGN.canonical_sha256(case_ids) == training_plan["inputs"]["holdout_mixture_plan"]["case_ids_sha256"],
		"holdout.paired_cases", "case IDs do not match the pre-frozen holdout plan")
	safety = _exact_keys(evidence["safety"], {
		"catastrophe_rate_percent", "clean_dnsmos_sig_loss", "clean_estoi_loss", "clean_wer_loss_percentage_points",
		"max_edge_loss_ms", "model_hash_error_count", "nan_inf_count", "new_clipping_count", "tail_loss_count",
		"unexplained_fallback_count",
	}, set(), "holdout.safety")
	performance = _exact_keys(evidence["performance"], {
		"callback_p99_ms", "deadline_miss_count", "max_processing_ms", "memory_growth_bytes_after_warmup", "rtf_mean",
		"soak_duration_seconds",
	}, set(), "holdout.performance")
	reasons = _evaluate_gates(safety, performance)
	bootstrap = _bootstrap_lower_bound(differences, f"{selection_sha256}\0{holdout_results_sha256}")
	if bootstrap["lower_bound"] <= 0.0:
		reasons.append("bootstrap.ovrl_lower_bound_not_positive")
	status = "custom-selected" if not reasons else "embedded-retained"
	custom_model = None
	manifest_fragment = None
	if status == "custom-selected":
		model_id = f"rnnoise:custom:{training_plan['campaign_id']}:{selected['candidate_id']}"
		custom_model = {
			"candidate_id": selected["candidate_id"],
			"manifest_relative_path": selected["model"]["relative_path"],
			"model_id": model_id,
			"sha256": selected["model"]["sha256"],
			"size_bytes": selected["model"]["size_bytes"],
		}
		manifest_fragment = {
			"model": {
				"algorithmicLatencyMs": 30,
				"backend": "RNNoise",
				"id": model_id,
				"licenseSpdx": training_plan["toolchain"]["output_model"]["license_spdx"],
				"path": selected["model"]["relative_path"],
				"recipeCompatibility": ["input.balanced.rnnoise-custom"],
				"sampleRateHz": 48000,
				"sha256": selected["model"]["sha256"],
				"sizeBytes": selected["model"]["size_bytes"],
			},
			"schemaVersion": 1,
		}
	decision = {
		"bootstrap": bootstrap,
		"custom_model": custom_model,
		"embedded_reference": dict(embedded),
		"holdout_results_sha256": holdout_results_sha256,
		"holdout_mixture_plan_sha256": evidence["holdout_mixture_plan_sha256"],
		"reason_codes": sorted(reasons),
		"schema_version": 1,
		"status": status,
		"training_plan_sha256": training_plan_sha256,
		"validation_selection_sha256": selection_sha256,
	}
	model_card = {
		"decision": {"reason_codes": sorted(reasons), "status": status},
		"evaluation": {
			"bootstrap": bootstrap,
			"holdout_results_sha256": holdout_results_sha256,
			"holdout_mixture_plan_sha256": evidence["holdout_mixture_plan_sha256"],
			"performance_gates": dict(performance),
			"safety_gates": dict(safety),
		},
		"intended_use": "Mumble Balanced input enhancement at 48 kHz before the unchanged Opus transport",
		"known_limitations": [
			"Approved only for the corpus cohorts and Windows/WASAPI hardware classes represented by qualification evidence.",
			"Must fall back to the embedded RNNoise model on hash, initialization, deadline, NaN or Inf failure.",
			"Does not authorize Auto, Voice Focus, receiver cleanup, speaker identification, or demographic classification.",
		],
		"model": custom_model if custom_model is not None else dict(embedded),
		"model_license": training_plan["toolchain"]["output_model"] if custom_model is not None else {
			"license_spdx": embedded["license_spdx"], "source": "hash-bound embedded product reference"
		},
		"provenance": {
			"approved_training_sources": training_plan["approved_training_sources"],
			"campaign_id": training_plan["campaign_id"],
			"input_fingerprint_sha256": training_plan["input_fingerprint_sha256"],
			"training_plan_sha256": training_plan_sha256,
			"training_seeds": [job["seed"] for job in training_plan["training_jobs"]],
			"validation_results_sha256": selection["validation_results_sha256"],
			"validation_selected_candidate": selection["selected"],
			"validation_selection_sha256": selection_sha256,
		},
		"schema_version": 1,
	}
	return decision, model_card, manifest_fragment


def _write_model_card_markdown(path: Path, card: Mapping[str, Any]) -> None:
	model = card["model"]
	lines = [
		"# RNNoise model card",
		"",
		f"- Decision: `{card['decision']['status']}`",
		f"- Campaign: `{card['provenance']['campaign_id']}`",
		f"- Training-plan SHA-256: `{card['provenance']['training_plan_sha256']}`",
		f"- Holdout SHA-256: `{card['evaluation']['holdout_results_sha256']}`",
		f"- Product model: `{model['model_id']}`",
		f"- Bootstrap 95% lower bound: `{card['evaluation']['bootstrap']['lower_bound']:.6f}` OVRL",
		"",
		"## Intended use",
		"",
		card["intended_use"],
		"",
		"## Known limitations",
		"",
	]
	lines.extend(f"- {item}" for item in card["known_limitations"])
	lines.extend(["", "## Approved training sources", ""])
	lines.extend(f"- `{source['id']}` — `{source['license']['spdx']}`" for source in card["provenance"]["approved_training_sources"])
	path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def open_holdout_once(
	training_plan_path: Path,
	selection_path: Path,
	holdout_results_path: Path,
	model_root: Path,
	embedded_reference_path: Path,
	receipt_path: Path,
	output_dir: Path,
) -> Mapping[str, Any]:
	_expect(not os.path.lexists(output_dir), "output_dir", "must not already exist")
	_expect(not os.path.lexists(receipt_path), "receipt", "holdout was already opened or receipt path is occupied")
	training_plan = CAMPAIGN.load_json(training_plan_path)
	selection = CAMPAIGN.load_json(selection_path)
	holdout = CAMPAIGN.load_json(holdout_results_path)
	training_sha = CAMPAIGN.file_sha256(training_plan_path)
	selection_sha = CAMPAIGN.file_sha256(selection_path)
	holdout_sha = CAMPAIGN.file_sha256(holdout_results_path)
	decision, card, fragment = evaluate_holdout(
		training_plan,
		selection,
		holdout,
		training_plan_sha256=training_sha,
		selection_sha256=selection_sha,
		holdout_results_sha256=holdout_sha,
		model_root=model_root,
		embedded_reference_path=embedded_reference_path,
	)
	receipt = {
		"holdout_results_sha256": holdout_sha,
		"holdout_mixture_plan_sha256": training_plan["inputs"]["holdout_mixture_plan"]["file_sha256"],
		"schema_version": 1,
		"status": "consumed",
		"training_plan_sha256": training_sha,
		"validation_selection_sha256": selection_sha,
	}
	receipt_path.parent.mkdir(parents=True, exist_ok=True)
	with receipt_path.open("x", encoding="utf-8") as stream:
		json.dump(receipt, stream, indent=2, sort_keys=True)
		stream.write("\n")
	decision = dict(decision)
	decision["one_shot_receipt_sha256"] = CAMPAIGN.file_sha256(receipt_path)
	output_dir.mkdir(parents=True, exist_ok=False)
	_write_json(output_dir / "decision.json", decision)
	_write_json(output_dir / "rnnoise-model-card.json", card)
	_write_model_card_markdown(output_dir / "rnnoise-model-card.md", card)
	if fragment is not None:
		_write_json(output_dir / "input-models.rnnoise-custom.fragment.json", fragment)
	return decision


def _self_test_plan(root: Path) -> Path:
	validation_case_ids = [f"validation-{index:03d}" for index in range(50)]
	holdout_case_ids = [f"holdout-{index:03d}" for index in range(40)]
	inputs = {
		"corpus_lock": {"canonical_sha256": "1" * 64, "file_sha256": "2" * 64},
		"inventory": {"canonical_sha256": "3" * 64, "file_sha256": "4" * 64},
		"toolchain_manifest": {"canonical_sha256": "5" * 64, "file_sha256": "6" * 64},
		"tuning_mixture_plan": {
			"canonical_sha256": "7" * 64, "case_count": 100,
			"case_ids_sha256": CAMPAIGN.canonical_sha256([f"tuning-{index:03d}" for index in range(100)]), "file_sha256": "8" * 64,
		},
		"validation_mixture_plan": {
			"canonical_sha256": "9" * 64, "case_count": len(validation_case_ids),
			"case_ids_sha256": CAMPAIGN.canonical_sha256(validation_case_ids), "file_sha256": "a" * 64,
		},
		"holdout_mixture_plan": {
			"canonical_sha256": "b" * 64, "case_count": len(holdout_case_ids),
			"case_ids_sha256": CAMPAIGN.canonical_sha256(holdout_case_ids), "file_sha256": "c" * 64,
		},
	}
	input_fingerprint = CAMPAIGN.canonical_sha256(inputs)
	seed_root = "self-test"
	jobs = []
	for index in range(5):
		seed_digest = hashlib.sha256(f"rnnoise-seed-v1\0{seed_root}\0{input_fingerprint}\0{index}".encode("utf-8")).digest()
		seed = int.from_bytes(seed_digest[:8], "big")
		candidate_id = f"rnnoise-{index + 1:02d}-{seed_digest.hex()[:12]}"
		model_path = f"rnnoise/custom/self-test/{candidate_id}.weights_blob.bin"
		artifact = root / Path(*PurePosixPath(model_path).parts)
		artifact.parent.mkdir(parents=True, exist_ok=True)
		artifact.write_bytes(f"candidate-{index}".encode("ascii"))
		jobs.append({
			"candidate_id": candidate_id,
			"input_fingerprint_sha256": CAMPAIGN.canonical_sha256({"inputs": inputs, "seed": seed}),
			"output_manifest_relative_path": model_path,
			"seed": seed,
		})
	toolchain_files = [
		{"relative_path": "ATTRIBUTION.txt", "role": "attribution-notice", "sha256": "4" * 64, "size_bytes": 1},
		{"relative_path": "trainer.bin", "role": "trainer", "sha256": "5" * 64, "size_bytes": 1},
		{"relative_path": "training.json", "role": "training-config", "sha256": "6" * 64, "size_bytes": 1},
	]
	plan = {
		"approved_training_sources": [
			{
				"id": "self-test-noise",
				"integrity": {"algorithm": "sha256", "digest": "b" * 64, "size_bytes": 1},
				"kind": "environmental_noise",
				"license": {"evidence_url": "https://example.invalid", "spdx": "CC0-1.0", "status": "verified"},
				"roles": ["training_candidate"],
				"training_status": "allowed_with_attribution",
			},
			{
				"id": "self-test-speech",
				"integrity": {"algorithm": "sha256", "digest": "a" * 64, "size_bytes": 1},
				"kind": "clean_speech",
				"license": {"evidence_url": "https://example.invalid", "spdx": "CC0-1.0", "status": "verified"},
				"roles": ["training_candidate"],
				"training_status": "allowed_with_attribution",
			},
		],
		"campaign_id": "self-test",
		"input_fingerprint_sha256": input_fingerprint,
		"inputs": inputs,
		"schema_version": 1,
		"seed_derivation": CAMPAIGN.SEED_DERIVATION,
		"seed_root": seed_root,
		"toolchain": {
			"files": toolchain_files,
			"output_model": {
				"attribution_file_relative_path": "ATTRIBUTION.txt",
				"attribution_sha256": "4" * 64,
				"license_spdx": "LicenseRef-Mumble-RNNoise-SelfTest",
			},
			"runtime": {"name": "self-test", "revision": "locked", "version": "1"},
			"schema_version": 1,
			"toolchain_id": "rnnoise-self-test",
		},
		"training_jobs": jobs,
	}
	path = root / "training-plan.json"
	_write_json(path, plan)
	return path


def run_self_test() -> None:
	with tempfile.TemporaryDirectory() as temporary:
		root = Path(temporary)
		plan_path = _self_test_plan(root)
		plan = CAMPAIGN.load_json(plan_path)
		plan_sha = CAMPAIGN.file_sha256(plan_path)
		candidates = []
		for index, job in enumerate(plan["training_jobs"]):
			artifact = root / Path(*PurePosixPath(job["output_manifest_relative_path"]).parts)
			candidates.append({
				"candidate_id": job["candidate_id"],
				"gates": {"model_hash_error_count": 0, "nan_inf_count": 0, "unexplained_fallback_count": 0},
				"metrics": {
					"bak_median": 3.0 + index / 10,
					"case_count": 50,
					"estoi_median": 0.8,
					"ovrl_median": 3.0 + index / 10,
					"sig_median": 3.5,
					"wer_percent": 5.0,
				},
				"model": {
					"relative_path": job["output_manifest_relative_path"],
					"sha256": CAMPAIGN.file_sha256(artifact),
					"size_bytes": artifact.stat().st_size,
				},
				"seed": job["seed"],
			})
		validation_path = root / "validation.json"
		_write_json(validation_path, {
			"candidates": candidates,
			"schema_version": 1,
			"stage": "validation",
			"training_plan_sha256": plan_sha,
			"validation_case_ids_sha256": plan["inputs"]["validation_mixture_plan"]["case_ids_sha256"],
			"validation_mixture_plan_sha256": plan["inputs"]["validation_mixture_plan"]["file_sha256"],
		})
		selection = select_on_validation(plan, CAMPAIGN.load_json(validation_path), training_plan_sha256=plan_sha,
			validation_results_sha256=CAMPAIGN.file_sha256(validation_path), model_root=root)
		_expect(selection["selected"]["candidate_id"] == candidates[-1]["candidate_id"], "self-test", "validation selected wrong candidate")
		selection_path = root / "selection.json"
		_write_json(selection_path, selection)
		embedded_path = root / "rnnoise-embedded-reference.bin"
		embedded_path.write_bytes(b"embedded reference")
		holdout_case_ids = [f"holdout-{index:03d}" for index in range(plan["inputs"]["holdout_mixture_plan"]["case_count"])]
		base_holdout = {
			"embedded_reference": {
				"license_spdx": "BSD-3-Clause",
				"model_id": "rnnoise:embedded",
				"sha256": CAMPAIGN.file_sha256(embedded_path),
				"size_bytes": embedded_path.stat().st_size,
			},
			"paired_cases": [
				{"candidate_ovrl": 3.4, "case_id": case_id, "embedded_ovrl": 3.1}
				for case_id in holdout_case_ids
			],
			"holdout_mixture_plan_sha256": plan["inputs"]["holdout_mixture_plan"]["file_sha256"],
			"performance": {
				"callback_p99_ms": 4.0,
				"deadline_miss_count": 0,
				"max_processing_ms": 9.0,
				"memory_growth_bytes_after_warmup": 0,
				"rtf_mean": 0.12,
				"soak_duration_seconds": 3600,
			},
			"safety": {
				"catastrophe_rate_percent": 0.0,
				"clean_dnsmos_sig_loss": 0.01,
				"clean_estoi_loss": 0.001,
				"clean_wer_loss_percentage_points": 0.2,
				"max_edge_loss_ms": 10.0,
				"model_hash_error_count": 0,
				"nan_inf_count": 0,
				"new_clipping_count": 0,
				"tail_loss_count": 0,
				"unexplained_fallback_count": 0,
			},
			"schema_version": 1,
			"selected_candidate_id": selection["selected"]["candidate_id"],
			"selected_model_sha256": selection["selected"]["model"]["sha256"],
			"stage": "holdout",
			"training_plan_sha256": plan_sha,
			"validation_selection_sha256": CAMPAIGN.file_sha256(selection_path),
		}
		custom_holdout = root / "custom-holdout.json"
		_write_json(custom_holdout, base_holdout)
		custom_receipt = root / "custom.receipt.json"
		custom_output = root / "custom-output"
		decision = open_holdout_once(plan_path, selection_path, custom_holdout, root, embedded_path, custom_receipt, custom_output)
		_expect(decision["status"] == "custom-selected", "self-test", "positive custom model was not selected")
		_expect((custom_output / "input-models.rnnoise-custom.fragment.json").is_file(), "self-test", "custom manifest missing")
		try:
			open_holdout_once(plan_path, selection_path, custom_holdout, root, embedded_path, custom_receipt, root / "repeat-output")
		except SelectionError as error:
			_expect("receipt" in str(error), "self-test", "repeat did not fail on receipt")
		else:
			raise SelectionError("self-test: one-shot holdout was opened twice")
		retained = json.loads(json.dumps(base_holdout))
		for case in retained["paired_cases"]:
			case["candidate_ovrl"] = case["embedded_ovrl"]
		retained_path = root / "retained-holdout.json"
		_write_json(retained_path, retained)
		retained_decision = open_holdout_once(plan_path, selection_path, retained_path, root, embedded_path,
			root / "retained.receipt.json", root / "retained-output")
		_expect(retained_decision["status"] == "embedded-retained", "self-test", "non-positive bootstrap did not retain embedded")
		_expect("bootstrap.ovrl_lower_bound_not_positive" in retained_decision["reason_codes"], "self-test", "bootstrap reason missing")
		unsafe = json.loads(json.dumps(base_holdout))
		unsafe["safety"]["new_clipping_count"] = 1
		unsafe_path = root / "unsafe-holdout.json"
		_write_json(unsafe_path, unsafe)
		unsafe_decision = open_holdout_once(plan_path, selection_path, unsafe_path, root, embedded_path,
			root / "unsafe.receipt.json", root / "unsafe-output")
		_expect(unsafe_decision["status"] == "embedded-retained", "self-test", "failed safety gate did not retain embedded")
		_expect("safety.new_clipping_count" in unsafe_decision["reason_codes"], "self-test", "safety retention reason missing")


def _parser() -> argparse.ArgumentParser:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--self-test", action="store_true")
	subparsers = parser.add_subparsers(dest="command")
	validation = subparsers.add_parser("select-validation", help="seal one model using validation results only")
	validation.add_argument("--training-plan", type=Path, required=True)
	validation.add_argument("--validation-results", type=Path, required=True)
	validation.add_argument("--model-root", type=Path, required=True)
	validation.add_argument("--output", type=Path, required=True)
	holdout = subparsers.add_parser("open-holdout", help="consume one holdout receipt and emit the final decision")
	holdout.add_argument("--training-plan", type=Path, required=True)
	holdout.add_argument("--selection", type=Path, required=True)
	holdout.add_argument("--holdout-results", type=Path, required=True)
	holdout.add_argument("--model-root", type=Path, required=True)
	holdout.add_argument("--embedded-reference-path", type=Path, required=True)
	holdout.add_argument("--receipt", type=Path, required=True)
	holdout.add_argument("--output-dir", type=Path, required=True)
	return parser


def main(argv: Sequence[str] | None = None) -> int:
	args = _parser().parse_args(argv)
	try:
		if args.self_test:
			run_self_test()
			print("RNNoise model selection self-test: ok")
			return 0
		_expect(args.command is not None, "command", "select-validation or open-holdout is required")
		if args.command == "select-validation":
			_expect(not os.path.lexists(args.output), "--output", "sealed validation selection must not already exist")
			training_plan = CAMPAIGN.load_json(args.training_plan)
			validation = CAMPAIGN.load_json(args.validation_results)
			selection = select_on_validation(
				training_plan,
				validation,
				training_plan_sha256=CAMPAIGN.file_sha256(args.training_plan),
				validation_results_sha256=CAMPAIGN.file_sha256(args.validation_results),
				model_root=args.model_root,
			)
			_write_json(args.output, selection)
			print(f"RNNoise model selection: validation sealed {selection['selected']['candidate_id']}; sha256={CAMPAIGN.file_sha256(args.output)}")
		else:
			decision = open_holdout_once(args.training_plan, args.selection, args.holdout_results, args.model_root,
				args.embedded_reference_path, args.receipt, args.output_dir)
			print(f"RNNoise model selection: {decision['status']}; bootstrap-lower={decision['bootstrap']['lower_bound']:.6f}")
		return 0
	except (SelectionError, OSError, ValueError) as error:
		print(f"RNNoise model selection: FAIL: {error}", file=sys.stderr)
		return 1


if __name__ == "__main__":
	sys.exit(main())
