# Qt Quick Theme Coverage Guide

The supported Windows desktop product UI is themed through typed C++ tokens exposed directly to QML. Product UI must not depend on HTML, CSS, JavaScript, WebChannel, or a QWidget theme bridge. Retained Linux/macOS client builds are diagnostic and non-gating.

## Runtime pipeline

1. `Settings::qsModernShellTheme` identifies a built-in theme or a `custom:<id>` manifest.
2. `ModernTheme` validates `*.mumble-theme.json` manifests and preserves profile-local legacy CSS only as transitional import input.
3. `activeUiThemeTokens()` resolves the active palette and custom accent overlay.
4. `QmlThemeController` maps the active palette and manifest metrics to typed `QColor` and integer properties.
5. `QmlShellHost` exposes that controller as the C++-owned `uiTheme` context property.
6. The `Theme.qml` singleton binds its stable public property names to `uiTheme` and supplies safe startup defaults.

Settings apply, application palette changes, and OS theme changes refresh the controller. Refresh is idempotent: `themeChanged` is emitted only when a published token or metric changes.

## Sources of truth

- Built-in palette values and runtime aliases: `src/mumble/UiTheme.h` and `src/mumble/UiTheme.cpp`.
- Custom manifest parsing and validation: `src/mumble/ModernTheme.h` and `src/mumble/ModernTheme.cpp`.
- QML-facing typed state: `src/mumble/QmlThemeController.h` and `src/mumble/QmlThemeController.cpp`.
- Stable QML token names and safe defaults: `src/mumble/qml-shell/Theme.qml`.
- Bundled custom-theme examples: `src/mumble/themes/*.mumble-theme.json`.
- User authoring contract: `docs/modern-custom-themes.md`.

## QML token contract

Use `Theme` properties instead of color literals for product chrome:

- Surfaces: `shellBackground`, `panel`, `rail`, `strip`, `divider`, `selected`.
- Text: `textStrong`, `textMain`, `textMuted`.
- Interaction: `accent`, `focus`.
- Semantic state: `danger`, `success`, `warning`.
- Layout: `shellRadius`, `innerRadius`, `spacing`.

Hardcoded colors are acceptable only for content whose color is intrinsic or for a narrowly defined protocol/media signal. Repeated functional colors belong in the typed controller.

## Adding a token

1. Add the canonical value or alias to `UiThemeTokens` or the typed custom-manifest structure.
2. Add a `Q_PROPERTY` to `QmlThemeController` and include it in the idempotent comparison/update.
3. Expose a stable property in `Theme.qml` with a safe fallback.
4. Replace product literals with that `Theme` property.
5. Add controller tests for mapping and signal behavior.
6. Check dark, light, custom manifest, custom accent, high DPI, disabled, hover, focus, and error states.

## Review checklist

- Main shell, rails, timeline, composer, dialogs, menus, banners, tools, media and screen-share windows use `Theme`.
- Rich previews, provider embeddings, media chrome, and their loading, empty,
  error, disabled, and fallback states use the same tokens and interaction rules.
- Focus rings and keyboard navigation remain visible in dark and light themes.
- Muted text and dividers retain sufficient contrast.
- Semantic success/warning/danger colors are not confused with selection or accent.
- Theme changes apply without recreating the QML engine or resetting models.
- Reapplying identical settings does not emit `themeChanged`.
- Invalid manifests fall back safely and never inject executable styling.
- Packaged builds contain JSON manifests but no product HTML/CSS/JS resources.

## Verification

Run the controller and manifest tests, lint the QML module, build the client, and run the strict Modern-only verifier. Screenshot review should cover dark, light and a custom manifest at the reference DPI sizes. This checklist defines required coverage; source and controller tests alone do not prove visual parity.
