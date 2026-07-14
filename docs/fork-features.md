# Fork Feature Inventory

Status snapshot: 2026-06-05.

This document describes the fork-specific surface area in this repository. It
is not an official Mumble roadmap, and several features are intentionally scoped
to this community fork. The intended fork client path is now modern-only, but
core Mumble compatibility remains a protocol constraint: unsupported fork
features should degrade without breaking normal voice and basic text chat.

For the short current-state handoff, see
[`status-and-roadmap.md`](status-and-roadmap.md).

## Upstream Baseline Retained

The fork keeps the normal Mumble foundation:

- low-latency Opus voice chat
- encrypted client/server sessions
- channels, ACLs, groups, comments, and registration
- server administration paths
- local recording controls, shortcuts, plugin infrastructure, and positional-audio plugin support
- cross-platform CMake build structure and the upstream documentation tree

## Modern Client Shell

The Modern shell is the visible fork client path. It is a native Qt Quick client
surface built around persistent chat, rich media, direct messages, and modern
dialogs. No classic Qt Widgets product layout or hidden compatibility view is
created. Qt Widgets remains linked only for documented operating-system and
third-party-plugin surfaces.

Current capabilities:

- persistent chat as the primary center pane
- direct-message tray with private in-memory mode and persistent-history mode
  when the server and registered users support it
- room-aware composer state and send controls
- file-picker, drag-and-drop, clipboard-file, and pasted-bitmap attachment input
- typed attachment drafts with validation, upload progress, retry, and removal
- dedicated voice-room and text-room navigation
- server MOTD/sidebar presentation
- direct server-log rendering for the Modern timeline path
- compact message controls for reply/delete actions
- rich media and provider-specific preview cards
- playable YouTube/video preview controls through a lazy, isolated WebEngineQuick media surface
- image, GIF, video, product, Steam/game-store, social posts including X/Twitter, GitHub, finance, forum, article, map, place, traffic, and weather card layouts
- local disk preview cache with selective session refresh for richer providers
- theme, accent, density, dialog, and context-menu polish for the Modern shell
- update banners, feedback, and crash-report handoff flows that avoid publishing private local context by default
- a minimal Qt Quick startup failure notice instead of a classic client fallback

Tracked public screenshot assets:

- [`../screenshots/modern-shell-showcase.png`](../screenshots/modern-shell-showcase.png)
- [`../screenshots/modern-client-rich-chat.png`](../screenshots/modern-client-rich-chat.png)
- [`../screenshots/modern-rich-card-youtube.png`](../screenshots/modern-rich-card-youtube.png)
- [`../screenshots/modern-theme-settings.png`](../screenshots/modern-theme-settings.png)
- [`../screenshots/modern-feedback-report.png`](../screenshots/modern-feedback-report.png)
- [`../screenshots/modern-context-menu.png`](../screenshots/modern-context-menu.png)
- [`../screenshots/modern-crash-github-submit.png`](../screenshots/modern-crash-github-submit.png)
- [`../screenshots/modern-stonks-overview.png`](../screenshots/modern-stonks-overview.png)

## Persistent Chat

Persistent chat is a forked client + server protocol, not an overload of the
legacy transient `TextMessage` path.

Current capabilities:

- persistent thread storage in the server database
- voice-channel chat history
- dedicated text channels / text rooms
- optional server-global chat thread through `persistentglobalchat`
- private/direct-message history when `ChatFeatureDirectMessages` is advertised
  and both users have registered identities
- private non-persistent direct-message mode for the normal transient text path
- ACL-filtered aggregate readable-history views
- history pagination / "load older"
- history warmup for active rooms and direct messages
- read state and unread state
- message replies with actor/snippet metadata
- message deletion with a dedicated ACL permission
- reactions and reaction aggregates
- actor avatar hashes for richer Modern timeline presentation
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
- `ChatHistoryWarmupRequest`
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
- text-plus-attachment and attachment-only persistent messages
- image previews with explicit original-file saving, plus safe download cards for other media and files
- per-message attachment count limits
- per-asset and total-storage quota controls
- atomically committed, SHA-256-addressed local object storage under a per-server root
- temporary upload cleanup for abandoned incoming files
- grace-period cleanup for committed objects that are no longer referenced by a live message or embed
- image normalization for raster formats, including EXIF orientation handling and metadata stripping by re-encoding
- server-generated preview derivatives for uploaded images
- preview-cache assets for selected remote URL media
- MIME allowlisting for images, video, audio, documents, archives, and opaque binary downloads
- persistent-chat protocol-version gating so incompatible peers cannot negotiate native attachments; older clients receive readable attachment placeholders

Important server keys:

```ini
chat_asset_storage_path=chat-assets
chat_asset_max_bytes=26214400
chat_asset_total_quota_bytes=2147483648
chat_attachment_limit=4
chat_preview_fetch_enabled=false
chat_preview_client_assist_enabled=true
chat_preview_client_assist_lease_ms=30000
chat_preview_client_assist_fallback_ms=3500
chat_preview_client_assist_thumbnail_max_bytes=524288
```

See [`rich-chat-server.md`](rich-chat-server.md) for rollout and storage notes.

## Link Preview Cards

The preview system is server-authoritative but client-assisted. Murmur creates
pending embeds, leases at most one public HTTPS preview attempt to one capable
client, sanitizes any returned thumbnail bytes, stores metadata/assets itself,
and falls back to its own server-side fetch path if no usable client result
arrives quickly.

Current provider families include:

- direct images, GIFs, WebM, and common video/audio links
- YouTube and YouTube Shorts
- Twitch videos and clips
- Streamable, Vimeo, Dailymotion, and TikTok video cards
- Spotify and SoundCloud links
- X/Twitter, Reddit, Facebook, Instagram, Bluesky, Threads, Patreon, Imgur, and 4chan
- GitHub repository cards
- Steam and game-store links, including Epic Games Store, GOG, Ubisoft, EA, Humble Store, Fanatical, Green Man Gaming, itch.io, Battle.net, and Xbox Store
- finance links for Yahoo Finance, Google Finance, X cashtags, Avanza, Nordnet, and Interactive Brokers
- Swedish/product/listing providers including Tradera, Blocket, Prisjakt, PriceRunner, Inet, Webhallen, Elgiganten, POWER, Komplett, Systembolaget, and Amazon
- forum/article/audio providers including Flashback, SweClockers, Existenz, GP, SVT, Omni, Aftonbladet, Expressen, DN, and Sveriges Radio
- real-estate providers including Hemnet and Booli
- weather/place/traffic providers including SMHI, Klart, Yr, Hitta, Eniro, Google Maps, SJ, SL, and Vasttrafik

Safety behavior:

- localhost and private-network targets are blocked for automatic previews
- the server-side preview fetcher is disabled by default with `chat_preview_fetch_enabled=false`
- when previews are enabled, client assist is enabled by default; set
  `chat_preview_client_assist_enabled=false` to force server-only fetching
- TikTok video URLs render with the official iframe player when a post ID is available, while oEmbed still supplies title and thumbnail metadata

## Fork Feature Gates

Fork experiments use explicit feature advertisement instead of guessing from
release strings or UI shape.

Current fork feature gates include:

- server link-preview proxy support
- room-scoped watch-together sessions
- richer screen-share session presence
- virtualized chat presentation contracts
- server-backed Stonks ledger state
- client-assisted, server-authoritative link previews
- in-app feedback handoff to an administrator-configured GitHub target

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
- server-backed portfolio ledger with versioned position history, privacy-filtered state, and leaderboard windows
- manual score commands as a legacy fallback, plus follows and user summary commands
- server database tables for scores, follows, feed preferences, pinned tickers,
  portfolio history, and saved positions
- Modern Stonks panel for overview, portfolio ledger, leaderboard, following, and admin config
- client-local, opt-in rolling Stonks ticker banner for the Modern conversation header
- optional social announcements in the configured Stonks text channel

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
- GStreamer-first LiveKit WebRTC helper runtime for screen-share publish/view
- Windows helper probing for GStreamer D3D11 LiveKit capture, Windows Graphics
  Capture, D3D11 Desktop Duplication, `gdigrab`, and test-pattern capture
- Linux helper probing for X11/ffmpeg capture, test-pattern capture, and
  PipeWire runtime diagnostics
- helper self-test and local runtime verification hooks for packaged builds
- shared/WebEngine and `mumble-forked` Windows release payloads stage the
  bundled GStreamer runtime under `gstreamer\` for installed clients
- LiveKit-compatible relay-token path when API key/secret are configured

Important server keys:

```ini
screen_share_enabled=false
screen_share_recording_enabled=false
screen_share_helper_required=true
screen_share_codec_preferences="h264 av1 vp9 vp8"
screen_share_max_width=3840
screen_share_max_height=2160
screen_share_max_fps=144
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
shared/WebEngine Windows client payload. The shared/WebEngine lane is the
canonical fork client lane because it carries the Modern shell.

Current build/release features:

- static Windows server validation in `CI`
- Linux server build and one practical Linux `ctest` lane in `CI`
- separate `Windows Shared Client Installer` workflow for the WebEngine payload
- reusable `Windows Shared Build Environment` workflow/archive
- manual `mumble-forked MSI Release` workflow for a stable unsigned convenience MSI
- generated `changelog.md`
- generated `mumble-forked-update.json`
- in-app package update prompt/download/install handoff for fork release
  manifests, with MSI fallback on package apply failure
- one-shot update resume snapshot for reopening the client on the same server,
  voice room, chat view, and saved window layout where possible

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
- `Connect to the last server on startup` is a client-side network setting; update
  restarts also use a one-shot client resume snapshot when the user accepts the update

## Legacy Cuts

The fork has intentionally removed several older or unused client/runtime
features from the product direction:

- ASIO input path
- G15/LCD support
- PositionalAudioViewer developer window
- in-game overlay subprojects and launcher paths
- TalkingUI and related overlay-facing settings

Positional audio itself, the plugin manager, and the manual plugin remain in
scope.

## Compatibility Notes

Fork-only behavior should be capability-gated:

- new client + new server: fork features may activate
- new client + old server: voice and basic text chat should continue, while fork features stay hidden or disabled
- old client + new server: normal Mumble behavior should continue, and unsupported fork messages should not be sent
- ordinary upstream/native clients should still connect to the forked server for
  baseline voice, channel, ACL, registration/certificate, and basic text
  behavior
- those clients are not expected to receive full persistent rich chat or screen-share parity
- fork desktop clients use the Qt Quick Modern shell as the visible layout;
  classic layout is not a user-facing compatibility promise

## Known Gaps

The feature set is still moving. Current known gaps include:

- remaining Qt Widgets/native dialog surfaces such as plugin settings, plugin
  install/update, Search, Voice Recorder, and selected certificate flows need a
  final modernize-or-keep decision
- no full production promise for screen sharing
- screen-share publish/view needs more live validation across the target Windows
  runtime before it should be called stable
- no video transcoding or poster extraction for uploaded video assets
- no document preview rendering
- no long-horizon preview-cache pruning policy
- no promise that all provider-specific preview parsers survive remote website changes
