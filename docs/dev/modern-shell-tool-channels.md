# Modern shell tool channels

The Qt Quick client can place real persistent text channels in the `TOOLS`
section for development and production-server testing. Set
`MUMBLE_MODERN_TOOL_TEXT_CHANNELS` before starting the client:

```powershell
$env:MUMBLE_MODERN_TOOL_TEXT_CHANNELS = '#TestStuff'
```

Names are matched case-insensitively and may include the leading `#`. A stable
channel ID can be used as `id:42`. Separate multiple selectors with commas or
semicolons:

```powershell
$env:MUMBLE_MODERN_TOOL_TEXT_CHANNELS = '#TestStuff;id:42'
```

This changes only the channel's navigation placement and debug labeling. The
channel remains a normal server-owned persistent text channel, including its
history, unread state, permissions, and composer behavior. Restart the client
after changing the environment variable.

## Activity server log

`Activity` is the server's actual Murmur log, not a client-side approximation.
When both endpoints negotiate `server_log_stream`, Murmur sends a bounded recent
backlog followed by live entries using the exact text passed to `Server::log`.
The client renders the text literally and keeps the surface read-only.

The client keeps this authoritative stream separate from its local session-log
buffer. That local buffer remains available only to the legacy, non-persistent
chat fallback; client notices can therefore never be mislabeled or leaked into
Activity, while older servers keep their existing ephemeral conversation view.

Access is controlled by the root-only `Use tools` ACL permission. The server
checks the effective root permission before sending any backlog or live entry,
stops delivery immediately after revocation, and sends a reset so the client
drops entries it is no longer allowed to retain. Old clients and old servers do
not receive or expose the stream. `Use tools` remains default-deny for ordinary
users; root administrators and SuperUser retain access through the existing
root permission rules.

The initial backlog is capped at 200 entries and 512 KiB, individual entries at
32 KiB, and the client retains at most 2,000 entries per connection. These limits
keep a noisy server from growing the client timeline without bound.
