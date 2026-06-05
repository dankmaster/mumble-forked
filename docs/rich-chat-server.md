# Rich Chat Server Setup

This document covers the server-side requirements for persistent rich chat media in Murmur.

## Current Scope

This build supports:

- persistent structured chat bodies for channel, text-channel, optional
  server-global, and private/direct-message scopes
- authenticated asset upload initialization, chunked upload, commit, and ranged download over the existing Mumble control channel
- server-side asset metadata persistence in the database
- server-side object storage on local disk
- image upload normalization for raster formats, including EXIF-orientation application and metadata stripping by re-encoding
- server-generated bitmap preview derivatives for uploaded images
- server-authoritative URL preview metadata and bitmap thumbnails, with one
  capable client allowed to assist before Murmur falls back to its own public
  HTTPS fetch path
- direct-message history when `ChatFeatureDirectMessages` is advertised and
  both participants have registered user identities
- actor avatar hashes for richer Modern chat presentation
- per-message attachment count limits
- MIME allowlisting and asset-size / storage-quota enforcement
- background cleanup of abandoned temporary upload files

This build does not yet support:

- video transcoding / poster extraction
- document preview rendering
- long-horizon preview-cache retention and quota-aware asset pruning

## Required Server Changes

1. Deploy a Murmur binary that includes the rich-chat schema and transfer handlers.
2. Start the server once so schema migrations create the new chat asset tables.
3. Ensure the Murmur service account can create and write files in the chat asset storage root.
4. Configure conservative upload and quota limits before exposing the feature broadly.
5. Back up both the database and the asset storage directory together.
6. If enabling URL previews, ensure capable clients can fetch public HTTPS
   metadata and the Murmur host has outbound HTTPS access for fallback.

## Configuration Keys

These keys can be set globally in `mumble-server.ini` or per virtual server through the existing Murmur configuration database path.

- `persistentglobalchat`
  Enables persisted server-global chat when set to `true`.
- `chat_asset_storage_path`
  Optional filesystem path for chat asset objects. If unset, Murmur uses `<config-dir>/chat-assets`.
- `chat_asset_max_bytes`
  Maximum accepted size for one uploaded asset.
- `chat_asset_total_quota_bytes`
  Total on-disk quota for stored chat assets per virtual server. Set `0` for unlimited.
- `chat_attachment_limit`
  Maximum number of attachment asset references accepted on one persistent chat message.
- `chat_preview_fetch_enabled`
  Master switch for automatic URL embeds. When enabled, client-assisted
  previews are attempted first by default and Murmur falls back to server-side
  HTTPS fetching if no usable client result arrives.
- `chat_preview_client_assist_enabled`
  Allows Murmur to lease one pending URL preview to one capable client while
  keeping all persistence and `ChatEmbedState` broadcasts server-authoritative.
  Set to `false` to restore the server-only preview fetch path.
- `chat_preview_client_assist_lease_ms`
  Maximum lifetime of one client-assist lease.
- `chat_preview_client_assist_fallback_ms`
  Delay before Murmur starts its own server-side fetch while the assisted embed
  is still pending.
- `chat_preview_client_assist_thumbnail_max_bytes`
  Maximum encoded thumbnail payload accepted from an assisting client before
  Murmur sanitizes and stores it as preview-cache media.

## Recommended Baseline

For an initial rollout:

```ini
persistentglobalchat=false
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

These values mean:

- 25 MiB max per asset
- 2 GiB per-server stored asset quota
- 4 attachments per message
- URL previews stay off until explicitly enabled; when enabled, client assist is
  the default first attempt and Murmur remains authoritative

## Storage Layout

Murmur stores files under the configured root in a per-server directory:

```text
chat-assets/
  server-1/
    incoming/
    objects/
      ab/
        cd/
          abcdef123456-<sha256>.bin
```

- `incoming/` contains temporary upload files while a client is still sending chunks.
- `objects/` contains committed assets addressed by SHA-256-derived paths.
- preview thumbnails are stored in the same object tree as normal assets and are marked as `PreviewCache` in the database.

## Network / Firewall Notes

- Asset traffic uses the existing authenticated Mumble TCP connection.
- No extra HTTP endpoint is required for this phase.
- Reverse proxies do not need special routing for asset download in this phase.
- URL previews are client-assisted first when `chat_preview_fetch_enabled=true`
  and `chat_preview_client_assist_enabled=true`; Murmur still performs bounded
  outbound HTTPS fetches for fallback and remains the only process that persists
  preview metadata/assets.

## Operational Notes

- If you prune database rows, prune the asset object store in the same maintenance window.
- If you restore from backup, restore both the database and the `chat-assets` directory from the same point in time.
- If the asset root lives on a separate volume, keep it on fast local storage; chat attachments are read directly from disk during client download.
- The current cleanup timer only removes abandoned files from `incoming/`; it does not yet reclaim long-lived preview-cache assets automatically.

## MIME Policy In This Phase

Accepted upload MIME classes are intentionally narrow:

- Raster images: `image/png`, `image/jpeg`, `image/webp`, `image/gif`, `image/bmp`
- Inline playable link previews: direct `.gif` and `.webm` HTTPS links are
  cached as preview-cache chat assets when preview fetching is enabled. Modern
  UI renders GIFs inline and WebM with video controls; WebM does not autoplay by
  default.
- Videos: `video/mp4`, `video/webm`, `video/quicktime`
- Documents: `application/pdf`, `text/plain`, `text/markdown`
- Binary downloads: `application/octet-stream`, `application/zip`

Not accepted inline:

- SVG
- HTML
- arbitrary rich document formats

## Upgrade / Rollout Checklist

1. Build and deploy the updated `mumble-server`.
2. Confirm the service account can write to the configured `chat_asset_storage_path`.
3. If you want URL previews, set `chat_preview_fetch_enabled=true`. Keep
   `chat_preview_client_assist_enabled=true` for the default hybrid path, or set
   it to `false` for server-only fetching.
4. Restart Murmur and confirm startup completes without schema errors.
5. Send a small attachment from a compatible client and verify:
   - the asset row exists in `chat_assets`
   - the object file exists on disk
   - the message row references the asset through `chat_message_attachments`
6. Send a message with a public HTTPS URL and verify:
   - a row appears in `chat_message_embeds`
   - the embed status transitions to `Ready`, `Blocked`, or `Failed`
   - a preview thumbnail asset is created when the remote page exposes an image
7. Verify backups include both the SQL database and the asset directory.

## Follow-Up Work

The next server-side milestones after this phase are:

1. video/document derivative generation
2. stricter fetch-worker isolation and richer SSRF hardening
3. quota-aware cleanup and retention policies for preview-cache assets
4. richer client presentation for attachments, inline media, and transfer progress
