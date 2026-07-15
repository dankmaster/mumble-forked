#!/usr/bin/env python3
"""Score received PCM on an attested fixed timeline without correlation shifts.

This small scorer covers deterministic transport/health invariants that must not
be hidden by perceptual alignment: declared latency, tail drain, speech-edge
loss, clipping and a loudness-matched fixed-timeline SDR.  DNSMOS, eSTOI and WER
remain trusted-harness metrics and can be joined by case ID later.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import struct
import sys
import tempfile
import wave
from pathlib import Path
from typing import Mapping, Sequence


class ScoreError(ValueError):
	pass


def _sha256(path: Path) -> str:
	digest = hashlib.sha256()
	with path.open("rb") as stream:
		for chunk in iter(lambda: stream.read(1024 * 1024), b""):
			digest.update(chunk)
	return digest.hexdigest()


def _read_mono_wav(path: Path) -> tuple[int, list[float], int]:
	try:
		contents = path.read_bytes()
	except OSError as error:
		raise ScoreError(f"unable to read {path}: {error}") from error
	if len(contents) < 12 or contents[:4] != b"RIFF" or contents[8:12] != b"WAVE":
		raise ScoreError(f"expected a RIFF/WAVE file: {path}")
	declared_size = struct.unpack_from("<I", contents, 4)[0] + 8
	if declared_size > len(contents) or declared_size < 12:
		raise ScoreError(f"truncated or unfinalized RIFF header: {path}")

	fmt: tuple[int, int, int, int] | None = None
	audio = b""
	offset = 12
	while offset + 8 <= declared_size:
		chunk_id = contents[offset : offset + 4]
		chunk_size = struct.unpack_from("<I", contents, offset + 4)[0]
		chunk_start = offset + 8
		chunk_end = chunk_start + chunk_size
		if chunk_end > declared_size or chunk_end > len(contents):
			raise ScoreError(f"truncated WAV chunk in {path}")
		chunk = contents[chunk_start:chunk_end]
		if chunk_id == b"fmt ":
			if len(chunk) < 16:
				raise ScoreError(f"invalid WAV fmt chunk: {path}")
			format_code, channels, rate, _, block_align, bits = struct.unpack_from("<HHIIHH", chunk, 0)
			if format_code == 0xFFFE:
				if len(chunk) < 40:
					raise ScoreError(f"invalid extensible WAV fmt chunk: {path}")
				format_code = struct.unpack_from("<H", chunk, 24)[0]
			fmt = (format_code, channels, rate, bits)
			if block_align != channels * ((bits + 7) // 8):
				raise ScoreError(f"inconsistent WAV block alignment: {path}")
		elif chunk_id == b"data":
			if chunk_size == 0 and len(contents) > chunk_start:
				raise ScoreError(f"unfinalized WAV data chunk: {path}")
			audio = chunk
		offset = chunk_end + (chunk_size & 1)
	if fmt is None or not audio:
		raise ScoreError(f"WAV has no finalized fmt/data chunks: {path}")

	format_code, channels, rate, bits = fmt
	if channels != 1:
		raise ScoreError(f"expected mono WAV, got {channels} channels: {path}")
	clipped = 0
	if format_code == 3 and bits == 32:
		if len(audio) % 4:
			raise ScoreError(f"misaligned float WAV payload: {path}")
		values = list(struct.unpack(f"<{len(audio) // 4}f", audio))
		if any(not math.isfinite(value) for value in values):
			raise ScoreError(f"float WAV contains NaN/Inf: {path}")
		clipped = sum(1 for value in values if abs(value) >= 1.0)
		return rate, values, clipped
	if format_code != 1:
		raise ScoreError(f"unsupported WAV format {format_code}/{bits} bit: {path}")
	if bits == 16:
		if len(audio) % 2:
			raise ScoreError(f"misaligned PCM16 WAV payload: {path}")
		integers = struct.unpack(f"<{len(audio) // 2}h", audio)
		clipped = sum(1 for value in integers if value in (-32768, 32767))
		return rate, [value / 32768.0 for value in integers], clipped
	if bits == 24:
		if len(audio) % 3:
			raise ScoreError(f"misaligned PCM24 WAV payload: {path}")
		integers = []
		for index in range(0, len(audio), 3):
			value = int.from_bytes(audio[index : index + 3], "little", signed=False)
			integers.append(value - (1 << 24) if value & 0x800000 else value)
		clipped = sum(1 for value in integers if value in (-8_388_608, 8_388_607))
		return rate, [value / 8_388_608.0 for value in integers], clipped
	if bits == 32:
		if len(audio) % 4:
			raise ScoreError(f"misaligned PCM32 WAV payload: {path}")
		integers = struct.unpack(f"<{len(audio) // 4}i", audio)
		clipped = sum(1 for value in integers if value in (-2_147_483_648, 2_147_483_647))
		return rate, [value / 2_147_483_648.0 for value in integers], clipped
	raise ScoreError(f"unsupported PCM width {bits}: {path}")


def _frame_rms(samples: Sequence[float], frame_samples: int) -> list[float]:
	return [
		math.sqrt(sum(value * value for value in samples[start : start + frame_samples]) / frame_samples)
		for start in range(0, len(samples) - frame_samples + 1, frame_samples)
	]


def _active_edges(samples: Sequence[float], frame_samples: int) -> tuple[int, int, float]:
	levels = _frame_rms(samples, frame_samples)
	if not levels:
		raise ScoreError("audio is shorter than one 10 ms frame")
	threshold = max(10.0 ** (-50.0 / 20.0), max(levels) * 0.05)
	active = [index for index, value in enumerate(levels) if value >= threshold]
	if not active:
		raise ScoreError("reference contains no detectable speech/activity")
	return active[0] * frame_samples, min(len(samples), (active[-1] + 1) * frame_samples), threshold


def score(
	reference_path: Path,
	received_path: Path,
	latency_samples: int,
	transport_baseline_path: Path | None = None,
	transport_baseline_latency_samples: int = 0,
	qualified_transport_baseline: bool = False,
) -> Mapping[str, object]:
	if latency_samples < 0:
		raise ScoreError("latency must be non-negative")
	if transport_baseline_latency_samples < 0:
		raise ScoreError("transport baseline latency must be non-negative")
	reference_rate, reference, reference_clipped = _read_mono_wav(reference_path)
	received_rate, received, received_clipped = _read_mono_wav(received_path)
	if reference_rate != received_rate:
		raise ScoreError(f"sample-rate mismatch: {reference_rate} != {received_rate}")
	frame_samples = reference_rate // 100
	if frame_samples * 100 != reference_rate:
		raise ScoreError("sample rate does not have an exact 10 ms frame")

	expected_length = latency_samples + len(reference)
	missing_tail = max(0, expected_length - len(received))
	comparison_length = min(len(reference), max(0, len(received) - latency_samples))
	if comparison_length < frame_samples:
		raise ScoreError("received capture has no comparable fixed-timeline audio")
	expected = reference[:comparison_length]
	actual = received[latency_samples : latency_samples + comparison_length]

	# Loudness matching is a scalar amplitude fit only. No temporal search or
	# correlation alignment is performed.
	actual_energy = sum(value * value for value in actual)
	gain = 1.0 if actual_energy <= 1e-20 else sum(a * b for a, b in zip(expected, actual)) / actual_energy
	matched = [value * gain for value in actual]
	error_energy = sum((left - right) ** 2 for left, right in zip(expected, matched))
	reference_energy = sum(value * value for value in expected)
	fixed_sdr_db = 10.0 * math.log10(max(reference_energy, 1e-20) / max(error_energy, 1e-20))

	reference_onset, reference_end, threshold = _active_edges(reference, frame_samples)
	baseline_record: Mapping[str, object] | None = None
	onset_baseline_adjustment = 0
	if transport_baseline_path is not None:
		baseline_rate, baseline, baseline_clipped = _read_mono_wav(transport_baseline_path)
		if baseline_rate != reference_rate:
			raise ScoreError(f"transport baseline sample-rate mismatch: {baseline_rate} != {reference_rate}")
		baseline_levels = _frame_rms(baseline, frame_samples)
		baseline_active = [index for index, value in enumerate(baseline_levels) if value >= threshold]
		if not baseline_active:
			raise ScoreError("transport baseline contains no detectable speech/activity")
		baseline_onset = baseline_active[0] * frame_samples
		baseline_end = min(len(baseline), (baseline_active[-1] + 1) * frame_samples)
		raw_onset_offset = baseline_onset - (reference_onset + transport_baseline_latency_samples)
		# A negative control offset cannot justify moving the target's expected
		# onset earlier. Only the positive startup delay in the unchanged Original
		# transport is removed from the enhancement edge-loss gate.
		# An unqualified/self control may account for at most one 10 ms frame of
		# route-start jitter. Once that Original control has independently passed
		# the fixed-timeline edge/tail/clipping gates, its complete observed route
		# offset may be removed from a paired enhancement case. That measures only
		# latency/loss added by enhancement without allowing a broken control to
		# bless itself.
		onset_baseline_adjustment = max(0, raw_onset_offset)
		if not qualified_transport_baseline:
			onset_baseline_adjustment = min(frame_samples, onset_baseline_adjustment)
		baseline_record = {
			"sha256": _sha256(transport_baseline_path),
			"qualification": (
				"caller-verified-passing-original"
				if qualified_transport_baseline
				else "unqualified-one-frame-cap"
			),
			"declared_latency_samples": transport_baseline_latency_samples,
			"received_samples": len(baseline),
			"received_onset_samples": baseline_onset,
			"received_end_samples": baseline_end,
			"raw_onset_offset_samples": raw_onset_offset,
			"applied_onset_adjustment_samples": onset_baseline_adjustment,
			"clipped_samples": baseline_clipped,
		}
	received_levels = _frame_rms(received, frame_samples)
	received_active = [index for index, value in enumerate(received_levels) if value >= threshold]
	if received_active:
		received_onset = received_active[0] * frame_samples
		received_end = min(len(received), (received_active[-1] + 1) * frame_samples)
		expected_onset = reference_onset + latency_samples + onset_baseline_adjustment
		onset_loss = max(0, received_onset - expected_onset)
		end_loss = max(0, (reference_end + latency_samples) - received_end)
	else:
		received_onset = None
		received_end = None
		expected_onset = reference_onset + latency_samples + onset_baseline_adjustment
		onset_loss = reference_end - reference_onset
		end_loss = reference_end - reference_onset

	return {
		"schema_version": 1,
		"scorer": "mumble-fixed-timeline-v1",
		"timeline_alignment": "fixed-paired-original-onset" if baseline_record else "fixed",
		"sample_rate_hz": reference_rate,
		"frame_samples": frame_samples,
		"declared_latency_samples": latency_samples,
		"reference_sha256": _sha256(reference_path),
		"received_sha256": _sha256(received_path),
		"reference_samples": len(reference),
		"received_samples": len(received),
		"compared_samples": comparison_length,
		"missing_tail_samples": missing_tail,
		"loudness_match_gain": gain,
		"fixed_timeline_sdr_db": fixed_sdr_db,
		"reference_onset_samples": reference_onset,
		"reference_end_samples": reference_end,
		"expected_onset_samples": expected_onset,
		"received_onset_samples": received_onset,
		"received_end_samples": received_end,
		"onset_loss_samples": onset_loss,
		"end_loss_samples": end_loss,
		"reference_clipped_samples": reference_clipped,
		"received_clipped_samples": received_clipped,
		"transport_baseline": baseline_record,
	}


def _write_wave(path: Path, samples: Sequence[float], rate: int = 48_000) -> None:
	pcm = b"".join(struct.pack("<h", round(max(-0.999, min(0.999, value)) * 32767.0)) for value in samples)
	with wave.open(str(path), "wb") as stream:
		stream.setnchannels(1)
		stream.setsampwidth(2)
		stream.setframerate(rate)
		stream.writeframes(pcm)


def self_test() -> None:
	with tempfile.TemporaryDirectory(prefix="mumble-fixed-score-") as directory:
		root = Path(directory)
		reference = [0.0] * 480 + [0.2 * math.sin(index / 11.0) for index in range(4_800)] + [0.0] * 480
		latency = 1_440
		received = [0.0] * latency + reference
		_write_wave(root / "reference.wav", reference)
		_write_wave(root / "received.wav", received)
		result = score(root / "reference.wav", root / "received.wav", latency)
		if result["missing_tail_samples"] != 0 or result["onset_loss_samples"] != 0 or result["end_loss_samples"] != 0:
			raise AssertionError("exact delayed signal failed fixed-timeline edge checks")
		if float(result["fixed_timeline_sdr_db"]) < 80.0:
			raise AssertionError("exact delayed signal has unexpectedly low fixed-timeline SDR")
		wrong = score(root / "reference.wav", root / "received.wav", 0)
		if float(wrong["fixed_timeline_sdr_db"]) >= float(result["fixed_timeline_sdr_db"]):
			raise AssertionError("undeclared delay was hidden by scorer")

		transport_delay = 480
		target_latency = 960
		baseline = [0.0] * transport_delay + reference
		target = [0.0] * (transport_delay + target_latency) + reference
		_write_wave(root / "transport-baseline.wav", baseline)
		_write_wave(root / "transport-target.wav", target)
		paired = score(
			root / "reference.wav",
			root / "transport-target.wav",
			target_latency,
			root / "transport-baseline.wav",
			0,
		)
		if paired["timeline_alignment"] != "fixed-paired-original-onset" or paired["onset_loss_samples"] != 0:
			raise AssertionError("paired Original startup baseline did not preserve the fixed onset timeline")
		late = [0.0] * (transport_delay + target_latency + 960) + reference
		_write_wave(root / "transport-target-late.wav", late)
		late_result = score(
			root / "reference.wav",
			root / "transport-target-late.wav",
			target_latency,
			root / "transport-baseline.wav",
			0,
		)
		if late_result["onset_loss_samples"] != 960:
			raise AssertionError("paired baseline hid an enhancement-only onset regression")

		late_control_delay = 1_440
		late_control = [0.0] * late_control_delay + reference
		late_control_target = [0.0] * (late_control_delay + target_latency) + reference
		_write_wave(root / "transport-baseline-late.wav", late_control)
		_write_wave(root / "transport-target-control-late.wav", late_control_target)
		late_control_result = score(
			root / "reference.wav",
			root / "transport-target-control-late.wav",
			target_latency,
			root / "transport-baseline-late.wav",
			0,
		)
		if late_control_result["onset_loss_samples"] != 960:
			raise AssertionError("an excessively late Original control hid startup loss")

		qualified_control_result = score(
			root / "reference.wav",
			root / "transport-target-control-late.wav",
			target_latency,
			root / "transport-baseline-late.wav",
			0,
			True,
		)
		if qualified_control_result["onset_loss_samples"] != 0:
			raise AssertionError("a separately qualified Original control was not applied to the paired timeline")
		qualified_target_late = [0.0] * (late_control_delay + target_latency + 960) + reference
		_write_wave(root / "transport-target-qualified-late.wav", qualified_target_late)
		qualified_late_result = score(
			root / "reference.wav",
			root / "transport-target-qualified-late.wav",
			target_latency,
			root / "transport-baseline-late.wav",
			0,
			True,
		)
		if qualified_late_result["onset_loss_samples"] != 960:
			raise AssertionError("qualified Original offset hid enhancement-only startup loss")


def _write_json(path: Path, value: Mapping[str, object]) -> None:
	path.parent.mkdir(parents=True, exist_ok=True)
	temporary = path.with_name(f".{path.name}.{os.getpid()}.tmp")
	temporary.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")
	os.replace(temporary, path)


def main(argv: Sequence[str] | None = None) -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--reference", type=Path)
	parser.add_argument("--received", type=Path)
	parser.add_argument("--latency-samples", type=int)
	parser.add_argument("--transport-baseline", type=Path)
	parser.add_argument("--transport-baseline-latency-samples", type=int, default=0)
	parser.add_argument(
		"--qualified-transport-baseline",
		action="store_true",
		help="use the full paired Original route offset after the caller independently qualified that control",
	)
	parser.add_argument("--output", type=Path)
	parser.add_argument("--max-edge-loss-samples", type=int, default=480)
	parser.add_argument("--require-complete-tail", action="store_true")
	parser.add_argument("--fail-on-new-clipping", action="store_true")
	parser.add_argument("--self-test", action="store_true")
	args = parser.parse_args(argv)
	try:
		if args.self_test:
			self_test()
			print("Fixed-timeline scorer self-test: ok")
			if args.reference is None:
				return 0
		if args.reference is None or args.received is None or args.latency_samples is None or args.output is None:
			raise ScoreError("--reference, --received, --latency-samples, and --output are required")
		result = score(
			args.reference,
			args.received,
			args.latency_samples,
			args.transport_baseline,
			args.transport_baseline_latency_samples,
			args.qualified_transport_baseline,
		)
		passed = (
			int(result["onset_loss_samples"]) <= args.max_edge_loss_samples
			and int(result["end_loss_samples"]) <= args.max_edge_loss_samples
			and (not args.require_complete_tail or int(result["missing_tail_samples"]) == 0)
			and (
				not args.fail_on_new_clipping
				or int(result["received_clipped_samples"]) <= int(result["reference_clipped_samples"])
			)
		)
		result = { **result, "passed": passed }
		_write_json(args.output, result)
		print(f"Fixed-timeline score: {'passed' if passed else 'failed'}; {args.output}")
		return 0 if passed else 1
	except (AssertionError, OSError, ScoreError, ValueError) as error:
		print(f"Fixed-timeline scorer error: {error}", file=sys.stderr)
		return 1


if __name__ == "__main__":
	sys.exit(main())
