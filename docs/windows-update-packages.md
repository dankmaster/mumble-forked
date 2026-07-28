# Windows Update Packages

This document describes the Windows fork update-package path. The short
version is: use the update package as the normal in-app updater path, while
keeping the MSI as the canonical installer, recovery path, and verified
fallback if package apply fails.

## Current State

The `mumble-forked MSI Release` workflow produces:

- `mumble-forked-<version>.msi`
- `mumble-forked-<version>.msi.sha256`
- `mumble-forked-<version>.mumble-update`
- `mumble-forked-<version>.mumble-update.sha256`
- `mumble-forked-update-files.json`
- `mumble-forked-update.json`
- `changelog.md`
- release notes and preview text

The update manifest preserves `installerUrl` and top-level `sha256` for old
clients. Newer clients also understand `manifestVersion: 2`, structured
`installer` and `package` entries, and `preferredUpdate`.

Current releases use `preferredUpdate: "package"`. Package-capable clients
download and verify the package first. The package archive contains only files
that are new or changed relative to the previous published file manifest, while
its internal `manifest.json` always describes the complete target installation.
The separately published `mumble-forked-update-files.json` is the next
release's authenticated comparison input; it is not trusted by the client as a
substitute for the signed outer package identity.

Updater protocol v4 additionally
requires a signed channel-pointer-v2 recovery set: the candidate MSI and two
previous immutable MSI assets, each with an exact size and SHA-256. The
candidate also carries the SHA-256 of the exact signed `mumble.exe` extracted
from its MSI payload verification. The client
selects the installed build when it is present in that set, otherwise the
newest verified recovery MSI. Older clients ignore the additive package fields
and continue to use the MSI through the preserved top-level fields.

The public stable channel deliberately preserves the MSI as a fresh-install and
recovery artifact. A current client does not download the candidate MSI during
the normal package path. It fetches that verified MSI only if package download
or preparation fails, or after the native updater records a safe apply failure
and restarts the restored client. Cancellation never triggers fallback.

For the unsigned public `mumble-forked` bridge, the outer release manifest
advertises `minUpdaterVersion: 3` so build 84 can select the package. The
package's authenticated internal manifest still requires updater protocol 4.
Signed channel-pointer-v2 releases advertise version 4 in the outer pointer as
well and therefore require the complete known-good recovery MSI set before
handoff.

`mumble-updater.exe` supports both `--installer <msi>` and
`--package <mumble-update>`. Package mode now has a native prepare/commit split.
After Mumble downloads and verifies the outer package SHA-256, it launches the
copied updater with `--prepare --no-ui`; that prepare pass reads the inner
manifest, plans changed files, extracts and verifies only changed payload files
into a staged directory, and writes a prepared sidecar. When the user chooses
install/restart, the updater waits for Mumble to exit, commits the prepared
changed files with backups, records an installed manifest for the app directory,
and restarts `mumble.exe`. If the app directory is not writable, the copied
updater relaunches itself with `runas`. When package mode was launched with a
verified `--installer` fallback, package failure runs the MSI fallback; user
cancellation is kept distinct from failure and does not trigger the MSI
fallback. A protocol-v4 MSI handoff also supplies `--recovery-installer` and
its mandatory digest plus `--candidate-executable-sha256`. The updater persists that MSI outside the installed
payload before changing the machine and commits only after the restarted
client publishes its ten-second health marker.

The updater is self-contained. It links a private `/MT` zlib 1.3.1 target and
the build runs `dumpbin /dependents`; importing `zlib1.dll` is a hard failure.
The copied recovery updater therefore needs no sidecar zlib DLL.

Input-enhancement package trust is separate from Authenticode. A release
candidate must have a positive `BUILD_NUMBER` and the expected Ed25519 public
key compiled into `mumble.exe`; signed model, recipe, policy and channel
manifests are then verified with that exact key. The pre-Azure rehearsal uses
a test key created before the candidate build and runs the candidate's
`--write-input-enhancement-build-identity` diagnostic to attest the positive
build number and raw public-key SHA-256 before packaging evidence is accepted.
Build 0 is intentionally unmanaged and can be useful for local development,
but it cannot satisfy release qualification. In an unmanaged build without
local catalogs only Original/Light are available; strict unsigned local
catalogs may enable neural profiles for development, but are never release
evidence. Authenticode or future Azure signing does not make an unmanaged or
wrong-key build eligible.

## Goals

- Keep the MSI for fresh installs, repair, uninstall, Start Menu/registry
  ownership, and manual recovery.
- Add an update package artifact for the normal in-app update path.
- Keep old clients compatible by preserving the existing top-level
  `installerUrl` and `sha256` manifest fields.
- Let newer clients prefer the package when supported and fall back to MSI when
  the package is missing, unsupported, fails validation before handoff, or
  reports failure after handoff.
- Publish a complete target file manifest but transfer only changed/new files;
  no binary-diff format is required.
- Download the candidate MSI lazily, only after package failure.
- Distinguish user cancellation from failure so cancelled elevation or installer
  flows do not trigger the MSI fallback.
- Make the user-facing flow explicit: available, downloading with progress,
  ready to install/restart, applying, failed/retry.
- Make update reminders explicit: “Remind me next week” is the only persisted
  postponement. Closing the banner is session-only and the next startup check
  shows it again.
- Require cryptographic integrity for downloaded assets. Treat SHA-256 as
  integrity and signing as the long-term trust boundary.

## Non-Goals For The First Package Version

- No binary diffing or block-level patching.
- No background self-update while Mumble keeps running.
- No replacing a running `mumble-updater.exe` in place. The client should copy
  the updater to the update working directory before launching it, as it does
  for MSI handoff today.
- No removal of MSI publishing.

## Release Manifest

The public manifest should become additive. Existing clients continue to read
`installerUrl` and `sha256`; new clients can read structured installer and
package entries.

Example:

```json
{
  "manifestVersion": 2,
  "name": "mumble-forked",
  "version": "1.7.123",
  "build": 123,
  "commit": "0123456789abcdef0123456789abcdef01234567",
  "previousCommit": "fedcba9876543210fedcba9876543210fedcba98",
  "branch": "master",
  "announcement": "Update announcement shown in the client.",
  "releaseNotes": "Optional human-written notes.",
  "changelog": "Generated changelog.",
  "releaseUrl": "https://github.com/dankmaster/mumble/releases/tag/mumble-forked",
  "installerUrl": "https://github.com/dankmaster/mumble/releases/download/mumble-forked/mumble-forked-1.7.123.msi",
  "sha256": "<msi-sha256-for-old-clients>",
  "installer": {
    "format": "msi",
    "url": "https://github.com/dankmaster/mumble/releases/download/mumble-forked/mumble-forked-1.7.123.msi",
    "sha256": "<msi-sha256>",
    "size": 123456789
  },
  "package": {
    "format": "mumble-update-v1",
    "url": "https://github.com/dankmaster/mumble/releases/download/mumble-forked/mumble-forked-1.7.123.mumble-update",
    "sha256": "<package-sha256>",
    "size": 123456789,
    "minUpdaterVersion": 3,
    "applyMode": "replace-staged-payload",
    "requiresElevation": "auto",
    "payloadMode": "sparse",
    "payloadFileCount": 11,
    "targetFileCount": 2032,
    "removedFileCount": 0,
    "baseCommit": "fedcba9876543210fedcba9876543210fedcba98",
    "baseBuild": 122,
    "baseManifestSha256": "<previous-target-manifest-sha256>",
    "fileManifest": {
      "url": "https://github.com/dankmaster/mumble/releases/download/mumble-forked/mumble-forked-update-files.json",
      "sha256": "<complete-target-manifest-sha256>",
      "size": 234567
    }
  },
  "preferredUpdate": "package",
  "publishedAt": "2026-05-31T00:00:00.0000000Z"
}
```

Compatibility rules:

- `installerUrl` and top-level `sha256` continue to describe the MSI.
- `package` is optional and must be ignored by clients that do not understand
  it.
- `preferredUpdate` is advisory. The client still validates local support,
  platform, trust, and package fields before choosing package mode.
- `package.fileManifest` describes the complete target file set used to build
  the next release. The package's own SHA-256 and internal manifest remain the
  client admission boundary.
- The public compatibility manifest may advertise updater version 3 while its
  verified package internally requires version 4. Signed channel pointers use
  version 4 in both places and fail closed without their recovery set.
- A future signed manifest can wrap the same fields without changing the basic
  package decision logic.

For input-enhancement community releases, the signed channel pointer is that
wrapper. Schema v2 has a fail-closed recovery contract:

- `installer` identifies the candidate MSI and exact signed `mumble.exe` hash
  from the immutable candidate tag;
- `recoveryInstallers` contains exactly two distinct earlier immutable MSIs;
- `knownGoodTags` contains the candidate followed by those two recovery tags
  in the same order;
- every URL must be a GitHub release asset in the same repository and must
  match its declared tag and file name;
- package selection normalizes `minUpdaterVersion` to 4. A v4 package without
  the complete recovery set is not installable and is not silently downgraded.

The first schema-v2 publication can bootstrap from a signed schema-v1 pointer
only by supplying an explicit schema-v1 bootstrap file with exactly two
records (`immutableTag`, `fileName`, `size`, `sha256`). Missing, duplicate or
extra metadata fails closed; an incomplete schema-v2 pointer is never emitted.

## Package Artifact

The first package should be a normal archive with a dedicated extension, for
example:

```text
mumble-forked-1.7.123.mumble-update
```

The artifact is a ZIP archive generated from the already staged Windows payload
at:

```text
build-shared-webengine/shared-webengine-stage/
```

The internal manifest contains every target file. `payload/` contains only the
changed/new subset relative to the verified previous target manifest:

```text
manifest.json
payload/
  mumble.exe
  mumble-updater.exe
  qml/changed-module.dll
```

Internal `manifest.json` example:

```json
{
  "manifestVersion": 1,
  "format": "mumble-update-v1",
  "packageId": "mumble-forked",
  "version": "1.7.123",
  "build": 123,
  "commit": "0123456789abcdef0123456789abcdef01234567",
  "minUpdaterVersion": 4,
  "applyMode": "replace-staged-payload",
  "createdAt": "2026-05-31T00:00:00.0000000Z",
  "files": [
    {
      "path": "mumble.exe",
      "size": 12345678,
      "sha256": "<file-sha256>"
    }
  ]
}
```

The outer manifest verifies the downloaded package as one blob. The inner
manifest verifies the complete desired installation. During prepare, an omitted
target file is accepted only when the installed file already has the target
size and SHA-256. If it differs, prepare fails before mutation because the
sparse archive cannot supply it. This makes the package safe for a client on an
unexpected base: the client falls back to the verified MSI rather than applying
an incomplete update.

Files present in the previous installed manifest but absent from the new target
are stale managed files. They are removed only after local-modification checks
and remain covered by rollback.

## Client Selection Logic

New client behavior:

1. Fetch and normalize `mumble-forked-update.json`.
2. If `package` is present, trusted, supported by this client, and has a valid
   SHA-256, choose package mode.
3. For a v4 package, require channel-pointer schema v2 with the exact candidate
   MSI and exactly two previous recovery MSIs.
4. Download and prepare the package without downloading the candidate MSI.
5. If package download/preparation fails, download and verify the candidate MSI
   and present it as the ready update.
6. If native apply fails after Mumble closes, the updater rolls back, records an
   installation-scoped fallback request bound to that package SHA-256, and
   restarts Mumble. The next update attempt downloads the verified MSI.
7. Otherwise use the existing MSI mode for a legacy manifest or client.
8. Otherwise show the release URL as a manual fallback.

The Modern shell banner should not need a second UX model. It can keep the same
states and actions:

- available
- downloading with progress
- ready
- applying / installing
- failed with retry
- remind next week
- closed for this session

Only the labels should vary:

- package mode: "Download update", "Install and restart"
- MSI fallback: "Download installer", "Install and restart"

## Updater Apply Flow

`mumble-updater.exe` has a second mode beside `--installer`:

```text
mumble-updater.exe --package <path> --app <path> --working-dir <dir> --parent-pid <pid> ...
```

Current package-mode flow:

1. Parse package arguments and log paths.
2. Wait for the parent Mumble process to exit.
3. Prepare mode reads the internal package manifest and compares it with the
   installed manifest or current app files while Mumble is still running.
4. Prepare mode extracts and verifies changed payload files into
   `prepared-packages/<package-sha256>/`.
5. Commit mode validates the prepared sidecar against the package SHA-256 and
   requested app path.
6. Detect whether the app directory is writable.
7. If elevation is required, relaunch the copied updater with `runas` and the
   same package arguments.
8. Create backups only for files that will be replaced.
9. Copy changed staged payload files into the app directory.
10. Verify copied file sizes and record the installed manifest.
11. Restart `mumble.exe` and wait for update-health schema v3. The marker binds
    updater protocol v4, transaction ID, package identity, the actual running
    executable SHA-256, app path, settings
    load, manifest verification, audio initialization and at least ten seconds
    of stable runtime.
12. Commit only after a valid marker. Crash, process kill, failed audio init,
    unreadable journal, timeout or reboot resumes the durable recovery path.
13. Native packages restore their verified file snapshot. MSI transactions
    reinstall the already-downloaded, verified known-good MSI and never accept
    a mixed payload as success.

The pending journal records whether the transaction is `native-package` or
`windows-installer`, the expected candidate executable SHA-256, the recovery
MSI path/size/hash, the Windows kernel boot-session identity, and the 3010
reboot flag. A candidate 3010 can never enter health probation; it fails closed
into known-good recovery. A successful recovery in the same boot remains
non-terminal because deferred candidate operations can still win at restart.
If recovery itself returns 3010, the journal is rebound to that boot session;
the journal and startup recovery stay armed until a later, different boot
session verifies a terminal exact-known-good state.
Its persistent per-user recovery trigger is removed only after a terminal
committed or rolled-back state is durably written.

## Install Location Strategy

There are two viable strategies.

### Phase 1: Elevated In-Place Payload Replace

This is the smallest production change. It works with the current MSI-installed
layout, including `Program Files`, by prompting for elevation when the install
directory is not writable.

Pros:

- Does not require redesigning the production install layout.
- Keeps the MSI as the owner of shortcuts, registry, and uninstall metadata.
- Lets the package path ship sooner.

Cons:

- Still has a UAC prompt for `Program Files`.
- Replacing many files in place needs careful rollback and verification.
- The MSI database will not know every file's new version until a later MSI
  install/repair.

### Phase 2: Versioned App Directories

The nicer long-term shape is a versioned app layout:

```text
Mumble/
  apps/
    1.7.123/
    1.7.124/
  current-app.txt
  MumbleLauncher.exe
```

The updater installs the new payload into a fresh app directory, verifies it,
then switches a pointer or launcher target.

Pros:

- More atomic update switch.
- Easier rollback.
- Cleaner package model.

Cons:

- Requires installer/layout changes.
- Needs a launcher or stable entry point.
- Needs migration and cleanup policy for old app versions.

The dev client already uses a similar `apps/<timestamp>` plus `current-app.txt`
layout, so it is a good local proving ground for this phase.

## Release Workflow Changes

The `mumble-forked MSI Release` workflow includes a package step after the
shared payload has been staged and validated:

1. Read `build-shared-webengine/shared-webengine-stage`.
2. Verify the staged payload includes the pinned GStreamer runtime and that the
   packaged helper reports GStreamer LiveKit publish/view capability.
3. Download and verify the previous standalone target file manifest. For the
   one-time bridge from a release that predates that asset, verify the previous
   full package and extract only its `manifest.json`.
4. Generate the complete target manifest with file sizes and SHA-256 hashes.
5. Copy only changed/new files into `payload/`.
6. Archive `manifest.json` and the sparse `payload/`.
7. Publish the complete target manifest as
   `mumble-forked-update-files.json`.
8. Hash the final `.mumble-update` archive.
9. Add package/base/file-manifest fields to `mumble-forked-update.json`.
10. Upload the package and target manifest beside the MSI.
11. Include both in the workflow artifact.

The existing MSI asset, MSI `.sha256`, `changelog.md`, and manifest upload stay
in place.

## Package Default Policy

Releases use:

```json
{
  "preferredUpdate": "package"
}
```

New clients choose package mode when the package fields validate and do not
download the candidate MSI unless the package path fails. Older clients ignore
the package fields and continue to use the MSI through the preserved
`installerUrl` and top-level `sha256`.

### Bridge From Build 84 And Older Fork Clients

The first sparse-capable release is also a migration release:

- Build 84 already understands the full-target-manifest/sparse-payload archive
  semantics. Its older frontend still downloads the MSI eagerly, so it can
  safely fall back during this one transition.
- The new client installed by that release contains lazy MSI fallback. Every
  later normal update therefore transfers only its sparse package unless
  recovery is actually needed.
- A package-capable client on an unexpected or much older base fails during
  prepare before any installation mutation. Existing eager-fallback clients
  already have the verified MSI; new clients download it at that point.
- Fork clients too old to understand package fields keep reading the preserved
  top-level MSI URL and SHA-256.
- A manual MSI remains the final migration path for clients older than these
  additive manifest contracts.

## Implementation Phase Ledger

The package path is implemented enough to be the preferred fork update mode.
Keep this section as a ledger for what exists and what still needs hardening.

### Phase 0: MSI UX Baseline

Status: complete.

Keep the current banner-based MSI updater as the user-visible baseline. It gives
users clear progress and clear install/restart actions while the package work is
being built.

### Phase 1: Package Artifact And Manifest

Status: complete for `mumble-forked` releases.

- Add a script that creates `.mumble-update` from
  `shared-webengine-stage`.
- Generate the internal package manifest.
- Publish the package as a release asset.
- Extend `mumble-forked-update.json` with additive package fields.
- Publish package-capable releases with `preferredUpdate: "package"`.

Acceptance checks:

- Package exists in the release assets.
- Package SHA-256 in the public manifest matches the uploaded archive.
- Internal file hashes match the archive contents.
- Existing clients still install via MSI.

### Phase 2: Client Package Detection And Download

Status: implemented.

- Add package field parsing to `VersionCheck`.
- Prefer package mode when all package fields validate.
- Download package to the existing `Updates` directory.
- Verify outer package SHA-256.
- Keep MSI fallback when package fields are missing or invalid.
- Fetch the verified MSI only after package failure.
- Surface the selected mode in automation/debug summaries.

Acceptance checks:

- New clients choose package mode for a valid package manifest.
- New clients fall back to MSI for old manifests.
- Bad package hash fails before handoff and keeps retry/details actions useful.
- A safe native failure is bound to the exact package SHA-256 and selects MSI
  mode on the next attempt.

### Phase 3: Updater Package Mode

Status: implemented with protocol-v4 MSI health qualification and recovery.

- Add `--package` mode to `mumble-updater.exe`.
- Add archive extraction and internal manifest verification.
- Apply payload to the app directory with backup and rollback.
- Restart Mumble after a successful apply.
- Log each major step to `mumble-updater.log`.
- If package apply fails and a verified MSI fallback was supplied, run the MSI.
- Treat cancellation exit codes separately from failure and do not launch the
  fallback MSI after cancellation.
- Persist and verify a known-good MSI before candidate installation, including
  recovery across health timeout, process termination, reboot and exit 3010.

Acceptance checks:

- Package update works when the app directory is writable.
- Package update prompts for elevation when required.
- Failed extraction, failed hash validation, and failed copy leave clear logs.
- Legacy `--installer` mode still works; v4 `--installer` plus
  `--recovery-installer` is health-qualified and rollback-capable.
- Cancelled elevation or installer flows are reported as cancelled, not failed.

### Phase 4: Local And Automated Verification

Status: active hardening area.

- Add a local package smoke path for the synced dev client.
- Add CI validation that builds a package and verifies its manifest.
- Add updater dry-run or test helper coverage for package argument parsing and
  manifest validation.
- Verify exact sparse payload selection, complete target parity, base-manifest
  digest binding, tamper rejection, and unexpected-base failure before mutation.
- Exercise the Modern banner mockups for package-selected states.

Acceptance checks:

- CI fails if the package is missing `mumble.exe`, `mumble-updater.exe`,
  Qt runtime files, or optional runtime files expected by the staged payload.
- CI fails if `mumble-updater.exe` imports `zlib1.dll`.
- Local dev update can apply a package to an isolated writable app directory.
- Package and MSI paths both remain visible in automation diagnostics.

### Phase 5: Signing And Versioned Layout

Status: Azure/Authenticode intentionally deferred until the same unsigned
candidate passes protected quality, release rehearsal, recovery and dogfood.

- Add code signing for MSI, updater, and package assets when certificates are
  available.
- Add manifest/package signatures, or signed release metadata, so SHA-256 is not
  the only trust boundary.
- Introduce versioned production app directories if we want atomic switch and
  cleaner rollback.
- Consider block-level binary diffs only if changed-file packages are still too
  large; they are not required for sparse release payloads.

## Remaining Decisions

- Package trust: the current internal version uses SHA-256 integrity like the
  MSI path; signing is still the long-term trust boundary.
- Production layout: whether to keep the v1 in-place replacement path long-term,
  or move to versioned app directories for more atomic updates.
- Binary deltas: deferred. The current sparse package already avoids transferring
  every unchanged runtime file.
