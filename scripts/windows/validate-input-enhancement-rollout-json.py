#!/usr/bin/env python3
"""Strict raw-JSON validation for input-enhancement rollout evidence.

PowerShell's JSON conversion deliberately performs useful coercions (including
ISO timestamp conversion). Release evidence must not rely on those coercions:
this validator checks the serialized JSON types and exact object shapes before
the PowerShell semantic and cryptographic verifiers consume the document.
"""

from __future__ import annotations

import argparse
import json
import math
import re
import sys
from datetime import datetime
from pathlib import Path
from typing import Any, Iterable


class ValidationError(ValueError):
	"""Raised when signed evidence does not match its strict raw contract."""


SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
BUILD_ID_RE = re.compile(r"^mumble-forked-build-[1-9][0-9]*-[0-9a-f]{12}$")
IDENTIFIER_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$")
UTC_RE = re.compile(r"^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z$")
REASON_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$")


def _fail(context: str, message: str) -> None:
	raise ValidationError(f"{context}: {message}")


def _object_pairs(pairs: Iterable[tuple[str, Any]]) -> dict[str, Any]:
	result: dict[str, Any] = {}
	for key, value in pairs:
		if key in result:
			raise ValidationError(f"duplicate JSON property {key!r}")
		result[key] = value
	return result


def _load(path: Path) -> Any:
	try:
		raw = path.read_bytes()
	except OSError as error:
		raise ValidationError(f"unable to read {path}: {error}") from error
	if raw.startswith(b"\xef\xbb\xbf"):
		_fail(str(path), "UTF-8 BOM is forbidden")
	try:
		text = raw.decode("utf-8", errors="strict")
	except UnicodeDecodeError as error:
		raise ValidationError(f"{path}: invalid UTF-8: {error}") from error
	try:
		return json.loads(
			text,
			object_pairs_hook=_object_pairs,
			parse_constant=lambda token: _fail(str(path), f"non-finite JSON number {token!r}"),
		)
	except (json.JSONDecodeError, ValidationError) as error:
		raise ValidationError(f"{path}: invalid strict JSON: {error}") from error


def _object(value: Any, expected: set[str], context: str) -> dict[str, Any]:
	if not isinstance(value, dict):
		_fail(context, "must be an object")
	actual = set(value)
	if actual != expected:
		missing = sorted(expected - actual)
		extra = sorted(actual - expected)
		_fail(context, f"property mismatch; missing={missing}, extra={extra}")
	return value


def _string(value: Any, context: str) -> str:
	if not isinstance(value, str):
		_fail(context, "must be a JSON string")
	return value


def _integer(value: Any, context: str, minimum: int = 0) -> int:
	if isinstance(value, bool) or not isinstance(value, int):
		_fail(context, "must be a JSON integer")
	if value < minimum:
		_fail(context, f"must be >= {minimum}")
	return value


def _number(value: Any, context: str, minimum: float = 0.0, maximum: float | None = None) -> float:
	if isinstance(value, bool) or not isinstance(value, (int, float)):
		_fail(context, "must be a JSON number")
	number = float(value)
	if not math.isfinite(number) or number < minimum or (maximum is not None and number > maximum):
		_fail(context, f"must be finite and in [{minimum}, {maximum if maximum is not None else 'inf'}]")
	return number


def _boolean(value: Any, expected: bool, context: str) -> None:
	if type(value) is not bool or value is not expected:
		_fail(context, f"must be the JSON boolean {str(expected).lower()}")


def _sha256(value: Any, context: str) -> str:
	text = _string(value, context)
	if not SHA256_RE.fullmatch(text):
		_fail(context, "must be a lowercase SHA-256")
	return text


def _identifier(value: Any, context: str) -> str:
	text = _string(value, context)
	if not IDENTIFIER_RE.fullmatch(text):
		_fail(context, "must be a safe identifier")
	return text


def _utc(value: Any, context: str) -> str:
	text = _string(value, context)
	if not UTC_RE.fullmatch(text):
		_fail(context, "must use canonical UTC seconds (yyyy-MM-ddTHH:mm:ssZ)")
	try:
		datetime.strptime(text, "%Y-%m-%dT%H:%M:%SZ")
	except ValueError as error:
		raise ValidationError(f"{context}: invalid UTC timestamp: {error}") from error
	return text


def validate_aggregate(value: Any) -> None:
	root = _object(value, {
		"generatedAtUtc", "kind", "population", "preference", "privacy", "query",
		"recipeSetVersion", "reliability", "rolloutAudience", "schemaVersion",
		"sourceChannel", "testedBuildIds", "window",
	}, "aggregate")
	if type(root["schemaVersion"]) is not int or root["schemaVersion"] != 2:
		_fail("aggregate.schemaVersion", "must be the JSON integer 2")
	if _string(root["kind"], "aggregate.kind") != "input-enhancement-telemetry-aggregate-export":
		_fail("aggregate.kind", "unsupported kind")
	_utc(root["generatedAtUtc"], "aggregate.generatedAtUtc")
	if _string(root["sourceChannel"], "aggregate.sourceChannel") not in {"preview", "stable"}:
		_fail("aggregate.sourceChannel", "unsupported channel")
	if _string(root["rolloutAudience"], "aggregate.rolloutAudience") not in {"private-community", "public"}:
		_fail("aggregate.rolloutAudience", "unsupported audience")
	_identifier(root["recipeSetVersion"], "aggregate.recipeSetVersion")
	builds = root["testedBuildIds"]
	if not isinstance(builds, list):
		_fail("aggregate.testedBuildIds", "must be a JSON array")
	if len(builds) != 1 or not isinstance(builds[0], str) or not BUILD_ID_RE.fullmatch(builds[0]):
		_fail("aggregate.testedBuildIds", "must contain exactly one immutable build ID")

	window = _object(root["window"], {"endUtc", "observationDays", "startUtc"}, "aggregate.window")
	_utc(window["startUtc"], "aggregate.window.startUtc")
	_utc(window["endUtc"], "aggregate.window.endUtc")
	_integer(window["observationDays"], "aggregate.window.observationDays", 1)

	query = _object(root["query"], {
		"id", "sha256", "sourceEventCount", "sourceSnapshotSha256", "windowSha256",
	}, "aggregate.query")
	if _string(query["id"], "aggregate.query.id") != "input-enhancement-rollout-v2":
		_fail("aggregate.query.id", "unsupported query contract")
	_sha256(query["sha256"], "aggregate.query.sha256")
	_sha256(query["sourceSnapshotSha256"], "aggregate.query.sourceSnapshotSha256")
	_sha256(query["windowSha256"], "aggregate.query.windowSha256")
	_integer(query["sourceEventCount"], "aggregate.query.sourceEventCount")

	population = _object(root["population"], {
		"distinctDevices", "distinctUsers", "intendedCommunityDevices", "talkHours",
	}, "aggregate.population")
	_integer(population["distinctUsers"], "aggregate.population.distinctUsers")
	_integer(population["distinctDevices"], "aggregate.population.distinctDevices")
	_integer(population["intendedCommunityDevices"], "aggregate.population.intendedCommunityDevices", 1)
	_number(population["talkHours"], "aggregate.population.talkHours")

	reliability = _object(root["reliability"], {
		"callbackOverrunFrameRate", "crashFreeSessionRate", "fallbackSessionRate",
		"manualRollbackOrOptOutRate", "modelHashMismatchCount", "p0Count", "p1Count",
		"recurrentCallbackRegressionCount",
	}, "aggregate.reliability")
	for name in ("p0Count", "p1Count", "modelHashMismatchCount", "recurrentCallbackRegressionCount"):
		_integer(reliability[name], f"aggregate.reliability.{name}")
	for name in ("crashFreeSessionRate", "fallbackSessionRate", "callbackOverrunFrameRate", "manualRollbackOrOptOutRate"):
		_number(reliability[name], f"aggregate.reliability.{name}", 0.0, 1.0)

	preference = _object(root["preference"], {"blindAbResponses", "selectedOverOriginalRate"}, "aggregate.preference")
	_integer(preference["blindAbResponses"], "aggregate.preference.blindAbResponses")
	_number(preference["selectedOverOriginalRate"], "aggregate.preference.selectedOverOriginalRate", 0.0, 1.0)

	privacy = _object(root["privacy"], {
		"optInOnly", "rawAudioIncluded", "rawDeviceIdsIncluded", "retentionDays",
		"transcriptsIncluded", "voiceprintsIncluded",
	}, "aggregate.privacy")
	_boolean(privacy["optInOnly"], True, "aggregate.privacy.optInOnly")
	for name in ("rawAudioIncluded", "rawDeviceIdsIncluded", "transcriptsIncluded", "voiceprintsIncluded"):
		_boolean(privacy[name], False, f"aggregate.privacy.{name}")
	retention = _integer(privacy["retentionDays"], "aggregate.privacy.retentionDays", 1)
	if retention > 30:
		_fail("aggregate.privacy.retentionDays", "must be <= 30")


def _validate_file_reference(value: Any, context: str, file_name: str) -> None:
	reference = _object(value, {
		"fileName", "sha256", "signatureFileName", "signatureSha256",
	}, context)
	if _string(reference["fileName"], f"{context}.fileName") != file_name:
		_fail(f"{context}.fileName", "unexpected stable filename")
	if _string(reference["signatureFileName"], f"{context}.signatureFileName") != f"{file_name}.sig":
		_fail(f"{context}.signatureFileName", "unexpected stable signature filename")
	_sha256(reference["sha256"], f"{context}.sha256")
	_sha256(reference["signatureSha256"], f"{context}.signatureSha256")


def validate_rollout(value: Any) -> None:
	root = _object(value, {
		"domainRnnoiseTrack", "generatedAtUtc", "kind", "schemaVersion", "sourceAggregate",
	}, "rollout")
	if type(root["schemaVersion"]) is not int or root["schemaVersion"] != 2:
		_fail("rollout.schemaVersion", "must be the JSON integer 2")
	if _string(root["kind"], "rollout.kind") != "input-enhancement-rollout-qualification":
		_fail("rollout.kind", "unsupported kind")
	_utc(root["generatedAtUtc"], "rollout.generatedAtUtc")
	source = _object(root["sourceAggregate"], {
		"fileName", "querySha256", "sha256", "signatureFileName", "signatureSha256", "windowSha256",
	}, "rollout.sourceAggregate")
	if _string(source["fileName"], "rollout.sourceAggregate.fileName") != "input-enhancement-aggregate-export.json":
		_fail("rollout.sourceAggregate.fileName", "unexpected stable filename")
	if _string(source["signatureFileName"], "rollout.sourceAggregate.signatureFileName") != "input-enhancement-aggregate-export.json.sig":
		_fail("rollout.sourceAggregate.signatureFileName", "unexpected stable signature filename")
	for name in ("querySha256", "sha256", "signatureSha256", "windowSha256"):
		_sha256(source[name], f"rollout.sourceAggregate.{name}")

	track = root["domainRnnoiseTrack"]
	if not isinstance(track, dict):
		_fail("rollout.domainRnnoiseTrack", "must be an object")
	status = track.get("status")
	if status == "pending":
		_object(track, {"status"}, "rollout.domainRnnoiseTrack")
		return
	if status != "completed":
		_fail("rollout.domainRnnoiseTrack.status", "must be pending or completed")
	completed = _object(track, {"decision", "outcome", "status"}, "rollout.domainRnnoiseTrack")
	if _string(completed["outcome"], "rollout.domainRnnoiseTrack.outcome") not in {"embedded-retained", "custom-promoted"}:
		_fail("rollout.domainRnnoiseTrack.outcome", "unsupported mapped outcome")
	_validate_file_reference(
		completed["decision"],
		"rollout.domainRnnoiseTrack.decision",
		"rnnoise-selection-decision.json",
	)


def _safe_relative_path(value: Any, context: str) -> str:
	path = _string(value, context)
	if not path or path.startswith(("/", "\\")) or "\\" in path:
		_fail(context, "must be a non-empty POSIX relative path")
	parts = path.split("/")
	if any(part in {"", ".", ".."} for part in parts):
		_fail(context, "contains an unsafe path segment")
	return path


def validate_rnnoise_decision(value: Any) -> None:
	root = _object(value, {
		"bootstrap", "custom_model", "embedded_reference", "holdout_mixture_plan_sha256",
		"holdout_results_sha256", "one_shot_receipt_sha256", "reason_codes", "schema_version",
		"status", "training_plan_sha256", "validation_selection_sha256",
	}, "rnnoiseDecision")
	if type(root["schema_version"]) is not int or root["schema_version"] != 1:
		_fail("rnnoiseDecision.schema_version", "must be the JSON integer 1")
	for name in (
		"holdout_mixture_plan_sha256", "holdout_results_sha256", "one_shot_receipt_sha256",
		"training_plan_sha256", "validation_selection_sha256",
	):
		_sha256(root[name], f"rnnoiseDecision.{name}")
	status = _string(root["status"], "rnnoiseDecision.status")
	if status not in {"embedded-retained", "custom-selected"}:
		_fail("rnnoiseDecision.status", "unsupported selection decision")
	reasons = root["reason_codes"]
	if not isinstance(reasons, list) or any(not isinstance(reason, str) or not REASON_RE.fullmatch(reason) for reason in reasons):
		_fail("rnnoiseDecision.reason_codes", "must be an array of safe strings")
	if reasons != sorted(set(reasons)):
		_fail("rnnoiseDecision.reason_codes", "must be sorted and unique")

	bootstrap = _object(root["bootstrap"], {
		"confidence", "iterations", "lower_bound", "median_improvement", "metric", "sampler", "seed_sha256",
	}, "rnnoiseDecision.bootstrap")
	if _number(bootstrap["confidence"], "rnnoiseDecision.bootstrap.confidence", 0.0, 1.0) != 0.95:
		_fail("rnnoiseDecision.bootstrap.confidence", "must be 0.95")
	if _integer(bootstrap["iterations"], "rnnoiseDecision.bootstrap.iterations", 1) != 20_000:
		_fail("rnnoiseDecision.bootstrap.iterations", "must be 20000")
	lower_bound = _number(bootstrap["lower_bound"], "rnnoiseDecision.bootstrap.lower_bound", -10.0, 10.0)
	_number(bootstrap["median_improvement"], "rnnoiseDecision.bootstrap.median_improvement", -10.0, 10.0)
	if _string(bootstrap["metric"], "rnnoiseDecision.bootstrap.metric") != "paired per-case OVRL improvement, median bootstrap":
		_fail("rnnoiseDecision.bootstrap.metric", "unexpected metric")
	if _string(bootstrap["sampler"], "rnnoiseDecision.bootstrap.sampler") != "splitmix64-v1":
		_fail("rnnoiseDecision.bootstrap.sampler", "unexpected sampler")
	_sha256(bootstrap["seed_sha256"], "rnnoiseDecision.bootstrap.seed_sha256")

	embedded = _object(root["embedded_reference"], {"license_spdx", "model_id", "sha256", "size_bytes"}, "rnnoiseDecision.embedded_reference")
	if _string(embedded["model_id"], "rnnoiseDecision.embedded_reference.model_id") != "rnnoise:embedded":
		_fail("rnnoiseDecision.embedded_reference.model_id", "must name rnnoise:embedded")
	if not _string(embedded["license_spdx"], "rnnoiseDecision.embedded_reference.license_spdx").strip():
		_fail("rnnoiseDecision.embedded_reference.license_spdx", "must be non-empty")
	_sha256(embedded["sha256"], "rnnoiseDecision.embedded_reference.sha256")
	_integer(embedded["size_bytes"], "rnnoiseDecision.embedded_reference.size_bytes", 1)

	if status == "embedded-retained":
		if root["custom_model"] is not None or not reasons:
			_fail("rnnoiseDecision", "embedded-retained requires null custom_model and at least one reason")
		return
	if reasons or lower_bound <= 0.0:
		_fail("rnnoiseDecision", "custom-selected requires no rejection reasons and a positive bootstrap lower bound")
	custom = _object(root["custom_model"], {
		"candidate_id", "manifest_relative_path", "model_id", "sha256", "size_bytes",
	}, "rnnoiseDecision.custom_model")
	_identifier(custom["candidate_id"], "rnnoiseDecision.custom_model.candidate_id")
	_safe_relative_path(custom["manifest_relative_path"], "rnnoiseDecision.custom_model.manifest_relative_path")
	model_id = _string(custom["model_id"], "rnnoiseDecision.custom_model.model_id")
	if not model_id.startswith("rnnoise:custom:") or len(model_id) > 192:
		_fail("rnnoiseDecision.custom_model.model_id", "must be a bounded rnnoise:custom model ID")
	_sha256(custom["sha256"], "rnnoiseDecision.custom_model.sha256")
	_integer(custom["size_bytes"], "rnnoiseDecision.custom_model.size_bytes", 1)


def main() -> int:
	parser = argparse.ArgumentParser()
	parser.add_argument("--kind", choices=("aggregate", "rollout", "rnnoise-decision"), required=True)
	parser.add_argument("--path", type=Path, required=True)
	args = parser.parse_args()
	try:
		value = _load(args.path)
		{
			"aggregate": validate_aggregate,
			"rollout": validate_rollout,
			"rnnoise-decision": validate_rnnoise_decision,
		}[args.kind](value)
	except ValidationError as error:
		print(f"Input-enhancement {args.kind} raw-schema validation failed: {error}", file=sys.stderr)
		return 1
	print(f"Input-enhancement {args.kind} raw-schema validation passed: {args.path}")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
