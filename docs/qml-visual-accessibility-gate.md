# Qt Quick visual and accessibility gate

`scripts/windows/invoke-qml-visual-matrix.ps1` is the full fail-closed Windows matrix runner. It starts one isolated client process per device-pixel-ratio group with `QT_SCALE_FACTOR`, a copied config, and a unique automation port and token. `invoke-qml-visual-gate.ps1` is its attach-mode worker: it verifies the existing `QQuickWindow`'s actual DPR and only runs cases matching that DPR. It never attempts to change DPR at runtime because DPR belongs to screen/process scale state and Qt exposes no truthful window-level setter.

The automation server must prove that it can set an exact Qt Quick fixture, resize the top-level `QQuickWindow`, override theme, capture the window, and serialize the QML accessibility tree. Every case records state, dimensions, actual DPR, SHA-256 hashes, and accessibility output in `manifest.json`.

The default matrix covers reference desktop and compact sizes, 1.0 and 1.5 device-pixel ratios, light and dark themes, and empty, loading, error, and connected states. A missing capability, state, capture, accessibility tree, dimension, hash, or baseline case fails the run. CI only compares candidates; it never updates a baseline.

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

## Required backend hook

The current automation server exposes `captureQml` and `qmlReadinessState`, but it does not expose deterministic whole-shell state injection, exact top-level sizing/theme control, actual window DPR, or an accessibility-tree snapshot. Therefore the gate intentionally fails at its first capability request today instead of reporting a partial pass.

The remaining frontend-neutral hook consists of three commands:

- `qmlVisualGateCapabilities`: returns `capture`, `state_injection`, `window_resize`, `theme_override`, `accessibility_snapshot`, `supported_states`, and the read-only `actual_device_pixel_ratio` of the active top-level window.
- `setQmlVisualGateState`: atomically applies `case_id`, `state`, `theme`, `layout`, and logical `width`/`height`, waits for a rendered frame, and returns the exact applied values, read-only `actual_device_pixel_ratio`, and a monotonically increasing `generation`.
- `qmlAccessibilitySnapshot`: returns a stable semantic tree for the requested generation, starting with a non-empty root `role`.

`captureQml` must also honor the requested generation so a stale frame cannot be accepted. Fixture state must be owned by frontend-neutral controllers/models rather than injected into QML object names. The backend must not add a synthetic DPR setter: process launch owns scale, while automation only reports and verifies the resulting actual DPR. Once these commands exist, the runner needs no QML-internal knowledge.
