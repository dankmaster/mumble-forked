# Current Status And Roadmap

Status snapshot: 2026-06-05.

This document is the short public handoff for where the fork stands today and
what the next product direction is. It describes the intended `master` state,
not a temporary experiment branch.

## Product Stance

This fork is a private-community Mumble build. The goal is still to preserve the
core voice experience that makes Mumble useful, but the fork desktop client
product direction is now modern-only:

- the WebEngine Modern shell is the visible client shell for the forked client
- classic layout switching is no longer a product path for that client
- non-WebEngine Qt Widgets UI remains as migration scaffolding and for narrow
  fallback surfaces while the codebase is cleaned up
- server compatibility for upstream/native Mumble clients remains a product
  requirement: ordinary voice, channel membership, ACLs, registration,
  certificates, and basic text should keep working where practical
- fork-only features remain capability-gated so unsupported clients and servers
  keep ordinary voice and basic text behavior

The fork is not trying to become a general replacement for upstream Mumble. When
a change is broadly useful, it should still be considered for upstream first.

## Current State

The main feature surface today is:

- Modern shell: WebEngine-based navigator, chat timeline, room list, rich cards,
  direct-message tray, themed dialogs, context menus, update banners, feedback,
  crash handoff, and modern settings for the commonly used pages.
- Persistent chat: stored voice-room and text-room history, optional
  server-global chat, private/direct-message protocol support when both peers
  and the server can use it, pagination, read state, replies, deletion,
  reactions, actor avatars, warmup, and history grants.
- Rich media: authenticated chunked uploads/downloads, stored assets, image
  normalization, generated preview derivatives, URL embeds, client-assisted
  previews, and server-owned thumbnail persistence.
- Stonks: scoped finance chat behavior, Yahoo quote parsing, provider links,
  Modern portfolio/leaderboard/following UI, and server-backed ledger tables.
- Screen sharing: experimental signaling, server policy, helper IPC, capability
  probing, Windows/Linux helper runtime paths, GStreamer/LiveKit publish/view
  support where the runtime is packaged, and diagnostic/self-test hooks.
- Speech cleanup: RNNoise, DTLN, and DeepFilterNet model plumbing with local
  benchmark and packaged-runtime smoke paths.
- Windows distribution: shared/WebEngine payload builds, `mumble-forked` MSI,
  update package artifacts, manifest-driven in-app update prompts, and MSI
  fallback for failed package apply.
- Server compatibility: ordinary upstream/native clients can still connect to
  the forked server for baseline Mumble behavior, but they do not get the full
  persistent rich chat, stonks, screen-share, or Modern shell feature surface.
- Legacy cuts: ASIO, G15/LCD, PositionalAudioViewer, in-game overlay, and
  TalkingUI have been removed from the fork direction.

## Near-Term Direction

The next work should make the modern-only stance true in the code, not just in
the UI:

- finish modern settings by deciding whether the plugin settings page is
  modernized, kept as a native dialog escape hatch, or intentionally deferred
- finish or cut remaining classic-dialog surfaces such as Search, Voice
  Recorder, Certificate Wizard edge cases, plugin installer/updater, and any
  rare text-message fallback that still opens a widget dialog
- continue cutting classic layout scaffolding once the remaining user-facing
  flows have modern equivalents
- keep server-side native-client compatibility boring and explicit: do not
  remove baseline `Version`, session/channel, ACL, registration/certificate, or
  transient `TextMessage` behavior just because the fork desktop client is
  modern-only
- keep server-log rendering on a direct modern path instead of rebuilding and
  mirroring the classic `QTextDocument` path for the WebEngine shell
- keep direct messages honest: persistent DM history is supported only when the
  server advertises it and both users have registered identities; otherwise the
  Modern tray uses private non-persistent text-message mode
- turn the screen-share path from "experimental but wired" into a verified
  release feature by proving real publish/view sessions, runtime packaging, and
  failure recovery on the target Windows client
- harden rich media with cache retention, preview pruning, derivative coverage,
  and stricter fetch isolation
- keep release tooling boring: CI, package manifest validation, updater
  fallback behavior, and sanitized public screenshots should stay current as
  features move

## Documentation Rules

When updating public docs:

- write from current `master` behavior unless a document is clearly labeled as a
  historical plan
- call unfinished work "experimental", "deferred", or "migration scaffolding"
  instead of presenting it as complete
- verify feature claims against `src/Mumble.proto`, `src/ForkFeature.*`,
  `src/ChatFeature.*`, `src/mumble/MainWindow.cpp`,
  `src/mumble/ModernSettingsController.cpp`, `src/murmur/Messages.cpp`, and the
  relevant helper/runtime files
- keep public screenshots redacted and sourced from sanitized UI captures
