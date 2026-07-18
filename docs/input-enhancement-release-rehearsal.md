# Input-enhancement pre-Azure release rehearsal

This rehearsal proves the complete release sequence without Azure Artifact
Signing, production credentials, a GitHub Release, or a publishable channel
pointer. It is deliberately split into two phases. A receipt produced before
the signed candidate exists is invalid by construction.

## Phase 1: Prepare

`prepare-input-enhancement-release-rehearsal.ps1` runs on the protected Windows
build runner. It:

1. verifies a clean checkout at the requested protected-master commit;
2. verifies the hash-pinned unsigned handoff, measured qualification,
   listening qualification, release-smoke harness, case set, fixture manifest,
   OG server, and protected prepare executor;
3. creates a one-day in-memory Authenticode certificate with an ephemeral
   private-key handle and a new test-only Ed25519 key in a private temporary
   directory; the certificate is never installed in a persistent store;
4. asks the protected executor to build the unsigned stage with that test
   public key, validate `candidate-build-receipt.json` live against the exact
   build/stage roots, sign only the declared PE files, build/sign the MSI and
   immutable update payload, and write the before/after inventories;
5. disposes the ephemeral certificate/key handles, deletes the PFX, Ed25519
   private key, password environment variable, and temporary directory, then
   independently proves every path/store/environment absence before attesting
   cleanup; and
6. emits canonical `rehearsal-challenge.json` and exits without creating a
   draft.

The challenge contains a 256-bit random `challengeId`, source/build identity,
the test public key, complete sorted SHA-256/size inventories for `unsigned/`
and `signed/`, separate captured inventories for both unsigned and signed
runtime-payload subtrees, the exact candidate-build receipt,
qualification/smoke/policy bindings, and a record for every transformation:

- `unchanged`: identical path, size, and hash;
- `authenticode-pe`: the only permitted changed existing files, restricted to
  `.exe` and `.dll`; the verifier parses the terminal WIN_CERTIFICATE, verifies
  its CMS signature and prepared certificate thumbprint, checks live
  Authenticode status, and requires identical PE bytes after normalizing only
  the checksum, security-directory entry, certificate blob, and at most seven
  zero alignment bytes;
- `packaged-output`: signed-only `.msi`, `.mumble-update`, `.zip`, `.json`, or
  `.sig` outputs.

Every union path must appear exactly once. Extra files, missing files,
case/path ambiguity, reparse points, root escapes, private-key extensions,
undeclared PE mutations, and non-canonical JSON fail closed. The tree hash is
SHA-256 over the compact JSON object `{ "files": [...] }` using the sorted
`path`, `sha256`, `size` records.

The protected prepare executor contract is:

```text
-Operation Prepare
-SourceRoot/-SourceSha/-BuildNumber
-ChallengeId
-UnsignedHandoffArchivePath
-MeasuredEvidenceArchivePath
-ListeningQualificationPath
-ReleaseSmokeHarnessPath/-FixtureManifestPath/-CaseSetPath
-ServerExecutablePath
-EphemeralPfxPath/-EphemeralPfxPasswordEnvironmentVariable
-EphemeralCertificateSubject/-EphemeralCertificateThumbprint
-EphemeralEd25519PrivateKeyPath/-EphemeralEd25519PublicKeyHex
-TimestampUrl
-OutputRoot
```

It must produce `prepare-build.json`, `rehearsal-challenge.json`, `unsigned/`,
and `signed/`. `prepare-build.json` identifies the contained build/stage roots,
candidate receipt, unsigned executable hash, and unsigned stage-payload hash.
The wrapper independently runs `candidate_build_receipt.py --validate` before
accepting the challenge. Challenge schema v2 additionally requires
`stagedPayload = { root, files, treeSha256 }` in both trees; the declared
`stagedPayloadSha256` must be exactly the verifier-derived subtree hash. Its
records are the shared `payload_identity.py` contract
`{ path, sha256, size_bytes }`, and the hash is SHA-256 over the
case-sensitively sorted canonical JSON array. It therefore matches the live
candidate-build receipt rather than introducing a second payload identity.

## Independent observation

Only after the prepared artifact has been uploaded and downloaded may the
hash-pinned observer and protected VM executor run. The prepare, observation,
and finalize phases use distinct ephemeral runner labels. Both receive the exact
challenge file and must copy its `challengeId`, source/build identity, signed
executable hash, signed payload hash, installer hash, and policy hash into their
evidence and receipts.

- Kill-switch trace schema v3 and observer-receipt schema v3 prove the exact
  launched client changed to `Original` within the policy refresh budget.
- Updater VM evidence schema v2 and receipt schema v3 prove all 26 N-2/N-1 native/MSI
  rollback cases against the exact signed payload.

These tools and outputs live outside both the source checkout and prepared
artifact. Each receipt carries a canonical-payload Ed25519 signature made by
that observer's own protected key. Kill-switch and VM identities and public
keys must be distinct and are pinned independently by Finalize. Their hashes
are passed independently to Finalize. A receipt with a different or missing
challenge ID, altered signed field, substituted identity, or wrong observer
key is rejected, even when all candidate hashes happen to match.

The protected observer executors receive `-ObserverIdentity` and
`-ObserverPublicKeyHex`. Their service-account configuration owns the matching
private key; the workflow never supplies it. Receipt schema v3 adds
`observerIdentity`, `observerPublicKeyHex`, and
`attestation = { algorithm, payloadSha256, signatureBase64 }`. The signature is
over the compact UTF-8 JSON object reconstructed in fixed field order by
`assert-input-enhancement-kill-switch-observation.ps1` or
`assert-input-enhancement-updater-vm-evidence.ps1`. Timestamps in that signed
payload use canonical UTC `yyyy-MM-ddTHH:mm:ssZ` form.

## Phase 2: Finalize

`finalize-input-enhancement-release-rehearsal.ps1`:

1. rehashes and fully validates the unchanged prepared challenge and both
   complete trees;
2. rejects a challenge already pending or finalized in the persistent replay
   ledger;
3. validates the independent VM evidence/receipt and kill-switch trace/receipt
   before invoking the hash-pinned finalize executor;
4. assembles the local draft without rebuilding or resigning any product byte;
5. runs schema-v2 release-rehearsal verification, including Original 6 plus
   enhanced 24 release-smoke cases, qualification/listening gates, detached
   signatures, updater protocol v4, the 26-case VM matrix, kill switch, exact
   unsigned-to-signed transformation, and static updater-runtime inspection;
6. creates and re-verifies `draft-manifest.json`; and
7. before any finalize-executor/output side effect, atomically and durably
   creates `<challengeId>.pending.json`; after full verification, durably
   creates `<challengeId>.finalized.json` and removes the pending marker.

The protected finalize executor receives only `-Operation Finalize`, the
unchanged prepared root/challenge, the independently produced evidence files,
its own expected hash, draft name, and a new empty output root. It cannot
receive signing private material. It must copy the prepared challenge and its
`unsigned/` and `signed/` trees into the draft so remote verification can
recompute the full transformation.

The replay ledger must be persistent and must not overlap the source,
prepared, or draft parent. A second finalize attempt fails before invoking the
executor. A concurrent race loses at atomic pending-marker creation and is not
an accepted draft. A crash leaves `pending` (or both markers during the final
transition), so it fails safe and requires an explicit operator audit rather
than silently replaying finalize side effects.

## Workflow and remote attestation

`.github/workflows/input-enhancement-release-rehearsal.yml` has four jobs:

1. `prepare` creates and uploads the prepared challenge;
2. `observe` downloads it and produces independent challenge-bound receipts;
3. `finalize` downloads both artifacts and consumes the challenge once; and
4. `remote-reverify` downloads the draft, reruns the full release verifier,
   verifies every manifest byte, and emits schema-v2 remote attestation bound
   to the challenge ID/hash and draft-manifest hash.

The workflow grants only `actions: read` and `contents: read`. It must not gain
`id-token: write`, `contents: write`, an environment, production secrets,
Azure/Trusted Signing, or GitHub Release calls. Actions artifacts are temporary
rehearsal transport, not a public release.

## Static updater requirement

`assert-windows-update-package.ps1 -RequireUpdaterRuntime` expands and verifies
the package manifest and then runs
`assert-mumble-updater-static-runtime.ps1` against the exact expanded
`mumble-updater.exe`. `dumpbin /dependents` must succeed and must not list
`zlib1.dll`. A missing updater or unavailable `dumpbin.exe` fails release
qualification; the switch is not advisory.

## External prerequisites

The tracked code intentionally does not provide machine-specific signing,
observer, VM, or MSI executors. Before a rehearsal can pass, operators must
provision and hash-pin:

- the prepare and finalize executors;
- an independent kill-switch observer with a protected Ed25519 attestation key
  and stable observer identity;
- an isolated updater-VM executor/harness, image, snapshot, and hardware
  fingerprint, plus a different protected Ed25519 attestation key and identity;
- a persistent replay ledger outside the Actions checkout/temp draft roots;
- the unsigned handoff and protected qualification/listening/smoke inputs.

Azure/OIDC configuration remains out of scope until this exact two-phase
rehearsal and community dogfood are green.
