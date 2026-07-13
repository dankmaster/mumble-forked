# Archived classic QSS color audit

The March 2026 audit previously stored here described the retired QWidget QSS
themes under the repository's former `themes/` directory. The Windows product
client moved to a Qt Quick-only frontend in July 2026, and those QSS files,
their runtime loader, and their compiled resource collections were removed.

Current product colors are defined by the typed Modern theme pipeline:

- `src/mumble/ModernTheme.cpp` loads and validates Modern theme manifests.
- `src/mumble/UiTheme.cpp` provides built-in and native-window theme tokens.
- `src/mumble/QmlThemeController.cpp` publishes the active tokens to QML.
- `src/mumble/qml-shell/Theme.qml` is the QML design-system facade.

See `docs/theme-coverage-guide.md` and `docs/modern-custom-themes.md` for the
current review checklist and custom-theme format. Native tray/status icons are
kept separately under `icons/native/`; they are OS integration assets and are
not a product theme or stylesheet fallback.
