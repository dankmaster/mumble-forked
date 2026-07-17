#!/usr/bin/env python3
"""Hash-attested, fixed-timeline objective scoring for input enhancement.

This module deliberately keeps release-gating alignment and ASR reference
construction explicit.  It never discovers latency by correlation and it does
not accept an utterance-level transcript for a rendered corpus window.
"""

from __future__ import annotations

import hashlib
import importlib.metadata
import importlib.util
import json
import math
import os
import re
import sys
import tempfile
import unicodedata
import uuid
from datetime import datetime, timedelta, timezone
from pathlib import Path, PurePosixPath
from types import SimpleNamespace
from typing import Any, Callable, Mapping, MutableMapping, Sequence


SCHEMA_VERSION = 2
SCORER_ID = "mumble-objective-quality-v2"
REFERENCE_SCHEMA_VERSION = 1
SEGMENT_ATTESTATION = "mumble-segment-matched-transcript-v1"
NORMALIZATION_ID = "unicode-nfkc-casefold-alnum-v1"
SAMPLE_RATE_HZ = 48_000
ORDINARY_DATASET_SPLITS = ("tuning", "validation", "pr-smoke", "release-fixture")
RELEASE_HOLDOUT_SPLIT = "release-holdout"
ALLOWED_DATASET_SPLITS = (*ORDINARY_DATASET_SPLITS, RELEASE_HOLDOUT_SPLIT)
ALLOWED_LANGUAGES = ("en", "sv")
ALLOWED_PROFILES = ("Original", "Light", "Balanced", "Quality", "VoiceFocus", "Auto")
ALLOWED_CONDITIONS = ("clean", "noisy", "severe")
SIGNAL_STAGES = ("sender-pre-opus", "receiver-capture")
TRANSCRIPTION_PARAMETERS = {
	"beam_size": 5,
	"condition_on_previous_text": False,
	"task": "transcribe",
	"temperature": 0.0,
	"vad_filter": False,
	"word_timestamps": False,
}
SCORING_DISTRIBUTIONS = (
	"ctranslate2",
	"faster-whisper",
	"jiwer",
	"librosa",
	"numpy",
	"onnxruntime",
	"pystoi",
	"scipy",
	"soundfile",
	"tokenizers",
)
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
IDENTIFIER_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._:-]{0,127}$")
HOLDOUT_PURPOSE = "input-enhancement-community-release-final-qualification"
HOLDOUT_ATTESTATION_ID = "mumble-input-enhancement-release-holdout-opening-v1"
HOLDOUT_OPENING_REPORT_ID = "mumble-input-enhancement-release-holdout-opening-report-v1"
HOLDOUT_RECEIPT_ID = "mumble-input-enhancement-release-holdout-receipt-v1"
HOLDOUT_AUTHORIZATION_KIND = "detached-ed25519-release-owner-approval-v1"
HOLDOUT_SIGNATURE_ENCODING = "raw-ed25519-64-byte-file"
HOLDOUT_MAX_AUTHORIZATION_WINDOW = timedelta(hours=24)
HOLDOUT_MAX_CLOCK_SKEW = timedelta(minutes=5)
UUID4_RE = re.compile(r"^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$")

# RFC 8032 Ed25519 constants.  Verification lives here instead of relying on
# an unpinned host crypto package: the objective-metrics environment is itself
# hash-pinned and intentionally has no optional crypto dependency.
_ED25519_P = 2**255 - 19
_ED25519_L = 2**252 + 27742317777372353535851937790883648493
_ED25519_D = (-121665 * pow(121666, _ED25519_P - 2, _ED25519_P)) % _ED25519_P
_ED25519_I = pow(2, (_ED25519_P - 1) // 4, _ED25519_P)


class ObjectiveScoreError(ValueError):
	"""Raised when scoring inputs or provenance are not release-safe."""


def _expect(condition: bool, path: str, message: str) -> None:
	if not condition:
		raise ObjectiveScoreError(f"{path}: {message}")


def _mapping(value: Any, path: str) -> Mapping[str, Any]:
	_expect(isinstance(value, dict), path, "expected an object")
	return value


def _exact_keys(value: Mapping[str, Any], expected: set[str], path: str) -> None:
	missing = sorted(expected - set(value))
	unknown = sorted(set(value) - expected)
	_expect(not missing, path, f"missing keys: {', '.join(missing)}")
	_expect(not unknown, path, f"unknown keys: {', '.join(unknown)}")


def canonical_json_bytes(value: Any) -> bytes:
	return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")


def canonical_sha256(value: Any) -> str:
	return hashlib.sha256(canonical_json_bytes(value)).hexdigest()


def sha256(path: Path) -> str:
	digest = hashlib.sha256()
	with path.open("rb") as stream:
		for block in iter(lambda: stream.read(1024 * 1024), b""):
			digest.update(block)
	return digest.hexdigest()


def file_record(path: Path, *, include_path: bool = False) -> dict[str, Any]:
	_expect(path.is_file() and not path.is_symlink(), str(path), "required regular file is missing")
	record: dict[str, Any] = {
		"sha256": sha256(path),
		"size_bytes": path.stat().st_size,
	}
	if include_path:
		record["path"] = str(path.resolve())
	return record


def _ed25519_xrecover(y: int) -> int:
	xx = ((y * y - 1) * pow(_ED25519_D * y * y + 1, _ED25519_P - 2, _ED25519_P)) % _ED25519_P
	x = pow(xx, (_ED25519_P + 3) // 8, _ED25519_P)
	if (x * x - xx) % _ED25519_P != 0:
		x = (x * _ED25519_I) % _ED25519_P
	_expect((x * x - xx) % _ED25519_P == 0, "Ed25519 point", "invalid square root")
	return x


def _ed25519_base_point() -> tuple[int, int, int, int]:
	y = (4 * pow(5, _ED25519_P - 2, _ED25519_P)) % _ED25519_P
	x = _ed25519_xrecover(y)
	if x & 1:
		x = _ED25519_P - x
	return (x, y, 1, (x * y) % _ED25519_P)


_ED25519_BASE = _ed25519_base_point()
_ED25519_IDENTITY = (0, 1, 1, 0)


def _ed25519_add(
	left: tuple[int, int, int, int], right: tuple[int, int, int, int]
) -> tuple[int, int, int, int]:
	x1, y1, z1, t1 = left
	x2, y2, z2, t2 = right
	a = ((y1 - x1) * (y2 - x2)) % _ED25519_P
	b = ((y1 + x1) * (y2 + x2)) % _ED25519_P
	c = (2 * _ED25519_D * t1 * t2) % _ED25519_P
	d = (2 * z1 * z2) % _ED25519_P
	e = (b - a) % _ED25519_P
	f = (d - c) % _ED25519_P
	g = (d + c) % _ED25519_P
	h = (b + a) % _ED25519_P
	return ((e * f) % _ED25519_P, (g * h) % _ED25519_P, (f * g) % _ED25519_P, (e * h) % _ED25519_P)


def _ed25519_scalar_multiply(
	point: tuple[int, int, int, int], scalar: int
) -> tuple[int, int, int, int]:
	result = _ED25519_IDENTITY
	addend = point
	while scalar:
		if scalar & 1:
			result = _ed25519_add(result, addend)
		addend = _ed25519_add(addend, addend)
		scalar >>= 1
	return result


def _ed25519_encode(point: tuple[int, int, int, int]) -> bytes:
	x, y, z, _ = point
	zinv = pow(z, _ED25519_P - 2, _ED25519_P)
	x = (x * zinv) % _ED25519_P
	y = (y * zinv) % _ED25519_P
	encoded = y | ((x & 1) << 255)
	return encoded.to_bytes(32, "little")


def _ed25519_decode(encoded: bytes, label: str) -> tuple[int, int, int, int]:
	_expect(len(encoded) == 32, label, "must be exactly 32 bytes")
	value = int.from_bytes(encoded, "little")
	sign = value >> 255
	y = value & ((1 << 255) - 1)
	_expect(y < _ED25519_P, label, "non-canonical point encoding")
	x = _ed25519_xrecover(y)
	if (x & 1) != sign:
		x = _ED25519_P - x
	_expect(not (x == 0 and sign == 1), label, "non-canonical point sign")
	point = (x, y, 1, (x * y) % _ED25519_P)
	_expect(_ed25519_encode(point) == encoded, label, "non-canonical point encoding")
	# Release approval keys and R values must be in the prime-order subgroup.
	_expect(_ed25519_encode(_ed25519_scalar_multiply(point, _ED25519_L)) == _ed25519_encode(_ED25519_IDENTITY), label, "point is outside the prime-order subgroup")
	_expect(_ed25519_encode(point) != _ed25519_encode(_ED25519_IDENTITY), label, "identity point is forbidden")
	return point


def _verify_ed25519(public_key: bytes, message: bytes, signature: bytes) -> None:
	_expect(len(signature) == 64, "holdout approval signature", "must be exactly 64 raw bytes")
	a = _ed25519_decode(public_key, "holdout approval public key")
	r_encoded = signature[:32]
	r = _ed25519_decode(r_encoded, "holdout approval signature.R")
	s = int.from_bytes(signature[32:], "little")
	_expect(s < _ED25519_L, "holdout approval signature.S", "non-canonical scalar")
	h = int.from_bytes(hashlib.sha512(r_encoded + public_key + message).digest(), "little") % _ED25519_L
	left = _ed25519_scalar_multiply(_ED25519_BASE, s)
	right = _ed25519_add(r, _ed25519_scalar_multiply(a, h))
	_expect(_ed25519_encode(left) == _ed25519_encode(right), "holdout approval signature", "detached Ed25519 verification failed")


def _ed25519_sign_for_self_test(seed: bytes, message: bytes) -> tuple[bytes, bytes]:
	"""Return (public key, signature) for synthetic self-test evidence only."""
	_expect(len(seed) == 32, "self-test Ed25519 seed", "must be 32 bytes")
	expanded = hashlib.sha512(seed).digest()
	clamped = bytearray(expanded[:32])
	clamped[0] &= 248
	clamped[31] &= 63
	clamped[31] |= 64
	secret_scalar = int.from_bytes(clamped, "little")
	public_key = _ed25519_encode(_ed25519_scalar_multiply(_ED25519_BASE, secret_scalar))
	nonce = int.from_bytes(hashlib.sha512(expanded[32:] + message).digest(), "little") % _ED25519_L
	r_encoded = _ed25519_encode(_ed25519_scalar_multiply(_ED25519_BASE, nonce))
	challenge = int.from_bytes(hashlib.sha512(r_encoded + public_key + message).digest(), "little") % _ED25519_L
	s = (nonce + challenge * secret_scalar) % _ED25519_L
	return public_key, r_encoded + s.to_bytes(32, "little")


def _parse_utc_timestamp(value: Any, path: str) -> datetime:
	_expect(isinstance(value, str) and bool(re.fullmatch(r"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z", value)), path, "must be canonical UTC YYYY-MM-DDTHH:MM:SSZ")
	try:
		return datetime.strptime(value, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=timezone.utc)
	except ValueError as error:
		raise ObjectiveScoreError(f"{path}: invalid UTC timestamp") from error


def _existing_directory(path: Path, label: str) -> Path:
	is_junction = bool(getattr(path, "is_junction", lambda: False)())
	_expect(path.is_dir() and not path.is_symlink() and not is_junction, label, "must be an existing non-link directory")
	return path.resolve()


def _path_below(root: Path, relative: str, label: str) -> Path:
	_safe_relative(relative, label)
	candidate = root.joinpath(*PurePosixPath(relative).parts)
	resolved_parent = candidate.parent.resolve()
	_expect(resolved_parent == root or root in resolved_parent.parents, label, "escapes the protected opening root")
	return candidate


def _write_new_json(path: Path, value: Mapping[str, Any], label: str) -> dict[str, Any]:
	_expect(not os.path.lexists(path), label, "path is already occupied")
	payload = canonical_json_bytes(value) + b"\n"
	try:
		with path.open("xb") as stream:
			stream.write(payload)
			stream.flush()
			os.fsync(stream.fileno())
	except FileExistsError as error:
		raise ObjectiveScoreError(f"{label}: path was concurrently occupied") from error
	return file_record(path)


def _load_json(path: Path, label: str) -> Any:
	payload = _read_regular_bytes(path, label)[0]
	return _load_json_bytes(payload, label, path)


def _load_json_bytes(payload: bytes, label: str, source: Path | None = None) -> Any:
	def reject_duplicates(pairs: Sequence[tuple[str, Any]]) -> MutableMapping[str, Any]:
		result: MutableMapping[str, Any] = {}
		for key, value in pairs:
			if key in result:
				raise ObjectiveScoreError(f"{label}: duplicate JSON key {key!r}")
			result[key] = value
		return result

	try:
		return json.loads(payload.decode("utf-8"), object_pairs_hook=reject_duplicates)
	except (UnicodeError, json.JSONDecodeError) as error:
		raise ObjectiveScoreError(f"{label}: unable to parse {source or '<bytes>'}: {error}") from error


def _read_regular_bytes(path: Path, label: str) -> tuple[bytes, dict[str, Any]]:
	_expect(path.is_file() and not path.is_symlink(), label, "required regular file is missing")
	try:
		payload = path.read_bytes()
	except OSError as error:
		raise ObjectiveScoreError(f"{label}: unable to read {path}: {error}") from error
	_expect(path.is_file() and not path.is_symlink(), label, "file changed type while it was read")
	return payload, {"sha256": hashlib.sha256(payload).hexdigest(), "size_bytes": len(payload)}


def _safe_relative(value: Any, path: str) -> str:
	_expect(isinstance(value, str) and bool(value), path, "expected a relative path")
	parsed = PurePosixPath(value)
	_expect(value == parsed.as_posix(), path, "must use normalized forward slashes")
	_expect(not parsed.is_absolute() and "." not in parsed.parts and ".." not in parsed.parts, path, "unsafe path")
	return value


def _hash(value: Any, path: str) -> str:
	_expect(isinstance(value, str) and bool(SHA256_RE.fullmatch(value)), path, "invalid lowercase SHA-256")
	return value


def _integer(value: Any, path: str, minimum: int = 0) -> int:
	_expect(isinstance(value, int) and not isinstance(value, bool), path, "expected an integer")
	_expect(value >= minimum, path, f"must be >= {minimum}")
	return value


def _number(value: Any, path: str) -> float:
	_expect(isinstance(value, (int, float)) and not isinstance(value, bool), path, "expected a number")
	number = float(value)
	_expect(math.isfinite(number), path, "must be finite")
	return number


def _stable_metric(value: float) -> float:
	"""Remove platform-level floating dust without affecting release gates."""

	return round(float(value), 12)


def normalize_text(value: str) -> str:
	"""Normalize ASR text without language-specific or punctuation heuristics."""

	normalized = unicodedata.normalize("NFKC", value).casefold()
	characters = [character if character.isalnum() else " " for character in normalized]
	return " ".join("".join(characters).split())


def word_error(reference: str, hypothesis: str) -> tuple[int, int, float]:
	reference_words = reference.split()
	hypothesis_words = hypothesis.split()
	_expect(bool(reference_words), "WER reference", "normalizes to zero words")
	previous = list(range(len(hypothesis_words) + 1))
	for reference_index, reference_word in enumerate(reference_words, start=1):
		current = [reference_index]
		for hypothesis_index, hypothesis_word in enumerate(hypothesis_words, start=1):
			current.append(min(
				current[-1] + 1,
				previous[hypothesis_index] + 1,
				previous[hypothesis_index - 1] + (reference_word != hypothesis_word),
			))
		previous = current
	errors = previous[-1]
	return errors, len(reference_words), errors / len(reference_words)


def _verify_file_from_record(root: Path, record: Mapping[str, Any], path: str) -> Path:
	missing = {"relative_path", "sha256", "size_bytes"} - set(record)
	_expect(not missing, path, f"missing file-record keys: {', '.join(sorted(missing))}")
	relative = _safe_relative(record["relative_path"], f"{path}.relative_path")
	actual = root.joinpath(*PurePosixPath(relative).parts)
	_expect(actual.is_file() and not actual.is_symlink(), path, f"missing regular file: {actual}")
	_expect(actual.stat().st_size == _integer(record["size_bytes"], f"{path}.size_bytes"), path, "size mismatch")
	_expect(sha256(actual) == _hash(record["sha256"], f"{path}.sha256"), path, "hash mismatch")
	return actual


def _distribution_record(name: str) -> Mapping[str, Any]:
	distribution = importlib.metadata.distribution(name)
	files = distribution.files
	_expect(files is not None, f"distribution {name}", "has no installed-file inventory")
	records = []
	for entry in sorted(files, key=lambda item: str(item).replace("\\", "/").casefold()):
		relative = str(entry).replace("\\", "/")
		if relative.endswith(".pyc") or "/__pycache__/" in f"/{relative}":
			continue
		actual = Path(distribution.locate_file(entry)).resolve()
		if not actual.exists():
			continue
		_expect(actual.is_file() and not actual.is_symlink(), f"distribution {name}", f"invalid file: {actual}")
		records.append({
			"relative_path": relative,
			"sha256": sha256(actual),
			"size_bytes": actual.stat().st_size,
		})
	_expect(bool(records), f"distribution {name}", "has no hashable files")
	return {
		"name": name,
		"version": distribution.version,
		"file_count": len(records),
		"size_bytes": sum(record["size_bytes"] for record in records),
		"tree_sha256": canonical_sha256(records),
		"files": records,
	}


def verify_metrics_runtime(
	runtime_root: Path,
	manifest_path: Path,
	*,
	verify_environment: bool = True,
) -> Mapping[str, Any]:
	"""Verify manifest, lock, inventory, models, and the executing Python runtime."""

	runtime_root = runtime_root.resolve()
	manifest_path = manifest_path.resolve()
	_expect(runtime_root.is_dir() and not runtime_root.is_symlink(), "metrics runtime", "invalid runtime root")
	try:
		manifest_path.relative_to(runtime_root)
	except ValueError as error:
		raise ObjectiveScoreError("metrics manifest: must be below the runtime root") from error
	manifest = _mapping(_load_json(manifest_path, "metrics manifest"), "metrics manifest")
	_exact_keys(manifest, {"models", "runtime", "schema_version"}, "metrics manifest")
	_expect(manifest["schema_version"] == 1, "metrics manifest.schema_version", "unsupported version")
	runtime = _mapping(manifest["runtime"], "metrics manifest.runtime")
	_exact_keys(runtime, {"id", "relative_path", "sha256", "size_bytes", "version"}, "metrics manifest.runtime")
	lock_path = _verify_file_from_record(runtime_root, runtime, "metrics manifest.runtime")
	lock = _mapping(_load_json(lock_path, "metrics runtime lock"), "metrics runtime lock")
	_exact_keys(lock, {"id", "inventory", "schema_version", "sources", "version"}, "metrics runtime lock")
	_expect(lock["schema_version"] == 1, "metrics runtime lock.schema_version", "unsupported version")
	_expect(lock["id"] == runtime["id"] and lock["version"] == runtime["version"], "metrics runtime lock", "identity mismatch")
	inventory_ref = _mapping(lock["inventory"], "metrics runtime lock.inventory")
	_exact_keys(
		inventory_ref,
		{"assets_tree_sha256", "canonical_sha256", "distributions_tree_sha256", "relative_path", "sha256", "size_bytes", "whisper_tree_sha256"},
		"metrics runtime lock.inventory",
	)
	inventory_path = _verify_file_from_record(runtime_root, inventory_ref, "metrics runtime lock.inventory")
	inventory = _mapping(_load_json(inventory_path, "metrics runtime inventory"), "metrics runtime inventory")
	_expect(canonical_sha256(inventory) == _hash(inventory_ref["canonical_sha256"], "metrics runtime lock.inventory.canonical_sha256"), "metrics runtime inventory", "canonical hash mismatch")
	_exact_keys(inventory, {"assets", "assets_tree_sha256", "distributions", "distributions_tree_sha256", "python", "schema_version", "whisper_snapshot"}, "metrics runtime inventory")
	_expect(inventory["schema_version"] == 1, "metrics runtime inventory.schema_version", "unsupported version")
	assets = inventory["assets"]
	_expect(isinstance(assets, list) and bool(assets), "metrics runtime inventory.assets", "expected a non-empty array")
	_expect(canonical_sha256(assets) == inventory["assets_tree_sha256"] == inventory_ref["assets_tree_sha256"], "metrics runtime inventory.assets", "tree hash mismatch")
	asset_by_path: dict[str, Mapping[str, Any]] = {}
	for index, value in enumerate(assets):
		record = _mapping(value, f"metrics runtime inventory.assets[{index}]")
		_exact_keys(record, {"relative_path", "sha256", "size_bytes"}, f"metrics runtime inventory.assets[{index}]")
		relative = _safe_relative(record["relative_path"], f"metrics runtime inventory.assets[{index}].relative_path")
		_expect(relative not in asset_by_path, "metrics runtime inventory.assets", f"duplicate path: {relative}")
		asset_by_path[relative] = record
		# The legacy ignored scorer is provenance-only: the runtime builder records
		# its source bytes without copying it into the runtime payload.
		if relative != "external/scorer/score-input-enhancement-objective.py":
			_verify_file_from_record(runtime_root, record, f"metrics runtime inventory asset {relative}")

	whisper = _mapping(inventory["whisper_snapshot"], "metrics runtime inventory.whisper_snapshot")
	_exact_keys(whisper, {"file_count", "size_bytes", "tree_sha256"}, "metrics runtime inventory.whisper_snapshot")
	_expect(whisper["tree_sha256"] == inventory_ref["whisper_tree_sha256"], "metrics runtime inventory.whisper_snapshot", "tree hash mismatch")
	distributions = inventory["distributions"]
	_expect(isinstance(distributions, list), "metrics runtime inventory.distributions", "expected an array")
	distribution_summary = []
	for index, value in enumerate(distributions):
		record = _mapping(value, f"metrics runtime inventory.distributions[{index}]")
		_exact_keys(record, {"file_count", "files", "name", "size_bytes", "tree_sha256", "version"}, f"metrics runtime inventory.distributions[{index}]")
		distribution_summary.append({key: record[key] for key in ("name", "version", "file_count", "size_bytes", "tree_sha256")})
	_expect(canonical_sha256(distribution_summary) == inventory["distributions_tree_sha256"] == inventory_ref["distributions_tree_sha256"], "metrics runtime inventory.distributions", "tree hash mismatch")
	if verify_environment:
		python = _mapping(inventory["python"], "metrics runtime inventory.python")
		_exact_keys(python, {"executable", "implementation", "venv_root", "version"}, "metrics runtime inventory.python")
		_expect(Path(sys.prefix).resolve() == Path(str(python["venv_root"])).resolve(), "metrics runtime Python", "the scorer is not running in the pinned venv")
		_expect(sys.implementation.name == python["implementation"] and sys.version.split()[0] == python["version"], "metrics runtime Python", "implementation or version drift")
		executable = _mapping(python["executable"], "metrics runtime inventory.python.executable")
		_expect(Path(sys.executable).is_file(), "metrics runtime Python", "executable is missing")
		_expect(sha256(Path(sys.executable)) == executable["sha256"] and Path(sys.executable).stat().st_size == executable["size_bytes"], "metrics runtime Python", "executable drift")
		actual_distributions = [_distribution_record(name) for name in SCORING_DISTRIBUTIONS]
		_expect(actual_distributions == distributions, "metrics runtime distributions", "installed distribution bytes drifted from inventory")

	models = manifest["models"]
	_expect(isinstance(models, list), "metrics manifest.models", "expected an array")
	model_by_id: dict[str, Mapping[str, Any]] = {}
	for index, value in enumerate(models):
		record = _mapping(value, f"metrics manifest.models[{index}]")
		_exact_keys(record, {"id", "relative_path", "sha256", "size_bytes"}, f"metrics manifest.models[{index}]")
		_expect(record["id"] not in model_by_id, "metrics manifest.models", f"duplicate id: {record['id']}")
		_verify_file_from_record(runtime_root, record, f"metrics manifest model {record['id']}")
		model_by_id[str(record["id"])] = record
	for required in ("dnsmos", "estoi", "wer-en", "wer-sv"):
		_expect(required in model_by_id, "metrics manifest.models", f"missing {required}")
	legacy = asset_by_path.get("external/scorer/score-input-enhancement-objective.py")
	_expect(legacy is not None, "metrics runtime inventory.assets", "missing legacy scorer provenance pin")
	return {
		"id": runtime["id"],
		"version": runtime["version"],
		"manifest": {**file_record(manifest_path), "relative_path": manifest_path.relative_to(runtime_root).as_posix()},
		"lock": {**file_record(lock_path), "relative_path": lock_path.relative_to(runtime_root).as_posix()},
		"inventory": {**file_record(inventory_path), "relative_path": inventory_path.relative_to(runtime_root).as_posix()},
		"sources": lock["sources"],
		"assets_tree_sha256": inventory["assets_tree_sha256"],
		"distributions_tree_sha256": inventory["distributions_tree_sha256"],
		"whisper_tree_sha256": whisper["tree_sha256"],
		"models": model_by_id,
		"legacy_local_scorer_pin": dict(legacy),
		"runtime_root": runtime_root,
	}


def _audio_record(path: Path, data: Any, rate: int, channels: int) -> Mapping[str, Any]:
	return {
		**file_record(path),
		"channels": channels,
		"frames": int(len(data)),
		"sample_rate_hz": rate,
	}


def _read_audio(path: Path) -> tuple[Any, int, int]:
	try:
		import numpy as np
		import soundfile as sf
	except ImportError as error:
		raise ObjectiveScoreError("the pinned numpy/soundfile runtime is unavailable") from error
	_expect(path.is_file() and not path.is_symlink(), str(path), "audio is missing or not a regular file")
	data, rate = sf.read(path, dtype="float32", always_2d=True)
	_expect(rate == SAMPLE_RATE_HZ, str(path), f"must be {SAMPLE_RATE_HZ} Hz")
	_expect(data.shape[1] == 1, str(path), "must be mono")
	mono = np.asarray(data[:, 0], dtype=np.float32)
	_expect(bool(np.all(np.isfinite(mono))), str(path), "contains NaN or Inf")
	return mono, int(rate), int(data.shape[1])


def _fixed_window(data: Any, latency_samples: int, length: int, label: str) -> Any:
	_integer(latency_samples, f"{label} latency samples")
	_expect(len(data) >= latency_samples + length, label, "is shorter than the declared fixed-latency scoring window; padding is forbidden")
	return data[latency_samples:latency_samples + length]


def _load_fixed_score(
	path: Path,
	*,
	reference_sha256: str,
	received_sha256: str,
	declared_latency_samples: int,
	timeline_alignment: str,
) -> Mapping[str, Any]:
	score = _mapping(_load_json(path, "fixed-timeline score"), "fixed-timeline score")
	_expect(score.get("schema_version") == 3 and score.get("scorer") == "mumble-fixed-timeline-v3", "fixed-timeline score", "unsupported scorer")
	_expect(score.get("passed") is True, "fixed-timeline score.passed", "route/edge/tail gate did not pass")
	_expect(score.get("timeline_alignment") == timeline_alignment, "fixed-timeline score.timeline_alignment", "unexpected alignment")
	_expect(score.get("reference_sha256") == reference_sha256, "fixed-timeline score.reference_sha256", "does not bind the exact clean reference")
	_expect(score.get("received_sha256") == received_sha256, "fixed-timeline score.received_sha256", "does not bind the exact capture")
	_expect(score.get("sample_rate_hz") == SAMPLE_RATE_HZ and score.get("frame_samples") == SAMPLE_RATE_HZ // 100, "fixed-timeline score", "invalid sample/frame rate")
	_expect(score.get("declared_latency_samples") == declared_latency_samples, "fixed-timeline score.declared_latency_samples", "does not match the caller declaration")
	limits = _mapping(score.get("qualification_limits"), "fixed-timeline score.qualification_limits")
	for edge in ("onset", "end"):
		loss = _integer(score.get(f"{edge}_loss_samples"), f"fixed-timeline score.{edge}_loss_samples")
		limit = _integer(limits.get(f"max_{edge}_loss_samples"), f"fixed-timeline score.qualification_limits.max_{edge}_loss_samples")
		_expect(loss <= limit, f"fixed-timeline score.{edge}_loss_samples", "exceeds its independently recorded edge gate")
	_expect(score.get("received_clipped_samples") == 0, "fixed-timeline score.received_clipped_samples", "new clipping is forbidden")
	return score


def _receiver_route_alignment(args: Any, clean_record: Mapping[str, Any], input_records: Mapping[str, Any]) -> tuple[int, Mapping[str, Any]]:
	required_paths = {
		"route control WAV": args.route_control_wav,
		"route control score": args.route_control_score,
		"candidate fixed-timeline score": args.candidate_fixed_timeline_score,
		"route E2E manifest": args.route_e2e_manifest,
	}
	missing = [label for label, path in required_paths.items() if path is None]
	_expect(not missing, "receiver-capture alignment", f"missing required qualified route inputs: {', '.join(missing)}")
	_expect(args.original_latency_samples == 0, "receiver-capture Original latency", "must be zero; OG route delay comes only from the qualified control")
	control_record = file_record(args.route_control_wav)
	control_score_record = file_record(args.route_control_score)
	candidate_score_record = file_record(args.candidate_fixed_timeline_score)
	manifest_record = file_record(args.route_e2e_manifest)
	control_score = _load_fixed_score(
		args.route_control_score,
		reference_sha256=clean_record["sha256"],
		received_sha256=control_record["sha256"],
		declared_latency_samples=0,
		timeline_alignment="fixed",
	)
	_expect(control_score.get("transport_baseline") is None, "route control score.transport_baseline", "the independently passing Original control cannot bless itself")
	candidate_score = _load_fixed_score(
		args.candidate_fixed_timeline_score,
		reference_sha256=clean_record["sha256"],
		received_sha256=input_records["candidate"]["sha256"],
		declared_latency_samples=args.candidate_latency_samples,
		timeline_alignment="fixed-paired-original-route",
	)
	baseline = _mapping(candidate_score.get("transport_baseline"), "candidate fixed-timeline score.transport_baseline")
	_expect(baseline.get("qualification") == "caller-verified-passing-original", "candidate fixed-timeline score.transport_baseline.qualification", "control was not independently qualified")
	_expect(baseline.get("sha256") == control_record["sha256"], "candidate fixed-timeline score.transport_baseline.sha256", "does not bind the exact control WAV")
	_expect(baseline.get("declared_latency_samples") == 0, "candidate fixed-timeline score.transport_baseline.declared_latency_samples", "Original control latency must be zero")
	route_offset = _integer(baseline.get("applied_onset_adjustment_samples"), "candidate fixed-timeline score.transport_baseline.applied_onset_adjustment_samples")
	_expect(route_offset % (SAMPLE_RATE_HZ // 100) == 0, "candidate fixed-timeline score.transport_baseline.applied_onset_adjustment_samples", "must be 10 ms frame aligned")

	manifest = _mapping(_load_json(args.route_e2e_manifest, "route E2E manifest"), "route E2E manifest")
	_expect(manifest.get("schema_version") == 3 and manifest.get("status") == "passed", "route E2E manifest", "must be a passing schema-v3 run")
	_expect(manifest.get("case_id") == args.case_id and manifest.get("profile") == args.profile, "route E2E manifest", "case/profile mismatch")
	_expect(manifest.get("receiver_cleanup") is False and manifest.get("private_audio_do_not_upload") is True, "route E2E manifest", "receiver cleanup or privacy contract mismatch")
	results = _mapping(manifest.get("results"), "route E2E manifest.results")
	control = _mapping(results.get("control"), "route E2E manifest.results.control")
	original = _mapping(results.get("original_comparison"), "route E2E manifest.results.original_comparison")
	candidate = _mapping(results.get("candidate"), "route E2E manifest.results.candidate")
	_expect(control.get("qualification_purpose") == "clean-original-route-control", "route E2E manifest.results.control", "wrong qualification purpose")
	_expect(original.get("qualification_purpose") == "noisy-original-quality-comparison", "route E2E manifest.results.original_comparison", "wrong qualification purpose")
	_expect(candidate.get("qualification_purpose") == "noisy-enhanced-candidate", "route E2E manifest.results.candidate", "wrong qualification purpose")
	_expect(control.get("capture_sha256") == control_record["sha256"], "route E2E manifest.results.control.capture_sha256", "control WAV mismatch")
	_expect(original.get("capture_sha256") == input_records["noisy_original"]["sha256"], "route E2E manifest.results.original_comparison.capture_sha256", "Original comparison WAV mismatch")
	_expect(candidate.get("capture_sha256") == input_records["candidate"]["sha256"], "route E2E manifest.results.candidate.capture_sha256", "candidate WAV mismatch")
	_expect(control.get("fixed_timeline_score_sha256") == control_score_record["sha256"], "route E2E manifest.results.control.fixed_timeline_score_sha256", "control score mismatch")
	_expect(candidate.get("fixed_timeline_score_sha256") == candidate_score_record["sha256"], "route E2E manifest.results.candidate.fixed_timeline_score_sha256", "candidate score mismatch")
	stable_identity_keys = (
		"client_binary_sha256", "model_manifest_sha256", "recipe_manifest_sha256",
		"run_provenance_sha256", "runtime_payload_sha256", "server_binary_sha256",
	)
	identities = [_mapping(role.get("execution_identity"), f"route E2E manifest.results.{name}.execution_identity") for name, role in (("control", control), ("original_comparison", original), ("candidate", candidate))]
	stable_identity = {key: identities[0].get(key) for key in stable_identity_keys}
	for index, identity in enumerate(identities):
		_expect({key: identity.get(key) for key in stable_identity_keys} == stable_identity, f"route E2E manifest execution identity[{index}]", "control, Original and candidate do not share transport/runtime provenance")
		for key in stable_identity_keys:
			_hash(identity.get(key), f"route E2E manifest execution identity[{index}].{key}")
	_expect(manifest.get("run_provenance_sha256") == stable_identity["run_provenance_sha256"], "route E2E manifest.run_provenance_sha256", "execution identity mismatch")
	input_gate = _mapping(manifest.get("input_timeline_gate"), "route E2E manifest.input_timeline_gate")
	_expect(input_gate.get("alignment") == "fixed-declared-latency" and input_gate.get("artifact") == "sender_pre_opus", "route E2E manifest.input_timeline_gate", "missing independent pre-Opus edge gate")
	_expect(input_gate.get("complete_tail_required") is True, "route E2E manifest.input_timeline_gate.complete_tail_required", "tail gate must be independent and strict")
	_expect(_integer(input_gate.get("max_onset_loss_samples"), "route E2E manifest.input_timeline_gate.max_onset_loss_samples") <= SAMPLE_RATE_HZ // 100, "route E2E manifest.input_timeline_gate", "onset gate exceeds one frame")
	_expect(_integer(input_gate.get("max_end_loss_samples"), "route E2E manifest.input_timeline_gate.max_end_loss_samples") <= SAMPLE_RATE_HZ // 100, "route E2E manifest.input_timeline_gate", "end gate exceeds one frame")
	return route_offset, {
		"route_offset_samples": route_offset,
		"control_wav": control_record,
		"control_fixed_timeline_score": control_score_record,
		"candidate_fixed_timeline_score": candidate_score_record,
		"e2e_manifest": manifest_record,
		"stable_execution_identity": stable_identity,
		"edge_tail_gate": {
			"candidate_passed": True,
			"control_passed": True,
			"pre_opus_complete_tail_required": True,
			"pre_opus_max_end_loss_samples": input_gate["max_end_loss_samples"],
			"pre_opus_max_onset_loss_samples": input_gate["max_onset_loss_samples"],
		},
	}


def _timeline_alignment(args: Any, clean_record: Mapping[str, Any], input_records: Mapping[str, Any]) -> tuple[int, int, Mapping[str, Any] | None]:
	_expect(args.signal_stage in SIGNAL_STAGES, "signal stage", "must be sender-pre-opus or receiver-capture")
	route_args = (args.route_control_wav, args.route_control_score, args.candidate_fixed_timeline_score, args.route_e2e_manifest)
	if args.signal_stage == "sender-pre-opus":
		_expect(all(value is None for value in route_args), "sender-pre-opus alignment", "route-control arguments are forbidden")
		original_start, candidate_start = _compose_window_starts(0, args.original_latency_samples, args.candidate_latency_samples)
		return original_start, candidate_start, None
	route_offset, binding = _receiver_route_alignment(args, clean_record, input_records)
	original_start, candidate_start = _compose_window_starts(route_offset, args.original_latency_samples, args.candidate_latency_samples)
	return original_start, candidate_start, binding


def _compose_window_starts(route_offset: int, original_latency: int, candidate_latency: int) -> tuple[int, int]:
	_integer(route_offset, "route offset")
	_integer(original_latency, "Original latency")
	_integer(candidate_latency, "candidate latency")
	_expect(route_offset % (SAMPLE_RATE_HZ // 100) == 0, "route offset", "must be frame aligned")
	return route_offset + original_latency, route_offset + candidate_latency


def _load_dnsmos(script_path: Path, primary_model: Path, p808_model: Path) -> Any:
	import librosa

	original_resample = librosa.resample

	def compatible_resample(audio: Any, *positional: Any, **keywords: Any) -> Any:
		if positional:
			_expect(len(positional) == 2 and "orig_sr" not in keywords and "target_sr" not in keywords, "DNSMOS", "unexpected librosa.resample call")
			keywords["orig_sr"], keywords["target_sr"] = positional
		return original_resample(audio, **keywords)

	librosa.resample = compatible_resample
	spec = importlib.util.spec_from_file_location("mumble_pinned_dnsmos", script_path)
	_expect(spec is not None and spec.loader is not None, "DNSMOS", "unable to load pinned implementation")
	module = importlib.util.module_from_spec(spec)
	assert spec is not None and spec.loader is not None
	spec.loader.exec_module(module)
	return module, module.ComputeScore(str(primary_model), str(p808_model))


def _transcribe(model: Any, path: Path, language: str) -> str:
	segments, _ = model.transcribe(str(path), language=language, **TRANSCRIPTION_PARAMETERS)
	return normalize_text(" ".join(segment.text for segment in segments))


def _reference_binding(runtime: Mapping[str, Any], language: str, clean_record: Mapping[str, Any]) -> Mapping[str, Any]:
	model = runtime["models"][f"wer-{language}"]
	return {
		"clean_reference": clean_record,
		"language": language,
		"metrics_manifest_sha256": runtime["manifest"]["sha256"],
		"model": dict(model),
		"normalization": NORMALIZATION_ID,
		"runtime_id": runtime["id"],
		"runtime_version": runtime["version"],
		"transcription_parameters": TRANSCRIPTION_PARAMETERS,
	}


def _validate_clean_asr_reference(value: Any, binding: Mapping[str, Any]) -> Mapping[str, Any]:
	reference = _mapping(value, "clean ASR reference")
	_exact_keys(reference, {"binding", "kind", "normalized_transcript", "normalized_transcript_sha256", "private_text_do_not_publish", "schema_version", "word_count"}, "clean ASR reference")
	_expect(reference["schema_version"] == REFERENCE_SCHEMA_VERSION, "clean ASR reference.schema_version", "unsupported version")
	_expect(reference["kind"] == "clean-asr-consistency" and reference["private_text_do_not_publish"] is True, "clean ASR reference", "invalid kind or privacy marker")
	_expect(reference["binding"] == binding, "clean ASR reference.binding", "does not match the exact clean window and pinned runtime")
	text = reference["normalized_transcript"]
	_expect(isinstance(text, str) and text == normalize_text(text) and bool(text), "clean ASR reference.normalized_transcript", "must be non-empty canonical normalized text")
	_expect(hashlib.sha256(text.encode("utf-8")).hexdigest() == _hash(reference["normalized_transcript_sha256"], "clean ASR reference.normalized_transcript_sha256"), "clean ASR reference", "transcript hash mismatch")
	_expect(len(text.split()) == _integer(reference["word_count"], "clean ASR reference.word_count", 1), "clean ASR reference.word_count", "does not match transcript")
	return reference


def ensure_clean_asr_reference(
	path: Path,
	binding: Mapping[str, Any],
	transcribe_clean: Callable[[], str],
) -> tuple[str, Mapping[str, Any]]:
	if path.exists():
		reference = _validate_clean_asr_reference(_load_json(path, "clean ASR reference"), binding)
	else:
		text = normalize_text(transcribe_clean())
		_expect(bool(text), "clean ASR reference", "pinned ASR returned no words for the clean window")
		reference = {
			"schema_version": REFERENCE_SCHEMA_VERSION,
			"kind": "clean-asr-consistency",
			"private_text_do_not_publish": True,
			"binding": binding,
			"normalized_transcript": text,
			"normalized_transcript_sha256": hashlib.sha256(text.encode("utf-8")).hexdigest(),
			"word_count": len(text.split()),
		}
		path.parent.mkdir(parents=True, exist_ok=True)
		payload = json.dumps(reference, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
		try:
			with path.open("x", encoding="utf-8", newline="\n") as stream:
				stream.write(payload)
		except FileExistsError as error:
			raise ObjectiveScoreError(f"clean ASR reference appeared concurrently; rerun to validate it: {path}") from error
	return str(reference["normalized_transcript"]), {
		"sha256": sha256(path),
		"size_bytes": path.stat().st_size,
	}


def load_segment_ground_truth(
	transcript_path: Path,
	attestation_path: Path,
	clean_record: Mapping[str, Any],
	language: str,
) -> tuple[str, Mapping[str, Any], Mapping[str, Any]]:
	attestation = _mapping(_load_json(attestation_path, "segment transcript attestation"), "segment transcript attestation")
	_exact_keys(attestation, {"attestation", "clean_reference", "language", "schema_version", "source_segment", "transcript"}, "segment transcript attestation")
	_expect(attestation["schema_version"] == 1 and attestation["attestation"] == SEGMENT_ATTESTATION, "segment transcript attestation", "unsupported attestation")
	_expect(attestation["language"] == language, "segment transcript attestation.language", "language mismatch")
	_expect(attestation["clean_reference"] == clean_record, "segment transcript attestation.clean_reference", "does not bind the exact clean rendered window")
	segment = _mapping(attestation["source_segment"], "segment transcript attestation.source_segment")
	_exact_keys(segment, {"length_samples", "source_audio_sha256", "start_sample", "transcript_coverage_length_samples", "transcript_coverage_start_sample"}, "segment transcript attestation.source_segment")
	_hash(segment["source_audio_sha256"], "segment transcript attestation.source_segment.source_audio_sha256")
	start = _integer(segment["start_sample"], "segment transcript attestation.source_segment.start_sample")
	length = _integer(segment["length_samples"], "segment transcript attestation.source_segment.length_samples", 1)
	_expect(length == clean_record["frames"], "segment transcript attestation.source_segment.length_samples", "must equal the exact rendered clean-window length")
	_expect(segment["transcript_coverage_start_sample"] == start and segment["transcript_coverage_length_samples"] == length, "segment transcript attestation.source_segment", "transcript coverage must equal the exact source segment")
	transcript = _mapping(attestation["transcript"], "segment transcript attestation.transcript")
	_exact_keys(transcript, {"derivation", "normalization", "sha256", "size_bytes", "source_scope"}, "segment transcript attestation.transcript")
	_expect(transcript["source_scope"] == "exact-rendered-window", "segment transcript attestation.transcript.source_scope", "whole-utterance or unbounded transcripts are forbidden")
	_expect(transcript["derivation"] in ("human-segment-transcription", "forced-alignment-reviewed"), "segment transcript attestation.transcript.derivation", "unsupported derivation")
	_expect(transcript["normalization"] == NORMALIZATION_ID, "segment transcript attestation.transcript.normalization", "normalization mismatch")
	_expect(file_record(transcript_path) == {"sha256": transcript["sha256"], "size_bytes": transcript["size_bytes"]}, "segment transcript attestation.transcript", "file hash or size mismatch")
	try:
		raw_text = transcript_path.read_text(encoding="utf-8")
	except (OSError, UnicodeError) as error:
		raise ObjectiveScoreError(f"segment transcript: unable to read {transcript_path}: {error}") from error
	text = normalize_text(raw_text)
	_expect(bool(text), "segment transcript", "normalizes to zero words")
	return text, file_record(transcript_path), file_record(attestation_path)


_HOLDOUT_ARGUMENTS = (
	"release_holdout_opening_attestation",
	"release_holdout_opening_signature",
	"release_holdout_approval_public_key",
	"release_holdout_opening_root",
	"release_build",
	"expected_holdout_opening_sha256",
	"expected_holdout_plan_sha256",
	"expected_holdout_inventory_sha256",
	"expected_release_build_sha256",
	"expected_release_holdout_approval_public_key_sha256",
)


def _consume_release_holdout_opening(args: Any, *, now: datetime | None = None) -> Mapping[str, Any] | None:
	present = {name: getattr(args, name, None) for name in _HOLDOUT_ARGUMENTS}
	if args.dataset_split != RELEASE_HOLDOUT_SPLIT:
		unexpected = [f"--{name.replace('_', '-')}" for name, value in present.items() if value is not None]
		_expect(not unexpected, "release holdout opening", f"arguments are forbidden for {args.dataset_split}: {', '.join(unexpected)}")
		return None
	missing = [f"--{name.replace('_', '-')}" for name, value in present.items() if value is None]
	_expect(not missing, "release holdout opening", f"missing required arguments: {', '.join(missing)}")

	attestation_path = Path(present["release_holdout_opening_attestation"])
	signature_path = Path(present["release_holdout_opening_signature"])
	public_key_path = Path(present["release_holdout_approval_public_key"])
	release_build_path = Path(present["release_build"])
	for sensitive_path, label in (
		(attestation_path, "release holdout opening attestation path"),
		(signature_path, "release holdout approval signature path"),
		(public_key_path, "release holdout approval public-key path"),
		(release_build_path, "release holdout release-build path"),
		(Path(present["release_holdout_opening_root"]), "release holdout opening root path"),
	):
		_expect(sensitive_path.is_absolute(), label, "must be absolute")
	attestation_bytes, attestation_record = _read_regular_bytes(attestation_path, "release holdout opening attestation")
	_expect(attestation_record["sha256"] == _hash(present["expected_holdout_opening_sha256"], "expected holdout opening SHA-256"), "release holdout opening attestation", "does not match the externally approved attestation hash")
	attestation = _mapping(_load_json_bytes(attestation_bytes, "release holdout opening attestation", attestation_path), "release holdout opening attestation")
	_exact_keys(attestation, {
		"attestation", "authorization", "authorized_at_utc", "dataset_split", "expected",
		"opening_report_relative_path", "purpose", "receipt_relative_path", "run_id", "schema_version", "valid_until_utc",
	}, "release holdout opening attestation")
	_expect(attestation["schema_version"] == 1 and attestation["attestation"] == HOLDOUT_ATTESTATION_ID, "release holdout opening attestation", "unsupported schema or attestation id")
	_expect(attestation["dataset_split"] == RELEASE_HOLDOUT_SPLIT, "release holdout opening attestation.dataset_split", "must be release-holdout")
	_expect(attestation["purpose"] == HOLDOUT_PURPOSE, "release holdout opening attestation.purpose", "is not an explicit final community-release qualification authorization")
	run_id = attestation["run_id"]
	_expect(isinstance(run_id, str) and bool(UUID4_RE.fullmatch(run_id)), "release holdout opening attestation.run_id", "must be a canonical lowercase UUIDv4")
	try:
		_expect(str(uuid.UUID(run_id)) == run_id and uuid.UUID(run_id).version == 4, "release holdout opening attestation.run_id", "must be a canonical UUIDv4")
	except ValueError as error:
		raise ObjectiveScoreError("release holdout opening attestation.run_id: invalid UUID") from error

	authorized_at = _parse_utc_timestamp(attestation["authorized_at_utc"], "release holdout opening attestation.authorized_at_utc")
	valid_until = _parse_utc_timestamp(attestation["valid_until_utc"], "release holdout opening attestation.valid_until_utc")
	current = now or datetime.now(timezone.utc)
	_expect(current.tzinfo is not None, "release holdout opening clock", "must be timezone-aware")
	current = current.astimezone(timezone.utc)
	_expect(authorized_at <= current + HOLDOUT_MAX_CLOCK_SKEW, "release holdout opening attestation.authorized_at_utc", "is too far in the future")
	_expect(valid_until > current, "release holdout opening attestation.valid_until_utc", "authorization has expired")
	_expect(authorized_at < valid_until and valid_until - authorized_at <= HOLDOUT_MAX_AUTHORIZATION_WINDOW, "release holdout opening attestation validity", "must be positive and no longer than 24 hours")

	expected = _mapping(attestation["expected"], "release holdout opening attestation.expected")
	_exact_keys(expected, {"corpus_inventory_sha256", "mixture_plan_sha256", "release_build"}, "release holdout opening attestation.expected")
	plan_hash = _hash(expected["mixture_plan_sha256"], "release holdout opening attestation.expected.mixture_plan_sha256")
	inventory_hash = _hash(expected["corpus_inventory_sha256"], "release holdout opening attestation.expected.corpus_inventory_sha256")
	_expect(plan_hash == _hash(present["expected_holdout_plan_sha256"], "expected holdout plan SHA-256"), "release holdout opening attestation.expected.mixture_plan_sha256", "does not match the protected external plan hash")
	_expect(inventory_hash == _hash(present["expected_holdout_inventory_sha256"], "expected holdout inventory SHA-256"), "release holdout opening attestation.expected.corpus_inventory_sha256", "does not match the protected external inventory hash")
	expected_build = _mapping(expected["release_build"], "release holdout opening attestation.expected.release_build")
	_exact_keys(expected_build, {"sha256", "size_bytes"}, "release holdout opening attestation.expected.release_build")
	_hash(expected_build["sha256"], "release holdout opening attestation.expected.release_build.sha256")
	_integer(expected_build["size_bytes"], "release holdout opening attestation.expected.release_build.size_bytes", 1)
	release_build_record = file_record(release_build_path)
	_expect(release_build_record == expected_build, "release holdout opening attestation.expected.release_build", "does not identify the exact release build file")
	_expect(release_build_record["sha256"] == _hash(present["expected_release_build_sha256"], "expected release build SHA-256"), "release holdout opening attestation.expected.release_build.sha256", "does not match the protected external release-build hash")

	authorization = _mapping(attestation["authorization"], "release holdout opening attestation.authorization")
	_exact_keys(authorization, {"key_id", "kind", "public_key_sha256", "signature_encoding"}, "release holdout opening attestation.authorization")
	_expect(authorization["kind"] == HOLDOUT_AUTHORIZATION_KIND and authorization["signature_encoding"] == HOLDOUT_SIGNATURE_ENCODING, "release holdout opening attestation.authorization", "unsupported authorization mechanism")
	_expect(isinstance(authorization["key_id"], str) and bool(IDENTIFIER_RE.fullmatch(authorization["key_id"])), "release holdout opening attestation.authorization.key_id", "invalid key id")
	public_key, public_key_record = _read_regular_bytes(public_key_path, "release holdout approval public key")
	_expect(
		public_key_record["sha256"] == _hash(
			present["expected_release_holdout_approval_public_key_sha256"],
			"expected release holdout approval public-key SHA-256",
		),
		"release holdout approval public key",
		"does not match the externally pinned release-owner trust root",
	)
	_expect(public_key_record["sha256"] == _hash(authorization["public_key_sha256"], "release holdout opening attestation.authorization.public_key_sha256"), "release holdout approval public key", "hash mismatch")
	_expect(len(public_key) == 32, "release holdout approval public key", "must contain exactly 32 raw Ed25519 bytes")
	signature, signature_record = _read_regular_bytes(signature_path, "release holdout approval signature")
	_verify_ed25519(public_key, canonical_json_bytes(attestation), signature)

	opening_root = _existing_directory(Path(present["release_holdout_opening_root"]), "release holdout opening root")
	receipts_root = _existing_directory(opening_root / "receipts", "release holdout receipts directory")
	reports_root = _existing_directory(opening_root / "reports", "release holdout reports directory")
	receipt_relative = _safe_relative(attestation["receipt_relative_path"], "release holdout opening attestation.receipt_relative_path")
	report_relative = _safe_relative(attestation["opening_report_relative_path"], "release holdout opening attestation.opening_report_relative_path")
	_expect(receipt_relative == f"receipts/{run_id}.json", "release holdout opening attestation.receipt_relative_path", "must be the unique run-id receipt path")
	_expect(report_relative == f"reports/{run_id}.json", "release holdout opening attestation.opening_report_relative_path", "must be the unique run-id report path")
	receipt_path = _path_below(opening_root, receipt_relative, "release holdout receipt path")
	report_path = _path_below(opening_root, report_relative, "release holdout report path")
	_expect(receipt_path.parent.resolve() == receipts_root and report_path.parent.resolve() == reports_root, "release holdout opening paths", "must use the protected receipt/report directories")
	_expect(not os.path.lexists(receipt_path), "release holdout receipt", "run id was already consumed or path is occupied")
	_expect(not os.path.lexists(report_path), "release holdout opening report", "run id report is already occupied")

	consumed_at = current.strftime("%Y-%m-%dT%H:%M:%SZ")
	receipt_document = {
		"schema_version": 1,
		"receipt": HOLDOUT_RECEIPT_ID,
		"status": "opening-consumed-before-audio-read",
		"run_id": run_id,
		"purpose": HOLDOUT_PURPOSE,
		"consumed_at_utc": consumed_at,
		"attestation": attestation_record,
		"detached_signature": signature_record,
		"approval_public_key": public_key_record,
		"corpus_inventory_sha256": inventory_hash,
		"mixture_plan_sha256": plan_hash,
		"release_build": release_build_record,
	}
	receipt_record = _write_new_json(receipt_path, receipt_document, "release holdout receipt")
	report_document = {
		"schema_version": 1,
		"opening_report": HOLDOUT_OPENING_REPORT_ID,
		"status": "opening-consumed-before-audio-read",
		"run_id": run_id,
		"purpose": HOLDOUT_PURPOSE,
		"authorized_at_utc": attestation["authorized_at_utc"],
		"valid_until_utc": attestation["valid_until_utc"],
		"consumed_at_utc": consumed_at,
		"corpus_inventory_sha256": inventory_hash,
		"mixture_plan_sha256": plan_hash,
		"release_build": release_build_record,
		"attestation": attestation_record,
		"detached_signature": signature_record,
		"approval_public_key": public_key_record,
		"receipt": receipt_record,
	}
	report_record = _write_new_json(report_path, report_document, "release holdout opening report")
	return {
		"run_id": run_id,
		"purpose": HOLDOUT_PURPOSE,
		"authorized_at_utc": attestation["authorized_at_utc"],
		"valid_until_utc": attestation["valid_until_utc"],
		"corpus_inventory_sha256": inventory_hash,
		"mixture_plan_sha256": plan_hash,
		"release_build": release_build_record,
		"attestation": attestation_record,
		"detached_signature": signature_record,
		"approval_public_key": public_key_record,
		"receipt": receipt_record,
		"opening_report": report_record,
	}


def validate_score_document(value: Any) -> Mapping[str, Any]:
	root = _mapping(value, "objective score")
	root_keys = {"alignment", "candidate_minus_original", "case_id", "condition", "dataset_split", "inputs", "metrics", "profile", "runtime", "schema_version", "scorer", "scorer_files", "status", "wer_reference"}
	if root.get("dataset_split") == RELEASE_HOLDOUT_SPLIT:
		root_keys.add("release_holdout_opening")
	_exact_keys(root, root_keys, "objective score")
	_expect(root["schema_version"] == SCHEMA_VERSION and root["scorer"] == SCORER_ID and root["status"] == "passed", "objective score", "unsupported or non-passing score document")
	_expect(isinstance(root["case_id"], str) and bool(IDENTIFIER_RE.fullmatch(root["case_id"])), "objective score.case_id", "invalid identifier")
	_expect(root["profile"] in ALLOWED_PROFILES, "objective score.profile", "unsupported profile")
	_expect(root["condition"] in ALLOWED_CONDITIONS, "objective score.condition", "unsupported condition")
	_expect(root["dataset_split"] in ALLOWED_DATASET_SPLITS, "objective score.dataset_split", "unknown or unqualified holdout split")
	opening: Mapping[str, Any] | None = None
	if root["dataset_split"] == RELEASE_HOLDOUT_SPLIT:
		opening = _mapping(root["release_holdout_opening"], "objective score.release_holdout_opening")
		_exact_keys(opening, {
			"approval_public_key", "attestation", "authorized_at_utc", "corpus_inventory_sha256", "detached_signature",
			"mixture_plan_sha256", "opening_report", "purpose", "receipt", "release_build", "run_id", "valid_until_utc",
		}, "objective score.release_holdout_opening")
		_expect(opening["purpose"] == HOLDOUT_PURPOSE, "objective score.release_holdout_opening.purpose", "unsupported purpose")
		_expect(isinstance(opening["run_id"], str) and bool(UUID4_RE.fullmatch(opening["run_id"])), "objective score.release_holdout_opening.run_id", "must be canonical UUIDv4")
		authorized_at = _parse_utc_timestamp(opening["authorized_at_utc"], "objective score.release_holdout_opening.authorized_at_utc")
		valid_until = _parse_utc_timestamp(opening["valid_until_utc"], "objective score.release_holdout_opening.valid_until_utc")
		_expect(authorized_at < valid_until and valid_until - authorized_at <= HOLDOUT_MAX_AUTHORIZATION_WINDOW, "objective score.release_holdout_opening validity", "must be positive and no longer than 24 hours")
		for hash_name in ("corpus_inventory_sha256", "mixture_plan_sha256"):
			_hash(opening[hash_name], f"objective score.release_holdout_opening.{hash_name}")
		for file_name in ("approval_public_key", "attestation", "detached_signature", "opening_report", "receipt", "release_build"):
			record = _mapping(opening[file_name], f"objective score.release_holdout_opening.{file_name}")
			_exact_keys(record, {"sha256", "size_bytes"}, f"objective score.release_holdout_opening.{file_name}")
			_hash(record["sha256"], f"objective score.release_holdout_opening.{file_name}.sha256")
			_integer(record["size_bytes"], f"objective score.release_holdout_opening.{file_name}.size_bytes", 1)
	alignment = _mapping(root["alignment"], "objective score.alignment")
	_exact_keys(alignment, {"candidate_latency_samples", "candidate_window_start_samples", "correlation_search_used", "method", "original_latency_samples", "original_window_start_samples", "qualified_route_binding", "reference_samples", "sample_rate_hz", "signal_stage"}, "objective score.alignment")
	_expect(alignment["method"] == "caller-declared-fixed-latency" and alignment["correlation_search_used"] is False, "objective score.alignment", "release gates forbid correlation alignment")
	_expect(alignment["sample_rate_hz"] == SAMPLE_RATE_HZ, "objective score.alignment.sample_rate_hz", "unsupported rate")
	_expect(alignment["signal_stage"] in SIGNAL_STAGES, "objective score.alignment.signal_stage", "unsupported stage")
	for key in ("candidate_latency_samples", "candidate_window_start_samples", "original_latency_samples", "original_window_start_samples", "reference_samples"):
		_integer(alignment[key], f"objective score.alignment.{key}", 1 if key == "reference_samples" else 0)
	if alignment["signal_stage"] == "sender-pre-opus":
		_expect(alignment["qualified_route_binding"] is None, "objective score.alignment.qualified_route_binding", "must be null before Opus")
		_expect(alignment["original_window_start_samples"] == alignment["original_latency_samples"] and alignment["candidate_window_start_samples"] == alignment["candidate_latency_samples"], "objective score.alignment", "pre-Opus window starts must equal declared latencies")
	else:
		binding = _mapping(alignment["qualified_route_binding"], "objective score.alignment.qualified_route_binding")
		_exact_keys(binding, {"candidate_fixed_timeline_score", "control_fixed_timeline_score", "control_wav", "e2e_manifest", "edge_tail_gate", "route_offset_samples", "stable_execution_identity"}, "objective score.alignment.qualified_route_binding")
		route_offset = _integer(binding["route_offset_samples"], "objective score.alignment.qualified_route_binding.route_offset_samples")
		_expect(route_offset % (SAMPLE_RATE_HZ // 100) == 0, "objective score.alignment.qualified_route_binding.route_offset_samples", "must be frame aligned")
		_expect(alignment["original_window_start_samples"] == route_offset + alignment["original_latency_samples"] and alignment["candidate_window_start_samples"] == route_offset + alignment["candidate_latency_samples"], "objective score.alignment", "receiver windows must preserve route offset plus declared enhancement latency")
		for file_name in ("candidate_fixed_timeline_score", "control_fixed_timeline_score", "control_wav", "e2e_manifest"):
			record = _mapping(binding[file_name], f"objective score.alignment.qualified_route_binding.{file_name}")
			_exact_keys(record, {"sha256", "size_bytes"}, f"objective score.alignment.qualified_route_binding.{file_name}")
			_hash(record["sha256"], f"objective score.alignment.qualified_route_binding.{file_name}.sha256")
			_integer(record["size_bytes"], f"objective score.alignment.qualified_route_binding.{file_name}.size_bytes", 1)
		identity = _mapping(binding["stable_execution_identity"], "objective score.alignment.qualified_route_binding.stable_execution_identity")
		identity_keys = {"client_binary_sha256", "model_manifest_sha256", "recipe_manifest_sha256", "run_provenance_sha256", "runtime_payload_sha256", "server_binary_sha256"}
		_exact_keys(identity, identity_keys, "objective score.alignment.qualified_route_binding.stable_execution_identity")
		for key in identity_keys:
			_hash(identity[key], f"objective score.alignment.qualified_route_binding.stable_execution_identity.{key}")
		gate = _mapping(binding["edge_tail_gate"], "objective score.alignment.qualified_route_binding.edge_tail_gate")
		_exact_keys(gate, {"candidate_passed", "control_passed", "pre_opus_complete_tail_required", "pre_opus_max_end_loss_samples", "pre_opus_max_onset_loss_samples"}, "objective score.alignment.qualified_route_binding.edge_tail_gate")
		_expect(gate["candidate_passed"] is True and gate["control_passed"] is True and gate["pre_opus_complete_tail_required"] is True, "objective score.alignment.qualified_route_binding.edge_tail_gate", "all independent gates must pass")
		for key in ("pre_opus_max_end_loss_samples", "pre_opus_max_onset_loss_samples"):
			_expect(_integer(gate[key], f"objective score.alignment.qualified_route_binding.edge_tail_gate.{key}") <= SAMPLE_RATE_HZ // 100, f"objective score.alignment.qualified_route_binding.edge_tail_gate.{key}", "exceeds one frame")
	if opening is not None:
		_expect(alignment["signal_stage"] == "receiver-capture", "objective score.release_holdout_opening", "release holdout must use the qualified client-1/server/client-2 receiver path")
		identity = _mapping(alignment["qualified_route_binding"]["stable_execution_identity"], "objective score.release_holdout_opening route identity")
		_expect(identity["client_binary_sha256"] == opening["release_build"]["sha256"], "objective score.release_holdout_opening.release_build.sha256", "does not match the exact E2E client binary")
	inputs = _mapping(root["inputs"], "objective score.inputs")
	_exact_keys(inputs, {"candidate", "clean_reference", "noisy_original"}, "objective score.inputs")
	for role in ("clean_reference", "noisy_original", "candidate"):
		record = _mapping(inputs[role], f"objective score.inputs.{role}")
		_exact_keys(record, {"channels", "frames", "sample_rate_hz", "sha256", "size_bytes"}, f"objective score.inputs.{role}")
		_expect(record["channels"] == 1 and record["sample_rate_hz"] == SAMPLE_RATE_HZ, f"objective score.inputs.{role}", "must be mono 48 kHz")
		_integer(record["frames"], f"objective score.inputs.{role}.frames", 1)
		_integer(record["size_bytes"], f"objective score.inputs.{role}.size_bytes", 1)
		_hash(record["sha256"], f"objective score.inputs.{role}.sha256")
	_expect(inputs["clean_reference"]["frames"] == alignment["reference_samples"], "objective score.inputs.clean_reference.frames", "does not match alignment")
	_expect(inputs["noisy_original"]["frames"] >= alignment["original_window_start_samples"] + alignment["reference_samples"], "objective score.inputs.noisy_original.frames", "cannot contain the fixed scoring window")
	_expect(inputs["candidate"]["frames"] >= alignment["candidate_window_start_samples"] + alignment["reference_samples"], "objective score.inputs.candidate.frames", "cannot contain the fixed scoring window")

	runtime = _mapping(root["runtime"], "objective score.runtime")
	_exact_keys(runtime, {"assets_tree_sha256", "distributions_tree_sha256", "id", "inventory", "legacy_local_scorer_pin", "lock", "manifest", "models", "sources", "version", "whisper_tree_sha256"}, "objective score.runtime")
	_expect(isinstance(runtime["id"], str) and bool(runtime["id"]) and isinstance(runtime["version"], str) and bool(runtime["version"]), "objective score.runtime", "runtime id/version are required")
	for tree_name in ("assets_tree_sha256", "distributions_tree_sha256", "whisper_tree_sha256"):
		_hash(runtime[tree_name], f"objective score.runtime.{tree_name}")
	for file_name in ("manifest", "lock", "inventory", "legacy_local_scorer_pin"):
		record = _mapping(runtime[file_name], f"objective score.runtime.{file_name}")
		_exact_keys(record, {"relative_path", "sha256", "size_bytes"}, f"objective score.runtime.{file_name}")
		_safe_relative(record["relative_path"], f"objective score.runtime.{file_name}.relative_path")
		_hash(record["sha256"], f"objective score.runtime.{file_name}.sha256")
		_integer(record["size_bytes"], f"objective score.runtime.{file_name}.size_bytes", 1)
	sources = _mapping(runtime["sources"], "objective score.runtime.sources")
	_exact_keys(sources, {"dnsmos", "wer"}, "objective score.runtime.sources")
	for source_name in ("dnsmos", "wer"):
		source = _mapping(sources[source_name], f"objective score.runtime.sources.{source_name}")
		_exact_keys(source, {"repository", "revision"}, f"objective score.runtime.sources.{source_name}")
		_expect(isinstance(source["repository"], str) and bool(source["repository"]), f"objective score.runtime.sources.{source_name}.repository", "required")
		_expect(isinstance(source["revision"], str) and bool(re.fullmatch(r"[0-9a-f]{40}", source["revision"])), f"objective score.runtime.sources.{source_name}.revision", "must be a pinned commit")
	models = _mapping(runtime["models"], "objective score.runtime.models")
	_exact_keys(models, {"dnsmos", "estoi", "wer-en", "wer-sv"}, "objective score.runtime.models")
	for model_id, value in models.items():
		record = _mapping(value, f"objective score.runtime.models.{model_id}")
		_exact_keys(record, {"id", "relative_path", "sha256", "size_bytes"}, f"objective score.runtime.models.{model_id}")
		_expect(record["id"] == model_id, f"objective score.runtime.models.{model_id}.id", "model id mismatch")
		_safe_relative(record["relative_path"], f"objective score.runtime.models.{model_id}.relative_path")
		_hash(record["sha256"], f"objective score.runtime.models.{model_id}.sha256")
		_integer(record["size_bytes"], f"objective score.runtime.models.{model_id}.size_bytes", 1)
	scorer_files = _mapping(root["scorer_files"], "objective score.scorer_files")
	_exact_keys(scorer_files, {"cli", "implementation"}, "objective score.scorer_files")
	for file_name in ("cli", "implementation"):
		record = _mapping(scorer_files[file_name], f"objective score.scorer_files.{file_name}")
		_exact_keys(record, {"name", "sha256", "size_bytes"}, f"objective score.scorer_files.{file_name}")
		_expect(isinstance(record["name"], str) and bool(record["name"]) and "/" not in record["name"] and "\\" not in record["name"], f"objective score.scorer_files.{file_name}.name", "must be a base filename")
		_hash(record["sha256"], f"objective score.scorer_files.{file_name}.sha256")
		_integer(record["size_bytes"], f"objective score.scorer_files.{file_name}.size_bytes", 1)
	reference = _mapping(root["wer_reference"], "objective score.wer_reference")
	_exact_keys(reference, {"artifact", "attestation", "kind", "label", "language", "normalization", "text_sha256", "word_count"}, "objective score.wer_reference")
	_expect(reference["kind"] in ("clean-asr-consistency", "segment-ground-truth"), "objective score.wer_reference.kind", "unsupported reference")
	expected_label = "clean-ASR-consistency WER" if reference["kind"] == "clean-asr-consistency" else "segment-ground-truth WER"
	_expect(reference["label"] == expected_label, "objective score.wer_reference.label", "must clearly identify the WER semantics")
	_expect(reference["language"] in ALLOWED_LANGUAGES and reference["normalization"] == NORMALIZATION_ID, "objective score.wer_reference", "language or normalization mismatch")
	_hash(reference["text_sha256"], "objective score.wer_reference.text_sha256")
	_integer(reference["word_count"], "objective score.wer_reference.word_count", 1)
	for artifact_name in ("artifact", "attestation"):
		artifact = reference[artifact_name]
		if artifact is None:
			_expect(artifact_name == "attestation" and reference["kind"] == "clean-asr-consistency", f"objective score.wer_reference.{artifact_name}", "may only be null for clean-ASR consistency")
		else:
			artifact_value = _mapping(artifact, f"objective score.wer_reference.{artifact_name}")
			_exact_keys(artifact_value, {"sha256", "size_bytes"}, f"objective score.wer_reference.{artifact_name}")
			_hash(artifact_value["sha256"], f"objective score.wer_reference.{artifact_name}.sha256")
			_integer(artifact_value["size_bytes"], f"objective score.wer_reference.{artifact_name}.size_bytes", 1)
	metrics = _mapping(root["metrics"], "objective score.metrics")
	_exact_keys(metrics, {"candidate", "original"}, "objective score.metrics")
	for role in ("original", "candidate"):
		entry = _mapping(metrics[role], f"objective score.metrics.{role}")
		_exact_keys(entry, {"dnsmos_bak", "dnsmos_ovrl", "dnsmos_sig", "estoi", "wer"}, f"objective score.metrics.{role}")
		for key in ("dnsmos_bak", "dnsmos_ovrl", "dnsmos_sig", "estoi"):
			_number(entry[key], f"objective score.metrics.{role}.{key}")
		wer = _mapping(entry["wer"], f"objective score.metrics.{role}.wer")
		_exact_keys(wer, {"errors", "hypothesis_sha256", "rate", "reference_words"}, f"objective score.metrics.{role}.wer")
		_integer(wer["errors"], f"objective score.metrics.{role}.wer.errors")
		_expect(_integer(wer["reference_words"], f"objective score.metrics.{role}.wer.reference_words", 1) == reference["word_count"], f"objective score.metrics.{role}.wer.reference_words", "reference mismatch")
		rate = _number(wer["rate"], f"objective score.metrics.{role}.wer.rate")
		_expect(math.isclose(rate, wer["errors"] / wer["reference_words"], rel_tol=1e-12, abs_tol=1e-12), f"objective score.metrics.{role}.wer.rate", "does not match errors/reference_words")
		_hash(wer["hypothesis_sha256"], f"objective score.metrics.{role}.wer.hypothesis_sha256")
	deltas = _mapping(root["candidate_minus_original"], "objective score.candidate_minus_original")
	_exact_keys(deltas, {"dnsmos_bak", "dnsmos_ovrl", "dnsmos_sig", "estoi", "wer_delta_kind", "wer_delta_percentage_points"}, "objective score.candidate_minus_original")
	for key in ("dnsmos_bak", "dnsmos_ovrl", "dnsmos_sig", "estoi", "wer_delta_percentage_points"):
		_number(deltas[key], f"objective score.candidate_minus_original.{key}")
	_expect(deltas["wer_delta_kind"] == reference["kind"], "objective score.candidate_minus_original.wer_delta_kind", "reference mismatch")
	expected_deltas = {
		"dnsmos_bak": metrics["candidate"]["dnsmos_bak"] - metrics["original"]["dnsmos_bak"],
		"dnsmos_ovrl": metrics["candidate"]["dnsmos_ovrl"] - metrics["original"]["dnsmos_ovrl"],
		"dnsmos_sig": metrics["candidate"]["dnsmos_sig"] - metrics["original"]["dnsmos_sig"],
		"estoi": metrics["candidate"]["estoi"] - metrics["original"]["estoi"],
		"wer_delta_percentage_points": (metrics["candidate"]["wer"]["rate"] - metrics["original"]["wer"]["rate"]) * 100.0,
	}
	for name, expected in expected_deltas.items():
		_expect(math.isclose(float(deltas[name]), float(expected), rel_tol=1e-9, abs_tol=1e-9), f"objective score.candidate_minus_original.{name}", f"does not match original/candidate metrics ({expected!r})")
	return root


def _score(args: Any) -> Mapping[str, Any]:
	_expect(args.dataset_split in ALLOWED_DATASET_SPLITS, "dataset split", "unknown or unqualified holdout split")
	_expect(args.language in ALLOWED_LANGUAGES, "language", "unsupported language")
	_expect(args.profile in ALLOWED_PROFILES, "profile", "unsupported profile")
	_expect(args.condition in ALLOWED_CONDITIONS, "condition", "unsupported condition")
	_expect(bool(IDENTIFIER_RE.fullmatch(args.case_id)), "case id", "invalid identifier")
	_expect(args.original_latency_samples >= 0 and args.candidate_latency_samples >= 0, "latency", "must be non-negative caller declarations")
	runtime = verify_metrics_runtime(args.metrics_runtime_root, args.metrics_manifest)
	# This validation and atomic one-shot receipt happen before any WAV is read.
	# A failed scoring run therefore consumes its explicit holdout authorization
	# instead of silently permitting a second look at the protected split.
	release_holdout_opening = _consume_release_holdout_opening(args)
	clean, rate, clean_channels = _read_audio(args.clean_reference)
	original_raw, original_rate, original_channels = _read_audio(args.noisy_original)
	candidate_raw, candidate_rate, candidate_channels = _read_audio(args.candidate)
	_expect(rate == original_rate == candidate_rate, "audio", "sample-rate mismatch")
	clean_record = _audio_record(args.clean_reference, clean, rate, clean_channels)
	input_records = {
		"clean_reference": clean_record,
		"noisy_original": _audio_record(args.noisy_original, original_raw, original_rate, original_channels),
		"candidate": _audio_record(args.candidate, candidate_raw, candidate_rate, candidate_channels),
	}
	original_start, candidate_start, route_binding = _timeline_alignment(args, clean_record, input_records)
	if release_holdout_opening is not None:
		_expect(args.signal_stage == "receiver-capture" and route_binding is not None, "release holdout opening", "release holdout must traverse the qualified client-1/server/client-2 receiver path")
		_expect(route_binding["stable_execution_identity"]["client_binary_sha256"] == release_holdout_opening["release_build"]["sha256"], "release holdout opening.release_build.sha256", "does not match the exact E2E client binary")
	original = _fixed_window(original_raw, original_start, len(clean), "Original WAV")
	candidate = _fixed_window(candidate_raw, candidate_start, len(clean), "candidate WAV")

	try:
		import numpy as np
		import soundfile as sf
		from faster_whisper import WhisperModel
		from pystoi import stoi
	except ImportError as error:
		raise ObjectiveScoreError("the pinned scoring runtime is incomplete") from error
	model_record = runtime["models"][f"wer-{args.language}"]
	model_root = runtime["runtime_root"] / PurePosixPath(str(model_record["relative_path"])).parent
	model = WhisperModel(str(model_root), device="cpu", compute_type="int8", local_files_only=True, cpu_threads=4, num_workers=1)
	binding = _reference_binding(runtime, args.language, clean_record)

	with tempfile.TemporaryDirectory(prefix="mumble-objective-score-") as directory:
		temporary = Path(directory)
		aligned_original = temporary / "original-fixed-window.wav"
		aligned_candidate = temporary / "candidate-fixed-window.wav"
		sf.write(aligned_original, np.asarray(original, dtype=np.float32), rate, subtype="FLOAT")
		sf.write(aligned_candidate, np.asarray(candidate, dtype=np.float32), rate, subtype="FLOAT")
		if args.wer_reference_kind == "clean-asr-consistency":
			_expect(args.clean_asr_reference is not None, "clean ASR reference", "--clean-asr-reference is required")
			_expect(args.segment_transcript is None and args.segment_transcript_attestation is None, "WER reference", "segment transcript arguments are forbidden in clean-ASR mode")
			reference_text, reference_artifact = ensure_clean_asr_reference(
				args.clean_asr_reference,
				binding,
				lambda: _transcribe(model, args.clean_reference, args.language),
			)
			attestation_artifact = None
		else:
			_expect(args.clean_asr_reference is None, "WER reference", "clean-ASR reference is forbidden in segment-ground-truth mode")
			_expect(args.segment_transcript is not None and args.segment_transcript_attestation is not None, "segment ground truth", "transcript and exact-window attestation are both required")
			reference_text, reference_artifact, attestation_artifact = load_segment_ground_truth(
				args.segment_transcript, args.segment_transcript_attestation, clean_record, args.language
			)
		original_text = _transcribe(model, aligned_original, args.language)
		candidate_text = _transcribe(model, aligned_candidate, args.language)
		original_errors, reference_words, original_wer = word_error(reference_text, original_text)
		candidate_errors, _, candidate_wer = word_error(reference_text, candidate_text)

		dnsmos_script_record = runtime["legacy_local_scorer_pin"]
		_ = dnsmos_script_record  # keep the old scorer pin visible but never execute it
		dnsmos_script = runtime["runtime_root"] / "dnsmos_local.py"
		primary_model = runtime["runtime_root"] / runtime["models"]["dnsmos"]["relative_path"]
		p808_model = runtime["runtime_root"] / "model_v8.onnx"
		module, dnsmos = _load_dnsmos(dnsmos_script, primary_model, p808_model)
		original_mos = dnsmos(str(aligned_original), module.SAMPLING_RATE, False)
		candidate_mos = dnsmos(str(aligned_candidate), module.SAMPLING_RATE, False)
		original_estoi = float(stoi(clean, original, rate, extended=True))
		candidate_estoi = float(stoi(clean, candidate, rate, extended=True))

	metrics = {
		"original": {
			"dnsmos_ovrl": _stable_metric(original_mos["OVRL"]),
			"dnsmos_sig": _stable_metric(original_mos["SIG"]),
			"dnsmos_bak": _stable_metric(original_mos["BAK"]),
			"estoi": _stable_metric(original_estoi),
			"wer": {
				"errors": original_errors,
				"reference_words": reference_words,
				"rate": _stable_metric(original_wer),
				"hypothesis_sha256": hashlib.sha256(original_text.encode("utf-8")).hexdigest(),
			},
		},
		"candidate": {
			"dnsmos_ovrl": _stable_metric(candidate_mos["OVRL"]),
			"dnsmos_sig": _stable_metric(candidate_mos["SIG"]),
			"dnsmos_bak": _stable_metric(candidate_mos["BAK"]),
			"estoi": _stable_metric(candidate_estoi),
			"wer": {
				"errors": candidate_errors,
				"reference_words": reference_words,
				"rate": _stable_metric(candidate_wer),
				"hypothesis_sha256": hashlib.sha256(candidate_text.encode("utf-8")).hexdigest(),
			},
		},
	}
	implementation_path = Path(__file__).resolve()
	cli_path = Path(args.scorer_cli).resolve()
	runtime_public = {key: value for key, value in runtime.items() if key != "runtime_root"}
	document = {
		"schema_version": SCHEMA_VERSION,
		"scorer": SCORER_ID,
		"status": "passed",
		"case_id": args.case_id,
		"profile": args.profile,
		"condition": args.condition,
		"dataset_split": args.dataset_split,
		"alignment": {
			"method": "caller-declared-fixed-latency",
			"correlation_search_used": False,
			"signal_stage": args.signal_stage,
			"sample_rate_hz": rate,
			"reference_samples": len(clean),
			"original_latency_samples": args.original_latency_samples,
			"candidate_latency_samples": args.candidate_latency_samples,
			"original_window_start_samples": original_start,
			"candidate_window_start_samples": candidate_start,
			"qualified_route_binding": route_binding,
		},
		"inputs": input_records,
		"runtime": runtime_public,
		"scorer_files": {
			"cli": {**file_record(cli_path), "name": cli_path.name},
			"implementation": {**file_record(implementation_path), "name": implementation_path.name},
		},
		"wer_reference": {
			"kind": args.wer_reference_kind,
			"label": "clean-ASR-consistency WER" if args.wer_reference_kind == "clean-asr-consistency" else "segment-ground-truth WER",
			"language": args.language,
			"normalization": NORMALIZATION_ID,
			"text_sha256": hashlib.sha256(reference_text.encode("utf-8")).hexdigest(),
			"word_count": reference_words,
			"artifact": reference_artifact,
			"attestation": attestation_artifact,
		},
		"metrics": metrics,
		"candidate_minus_original": {
			"dnsmos_ovrl": _stable_metric(metrics["candidate"]["dnsmos_ovrl"] - metrics["original"]["dnsmos_ovrl"]),
			"dnsmos_sig": _stable_metric(metrics["candidate"]["dnsmos_sig"] - metrics["original"]["dnsmos_sig"]),
			"dnsmos_bak": _stable_metric(metrics["candidate"]["dnsmos_bak"] - metrics["original"]["dnsmos_bak"]),
			"estoi": _stable_metric(metrics["candidate"]["estoi"] - metrics["original"]["estoi"]),
			"wer_delta_percentage_points": _stable_metric((metrics["candidate"]["wer"]["rate"] - metrics["original"]["wer"]["rate"]) * 100.0),
			"wer_delta_kind": args.wer_reference_kind,
		},
	}
	if release_holdout_opening is not None:
		document["release_holdout_opening"] = release_holdout_opening
	validate_score_document(document)
	return document


def _sample_score() -> Mapping[str, Any]:
	hash_value = "1" * 64
	file_value = {"sha256": hash_value, "size_bytes": 1}
	audio_value = {"sha256": hash_value, "size_bytes": 1, "channels": 1, "frames": 50400, "sample_rate_hz": SAMPLE_RATE_HZ}
	relative_file = {"relative_path": "pinned/file.bin", "sha256": hash_value, "size_bytes": 1}
	model_values = {
		model_id: {"id": model_id, "relative_path": f"models/{model_id}.bin", "sha256": hash_value, "size_bytes": 1}
		for model_id in ("dnsmos", "estoi", "wer-en", "wer-sv")
	}
	runtime = {
		"id": "self-test-runtime",
		"version": "1",
		"manifest": dict(relative_file),
		"lock": dict(relative_file),
		"inventory": dict(relative_file),
		"sources": {
			"dnsmos": {"repository": "microsoft/DNS-Challenge", "revision": "1" * 40},
			"wer": {"repository": "Systran/faster-whisper-small", "revision": "2" * 40},
		},
		"assets_tree_sha256": hash_value,
		"distributions_tree_sha256": hash_value,
		"whisper_tree_sha256": hash_value,
		"models": model_values,
		"legacy_local_scorer_pin": dict(relative_file),
	}
	metric = {
		"dnsmos_bak": 2.0,
		"dnsmos_ovrl": 2.0,
		"dnsmos_sig": 2.0,
		"estoi": 0.8,
		"wer": {"errors": 1, "reference_words": 2, "rate": 0.5, "hypothesis_sha256": hash_value},
	}
	return {
		"schema_version": SCHEMA_VERSION,
		"scorer": SCORER_ID,
		"status": "passed",
		"case_id": "self-test",
		"profile": "Balanced",
		"condition": "noisy",
		"dataset_split": "validation",
		"alignment": {"method": "caller-declared-fixed-latency", "correlation_search_used": False, "signal_stage": "sender-pre-opus", "sample_rate_hz": SAMPLE_RATE_HZ, "reference_samples": 48000, "original_latency_samples": 0, "candidate_latency_samples": 2400, "original_window_start_samples": 0, "candidate_window_start_samples": 2400, "qualified_route_binding": None},
		"inputs": {"clean_reference": dict(audio_value, frames=48000), "noisy_original": dict(audio_value), "candidate": dict(audio_value)},
		"runtime": runtime,
		"scorer_files": {"cli": {"name": "score-objective-quality.py", **file_value}, "implementation": {"name": "objective_quality_score.py", **file_value}},
		"wer_reference": {"kind": "clean-asr-consistency", "label": "clean-ASR-consistency WER", "language": "en", "normalization": NORMALIZATION_ID, "text_sha256": hash_value, "word_count": 2, "artifact": file_value, "attestation": None},
		"metrics": {"original": metric, "candidate": dict(metric)},
		"candidate_minus_original": {"dnsmos_bak": 0.0, "dnsmos_ovrl": 0.0, "dnsmos_sig": 0.0, "estoi": 0.0, "wer_delta_kind": "clean-asr-consistency", "wer_delta_percentage_points": 0.0},
	}


def run_self_test() -> None:
	_expect(normalize_text("  HÉJ—World! 42 ") == "héj world 42", "self-test", "normalization failed")
	_expect(word_error("one two three", "one three") == (1, 3, 1 / 3), "self-test", "WER failed")
	_verify_ed25519(
		bytes.fromhex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"),
		b"",
		bytes.fromhex(
			"e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e06522490155"
			"5fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"
		),
	)
	validate_score_document(_sample_score())
	_expect(_compose_window_starts(960, 0, 1440) == (960, 2400), "self-test", "qualified route delay was not preserved independently of enhancement latency")
	missing_route = SimpleNamespace(
		signal_stage="receiver-capture",
		route_control_wav=None,
		route_control_score=None,
		candidate_fixed_timeline_score=None,
		route_e2e_manifest=None,
		original_latency_samples=0,
		candidate_latency_samples=1440,
	)
	try:
		_timeline_alignment(missing_route, {}, {})
	except ObjectiveScoreError:
		pass
	else:
		raise AssertionError("receiver capture was accepted without a qualified Original route binding")
	bad = dict(_sample_score())
	bad["dataset_split"] = "holdout"
	try:
		validate_score_document(bad)
	except ObjectiveScoreError:
		pass
	else:
		raise AssertionError("holdout score was accepted")
	missing_opening = dict(_sample_score())
	missing_opening["dataset_split"] = RELEASE_HOLDOUT_SPLIT
	try:
		validate_score_document(missing_opening)
	except ObjectiveScoreError:
		pass
	else:
		raise AssertionError("release-holdout score was accepted without opening provenance")
	missing_args = SimpleNamespace(dataset_split=RELEASE_HOLDOUT_SPLIT, **{name: None for name in _HOLDOUT_ARGUMENTS})
	try:
		_consume_release_holdout_opening(missing_args)
	except ObjectiveScoreError:
		pass
	else:
		raise AssertionError("release holdout opening accepted missing authorization arguments")
	with tempfile.TemporaryDirectory() as directory:
		root = Path(directory)
		binding = {"exact": "binding"}
		reference_path = root / "clean-reference.json"
		text, artifact = ensure_clean_asr_reference(reference_path, binding, lambda: "Clean, exact window!")
		_expect(text == "clean exact window" and artifact["sha256"] == sha256(reference_path), "self-test", "reference creation failed")
		second, _ = ensure_clean_asr_reference(reference_path, binding, lambda: (_ for _ in ()).throw(AssertionError("reference was retranscribed")))
		_expect(second == text, "self-test", "reference reuse failed")
		transcript = root / "utterance.txt"
		transcript.write_text("whole utterance", encoding="utf-8")
		attestation = root / "attestation.json"
		attestation.write_text(json.dumps({
			"schema_version": 1,
			"attestation": SEGMENT_ATTESTATION,
			"language": "en",
			"clean_reference": {"sha256": "2" * 64, "size_bytes": 1, "channels": 1, "frames": 48000, "sample_rate_hz": SAMPLE_RATE_HZ},
			"source_segment": {"source_audio_sha256": "3" * 64, "start_sample": 0, "length_samples": 48000, "transcript_coverage_start_sample": 0, "transcript_coverage_length_samples": 48000},
			"transcript": {"sha256": sha256(transcript), "size_bytes": transcript.stat().st_size, "derivation": "human-segment-transcription", "normalization": NORMALIZATION_ID, "source_scope": "whole-utterance"},
		}), encoding="utf-8")
		try:
			load_segment_ground_truth(transcript, attestation, {"sha256": "2" * 64, "size_bytes": 1, "channels": 1, "frames": 48000, "sample_rate_hz": SAMPLE_RATE_HZ}, "en")
		except ObjectiveScoreError:
			pass
		else:
			raise AssertionError("whole-utterance transcript was accepted")

		opening_root = root / "protected-opening"
		(opening_root / "receipts").mkdir(parents=True)
		(opening_root / "reports").mkdir()
		release_build = root / "mumble-release.exe"
		release_build.write_bytes(b"synthetic release build")
		seed = bytes(range(32))
		public_key, _ = _ed25519_sign_for_self_test(seed, b"")
		public_key_path = root / "release-owner.pub"
		public_key_path.write_bytes(public_key)
		fixed_now = datetime.now(timezone.utc).replace(microsecond=0)

		def opening_args(sequence: int, *, receipt_path: str | None = None, expected_plan: str | None = None) -> SimpleNamespace:
			run_id = f"00000000-0000-4000-8000-{sequence:012d}"
			attestation = {
				"schema_version": 1,
				"attestation": HOLDOUT_ATTESTATION_ID,
				"dataset_split": RELEASE_HOLDOUT_SPLIT,
				"purpose": HOLDOUT_PURPOSE,
				"run_id": run_id,
				"authorized_at_utc": (fixed_now - timedelta(minutes=1)).strftime("%Y-%m-%dT%H:%M:%SZ"),
				"valid_until_utc": (fixed_now + timedelta(minutes=10)).strftime("%Y-%m-%dT%H:%M:%SZ"),
				"receipt_relative_path": receipt_path if receipt_path is not None else f"receipts/{run_id}.json",
				"opening_report_relative_path": f"reports/{run_id}.json",
				"expected": {
					"corpus_inventory_sha256": "a" * 64,
					"mixture_plan_sha256": "b" * 64,
					"release_build": file_record(release_build),
				},
				"authorization": {
					"kind": HOLDOUT_AUTHORIZATION_KIND,
					"key_id": "self-test-release-owner",
					"public_key_sha256": sha256(public_key_path),
					"signature_encoding": HOLDOUT_SIGNATURE_ENCODING,
				},
			}
			_, signature = _ed25519_sign_for_self_test(seed, canonical_json_bytes(attestation))
			attestation_path = root / f"opening-{sequence}.json"
			signature_path = root / f"opening-{sequence}.sig"
			attestation_path.write_bytes(canonical_json_bytes(attestation) + b"\n")
			signature_path.write_bytes(signature)
			return SimpleNamespace(
				dataset_split=RELEASE_HOLDOUT_SPLIT,
				release_holdout_opening_attestation=attestation_path,
				release_holdout_opening_signature=signature_path,
				release_holdout_approval_public_key=public_key_path,
				release_holdout_opening_root=opening_root,
				release_build=release_build,
				expected_holdout_opening_sha256=sha256(attestation_path),
				expected_holdout_plan_sha256=expected_plan if expected_plan is not None else "b" * 64,
				expected_holdout_inventory_sha256="a" * 64,
				expected_release_build_sha256=sha256(release_build),
				expected_release_holdout_approval_public_key_sha256=sha256(public_key_path),
			)

		wrong_hash_args = opening_args(2, expected_plan="f" * 64)
		try:
			_consume_release_holdout_opening(wrong_hash_args, now=fixed_now)
		except ObjectiveScoreError:
			pass
		else:
			raise AssertionError("release holdout opening accepted a mismatched external plan hash")
		wrong_attestation_hash_args = opening_args(4)
		wrong_attestation_hash_args.expected_holdout_opening_sha256 = "f" * 64
		try:
			_consume_release_holdout_opening(wrong_attestation_hash_args, now=fixed_now)
		except ObjectiveScoreError:
			pass
		else:
			raise AssertionError("release holdout opening accepted a mismatched external attestation hash")
		wrong_key_args = opening_args(5)
		wrong_key_args.expected_release_holdout_approval_public_key_sha256 = "f" * 64
		try:
			_consume_release_holdout_opening(wrong_key_args, now=fixed_now)
		except ObjectiveScoreError:
			pass
		else:
			raise AssertionError("release holdout opening accepted a self-declared release-owner key")
		bad_signature_args = opening_args(5)
		bad_signature = bytearray(bad_signature_args.release_holdout_opening_signature.read_bytes())
		bad_signature[-1] ^= 1
		bad_signature_args.release_holdout_opening_signature.write_bytes(bad_signature)
		try:
			_consume_release_holdout_opening(bad_signature_args, now=fixed_now)
		except ObjectiveScoreError:
			pass
		else:
			raise AssertionError("release holdout opening accepted an invalid detached signature")

		unsafe_path_args = opening_args(3, receipt_path="../escape.json")
		try:
			_consume_release_holdout_opening(unsafe_path_args, now=fixed_now)
		except ObjectiveScoreError:
			pass
		else:
			raise AssertionError("release holdout opening accepted an unsafe receipt path")

		valid_args = opening_args(1)
		opening = _consume_release_holdout_opening(valid_args, now=fixed_now)
		_expect(opening is not None, "self-test holdout opening", "valid signed opening was rejected")
		holdout_score = dict(_sample_score())
		holdout_score["dataset_split"] = RELEASE_HOLDOUT_SPLIT
		holdout_score["release_holdout_opening"] = opening
		holdout_score["alignment"] = dict(holdout_score["alignment"])
		holdout_score["alignment"].update({
			"signal_stage": "receiver-capture",
			"original_window_start_samples": 960,
			"candidate_window_start_samples": 3360,
			"qualified_route_binding": {
				"route_offset_samples": 960,
				"control_wav": {"sha256": "1" * 64, "size_bytes": 1},
				"control_fixed_timeline_score": {"sha256": "1" * 64, "size_bytes": 1},
				"candidate_fixed_timeline_score": {"sha256": "1" * 64, "size_bytes": 1},
				"e2e_manifest": {"sha256": "1" * 64, "size_bytes": 1},
				"stable_execution_identity": {
					"client_binary_sha256": opening["release_build"]["sha256"],
					"model_manifest_sha256": "1" * 64,
					"recipe_manifest_sha256": "1" * 64,
					"run_provenance_sha256": "1" * 64,
					"runtime_payload_sha256": "1" * 64,
					"server_binary_sha256": "1" * 64,
				},
				"edge_tail_gate": {
					"candidate_passed": True,
					"control_passed": True,
					"pre_opus_complete_tail_required": True,
					"pre_opus_max_end_loss_samples": 0,
					"pre_opus_max_onset_loss_samples": 0,
				},
			},
		})
		holdout_score["inputs"] = dict(holdout_score["inputs"])
		holdout_score["inputs"]["noisy_original"] = dict(holdout_score["inputs"]["noisy_original"], frames=60000)
		holdout_score["inputs"]["candidate"] = dict(holdout_score["inputs"]["candidate"], frames=60000)
		validate_score_document(holdout_score)
		try:
			_consume_release_holdout_opening(valid_args, now=fixed_now)
		except ObjectiveScoreError:
			pass
		else:
			raise AssertionError("release holdout opening run id was reusable")


def run_cli(args: Any) -> Mapping[str, Any]:
	if args.self_test:
		run_self_test()
		return {"self_test": "ok"}
	_expect(not os.path.lexists(args.output), "objective score output", "refusing to replace an existing or occupied score artifact")
	document = _score(args)
	args.output.parent.mkdir(parents=True, exist_ok=True)
	try:
		with args.output.open("x", encoding="utf-8", newline="\n") as stream:
			json.dump(document, stream, ensure_ascii=False, indent=2, sort_keys=True)
			stream.write("\n")
	except FileExistsError as error:
		raise ObjectiveScoreError(f"refusing to replace an existing score artifact: {args.output}") from error
	return document["candidate_minus_original"]
