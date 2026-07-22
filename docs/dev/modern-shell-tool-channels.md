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
