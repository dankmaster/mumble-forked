# Qt Quick visual and accessibility gate

`scripts/windows/invoke-qml-visual-matrix.ps1` is the full fail-closed Windows matrix runner. It starts one isolated client process per device-pixel-ratio group with `QT_SCALE_FACTOR`, a copied config, and a unique automation port and token. `invoke-qml-visual-gate.ps1` is its attach-mode worker: it verifies the existing `QQuickWindow`'s actual DPR and only runs cases matching that DPR. It never attempts to change DPR at runtime because DPR belongs to screen/process scale state and Qt exposes no truthful window-level setter.

The checked-in matrix also crosses the real responsive breakpoint. It captures a compact 760 px shell with navigation closed and the minimum-width 420 px shell with the navigation drawer open. The open-drawer case must expose exactly one semantic `Rooms and participants` dialog in the accessibility tree.

The screenshot matrix uses Qt Quick's software backend and basic render loop to make captures complete and pixel-deterministic across Windows GPU drivers. This override is scoped to visual-gate child processes; production clients and performance measurements continue to use the normal accelerated renderer.

The automation server must prove that it can set an exact Qt Quick fixture, resize the top-level `QQuickWindow`, override theme, capture the window, and serialize the QML accessibility tree. Every case records state, dimensions, actual DPR, SHA-256 hashes, and accessibility output in `manifest.json`.

The default matrix covers reference desktop and compact sizes, 1.0 and 1.5 device-pixel ratios, light and dark themes, and empty, loading, error, and connected states. The HiDPI case uses a 960×600 logical window (1440×900 physical pixels at DPR 1.5) so Windows can honor the exact requested size on common 1080p runners. A missing capability, state, capture, accessibility tree, dimension, hash, or baseline case fails the run. CI only compares candidates; it never updates a baseline.

The reviewed Windows reference artifacts are tracked in `qml-visual-baseline/`. The Windows shared-client workflow builds a separate automation-enabled client only after publishing the release payload, then compares all matrix cases against that baseline. The shipped client and installer remain automation-disabled.

Run a gate after starting an automation-enabled QML client:

```powershell
.\scripts\windows\invoke-qml-visual-matrix.ps1 `
  -Executable .\build\mumble.exe `
  -ConfigPath .\.tmp\visual-source-settings.json `
  -BaselineManifestPath .\qml-visual-baseline\manifest.json
```

Attach mode is useful for diagnosis, but only for the current process DPR:

```powershell
.\scripts\windows\invoke-qml-visual-gate.ps1 `
  -AutomationPort 64799 -AutomationToken $env:MUMBLE_MODERN_AUTOMATION_TOKEN `
  -ExpectedDevicePixelRatio 1.5 `
  -BaselineManifestPath .\qml-visual-baseline\manifest.json
```

After reviewing every PNG and accessibility JSON file, baseline replacement is a separate, explicit operation with confirmation:

```powershell
.\scripts\windows\update-qml-visual-baseline.ps1 `
  -CandidateDirectory .\.tmp\qml-visual-gate `
  -BaselineDirectory .\qml-visual-baseline `
  -AcceptReviewedCandidates
```

No baseline or screenshot directory is created in the repository automatically.

When no reviewed baseline exists yet, generate candidates without claiming a passing gate:

```powershell
.\scripts\windows\invoke-qml-visual-matrix.ps1 `
  -Executable $executable `
  -ConfigPath "$env:LOCALAPPDATA\MumbleDevClient\state\mumble_settings.json" `
  -CandidateOnly `
  -OutputDirectory .\.tmp\qml-visual-candidates
```

Candidate mode is a separate PowerShell parameter set, writes `mode: candidate-only` into manifests, prints a warning, performs no baseline comparison, and cannot be combined with `-BaselineManifestPath`. It is evidence generation, never a green CI gate.

## Frontend-neutral backend hook

The automation server exposes the complete fail-closed hook through three commands:

- `qmlVisualGateCapabilities`: returns `capture`, `state_injection`, `window_resize`, `theme_override`, `accessibility_snapshot`, `supported_states`, and the read-only `actual_device_pixel_ratio` of the active top-level window.
- `setQmlVisualGateState`: atomically applies `case_id`, `state`, `theme`, `layout`, and logical `width`/`height`, waits for a rendered frame, and returns the exact applied values, read-only `actual_device_pixel_ratio`, and a monotonically increasing `generation`.
- `qmlAccessibilitySnapshot`: returns a stable semantic tree for the requested generation, starting with a non-empty root `role`.

`captureQml` honors the requested generation so a stale frame cannot be accepted. Fixture state is owned by frontend-neutral controllers/models rather than injected into QML object names. There is deliberately no synthetic DPR setter: process launch owns scale, while automation only reports and verifies the resulting actual DPR. The runner therefore needs no QML-internal object names or Windows UI Automation access.
