#!/usr/bin/env python3
"""Validate and migrate hash-bound audio corpus inventories.

Schema v3 distinguishes speech, noise, room impulse responses, and microphone
responses.  It also binds every derived WAV and transcript to explicit local
provenance.  Migration from schema v2 is intentionally draft-only: missing
transcripts and response assets must be curated before release qualification.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
import re
import sys
import tempfile
from pathlib import Path, PurePosixPath
from typing import Any, Mapping, MutableMapping, Sequence


class InventoryError(ValueError):
	"""Raised when an inventory is ambiguous or not release-safe."""


KINDS = ("speech", "noise", "rir", "microphone_response")
SPLITS = ("tuning", "validation", "holdout")
DEFAULT_SPLIT_SEED = "mumble-input-enhancement-v1"
MODELED_RESPONSE_SOURCE_ID = "mumble-modeled-microphone-responses-v1"
MODELED_RESPONSE_DEFINITION = Path(__file__).with_name("modeled-microphone-responses-v1.json")
DIVERSITY_REQUIREMENTS = {
	"pr_smoke": {
		"speaker_groups": 2, "languages": 1, "noise_groups": 2, "noise_classes": 2,
		"rir_groups": 2, "device_groups": 2, "device_families": 2,
	},
	"release": {
		"speaker_groups": 2, "languages": 1, "noise_groups": 2, "noise_classes": 3,
		"rir_groups": 2, "device_groups": 2, "device_families": 2,
	},
	"master_quality": {
		"speaker_groups": 8, "languages": 2, "noise_groups": 8, "noise_classes": 6,
		"rir_groups": 6, "device_groups": 4, "device_families": 4,
	},
	"nightly": {
		"speaker_groups": 16, "languages": 2, "noise_groups": 16, "noise_classes": 10,
		"rir_groups": 12, "device_groups": 8, "device_families": 4,
	},
}
NOISE_CLASSES = (
	"fan", "hvac", "hum", "keyboard", "mouse", "traffic", "rain", "wind",
	"handling", "babble", "music-tv", "competing-speech", "paired-noisy-speech",
)
DERIVATIONS = ("extracted", "decoded", "resampled", "recorded", "synthesized", "legacy-v2-import")
HEX64 = re.compile(r"[0-9a-f]{64}")
IDENTIFIER = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]*")


def _load_lock_module() -> Any:
	path = Path(__file__).with_name("validate-corpus-lock.py")
	spec = importlib.util.spec_from_file_location("mumble_audio_inventory_lock", path)
	if spec is None or spec.loader is None:
		raise InventoryError(f"unable to load corpus-lock validator: {path}")
	module = importlib.util.module_from_spec(spec)
	spec.loader.exec_module(module)
	return module


LOCK = _load_lock_module()


def canonical_sha256(value: Any) -> str:
	encoded = json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")
	return hashlib.sha256(encoded).hexdigest()


def file_sha256(path: Path) -> str:
	digest = hashlib.sha256()
	with path.open("rb") as stream:
		for chunk in iter(lambda: stream.read(1024 * 1024), b""):
			digest.update(chunk)
	return digest.hexdigest()


def _stable_int(seed: str, *parts: object) -> int:
	payload = "\0".join((seed, *(str(part) for part in parts))).encode("utf-8")
	return int.from_bytes(hashlib.sha256(payload).digest()[:8], "big")


def assigned_split(seed: str, kind: str, group_id: str) -> str:
	bucket = _stable_int(seed, "split-v1", kind, group_id) % 100
	return "tuning" if bucket < 60 else "validation" if bucket < 80 else "holdout"


def diversity_summary(items: Sequence[Mapping[str, Any]], seed: str, split: str) -> Mapping[str, int]:
	selected = [item for item in items if assigned_split(seed, item["kind"], item["group_id"]) == split]
	return {
		"speaker_groups": len({item["group_id"] for item in selected if item["kind"] == "speech"}),
		"languages": len({item["language"] for item in selected if item["kind"] == "speech"}),
		"noise_groups": len({item["group_id"] for item in selected if item["kind"] == "noise"}),
		"noise_classes": len({item["noise_class"] for item in selected if item["kind"] == "noise"}),
		"rir_groups": len({item["group_id"] for item in selected if item["kind"] == "rir"}),
		"device_groups": len({item["group_id"] for item in selected if item["kind"] == "microphone_response"}),
		"device_families": len({
			item["microphone_response"]["device_family"]
			for item in selected if item["kind"] == "microphone_response"
		}),
	}


def validate_diversity(
	items: Sequence[Mapping[str, Any]], suite: str, seed: str = DEFAULT_SPLIT_SEED,
	required_splits: Sequence[str] = SPLITS,
) -> Mapping[str, Mapping[str, int]]:
	_expect(suite in DIVERSITY_REQUIREMENTS, "suite", "unknown qualification suite")
	_expect(isinstance(seed, str) and bool(seed), "split_seed", "must be non-empty")
	_expect(bool(required_splits) and all(split in SPLITS for split in required_splits), "splits", "unknown split")
	requirements = DIVERSITY_REQUIREMENTS[suite]
	summaries: dict[str, Mapping[str, int]] = {}
	for split in required_splits:
		summary = diversity_summary(items, seed, split)
		for key, minimum in requirements.items():
			_expect(
				summary[key] >= minimum,
				f"inventory.diversity.{split}.{key}",
				f"requires at least {minimum} distinct values for {suite}; found {summary[key]}",
			)
		summaries[split] = summary
	return summaries


def _load_json(path: Path) -> Any:
	def reject_duplicates(pairs: Sequence[tuple[str, Any]]) -> MutableMapping[str, Any]:
		result: MutableMapping[str, Any] = {}
		for key, value in pairs:
			if key in result:
				raise InventoryError(f"duplicate JSON key: {key}")
			result[key] = value
		return result
	try:
		return json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=reject_duplicates)
	except (OSError, json.JSONDecodeError) as error:
		raise InventoryError(f"unable to read {path}: {error}") from error


def _expect(condition: bool, path: str, message: str) -> None:
	if not condition:
		raise InventoryError(f"{path}: {message}")


def _keys(value: Mapping[str, Any], required: set[str], optional: set[str], path: str) -> None:
	missing = sorted(required - set(value))
	unknown = sorted(set(value) - required - optional)
	_expect(not missing, path, f"missing keys: {', '.join(missing)}")
	_expect(not unknown, path, f"unknown keys: {', '.join(unknown)}")


def _identifier(value: Any, path: str) -> str:
	_expect(isinstance(value, str) and bool(IDENTIFIER.fullmatch(value)), path, "must be a stable identifier")
	return value


def _hash(value: Any, path: str) -> str:
	_expect(isinstance(value, str) and bool(HEX64.fullmatch(value)), path, "must be lowercase SHA-256")
	return value


def _positive_integer(value: Any, path: str, allow_zero: bool = False) -> int:
	_expect(isinstance(value, int) and not isinstance(value, bool), path, "must be an integer")
	_expect(value >= 0 if allow_zero else value > 0, path, "must be non-negative" if allow_zero else "must be positive")
	return value


def _safe_path(value: Any, path: str) -> str:
	_expect(isinstance(value, str) and bool(value), path, "must be a non-empty path")
	parsed = PurePosixPath(value)
	_expect(value == parsed.as_posix(), path, "must use normalized forward slashes")
	_expect(not parsed.is_absolute() and "." not in parsed.parts and ".." not in parsed.parts, path, "unsafe path")
	return value


def _allowed_sources(manifest: Mapping[str, Any]) -> Mapping[str, Mapping[str, Any]]:
	return {
		source["id"]: source
		for source in manifest["sources"]
		if source["license"]["status"] == "verified"
		and "local_eval" in source["roles"]
		and source["integrity"]["algorithm"] == "sha256"
		and "artifact_path" in source["integrity"]
	}


def _validate_provenance(value: Any, path: str) -> None:
	_expect(isinstance(value, dict), path, "must be an object")
	_keys(
		value,
		{"derivation", "parent_sha256", "parameters_sha256", "source_path", "tool", "tool_version"},
		set(),
		path,
	)
	_expect(value["derivation"] in DERIVATIONS, f"{path}.derivation", "unsupported derivation")
	_hash(value["parent_sha256"], f"{path}.parent_sha256")
	_hash(value["parameters_sha256"], f"{path}.parameters_sha256")
	_safe_path(value["source_path"], f"{path}.source_path")
	for key in ("tool", "tool_version"):
		_expect(isinstance(value[key], str) and bool(value[key].strip()), f"{path}.{key}", "must be non-empty")


def _validate_transcript(value: Any, path: str, require_release: bool) -> None:
	_expect(isinstance(value, dict), path, "must be an object")
	status = value.get("status")
	_expect(status in ("verified", "unavailable"), f"{path}.status", "must be verified or unavailable")
	if status == "verified":
		_keys(value, {"normalization", "relative_path", "sha256", "size_bytes", "status"}, set(), path)
		_safe_path(value["relative_path"], f"{path}.relative_path")
		_hash(value["sha256"], f"{path}.sha256")
		_positive_integer(value["size_bytes"], f"{path}.size_bytes", allow_zero=True)
		_expect(value["normalization"] in ("exact-utf8", "nfc-utf8"), f"{path}.normalization", "unsupported")
	else:
		_keys(value, {"reason", "status"}, set(), path)
		_expect(isinstance(value["reason"], str) and bool(value["reason"]), f"{path}.reason", "must be non-empty")
		_expect(not require_release, path, "release inventory requires a verified transcript hash")


def validate_inventory(
	value: Any,
	manifest: Mapping[str, Any],
	*,
	require_release: bool = False,
) -> list[Mapping[str, Any]]:
	_expect(isinstance(value, dict), "inventory", "must be an object")
	_keys(
		value,
		{"corpus_lock_sha256", "eligibility", "inventory_id", "items", "provenance", "schema_version"},
		set(),
		"inventory",
	)
	_expect(value["schema_version"] == 3, "inventory.schema_version", "unsupported version")
	_identifier(value["inventory_id"], "inventory.inventory_id")
	_expect(value["eligibility"] in ("draft", "release"), "inventory.eligibility", "must be draft or release")
	expected_lock = LOCK.canonical_manifest_sha256(manifest)
	_expect(value["corpus_lock_sha256"] == expected_lock, "inventory.corpus_lock_sha256", "lock mismatch")
	provenance = value["provenance"]
	_expect(isinstance(provenance, dict), "inventory.provenance", "must be an object")
	_keys(
		provenance,
		{"generated_from_state_sha256", "generator", "generator_version", "transformation_manifest_sha256"},
		set(),
		"inventory.provenance",
	)
	for key in ("generated_from_state_sha256", "transformation_manifest_sha256"):
		_hash(provenance[key], f"inventory.provenance.{key}")
	for key in ("generator", "generator_version"):
		_expect(isinstance(provenance[key], str) and bool(provenance[key].strip()), f"inventory.provenance.{key}", "must be non-empty")
	_expect(isinstance(value["items"], list) and value["items"], "inventory.items", "must be a non-empty array")
	if require_release:
		_expect(value["eligibility"] == "release", "inventory.eligibility", "release qualification rejects draft inventory")

	allowed = _allowed_sources(manifest)
	excluded = {source["id"] for source in manifest["excluded_sources"]}
	ids: list[str] = []
	kinds: set[str] = set()
	items: list[Mapping[str, Any]] = []
	for index, item in enumerate(value["items"]):
		path = f"inventory.items[{index}]"
		_expect(isinstance(item, dict), path, "must be an object")
		common = {
			"channels", "duration_samples", "group_id", "id", "kind", "provenance", "relative_path",
			"sample_rate_hz", "sha256", "size_bytes", "source_artifact_sha256", "source_id",
		}
		kind = item.get("kind")
		_expect(kind in KINDS, f"{path}.kind", "unsupported kind")
		variant = {
			"speech": {"language", "speaker_id", "transcript"},
			"noise": {"noise_class"},
			"rir": {"rir"},
			"microphone_response": {"microphone_response"},
		}[kind]
		_keys(item, common | variant, set(), path)
		_identifier(item["id"], f"{path}.id")
		_identifier(item["group_id"], f"{path}.group_id")
		_safe_path(item["relative_path"], f"{path}.relative_path")
		modeled_source = (
			kind == "microphone_response"
			and item.get("source_id") == MODELED_RESPONSE_SOURCE_ID
			and isinstance(item.get("microphone_response"), dict)
			and item["microphone_response"].get("response_kind") == "modeled"
		)
		_expect(item["source_id"] not in excluded, f"{path}.source_id", "excluded source")
		_expect(modeled_source or item["source_id"] in allowed, f"{path}.source_id", "source is not approved for local evaluation")
		_hash(item["sha256"], f"{path}.sha256")
		expected_artifact_sha256 = (
			file_sha256(MODELED_RESPONSE_DEFINITION)
			if modeled_source else allowed[item["source_id"]]["integrity"]["digest"]
		)
		_expect(item["source_artifact_sha256"] == expected_artifact_sha256, f"{path}.source_artifact_sha256", "does not match its pinned source artifact")
		for key in ("channels", "duration_samples", "sample_rate_hz", "size_bytes"):
			_positive_integer(item[key], f"{path}.{key}")
		_validate_provenance(item["provenance"], f"{path}.provenance")
		if kind == "speech":
			_expect(
				allowed[item["source_id"]]["kind"] in ("clean_speech", "paired_clean_noisy_speech"),
				f"{path}.source_id", "source kind is not speech",
			)
			_expect(isinstance(item["language"], str) and bool(item["language"]), f"{path}.language", "required")
			_identifier(item["speaker_id"], f"{path}.speaker_id")
			_validate_transcript(item["transcript"], f"{path}.transcript", require_release)
		elif kind == "noise":
			_expect(
				allowed[item["source_id"]]["kind"] in ("environmental_noise", "environmental_noise_and_rir", "paired_clean_noisy_speech"),
				f"{path}.source_id", "source kind is not noise",
			)
			_expect(item["noise_class"] in NOISE_CLASSES, f"{path}.noise_class", "unsupported noise class")
		elif kind == "rir":
			_expect(allowed[item["source_id"]]["kind"] == "environmental_noise_and_rir", f"{path}.source_id", "source kind has no RIRs")
			rir = item["rir"]
			_expect(isinstance(rir, dict), f"{path}.rir", "must be an object")
			_keys(rir, {"receiver_position_id", "rir_kind", "room_id", "rt60_ms", "source_position_id"}, set(), f"{path}.rir")
			_expect(rir["rir_kind"] in ("measured", "simulated"), f"{path}.rir.rir_kind", "unsupported")
			for key in ("room_id", "source_position_id", "receiver_position_id"):
				_identifier(rir[key], f"{path}.rir.{key}")
			_positive_integer(rir["rt60_ms"], f"{path}.rir.rt60_ms", allow_zero=True)
		else:
			response = item["microphone_response"]
			_expect(isinstance(response, dict), f"{path}.microphone_response", "must be an object")
			_keys(response, {"calibration_id", "device_family", "device_id", "response_kind"}, set(), f"{path}.microphone_response")
			_expect(response["response_kind"] in ("measured", "modeled"), f"{path}.microphone_response.response_kind", "unsupported")
			if response["response_kind"] == "modeled":
				_expect(item["provenance"]["derivation"] == "synthesized", f"{path}.provenance.derivation", "modeled response must be synthesized")
				_expect(modeled_source, f"{path}.source_id", "modeled responses must bind the tracked response definition")
				_expect(item["provenance"]["parent_sha256"] == expected_artifact_sha256, f"{path}.provenance.parent_sha256", "must bind the tracked response definition")
			else:
				_expect(item["provenance"]["derivation"] in ("recorded", "extracted", "decoded", "resampled"), f"{path}.provenance.derivation", "measured response must come from recorded material")
			_expect(response["device_family"] in ("headset", "laptop", "usb", "phone"), f"{path}.microphone_response.device_family", "unsupported")
			for key in ("device_id", "calibration_id"):
				_identifier(response[key], f"{path}.microphone_response.{key}")
		items.append(item)
		ids.append(item["id"])
		kinds.add(kind)
	_expect(ids == sorted(set(ids)), "inventory.items", "ids must be unique and sorted")
	if require_release:
		missing_kinds = sorted(set(KINDS) - kinds)
		_expect(not missing_kinds, "inventory.items", f"release inventory is missing kinds: {', '.join(missing_kinds)}")
	return items


def migrate_v2(value: Any, manifest: Mapping[str, Any], corpus_state_path: Path, inventory_id: str) -> Mapping[str, Any]:
	_expect(isinstance(value, dict), "inventory", "must be an object")
	_keys(value, {"corpus_lock_sha256", "items", "schema_version"}, set(), "inventory")
	_expect(value["schema_version"] == 2, "inventory.schema_version", "migration accepts only schema v2")
	expected_lock = LOCK.canonical_manifest_sha256(manifest)
	_expect(value["corpus_lock_sha256"] == expected_lock, "inventory.corpus_lock_sha256", "lock mismatch")
	state = _load_json(corpus_state_path)
	_expect(state.get("schema_version") == 3, "corpus_state.schema_version", "migration requires freshly generated schema-v3 corpus state")
	_expect(state.get("corpus_lock_sha256") == expected_lock, "corpus_state.corpus_lock_sha256", "lock mismatch")
	_identifier(inventory_id, "inventory_id")
	items = []
	for index, old in enumerate(value["items"]):
		_expect(isinstance(old, dict), f"inventory.items[{index}]", "must be an object")
		kind = old.get("kind")
		_expect(kind in ("speech", "noise"), f"inventory.items[{index}].kind", "legacy v2 supports speech/noise only")
		item = {key: old[key] for key in (
			"channels", "duration_samples", "group_id", "id", "kind", "relative_path", "sample_rate_hz",
			"sha256", "size_bytes", "source_artifact_sha256", "source_id",
		)}
		item["provenance"] = {
			"derivation": "legacy-v2-import",
			"parent_sha256": old["sha256"],
			"parameters_sha256": canonical_sha256(old),
			"source_path": old["relative_path"],
			"tool": "mumble-corpus-inventory-migrator",
			"tool_version": "3",
		}
		if kind == "speech":
			item["language"] = old["language"]
			item["speaker_id"] = old["group_id"]
			item["transcript"] = {"status": "unavailable", "reason": "legacy-schema-v2"}
		else:
			item["noise_class"] = old["noise_class"]
		items.append(item)
	result = {
		"schema_version": 3,
		"inventory_id": inventory_id,
		"eligibility": "draft",
		"corpus_lock_sha256": expected_lock,
		"provenance": {
			"generator": "mumble-corpus-inventory-migrator",
			"generator_version": "3",
			"generated_from_state_sha256": file_sha256(corpus_state_path),
			"transformation_manifest_sha256": canonical_sha256(value),
		},
		"items": sorted(items, key=lambda item: item["id"]),
	}
	validate_inventory(result, manifest)
	return result


def _write_json_atomic(path: Path, value: Mapping[str, Any]) -> None:
	path.parent.mkdir(parents=True, exist_ok=True)
	temporary = path.with_name(f".{path.name}.{os.getpid()}.tmp")
	temporary.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")
	os.replace(temporary, path)


def _self_test_v2(manifest: Mapping[str, Any]) -> Mapping[str, Any]:
	sources = {source["id"]: source for source in manifest["sources"]}
	common = {
		"sample_rate_hz": 48000, "channels": 1, "duration_samples": 480000,
		"size_bytes": 960044,
	}
	return {
		"schema_version": 2,
		"corpus_lock_sha256": LOCK.canonical_manifest_sha256(manifest),
		"items": [
			{**common, "source_id": "openslr28-rirs-noises", "source_artifact_sha256": sources["openslr28-rirs-noises"]["integrity"]["digest"], "id": "noise-001", "kind": "noise", "relative_path": "noise.wav", "group_id": "noise-001", "noise_class": "fan", "sha256": hashlib.sha256(b"noise").hexdigest()},
			{**common, "source_id": "mcgill-tsp-speech-v2-48k", "source_artifact_sha256": sources["mcgill-tsp-speech-v2-48k"]["integrity"]["digest"], "id": "speech-001", "kind": "speech", "relative_path": "speech.wav", "group_id": "speaker-001", "language": "sv-SE", "sha256": hashlib.sha256(b"speech").hexdigest()},
		],
	}


def run_self_test() -> None:
	manifest = LOCK.load_validated_manifest(Path(__file__).with_name("corpus-lock.json"))
	with tempfile.TemporaryDirectory(prefix="mumble-inventory-v3-") as directory:
		root = Path(directory)
		state = {
			"schema_version": 3,
			"corpus_lock_sha256": LOCK.canonical_manifest_sha256(manifest),
			"purpose": "local-eval",
			"archives": [],
		}
		state_path = root / "corpus-state.json"
		state_path.write_text(json.dumps(state), encoding="utf-8")
		draft = migrate_v2(_self_test_v2(manifest), manifest, state_path, "self-test-v3")
		validate_inventory(draft, manifest)
		try:
			validate_inventory(draft, manifest, require_release=True)
		except InventoryError as error:
			if "draft" not in str(error):
				raise
		else:
			raise AssertionError("draft migration was accepted for release")
		bad = json.loads(json.dumps(draft))
		bad["items"][1]["transcript"] = {
			"status": "verified", "relative_path": "../escape.txt", "sha256": "0" * 64,
			"size_bytes": 1, "normalization": "exact-utf8",
		}
		try:
			validate_inventory(bad, manifest)
		except InventoryError:
			pass
		else:
			raise AssertionError("unsafe transcript path was accepted")


def main(argv: Sequence[str] | None = None) -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--manifest", type=Path, default=Path(__file__).with_name("corpus-lock.json"))
	parser.add_argument("--validate", type=Path)
	parser.add_argument("--require-release-eligible", action="store_true")
	parser.add_argument("--suite", choices=tuple(DIVERSITY_REQUIREMENTS), default="release")
	parser.add_argument("--split-seed", default=DEFAULT_SPLIT_SEED)
	parser.add_argument("--migrate-v2", type=Path)
	parser.add_argument("--corpus-state", type=Path)
	parser.add_argument("--inventory-id", default="mumble-input-enhancement-v3")
	parser.add_argument("--output", type=Path)
	parser.add_argument("--self-test", action="store_true")
	args = parser.parse_args(argv)
	try:
		if args.self_test:
			run_self_test()
			print("corpus inventory v3 self-test: ok")
			if args.validate is None and args.migrate_v2 is None:
				return 0
		manifest = LOCK.load_validated_manifest(args.manifest)
		if args.validate is not None:
			inventory = _load_json(args.validate)
			items = validate_inventory(inventory, manifest, require_release=args.require_release_eligible)
			if args.require_release_eligible:
				validate_diversity(items, args.suite, args.split_seed)
			print(f"corpus inventory: ok; items={len(items)}; sha256={canonical_sha256(inventory)}")
			return 0
		if args.migrate_v2 is not None:
			if args.corpus_state is None or args.output is None:
				raise InventoryError("--corpus-state and --output are required with --migrate-v2")
			result = migrate_v2(_load_json(args.migrate_v2), manifest, args.corpus_state, args.inventory_id)
			_write_json_atomic(args.output, result)
			print(f"corpus inventory: wrote draft schema v3; items={len(result['items'])}; output={args.output}")
			return 0
		raise InventoryError("choose --validate or --migrate-v2")
	except (AssertionError, InventoryError, LOCK.ValidationError) as error:
		print(f"corpus inventory: error: {error}", file=sys.stderr)
		return 1


if __name__ == "__main__":
	sys.exit(main())
