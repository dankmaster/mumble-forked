import QtQuick
import QtTest
import Mumble.Theme 1.0
import "qrc:/qml-shell" as Shell

// The redesigned link card (docs/embed-cards-redesign.md): one neutral frame,
// the source named once, a family per kind of content, and a media box whose
// height follows the card's width only.
TestCase {
	id: testCase
	name: "EmbedCard"
	when: windowShown
	visible: true
	width: 900
	height: 1400

	Component {
		id: cardComponent

		Shell.EmbedCard {
			width: 440
		}
	}

	Rectangle {
		id: stage
		anchors.fill: parent
		color: Theme.panel
	}

	function documentFor(presentation, providerId, providerLabel, extra) {
		const document = {
			"schemaVersion": 1,
			"commonPresentation": true,
			"presentation": presentation,
			"provider": { "id": providerId, "label": providerLabel, "host": providerId + ".example" },
			"source": { "url": "https://" + providerId + ".example/item", "displayUrl": "https://" + providerId + ".example/item" },
			"content": { "type": presentation, "title": "", "description": "" },
			"media": [],
			"facts": [],
			"playback": { "mode": "none" }
		}
		for (const key in extra)
			document[key] = extra[key]
		return document
	}
	function image(id) {
		return { "id": "media:" + id, "kind": "image", "url": "image://mumble/card-" + id + "?g=1" }
	}
	function video(id) {
		return { "id": "media:" + id, "kind": "video", "url": "https://media.example/" + id + ".mp4",
			"posterUrl": "image://mumble/poster-" + id + "?g=1", "directPlayable": true }
	}
	function cardFor(document, extraPreview) {
		const preview = { "url": document.source.url, "document": document }
		for (const key in extraPreview)
			preview[key] = extraPreview[key]
		const card = createTemporaryObject(cardComponent, stage, { "preview": preview })
		verify(card !== null)
		return card
	}
	function samples() {
		return {
			"link": documentFor("link", "", "example.com", {
				"content": { "type": "link", "title": "Example Domain",
					"description": "This domain is for use in illustrative examples in documents." },
				"media": [image("link")]
			}),
			"article": documentFor("article", "", "Sveriges Radio", {
				"content": { "type": "article", "title": "A long article title that wraps onto a second line of the card",
					"description": "The article's lead, long enough to take two or three lines of the card's text.",
					"author": { "name": "Reporter Name" } },
				"media": [image("hero")],
				"facts": [{ "key": "section", "label": "Section", "value": "News" }]
			}),
			"video": documentFor("media", "youtube", "YouTube", {
				"content": { "type": "video", "title": "A video title", "author": { "name": "Channel" } },
				"media": [video("clip")],
				"playback": { "mode": "provider", "provider": "youtube", "url": "https://www.youtube.com/embed/x" }
			}),
			"short": documentFor("media", "youtube", "YouTube", {
				"content": { "type": "video", "title": "A short", "author": { "name": "Channel" } },
				"media": [video("short")],
				"playback": { "mode": "provider", "provider": "youtube", "url": "https://www.youtube.com/embed/s" }
			}),
			"audio": documentFor("media", "spotify", "Spotify", {
				"content": { "type": "audio", "title": "Song title", "author": { "name": "Artist" } },
				"media": [image("art")],
				"playback": { "mode": "provider", "provider": "spotify", "url": "https://open.spotify.com/embed/x" }
			}),
			"post": documentFor("social", "x", "X", {
				"content": { "type": "social-post", "title": "X",
					"description": "A post with a few sentences. It wraps over several lines of the card, as posts do.",
					"author": { "name": "Mumble Design", "handle": "@mumbledesign" } },
				"media": [image("p1"), image("p2"), image("p3")],
				"thread": { "kind": "conversation", "items": [{ "role": "reply-context", "authorHandle": "@source",
					"text": "The parent post" }] },
				"facts": [{ "key": "replies", "label": "Replies", "value": 18 },
					{ "key": "likes", "label": "Likes", "value": 624 }]
			}),
			"product": documentFor("game", "steam", "Steam", {
				"content": { "type": "game", "title": "Portal 2",
					"description": "The \"Perpetual Testing Initiative\" has been expanded to allow you to design co-op puzzles." },
				"media": [image("s1"), video("s2"), image("s3")],
				"playback": { "mode": "native", "provider": "steam" },
				"facts": [{ "key": "price", "label": "Price", "value": "9,69€" },
					{ "key": "developer", "label": "Developer", "value": "Valve" },
					{ "key": "reviews", "label": "Reviews", "value": "Overwhelmingly Positive" }]
			}),
			"image": documentFor("media", "imgur", "Imgur", {
				"content": { "type": "image", "title": "Imgur" },
				"media": [image("img")]
			})
		}
	}

	function test_family_data() {
		return [
			{ tag: "link", family: "link" },
			{ tag: "article", family: "article" },
			{ tag: "video", family: "video" },
			{ tag: "audio", family: "audio" },
			{ tag: "post", family: "post" },
			{ tag: "product", family: "product" },
			{ tag: "image", family: "image" }
		]
	}
	function test_family(data) {
		const card = cardFor(samples()[data.tag])
		compare(card.family, data.family)
	}

	function test_frame_is_neutral() {
		const card = cardFor(samples().product)
		compare(String(card.border.color), String(Theme.previewCardBorder))
		compare(String(card.color), String(Theme.previewCardBackground))
	}

	function test_header_names_the_source_once() {
		const card = cardFor(samples().post)
		const header = findChild(card, "embedCardHeader")
		compare(header.text, "X")
		// The document's title is the provider's name; the card does not repeat it.
		compare(card.title, "")
		const video = cardFor(samples().video)
		compare(findChild(video, "embedCardHeader").text, "YouTube  ·  Channel")
	}

	function test_media_box_height_follows_the_width() {
		const card = cardFor(samples().video)
		const box = findChild(card, "embedCardMediaBox")
		compare(box.height, Math.round(card.contentWidth * 9 / 16))
		card.width = 600
		compare(box.height, Math.min(360, Math.round(card.contentWidth * 9 / 16)))
		const portrait = cardFor(samples().short, { "embedAspect": "short" })
		compare(findChild(portrait, "embedCardMediaBox").height,
			Math.min(420, Math.round(portrait.contentWidth * 1.25)))
	}

	function test_card_height_does_not_wait_for_its_picture() {
		const card = cardFor(samples().article)
		const before = card.implicitHeight
		tryVerify(() => findChild(card, "embedCardMediaImage").status === Image.Ready, 2000)
		compare(card.implicitHeight, before)
	}

	function test_post_shows_author_text_reply_and_pictures() {
		const card = cardFor(samples().post)
		const box = findChild(card, "embedCardMediaBox")
		verify(box.visible)
		// Three pictures: one large beside two stacked, in a 16:9 box.
		compare(box.height, Math.round(card.contentWidth * 9 / 16))
		compare(findChild(card, "embedCardFacts").text, "Replies 18  ·  Likes 624")
	}

	function test_play_asks_the_view_and_the_box_takes_the_players_height() {
		const card = cardFor(samples().video)
		const playSpy = createTemporaryObject(spyComponent, testCase, { "target": card, "signalName": "playRequested" })
		const play = findChild(card, "embedCardPlay")
		verify(play.visible)
		mouseClick(findChild(card, "embedCardMediaBox"))
		compare(playSpy.count, 1)
		card.playerHeight = 300
		card.playing = true
		compare(findChild(card, "embedCardMediaBox").height, 300)
		verify(!play.visible)
		compare(String(findChild(card, "embedCardMediaImage").source), "")
	}

	function test_audio_opens_its_player_below_the_art() {
		const card = cardFor(samples().audio)
		const box = findChild(card, "embedCardMediaBox")
		verify(!box.visible)
		card.playerHeight = 152
		card.playing = true
		verify(box.visible)
		compare(box.height, 152)
		// Spotify's player shows the cover, title and artist itself.
		verify(!findChild(card, "embedCardSpotify").visible)
		verify(!findChild(card, "embedCardSpotifyPlay").visible)
	}

	function test_a_native_player_keeps_the_cards_text() {
		const card = cardFor(samples().product)
		card.playerHeight = 260
		card.playing = true
		verify(!card.playerShowsContent)
		verify(findChild(card, "embedCardFacts").visible, "a Steam trailer has no text of its own")
	}

	// A store's pictures sit in a strip under its lead picture: a picture opens the
	// detail sheet at it, a trailer plays in the card, and the lead picture is a
	// picture, not a play button.
	function test_product_strip_plays_trailers_and_opens_pictures() {
		const card = cardFor(samples().product)
		const playSpy = createTemporaryObject(spyComponent, testCase, { "target": card, "signalName": "playRequested" })
		const detailsSpy = createTemporaryObject(spyComponent, testCase, { "target": card, "signalName": "detailsRequested" })
		verify(card.showStrip)
		verify(!card.canPlay)
		verify(!findChild(card, "embedCardPlay").visible)
		tryVerify(() => findChild(card, "embedCardStripItem1") !== null)
		mouseClick(findChild(card, "embedCardStripItem1"))
		compare(playSpy.count, 1)
		compare(playSpy.signalArguments[0][0], 1)
		mouseClick(findChild(card, "embedCardMediaBox"))
		compare(detailsSpy.count, 1)
		compare(detailsSpy.signalArguments[0][0], 0)
	}

	function test_audio_card_says_what_it_is_and_who_made_it() {
		const album = documentFor("media", "spotify", "Spotify", {
			"source": { "url": "https://open.spotify.com/album/4aawyAB9vmqN3uQ7FjRGTy" },
			"content": { "type": "audio", "title": "Bobo & Gileus" },
			"media": [image("cover")],
			"playback": { "mode": "provider", "provider": "spotify", "url": "https://open.spotify.com/embed/album/x" }
		})
		const card = cardFor(album)
		compare(card.family, "audio")
		compare(card.audioKind, "Album")
		compare(findChild(card, "embedCardHeader").text, "Spotify")
		// Spotify looks like its own embed: its panel and round play button.
		verify(card.spotifyStyle)
		verify(findChild(card, "embedCardSpotify").visible)
		verify(!findChild(card, "embedCardAudio").visible)
		const spotifyPlaySpy = createTemporaryObject(spyComponent, testCase, { "target": card, "signalName": "playRequested" })
		mouseClick(findChild(card, "embedCardSpotifyPlay"))
		compare(spotifyPlaySpy.count, 1)
		const track = documentFor("media", "soundcloud", "SoundCloud", {
			"source": { "url": "https://soundcloud.com/forss/flickermood" },
			"content": { "type": "audio", "title": "Flickermood", "author": { "name": "Forss" } },
			"media": [image("wave")],
			"playback": { "mode": "provider", "provider": "soundcloud", "url": "https://w.soundcloud.com/player/?url=x" }
		})
		const soundCloud = cardFor(track)
		compare(soundCloud.family, "audio")
		compare(soundCloud.audioKind, "Track")
		compare(soundCloud.byline, "Forss")
		verify(!soundCloud.spotifyStyle)
		verify(findChild(soundCloud, "embedCardAudioPlay").visible)
	}

	function test_header_falls_back_to_the_providers_own_line() {
		const reddit = documentFor("media", "reddit", "Reddit", {
			"content": { "type": "video", "title": "A post", "subtitle": "u/someone · r/pics" },
			"media": [video("clip")],
			"playback": { "mode": "native", "provider": "reddit" }
		})
		compare(findChild(cardFor(reddit), "embedCardHeader").text, "Reddit  ·  u/someone · r/pics")
	}

	function test_a_video_without_a_picture_gets_a_short_placeholder() {
		const document = documentFor("media", "direct", "4chan", {
			"content": { "type": "video", "title": "1785247201381165.mp4" },
			"media": [{ "id": "media:0", "kind": "video", "url": "https://media.example/clip.mp4", "directPlayable": true }],
			"playback": { "mode": "native", "provider": "direct" }
		})
		const card = cardFor(document)
		compare(card.family, "video")
		compare(card.title, "", "the file name is not a title")
		verify(findChild(card, "embedCardMediaBox").height <= 200)
		verify(findChild(card, "embedCardNoPicture").visible)
		verify(findChild(card, "embedCardPlay").visible)
	}

	function test_a_link_to_a_picture_is_a_picture_card() {
		const document = documentFor("link", "", "upload.wikimedia.org", {
			"source": { "url": "https://upload.wikimedia.org/wikipedia/commons/3/3f/JPEG_example_flower.jpg" },
			"content": { "type": "link", "title": "JPEG_example_flower.jpg" }
		})
		const card = cardFor(document, { "thumbnailUrl": "image://mumble/flower?g=1" })
		compare(card.family, "image")
		verify(findChild(card, "embedCardMediaBox").visible)
		compare(card.title, "", "the file name is not a title")
	}

	function test_product_leads_its_facts_with_the_price() {
		const card = cardFor(samples().product)
		compare(findChild(card, "embedCardFacts").text, "9,69€  ·  Developer Valve  ·  Reviews Overwhelmingly Positive")
	}

	function test_only_managed_pictures_are_drawn() {
		const document = samples().link
		document.media = [{ "id": "media:0", "kind": "image", "url": "https://tracker.example/pixel.png" }]
		const card = cardFor(document)
		compare(card.imageSource, "")
	}

	// With MUMBLE_EMBED_CARD_GALLERY_DIR set, saves one picture per family.
	function test_gallery() {
		if (String(embedCardGalleryDirectory || "").length === 0)
			skip("MUMBLE_EMBED_CARD_GALLERY_DIR is not set")
		const all = samples()
		for (const tag in all) {
			const preview = { "url": all[tag].source.url, "document": all[tag] }
			if (tag === "short")
				preview.embedAspect = "short"
			const card = cardComponent.createObject(stage, { "preview": preview, "x": 20, "y": 20 })
			verify(card !== null)
			wait(300)
			const picture = grabImage(card)
			picture.save(embedCardGalleryDirectory + "/card-" + tag + ".png")
			card.destroy()
			wait(0)
		}
	}

	Component {
		id: spyComponent
		SignalSpy {}
	}
}
