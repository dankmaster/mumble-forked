#!/usr/bin/env python3
"""Orchestrate a hash-bound client-1/server/client-2 quality case.

The tracked core owns the portable contract, provenance, Original pairing, and
fixed-timeline scoring.  A runner-local adapter owns only machine-specific
process launch, client automation, and capture.  The adapter is invoked as:

    adapter --contract <adapter-contract.json> --result <adapter-result.json>

No corpus or captured audio is copied into the evidence manifest.
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

from payload_identity import (
	PayloadIdentityError,
	canonical_json_sha256 as canonical_payload_json_sha256,
	payload_file_attestation,
	payload_tree_attestation,
)


class E2EError(RuntimeError):
	"""Raised when a run cannot produce trustworthy two-client evidence."""


HEX64 = re.compile(r"[0-9a-f]{64}")
SAMPLE_RATE_HZ = 48_000
FRAME_SAMPLES = 480
# The unchanged local Mumble route has a bounded startup buffer that is not
# profile latency: four fixed 10 ms route frames plus the selected Opus packet
# duration. Original parity is qualified separately against the frozen legacy
# client. Receiver captures may consume this route budget; the hard one-frame
# input edge/tail gate is enforced independently on sender pre-Opus PCM.
ORIGINAL_ROUTE_FIXED_STARTUP_FRAMES = 4

PERFORMANCE_BUDGETS = {
	"Original": {"callback_p99_ms": 5.0, "worker_p99_ms": 5.0, "mean_rtf": 0.15},
	"Light": {"callback_p99_ms": 5.0, "worker_p99_ms": 5.0, "mean_rtf": 0.15},
	"Balanced": {"callback_p99_ms": 5.0, "worker_p99_ms": 5.0, "mean_rtf": 0.15},
	"Quality": {"callback_p99_ms": 8.0, "worker_p99_ms": 8.0, "mean_rtf": 0.35},
	"VoiceFocus": {"callback_p99_ms": 8.0, "worker_p99_ms": 8.0, "mean_rtf": 0.35},
	"Auto": {"callback_p99_ms": 8.0, "worker_p99_ms": 8.0, "mean_rtf": 0.35},
}
CORE_PROFILES = ("Original", "Light", "Balanced", "Quality", "VoiceFocus")
PRODUCT_PROFILES = (*CORE_PROFILES, "Auto")
FIXED_RECIPE_CONTRACTS = {
	"Original": ({"None"}, 0.0, "Low"),
	"Light": ({"Speex"}, 10.0, "Low"),
	"Balanced": ({"RNNoise"}, 30.0, "Standard"),
	"Quality": ({"DeepFilterNet"}, 50.0, "High"),
	"VoiceFocus": ({"DeepFilterNet"}, 50.0, "High"),
}
FIXED_PROFILE_MODEL_INITIALIZATION_ATTEMPTS = {
	"Original": 0,
	"Light": 0,
	"Balanced": 1,
	"Quality": 1,
	"VoiceFocus": 1,
}
MODEL_FIELDS = {
	"id", "version", "backend", "path", "sha256", "size", "licenseSpdx",
	"sampleRateHz", "algorithmicLatencyMs", "recipeCompatibility",
}
RECIPE_FIELDS = {
	"id", "revision", "profile", "engine", "modelIds", "noiseReductionRange",
	"naturalCrispRange", "latencyBudgetMs", "minimumCpuClass", "executionSemanticsVersion",
	"mixCurveVersion", "adaptationPolicyVersion",
}
EXECUTION_IDENTITY_FIELDS = {
	"run_provenance_sha256", "runtime_payload_sha256", "client_binary_sha256",
	"server_binary_sha256", "model_manifest_sha256", "recipe_manifest_sha256",
}


def _load_script(name: str, module_name: str) -> Any:
	path = Path(__file__).with_name(name)
	spec = importlib.util.spec_from_file_location(module_name, path)
	if spec is None or spec.loader is None:
		raise E2EError(f"unable to load {path}")
	module = importlib.util.module_from_spec(spec)
	spec.loader.exec_module(module)
	return module


PLAN = _load_script("generate-mixture-plan.py", "mumble_two_client_plan")
INVENTORY = _load_script("corpus-inventory-v3.py", "mumble_two_client_inventory")
LOCK = _load_script("validate-corpus-lock.py", "mumble_two_client_lock")


def _load_json(path: Path) -> Any:
	def reject_duplicates(pairs: Sequence[tuple[str, Any]]) -> MutableMapping[str, Any]:
		result: MutableMapping[str, Any] = {}
		for key, value in pairs:
			if key in result:
				raise E2EError(f"duplicate JSON key in {path}: {key}")
			result[key] = value
		return result
	def reject_constant(value: str) -> None:
		raise E2EError(f"non-finite JSON number in {path}: {value}")
	def finite_float(value: str) -> float:
		result = float(value)
		if not math.isfinite(result):
			raise E2EError(f"non-finite JSON number in {path}: {value}")
		return result
	try:
		return json.loads(
			path.read_text(encoding="utf-8"), object_pairs_hook=reject_duplicates,
			parse_constant=reject_constant, parse_float=finite_float,
		)
	except (OSError, json.JSONDecodeError) as error:
		raise E2EError(f"unable to read {path}: {error}") from error


def _file_sha256(path: Path) -> str:
	try:
		return str(payload_file_attestation(path)["sha256"])
	except PayloadIdentityError as error:
		raise E2EError(str(error)) from error


def _canonical_sha256(value: Any) -> str:
	try:
		return canonical_payload_json_sha256(value)
	except PayloadIdentityError as error:
		raise E2EError(str(error)) from error


def _lexical_absolute(path: Path) -> Path:
	return Path(os.path.abspath(os.fspath(path)))


def _safe_relative(value: Any, path: str) -> PurePosixPath:
	if not isinstance(value, str) or not value:
		raise E2EError(f"{path}: expected a non-empty relative path")
	parsed = PurePosixPath(value)
	if parsed.is_absolute() or parsed.as_posix() != value or "." in parsed.parts or ".." in parsed.parts:
		raise E2EError(f"{path}: unsafe path")
	return parsed


def _below(root: Path, relative: Any, label: str) -> Path:
	root = Path(os.path.abspath(os.fspath(root)))
	path = Path(os.path.abspath(os.fspath(root.joinpath(*_safe_relative(relative, label).parts))))
	try:
		path.relative_to(root)
	except ValueError as error:
		raise E2EError(f"{label}: path escapes root") from error
	return path


def _verified_file(path: Path, expected_hash: str | None = None, expected_size: int | None = None) -> Mapping[str, Any]:
	try:
		attestation = payload_file_attestation(path)
	except PayloadIdentityError as error:
		raise E2EError(str(error)) from error
	resolved = Path(str(attestation["path"]))
	size = int(attestation["size_bytes"])
	actual = str(attestation["sha256"])
	if expected_hash is not None and (not HEX64.fullmatch(expected_hash) or actual != expected_hash):
		raise E2EError(f"SHA-256 mismatch for {resolved}")
	if expected_size is not None and size != expected_size:
		raise E2EError(f"size mismatch for {resolved}: expected {expected_size}, got {size}")
	return {"path": str(resolved), "sha256": actual, "size_bytes": size}


def _is_reparse(path: Path) -> bool:
	metadata = os.lstat(path)
	return path.is_symlink() or bool(getattr(metadata, "st_file_attributes", 0) & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400))


def _assert_file_attestation(path: Path, attestation: Mapping[str, Any], label: str) -> None:
	current = _verified_file(path)
	if current["sha256"] != attestation["sha256"] or current["size_bytes"] != attestation["size_bytes"]:
		raise E2EError(f"{label} changed after run provenance was sealed")


def _tree_attestation(root: Path) -> Mapping[str, Any]:
	try:
		return payload_tree_attestation(root)
	except PayloadIdentityError as error:
		raise E2EError(str(error)) from error


def _assert_packaged_runtime_root(root: Path) -> None:
	"""Reject mutable CMake trees before sealing release runtime evidence."""
	root = root.resolve()
	if not root.is_dir():
		raise E2EError(f"runtime root does not exist: {root}")
	build_markers = [name for name in ("CMakeCache.txt", "build.ninja", "Testing") if (root / name).exists()]
	if build_markers:
		raise E2EError(
			"runtime root must be a staged/packaged payload, not a mutable CMake build tree; "
			f"found build marker(s): {', '.join(build_markers)}"
		)


def _verify_metric_models(manifest_path: Path) -> Mapping[str, Any]:
	manifest = _load_json(manifest_path)
	if not isinstance(manifest, dict) or set(manifest) != {"models", "runtime", "schema_version"}:
		raise E2EError("metrics manifest must contain only schema_version, runtime, and models")
	if manifest["schema_version"] != 1 or not isinstance(manifest["models"], list) or not manifest["models"]:
		raise E2EError("metrics manifest schema/models are invalid")
	runtime = manifest["runtime"]
	if not isinstance(runtime, dict) or set(runtime) != {"id", "relative_path", "sha256", "size_bytes", "version"}:
		raise E2EError("metrics runtime must pin id, version, path, size, and SHA-256")
	runtime_path = _below(manifest_path.parent, runtime["relative_path"], "metrics.runtime.relative_path")
	runtime_file = _verified_file(runtime_path, runtime["sha256"], runtime["size_bytes"])
	if not isinstance(runtime["id"], str) or not runtime["id"] or not isinstance(runtime["version"], str) or not runtime["version"]:
		raise E2EError("metrics runtime id/version must be non-empty")
	verified = []
	ids = []
	for index, model in enumerate(manifest["models"]):
		if not isinstance(model, dict) or set(model) != {"id", "relative_path", "sha256", "size_bytes"}:
			raise E2EError(f"metrics manifest models[{index}] has invalid keys")
		path = _below(manifest_path.parent, model["relative_path"], f"metrics.models[{index}].relative_path")
		info = _verified_file(path, model["sha256"], model["size_bytes"])
		verified.append({"id": model["id"], "sha256": info["sha256"], "size_bytes": info["size_bytes"]})
		ids.append(model["id"])
	if ids != sorted(set(ids)):
		raise E2EError("metrics model ids must be unique and sorted")
	required_ids = {"dnsmos", "estoi", "wer-en", "wer-sv"}
	if set(ids) != required_ids:
		raise E2EError(f"metrics manifest must pin exactly: {', '.join(sorted(required_ids))}")
	return {
		"manifest": _verified_file(manifest_path),
		"runtime": {"id": runtime["id"], "version": runtime["version"], **runtime_file},
		"models_sha256": _canonical_sha256(verified),
		"models": verified,
	}


def _exact_integer(value: Any, minimum: int, maximum: int, label: str) -> int:
	if not isinstance(value, int) or isinstance(value, bool) or not minimum <= value <= maximum:
		raise E2EError(f"{label}: expected an integer in [{minimum}, {maximum}]")
	return value


def _finite_number(value: Any, minimum: float, maximum: float, label: str) -> float:
	if isinstance(value, bool) or not isinstance(value, (int, float)):
		raise E2EError(f"{label}: expected a finite number")
	number = float(value)
	if not math.isfinite(number) or not minimum <= number <= maximum:
		raise E2EError(f"{label}: expected a finite number in [{minimum}, {maximum}]")
	return number


def _non_empty_text(value: Any, label: str) -> str:
	if not isinstance(value, str) or not value:
		raise E2EError(f"{label}: expected non-empty text")
	return value


def _sha256_text(value: Any, label: str) -> str:
	if not isinstance(value, str) or not HEX64.fullmatch(value):
		raise E2EError(f"{label}: expected lowercase SHA-256")
	return value


def _control_range(value: Any, label: str) -> None:
	if not isinstance(value, list) or len(value) != 2:
		raise E2EError(f"{label}: expected a two-element range")
	minimum = _exact_integer(value[0], 0, 100, f"{label}[0]")
	maximum = _exact_integer(value[1], 0, 100, f"{label}[1]")
	if minimum > maximum:
		raise E2EError(f"{label}: minimum exceeds maximum")


def _milliseconds_to_samples(value: Any, label: str) -> int:
	milliseconds = _finite_number(value, 0.0, 1000.0, label)
	exact = milliseconds * SAMPLE_RATE_HZ / 1000.0
	samples = round(exact)
	if not math.isclose(exact, samples, rel_tol=0.0, abs_tol=1e-9):
		raise E2EError(f"{label}: must resolve to an exact 48 kHz sample count")
	return samples


def _expected_recipe_latency_samples(
	engine: str, execution_semantics_version: int, model_ids: Sequence[str],
	models: Mapping[str, Mapping[str, Any]], latency_budget_ms: Any, label: str,
) -> int:
	if engine == "None":
		if model_ids:
			raise E2EError(f"{label}: Original must not bind a model")
		expected = 0
	elif engine == "Speex":
		if model_ids:
			raise E2EError(f"{label}: Speex must not bind a model")
		expected = FRAME_SAMPLES
	elif engine in {"RNNoise", "DeepFilterNet"}:
		if len(model_ids) != 1:
			raise E2EError(f"{label}: {engine} requires exactly one model")
		model = models[model_ids[0]]
		expected = int(model["algorithmic_latency_samples"])
		if engine == "DeepFilterNet":
			# Semantics v5 runs the realtime worker. It gives inference two full
			# callback periods and emits frame N at N+2; the catalog value is only
			# the model's intrinsic latency.
			expected += (2 if execution_semantics_version >= 5 else 1) * FRAME_SAMPLES
	else:
		raise E2EError(f"{label}: no qualified core latency contract for engine {engine}")
	budget = _milliseconds_to_samples(latency_budget_ms, f"{label}.latencyBudgetMs")
	if expected > budget:
		raise E2EError(f"{label}: exact execution latency exceeds latencyBudgetMs")
	if expected % FRAME_SAMPLES != 0:
		raise E2EError(f"{label}: exact execution latency must be 10 ms frame aligned")
	return expected


def _verify_product_catalog(
	model_manifest_path: Path, recipe_manifest_path: Path, runtime_root: Path
) -> Mapping[str, Any]:
	model_manifest = _load_json(model_manifest_path)
	recipe_manifest = _load_json(recipe_manifest_path)
	model_root_fields = {"schemaVersion", "catalogRevision", "generatedFromAssets", "models"}
	recipe_root_fields = {"schemaVersion", "catalogRevision", "modelManifestSha256", "recipes"}
	if not isinstance(model_manifest, dict) or set(model_manifest) != model_root_fields:
		raise E2EError("model manifest root schema is invalid")
	if not isinstance(recipe_manifest, dict) or set(recipe_manifest) != recipe_root_fields:
		raise E2EError("recipe manifest root schema is invalid")
	if model_manifest["schemaVersion"] != 1 or model_manifest["generatedFromAssets"] is not True:
		raise E2EError("model manifest must be generated schema v1")
	if recipe_manifest["schemaVersion"] != 2:
		raise E2EError("recipe manifest must be schema v2")
	catalog_revision = _non_empty_text(model_manifest["catalogRevision"], "model catalog revision")
	if recipe_manifest["catalogRevision"] != catalog_revision:
		raise E2EError("model and recipe catalog revisions differ")
	model_manifest_info = _verified_file(model_manifest_path)
	recipe_manifest_info = _verified_file(recipe_manifest_path)
	if _sha256_text(recipe_manifest["modelManifestSha256"], "recipe model-manifest SHA-256") != model_manifest_info["sha256"]:
		raise E2EError("recipe manifest is not bound to the exact model manifest bytes")
	if not isinstance(model_manifest["models"], list) or not model_manifest["models"]:
		raise E2EError("model manifest models must be a non-empty array")

	models: dict[str, Mapping[str, Any]] = {}
	model_compatibility: dict[str, set[str]] = {}
	asset_paths: set[str] = set()
	for index, model in enumerate(model_manifest["models"]):
		label = f"model manifest models[{index}]"
		if not isinstance(model, dict) or set(model) != MODEL_FIELDS:
			raise E2EError(f"{label}: invalid keys")
		model_id = _non_empty_text(model["id"], f"{label}.id")
		if model_id in models:
			raise E2EError(f"{label}.id: duplicate model {model_id}")
		version = _non_empty_text(model["version"], f"{label}.version")
		backend = _non_empty_text(model["backend"], f"{label}.backend")
		_non_empty_text(model["licenseSpdx"], f"{label}.licenseSpdx")
		relative_path = _safe_relative(model["path"], f"{label}.path")
		path_key = relative_path.as_posix().casefold()
		if path_key in asset_paths:
			raise E2EError(f"{label}.path: duplicate model asset path")
		asset_paths.add(path_key)
		size = _exact_integer(model["size"], 1, 4 * 1024 * 1024 * 1024, f"{label}.size")
		_exact_integer(model["sampleRateHz"], 1, 384000, f"{label}.sampleRateHz")
		algorithmic_latency_samples = _milliseconds_to_samples(
			model["algorithmicLatencyMs"], f"{label}.algorithmicLatencyMs"
		)
		asset = _below(runtime_root, relative_path.as_posix(), f"{label}.path")
		verified_asset = _verified_file(asset, _sha256_text(model["sha256"], f"{label}.sha256"), size)
		compatibility = model["recipeCompatibility"]
		if (
			not isinstance(compatibility, list)
			or any(not isinstance(item, str) or not item for item in compatibility)
			or len(compatibility) != len(set(compatibility))
		):
			raise E2EError(f"{label}.recipeCompatibility: invalid recipe ID list")
		models[model_id] = {
			"id": model_id,
			"version": version,
			"backend": backend,
			"sha256": verified_asset["sha256"],
			"algorithmic_latency_samples": algorithmic_latency_samples,
		}
		model_compatibility[model_id] = set(compatibility)

	if not isinstance(recipe_manifest["recipes"], list) or not recipe_manifest["recipes"]:
		raise E2EError("recipe manifest recipes must be a non-empty array")
	recipe_models: dict[str, set[str]] = {}
	expected_latency_samples_by_recipe_id: dict[str, int] = {}
	bindings: list[Mapping[str, Any]] = []
	seen_recipe_ids: set[str] = set()
	seen_profiles: set[str] = set()
	for index, recipe in enumerate(recipe_manifest["recipes"]):
		label = f"recipe manifest recipes[{index}]"
		if not isinstance(recipe, dict) or set(recipe) not in (RECIPE_FIELDS, RECIPE_FIELDS | {"advancedOnly"}):
			raise E2EError(f"{label}: invalid keys")
		recipe_id = _non_empty_text(recipe["id"], f"{label}.id")
		if recipe_id in seen_recipe_ids:
			raise E2EError(f"{label}.id: duplicate recipe {recipe_id}")
		seen_recipe_ids.add(recipe_id)
		profile = recipe["profile"]
		if profile not in PRODUCT_PROFILES:
			raise E2EError(f"{label}.profile: unsupported product profile")
		seen_profiles.add(profile)
		engine = recipe["engine"]
		if engine not in {"None", "Speex", "RNNoise", "DeepFilterNet", "DTLN"}:
			raise E2EError(f"{label}.engine: unsupported engine")
		revision = _exact_integer(recipe["revision"], 1, 2**31 - 1, f"{label}.revision")
		model_ids = recipe["modelIds"]
		if (
			not isinstance(model_ids, list)
			or len(model_ids) > 8
			or any(not isinstance(item, str) or not item for item in model_ids)
			or len(model_ids) != len(set(model_ids))
		):
			raise E2EError(f"{label}.modelIds: invalid model ID list")
		neural = engine in {"RNNoise", "DeepFilterNet", "DTLN"}
		if neural != bool(model_ids):
			raise E2EError(f"{label}: engine/model relationship is invalid")
		for model_id in model_ids:
			model = models.get(model_id)
			if model is None or model["backend"] != engine:
				raise E2EError(f"{label}: unknown or incompatible model {model_id}")
			if recipe_id not in model_compatibility[model_id]:
				raise E2EError(f"{label}: model {model_id} does not declare recipe compatibility")
		_control_range(recipe["noiseReductionRange"], f"{label}.noiseReductionRange")
		_control_range(recipe["naturalCrispRange"], f"{label}.naturalCrispRange")
		latency_ms = _finite_number(recipe["latencyBudgetMs"], 0.0, 1000.0, f"{label}.latencyBudgetMs")
		if not float(latency_ms * 48.0).is_integer():
			raise E2EError(f"{label}.latencyBudgetMs: must map to whole 48 kHz samples")
		if recipe["minimumCpuClass"] not in {"Low", "Standard", "High"}:
			raise E2EError(f"{label}.minimumCpuClass: unsupported CPU class")
		advanced_only = recipe.get("advancedOnly", False)
		if not isinstance(advanced_only, bool):
			raise E2EError(f"{label}.advancedOnly: expected boolean")
		# The fixed contracts describe the five public product profiles. Expert
		# recipes intentionally retain their own engine/latency contract and are
		# never exposed as a product binding, even when their migration profile is
		# Quality. A non-Advanced recipe can never use that escape hatch.
		if not advanced_only and profile in FIXED_RECIPE_CONTRACTS:
			allowed_engines, required_latency_ms, required_cpu = FIXED_RECIPE_CONTRACTS[profile]
			if engine not in allowed_engines:
				raise E2EError(f"{label}.engine: violates the fixed {profile} recipe contract")
			if latency_ms != required_latency_ms:
				raise E2EError(f"{label}.latencyBudgetMs: violates the fixed {profile} recipe contract")
			if recipe["minimumCpuClass"] != required_cpu:
				raise E2EError(f"{label}.minimumCpuClass: violates the fixed {profile} recipe contract")
		execution_semantics_version = _exact_integer(
			recipe["executionSemanticsVersion"], 1, 2**31 - 1, f"{label}.executionSemanticsVersion"
		)
		for field in ("mixCurveVersion", "adaptationPolicyVersion"):
			_exact_integer(recipe[field], 1, 2**31 - 1, f"{label}.{field}")
		recipe_models[recipe_id] = set(model_ids)
		if not advanced_only:
			expected_latency_samples_by_recipe_id[recipe_id] = _expected_recipe_latency_samples(
				engine, execution_semantics_version, model_ids, models, recipe["latencyBudgetMs"], label
			)
			bindings.append({
				"profile": profile,
				"engine": engine,
				"recipe": {
					"catalog_revision": catalog_revision,
					"id": recipe_id,
					"manifest_sha256": recipe_manifest_info["sha256"],
					"revision": revision,
				},
				"models": sorted([
					{"id": model_id, "sha256": models[model_id]["sha256"], "version": models[model_id]["version"]}
					for model_id in model_ids
				], key=lambda model: model["id"]),
			})

	if seen_profiles != set(PRODUCT_PROFILES):
		raise E2EError("recipe manifest does not cover every product profile")
	for model_id, compatible_recipes in model_compatibility.items():
		for recipe_id in compatible_recipes:
			if recipe_id not in recipe_models or model_id not in recipe_models[recipe_id]:
				raise E2EError(f"model {model_id} declares invalid compatibility with {recipe_id}")
	for profile in CORE_PROFILES:
		if sum(binding["profile"] == profile for binding in bindings) != 1:
			raise E2EError(f"product profile {profile} must have exactly one non-advanced recipe")
	if not any(binding["profile"] == "Auto" for binding in bindings):
		raise E2EError("product profile Auto must have at least one non-advanced recipe")
	bindings.sort(key=lambda binding: (binding["profile"], binding["recipe"]["id"]))
	return {
		"catalog_revision": catalog_revision,
		"model_manifest": model_manifest_info,
		"recipe_manifest": recipe_manifest_info,
		"bindings": bindings,
		"expected_latency_samples_by_recipe_id": expected_latency_samples_by_recipe_id,
	}


def _execution_identity_from_paths(paths: Mapping[str, Any]) -> Mapping[str, str]:
	required_paths = {
		"run_provenance", "runtime_root", "client_binary", "server_binary", "model_manifest", "recipe_manifest",
	}
	policy_paths = {"input_enhancement_policy_manifest", "input_enhancement_policy_signature"}
	if not isinstance(paths, dict) or set(paths) not in (required_paths, required_paths | policy_paths):
		raise E2EError("adapter contract paths are incomplete")
	resolved: dict[str, Path] = {}
	for key, value in paths.items():
		if not isinstance(value, str) or not value or not Path(value).is_absolute():
			raise E2EError(f"adapter contract path {key} must be absolute")
		resolved[key] = _lexical_absolute(Path(value))
	provenance = _load_json(resolved["run_provenance"])
	if not isinstance(provenance, dict):
		raise E2EError("run provenance must be a JSON object")
	policy = provenance.get("input_enhancement_policy")
	if set(paths) == required_paths:
		if policy is not None:
			raise E2EError("run provenance binds a policy but adapter contract paths omit it")
	else:
		if not isinstance(policy, dict) or set(policy) != {"manifest", "signature"}:
			raise E2EError("adapter policy paths are not bound by run provenance")
		for key, record_name in (
			("input_enhancement_policy_manifest", "manifest"),
			("input_enhancement_policy_signature", "signature"),
		):
			record = policy[record_name]
			verified = _verified_file(resolved[key])
			if not isinstance(record, dict) or verified["sha256"] != record.get("sha256") or verified["size_bytes"] != record.get("size_bytes"):
				raise E2EError(f"adapter {record_name} policy bytes differ from run provenance")
	return {
		"run_provenance_sha256": _canonical_sha256(provenance),
		"runtime_payload_sha256": str(_tree_attestation(resolved["runtime_root"])["sha256"]),
		"client_binary_sha256": str(_verified_file(resolved["client_binary"])["sha256"]),
		"server_binary_sha256": str(_verified_file(resolved["server_binary"])["sha256"]),
		"model_manifest_sha256": str(_verified_file(resolved["model_manifest"])["sha256"]),
		"recipe_manifest_sha256": str(_verified_file(resolved["recipe_manifest"])["sha256"]),
	}


def _rendered_case(plan: Mapping[str, Any], case: Mapping[str, Any], render_manifest_path: Path, render_root: Path) -> Mapping[str, Any]:
	manifest = _load_json(render_manifest_path)
	if manifest.get("schema_version") != 2 or manifest.get("renderer") != "mumble-audio-mixture-renderer-v2":
		raise E2EError("render manifest is not schema v2")
	if manifest.get("plan_sha256") != PLAN.canonical_sha256(plan):
		raise E2EError("render manifest does not bind the selected plan")
	if manifest.get("corpus_inventory_sha256") != plan["corpus_inventory_sha256"]:
		raise E2EError("render manifest inventory hash mismatch")
	matches = [item for item in manifest.get("cases", []) if item.get("case_id") == case["case_id"]]
	if len(matches) != 1:
		raise E2EError(f"render manifest must contain exactly one {case['case_id']} entry")
	entry = matches[0]
	input_path = _below(render_root, entry["input"]["path"], "render.input.path")
	reference_path = _below(render_root, entry["clean_reference"]["path"], "render.clean_reference.path")
	return {
		"manifest": _verified_file(render_manifest_path),
		"entry_sha256": _canonical_sha256(entry),
		"input": _verified_file(input_path, entry["input"]["sha256"]),
		"clean_reference": _verified_file(reference_path, entry["clean_reference"]["sha256"]),
	}


def _adapter_command(adapter: Path, contract: Path, result: Path, extra: Sequence[str]) -> list[str]:
	if adapter.suffix.lower() == ".py":
		return [sys.executable, str(adapter), "--contract", str(contract), "--result", str(result), *extra]
	return [str(adapter), "--contract", str(contract), "--result", str(result), *extra]


def _write_json(path: Path, value: Mapping[str, Any]) -> None:
	path.parent.mkdir(parents=True, exist_ok=True)
	temporary = path.with_name(f".{path.name}.{os.getpid()}.tmp")
	try:
		encoded = json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + "\n"
	except (TypeError, ValueError) as error:
		raise E2EError(f"refusing to emit non-finite or non-JSON evidence: {error}") from error
	temporary.write_text(encoded, encoding="utf-8")
	os.replace(temporary, path)


def _build_contract(
	role: str,
	profile: str,
	case: Mapping[str, Any],
	rendered: Mapping[str, Any],
	paths: Mapping[str, str],
	provenance_sha256: str,
	expected_execution_identity: Mapping[str, str],
	authorized_product_bindings: Sequence[Mapping[str, Any]],
	output_root: Path,
) -> Mapping[str, Any]:
	# Edge/tail qualification needs clean inputs for both the unchanged route
	# control and the enhanced pre-Opus probe. Noisy quality roles must never
	# qualify an edge merely because room noise crosses the activity threshold
	# before the utterance begins.
	contract_input = rendered["clean_reference"] if role in {"control", "candidate_edge"} else rendered["input"]
	profile_bindings = [binding for binding in authorized_product_bindings if binding["profile"] == profile]
	if profile != "Auto" and len(profile_bindings) != 1:
		raise E2EError(f"profile {profile} does not resolve to exactly one product recipe")
	if not profile_bindings:
		raise E2EError(f"profile {profile} has no authorized product recipe")
	return {
		"schema_version": 3,
		"contract": "mumble-two-client-adapter-v3",
		"role": role,
		"run_provenance_sha256": provenance_sha256,
		"profile": profile,
		"expected_execution_identity": dict(expected_execution_identity),
		"authorized_product_bindings": profile_bindings,
		"performance_budgets": dict(PERFORMANCE_BUDGETS[profile]),
		# The client accepts this only in an E2E-enabled build with the adapter's
		# non-empty per-run token. It makes the qualified tier explicit instead of
		# relying on thread count or an unrelated Auto-policy microbenchmark.
		"cpu_class": "High",
		"controls": case["controls"],
		"transport": {
			**case["transport"],
			"server_host": "127.0.0.1",
		},
		"startup": case["startup"],
		"input": contract_input,
		"clean_reference": rendered["clean_reference"],
		"paths": paths,
		"output_root": str(output_root.resolve()),
		"requirements": {
			"receiver_cleanup": False,
			"callback_frame_samples": 480,
			"sample_rate_hz": 48000,
			"causal_drain_required": True,
			"runtime_diagnostics_required": True,
			"callback_and_worker_metrics_required": True,
			"sender_pre_opus": {
				"required": True,
				"channels": 1,
				"sample_rate_hz": 48000,
				"timeline_origin": "source-after-transmitted-preroll",
				"include_causal_tail": True,
			},
		},
	}


def _validate_adapter_result(
	result_path: Path,
	contract: Mapping[str, Any],
	contract_path: Path,
	expected_contract_sha256: str,
	role_root: Path,
	expected_latency_samples: int,
) -> tuple[Mapping[str, Any], Path, Path]:
	if _file_sha256(contract_path) != expected_contract_sha256:
		raise E2EError("adapter contract changed while the machine adapter was running")
	result = _load_json(result_path)
	required = {
		"capture", "diagnostics", "execution_identity", "input_sha256", "profile", "receiver_cleanup",
		"sender_pre_opus",
		"role", "schema_version", "status", "transport",
	}
	if not isinstance(result, dict) or set(result) != required:
		raise E2EError(f"adapter result has invalid keys: {result_path}")
	if result["schema_version"] != 3 or result["status"] != "passed":
		raise E2EError(f"adapter did not pass {contract['role']}: {result.get('status')}")
	if result["role"] != contract["role"] or result["profile"] != contract["profile"]:
		raise E2EError("adapter role/profile mismatch")
	if result["receiver_cleanup"] is not False or result["input_sha256"] != contract["input"]["sha256"]:
		raise E2EError("adapter did not attest the requested input/receiver-cleanup state")
	expected_identity = contract["expected_execution_identity"]
	if not isinstance(expected_identity, dict) or set(expected_identity) != EXECUTION_IDENTITY_FIELDS:
		raise E2EError("adapter contract has an invalid expected execution identity")
	for key, value in expected_identity.items():
		_sha256_text(value, f"adapter contract expected_execution_identity.{key}")
	if expected_identity["run_provenance_sha256"] != contract["run_provenance_sha256"]:
		raise E2EError("adapter contract provenance identity is internally inconsistent")
	execution_identity = result["execution_identity"]
	identity_fields = EXECUTION_IDENTITY_FIELDS | {"contract_file_sha256"}
	if not isinstance(execution_identity, dict) or set(execution_identity) != identity_fields:
		raise E2EError("adapter execution identity is incomplete")
	for key, value in execution_identity.items():
		_sha256_text(value, f"adapter execution_identity.{key}")
	if execution_identity["contract_file_sha256"] != expected_contract_sha256:
		raise E2EError("adapter did not attest the exact contract bytes")
	for key in EXECUTION_IDENTITY_FIELDS:
		if execution_identity[key] != expected_identity[key]:
			raise E2EError(f"adapter execution identity mismatch: {key}")
	observed_identity = _execution_identity_from_paths(contract["paths"])
	if observed_identity != expected_identity:
		raise E2EError("runtime or provenance changed after the contract was emitted")
	for key in ("opus_bitrate_bps", "frames_per_packet", "transmit_mode"):
		if not isinstance(result["transport"], dict) or set(result["transport"]) != {
			"opus_bitrate_bps", "frames_per_packet", "transmit_mode",
		} or result["transport"][key] != contract["transport"][key]:
			raise E2EError(f"adapter transport mismatch: {key}")
	diagnostics = result["diagnostics"]
	required_diagnostics = {
		"active_engine", "active_models", "active_profile", "active_recipe", "callback_frame_count",
		"callback_p99_ms", "deadline_miss_count", "declared_latency_samples", "fallback_count",
		"invalid_output_count", "mean_rtf", "model_initialization_attempts", "tail_drained",
		"worker_frame_count", "worker_p99_ms",
	}
	if not isinstance(diagnostics, dict) or set(diagnostics) != required_diagnostics:
		raise E2EError("adapter diagnostics are incomplete")
	if diagnostics["active_profile"] != contract["profile"]:
		raise E2EError("adapter active profile mismatch")
	model_initialization_attempts = _exact_integer(
		diagnostics["model_initialization_attempts"], 0, 2**31 - 1,
		"adapter diagnostics.model_initialization_attempts",
	)
	expected_model_initialization_attempts = FIXED_PROFILE_MODEL_INITIALIZATION_ATTEMPTS.get(
		contract["profile"],
		1 if diagnostics["active_engine"] in {"RNNoise", "DeepFilterNet"} else 0,
	)
	if model_initialization_attempts != expected_model_initialization_attempts:
		raise E2EError(
			"adapter model-initialization attestation mismatch: "
			f"profile={contract['profile']} expected={expected_model_initialization_attempts} "
			f"observed={model_initialization_attempts}"
		)
	for key in ("deadline_miss_count", "fallback_count", "invalid_output_count"):
		if _exact_integer(diagnostics[key], 0, 2**63 - 1, f"adapter diagnostics.{key}") != 0:
			raise E2EError(f"adapter reported {key}={diagnostics[key]}")
	if diagnostics["tail_drained"] is not True:
		raise E2EError("adapter did not drain the causal tail")
	declared_latency_samples = _exact_integer(
		diagnostics["declared_latency_samples"], 0, 2**31 - 1,
		"adapter diagnostics.declared_latency_samples",
	)
	if declared_latency_samples != expected_latency_samples:
		raise E2EError(
			"adapter declared latency does not match the manifest-derived execution contract: "
			f"expected={expected_latency_samples} observed={declared_latency_samples}"
		)
	active_recipe = diagnostics["active_recipe"]
	if not isinstance(active_recipe, dict) or set(active_recipe) != {
		"catalog_revision", "id", "manifest_sha256", "revision",
	}:
		raise E2EError("adapter active recipe attestation is invalid")
	_non_empty_text(active_recipe["catalog_revision"], "adapter active recipe catalog_revision")
	_non_empty_text(active_recipe["id"], "adapter active recipe id")
	_sha256_text(active_recipe["manifest_sha256"], "adapter active recipe manifest_sha256")
	_exact_integer(active_recipe["revision"], 1, 2**31 - 1, "adapter active recipe revision")
	active_models = diagnostics["active_models"]
	if not isinstance(active_models, list):
		raise E2EError("adapter active models must be an array")
	validated_models = []
	for index, model in enumerate(active_models):
		if not isinstance(model, dict) or set(model) != {"id", "sha256", "version"}:
			raise E2EError(f"adapter active_models[{index}] is invalid")
		validated_models.append({
			"id": _non_empty_text(model["id"], f"adapter active_models[{index}].id"),
			"sha256": _sha256_text(model["sha256"], f"adapter active_models[{index}].sha256"),
			"version": _non_empty_text(model["version"], f"adapter active_models[{index}].version"),
		})
	if validated_models != sorted(validated_models, key=lambda model: model["id"]):
		raise E2EError("adapter active models must be sorted by model ID")
	if len({model["id"] for model in validated_models}) != len(validated_models):
		raise E2EError("adapter active model IDs must be unique")
	observed_binding = {
		"profile": diagnostics["active_profile"],
		"engine": diagnostics["active_engine"],
		"recipe": dict(active_recipe),
		"models": validated_models,
	}
	if observed_binding not in contract["authorized_product_bindings"]:
		raise E2EError("adapter active recipe/model binding is not authorized by the product manifests")
	callback_frames = _exact_integer(
		diagnostics["callback_frame_count"], 1, 2**63 - 1, "adapter diagnostics.callback_frame_count"
	)
	worker_frames = _exact_integer(
		diagnostics["worker_frame_count"], 0, 2**63 - 1, "adapter diagnostics.worker_frame_count"
	)
	callback_p99 = _finite_number(
		diagnostics["callback_p99_ms"], 0.0, 60_000.0, "adapter diagnostics.callback_p99_ms"
	)
	worker_p99 = _finite_number(
		diagnostics["worker_p99_ms"], 0.0, 60_000.0, "adapter diagnostics.worker_p99_ms"
	)
	mean_rtf = _finite_number(diagnostics["mean_rtf"], 0.0, 1000.0, "adapter diagnostics.mean_rtf")
	if callback_frames <= 0:
		raise E2EError("adapter did not observe callback processing")
	if diagnostics["active_engine"] == "DeepFilterNet" and worker_frames == 0:
		raise E2EError("DeepFilterNet adapter result has no worker observations")
	if worker_frames == 0 and worker_p99 != 0.0:
		raise E2EError("adapter reported worker latency without worker frames")
	budgets = contract["performance_budgets"]
	if not isinstance(budgets, dict) or set(budgets) != {"callback_p99_ms", "worker_p99_ms", "mean_rtf"}:
		raise E2EError("adapter contract performance budgets are invalid")
	if callback_p99 > budgets["callback_p99_ms"]:
		raise E2EError("adapter callback p99 exceeds the contract budget")
	if worker_p99 > budgets["worker_p99_ms"]:
		raise E2EError("adapter worker p99 exceeds the contract budget")
	if mean_rtf > budgets["mean_rtf"]:
		raise E2EError("adapter mean RTF exceeds the contract budget")
	capture = result["capture"]
	if not isinstance(capture, dict) or set(capture) != {"relative_path", "sha256", "size_bytes"}:
		raise E2EError("adapter capture attestation is invalid")
	capture_path = _below(role_root, capture["relative_path"], "adapter.capture.relative_path")
	capture_size = _exact_integer(capture["size_bytes"], 1, 2**63 - 1, "adapter capture size_bytes")
	_verified_file(capture_path, _sha256_text(capture["sha256"], "adapter capture sha256"), capture_size)
	sender_pre_opus = result["sender_pre_opus"]
	if not isinstance(sender_pre_opus, dict) or set(sender_pre_opus) != {
		"relative_path", "sha256", "size_bytes",
	}:
		raise E2EError("adapter sender_pre_opus attestation is invalid")
	sender_pre_opus_path = _below(
		role_root, sender_pre_opus["relative_path"], "adapter.sender_pre_opus.relative_path"
	)
	sender_pre_opus_size = _exact_integer(
		sender_pre_opus["size_bytes"], 1, 2**63 - 1, "adapter sender_pre_opus size_bytes"
	)
	_verified_file(
		sender_pre_opus_path,
		_sha256_text(sender_pre_opus["sha256"], "adapter sender_pre_opus sha256"),
		sender_pre_opus_size,
	)
	return result, capture_path, sender_pre_opus_path


def _score(
	reference: Path,
	received: Path,
	latency: int,
	output: Path,
	baseline: Path | None = None,
	require_complete_tail: bool = True,
	max_onset_loss_samples: int = FRAME_SAMPLES,
	max_end_loss_samples: int = FRAME_SAMPLES,
	scorer_attestation: Mapping[str, Any] | None = None,
) -> Mapping[str, Any]:
	scorer_path = Path(__file__).with_name("score-fixed-timeline.py")
	if scorer_attestation is not None:
		_assert_file_attestation(scorer_path, scorer_attestation, "fixed-timeline scorer")
	command = [
		sys.executable, str(scorer_path),
		"--reference", str(reference), "--received", str(received),
		"--latency-samples", str(latency),
		"--max-onset-loss-samples", str(max_onset_loss_samples),
		"--max-end-loss-samples", str(max_end_loss_samples),
		"--fail-on-new-clipping", "--output", str(output),
	]
	if require_complete_tail:
		command.append("--require-complete-tail")
	if baseline is not None:
		command.extend([
			"--transport-baseline", str(baseline), "--transport-baseline-latency-samples", "0",
			"--qualified-transport-baseline",
		])
	completed = subprocess.run(command, check=False, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
	if scorer_attestation is not None:
		_assert_file_attestation(scorer_path, scorer_attestation, "fixed-timeline scorer")
	if completed.returncode != 0:
		raise E2EError(f"fixed-timeline scoring failed ({completed.returncode}): {completed.stdout.strip()}")
	return _load_json(output)


def _paired_cases(plan: Mapping[str, Any], first_id: str, second_id: str) -> tuple[Mapping[str, Any], Mapping[str, Any]]:
	selected = [case for case in plan["cases"] if case["case_id"] in {first_id, second_id}]
	if len(selected) != 2 or len({str(case["case_id"]) for case in selected}) != 2:
		raise E2EError("paired E2E requires two distinct case IDs present exactly once in the plan")
	if {str(case["profile"]) for case in selected} != {"Quality", "VoiceFocus"}:
		raise E2EError("paired E2E is restricted to one Quality and one VoiceFocus case")
	comparison_ids = {case.get("comparison_scene_id") for case in selected}
	if len(comparison_ids) != 1 or None in comparison_ids or "" in comparison_ids:
		raise E2EError("paired E2E cases must share one explicit comparison_scene_id")
	normalized = []
	for case in selected:
		copy_case = json.loads(json.dumps(case))
		for key in ("case_id", "profile", "controls", "comparison_scene_id"):
			copy_case.pop(key)
		normalized.append(copy_case)
	if normalized[0] != normalized[1]:
		raise E2EError("paired E2E cases do not share byte-identical scene, startup, and transport contracts")
	selected.sort(key=lambda case: (0 if case["profile"] == "Quality" else 1, str(case["case_id"])))
	return selected[0], selected[1]


def _input_enhancement_policy(args: argparse.Namespace, *, required: bool) -> Mapping[str, Any] | None:
	manifest_path = getattr(args, "input_enhancement_policy_manifest", None)
	signature_path = getattr(args, "input_enhancement_policy_signature", None)
	if (manifest_path is None) != (signature_path is None):
		raise E2EError("input-enhancement policy manifest and signature must be supplied together")
	if manifest_path is None:
		if required:
			raise E2EError("enhanced schema-v3 E2E runs require an explicit signed input-enhancement policy")
		return None
	manifest = _verified_file(manifest_path)
	signature = _verified_file(signature_path)
	if signature["size_bytes"] != 64:
		raise E2EError("input-enhancement policy signature must be a raw 64-byte Ed25519 signature")
	document = _load_json(manifest_path)
	if not isinstance(document, dict):
		raise E2EError("input-enhancement policy manifest must be a JSON object")
	return {"manifest": manifest, "signature": signature}


def _policy_paths_and_adapter_args(
	policy: Mapping[str, Any] | None,
) -> tuple[Mapping[str, str], list[str]]:
	if policy is None:
		return {}, []
	manifest = str(policy["manifest"]["path"])
	signature = str(policy["signature"]["path"])
	return (
		{
			"input_enhancement_policy_manifest": manifest,
			"input_enhancement_policy_signature": signature,
		},
		[
			"--input-enhancement-policy-manifest", manifest,
			"--input-enhancement-policy-signature", signature,
		],
	)


def _run_paired(args: argparse.Namespace) -> Mapping[str, Any]:
	"""Run Quality and VoiceFocus against one shared Original route anchor.

	Each emitted case manifest still exposes the exact four logical roles expected
	by schema-v3 measurement evidence.  The clean control and noisy Original role
	are executed once under the same provenance as both candidate/edge pairs, so
	the severe VoiceFocus-vs-Quality comparison has a genuinely shared route
	anchor rather than two independently jittered post-hoc baselines.
	"""

	plan = PLAN.validate_plan(_load_json(args.plan))
	quality_case, voice_case = _paired_cases(plan, str(args.case_id), str(args.paired_case_id))
	cases = (quality_case, voice_case)
	if any(case["transport"]["receiver_cleanup"] is not False for case in cases):
		raise E2EError("receiver cleanup must remain disabled")
	manifest = LOCK.load_validated_manifest(args.corpus_lock)
	inventory = _load_json(args.inventory)
	INVENTORY.validate_inventory(inventory, manifest, require_release=True)
	if INVENTORY.canonical_sha256(inventory) != plan["corpus_inventory_sha256"]:
		raise E2EError("plan does not bind the supplied inventory")
	if LOCK.canonical_manifest_sha256(manifest) != plan["corpus_lock_sha256"]:
		raise E2EError("plan does not bind the supplied corpus lock")
	_assert_packaged_runtime_root(args.runtime_root)
	if args.output_root.exists() and any(args.output_root.iterdir()):
		raise E2EError(f"output root must be empty: {args.output_root}")
	args.output_root.mkdir(parents=True, exist_ok=True)

	runtime = _tree_attestation(args.runtime_root)
	client = _verified_file(args.client_binary)
	server = _verified_file(args.server_binary)
	try:
		args.client_binary.resolve().relative_to(args.runtime_root.resolve())
	except ValueError as error:
		raise E2EError("client binary must be inside the attested runtime root") from error
	for label, manifest_path in (("model", args.model_manifest), ("recipe", args.recipe_manifest)):
		try:
			manifest_path.resolve().relative_to(args.runtime_root.resolve())
		except ValueError as error:
			raise E2EError(f"{label} manifest must be inside the attested runtime root") from error
	adapter = _verified_file(args.adapter)
	rendered_by_id = {
		str(case["case_id"]): _rendered_case(plan, case, args.render_manifest, args.render_root)
		for case in cases
	}
	if len({rendered_by_id[str(case["case_id"])]["input"]["sha256"] for case in cases}) != 1:
		raise E2EError("paired E2E inputs are not byte-identical")
	if len({rendered_by_id[str(case["case_id"])]["clean_reference"]["sha256"] for case in cases}) != 1:
		raise E2EError("paired E2E clean references are not byte-identical")
	metrics = _verify_metric_models(args.metrics_manifest)
	product_catalog = _verify_product_catalog(args.model_manifest, args.recipe_manifest, args.runtime_root)
	policy = _input_enhancement_policy(args, required=True)
	policy_paths, policy_adapter_args = _policy_paths_and_adapter_args(policy)
	if any(
		item in ("--input-enhancement-policy-manifest", "--input-enhancement-policy-signature")
		for item in args.adapter_arg
	):
		raise E2EError("policy adapter arguments are owned by the tracked orchestrator and may not be overridden")
	provenance = {
		"schema_version": 2,
		"comparison_scene_id": quality_case["comparison_scene_id"],
		"paired_cases": [
			{
				"case_id": case["case_id"], "profile": case["profile"],
				"rendered": rendered_by_id[str(case["case_id"])],
			}
			for case in cases
		],
		"plan": _verified_file(args.plan),
		"corpus_lock": _verified_file(args.corpus_lock),
		"corpus_inventory": _verified_file(args.inventory),
		"qualification_case_set": (
			_verified_file(args.qualification_case_set)
			if getattr(args, "qualification_case_set", None) is not None else None
		),
		"runtime_payload": runtime,
		"client_binary": client,
		"server_binary": server,
		"model_manifest": product_catalog["model_manifest"],
		"recipe_manifest": product_catalog["recipe_manifest"],
		"product_catalog": {
			"catalog_revision": product_catalog["catalog_revision"],
			"bindings_sha256": _canonical_sha256(product_catalog["bindings"]),
		},
		"metrics": metrics,
		"input_enhancement_policy": policy,
		"orchestrator": _verified_file(Path(__file__)),
		"fixed_timeline_scorer": _verified_file(Path(__file__).with_name("score-fixed-timeline.py")),
		"machine_adapter": adapter,
	}
	provenance_path = args.output_root / "run-provenance.json"
	_write_json(provenance_path, provenance)
	provenance_sha256 = _canonical_sha256(provenance)
	paths = {
		"run_provenance": str(_lexical_absolute(provenance_path)),
		"runtime_root": str(_lexical_absolute(args.runtime_root)),
		"client_binary": str(_lexical_absolute(args.client_binary)),
		"server_binary": str(_lexical_absolute(args.server_binary)),
		"model_manifest": str(_lexical_absolute(args.model_manifest)),
		"recipe_manifest": str(_lexical_absolute(args.recipe_manifest)),
		**policy_paths,
	}
	expected_execution_identity = _execution_identity_from_paths(paths)
	if expected_execution_identity != {
		"run_provenance_sha256": provenance_sha256,
		"runtime_payload_sha256": runtime["sha256"],
		"client_binary_sha256": client["sha256"],
		"server_binary_sha256": server["sha256"],
		"model_manifest_sha256": product_catalog["model_manifest"]["sha256"],
		"recipe_manifest_sha256": product_catalog["recipe_manifest"]["sha256"],
	}:
		raise E2EError("runtime identity changed while paired provenance was being sealed")

	primary_rendered = rendered_by_id[str(quality_case["case_id"])]
	specifications: list[tuple[str, str, Mapping[str, Any], Mapping[str, Any], Path]] = [
		("shared", "control", quality_case, primary_rendered, args.output_root / "shared" / "control"),
		("shared", "original_comparison", quality_case, primary_rendered, args.output_root / "shared" / "original_comparison"),
	]
	for case in cases:
		case_id = str(case["case_id"])
		rendered = rendered_by_id[case_id]
		specifications.extend([
			(case_id, "candidate", case, rendered, args.output_root / "cases" / case_id / "candidate"),
			(case_id, "candidate_edge", case, rendered, args.output_root / "cases" / case_id / "candidate_edge"),
		])
	contracts: list[tuple[str, str, Mapping[str, Any], Mapping[str, Any], Path, Path, str, Path]] = []
	for owner, role, case, rendered, role_root in specifications:
		profile = "Original" if role in ("control", "original_comparison") else str(case["profile"])
		contract_path = role_root / "adapter-contract.json"
		result_path = role_root / "adapter-result.json"
		contract = _build_contract(
			role, profile, case, rendered, paths, provenance_sha256, expected_execution_identity,
			product_catalog["bindings"], role_root,
		)
		_write_json(contract_path, contract)
		contracts.append((owner, role, case, rendered, role_root, contract_path, _file_sha256(contract_path), result_path))
	if args.emit_contracts_only:
		result = {
			"schema_version": 1, "kind": "mumble-two-client-paired-e2e-v1", "status": "contracts_emitted",
			"comparison_scene_id": quality_case["comparison_scene_id"], "run_provenance_sha256": provenance_sha256,
			"contracts": [
				{"owner": owner, "role": role, "relative_path": contract_path.relative_to(args.output_root).as_posix(), "sha256": digest}
				for owner, role, _, _, _, contract_path, digest, _ in contracts
			],
			"private_audio_do_not_upload": True,
		}
		_write_json(args.output_root / "paired-e2e-manifest.json", result)
		return result

	results: dict[tuple[str, str], Mapping[str, Any]] = {}
	captures: dict[tuple[str, str], Path] = {}
	pre_opus: dict[tuple[str, str], Path] = {}
	role_roots: dict[tuple[str, str], Path] = {}
	_assert_file_attestation(Path(__file__), provenance["orchestrator"], "two-client orchestrator")
	_assert_file_attestation(args.adapter, provenance["machine_adapter"], "machine adapter")
	for owner, role, _, _, role_root, contract_path, contract_sha256, result_path in contracts:
		contract = _load_json(contract_path)
		_assert_file_attestation(args.adapter, provenance["machine_adapter"], "machine adapter")
		completed = subprocess.run(
			_adapter_command(
				args.adapter.resolve(), contract_path.resolve(), result_path.resolve(),
				[*args.adapter_arg, *policy_adapter_args],
			),
			check=False, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
		)
		_assert_file_attestation(args.adapter, provenance["machine_adapter"], "machine adapter")
		if completed.returncode != 0:
			raise E2EError(f"machine adapter failed for {owner}/{role} ({completed.returncode}): {completed.stdout.strip()}")
		binding = contract["authorized_product_bindings"][0]
		expected_latency = product_catalog["expected_latency_samples_by_recipe_id"][str(binding["recipe"]["id"])]
		validated, capture, sender_pre_opus = _validate_adapter_result(
			result_path, contract, contract_path, contract_sha256, role_root, expected_latency,
		)
		key = (owner, role)
		results[key] = validated; captures[key] = capture; pre_opus[key] = sender_pre_opus; role_roots[key] = role_root

	clean_path = Path(primary_rendered["clean_reference"]["path"])
	control_key = ("shared", "control")
	original_key = ("shared", "original_comparison")
	control_pre_score_path = role_roots[control_key] / "pre-opus-fixed-timeline-score.json"
	_score(
		clean_path, pre_opus[control_key], int(results[control_key]["diagnostics"]["declared_latency_samples"]),
		control_pre_score_path, require_complete_tail=True,
		max_onset_loss_samples=FRAME_SAMPLES, max_end_loss_samples=FRAME_SAMPLES,
		scorer_attestation=provenance["fixed_timeline_scorer"],
	)
	transport = quality_case["transport"]
	require_complete_capture_tail = transport["transmit_mode"] != "VAD"
	route_budget = (ORIGINAL_ROUTE_FIXED_STARTUP_FRAMES + int(transport["frames_per_packet"])) * FRAME_SAMPLES
	control_score_path = role_roots[control_key] / "fixed-timeline-score.json"
	_score(
		clean_path, captures[control_key], 0, control_score_path,
		require_complete_tail=require_complete_capture_tail,
		max_onset_loss_samples=route_budget, max_end_loss_samples=route_budget,
		scorer_attestation=provenance["fixed_timeline_scorer"],
	)

	case_manifest_records = []
	for case in cases:
		case_id = str(case["case_id"])
		candidate_key = (case_id, "candidate")
		edge_key = (case_id, "candidate_edge")
		edge_score_path = role_roots[edge_key] / "pre-opus-fixed-timeline-score.json"
		_score(
			clean_path, pre_opus[edge_key], int(results[edge_key]["diagnostics"]["declared_latency_samples"]),
			edge_score_path, require_complete_tail=True,
			max_onset_loss_samples=FRAME_SAMPLES, max_end_loss_samples=FRAME_SAMPLES,
			scorer_attestation=provenance["fixed_timeline_scorer"],
		)
		candidate_score_path = role_roots[candidate_key] / "fixed-timeline-score.json"
		_score(
			clean_path, captures[candidate_key], int(results[candidate_key]["diagnostics"]["declared_latency_samples"]),
			candidate_score_path, captures[control_key],
			require_complete_tail=require_complete_capture_tail,
			max_onset_loss_samples=route_budget, max_end_loss_samples=route_budget,
			scorer_attestation=provenance["fixed_timeline_scorer"],
		)
		logical = {
			"control": (control_key, control_score_path, control_pre_score_path, "clean-original-route-control"),
			"original_comparison": (original_key, None, None, "noisy-original-quality-comparison"),
			"candidate": (candidate_key, candidate_score_path, None, "noisy-enhanced-candidate"),
			"candidate_edge": (edge_key, None, edge_score_path, "clean-enhanced-input-edge-probe"),
		}
		manifest_results = {}
		for role, (key, fixed_path, pre_path, purpose) in logical.items():
			result = results[key]
			diagnostics = result["diagnostics"]
			manifest_results[role] = {
				"adapter_contract_sha256": result["execution_identity"]["contract_file_sha256"],
				"adapter_result_sha256": _file_sha256(role_roots[key] / "adapter-result.json"),
				"capture_sha256": result["capture"]["sha256"],
				"sender_pre_opus_sha256": result["sender_pre_opus"]["sha256"],
				"execution_identity": result["execution_identity"],
				"active_recipe": diagnostics["active_recipe"], "active_models": diagnostics["active_models"],
				"performance": {
					"callback_frame_count": diagnostics["callback_frame_count"],
					"callback_p99_ms": diagnostics["callback_p99_ms"],
					"model_initialization_attempts": diagnostics["model_initialization_attempts"],
					"worker_frame_count": diagnostics["worker_frame_count"], "worker_p99_ms": diagnostics["worker_p99_ms"],
					"mean_rtf": diagnostics["mean_rtf"],
				},
				"fixed_timeline_score_sha256": _file_sha256(fixed_path) if fixed_path is not None else None,
				"pre_opus_fixed_timeline_score_sha256": _file_sha256(pre_path) if pre_path is not None else None,
				"qualification_purpose": purpose,
			}
		rendered = rendered_by_id[case_id]
		case_manifest = {
			"schema_version": 3, "status": "passed", "case_id": case_id, "profile": case["profile"],
			"run_provenance_sha256": provenance_sha256, "receiver_cleanup": False,
			"qualification_binding": {
				"mixture_plan_sha256": provenance["plan"]["sha256"],
				"case_set_sha256": provenance["qualification_case_set"]["sha256"] if provenance["qualification_case_set"] is not None else None,
				"corpus_inventory_sha256": provenance["corpus_inventory"]["sha256"],
				"corpus_lock_sha256": plan["corpus_lock_sha256"], "case_id": case_id,
				"profile": case["profile"], "dataset_split": plan["split"],
				"plan_case_sha256": _canonical_sha256(case), "render_manifest_sha256": rendered["manifest"]["sha256"],
				"render_entry_sha256": rendered["entry_sha256"], "source_input_sha256": rendered["input"]["sha256"],
				"clean_reference_sha256": rendered["clean_reference"]["sha256"],
				"input_enhancement_policy_manifest_sha256": policy["manifest"]["sha256"],
				"input_enhancement_policy_signature_sha256": policy["signature"]["sha256"],
			},
			"input_timeline_gate": {
				"artifact": "sender_pre_opus", "alignment": "fixed-declared-latency",
				"roles": ["control", "candidate_edge"], "max_onset_loss_samples": FRAME_SAMPLES,
				"max_end_loss_samples": FRAME_SAMPLES, "complete_tail_required": True,
			},
			"route_control": {
				"onset_budget_samples": route_budget, "end_loss_budget_samples": route_budget,
				"receiver_edge_gate": "route-bounded-not-input-latency",
				"capture_tail_rule": "complete" if require_complete_capture_tail else "vad-speech-edge",
				"causal_tail_drain_required": True, "legacy_original_parity_required": True,
			},
			"results": manifest_results, "private_audio_do_not_upload": True,
		}
		case_manifest_path = args.output_root / "cases" / case_id / "e2e-manifest.json"
		_write_json(case_manifest_path, case_manifest)
		case_manifest_records.append({
			"case_id": case_id, "profile": case["profile"],
			"relative_path": case_manifest_path.relative_to(args.output_root).as_posix(),
			"sha256": _file_sha256(case_manifest_path),
		})

	_assert_file_attestation(Path(__file__), provenance["orchestrator"], "two-client orchestrator")
	_assert_file_attestation(args.adapter, provenance["machine_adapter"], "machine adapter")
	result = {
		"schema_version": 1, "kind": "mumble-two-client-paired-e2e-v1", "status": "passed",
		"comparison_scene_id": quality_case["comparison_scene_id"], "run_provenance_sha256": provenance_sha256,
		"cases": case_manifest_records, "shared_roles": ["control", "original_comparison"],
		"private_audio_do_not_upload": True,
	}
	_write_json(args.output_root / "paired-e2e-manifest.json", result)
	return result


def run(args: argparse.Namespace) -> Mapping[str, Any]:
	if getattr(args, "paired_case_id", None) is not None:
		return _run_paired(args)
	plan = PLAN.validate_plan(_load_json(args.plan))
	cases = [case for case in plan["cases"] if case["case_id"] == args.case_id]
	if len(cases) != 1:
		raise E2EError(f"plan must contain exactly one case named {args.case_id}")
	case = cases[0]
	if case["transport"]["receiver_cleanup"] is not False:
		raise E2EError("receiver cleanup must remain disabled")
	manifest = LOCK.load_validated_manifest(args.corpus_lock)
	inventory = _load_json(args.inventory)
	INVENTORY.validate_inventory(inventory, manifest, require_release=True)
	if INVENTORY.canonical_sha256(inventory) != plan["corpus_inventory_sha256"]:
		raise E2EError("plan does not bind the supplied inventory")
	if LOCK.canonical_manifest_sha256(manifest) != plan["corpus_lock_sha256"]:
		raise E2EError("plan does not bind the supplied corpus lock")
	_assert_packaged_runtime_root(args.runtime_root)
	if args.output_root.exists() and any(args.output_root.iterdir()):
		raise E2EError(f"output root must be empty: {args.output_root}")
	args.output_root.mkdir(parents=True, exist_ok=True)

	runtime = _tree_attestation(args.runtime_root)
	client = _verified_file(args.client_binary)
	server = _verified_file(args.server_binary)
	try:
		args.client_binary.resolve().relative_to(args.runtime_root.resolve())
	except ValueError as error:
		raise E2EError("client binary must be inside the attested runtime root") from error
	for label, manifest_path in (("model", args.model_manifest), ("recipe", args.recipe_manifest)):
		try:
			manifest_path.resolve().relative_to(args.runtime_root.resolve())
		except ValueError as error:
			raise E2EError(f"{label} manifest must be inside the attested runtime root") from error
	adapter = _verified_file(args.adapter)
	rendered = _rendered_case(plan, case, args.render_manifest, args.render_root)
	metrics = _verify_metric_models(args.metrics_manifest)
	product_catalog = _verify_product_catalog(args.model_manifest, args.recipe_manifest, args.runtime_root)
	policy = _input_enhancement_policy(args, required=case["profile"] != "Original")
	policy_paths, policy_adapter_args = _policy_paths_and_adapter_args(policy)
	if any(
		item in ("--input-enhancement-policy-manifest", "--input-enhancement-policy-signature")
		for item in args.adapter_arg
	):
		raise E2EError("policy adapter arguments are owned by the tracked orchestrator and may not be overridden")
	provenance = {
		"schema_version": 1,
		"case_id": case["case_id"],
		"plan": _verified_file(args.plan),
		"corpus_lock": _verified_file(args.corpus_lock),
		"corpus_inventory": _verified_file(args.inventory),
		"qualification_case_set": (
			_verified_file(args.qualification_case_set)
			if getattr(args, "qualification_case_set", None) is not None else None
		),
		"render_manifest": rendered["manifest"],
		"runtime_payload": runtime,
		"client_binary": client,
		"server_binary": server,
		"model_manifest": product_catalog["model_manifest"],
		"recipe_manifest": product_catalog["recipe_manifest"],
		"product_catalog": {
			"catalog_revision": product_catalog["catalog_revision"],
			"bindings_sha256": _canonical_sha256(product_catalog["bindings"]),
		},
		"metrics": metrics,
		"input_enhancement_policy": policy,
		"orchestrator": _verified_file(Path(__file__)),
		"fixed_timeline_scorer": _verified_file(Path(__file__).with_name("score-fixed-timeline.py")),
		"machine_adapter": adapter,
	}
	_write_json(args.output_root / "run-provenance.json", provenance)
	provenance_sha256 = _canonical_sha256(provenance)
	paths = {
		"run_provenance": str(_lexical_absolute(args.output_root / "run-provenance.json")),
		"runtime_root": str(_lexical_absolute(args.runtime_root)),
		"client_binary": str(_lexical_absolute(args.client_binary)),
		"server_binary": str(_lexical_absolute(args.server_binary)),
		"model_manifest": str(_lexical_absolute(args.model_manifest)),
		"recipe_manifest": str(_lexical_absolute(args.recipe_manifest)),
		**policy_paths,
	}
	expected_execution_identity = _execution_identity_from_paths(paths)
	provenance_identity = {
		"run_provenance_sha256": provenance_sha256,
		"runtime_payload_sha256": runtime["sha256"],
		"client_binary_sha256": client["sha256"],
		"server_binary_sha256": server["sha256"],
		"model_manifest_sha256": product_catalog["model_manifest"]["sha256"],
		"recipe_manifest_sha256": product_catalog["recipe_manifest"]["sha256"],
	}
	if expected_execution_identity != provenance_identity:
		raise E2EError("runtime identity changed while the run provenance was being sealed")
	# The clean control anchors fixed speech edges through the unchanged route.
	# The noisy Original comparison remains available for OVRL/BAK/SIG/eSTOI/WER
	# comparisons and is deliberately not treated as an edge-control.
	# Keep the candidate side explicit even when the requested profile is
	# Original.  Master/nightly case evidence requires receiver-capture
	# objective scores for every core profile, and that scorer deliberately
	# requires an independently captured noisy candidate plus its paired fixed-
	# timeline score.  Reusing ``original_comparison`` would collapse the two
	# sides of the comparison and leave no candidate-role attestation.
	roles = [
		("control", "Original"),
		("original_comparison", "Original"),
		("candidate", case["profile"]),
		("candidate_edge", case["profile"]),
	]
	contracts: list[tuple[str, Mapping[str, Any], Path, str, Path]] = []
	for role, profile in roles:
		role_root = args.output_root / role
		contract_path = role_root / "adapter-contract.json"
		result_path = role_root / "adapter-result.json"
		contract = _build_contract(
			role, profile, case, rendered, paths, provenance_sha256, expected_execution_identity,
			product_catalog["bindings"], role_root,
		)
		_write_json(contract_path, contract)
		contracts.append((role, contract, contract_path, _file_sha256(contract_path), result_path))
	if args.emit_contracts_only:
		manifest_result = {
			"schema_version": 3, "status": "contracts_emitted", "case_id": case["case_id"],
			"profile": case["profile"], "run_provenance_sha256": provenance_sha256,
			"contracts": [
				{
					"role": role,
					"relative_path": str(contract_path.relative_to(args.output_root).as_posix()),
					"sha256": contract_sha256,
				}
				for role, _, contract_path, contract_sha256, _ in contracts
			],
			"private_audio_do_not_upload": True,
		}
		_write_json(args.output_root / "e2e-manifest.json", manifest_result)
		return manifest_result

	results: dict[str, Any] = {}
	captures: dict[str, Path] = {}
	sender_pre_opus_artifacts: dict[str, Path] = {}
	_assert_file_attestation(Path(__file__), provenance["orchestrator"], "two-client orchestrator")
	_assert_file_attestation(args.adapter, provenance["machine_adapter"], "machine adapter")
	for role, contract, contract_path, contract_sha256, result_path in contracts:
		_assert_file_attestation(args.adapter, provenance["machine_adapter"], "machine adapter")
		command = _adapter_command(
			args.adapter.resolve(), contract_path.resolve(), result_path.resolve(),
			[*args.adapter_arg, *policy_adapter_args],
		)
		completed = subprocess.run(command, check=False, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
		_assert_file_attestation(args.adapter, provenance["machine_adapter"], "machine adapter")
		if completed.returncode != 0:
			raise E2EError(f"machine adapter failed for {role} ({completed.returncode}): {completed.stdout.strip()}")
		authorized_binding = contract["authorized_product_bindings"][0]
		recipe_id = str(authorized_binding["recipe"]["id"])
		expected_latency_samples = product_catalog["expected_latency_samples_by_recipe_id"][recipe_id]
		result, capture, sender_pre_opus = _validate_adapter_result(
			result_path, contract, contract_path, contract_sha256, result_path.parent,
			expected_latency_samples,
		)
		results[role] = result
		captures[role] = capture
		sender_pre_opus_artifacts[role] = sender_pre_opus
	pre_opus_scores: dict[str, Any] = {}
	for role in ("control", "candidate_edge"):
		if role not in results:
			continue
		result = results[role]
		pre_opus_score_path = args.output_root / role / "pre-opus-fixed-timeline-score.json"
		pre_opus_scores[role] = _score(
			Path(rendered["clean_reference"]["path"]), sender_pre_opus_artifacts[role],
			int(result["diagnostics"]["declared_latency_samples"]), pre_opus_score_path,
			require_complete_tail=True,
			max_onset_loss_samples=FRAME_SAMPLES,
			max_end_loss_samples=FRAME_SAMPLES,
			scorer_attestation=provenance["fixed_timeline_scorer"],
		)
	control_score_path = args.output_root / "control" / "fixed-timeline-score.json"
	# VAD intentionally stops transmitting trailing room silence after the
	# utterance. The hard one-frame input edge/tail gate is already enforced on
	# sender pre-Opus PCM above. Continuous and PTT must additionally retain the
	# complete receiver capture timeline through the real route.
	require_complete_capture_tail = case["transport"]["transmit_mode"] != "VAD"
	control_onset_budget_samples = (
		ORIGINAL_ROUTE_FIXED_STARTUP_FRAMES + int(case["transport"]["frames_per_packet"])
	) * FRAME_SAMPLES
	# The receiver WAV starts at the first decoded OG packet rather than at the
	# sender's source-timeline zero. That bounded route-origin offset moves both
	# observed speech edges earlier; treating the earlier end as truncation makes
	# a clean Original control fail even when its whole waveform is present. The
	# separately attested complete-tail rule remains strict for Continuous/PTT.
	# Both control and candidate receiver edges use this route bound; it must not
	# be interpreted as, or substituted for, the hard sender input-latency gate.
	control_end_budget_samples = control_onset_budget_samples
	control_score = _score(
		# Both the control input and edge reference are clean. A noisy Original
		# comparison cannot be a trustworthy onset anchor because leading room
		# noise may cross the activity threshold before speech starts.
		Path(rendered["clean_reference"]["path"]), captures["control"],
		int(results["control"]["diagnostics"]["declared_latency_samples"]), control_score_path,
		require_complete_tail=require_complete_capture_tail,
		max_onset_loss_samples=control_onset_budget_samples,
		max_end_loss_samples=control_end_budget_samples,
		scorer_attestation=provenance["fixed_timeline_scorer"],
	)
	scores: dict[str, Any] = {"control": control_score}
	if "candidate" in results:
		candidate_score_path = args.output_root / "candidate" / "fixed-timeline-score.json"
		scores["candidate"] = _score(
			Path(rendered["clean_reference"]["path"]), captures["candidate"],
			int(results["candidate"]["diagnostics"]["declared_latency_samples"]), candidate_score_path,
			captures["control"],
			require_complete_tail=require_complete_capture_tail,
			max_onset_loss_samples=control_onset_budget_samples,
			max_end_loss_samples=control_end_budget_samples,
			scorer_attestation=provenance["fixed_timeline_scorer"],
		)
	_assert_file_attestation(Path(__file__), provenance["orchestrator"], "two-client orchestrator")
	_assert_file_attestation(args.adapter, provenance["machine_adapter"], "machine adapter")
	manifest_result = {
		"schema_version": 3,
		"status": "passed",
		"case_id": case["case_id"],
		"profile": case["profile"],
		"run_provenance_sha256": provenance_sha256,
		"receiver_cleanup": False,
		"qualification_binding": {
			"mixture_plan_sha256": provenance["plan"]["sha256"],
			"case_set_sha256": provenance["qualification_case_set"]["sha256"] if provenance["qualification_case_set"] is not None else None,
			"corpus_inventory_sha256": provenance["corpus_inventory"]["sha256"],
			"corpus_lock_sha256": plan["corpus_lock_sha256"],
			"case_id": case["case_id"],
			"profile": case["profile"],
			"dataset_split": plan["split"],
			"plan_case_sha256": _canonical_sha256(case),
			"render_manifest_sha256": rendered["manifest"]["sha256"],
			"render_entry_sha256": rendered["entry_sha256"],
			"source_input_sha256": rendered["input"]["sha256"],
			"clean_reference_sha256": rendered["clean_reference"]["sha256"],
			"input_enhancement_policy_manifest_sha256": (
				policy["manifest"]["sha256"] if policy is not None else None
			),
			"input_enhancement_policy_signature_sha256": (
				policy["signature"]["sha256"] if policy is not None else None
			),
		},
		"input_timeline_gate": {
			"artifact": "sender_pre_opus",
			"alignment": "fixed-declared-latency",
			"roles": [role for role in ("control", "candidate_edge") if role in results],
			"max_onset_loss_samples": FRAME_SAMPLES,
			"max_end_loss_samples": FRAME_SAMPLES,
			"complete_tail_required": True,
		},
		"route_control": {
			"onset_budget_samples": control_onset_budget_samples,
			"end_loss_budget_samples": control_end_budget_samples,
			"receiver_edge_gate": "route-bounded-not-input-latency",
			"capture_tail_rule": "complete" if require_complete_capture_tail else "vad-speech-edge",
			"causal_tail_drain_required": True,
			"legacy_original_parity_required": True,
		},
		"results": {
			role: {
				"adapter_contract_sha256": result["execution_identity"]["contract_file_sha256"],
				"adapter_result_sha256": _file_sha256(args.output_root / role / "adapter-result.json"),
				"capture_sha256": result["capture"]["sha256"],
				"sender_pre_opus_sha256": result["sender_pre_opus"]["sha256"],
				"execution_identity": result["execution_identity"],
				"active_recipe": result["diagnostics"]["active_recipe"],
				"active_models": result["diagnostics"]["active_models"],
				"performance": {
					"callback_frame_count": result["diagnostics"]["callback_frame_count"],
					"callback_p99_ms": result["diagnostics"]["callback_p99_ms"],
					"model_initialization_attempts": result["diagnostics"]["model_initialization_attempts"],
					"worker_frame_count": result["diagnostics"]["worker_frame_count"],
					"worker_p99_ms": result["diagnostics"]["worker_p99_ms"],
					"mean_rtf": result["diagnostics"]["mean_rtf"],
				},
				"fixed_timeline_score_sha256": (
					_file_sha256(args.output_root / role / "fixed-timeline-score.json") if role in scores else None
				),
				"pre_opus_fixed_timeline_score_sha256": (
					_file_sha256(args.output_root / role / "pre-opus-fixed-timeline-score.json")
					if role in pre_opus_scores else None
				),
				"qualification_purpose": (
					"clean-original-route-control" if role == "control"
					else "noisy-original-quality-comparison" if role == "original_comparison"
					else "clean-enhanced-input-edge-probe" if role == "candidate_edge"
					else "noisy-enhanced-candidate"
				),
			}
			for role, result in results.items()
		},
		"private_audio_do_not_upload": True,
	}
	_write_json(args.output_root / "e2e-manifest.json", manifest_result)
	return manifest_result


def _write_sine(path: Path, samples: int = 48000, frequency_hz: int = 220) -> None:
	path.parent.mkdir(parents=True, exist_ok=True)
	pcm = b"".join(struct.pack("<h", round(6000 * __import__("math").sin(2 * __import__("math").pi * frequency_hz * index / 48000))) for index in range(samples))
	with wave.open(str(path), "wb") as stream:
		stream.setnchannels(1); stream.setsampwidth(2); stream.setframerate(48000); stream.writeframes(pcm)


def run_self_test() -> None:
	with tempfile.TemporaryDirectory(prefix="mumble-two-client-e2e-") as directory:
		root = Path(directory)
		runtime = root / "runtime"; runtime.mkdir()
		client = runtime / "mumble.exe"; client.write_bytes(b"client")
		(runtime / "CMakeCache.txt").write_text("mutable build marker\n", encoding="utf-8")
		try:
			_assert_packaged_runtime_root(runtime)
		except E2EError as error:
			if "staged/packaged payload" not in str(error):
				raise AssertionError("mutable build-root rejection returned the wrong error") from error
		else:
			raise AssertionError("mutable CMake build tree was accepted as a release runtime")
		(runtime / "CMakeCache.txt").unlink()
		_assert_packaged_runtime_root(runtime)
		server = root / "mumble-server.exe"; server.write_bytes(b"server")
		rnnoise_asset = runtime / "rnnoise.dll"; rnnoise_asset.write_bytes(b"rnnoise-self-test")
		deepfilter_asset = runtime / "deepfilternet" / "model.tar.gz"
		deepfilter_asset.parent.mkdir(); deepfilter_asset.write_bytes(b"deepfilter-self-test")
		dtln_asset = runtime / "dtln" / "expert.onnx"
		dtln_asset.parent.mkdir(); dtln_asset.write_bytes(b"dtln-expert-self-test")
		model_manifest = runtime / "input-models.json"
		_write_json(model_manifest, {
			"schemaVersion": 1,
			"catalogRevision": "self-test-catalog-v2",
			"generatedFromAssets": True,
			"models": [
				{
					"id": "rnnoise:embedded", "version": "self-test-rnnoise", "backend": "RNNoise",
					"path": "rnnoise.dll", "sha256": _file_sha256(rnnoise_asset),
					"size": rnnoise_asset.stat().st_size, "licenseSpdx": "BSD-3-Clause",
					"sampleRateHz": 48000, "algorithmicLatencyMs": 30,
					"recipeCompatibility": [
						"input.balanced.rnnoise-embedded", "input.auto.balanced.rnnoise-embedded",
					],
				},
				{
					"id": "deepfilternet:low-latency", "version": "self-test-deepfilter-ll", "backend": "DeepFilterNet",
					"path": "deepfilternet/model.tar.gz", "sha256": _file_sha256(deepfilter_asset),
					"size": deepfilter_asset.stat().st_size, "licenseSpdx": "MIT OR Apache-2.0",
					"sampleRateHz": 48000, "algorithmicLatencyMs": 10,
					"recipeCompatibility": [
						"input.quality.deepfilternet-low-latency", "input.auto.quality.deepfilternet-low-latency",
						"input.voice-focus.deepfilternet-low-latency",
					],
				},
				{
					"id": "dtln:expert", "version": "self-test-dtln", "backend": "DTLN",
					"path": "dtln/expert.onnx", "sha256": _file_sha256(dtln_asset),
					"size": dtln_asset.stat().st_size, "licenseSpdx": "MIT",
					"sampleRateHz": 16000, "algorithmicLatencyMs": 32,
					"recipeCompatibility": ["input.expert.dtln"],
				},
			],
		})

		def recipe(
			recipe_id: str, profile: str, engine: str, model_ids: list[str], latency_ms: int,
			minimum_cpu: str, noise_range: list[int], character_range: list[int],
		) -> Mapping[str, Any]:
			return {
				"id": recipe_id, "revision": 1, "profile": profile, "engine": engine,
				"modelIds": model_ids, "noiseReductionRange": noise_range,
				"naturalCrispRange": character_range, "latencyBudgetMs": latency_ms,
				"minimumCpuClass": minimum_cpu, "executionSemanticsVersion": 5,
				"mixCurveVersion": 4, "adaptationPolicyVersion": 1,
			}

		recipe_manifest = runtime / "input-recipes.json"
		advanced_dtln_recipe = dict(recipe(
			"input.expert.dtln", "Quality", "DTLN", ["dtln:expert"],
			50, "High", [0, 100], [0, 100],
		))
		advanced_dtln_recipe["advancedOnly"] = True
		recipe_manifest_payload = {
			"schemaVersion": 2,
			"catalogRevision": "self-test-catalog-v2",
			"modelManifestSha256": _file_sha256(model_manifest),
			"recipes": [
				recipe("input.original", "Original", "None", [], 0, "Low", [0, 0], [0, 0]),
				recipe("input.light.speex", "Light", "Speex", [], 10, "Low", [0, 100], [0, 100]),
				recipe(
					"input.balanced.rnnoise-embedded", "Balanced", "RNNoise", ["rnnoise:embedded"],
					30, "Standard", [20, 90], [10, 90],
				),
				recipe(
					"input.quality.deepfilternet-low-latency", "Quality", "DeepFilterNet",
					["deepfilternet:low-latency"], 50, "High", [25, 90], [25, 100],
				),
				recipe("input.auto.light.speex", "Auto", "Speex", [], 10, "Low", [0, 100], [0, 100]),
				recipe(
					"input.auto.balanced.rnnoise-embedded", "Auto", "RNNoise", ["rnnoise:embedded"],
					30, "Standard", [20, 90], [10, 90],
				),
				recipe(
					"input.auto.quality.deepfilternet-low-latency", "Auto", "DeepFilterNet",
					["deepfilternet:low-latency"], 50, "High", [25, 90], [25, 100],
				),
				recipe(
					"input.voice-focus.deepfilternet-low-latency", "VoiceFocus", "DeepFilterNet",
					["deepfilternet:low-latency"], 50, "High", [70, 100], [40, 100],
				),
				advanced_dtln_recipe,
			],
		}
		_write_json(recipe_manifest, recipe_manifest_payload)
		verified_catalog = _verify_product_catalog(model_manifest, recipe_manifest, runtime)
		if any(binding["recipe"]["id"] == "input.expert.dtln" for binding in verified_catalog["bindings"]):
			raise AssertionError("Advanced DTLN recipe leaked into public product bindings")
		bad_fixed_contract = json.loads(json.dumps(recipe_manifest_payload))
		bad_fixed_contract["recipes"][3]["minimumCpuClass"] = "Standard"
		_write_json(recipe_manifest, bad_fixed_contract)
		try:
			_verify_product_catalog(model_manifest, recipe_manifest, runtime)
		except E2EError:
			pass
		else:
			raise AssertionError("fixed Quality CPU contract drift was accepted")
		bad_public_dtln = json.loads(json.dumps(recipe_manifest_payload))
		bad_public_dtln["recipes"][-1]["advancedOnly"] = False
		_write_json(recipe_manifest, bad_public_dtln)
		try:
			_verify_product_catalog(model_manifest, recipe_manifest, runtime)
		except E2EError as error:
			if "fixed Quality recipe contract" not in str(error):
				raise AssertionError("public DTLN rejection returned the wrong contract error") from error
		else:
			raise AssertionError("non-Advanced DTLN Quality recipe was accepted")
		_write_json(recipe_manifest, recipe_manifest_payload)
		metrics_runtime = root / "metrics-runtime.lock"; metrics_runtime.write_bytes(b"python-lock")
		metric_models = []
		for metric_id in ("dnsmos", "estoi", "wer-en", "wer-sv"):
			metric_path = root / f"{metric_id}.bin"; metric_path.write_bytes(metric_id.encode("ascii"))
			metric_models.append({
				"id": metric_id, "relative_path": metric_path.name, "sha256": _file_sha256(metric_path),
				"size_bytes": metric_path.stat().st_size,
			})
		metrics_manifest = root / "metrics.json"
		metrics_manifest.write_text(json.dumps({
			"schema_version": 1,
			"runtime": {
				"id": "self-test", "version": "1", "relative_path": metrics_runtime.name,
				"sha256": _file_sha256(metrics_runtime), "size_bytes": metrics_runtime.stat().st_size,
			},
			"models": metric_models,
		}), encoding="utf-8")
		policy_manifest = root / "input-enhancement-policy.json"
		policy_manifest.write_text('{"inputEnhancement":{"available":true,"forceOriginal":false}}\n', encoding="utf-8")
		policy_signature = root / "input-enhancement-policy.sig"
		policy_signature.write_bytes(bytes(range(64)))
		# The self-test adapter emulates the protected machine adapter while still
		# independently hashing every path named by the contract.
		manifest_path = Path(__file__).with_name("corpus-lock.json")
		manifest = LOCK.load_validated_manifest(manifest_path)
		inventory = PLAN._self_test_inventory(manifest, "mumble-plan-self-test")
		inventory_path = root / "inventory.json"; _write_json(inventory_path, inventory)
		# Keep the orchestration fixture bound to a fully valid PR-smoke plan.  The
		# schema-v4 plan contract qualifies both control-grid endpoints for every
		# product profile, which deliberately cannot be represented by the old
		# four-case shortcut used by this self-test.
		plan = PLAN.generate_plan(manifest, inventory, "pr_smoke", "validation", "mumble-plan-self-test", 30, 1000)
		plan_path = root / "plan.json"; _write_json(plan_path, plan)
		selected_case = plan["cases"][3]
		if selected_case["profile"] != "Quality":
			raise AssertionError("self-test must exercise the DeepFilterNet Quality worker contract")
		original_case = next(case for case in plan["cases"] if case["profile"] == "Original")
		audio = root / "audio"
		render_cases = []
		for index, rendered_case in enumerate((selected_case, original_case)):
			case_root = audio / rendered_case["case_id"]
			case_root.mkdir(parents=True)
			_write_sine(case_root / "client1-input.wav", frequency_hz=220 + index * 20)
			_write_sine(case_root / "clean-reference.wav", frequency_hz=330 + index * 20)
			render_cases.append({
				"case_id": rendered_case["case_id"],
				"input": {
					"path": f"{rendered_case['case_id']}/client1-input.wav",
					"sha256": _file_sha256(case_root / "client1-input.wav"),
				},
				"clean_reference": {
					"path": f"{rendered_case['case_id']}/clean-reference.wav",
					"sha256": _file_sha256(case_root / "clean-reference.wav"),
				},
			})
		render_manifest = {
			"schema_version": 2, "renderer": "mumble-audio-mixture-renderer-v2",
			"plan_sha256": PLAN.canonical_sha256(plan), "corpus_lock_sha256": plan["corpus_lock_sha256"],
			"corpus_inventory_sha256": plan["corpus_inventory_sha256"], "private_audio_do_not_upload": True,
			"cases": render_cases,
		}
		render_manifest_path = root / "render.json"; _write_json(render_manifest_path, render_manifest)
		adapter = root / "adapter.py"
		adapter.write_text(
			"import argparse,hashlib,json,wave\n"
			"from pathlib import Path\n"
			"def fh(path):\n"
			" d=hashlib.sha256()\n"
			" with open(path,'rb') as s:\n"
			"  for chunk in iter(lambda:s.read(1024*1024),b''): d.update(chunk)\n"
			" return d.hexdigest()\n"
			"def canonical(value): return hashlib.sha256(json.dumps(value,ensure_ascii=False,sort_keys=True,separators=(',',':')).encode('utf8')).hexdigest()\n"
			"def tree(root):\n"
			" root=Path(root).resolve(); entries=[]\n"
			" for path in sorted(root.rglob('*'),key=lambda p:p.relative_to(root).as_posix()):\n"
			"  if path.is_file(): entries.append({'path':path.relative_to(root).as_posix(),'sha256':fh(path),'size_bytes':path.stat().st_size})\n"
			" return canonical(entries)\n"
			"def delayed(source,target,latency):\n"
			" with wave.open(str(source),'rb') as r:\n"
			"  params=r.getparams(); frames=r.readframes(r.getnframes())\n"
			" with wave.open(str(target),'wb') as w:\n"
			"  w.setparams(params); w.writeframes(b'\\0\\0'*latency+frames)\n"
			"p=argparse.ArgumentParser();p.add_argument('--contract');p.add_argument('--result');p.add_argument('--input-enhancement-policy-manifest');p.add_argument('--input-enhancement-policy-signature');a=p.parse_args()\n"
			"c=json.load(open(a.contract,encoding='utf8')); out=c['output_root']+'/capture.wav'; pre=c['output_root']+'/sender-pre-opus.wav'\n"
			"paths=c['paths']; binding=c['authorized_product_bindings'][0]; engine=binding['engine']\n"
			"latency={'Original':0,'Light':480,'Balanced':1440,'Quality':1440,'VoiceFocus':1440}[c['profile']]\n"
			"delayed(c['clean_reference']['path'],out,latency); delayed(c['clean_reference']['path'],pre,latency); data=open(out,'rb').read(); pre_data=open(pre,'rb').read()\n"
			"identity={'contract_file_sha256':fh(a.contract),'run_provenance_sha256':canonical(json.load(open(paths['run_provenance'],encoding='utf8'))),'runtime_payload_sha256':tree(paths['runtime_root']),'client_binary_sha256':fh(paths['client_binary']),'server_binary_sha256':fh(paths['server_binary']),'model_manifest_sha256':fh(paths['model_manifest']),'recipe_manifest_sha256':fh(paths['recipe_manifest'])}\n"
			"worker_frames=100 if engine=='DeepFilterNet' else 0\n"
			"r={'schema_version':3,'status':'passed','role':c['role'],'profile':c['profile'],"
			"'receiver_cleanup':False,'input_sha256':c['input']['sha256'],"
			"'execution_identity':identity,"
			"'transport':{k:c['transport'][k] for k in ('opus_bitrate_bps','frames_per_packet','transmit_mode')},"
			"'capture':{'relative_path':'capture.wav','sha256':hashlib.sha256(data).hexdigest(),'size_bytes':len(data)},"
			"'sender_pre_opus':{'relative_path':'sender-pre-opus.wav','sha256':hashlib.sha256(pre_data).hexdigest(),'size_bytes':len(pre_data)},"
			"'diagnostics':{'active_profile':c['profile'],'active_engine':engine,'active_recipe':binding['recipe'],'active_models':binding['models'],'callback_frame_count':100,'callback_p99_ms':1.0,'model_initialization_attempts':1 if engine in ('RNNoise','DeepFilterNet') else 0,'worker_frame_count':worker_frames,'worker_p99_ms':2.0 if worker_frames else 0.0,'mean_rtf':0.1,'deadline_miss_count':0,'declared_latency_samples':latency,'fallback_count':0,'invalid_output_count':0,'tail_drained':True}}\n"
			"json.dump(r,open(a.result,'w',encoding='utf8'),sort_keys=True)\n",
			encoding="utf-8",
		)
		args = argparse.Namespace(
			plan=plan_path, case_id=selected_case["case_id"], render_manifest=render_manifest_path,
			render_root=audio, runtime_root=runtime, client_binary=client, server_binary=server,
			model_manifest=model_manifest, recipe_manifest=recipe_manifest, inventory=inventory_path,
			corpus_lock=manifest_path, metrics_manifest=metrics_manifest, adapter=adapter,
			input_enhancement_policy_manifest=policy_manifest,
			input_enhancement_policy_signature=policy_signature,
			qualification_case_set=plan_path, adapter_arg=[], output_root=root / "output", emit_contracts_only=False,
		)
		result = run(args)
		if result["status"] != "passed" or set(result["results"]) != {
			"control", "original_comparison", "candidate", "candidate_edge"
		}:
			raise AssertionError(
				"orchestration did not produce route, Original-comparison, noisy candidate, and clean edge evidence"
			)
		control_contract = _load_json(root / "output" / "control" / "adapter-contract.json")
		original_contract = _load_json(root / "output" / "original_comparison" / "adapter-contract.json")
		candidate_edge_contract = _load_json(root / "output" / "candidate_edge" / "adapter-contract.json")
		if control_contract["input"]["sha256"] != render_manifest["cases"][0]["clean_reference"]["sha256"]:
			raise AssertionError("clean Original route control did not use the clean reference")
		if result["route_control"]["end_loss_budget_samples"] != result["route_control"]["onset_budget_samples"]:
			raise AssertionError("Original route-origin budget was not applied symmetrically to both speech edges")
		if result["route_control"]["receiver_edge_gate"] != "route-bounded-not-input-latency":
			raise AssertionError("receiver capture was not explicitly separated from the hard input timeline gate")
		if result["input_timeline_gate"] != {
			"artifact": "sender_pre_opus",
			"alignment": "fixed-declared-latency",
			"roles": ["control", "candidate_edge"],
			"max_onset_loss_samples": FRAME_SAMPLES,
			"max_end_loss_samples": FRAME_SAMPLES,
			"complete_tail_required": True,
		}:
			raise AssertionError("hard input timeline gate has unexpected semantics")
		binding = result.get("qualification_binding")
		if (
			not isinstance(binding, dict)
			or binding.get("mixture_plan_sha256") != _file_sha256(plan_path)
			or binding.get("case_set_sha256") != _file_sha256(plan_path)
			or binding.get("plan_case_sha256") != _canonical_sha256(selected_case)
			or binding.get("render_entry_sha256") != _canonical_sha256(render_manifest["cases"][0])
		):
			raise AssertionError("E2E manifest did not bind the exact protected plan, case, and render entry")
		if original_contract["input"]["sha256"] != render_manifest["cases"][0]["input"]["sha256"]:
			raise AssertionError("noisy Original comparison did not use the rendered mixture")
		if (
			candidate_edge_contract["input"]["sha256"]
			!= render_manifest["cases"][0]["clean_reference"]["sha256"]
			or candidate_edge_contract["profile"] != selected_case["profile"]
		):
			raise AssertionError("candidate_edge did not use clean input with the enhanced candidate profile")
		for role in ("control", "candidate_edge"):
			evidence = result["results"][role]
			pre_opus_score_path = root / "output" / role / "pre-opus-fixed-timeline-score.json"
			pre_opus_score = _load_json(pre_opus_score_path)
			if (
				pre_opus_score.get("passed") is not True
				or evidence["pre_opus_fixed_timeline_score_sha256"] != _file_sha256(pre_opus_score_path)
				or evidence["sender_pre_opus_sha256"]
				!= _load_json(root / "output" / role / "adapter-result.json")["sender_pre_opus"]["sha256"]
			):
				raise AssertionError(f"{role} did not bind a passing sender pre-Opus timeline artifact")
		for role in ("original_comparison", "candidate"):
			if result["results"][role]["pre_opus_fixed_timeline_score_sha256"] is not None:
				raise AssertionError(f"noisy {role} was incorrectly used as a hard speech-edge anchor")
		candidate_evidence = result["results"]["candidate"]
		candidate_edge_evidence = result["results"]["candidate_edge"]
		if (
			candidate_evidence["active_recipe"]["id"] != "input.quality.deepfilternet-low-latency"
			or candidate_evidence["active_models"][0]["id"] != "deepfilternet:low-latency"
			or candidate_evidence["performance"]["model_initialization_attempts"] != 1
			or candidate_evidence["performance"]["worker_frame_count"] <= 0
			or candidate_evidence["execution_identity"]["run_provenance_sha256"] != result["run_provenance_sha256"]
		):
			raise AssertionError("final evidence did not preserve the active binding, worker metrics, and provenance")
		if candidate_edge_evidence["qualification_purpose"] != "clean-enhanced-input-edge-probe":
			raise AssertionError("candidate_edge evidence was not labelled as the clean enhanced input-edge probe")

		original_args = argparse.Namespace(
			plan=plan_path, case_id=original_case["case_id"], render_manifest=render_manifest_path,
			render_root=audio, runtime_root=runtime, client_binary=client, server_binary=server,
			model_manifest=model_manifest, recipe_manifest=recipe_manifest, inventory=inventory_path,
			corpus_lock=manifest_path, metrics_manifest=metrics_manifest, adapter=adapter,
			input_enhancement_policy_manifest=None, input_enhancement_policy_signature=None,
			qualification_case_set=plan_path, adapter_arg=[], output_root=root / "original-output",
			emit_contracts_only=False,
		)
		original_result = run(original_args)
		if set(original_result["results"]) != {
			"control", "original_comparison", "candidate", "candidate_edge"
		}:
			raise AssertionError("Original did not produce the complete receiver-comparison role set")
		original_candidate = original_result["results"]["candidate"]
		if (
			original_candidate["active_recipe"]["id"] != "input.original"
			or original_candidate["active_models"] != []
			or original_candidate["performance"]["model_initialization_attempts"] != 0
			or original_candidate["fixed_timeline_score_sha256"]
			!= _file_sha256(root / "original-output" / "candidate" / "fixed-timeline-score.json")
		):
			raise AssertionError("Original candidate did not retain its zero-model paired-route contract")
		original_candidate_score = _load_json(
			root / "original-output" / "candidate" / "fixed-timeline-score.json"
		)
		original_edge_score = _load_json(
			root / "original-output" / "candidate_edge" / "pre-opus-fixed-timeline-score.json"
		)
		if (
			original_candidate_score.get("passed") is not True
			or original_candidate_score.get("timeline_alignment") != "fixed-paired-original-route"
			or original_candidate_score.get("declared_latency_samples") != 0
			or original_edge_score.get("passed") is not True
		):
			raise AssertionError("Original candidate did not produce passing zero-latency route and input-edge scores")
		objective = _load_script("objective_quality_score.py", "mumble_two_client_original_objective")
		original_role_root = root / "original-output"
		original_candidate_result = _load_json(original_role_root / "candidate" / "adapter-result.json")
		original_comparison_result = _load_json(
			original_role_root / "original_comparison" / "adapter-result.json"
		)
		original_render_entry = next(
			entry for entry in render_cases if entry["case_id"] == original_case["case_id"]
		)
		route_offset, route_binding = objective._receiver_route_alignment(
			argparse.Namespace(
				case_id=original_case["case_id"], profile="Original",
				original_latency_samples=0, candidate_latency_samples=0,
				route_control_wav=original_role_root / "control" / "capture.wav",
				route_control_score=original_role_root / "control" / "fixed-timeline-score.json",
				candidate_fixed_timeline_score=original_role_root / "candidate" / "fixed-timeline-score.json",
				route_e2e_manifest=original_role_root / "e2e-manifest.json",
			),
			objective.file_record(audio / original_render_entry["clean_reference"]["path"]),
			{
				"noisy_original": objective.file_record(
					original_role_root / "original_comparison"
					/ original_comparison_result["capture"]["relative_path"]
				),
				"candidate": objective.file_record(
					original_role_root / "candidate"
					/ original_candidate_result["capture"]["relative_path"]
				),
			},
		)
		if route_offset < 0 or route_binding["edge_tail_gate"]["candidate_passed"] is not True:
			raise AssertionError("Original paired route is not accepted by the objective receiver scorer")

		paired_plan = PLAN.generate_plan(
			manifest, inventory, "release", "validation", "mumble-plan-self-test", 30, 1000,
		)
		paired_groups: dict[str, list[Mapping[str, Any]]] = {}
		for paired_case in paired_plan["cases"]:
			if paired_case.get("comparison_scene_id") is not None:
				paired_groups.setdefault(str(paired_case["comparison_scene_id"]), []).append(paired_case)
		paired_cases = next(
			rows for rows in paired_groups.values()
			if rows[0]["noise"] is not None and rows[0]["mix"]["snr_db"] <= 0
		)
		paired_plan_path = root / "paired-plan.json"; _write_json(paired_plan_path, paired_plan)
		paired_audio = root / "paired-audio"
		shared_input = paired_audio / "shared-input.wav"; _write_sine(shared_input, frequency_hz=275)
		shared_clean = paired_audio / "shared-clean.wav"; _write_sine(shared_clean, frequency_hz=375)
		paired_render_cases = []
		for paired_case in paired_cases:
			paired_render_cases.append({
				"case_id": paired_case["case_id"],
				"input": {"path": shared_input.relative_to(paired_audio).as_posix(), "sha256": _file_sha256(shared_input)},
				"clean_reference": {"path": shared_clean.relative_to(paired_audio).as_posix(), "sha256": _file_sha256(shared_clean)},
			})
		paired_render_manifest = {
			"schema_version": 2, "renderer": "mumble-audio-mixture-renderer-v2",
			"plan_sha256": PLAN.canonical_sha256(paired_plan),
			"corpus_lock_sha256": paired_plan["corpus_lock_sha256"],
			"corpus_inventory_sha256": paired_plan["corpus_inventory_sha256"],
			"private_audio_do_not_upload": True, "cases": paired_render_cases,
		}
		paired_render_path = root / "paired-render.json"; _write_json(paired_render_path, paired_render_manifest)
		paired_args = argparse.Namespace(
			plan=paired_plan_path, case_id=paired_cases[0]["case_id"], paired_case_id=paired_cases[1]["case_id"],
			render_manifest=paired_render_path, render_root=paired_audio,
			runtime_root=runtime, client_binary=client, server_binary=server,
			model_manifest=model_manifest, recipe_manifest=recipe_manifest, inventory=inventory_path,
			corpus_lock=manifest_path, metrics_manifest=metrics_manifest, adapter=adapter,
			input_enhancement_policy_manifest=policy_manifest,
			input_enhancement_policy_signature=policy_signature,
			qualification_case_set=paired_plan_path, adapter_arg=[], output_root=root / "paired-output",
			emit_contracts_only=False,
		)
		paired_result = run(paired_args)
		if paired_result.get("kind") != "mumble-two-client-paired-e2e-v1" or len(paired_result.get("cases", [])) != 2:
			raise AssertionError("paired orchestration did not emit two logical case manifests")
		logical_manifests = {
			case["profile"]: _load_json(
				root / "paired-output" / next(
					record["relative_path"] for record in paired_result["cases"] if record["profile"] == case["profile"]
				)
			)
			for case in paired_cases
		}
		for shared_role in ("control", "original_comparison"):
			quality_shared = logical_manifests["Quality"]["results"][shared_role]
			voice_shared = logical_manifests["VoiceFocus"]["results"][shared_role]
			if quality_shared != voice_shared:
				raise AssertionError(f"paired {shared_role} evidence was not shared byte-for-byte")
		paired_route_bindings = {}
		for paired_case in paired_cases:
			case_id = paired_case["case_id"]
			paired_case_root = root / "paired-output" / "cases" / case_id
			candidate_result = _load_json(paired_case_root / "candidate" / "adapter-result.json")
			original_result = _load_json(root / "paired-output" / "shared" / "original_comparison" / "adapter-result.json")
			_, binding = objective._receiver_route_alignment(
				argparse.Namespace(
					case_id=case_id, profile=paired_case["profile"], original_latency_samples=0,
					candidate_latency_samples=candidate_result["diagnostics"]["declared_latency_samples"],
					route_control_wav=root / "paired-output" / "shared" / "control" / "capture.wav",
					route_control_score=root / "paired-output" / "shared" / "control" / "fixed-timeline-score.json",
					candidate_fixed_timeline_score=paired_case_root / "candidate" / "fixed-timeline-score.json",
					route_e2e_manifest=paired_case_root / "e2e-manifest.json",
				),
				objective.file_record(shared_clean),
				{
					"noisy_original": objective.file_record(
						root / "paired-output" / "shared" / "original_comparison" / original_result["capture"]["relative_path"]
					),
					"candidate": objective.file_record(paired_case_root / "candidate" / candidate_result["capture"]["relative_path"]),
				},
			)
			paired_route_bindings[paired_case["profile"]] = binding
		for field in ("control_wav", "control_fixed_timeline_score", "route_offset_samples", "stable_execution_identity"):
			if paired_route_bindings["Quality"][field] != paired_route_bindings["VoiceFocus"][field]:
				raise AssertionError(f"paired objective route binding did not share {field}")
		tampered_pair_plan = json.loads(json.dumps(paired_plan))
		tampered_voice = next(case for case in tampered_pair_plan["cases"] if case["case_id"] == paired_cases[1]["case_id"])
		tampered_voice["startup"]["preroll_ms"] = 300 if tampered_voice["startup"]["preroll_ms"] == 0 else 0
		tampered_pair_path = root / "tampered-paired-plan.json"; _write_json(tampered_pair_path, tampered_pair_plan)
		tampered_args = argparse.Namespace(**{**vars(paired_args), "plan": tampered_pair_path, "output_root": root / "tampered-pair-output"})
		try:
			run(tampered_args)
		except (E2EError, PLAN.PlanError):
			pass
		else:
			raise AssertionError("paired E2E accepted mismatched startup contracts")

		candidate_root = root / "output" / "candidate"
		candidate_contract_path = candidate_root / "adapter-contract.json"
		candidate_contract = _load_json(candidate_contract_path)
		candidate_contract_sha256 = _file_sha256(candidate_contract_path)
		valid_adapter_result = _load_json(candidate_root / "adapter-result.json")
		negative_result_path = candidate_root / "negative-adapter-result.json"

		def expect_result_failure(label: str, mutate: Any) -> None:
			invalid_result = json.loads(json.dumps(valid_adapter_result))
			mutate(invalid_result)
			_write_json(negative_result_path, invalid_result)
			try:
				_validate_adapter_result(
					negative_result_path, candidate_contract, candidate_contract_path,
					candidate_contract_sha256, candidate_root, 1440,
				)
			except E2EError:
				return
			raise AssertionError(f"adapter validation accepted corrupted {label} evidence")

		for identity_field in sorted(EXECUTION_IDENTITY_FIELDS | {"contract_file_sha256"}):
			expect_result_failure(
				identity_field,
				lambda value, field=identity_field: value["execution_identity"].__setitem__(field, "0" * 64),
			)
		expect_result_failure(
			"active recipe",
			lambda value: value["diagnostics"]["active_recipe"].__setitem__("id", "input.quality.changed"),
		)
		expect_result_failure(
			"active model",
			lambda value: value["diagnostics"]["active_models"][0].__setitem__("sha256", "0" * 64),
		)
		expect_result_failure(
			"declared semantics-v5 latency",
			lambda value: value["diagnostics"].__setitem__("declared_latency_samples", 960),
		)
		expect_result_failure(
			"sender pre-Opus artifact",
			lambda value: value["sender_pre_opus"].__setitem__("sha256", "0" * 64),
		)
		expect_result_failure(
			"model initialization attempts",
			lambda value: value["diagnostics"].__setitem__("model_initialization_attempts", 0),
		)
		expect_result_failure(
			"callback budget",
			lambda value: value["diagnostics"].__setitem__(
				"callback_p99_ms", candidate_contract["performance_budgets"]["callback_p99_ms"] + 0.01
			),
		)
		expect_result_failure(
			"worker budget",
			lambda value: value["diagnostics"].__setitem__(
				"worker_p99_ms", candidate_contract["performance_budgets"]["worker_p99_ms"] + 0.01
			),
		)
		expect_result_failure(
			"mean RTF budget",
			lambda value: value["diagnostics"].__setitem__(
				"mean_rtf", candidate_contract["performance_budgets"]["mean_rtf"] + 0.01
			),
		)

		candidate_edge_root = root / "output" / "candidate_edge"
		late_pre_opus = candidate_edge_root / "late-pre-opus.wav"
		with wave.open(str(case_root / "clean-reference.wav"), "rb") as source:
			parameters = source.getparams()
			frames = source.readframes(source.getnframes())
		with wave.open(str(late_pre_opus), "wb") as target:
			target.setparams(parameters)
			target.writeframes(b"\x00\x00" * (FRAME_SAMPLES * 2) + frames)
		late_score_path = candidate_edge_root / "late-pre-opus-fixed-timeline-score.json"
		try:
			_score(
				case_root / "clean-reference.wav", late_pre_opus, 0, late_score_path,
				require_complete_tail=True,
				max_onset_loss_samples=FRAME_SAMPLES,
				max_end_loss_samples=FRAME_SAMPLES,
			)
		except E2EError:
			late_score = _load_json(late_score_path)
			if late_score.get("passed") is not False or late_score.get("onset_loss_samples") != FRAME_SAMPLES * 2:
				raise AssertionError("negative pre-Opus timeline case failed for an unexpected reason")
		else:
			raise AssertionError("hard pre-Opus timeline gate accepted a two-frame onset regression")

		# A VAD sender is allowed to stop producing packet-path callbacks once only
		# trailing room silence remains, but its attested pre-Opus WAV still has a
		# source-timeline contract. Keep the complete-tail gate strict: the runtime
		# capture backend must reconstruct that unsent source interval as zero PCM,
		# while any causal latency beyond the source still needs real drain output.
		vad_reference = candidate_edge_root / "vad-source-timeline.wav"
		vad_truncated = candidate_edge_root / "vad-truncated-pre-opus.wav"
		vad_completed = candidate_edge_root / "vad-completed-pre-opus.wav"
		speech_samples = FRAME_SAMPLES * 6
		trailing_silence_samples = FRAME_SAMPLES * 3
		speech_pcm = b"".join(
			struct.pack("<h", round(6000 * math.sin(2 * math.pi * 330 * index / 48000)))
			for index in range(speech_samples)
		)
		with wave.open(str(vad_reference), "wb") as target:
			target.setnchannels(1); target.setsampwidth(2); target.setframerate(48000)
			target.writeframes(speech_pcm + b"\x00\x00" * trailing_silence_samples)
		with wave.open(str(vad_truncated), "wb") as target:
			target.setnchannels(1); target.setsampwidth(2); target.setframerate(48000)
			target.writeframes(speech_pcm)
		with wave.open(str(vad_completed), "wb") as target:
			target.setnchannels(1); target.setsampwidth(2); target.setframerate(48000)
			target.writeframes(speech_pcm + b"\x00\x00" * trailing_silence_samples)

		truncated_score_path = candidate_edge_root / "vad-truncated-score.json"
		try:
			_score(vad_reference, vad_truncated, 0, truncated_score_path, require_complete_tail=True)
		except E2EError:
			truncated_score = _load_json(truncated_score_path)
			if (
				truncated_score.get("passed") is not False
				or truncated_score.get("missing_tail_samples") != trailing_silence_samples
				or truncated_score.get("onset_loss_samples") > FRAME_SAMPLES
				or truncated_score.get("end_loss_samples") > FRAME_SAMPLES
			):
				raise AssertionError("truncated VAD source timeline failed for an unexpected reason")
		else:
			raise AssertionError("complete-tail gate accepted a truncated VAD source timeline")

		completed_score = _score(
			vad_reference, vad_completed, 0, candidate_edge_root / "vad-completed-score.json",
			require_complete_tail=True,
		)
		if completed_score.get("passed") is not True or completed_score.get("missing_tail_samples") != 0:
			raise AssertionError("zero-completed VAD source timeline did not satisfy the strict tail gate")


def main(argv: Sequence[str] | None = None) -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--plan", type=Path)
	parser.add_argument("--case-id")
	parser.add_argument(
		"--paired-case-id",
		help="run one Quality/VoiceFocus comparison pair with shared control and Original roles",
	)
	parser.add_argument("--render-manifest", type=Path)
	parser.add_argument("--render-root", type=Path)
	parser.add_argument("--runtime-root", type=Path)
	parser.add_argument("--client-binary", type=Path)
	parser.add_argument("--server-binary", type=Path)
	parser.add_argument("--model-manifest", type=Path)
	parser.add_argument("--recipe-manifest", type=Path)
	parser.add_argument("--inventory", type=Path)
	parser.add_argument(
		"--qualification-case-set", type=Path,
		help="optional protected case-set file; required downstream before E2E evidence can qualify a release",
	)
	parser.add_argument("--corpus-lock", type=Path, default=Path(__file__).with_name("corpus-lock.json"))
	parser.add_argument("--metrics-manifest", type=Path)
	parser.add_argument(
		"--input-enhancement-policy-manifest", type=Path,
		help="explicit signed channel policy; required for every enhanced schema-v3 run",
	)
	parser.add_argument(
		"--input-enhancement-policy-signature", type=Path,
		help="raw 64-byte Ed25519 signature paired with --input-enhancement-policy-manifest",
	)
	parser.add_argument("--adapter", type=Path)
	parser.add_argument("--adapter-arg", action="append", default=[])
	parser.add_argument("--output-root", type=Path)
	parser.add_argument("--emit-contracts-only", action="store_true")
	parser.add_argument("--self-test", action="store_true")
	args = parser.parse_args(argv)
	try:
		if args.self_test:
			run_self_test()
			print("two-client E2E core self-test: ok")
			if args.plan is None:
				return 0
		required = (
			"plan", "case_id", "render_manifest", "render_root", "runtime_root", "client_binary", "server_binary",
			"model_manifest", "recipe_manifest", "inventory", "metrics_manifest", "adapter", "output_root",
		)
		missing = [name for name in required if getattr(args, name) is None]
		if missing:
			raise E2EError(f"missing required arguments: {', '.join('--' + name.replace('_', '-') for name in missing)}")
		result = run(args)
		print(f"two-client E2E: {result['status']}; case={result['case_id']}; output={args.output_root}")
		return 0
	except (E2EError, INVENTORY.InventoryError, LOCK.ValidationError, PLAN.PlanError, AssertionError) as error:
		print(f"two-client E2E: error: {error}", file=sys.stderr)
		return 1


if __name__ == "__main__":
	sys.exit(main())
