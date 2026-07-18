#!/usr/bin/env python3
"""Score fixed-timeline input-enhancement WAVs with pinned objective metrics."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Sequence

from objective_quality_score import (
	ALLOWED_CONDITIONS,
	ALLOWED_DATASET_SPLITS,
	ALLOWED_LANGUAGES,
	ALLOWED_PROFILES,
	ObjectiveScoreError,
	SIGNAL_STAGES,
	run_cli,
)


def main(argv: Sequence[str] | None = None) -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--case-id")
	parser.add_argument("--profile", choices=ALLOWED_PROFILES)
	parser.add_argument("--condition", choices=ALLOWED_CONDITIONS)
	parser.add_argument("--dataset-split", choices=ALLOWED_DATASET_SPLITS)
	parser.add_argument("--signal-stage", choices=SIGNAL_STAGES)
	parser.add_argument("--clean-reference", type=Path)
	parser.add_argument("--noisy-original", type=Path)
	parser.add_argument("--candidate", type=Path)
	parser.add_argument("--original-latency-samples", type=int)
	parser.add_argument("--candidate-latency-samples", type=int)
	parser.add_argument("--route-control-wav", type=Path)
	parser.add_argument("--route-control-score", type=Path)
	parser.add_argument("--candidate-fixed-timeline-score", type=Path)
	parser.add_argument("--route-e2e-manifest", type=Path)
	parser.add_argument("--metrics-runtime-root", type=Path)
	parser.add_argument("--metrics-manifest", type=Path)
	parser.add_argument("--language", choices=ALLOWED_LANGUAGES)
	parser.add_argument("--wer-reference-kind", choices=("clean-asr-consistency", "segment-ground-truth"))
	parser.add_argument("--clean-asr-reference", type=Path)
	parser.add_argument("--segment-transcript", type=Path)
	parser.add_argument("--segment-transcript-attestation", type=Path)
	parser.add_argument("--release-holdout-opening-attestation", type=Path)
	parser.add_argument("--release-holdout-opening-signature", type=Path)
	parser.add_argument("--release-holdout-approval-public-key", type=Path)
	parser.add_argument("--release-holdout-opening-root", type=Path)
	parser.add_argument("--release-build", type=Path)
	parser.add_argument("--expected-holdout-opening-sha256")
	parser.add_argument("--expected-holdout-plan-sha256")
	parser.add_argument("--expected-holdout-inventory-sha256")
	parser.add_argument("--expected-release-build-sha256")
	parser.add_argument("--expected-release-holdout-approval-public-key-sha256")
	parser.add_argument("--output", type=Path)
	parser.add_argument("--self-test", action="store_true")
	args = parser.parse_args(argv)
	args.scorer_cli = Path(__file__)
	try:
		if not args.self_test:
			required = (
				"case_id", "profile", "condition", "dataset_split", "signal_stage", "clean_reference", "noisy_original", "candidate",
				"original_latency_samples", "candidate_latency_samples", "metrics_runtime_root",
				"metrics_manifest", "language", "wer_reference_kind", "output",
			)
			missing = [f"--{name.replace('_', '-')}" for name in required if getattr(args, name) is None]
			if missing:
				raise ObjectiveScoreError(f"missing required arguments: {', '.join(missing)}")
		result = run_cli(args)
		print(json.dumps(result, sort_keys=True, separators=(",", ":")))
		return 0
	except (AssertionError, ObjectiveScoreError, OSError) as error:
		print(f"objective quality scorer: error: {error}", file=sys.stderr)
		return 1


if __name__ == "__main__":
	raise SystemExit(main())
