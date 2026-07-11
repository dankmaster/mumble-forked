# Chat Architecture

Status snapshot: 2026-06-05.

This document describes the fork-specific chat system as it exists today. It is
a client + server subsystem, not an overload of Mumble's transient
`TextMessage` path.

## Goal

The fork chat goal is to make rooms feel persistent and media-capable while
keeping ordinary Mumble voice behavior intact:

1. stored voice-room and text-room history
2. optional server-global chat
3. direct messages with persistent history when the server and identities allow
   it
4. rich link previews and typed media assets
5. capability-gated fallback for unsupported clients and servers

## Why This Needs A Fork

Upstream Mumble text chat is routed as a transient `TextMessage` with only:

- sender session
- target sessions
- target channels / trees
- message body

The fork adds first-class concepts that need protocol messages, server storage,
and Modern client UI:

- chat threads
- stable message IDs
- history pagination
- read state
- replies
- deletion
- reactions
- typed attachments
- structured embeds
- direct-message history

## Compatibility Direction

Fork chat is negotiated explicitly:

- new forked clients and servers use the `Chat*` protocol messages
- old clients and servers keep voice and basic text chat behavior where
  practical
- unsupported fork messages are not sent to clients that have not advertised
  support
- ordinary upstream/native clients can still connect to the forked server for
  baseline Mumble behavior
- those clients are not expected to render full persistent rich chat

The transient `TextMessage` path still exists for basic interoperability and for
private non-persistent direct-message mode, but it is not the primary wire
format for persistent rooms.

## Protocol Surface

Fine-grained support is advertised through `ChatFeature` values in
`Version` and `ServerConfig`.

Current feature bits:

- `ChatFeaturePersistentHistory`
- `ChatFeatureHistoryPagination`
- `ChatFeatureReadState`
- `ChatFeatureReactions`
- `ChatFeatureMessageDelete`
- `ChatFeatureAttachments`
- `ChatFeatureEmbeds`
- `ChatFeatureHistoryGrants`
- `ChatFeatureTextChannels`
- `ChatFeatureDirectMessages`
- `ChatFeatureHistoryWarmup`
- `ChatFeatureActorAvatars`

Current protocol messages include:

- `ChatSend`
- `ChatMessage`
- `ChatMessageDelete`
- `ChatHistoryRequest`
- `ChatHistoryWarmupRequest`
- `ChatHistoryResponse`
- `ChatReadStateUpdate`
- `ChatHistoryGrantSync`
- `ChatAssetUploadInit`
- `ChatAssetUploadChunk`
- `ChatAssetUploadCommit`
- `ChatAssetState`
- `ChatAssetRequest`
- `ChatAssetChunk`
- `ChatEmbedState`
- `ChatEmbedAssistRequest`
- `ChatEmbedAssistResult`
- `ChatReactionToggle`
- `ChatReactionState`
- `TextChannelSync`

## Scope Model

Current chat scopes are:

- `Channel`: persistent history for a voice room
- `TextChannel`: persistent history for a dedicated text room
- `ServerGlobal`: optional server-global persistent thread, gated by
  `persistentglobalchat`
- `Private`: direct-message history when both endpoint support and registered
  identities are available
- `Aggregate`: client presentation scope for combined readable activity; not a
  normal send target

Private/direct-message behavior is intentionally dual-mode in the Modern
client. When the server advertises `ChatFeatureDirectMessages` and both users
have registered identities, the client can request persistent private history.
Otherwise the Modern direct-message tray uses private non-persistent
`TextMessage` transport and keeps it separate from room chat.

## Server Model

The server stores persistent chat in first-class tables. The important current
tables include:

- `chat_threads`
- `chat_messages`
- `chat_read_state`
- `chat_message_reactions`
- `chat_history_grants`
- `chat_assets`
- `chat_message_attachments`
- `chat_message_embeds`
- `text_channels`

Thread scope keys are canonicalized by scope. Examples:

- `channel:42`
- `text-channel:7`
- `global`
- `private:12:98`

### Read-only administrative history API

The fork's ICE `Server` interface exposes bounded, read-only history methods
for trusted administrative tooling:

- `getPersistentChatThreads(offset, limit)` returns conversations ordered by
  most recent activity
- `getPersistentChatMessages(threadId, beforeMessageId, limit)` returns a
  chronological page, with `beforeMessageId = 0` selecting the latest page

Both methods require the ICE read secret, cap pages at 200 records, and omit
deleted message bodies. They are intended for an authenticated admin surface;
they do not replace client-side history permissions or the normal chat wire
protocol.

History visibility follows channel permissions for room/text scopes. Private
threads are visible only to the registered users that form the private scope.

## Rich Media Model

Attachments and preview assets are stored by Murmur and transferred over the
authenticated Mumble control connection. The current media path supports:

- upload initialization, chunk upload, commit, and ranged download
- per-message attachment count limits
- per-asset and total-storage quota controls
- local filesystem object storage under a per-server root
- temporary upload cleanup for abandoned incoming files
- image normalization, EXIF orientation handling, and metadata stripping
- server-generated preview derivatives for uploaded images
- preview-cache assets for selected remote URL media
- MIME allowlisting for images, videos, documents, and binary downloads

See [`rich-chat-server.md`](rich-chat-server.md) for operations and rollout
details.

## Embed / Preview Model

The preview system is server-authoritative and optionally client-assisted:

- Murmur creates pending embed rows for supported URLs.
- Murmur may lease one public HTTPS preview attempt to one capable client.
- The assisting client returns bounded metadata and thumbnail bytes.
- Murmur sanitizes thumbnail bytes, stores assets, persists metadata, and
  broadcasts `ChatEmbedState`.
- If no usable assist result arrives, Murmur falls back to its own bounded
  public HTTPS fetch path when preview fetching is enabled.

Safety rules remain server-owned:

- block localhost and private-network preview targets
- re-check blocked-address policy after redirects
- cap response body, redirect count, content type, and thumbnail size
- avoid direct third-party thumbnail loads when a server-stored thumbnail is
  available

## Client Model

The Modern shell is the primary chat UI. It presents:

- room and text-channel navigation
- persistent timeline rendering
- server log as a Modern timeline scope
- replies, deletion, reactions, and unread/read state
- media cards and inline attachment rendering
- direct-message tray with private and persistent-history modes
- history warmup for active rooms/direct-message conversations
- room-aware composer and attachment controls

The current direction is to keep high-volume updates patch-based and avoid full
snapshot rebuilds on hot paths.

## Retention Direction

Current behavior is simple:

- persistent messages are retained by default
- no automatic expiry is promised yet
- abandoned upload temp files are cleaned up

Future retention work should add:

- preview-cache pruning
- quota-aware asset cleanup
- optional retention windows by scope
- moderation-friendly deletion/audit policy

## Remaining Work

The chat stack is active, but not finished. The next useful improvements are:

1. long-horizon preview-cache retention and quota-aware pruning
2. video poster extraction and document preview derivatives
3. stricter fetch-worker isolation and richer SSRF hardening
4. more complete persistent direct-message UX and test coverage
5. richer moderation/admin tooling for history grants, deletion, and text-room
   management
