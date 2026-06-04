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

## Decision gate — RESOLVED 2026-06-03: MODERN-ONLY
**Decision: modern-only. No vanilla-server fallback requirement.** Force the modern
layout as the single UI, delete the classic Qt Widgets layout, and replace the
WebEngine-boot-failure case with a minimal error screen (not a full classic UI). All of
Phases 1–4 + 6 are in scope. The original question is kept below for context.

The whole plan branches on one product question:

**Q: Must the client still connect to *vanilla* Mumble servers (and degrade gracefully if the WebEngine shell fails to boot)?**

- Modern layout currently auto-activates only on **fork-compatible servers** ("Fork features detected"); vanilla servers show "Standard server" and fall back to classic. Classic is also the fallback when `m_modernShellRuntimeDisabled` (WebEngine boot failure). See `MainWindow::effectiveWindowLayout()` (`src/mumble/MainWindow.cpp`, ~line 12755).
- **If NO (closed fork ecosystem, modern is mandatory):** set `modernLayoutPolicy = ModernLayoutForced` as the only path and delete classic layout entirely (Phases 1–4 + 6). Keep a minimal "WebEngine failed to start" error screen instead of falling back to a full classic UI.
- **If YES (vanilla support required):** keep the classic layout as a real fallback, but still do Phases 1 and 3 (they remove QtWidgets *leaking into modern* without losing the fallback). Defer Phase 4.

> Recommendation: confirm with product owner. Everything below assumes **modern-only** unless a phase is marked "vanilla-safe".

## Current architecture (verified facts + entry points)
- **Transport is a bridge, not direct integration.** `ModernShellHost` creates a `QWebChannel`, registers `modernBridge` (`ModernShellBridge`), and pushes state as `QVariantMap` snapshots/patches (QVariant→JSON→JS) plus `runJavaScript`. The shell runs in a **separate QtWebEngine (Chromium) render process**. See `src/mumble/ModernShellHost.cpp` (~244-247, 316+).
- **Layout swap:** `MainWindow::applyShellLayout()` swaps the central widget between `activateModernShell()` and `activateLegacyShell()`. Only one is visible at a time. (`MainWindow.cpp` ~12850.)
- **Shared source-of-truth (keep):** `pmModel` (`UserModel`) is fed by the protocol; both the classic tree and the modern snapshot read from it.
- **Double-maintenance (target):** the classic server-log `QTextDocument` (`qteLog`) is built and mirrored to the web via `QTextDocument::contentsChange → serverLog.append` patch (`MainWindow.cpp` ~13177).
- **Modern settings coverage is partial:** `modernSettingsPageSupported()` (`MainWindow.cpp` ~14656) lists covered pages: Network, ScreenShare, AudioInput/Output, Shortcuts/KeyBindings, Look/UI, Messages&Sounds, About. **The remaining uncovered `ConfigDialog` pages are Overlay and Plugins** — see `openConfigDialogPage()` (~42154).
- **Connect** already routes modern in modern mode (`openServerConnectDialog` → `openModernConnectDialog`, ~41869); classic `ConnectDialog` is fallback.
- **Build configs:** `build/` = classic (modern-layout OFF), `build-shared-webengine/` = modern (`MUMBLE_HAS_MODERN_LAYOUT` ON, set in `src/mumble/CMakeLists.txt` ~792 under `if(modern-layout-webengine)`).

## Inventory — keep / migrate / delete

### Keep (source-of-truth / shared)
- `pmModel` / `UserModel`, protocol message handling, `ServerHandler`, audio, `GlobalShortcut`, `Log` core.

### Migrate to modern (no modern equivalent yet — they launch classic QtWidgets in modern today)
- `ConfigDialog` settings pages not in `modernSettingsPageSupported`: **Overlay, Plugins**.
- Standalone classic dialogs: `ServerInformation`, `Tokens`, `BanEditor`, `UserEdit` / `UserLocalNicknameDialog`, `SearchDialog`, `AudioWizard`, `OverlayEditor`, plugin installer/updater.

### Already modern (classic = fallback only; delete classic when layout is dropped)
- Connect, Settings (covered pages), VoiceRecorder, ACL editor, image viewer, screen-share picker/status, TextMessage, create/delete-room & drag confirms, SSL dialogs, feedback.

### Independent legacy features (orthogonal to layout — delete on a separate product call)
- In-game **Overlay** and **TalkingUI** floating window. Do NOT bundle these into the layout removal; decide per-feature. LCD and PositionalAudioViewer have already been cut.

### `.ui` forms likely deletable once classic layout + ConfigDialog go (39 total today)
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

### Phase 1 — Complete modern settings, retire `ConfigDialog` (vanilla-safe)
- **Scope corrected 2026-06-03 after verification.** The classic settings pages are
  registered as `ConfigWidget`s (grep `ConfigRegistrar`): AudioInput(1000),
  AudioOutput(1010), GlobalShortcut(1200), Look(1100), Network(1300), ScreenShare(1310),
  Log(4000). All of these are **already covered** by `ModernSettingsController` /
  `modernSettingsPageSupported()`. **PositionalAudio is already done too** — folded into
  the modern Audio Output page (`audio.positional*`, `audio.*Distance`, `audio.bloom`).
  **Cert is not a ConfigDialog page at all** — it's the standalone `CertWizard`, already
  reachable in modern via the `changeCertificate` action / `openCertWizardDialog`.
- **Actually-uncovered pages that still fall through to classic `ConfigDialog`:**
  - `OverlayConfig` (6000) — large: live preview + layout editor (`OverlayEditor`),
    fonts/colors/fps, per-element show/hide.
  - `PluginConfig` (5000) — medium: plugin list, enable/config, positional-audio
    permissions, install/update.
- **Sequencing tension with Phase 5:** Overlay is *also* on the Phase 5 removal
  list (an independent legacy feature). Building a full modern page for a feature we may
  delete is wasted work — resolve the modernize-vs-cut call before implementing it.
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
  - **Overlay → CUT ENTIRELY.** Remove the in-game Overlay feature (`Overlay*.cpp/.h`,
    `OverlayConfig`, `OverlayEditor*`, `Overlay*.ui`, settings members, menu/API hooks,
    CMake entries).
  - **Phase 0 commit:** the ChatPerfTrace gating was swept into pushed commit `79b508e75`
    (by a parallel Sonnet 4.6 session) whose message documents it. Left as-is (no
    force-push to the shared `master`).
  - Remaining order: (1) Overlay removal, (2) Plugins page/decision. `ConfigDialog`
    retirement waits on the deferred Plugins page.

### Overlay removal — full map (execute in a fresh session; it's large)
LCD + ASIO are CUT and committed on branch `modern-only/cut-lcd-asio` (`858c01f34`).
Overlay is mapped but NOT yet removed — it's bigger than LCD+ASIO combined and would break
the build if left half-done, so do it as its own focused pass. Surface to remove:

- **Client files to delete (25):** all `src/mumble/Overlay*` —
  `Overlay.{cpp,h,ui}`, `OverlayClient.*`, `OverlayConfig.*`, `OverlayEditor.{cpp,h,ui}`,
  `OverlayEditorScene.*`, `OverlayPositionableItem.*`, `OverlayText.*`, `OverlayUser.*`,
  `OverlayUserGroup.*`, `OverlayUtils.h`, `Overlay_macx.mm`, `Overlay_unix.cpp`,
  `Overlay_win.{cpp,h}`.
- **`USE_OVERLAY` guard sites (11 files):** `CMakeLists.txt`, `ConfigDialog.cpp`,
  `Global.cpp`, `Global.h` (the `Overlay *o;` member + fwd decl), `GlobalShortcut_macx.mm`,
  `main.cpp` (create `new Overlay()` ~853 + `delete Global::get().o` ~1057), `MainWindow.cpp`
  (~12649 `if (Global::get().o) …`), `MainWindow.h`, `Messages.cpp`, `os_macx.mm`,
  `UserModel.cpp` (the now-only line left in `updateOverlay()` — the whole method can go).
- **Settings:** `OverlaySettings` struct in `Settings.h` (~lines 107–188) + `OverlaySettings os`
  member (~366) + its `operator==/!=`; `OVERLAY_SETTINGS` + `WIN_OVERLAY_SETTINGS` macros and
  `PROCESS_ALL_OVERLAY_SETTINGS(_WITH_INTERMEDIATE_OPERATION)` in `SettingsMacros.h` (and the
  `WIN_OVERLAY_SETTINGS` entries in the two aggregate lists); all overlay keys in
  `SettingsKeys.h`; the overlay `LOAD(...)` block in `Settings.cpp`; the `os` (de)serialization
  in `JSONSerialization.cpp` (`j["overlay"]` ~166/249-250 and `to_json/from_json(OverlaySettings)`
  ~292-303). NOTE: `OverlaySettings` is referenced by `Overlay.h`/config too — delete together.
- **`OverlayConfig` registrar** (`ConfigRegistrar(6000)`) lives in `OverlayConfig.cpp` (deleted) —
  removes the last-but-one classic config page leak.
- **Separate subprojects to delete + de-register in root `CMakeLists.txt`:** dirs `overlay/`,
  `overlay_gl/`, `overlay_winx64/`, `macx/osax/`; root options `overlay` (~124) and
  `overlay-xcompile` (~126); the `if(overlay) add_subdirectory(...)` block (~209-220) and the
  ARM-mac `set(overlay OFF …)` block (~204-207). Check `src/mumble/CMakeLists.txt` for
  `Overlay*` source entries + any overlay helper copy/install steps + `--overlay` installer args.
- **Translations:** overlay strings drop out via `lupdate` (don't hand-edit `.ts`).
- Verify: configure + `cmake --build build-shared-webengine --target mumble` links clean; app
  starts; no `Global::get().o` / `USE_OVERLAY` / `OverlaySettings` references remain
  (`grep -rn "Overlay\|USE_OVERLAY\|\.o\b" src/mumble`).
- Pattern for adding a page (per `ModernSettingsController`): add to `pages()`, add an
  `if (m_activePage == ...)` branch in `sectionsForActivePage()`, handle each field id in
  `updateField()`, handle any buttons in `invokeAction()`, map incoming names in
  `setActivePage()`, and add the names to `modernSettingsPageSupported()` in
  `MainWindow.cpp`. Field helpers: `boolField`/`selectField`/`numberField`/`rangeField`/
  `fieldItem`/`actionField`/`noteField`/`hintedField`/`advancedField`/`advancedSection`.
- Once every page is covered (or its feature cut), remove the classic fallback branch in
  `openConfigDialogPage()` (modern path only), then delete `ConfigDialog` + the migrated
  sub-config `.ui`/`.cpp`/`.h`.
- Verify: every settings entry point opens the modern dialog; settings persist + apply (`applyModernSettings`).

### Phase 2 — Modernize or drop remaining classic dialogs
- For each of `ServerInformation`, `Tokens`, `BanEditor`, `UserEdit`/`UserLocalNicknameDialog`, `SearchDialog`, `AudioWizard`, `OverlayEditor`: build a modern dialog (`modernDialogDto` + bespoke renderer or generic fields, wired via `handleModernGenericDialogAction`) OR cut the feature if unused.
- Verify each entry point (mostly via modern context-menu/app-menu actions) opens the modern equivalent.

### Phase 3 — Cut the classic server-log QTextDocument mirror (vanilla-safe-ish)
- Feed the modern server log directly from the `Log` source instead of building `qteLog`'s `QTextDocument` and mirroring it via `contentsChange`.
- Remove the `qteLog` build/mirror path in modern mode; keep classic `qteLog` only if classic layout is retained.
- Verify: server log renders/append/scroll correctly in modern; re-measure vs Phase 0 baseline (expect reduced main-thread + serialization cost).

### Phase 4 — Remove classic layout scaffolding (modern-only only)
- Delete `activateLegacyShell()` and the classic central view (`qtvUsers` tree, `qdwLog`/`qdwChat` docks, `MainWindow.ui` central), the Hybrid/Custom layout code, and collapse `effectiveWindowLayout()` to always-modern.
- Remove the ~100 `usesModernShell()` branches (they become unconditional modern).
- Replace WebEngine-boot-failure fallback with a minimal error UI (no full classic UI).
- Verify: app starts straight into modern; no dead refs; both old build flags still configure.

### Phase 5 — Prune independent legacy features (product-gated)
- Per separate decision: remove Overlay / TalkingUI and their `.ui`/code/settings.

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
1. Get the decision-gate answer (modern-only vs keep-vanilla).
2. Take the Phase 0 baseline measurement.
3. Start Phase 1 (modern settings coverage) — it's vanilla-safe and removes the biggest QtWidgets leak (`ConfigDialog`).
