# Input-enhancement quality tools

These tracked, dependency-free tools make corpus acquisition and quality-run
inputs reproducible without checking licensed audio into Git. Audio archives,
extracted audio, temporary mixtures, and per-run captures belong outside the
repository or below the ignored `.tmp/` directory.

## Corpus acquisition

`corpus-lock.json` is the policy and integrity source of truth. List the current
decision for each source before downloading anything:

```powershell
python scripts/audio-quality/fetch-corpus.py --list
```

Fetch all reviewed local-evaluation archives into ignored storage:

```powershell
python scripts/audio-quality/fetch-corpus.py `
  --all `
  --purpose local-eval `
  --artifact-root .tmp/audio-quality-corpus
```

The fetcher refuses ambiguous, restricted, non-commercial, evaluation-only
training, and unpinned downloads. Existing archives are accepted only after
their locked byte size and SHA-256 both match. The selected set now includes
OpenSLR SLR28, a 1,311,166,223-byte Apache-2.0 archive with real isotropic and
point-source noise plus real/simulated RIRs. The fetcher does not extract
archives. Its schema-v3 `corpus-state.json` records the canonical lock hash,
fetcher hash, URL hash, and each `source_artifact_sha256`. Re-fetch or verify
the corpus after changing the lock; older state files are not release evidence.

## Deterministic mixture plans

`corpus-inventory-v3.schema.json` defines the local inventory contract. Every
WAV has extracted-file and source-archive hashes plus derivation provenance.
Speech additionally binds its UTF-8 transcript; RIR and microphone responses
are distinct asset kinds rather than generated labels:

```json
{
  "schema_version": 3,
  "inventory_id": "windows-community-v1",
  "eligibility": "release",
  "corpus_lock_sha256": "<canonical lock SHA-256>",
  "provenance": {
    "generator": "local-inventory-builder",
    "generator_version": "1",
    "generated_from_state_sha256": "<corpus-state.json SHA-256>",
    "transformation_manifest_sha256": "<conversion manifest SHA-256>"
  },
  "items": [
    {
      "id": "speech-0001",
      "kind": "speech",
      "source_id": "mcgill-tsp-speech-v2-48k",
      "relative_path": "extracted/tsp/speech-0001.wav",
      "group_id": "speaker-001",
      "speaker_id": "speaker-001",
      "language": "en-US",
      "transcript": {
        "status": "verified",
        "relative_path": "transcripts/speech-0001.txt",
        "sha256": "<transcript byte SHA-256>",
        "size_bytes": 42,
        "normalization": "exact-utf8"
      },
      "sample_rate_hz": 48000,
      "channels": 1,
      "duration_samples": 576000,
      "size_bytes": 1152044,
      "sha256": "<SHA-256 of this extracted/converted WAV>",
      "source_artifact_sha256": "9cfb3a3a13014c8ff90770a5d1923f376da73ac927b9100f09826f60cf06cf43",
      "provenance": {
        "derivation": "extracted",
        "parent_sha256": "<archive member SHA-256>",
        "parameters_sha256": "<canonical conversion parameters SHA-256>",
        "source_path": "TSP/.../speech-0001.wav",
        "tool": "ffmpeg",
        "tool_version": "8.0"
      }
    },
    {
      "id": "noise-0001",
      "kind": "noise",
      "source_id": "openslr28-rirs-noises",
      "relative_path": "extracted/openslr28/real-noise-0001.wav",
      "group_id": "openslr28-real-noise-0001",
      "noise_class": "hvac",
      "sample_rate_hz": 16000,
      "channels": 8,
      "duration_samples": 480000,
      "size_bytes": 7680044,
      "sha256": "<SHA-256 of this extracted WAV>",
      "source_artifact_sha256": "3b50cfde915b3984738169b4beb341e9f6b8062ae4c2076146c5db71c2c05dc7",
      "provenance": { "derivation": "extracted", "parent_sha256": "<SHA-256>", "parameters_sha256": "<SHA-256>", "source_path": "RIRS_NOISES/...wav", "tool": "inventory-builder", "tool_version": "1" }
    }
  ]
}
```

The omitted `rir` and `microphone_response` items follow the checked-in schema
and carry measured/simulated or measured/modeled metadata. Release validation
requires all four kinds and verified transcript hashes. Stable group IDs for
all four kinds are assigned disjointly to tuning, validation, or holdout by
SHA-256. The renderer verifies and applies the locked response WAVs using a
bounded deterministic sparse-impulse transform.

Migrate an old schema-v2 inventory only as an explicit draft. Migration never
invents transcript hashes or response assets, so the result remains ineligible
until curated:

```powershell
python scripts/audio-quality/corpus-inventory-v3.py `
  --migrate-v2 .tmp/audio-quality-corpus/inventory-v2.json `
  --corpus-state .tmp/audio-quality-corpus/corpus-state.json `
  --output .tmp/audio-quality-corpus/inventory-v3-draft.json

python scripts/audio-quality/corpus-inventory-v3.py `
  --validate .tmp/audio-quality-corpus/inventory-v3.json `
  --require-release-eligible
```

```powershell
python scripts/audio-quality/generate-mixture-plan.py `
  --inventory .tmp/audio-quality-corpus/inventory.json `
  --suite pr_smoke `
  --split validation `
  --seed mumble-input-enhancement-v1 `
  --output .tmp/audio-quality-runs/pr-smoke-plan.json
```

Render the plan into private, deterministic PCM inputs and clean references:

```powershell
python scripts/audio-quality/render-mixture-plan.py `
  --plan .tmp/audio-quality-runs/pr-smoke-plan.json `
  --corpus-root .tmp/audio-quality-corpus `
  --output-root .tmp/audio-quality-runs/pr-smoke-audio
```

The renderer accepts only uncompressed PCM WAV sources beneath `--corpus-root`,
checks inventory metadata against each file, downmixes/resamples locally and
applies the plan's SNR, room, distance, microphone-response, gain and clipping
transforms. Its manifest contains source/output SHA-256 values; the WAV files
remain private local evidence and must never be placed in a CI upload artifact.
Compressed corpus assets must first be converted into a locked local inventory;
that conversion is intentionally outside the renderer's trust boundary.

Run offline profile qualification through the same versioned product recipe
and `InputEnhancement::Pipeline` used by capture. The older `--backend` form is
retained only for explicit Expert/compatibility comparisons:

```powershell
.\build-input-enhancement\speech_cleanup_benchmark.exe `
  --profile Balanced `
  --noise-reduction 50 `
  --natural-clear 50 `
  --cpu-class Standard `
  --input .tmp/audio-quality-runs/pr-smoke-audio/<case>/client1-input.wav `
  --clean-reference .tmp/audio-quality-runs/pr-smoke-audio/<case>/clean-reference.wav `
  --output .tmp/audio-quality-runs/<run>/<case>/balanced.wav `
  --report .tmp/audio-quality-runs/<run>/<case>/balanced.json
```

The report attests requested/active profile, recipe revision, engine, model ID
and optional verified model SHA-256, causal latency/drain, fallback/deadline
counters, and callback p50/p95/p99. Product mode rejects `--mix-factor` and
direct backend/model overrides so tuning cannot accidentally qualify an
untested recipe. `--authorized-model-sha256` and `--authorized-model-path`
must be supplied together by the protected harness after it has verified the
signed package catalog. The pipeline then binds the hash to the exact asset
path reported by the initialized processor before publication.

After client 2 has captured the decoded signal, score the attested latency and
tail without permitting a correlation-based time shift:

```powershell
python scripts/audio-quality/score-fixed-timeline.py `
  --reference .tmp/audio-quality-runs/pr-smoke-audio/<case>/clean-reference.wav `
  --received .tmp/audio-quality-runs/<run>/<case>/client2-received.wav `
  --latency-samples 1440 `
  --require-complete-tail `
  --fail-on-new-clipping `
  --output .tmp/audio-quality-runs/<run>/<case>/fixed-timeline-score.json
```

This scorer reports loudness-matched fixed-timeline SDR, onset/end loss, tail
loss and clipping. It deliberately performs no temporal search. DNSMOS, eSTOI
and WER stay in the protected quality harness and are joined to this evidence
by case ID.

The localhost Mumble receive path has a small startup delay from the unchanged
Opus/jitter path, and that delay is not an input-enhancement latency. For the
packaged client-1/server/client-2 gates, first capture a paired `Original`
control with the same fixture, bitrate, packet size, transmit mode and pre-roll,
and the exact same packaged client bytes, then pass it as
`--transport-baseline`. The control is first scored without trust; that
self/unqualified pass may remove at most one 10 ms frame and must independently
meet the onset/end, complete-tail and clipping gates. Only after the protected
harness has attested that result, the Original voice contract, the pairing and
the control bytes may it add `--qualified-transport-baseline`. That second
paired score removes the control's complete positive, frame-aligned OG startup
offset and measures only delay added by enhancement. It performs no
correlation, shift, trim or post-padding. End loss and tail completeness remain
on the absolute declared enhancement timeline, and a target that starts more
than one frame later than `Original + declared enhancement latency` still
fails.

```powershell
python scripts/audio-quality/score-fixed-timeline.py `
  --reference .tmp/audio-quality-runs/case/clean-reference.wav `
  --received .tmp/audio-quality-runs/case/quality-received.wav `
  --latency-samples 2400 `
  --transport-baseline .tmp/audio-quality-runs/case/original-received.wav `
  --transport-baseline-latency-samples 0 `
  --qualified-transport-baseline `
  --max-edge-loss-samples 480 --require-complete-tail --fail-on-new-clipping `
  --output .tmp/audio-quality-runs/case/quality-fixed-timeline.json
```

The resulting attestation uses `timeline_alignment =
fixed-paired-original-onset` and records the baseline WAV hash, declared
baseline latency, qualification mode, raw onset offset and applied adjustment.

## RNNoise model campaign

`freeze-rnnoise-training-plan.py` seals model training without running a large
job. It requires a release-eligible schema-v3 inventory plus pre-frozen,
disjoint tuning, validation and protected-holdout mixture plans made with the
same split seed. It verifies every pinned trainer/config file below the
supplied toolchain root, and binds the raw and canonical SHA-256 values of the
corpus lock, inventory, all three mixtures and toolchain. At least five unique uint64 seeds and
manifest-relative model output paths are derived deterministically from that
fingerprint:

```powershell
python scripts/audio-quality/freeze-rnnoise-training-plan.py `
  --corpus-lock scripts/audio-quality/corpus-lock.json `
  --inventory C:/protected-audio/training/inventory-v3.json `
  --tuning-mixture-plan C:/protected-audio/training/tuning-plan.json `
  --validation-mixture-plan C:/protected-audio/training/validation-plan.json `
  --holdout-mixture-plan C:/protected-audio/holdout/plan.json `
  --toolchain-manifest C:/protected-audio/rnnoise/toolchain.json `
  --toolchain-root C:/protected-audio/rnnoise/toolchain `
  --campaign-id balanced-2026-01 `
  --seed-root balanced-domain-v1 `
  --seed-count 5 `
  --output C:/protected-audio/rnnoise/balanced-2026-01/training-plan.json
```

The toolchain manifest is schema v1. Its `files` array is sorted by
`relative_path` and pins `relative_path`, `role`, `sha256`, and `size_bytes`.
It must contain exactly one `attribution-notice`, plus `trainer` and
`training-config` roles. `output_model` names and hashes that notice and pins a
reviewed output-model SPDX expression; the tool will not assume that the
RNNoise source-code license automatically applies to newly trained weights.
For example:

```json
{
  "schema_version": 1,
  "toolchain_id": "rnnoise-pytorch-locked-v1",
  "runtime": { "name": "protected-trainer", "version": "1", "revision": "<commit>" },
  "output_model": {
    "license_spdx": "<reviewed SPDX expression>",
    "attribution_file_relative_path": "ATTRIBUTION.txt",
    "attribution_sha256": "<SHA-256>"
  },
  "files": [
    { "role": "attribution-notice", "relative_path": "ATTRIBUTION.txt", "sha256": "<SHA-256>", "size_bytes": 123 },
    { "role": "trainer", "relative_path": "bin/train-rnnoise.exe", "sha256": "<SHA-256>", "size_bytes": 456 },
    { "role": "training-config", "relative_path": "config/domain-v1.json", "sha256": "<SHA-256>", "size_bytes": 789 }
  ]
}
```

Every source actually referenced as speech, noise, RIR or microphone response
must have a verified license, `training_status = allowed_with_attribution` and
the `training_candidate` role in the exact locked corpus. The freezer also
requires at least one noise asset; a clean-only plan is rejected. The current
checked-in lock intentionally has no training-approved noise source, so it
cannot yet produce a product-training campaign. SLR28 remains evaluation-only.
Approve and pin a genuinely training-licensed noise source before starting a
campaign; do not loosen the gate to reuse evaluation material.

After the protected trainer produces all frozen model paths, validation and
holdout are separate commands. Validation must contain every seed candidate;
only candidates with clean init/hash/fallback counters enter the deterministic
OVRL/BAK/SIG/eSTOI/WER ranking:

```powershell
python scripts/audio-quality/select-rnnoise-model.py select-validation `
  --training-plan C:/protected-audio/rnnoise/balanced-2026-01/training-plan.json `
  --validation-results C:/protected-audio/rnnoise/balanced-2026-01/validation.json `
  --model-root C:/protected-audio/runtime-payload `
  --output C:/protected-audio/rnnoise/balanced-2026-01/validation-selection.json
```

The sealed candidate is then evaluated once against a hash-pinned embedded
RNNoise reference. `open-holdout` refuses an existing receipt or output
directory and never re-ranks candidates from holdout results:

```powershell
python scripts/audio-quality/select-rnnoise-model.py open-holdout `
  --training-plan C:/protected-audio/rnnoise/balanced-2026-01/training-plan.json `
  --selection C:/protected-audio/rnnoise/balanced-2026-01/validation-selection.json `
  --holdout-results C:/protected-audio/holdout/balanced-2026-01.json `
  --model-root C:/protected-audio/runtime-payload `
  --embedded-reference-path C:/protected-audio/legacy/rnnoise-embedded-reference.bin `
  --receipt C:/protected-audio/holdout/receipts/balanced-2026-01.json `
  --output-dir C:/protected-audio/rnnoise/balanced-2026-01/decision
```

Validation and holdout result envelopes must repeat the raw SHA-256 of their
pre-frozen plan. The campaign also commits the sorted case IDs and case count;
validation candidates must cover that count and the paired holdout rows must
match that ID set exactly. A result from a regenerated, partial or retuned
split is rejected.
Custom selection requires every clean-speech, invalid-output, hash, fallback,
tail, catastrophe, Balanced RTF/callback and 3,600-second soak gate to pass,
plus a strictly positive deterministic paired-bootstrap 95% lower bound for
holdout OVRL improvement over embedded. Otherwise the decision is
`embedded-retained`, which is a valid non-blocking campaign result. A winning
campaign emits JSON/Markdown model cards and an
`input-models.rnnoise-custom.fragment.json`; its model path and SHA-256 were
verified against the exact runtime payload. Audio and protected holdout
results remain local and must not be uploaded.

Before a completed campaign can be referenced by rollout evidence, copy the
one-shot `decision/decision.json` bytes to the stable release-evidence name
`rnnoise-selection-decision.json` and sign those exact bytes with the release
Ed25519 identity:

```powershell
Copy-Item C:/protected-audio/rnnoise/balanced-2026-01/decision/decision.json `
  C:/protected-audio/release-evidence/rnnoise-selection-decision.json
scripts/windows/protect-input-enhancement-json.ps1 `
  -InputPath C:/protected-audio/release-evidence/rnnoise-selection-decision.json `
  -SignaturePath C:/protected-audio/release-evidence/rnnoise-selection-decision.json.sig `
  -PrivateKeyBase64 $env:INPUT_ENHANCEMENT_ED25519_PRIVATE_KEY_BASE64 `
  -ExpectedPublicKeyHex $env:INPUT_ENHANCEMENT_ED25519_PUBLIC_KEY_HEX
scripts/windows/assert-input-enhancement-rnnoise-selection-decision.ps1 `
  -DecisionPath C:/protected-audio/release-evidence/rnnoise-selection-decision.json `
  -DecisionSignaturePath C:/protected-audio/release-evidence/rnnoise-selection-decision.json.sig `
  -PublicKeyHex $env:INPUT_ENHANCEMENT_ED25519_PUBLIC_KEY_HEX
```

The rollout generator accepts only that verified decision/signature pair. It
maps `embedded-retained` to the non-blocking retained outcome and
`custom-selected` to the promoted outcome; operators cannot enter campaign
status or outcome manually. Omitting the pair leaves the track pending.

## Tracked two-client E2E core

`run-two-client-e2e.py` owns the portable client 1 → localhost server → client
2 contract. It verifies the release-eligible inventory and plan, rendered
inputs, full staged runtime tree, client/server bytes, model/recipe manifests,
every manifest-listed model asset, metric models, adapter, and itself before a
process starts. The schema-v3 contract then requires the protected adapter to
independently hash the actual paths it is about to launch. After the adapter
returns, the tracked core hashes the contract, provenance and runtime again.
Copying the contract's expected hashes into the result is not an
implementation: the adapter must derive them from the actual client image,
server image, complete runtime tree, and model/recipe manifest bytes immediately
before process launch.

Every case gets two distinct Original runs: a clean route control that alone
may anchor fixed speech-edge/tail scoring, and a noisy Original comparison
retained for OVRL/BAK/SIG/eSTOI/WER comparison. Enhanced cases then run the
candidate on the same noisy mixture. Leading room noise can therefore never
qualify a broken transport anchor by crossing the speech threshold before the
utterance starts.

Only machine operations remain local. A runner-local adapter receives
`--contract <json> --result <json>` and starts the two clients/server, streams
the WAV at real callback cadence, drains the causal tail, and writes the strict
audio-free result attestation. Use `--emit-contracts-only` when developing or
auditing a local adapter:

```powershell
python scripts/audio-quality/run-two-client-e2e.py `
  --plan .tmp/audio-quality-runs/validation-plan.json `
  --case-id master_quality-validation-00001 `
  --render-manifest .tmp/audio-quality-runs/audio/render-manifest.json `
  --render-root .tmp/audio-quality-runs/audio `
  --inventory .tmp/audio-quality-corpus/inventory-v3.json `
  --runtime-root build-input-enhancement `
  --client-binary build-input-enhancement/shared-webengine-stage/mumble.exe `
  --server-binary build-input-enhancement/mumble-server.exe `
  --model-manifest build-input-enhancement/shared-webengine-stage/input-models.json `
  --recipe-manifest build-input-enhancement/shared-webengine-stage/input-recipes.json `
  --metrics-manifest C:/protected-audio-harness/metrics-manifest.json `
  --adapter scripts/local/run-two-client-e2e-adapter.py `
  --adapter-arg=--client-build-dir `
  --adapter-arg=build-input-enhancement `
  --output-root .tmp/audio-quality-runs/e2e-00001
```

Adapter contract/result schema v2 is rejected. A v3 result has exactly these
top-level fields:

```json
{
  "schema_version": 3,
  "status": "passed",
  "role": "candidate",
  "profile": "Quality",
  "receiver_cleanup": false,
  "input_sha256": "<contract input SHA-256>",
  "transport": {
    "opus_bitrate_bps": 64000,
    "frames_per_packet": 1,
    "transmit_mode": "Continuous"
  },
  "capture": {
    "relative_path": "capture.wav",
    "sha256": "<capture SHA-256>",
    "size_bytes": 1234
  },
  "execution_identity": {
    "contract_file_sha256": "<actual contract-file SHA-256>",
    "run_provenance_sha256": "<canonical provenance SHA-256>",
    "runtime_payload_sha256": "<canonical complete-tree SHA-256>",
    "client_binary_sha256": "<actual launched client SHA-256>",
    "server_binary_sha256": "<actual launched server SHA-256>",
    "model_manifest_sha256": "<actual model-manifest SHA-256>",
    "recipe_manifest_sha256": "<actual recipe-manifest SHA-256>"
  },
  "diagnostics": {
    "active_profile": "Quality",
    "active_engine": "DeepFilterNet",
    "active_recipe": {
      "catalog_revision": "input-recipes-v2",
      "id": "input.quality.deepfilternet-balanced",
      "manifest_sha256": "<recipe-manifest SHA-256>",
      "revision": 1
    },
    "active_models": [
      { "id": "deepfilternet:balanced", "sha256": "<model SHA-256>", "version": "<version>" }
    ],
    "callback_frame_count": 100,
    "callback_p99_ms": 4.5,
    "worker_frame_count": 100,
    "worker_p99_ms": 7.0,
    "mean_rtf": 0.25,
    "deadline_miss_count": 0,
    "fallback_count": 0,
    "invalid_output_count": 0,
    "declared_latency_samples": 1920,
    "tail_drained": true
  }
}
```

The live client diagnostics must supply the active profile, engine, recipe ID
and revision, model ID/hash, frame counts and timings. The adapter may enrich
those observations with the version and catalog hashes from the independently
verified runtime manifests. Active models are sorted by ID; non-neural recipes
use an empty array. DeepFilterNet must report worker frames. A zero worker-frame
count requires `worker_p99_ms = 0`. Original/Light/Balanced fail above 5 ms p99
or 0.15 mean RTF; Quality/Voice Focus fail above 8 ms p99 or 0.35 mean RTF.
Deadline misses, fallbacks, invalid output, an unauthorized active binding, or
any identity mismatch fail the case. The final `e2e-manifest.json` preserves the
contract/result/capture hashes, execution identity, active binding, and measured
callback/worker evidence for downstream qualification.

The ignored `scripts/local` adapter is deliberately not qualification logic.
Changing it changes the attested adapter hash and the protected runner must
approve that exact hash. Existing local adapters must be upgraded to emit the
schema-v3 result above before they can run qualification again.

## Offline blind A/B listening

`blind-listening.py` takes a hash-bound pair manifest, attenuates both PCM WAVs
to the quieter integrated RMS, deterministically randomizes pair order and A/B
orientation, and writes a static viewer. `listening-session.json`, `index.html`,
and audio paths contain opaque IDs only. Profile identities stay in the private
schema-v3 `private/answer-key.json`, whose source rows also hash each exact
public pair manifest; it is never given to listeners. The viewer has a
strict CSP, uses no server/network API, and exports responses as a local
`listening-session.json` download.

```powershell
python scripts/audio-quality/blind-listening.py `
  --source-manifest .tmp/listening/source-pairs.json `
  --source-root .tmp/audio-quality-runs/listening-audio `
  --output-root .tmp/listening/pack `
  --seed community-round-1
```

The source manifest uses schema v3 and binds the pack to the exact Git SHA,
tested executable, staged payload, OG server, corpus lock and inventory,
mixture plan, protected case set, trusted harness, pinned metrics runtime,
release fixtures, model/recipe manifests and recipe-set version. It also binds
the exact protected quality-qualification JSON, its suite, runner class and
privacy-safe hardware fingerprint. A local-development runner identity is not
accepted for release listening evidence. Each source pair declares `id`,
`cohort` (`clean`, `noisy`, or `severe`) and two
`{label,relative_path,sha256}` entries. Labels are limited to the five core
profiles and are used only in the answer key. Each public session carries only
the SHA-256 of that qualification binding. Qualification schema v3 restores the
full binding and writes a sibling `<aggregate-name>.evidence/` tree containing
canonical source, private answer-key and completed-session JSON. Its sorted
session hash list records each listener pseudonym, session ID, session-instance
ID and exact source/key/pack binding. The public response/pack contract remains
schema v2; schema-v1 sessions, source-v2 manifests and qualification-v2 summary
files are rejected fail-closed.
The machine-readable contracts live in `blind-listening-source.schema.json`,
`blind-listening-session.schema.json`, and
`blind-listening-qualification.schema.json`. Validate exported sessions and
run the aggregate acceptance gate locally:

```powershell
python scripts/audio-quality/blind-listening.py `
  --validate-session .tmp/listening/responses/alice-1.json --require-complete

python scripts/audio-quality/blind-listening.py --aggregate `
  --source-manifest .tmp/listening/source-pairs.json `
  --answer-key .tmp/listening/pack/private/answer-key.json `
  --session .tmp/listening/responses/alice-1.json `
  --session .tmp/listening/responses/bob-1.json `
  --session .tmp/listening/responses/carol-1.json `
  --aggregate-output .tmp/listening/listening-gate.json
```

Every session must answer at least 26 distinct randomized pairs: at least 12
noisy Quality-vs-Original comparisons, 12 severe VoiceFocus-vs-Quality
comparisons, and two clean pairs that cover both Quality and VoiceFocus. A/B
placement is deterministically randomized and balanced in the two required
comparison cohorts. Each required comparison needs at least eight decisive
non-tie votes per session; ties are retained in evidence but cannot carry the
gate. Aggregation normally requires three distinct listeners. For a declared
one- or two-person community, pass
`--expected-community-size 1|2`; every person must then submit two sessions.
The gate requires Quality over Original in at least 60% of non-tie noisy votes,
VoiceFocus over Quality in at least 60% of non-tie severe votes, no lower
VoiceFocus median intelligibility, and no identical clean-speech artifact tag
reported by two distinct listeners. Before rehearsal, validate the aggregate
with `scripts/windows/assert-input-enhancement-listening-qualification.ps1`;
that assertion resolves only regular, non-reparse files below the aggregate,
verifies the canonical source/key/session hashes, rejects missing or extra tree
entries, rejoins every concealed response to the source and answer key, and
recomputes counts, preferences, intelligibility medians and recurring-artifact
gates. It then requires the qualification binding to match the exact candidate,
corpus, trusted harness, metrics runtime, protected runner/hardware identity and
quality evidence hash. The qualification JSON and its sibling evidence tree are
one indivisible release input; never copy or stage the JSON by itself.

### Original voice transport contract

The Original-vs-legacy qualification covers all 45 combinations of five Opus
bitrates, one/two/four frames per packet, and Continuous/PTT/VAD. Each paired
case must have byte-identical input PCM, pre-Opus PCM, framed Opus payloads,
packet counts, and terminator counts. Original must also report zero model
initializations, algorithmic latency, fallbacks, and deadline misses. The
evidence root identifies both the candidate and legacy executable SHA-256;
every one of the 45 cases repeats those identities, and the candidate hash is
independently matched to the exact staged `mumble.exe` supplied to the gate.

Before client 1 is started, pad both the noisy input and clean reference with
digital silence to a common packet/frame boundary. For the complete matrix,
using four frames per packet creates the shared 1,920-sample alignment that is
also valid for the one- and two-frame cases:

```powershell
python scripts/audio-quality/pad-fixed-timeline-wav.py `
  --input .tmp/original-contract/source-noisy.wav `
  --clean-reference .tmp/original-contract/source-clean.wav `
  --output-input .tmp/original-contract/client1-input-padded.wav `
  --output-clean-reference .tmp/original-contract/clean-reference-padded.wav `
  --attestation .tmp/original-contract/fixture-attestation.json `
  --frames-per-packet 4
```

The attestation records original/padded container and PCM hashes, original and
padded sample counts, and the exact number of appended zero samples. Padding
prevents a partial source frame from looking like receiver tail loss; trimming
or post-padding a receiver capture is forbidden.

Both client-2 WAV SHA-256 values remain in the qualification as independent
attestations, but they are not compared with each other. Separate live runs can
legitimately differ by a jitter-buffer capture frame even when their Opus
payloads are identical. Each receiver capture must instead carry a passing
fixed-timeline result with onset and end loss no greater than 480 samples,
zero missing tail samples, and zero clipped samples. The qualification records
both received sample counts and `receiver_jitter_delta_samples` (Original minus
legacy) so this live timing variation remains visible rather than being hidden
by correlation alignment.

## Qualification contract

`quality-qualification.schema.json` describes the schema-v3 portable result
envelope; schema v1/v2 self-reported summaries are deliberately ineligible.
Every v3 envelope must hash-attest an audio-free `case_evidence_jsonl` artifact.
Its first canonical JSONL record binds the exact scope, suite, and complete
`build` object; subsequent records are sorted profile/case measurements (plus
sorted transition measurements for Auto). `validate-quality-qualification.py`
requires the artifact bytes, verifies size/hash/canonical encoding and then
recomputes coverage, profile/cohort/language medians, catastrophe rate, all
counters, RTF, callback p99, processing maximum, memory growth, and soak time.
Reported totals must match those independent calculations. A passing result
must include fixed-timeline cold/warm coverage,
receiver cleanup disabled, all five core profiles, clean/noisy quality limits,
Balanced/Quality/VoiceFocus performance limits, zero invalid-output/hash/fallback counters,
and hashed JUnit/JSON/HTML/CSV/Parquet/spectrogram-index artifacts. It also
requires zero deadline, latency-attestation, and tail-drain failures. Artifact
metadata explicitly forbids raw or encoded audio samples while allowing the
failure spectrograms required for diagnostics.

All artifact paths are namespaced below
`artifacts/<suite>-<runner_class>/`. This prevents the low-performance,
mainstream, master and nightly evidence trees from colliding when they are
merged into the measured release handoff.

`quality-case-evidence.schema.json` documents each JSONL record. The tracked
canonicalizer is intended for the protected harness; it refuses to overwrite
evidence and emits both the artifact hash/size and the independently recomputed
summary values that belong in `qualification.json`:

```powershell
python scripts/audio-quality/generate-quality-case-evidence.py `
  --qualification C:/protected-audio/results/qualification-template.json `
  --records C:/protected-audio/results/case-records.json `
  --output C:/protected-audio/results/artifacts/master_quality-low-performance/case-evidence.jsonl
```

The input record envelope is `{ "schema_version": 1, "cases": [...],
"auto_transitions": [...] }`. Average RTF is total processing time divided by
total audio time; callback p99 uses nearest rank over all recorded callback
durations; memory growth is the worst per-case value; soak seconds are summed.
No raw/encoded audio, transcript, endpoint, username, path, or device ID belongs
in this evidence.

Run all fast policy regressions with:

```powershell
python scripts/audio-quality/run-self-tests.py
```

## CI trust boundary

`.github/workflows/input-enhancement-quality.yml` runs the dependency-free
policy tests and Original source-contract check on GitHub-hosted runners. Every
pull request is classified explicitly as `applicable` or `not-applicable`;
an applicable pull request also builds the exact candidate and runs 30
deterministic generated-audio product-pipeline cases: clean, stationary and
transient scenes, each with cold/warm start, across Original, Light, Balanced,
Quality and VoiceFocus. The smoke rejects recipe/model mismatches, fallback, non-finite
output, deadline/latency/tail errors and any Original PCM difference. Its
audio-free JSON/JUnit output is correctness/catastrophe evidence, not a claim
of human-speech perceptual quality.

The `master_quality` (at least 500 cases) and `nightly` (at least 5,000 cases)
suites are eligible for self-hosted execution only when all of these are true:

- the event is a push to `master`, the nightly schedule, or a manual dispatch
  of `master` in `dankmaster/mumble-forked`;
- repository variable `INPUT_ENHANCEMENT_QUALITY_RUNNERS_CONFIGURED` is exactly
  `true`;
- repository variable `INPUT_ENHANCEMENT_QUALITY_HARNESS` is an absolute path
  to the trusted runner-local harness;
- repository variable `INPUT_ENHANCEMENT_QUALITY_LEGACY_BINARY` is an absolute
  path to the immutable runner-local legacy `mumble.exe` used for all 45
  Original comparisons;
- protected runners exist with labels `input-enhancement-low` and
  `input-enhancement-mainstream` in a runner group restricted to this workflow.

If the infrastructure is absent, the workflow succeeds with an explicit
`NOT RUN` summary and does not create a qualification artifact. Once the
configuration flag is enabled, a missing harness, missing corpus, missing
evidence, failed metric, invalid hash, or unsafe artifact fails the job.

The trusted harness receives this contract:

| Input | PowerShell harness | Other executable/Python harness |
|---|---|---|
| suite | `-Suite` | `--suite` |
| checked-out source | `-SourceRoot` | `--source-root` |
| new empty result directory | `-OutputRoot` | `--output-root` |
| exact 40-character commit | `-SourceSha` | `--source-sha` |
| checked-in corpus lock | `-CorpusLock` | `--corpus-lock` |
| exact staged client executable | `-TestedBinaryPath` | `--tested-binary` |
| exact trusted legacy executable | `-LegacyBinaryPath` | `--legacy-binary` |
| complete staged client root | `-StagedClientRoot` | `--staged-client-root` |
| unsigned model manifest | `-ModelManifestPath` | `--model-manifest` |
| unsigned recipe manifest | `-RecipeManifestPath` | `--recipe-manifest` |

It must write `qualification.json`, `original-voice-qualification.json`, and
the audio-free artifacts named by `qualification.json` below the result root.
Artifact suffixes are part of the upload contract: canonical case evidence
`.jsonl`, JUnit `.xml`, per-case
`.csv`/`.parquet`, summaries `.json`/`.html`, and failure-spectrogram index
`.json`.
The gate independently validates semantic limits, hashes, JUnit and summary
artifacts, the 45-case Original/legacy transport matrix, the tested commit, and
the corpus-lock fingerprint. It hashes both supplied executables itself and
rejects evidence whose root or any matrix case names different candidate or
legacy bytes. Only the allowlisted, validated files are copied
to the upload directory; WAV/Opus/corpus material is never uploaded.
