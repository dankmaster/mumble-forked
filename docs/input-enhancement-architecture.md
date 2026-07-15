# Input enhancement architecture

Input enhancement is a capture-side product layer. It receives mono 48 kHz
PCM after the existing echo-cancellation point and before Mumble's existing
preprocessor, voice activation, Opus encoder and voice transport. It does not
change the voice protobuf, packet format, Murmur forwarding, Opus packet
assembly or receiver decoder. This is a protected compatibility boundary, not
a transport replacement: release qualification uses the existing Opus/server
path and keeps receiver-side cleanup disabled.

## Product profiles

| Profile | Product engine | Added causal latency budget | Intended use |
|---|---|---:|---|
| Original | Existing Mumble input path | 0 ms | Exact compatibility and recovery |
| Light | Speex denoise | up to 10 ms | Low CPU and stationary noise |
| Balanced | RNNoise | up to 30 ms | General-purpose default candidate |
| Crisp | DeepFilterNet | up to 50 ms | Full-band quality on capable hardware |
| Auto | Policy over signed recipes | Selected-profile budget | Environment and CPU adaptation |

`InputEnhancement::RecipeCatalog` maps the public 0–100 controls into bounded,
qualified intervals. The signed `input-recipes.json` catalog must authorize the
compiled recipe revision, requested profile, concrete engine, model IDs,
control ranges, latency, CPU class, execution-semantics revision, qualified-mix
curve revision and adaptation-policy revision. A neural model is re-hashed
immediately before initialization and must match signed `input-models.json`.

DTLN and alternate RNNoise/DeepFilterNet models remain Advanced/benchmark
choices. They are not silently substituted for a product recipe.

## Real-time boundary and failure behavior

`InputEnhancement::Pipeline` is configured and warmed before capture callbacks
can use it. The callback processes exactly one preallocated 480-sample frame.
Model construction, model hashing, recipe verification, thread creation and
processor reset happen off callback. DeepFilterNet inference runs on its own
bounded SPSC worker because the tract runtime allocates inference tensors; the
audio callback only publishes and consumes preallocated frames.

The pipeline maintains a dry signal on the same causal timeline as neural
output. Invalid input is sanitized, out-of-range output is clamped, and model
errors, non-finite output, queue/deadline failure or latency mismatch latch a
fallback. An active utterance drains the declared delayed dry tail before the
client returns to zero-latency Original, avoiding a timeline jump or lost final
consonant.

The 5 ms Balanced and 8 ms Crisp values are p99 qualification limits. The hard
runtime catastrophe threshold is 10 ms; a single scheduler preemption below
that threshold is recorded but does not unnecessarily change the user's
profile.

`Original` does not construct a product pipeline or model. Source-contract and
E2E tests compare input PCM, pre-Opus PCM, encoded Opus payloads, packet counts
and terminators against the legacy-off path. Live receiver WAV lengths are not
required to be byte-identical because the jitter buffer and mixer are clocked
independently; each capture instead has to pass fixed-timeline onset, end, tail
and clipping checks.

## Settings and device identity

The JSON settings object `audio.input_enhancement` has schema version 2. It
contains a global default and at most 32 physical microphone entries. Each
entry stores the profile, reduction and character controls, Auto permission,
calibration state, last-known-good recipe and pending probation state.

Probation persists two exact recipe bindings: the candidate and the previous
last-known-good recipe. A binding covers the signed catalog revision, recipe
ID/revision, requested and effective profile, engine, validated controls,
latency/CPU contract, model ID, model SHA-256 and the manifest-relative model
path. It also carries a canonical SHA-256 execution fingerprint over every
recipe field, including the exact IEEE-754 mix-factor bits and the versioned
mix/adaptation semantics. Absolute paths are never trusted from settings.
Startup resolves the relative path through the verified package, re-hashes the
asset and requires every field to match the currently compiled recipe. Missing
fields, unsafe paths, malformed hashes, profile/control disagreement,
execution drift or catalog/model drift selects `Original`; a same-named
replacement recipe is never substituted.
Build-number-zero development bindings carry a reserved unmanaged revision
that no signed release catalog can match.

WASAPI uses the persistent endpoint ID and resolves `System default` to the
physical endpoint that is currently open. This is the qualified Windows-GA
identity path. Other backends currently persist their configured backend
selection only when it is stable; they still need the WASAPI-equivalent
opened-device confirmation (and PipeWire node serial binding) before their
per-physical-device behavior can be qualified. An identity that cannot be made
stable is session-only. Editing a draft input device edits the resolved
physical device's profile, not whichever microphone happened to be running
when Settings opened.
An existing profile's LRU timestamp advances only after WASAPI confirms that
the expected physical endpoint really opened; the timestamp update is queued
off the capture thread and saved through the normal settings store.

Legacy settings are migrated without changing sound. The exact previous
engine, model, strength and custom path remain in a hidden legacy override
until the user explicitly selects a product profile. Corrupt or unsupported
input-enhancement JSON fails to Original.

## Calibration and Auto

Calibration is a local-only state machine:

1. one-second level check;
2. eight seconds of room sound;
3. twelve seconds of guided speech;
4. optional eight-second local-noise capture;
5. product-pipeline plus current Opus encode/decode evaluation;
6. loudness-matched randomized A/B playback;
7. Apply or Cancel.

An atomic transmission block is published before calibration starts and is
checked again at the final pre-encode gate. No partial packet or terminator is
sent during calibration. Captured PCM and playback buffers stay in memory and
are overwritten on completion, cancellation or error. Candidate evaluation is
off the UI/audio threads and accepts only recipes/models copied from the
verified immutable package snapshot. Apply starts a 60-second probation that
requires at least ten seconds of processed speech; any initialization,
non-finite output, deadline or crash signal restores the last-known-good entry
and exposes Undo. Healthy completion promotes the exact candidate binding.
Rollback and Undo move preference plus binding atomically; a live probation
failure durably saves the rollback and queues an input restart so the running
candidate is actually replaced by the verified last-known-good recipe (or
`Original` when that exact asset is unavailable). The one-shot candidate and
its exact binding are persisted with the rollback, so the replacement
`AudioInput` can restore Undo after that restart; Undo consumes the stored pair
and starts a fresh probation.

The callback's 480-sample conversion scratch frame is raw audio too. It is
securely overwritten after every ingestion and whenever the callback is
quiesced or the bridge is destroyed. The diagnostic clear check covers the
session store, callback scratch and every A/B playback buffer.

The calibration rollback baseline never comes from the editable Settings
draft. `AudioInput` records it only after the applied recipe has been verified,
prepared and made healthy; draft values shape candidate controls only. Legacy,
unhealthy or inexact active paths fail closed without recording or applying a
draft. `Auto` and automatic adaptation are also excluded from calibration for
this release because a dynamic policy does not yet have an exact set-binding;
the UI reports that the user must first apply an explicit product profile with
adaptation off.

Auto v1 consumes only coarse noise-floor, SNR, stationarity, VAD confidence,
CPU class and callback-pressure buckets plus the user's controls. It contains
no speaker, language, identity or demographic input. Control movement is
limited to ±20 points with hysteresis. A cross-engine change requires silence
and a pristine processor prepared off callback. New installations remain on
`Original`; Auto is neither calibrated nor eligible to become the default
until its exact set-binding and rollout gates are implemented and qualified.

The current cross-engine handoff is atomic only after 300 ms of verified
acoustic silence and after the active causal tail has drained. This is longer
than the combined maximum qualified latencies and prevents speech loss while
keeping the callback lock-free. It is deliberately not described as an
overlap crossfade: mixing engines with unequal causal latency would require a
second aligned delay path and simultaneous inference. Auto remains ineligible
as a new-install default until that handoff is either implemented and
qualified or product qualification proves the drained-silence handoff is
artifact-free across every supported recipe pair.

## Policy, recovery and release

The optional HTTPS channel policy is an Ed25519-verified, expiring document
with exactly these fields: availability, force-Original, recommended profile,
recipe-set version, minimum build and expiry. Invalid policy is never applied.
Policy does not use Murmur, protobuf or the voice transport.

Recovery is available through `--disable-input-enhancement` and the matching
environment variable. Release builds without a valid embedded verification
key and signed package catalogs fail closed; only an explicit build-number-zero
developer build may run unmanaged.

Native package application is restricted to a non-elevated token and a
same-user-writable installation. Machine-wide/Program Files installations use
the signed MSI fallback and Windows Installer's privileged transaction
boundary; the native ZIP path never turns user-writable updater state into an
elevated file writer. Its mandatory package SHA-256 is verified while the ZIP
is held open without write/delete sharing, and the public prepare sidecar is
only a cache-complete signal: paths, hashes, health policy and stale-file
decisions are rebuilt from the verified ZIP and current installation.

Only the native same-user package path currently owns the `awaitingHealth`
journal, stable-runtime marker and automatic restoration of the previous
payload after a failed client start. Windows Installer can roll back an MSI
installation failure, but the current MSI path does not reinstall the previous
signed MSI after a *successful* install whose client later fails the audio
health gates. Machine-wide stable GA is therefore blocked until that
post-install health rollback is implemented and exercised against a deliberately
broken signed candidate.

The native transaction is journaled before mutation. Every staged source is
verified and every prior managed file plus installed manifest is copied,
hashed and flushed before schema-v2 state (`rollbackArmed`, then
`awaitingHealth`) is atomically replaced with write-through semantics. A
random 128-bit transaction ID binds the journal, health marker and exact backup
directory. Only then may the first application file change. A per-installation
mutex admits a single writer. Each updater SHA-256 gets a recovery directory
containing the exact same-user updater and its verified `zlib1.dll`; neither is
ever elevated. A detached watchdog rolls back if the owning updater dies,
while a flushed, persistent per-installation `Run` entry covers power loss and
reboot. Recovery needs neither the downloaded package nor a working new
client, is idempotent for a journal written before mutation or during a mixed
payload, records `committed` or `rolledBack` durably before cleanup, and removes
its bootstrap registration only after durable terminal state.

The current shared Windows package still links the updater to the adjacent
`zlib1.dll`. The recovery copy is hash-verified and the updater is deliberately
never elevated, which contains this to the same user's security boundary, but
Windows loads that dependency before updater policy can run. This is acceptable
for preview qualification only; stable GA requires zlib to be statically linked
or loaded from an equivalently immutable, trusted location.

The Windows release workflow builds once from a locked commit, qualifies that
stage, signs PE/MSI files, creates an immutable artifact, runs twelve fixed
two-client cases against that exact staged update, and promotes unchanged
hashes to preview or stable. Qualification and release-smoke evidence are
Ed25519-signed, and promotion re-verifies both signatures immediately before
publishing the signed channel pointer. Stable/Auto promotion additionally
requires signed rollout evidence for the immutable build; emergency disable
or force-Original policy remains available without waiting for pilot gates.
The previous signed recovery pointer and all referenced hashes/signatures are
verified before they are carried forward. At least two previous releases are
retained for rollback. A health marker is written only after settings,
policy/package verification, audio initialization and stable runtime checks
succeed.

## Current production eligibility

The tracked implementation is a production-qualification candidate, not proof
that a stable production release has occurred. `Original`, `Light`, `Balanced`
and `Crisp` may advance through signed preview and stable opt-in qualification.
`Auto` may be tested explicitly, but it cannot be recommended or become the
new-install default while it uses the drained 300 ms silence handoff instead of
a qualified aligned crossfade (and while exact Auto set-binding remains
unimplemented).

Stable publication also depends on evidence outside this repository: Azure
Artifact Signing/OIDC and protected release environments, protected N100-like
and mainstream Windows runners, the full holdout/release suites against the
exact signed artifact, a tested rollback drill, and the required opt-in pilot
devices, talk hours, telemetry and dashboards. Until those gates are satisfied
and the MSI post-health rollback and updater zlib gaps above are closed, the
workflow must stop at preview rather than describe the build as GA. None of
these gates authorizes a change to the existing Mumble voice protocol or OG
Opus transport.

## Quality evidence

Reproducible corpus metadata and license decisions live under
`scripts/audio-quality`. Private audio remains in ignored runner storage. The
mixture generator creates deterministic 48 kHz cases, and the fixed-timeline
scorer deliberately forbids correlation alignment for release decisions.
The fetchable local-evaluation set includes the Apache-2.0 OpenSLR SLR28
noise/RIR database. Corpus inventory v2 binds every WAV to both the locked
source-archive SHA-256 and its own size/SHA-256; rendering re-verifies the
per-file bytes before decoding.

Use `speech_cleanup_benchmark --profile ...` for offline product measurements;
direct `--backend` selection is Expert-only. The local two-client harness feeds
the same cases to client 1, through the unchanged Opus/server path on
127.0.0.1, captures client 2 with receiver cleanup disabled and records build,
corpus, recipe and model hashes plus latency/drain/fallback and callback/worker
diagnostics. Because the protected OG receive path has its own startup jitter,
each packaged timeline group carries a separately hashed `Original` capture
with identical transport settings. The scorer may subtract only that control's
positive frame-aligned startup-onset offset. An unqualified/self control is
capped to one 10 ms frame and must first pass its own fixed-timeline,
complete-tail, clipping and OG voice-contract gates. Only that independently
qualified control may contribute its full observed OG startup offset to the
paired enhancement score. The scorer still performs no correlation search,
and enhancement end, tail completeness and clipping remain on the absolute
declared timeline.

CI can prove correctness and enforce evidence schemas. Public GA additionally
requires credentials and external evidence that cannot be manufactured by a
source change: Authenticode signing, protected low/mainstream runners, the
preview/stable pilot hours and devices, opt-in field telemetry, dashboard and
tested channel rollback.
