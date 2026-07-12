# Qt Quick Modern Client Migration

Status snapshot: 2026-07-12.

The fork desktop client now starts a direct `QQuickWindow` as its only product
window. The Qt Quick shell is the default path on Windows and does not require
an environment flag. Qt Widgets remains linked for the tray, operating-system
integration, file pickers, and plugin-owned Configure/About windows.

## Completed

- All `.ui` files under `src/mumble` have been removed.
- `MainWindowUi.h` and its generated widget builder have been removed.
- No classic tree, dock, native chat editor, log view, toolbar, or Mumble menu
  is instantiated by the QML client.
- The room, participant, chat, action, dialog, async-operation, selection, and
  media surfaces are exposed as typed QObject controllers or
  `QAbstractItemModel` implementations.
- User and channel selection use stable protocol IDs instead of a `QTreeView`
  current index.
- Settings, plugins, certificate management, recorder, ACL, Manual Plugin,
  PTT, message events, and shortcut editing render in QML.
- Plugin update downloads report asynchronous progress, cancellation, and a
  final result per plugin.
- Interactive provider playback uses an isolated, off-the-record
  `QtWebEngineQuick` surface. It is instantiated only after explicit user
  interaction and uses no WebChannel.
- UI automation reads controllers and models rather than the Windows UIA tree
  inside a browser surface.

## Compatibility Guarantees

The migration does not change the Mumble protocol, fork feature protocol,
plugin ABI, certificate format, plugin settings format, or compatibility with
ordinary Mumble clients and servers.

## Source Cutover Complete

The HTML/CSS/JavaScript product shell, WebChannel bridge, snapshot/hydration
transport, WebEngine widget hosts, classic layout, and Mumble-owned widget
dialogs have been removed. WebEngine references are limited to the isolated
Qt Quick provider player. The static Windows client lane is retired because
Qt WebEngine is a required shared dependency for that player.

Release validation is split across the shared Windows client/installer lane and
the Qt Quick Linux/macOS desktop workflow. Performance, connected-state,
accessibility, screenshot, and installer-upgrade evidence is produced by the
typed automation and packaging gates rather than by a fallback frontend.

Run the inventory locally with:

```powershell
.\scripts\windows\verify-modern-only.ps1
```

`-Strict` is the source-level release gate and must remain green.
