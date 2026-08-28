# LiveKit Relay Setup (server operator guide)

Mumble-forked screen sharing does **not** carry video through Murmur. Murmur only
coordinates screen-share sessions and mints short-lived join tokens; the actual
WebRTC transport is a **LiveKit** server (the SFU) that the client helper
publishes to. This guide sets up that LiveKit instance and wires it into
`mumble-server`.

Target audience: the person running the private community server. Your users
only need the client (`scripts/install-linux-client.sh`); they do not need this
guide.

## Components

- `livekit-server` — the WebRTC SFU/media relay (separate service, not in this repo).
- `mumble-server` — forks; announces the relay URL and mints LiveKit JWTs.
- Client `mumble-screen-helper` — GStreamer capture/publish, connects to LiveKit.

## 1. Install livekit-server

Pick one:

- **Standalone binary** (recommended for a single host):
  `https://github.com/livekit/livekit/releases` — download the `livekit-server`
  static binary for your arch, `chmod +x`, and place it in `PATH`.
- **Docker**:
  `docker run --rm livekit/livekit-server --version` (see the official LiveKit docs).

Generate an API key pair:

```bash
livekit-server generate-keys
# prints something like:
#   API Key:    APImumble...
#   API Secret: <base64-secret>
```

Copy the key and secret — you will paste them into both `livekit.yaml` and
`mumble-server.ini`.

## 2. Configure livekit-server

Create `livekit.yaml`:

```yaml
port: 7880          # WebSocket signal port
bind_addresses:
  - ""
rtc:
  tcp_port: 7881
  port_range_start: 50000
  port_range_end: 50100
  use_external_ip: true
keys:
  APImumble...: "<base64-secret>"
logging:
  level: info
```

Notes:

- `port` is where LiveKit serves the WebSocket the client helper connects to
  (`wss://` if you terminate TLS in front, or `ws://` on a LAN). The helper
  normalizes an announced `https://`/`wss://` relay URL to the LiveKit websocket.
- `rtc.port_range_start/end` is the **UDP** range for media; open it in the
  firewall.
- Keep `keys` in sync with the pair you generated.

Run it:

```bash
livekit-server --config livekit.yaml
```

## 3. Wire it into mumble-server

Edit `auxiliary_files/mumble-server.ini` (the config used by the `mumble-server`
binary you ship) and uncomment/set:

```ini
screen_share_enabled=true
screen_share_relay_url="wss://relay.example.com/mumble-screen"
screen_share_relay_api_key="APImumble..."
screen_share_relay_api_secret="<base64-secret>"
screen_share_codec_preferences="h264 av1 vp9 vp8"
screen_share_diagnostics_logging=false
```

- `screen_share_relay_url` is what Murmur announces to clients. If you terminate
  TLS in front of LiveKit, use `wss://`; on a trusted LAN you can use
  `ws://host:7880`. The trailing path is arbitrary (the helper uses the URL's
  scheme/host as the LiveKit websocket endpoint).
- When both `screen_share_relay_api_key` and `screen_share_relay_api_secret`
  are set, Murmur mints short-lived LiveKit-compatible JWT join tokens per
  viewer/publisher with join+publish grants. Keep the key/secret server-side
  only — they must not reach clients directly.
- Optionally set `screen_share_max_width/height/fps` if you want to cap quality.

Restart `mumble-server` after editing.

## 4. Firewall / ports

On the LiveKit host open:

- **TCP 7880** (signal WebSocket) — and 443 if you front it with TLS.
- **UDP 50000-50100** (media) — the `rtc.port_range_*` range above.

## 5. Verify

- On a client, run `gst-inspect-1.0 --exists livekitwebrtcsink`; it should
  succeed (the helper uses `livekitwebrtcsink`/`livekitwebrtcsrc` for
  publish/view).
- Share a screen from the client; with `screen_share_diagnostics_logging=true`
  the Murmur log and the helper's `screen-share-helper.log` will show session
  state, and the client helper should connect to LiveKit and stream.
- A missing/incorrect key/secret shows up as LiveKit rejecting the join token;
  confirm the `keys` in `livekit.yaml` exactly match the API key/secret in
  `mumble-server.ini`.

## Related docs

- `docs/screen-sharing-relay-deployment.md` — full relay contract and config.
- `docs/screen-sharing-architecture.md` — how the helper, Murmur, and the relay fit together.
