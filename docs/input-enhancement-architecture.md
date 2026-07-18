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
| Quality | DeepFilterNet | up to 50 ms | Full-band quality on capable hardware |
| Voice Focus | DeepFilterNet | up to 50 ms | Explicit aggressive cleanup for severe noise |
| Auto | Policy over signed recipes | Selected-profile budget | Experimental environment and CPU adaptation |

`Crisp` is accepted only as a legacy serialized/source alias for `Quality`.
New settings, policies, diagnostics and manifests always write `Quality`.
Voice Focus uses the same verified full-band model as Quality with a separate
70–100 reduction and 40–100 character envelope. Auto cannot select it.

`InputEnhancement::RecipeCatalog` maps the public 0–100 controls into bounded,
qualified intervals. The packaged recipe manifest uses schema version 2 and
the current catalog revision is `input-recipes-v4`. Signed
`input-recipes.json` must authorize the compiled recipe revision, requested
profile, concrete engine, model IDs, control ranges, latency, CPU class,
execution-semantics revision, qualified-mix curve revision and
adaptation-policy revision. A neural model is re-hashed immediately before
initialization and must match signed `input-models.json`.

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

Product Quality and Voice Focus recipes opt in to a peak-limited 8×
DeepFilterNet model-domain gain for very quiet microphones. The inverse gain is
delayed by the model's exact native latency and applied to the corresponding
wet output, so user PCM level and dry timeline do not change. The buffers are
allocated during preparation, never in the callback. This execution semantic
is versioned in the recipe manifest. Legacy/Expert input selections and all
receiver cleanup leave the opt-in flag false and call the historical model API
directly. Quality preserves its 0.70 minimum and 0.75 normal anchor while high
controls can reach 0.95. Explicit-only Voice Focus can reach 1.00; it is never
selected by Auto and the endpoint must still clear clean-speech, WER,
catastrophe and blind-listening gates. These curves are versioned independently
of the UI controls.

The product-only DeepFilterNet path raises only weak input towards a -24 dBFS
model-domain peak, capped at +18.06 dB. It never attenuates or raises an already
louder input. The exact causal gain is removed from the corresponding model
output before wet/dry mixing, so microphone level and the dry timeline stay
unchanged. This avoids overdriving normal and noisy input while preserving the
quiet-microphone protection that motivated model-domain normalization.

Balanced and DeepFilterNet profiles share the same causal dry-aligned onset
ramp and quiet-room release guard. The release guard follows the learned room
floor and opens only for weak utterance tails, preserving final consonants and
room decay without weakening suppression across noisy low-energy speech.

The 5 ms Balanced and 8 ms Quality/Voice Focus values are p99 qualification
limits. The hard runtime catastrophe threshold is 10 ms; a single scheduler
preemption below that threshold is recorded but does not unnecessarily change
the user's profile.

`Original` does not construct a product pipeline or model. Source-contract and
E2E tests compare input PCM, pre-Opus PCM, encoded Opus payloads, packet counts
and terminators against the legacy-off path. Live receiver WAV lengths are not
required to be byte-identical because the jitter buffer and mixer are clocked
independently; each capture instead has to pass fixed-timeline onset, end, tail
and clipping checks.

## Settings and device identity

The JSON settings object `audio.input_enhancement` has schema version 3. It
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

Schema-v2 settings migrate the old Crisp value to the same numeric Quality
profile and preserve both controls. Legacy settings are migrated without
changing sound. The exact previous engine, model, strength and custom path
remain in a hidden legacy override until the user explicitly selects a product
profile. Corrupt or unsupported input-enhancement JSON fails to Original.

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
this release because the live dual-pipeline transition path is not yet
available; the UI reports that the user must first apply an explicit product
profile with adaptation off.

Auto consumes only coarse noise-floor, SNR, stationarity, VAD confidence, CPU
class and callback-pressure buckets plus the user's controls. It contains no
speaker, language, identity or demographic input. Control movement is limited
to ±20 points with hysteresis. A cross-engine change requires silence and a
pristine processor prepared off callback. New installations remain on
`Original`; Auto is neither calibrated nor eligible to become the default
until its live transition path and separate rollout gates are implemented and
qualified.

The Auto-v2 library defines an exact, fingerprinted set binding for Light,
Balanced and Quality, fixed-size session diagnostics, a bounded off-audio-
thread capability probe keyed by the exact build, CPU and model-set hashes, and
the callback-safe
`Idle → Priming → Fading → Rebase → Active/Abort` coordinator. The coordinator
requires 300 ms verified silence and completed tail drain, aligns both natural
latencies in preallocated delay lines, performs a 40 ms equal-power crossfade,
rebases only during continued silence, and aborts to the source on deadline or
invalid output. Voice Focus is rejected from the set. The current AudioInput
bank cannot yet lease stable source and candidate processors through
commit/abort, including the non-neural Light path. Consequently Auto readiness
is deliberately non-selectable and a persisted or requested Auto profile fails
closed to `Original`; the partially wired v1 switching path is not treated as
production behavior. Auto remains visible only as Advanced/experimental and is
outside core release qualification until dual-pipeline feeding is integrated.
The separate automatic-adaptation control is Advanced-only for the same
reason; Basic exposes only Original, Light, Balanced, Quality and Voice Focus.

## Policy, recovery and release

The optional HTTPS channel policy is an Ed25519-verified, expiring document
with exactly these fields: availability, force-Original, recommended profile,
recipe-set version, minimum build and expiry. Invalid policy is never applied.
Voice Focus is rejected as a remote recommendation because it is a
manual-only aggressive profile. The client checks policy immediately at
startup and then on a randomized 15–17 minute cadence; the bounded 10-second
transfer timeout leaves margin for an active profile to reach Original within
the 20-minute emergency contract. Policy does not use Murmur, protobuf or the
voice transport.

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

Update-health schema version 3 and updater protocol version 4 cover both native
packages and Windows Installer transactions. The journal names the transaction
mode, exact candidate executable SHA-256, package identities, transaction ID,
recovery material and any pending reboot. A health marker is accepted only from
that exact running executable after settings/package verification, audio
initialization and at least ten seconds of stable runtime.

Native package admission requires the exact protocol-v4 version and the
explicit production health contract (`required=true`, 10,000 ms stable runtime
and 45,000 ms timeout). Missing, older, false or differently timed fields are
rejected before staging or application mutation, so malformed packages cannot
silently bypass health probation.

The native transaction is journaled before mutation. Every staged source is
verified and every prior managed file plus installed manifest is copied,
hashed and flushed before `rollbackArmed`, then `awaitingHealth`, is atomically
replaced with write-through semantics. A random 128-bit transaction ID binds
the journal, health marker and exact backup directory. Only then may the first
application file change. A per-installation mutex admits a single writer. Each
updater SHA-256 gets a recovery directory containing the exact same-user,
self-contained updater; it is never elevated. A detached watchdog rolls back
if the owning updater dies, while a flushed, persistent per-installation `Run`
entry covers power loss and reboot. Recovery needs neither the downloaded
package nor a working new client, is idempotent for a journal written before
mutation or during a mixed payload, records `committed` or `rolledBack` durably
before cleanup, and removes its bootstrap registration only after durable
terminal state.

Before an MSI candidate runs, the client downloads and verifies a known-good
MSI and the updater persists it outside the installed payload and arms recovery.
A successfully installed candidate that crashes, fails audio initialization or
misses its health marker causes the known-good MSI to be reinstalled. Candidate
exit code 3010 cannot enter health probation and fails closed to recovery; a
3010 from recovery leaves the journal and startup recovery armed across reboot
instead of claiming success. Protected VM evidence must still prove these
destructive cases against N-2→N and N-1→N payloads.

The updater owns a private static zlib target using the `/MT` runtime; third-
party warnings are isolated without weakening warnings for updater sources.
Release verification rejects a `mumble-updater.exe` whose import table still
contains `zlib1.dll`.

Channel-pointer schema version 2 binds the immutable candidate package and MSI
to exactly two earlier recovery MSIs, including their tag, URL, size and
SHA-256. The client downloads and verifies the selected known-good MSI before
handoff. The first v2 publication requires an explicit, hash-attested bootstrap
set with two recovery records; an incomplete legacy pointer cannot silently
become v2.

The production release contract builds once from a locked commit, qualifies
that stage, signs the already-qualified PE payload, builds and signs the MSI,
creates an immutable artifact, runs 30 fixed two-client cases against those
exact bytes, and promotes unchanged hashes. Qualification and release-smoke
evidence are Ed25519-signed, and promotion re-verifies both signatures
immediately before publishing the channel pointer. Stable/Auto promotion also
requires signed rollout evidence for the immutable build; emergency disable or
force-Original policy remains available without waiting for pilot gates. The
previous signed recovery pointer and all referenced hashes/signatures are
verified before they are carried forward. At least two previous releases are
retained for rollback.

The tracked pre-Azure rehearsal exercises that sequence with ephemeral test
keys, no repository write and no public release. This input-enhancement work
does not configure or exercise Azure/OIDC production signing: production
Authenticode is deliberately the final step after the unsigned candidate,
rollback evidence and internal dogfood are complete.

## Current production eligibility

The tracked implementation is a production-qualification candidate, not proof
that a stable production release has occurred. `Original`, `Light`, `Balanced`,
`Quality` and `Voice Focus` form the core qualification scope. `Auto` has its
own non-blocking scope and cannot be recommended or become the new-install
default until the v2 coordinator is integrated into live AudioInput and every
transition pair passes its dedicated suite.

Stable publication also depends on evidence outside this repository:
protected N100-like and mainstream Windows runners, measured master/nightly
qualification and soak, the protected destructive updater VM matrix, blind
listening, the internal dogfood window, and finally Azure Artifact Signing/OIDC
and protected release environments. Telemetry is intentionally deferred until
there is a qualified candidate worth dogfooding. Until those gates are
satisfied, the build is an internal candidate rather than a public preview or
GA release. None of these gates authorizes a change to the existing Mumble
voice protocol or OG Opus transport.

## Quality evidence

Reproducible corpus metadata and license decisions live under
`scripts/audio-quality`. Private audio remains in ignored runner storage. The
mixture generator creates deterministic 48 kHz cases, and the fixed-timeline
scorer deliberately forbids correlation alignment for release decisions.
The fetchable local-evaluation set includes the Apache-2.0 OpenSLR SLR28
noise/RIR database. Corpus inventory v3 binds every WAV to both the locked
source-archive SHA-256 and its own size/SHA-256, distinguishes real RIR and
microphone-response assets, hashes transcripts, and records derivation
provenance; rendering re-verifies the per-file bytes before decoding.

Use `speech_cleanup_benchmark --profile ...` for offline product measurements;
direct `--backend` selection is Expert-only. The local two-client harness feeds
the same cases to client 1, through the unchanged Opus/server path on
127.0.0.1, captures client 2 with receiver cleanup disabled and records build,
corpus, recipe and model hashes plus latency/drain/fallback and callback/worker
diagnostics. Because the protected OG receive path has its own startup jitter,
each packaged timeline group carries a separately hashed `Original` capture
with identical transport settings. The scorer may subtract only that control's
positive frame-aligned startup offset and, for VAD only, its observed OG
speech-end truncation. An unqualified/self control is capped to one 10 ms
frame. The protected orchestrator first qualifies the control against pinned
transport budgets, clipping and the byte-exact OG voice contract. Continuous
and PTT require a complete capture timeline. VAD instead requires the causal
processor tail-drain attestation because its intended behavior omits trailing
room silence; its route end budget is capped to one encoded packet. Only that
independently qualified control may contribute its observed OG route offsets
to the paired enhancement score. The candidate still gets at most one 10 ms
additional onset or end loss, no missing causal tail and no new clipping. The
scorer performs no correlation search, shift, trim or post-padding.

Rollout qualification does not accept operator-entered population or
reliability totals. A separately signed aggregate-export schema v2 binds a
pinned query SHA-256, immutable source-snapshot SHA-256, one exact immutable
build, rollout audience, recipe set and canonical observation window. The
window must end within the evidence-age limit and the exporter ingest gap may
not exceed 24 hours. Raw JSON is checked by the tracked strict validator before
PowerShell conversion, so booleans, arrays, integers and numeric fields cannot
be supplied through coercible strings or alternate types.

The `private-community` audience maps only to `community-stable`: at least
seven observation days, 20 talk-hours, zero P0/P1/model-hash/callback-regression
events, and `distinctDevices >= intendedCommunityDevices` with an intended
device count of at least one. The later `public` audience retains the separate
10/25/50-device `stable-opt-in`, `auto-recommended` and `auto-default` gates.
The signed aggregate binds the audience, so a public promotion cannot select
the smaller private-community threshold at publication time.

A schema-v2 rollout envelope contains exact hashes of the aggregate and its
separate Ed25519 signature. A pending RNNoise track contains no completion
claim. Completion is accepted only when the envelope also binds the exact bytes
and detached release signature of `rnnoise-selection-decision.json`, the strict
one-shot decision emitted by `select-rnnoise-model.py`; `embedded-retained` maps
to the same rollout outcome and `custom-selected` maps to `custom-promoted`.
There is no CLI path for manually declaring that campaign complete. Telemetry
remains unimplemented until an actual qualified dogfood candidate exists;
these schemas are a fail-closed future trust boundary, not fabricated field
evidence.

CI can prove correctness and enforce evidence schemas. Public GA additionally
requires credentials and external evidence that cannot be manufactured by a
source change: Authenticode signing, protected low/mainstream runners, the
preview/stable pilot hours and devices, opt-in field telemetry, dashboard and
tested channel rollback.

Release qualification consumes one schema-v2 measured-evidence archive with
four direct sibling results: `master_quality` (at least 500 cases) and
`nightly` (at least 5,000 cases), each from both the protected low-performance
and mainstream Windows runner classes. All four bind the same source,
executable, staged payload, server, models, recipe catalog, corpus and trusted
harness identities; nightly additionally carries the required one-hour soak
evidence.

Blind-listening source schema v3 binds its opaque A/B pack to one exact
protected quality result and the same binary, payload, corpus, mixture, case
set, metrics runtime, models, recipes, fixtures, runner class and hardware
fingerprint. Session and aggregate schema v2 fail closed on older or incomplete
evidence. Every session contains at least 12 noisy Quality-versus-Original
pairs, 12 severe Voice-Focus-versus-Quality pairs and two clean controls, with
at least eight decisive votes in each required comparison. The aggregate gate
enforces the 60% preferences, intelligibility and recurring clean-artifact
rules before release rehearsal can consume it.
