#!/usr/bin/env python3
"""Run the dependency-free input-enhancement quality-tool self-tests."""

from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path


def main() -> int:
	root = Path(__file__).resolve().parent
	scripts = (
		"check-input-enhancement-boundary.py",
		"check-original-voice-contract.py",
		"detect-audio-quality-applicability.py",
		"validate-corpus-lock.py",
		"fetch-corpus.py",
		"corpus-inventory-v3.py",
		"build-corpus-inventory-v3.py",
		"generate-mixture-plan.py",
		"pad-fixed-timeline-wav.py",
		"render-mixture-plan.py",
		"freeze-rnnoise-training-plan.py",
		"select-rnnoise-model.py",
		"score-fixed-timeline.py",
		"score-objective-quality.py",
		"run-offline-quality-campaign.py",
		"run-two-client-e2e.py",
		"blind-listening.py",
		"measurement_evidence.py",
		"run-ci-quality-gate.py",
		"generate-quality-case-evidence.py",
		"validate-quality-qualification.py",
	)
	for script in scripts:
		result = subprocess.run([ sys.executable, str(root / script), "--self-test" ], check=False)
		if result.returncode != 0:
			print(f"audio-quality self-tests: {script} failed with {result.returncode}", file=sys.stderr)
			return result.returncode
	try:
		for schema_name in (
			"quality-qualification.schema.json",
			"quality-case-evidence.schema.json",
			"objective-quality-score.schema.json",
			"measurement-index.schema.json",
			"release-holdout-opening.schema.json",
			"release-holdout-opening-report.schema.json",
			"release-holdout-receipt.schema.json",
			"corpus-inventory-v3.schema.json",
			"blind-listening-source.schema.json",
			"blind-listening-session.schema.json",
			"blind-listening-qualification.schema.json",
		):
			json.loads((root / schema_name).read_text(encoding="utf-8"))
	except (OSError, json.JSONDecodeError) as error:
		print(f"audio-quality self-tests: invalid qualification schema: {error}", file=sys.stderr)
		return 1
	print("audio-quality self-tests: ok")
	return 0


if __name__ == "__main__":
	sys.exit(main())
