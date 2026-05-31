# Windows Update Packages

This document describes the Windows fork update-package path. The short
version is: keep the MSI as the canonical installer and recovery path, but add
a first-class update package that the in-app updater can download, verify,
apply, and restart through `mumble-updater.exe`.

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

Bootstrap releases keep `preferredUpdate: "installer"` so current users get the
MSI once and receive the package-capable client and updater. A later release can
switch to `preferredUpdate: "package"` without removing the MSI fallback.

`mumble-updater.exe` supports both `--installer <msi>` and
`--package <mumble-update>`. Package mode waits for Mumble to exit, verifies the
inner package manifest and file hashes, backs up replaced files, copies the full
payload into the app directory, verifies the result, and restarts `mumble.exe`.
If the app directory is not writable, the copied updater relaunches itself with
`runas`.

## Goals

- Keep the MSI for fresh installs, repair, uninstall, Start Menu/registry
  ownership, and manual recovery.
- Add an update package artifact for the normal in-app update path.
- Keep old clients compatible by preserving the existing top-level
  `installerUrl` and `sha256` manifest fields.
- Let newer clients prefer the package when supported and fall back to MSI when
  the package is missing, unsupported, or fails validation before handoff.
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
  "preferredUpdate": "installer",
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
2. If `preferredUpdate` is `package`, and `package` is present, trusted,
   supported by this client, and has a valid SHA-256, choose package mode.
3. Otherwise use the existing MSI mode if `installerUrl` and top-level `sha256`
   are valid.
4. Otherwise show the release URL as a manual fallback.

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

`mumble-updater.exe` should grow a second mode beside `--installer`:

```text
mumble-updater.exe --package <path> --app <path> --working-dir <dir> --parent-pid <pid> ...
```

Proposed package-mode flow:

1. Parse package arguments and log paths.
2. Wait for the parent Mumble process to exit.
3. Extract the archive into a temporary directory under the update working
   directory.
4. Read the internal package manifest.
5. Verify every listed file exists, has the expected size, and matches SHA-256.
6. Detect whether the app directory is writable.
7. If elevation is required, relaunch the copied updater with `runas` and the
   same package arguments.
8. Create a backup manifest for files that will be replaced.
9. Copy payload files into the app directory.
10. Verify copied files.
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

The `mumble-forked MSI Release` workflow should add a package step after the
shared payload has been staged and validated:

1. Read `build-shared-webengine/shared-webengine-stage`.
2. Generate the internal package manifest with file sizes and SHA-256 hashes.
3. Archive `manifest.json` and `payload/`.
4. Hash the final `.mumble-update` archive.
5. Add package fields to `mumble-forked-update.json`.
6. Upload the package beside the MSI.
7. Include the package in the workflow artifact.

The existing MSI asset, MSI `.sha256`, `changelog.md`, and manifest upload stay
in place.

## Bootstrap Release Policy

The first release that ships package support should still use:

```json
{
  "preferredUpdate": "installer"
}
```

That makes the release a bootstrap: users install the MSI once, and that MSI
places the package-capable `mumble-updater.exe` beside the client. The workflow
still publishes the `.mumble-update` asset and its manifest metadata so the
package track can be inspected and smoke-tested immediately.

The next release can change only:

```json
{
  "preferredUpdate": "package"
}
```

New clients then choose package mode when the package fields validate. Older
clients ignore the package fields and continue to use the MSI through the
preserved `installerUrl` and top-level `sha256`.

## Implementation Phases

### Phase 0: MSI UX Baseline

Keep the current banner-based MSI updater as the user-visible baseline. It gives
users clear progress and clear install/restart actions while the package work is
being built.

### Phase 1: Package Artifact And Manifest

- Add a script that creates `.mumble-update` from
  `shared-webengine-stage`.
- Generate the internal package manifest.
- Publish the package as a release asset.
- Extend `mumble-forked-update.json` with additive package fields.
- Keep bootstrap client behavior on MSI with `preferredUpdate: "installer"`.

Acceptance checks:

- Package exists in the release assets.
- Package SHA-256 in the public manifest matches the uploaded archive.
- Internal file hashes match the archive contents.
- Existing clients still install via MSI.

### Phase 2: Client Package Detection And Download

- Add package field parsing to `VersionCheck`.
- Prefer package mode only when `preferredUpdate` is `package` and all package
  fields validate.
- Download package to the existing `Updates` directory.
- Verify outer package SHA-256.
- Keep MSI fallback when package fields are missing or invalid.
- Surface the selected mode in automation/debug summaries.

Acceptance checks:

- New clients choose package mode for a valid package manifest.
- New clients fall back to MSI for old manifests.
- Bad package hash fails before handoff and keeps retry/details actions useful.

### Phase 3: Updater Package Mode

- Add `--package` mode to `mumble-updater.exe`.
- Add archive extraction and internal manifest verification.
- Apply payload to the app directory with backup and rollback.
- Restart Mumble after a successful apply.
- Log each major step to `mumble-updater.log`.

Acceptance checks:

- Package update works when the app directory is writable.
- Package update prompts for elevation when required.
- Failed extraction, failed hash validation, and failed copy leave clear logs.
- Existing `--installer` MSI mode still works.

### Phase 4: Local And Automated Verification

- Add a local package smoke path for the synced dev client.
- Add CI validation that builds a package and verifies its manifest.
- Add updater dry-run or test helper coverage for package argument parsing and
  manifest validation.
- Exercise the Modern banner mockups for package-selected states.

Acceptance checks:

- CI fails if the package is missing `mumble.exe`, `mumble-updater.exe`, Qt
  runtime files, or optional runtime files expected by the staged payload.
- Local dev update can apply a package to an isolated writable app directory.
- Package and MSI paths both remain visible in automation diagnostics.

### Phase 5: Signing And Versioned Layout

- Add code signing for MSI, updater, and package assets when certificates are
  available.
- Add manifest/package signatures, or signed release metadata, so SHA-256 is not
  the only trust boundary.
- Introduce versioned production app directories if we want atomic switch and
  cleaner rollback.
- Consider delta packages only after the full package flow is stable.

## Open Decisions

- Package extension: `.mumble-update` is descriptive, but `.mumblepkg` is shorter.
- Elevation UX: use `ShellExecuteExW(..., "runas", ...)` from the copied updater,
  or always let the client perform the elevation handoff before quitting.
- Package trust: whether the first internal version is SHA-256 only like the MSI
  path, or whether package mode should wait for signing.
- Production layout: whether to keep the v1 in-place replacement path long-term,
  or move to versioned app directories for more atomic updates.
