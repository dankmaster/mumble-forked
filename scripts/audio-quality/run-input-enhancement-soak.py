#!/usr/bin/env python3
"""Produce hash-bound realtime input-enhancement soak evidence.

The benchmark owns the streaming 10 ms product-pipeline loop.  This producer
pins the packaged payload and product catalogs, observes child RSS, rejects an
accelerated/offline substitute, and publishes only audio-free v2 reports after
all required core profiles pass.
"""

from __future__ import annotations

import argparse
import ctypes
import hashlib
import importlib.util
import json
import math
import os
import secrets
import shutil
import subprocess
import sys
import tempfile
import time
import wave
from pathlib import Path
from typing import Any, Callable, Mapping, MutableMapping, Sequence


class SoakError(RuntimeError):
	"""Raised when trustworthy soak evidence cannot be produced."""


SCRIPT_DIR = Path(__file__).resolve().parent
PROFILES = ("Balanced", "Quality", "VoiceFocus")
SAMPLE_RATE_HZ = 48_000
FRAME_SAMPLES = 480
SOAK_KIND = "mumble-input-enhancement-soak-v2"
READY_KIND = "mumble-input-enhancement-realtime-ready-v1"
QUALIFICATION_DURATION_SECONDS = 3600
SHORT_SMOKE_MAX_RSS_GROWTH_BYTES = 8 * 1024 * 1024
PERFORMANCE_BUDGETS = {
	"Balanced": {"mean_rtf": 0.15, "callback_p99_ms": 5.0, "worker_p99_ms": 5.0},
	"Quality": {"mean_rtf": 0.35, "callback_p99_ms": 8.0, "worker_p99_ms": 8.0},
	"VoiceFocus": {"mean_rtf": 0.35, "callback_p99_ms": 8.0, "worker_p99_ms": 8.0},
}


def _load_e2e_module() -> Any:
	path = SCRIPT_DIR / "run-two-client-e2e.py"
	spec = importlib.util.spec_from_file_location("mumble_soak_e2e_contract", path)
	if spec is None or spec.loader is None:
		raise SoakError(f"unable to load product catalog verifier {path}")
	module = importlib.util.module_from_spec(spec)
	spec.loader.exec_module(module)
	return module


def _canonical_bytes(value: Any) -> bytes:
	return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")


def _canonical_sha256(value: Any) -> str:
	return hashlib.sha256(_canonical_bytes(value)).hexdigest()


def _sha256(path: Path) -> str:
	digest = hashlib.sha256()
	with path.open("rb") as stream:
		for block in iter(lambda: stream.read(1024 * 1024), b""):
			digest.update(block)
	return digest.hexdigest()


def _load_json(path: Path, label: str) -> Any:
	def reject_duplicates(pairs: Sequence[tuple[str, Any]]) -> MutableMapping[str, Any]:
		result: MutableMapping[str, Any] = {}
		for key, value in pairs:
			if key in result:
				raise SoakError(f"{label}: duplicate JSON key {key!r}")
			result[key] = value
		return result

	try:
		return json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=reject_duplicates)
	except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
		raise SoakError(f"unable to read {label} {path}: {error}") from error


def _regular_file(path: Path, label: str) -> Path:
	resolved = path.resolve()
	if not resolved.is_file() or resolved.is_symlink():
		raise SoakError(f"{label} is not a regular file: {resolved}")
	return resolved


def _directory(path: Path, label: str) -> Path:
	resolved = path.resolve()
	if not resolved.is_dir() or resolved.is_symlink():
		raise SoakError(f"{label} is not a regular directory: {resolved}")
	return resolved


def _file_record(path: Path) -> Mapping[str, Any]:
	path = _regular_file(path, str(path))
	return {"sha256": _sha256(path), "size_bytes": path.stat().st_size}


def _assert_file_record(path: Path, record: Mapping[str, Any], label: str) -> None:
	current = _file_record(path)
	if current != record:
		raise SoakError(f"{label} changed while soak evidence was being produced")


def _number(value: Any, label: str, minimum: float = 0.0) -> float:
	if isinstance(value, bool) or not isinstance(value, (int, float)):
		raise SoakError(f"{label}: expected a number")
	result = float(value)
	if not math.isfinite(result) or result < minimum:
		raise SoakError(f"{label}: expected a finite value >= {minimum}")
	return result


def _integer(value: Any, label: str, minimum: int = 0) -> int:
	if isinstance(value, bool) or not isinstance(value, int) or value < minimum:
		raise SoakError(f"{label}: expected an integer >= {minimum}")
	return value


def _exact_bool(value: Any, expected: bool, label: str) -> None:
	if value is not expected:
		raise SoakError(f"{label}: expected {expected!r}")


def _assert_outside(path: Path, container: Path, label: str) -> None:
	try:
		path.resolve().relative_to(container.resolve())
	except ValueError:
		return
	raise SoakError(f"{label} must not be inside the immutable runtime payload")


def _validate_source_wav(path: Path) -> None:
	try:
		with wave.open(str(path), "rb") as stream:
			if stream.getnchannels() != 1 or stream.getsampwidth() != 2 or stream.getframerate() != SAMPLE_RATE_HZ:
				raise SoakError("source WAV must be mono PCM16 at 48 kHz")
			frames = stream.getnframes()
			if frames <= 0 or frames > 60 * SAMPLE_RATE_HZ:
				raise SoakError("source WAV must contain from >0 through 60 seconds of samples")
	except (OSError, wave.Error) as error:
		raise SoakError(f"unable to validate source WAV {path}: {error}") from error


def _resident_set_size(pid: int) -> int:
	if sys.platform == "win32":
		class ProcessMemoryCountersEx(ctypes.Structure):
			_fields_ = [
				("cb", ctypes.c_ulong), ("PageFaultCount", ctypes.c_ulong),
				("PeakWorkingSetSize", ctypes.c_size_t), ("WorkingSetSize", ctypes.c_size_t),
				("QuotaPeakPagedPoolUsage", ctypes.c_size_t), ("QuotaPagedPoolUsage", ctypes.c_size_t),
				("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t), ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
				("PagefileUsage", ctypes.c_size_t), ("PeakPagefileUsage", ctypes.c_size_t),
				("PrivateUsage", ctypes.c_size_t),
			]

		kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
		psapi = ctypes.WinDLL("psapi", use_last_error=True)
		kernel32.OpenProcess.argtypes = [ctypes.c_ulong, ctypes.c_int, ctypes.c_ulong]
		kernel32.OpenProcess.restype = ctypes.c_void_p
		kernel32.CloseHandle.argtypes = [ctypes.c_void_p]
		psapi.GetProcessMemoryInfo.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_ulong]
		handle = kernel32.OpenProcess(0x0410, 0, pid)
		if not handle:
			raise OSError(ctypes.get_last_error(), f"OpenProcess({pid}) failed")
		try:
			counters = ProcessMemoryCountersEx()
			counters.cb = ctypes.sizeof(counters)
			if not psapi.GetProcessMemoryInfo(handle, ctypes.byref(counters), counters.cb):
				raise OSError(ctypes.get_last_error(), f"GetProcessMemoryInfo({pid}) failed")
			return int(counters.WorkingSetSize)
		finally:
			kernel32.CloseHandle(handle)
	if sys.platform.startswith("linux"):
		for line in Path(f"/proc/{pid}/status").read_text(encoding="ascii").splitlines():
			if line.startswith("VmRSS:"):
				return int(line.split()[1]) * 1024
		raise OSError(f"VmRSS missing for process {pid}")
	completed = subprocess.run(
		["ps", "-o", "rss=", "-p", str(pid)], check=True, capture_output=True, text=True,
	)
	return int(completed.stdout.strip()) * 1024


def _monitor_process(
	command: Sequence[str], *, cwd: Path, stdout_path: Path, stderr_path: Path,
	duration_seconds: int, environment: Mapping[str, str], rss_reader: Callable[[int], int],
	ready_path: Path, ready_nonce: str,
) -> tuple[float, list[int], int]:
	if ready_path.exists() or ready_path.is_symlink():
		raise SoakError(f"realtime ready marker already exists before process start: {ready_path}")
	started = time.monotonic()
	warmup_after_ready = min(60.0, max(0.2, duration_seconds * 0.02))
	interval = min(0.25, max(0.025, duration_seconds / 20.0))
	timeout = duration_seconds + max(120.0, duration_seconds * 0.20)
	post_warmup: list[int] = []
	ready_seen_at: float | None = None
	ready_record: Mapping[str, Any] | None = None
	with stdout_path.open("wb") as stdout_stream, stderr_path.open("wb") as stderr_stream:
		process = subprocess.Popen(
			list(command), cwd=str(cwd), env=dict(environment), stdout=stdout_stream, stderr=stderr_stream,
		)
		try:
			while process.poll() is None:
				now = time.monotonic()
				elapsed = now - started
				if elapsed > timeout:
					raise SoakError(f"benchmark exceeded its realtime timeout after {elapsed:.3f} seconds")
				if ready_seen_at is None and (ready_path.exists() or ready_path.is_symlink()):
					document = _load_json(ready_path, "realtime ready marker")
					expected = {"kind": READY_KIND, "nonce": ready_nonce, "process_id": process.pid}
					if document != expected or ready_path.read_bytes() != _canonical_bytes(expected) + b"\n":
						raise SoakError(
							"realtime ready marker is not canonically bound to this process and nonce: "
							f"expected {expected!r}, observed {document!r}"
						)
					ready_record = _file_record(ready_path)
					ready_seen_at = now
				if ready_seen_at is not None and now - ready_seen_at >= warmup_after_ready:
					try:
						post_warmup.append(rss_reader(process.pid))
					except OSError as error:
						raise SoakError(f"unable to sample benchmark RSS: {error}") from error
				time.sleep(interval)
			return_code = process.wait()
		except BaseException:
			if process.poll() is None:
				process.kill()
				process.wait()
			raise
	wall_seconds = time.monotonic() - started
	if return_code != 0:
		return wall_seconds, post_warmup, return_code
	if ready_seen_at is None or ready_record is None:
		raise SoakError("benchmark completed without its post-initialization realtime ready marker")
	_assert_file_record(ready_path, ready_record, "realtime ready marker")
	if len(post_warmup) < 3:
		raise SoakError(f"benchmark produced only {len(post_warmup)} post-warmup RSS samples")
	return wall_seconds, post_warmup, return_code


def _validate_raw_report(
	raw: Mapping[str, Any], *, profile: str, binding: Mapping[str, Any], model_path: Path,
	recipe: Mapping[str, Any], duration_seconds: int, outer_wall_seconds: float,
	expected_latency_samples: int,
) -> Mapping[str, Any]:
	label = f"{profile} benchmark report"
	if raw.get("processing_mode") != "product-profile-realtime-soak":
		raise SoakError(f"{label}: not the explicit realtime product soak mode")
	_exact_bool(raw.get("realtime_streaming"), True, f"{label}.realtime_streaming")
	_exact_bool(raw.get("realtime_pacing"), True, f"{label}.realtime_pacing")
	if _integer(raw.get("realtime_requested_duration_seconds"), f"{label}.realtime_requested_duration_seconds", 1) != duration_seconds:
		raise SoakError(f"{label}: requested duration mismatch")
	realtime_wall_seconds = _number(raw.get("realtime_wall_ms"), f"{label}.realtime_wall_ms") / 1000.0
	if realtime_wall_seconds < duration_seconds:
		raise SoakError(f"{label}: benchmark wall duration was shorter than the requested realtime duration")
	if outer_wall_seconds < duration_seconds:
		raise SoakError(f"{label}: process wall duration was shorter than the requested realtime duration")
	if raw.get("requested_profile") != profile or raw.get("active_profile") != profile:
		raise SoakError(f"{label}: requested/active profile mismatch")
	if raw.get("active_engine") != binding["engine"]:
		raise SoakError(f"{label}: active engine mismatch")
	if raw.get("requested_recipe_id") != binding["recipe"]["id"]:
		raise SoakError(f"{label}: active recipe mismatch")
	if _integer(raw.get("recipe_revision"), f"{label}.recipe_revision", 1) != binding["recipe"]["revision"]:
		raise SoakError(f"{label}: recipe revision mismatch")
	for field in ("requested_ui_noise_reduction", "requested_ui_natural_clear"):
		if _integer(raw.get(field), f"{label}.{field}") != 70:
			raise SoakError(f"{label}.{field}: soak must use the fixed UI control value 70")
	for raw_field, catalog_field in (
		("validated_recipe_noise_reduction", "noiseReductionRange"),
		("validated_recipe_natural_clear", "naturalCrispRange"),
	):
		interval = recipe.get(catalog_field)
		if (
			not isinstance(interval, list) or len(interval) != 2
			or any(isinstance(value, bool) or not isinstance(value, int) for value in interval)
		):
			raise SoakError(f"{label}: invalid signed recipe control interval {catalog_field}")
		minimum, maximum = interval
		expected_control = minimum + ((70 * (maximum - minimum) + 50) // 100)
		if _integer(raw.get(raw_field), f"{label}.{raw_field}") != expected_control:
			raise SoakError(f"{label}.{raw_field}: does not match the one-time signed recipe mapping")
	models = binding["models"]
	if not isinstance(models, list) or len(models) != 1:
		raise SoakError(f"{label}: core neural soak requires exactly one authorized model")
	model = models[0]
	if raw.get("active_model_id") != model["id"] or raw.get("active_model_sha256") != model["sha256"]:
		raise SoakError(f"{label}: model identity/hash mismatch")
	if model["id"] == "rnnoise:embedded":
		# The embedded model is compiled into the hash-authorized RNNoise runtime
		# and intentionally keeps the historical empty-path diagnostic. The
		# packaged runtime asset itself was already verified above.
		if raw.get("active_model_path") != "":
			raise SoakError(f"{label}: embedded RNNoise must not publish an external model path")
	else:
		try:
			reported_model_path = Path(str(raw.get("active_model_path"))).resolve(strict=True)
		except (OSError, RuntimeError) as error:
			raise SoakError(f"{label}: invalid active model path") from error
		if reported_model_path != model_path or _sha256(reported_model_path) != model["sha256"]:
			raise SoakError(f"{label}: active model path is not the hash-authorized packaged asset")
	if raw.get("output_path") != "":
		raise SoakError(f"{label}: realtime soak must not create/retain an output WAV")
	if _integer(raw.get("sample_rate"), f"{label}.sample_rate", 1) != SAMPLE_RATE_HZ:
		raise SoakError(f"{label}: unexpected sample rate")
	input_samples = duration_seconds * SAMPLE_RATE_HZ
	latency = _integer(raw.get("reported_latency_samples"), f"{label}.reported_latency_samples")
	if latency != expected_latency_samples or latency % FRAME_SAMPLES:
		raise SoakError(f"{label}: declared latency does not match the signed recipe/model contract")
	output_samples = input_samples + latency
	expected_timeline = {
		"input_sample_count": input_samples,
		"output_sample_count": output_samples,
		"sample_count": output_samples,
		"drain_sample_count": latency,
		"processing_padding_sample_count": 0,
		"pacing_frame_count": output_samples // FRAME_SAMPLES,
	}
	for field, expected in expected_timeline.items():
		if _integer(raw.get(field), f"{label}.{field}") != expected:
			raise SoakError(f"{label}.{field}: expected {expected}")
	frame_count = expected_timeline["pacing_frame_count"]
	if _integer(raw.get("processed_frames"), f"{label}.processed_frames") != frame_count:
		raise SoakError(f"{label}: product pipeline did not process every paced frame")
	if _integer(raw.get("neural_frames"), f"{label}.neural_frames") != frame_count:
		raise SoakError(f"{label}: neural processor did not process every paced frame")
	worker_frames = _integer(raw.get("worker_processing_frames"), f"{label}.worker_processing_frames")
	if binding["engine"] == "DeepFilterNet" and worker_frames != frame_count:
		raise SoakError(f"{label}: DeepFilterNet worker did not process every paced frame")
	if binding["engine"] == "RNNoise" and worker_frames != 0:
		raise SoakError(f"{label}: synchronous RNNoise unexpectedly reported worker frames")
	if not math.isclose(_number(raw.get("audio_ms"), f"{label}.audio_ms"), duration_seconds * 1000.0, abs_tol=0.001):
		raise SoakError(f"{label}: audio duration mismatch")
	if raw.get("used_fallback") is not False or _integer(raw.get("fallback_count"), f"{label}.fallback_count") != 0:
		raise SoakError(f"{label}: fallback occurred")
	deadline_misses = _integer(raw.get("deadline_misses"), f"{label}.deadline_misses")
	pacing_deadline_misses = _integer(raw.get("pacing_deadline_miss_count"), f"{label}.pacing_deadline_miss_count")
	if deadline_misses or pacing_deadline_misses:
		raise SoakError(f"{label}: callback or pacing deadline miss occurred")
	if _number(raw.get("pacing_max_deadline_overrun_ms"), f"{label}.pacing_max_deadline_overrun_ms") != 0.0:
		raise SoakError(f"{label}: nonzero pacing deadline overrun without a permitted miss")
	invalid_output = _integer(raw.get("non_finite_sample_count"), f"{label}.non_finite_sample_count")
	out_of_range = _integer(raw.get("out_of_range_sample_count"), f"{label}.out_of_range_sample_count")
	if invalid_output or out_of_range:
		raise SoakError(f"{label}: invalid or out-of-range output")
	input_clipping = _integer(raw.get("input_saturated_sample_count"), f"{label}.input_saturated_sample_count")
	output_clipping = _integer(raw.get("saturated_sample_count"), f"{label}.saturated_sample_count")
	new_clipping = max(0, output_clipping - input_clipping)
	if new_clipping:
		raise SoakError(f"{label}: enhancement introduced clipping")
	mean_rtf = _number(raw.get("rtf"), f"{label}.rtf")
	callback_total = _number(
		raw.get("realtime_callback_processing_total_ms"),
		f"{label}.realtime_callback_processing_total_ms",
	)
	worker_total = _number(raw.get("worker_processing_total_ms"), f"{label}.worker_processing_total_ms")
	total_processing = _number(raw.get("processing_wall_ms"), f"{label}.processing_wall_ms")
	if not math.isclose(total_processing, callback_total + worker_total, rel_tol=1e-9, abs_tol=1e-6):
		raise SoakError(f"{label}: total RTF cost does not include callback plus neural-worker processing")
	if not math.isclose(mean_rtf, total_processing / (duration_seconds * 1000.0), rel_tol=1e-9, abs_tol=1e-9):
		raise SoakError(f"{label}: RTF does not match the complete processing cost")
	callback_p99 = _number(raw.get("callback_p99_ms"), f"{label}.callback_p99_ms")
	worker_p99 = _number(raw.get("worker_processing_p99_ms"), f"{label}.worker_processing_p99_ms")
	maximum_internal = max(
		_number(raw.get("maximum_processing_ms"), f"{label}.maximum_processing_ms"),
		_number(raw.get("worker_processing_maximum_ms"), f"{label}.worker_processing_maximum_ms"),
	)
	budget = PERFORMANCE_BUDGETS[profile]
	if mean_rtf > budget["mean_rtf"] or callback_p99 > budget["callback_p99_ms"] or worker_p99 > budget["worker_p99_ms"]:
		raise SoakError(f"{label}: profile performance budget exceeded")
	if maximum_internal > 10.0:
		raise SoakError(f"{label}: internal processing exceeded 10 ms")
	return {
		"audio_duration_seconds": float(duration_seconds),
		"wall_duration_seconds": realtime_wall_seconds,
		"declared_latency_samples": latency,
		"mean_rtf": mean_rtf,
		"callback_p99_ms": callback_p99,
		"worker_p99_ms": worker_p99,
		"maximum_internal_processing_ms": maximum_internal,
		"deadline_miss_count": deadline_misses + pacing_deadline_misses,
		"fallback_count": 0,
		"invalid_output_count": invalid_output + out_of_range,
		"new_clipping_count": new_clipping,
		"tail_drain_failure_count": 0,
	}


def _write_atomic(path: Path, payload: bytes) -> None:
	path.parent.mkdir(parents=True, exist_ok=True)
	with tempfile.NamedTemporaryFile(dir=path.parent, prefix=f".{path.name}.", suffix=".tmp", delete=False) as stream:
		temporary = Path(stream.name)
		stream.write(payload)
		stream.flush()
		os.fsync(stream.fileno())
	try:
		os.replace(temporary, path)
	except BaseException:
		temporary.unlink(missing_ok=True)
		raise


def _validate_memory_growth(duration_seconds: int, memory_growth: int, profile: str) -> None:
	if duration_seconds >= QUALIFICATION_DURATION_SECONDS and memory_growth > 0:
		raise SoakError(f"{profile}: RSS grew by {memory_growth} bytes after warmup")
	if duration_seconds < QUALIFICATION_DURATION_SECONDS and memory_growth > SHORT_SMOKE_MAX_RSS_GROWTH_BYTES:
		raise SoakError(
			f"{profile}: short-smoke RSS grew by {memory_growth} bytes, above the diagnostic bound"
		)


def run_campaign(
	args: argparse.Namespace, *, allow_fake_tools: bool = False,
	rss_reader: Callable[[int], int] = _resident_set_size,
) -> list[Mapping[str, Any]]:
	duration_seconds = _integer(args.duration_seconds, "duration seconds", 1)
	if duration_seconds > 14_400:
		raise SoakError("duration seconds must be <= 14400")
	runtime_root = _directory(args.runtime_root, "runtime root")
	benchmark = _regular_file(args.benchmark, "speech_cleanup_benchmark")
	client = _regular_file(args.client, "client binary")
	server = _regular_file(args.server, "server binary")
	run_provenance = _regular_file(args.run_provenance, "canonical run provenance")
	model_manifest = _regular_file(args.model_manifest or runtime_root / "input-models.json", "input-models.json")
	recipe_manifest = _regular_file(args.recipe_manifest or runtime_root / "input-recipes.json", "input-recipes.json")
	source_wav = _regular_file(args.source_wav, "short source WAV")
	if client != runtime_root / "mumble.exe":
		raise SoakError("client binary must be the exact packaged runtime-root mumble.exe")
	if model_manifest != runtime_root / "input-models.json" or recipe_manifest != runtime_root / "input-recipes.json":
		raise SoakError("model and recipe manifests must be the exact packaged runtime-root manifests")
	if not allow_fake_tools and benchmark.name.lower() != "speech_cleanup_benchmark.exe":
		raise SoakError("release soak requires speech_cleanup_benchmark.exe")
	_validate_source_wav(source_wav)
	_assert_outside(source_wav, runtime_root, "private source WAV")
	output_root = args.output_root.resolve()
	if output_root.exists():
		raise SoakError(f"output root must not already exist: {output_root}")
	_assert_outside(output_root, runtime_root, "output root")
	if args.measurement_fragment is not None:
		fragment_path = args.measurement_fragment.resolve()
		if fragment_path.exists():
			raise SoakError(f"measurement fragment must not already exist: {fragment_path}")
		_assert_outside(fragment_path, runtime_root, "measurement fragment")
		try:
			fragment_relative_root = output_root.relative_to(fragment_path.parent.resolve())
		except ValueError as error:
			raise SoakError("output root must be below the measurement fragment directory") from error
	else:
		fragment_path = None
		fragment_relative_root = None

	provenance_document = _load_json(run_provenance, "run provenance")
	if not isinstance(provenance_document, dict):
		raise SoakError("run provenance must be a JSON object")
	if run_provenance.read_bytes() != _canonical_bytes(provenance_document) + b"\n":
		raise SoakError("run provenance must be canonical sorted-key UTF-8 JSON with one LF")

	e2e = _load_e2e_module()
	try:
		e2e._assert_packaged_runtime_root(runtime_root)
		catalog = e2e._verify_product_catalog(model_manifest, recipe_manifest, runtime_root)
		runtime_record = e2e._tree_attestation(runtime_root)
	except Exception as error:
		raise SoakError(f"packaged product catalog verification failed: {error}") from error
	bindings: dict[str, Mapping[str, Any]] = {}
	for profile in PROFILES:
		matches = [binding for binding in catalog["bindings"] if binding["profile"] == profile]
		if len(matches) != 1:
			raise SoakError(f"{profile} must have exactly one public product binding")
		bindings[profile] = matches[0]
	model_document = _load_json(model_manifest, "input-models.json")
	model_entries = {str(item["id"]): item for item in model_document["models"]}
	recipe_document = _load_json(recipe_manifest, "input-recipes.json")
	recipe_entries = {str(item["id"]): item for item in recipe_document["recipes"]}
	model_paths: dict[str, Path] = {}
	for profile, binding in bindings.items():
		model = binding["models"][0]
		entry = model_entries.get(model["id"])
		if not isinstance(entry, dict):
			raise SoakError(f"{profile}: authorized model is absent from input-models.json")
		model_path = (runtime_root / str(entry["path"])).resolve()
		try:
			model_path.relative_to(runtime_root)
		except ValueError as error:
			raise SoakError(f"{profile}: model path escapes the runtime payload") from error
		if _sha256(model_path) != model["sha256"]:
			raise SoakError(f"{profile}: model asset changed after catalog verification")
		model_paths[profile] = model_path

	tracked_paths = {
		"benchmark": benchmark, "client": client, "server": server, "run provenance": run_provenance,
		"model manifest": model_manifest, "recipe manifest": recipe_manifest, "source WAV": source_wav,
	}
	tracked_records = {label: _file_record(path) for label, path in tracked_paths.items()}
	execution_identity = {
		"client_binary_sha256": tracked_records["client"]["sha256"],
		"server_binary_sha256": tracked_records["server"]["sha256"],
		"model_manifest_sha256": tracked_records["model manifest"]["sha256"],
		"recipe_manifest_sha256": tracked_records["recipe manifest"]["sha256"],
		"run_provenance_sha256": _canonical_sha256(provenance_document),
		"runtime_payload_sha256": runtime_record["sha256"],
	}

	reports: list[Mapping[str, Any]] = []
	with tempfile.TemporaryDirectory(prefix="mumble-input-enhancement-soak-") as temporary_text:
		temporary_root = Path(temporary_text).resolve()
		try:
			temporary_root.relative_to(Path(tempfile.gettempdir()).resolve())
		except ValueError as error:
			raise SoakError("temporary soak workspace escaped the operating-system temp root") from error
		for index, profile in enumerate(PROFILES, 1):
			binding = bindings[profile]
			model = binding["models"][0]
			raw_report_path = temporary_root / f"{index:02d}-{profile.lower()}-raw.json"
			ready_path = temporary_root / f"{index:02d}-{profile.lower()}-ready.json"
			ready_nonce = secrets.token_hex(32)
			stdout_path = temporary_root / f"{index:02d}-{profile.lower()}.stdout.log"
			stderr_path = temporary_root / f"{index:02d}-{profile.lower()}.stderr.log"
			runtime_benchmark = runtime_root / f"speech_cleanup_benchmark-soak-{os.getpid()}-{index}{benchmark.suffix}"
			if runtime_benchmark.exists():
				raise SoakError(f"temporary runtime benchmark collision: {runtime_benchmark}")
			shutil.copy2(benchmark, runtime_benchmark)
			if _sha256(runtime_benchmark) != tracked_records["benchmark"]["sha256"]:
				runtime_benchmark.unlink(missing_ok=True)
				raise SoakError("temporary runtime benchmark copy failed hash verification")
			command = [str(runtime_benchmark)]
			if allow_fake_tools:
				command.insert(0, sys.executable)
			command.extend([
				"--profile", profile,
				"--noise-reduction", "70",
				"--natural-clear", "70",
				"--cpu-class", "High",
				"--input", str(source_wav),
				"--report", str(raw_report_path),
				"--authorized-model-sha256", model["sha256"],
				"--authorized-model-path", str(model_paths[profile]),
				"--realtime-pace",
				"--soak-duration-seconds", str(duration_seconds),
				"--realtime-ready-file", str(ready_path),
				"--realtime-ready-nonce", ready_nonce,
			])
			environment = dict(os.environ)
			if allow_fake_tools:
				environment["MUMBLE_SOAK_SELF_TEST_FAULT"] = str(getattr(args, "_self_test_fault", ""))
			try:
				outer_wall, rss_samples, return_code = _monitor_process(
					command, cwd=runtime_root, stdout_path=stdout_path, stderr_path=stderr_path,
					duration_seconds=duration_seconds, environment=environment, rss_reader=rss_reader,
					ready_path=ready_path, ready_nonce=ready_nonce,
				)
			finally:
				runtime_benchmark.unlink(missing_ok=True)
			if return_code != 0:
				stderr = stderr_path.read_text(encoding="utf-8", errors="replace")[-2000:]
				raise SoakError(f"{profile} benchmark failed with exit {return_code}: {stderr}")
			for label, path in tracked_paths.items():
				_assert_file_record(path, tracked_records[label], label)
			try:
				current_runtime = e2e._tree_attestation(runtime_root)
			except Exception as error:
				raise SoakError(f"unable to re-attest runtime payload after {profile}: {error}") from error
			if current_runtime["sha256"] != runtime_record["sha256"]:
				raise SoakError(f"runtime payload changed during {profile} soak")
			raw = _load_json(raw_report_path, f"{profile} raw benchmark report")
			if not isinstance(raw, dict):
				raise SoakError(f"{profile} raw benchmark report must be an object")
			validated = _validate_raw_report(
				raw, profile=profile, binding=binding, model_path=model_paths[profile],
				recipe=recipe_entries[binding["recipe"]["id"]], duration_seconds=duration_seconds,
				outer_wall_seconds=outer_wall,
				expected_latency_samples=catalog["expected_latency_samples_by_recipe_id"][binding["recipe"]["id"]],
			)
			rss_warmup, rss_end, rss_peak = rss_samples[0], rss_samples[-1], max(rss_samples)
			memory_growth = rss_end - rss_warmup
			_validate_memory_growth(duration_seconds, memory_growth, profile)
			reports.append({
				"schema_version": 2,
				"kind": SOAK_KIND,
				"status": "completed",
				"profile": profile,
				"active_bindings": [binding],
				"execution_identity": execution_identity,
				**validated,
				"rss_warmup_bytes": rss_warmup,
				"rss_end_bytes": rss_end,
				"rss_peak_bytes": rss_peak,
				"memory_growth_bytes_after_warmup": memory_growth,
			})

	output_root.parent.mkdir(parents=True, exist_ok=True)
	staging = Path(tempfile.mkdtemp(prefix=f".{output_root.name}.", dir=output_root.parent))
	try:
		filenames = ("01-balanced.json", "02-quality.json", "03-voice-focus.json")
		payloads: list[bytes] = []
		for filename, report in zip(filenames, reports, strict=True):
			payload = _canonical_bytes(report) + b"\n"
			(staging / filename).write_bytes(payload)
			payloads.append(payload)
		os.replace(staging, output_root)
	except BaseException:
		shutil.rmtree(staging, ignore_errors=True)
		raise

	if fragment_path is not None:
		assert fragment_relative_root is not None
		fragment = {"soak_reports": []}
		for filename, report in zip(filenames, reports, strict=True):
			payload = (output_root / filename).read_bytes()
			fragment["soak_reports"].append({
				"profile": report["profile"],
				"report": {
					"contains_audio_samples": False,
					"path": (fragment_relative_root / filename).as_posix(),
					"sha256": hashlib.sha256(payload).hexdigest(),
					"size_bytes": len(payload),
				},
			})
		_write_atomic(fragment_path, _canonical_bytes(fragment) + b"\n")
	return reports


def _parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--benchmark", type=Path)
	parser.add_argument("--runtime-root", type=Path)
	parser.add_argument("--client", type=Path)
	parser.add_argument("--server", type=Path)
	parser.add_argument("--run-provenance", type=Path)
	parser.add_argument("--model-manifest", type=Path)
	parser.add_argument("--recipe-manifest", type=Path)
	parser.add_argument("--source-wav", type=Path)
	parser.add_argument("--output-root", type=Path)
	parser.add_argument("--measurement-fragment", type=Path)
	parser.add_argument("--duration-seconds", type=int, default=3600)
	parser.add_argument("--self-test", action="store_true")
	args = parser.parse_args(argv)
	if not args.self_test:
		for name in ("benchmark", "runtime_root", "client", "server", "run_provenance", "source_wav", "output_root"):
			if getattr(args, name) is None:
				parser.error(f"--{name.replace('_', '-')} is required")
	return args


def main(argv: Sequence[str] | None = None) -> int:
	args = _parse_args(argv)
	if args.self_test:
		completed = subprocess.run([sys.executable, str(SCRIPT_DIR / "test-input-enhancement-soak.py")], check=False)
		return completed.returncode
	try:
		run_campaign(args)
	except SoakError as error:
		print(f"input enhancement soak: {error}", file=sys.stderr)
		return 1
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
