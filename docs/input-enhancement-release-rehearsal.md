# Input-enhancement release rehearsal

The manual `Input Enhancement Pre-Azure Release Rehearsal` workflow exercises
the release sequence without Azure, production credentials, a GitHub release,
or write access to repository contents. It is a qualification tool, not a
publication path.

The first job runs only on the protected Windows mainstream runner and only
from `master`. It consumes hash-pinned inputs outside the checkout, requires a
test Ed25519 key that was created before the candidate was built, creates a
one-day self-signed code-signing certificate after handoff, invokes the
protected executor, destroys the ephemeral certificate key, validates the
complete result, and uploads a seven-day draft Actions artifact. A separate
GitHub-hosted Windows job downloads that artifact, recomputes every byte hash,
reruns all evidence checks, and uploads only a small remote-reverification
receipt. Workflow permissions are `actions: read` and `contents: read`; there
is no OIDC permission or release environment.

## Protected runner inputs

Configure these repository variables only after installing and hashing the
files outside `GITHUB_WORKSPACE`:

```text
INPUT_ENHANCEMENT_REHEARSAL_EXECUTOR
INPUT_ENHANCEMENT_REHEARSAL_EXECUTOR_SHA256
INPUT_ENHANCEMENT_REHEARSAL_TEST_ED25519_PRIVATE_KEY
INPUT_ENHANCEMENT_REHEARSAL_TEST_ED25519_PRIVATE_KEY_SHA256
INPUT_ENHANCEMENT_REHEARSAL_TEST_ED25519_PUBLIC_KEY_HEX
INPUT_ENHANCEMENT_REHEARSAL_UNSIGNED_HANDOFF_ARCHIVE
INPUT_ENHANCEMENT_REHEARSAL_UNSIGNED_HANDOFF_ARCHIVE_SHA256
INPUT_ENHANCEMENT_REHEARSAL_MEASURED_EVIDENCE_ARCHIVE
INPUT_ENHANCEMENT_REHEARSAL_MEASURED_EVIDENCE_ARCHIVE_SHA256
INPUT_ENHANCEMENT_REHEARSAL_LISTENING_QUALIFICATION
INPUT_ENHANCEMENT_REHEARSAL_LISTENING_QUALIFICATION_SHA256
INPUT_ENHANCEMENT_RELEASE_SMOKE_HARNESS
INPUT_ENHANCEMENT_RELEASE_SMOKE_HARNESS_SHA256
INPUT_ENHANCEMENT_RELEASE_SMOKE_FIXTURE_MANIFEST
INPUT_ENHANCEMENT_RELEASE_SMOKE_FIXTURE_MANIFEST_SHA256
INPUT_ENHANCEMENT_RELEASE_SMOKE_CASE_SET
INPUT_ENHANCEMENT_RELEASE_SMOKE_CASE_SET_SHA256
INPUT_ENHANCEMENT_RELEASE_SMOKE_SERVER_BINARY
INPUT_ENHANCEMENT_RELEASE_SMOKE_SERVER_BINARY_SHA256
INPUT_ENHANCEMENT_KILL_SWITCH_OBSERVER
INPUT_ENHANCEMENT_KILL_SWITCH_OBSERVER_SHA256
INPUT_ENHANCEMENT_KILL_SWITCH_OBSERVER_RECEIPT
INPUT_ENHANCEMENT_KILL_SWITCH_OBSERVER_RECEIPT_SHA256
INPUT_ENHANCEMENT_UPDATER_VM_EXECUTOR
INPUT_ENHANCEMENT_UPDATER_VM_EXECUTOR_SHA256
INPUT_ENHANCEMENT_UPDATER_VM_RECEIPT
INPUT_ENHANCEMENT_UPDATER_VM_RECEIPT_SHA256
INPUT_ENHANCEMENT_UPDATER_VM_IMAGE_SHA256
INPUT_ENHANCEMENT_UPDATER_VM_SNAPSHOT_SHA256
INPUT_ENHANCEMENT_UPDATER_VM_HARDWARE_FINGERPRINT_SHA256
```

Generate the rehearsal Ed25519 key before configuring or building the
candidate. Keep its private key as a regular hash-pinned file outside the
checkout and pass its public key to CMake as
`MUMBLE_INPUT_ENHANCEMENT_POLICY_PUBLIC_KEY_HEX` together with a positive
`BUILD_NUMBER`. The public-key hash attested by the resulting `mumble.exe`
must therefore be known before any quality or release evidence is produced.
The rehearsal orchestrator derives the public key from the protected private
key and rejects a mismatched repository variable. This is a test-only key; it
is independent of the later production Ed25519 key and Azure signing.

The measured-evidence archive must contain the schema-v2 `core_release`
attestation and all four direct sibling results: master-quality and nightly for
both low-performance and mainstream runners. The unsigned handoff and all
evidence must identify the same payload. Listening input is a passing schema-v3
aggregate produced from a schema-v3 source pack and must bind itself to exactly
one of those protected runner results. Its sibling `<name>.evidence/` tree is an
indivisible input: the protected executor must preserve the relative tree in the
draft artifact so the release verifier can re-read every canonical source,
answer-key and session file and reject missing, extra or changed evidence.

The kill-switch receipt is produced independently of the rehearsal executor
and binds the real launched client PID, start/end timestamps, executable hash,
staged-payload hash, policy hash and runtime-trace hash. The updater VM receipt
binds the exact evidence bytes to a separate hash-pinned VM executor and the
protected image, snapshot and hardware fingerprints. The orchestrator copies
the protected receipts over any executor output before validation.

## Executor contract

The protected PowerShell executor is machine-specific but hash pinned. It
receives the exact source/build identity, unsigned handoff, measured and
listening evidence, release-smoke inputs, the prebuilt test Ed25519 key, the
ephemeral PFX path, test signer identity, timestamp URL, draft name and output
root. It must:

1. sign the staged PE payload with the supplied ephemeral certificate;
2. run `new-input-enhancement-embedded-key-attestation.ps1` against the exact
   signed staged `mumble.exe`; build 0, unmanaged mode, a different build
   number or a different embedded public-key hash must stop the rehearsal;
3. build the MSI from those signed bytes, then sign that MSI;
4. create the package, complete qualification and immutable audit artifact;
5. run six Original controls plus 24 enhanced localhost transport cases;
6. run the tracked updater-protocol-v4 simulator and the protected isolated
   Windows VM rollback matrix for native/MSI N-2 and N-1 starts, including
   update-health schema v3, exact candidate-executable binding and recovery MSI
   reinstall;
7. create normal and force-Original policies and exercise startup plus
   15-minute refresh-with-jitter behavior;
8. consume the independent observer/VM receipts, emit the signed protocol
   evidence, VM evidence and schema-v2 policy runtime trace,
   followed by `rehearsal.json` and the exact flat artifact set required by
   `assert-input-enhancement-release-rehearsal.ps1`.

`rehearsal.json` must mark `ephemeralSigning.ed25519Provisioning` as
`prebuilt-before-candidate-build`, include `embeddedKeyAttestation` in its
artifact map, and bind both `embeddedKeyAttestationSha256` and the raw
`embeddedPublicKeySha256`. The orchestrator and the independent remote job
both compare that public key with the protected expected value.

The orchestrator rejects executors containing release-API, repository-write,
Azure or production-signing capabilities. It also refuses Azure/OIDC/private
production-key environment variables. Private key files and audio formats are
forbidden in the draft manifest.

## Acceptance

The rehearsal fails closed unless it verifies:

- the exact master commit and every protected input hash;
- a positive managed build whose exact packaged `mumble.exe` reports the
  expected compile-time Ed25519 public-key hash;
- both 500-case master and 5,000-case nightly evidence sets;
- blind-listening evidence bound to the same binary, payload, corpus, mixture,
  model, recipe, protected runner and hardware identities;
- qualification plus exactly 6 Original and 24 enhanced release-smoke cases;
- the deterministic 26-case protocol simulation and the protected 26-case VM
  rollback matrix, covering native/MSI install failure, pre-marker crash,
  audio-init failure, process kill, power loss and both candidate/recovery
  3010 behavior from N-2 and N-1;
- channel-pointer schema v2 with the candidate MSI and exactly two earlier
  recovery MSIs;
- a separately signed force-Original policy plus a protected independent
  observer receipt proving that the exact staged client process reaches
  Original within 20 minutes;
- byte-identical download from the Actions draft artifact store.

Azure/Authenticode production signing remains a later workflow. The qualified
signing job is deliberately disabled until an additional verifier binds the
same candidate to this rehearsal, protected listening/VM receipts and at least
seven days plus 20 talk-hours of dogfood evidence.

The machine-specific rehearsal executor, independent kill-switch observer and
isolated Windows VM executor/receipt producer are protected external
components. This repository defines and verifies their fail-closed contracts;
it does not provide a portable implementation of those machine-specific
executors.
