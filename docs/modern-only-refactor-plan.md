# Modern-Only Refactor Plan (toward 1.0)

> Handoff plan for a fresh session. Read this top-to-bottom before touching code.
> The goal is to make the modern web shell the single UI and remove the classic
> Qt Widgets layout + dead legacy code, as the fork approaches a 1.0 release.

## Outcome we want
- One UI path: the **modern shell** (`src/mumble/modern-shell/` driven via `ModernShellHost` + `QWebChannel`).
- Classic Qt Widgets layout (`qtvUsers` tree, docks, `MainWindow.ui` central view, Hybrid/Custom layouts) **removed**.
- Classic dialogs that still launch in modern mode either **modernized** or **deleted**.
- Reduced double-maintenance (notably the classic server-log `QTextDocument` mirror).
- Net: smaller binary/surface, fewer `usesModernShell()` branches (~100 collapse to one path), lower per-state-change cost.

## Decision Gate
**Decision: modern-only fork desktop client.** Force the modern layout as the
single visible UI for the forked desktop client, delete the classic Qt Widgets
layout, and replace the WebEngine-boot-failure case with a minimal error screen
(not a full classic UI). This does not remove the requirement that the forked
server accepts ordinary upstream/native Mumble clients for baseline voice,
channel, ACL, registration/certificate, and basic text behavior. All of Phases
1–4 + 6 are in scope. The current `master` documentation should state this
product direction plainly. The remaining question is how aggressively to delete
the leftover Qt Widgets client scaffolding while non-WebEngine and migration
builds still exist.

## Current architecture (verified facts + entry points)
- **Transport is a bridge, not direct integration.** `ModernShellHost` creates a `QWebChannel`, registers `modernBridge` (`ModernShellBridge`), and pushes state as `QVariantMap` snapshots/patches (QVariant→JSON→JS) plus `runJavaScript`. The shell runs in a **separate QtWebEngine (Chromium) render process**. See `src/mumble/ModernShellHost.cpp` (~244-247, 316+).
- **Layout policy:** in `MUMBLE_HAS_MODERN_LAYOUT` builds, `effectiveWindowLayout()` resolves to `LayoutModern` and `applyShellLayout()` activates the Modern shell. Non-WebEngine Qt Widgets client builds still keep `activateLegacyShell()` as temporary scaffolding.
- **WebEngine failure:** Modern boot failure now shows a minimal failure notice instead of falling back to a full classic client layout.
- **Shared source-of-truth (keep):** `pmModel` (`UserModel`) is fed by the protocol; modern snapshots read from it, and minimal-snapshot/safe-mode paths avoid unnecessary hidden Qt tree churn.
- **Server log direction:** the Modern server log now has a separate `LogDocument` fed by `Log::serverLogEntryAppended` / `appendModernServerLogEntry`; `qteLog` should remain only for Qt Widgets/non-WebEngine client fallback.
- **Modern settings coverage is partial but broad:** `modernSettingsPageSupported()` covers Network, ScreenShare, AudioInput/Output, Shortcuts/KeyBindings, Look/Appearance, UI, Messages&Sounds, and About. **The remaining uncovered `ConfigDialog` page is Plugins** — see `openConfigDialogPage()`.
- **Connect** routes modern in modern mode (`openServerConnectDialog` → `openModernConnectDialog`); classic `ConnectDialog` is fallback for Qt Widgets/non-WebEngine client paths.
- **Build configs:** `build/` = classic (modern-layout OFF), `build-shared-webengine/` = modern (`MUMBLE_HAS_MODERN_LAYOUT` ON, set in `src/mumble/CMakeLists.txt` ~792 under `if(modern-layout-webengine)`).

## Inventory — keep / migrate / delete

### Keep (source-of-truth / shared)
- `pmModel` / `UserModel`, protocol message handling, `ServerHandler`, audio, `GlobalShortcut`, `Log` core.

### Migrate to modern (no modern equivalent yet — they launch classic QtWidgets in modern today)
- `ConfigDialog` settings pages not in `modernSettingsPageSupported`: **Plugins**.
- Remaining standalone Qt Widgets/native dialog surfaces to decide or modernize: `SearchDialog`,
  `VoiceRecorderDialog`, `CertWizard` edge cases, plugin installer/updater, and
  any rare text-message fallback that cannot infer a Modern composer target.

### Already modern (classic = fallback only; delete classic when layout is dropped)
- Connect, Settings (covered pages), Server Information, Tokens, Registered
  Users, Ban List, User Information, local nickname, user/self comments, reset
  confirmations, kick/ban, About, About Qt, Audio Stats, ACL editor, image
  viewer, screen-share picker/status, TextMessage/common message prompts,
  create/delete-room & drag confirms, SSL dialogs, feedback, crash/update
  handoff, and direct-message tray.

### Independent legacy features (orthogonal to layout — delete on a separate product call)
- In-game **Overlay**, **TalkingUI**, LCD, and PositionalAudioViewer have already been cut.

### `.ui` forms likely deletable once classic layout + ConfigDialog go (34 total today)
`MainWindow.ui`, `ConnectDialog*.ui`, `ConfigDialog.ui`, `AudioInput/Output.ui`, `NetworkConfig.ui`, `LookConfig.ui`, `PluginConfig.ui`, `GlobalShortcut*.ui`, `Cert.ui`, `Log.ui`, `ServerInformation.ui`, `Tokens.ui`, `UserEdit.ui`, `UserInformation.ui`, `SearchDialog.ui`, `BanEditor.ui`, `ACLEditor.ui`, … (confirm each has a modern replacement before deleting).

## Phased plan (each phase independently buildable + shippable)

### Phase 0 — Guardrails + baseline (do first)
- Confirm the decision gate.
- **[DONE 2026-06-03] ChatPerfTrace is now build-gated to the dev client only.** It used
  to be compiled into every build and merely gated at runtime by the
  `MUMBLE_CHAT_PERF_TRACE` env var. Now:
  - New CMake option `chat-perf-trace` (`src/mumble/CMakeLists.txt`), defaulting to
    `${MUMBLE_MODERN_LAYOUT_TOOLING_DEFAULT}` — i.e. **ON for local dev builds, OFF for
    GitHub packaging builds** (`packaging AND GITHUB_ACTIONS`). When ON it defines
    `MUMBLE_HAS_CHAT_PERF_TRACE` on `mumble_client_object_lib`.
  - `ChatPerfTrace.h` wraps the full tracer in `#if defined(MUMBLE_HAS_CHAT_PERF_TRACE)`
    with a zero-cost no-op `#else` branch (constexpr-false `enabled()`, empty inline
    record fns, empty-ctor `ScopedDuration`). Call sites are unchanged; in CI builds the
    optimizer eliminates them entirely. Verified: dev client links clean (full branch);
    no-op branch compiles under `/W4 /WX` (RAII locals don't trip unused-var).
  - To run a trace on the dev client: build `build-shared-webengine` (option auto-ON),
    then launch with env `MUMBLE_CHAT_PERF_TRACE=1` (optional
    `MUMBLE_CHAT_PERF_TRACE_PATH=...`; default `%TEMP%\mumble-chat-perf.log`).
- **[TODO — needs a live session]** Capture the **baseline perf measurement** with the
  dev client (`MUMBLE_CHAT_PERF_TRACE=1`): join a busy voice channel, scroll chat, open
  dialogs. Record snapshot/patch serialization counts and frame hitches so later phases
  can be compared. (Requires running against a real server — do this before Phase 3.)
- Note guardrails: **never-freeze** (route high-frequency updates through patches/in-place DOM, not full snapshots/rebuilds), keep `node --check` green on `app.js`/`dialog.js`, and keep `TestModernDialogControllers` + `ModernUiAutomationServer` flows passing.

### Phase 1 — Complete modern settings, retire `ConfigDialog`
- **Scope corrected 2026-06-03 after verification.** The classic settings pages are
  registered as `ConfigWidget`s (grep `ConfigRegistrar`): AudioInput(1000),
  AudioOutput(1010), GlobalShortcut(1200), Look(1100), Network(1300), ScreenShare(1310),
  Log(4000). All of these are **already covered** by `ModernSettingsController` /
  `modernSettingsPageSupported()`. **PositionalAudio is already done too** — folded into
  the modern Audio Output page (`audio.positional*`, `audio.*Distance`, `audio.bloom`).
  **Cert is not a ConfigDialog page at all** — it's the standalone `CertWizard`, already
  reachable in modern via the `changeCertificate` action / `openCertWizardDialog`.
- **Actually-uncovered pages that still fall through to classic `ConfigDialog`:**
  - `PluginConfig` (5000) — medium: plugin list, enable/config, positional-audio
    permissions, install/update.
- **DECISIONS 2026-06-03 (updated):**
  - **LCD → CUT (final, 2026-06-03).** Reversed the earlier "modernize" call: LCD is
    scrapped entirely (G15 hardware is dead; user confirmed). Removed `LCD.*`,
    `G15LCDEngine_*`, `LCD.ui`, `helpers/g15helper/`, the `g15` option + CMake blocks +
    `--g15` installer arg, `Global::lcd`, settings (`qmLCDDevices`, `iLCDUserView*`) incl.
    keys/macros/JSON, and the `updateUserView` render hook. The modern LCD page below was
    discarded. Builds + links clean.
  - **ASIO → CUT (final, 2026-06-03).** User does not use ASIO (logs show
    `ASIO: No valid devices found, disabling` every run; WASAPI is the active path and the
    modern Windows standard for voice). Removed `ASIOInput.*`, the `asio` option + CMake
    block + `USE_ASIO`, settings (`bASIOEnable`/`qsASIOclass`/`qlASIO*`) incl. keys/macros,
    and the `ASIO` mention in `AudioStats.ui`. Builds clean.
  - ~~**LCD → DONE.** Modern LCD page added~~ (superseded by the CUT above):
    `sectionsForActivePage()` lcd branch + `updateField` `lcd.*` + `setActivePage`
    `LCDConfig`/`lcd`; `modernSettingsPageSupported()` updated; apply via new
    `LCD::applyDeviceSettings()` called from `MainWindow::applyModernSettings`, device
    enumeration via new `LCD::deviceNames()`). Builds clean. (Uncommitted as of this note.)
  - **Plugins → DEFERRED.** Keep the classic `PluginConfig` reachable; do **not** build a
    modern Plugins page yet, and do **not** touch the plugin subsystem (it powers
    positional audio). This means `ConfigDialog` **cannot be fully retired** until Plugins
    is handled later.
  - **ASIO/G15 pipeline leftovers → CLEANED 2026-06-04.** CI/dependency bootstrap, signing,
    local staging, and installer feature parsing no longer download ASIO/G15 SDKs or look
    for `mumble-g15-helper.exe` / `--g15` after the feature cut.
  - **PositionalAudioViewer → CUT 2026-06-04.** Removed the Developer menu dialog and its
    three Qt Widget files while keeping positional audio, ManualPlugin, and plugin manager
    behavior intact.
  - **Overlay + TalkingUI → CUT 2026-06-04.** Removed the in-game overlay subprojects
    (`overlay/`, `overlay_gl/`, `overlay_winx64/`, `macx/osax`), `src/mumble/Overlay*`,
    TalkingUI files, `PathListWidget`, overlay/TalkingUI settings + JSON/key macros,
    menu/global-shortcut registrations, CI/staging/installer payload paths, themes, and
    overlay helper scripts/docs. Legacy shortcut enum IDs remain in place to avoid
    reshuffling persisted shortcut values.
  - **Phase 0 commit:** the ChatPerfTrace gating was swept into pushed commit `79b508e75`
    (by a parallel Sonnet 4.6 session) whose message documents it. Left as-is (no
    force-push to the shared `master`).
  - Remaining order: Plugins page/decision. `ConfigDialog` retirement waits on the
    deferred Plugins page.

### Completed legacy cuts
- Overlay/TalkingUI cut: done 2026-06-04; verify with a configure/build plus residual search
  excluding intentional words such as screen-share overlay bars and theme token `overlay0`.
- Theme cleanup candidates found during Modern custom-theme work:
  - `LookConfig.cpp` / `LookConfig.ui` still expose classic QSS theme selection
    and the old `Open Themes Directory` flow.
  - `Themes.cpp`, `ThemeInfo.cpp`, `themes/*.qss`, and
    `Settings::{styleType, themeName, themeStyleName, themeDarkName,
    themeDarkStyleName}` still back the classic theme model.
  - `activeUiThemeTokens()` now accepts custom Modern tokens and custom accent
    overlays, but built-in native token unification still needs a Modern
    registry instead of QSS-era hardcoded presets.
  - Bundled Modern example themes now stage from
    `src/mumble/modern-shell/themes/` into `ModernThemes`; keep this path if
    classic `themes/` is deleted.
- Pattern for adding a page (per `ModernSettingsController`): add to `pages()`, add an
  `if (m_activePage == ...)` branch in `sectionsForActivePage()`, handle each field id in
  `updateField()`, handle any buttons in `invokeAction()`, map incoming names in
  `setActivePage()`, and add the names to `modernSettingsPageSupported()` in
  `MainWindow.cpp`. Field helpers: `boolField`/`selectField`/`numberField`/`rangeField`/
  `fieldItem`/`actionField`/`noteField`/`hintedField`/`advancedField`/`advancedSection`.
- Once every page is covered (or its feature cut), remove the Qt Widgets scaffold branch in
  `openConfigDialogPage()` (modern path only), then delete `ConfigDialog` + the migrated
  sub-config `.ui`/`.cpp`/`.h`.
- Verify: every settings entry point opens the modern dialog; settings persist + apply (`applyModernSettings`).

### Phase 2 — Modernize or drop remaining classic dialogs
- For each remaining surface (`SearchDialog`, `VoiceRecorderDialog`,
  `CertWizard` edge cases, plugin installer/updater, plugin settings, and rare
  text-message fallback prompts): build a modern dialog (`modernDialogDto` +
  bespoke renderer or generic fields, wired via `handleModernGenericDialogAction`)
  OR keep it as an explicit native dialog escape hatch with docs saying why.
- Verify each entry point (mostly via modern context-menu/app-menu actions) opens the modern equivalent.

### Phase 3 — Cut the classic server-log QTextDocument mirror
- Status: direct Modern log document and append signal are in place in the
  current worktree/master direction.
- Finish pruning any remaining WebEngine dependency on `qteLog`'s
  `contentsChange` mirror; keep classic `qteLog` only for Qt Widgets/non-WebEngine
  client fallback.
- Verify: server log renders/append/scroll correctly in Modern; re-measure vs
  Phase 0 baseline if the baseline was captured.

### Phase 4 — Remove classic layout scaffolding (modern-only only)
- WebEngine-enabled builds now collapse to Modern in `effectiveWindowLayout()`;
  keep pushing the simplification inward.
- Delete or quarantine `activateLegacyShell()` and the classic central view
  (`qtvUsers` tree, `qdwLog`/`qdwChat` docks, `MainWindow.ui` central) once
  Qt Widgets/non-WebEngine client build expectations are decided.
- Remove remaining `usesModernShell()` branches that now only distinguish
  modern from dead layout paths.
- Verify: app starts straight into Modern; no dead refs; both intended build
  flags still configure.

### Phase 5 — Prune independent legacy features (product-gated)
- Overlay / TalkingUI are already removed. Remaining candidates need a separate product call.

### Phase 6 — Final cleanup
- Remove the `modern-layout-webengine` build option (always on) and the `MUMBLE_HAS_MODERN_LAYOUT` guards; delete now-unreferenced `.ui` forms, `.cpp/.h`, and prune their entries from `CMakeLists.txt`.
- Run Qt lupdate so stale strings drop from `mumble_*.ts` (don't hand-edit translations).
- Delete dead settings keys (`SettingsKeys.h`/`Settings.*`) for removed features.

## Build & verify (commands that work here)
- Modern build (the one that matters):
  `cmd /c '"<VS>\VC\Auxiliary\Build\vcvars64.bat" && cmake --build build-shared-webengine --target mumble'`
  (run via the PowerShell tool; this shell is NOT a VS dev shell by default — vcvars is required or MSVC can't find `<utility>`/`<limits>`).
- Classic build (keep green while classic exists): `cmake --build build --target mumble`.
- JS: `node --check src/mumble/modern-shell/app.js` and `dialog.js`.
- Tests: `TestModernDialogControllers`, `ModernUiAutomationServer` flows.
- Each MainWindow.cpp recompile is slow (~2-3 min); batch related edits, build once per phase. Watch `/WX` (warnings are errors — e.g. C4458 shadowing).

## Risks / gotchas
- **/WX** turns warnings into errors (shadowing, unused). 
- Header decls of modern-only methods must be guarded the same as their definitions, or the classic build fails to link (MOC references unguarded slot decls). Pattern: wrap modern-only members in `#if defined(MUMBLE_HAS_MODERN_LAYOUT)`.
- Don't regress **never-freeze**: keep talk-state surgical, room list keyed-reconcile, app-only patch fast-path.
- Translations live in `mumble_*.ts` — regenerate, don't hand-edit.
- WebEngine is a separate process; "direct integration" isn't an option — optimize the bridge (patches over full snapshots), don't try to remove it.

## First actions for the new session
1. Treat modern-only as resolved master direction.
2. Verify current direct server-log path with a live Modern session.
3. Decide Plugins settings: modernize, explicitly keep as a native dialog escape hatch,
   or defer with a visible migration note.
4. Continue Phase 2 on Search / Voice Recorder / Certificate / plugin installer
   surfaces.
5. After those are handled, prune remaining classic layout scaffolding and
   stale `.ui` files in focused commits.
