# Screen Sharing Implementation Plan

Status snapshot: 2026-07-13.

This document is the execution ledger for the screen-sharing feature described
in [`screen-sharing-architecture.md`](screen-sharing-architecture.md). The
feature is no longer planning-only, but it is still experimental and should not
be described as production-ready.

## Current Product Definition

The Windows-client/Linux-server MVP is:

1. a Windows fork client can start one live screen share in its current channel
2. other Windows fork clients in that channel can view it
3. old clients still connect, talk, and chat normally
4. old servers still accept the new client, but screen share UI stays disabled
5. no recording in MVP
6. no server-side transcoding in MVP
7. no stock-upstream client compatibility for viewing the stream

First stable quality target: `720p30`.

Expanded picker/runtime targets are `720p`, `1080p`, and `1440p` at practical
FPS values up to the advertised server/helper cap, currently `144 FPS`.

## Compatibility Contract

This remains mandatory:

- new Windows client + new server: screen share may be available when policy
  and helper runtime allow it
- new Windows client + old server: voice and chat work, screen share hidden or
  disabled
- old client + new server: voice and chat work, old client is never sent
  screen-share traffic
- old client + old server: unchanged

Screen share is capability-gated, not version-assumed.

## Implemented Foundation

Protocol and policy:

- `Version` advertises screen-share signaling, capture, viewing, codecs, maximum
  width/height, and FPS
- `ServerConfig` advertises enablement, recording policy, helper requirement,
  codec preferences, relay URL, maximum width/height/FPS, and supported fork
  features
- protocol messages exist for create, state, offer, answer, ICE candidate, and
  stop
- fork feature gate exists for richer screen-share session presence

Murmur:

- stores per-session support flags from `Version`
- sends server policy through `ServerConfig`
- applies runtime config updates for screen-share keys
- carries active session state and relay metadata
- can use LiveKit-compatible relay API key/secret settings for token-style
  relay handoff

Client:

- `ScreenShareManager` owns client session state, helper coordination, view
  focus/reopen behavior, external runtime watchdogs, and diagnostics
- the Qt Quick client exposes picker/status/toast flows and a native scene-graph
  video item
- there is no classic or native Mumble-owned dialog fallback
- local settings include screen-share auto-open and diagnostics controls

Helper:

- `src/screen-helper/` builds `mumble-screen-helper`
- local IPC is JSON over `QLocalSocket`
- helper supports capability query, source listing, publish/view start, stop,
  and self-test
- helper runtime probing covers GStreamer, FFmpeg, LiveKit, encoder/decoder
  availability, and capture backends
- packaged Windows shared Qt payloads can stage GStreamer under
  `gstreamer\`

## Current Runtime Shape

Windows probing:

- GStreamer D3D11 LiveKit capture
- Windows Graphics Capture with D3D11
- D3D11 Desktop Duplication
- `gdigrab`
- lavfi/test-pattern capture

Linux server/relay:

- Murmur policy, session state, and LiveKit-compatible token handoff
- external LiveKit/WebRTC fanout, congestion control, and NAT traversal
- no client capture or rendering claim in this delivery

Relay/media:

- WebRTC relay transport is the main intended runtime path
- direct/file-style runtime remains useful for self-tests and diagnostics
- H.264 is the first practical codec; AV1, VP9, and VP8 are advertised only when
  the helper runtime can execute them

## Phase Ledger

### Phase 0: Decision Spikes

Status: mostly complete.

Done:

- selected separate helper process instead of embedding media in the GUI process
- selected external relay/SFU direction instead of custom Murmur media fanout
- selected local IPC between client and helper
- kept Mumble as auth/control plane only

Still useful:

- record current real-server bandwidth and latency baselines before promising
  higher resolutions or many viewers

### Phase 1: Capability And Policy Plumbing

Status: implemented.

Done:

- `Version` capability flags
- `ServerConfig` screen-share flags
- per-session support tracking on Murmur
- client-side feature gating
- server config keys and runtime update path

Verification still needed:

- compatibility matrix against old clients/servers in a release-like build

### Phase 2: Murmur Live Session State

Status: implemented enough for experimental use.

Done:

- in-memory active-share/session state
- create/state/stop-style protocol flow
- relay metadata in state messages
- teardown hooks for disconnect and invalid state

Still needed:

- more live-session tests for channel moves, permission loss, duplicate shares,
  and late joiners

### Phase 3: Helper Skeleton

Status: implemented.

Done:

- helper target
- local IPC
- capability query
- source listing
- start/stop publish and view commands
- self-test payload path
- diagnostics logging

Still needed:

- broader automated coverage for IPC error cases and helper restart behavior

### Phase 4: First Media Path

Status: active experimental path.

Done:

- GStreamer/LiveKit capability detection
- helper session planner for publish/view
- Windows capture/runtime probing and native-frame viewing
- Linux Murmur/LiveKit relay integration
- packaged Windows runtime staging checks in shared Qt release paths

Still needed:

- prove real publish/view with two forked clients on the target server
- document observed quality/latency for `720p30`, `1080p30`, and one high-FPS
  path
- verify helper fallback behavior when GStreamer or capture backends are missing
- verify relay TLS/TURN, token expiry, firewall, and service recovery against
  the deployed Linux host

### Phase 5: Mumble UX Integration

Status: implemented for the current experimental feature surface.

Done:

- Modern picker/status path
- quality and audio options
- watch/focus/reopen behavior
- toast-based error surfacing in the Qt Quick shell
- diagnostics setting
- native Qt Quick decoded-frame rendering

Still needed:

- polish late-join presentation and viewer-ready state
- better room-list stream state presentation
- clearer recovery actions after helper/runtime failures

### Phase 6: Other Desktop Clients

Status: outside this delivery.

The production scope is the Windows Qt Quick client plus Linux-hosted
Murmur/relay. Linux and macOS desktop-client capture, shortcuts, rendering,
packaging, and runtime validation require a separate future plan and are not
release gates here.

### Phase 7: Recording

Status: deferred.

Recording should wait until live publish/view is stable. Preferred future shape:
relay-side recording or helper-side subscription that writes asynchronously.

## Testing Plan

Compatibility:

1. new client + new server
2. new client + old server
3. old client + new server
4. old client + old server

Session scenarios:

- start share
- join viewer late
- stop share
- sharer disconnect
- viewer disconnect
- sharer changes channel
- viewer changes channel
- permission revoked mid-session
- server restart
- helper crash/restart

Quality scenarios:

- `720p30` stability
- `720p60`
- `1080p30`
- `1080p60`
- `1440p30`
- multi-viewer fanout
- temporary relay loss

Regression areas:

- standard voice path
- standard text and persistent chat
- channel move behavior
- reconnect logic
- CPU/GPU use on the client
- server memory growth

## Deployment Readiness Checklist

Before calling this production-ready:

- measure actual relay/server uplink throughput
- measure packet loss to typical clients
- verify relay TLS and TURN need
- confirm firewall rules for relay ports
- verify packaged helper reports GStreamer LiveKit publish/view capability
- verify diagnostics logs are sufficient when publish/view fails

## Immediate Next Work

1. Run a live two-client Windows publish/view pass against the target server.
2. Capture diagnostics and screenshots for a successful `720p30` share.
3. Exercise helper failure cases: no GStreamer, capture backend missing, relay
   token expired, helper killed mid-session, and relay service loss.
4. Validate the Windows packaged runtime/MSI and the Linux Murmur/relay
   deployment.
5. Only after that, decide whether screen sharing graduates from experimental
   to a stable fork feature.
