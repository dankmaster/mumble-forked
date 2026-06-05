# Security hardening pass

This branch (`security/hardening-pass`) implements the fixes from a security review of the
fork-specific code. **None of it has been compiled or run** — it was authored without a Qt build
environment, so every change must be built and exercised before merging. Items that could plausibly
affect runtime behaviour are flagged below.

## P0 — Client RCE / XSS (modern WebEngine shell)

### 1. Sanitize direct-message HTML before it reaches the shell
Direct messages were rendered with `innerHTML` in the WebEngine shell using the raw, sender-controlled
`message.message()` field, allowing `<img src=x onerror=...>`-style script execution in a Chromium
context that exposes a powerful native bridge.

- `src/mumble/MainWindow.cpp` — `appendModernDirectMessage()` (live DM path, also covers
  `Messages.cpp` `msgTextMessage`) now routes the body through `persistentChatContentHtml()`
  (the same `Log::validHtml` sanitizer the channel chat already uses).
- `src/mumble/MainWindow.cpp` — persistent DM builder now sanitizes the raw `message.message()`
  branch the same way; the already-escaped `body_text` branch is left untouched.

### 2. Content-Security-Policy on the shell pages
The shell HTML had no CSP. Added a `<meta http-equiv="Content-Security-Policy">` to
`index.html`, `dialog.html`, `popup.html`:

```
default-src 'self' qrc:; script-src 'self' qrc: https://www.youtube.com; style-src 'self' qrc: 'unsafe-inline'; img-src 'self' qrc: https: data: blob:; media-src 'self' qrc: https: blob:; font-src 'self' qrc: data:; frame-src https:; connect-src 'self' qrc:; object-src 'none'; base-uri 'none'; form-action 'none'
```

`script-src` is intentionally strict (no `'unsafe-inline'`/`'unsafe-eval'`) — this blocks inline event
handlers, inline `<script>`, `javascript:` URIs and `eval`, neutralizing the XSS execution path even if
unsanitized HTML reaches the DOM. The main shell allows the fixed YouTube iframe API script because the
custom controls load it explicitly; `connect-src` remains local/qrc-only so arbitrary HTML in the
renderer cannot perform direct network fetches. Finance lookups use the native bridge, and
playlist-based inline HLS falls back to "Open in browser" unless a future native allowlisted media-fetch
bridge is added. Passive image/media/frame loads remain available for previews/embeds. **Must be
validated by running the shell** — a wrong CSP yields a blank page. If the shell fails to load, loosen
the specific directive that breaks.

### 3. Network egress backstop
`src/mumble/ModernShellHost.cpp` — `PreviewMediaUrlInterceptor` never blocked anything. Added a block
for exfiltration-prone request types (`ResourceTypeXhr`, `ResourceTypePing`, `ResourceTypeCspReport`,
`ResourceTypeWebSocket`) from the qrc shell document to non-allowlisted hosts. Passive sub-resource
types, and requests initiated by approved iframe players, are left alone so embeds keep loading their
own assets/API/CDN/WebSocket dependencies. The `connect-src` CSP directive (item 2) is the primary
control; this is defense-in-depth.

Provider login / bot-verification flows are handled through an explicit user action in the Modern shell.
The session window only opens allowlisted media-provider URLs and shares the Modern WebEngine profile so
approved embeds can benefit from user-solved login/captcha challenges. It does not attempt to solve or
bypass provider challenges in background fetches.

## P1 — Screen-share / helper process

### 4 + 5. Reject server-trusted `file://` relays and require TLS
`src/ScreenShare.cpp` — `normalizeRelayUrl()` now rejects `file://` (a server-supplied file relay let
the helper write the screen recording to a server-chosen path) and rejects plaintext `ws://`/`http://`/
`rtmp://`, accepting only `wss`/`https`/`rtmps`.

> ⚠️ **Behaviour change / verify:** if your private relay legitimately uses a plaintext scheme on a
> trusted LAN, this will stop it. Relax the scheme allowlist if that is intended. The local-file
> recording feature is also removed as a *server-driven* option; reintroduce it as a client-configured
> directory if needed.

### 6. Restrict the helper IPC socket to the current user
`src/screen-helper/ScreenShareHelperServer.cpp` — `QLocalServer::setSocketOptions(UserAccessOption)`
before `listen()`, so the named pipe / socket DACL is limited to the current user account.

> **Deferred:** per-process peer authentication (a nonce/token between the Mumble client and the
> helper) is *not* implemented. It is low severity (a same-user process can already capture the screen
> directly) and a correct token scheme needs care around an already-running helper from a prior session.
> Suggested design: client generates a token, writes it to a user-private file in the runtime dir, helper
> reads it at startup and requires a matching `token` field on every request.

### 7. Reject relay URLs with gst-launch metacharacters
`src/ScreenShare.cpp` — `normalizeRelayUrl()` rejects URLs containing whitespace, `!`, `\` or control
characters, which have special meaning when the URL is interpolated into a gst-launch pipeline token.

## P2 — Server hardening (against malicious clients)

### 8. Rate-limit expensive, low-frequency handlers
`src/murmur/Messages.cpp` — added `RATELIMIT(uSource)` to `msgChatAssetUploadInit`,
`msgChatAssetUploadCommit` (full-file SHA-256 + image re-encode) and `msgStonksAction`.
`msgChatSend` and `msgChatReactionToggle` already had it.

> **Residual:** `msgChatAssetUploadChunk` is intentionally *not* per-chunk rate-limited (it would stall
> large legitimate uploads). Chunk volume is bounded by the declared byte size + server quota, but a
> client could still send many tiny chunks. Consider a minimum non-final chunk size or a per-connection
> chunk-rate cap.

### 9. Message size limit — verified, no change
`msgChatSend` already enforces `isTextAllowed()` → `PERM_DENIED_TYPE(TextTooLong)`. No change needed.

## P3 — Code quality

- **11.** `src/ScreenShare.cpp` — `webRtcRelayCodecPreferenceList()` now delegates to
  `defaultCodecPreferenceList()` (removed duplicate literal).
- **12.** `src/screen-helper/ScreenShareHelperServer.cpp` — IPC read consumes only the framed request
  line instead of clearing the whole buffer.
- **13.** `src/mumble/WebFetch.cpp` — fixed a garbled comment.
- **10. (deferred):** breaking up the ~40k-line `MainWindow.cpp` into the existing `Modern*Controller`
  pattern was **not** attempted. A mechanical split without a compiler would very likely leave the tree
  non-building. This should be done incrementally with build feedback.

## What was reviewed and found OK (no change)

Server-side authorization held up: DM history is keyed by the requester's own user id (no IDOR), history
grants require `ChanACL::Write`, asset download is gated by `canAccessChatAsset`, asset storage is
content-addressed (no path traversal) with image re-encoding, and screen-share signaling is gated on
same-channel co-presence. SQL is parameterized via `soci::use`.

## Build / verify checklist before merge

1. Build the client and `mumble-server`.
2. Launch the modern shell — confirm it renders (CSP not too strict) and previews/embeds still load.
3. Send a DM containing `<img src=x onerror=alert(1)>` from another client — confirm no script runs and
   the text renders inert.
4. Exercise screen-share publish/view over a `wss`/`rtmps` relay.
5. Confirm screen-share still works after the `UserAccessOption` socket change on Windows.
