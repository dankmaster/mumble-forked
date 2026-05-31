# Theme Coverage Guide

Date: 2026-05-30

Use this guide whenever a settings theme, Modern shell theme, or accent change
touches visible UI. The goal is that changing a theme in settings recolors the
whole client experience, not only the widgets that happened to be closest to
the original change.

## Theme Pipeline

Mumble currently has two related theme systems:

- Classic Qt theme selection uses `Settings::{styleType, themeName,
  themeStyleName, themeDarkName, themeDarkStyleName}`. `Themes::apply()` loads
  `themes/<theme>/<style>.qss` and sets the application stylesheet.
- Runtime Qt styling reads `activeUiThemeTokens()` from `src/mumble/UiTheme.cpp`.
  This is the token bridge for custom C++ painting, runtime palettes, injected
  QSS, text documents, and native title-bar colors.
- Modern shell settings use `Settings::qsModernShellTheme`,
  `qsModernShellAccent`, `qsModernShellDensity`, and `qsModernShellRailSide`.
  `MainWindow.cpp` and `ModernSettingsController.cpp` serialize those settings
  as `uiTweaks`; `modern-shell/app.js` and `modern-shell/dialog.js` apply them
  to `document.documentElement.dataset.theme` and CSS custom properties.

Changing only one layer is not enough. A complete theme pass checks Qt/QSS,
runtime C++ painting, native window chrome, and Modern shell CSS.

## Source Of Truth

- Add or adjust runtime colors in `src/mumble/UiTheme.h` and
  `src/mumble/UiTheme.cpp` first.
- Add or adjust Modern shell CSS variables in
  `src/mumble/modern-shell/styles.css` under `:root` and the matching
  `:root[data-theme="..."]` block.
- If a Modern shell theme option is added or renamed, update every allow-list:
  `ModernSettingsController.cpp`, `MainWindow.cpp`, `modern-shell/app.js`, and
  `modern-shell/dialog.js`.
- If a Qt theme/style is added or renamed, update the QSS files under
  `themes/` and make sure `activeUiThemeTokens()` can resolve it if any C++
  runtime surface needs tokens for that theme.

Avoid introducing new ad hoc color literals in product UI code. Use
`UiThemeTokens`, `uiThemeColorWithAlpha(...)`, `uiThemeQssColor(...)`,
`QPalette`, or CSS custom properties. Hardcoded colors are allowed only when the
color is content or a functional signal rather than application chrome.

## Qt Surface Checklist

When touching Qt-side theming, check these surfaces:

- Main window background, native frame/title bar, menu bar, toolbars, status
  area, dock widgets, dock title bars, splitters, scrollbars, and focus rings.
- Server navigator: voice tree, text-room list, active/hover rows, current-room
  marker, section headers, MOTD block, footer/self presence, and empty states.
- Persistent chat: history viewport, log fallback view, message groups,
  sender/time labels, system rows, search matches, link preview cards, image
  preview widgets, composer, reply bar, attach/send controls, and separators.
- Dialogs still backed by Qt widgets: Connect, Config, Search, Certificate,
  Audio Wizard, Audio Stats, Ban/ACL/Tokens/User dialogs, Plugin Updater,
  Responsive Image dialog, PTT button window, Talking UI, and any fallback
  prompt that can still open outside the Modern shell.
- Native or platform-adjacent UI: Windows title-bar color, native modal
  container backgrounds, tray-related popups where applicable, and WebEngine
  view background colors before content loads.

For custom widgets and delegates, styling the parent is rarely enough. Also set
the widget palette, viewport palette, `setAutoFillBackground(true)` where the
widget paints its own background, and any delegate colors used during painting.

## Modern Shell Checklist

When touching `src/mumble/modern-shell/styles.css`, every visible color should
come from a CSS custom property unless it is an explicit functional exception.
Check these surfaces for every theme:

- Body/app shell, window header, app/menu bands, action buttons, focus outline,
  native form controls, scrollbars, and selection states.
- Utility rail: server identity card, MOTD/note card, text/voice room sections,
  room rows, active/joined/hover states, badges, overflow fades, and compact
  rail overlay.
- Conversation pane: header, ticker/banner, search bar, MOTD banner, message
  list, day dividers, system messages, incoming/self bubbles, reply previews,
  reactions, link cards, provider-specific media cards, image viewer, composer,
  jump-to-latest, attach/send buttons, and disabled/empty states.
- Popups: app menu, self menu, context menus, direct-message tray, toasts,
  reaction picker, custom selects, and any floating panel positioned from JS.
- Modern dialogs: backdrop, dialog shell, settings rail, settings footer,
  section cards, fields, validation states, ranges/meters, connect favorites
  and editor, stonks views, destructive/warning actions, and close controls.

Provider-branded previews may keep provider colors only inside the preview
content itself. The card shell, borders, text hierarchy, hover/selected states,
and surrounding controls still need theme variables.

## Functional Exceptions

These color sources can stay outside the app theme, but they must be called out
in review notes when they appear in an audit:

- Audio VU meters, VAD bands, and charts that rely on red/yellow/green
  semantics.
- Overlay editor handles, checker/guide visuals, alpha buffers, and overlay
  user-configurable colors.
- Transparent render buffers such as `Qt::transparent`.
- User-selected colors, avatar/generated identity colors, provider logos, and
  embedded content colors.
- Test fixtures and documentation examples that do not paint runtime UI.

## Implementation Rules

- Prefer a semantic token over a one-off token. For example, use `surface1`,
  `accentSubtle`, `danger`, `warning`, or `textMuted` before adding a new field.
- If a new token is genuinely needed, add it to every supported
  `UiThemeTokens` preset and to every Modern shell CSS theme block.
- For C++ injected QSS, serialize token colors with `uiThemeQssColor(...)`.
- For fallback paths, derive colors from the effective `QPalette` rather than
  fixed dark/light assumptions.
- For text documents and rich text, refresh the generated stylesheet after
  theme application. `Themes::apply()` already calls
  `MainWindow::refreshTextDocumentStylesheets()` when the main window exists.
- For Modern shell settings, make preview changes flow through `uiTweaks` so
  the settings dialog and the main shell preview the same theme.
- When overriding a Modern shell accent in JS, update `--accent`,
  `--accent-rgb`, `--accent-soft`, and `--accent-border` together. Alpha colors
  should derive from `--accent-rgb`.
- Do not leave "default dark" CSS literals in shared selectors after adding a
  light or non-default theme. Move the literal into `:root` or a semantic
  custom property.

## Audit Commands

Run these searches before declaring a theme pass complete:

```powershell
rg -n "activeUiThemeTokens\(|UiThemeTokens|uiThemeQssColor|QPalette|setPalette|setStyleSheet" src/mumble -g "*.cpp" -g "*.h"
rg -n "QColor\s*\(|QColorConstants|Qt::(red|green|yellow|blue|cyan|magenta|black|white|gray|darkGray|lightGray|transparent)" src/mumble -g "*.cpp" -g "*.h"
rg -n "#[0-9a-fA-F]{3,8}\b|rgba?\(|hsla?\(" src/mumble/modern-shell/styles.css
rg -n "data-theme|dataset.theme|modernShellTheme|qsModernShellTheme|uiTweaks" src/mumble src/mumble/modern-shell
git diff --check
```

Classify every new hit as one of:

- token definition
- token serialization or bridge code
- themed surface consuming tokens
- functional exception
- issue to fix before handoff

## Visual Verification

At minimum, verify:

- Modern shell `dark`, `light`, and one non-default theme such as `mocha`,
  `nord`, or `gruvbox`.
- The classic Qt configured light and dark styles if the change touches Qt
  widgets or shared dialogs.
- Main shell, Modern settings, connect dialog, context/self/app menus, a link
  preview card, image preview/dialog, text room with real chat, and voice-room
  navigator state.

Useful local commands:

```powershell
.\scripts\local\dev-build.ps1 -Fast -Launch
.\scripts\local\capture-dev-ui.ps1 -SkipBuild -Restart
.\scripts\local\review-dev-ui.ps1 -SkipBuild
.\scripts\local\invoke-dev-chat-matrix.ps1 -SkipBuild -ChannelScopes 'VC Root / AFK','#general'
```

A theme pass is not complete if switching settings leaves any visible window
element in the previous theme, except for a documented functional exception.
