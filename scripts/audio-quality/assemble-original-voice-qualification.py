#!/usr/bin/env python3
"""Assemble hash-bound 45-pair Original-vs-legacy qualification evidence.

The assembler consumes an externally pinned campaign-bindings manifest.  It
does not launch clients and never fills missing evidence.  Every referenced
manifest, executable, staged payload, tool, receipt and fixture is reopened and
hashed before the exact schema-v1 document accepted by
``check-original-voice-contract.py`` is emitted.  Additional identities that
cannot fit in that deliberately small schema are retained in a separate,
deterministic provenance receipt.
"""

from __future__ import annotations

import argparse
import copy
import functools
import hashlib
import importlib.util
import json
import math
import os
import re
import stat
import struct
import subprocess
import sys
import tempfile
import wave
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Any, Mapping, MutableMapping, Sequence

from payload_identity import (
	PayloadIdentityError,
	canonical_json_bytes,
	canonical_json_sha256,
	file_sha256,
	is_reparse,
	payload_file_attestation,
	payload_tree_attestation,
	payload_tree_records,
)


LEGACY_INSTRUMENTATION_BASE_COMMIT = "ada2a85f6b551a2f3d8c6b23649edcd3c0b9a8f8"
LEGACY_UI_BUILD_COMMIT = "fd11a8941de27331a8469ddfd8d21b29ea2c505f"
LEGACY_BUILD_COMMIT = "234e5042669ee5387b06af7069f6157f465be0c9"
LEGACY_EXECUTABLE_SHA256 = "eb062ee53356e8223eb1264f9be5f8276a5562963ef6cf932410aa0f561dc816"
# Retain the short name internally for the qualification schema's built legacy
# identity. It deliberately does not refer to the instrumentation base.
LEGACY_COMMIT = LEGACY_BUILD_COMMIT
SERVER_COMMIT = "edd13692174b81554726b58cd2fa27135d45b0df"
EMPTY_SHA256 = hashlib.sha256(b"").hexdigest()
SAMPLE_RATE_HZ = 48_000
FRAME_SAMPLES = 480
ALIGNMENT_SAMPLES = 1_920
ORIGINAL_ROUTE_FIXED_STARTUP_FRAMES = 4
HEX64 = re.compile(r"^[0-9a-f]{64}$")
COMMIT40 = re.compile(r"^[0-9a-f]{40}$")

BINDING_KIND = "mumble-original-voice-campaign-bindings-v1"
WORKTREE_RECEIPT_KIND = "mumble-clean-worktree-receipt-v1"
PROVENANCE_KIND = "mumble-original-voice-qualification-provenance-v1"
FIXTURE_KIND = "mumble-original-voice-rendered-fixture-v2"
PARITY_CASE_ID = "master_quality-validation-00081"
PARITY_RENDERED_SAMPLES = 288_000

ROOT_KEYS = {
	"campaign_id", "candidate_commit", "cases", "corpus", "identities", "kind", "schema_version", "transport",
}
CLIENT_IDENTITY_KEYS = {
	"build_executable", "commit", "qualified_executable", "stage_executable", "stage_payload", "worktree_receipt",
}
LEGACY_CLIENT_IDENTITY_KEYS = CLIENT_IDENTITY_KEYS | {"instrumentation_base_commit"}
SERVER_IDENTITY_KEYS = {"commit", "executable", "worktree_receipt"}
IDENTITY_KEYS = {"candidate", "legacy", "server", "tools"}
TOOLS_KEYS = {"scorer", "wrapper"}
RUN_PROVENANCE_KEYS = {"fixed_timeline_scorer", "wrapper"}
FILE_REFERENCE_KEYS = {"path", "sha256", "size_bytes"}
TREE_REFERENCE_KEYS = {"file_count", "path", "sha256"}
TRANSPORT_KEYS = {
	"drain_milliseconds", "pre_roll_frames", "receiver_cleanup_enabled", "sender_auto_adapt",
	"sender_cleanup_mode", "server_host", "tail_frames", "transport_path", "voice_transport",
}
CORPUS_KEYS = {
	"clean_reference_wav", "corpus_inventory", "corpus_inventory_canonical_sha256", "corpus_lock",
	"corpus_lock_canonical_sha256", "fixture_attestation", "input_wav", "mixture_plan",
	"mixture_plan_canonical_sha256", "render_entry_sha256", "render_manifest", "selected_case_id",
}
CASE_BINDING_KEYS = {"bitrate_bps", "frames_per_packet", "legacy_manifest", "original_manifest", "transmit_mode"}
WORKTREE_RECEIPT_KEYS = {
	"clean", "git_commit", "git_status_porcelain_sha256", "git_tree_sha", "kind", "role", "schema_version",
	"source_root",
}
VOICE_CONTRACT_KEYS = {
	"active_model_sha256", "algorithmic_latency_samples", "bitrate_bps", "deadline_miss_count",
	"enhancement_profile", "fallback_count", "frames_per_packet", "implementation", "input_pcm_encoding",
	"input_pcm_sha256", "model_initialization_attempts", "opus_packet_hash_framing", "opus_packets_sha256",
	"packet_count", "pre_opus_pcm_encoding", "pre_opus_pcm_sha256", "ptt_hold_activated", "schema_version",
	"terminator_count", "transmit_mode",
}
VOICE_EVIDENCE_KEYS = {
	"algorithmic_latency_samples", "bitrate_bps", "deadline_miss_count", "enhancement_profile", "fallback_count",
	"frames_per_packet", "implementation", "input_pcm_sha256", "model_initialization_attempts",
	"opus_packets_sha256", "packet_count", "pre_opus_pcm_sha256", "received_pcm_sha256", "schema_version",
	"terminator_count", "transmit_mode",
}
FIXED_TIMELINE_KEYS = {
	"compared_samples", "declared_latency_samples", "end_loss_samples", "expected_end_samples",
	"expected_onset_samples", "fixed_timeline_sdr_db", "frame_samples", "loudness_match_gain",
	"missing_tail_samples", "onset_loss_samples", "passed", "qualification_limits", "received_clipped_samples",
	"received_end_samples", "received_onset_samples", "received_samples", "received_sha256",
	"reference_clipped_samples", "reference_end_samples", "reference_onset_samples", "reference_samples",
	"reference_sha256", "sample_rate_hz", "schema_version", "scorer", "timeline_alignment",
	"transport_baseline",
}
QUALIFICATION_LIMIT_KEYS = {
	"fail_on_new_clipping", "max_end_loss_samples", "max_onset_loss_samples", "require_complete_tail",
}
FIXED_TIMELINE_BINDING_KEYS = {
	"mode", "reference_artifact", "scored_artifact", "timeline_origin",
}
QUALIFICATION_ROOT_KEYS = {
	"candidate_build_sha", "candidate_executable_sha256", "cases", "corpus_sha256", "legacy_build_sha",
	"legacy_executable_sha256", "profile", "receiver_cleanup_enabled", "schema_version", "server_host",
	"transport_path",
}
QUALIFICATION_CASE_KEYS = {
	"algorithmic_latency_samples", "bitrate_bps", "candidate_executable_sha256", "deadline_miss_count",
	"enhancement_profile", "fallback_count", "frames_per_packet", "input_pcm_sha256",
	"legacy_executable_sha256", "legacy_input_pcm_sha256", "legacy_opus_packets_sha256", "legacy_packet_count",
	"legacy_pcm_sha256", "legacy_received_pcm_sha256", "legacy_received_sample_count",
	"legacy_receiver_clipped_samples", "legacy_receiver_end_loss_samples", "legacy_receiver_fixed_timeline_passed",
	"legacy_receiver_missing_tail_samples", "legacy_receiver_onset_loss_samples", "legacy_terminator_count",
	"model_initialization_attempts", "original_input_pcm_sha256", "original_opus_packets_sha256",
	"original_packet_count", "original_pcm_sha256", "original_received_pcm_sha256",
	"original_received_sample_count", "original_receiver_clipped_samples", "original_receiver_end_loss_samples",
	"original_receiver_fixed_timeline_passed", "original_receiver_missing_tail_samples",
	"original_receiver_onset_loss_samples", "original_terminator_count", "receiver_jitter_delta_samples",
	"transmit_mode",
}


class AssemblyError(ValueError):
	"""Raised when campaign evidence is incomplete, mutable or inconsistent."""


def _load_checker() -> Any:
	path = Path(__file__).with_name("check-original-voice-contract.py")
	spec = importlib.util.spec_from_file_location("mumble_original_voice_checker_for_assembler", path)
	if spec is None or spec.loader is None:
		raise AssemblyError(f"unable to load Original contract checker: {path}")
	module = importlib.util.module_from_spec(spec)
	spec.loader.exec_module(module)
	return module


CHECKER = _load_checker()
REQUIRED_MATRIX = tuple(dict(item) for item in CHECKER.required_matrix())


def _load_boundary_checker() -> Any:
	path = Path(__file__).with_name("check-input-enhancement-boundary.py")
	spec = importlib.util.spec_from_file_location("mumble_input_enhancement_boundary_for_assembler", path)
	if spec is None or spec.loader is None:
		raise AssemblyError(f"unable to load input-enhancement boundary checker: {path}")
	module = importlib.util.module_from_spec(spec)
	spec.loader.exec_module(module)
	return module


BOUNDARY_CHECKER = _load_boundary_checker()


def _expect(condition: bool, path: str, message: str) -> None:
	if not condition:
		raise AssemblyError(f"{path}: {message}")


def _mapping(value: Any, path: str) -> Mapping[str, Any]:
	_expect(isinstance(value, dict), path, "expected an object")
	return value


def _array(value: Any, path: str) -> Sequence[Any]:
	_expect(isinstance(value, list), path, "expected an array")
	return value


def _exact_keys(value: Mapping[str, Any], expected: set[str], path: str) -> None:
	missing = sorted(expected - set(value))
	unknown = sorted(set(value) - expected)
	_expect(not missing, path, f"missing keys: {', '.join(missing)}")
	_expect(not unknown, path, f"unknown keys: {', '.join(unknown)}")


def _required_keys(value: Mapping[str, Any], required: set[str], path: str) -> None:
	missing = sorted(required - set(value))
	_expect(not missing, path, f"missing keys: {', '.join(missing)}")


def _integer(value: Any, path: str, minimum: int | None = None) -> int:
	_expect(isinstance(value, int) and not isinstance(value, bool), path, "expected an integer")
	if minimum is not None:
		_expect(value >= minimum, path, f"must be >= {minimum}")
	return value


def _integerish(value: Any, path: str, minimum: int | None = None) -> int:
	if isinstance(value, str) and re.fullmatch(r"[0-9]+", value):
		result = int(value)
	else:
		result = _integer(value, path)
	if minimum is not None:
		_expect(result >= minimum, path, f"must be >= {minimum}")
	return result


def _text(value: Any, path: str) -> str:
	_expect(isinstance(value, str) and bool(value), path, "expected a non-empty string")
	return value


def _sha256(value: Any, path: str) -> str:
	_expect(isinstance(value, str) and bool(HEX64.fullmatch(value)), path, "expected a lowercase SHA-256")
	return value


def _commit(value: Any, path: str) -> str:
	_expect(isinstance(value, str) and bool(COMMIT40.fullmatch(value)), path, "expected a full lowercase Git commit")
	return value


def _load_json_bytes(raw: bytes, path: str) -> Mapping[str, Any]:
	def reject_duplicates(pairs: Sequence[tuple[str, Any]]) -> MutableMapping[str, Any]:
		result: MutableMapping[str, Any] = {}
		for key, value in pairs:
			if key in result:
				raise AssemblyError(f"{path}: duplicate JSON key {key!r}")
			result[key] = value
		return result
	def reject_constant(value: str) -> None:
		raise AssemblyError(f"{path}: non-finite JSON number {value!r} is forbidden")
	def finite_float(value: str) -> float:
		result = float(value)
		if not math.isfinite(result):
			raise AssemblyError(f"{path}: non-finite JSON number {value!r} is forbidden")
		return result

	try:
		value = json.loads(
			raw.decode("utf-8"), object_pairs_hook=reject_duplicates,
			parse_constant=reject_constant, parse_float=finite_float,
		)
	except (UnicodeDecodeError, json.JSONDecodeError) as error:
		raise AssemblyError(f"{path}: invalid UTF-8 JSON: {error}") from error
	return _mapping(value, path)


def _reject_reparse_components(path: Path, label: str) -> Path:
	absolute = Path(os.path.abspath(os.fspath(path)))
	current = Path(absolute.anchor)
	try:
		for part in absolute.parts[1:]:
			current /= part
			_expect(not is_reparse(current), label, f"reparse points are forbidden: {current}")
	except PayloadIdentityError as error:
		raise AssemblyError(f"{label}: {error}") from error
	return absolute


def _regular_file(path: Path, label: str) -> Path:
	absolute = _reject_reparse_components(path, label)
	try:
		resolved = absolute.resolve(strict=True)
	except OSError as error:
		raise AssemblyError(f"{label}: missing file {path}: {error}") from error
	_expect(resolved.is_file(), label, f"not a regular file: {resolved}")
	try:
		_expect(not is_reparse(resolved), label, f"reparse points are forbidden: {resolved}")
	except PayloadIdentityError as error:
		raise AssemblyError(f"{label}: {error}") from error
	return resolved


def _directory(path: Path, label: str) -> Path:
	absolute = _reject_reparse_components(path, label)
	try:
		resolved = absolute.resolve(strict=True)
	except OSError as error:
		raise AssemblyError(f"{label}: missing directory {path}: {error}") from error
	_expect(resolved.is_dir(), label, f"not a directory: {resolved}")
	try:
		_expect(not is_reparse(resolved), label, f"reparse points are forbidden: {resolved}")
	except PayloadIdentityError as error:
		raise AssemblyError(f"{label}: {error}") from error
	return resolved


def _resolve_reference_path(base: Path, value: Any, label: str) -> Path:
	text = _text(value, label)
	candidate = Path(text)
	if not candidate.is_absolute():
		candidate = base / candidate
	return candidate


@dataclass(frozen=True)
class AttestedFile:
	path: Path
	sha256: str
	size_bytes: int

	def receipt(self) -> Mapping[str, Any]:
		return {"path": str(self.path), "sha256": self.sha256, "size_bytes": self.size_bytes}


@dataclass(frozen=True)
class AttestedTree:
	path: Path
	sha256: str
	file_count: int

	def receipt(self) -> Mapping[str, Any]:
		return {"path": str(self.path), "sha256": self.sha256, "file_count": self.file_count}


def _file_reference(value: Any, base: Path, label: str) -> AttestedFile:
	reference = _mapping(value, label)
	_exact_keys(reference, FILE_REFERENCE_KEYS, label)
	expected_hash = _sha256(reference["sha256"], f"{label}.sha256")
	expected_size = _integer(reference["size_bytes"], f"{label}.size_bytes", 1)
	path = _regular_file(_resolve_reference_path(base, reference["path"], f"{label}.path"), label)
	try:
		actual = payload_file_attestation(path)
	except PayloadIdentityError as error:
		raise AssemblyError(f"{label}: {error}") from error
	_expect(actual["size_bytes"] == expected_size, f"{label}.size_bytes", "file size differs from external pin")
	_expect(actual["sha256"] == expected_hash, f"{label}.sha256", "file bytes differ from external pin")
	return AttestedFile(path, str(actual["sha256"]), expected_size)


def _tree_reference(value: Any, base: Path, label: str) -> AttestedTree:
	reference = _mapping(value, label)
	_exact_keys(reference, TREE_REFERENCE_KEYS, label)
	expected_hash = _sha256(reference["sha256"], f"{label}.sha256")
	expected_count = _integer(reference["file_count"], f"{label}.file_count", 1)
	path = _directory(_resolve_reference_path(base, reference["path"], f"{label}.path"), label)
	try:
		attestation = payload_tree_attestation(path)
	except PayloadIdentityError as error:
		raise AssemblyError(f"{label}: {error}") from error
	_expect(attestation["file_count"] == expected_count, f"{label}.file_count", "tree file count differs from pin")
	_expect(attestation["sha256"] == expected_hash, f"{label}.sha256", "tree bytes differ from external pin")
	return AttestedTree(path, expected_hash, expected_count)


def _attested_json(reference: Any, base: Path, label: str) -> tuple[Mapping[str, Any], AttestedFile]:
	attested = _file_reference(reference, base, label)
	try:
		before = payload_file_attestation(attested.path)
		raw = attested.path.read_bytes()
		after = payload_file_attestation(attested.path)
	except OSError as error:
		raise AssemblyError(f"{label}: unable to read {attested.path}: {error}") from error
	except PayloadIdentityError as error:
		raise AssemblyError(f"{label}: {error}") from error
	_expect(before == after, label, "file changed while being read")
	_expect(len(raw) == attested.size_bytes, f"{label}.size_bytes", "file changed while being read")
	_expect(hashlib.sha256(raw).hexdigest() == attested.sha256, f"{label}.sha256", "file changed while being read")
	return _load_json_bytes(raw, label), attested


def _same_path(left: Path | str, right: Path | str, label: str) -> None:
	left_path = _reject_reparse_components(Path(left), f"{label}.left")
	right_path = _reject_reparse_components(Path(right), f"{label}.right")
	_expect(os.path.normcase(str(left_path)) == os.path.normcase(str(right_path)), label, f"path mismatch: {left_path} != {right_path}")


@dataclass(frozen=True)
class WaveInfo:
	path: Path
	format_code: int
	channels: int
	sample_rate_hz: int
	bits_per_sample: int
	block_align: int
	sample_count: int
	pcm: bytes
	container_sha256: str


def _read_wave(path: Path, label: str) -> WaveInfo:
	resolved = _regular_file(path, label)
	try:
		before = payload_file_attestation(resolved)
		raw = resolved.read_bytes()
		after = payload_file_attestation(resolved)
	except OSError as error:
		raise AssemblyError(f"{label}: unable to read WAV: {error}") from error
	except PayloadIdentityError as error:
		raise AssemblyError(f"{label}: {error}") from error
	_expect(before == after, label, "WAV changed while it was read")
	_expect(len(raw) == before["size_bytes"] and hashlib.sha256(raw).hexdigest() == before["sha256"], label, "WAV read differs from its stable file attestation")
	_expect(len(raw) >= 44 and raw[:4] == b"RIFF" and raw[8:12] == b"WAVE", label, "expected a RIFF/WAVE file")
	_expect(struct.unpack_from("<I", raw, 4)[0] + 8 == len(raw), label, "RIFF size does not match the file length")
	offset = 12
	fmt: tuple[int, int, int, int, int, int] | None = None
	pcm: bytes | None = None
	while offset + 8 <= len(raw):
		chunk_id = raw[offset : offset + 4]
		chunk_size = struct.unpack_from("<I", raw, offset + 4)[0]
		chunk_start = offset + 8
		chunk_end = chunk_start + chunk_size
		_expect(chunk_end <= len(raw), label, "WAV chunk exceeds file length")
		if chunk_id == b"fmt ":
			_expect(fmt is None, label, "WAV contains multiple fmt chunks")
			_expect(chunk_size >= 16, label, "WAV fmt chunk is truncated")
			format_code, channels, sample_rate, byte_rate, block_align, bits = struct.unpack_from("<HHIIHH", raw, chunk_start)
			_expect(format_code in (1, 3), label, "WAV must use integer PCM or IEEE float encoding")
			if chunk_size != 16:
				_expect(chunk_size >= 18, label, "extended WAV fmt chunk has no extension size")
				extension_size = struct.unpack_from("<H", raw, chunk_start + 16)[0]
				_expect(18 + extension_size == chunk_size, label, "WAV fmt extension size is inconsistent")
			fmt = (format_code, channels, sample_rate, bits, block_align, byte_rate)
		elif chunk_id == b"data":
			_expect(pcm is None, label, "WAV contains multiple data chunks")
			_expect(fmt is not None, label, "WAV data chunk appears before fmt")
			pcm = raw[chunk_start:chunk_end]
		offset = chunk_end + (chunk_size & 1)
		_expect(offset <= len(raw), label, "WAV chunk padding exceeds file length")
	_expect(offset == len(raw), label, "WAV contains a truncated chunk header")
	_expect(fmt is not None and pcm is not None, label, "WAV is missing fmt/data chunks")
	format_code, channels, sample_rate, bits, block_align, byte_rate = fmt
	_expect(channels > 0 and sample_rate > 0 and bits > 0 and bits % 8 == 0, label, "WAV fmt values are invalid")
	expected_block_align = channels * (bits // 8)
	_expect(block_align == expected_block_align, label, "WAV block alignment is inconsistent with channels/bit depth")
	_expect(byte_rate == sample_rate * block_align, label, "WAV byte rate is inconsistent with sample rate/block alignment")
	_expect(block_align > 0 and len(pcm) % block_align == 0, label, "WAV data is not sample aligned")
	if format_code == 1:
		_expect(bits in (8, 16, 24, 32), label, "integer PCM WAV uses an unsupported sample width")
	else:
		_expect(bits in (32, 64), label, "IEEE-float WAV must use float32 or float64 samples")
		float_code = "f" if bits == 32 else "d"
		_expect(all(math.isfinite(sample[0]) for sample in struct.iter_unpack(f"<{float_code}", pcm)), label, "IEEE-float WAV contains NaN or Inf")
	return WaveInfo(
		resolved, format_code, channels, sample_rate, bits, block_align, len(pcm) // block_align, pcm,
		hashlib.sha256(raw).hexdigest(),
	)


def _wave_clipped_samples(value: WaveInfo, label: str) -> int:
	if value.format_code == 3:
		float_code = "f" if value.bits_per_sample == 32 else "d"
		return sum(1 for sample in struct.iter_unpack(f"<{float_code}", value.pcm) if abs(sample[0]) >= 1.0)
	if value.bits_per_sample == 8:
		return sum(1 for sample in value.pcm if sample in (0, 255))
	if value.bits_per_sample == 16:
		return sum(1 for sample in struct.iter_unpack("<h", value.pcm) if sample[0] in (-32768, 32767))
	if value.bits_per_sample == 24:
		clipped = 0
		for offset in range(0, len(value.pcm), 3):
			sample = int.from_bytes(value.pcm[offset : offset + 3], "little", signed=True)
			clipped += sample in (-8_388_608, 8_388_607)
		return clipped
	if value.bits_per_sample == 32:
		return sum(
			1 for sample in struct.iter_unpack("<i", value.pcm)
			if sample[0] in (-2_147_483_648, 2_147_483_647)
		)
	raise AssemblyError(f"{label}: unsupported WAV format for clipping verification")


@dataclass(frozen=True)
class CorpusBinding:
	lock: AttestedFile
	lock_canonical_sha256: str
	inventory: AttestedFile
	inventory_canonical_sha256: str
	mixture_plan: AttestedFile
	mixture_plan_canonical_sha256: str
	render_manifest: AttestedFile
	render_entry_sha256: str
	fixture_attestation: AttestedFile
	input_wav: AttestedFile
	clean_wav: AttestedFile
	input_wave: WaveInfo
	clean_wave: WaveInfo


def _validate_corpus(value: Any, base: Path, expected_rendered_samples: int) -> CorpusBinding:
	corpus = _mapping(value, "bindings.corpus")
	_exact_keys(corpus, CORPUS_KEYS, "bindings.corpus")
	lock, lock_ref = _attested_json(corpus["corpus_lock"], base, "bindings.corpus.corpus_lock")
	lock_canonical = _sha256(corpus["corpus_lock_canonical_sha256"], "bindings.corpus.corpus_lock_canonical_sha256")
	_expect(canonical_json_sha256(lock) == lock_canonical, "bindings.corpus.corpus_lock_canonical_sha256", "canonical corpus-lock hash mismatch")
	inventory, inventory_ref = _attested_json(corpus["corpus_inventory"], base, "bindings.corpus.corpus_inventory")
	inventory_canonical = _sha256(corpus["corpus_inventory_canonical_sha256"], "bindings.corpus.corpus_inventory_canonical_sha256")
	_expect(canonical_json_sha256(inventory) == inventory_canonical, "bindings.corpus.corpus_inventory_canonical_sha256", "canonical inventory hash mismatch")
	_expect(inventory.get("schema_version") == 3 and inventory.get("eligibility") == "release", "corpus inventory", "Original release qualification requires a release-eligible schema-v3 inventory")
	_expect(inventory.get("corpus_lock_sha256") == lock_canonical, "corpus inventory.corpus_lock_sha256", "inventory is not bound to the supplied corpus lock")
	items = _array(inventory.get("items"), "corpus inventory.items")
	item_by_id: dict[str, Mapping[str, Any]] = {}
	for index, raw_item in enumerate(items):
		item = _mapping(raw_item, f"corpus inventory.items[{index}]")
		item_id = _text(item.get("id"), f"corpus inventory.items[{index}].id")
		_expect(item_id not in item_by_id, f"corpus inventory.items[{index}].id", "duplicate inventory item ID")
		item_by_id[item_id] = item

	plan, plan_ref = _attested_json(corpus["mixture_plan"], base, "bindings.corpus.mixture_plan")
	plan_canonical = _sha256(corpus["mixture_plan_canonical_sha256"], "bindings.corpus.mixture_plan_canonical_sha256")
	_expect(canonical_json_sha256(plan) == plan_canonical, "bindings.corpus.mixture_plan_canonical_sha256", "canonical mixture-plan hash mismatch")
	_required_keys(
		plan,
		{"cases", "corpus_inventory_sha256", "corpus_lock_sha256", "format", "generator", "schema_version", "split", "suite", "timeline_alignment"},
		"mixture plan",
	)
	_expect(plan["schema_version"] == 4 and plan["generator"] == "mumble-audio-mixture-plan-v4", "mixture plan", "expected the frozen schema-v4 generator")
	_expect(plan["suite"] == "master_quality" and plan["split"] == "validation" and plan["timeline_alignment"] == "fixed", "mixture plan", "Original parity fixture must come from master-quality validation")
	_expect(plan["corpus_inventory_sha256"] == inventory_canonical and plan["corpus_lock_sha256"] == lock_canonical, "mixture plan", "plan is not bound to the supplied lock/inventory")
	plan_format = _mapping(plan["format"], "mixture plan.format")
	_expect(
		plan_format.get("sample_rate_hz") == SAMPLE_RATE_HZ and plan_format.get("channels") == 1
		and plan_format.get("frame_samples") == FRAME_SAMPLES and plan_format.get("duration_ms") == 6000,
		"mixture plan.format", "unexpected parity audio format",
	)
	plan_cases = _array(plan["cases"], "mixture plan.cases")
	_expect(len(plan_cases) == 500, "mixture plan.cases", "master-quality validation plan must contain exactly 500 cases")
	case_id = _text(corpus["selected_case_id"], "bindings.corpus.selected_case_id")
	_expect(case_id == PARITY_CASE_ID, "bindings.corpus.selected_case_id", "unexpected frozen Original parity case")
	case_matches = [item for item in plan_cases if isinstance(item, dict) and item.get("case_id") == case_id]
	_expect(len(case_matches) == 1, "mixture plan.cases", "selected parity case must occur exactly once")
	selected_case = _mapping(case_matches[0], f"mixture plan case {case_id}")
	_expect(selected_case.get("profile") == "Original", f"mixture plan case {case_id}.profile", "parity fixture must use Original")
	_expect(_mapping(selected_case.get("startup"), f"mixture plan case {case_id}.startup").get("preroll_ms") == 0, f"mixture plan case {case_id}.startup", "parity fixture must be cold-start")
	speech = _mapping(selected_case.get("speech"), f"mixture plan case {case_id}.speech")
	noise = _mapping(selected_case.get("noise"), f"mixture plan case {case_id}.noise")
	mix = _mapping(selected_case.get("mix"), f"mixture plan case {case_id}.mix")
	rir = _mapping(mix.get("rir"), f"mixture plan case {case_id}.mix.rir")
	microphone = _mapping(mix.get("microphone_response"), f"mixture plan case {case_id}.mix.microphone_response")
	_expect(speech.get("language") == "sv-SE", f"mixture plan case {case_id}.speech.language", "frozen parity case must be Swedish")
	_expect(noise.get("class") == "competing-speech" and mix.get("snr_db") == 5, f"mixture plan case {case_id}", "frozen parity scene must be competing speech at 5 dB SNR")
	for component_label, component, expected_kind in (
		("speech", speech, "speech"), ("noise", noise, "noise"), ("rir", rir, "rir"),
		("microphone_response", microphone, "microphone_response"),
	):
		item_id = _text(component.get("item_id"), f"mixture plan case {case_id}.{component_label}.item_id")
		_expect(item_id in item_by_id, f"mixture plan case {case_id}.{component_label}.item_id", "component is absent from the pinned inventory")
		inventory_item = item_by_id[item_id]
		_expect(inventory_item.get("kind") == expected_kind, f"corpus inventory item {item_id}.kind", "plan component kind mismatch")
		for field in ("relative_path", "sha256", "size_bytes", "source_artifact_sha256", "source_id"):
			_expect(component.get(field) == inventory_item.get(field), f"mixture plan case {case_id}.{component_label}.{field}", "plan component differs from inventory")
		_expect(component.get("input_channels") == inventory_item.get("channels") and component.get("input_sample_rate_hz") == inventory_item.get("sample_rate_hz"), f"mixture plan case {case_id}.{component_label}", "plan component format differs from inventory")
	for component_label, component in (("speech", speech), ("noise", noise)):
		window = _mapping(component.get("window"), f"mixture plan case {case_id}.{component_label}.window")
		_expect(_integer(window.get("length_samples"), f"mixture plan case {case_id}.{component_label}.window.length_samples") == expected_rendered_samples, f"mixture plan case {case_id}.{component_label}.window", "selected source window has the wrong fixed duration")

	input_ref = _file_reference(corpus["input_wav"], base, "bindings.corpus.input_wav")
	clean_ref = _file_reference(corpus["clean_reference_wav"], base, "bindings.corpus.clean_reference_wav")
	render_manifest, render_ref = _attested_json(corpus["render_manifest"], base, "bindings.corpus.render_manifest")
	_exact_keys(
		render_manifest,
		{"cases", "channels", "corpus_inventory_sha256", "corpus_lock_sha256", "plan_sha256", "private_audio_do_not_upload", "renderer", "sample_rate_hz", "schema_version"},
		"render manifest",
	)
	_expect(render_manifest["schema_version"] == 2 and render_manifest["renderer"] == "mumble-audio-mixture-renderer-v2", "render manifest", "expected schema-v2 deterministic renderer")
	_expect(
		render_manifest["plan_sha256"] == plan_canonical
		and render_manifest["corpus_inventory_sha256"] == inventory_canonical
		and render_manifest["corpus_lock_sha256"] == lock_canonical,
		"render manifest", "render manifest is not bound to the supplied plan/inventory/lock",
	)
	_expect(render_manifest["channels"] == 1 and render_manifest["sample_rate_hz"] == SAMPLE_RATE_HZ and render_manifest["private_audio_do_not_upload"] is True, "render manifest", "render manifest audio/privacy contract mismatch")
	render_cases = _array(render_manifest["cases"], "render manifest.cases")
	render_matches = [item for item in render_cases if isinstance(item, dict) and item.get("case_id") == case_id]
	_expect(len(render_matches) == 1, "render manifest.cases", "selected parity render must occur exactly once")
	render_entry = _mapping(render_matches[0], f"render manifest case {case_id}")
	_exact_keys(
		render_entry,
		{"case_id", "clean_reference", "input", "microphone_response_source_sha256", "noise_source_sha256", "profile", "rendered_samples", "rir_source_sha256", "speech_source_sha256", "startup_preroll_ms"},
		f"render manifest case {case_id}",
	)
	render_entry_sha = _sha256(corpus["render_entry_sha256"], "bindings.corpus.render_entry_sha256")
	_expect(canonical_json_sha256(render_entry) == render_entry_sha, "bindings.corpus.render_entry_sha256", "render-entry hash mismatch")
	_expect(render_entry.get("profile") == "Original" and render_entry.get("startup_preroll_ms") == 0 and render_entry.get("rendered_samples") == expected_rendered_samples, f"render manifest case {case_id}", "rendered case metadata differs from the frozen plan case")
	for field, expected in (
		("speech_source_sha256", speech.get("sha256")), ("noise_source_sha256", noise.get("sha256")),
		("rir_source_sha256", rir.get("sha256")), ("microphone_response_source_sha256", microphone.get("sha256")),
	):
		_expect(render_entry.get(field) == expected, f"render manifest case {case_id}.{field}", "rendered source differs from the plan/inventory")

	def bind_rendered_file(entry_field: str, expected: AttestedFile) -> None:
		entry = _mapping(render_entry.get(entry_field), f"render manifest case {case_id}.{entry_field}")
		_exact_keys(entry, {"path", "sha256"}, f"render manifest case {case_id}.{entry_field}")
		relative = PurePosixPath(_text(entry["path"], f"render manifest case {case_id}.{entry_field}.path"))
		_expect(not relative.is_absolute() and relative.as_posix() == entry["path"] and "." not in relative.parts and ".." not in relative.parts, f"render manifest case {case_id}.{entry_field}.path", "unsafe rendered path")
		_same_path(render_ref.path.parent.joinpath(*relative.parts), expected.path, f"render manifest case {case_id}.{entry_field}.path")
		_expect(_sha256(entry["sha256"], f"render manifest case {case_id}.{entry_field}.sha256") == expected.sha256, f"render manifest case {case_id}.{entry_field}.sha256", "rendered WAV differs from external pin")

	bind_rendered_file("input", input_ref)
	bind_rendered_file("clean_reference", clean_ref)
	fixture, fixture_ref = _attested_json(corpus["fixture_attestation"], base, "bindings.corpus.fixture_attestation")
	_exact_keys(
		fixture,
		{"alignment_samples", "case_id", "clean_reference_sha256", "corpus_inventory_sha256", "input_sha256", "kind", "mixture_plan_sha256", "render_entry_sha256", "render_manifest_sha256", "rendered_samples", "schema_version"},
		"fixture attestation",
	)
	_expect(fixture["schema_version"] == 2 and fixture["kind"] == FIXTURE_KIND, "fixture attestation", "unsupported rendered-fixture contract")
	_expect(fixture["case_id"] == case_id, "fixture attestation.case_id", "case mismatch")
	_expect(_integer(fixture["alignment_samples"], "fixture attestation.alignment_samples") == ALIGNMENT_SAMPLES, "fixture attestation", "alignment mismatch")
	_expect(_integer(fixture["rendered_samples"], "fixture attestation.rendered_samples") == expected_rendered_samples, "fixture attestation", "rendered length mismatch")
	for field, expected in (
		("corpus_inventory_sha256", inventory_canonical), ("mixture_plan_sha256", plan_canonical),
		("render_manifest_sha256", render_ref.sha256), ("render_entry_sha256", render_entry_sha),
		("input_sha256", input_ref.sha256), ("clean_reference_sha256", clean_ref.sha256),
	):
		_expect(_sha256(fixture[field], f"fixture attestation.{field}") == expected, f"fixture attestation.{field}", "fixture contract binding mismatch")
	input_wave = _read_wave(input_ref.path, "bindings.corpus.input_wav")
	clean_wave = _read_wave(clean_ref.path, "bindings.corpus.clean_reference_wav")
	for wave_info, label in ((input_wave, "input"), (clean_wave, "clean reference")):
		_expect((wave_info.format_code, wave_info.channels, wave_info.sample_rate_hz, wave_info.bits_per_sample) == (1, 1, SAMPLE_RATE_HZ, 16), f"fixture attestation.{label}", "rendered parity WAV must be mono 48 kHz PCM16")
	_expect(input_wave.sample_count == clean_wave.sample_count == expected_rendered_samples, "fixture attestation", "rendered WAV lengths differ from the frozen case")
	_expect(expected_rendered_samples % ALIGNMENT_SAMPLES == 0, "fixture attestation", "rendered case is not 1,920-sample aligned")
	return CorpusBinding(
		lock_ref, lock_canonical, inventory_ref, inventory_canonical, plan_ref, plan_canonical,
		render_ref, render_entry_sha, fixture_ref, input_ref, clean_ref, input_wave, clean_wave,
	)


@functools.lru_cache(maxsize=8)
def _verify_legacy_revision_contract(
	instrumentation_base: str = LEGACY_INSTRUMENTATION_BASE_COMMIT,
	build_commit: str = LEGACY_BUILD_COMMIT,
) -> Mapping[str, Any]:
	base = _commit(instrumentation_base, "legacy instrumentation base")
	build = _commit(build_commit, "legacy build commit")
	repository = Path(__file__).resolve().parents[2]

	def git(*arguments: str) -> str:
		try:
			completed = subprocess.run(
				["git", "-C", str(repository), *arguments], check=True,
				stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8",
			)
		except (OSError, subprocess.CalledProcessError) as error:
			details = error.stderr.strip() if isinstance(error, subprocess.CalledProcessError) else str(error)
			raise AssemblyError(f"legacy revision contract: Git verification failed: {details}") from error
		return completed.stdout.strip()

	ui_build = _commit(LEGACY_UI_BUILD_COMMIT, "legacy UI build commit")
	parents = git("show", "-s", "--format=%P", build).split()
	ui_parents = git("show", "-s", "--format=%P", ui_build).split()
	_expect(parents == [ui_build], "legacy revision contract", "built legacy commit must directly follow the frozen UI-build commit")
	_expect(ui_parents == [base], "legacy revision contract", "UI-build commit must directly follow the instrumentation base")
	ui_changed_paths = [
		line for line in git("diff", "--name-only", "--diff-filter=ACDMRT", base, ui_build, "--").splitlines()
		if line
	]
	_expect(ui_changed_paths == ["src/mumble/ModernSettingsController.cpp"], "legacy revision contract", "unexpected frozen UI-build delta")
	ui_runtime_paths, ui_protected_paths = BOUNDARY_CHECKER.classify_changes(ui_changed_paths)
	_expect(not ui_runtime_paths, "legacy revision contract", f"UI-only build fix changed input-enhancement runtime: {ui_runtime_paths}")
	_expect(not ui_protected_paths, "legacy revision contract", f"UI-only build fix changed protected voice/server/receive files: {ui_protected_paths}")
	instrumentation_changed_paths = [
		line for line in git("diff", "--name-only", "--diff-filter=ACDMRT", ui_build, build, "--").splitlines()
		if line
	]
	_expect(
		instrumentation_changed_paths == [
			"src/mumble/SpeechCleanupTestAudio.cpp", "src/mumble/SpeechCleanupTestAudio.h",
		],
		"legacy revision contract", "unexpected frozen E2E instrumentation delta",
	)
	runtime_paths, protected_paths = BOUNDARY_CHECKER.classify_changes(instrumentation_changed_paths)
	_expect(runtime_paths == instrumentation_changed_paths, "legacy revision contract", "E2E backport escaped the test-audio runtime boundary")
	_expect(not protected_paths, "legacy revision contract", f"UI-only build fix changed protected voice/server/receive files: {protected_paths}")
	try:
		verified_functions = CHECKER.verify_source_contract(base, build)
	except CHECKER.ContractError as error:
		raise AssemblyError(f"legacy revision contract: protected Original source check failed: {error}") from error
	_expect(len(verified_functions) == 4, "legacy revision contract", "protected Original checker did not verify all four source regions")
	return {
		"instrumentation_base_commit": base,
		"ui_build_commit": ui_build,
		"build_commit": build,
		"two_step_parent_chain_verified": True,
		"ui_only_changed_paths": ui_changed_paths,
		"instrumentation_changed_paths": instrumentation_changed_paths,
		"changed_paths_sha256": canonical_json_sha256([*ui_changed_paths, *instrumentation_changed_paths]),
		"input_enhancement_runtime_changes": runtime_paths,
		"protected_voice_path_changes": protected_paths,
		"protected_source_regions": [
			{"label": label, "sha256": digest} for label, digest in verified_functions
		],
	}


@functools.lru_cache(maxsize=8)
def _verify_candidate_revision_contract(
	legacy_build_commit: str,
	candidate_commit: str,
) -> Mapping[str, Any]:
	"""Bind the candidate to the frozen legacy Original/transport boundary."""
	base = _commit(legacy_build_commit, "candidate revision contract legacy build")
	head = _commit(candidate_commit, "candidate revision contract candidate")
	repository = Path(__file__).resolve().parents[2]

	def git(*arguments: str) -> str:
		try:
			completed = subprocess.run(
				["git", "-C", str(repository), *arguments], check=True,
				stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8",
			)
		except (OSError, subprocess.CalledProcessError) as error:
			details = error.stderr.strip() if isinstance(error, subprocess.CalledProcessError) else str(error)
			raise AssemblyError(f"candidate revision contract: Git verification failed: {details}") from error
		return completed.stdout.strip()

	# Fail before classifying a path list if either externally supplied revision
	# is absent from the repository used by the assembler.
	git("cat-file", "-e", f"{base}^{{commit}}")
	git("cat-file", "-e", f"{head}^{{commit}}")
	changed_paths = [
		line for line in git("diff", "--name-only", "--diff-filter=ACDMRT", base, head, "--").splitlines()
		if line
	]
	runtime_paths, protected_paths = BOUNDARY_CHECKER.classify_changes(changed_paths)
	_expect(
		not protected_paths,
		"candidate revision contract",
		f"candidate changed protected voice/server/receive paths: {protected_paths}",
	)
	try:
		verified_functions = CHECKER.verify_source_contract(base, head)
	except CHECKER.ContractError as error:
		raise AssemblyError(f"candidate revision contract: protected Original source check failed: {error}") from error
	_expect(
		len(verified_functions) == 4,
		"candidate revision contract",
		"protected Original checker did not verify all four source regions",
	)
	return {
		"legacy_build_commit": base,
		"candidate_commit": head,
		"changed_paths": changed_paths,
		"changed_paths_sha256": canonical_json_sha256(changed_paths),
		"input_enhancement_runtime_changes": runtime_paths,
		"protected_voice_path_changes": protected_paths,
		"protected_source_regions": [
			{"label": label, "sha256": digest} for label, digest in verified_functions
		],
	}


@dataclass(frozen=True)
class WorktreeReceipt:
	file: AttestedFile
	role: str
	commit: str
	source_root: Path


def _verify_live_worktree(source_root: Path, commit: str, tree_sha: str, label: str) -> None:
	def git(*arguments: str) -> bytes:
		try:
			completed = subprocess.run(
				["git", "-C", str(source_root), *arguments], check=True, stdout=subprocess.PIPE,
				stderr=subprocess.PIPE,
			)
		except (OSError, subprocess.CalledProcessError) as error:
			details = error.stderr.decode("utf-8", errors="replace").strip() if isinstance(error, subprocess.CalledProcessError) else str(error)
			raise AssemblyError(f"{label}: live Git verification failed: {details}") from error
		return completed.stdout

	live_commit = git("rev-parse", "HEAD").decode("ascii", errors="strict").strip()
	live_tree = git("rev-parse", "HEAD^{tree}").decode("ascii", errors="strict").strip()
	status = git("status", "--porcelain=v1", "--untracked-files=all")
	_expect(live_commit == commit, label, f"live HEAD {live_commit!r} differs from the receipt commit")
	_expect(live_tree == tree_sha, label, f"live tree {live_tree!r} differs from the receipt tree")
	_expect(status == b"", label, "source worktree is no longer clean")


def _validate_worktree_receipt(
	value: Any, base: Path, role: str, commit: str, verify_live_git: bool,
) -> WorktreeReceipt:
	receipt, file_ref = _attested_json(value, base, f"bindings.identities.{role}.worktree_receipt")
	_exact_keys(receipt, WORKTREE_RECEIPT_KEYS, f"{role} worktree receipt")
	_expect(receipt["schema_version"] == 1 and receipt["kind"] == WORKTREE_RECEIPT_KIND, f"{role} worktree receipt", "unsupported receipt contract")
	_expect(receipt["role"] == role, f"{role} worktree receipt.role", "role mismatch")
	_expect(_commit(receipt["git_commit"], f"{role} worktree receipt.git_commit") == commit, f"{role} worktree receipt", "commit mismatch")
	_expect(receipt["clean"] is True, f"{role} worktree receipt.clean", "worktree was not clean")
	_expect(_sha256(receipt["git_status_porcelain_sha256"], f"{role} worktree receipt.git_status_porcelain_sha256") == EMPTY_SHA256, f"{role} worktree receipt", "status receipt is not the SHA-256 of empty porcelain output")
	tree_sha = _commit(receipt["git_tree_sha"], f"{role} worktree receipt.git_tree_sha")
	source_root_text = _text(receipt["source_root"], f"{role} worktree receipt.source_root")
	source_root = Path(source_root_text)
	_expect(source_root.is_absolute(), f"{role} worktree receipt.source_root", "source root must be absolute")
	resolved_source_root = _directory(source_root, f"{role} worktree receipt.source_root")
	if verify_live_git:
		_verify_live_worktree(resolved_source_root, commit, tree_sha, f"{role} worktree receipt")
	return WorktreeReceipt(file_ref, role, commit, resolved_source_root)


@dataclass(frozen=True)
class ClientIdentity:
	role: str
	commit: str
	worktree: WorktreeReceipt
	build_executable: AttestedFile
	stage_executable: AttestedFile
	qualified_executable: AttestedFile
	stage_payload: AttestedTree

	@property
	def executable_sha256(self) -> str:
		return self.build_executable.sha256


def _validate_client_identity(
	value: Any, base: Path, role: str, expected_commit: str, verify_live_git: bool,
) -> ClientIdentity:
	identity = _mapping(value, f"bindings.identities.{role}")
	_exact_keys(
		identity, LEGACY_CLIENT_IDENTITY_KEYS if role == "legacy" else CLIENT_IDENTITY_KEYS,
		f"bindings.identities.{role}",
	)
	commit = _commit(identity["commit"], f"bindings.identities.{role}.commit")
	_expect(commit == expected_commit, f"bindings.identities.{role}.commit", "unexpected frozen commit")
	if role == "legacy":
		instrumentation_base = _commit(
			identity["instrumentation_base_commit"],
			"bindings.identities.legacy.instrumentation_base_commit",
		)
		_expect(
			instrumentation_base == LEGACY_INSTRUMENTATION_BASE_COMMIT,
			"bindings.identities.legacy.instrumentation_base_commit", "unexpected frozen instrumentation base",
		)
	worktree = _validate_worktree_receipt(identity["worktree_receipt"], base, role, commit, verify_live_git)
	build = _file_reference(identity["build_executable"], base, f"bindings.identities.{role}.build_executable")
	stage = _file_reference(identity["stage_executable"], base, f"bindings.identities.{role}.stage_executable")
	qualified = _file_reference(identity["qualified_executable"], base, f"bindings.identities.{role}.qualified_executable")
	payload = _tree_reference(identity["stage_payload"], base, f"bindings.identities.{role}.stage_payload")
	_expect(build.sha256 == stage.sha256 == qualified.sha256, f"bindings.identities.{role}", "build/stage/qualified executable hashes differ")
	_expect(len({os.path.normcase(str(item.path)) for item in (build, stage, qualified)}) == 3, f"bindings.identities.{role}", "three-way executable pins must identify three distinct files")
	file_identities = {(item.path.stat().st_dev, item.path.stat().st_ino) for item in (build, stage, qualified)}
	_expect(len(file_identities) == 3, f"bindings.identities.{role}", "three-way executable pins must not be hardlinks to the same file identity")
	_expect(stage.path.parent == payload.path, f"bindings.identities.{role}.stage_executable", "stage executable must be at the stage payload root")
	_expect(stage.path.name.lower() == "mumble.exe", f"bindings.identities.{role}.stage_executable", "expected mumble.exe")
	if role == "legacy" and verify_live_git:
		_expect(
			build.sha256 == LEGACY_EXECUTABLE_SHA256,
			"bindings.identities.legacy", "legacy executable differs from the frozen buildable reference",
		)
	return ClientIdentity(role, commit, worktree, build, stage, qualified, payload)


@dataclass(frozen=True)
class ServerIdentity:
	commit: str
	worktree: WorktreeReceipt
	executable: AttestedFile


def _validate_server_identity(value: Any, base: Path, verify_live_git: bool) -> ServerIdentity:
	identity = _mapping(value, "bindings.identities.server")
	_exact_keys(identity, SERVER_IDENTITY_KEYS, "bindings.identities.server")
	commit = _commit(identity["commit"], "bindings.identities.server.commit")
	_expect(commit == SERVER_COMMIT, "bindings.identities.server.commit", "unexpected frozen OG server commit")
	worktree = _validate_worktree_receipt(identity["worktree_receipt"], base, "server", commit, verify_live_git)
	executable = _file_reference(identity["executable"], base, "bindings.identities.server.executable")
	_expect(executable.path.name.lower() == "mumble-server.exe", "bindings.identities.server.executable", "expected mumble-server.exe")
	return ServerIdentity(commit, worktree, executable)


@dataclass(frozen=True)
class ToolIdentity:
	wrapper: AttestedFile
	scorer: AttestedFile


def _validate_tools(value: Any, base: Path) -> ToolIdentity:
	tools = _mapping(value, "bindings.identities.tools")
	_exact_keys(tools, TOOLS_KEYS, "bindings.identities.tools")
	return ToolIdentity(
		_file_reference(tools["wrapper"], base, "bindings.identities.tools.wrapper"),
		_file_reference(tools["scorer"], base, "bindings.identities.tools.scorer"),
	)


def _validate_transport(value: Any) -> Mapping[str, Any]:
	transport = _mapping(value, "bindings.transport")
	_exact_keys(transport, TRANSPORT_KEYS, "bindings.transport")
	expected = {
		"transport_path": "client1-opus-server-client2",
		"server_host": "127.0.0.1",
		"voice_transport": "tcp_tunnel",
		"receiver_cleanup_enabled": False,
		"sender_cleanup_mode": "Off",
		"sender_auto_adapt": False,
		"pre_roll_frames": 0,
		"tail_frames": 0,
		"drain_milliseconds": 1500,
	}
	_expect(dict(transport) == expected, "bindings.transport", "transport settings differ from the frozen parity contract")
	return transport


def _validate_launched_payload(app_path: Path, stage: AttestedTree, label: str) -> Mapping[str, Any]:
	app_root = _directory(app_path, label)
	stage_root = stage.path

	def entries(directory: Path, entry_label: str) -> dict[str, Path]:
		try:
			with os.scandir(directory) as iterator:
				children = list(iterator)
		except OSError as error:
			raise AssemblyError(f"{entry_label}: unable to enumerate payload: {error}") from error
		result = {entry.name: Path(entry.path) for entry in children}
		_expect(len(result) == len(children), entry_label, "payload contains duplicate names")
		return result

	def compare(stage_dir: Path, launched_dir: Path, relative: Path) -> None:
		stage_entries = entries(stage_dir, f"{label}.stage/{relative.as_posix()}")
		launched_entries = entries(launched_dir, f"{label}.app/{relative.as_posix()}")
		_expect(set(stage_entries) == set(launched_entries), f"{label}.app/{relative.as_posix()}", "launched payload names differ from the pinned stage")
		for name in sorted(stage_entries):
			source = stage_entries[name]
			target = launched_entries[name]
			try:
				source_metadata = os.lstat(source)
				target_metadata = os.lstat(target)
			except OSError as error:
				raise AssemblyError(f"{label}: unable to inspect launched payload entry: {error}") from error
			target_reparse = is_reparse(target)
			child_relative = relative / name
			if stat.S_ISDIR(source_metadata.st_mode):
				_expect(not target_reparse and stat.S_ISDIR(target_metadata.st_mode), f"{label}.app/{child_relative.as_posix()}", "launched directory is not an independent non-reparse snapshot")
				compare(source, target, child_relative)
				continue
			_expect(stat.S_ISREG(source_metadata.st_mode), f"{label}.stage/{child_relative.as_posix()}", "stage contains a non-regular entry")
			_expect(not target_reparse and stat.S_ISREG(target_metadata.st_mode), f"{label}.app/{child_relative.as_posix()}", "launched file is not a regular non-reparse file")
			_expect(target_metadata.st_nlink == 1, f"{label}.app/{child_relative.as_posix()}", "launched file is a hardlink alias instead of an independent snapshot")
			_expect((source_metadata.st_dev, source_metadata.st_ino) != (target_metadata.st_dev, target_metadata.st_ino), f"{label}.app/{child_relative.as_posix()}", "launched file aliases the pinned stage file")
			_expect(source_metadata.st_size == target_metadata.st_size and file_sha256(source) == file_sha256(target), f"{label}.app/{child_relative.as_posix()}", "launched file bytes differ from the pinned stage")

	compare(stage_root, app_root, Path("."))
	try:
		launched_attestation = payload_tree_attestation(app_root)
	except PayloadIdentityError as error:
		raise AssemblyError(f"{label}: {error}") from error
	_expect(
		launched_attestation["file_count"] == stage.file_count and launched_attestation["sha256"] == stage.sha256,
		label, "full launched runtime identity differs from the pinned stage",
	)
	# Reopen the pinned stage after comparing the launch view. This also proves
	# that junction-backed app directories did not point at a mutable substitute.
	stage_records = payload_tree_records(stage_root)
	_expect(len(stage_records) == stage.file_count and canonical_json_sha256(stage_records) == stage.sha256, label, "pinned stage changed while the launch view was verified")
	return {
		"path": str(app_root), "logical_stage_path": str(stage_root),
		"sha256": launched_attestation["sha256"], "file_count": launched_attestation["file_count"],
	}


def _artifact_path(manifest: Mapping[str, Any], name: str, run_root: Path, label: str) -> Path:
	artifacts = _mapping(manifest.get("artifacts"), f"{label}.artifacts")
	path = _regular_file(Path(_text(artifacts.get(name), f"{label}.artifacts.{name}")), f"{label}.artifacts.{name}")
	try:
		path.relative_to(run_root)
	except ValueError as error:
		raise AssemblyError(f"{label}.artifacts.{name}: artifact escapes the isolated run root") from error
	return path


def _json_artifact_equals(path: Path, expected: Any, label: str) -> Mapping[str, Any]:
	try:
		before = payload_file_attestation(path)
		raw = path.read_bytes()
		after = payload_file_attestation(path)
	except OSError as error:
		raise AssemblyError(f"{label}: unable to read artifact: {error}") from error
	except PayloadIdentityError as error:
		raise AssemblyError(f"{label}: {error}") from error
	_expect(before == after, label, "artifact changed while it was read")
	_expect(len(raw) == before["size_bytes"] and hashlib.sha256(raw).hexdigest() == before["sha256"], label, "artifact read differs from its stable file attestation")
	value = _load_json_bytes(raw, label)
	_expect(value == expected, label, "artifact JSON differs from the copy embedded in the E2E manifest")
	return value


@dataclass(frozen=True)
class RunEvidence:
	manifest: AttestedFile
	launched_payload: Mapping[str, Any]
	implementation: str
	profile: str
	input_pcm_sha256: str
	pre_opus_pcm_sha256: str
	pre_opus_artifact_pcm_sha256: str
	opus_packets_sha256: str
	received_pcm_sha256: str
	capture_path: Path
	capture_wave: WaveInfo
	packet_count: int
	terminator_count: int
	model_initialization_attempts: int
	algorithmic_latency_samples: int
	fallback_count: int
	deadline_miss_count: int
	received_samples: int
	timeline_passed: bool
	onset_loss_samples: int
	end_loss_samples: int
	missing_tail_samples: int
	clipped_samples: int


def _validate_remote_cleanup(value: Any, label: str) -> None:
	remote = _mapping(value, label)
	_required_keys(
		remote,
		{"active", "diagnostics_captured", "drain_completed", "drained_samples", "forced_off", "processor_ready",
		 "reported_latency_samples", "requested_enabled", "used_fallback", "was_applied"},
		label,
	)
	_expect(remote["diagnostics_captured"] is True and remote["drain_completed"] is True and remote["forced_off"] is True, label, "receiver cleanup-off diagnostics are incomplete")
	for field in ("active", "processor_ready", "requested_enabled", "used_fallback", "was_applied"):
		_expect(remote[field] is False, f"{label}.{field}", "receiver cleanup was not fully disabled")
	_expect(_integerish(remote["reported_latency_samples"], f"{label}.reported_latency_samples") == 0, label, "receiver latency must be zero")
	_expect(_integerish(remote["drained_samples"], f"{label}.drained_samples") == 0, label, "receiver cleanup drained samples while disabled")


def _validate_run_manifest(
	reference: Any,
	base: Path,
	case: Mapping[str, Any],
	implementation: str,
	client: ClientIdentity,
	server: ServerIdentity,
	tools: ToolIdentity,
	corpus: CorpusBinding,
	transport_binding: Mapping[str, Any],
	label: str,
) -> RunEvidence:
	manifest, manifest_ref = _attested_json(reference, base, label)
	_required_keys(
		manifest,
		{"artifact_kind", "artifacts", "attested_stage_only", "build", "cleanup", "completion", "input", "phase",
		 "preflight_only", "repo_root", "run_root", "schema_version", "skip_build", "status", "transport",
		 "voice_contract_evidence", "quality", "qualification_provenance"},
		label,
	)
	_expect(manifest["schema_version"] == 1 and manifest["artifact_kind"] == "speech_cleanup_e2e", label, "unsupported E2E manifest")
	_expect(manifest["status"] == "passed" and manifest["phase"] == "complete", label, "E2E run did not pass")
	_expect(manifest["preflight_only"] is False and manifest["skip_build"] is True and manifest["attested_stage_only"] is True, label, "run was not an attested, no-build execution")
	run_provenance = _mapping(manifest["qualification_provenance"], f"{label}.qualification_provenance")
	_exact_keys(run_provenance, RUN_PROVENANCE_KEYS, f"{label}.qualification_provenance")
	for field, expected_tool in (("wrapper", tools.wrapper), ("fixed_timeline_scorer", tools.scorer)):
		reported_tool = _file_reference(run_provenance[field], base, f"{label}.qualification_provenance.{field}")
		_same_path(reported_tool.path, expected_tool.path, f"{label}.qualification_provenance.{field}.path")
		_expect(reported_tool.sha256 == expected_tool.sha256 and reported_tool.size_bytes == expected_tool.size_bytes, f"{label}.qualification_provenance.{field}", "run used a different pinned tool")
	run_root = _directory(Path(_text(manifest["run_root"], f"{label}.run_root")), f"{label}.run_root")
	_same_path(manifest_ref.path, run_root / "manifest.json", f"{label}.run_root")
	launched_payload = _validate_launched_payload(run_root / "app", client.stage_payload, f"{label}.launched_payload")
	launched_client = _regular_file(run_root / "app" / "mumble.exe", f"{label}.launched_client")
	_expect(file_sha256(launched_client) == client.executable_sha256, f"{label}.launched_client", "actual launched stage client differs from the pinned client")

	profile = "Legacy" if implementation == "legacy" else "Original"
	expected_mode = str(case["transmit_mode"])
	wrapper_mode = {"continuous": "Continuous", "push_to_talk": "PTT", "vad": "VAD"}[expected_mode]
	cleanup = _mapping(manifest["cleanup"], f"{label}.cleanup")
	sender_cleanup = _mapping(cleanup.get("sender"), f"{label}.cleanup.sender")
	receiver_cleanup = _mapping(cleanup.get("receiver"), f"{label}.cleanup.receiver")
	_expect(sender_cleanup.get("mode") == "Off", f"{label}.cleanup.sender.mode", "legacy cleanup mode must be Off")
	_expect(sender_cleanup.get("input_enhancement_profile") == profile, f"{label}.cleanup.sender.input_enhancement_profile", "profile mismatch")
	_expect(sender_cleanup.get("auto_adapt") is False, f"{label}.cleanup.sender.auto_adapt", "Auto adaptation must be disabled")
	_expect(receiver_cleanup.get("enabled") is False, f"{label}.cleanup.receiver.enabled", "receiver cleanup must be disabled")

	transport = _mapping(manifest["transport"], f"{label}.transport")
	_expect(transport.get("host") == transport_binding["server_host"], f"{label}.transport.host", "server host mismatch")
	_expect(transport.get("voice_transport") == transport_binding["voice_transport"], f"{label}.transport.voice_transport", "voice transport mismatch")
	_expect(_integer(transport.get("bitrate_bps"), f"{label}.transport.bitrate_bps") == case["bitrate_bps"], label, "bitrate mismatch")
	_expect(_integer(transport.get("frames_per_packet"), f"{label}.transport.frames_per_packet") == case["frames_per_packet"], label, "frames-per-packet mismatch")
	_expect(transport.get("transmit_mode") == wrapper_mode, f"{label}.transport.transmit_mode", "transmit mode mismatch")
	for field in ("pre_roll_frames", "tail_frames", "drain_milliseconds"):
		_expect(_integer(transport.get(field), f"{label}.transport.{field}") == transport_binding[field], label, f"{field} mismatch")

	build = _mapping(manifest["build"], f"{label}.build")
	_required_keys(
		build,
		{"client_build_dir", "client_executable_sha256", "client_exe", "client_stage_dir", "git_dirty", "git_head",
		 "server_build_dir", "server_executable_sha256", "server_exe", "server_git_dirty", "server_git_head",
		 "server_source_root", "source_root"},
		f"{label}.build",
	)
	_expect(build["git_head"] == client.commit and build["git_dirty"] is False, f"{label}.build", "client build did not come from the clean pinned commit")
	_expect(build["server_git_head"] == server.commit and build["server_git_dirty"] is False, f"{label}.build", "server build did not come from the clean pinned commit")
	_expect(_sha256(build["client_executable_sha256"], f"{label}.build.client_executable_sha256") == client.executable_sha256, f"{label}.build", "client executable hash mismatch")
	_expect(_sha256(build["server_executable_sha256"], f"{label}.build.server_executable_sha256") == server.executable.sha256, f"{label}.build", "server executable hash mismatch")
	_same_path(build["client_exe"], client.build_executable.path, f"{label}.build.client_exe")
	_same_path(build["client_build_dir"], client.build_executable.path.parent, f"{label}.build.client_build_dir")
	_same_path(build["client_stage_dir"], client.stage_payload.path, f"{label}.build.client_stage_dir")
	_same_path(build["server_exe"], server.executable.path, f"{label}.build.server_exe")
	_same_path(build["server_build_dir"], server.executable.path.parent, f"{label}.build.server_build_dir")
	_same_path(build["source_root"], client.worktree.source_root, f"{label}.build.source_root")
	_same_path(build["server_source_root"], server.worktree.source_root, f"{label}.build.server_source_root")

	input_binding = _mapping(manifest["input"], f"{label}.input")
	_expect(input_binding.get("unbaselined_voice_contract_control") is True, f"{label}.input.unbaselined_voice_contract_control", "release parity control was not explicitly scored without a transport baseline")
	_expect(_sha256(input_binding.get("sha256"), f"{label}.input.sha256") == corpus.input_wav.sha256, f"{label}.input", "input fixture hash mismatch")
	_expect(_sha256(input_binding.get("clean_reference_sha256"), f"{label}.input.clean_reference_sha256") == corpus.clean_wav.sha256, f"{label}.input", "clean-reference hash mismatch")
	_same_path(input_binding.get("path"), corpus.input_wav.path, f"{label}.input.path")
	_same_path(input_binding.get("clean_reference_path"), corpus.clean_wav.path, f"{label}.input.clean_reference_path")

	completion = _mapping(manifest["completion"], f"{label}.completion")
	_required_keys(completion, {"capture", "capture_file", "input", "sender_pre_opus_file"}, f"{label}.completion")
	input_result = _mapping(completion["input"], f"{label}.completion.input")
	capture_result = _mapping(completion["capture"], f"{label}.completion.capture")
	input_done_path = _artifact_path(manifest, "input_done", run_root, label)
	capture_done_path = _artifact_path(manifest, "capture_done", run_root, label)
	_json_artifact_equals(input_done_path, input_result, f"{label}.artifacts.input_done")
	_json_artifact_equals(capture_done_path, capture_result, f"{label}.artifacts.capture_done")
	_expect(input_result.get("ok") is True and input_result.get("role") == "input" and input_result.get("error") == "", f"{label}.completion.input", "sender input did not complete cleanly")
	_expect(_integerish(input_result.get("sample_rate"), f"{label}.completion.input.sample_rate") == SAMPLE_RATE_HZ, label, "sender sample rate mismatch")
	_expect(_integerish(input_result.get("pre_roll_frames"), f"{label}.completion.input.pre_roll_frames") == 0, label, "sender preroll mismatch")
	_expect(_integerish(input_result.get("tail_frames"), f"{label}.completion.input.tail_frames") == 0, label, "sender tail-frame mismatch")
	_expect(input_result.get("terminator_submitted") is True and input_result.get("used_fallback") is False, f"{label}.completion.input", "sender did not terminate cleanly")
	_expect(input_result.get("effective_cleanup_mode") == "Off", f"{label}.completion.input.effective_cleanup_mode", "legacy cleanup unexpectedly active")
	_expect(_integerish(input_result.get("reported_latency_samples"), f"{label}.completion.input.reported_latency_samples") == 0, label, "control latency must be zero")
	_expect(_integerish(input_result.get("drained_cleanup_samples"), f"{label}.completion.input.drained_cleanup_samples") == 0, label, "control drained enhancement samples")
	_expect(_integerish(input_result.get("source_frames"), f"{label}.completion.input.source_frames", 1) == corpus.input_wave.sample_count, label, "sender source frame count mismatch")
	_expect(_integerish(input_result.get("submitted_frames"), f"{label}.completion.input.submitted_frames", 1) % FRAME_SAMPLES == 0, label, "sender submitted partial callback")

	voice = _mapping(input_result.get("voice_contract"), f"{label}.completion.input.voice_contract")
	_exact_keys(voice, VOICE_CONTRACT_KEYS, f"{label}.completion.input.voice_contract")
	_expect(voice["schema_version"] == 1, f"{label}.voice_contract.schema_version", "expected schema 1")
	_expect(voice["implementation"] == implementation and voice["enhancement_profile"] == profile, f"{label}.voice_contract", "implementation/profile mismatch")
	_expect(_integer(voice["bitrate_bps"], f"{label}.voice_contract.bitrate_bps") == case["bitrate_bps"], label, "voice bitrate mismatch")
	_expect(_integer(voice["frames_per_packet"], f"{label}.voice_contract.frames_per_packet") == case["frames_per_packet"], label, "voice frames-per-packet mismatch")
	_expect(voice["transmit_mode"] == expected_mode, f"{label}.voice_contract.transmit_mode", "voice transmit mode mismatch")
	_expect(voice["ptt_hold_activated"] is (expected_mode == "push_to_talk"), f"{label}.voice_contract.ptt_hold_activated", "PTT hold evidence mismatch")
	_expect(voice["input_pcm_encoding"] == "ieee754-f32le", f"{label}.voice_contract.input_pcm_encoding", "unexpected input PCM encoding")
	_expect(voice["pre_opus_pcm_encoding"] == "signed-s16le", f"{label}.voice_contract.pre_opus_pcm_encoding", "unexpected pre-Opus encoding")
	_expect(voice["opus_packet_hash_framing"] == "u32le-length+payload", f"{label}.voice_contract.opus_packet_hash_framing", "unexpected Opus hash framing")
	_expect(voice["active_model_sha256"] == "", f"{label}.voice_contract.active_model_sha256", "control path activated a model")
	input_pcm_sha = _sha256(voice["input_pcm_sha256"], f"{label}.voice_contract.input_pcm_sha256")
	pre_opus_sha = _sha256(voice["pre_opus_pcm_sha256"], f"{label}.voice_contract.pre_opus_pcm_sha256")
	opus_sha = _sha256(voice["opus_packets_sha256"], f"{label}.voice_contract.opus_packets_sha256")
	safety = {
		field: _integer(voice[field], f"{label}.voice_contract.{field}", 0)
		for field in ("model_initialization_attempts", "algorithmic_latency_samples", "fallback_count", "deadline_miss_count")
	}
	_expect(all(value == 0 for value in safety.values()), f"{label}.voice_contract", "control path reported enhancement activity")
	packet_count = _integer(voice["packet_count"], f"{label}.voice_contract.packet_count", 1)
	terminator_count = _integer(voice["terminator_count"], f"{label}.voice_contract.terminator_count", 1)

	pre_opus_record = _mapping(completion["sender_pre_opus_file"], f"{label}.completion.sender_pre_opus_file")
	_exact_keys(pre_opus_record, {"bytes", "callbacks", "encoding", "path", "sample_frames", "sha256", "timeline_origin"}, f"{label}.completion.sender_pre_opus_file")
	pre_opus_path = _artifact_path(manifest, "sender_pre_opus_wav", run_root, label)
	_same_path(pre_opus_record["path"], pre_opus_path, f"{label}.completion.sender_pre_opus_file.path")
	_expect(_integer(pre_opus_record["bytes"], f"{label}.completion.sender_pre_opus_file.bytes", 45) == pre_opus_path.stat().st_size, label, "pre-Opus WAV size mismatch")
	_expect(_sha256(pre_opus_record["sha256"], f"{label}.completion.sender_pre_opus_file.sha256") == file_sha256(pre_opus_path), label, "pre-Opus WAV hash mismatch")
	_expect(pre_opus_record["encoding"] == "signed-s16le" and pre_opus_record["timeline_origin"] == "source-after-transmitted-preroll", label, "pre-Opus timeline contract mismatch")
	pre_opus_wave = _read_wave(pre_opus_path, f"{label}.sender_pre_opus_wav")
	_expect((pre_opus_wave.format_code, pre_opus_wave.channels, pre_opus_wave.sample_rate_hz, pre_opus_wave.bits_per_sample) == (1, 1, SAMPLE_RATE_HZ, 16), label, "pre-Opus WAV must be mono 48 kHz PCM16")
	pre_opus_artifact_pcm_sha = hashlib.sha256(pre_opus_wave.pcm).hexdigest()
	_expect(_integerish(pre_opus_record["sample_frames"], f"{label}.completion.sender_pre_opus_file.sample_frames", 1) == pre_opus_wave.sample_count, label, "pre-Opus sample count mismatch")
	_expect(pre_opus_wave.sample_count % FRAME_SAMPLES == 0, label, "pre-Opus WAV contains a partial callback")
	pre_opus_callbacks = _integerish(pre_opus_record["callbacks"], f"{label}.completion.sender_pre_opus_file.callbacks", 1)
	_expect(pre_opus_callbacks * FRAME_SAMPLES <= pre_opus_wave.sample_count, label, "pre-Opus callback count exceeds the reconstructed source timeline")
	if expected_mode != "vad":
		_expect(pre_opus_callbacks * FRAME_SAMPLES == pre_opus_wave.sample_count, label, "non-VAD pre-Opus callback count mismatch")
	pre_opus_capture = _mapping(input_result.get("pre_opus_capture"), f"{label}.completion.input.pre_opus_capture")
	_required_keys(pre_opus_capture, {"callbacks", "enabled", "encoding", "path", "sample_frames", "timeline_origin"}, f"{label}.completion.input.pre_opus_capture")
	_expect(pre_opus_capture["enabled"] is True and pre_opus_capture["encoding"] == "signed-s16le" and pre_opus_capture["timeline_origin"] == "source-after-transmitted-preroll", label, "raw pre-Opus capture attestation mismatch")
	_same_path(pre_opus_capture["path"], pre_opus_path, f"{label}.completion.input.pre_opus_capture.path")
	_expect(_integerish(pre_opus_capture["sample_frames"], f"{label}.completion.input.pre_opus_capture.sample_frames") == pre_opus_wave.sample_count, label, "raw pre-Opus sample count mismatch")
	raw_callbacks = _integerish(pre_opus_capture["callbacks"], f"{label}.completion.input.pre_opus_capture.callbacks", 1)
	_expect(raw_callbacks == pre_opus_callbacks, label, "raw and file pre-Opus callback counts differ")
	_expect(raw_callbacks * FRAME_SAMPLES <= pre_opus_wave.sample_count, label, "raw pre-Opus callback count exceeds the reconstructed source timeline")

	capture_record = _mapping(completion["capture_file"], f"{label}.completion.capture_file")
	_exact_keys(capture_record, {"bytes", "path", "sha256"}, f"{label}.completion.capture_file")
	capture_path = _artifact_path(manifest, "capture_wav", run_root, label)
	_same_path(capture_record["path"], capture_path, f"{label}.completion.capture_file.path")
	_expect(_integer(capture_record["bytes"], f"{label}.completion.capture_file.bytes", 45) == capture_path.stat().st_size, label, "capture WAV size mismatch")
	received_sha = _sha256(capture_record["sha256"], f"{label}.completion.capture_file.sha256")
	_expect(received_sha == file_sha256(capture_path), label, "capture WAV hash mismatch")
	capture_wave = _read_wave(capture_path, f"{label}.capture_wav")
	_expect((capture_wave.format_code, capture_wave.channels, capture_wave.sample_rate_hz, capture_wave.bits_per_sample) == (3, 1, SAMPLE_RATE_HZ, 32), label, "receiver capture must be mono 48 kHz float32")
	_expect(capture_result.get("ok") is True and capture_result.get("role") == "capture" and capture_result.get("error") == "", f"{label}.completion.capture", "receiver capture did not complete cleanly")
	_expect(_integerish(capture_result.get("sample_rate"), f"{label}.completion.capture.sample_rate") == SAMPLE_RATE_HZ, label, "capture sample rate mismatch")
	_expect(_integerish(capture_result.get("channels"), f"{label}.completion.capture.channels") == 1, label, "capture channel mismatch")
	_expect(_integerish(capture_result.get("captured_frames"), f"{label}.completion.capture.captured_frames", 1) == capture_wave.sample_count, label, "capture sample count mismatch")
	_expect(_integerish(capture_result.get("discarded_pre_roll_frames"), f"{label}.completion.capture.discarded_pre_roll_frames") == 0, label, "capture discarded preroll")
	_expect(capture_result.get("stop_gate_observed") is True, f"{label}.completion.capture.stop_gate_observed", "capture did not observe stop gate")
	_same_path(capture_result.get("capture_wav"), capture_path, f"{label}.completion.capture.capture_wav")
	_validate_remote_cleanup(capture_result.get("remote_cleanup"), f"{label}.completion.capture.remote_cleanup")

	voice_evidence = _mapping(manifest["voice_contract_evidence"], f"{label}.voice_contract_evidence")
	_exact_keys(voice_evidence, VOICE_EVIDENCE_KEYS, f"{label}.voice_contract_evidence")
	expected_projection = {
		"schema_version": 1,
		"implementation": implementation,
		"bitrate_bps": case["bitrate_bps"],
		"frames_per_packet": case["frames_per_packet"],
		"transmit_mode": expected_mode,
		"input_pcm_sha256": input_pcm_sha,
		"pre_opus_pcm_sha256": pre_opus_sha,
		"opus_packets_sha256": opus_sha,
		"received_pcm_sha256": received_sha,
		"packet_count": packet_count,
		"terminator_count": terminator_count,
		"enhancement_profile": profile,
		**safety,
	}
	_expect(dict(voice_evidence) == expected_projection, f"{label}.voice_contract_evidence", "projected voice evidence differs from raw sender evidence")

	quality = _mapping(manifest["quality"], f"{label}.quality")
	timeline_binding = _mapping(quality.get("fixed_timeline_binding"), f"{label}.quality.fixed_timeline_binding")
	_exact_keys(timeline_binding, FIXED_TIMELINE_BINDING_KEYS, f"{label}.quality.fixed_timeline_binding")
	_expect(
		dict(timeline_binding) == {
			"mode": "unbaselined-voice-contract-control",
			"reference_artifact": "input_wav",
			"scored_artifact": "sender_pre_opus_wav",
			"timeline_origin": "source-after-transmitted-preroll",
		},
		f"{label}.quality.fixed_timeline_binding",
		"control edge score was not bound to the absolute source-aligned pre-Opus artifact",
	)
	fixed_timeline = _mapping(quality.get("fixed_timeline"), f"{label}.quality.fixed_timeline")
	fixed_timeline_path = _artifact_path(manifest, "fixed_timeline_score", run_root, label)
	_json_artifact_equals(fixed_timeline_path, fixed_timeline, f"{label}.artifacts.fixed_timeline_score")
	_exact_keys(fixed_timeline, FIXED_TIMELINE_KEYS, f"{label}.quality.fixed_timeline")
	_expect(fixed_timeline["schema_version"] == 3 and fixed_timeline["scorer"] == "mumble-fixed-timeline-v3", f"{label}.quality.fixed_timeline", "unexpected scorer schema")
	_expect(fixed_timeline["passed"] is True and fixed_timeline["timeline_alignment"] == "fixed", f"{label}.quality.fixed_timeline", "unbaselined fixed timeline did not pass")
	_expect(fixed_timeline["transport_baseline"] is None, f"{label}.quality.fixed_timeline.transport_baseline", "control qualification must not self-baseline or subtract a transport baseline")
	_expect(_integer(fixed_timeline["sample_rate_hz"], f"{label}.fixed_timeline.sample_rate_hz") == SAMPLE_RATE_HZ, label, "timeline sample rate mismatch")
	_expect(_integer(fixed_timeline["frame_samples"], f"{label}.fixed_timeline.frame_samples") == FRAME_SAMPLES, label, "timeline frame size mismatch")
	_expect(_integer(fixed_timeline["declared_latency_samples"], f"{label}.fixed_timeline.declared_latency_samples") == 0, label, "control timeline declared latency")
	_expect(_sha256(fixed_timeline["reference_sha256"], f"{label}.fixed_timeline.reference_sha256") == corpus.input_wav.sha256, label, "timeline input reference hash mismatch")
	_expect(_sha256(fixed_timeline["received_sha256"], f"{label}.fixed_timeline.received_sha256") == file_sha256(pre_opus_path), label, "timeline scored pre-Opus artifact hash mismatch")
	_expect(_integer(fixed_timeline["reference_samples"], f"{label}.fixed_timeline.reference_samples", 1) == corpus.input_wave.sample_count, label, "timeline input sample count mismatch")
	timeline_received_samples = _integer(fixed_timeline["received_samples"], f"{label}.fixed_timeline.received_samples", 1)
	_expect(timeline_received_samples == pre_opus_wave.sample_count, label, "timeline pre-Opus sample count mismatch")
	_expect(_integer(fixed_timeline["compared_samples"], f"{label}.fixed_timeline.compared_samples", 1) <= max(corpus.input_wave.sample_count, timeline_received_samples), label, "timeline comparison length is invalid")
	limits = _mapping(fixed_timeline["qualification_limits"], f"{label}.fixed_timeline.qualification_limits")
	_exact_keys(limits, QUALIFICATION_LIMIT_KEYS, f"{label}.fixed_timeline.qualification_limits")
	_expect(
		dict(limits) == {
			"max_onset_loss_samples": FRAME_SAMPLES, "max_end_loss_samples": FRAME_SAMPLES,
			"require_complete_tail": True, "fail_on_new_clipping": True,
		},
		f"{label}.fixed_timeline.qualification_limits", "timeline qualification limits differ from the release contract",
	)
	onset = _integer(fixed_timeline["onset_loss_samples"], f"{label}.fixed_timeline.onset_loss_samples", 0)
	end = _integer(fixed_timeline["end_loss_samples"], f"{label}.fixed_timeline.end_loss_samples", 0)
	tail = _integer(fixed_timeline["missing_tail_samples"], f"{label}.fixed_timeline.missing_tail_samples", 0)
	timeline_clipped = _integer(fixed_timeline["received_clipped_samples"], f"{label}.fixed_timeline.received_clipped_samples", 0)
	reference_clipped = _integer(fixed_timeline["reference_clipped_samples"], f"{label}.fixed_timeline.reference_clipped_samples", 0)
	_expect(onset <= FRAME_SAMPLES and end <= FRAME_SAMPLES, f"{label}.quality.fixed_timeline", "receiver edge loss exceeds one frame")
	_expect(tail == 0, f"{label}.quality.fixed_timeline.missing_tail_samples", "receiver tail is incomplete")
	_expect(timeline_clipped == 0 and timeline_clipped <= reference_clipped, f"{label}.quality.fixed_timeline.received_clipped_samples", "pre-Opus path introduced clipping")
	receiver_clipped = _wave_clipped_samples(capture_wave, f"{label}.capture_wav")
	_expect(receiver_clipped == 0, f"{label}.capture_wav", "receiver capture contains clipped samples")
	received_pcm_sha = hashlib.sha256(capture_wave.pcm).hexdigest()

	return RunEvidence(
		manifest_ref, launched_payload, implementation, profile, input_pcm_sha, pre_opus_sha, pre_opus_artifact_pcm_sha,
		opus_sha, received_pcm_sha, capture_path, capture_wave, packet_count,
		terminator_count, safety["model_initialization_attempts"], safety["algorithmic_latency_samples"],
		safety["fallback_count"], safety["deadline_miss_count"], capture_wave.sample_count, True, onset, end, tail, receiver_clipped,
	)


def _case_key(value: Mapping[str, Any]) -> tuple[int, int, str]:
	return (int(value["bitrate_bps"]), int(value["frames_per_packet"]), str(value["transmit_mode"]))


@functools.lru_cache(maxsize=8)
def _load_attested_scorer(path_text: str, expected_sha256: str) -> Any:
	path = _regular_file(Path(path_text), "receiver timeline scorer")
	try:
		attestation = payload_file_attestation(path)
	except PayloadIdentityError as error:
		raise AssemblyError(f"receiver timeline scorer: {error}") from error
	_expect(attestation["sha256"] == expected_sha256, "receiver timeline scorer", "bytes differ from the pinned scorer")
	_expect(path.suffix.lower() == ".py", "receiver timeline scorer", "pinned scorer must be a Python source file")
	module_name = f"mumble_original_receiver_scorer_{expected_sha256}"
	spec = importlib.util.spec_from_file_location(module_name, path)
	_expect(spec is not None and spec.loader is not None, "receiver timeline scorer", "unable to create module spec")
	module = importlib.util.module_from_spec(spec)
	sys.modules[module_name] = module
	try:
		spec.loader.exec_module(module)
	except Exception as error:
		raise AssemblyError(f"receiver timeline scorer: unable to load pinned scorer: {error}") from error
	_expect(callable(getattr(module, "score", None)), "receiver timeline scorer", "pinned scorer has no score function")
	return module


def _receiver_score(
	scorer: Any,
	reference: Path,
	received: Path,
	*,
	baseline: Path | None,
	qualified_baseline: bool,
	max_edge_loss_samples: int,
	label: str,
) -> Mapping[str, Any]:
	try:
		raw = scorer.score(reference, received, 0, baseline, 0, qualified_baseline)
	except Exception as error:
		raise AssemblyError(f"{label}: pinned fixed-timeline scorer failed: {error}") from error
	result = _mapping(raw, label)
	_required_keys(
		result,
		{
			"compared_samples", "declared_latency_samples", "end_loss_samples", "expected_end_samples",
			"expected_onset_samples", "fixed_timeline_sdr_db", "frame_samples", "loudness_match_gain",
			"missing_tail_samples", "onset_loss_samples", "received_clipped_samples", "received_end_samples",
			"received_onset_samples", "received_samples", "received_sha256", "reference_clipped_samples",
			"reference_end_samples", "reference_onset_samples", "reference_samples", "reference_sha256",
			"sample_rate_hz", "schema_version", "scorer", "timeline_alignment", "transport_baseline",
		},
		label,
	)
	_expect(result["schema_version"] == 3 and result["scorer"] == "mumble-fixed-timeline-v3", label, "unexpected scorer contract")
	_expect(_integer(result["sample_rate_hz"], f"{label}.sample_rate_hz") == SAMPLE_RATE_HZ, label, "sample-rate mismatch")
	_expect(_integer(result["frame_samples"], f"{label}.frame_samples") == FRAME_SAMPLES, label, "frame-size mismatch")
	_expect(_integer(result["declared_latency_samples"], f"{label}.declared_latency_samples") == 0, label, "Original route declared enhancement latency")
	_expect(_sha256(result["reference_sha256"], f"{label}.reference_sha256") == file_sha256(reference), label, "reference hash mismatch")
	_expect(_sha256(result["received_sha256"], f"{label}.received_sha256") == file_sha256(received), label, "receiver hash mismatch")
	onset = _integer(result["onset_loss_samples"], f"{label}.onset_loss_samples", 0)
	end = _integer(result["end_loss_samples"], f"{label}.end_loss_samples", 0)
	tail = _integer(result["missing_tail_samples"], f"{label}.missing_tail_samples", 0)
	received_clipped = _integer(result["received_clipped_samples"], f"{label}.received_clipped_samples", 0)
	reference_clipped = _integer(result["reference_clipped_samples"], f"{label}.reference_clipped_samples", 0)
	passed = (
		onset <= max_edge_loss_samples and end <= max_edge_loss_samples and tail == 0
		and received_clipped <= reference_clipped
	)
	sealed = {
		**dict(result),
		"qualification_limits": {
			"max_onset_loss_samples": max_edge_loss_samples,
			"max_end_loss_samples": max_edge_loss_samples,
			"require_complete_tail": True,
			"fail_on_new_clipping": True,
		},
		"passed": passed,
	}
	# Canonical serialization rejects non-finite metrics emitted by a compromised
	# or incompatible scorer before any value can enter release evidence.
	canonical_json_bytes(sealed)
	_expect(passed, label, "actual receiver route failed fixed-timeline qualification")
	return sealed


def _receiver_pair_evidence(
	case: Mapping[str, Any],
	legacy: RunEvidence,
	original: RunEvidence,
	corpus: CorpusBinding,
	tools: ToolIdentity,
) -> Mapping[str, Any]:
	scorer = _load_attested_scorer(str(tools.scorer.path), tools.scorer.sha256)
	for label, run in (("legacy", legacy), ("candidate", original)):
		try:
			current = payload_file_attestation(run.capture_path)
		except PayloadIdentityError as error:
			raise AssemblyError(f"{label} receiver capture: {error}") from error
		_expect(
			current["sha256"] == run.capture_wave.container_sha256,
			f"{label} receiver capture", "changed before paired receiver scoring",
		)
	route_budget = (ORIGINAL_ROUTE_FIXED_STARTUP_FRAMES + int(case["frames_per_packet"])) * FRAME_SAMPLES
	legacy_independent = _receiver_score(
		scorer, corpus.input_wav.path, legacy.capture_path, baseline=None, qualified_baseline=False,
		max_edge_loss_samples=route_budget, label="legacy receiver route control",
	)
	_expect(
		legacy_independent["timeline_alignment"] == "fixed" and legacy_independent["transport_baseline"] is None,
		"legacy receiver route control", "independent control unexpectedly used a transport baseline",
	)
	legacy_paired = _receiver_score(
		scorer, corpus.input_wav.path, legacy.capture_path, baseline=legacy.capture_path,
		qualified_baseline=True, max_edge_loss_samples=FRAME_SAMPLES,
		label="legacy receiver qualified-baseline projection",
	)
	original_paired = _receiver_score(
		scorer, corpus.input_wav.path, original.capture_path, baseline=legacy.capture_path,
		qualified_baseline=True, max_edge_loss_samples=FRAME_SAMPLES,
		label="candidate Original receiver paired route",
	)
	for label, score in (("legacy", legacy_paired), ("candidate", original_paired)):
		baseline = _mapping(score["transport_baseline"], f"{label} receiver transport_baseline")
		_expect(
			baseline.get("qualification") == "caller-verified-passing-original"
			and baseline.get("sha256") == file_sha256(legacy.capture_path)
			and score["timeline_alignment"] == "fixed-paired-original-route",
			f"{label} receiver transport_baseline",
			"paired receiver score did not use the independently qualified Legacy route",
		)
	for label, run in (("legacy", legacy), ("candidate", original)):
		try:
			current = payload_file_attestation(run.capture_path)
		except PayloadIdentityError as error:
			raise AssemblyError(f"{label} receiver capture: {error}") from error
		_expect(
			current["sha256"] == run.capture_wave.container_sha256,
			f"{label} receiver capture", "changed while paired receiver scoring ran",
		)
	return {
		"schema_version": 1,
		"scorer": tools.scorer.receipt(),
		"reference": corpus.input_wav.receipt(),
		"baseline_contract": {
			"implementation": "legacy",
			"independent_route_budget_samples": route_budget,
			"independent_score": legacy_independent,
		},
		"legacy_paired_score": legacy_paired,
		"original_paired_score": original_paired,
	}


def _qualification_case(
	case: Mapping[str, Any], legacy: RunEvidence, original: RunEvidence,
	legacy_identity: ClientIdentity, candidate_identity: ClientIdentity,
	corpus: CorpusBinding, tools: ToolIdentity,
) -> tuple[Mapping[str, Any], Mapping[str, Any]]:
	case_name = f"case {case['bitrate_bps']}/{case['frames_per_packet']}/{case['transmit_mode']}"
	_expect(legacy.input_pcm_sha256 == original.input_pcm_sha256, case_name, "input PCM differs between Legacy and Original")
	_expect(legacy.pre_opus_pcm_sha256 == original.pre_opus_pcm_sha256, case_name, "pre-Opus PCM differs between Legacy and Original")
	_expect(legacy.pre_opus_artifact_pcm_sha256 == original.pre_opus_artifact_pcm_sha256, case_name, "captured pre-Opus source timelines differ between Legacy and Original")
	_expect(legacy.opus_packets_sha256 == original.opus_packets_sha256, case_name, "Opus packets differ between Legacy and Original")
	_expect(legacy.packet_count == original.packet_count, case_name, "packet counts differ")
	_expect(legacy.terminator_count == original.terminator_count, case_name, "terminator counts differ")
	receiver_jitter_delta = original.received_samples - legacy.received_samples
	_expect(
		abs(receiver_jitter_delta) <= FRAME_SAMPLES,
		case_name,
		"receiver capture lengths differ by more than one legitimate localhost jitter frame",
	)
	receiver = _receiver_pair_evidence(case, legacy, original, corpus, tools)
	legacy_receiver = _mapping(receiver["legacy_paired_score"], f"{case_name}.legacy_receiver")
	original_receiver = _mapping(receiver["original_paired_score"], f"{case_name}.original_receiver")
	result = {
		"bitrate_bps": case["bitrate_bps"],
		"frames_per_packet": case["frames_per_packet"],
		"transmit_mode": case["transmit_mode"],
		"enhancement_profile": "Original",
		"model_initialization_attempts": original.model_initialization_attempts,
		"algorithmic_latency_samples": original.algorithmic_latency_samples,
		"fallback_count": original.fallback_count,
		"deadline_miss_count": original.deadline_miss_count,
		"legacy_executable_sha256": legacy_identity.executable_sha256,
		"candidate_executable_sha256": candidate_identity.executable_sha256,
		"input_pcm_sha256": original.input_pcm_sha256,
		"legacy_input_pcm_sha256": legacy.input_pcm_sha256,
		"original_input_pcm_sha256": original.input_pcm_sha256,
		"legacy_pcm_sha256": legacy.pre_opus_pcm_sha256,
		"original_pcm_sha256": original.pre_opus_pcm_sha256,
		"legacy_opus_packets_sha256": legacy.opus_packets_sha256,
		"original_opus_packets_sha256": original.opus_packets_sha256,
		"legacy_received_pcm_sha256": legacy.received_pcm_sha256,
		"original_received_pcm_sha256": original.received_pcm_sha256,
		"legacy_received_sample_count": legacy.received_samples,
		"original_received_sample_count": original.received_samples,
		"receiver_jitter_delta_samples": receiver_jitter_delta,
		"legacy_receiver_fixed_timeline_passed": legacy_receiver["passed"],
		"legacy_receiver_onset_loss_samples": legacy_receiver["onset_loss_samples"],
		"legacy_receiver_end_loss_samples": legacy_receiver["end_loss_samples"],
		"legacy_receiver_missing_tail_samples": legacy_receiver["missing_tail_samples"],
		"legacy_receiver_clipped_samples": legacy_receiver["received_clipped_samples"],
		"original_receiver_fixed_timeline_passed": original_receiver["passed"],
		"original_receiver_onset_loss_samples": original_receiver["onset_loss_samples"],
		"original_receiver_end_loss_samples": original_receiver["end_loss_samples"],
		"original_receiver_missing_tail_samples": original_receiver["missing_tail_samples"],
		"original_receiver_clipped_samples": original_receiver["received_clipped_samples"],
		"legacy_packet_count": legacy.packet_count,
		"original_packet_count": original.packet_count,
		"legacy_terminator_count": legacy.terminator_count,
		"original_terminator_count": original.terminator_count,
	}
	_exact_keys(result, QUALIFICATION_CASE_KEYS, case_name)
	return result, receiver


def _identity_receipt(client: ClientIdentity) -> Mapping[str, Any]:
	receipt = {
		"commit": client.commit,
		"worktree_receipt": client.worktree.file.receipt(),
		"build_executable": client.build_executable.receipt(),
		"stage_executable": client.stage_executable.receipt(),
		"qualified_executable": client.qualified_executable.receipt(),
		"stage_payload": client.stage_payload.receipt(),
	}
	if client.role == "legacy":
		receipt["instrumentation_base_commit"] = LEGACY_INSTRUMENTATION_BASE_COMMIT
	return receipt


def assemble(
	bindings_path: Path,
	expected_bindings_sha256: str,
	candidate_commit: str,
	legacy_build_commit: str,
	legacy_instrumentation_base: str,
	legacy_executable_sha256: str,
	*,
	verify_live_git: bool = True,
) -> tuple[Mapping[str, Any], Mapping[str, Any]]:
	bindings_file = _regular_file(bindings_path, "campaign bindings")
	expected_bindings_hash = _sha256(expected_bindings_sha256, "expected campaign-bindings SHA-256")
	try:
		bindings_before = payload_file_attestation(bindings_file)
		bindings_bytes = bindings_file.read_bytes()
		bindings_after = payload_file_attestation(bindings_file)
	except PayloadIdentityError as error:
		raise AssemblyError(f"campaign bindings: {error}") from error
	_expect(bindings_before == bindings_after, "campaign bindings", "changed while being read")
	_expect(hashlib.sha256(bindings_bytes).hexdigest() == expected_bindings_hash, "campaign bindings", "bytes differ from the external SHA-256 pin")
	bindings = _load_json_bytes(bindings_bytes, "campaign bindings")
	_exact_keys(bindings, ROOT_KEYS, "campaign bindings")
	_expect(bindings["schema_version"] == 1 and bindings["kind"] == BINDING_KIND, "campaign bindings", "unsupported bindings contract")
	campaign_id = _text(bindings["campaign_id"], "campaign bindings.campaign_id")
	_expect(bool(re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,127}", campaign_id)), "campaign bindings.campaign_id", "unsafe campaign ID")
	expected_candidate_commit = _commit(candidate_commit, "--candidate-commit")
	expected_legacy_build = _commit(legacy_build_commit, "--legacy-build-commit")
	expected_legacy_base = _commit(legacy_instrumentation_base, "--legacy-instrumentation-base")
	expected_legacy_executable = _sha256(legacy_executable_sha256, "--legacy-executable-sha256")
	_expect(expected_legacy_build == LEGACY_BUILD_COMMIT, "--legacy-build-commit", "does not match the frozen buildable legacy reference")
	_expect(expected_legacy_base == LEGACY_INSTRUMENTATION_BASE_COMMIT, "--legacy-instrumentation-base", "does not match the frozen instrumentation base")
	_expect(expected_legacy_executable == LEGACY_EXECUTABLE_SHA256, "--legacy-executable-sha256", "does not match the frozen staged legacy client")
	legacy_revision_contract = _verify_legacy_revision_contract(expected_legacy_base, expected_legacy_build)
	candidate_revision_contract = _verify_candidate_revision_contract(expected_legacy_build, expected_candidate_commit)
	_expect(_commit(bindings["candidate_commit"], "campaign bindings.candidate_commit") == expected_candidate_commit, "campaign bindings.candidate_commit", "does not match --candidate-commit")
	base = bindings_file.parent

	identities = _mapping(bindings["identities"], "campaign bindings.identities")
	_exact_keys(identities, IDENTITY_KEYS, "campaign bindings.identities")
	legacy_identity = _validate_client_identity(identities["legacy"], base, "legacy", LEGACY_COMMIT, verify_live_git)
	candidate_identity = _validate_client_identity(identities["candidate"], base, "candidate", expected_candidate_commit, verify_live_git)
	server_identity = _validate_server_identity(identities["server"], base, verify_live_git)
	tools = _validate_tools(identities["tools"], base)
	client_snapshot_paths = [
		legacy_identity.build_executable.path, legacy_identity.stage_executable.path,
		legacy_identity.qualified_executable.path, candidate_identity.build_executable.path,
		candidate_identity.stage_executable.path, candidate_identity.qualified_executable.path,
	]
	_expect(
		len({os.path.normcase(str(path)) for path in client_snapshot_paths}) == len(client_snapshot_paths),
		"campaign bindings.identities", "legacy and candidate executable snapshots must all be distinct files",
	)
	_expect(
		os.path.normcase(str(legacy_identity.stage_payload.path))
		!= os.path.normcase(str(candidate_identity.stage_payload.path)),
		"campaign bindings.identities", "legacy and candidate stage payloads must be distinct snapshots",
	)
	_expect(
		os.path.normcase(str(tools.wrapper.path)) != os.path.normcase(str(tools.scorer.path)),
		"campaign bindings.identities.tools", "wrapper and scorer must be distinct pinned tools",
	)
	transport = _validate_transport(bindings["transport"])
	corpus = _validate_corpus(
		bindings["corpus"], base, PARITY_RENDERED_SAMPLES if verify_live_git else ALIGNMENT_SAMPLES,
	)

	case_bindings = _array(bindings["cases"], "campaign bindings.cases")
	_expect(len(case_bindings) == len(REQUIRED_MATRIX) == 45, "campaign bindings.cases", "expected exactly 45 paired cases")
	validated_case_bindings: list[Mapping[str, Any]] = []
	qualification_cases: list[Mapping[str, Any]] = []
	manifest_receipts: list[Mapping[str, Any]] = []
	for index, (raw_case, required_case) in enumerate(zip(case_bindings, REQUIRED_MATRIX)):
		case = _mapping(raw_case, f"campaign bindings.cases[{index}]")
		_exact_keys(case, CASE_BINDING_KEYS, f"campaign bindings.cases[{index}]")
		matrix_case = {
			"bitrate_bps": _integer(case["bitrate_bps"], f"campaign bindings.cases[{index}].bitrate_bps"),
			"frames_per_packet": _integer(case["frames_per_packet"], f"campaign bindings.cases[{index}].frames_per_packet"),
			"transmit_mode": _text(case["transmit_mode"], f"campaign bindings.cases[{index}].transmit_mode"),
		}
		_expect(matrix_case == required_case, f"campaign bindings.cases[{index}]", f"matrix order/content differs from required case {required_case}")
		legacy = _validate_run_manifest(
			case["legacy_manifest"], base, matrix_case, "legacy", legacy_identity, server_identity, tools, corpus,
			transport, f"case[{index}].legacy_manifest",
		)
		original = _validate_run_manifest(
			case["original_manifest"], base, matrix_case, "original", candidate_identity, server_identity, tools, corpus,
			transport, f"case[{index}].original_manifest",
		)
		qualification_case, receiver_timeline = _qualification_case(
			matrix_case, legacy, original, legacy_identity, candidate_identity, corpus, tools,
		)
		qualification_cases.append(qualification_case)
		validated_case_bindings.append(dict(case))
		manifest_receipts.append(
			{
				**matrix_case,
				"legacy_manifest": legacy.manifest.receipt(), "legacy_launched_payload": legacy.launched_payload,
				"original_manifest": original.manifest.receipt(), "original_launched_payload": original.launched_payload,
				"sender_pre_opus_timeline": {
					"legacy": {
						"passed": legacy.timeline_passed, "onset_loss_samples": legacy.onset_loss_samples,
						"end_loss_samples": legacy.end_loss_samples, "missing_tail_samples": legacy.missing_tail_samples,
					},
					"original": {
						"passed": original.timeline_passed, "onset_loss_samples": original.onset_loss_samples,
						"end_loss_samples": original.end_loss_samples, "missing_tail_samples": original.missing_tail_samples,
					},
				},
				"receiver_timeline": receiver_timeline,
			}
		)

	qualification: Mapping[str, Any] = {
		"schema_version": 1,
		"profile": "Original",
		"transport_path": transport["transport_path"],
		"server_host": transport["server_host"],
		"receiver_cleanup_enabled": False,
		"legacy_build_sha": LEGACY_BUILD_COMMIT,
		"candidate_build_sha": expected_candidate_commit,
		"legacy_executable_sha256": legacy_identity.executable_sha256,
		"candidate_executable_sha256": candidate_identity.executable_sha256,
		"corpus_sha256": corpus.lock_canonical_sha256,
		"cases": qualification_cases,
	}
	_exact_keys(qualification, QUALIFICATION_ROOT_KEYS, "assembled qualification")
	try:
		case_count = CHECKER.validate_qualification(qualification)
	except CHECKER.ContractError as error:
		raise AssemblyError(f"assembled qualification was rejected by the current Original checker: {error}") from error
	_expect(case_count == 45, "assembled qualification", "current checker did not validate exactly 45 cases")

	assembler = _regular_file(Path(__file__), "assembler")
	provenance: Mapping[str, Any] = {
		"schema_version": 1,
		"kind": PROVENANCE_KIND,
		"campaign_id": campaign_id,
		"campaign_bindings": {
			"path": str(bindings_file), "sha256": expected_bindings_hash, "size_bytes": len(bindings_bytes),
		},
		"assembler": {
			"path": str(assembler), "sha256": file_sha256(assembler), "size_bytes": assembler.stat().st_size,
		},
		"commits": {
			"legacy_instrumentation_base": LEGACY_INSTRUMENTATION_BASE_COMMIT,
			"legacy_build": LEGACY_BUILD_COMMIT,
			"candidate": expected_candidate_commit,
			"server": SERVER_COMMIT,
		},
		"legacy_revision_contract": legacy_revision_contract,
		"candidate_revision_contract": candidate_revision_contract,
		"identities": {
			"legacy": _identity_receipt(legacy_identity),
			"candidate": _identity_receipt(candidate_identity),
			"server": {
				"commit": server_identity.commit,
				"worktree_receipt": server_identity.worktree.file.receipt(),
				"executable": server_identity.executable.receipt(),
			},
			"tools": {"wrapper": tools.wrapper.receipt(), "scorer": tools.scorer.receipt()},
		},
		"transport": dict(transport),
		"corpus": {
			"corpus_lock": corpus.lock.receipt(),
			"corpus_lock_canonical_sha256": corpus.lock_canonical_sha256,
			"corpus_inventory": corpus.inventory.receipt(),
			"corpus_inventory_canonical_sha256": corpus.inventory_canonical_sha256,
			"mixture_plan": corpus.mixture_plan.receipt(),
			"mixture_plan_canonical_sha256": corpus.mixture_plan_canonical_sha256,
			"render_manifest": corpus.render_manifest.receipt(),
			"render_entry_sha256": corpus.render_entry_sha256,
			"selected_case_id": PARITY_CASE_ID,
			"fixture_attestation": corpus.fixture_attestation.receipt(),
			"input_wav": corpus.input_wav.receipt(),
			"clean_reference_wav": corpus.clean_wav.receipt(),
		},
		"matrix": {
			"required_pair_count": 45,
			"completed_pair_count": len(qualification_cases),
			"required_matrix_sha256": canonical_json_sha256(list(REQUIRED_MATRIX)),
			"case_bindings_sha256": canonical_json_sha256(validated_case_bindings),
			"manifests": manifest_receipts,
		},
	}

	# Reopen the global trust roots after all 90 manifests and their artifacts
	# have been consumed. A mutable payload cannot qualify by changing mid-run.
	for label, file_ref in (
		("legacy build executable", legacy_identity.build_executable),
		("legacy stage executable", legacy_identity.stage_executable),
		("legacy qualified executable", legacy_identity.qualified_executable),
		("candidate build executable", candidate_identity.build_executable),
		("candidate stage executable", candidate_identity.stage_executable),
		("candidate qualified executable", candidate_identity.qualified_executable),
		("server executable", server_identity.executable),
		("wrapper", tools.wrapper),
		("scorer", tools.scorer),
		("corpus lock", corpus.lock),
		("corpus inventory", corpus.inventory),
		("mixture plan", corpus.mixture_plan),
		("render manifest", corpus.render_manifest),
		("fixture attestation", corpus.fixture_attestation),
		("input WAV", corpus.input_wav),
		("clean-reference WAV", corpus.clean_wav),
	):
		try:
			current = payload_file_attestation(file_ref.path)
		except PayloadIdentityError as error:
			raise AssemblyError(f"{label}: {error}") from error
		_expect(current["size_bytes"] == file_ref.size_bytes and current["sha256"] == file_ref.sha256, label, "changed while qualification was assembled")
	for label, tree_ref in (("legacy stage payload", legacy_identity.stage_payload), ("candidate stage payload", candidate_identity.stage_payload)):
		attestation = payload_tree_attestation(tree_ref.path)
		_expect(attestation["file_count"] == tree_ref.file_count and attestation["sha256"] == tree_ref.sha256, label, "changed while qualification was assembled")
	try:
		final_bindings = payload_file_attestation(bindings_file)
	except PayloadIdentityError as error:
		raise AssemblyError(f"campaign bindings: {error}") from error
	_expect(final_bindings["sha256"] == expected_bindings_hash, "campaign bindings", "changed while qualification was assembled")
	return qualification, provenance


def _json_output_bytes(value: Mapping[str, Any]) -> bytes:
	try:
		return (
			json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True, allow_nan=False) + "\n"
		).encode("utf-8")
	except (TypeError, ValueError) as error:
		raise AssemblyError(f"refusing to emit non-finite or non-JSON evidence: {error}") from error


def _write_outputs(
	qualification: Mapping[str, Any], provenance: Mapping[str, Any], output_path: Path, provenance_path: Path,
) -> None:
	output = Path(os.path.abspath(os.fspath(output_path)))
	receipt = Path(os.path.abspath(os.fspath(provenance_path)))
	_expect(output != receipt, "output", "qualification and provenance paths must differ")
	_expect(output.parent == receipt.parent, "output", "qualification and provenance must be siblings for fail-closed publication")
	output.parent.mkdir(parents=True, exist_ok=True)
	parent = _directory(output.parent, "output parent")
	output = parent / output.name
	receipt = parent / receipt.name
	_expect(not output.exists() and not receipt.exists(), "output", "refusing to overwrite qualification evidence")
	qualification_bytes = _json_output_bytes(qualification)
	provenance_with_output = dict(provenance)
	provenance_with_output["qualification"] = {
		"path": output.name,
		"sha256": hashlib.sha256(qualification_bytes).hexdigest(),
		"size_bytes": len(qualification_bytes),
	}
	provenance_bytes = _json_output_bytes(provenance_with_output)
	qualification_temp = output.parent / f".{output.name}.{os.getpid()}.tmp"
	provenance_temp = receipt.parent / f".{receipt.name}.{os.getpid()}.tmp"
	try:
		for path, payload in ((qualification_temp, qualification_bytes), (provenance_temp, provenance_bytes)):
			with path.open("xb") as stream:
				stream.write(payload)
				stream.flush()
				os.fsync(stream.fileno())
		# Hard-link publication is an atomic create-if-absent operation on the same
		# directory. Unlike os.replace/rename it can never overwrite a target that
		# appears after the preflight existence check. Remove each temporary name
		# immediately so the published file returns to a single-link snapshot.
		# Publish the receipt first; an orphaned receipt is harmless, whereas a
		# qualification without its receipt could be consumed out of context.
		for temporary, target in ((provenance_temp, receipt), (qualification_temp, output)):
			try:
				os.link(temporary, target, follow_symlinks=False)
			except FileExistsError as error:
				raise AssemblyError(f"output: refusing to overwrite qualification evidence: {target}") from error
			except OSError as error:
				raise AssemblyError(f"output: atomic no-overwrite publication failed for {target}: {error}") from error
			temporary.unlink()
	finally:
		for path in (qualification_temp, provenance_temp):
			try:
				path.unlink()
			except FileNotFoundError:
				pass
	_expect(output.read_bytes() == qualification_bytes, "output", "qualification bytes changed after publication")
	_expect(receipt.read_bytes() == provenance_bytes, "output", "provenance bytes changed after publication")


def _test_write_json(path: Path, value: Mapping[str, Any]) -> None:
	path.parent.mkdir(parents=True, exist_ok=True)
	path.write_bytes(_json_output_bytes(value))


def _test_file_reference(path: Path) -> Mapping[str, Any]:
	resolved = path.resolve()
	return {"path": str(resolved), "sha256": file_sha256(resolved), "size_bytes": resolved.stat().st_size}


def _test_tree_reference(path: Path) -> Mapping[str, Any]:
	attestation = payload_tree_attestation(path)
	return {"path": str(path.resolve()), "sha256": attestation["sha256"], "file_count": attestation["file_count"]}


def _test_write_pcm16(path: Path, samples: Sequence[int]) -> None:
	path.parent.mkdir(parents=True, exist_ok=True)
	with wave.open(str(path), "wb") as stream:
		stream.setnchannels(1)
		stream.setsampwidth(2)
		stream.setframerate(SAMPLE_RATE_HZ)
		stream.writeframes(b"".join(struct.pack("<h", sample) for sample in samples))


def _test_write_float32(path: Path, samples: Sequence[float]) -> None:
	pcm = b"".join(struct.pack("<f", sample) for sample in samples)
	fmt = struct.pack("<HHIIHH", 3, 1, SAMPLE_RATE_HZ, SAMPLE_RATE_HZ * 4, 4, 32)
	riff_size = 4 + (8 + len(fmt)) + (8 + len(pcm))
	path.parent.mkdir(parents=True, exist_ok=True)
	path.write_bytes(
		b"RIFF" + struct.pack("<I", riff_size) + b"WAVE"
		+ b"fmt " + struct.pack("<I", len(fmt)) + fmt
		+ b"data" + struct.pack("<I", len(pcm)) + pcm
	)


@dataclass
class _SelfTestCampaign:
	root: Path
	bindings_path: Path
	bindings: MutableMapping[str, Any]
	candidate_commit: str
	manifests: Mapping[tuple[int, str], Path]
	paths: Mapping[str, Path]

	def bindings_sha256(self) -> str:
		return file_sha256(self.bindings_path)

	def rewrite_bindings(self) -> None:
		_test_write_json(self.bindings_path, self.bindings)

	def update_manifest(self, index: int, implementation: str, mutate: Any) -> None:
		path = self.manifests[(index, implementation)]
		document = _load_json_bytes(path.read_bytes(), f"self-test {implementation} manifest")
		mutable = copy.deepcopy(document)
		mutate(mutable)
		_test_write_json(path, mutable)
		field = "legacy_manifest" if implementation == "legacy" else "original_manifest"
		self.bindings["cases"][index][field] = _test_file_reference(path)
		self.rewrite_bindings()


def _test_worktree_receipt(path: Path, role: str, commit: str, source_root: Path) -> Mapping[str, Any]:
	document = {
		"schema_version": 1,
		"kind": WORKTREE_RECEIPT_KIND,
		"role": role,
		"source_root": str(source_root.resolve()),
		"git_commit": commit,
		"git_tree_sha": "f" * 40,
		"clean": True,
		"git_status_porcelain_sha256": EMPTY_SHA256,
	}
	_test_write_json(path, document)
	return _test_file_reference(path)


def _test_client_identity(root: Path, role: str, commit: str, executable_bytes: bytes) -> tuple[Mapping[str, Any], Mapping[str, Path]]:
	client_root = root / role
	source_root = client_root / "source"
	build = client_root / "build" / "mumble.exe"
	stage = client_root / "stage" / "mumble.exe"
	qualified = client_root / "qualified" / "mumble.exe"
	for path in (source_root, build.parent, stage.parent, qualified.parent):
		path.mkdir(parents=True, exist_ok=True)
	for path in (build, stage, qualified):
		path.write_bytes(executable_bytes)
	(stage.parent / "runtime.dat").write_bytes(b"runtime-" + role.encode("ascii"))
	receipt_path = client_root / "clean-worktree.json"
	identity = {
		"commit": commit,
		"worktree_receipt": _test_worktree_receipt(receipt_path, role, commit, source_root),
		"build_executable": _test_file_reference(build),
		"stage_executable": _test_file_reference(stage),
		"qualified_executable": _test_file_reference(qualified),
		"stage_payload": _test_tree_reference(stage.parent),
	}
	if role == "legacy":
		identity["instrumentation_base_commit"] = LEGACY_INSTRUMENTATION_BASE_COMMIT
	return identity, {
		f"{role}_source": source_root, f"{role}_build": build, f"{role}_stage": stage,
		f"{role}_qualified": qualified, f"{role}_receipt": receipt_path,
	}


def _test_fixed_timeline(clean_path: Path, capture_path: Path, sample_count: int) -> Mapping[str, Any]:
	capture_hash = file_sha256(capture_path)
	return {
		"schema_version": 3,
		"scorer": "mumble-fixed-timeline-v3",
		"timeline_alignment": "fixed",
		"sample_rate_hz": SAMPLE_RATE_HZ,
		"frame_samples": FRAME_SAMPLES,
		"declared_latency_samples": 0,
		"reference_sha256": file_sha256(clean_path),
		"received_sha256": capture_hash,
		"reference_samples": sample_count,
		"received_samples": sample_count,
		"compared_samples": sample_count,
		"missing_tail_samples": 0,
		"loudness_match_gain": 1.0,
		"fixed_timeline_sdr_db": 90.0,
		"reference_onset_samples": 0,
		"reference_end_samples": sample_count,
		"expected_onset_samples": 0,
		"expected_end_samples": sample_count,
		"received_onset_samples": 0,
		"received_end_samples": sample_count,
		"onset_loss_samples": 0,
		"end_loss_samples": 0,
		"reference_clipped_samples": 0,
		"received_clipped_samples": 0,
		"transport_baseline": None,
		"qualification_limits": {
			"max_onset_loss_samples": FRAME_SAMPLES,
			"max_end_loss_samples": FRAME_SAMPLES,
			"require_complete_tail": True,
			"fail_on_new_clipping": True,
		},
		"passed": True,
	}


def _build_self_test_campaign(root: Path) -> _SelfTestCampaign:
	root.mkdir(parents=True, exist_ok=False)
	try:
		candidate_commit = subprocess.check_output(
			["git", "-C", str(Path(__file__).resolve().parents[2]), "rev-parse", "HEAD"],
			text=True, encoding="utf-8",
		).strip()
	except (OSError, subprocess.CalledProcessError) as error:
		raise AssemblyError(f"self-test: unable to resolve candidate commit: {error}") from error
	legacy_identity, legacy_paths = _test_client_identity(root, "legacy", LEGACY_COMMIT, b"legacy-client-v1")
	candidate_identity, candidate_paths = _test_client_identity(root, "candidate", candidate_commit, b"candidate-client-v1")

	server_source = root / "server" / "source"
	server_build = root / "server" / "build"
	server_source.mkdir(parents=True)
	server_build.mkdir(parents=True)
	server_exe = server_build / "mumble-server.exe"
	server_exe.write_bytes(b"og-server-v1")
	server_receipt = root / "server" / "clean-worktree.json"
	server_identity = {
		"commit": SERVER_COMMIT,
		"worktree_receipt": _test_worktree_receipt(server_receipt, "server", SERVER_COMMIT, server_source),
		"executable": _test_file_reference(server_exe),
	}

	tools_root = root / "tools"
	tools_root.mkdir()
	wrapper = tools_root / "invoke-speech-cleanup-e2e.ps1"
	scorer = tools_root / "score-fixed-timeline.py"
	wrapper.write_bytes(b"pinned-wrapper-v1")
	scorer.write_bytes(Path(__file__).with_name("score-fixed-timeline.py").read_bytes())

	corpus_root = root / "corpus"
	corpus_root.mkdir()
	corpus_lock_path = corpus_root / "corpus-lock.json"
	corpus_lock = {"schema_version": 2, "kind": "self-test-lock", "sources": [{"id": "synthetic", "license": "CC0-1.0"}]}
	_test_write_json(corpus_lock_path, corpus_lock)
	render_root = corpus_root / "rendered" / PARITY_CASE_ID
	render_root.mkdir(parents=True)
	input_wav = render_root / "client1-input.wav"
	clean_wav = render_root / "clean-reference.wav"
	active = [round(8_000 * math.sin(index / 19.0)) for index in range(960)]
	clean_active = [round(7_000 * math.sin(index / 19.0)) for index in range(960)]
	_test_write_pcm16(input_wav, [*active, *([0] * 960)])
	_test_write_pcm16(clean_wav, [*clean_active, *([0] * 960)])
	inventory_path = corpus_root / "inventory-v3.json"
	component_specs = (
		("self-test-speech", "speech", "audio/speech/self-test.wav", "1" * 64, 3844, "5" * 64, "self-test-speech-source", ALIGNMENT_SAMPLES),
		("self-test-noise", "noise", "audio/noise/self-test.wav", "2" * 64, 3844, "6" * 64, "self-test-noise-source", ALIGNMENT_SAMPLES),
		("self-test-rir", "rir", "audio/rir/self-test.wav", "3" * 64, 1964, "7" * 64, "self-test-rir-source", 960),
		("self-test-microphone", "microphone_response", "audio/microphone-response/self-test.wav", "4" * 64, 1068, "8" * 64, "self-test-microphone-source", 512),
	)
	inventory_items = [
		{
			"id": item_id, "kind": kind, "relative_path": relative_path, "sha256": sha256,
			"size_bytes": size_bytes, "source_artifact_sha256": source_sha256,
			"source_id": source_id, "duration_samples": duration_samples,
			"sample_rate_hz": SAMPLE_RATE_HZ, "channels": 1,
		}
		for item_id, kind, relative_path, sha256, size_bytes, source_sha256, source_id, duration_samples
		in component_specs
	]
	inventory = {
		"schema_version": 3,
		"inventory_id": "assembler-self-test-v3",
		"eligibility": "release",
		"corpus_lock_sha256": canonical_json_sha256(corpus_lock),
		"provenance": {
			"generator": "assembler-self-test", "generator_version": "1",
			"generated_from_state_sha256": "1" * 64, "transformation_manifest_sha256": "2" * 64,
		},
		"items": inventory_items,
	}
	_test_write_json(inventory_path, inventory)
	component_by_id = {item["id"]: item for item in inventory_items}

	def plan_component(item_id: str) -> MutableMapping[str, Any]:
		item = component_by_id[item_id]
		return {
			"item_id": item_id, "relative_path": item["relative_path"], "sha256": item["sha256"],
			"size_bytes": item["size_bytes"], "source_artifact_sha256": item["source_artifact_sha256"],
			"source_id": item["source_id"], "input_channels": item["channels"],
			"input_sample_rate_hz": item["sample_rate_hz"],
		}

	speech_component = plan_component("self-test-speech")
	speech_component.update({"language": "sv-SE", "window": {"start_sample": 0, "length_samples": ALIGNMENT_SAMPLES}})
	noise_component = plan_component("self-test-noise")
	noise_component.update({"class": "competing-speech", "window": {"start_sample": 0, "length_samples": ALIGNMENT_SAMPLES}})
	selected_case = {
		"case_id": PARITY_CASE_ID, "profile": "Original", "startup": {"preroll_ms": 0},
		"speech": speech_component, "noise": noise_component,
		"mix": {
			"snr_db": 5, "rir": plan_component("self-test-rir"),
			"microphone_response": plan_component("self-test-microphone"),
		},
	}
	plan_cases: list[Mapping[str, Any]] = [
		{"case_id": f"master_quality-validation-{index:05d}"} for index in range(1, 501)
	]
	plan_cases[80] = selected_case
	mixture_plan = {
		"schema_version": 4, "generator": "mumble-audio-mixture-plan-v4",
		"suite": "master_quality", "split": "validation", "timeline_alignment": "fixed",
		"corpus_lock_sha256": canonical_json_sha256(corpus_lock),
		"corpus_inventory_sha256": canonical_json_sha256(inventory),
		"format": {"sample_rate_hz": SAMPLE_RATE_HZ, "channels": 1, "frame_samples": FRAME_SAMPLES, "duration_ms": 6000},
		"cases": plan_cases,
	}
	mixture_plan_path = corpus_root / "master-quality-validation-500.json"
	_test_write_json(mixture_plan_path, mixture_plan)
	render_entry = {
		"case_id": PARITY_CASE_ID, "profile": "Original", "startup_preroll_ms": 0,
		"input": {"path": input_wav.relative_to(corpus_root).as_posix(), "sha256": file_sha256(input_wav)},
		"clean_reference": {"path": clean_wav.relative_to(corpus_root).as_posix(), "sha256": file_sha256(clean_wav)},
		"speech_source_sha256": component_by_id["self-test-speech"]["sha256"],
		"noise_source_sha256": component_by_id["self-test-noise"]["sha256"],
		"rir_source_sha256": component_by_id["self-test-rir"]["sha256"],
		"microphone_response_source_sha256": component_by_id["self-test-microphone"]["sha256"],
		"rendered_samples": ALIGNMENT_SAMPLES,
	}
	render_manifest = {
		"schema_version": 2, "renderer": "mumble-audio-mixture-renderer-v2",
		"plan_sha256": canonical_json_sha256(mixture_plan),
		"corpus_lock_sha256": canonical_json_sha256(corpus_lock),
		"corpus_inventory_sha256": canonical_json_sha256(inventory),
		"sample_rate_hz": SAMPLE_RATE_HZ, "channels": 1, "private_audio_do_not_upload": True,
		"cases": [render_entry],
	}
	render_manifest_path = corpus_root / "render-manifest.json"
	_test_write_json(render_manifest_path, render_manifest)
	fixture_path = corpus_root / "fixture-attestation.json"
	fixture = {
		"schema_version": 2, "kind": FIXTURE_KIND, "case_id": PARITY_CASE_ID,
		"alignment_samples": ALIGNMENT_SAMPLES, "rendered_samples": ALIGNMENT_SAMPLES,
		"corpus_inventory_sha256": canonical_json_sha256(inventory),
		"mixture_plan_sha256": canonical_json_sha256(mixture_plan),
		"render_manifest_sha256": file_sha256(render_manifest_path),
		"render_entry_sha256": canonical_json_sha256(render_entry),
		"input_sha256": file_sha256(input_wav), "clean_reference_sha256": file_sha256(clean_wav),
	}
	_test_write_json(fixture_path, fixture)

	transport = {
		"transport_path": "client1-opus-server-client2",
		"server_host": "127.0.0.1",
		"voice_transport": "tcp_tunnel",
		"receiver_cleanup_enabled": False,
		"sender_cleanup_mode": "Off",
		"sender_auto_adapt": False,
		"pre_roll_frames": 0,
		"tail_frames": 0,
		"drain_milliseconds": 1500,
	}
	bindings: MutableMapping[str, Any] = {
		"schema_version": 1,
		"kind": BINDING_KIND,
		"campaign_id": "assembler-self-test",
		"candidate_commit": candidate_commit,
		"identities": {
			"legacy": legacy_identity,
			"candidate": candidate_identity,
			"server": server_identity,
			"tools": {"wrapper": _test_file_reference(wrapper), "scorer": _test_file_reference(scorer)},
		},
		"transport": transport,
		"corpus": {
			"corpus_lock": _test_file_reference(corpus_lock_path),
			"corpus_lock_canonical_sha256": canonical_json_sha256(corpus_lock),
			"corpus_inventory": _test_file_reference(inventory_path),
			"corpus_inventory_canonical_sha256": canonical_json_sha256(inventory),
			"mixture_plan": _test_file_reference(mixture_plan_path),
			"mixture_plan_canonical_sha256": canonical_json_sha256(mixture_plan),
			"render_manifest": _test_file_reference(render_manifest_path),
			"render_entry_sha256": canonical_json_sha256(render_entry),
			"selected_case_id": PARITY_CASE_ID,
			"fixture_attestation": _test_file_reference(fixture_path),
			"input_wav": _test_file_reference(input_wav),
			"clean_reference_wav": _test_file_reference(clean_wav),
		},
		"cases": [],
	}
	manifests: dict[tuple[int, str], Path] = {}
	input_pcm_sha = hashlib.sha256(b"self-test-float-input-pcm").hexdigest()
	pre_opus_wave = _read_wave(input_wav, "self-test pre-Opus source")
	pre_opus_pcm_sha = hashlib.sha256(pre_opus_wave.pcm).hexdigest()
	mode_names = {"continuous": "Continuous", "push_to_talk": "PTT", "vad": "VAD"}

	for index, case in enumerate(REQUIRED_MATRIX):
		case_binding: MutableMapping[str, Any] = dict(case)
		opus_sha = hashlib.sha256(canonical_json_bytes({"case": case, "opus": "same-pair"})).hexdigest()
		packet_count = max(1, ALIGNMENT_SAMPLES // (FRAME_SAMPLES * int(case["frames_per_packet"])))
		for implementation, identity, identity_paths in (
			("legacy", legacy_identity, legacy_paths), ("original", candidate_identity, candidate_paths),
		):
			profile = "Legacy" if implementation == "legacy" else "Original"
			run_root = root / "runs" / f"{index:02d}-{implementation}"
			artifacts = run_root / "artifacts"
			app = run_root / "app"
			artifacts.mkdir(parents=True)
			app.mkdir()
			client_bytes = (identity_paths[f"{'legacy' if implementation == 'legacy' else 'candidate'}_stage"]).read_bytes()
			(app / "mumble.exe").write_bytes(client_bytes)
			(app / "runtime.dat").write_bytes((Path(identity["stage_payload"]["path"]) / "runtime.dat").read_bytes())
			pre_opus_path = artifacts / "sender-pre-opus.wav"
			_test_write_pcm16(pre_opus_path, [*active, *([0] * 960)])
			capture_path = artifacts / "receiver-capture.wav"
			capture_samples = [0.2 * math.sin(sample / 17.0) if sample < 960 else 0.0 for sample in range(ALIGNMENT_SAMPLES)]
			_test_write_float32(capture_path, capture_samples)
			voice = {
				"schema_version": 1,
				"implementation": implementation,
				"bitrate_bps": case["bitrate_bps"],
				"frames_per_packet": case["frames_per_packet"],
				"transmit_mode": case["transmit_mode"],
				"ptt_hold_activated": case["transmit_mode"] == "push_to_talk",
				"enhancement_profile": profile,
				"input_pcm_encoding": "ieee754-f32le",
				"input_pcm_sha256": input_pcm_sha,
				"pre_opus_pcm_encoding": "signed-s16le",
				"pre_opus_pcm_sha256": pre_opus_pcm_sha,
				"opus_packet_hash_framing": "u32le-length+payload",
				"opus_packets_sha256": opus_sha,
				"packet_count": packet_count,
				"terminator_count": 1,
				"active_model_sha256": "",
				"model_initialization_attempts": 0,
				"algorithmic_latency_samples": 0,
				"fallback_count": 0,
				"deadline_miss_count": 0,
			}
			pre_opus_record = {
				"path": str(pre_opus_path.resolve()), "sha256": file_sha256(pre_opus_path),
				"bytes": pre_opus_path.stat().st_size, "encoding": "signed-s16le",
				"sample_frames": ALIGNMENT_SAMPLES, "callbacks": ALIGNMENT_SAMPLES // FRAME_SAMPLES,
				"timeline_origin": "source-after-transmitted-preroll",
			}
			input_result = {
				"ok": True, "role": "input", "error": "", "sample_rate": str(SAMPLE_RATE_HZ),
				"pre_roll_frames": "0", "tail_frames": "0", "terminator_submitted": True,
				"used_fallback": False, "effective_cleanup_mode": "Off", "reported_latency_samples": "0",
				"drained_cleanup_samples": "0", "source_frames": str(ALIGNMENT_SAMPLES),
				"submitted_frames": str(ALIGNMENT_SAMPLES), "voice_contract": voice,
				"pre_opus_capture": {
					"enabled": True, "path": str(pre_opus_path.resolve()), "encoding": "signed-s16le",
					"sample_frames": str(ALIGNMENT_SAMPLES), "callbacks": str(ALIGNMENT_SAMPLES // FRAME_SAMPLES),
					"timeline_origin": "source-after-transmitted-preroll",
				},
			}
			remote_cleanup = {
				"diagnostics_captured": True, "drain_completed": True, "forced_off": True,
				"active": False, "processor_ready": False, "requested_enabled": False,
				"used_fallback": False, "was_applied": False, "reported_latency_samples": "0",
				"drained_samples": "0",
			}
			capture_result = {
				"ok": True, "role": "capture", "error": "", "sample_rate": str(SAMPLE_RATE_HZ),
				"channels": "1", "captured_frames": str(ALIGNMENT_SAMPLES), "discarded_pre_roll_frames": "0",
				"stop_gate_observed": True, "capture_wav": str(capture_path.resolve()),
				"remote_cleanup": remote_cleanup,
			}
			input_done = artifacts / "input.done.json"
			capture_done = artifacts / "capture.done.json"
			_test_write_json(input_done, input_result)
			_test_write_json(capture_done, capture_result)
			fixed_path = artifacts / "fixed-timeline-score.json"
			fixed = _test_fixed_timeline(input_wav, pre_opus_path, ALIGNMENT_SAMPLES)
			_test_write_json(fixed_path, fixed)
			voice_projection = {
				"schema_version": 1, "implementation": implementation, "bitrate_bps": case["bitrate_bps"],
				"frames_per_packet": case["frames_per_packet"], "transmit_mode": case["transmit_mode"],
				"input_pcm_sha256": input_pcm_sha, "pre_opus_pcm_sha256": pre_opus_pcm_sha,
				"opus_packets_sha256": opus_sha, "received_pcm_sha256": file_sha256(capture_path),
				"packet_count": packet_count, "terminator_count": 1, "enhancement_profile": profile,
				"model_initialization_attempts": 0, "algorithmic_latency_samples": 0,
				"fallback_count": 0, "deadline_miss_count": 0,
			}
			role_prefix = "legacy" if implementation == "legacy" else "candidate"
			manifest = {
				"schema_version": 1, "artifact_kind": "speech_cleanup_e2e", "status": "passed", "phase": "complete",
				"preflight_only": False, "skip_build": True, "attested_stage_only": True,
				"qualification_provenance": {
					"wrapper": _test_file_reference(wrapper), "fixed_timeline_scorer": _test_file_reference(scorer),
				},
				"repo_root": str(identity_paths[f"{role_prefix}_source"].resolve()), "run_root": str(run_root.resolve()),
				"cleanup": {
					"sender": {"mode": "Off", "input_enhancement_profile": profile, "auto_adapt": False},
					"receiver": {"enabled": False},
				},
				"transport": {
					"host": "127.0.0.1", "voice_transport": "tcp_tunnel", "bitrate_bps": case["bitrate_bps"],
					"frames_per_packet": case["frames_per_packet"], "transmit_mode": mode_names[case["transmit_mode"]],
					"pre_roll_frames": 0, "tail_frames": 0, "drain_milliseconds": 1500,
				},
				"build": {
					"git_head": identity["commit"], "git_dirty": False,
					"source_root": str(identity_paths[f"{role_prefix}_source"].resolve()),
					"client_executable_sha256": identity["build_executable"]["sha256"],
					"client_exe": identity["build_executable"]["path"],
					"client_build_dir": str(Path(identity["build_executable"]["path"]).parent),
					"client_stage_dir": identity["stage_payload"]["path"],
					"server_git_head": SERVER_COMMIT, "server_git_dirty": False,
					"server_source_root": str(server_source.resolve()),
					"server_executable_sha256": server_identity["executable"]["sha256"],
					"server_exe": server_identity["executable"]["path"],
					"server_build_dir": str(server_build.resolve()),
				},
				"input": {
					"path": str(input_wav.resolve()), "sha256": file_sha256(input_wav),
					"clean_reference_path": str(clean_wav.resolve()), "clean_reference_sha256": file_sha256(clean_wav),
					"unbaselined_voice_contract_control": True,
				},
				"completion": {
					"input": input_result, "capture": capture_result, "sender_pre_opus_file": pre_opus_record,
					"capture_file": {"path": str(capture_path.resolve()), "sha256": file_sha256(capture_path), "bytes": capture_path.stat().st_size},
				},
				"voice_contract_evidence": voice_projection,
				"quality": {
					"fixed_timeline_binding": {
						"mode": "unbaselined-voice-contract-control",
						"reference_artifact": "input_wav",
						"scored_artifact": "sender_pre_opus_wav",
						"timeline_origin": "source-after-transmitted-preroll",
					},
					"fixed_timeline": fixed,
				},
				"artifacts": {
					"input_done": str(input_done.resolve()), "capture_done": str(capture_done.resolve()),
					"sender_pre_opus_wav": str(pre_opus_path.resolve()), "capture_wav": str(capture_path.resolve()),
					"fixed_timeline_score": str(fixed_path.resolve()),
				},
			}
			manifest_path = run_root / "manifest.json"
			_test_write_json(manifest_path, manifest)
			manifests[(index, implementation)] = manifest_path
			case_binding[f"{'legacy' if implementation == 'legacy' else 'original'}_manifest"] = _test_file_reference(manifest_path)
		bindings["cases"].append(case_binding)

	bindings_path = root / "campaign-bindings.json"
	_test_write_json(bindings_path, bindings)
	paths = {
		**legacy_paths, **candidate_paths, "server_exe": server_exe, "server_receipt": server_receipt,
		"wrapper": wrapper, "scorer": scorer,
	}
	return _SelfTestCampaign(root, bindings_path, bindings, candidate_commit, manifests, paths)


def _test_update_identity_reference(campaign: _SelfTestCampaign, role: str, field: str, path: Path) -> None:
	campaign.bindings["identities"][role][field] = _test_file_reference(path)
	campaign.rewrite_bindings()


def _test_mutate_score(campaign: _SelfTestCampaign, index: int, implementation: str, mutate: Any) -> None:
	manifest_path = campaign.manifests[(index, implementation)]
	manifest = copy.deepcopy(_load_json_bytes(manifest_path.read_bytes(), "self-test score manifest"))
	fixed_path = Path(manifest["artifacts"]["fixed_timeline_score"])
	fixed = copy.deepcopy(_load_json_bytes(fixed_path.read_bytes(), "self-test fixed score"))
	mutate(fixed)
	_test_write_json(fixed_path, fixed)
	manifest["quality"]["fixed_timeline"] = fixed
	_test_write_json(manifest_path, manifest)
	field = "legacy_manifest" if implementation == "legacy" else "original_manifest"
	campaign.bindings["cases"][index][field] = _test_file_reference(manifest_path)
	campaign.rewrite_bindings()


def _expect_self_test_failure(root: Path, name: str, mutate: Any) -> None:
	campaign = _build_self_test_campaign(root / name)
	mutate(campaign)
	try:
		assemble(
			campaign.bindings_path, campaign.bindings_sha256(), campaign.candidate_commit,
			LEGACY_BUILD_COMMIT, LEGACY_INSTRUMENTATION_BASE_COMMIT, LEGACY_EXECUTABLE_SHA256,
			verify_live_git=False,
		)
	except (AssemblyError, PayloadIdentityError):
		return
	raise AssertionError(f"assembler accepted invalid self-test campaign: {name}")


def run_self_test() -> None:
	with tempfile.TemporaryDirectory(prefix="mumble-original-assembler-") as directory:
		root = Path(directory)
		legacy_revision = _verify_legacy_revision_contract()
		if (
			legacy_revision["instrumentation_base_commit"] != LEGACY_INSTRUMENTATION_BASE_COMMIT
			or legacy_revision["build_commit"] != LEGACY_BUILD_COMMIT
			or legacy_revision["ui_build_commit"] != LEGACY_UI_BUILD_COMMIT
			or legacy_revision["ui_only_changed_paths"] != ["src/mumble/ModernSettingsController.cpp"]
			or legacy_revision["instrumentation_changed_paths"] != [
				"src/mumble/SpeechCleanupTestAudio.cpp", "src/mumble/SpeechCleanupTestAudio.h",
			]
			or legacy_revision["protected_voice_path_changes"]
			or len(legacy_revision["protected_source_regions"]) != 4
		):
			raise AssertionError("frozen buildable legacy revision contract is incomplete")
		try:
			_verify_legacy_revision_contract(LEGACY_BUILD_COMMIT, LEGACY_INSTRUMENTATION_BASE_COMMIT)
		except AssemblyError:
			pass
		else:
			raise AssertionError("legacy revision contract accepted a reversed base/build relationship")
		try:
			_verify_legacy_revision_contract(
				"512cb0ce1e664984abf3b8eddd616909b7f86880", SERVER_COMMIT,
			)
		except AssemblyError:
			pass
		else:
			raise AssertionError("legacy revision contract accepted a protected receive-path change")
		live_git_root = root / "live-git"
		live_git_root.mkdir()
		for arguments in (
			("init", "--quiet"), ("config", "user.email", "self-test@mumble.invalid"),
			("config", "user.name", "Mumble Self Test"),
		):
			subprocess.run(["git", "-C", str(live_git_root), *arguments], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
		(live_git_root / "tracked.txt").write_text("clean\n", encoding="utf-8")
		subprocess.run(["git", "-C", str(live_git_root), "add", "tracked.txt"], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
		subprocess.run(["git", "-C", str(live_git_root), "commit", "--quiet", "-m", "self-test"], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
		live_commit = subprocess.check_output(["git", "-C", str(live_git_root), "rev-parse", "HEAD"], text=True).strip()
		live_tree = subprocess.check_output(["git", "-C", str(live_git_root), "rev-parse", "HEAD^{tree}"], text=True).strip()
		_verify_live_worktree(live_git_root, live_commit, live_tree, "self-test live worktree")
		(live_git_root / "tracked.txt").write_text("dirty\n", encoding="utf-8")
		try:
			_verify_live_worktree(live_git_root, live_commit, live_tree, "self-test dirty worktree")
		except AssemblyError:
			pass
		else:
			raise AssertionError("live worktree verifier accepted dirty source")

		campaign = _build_self_test_campaign(root / "valid")
		try:
			assemble(
				campaign.bindings_path, campaign.bindings_sha256(), campaign.candidate_commit,
				LEGACY_BUILD_COMMIT, LEGACY_INSTRUMENTATION_BASE_COMMIT, "0" * 64,
				verify_live_git=False,
			)
		except AssemblyError:
			pass
		else:
			raise AssertionError("assembler accepted the wrong frozen legacy executable identity")
		qualification, provenance = assemble(
			campaign.bindings_path, campaign.bindings_sha256(), campaign.candidate_commit,
			LEGACY_BUILD_COMMIT, LEGACY_INSTRUMENTATION_BASE_COMMIT, LEGACY_EXECUTABLE_SHA256,
			verify_live_git=False,
		)
		second_qualification, second_provenance = assemble(
			campaign.bindings_path, campaign.bindings_sha256(), campaign.candidate_commit,
			LEGACY_BUILD_COMMIT, LEGACY_INSTRUMENTATION_BASE_COMMIT, LEGACY_EXECUTABLE_SHA256,
			verify_live_git=False,
		)
		if qualification != second_qualification or provenance != second_provenance:
			raise AssertionError("assembler output is not deterministic")
		if len(qualification["cases"]) != 45 or CHECKER.validate_qualification(qualification) != 45:
			raise AssertionError("positive campaign did not produce the exact 45-case qualification")
		if set(qualification) != QUALIFICATION_ROOT_KEYS or any(set(case) != QUALIFICATION_CASE_KEYS for case in qualification["cases"]):
			raise AssertionError("assembler emitted fields outside the release-gate schema")
		output = root / "published" / "original-voice-qualification.json"
		receipt = root / "published" / "original-voice-provenance.json"
		_write_outputs(qualification, provenance, output, receipt)
		published = _load_json_bytes(output.read_bytes(), "published self-test qualification")
		published_receipt = _load_json_bytes(receipt.read_bytes(), "published self-test provenance")
		if CHECKER.validate_qualification(published) != 45:
			raise AssertionError("published qualification was rejected")
		if published_receipt["qualification"]["sha256"] != file_sha256(output):
			raise AssertionError("published provenance did not bind the qualification bytes")
		try:
			_write_outputs(qualification, provenance, output, receipt)
		except AssemblyError:
			pass
		else:
			raise AssertionError("publisher overwrote existing evidence")

		# Separate localhost runs may legitimately retain one extra jitter-buffer
		# frame. The receiver route remains independently/paired qualified, while
		# the qualification records the exact signed capture-length delta instead
		# of demanding byte-identical receiver WAVs.
		jitter_manifest_path = campaign.manifests[(0, "original")]
		jitter_manifest = copy.deepcopy(_load_json_bytes(jitter_manifest_path.read_bytes(), "self-test jitter manifest"))
		jitter_capture = Path(jitter_manifest["artifacts"]["capture_wav"])
		jitter_samples = [
			0.2 * math.sin(sample / 17.0) if sample < 960 else 0.0
			for sample in range(ALIGNMENT_SAMPLES + FRAME_SAMPLES)
		]
		_test_write_float32(jitter_capture, jitter_samples)
		jitter_hash = file_sha256(jitter_capture)
		jitter_manifest["completion"]["capture"]["captured_frames"] = str(ALIGNMENT_SAMPLES + FRAME_SAMPLES)
		jitter_manifest["completion"]["capture_file"].update({
			"sha256": jitter_hash, "bytes": jitter_capture.stat().st_size,
		})
		jitter_manifest["voice_contract_evidence"]["received_pcm_sha256"] = jitter_hash
		_test_write_json(Path(jitter_manifest["artifacts"]["capture_done"]), jitter_manifest["completion"]["capture"])
		_test_write_json(jitter_manifest_path, jitter_manifest)
		campaign.bindings["cases"][0]["original_manifest"] = _test_file_reference(jitter_manifest_path)
		campaign.rewrite_bindings()
		jitter_qualification, _ = assemble(
			campaign.bindings_path, campaign.bindings_sha256(), campaign.candidate_commit,
			LEGACY_BUILD_COMMIT, LEGACY_INSTRUMENTATION_BASE_COMMIT, LEGACY_EXECUTABLE_SHA256,
			verify_live_git=False,
		)
		if (
			jitter_qualification["cases"][0]["receiver_jitter_delta_samples"] != FRAME_SAMPLES
			or jitter_qualification["cases"][0]["legacy_received_pcm_sha256"]
			== jitter_qualification["cases"][0]["original_received_pcm_sha256"]
		):
			raise AssertionError("one-frame receiver jitter was not preserved as bounded, distinct evidence")

		strict_wave = root / "strict-wave.wav"
		_test_write_pcm16(strict_wave, [0, 1, -1, 2])
		bad_block_align = bytearray(strict_wave.read_bytes())
		struct.pack_into("<H", bad_block_align, 32, 4)
		(root / "bad-block-align.wav").write_bytes(bad_block_align)
		try:
			_read_wave(root / "bad-block-align.wav", "self-test bad block alignment")
		except AssemblyError:
			pass
		else:
			raise AssertionError("WAV parser accepted inconsistent block alignment")
		bad_fmt_extension = bytearray(strict_wave.read_bytes())
		struct.pack_into("<I", bad_fmt_extension, 16, 17)
		bad_fmt_extension[4:8] = struct.pack("<I", len(bad_fmt_extension) - 8)
		(root / "bad-fmt-extension.wav").write_bytes(bad_fmt_extension)
		try:
			_read_wave(root / "bad-fmt-extension.wav", "self-test bad fmt extension")
		except AssemblyError:
			pass
		else:
			raise AssertionError("WAV parser accepted a malformed fmt extension")

		_expect_self_test_failure(root, "tampered-build", lambda value: value.paths["candidate_build"].write_bytes(b"tampered"))
		_expect_self_test_failure(root, "tampered-stage", lambda value: value.paths["candidate_stage"].write_bytes(b"tampered"))
		_expect_self_test_failure(root, "tampered-runtime", lambda value: (value.manifests[(0, "original")].parent / "app" / "mumble.exe").write_bytes(b"stale-stage"))
		_expect_self_test_failure(root, "tampered-runtime-file", lambda value: (value.manifests[(0, "original")].parent / "app" / "runtime.dat").write_bytes(b"stale-runtime"))
		_expect_self_test_failure(root, "missing-runtime-file", lambda value: (value.manifests[(0, "original")].parent / "app" / "runtime.dat").unlink())
		_expect_self_test_failure(root, "extra-runtime-file", lambda value: (value.manifests[(0, "original")].parent / "app" / "unattested.dll").write_bytes(b"extra"))

		def hardlinked_launched_runtime(value: _SelfTestCampaign) -> None:
			launched = value.manifests[(0, "original")].parent / "app" / "runtime.dat"
			launched.unlink()
			os.link(value.paths["candidate_stage"].parent / "runtime.dat", launched)
		_expect_self_test_failure(root, "hardlinked-launched-runtime", hardlinked_launched_runtime)
		_expect_self_test_failure(root, "tampered-server", lambda value: value.paths["server_exe"].write_bytes(b"tampered"))
		_expect_self_test_failure(root, "tampered-wrapper", lambda value: value.paths["wrapper"].write_bytes(b"tampered"))
		_expect_self_test_failure(root, "tampered-scorer", lambda value: value.paths["scorer"].write_bytes(b"tampered"))
		_expect_self_test_failure(root, "wrong-run-tool", lambda value: value.update_manifest(0, "original", lambda doc: doc["qualification_provenance"].__setitem__("wrapper", copy.deepcopy(value.bindings["identities"]["tools"]["scorer"]))))

		def hardlinked_snapshot(value: _SelfTestCampaign) -> None:
			qualified = value.paths["candidate_qualified"]
			qualified.unlink()
			os.link(value.paths["candidate_build"], qualified)
			_test_update_identity_reference(value, "candidate", "qualified_executable", qualified)
		_expect_self_test_failure(root, "hardlinked-snapshot", hardlinked_snapshot)

		def hardlinked_score_artifact(value: _SelfTestCampaign) -> None:
			manifest = _load_json_bytes(value.manifests[(0, "original")].read_bytes(), "self-test manifest")
			score = Path(manifest["artifacts"]["fixed_timeline_score"])
			alias_source = score.with_name("fixed-timeline-score-source.json")
			alias_source.write_bytes(score.read_bytes())
			score.unlink()
			os.link(alias_source, score)
		_expect_self_test_failure(root, "hardlinked-score-artifact", hardlinked_score_artifact)

		def dirty_receipt(value: _SelfTestCampaign) -> None:
			path = value.paths["candidate_receipt"]
			document = copy.deepcopy(_load_json_bytes(path.read_bytes(), "self-test dirty receipt"))
			document["clean"] = False
			_test_write_json(path, document)
			_test_update_identity_reference(value, "candidate", "worktree_receipt", path)
		_expect_self_test_failure(root, "dirty-receipt", dirty_receipt)

		def wrong_matrix(value: _SelfTestCampaign) -> None:
			value.bindings["cases"][0]["bitrate_bps"] = 9_999
			value.rewrite_bindings()
		_expect_self_test_failure(root, "wrong-matrix", wrong_matrix)
		_expect_self_test_failure(root, "wrong-profile", lambda value: value.update_manifest(0, "original", lambda doc: doc["cleanup"]["sender"].__setitem__("input_enhancement_profile", "Quality")))
		_expect_self_test_failure(root, "wrong-encoding", lambda value: value.update_manifest(0, "original", lambda doc: doc["completion"]["input"]["voice_contract"].__setitem__("input_pcm_encoding", "signed-s16le")))

		def mismatch_input(value: _SelfTestCampaign) -> None:
			def mutate(document: MutableMapping[str, Any]) -> None:
				document["completion"]["input"]["voice_contract"]["input_pcm_sha256"] = "d" * 64
				document["voice_contract_evidence"]["input_pcm_sha256"] = "d" * 64
			value.update_manifest(0, "original", mutate)
		_expect_self_test_failure(root, "mismatched-input", mismatch_input)

		def mismatch_opus(value: _SelfTestCampaign) -> None:
			def mutate(document: MutableMapping[str, Any]) -> None:
				document["completion"]["input"]["voice_contract"]["opus_packets_sha256"] = "e" * 64
				document["voice_contract_evidence"]["opus_packets_sha256"] = "e" * 64
			value.update_manifest(0, "original", mutate)
		_expect_self_test_failure(root, "mismatched-opus", mismatch_opus)

		def broken_receiver_route(value: _SelfTestCampaign) -> None:
			manifest_path = value.manifests[(0, "original")]
			manifest = copy.deepcopy(_load_json_bytes(manifest_path.read_bytes(), "self-test receiver manifest"))
			capture_path = Path(manifest["artifacts"]["capture_wav"])
			capture_samples = [0.0] * ALIGNMENT_SAMPLES
			_test_write_float32(capture_path, capture_samples)
			capture_hash = file_sha256(capture_path)
			manifest["completion"]["capture_file"].update({"sha256": capture_hash, "bytes": capture_path.stat().st_size})
			manifest["voice_contract_evidence"]["received_pcm_sha256"] = capture_hash
			_test_write_json(manifest_path, manifest)
			value.bindings["cases"][0]["original_manifest"] = _test_file_reference(manifest_path)
			value.rewrite_bindings()
		_expect_self_test_failure(root, "broken-receiver-route", broken_receiver_route)

		def identically_truncated_receivers(value: _SelfTestCampaign) -> None:
			for implementation in ("legacy", "original"):
				manifest_path = value.manifests[(0, implementation)]
				manifest = copy.deepcopy(_load_json_bytes(manifest_path.read_bytes(), "self-test truncated receiver manifest"))
				capture_path = Path(manifest["artifacts"]["capture_wav"])
				truncated_samples = [0.2 * math.sin(sample / 17.0) for sample in range(FRAME_SAMPLES)]
				_test_write_float32(capture_path, truncated_samples)
				capture_hash = file_sha256(capture_path)
				manifest["completion"]["capture"]["captured_frames"] = str(FRAME_SAMPLES)
				manifest["completion"]["capture_file"].update({
					"sha256": capture_hash, "bytes": capture_path.stat().st_size,
				})
				manifest["voice_contract_evidence"]["received_pcm_sha256"] = capture_hash
				capture_done = Path(manifest["artifacts"]["capture_done"])
				_test_write_json(capture_done, manifest["completion"]["capture"])
				_test_write_json(manifest_path, manifest)
				field = "legacy_manifest" if implementation == "legacy" else "original_manifest"
				value.bindings["cases"][0][field] = _test_file_reference(manifest_path)
			value.rewrite_bindings()
		_expect_self_test_failure(root, "identically-truncated-receivers", identically_truncated_receivers)

		def duplicate_case(value: _SelfTestCampaign) -> None:
			value.bindings["cases"][1] = copy.deepcopy(value.bindings["cases"][0])
			value.rewrite_bindings()
		_expect_self_test_failure(root, "duplicate-case", duplicate_case)
		def missing_case(value: _SelfTestCampaign) -> None:
			value.bindings["cases"].pop()
			value.rewrite_bindings()
		_expect_self_test_failure(root, "missing-case", missing_case)
		_expect_self_test_failure(root, "failed-timeline", lambda value: _test_mutate_score(value, 0, "original", lambda score: score.__setitem__("passed", False)))
		_expect_self_test_failure(root, "tail-loss", lambda value: _test_mutate_score(value, 0, "original", lambda score: score.__setitem__("missing_tail_samples", 1)))
		_expect_self_test_failure(root, "clipping", lambda value: _test_mutate_score(value, 0, "original", lambda score: score.__setitem__("received_clipped_samples", 1)))

		def byte_tamper(value: _SelfTestCampaign) -> None:
			with value.manifests[(0, "original")].open("ab") as stream:
				stream.write(b" ")
		_expect_self_test_failure(root, "byte-tamper", byte_tamper)
		_expect_self_test_failure(root, "missing-evidence", lambda value: value.update_manifest(0, "original", lambda doc: doc["completion"]["input"]["voice_contract"].pop("pre_opus_pcm_sha256")))
		def raw_nonfinite_json(value: _SelfTestCampaign, token: str) -> None:
			path = value.manifests[(0, "original")]
			raw = path.read_text(encoding="utf-8")
			raw = raw.replace('"fixed_timeline_sdr_db": 90.0', f'"fixed_timeline_sdr_db": {token}', 1)
			path.write_text(raw, encoding="utf-8")
			value.bindings["cases"][0]["original_manifest"] = _test_file_reference(path)
			value.rewrite_bindings()
		_expect_self_test_failure(root, "nonfinite-json", lambda value: raw_nonfinite_json(value, "NaN"))
		_expect_self_test_failure(root, "overflowing-json-float", lambda value: raw_nonfinite_json(value, "1e999"))

		def nonfinite_capture(value: _SelfTestCampaign) -> None:
			manifest_path = value.manifests[(0, "original")]
			manifest = copy.deepcopy(_load_json_bytes(manifest_path.read_bytes(), "self-test nonfinite capture manifest"))
			capture_path = Path(manifest["artifacts"]["capture_wav"])
			_test_write_float32(capture_path, [float("nan"), *([0.0] * (ALIGNMENT_SAMPLES - 1))])
			capture_hash = file_sha256(capture_path)
			manifest["completion"]["capture_file"].update({
				"sha256": capture_hash, "bytes": capture_path.stat().st_size,
			})
			manifest["voice_contract_evidence"]["received_pcm_sha256"] = capture_hash
			_test_write_json(manifest_path, manifest)
			value.bindings["cases"][0]["original_manifest"] = _test_file_reference(manifest_path)
			value.rewrite_bindings()
		_expect_self_test_failure(root, "nonfinite-capture", nonfinite_capture)

		def duplicate_json_key(value: _SelfTestCampaign) -> None:
			path = value.manifests[(0, "original")]
			text_value = path.read_text(encoding="utf-8")
			text_value = text_value.replace('  "schema_version": 1,', '  "schema_version": 1,\n  "schema_version": 1,', 1)
			path.write_text(text_value, encoding="utf-8")
			value.bindings["cases"][0]["original_manifest"] = _test_file_reference(path)
			value.rewrite_bindings()
		_expect_self_test_failure(root, "duplicate-json-key", duplicate_json_key)

		def unbound_inventory(value: _SelfTestCampaign) -> None:
			path = Path(value.bindings["corpus"]["corpus_inventory"]["path"])
			document = copy.deepcopy(_load_json_bytes(path.read_bytes(), "self-test inventory"))
			document["items"][0]["sha256"] = "9" * 64
			_test_write_json(path, document)
			value.bindings["corpus"]["corpus_inventory"] = _test_file_reference(path)
			value.bindings["corpus"]["corpus_inventory_canonical_sha256"] = canonical_json_sha256(document)
			value.rewrite_bindings()
		_expect_self_test_failure(root, "unbound-inventory", unbound_inventory)

		def standalone_wav_copy(value: _SelfTestCampaign) -> None:
			original = Path(value.bindings["corpus"]["input_wav"]["path"])
			copied = original.with_name("byte-identical-standalone-copy.wav")
			copied.write_bytes(original.read_bytes())
			value.bindings["corpus"]["input_wav"] = _test_file_reference(copied)
			value.rewrite_bindings()
		_expect_self_test_failure(root, "standalone-wav-copy", standalone_wav_copy)

		def render_source_not_in_plan(value: _SelfTestCampaign) -> None:
			render_path = Path(value.bindings["corpus"]["render_manifest"]["path"])
			render = copy.deepcopy(_load_json_bytes(render_path.read_bytes(), "self-test render manifest"))
			render["cases"][0]["speech_source_sha256"] = "a" * 64
			_test_write_json(render_path, render)
			render_entry_sha = canonical_json_sha256(render["cases"][0])
			value.bindings["corpus"]["render_manifest"] = _test_file_reference(render_path)
			value.bindings["corpus"]["render_entry_sha256"] = render_entry_sha
			fixture_path = Path(value.bindings["corpus"]["fixture_attestation"]["path"])
			fixture = copy.deepcopy(_load_json_bytes(fixture_path.read_bytes(), "self-test fixture"))
			fixture["render_manifest_sha256"] = file_sha256(render_path)
			fixture["render_entry_sha256"] = render_entry_sha
			_test_write_json(fixture_path, fixture)
			value.bindings["corpus"]["fixture_attestation"] = _test_file_reference(fixture_path)
			value.rewrite_bindings()
		_expect_self_test_failure(root, "render-source-not-in-plan", render_source_not_in_plan)


def main(argv: Sequence[str] | None = None) -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--bindings", type=Path, help="externally pinned 45-pair campaign-bindings JSON")
	parser.add_argument("--bindings-sha256", help="external SHA-256 pin for --bindings")
	parser.add_argument("--candidate-commit", help="full clean candidate Git commit")
	parser.add_argument("--legacy-build-commit", help="exact frozen buildable legacy client commit")
	parser.add_argument("--legacy-instrumentation-base", help="exact frozen legacy E2E instrumentation base commit")
	parser.add_argument("--legacy-executable-sha256", help="exact frozen staged legacy mumble.exe SHA-256")
	parser.add_argument("--output", type=Path, help="new original-voice-qualification.json path")
	parser.add_argument("--provenance-output", type=Path, help="new sibling provenance receipt path")
	parser.add_argument("--self-test", action="store_true")
	args = parser.parse_args(argv)
	try:
		if args.self_test:
			run_self_test()
			print("Original voice qualification assembler self-test: ok")
			if args.bindings is None:
				return 0
		missing = [
			name for name, value in (
				("--bindings", args.bindings), ("--bindings-sha256", args.bindings_sha256),
				("--candidate-commit", args.candidate_commit),
				("--legacy-build-commit", args.legacy_build_commit),
				("--legacy-instrumentation-base", args.legacy_instrumentation_base),
				("--legacy-executable-sha256", args.legacy_executable_sha256),
				("--provenance-output", args.provenance_output), ("--output", args.output),
			) if value is None
		]
		if missing:
			raise AssemblyError("required arguments are missing: " + ", ".join(missing))
		qualification, provenance = assemble(
			args.bindings, args.bindings_sha256, args.candidate_commit,
			args.legacy_build_commit, args.legacy_instrumentation_base, args.legacy_executable_sha256,
		)
		_write_outputs(qualification, provenance, args.output, args.provenance_output)
		print(f"Original voice qualification assembled: {args.output} (45 paired cases)")
		print(f"Original voice provenance receipt: {args.provenance_output}")
		return 0
	except (AssemblyError, PayloadIdentityError, OSError, CHECKER.ContractError) as error:
		print(f"Original voice qualification assembler error: {error}", file=sys.stderr)
		return 1


if __name__ == "__main__":
	sys.exit(main())
