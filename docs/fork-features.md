# Fork Feature Inventory

Status snapshot: 2026-05-26.

This document describes the fork-specific surface area in this repository. It
is not an official Mumble roadmap, and several features are intentionally scoped
to this community fork. Core Mumble compatibility remains a design constraint:
unsupported fork features should degrade without breaking normal voice and basic
text chat.

## Upstream Baseline Retained

The fork keeps the normal Mumble foundation:

- low-latency Opus voice chat
- encrypted client/server sessions
- channels, ACLs, groups, comments, and registration
- classic Qt client UI and classic server administration paths
- local recording controls, shortcuts, overlay/plugin infrastructure, and positional-audio plugin support
- cross-platform CMake build structure and the upstream documentation tree

## Modern Client Shell

The modern shell is an optional WebEngine-based client surface built around the
persistent-chat work. It keeps the classic client available as a fallback.

Current capabilities:

- persistent chat as the primary center pane
- room-aware composer state and send controls
- dedicated voice-room and text-room navigation
- server MOTD/sidebar presentation
- compact message controls for reply/delete actions
- rich media and provider-specific preview cards
- playable YouTube/video preview controls when the provider and WebEngine surface support inline playback
- image, GIF, video, product, Steam/game-store, social posts including X/Twitter, GitHub, finance, forum, article, map, place, traffic, and weather card layouts
- local disk preview cache with selective session refresh for richer providers

Tracked public screenshot assets:

- [`../screenshots/modern-client-rich-chat.png`](../screenshots/modern-client-rich-chat.png)
- [`../screenshots/modern-rich-card-youtube.png`](../screenshots/modern-rich-card-youtube.png)
- [`../screenshots/Mumble.png`](../screenshots/Mumble.png)

## Persistent Chat

Persistent chat is a forked client + server protocol, not an overload of the
legacy transient `TextMessage` path.

Current capabilities:

- persistent thread storage in the server database
- voice-channel chat history
- dedicated text channels / text rooms
- optional server-global chat thread through `persistentglobalchat`
- ACL-filtered aggregate readable-history views
- history pagination / "load older"
- read state and unread state
- message replies with actor/snippet metadata
- message deletion with a dedicated ACL permission
- reactions and reaction aggregates
- history grants for controlled access
- server-to-client feature negotiation through advertised chat features
- fallback behavior for clients or servers that do not advertise support

Primary protocol and storage features include:

- `ChatSend`
- `ChatMessage`
- `ChatHistoryRequest`
- `ChatHistoryResponse`
- `ChatReadStateUpdate`
- `ChatDelete`
- `ChatReactionToggle`
- `ChatReactionState`
- `TextChannelSync`
- `chat_threads`
- `chat_messages`
- `chat_read_state`
- `chat_message_reactions`
- `chat_history_grants`
- `text_channels`

## Rich Media And Assets

Rich chat media is stored by Murmur and transferred over the existing
authenticated Mumble control connection.

Current capabilities:

- upload initialization, chunk upload, commit, and ranged download
- per-message attachment count limits
- per-asset and total-storage quota controls
- local filesystem object storage under a per-server root
- temporary upload cleanup for abandoned incoming files
- image normalization for raster formats, including EXIF orientation handling and metadata stripping by re-encoding
- server-generated preview derivatives for uploaded images
- preview-cache assets for selected remote URL media
- MIME allowlisting for images, videos, documents, and binary downloads

Important server keys:

```ini
chat_asset_storage_path=chat-assets
chat_asset_max_bytes=26214400
chat_asset_total_quota_bytes=2147483648
chat_attachment_limit=4
chat_preview_fetch_enabled=false
```

See [`rich-chat-server.md`](rich-chat-server.md) for rollout and storage notes.

## Link Preview Cards

The preview system combines server-fetched metadata, client-side fallback
fetches, provider-specific parsing, safe-target checks, thumbnail caching, and
WebEngine card rendering.

Current provider families include:

- direct images, GIFs, WebM, and common video/audio links
- YouTube and YouTube Shorts
- Twitch videos and clips
- Streamable, Vimeo, Dailymotion, and TikTok oEmbed cards
- Spotify and SoundCloud links
- X/Twitter, Reddit, Facebook, Instagram, Bluesky, Threads, Patreon, Imgur, and 4chan
- GitHub repository cards
- Steam and game-store links, including Epic Games Store, GOG, Ubisoft, EA, Humble Store, Fanatical, Green Man Gaming, itch.io, Battle.net, and Xbox Store
- finance links for Yahoo Finance, Google Finance, X cashtags, Avanza, Nordnet, and Interactive Brokers
- Swedish/product/listing providers including Tradera, Blocket, Prisjakt, PriceRunner, Inet, Webhallen, Elgiganten, Komplett, Systembolaget, and Amazon
- forum/article/audio providers including Flashback, SweClockers, Existenz, GP, SVT, Omni, Aftonbladet, Expressen, DN, and Sveriges Radio
- real-estate providers including Hemnet and Booli
- weather/place/traffic providers including SMHI, Klart, Yr, Hitta, Eniro, Google Maps, SJ, SL, and Vasttrafik

Safety behavior:

- localhost and private-network targets are blocked for automatic previews
- the server-side preview fetcher is disabled by default with `chat_preview_fetch_enabled=false`
- TikTok is kept oEmbed-only in the current client because the iframe player can show an unhideable cross-origin cookie wall inside Qt WebEngine

## Fork Feature Gates

Fork experiments use explicit feature advertisement instead of guessing from
release strings or UI shape.

Current fork feature gates include:

- server link-preview proxy support
- room-scoped watch-together sessions
- richer screen-share session presence
- virtualized chat presentation contracts

See [`fork-extension-architecture.md`](fork-extension-architecture.md).

## Watch Together

Watch together currently has protocol and server-side room-session plumbing. It
is not presented as a finished user-facing client feature yet.

Current capabilities:

- `WatchTogetherSync` protocol messages
- direct-media and YouTube source kinds
- start, state, join, leave, state-request, end, and host-transfer events
- room-scoped server session state
- late-join state reannouncement to clients that advertise support
- host-transfer validation against connected clients that advertise support

## Finance And Stonks

The fork includes a small finance/stonks feature set for a scoped text room.

Current capabilities:

- cashtag extraction from normal chat text, such as `$RKLB`
- Yahoo Finance quote URL generation
- Yahoo chart parsing for quote metadata, day change, and recent close points
- finance cards with provider links and sparkline metadata
- command parsing for `quote <ticker>` and `quote $ticker`
- a `#stonks` room command handler scoped to that text channel
- manual scores, leaderboard windows, follows, and user summary commands
- server database tables for scores and follows

Typical commands:

```text
rklb
quote rklb
score 30d +12.3
leaderboard 30d
follow <user>
unfollow <user>
following
me
```

See [`dev/maintenance/StonksLiveServerUpdate.md`](dev/maintenance/StonksLiveServerUpdate.md)
for schema and deployment notes.

## Screen Sharing

Screen sharing is experimental. The fork treats Mumble as the control plane and
keeps video media out of the normal voice transport.

Current capabilities:

- feature advertisement for signaling, capture, viewing, codecs, and maximum dimensions
- server configuration for enablement, recording policy, helper requirement, codec preferences, relay URL, LiveKit credentials, dimensions, FPS, and diagnostics
- per-session capability gating so old clients and servers keep normal voice/chat behavior
- external `mumble-screen-helper` process
- helper IPC and runtime diagnostics
- relay/WebRTC browser shell under [`../relay-webapp/`](../relay-webapp/)
- LiveKit-compatible relay-token path when API key/secret are configured

Important server keys:

```ini
screen_share_enabled=false
screen_share_recording_enabled=false
screen_share_helper_required=true
screen_share_codec_preferences="vp8 h264 av1 vp9"
screen_share_max_width=1920
screen_share_max_height=1080
screen_share_max_fps=60
screen_share_relay_url="wss://relay.example.com/mumble-screen"
screen_share_diagnostics_logging=false
```

See [`screen-sharing-architecture.md`](screen-sharing-architecture.md) and
[`screen-sharing-relay-deployment.md`](screen-sharing-relay-deployment.md).

## Speech Cleanup

The client includes experimental speech-cleanup model plumbing and local
verification tools.

Current model families:

- RNNoise embedded model
- RNNoise little model
- RNNoise custom model path
- DTLN baseline
- DTLN `norm500h`
- DTLN `norm40h`
- DeepFilterNet default, gentle, balanced, low-latency, maximum, and maximum-postfilter profiles

The benchmark target is `speech_cleanup_benchmark.exe`, with local wrappers for
offline corpus comparison and packaged-model smoke coverage.

## Windows Builds And Fork Releases

The fork keeps separate lanes for normal PR validation and the heavier
shared/WebEngine Windows client payload.

Current build/release features:

- static Windows client/server validation in `CI`
- Linux server build and one practical Linux `ctest` lane in `CI`
- separate `Windows Shared Client Installer` workflow for the WebEngine payload
- reusable `Windows Shared Build Environment` workflow/archive
- manual `mumble-forked MSI Release` workflow for a stable unsigned convenience MSI
- generated `changelog.md`
- generated `mumble-forked-update.json`
- in-app update popup support for fork release manifests

See [`windows-builds.md`](windows-builds.md).

## Fork Identity And Update Controls

The client includes small deployment controls that are useful for a private fork
without changing the numeric Mumble protocol version.

Current controls:

- hidden advertised release-string override
- hidden advertised OS override
- hidden advertised OS-version override
- `MUMBLE_FORK_UPDATE_URL` for overriding the GitHub release API URL
- `MUMBLE_FORK_UPDATE_MANIFEST_URL` for pointing at a generated update manifest
- `MUMBLE_FORK_FORCE_UPDATE_NOTIFICATION` for local preview of update popups
- automatic startup update checks skip the public `mumble-forked` manifest for local build-number-0 clients unless an override is set

## Compatibility Notes

Fork-only behavior should be capability-gated:

- new client + new server: fork features may activate
- new client + old server: voice and basic text chat should continue, while fork features stay hidden or disabled
- old client + new server: normal Mumble behavior should continue, and unsupported fork messages should not be sent
- stock clients are not expected to receive full persistent rich chat or screen-share parity

## Known Gaps

The feature set is still moving. Current known gaps include:

- no full production promise for screen sharing
- no video transcoding or poster extraction for uploaded video assets
- no document preview rendering
- no long-horizon preview-cache pruning policy
- no promise that all provider-specific preview parsers survive remote website changes
