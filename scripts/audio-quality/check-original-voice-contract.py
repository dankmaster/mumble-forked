#!/usr/bin/env python3
"""Verify that Input Enhancement's Original profile preserves Mumble's voice path.

This checker deliberately has two independent responsibilities:

* source contract: compare the Opus encoder, packet assembly, packet transport,
  and protected receive/server files with a trusted base revision;
* qualification contract: reject an Original-vs-legacy evidence file unless it
  contains the complete bitrate/frame/transmit-mode matrix, byte-identical
  input/pre-Opus PCM and Opus packet hashes, zero enhancement initialization
  and latency, and passing fixed-timeline receiver evidence.

Live client-2 WAV hashes are retained as attestations but deliberately are not
compared across separate runs. The receiver jitter/mixer can legitimately add
or remove a 10 ms capture frame even when the protected Opus payload is byte
identical. Receiver correctness is instead gated on its attested fixed
timeline, complete tail, and clipping result.

The qualification validator does not manufacture evidence. The local/CI E2E
harness must produce the JSON that is passed via ``--qualification``.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path, PurePosixPath
from typing import Any, Dict, Iterable, List, Mapping, Sequence, Tuple


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
AUDIO_INPUT_PATH = "src/mumble/AudioInput.cpp"

PROTECTED_EXACT_PATHS = {
	"src/Mumble.proto",
	"src/MumbleProtocol.cpp",
	"src/MumbleProtocol.h",
	"src/PacketDataStream.cpp",
	"src/PacketDataStream.h",
	"src/mumble/ServerHandler.cpp",
	"src/mumble/ServerHandler.h",
}
PROTECTED_PREFIXES = (
	"src/murmur/",
	"src/mumble/AudioOutput",
)

PROTECTED_FUNCTIONS = (
	("Opus encoder", "int AudioInput::encodeOpusFrame("),
	("voice transport", "static void sendAudioFrame("),
	("voice packet construction", "void AudioInput::flushCheck("),
)
PACKET_ASSEMBLY_START = "\tEncodingOutputBuffer buffer;"
PACKET_ASSEMBLY_END = "\n\tbPreviousVoice = bIsSpeech;"

BITRATES_BPS = (8000, 16000, 40000, 64000, 128000)
FRAMES_PER_PACKET = (1, 2, 4)
TRANSMIT_MODES = ("continuous", "push_to_talk", "vad")
SHA256_PATTERN = re.compile(r"^[0-9a-fA-F]{64}$")
RECEIVER_MAX_EDGE_LOSS_SAMPLES = 480


class ContractError(RuntimeError):
	"""Raised when source or qualification evidence violates the contract."""


def normalize_repo_path(value: str) -> str:
	path = value.strip().replace("\\", "/")
	while path.startswith("./"):
		path = path[2:]
	parsed = PurePosixPath(path)
	if not path or parsed.is_absolute() or ".." in parsed.parts:
		raise ContractError(f"unsafe repository path: {value!r}")
	return parsed.as_posix()


def run_git(arguments: Sequence[str]) -> str:
	try:
		completed = subprocess.run(
			["git", *arguments],
			cwd=REPOSITORY_ROOT,
			check=True,
			capture_output=True,
			text=True,
			encoding="utf-8",
		)
	except (OSError, subprocess.CalledProcessError) as error:
		details = error.stderr.strip() if isinstance(error, subprocess.CalledProcessError) and error.stderr else str(error)
		raise ContractError(f"git {' '.join(arguments)} failed: {details}") from error
	return completed.stdout


def source_at(path: str, revision: str | None) -> str:
	if revision:
		content = run_git(["show", f"{revision}:{path}"])
	else:
		try:
			content = (REPOSITORY_ROOT / path).read_text(encoding="utf-8")
		except OSError as error:
			raise ContractError(f"unable to read working-tree file {path}: {error}") from error
	return content.replace("\r\n", "\n")


def changed_paths(base: str, head: str | None) -> List[str]:
	arguments = ["diff", "--name-only", "--diff-filter=ACDMRT", base]
	if head:
		arguments.append(head)
	arguments.append("--")
	paths = {normalize_repo_path(line) for line in run_git(arguments).splitlines() if line.strip()}
	if not head:
		paths.update(
			normalize_repo_path(line)
			for line in run_git(["ls-files", "--others", "--exclude-standard"]).splitlines()
			if line.strip()
		)
	return sorted(paths)


def is_protected_file(path: str) -> bool:
	return path in PROTECTED_EXACT_PATHS or any(path.startswith(prefix) for prefix in PROTECTED_PREFIXES)


def extract_cpp_function(source: str, signature: str) -> str:
	if source.count(signature) != 1:
		raise ContractError(f"expected exactly one C++ function signature {signature!r}")
	start = source.index(signature)
	opening_brace = source.find("{", start + len(signature))
	if opening_brace < 0:
		raise ContractError(f"unable to find opening brace for {signature!r}")

	depth = 0
	index = opening_brace
	state = "code"
	while index < len(source):
		character = source[index]
		next_character = source[index + 1] if index + 1 < len(source) else ""

		if state == "line_comment":
			if character == "\n":
				state = "code"
		elif state == "block_comment":
			if character == "*" and next_character == "/":
				state = "code"
				index += 1
		elif state in ("string", "character"):
			if character == "\\":
				index += 1
			elif (state == "string" and character == '"') or (state == "character" and character == "'"):
				state = "code"
		else:
			if character == "/" and next_character == "/":
				state = "line_comment"
				index += 1
			elif character == "/" and next_character == "*":
				state = "block_comment"
				index += 1
			elif character == '"':
				state = "string"
			elif character == "'":
				state = "character"
			elif character == "{":
				depth += 1
			elif character == "}":
				depth -= 1
				if depth == 0:
					return source[start : index + 1]
		index += 1

	raise ContractError(f"unterminated C++ function {signature!r}")


def extract_packet_assembly(source: str) -> str:
	if source.count(PACKET_ASSEMBLY_START) != 1:
		raise ContractError("unable to identify the unique Opus packet-assembly start marker")
	start = source.index(PACKET_ASSEMBLY_START)
	end = source.find(PACKET_ASSEMBLY_END, start)
	if end < 0:
		raise ContractError("unable to identify the Opus packet-assembly end marker")
	return source[start:end]


def content_hash(content: str) -> str:
	return hashlib.sha256(content.encode("utf-8")).hexdigest()


def verify_source_contract(base: str, head: str | None) -> List[Tuple[str, str]]:
	changed_protected = [path for path in changed_paths(base, head) if is_protected_file(path)]
	if changed_protected:
		raise ContractError(
			"protected receive/server/transport files changed: " + ", ".join(changed_protected)
		)

	baseline = source_at(AUDIO_INPUT_PATH, base)
	candidate = source_at(AUDIO_INPUT_PATH, head)
	verified: List[Tuple[str, str]] = []
	for label, signature in PROTECTED_FUNCTIONS:
		baseline_function = extract_cpp_function(baseline, signature)
		candidate_function = extract_cpp_function(candidate, signature)
		if candidate_function != baseline_function:
			raise ContractError(
				f"{label} changed relative to {base}: "
				f"base={content_hash(baseline_function)} candidate={content_hash(candidate_function)}"
			)
		verified.append((label, content_hash(candidate_function)))

	baseline_assembly = extract_packet_assembly(baseline)
	candidate_assembly = extract_packet_assembly(candidate)
	if candidate_assembly != baseline_assembly:
		raise ContractError(
			f"Opus buffering/packet assembly changed relative to {base}: "
			f"base={content_hash(baseline_assembly)} candidate={content_hash(candidate_assembly)}"
		)
	verified.append(("Opus buffering/packet assembly", content_hash(candidate_assembly)))
	return verified


def required_matrix() -> List[Dict[str, Any]]:
	return [
		{
			"bitrate_bps": bitrate,
			"frames_per_packet": frames,
			"transmit_mode": mode,
		}
		for bitrate in BITRATES_BPS
		for frames in FRAMES_PER_PACKET
		for mode in TRANSMIT_MODES
	]


def require_integer(case: Mapping[str, Any], field: str, case_name: str) -> int:
	value = case.get(field)
	if isinstance(value, bool) or not isinstance(value, int):
		raise ContractError(f"{case_name}: {field} must be an integer")
	return value


def require_sha256(case: Mapping[str, Any], field: str, case_name: str) -> str:
	value = case.get(field)
	if not isinstance(value, str) or not SHA256_PATTERN.fullmatch(value):
		raise ContractError(f"{case_name}: {field} must be a SHA-256 hex digest")
	return value.lower()


def require_receiver_timeline(case: Mapping[str, Any], implementation: str, case_name: str) -> int:
	prefix = f"{implementation}_receiver"
	if case.get(f"{prefix}_fixed_timeline_passed") is not True:
		raise ContractError(f"{case_name}: {implementation} receiver fixed-timeline score did not pass")

	received_samples = require_integer(case, f"{implementation}_received_sample_count", case_name)
	if received_samples <= 0:
		raise ContractError(f"{case_name}: {implementation} receiver sample count must be positive")

	for edge in ("onset", "end"):
		loss = require_integer(case, f"{prefix}_{edge}_loss_samples", case_name)
		if loss < 0 or loss > RECEIVER_MAX_EDGE_LOSS_SAMPLES:
			raise ContractError(
				f"{case_name}: {implementation} receiver {edge} loss {loss} exceeds "
				f"{RECEIVER_MAX_EDGE_LOSS_SAMPLES} samples"
			)

	if require_integer(case, f"{prefix}_missing_tail_samples", case_name) != 0:
		raise ContractError(f"{case_name}: {implementation} receiver is missing the drained tail")
	if require_integer(case, f"{prefix}_clipped_samples", case_name) != 0:
		raise ContractError(f"{case_name}: {implementation} receiver introduced clipping")
	return received_samples


def validate_qualification(document: Mapping[str, Any]) -> int:
	if document.get("schema_version") != 1:
		raise ContractError("qualification schema_version must be 1")
	if document.get("profile") != "Original":
		raise ContractError("qualification profile must be Original")
	if document.get("transport_path") != "client1-opus-server-client2":
		raise ContractError("qualification must use the client1-opus-server-client2 transport path")
	if document.get("server_host") != "127.0.0.1":
		raise ContractError("qualification server_host must be 127.0.0.1")
	if document.get("receiver_cleanup_enabled") is not False:
		raise ContractError("receiver cleanup must be explicitly disabled")
	for field in ("legacy_build_sha", "candidate_build_sha"):
		value = document.get(field)
		if not isinstance(value, str) or not re.fullmatch(r"[0-9a-fA-F]{7,64}", value):
			raise ContractError(f"qualification {field} must be a Git commit hex digest")
	corpus_sha256 = document.get("corpus_sha256")
	if not isinstance(corpus_sha256, str) or not SHA256_PATTERN.fullmatch(corpus_sha256):
		raise ContractError("qualification corpus_sha256 must be a SHA-256 hex digest")
	legacy_executable_sha256 = require_sha256(document, "legacy_executable_sha256", "qualification")
	candidate_executable_sha256 = require_sha256(document, "candidate_executable_sha256", "qualification")

	cases = document.get("cases")
	if not isinstance(cases, list):
		raise ContractError("qualification cases must be an array")

	required = {
		(item["bitrate_bps"], item["frames_per_packet"], item["transmit_mode"])
		for item in required_matrix()
	}
	seen = set()
	for index, raw_case in enumerate(cases):
		if not isinstance(raw_case, dict):
			raise ContractError(f"case {index}: expected an object")
		bitrate = require_integer(raw_case, "bitrate_bps", f"case {index}")
		frames = require_integer(raw_case, "frames_per_packet", f"case {index}")
		mode = raw_case.get("transmit_mode")
		key = (bitrate, frames, mode)
		case_name = f"case {bitrate}/{frames}/{mode}"
		if key not in required:
			raise ContractError(f"{case_name}: unexpected matrix combination")
		if key in seen:
			raise ContractError(f"{case_name}: duplicate matrix combination")
		seen.add(key)

		if raw_case.get("enhancement_profile") != "Original":
			raise ContractError(f"{case_name}: enhancement_profile must be Original")
		if require_integer(raw_case, "model_initialization_attempts", case_name) != 0:
			raise ContractError(f"{case_name}: Original initialized an enhancement model")
		if require_integer(raw_case, "algorithmic_latency_samples", case_name) != 0:
			raise ContractError(f"{case_name}: Original added algorithmic latency")
		if require_integer(raw_case, "fallback_count", case_name) != 0:
			raise ContractError(f"{case_name}: Original reported an unexplained fallback")
		if require_integer(raw_case, "deadline_miss_count", case_name) != 0:
			raise ContractError(f"{case_name}: Original reported a processing deadline miss")
		if require_sha256(raw_case, "legacy_executable_sha256", case_name) != legacy_executable_sha256:
			raise ContractError(f"{case_name}: legacy executable differs from the qualification identity")
		if require_sha256(raw_case, "candidate_executable_sha256", case_name) != candidate_executable_sha256:
			raise ContractError(f"{case_name}: candidate executable differs from the qualification identity")

		input_pcm = require_sha256(raw_case, "input_pcm_sha256", case_name)
		legacy_input_pcm = require_sha256(raw_case, "legacy_input_pcm_sha256", case_name)
		original_input_pcm = require_sha256(raw_case, "original_input_pcm_sha256", case_name)
		if legacy_input_pcm != original_input_pcm or input_pcm != original_input_pcm:
			raise ContractError(f"{case_name}: Original input PCM differs from legacy")

		legacy_pcm = require_sha256(raw_case, "legacy_pcm_sha256", case_name)
		original_pcm = require_sha256(raw_case, "original_pcm_sha256", case_name)
		if legacy_pcm != original_pcm:
			raise ContractError(f"{case_name}: Original PCM differs from legacy")

		legacy_opus = require_sha256(raw_case, "legacy_opus_packets_sha256", case_name)
		original_opus = require_sha256(raw_case, "original_opus_packets_sha256", case_name)
		if legacy_opus != original_opus:
			raise ContractError(f"{case_name}: Original Opus packets differ from legacy")

		# Preserve both live-capture hashes for attestation. They are intentionally
		# not compared: separate localhost runs can differ by a jitter-buffer frame
		# even when the protected Opus payload above is byte-identical.
		require_sha256(raw_case, "legacy_received_pcm_sha256", case_name)
		require_sha256(raw_case, "original_received_pcm_sha256", case_name)
		legacy_received_samples = require_receiver_timeline(raw_case, "legacy", case_name)
		original_received_samples = require_receiver_timeline(raw_case, "original", case_name)
		jitter_delta = require_integer(raw_case, "receiver_jitter_delta_samples", case_name)
		if jitter_delta != original_received_samples - legacy_received_samples:
			raise ContractError(
				f"{case_name}: receiver_jitter_delta_samples does not match the attested capture lengths"
			)

		for metric in ("packet_count", "terminator_count"):
			legacy_value = require_integer(raw_case, f"legacy_{metric}", case_name)
			original_value = require_integer(raw_case, f"original_{metric}", case_name)
			if legacy_value != original_value:
				raise ContractError(f"{case_name}: Original {metric} differs from legacy")

	missing = required - seen
	if missing:
		preview = ", ".join(f"{bitrate}/{frames}/{mode}" for bitrate, frames, mode in sorted(missing)[:5])
		raise ContractError(f"qualification is missing {len(missing)} matrix cases (first: {preview})")
	return len(seen)


def passing_self_test_document() -> Dict[str, Any]:
	digest = "a" * 64
	legacy_received_digest = "b" * 64
	original_received_digest = "c" * 64
	legacy_executable_digest = "d" * 64
	candidate_executable_digest = "e" * 64
	cases = []
	for item in required_matrix():
		cases.append(
			{
				**item,
				"enhancement_profile": "Original",
				"model_initialization_attempts": 0,
				"algorithmic_latency_samples": 0,
				"fallback_count": 0,
				"deadline_miss_count": 0,
				"legacy_executable_sha256": legacy_executable_digest,
				"candidate_executable_sha256": candidate_executable_digest,
				"input_pcm_sha256": digest,
				"legacy_input_pcm_sha256": digest,
				"original_input_pcm_sha256": digest,
				"legacy_pcm_sha256": digest,
				"original_pcm_sha256": digest,
				"legacy_opus_packets_sha256": digest,
				"original_opus_packets_sha256": digest,
				"legacy_received_pcm_sha256": legacy_received_digest,
				"original_received_pcm_sha256": original_received_digest,
				"legacy_received_sample_count": 10000,
				"original_received_sample_count": 10480,
				"receiver_jitter_delta_samples": 480,
				"legacy_receiver_fixed_timeline_passed": True,
				"legacy_receiver_onset_loss_samples": 0,
				"legacy_receiver_end_loss_samples": 480,
				"legacy_receiver_missing_tail_samples": 0,
				"legacy_receiver_clipped_samples": 0,
				"original_receiver_fixed_timeline_passed": True,
				"original_receiver_onset_loss_samples": 480,
				"original_receiver_end_loss_samples": 0,
				"original_receiver_missing_tail_samples": 0,
				"original_receiver_clipped_samples": 0,
				"legacy_packet_count": 10,
				"original_packet_count": 10,
				"legacy_terminator_count": 1,
				"original_terminator_count": 1,
			}
		)
	return {
		"schema_version": 1,
		"profile": "Original",
		"transport_path": "client1-opus-server-client2",
		"server_host": "127.0.0.1",
		"receiver_cleanup_enabled": False,
		"legacy_build_sha": "1" * 40,
		"candidate_build_sha": "2" * 40,
		"legacy_executable_sha256": legacy_executable_digest,
		"candidate_executable_sha256": candidate_executable_digest,
		"corpus_sha256": digest,
		"cases": cases,
	}


def run_self_test() -> None:
	sample = """int Example::function() {
	const char *text = "not a } brace";
	// } neither is this
	if (true) { /* } */ return 1; }
}
int unrelated() { return 2; }
"""
	extracted = extract_cpp_function(sample, "int Example::function(")
	if "unrelated" in extracted or not extracted.endswith("}"):
		raise AssertionError("C++ function extraction regression")

	document = passing_self_test_document()
	if validate_qualification(document) != 45:
		raise AssertionError("qualification matrix size regression")

	broken = json.loads(json.dumps(document))
	broken["cases"][0]["original_pcm_sha256"] = "b" * 64
	try:
		validate_qualification(broken)
	except ContractError:
		pass
	else:
		raise AssertionError("PCM mismatch was not rejected")

	broken_input = json.loads(json.dumps(document))
	broken_input["cases"][0]["original_input_pcm_sha256"] = "d" * 64
	try:
		validate_qualification(broken_input)
	except ContractError:
		pass
	else:
		raise AssertionError("input PCM mismatch was not rejected")

	for label, mutate in (
		("missing candidate executable identity", lambda value: value.pop("candidate_executable_sha256")),
		("missing legacy executable identity", lambda value: value.pop("legacy_executable_sha256")),
		(
			"case candidate executable mismatch",
			lambda value: value["cases"][0].__setitem__("candidate_executable_sha256", "f" * 64),
		),
		(
			"case legacy executable mismatch",
			lambda value: value["cases"][0].__setitem__("legacy_executable_sha256", "f" * 64),
		),
	):
		broken_executable = json.loads(json.dumps(document))
		mutate(broken_executable)
		try:
			validate_qualification(broken_executable)
		except ContractError:
			pass
		else:
			raise AssertionError(f"{label} was not rejected")

	for label, mutate in (
		("failed receiver timeline", lambda case: case.__setitem__("legacy_receiver_fixed_timeline_passed", False)),
		("receiver edge loss", lambda case: case.__setitem__("original_receiver_onset_loss_samples", 481)),
		("receiver tail loss", lambda case: case.__setitem__("legacy_receiver_missing_tail_samples", 1)),
		("receiver clipping", lambda case: case.__setitem__("original_receiver_clipped_samples", 1)),
		("receiver jitter delta", lambda case: case.__setitem__("receiver_jitter_delta_samples", 0)),
	):
		broken_receiver = json.loads(json.dumps(document))
		mutate(broken_receiver["cases"][0])
		try:
			validate_qualification(broken_receiver)
		except ContractError:
			pass
		else:
			raise AssertionError(f"{label} was not rejected")

	missing = json.loads(json.dumps(document))
	missing["cases"].pop()
	try:
		validate_qualification(missing)
	except ContractError:
		pass
	else:
		raise AssertionError("incomplete matrix was not rejected")


def load_qualification(path: Path) -> Mapping[str, Any]:
	try:
		value = json.loads(path.read_text(encoding="utf-8"))
	except (OSError, json.JSONDecodeError) as error:
		raise ContractError(f"unable to read qualification {path}: {error}") from error
	if not isinstance(value, dict):
		raise ContractError("qualification root must be an object")
	return value


def main(argv: Sequence[str] | None = None) -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--base", help="trusted base Git revision for the source contract")
	parser.add_argument("--head", help="candidate Git revision; defaults to the working tree")
	parser.add_argument("--qualification", type=Path, help="Original-vs-legacy E2E qualification JSON")
	parser.add_argument("--print-required-matrix", action="store_true", help="print the required 45-case matrix")
	parser.add_argument("--self-test", action="store_true", help="run parser and evidence-validator regressions")
	args = parser.parse_args(argv)

	try:
		did_work = False
		if args.self_test:
			run_self_test()
			print("original voice contract self-test: ok")
			did_work = True
		if args.print_required_matrix:
			print(json.dumps({"schema_version": 1, "required_cases": required_matrix()}, indent=2))
			did_work = True
		if args.base:
			verified = verify_source_contract(args.base, args.head)
			for label, digest in verified:
				print(f"original voice source contract: {label}: {digest}")
			print(f"original voice source contract: ok against {args.base}")
			did_work = True
		elif args.head:
			raise ContractError("--head requires --base")
		if args.qualification:
			case_count = validate_qualification(load_qualification(args.qualification))
			print(f"original voice qualification contract: ok; cases={case_count}")
			did_work = True
		if not did_work:
			raise ContractError("provide --base, --qualification, --self-test, or --print-required-matrix")
		return 0
	except (AssertionError, ContractError) as error:
		print(f"original voice contract: error: {error}", file=sys.stderr)
		return 1


if __name__ == "__main__":
	sys.exit(main())
