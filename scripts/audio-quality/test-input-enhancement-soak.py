#!/usr/bin/env python3
"""Deterministic producer/consumer tests for realtime soak evidence."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import math
import os
import shutil
import struct
import sys
import tempfile
import wave
from pathlib import Path
from typing import Any, Mapping


SCRIPT_DIR = Path(__file__).resolve().parent


def _load(name: str, module_name: str) -> Any:
	path = SCRIPT_DIR / name
	spec = importlib.util.spec_from_file_location(module_name, path)
	if spec is None or spec.loader is None:
		raise AssertionError(f"unable to load {path}")
	module = importlib.util.module_from_spec(spec)
	spec.loader.exec_module(module)
	return module


SOAK = _load("run-input-enhancement-soak.py", "mumble_input_enhancement_soak_tested")
MEASUREMENT = _load("measurement_evidence.py", "mumble_input_enhancement_soak_measurement")


def _canonical_bytes(value: Any) -> bytes:
	return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")


def _sha256(path: Path) -> str:
	return hashlib.sha256(path.read_bytes()).hexdigest()


def _write_json(path: Path, value: Any) -> None:
	path.parent.mkdir(parents=True, exist_ok=True)
	path.write_bytes(_canonical_bytes(value) + b"\n")


def _write_source(path: Path) -> None:
	path.parent.mkdir(parents=True, exist_ok=True)
	with wave.open(str(path), "wb") as stream:
		stream.setnchannels(1)
		stream.setsampwidth(2)
		stream.setframerate(SOAK.SAMPLE_RATE_HZ)
		for sample_index in range(SOAK.FRAME_SAMPLES * 2):
			sample = int(4000 * math.sin(2.0 * math.pi * 220.0 * sample_index / SOAK.SAMPLE_RATE_HZ))
			stream.writeframesraw(struct.pack("<h", sample))


def _recipe(
	recipe_id: str, profile: str, engine: str, model_ids: list[str], latency_ms: int,
	minimum_cpu: str, noise_range: list[int], character_range: list[int],
) -> Mapping[str, Any]:
	return {
		"id": recipe_id,
		"revision": 1,
		"profile": profile,
		"engine": engine,
		"modelIds": model_ids,
		"noiseReductionRange": noise_range,
		"naturalCrispRange": character_range,
		"latencyBudgetMs": latency_ms,
		"minimumCpuClass": minimum_cpu,
		"executionSemanticsVersion": 5,
		"mixCurveVersion": 4,
		"adaptationPolicyVersion": 1,
	}


def _fake_benchmark_source() -> str:
	return r'''#!/usr/bin/env python3
import argparse,json,os,sys,time
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('--profile',required=True); p.add_argument('--noise-reduction',required=True)
p.add_argument('--natural-clear',required=True); p.add_argument('--cpu-class',required=True)
p.add_argument('--input',required=True); p.add_argument('--report',required=True)
p.add_argument('--authorized-model-sha256',required=True); p.add_argument('--authorized-model-path',required=True)
p.add_argument('--realtime-pace',action='store_true'); p.add_argument('--soak-duration-seconds',type=int,required=True)
p.add_argument('--realtime-ready-file',required=True); p.add_argument('--realtime-ready-nonce',required=True)
p.add_argument('--output'); p.add_argument('--analysis-only',action='store_true')
a=p.parse_args()
if a.output or a.analysis_only or not a.realtime_pace: sys.exit(22)
fault=os.environ.get('MUMBLE_SOAK_SELF_TEST_FAULT','')
duration=a.soak_duration_seconds
if fault!='ready-missing':
 ready={'kind':'mumble-input-enhancement-realtime-ready-v1','nonce':a.realtime_ready_nonce,'process_id':os.getpid()}
 if fault=='ready-nonce': ready['nonce']='f'*64
 if fault=='ready-pid': ready['process_id']=os.getpid()+1
 ready_path=Path(a.realtime_ready_file)
 ready_temp=ready_path.with_name(ready_path.name+'.tmp-'+str(os.getpid()))
 ready_temp.write_bytes((json.dumps(ready,sort_keys=True,separators=(',',':'))+'\n').encode('utf8'))
 os.replace(ready_temp,ready_path)
latency=1440 if a.profile=='Balanced' else 2400
frames=(duration*48000+latency)//480
engine='RNNoise' if a.profile=='Balanced' else 'DeepFilterNet'
model_id='rnnoise:embedded' if a.profile=='Balanced' else 'deepfilternet:self-test'
recipe_id={'Balanced':'input.balanced.rnnoise-self-test','Quality':'input.quality.deepfilter-self-test','VoiceFocus':'input.voice-focus.deepfilter-self-test'}[a.profile]
validated_controls={'Balanced':(69,66),'Quality':(71,78),'VoiceFocus':(91,82)}[a.profile]
callback_total=20.0
worker_total=0.0 if engine=='RNNoise' else 25.0
processing_total=callback_total+worker_total
raw={
 'processing_mode':'product-profile-realtime-soak','realtime_streaming':True,'realtime_pacing':True,
 'realtime_requested_duration_seconds':duration,'realtime_wall_ms':duration*1000.0+50.0,
 'realtime_callback_processing_total_ms':callback_total,
 'requested_profile':a.profile,'active_profile':a.profile,'active_engine':engine,
 'requested_recipe_id':recipe_id,'recipe_revision':1,
 'requested_ui_noise_reduction':70,'requested_ui_natural_clear':70,
 'validated_recipe_noise_reduction':validated_controls[0],'validated_recipe_natural_clear':validated_controls[1],
 'active_model_id':model_id,'active_model_sha256':a.authorized_model_sha256,
 'active_model_path':'' if model_id=='rnnoise:embedded' else str(Path(a.authorized_model_path).resolve()),'output_path':'',
 'sample_rate':48000,'input_sample_count':duration*48000,
 'output_sample_count':duration*48000+latency,'sample_count':duration*48000+latency,
 'reported_latency_samples':latency,'drain_sample_count':latency,'processing_padding_sample_count':0,
 'pacing_frame_count':frames,'processed_frames':frames,'neural_frames':frames,
 'worker_processing_frames':0 if engine=='RNNoise' else frames,
 'worker_processing_total_ms':worker_total,'worker_processing_p99_ms':2.0,
 'worker_processing_maximum_ms':3.0,'maximum_processing_ms':2.0,
 'processing_wall_ms':processing_total,'rtf':processing_total/(duration*1000.0),
 'callback_p99_ms':1.0,'audio_ms':duration*1000.0,'processed_audio_ms':(duration*48000+latency)/48.0,
 'used_fallback':False,'fallback_reason':'None','fallback_count':0,'deadline_misses':0,
 'pacing_deadline_miss_count':0,'pacing_max_deadline_overrun_ms':0.0,
 'pacing_late_start_count':0,'pacing_max_late_start_ms':0.0,
 'non_finite_sample_count':0,'out_of_range_sample_count':0,
 'input_saturated_sample_count':0,'saturated_sample_count':0,
}
if fault=='accelerated': raw['realtime_wall_ms']=250.0
elif fault=='fallback': raw['used_fallback']=True; raw['fallback_count']=1
elif fault=='deadline': raw['deadline_misses']=1
elif fault=='pacing-deadline': raw['pacing_deadline_miss_count']=1; raw['pacing_max_deadline_overrun_ms']=0.5
elif fault=='invalid-output': raw['non_finite_sample_count']=1
elif fault=='clipping': raw['saturated_sample_count']=1
elif fault=='tail': raw['output_sample_count']-=480; raw['sample_count']-=480
elif fault=='model': raw['active_model_sha256']='f'*64
elif fault=='model-path': raw['active_model_path']=str(Path(a.authorized_model_path).resolve()) if model_id=='rnnoise:embedded' else ''
elif fault=='recipe': raw['requested_recipe_id']='input.tampered'
elif fault=='missing-key': del raw['realtime_pacing']
elif fault=='duration': raw['realtime_requested_duration_seconds']=duration+1
elif fault=='pacing-frames': raw['pacing_frame_count']-=1
elif fault=='worker-frames': raw['worker_processing_frames']=1 if engine=='RNNoise' else frames-1
elif fault=='worker-cost': raw['processing_wall_ms']=callback_total
elif fault=='control-mapping': raw['validated_recipe_noise_reduction']+=1
elif fault=='output-mode': raw['output_path']='retained.wav'
elif fault=='analysis-mode': raw['processing_mode']='analysis-only'
Path(a.report).write_text(json.dumps(raw),encoding='utf8')
if fault=='runtime-mutation': Path.cwd().joinpath('runtime-tamper.bin').write_bytes(b'tamper')
if fault=='source-mutation': Path(a.input).open('ab').write(b'tamper')
if fault=='nonzero': sys.exit(7)
time.sleep(0.25 if fault=='accelerated' else duration+0.10)
'''


def _make_case(root: Path, fault: str = "") -> argparse.Namespace:
	runtime = root / "runtime"
	runtime.mkdir(parents=True)
	client = runtime / "mumble.exe"
	client.write_bytes(b"self-test-client")
	server = root / "mumble-server.exe"
	server.write_bytes(b"self-test-server")
	rnnoise = runtime / "rnnoise" / "rnnoise-self-test.bin"
	rnnoise.parent.mkdir(parents=True)
	rnnoise.write_bytes(b"rnnoise-self-test-model")
	deepfilter = runtime / "deepfilternet" / "deepfilter-self-test.tar.gz"
	deepfilter.parent.mkdir(parents=True)
	deepfilter.write_bytes(b"deepfilter-self-test-model")
	rn_recipes = ["input.balanced.rnnoise-self-test", "input.auto.balanced.rnnoise-self-test"]
	df_recipes = [
		"input.quality.deepfilter-self-test", "input.voice-focus.deepfilter-self-test",
		"input.auto.quality.deepfilter-self-test",
	]
	model_manifest = runtime / "input-models.json"
	_write_json(model_manifest, {
		"schemaVersion": 1, "catalogRevision": "soak-self-test-v2", "generatedFromAssets": True,
		"models": [
			{
				"id": "rnnoise:embedded", "version": "1", "backend": "RNNoise",
				"path": rnnoise.relative_to(runtime).as_posix(), "sha256": _sha256(rnnoise),
				"size": rnnoise.stat().st_size, "licenseSpdx": "BSD-3-Clause", "sampleRateHz": 48000,
				"algorithmicLatencyMs": 30, "recipeCompatibility": rn_recipes,
			},
			{
				"id": "deepfilternet:self-test", "version": "1", "backend": "DeepFilterNet",
				"path": deepfilter.relative_to(runtime).as_posix(), "sha256": _sha256(deepfilter),
				"size": deepfilter.stat().st_size, "licenseSpdx": "MIT", "sampleRateHz": 48000,
				"algorithmicLatencyMs": 30, "recipeCompatibility": df_recipes,
			},
		],
	})
	recipe_manifest = runtime / "input-recipes.json"
	_write_json(recipe_manifest, {
		"schemaVersion": 2, "catalogRevision": "soak-self-test-v2",
		"modelManifestSha256": _sha256(model_manifest),
		"recipes": [
			_recipe("input.original", "Original", "None", [], 0, "Low", [0, 0], [0, 0]),
			_recipe("input.light.speex", "Light", "Speex", [], 10, "Low", [0, 100], [0, 100]),
			_recipe(rn_recipes[0], "Balanced", "RNNoise", ["rnnoise:embedded"], 30, "Standard", [20, 90], [10, 90]),
			_recipe(df_recipes[0], "Quality", "DeepFilterNet", ["deepfilternet:self-test"], 50, "High", [25, 90], [25, 100]),
			_recipe(df_recipes[1], "VoiceFocus", "DeepFilterNet", ["deepfilternet:self-test"], 50, "High", [70, 100], [40, 100]),
			_recipe("input.auto.light.speex", "Auto", "Speex", [], 10, "Low", [0, 100], [0, 100]),
			_recipe(rn_recipes[1], "Auto", "RNNoise", ["rnnoise:embedded"], 30, "Standard", [20, 90], [10, 90]),
			_recipe(df_recipes[2], "Auto", "DeepFilterNet", ["deepfilternet:self-test"], 50, "High", [25, 90], [25, 100]),
		],
	})
	benchmark = root / "fake-speech-cleanup-benchmark.py"
	benchmark.write_text(_fake_benchmark_source(), encoding="utf-8")
	provenance = root / "run-provenance.json"
	_write_json(provenance, {"build_sha": "a" * 40, "kind": "soak-self-test"})
	source = root / "short-source.wav"
	_write_source(source)
	return argparse.Namespace(
		benchmark=benchmark, runtime_root=runtime, client=client, server=server,
		run_provenance=provenance, model_manifest=None, recipe_manifest=None,
		source_wav=source, output_root=root / "artifacts" / "nightly-low-performance" / "soak",
		measurement_fragment=root / "artifacts" / "nightly-low-performance" / "soak-fragment.json",
		duration_seconds=1, _self_test_fault=fault,
	)


def _stable_rss(_: int) -> int:
	return 100_000


class _GrowingRss:
	def __init__(self) -> None:
		self.value = 100_000

	def __call__(self, _: int) -> int:
		self.value += 2 * 1024 * 1024
		return self.value


def _assert_consumer_accepts(root: Path, args: argparse.Namespace, reports: list[Mapping[str, Any]]) -> None:
	prefix = "artifacts/nightly-low-performance/"
	filenames = ("01-balanced.json", "02-quality.json", "03-voice-focus.json")
	entries = []
	for profile, filename in zip(SOAK.PROFILES, filenames, strict=True):
		path = args.output_root / filename
		payload = path.read_bytes()
		entries.append({
			"profile": profile,
			"report": {
				"contains_audio_samples": False,
				"path": path.relative_to(root).as_posix(),
				"sha256": hashlib.sha256(payload).hexdigest(), "size_bytes": len(payload),
			},
		})
	identity = reports[0]["execution_identity"]
	build = {
		"tested_binary_sha256": identity["client_binary_sha256"],
		"server_binary_sha256": identity["server_binary_sha256"],
		"model_manifest_sha256": identity["model_manifest_sha256"],
		"recipe_manifest_sha256": identity["recipe_manifest_sha256"],
		"staged_payload_sha256": identity["runtime_payload_sha256"],
		"model_hashes": sorted({model["sha256"] for report in reports for binding in report["active_bindings"] for model in binding["models"]}),
	}
	profile_bindings = {report["profile"]: report["active_bindings"] for report in reports}
	derived = {}
	for profile in SOAK.PROFILES:
		derived[(profile, "case-001")] = {
			"algorithmic_latency_ms": 30.0 if profile == "Balanced" else 50.0,
			"speech_edge_loss_ms": 0.0,
			"counters": MEASUREMENT._runtime_counters(),
			"performance": {
				"audio_duration_seconds": 0.0, "processing_duration_seconds": 0.0,
				"callback_durations_ms": [], "worker_durations_ms": [],
				"max_internal_processing_ms": 0.0, "memory_growth_bytes": 0,
				"soak_duration_seconds": 0,
			},
		}
	MEASUREMENT._apply_soak_reports(
		root, prefix, entries, "nightly", "core", build, profile_bindings, derived, {},
	)
	for profile in SOAK.PROFILES:
		if derived[(profile, "case-001")]["performance"]["soak_duration_seconds"] < 1:
			raise AssertionError(f"consumer lost {profile} realtime wall duration")


def main() -> int:
	if SOAK._resident_set_size(os.getpid()) <= 0:
		raise AssertionError("platform RSS sampler returned no resident memory")
	SOAK._validate_memory_growth(1, 24 * 1024, "short-smoke-self-test")
	SOAK._validate_memory_growth(SOAK.QUALIFICATION_DURATION_SECONDS, 0, "qualification-self-test")
	for duration, growth in (
		(1, SOAK.SHORT_SMOKE_MAX_RSS_GROWTH_BYTES + 1),
		(SOAK.QUALIFICATION_DURATION_SECONDS, 1),
	):
		try:
			SOAK._validate_memory_growth(duration, growth, "growth-self-test")
		except SOAK.SoakError:
			pass
		else:
			raise AssertionError(f"RSS growth {growth} was accepted for duration {duration}")
	schema = json.loads((SCRIPT_DIR / "input-enhancement-soak-report.schema.json").read_text(encoding="utf-8"))
	if "Light" not in schema["$defs"]["profileBinding"]["properties"]["profile"]["enum"]:
		raise AssertionError("soak schema cannot represent the Light member of an Auto recipe set")

	with tempfile.TemporaryDirectory(prefix="mumble-input-enhancement-soak-self-test-") as temporary_text:
		temporary_root = Path(temporary_text).resolve()
		try:
			temporary_root.relative_to(Path(tempfile.gettempdir()).resolve())
		except ValueError as error:
			raise AssertionError("self-test temp root escaped the operating-system temp directory") from error
		early_marker = temporary_root / "preexisting-early-ready.json"
		_write_json(early_marker, {"kind": SOAK.READY_KIND, "nonce": "a" * 64, "process_id": 1})
		try:
			SOAK._monitor_process(
				[sys.executable, "-c", "raise SystemExit(99)"], cwd=temporary_root,
				stdout_path=temporary_root / "early.stdout", stderr_path=temporary_root / "early.stderr",
				duration_seconds=1, environment=os.environ, rss_reader=_stable_rss,
				ready_path=early_marker, ready_nonce="a" * 64,
			)
		except SOAK.SoakError as error:
			if "already exists before process start" not in str(error):
				raise AssertionError("preexisting ready marker failed for the wrong reason") from error
		else:
			raise AssertionError("preexisting/early ready marker was accepted")
		happy_root = temporary_root / "happy"
		happy_args = _make_case(happy_root)
		reports = SOAK.run_campaign(happy_args, allow_fake_tools=True, rss_reader=_stable_rss)
		if [report["profile"] for report in reports] != list(SOAK.PROFILES):
			raise AssertionError("producer lost required profile order")
		_assert_consumer_accepts(happy_root, happy_args, reports)
		fragment = json.loads(happy_args.measurement_fragment.read_text(encoding="utf-8"))
		if [entry["profile"] for entry in fragment["soak_reports"]] != list(SOAK.PROFILES):
			raise AssertionError("measurement fragment lost required profile order")

		faults = (
			"accelerated", "fallback", "deadline", "pacing-deadline", "invalid-output", "clipping",
			"tail", "model", "model-path", "recipe", "missing-key", "duration", "pacing-frames", "worker-frames",
			"worker-cost", "control-mapping", "output-mode", "analysis-mode", "runtime-mutation", "source-mutation",
			"ready-missing", "ready-nonce", "ready-pid", "nonzero",
		)
		for fault in faults:
			case_root = temporary_root / f"fault-{fault}"
			args = _make_case(case_root, fault)
			try:
				SOAK.run_campaign(args, allow_fake_tools=True, rss_reader=_stable_rss)
			except SOAK.SoakError:
				pass
			else:
				raise AssertionError(f"producer accepted injected fault {fault!r}")
			if args.output_root.exists() or args.measurement_fragment.exists():
				raise AssertionError(f"fault {fault!r} published partial evidence")

		rss_root = temporary_root / "fault-rss-growth"
		rss_args = _make_case(rss_root)
		try:
			SOAK.run_campaign(rss_args, allow_fake_tools=True, rss_reader=_GrowingRss())
		except SOAK.SoakError:
			pass
		else:
			raise AssertionError("producer accepted growing post-warmup RSS")
		if rss_args.output_root.exists() or rss_args.measurement_fragment.exists():
			raise AssertionError("RSS-growth fault published partial evidence")

	print("input enhancement soak self-test: ok")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
