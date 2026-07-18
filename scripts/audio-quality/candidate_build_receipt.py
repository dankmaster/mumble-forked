#!/usr/bin/env python3
"""Create and validate a fail-closed Windows candidate build receipt.

The receipt is local qualification evidence, not a substitute for Authenticode
or a reproducible-build service.  It prevents the ordinary stale-build failure
mode by requiring a clean-build invocation record, a clean matching Git tree,
an up-to-date Ninja graph, and exact hashes for the configured toolchain, build
graph, candidate executable, complete staged payload, signed package manifests,
embedded-key diagnostic and release-test gates.
"""

from __future__ import annotations

import argparse
import base64
import datetime as dt
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any, Callable, Mapping, Sequence

from payload_identity import (
	PayloadIdentityError,
	canonical_json_sha256,
	file_sha256,
	payload_file_attestation,
	payload_tree_attestation,
)


RECEIPT_KIND = "mumble-windows-candidate-build-receipt-v1"
INVOCATION_KIND = "mumble-windows-candidate-build-invocation-v1"
HEX40 = re.compile(r"^[0-9a-f]{40}$")
HEX64 = re.compile(r"^[0-9a-f]{64}$")
FILE_KEYS = {"path", "sha256", "size_bytes"}
TREE_KEYS = {"path", "sha256", "file_count"}
RECEIPT_KEYS = {
	"schema_version", "kind", "receipt_body_sha256", "source", "invocation", "configuration",
	"toolchain", "freshness", "candidate", "package", "evidence",
}
CRITICAL_CACHE = {
	"CMAKE_BUILD_TYPE": "Release",
	"client": "ON",
	"server": "OFF",
	"tests": "ON",
	"benchmarks": "ON",
	"speech-cleanup-e2e": "ON",
	"modern-layout-automation": "ON",
	"rnnoise": "ON",
	"bundled-rnnoise": "ON",
	"dtln": "ON",
	"deepfilternet": "ON",
	"input-enhancement-signed-policy-required": "ON",
}
REQUIRED_GATES = {
	"DeepFilterNetCapiTests", "TestInputEnhancement", "TestInputEnhancementAuto",
	"TestInputEnhancementAutoV2", "TestInputEnhancementCalibration",
	"TestInputEnhancementCalibrationRuntime", "TestInputEnhancementPolicy",
	"TestInputEnhancementPolicyConfiguredKey", "TestInputEnhancementPolicyController",
	"TestInputEnhancementPackageVerifier", "TestInputEnhancementSettings",
	"TestInputEnhancementCalibrationPlayback", "TestModernDialogControllers",
	"TestQmlQuickComponents", "TestUpdateHealth", "TestUpdaterHealthIntegration",
	"TestUpdaterProtocolV4Simulation", "TestSpeechCleanup", "SpeechCleanupBenchmarkSelfTest",
}


class BuildReceiptError(RuntimeError):
	"""Raised when candidate build evidence is incomplete or inconsistent."""


def _reject_constant(value: str) -> Any:
	raise BuildReceiptError(f"JSON contains forbidden non-finite constant {value}")


def _object_pairs(pairs: Sequence[tuple[str, Any]]) -> Mapping[str, Any]:
	result: dict[str, Any] = {}
	for key, value in pairs:
		if key in result:
			raise BuildReceiptError(f"JSON contains duplicate key {key!r}")
		result[key] = value
	return result


def _load_json(path: Path, label: str) -> Mapping[str, Any]:
	try:
		value = json.loads(
			path.read_text(encoding="utf-8"), parse_constant=_reject_constant,
			object_pairs_hook=_object_pairs,
		)
	except (OSError, UnicodeError, json.JSONDecodeError) as error:
		raise BuildReceiptError(f"{label}: unable to read strict JSON {path}: {error}") from error
	if not isinstance(value, dict):
		raise BuildReceiptError(f"{label}: JSON root must be an object")
	return value


def _json_bytes(value: Mapping[str, Any]) -> bytes:
	try:
		return (json.dumps(value, indent=2, sort_keys=True, ensure_ascii=False, allow_nan=False) + "\n").encode("utf-8")
	except (TypeError, ValueError) as error:
		raise BuildReceiptError(f"receipt cannot be serialized as finite JSON: {error}") from error


def _exact(value: Any, keys: set[str], label: str) -> Mapping[str, Any]:
	if not isinstance(value, dict) or set(value) != keys:
		raise BuildReceiptError(f"{label} has missing or unexpected fields")
	return value


def _expect(condition: bool, label: str, detail: str) -> None:
	if not condition:
		raise BuildReceiptError(f"{label}: {detail}")


def _absolute(path: Path) -> Path:
	return Path(os.path.abspath(os.fspath(path)))


def _within(path: Path, root: Path) -> bool:
	try:
		_absolute(path).relative_to(_absolute(root))
		return True
	except ValueError:
		return False


def _regular(path: Path, label: str) -> Path:
	resolved = _absolute(path)
	try:
		payload_file_attestation(resolved)
	except PayloadIdentityError as error:
		raise BuildReceiptError(f"{label}: {error}") from error
	return resolved


def _directory(path: Path, label: str) -> Path:
	resolved = _absolute(path)
	if not resolved.is_dir():
		raise BuildReceiptError(f"{label}: directory does not exist: {resolved}")
	return resolved


def _file_ref(path: Path) -> Mapping[str, Any]:
	try:
		return dict(payload_file_attestation(path))
	except PayloadIdentityError as error:
		raise BuildReceiptError(str(error)) from error


def _tree_ref(path: Path) -> Mapping[str, Any]:
	try:
		value = payload_tree_attestation(path)
	except PayloadIdentityError as error:
		raise BuildReceiptError(str(error)) from error
	return {"path": str(_absolute(path)), "sha256": value["sha256"], "file_count": value["file_count"]}


def _validate_file_ref(value: Any, label: str) -> Path:
	reference = _exact(value, FILE_KEYS, label)
	path = _regular(Path(str(reference["path"])), label)
	actual = _file_ref(path)
	_expect(dict(reference) == actual, label, "file bytes or size differ from the receipt")
	return path


def _validate_tree_ref(value: Any, label: str) -> Path:
	reference = _exact(value, TREE_KEYS, label)
	path = _directory(Path(str(reference["path"])), label)
	actual = _tree_ref(path)
	_expect(dict(reference) == actual, label, "payload tree differs from the receipt")
	return path


def _run_git(source: Path, *arguments: str, binary: bool = False) -> str | bytes:
	try:
		result = subprocess.run(
			["git", "-C", str(source), *arguments], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
		)
	except (OSError, subprocess.CalledProcessError) as error:
		detail = error.stderr.decode("utf-8", errors="replace").strip() if isinstance(error, subprocess.CalledProcessError) else str(error)
		raise BuildReceiptError(f"Git verification failed for {source}: {detail}") from error
	return result.stdout if binary else result.stdout.decode("ascii", errors="strict").strip()


def _source_identity(source: Path, expected_commit: str | None = None) -> Mapping[str, Any]:
	root = _directory(source, "candidate source root")
	commit = str(_run_git(root, "rev-parse", "HEAD")).lower()
	tree = str(_run_git(root, "rev-parse", "HEAD^{tree}")).lower()
	status = bytes(_run_git(root, "status", "--porcelain=v1", "--untracked-files=all", binary=True))
	_expect(bool(HEX40.fullmatch(commit) and HEX40.fullmatch(tree)), "candidate source", "invalid Git object identity")
	if expected_commit is not None:
		_expect(commit == expected_commit.lower(), "candidate source", "HEAD differs from the expected commit")
	_expect(status == b"", "candidate source", "worktree is not clean, including untracked files")
	return {
		"root": str(root), "commit": commit, "tree": tree, "clean": True,
		"status_porcelain_sha256": hashlib.sha256(status).hexdigest(),
	}


def _cache_entries(path: Path) -> Mapping[str, Mapping[str, str]]:
	entries: dict[str, Mapping[str, str]] = {}
	try:
		lines = path.read_text(encoding="utf-8", errors="strict").splitlines()
	except (OSError, UnicodeError) as error:
		raise BuildReceiptError(f"unable to read CMake cache {path}: {error}") from error
	for line in lines:
		if not line or line.startswith(("#", "//")) or "=" not in line:
			continue
		left, value = line.split("=", 1)
		if ":" not in left:
			continue
		name, entry_type = left.split(":", 1)
		if name in entries:
			raise BuildReceiptError(f"CMake cache contains duplicate entry {name!r}")
		entries[name] = {"type": entry_type, "value": value}
	return entries


def _cache_value(entries: Mapping[str, Mapping[str, str]], name: str) -> str:
	value = entries.get(name)
	if not isinstance(value, dict) or set(value) != {"type", "value"}:
		raise BuildReceiptError(f"CMake cache is missing required entry {name}")
	return str(value["value"])


def _iso_timestamp(value: Any, label: str) -> dt.datetime:
	if not isinstance(value, str) or not value or value.endswith("Z") is False:
		raise BuildReceiptError(f"{label} must be a UTC ISO-8601 timestamp ending in Z")
	try:
		parsed = dt.datetime.fromisoformat(value[:-1] + "+00:00")
	except ValueError as error:
		raise BuildReceiptError(f"{label} is not a valid timestamp") from error
	if parsed.utcoffset() != dt.timedelta(0):
		raise BuildReceiptError(f"{label} is not UTC")
	return parsed


def _validate_invocation(value: Mapping[str, Any], source: Mapping[str, Any], build: Path, stage: Path) -> None:
	_exact(value, {
		"schema_version", "kind", "source_commit", "source_tree", "build_root", "stage_root",
		"clean_build", "command", "configuration_options", "targets", "started_at_utc",
		"finished_at_utc", "exit_code",
	}, "build invocation")
	_expect(value["schema_version"] == 1 and value["kind"] == INVOCATION_KIND, "build invocation", "unsupported contract")
	_expect(value["source_commit"] == source["commit"] and value["source_tree"] == source["tree"], "build invocation", "source identity mismatch")
	_expect(_absolute(Path(str(value["build_root"]))) == build, "build invocation", "build root mismatch")
	_expect(_absolute(Path(str(value["stage_root"]))) == stage, "build invocation", "stage root mismatch")
	_expect(value["clean_build"] is True and value["exit_code"] == 0, "build invocation", "qualification requires a successful clean build")
	command = value["command"]
	targets = value["targets"]
	options = value["configuration_options"]
	_expect(isinstance(command, list) and command and all(isinstance(item, str) and item for item in command), "build invocation.command", "must be a non-empty argv array")
	_expect(isinstance(targets, list) and targets and all(isinstance(item, str) and item for item in targets), "build invocation.targets", "must name built targets")
	_expect("mumble" in targets, "build invocation.targets", "must include the mumble target")
	_expect(isinstance(options, dict) and options and all(isinstance(key, str) and isinstance(item, str) for key, item in options.items()), "build invocation.configuration_options", "must be a string map")
	started = _iso_timestamp(value["started_at_utc"], "build invocation.started_at_utc")
	finished = _iso_timestamp(value["finished_at_utc"], "build invocation.finished_at_utc")
	_expect(finished >= started, "build invocation", "finish precedes start")


def _tool_reference(cache: Mapping[str, Mapping[str, str]], name: str, label: str) -> Mapping[str, Any]:
	return _file_ref(_regular(Path(_cache_value(cache, name)), label))


def _run_freshness(ninja: Path, build: Path) -> Mapping[str, Any]:
	command = [str(ninja), "-C", str(build), "-n", "mumble"]
	try:
		result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
	except OSError as error:
		raise BuildReceiptError(f"unable to run pinned Ninja freshness check: {error}") from error
	combined = result.stdout + result.stderr
	text = combined.decode("utf-8", errors="replace")
	_expect(result.returncode == 0, "build freshness", f"pinned Ninja returned {result.returncode}: {text.strip()}")
	_expect("ninja: no work to do." in text.lower(), "build freshness", "candidate target is stale or build graph wants to rebuild it")
	return {
		"command": command, "exit_code": 0, "no_work_to_do": True,
		"output_sha256": hashlib.sha256(combined).hexdigest(),
	}


def _verify_signature(openssl: Path, public_key_hex: str, document: Path, signature: Path) -> None:
	try:
		public_key = bytes.fromhex(public_key_hex)
	except ValueError as error:
		raise BuildReceiptError("package public key is not hexadecimal") from error
	_expect(len(public_key) == 32, "package public key", "Ed25519 key must contain 32 bytes")
	signature_bytes = signature.read_bytes()
	_expect(len(signature_bytes) == 64, str(signature), "Ed25519 signature must contain 64 raw bytes")
	# RFC 8410 SubjectPublicKeyInfo prefix for a raw Ed25519 public key.
	spki = bytes.fromhex("302a300506032b6570032100") + public_key
	with tempfile.TemporaryDirectory(prefix="mumble-build-receipt-ed25519-") as temporary:
		key_path = Path(temporary) / "public.der"
		key_path.write_bytes(spki)
		command = [
			str(openssl), "pkeyutl", "-verify", "-pubin", "-keyform", "DER", "-inkey", str(key_path),
			"-rawin", "-in", str(document), "-sigfile", str(signature),
		]
		result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
	if result.returncode != 0:
		detail = (result.stderr or result.stdout).decode("utf-8", errors="replace").strip()
		raise BuildReceiptError(f"{document.name}: Ed25519 signature verification failed: {detail}")


def _validate_embedded_attestation(document: Mapping[str, Any], candidate_sha: str, public_key_hex: str) -> int:
	_exact(document, {
		"schemaVersion", "kind", "passed", "audioFree", "createdAtUtc", "candidateExecutableSha256",
		"generatorSha256", "runtimeDiagnosticSha256", "runtimeDiagnosticBase64",
	}, "embedded-key attestation")
	_expect(document["schemaVersion"] == 1 and document["kind"] == "input-enhancement-embedded-key-attestation", "embedded-key attestation", "unsupported contract")
	_expect(document["passed"] is True and document["audioFree"] is True, "embedded-key attestation", "did not pass")
	_expect(document["candidateExecutableSha256"] == candidate_sha, "embedded-key attestation", "candidate hash mismatch")
	try:
		diagnostic_bytes = base64.b64decode(document["runtimeDiagnosticBase64"], validate=True)
	except (ValueError, TypeError) as error:
		raise BuildReceiptError("embedded-key attestation contains invalid base64") from error
	_expect(base64.b64encode(diagnostic_bytes).decode("ascii") == document["runtimeDiagnosticBase64"], "embedded-key attestation", "diagnostic base64 is not canonical")
	_expect(hashlib.sha256(diagnostic_bytes).hexdigest() == document["runtimeDiagnosticSha256"], "embedded-key attestation", "diagnostic hash mismatch")
	try:
		diagnostic = json.loads(diagnostic_bytes.decode("utf-8"), parse_constant=_reject_constant, object_pairs_hook=_object_pairs)
	except (UnicodeError, json.JSONDecodeError) as error:
		raise BuildReceiptError("embedded-key runtime diagnostic is not strict UTF-8 JSON") from error
	_exact(diagnostic, {"schemaVersion", "kind", "buildNumber", "packageVerificationMode", "configuredPublicKeySha256"}, "embedded-key runtime diagnostic")
	key_sha = hashlib.sha256(bytes.fromhex(public_key_hex)).hexdigest()
	_expect(
		diagnostic["schemaVersion"] == 1 and diagnostic["kind"] == "mumble-input-enhancement-build-identity"
		and isinstance(diagnostic["buildNumber"], int) and not isinstance(diagnostic["buildNumber"], bool)
		and diagnostic["buildNumber"] > 0 and diagnostic["packageVerificationMode"] == "managed-signed"
		and diagnostic["configuredPublicKeySha256"] == key_sha,
		"embedded-key runtime diagnostic", "build number, signing mode or public key mismatch",
	)
	return int(diagnostic["buildNumber"])


def _validate_test_gates(document: Mapping[str, Any]) -> None:
	_exact(document, {"schemaVersion", "passed", "buildType", "cmakeOptions", "gates"}, "release test gates")
	_expect(document["schemaVersion"] == 1 and document["passed"] is True and document["buildType"] == "Release", "release test gates", "top-level gate did not pass")
	options = _exact(document["cmakeOptions"], {"tests", "benchmarks", "speechCleanupE2e"}, "release test gates.cmakeOptions")
	_expect(all(value is True for value in options.values()), "release test gates.cmakeOptions", "required build coverage was disabled")
	gates = document["gates"]
	_expect(isinstance(gates, list) and gates, "release test gates.gates", "gate list is empty")
	names: set[str] = set()
	for index, gate in enumerate(gates):
		item = _exact(gate, {"name", "passed", "exitCode", "durationMs"}, f"release test gates.gates[{index}]")
		name = item["name"]
		_expect(isinstance(name, str) and name and name not in names, f"release test gates.gates[{index}]", "invalid or duplicate name")
		names.add(name)
		_expect(item["passed"] is True and item["exitCode"] == 0, f"release test gates.gates[{index}]", "gate failed")
		_expect(isinstance(item["durationMs"], int) and not isinstance(item["durationMs"], bool) and item["durationMs"] >= 0, f"release test gates.gates[{index}]", "invalid duration")
	_expect(REQUIRED_GATES <= names, "release test gates", "one or more mandatory gates are absent")


def _validate_channel_policy(path: Path, build_number: int, recipe_set_version: str) -> Mapping[str, Any]:
	try:
		raw = path.read_bytes()
	except OSError as error:
		raise BuildReceiptError(f"unable to read qualification channel policy: {error}") from error
	_expect(0 < len(raw) <= 2048, "qualification channel policy", "unsafe byte length")
	policy = _load_json(path, "qualification channel policy")
	expected_order = ["available", "expiresAt", "forceOriginal", "minBuild", "recipeSetVersion", "recommendedProfile"]
	_expect(list(policy) == expected_order, "qualification channel policy", "fields are not in canonical lexical order")
	_expect(policy["available"] is True and policy["forceOriginal"] is False, "qualification channel policy", "qualification must enable enhancement without forceOriginal")
	_expect(isinstance(policy["minBuild"], int) and not isinstance(policy["minBuild"], bool) and policy["minBuild"] == build_number, "qualification channel policy.minBuild", "does not match embedded positive build")
	_expect(policy["recipeSetVersion"] == recipe_set_version, "qualification channel policy.recipeSetVersion", "does not match signed recipe manifest")
	_expect(policy["recommendedProfile"] in ("Original", "Light", "Balanced", "Quality", "Auto"), "qualification channel policy.recommendedProfile", "unsupported or manual-only profile")
	try:
		expires = dt.datetime.strptime(str(policy["expiresAt"]), "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=dt.timezone.utc)
	except ValueError as error:
		raise BuildReceiptError("qualification channel policy.expiresAt is not canonical UTC") from error
	now = dt.datetime.now(dt.timezone.utc)
	_expect(now < expires <= now + dt.timedelta(days=31), "qualification channel policy.expiresAt", "policy is expired or exceeds the 31-day client limit")
	canonical = json.dumps(policy, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
	_expect(raw == canonical, "qualification channel policy", "JSON bytes are not canonical")
	return policy


def _receipt_body_hash(document: Mapping[str, Any]) -> str:
	body = dict(document)
	body.pop("receipt_body_sha256", None)
	return canonical_json_sha256(body)


def validate_receipt(
	receipt_path: Path,
	*,
	expected_source_root: Path | None = None,
	expected_commit: str | None = None,
	expected_build_root: Path | None = None,
	expected_stage_root: Path | None = None,
	expected_executable_sha256: str | None = None,
	expected_stage_payload_sha256: str | None = None,
	expected_public_key_hex: str | None = None,
	verify_live: bool = True,
	signature_verifier: Callable[[Path, str, Path, Path], None] = _verify_signature,
) -> Mapping[str, Any]:
	path = _regular(receipt_path, "candidate build receipt")
	before = _file_ref(path)
	document = _load_json(path, "candidate build receipt")
	_exact(document, RECEIPT_KEYS, "candidate build receipt")
	_expect(document["schema_version"] == 1 and document["kind"] == RECEIPT_KIND, "candidate build receipt", "unsupported contract")
	_expect(document["receipt_body_sha256"] == _receipt_body_hash(document), "candidate build receipt", "body hash mismatch")

	source = _exact(document["source"], {"root", "commit", "tree", "clean", "status_porcelain_sha256"}, "candidate build receipt.source")
	_expect(source["clean"] is True and source["status_porcelain_sha256"] == hashlib.sha256(b"").hexdigest(), "candidate build receipt.source", "source was not clean")
	_expect(isinstance(source["commit"], str) and HEX40.fullmatch(source["commit"]) is not None, "candidate build receipt.source.commit", "invalid commit")
	_expect(isinstance(source["tree"], str) and HEX40.fullmatch(source["tree"]) is not None, "candidate build receipt.source.tree", "invalid tree")
	source_root = _directory(Path(str(source["root"])), "candidate receipt source root")
	if expected_source_root is not None:
		_expect(source_root == _absolute(expected_source_root), "candidate build receipt.source.root", "external source root mismatch")
	if expected_commit is not None:
		_expect(source["commit"] == expected_commit.lower(), "candidate build receipt.source.commit", "external commit mismatch")
	if verify_live:
		_expect(dict(source) == _source_identity(source_root, source["commit"]), "candidate build receipt.source", "live source identity changed")

	invocation_path = _validate_file_ref(document["invocation"], "candidate build invocation")
	invocation = _load_json(invocation_path, "candidate build invocation")
	configuration = _exact(document["configuration"], {"build_root", "cmake_cache", "cache_entries_sha256", "build_graph"}, "candidate build receipt.configuration")
	build_root = _directory(Path(str(configuration["build_root"])), "candidate build root")
	if expected_build_root is not None:
		_expect(build_root == _absolute(expected_build_root), "candidate build receipt.configuration.build_root", "external build root mismatch")
	cache_path = _validate_file_ref(configuration["cmake_cache"], "candidate CMake cache")
	_expect(cache_path == build_root / "CMakeCache.txt", "candidate CMake cache", "must be at build root")
	cache = _cache_entries(cache_path)
	_expect(configuration["cache_entries_sha256"] == canonical_json_sha256(cache), "candidate CMake cache", "canonical entries hash mismatch")
	_expect(_absolute(Path(_cache_value(cache, "CMAKE_HOME_DIRECTORY"))) == source_root, "candidate CMake cache", "source root mismatch")
	for name, wanted in CRITICAL_CACHE.items():
		_expect(_cache_value(cache, name).upper() == wanted.upper(), "candidate CMake cache", f"{name} must equal {wanted}")

	candidate = _exact(document["candidate"], {"build_executable", "staged_executable", "staged_payload"}, "candidate build receipt.candidate")
	build_exe = _validate_file_ref(candidate["build_executable"], "candidate build executable")
	stage_exe = _validate_file_ref(candidate["staged_executable"], "candidate staged executable")
	stage_root = _validate_tree_ref(candidate["staged_payload"], "candidate staged payload")
	_expect(build_exe == build_root / "mumble.exe", "candidate build executable", "must be build-root mumble.exe")
	_expect(stage_exe == stage_root / "mumble.exe", "candidate staged executable", "must be stage-root mumble.exe")
	_expect(candidate["build_executable"]["sha256"] == candidate["staged_executable"]["sha256"], "candidate executable", "build and stage bytes differ")
	if expected_stage_root is not None:
		_expect(stage_root == _absolute(expected_stage_root), "candidate staged payload", "external stage root mismatch")
	if expected_executable_sha256 is not None:
		_expect(candidate["staged_executable"]["sha256"] == expected_executable_sha256.lower(), "candidate executable", "external executable hash mismatch")
	if expected_stage_payload_sha256 is not None:
		_expect(candidate["staged_payload"]["sha256"] == expected_stage_payload_sha256.lower(), "candidate staged payload", "external payload hash mismatch")

	_validate_invocation(invocation, source, build_root, stage_root)
	invocation_options = invocation["configuration_options"]
	for name in set(CRITICAL_CACHE) | {"BUILD_NUMBER", "MUMBLE_INPUT_ENHANCEMENT_POLICY_PUBLIC_KEY_HEX"}:
		_expect(invocation_options.get(name) == _cache_value(cache, name), "candidate build invocation.configuration_options", f"{name} does not match CMake cache")

	graph = _exact(configuration["build_graph"], {"build_ninja", "rules_ninja", "configure_log", "check_cache"}, "candidate build receipt.configuration.build_graph")
	for name, relative in {
		"build_ninja": "build.ninja", "rules_ninja": "CMakeFiles/rules.ninja",
		"configure_log": "CMakeFiles/CMakeConfigureLog.yaml", "check_cache": "CMakeFiles/cmake.check_cache",
	}.items():
		actual = _validate_file_ref(graph[name], f"candidate build graph {name}")
		_expect(actual == build_root / Path(relative), f"candidate build graph {name}", "unexpected path")

	toolchain = _exact(document["toolchain"], {"cmake", "ninja", "c_compiler", "cxx_compiler", "toolchain_file", "identity_sha256"}, "candidate build receipt.toolchain")
	tool_paths: dict[str, Path] = {}
	for name, cache_name in {
		"cmake": "CMAKE_COMMAND", "ninja": "CMAKE_MAKE_PROGRAM", "c_compiler": "CMAKE_C_COMPILER",
		"cxx_compiler": "CMAKE_CXX_COMPILER", "toolchain_file": "CMAKE_TOOLCHAIN_FILE",
	}.items():
		tool_paths[name] = _validate_file_ref(toolchain[name], f"candidate toolchain {name}")
		_expect(tool_paths[name] == _absolute(Path(_cache_value(cache, cache_name))), f"candidate toolchain {name}", "CMake cache path mismatch")
	tool_identity = {name: dict(toolchain[name]) for name in ("cmake", "ninja", "c_compiler", "cxx_compiler", "toolchain_file")}
	_expect(toolchain["identity_sha256"] == canonical_json_sha256(tool_identity), "candidate toolchain", "identity hash mismatch")

	package = _exact(document["package"], {
		"public_key_hex", "openssl", "models_manifest", "models_signature", "recipes_manifest",
		"recipes_signature", "channel_policy", "channel_policy_signature",
	}, "candidate build receipt.package")
	public_key_hex = str(package["public_key_hex"]).lower()
	_expect(HEX64.fullmatch(public_key_hex) is not None, "candidate build receipt.package.public_key_hex", "invalid Ed25519 public key")
	if expected_public_key_hex is not None:
		_expect(public_key_hex == expected_public_key_hex.lower(), "candidate build receipt.package.public_key_hex", "external key mismatch")
	_expect(_cache_value(cache, "MUMBLE_INPUT_ENHANCEMENT_POLICY_PUBLIC_KEY_HEX").lower() == public_key_hex, "candidate CMake cache", "embedded policy key mismatch")
	openssl = _validate_file_ref(package["openssl"], "candidate OpenSSL verifier")
	models = _validate_file_ref(package["models_manifest"], "candidate input-models.json")
	models_sig = _validate_file_ref(package["models_signature"], "candidate input-models.json.sig")
	recipes = _validate_file_ref(package["recipes_manifest"], "candidate input-recipes.json")
	recipes_sig = _validate_file_ref(package["recipes_signature"], "candidate input-recipes.json.sig")
	channel_policy = _validate_file_ref(package["channel_policy"], "candidate qualification channel policy")
	channel_policy_sig = _validate_file_ref(package["channel_policy_signature"], "candidate qualification channel policy signature")
	for actual, name in ((models, "input-models.json"), (models_sig, "input-models.json.sig"), (recipes, "input-recipes.json"), (recipes_sig, "input-recipes.json.sig")):
		_expect(actual == stage_root / name, name, "manifest/signature must be inside the attested stage root")
	signature_verifier(openssl, public_key_hex, models, models_sig)
	signature_verifier(openssl, public_key_hex, recipes, recipes_sig)
	_expect(channel_policy.name == "input-enhancement-policy.json" and channel_policy_sig.name == "input-enhancement-policy.json.sig", "qualification channel policy", "must use stable policy filenames")
	signature_verifier(openssl, public_key_hex, channel_policy, channel_policy_sig)
	recipe_document = _load_json(recipes, "candidate input-recipes.json")
	recipe_set_version = recipe_document.get("catalogRevision")
	_expect(isinstance(recipe_set_version, str) and re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,63}", recipe_set_version) is not None, "candidate input-recipes.json.catalogRevision", "invalid recipe set version")

	evidence = _exact(document["evidence"], {"embedded_key_attestation", "embedded_key_attestation_generator", "test_gates"}, "candidate build receipt.evidence")
	attestation_path = _validate_file_ref(evidence["embedded_key_attestation"], "candidate embedded-key attestation")
	generator = _validate_file_ref(evidence["embedded_key_attestation_generator"], "candidate embedded-key attestation generator")
	attestation = _load_json(attestation_path, "candidate embedded-key attestation")
	build_number = _validate_embedded_attestation(attestation, str(candidate["staged_executable"]["sha256"]), public_key_hex)
	_expect(attestation["generatorSha256"] == file_sha256(generator), "candidate embedded-key attestation", "generator hash mismatch")
	_expect(str(build_number) == _cache_value(cache, "BUILD_NUMBER"), "candidate build number", "embedded diagnostic and CMake cache differ")
	test_gates_path = _validate_file_ref(evidence["test_gates"], "candidate release test gates")
	_validate_test_gates(_load_json(test_gates_path, "candidate release test gates"))
	_validate_channel_policy(channel_policy, build_number, recipe_set_version)

	freshness = _exact(document["freshness"], {"command", "exit_code", "no_work_to_do", "output_sha256"}, "candidate build receipt.freshness")
	_expect(freshness["exit_code"] == 0 and freshness["no_work_to_do"] is True and HEX64.fullmatch(str(freshness["output_sha256"])) is not None, "candidate build receipt.freshness", "invalid recorded result")
	if verify_live:
		live_freshness = _run_freshness(tool_paths["ninja"], build_root)
		_expect(live_freshness == dict(freshness), "candidate build receipt.freshness", "live freshness command/output changed")
	_expect(before == _file_ref(path), "candidate build receipt", "receipt changed while being validated")
	return document


def create_receipt(args: argparse.Namespace) -> Mapping[str, Any]:
	source = _source_identity(args.source_root, args.expected_commit)
	build = _directory(args.build_root, "candidate build root")
	stage = _directory(args.stage_root, "candidate stage root")
	output = _absolute(args.output)
	for protected in (_absolute(Path(str(source["root"]))), build, stage):
		_expect(not _within(output, protected), "candidate build receipt output", "must be outside source/build/stage trees")
	invocation_path = _regular(args.build_invocation, "candidate build invocation")
	for protected in (_absolute(Path(str(source["root"]))), build, stage):
		_expect(not _within(invocation_path, protected), "candidate build invocation", "must be an external evidence file")
	invocation = _load_json(invocation_path, "candidate build invocation")
	cache_path = _regular(build / "CMakeCache.txt", "candidate CMake cache")
	cache = _cache_entries(cache_path)
	_validate_invocation(invocation, source, build, stage)
	for name, wanted in CRITICAL_CACHE.items():
		_expect(_cache_value(cache, name).upper() == wanted.upper(), "candidate CMake cache", f"{name} must equal {wanted}")
	public_key = args.public_key_hex.lower()
	_expect(HEX64.fullmatch(public_key) is not None, "--public-key-hex", "must contain 64 lowercase hexadecimal characters")
	build_exe = _regular(build / "mumble.exe", "candidate build executable")
	stage_exe = _regular(stage / "mumble.exe", "candidate staged executable")
	_expect(file_sha256(build_exe) == file_sha256(stage_exe), "candidate executable", "build and stage bytes differ")
	toolchain_refs = {
		name: _tool_reference(cache, cache_name, f"candidate toolchain {name}")
		for name, cache_name in {
			"cmake": "CMAKE_COMMAND", "ninja": "CMAKE_MAKE_PROGRAM", "c_compiler": "CMAKE_C_COMPILER",
			"cxx_compiler": "CMAKE_CXX_COMPILER", "toolchain_file": "CMAKE_TOOLCHAIN_FILE",
		}.items()
	}
	freshness = _run_freshness(Path(str(toolchain_refs["ninja"]["path"])), build)
	document: dict[str, Any] = {
		"schema_version": 1,
		"kind": RECEIPT_KIND,
		"source": source,
		"invocation": _file_ref(invocation_path),
		"configuration": {
			"build_root": str(build),
			"cmake_cache": _file_ref(cache_path),
			"cache_entries_sha256": canonical_json_sha256(cache),
			"build_graph": {
				"build_ninja": _file_ref(_regular(build / "build.ninja", "build.ninja")),
				"rules_ninja": _file_ref(_regular(build / "CMakeFiles" / "rules.ninja", "rules.ninja")),
				"configure_log": _file_ref(_regular(build / "CMakeFiles" / "CMakeConfigureLog.yaml", "configure log")),
				"check_cache": _file_ref(_regular(build / "CMakeFiles" / "cmake.check_cache", "CMake check-cache marker")),
			},
		},
		"toolchain": {**toolchain_refs, "identity_sha256": canonical_json_sha256(toolchain_refs)},
		"freshness": freshness,
		"candidate": {
			"build_executable": _file_ref(build_exe), "staged_executable": _file_ref(stage_exe),
			"staged_payload": _tree_ref(stage),
		},
		"package": {
			"public_key_hex": public_key,
			"openssl": _file_ref(_regular(args.openssl, "OpenSSL verifier")),
			"models_manifest": _file_ref(_regular(stage / "input-models.json", "input-models.json")),
			"models_signature": _file_ref(_regular(stage / "input-models.json.sig", "input-models.json.sig")),
			"recipes_manifest": _file_ref(_regular(stage / "input-recipes.json", "input-recipes.json")),
			"recipes_signature": _file_ref(_regular(stage / "input-recipes.json.sig", "input-recipes.json.sig")),
			"channel_policy": _file_ref(_regular(args.channel_policy, "qualification channel policy")),
			"channel_policy_signature": _file_ref(_regular(args.channel_policy_signature, "qualification channel policy signature")),
		},
		"evidence": {
			"embedded_key_attestation": _file_ref(_regular(args.embedded_key_attestation, "embedded-key attestation")),
			"embedded_key_attestation_generator": _file_ref(_regular(args.embedded_key_attestation_generator, "embedded-key attestation generator")),
			"test_gates": _file_ref(_regular(args.test_gates, "release test gates")),
		},
	}
	document["receipt_body_sha256"] = _receipt_body_hash(document)
	output.parent.mkdir(parents=True, exist_ok=True)
	created = False
	try:
		with output.open("xb") as stream:
			stream.write(_json_bytes(document))
			stream.flush()
			os.fsync(stream.fileno())
		created = True
	except FileExistsError as error:
		raise BuildReceiptError(f"refusing to overwrite build receipt: {output}") from error
	try:
		validate_receipt(
			output, expected_source_root=Path(str(source["root"])), expected_commit=str(source["commit"]),
			expected_build_root=build, expected_stage_root=stage, expected_executable_sha256=file_sha256(stage_exe),
			expected_stage_payload_sha256=str(document["candidate"]["staged_payload"]["sha256"]),
			expected_public_key_hex=public_key,
		)
	except BaseException:
		if created:
			try:
				output.unlink()
			except OSError:
				pass
		raise
	return document


def run_self_test() -> None:
	# Exercise strict parsing and the receipt's self-hash first.
	minimal = {
		"schema_version": 1, "kind": RECEIPT_KIND, "source": {}, "invocation": {},
		"configuration": {}, "toolchain": {}, "freshness": {}, "candidate": {}, "package": {}, "evidence": {},
	}
	minimal["receipt_body_sha256"] = _receipt_body_hash(minimal)
	if not HEX64.fullmatch(minimal["receipt_body_sha256"]):
		raise AssertionError("receipt body hash regression")
	tampered = dict(minimal)
	tampered["kind"] = "tampered"
	if tampered["receipt_body_sha256"] == _receipt_body_hash(tampered):
		raise AssertionError("receipt body hash accepted tampering")
	with tempfile.TemporaryDirectory(prefix="mumble-build-receipt-selftest-") as temporary:
		root = Path(temporary)
		duplicate = root / "duplicate.json"
		duplicate.write_text('{"schema_version":1,"schema_version":1}\n', encoding="utf-8")
		try:
			_load_json(duplicate, "duplicate self-test")
		except BuildReceiptError:
			pass
		else:
			raise AssertionError("strict JSON accepted duplicate keys")
		invocation = {
			"schema_version": 1, "kind": INVOCATION_KIND, "source_commit": "1" * 40,
			"source_tree": "2" * 40, "build_root": str(root / "build"), "stage_root": str(root / "stage"),
			"clean_build": True, "command": ["builder", "--clean"], "configuration_options": {"client": "ON"},
			"targets": ["mumble"], "started_at_utc": "2026-01-01T00:00:00Z",
			"finished_at_utc": "2026-01-01T00:01:00Z", "exit_code": 0,
		}
		_validate_invocation(invocation, {"commit": "1" * 40, "tree": "2" * 40}, root / "build", root / "stage")
		invocation["clean_build"] = False
		try:
			_validate_invocation(invocation, {"commit": "1" * 40, "tree": "2" * 40}, root / "build", root / "stage")
		except BuildReceiptError:
			pass
		else:
			raise AssertionError("invocation accepted a non-clean build")

		# Build a complete synthetic evidence graph. Only live Git/Ninja and
		# Ed25519 execution are substituted; production callers cannot disable
		# those checks because verify_live defaults to true and the CLI exposes
		# no bypass.
		source = root / "source"
		build = root / "build"
		stage = root / "stage"
		tools = root / "tools"
		evidence_root = root / "evidence"
		for directory_path in (source, build / "CMakeFiles", stage, tools, evidence_root):
			directory_path.mkdir(parents=True, exist_ok=True)
		commit = "1" * 40
		tree = "2" * 40
		public_key = "3" * 64
		for name in ("cmake.exe", "ninja.exe", "cl.exe", "vcpkg.cmake", "openssl.exe"):
			(tools / name).write_bytes(("tool-" + name).encode("ascii"))
		for relative in ("build.ninja", "CMakeFiles/rules.ninja", "CMakeFiles/CMakeConfigureLog.yaml", "CMakeFiles/cmake.check_cache"):
			path = build / relative
			path.parent.mkdir(parents=True, exist_ok=True)
			path.write_bytes(("graph-" + relative).encode("ascii"))
		(build / "mumble.exe").write_bytes(b"candidate-executable")
		(stage / "mumble.exe").write_bytes(b"candidate-executable")
		(stage / "runtime.dll").write_bytes(b"runtime-v1")
		(stage / "input-models.json").write_text('{"schemaVersion":1}\n', encoding="utf-8")
		(stage / "input-models.json.sig").write_bytes(b"m" * 64)
		(stage / "input-recipes.json").write_text('{"schemaVersion":2,"catalogRevision":"input-recipes-v2"}\n', encoding="utf-8")
		(stage / "input-recipes.json.sig").write_bytes(b"r" * 64)
		policy_path = evidence_root / "input-enhancement-policy.json"
		policy_signature_path = evidence_root / "input-enhancement-policy.json.sig"
		policy_expiry = (dt.datetime.now(dt.timezone.utc) + dt.timedelta(days=10)).strftime("%Y-%m-%dT%H:%M:%SZ")
		policy_path.write_text(
			'{"available":true,"expiresAt":"' + policy_expiry + '","forceOriginal":false,'
			'"minBuild":44,"recipeSetVersion":"input-recipes-v2","recommendedProfile":"Original"}',
			encoding="utf-8",
		)
		policy_signature_path.write_bytes(b"p" * 64)
		cache_values = {
			**CRITICAL_CACHE,
			"BUILD_NUMBER": "44",
			"MUMBLE_INPUT_ENHANCEMENT_POLICY_PUBLIC_KEY_HEX": public_key,
			"CMAKE_HOME_DIRECTORY": str(source),
			"CMAKE_COMMAND": str(tools / "cmake.exe"),
			"CMAKE_MAKE_PROGRAM": str(tools / "ninja.exe"),
			"CMAKE_C_COMPILER": str(tools / "cl.exe"),
			"CMAKE_CXX_COMPILER": str(tools / "cl.exe"),
			"CMAKE_TOOLCHAIN_FILE": str(tools / "vcpkg.cmake"),
		}
		cache_path = build / "CMakeCache.txt"
		cache_path.write_text("".join(f"{name}:STRING={value}\n" for name, value in cache_values.items()), encoding="utf-8")
		invocation_path = evidence_root / "invocation.json"
		invocation = {
			"schema_version": 1, "kind": INVOCATION_KIND, "source_commit": commit, "source_tree": tree,
			"build_root": str(build), "stage_root": str(stage), "clean_build": True,
			"command": ["pwsh.exe", "-File", "build-local-windows-client.ps1", "-CleanBuild"],
			"configuration_options": {
				name: value for name, value in cache_values.items()
				if name in set(CRITICAL_CACHE) | {"BUILD_NUMBER", "MUMBLE_INPUT_ENHANCEMENT_POLICY_PUBLIC_KEY_HEX"}
			},
			"targets": ["mumble"], "started_at_utc": "2026-01-01T00:00:00Z",
			"finished_at_utc": "2026-01-01T00:01:00Z", "exit_code": 0,
		}
		invocation_path.write_bytes(_json_bytes(invocation))
		generator = evidence_root / "new-input-enhancement-embedded-key-attestation.ps1"
		generator.write_bytes(b"self-test-generator")
		diagnostic = {
			"schemaVersion": 1, "kind": "mumble-input-enhancement-build-identity", "buildNumber": 44,
			"packageVerificationMode": "managed-signed",
			"configuredPublicKeySha256": hashlib.sha256(bytes.fromhex(public_key)).hexdigest(),
		}
		diagnostic_bytes = json.dumps(diagnostic, sort_keys=True, separators=(",", ":")).encode("utf-8")
		attestation = {
			"schemaVersion": 1, "kind": "input-enhancement-embedded-key-attestation", "passed": True,
			"audioFree": True, "createdAtUtc": "2026-01-01T00:02:00Z",
			"candidateExecutableSha256": file_sha256(stage / "mumble.exe"),
			"generatorSha256": file_sha256(generator),
			"runtimeDiagnosticSha256": hashlib.sha256(diagnostic_bytes).hexdigest(),
			"runtimeDiagnosticBase64": base64.b64encode(diagnostic_bytes).decode("ascii"),
		}
		attestation_path = evidence_root / "embedded-key-attestation.json"
		attestation_path.write_bytes(_json_bytes(attestation))
		gates = {
			"schemaVersion": 1, "passed": True, "buildType": "Release",
			"cmakeOptions": {"tests": True, "benchmarks": True, "speechCleanupE2e": True},
			"gates": [
				{"name": name, "passed": True, "exitCode": 0, "durationMs": 1}
				for name in sorted(REQUIRED_GATES)
			],
		}
		gates_path = evidence_root / "test-gates.json"
		gates_path.write_bytes(_json_bytes(gates))
		cache = _cache_entries(cache_path)
		toolchain = {
			"cmake": _file_ref(tools / "cmake.exe"), "ninja": _file_ref(tools / "ninja.exe"),
			"c_compiler": _file_ref(tools / "cl.exe"), "cxx_compiler": _file_ref(tools / "cl.exe"),
			"toolchain_file": _file_ref(tools / "vcpkg.cmake"),
		}
		full_receipt: dict[str, Any] = {
			"schema_version": 1, "kind": RECEIPT_KIND,
			"source": {
				"root": str(source), "commit": commit, "tree": tree, "clean": True,
				"status_porcelain_sha256": hashlib.sha256(b"").hexdigest(),
			},
			"invocation": _file_ref(invocation_path),
			"configuration": {
				"build_root": str(build), "cmake_cache": _file_ref(cache_path),
				"cache_entries_sha256": canonical_json_sha256(cache),
				"build_graph": {
					"build_ninja": _file_ref(build / "build.ninja"),
					"rules_ninja": _file_ref(build / "CMakeFiles/rules.ninja"),
					"configure_log": _file_ref(build / "CMakeFiles/CMakeConfigureLog.yaml"),
					"check_cache": _file_ref(build / "CMakeFiles/cmake.check_cache"),
				},
			},
			"toolchain": {**toolchain, "identity_sha256": canonical_json_sha256(toolchain)},
			"freshness": {
				"command": [str(tools / "ninja.exe"), "-C", str(build), "-n", "mumble"],
				"exit_code": 0, "no_work_to_do": True, "output_sha256": "4" * 64,
			},
			"candidate": {
				"build_executable": _file_ref(build / "mumble.exe"),
				"staged_executable": _file_ref(stage / "mumble.exe"), "staged_payload": _tree_ref(stage),
			},
			"package": {
				"public_key_hex": public_key, "openssl": _file_ref(tools / "openssl.exe"),
				"models_manifest": _file_ref(stage / "input-models.json"),
				"models_signature": _file_ref(stage / "input-models.json.sig"),
				"recipes_manifest": _file_ref(stage / "input-recipes.json"),
				"recipes_signature": _file_ref(stage / "input-recipes.json.sig"),
				"channel_policy": _file_ref(policy_path),
				"channel_policy_signature": _file_ref(policy_signature_path),
			},
			"evidence": {
				"embedded_key_attestation": _file_ref(attestation_path),
				"embedded_key_attestation_generator": _file_ref(generator), "test_gates": _file_ref(gates_path),
			},
		}
		full_receipt["receipt_body_sha256"] = _receipt_body_hash(full_receipt)
		full_receipt_path = evidence_root / "candidate-build-receipt.json"
		full_receipt_path.write_bytes(_json_bytes(full_receipt))
		validate_receipt(
			full_receipt_path, expected_source_root=source, expected_commit=commit, expected_build_root=build,
			expected_stage_root=stage, expected_executable_sha256=file_sha256(stage / "mumble.exe"),
			expected_stage_payload_sha256=full_receipt["candidate"]["staged_payload"]["sha256"],
			expected_public_key_hex=public_key, verify_live=False,
			signature_verifier=lambda _openssl, _key, document, signature: _expect(
				document.is_file() and signature.stat().st_size == 64, "self-test signature", "invalid fixture",
			),
		)
		invalid_policy = evidence_root / "invalid-policy.json"
		invalid_policy.write_text(
			'{"available":true,"expiresAt":"' + policy_expiry + '","forceOriginal":true,'
			'"minBuild":44,"recipeSetVersion":"input-recipes-v2","recommendedProfile":"Original"}',
			encoding="utf-8",
		)
		try:
			_validate_channel_policy(invalid_policy, 44, "input-recipes-v2")
		except BuildReceiptError:
			pass
		else:
			raise AssertionError("policy validator accepted forceOriginal=true for qualification")
		failed_gates = json.loads(json.dumps(gates))
		failed_gates["gates"][0]["passed"] = False
		try:
			_validate_test_gates(failed_gates)
		except BuildReceiptError:
			pass
		else:
			raise AssertionError("test-gate validator accepted a failed mandatory gate")
		tampered_receipt = dict(full_receipt)
		tampered_receipt["source"] = {**full_receipt["source"], "commit": "9" * 40}
		tampered_receipt_path = evidence_root / "tampered-receipt.json"
		tampered_receipt_path.write_bytes(_json_bytes(tampered_receipt))
		try:
			validate_receipt(tampered_receipt_path, verify_live=False, signature_verifier=lambda *_args: None)
		except BuildReceiptError:
			pass
		else:
			raise AssertionError("receipt accepted a relabelled body without a matching body hash")
		(stage / "runtime.dll").write_bytes(b"tampered-runtime")
		try:
			validate_receipt(full_receipt_path, verify_live=False, signature_verifier=lambda *_args: None)
		except BuildReceiptError:
			pass
		else:
			raise AssertionError("receipt accepted a tampered staged runtime")


def parser() -> argparse.ArgumentParser:
	result = argparse.ArgumentParser(description=__doc__)
	mode = result.add_mutually_exclusive_group(required=True)
	mode.add_argument("--create", action="store_true")
	mode.add_argument("--validate", action="store_true")
	mode.add_argument("--self-test", action="store_true")
	result.add_argument("--receipt", type=Path)
	result.add_argument("--output", type=Path)
	result.add_argument("--source-root", type=Path)
	result.add_argument("--expected-commit")
	result.add_argument("--build-root", type=Path)
	result.add_argument("--stage-root", type=Path)
	result.add_argument("--build-invocation", type=Path)
	result.add_argument("--public-key-hex")
	result.add_argument("--openssl", type=Path)
	result.add_argument("--embedded-key-attestation", type=Path)
	result.add_argument("--embedded-key-attestation-generator", type=Path)
	result.add_argument("--test-gates", type=Path)
	result.add_argument("--channel-policy", type=Path)
	result.add_argument("--channel-policy-signature", type=Path)
	result.add_argument("--expected-executable-sha256")
	result.add_argument("--expected-stage-payload-sha256")
	return result


def main(argv: Sequence[str] | None = None) -> int:
	args = parser().parse_args(argv)
	try:
		if args.self_test:
			run_self_test()
			print("candidate build receipt self-test: ok")
			return 0
		if args.create:
			required = (
				"output", "source_root", "expected_commit", "build_root", "stage_root", "build_invocation",
				"public_key_hex", "openssl", "embedded_key_attestation", "embedded_key_attestation_generator",
				"test_gates", "channel_policy", "channel_policy_signature",
			)
			missing = [f"--{name.replace('_', '-')}" for name in required if getattr(args, name) is None]
			if missing:
				raise BuildReceiptError("create arguments are missing: " + ", ".join(missing))
			document = create_receipt(args)
			print(f"candidate build receipt: {args.output}")
			print(f"candidate build receipt body SHA-256: {document['receipt_body_sha256']}")
			return 0
		required = ("receipt", "source_root", "expected_commit", "build_root", "stage_root", "public_key_hex")
		missing = [f"--{name.replace('_', '-')}" for name in required if getattr(args, name) is None]
		if missing:
			raise BuildReceiptError("validate arguments are missing: " + ", ".join(missing))
		validate_receipt(
			args.receipt, expected_source_root=args.source_root, expected_commit=args.expected_commit,
			expected_build_root=args.build_root, expected_stage_root=args.stage_root,
			expected_executable_sha256=args.expected_executable_sha256,
			expected_stage_payload_sha256=args.expected_stage_payload_sha256,
			expected_public_key_hex=args.public_key_hex,
		)
		print("candidate build receipt: valid")
		return 0
	except (BuildReceiptError, PayloadIdentityError, OSError, subprocess.SubprocessError) as error:
		print(f"candidate build receipt error: {error}", file=sys.stderr)
		return 1


if __name__ == "__main__":
	raise SystemExit(main())
