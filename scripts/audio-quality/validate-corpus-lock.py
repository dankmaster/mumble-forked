#!/usr/bin/env python3
"""Validate the pinned input-enhancement corpus manifest without third-party packages."""

from __future__ import annotations

import argparse
import copy
import datetime as dt
import hashlib
import json
import re
import sys
import tempfile
from pathlib import Path, PurePosixPath
from typing import Any, Dict, Iterable, List, Mapping, MutableMapping, Sequence, Set


class ValidationError(ValueError):
	"""Raised when the lock file violates its schema or safety policy."""


KNOWN_ROLES = {
	"fullband_quality",
	"local_eval",
	"local_mixture_generation",
	"multilingual_regression",
	"narrowband_regression",
	"redistributable_fixture",
	"training_candidate",
}
KNOWN_SPDX = {
	"Apache-2.0",
	"BSD-2-Clause",
	"CC-BY-4.0",
	"CC-BY-NC-3.0",
	"CC-BY-SA-3.0",
	"CC0-1.0",
	"LicenseRef-DEMAND-Ambiguous",
}
LICENSE_STATUSES = { "verified", "ambiguous", "restricted" }
TRAINING_STATUSES = {
	"allowed_with_attribution",
	"blocked_ambiguous_license",
	"blocked_evaluation_only",
	"blocked_noncommercial",
}
REDISTRIBUTION_STATUSES = {
	"allowed_with_attribution",
	"blocked_ambiguous_license",
	"blocked_evaluation_only",
	"blocked_noncommercial",
	"source_license_review_required",
}
PURPOSES = ("local-eval", "training", "fixture")
SOURCE_REQUIRED_KEYS = {
	"audio",
	"id",
	"integrity",
	"kind",
	"landing_page",
	"license",
	"redistribution_status",
	"roles",
	"source_url",
	"training_status",
	"version",
}
SIDECAR_REQUIRED_KEYS = { "id", "integrity", "kind", "source_url" }
SIDECAR_KINDS = { "archive_part", "label_metadata", "license_metadata", "transcript_metadata" }
EXCLUDED_REQUIRED_KEYS = {
	"id",
	"integrity",
	"kind",
	"license",
	"reason",
	"redistribution_status",
	"roles",
	"source_url",
	"training_status",
}


def _expect(condition: bool, path: str, message: str) -> None:
	if not condition:
		raise ValidationError(f"{path}: {message}")


def _expect_mapping(value: Any, path: str) -> Mapping[str, Any]:
	_expect(isinstance(value, dict), path, "expected an object")
	return value


def _expect_exact_keys(value: Mapping[str, Any], required: Set[str], optional: Set[str], path: str) -> None:
	actual = set(value)
	missing = sorted(required - actual)
	unknown = sorted(actual - required - optional)
	_expect(not missing, path, f"missing keys: {', '.join(missing)}")
	_expect(not unknown, path, f"unknown keys: {', '.join(unknown)}")


def _expect_nonempty_string(value: Any, path: str) -> str:
	_expect(isinstance(value, str) and bool(value.strip()), path, "expected a non-empty string")
	return value


def _expect_https_url(value: Any, path: str) -> None:
	url = _expect_nonempty_string(value, path)
	_expect(url.startswith("https://"), path, "URL must use HTTPS")


def _expect_sorted_unique_strings(value: Any, allowed: Set[str], path: str) -> List[str]:
	_expect(isinstance(value, list) and bool(value), path, "expected a non-empty array")
	_expect(all(isinstance(item, str) for item in value), path, "all entries must be strings")
	_expect(value == sorted(set(value)), path, "entries must be unique and sorted")
	unknown = sorted(set(value) - allowed)
	_expect(not unknown, path, f"unknown entries: {', '.join(unknown)}")
	return value


def _safe_artifact_path(value: str, path: str) -> None:
	parsed = PurePosixPath(value)
	_expect(value == parsed.as_posix(), path, "must use normalized forward slashes")
	_expect(not parsed.is_absolute(), path, "must be relative")
	_expect(".." not in parsed.parts and "." not in parsed.parts, path, "must not escape the artifact root")


def _validate_integrity(value: Any, path: str) -> None:
	integrity = _expect_mapping(value, path)
	_expect_exact_keys(integrity, { "algorithm", "digest" }, { "artifact_path", "size_bytes" }, path)
	algorithm = _expect_nonempty_string(integrity["algorithm"], f"{path}.algorithm")
	digest = _expect_nonempty_string(integrity["digest"], f"{path}.digest")
	lengths = { "sha256": 64, "git-sha1": 40 }
	_expect(algorithm in lengths, f"{path}.algorithm", "must be sha256 or git-sha1")
	_expect(
		bool(re.fullmatch(rf"[0-9a-f]{{{lengths[algorithm]}}}", digest)),
		f"{path}.digest",
		f"must be a lowercase {lengths[algorithm]}-character hexadecimal digest",
	)
	if algorithm == "sha256":
		_expect("size_bytes" in integrity, path, "sha256 entries must pin size_bytes")
	if "size_bytes" in integrity:
		_expect(
			isinstance(integrity["size_bytes"], int) and not isinstance(integrity["size_bytes"], bool)
			and integrity["size_bytes"] > 0,
			f"{path}.size_bytes",
			"must be a positive integer",
		)
	if "artifact_path" in integrity:
		_expect(algorithm == "sha256", f"{path}.artifact_path", "local artifacts require sha256")
		artifact_path = _expect_nonempty_string(integrity["artifact_path"], f"{path}.artifact_path")
		_safe_artifact_path(artifact_path, f"{path}.artifact_path")


def _validate_license(value: Any, path: str) -> Mapping[str, Any]:
	license_info = _expect_mapping(value, path)
	_expect_exact_keys(license_info, { "evidence_url", "spdx", "status" }, { "notes" }, path)
	spdx = _expect_nonempty_string(license_info["spdx"], f"{path}.spdx")
	_expect(spdx in KNOWN_SPDX, f"{path}.spdx", "license is not in the reviewed SPDX allowlist")
	_expect(license_info["status"] in LICENSE_STATUSES, f"{path}.status", "unknown license status")
	_expect_https_url(license_info["evidence_url"], f"{path}.evidence_url")
	if "notes" in license_info:
		_expect_nonempty_string(license_info["notes"], f"{path}.notes")
	return license_info


def _validate_audio(value: Any, path: str) -> None:
	audio = _expect_mapping(value, path)
	_expect_exact_keys(
		audio,
		{ "sample_rate_hz" },
		{ "channel_counts", "channels", "file_count", "pair_count" },
		path,
	)
	_expect(
		("channels" in audio) != ("channel_counts" in audio),
		path,
		"must specify exactly one of channels or channel_counts",
	)
	if "channel_counts" in audio:
		counts = audio["channel_counts"]
		_expect(isinstance(counts, list) and bool(counts), f"{path}.channel_counts", "expected a non-empty array")
		_expect(
			all(isinstance(count, int) and not isinstance(count, bool) and count > 0 for count in counts),
			f"{path}.channel_counts",
			"all entries must be positive integers",
		)
		_expect(counts == sorted(set(counts)), f"{path}.channel_counts", "entries must be unique and sorted")
	for key in set(audio) - { "channel_counts" }:
		_expect(
			isinstance(audio[key], int) and not isinstance(audio[key], bool) and audio[key] > 0,
			f"{path}.{key}",
			"must be a positive integer",
		)


def _validate_sidecars(value: Any, path: str) -> None:
	_expect(isinstance(value, list) and bool(value), path, "expected a non-empty array")
	ids: List[str] = []
	for index, raw_sidecar in enumerate(value):
		sidecar_path = f"{path}[{index}]"
		sidecar = _expect_mapping(raw_sidecar, sidecar_path)
		_expect_exact_keys(sidecar, SIDECAR_REQUIRED_KEYS, set(), sidecar_path)
		sidecar_id = _expect_nonempty_string(sidecar["id"], f"{sidecar_path}.id")
		_expect(
			bool(re.fullmatch(r"[a-z0-9][a-z0-9-]*", sidecar_id)),
			f"{sidecar_path}.id",
			"must be a lowercase slug",
		)
		_expect(sidecar["kind"] in SIDECAR_KINDS, f"{sidecar_path}.kind", "unknown reviewed sidecar kind")
		_expect_https_url(sidecar["source_url"], f"{sidecar_path}.source_url")
		_validate_integrity(sidecar["integrity"], f"{sidecar_path}.integrity")
		_expect(
			"artifact_path" in sidecar["integrity"],
			f"{sidecar_path}.integrity",
			"sidecars must pin a downloadable local artifact",
		)
		ids.append(sidecar_id)
	_expect(ids == sorted(set(ids)), path, "sidecar ids must be unique and sorted")


def _validate_source(value: Any, path: str, allowed_roles: Set[str], excluded: bool) -> None:
	source = _expect_mapping(value, path)
	required = EXCLUDED_REQUIRED_KEYS if excluded else SOURCE_REQUIRED_KEYS
	_expect_exact_keys(source, required, { "notes", "sidecars" } if not excluded else { "notes" }, path)
	source_id = _expect_nonempty_string(source["id"], f"{path}.id")
	_expect(bool(re.fullmatch(r"[a-z0-9][a-z0-9-]*", source_id)), f"{path}.id", "must be a lowercase slug")
	_expect_nonempty_string(source["kind"], f"{path}.kind")
	_expect_https_url(source["source_url"], f"{path}.source_url")
	if not excluded:
		_expect_nonempty_string(source["version"], f"{path}.version")
		_expect_https_url(source["landing_page"], f"{path}.landing_page")
		_validate_audio(source["audio"], f"{path}.audio")
		if "sidecars" in source:
			_validate_sidecars(source["sidecars"], f"{path}.sidecars")
	else:
		_expect_nonempty_string(source["reason"], f"{path}.reason")
	_validate_integrity(source["integrity"], f"{path}.integrity")
	license_info = _validate_license(source["license"], f"{path}.license")
	roles = _expect_sorted_unique_strings(source["roles"], allowed_roles, f"{path}.roles")
	training_status = source["training_status"]
	redistribution_status = source["redistribution_status"]
	_expect(training_status in TRAINING_STATUSES, f"{path}.training_status", "unknown status")
	_expect(redistribution_status in REDISTRIBUTION_STATUSES, f"{path}.redistribution_status", "unknown status")

	restricted = (
		license_info["status"] != "verified"
		or "-NC-" in license_info["spdx"]
		or license_info["spdx"].startswith("LicenseRef-")
	)
	if restricted or excluded:
		_expect(training_status.startswith("blocked_"), f"{path}.training_status", "restricted sources must be blocked")
		_expect(
			redistribution_status.startswith("blocked_"),
			f"{path}.redistribution_status",
			"restricted sources must be blocked",
		)
		_expect("training_candidate" not in roles, f"{path}.roles", "restricted sources cannot train product models")
		_expect(
			"redistributable_fixture" not in roles,
			f"{path}.roles",
			"restricted sources cannot become redistributable fixtures",
		)
	if training_status == "allowed_with_attribution":
		_expect("training_candidate" in roles, f"{path}.roles", "allowed training requires training_candidate")
		_expect(license_info["status"] == "verified", f"{path}.license.status", "training requires a verified license")
	if "training_candidate" in roles:
		_expect(training_status == "allowed_with_attribution", f"{path}.training_status", "training role is inconsistent")
	if "redistributable_fixture" in roles:
		_expect(
			redistribution_status == "allowed_with_attribution",
			f"{path}.redistribution_status",
			"fixture role requires explicit redistribution approval",
		)


def validate_manifest(value: Any) -> Mapping[str, Any]:
	manifest = _expect_mapping(value, "root")
	_expect_exact_keys(
		manifest,
		{ "description", "excluded_sources", "policy", "schema_version", "sources", "verified_at" },
		set(),
		"root",
	)
	_expect(manifest["schema_version"] == 1, "root.schema_version", "unsupported schema version")
	_expect_nonempty_string(manifest["description"], "root.description")
	try:
		dt.date.fromisoformat(_expect_nonempty_string(manifest["verified_at"], "root.verified_at"))
	except ValueError as error:
		raise ValidationError("root.verified_at: expected ISO date YYYY-MM-DD") from error

	policy = _expect_mapping(manifest["policy"], "root.policy")
	_expect_exact_keys(
		policy,
		{
			"allowed_roles",
			"derived_audio_tracked_in_git",
			"downloaded_audio_tracked_in_git",
			"personal_audio_default",
			"unknown_or_ambiguous_license",
		},
		set(),
		"root.policy",
	)
	allowed_roles = set(_expect_sorted_unique_strings(policy["allowed_roles"], KNOWN_ROLES, "root.policy.allowed_roles"))
	_expect(policy["derived_audio_tracked_in_git"] is False, "root.policy.derived_audio_tracked_in_git", "must be false")
	_expect(policy["downloaded_audio_tracked_in_git"] is False, "root.policy.downloaded_audio_tracked_in_git", "must be false")
	_expect(
		policy["personal_audio_default"] == "local_only_delete_after_calibration",
		"root.policy.personal_audio_default",
		"unsafe personal-audio default",
	)
	_expect(
		policy["unknown_or_ambiguous_license"] == "blocked",
		"root.policy.unknown_or_ambiguous_license",
		"unknown licenses must default to blocked",
	)

	sources = manifest["sources"]
	excluded_sources = manifest["excluded_sources"]
	_expect(isinstance(sources, list) and bool(sources), "root.sources", "expected a non-empty array")
	_expect(isinstance(excluded_sources, list), "root.excluded_sources", "expected an array")
	for index, source in enumerate(sources):
		_validate_source(source, f"root.sources[{index}]", allowed_roles, excluded=False)
	for index, source in enumerate(excluded_sources):
		_validate_source(source, f"root.excluded_sources[{index}]", allowed_roles, excluded=True)

	source_ids = [source["id"] for source in sources]
	excluded_ids = [source["id"] for source in excluded_sources]
	_expect(source_ids == sorted(source_ids), "root.sources", "sources must be sorted by id")
	_expect(excluded_ids == sorted(excluded_ids), "root.excluded_sources", "sources must be sorted by id")
	all_ids = source_ids + excluded_ids
	_expect(len(all_ids) == len(set(all_ids)), "root", "source ids must be unique")
	artifact_owners: Dict[str, str] = {}
	for source in sources:
		descriptors = [("primary", source["integrity"])] + [
			(sidecar["id"], sidecar["integrity"]) for sidecar in source.get("sidecars", [])
		]
		for artifact_id, integrity in descriptors:
			if "artifact_path" not in integrity:
				continue
			artifact_path = integrity["artifact_path"]
			owner = f"{source['id']}:{artifact_id}"
			_expect(
				artifact_path not in artifact_owners,
				"root.sources",
				f"artifact_path {artifact_path!r} is shared by {artifact_owners.get(artifact_path)} and {owner}",
			)
			artifact_owners[artifact_path] = owner
	return manifest


def _load_json(path: Path) -> Any:
	def reject_duplicate_keys(pairs: Sequence[tuple[str, Any]]) -> MutableMapping[str, Any]:
		result: MutableMapping[str, Any] = {}
		for key, value in pairs:
			if key in result:
				raise ValidationError(f"duplicate JSON key: {key}")
			result[key] = value
		return result

	try:
		with path.open("r", encoding="utf-8") as stream:
			return json.load(stream, object_pairs_hook=reject_duplicate_keys)
	except (OSError, json.JSONDecodeError) as error:
		raise ValidationError(f"{path}: unable to load JSON: {error}") from error


def load_validated_manifest(path: Path) -> Mapping[str, Any]:
	"""Load and validate a corpus lock for the other tracked quality tools."""
	return validate_manifest(_load_json(path))


def source_policy_error(source: Mapping[str, Any], purpose: str) -> str | None:
	"""Return why a source is ineligible for a concrete artifact use.

	This is also used by artifact verification.  Merely having an artifact_path
	does not make a blocked or ambiguously licensed archive required evidence.
	"""
	_expect(purpose in PURPOSES, "purpose", f"must be one of {', '.join(PURPOSES)}")
	license_info = source["license"]
	if license_info["status"] != "verified" or str(license_info["spdx"]).startswith("LicenseRef-"):
		return f"license {license_info['spdx']} is not verified"
	if "-NC-" in license_info["spdx"]:
		return f"non-commercial license {license_info['spdx']} is blocked"
	if purpose == "local-eval" and "local_eval" not in source["roles"]:
		return "source is not approved for local evaluation"
	if purpose == "training" and source["training_status"] != "allowed_with_attribution":
		return f"training status is {source['training_status']}"
	if purpose == "fixture" and source["redistribution_status"] != "allowed_with_attribution":
		return f"redistribution status is {source['redistribution_status']}"
	if source["integrity"]["algorithm"] != "sha256" or "artifact_path" not in source["integrity"]:
		return "source is not represented by a pinned downloadable archive"
	return None


def _sha256_file(path: Path) -> str:
	digest = hashlib.sha256()
	with path.open("rb") as stream:
		for chunk in iter(lambda: stream.read(1024 * 1024), b""):
			digest.update(chunk)
	return digest.hexdigest()


def verify_artifacts(manifest: Mapping[str, Any], artifact_root: Path, purpose: str = "local-eval") -> int:
	verified = 0
	for source in manifest["sources"]:
		if source_policy_error(source, purpose) is not None:
			continue
		descriptors = [("primary", source["integrity"])] + [
			(sidecar["id"], sidecar["integrity"]) for sidecar in source.get("sidecars", [])
		]
		for artifact_id, integrity in descriptors:
			path = artifact_root.joinpath(*PurePosixPath(integrity["artifact_path"]).parts)
			label = f"{source['id']}:{artifact_id}"
			_expect(path.is_file(), label, f"artifact is missing: {path}")
			_expect(path.stat().st_size == integrity["size_bytes"], label, "artifact size does not match lock")
			_expect(_sha256_file(path) == integrity["digest"], label, "artifact sha256 does not match lock")
			verified += 1
	return verified


def canonical_manifest_sha256(manifest: Mapping[str, Any]) -> str:
	canonical = json.dumps(manifest, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")
	return hashlib.sha256(canonical).hexdigest()


def run_self_test() -> None:
	fixture: Dict[str, Any] = {
		"schema_version": 1,
		"verified_at": "2026-07-14",
		"description": "self test",
		"policy": {
			"allowed_roles": [ "local_eval", "training_candidate" ],
			"derived_audio_tracked_in_git": False,
			"downloaded_audio_tracked_in_git": False,
			"personal_audio_default": "local_only_delete_after_calibration",
			"unknown_or_ambiguous_license": "blocked",
		},
		"sources": [
			{
				"id": "safe-source",
				"kind": "clean_speech",
				"version": "1",
				"source_url": "https://example.invalid/source.wav",
				"landing_page": "https://example.invalid/",
				"integrity": { "algorithm": "sha256", "digest": "0" * 64, "size_bytes": 1 },
				"license": {
					"spdx": "BSD-2-Clause",
					"status": "verified",
					"evidence_url": "https://example.invalid/license",
				},
				"audio": { "channels": 1, "sample_rate_hz": 48000 },
				"roles": [ "local_eval", "training_candidate" ],
				"training_status": "allowed_with_attribution",
				"redistribution_status": "source_license_review_required",
			}
		],
		"excluded_sources": [],
	}
	validate_manifest(fixture)
	multi_channel = copy.deepcopy(fixture)
	multi_channel["sources"][0]["audio"] = {
		"channel_counts": [ 1, 2, 8 ],
		"file_count": 3,
		"sample_rate_hz": 16000,
	}
	validate_manifest(multi_channel)
	bad_channel_counts = copy.deepcopy(multi_channel)
	bad_channel_counts["sources"][0]["audio"]["channel_counts"] = [ 2, 1, 2 ]
	try:
		validate_manifest(bad_channel_counts)
	except ValidationError:
		pass
	else:
		raise AssertionError("self-test accepted duplicate or unsorted channel counts")

	unsafe = copy.deepcopy(fixture)
	unsafe["sources"][0]["license"] = {
		"spdx": "CC-BY-NC-3.0",
		"status": "restricted",
		"evidence_url": "https://example.invalid/license",
	}
	try:
		validate_manifest(unsafe)
	except ValidationError:
		pass
	else:
		raise AssertionError("self-test failed to block non-commercial training data")

	bad_hash = copy.deepcopy(fixture)
	bad_hash["sources"][0]["integrity"]["digest"] = "A" * 64
	try:
		validate_manifest(bad_hash)
	except ValidationError:
		pass
	else:
		raise AssertionError("self-test accepted a non-canonical hash")

	policy_fixture = copy.deepcopy(fixture)
	safe = policy_fixture["sources"][0]
	safe["integrity"] = {
		"algorithm": "sha256", "artifact_path": "downloads/safe.bin",
		"digest": hashlib.sha256(b"x").hexdigest(), "size_bytes": 1,
	}
	safe["sidecars"] = [
		{
			"id": "transcripts", "kind": "transcript_metadata",
			"source_url": "https://example.invalid/transcripts.tsv",
			"integrity": {
				"algorithm": "sha256", "artifact_path": "downloads/transcripts.tsv",
				"digest": hashlib.sha256(b"metadata").hexdigest(), "size_bytes": 8,
			},
		}
	]
	bad_sidecar_kind = copy.deepcopy(policy_fixture)
	bad_sidecar_kind["sources"][0]["sidecars"][0]["kind"] = "unreviewed_metadata"
	try:
		validate_manifest(bad_sidecar_kind)
	except ValidationError:
		pass
	else:
		raise AssertionError("self-test accepted an unknown sidecar kind")
	blocked = copy.deepcopy(safe)
	blocked.pop("sidecars")
	blocked["id"] = "blocked-source"
	blocked["integrity"] = {
		"algorithm": "sha256", "artifact_path": "downloads/blocked.bin",
		"digest": hashlib.sha256(b"missing").hexdigest(), "size_bytes": 7,
	}
	blocked["license"] = {
		"spdx": "LicenseRef-DEMAND-Ambiguous", "status": "ambiguous",
		"evidence_url": "https://example.invalid/ambiguous-license",
	}
	blocked["training_status"] = "blocked_ambiguous_license"
	blocked["redistribution_status"] = "blocked_ambiguous_license"
	blocked["roles"] = [ "local_eval" ]
	policy_fixture["sources"] = [ blocked, safe ]
	validate_manifest(policy_fixture)
	with tempfile.TemporaryDirectory(prefix="mumble-corpus-lock-") as directory:
		root = Path(directory)
		(root / "downloads").mkdir()
		(root / "downloads" / "safe.bin").write_bytes(b"x")
		(root / "downloads" / "transcripts.tsv").write_bytes(b"metadata")
		if verify_artifacts(policy_fixture, root, "local-eval") != 2:
			raise AssertionError("artifact verification did not select the policy-eligible primary and sidecar")


def main(argv: Sequence[str] | None = None) -> int:
	default_manifest = Path(__file__).with_name("corpus-lock.json")
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("manifest", nargs="?", type=Path, default=default_manifest)
	parser.add_argument("--artifact-root", type=Path, help="also verify pinned local archives below this directory")
	parser.add_argument("--purpose", choices=PURPOSES, default="local-eval", help="verify only archives eligible for this use")
	parser.add_argument("--self-test", action="store_true", help="run built-in policy regression tests first")
	args = parser.parse_args(argv)

	try:
		if args.self_test:
			run_self_test()
			print("corpus-lock validator self-test: ok")
		manifest = load_validated_manifest(args.manifest)
		verified = verify_artifacts(manifest, args.artifact_root, args.purpose) if args.artifact_root else 0
		print(
			f"corpus lock: ok; sources={len(manifest['sources'])}; excluded={len(manifest['excluded_sources'])}; "
			f"artifacts_verified={verified}; manifest_sha256={canonical_manifest_sha256(manifest)}"
		)
		return 0
	except (ValidationError, AssertionError) as error:
		print(f"corpus lock: error: {error}", file=sys.stderr)
		return 1


if __name__ == "__main__":
	sys.exit(main())
