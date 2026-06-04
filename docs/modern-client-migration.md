# Modern Client Migration

This branch starts the real migration away from the legacy Qt Widgets client UI. The first pass makes the WebEngine
Modern shell the default visible client path and begins moving normal user workflows into bridge-driven modern panels.
Native operating-system pickers and internal Qt helpers may remain while the user-facing workflows are migrated.

## Step 1 Scope

- Modern layout is forced for the client shell on this branch.
- Classic layout switching is removed from the visible Modern shell UX.
- The non-WebEngine/client fallback remains as temporary migration scaffolding.
- Connect, saved-server editing, selected settings pages, failed-connection prompts, and common action dialogs now have
  Modern shell DTO/controller paths.
- The Modern settings dialog now splits audio into input and output pages with the high-impact legacy controls carried
  across: backend/device selection, exclusive mode, transmit/VAD timing, compression, echo/noise cleanup, cue files,
  idle behavior, playback, attenuation, positional audio, loopback, and remote speech cleanup.
- Modern shell routes for Audio Wizard, ACL, and server/admin dialogs no longer silently fall back to legacy Widgets.
  Audio Wizard opens the modern audio input page. Server Information, Tokens, Registered Users, Ban List, ACL data,
  Create Room, User Information, local nickname, self comment, user comment, reset confirmations, kick/ban, About,
  About Qt, and Audio Stats now publish real Modern dialog DTOs instead of migration notices.
- A broader legacy-dialog guard now protects Modern shell action fallback, global-shortcut openers, and server-response
  handlers. Remaining captured placeholders are limited to Certificate Wizard, server settings editing, Search,
  Voice Recorder, chat-history grant, and rare text-message fallback cases where the Modern composer cannot infer a
  target.
- Server builds are not part of this migration pass.

## Bridge And Controller Surface

- `ModernShellBridge` publishes dialog state and accepts dialog field/action calls from the WebEngine shell.
- `ModernDialogController` owns the active modal state and dispatches connect, settings, and failed-connection actions.
- `ModernConnectController` builds saved-server DTOs and validates connect/save/remove actions.
- `ModernSettingsController` builds initial Look, Network, Screen Share, Audio input, and Audio output settings pages
  and always forces the Modern layout fields in its draft.

## Remaining `.ui` Inventory

The repository currently contains 39 `.ui` files. This pass starts replacement of `ConnectDialog.ui`,
`ConnectDialogEdit.ui`, `ConfigDialog.ui`, `LookConfig.ui`, `NetworkConfig.ui`, `AudioInput.ui`, `AudioOutput.ui`,
`ServerInformation.ui`, `Tokens.ui`, `UserEdit.ui`, `BanEditor.ui`, `ACLEditor.ui`, `UserInformation.ui`,
`UserLocalNicknameDialog.ui`, `RichTextEditor.ui`, `AudioStats.ui`, `widgets/BanDialog.ui`, and
`widgets/FailedConnectionDialog.ui` through Modern shell routes. The inventory keeps all files visible until their
legacy classes can be deleted safely.

### Priority 1: Settings Pages

- `src/mumble/AudioInput.ui`
- `src/mumble/AudioOutput.ui`
- `src/mumble/GlobalShortcut.ui`
- `src/mumble/Log.ui`
- `src/mumble/LookConfig.ui`
- `src/mumble/NetworkConfig.ui`
- `src/mumble/Overlay.ui`
- `src/mumble/PluginConfig.ui`
- `src/mumble/SearchDialog.ui`

### Priority 2: Connect And Server/Admin Dialogs

- `src/mumble/ACLEditor.ui`
- `src/mumble/BanEditor.ui`
- `src/mumble/ConnectDialog.ui`
- `src/mumble/ConnectDialogEdit.ui`
- `src/mumble/ServerInformation.ui`
- `src/mumble/Tokens.ui`
- `src/mumble/UserEdit.ui`
- `src/mumble/widgets/BanDialog.ui`

### Priority 3: User/Self Actions And Common Prompts

- `src/mumble/RichTextEditor.ui`
- `src/mumble/RichTextEditorLink.ui`
- `src/mumble/TextMessage.ui`
- `src/mumble/UserInformation.ui`
- `src/mumble/UserLocalNicknameDialog.ui`
- `src/mumble/widgets/FailedConnectionDialog.ui`

### Priority 4: Wizards

- `src/mumble/AudioWizard.ui`
- `src/mumble/Cert.ui`

### Priority 5: Plugin, Overlay, Recorder, And Tool Windows

- `src/mumble/AudioStats.ui`
- `src/mumble/GlobalShortcutButtons.ui`
- `src/mumble/GlobalShortcutTarget.ui`
- `src/mumble/ManualPlugin.ui`
- `src/mumble/OverlayEditor.ui`
- `src/mumble/PluginInstaller.ui`
- `src/mumble/PluginUpdater.ui`
- `src/mumble/PTTButtonWidget.ui`
- `src/mumble/VoiceRecorderDialog.ui`

### Final Cleanup Candidates

- `src/mumble/MainWindow.ui`
- `src/mumble/ConfigDialog.ui`
- Legacy dock, menu, splitter, and user-tree glue that only exists to host the classic shell.
