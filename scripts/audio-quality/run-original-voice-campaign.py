#!/usr/bin/env python3
"""Run and seal the 45-pair Legacy-vs-Original localhost campaign.

This is the tracked execution companion to
``assemble-original-voice-qualification.py``.  It invokes an externally
pinned Windows two-client wrapper, but owns the release-sensitive parts that
must be deterministic and reviewable: the exact matrix, independent payload
snapshots, clean-worktree receipts, the frozen rendered-fixture attestation,
explicit per-case manifest bindings, and final qualification assembly.

The campaign is deliberately sequential.  Running multiple localhost client
pairs at once would turn CPU and jitter contention into qualification input.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import wave
from pathlib import Path, PurePosixPath
from typing import Any, Mapping, MutableMapping, Sequence

from payload_identity import (
	PayloadIdentityError,
	canonical_json_sha256,
	file_sha256,
	payload_file_attestation,
	is_reparse,
	payload_tree_attestation,
	payload_tree_records,
)


LEGACY_INSTRUMENTATION_BASE_COMMIT = "ada2a85f6b551a2f3d8c6b23649edcd3c0b9a8f8"
LEGACY_BUILD_COMMIT = "234e5042669ee5387b06af7069f6157f465be0c9"
LEGACY_EXECUTABLE_SHA256 = "eb062ee53356e8223eb1264f9be5f8276a5562963ef6cf932410aa0f561dc816"
LEGACY_STAGE_PAYLOAD_SHA256 = "970ceaa1bc3e1fc9ab25ff8f1c8ce8fd5854445b4660c22be227ff0a806a96a1"
SERVER_COMMIT = "edd13692174b81554726b58cd2fa27135d45b0df"
SERVER_EXECUTABLE_SHA256 = "d78f6d2889a3140fd87a68db84d8aebcfe9712848d6fc2cd257d452b68be1b5e"
EMPTY_SHA256 = hashlib.sha256(b"").hexdigest()
CASE_ID = "master_quality-validation-00081"
RENDERED_SAMPLES = 288_000
SAMPLE_RATE_HZ = 48_000
ALIGNMENT_SAMPLES = 1_920
BINDING_KIND = "mumble-original-voice-campaign-bindings-v1"
STATE_KIND = "mumble-original-voice-campaign-state-v1"
WORKTREE_KIND = "mumble-clean-worktree-receipt-v1"
FIXTURE_KIND = "mumble-original-voice-rendered-fixture-v2"
HEX40 = re.compile(r"^[0-9a-f]{40}$")
SAFE_ID = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$")
HEX64 = re.compile(r"^[0-9a-f]{64}$")


class CampaignError(RuntimeError):
	"""Raised when a campaign input or produced artifact is not trustworthy."""


def _load_checker() -> Any:
	path = Path(__file__).with_name("check-original-voice-contract.py")
	spec = importlib.util.spec_from_file_location("mumble_original_campaign_checker", path)
	if spec is None or spec.loader is None:
		raise CampaignError(f"unable to load Original contract checker: {path}")
	module = importlib.util.module_from_spec(spec)
	spec.loader.exec_module(module)
	return module


CHECKER = _load_checker()
REQUIRED_MATRIX = tuple(dict(item) for item in CHECKER.required_matrix())


def _reject_constant(value: str) -> Any:
	raise CampaignError(f"JSON contains forbidden non-finite constant {value}")


def _object_pairs(pairs: Sequence[tuple[str, Any]]) -> Mapping[str, Any]:
	result: dict[str, Any] = {}
	for key, value in pairs:
		if key in result:
			raise CampaignError(f"JSON contains duplicate key {key!r}")
		result[key] = value
	return result


def load_json(path: Path, label: str) -> Mapping[str, Any]:
	try:
		value = json.loads(
			path.read_text(encoding="utf-8"), parse_constant=_reject_constant,
			object_pairs_hook=_object_pairs,
		)
	except (OSError, UnicodeError, json.JSONDecodeError) as error:
		raise CampaignError(f"{label}: unable to read strict JSON {path}: {error}") from error
	if not isinstance(value, dict):
		raise CampaignError(f"{label}: JSON root must be an object")
	return value


def json_bytes(value: Mapping[str, Any]) -> bytes:
	try:
		return (json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True, allow_nan=False) + "\n").encode("utf-8")
	except (TypeError, ValueError) as error:
		raise CampaignError(f"refusing to serialize non-finite campaign evidence: {error}") from error


def write_new(path: Path, payload: bytes) -> None:
	path.parent.mkdir(parents=True, exist_ok=True)
	try:
		with path.open("xb") as stream:
			stream.write(payload)
			stream.flush()
			os.fsync(stream.fileno())
	except FileExistsError as error:
		raise CampaignError(f"refusing to overwrite sealed campaign artifact: {path}") from error


def write_atomic_mutable(path: Path, value: Mapping[str, Any]) -> None:
	payload = json_bytes(value)
	path.parent.mkdir(parents=True, exist_ok=True)
	temporary: Path | None = None
	try:
		with tempfile.NamedTemporaryFile(prefix=f".{path.name}.", suffix=".tmp", dir=path.parent, delete=False) as stream:
			temporary = Path(stream.name)
			stream.write(payload)
			stream.flush()
			os.fsync(stream.fileno())
		os.replace(temporary, path)
	finally:
		if temporary is not None:
			try:
				temporary.unlink()
			except FileNotFoundError:
				pass


def absolute(path: Path) -> Path:
	return Path(os.path.abspath(os.fspath(path)))


def is_within(path: Path, root: Path) -> bool:
	try:
		absolute(path).relative_to(absolute(root))
		return True
	except ValueError:
		return False


def sha256_argument(value: str, label: str) -> str:
	result = value.lower()
	if not HEX64.fullmatch(result):
		raise CampaignError(f"{label} must be a full lowercase SHA-256 digest")
	return result


def reject_reparse_components(path: Path, label: str) -> None:
	resolved = absolute(path)
	current = Path(resolved.anchor)
	for part in resolved.parts[1:]:
		current /= part
		if current.exists() and is_reparse(current):
			raise CampaignError(f"{label} traverses a reparse point: {current}")


def regular_file(path: Path, label: str) -> Path:
	resolved = absolute(path)
	if not resolved.is_file():
		raise CampaignError(f"{label}: regular file does not exist: {resolved}")
	try:
		payload_file_attestation(resolved)
	except PayloadIdentityError as error:
		raise CampaignError(f"{label}: {error}") from error
	return resolved


def directory(path: Path, label: str) -> Path:
	resolved = absolute(path)
	if not resolved.is_dir():
		raise CampaignError(f"{label}: directory does not exist: {resolved}")
	return resolved


def file_reference(path: Path) -> Mapping[str, Any]:
	try:
		return dict(payload_file_attestation(path))
	except PayloadIdentityError as error:
		raise CampaignError(str(error)) from error


def tree_reference(path: Path) -> Mapping[str, Any]:
	try:
		value = payload_tree_attestation(path)
	except PayloadIdentityError as error:
		raise CampaignError(str(error)) from error
	return {"path": str(absolute(path)), "sha256": value["sha256"], "file_count": value["file_count"]}


def run_git(source: Path, *arguments: str, binary: bool = False) -> bytes | str:
	try:
		result = subprocess.run(
			["git", "-C", str(source), *arguments], check=True, stdout=subprocess.PIPE,
			stderr=subprocess.PIPE,
		)
	except (OSError, subprocess.CalledProcessError) as error:
		detail = error.stderr.decode("utf-8", errors="replace").strip() if isinstance(error, subprocess.CalledProcessError) else str(error)
		raise CampaignError(f"Git failed for {source}: {detail}") from error
	return result.stdout if binary else result.stdout.decode("utf-8", errors="strict").strip()


def clean_worktree_receipt(source: Path, role: str, expected_commit: str) -> Mapping[str, Any]:
	root = directory(source, f"{role} source root")
	head = str(run_git(root, "rev-parse", "HEAD"))
	if head != expected_commit or not HEX40.fullmatch(head):
		raise CampaignError(f"{role} source HEAD {head!r} differs from required commit {expected_commit}")
	tree = str(run_git(root, "rev-parse", "HEAD^{tree}"))
	status = bytes(run_git(root, "status", "--porcelain=v1", "--untracked-files=all", binary=True))
	if status:
		preview = status.decode("utf-8", errors="replace").splitlines()[:5]
		raise CampaignError(f"{role} source worktree is not clean (including untracked files): {' | '.join(preview)}")
	return {
		"schema_version": 1,
		"kind": WORKTREE_KIND,
		"role": role,
		"git_commit": head,
		"git_tree_sha": tree,
		"git_status_porcelain_sha256": EMPTY_SHA256,
		"clean": True,
		"source_root": str(root),
	}


def cmake_cache(cache: Path) -> Mapping[str, str]:
	result: dict[str, str] = {}
	try:
		lines = cache.read_text(encoding="utf-8", errors="strict").splitlines()
	except (OSError, UnicodeError) as error:
		raise CampaignError(f"unable to read CMake cache {cache}: {error}") from error
	for line in lines:
		if not line or line.startswith(("#", "//")) or "=" not in line or ":" not in line.split("=", 1)[0]:
			continue
		left, value = line.split("=", 1)
		name, _kind = left.split(":", 1)
		result[name] = value
	return result


def verify_build_root(build: Path, source: Path, role: str) -> Mapping[str, str]:
	root = directory(build, f"{role} build root")
	cache_path = regular_file(root / "CMakeCache.txt", f"{role} CMake cache")
	cache = cmake_cache(cache_path)
	home = cache.get("CMAKE_HOME_DIRECTORY", "")
	if not home or absolute(Path(home)) != absolute(source):
		raise CampaignError(f"{role} CMake cache is not bound to source root {source}")
	if role in ("legacy", "candidate"):
		for field in ("speech-cleanup-e2e", "modern-layout-automation"):
			if cache.get(field) != "ON":
				raise CampaignError(f"{role} build must have {field}=ON")
	else:
		if cache.get("server") != "ON" or cache.get("client") != "OFF":
			raise CampaignError("server build must have server=ON and client=OFF")
	return cache


def copy_payload_snapshot(source: Path, destination: Path) -> Mapping[str, Any]:
	if destination.exists():
		raise CampaignError(f"payload snapshot destination already exists: {destination}")
	try:
		source_records = list(payload_tree_records(source))
	except PayloadIdentityError as error:
		raise CampaignError(f"unsafe source stage payload: {error}") from error
	destination.mkdir(parents=True)
	for record in source_records:
		relative = PurePosixPath(str(record["path"]))
		source_file = source.joinpath(*relative.parts)
		target_file = destination.joinpath(*relative.parts)
		target_file.parent.mkdir(parents=True, exist_ok=True)
		shutil.copy2(source_file, target_file, follow_symlinks=False)
	try:
		source_after = payload_tree_attestation(source)
		destination_attestation = payload_tree_attestation(destination)
	except PayloadIdentityError as error:
		raise CampaignError(f"unable to seal copied stage payload: {error}") from error
	expected_hash = canonical_json_sha256(source_records)
	if source_after["sha256"] != expected_hash or destination_attestation["sha256"] != expected_hash:
		raise CampaignError("stage payload changed during snapshot or copied bytes differ")
	return {"path": str(destination), "sha256": expected_hash, "file_count": len(source_records)}


def wave_samples(path: Path, label: str) -> int:
	try:
		with wave.open(str(path), "rb") as stream:
			if (
				stream.getcomptype() != "NONE" or stream.getnchannels() != 1
				or stream.getframerate() != SAMPLE_RATE_HZ or stream.getsampwidth() != 2
			):
				raise CampaignError(f"{label}: expected mono 48 kHz PCM16 WAV")
			count = stream.getnframes()
			if len(stream.readframes(count)) != count * 2:
				raise CampaignError(f"{label}: truncated PCM payload")
			return count
	except (OSError, EOFError, wave.Error) as error:
		raise CampaignError(f"{label}: unable to parse WAV: {error}") from error


def select_one(values: Sequence[Any], predicate: Any, label: str) -> Mapping[str, Any]:
	matches = [item for item in values if isinstance(item, dict) and predicate(item)]
	if len(matches) != 1:
		raise CampaignError(f"{label}: expected exactly one matching entry, found {len(matches)}")
	return matches[0]


def make_fixture(
	corpus_lock: Path, inventory: Path, plan: Path, render_manifest: Path,
	input_wav: Path, clean_wav: Path,
) -> tuple[Mapping[str, Any], Mapping[str, Any]]:
	lock_json = load_json(corpus_lock, "corpus lock")
	inventory_json = load_json(inventory, "corpus inventory")
	plan_json = load_json(plan, "mixture plan")
	render_json = load_json(render_manifest, "render manifest")
	lock_canonical = canonical_json_sha256(lock_json)
	inventory_canonical = canonical_json_sha256(inventory_json)
	plan_canonical = canonical_json_sha256(plan_json)
	if inventory_json.get("schema_version") != 3 or inventory_json.get("eligibility") != "release":
		raise CampaignError("Original campaign requires a release-eligible schema-v3 inventory")
	if inventory_json.get("corpus_lock_sha256") != lock_canonical:
		raise CampaignError("inventory is not bound to the supplied corpus lock")
	if (
		plan_json.get("schema_version") != 4 or plan_json.get("generator") != "mumble-audio-mixture-plan-v4"
		or plan_json.get("suite") != "master_quality" or plan_json.get("split") != "validation"
		or plan_json.get("timeline_alignment") != "fixed"
	):
		raise CampaignError("mixture plan is not the fixed master-quality validation-v4 plan")
	plan_cases = plan_json.get("cases")
	if not isinstance(plan_cases, list) or len(plan_cases) != 500:
		raise CampaignError("mixture plan must contain exactly 500 cases")
	plan_format = plan_json.get("format")
	if not isinstance(plan_format, dict) or (
		plan_format.get("sample_rate_hz"), plan_format.get("channels"),
		plan_format.get("frame_samples"), plan_format.get("duration_ms")
	) != (SAMPLE_RATE_HZ, 1, 480, 6000):
		raise CampaignError("mixture plan has the wrong fixed 48 kHz mono format")
	plan_case = select_one(plan_cases, lambda item: item.get("case_id") == CASE_ID, "mixture plan")
	if plan_case.get("profile") != "Original" or not isinstance(plan_case.get("startup"), dict) or plan_case["startup"].get("preroll_ms") != 0:
		raise CampaignError("frozen parity plan case is not cold-start Original")
	speech = plan_case.get("speech")
	noise = plan_case.get("noise")
	mix = plan_case.get("mix")
	if not isinstance(speech, dict) or not isinstance(noise, dict) or not isinstance(mix, dict):
		raise CampaignError("frozen parity plan case components are incomplete")
	if speech.get("language") != "sv-SE" or noise.get("class") != "competing-speech" or mix.get("snr_db") != 5:
		raise CampaignError("frozen parity scene must be Swedish competing speech at 5 dB SNR")
	for label, component in (("speech", speech), ("noise", noise)):
		window = component.get("window")
		if not isinstance(window, dict) or window.get("length_samples") != RENDERED_SAMPLES:
			raise CampaignError(f"frozen parity {label} window is not exactly 288000 samples")
	if plan_json.get("corpus_lock_sha256") != lock_canonical or plan_json.get("corpus_inventory_sha256") != inventory_canonical:
		raise CampaignError("mixture plan trust roots differ from lock/inventory")
	if (
		render_json.get("schema_version") != 2 or render_json.get("renderer") != "mumble-audio-mixture-renderer-v2"
		or render_json.get("private_audio_do_not_upload") is not True
		or render_json.get("sample_rate_hz") != SAMPLE_RATE_HZ or render_json.get("channels") != 1
		or render_json.get("corpus_lock_sha256") != lock_canonical
		or render_json.get("corpus_inventory_sha256") != inventory_canonical
		or render_json.get("plan_sha256") != plan_canonical
	):
		raise CampaignError("render manifest differs from the protected v2 trust roots")
	render_cases = render_json.get("cases")
	if not isinstance(render_cases, list):
		raise CampaignError("render manifest cases must be an array")
	render_case = select_one(render_cases, lambda item: item.get("case_id") == CASE_ID, "render manifest")
	if render_case.get("profile") != "Original" or render_case.get("startup_preroll_ms") != 0 or render_case.get("rendered_samples") != RENDERED_SAMPLES:
		raise CampaignError("frozen rendered parity case metadata mismatch")
	for field, expected in (
		("speech_source_sha256", speech.get("sha256")),
		("noise_source_sha256", noise.get("sha256")),
		("rir_source_sha256", mix.get("rir", {}).get("sha256") if isinstance(mix.get("rir"), dict) else None),
		(
			"microphone_response_source_sha256",
			mix.get("microphone_response", {}).get("sha256") if isinstance(mix.get("microphone_response"), dict) else None,
		),
	):
		if expected is None or render_case.get(field) != expected:
			raise CampaignError(f"rendered parity source binding mismatch for {field}")
	for field, path in (("input", input_wav), ("clean_reference", clean_wav)):
		reference = render_case.get(field)
		if not isinstance(reference, dict) or set(reference) != {"path", "sha256"}:
			raise CampaignError(f"render case {field} reference is incomplete")
		relative = PurePosixPath(str(reference["path"]))
		if relative.is_absolute() or "." in relative.parts or ".." in relative.parts:
			raise CampaignError(f"render case {field} path is unsafe")
		expected_path = render_manifest.parent.joinpath(*relative.parts)
		if absolute(expected_path) != absolute(path) or reference["sha256"] != file_sha256(path):
			raise CampaignError(f"render case {field} differs from the selected WAV")
	if wave_samples(input_wav, "input WAV") != RENDERED_SAMPLES or wave_samples(clean_wav, "clean-reference WAV") != RENDERED_SAMPLES:
		raise CampaignError("frozen parity WAVs must both contain exactly 288000 samples")
	if RENDERED_SAMPLES % ALIGNMENT_SAMPLES:
		raise CampaignError("frozen parity WAV is not aligned to 1920 samples")
	fixture = {
		"schema_version": 2,
		"kind": FIXTURE_KIND,
		"case_id": CASE_ID,
		"alignment_samples": ALIGNMENT_SAMPLES,
		"rendered_samples": RENDERED_SAMPLES,
		"corpus_inventory_sha256": inventory_canonical,
		"mixture_plan_sha256": plan_canonical,
		"render_manifest_sha256": file_sha256(render_manifest),
		"render_entry_sha256": canonical_json_sha256(render_case),
		"input_sha256": file_sha256(input_wav),
		"clean_reference_sha256": file_sha256(clean_wav),
	}
	corpus_binding = {
		"corpus_lock": file_reference(corpus_lock),
		"corpus_lock_canonical_sha256": lock_canonical,
		"corpus_inventory": file_reference(inventory),
		"corpus_inventory_canonical_sha256": inventory_canonical,
		"mixture_plan": file_reference(plan),
		"mixture_plan_canonical_sha256": plan_canonical,
		"render_manifest": file_reference(render_manifest),
		"render_entry_sha256": fixture["render_entry_sha256"],
		"selected_case_id": CASE_ID,
		"input_wav": file_reference(input_wav),
		"clean_reference_wav": file_reference(clean_wav),
	}
	return fixture, corpus_binding


def case_key(case: Mapping[str, Any]) -> str:
	return f"b{case['bitrate_bps']}-f{case['frames_per_packet']}-{case['transmit_mode']}"


def wrapper_mode(mode: str) -> str:
	return {"continuous": "Continuous", "push_to_talk": "PTT", "vad": "VAD"}[mode]


def identity_role(implementation: str) -> str:
	if implementation == "legacy":
		return "legacy"
	if implementation == "original":
		return "candidate"
	raise CampaignError(f"unsupported campaign implementation: {implementation!r}")


def command_for_case(
	powershell: Path, wrapper: Path, output: Path, campaign_id: str, case: Mapping[str, Any], role: str,
	input_wav: Path, clean_wav: Path, build_root: Path, stage_root: Path,
	server_build_root: Path, server_exe: Path, scorer: Path, timeout: int,
) -> list[str]:
	profile = "Legacy" if role == "legacy" else "Original"
	label = f"original-contract-{campaign_id}-{role}-{case_key(case)}"
	return [
		str(powershell), "-NoLogo", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
		"-File", str(wrapper),
		"-InputWav", str(input_wav), "-CleanReferenceWav", str(clean_wav),
		"-OutputRoot", str(output), "-RunLabel", label,
		"-ClientBuildDir", str(build_root), "-ClientStageDir", str(stage_root),
		"-ServerBuildDir", str(server_build_root), "-ServerExe", str(server_exe),
		"-SenderBitrateBps", str(case["bitrate_bps"]),
		"-SenderFramesPerPacket", str(case["frames_per_packet"]),
		"-SenderTransmitMode", wrapper_mode(str(case["transmit_mode"])),
		"-SenderInputEnhancementProfile", profile,
		"-SenderCleanupMode", "Off", "-DisableSenderAutoAdapt",
		"-PreRollFrames", "0", "-TailFrames", "0", "-DrainMilliseconds", "1500",
		"-FixedTimelineScorerPath", str(scorer),
		"-RequireVoiceContractEvidence", "-UnbaselinedVoiceContractControl",
		"-SkipBuild", "-AttestedStageOnly", "-TimeoutSeconds", str(timeout),
	]


def validate_manifest_minimal(path: Path, case: Mapping[str, Any], role: str, state: Mapping[str, Any]) -> Mapping[str, Any]:
	manifest = load_json(path, f"{role} {case_key(case)} manifest")
	profile = "Legacy" if role == "legacy" else "Original"
	sealed = state["sealed"]
	spec = state["spec"]
	run_root = absolute(Path(str(manifest.get("run_root", ""))))
	if path != run_root / "manifest.json" or not is_within(run_root, Path(state["runs_root"])):
		raise CampaignError(f"{path}: manifest/run root escaped the sealed campaign runs tree")
	expected_label = f"original-contract-{state['campaign_id']}-{role}-{case_key(case)}"
	if manifest.get("run_label") != expected_label:
		raise CampaignError(f"{path}: deterministic run label mismatch")
	if manifest.get("status") != "passed" or manifest.get("phase") != "complete":
		raise CampaignError(f"{path}: E2E wrapper did not report a complete passing run")
	if manifest.get("skip_build") is not True or manifest.get("attested_stage_only") is not True or manifest.get("preflight_only") is not False:
		raise CampaignError(f"{path}: run was not an attested no-build execution")
	provenance = manifest.get("qualification_provenance")
	if not isinstance(provenance, dict) or provenance != {
		"wrapper": spec["wrapper"], "fixed_timeline_scorer": spec["scorer"],
	}:
		raise CampaignError(f"{path}: wrapper/scorer provenance differs from the sealed tools")
	cleanup = manifest.get("cleanup")
	transport = manifest.get("transport")
	build = manifest.get("build")
	input_binding = manifest.get("input")
	if not all(isinstance(item, dict) for item in (cleanup, transport, build, input_binding)):
		raise CampaignError(f"{path}: incomplete manifest roots")
	sender = cleanup.get("sender")
	receiver = cleanup.get("receiver")
	if not isinstance(sender, dict) or not isinstance(receiver, dict):
		raise CampaignError(f"{path}: incomplete cleanup roots")
	if sender.get("mode") != "Off" or sender.get("input_enhancement_profile") != profile or sender.get("auto_adapt") is not False or receiver.get("enabled") is not False:
		raise CampaignError(f"{path}: cleanup/profile contract mismatch")
	if (
		transport.get("host") != "127.0.0.1" or transport.get("voice_transport") != "tcp_tunnel"
		or transport.get("bitrate_bps") != case["bitrate_bps"]
		or transport.get("frames_per_packet") != case["frames_per_packet"]
		or transport.get("transmit_mode") != wrapper_mode(str(case["transmit_mode"]))
		or transport.get("pre_roll_frames") != 0 or transport.get("tail_frames") != 0
		or transport.get("drain_milliseconds") != 1500
	):
		raise CampaignError(f"{path}: transport contract mismatch")
	client = sealed[identity_role(role)]
	server = sealed["server"]
	client_receipt = load_json(Path(client["worktree_receipt"]["path"]), f"{role} worktree receipt")
	server_receipt = load_json(Path(server["worktree_receipt"]["path"]), "server worktree receipt")
	if (
		build.get("git_head") != client["commit"] or build.get("git_dirty") is not False
		or build.get("client_executable_sha256") != client["build_executable"]["sha256"]
		or absolute(Path(str(build.get("client_exe", "")))) != Path(client["build_executable"]["path"])
		or absolute(Path(str(build.get("client_build_dir", "")))) != Path(client["build_executable"]["path"]).parent
		or absolute(Path(str(build.get("client_stage_dir", "")))) != Path(client["stage_payload"]["path"])
		or absolute(Path(str(build.get("source_root", "")))) != Path(str(client_receipt.get("source_root", "")))
		or build.get("server_git_head") != server["commit"] or build.get("server_git_dirty") is not False
		or build.get("server_executable_sha256") != server["executable"]["sha256"]
		or absolute(Path(str(build.get("server_exe", "")))) != Path(server["executable"]["path"])
		or absolute(Path(str(build.get("server_build_dir", "")))) != Path(server["executable"]["path"]).parent
		or absolute(Path(str(build.get("server_source_root", "")))) != Path(str(server_receipt.get("source_root", "")))
	):
		raise CampaignError(f"{path}: build identity mismatch")
	if input_binding.get("unbaselined_voice_contract_control") is not True:
		raise CampaignError(f"{path}: control was not explicitly unbaselined")
	for field, expected in (
		("path", spec["corpus"]["input_wav"]),
		("clean_reference_path", spec["corpus"]["clean_reference_wav"]),
	):
		if absolute(Path(str(input_binding.get(field, "")))) != Path(expected["path"]):
			raise CampaignError(f"{path}: input binding {field} path mismatch")
	if (
		input_binding.get("sha256") != spec["corpus"]["input_wav"]["sha256"]
		or input_binding.get("clean_reference_sha256") != spec["corpus"]["clean_reference_wav"]["sha256"]
	):
		raise CampaignError(f"{path}: input/clean-reference hash mismatch")
	voice = manifest.get("voice_contract_evidence")
	if (
		not isinstance(voice, dict) or voice.get("implementation") != role or voice.get("enhancement_profile") != profile
		or voice.get("bitrate_bps") != case["bitrate_bps"]
		or voice.get("frames_per_packet") != case["frames_per_packet"]
		or voice.get("transmit_mode") != case["transmit_mode"]
	):
		raise CampaignError(f"{path}: voice-contract role/profile mismatch")
	for field in ("model_initialization_attempts", "algorithmic_latency_samples", "fallback_count", "deadline_miss_count"):
		if voice.get(field) != 0:
			raise CampaignError(f"{path}: Original control reported {field}={voice.get(field)!r}")
	if (
		isinstance(voice.get("packet_count"), bool)
		or not isinstance(voice.get("packet_count"), int)
		or voice["packet_count"] <= 0
		or isinstance(voice.get("terminator_count"), bool)
		or not isinstance(voice.get("terminator_count"), int)
		or voice["terminator_count"] <= 0
	):
		raise CampaignError(f"{path}: voice-contract packet/terminator counts are invalid")
	artifacts = manifest.get("artifacts")
	quality = manifest.get("quality")
	completion = manifest.get("completion")
	if not all(isinstance(item, dict) for item in (artifacts, quality, completion)):
		raise CampaignError(f"{path}: artifacts/quality/completion roots are incomplete")
	binding = quality.get("fixed_timeline_binding")
	if binding != {
		"mode": "unbaselined-voice-contract-control",
		"reference_artifact": "input_wav",
		"scored_artifact": "sender_pre_opus_wav",
		"timeline_origin": "source-after-transmitted-preroll",
	}:
		raise CampaignError(f"{path}: fixed-timeline control binding mismatch")
	fixed = quality.get("fixed_timeline")
	if not isinstance(fixed, dict):
		raise CampaignError(f"{path}: fixed-timeline score is missing")
	fixed_path = regular_file(Path(str(artifacts.get("fixed_timeline_score", ""))), "fixed-timeline artifact")
	pre_opus_path = regular_file(Path(str(artifacts.get("sender_pre_opus_wav", ""))), "sender pre-Opus artifact")
	if not is_within(fixed_path, run_root) or not is_within(pre_opus_path, run_root):
		raise CampaignError(f"{path}: fixed-timeline/pre-Opus artifact escaped the run root")
	if load_json(fixed_path, "fixed-timeline artifact") != fixed:
		raise CampaignError(f"{path}: embedded and on-disk fixed-timeline scores differ")
	if (
		fixed.get("schema_version") != 3 or fixed.get("scorer") != "mumble-fixed-timeline-v3"
		or fixed.get("passed") is not True or fixed.get("timeline_alignment") != "fixed"
		or fixed.get("transport_baseline") is not None or fixed.get("declared_latency_samples") != 0
		or fixed.get("reference_sha256") != spec["corpus"]["input_wav"]["sha256"]
		or fixed.get("received_sha256") != file_sha256(pre_opus_path)
		or isinstance(fixed.get("onset_loss_samples"), bool)
		or not isinstance(fixed.get("onset_loss_samples"), int)
		or not 0 <= fixed["onset_loss_samples"] <= 480
		or isinstance(fixed.get("end_loss_samples"), bool)
		or not isinstance(fixed.get("end_loss_samples"), int)
		or not 0 <= fixed["end_loss_samples"] <= 480
		or fixed.get("missing_tail_samples") != 0 or fixed.get("received_clipped_samples") != 0
	):
		raise CampaignError(f"{path}: fixed-timeline control result mismatch")
	return manifest


def next_log_base(logs: Path, key: str, role: str) -> Path:
	attempt = 1
	while (logs / f"{key}-{role}-attempt-{attempt:03d}.stdout.log").exists() or (logs / f"{key}-{role}-attempt-{attempt:03d}.stderr.log").exists():
		attempt += 1
	return logs / f"{key}-{role}-attempt-{attempt:03d}"


def run_one(state: MutableMapping[str, Any], state_path: Path, case: Mapping[str, Any], role: str) -> None:
	for cache_role in (identity_role(role), "server"):
		reference = state["spec"]["build_caches"][cache_role]
		if file_reference(Path(reference["path"])) != reference:
			raise CampaignError(f"{cache_role} CMake cache changed after the campaign was sealed")
	for field in ("wrapper", "scorer"):
		reference = state["spec"][field]
		if file_reference(Path(reference["path"])) != reference:
			raise CampaignError(f"{field} changed after the campaign was sealed")
	for field in ("input_wav", "clean_reference_wav"):
		reference = state["spec"]["corpus"][field]
		if file_reference(Path(reference["path"])) != reference:
			raise CampaignError(f"corpus {field} changed after the campaign was sealed")
	key = case_key(case)
	completed = state.setdefault("completed", {})
	case_state = completed.setdefault(key, {})
	if role in case_state:
		reference = case_state[role]
		if not isinstance(reference, dict):
			raise CampaignError(f"campaign state {key}/{role} reference is invalid")
		path = regular_file(Path(str(reference.get("path", ""))), f"resumed {key}/{role} manifest")
		if file_reference(path) != reference:
			raise CampaignError(f"resumed {key}/{role} manifest differs from its sealed state")
		validate_manifest_minimal(path, case, role, state)
		print(f"Original campaign: resume verified {key}/{role}")
		return
	runs_root = Path(state["runs_root"])
	before = {absolute(path) for path in runs_root.rglob("manifest.json")} if runs_root.exists() else set()
	client = state["sealed"][identity_role(role)]
	server = state["sealed"]["server"]
	command = command_for_case(
		Path(state["spec"]["powershell"]), Path(state["spec"]["wrapper"]["path"]), runs_root,
		state["campaign_id"], case, role, Path(state["spec"]["corpus"]["input_wav"]["path"]),
		Path(state["spec"]["corpus"]["clean_reference_wav"]["path"]),
		Path(client["build_executable"]["path"]).parent, Path(client["stage_payload"]["path"]),
		Path(server["executable"]["path"]).parent, Path(server["executable"]["path"]),
		Path(state["spec"]["scorer"]["path"]), int(state["spec"]["timeout_seconds"]),
	)
	logs = Path(state["logs_root"])
	logs.mkdir(parents=True, exist_ok=True)
	log_base = next_log_base(logs, key, role)
	print(f"Original campaign: running {key}/{role}")
	try:
		result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8", errors="replace")
	except OSError as error:
		raise CampaignError(f"unable to start E2E wrapper for {key}/{role}: {error}") from error
	write_new(log_base.with_suffix(".stdout.log"), result.stdout.encode("utf-8"))
	write_new(log_base.with_suffix(".stderr.log"), result.stderr.encode("utf-8"))
	after = {absolute(path) for path in runs_root.rglob("manifest.json")}
	created = sorted(after - before, key=str)
	if len(created) != 1:
		raise CampaignError(f"{key}/{role}: wrapper created {len(created)} new manifests; expected exactly one")
	manifest_path = created[0]
	if result.returncode != 0:
		raise CampaignError(f"{key}/{role}: E2E wrapper exited {result.returncode}; manifest={manifest_path}")
	validate_manifest_minimal(manifest_path, case, role, state)
	case_state[role] = file_reference(manifest_path)
	write_atomic_mutable(state_path, state)


def initial_state(args: argparse.Namespace, output: Path) -> MutableMapping[str, Any]:
	reject_reparse_components(output.parent, "campaign output parent")
	if not SAFE_ID.fullmatch(args.campaign_id):
		raise CampaignError("campaign ID must match [A-Za-z0-9][A-Za-z0-9._-]{0,127}")
	candidate_commit = args.candidate_commit.lower()
	if not HEX40.fullmatch(candidate_commit):
		raise CampaignError("candidate commit must be a full lowercase 40-character Git SHA")
	expected_candidate_executable = sha256_argument(
		args.expected_candidate_executable_sha256, "--expected-candidate-executable-sha256",
	)
	expected_candidate_payload = sha256_argument(
		args.expected_candidate_stage_payload_sha256, "--expected-candidate-stage-payload-sha256",
	)
	sources = {
		"candidate": directory(args.candidate_source_root, "candidate source root"),
		"legacy": directory(args.legacy_source_root, "legacy source root"),
		"server": directory(args.server_source_root, "server source root"),
	}
	if len({str(path).casefold() for path in sources.values()}) != 3:
		raise CampaignError("candidate, legacy and server source roots must be distinct")
	for root in sources.values():
		if is_within(output, root):
			raise CampaignError("campaign output root must be outside all live source worktrees")
	commits = {"candidate": candidate_commit, "legacy": LEGACY_BUILD_COMMIT, "server": SERVER_COMMIT}
	receipts: dict[str, Mapping[str, Any]] = {
		role: clean_worktree_receipt(sources[role], role, commits[role]) for role in ("candidate", "legacy", "server")
	}
	builds = {
		"candidate": directory(args.candidate_build_dir, "candidate build root"),
		"legacy": directory(args.legacy_build_dir, "legacy build root"),
		"server": directory(args.server_build_dir, "server build root"),
	}
	if is_within(builds["server"], sources["server"]):
		raise CampaignError("OG server build root must be external to its clean source worktree")
	for role in ("candidate", "legacy", "server"):
		verify_build_root(builds[role], sources[role], role)
	server_exe = regular_file(args.server_binary, "OG server executable")
	if server_exe.parent != builds["server"] or server_exe.name.lower() != "mumble-server.exe":
		raise CampaignError("server binary must be mumble-server.exe at the external server build root")
	if file_sha256(server_exe) != SERVER_EXECUTABLE_SHA256:
		raise CampaignError("OG server executable differs from the frozen external server binary")
	paths = {
		"wrapper": regular_file(args.wrapper, "two-client wrapper"),
		"scorer": regular_file(args.scorer, "fixed-timeline scorer"),
		"corpus_lock": regular_file(args.corpus_lock, "corpus lock"),
		"inventory": regular_file(args.corpus_inventory, "corpus inventory"),
		"plan": regular_file(args.mixture_plan, "mixture plan"),
		"render": regular_file(args.render_manifest, "render manifest"),
		"input": regular_file(args.input_wav, "input WAV"),
		"clean": regular_file(args.clean_reference_wav, "clean-reference WAV"),
		"assembler": regular_file(args.assembler, "qualification assembler"),
		"checker": regular_file(args.checker, "Original checker"),
		"powershell": regular_file(args.powershell, "PowerShell executable"),
	}
	fixture, corpus = make_fixture(
		paths["corpus_lock"], paths["inventory"], paths["plan"], paths["render"], paths["input"], paths["clean"],
	)
	output.mkdir(parents=True, exist_ok=False)
	sealed_root = output / "sealed"
	receipt_root = sealed_root / "worktrees"
	for role, receipt in receipts.items():
		write_new(receipt_root / f"{role}.json", json_bytes(receipt))
	fixture_path = output / "fixture-attestation.json"
	write_new(fixture_path, json_bytes(fixture))
	corpus["fixture_attestation"] = file_reference(fixture_path)
	sealed: dict[str, Any] = {}
	for role in ("legacy", "candidate"):
		build_exe = regular_file(builds[role] / "mumble.exe", f"{role} build executable")
		if role == "legacy" and file_sha256(build_exe) != LEGACY_EXECUTABLE_SHA256:
			raise CampaignError("legacy build executable differs from the frozen reference")
		if role == "candidate" and file_sha256(build_exe) != expected_candidate_executable:
			raise CampaignError("candidate build executable differs from the external expected SHA-256 pin")
		stage_source = directory(args.legacy_stage_dir if role == "legacy" else args.candidate_stage_dir, f"{role} source stage")
		stage_source_exe = regular_file(stage_source / "mumble.exe", f"{role} source-stage executable")
		if file_sha256(build_exe) != file_sha256(stage_source_exe):
			raise CampaignError(f"{role} build and source-stage mumble.exe bytes differ")
		stage_source_identity = tree_reference(stage_source)
		expected_payload = LEGACY_STAGE_PAYLOAD_SHA256 if role == "legacy" else expected_candidate_payload
		if stage_source_identity["sha256"] != expected_payload:
			raise CampaignError(f"{role} source-stage payload differs from its external expected SHA-256 pin")
		stage = sealed_root / role / "stage"
		stage_ref = copy_payload_snapshot(stage_source, stage)
		qualified = sealed_root / role / "qualified" / "mumble.exe"
		qualified.parent.mkdir(parents=True)
		shutil.copy2(build_exe, qualified, follow_symlinks=False)
		identity = {
			"commit": commits[role],
			"worktree_receipt": file_reference(receipt_root / f"{role}.json"),
			"build_executable": file_reference(build_exe),
			"stage_executable": file_reference(stage / "mumble.exe"),
			"qualified_executable": file_reference(qualified),
			"stage_payload": stage_ref,
		}
		if role == "legacy":
			identity["instrumentation_base_commit"] = LEGACY_INSTRUMENTATION_BASE_COMMIT
		sealed[role] = identity
	sealed["server"] = {
		"commit": SERVER_COMMIT,
		"worktree_receipt": file_reference(receipt_root / "server.json"),
		"executable": file_reference(server_exe),
	}
	spec = {
		"candidate_commit": candidate_commit,
		"external_expected_hashes": {
			"candidate_executable_sha256": expected_candidate_executable,
			"candidate_stage_payload_sha256": expected_candidate_payload,
			"legacy_executable_sha256": LEGACY_EXECUTABLE_SHA256,
			"legacy_stage_payload_sha256": LEGACY_STAGE_PAYLOAD_SHA256,
			"server_executable_sha256": SERVER_EXECUTABLE_SHA256,
		},
		"origins": {
			"candidate_source_root": str(sources["candidate"]),
			"candidate_build_dir": str(builds["candidate"]),
			"candidate_stage_dir": str(directory(args.candidate_stage_dir, "candidate source stage")),
			"legacy_source_root": str(sources["legacy"]),
			"legacy_build_dir": str(builds["legacy"]),
			"legacy_stage_dir": str(directory(args.legacy_stage_dir, "legacy source stage")),
			"server_source_root": str(sources["server"]),
			"server_build_dir": str(builds["server"]),
			"server_binary": str(server_exe),
		},
		"powershell": str(paths["powershell"]),
		"build_caches": {
			role: file_reference(builds[role] / "CMakeCache.txt") for role in ("candidate", "legacy", "server")
		},
		"wrapper": file_reference(paths["wrapper"]),
		"scorer": file_reference(paths["scorer"]),
		"assembler": file_reference(paths["assembler"]),
		"checker": file_reference(paths["checker"]),
		"timeout_seconds": args.timeout_seconds,
		"corpus": corpus,
		"required_matrix_sha256": canonical_json_sha256(list(REQUIRED_MATRIX)),
	}
	return {
		"schema_version": 1,
		"kind": STATE_KIND,
		"campaign_id": args.campaign_id,
		"spec_sha256": canonical_json_sha256(spec),
		"spec": spec,
		"sealed": sealed,
		"runs_root": str(output / "runs"),
		"logs_root": str(output / "logs"),
		"completed": {},
	}


def validate_resumed_state(args: argparse.Namespace, output: Path, state: MutableMapping[str, Any]) -> None:
	if state.get("schema_version") != 1 or state.get("kind") != STATE_KIND or state.get("campaign_id") != args.campaign_id:
		raise CampaignError("existing campaign state has the wrong schema, kind or campaign ID")
	spec = state.get("spec")
	if not isinstance(spec, dict) or canonical_json_sha256(spec) != state.get("spec_sha256"):
		raise CampaignError("existing campaign state specification hash mismatch")
	if spec.get("candidate_commit") != args.candidate_commit.lower():
		raise CampaignError("existing campaign candidate commit differs from the requested commit")
	expected_hashes = spec.get("external_expected_hashes")
	if not isinstance(expected_hashes, dict) or expected_hashes != {
		"candidate_executable_sha256": sha256_argument(
			args.expected_candidate_executable_sha256, "--expected-candidate-executable-sha256",
		),
		"candidate_stage_payload_sha256": sha256_argument(
			args.expected_candidate_stage_payload_sha256, "--expected-candidate-stage-payload-sha256",
		),
		"legacy_executable_sha256": LEGACY_EXECUTABLE_SHA256,
		"legacy_stage_payload_sha256": LEGACY_STAGE_PAYLOAD_SHA256,
		"server_executable_sha256": SERVER_EXECUTABLE_SHA256,
	}:
		raise CampaignError("resumed campaign external binary/payload pins differ")
	origins = spec.get("origins")
	if not isinstance(origins, dict):
		raise CampaignError("existing campaign origin paths are incomplete")
	for field, requested in (
		("candidate_source_root", args.candidate_source_root),
		("candidate_build_dir", args.candidate_build_dir),
		("candidate_stage_dir", args.candidate_stage_dir),
		("legacy_source_root", args.legacy_source_root),
		("legacy_build_dir", args.legacy_build_dir),
		("legacy_stage_dir", args.legacy_stage_dir),
		("server_source_root", args.server_source_root),
		("server_build_dir", args.server_build_dir),
		("server_binary", args.server_binary),
	):
		if absolute(Path(str(origins.get(field, "")))) != absolute(requested):
			raise CampaignError(f"resumed campaign origin {field} differs")
	if absolute(Path(str(state.get("runs_root", "")))) != output / "runs":
		raise CampaignError("existing campaign runs root is not exactly output/runs")
	if absolute(Path(str(state.get("logs_root", "")))) != output / "logs":
		raise CampaignError("existing campaign logs root is not exactly output/logs")
	reject_reparse_components(output, "campaign output root")
	for field in ("runs_root", "logs_root"):
		path = Path(state[field])
		if path.exists() and not path.is_dir():
			raise CampaignError(f"existing campaign {field} is not a directory")
		reject_reparse_components(path, f"campaign {field}")
	for name, requested in (
		("powershell", args.powershell), ("wrapper", args.wrapper), ("scorer", args.scorer),
		("assembler", args.assembler), ("checker", args.checker),
	):
		if name == "powershell":
			if absolute(Path(str(spec.get(name, "")))) != absolute(requested):
				raise CampaignError(f"resumed campaign {name} path differs")
		elif spec.get(name) != file_reference(regular_file(requested, name)):
			raise CampaignError(f"resumed campaign {name} identity differs")
	if spec.get("timeout_seconds") != args.timeout_seconds:
		raise CampaignError("resumed campaign timeout differs")
	build_caches = spec.get("build_caches")
	if not isinstance(build_caches, dict) or set(build_caches) != {"candidate", "legacy", "server"}:
		raise CampaignError("existing campaign build-cache pins are incomplete")
	for role, requested in (
		("candidate", args.candidate_build_dir), ("legacy", args.legacy_build_dir), ("server", args.server_build_dir),
	):
		reference = build_caches[role]
		if not isinstance(reference, dict) or reference != file_reference(regular_file(requested / "CMakeCache.txt", f"{role} CMake cache")):
			raise CampaignError(f"resumed campaign {role} CMake cache changed")
	corpus = spec.get("corpus")
	if not isinstance(corpus, dict):
		raise CampaignError("existing campaign corpus binding is invalid")
	for field, requested in (
		("corpus_lock", args.corpus_lock),
		("corpus_inventory", args.corpus_inventory),
		("mixture_plan", args.mixture_plan),
		("render_manifest", args.render_manifest),
		("input_wav", args.input_wav),
		("clean_reference_wav", args.clean_reference_wav),
	):
		reference = corpus.get(field)
		if not isinstance(reference, dict) or reference != file_reference(regular_file(requested, field)):
			raise CampaignError(f"resumed campaign corpus {field} identity differs")
	fixture_reference = corpus.get("fixture_attestation")
	if not isinstance(fixture_reference, dict) or fixture_reference != file_reference(Path(str(fixture_reference.get("path", "")))):
		raise CampaignError("resumed campaign fixture attestation changed")
	sealed = state.get("sealed")
	if not isinstance(sealed, dict) or set(sealed) != {"legacy", "candidate", "server"}:
		raise CampaignError("existing campaign sealed identities are incomplete")
	for role, source, commit in (
		("candidate", args.candidate_source_root, args.candidate_commit.lower()),
		("legacy", args.legacy_source_root, LEGACY_BUILD_COMMIT),
		("server", args.server_source_root, SERVER_COMMIT),
	):
		clean_worktree_receipt(source, role, commit)
		identity = sealed[role]
		if not isinstance(identity, dict):
			raise CampaignError(f"resumed {role} identity is invalid")
		if role != "server":
			stage = tree_reference(Path(identity["stage_payload"]["path"]))
			if stage != identity["stage_payload"]:
				raise CampaignError(f"resumed {role} stage snapshot changed")
			for field in ("build_executable", "stage_executable", "qualified_executable", "worktree_receipt"):
				if file_reference(Path(identity[field]["path"])) != identity[field]:
					raise CampaignError(f"resumed {role} {field} changed")
		else:
			for field in ("executable", "worktree_receipt"):
				if file_reference(Path(identity[field]["path"])) != identity[field]:
					raise CampaignError(f"resumed server {field} changed")


def make_bindings(state: Mapping[str, Any]) -> Mapping[str, Any]:
	completed = state.get("completed")
	if not isinstance(completed, dict):
		raise CampaignError("campaign state completed set is invalid")
	cases: list[Mapping[str, Any]] = []
	for case in REQUIRED_MATRIX:
		key = case_key(case)
		case_state = completed.get(key)
		if not isinstance(case_state, dict) or set(case_state) != {"legacy", "original"}:
			raise CampaignError(f"campaign is incomplete at {key}")
		cases.append({**case, "legacy_manifest": case_state["legacy"], "original_manifest": case_state["original"]})
	spec = state["spec"]
	sealed = state["sealed"]
	return {
		"schema_version": 1,
		"kind": BINDING_KIND,
		"campaign_id": state["campaign_id"],
		"candidate_commit": spec["candidate_commit"],
		"identities": {
			"legacy": sealed["legacy"],
			"candidate": sealed["candidate"],
			"server": sealed["server"],
			"tools": {"wrapper": spec["wrapper"], "scorer": spec["scorer"]},
		},
		"transport": {
			"transport_path": "client1-opus-server-client2",
			"server_host": "127.0.0.1",
			"voice_transport": "tcp_tunnel",
			"receiver_cleanup_enabled": False,
			"sender_cleanup_mode": "Off",
			"sender_auto_adapt": False,
			"pre_roll_frames": 0,
			"tail_frames": 0,
			"drain_milliseconds": 1500,
		},
		"corpus": spec["corpus"],
		"cases": cases,
	}


def publish_bindings(output: Path, bindings: Mapping[str, Any]) -> tuple[Path, str]:
	path = output / "campaign-bindings.json"
	payload = json_bytes(bindings)
	if path.exists():
		if path.read_bytes() != payload:
			raise CampaignError("existing sealed campaign-bindings.json differs from completed state")
	else:
		write_new(path, payload)
	digest = hashlib.sha256(payload).hexdigest()
	pin = output / "campaign-bindings.sha256"
	pin_payload = f"{digest}  {path.name}\n".encode("ascii")
	if pin.exists():
		if pin.read_bytes() != pin_payload:
			raise CampaignError("existing external campaign-bindings pin differs")
	else:
		write_new(pin, pin_payload)
	return path, digest


def assemble(state: Mapping[str, Any], bindings_path: Path, bindings_sha: str, publication: Path) -> None:
	publication = absolute(publication)
	publication.mkdir(parents=True, exist_ok=True)
	qualification = publication / "original-voice-qualification.json"
	provenance = publication / "original-voice-provenance.json"
	if qualification.exists() or provenance.exists():
		raise CampaignError("publication root already contains Original qualification output")
	legacy_sha = state["sealed"]["legacy"]["build_executable"]["sha256"]
	command = [
		sys.executable, state["spec"]["assembler"]["path"],
		"--bindings", str(bindings_path), "--bindings-sha256", bindings_sha,
		"--candidate-commit", state["spec"]["candidate_commit"],
		"--legacy-build-commit", LEGACY_BUILD_COMMIT,
		"--legacy-instrumentation-base", LEGACY_INSTRUMENTATION_BASE_COMMIT,
		"--legacy-executable-sha256", legacy_sha,
		"--output", str(qualification), "--provenance-output", str(provenance),
	]
	result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8", errors="replace")
	if result.returncode:
		raise CampaignError(f"qualification assembler failed: {(result.stderr or result.stdout).strip()}")
	check = subprocess.run(
		[
			sys.executable, state["spec"]["checker"]["path"],
			"--base", LEGACY_BUILD_COMMIT, "--head", state["spec"]["candidate_commit"],
			"--qualification", str(qualification),
		],
		stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8", errors="replace",
	)
	if check.returncode:
		raise CampaignError(f"assembled qualification checker failed: {(check.stderr or check.stdout).strip()}")
	print(result.stdout.strip())
	print(check.stdout.strip())


def run_self_test() -> None:
	if len(REQUIRED_MATRIX) != 45 or REQUIRED_MATRIX[0] != {"bitrate_bps": 8000, "frames_per_packet": 1, "transmit_mode": "continuous"}:
		raise AssertionError("required 45-case matrix regression")
	if REQUIRED_MATRIX[-1] != {"bitrate_bps": 128000, "frames_per_packet": 4, "transmit_mode": "vad"}:
		raise AssertionError("required matrix order regression")
	if identity_role("legacy") != "legacy" or identity_role("original") != "candidate":
		raise AssertionError("implementation-to-sealed-identity mapping regression")
	try:
		identity_role("candidate")
	except CampaignError:
		pass
	else:
		raise AssertionError("implementation-to-sealed-identity mapping accepted an unknown role")
	with tempfile.TemporaryDirectory(prefix="mumble-original-campaign-") as temporary:
		root = Path(temporary)
		stage = root / "stage"
		(stage / "nested").mkdir(parents=True)
		(stage / "mumble.exe").write_bytes(b"client")
		(stage / "nested" / "runtime.dll").write_bytes(b"runtime")
		snapshot = root / "snapshot"
		attestation = copy_payload_snapshot(stage, snapshot)
		if attestation["sha256"] != tree_reference(stage)["sha256"] or (snapshot / "mumble.exe").read_bytes() != b"client":
			raise AssertionError("independent payload snapshot regression")
		command = command_for_case(
			Path("pwsh.exe"), Path("wrapper.ps1"), root / "runs", "selftest", REQUIRED_MATRIX[0], "legacy",
			Path("input.wav"), Path("clean.wav"), Path("build"), snapshot,
			Path("server-build"), Path("server-build/mumble-server.exe"), Path("scorer.py"), 180,
		)
		for required in (
			"-AttestedStageOnly", "-UnbaselinedVoiceContractControl", "-RequireVoiceContractEvidence",
			"-DisableSenderAutoAdapt", "-SkipBuild",
		):
			if required not in command:
				raise AssertionError(f"case command omitted {required}")
		logs = root / "logs"
		logs.mkdir()
		first_log = next_log_base(logs, "case", "original")
		first_log.with_suffix(".stdout.log").write_bytes(b"failed attempt")
		if next_log_base(logs, "case", "original").name != "case-original-attempt-002":
			raise AssertionError("failed-run retry did not advance to an immutable attempt log")
		duplicate = root / "duplicate.json"
		duplicate.write_text('{"schema_version":1,"schema_version":1}\n', encoding="utf-8")
		try:
			load_json(duplicate, "duplicate self-test")
		except CampaignError:
			pass
		else:
			raise AssertionError("strict JSON loader accepted duplicate keys")
	if canonical_json_sha256(list(REQUIRED_MATRIX)) != "afd474298f8331474a9c7d36e083121526fb984a4d65e284bf65a878b7002f3f":
		raise AssertionError("required matrix canonical hash changed unexpectedly")


def parser() -> argparse.ArgumentParser:
	result = argparse.ArgumentParser(description=__doc__)
	mode = result.add_mutually_exclusive_group()
	mode.add_argument("--prepare-only", action="store_true", help="seal inputs without starting clients")
	mode.add_argument("--execute", action="store_true", help="run missing pairs, bind and assemble")
	mode.add_argument("--assemble-only", action="store_true", help="assemble an already complete campaign")
	mode.add_argument("--self-test", action="store_true")
	result.add_argument("--resume", action="store_true", help="reuse an existing matching campaign state")
	result.add_argument("--campaign-id")
	result.add_argument("--output-root", type=Path)
	result.add_argument("--publication-root", type=Path)
	result.add_argument("--candidate-commit")
	result.add_argument("--expected-candidate-executable-sha256")
	result.add_argument("--expected-candidate-stage-payload-sha256")
	result.add_argument("--candidate-source-root", type=Path)
	result.add_argument("--candidate-build-dir", type=Path)
	result.add_argument("--candidate-stage-dir", type=Path)
	result.add_argument("--legacy-source-root", type=Path)
	result.add_argument("--legacy-build-dir", type=Path)
	result.add_argument("--legacy-stage-dir", type=Path)
	result.add_argument("--server-source-root", type=Path)
	result.add_argument("--server-build-dir", type=Path)
	result.add_argument("--server-binary", type=Path)
	result.add_argument("--wrapper", type=Path)
	result.add_argument("--powershell", type=Path)
	result.add_argument("--scorer", type=Path, default=Path(__file__).with_name("score-fixed-timeline.py"))
	result.add_argument("--assembler", type=Path, default=Path(__file__).with_name("assemble-original-voice-qualification.py"))
	result.add_argument("--checker", type=Path, default=Path(__file__).with_name("check-original-voice-contract.py"))
	result.add_argument("--corpus-lock", type=Path)
	result.add_argument("--corpus-inventory", type=Path)
	result.add_argument("--mixture-plan", type=Path)
	result.add_argument("--render-manifest", type=Path)
	result.add_argument("--input-wav", type=Path)
	result.add_argument("--clean-reference-wav", type=Path)
	result.add_argument("--timeout-seconds", type=int, default=180)
	return result


def main(argv: Sequence[str] | None = None) -> int:
	args = parser().parse_args(argv)
	try:
		if args.self_test:
			run_self_test()
			print("Original voice campaign runner self-test: ok")
			return 0
		if not (args.prepare_only or args.execute or args.assemble_only):
			raise CampaignError("choose --prepare-only, --execute, --assemble-only, or --self-test")
		required = (
			"campaign_id", "output_root", "candidate_commit", "candidate_source_root", "candidate_build_dir",
			"expected_candidate_executable_sha256", "expected_candidate_stage_payload_sha256",
			"candidate_stage_dir", "legacy_source_root", "legacy_build_dir", "legacy_stage_dir",
			"server_source_root", "server_build_dir", "server_binary", "wrapper", "powershell",
			"corpus_lock", "corpus_inventory", "mixture_plan", "render_manifest", "input_wav",
			"clean_reference_wav",
		)
		missing = [f"--{name.replace('_', '-')}" for name in required if getattr(args, name) is None]
		if missing:
			raise CampaignError("required arguments are missing: " + ", ".join(missing))
		if not 15 <= args.timeout_seconds <= 900:
			raise CampaignError("--timeout-seconds must be between 15 and 900")
		output = absolute(args.output_root)
		state_path = output / "campaign-state.json"
		if output.exists():
			if not args.resume:
				raise CampaignError("output root already exists; pass --resume only for the exact same campaign")
			state = dict(load_json(state_path, "campaign state"))
			validate_resumed_state(args, output, state)
		else:
			if args.resume or args.assemble_only:
				raise CampaignError("cannot resume/assemble a campaign whose output root does not exist")
			state = initial_state(args, output)
			write_new(state_path, json_bytes(state))
			print(f"Original campaign prepared: {output}")
		if args.prepare_only:
			return 0
		if args.execute:
			for case in REQUIRED_MATRIX:
				for role in ("legacy", "original"):
					run_one(state, state_path, case, role)
		bindings = make_bindings(state)
		bindings_path, bindings_sha = publish_bindings(output, bindings)
		publication = absolute(args.publication_root) if args.publication_root else output / "publication"
		if not is_within(publication, output):
			raise CampaignError("publication root must remain below the external campaign output root")
		assemble(state, bindings_path, bindings_sha, publication)
		print(f"Original campaign bindings: {bindings_path}")
		print(f"Original campaign bindings SHA-256: {bindings_sha}")
		print(f"Original campaign publication: {publication}")
		return 0
	except (AssertionError, CampaignError, PayloadIdentityError, OSError, CHECKER.ContractError) as error:
		print(f"Original voice campaign error: {error}", file=sys.stderr)
		return 1


if __name__ == "__main__":
	raise SystemExit(main())
