#!/usr/bin/env python3
"""Canonical file and directory identities shared by audio qualification tools."""

from __future__ import annotations

import hashlib
import json
import os
import stat
from pathlib import Path
from typing import Any, Mapping, Sequence


class PayloadIdentityError(ValueError):
	"""Raised when a protected payload cannot be hashed safely."""


def canonical_json_bytes(value: Any) -> bytes:
	try:
		return json.dumps(
			value, ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=False,
		).encode("utf-8")
	except (TypeError, ValueError) as error:
		raise PayloadIdentityError(f"value cannot be represented as finite canonical JSON: {error}") from error


def canonical_json_sha256(value: Any) -> str:
	return hashlib.sha256(canonical_json_bytes(value)).hexdigest()


def file_sha256(path: Path) -> str:
	digest = hashlib.sha256()
	try:
		with path.open("rb") as stream:
			for block in iter(lambda: stream.read(1024 * 1024), b""):
				digest.update(block)
	except OSError as error:
		raise PayloadIdentityError(f"unable to hash protected file {path}: {error}") from error
	return digest.hexdigest()


def is_reparse(path: Path) -> bool:
	try:
		metadata = os.lstat(path)
	except OSError as error:
		raise PayloadIdentityError(f"unable to inspect protected path {path}: {error}") from error
	attributes = getattr(metadata, "st_file_attributes", 0)
	reparse_flag = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
	return path.is_symlink() or bool(attributes & reparse_flag)


def _lexical_absolute(path: Path) -> Path:
	# Path.resolve() follows junctions/symlinks before they can be rejected.
	return Path(os.path.abspath(os.fspath(path)))


def _assert_no_reparse_components(path: Path) -> None:
	absolute = _lexical_absolute(path)
	current = Path(absolute.anchor)
	for part in absolute.parts[1:]:
		current /= part
		if is_reparse(current):
			raise PayloadIdentityError(f"protected provenance paths must not traverse reparse points: {current}")


def _stable_regular_file_hash(path: Path, before: os.stat_result | None = None) -> tuple[str, int]:
	try:
		initial = os.lstat(path) if before is None else before
	except OSError as error:
		raise PayloadIdentityError(f"unable to inspect protected file {path}: {error}") from error
	attributes = getattr(initial, "st_file_attributes", 0)
	reparse_flag = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
	if not stat.S_ISREG(initial.st_mode) or attributes & reparse_flag:
		raise PayloadIdentityError(f"protected provenance entry is not a regular non-reparse file: {path}")
	if initial.st_nlink != 1:
		raise PayloadIdentityError(
			f"protected provenance files must be independent snapshots, not hardlink aliases: {path} "
			f"(link count {initial.st_nlink})"
		)
	digest = hashlib.sha256()
	try:
		with path.open("rb") as stream:
			for block in iter(lambda: stream.read(1024 * 1024), b""):
				digest.update(block)
		final = os.lstat(path)
	except OSError as error:
		raise PayloadIdentityError(f"unable to hash protected file {path}: {error}") from error
	identity_before = (
		initial.st_dev, initial.st_ino, initial.st_size, initial.st_mtime_ns, initial.st_mode, initial.st_nlink,
	)
	identity_after = (
		final.st_dev, final.st_ino, final.st_size, final.st_mtime_ns, final.st_mode, final.st_nlink,
	)
	if identity_before != identity_after:
		raise PayloadIdentityError(f"protected provenance file changed while it was hashed: {path}")
	return digest.hexdigest(), initial.st_size


def payload_tree_records(path: Path) -> Sequence[Mapping[str, Any]]:
	"""Return the gate-compatible canonical inventory for a directory.

	The contract is intentionally exact: case-sensitive POSIX relative paths,
	case-sensitive path ordering, and records containing only ``path``,
	``sha256`` and ``size_bytes``.
	"""

	root = _lexical_absolute(path)
	_assert_no_reparse_components(root)
	try:
		root_stat = os.lstat(root)
	except OSError as error:
		raise PayloadIdentityError(f"protected provenance directory does not exist: {root}: {error}") from error
	if not stat.S_ISDIR(root_stat.st_mode):
		raise PayloadIdentityError(f"protected provenance directory does not exist: {root}")
	records: list[Mapping[str, Any]] = []
	stack = [root]
	seen_casefold: dict[str, str] = {}
	directory_snapshots: list[tuple[Path, tuple[int, int, int, int]]] = []
	while stack:
		directory = stack.pop()
		try:
			directory_before = os.lstat(directory)
		except OSError as error:
			raise PayloadIdentityError(f"unable to inspect protected directory {directory}: {error}") from error
		directory_attributes = getattr(directory_before, "st_file_attributes", 0)
		reparse_flag = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
		if not stat.S_ISDIR(directory_before.st_mode) or directory_attributes & reparse_flag or directory.is_symlink():
			raise PayloadIdentityError(f"protected provenance tree contains an unsafe directory: {directory}")
		try:
			with os.scandir(directory) as entries:
				children = list(entries)
		except OSError as error:
			raise PayloadIdentityError(f"unable to enumerate protected directory {directory}: {error}") from error
		for entry in children:
			candidate = Path(entry.path)
			try:
				metadata = entry.stat(follow_symlinks=False)
			except OSError as error:
				raise PayloadIdentityError(f"unable to inspect protected entry {candidate}: {error}") from error
			attributes = getattr(metadata, "st_file_attributes", 0)
			if entry.is_symlink() or attributes & reparse_flag:
				raise PayloadIdentityError(f"protected provenance trees must not contain reparse points: {candidate}")
			if stat.S_ISDIR(metadata.st_mode):
				stack.append(candidate)
				continue
			if not stat.S_ISREG(metadata.st_mode):
				raise PayloadIdentityError(f"protected provenance trees must contain only directories and regular files: {candidate}")
			relative = candidate.relative_to(root).as_posix()
			folded = relative.casefold()
			if folded in seen_casefold and seen_casefold[folded] != relative:
				raise PayloadIdentityError(
					f"protected provenance tree contains a case-insensitive path collision: "
					f"{seen_casefold[folded]!r} and {relative!r}"
				)
			seen_casefold[folded] = relative
			try:
				fresh_metadata = os.lstat(candidate)
			except OSError as error:
				raise PayloadIdentityError(f"unable to reinspect protected file {candidate}: {error}") from error
			digest, size = _stable_regular_file_hash(candidate, fresh_metadata)
			records.append({"path": relative, "sha256": digest, "size_bytes": size})
		try:
			directory_after = os.lstat(directory)
		except OSError as error:
			raise PayloadIdentityError(f"unable to reinspect protected directory {directory}: {error}") from error
		directory_identity_before = (
			directory_before.st_dev, directory_before.st_ino, directory_before.st_mtime_ns, directory_before.st_mode,
		)
		directory_identity_after = (
			directory_after.st_dev, directory_after.st_ino, directory_after.st_mtime_ns, directory_after.st_mode,
		)
		if directory_identity_before != directory_identity_after:
			raise PayloadIdentityError(f"protected provenance directory changed while it was enumerated: {directory}")
		directory_snapshots.append((directory, directory_identity_after))
	if not records:
		raise PayloadIdentityError(f"protected provenance directory contains no files: {root}")
	# A parent can change after its first enumeration while a child is being
	# hashed. Recheck every directory once the complete inventory is sealed.
	for directory, expected_identity in directory_snapshots:
		try:
			current = os.lstat(directory)
		except OSError as error:
			raise PayloadIdentityError(f"unable to finalize protected directory {directory}: {error}") from error
		current_identity = (current.st_dev, current.st_ino, current.st_mtime_ns, current.st_mode)
		if current_identity != expected_identity or is_reparse(directory):
			raise PayloadIdentityError(f"protected provenance directory changed while its tree was hashed: {directory}")
	return sorted(records, key=lambda record: str(record["path"]))


def payload_file_attestation(path: Path) -> Mapping[str, Any]:
	"""Return a stable identity for one independent, non-reparse file snapshot."""
	absolute = _lexical_absolute(path)
	_assert_no_reparse_components(absolute)
	try:
		metadata = os.lstat(absolute)
	except OSError as error:
		raise PayloadIdentityError(f"protected provenance file does not exist: {absolute}: {error}") from error
	digest, size = _stable_regular_file_hash(absolute, metadata)
	return {"path": str(absolute), "sha256": digest, "size_bytes": size}


def payload_sha256(path: Path) -> str:
	absolute = _lexical_absolute(path)
	_assert_no_reparse_components(absolute)
	try:
		metadata = os.lstat(absolute)
	except OSError as error:
		raise PayloadIdentityError(f"protected provenance path does not exist: {absolute}: {error}") from error
	if stat.S_ISREG(metadata.st_mode):
		return payload_file_attestation(absolute)["sha256"]
	if not stat.S_ISDIR(metadata.st_mode):
		raise PayloadIdentityError(f"protected provenance path is not a regular file or directory: {absolute}")
	return canonical_json_sha256(payload_tree_records(absolute))


def payload_tree_attestation(path: Path) -> Mapping[str, Any]:
	root = _lexical_absolute(path)
	records = payload_tree_records(root)
	return {
		"root": str(root),
		"file_count": len(records),
		"sha256": canonical_json_sha256(records),
	}
