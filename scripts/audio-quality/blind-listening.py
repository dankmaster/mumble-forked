#!/usr/bin/env python3
"""Build and evaluate offline, label-blind A/B listening sessions.

Generation writes loudness-matched WAVs, a static no-network viewer, an
identity-free ``listening-session.json``, and a separate private answer key.
Aggregation joins exported sessions to that key and enforces the community
preference gates without uploading audio or responses.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import re
import shutil
import stat
import statistics
import struct
import subprocess
import sys
import tempfile
import wave
from pathlib import Path, PurePosixPath
from typing import Any, Mapping, MutableMapping, Sequence


class ListeningError(ValueError):
	"""Raised for unsafe input, invalid responses, or a failed listening gate."""


HEX64 = re.compile(r"[0-9a-f]{64}")
IDENTIFIER = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]*")
COHORTS = ("clean", "noisy", "severe")
PROFILES = ("Original", "Light", "Balanced", "Quality", "VoiceFocus")
PREFERENCES = ("A", "B", "Tie")
PROTECTED_RUNNER_CLASSES = ("low-performance", "mainstream")
PROTECTED_SUITES = ("master_quality", "nightly")
MIN_TOTAL_PAIRS_PER_SESSION = 26
MIN_QUALITY_NOISY_PAIRS_PER_SESSION = 12
MIN_VOICE_SEVERE_PAIRS_PER_SESSION = 12
MIN_CLEAN_PAIRS_PER_SESSION = 2
MIN_DECISIVE_VOTES_PER_COMPARISON_PER_SESSION = 8
QUALIFICATION_BINDING_KEYS = {
	"case_set_sha256", "corpus_inventory_sha256", "corpus_lock_sha256", "git_sha",
	"hardware_fingerprint_sha256", "harness_sha256", "metrics_runtime_sha256",
	"mixture_plan_sha256", "model_manifest_sha256", "protected_quality_qualification_sha256",
	"qualification_suite", "recipe_manifest_sha256", "recipe_set_version", "release_fixtures_sha256",
	"runner_class", "server_binary_sha256", "staged_payload_sha256", "tested_binary_sha256",
}
CANONICALIZATION = "mumble-json-sorted-keys-indent2-utf8-lf-v1"
SESSION_SOURCE_BINDING_KEYS = {
	"answer_key_sha256", "pack_id", "qualification_binding_sha256", "source_manifest_sha256",
}
DERIVED_QUALIFICATION_KEYS = {
	"listener_count", "minimum_clean_pairs_per_session", "minimum_pairs_per_session",
	"minimum_quality_noisy_decisive_votes_per_session", "minimum_quality_noisy_pairs_per_session",
	"minimum_voice_focus_severe_decisive_votes_per_session", "minimum_voice_focus_severe_pairs_per_session",
	"quality_intelligibility_median", "quality_noisy_preference", "quality_noisy_vote_evidence",
	"recurring_clean_artifacts", "session_count", "voice_focus_intelligibility_median",
	"voice_focus_severe_preference", "voice_focus_severe_vote_evidence",
}
QUALIFICATION_KEYS = DERIVED_QUALIFICATION_KEYS | {
	"answer_key_sha256", "pack_id", "qualification_binding", "schema_version", "session_manifest",
	"source_manifest_sha256", "status",
}


def _load_json(path: Path) -> Any:
	def reject_duplicates(pairs: Sequence[tuple[str, Any]]) -> MutableMapping[str, Any]:
		result: MutableMapping[str, Any] = {}
		for key, value in pairs:
			if key in result:
				raise ListeningError(f"duplicate JSON key in {path}: {key}")
			result[key] = value
		return result
	try:
		return json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=reject_duplicates)
	except (OSError, json.JSONDecodeError) as error:
		raise ListeningError(f"unable to read {path}: {error}") from error


def _canonical_json_bytes(value: Any) -> bytes:
	try:
		return (
			json.dumps(value, indent=2, sort_keys=True, ensure_ascii=False, allow_nan=False) + "\n"
		).encode("utf-8")
	except (TypeError, ValueError) as error:
		raise ListeningError(f"value is not canonical JSON: {error}") from error


def _canonical_file_sha256(value: Any) -> str:
	return hashlib.sha256(_canonical_json_bytes(value)).hexdigest()


def _write_json(path: Path, value: Mapping[str, Any]) -> None:
	path.parent.mkdir(parents=True, exist_ok=True)
	temporary = path.with_name(f".{path.name}.{os.getpid()}.tmp")
	temporary.write_bytes(_canonical_json_bytes(value))
	os.replace(temporary, path)


def _sha256(path: Path) -> str:
	digest = hashlib.sha256()
	with path.open("rb") as stream:
		for chunk in iter(lambda: stream.read(1024 * 1024), b""):
			digest.update(chunk)
	return digest.hexdigest()


def _canonical_sha256(value: Any) -> str:
	return hashlib.sha256(
		json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")
	).hexdigest()


def _stable_bytes(seed: str, *parts: str) -> bytes:
	return hashlib.sha256("\0".join((seed, *parts)).encode("utf-8")).digest()


def _safe_input(root: Path, relative: Any, path: str) -> Path:
	if not isinstance(relative, str) or not relative:
		raise ListeningError(f"{path}: expected a non-empty path")
	parsed = PurePosixPath(relative)
	if parsed.is_absolute() or parsed.as_posix() != relative or "." in parsed.parts or ".." in parsed.parts:
		raise ListeningError(f"{path}: unsafe path")
	root = root.resolve()
	resolved = root.joinpath(*parsed.parts).resolve()
	try:
		resolved.relative_to(root)
	except ValueError as error:
		raise ListeningError(f"{path}: escapes source root") from error
	if not resolved.is_file() or resolved.is_symlink() or resolved.suffix.lower() != ".wav":
		raise ListeningError(f"{path}: expected a regular PCM WAV")
	return resolved


def _is_reparse_point(path: Path) -> bool:
	try:
		attributes = getattr(path.lstat(), "st_file_attributes", 0)
	except OSError as error:
		raise ListeningError(f"unable to inspect evidence path {path}: {error}") from error
	return path.is_symlink() or bool(attributes & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400))


def _safe_evidence_path(root: Path, relative: Any, context: str, *, expect_directory: bool = False) -> Path:
	if not isinstance(relative, str) or not relative:
		raise ListeningError(f"{context}: expected a non-empty relative path")
	parsed = PurePosixPath(relative)
	if (parsed.is_absolute() or parsed.as_posix() != relative or "." in parsed.parts or ".." in parsed.parts
		or "\\" in relative):
		raise ListeningError(f"{context}: unsafe evidence-relative path")
	root = root.resolve()
	resolved = root.joinpath(*parsed.parts)
	try:
		resolved.resolve().relative_to(root)
	except (OSError, ValueError) as error:
		raise ListeningError(f"{context}: evidence path escapes its qualification root") from error
	current = root
	for part in parsed.parts:
		current = current / part
		if not current.exists():
			raise ListeningError(f"{context}: missing evidence path {relative}")
		if _is_reparse_point(current):
			raise ListeningError(f"{context}: symlink/reparse evidence paths are forbidden")
	if expect_directory:
		if not resolved.is_dir():
			raise ListeningError(f"{context}: expected an evidence directory")
	elif not resolved.is_file() or resolved.suffix.lower() != ".json":
		raise ListeningError(f"{context}: expected a regular JSON evidence file")
	return resolved.resolve()


def _load_canonical_evidence_file(path: Path, expected_sha256: str, context: str) -> Any:
	if not HEX64.fullmatch(str(expected_sha256)) or _sha256(path) != expected_sha256:
		raise ListeningError(f"{context}: SHA-256 mismatch")
	value = _load_json(path)
	try:
		raw = path.read_bytes()
	except OSError as error:
		raise ListeningError(f"{context}: unable to read evidence bytes: {error}") from error
	if raw != _canonical_json_bytes(value):
		raise ListeningError(f"{context}: file is not canonical {CANONICALIZATION}")
	return value


def _decode_pcm(raw: bytes, width: int) -> list[float]:
	if width == 1:
		return [(value - 128) / 128.0 for value in raw]
	if width == 2:
		return [value / 32768.0 for value in struct.unpack(f"<{len(raw) // 2}h", raw)]
	if width == 3:
		values = []
		for offset in range(0, len(raw), 3):
			value = int.from_bytes(raw[offset : offset + 3], "little", signed=False)
			values.append((value - (1 << 24) if value & 0x800000 else value) / 8_388_608.0)
		return values
	if width == 4:
		return [value / 2_147_483_648.0 for value in struct.unpack(f"<{len(raw) // 4}i", raw)]
	raise ListeningError(f"unsupported PCM width: {width}")


def _read_wav(path: Path) -> tuple[int, int, list[float]]:
	try:
		with wave.open(str(path), "rb") as stream:
			if stream.getcomptype() != "NONE":
				raise ListeningError(f"compressed WAV is unsupported: {path}")
			rate, channels = stream.getframerate(), stream.getnchannels()
			samples = _decode_pcm(stream.readframes(stream.getnframes()), stream.getsampwidth())
	except (OSError, wave.Error) as error:
		raise ListeningError(f"unable to read {path}: {error}") from error
	if rate <= 0 or channels not in (1, 2) or not samples:
		raise ListeningError(f"invalid WAV format: {path}")
	return rate, channels, samples


def _write_wav(path: Path, rate: int, channels: int, samples: Sequence[float]) -> None:
	pcm = bytearray()
	for sample in samples:
		if not math.isfinite(sample):
			raise ListeningError("non-finite listening sample")
		value = max(-1.0, min(1.0, sample))
		pcm.extend(struct.pack("<h", -32768 if value <= -1.0 else round(value * 32767)))
	path.parent.mkdir(parents=True, exist_ok=True)
	with wave.open(str(path), "wb") as stream:
		stream.setnchannels(channels); stream.setsampwidth(2); stream.setframerate(rate); stream.writeframes(pcm)


def _rms(samples: Sequence[float]) -> float:
	return math.sqrt(sum(value * value for value in samples) / len(samples)) if samples else 0.0


def _match_pair(left: Path, right: Path, out_left: Path, out_right: Path) -> Mapping[str, Any]:
	left_rate, left_channels, left_samples = _read_wav(left)
	right_rate, right_channels, right_samples = _read_wav(right)
	if (left_rate, left_channels, len(left_samples)) != (right_rate, right_channels, len(right_samples)):
		raise ListeningError("A/B sources must have identical sample rate, channels, and duration")
	left_rms, right_rms = _rms(left_samples), _rms(right_samples)
	if left_rms <= 1e-8 or right_rms <= 1e-8:
		raise ListeningError("A/B source has no usable energy")
	# Match by attenuating to the quieter integrated RMS.  This is deterministic,
	# exact for PCM samples, and cannot introduce clipping through amplification.
	target = min(left_rms, right_rms)
	left_gain, right_gain = target / left_rms, target / right_rms
	_write_wav(out_left, left_rate, left_channels, [value * left_gain for value in left_samples])
	_write_wav(out_right, right_rate, right_channels, [value * right_gain for value in right_samples])
	return {
		"algorithm": "integrated-rms-attenuate-to-quieter-v1",
		"source_rms_dbfs": [20.0 * math.log10(left_rms), 20.0 * math.log10(right_rms)],
		"matched_rms_dbfs": 20.0 * math.log10(target),
		"gain_db": [20.0 * math.log10(left_gain), 20.0 * math.log10(right_gain)],
	}


def _validate_qualification_binding(value: Any) -> Mapping[str, Any]:
	if not isinstance(value, dict) or set(value) != QUALIFICATION_BINDING_KEYS:
		raise ListeningError("qualification binding has missing or unexpected keys")
	if not re.fullmatch(r"[0-9a-f]{40}", str(value["git_sha"])):
		raise ListeningError("qualification binding git_sha is invalid")
	for key in QUALIFICATION_BINDING_KEYS - {
		"git_sha", "qualification_suite", "recipe_set_version", "runner_class",
	}:
		if not HEX64.fullmatch(str(value[key])):
			raise ListeningError(f"qualification binding {key} is invalid")
	if not isinstance(value["recipe_set_version"], str) or not IDENTIFIER.fullmatch(value["recipe_set_version"]):
		raise ListeningError("qualification binding recipe_set_version is invalid")
	if value["runner_class"] not in PROTECTED_RUNNER_CLASSES:
		raise ListeningError("qualification binding runner_class is not a protected runner class")
	if value["qualification_suite"] not in PROTECTED_SUITES:
		raise ListeningError("qualification binding qualification_suite is invalid")
	return value


def _comparison_kind(pair: Mapping[str, Any]) -> str | None:
	profiles = {pair["left"]["label"], pair["right"]["label"]}
	if pair["cohort"] == "noisy" and profiles == {"Quality", "Original"}:
		return "quality_noisy"
	if pair["cohort"] == "severe" and profiles == {"VoiceFocus", "Quality"}:
		return "voice_focus_severe"
	if pair["cohort"] == "clean":
		return "clean"
	return None


def _validate_pair_coverage(pairs: Sequence[Mapping[str, Any]], path: str) -> None:
	counts = {"quality_noisy": 0, "voice_focus_severe": 0, "clean": 0}
	clean_profiles: set[str] = set()
	for pair in pairs:
		kind = _comparison_kind(pair)
		if kind is not None:
			counts[kind] += 1
		if kind == "clean":
			clean_profiles.update((pair["left"]["label"], pair["right"]["label"]))
	if len(pairs) < MIN_TOTAL_PAIRS_PER_SESSION:
		raise ListeningError(f"{path}: requires at least {MIN_TOTAL_PAIRS_PER_SESSION} distinct pairs")
	if counts["quality_noisy"] < MIN_QUALITY_NOISY_PAIRS_PER_SESSION:
		raise ListeningError(
			f"{path}: requires at least {MIN_QUALITY_NOISY_PAIRS_PER_SESSION} noisy Quality-vs-Original pairs"
		)
	if counts["voice_focus_severe"] < MIN_VOICE_SEVERE_PAIRS_PER_SESSION:
		raise ListeningError(
			f"{path}: requires at least {MIN_VOICE_SEVERE_PAIRS_PER_SESSION} severe VoiceFocus-vs-Quality pairs"
		)
	if counts["clean"] < MIN_CLEAN_PAIRS_PER_SESSION or not {"Quality", "VoiceFocus"}.issubset(clean_profiles):
		raise ListeningError(
			f"{path}: requires at least {MIN_CLEAN_PAIRS_PER_SESSION} clean pairs covering Quality and VoiceFocus"
		)


def _validate_source_manifest(value: Any) -> list[Mapping[str, Any]]:
	if not isinstance(value, dict) or set(value) != {"pairs", "qualification_binding", "schema_version", "session_id"}:
		raise ListeningError("source manifest must contain schema_version, session_id, qualification_binding, and pairs")
	if value["schema_version"] != 3 or not IDENTIFIER.fullmatch(str(value["session_id"])):
		raise ListeningError("source manifest identity/schema is invalid")
	_validate_qualification_binding(value["qualification_binding"])
	if not isinstance(value["pairs"], list) or not value["pairs"]:
		raise ListeningError("source manifest pairs must be non-empty")
	ids = []
	for index, pair in enumerate(value["pairs"]):
		if not isinstance(pair, dict) or set(pair) != {"cohort", "id", "left", "right"}:
			raise ListeningError(f"pairs[{index}] has invalid keys")
		if not IDENTIFIER.fullmatch(str(pair["id"])) or pair["cohort"] not in COHORTS:
			raise ListeningError(f"pairs[{index}] has invalid id/cohort")
		for side in ("left", "right"):
			entry = pair[side]
			if not isinstance(entry, dict) or set(entry) != {"label", "relative_path", "sha256"}:
				raise ListeningError(f"pairs[{index}].{side} has invalid keys")
			if entry["label"] not in PROFILES or not HEX64.fullmatch(str(entry["sha256"])):
				raise ListeningError(f"pairs[{index}].{side} has invalid label/hash")
		if pair["left"]["label"] == pair["right"]["label"]:
			raise ListeningError(f"pairs[{index}] compares the same profile")
		ids.append(pair["id"])
	if ids != sorted(set(ids)):
		raise ListeningError("source pair ids must be unique and sorted")
	_validate_pair_coverage(value["pairs"], "source manifest")
	return value["pairs"]


def _balanced_orientation_plan(pairs: Sequence[Mapping[str, Any]], seed: str) -> Mapping[str, bool]:
	"""Return deterministic swaps with balanced A/B placement per cohort/profile pair."""
	groups: dict[tuple[str, tuple[str, str]], list[Mapping[str, Any]]] = {}
	for pair in pairs:
		labels = tuple(sorted((pair["left"]["label"], pair["right"]["label"])))
		groups.setdefault((pair["cohort"], labels), []).append(pair)
	result: dict[str, bool] = {}
	for (cohort, labels), group in sorted(groups.items()):
		randomized = sorted(group, key=lambda pair: _stable_bytes(seed, "orientation", pair["id"]))
		start = _stable_bytes(seed, "orientation-start", cohort, *labels)[0] & 1
		for index, pair in enumerate(randomized):
			desired_a = labels[(index + start) & 1]
			result[pair["id"]] = pair["left"]["label"] != desired_a
	return result


def _validate_orientation_coverage(key_pairs: Sequence[Mapping[str, Any]]) -> None:
	for name, cohort, profiles in (
		("Quality-vs-Original noisy", "noisy", {"Quality", "Original"}),
		("VoiceFocus-vs-Quality severe", "severe", {"VoiceFocus", "Quality"}),
	):
		matching = [
			pair for pair in key_pairs
			if pair["cohort"] == cohort and {pair["profile_a"], pair["profile_b"]} == profiles
		]
		for profile in profiles:
			in_a = sum(pair["profile_a"] == profile for pair in matching)
			in_b = sum(pair["profile_b"] == profile for pair in matching)
			if min(in_a, in_b) < 4:
				raise ListeningError(f"{name} randomization is not meaningfully balanced for {profile}")


def _viewer(session: Mapping[str, Any]) -> str:
	public_json = json.dumps(session, ensure_ascii=False, separators=(",", ":")).replace("</", "<\\/")
	return f"""<!doctype html>
<html lang="sv"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; media-src 'self' blob:; style-src 'unsafe-inline'; script-src 'unsafe-inline'; connect-src 'none'; img-src 'none'; object-src 'none'; base-uri 'none'; form-action 'none'">
<title>Blind A/B-lyssning</title><style>
body{{font:16px system-ui;max-width:900px;margin:2rem auto;padding:0 1rem;background:#111;color:#eee}}
.pair{{border:1px solid #555;border-radius:10px;padding:1rem;margin:1rem 0}} audio{{width:48%;margin-right:1%}}
label{{display:block;margin:.6rem 0}} select,input{{font:inherit}} .muted{{color:#aaa}} button{{padding:.7rem 1rem}}
</style></head><body><h1>Blind A/B-lyssning</h1>
<p class="muted">Profilerna är dolda. Lyssna i valfri ordning och använd samma uppspelningsnivå.</p>
<label>Lokalt lyssnar-ID <input id="listener" autocomplete="off" placeholder="pseudonym"></label>
<main id="pairs"></main><button id="export">Spara listening-session.json</button>
<script>const session={public_json}; const root=document.getElementById('pairs');
const dims=['quality','noise_control','intelligibility','artifacts'];
for(const p of session.pairs){{const d=document.createElement('section');d.className='pair';
d.innerHTML=`<h2>Par ${{p.ordinal}}</h2><audio controls preload="metadata" src="${{p.audio_a.relative_path}}"></audio><audio controls preload="metadata" src="${{p.audio_b.relative_path}}"></audio>
<label>Helhet <select data-pair="${{p.pair_id}}" data-key="overall_preference"><option value="">Välj</option><option>A</option><option>B</option><option>Tie</option></select></label>`;
for(const dim of dims){{const nice={{quality:'Kvalitet',noise_control:'Bruskontroll',intelligibility:'Talbegriplighet',artifacts:'Frihet från artefakter'}}[dim];
d.insertAdjacentHTML('beforeend',`<label>${{nice}} A <input type="range" min="1" max="5" value="3" data-pair="${{p.pair_id}}" data-key="${{dim}}_a"> B <input type="range" min="1" max="5" value="3" data-pair="${{p.pair_id}}" data-key="${{dim}}_b"></label>`);}}
d.insertAdjacentHTML('beforeend',`<label>Artefakttaggar A <input data-pair="${{p.pair_id}}" data-key="artifact_tags_a" placeholder="t.ex. metallic,pumping"></label><label>Artefakttaggar B <input data-pair="${{p.pair_id}}" data-key="artifact_tags_b"></label>`);root.appendChild(d);}}
document.getElementById('export').onclick=()=>{{const out=structuredClone(session);out.listener_id=document.getElementById('listener').value.trim();out.session_instance_id=crypto.randomUUID();out.responses={{}};
for(const el of document.querySelectorAll('[data-pair]')){{const id=el.dataset.pair,key=el.dataset.key;(out.responses[id]??={{}})[key]=key.startsWith('artifact_tags_')?el.value.split(',').map(x=>x.trim().toLowerCase()).filter(Boolean):key==='overall_preference'?el.value:Number(el.value);}}
const blob=new Blob([JSON.stringify(out,null,2)+'\\n'],{{type:'application/json'}}),a=document.createElement('a');a.href=URL.createObjectURL(blob);a.download='listening-session.json';a.click();setTimeout(()=>URL.revokeObjectURL(a.href),1000);}};
</script></body></html>"""


def generate(source_manifest: Path, source_root: Path, output_root: Path, seed: str) -> Mapping[str, Any]:
	value = _load_json(source_manifest)
	pairs = _validate_source_manifest(value)
	if output_root.exists() and any(output_root.iterdir()):
		raise ListeningError(f"output root must be empty: {output_root}")
	output_root.mkdir(parents=True, exist_ok=True)
	ordered = sorted(pairs, key=lambda pair: _stable_bytes(seed, "order", pair["id"]))
	orientations = _balanced_orientation_plan(pairs, seed)
	public_pairs = []
	key_pairs = []
	for ordinal, pair in enumerate(ordered, start=1):
		digest = _stable_bytes(seed, "pair", pair["id"])
		opaque = digest.hex()[:16]
		swap = orientations[pair["id"]]
		first = pair["right"] if swap else pair["left"]
		second = pair["left"] if swap else pair["right"]
		first_source = _safe_input(source_root, first["relative_path"], f"pair {pair['id']} first")
		second_source = _safe_input(source_root, second["relative_path"], f"pair {pair['id']} second")
		if _sha256(first_source) != first["sha256"] or _sha256(second_source) != second["sha256"]:
			raise ListeningError(f"pair {pair['id']}: source SHA-256 mismatch")
		pair_root = output_root / "audio" / opaque
		audio_a, audio_b = pair_root / "a.wav", pair_root / "b.wav"
		loudness = _match_pair(first_source, second_source, audio_a, audio_b)
		public_pair = {
			"pair_id": opaque, "ordinal": ordinal,
			"audio_a": {"relative_path": f"audio/{opaque}/a.wav", "sha256": _sha256(audio_a)},
			"audio_b": {"relative_path": f"audio/{opaque}/b.wav", "sha256": _sha256(audio_b)},
			"loudness_match": loudness,
		}
		public_pairs.append(public_pair)
		key_pairs.append({
			"pair_id": opaque, "source_pair_id": pair["id"], "cohort": pair["cohort"],
			"profile_a": first["label"], "profile_b": second["label"],
			"source_a_sha256": first["sha256"], "source_b_sha256": second["sha256"],
			"public_pair_sha256": _canonical_sha256(public_pair),
		})
	_validate_orientation_coverage(key_pairs)
	pack_id = _canonical_sha256(public_pairs)
	answer_key = {
		"schema_version": 3, "answer_key": "mumble-blind-listening-key-v3", "session_id": value["session_id"],
		"seed_sha256": hashlib.sha256(seed.encode("utf-8")).hexdigest(), "pairs": key_pairs,
		"pack_id": pack_id,
		"source_manifest_sha256": _canonical_file_sha256(value),
		"qualification_binding": value["qualification_binding"],
		"private_do_not_distribute_to_listeners": True,
	}
	key_path = output_root / "private" / "answer-key.json"
	_write_json(key_path, answer_key)
	session = {
		"schema_version": 2, "session": "mumble-blind-listening-v2", "session_id": value["session_id"],
		"pack_id": pack_id, "answer_key_sha256": _sha256(key_path),
		"qualification_binding_sha256": _canonical_sha256(value["qualification_binding"]),
		"loudness_algorithm": "integrated-rms-attenuate-to-quieter-v1", "offline_only": True,
		"listener_id": None, "session_instance_id": None, "pairs": public_pairs, "responses": {},
	}
	_write_json(output_root / "listening-session.json", session)
	(output_root / "index.html").write_text(_viewer(session), encoding="utf-8")
	validate_session(session, require_complete=False)
	return session


def _score(value: Any, path: str) -> int:
	if not isinstance(value, int) or isinstance(value, bool) or not 1 <= value <= 5:
		raise ListeningError(f"{path}: must be an integer from 1 to 5")
	return value


def _finite_number(value: Any, path: str) -> float:
	if not isinstance(value, (int, float)) or isinstance(value, bool) or not math.isfinite(float(value)):
		raise ListeningError(f"{path}: must be finite")
	return float(value)


def _validate_public_pair(pair: Any, index: int) -> str:
	if not isinstance(pair, dict) or set(pair) != {"audio_a", "audio_b", "loudness_match", "ordinal", "pair_id"}:
		raise ListeningError(f"session pairs[{index}] has invalid keys")
	pair_id = str(pair["pair_id"])
	if not re.fullmatch(r"[0-9a-f]{16}", pair_id):
		raise ListeningError(f"session pairs[{index}] has invalid opaque id")
	if not isinstance(pair["ordinal"], int) or isinstance(pair["ordinal"], bool) or pair["ordinal"] != index + 1:
		raise ListeningError(f"session pairs[{index}] has invalid ordinal")
	for side in ("a", "b"):
		audio = pair[f"audio_{side}"]
		if not isinstance(audio, dict) or set(audio) != {"relative_path", "sha256"}:
			raise ListeningError(f"session pairs[{index}].audio_{side} has invalid keys")
		if audio["relative_path"] != f"audio/{pair_id}/{side}.wav" or not HEX64.fullmatch(str(audio["sha256"])):
			raise ListeningError(f"session pairs[{index}].audio_{side} has invalid identity")
	loudness = pair["loudness_match"]
	if not isinstance(loudness, dict) or set(loudness) != {
		"algorithm", "gain_db", "matched_rms_dbfs", "source_rms_dbfs",
	}:
		raise ListeningError(f"session pairs[{index}].loudness_match has invalid keys")
	if loudness["algorithm"] != "integrated-rms-attenuate-to-quieter-v1":
		raise ListeningError(f"session pairs[{index}] has an unapproved loudness algorithm")
	_finite_number(loudness["matched_rms_dbfs"], f"session pairs[{index}].matched_rms_dbfs")
	for name in ("gain_db", "source_rms_dbfs"):
		values = loudness[name]
		if not isinstance(values, list) or len(values) != 2:
			raise ListeningError(f"session pairs[{index}].{name} must contain two values")
		for value_index, item in enumerate(values):
			_finite_number(item, f"session pairs[{index}].{name}[{value_index}]")
	return pair_id


def validate_session(value: Any, *, require_complete: bool) -> Mapping[str, Any]:
	required = {
		"answer_key_sha256", "listener_id", "loudness_algorithm", "offline_only", "pack_id", "pairs", "responses",
		"qualification_binding_sha256", "schema_version", "session", "session_id", "session_instance_id",
	}
	if not isinstance(value, dict) or set(value) != required:
		raise ListeningError("listening session has invalid keys")
	if value["schema_version"] != 2 or value["session"] != "mumble-blind-listening-v2" or value["offline_only"] is not True:
		raise ListeningError("listening session identity/offline contract is invalid")
	if value["loudness_algorithm"] != "integrated-rms-attenuate-to-quieter-v1" or not IDENTIFIER.fullmatch(str(value["session_id"])):
		raise ListeningError("listening session algorithm/session_id is invalid")
	for key in ("answer_key_sha256", "pack_id", "qualification_binding_sha256"):
		if not isinstance(value[key], str) or not HEX64.fullmatch(value[key]):
			raise ListeningError(f"listening session {key} is invalid")
	if not isinstance(value["pairs"], list) or not value["pairs"]:
		raise ListeningError("listening session pairs are missing")
	pair_ids = []
	for index, pair in enumerate(value["pairs"]):
		pair_ids.append(_validate_public_pair(pair, index))
	if len(pair_ids) != len(set(pair_ids)):
		raise ListeningError("session pair ids are not unique")
	if value["pack_id"] != _canonical_sha256(value["pairs"]):
		raise ListeningError("listening session pack_id does not bind its public pair/audio manifest")
	if not isinstance(value["responses"], dict) or set(value["responses"]) - set(pair_ids):
		raise ListeningError("session responses contain unknown pairs")
	if require_complete:
		if not isinstance(value["listener_id"], str) or not IDENTIFIER.fullmatch(value["listener_id"]):
			raise ListeningError("completed session requires a stable pseudonymous listener_id")
		if not isinstance(value["session_instance_id"], str) or not IDENTIFIER.fullmatch(value["session_instance_id"]):
			raise ListeningError("completed session requires session_instance_id")
		if set(value["responses"]) != set(pair_ids):
			raise ListeningError("completed session must answer every pair exactly once")
		if len(value["responses"]) < MIN_TOTAL_PAIRS_PER_SESSION:
			raise ListeningError(f"completed session requires at least {MIN_TOTAL_PAIRS_PER_SESSION} answered pairs")
	for pair_id, response in value["responses"].items():
		required_response = {
			"artifact_tags_a", "artifact_tags_b", "artifacts_a", "artifacts_b", "intelligibility_a",
			"intelligibility_b", "noise_control_a", "noise_control_b", "overall_preference", "quality_a", "quality_b",
		}
		if not isinstance(response, dict) or set(response) != required_response:
			raise ListeningError(f"response {pair_id} has invalid keys")
		if response["overall_preference"] not in PREFERENCES:
			raise ListeningError(f"response {pair_id} has invalid overall preference")
		for dimension in ("quality", "noise_control", "intelligibility", "artifacts"):
			_score(response[f"{dimension}_a"], f"response {pair_id}.{dimension}_a")
			_score(response[f"{dimension}_b"], f"response {pair_id}.{dimension}_b")
		for side in ("a", "b"):
			tags = response[f"artifact_tags_{side}"]
			if not isinstance(tags, list) or any(not isinstance(tag, str) or not re.fullmatch(r"[a-z0-9][a-z0-9._-]*", tag) for tag in tags):
				raise ListeningError(f"response {pair_id}.artifact_tags_{side} is invalid")
			if len(tags) != len(set(tags)):
				raise ListeningError(f"response {pair_id}.artifact_tags_{side} contains duplicates")
	return value


def _validate_answer_key(value: Any) -> Mapping[str, Any]:
	required = {
		"answer_key", "pack_id", "pairs", "private_do_not_distribute_to_listeners", "qualification_binding",
		"schema_version", "seed_sha256", "session_id", "source_manifest_sha256",
	}
	if not isinstance(value, dict) or set(value) != required:
		raise ListeningError("answer key has missing or unexpected keys")
	if value["schema_version"] != 3 or value["answer_key"] != "mumble-blind-listening-key-v3":
		raise ListeningError("answer key identity/schema is invalid")
	if value["private_do_not_distribute_to_listeners"] is not True or not IDENTIFIER.fullmatch(str(value["session_id"])):
		raise ListeningError("answer key privacy/session identity is invalid")
	for name in ("pack_id", "seed_sha256", "source_manifest_sha256"):
		if not HEX64.fullmatch(str(value[name])):
			raise ListeningError(f"answer key {name} is invalid")
	_validate_qualification_binding(value["qualification_binding"])
	if not isinstance(value["pairs"], list) or not value["pairs"]:
		raise ListeningError("answer key pairs are missing")
	pair_ids: list[str] = []
	source_ids: list[str] = []
	for index, pair in enumerate(value["pairs"]):
		if not isinstance(pair, dict) or set(pair) != {
			"cohort", "pair_id", "profile_a", "profile_b", "source_a_sha256", "source_b_sha256",
			"public_pair_sha256", "source_pair_id",
		}:
			raise ListeningError(f"answer key pairs[{index}] has invalid keys")
		if not re.fullmatch(r"[0-9a-f]{16}", str(pair["pair_id"])) or not IDENTIFIER.fullmatch(str(pair["source_pair_id"])):
			raise ListeningError(f"answer key pairs[{index}] has invalid identity")
		if pair["cohort"] not in COHORTS or pair["profile_a"] not in PROFILES or pair["profile_b"] not in PROFILES:
			raise ListeningError(f"answer key pairs[{index}] has invalid cohort/profile")
		if pair["profile_a"] == pair["profile_b"]:
			raise ListeningError(f"answer key pairs[{index}] compares the same profile")
		for name in ("public_pair_sha256", "source_a_sha256", "source_b_sha256"):
			if not HEX64.fullmatch(str(pair[name])):
				raise ListeningError(f"answer key pairs[{index}].{name} is invalid")
		pair_ids.append(pair["pair_id"])
		source_ids.append(pair["source_pair_id"])
	if len(pair_ids) != len(set(pair_ids)) or len(source_ids) != len(set(source_ids)):
		raise ListeningError("answer key pair identities are not unique")
	_validate_orientation_coverage(value["pairs"])
	return value


def _answer_kind(answer: Mapping[str, Any]) -> str | None:
	profiles = {answer["profile_a"], answer["profile_b"]}
	if answer["cohort"] == "noisy" and profiles == {"Quality", "Original"}:
		return "quality_noisy"
	if answer["cohort"] == "severe" and profiles == {"VoiceFocus", "Quality"}:
		return "voice_focus_severe"
	if answer["cohort"] == "clean":
		return "clean"
	return None


def _validate_answer_key_against_source(source: Mapping[str, Any], key: Mapping[str, Any]) -> None:
	source_pairs = _validate_source_manifest(source)
	_validate_answer_key(key)
	if key["session_id"] != source["session_id"] or key["qualification_binding"] != source["qualification_binding"]:
		raise ListeningError("answer key does not bind the source manifest identity")
	if key["source_manifest_sha256"] != _canonical_file_sha256(source):
		raise ListeningError("answer key does not bind the canonical source manifest bytes")
	source_lookup = {pair["id"]: pair for pair in source_pairs}
	if {pair["source_pair_id"] for pair in key["pairs"]} != set(source_lookup):
		raise ListeningError("answer key has missing or extra source pairs")
	for answer in key["pairs"]:
		source_pair = source_lookup[answer["source_pair_id"]]
		if answer["cohort"] != source_pair["cohort"]:
			raise ListeningError(f"answer key pair {answer['pair_id']} has the wrong source cohort")
		actual = (
			(answer["profile_a"], answer["source_a_sha256"]),
			(answer["profile_b"], answer["source_b_sha256"]),
		)
		forward = (
			(source_pair["left"]["label"], source_pair["left"]["sha256"]),
			(source_pair["right"]["label"], source_pair["right"]["sha256"]),
		)
		if actual not in (forward, tuple(reversed(forward))):
			raise ListeningError(f"answer key pair {answer['pair_id']} is not an orientation of its source pair")


def _validate_session_binding(session: Mapping[str, Any], key: Mapping[str, Any], key_hash: str) -> None:
	binding_hash = _canonical_sha256(key["qualification_binding"])
	lookup = {pair["pair_id"]: pair for pair in key["pairs"]}
	if (session["answer_key_sha256"] != key_hash
		or session["qualification_binding_sha256"] != binding_hash
		or session["pack_id"] != key["pack_id"]
		or session["session_id"] != key["session_id"]
		or {pair["pair_id"] for pair in session["pairs"]} != set(lookup)
		or set(session["responses"]) != set(lookup)):
		raise ListeningError("session does not bind this answer key and its exact pair set")
	for public_pair in session["pairs"]:
		answer = lookup[public_pair["pair_id"]]
		if answer["public_pair_sha256"] != _canonical_sha256(public_pair):
			raise ListeningError(
				f"session public pair {public_pair['pair_id']} does not match the source-bound answer key"
			)


def _summarize_sessions(
	key: Mapping[str, Any], sessions: Sequence[Mapping[str, Any]], expected_community_size: int | None,
) -> Mapping[str, Any]:
	if not sessions:
		raise ListeningError("at least one completed session is required")
	lookup = {pair["pair_id"]: pair for pair in key["pairs"]}
	instance_ids = [session["session_instance_id"] for session in sessions]
	if len(instance_ids) != len(set(instance_ids)):
		raise ListeningError("duplicate session_instance_id")
	listeners = sorted({session["listener_id"] for session in sessions})
	if expected_community_size is None or expected_community_size >= 3:
		if len(listeners) < 3:
			raise ListeningError("listening gate requires at least three distinct listeners")
	else:
		if expected_community_size not in (1, 2) or len(listeners) != expected_community_size:
			raise ListeningError("small-community gate requires every declared listener")
		for listener in listeners:
			if sum(session["listener_id"] == listener for session in sessions) < 2:
				raise ListeningError("small-community gate requires two sessions per listener")

	quality_votes = {"Quality": 0, "Original": 0}
	voice_votes = {"VoiceFocus": 0, "Quality": 0}
	quality_ties = 0
	voice_ties = 0
	quality_presented = 0
	voice_presented = 0
	voice_intelligibility: dict[str, list[int]] = {"VoiceFocus": [], "Quality": []}
	clean_artifact_reporters: dict[tuple[str, str], set[str]] = {}
	session_coverages = []
	for session in sessions:
		coverage = {
			"clean": 0, "quality_noisy": 0, "quality_noisy_decisive": 0,
			"voice_focus_severe": 0, "voice_focus_severe_decisive": 0,
		}
		clean_profiles: set[str] = set()
		for pair_id, response in session["responses"].items():
			answer = lookup[pair_id]
			kind = _answer_kind(answer)
			profiles = {"A": answer["profile_a"], "B": answer["profile_b"]}
			chosen = profiles.get(response["overall_preference"])
			profile_set = {answer["profile_a"], answer["profile_b"]}
			if kind == "quality_noisy":
				coverage["quality_noisy"] += 1
				quality_presented += 1
				if chosen in quality_votes:
					coverage["quality_noisy_decisive"] += 1
					quality_votes[chosen] += 1
				else:
					quality_ties += 1
			if kind == "voice_focus_severe":
				coverage["voice_focus_severe"] += 1
				voice_presented += 1
				if chosen in voice_votes:
					coverage["voice_focus_severe_decisive"] += 1
					voice_votes[chosen] += 1
				else:
					voice_ties += 1
				voice_intelligibility[answer["profile_a"]].append(response["intelligibility_a"])
				voice_intelligibility[answer["profile_b"]].append(response["intelligibility_b"])
			if kind == "clean":
				coverage["clean"] += 1
				clean_profiles.update(profile_set)
				for side in ("a", "b"):
					profile = answer[f"profile_{side}"]
					for tag in set(response[f"artifact_tags_{side}"]):
						clean_artifact_reporters.setdefault((profile, tag), set()).add(session["listener_id"])
		if coverage["quality_noisy"] < MIN_QUALITY_NOISY_PAIRS_PER_SESSION:
			raise ListeningError("each session requires at least 12 answered noisy Quality-vs-Original pairs")
		if coverage["voice_focus_severe"] < MIN_VOICE_SEVERE_PAIRS_PER_SESSION:
			raise ListeningError("each session requires at least 12 answered severe VoiceFocus-vs-Quality pairs")
		if coverage["clean"] < MIN_CLEAN_PAIRS_PER_SESSION or not {"Quality", "VoiceFocus"}.issubset(clean_profiles):
			raise ListeningError("each session requires clean-pair evidence covering Quality and VoiceFocus")
		if coverage["quality_noisy_decisive"] < MIN_DECISIVE_VOTES_PER_COMPARISON_PER_SESSION:
			raise ListeningError("Quality-vs-Original evidence has too many ties in a session")
		if coverage["voice_focus_severe_decisive"] < MIN_DECISIVE_VOTES_PER_COMPARISON_PER_SESSION:
			raise ListeningError("VoiceFocus-vs-Quality evidence has too many ties in a session")
		session_coverages.append(coverage)

	def preference_rate(votes: Mapping[str, int], preferred: str) -> float:
		total = sum(votes.values())
		if total == 0:
			raise ListeningError(f"no non-tie votes for required comparison: {', '.join(votes)}")
		return votes[preferred] / total

	quality_rate = preference_rate(quality_votes, "Quality")
	voice_rate = preference_rate(voice_votes, "VoiceFocus")
	if quality_rate < 0.60:
		raise ListeningError(f"Quality noisy preference gate failed: {quality_rate:.3f} < 0.600")
	if voice_rate < 0.60:
		raise ListeningError(f"VoiceFocus severe preference gate failed: {voice_rate:.3f} < 0.600")
	if not voice_intelligibility["VoiceFocus"] or not voice_intelligibility["Quality"]:
		raise ListeningError("VoiceFocus/Quality intelligibility evidence is missing")
	voice_median = statistics.median(voice_intelligibility["VoiceFocus"])
	quality_median = statistics.median(voice_intelligibility["Quality"])
	if voice_median < quality_median:
		raise ListeningError(f"VoiceFocus intelligibility median {voice_median} is below Quality {quality_median}")
	recurring = [
		{"profile": profile, "tag": tag, "listener_count": len(reporters)}
		for (profile, tag), reporters in sorted(clean_artifact_reporters.items()) if len(reporters) >= 2
	]
	if recurring:
		raise ListeningError(f"recurring clean-speech artifact reports: {recurring}")
	return {
		"session_count": len(sessions), "listener_count": len(listeners),
		"minimum_pairs_per_session": min(len(session["responses"]) for session in sessions),
		"minimum_clean_pairs_per_session": min(item["clean"] for item in session_coverages),
		"minimum_quality_noisy_pairs_per_session": min(item["quality_noisy"] for item in session_coverages),
		"minimum_quality_noisy_decisive_votes_per_session": min(
			item["quality_noisy_decisive"] for item in session_coverages
		),
		"minimum_voice_focus_severe_pairs_per_session": min(item["voice_focus_severe"] for item in session_coverages),
		"minimum_voice_focus_severe_decisive_votes_per_session": min(
			item["voice_focus_severe_decisive"] for item in session_coverages
		),
		"quality_noisy_preference": quality_rate, "voice_focus_severe_preference": voice_rate,
		"quality_noisy_vote_evidence": {
			"presented": quality_presented, "decisive": sum(quality_votes.values()), "ties": quality_ties,
			"preferred": quality_votes["Quality"], "comparator": quality_votes["Original"],
		},
		"voice_focus_severe_vote_evidence": {
			"presented": voice_presented, "decisive": sum(voice_votes.values()), "ties": voice_ties,
			"preferred": voice_votes["VoiceFocus"], "comparator": voice_votes["Quality"],
		},
		"voice_focus_intelligibility_median": voice_median, "quality_intelligibility_median": quality_median,
		"recurring_clean_artifacts": recurring,
	}


def _validate_session_manifest(value: Any) -> Mapping[str, Any]:
	required = {
		"answer_key", "canonicalization", "evidence_root", "schema_version", "session_root", "sessions",
		"source_manifest",
	}
	if not isinstance(value, dict) or set(value) != required:
		raise ListeningError("qualification session_manifest has missing or unexpected keys")
	if value["schema_version"] != 1 or value["canonicalization"] != CANONICALIZATION:
		raise ListeningError("qualification session_manifest identity/canonicalization is invalid")
	root = value["evidence_root"]
	if not isinstance(root, str) or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*\.evidence", root):
		raise ListeningError("qualification evidence_root is invalid")
	if value["session_root"] != f"{root}/sessions":
		raise ListeningError("qualification session_root is not canonical")
	for name, expected_path in (
		("source_manifest", f"{root}/source-manifest.json"),
		("answer_key", f"{root}/private/answer-key.json"),
	):
		record = value[name]
		if not isinstance(record, dict) or set(record) != {"relative_path", "sha256"}:
			raise ListeningError(f"qualification {name} reference is invalid")
		if record["relative_path"] != expected_path or not HEX64.fullmatch(str(record["sha256"])):
			raise ListeningError(f"qualification {name} path/hash is invalid")
	records = value["sessions"]
	if not isinstance(records, list) or not records:
		raise ListeningError("qualification session list is empty")
	for index, record in enumerate(records):
		if not isinstance(record, dict) or set(record) != {
			"listener_pseudonym", "relative_path", "session_id", "session_instance_id", "sha256", "source_binding",
		}:
			raise ListeningError(f"qualification sessions[{index}] has invalid keys")
		for name in ("listener_pseudonym", "session_id", "session_instance_id"):
			if not isinstance(record[name], str) or not IDENTIFIER.fullmatch(record[name]):
				raise ListeningError(f"qualification sessions[{index}].{name} is invalid")
		if not HEX64.fullmatch(str(record["sha256"])):
			raise ListeningError(f"qualification sessions[{index}].sha256 is invalid")
		expected_prefix = f"{value['session_root']}/{index:06d}-"
		if record["relative_path"] != f"{expected_prefix}{record['sha256']}.json":
			raise ListeningError("qualification session paths are not canonical/sorted")
		binding = record["source_binding"]
		if not isinstance(binding, dict) or set(binding) != SESSION_SOURCE_BINDING_KEYS:
			raise ListeningError(f"qualification sessions[{index}].source_binding is invalid")
		for name in SESSION_SOURCE_BINDING_KEYS:
			if not HEX64.fullmatch(str(binding[name])):
				raise ListeningError(f"qualification sessions[{index}].source_binding.{name} is invalid")
	if records != sorted(
		records,
		key=lambda item: (item["listener_pseudonym"], item["session_id"], item["session_instance_id"]),
	):
		raise ListeningError("qualification sessions are not sorted by listener/session identity")
	return value


def aggregate(
	source_manifest_path: Path, answer_key_path: Path, session_paths: Sequence[Path],
	expected_community_size: int | None, aggregate_output: Path,
) -> Mapping[str, Any]:
	source = _load_json(source_manifest_path)
	_validate_source_manifest(source)
	key = _validate_answer_key(_load_json(answer_key_path))
	_validate_answer_key_against_source(source, key)
	key_hash = _canonical_file_sha256(key)
	source_hash = _canonical_file_sha256(source)
	if key["source_manifest_sha256"] != source_hash:
		raise ListeningError("answer key source hash differs from the canonical source manifest")
	sessions = sorted(
		(validate_session(_load_json(path), require_complete=True) for path in session_paths),
		key=lambda item: (item["listener_id"], item["session_id"], item["session_instance_id"]),
	)
	for session in sessions:
		_validate_session_binding(session, key, key_hash)
	if sessions and any(session["pairs"] != sessions[0]["pairs"] for session in sessions[1:]):
		raise ListeningError("completed sessions do not contain the exact same public pair manifest")
	summary = _summarize_sessions(key, sessions, expected_community_size)

	aggregate_output = aggregate_output.resolve()
	aggregate_output.parent.mkdir(parents=True, exist_ok=True)
	evidence_name = f"{aggregate_output.stem}.evidence"
	if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*\.evidence", evidence_name):
		raise ListeningError("aggregate output filename cannot form a safe evidence root")
	evidence_root = aggregate_output.parent / evidence_name
	if evidence_root.exists():
		raise ListeningError(f"qualification evidence root must not already exist: {evidence_root}")
	source_copy = evidence_root / "source-manifest.json"
	key_copy = evidence_root / "private" / "answer-key.json"
	_write_json(source_copy, source)
	_write_json(key_copy, key)
	if _sha256(source_copy) != source_hash or _sha256(key_copy) != key_hash:
		raise ListeningError("canonical source/answer-key evidence hash is unstable")
	binding_hash = _canonical_sha256(key["qualification_binding"])
	source_binding = {
		"answer_key_sha256": key_hash,
		"pack_id": key["pack_id"],
		"qualification_binding_sha256": binding_hash,
		"source_manifest_sha256": source_hash,
	}
	records = []
	for index, session in enumerate(sessions):
		session_hash = _canonical_file_sha256(session)
		relative_path = f"{evidence_name}/sessions/{index:06d}-{session_hash}.json"
		session_copy = aggregate_output.parent.joinpath(*PurePosixPath(relative_path).parts)
		_write_json(session_copy, session)
		if _sha256(session_copy) != session_hash:
			raise ListeningError("canonical session evidence hash is unstable")
		records.append({
			"relative_path": relative_path,
			"sha256": session_hash,
			"listener_pseudonym": session["listener_id"],
			"session_id": session["session_id"],
			"session_instance_id": session["session_instance_id"],
			"source_binding": source_binding,
		})
	session_manifest = {
		"schema_version": 1,
		"canonicalization": CANONICALIZATION,
		"evidence_root": evidence_name,
		"source_manifest": {"relative_path": f"{evidence_name}/source-manifest.json", "sha256": source_hash},
		"answer_key": {"relative_path": f"{evidence_name}/private/answer-key.json", "sha256": key_hash},
		"session_root": f"{evidence_name}/sessions",
		"sessions": records,
	}
	result = {
		"schema_version": 3, "status": "passed", **summary,
		"answer_key_sha256": key_hash, "pack_id": key["pack_id"],
		"source_manifest_sha256": source_hash, "qualification_binding": key["qualification_binding"],
		"session_manifest": session_manifest,
	}
	_validate_session_manifest(session_manifest)
	_write_json(aggregate_output, result)
	return result


def validate_qualification_file(
	qualification_path: Path, expected_community_size: int | None,
) -> Mapping[str, Any]:
	qualification_path = qualification_path.resolve()
	value = _load_json(qualification_path)
	if not isinstance(value, dict) or set(value) != QUALIFICATION_KEYS:
		raise ListeningError("listening qualification has missing or unexpected keys")
	if value["schema_version"] != 3 or value["status"] != "passed":
		raise ListeningError("listening qualification must be a passing schema-v3 aggregate")
	for name in ("answer_key_sha256", "pack_id", "source_manifest_sha256"):
		if not HEX64.fullmatch(str(value[name])):
			raise ListeningError(f"listening qualification {name} is invalid")
	binding = _validate_qualification_binding(value["qualification_binding"])
	manifest = _validate_session_manifest(value["session_manifest"])
	if (manifest["source_manifest"]["sha256"] != value["source_manifest_sha256"]
		or manifest["answer_key"]["sha256"] != value["answer_key_sha256"]):
		raise ListeningError("qualification root hashes differ from its session manifest")

	base = qualification_path.parent
	evidence_root = _safe_evidence_path(base, manifest["evidence_root"], "qualification evidence_root", expect_directory=True)
	session_root = _safe_evidence_path(base, manifest["session_root"], "qualification session_root", expect_directory=True)
	source_path = _safe_evidence_path(base, manifest["source_manifest"]["relative_path"], "source manifest evidence")
	key_path = _safe_evidence_path(base, manifest["answer_key"]["relative_path"], "answer-key evidence")
	session_paths = [
		_safe_evidence_path(base, record["relative_path"], f"session evidence {index}")
		for index, record in enumerate(manifest["sessions"])
	]
	expected_files = {source_path, key_path, *session_paths}
	allowed_directories = {evidence_root, key_path.parent, session_root}
	for item in evidence_root.rglob("*"):
		if _is_reparse_point(item):
			raise ListeningError("qualification evidence tree contains a symlink/reparse point")
		resolved = item.resolve()
		if item.is_dir():
			if resolved not in allowed_directories:
				raise ListeningError(f"qualification evidence tree contains an extra directory: {item}")
		elif not item.is_file() or resolved not in expected_files:
			raise ListeningError(f"qualification evidence tree contains an extra file: {item}")
	if len(expected_files) != 2 + len(session_paths):
		raise ListeningError("qualification evidence manifest contains duplicate file references")

	source_value = _load_canonical_evidence_file(
		source_path, manifest["source_manifest"]["sha256"], "source manifest evidence",
	)
	_validate_source_manifest(source_value)
	key = _validate_answer_key(_load_canonical_evidence_file(
		key_path, manifest["answer_key"]["sha256"], "answer-key evidence",
	))
	_validate_answer_key_against_source(source_value, key)
	if key["qualification_binding"] != binding or key["pack_id"] != value["pack_id"]:
		raise ListeningError("qualification binding/pack differs from its answer key")
	key_hash = manifest["answer_key"]["sha256"]
	expected_source_binding = {
		"answer_key_sha256": key_hash,
		"pack_id": key["pack_id"],
		"qualification_binding_sha256": _canonical_sha256(binding),
		"source_manifest_sha256": manifest["source_manifest"]["sha256"],
	}
	sessions = []
	seen_session_hashes: set[str] = set()
	for index, (record, path) in enumerate(zip(manifest["sessions"], session_paths)):
		if record["source_binding"] != expected_source_binding:
			raise ListeningError(f"session evidence {index} has the wrong source binding")
		if record["sha256"] in seen_session_hashes:
			raise ListeningError("qualification evidence contains duplicate canonical session files")
		seen_session_hashes.add(record["sha256"])
		session = validate_session(
			_load_canonical_evidence_file(path, record["sha256"], f"session evidence {index}"),
			require_complete=True,
		)
		if (record["listener_pseudonym"] != session["listener_id"]
			or record["session_id"] != session["session_id"]
			or record["session_instance_id"] != session["session_instance_id"]):
			raise ListeningError(f"session evidence {index} metadata does not match its canonical file")
		_validate_session_binding(session, key, key_hash)
		sessions.append(session)
	if sessions and any(session["pairs"] != sessions[0]["pairs"] for session in sessions[1:]):
		raise ListeningError("qualification sessions contain different public pair manifests")
	recomputed = _summarize_sessions(key, sessions, expected_community_size)
	if len(manifest["sessions"]) != recomputed["session_count"]:
		raise ListeningError("qualification session manifest count is inconsistent")
	for name in DERIVED_QUALIFICATION_KEYS:
		if value[name] != recomputed[name]:
			raise ListeningError(f"qualification field {name} does not match recomputed session evidence")
	return value


def _write_tone(path: Path, amplitude: float, frequency: float = 220.0) -> None:
	samples = [amplitude * math.sin(2 * math.pi * frequency * index / 48000) for index in range(48000)]
	_write_wav(path, 48000, 1, samples)


def run_self_test() -> None:
	with tempfile.TemporaryDirectory(prefix="mumble-blind-listening-") as directory:
		root = Path(directory); sources = root / "sources"; sources.mkdir()
		_write_tone(sources / "original.wav", 0.1); _write_tone(sources / "quality.wav", 0.3)
		manifest = {
			"schema_version": 3, "session_id": "self-test",
			"qualification_binding": {
				"git_sha": "12" * 20,
				"tested_binary_sha256": "13" * 32,
				"staged_payload_sha256": "14" * 32,
				"server_binary_sha256": "15" * 32,
				"corpus_lock_sha256": "1a" * 32,
				"corpus_inventory_sha256": "16" * 32,
				"mixture_plan_sha256": "17" * 32,
				"case_set_sha256": "1b" * 32,
				"harness_sha256": "1c" * 32,
				"metrics_runtime_sha256": "1d" * 32,
				"model_manifest_sha256": "18" * 32,
				"recipe_manifest_sha256": "19" * 32,
				"recipe_set_version": "input-recipes-v2",
				"release_fixtures_sha256": "1e" * 32,
				"runner_class": "mainstream",
				"hardware_fingerprint_sha256": "1f" * 32,
				"qualification_suite": "master_quality",
				"protected_quality_qualification_sha256": "20" * 32,
			},
			"pairs": ([
				{
					"id": f"pair-{index:02d}", "cohort": "noisy",
					"left": {"label": "Original", "relative_path": "original.wav", "sha256": _sha256(sources / "original.wav")},
					"right": {"label": "Quality", "relative_path": "quality.wav", "sha256": _sha256(sources / "quality.wav")},
				}
				for index in range(12)
			] + [
				{
					"id": f"pair-{index:02d}", "cohort": "severe",
					"left": {"label": "VoiceFocus", "relative_path": "original.wav", "sha256": _sha256(sources / "original.wav")},
					"right": {"label": "Quality", "relative_path": "quality.wav", "sha256": _sha256(sources / "quality.wav")},
				}
				for index in range(12, 24)
			] + [
				{
					"id": "pair-24", "cohort": "clean",
					"left": {"label": "Original", "relative_path": "original.wav", "sha256": _sha256(sources / "original.wav")},
					"right": {"label": "Quality", "relative_path": "quality.wav", "sha256": _sha256(sources / "quality.wav")},
				},
				{
					"id": "pair-25", "cohort": "clean",
					"left": {"label": "Original", "relative_path": "original.wav", "sha256": _sha256(sources / "original.wav")},
					"right": {"label": "VoiceFocus", "relative_path": "quality.wav", "sha256": _sha256(sources / "quality.wav")},
				},
			]),
		}
		manifest_path = root / "source.json"; _write_json(manifest_path, manifest)
		out_a, out_b = root / "a", root / "b"
		session_a = generate(manifest_path, sources, out_a, "fixed-seed")
		session_b = generate(manifest_path, sources, out_b, "fixed-seed")
		if _canonical_sha256(session_a) != _canonical_sha256(session_b):
			raise AssertionError("identical listening inputs were not deterministic")
		public = (out_a / "listening-session.json").read_text(encoding="utf-8") + (out_a / "index.html").read_text(encoding="utf-8")
		if any(secret in public for secret in ("Original", "Quality", "VoiceFocus", "original.wav", "quality.wav")):
			raise AssertionError("public listening pack leaked profile labels or source names")
		if any(token in public.lower() for token in ("http://", "https://", "fetch(", "xmlhttprequest", "websocket")):
			raise AssertionError("offline listening viewer contains a network primitive")
		first = session_a["pairs"][0]
		_, _, samples_a = _read_wav(out_a / first["audio_a"]["relative_path"])
		_, _, samples_b = _read_wav(out_a / first["audio_b"]["relative_path"])
		if abs(20 * math.log10(_rms(samples_a) / _rms(samples_b))) > 0.01:
			raise AssertionError("listening pair was not loudness matched")
		bad = json.loads(json.dumps(session_a)); bad["pairs"][0]["profile"] = "Quality"
		try:
			validate_session(bad, require_complete=False)
		except ListeningError:
			pass
		else:
			raise AssertionError("session validator accepted a profile-label field")
		bad = json.loads(json.dumps(session_a)); bad["pairs"][0]["audio_a"]["sha256"] = "ff" * 32
		try:
			validate_session(bad, require_complete=False)
		except ListeningError:
			pass
		else:
			raise AssertionError("session validator accepted a public pack/hash mismatch")
		key_path = out_a / "private" / "answer-key.json"
		key = _load_json(key_path)
		key_lookup = {pair["pair_id"]: pair for pair in key["pairs"]}
		session_paths = []
		for listener_index in range(3):
			completed = json.loads(json.dumps(session_a))
			completed["listener_id"] = f"listener-{listener_index}"
			completed["session_instance_id"] = f"session-{listener_index}"
			for pair in completed["pairs"]:
				answer = key_lookup[pair["pair_id"]]
				preferred = "Quality" if answer["cohort"] == "noisy" else "VoiceFocus"
				preference = "A" if answer["profile_a"] == preferred else "B"
				completed["responses"][pair["pair_id"]] = {
					"overall_preference": preference,
					"quality_a": 4, "quality_b": 4, "noise_control_a": 4, "noise_control_b": 4,
					"intelligibility_a": 4, "intelligibility_b": 4, "artifacts_a": 4, "artifacts_b": 4,
					"artifact_tags_a": [], "artifact_tags_b": [],
				}
			path = root / f"completed-{listener_index}.json"; _write_json(path, completed); session_paths.append(path)
		aggregate_path = root / "aggregate-a" / "listening-gate.json"
		result = aggregate(manifest_path, key_path, session_paths, None, aggregate_path)
		validate_qualification_file(aggregate_path, None)

		def expect_qualification_failure(path: Path, label: str) -> None:
			try:
				validate_qualification_file(path, None)
			except ListeningError:
				return
			raise AssertionError(f"qualification verifier accepted {label}")

		if (result["schema_version"] != 3 or result["quality_noisy_preference"] != 1.0
			or result["voice_focus_severe_preference"] != 1.0
			or result["minimum_quality_noisy_pairs_per_session"] != 12
			or result["minimum_voice_focus_severe_pairs_per_session"] != 12
			or len(result["session_manifest"]["sessions"]) != 3):
			raise AssertionError("aggregate preference gates did not join concealed A/B identities")
		order_path = root / "aggregate-b" / "listening-gate.json"
		reordered = aggregate(manifest_path, key_path, list(reversed(session_paths)), None, order_path)
		if _canonical_json_bytes(result) != _canonical_json_bytes(reordered):
			raise AssertionError("session CLI ordering changed canonical qualification evidence")
		for left_record, right_record in zip(
			result["session_manifest"]["sessions"], reordered["session_manifest"]["sessions"],
		):
			left = aggregate_path.parent.joinpath(*PurePosixPath(left_record["relative_path"]).parts)
			right = order_path.parent.joinpath(*PurePosixPath(right_record["relative_path"]).parts)
			if left.read_bytes() != right.read_bytes():
				raise AssertionError("session CLI ordering changed canonical session files")
		noncanonical = root / "completed-noncanonical.json"
		noncanonical.write_text(json.dumps(_load_json(session_paths[0]), ensure_ascii=False), encoding="utf-8")
		normalized_path = root / "aggregate-c" / "listening-gate.json"
		normalized = aggregate(manifest_path, key_path, [noncanonical, *session_paths[1:]], None, normalized_path)
		if _canonical_json_bytes(result) != _canonical_json_bytes(normalized):
			raise AssertionError("non-canonical response input did not normalize deterministically")

		summary_tampered = json.loads(json.dumps(result))
		summary_tampered["quality_noisy_preference"] = 0.60
		summary_tampered_path = aggregate_path.parent / "summary-tampered.json"
		_write_json(summary_tampered_path, summary_tampered)
		expect_qualification_failure(summary_tampered_path, "a fabricated aggregate summary")

		first_record = result["session_manifest"]["sessions"][0]
		first_evidence_path = aggregate_path.parent.joinpath(*PurePosixPath(first_record["relative_path"]).parts)
		original_session_bytes = first_evidence_path.read_bytes()
		tampered_session = _load_json(first_evidence_path)
		first_response = tampered_session["responses"][next(iter(tampered_session["responses"]))]
		first_response["quality_a"] = 3 if first_response["quality_a"] != 3 else 2
		_write_json(first_evidence_path, tampered_session)
		expect_qualification_failure(aggregate_path, "a session changed without its manifest hash")
		first_evidence_path.write_bytes(original_session_bytes)

		extra_path = first_evidence_path.parent / "extra.json"
		_write_json(extra_path, {"unexpected": True})
		expect_qualification_failure(aggregate_path, "an extra evidence-tree file")
		extra_path.unlink()

		missing_path = first_evidence_path.with_suffix(".missing")
		os.replace(first_evidence_path, missing_path)
		expect_qualification_failure(aggregate_path, "a missing canonical session file")
		os.replace(missing_path, first_evidence_path)

		fabricated_root = root / "aggregate-fabricated"
		shutil.copytree(aggregate_path.parent, fabricated_root)
		fabricated_path = fabricated_root / aggregate_path.name
		fabricated = _load_json(fabricated_path)
		fabricated_key_path = fabricated_root.joinpath(
			*PurePosixPath(fabricated["session_manifest"]["answer_key"]["relative_path"]).parts
		)
		fabricated_key = _validate_answer_key(_load_json(fabricated_key_path))
		fabricated_lookup = {pair["pair_id"]: pair for pair in fabricated_key["pairs"]}
		for index, record in enumerate(fabricated["session_manifest"]["sessions"]):
			old_path = fabricated_root.joinpath(*PurePosixPath(record["relative_path"]).parts)
			session = _load_json(old_path)
			for pair_id, response in session["responses"].items():
				answer = fabricated_lookup[pair_id]
				if _answer_kind(answer) == "quality_noisy":
					response["overall_preference"] = "A" if answer["profile_a"] == "Original" else "B"
			new_hash = _canonical_file_sha256(session)
			new_relative = f"{fabricated['session_manifest']['session_root']}/{index:06d}-{new_hash}.json"
			new_path = fabricated_root.joinpath(*PurePosixPath(new_relative).parts)
			_write_json(new_path, session)
			old_path.unlink()
			record["relative_path"] = new_relative
			record["sha256"] = new_hash
		_write_json(fabricated_path, fabricated)
		expect_qualification_failure(fabricated_path, "rehashed sessions whose real preference gate fails")

		legacy_qualification = json.loads(json.dumps(result))
		legacy_qualification["schema_version"] = 2
		legacy_path = aggregate_path.parent / "legacy-v2.json"
		_write_json(legacy_path, legacy_qualification)
		expect_qualification_failure(legacy_path, "a legacy schema-v2 aggregate")
		pwsh = shutil.which("pwsh")
		assertion = Path(__file__).resolve().parent.parent / "windows" / "assert-input-enhancement-listening-qualification.ps1"
		if pwsh is not None and assertion.is_file():
			binding = manifest["qualification_binding"]
			arguments = [
				pwsh, "-NoProfile", "-File", str(assertion),
				"-ListeningQualificationPath", str(aggregate_path),
				"-PythonExecutable", sys.executable,
				"-ExpectedSourceSha", binding["git_sha"],
				"-ExpectedTestedBinarySha256", binding["tested_binary_sha256"],
				"-ExpectedStagedPayloadSha256", binding["staged_payload_sha256"],
				"-ExpectedServerBinarySha256", binding["server_binary_sha256"],
				"-ExpectedCorpusInventorySha256", binding["corpus_inventory_sha256"],
				"-ExpectedCorpusLockSha256", binding["corpus_lock_sha256"],
				"-ExpectedMixturePlanSha256", binding["mixture_plan_sha256"],
				"-ExpectedCaseSetSha256", binding["case_set_sha256"],
				"-ExpectedHarnessSha256", binding["harness_sha256"],
				"-ExpectedMetricsRuntimeSha256", binding["metrics_runtime_sha256"],
				"-ExpectedModelManifestSha256", binding["model_manifest_sha256"],
				"-ExpectedRecipeManifestSha256", binding["recipe_manifest_sha256"],
				"-ExpectedRecipeSetVersion", binding["recipe_set_version"],
				"-ExpectedReleaseFixturesSha256", binding["release_fixtures_sha256"],
				"-ExpectedRunnerClass", binding["runner_class"],
				"-ExpectedHardwareFingerprintSha256", binding["hardware_fingerprint_sha256"],
				"-ExpectedQualificationSuite", binding["qualification_suite"],
				"-ExpectedProtectedQualityQualificationSha256", binding["protected_quality_qualification_sha256"],
			]
			completed = subprocess.run(arguments, check=False, capture_output=True, text=True)
			if completed.returncode != 0:
				raise AssertionError(
					"PowerShell listening verifier rejected valid aggregate: "
					+ (completed.stderr or completed.stdout).strip()
				)
			tampered = json.loads(json.dumps(result))
			tampered["qualification_binding"]["harness_sha256"] = "ff" * 32
			tampered_path = aggregate_path.parent / "listening-gate-tampered.json"; _write_json(tampered_path, tampered)
			tampered_arguments = list(arguments)
			tampered_arguments[tampered_arguments.index("-ListeningQualificationPath") + 1] = str(tampered_path)
			if subprocess.run(tampered_arguments, check=False, capture_output=True, text=True).returncode == 0:
				raise AssertionError("PowerShell listening verifier accepted a changed protected harness identity")
		tie_session = _load_json(session_paths[0])
		for pair_id, response in tie_session["responses"].items():
			if _answer_kind(key_lookup[pair_id]) in ("quality_noisy", "voice_focus_severe"):
				response["overall_preference"] = "Tie"
		tie_session["session_instance_id"] = "tie-session"
		tie_path = root / "tie-session.json"; _write_json(tie_path, tie_session)
		try:
			aggregate(manifest_path, key_path, [tie_path, *session_paths[1:]], None, root / "tie" / "gate.json")
		except ListeningError:
			pass
		else:
			raise AssertionError("aggregate accepted a required comparison carried entirely by ties")
		legacy_manifest = json.loads(json.dumps(manifest)); legacy_manifest["schema_version"] = 2
		try:
			_validate_source_manifest(legacy_manifest)
		except ListeningError:
			pass
		else:
			raise AssertionError("legacy source schema was not rejected fail-closed")
		missing_provenance = json.loads(json.dumps(manifest))
		del missing_provenance["qualification_binding"]["metrics_runtime_sha256"]
		try:
			_validate_source_manifest(missing_provenance)
		except ListeningError:
			pass
		else:
			raise AssertionError("source manifest without protected metrics provenance was accepted")
		insufficient_comparison = json.loads(json.dumps(manifest))
		insufficient_comparison["pairs"] = [
			pair for pair in insufficient_comparison["pairs"] if pair["id"] != "pair-11"
		]
		extra_clean = json.loads(json.dumps(insufficient_comparison["pairs"][-1]))
		extra_clean["id"] = "pair-26"
		insufficient_comparison["pairs"].append(extra_clean)
		try:
			_validate_source_manifest(insufficient_comparison)
		except ListeningError:
			pass
		else:
			raise AssertionError("source manifest with only 11 noisy Quality comparisons was accepted")


def main(argv: Sequence[str] | None = None) -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--source-manifest", type=Path)
	parser.add_argument("--source-root", type=Path)
	parser.add_argument("--output-root", type=Path)
	parser.add_argument("--seed", default="mumble-blind-listening-v2")
	parser.add_argument("--validate-session", type=Path)
	parser.add_argument("--require-complete", action="store_true")
	parser.add_argument("--aggregate", action="store_true")
	parser.add_argument("--answer-key", type=Path)
	parser.add_argument("--session", action="append", type=Path, default=[])
	parser.add_argument("--expected-community-size", type=int)
	parser.add_argument("--aggregate-output", type=Path)
	parser.add_argument("--validate-qualification", type=Path)
	parser.add_argument("--self-test", action="store_true")
	args = parser.parse_args(argv)
	try:
		if args.self_test:
			run_self_test(); print("blind listening self-test: ok")
			if (args.source_manifest is None and args.validate_session is None
				and args.validate_qualification is None and not args.aggregate):
				return 0
		if args.aggregate:
			if args.source_manifest is None or args.answer_key is None or not args.session or args.aggregate_output is None:
				raise ListeningError(
					"--aggregate requires --source-manifest, --answer-key, repeated --session, and --aggregate-output"
				)
			result = aggregate(
				args.source_manifest, args.answer_key, args.session, args.expected_community_size, args.aggregate_output,
			)
			print(json.dumps(result, sort_keys=True)); return 0
		if args.validate_qualification is not None:
			validate_qualification_file(args.validate_qualification, args.expected_community_size)
			print("listening qualification: ok"); return 0
		if args.validate_session is not None:
			validate_session(_load_json(args.validate_session), require_complete=args.require_complete)
			print("listening session: ok"); return 0
		if args.source_manifest is None or args.source_root is None or args.output_root is None:
			raise ListeningError("--source-manifest, --source-root, and --output-root are required")
		result = generate(args.source_manifest, args.source_root, args.output_root, args.seed)
		print(f"blind listening: wrote {len(result['pairs'])} pair(s) to {args.output_root}")
		return 0
	except (AssertionError, ListeningError) as error:
		print(f"blind listening: error: {error}", file=sys.stderr)
		return 1


if __name__ == "__main__":
	sys.exit(main())
