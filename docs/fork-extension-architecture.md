# Fork Extension Architecture Notes

This fork keeps product experiments behind explicit protocol features instead
of inferring support from release strings or UI shape. Each fork feature has a
minimum protocol revision and a fallback policy in `ForkFeature.cpp`.

Status snapshot: 2026-06-05.

Current fork extension protocol revision: `3`.

## Current Feature Scope

- `ForkFeatureServerLinkPreviewProxy`: server-side link-preview fetching,
  thumbnail sanitizing/downscaling, direct GIF/WebM media caching, cache
  retention, and delivery through the existing `ChatEmbedRef` and
  `ChatAssetRef` model.
- `ForkFeatureClientAssistedLinkPreviews`: protocol revision 2 client assist
  for link previews, where Murmur leases one pending embed to one capable
  client but remains the authority that stores assets and broadcasts state.
- `ForkFeatureWatchTogetherRooms`: ephemeral, room-scoped synchronized media
  sessions that are independent of screen sharing.
- `ForkFeatureScreenShareSessionPresence`: richer screen-share state for late
  joiners, diagnostics, viewer counts, and thumbnails.
- `ForkFeatureVirtualizedChatPresentation`: a client presentation contract for
  high-volume chat timelines and media cards.
- `ForkFeatureStonksLedger`: server-backed social finance portfolio state,
  follows, leaderboard state, feed preferences, and Modern Stonks presentation.
- `ForkFeatureInAppFeedback`: client feedback reports that Murmur can forward
  to an administrator-configured GitHub target.

Drawing overlays are not part of the current fork direction. The old in-game
overlay and TalkingUI paths have been cut; future collaborative annotation
would need a new product decision and a separate permission model.

## Link Previews

The link-preview path is server-authoritative. Murmur creates pending embed
rows, can lease one URL to one capable client for bounded metadata/thumbnail
work, and still performs all preview persistence, asset sanitizing, quota
checks, and `ChatEmbedState` broadcasts itself. If no client response wins the
race, Murmur falls back to its own bounded public HTTPS fetch path.

The hardening baseline remains:

- re-validate DNS and blocked-address policy after every redirect
- cap response body, content type, redirect count, and per-host concurrency
- cache completed preview metadata by canonical URL hash
- preserve direct `.gif` and `.webm` links as cached playable media assets
  instead of flattening them into still thumbnails
- expose clear error codes to clients without leaking fetch internals
- prefer server-stored thumbnails over direct third-party image loads

Autoplay starts conservative. Animated GIFs render as normal images in Modern
UI, while WebM embeds use native video controls, muted/loop-capable playback,
and no autoplay by default. A client setting can later switch WebM preload or
autoplay behavior without changing the server contract.

## Watch Together

Watch-together sessions are room sessions, not screen-share sessions. The server
stamps the actor session, validates current-room membership and `TextMessage`
permission, stores the latest session state in memory, and answers
`WatchTogetherEventStateRequest` for late joiners.

The UI layer can start simple: a chat/header media card with play/pause/seek
sync, host transfer, join/leave, and a provider adapter for direct media and
YouTube.

## Screen-Share Presence

The current screen-share relay/token model remains the right core. The active
implementation already carries signaling, server policy, helper IPC, and
GStreamer/LiveKit-capability plumbing. The next value is better state
presentation and verification: late join reannounce, explicit viewer-ready
state, diagnostics, stats, and eventually thumbnail assets.

## Chat Performance

The performance lesson is presentation, not a rewrite. Keep server paging and
asset/embed delivery, then make the Modern UI render chat as a virtualized
timeline with stable media-card dimensions, lazy asset hydration, and batched
DOM updates. That should address the slow path without changing the transport
model.
