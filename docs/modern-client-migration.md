# Modern Client Migration

Status snapshot: 2026-06-05.

The fork is migrating away from the legacy Qt Widgets client layout. In the
current master state, the WebEngine Modern shell is the intended visible client
path. Native operating-system pickers and selected internal Qt helpers may
remain while user-facing workflows are migrated or intentionally kept as narrow
escape hatches.

## Current Scope

- Modern layout is forced for the forked client shell in the current master state.
- Classic layout switching is removed from the visible Modern shell UX and the
  Modern settings page says so explicitly.
- WebEngine boot failure now shows a minimal failure notice instead of falling
  back to a full classic client layout in WebEngine-enabled builds.
- The non-WebEngine Qt Widgets path remains temporary build and migration
  scaffolding.
- Connect, saved-server editing, selected settings pages, failed-connection prompts, and common action dialogs now have
  Modern shell DTO/controller paths.
- The Modern settings dialog now splits audio into input and output pages with the high-impact legacy controls carried
  across: backend/device selection, exclusive mode, transmit/VAD timing, compression, echo/noise cleanup, cue files,
  idle behavior, playback, attenuation, positional audio, loopback, and remote speech cleanup.
- Modern settings pages currently cover Audio Input, Audio Output, Appearance,
  User Interface, Messages & Sounds, Key Bindings, Network, Screen Sharing, and
  About.
- Modern shell routes for Audio Wizard, ACL, and server/admin dialogs no longer silently fall back to legacy Widgets.
  Audio Wizard opens the modern audio input page. Server Information, Tokens, Registered Users, Ban List, ACL data,
  Create Room, User Information, local nickname, self comment, user comment, reset confirmations, kick/ban, About,
  About Qt, and Audio Stats now publish real Modern dialog DTOs instead of migration notices.
- A broader legacy-dialog guard now protects Modern shell action fallback, global-shortcut openers, and server-response
  handlers. Remaining Qt Widgets/native dialog surfaces include Plugin settings/install/update, Search, Voice Recorder,
  Certificate Wizard edge cases, and rare text-message fallback cases where the Modern composer cannot infer a target.
- Modern server log rendering now has its own document/append path instead of
  depending on the classic log widget as the WebEngine source of truth.
- Server builds and ordinary upstream/native client compatibility are not part
  of this migration pass; keep the server's baseline Mumble protocol behavior
  intact while the fork desktop client becomes modern-only.

## Bridge And Controller Surface

- `ModernShellBridge` publishes dialog state and accepts dialog field/action calls from the WebEngine shell.
- `ModernDialogController` owns the active modal state and dispatches connect, settings, and failed-connection actions.
- `ModernConnectController` builds saved-server DTOs and validates connect/save/remove actions.
- `ModernSettingsController` builds the active Modern settings pages and always
  forces the Modern layout fields in its draft.

## Remaining `.ui` Inventory

The repository currently contains 34 `.ui` files under `src/mumble`. Several
already have Modern shell routes, but the files remain until their legacy
classes can be deleted safely.

### Priority 1: Settings Pages

- `src/mumble/AudioInput.ui`
- `src/mumble/AudioOutput.ui`
- `src/mumble/GlobalShortcut.ui`
- `src/mumble/Log.ui`
- `src/mumble/LookConfig.ui`
- `src/mumble/NetworkConfig.ui`
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

### Priority 5: Plugin, Recorder, And Tool Windows

- `src/mumble/AudioStats.ui`
- `src/mumble/GlobalShortcutButtons.ui`
- `src/mumble/GlobalShortcutTarget.ui`
- `src/mumble/ManualPlugin.ui`
- `src/mumble/PluginInstaller.ui`
- `src/mumble/PluginUpdater.ui`
- `src/mumble/PTTButtonWidget.ui`
- `src/mumble/VoiceRecorderDialog.ui`

### Final Cleanup Candidates

- `src/mumble/MainWindow.ui`
- `src/mumble/ConfigDialog.ui`
- Legacy dock, menu, splitter, and user-tree glue that only exists to host the classic shell.

## Already Cut From This Direction

- ASIO
- G15/LCD
- PositionalAudioViewer
- in-game overlay
- TalkingUI

Positional audio, plugin infrastructure, and the manual plugin are still in
scope.
