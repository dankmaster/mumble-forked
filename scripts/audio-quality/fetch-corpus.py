#!/usr/bin/env python3
"""Fetch reviewed audio-quality archives exactly as pinned by corpus-lock.json.

The downloader fails closed for ambiguous/restricted licenses and writes only to
an external directory or the repository's ignored .tmp tree. It never extracts
archives and never makes downloaded audio part of a source artifact.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
import re
import sys
import tempfile
import urllib.error
import urllib.request
from pathlib import Path, PurePosixPath
from typing import Any, Iterable, Mapping, Sequence


class FetchError(RuntimeError):
	"""Raised for unsafe policy, destination, or integrity failures."""


def _load_lock_module() -> Any:
	path = Path(__file__).with_name("validate-corpus-lock.py")
	spec = importlib.util.spec_from_file_location("mumble_audio_corpus_lock", path)
	if spec is None or spec.loader is None:
		raise FetchError(f"unable to load corpus-lock validator: {path}")
	module = importlib.util.module_from_spec(spec)
	spec.loader.exec_module(module)
	return module


LOCK = _load_lock_module()


def _repo_root() -> Path:
	for candidate in Path(__file__).resolve().parents:
		if candidate.joinpath(".git").exists():
			return candidate
	raise FetchError("unable to locate repository root")


def validate_artifact_root(root: Path) -> Path:
	"""Keep protected corpus material outside tracked repository paths."""
	resolved = root.expanduser().resolve()
	repo = _repo_root()
	try:
		relative = resolved.relative_to(repo)
	except ValueError:
		return resolved
	if not relative.parts or relative.parts[0] != ".tmp":
		raise FetchError(
			f"artifact root inside the repository must be below its ignored .tmp directory: {resolved}"
		)
	return resolved


def source_policy_error(source: Mapping[str, Any], purpose: str) -> str | None:
	try:
		return LOCK.source_policy_error(source, purpose)
	except LOCK.ValidationError as error:
		raise FetchError(str(error)) from error


def _sha256(path: Path) -> str:
	digest = hashlib.sha256()
	with path.open("rb") as stream:
		for chunk in iter(lambda: stream.read(1024 * 1024), b""):
			digest.update(chunk)
	return digest.hexdigest()


def verify_archive(path: Path, integrity: Mapping[str, Any]) -> None:
	if not path.is_file():
		raise FetchError(f"archive is missing: {path}")
	actual_size = path.stat().st_size
	if actual_size != integrity["size_bytes"]:
		raise FetchError(
			f"archive size mismatch for {path}: expected {integrity['size_bytes']}, got {actual_size}"
		)
	actual_digest = _sha256(path)
	if actual_digest != integrity["digest"]:
		raise FetchError(
			f"archive sha256 mismatch for {path}: expected {integrity['digest']}, got {actual_digest}"
		)


def _destination(root: Path, source: Mapping[str, Any]) -> Path:
	parts = PurePosixPath(source["integrity"]["artifact_path"]).parts
	destination = root.joinpath(*parts).resolve()
	try:
		destination.relative_to(root)
	except ValueError as error:
		raise FetchError(f"unsafe artifact path for {source['id']}") from error
	return destination


def _fetch_locked_artifact(
	label: str, source_url: str, integrity: Mapping[str, Any], destination: Path, dry_run: bool = False
) -> tuple[str, Path]:
	if destination.exists():
		verify_archive(destination, integrity)
		return "verified-existing", destination
	if dry_run:
		return "would-download", destination

	destination.parent.mkdir(parents=True, exist_ok=True)
	request = urllib.request.Request(
		source_url,
		headers={ "User-Agent": "MumbleInputEnhancementCorpus/1" },
	)
	temporary: Path | None = None
	try:
		with tempfile.NamedTemporaryFile(
			mode="wb", prefix=f".{destination.name}.", suffix=".part", dir=destination.parent, delete=False
		) as output:
			temporary = Path(output.name)
			with urllib.request.urlopen(request, timeout=60) as response:
				while True:
					chunk = response.read(1024 * 1024)
					if not chunk:
						break
					output.write(chunk)
			output.flush()
			os.fsync(output.fileno())
		verify_archive(temporary, integrity)
		os.replace(temporary, destination)
		temporary = None
		return "downloaded", destination
	except (OSError, urllib.error.URLError) as error:
		raise FetchError(f"failed to fetch {label}: {error}") from error
	finally:
		if temporary is not None:
			try:
				temporary.unlink(missing_ok=True)
			except OSError:
				pass


def fetch_archive(source: Mapping[str, Any], root: Path, dry_run: bool = False) -> tuple[str, Path]:
	return _fetch_locked_artifact(
		source["id"], source["source_url"], source["integrity"], _destination(root, source), dry_run
	)


def fetch_sidecars(source: Mapping[str, Any], root: Path, dry_run: bool = False) -> list[tuple[Mapping[str, Any], str, Path]]:
	results = []
	for sidecar in source.get("sidecars", []):
		parts = PurePosixPath(sidecar["integrity"]["artifact_path"]).parts
		destination = root.joinpath(*parts).resolve()
		try:
			destination.relative_to(root)
		except ValueError as error:
			raise FetchError(f"unsafe sidecar artifact path for {source['id']}:{sidecar['id']}") from error
		status, path = _fetch_locked_artifact(
			f"{source['id']}:{sidecar['id']}", sidecar["source_url"], sidecar["integrity"], destination, dry_run
		)
		results.append((sidecar, status, path))
	return results


def _selected_sources(
	manifest: Mapping[str, Any], requested: Iterable[str], select_all: bool, purpose: str
) -> list[Mapping[str, Any]]:
	by_id = { source["id"]: source for source in manifest["sources"] }
	excluded = { source["id"]: source for source in manifest["excluded_sources"] }
	requested_ids = sorted(set(requested))
	if select_all:
		return [ source for source in manifest["sources"] if source_policy_error(source, purpose) is None ]
	if not requested_ids:
		raise FetchError("select at least one --source or pass --all")
	selected = []
	for source_id in requested_ids:
		if source_id in excluded:
			raise FetchError(f"source {source_id} is explicitly excluded: {excluded[source_id]['reason']}")
		if source_id not in by_id:
			raise FetchError(f"unknown source id: {source_id}")
		error = source_policy_error(by_id[source_id], purpose)
		if error:
			raise FetchError(f"source {source_id} is blocked for {purpose}: {error}")
		selected.append(by_id[source_id])
	return selected


def _write_state(
	root: Path, manifest: Mapping[str, Any], purpose: str, results: Sequence[tuple[Mapping[str, Any], str, Path]]
) -> None:
	merged: dict[str, tuple[Mapping[str, Any], str, Path]] = {
		source["id"]: (source, status, path) for source, status, path in results
	}
	for source in manifest["sources"]:
		if source["id"] in merged or source_policy_error(source, purpose) is not None:
			continue
		path = _destination(root, source)
		if not path.is_file():
			continue
		verify_archive(path, source["integrity"])
		merged[source["id"]] = (source, "verified-existing", path)
	state_results = [merged[source_id] for source_id in sorted(merged)]
	state = {
		"schema_version": 3,
		"state_kind": "mumble-input-enhancement-corpus-state",
		"corpus_lock_sha256": LOCK.canonical_manifest_sha256(manifest),
		"fetcher_sha256": _sha256(Path(__file__).resolve()),
		"purpose": purpose,
		"archives": [
			{
				"source_id": source["id"],
				"source_kind": source["kind"],
				"relative_path": path.relative_to(root).as_posix(),
				"source_url_sha256": hashlib.sha256(source["source_url"].encode("utf-8")).hexdigest(),
				"source_artifact_sha256": source["integrity"]["digest"],
				"size_bytes": source["integrity"]["size_bytes"],
				"verified": True,
			}
			for source, status, path in state_results
		],
	}
	temporary = root / ".corpus-state.json.tmp"
	temporary.write_text(json.dumps(state, indent=2, sort_keys=True) + "\n", encoding="utf-8")
	os.replace(temporary, root / "corpus-state.json")


def run_self_test() -> None:
	manifest = LOCK.load_validated_manifest(Path(__file__).with_name("corpus-lock.json"))
	by_id = { source["id"]: source for source in manifest["sources"] }
	demand = by_id["demand-ooffice-16k"]
	if source_policy_error(demand, "local-eval") is not None:
		raise AssertionError("reviewed DEMAND local-evaluation source was rejected")
	if source_policy_error(demand, "training") is None:
		raise AssertionError("evaluation-only DEMAND source was accepted for training")
	if source_policy_error(demand, "fixture") is None:
		raise AssertionError("local-only DEMAND source was accepted as a redistributable fixture")
	if source_policy_error(by_id["mcgill-tsp-speech-v2-48k"], "training") is not None:
		raise AssertionError("reviewed McGill training source was rejected")
	if source_policy_error(by_id["openslr28-rirs-noises"], "local-eval") is not None:
		raise AssertionError("reviewed OpenSLR28 noise/RIR source was rejected")
	local_eval_sources = _selected_sources(manifest, [], True, "local-eval")
	if not any(source["kind"] == "environmental_noise_and_rir" for source in local_eval_sources):
		raise AssertionError("--all local-eval does not include a fetchable real noise/RIR source")
	if source_policy_error(by_id["openslr31-mini-librispeech-dev-clean-2"], "training") is None:
		raise AssertionError("evaluation-only source was accepted for training")
	if source_policy_error(by_id["openslr12-librispeech-test-clean"], "local-eval") is not None:
		raise AssertionError("reviewed LibriSpeech test-clean expansion was rejected")
	if source_policy_error(by_id["openslr12-librispeech-test-clean"], "training") is None:
		raise AssertionError("evaluation-only LibriSpeech test-clean was accepted for training")
	fleurs = by_id["google-fleurs-sv-se-train-v2"]
	if source_policy_error(fleurs, "local-eval") is not None or len(fleurs.get("sidecars", [])) != 1:
		raise AssertionError("reviewed Swedish FLEURS archive and transcript sidecar were rejected")
	for source_id in ("rixvox-v1-dev-0", "rixvox-v1-test-0"):
		rixvox = by_id[source_id]
		if source_policy_error(rixvox, "training") is not None:
			raise AssertionError(f"reviewed RixVox source was rejected for training: {source_id}")
		if [value["kind"] for value in rixvox.get("sidecars", [])] != ["transcript_metadata"]:
			raise AssertionError(f"RixVox metadata sidecar changed: {source_id}")
	fsd50k = by_id["fsd50k-eval-cc0-subset"]
	if source_policy_error(fsd50k, "local-eval") is not None or source_policy_error(fsd50k, "training") is None:
		raise AssertionError("FSD50K evaluation subset policy changed")
	if [value["kind"] for value in fsd50k.get("sidecars", [])] != ["archive_part", "license_metadata", "label_metadata"]:
		raise AssertionError("FSD50K split archive or metadata sidecars changed")
	if "voxpopuli-swedish-speaker-expansion" not in {source["id"] for source in manifest["excluded_sources"]}:
		raise AssertionError("VoxPopuli Swedish rejection audit is missing")
	with tempfile.TemporaryDirectory() as directory:
		external = validate_artifact_root(Path(directory))
		if external != Path(directory).resolve():
			raise AssertionError("external artifact root changed unexpectedly")
		payload_path = external / "fixture.bin"
		payload_path.write_bytes(b"locked fixture")
		fixture_integrity = {
			"size_bytes": len(b"locked fixture"),
			"digest": hashlib.sha256(b"locked fixture").hexdigest(),
		}
		verify_archive(payload_path, fixture_integrity)
		payload_path.write_bytes(b"corrupt")
		try:
			verify_archive(payload_path, fixture_integrity)
		except FetchError:
			pass
		else:
			raise AssertionError("corrupt archive was accepted")
		state_root = external / "state"
		state_root.mkdir()
		state_source = by_id["openslr28-rirs-noises"]
		state_path = state_root.joinpath(*PurePosixPath(state_source["integrity"]["artifact_path"]).parts)
		state_path.parent.mkdir(parents=True)
		_write_state(state_root, manifest, "local-eval", [ (state_source, "verified-existing", state_path) ])
		state = json.loads((state_root / "corpus-state.json").read_text(encoding="utf-8"))
		if (
			state["schema_version"] != 3
			or state["state_kind"] != "mumble-input-enhancement-corpus-state"
			or state["archives"][0]["source_artifact_sha256"] != state_source["integrity"]["digest"]
			or not re.fullmatch(r"[0-9a-f]{64}", state["fetcher_sha256"])
		):
			raise AssertionError("corpus state did not expose the locked source artifact hash")
	try:
		validate_artifact_root(_repo_root() / "scripts" / "audio-quality" / "downloads")
	except FetchError:
		pass
	else:
		raise AssertionError("tracked corpus destination was accepted")
	selected_demand = _selected_sources(manifest, [ "demand-ooffice-16k" ], False, "local-eval")
	if [source["id"] for source in selected_demand] != ["demand-ooffice-16k"]:
		raise AssertionError("explicit DEMAND local-evaluation selection changed")


def main(argv: Sequence[str] | None = None) -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--manifest", type=Path, default=Path(__file__).with_name("corpus-lock.json"))
	parser.add_argument("--artifact-root", type=Path)
	parser.add_argument("--source", action="append", default=[], help="source id to fetch; repeatable")
	parser.add_argument("--all", action="store_true", help="fetch every archive approved for the selected purpose")
	parser.add_argument("--purpose", choices=("local-eval", "training", "fixture"), default="local-eval")
	parser.add_argument("--list", action="store_true", help="show policy status without downloading")
	parser.add_argument("--dry-run", action="store_true")
	parser.add_argument("--self-test", action="store_true")
	args = parser.parse_args(argv)

	try:
		if args.self_test:
			run_self_test()
			print("corpus fetcher self-test: ok")
			if not (args.list or args.source or args.all):
				return 0
		manifest = LOCK.load_validated_manifest(args.manifest)
		if args.list:
			for source in manifest["sources"]:
				error = source_policy_error(source, args.purpose)
				print(f"{source['id']}\t{'blocked: ' + error if error else 'fetchable'}")
			for source in manifest["excluded_sources"]:
				print(f"{source['id']}\texcluded: {source['reason']}")
			if not (args.source or args.all):
				return 0
		if args.artifact_root is None:
			raise FetchError("--artifact-root is required for fetch or dry-run")
		root = validate_artifact_root(args.artifact_root)
		selected = _selected_sources(manifest, args.source, args.all, args.purpose)
		results = []
		for source in selected:
			status, path = fetch_archive(source, root, dry_run=args.dry_run)
			results.append((source, status, path))
			print(f"{source['id']}: {status}: {path}")
			for sidecar, sidecar_status, sidecar_path in fetch_sidecars(source, root, dry_run=args.dry_run):
				print(f"{source['id']}:{sidecar['id']}: {sidecar_status}: {sidecar_path}")
		if not args.dry_run:
			root.mkdir(parents=True, exist_ok=True)
			_write_state(root, manifest, args.purpose, results)
		return 0
	except (FetchError, LOCK.ValidationError, AssertionError) as error:
		print(f"corpus fetch: error: {error}", file=sys.stderr)
		return 1


if __name__ == "__main__":
	sys.exit(main())
