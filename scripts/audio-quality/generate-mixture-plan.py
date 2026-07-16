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


def assigned_split(seed: str, kind: str, group_id: str) -> str:
	bucket = _stable_int(seed, "split-v1", kind, group_id) % 100
	return "tuning" if bucket < 60 else "validation" if bucket < 80 else "holdout"


def validate_inventory(
	value: Any, manifest: Mapping[str, Any], expected_lock_sha256: str
) -> list[Mapping[str, Any]]:
	_expect(value.get("corpus_lock_sha256") == expected_lock_sha256, "inventory.corpus_lock_sha256", "lock mismatch")
	try:
		return INVENTORY.validate_inventory(value, manifest, require_release=True)
	except INVENTORY.InventoryError as error:
		raise PlanError(str(error)) from error


def _choice(values: Sequence[Any], seed: str, case_index: int, label: str) -> Any:
	return values[_stable_int(seed, "choice-v1", case_index, label) % len(values)]


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
	items = validate_inventory(inventory, manifest, lock_sha)
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

	cases = []
	for index in range(case_count):
		clean = _choice(speech, seed, index, "speech")
		rir = _choice(rirs, seed, index, "rir")
		microphone_response = _choice(microphone_responses, seed, index, "microphone-response")
		snr = SNR_DB[index % len(SNR_DB)]
		selected_noise = None if snr is None else _choice(noise, seed, index, "noise")
		case = {
			"case_id": f"{suite}-{split}-{index + 1:05d}",
			"profile": PROFILES[index % len(PROFILES)],
			"controls": {
				"noise_reduction": _choice((20, 40, 60, 80), seed, index, "reduction"),
				"natural_clear": _choice((20, 40, 60, 80), seed, index, "character"),
			},
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
				"window": _source_window(clean, duration_ms, seed, index, "speech"),
			},
			"noise": None,
			"mix": {
				"snr_db": snr,
				"speech_gain_db": _choice(GAIN_DB, seed, index, "gain"),
				"mild_clipping": (_stable_int(seed, "clip-v1", index) % 10) == 0,
				"distance_cm": _choice(DISTANCE_CM, seed, index, "distance"),
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
				"opus_bitrate_bps": BITRATES[index % len(BITRATES)],
				"frames_per_packet": PACKET_FRAMES[index % len(PACKET_FRAMES)],
				"transmit_mode": TRANSMIT_MODES[index % len(TRANSMIT_MODES)],
				"receiver_cleanup": False,
			},
			"startup": { "preroll_ms": 0 if index % 2 == 0 else 300 },
		}
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
				"window": _source_window(selected_noise, duration_ms, seed, index, "noise"),
				"loop_if_needed": False,
			}
		cases.append(case)

	plan = {
		"schema_version": 3,
		"generator": "mumble-audio-mixture-plan-v3",
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
			"cases", "corpus_inventory_sha256", "corpus_lock_sha256", "format", "generator", "schema_version", "seed", "split",
			"split_algorithm", "suite", "timeline_alignment",
		},
		set(),
		"plan",
	)
	_expect(value["schema_version"] == 3, "plan.schema_version", "unsupported version")
	_expect(value["generator"] == "mumble-audio-mixture-plan-v3", "plan.generator", "unknown generator")
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
		_expect("natural_clear" in case["controls"] and "natural_crisp" not in case["controls"], f"{path}.controls", "must use natural_clear")
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
		validate_inventory(bad_file_hash, manifest, LOCK.canonical_manifest_sha256(manifest))
	except PlanError:
		pass
	else:
		raise AssertionError("inventory accepted a non-canonical per-file SHA-256")
	bad_artifact_hash = copy.deepcopy(inventory)
	bad_artifact_hash["items"][0]["source_artifact_sha256"] = "0" * 64
	try:
		validate_inventory(bad_artifact_hash, manifest, LOCK.canonical_manifest_sha256(manifest))
	except PlanError:
		pass
	else:
		raise AssertionError("inventory accepted a file bound to the wrong source artifact")
	first = generate_plan(manifest, inventory, "pr_smoke", "validation", seed, 30, 6000)
	second = generate_plan(manifest, inventory, "pr_smoke", "validation", seed, 30, 6000)
	if canonical_sha256(first) != canonical_sha256(second):
		raise AssertionError("identical inputs did not produce an identical plan")
	if { case["startup"]["preroll_ms"] for case in first["cases"] } != { 0, 300 }:
		raise AssertionError("cold/warm coverage is missing")
	plans = {
		split: generate_plan(manifest, inventory, "release", split, seed, 30, 6000)
		for split in SPLITS
	}
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
