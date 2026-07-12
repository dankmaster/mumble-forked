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

## Remaining Stabilization Work

The old WebEngine frontend sources and bridge classes are no longer part of the
desktop client target. Some source files remain in the tree as deletion debt
until their cross-platform replacement and packaging gates are green.

Remaining cleanup is tracked by `scripts/windows/verify-modern-only.ps1`:

- delete unreachable classic widget code and pointer aliases from
  `MainWindow`;
- remove the old HTML/CSS/JS shell, WebChannel serialization, snapshot and
  hydration bridge, and WebEngine widget hosts;
- retain WebEngine references only in the Qt Quick media allowlist;
- finish native QML screen-share presentation and watch-together protocol
  synchronization;
- complete accessibility, screenshot, performance, installer-upgrade, Linux,
  and macOS gates.

Run the inventory locally with:

```powershell
.\scripts\windows\verify-modern-only.ps1
```

`-Strict` intentionally stays red until every remaining stabilization item is
removed.
