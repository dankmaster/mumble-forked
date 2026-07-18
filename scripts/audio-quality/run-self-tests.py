#!/usr/bin/env python3
"""Run the dependency-free input-enhancement quality-tool self-tests."""

from __future__ import annotations

import hashlib
import importlib.util
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

import payload_identity


def _load_script(path: Path, module_name: str):
	spec = importlib.util.spec_from_file_location(module_name, path)
	if spec is None or spec.loader is None:
		raise RuntimeError(f"unable to load self-test module: {path}")
	module = importlib.util.module_from_spec(spec)
	sys.modules[module_name] = module
	spec.loader.exec_module(module)
	return module


def _payload_identity_regression(root: Path) -> None:
	gate = _load_script(root / "run-ci-quality-gate.py", "mumble_gate_payload_identity_self_test")
	e2e = _load_script(root / "run-two-client-e2e.py", "mumble_e2e_payload_identity_self_test")
	with tempfile.TemporaryDirectory(prefix="mumble-payload-identity-") as directory:
		payload = Path(directory) / "payload"
		(payload / "nested").mkdir(parents=True)
		(payload / "Z-runtime.dll").write_bytes(b"runtime-z")
		(payload / "a-model.bin").write_bytes(b"model-a")
		(payload / "nested" / "recipe.json").write_bytes(b"{}\n")
		expected_records = [
			{
				"path": path.relative_to(payload).as_posix(),
				"sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
				"size_bytes": path.stat().st_size,
			}
			for path in sorted((item for item in payload.rglob("*") if item.is_file()), key=lambda item: item.relative_to(payload).as_posix())
		]
		expected = payload_identity.canonical_json_sha256(expected_records)
		if gate.payload_sha256(payload) != expected:
			raise AssertionError("CI gate payload identity differs from the canonical path/sha256/size contract")
		e2e_attestation = e2e._tree_attestation(payload)
		if e2e_attestation["sha256"] != expected or e2e_attestation["file_count"] != len(expected_records):
			raise AssertionError("two-client E2E payload identity differs from the CI gate contract")
		if list(payload_identity.payload_tree_records(payload)) != expected_records:
			raise AssertionError("shared payload inventory has the wrong fields or case-sensitive order")
		try:
			payload_identity.canonical_json_sha256({"invalid": float("nan")})
		except payload_identity.PayloadIdentityError:
			pass
		else:
			raise AssertionError("canonical payload JSON accepted a non-finite number")

		hardlink = payload / "runtime-hardlink.dll"
		try:
			os.link(payload / "Z-runtime.dll", hardlink)
		except OSError:
			pass
		else:
			try:
				payload_identity.payload_tree_records(payload)
			except payload_identity.PayloadIdentityError:
				pass
			else:
				raise AssertionError("payload identity accepted a hardlink alias")
			hardlink.unlink()

		# This may require Windows Developer Mode/admin. When the OS permits the
		# symlink, the protected walker must reject it before traversal.
		link = payload / "linked-runtime"
		try:
			link.symlink_to(payload / "nested", target_is_directory=True)
		except OSError:
			pass
		else:
			try:
				payload_identity.payload_tree_records(payload)
			except payload_identity.PayloadIdentityError:
				pass
			else:
				raise AssertionError("payload identity followed a symlink/reparse point")


def main() -> int:
	root = Path(__file__).resolve().parent
	try:
		_payload_identity_regression(root)
	except (AssertionError, OSError, RuntimeError, payload_identity.PayloadIdentityError) as error:
		print(f"audio-quality self-tests: payload identity regression failed: {error}", file=sys.stderr)
		return 1
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
		"tune-input-enhancement-recipes.py",
		"run-two-client-e2e.py",
		"write-quality-parquet.py",
		"run-e2e-quality-campaign.py",
		"candidate_build_receipt.py",
		"assemble-original-voice-qualification.py",
		"run-original-voice-campaign.py",
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
			"candidate-build-invocation.schema.json",
		):
			json.loads((root / schema_name).read_text(encoding="utf-8"))
	except (OSError, json.JSONDecodeError) as error:
		print(f"audio-quality self-tests: invalid qualification schema: {error}", file=sys.stderr)
		return 1
	print("audio-quality self-tests: ok")
	return 0


if __name__ == "__main__":
	sys.exit(main())
