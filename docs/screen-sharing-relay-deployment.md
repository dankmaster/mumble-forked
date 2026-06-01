## Screen-Share Relay Deployment

This fork now supports two practical relay execution modes:

1. `direct-runtime`
   Use `file://`, `rtmp://`, or `rtmps://` relay URLs. The helper executes
   `ffmpeg` or `ffplay` directly.

2. `gstreamer-livekit-runtime`
   Use `http://`, `https://`, `ws://`, or `wss://` relay URLs. The helper
   launches `gst-launch-1.0` with LiveKit WebRTC elements. The Windows publish
   path prefers D3D11 screen capture and H.264 hardware encoding.

### Recommended Production Shape

- `Murmur`: auth, ACL, channel membership, screen-share session state
- `LiveKit`: actual SFU/WebRTC transport
- `mumble-screen-helper`: GStreamer capture, encode, publish, subscribe, and
  detached viewer process supervision

For small Windows-heavy groups, this keeps Murmur out of the media path and
avoids server-side transcoding. The relay web app remains in the tree only as
an explicit fallback/debug path gated by `MUMBLE_SCREENSHARE_ALLOW_RELAY_WEBAPP=1`.

### Server Config

Use these settings in `mumble-server.ini`:

```ini
screen_share_enabled=true
screen_share_relay_url="wss://relay.example.com/mumble-screen"
screen_share_relay_api_key="your-livekit-api-key"
screen_share_relay_api_secret="your-livekit-api-secret"
screen_share_codec_preferences="h264 av1 vp9 vp8"
screen_share_diagnostics_logging=true
```

`screen_share_relay_url` is the URL announced to clients. For the GStreamer
LiveKit runtime:

- if Murmur announces `https://relay.example.com/mumble-screen`, the helper
  uses `wss://relay.example.com/mumble-screen` for the LiveKit WebSocket
- if Murmur announces `wss://relay.example.com/mumble-screen`, the helper uses
  that value directly
- the relay join token is passed to the LiveKit signaller as an auth token, not
  in a browser URL

The helper probes GStreamer with `gst-inspect-1.0` and reports:

- `gstreamer_available`
- `gstreamer_livekit_publish_available`
- `gstreamer_livekit_view_available`
- `gstreamer_missing_elements`
- active capture, encoder, renderer, bitrate, quality profile, and degradation
  metadata in helper replies/logs

Windows shared/MSI builds stage the GStreamer runtime inside the app payload
under `gstreamer\bin`, `gstreamer\lib\gstreamer-1.0`, and
`gstreamer\libexec`. The helper searches beside itself before global `PATH`, so
installed `mumble-forked` clients do not depend on a separate system GStreamer
install.

### GStreamer Launch Contract

The helper receives these IPC fields from the client/session state:

- `relay_url`
- `relay_room_id`
- `relay_session_id`
- `stream_id`
- `relay_role`
- `codec`
- `requested_codec`
- `transport`
- `width`
- `height`
- `fps`
- `bitrate_kbps`
- `min_bitrate_kbps`
- `max_bitrate_kbps`
- `quality_profile`
- `capture_source_id`

For the GStreamer WebRTC runtime, current clients request H.264 first. The
Windows publisher path prefers:

- `d3d11screencapturesrc`
- `nvd3d11h264enc`
- `mfh264enc`
- `x264enc` / `openh264enc` fallback

The default picker selection is `720p30` with the `auto` compression profile:
about `4000 kbps` start, `1200 kbps` minimum, and `6000 kbps` maximum. The
publisher picker also exposes `720p`, `1080p`, and `1440p` at `30 FPS` or
`60 FPS` when the server and local runtime advertise those limits. The first
adaptive degradation policy is FPS first, then resolution.

The helper currently launches GStreamer as an external process and records
planned/active capture, encoder, renderer, bitrate, dropped-frame placeholders,
and adaptive downgrade reason in the IPC reply. Deeper WebRTC stats should be
added through a native GStreamer controller once the pipeline is embedded.

### LiveKit Token Contract

When both `screen_share_relay_api_key` and `screen_share_relay_api_secret` are
configured and the relay transport is WebRTC, Murmur now mints LiveKit-style
JWT join tokens per recipient. Tokens are short-lived and are refreshed by
Murmur when it resends session state.

Publisher grant:

- `roomJoin=true`
- `canPublish=true`
- `canPublishSources=["camera", "screen_share"]`
- when system audio is requested, also `["microphone", "screen_share_audio"]`
- `canSubscribe=false`

The GStreamer `livekitwebrtcsink` producer path expects a publish-only token.
If a publisher token can also subscribe, LiveKit can leave the producer in the
join path until it closes the connection with `JOIN_TIMEOUT`. Current
GStreamer LiveKit builds expose the WASAPI loopback audio pad as a regular
LiveKit microphone source, so audio-enabled screen-share sessions must allow
`microphone` until the helper can mark that pad as `screen_share_audio`
explicitly.

Viewer grant:

- `roomJoin=true`
- `canPublish=false`
- `canSubscribe=true`

Rotate any proof-of-concept LiveKit API secret before exposing a real relay.
Deploy the new Murmur config and LiveKit key/secret together, then restart
Murmur so newly minted screen-share tokens are signed with the new secret. Old
join tokens are intentionally short-lived and should expire without manual
cleanup.

### Relay Hardening Checklist

- Serve the LiveKit WebSocket over TLS from the public
  `screen_share_relay_url`; do not expose the internal LiveKit API port directly
  without a reverse proxy or load balancer.
- If the LiveKit WebSocket is behind nginx, Caddy, Traefik, or another reverse
  proxy, set the WebSocket read/send/idle timeouts longer than the relay token
  lifetime. A default 60-second proxy timeout will drop active GStreamer
  sessions with `Signalling error: Error: Server disconnected`.
- If the WebSocket stays alive with ping/pong but LiveKit logs `JOIN_TIMEOUT`
  at roughly 60 seconds, inspect publisher JWT grants, ICE candidate reachability,
  and whether the GStreamer publisher reaches an active peer connection.
- Keep `7881/tcp` exposed for WebRTC-over-TCP fallback and either expose
  `7882/udp` when using LiveKit UDP mux or the configured UDP port range when
  not using mux.
- Monitor the relay process, TLS endpoint, WebSocket upgrade path, and LiveKit
  Prometheus metrics if `prometheus_port` is enabled in the LiveKit config.
- Keep Murmur `screen_share_diagnostics_logging=true` during rollout so codec
  negotiation and relay connection failures are visible, then reduce verbosity
  after the relay has been stable.
- Verify each deploy with one publisher and one viewer: negotiated codec H.264,
  GStreamer publish/view capability true, stable frame rate, and no fallback
  request. Then test one publisher with eight viewers.

### UX Expectation

Users should not manually copy URLs.

- `Start Screen Share`: Mumble starts the helper, the helper starts the
  GStreamer publisher pipeline, and the user sees normal themed status in the
  app shell
- `Watch Screen Share`: Mumble starts the helper and opens the GStreamer viewer
  window

The browser relay app is not the normal implementation path.
