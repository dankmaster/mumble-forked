# Screen Sharing Architecture

Status snapshot: 2026-07-13.

Screen sharing is an experimental fork feature. This delivery targets the
Windows Qt Quick client with Murmur and the external media relay deployed on
Linux. Linux and macOS desktop clients are outside this delivery and are not
part of its compatibility or release claims.

## Goal

The supported product shape is:

1. a Windows client can share a display, window, or supported application
   source in its current voice room
2. other Windows fork clients in that room can view it in the Qt Quick UI
3. normal Mumble voice remains on the existing Opus transport
4. Murmur supplies auth, policy, presence, and signaling while an external
   Linux-hosted relay carries media
5. old clients and servers retain ordinary voice/text behavior
6. recording waits until live publish/view is reliable

## Current Status

Implemented or wired today:

- `Version` capability advertisement for signaling, capture, viewing, codecs,
  dimensions, and FPS
- `ServerConfig` policy advertisement for enablement, recording policy, helper
  requirement, codec preferences, relay URL, maximum dimensions/FPS, and relay
  settings
- capability-gated `ScreenShareCreate`, `ScreenShareState`,
  `ScreenShareOffer`, `ScreenShareAnswer`, `ScreenShareIceCandidate`, and
  `ScreenShareStop` messages
- Murmur policy/state handling for active sessions and relay-token metadata
- client-side `ScreenShareManager`
- external `mumble-screen-helper` with local `QLocalSocket` IPC, capability
  probing, source listing, start/stop, diagnostics, and self-test hooks
- GStreamer/LiveKit publish and view planning where the Windows runtime is
  packaged
- Windows capture/runtime paths for GStreamer D3D11, Windows Graphics Capture,
  D3D11 Desktop Duplication, FFmpeg `gdigrab`, and test-pattern capture
- a native Qt Quick decoded-video item backed by bounded shared memory; screen
  sharing does not use the WebEngine media-player exception
- QML source picker, status, recovery, and toast flows; there is no classic or
  native Mumble-owned dialog fallback
- Linux Murmur/relay deployment with LiveKit-compatible token handoff

Not promised yet:

- production-grade screen-share reliability
- Linux or macOS desktop-client support in this delivery
- recording
- multi-stream mosaic
- stock upstream-client viewing support

## Why Video Is Not On Mumble Voice Transport

Mumble's real-time transport is voice-oriented. Its normal UDP payload and
packet sizing are unsuitable for video, and plugin data relay is bounded and
rate-limited rather than a media transport. The fork therefore uses
Mumble/Murmur only for authentication, policy, room context, presence, and
signaling.

Encoded media travels through the external LiveKit/WebRTC relay path. Murmur
does not fan out or transcode video frames.

## Compatibility Contract

- new Windows client + new server: screen share is available only when server
  policy, peer capabilities, helper, capture backend, and relay runtime agree
- new Windows client + old server: voice and chat continue; screen sharing stays
  hidden or disabled and no fork message is sent
- old client + new server: ordinary voice/text continues and unsupported peers
  are not sent screen-share messages
- old client + old server: unchanged

Unsupported peers are not expected to view streams; they remain normal Mumble
peers.

## System Boundary

The implementation is split into three planes:

- **Windows client/control plane:** Qt Quick UX, `ScreenShareManager`, room and
  permission state, helper supervision, and native frame presentation
- **Windows screen helper:** capture, encoder/decoder process launch,
  diagnostics, self-test, and the bounded decoded-frame channel
- **Linux server/relay:** Murmur validates room/policy state and supplies relay
  metadata; LiveKit/WebRTC provides fanout, congestion control, and NAT
  traversal

The main client sends narrow helper commands for capability query, source list,
publish/view start and stop, and self-test. Product UI remains in QML.

## Windows Capture And Publish

The helper probes the packaged runtime before advertising a path. Depending on
the selected source and available elements, publishing can use:

- GStreamer D3D11 screen capture and conversion
- Windows Graphics Capture
- D3D11 Desktop Duplication
- FFmpeg `gdigrab` fallback
- an explicit test-pattern diagnostics path

The GStreamer/LiveKit route is the intended release path. H.264 is the first
practical codec; other codecs are advertised only when the packaged helper
runtime can execute them. Resolution, FPS, and bitrate are clamped by client,
helper, and server policy.

Source/window/process-specific audio capture is enabled only when the required
Windows/GStreamer elements are available. Missing audio support must surface as
a capability warning rather than silently changing the requested session.

## Native Qt Quick Viewing

Helper IPC version 2 can expose a bounded BGRA shared-memory ring with
generation, dimensions, stride, sequence, and timestamp metadata. The client
copies frames off the GUI thread and renders immutable frames through a custom
Qt Quick scene-graph texture item.

FFmpeg decodes fixed-size BGRA `rawvideo` on stdout and keeps stderr for
diagnostics. GStreamer uses `videoconvert`, `videoscale`, and a quiet `fdsink`.
The helper assembler retains at most two complete frames, discards stale
backlog only at frame boundaries, and publishes sequence gaps through the
three-slot ring. Generation changes and detach invalidate stale readers.

GStreamer keeps its negotiated audio branch while publishing video frames. The
plain FFmpeg raw-video path is selected only for video-only sessions; sessions
that require audio retain the explicitly allowlisted external renderer until a
separate decoded-audio sink exists.

`MUMBLE_SCREENSHARE_NATIVE_FRAME_TEST_PATTERN=1` enables a deterministic helper
producer for process-boundary tests without a relay. A foreign FFplay/GStreamer
window is retained only as an explicit Windows development/runtime fallback if
native frame startup or shared-memory allocation fails. It is not a classic
client UI.

A future optimization may replace CPU BGRA with shareable GPU textures and
synchronization fences. The current correctness contract is fixed bounds,
sequence-gap accounting, generation-safe reset, and deterministic detach.

## Linux Server And Relay

Murmur remains deliberately narrow on the Linux host:

- validate whether a user may start or view a share
- expose stream state only to capable clients
- tear down shares on disconnect or invalid room/permission context
- mint short-lived relay metadata/tokens when LiveKit credentials are configured
- advertise policy through `ServerConfig`

LiveKit/WebRTC is responsible for media fanout, congestion control, and NAT
traversal. Relay TLS, TURN, firewall rules, token expiry, and service recovery
are deployment concerns and must be validated against the real Linux server.
Murmur should not duplicate encoded video or transcode media.

See [`screen-sharing-relay-deployment.md`](screen-sharing-relay-deployment.md)
for deployment details.

## Product UX

All Mumble-owned screen-share UI is Qt Quick/QML:

- start/stop from the current room
- themed source/quality picker
- room stream state and watch/focus/reopen actions
- helper/runtime status and clear recovery errors
- native QML video presentation

Qt Widgets is not a product fallback. OS-owned Windows permission/chooser
surfaces may remain native because Windows owns them.

## Recording Direction

Recording remains deferred. If added, prefer relay-side recording or an
asynchronous helper subscriber. Recording must not sit on the critical live
publish/view path.

## Verification Required Before Stable

- real two-client Windows publish/view using the staged runtime and Linux relay
- helper crash/restart and renderer/decoder failure recovery
- publisher/viewer disconnect, room move, permission loss, and reconnect
- `720p30`, `1080p30`, and one high-FPS/high-resolution profile on the target
  Windows hardware
- relay TLS/TURN/firewall/token behavior on the deployed Linux host
- memory, frame-drop, audio, and UI-thread behavior under sustained sessions
- clear QML errors and useful helper diagnostics for every failure class
- Windows staging, MSI, upgrade, and packaged-runtime validation

Linux/macOS desktop-client capture, shortcuts, packaging, and runtime validation
are not gates for this delivery because those clients are explicitly out of
scope.

See [`screen-sharing-implementation-plan.md`](screen-sharing-implementation-plan.md)
for the execution ledger.
