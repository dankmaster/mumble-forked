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
archives. Its schema-v2 `corpus-state.json` records the canonical lock hash and
each `source_artifact_sha256`; use those values when authoring an inventory.

## Deterministic mixture plans

The plan generator consumes a local inventory with this shape:

```json
{
  "schema_version": 2,
  "corpus_lock_sha256": "<canonical lock SHA-256>",
  "items": [
    {
      "id": "speech-0001",
      "kind": "speech",
      "source_id": "mcgill-tsp-speech-v2-48k",
      "relative_path": "extracted/tsp/speech-0001.wav",
      "group_id": "speaker-001",
      "language": "en-US",
      "sample_rate_hz": 48000,
      "channels": 1,
      "duration_samples": 576000,
      "size_bytes": 1152044,
      "sha256": "<SHA-256 of this extracted/converted WAV>",
      "source_artifact_sha256": "9cfb3a3a13014c8ff90770a5d1923f376da73ac927b9100f09826f60cf06cf43"
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
      "source_artifact_sha256": "3b50cfde915b3984738169b4beb341e9f6b8062ae4c2076146c5db71c2c05dc7"
    }
  ]
}
```

Schema-v1 inventories are rejected because they did not bind the extracted
bytes. Every schema-v2 item must reference a verified, fetchable
local-evaluation archive, repeat that archive's locked SHA-256 as
`source_artifact_sha256`, and pin its own WAV with `size_bytes` plus `sha256`.
The renderer rechecks both per-file values before decoding, so changing an
extracted or converted file after inventory creation fails closed. Stable
speech/noise group IDs are assigned to tuning, validation, or holdout by SHA-256;
room-response and microphone-response IDs are split-specific as well. The output
always requests mono 48 kHz, 480-sample frames, receiver cleanup off, fixed
timeline scoring, and both 0 ms cold and 300 ms warm starts.

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
  --natural-crisp 50 `
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
  --received .tmp/audio-quality-runs/case/crisp-received.wav `
  --latency-samples 2400 `
  --transport-baseline .tmp/audio-quality-runs/case/original-received.wav `
  --transport-baseline-latency-samples 0 `
  --qualified-transport-baseline `
  --max-edge-loss-samples 480 --require-complete-tail --fail-on-new-clipping `
  --output .tmp/audio-quality-runs/case/crisp-fixed-timeline.json
```

The resulting attestation uses `timeline_alignment =
fixed-paired-original-onset` and records the baseline WAV hash, declared
baseline latency, qualification mode, raw onset offset and applied adjustment.

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

`quality-qualification.schema.json` describes the portable result envelope;
`validate-quality-qualification.py` applies the stricter semantic gates from the
product plan. A passing result must include fixed-timeline cold/warm coverage,
receiver cleanup disabled, all five profiles, clean/noisy quality limits,
Balanced/Crisp performance limits, zero invalid-output/hash/fallback counters,
and hashed JUnit/JSON/HTML/CSV/Parquet/spectrogram-index artifacts. It also
requires zero deadline, latency-attestation, and tail-drain failures. Artifact
metadata explicitly forbids raw or encoded audio samples while allowing the
failure spectrograms required for diagnostics.

Run all fast policy regressions with:

```powershell
python scripts/audio-quality/run-self-tests.py
```

## CI trust boundary

`.github/workflows/input-enhancement-quality.yml` runs the dependency-free
policy tests and Original source-contract check on GitHub-hosted runners. Every
pull request is classified explicitly as `applicable` or `not-applicable`;
an applicable pull request also builds the exact candidate and runs 24
deterministic generated-audio product-pipeline cases: clean, stationary and
transient scenes, each with cold/warm start, across Original, Light, Balanced
and Crisp. The smoke rejects recipe/model mismatches, fallback, non-finite
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
Artifact suffixes are part of the upload contract: JUnit `.xml`, per-case
`.csv`/`.parquet`, summaries `.json`/`.html`, and failure-spectrogram index
`.json`.
The gate independently validates semantic limits, hashes, JUnit and summary
artifacts, the 45-case Original/legacy transport matrix, the tested commit, and
the corpus-lock fingerprint. It hashes both supplied executables itself and
rejects evidence whose root or any matrix case names different candidate or
legacy bytes. Only the allowlisted, validated files are copied
to the upload directory; WAV/Opus/corpus material is never uploaded.
