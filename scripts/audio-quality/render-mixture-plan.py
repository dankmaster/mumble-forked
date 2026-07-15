#!/usr/bin/env python3
"""Render a locked mixture plan to deterministic mono 48 kHz PCM WAV files.

The renderer is intentionally dependency-free.  It never downloads corpus
material and it accepts only WAV inventory entries below the caller supplied
corpus root.  Generated audio is private local evidence and must not be
uploaded by CI; the emitted manifest contains hashes and transformation
parameters so a trusted harness can attest the exact inputs without exposing
samples.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import math
import os
import struct
import sys
import tempfile
import wave
from pathlib import Path, PurePosixPath
from typing import Any, Iterable, Mapping, Sequence


class RenderError(ValueError):
	"""Raised when a plan or local source is unsafe or cannot be rendered."""


TARGET_RATE = 48_000
TARGET_CHANNELS = 1


def _load_plan_module() -> Any:
	path = Path(__file__).with_name("generate-mixture-plan.py")
	spec = importlib.util.spec_from_file_location("mumble_audio_mixture_plan", path)
	if spec is None or spec.loader is None:
		raise RenderError(f"unable to load plan validator: {path}")
	module = importlib.util.module_from_spec(spec)
	spec.loader.exec_module(module)
	return module


PLAN = _load_plan_module()


def _load_json(path: Path) -> Any:
	try:
		return json.loads(path.read_text(encoding="utf-8"))
	except (OSError, json.JSONDecodeError) as error:
		raise RenderError(f"unable to read JSON {path}: {error}") from error


def _sha256(path: Path) -> str:
	digest = hashlib.sha256()
	with path.open("rb") as stream:
		for chunk in iter(lambda: stream.read(1024 * 1024), b""):
			digest.update(chunk)
	return digest.hexdigest()


def _safe_source(root: Path, relative: str) -> Path:
	parsed = PurePosixPath(relative)
	if parsed.is_absolute() or relative != parsed.as_posix() or "." in parsed.parts or ".." in parsed.parts:
		raise RenderError(f"unsafe corpus path: {relative!r}")
	root = root.resolve()
	path = root.joinpath(*parsed.parts).resolve()
	try:
		path.relative_to(root)
	except ValueError as error:
		raise RenderError(f"corpus path escapes root: {relative!r}") from error
	if not path.is_file():
		raise RenderError(f"corpus source does not exist: {path}")
	if path.suffix.lower() != ".wav":
		raise RenderError(
			f"source must be converted to locked PCM WAV before rendering: {relative!r}"
		)
	return path


def _verify_locked_source(path: Path, source: Mapping[str, Any]) -> None:
	expected_size = source.get("size_bytes")
	expected_sha256 = source.get("sha256")
	if not isinstance(expected_size, int) or isinstance(expected_size, bool) or expected_size <= 0:
		raise RenderError(f"source has no valid locked size: {path}")
	if not isinstance(expected_sha256, str) or len(expected_sha256) != 64:
		raise RenderError(f"source has no valid locked SHA-256: {path}")
	actual_size = path.stat().st_size
	if actual_size != expected_size:
		raise RenderError(f"source size mismatch for {path}: expected {expected_size}, got {actual_size}")
	actual_sha256 = _sha256(path)
	if actual_sha256 != expected_sha256:
		raise RenderError(f"source SHA-256 mismatch for {path}: expected {expected_sha256}, got {actual_sha256}")


def _decode_pcm(raw: bytes, sample_width: int) -> list[float]:
	if sample_width == 1:
		return [(value - 128) / 128.0 for value in raw]
	if sample_width == 2:
		count = len(raw) // 2
		return [value / 32768.0 for value in struct.unpack(f"<{count}h", raw)]
	if sample_width == 3:
		result = []
		for offset in range(0, len(raw), 3):
			value = int.from_bytes(raw[offset : offset + 3], "little", signed=False)
			if value & 0x800000:
				value -= 1 << 24
			result.append(value / 8_388_608.0)
		return result
	if sample_width == 4:
		count = len(raw) // 4
		return [value / 2_147_483_648.0 for value in struct.unpack(f"<{count}i", raw)]
	raise RenderError(f"unsupported PCM sample width: {sample_width} bytes")


def _read_window(path: Path, window: Mapping[str, int], expected_rate: int, expected_channels: int) -> list[float]:
	try:
		with wave.open(str(path), "rb") as stream:
			if stream.getcomptype() != "NONE":
				raise RenderError(f"compressed WAV is unsupported: {path}")
			if stream.getframerate() != expected_rate or stream.getnchannels() != expected_channels:
				raise RenderError(
					f"inventory metadata mismatch for {path}: actual "
					f"{stream.getframerate()} Hz/{stream.getnchannels()} ch, expected "
					f"{expected_rate} Hz/{expected_channels} ch"
				)
			start = int(window["start_sample"])
			length = int(window["length_samples"])
			if start < 0 or length <= 0 or start + length > stream.getnframes():
				raise RenderError(f"source window exceeds WAV frames: {path}")
			stream.setpos(start)
			raw = stream.readframes(length)
			samples = _decode_pcm(raw, stream.getsampwidth())
	except (wave.Error, OSError) as error:
		raise RenderError(f"unable to decode {path}: {error}") from error

	channels = expected_channels
	if channels == 1:
		return samples
	return [sum(samples[offset : offset + channels]) / channels for offset in range(0, len(samples), channels)]


def _resample(samples: Sequence[float], source_rate: int, target_rate: int = TARGET_RATE) -> list[float]:
	if source_rate == target_rate:
		return list(samples)
	if not samples or source_rate <= 0:
		raise RenderError("cannot resample empty/invalid audio")
	target_length = round(len(samples) * target_rate / source_rate)
	if target_length <= 0:
		raise RenderError("resampled signal would be empty")
	step = source_rate / target_rate
	result = [0.0] * target_length
	for index in range(target_length):
		position = index * step
		left = min(int(position), len(samples) - 1)
		right = min(left + 1, len(samples) - 1)
		fraction = position - left
		result[index] = samples[left] + (samples[right] - samples[left]) * fraction
	return result


def _rms(samples: Iterable[float]) -> float:
	values = list(samples)
	if not values:
		return 0.0
	return math.sqrt(sum(value * value for value in values) / len(values))


def _one_pole_lowpass(samples: Sequence[float], cutoff_hz: float) -> list[float]:
	alpha = 1.0 - math.exp(-2.0 * math.pi * cutoff_hz / TARGET_RATE)
	state = 0.0
	output = []
	for sample in samples:
		state += alpha * (sample - state)
		output.append(state)
	return output


def _one_pole_highpass(samples: Sequence[float], cutoff_hz: float) -> list[float]:
	low = _one_pole_lowpass(samples, cutoff_hz)
	return [sample - low_sample for sample, low_sample in zip(samples, low)]


def _microphone_response(samples: Sequence[float], family: str) -> list[float]:
	parameters = {
		"headset": (80.0, 14_000.0, 1.02),
		"laptop": (150.0, 9_000.0, 1.08),
		"usb": (60.0, 16_000.0, 1.00),
		"phone": (220.0, 7_000.0, 1.12),
	}
	if family not in parameters:
		raise RenderError(f"unsupported microphone family: {family!r}")
	highpass, lowpass, gain = parameters[family]
	band_limited = _one_pole_lowpass(_one_pole_highpass(samples, highpass), lowpass)
	return [value * gain for value in band_limited]


def _synthetic_room(samples: Sequence[float], rt60_ms: int, room_id: str) -> list[float]:
	if rt60_ms <= 0:
		return list(samples)
	# Three deterministic, low-level early reflections.  This is intentionally a
	# stable test transform, not an attempt to synthesize a perceptual room model.
	seed = hashlib.sha256(room_id.encode("utf-8")).digest()
	delays_ms = (7 + seed[0] % 9, 19 + seed[1] % 17, 37 + seed[2] % 29)
	gains = (0.18, -0.11, 0.07)
	output = list(samples)
	decay = math.exp(-6.90775527898 * max(delays_ms) / rt60_ms)
	for delay_ms, gain in zip(delays_ms, gains):
		delay = round(delay_ms * TARGET_RATE / 1000)
		reflection_gain = gain * decay
		for index in range(delay, len(output)):
			output[index] += samples[index - delay] * reflection_gain
	return output


def _fit_length(samples: Sequence[float], length: int) -> list[float]:
	if len(samples) >= length:
		return list(samples[:length])
	return [*samples, *([0.0] * (length - len(samples)))]


def _write_pcm16(path: Path, samples: Sequence[float]) -> None:
	path.parent.mkdir(parents=True, exist_ok=True)
	pcm = bytearray()
	for sample in samples:
		if not math.isfinite(sample):
			raise RenderError(f"non-finite sample before writing {path}")
		value = max(-1.0, min(1.0, sample))
		integer = -32768 if value <= -1.0 else round(value * 32767.0)
		pcm.extend(struct.pack("<h", integer))
	with wave.open(str(path), "wb") as stream:
		stream.setnchannels(TARGET_CHANNELS)
		stream.setsampwidth(2)
		stream.setframerate(TARGET_RATE)
		stream.writeframes(pcm)


def _render_case(case: Mapping[str, Any], corpus_root: Path, output_root: Path, target_samples: int) -> Mapping[str, Any]:
	speech_info = case["speech"]
	speech_path = _safe_source(corpus_root, speech_info["relative_path"])
	_verify_locked_source(speech_path, speech_info)
	speech = _read_window(
		speech_path,
		speech_info["window"],
		int(speech_info["input_sample_rate_hz"]),
		int(speech_info["input_channels"]),
	)
	speech = _fit_length(_resample(speech, int(speech_info["input_sample_rate_hz"])), target_samples)
	mix = case["mix"]
	speech = _synthetic_room(speech, int(mix["rir"]["rt60_ms"]), str(mix["rir"]["id"]))
	distance_scale = min(2.0, max(0.2, 30.0 / float(mix["distance_cm"])))
	speech = [value * distance_scale for value in speech]

	noise = [0.0] * target_samples
	noise_info = case["noise"]
	if noise_info is not None:
		noise_path = _safe_source(corpus_root, noise_info["relative_path"])
		_verify_locked_source(noise_path, noise_info)
		noise = _read_window(
			noise_path,
			noise_info["window"],
			int(noise_info["input_sample_rate_hz"]),
			int(noise_info["input_channels"]),
		)
		noise = _fit_length(_resample(noise, int(noise_info["input_sample_rate_hz"])), target_samples)
		speech_rms = _rms(speech)
		noise_rms = _rms(noise)
		if speech_rms <= 1e-12 or noise_rms <= 1e-12:
			raise RenderError(f"{case['case_id']}: speech/noise window has no usable energy")
		target_noise_rms = speech_rms / (10.0 ** (float(mix["snr_db"]) / 20.0))
		noise_scale = target_noise_rms / noise_rms
		noise = [value * noise_scale for value in noise]

	family = str(mix["microphone_response"]["family"])
	clean = _microphone_response(speech, family)
	noisy = _microphone_response([left + right for left, right in zip(speech, noise)], family)
	gain = 10.0 ** (float(mix["speech_gain_db"]) / 20.0)
	clean = [value * gain for value in clean]
	noisy = [value * gain for value in noisy]
	if bool(mix["mild_clipping"]):
		clean = [max(-1.0, min(1.0, value * 1.08)) for value in clean]
		noisy = [max(-1.0, min(1.0, value * 1.08)) for value in noisy]

	case_root = output_root / str(case["case_id"])
	input_path = case_root / "client1-input.wav"
	reference_path = case_root / "clean-reference.wav"
	_write_pcm16(input_path, noisy)
	_write_pcm16(reference_path, clean)
	return {
		"case_id": case["case_id"],
		"profile": case["profile"],
		"startup_preroll_ms": case["startup"]["preroll_ms"],
		"input": { "path": input_path.relative_to(output_root).as_posix(), "sha256": _sha256(input_path) },
		"clean_reference": {
			"path": reference_path.relative_to(output_root).as_posix(),
			"sha256": _sha256(reference_path),
		},
		"speech_source_sha256": _sha256(speech_path),
		"noise_source_sha256": _sha256(noise_path) if noise_info is not None else None,
		"rendered_samples": target_samples,
	}


def render(plan: Mapping[str, Any], corpus_root: Path, output_root: Path) -> Mapping[str, Any]:
	PLAN.validate_plan(plan)
	if output_root.exists() and any(output_root.iterdir()):
		raise RenderError(f"output root must be empty: {output_root}")
	output_root.mkdir(parents=True, exist_ok=True)
	target_samples = round(int(plan["format"]["duration_ms"]) * TARGET_RATE / 1000)
	entries = [_render_case(case, corpus_root, output_root, target_samples) for case in plan["cases"]]
	manifest = {
		"schema_version": 1,
		"renderer": "mumble-audio-mixture-renderer-v1",
		"plan_sha256": PLAN.canonical_sha256(plan),
		"corpus_lock_sha256": plan["corpus_lock_sha256"],
		"sample_rate_hz": TARGET_RATE,
		"channels": TARGET_CHANNELS,
		"private_audio_do_not_upload": True,
		"cases": entries,
	}
	manifest_path = output_root / "render-manifest.json"
	temporary = manifest_path.with_suffix(".tmp")
	temporary.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
	os.replace(temporary, manifest_path)
	return manifest


def _write_fixture(path: Path, rate: int, frequency: float, seconds: int) -> None:
	samples = [0.2 * math.sin(2.0 * math.pi * frequency * index / rate) for index in range(rate * seconds)]
	_write_pcm16(path, samples if rate == TARGET_RATE else _resample(samples, rate, rate))


def self_test() -> None:
	# Exercise the byte-stable DSP using a minimal hand-authored valid plan.  The
	# plan validator owns broader schema/split coverage in its own self-test.
	with tempfile.TemporaryDirectory(prefix="mumble-mixture-render-") as directory:
		root = Path(directory)
		corpus = root / "corpus"
		corpus.mkdir()
		_write_fixture(corpus / "speech.wav", TARGET_RATE, 220.0, 1)
		_write_fixture(corpus / "noise.wav", TARGET_RATE, 997.0, 1)
		speech_path = corpus / "speech.wav"
		noise_path = corpus / "noise.wav"
		artifact_sha256 = "a" * 64
		case = {
			"case_id": "self-test-validation-00001",
			"profile": "Balanced",
			"startup": { "preroll_ms": 0 },
			"speech": {
				"relative_path": "speech.wav", "input_sample_rate_hz": TARGET_RATE,
				"input_channels": 1, "window": { "start_sample": 0, "length_samples": TARGET_RATE },
				"sha256": _sha256(speech_path), "size_bytes": speech_path.stat().st_size,
				"source_artifact_sha256": artifact_sha256,
			},
			"noise": {
				"relative_path": "noise.wav", "input_sample_rate_hz": TARGET_RATE,
				"input_channels": 1, "window": { "start_sample": 0, "length_samples": TARGET_RATE },
				"sha256": _sha256(noise_path), "size_bytes": noise_path.stat().st_size,
				"source_artifact_sha256": artifact_sha256,
			},
			"mix": {
				"snr_db": 0, "speech_gain_db": 0, "mild_clipping": False, "distance_cm": 30,
				"rir": { "id": "validation-office-v1", "rt60_ms": 150 },
				"microphone_response": { "family": "usb", "id": "validation-usb-v1" },
			},
		}
		output_a = root / "a"
		output_b = root / "b"
		a = _render_case(case, corpus, output_a, TARGET_RATE)
		b = _render_case(case, corpus, output_b, TARGET_RATE)
		if a["input"]["sha256"] != b["input"]["sha256"]:
			raise AssertionError("renderer is not byte deterministic")
		if a["input"]["sha256"] == a["clean_reference"]["sha256"]:
			raise AssertionError("0 dB noise did not alter the rendered input")
		corrupt = bytearray(noise_path.read_bytes())
		corrupt[-1] ^= 0x01
		noise_path.write_bytes(corrupt)
		try:
			_render_case(case, corpus, root / "corrupt", TARGET_RATE)
		except RenderError as error:
			if "SHA-256 mismatch" not in str(error):
				raise
		else:
			raise AssertionError("renderer accepted a corpus file whose per-file hash changed")


def main(argv: Sequence[str] | None = None) -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--plan", type=Path)
	parser.add_argument("--corpus-root", type=Path)
	parser.add_argument("--output-root", type=Path)
	parser.add_argument("--self-test", action="store_true")
	args = parser.parse_args(argv)
	try:
		if args.self_test:
			self_test()
			print("Mixture renderer self-test: ok")
			if args.plan is None:
				return 0
		if args.plan is None or args.corpus_root is None or args.output_root is None:
			raise RenderError("--plan, --corpus-root, and --output-root are required")
		manifest = render(_load_json(args.plan), args.corpus_root, args.output_root)
		print(f"Rendered {len(manifest['cases'])} private case(s) into {args.output_root}")
		return 0
	except (AssertionError, KeyError, RenderError, TypeError, ValueError) as error:
		print(f"Mixture renderer error: {error}", file=sys.stderr)
		return 1


if __name__ == "__main__":
	sys.exit(main())
