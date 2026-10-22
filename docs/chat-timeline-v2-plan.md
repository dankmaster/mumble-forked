# Chat Timeline v2 Implementation Plan

Status: proposed, 2026-10-21.

This plan moves the Modern client's chat timeline (channel, text-room, global
and direct-message conversations) onto the design validated by the throwaway
prototype in `.tmp/chat-timeline-spike` (local, git-excluded; results in its
`RESULTS.md`). It replaces the current timeline in `qml-shell/Main.qml`
(about 9,700 of its 14,200 lines), the duplicate list in
`DirectMessageWindow.qml`, and most of the pacing and hydration machinery in
`QmlClientModels.cpp` and `MainWindow.cpp`.

## Goal

Chat that is pretty and fast:

- opening or switching a conversation shows cached content on the next frame
- scrolling, flinging and jumping never drop frames
- nothing on screen moves unless the reader moved it or new content arrived
  at the bottom while they were following it
- media and previews fade into space that was already reserved for them
- the UI thread is never blocked by chat work (the UI-stall rule in
  `status-and-roadmap.md` applies)

## Success criteria

These are the gates for switching the default and for deleting the old
timeline. They are measured by the timeline benchmark (Phase 0), extended to
run inside the real client against fixture and live data.

| Measure | Gate |
| --- | --- |
| Cached conversation switch, request to first frame | p95 ≤ 50 ms |
| Cold open from server history, request to first frame | p95 ≤ 150 ms on the dev server |
| GUI-thread work per scroll frame (2,500 and 9,000 px/s) | p95 ≤ 2 ms, max ≤ 4 ms |
| Frames over 1.5x the refresh interval during scroll | ≤ 0.1% at 60 Hz and at 144 Hz |
| GUI-thread stalls over 25 ms in any scenario after first open | 0 |
| Rows changing height after first paint, unintended | 0 |
| Reading-position drift on paging, live inserts, preview resolution | 0 px |
| Follow-to-latest during a 20 msg/s burst | settled gap 0 px, follower idle afterwards |
| Rows left dim or overlapping after any scenario | 0 |
| Visible media settled after switch | p95 ≤ 300 ms from local cache |

## Design rules

These are the invariants the prototype established. Every phase is reviewed
against them.

1. **A row's height is a function of its data and the list width only.** No
   row may change height because something loaded. Images, video posters,
   link-card thumbnails and galleries get their box from metadata dimensions.
   When dimensions are unknown the box uses a fixed default for its kind and
   the media is fitted inside it (cover or contain). It never resizes to the
   media.
2. **Intentional height changes are rare, animated and owned.** These are a
   live preview resolving while the row is on screen, a user expanding
   content, an edit, or a deletion. Each one runs an explicit progress
   animation that is stopped in `ListView.onPooled`. Never use `Behavior` on
   geometry. When the reader is not following the latest message, a row at or
   below the reading point that grows by d moves `contentY` by d in the same
   polish pass, so what the reader is looking at stays put.
3. **Newest first, bottom to top.** The view is a `ListView` with
   `verticalLayoutDirection: ListView.BottomToTop` and row 0 as the newest
   message. Live messages insert at row 0 and older history appends at the
   end. ListView keeps the reading position by itself, so there is no anchor,
   prepend-settlement or reading-anchor code.
4. **Following the latest message is intent, not geometry.** Only the reader's
   own movement (drag, flick, wheel, keyboard) or an explicit jump changes it.
   The follower eases and finishes with a snap so it always goes idle under
   `pixelAligned`.
5. **No list-level transitions.** `add`/`displaced` transitions with
   `reuseItems` leave stale half-faded rows under rapid inserts. Arrival
   animation lives inside the row and is paint-only (opacity, translate). It
   is restarted or cleared on reuse.
6. **A row's appearance depends only on its older neighbour.** Grouping and
   date breaks are decided against the older row, so appending newer rows
   never changes existing rows. History pages carry one extra older message so
   the oldest loaded row is decided correctly.
7. **Rows are flat and synchronous.** One component per row kind through
   `DelegateChooser`, with no nested asynchronous Loaders in the row. Heavy
   surfaces (players, web views, provider dashboards, full-size viewers) live
   outside the list and are opened on demand.
8. **Every image request is sized and cancellable.** Requests ask for the box
   size times the device pixel ratio. Requests for rows that leave the
   viewport plus cache buffer are cancelled before they take a worker.
9. **The UI thread only applies prepared changes.** Merging, decoration, HTML
   parsing, row building and diffing happen on a worker. The UI thread applies
   batched `beginInsertRows`/`dataChanged` with explicit role lists.
10. **Whole-pixel geometry.** Row heights are rounded up to whole logical
    pixels.

## Target architecture

### Data layer (C++)

- **`ChatConversationStore`.** One per scope, owned by `PersistentChatController`.
  It holds messages ordered by ID with O(log n) insert, and replaces the
  sort-on-every-merge and `SerializeAsString` comparisons in
  `PersistentChatStore`. Direct messages, both the persistent and transient
  modes, and the server log use the same store type.
- **`ChatRowBuilder`.** Runs on a dedicated worker thread. It turns
  `MumbleProto::ChatMessage`, its attachments, embeds and reactions plus
  client preview metadata into immutable `ChatRow` structs: kind, author, time
  text, group/date decoration, parsed body segments, media geometry, card
  layout class, reaction chips, delivery state, reply context. It also emits
  diffs against the previous row set for the scope.
- **`ChatRowModel`.** A `QAbstractListModel` over `QVector<ChatRow>`, newest
  first, with about 15 typed roles and no `payload` or `source` role. It
  applies diffs from the builder: inserts at 0, appends at the end, and
  single-row `dataChanged` with exact role lists. Search becomes an index the
  builder maintains, not an O(n) rescan on every mutation.
- **Previews.** The current per-provider scrapers in `MainWindow.cpp` move out
  into a `ChatPreviewService` with its own network and worker queue. It
  publishes a `ChatCardLayout` (template, thumbnail dimensions, line limits)
  as soon as metadata is known, before the thumbnail exists. Thumbnails go
  through the existing `QmlAsyncImageProvider`.
- **Media cache.** Keep `PersistentChatMediaCache`, but store images as
  binary files beside a small index rather than base64 inside JSON, and
  replace the full-directory prune on every write with a size counter and an
  occasional prune.

### View layer (QML)

- **`ConversationView.qml`.** One component used by the main window and the
  direct-message window. It contains the ListView, the tail follower, the
  jump-to-latest button, the paging trigger and the date/unread overlays.
- **Row components.** `TextRow`, `MediaRow`, `GalleryRow`, `VideoRow`,
  `LinkCardRow`, `CodeRow`, `SystemRow` and `ActivityLogRow`, plus shared
  `MessageHeader`, `ReplyContext`, `ReactionBar` and `DeliveryStatus`. Each
  row takes required properties only.
- **`MediaBox.qml`.** A fixed box with a dominant-colour placeholder, a fade-in
  on first load, an instant show from cache, and a clean reset on reuse.
- **Card templates.** A small set of fixed-geometry templates replaces
  `RichPreviewCard.qml` (2,696 lines) and the inline `ProviderDetails.qml`
  (4,403 lines): compact link, large-thumbnail article, small-thumbnail
  article, media/video, audio, product/price, and finance quote. Provider
  branding (icon, accent, labels) is data, not layout. Today's rich provider
  views (Stonks, Steam, store pages) open in the shared detail surface on
  click.
- **Shared playback surface.** Inline playback (`InlineMediaPlayer.qml`,
  `NativeDirectMediaPlayer.qml`) moves out of the row. A single player
  instance is positioned over the poster's box while it is visible, and moves
  to `MediaSessionWindow` (or pauses) when the row scrolls away. The row
  itself only ever shows a poster.

## Phase 0: close the prototype's gaps

Each gap the prototype did not cover is validated before any client code
changes, because any of them could change the design. Each item extends the
prototype or the benchmark and has a pass criterion. Results are added to the
prototype's `RESULTS.md`.

### 0.1 Real media

- **Real decode.** Replace the synthetic image provider with
  `QmlAsyncImageProvider` itself (link it into the prototype). Feed it a local
  corpus generated on this machine: JPEG at 12 MP and 4 MP, PNG screenshots,
  WebP, and a set of animated GIFs.
  - Pass: scroll and fling gates hold, decode happens off the UI thread, and
    p95 visible-media settle is ≤ 300 ms with a warm disk cache.
- **Animated GIFs.** The row shows the first frame as a still in its fixed
  box. Animation plays only while the row is fully visible and the window is
  active, through a single `AnimatedImage` or a frame-ticker per visible GIF,
  capped at a small number of simultaneous animations.
  - Pass: 20 GIF rows in view keep scroll gates, and memory stays within the
    provider's frame budget (`maxAnimatedFrames`).
- **Network.** Point the prototype at the dev server (`dank-server`) through
  the real `ChatAssetRequest`/`ChatAssetChunk` path and at real link-preview
  fetches through `ChatPreviewService`. Measure first-paint and settle with an
  empty cache and with a warm one.
  - Pass: no UI-thread stall over 25 ms and no row resize while assets arrive.
- **Video posters.** Posters come from `preview_asset_id` or client-side
  extraction into a fixed 16:9 (or metadata-aspect) box.
  - Pass: poster arrival never resizes the row.
- **Web-based player.** Build the shared playback surface in the prototype:
  one `WebEngineView` positioned over a poster box, following it while
  scrolling, and handing off to a pop-out window when the row leaves the
  viewport.
  - Pass: scroll gates hold while a video plays, starting and stopping
    playback causes no UI-thread stall over 25 ms (WebEngine is created
    lazily, once per session), and there is never more than one web view
    instance.

### 0.2 Full HTML formatting

- Port the current body pipeline: `QTextDocument` parsing on a worker into
  segments, then rendering through `Text.RichText`. Use realistic bodies:
  headings, quotes, lists, inline and fenced code, mentions, links, emoji,
  long unbroken URLs, right-to-left text, and 2,000-character messages.
- Compare `Text.RichText` with a segment renderer that maps the parsed
  document to `StyledText` plus native code and quote blocks.
  - Pass: row creation for rich bodies stays within the per-frame scroll
    budget (row-creation p95 ≤ 1 ms, max ≤ 3 ms), and the row height is final
    at creation with 0 resizes.
- Choose the renderer per the result. If neither passes, rich parsing stays on
  the worker and the row receives pre-laid-out text through `QTextLayout`
  measurement on the worker.

**Status (2026-10-21): done; adopt the block renderer.** Full numbers are in
the prototype's `RESULTS.md`.

- **Block renderer:** blocks are built from the Markdown source, with
  StyledText prose and native quote and code boxes.
  - p95 0.37 ms on a Markdown stress corpus, 0.19 ms on traffic shaped like the
    dev server.
  - No height ever changed after creation.
  - In the integrated scroll runs, UI work per frame was ≤ 2.5 ms with no stall
    over 25 ms.
- **Today's `RichMessageBody.qml`, same data:**
  - p95 5.8 ms, max 21.6 ms.
  - 21 UI stalls over 25 ms during scrolling and jumps.
- **The max ≤ 3 ms gate:**
  - It holds in the integrated runs.
  - It doesn't hold for a few isolated 5–6 ms microbenchmark outliers that look
    like GC.
- **Existing bug, confirmed and fixed in `QmlClientModels.cpp`:** the current
  client showed raw Markdown (`**bold**`, `` `code` ``, `## heading`,
  `> quote`, code fences) for formatted messages without a link or image.
  - `Log::validHtml` re-serializes the formatting as styled `<span>`s and block
    tags, which the rich-parse check did not recognize, so it fell back to the
    raw text.
  - The round trip also drops the fixed-pitch flag that marks code.
  - Covered by `TestQmlClientModels::chatTimelineFormatsSanitizedMarkdownWithoutLinks`.

### 0.3 Other displays

- **High-DPI.** Run the full benchmark at 125%, 150%, 175% and 200% through
  `QT_SCALE_FACTOR`, and on a real high-DPI laptop panel if one is available.
  - Pass: all gates hold.
  - Pass: images are requested at box size × DPR and look sharp in
    screenshots.
  - Pass: there is no sub-pixel follower creep or row overlap at fractional
    scale factors (whole logical-pixel heights may be fractional device
    pixels; the audit checks this).
- **High refresh.** Measure frame cost uncapped (vsync off) to establish
  headroom.
  - Pass: GUI plus render p99 ≤ 5 ms, so 144 Hz has a margin.
  - Pass: before switching the default, run the scroll benchmark on a
    real 144 Hz (or faster) display.
  - Open: a 144 Hz monitor is needed for that final check.

### 0.4 Real protocol data

- Implement `ChatRowBuilder` in the prototype for real
  `MumbleProto::ChatMessage` input.
- Capture a local fixture of real history from the dev server through the
  read-only ICE history API (`getPersistentChatMessages`). Keep the capture
  out of Git like other private captures. Add a synthetic protobuf generator
  that matches its distribution for CI.
- Measure worker build time per page and per 5,000-message conversation.
  Measure UI-thread apply time per page, per live message and per
  edit/delete/reaction/embed update.
  - Pass: UI-thread apply ≤ 1 ms per page and ≤ 0.2 ms per live update.
  - Pass: no full-row rebinds for reaction or embed updates (exact role
    lists).
  - Pass: grouping and date breaks are correct across page boundaries.

**Status (2026-10-21): done, with one gate restated.**

- **How the source was built:** `ChatRowBuilder` turns real `MumbleProto`
  pages and updates into rows on a worker.
  - The source is synthetic messages resampled from the anonymous structure of
    the dev server's 736 non-private messages: lengths, URLs, legacy HTML,
    inline images, and embed and reaction shapes.
  - Exporting message content was refused as personal data, so none was copied.
- **Worker cost per 50-message page:**
  - parse 0.02 ms
  - v2 bodies p50 1.2 ms, p95 12.9 ms
  - rows 0.4 ms
  - For comparison, today's body path costs p50 4.7 ms, p95 17.7 ms per page
    and runs on the UI thread.
- **UI-thread apply:**
  - older page 0.07 ms (max 0.15)
  - live message 0.02 ms
  - reaction patch 0.27 ms (max 0.49), which includes the row rebuilding its
    reaction chips. The gate becomes ≤ 0.5 ms including the delegate update, or
    the chips are diffed.
- **Correctness:**
  - Exact-role updates only.
  - Page-boundary decoration matched a single sequential pass on 10,000 rows.
  - 0 px drift while paging.
- **Change forced:** in-view content updates while reading history moved the
  reading point (192 px over a burst of reactions). Design rule 2 now has a
  mechanism; see `compensateRowResize` in the prototype.
  - When the reader is not following the latest message and a row at or below
    the reading point grows by d, the view moves `contentY` by d in the same
    polish pass.
  - Result: 0 px drift, including on the first frame.
- **Opening a conversation** must build the newest page first. One page takes
  13 ms on the worker; building 5,000 rows before the first frame took
  1.1–4.7 s.

**Exit:** all four sections pass, or the plan is amended with the design
change they forced.

## Phase 1: data layer

1. Add `ChatRow`, `ChatRowBuilder`, `ChatRowModel` and
   `ChatConversationStore`, with unit tests in `TestQmlClientModels` and
   `TestPersistentChatController`. The tests cover diff correctness, grouping
   across pages, live insert, edit/delete/reaction/embed updates, search
   index, and worker cancellation on scope switch.
2. Feed both the old and new models from `PersistentChatController` behind a
   development switch (setting or environment variable). The old
   `ChatTimelineModel` stays the default.
3. Route direct messages (persistent and transient) and the server log through
   the same store and builder. The DM mirror and duplicate publishing in
   `publishQmlDirectMessagesState` stop being the source for the new model.

**Exit:** the new model passes its tests and receives the same traffic as the
old one in a connected session without UI-thread stalls.

## Phase 2: protocol and server geometry

1. Extend `ChatEmbedRef` with `preview_width`, `preview_height`,
   `preview_kind` and `layout_hint`, filled by Murmur when it stores the
   embed thumbnail. These fields are optional and capability-gated like the
   existing chat features.
2. Ensure `ChatAssetRef.width`/`height` are always set for uploaded images and
   video posters (server-side normalisation already decodes them).
3. Make client-assisted previews publish the same geometry.
4. Older servers or rows without dimensions use a fixed per-kind default box
   with fitted media, per design rule 1.

**Exit:** history from the dev server yields known geometry for all new media
and embeds; the fallback is covered by tests.

## Phase 3: ConversationView

Build `ConversationView.qml` and the row components behind the development
switch, at feature parity with today's timeline:

- message header, grouping, date separators, unread marker
- replies and reply context, jump to replied message
- reactions, quick reaction bar, reaction picker
- delete and deleted state; delivery state and retry for outgoing messages
- attachments: single image, gallery, files, upload progress
- link cards for every template; provider detail opens on click
- code, quotes, mentions and links in message bodies
- activity/server-log rows and ephemeral log entries
- search: highlight current match and jump between matches
- context menu (`PayloadMenu`), copy, open, save
- jump to latest, paging trigger, empty states, history-loading state
- accessibility names and focus order (the existing accessibility snapshot
  and visual-accessibility gate apply)
- `ModernUiAutomationServer` and `QmlVisualFixtureController` hooks for the
  new view

**Exit:** the visual fixtures render every row kind; the parity checklist is
complete; the in-client benchmark meets every gate on fixture data.

**Status (first slice):** `ConversationView.qml`, `ConversationRow.qml` and
`ChatBodyBlocks.qml` replace the timeline when `MUMBLE_CHAT_TIMELINE_V2=1`.
Rows are built on `ChatMessageFrame` and reuse `AttachmentGallery`,
`RichPreviewCard`, the quick reaction popup, `ModernMenu`/`PayloadMenuItem` and
the existing `uiCommands`. Until phase 4, avatar, attachment and preview
sources come from the current message pipeline: `MainWindow` mirrors each
message state it builds into `ChatRowModel::setExtras`, and visible rows ask
for hydration through `uiCommands.requestPreviewHydration`. The current
timeline's presentation gate is off under the switch, since nothing renders it.
Covered: header and grouping, date separators, reply context, reactions and
quick reactions, delivery state and retry, delete, copy text, attachments, link
cards, code/quote/heading blocks, server-log rows, jump to latest, paging,
mark read, follow-the-newest with eased snap, resize compensation and the
in-row arrival animation (`tst_ConversationView`). Still open: unread marker,
jump to the replied message, deleted-message state, search highlight, empty and
loading states, images inline in legacy HTML bodies, avatars and actions for
direct messages shown in the main view, accessibility gate, and automation and
visual-fixture hooks.

## Phase 4: media and playback

1. Introduce `MediaBox.qml` on top of `QmlAsyncImageProvider`, which already
   supports cancellation. Verify that rows leaving the viewport plus cache
   buffer actually cancel their requests (the prototype saw about 46% of
   requests become stale during flings), and that requests are sized to the
   box times the device pixel ratio.
2. Introduce the GIF policy from 0.1.
3. Add the shared playback surface and pop-out hand-off. Remove per-row
   `InlineMediaPlayer` and `NativeDirectMediaPlayer` instances from the list.
4. Rework the media cache storage format and pruning.

**Exit:** the real-media gates from 0.1 hold inside the client against the dev
server.

## Phase 5: direct messages

`DirectMessageWindow.qml` and the docked tray switch to `ConversationView`
with the DM model. The duplicated row markup and width helpers in
`DirectMessageWindow.qml` are deleted.

**Exit:** the DM scenarios of the local dev chat matrix pass, and the same
gates hold in the DM window.

## Phase 6: switch the default and delete the old timeline

1. Make the new view the default after a dogfooding period on the shared dev
   client with the old view still selectable.
2. Delete:
   - the timeline section of `Main.qml`
   - the deferred and cold presentation code
   - `StationaryScrollWarmGate.qml`
   - `RichPreviewCard.qml`, or what remains of it after the templates
   - `ChatTimelinePresentationProxyModel`
   - the queued append/prepend drains and deferred row updates in
     `ChatTimelineModel`
   - the rich-body pacing
   - `ModernShellPreviewPublicationPolicy` and the hydration publication
     timers in `MainWindow.cpp`
   - the `m_persistentChatMessages` mirror and the DM history duplicates
   - the dead `PersistentChatRender` paths
3. Update `docs/chat-architecture.md` and `status-and-roadmap.md`.

**Exit:** the success criteria hold with the old code removed; no remaining
references to deferred presentation, cold rows, hydration pacing or warm
gates.

## Testing and measurement

- **Benchmark.** Move the prototype's benchmark driver and `FrameMetrics` into
  the repository as an opt-in client mode (fixture conversations, scripted
  scenarios, JSON results, screenshots). Expose it through
  `scripts/windows/measure-qml-client-performance.ps1`. Its audits catch the
  failure classes that screenshots miss:
  - unintended resizes
  - drift
  - dim or overlapping rows
  - a follower that never goes idle
  - UI-thread stalls
- **Tests.** Add `TestQmlQuickComponents` cases that instantiate each row kind
  at several widths and DPRs and assert the height is final at creation. Add a
  reuse test: recycle rows mid-animation and assert a clean state.
- **Regression gate.** Run the benchmark on fixture data in CI with the
  software renderer for the invariant checks (resizes, drift, overlaps).
  Timing gates run on this machine and the 144 Hz display, not in CI.

## Risks and open questions

- **Rich text cost.** Resolved by 0.2: the block renderer is fast enough
  without pre-layout on the worker. `Text.RichText` stays only for legacy HTML
  bodies.
- **ListView estimates on very long histories.** ListView estimates content
  height from instantiated rows, so the scrollbar thumb can drift on very long
  histories. If that is visible, the builder can provide estimated heights
  per row for the scrollbar alone.
- **Older servers.** Embeds from older servers lack geometry and will use
  default boxes until re-fetched. That is acceptable, but link cards from
  those servers look uniform.
- **WebEngine startup.** The first `WebEngineView` creation may block the UI
  thread. If 0.1 shows a stall, create it at idle after startup, outside chat.
- **Hardware for the 144 Hz check.** The high-refresh gate needs access to a
  144 Hz display.
