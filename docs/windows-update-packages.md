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
- `mumble-forked-update.json`
- `changelog.md`
- release notes and preview text

The update manifest preserves `installerUrl` and top-level `sha256` for old
clients. Newer clients also understand `manifestVersion: 2`, structured
`installer` and `package` entries, and `preferredUpdate`.

Current releases use `preferredUpdate: "package"`. Package-capable clients
download and verify the package first; when installer metadata is available,
they also prepare the MSI as the fallback that `mumble-updater.exe` can run if
the package updater reports failure. Older clients ignore the package fields and
continue to use the MSI through the preserved top-level fields.

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
fallback.

## Goals

- Keep the MSI for fresh installs, repair, uninstall, Start Menu/registry
  ownership, and manual recovery.
- Add an update package artifact for the normal in-app update path.
- Keep old clients compatible by preserving the existing top-level
  `installerUrl` and `sha256` manifest fields.
- Let newer clients prefer the package when supported and fall back to MSI when
  the package is missing, unsupported, fails validation before handoff, or
  reports failure after handoff.
- Distinguish user cancellation from failure so cancelled elevation or installer
  flows do not trigger the MSI fallback.
- Keep the first package format simple: full staged payload first, delta
  packages later only if bandwidth becomes a real problem.
- Make the user-facing flow explicit: available, downloading with progress,
  ready to install/restart, applying, failed/retry.
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
    "minUpdaterVersion": 2,
    "applyMode": "replace-staged-payload",
    "requiresElevation": "auto"
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
- A future signed manifest can wrap the same fields without changing the basic
  package decision logic.

## Package Artifact

The first package should be a normal archive with a dedicated extension, for
example:

```text
mumble-forked-1.7.123.mumble-update
```

The simplest implementation is a ZIP archive containing the already staged
Windows payload from:

```text
build-shared-webengine/shared-webengine-stage/
```

Suggested archive layout:

```text
manifest.json
payload/
  mumble.exe
  mumble-updater.exe
  Qt6Core.dll
  gstreamer/
    bin/
    lib/gstreamer-1.0/
    libexec/
  ...
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
  "minUpdaterVersion": 2,
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
manifest verifies extracted files before they are copied into the app directory.

## Client Selection Logic

New client behavior:

1. Fetch and normalize `mumble-forked-update.json`.
2. If `package` is present, trusted, supported by this client, and has a valid
   SHA-256, choose package mode.
3. If package mode is selected and the MSI metadata is valid, download and
   verify the MSI as the package-failure fallback before handoff.
4. Otherwise use the existing MSI mode if `installerUrl` and top-level `sha256`
   are valid.
5. Otherwise show the release URL as a manual fallback.

The Modern shell banner should not need a second UX model. It can keep the same
states and actions:

- available
- downloading with progress
- ready
- applying / installing
- failed with retry
- dismissed

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
11. Restart `mumble.exe`.
12. Leave logs and rollback data in the update directory.

For the first version, rollback can be conservative:

- Before copying a changed file, move or copy the previous file into a timestamp
  backup directory.
- If the copy phase fails, restore files from that backup.
- If restart fails, leave the backup and logs for diagnostics instead of trying
  to infer app health.

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
3. Generate the internal package manifest with file sizes and SHA-256 hashes.
4. Archive `manifest.json` and `payload/`.
5. Hash the final `.mumble-update` archive.
6. Add package fields to `mumble-forked-update.json`.
7. Upload the package beside the MSI.
8. Include the package in the workflow artifact.

The existing MSI asset, MSI `.sha256`, `changelog.md`, and manifest upload stay
in place.

## Package Default Policy

Releases use:

```json
{
  "preferredUpdate": "package"
}
```

New clients choose package mode when the package fields validate. Before
handoff, they also download and verify the MSI fallback when the manifest
includes installer metadata. Older clients ignore the package fields and
continue to use the MSI through the preserved `installerUrl` and top-level
`sha256`.

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
- Prepare the verified MSI fallback beside the package when package mode is
  selected and installer metadata is valid.
- Surface the selected mode in automation/debug summaries.

Acceptance checks:

- New clients choose package mode for a valid package manifest.
- New clients fall back to MSI for old manifests.
- Bad package hash fails before handoff and keeps retry/details actions useful.
- Package handoff passes the verified MSI fallback to `mumble-updater.exe`.

### Phase 3: Updater Package Mode

Status: implemented with MSI fallback.

- Add `--package` mode to `mumble-updater.exe`.
- Add archive extraction and internal manifest verification.
- Apply payload to the app directory with backup and rollback.
- Restart Mumble after a successful apply.
- Log each major step to `mumble-updater.log`.
- If package apply fails and a verified MSI fallback was supplied, run the MSI.
- Treat cancellation exit codes separately from failure and do not launch the
  fallback MSI after cancellation.

Acceptance checks:

- Package update works when the app directory is writable.
- Package update prompts for elevation when required.
- Failed extraction, failed hash validation, and failed copy leave clear logs.
- Existing `--installer` MSI mode still works.
- Cancelled elevation or installer flows are reported as cancelled, not failed.

### Phase 4: Local And Automated Verification

Status: active hardening area.

- Add a local package smoke path for the synced dev client.
- Add CI validation that builds a package and verifies its manifest.
- Add updater dry-run or test helper coverage for package argument parsing and
  manifest validation.
- Exercise the Modern banner mockups for package-selected states.

Acceptance checks:

- CI fails if the package is missing `mumble.exe`, `mumble-updater.exe`,
  `zlib1.dll` for the copied updater, Qt runtime files, or optional runtime
  files expected by the staged payload.
- Local dev update can apply a package to an isolated writable app directory.
- Package and MSI paths both remain visible in automation diagnostics.

### Phase 5: Signing And Versioned Layout

Status: future work.

- Add code signing for MSI, updater, and package assets when certificates are
  available.
- Add manifest/package signatures, or signed release metadata, so SHA-256 is not
  the only trust boundary.
- Introduce versioned production app directories if we want atomic switch and
  cleaner rollback.
- Consider delta packages only after the full package flow is stable.

## Remaining Decisions

- Package trust: the current internal version uses SHA-256 integrity like the
  MSI path; signing is still the long-term trust boundary.
- Production layout: whether to keep the v1 in-place replacement path long-term,
  or move to versioned app directories for more atomic updates.
- Delta packages: still deferred until the full package flow is boring and
  bandwidth pressure is real.
