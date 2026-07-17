#!/usr/bin/env python3
"""Generate a deterministic, split-safe 48 kHz mono audio-quality mixture plan.

This script plans transformations; it does not read, decode, or write audio. An
executor can consume the plan later while preserving the exact corpus, timing,
Opus, profile, and cold/warm-start choices recorded here.
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import importlib.util
import json
import math
import os
import re
import sys
from pathlib import Path, PurePosixPath
from typing import Any, Mapping, MutableMapping, Sequence


class PlanError(ValueError):
	"""Raised when an inventory or generated plan is unsafe or invalid."""


SUITE_CASES = { "pr_smoke": 30, "master_quality": 500, "nightly": 5000, "release": 30 }
SPLITS = ("tuning", "validation", "holdout")
PROFILES = ("Original", "Light", "Balanced", "Quality", "VoiceFocus")
CONTROL_DIMENSIONS = ("noise_reduction", "natural_clear")
# Plan ``controls`` are persisted public UI integers in [0, 100].  These are
# deliberately separate from the validated recipe coordinates below.  C++ maps
# UI to recipe space exactly once with:
#
#   minimum + ((ui * (maximum - minimum) + 50) / 100)
#
# using integer division.  Qualification targets the complete five-point recipe
# grid and stores the nearest canonical UI integer that round-trips exactly.
PROFILE_RECIPE_CONTROL_RANGES = {
	"Light": {"noise_reduction": (0, 100), "natural_clear": (0, 100)},
	"Balanced": {"noise_reduction": (20, 90), "natural_clear": (10, 90)},
	"Quality": {"noise_reduction": (25, 90), "natural_clear": (25, 100)},
	"VoiceFocus": {"noise_reduction": (70, 100), "natural_clear": (40, 100)},
}
PROFILE_RECIPE_CONTROL_GRID = {
	profile: {
		dimension: tuple(range(bounds[0], bounds[1] + 1, 5))
		for dimension, bounds in dimensions.items()
	}
	for profile, dimensions in PROFILE_RECIPE_CONTROL_RANGES.items()
}
CONTROL_SEMANTICS = {
	"plan_values": "persisted-ui-integers-0-100",
	"recipe_values": "profile-qualified-integer-grid-step-5",
	"mapping": "minimum+((ui*(maximum-minimum)+50)//100)",
	"inverse": "nearest-exact-round-trip-lower-tie-v1",
}
SNR_DB = (-5, 0, 5, 10, 20, None)
MICROPHONES = ("headset", "laptop", "usb", "phone")
GAIN_DB = (-6, -3, 0, 3, 6)
DISTANCE_CM = (10, 30, 60, 100)
RT60_MS = (0, 150, 300, 600)
BITRATES = (8000, 16000, 40000, 64000, 128000)
PACKET_FRAMES = (1, 2, 4)
TRANSMIT_MODES = ("Continuous", "PTT", "VAD")
NOISE_BEHAVIOR = {
	"fan": "stationary",
	"hvac": "stationary",
	"hum": "stationary",
	"keyboard": "transient",
	"mouse": "transient",
	"traffic": "dynamic",
	"rain": "dynamic",
	"wind": "dynamic",
	"handling": "transient",
	"babble": "dynamic",
	"music-tv": "dynamic",
	"competing-speech": "dynamic",
	"paired-noisy-speech": "dynamic",
}


def _load_lock_module() -> Any:
	path = Path(__file__).with_name("validate-corpus-lock.py")
	spec = importlib.util.spec_from_file_location("mumble_audio_corpus_lock_for_plan", path)
	if spec is None or spec.loader is None:
		raise PlanError(f"unable to load corpus-lock validator: {path}")
	module = importlib.util.module_from_spec(spec)
	spec.loader.exec_module(module)
	return module


LOCK = _load_lock_module()


def _load_inventory_module() -> Any:
	path = Path(__file__).with_name("corpus-inventory-v3.py")
	spec = importlib.util.spec_from_file_location("mumble_audio_corpus_inventory_v3_for_plan", path)
	if spec is None or spec.loader is None:
		raise PlanError(f"unable to load corpus inventory validator: {path}")
	module = importlib.util.module_from_spec(spec)
	spec.loader.exec_module(module)
	return module


INVENTORY = _load_inventory_module()


def _expect(condition: bool, path: str, message: str) -> None:
	if not condition:
		raise PlanError(f"{path}: {message}")


def _exact_keys(value: Mapping[str, Any], required: set[str], optional: set[str], path: str) -> None:
	missing = sorted(required - set(value))
	unknown = sorted(set(value) - required - optional)
	_expect(not missing, path, f"missing keys: {', '.join(missing)}")
	_expect(not unknown, path, f"unknown keys: {', '.join(unknown)}")


def _safe_path(value: Any, path: str) -> str:
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
				raise PlanError(f"duplicate JSON key: {key}")
			result[key] = value
		return result

	try:
		return json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=reject_duplicates)
	except (OSError, json.JSONDecodeError) as error:
		raise PlanError(f"unable to read {path}: {error}") from error


def _stable_int(seed: str, *parts: object) -> int:
	payload = "\0".join((seed, *(str(part) for part in parts))).encode("utf-8")
	return int.from_bytes(hashlib.sha256(payload).digest()[:8], "big")


def map_ui_control_to_recipe(profile: str, dimension: str, ui_value: int) -> int:
	"""Mirror C++ validatedControlsForProfile() using persisted UI coordinates."""
	_expect(profile in PROFILES, "control.profile", "unknown profile")
	_expect(dimension in CONTROL_DIMENSIONS, "control.dimension", "unknown dimension")
	_expect(
		isinstance(ui_value, int) and not isinstance(ui_value, bool) and 0 <= ui_value <= 100,
		"control.ui_value",
		"must be an integer from 0 to 100",
	)
	if profile == "Original":
		return 0
	minimum, maximum = PROFILE_RECIPE_CONTROL_RANGES[profile][dimension]
	return minimum + ((ui_value * (maximum - minimum) + 50) // 100)


def nearest_ui_control_for_recipe(profile: str, dimension: str, recipe_value: int) -> int:
	"""Choose the nearest canonical UI integer that maps to one recipe-grid value."""
	_expect(profile in PROFILE_RECIPE_CONTROL_GRID, "control.profile", "profile has no qualified recipe grid")
	_expect(dimension in CONTROL_DIMENSIONS, "control.dimension", "unknown dimension")
	_expect(
		recipe_value in PROFILE_RECIPE_CONTROL_GRID[profile][dimension],
		"control.recipe_value",
		"is outside the qualified five-point recipe grid",
	)
	minimum, maximum = PROFILE_RECIPE_CONTROL_RANGES[profile][dimension]
	candidates = [
		ui_value for ui_value in range(101)
		if map_ui_control_to_recipe(profile, dimension, ui_value) == recipe_value
	]
	_expect(bool(candidates), "control.recipe_value", "has no exact persisted-UI round trip")
	# Compare exact integer numerators rather than floats.  A lower UI value wins
	# the mathematically exact tie, making generation stable across Python builds.
	ideal_numerator = (recipe_value - minimum) * 100
	range_width = maximum - minimum
	return min(candidates, key=lambda ui_value: (abs(ui_value * range_width - ideal_numerator), ui_value))


def qualified_ui_control_values(profile: str, dimension: str) -> tuple[int, ...]:
	if profile == "Original":
		return (50,)
	return tuple(
		nearest_ui_control_for_recipe(profile, dimension, recipe_value)
		for recipe_value in PROFILE_RECIPE_CONTROL_GRID[profile][dimension]
	)


def validated_recipe_controls(profile: str, controls: Mapping[str, Any]) -> dict[str, int]:
	"""Return the exact C++ recipe coordinates expected from plan UI controls."""
	return {
		dimension: map_ui_control_to_recipe(profile, dimension, controls.get(dimension))
		for dimension in CONTROL_DIMENSIONS
	}


PROFILE_UI_CONTROL_VALUES = {
	profile: {
		dimension: qualified_ui_control_values(profile, dimension)
		for dimension in CONTROL_DIMENSIONS
	}
	for profile in PROFILES
}


def assigned_split(seed: str, kind: str, group_id: str) -> str:
	return INVENTORY.assigned_split(seed, kind, group_id)


def validate_inventory(
	value: Any, manifest: Mapping[str, Any], expected_lock_sha256: str, suite: str, seed: str
) -> list[Mapping[str, Any]]:
	_expect(value.get("corpus_lock_sha256") == expected_lock_sha256, "inventory.corpus_lock_sha256", "lock mismatch")
	try:
		items = INVENTORY.validate_inventory(value, manifest, require_release=True)
		INVENTORY.validate_diversity(items, suite, seed)
		return items
	except INVENTORY.InventoryError as error:
		raise PlanError(str(error)) from error


def _choice(values: Sequence[Any], seed: str, case_index: int, label: str) -> Any:
	return values[_stable_int(seed, "choice-v1", case_index, label) % len(values)]


def _evenly_spaced(values: Sequence[int], index: int, count: int) -> int:
	_expect(bool(values), "control grid", "must not be empty")
	_expect(count > 0 and 0 <= index < count, "control grid", "invalid subset index")
	if count == 1:
		return values[len(values) // 2]
	# Integer nearest-neighbour selection with deterministic lower ties.  The
	# first and final occurrences therefore always exercise both endpoints.
	position_numerator = index * (len(values) - 1)
	denominator = count - 1
	position = (2 * position_numerator + denominator - 1) // (2 * denominator)
	return values[position]


def _profile_for_case(suite: str, case_count: int, index: int) -> str:
	if suite == "release" and case_count == SUITE_CASES["release"]:
		return PROFILES[index // 6]
	return PROFILES[index % len(PROFILES)]


def _planned_ui_controls(
	suite: str, split: str, seed: str, profile: str, occurrence: int, occurrence_count: int,
) -> dict[str, int]:
	if profile == "Original":
		return {"noise_reduction": 50, "natural_clear": 50}

	# Release's locked severe comparator requires byte-identical scenes and
	# persisted UI controls for Quality and Voice Focus.  Select only canonical UI
	# representatives shared by both profiles; each profile may still map those
	# public values to its own separately qualified recipe range.
	if suite == "release" and occurrence_count == 6 and profile in ("Quality", "VoiceFocus"):
		controls = {}
		for dimension in CONTROL_DIMENSIONS:
			shared = tuple(sorted(
				set(PROFILE_UI_CONTROL_VALUES["Quality"][dimension])
				& set(PROFILE_UI_CONTROL_VALUES["VoiceFocus"][dimension])
			))
			_expect(len(shared) >= 2, f"release controls.{dimension}", "paired profiles have no endpoint-safe UI subset")
			controls[dimension] = _evenly_spaced(shared, occurrence, occurrence_count)
		return controls

	controls = {}
	full_grid = suite in ("master_quality", "nightly") and split in ("tuning", "validation")
	for dimension in CONTROL_DIMENSIONS:
		grid = PROFILE_RECIPE_CONTROL_GRID[profile][dimension]
		if full_grid:
			offset = _stable_int(seed, "recipe-grid-offset-v1", split, profile, dimension) % len(grid)
			recipe_value = grid[(occurrence + offset) % len(grid)]
		else:
			recipe_value = _evenly_spaced(grid, occurrence, occurrence_count)
		controls[dimension] = nearest_ui_control_for_recipe(profile, dimension, recipe_value)
	return controls


def _group_choice(values: Sequence[Mapping[str, Any]], seed: str, case_index: int, label: str) -> Mapping[str, Any]:
	"""Cycle independent groups before selecting a deterministic item within one."""
	grouped: dict[str, list[Mapping[str, Any]]] = {}
	for value in values:
		grouped.setdefault(str(value["group_id"]), []).append(value)
	groups = sorted(grouped)
	offset = _stable_int(seed, "group-order-v1", label) % len(groups)
	group_id = groups[(case_index + offset) % len(groups)]
	return _choice(sorted(grouped[group_id], key=lambda item: item["id"]), seed, case_index, f"{label}:{group_id}")


def _source_window(item: Mapping[str, Any], duration_ms: int, seed: str, case_index: int, label: str) -> dict[str, int]:
	needed = math.ceil(duration_ms * item["sample_rate_hz"] / 1000)
	_expect(item["duration_samples"] >= needed, item["id"], f"shorter than requested {duration_ms} ms")
	maximum_start = item["duration_samples"] - needed
	start = _stable_int(seed, "window-v1", case_index, label) % (maximum_start + 1)
	return { "start_sample": start, "length_samples": needed }


def generate_plan(
	manifest: Mapping[str, Any], inventory: Any, suite: str, split: str, seed: str, case_count: int, duration_ms: int
) -> Mapping[str, Any]:
	lock_sha = LOCK.canonical_manifest_sha256(manifest)
	items = validate_inventory(inventory, manifest, lock_sha, suite, seed)
	speech = sorted(
		(item for item in items if item["kind"] == "speech" and assigned_split(seed, "speech", item["group_id"]) == split),
		key=lambda item: item["id"],
	)
	noise = sorted(
		(item for item in items if item["kind"] == "noise" and assigned_split(seed, "noise", item["group_id"]) == split),
		key=lambda item: item["id"],
	)
	rirs = sorted(
		(item for item in items if item["kind"] == "rir" and assigned_split(seed, "rir", item["group_id"]) == split),
		key=lambda item: item["id"],
	)
	microphone_responses = sorted(
		(
			item for item in items
			if item["kind"] == "microphone_response"
			and assigned_split(seed, "microphone_response", item["group_id"]) == split
		),
		key=lambda item: item["id"],
	)
	_expect(speech, "inventory", f"no speech groups assigned to {split}")
	_expect(noise, "inventory", f"no noise groups assigned to {split}")
	_expect(rirs, "inventory", f"no RIR groups assigned to {split}")
	_expect(microphone_responses, "inventory", f"no microphone-response groups assigned to {split}")
	_expect(case_count > 0, "case_count", "must be positive")
	_expect(duration_ms >= 1000 and duration_ms % 10 == 0, "duration_ms", "must be >=1000 and align to 10 ms")

	profile_schedule = [_profile_for_case(suite, case_count, index) for index in range(case_count)]
	profile_totals = {profile: profile_schedule.count(profile) for profile in PROFILES}
	profile_occurrences = {profile: 0 for profile in PROFILES}
	cases = []
	for index in range(case_count):
		comparison_scene_id = None
		if suite == "release" and case_count == SUITE_CASES["release"]:
			_expect(
				{"en-US", "sv-SE"}.issubset({item["language"] for item in speech}),
				"inventory", "release matrix requires en-US and sv-SE speech",
			)
			profile = profile_schedule[index]
			language = ("en-US", "sv-SE")[(index // 3) % 2]
			variant = index % 3
			scene_index = index - 6 if profile == "VoiceFocus" else index
			if profile in ("Quality", "VoiceFocus"):
				comparison_scene_id = f"{suite}-{split}-quality-voicefocus-{language.lower()}-{variant}"
			language_speech = [item for item in speech if item["language"] == language]
			clean = _group_choice(language_speech, seed, scene_index, f"speech:{language}")
			if profile in ("Quality", "VoiceFocus"):
				snr = None if variant == 0 else (-5, 0)[variant - 1]
			else:
				snr = None if variant == 0 else SNR_DB[((scene_index // 3) * 2 + variant - 1) % (len(SNR_DB) - 1)]
			noise_choice_index = (scene_index // 3) * 2 + max(0, variant - 1)
			preroll_ms = 0 if variant != 1 else 300
		else:
			profile = profile_schedule[index]
			scene_index = index
			clean = _group_choice(speech, seed, scene_index, "speech")
			snr = SNR_DB[index % len(SNR_DB)]
			noise_choice_index = index
			preroll_ms = 0 if index % 2 == 0 else 300
		rir = _group_choice(rirs, seed, scene_index, "rir")
		microphone_response = _group_choice(microphone_responses, seed, scene_index, "microphone-response")
		selected_noise = None if snr is None else _group_choice(noise, seed, noise_choice_index, "noise")
		profile_occurrence = profile_occurrences[profile]
		profile_occurrences[profile] += 1
		case = {
			"case_id": f"{suite}-{split}-{index + 1:05d}",
			"profile": profile,
			# Persist exactly what the client settings/UI stores.  The validator and
			# benchmark independently derive the profile-qualified recipe values.
			"controls": _planned_ui_controls(
				suite, split, seed, profile, profile_occurrence, profile_totals[profile]
			),
			"speech": {
				"item_id": clean["id"],
				"source_id": clean["source_id"],
				"relative_path": clean["relative_path"],
				"language": clean["language"],
				"speaker_id": clean["speaker_id"],
				"transcript_sha256": clean["transcript"]["sha256"],
				"group_id": clean["group_id"],
				"input_sample_rate_hz": clean["sample_rate_hz"],
				"input_channels": clean["channels"],
				"sha256": clean["sha256"],
				"size_bytes": clean["size_bytes"],
				"source_artifact_sha256": clean["source_artifact_sha256"],
				"window": _source_window(clean, duration_ms, seed, scene_index, "speech"),
			},
			"noise": None,
			"mix": {
				"snr_db": snr,
				"speech_gain_db": _choice(GAIN_DB, seed, scene_index, "gain"),
				"mild_clipping": (_stable_int(seed, "clip-v1", scene_index) % 10) == 0,
				"distance_cm": _choice(DISTANCE_CM, seed, scene_index, "distance"),
				"rir": {
					"item_id": rir["id"], "group_id": rir["group_id"], "relative_path": rir["relative_path"],
					"input_sample_rate_hz": rir["sample_rate_hz"], "input_channels": rir["channels"],
					"duration_samples": rir["duration_samples"], "sha256": rir["sha256"],
					"size_bytes": rir["size_bytes"], "source_id": rir["source_id"],
					"source_artifact_sha256": rir["source_artifact_sha256"], **rir["rir"],
				},
				"microphone_response": {
					"item_id": microphone_response["id"], "group_id": microphone_response["group_id"],
					"relative_path": microphone_response["relative_path"],
					"input_sample_rate_hz": microphone_response["sample_rate_hz"],
					"input_channels": microphone_response["channels"],
					"duration_samples": microphone_response["duration_samples"],
					"sha256": microphone_response["sha256"], "size_bytes": microphone_response["size_bytes"],
					"source_id": microphone_response["source_id"],
					"source_artifact_sha256": microphone_response["source_artifact_sha256"],
					**microphone_response["microphone_response"],
				},
			},
			"transport": {
				"opus_bitrate_bps": BITRATES[scene_index % len(BITRATES)],
				"frames_per_packet": PACKET_FRAMES[scene_index % len(PACKET_FRAMES)],
				"transmit_mode": TRANSMIT_MODES[scene_index % len(TRANSMIT_MODES)],
				"receiver_cleanup": False,
			},
			"startup": { "preroll_ms": preroll_ms },
		}
		if comparison_scene_id is not None:
			case["comparison_scene_id"] = comparison_scene_id
		if selected_noise is not None:
			case["noise"] = {
				"item_id": selected_noise["id"],
				"source_id": selected_noise["source_id"],
				"relative_path": selected_noise["relative_path"],
				"class": selected_noise["noise_class"],
				"behavior": NOISE_BEHAVIOR[selected_noise["noise_class"]],
				"group_id": selected_noise["group_id"],
				"input_sample_rate_hz": selected_noise["sample_rate_hz"],
				"input_channels": selected_noise["channels"],
				"sha256": selected_noise["sha256"],
				"size_bytes": selected_noise["size_bytes"],
				"source_artifact_sha256": selected_noise["source_artifact_sha256"],
				"window": _source_window(selected_noise, duration_ms, seed, scene_index, "noise"),
				"loop_if_needed": False,
			}
		cases.append(case)

	plan = {
		"schema_version": 4,
		"generator": "mumble-audio-mixture-plan-v4",
		"control_semantics": CONTROL_SEMANTICS,
		"corpus_lock_sha256": lock_sha,
		"corpus_inventory_sha256": INVENTORY.canonical_sha256(inventory),
		"seed": seed,
		"split": split,
		"split_algorithm": "sha256-v1 by kind/group: tuning=0..59, validation=60..79, holdout=80..99",
		"suite": suite,
		"format": { "sample_rate_hz": 48000, "channels": 1, "frame_samples": 480, "duration_ms": duration_ms },
		"timeline_alignment": "fixed",
		"cases": cases,
	}
	validate_plan(plan)
	return plan


def validate_plan(value: Any) -> Mapping[str, Any]:
	_expect(isinstance(value, dict), "plan", "expected an object")
	_exact_keys(
		value,
		{
			"cases", "control_semantics", "corpus_inventory_sha256", "corpus_lock_sha256", "format", "generator",
			"schema_version", "seed", "split", "split_algorithm", "suite", "timeline_alignment",
		},
		set(),
		"plan",
	)
	_expect(value["schema_version"] == 4, "plan.schema_version", "unsupported version")
	_expect(value["generator"] == "mumble-audio-mixture-plan-v4", "plan.generator", "unknown generator")
	_expect(value["control_semantics"] == CONTROL_SEMANTICS, "plan.control_semantics", "unknown UI-to-recipe mapping")
	_expect(value["split"] in SPLITS, "plan.split", "unknown split")
	_expect(value["suite"] in SUITE_CASES, "plan.suite", "unknown suite")
	_expect(value["timeline_alignment"] == "fixed", "plan.timeline_alignment", "release plans require fixed alignment")
	_expect(bool(re.fullmatch(r"[0-9a-f]{64}", value["corpus_lock_sha256"])), "plan.corpus_lock_sha256", "invalid hash")
	_expect(bool(re.fullmatch(r"[0-9a-f]{64}", value["corpus_inventory_sha256"])), "plan.corpus_inventory_sha256", "invalid hash")
	_expect(value["format"]["sample_rate_hz"] == 48000, "plan.format.sample_rate_hz", "must be 48000")
	_expect(value["format"]["channels"] == 1, "plan.format.channels", "must be mono")
	_expect(value["format"]["frame_samples"] == 480, "plan.format.frame_samples", "must be 10 ms")
	_expect(isinstance(value["cases"], list) and value["cases"], "plan.cases", "must not be empty")
	case_ids = []
	startup_modes = set()
	recipe_control_coverage = {
		profile: {dimension: set() for dimension in CONTROL_DIMENSIONS}
		for profile in PROFILE_RECIPE_CONTROL_GRID
	}
	for index, case in enumerate(value["cases"]):
		path = f"plan.cases[{index}]"
		for label, source in (
			("speech", case["speech"]),
			("noise", case["noise"]),
			("rir", case["mix"]["rir"]),
			("microphone_response", case["mix"]["microphone_response"]),
		):
			if source is None:
				continue
			_expect(
				isinstance(source.get("sha256"), str) and bool(re.fullmatch(r"[0-9a-f]{64}", source["sha256"])),
				f"{path}.{label}.sha256",
				"invalid per-file hash",
			)
			_expect(
				isinstance(source.get("source_artifact_sha256"), str)
				and bool(re.fullmatch(r"[0-9a-f]{64}", source["source_artifact_sha256"])),
				f"{path}.{label}.source_artifact_sha256",
				"invalid source artifact hash",
			)
			_expect(
				isinstance(source.get("size_bytes"), int)
				and not isinstance(source["size_bytes"], bool)
				and source["size_bytes"] > 0,
				f"{path}.{label}.size_bytes",
				"must be a positive integer",
			)
		_expect(case["profile"] in PROFILES, f"{path}.profile", "unknown profile")
		_expect(isinstance(case.get("controls"), dict), f"{path}.controls", "must be an object")
		_exact_keys(case["controls"], set(CONTROL_DIMENSIONS), set(), f"{path}.controls")
		for control_name in CONTROL_DIMENSIONS:
			control_value = case["controls"].get(control_name)
			_expect(
				isinstance(control_value, int) and not isinstance(control_value, bool) and 0 <= control_value <= 100,
				f"{path}.controls.{control_name}",
				"must be a persisted UI integer from 0 to 100",
			)
			_expect(
				control_value in PROFILE_UI_CONTROL_VALUES[case["profile"]][control_name],
				f"{path}.controls.{control_name}",
				"is not the canonical UI representative of a qualified recipe-grid value",
			)
		if case["profile"] in recipe_control_coverage:
			validated = validated_recipe_controls(case["profile"], case["controls"])
			for control_name in CONTROL_DIMENSIONS:
				recipe_control_coverage[case["profile"]][control_name].add(validated[control_name])
		_expect(case["transport"]["receiver_cleanup"] is False, f"{path}.transport.receiver_cleanup", "must be false")
		_expect(case["transport"]["opus_bitrate_bps"] in BITRATES, f"{path}.transport.opus_bitrate_bps", "unsupported")
		_expect(case["transport"]["frames_per_packet"] in PACKET_FRAMES, f"{path}.transport.frames_per_packet", "unsupported")
		_expect(case["transport"]["transmit_mode"] in TRANSMIT_MODES, f"{path}.transport.transmit_mode", "unsupported")
		_expect(case["startup"]["preroll_ms"] in (0, 300), f"{path}.startup.preroll_ms", "must be 0 or 300")
		startup_modes.add(case["startup"]["preroll_ms"])
		if case["noise"] is None:
			_expect(case["mix"]["snr_db"] is None, f"{path}.mix.snr_db", "clean case must use null")
		else:
			_expect(case["mix"]["snr_db"] in SNR_DB[:-1], f"{path}.mix.snr_db", "unsupported SNR")
		case_ids.append(case["case_id"])
	_expect(case_ids == sorted(set(case_ids)), "plan.cases", "case ids must be unique and sorted")
	_expect(startup_modes == { 0, 300 } or len(value["cases"]) == 1, "plan.cases", "must cover cold and warm start")
	full_grid_required = value["suite"] in ("master_quality", "nightly") and value["split"] in ("tuning", "validation")
	endpoint_coverage_required = value["suite"] in ("pr_smoke", "release")
	for profile, dimensions in PROFILE_RECIPE_CONTROL_GRID.items():
		for control_name, expected_grid in dimensions.items():
			actual = recipe_control_coverage[profile][control_name]
			label = f"plan.control_coverage.{profile}.{control_name}"
			if full_grid_required:
				missing = sorted(set(expected_grid) - actual)
				_expect(not missing, label, f"missing qualified recipe-grid values: {missing}")
			elif endpoint_coverage_required:
				missing = sorted({expected_grid[0], expected_grid[-1]} - actual)
				_expect(not missing, label, f"missing qualified recipe-grid endpoints: {missing}")
	requirements = INVENTORY.DIVERSITY_REQUIREMENTS[value["suite"]]
	diversity = {
		"speaker_groups": len({case["speech"]["group_id"] for case in value["cases"]}),
		"languages": len({case["speech"]["language"] for case in value["cases"]}),
		"noise_groups": len({case["noise"]["group_id"] for case in value["cases"] if case["noise"] is not None}),
		"noise_classes": len({case["noise"]["class"] for case in value["cases"] if case["noise"] is not None}),
		"rir_groups": len({case["mix"]["rir"]["group_id"] for case in value["cases"]}),
		"device_groups": len({case["mix"]["microphone_response"]["group_id"] for case in value["cases"]}),
		"device_families": len({case["mix"]["microphone_response"]["device_family"] for case in value["cases"]}),
	}
	for key, minimum in requirements.items():
		_expect(diversity[key] >= minimum, f"plan.diversity.{key}", f"requires at least {minimum}; found {diversity[key]}")
	if value["suite"] == "release" and len(value["cases"]) == SUITE_CASES["release"]:
		for profile in PROFILES:
			for language in ("en-US", "sv-SE"):
				rows = [
					case for case in value["cases"]
					if case["profile"] == profile and case["speech"]["language"] == language
				]
				label = f"plan.release_matrix.{profile}.{language}"
				_expect(len(rows) == 3, label, f"requires exactly three cases; found {len(rows)}")
				_expect(sum(case["noise"] is None for case in rows) == 1, label, "requires exactly one clean case")
				_expect(sum(case["noise"] is not None for case in rows) == 2, label, "requires exactly two noisy cases")
				_expect({case["startup"]["preroll_ms"] for case in rows} == {0, 300}, label, "requires cold and warm start")
				transport = {
					(
						case["transport"]["opus_bitrate_bps"], case["transport"]["frames_per_packet"],
						case["transport"]["transmit_mode"],
					)
					for case in rows
				}
				_expect(len(transport) == 3, label, "requires three distinct transport recipes")
		paired: dict[str, list[Mapping[str, Any]]] = {}
		for case in value["cases"]:
			comparison_scene_id = case.get("comparison_scene_id")
			if comparison_scene_id is not None:
				_expect(isinstance(comparison_scene_id, str) and bool(comparison_scene_id), "plan.comparison_scene_id", "must be non-empty")
				paired.setdefault(comparison_scene_id, []).append(case)
		_expect(len(paired) == 6, "plan.quality_voicefocus_pairs", f"requires six paired scenes; found {len(paired)}")
		for comparison_scene_id, rows in paired.items():
			label = f"plan.quality_voicefocus_pairs.{comparison_scene_id}"
			_expect({row["profile"] for row in rows} == {"Quality", "VoiceFocus"}, label, "requires one Quality and one VoiceFocus case")
			_expect(len(rows) == 2, label, f"requires exactly two cases; found {len(rows)}")
			normalized = []
			for row in rows:
				copy_row = copy.deepcopy(row)
				copy_row.pop("case_id")
				copy_row.pop("profile")
				copy_row.pop("comparison_scene_id")
				normalized.append(copy_row)
			_expect(normalized[0] == normalized[1], label, "paired profiles do not share an identical rendered scene and transport")
			if rows[0]["noise"] is not None:
				_expect(rows[0]["mix"]["snr_db"] in (-5, 0), label, "paired noisy comparator must be severe")
	return value


def canonical_sha256(value: Mapping[str, Any]) -> str:
	encoded = json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")
	return hashlib.sha256(encoded).hexdigest()


def _write_json_atomic(path: Path, value: Mapping[str, Any]) -> None:
	path.parent.mkdir(parents=True, exist_ok=True)
	temporary = path.with_name(f".{path.name}.{os.getpid()}.tmp")
	temporary.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")
	os.replace(temporary, path)


def _self_test_inventory(manifest: Mapping[str, Any], seed: str) -> Mapping[str, Any]:
	lock_sha = LOCK.canonical_manifest_sha256(manifest)
	sources = {source["id"]: source for source in manifest["sources"]}
	items = []
	for kind in INVENTORY.KINDS:
		for index in range(80):
			if kind == "microphone_response":
				source_id = INVENTORY.MODELED_RESPONSE_SOURCE_ID
				artifact_sha256 = INVENTORY.file_sha256(INVENTORY.MODELED_RESPONSE_DEFINITION)
			else:
				source_id = "openslr28-rirs-noises" if kind in ("noise", "rir") else "mcgill-tsp-speech-v2-48k"
				artifact_sha256 = sources[source_id]["integrity"]["digest"]
			file_sha256 = hashlib.sha256(f"{kind}-{index:03d}".encode("utf-8")).hexdigest()
			item = {
				"id": f"{kind}-{index:03d}",
				"kind": kind,
				"source_id": source_id,
				"relative_path": f"extracted/{kind}-{index:03d}.wav",
				"group_id": f"{kind}-group-{index:03d}",
				"sample_rate_hz": 16000,
				"channels": 1,
				"duration_samples": 16000 * 12,
				"sha256": file_sha256,
				"size_bytes": 16000 * 12 * 2 + 44,
				"source_artifact_sha256": artifact_sha256,
				"provenance": {
					"derivation": "synthesized" if kind == "microphone_response" else "extracted", "parent_sha256": artifact_sha256,
					"parameters_sha256": hashlib.sha256(b"self-test-parameters-v3").hexdigest(),
					"source_path": f"archive/{kind}-{index:03d}.wav", "tool": "self-test", "tool_version": "3",
				},
			}
			if kind == "speech":
				item["language"] = "sv-SE" if index % 2 else "en-US"
				item["speaker_id"] = f"speaker-{index:03d}"
				item["transcript"] = {
					"status": "verified", "relative_path": f"transcripts/{index:03d}.txt",
					"sha256": hashlib.sha256(f"transcript-{index:03d}".encode("utf-8")).hexdigest(),
					"size_bytes": 20, "normalization": "exact-utf8",
				}
			elif kind == "noise":
				item["noise_class"] = tuple(NOISE_BEHAVIOR)[index % len(NOISE_BEHAVIOR)]
			elif kind == "rir":
				item["rir"] = {
					"rir_kind": "measured", "room_id": f"room-{index:03d}", "rt60_ms": RT60_MS[index % len(RT60_MS)],
					"source_position_id": f"source-{index:03d}", "receiver_position_id": f"receiver-{index:03d}",
				}
			else:
				item["microphone_response"] = {
					"response_kind": "modeled", "device_family": MICROPHONES[index % len(MICROPHONES)],
					"device_id": f"device-{index:03d}", "calibration_id": f"calibration-{index:03d}",
				}
			items.append(item)
	return {
		"schema_version": 3, "inventory_id": "mixture-plan-self-test", "eligibility": "release",
		"corpus_lock_sha256": lock_sha,
		"provenance": {
			"generator": "self-test", "generator_version": "3",
			"generated_from_state_sha256": hashlib.sha256(b"state").hexdigest(),
			"transformation_manifest_sha256": hashlib.sha256(b"transforms").hexdigest(),
		},
		"items": sorted(items, key=lambda item: item["id"]),
	}


def run_self_test() -> None:
	manifest = LOCK.load_validated_manifest(Path(__file__).with_name("corpus-lock.json"))
	seed = "mumble-plan-self-test"
	inventory = _self_test_inventory(manifest, seed)
	bad_file_hash = copy.deepcopy(inventory)
	bad_file_hash["items"][0]["sha256"] = "A" * 64
	try:
		validate_inventory(bad_file_hash, manifest, LOCK.canonical_manifest_sha256(manifest), "pr_smoke", seed)
	except PlanError:
		pass
	else:
		raise AssertionError("inventory accepted a non-canonical per-file SHA-256")
	bad_artifact_hash = copy.deepcopy(inventory)
	bad_artifact_hash["items"][0]["source_artifact_sha256"] = "0" * 64
	try:
		validate_inventory(bad_artifact_hash, manifest, LOCK.canonical_manifest_sha256(manifest), "pr_smoke", seed)
	except PlanError:
		pass
	else:
		raise AssertionError("inventory accepted a file bound to the wrong source artifact")
	one_noise_class = copy.deepcopy(inventory)
	for item in one_noise_class["items"]:
		if item["kind"] == "noise":
			item["noise_class"] = "fan"
	try:
		validate_inventory(one_noise_class, manifest, LOCK.canonical_manifest_sha256(manifest), "release", seed)
	except PlanError as error:
		if "noise_classes" not in str(error):
			raise
	else:
		raise AssertionError("release inventory accepted a single noise class")
	first = generate_plan(manifest, inventory, "pr_smoke", "validation", seed, 30, 6000)
	second = generate_plan(manifest, inventory, "pr_smoke", "validation", seed, 30, 6000)
	if canonical_sha256(first) != canonical_sha256(second):
		raise AssertionError("identical inputs did not produce an identical plan")
	if { case["startup"]["preroll_ms"] for case in first["cases"] } != { 0, 300 }:
		raise AssertionError("cold/warm coverage is missing")
	for profile, dimensions in PROFILE_RECIPE_CONTROL_GRID.items():
		for dimension, grid in dimensions.items():
			for recipe_value, ui_value in zip(grid, PROFILE_UI_CONTROL_VALUES[profile][dimension]):
				if map_ui_control_to_recipe(profile, dimension, ui_value) != recipe_value:
					raise AssertionError(f"{profile}/{dimension} UI inverse did not round-trip {recipe_value}")
	plans = {
		split: generate_plan(manifest, inventory, "release", split, seed, 30, 6000)
		for split in SPLITS
	}
	bad_release_matrix = copy.deepcopy(plans["validation"])
	bad_release_matrix["cases"][0]["speech"]["language"] = "sv-SE"
	try:
		validate_plan(bad_release_matrix)
	except PlanError as error:
		if "release_matrix" not in str(error):
			raise
	else:
		raise AssertionError("release plan accepted incomplete profile-by-language coverage")
	bad_comparator = copy.deepcopy(plans["validation"])
	voice_case = next(case for case in bad_comparator["cases"] if case["profile"] == "VoiceFocus")
	voice_case["mix"]["distance_cm"] = 999
	try:
		validate_plan(bad_comparator)
	except PlanError as error:
		if "quality_voicefocus_pairs" not in str(error):
			raise
	else:
		raise AssertionError("release plan accepted mismatched Quality/VoiceFocus comparator scenes")
	for profile in ("Quality", "VoiceFocus"):
		bad_controls = copy.deepcopy(first)
		profile_case = next(case for case in bad_controls["cases"] if case["profile"] == profile)
		# These are legacy *recipe* values that the schema-v3 generator wrote into
		# persisted UI fields and the client then mapped a second time.
		profile_case["controls"]["noise_reduction"] = 70
		try:
			validate_plan(bad_controls)
		except PlanError as error:
			if "canonical UI representative" not in str(error):
				raise
		else:
			raise AssertionError(f"plan accepted legacy double-mapped {profile} controls")

	missing_endpoint = copy.deepcopy(first)
	light_midpoint = nearest_ui_control_for_recipe("Light", "noise_reduction", 50)
	for case in missing_endpoint["cases"]:
		if case["profile"] == "Light" and map_ui_control_to_recipe(
			"Light", "noise_reduction", case["controls"]["noise_reduction"]
		) == 0:
			case["controls"]["noise_reduction"] = light_midpoint
	try:
		validate_plan(missing_endpoint)
	except PlanError as error:
		if "control_coverage.Light.noise_reduction" not in str(error) or "endpoints" not in str(error):
			raise
	else:
		raise AssertionError("PR plan accepted missing qualified recipe-grid endpoint coverage")

	master = generate_plan(manifest, inventory, "master_quality", "validation", seed, 500, 6000)
	missing_grid = copy.deepcopy(master)
	quality_grid = PROFILE_RECIPE_CONTROL_GRID["Quality"]["natural_clear"]
	removed_value = quality_grid[len(quality_grid) // 2]
	replacement_ui = nearest_ui_control_for_recipe("Quality", "natural_clear", quality_grid[0])
	for case in missing_grid["cases"]:
		if case["profile"] == "Quality" and map_ui_control_to_recipe(
			"Quality", "natural_clear", case["controls"]["natural_clear"]
		) == removed_value:
			case["controls"]["natural_clear"] = replacement_ui
	try:
		validate_plan(missing_grid)
	except PlanError as error:
		if "control_coverage.Quality.natural_clear" not in str(error) or "missing qualified recipe-grid values" not in str(error):
			raise
	else:
		raise AssertionError("master plan accepted incomplete qualified recipe-grid coverage")
	room_ids = {
		split: { case["mix"]["rir"]["item_id"] for case in plan["cases"] }
		for split, plan in plans.items()
	}
	microphone_ids = {
		split: { case["mix"]["microphone_response"]["item_id"] for case in plan["cases"] }
		for split, plan in plans.items()
	}
	for left_index, left in enumerate(SPLITS):
		for right in SPLITS[left_index + 1 :]:
			if room_ids[left] & room_ids[right] or microphone_ids[left] & microphone_ids[right]:
				raise AssertionError("room or microphone response leaked between splits")
	groups = { split: set() for split in SPLITS }
	for item in inventory["items"]:
		groups[assigned_split(seed, item["kind"], item["group_id"])].add((item["kind"], item["group_id"]))
	if any(groups[left] & groups[right] for left in SPLITS for right in SPLITS if left < right):
		raise AssertionError("split group leakage detected")


def main(argv: Sequence[str] | None = None) -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--manifest", type=Path, default=Path(__file__).with_name("corpus-lock.json"))
	parser.add_argument("--inventory", type=Path)
	parser.add_argument("--output", type=Path)
	parser.add_argument("--suite", choices=tuple(SUITE_CASES), default="pr_smoke")
	parser.add_argument("--split", choices=SPLITS, default="validation")
	parser.add_argument("--seed", default="mumble-input-enhancement-v1")
	parser.add_argument("--cases", type=int)
	parser.add_argument("--duration-ms", type=int, default=6000)
	parser.add_argument("--validate-plan", type=Path)
	parser.add_argument("--self-test", action="store_true")
	args = parser.parse_args(argv)
	try:
		if args.self_test:
			run_self_test()
			print("mixture-plan generator self-test: ok")
			if args.inventory is None and args.validate_plan is None:
				return 0
		if args.validate_plan is not None:
			plan = validate_plan(_load_json(args.validate_plan))
			print(f"mixture plan: ok; cases={len(plan['cases'])}; sha256={canonical_sha256(plan)}")
			return 0
		if args.inventory is None or args.output is None:
			raise PlanError("--inventory and --output are required to generate a plan")
		manifest = LOCK.load_validated_manifest(args.manifest)
		case_count = args.cases if args.cases is not None else SUITE_CASES[args.suite]
		plan = generate_plan(
			manifest, _load_json(args.inventory), args.suite, args.split, args.seed, case_count, args.duration_ms
		)
		_write_json_atomic(args.output, plan)
		digest = canonical_sha256(plan)
		args.output.with_suffix(args.output.suffix + ".sha256").write_text(f"{digest}  {args.output.name}\n", encoding="ascii")
		print(f"mixture plan: wrote {len(plan['cases'])} cases; sha256={digest}; output={args.output}")
		return 0
	except (PlanError, LOCK.ValidationError, AssertionError) as error:
		print(f"mixture plan: error: {error}", file=sys.stderr)
		return 1


if __name__ == "__main__":
	sys.exit(main())
