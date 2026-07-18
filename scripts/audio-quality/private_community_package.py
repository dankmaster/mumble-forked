#!/usr/bin/env python3
"""Create and verify the unsigned private-community Windows ZIP.

The package is intentionally portable-only.  It never signs PE/MSI files and
never publishes anything.  Its trust boundary is the live candidate-build
receipt, a hash-pinned core measured-quality attestation, and the existing
Ed25519-signed models, recipes, and packaged bootstrap policy.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import stat
import sys
import tempfile
import zipfile
from pathlib import Path, PurePosixPath
from typing import Any, Mapping, MutableMapping, Sequence

from candidate_build_receipt import BuildReceiptError, validate_receipt
from payload_identity import (
	PayloadIdentityError,
	canonical_json_bytes,
	canonical_json_sha256,
	file_sha256,
	payload_file_attestation,
	payload_tree_attestation,
	payload_tree_records,
)


KIND = "mumble-unsigned-private-community-package-v1"
RECEIPT_KIND = "mumble-unsigned-private-community-package-receipt-v1"
SIGNED_BOOTSTRAP_FILES = (
	"input-enhancement-policy.json",
	"input-enhancement-policy.json.sig",
	"input-models.json",
	"input-models.json.sig",
	"input-recipes.json",
	"input-recipes.json.sig",
)
HEX40 = re.compile(r"^[0-9a-f]{40}$")
HEX64 = re.compile(r"^[0-9a-f]{64}$")
REQUIRED_RUNNERS = {
	("master_quality", "low-performance"): 500,
	("master_quality", "mainstream"): 500,
	("nightly", "low-performance"): 5000,
	("nightly", "mainstream"): 5000,
}
WARNING_TEXT = """MUMBLE - UNSIGNED PRIVATE COMMUNITY BUILD

This portable build is for the private test community only.

- It has not been Authenticode/Azure signed and Windows may show a warning.
- It is not a public release and must not be mirrored or redistributed.
- Its input-enhancement policy is an expiring, Ed25519-signed bootstrap file.
- If trust, policy, model, or recipe verification fails, Mumble falls back to Original.
- Remove this directory before installing a later public build.

The metadata/private-community-package.json inventory identifies every app byte.
"""


class CommunityPackageError(ValueError):
	"""Raised when private-community packaging cannot be proven safe."""


def _absolute(path: Path) -> Path:
	return Path(os.path.abspath(os.fspath(path)))


def _within(path: Path, root: Path) -> bool:
	try:
		_absolute(path).relative_to(_absolute(root))
		return True
	except ValueError:
		return False


def _expect(condition: bool, label: str, detail: str) -> None:
	if not condition:
		raise CommunityPackageError(f"{label}: {detail}")


def _reject_constant(value: str) -> Any:
	raise CommunityPackageError(f"JSON contains forbidden non-finite constant {value}")


def _object_pairs(pairs: Sequence[tuple[str, Any]]) -> MutableMapping[str, Any]:
	result: MutableMapping[str, Any] = {}
	for key, value in pairs:
		if key in result:
			raise CommunityPackageError(f"JSON contains duplicate key {key!r}")
		result[key] = value
	return result


def _load_json(path: Path, label: str) -> Mapping[str, Any]:
	try:
		value = json.loads(
			path.read_text(encoding="utf-8"), parse_constant=_reject_constant,
			object_pairs_hook=_object_pairs,
		)
	except (OSError, UnicodeError, json.JSONDecodeError) as error:
		raise CommunityPackageError(f"{label}: unable to read strict JSON {path}: {error}") from error
	_expect(isinstance(value, dict), label, "JSON root must be an object")
	return value


def _exact(value: Any, keys: set[str], label: str) -> Mapping[str, Any]:
	_expect(isinstance(value, dict), label, "must be an object")
	assert isinstance(value, dict)
	_expect(set(value) == keys, label, "has missing or unexpected fields")
	return value


def _hash(value: Any, label: str, length: int = 64) -> str:
	_expect(isinstance(value, str) and re.fullmatch(rf"[0-9a-f]{{{length}}}", value) is not None,
		label, "invalid lowercase hexadecimal hash")
	return str(value)


def _safe_relative(value: Any, label: str) -> str:
	_expect(isinstance(value, str) and bool(value), label, "must be a non-empty relative path")
	parsed = PurePosixPath(str(value))
	_expect(
		str(value) == parsed.as_posix() and not parsed.is_absolute()
		and "." not in parsed.parts and ".." not in parsed.parts and "\\" not in str(value),
		label, "must be a normalized safe POSIX path",
	)
	return str(value)


def _record(path: Path, relative: str | None = None) -> Mapping[str, Any]:
	attestation = payload_file_attestation(path)
	return {
		"path": relative if relative is not None else path.name,
		"sha256": attestation["sha256"],
		"size_bytes": attestation["size_bytes"],
	}


def _assert_regular_with_hash(path: Path, expected: str, label: str) -> Path:
	actual = payload_file_attestation(path)
	_expect(actual["sha256"] == expected, label, "bytes differ from the pinned SHA-256")
	return Path(str(actual["path"]))


def _model_and_recipe_identity(stage: Path) -> tuple[Mapping[str, Any], Mapping[str, Any], list[str]]:
	models = _load_json(stage / "input-models.json", "signed model manifest")
	recipes = _load_json(stage / "input-recipes.json", "signed recipe manifest")
	_exact(models, {"schemaVersion", "catalogRevision", "generatedFromAssets", "models"}, "signed model manifest")
	_exact(recipes, {"schemaVersion", "catalogRevision", "modelManifestSha256", "recipes"}, "signed recipe manifest")
	_expect(models["schemaVersion"] == 1 and recipes["schemaVersion"] == 2,
		"signed package manifests", "unsupported schema")
	_expect(isinstance(models["catalogRevision"], str) and bool(models["catalogRevision"])
		and recipes["catalogRevision"] == models["catalogRevision"],
		"signed package manifests", "catalog revision mismatch")
	_expect(recipes["modelManifestSha256"] == file_sha256(stage / "input-models.json"),
		"signed recipe manifest", "modelManifestSha256 mismatch")
	model_hashes: list[str] = []
	seen: set[str] = set()
	for index, value in enumerate(models["models"] if isinstance(models["models"], list) else []):
		model = _exact(value, {
			"id", "version", "backend", "path", "sha256", "size", "licenseSpdx",
			"sampleRateHz", "algorithmicLatencyMs", "recipeCompatibility",
		}, f"signed model manifest.models[{index}]")
		identifier = str(model["id"])
		_expect(identifier and identifier not in seen, f"signed model manifest.models[{index}].id", "invalid or duplicate")
		seen.add(identifier)
		relative = _safe_relative(model["path"], f"signed model manifest.models[{index}].path")
		asset = stage.joinpath(*PurePosixPath(relative).parts)
		actual = payload_file_attestation(asset)
		digest = _hash(model["sha256"], f"signed model manifest.models[{index}].sha256")
		_expect(actual["sha256"] == digest and actual["size_bytes"] == model["size"],
			f"signed model manifest.models[{index}]", "asset hash or size mismatch")
		model_hashes.append(digest)
	_expect(bool(model_hashes), "signed model manifest.models", "must not be empty")
	return models, recipes, sorted(set(model_hashes))


def _validate_quality_sidecar(
	path: Path,
	*,
	suite: str,
	runner_class: str,
	minimum_cases: int,
	source_sha: str,
	protected: Mapping[str, Any],
) -> None:
	document = _load_json(path, f"{suite}/{runner_class} quality qualification")
	_expect(document.get("schema_version") == 3 and document.get("status") == "passed"
		and document.get("qualification_scope") == "core" and document.get("suite") == suite,
		f"{suite}/{runner_class} quality qualification", "is not a passing schema-v3 core qualification")
	build = document.get("build")
	coverage = document.get("coverage")
	_expect(isinstance(build, dict) and isinstance(coverage, dict),
		f"{suite}/{runner_class} quality qualification", "missing build or coverage")
	assert isinstance(build, dict) and isinstance(coverage, dict)
	_expect(build.get("git_sha") == source_sha and build.get("runner_class") == runner_class,
		f"{suite}/{runner_class} quality qualification", "source or runner mismatch")
	_expect(isinstance(coverage.get("case_count"), int) and coverage["case_count"] >= minimum_cases
		and coverage.get("failed_case_count") == 0,
		f"{suite}/{runner_class} quality qualification", "coverage is below the release gate")
	for key, expected in protected.items():
		_expect(build.get(key) == expected, f"{suite}/{runner_class} quality qualification.build.{key}",
			"differs from the protected build identity")


def _validate_original_sidecar(
	path: Path, *, source_sha: str, tested_binary_sha256: str, legacy_binary_sha256: str,
) -> None:
	document = _load_json(path, "Original transport qualification")
	cases = document.get("cases")
	_expect(document.get("candidate_build_sha") == source_sha and document.get("profile") == "Original"
		and document.get("candidate_executable_sha256") == tested_binary_sha256
		and document.get("legacy_executable_sha256") == legacy_binary_sha256
		and document.get("receiver_cleanup_enabled") is False
		and document.get("transport_path") == "client1-opus-server-client2"
		and isinstance(cases, list) and len(cases) == 45,
		"Original transport qualification", "identity, transport, or 45-case coverage mismatch")
	assert isinstance(cases, list)
	for index, case in enumerate(cases):
		_expect(isinstance(case, dict), f"Original transport qualification.cases[{index}]", "must be an object")
		assert isinstance(case, dict)
		_expect(case.get("enhancement_profile") == "Original"
			and case.get("model_initialization_attempts") == 0
			and case.get("candidate_executable_sha256") == tested_binary_sha256
			and case.get("legacy_executable_sha256") == legacy_binary_sha256
			and case.get("algorithmic_latency_samples") == 0
			and case.get("fallback_count") == 0 and case.get("deadline_miss_count") == 0
			and case.get("original_receiver_fixed_timeline_passed") is True,
			f"Original transport qualification.cases[{index}]", "contains a failing Original case")


def validate_core_attestation(
	path: Path,
	*,
	expected_sha256: str,
	source_sha: str,
	stage: Path,
	candidate: Mapping[str, Any],
	build_number: int,
) -> Mapping[str, Any]:
	_assert_regular_with_hash(path, expected_sha256, "core measured-quality attestation")
	document = _load_json(path, "core measured-quality attestation")
	_exact(document, {
		"schemaVersion", "passed", "suite", "sourceSha", "testedBinaryFileName",
		"testedBinarySha256", "legacyBinarySha256", "harnessSha256", "corpusLockSha256",
		"recipeSetVersion", "modelHashes", "protectedBuildIdentity", "unsignedModelManifest",
		"unsignedRecipeManifest", "masterInputIdentity", "nightlyInputIdentity", "runners",
		"nightlyRunners", "qualityWorkflowRunId", "nightlyQualityWorkflowRunId", "createdAtUtc",
	}, "core measured-quality attestation")
	_expect(document["schemaVersion"] == 2 and document["passed"] is True
		and document["suite"] == "core_release" and document["sourceSha"] == source_sha,
		"core measured-quality attestation", "must be passing schema-v2 core_release evidence for the source")
	tested_sha = file_sha256(stage / "mumble.exe")
	_expect(document["testedBinaryFileName"] == "mumble.exe" and document["testedBinarySha256"] == tested_sha,
		"core measured-quality attestation", "does not bind the exact staged client")
	models, recipes, model_hashes = _model_and_recipe_identity(stage)
	_expect(document["recipeSetVersion"] == recipes["catalogRevision"]
		and document["modelHashes"] == model_hashes,
		"core measured-quality attestation", "recipe/model identity mismatch")
	for field, manifest_name in (
		("unsignedModelManifest", "input-models.json"),
		("unsignedRecipeManifest", "input-recipes.json"),
	):
		record = _exact(document[field], {"fileName", "sha256"}, f"core measured-quality attestation.{field}")
		_expect(record["sha256"] == file_sha256(stage / manifest_name),
			f"core measured-quality attestation.{field}", "does not bind the signed package manifest bytes")

	protected = _exact(document["protectedBuildIdentity"], {
		"tested_binary_sha256", "staged_payload_sha256", "legacy_binary_sha256", "server_binary_sha256",
		"harness_sha256", "corpus_lock_sha256", "corpus_inventory_sha256", "release_fixtures_sha256",
		"metrics_runtime_sha256", "model_manifest_sha256", "recipe_manifest_sha256",
		"recipe_set_version", "model_hashes",
	}, "core measured-quality attestation.protectedBuildIdentity")
	for key, value in protected.items():
		if key not in ("recipe_set_version", "model_hashes"):
			_hash(value, f"core measured-quality attestation.protectedBuildIdentity.{key}")
	_expect(protected["tested_binary_sha256"] == tested_sha
		and protected["legacy_binary_sha256"] == document["legacyBinarySha256"]
		and protected["harness_sha256"] == document["harnessSha256"]
		and protected["corpus_lock_sha256"] == document["corpusLockSha256"]
		and protected["model_manifest_sha256"] == file_sha256(stage / "input-models.json")
		and protected["recipe_manifest_sha256"] == file_sha256(stage / "input-recipes.json")
		and protected["recipe_set_version"] == recipes["catalogRevision"]
		and protected["model_hashes"] == model_hashes,
		"core measured-quality attestation.protectedBuildIdentity", "manifest or runtime identity mismatch")

	stage_records = list(payload_tree_records(stage))
	paths = {str(record["path"]) for record in stage_records}
	_expect(all(name in paths for name in SIGNED_BOOTSTRAP_FILES), "staged payload", "signed bootstrap files are incomplete")
	base_records = [record for record in stage_records if str(record["path"]) not in SIGNED_BOOTSTRAP_FILES]
	base_sha = canonical_json_sha256(base_records)
	_expect(protected["staged_payload_sha256"] == base_sha,
		"core measured-quality attestation.protectedBuildIdentity.staged_payload_sha256",
		"does not match the exact staged runtime after excluding only the six signed bootstrap files")
	full_sha = canonical_json_sha256(stage_records)
	candidate_payload = _exact(candidate["candidate"], {
		"build_executable", "staged_executable", "staged_payload",
	}, "candidate build receipt.candidate")
	_expect(candidate_payload["staged_payload"].get("sha256") == full_sha,
		"candidate build receipt.candidate.staged_payload", "does not bind the complete community stage")
	_expect(candidate_payload["staged_executable"].get("sha256") == tested_sha,
		"candidate build receipt.candidate.staged_executable", "does not bind the qualified client")

	package = _exact(candidate["package"], {
		"public_key_hex", "openssl", "models_manifest", "models_signature", "recipes_manifest",
		"recipes_signature", "channel_policy", "channel_policy_signature",
	}, "candidate build receipt.package")
	for field, name in (
		("models_manifest", "input-models.json"), ("models_signature", "input-models.json.sig"),
		("recipes_manifest", "input-recipes.json"), ("recipes_signature", "input-recipes.json.sig"),
		("channel_policy", "input-enhancement-policy.json"),
		("channel_policy_signature", "input-enhancement-policy.json.sig"),
	):
		reference = package[field]
		_expect(isinstance(reference, dict) and Path(str(reference.get("path", ""))) == stage / name
			and reference.get("sha256") == file_sha256(stage / name),
			f"candidate build receipt.package.{field}", "must bind the exact packaged bootstrap file")

	policy = _load_json(stage / "input-enhancement-policy.json", "packaged bootstrap policy")
	_expect(policy.get("minBuild") == build_number and policy.get("recipeSetVersion") == recipes["catalogRevision"]
		and policy.get("available") is True and policy.get("forceOriginal") is False,
		"packaged bootstrap policy", "build/catalog identity or enablement mismatch")

	evidence_root = path.parent
	seen: set[tuple[str, str]] = set()
	for field, expected_suite in (("runners", "master_quality"), ("nightlyRunners", "nightly")):
		values = document[field]
		_expect(isinstance(values, list) and len(values) == 2,
			f"core measured-quality attestation.{field}", "must contain both protected runner classes")
		for index, value in enumerate(values):
			record = _exact(value, {
				"suite", "runnerClass", "hardwareFingerprintSha256", "harnessProvenanceSha256",
				"qualityQualification", "originalVoiceQualification",
			}, f"core measured-quality attestation.{field}[{index}]")
			key = (str(record["suite"]), str(record["runnerClass"]))
			_hash(record["hardwareFingerprintSha256"], f"core measured-quality attestation.{field}[{index}].hardwareFingerprintSha256")
			_hash(record["harnessProvenanceSha256"], f"core measured-quality attestation.{field}[{index}].harnessProvenanceSha256")
			_expect(key in REQUIRED_RUNNERS and key not in seen and key[0] == expected_suite,
				f"core measured-quality attestation.{field}[{index}]", "unexpected or duplicate runner")
			seen.add(key)
			minimum = REQUIRED_RUNNERS[key]
			for evidence_field in ("qualityQualification", "originalVoiceQualification"):
				evidence = _exact(record[evidence_field], {"fileName", "sha256", "caseCount"},
					f"core measured-quality attestation.{field}[{index}].{evidence_field}")
				file_name = _safe_relative(evidence["fileName"], f"{evidence_field}.fileName")
				_expect("/" not in file_name, f"{evidence_field}.fileName", "must be a direct sibling")
				sidecar = evidence_root / file_name
				_assert_regular_with_hash(sidecar, _hash(evidence["sha256"], f"{evidence_field}.sha256"), evidence_field)
				if evidence_field == "qualityQualification":
					_expect(isinstance(evidence["caseCount"], int) and not isinstance(evidence["caseCount"], bool)
						and evidence["caseCount"] >= minimum, evidence_field, "case count is below the suite gate")
					_validate_quality_sidecar(sidecar, suite=key[0], runner_class=key[1], minimum_cases=minimum,
						source_sha=source_sha, protected=protected)
				else:
					_expect(isinstance(evidence["caseCount"], int) and not isinstance(evidence["caseCount"], bool)
						and evidence["caseCount"] == 45, evidence_field, "must contain all 45 Original cases")
					_validate_original_sidecar(sidecar, source_sha=source_sha, tested_binary_sha256=tested_sha,
						legacy_binary_sha256=str(document["legacyBinarySha256"]))
	_expect(seen == set(REQUIRED_RUNNERS), "core measured-quality attestation", "runner matrix is incomplete")
	return {
		"document": document,
		"base_payload_sha256": base_sha,
		"full_payload_sha256": full_sha,
		"full_payload_records": stage_records,
		"catalog_revision": recipes["catalogRevision"],
	}


def _zip_info(path: str) -> zipfile.ZipInfo:
	info = zipfile.ZipInfo(path, date_time=(1980, 1, 1, 0, 0, 0))
	info.compress_type = zipfile.ZIP_STORED
	info.create_system = 3
	info.external_attr = (stat.S_IFREG | 0o644) << 16
	return info


def _write_json_exclusive(path: Path, value: Mapping[str, Any]) -> None:
	data = canonical_json_bytes(value)
	with path.open("xb") as stream:
		stream.write(data)
		stream.write(b"\n")
		stream.flush()
		os.fsync(stream.fileno())


def create_archive(
	*,
	stage: Path,
	output_zip: Path,
	output_receipt: Path,
	output_sha256: Path,
	source_sha: str,
	build_number: int,
	public_key_hex: str,
	candidate_receipt_sha256: str,
	core_attestation_sha256: str,
	binding: Mapping[str, Any],
) -> Mapping[str, Any]:
	for output in (output_zip, output_receipt, output_sha256):
		_expect(not output.exists(), str(output), "refusing to overwrite an existing immutable output")
	_expect(output_zip.parent == output_receipt.parent == output_sha256.parent,
		"package outputs", "must be direct siblings")
	archive_root = f"mumble-1.7.{build_number}-{source_sha[:12]}-UNSIGNED-PRIVATE-COMMUNITY"
	expected_zip_name = archive_root + ".zip"
	_expect(output_zip.name == expected_zip_name, "output ZIP", f"must be named {expected_zip_name!r}")
	records = list(binding["full_payload_records"])
	manifest = {
		"schema_version": 1,
		"kind": KIND,
		"distribution": {
			"audience": "private-community",
			"authenticode": "not-provided",
			"azure_artifact_signing": False,
			"public_release": False,
			"portable_zip_only": True,
			"warning_file": "metadata/README-UNSIGNED-PRIVATE-COMMUNITY.txt",
		},
		"source": {
			"sha": source_sha,
			"build_number": build_number,
			"build_id": f"mumble-forked-build-{build_number}-{source_sha[:12]}",
		},
		"trust": {
			"algorithm": "Ed25519",
			"purpose": "private-community-operator-test",
			"public_key_hex": public_key_hex,
			"signed_bootstrap_files": [
				_record(stage / name, name) for name in SIGNED_BOOTSTRAP_FILES
			],
		},
		"qualification": {
			"candidate_build_receipt_sha256": candidate_receipt_sha256,
			"core_measured_attestation_sha256": core_attestation_sha256,
			"measured_base_payload_sha256": binding["base_payload_sha256"],
			"scope": "core",
		},
		"payload": {
			"root": "app",
			"sha256": binding["full_payload_sha256"],
			"file_count": len(records),
			"files": records,
		},
	}
	manifest_bytes = canonical_json_bytes(manifest) + b"\n"
	warning_bytes = WARNING_TEXT.encode("utf-8")
	temporary = output_zip.with_name(output_zip.name + f".tmp-{os.getpid()}")
	_expect(not temporary.exists(), str(temporary), "temporary output already exists")
	try:
		with zipfile.ZipFile(temporary, "x", allowZip64=True) as archive:
			for record in records:
				relative = _safe_relative(record["path"], "payload record path")
				source = stage.joinpath(*PurePosixPath(relative).parts)
				entry = f"{archive_root}/app/{relative}"
				with source.open("rb") as reader, archive.open(_zip_info(entry), "w") as writer:
					shutil.copyfileobj(reader, writer, length=1024 * 1024)
			archive.writestr(_zip_info(f"{archive_root}/metadata/private-community-package.json"), manifest_bytes)
			archive.writestr(_zip_info(f"{archive_root}/metadata/README-UNSIGNED-PRIVATE-COMMUNITY.txt"), warning_bytes)
		with temporary.open("rb+") as stream:
			os.fsync(stream.fileno())
		os.replace(temporary, output_zip)
		archive_sha = file_sha256(output_zip)
		receipt = {
			"schema_version": 1,
			"kind": RECEIPT_KIND,
			"unsigned_private_community": True,
			"source_sha": source_sha,
			"build_number": build_number,
			"archive": {
				"file_name": output_zip.name,
				"sha256": archive_sha,
				"size_bytes": output_zip.stat().st_size,
			},
			"internal_manifest_sha256": hashlib.sha256(manifest_bytes).hexdigest(),
			"staged_payload_sha256": binding["full_payload_sha256"],
			"candidate_build_receipt_sha256": candidate_receipt_sha256,
			"core_measured_attestation_sha256": core_attestation_sha256,
		}
		_write_json_exclusive(output_receipt, receipt)
		line = f"{archive_sha} *{output_zip.name}\n".encode("ascii")
		with output_sha256.open("xb") as stream:
			stream.write(line)
			stream.flush()
			os.fsync(stream.fileno())
		validate_archive(output_zip, output_receipt, output_sha256)
		return receipt
	except BaseException:
		for path in (temporary, output_zip, output_receipt, output_sha256):
			try:
				path.unlink()
			except FileNotFoundError:
				pass
		raise


def validate_archive(zip_path: Path, receipt_path: Path, sha256_path: Path) -> Mapping[str, Any]:
	receipt = _load_json(receipt_path, "private-community package receipt")
	_exact(receipt, {
		"schema_version", "kind", "unsigned_private_community", "source_sha", "build_number", "archive",
		"internal_manifest_sha256", "staged_payload_sha256", "candidate_build_receipt_sha256",
		"core_measured_attestation_sha256",
	}, "private-community package receipt")
	_expect(receipt["schema_version"] == 1 and receipt["kind"] == RECEIPT_KIND
		and receipt["unsigned_private_community"] is True,
		"private-community package receipt", "identity or unsigned marker mismatch")
	source_sha = _hash(receipt["source_sha"], "private-community package receipt.source_sha", 40)
	build_number = receipt["build_number"]
	_expect(isinstance(build_number, int) and not isinstance(build_number, bool) and build_number > 0,
		"private-community package receipt.build_number", "must be a positive integer")
	for field in (
		"internal_manifest_sha256", "staged_payload_sha256", "candidate_build_receipt_sha256",
		"core_measured_attestation_sha256",
	):
		_hash(receipt[field], f"private-community package receipt.{field}")
	archive_record = _exact(receipt["archive"], {"file_name", "sha256", "size_bytes"}, "package receipt.archive")
	_hash(archive_record["sha256"], "package receipt.archive.sha256")
	actual_zip = payload_file_attestation(zip_path)
	_expect(archive_record["file_name"] == zip_path.name and archive_record["sha256"] == actual_zip["sha256"]
		and archive_record["size_bytes"] == actual_zip["size_bytes"],
		"package receipt.archive", "ZIP name, hash, or size mismatch")
	try:
		checksum = sha256_path.read_text(encoding="ascii")
	except (OSError, UnicodeError) as error:
		raise CommunityPackageError(f"unable to read package checksum: {error}") from error
	_expect(checksum == f"{actual_zip['sha256']} *{zip_path.name}\n", "package checksum", "is not exact")

	with zipfile.ZipFile(zip_path, "r") as archive:
		infos = archive.infolist()
		names = [info.filename for info in infos]
		_expect(len(names) == len(set(names)), "private-community ZIP", "contains duplicate entries")
		for index, info in enumerate(infos):
			name = _safe_relative(info.filename, f"ZIP entry {index}")
			_expect(not name.endswith("/") and info.date_time == (1980, 1, 1, 0, 0, 0)
				and info.compress_type == zipfile.ZIP_STORED and not (info.flag_bits & 0x1)
				and info.create_system == 3
				and ((info.external_attr >> 16) & 0o170000) == stat.S_IFREG,
				f"ZIP entry {name}", "is not a deterministic unencrypted regular file")
		root_candidates = {PurePosixPath(name).parts[0] for name in names}
		_expect(len(root_candidates) == 1, "private-community ZIP", "must have exactly one archive root")
		root = next(iter(root_candidates))
		expected_root = f"mumble-1.7.{build_number}-{source_sha[:12]}-UNSIGNED-PRIVATE-COMMUNITY"
		_expect(root == expected_root and zip_path.name == expected_root + ".zip",
			"private-community ZIP", "archive root or file name does not match source/build identity")
		manifest_name = f"{root}/metadata/private-community-package.json"
		warning_name = f"{root}/metadata/README-UNSIGNED-PRIVATE-COMMUNITY.txt"
		_expect(manifest_name in names and warning_name in names, "private-community ZIP", "metadata files are missing")
		manifest_bytes = archive.read(manifest_name)
		_expect(hashlib.sha256(manifest_bytes).hexdigest() == receipt["internal_manifest_sha256"],
			"private-community ZIP manifest", "hash differs from the receipt")
		try:
			manifest = json.loads(manifest_bytes.decode("utf-8"), parse_constant=_reject_constant,
				object_pairs_hook=_object_pairs)
		except (UnicodeError, json.JSONDecodeError) as error:
			raise CommunityPackageError(f"private-community ZIP manifest is invalid: {error}") from error
		_expect(isinstance(manifest, dict) and canonical_json_bytes(manifest) + b"\n" == manifest_bytes,
			"private-community ZIP manifest", "is not canonical JSON")
		assert isinstance(manifest, dict)
		_exact(manifest, {"schema_version", "kind", "distribution", "source", "trust", "qualification", "payload"},
			"private-community ZIP manifest")
		_expect(manifest["schema_version"] == 1 and manifest["kind"] == KIND,
			"private-community ZIP manifest", "unsupported identity")
		distribution = _exact(manifest["distribution"], {
			"audience", "authenticode", "azure_artifact_signing", "public_release", "portable_zip_only",
			"warning_file",
		}, "private-community ZIP manifest.distribution")
		_expect(distribution.get("audience") == "private-community"
			and distribution.get("authenticode") == "not-provided"
			and distribution.get("azure_artifact_signing") is False
			and distribution.get("public_release") is False
			and distribution.get("portable_zip_only") is True,
			"private-community ZIP manifest.distribution", "unsafe distribution flags")
		_expect(distribution["warning_file"] == "metadata/README-UNSIGNED-PRIVATE-COMMUNITY.txt",
			"private-community ZIP manifest.distribution.warning_file", "unexpected warning path")
		source = _exact(manifest["source"], {"sha", "build_number", "build_id"},
			"private-community ZIP manifest.source")
		_expect(source["sha"] == source_sha and source["build_number"] == build_number
			and source["build_id"] == f"mumble-forked-build-{build_number}-{source_sha[:12]}",
			"private-community ZIP manifest.source", "differs from the external receipt")
		trust = _exact(manifest["trust"], {"algorithm", "purpose", "public_key_hex", "signed_bootstrap_files"},
			"private-community ZIP manifest.trust")
		_expect(trust["algorithm"] == "Ed25519" and trust["purpose"] == "private-community-operator-test"
			and HEX64.fullmatch(str(trust["public_key_hex"])) is not None,
			"private-community ZIP manifest.trust", "invalid trust-root identity")
		bootstrap_records = trust["signed_bootstrap_files"]
		_expect(isinstance(bootstrap_records, list) and len(bootstrap_records) == len(SIGNED_BOOTSTRAP_FILES),
			"private-community ZIP manifest.trust.signed_bootstrap_files", "incomplete signed bootstrap set")
		for index, (record, expected_name) in enumerate(zip(bootstrap_records, SIGNED_BOOTSTRAP_FILES)):
			item = _exact(record, {"path", "sha256", "size_bytes"},
				f"private-community ZIP manifest.trust.signed_bootstrap_files[{index}]")
			_expect(item["path"] == expected_name and HEX64.fullmatch(str(item["sha256"])) is not None
				and isinstance(item["size_bytes"], int) and item["size_bytes"] >= 0,
				f"private-community ZIP manifest.trust.signed_bootstrap_files[{index}]", "invalid file record")
		qualification = _exact(manifest["qualification"], {
			"candidate_build_receipt_sha256", "core_measured_attestation_sha256",
			"measured_base_payload_sha256", "scope",
		}, "private-community ZIP manifest.qualification")
		_expect(qualification["scope"] == "core"
			and qualification["candidate_build_receipt_sha256"] == receipt["candidate_build_receipt_sha256"]
			and qualification["core_measured_attestation_sha256"] == receipt["core_measured_attestation_sha256"],
			"private-community ZIP manifest.qualification", "differs from the external receipt")
		_hash(qualification["measured_base_payload_sha256"],
			"private-community ZIP manifest.qualification.measured_base_payload_sha256")
		_expect(archive.read(warning_name) == WARNING_TEXT.encode("utf-8"),
			"private-community ZIP warning", "warning text changed")
		payload = _exact(manifest["payload"], {"root", "sha256", "file_count", "files"},
			"private-community ZIP manifest.payload")
		_expect(payload.get("root") == "app" and isinstance(payload.get("files"), list),
			"private-community ZIP manifest.payload", "invalid payload record")
		assert isinstance(payload, dict) and isinstance(payload["files"], list)
		records = payload["files"]
		_expect(payload.get("file_count") == len(records) and payload.get("sha256") == canonical_json_sha256(records)
			and payload.get("sha256") == receipt["staged_payload_sha256"],
			"private-community ZIP manifest.payload", "tree count or identity mismatch")
		expected_entries = {manifest_name, warning_name}
		seen_payload: set[str] = set()
		previous_relative = ""
		for index, record in enumerate(records):
			item = _exact(record, {"path", "sha256", "size_bytes"}, f"payload.files[{index}]")
			relative = _safe_relative(item["path"], f"payload.files[{index}].path")
			_expect(relative not in seen_payload and (not previous_relative or relative > previous_relative)
				and _hash(item["sha256"], f"payload.files[{index}].sha256"),
				f"payload.files[{index}]", "duplicate or invalid hash")
			seen_payload.add(relative)
			previous_relative = relative
			entry = f"{root}/app/{relative}"
			expected_entries.add(entry)
			info = archive.getinfo(entry)
			_expect(info.file_size == item["size_bytes"], entry, "size mismatch")
			digest = hashlib.sha256()
			with archive.open(info, "r") as stream:
				for block in iter(lambda: stream.read(1024 * 1024), b""):
					digest.update(block)
			_expect(digest.hexdigest() == item["sha256"], entry, "hash mismatch")
		_expect(set(names) == expected_entries, "private-community ZIP", "contains missing or unmanifested entries")
	return receipt


def run_self_test() -> None:
	with tempfile.TemporaryDirectory(prefix="mumble-private-community-selftest-") as temporary:
		root = Path(temporary)
		stage = root / "stage"
		output = root / "output"
		stage.mkdir()
		output.mkdir()
		(stage / "mumble.exe").write_bytes(b"unsigned-mumble")
		(stage / "runtime.dll").write_bytes(b"runtime")
		for name in SIGNED_BOOTSTRAP_FILES:
			(stage / name).write_bytes(("signed-bootstrap:" + name).encode("ascii"))
		records = list(payload_tree_records(stage))
		base = [record for record in records if record["path"] not in SIGNED_BOOTSTRAP_FILES]
		binding = {
			"base_payload_sha256": canonical_json_sha256(base),
			"full_payload_sha256": canonical_json_sha256(records),
			"full_payload_records": records,
		}
		source_sha = "1" * 40
		public_key = "2" * 64
		name = f"mumble-1.7.44-{source_sha[:12]}-UNSIGNED-PRIVATE-COMMUNITY"
		zip_path = output / f"{name}.zip"
		receipt_path = output / f"{name}.receipt.json"
		sha_path = output / f"{name}.sha256"
		create_archive(
			stage=stage, output_zip=zip_path, output_receipt=receipt_path, output_sha256=sha_path,
			source_sha=source_sha, build_number=44, public_key_hex=public_key,
			candidate_receipt_sha256="3" * 64, core_attestation_sha256="4" * 64, binding=binding,
		)
		validate_archive(zip_path, receipt_path, sha_path)
		try:
			create_archive(
				stage=stage, output_zip=zip_path, output_receipt=receipt_path, output_sha256=sha_path,
				source_sha=source_sha, build_number=44, public_key_hex=public_key,
				candidate_receipt_sha256="3" * 64, core_attestation_sha256="4" * 64, binding=binding,
			)
		except CommunityPackageError:
			pass
		else:
			raise AssertionError("immutable package outputs were overwritten")
		tampered = output / "tampered.zip"
		shutil.copy2(zip_path, tampered)
		with zipfile.ZipFile(tampered, "a") as archive:
			archive.writestr("unexpected.txt", b"tampered")
		tampered_receipt = dict(_load_json(receipt_path, "self-test receipt"))
		tampered_receipt["archive"] = {
			"file_name": tampered.name,
			"sha256": file_sha256(tampered),
			"size_bytes": tampered.stat().st_size,
		}
		tampered_receipt_path = output / "tampered.receipt.json"
		tampered_sha_path = output / "tampered.sha256"
		_write_json_exclusive(tampered_receipt_path, tampered_receipt)
		tampered_sha_path.write_text(f"{file_sha256(tampered)} *{tampered.name}\n", encoding="ascii")
		try:
			validate_archive(tampered, tampered_receipt_path, tampered_sha_path)
		except CommunityPackageError:
			pass
		else:
			raise AssertionError("archive validator accepted an unmanifested ZIP entry")

		# Build the smallest complete core-binding graph. This exercises the
		# measured-base/full-stage split and proves that a stale runtime or
		# substituted sidecar fails even when the outer labels still look valid.
		binding_stage = root / "binding-stage"
		evidence_root = root / "core-evidence"
		binding_stage.mkdir()
		evidence_root.mkdir()
		(binding_stage / "mumble.exe").write_bytes(b"qualified-client")
		(binding_stage / "runtime.dll").write_bytes(b"qualified-runtime")
		(binding_stage / "model.bin").write_bytes(b"qualified-model")
		model_manifest = {
			"schemaVersion": 1, "catalogRevision": "recipes-test-v1", "generatedFromAssets": True,
			"models": [{
				"id": "model", "version": "1", "backend": "test", "path": "model.bin",
				"sha256": file_sha256(binding_stage / "model.bin"),
				"size": (binding_stage / "model.bin").stat().st_size, "licenseSpdx": "MIT",
				"sampleRateHz": 48000, "algorithmicLatencyMs": 0.0,
				"recipeCompatibility": ["recipe"],
			}],
		}
		_write_json_exclusive(binding_stage / "input-models.json", model_manifest)
		recipe_manifest = {
			"schemaVersion": 2, "catalogRevision": "recipes-test-v1",
			"modelManifestSha256": file_sha256(binding_stage / "input-models.json"),
			"recipes": [],
		}
		_write_json_exclusive(binding_stage / "input-recipes.json", recipe_manifest)
		policy = {
			"available": True, "expiresAt": "2099-01-01T00:00:00Z", "forceOriginal": False,
			"minBuild": 44, "recipeSetVersion": "recipes-test-v1", "recommendedProfile": "Original",
		}
		(binding_stage / "input-enhancement-policy.json").write_bytes(canonical_json_bytes(policy))
		for name in ("input-models.json.sig", "input-recipes.json.sig", "input-enhancement-policy.json.sig"):
			(binding_stage / name).write_bytes(name.encode("ascii").ljust(64, b"."))
		binding_records = list(payload_tree_records(binding_stage))
		base_records = [record for record in binding_records if record["path"] not in SIGNED_BOOTSTRAP_FILES]
		model_hashes = [file_sha256(binding_stage / "model.bin")]
		protected = {
			"tested_binary_sha256": file_sha256(binding_stage / "mumble.exe"),
			"staged_payload_sha256": canonical_json_sha256(base_records),
			"legacy_binary_sha256": "5" * 64, "server_binary_sha256": "6" * 64,
			"harness_sha256": "7" * 64, "corpus_lock_sha256": "8" * 64,
			"corpus_inventory_sha256": "9" * 64, "release_fixtures_sha256": "a" * 64,
			"metrics_runtime_sha256": "b" * 64,
			"model_manifest_sha256": file_sha256(binding_stage / "input-models.json"),
			"recipe_manifest_sha256": file_sha256(binding_stage / "input-recipes.json"),
			"recipe_set_version": "recipes-test-v1", "model_hashes": model_hashes,
		}
		runner_records: dict[str, list[Mapping[str, Any]]] = {"runners": [], "nightlyRunners": []}
		for (suite, runner_class), minimum in REQUIRED_RUNNERS.items():
			quality_name = f"quality-{suite}-{runner_class}.json"
			original_name = f"original-{suite}-{runner_class}.json"
			_write_json_exclusive(evidence_root / quality_name, {
				"schema_version": 3, "status": "passed", "qualification_scope": "core", "suite": suite,
				"build": {**protected, "git_sha": source_sha, "runner_class": runner_class},
				"coverage": {"case_count": minimum, "failed_case_count": 0},
			})
			original_cases = [{
				"enhancement_profile": "Original", "model_initialization_attempts": 0,
				"candidate_executable_sha256": protected["tested_binary_sha256"],
				"legacy_executable_sha256": protected["legacy_binary_sha256"],
				"algorithmic_latency_samples": 0, "fallback_count": 0, "deadline_miss_count": 0,
				"original_receiver_fixed_timeline_passed": True,
			} for _ in range(45)]
			_write_json_exclusive(evidence_root / original_name, {
				"candidate_build_sha": source_sha, "profile": "Original",
				"candidate_executable_sha256": protected["tested_binary_sha256"],
				"legacy_executable_sha256": protected["legacy_binary_sha256"],
				"receiver_cleanup_enabled": False, "transport_path": "client1-opus-server-client2",
				"cases": original_cases,
			})
			target = "runners" if suite == "master_quality" else "nightlyRunners"
			runner_records[target].append({
				"suite": suite, "runnerClass": runner_class,
				"hardwareFingerprintSha256": "c" * 64, "harnessProvenanceSha256": "d" * 64,
				"qualityQualification": {
					"fileName": quality_name, "sha256": file_sha256(evidence_root / quality_name),
					"caseCount": minimum,
				},
				"originalVoiceQualification": {
					"fileName": original_name, "sha256": file_sha256(evidence_root / original_name),
					"caseCount": 45,
				},
			})
		attestation = {
			"schemaVersion": 2, "passed": True, "suite": "core_release", "sourceSha": source_sha,
			"testedBinaryFileName": "mumble.exe", "testedBinarySha256": protected["tested_binary_sha256"],
			"legacyBinarySha256": protected["legacy_binary_sha256"], "harnessSha256": protected["harness_sha256"],
			"corpusLockSha256": protected["corpus_lock_sha256"], "recipeSetVersion": "recipes-test-v1",
			"modelHashes": model_hashes, "protectedBuildIdentity": protected,
			"unsignedModelManifest": {"fileName": "unsigned-input-models.json", "sha256": protected["model_manifest_sha256"]},
			"unsignedRecipeManifest": {"fileName": "unsigned-input-recipes.json", "sha256": protected["recipe_manifest_sha256"]},
			"masterInputIdentity": {}, "nightlyInputIdentity": {},
			"runners": runner_records["runners"], "nightlyRunners": runner_records["nightlyRunners"],
			"qualityWorkflowRunId": "1", "nightlyQualityWorkflowRunId": "2", "createdAtUtc": "2026-01-01T00:00:00Z",
		}
		attestation_path = evidence_root / "core-attestation.json"
		_write_json_exclusive(attestation_path, attestation)
		package_refs = {}
		for field, file_name in (
			("models_manifest", "input-models.json"), ("models_signature", "input-models.json.sig"),
			("recipes_manifest", "input-recipes.json"), ("recipes_signature", "input-recipes.json.sig"),
			("channel_policy", "input-enhancement-policy.json"),
			("channel_policy_signature", "input-enhancement-policy.json.sig"),
		):
			package_refs[field] = _record(binding_stage / file_name, str(binding_stage / file_name))
		package_refs.update({"public_key_hex": public_key, "openssl": {}})
		candidate = {
			"candidate": {
				"build_executable": {},
				"staged_executable": _record(binding_stage / "mumble.exe"),
				"staged_payload": {"path": str(binding_stage), "sha256": canonical_json_sha256(binding_records),
					"file_count": len(binding_records)},
			},
			"package": package_refs,
		}
		validate_core_attestation(
			attestation_path, expected_sha256=file_sha256(attestation_path), source_sha=source_sha,
			stage=binding_stage, candidate=candidate, build_number=44,
		)
		(binding_stage / "runtime.dll").write_bytes(b"stale-substitution")
		try:
			validate_core_attestation(
				attestation_path, expected_sha256=file_sha256(attestation_path), source_sha=source_sha,
				stage=binding_stage, candidate=candidate, build_number=44,
			)
		except CommunityPackageError:
			pass
		else:
			raise AssertionError("core binding accepted a changed staged runtime")


def _parser() -> argparse.ArgumentParser:
	parser = argparse.ArgumentParser(description=__doc__)
	mode = parser.add_mutually_exclusive_group(required=True)
	mode.add_argument("--create", action="store_true")
	mode.add_argument("--validate", action="store_true")
	mode.add_argument("--self-test", action="store_true")
	parser.add_argument("--stage-root", type=Path)
	parser.add_argument("--candidate-receipt", type=Path)
	parser.add_argument("--candidate-receipt-sha256")
	parser.add_argument("--core-attestation", type=Path)
	parser.add_argument("--core-attestation-sha256")
	parser.add_argument("--source-root", type=Path)
	parser.add_argument("--source-sha")
	parser.add_argument("--build-root", type=Path)
	parser.add_argument("--build-number", type=int)
	parser.add_argument("--public-key-hex")
	parser.add_argument("--output-zip", type=Path)
	parser.add_argument("--output-receipt", type=Path)
	parser.add_argument("--output-sha256", type=Path)
	return parser


def main(argv: Sequence[str] | None = None) -> int:
	args = _parser().parse_args(argv)
	try:
		if args.self_test:
			run_self_test()
			print("private-community package self-test: ok")
			return 0
		if args.validate:
			required = ("output_zip", "output_receipt", "output_sha256")
			missing = [name for name in required if getattr(args, name) is None]
			_expect(not missing, "validate arguments", "missing " + ", ".join(missing))
			validate_archive(args.output_zip, args.output_receipt, args.output_sha256)
			print("private-community package: valid")
			return 0
		required = (
			"stage_root", "candidate_receipt", "candidate_receipt_sha256", "core_attestation",
			"core_attestation_sha256", "source_root", "source_sha", "build_root", "build_number",
			"public_key_hex", "output_zip", "output_receipt", "output_sha256",
		)
		missing = [name for name in required if getattr(args, name) is None]
		_expect(not missing, "create arguments", "missing " + ", ".join(missing))
		_expect(isinstance(args.source_sha, str) and HEX40.fullmatch(args.source_sha) is not None,
			"source SHA", "must be 40 lowercase hexadecimal characters")
		_expect(isinstance(args.public_key_hex, str) and HEX64.fullmatch(args.public_key_hex) is not None,
			"public key", "must be 64 lowercase hexadecimal characters")
		_expect(isinstance(args.build_number, int) and args.build_number > 0, "build number", "must be positive")
		_expect(isinstance(args.candidate_receipt_sha256, str) and HEX64.fullmatch(args.candidate_receipt_sha256),
			"candidate receipt SHA-256", "must be lowercase hexadecimal")
		_expect(isinstance(args.core_attestation_sha256, str) and HEX64.fullmatch(args.core_attestation_sha256),
			"core attestation SHA-256", "must be lowercase hexadecimal")
		stage = _absolute(args.stage_root)
		source = _absolute(args.source_root)
		build = _absolute(args.build_root)
		output_zip = _absolute(args.output_zip)
		output_receipt = _absolute(args.output_receipt)
		output_sha256 = _absolute(args.output_sha256)
		_expect(output_zip.parent.is_dir() and output_receipt.parent == output_zip.parent
			and output_sha256.parent == output_zip.parent,
			"package outputs", "must share one existing output directory")
		for output in (output_zip, output_receipt, output_sha256):
			for protected in (source, build, stage):
				_expect(not _within(output, protected), str(output),
					f"must not be created inside protected root {protected}")
		_assert_regular_with_hash(args.candidate_receipt, args.candidate_receipt_sha256, "candidate build receipt")
		stage_attestation = payload_tree_attestation(stage)
		candidate = validate_receipt(
			args.candidate_receipt, expected_source_root=source, expected_commit=args.source_sha,
			expected_build_root=build, expected_stage_root=stage,
			expected_executable_sha256=file_sha256(stage / "mumble.exe"),
			expected_stage_payload_sha256=str(stage_attestation["sha256"]),
			expected_public_key_hex=args.public_key_hex,
		)
		binding = validate_core_attestation(
			args.core_attestation, expected_sha256=args.core_attestation_sha256,
			source_sha=args.source_sha, stage=stage, candidate=candidate, build_number=args.build_number,
		)
		receipt = create_archive(
			stage=stage, output_zip=output_zip, output_receipt=output_receipt,
			output_sha256=output_sha256, source_sha=args.source_sha, build_number=args.build_number,
			public_key_hex=args.public_key_hex, candidate_receipt_sha256=args.candidate_receipt_sha256,
			core_attestation_sha256=args.core_attestation_sha256, binding=binding,
		)
		print(f"private-community ZIP: {output_zip}")
		print(f"private-community ZIP SHA-256: {receipt['archive']['sha256']}")
		return 0
	except (CommunityPackageError, BuildReceiptError, PayloadIdentityError, OSError, zipfile.BadZipFile) as error:
		print(f"private-community package error: {error}", file=sys.stderr)
		return 1


if __name__ == "__main__":
	raise SystemExit(main())
