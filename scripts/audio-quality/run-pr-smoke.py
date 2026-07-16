#!/usr/bin/env python3
"""Run 30 deterministic core-profile smoke cases on an ephemeral CI build.

This is deliberately a correctness/catastrophe smoke, not a replacement for the
licensed human-speech corpus qualification on protected runners. Generated WAV
files remain local to the job; only the audio-free JSON/JUnit attestations are
placed in the upload directory.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import random
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import wave
from contextlib import contextmanager
from pathlib import Path
from typing import Any, Iterator, Mapping, Sequence
from xml.etree import ElementTree


SAMPLE_RATE = 48_000
PROFILES = ("Original", "Light", "Balanced", "Quality", "VoiceFocus")
SCENES = ("clean", "stationary-hvac", "transient-keyboard")
STARTUPS = (("cold", 0), ("warm", 14_400))
EXPECTED = {
	"Original": ("None", "input.original", 0, ""),
	"Light": ("Speex", "input.light.speex", 0, ""),
	"Balanced": ("RNNoise", "input.balanced.rnnoise-embedded", 1_440, "rnnoise:embedded"),
	"Quality": ("DeepFilterNet", "input.quality.deepfilternet-balanced", 2_400, "deepfilternet:balanced"),
	"VoiceFocus": (
		"DeepFilterNet", "input.voice-focus.deepfilternet-balanced", 2_400, "deepfilternet:balanced"
	),
}


class SmokeError(RuntimeError):
	"""Raised for an invalid build, invocation, or benchmark report."""


def load_json(path: Path) -> Mapping[str, Any]:
	try:
		value = json.loads(path.read_text(encoding="utf-8"))
	except (OSError, json.JSONDecodeError) as error:
		raise SmokeError(f"unable to read {path}: {error}") from error
	if not isinstance(value, dict):
		raise SmokeError(f"{path} must contain a JSON object")
	return value


def file_sha256(path: Path) -> str:
	digest = hashlib.sha256()
	with path.open("rb") as stream:
		for block in iter(lambda: stream.read(1024 * 1024), b""):
			digest.update(block)
	return digest.hexdigest()


@contextmanager
def benchmark_in_stage(benchmark: Path, stage_root: Path) -> Iterator[Path]:
	"""Run the benchmark with the staged runtime as its application directory.

	Windows resolves DLLs beside the executable before PATH, and the product
	pipeline deliberately binds embedded-model identity to the loader-resolved
	module path. Executing the build-tree benchmark in place would therefore load
	a build-tree RNNoise DLL while authorizing the staged DLL, even when the bytes
	are identical. A verified temporary copy makes both identities refer to the
	exact staged runtime without relaxing path or hash authentication.
	"""
	file_descriptor, temporary_name = tempfile.mkstemp(
		prefix=".speech_cleanup_benchmark-pr-smoke-", suffix=benchmark.suffix, dir=stage_root
	)
	os.close(file_descriptor)
	staged_benchmark = Path(temporary_name)
	try:
		shutil.copy2(benchmark, staged_benchmark)
		if file_sha256(staged_benchmark) != file_sha256(benchmark):
			raise SmokeError("staged benchmark copy does not match the requested benchmark")
		yield staged_benchmark
	finally:
		staged_benchmark.unlink(missing_ok=True)


def write_wav(path: Path, samples: Sequence[float]) -> None:
	path.parent.mkdir(parents=True, exist_ok=True)
	packed = bytearray()
	for sample in samples:
		value = max(-1.0, min(1.0, sample))
		packed.extend(struct.pack("<h", int(round(value * 32767.0))))
	with wave.open(str(path), "wb") as output:
		output.setnchannels(1)
		output.setsampwidth(2)
		output.setframerate(SAMPLE_RATE)
		output.writeframes(bytes(packed))


def wav_samples(path: Path) -> list[float]:
	payload = path.read_bytes()
	if len(payload) < 12 or payload[:4] != b"RIFF" or payload[8:12] != b"WAVE":
		raise SmokeError(f"invalid RIFF/WAVE file {path}")
	format_tag = channels = sample_rate = bits = None
	data = None
	offset = 12
	while offset + 8 <= len(payload):
		chunk_id, chunk_size = payload[offset : offset + 4], struct.unpack_from("<I", payload, offset + 4)[0]
		chunk = payload[offset + 8 : offset + 8 + chunk_size]
		if len(chunk) != chunk_size:
			raise SmokeError(f"truncated WAV chunk in {path}")
		if chunk_id == b"fmt " and chunk_size >= 16:
			format_tag, channels, sample_rate, _, _, bits = struct.unpack_from("<HHIIHH", chunk)
		elif chunk_id == b"data":
			data = chunk
		offset += 8 + chunk_size + (chunk_size & 1)
	if channels != 1 or sample_rate != SAMPLE_RATE or data is None:
		raise SmokeError(f"unexpected WAV layout in {path}")
	if format_tag == 1 and bits == 16 and len(data) % 2 == 0:
		return [value / 32768.0 for (value,) in struct.iter_unpack("<h", data)]
	if format_tag == 3 and bits == 32 and len(data) % 4 == 0:
		return [value for (value,) in struct.iter_unpack("<f", data)]
	raise SmokeError(f"unsupported WAV format tag={format_tag}, bits={bits} in {path}")


def speech_like(index: int) -> float:
	t = index / SAMPLE_RATE
	# Voiced harmonics with slow formant movement, plus deterministic plosive and
	# sibilant regions. This catches framing/tail/non-finite failures without
	# redistributing or uploading human speech.
	envelope = min(1.0, index / 240.0)
	if t > 1.7:
		envelope *= max(0.0, (2.0 - t) / 0.3)
	f0 = 145.0 + 18.0 * math.sin(2.0 * math.pi * 1.3 * t)
	voiced = sum(math.sin(2.0 * math.pi * f0 * harmonic * t) / harmonic for harmonic in range(1, 7))
	voiced *= 0.16 * envelope
	plosive = 0.30 * math.exp(-index / 180.0) * math.sin(2.0 * math.pi * 900.0 * t)
	sibilant = 0.0
	if 0.72 <= t < 0.96:
		sibilant = 0.07 * (math.sin(2.0 * math.pi * 5_900.0 * t) + math.sin(2.0 * math.pi * 7_300.0 * t))
	return voiced + plosive + sibilant


def generate_case(scene: str, preroll: int, seed: int) -> tuple[list[float], list[float]]:
	rng = random.Random(seed)
	speech_samples = 2 * SAMPLE_RATE
	clean = [0.0] * preroll + [speech_like(index) for index in range(speech_samples)]
	noisy: list[float] = []
	hum_phase = 0.0
	for index, clean_sample in enumerate(clean):
		t = index / SAMPLE_RATE
		noise = 0.0
		if scene == "stationary-hvac":
			hum_phase += 2.0 * math.pi * 60.0 / SAMPLE_RATE
			noise = 0.055 * math.sin(hum_phase) + 0.018 * (rng.random() * 2.0 - 1.0)
		elif scene == "transient-keyboard":
			noise = 0.012 * (rng.random() * 2.0 - 1.0)
			for click_time in (0.18, 0.61, 1.08, 1.54):
				delta = abs(t - click_time)
				if delta < 0.008:
					noise += 0.36 * math.exp(-delta * 700.0) * math.sin(2.0 * math.pi * 2_400.0 * delta)
		noisy.append(max(-0.92, min(0.92, clean_sample + noise)))
	return noisy, clean


def model_records(manifest: Mapping[str, Any], stage_root: Path) -> dict[str, tuple[str, Path]]:
	models = manifest.get("models")
	if not isinstance(models, list):
		raise SmokeError("model manifest has no models array")
	records: dict[str, tuple[str, Path]] = {}
	for value in models:
		if not isinstance(value, dict):
			raise SmokeError("model manifest contains a non-object record")
		model_id, sha256, relative = value.get("id"), value.get("sha256"), value.get("path")
		if not isinstance(model_id, str) or not isinstance(sha256, str) or not isinstance(relative, str):
			raise SmokeError("model manifest record is incomplete")
		path = (stage_root / relative).resolve()
		if not path.is_file() or file_sha256(path) != sha256:
			raise SmokeError(f"staged model {model_id!r} does not match its manifest")
		records[model_id] = (sha256, path)
	return records


def run_case(
	benchmark: Path,
	stage_root: Path,
	work_root: Path,
	profile: str,
	scene: str,
	startup: str,
	preroll: int,
	models: Mapping[str, tuple[str, Path]],
	environment: Mapping[str, str],
) -> Mapping[str, Any]:
	case_id = f"{scene}-{profile.lower()}-{startup}"
	case_root = work_root / case_id
	case_root.mkdir(parents=True, exist_ok=True)
	noisy, clean = generate_case(scene, preroll, seed=int(hashlib.sha256(case_id.encode()).hexdigest()[:8], 16))
	input_path, clean_path = case_root / "input.wav", case_root / "clean.wav"
	output_path, report_path = case_root / "output.wav", case_root / "report.json"
	write_wav(input_path, noisy)
	write_wav(clean_path, clean)
	noise_reduction = {"clean": 35, "stationary-hvac": 60, "transient-keyboard": 75}[scene]
	command = [
		str(benchmark), "--profile", profile,
		"--noise-reduction", str(noise_reduction), "--natural-clear", "55",
		"--cpu-class", "High", "--input", str(input_path), "--clean-reference", str(clean_path),
		"--output", str(output_path), "--report", str(report_path),
	]
	expected_engine, expected_recipe, expected_latency, expected_model = EXPECTED[profile]
	if expected_model:
		try:
			model_hash, model_path = models[expected_model]
		except KeyError as error:
			raise SmokeError(f"missing required staged model {expected_model}") from error
		command += ["--authorized-model-sha256", model_hash, "--authorized-model-path", str(model_path)]
	started = time.monotonic()
	completed = subprocess.run(
		command, cwd=stage_root, env=dict(environment), capture_output=True, text=True,
		encoding="utf-8", errors="replace", timeout=120, check=False,
	)
	duration = time.monotonic() - started
	if completed.returncode != 0:
		details = "\n".join(
			f"{name}: {value.strip()}"
			for name, value in (("stdout", completed.stdout), ("stderr", completed.stderr))
			if value.strip()
		)
		if not details:
			details = "benchmark produced no stdout/stderr; inspect the command and case artifacts below"
		raise SmokeError(
			f"{case_id}: benchmark exited {completed.returncode}: {details}; "
			f"command={subprocess.list2cmdline(command)}; case_root={case_root}"
		)
	report = load_json(report_path)
	checks = {
		"processing_mode": report.get("processing_mode") == "product-profile",
		"requested_profile": report.get("requested_profile") == profile,
		"active_profile": report.get("active_profile") == profile,
		"active_engine": report.get("active_engine") == expected_engine,
		"recipe": report.get("requested_recipe_id") == expected_recipe and report.get("recipe_revision") == 1,
		"latency": report.get("reported_latency_samples") == expected_latency
			and report.get("drain_sample_count") == expected_latency,
		"timeline": report.get("output_sample_count") == report.get("input_sample_count", -1) + expected_latency,
		"finite": report.get("non_finite_sample_count") == 0 and report.get("out_of_range_sample_count") == 0,
		"catastrophe": report.get("used_fallback") is False and report.get("fallback_count") == 0
			and report.get("deadline_misses") == 0,
		"model": report.get("active_model_id", "") == expected_model,
	}
	if expected_model:
		checks["model_hash"] = report.get("active_model_sha256") == models[expected_model][0]
	if profile == "Original":
		input_samples, output_samples = wav_samples(input_path), wav_samples(output_path)
		checks["original_pcm"] = len(input_samples) == len(output_samples) and all(
			abs(left - right) <= 1.0e-7 for left, right in zip(input_samples, output_samples)
		)
	failed = sorted(name for name, passed in checks.items() if not passed)
	if failed:
		raise SmokeError(f"{case_id}: report failed checks: {', '.join(failed)}")
	return {
		"id": case_id,
		"profile": profile,
		"scene": scene,
		"startup": startup,
		"prerollSamples": preroll,
		"activeEngine": report.get("active_engine"),
		"activeModelId": report.get("active_model_id", ""),
		"activeModelSha256": report.get("active_model_sha256", ""),
		"recipeId": report.get("requested_recipe_id"),
		"recipeRevision": report.get("recipe_revision"),
		"latencySamples": report.get("reported_latency_samples"),
		"callbackP99Ms": report.get("callback_p99_ms"),
		"rtf": report.get("rtf"),
		"durationSeconds": duration,
		"passed": True,
	}


def write_junit(path: Path, cases: Sequence[Mapping[str, Any]], elapsed: float) -> None:
	suite = ElementTree.Element(
		"testsuite", name="input-enhancement-pr-smoke", tests=str(len(cases)), failures="0", time=f"{elapsed:.3f}"
	)
	for case in cases:
		ElementTree.SubElement(
			suite, "testcase", classname=f"{case['scene']}.{case['startup']}",
			name=str(case["profile"]), time=f"{float(case['durationSeconds']):.3f}"
		)
	ElementTree.ElementTree(suite).write(path, encoding="utf-8", xml_declaration=True)


def main(argv: Sequence[str] | None = None) -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--benchmark", required=True, type=Path)
	parser.add_argument("--stage-root", required=True, type=Path)
	parser.add_argument("--model-manifest", required=True, type=Path)
	parser.add_argument("--recipe-manifest", required=True, type=Path)
	parser.add_argument("--source-sha", required=True)
	parser.add_argument("--output-root", required=True, type=Path)
	args = parser.parse_args(argv)
	try:
		benchmark, stage_root = args.benchmark.resolve(), args.stage_root.resolve()
		if not benchmark.is_file() or not stage_root.is_dir():
			raise SmokeError("benchmark or staged client root does not exist")
		if args.source_sha.lower() != args.source_sha or len(args.source_sha) != 40 or any(
			character not in "0123456789abcdef" for character in args.source_sha
		):
			raise SmokeError("source SHA must be a lowercase full Git SHA")
		model_manifest, recipe_manifest = load_json(args.model_manifest), load_json(args.recipe_manifest)
		if model_manifest.get("catalogRevision") != recipe_manifest.get("catalogRevision"):
			raise SmokeError("model and recipe manifests do not share a catalog revision")
		models = model_records(model_manifest, stage_root)
		output_root = args.output_root.resolve()
		if output_root.exists():
			shutil.rmtree(output_root)
		work_root, upload_root = output_root / "work", output_root / "upload"
		work_root.mkdir(parents=True)
		upload_root.mkdir(parents=True)
		environment = os.environ.copy()
		environment["PATH"] = str(stage_root) + os.pathsep + environment.get("PATH", "")
		started = time.monotonic()
		with benchmark_in_stage(benchmark, stage_root) as staged_benchmark:
			cases = [
				run_case(staged_benchmark, stage_root, work_root, profile, scene, startup, preroll, models, environment)
				for scene in SCENES
				for startup, preroll in STARTUPS
				for profile in PROFILES
			]
		if len(cases) != 30:
			raise SmokeError(f"internal suite error: expected 30 cases, got {len(cases)}")
		elapsed = time.monotonic() - started
		summary = {
			"schemaVersion": 1,
			"suite": "input-enhancement-pr-smoke-v1",
			"status": "passed",
			"audioFree": True,
			"sourceSha": args.source_sha,
			"testedBinarySha256": file_sha256(stage_root / "mumble.exe"),
			"benchmarkSha256": file_sha256(benchmark),
			"recipeSetVersion": recipe_manifest["catalogRevision"],
			"caseCount": len(cases),
			"receiverCleanupEnabled": False,
			"generatedFixture": True,
			"elapsedSeconds": elapsed,
			"cases": cases,
		}
		(upload_root / "pr-smoke.json").write_text(
			json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8", newline="\n"
		)
		write_junit(upload_root / "pr-smoke.junit.xml", cases, elapsed)
		print(f"input-enhancement PR smoke: passed {len(cases)}/30 cases in {elapsed:.1f}s")
		return 0
	except (OSError, SmokeError, subprocess.SubprocessError, KeyError, ValueError) as error:
		print(f"input-enhancement PR smoke: failed: {error}", file=sys.stderr)
		return 1


if __name__ == "__main__":
	sys.exit(main())
