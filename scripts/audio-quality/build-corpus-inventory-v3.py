#!/usr/bin/env python3
"""Materialize a deterministic, release-eligible schema-v3 evaluation corpus.

The builder verifies the pinned local-evaluation archives, reads only selected
members, converts selected audio to mono 48 kHz PCM16, and writes a hash-bound
inventory. McGill TSP is verified and recorded but is deliberately not emitted
as release speech because its archive has no utterance transcripts. Mini
LibriSpeech supplies transcripted English; pinned FLEURS supplies three
transcripted Swedish speakers; OpenSLR28 supplies room noise and RIRs; DEMAND
supplies split-bound environmental-noise families; tracked FIR definitions
supply explicitly modeled microphone responses. Nightly additionally uses a
privacy-scrubbed, metadata-only selection over RixVox v1, LibriSpeech
test-clean and clip-level-CC0 FSD50K. New nightly holdout members remain sealed;
this builder intentionally has no unauthenticated unseal path.

DEMAND holdout preparation is deliberately mechanical: the fixed member,
offset and length below are converted and hashed without listening, scoring,
rendering a mixture, or using any model output. Holdout mixtures remain sealed
until final qualification.
"""

from __future__ import annotations

import argparse
import array
import binascii
import csv
import hashlib
import importlib.util
import io
import json
import math
import os
import re
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile
import wave
import zipfile
import zlib
from pathlib import Path, PurePosixPath
from typing import Any, Mapping, MutableMapping, Sequence


class BuildError(ValueError):
	"""Raised when corpus material cannot be produced reproducibly."""


SCRIPT_VERSION = "4"
# Identifier-only search across the frozen speech/noise/RIR/device group IDs.
# This seed gives every split the full master-quality diversity floor without
# looking at, rendering, listening to, or scoring any audio.
COMMUNITY_RELEASE_SPLIT_SEED = "mumble-community-master-v2-00909005"
DEMAND_SOURCE_SPECS = (
	{
		"source_id": "demand-dkitchen-16k", "member": "DKITCHEN/ch01.wav",
		"group_id": "demand-dkitchen", "noise_class": "hum", "split": "tuning",
	},
	{
		"source_id": "demand-dliving-16k", "member": "DLIVING/ch01.wav",
		"group_id": "demand-dliving", "noise_class": "music-tv", "split": "holdout",
	},
	{
		"source_id": "demand-dwashing-16k", "member": "DWASHING/ch01.wav",
		"group_id": "demand-dwashing", "noise_class": "hum", "split": "validation",
	},
	{
		"source_id": "demand-nfield-16k", "member": "NFIELD/ch01.wav",
		"group_id": "demand-nfield", "noise_class": "wind", "split": "tuning",
	},
	{
		"source_id": "demand-npark-16k", "member": "NPARK/ch01.wav",
		"group_id": "demand-npark", "noise_class": "wind", "split": "holdout",
	},
	{
		"source_id": "demand-nriver-16k", "member": "NRIVER/ch01.wav",
		"group_id": "demand-nriver", "noise_class": "rain", "split": "holdout",
	},
	{
		"source_id": "demand-ohallway-16k", "member": "OHALLWAY/ch01.wav",
		"group_id": "demand-ohallway", "noise_class": "handling", "split": "tuning",
	},
	{
		"source_id": "demand-omeeting-16k", "member": "OMEETING/ch01.wav",
		"group_id": "demand-omeeting", "noise_class": "competing-speech", "split": "tuning",
	},
	{
		"source_id": "demand-ooffice-16k", "member": "OOFFICE/ch01.wav",
		"group_id": "demand-ooffice", "noise_class": "keyboard", "split": "validation",
	},
	{
		"source_id": "demand-pcafeter-16k", "member": "PCAFETER/ch01.wav",
		"group_id": "demand-pcafeter", "noise_class": "babble", "split": "holdout",
	},
	{
		"source_id": "demand-presto-16k", "member": "PRESTO/ch01.wav",
		"group_id": "demand-presto", "noise_class": "babble", "split": "validation",
	},
	{
		"source_id": "demand-pstation-16k", "member": "PSTATION/ch01.wav",
		"group_id": "demand-pstation", "noise_class": "competing-speech", "split": "validation",
	},
	{
		"source_id": "demand-scafe-48k", "member": "SCAFE/ch01.wav",
		"group_id": "demand-scafe", "noise_class": "babble", "split": "holdout",
	},
	{
		"source_id": "demand-spsquare-16k", "member": "SPSQUARE/ch01.wav",
		"group_id": "demand-spsquare", "noise_class": "babble", "split": "tuning",
	},
	{
		"source_id": "demand-straffic-16k", "member": "STRAFFIC/ch01.wav",
		"group_id": "demand-straffic", "noise_class": "traffic", "split": "holdout",
	},
	{
		"source_id": "demand-tbus-16k", "member": "TBUS/ch01.wav",
		"group_id": "demand-tbus", "noise_class": "traffic", "split": "validation",
	},
	{
		"source_id": "demand-tcar-16k", "member": "TCAR/ch01.wav",
		"group_id": "demand-tcar", "noise_class": "traffic", "split": "validation",
	},
	{
		"source_id": "demand-tmetro-16k", "member": "TMETRO/ch01.wav",
		"group_id": "demand-tmetro", "noise_class": "traffic", "split": "tuning",
	},
)
DEMAND_SOURCE_IDS = tuple(spec["source_id"] for spec in DEMAND_SOURCE_SPECS)
REQUIRED_SOURCE_IDS = (
	*DEMAND_SOURCE_IDS,
	"google-fleurs-sv-se-train-v2",
	"mcgill-tsp-speech-v2-48k",
	"openslr28-rirs-noises",
	"openslr31-mini-librispeech-dev-clean-2",
)
SPEECH_SOURCE_ID = "openslr31-mini-librispeech-dev-clean-2"
NIGHTLY_SELECTION_PATH = Path(__file__).with_name("nightly-corpus-selection-v1.json")
NIGHTLY_SPEECH_SOURCE_ID = "openslr12-librispeech-test-clean"
RIXVOX_SOURCE_IDS = ("rixvox-v1-dev-0", "rixvox-v1-test-0")
RIXVOX_METADATA_SIDECAR_ID = "split-metadata"
FSD50K_SOURCE_ID = "fsd50k-eval-cc0-subset"
FSD50K_FIRST_VOLUME_SIDECAR_ID = "eval-audio-first-volume"
FSD50K_GROUND_TRUTH_SIDECAR_ID = "eval-ground-truth"
FSD50K_METADATA_SIDECAR_ID = "eval-clip-metadata"
NIGHTLY_SOURCE_IDS = (
	*REQUIRED_SOURCE_IDS,
	FSD50K_SOURCE_ID,
	NIGHTLY_SPEECH_SOURCE_ID,
	*RIXVOX_SOURCE_IDS,
)
SWEDISH_SPEECH_SOURCE_ID = "google-fleurs-sv-se-train-v2"
SWEDISH_TRANSCRIPT_SIDECAR_ID = "train-transcripts"
ROOM_SOURCE_ID = "openslr28-rirs-noises"
MIN_AUDIO_MS = 6000
SPEAKERS_PER_SPLIT = 8
UTTERANCES_PER_SPEAKER = 2
# Preserve every available independent OpenSLR28 isotropic-noise environment;
# the optional selector caps at this value but never invents duplicate groups.
NOISE_GROUPS_PER_SPLIT = 64
RIR_GROUPS_PER_SPLIT = 12
DEVICE_GROUPS_PER_SPLIT = 8
DEMAND_SEGMENT_START_MS = 60000
DEMAND_SEGMENT_DURATION_MS = 60000
MAX_MEMBER_BYTES = 512 * 1024 * 1024


def _load_sibling(name: str, module_name: str) -> Any:
	path = Path(__file__).with_name(name)
	spec = importlib.util.spec_from_file_location(module_name, path)
	if spec is None or spec.loader is None:
		raise BuildError(f"unable to load {path}")
	module = importlib.util.module_from_spec(spec)
	spec.loader.exec_module(module)
	return module


LOCK = _load_sibling("validate-corpus-lock.py", "mumble_builder_corpus_lock")
INVENTORY = _load_sibling("corpus-inventory-v3.py", "mumble_builder_inventory_v3")


def _expect(condition: bool, path: str, message: str) -> None:
	if not condition:
		raise BuildError(f"{path}: {message}")


def _load_json(path: Path) -> Any:
	def reject_duplicates(pairs: Sequence[tuple[str, Any]]) -> MutableMapping[str, Any]:
		result: MutableMapping[str, Any] = {}
		for key, value in pairs:
			if key in result:
				raise BuildError(f"{path}: duplicate JSON key {key!r}")
			result[key] = value
		return result
	try:
		return json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=reject_duplicates)
	except (OSError, json.JSONDecodeError) as error:
		raise BuildError(f"unable to read {path}: {error}") from error


def _canonical_bytes(value: Any) -> bytes:
	return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")


def _canonical_sha256(value: Any) -> str:
	return hashlib.sha256(_canonical_bytes(value)).hexdigest()


def _file_sha256(path: Path) -> str:
	digest = hashlib.sha256()
	with path.open("rb") as stream:
		for chunk in iter(lambda: stream.read(1024 * 1024), b""):
			digest.update(chunk)
	return digest.hexdigest()


def _load_nightly_selection(path: Path = NIGHTLY_SELECTION_PATH) -> tuple[Mapping[str, Any], str]:
	selection = _load_json(path)
	_expect(isinstance(selection, dict), str(path), "must be an object")
	_expect(
		set(selection) == {
			"fsd50k", "openslr12_test_clean_speakers", "rixvox", "schema_version",
			"selection_policy", "split_seed",
		},
		str(path), "unexpected selection manifest keys",
	)
	_expect(selection["schema_version"] == 1, str(path), "unsupported selection schema")
	_expect(selection["split_seed"] == COMMUNITY_RELEASE_SPLIT_SEED, str(path), "split seed changed")
	_expect("no audio" in selection["selection_policy"], str(path), "selection must declare score-free metadata policy")

	openslr = selection["openslr12_test_clean_speakers"]
	_expect(isinstance(openslr, dict) and set(openslr) == set(INVENTORY.SPLITS), "nightly.openslr12", "requires every split")
	for split in INVENTORY.SPLITS:
		speakers = openslr[split]
		_expect(
			isinstance(speakers, list) and len(speakers) == 2
			and speakers == sorted(set(speakers)) and all(re.fullmatch(r"[0-9]+", value) for value in speakers),
			f"nightly.openslr12.{split}", "requires two sorted unique numeric speaker IDs",
		)
		for speaker in speakers:
			group = f"librispeech-speaker-{speaker}"
			_expect(INVENTORY.assigned_split(selection["split_seed"], "speech", group) == split, group, "frozen split changed")

	rixvox = selection["rixvox"]
	_expect(isinstance(rixvox, dict) and set(rixvox) == {"revision", "speaker_group_rule", "utterances"}, "nightly.rixvox", "unexpected keys")
	_expect(rixvox["revision"] == "9b2b6066ee184faf436363ff0823f2e465ccfb31", "nightly.rixvox.revision", "revision changed")
	rows = rixvox["utterances"]
	_expect(isinstance(rows, list) and len(rows) == 15, "nightly.rixvox.utterances", "requires exactly fifteen speakers")
	groups: set[str] = set()
	members: set[tuple[str, str]] = set()
	rixvox_counts = {split: 0 for split in INVENTORY.SPLITS}
	for index, row in enumerate(rows):
		label = f"nightly.rixvox.utterances[{index}]"
		_expect(isinstance(row, dict) and set(row) == {
			"duration_ms", "group_id", "member", "source_id", "transcript", "utterance_rank_sha256",
		}, label, "unexpected keys")
		_expect(row["source_id"] in RIXVOX_SOURCE_IDS, f"{label}.source_id", "unknown shard")
		_expect(bool(re.fullmatch(r"rixvox-v1-speaker-[0-9a-f]{64}", row["group_id"])), f"{label}.group_id", "must be privacy-hashed")
		_safe_member(row["member"])
		_expect(isinstance(row["duration_ms"], int) and row["duration_ms"] >= MIN_AUDIO_MS, f"{label}.duration_ms", "too short")
		_expect(isinstance(row["transcript"], str) and bool(row["transcript"].strip()), f"{label}.transcript", "missing transcript")
		_expect(bool(re.fullmatch(r"[0-9a-f]{64}", row["utterance_rank_sha256"])), f"{label}.utterance_rank_sha256", "invalid rank")
		_expect(row["group_id"] not in groups, f"{label}.group_id", "duplicate speaker")
		_expect((row["source_id"], row["member"]) not in members, f"{label}.member", "duplicate member")
		groups.add(row["group_id"])
		members.add((row["source_id"], row["member"]))
		rixvox_counts[INVENTORY.assigned_split(selection["split_seed"], "speech", row["group_id"])] += 1
	_expect(rixvox_counts == {"tuning": 5, "validation": 5, "holdout": 5}, "nightly.rixvox", f"split regression: {rixvox_counts}")

	fsd = selection["fsd50k"]
	_expect(isinstance(fsd, dict) and set(fsd) == {"clip_license_required", "clips", "group_rule"}, "nightly.fsd50k", "unexpected keys")
	_expect(fsd["clip_license_required"] == "http://creativecommons.org/publicdomain/zero/1.0/", "nightly.fsd50k", "must remain clip-level CC0")
	clips = fsd["clips"]
	_expect(isinstance(clips, list) and len(clips) == 21, "nightly.fsd50k.clips", "requires exactly twenty-one groups")
	fsd_groups: set[str] = set()
	clip_ids: set[str] = set()
	fsd_counts = {split: 0 for split in INVENTORY.SPLITS}
	for index, clip in enumerate(clips):
		label = f"nightly.fsd50k.clips[{index}]"
		_expect(isinstance(clip, dict) and set(clip) == {
			"clip_id", "group_id", "member", "noise_class", "required_label",
		}, label, "unexpected keys")
		_expect(bool(re.fullmatch(r"[0-9]+", clip["clip_id"])), f"{label}.clip_id", "invalid clip id")
		_expect(clip["member"] == f"FSD50K.eval_audio/{clip['clip_id']}.wav", f"{label}.member", "member/id mismatch")
		_expect(bool(re.fullmatch(r"fsd50k-uploader-[0-9a-f]{64}", clip["group_id"])), f"{label}.group_id", "must hash uploader")
		_expect(clip["noise_class"] in INVENTORY.NOISE_CLASSES, f"{label}.noise_class", "unsupported class")
		_expect(isinstance(clip["required_label"], str) and bool(clip["required_label"]), f"{label}.required_label", "missing direct label")
		_expect(clip["clip_id"] not in clip_ids and clip["group_id"] not in fsd_groups, label, "clip and uploader groups must be unique")
		clip_ids.add(clip["clip_id"])
		fsd_groups.add(clip["group_id"])
		fsd_counts[INVENTORY.assigned_split(selection["split_seed"], "noise", clip["group_id"])] += 1
	_expect(fsd_counts == {"tuning": 6, "validation": 7, "holdout": 8}, "nightly.fsd50k", f"split regression: {fsd_counts}")
	return selection, _file_sha256(path)


def _write_json(path: Path, value: Any) -> bytes:
	payload = (json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode("utf-8")
	path.parent.mkdir(parents=True, exist_ok=True)
	with path.open("xb") as stream:
		stream.write(payload)
	return payload


def _safe_member(name: str) -> str:
	_expect(isinstance(name, str) and bool(name), "archive member", "empty name")
	_expect("\\" not in name and "\0" not in name, name, "non-portable archive member")
	parsed = PurePosixPath(name)
	_expect(name == parsed.as_posix(), name, "member path is not normalized")
	_expect(not parsed.is_absolute() and "." not in parsed.parts and ".." not in parsed.parts, name, "unsafe archive member")
	return name


class ArchiveReader:
	def __init__(self, path: Path) -> None:
		self.path = path
		self._zip: zipfile.ZipFile | None = None
		self._tar: tarfile.TarFile | None = None
		self._members: dict[str, Any] = {}

	def __enter__(self) -> "ArchiveReader":
		if self.path.name.lower().endswith(".zip"):
			self._zip = zipfile.ZipFile(self.path, "r")
			for info in self._zip.infolist():
				if info.is_dir():
					continue
				name = _safe_member(info.filename)
				_expect(name not in self._members, name, "duplicate archive member")
				_expect(not (info.flag_bits & 0x1), name, "encrypted members are forbidden")
				_expect(0 <= info.file_size <= MAX_MEMBER_BYTES, name, "member exceeds safety limit")
				self._members[name] = info
		else:
			self._tar = tarfile.open(self.path, "r:gz")
			for info in self._tar.getmembers():
				if info.isdir():
					continue
				name = _safe_member(info.name)
				_expect(info.isfile(), name, "links and special archive members are forbidden")
				_expect(name not in self._members, name, "duplicate archive member")
				_expect(0 <= info.size <= MAX_MEMBER_BYTES, name, "member exceeds safety limit")
				self._members[name] = info
		return self

	def __exit__(self, *_: object) -> None:
		if self._zip is not None:
			self._zip.close()
		if self._tar is not None:
			self._tar.close()

	@property
	def names(self) -> tuple[str, ...]:
		return tuple(sorted(self._members))

	def read(self, name: str) -> bytes:
		_safe_member(name)
		_expect(name in self._members, name, "archive member is missing")
		if self._zip is not None:
			payload = self._zip.read(self._members[name])
		else:
			assert self._tar is not None
			stream = self._tar.extractfile(self._members[name])
			_expect(stream is not None, name, "unable to read archive member")
			with stream:
				payload = stream.read(MAX_MEMBER_BYTES + 1)
		_expect(len(payload) <= MAX_MEMBER_BYTES, name, "decompressed member exceeds safety limit")
		return payload


class SplitZipReader:
	"""Read selected members from an explicitly ordered, hash-verified split ZIP."""

	def __init__(self, paths: Sequence[Path]) -> None:
		self.paths = tuple(paths)
		self._sizes: tuple[int, ...] = ()
		self._members: dict[str, tuple[int, int, int, int, int, int, int]] = {}

	def _advance(self, disk: int, offset: int) -> tuple[int, int]:
		while disk < len(self._sizes) and offset >= self._sizes[disk]:
			offset -= self._sizes[disk]
			disk += 1
		_expect(disk < len(self._sizes), "split ZIP", "offset escapes archive volumes")
		return disk, offset

	def _read_span(self, disk: int, offset: int, length: int) -> bytes:
		_expect(0 <= disk < len(self.paths) and offset >= 0 and length >= 0, "split ZIP", "invalid span")
		parts = []
		remaining = length
		disk, offset = self._advance(disk, offset)
		while remaining:
			available = self._sizes[disk] - offset
			take = min(available, remaining)
			with self.paths[disk].open("rb") as stream:
				stream.seek(offset)
				payload = stream.read(take)
			_expect(len(payload) == take, "split ZIP", "truncated volume")
			parts.append(payload)
			remaining -= take
			disk += 1
			offset = 0
			_expect(remaining == 0 or disk < len(self.paths), "split ZIP", "span escapes archive volumes")
		return b"".join(parts)

	@staticmethod
	def _zip64_values(extra: bytes, uncompressed: int, compressed: int, local_offset: int, disk: int) -> tuple[int, int, int, int]:
		fields: dict[int, bytes] = {}
		offset = 0
		while offset + 4 <= len(extra):
			field_id, size = struct.unpack_from("<HH", extra, offset)
			offset += 4
			_expect(offset + size <= len(extra), "split ZIP extra", "truncated field")
			fields[field_id] = extra[offset:offset + size]
			offset += size
		if not any((uncompressed == 0xFFFFFFFF, compressed == 0xFFFFFFFF, local_offset == 0xFFFFFFFF, disk == 0xFFFF)):
			return uncompressed, compressed, local_offset, disk
		_expect(0x0001 in fields, "split ZIP", "ZIP64 values are missing")
		payload = fields[0x0001]
		position = 0
		values = [uncompressed, compressed, local_offset, disk]
		for index, (sentinel, width) in enumerate(((0xFFFFFFFF, 8), (0xFFFFFFFF, 8), (0xFFFFFFFF, 8), (0xFFFF, 4))):
			if values[index] != sentinel:
				continue
			_expect(position + width <= len(payload), "split ZIP", "truncated ZIP64 field")
			values[index] = int.from_bytes(payload[position:position + width], "little")
			position += width
		return values[0], values[1], values[2], values[3]

	def __enter__(self) -> "SplitZipReader":
		_expect(len(self.paths) >= 2, "split ZIP", "requires at least two volumes")
		_expect(all(path.is_file() for path in self.paths), "split ZIP", "a volume is missing")
		self._sizes = tuple(path.stat().st_size for path in self.paths)
		tail_size = min(self._sizes[-1], 65557)
		with self.paths[-1].open("rb") as stream:
			stream.seek(self._sizes[-1] - tail_size)
			tail = stream.read(tail_size)
		eocd_offset = tail.rfind(b"PK\x05\x06")
		_expect(eocd_offset >= 0 and eocd_offset + 22 <= len(tail), "split ZIP", "end record is missing")
		(
			disk_number, directory_disk, entries_on_disk, total_entries,
			directory_size, directory_offset, comment_size,
		) = struct.unpack_from("<4H2IH", tail, eocd_offset + 4)
		_expect(eocd_offset + 22 + comment_size == len(tail), "split ZIP", "invalid end-record comment")
		_expect(disk_number == len(self.paths) - 1 and directory_disk < len(self.paths), "split ZIP", "volume order/count mismatch")
		_expect(total_entries not in (0, 0xFFFF) and directory_size != 0xFFFFFFFF and directory_offset != 0xFFFFFFFF, "split ZIP", "unsupported ZIP64 end record")
		_expect(entries_on_disk <= total_entries and directory_size <= 128 * 1024 * 1024, "split ZIP", "implausible directory")
		directory = self._read_span(directory_disk, directory_offset, directory_size)
		offset = 0
		for _ in range(total_entries):
			_expect(offset + 46 <= len(directory) and directory[offset:offset + 4] == b"PK\x01\x02", "split ZIP", "invalid central directory")
			(
				flags, method, crc32, compressed, uncompressed, filename_size,
				extra_size, member_comment_size, member_disk, local_offset,
			) = (
				struct.unpack_from("<H", directory, offset + 8)[0],
				struct.unpack_from("<H", directory, offset + 10)[0],
				struct.unpack_from("<I", directory, offset + 16)[0],
				struct.unpack_from("<I", directory, offset + 20)[0],
				struct.unpack_from("<I", directory, offset + 24)[0],
				struct.unpack_from("<H", directory, offset + 28)[0],
				struct.unpack_from("<H", directory, offset + 30)[0],
				struct.unpack_from("<H", directory, offset + 32)[0],
				struct.unpack_from("<H", directory, offset + 34)[0],
				struct.unpack_from("<I", directory, offset + 42)[0],
			)
			end = offset + 46 + filename_size + extra_size + member_comment_size
			_expect(end <= len(directory), "split ZIP", "truncated central member")
			filename_bytes = directory[offset + 46:offset + 46 + filename_size]
			extra = directory[offset + 46 + filename_size:offset + 46 + filename_size + extra_size]
			uncompressed, compressed, local_offset, member_disk = self._zip64_values(
				extra, uncompressed, compressed, local_offset, member_disk,
			)
			encoding = "utf-8" if flags & 0x800 else "cp437"
			try:
				name = filename_bytes.decode(encoding)
			except UnicodeError as error:
				raise BuildError("split ZIP: invalid member filename") from error
			if not name.endswith("/"):
				name = _safe_member(name)
				_expect(name not in self._members, name, "duplicate archive member")
				_expect(not (flags & 0x1), name, "encrypted members are forbidden")
				_expect(method in (0, 8), name, "unsupported compression method")
				_expect(uncompressed <= MAX_MEMBER_BYTES and member_disk < len(self.paths), name, "member exceeds safety limits")
				self._members[name] = (member_disk, local_offset, flags, method, crc32, compressed, uncompressed)
			offset = end
		_expect(offset == len(directory) and len(self._members) > 0, "split ZIP", "directory length/count mismatch")
		return self

	def __exit__(self, *_: object) -> None:
		return None

	@property
	def names(self) -> tuple[str, ...]:
		return tuple(sorted(self._members))

	def read(self, name: str) -> bytes:
		_safe_member(name)
		_expect(name in self._members, name, "archive member is missing")
		disk, local_offset, flags, method, expected_crc, compressed_size, uncompressed_size = self._members[name]
		header = self._read_span(disk, local_offset, 30)
		_expect(header[:4] == b"PK\x03\x04", name, "invalid local header")
		local_flags, local_method = struct.unpack_from("<HH", header, 6)
		filename_size, extra_size = struct.unpack_from("<HH", header, 26)
		_expect(local_flags == flags and local_method == method, name, "central/local header mismatch")
		data_disk, data_offset = self._advance(disk, local_offset + 30 + filename_size + extra_size)
		compressed = self._read_span(data_disk, data_offset, compressed_size)
		try:
			payload = compressed if method == 0 else zlib.decompress(compressed, -zlib.MAX_WBITS)
		except zlib.error as error:
			raise BuildError(f"{name}: invalid deflate payload") from error
		_expect(len(payload) == uncompressed_size, name, "uncompressed size mismatch")
		_expect(binascii.crc32(payload) & 0xFFFFFFFF == expected_crc, name, "CRC32 mismatch")
		return payload


def _validate_state(
	state_path: Path, manifest: Mapping[str, Any], artifact_root: Path,
	required_source_ids: Sequence[str] = REQUIRED_SOURCE_IDS,
) -> tuple[Mapping[str, Path], str]:
	state = _load_json(state_path)
	_expect(isinstance(state, dict), "corpus-state", "must be an object")
	_expect(
		set(state) == {"archives", "corpus_lock_sha256", "fetcher_sha256", "purpose", "schema_version", "state_kind"},
		"corpus-state", "unexpected schema-v3 state keys",
	)
	_expect(state["schema_version"] == 3, "corpus-state.schema_version", "must be 3")
	_expect(state["state_kind"] == "mumble-input-enhancement-corpus-state", "corpus-state.state_kind", "unexpected state kind")
	_expect(state["purpose"] == "local-eval", "corpus-state.purpose", "release evaluation requires local-eval")
	lock_sha = LOCK.canonical_manifest_sha256(manifest)
	_expect(state["corpus_lock_sha256"] == lock_sha, "corpus-state.corpus_lock_sha256", "lock mismatch")
	_expect(isinstance(state["archives"], list), "corpus-state.archives", "must be an array")
	archives = {entry.get("source_id"): entry for entry in state["archives"] if isinstance(entry, dict)}
	_expect(len(archives) == len(state["archives"]), "corpus-state.archives", "duplicate or malformed source entries")
	by_id = {source["id"]: source for source in manifest["sources"]}
	resolved: dict[str, Path] = {}
	root = artifact_root.resolve()
	for source_id in required_source_ids:
		_expect(source_id in archives, f"corpus-state.archives.{source_id}", "required archive is absent")
		entry = archives[source_id]
		_expect(
			set(entry) == {"relative_path", "size_bytes", "source_artifact_sha256", "source_id", "source_kind", "source_url_sha256", "verified"},
			f"corpus-state.archives.{source_id}", "unexpected state entry keys",
		)
		source = by_id[source_id]
		_expect(LOCK.source_policy_error(source, "local-eval") is None, source_id, "source is not policy-eligible")
		_expect(entry["verified"] is True, source_id, "state does not attest verification")
		_expect(entry["source_artifact_sha256"] == source["integrity"]["digest"], source_id, "state artifact hash mismatch")
		_expect(entry["size_bytes"] == source["integrity"]["size_bytes"], source_id, "state size mismatch")
		relative = PurePosixPath(entry["relative_path"])
		_expect(entry["relative_path"] == relative.as_posix() and not relative.is_absolute() and ".." not in relative.parts, source_id, "unsafe state path")
		path = root.joinpath(*relative.parts).resolve()
		try:
			path.relative_to(root)
		except ValueError as error:
			raise BuildError(f"{source_id}: archive escapes artifact root") from error
		_expect(path.is_file(), source_id, f"archive is missing: {path}")
		_expect(path.stat().st_size == source["integrity"]["size_bytes"], source_id, "archive size mismatch")
		_expect(_file_sha256(path) == source["integrity"]["digest"], source_id, "archive hash mismatch")
		resolved[source_id] = path
		for sidecar in source.get("sidecars", []):
			integrity = sidecar["integrity"]
			sidecar_id = sidecar["id"]
			relative = PurePosixPath(integrity["artifact_path"])
			path = root.joinpath(*relative.parts).resolve()
			try:
				path.relative_to(root)
			except ValueError as error:
				raise BuildError(f"{source_id}:{sidecar_id}: sidecar escapes artifact root") from error
			label = f"{source_id}:{sidecar_id}"
			_expect(path.is_file(), label, f"sidecar is missing: {path}")
			_expect(path.stat().st_size == integrity["size_bytes"], label, "sidecar size mismatch")
			_expect(_file_sha256(path) == integrity["digest"], label, "sidecar hash mismatch")
			resolved[label] = path
	return resolved, _file_sha256(state_path)


def _ffmpeg_identity(path: Path, expected_sha256: str) -> Mapping[str, str]:
	_expect(path.is_file(), "ffmpeg", f"executable is missing: {path}")
	actual = _file_sha256(path)
	_expect(re.fullmatch(r"[0-9a-f]{64}", expected_sha256) is not None, "ffmpeg_sha256", "must be lowercase SHA-256")
	_expect(actual == expected_sha256, "ffmpeg", "executable hash mismatch")
	try:
		result = subprocess.run([str(path), "-version"], check=True, capture_output=True, text=True, timeout=15)
	except (OSError, subprocess.SubprocessError) as error:
		raise BuildError(f"unable to execute ffmpeg: {error}") from error
	version = result.stdout.splitlines()[0].strip() if result.stdout else ""
	_expect(bool(version), "ffmpeg", "version output is empty")
	return {"path_sha256": actual, "version": version}


def _wav_info(path: Path) -> tuple[int, int, int, int]:
	try:
		with wave.open(str(path), "rb") as stream:
			info = (stream.getframerate(), stream.getnchannels(), stream.getsampwidth(), stream.getnframes())
	except (OSError, wave.Error) as error:
		raise BuildError(f"invalid WAV {path}: {error}") from error
	_expect(info[0] == 48000 and info[1] == 1 and info[2] == 2 and info[3] > 0, str(path), "must be mono 48 kHz PCM16")
	return info


def _repeat_pcm16_wav_to_minimum(path: Path, minimum_duration_ms: int) -> bool:
	"""Repeat a short noise recording to one exact, deterministic minimum.

	This is deliberately a materialization-time operation: the repeated bytes,
	their hash and the policy parameters enter the inventory.  Plan rendering can
	then require a real contiguous source window and never hide short input with
	implicit looping or zero padding.
	"""
	_expect(
		isinstance(minimum_duration_ms, int) and not isinstance(minimum_duration_ms, bool)
		and minimum_duration_ms >= 1000
		and minimum_duration_ms % 10 == 0,
		str(path), "minimum duration must be >=1000 ms and align to 10 ms",
	)
	rate, channels, sample_width, source_samples = _wav_info(path)
	required_samples = math.ceil(minimum_duration_ms * rate / 1000)
	if source_samples >= required_samples:
		return False
	with wave.open(str(path), "rb") as stream:
		payload = stream.readframes(source_samples)
	_expect(len(payload) == source_samples * channels * sample_width, str(path), "truncated PCM payload")
	repetitions = math.ceil(required_samples / source_samples)
	frame_bytes = channels * sample_width
	repeated = (payload * repetitions)[: required_samples * frame_bytes]
	temporary = path.with_name(f".{path.name}.repeat-{os.getpid()}.tmp.wav")
	try:
		with wave.open(str(temporary), "wb") as stream:
			stream.setnchannels(channels)
			stream.setsampwidth(sample_width)
			stream.setframerate(rate)
			stream.writeframes(repeated)
		_expect(_wav_info(temporary)[3] == required_samples, str(path), "repeated output duration changed")
		os.replace(temporary, path)
	finally:
		temporary.unlink(missing_ok=True)
	return True


def _convert_member(
	reader: ArchiveReader, member: str, destination: Path, ffmpeg: Path,
	ffmpeg_identity: Mapping[str, str], scratch: Path, *, minimum_duration_ms: int | None = None,
) -> tuple[Mapping[str, Any], Mapping[str, Any]]:
	payload = reader.read(member)
	parent_sha = hashlib.sha256(payload).hexdigest()
	parameters = {
		"codec": "pcm_s16le", "channels": 1, "ffmpeg_sha256": ffmpeg_identity["path_sha256"],
		"ffmpeg_version": ffmpeg_identity["version"], "sample_rate_hz": 48000,
		"flags": ["-bitexact", "-fflags", "+bitexact", "-flags:a", "+bitexact", "-map_metadata", "-1", "-threads", "1"],
	}
	if minimum_duration_ms is not None:
		parameters.update({
			"minimum_duration_ms": minimum_duration_ms,
			"short_source_policy": "repeat-whole-clip-to-exact-minimum-v1",
		})
	parameters_sha = _canonical_sha256(parameters)
	suffix = PurePosixPath(member).suffix or ".bin"
	with tempfile.NamedTemporaryFile(prefix="source-", suffix=suffix, dir=scratch, delete=False) as stream:
		source_path = Path(stream.name)
		stream.write(payload)
	temporary_output = scratch / f"converted-{hashlib.sha256(member.encode('utf-8')).hexdigest()}.wav"
	try:
		command = [
			str(ffmpeg), "-nostdin", "-hide_banner", "-loglevel", "error", "-threads", "1",
			"-i", str(source_path), "-map_metadata", "-1", "-ac", "1", "-ar", "48000",
			"-c:a", "pcm_s16le", "-bitexact", "-fflags", "+bitexact", "-flags:a", "+bitexact",
			"-y", str(temporary_output),
		]
		try:
			subprocess.run(command, check=True, capture_output=True, timeout=120)
		except (OSError, subprocess.SubprocessError) as error:
			raise BuildError(f"ffmpeg failed for {member}: {error}") from error
		info = _wav_info(temporary_output)
		if minimum_duration_ms is not None:
			_repeat_pcm16_wav_to_minimum(temporary_output, minimum_duration_ms)
			info = _wav_info(temporary_output)
		destination.parent.mkdir(parents=True, exist_ok=True)
		_expect(not destination.exists(), str(destination), "refusing to overwrite output")
		os.replace(temporary_output, destination)
	finally:
		source_path.unlink(missing_ok=True)
		temporary_output.unlink(missing_ok=True)
	output_sha = _file_sha256(destination)
	common = {
		"sample_rate_hz": info[0], "channels": info[1], "duration_samples": info[3],
		"sha256": output_sha, "size_bytes": destination.stat().st_size,
		"provenance": {
			"derivation": "resampled", "parent_sha256": parent_sha, "parameters_sha256": parameters_sha,
			"source_path": member, "tool": "ffmpeg", "tool_version": ffmpeg_identity["version"],
		},
	}
	transform = {
		"source_member": member, "source_member_sha256": parent_sha, "parameters": parameters,
		"parameters_sha256": parameters_sha, "output_sha256": output_sha,
	}
	return common, transform


def _convert_member_segment(
	reader: ArchiveReader, member: str, destination: Path, ffmpeg: Path,
	ffmpeg_identity: Mapping[str, str], scratch: Path, *, source_sample_rate_hz: int,
	start_ms: int, duration_ms: int,
) -> tuple[Mapping[str, Any], Mapping[str, Any]]:
	"""Blindly convert one fixed archive member/window without content selection."""
	_expect(start_ms >= 0 and duration_ms >= MIN_AUDIO_MS, member, "invalid deterministic segment")
	_expect(duration_ms % 10 == 0, member, "segment must align to the 10 ms product timeline")
	payload = reader.read(member)
	parent_sha = hashlib.sha256(payload).hexdigest()
	derivation = "resampled" if source_sample_rate_hz != 48000 else "decoded"
	parameters = {
		"codec": "pcm_s16le", "channels": 1, "ffmpeg_sha256": ffmpeg_identity["path_sha256"],
		"ffmpeg_version": ffmpeg_identity["version"], "sample_rate_hz": 48000,
		"source_sample_rate_hz": source_sample_rate_hz,
		"segment_start_ms": start_ms, "segment_duration_ms": duration_ms,
		"selection": "fixed-ch01-window-v1-no-audio-score",
		"flags": ["-bitexact", "-fflags", "+bitexact", "-flags:a", "+bitexact", "-map_metadata", "-1", "-threads", "1"],
	}
	parameters_sha = _canonical_sha256(parameters)
	with tempfile.NamedTemporaryFile(prefix="source-", suffix=".wav", dir=scratch, delete=False) as stream:
		source_path = Path(stream.name)
		stream.write(payload)
	temporary_output = scratch / f"segment-{hashlib.sha256(member.encode('utf-8')).hexdigest()}.wav"
	try:
		command = [
			str(ffmpeg), "-nostdin", "-hide_banner", "-loglevel", "error", "-threads", "1",
			"-i", str(source_path), "-ss", f"{start_ms / 1000.0:.3f}", "-t", f"{duration_ms / 1000.0:.3f}",
			"-map", "0:a:0", "-map_metadata", "-1", "-ac", "1", "-ar", "48000",
			"-c:a", "pcm_s16le", "-bitexact", "-fflags", "+bitexact", "-flags:a", "+bitexact",
			"-y", str(temporary_output),
		]
		try:
			subprocess.run(command, check=True, capture_output=True, timeout=120)
		except (OSError, subprocess.SubprocessError) as error:
			raise BuildError(f"ffmpeg failed for fixed segment {member}: {error}") from error
		info = _wav_info(temporary_output)
		_expect(info[3] == duration_ms * 48, member, "fixed segment output duration changed")
		destination.parent.mkdir(parents=True, exist_ok=True)
		_expect(not destination.exists(), str(destination), "refusing to overwrite output")
		os.replace(temporary_output, destination)
	finally:
		source_path.unlink(missing_ok=True)
		temporary_output.unlink(missing_ok=True)
	output_sha = _file_sha256(destination)
	common = {
		"sample_rate_hz": info[0], "channels": info[1], "duration_samples": info[3],
		"sha256": output_sha, "size_bytes": destination.stat().st_size,
		"provenance": {
			"derivation": derivation, "parent_sha256": parent_sha, "parameters_sha256": parameters_sha,
			"source_path": member, "tool": "ffmpeg", "tool_version": ffmpeg_identity["version"],
		},
	}
	transform = {
		"source_member": member, "source_member_sha256": parent_sha, "parameters": parameters,
		"parameters_sha256": parameters_sha, "output_sha256": output_sha,
	}
	return common, transform


def _select_groups(
	groups: Sequence[str], kind: str, seed: str, per_split: int,
	*, family_for_group: Mapping[str, str] | None = None, minimum_families: int = 0,
) -> Mapping[str, list[str]]:
	selected: dict[str, list[str]] = {split: [] for split in INVENTORY.SPLITS}
	for split in INVENTORY.SPLITS:
		candidates = sorted(group for group in set(groups) if INVENTORY.assigned_split(seed, kind, group) == split)
		for group in candidates:
			selected[split].append(group)
			families = {family_for_group[value] for value in selected[split]} if family_for_group else set()
			if len(selected[split]) >= per_split and len(families) >= minimum_families:
				break
		_expect(len(selected[split]) >= per_split, f"selection.{kind}.{split}", f"needs {per_split} independent groups")
		if family_for_group is not None:
			_expect(len({family_for_group[value] for value in selected[split]}) >= minimum_families, f"selection.{kind}.{split}", f"needs {minimum_families} device families")
	return selected


def _select_optional_groups(groups: Sequence[str], kind: str, seed: str, per_split: int) -> Mapping[str, list[str]]:
	"""Select up to N provenance supplements per split without creating a release floor."""
	return {
		split: sorted(group for group in set(groups) if INVENTORY.assigned_split(seed, kind, group) == split)[:per_split]
		for split in INVENTORY.SPLITS
	}


def _estimate_rt60_ms(path: Path) -> int:
	with wave.open(str(path), "rb") as stream:
		frames = stream.readframes(stream.getnframes())
	samples = array.array("h")
	samples.frombytes(frames)
	if sys.byteorder != "little":
		samples.byteswap()
	energy = [float(sample) * float(sample) for sample in samples]
	if not energy or max(energy) <= 0:
		return 0
	cumulative = [0.0] * len(energy)
	total = 0.0
	for index in range(len(energy) - 1, -1, -1):
		total += energy[index]
		cumulative[index] = total
	if total <= 0:
		return 0
	points = []
	for index, value in enumerate(cumulative):
		db = 10.0 * math.log10(max(value / total, 1e-20))
		if -35.0 <= db <= -5.0:
			points.append((index / 48000.0, db))
	if len(points) < 20:
		return 0
	mean_x = sum(point[0] for point in points) / len(points)
	mean_y = sum(point[1] for point in points) / len(points)
	denominator = sum((point[0] - mean_x) ** 2 for point in points)
	if denominator <= 0:
		return 0
	slope = sum((point[0] - mean_x) * (point[1] - mean_y) for point in points) / denominator
	if slope >= -1e-9:
		return 0
	return max(0, min(5000, int(round((-60.0 / slope) * 1000.0))))


def _parse_librispeech(reader: ArchiveReader) -> Mapping[str, list[tuple[str, str]]]:
	transcripts: dict[str, str] = {}
	for member in reader.names:
		if not member.endswith(".trans.txt"):
			continue
		text = reader.read(member).decode("utf-8")
		for line in text.splitlines():
			utterance, separator, transcript = line.partition(" ")
			_expect(bool(separator) and bool(transcript.strip()), member, "malformed transcript line")
			_expect(utterance not in transcripts, utterance, "duplicate transcript")
			transcripts[utterance] = transcript.strip()
	by_speaker: dict[str, list[tuple[str, str]]] = {}
	for member in reader.names:
		if not member.lower().endswith(".flac"):
			continue
		utterance = PurePosixPath(member).stem
		_expect(utterance in transcripts, member, "speech file has no transcript")
		parts = utterance.split("-")
		_expect(len(parts) >= 3 and parts[0].isdigit(), utterance, "unexpected LibriSpeech utterance id")
		by_speaker.setdefault(parts[0], []).append((member, transcripts[utterance]))
	return {speaker: sorted(values) for speaker, values in by_speaker.items()}


def _flac_duration_ms(payload: bytes) -> int:
	_expect(payload.startswith(b"fLaC"), "FLAC", "missing stream marker")
	offset = 4
	while offset + 4 <= len(payload):
		header = payload[offset:offset + 4]
		block_type = header[0] & 0x7F
		length = int.from_bytes(header[1:4], "big")
		offset += 4
		_expect(offset + length <= len(payload), "FLAC", "truncated metadata block")
		block = payload[offset:offset + length]
		if block_type == 0:
			_expect(length == 34, "FLAC STREAMINFO", "unexpected block length")
			packed = int.from_bytes(block[10:18], "big")
			sample_rate = packed >> 44
			total_samples = packed & ((1 << 36) - 1)
			_expect(sample_rate > 0 and total_samples > 0, "FLAC STREAMINFO", "missing duration")
			return int(total_samples * 1000 // sample_rate)
		offset += length
	raise BuildError("FLAC: STREAMINFO block is missing")


def _materialize_speech(
	reader: ArchiveReader, destination: Path, ffmpeg: Path, identity: Mapping[str, str], scratch: Path,
	seed: str, source: Mapping[str, Any], transforms: list[Mapping[str, Any]],
) -> list[Mapping[str, Any]]:
	by_speaker = _parse_librispeech(reader)
	by_speaker = {
		speaker: [value for value in values if _flac_duration_ms(reader.read(value[0])) >= MIN_AUDIO_MS]
		for speaker, values in by_speaker.items()
	}
	by_speaker = {speaker: values for speaker, values in by_speaker.items() if len(values) >= UTTERANCES_PER_SPEAKER}
	group_for_speaker = {speaker: f"librispeech-speaker-{speaker}" for speaker in by_speaker}
	selected = _select_groups(tuple(group_for_speaker.values()), "speech", seed, SPEAKERS_PER_SPLIT)
	wanted = {group for values in selected.values() for group in values}
	items = []
	for speaker in sorted(by_speaker):
		group = group_for_speaker[speaker]
		if group not in wanted:
			continue
		accepted = 0
		for member, transcript in by_speaker[speaker]:
			utterance = PurePosixPath(member).stem.lower()
			relative = f"audio/speech/{utterance}.wav"
			common, transform = _convert_member(reader, member, destination / relative, ffmpeg, identity, scratch)
			if common["duration_samples"] < MIN_AUDIO_MS * 48:
				(destination / relative).unlink()
				continue
			transcript_relative = f"transcripts/{utterance}.txt"
			transcript_payload = (transcript + "\n").encode("utf-8")
			transcript_path = destination / transcript_relative
			transcript_path.parent.mkdir(parents=True, exist_ok=True)
			with transcript_path.open("xb") as stream:
				stream.write(transcript_payload)
			item = {
				"id": f"speech-{utterance}", "kind": "speech", "source_id": SPEECH_SOURCE_ID,
				"relative_path": relative, "group_id": group,
				"source_artifact_sha256": source["integrity"]["digest"], **common,
				"language": "en-US", "speaker_id": f"speaker-{speaker}",
				"transcript": {
					"status": "verified", "relative_path": transcript_relative,
					"sha256": hashlib.sha256(transcript_payload).hexdigest(), "size_bytes": len(transcript_payload),
					"normalization": "exact-utf8",
				},
			}
			items.append(item)
			transforms.append({"item_id": item["id"], "source_id": SPEECH_SOURCE_ID, "output_path": relative, **transform})
			accepted += 1
			if accepted == UTTERANCES_PER_SPEAKER:
				break
		_expect(accepted == UTTERANCES_PER_SPEAKER, group, f"needs {UTTERANCES_PER_SPEAKER} utterances of at least {MIN_AUDIO_MS} ms")
	return items


def _materialize_selected_librispeech(
	reader: ArchiveReader, destination: Path, ffmpeg: Path, identity: Mapping[str, str], scratch: Path,
	source: Mapping[str, Any], selected_speakers: Mapping[str, Sequence[str]], seed: str,
	transforms: list[Mapping[str, Any]],
) -> list[Mapping[str, Any]]:
	by_speaker = _parse_librispeech(reader)
	wanted = {speaker for speakers in selected_speakers.values() for speaker in speakers}
	_expect(
		set(selected_speakers) <= set(INVENTORY.SPLITS)
		and len(wanted) == 2 * len(selected_speakers),
		source["id"], "nightly LibriSpeech selection must contain two unique speakers per requested split",
	)
	_expect(wanted <= set(by_speaker), source["id"], f"selected speakers missing from archive: {sorted(wanted - set(by_speaker))}")
	items: list[Mapping[str, Any]] = []
	for speaker in sorted(wanted):
		group = f"librispeech-speaker-{speaker}"
		candidates = []
		for member, transcript in by_speaker[speaker]:
			if _flac_duration_ms(reader.read(member)) < MIN_AUDIO_MS:
				continue
			rank = hashlib.sha256("\0".join((seed, "nightly-openslr12-utterance-v1", group, member)).encode("utf-8")).hexdigest()
			candidates.append((rank, member, transcript))
		candidates.sort()
		_expect(len(candidates) >= UTTERANCES_PER_SPEAKER, group, "not enough six-second utterances")
		for rank, member, transcript in candidates[:UTTERANCES_PER_SPEAKER]:
			utterance = PurePosixPath(member).stem.lower()
			relative = f"audio/speech/openslr12-{utterance}.wav"
			common, transform = _convert_member(reader, member, destination / relative, ffmpeg, identity, scratch)
			transcript_relative = f"transcripts/openslr12-{utterance}.txt"
			transcript_payload = (transcript + "\n").encode("utf-8")
			transcript_path = destination / transcript_relative
			transcript_path.parent.mkdir(parents=True, exist_ok=True)
			with transcript_path.open("xb") as stream:
				stream.write(transcript_payload)
			item = {
				"id": f"speech-openslr12-{utterance}", "kind": "speech", "source_id": source["id"],
				"relative_path": relative, "group_id": group,
				"source_artifact_sha256": source["integrity"]["digest"], **common,
				"language": "en-US", "speaker_id": f"speaker-{speaker}",
				"transcript": {
					"status": "verified", "relative_path": transcript_relative,
					"sha256": hashlib.sha256(transcript_payload).hexdigest(), "size_bytes": len(transcript_payload),
					"normalization": "exact-utf8",
				},
			}
			items.append(item)
			transforms.append({
				"item_id": item["id"], "source_id": source["id"], "output_path": relative,
				"selection_basis": "frozen-speaker-and-identifier-only-utterance-rank-v1",
				"utterance_rank_sha256": rank, **transform,
			})
	return items


def _parse_fleurs_swedish_tsv(path: Path) -> Mapping[str, list[Mapping[str, Any]]]:
	try:
		text = path.read_text(encoding="utf-8")
	except (OSError, UnicodeError) as error:
		raise BuildError(f"unable to read FLEURS metadata {path}: {error}") from error
	by_sentence: dict[str, list[Mapping[str, Any]]] = {}
	filenames: set[str] = set()
	for line_number, line in enumerate(text.splitlines(), 1):
		_expect(bool(line), f"FLEURS TSV line {line_number}", "empty lines are forbidden")
		fields = line.split("\t")
		_expect(len(fields) == 7, f"FLEURS TSV line {line_number}", "expected exactly seven tab-separated fields")
		sentence_id, filename, raw_transcript, normalized_transcript, character_transcript, sample_count, gender = fields
		_expect(bool(re.fullmatch(r"[0-9]+", sentence_id)), f"FLEURS TSV line {line_number}", "invalid sentence id")
		_expect(bool(re.fullmatch(r"[0-9]+\.wav", filename)), f"FLEURS TSV line {line_number}", "invalid audio filename")
		_expect(filename not in filenames, f"FLEURS TSV line {line_number}", "duplicate audio filename")
		_expect(bool(raw_transcript.strip()), f"FLEURS TSV line {line_number}", "raw transcript is empty")
		_expect(bool(normalized_transcript.strip()), f"FLEURS TSV line {line_number}", "normalized transcript is empty")
		_expect(bool(character_transcript.strip()), f"FLEURS TSV line {line_number}", "character transcript is empty")
		_expect(bool(re.fullmatch(r"[0-9]+", sample_count)) and int(sample_count) > 0, f"FLEURS TSV line {line_number}", "invalid sample count")
		_expect(gender in {"FEMALE", "MALE", "OTHER"}, f"FLEURS TSV line {line_number}", "invalid gender")
		filenames.add(filename)
		by_sentence.setdefault(sentence_id, []).append({
			"filename": filename,
			"raw_transcript": raw_transcript,
			"normalized_transcript": normalized_transcript,
			"sample_count": int(sample_count),
			"gender": gender,
		})
	return {sentence_id: sorted(rows, key=lambda row: row["filename"]) for sentence_id, rows in by_sentence.items()}


def _fleurs_speaker_group(sentence_id: str, filename: str) -> str:
	return f"fleurs-sv-se-sentence-{sentence_id}-recording-{PurePosixPath(filename).stem}"


def _fleurs_source_wav_info(payload: bytes, label: str) -> tuple[int, int, int]:
	_expect(len(payload) >= 12 and payload[:4] == b"RIFF" and payload[8:12] == b"WAVE", label, "invalid RIFF/WAVE header")
	declared_size = int.from_bytes(payload[4:8], "little") + 8
	_expect(declared_size == len(payload), label, "RIFF byte size does not match member")
	offset = 12
	format_info: tuple[int, int, int, int] | None = None
	data_size: int | None = None
	while offset + 8 <= len(payload):
		chunk_id = payload[offset:offset + 4]
		chunk_size = int.from_bytes(payload[offset + 4:offset + 8], "little")
		chunk_start = offset + 8
		chunk_end = chunk_start + chunk_size
		_expect(chunk_end <= len(payload), label, "truncated WAV chunk")
		if chunk_id == b"fmt ":
			_expect(format_info is None and chunk_size >= 16, label, "invalid or duplicate fmt chunk")
			format_tag = int.from_bytes(payload[chunk_start:chunk_start + 2], "little")
			channels = int.from_bytes(payload[chunk_start + 2:chunk_start + 4], "little")
			sample_rate = int.from_bytes(payload[chunk_start + 4:chunk_start + 8], "little")
			block_align = int.from_bytes(payload[chunk_start + 12:chunk_start + 14], "little")
			bits_per_sample = int.from_bytes(payload[chunk_start + 14:chunk_start + 16], "little")
			_expect(format_tag in (1, 3), label, "source WAV must be PCM or IEEE float")
			_expect(bits_per_sample in (16, 32), label, "source WAV must be PCM16 or float32")
			_expect(block_align == channels * (bits_per_sample // 8), label, "invalid WAV block alignment")
			format_info = (sample_rate, channels, block_align, format_tag)
		elif chunk_id == b"data":
			_expect(data_size is None, label, "duplicate data chunk")
			data_size = chunk_size
		offset = chunk_end + (chunk_size & 1)
	_expect(format_info is not None and data_size is not None, label, "WAV fmt or data chunk is missing")
	assert format_info is not None and data_size is not None
	sample_rate, channels, block_align, _ = format_info
	_expect(data_size % block_align == 0, label, "data chunk is not frame-aligned")
	return sample_rate, channels, data_size // block_align


def _materialize_fleurs_swedish(
	reader: ArchiveReader, metadata_path: Path, destination: Path, ffmpeg: Path,
	identity: Mapping[str, str], scratch: Path, seed: str, source: Mapping[str, Any],
	transforms: list[Mapping[str, Any]],
) -> tuple[list[Mapping[str, Any]], Mapping[str, Any]]:
	by_sentence = _parse_fleurs_swedish_tsv(metadata_path)
	wav_members: dict[str, str] = {}
	for member in reader.names:
		if not member.lower().endswith(".wav"):
			continue
		filename = PurePosixPath(member).name
		_expect(filename not in wav_members, member, "duplicate FLEURS WAV basename")
		wav_members[filename] = member
	metadata_filenames = {
		row["filename"] for rows in by_sentence.values() for row in rows
	}
	_expect(len(metadata_filenames) == source["audio"]["file_count"], "FLEURS TSV", "file count does not match corpus lock")
	_expect(set(wav_members) == metadata_filenames, "FLEURS archive", "WAV members do not exactly match transcript metadata")

	selected_sentence: str | None = None
	selected_rows: list[Mapping[str, Any]] = []
	for sentence_id in sorted(by_sentence, key=int):
		rows = by_sentence[sentence_id]
		if len(rows) != 3 or any(row["sample_count"] < MIN_AUDIO_MS * 16 for row in rows):
			continue
		if len({row["raw_transcript"] for row in rows}) != 1 or len({row["normalized_transcript"] for row in rows}) != 1:
			raise BuildError(f"FLEURS sentence {sentence_id}: recordings disagree on transcript")
		assigned = {
			INVENTORY.assigned_split(seed, "speech", _fleurs_speaker_group(sentence_id, row["filename"]))
			for row in rows
		}
		if assigned == set(INVENTORY.SPLITS):
			selected_sentence = sentence_id
			selected_rows = rows
			break
	_expect(selected_sentence is not None, "FLEURS selection", "no three-speaker sentence covers tuning, validation and holdout")

	assert selected_sentence is not None
	sidecar = next(
		(sidecar for sidecar in source.get("sidecars", []) if sidecar["id"] == SWEDISH_TRANSCRIPT_SIDECAR_ID),
		None,
	)
	_expect(sidecar is not None, "FLEURS source", "pinned transcript sidecar is missing")
	items: list[Mapping[str, Any]] = []
	selection_rows = []
	for row in selected_rows:
		filename = row["filename"]
		member = wav_members[filename]
		source_payload = reader.read(member)
		sample_rate, channels, frames = _fleurs_source_wav_info(source_payload, member)
		_expect((sample_rate, channels) == (16000, 1), member, "expected mono 16 kHz source audio")
		_expect(frames == row["sample_count"], member, "WAV frame count does not match TSV")
		stem = PurePosixPath(filename).stem
		group = _fleurs_speaker_group(selected_sentence, filename)
		assigned_split = INVENTORY.assigned_split(seed, "speech", group)
		relative = f"audio/speech/fleurs-sv-se-{stem}.wav"
		common, transform = _convert_member(reader, member, destination / relative, ffmpeg, identity, scratch)
		_expect(common["duration_samples"] == frames * 3, member, "48 kHz output duration does not match source timeline")
		transcript_relative = f"transcripts/fleurs-sv-se-{stem}.txt"
		transcript_payload = (row["raw_transcript"] + "\n").encode("utf-8")
		transcript_path = destination / transcript_relative
		transcript_path.parent.mkdir(parents=True, exist_ok=True)
		with transcript_path.open("xb") as stream:
			stream.write(transcript_payload)
		item = {
			"id": f"speech-fleurs-sv-se-{stem}", "kind": "speech", "source_id": SWEDISH_SPEECH_SOURCE_ID,
			"relative_path": relative, "group_id": group,
			"source_artifact_sha256": source["integrity"]["digest"], **common,
			"language": "sv-SE", "speaker_id": group,
			"transcript": {
				"status": "verified", "relative_path": transcript_relative,
				"sha256": hashlib.sha256(transcript_payload).hexdigest(), "size_bytes": len(transcript_payload),
				"normalization": "exact-utf8",
			},
		}
		items.append(item)
		transforms.append({
			"item_id": item["id"], "source_id": SWEDISH_SPEECH_SOURCE_ID, "output_path": relative,
			"transcript_sidecar_sha256": sidecar["integrity"]["digest"],
			"selection_basis": "first-numeric-sentence-with-three-distinct-recordings-covering-all-hash-splits-v1",
			**transform,
		})
		selection_rows.append({
			"assigned_split": assigned_split, "filename": filename, "group_id": group,
			"source_samples": frames,
		})
	return items, {
		"sentence_id": selected_sentence,
		"speaker_distinctness_basis": "FLEURS collection protocol: three recordings of one sentence use three different native speakers",
		"selection_uses_audio_scores": False,
		"transcript_sidecar_sha256": sidecar["integrity"]["digest"],
		"recordings": sorted(selection_rows, key=lambda row: row["assigned_split"]),
	}


def _resolve_selected_member(names: Sequence[str], expected: str, label: str) -> str:
	_safe_member(expected)
	if expected in names:
		return expected
	matches = [name for name in names if name.endswith("/" + expected)]
	_expect(len(matches) == 1, label, f"selected member {expected!r} is missing or ambiguous")
	return matches[0]


def _materialize_rixvox(
	readers: Mapping[str, ArchiveReader], selection_rows: Sequence[Mapping[str, Any]], destination: Path,
	ffmpeg: Path, identity: Mapping[str, str], scratch: Path, by_id: Mapping[str, Mapping[str, Any]],
	selection_sha256: str, transforms: list[Mapping[str, Any]],
) -> list[Mapping[str, Any]]:
	items: list[Mapping[str, Any]] = []
	for row in selection_rows:
		source_id = row["source_id"]
		source = by_id[source_id]
		reader = readers[source_id]
		member = _resolve_selected_member(reader.names, row["member"], source_id)
		payload = reader.read(member)
		sample_rate, channels, frames = _fleurs_source_wav_info(payload, member)
		_expect(channels == 1, member, "RixVox source must be mono")
		actual_duration_ms = frames * 1000.0 / sample_rate
		_expect(abs(actual_duration_ms - row["duration_ms"]) <= 10.0, member, "duration disagrees with pinned metadata")
		stem = PurePosixPath(row["member"]).stem.lower()
		group_hash = row["group_id"].removeprefix("rixvox-v1-speaker-")
		relative = f"audio/speech/rixvox-{group_hash[:12]}-{stem}.wav"
		common, transform = _convert_member(reader, member, destination / relative, ffmpeg, identity, scratch)
		transcript_relative = f"transcripts/rixvox-{group_hash[:12]}-{stem}.txt"
		transcript_payload = (row["transcript"] + "\n").encode("utf-8")
		transcript_path = destination / transcript_relative
		transcript_path.parent.mkdir(parents=True, exist_ok=True)
		with transcript_path.open("xb") as stream:
			stream.write(transcript_payload)
		item = {
			"id": f"speech-rixvox-{group_hash[:12]}-{stem}", "kind": "speech", "source_id": source_id,
			"relative_path": relative, "group_id": row["group_id"],
			"source_artifact_sha256": source["integrity"]["digest"], **common,
			"language": "sv-SE", "speaker_id": row["group_id"],
			"transcript": {
				"status": "verified", "relative_path": transcript_relative,
				"sha256": hashlib.sha256(transcript_payload).hexdigest(), "size_bytes": len(transcript_payload),
				"normalization": "exact-utf8",
			},
		}
		items.append(item)
		sidecar = next(value for value in source["sidecars"] if value["id"] == RIXVOX_METADATA_SIDECAR_ID)
		transforms.append({
			"item_id": item["id"], "source_id": source_id, "output_path": relative,
			"metadata_sidecar_sha256": sidecar["integrity"]["digest"],
			"nightly_selection_sha256": selection_sha256,
			"selection_basis": "privacy-hashed-speaker-reservoir-and-identifier-only-utterance-rank-v1",
			"utterance_rank_sha256": row["utterance_rank_sha256"], **transform,
		})
	return items


def _fsd50k_group_id(uploader: str) -> str:
	digest = hashlib.sha256(b"FSD50K-eval-uploader-v1\0" + uploader.encode("utf-8")).hexdigest()
	return f"fsd50k-uploader-{digest}"


def _read_fsd50k_metadata(metadata_path: Path, ground_truth_path: Path) -> tuple[Mapping[str, Mapping[str, Any]], Mapping[str, set[str]]]:
	with ArchiveReader(metadata_path) as reader:
		member = "FSD50K.metadata/eval_clips_info_FSD50K.json"
		_expect(member in reader.names, "FSD50K metadata", "eval clip metadata is missing")
		try:
			metadata = json.loads(reader.read(member).decode("utf-8"))
		except (UnicodeError, json.JSONDecodeError) as error:
			raise BuildError("FSD50K metadata: invalid JSON") from error
	_expect(isinstance(metadata, dict), "FSD50K metadata", "must be an object")
	with ArchiveReader(ground_truth_path) as reader:
		member = "FSD50K.ground_truth/eval.csv"
		_expect(member in reader.names, "FSD50K ground truth", "eval labels are missing")
		try:
			rows = csv.DictReader(io.StringIO(reader.read(member).decode("utf-8")))
			labels = {
				row["fname"]: set(row["labels"].split(","))
				for row in rows
			}
		except (UnicodeError, csv.Error, KeyError) as error:
			raise BuildError("FSD50K ground truth: invalid CSV") from error
	return metadata, labels


def _materialize_fsd50k(
	reader: SplitZipReader, metadata_path: Path, ground_truth_path: Path,
	selection: Mapping[str, Any], destination: Path, ffmpeg: Path, identity: Mapping[str, str],
	scratch: Path, source: Mapping[str, Any], selection_sha256: str,
	transforms: list[Mapping[str, Any]],
) -> list[Mapping[str, Any]]:
	metadata, labels = _read_fsd50k_metadata(metadata_path, ground_truth_path)
	items: list[Mapping[str, Any]] = []
	metadata_sidecar = next(value for value in source["sidecars"] if value["id"] == FSD50K_METADATA_SIDECAR_ID)
	ground_truth_sidecar = next(value for value in source["sidecars"] if value["id"] == FSD50K_GROUND_TRUTH_SIDECAR_ID)
	for clip in selection["clips"]:
		clip_id = clip["clip_id"]
		_expect(clip_id in metadata and clip_id in labels, clip_id, "selected FSD50K metadata is missing")
		info = metadata[clip_id]
		_expect(isinstance(info, dict), clip_id, "clip metadata must be an object")
		_expect(info.get("license") == selection["clip_license_required"], clip_id, "selected clip is not CC0")
		uploader = info.get("uploader")
		_expect(isinstance(uploader, str) and bool(uploader), clip_id, "uploader group is missing")
		_expect(_fsd50k_group_id(uploader) == clip["group_id"], clip_id, "privacy-hashed uploader group changed")
		_expect(clip["required_label"] in labels[clip_id], clip_id, "required direct label is absent")
		member = _resolve_selected_member(reader.names, clip["member"], clip_id)
		relative = f"audio/noise/fsd50k-{clip_id}.wav"
		common, transform = _convert_member(
			reader, member, destination / relative, ffmpeg, identity, scratch,
			minimum_duration_ms=MIN_AUDIO_MS,
		)  # type: ignore[arg-type]
		item = {
			"id": f"noise-fsd50k-{clip_id}", "kind": "noise", "source_id": source["id"],
			"relative_path": relative, "group_id": clip["group_id"], "noise_class": clip["noise_class"],
			"source_artifact_sha256": source["integrity"]["digest"], **common,
		}
		items.append(item)
		transforms.append({
			"item_id": item["id"], "source_id": source["id"], "output_path": relative,
			"clip_license": selection["clip_license_required"],
			"ground_truth_sidecar_sha256": ground_truth_sidecar["integrity"]["digest"],
			"metadata_sidecar_sha256": metadata_sidecar["integrity"]["digest"],
			"nightly_selection_sha256": selection_sha256,
			"required_direct_label": clip["required_label"],
			"selection_basis": "clip-level-cc0-direct-label-and-privacy-hashed-uploader-v1",
			**transform,
		})
	return items


NOISE_PATTERN = re.compile(r"RIRS_NOISES/real_rirs_isotropic_noises/RVB2014_type[12]_noise_([a-z0-9]+)_\d+\.wav$")
RIR_PATTERN = re.compile(r"RIRS_NOISES/simulated_rirs/(smallroom|mediumroom|largeroom)/(Room\d+)/(Room\d+)-(\d+)\.wav$")


def _materialize_noise_and_rirs(
	reader: ArchiveReader, destination: Path, ffmpeg: Path, identity: Mapping[str, str], scratch: Path,
	seed: str, source: Mapping[str, Any], transforms: list[Mapping[str, Any]],
) -> list[Mapping[str, Any]]:
	noise_members: dict[str, list[str]] = {}
	rir_members: dict[str, list[tuple[str, str, str, str]]] = {}
	for member in reader.names:
		noise_match = NOISE_PATTERN.fullmatch(member)
		if noise_match:
			group = f"slr28-noise-rvb2014-{noise_match.group(1)}"
			noise_members.setdefault(group, []).append(member)
		rir_match = RIR_PATTERN.fullmatch(member)
		if rir_match and rir_match.group(2) == rir_match.group(3):
			family, room, _, position = rir_match.groups()
			group = f"slr28-rir-{family}-{room.lower()}"
			rir_members.setdefault(group, []).append((member, family, room, position))
	noise_selection = _select_optional_groups(tuple(noise_members), "noise", seed, NOISE_GROUPS_PER_SPLIT)
	rir_selection = _select_groups(tuple(rir_members), "rir", seed, RIR_GROUPS_PER_SPLIT)
	items = []
	for group in sorted({value for groups in noise_selection.values() for value in groups}):
		accepted = False
		for member in sorted(noise_members[group]):
			name = PurePosixPath(member).stem.lower()
			relative = f"audio/noise/{name}.wav"
			common, transform = _convert_member(reader, member, destination / relative, ffmpeg, identity, scratch)
			if common["duration_samples"] < MIN_AUDIO_MS * 48:
				(destination / relative).unlink()
				continue
			item = {
				"id": f"noise-{name}", "kind": "noise", "source_id": ROOM_SOURCE_ID,
				"relative_path": relative, "group_id": group, "noise_class": "hvac",
				"source_artifact_sha256": source["integrity"]["digest"], **common,
			}
			items.append(item)
			transforms.append({
				"item_id": item["id"], "source_id": ROOM_SOURCE_ID, "output_path": relative,
				"curation_label": "hvac", "curation_basis": "OpenSLR28 isotropic room/office ambient-noise family", **transform,
			})
			accepted = True
			break
		_expect(accepted, group, f"has no noise member of at least {MIN_AUDIO_MS} ms")
	for group in sorted({value for groups in rir_selection.values() for value in groups}):
		member, family, room, position = sorted(rir_members[group])[0]
		name = PurePosixPath(member).stem.lower()
		relative = f"audio/rir/{family}-{name}.wav"
		common, transform = _convert_member(reader, member, destination / relative, ffmpeg, identity, scratch)
		item = {
			"id": f"rir-{family}-{name}", "kind": "rir", "source_id": ROOM_SOURCE_ID,
			"relative_path": relative, "group_id": group,
			"source_artifact_sha256": source["integrity"]["digest"], **common,
			"rir": {
				"rir_kind": "simulated", "room_id": f"{family}-{room.lower()}",
				"rt60_ms": _estimate_rt60_ms(destination / relative),
				"source_position_id": f"source-{position}", "receiver_position_id": f"receiver-{room.lower()}",
			},
		}
		items.append(item)
		transforms.append({"item_id": item["id"], "source_id": ROOM_SOURCE_ID, "output_path": relative, "rt60_estimator": "schroeder-minus5-minus35-v1", **transform})
	return items


def _materialize_demand_noise(
	archives: Mapping[str, Path], destination: Path, ffmpeg: Path, identity: Mapping[str, str],
	scratch: Path, seed: str, sources: Mapping[str, Mapping[str, Any]],
	transforms: list[Mapping[str, Any]],
) -> tuple[list[Mapping[str, Any]], Mapping[str, Any]]:
	items: list[Mapping[str, Any]] = []
	preparation = []
	for spec in DEMAND_SOURCE_SPECS:
		source_id = str(spec["source_id"])
		member = str(spec["member"])
		group = str(spec["group_id"])
		assigned_split = INVENTORY.assigned_split(seed, "noise", group)
		_expect(assigned_split == spec["split"], group, f"expected {spec['split']} split, got {assigned_split}")
		source = sources[source_id]
		source_rate = source["audio"]["sample_rate_hz"]
		_expect(source["audio"]["channels"] == 1 and source["audio"]["file_count"] == 16, source_id, "expected 16 separate mono DEMAND channel files")
		scene = PurePosixPath(member).parts[0]
		expected_members = {f"{scene}/ch{index:02d}.wav" for index in range(1, 17)}
		with ArchiveReader(archives[source_id]) as reader:
			actual_wavs = {name for name in reader.names if name.lower().endswith(".wav")}
			_expect(actual_wavs == expected_members, source_id, "archive members do not match the pinned 16-channel DEMAND layout")
			relative = f"audio/noise/{source_id}-ch01-60s.wav"
			common, transform = _convert_member_segment(
				reader, member, destination / relative, ffmpeg, identity, scratch,
				source_sample_rate_hz=source_rate,
				start_ms=DEMAND_SEGMENT_START_MS, duration_ms=DEMAND_SEGMENT_DURATION_MS,
			)
		item = {
			"id": f"noise-{source_id}-ch01", "kind": "noise", "source_id": source_id,
			"relative_path": relative, "group_id": group, "noise_class": spec["noise_class"],
			"source_artifact_sha256": source["integrity"]["digest"], **common,
		}
		items.append(item)
		transforms.append({
			"item_id": item["id"], "source_id": source_id, "output_path": relative,
			"assigned_split": assigned_split, "curation_label": spec["noise_class"],
			"curation_basis": "DEMAND scene identity mapped by tracked release policy; no audio or model score used",
			"holdout_preparation": (
				"blind-deterministic-conversion-only-no-listening-rendering-scoring"
				if assigned_split == "holdout" else "not-holdout"
			),
			**transform,
		})
		preparation.append({
			"source_id": source_id, "group_id": group, "assigned_split": assigned_split,
			"source_member": member, "segment_start_ms": DEMAND_SEGMENT_START_MS,
			"segment_duration_ms": DEMAND_SEGMENT_DURATION_MS,
			"selection_uses_audio_or_model_scores": False,
		})
	return items, {
		"policy": "holdout archives may be blindly converted and hashed, but must not be listened to, mixture-rendered, scored, or used for recipe decisions before final qualification",
		"recordings": preparation,
	}


def _load_response_definition() -> tuple[Mapping[str, Any], str]:
	path = INVENTORY.MODELED_RESPONSE_DEFINITION
	value = _load_json(path)
	_expect(isinstance(value, dict), str(path), "must be an object")
	_expect(set(value) == {"description", "families", "frame_samples", "generator", "sample_rate_hz", "schema_version", "variants_per_family"}, str(path), "unexpected keys")
	_expect(value["schema_version"] == 1 and value["generator"] == "fir-q15-v1", str(path), "unsupported definition")
	_expect(value["sample_rate_hz"] == 48000 and value["frame_samples"] >= 64, str(path), "unsupported format")
	_expect(isinstance(value["variants_per_family"], int) and value["variants_per_family"] >= 4, str(path), "too few variants")
	families = value["families"]
	_expect(isinstance(families, list) and len(families) == 4, str(path), "requires four device families")
	for family in families:
		_expect(set(family) == {"base_taps_q15", "device_family"}, str(path), "unexpected family keys")
		_expect(family["device_family"] in ("headset", "laptop", "usb", "phone"), str(path), "unknown device family")
		_expect(isinstance(family["base_taps_q15"], list) and family["base_taps_q15"], str(path), "missing taps")
		_expect(all(isinstance(tap, int) and -32768 <= tap <= 32767 for tap in family["base_taps_q15"]), str(path), "invalid Q15 taps")
	return value, _file_sha256(path)


def _response_samples(base_taps: Sequence[int], variant: int, frame_samples: int) -> list[int]:
	samples = [0] * frame_samples
	stride = 2 + variant % 3
	for index, base in enumerate(base_taps):
		position = 0 if index == 0 else index * stride + (variant // 3 if index >= 2 else 0)
		adjusted = int(round(base * (30000 + ((variant * 521 + index * 97) % 2200)) / 32768.0))
		samples[min(position, frame_samples - 1)] = max(-32768, min(32767, adjusted))
	return samples


def _write_pcm16_wav(path: Path, samples: Sequence[int]) -> None:
	path.parent.mkdir(parents=True, exist_ok=True)
	values = array.array("h", samples)
	if sys.byteorder != "little":
		values.byteswap()
	with wave.open(str(path), "wb") as stream:
		stream.setnchannels(1)
		stream.setsampwidth(2)
		stream.setframerate(48000)
		stream.writeframes(values.tobytes())


def _materialize_responses(destination: Path, seed: str, transforms: list[Mapping[str, Any]]) -> list[Mapping[str, Any]]:
	definition, definition_sha = _load_response_definition()
	candidates = []
	family_for_group: dict[str, str] = {}
	definition_for_group: dict[str, tuple[Mapping[str, Any], int]] = {}
	for family in definition["families"]:
		for variant in range(definition["variants_per_family"]):
			group = f"modeled-{family['device_family']}-{variant:02d}"
			candidates.append(group)
			family_for_group[group] = family["device_family"]
			definition_for_group[group] = (family, variant)
	selection = _select_groups(
		candidates, "microphone_response", seed, DEVICE_GROUPS_PER_SPLIT,
		family_for_group=family_for_group, minimum_families=4,
	)
	items = []
	for group in sorted({value for groups in selection.values() for value in groups}):
		family, variant = definition_for_group[group]
		relative = f"audio/microphone-response/{group}.wav"
		path = destination / relative
		samples = _response_samples(family["base_taps_q15"], variant, definition["frame_samples"])
		_write_pcm16_wav(path, samples)
		parameters = {
			"definition_sha256": definition_sha, "device_family": family["device_family"],
			"generator": definition["generator"], "variant": variant,
		}
		parameters_sha = _canonical_sha256(parameters)
		item = {
			"id": f"response-{group}", "kind": "microphone_response",
			"source_id": INVENTORY.MODELED_RESPONSE_SOURCE_ID, "relative_path": relative, "group_id": group,
			"sample_rate_hz": 48000, "channels": 1, "duration_samples": definition["frame_samples"],
			"sha256": _file_sha256(path), "size_bytes": path.stat().st_size,
			"source_artifact_sha256": definition_sha,
			"provenance": {
				"derivation": "synthesized", "parent_sha256": definition_sha, "parameters_sha256": parameters_sha,
				"source_path": INVENTORY.MODELED_RESPONSE_DEFINITION.name,
				"tool": "mumble-corpus-builder", "tool_version": SCRIPT_VERSION,
			},
			"microphone_response": {
				"response_kind": "modeled", "device_family": family["device_family"],
				"device_id": group, "calibration_id": f"fir-q15-v1-{variant:02d}",
			},
		}
		items.append(item)
		transforms.append({
			"item_id": item["id"], "source_id": INVENTORY.MODELED_RESPONSE_SOURCE_ID,
			"source_member": INVENTORY.MODELED_RESPONSE_DEFINITION.name,
			"source_member_sha256": definition_sha, "output_path": relative,
			"output_sha256": item["sha256"], "parameters": parameters, "parameters_sha256": parameters_sha,
		})
	return items


def build(
	manifest_path: Path, state_path: Path, artifact_root: Path, output: Path,
	ffmpeg: Path, ffmpeg_sha256: str, split_seed: str, suite: str = "master_quality",
) -> Mapping[str, Any]:
	_expect(not output.exists(), str(output), "refusing to overwrite an existing output root")
	_expect(suite in ("master_quality", "nightly"), "suite", "builder supports master_quality or nightly")
	manifest = LOCK.load_validated_manifest(manifest_path)
	required_source_ids = NIGHTLY_SOURCE_IDS if suite == "nightly" else REQUIRED_SOURCE_IDS
	archives, state_sha = _validate_state(state_path, manifest, artifact_root, required_source_ids)
	identity = _ffmpeg_identity(ffmpeg, ffmpeg_sha256)
	by_id = {source["id"]: source for source in manifest["sources"]}
	nightly_selection: Mapping[str, Any] | None = None
	nightly_selection_sha256: str | None = None
	if suite == "nightly":
		nightly_selection, nightly_selection_sha256 = _load_nightly_selection()
		_expect(split_seed == nightly_selection["split_seed"], "split_seed", "nightly selection is frozen to its audited seed")
	nightly_materialized_splits = ("tuning", "validation")
	output.parent.mkdir(parents=True, exist_ok=True)
	temporary = Path(tempfile.mkdtemp(prefix=f".{output.name}.", dir=output.parent))
	transforms: list[Mapping[str, Any]] = []
	try:
		scratch = temporary / ".scratch"
		scratch.mkdir()
		items: list[Mapping[str, Any]] = []
		with ArchiveReader(archives[SPEECH_SOURCE_ID]) as speech_reader:
			items.extend(_materialize_speech(speech_reader, temporary, ffmpeg, identity, scratch, split_seed, by_id[SPEECH_SOURCE_ID], transforms))
		if nightly_selection is not None:
			openslr_speakers = {
				split: nightly_selection["openslr12_test_clean_speakers"][split]
				for split in nightly_materialized_splits
			}
			with ArchiveReader(archives[NIGHTLY_SPEECH_SOURCE_ID]) as nightly_speech_reader:
				nightly_speech_items = _materialize_selected_librispeech(
					nightly_speech_reader, temporary, ffmpeg, identity, scratch,
					by_id[NIGHTLY_SPEECH_SOURCE_ID], openslr_speakers,
					split_seed, transforms,
				)
			base_speech_groups = {item["group_id"] for item in items if item["kind"] == "speech"}
			_expect(
				base_speech_groups.isdisjoint({item["group_id"] for item in nightly_speech_items}),
				"nightly LibriSpeech", "speaker overlaps the existing mini LibriSpeech source",
			)
			items.extend(nightly_speech_items)
			assert nightly_selection_sha256 is not None
			for source_id in RIXVOX_SOURCE_IDS:
				rows = [
					row for row in nightly_selection["rixvox"]["utterances"]
					if row["source_id"] == source_id
					and INVENTORY.assigned_split(split_seed, "speech", row["group_id"]) in nightly_materialized_splits
				]
				with ArchiveReader(archives[source_id]) as rixvox_reader:
					items.extend(_materialize_rixvox(
						{source_id: rixvox_reader}, rows, temporary, ffmpeg, identity, scratch,
						by_id, nightly_selection_sha256, transforms,
					))
		with ArchiveReader(archives[SWEDISH_SPEECH_SOURCE_ID]) as swedish_reader:
			swedish_items, swedish_selection = _materialize_fleurs_swedish(
				swedish_reader,
				archives[f"{SWEDISH_SPEECH_SOURCE_ID}:{SWEDISH_TRANSCRIPT_SIDECAR_ID}"],
				temporary, ffmpeg, identity, scratch, split_seed, by_id[SWEDISH_SPEECH_SOURCE_ID], transforms,
			)
			items.extend(swedish_items)
		with ArchiveReader(archives[ROOM_SOURCE_ID]) as room_reader:
			items.extend(_materialize_noise_and_rirs(room_reader, temporary, ffmpeg, identity, scratch, split_seed, by_id[ROOM_SOURCE_ID], transforms))
		demand_items, demand_preparation = _materialize_demand_noise(
			archives, temporary, ffmpeg, identity, scratch, split_seed, by_id, transforms,
		)
		items.extend(demand_items)
		if nightly_selection is not None:
			assert nightly_selection_sha256 is not None
			fsd_selection = {
				**nightly_selection["fsd50k"],
				"clips": [
					clip for clip in nightly_selection["fsd50k"]["clips"]
					if INVENTORY.assigned_split(split_seed, "noise", clip["group_id"]) in nightly_materialized_splits
				],
			}
			with SplitZipReader((
				archives[f"{FSD50K_SOURCE_ID}:{FSD50K_FIRST_VOLUME_SIDECAR_ID}"],
				archives[FSD50K_SOURCE_ID],
			)) as fsd_reader:
				items.extend(_materialize_fsd50k(
					fsd_reader,
					archives[f"{FSD50K_SOURCE_ID}:{FSD50K_METADATA_SIDECAR_ID}"],
					archives[f"{FSD50K_SOURCE_ID}:{FSD50K_GROUND_TRUTH_SIDECAR_ID}"],
					fsd_selection, temporary, ffmpeg, identity, scratch,
					by_id[FSD50K_SOURCE_ID], nightly_selection_sha256, transforms,
				))
		# Opening McGill is intentional: it verifies archive structure while release
		# materialization remains fail-closed until a transcript source is pinned.
		with ArchiveReader(archives["mcgill-tsp-speech-v2-48k"]) as mcgill_reader:
			mcgill_wavs = sum(1 for name in mcgill_reader.names if name.lower().endswith(".wav"))
			mcgill_transcripts = sum(1 for name in mcgill_reader.names if "transcript" in name.lower())
		items.extend(_materialize_responses(temporary, split_seed, transforms))
		items = sorted(items, key=lambda item: item["id"])
		for item in items:
			if item["kind"] in ("speech", "noise"):
				minimum_samples = math.ceil(MIN_AUDIO_MS * item["sample_rate_hz"] / 1000)
				_expect(
					item["duration_samples"] >= minimum_samples, item["id"],
					f"materialized {item['kind']} is shorter than {MIN_AUDIO_MS} ms",
				)
		transformation_manifest = {
			"schema_version": 1, "generator": "mumble-corpus-builder", "generator_version": SCRIPT_VERSION,
			"corpus_lock_sha256": LOCK.canonical_manifest_sha256(manifest), "corpus_state_sha256": state_sha,
			"split_seed": split_seed, "split_algorithm": "sha256-v1 by kind/group: tuning=0..59, validation=60..79, holdout=80..99",
			"split_seed_selection_basis": "identifier-only search over frozen speech/noise/RIR/device group IDs for the master-quality diversity floor and a 6/6/6 DEMAND scene assignment; no audio, metric, model output, or recipe result used",
			"qualification_suite": suite,
			"nightly_selection_sha256": nightly_selection_sha256,
			"nightly_materialized_splits": list(nightly_materialized_splits) if nightly_selection is not None else None,
			"nightly_sealed_splits": ["holdout"] if nightly_selection is not None else None,
			"ffmpeg": identity,
			"sources": [
				{
					"source_id": source_id, "source_artifact_sha256": by_id[source_id]["integrity"]["digest"],
					"selected_item_count": sum(1 for item in items if item["source_id"] == source_id),
					"disposition": (
						"verified-not-materialized-missing-transcripts"
						if source_id == "mcgill-tsp-speech-v2-48k" else "materialized-local-evaluation"
					),
				}
				for source_id in required_source_ids
			],
			"mcgill_archive_observation": {"wav_members": mcgill_wavs, "transcript_named_members": mcgill_transcripts},
			"fleurs_swedish_selection": swedish_selection,
			"demand_preparation": demand_preparation,
			"transforms": sorted(transforms, key=lambda value: value["item_id"]),
		}
		transform_payload = _write_json(temporary / "transformation-manifest.json", transformation_manifest)
		inventory: dict[str, Any] = {
			"schema_version": 3,
			"inventory_id": "mumble-community-nightly-v3" if suite == "nightly" else "mumble-community-release-v3",
			"eligibility": "nightly-partial" if suite == "nightly" else "release",
			"corpus_lock_sha256": LOCK.canonical_manifest_sha256(manifest),
			"provenance": {
				"generator": "mumble-corpus-builder", "generator_version": SCRIPT_VERSION,
				"generated_from_state_sha256": state_sha,
				"transformation_manifest_sha256": hashlib.sha256(transform_payload).hexdigest(),
			},
			"items": items,
		}
		if suite == "nightly":
			assert nightly_selection_sha256 is not None
			inventory["selection_sha256"] = nightly_selection_sha256
			inventory["sealed_splits"] = ["holdout"]
		try:
			release_eligible = suite != "nightly"
			validated = INVENTORY.validate_inventory(inventory, manifest, require_release=release_eligible)
			summary = INVENTORY.validate_diversity(
				validated, suite, split_seed,
				required_splits=INVENTORY.SPLITS if release_eligible else nightly_materialized_splits,
			)
		except INVENTORY.InventoryError as error:
			raise BuildError(str(error)) from error
		_write_json(temporary / "inventory-v3.json", inventory)
		_write_json(temporary / "split-summary.json", {"schema_version": 1, "split_seed": split_seed, "suite": suite, "splits": summary})
		shutil.rmtree(scratch)
		os.replace(temporary, output)
		return {"inventory": inventory, "diversity": summary, "output": str(output)}
	except Exception:
		shutil.rmtree(temporary, ignore_errors=True)
		raise


def _write_split_zip_fixture(first: Path, last: Path) -> tuple[str, bytes]:
	name = "fixture/data.bin"
	name_bytes = name.encode("utf-8")
	payload = bytes(range(100))
	crc = binascii.crc32(payload) & 0xFFFFFFFF
	local = struct.pack(
		"<4s5H3I2H", b"PK\x03\x04", 20, 0x800, 0, 0, 0, crc,
		len(payload), len(payload), len(name_bytes), 0,
	) + name_bytes + payload
	split_at = 30 + len(name_bytes) + 17
	first.write_bytes(local[:split_at])
	second_prefix = local[split_at:]
	central = struct.pack(
		"<4s6H3I5H2I", b"PK\x01\x02", 20, 20, 0x800, 0, 0, 0, crc,
		len(payload), len(payload), len(name_bytes), 0, 0, 0, 0, 0, 0,
	) + name_bytes
	eocd = struct.pack(
		"<4s4H2IH", b"PK\x05\x06", 1, 1, 1, 1, len(central), len(second_prefix), 0,
	)
	last.write_bytes(second_prefix + central + eocd)
	return name, payload


def run_self_test() -> None:
	selection, selection_sha = _load_nightly_selection()
	if not re.fullmatch(r"[0-9a-f]{64}", selection_sha) or selection["split_seed"] != COMMUNITY_RELEASE_SPLIT_SEED:
		raise AssertionError("nightly corpus selection was not hash-bound")
	demand_split_counts = {split: 0 for split in INVENTORY.SPLITS}
	for spec in DEMAND_SOURCE_SPECS:
		actual = INVENTORY.assigned_split(COMMUNITY_RELEASE_SPLIT_SEED, "noise", spec["group_id"])
		if actual != spec["split"]:
			raise AssertionError(f"{spec['group_id']} moved from {spec['split']} to {actual}")
		demand_split_counts[actual] += 1
	if demand_split_counts != {"tuning": 6, "validation": 6, "holdout": 6}:
		raise AssertionError(f"DEMAND split regression: {demand_split_counts}")
	if len({spec["source_id"] for spec in DEMAND_SOURCE_SPECS}) != 18:
		raise AssertionError("DEMAND source specifications must be unique")
	for unsafe in ("../escape.wav", "/absolute.wav", "a\\b.wav", "a/./b.wav"):
		try:
			_safe_member(unsafe)
		except BuildError:
			pass
		else:
			raise AssertionError(f"unsafe archive member was accepted: {unsafe}")
	with tempfile.TemporaryDirectory(prefix="mumble-corpus-builder-") as directory:
		root = Path(directory)
		first_volume = root / "fixture.z01"
		last_volume = root / "fixture.zip"
		fixture_member, fixture_payload = _write_split_zip_fixture(first_volume, last_volume)
		with SplitZipReader((first_volume, last_volume)) as split_reader:
			if split_reader.names != (fixture_member,) or split_reader.read(fixture_member) != fixture_payload:
				raise AssertionError("split ZIP reader failed its cross-volume fixture")
		unsafe_zip = root / "unsafe.zip"
		with zipfile.ZipFile(unsafe_zip, "w") as archive:
			archive.writestr("../escape.wav", b"x")
		try:
			with ArchiveReader(unsafe_zip):
				pass
		except BuildError:
			pass
		else:
			raise AssertionError("archive traversal member was accepted")
		if _parse_librispeech_fixture() != {"123": [("LibriSpeech/dev-clean-2/123/1/123-1-0000.flac", "HELLO WORLD")]}:
			raise AssertionError("LibriSpeech transcript parser fixture changed")
		fleurs_tsv = root / "fleurs.tsv"
		fleurs_rows = [
			("768148388401912982.wav", 120960),
			("14124189480263995347.wav", 129600),
			("18308813963978454955.wav", 151680),
		]
		fleurs_tsv.write_text("".join(
			f"5\t{filename}\tSvenskt test.\tsvenskt test\ts v e n s k t | t e s t |\t{samples}\tMALE\n"
			for filename, samples in fleurs_rows
		), encoding="utf-8")
		parsed_fleurs = _parse_fleurs_swedish_tsv(fleurs_tsv)
		if set(parsed_fleurs) != {"5"} or len(parsed_fleurs["5"]) != 3:
			raise AssertionError("FLEURS TSV parser fixture changed")
		fleurs_groups = {_fleurs_speaker_group("5", row["filename"]) for row in parsed_fleurs["5"]}
		if len(fleurs_groups) != 3:
			raise AssertionError("FLEURS recording identifiers did not remain independent groups")
		fmt = (
			(3).to_bytes(2, "little") + (1).to_bytes(2, "little") + (16000).to_bytes(4, "little")
			+ (64000).to_bytes(4, "little") + (4).to_bytes(2, "little") + (32).to_bytes(2, "little")
		)
		data = b"\0" * 40
		body = b"WAVE" + b"fmt " + len(fmt).to_bytes(4, "little") + fmt + b"data" + len(data).to_bytes(4, "little") + data
		float_wav = b"RIFF" + len(body).to_bytes(4, "little") + body
		if _fleurs_source_wav_info(float_wav, "self-test") != (16000, 1, 10):
			raise AssertionError("FLEURS float32 WAV parser fixture changed")
		definition, _ = _load_response_definition()
		response = root / "response.wav"
		_write_pcm16_wav(response, _response_samples(definition["families"][0]["base_taps_q15"], 3, definition["frame_samples"]))
		_wav_info(response)
		short_noise = root / "short-noise.wav"
		short_cycle = [index - 240 for index in range(480)]
		_write_pcm16_wav(short_noise, short_cycle)
		if not _repeat_pcm16_wav_to_minimum(short_noise, 1000):
			raise AssertionError("short noise fixture was not repeated")
		if _wav_info(short_noise)[3] != 48000:
			raise AssertionError("repeated noise fixture has the wrong fixed duration")
		first_hash = _file_sha256(short_noise)
		if _repeat_pcm16_wav_to_minimum(short_noise, 1000) or _file_sha256(short_noise) != first_hash:
			raise AssertionError("minimum-duration noise normalization is not idempotent")
		with wave.open(str(short_noise), "rb") as stream:
			first_two_cycles = stream.readframes(960)
		if first_two_cycles[:960] != first_two_cycles[960:]:
			raise AssertionError("noise repetition did not preserve the exact source cycle")
	device_groups = []
	device_families = {}
	for family in definition["families"]:
		for variant in range(definition["variants_per_family"]):
			group = f"modeled-{family['device_family']}-{variant:02d}"
			device_groups.append(group)
			device_families[group] = family["device_family"]
	_select_groups(
		device_groups, "microphone_response", COMMUNITY_RELEASE_SPLIT_SEED,
		DEVICE_GROUPS_PER_SPLIT, family_for_group=device_families, minimum_families=4,
	)
	groups = []
	index = 0
	while any(sum(1 for group in groups if INVENTORY.assigned_split(INVENTORY.DEFAULT_SPLIT_SEED, "speech", group) == split) < 2 for split in INVENTORY.SPLITS):
		groups.append(f"self-test-speaker-{index:03d}")
		index += 1
	selection = _select_groups(groups, "speech", INVENTORY.DEFAULT_SPLIT_SEED, 2)
	if any(len(values) < 2 for values in selection.values()):
		raise AssertionError("split-aware group selection did not cover every split")


class _FixtureReader:
	def __init__(self) -> None:
		self.names = (
			"LibriSpeech/dev-clean-2/123/1/123-1-0000.flac",
			"LibriSpeech/dev-clean-2/123/1/123-1.trans.txt",
		)
	def read(self, name: str) -> bytes:
		if name.endswith(".trans.txt"):
			return b"123-1-0000 HELLO WORLD\n"
		return b"not-decoded-in-self-test"


def _parse_librispeech_fixture() -> Mapping[str, list[tuple[str, str]]]:
	return _parse_librispeech(_FixtureReader())  # type: ignore[arg-type]


def main(argv: Sequence[str] | None = None) -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--manifest", type=Path, default=Path(__file__).with_name("corpus-lock.json"))
	parser.add_argument("--corpus-state", type=Path)
	parser.add_argument("--artifact-root", type=Path)
	parser.add_argument("--output", type=Path)
	parser.add_argument("--ffmpeg", type=Path)
	parser.add_argument("--ffmpeg-sha256")
	parser.add_argument("--split-seed", default=COMMUNITY_RELEASE_SPLIT_SEED)
	parser.add_argument("--suite", choices=("master_quality", "nightly"), default="master_quality")
	parser.add_argument("--self-test", action="store_true")
	args = parser.parse_args(argv)
	try:
		if args.self_test:
			run_self_test()
			print("corpus inventory builder self-test: ok")
			if all(value is None for value in (args.corpus_state, args.artifact_root, args.output, args.ffmpeg, args.ffmpeg_sha256)):
				return 0
		if any(value is None for value in (args.corpus_state, args.artifact_root, args.output, args.ffmpeg, args.ffmpeg_sha256)):
			raise BuildError("--corpus-state, --artifact-root, --output, --ffmpeg and --ffmpeg-sha256 are required")
		result = build(
			args.manifest, args.corpus_state, args.artifact_root, args.output,
			args.ffmpeg, args.ffmpeg_sha256, args.split_seed, args.suite,
		)
		print(f"corpus inventory builder: wrote {len(result['inventory']['items'])} items; output={args.output}")
		for split, summary in result["diversity"].items():
			print(f"  {split}: {json.dumps(summary, sort_keys=True)}")
		return 0
	except (AssertionError, BuildError, INVENTORY.InventoryError, LOCK.ValidationError) as error:
		print(f"corpus inventory builder: error: {error}", file=sys.stderr)
		return 1


if __name__ == "__main__":
	sys.exit(main())
