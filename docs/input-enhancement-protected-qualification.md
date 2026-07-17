# Protected input-enhancement qualification

The 500-case and 5,000-case audio suites are permitted to run only for the
canonical repository's protected `master` ref. They never run pull-request
code on self-hosted machines. Pull requests use the generated, audio-free
30-case correctness smoke on a GitHub-hosted Windows runner instead.

This document describes the pre-Azure qualification infrastructure. It does
not configure Authenticode, publish a release, or make a production claim.

## Repository controls

The protected `master` branch requires an up-to-date pull request, one
approval, code-owner review, resolved review conversations and the following
exact status checks:

- `pr-hygiene`
- `linux-server`
- `linux-tests`
- `windows-client-server`
- `audio-quality-scope`
- `audio-quality-policy-and-original-contract`
- `pr-audio-smoke-30-cases`

Force-pushes and branch deletion are disabled. `.github/CODEOWNERS` assigns
the input-enhancement audio, updater and release surfaces to the repository
owner. The repository currently has only one collaborator, so administrator
bypass remains the documented recovery path until a second eligible reviewer
is added. Enforcing the reviewer requirement for administrators is a
pre-release operational gate; enabling it before a second reviewer exists
would make maintenance changes impossible rather than add useful review.

Repository Actions default to read-only and workflows may not approve pull
requests. Jobs that publish, analyze security events or later obtain an OIDC
token must declare the narrower write permission on that job explicitly. No
self-hosted quality runner is considered provisioned merely because a workflow
contains the expected labels; registration and protected runner inputs remain
external operational gates.

## Runner classes

Provision two dedicated Windows x64 runners:

| Required label | Intended class | Required power policy |
|---|---|---|
| `input-enhancement-low` | N100-like low performance | fixed, documented AC plan |
| `input-enhancement-mainstream` | i5-12400-like mainstream | fixed, documented AC plan |

Keep Windows, firmware, drivers, capture backend, power plan and the protected
tool bundle fixed during a qualification series. The workflow creates a
privacy-safe hardware fingerprint from CPU shape, computer model, RAM, Windows
build and active power scheme. It excludes machine name and hardware serials.
Every `qualification.json` must contain that fingerprint and its runner class.

## Protected files

Install these files outside the Actions checkout and make them writable only by
the runner administrator:

- the trusted harness entry point;
- the frozen legacy Mumble client;
- the OG local Mumble server used by the transport test;
- corpus inventory schema v3;
- the frozen suite case set;
- release fixtures;
- the pinned DNSMOS/eSTOI/Swedish-WER/English-WER runtime bundle.

Configure the following repository variables only after independently hashing
the installed bytes:

```text
INPUT_ENHANCEMENT_QUALITY_RUNNERS_CONFIGURED=true
INPUT_ENHANCEMENT_QUALITY_PROVENANCE_CONFIGURED=true
INPUT_ENHANCEMENT_QUALITY_HARNESS=<absolute path>
INPUT_ENHANCEMENT_QUALITY_HARNESS_SHA256=<sha256>
INPUT_ENHANCEMENT_QUALITY_LEGACY_BINARY=<absolute path>
INPUT_ENHANCEMENT_QUALITY_LEGACY_BINARY_SHA256=<sha256>
INPUT_ENHANCEMENT_QUALITY_SERVER_BINARY=<absolute path>
INPUT_ENHANCEMENT_QUALITY_SERVER_BINARY_SHA256=<sha256>
INPUT_ENHANCEMENT_QUALITY_MASTER_CORPUS_INVENTORY=<absolute path>
INPUT_ENHANCEMENT_QUALITY_MASTER_CORPUS_INVENTORY_SHA256=<sha256>
INPUT_ENHANCEMENT_QUALITY_MASTER_CASE_SET=<absolute path>
INPUT_ENHANCEMENT_QUALITY_MASTER_CASE_SET_SHA256=<sha256>
INPUT_ENHANCEMENT_QUALITY_MASTER_MIXTURE_PLAN=<absolute path>
INPUT_ENHANCEMENT_QUALITY_MASTER_MIXTURE_PLAN_SHA256=<sha256>
INPUT_ENHANCEMENT_QUALITY_NIGHTLY_CORPUS_INVENTORY=<absolute path>
INPUT_ENHANCEMENT_QUALITY_NIGHTLY_CORPUS_INVENTORY_SHA256=<sha256>
INPUT_ENHANCEMENT_QUALITY_NIGHTLY_CASE_SET=<absolute path>
INPUT_ENHANCEMENT_QUALITY_NIGHTLY_CASE_SET_SHA256=<sha256>
INPUT_ENHANCEMENT_QUALITY_NIGHTLY_MIXTURE_PLAN=<absolute path>
INPUT_ENHANCEMENT_QUALITY_NIGHTLY_MIXTURE_PLAN_SHA256=<sha256>
INPUT_ENHANCEMENT_QUALITY_RELEASE_FIXTURES=<absolute path>
INPUT_ENHANCEMENT_QUALITY_RELEASE_FIXTURES_SHA256=<sha256>
INPUT_ENHANCEMENT_QUALITY_METRICS_RUNTIME=<absolute path>
INPUT_ENHANCEMENT_QUALITY_METRICS_RUNTIME_SHA256=<sha256>
```

File values use the SHA-256 of their exact bytes. Directory values use the
canonical tree hash implemented by `payload_sha256()` in
`scripts/audio-quality/run-ci-quality-gate.py`: recursively sort normalized
relative paths, reject symbolic links, record each regular file's path, size
and SHA-256, serialize the records as canonical JSON, then hash that JSON.

The master and nightly inventory, case-set and mixture-plan pairs are separate
because their suite names and minimum case counts are part of the signed
provenance contract. Setting the two `*_CONFIGURED` flags is not sufficient to
pass. The gate recomputes every hash, rejects checkout-local protected inputs,
and compares the resulting identities with both the harness command and its
output.

## Evidence contract

Both runners download the same build-once artifact. The qualification evidence
is rejected unless it binds all of these identities:

- source commit, client executable and complete staged runtime payload;
- legacy client and OG server;
- corpus lock, corpus inventory, mixture plan and case set;
- model and recipe manifests plus every selected product-model hash;
- release fixtures, metrics runtime and trusted harness;
- runner class and hardware fingerprint.

The core scope contains exactly `Original`, `Light`, `Balanced`, `Quality` and
`VoiceFocus`; `Auto` has a separate non-blocking qualification scope. Receiver
cleanup must be disabled. The nightly evidence must cover at least 5,000 cases
and a 3,600-second soak against the same candidate payload. Schema-v3 quality
evidence includes canonical, hash-bound per-case JSONL below
`artifacts/<suite>-<runner-class>/`; the semantic gate recomputes coverage,
cohort/language medians, catastrophes, counters and performance from those
records. Only audio-free JSON/JSONL, JUnit, CSV/Parquet summaries, HTML and
failure-spectrogram indexes may be uploaded; raw or reconstructed audio remains
runner-local. Older self-reported-only quality schemas are not qualifying.

Before accepting a result, inspect the artifact identities, runner
fingerprints, case count, cold/warm partition, soak duration and Original's
45-case legacy parity. A later release rehearsal must require both the 500-case
and 5,000-case evidence for the exact payload that is promoted.

## Blind-listening evidence

A release listening pack is derived only from a hash-attested protected quality
run. Its private answer key binds the source commit, complete staged payload,
tested client and OG server as well as the corpus lock/inventory, suite mixture
plan, case set, trusted harness, pinned metrics runtime, release fixtures,
model/recipe manifests, protected quality-qualification file, runner class and
privacy-safe hardware fingerprint. Public listener files contain only the
canonical binding hash and opaque A/B paths; they contain no profile label,
source path, raw device identity or network destination.

The private answer key uses schema v3 and binds each source/profile orientation
to the SHA-256 of its exact public opaque pair manifest. The passing
qualification also uses schema v3 and is inseparable from its sibling
canonical evidence tree. Its sorted session manifest records the SHA-256,
listener pseudonym, session/session-instance IDs and source binding for every
completed response file, plus exact source-manifest and private-answer-key
references. The semantic verifier securely resolves and hashes every regular
file, rejects missing, extra, non-canonical, symlink/reparse or tampered entries,
validates the answer-key orientation against the source pairs, and recomputes
all count, preference, intelligibility and recurring-artifact results. Summary
numbers in the qualification are assertions, never trusted inputs.

The semantic gate rejects legacy schemas and requires, per submitted session,
12 noisy Quality-vs-Original pairs, 12 severe VoiceFocus-vs-Quality pairs and
clean evidence covering both enhanced profiles. At least eight votes in each
required comparison must be decisive, so a high tie rate can never manufacture
a passing 60% preference. The release rehearsal must compare every bound hash
with the corresponding measured-run record instead of trusting manually copied
totals.
