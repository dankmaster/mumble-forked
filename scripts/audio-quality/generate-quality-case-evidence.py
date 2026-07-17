#!/usr/bin/env python3
"""Write canonical, qualification-bound per-case quality evidence as JSONL."""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import sys
import tempfile
from pathlib import Path
from typing import Any, Mapping, Sequence

from objective_quality_score import _sample_score

from quality_case_evidence import (
	CaseEvidenceError,
	load_case_evidence,
	summarize_case_evidence,
	validate_records,
	verify_objective_score_references,
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


def generate(qualification_value: Any, records_value: Any, output: Path, artifact_root: Path) -> Mapping[str, Any]:
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
	if records["schema_version"] != 3:
		raise GeneratorError("records.schema_version: unsupported version")
	if not isinstance(records["cases"], list) or not isinstance(records["auto_transitions"], list):
		raise GeneratorError("records cases and auto_transitions must be arrays")
	validated_cases, _ = validate_records(records["cases"], records["auto_transitions"], scope)
	verify_objective_score_references(validated_cases, artifact_root)
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
		"recomputed": summarize_case_evidence(cases, transitions, scope, suite),
	}


def _sample_case(profile: str, index: int, condition: str) -> Mapping[str, Any]:
	return {
		"record_type": "case",
		"case_id": f"case-{index:03d}",
		"profile": profile,
		"condition": condition,
		"dataset_split": "pr-smoke",
		"cohort_id": f"{condition}-cohort",
		"speaker_group_id": f"speaker-{index:03d}",
		"noise_group_id": None if condition == "clean" else f"noise-{index:03d}",
		"noise_class": None if condition == "clean" else f"noise-class-{index:03d}",
		"rir_group_id": f"room-{index:03d}",
		"device_group_id": f"device-{index:03d}",
		"language": "en-US" if index % 2 == 0 else "sv-SE",
		"startup_preroll_ms": 0 if index % 2 == 0 else 300,
		"fixed_timeline": True,
		"receiver_cleanup_enabled": False,
		"failed": False,
		"quality_pair_case_id": f"case-{index:03d}" if profile == "VoiceFocus" and condition == "severe" else None,
		"objective_score": {
			"path": f"objective-scores/{profile}/{index:03d}.json",
			"sha256": "0" * 64,
			"signal_stage": "sender-pre-opus",
			"size_bytes": 1,
			"wer_reference_kind": "clean-asr-consistency",
			"wer_reference_text_sha256": "1" * 64,
		},
		"metrics": {
			"algorithmic_latency_ms": { "Original": 0.0, "Light": 10.0, "Balanced": 30.0, "Quality": 50.0, "VoiceFocus": 50.0 }[profile],
			"dnsmos_bak_improvement": 0.1 if profile == "VoiceFocus" and condition == "severe" else 0.0,
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
			"worker_durations_ms": [ 0.1 ],
			"max_internal_processing_ms": 0.1,
			"memory_growth_bytes": 0,
			"soak_duration_seconds": 0,
		},
}


def _materialize_sample_score(root: Path, case: Mapping[str, Any]) -> None:
	reference_hash = case["objective_score"]["wer_reference_text_sha256"]
	document = copy.deepcopy(_sample_score())
	document.update({
		"case_id": case["case_id"],
		"profile": case["profile"],
		"condition": case["condition"],
		"dataset_split": case["dataset_split"],
	})
	latency_samples = int(round(float(case["metrics"]["algorithmic_latency_ms"]) * 48.0))
	document["alignment"].update({"candidate_latency_samples": latency_samples, "candidate_window_start_samples": latency_samples})
	document["wer_reference"]["text_sha256"] = reference_hash
	document["wer_reference"]["language"] = str(case["language"]).split("-", 1)[0].casefold()
	if case["profile"] == "VoiceFocus" and case["condition"] == "severe":
		document["metrics"]["candidate"]["dnsmos_bak"] += 0.1
		document["candidate_minus_original"]["dnsmos_bak"] = 0.1
	path = root.joinpath(*Path(case["objective_score"]["path"]).parts)
	path.parent.mkdir(parents=True, exist_ok=True)
	payload = (json.dumps(document, sort_keys=True, separators=(",", ":")) + "\n").encode("utf-8")
	path.write_bytes(payload)
	case["objective_score"]["sha256"] = hashlib.sha256(payload).hexdigest()
	case["objective_score"]["size_bytes"] = len(payload)


def run_self_test() -> None:
	qualification = {
		"schema_version": 3,
		"qualification_scope": "core",
		"suite": "pr_smoke",
		"build": { "git_sha": "1" * 40, "tested_binary_sha256": "2" * 64 },
	}
	records = {
		"schema_version": 3,
		"cases": [
			_sample_case(profile, index, condition)
			for profile in ("Original", "Light", "Balanced", "Quality", "VoiceFocus")
			for index, condition in enumerate(("clean", "clean", "noisy", "noisy", "severe", "severe"))
		],
		"auto_transitions": [],
	}
	with tempfile.TemporaryDirectory() as directory:
		root = Path(directory)
		for case in records["cases"]:
			_materialize_sample_score(root, case)
		output = root / "case-evidence.jsonl"
		metadata = generate(qualification, records, output, root)
		if metadata["artifact"]["sha256"] != hashlib.sha256(output.read_bytes()).hexdigest():
			raise AssertionError("generated metadata did not hash the canonical evidence")
		tampered_records = copy.deepcopy(records)
		tampered_voice_focus = next(
			case for case in tampered_records["cases"]
			if case["profile"] == "VoiceFocus" and case["condition"] == "severe"
		)
		tampered_voice_focus["metrics"]["severe_noise_bak_improvement_over_quality"] = 0.2
		try:
			generate(qualification, tampered_records, root / "tampered-case-evidence.jsonl", root)
		except CaseEvidenceError:
			pass
		else:
			raise AssertionError("self-reported VoiceFocus-over-Quality BAK was accepted without paired objective evidence")
		try:
			generate(qualification, records, output, root)
		except CaseEvidenceError:
			pass
		else:
			raise AssertionError("generator replaced an existing evidence file")


def main(argv: Sequence[str] | None = None) -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--qualification", type=Path)
	parser.add_argument("--records", type=Path)
	parser.add_argument("--output", type=Path)
	parser.add_argument("--artifact-root", type=Path)
	parser.add_argument("--self-test", action="store_true")
	args = parser.parse_args(argv)
	try:
		if args.self_test:
			run_self_test()
			print("quality case-evidence generator self-test: ok")
			if args.qualification is None and args.records is None and args.output is None and args.artifact_root is None:
				return 0
		if args.qualification is None or args.records is None or args.output is None or args.artifact_root is None:
			raise GeneratorError("--qualification, --records, --output, and --artifact-root are required")
		metadata = generate(_load_json(args.qualification), _load_json(args.records), args.output, args.artifact_root)
		print(json.dumps(metadata, sort_keys=True, separators=(",", ":")))
		return 0
	except (CaseEvidenceError, GeneratorError, AssertionError) as error:
		print(f"quality case-evidence generator: error: {error}", file=sys.stderr)
		return 1


if __name__ == "__main__":
	sys.exit(main())
