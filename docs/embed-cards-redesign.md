# Embed Cards Redesign (proposal)

Status: proposal, 2026-10-22. Builds on the "Card templates" and "Shared
playback surface" items in `chat-timeline-v2-plan.md` and replaces their
sketch with a concrete design.

## Why

The v2 timeline made scrolling predictable: rows get their height from the
row model, an anchor holds the reading position, and pooled rows drop their
cards. The link cards have not caught up. They are the last part of a row
whose height is not known when the row is built, and the most expensive part
to create and to rebind.

What dogfooding shows today:

- **Two render paths.** `EmbedDocument` (typed: YouTube, Steam, X, Instagram,
  TikTok, Reddit, Spotify, articles, …) feeds `EmbedDocumentBody` and
  `EmbedMediaGallery`. Everything else goes through `ProviderDetails.qml`
  (4,400 lines of per-provider templates). The two look different, and a
  provider looks different depending on which path its preview takes. Until
  2026-10-22 the v2 rows showed every card through the legacy path, because
  their previews lacked the typed document.
- **Identity repeated.** The provider name appeared in the header, in an
  initials tile, in a kicker and again in the title. The first polish pass
  removed most of it; the templates still carry the structure for it.
- **Frames in frames.** Templates drew their own panels inside the card frame.
  Also removed in the first pass, but each template still decides its own
  insets.
- **Height is discovered, not declared.** `ConversationRow` reserves a
  remembered or typical height, then adopts the card's `implicitHeight` once
  it loads. A card that grows (inline playback, a portrait video, a loaded
  description) moves the rows around it, and the view must hold the anchor
  against it.
- **Playback lives in the row.** The player is created inside the card. A
  pooled or scrolled-away row loses it, and a card with its own picture can
  show the poster and the player at once (the Steam "double clip").
- **Cost.** A card is 3-4 ms to create and about as much to rebind, which is
  why pooled rows drop their card.

## Principles

1. **One card, one frame, one identity.** A card is a single frame with a
   one-line header (favicon or provider mark, site name, overflow menu). The
   provider is named once.
2. **Height is data.** The row model gives each card its full height before
   any QML exists, from the card family, the column width and the media
   geometry. QML lays out inside that box and never reports a height back.
3. **Static by default, live on intent.** The card in the list is a cheap,
   static picture of the content. Players, galleries, menus and long text load
   only when the reader acts on that card.
4. **Provider is data, not layout.** A provider contributes a label, an
   accent, a mark and fields (author, metrics, price, facts). It does not
   contribute QML.
5. **Content first.** The title or post text leads; the media is shown at the
   aspect it has, within fixed buckets; metadata stays to one quiet line.

## Anatomy

```
┌──────────────────────────────────────────────┐
│ ◉ site name · author / channel           ⋯   │  header: 20 px, one line
│                                              │
│ Title in up to two lines, strong             │  title
│ Description in up to three lines, muted      │  body (optional)
│                                              │
│ ┌──────────────────────────────────────────┐ │
│ │                media box                 │ │  fixed aspect bucket
│ │               (poster / ▶)               │ │
│ └──────────────────────────────────────────┘ │
│ 12k likes · 3 replies · 2 days ago           │  facts: one line
└──────────────────────────────────────────────┘
```

- Width: the message column, capped at 520 px (440 px for compact).
- Media aspect buckets: 16:9, 4:3, 1:1 and 4:5, chosen from the media geometry.
  Portrait video (9:16, Shorts, Reels, TikTok) is shown 4:5 with a blurred
  backdrop, capped at 360 px high, and plays in the same box.
- Text: title 2 lines, body 3 lines, facts 1 line, all elided. "Show more"
  expands the body in place, an explicit reader action that the view anchors.
- Colour: one surface token for every card, an optional 3 px accent on the
  left edge from the provider, no tinted panels inside.

## Families

Seven families cover every provider we fetch today. Each has fixed geometry
for a given width and media geometry.

| Family | Layout | Providers |
| --- | --- | --- |
| Link | header, title, 2-line body, optional 80 px square thumbnail on the right | generic sites, GitHub, docs, Blocket listings without a hero |
| Article | header, hero media (16:9), title, body | news, blogs, Sveriges Radio, Wikipedia |
| Video | header, media box with play, title, facts | YouTube, Vimeo, Dailymotion, Twitch, direct video, TikTok, Reels |
| Audio | header, 1:1 art at 96 px, title, artist, compact transport | Spotify, SoundCloud, direct audio |
| Post | author row (avatar, name, handle), post text up to 6 lines, media grid (1-4), reply/quote context as an inset line, metrics | X, Bluesky, Mastodon, Reddit, Instagram, Facebook, Flashback |
| Product | header, media, title, price and rating line, one primary action | Steam, Amazon, Blocket, stores |
| Image | media only, caption line on hover | Giphy, Tenor, Imgur, direct images |

The richer views that exist today (the Steam purchase panel, Stonks charts,
Flashback threads) move to the shared detail surface that opens on click.

## Geometry contract

- `ChatRowBuilder` already builds a `card` block per row (layout, title, url,
  media size from `ChatEmbedRef.preview_width/height`). It gains
  `family`, `mediaAspect`, `titleLines` and `bodyLines` from `EmbedDocument`,
  and computes `cardHeight` for the row's width class.
- The row reserves `cardHeight` and the card is laid out inside it. Text is
  elided to the declared line counts, so it cannot change the height.
- A height change has exactly three causes: the preview resolves (a known,
  one-time change, applied while the row is off-screen when possible), the
  reader expands the card, or the reader starts portrait playback. All three
  go through the view's anchor lock.
- Server-side geometry (`preview_width/height`) means most cards have their
  final height on first paint, even before the thumbnail arrives.

## Playback

- One player surface for the whole timeline (`InlineMediaPlayer` or the
  native direct player), owned by `ConversationView`, not by a row.
- Pressing play docks the surface over the card's media box and hides the
  card's poster. The card never draws its own picture and a player at once.
- When the row scrolls out of view the player shrinks to a small dock at the
  bottom corner of the conversation (picture-in-picture), and returns to the
  box when the row comes back. Closing the dock stops playback.
- Rows stay cheap while a video plays: the row only shows a poster, so pooling
  and rebinding are unaffected.

## Integration with the v2 timeline

- **Model.** `preview` stays a role, but the row reads `document` (the typed
  embed) only. The legacy fields (`previewProvider`, `metadata`, …) are no
  longer read by QML.
- **Static card.** `EmbedCard.qml`: one component of about 300 lines, built
  from Labels and a `MediaBox`, no Loaders on the scroll path. Creating and
  rebinding target: under 1 ms, so pooled rows can keep their card again.
- **Live parts.** The overflow menu, gallery viewer, "show more" and the
  player load on hover, focus or click.
- **Messages that are only a link.** The URL text is hidden and the card is
  the message. A message with several links shows the first card and a row of
  link chips for the rest.
- **Grouping.** A card in a grouped message aligns with the body text, not
  with the avatar.
- **Accessibility.** The card is one focusable element with a summary
  ("YouTube video: title, channel, 4:12"). Enter opens, Space plays.

## Migration

1. Make the typed document the only input: extend `EmbedDocument` to the
   providers that still use templates (GitHub, Twitch, Vimeo, Dailymotion,
   Stonks, weather, Sveriges Radio, generic). Done when `commonPresentation`
   is true for every preview.
2. Add `family`, line counts and `cardHeight` to the row's `card` block, with
   `TestQmlQuickComponents` cases that create each family at several widths
   and assert the height matches the declared one.
3. Build `EmbedCard.qml` and `MediaBox.qml`, and render the card gallery
   (`docs/media` contact sheets, dark and light) for each family.
4. Move playback to the view-owned surface with the picture-in-picture dock.
5. Switch `ConversationRow` to `EmbedCard`; keep `RichPreviewCard` for the old
   timeline until phase 6 deletes both.

## Open questions

- Should a link-only message hide its URL text? (Proposed: yes.)
- Picture-in-picture dock or pause when a playing row leaves the view?
  (Proposed: dock.)
- Do we keep a left accent edge per provider, or a neutral card everywhere?
