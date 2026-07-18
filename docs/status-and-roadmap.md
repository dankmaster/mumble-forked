# Current Status And Roadmap

Status snapshot: 2026-07-15.

This document is the short public handoff for the fork's current product state.
It describes the Windows Qt Quick desktop client, not the retired WebEngine or
classic Qt Widgets clients.

## Product Stance

This fork is a private-community Mumble build. The Windows desktop client is
Modern-only:

- all Mumble-owned product UI is rendered by Qt Quick/QML in a direct
  `QQuickWindow`
- typed C++ controllers and `QAbstractItemModel` implementations own product
  state; QML does not consume browser snapshots, JSON patches, or WebChannel
- no classic layout, hidden compatibility tree, native chat/log dock, or
  Mumble-owned widget dialog is a product fallback
- Qt Widgets remains linked only for narrow operating-system integration and
  plugin-owned Configure/About windows
- lazy `QtWebEngineQuick` is an explicit media-player exception, not an
  application shell
- server compatibility for ordinary Mumble clients remains a requirement for
  voice, channel membership, ACLs, registration, certificates, and basic text
- fork-only features remain capability-gated so unsupported peers retain
  ordinary Mumble behavior

The fork is not trying to replace upstream Mumble for every use case. Changes
that are broadly useful should still be considered for upstream first.

## Current State

The main feature surface today is:

- **Windows Qt Quick client:** the structural cutover covers native room and
  participant navigation, a virtualized chat timeline and composer,
  direct-message windows, dialogs and menus, Settings, plugins, certificates,
  recorder, ACL/admin flows, Manual Plugin, PTT tool, updater state, and
  frontend-neutral UI automation. Visual and interaction parity is still being
  refined across these surfaces.
- **Direct state binding:** stable session/channel/scope/message IDs connect
  QML to typed models. Incremental row changes and role-level updates replace
  the retired full-shell snapshot and hydration transport.
- **Persistent chat:** stored voice-room and text-room history, optional
  server-global chat, capability-gated persistent direct messages, pagination,
  read state, replies, deletion, reactions, actor avatars, warmup, and history
  grants.
- **Rich content:** structured rich text and preview cards, authenticated
  attachments, normalized images, and an asynchronous image-provider pipeline.
  Sender-controlled images are constrained by MIME, encoded-size, dimension,
  decoded-pixel, source-store, and decoded-cache budgets. Remote preview image
  hydration uses bounded HTTPS fetches, redirect checks, public-address
  validation, cancellation, and generation-safe cache entries.
- **Media boundary:** interactive provider playback/watch-together creates an
  off-the-record `QtWebEngineQuick` view only after explicit user interaction.
  Navigation is allowlisted; downloads, permissions, authentication prompts,
  certificate exceptions, popups, and file dialogs fail closed. It has no
  WebChannel or application-state bridge. Screen-share video uses a native QML
  scene-graph item instead.
- **Plugins:** startup discovery, transaction recovery and package preparation
  run asynchronously. A serial lifecycle worker performs update queries,
  load/unload, installation, reload and rollback away from the GUI thread,
  while a lifecycle barrier protects in-flight audio/positional/event callbacks.
  Configure/About remains plugin-owned native UI. Cancellation, progress,
  per-item errors, rollback and partial success share stable operation IDs
  without changing the plugin ABI or settings format.
- **Screen sharing:** experimental control-plane signaling, helper IPC,
  capability probing, native QML viewing, and LiveKit/GStreamer integration.
  The supported client scope in this delivery is Windows capture and viewing;
  Murmur and the external relay remain deployable on Linux.
- **Stonks:** scoped finance chat behavior, quote parsing, provider links,
  Modern portfolio/leaderboard/following UI, and server-backed ledger tables.
- **Speech cleanup:** RNNoise, DTLN, and DeepFilterNet model plumbing with local
  benchmark and packaged-runtime smoke paths.
- **Distribution:** shared Windows Qt builds, `mumble-forked` MSI and update
  packages, manifest-driven in-app update prompts, and MSI fallback after a
  failed package apply.
- **Legacy cuts:** ASIO, G15/LCD, PositionalAudioViewer, in-game overlay,
  TalkingUI, the classic widget client, and the HTML/CSS/JavaScript product
  shell are outside the fork direction.

## Runtime And Verification Scope

Windows is the supported desktop production gate for this delivery and has the
local build, connected-review, packaging, installer, automation, screenshot,
and performance harnesses. The production scope is the Windows Qt Quick client
plus the Linux Murmur/relay deployment. Linux and macOS desktop-client porting,
capture, shortcuts, packaging, and runtime claims are explicitly outside this
delivery.

The checked-in visual manifest is the source of truth for the current fixture
matrix; results are evidence only for the recorded source and executable hashes.
Automated Windows CI covers focused tests, staged binary checks, and the visual
and accessibility matrix. Connected review, performance/media lifecycle, and
installer-upgrade checks remain release-checklist work for each candidate. The
Linux server lane is client-off and screen-helper-off; its CI run is the server
release authority after changes are pushed. The separate manual Linux/macOS
client workflow is diagnostic and non-gating.

Screen sharing as a whole remains experimental until real packaged publish/view
sessions, relay failure recovery, helper restart, and target quality profiles
are proven for the Windows client against the deployed Linux relay.

## Near-Term Direction

The architectural cutover is complete, but substantial product-parity and
release work remains:

- refine 1:1 visual and interaction parity across the shell, Settings, dialogs,
  menus, and tools
- apply the same design tokens and polish standards to rich previews, provider
  embeddings, media chrome, and their loading, empty, error, and fallback states
- keep the Modern-only source inventory strict: no `.ui` forms, WebChannel,
  browser product shell, compatibility widgets, or unallowlisted widget prompts
- keep WebEngine lazy, off-the-record, isolated to explicit media playback, and
  absent before the user opens a media session
- graduate screen sharing only after two-client publish/view, reconnect,
  helper/renderer failure, relay TLS/TURN, and quality tests are repeatable
- keep plugin file/network work asynchronous and report slow third-party ABI
  calls; a misbehaving in-process plugin cannot be made preemptible without a
  separate plugin process and ABI proxy
- preserve bounded rich-content fetch/decode caches and extend adversarial
  media tests as new providers are added
- preserve the Qt Quick performance gates: no steady-state model resets, no
  synchronous network/plugin/file work on the UI thread, no UI stalls over
  50 ms, and no Chromium renderer before media activation
- keep native-client protocol compatibility and release tooling boring and
  explicit

## Documentation Rules

When updating public docs:

- write from current `master` behavior unless a document is clearly labeled as
  an archived plan
- distinguish source/build coverage from live Windows and Linux-server
  verification
- call screen sharing experimental until its live release matrix is complete
- verify claims against `src/Mumble.proto`, `src/ForkFeature.*`,
  `src/ChatFeature.*`, `src/mumble/QmlShellHost.*`,
  `src/mumble/QmlClientModels.*`, `src/mumble/PluginManager.*`,
  `src/mumble/QmlImageProvider.*`, the Windows paths in `src/screen-helper/`,
  and `src/murmur/Messages.cpp`
- keep public screenshots redacted and sourced from sanitized UI captures
