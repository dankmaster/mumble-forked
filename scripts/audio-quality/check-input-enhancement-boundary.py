#!/usr/bin/env python3
"""Keep input-enhancement work separate from Mumble transport, server, and receive paths."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import PurePosixPath
from typing import Iterable, List, Sequence, Tuple


RUNTIME_EXACT_PATHS = {
	"src/mumble/AudioInput.cpp",
	"src/mumble/AudioInput.h",
}
PROTECTED_EXACT_PATHS = {
	"src/Mumble.proto",
	"src/MumbleProtocol.cpp",
	"src/MumbleProtocol.h",
	"src/PacketDataStream.cpp",
	"src/PacketDataStream.h",
	"src/mumble/ServerHandler.cpp",
	"src/mumble/ServerHandler.h",
}


def normalize_repo_path(value: str) -> str:
	path = value.strip().replace("\\", "/")
	while path.startswith("./"):
		path = path[2:]
	parsed = PurePosixPath(path)
	if not path or parsed.is_absolute() or ".." in parsed.parts:
		raise ValueError(f"unsafe repository path: {value!r}")
	return parsed.as_posix()


def is_input_enhancement_runtime(path: str) -> bool:
	if path in RUNTIME_EXACT_PATHS:
		return True
	if not path.startswith("src/mumble/"):
		return False
	name = PurePosixPath(path).name
	return "InputEnhancement" in name or "SpeechCleanup" in name


def is_protected_transport_or_server(path: str) -> bool:
	return (
		path in PROTECTED_EXACT_PATHS
		or path.startswith("src/murmur/")
		or path.startswith("src/mumble/AudioOutput")
	)


def classify_changes(paths: Iterable[str]) -> Tuple[List[str], List[str]]:
	normalized = sorted({ normalize_repo_path(path) for path in paths })
	runtime = [path for path in normalized if is_input_enhancement_runtime(path)]
	protected = [path for path in normalized if is_protected_transport_or_server(path)]
	return runtime, protected


def changed_files(base: str, head: str) -> List[str]:
	command = [ "git", "diff", "--name-only", "--diff-filter=ACMRT", f"{base}...{head}", "--" ]
	try:
		completed = subprocess.run(command, check=True, capture_output=True, text=True, encoding="utf-8")
	except (OSError, subprocess.CalledProcessError) as error:
		details = error.stderr.strip() if isinstance(error, subprocess.CalledProcessError) and error.stderr else str(error)
		raise RuntimeError(f"unable to inspect changed files: {details}") from error
	return [line for line in completed.stdout.splitlines() if line.strip()]


def run_self_test() -> None:
	cases = [
		([ "src/mumble/InputEnhancementPipeline.cpp" ], False),
		([ "src/Mumble.proto" ], False),
		([ "src/tests/TestSpeechCleanup/TestSpeechCleanup.cpp", "src/Mumble.proto" ], False),
		([ "src/mumble/AudioInput.cpp", "src/Mumble.proto" ], True),
		([ "src/mumble/RNNoiseSpeechCleanup.cpp", "src/murmur/Messages.cpp" ], True),
		([ "src\\mumble\\SpeechCleanupProcessor.h", "src\\mumble\\AudioOutputSpeech.cpp" ], True),
	]
	for paths, expected_conflict in cases:
		runtime, protected = classify_changes(paths)
		actual_conflict = bool(runtime and protected)
		if actual_conflict != expected_conflict:
			raise AssertionError(
				f"boundary self-test failed for {paths!r}: expected conflict={expected_conflict}, "
				f"got runtime={runtime!r}, protected={protected!r}"
			)


def main(argv: Sequence[str] | None = None) -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--base", help="base commit/ref used with a three-dot Git diff")
	parser.add_argument("--head", default="HEAD", help="head commit/ref (default: HEAD)")
	parser.add_argument(
		"--changed-file",
		action="append",
		default=[],
		help="classify this path instead of invoking Git; may be repeated",
	)
	parser.add_argument("--self-test", action="store_true", help="run built-in classification regression tests first")
	args = parser.parse_args(argv)

	try:
		if args.self_test:
			run_self_test()
			print("input-enhancement boundary self-test: ok")

		if args.changed_file and args.base:
			raise ValueError("--changed-file and --base are mutually exclusive")
		if args.changed_file:
			paths = args.changed_file
		elif args.base:
			paths = changed_files(args.base, args.head)
		elif args.self_test:
			return 0
		else:
			raise ValueError("provide --base or at least one --changed-file")

		runtime, protected = classify_changes(paths)
		if runtime and protected:
			print("input-enhancement boundary: rejected mixed change", file=sys.stderr)
			print("Input-enhancement runtime files:", file=sys.stderr)
			for path in runtime:
				print(f"  - {path}", file=sys.stderr)
			print("Protected transport/server/receive files:", file=sys.stderr)
			for path in protected:
				print(f"  - {path}", file=sys.stderr)
			print(
				"Split the protected transport/server/receive changes into a separate PR so the legacy voice path can be "
				"reviewed and qualified independently.",
				file=sys.stderr,
			)
			return 1

		print(
			f"input-enhancement boundary: ok; changed={len(set(paths))}; "
			f"runtime={len(runtime)}; protected={len(protected)}"
		)
		return 0
	except (AssertionError, RuntimeError, ValueError) as error:
		print(f"input-enhancement boundary: error: {error}", file=sys.stderr)
		return 2


if __name__ == "__main__":
	sys.exit(main())
