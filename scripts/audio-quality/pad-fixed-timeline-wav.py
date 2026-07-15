#!/usr/bin/env python3
"""Zero-pad paired PCM WAV fixtures to an attested packet/frame boundary.

The tool never trims or rewrites source samples. It appends digital silence to
both client-1 input and clean reference until they share a sample count aligned
to LCM(480, frames-per-packet * 480). The resulting fixed timeline prevents a
partial source frame from being mistaken for receiver tail loss.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import sys
import tempfile
import wave
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, Sequence


SAMPLE_RATE_HZ = 48000
FRAME_SAMPLES = 480
SUPPORTED_FRAMES_PER_PACKET = (1, 2, 4)


class PaddingError(RuntimeError):
	"""Raised when a source WAV cannot be padded without changing samples."""


@dataclass(frozen=True)
class PcmWave:
	channels: int
	sample_width: int
	sample_rate: int
	frame_count: int
	pcm: bytes


def sha256_bytes(value: bytes) -> str:
	return hashlib.sha256(value).hexdigest()


def read_pcm_wave(path: Path) -> PcmWave:
	try:
		file_bytes = path.read_bytes()
		with wave.open(str(path), "rb") as source:
			if source.getcomptype() != "NONE":
				raise PaddingError(f"{path}: compressed WAV input is not supported")
			channels = source.getnchannels()
			sample_width = source.getsampwidth()
			sample_rate = source.getframerate()
			frame_count = source.getnframes()
			pcm = source.readframes(frame_count)
	except (OSError, EOFError, wave.Error) as error:
		raise PaddingError(f"unable to read PCM WAV {path}: {error}") from error

	if channels != 1 or sample_rate != SAMPLE_RATE_HZ:
		raise PaddingError(f"{path}: expected mono {SAMPLE_RATE_HZ} Hz PCM WAV")
	if sample_width not in (1, 2, 3, 4):
		raise PaddingError(f"{path}: unsupported PCM sample width {sample_width}")
	if len(pcm) != frame_count * channels * sample_width:
		raise PaddingError(f"{path}: PCM payload length does not match its WAV header")
	# Force the source file read above so callers can separately attest its full
	# container hash without accepting a path that changed during parsing.
	if not file_bytes:
		raise PaddingError(f"{path}: source WAV is empty")
	return PcmWave(channels, sample_width, sample_rate, frame_count, pcm)


def write_pcm_wave(path: Path, source: PcmWave, pcm: bytes) -> bytes:
	path.parent.mkdir(parents=True, exist_ok=True)
	if len(pcm) % (source.channels * source.sample_width) != 0:
		raise PaddingError(f"{path}: padded PCM payload is not sample aligned")
	temporary_path: Path | None = None
	try:
		with tempfile.NamedTemporaryFile(prefix=path.name + ".", suffix=".tmp", dir=path.parent, delete=False) as stream:
			temporary_path = Path(stream.name)
		with wave.open(str(temporary_path), "wb") as destination:
			destination.setnchannels(source.channels)
			destination.setsampwidth(source.sample_width)
			destination.setframerate(source.sample_rate)
			destination.writeframes(pcm)
		os.replace(temporary_path, path)
		return path.read_bytes()
	finally:
		if temporary_path is not None:
			try:
				temporary_path.unlink()
			except FileNotFoundError:
				pass


def fixture_record(source_path: Path, output_path: Path, source: PcmWave, output_bytes: bytes, target_samples: int) -> Dict[str, Any]:
	output_pcm_bytes = target_samples * source.channels * source.sample_width
	return {
		"source_path": str(source_path.resolve()),
		"source_sha256": sha256_bytes(source_path.read_bytes()),
		"source_pcm_sha256": sha256_bytes(source.pcm),
		"source_sample_count": source.frame_count,
		"output_path": str(output_path.resolve()),
		"output_sha256": sha256_bytes(output_bytes),
		"output_pcm_sha256": sha256_bytes(output_bytes[-output_pcm_bytes:]),
		"output_sample_count": target_samples,
		"zero_pad_sample_count": target_samples - source.frame_count,
		"sample_rate_hz": source.sample_rate,
		"channels": source.channels,
		"sample_width_bytes": source.sample_width,
	}


def pad_pair(
	input_path: Path,
	clean_path: Path,
	output_input_path: Path,
	output_clean_path: Path,
	attestation_path: Path,
	frames_per_packet: int,
) -> Dict[str, Any]:
	if frames_per_packet not in SUPPORTED_FRAMES_PER_PACKET:
		raise PaddingError(f"frames_per_packet must be one of {SUPPORTED_FRAMES_PER_PACKET}")
	if input_path.resolve() in (output_input_path.resolve(), output_clean_path.resolve()) or clean_path.resolve() in (
		output_input_path.resolve(),
		output_clean_path.resolve(),
	):
		raise PaddingError("output paths must not overwrite either source WAV")
	if output_input_path.resolve() == output_clean_path.resolve():
		raise PaddingError("input and clean-reference outputs must be different files")

	input_wave = read_pcm_wave(input_path)
	clean_wave = read_pcm_wave(clean_path)
	alignment_samples = math.lcm(FRAME_SAMPLES, frames_per_packet * FRAME_SAMPLES)
	target_samples = (
		(max(input_wave.frame_count, clean_wave.frame_count) + alignment_samples - 1) // alignment_samples
	) * alignment_samples
	if target_samples <= 0:
		raise PaddingError("paired WAV fixtures contain no samples")

	input_silence_sample = b"\x80" if input_wave.sample_width == 1 else bytes(input_wave.sample_width)
	clean_silence_sample = b"\x80" if clean_wave.sample_width == 1 else bytes(clean_wave.sample_width)
	input_pcm = input_wave.pcm + input_silence_sample * (
		(target_samples - input_wave.frame_count) * input_wave.channels
	)
	clean_pcm = clean_wave.pcm + clean_silence_sample * (
		(target_samples - clean_wave.frame_count) * clean_wave.channels
	)
	input_output_bytes = write_pcm_wave(output_input_path, input_wave, input_pcm)
	clean_output_bytes = write_pcm_wave(output_clean_path, clean_wave, clean_pcm)
	attestation: Dict[str, Any] = {
		"schema_version": 1,
		"operation": "append-digital-silence-only",
		"frame_samples": FRAME_SAMPLES,
		"frames_per_packet": frames_per_packet,
		"alignment_samples": alignment_samples,
		"target_sample_count": target_samples,
		"input": fixture_record(input_path, output_input_path, input_wave, input_output_bytes, target_samples),
		"clean_reference": fixture_record(clean_path, output_clean_path, clean_wave, clean_output_bytes, target_samples),
	}
	attestation_path.parent.mkdir(parents=True, exist_ok=True)
	attestation_path.write_text(json.dumps(attestation, indent=2, sort_keys=True) + "\n", encoding="utf-8", newline="\n")
	return attestation


def run_self_test() -> None:
	with tempfile.TemporaryDirectory(prefix="mumble-fixed-timeline-padding-") as temporary:
		root = Path(temporary)
		input_path = root / "input.wav"
		clean_path = root / "clean.wav"
		for path, sample_count, value in ((input_path, 481, 17), (clean_path, 960, 29)):
			with wave.open(str(path), "wb") as destination:
				destination.setnchannels(1)
				destination.setsampwidth(2)
				destination.setframerate(SAMPLE_RATE_HZ)
				destination.writeframes(bytes((value, 0)) * sample_count)

		output_input = root / "padded-input.wav"
		output_clean = root / "padded-clean.wav"
		attestation_path = root / "attestation.json"
		result = pad_pair(input_path, clean_path, output_input, output_clean, attestation_path, 4)
		if result["target_sample_count"] != 1920 or result["alignment_samples"] != 1920:
			raise AssertionError("LCM-aligned sample count regression")
		padded_input = read_pcm_wave(output_input)
		padded_clean = read_pcm_wave(output_clean)
		if padded_input.pcm[: 481 * 2] != read_pcm_wave(input_path).pcm or any(padded_input.pcm[481 * 2 :]):
			raise AssertionError("input samples were changed instead of zero-padded")
		if padded_clean.pcm[: 960 * 2] != read_pcm_wave(clean_path).pcm or any(padded_clean.pcm[960 * 2 :]):
			raise AssertionError("clean-reference samples were changed instead of zero-padded")
		if json.loads(attestation_path.read_text(encoding="utf-8"))["input"]["zero_pad_sample_count"] != 1439:
			raise AssertionError("padding attestation regression")


def main(argv: Sequence[str] | None = None) -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--input", type=Path)
	parser.add_argument("--clean-reference", type=Path)
	parser.add_argument("--output-input", type=Path)
	parser.add_argument("--output-clean-reference", type=Path)
	parser.add_argument("--attestation", type=Path)
	parser.add_argument("--frames-per-packet", type=int, choices=SUPPORTED_FRAMES_PER_PACKET, default=4)
	parser.add_argument("--self-test", action="store_true")
	args = parser.parse_args(argv)
	try:
		if args.self_test:
			run_self_test()
			print("fixed-timeline WAV padding self-test: ok")
			return 0
		for name in ("input", "clean_reference", "output_input", "output_clean_reference", "attestation"):
			if getattr(args, name) is None:
				raise PaddingError(f"--{name.replace('_', '-')} is required")
		result = pad_pair(
			args.input,
			args.clean_reference,
			args.output_input,
			args.output_clean_reference,
			args.attestation,
			args.frames_per_packet,
		)
		print(
			f"fixed-timeline WAV padding: samples={result['target_sample_count']} "
			f"alignment={result['alignment_samples']} attestation={args.attestation}"
		)
		return 0
	except (AssertionError, PaddingError) as error:
		print(f"fixed-timeline WAV padding: error: {error}", file=sys.stderr)
		return 1


if __name__ == "__main__":
	raise SystemExit(main())
