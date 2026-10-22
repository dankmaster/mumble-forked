import QtQuick
import QtTest
import "qrc:/qml-shell" as Shell

// The detail sheet behind a link card: browse every picture, read everything,
// and play a chosen video in the card.
TestCase {
	id: testCase
	name: "EmbedDetailSheet"
	when: windowShown
	visible: true
	width: 1000
	height: 900

	Component {
		id: sheetComponent
		Shell.EmbedDetailSheet {}
	}
	Component {
		id: cardComponent
		Shell.EmbedCard {
			width: 440
		}
	}
	Component {
		id: spyComponent
		SignalSpy {}
	}

	function storeDocument() {
		return {
			"schemaVersion": 1,
			"presentation": "game",
			"provider": { "id": "steam", "label": "Steam" },
			"source": { "url": "https://store.example/app/1" },
			"content": { "type": "game", "title": "Portal 2", "description": "A puzzle game." },
			"media": [
				{ "id": "media:0", "kind": "image", "url": "image://mumble/store-0?g=1" },
				{ "id": "media:1", "kind": "video", "url": "https://media.example/trailer.mp4",
					"posterUrl": "image://mumble/store-1?g=1", "directPlayable": true },
				{ "id": "media:2", "kind": "image", "url": "image://mumble/store-2?g=1" }
			],
			"facts": [{ "key": "price", "label": "Price", "value": "9,69€" },
				{ "key": "developer", "label": "Developer", "value": "Valve" }],
			"playback": { "mode": "native", "provider": "steam" }
		}
	}
	function postDocument() {
		return {
			"schemaVersion": 1,
			"presentation": "social",
			"provider": { "id": "x", "label": "X" },
			"source": { "url": "https://x.example/post/1" },
			"content": { "type": "social-post", "title": "X",
				"description": "A long post. ".repeat(120),
				"author": { "name": "Mumble Design", "handle": "@mumbledesign" } },
			"media": [{ "id": "media:0", "kind": "image", "url": "image://mumble/post-0?g=1" },
				{ "id": "media:1", "kind": "image", "url": "image://mumble/post-1?g=1" }],
			"thread": { "kind": "conversation", "items": [{ "role": "quote", "authorHandle": "@quoted",
				"text": "The quoted post" }] },
			"facts": [],
			"playback": { "mode": "none" }
		}
	}
	function openSheet(document, index) {
		const sheet = createTemporaryObject(sheetComponent, testCase)
		verify(sheet !== null)
		sheet.show({ "url": document.source.url, "document": document }, "42", index || 0)
		tryCompare(sheet, "opened", true)
		// The sheet takes its content's height once that is laid out.
		tryVerify(() => sheet.height > 200, 2000, "sheet laid out: " + sheet.height)
		return sheet
	}

	function test_browses_every_picture() {
		const sheet = openSheet(storeDocument(), 0)
		const counter = findChild(sheet.contentItem, "embedDetailCounter")
		compare(counter.text, "1 / 3")
		mouseClick(findChild(sheet.contentItem, "embedDetailNext"))
		compare(counter.text, "2 / 3")
		mouseClick(findChild(sheet.contentItem, "embedDetailPrevious"))
		mouseClick(findChild(sheet.contentItem, "embedDetailPrevious"))
		compare(counter.text, "3 / 3", "previous wraps to the last picture")
		keyClick(Qt.Key_Right)
		compare(counter.text, "1 / 3", "the arrow keys step too")
		verify(findChild(sheet.contentItem, "embedDetailStrip").visible)
	}

	function test_opens_at_the_chosen_picture() {
		const sheet = openSheet(storeDocument(), 2)
		compare(findChild(sheet.contentItem, "embedDetailCounter").text, "3 / 3")
	}

	function test_a_chosen_video_plays_in_the_card() {
		const sheet = openSheet(storeDocument(), 1)
		const playSpy = createTemporaryObject(spyComponent, testCase, { "target": sheet, "signalName": "playRequested" })
		verify(findChild(sheet.contentItem, "embedDetailPlay").visible)
		mouseClick(findChild(sheet.contentItem, "embedDetailViewer"))
		compare(playSpy.count, 1)
		compare(playSpy.signalArguments[0][0], "42")
		compare(playSpy.signalArguments[0][1], 1)
		tryCompare(sheet, "visible", false)
	}

	function test_shows_every_fact_and_the_whole_text() {
		const sheet = openSheet(storeDocument(), 0)
		verify(findChild(sheet.contentItem, "embedDetailFacts").visible)
		const post = openSheet(postDocument(), 0)
		const description = findChild(post.contentItem, "embedDetailDescription")
		verify(description.visible)
		verify(description.lineCount > 6, "the whole post: " + description.lineCount + " lines")
	}

	// With MUMBLE_EMBED_CARD_GALLERY_DIR set, saves a picture of the sheet.
	function test_gallery() {
		if (String(embedCardGalleryDirectory || "").length === 0)
			skip("MUMBLE_EMBED_CARD_GALLERY_DIR is not set")
		const store = openSheet(storeDocument(), 0)
		wait(300)
		grabImage(store.contentItem).save(embedCardGalleryDirectory + "/sheet-store.png")
		store.close()
		const post = openSheet(postDocument(), 1)
		wait(300)
		grabImage(post.contentItem).save(embedCardGalleryDirectory + "/sheet-post.png")
	}

	function test_card_opens_details_when_it_has_more() {
		const store = createTemporaryObject(cardComponent, testCase,
			{ "preview": { "url": "https://store.example/app/1", "document": storeDocument() } })
		const detailsSpy = createTemporaryObject(spyComponent, testCase,
			{ "target": store, "signalName": "detailsRequested" })
		verify(store.hasDetails)
		mouseClick(findChild(store, "embedCardPictureCount"))
		compare(detailsSpy.count, 1)
		compare(detailsSpy.signalArguments[0][0], 0)

		const link = createTemporaryObject(cardComponent, testCase, { "y": 500, "preview": {
			"url": "https://example.com/",
			"document": { "schemaVersion": 1, "presentation": "link", "provider": { "id": "", "label": "example.com" },
				"source": { "url": "https://example.com/" }, "content": { "type": "link", "title": "Example" },
				"media": [], "facts": [], "playback": { "mode": "none" } } } })
		const openSpy = createTemporaryObject(spyComponent, testCase, { "target": link, "signalName": "openRequested" })
		verify(!link.hasDetails)
		mouseClick(link, 20, link.height - 10)
		compare(openSpy.count, 1, "a plain link opens the link")
	}
}
