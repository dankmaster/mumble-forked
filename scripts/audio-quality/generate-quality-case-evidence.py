#!/usr/bin/env python3
"""Write canonical, qualification-bound per-case quality evidence as JSONL."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
import tempfile
from pathlib import Path
from typing import Any, Mapping, Sequence

from quality_case_evidence import (
	CaseEvidenceError,
	load_case_evidence,
	summarize_case_evidence,
	write_case_evidence,
)


class GeneratorError(ValueError):
	"""Raised when the generator input cannot produce canonical evidence."""


def _load_json(path: Path) -> Any:
	try:
		return json.loads(path.read_text(encoding="utf-8"))
	except (OSError, json.JSONDecodeError) as error:
		raise GeneratorError(f"unable to read {path}: {error}") from error


def _mapping(value: Any, path: str) -> Mapping[str, Any]:
	if not isinstance(value, dict):
		raise GeneratorError(f"{path}: expected an object")
	return value


def generate(qualification_value: Any, records_value: Any, output: Path) -> Mapping[str, Any]:
	qualification = _mapping(qualification_value, "qualification")
	if qualification.get("schema_version") != 3:
		raise GeneratorError("qualification.schema_version: canonical case evidence requires schema v3")
	scope = qualification.get("qualification_scope")
	suite = qualification.get("suite")
	build = _mapping(qualification.get("build"), "qualification.build")
	if not isinstance(scope, str) or not isinstance(suite, str):
		raise GeneratorError("qualification scope and suite are required")
	records = _mapping(records_value, "records")
	if set(records) != { "auto_transitions", "cases", "schema_version" }:
		raise GeneratorError("records must contain exactly schema_version, cases, and auto_transitions")
	if records["schema_version"] != 1:
		raise GeneratorError("records.schema_version: unsupported version")
	if not isinstance(records["cases"], list) or not isinstance(records["auto_transitions"], list):
		raise GeneratorError("records cases and auto_transitions must be arrays")
	write_case_evidence(output, build, scope, suite, records["cases"], records["auto_transitions"])
	cases, transitions = load_case_evidence(output, build, scope, suite)
	payload = output.read_bytes()
	return {
		"artifact": {
			"path": output.as_posix(),
			"sha256": hashlib.sha256(payload).hexdigest(),
			"size_bytes": len(payload),
			"contains_audio_samples": False,
		},
		"recomputed": summarize_case_evidence(cases, transitions, scope),
	}


def _sample_case(profile: str, index: int, condition: str) -> Mapping[str, Any]:
	return {
		"record_type": "case",
		"case_id": f"case-{index:03d}",
		"profile": profile,
		"condition": condition,
		"cohort_id": f"{condition}-cohort",
		"language": "en-US" if index % 2 == 0 else "sv-SE",
		"startup_preroll_ms": 0 if index % 2 == 0 else 300,
		"fixed_timeline": True,
		"receiver_cleanup_enabled": False,
		"failed": False,
		"metrics": {
			"algorithmic_latency_ms": { "Original": 0.0, "Light": 10.0, "Balanced": 30.0, "Quality": 50.0, "VoiceFocus": 50.0 }[profile],
			"dnsmos_bak_improvement": 0.0,
			"dnsmos_ovrl_improvement": 0.0,
			"dnsmos_sig_loss": 0.0,
			"estoi_loss": 0.0,
			"severe_noise_bak_improvement_over_quality": 0.1 if profile == "VoiceFocus" and condition == "severe" else 0.0,
			"speech_edge_loss_ms": 0.0,
			"wer_loss_percentage_points": 0.0,
		},
		"counters": {
			"deadline_misses": 0,
			"latency_attestation_failures": 0,
			"model_hash_errors": 0,
			"nan_or_inf_count": 0,
			"new_clipping_cases": 0,
			"tail_drain_failures": 0,
			"unexplained_fallbacks": 0,
		},
		"performance": {
			"audio_duration_seconds": 1.0,
			"processing_duration_seconds": 0.01,
			"callback_durations_ms": [ 0.1 ],
			"max_internal_processing_ms": 0.1,
			"memory_growth_bytes": 0,
			"soak_duration_seconds": 0,
		},
	}


def run_self_test() -> None:
	qualification = {
		"schema_version": 3,
		"qualification_scope": "core",
		"suite": "pr_smoke",
		"build": { "git_sha": "1" * 40, "tested_binary_sha256": "2" * 64 },
	}
	records = {
		"schema_version": 1,
		"cases": [
			_sample_case(profile, index, condition)
			for profile in ("Original", "Light", "Balanced", "Quality", "VoiceFocus")
			for index, condition in enumerate(("clean", "noisy", "severe"))
		],
		"auto_transitions": [],
	}
	with tempfile.TemporaryDirectory() as directory:
		output = Path(directory) / "case-evidence.jsonl"
		metadata = generate(qualification, records, output)
		if metadata["artifact"]["sha256"] != hashlib.sha256(output.read_bytes()).hexdigest():
			raise AssertionError("generated metadata did not hash the canonical evidence")
		try:
			generate(qualification, records, output)
		except CaseEvidenceError:
			pass
		else:
			raise AssertionError("generator replaced an existing evidence file")


def main(argv: Sequence[str] | None = None) -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--qualification", type=Path)
	parser.add_argument("--records", type=Path)
	parser.add_argument("--output", type=Path)
	parser.add_argument("--self-test", action="store_true")
	args = parser.parse_args(argv)
	try:
		if args.self_test:
			run_self_test()
			print("quality case-evidence generator self-test: ok")
			if args.qualification is None and args.records is None and args.output is None:
				return 0
		if args.qualification is None or args.records is None or args.output is None:
			raise GeneratorError("--qualification, --records, and --output are required")
		metadata = generate(_load_json(args.qualification), _load_json(args.records), args.output)
		print(json.dumps(metadata, sort_keys=True, separators=(",", ":")))
		return 0
	except (CaseEvidenceError, GeneratorError, AssertionError) as error:
		print(f"quality case-evidence generator: error: {error}", file=sys.stderr)
		return 1


if __name__ == "__main__":
	sys.exit(main())
