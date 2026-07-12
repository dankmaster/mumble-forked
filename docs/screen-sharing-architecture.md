# Screen Sharing Architecture

Status snapshot: 2026-06-05.

Screen sharing is an experimental fork feature. The control plane is wired into
Mumble/Murmur, and the media path is intentionally kept outside the normal voice
transport.

## Goal

Build a fork-specific screen sharing system with:

1. live desktop or window sharing
2. Discord-like perceived quality on the private community server
3. ordinary Mumble voice remaining on the existing Opus transport
4. optional recording only after live viewing is reliable

## Current Status

Implemented or wired today:

- `Version` capability advertisement for signaling, capture, viewing, codecs,
  dimensions, and FPS
- `ServerConfig` policy advertisement for enablement, recording policy, helper
  requirement, codec preferences, relay URL, maximum dimensions/FPS, and relay
  settings
- fork feature gate for richer screen-share presence
- `ScreenShareCreate`, `ScreenShareState`, `ScreenShareOffer`,
  `ScreenShareAnswer`, `ScreenShareIceCandidate`, and `ScreenShareStop`
  protocol messages
- Murmur state and policy handling for active sessions and relay-token metadata
- client-side `ScreenShareManager`
- external `mumble-screen-helper`
- local helper IPC over `QLocalSocket`
- helper capability probing and self-test hooks
- Windows capture/runtime probing for GStreamer D3D11 LiveKit, Windows Graphics
  Capture, D3D11 Desktop Duplication, `gdigrab`, and test-pattern capture
- Linux helper probing for X11/ffmpeg capture, test-pattern capture, and
  PipeWire runtime diagnostics
- GStreamer/LiveKit publish and view capability checks where the runtime is
  packaged
- Modern shell picker/status/toast integration and native fallback dialogs for
  narrow non-modern paths

Not promised yet:

- production-grade screen-share reliability
- multi-platform parity
- recording
- multi-stream mosaic
- stock upstream client viewing support

## Why Video Is Not On Mumble Voice Transport

Current Mumble transport is voice-oriented:

- UDP messages are only `Audio` and `Ping`
- UDP packet size is capped at `1024` bytes
- the primary real-time payload is Opus audio
- `PluginDataTransmission` is a TCP relay intended for plugins, not media
  streaming

Plugin relay payloads are capped and rate-limited on the server. They are
useful for control messages, but not for video frames. The fork therefore uses
Mumble only for auth, policy, presence, and signaling.

## Compatibility Direction

Backward compatibility is explicit:

- new client + new server: screen share may be available when server policy and
  helper/runtime capabilities allow it
- new client + old server: voice and chat work; screen-share UI is hidden or
  disabled and screen-share messages are not sent
- old client + new server: ordinary voice and text behavior continue; the server
  does not push screen-share messages to unsupported clients
- old client + old server: unchanged

Unsupported peers are not expected to watch streams. They should simply keep
working as normal Mumble peers.

## Architecture

The implementation is split into three planes:

- `Mumble/Murmur control plane`: auth, ACL, channel membership, stream
  permissions, session state, relay metadata, and compatibility gating
- `Client-side screen helper`: capture, encode/decode, external runtime launch,
  local diagnostics, self-test, and viewer/publisher process supervision
- `External media relay`: LiveKit/WebRTC fanout, congestion control, NAT
  traversal support, and optional future recording tap

The main client communicates with the helper through a narrow IPC command
surface:

- query capabilities
- list sources
- start/stop publish
- start/stop view
- self-test

### Qt Quick rendering boundary

The current viewer is not a native Qt Quick video pipeline. The helper starts
`ffplay` (or a GStreamer-owned viewer process), returns only its process ID, and
the Windows client discovers that process's top-level `HWND`. The QML
`WindowContainer` therefore embeds a foreign native window. It does not receive
decoded frames and must not be described as a `QQuickItem`, scene-graph texture,
or `QVideoSink` implementation.

Local helper IPC version 2 now includes an MVP bounded BGRA shared-memory ring
with generation, dimensions, stride, sequence and timestamp metadata. The client
polls and copies it off the GUI thread and renders immutable frames through a
dedicated scene-graph texture item. A deterministic helper test producer can be
enabled with `MUMBLE_SCREENSHARE_NATIVE_FRAME_TEST_PATTERN=1` to exercise the
complete process boundary.

The remaining production blocker is the decoder feed adapter: current ffplay
and GStreamer viewer processes still own their decoded surfaces and do not push
frames into the v2 ring. Production native rendering requires either an appsink
adapter for decoded CPU frames or a later negotiated GPU transport that provides:

- a bounded decoded-frame stream with pixel format, dimensions, stride,
  timestamp and generation metadata; or
- shareable GPU textures plus platform handles, synchronization fences and
  device-loss recovery.

The MVP ring already provides fixed bounds, sequence-gap drop accounting,
generation resets and deterministic detach. The external-window renderer remains
an explicitly allowlisted development/platform fallback until the production
appsink adapter is connected; wrapping it in QML is still not considered native
rendering.

## Media Runtime

The helper prefers proven external media runtimes instead of embedding a custom
SFU or video stack in Murmur.

Current runtime shape:

- WebRTC relay sessions are represented by `ScreenShareRelayTransportWebRTC`
- direct/test transports remain useful for diagnostics and local verification
- H.264 is the practical first codec, with AV1, VP9, and VP8 advertised when the
  helper runtime can actually execute them
- default quality is `1280x720` at `30 FPS`; picker/server limits can expose
  up to `2560x1440` at `144 FPS`, while capture hard caps go to `3840x2160` at
  `144 FPS`
- bitrate is selected from codec, resolution, FPS, and quality profile, then
  clamped by policy

## Server Direction

Murmur stays narrow:

- validate whether a user may start or view a share
- expose stream state to supporting clients
- tear down shares when users disconnect or lose context
- mint short-lived relay metadata/tokens when LiveKit credentials are
  configured
- advertise policy through `ServerConfig`

Murmur should not duplicate encoded video or transcode media.

## Client Direction

The Modern shell should remain the main UX:

- start/stop share from the current room
- themed source/quality picker
- visible stream state in room context
- watch/focus/reopen controls
- clear helper/runtime diagnostics
- toast-based failure reporting for Modern users

The native dialog path can remain as a narrow fallback while non-modern builds
still exist.

## Recording Direction

Recording remains deferred. If it lands, prefer relay-side recording or a helper
subscriber that writes asynchronously. It should not sit on the critical live
publish/view path.

## Verification Needed Before Calling It Stable

Before screen sharing becomes a production feature, verify:

- real Windows publish/view sessions using the packaged GStreamer LiveKit
  runtime
- helper crash and restart behavior
- publisher disconnect, viewer disconnect, channel move, and permission loss
- `720p30`, `1080p30`, and one high-FPS/high-resolution path on the target
  machine
- relay TLS/TURN/firewall behavior for real users
- failure messages in the Modern UI and diagnostics logs

See [`screen-sharing-implementation-plan.md`](screen-sharing-implementation-plan.md)
for the phase ledger and [`screen-sharing-relay-deployment.md`](screen-sharing-relay-deployment.md)
for relay deployment notes.
