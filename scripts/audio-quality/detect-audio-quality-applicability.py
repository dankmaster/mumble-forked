#!/usr/bin/env python3
"""Classify whether a change requires the input-enhancement audio quality gate."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path, PurePosixPath
from typing import Iterable, List, Sequence


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]

EXACT_PATHS = {
	".github/actionlint.yaml",
	".gitmodules",
	"CMakeLists.txt",
	"scripts/windows/InputEnhancementReleaseTools.psm1",
	"scripts/windows/invoke-input-enhancement-release-tests.ps1",
	"src/UpdateHealth.cpp",
	"src/UpdateHealth.h",
	"3rdparty/deepfilternet-libdf",
	"3rdparty/deepfilternet-libdf-build/CMakeLists.txt",
	"3rdparty/rnnoise-build",
	"3rdparty/rnnoise-src",
	"src/mumble/AudioInput.cpp",
	"src/mumble/AudioInput.h",
	"src/mumble/Audio.cpp",
	"src/mumble/Audio.h",
	"src/mumble/ALSAAudio.cpp",
	"src/mumble/ALSAAudio.h",
	"src/mumble/CoreAudio.h",
	"src/mumble/JackAudio.cpp",
	"src/mumble/JackAudio.h",
	"src/mumble/OSS.cpp",
	"src/mumble/OSS.h",
	"src/mumble/PAAudio.cpp",
	"src/mumble/PAAudio.h",
	"src/mumble/PipeWire.cpp",
	"src/mumble/PipeWire.h",
	"src/mumble/PulseAudio.cpp",
	"src/mumble/PulseAudio.h",
	"src/mumble/WASAPI.cpp",
	"src/mumble/WASAPI.h",
	"src/mumble/WASAPINotificationClient.cpp",
	"src/mumble/WASAPINotificationClient.h",
	"src/mumble/CMakeLists.txt",
	"src/mumble/EnumStringConversions.cpp",
	"src/mumble/EnumStringConversions.h",
	"src/mumble/JSONSerialization.cpp",
	"src/mumble/ModernSettingsController.cpp",
	"src/mumble/Settings.cpp",
	"src/mumble/Settings.h",
	"src/mumble/UpdateHealthMonitor.cpp",
	"src/mumble/UpdateHealthMonitor.h",
	"src/mumble/VersionCheck.cpp",
	"src/mumble/VersionCheck.h",
	"src/tests/CMakeLists.txt",
}

PREFIXES = (
	".github/workflows/input-enhancement-",
	"3rdparty/deepfilternet-libdf/",
	"3rdparty/rnnoise-build/",
	"3rdparty/rnnoise-src/",
	"scripts/audio-quality/",
	"scripts/windows/assert-input-enhancement-",
	"scripts/windows/new-input-enhancement-",
	"scripts/windows/create-windows-update-package.ps1",
	"scripts/windows/assert-windows-update-package.ps1",
	"scripts/windows/tests/",
	"src/benchmarks/SpeechCleanup/",
	"src/mumble/deepfilternet/",
	"src/mumble/input-enhancement/",
	"src/tests/TestInputEnhancement/",
	"src/tests/TestInputEnhancementAuto/",
	"src/tests/TestInputEnhancementCalibration/",
	"src/tests/TestInputEnhancementCalibrationRuntime/",
	"src/tests/TestInputEnhancementPackageVerifier/",
	"src/tests/TestInputEnhancementPolicy/",
	"src/tests/TestInputEnhancementPolicyController/",
	"src/tests/TestInputEnhancementSettings/",
	"src/tests/TestSpeechCleanup/",
	"src/tests/TestUpdateHealth/",
	"src/updater/",
)

MUMBLE_AUDIO_NAME_MARKERS = (
	"DeepFilterNet",
	"DTLN",
	"InputEnhancement",
	"RNNoise",
	"SpeechCleanup",
)

class ApplicabilityError(RuntimeError):
	"""Raised when a change set cannot be classified safely."""


def normalize_repo_path(value: str) -> str:
	path = value.strip().replace("\\", "/")
	while path.startswith("./"):
		path = path[2:]
	parsed = PurePosixPath(path)
	if not path or parsed.is_absolute() or ".." in parsed.parts:
		raise ApplicabilityError(f"unsafe repository path: {value!r}")
	return parsed.as_posix()


def is_audio_quality_change(path: str) -> bool:
	path = normalize_repo_path(path)
	if path in EXACT_PATHS:
		return True
	if path.startswith(PREFIXES):
		return True
	if path.startswith("src/mumble/"):
		name = PurePosixPath(path).name
		return any(marker in name for marker in MUMBLE_AUDIO_NAME_MARKERS)
	return False


def classify(paths: Iterable[str]) -> List[str]:
	return sorted({ normalize_repo_path(path) for path in paths if is_audio_quality_change(path) })


def changed_files(base: str, head: str) -> List[str]:
	try:
		completed = subprocess.run(
			[ "git", "diff", "--name-only", "--diff-filter=ACDMRT", f"{base}...{head}", "--" ],
			cwd=REPOSITORY_ROOT,
			check=True,
			capture_output=True,
			text=True,
			encoding="utf-8",
		)
	except (OSError, subprocess.CalledProcessError) as error:
		details = error.stderr.strip() if isinstance(error, subprocess.CalledProcessError) and error.stderr else str(error)
		raise ApplicabilityError(f"unable to inspect changed files: {details}") from error
	return [ line for line in completed.stdout.splitlines() if line.strip() ]


def append_github_output(path: Path, applicable: bool, changed_count: int, matched: Sequence[str]) -> None:
	try:
		with path.open("a", encoding="utf-8", newline="\n") as stream:
			stream.write(f"applicable={'true' if applicable else 'false'}\n")
			stream.write(f"changed_count={changed_count}\n")
			stream.write(f"matched_count={len(matched)}\n")
			stream.write(f"matched_files_json={json.dumps(list(matched), separators=(',', ':'))}\n")
	except OSError as error:
		raise ApplicabilityError(f"unable to write GitHub output {path}: {error}") from error


def run_self_test() -> None:
	cases = (
		([ "README.md", "docs/fork-features.md" ], []),
		([ "src/mumble/InputEnhancement.cpp" ], [ "src/mumble/InputEnhancement.cpp" ]),
		([ "src\\mumble\\AudioInput.cpp" ], [ "src/mumble/AudioInput.cpp" ]),
		([ "src/mumble/WASAPI.cpp" ], [ "src/mumble/WASAPI.cpp" ]),
		([ "src/updater/main.cpp" ], [ "src/updater/main.cpp" ]),
		([ "src/UpdateHealth.cpp" ], [ "src/UpdateHealth.cpp" ]),
		([ "src/tests/TestUpdateHealth/TestUpdateHealth.cpp" ],
		 [ "src/tests/TestUpdateHealth/TestUpdateHealth.cpp" ]),
		([ "scripts/windows/invoke-input-enhancement-release-tests.ps1" ],
		 [ "scripts/windows/invoke-input-enhancement-release-tests.ps1" ]),
		([ "src/mumble/input-enhancement/input-recipes.descriptor.json" ],
		 [ "src/mumble/input-enhancement/input-recipes.descriptor.json" ]),
		([ "CMakeLists.txt" ], [ "CMakeLists.txt" ]),
		([ "src/Mumble.proto", "src/murmur/Messages.cpp" ], []),
		([ "src/mumble/AudioOutputSpeech.cpp" ], []),
		(
			[ "src/tests/TestSpeechCleanup/TestSpeechCleanup.cpp", "README.md" ],
			[ "src/tests/TestSpeechCleanup/TestSpeechCleanup.cpp" ],
		),
	)
	for paths, expected in cases:
		actual = classify(paths)
		if actual != expected:
			raise AssertionError(f"applicability mismatch for {paths!r}: expected {expected!r}, got {actual!r}")


def main(argv: Sequence[str] | None = None) -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--base", help="base commit/ref for a three-dot Git diff")
	parser.add_argument("--head", default="HEAD", help="head commit/ref (default: HEAD)")
	parser.add_argument("--changed-file", action="append", default=[], help="classify this path; may be repeated")
	parser.add_argument("--github-output", type=Path, help="append machine-readable outputs to this file")
	parser.add_argument("--self-test", action="store_true", help="run built-in classification regressions")
	args = parser.parse_args(argv)

	try:
		if args.self_test:
			run_self_test()
			print("audio-quality applicability self-test: ok")
		if args.base and args.changed_file:
			raise ApplicabilityError("--base and --changed-file are mutually exclusive")
		if args.base:
			paths = changed_files(args.base, args.head)
		elif args.changed_file:
			paths = args.changed_file
		elif args.self_test:
			return 0
		else:
			raise ApplicabilityError("provide --base, --changed-file, or --self-test")

		matched = classify(paths)
		applicable = bool(matched)
		if args.github_output:
			append_github_output(args.github_output, applicable, len(set(paths)), matched)
		print(
			f"audio-quality applicability: {'applicable' if applicable else 'not-applicable'}; "
			f"changed={len(set(paths))}; matched={len(matched)}"
		)
		for path in matched:
			print(f"  - {path}")
		return 0
	except (ApplicabilityError, AssertionError) as error:
		print(f"audio-quality applicability: error: {error}", file=sys.stderr)
		return 2


if __name__ == "__main__":
	sys.exit(main())
