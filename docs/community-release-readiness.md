# Qt Quick community release readiness

This document defines the frozen release train for the Qt Quick desktop client.
The supported release scope is the Windows client and Linux Murmur server. A
Linux desktop client is explicitly outside this gate.

## One candidate, three independent tracks

Every result belongs to one immutable candidate. The candidate manifest records
the source Git revision, a worktree fingerprint and the exact Windows
`mumble.exe` SHA-256. A dirty worktree can be fingerprinted for development, but
only a clean `candidate_kind=release` manifest is eligible for promotion.

Create a development candidate while blocker fixes are still being collected:

```powershell
.\scripts\windows\new-community-release-candidate.ps1 `
  -Executable .\build-shared-webengine\shared-webengine-stage\mumble.exe `
  -AllowDirtySource `
  -OutputPath .\.tmp\community-release\candidate.json
```

Omit `-AllowDirtySource` when cutting the final candidate. Keep every evidence
file below `.tmp/community-release/<candidate-id>/` locally or in an immutable CI
artifact. Never reuse evidence after the executable or source fingerprint
changes.

### Track A: connected product parity

The tracked scenario contract is
`scripts/windows/connected-product-release-matrix.json`. It uses only typed
controller/model automation commands and has no QML object-name or Windows UIA
dependency.

Run one shared build, then exercise the existing two-client harnesses without
rebuilding between them:

```powershell
.\scripts\local\dev-build.ps1 -Fast
.\scripts\local\invoke-dev-chat-matrix.ps1 -SkipBuild `
  -ChannelScopes '#general','#links','VC Root / Landing'
.\scripts\local\invoke-connected-product-parity-smoke.ps1 -SkipBuild `
  -VoiceScope 'VC Root / Landing'
.\scripts\local\invoke-screen-share-smoke.ps1 -SkipBuild `
  -VoiceScope 'VC Root / Landing' -SustainSeconds 90
```

The candidate policy requires saved-server connect, restart/reconnect, text and
voice rooms, chat, rich DM, mute/deafen and fail-safe PTT release. The release
policy additionally requires connected rich media, Watch Together, current
screen-share controls, read-only admin/ACL state, updater dry-run and the manual
voice attestation. Destructive admin actions are not run against a shared
community server.

The typed scenario results are assembled by
`scripts/windows/invoke-connected-product-release-gate.ps1`. Missing, skipped or
failed scenarios required by the selected policy fail closed.

### Track B: Windows release qualification

This track consumes the same candidate executable and runs in parallel with the
connected track:

1. Run the visual baseline gate, not `-CandidateOnly`.
2. Regenerate and lock a schema-v2 WebEngine performance reference on the same
   Windows reference machine; do not translate the old schema-v1 JSON.
3. Run five QML performance measurements with the candidate ID and source SHA.
4. Build/stage the packaging lane once and validate its runtime manifest,
   updater, helper, speech-cleanup and GStreamer payloads.
5. Generate `windows_msi_payload_evidence` with
   `scripts/windows/verify-windows-msi-payload.ps1` and require the MSI's
   administratively extracted `mumble.exe` SHA-256 to equal the frozen
   candidate executable before any installation starts. Pass that same
   evidence to the installer-upgrade and Windows community gates.
6. Verify fresh MSI install, previous-community-MSI upgrade, launch, settings
   preservation and uninstall in a disposable Windows VM.
7. Exercise updater prepare/commit, tampered-payload rejection and rollback.

The final evidence assembler is
`scripts/windows/invoke-windows-community-release-gate.ps1`. It validates the
candidate binding for visual, performance, connected and packaged artifacts.
It also requires a full, hash-bound result from
`scripts/windows/verify-windows-installer-upgrade.ps1`; `-ContractOnly` output
is useful for CI contract checks but is never release-eligible.

### Track C: Linux Murmur only

The `linux-server` CI job is the only qualifying Linux lane. One static Release
build tree uses `client=OFF`, `server=ON`, `tests=ON` and `screen-helper=OFF`,
runs the relevant CTest set against that tree, and uploads the deployable
`mumble-server`, CTest JUnit result and evidence manifest together. There is no
separate shared test binary and no Linux desktop qualification in this gate.

`scripts/linux/create-murmur-evidence.sh` fails closed when the candidate has
tracked worktree changes, when the server binary and `CMakeCache.txt` do not
come from the same build directory, or when the cache does not prove the exact
server-only, static, tested Release contract and build number. It also rejects
a build tree containing a Linux client or screen helper and records the
candidate Git SHA, server SHA-256, CMake cache SHA-256, configuration and CTest
JUnit result. The Windows executable hash is not shared across platforms; the
clean source Git SHA binds the Murmur result to the same candidate.

## Final promotion assembly

After the Windows community gate and the tested Linux Murmur lane are green,
assemble the immutable promotion evidence without rebuilding or publishing:

```powershell
.\scripts\windows\invoke-community-release-readiness-gate.ps1 `
  -CandidateManifestPath .\.tmp\community-release\candidate.json `
  -WindowsCommunityEvidencePath .\.tmp\community-release\windows-community-gate.json `
  -LinuxMurmurEvidencePath .\.tmp\community-release\linux-murmur-test-evidence.json `
  -OutputPath .\.tmp\community-release\promotion-evidence.json
```

The assembler verifies the clean release candidate again, binds the exact
candidate manifest and Windows executable SHA-256 to the complete Windows gate,
and requires passed Linux Murmur CTest evidence from the same source Git SHA,
the exact static configuration and the hash-bound CMake build contract. Missing,
dirty, development/candidate-only, mismatched, legacy or failed evidence is
written as a failed promotion result and exits non-zero. The command performs
no Git mutation, deployment or publication.

## Fast feedback order

Use the cheapest sufficient check after each blocker fix:

1. Affected unit, controller, QML component or Pester contract.
2. Targeted connected or visual scenario.
3. Four visual discovery shards after a blocker batch.
4. One central Windows build; never run competing relinks of `mumble.exe`.
5. Full visual, performance, package and installer gates only for a candidate.

Connected, Windows release qualification and Linux Murmur evidence may run at
the same time only after they have been given the same candidate identity.

## Release-train operating model

The freeze is run as one integration queue with three evidence owners, not as
three independent feature branches. One person or agent owns the central
Windows relink and candidate manifest. Other owners may prepare fixes and run
targeted tests, but they must not rebuild or replace the candidate executable
while a candidate is being qualified.

Every open item is classified before work starts:

- `P0 release blocker`: crash, data loss, stuck transmit, security issue,
  failed required gate or a core flow that cannot be completed.
- `P1 parity/polish blocker`: materially confusing navigation, missing product
  feedback, broken accessibility/focus, visual regression or poor fluency in a
  supported release flow.
- `P2 post-community`: new behavior, optional enhancement or refactor that is
  not required to ship the frozen product.

Only P0 and P1 items may change the candidate during the freeze. Each accepted
item must name its failing scenario or gate, contain the smallest practical
fix, and add or update a deterministic regression check. P2 work is recorded
outside the release branch and does not enter the current candidate.

Use this cadence:

1. Collect a small blocker batch while targeted tests run in parallel.
2. Let the integration owner perform one central build and create a new
   development candidate manifest.
3. Run affected visual shards and connected scenarios first.
4. When the batch is stable, run the complete Windows gate while Linux Murmur
   CI builds the same source revision.
5. Any source or binary change invalidates the candidate and returns to step 1;
   evidence is never patched forward to a different build.

Windows GPU work is exclusive on one reference machine. Visual capture,
connected UI automation, performance measurement and installer launch tests
must not overlap there. They are parallel work tracks, but their hardware runs
are serialized to preserve focus, process and timing integrity. Linux Murmur CI
is independent and may run throughout.

### Connected product owner

Owns real-server evidence for saved-server connect/reconnect, text and voice
rooms, chat, bidirectional and rich DM, self controls and PTT release, read-only
admin/ACL, screen share, Watch Together and updater dry-run. The final run uses
a Murmur binary built from the candidate source revision; a convenient older
dev server is useful for smoke testing but cannot qualify the release.

### Windows qualification owner

Owns visual/accessibility baselines, five-run performance evidence, staged
runtime validation, updater transactions and disposable-VM MSI qualification.
The owner consumes the already built candidate and must fail closed on a hash
or provenance mismatch. Performance is measured only while the reference
machine is otherwise idle.

### Linux Murmur owner

Owns only `client=OFF`, `server=ON`, `screen-helper=OFF`, the Murmur artifact and
server/shared CTest evidence. Linux desktop client failures are outside this
release train and must not hold the Windows-client community release.

## Community polish pass

Automation proves contracts; it does not decide whether the product feels
finished. Before the release candidate is cut, perform one connected manual
pass at the supported reference sizes and both themes. Record only actionable
P0/P1 findings and verify at least:

- a new user can understand connect, room selection and where text/voice state
  lives without knowing the old client;
- primary actions, context actions, dialogs and Settings have predictable
  navigation, focus return, cancel and error behavior;
- loading, empty, disabled, offline, permission-denied and partial-success
  states use the same design language as the normal state;
- chat, attachments, embeds, provider cards and detached media share the same
  typography, spacing, surfaces, progress and error semantics;
- screen sharing and Watch Together retain every frozen capability and have a
  clear start, active, reconnect, stop and failure path;
- no visible classic widget, WebChannel/main-shell dependency or unexpected
  Chromium process appears outside the documented plugin/OS/media allowlist.

The pass is complete when all P0 items are closed, remaining P1 items have an
explicit release decision, and two consecutive candidate runs produce no new
core-flow blocker. This is the point to create the clean `candidate_kind=release`
manifest and run the final three-track promotion assembly.

## Freeze and blocker policy

During the freeze, accepted changes are limited to:

- a reproducible release blocker or visible parity/polish defect;
- performance, accessibility, reliability or security fixes;
- test, packaging and release-evidence infrastructure;
- documentation that describes the frozen behavior.

New product features, protocol changes and unrelated refactors move to the
post-community backlog. A fix that changes the source or executable creates a
new candidate and invalidates older evidence.

## Promotion requirements

A community release is eligible only when all of the following are true:

- source is clean and the candidate manifest says `candidate_kind=release`;
- tracked visual baseline gate is green;
- connected `community-release` policy is green;
- Windows performance contract is green on the locked reference machine;
- Windows stage, MSI upgrade/uninstall and updater rollback are green;
- Linux server build and server/shared tests are green;
- manual review finds no unresolved blocker in navigation, chat, voice, DM,
  Settings, admin, screen share or provider playback.

The release workflow must promote the already verified immutable artifacts. It
must not rebuild a different binary while publishing.
