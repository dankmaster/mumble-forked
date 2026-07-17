#!/usr/bin/env python3
"""Freeze a license-approved, hash-bound RNNoise training campaign.

This tool deliberately does not train a model.  It seals the exact corpus,
inventory, tuning mixture, toolchain files and deterministic seeds that a
separate protected trainer is allowed to consume.  Any source used by the
mixture must be explicitly approved for training in corpus-lock.json.
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import importlib.util
import json
import os
import re
import sys
import tempfile
from pathlib import Path, PurePosixPath
from typing import Any, Mapping, MutableMapping, Sequence


class CampaignError(ValueError):
	"""Raised when a training campaign is not reproducible or license-safe."""


HEX64 = re.compile(r"[0-9a-f]{64}")
IDENTIFIER = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]*")
MINIMUM_SEED_COUNT = 5
SEED_DERIVATION = "uint64-be(sha256('rnnoise-seed-v1\\0' + seed_root + '\\0' + input_fingerprint + '\\0' + zero_based_index)[0:8])"


def _load_sibling(filename: str, module_name: str) -> Any:
	path = Path(__file__).with_name(filename)
	spec = importlib.util.spec_from_file_location(module_name, path)
	if spec is None or spec.loader is None:
		raise CampaignError(f"unable to load required tool: {path}")
	module = importlib.util.module_from_spec(spec)
	spec.loader.exec_module(module)
	return module


LOCK = _load_sibling("validate-corpus-lock.py", "mumble_rnnoise_campaign_lock")
INVENTORY = _load_sibling("corpus-inventory-v3.py", "mumble_rnnoise_campaign_inventory")
MIXTURE = _load_sibling("generate-mixture-plan.py", "mumble_rnnoise_campaign_mixture")


def canonical_sha256(value: Any) -> str:
	encoded = json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")
	return hashlib.sha256(encoded).hexdigest()


def _mixture_identity(plan: Mapping[str, Any], file_hash: str) -> Mapping[str, Any]:
	case_ids = [case["case_id"] for case in plan["cases"]]
	return {
		"canonical_sha256": MIXTURE.canonical_sha256(plan),
		"case_count": len(case_ids),
		"case_ids_sha256": canonical_sha256(case_ids),
		"file_sha256": file_hash,
	}


def file_sha256(path: Path) -> str:
	digest = hashlib.sha256()
	with path.open("rb") as stream:
		for chunk in iter(lambda: stream.read(1024 * 1024), b""):
			digest.update(chunk)
	return digest.hexdigest()


def load_json(path: Path) -> Any:
	def reject_duplicates(pairs: Sequence[tuple[str, Any]]) -> MutableMapping[str, Any]:
		result: MutableMapping[str, Any] = {}
		for key, value in pairs:
			if key in result:
				raise CampaignError(f"{path}: duplicate JSON key: {key}")
			result[key] = value
		return result

	try:
		return json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=reject_duplicates)
	except (OSError, json.JSONDecodeError) as error:
		raise CampaignError(f"unable to read {path}: {error}") from error


def _expect(condition: bool, path: str, message: str) -> None:
	if not condition:
		raise CampaignError(f"{path}: {message}")


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


def _write_json_atomic(path: Path, value: Mapping[str, Any]) -> None:
	path.parent.mkdir(parents=True, exist_ok=True)
	temporary = path.with_name(f".{path.name}.{os.getpid()}.tmp")
	temporary.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")
	os.replace(temporary, path)


def validate_toolchain(value: Any, root: Path) -> Mapping[str, Any]:
	_expect(root.is_dir() and not root.is_symlink(), "toolchain_root", "must be a real directory, not a symlink")
	root_resolved = root.resolve()
	manifest = _exact_keys(
		value,
		{"files", "output_model", "runtime", "schema_version", "toolchain_id"},
		set(),
		"toolchain",
	)
	_expect(manifest["schema_version"] == 1, "toolchain.schema_version", "unsupported version")
	_identifier(manifest["toolchain_id"], "toolchain.toolchain_id")
	runtime = _exact_keys(manifest["runtime"], {"name", "revision", "version"}, set(), "toolchain.runtime")
	for key in ("name", "revision", "version"):
		_expect(isinstance(runtime[key], str) and bool(runtime[key].strip()), f"toolchain.runtime.{key}", "required")
	output_model = _exact_keys(
		manifest["output_model"],
		{"attribution_file_relative_path", "attribution_sha256", "license_spdx"},
		set(),
		"toolchain.output_model",
	)
	_safe_relative_path(output_model["attribution_file_relative_path"], "toolchain.output_model.attribution_file_relative_path")
	_hash(output_model["attribution_sha256"], "toolchain.output_model.attribution_sha256")
	_expect(isinstance(output_model["license_spdx"], str) and bool(output_model["license_spdx"].strip()),
		"toolchain.output_model.license_spdx", "a reviewed output-model license is required")
	_expect(isinstance(manifest["files"], list) and manifest["files"], "toolchain.files", "must not be empty")
	roles: list[str] = []
	paths: list[str] = []
	for index, item_value in enumerate(manifest["files"]):
		item = _exact_keys(item_value, {"relative_path", "role", "sha256", "size_bytes"}, set(), f"toolchain.files[{index}]")
		role = _identifier(item["role"], f"toolchain.files[{index}].role")
		relative = _safe_relative_path(item["relative_path"], f"toolchain.files[{index}].relative_path")
		_hash(item["sha256"], f"toolchain.files[{index}].sha256")
		_expect(isinstance(item["size_bytes"], int) and not isinstance(item["size_bytes"], bool) and item["size_bytes"] > 0,
			f"toolchain.files[{index}].size_bytes", "must be positive")
		unresolved = root / Path(*PurePosixPath(relative).parts)
		current = root
		for component in PurePosixPath(relative).parts:
			current = current / component
			_expect(not current.is_symlink(), f"toolchain.files[{index}]", "symlink components are forbidden")
		artifact = unresolved.resolve()
		try:
			artifact.relative_to(root_resolved)
		except ValueError as error:
			raise CampaignError(f"toolchain.files[{index}].relative_path: escapes toolchain root") from error
		_expect(artifact.is_file() and not artifact.is_symlink(), f"toolchain.files[{index}]", "artifact is missing or a symlink")
		_expect(artifact.stat().st_size == item["size_bytes"], f"toolchain.files[{index}].size_bytes", "artifact size mismatch")
		_expect(file_sha256(artifact) == item["sha256"], f"toolchain.files[{index}].sha256", "artifact hash mismatch")
		roles.append(role)
		paths.append(relative)
	_expect(paths == sorted(set(paths)), "toolchain.files", "must be sorted by unique relative_path")
	for required_role in ("trainer", "training-config", "attribution-notice"):
		_expect(roles.count(required_role) == 1, "toolchain.files", f"exactly one {required_role} role is required")
	attribution_files = [item for item in manifest["files"] if item["role"] == "attribution-notice"]
	_expect(len(attribution_files) == 1, "toolchain.files", "exactly one attribution-notice is required")
	_expect(attribution_files[0]["relative_path"] == output_model["attribution_file_relative_path"],
		"toolchain.output_model.attribution_file_relative_path", "does not name the attribution-notice file")
	_expect(attribution_files[0]["sha256"] == output_model["attribution_sha256"],
		"toolchain.output_model.attribution_sha256", "does not match the attribution-notice file")
	return manifest


def _referenced_assets(plan: Mapping[str, Any]) -> list[tuple[str, Mapping[str, Any]]]:
	result: list[tuple[str, Mapping[str, Any]]] = []
	for case in plan["cases"]:
		for kind, asset in (
			("speech", case["speech"]),
			("noise", case["noise"]),
			("rir", case["mix"]["rir"]),
			("microphone_response", case["mix"]["microphone_response"]),
		):
			if asset is not None:
				result.append((kind, asset))
	return result


def _group_ids_by_kind(plan: Mapping[str, Any]) -> Mapping[str, set[str]]:
	result = {kind: set() for kind in ("speech", "noise", "rir", "microphone_response")}
	for kind, asset in _referenced_assets(plan):
		_expect(isinstance(asset.get("group_id"), str) and bool(asset["group_id"]), f"{kind}.group_id", "required")
		result[kind].add(asset["group_id"])
	return result


def _validate_disjoint_splits(plans: Mapping[str, Mapping[str, Any]]) -> None:
	groups = {name: _group_ids_by_kind(plan) for name, plan in plans.items()}
	names = sorted(groups)
	for left_index, left in enumerate(names):
		for right in names[left_index + 1:]:
			for kind in groups[left]:
				overlap = sorted(groups[left][kind] & groups[right][kind])
				_expect(not overlap, f"mixture splits {left}/{right}", f"{kind} groups overlap: {', '.join(overlap[:3])}")


def _validate_training_sources(
	manifest: Mapping[str, Any], inventory_items: Sequence[Mapping[str, Any]], plan: Mapping[str, Any]
) -> list[Mapping[str, Any]]:
	locked = {source["id"]: source for source in manifest["sources"]}
	items = {item["id"]: item for item in inventory_items}
	used_source_ids: set[str] = set()
	kinds: set[str] = set()
	for kind, asset in _referenced_assets(plan):
		item_id = asset.get("item_id")
		_expect(item_id in items, f"mixture asset {item_id}", "not present in the locked inventory")
		item = items[item_id]
		_expect(item["kind"] == kind, f"mixture asset {item_id}.kind", "inventory kind mismatch")
		for key in ("source_id", "sha256", "size_bytes", "source_artifact_sha256"):
			_expect(asset.get(key) == item[key], f"mixture asset {item_id}.{key}", "inventory binding mismatch")
		source_id = item["source_id"]
		if kind == "microphone_response" and source_id == INVENTORY.MODELED_RESPONSE_SOURCE_ID:
			_expect(
				item["source_artifact_sha256"] == INVENTORY.file_sha256(INVENTORY.MODELED_RESPONSE_DEFINITION),
				f"source {source_id}", "tracked response-definition hash mismatch",
			)
			_expect(item["provenance"]["derivation"] == "synthesized", f"source {source_id}", "modeled response is not a synthesized transform")
			kinds.add(kind)
			continue
		_expect(source_id in locked, f"source {source_id}", "not present in corpus lock")
		source = locked[source_id]
		_expect(source["license"]["status"] == "verified", f"source {source_id}.license", "license is not verified")
		_expect(source["training_status"] == "allowed_with_attribution", f"source {source_id}.training_status",
			"training is not explicitly allowed")
		_expect("training_candidate" in source["roles"], f"source {source_id}.roles", "training_candidate role is required")
		used_source_ids.add(source_id)
		kinds.add(kind)
	_expect("speech" in kinds, "mixture plan", "licensed training speech is required")
	_expect("noise" in kinds, "mixture plan", "licensed training noise is required; clean-only campaigns are forbidden")
	return [
		{
			"id": source_id,
			"integrity": copy.deepcopy(locked[source_id]["integrity"]),
			"kind": locked[source_id]["kind"],
			"license": copy.deepcopy(locked[source_id]["license"]),
			"roles": copy.deepcopy(locked[source_id]["roles"]),
			"training_status": locked[source_id]["training_status"],
		}
		for source_id in sorted(used_source_ids)
	]


def freeze_campaign(
	manifest: Mapping[str, Any],
	inventory: Mapping[str, Any],
	tuning_mixture: Mapping[str, Any],
	validation_mixture: Mapping[str, Any],
	holdout_mixture: Mapping[str, Any],
	toolchain: Mapping[str, Any],
	*,
	input_file_hashes: Mapping[str, str],
	campaign_id: str,
	seed_root: str,
	seed_count: int,
) -> Mapping[str, Any]:
	try:
		LOCK.validate_manifest(manifest)
	except ValueError as error:
		raise CampaignError(f"corpus lock: {error}") from error
	training_noise_sources = [
		source for source in manifest["sources"]
		if source["kind"] in ("environmental_noise", "environmental_noise_and_rir", "paired_clean_noisy_speech")
		and source["license"]["status"] == "verified"
		and source["training_status"] == "allowed_with_attribution"
		and "training_candidate" in source["roles"]
	]
	_expect(training_noise_sources, "corpus lock", "no license-approved training noise source is available")
	try:
		inventory_items = INVENTORY.validate_inventory(inventory, manifest, require_release=True)
		for split_name, plan in (("tuning", tuning_mixture), ("validation", validation_mixture), ("holdout", holdout_mixture)):
			MIXTURE.validate_plan(plan)
			_expect(plan["split"] == split_name, f"{split_name}_mixture.split", f"must be {split_name}")
	except ValueError as error:
		raise CampaignError(str(error)) from error
	_identifier(campaign_id, "campaign_id")
	_expect(isinstance(seed_root, str) and bool(seed_root.strip()), "seed_root", "must be non-empty")
	_expect(isinstance(seed_count, int) and not isinstance(seed_count, bool) and seed_count >= MINIMUM_SEED_COUNT,
		"seed_count", f"must be at least {MINIMUM_SEED_COUNT}")
	lock_canonical = LOCK.canonical_manifest_sha256(manifest)
	inventory_canonical = INVENTORY.canonical_sha256(inventory)
	plans = {"holdout": holdout_mixture, "tuning": tuning_mixture, "validation": validation_mixture}
	for name, plan in plans.items():
		_expect(plan["corpus_lock_sha256"] == lock_canonical, f"{name}_mixture.corpus_lock_sha256", "lock mismatch")
		_expect(plan["corpus_inventory_sha256"] == inventory_canonical, f"{name}_mixture.corpus_inventory_sha256", "inventory mismatch")
		_expect(plan["seed"] == tuning_mixture["seed"], f"{name}_mixture.seed", "all frozen splits must use the same split seed")
		_expect(plan["split_algorithm"] == tuning_mixture["split_algorithm"], f"{name}_mixture.split_algorithm", "split algorithm mismatch")
	_validate_disjoint_splits(plans)
	for key in ("corpus_lock", "inventory", "tuning_mixture_plan", "validation_mixture_plan", "holdout_mixture_plan", "toolchain_manifest"):
		_hash(input_file_hashes.get(key), f"input_file_hashes.{key}")
	approved_sources = _validate_training_sources(manifest, inventory_items, tuning_mixture)
	inputs = {
		"corpus_lock": {"canonical_sha256": lock_canonical, "file_sha256": input_file_hashes["corpus_lock"]},
		"inventory": {"canonical_sha256": inventory_canonical, "file_sha256": input_file_hashes["inventory"]},
		"tuning_mixture_plan": _mixture_identity(tuning_mixture, input_file_hashes["tuning_mixture_plan"]),
		"validation_mixture_plan": _mixture_identity(validation_mixture, input_file_hashes["validation_mixture_plan"]),
		"holdout_mixture_plan": _mixture_identity(holdout_mixture, input_file_hashes["holdout_mixture_plan"]),
		"toolchain_manifest": {"canonical_sha256": canonical_sha256(toolchain), "file_sha256": input_file_hashes["toolchain_manifest"]},
	}
	input_fingerprint = canonical_sha256(inputs)
	jobs = []
	seeds: set[int] = set()
	for index in range(seed_count):
		seed_digest = hashlib.sha256(f"rnnoise-seed-v1\0{seed_root}\0{input_fingerprint}\0{index}".encode("utf-8")).digest()
		seed = int.from_bytes(seed_digest[:8], "big")
		_expect(seed not in seeds, "training_jobs", "deterministic seed collision")
		seeds.add(seed)
		candidate_id = f"rnnoise-{index + 1:02d}-{seed_digest.hex()[:12]}"
		output_path = f"rnnoise/custom/{campaign_id}/{candidate_id}.weights_blob.bin"
		jobs.append({
			"candidate_id": candidate_id,
			"input_fingerprint_sha256": canonical_sha256({"inputs": inputs, "seed": seed}),
			"output_manifest_relative_path": output_path,
			"seed": seed,
		})
	result = {
		"approved_training_sources": approved_sources,
		"campaign_id": campaign_id,
		"input_fingerprint_sha256": input_fingerprint,
		"inputs": inputs,
		"schema_version": 1,
		"seed_derivation": SEED_DERIVATION,
		"seed_root": seed_root,
		"training_jobs": jobs,
		"toolchain": copy.deepcopy(toolchain),
	}
	validate_frozen_campaign(result)
	return result


def validate_frozen_campaign(value: Any) -> Mapping[str, Any]:
	plan = _exact_keys(value, {
		"approved_training_sources", "campaign_id", "input_fingerprint_sha256", "inputs", "schema_version",
		"seed_derivation", "seed_root", "toolchain", "training_jobs",
	}, set(), "training_plan")
	_expect(plan["schema_version"] == 1, "training_plan.schema_version", "unsupported version")
	_identifier(plan["campaign_id"], "training_plan.campaign_id")
	_hash(plan["input_fingerprint_sha256"], "training_plan.input_fingerprint_sha256")
	_expect(plan["seed_derivation"] == SEED_DERIVATION, "training_plan.seed_derivation", "unknown seed derivation")
	_expect(isinstance(plan["seed_root"], str) and bool(plan["seed_root"].strip()), "training_plan.seed_root", "required")
	inputs = _exact_keys(plan["inputs"], {
		"corpus_lock", "holdout_mixture_plan", "inventory", "toolchain_manifest", "tuning_mixture_plan", "validation_mixture_plan",
	}, set(), "training_plan.inputs")
	for name, identity_value in inputs.items():
		required = {"canonical_sha256", "file_sha256"}
		if name.endswith("_mixture_plan"):
			required |= {"case_count", "case_ids_sha256"}
		identity = _exact_keys(identity_value, required, set(), f"training_plan.inputs.{name}")
		_hash(identity["canonical_sha256"], f"training_plan.inputs.{name}.canonical_sha256")
		_hash(identity["file_sha256"], f"training_plan.inputs.{name}.file_sha256")
		if name.endswith("_mixture_plan"):
			_hash(identity["case_ids_sha256"], f"training_plan.inputs.{name}.case_ids_sha256")
			_expect(isinstance(identity["case_count"], int) and not isinstance(identity["case_count"], bool) and identity["case_count"] > 0,
				f"training_plan.inputs.{name}.case_count", "must be positive")
	_expect(plan["input_fingerprint_sha256"] == canonical_sha256(inputs), "training_plan.input_fingerprint_sha256", "input fingerprint mismatch")
	toolchain = _exact_keys(plan["toolchain"], {"files", "output_model", "runtime", "schema_version", "toolchain_id"}, set(), "training_plan.toolchain")
	_expect(toolchain["schema_version"] == 1, "training_plan.toolchain.schema_version", "unsupported version")
	_identifier(toolchain["toolchain_id"], "training_plan.toolchain.toolchain_id")
	_exact_keys(toolchain["runtime"], {"name", "revision", "version"}, set(), "training_plan.toolchain.runtime")
	output_model = _exact_keys(toolchain["output_model"],
		{"attribution_file_relative_path", "attribution_sha256", "license_spdx"}, set(), "training_plan.toolchain.output_model")
	_safe_relative_path(output_model["attribution_file_relative_path"], "training_plan.toolchain.output_model.attribution_file_relative_path")
	_hash(output_model["attribution_sha256"], "training_plan.toolchain.output_model.attribution_sha256")
	_expect(isinstance(output_model["license_spdx"], str) and bool(output_model["license_spdx"].strip()),
		"training_plan.toolchain.output_model.license_spdx", "required")
	_expect(isinstance(toolchain["files"], list) and toolchain["files"], "training_plan.toolchain.files", "must not be empty")
	toolchain_paths = []
	attribution_items = []
	toolchain_roles = []
	for index, file_value in enumerate(toolchain["files"]):
		file_item = _exact_keys(file_value, {"relative_path", "role", "sha256", "size_bytes"}, set(),
			f"training_plan.toolchain.files[{index}]")
		toolchain_paths.append(_safe_relative_path(file_item["relative_path"], f"training_plan.toolchain.files[{index}].relative_path"))
		_identifier(file_item["role"], f"training_plan.toolchain.files[{index}].role")
		toolchain_roles.append(file_item["role"])
		_hash(file_item["sha256"], f"training_plan.toolchain.files[{index}].sha256")
		_expect(isinstance(file_item["size_bytes"], int) and not isinstance(file_item["size_bytes"], bool) and file_item["size_bytes"] > 0,
			f"training_plan.toolchain.files[{index}].size_bytes", "must be positive")
		if file_item["role"] == "attribution-notice":
			attribution_items.append(file_item)
	_expect(toolchain_paths == sorted(set(toolchain_paths)), "training_plan.toolchain.files", "paths must be sorted and unique")
	for required_role in ("trainer", "training-config", "attribution-notice"):
		_expect(toolchain_roles.count(required_role) == 1, "training_plan.toolchain.files", f"exactly one {required_role} role is required")
	_expect(len(attribution_items) == 1, "training_plan.toolchain.files", "exactly one attribution-notice is required")
	_expect(attribution_items[0]["relative_path"] == output_model["attribution_file_relative_path"]
		and attribution_items[0]["sha256"] == output_model["attribution_sha256"], "training_plan.toolchain.output_model",
		"attribution binding mismatch")
	_expect(isinstance(plan["training_jobs"], list) and len(plan["training_jobs"]) >= MINIMUM_SEED_COUNT,
		"training_plan.training_jobs", f"at least {MINIMUM_SEED_COUNT} jobs are required")
	ids: list[str] = []
	seeds: list[int] = []
	paths: list[str] = []
	for index, job_value in enumerate(plan["training_jobs"]):
		job = _exact_keys(job_value, {"candidate_id", "input_fingerprint_sha256", "output_manifest_relative_path", "seed"}, set(),
			f"training_plan.training_jobs[{index}]")
		seed_digest = hashlib.sha256(
			f"rnnoise-seed-v1\0{plan['seed_root']}\0{plan['input_fingerprint_sha256']}\0{index}".encode("utf-8")
		).digest()
		expected_seed = int.from_bytes(seed_digest[:8], "big")
		expected_id = f"rnnoise-{index + 1:02d}-{seed_digest.hex()[:12]}"
		candidate_id = _identifier(job["candidate_id"], f"training_plan.training_jobs[{index}].candidate_id")
		ids.append(candidate_id)
		_expect(candidate_id == expected_id, f"training_plan.training_jobs[{index}].candidate_id", "seed-derived ID mismatch")
		_hash(job["input_fingerprint_sha256"], f"training_plan.training_jobs[{index}].input_fingerprint_sha256")
		_expect(job["input_fingerprint_sha256"] == canonical_sha256({"inputs": inputs, "seed": expected_seed}),
			f"training_plan.training_jobs[{index}].input_fingerprint_sha256", "job fingerprint mismatch")
		relative_path = _safe_relative_path(job["output_manifest_relative_path"], f"training_plan.training_jobs[{index}].output_manifest_relative_path")
		_expect(relative_path == f"rnnoise/custom/{plan['campaign_id']}/{candidate_id}.weights_blob.bin",
			f"training_plan.training_jobs[{index}].output_manifest_relative_path", "output path mismatch")
		paths.append(relative_path)
		_expect(isinstance(job["seed"], int) and not isinstance(job["seed"], bool) and 0 <= job["seed"] < 2**64,
			f"training_plan.training_jobs[{index}].seed", "must be uint64")
		seeds.append(job["seed"])
		_expect(job["seed"] == expected_seed, f"training_plan.training_jobs[{index}].seed", "seed derivation mismatch")
	_expect(len(ids) == len(set(ids)), "training_plan.training_jobs", "candidate IDs must be unique")
	_expect(len(seeds) == len(set(seeds)), "training_plan.training_jobs", "seeds must be unique")
	_expect(len(paths) == len(set(paths)), "training_plan.training_jobs", "output paths must be unique")
	_expect(isinstance(plan["approved_training_sources"], list) and plan["approved_training_sources"],
		"training_plan.approved_training_sources", "must not be empty")
	source_ids = []
	source_kinds = []
	for index, source_value in enumerate(plan["approved_training_sources"]):
		source = _exact_keys(source_value, {"id", "integrity", "kind", "license", "roles", "training_status"}, set(),
			f"training_plan.approved_training_sources[{index}]")
		source_ids.append(_identifier(source["id"], f"training_plan.approved_training_sources[{index}].id"))
		_expect(source["kind"] in ("clean_speech", "environmental_noise", "environmental_noise_and_rir", "paired_clean_noisy_speech"),
			f"training_plan.approved_training_sources[{index}].kind", "unsupported source kind")
		source_kinds.append(source["kind"])
		_expect(source["training_status"] == "allowed_with_attribution",
			f"training_plan.approved_training_sources[{index}].training_status", "training is not approved")
		_expect(isinstance(source["roles"], list) and "training_candidate" in source["roles"],
			f"training_plan.approved_training_sources[{index}].roles", "training_candidate role required")
		_expect(source["roles"] == sorted(set(source["roles"])), f"training_plan.approved_training_sources[{index}].roles",
			"roles must be sorted and unique")
		license_info = _exact_keys(source["license"], {"evidence_url", "spdx", "status"}, {"notes"},
			f"training_plan.approved_training_sources[{index}].license")
		_expect(license_info["status"] == "verified", f"training_plan.approved_training_sources[{index}].license.status",
			"license must be verified")
	_expect(source_ids == sorted(set(source_ids)), "training_plan.approved_training_sources", "source IDs must be sorted and unique")
	_expect(any(kind in ("clean_speech", "paired_clean_noisy_speech") for kind in source_kinds),
		"training_plan.approved_training_sources", "approved training speech is missing")
	_expect(any(kind in ("environmental_noise", "environmental_noise_and_rir", "paired_clean_noisy_speech") for kind in source_kinds),
		"training_plan.approved_training_sources", "approved training noise is missing")
	return plan


def run_self_test() -> None:
	manifest = copy.deepcopy(load_json(Path(__file__).with_name("corpus-lock.json")))
	for source in manifest["sources"]:
		if source["id"] in ("mcgill-tsp-speech-v2-48k", "openslr28-rirs-noises"):
			source["training_status"] = "allowed_with_attribution"
			source["roles"] = sorted(set(source["roles"]) | {"training_candidate"})
	LOCK.validate_manifest(manifest)
	inventory = MIXTURE._self_test_inventory(manifest, "rnnoise-campaign-self-test")
	tuning_mixture = MIXTURE.generate_plan(manifest, inventory, "pr_smoke", "tuning", "rnnoise-campaign-self-test", 2, 1000)
	validation_mixture = MIXTURE.generate_plan(manifest, inventory, "pr_smoke", "validation", "rnnoise-campaign-self-test", 2, 1000)
	holdout_mixture = MIXTURE.generate_plan(manifest, inventory, "pr_smoke", "holdout", "rnnoise-campaign-self-test", 2, 1000)
	with tempfile.TemporaryDirectory() as temporary:
		root = Path(temporary)
		(root / "trainer.bin").write_bytes(b"pinned trainer")
		(root / "training.json").write_bytes(b'{"epochs":1}\n')
		(root / "ATTRIBUTION.txt").write_bytes(b"Self-test corpus attribution\n")
		files = []
		for role, relative in (("attribution-notice", "ATTRIBUTION.txt"), ("trainer", "trainer.bin"), ("training-config", "training.json")):
			path = root / relative
			files.append({"relative_path": relative, "role": role, "sha256": file_sha256(path), "size_bytes": path.stat().st_size})
		files.sort(key=lambda item: item["relative_path"])
		toolchain = {
			"files": files,
			"output_model": {
				"attribution_file_relative_path": "ATTRIBUTION.txt",
				"attribution_sha256": file_sha256(root / "ATTRIBUTION.txt"),
				"license_spdx": "LicenseRef-Mumble-RNNoise-SelfTest",
			},
			"runtime": {"name": "self-test", "revision": "locked", "version": "1"},
			"schema_version": 1,
			"toolchain_id": "rnnoise-self-test",
		}
		validate_toolchain(toolchain, root)
		hashes = {key: "1" * 64 for key in (
			"corpus_lock", "inventory", "tuning_mixture_plan", "validation_mixture_plan", "holdout_mixture_plan", "toolchain_manifest",
		)}
		plan = freeze_campaign(manifest, inventory, tuning_mixture, validation_mixture, holdout_mixture, toolchain, input_file_hashes=hashes,
			campaign_id="self-test", seed_root="stable-root", seed_count=5)
		_expect(len({job["seed"] for job in plan["training_jobs"]}) == 5, "self-test", "seeds are not unique")
		blocked = copy.deepcopy(manifest)
		for source in blocked["sources"]:
			if source["id"] == "openslr28-rirs-noises":
				source["training_status"] = "blocked_evaluation_only"
		try:
			freeze_campaign(blocked, inventory, tuning_mixture, validation_mixture, holdout_mixture, toolchain, input_file_hashes=hashes,
				campaign_id="self-test", seed_root="stable-root", seed_count=5)
		except CampaignError as error:
			_expect("training" in str(error), "self-test", "license failure did not fail closed")
		else:
			raise CampaignError("self-test: evaluation-only noise was accepted")
		clean_only = copy.deepcopy(tuning_mixture)
		for case in clean_only["cases"]:
			case["noise"] = None
			case["mix"]["snr_db"] = None
		try:
			MIXTURE.validate_plan(clean_only)
		except MIXTURE.PlanError as error:
			_expect("noise" in str(error), "self-test", "plan diversity did not explain the clean-only rejection")
		else:
			try:
				freeze_campaign(manifest, inventory, clean_only, validation_mixture, holdout_mixture, toolchain, input_file_hashes=hashes,
					campaign_id="self-test", seed_root="stable-root", seed_count=5)
			except CampaignError as error:
				_expect("noise" in str(error), "self-test", "missing-noise failure did not fail closed")
			else:
				raise CampaignError("self-test: clean-only training plan was accepted")


def _parser() -> argparse.ArgumentParser:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--corpus-lock", type=Path, default=Path(__file__).with_name("corpus-lock.json"))
	parser.add_argument("--inventory", type=Path)
	parser.add_argument("--tuning-mixture-plan", "--mixture-plan", dest="tuning_mixture_plan", type=Path)
	parser.add_argument("--validation-mixture-plan", type=Path)
	parser.add_argument("--holdout-mixture-plan", type=Path)
	parser.add_argument("--toolchain-manifest", type=Path)
	parser.add_argument("--toolchain-root", type=Path)
	parser.add_argument("--campaign-id")
	parser.add_argument("--seed-root")
	parser.add_argument("--seed-count", type=int, default=MINIMUM_SEED_COUNT)
	parser.add_argument("--output", type=Path)
	parser.add_argument("--self-test", action="store_true")
	return parser


def main(argv: Sequence[str] | None = None) -> int:
	args = _parser().parse_args(argv)
	try:
		if args.self_test:
			run_self_test()
			print("RNNoise training campaign self-test: ok")
			return 0
		for name in ("inventory", "tuning_mixture_plan", "validation_mixture_plan", "holdout_mixture_plan", "toolchain_manifest", "toolchain_root", "campaign_id", "seed_root", "output"):
			_expect(getattr(args, name) is not None, f"--{name.replace('_', '-')}", "is required")
		_expect(not os.path.lexists(args.output), "--output", "frozen plan output must not already exist")
		manifest = load_json(args.corpus_lock)
		inventory = load_json(args.inventory)
		tuning_mixture = load_json(args.tuning_mixture_plan)
		validation_mixture = load_json(args.validation_mixture_plan)
		holdout_mixture = load_json(args.holdout_mixture_plan)
		toolchain = load_json(args.toolchain_manifest)
		validate_toolchain(toolchain, args.toolchain_root)
		hashes = {
			"corpus_lock": file_sha256(args.corpus_lock),
			"inventory": file_sha256(args.inventory),
			"tuning_mixture_plan": file_sha256(args.tuning_mixture_plan),
			"validation_mixture_plan": file_sha256(args.validation_mixture_plan),
			"holdout_mixture_plan": file_sha256(args.holdout_mixture_plan),
			"toolchain_manifest": file_sha256(args.toolchain_manifest),
		}
		plan = freeze_campaign(manifest, inventory, tuning_mixture, validation_mixture, holdout_mixture, toolchain, input_file_hashes=hashes,
			campaign_id=args.campaign_id, seed_root=args.seed_root, seed_count=args.seed_count)
		_write_json_atomic(args.output, plan)
		print(f"RNNoise training campaign: frozen; jobs={len(plan['training_jobs'])}; sha256={file_sha256(args.output)}")
		return 0
	except (CampaignError, OSError, ValueError) as error:
		print(f"RNNoise training campaign: FAIL: {error}", file=sys.stderr)
		return 1


if __name__ == "__main__":
	sys.exit(main())
