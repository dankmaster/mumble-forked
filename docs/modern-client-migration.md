# Windows Qt Quick Modern Client Migration

Status snapshot: 2026-07-13. Windows source cutover and the local release
verification matrix are complete.

The fork's Windows desktop client now starts a direct `QQuickWindow` through
`QQmlApplicationEngine` as its only product window. It does not place QML in a
`QQuickWidget`, create a hidden classic client, or use a browser as its
application shell. `QApplication` remains during the migration's technical
tail for tray/OS integration and explicitly allowlisted plugin-owned QWidget
windows.

## Current Architecture

Product state is owned in C++ and exposed directly to QML:

- `ClientSessionController` and `ActiveScopeController` publish connection and
  active-conversation state
- `RoomModel`, `ParticipantModel`, and `ChatTimelineModel` are incremental
  `QAbstractItemModel` implementations
- selection uses stable session, channel, and scope IDs; QML never retains a
  `QModelIndex` or raw model object across updates
- `UiCommandController` carries user intent back to the protocol/controllers
- `ClientActionRegistry` and `ActionModel` publish shortcut/action state without
  requiring a visible or hidden menu/toolbar
- `DialogStateController`, `AsyncOperationModel`, settings controllers, and
  plugin controllers publish typed fields, validation, operation progress,
  cancellation, and per-item results
- `ComposerController`, `MediaSessionBackend`, `QmlThemeController`, and the
  screen-share video item keep composition, media, design tokens, and decoded
  video out of ad-hoc QML JavaScript state

The code still uses some `ModernShell` names for historical C++ helpers and
DTO builders. They are in-process Qt Quick plumbing, not a WebEngine bridge.

## Completed Cutover

- All `.ui` files under `src/mumble` and the generated `MainWindowUi.h` layer
  have been removed.
- No classic tree, dock, native composer, log view, toolbar, or Mumble-owned
  menu is instantiated.
- The HTML/CSS/JavaScript product shell, WebChannel dependency, browser
  snapshots, JSON-patch transport, and WebEngine widget hosts have been
  removed.
- Settings, connect, plugins, certificates, recorder, ACL/admin, tokens,
  server/user information, Manual Plugin, PTT, updater, first-run, and prompt
  flows render in QML.
- UI automation reads controllers and models directly rather than scraping the
  Windows UIA tree inside a browser surface.
- Screen-share frames render through a native Qt Quick scene-graph item.
- The static/classic Windows client lane is retired; the shared Qt lane is the
  supported desktop-client build.

## Asynchronous Boundaries

Network, plugin-file, update, and image-decode work is not allowed to block the
QML render path:

- plugin startup uses asynchronous discovery and transaction recovery before
  applying libraries on a process-wide serial plugin worker
- plugin operations have stable IDs, progress phases, cancellation where the
  phase is cancellable, per-item results, partial-success state, and rollback
- update queries, package inspection, load/unload, installation, reload and
  rollback keep third-party lifecycle ABI calls off the GUI thread and publish
  measured results back through queued signals; plugin-owned Configure/About
  windows remain on the GUI thread by design
- runtime callbacks and lifecycle mutations share an exclusion boundary so a
  library cannot be unloaded while audio, positional or event callbacks are in
  flight; full crash or hang isolation still requires a future plugin process
  and ABI proxy
- chat images enter a controlled asynchronous image provider rather than being
  bound to arbitrary sender URLs in QML
- the image pipeline applies MIME, encoded-size, dimension, decoded-pixel,
  source-store, and decoded-cache limits, with cancellation and stale-generation
  rejection
- remote image hydration accepts public HTTPS targets only, validates DNS and
  every redirect, and applies bounded redirects, time, and response size

## WebEngine Media Exception

`QtWebEngineQuick` remains only for explicit interactive provider playback and
watch-together. The media `Loader` is inactive until a media session is opened,
so the main shell does not create a Chromium renderer.

The player uses an off-the-record profile, provider/navigation allowlisting,
and denied downloads, file dialogs, authentication prompts, permissions,
certificate exceptions, context menus, and popup windows. It is controlled by
`MediaSessionBackend`; it does not receive application state through WebChannel.
Closing the session destroys the loaded media surface.

The Windows Qt 6.9 runtime bundle may still contain WebChannel DLLs because
Qt's own WebEngineQuick plugin imports them transitively. `mumble.exe` neither
links to nor uses that API; staging records and gates its direct PE imports so
this vendor-runtime detail cannot turn back into an application bridge.

## Allowed Native Surfaces

Qt Widgets is not a product-UI fallback. The allowed native boundary is limited
to:

- OS file/folder pickers, tray integration, and native notifications
- crash, security, and external updater handoff where the operating system owns
  the surface
- third-party plugin Configure/About windows, opened without a hidden classic
  parent
- a minimal fatal startup/error handoff if the Qt Quick scene cannot start

All Mumble-owned dialogs, menus, settings pages, and tools belong in QML.

## Compatibility Guarantees

The migration does not change the Mumble protocol, fork feature protocol,
plugin ABI, certificate format, plugin settings format, or compatibility with
ordinary Mumble clients and servers. Older layout/settings values are read
without resurrecting the removed frontend.

## Delivery Scope

This delivery supports the Windows Qt Quick desktop client and a Linux-hosted
Murmur/relay. Linux and macOS desktop-client porting is explicitly out of scope:
this document makes no claim about their client UI, global shortcuts, capture,
packaging, or runtime readiness. Cross-platform client work belongs in a future
delivery after the Windows product and installer gates are stable.

## Release Gates

Run the source inventory locally with:

```powershell
.\scripts\windows\verify-modern-only.ps1 -Strict
```

The strict gate must report no product `.ui` files, classic compatibility
widgets, WebChannel, browser shell resources, or unallowlisted widget prompts.
WebEngine references are allowed only in the explicit Qt Quick media-player
allowlist.

Release validation also covers controller/model tests, Qt Quick component and
focus tests, connected UI automation, screenshot/accessibility matrices,
performance measurements, and Windows staging/MSI/upgrade. Source cutover being
complete does not waive those Windows runtime and packaging gates.

The reviewed screenshot/accessibility baseline lives in
`qml-visual-baseline/`. The Windows CI lane builds its automation-enabled visual
fixture only after publishing the automation-disabled release payload. Native
Linux CI remains the authoritative build and test gate for the server; it does
not build a desktop client.
