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
import concurrent.futures
import hashlib
import importlib.util
import json
import math
import os
import shutil
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
MAX_RENDER_JOBS = 32
WINDOWS_RESERVED_DEVICE_NAMES = {
	"CON", "PRN", "AUX", "NUL",
	*(f"COM{index}" for index in range(1, 10)),
	*(f"LPT{index}" for index in range(1, 10)),
}


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


def _read_entire(path: Path, expected_rate: int, expected_channels: int, expected_frames: int) -> list[float]:
	return _read_window(
		path,
		{"start_sample": 0, "length_samples": expected_frames},
		expected_rate,
		expected_channels,
	)


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


def _sparse_impulse_response(
	samples: Sequence[float], impulse: Sequence[float], *, max_span_samples: int = 4096, max_taps: int = 48
) -> list[float]:
	"""Apply a bounded deterministic approximation of a locked response WAV.

	The largest-energy taps from the first 85 ms are retained.  This keeps the
	dependency-free renderer tractable while ensuring the real, hash-attested
	response bytes materially determine every output sample.
	"""
	window = list(impulse[:max_span_samples])
	if not window or max(abs(value) for value in window) <= 1e-12:
		raise RenderError("impulse response has no usable energy")
	indices = sorted(range(len(window)), key=lambda index: (-abs(window[index]), index))[:max_taps]
	indices.sort()
	normalizer = sum(abs(window[index]) for index in indices)
	if normalizer <= 1e-12:
		raise RenderError("impulse response selected no usable taps")
	taps = [(index, window[index] / normalizer) for index in indices]
	output = [0.0] * len(samples)
	for delay, gain in taps:
		for index in range(delay, len(samples)):
			output[index] += samples[index - delay] * gain
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
	rir_info = mix["rir"]
	rir_path = _safe_source(corpus_root, rir_info["relative_path"])
	_verify_locked_source(rir_path, rir_info)
	rir_samples = _read_entire(
		rir_path,
		int(rir_info["input_sample_rate_hz"]),
		int(rir_info["input_channels"]),
		int(rir_info["duration_samples"]),
	)
	rir_samples = _resample(rir_samples, int(rir_info["input_sample_rate_hz"]))
	speech = _sparse_impulse_response(speech, rir_samples)
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

	microphone_info = mix["microphone_response"]
	microphone_path = _safe_source(corpus_root, microphone_info["relative_path"])
	_verify_locked_source(microphone_path, microphone_info)
	microphone_samples = _read_entire(
		microphone_path,
		int(microphone_info["input_sample_rate_hz"]),
		int(microphone_info["input_channels"]),
		int(microphone_info["duration_samples"]),
	)
	microphone_samples = _resample(microphone_samples, int(microphone_info["input_sample_rate_hz"]))
	clean = _sparse_impulse_response(speech, microphone_samples)
	noisy = _sparse_impulse_response([left + right for left, right in zip(speech, noise)], microphone_samples)
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
		"rir_source_sha256": _sha256(rir_path),
		"microphone_response_source_sha256": _sha256(microphone_path),
		"rendered_samples": target_samples,
	}


def _validated_jobs(value: int) -> int:
	if not isinstance(value, int) or isinstance(value, bool) or not 1 <= value <= MAX_RENDER_JOBS:
		raise RenderError(f"jobs must be an integer from 1 through {MAX_RENDER_JOBS}")
	return value


def _case_id(value: Any) -> str:
	if not isinstance(value, str) or not value or value in (".", ".."):
		raise RenderError("case id must be a non-empty safe path component")
	if any(character in value for character in '<>:"/\\|?*') or any(ord(character) < 32 for character in value):
		raise RenderError(f"unsafe case id: {value!r}")
	if value.endswith((".", " ")):
		raise RenderError(f"case id has a Windows-unsafe trailing dot or space: {value!r}")
	device_basename = value.split(".", 1)[0].rstrip(" ").upper()
	if device_basename in WINDOWS_RESERVED_DEVICE_NAMES:
		raise RenderError(f"case id uses a reserved Windows device name: {value!r}")
	return value


def _windows_case_id_key(value: str) -> str:
	return value.rstrip(" .").casefold()


def _reject_windows_path_collisions(case_ids: Sequence[str], label: str) -> None:
	seen: dict[str, str] = {}
	for case_id in case_ids:
		key = _windows_case_id_key(case_id)
		previous = seen.get(key)
		if previous is not None:
			raise RenderError(
				f"{label} contains a duplicate or Windows-normalized path collision: "
				f"{previous!r} and {case_id!r}"
			)
		seen[key] = case_id


def _select_cases(plan: Mapping[str, Any], requested_case_ids: Sequence[str] | None) -> list[Mapping[str, Any]]:
	all_cases = list(plan["cases"])
	plan_ids = [_case_id(case["case_id"]) for case in all_cases]
	_reject_windows_path_collisions(plan_ids, "plan")
	if requested_case_ids is None:
		return all_cases
	requested = [_case_id(case_id) for case_id in requested_case_ids]
	if not requested:
		raise RenderError("case-id filter must not be empty")
	_reject_windows_path_collisions(requested, "case-id filter")
	unknown = sorted(set(requested) - set(plan_ids))
	if unknown:
		raise RenderError(f"case-id filter contains unknown case id(s): {', '.join(unknown)}")
	selected = set(requested)
	# The canonical plan, rather than command-line ordering, owns manifest order.
	return [case for case, case_id in zip(all_cases, plan_ids) if case_id in selected]


def _render_entries(
	cases: Sequence[Mapping[str, Any]], corpus_root: Path, staging_root: Path, target_samples: int, jobs: int
) -> list[Mapping[str, Any]]:
	if jobs == 1:
		return [_render_case(case, corpus_root, staging_root, target_samples) for case in cases]
	entries: list[Mapping[str, Any] | None] = [None] * len(cases)
	futures: list[tuple[int, str, concurrent.futures.Future[Mapping[str, Any]]]] = []
	try:
		with concurrent.futures.ProcessPoolExecutor(max_workers=min(jobs, len(cases))) as executor:
			for index, case in enumerate(cases):
				future = executor.submit(_render_case, case, corpus_root, staging_root, target_samples)
				futures.append((index, str(case["case_id"]), future))
			for index, case_id, future in futures:
				try:
					entries[index] = future.result()
				except Exception as error:
					for _, _, pending in futures:
						pending.cancel()
					raise RenderError(f"worker failed while rendering {case_id}: {error}") from error
	except RenderError:
		raise
	except Exception as error:
		raise RenderError(f"unable to run render workers: {error}") from error
	if any(entry is None for entry in entries):
		raise RenderError("render worker completed without returning every case")
	return [entry for entry in entries if entry is not None]


def _render_validated(
	plan: Mapping[str, Any], corpus_root: Path, output_root: Path, *, jobs: int = 1,
	case_ids: Sequence[str] | None = None,
) -> Mapping[str, Any]:
	jobs = _validated_jobs(jobs)
	cases = _select_cases(plan, case_ids)
	if output_root.exists():
		if not output_root.is_dir() or output_root.is_symlink() or any(output_root.iterdir()):
			raise RenderError(f"output root must be an empty real directory: {output_root}")
	output_root.parent.mkdir(parents=True, exist_ok=True)
	staging_root = Path(tempfile.mkdtemp(prefix=f".{output_root.name}.render-", dir=output_root.parent)).resolve()
	target_samples = round(int(plan["format"]["duration_ms"]) * TARGET_RATE / 1000)
	try:
		entries = _render_entries(cases, corpus_root.resolve(), staging_root, target_samples, jobs)
		manifest = {
			"schema_version": 2,
			"renderer": "mumble-audio-mixture-renderer-v2",
			"plan_sha256": PLAN.canonical_sha256(plan),
			"corpus_lock_sha256": plan["corpus_lock_sha256"],
			"corpus_inventory_sha256": plan["corpus_inventory_sha256"],
			"sample_rate_hz": TARGET_RATE,
			"channels": TARGET_CHANNELS,
			"private_audio_do_not_upload": True,
			"cases": entries,
		}
		manifest_path = staging_root / "render-manifest.json"
		temporary = manifest_path.with_suffix(".tmp")
		temporary.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
		os.replace(temporary, manifest_path)
		if output_root.exists():
			# Re-check immediately before publishing so a concurrent writer cannot
			# turn a successful staging render into a mixed, apparently valid tree.
			if not output_root.is_dir() or output_root.is_symlink() or any(output_root.iterdir()):
				raise RenderError(f"output root changed while rendering: {output_root}")
			output_root.rmdir()
		os.replace(staging_root, output_root)
		return manifest
	finally:
		if staging_root.exists():
			shutil.rmtree(staging_root, ignore_errors=True)


def render(
	plan: Mapping[str, Any], corpus_root: Path, output_root: Path, *, jobs: int = 1,
	case_ids: Sequence[str] | None = None,
) -> Mapping[str, Any]:
	PLAN.validate_plan(plan)
	return _render_validated(plan, corpus_root, output_root, jobs=jobs, case_ids=case_ids)


def _write_fixture(path: Path, rate: int, frequency: float, seconds: int) -> None:
	samples = [0.2 * math.sin(2.0 * math.pi * frequency * index / rate) for index in range(rate * seconds)]
	_write_pcm16(path, samples if rate == TARGET_RATE else _resample(samples, rate, rate))


def _write_impulse_fixture(path: Path, rate: int, reflections: Sequence[tuple[int, float]]) -> None:
	samples = [0.0] * (rate // 10)
	for delay, gain in reflections:
		samples[delay] = gain
	_write_pcm16(path, samples)


def self_test() -> None:
	# Exercise the byte-stable DSP using a minimal hand-authored valid plan.  The
	# plan validator owns broader schema/split coverage in its own self-test.
	with tempfile.TemporaryDirectory(prefix="mumble-mixture-render-") as directory:
		root = Path(directory)
		corpus = root / "corpus"
		corpus.mkdir()
		_write_fixture(corpus / "speech.wav", TARGET_RATE, 220.0, 1)
		_write_fixture(corpus / "noise.wav", TARGET_RATE, 997.0, 1)
		_write_impulse_fixture(corpus / "rir.wav", TARGET_RATE, ((0, 1.0), (337, 0.2), (911, -0.1)))
		_write_impulse_fixture(corpus / "microphone.wav", TARGET_RATE, ((0, 1.0), (2, -0.15), (7, 0.05)))
		speech_path = corpus / "speech.wav"
		noise_path = corpus / "noise.wav"
		rir_path = corpus / "rir.wav"
		microphone_path = corpus / "microphone.wav"
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
				"rir": {
					"item_id": "validation-office-v1", "relative_path": "rir.wav",
					"input_sample_rate_hz": TARGET_RATE, "input_channels": 1,
					"duration_samples": TARGET_RATE // 10, "sha256": _sha256(rir_path),
					"size_bytes": rir_path.stat().st_size, "source_artifact_sha256": artifact_sha256,
					"rt60_ms": 150,
				},
				"microphone_response": {
					"item_id": "validation-usb-v1", "relative_path": "microphone.wav",
					"input_sample_rate_hz": TARGET_RATE, "input_channels": 1,
					"duration_samples": TARGET_RATE // 10, "sha256": _sha256(microphone_path),
					"size_bytes": microphone_path.stat().st_size, "source_artifact_sha256": artifact_sha256,
					"device_family": "usb",
				},
			},
		}
		cases = []
		for index, profile in enumerate(("Light", "Balanced", "Quality"), start=1):
			item = json.loads(json.dumps(case))
			item["case_id"] = f"self-test-validation-{index:05d}"
			item["profile"] = profile
			item["startup"]["preroll_ms"] = 0 if index % 2 else 300
			cases.append(item)
		plan = {
			"format": { "duration_ms": 1000 },
			"corpus_lock_sha256": "b" * 64,
			"corpus_inventory_sha256": "c" * 64,
			"cases": cases,
		}

		output_serial = root / "serial"
		output_parallel = root / "parallel"
		serial = _render_validated(plan, corpus, output_serial, jobs=1)
		parallel = _render_validated(plan, corpus, output_parallel, jobs=3)
		if (output_serial / "render-manifest.json").read_bytes() != (output_parallel / "render-manifest.json").read_bytes():
			raise AssertionError("manifest bytes differ between serial and parallel rendering")
		serial_tree = {
			path.relative_to(output_serial).as_posix(): _sha256(path)
			for path in output_serial.rglob("*") if path.is_file()
		}
		parallel_tree = {
			path.relative_to(output_parallel).as_posix(): _sha256(path)
			for path in output_parallel.rglob("*") if path.is_file()
		}
		if serial_tree != parallel_tree:
			raise AssertionError("rendered tree differs between serial and parallel rendering")
		if serial["cases"][0]["input"]["sha256"] == serial["cases"][0]["clean_reference"]["sha256"]:
			raise AssertionError("0 dB noise did not alter the rendered input")

		filtered_root = root / "filtered"
		filtered = _render_validated(
			plan, corpus, filtered_root, jobs=2,
			case_ids=(cases[2]["case_id"], cases[0]["case_id"]),
		)
		expected_filtered_ids = [cases[0]["case_id"], cases[2]["case_id"]]
		if [entry["case_id"] for entry in filtered["cases"]] != expected_filtered_ids:
			raise AssertionError("case filter did not preserve canonical plan order")
		full_entries = {entry["case_id"]: entry for entry in parallel["cases"]}
		if filtered["cases"] != [full_entries[case_id] for case_id in expected_filtered_ids]:
			raise AssertionError("case filter changed rendered case bytes or metadata")
		if {path.parent.name for path in filtered_root.glob("*/client1-input.wav")} != set(expected_filtered_ids):
			raise AssertionError("case filter rendered an unselected case")

		for bad_ids, expected_message in (
			((cases[0]["case_id"], cases[0]["case_id"]), "duplicate"),
			((cases[0]["case_id"], cases[0]["case_id"].upper()), "Windows-normalized"),
			(("unknown-case",), "unknown"),
		):
			try:
				_render_validated(plan, corpus, root / f"rejected-{expected_message}", case_ids=bad_ids)
			except RenderError as error:
				if expected_message not in str(error):
					raise
			else:
				raise AssertionError(f"renderer accepted a {expected_message} case-id filter")
		duplicate_plan = { **plan, "cases": [cases[0], cases[0]] }
		try:
			_select_cases(duplicate_plan, None)
		except RenderError as error:
			if "duplicate" not in str(error):
				raise
		else:
			raise AssertionError("renderer accepted duplicate case ids in a plan")
		collision_plan = { **plan, "cases": json.loads(json.dumps(cases[:2])) }
		collision_plan["cases"][0]["case_id"] = "Foo"
		collision_plan["cases"][1]["case_id"] = "foo"
		try:
			_select_cases(collision_plan, None)
		except RenderError as error:
			if "Windows-normalized" not in str(error):
				raise
		else:
			raise AssertionError("renderer accepted case-insensitive Windows path collisions")
		for unsafe_case_id in (
			".", "..", "trailing.", "trailing ", "CON", "con.wav", "CON .json", "PRN.log",
			"aux", "NUL.tar.gz", "COM1", "com9.wav", "LPT1", "lpt9.bin",
		):
			try:
				_case_id(unsafe_case_id)
			except RenderError:
				pass
			else:
				raise AssertionError(f"renderer accepted Windows-unsafe case id {unsafe_case_id!r}")
		for safe_case_id in ("console", "COM0", "COM10", "LPT0", "LPT10", "auxiliary", "case.name"):
			if _case_id(safe_case_id) != safe_case_id:
				raise AssertionError(f"renderer changed safe case id {safe_case_id!r}")
		for invalid_jobs in (0, MAX_RENDER_JOBS + 1):
			try:
				_validated_jobs(invalid_jobs)
			except RenderError:
				pass
			else:
				raise AssertionError(f"renderer accepted invalid worker count {invalid_jobs}")

		corrupt = bytearray(noise_path.read_bytes())
		corrupt[-1] ^= 0x01
		noise_path.write_bytes(corrupt)
		failure_plan = json.loads(json.dumps(plan))
		failure_plan["cases"] = failure_plan["cases"][:2]
		# The first worker completes and writes its clean case before the second
		# worker's locked-noise failure is observed.  The whole staging tree must
		# still disappear without publishing a manifest.
		failure_plan["cases"][0]["noise"] = None
		failure_plan["cases"][0]["mix"]["snr_db"] = None
		failure_root = root / "worker-failure"
		try:
			_render_validated(failure_plan, corpus, failure_root, jobs=2)
		except RenderError as error:
			if "SHA-256 mismatch" not in str(error):
				raise
		else:
			raise AssertionError("renderer accepted a corpus file whose per-file hash changed")
		if failure_root.exists() or list(root.glob(".worker-failure.render-*")):
			raise AssertionError("failed worker published a partial output tree or left staging behind")


def main(argv: Sequence[str] | None = None) -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--plan", type=Path)
	parser.add_argument("--corpus-root", type=Path)
	parser.add_argument("--output-root", type=Path)
	parser.add_argument("--jobs", type=int, default=1, help=f"parallel render workers (1-{MAX_RENDER_JOBS})")
	parser.add_argument("--case-id", action="append", dest="case_ids", help="render only this case id (repeatable)")
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
		manifest = render(
			_load_json(args.plan), args.corpus_root, args.output_root,
			jobs=args.jobs, case_ids=args.case_ids,
		)
		print(f"Rendered {len(manifest['cases'])} private case(s) into {args.output_root}")
		return 0
	except (AssertionError, KeyError, RenderError, TypeError, ValueError) as error:
		print(f"Mixture renderer error: {error}", file=sys.stderr)
		return 1


if __name__ == "__main__":
	sys.exit(main())
