import QtQuick
import QtQuick.Controls
import QtTest

TestCase {
    id: testCase
    name: "RichChatComponents"
    when: windowShown
    visible: true
    width: 720
    height: 620

    Loader {
        id: bodyLoader
        width: parent.width
        Component.onCompleted: setSource("qrc:/qml-shell/RichMessageBody.qml", {
            "segments": [
                { "text": "<img src=x onerror=alert(1)>" },
                { "text": " bad", "href": "javascript:alert(2)" },
                { "text": " safe", "href": "https://example.com/path?q=1", "bold": true }
            ]
        })
    }

    Loader {
        id: previewLoader
        anchors.top: bodyLoader.bottom
        anchors.topMargin: 12
        width: parent.width
        height: 180
        visible: true
        Component.onCompleted: setSource("qrc:/qml-shell/RichPreviewCard.qml", {
            "preview": {
                "state": "loading",
                "loading": true,
                "title": "Fetching preview",
                "url": "https://example.com/card"
            }
        })
    }

    Loader {
        id: attachmentLoader
        anchors.top: previewLoader.bottom
        anchors.topMargin: 12
        width: parent.width
        height: 180
        visible: true
        Component.onCompleted: setSource("qrc:/qml-shell/AttachmentGallery.qml", {
            "attachments": [{
                "id": "asset:1",
                "kind": "image",
                "url": "image://mumble/fixture-attachment?g=1",
                "thumbnailUrl": "image://mumble/fixture-attachment?g=1",
                "alt": "Test attachment"
            }]
        })
    }

    SignalSpy {
        id: attachmentSpy
        target: attachmentLoader.item
        signalName: "attachmentRequested"
    }

	SignalSpy {
		id: linkSpy
		target: bodyLoader.item
		signalName: "linkRequested"
	}

    SignalSpy {
        id: directMediaSpy
        target: previewLoader.item
        signalName: "directMediaRequested"
    }

    SignalSpy {
        id: externalOpenSpy
        target: previewLoader.item
        signalName: "externalOpenRequested"
    }

    SignalSpy {
        id: imageOpenSpy
        target: previewLoader.item
        signalName: "imageOpenRequested"
    }

    function init() {
        tryVerify(function() {
            return bodyLoader.item !== null && previewLoader.item !== null && attachmentLoader.item !== null
        })
        attachmentSpy.clear()
        directMediaSpy.clear()
        externalOpenSpy.clear()
        imageOpenSpy.clear()
		linkSpy.clear()
        previewLoader.item.preview = {
            "state": "loading",
            "loading": true,
            "title": "Fetching preview",
            "url": "https://example.com/card"
        }
        previewLoader.item.previewIdentity = "fixture:loading"
        previewLoader.item.watchTogetherAvailable = true
		previewLoader.item.renderActive = true
        previewLoader.item.resetForReuse()
    }

    function test_sender_content_is_escaped_and_links_are_allowlisted() {
        const body = bodyLoader.item
        verify(body.renderedHtml.indexOf("&lt;img") >= 0)
        verify(body.renderedHtml.indexOf("<img") < 0)
        verify(body.renderedHtml.indexOf("javascript:") < 0)
        verify(body.renderedHtml.indexOf("https://example.com/path?q=1") >= 0)
        compare(body.safeExternalUrl("file:///secret"), "")
        compare(body.safeExternalUrl("javascript:alert(1)"), "")
        compare(body.safeExternalUrl("https://example.com"), "https://example.com")
        const renderedText = findChild(body, "richMessageBodyText")
        verify(renderedText !== null)
        compare(renderedText.textFormat, Text.RichText)
		compare(body.keyboardLinks.length, 1)
		body.forceActiveFocus()
		tryCompare(body, "activeFocus", true)
		keyClick(Qt.Key_Return)
		compare(linkSpy.count, 1)
		compare(linkSpy.signalArguments[0][0], "https://example.com/path?q=1")
    }

    function test_preview_loading_error_and_expansion_states() {
        const card = previewLoader.item
        compare(card.previewState, "loading")
        card.preview = {
            "state": "error",
            "failed": true,
            "title": "Preview unavailable",
            "description": "Provider request failed",
            "url": "https://example.com/card"
        }
        compare(card.previewState, "error")
        verify(card.hasDetails)
        card.preview = {
            "state": "ready",
            "title": "Ready preview",
            "description": "Structured metadata",
            "url": "https://example.com/card",
            "previewSize": "compact"
        }
        verify(card.compact)
        card.userExpanded = true
        verify(card.expanded)
        verify(!card.compact)
    }

    function test_preview_identity_and_delegate_reuse_reset_transient_state() {
        const card = previewLoader.item
        card.preview = {
            "state": "ready",
            "title": "Gallery",
            "description": "Structured metadata",
            "previewSize": "compact",
            "mediaItems": [
                { "kind": "image", "mime": "image/png", "url": "image://mumble/one?g=1" },
                { "kind": "image", "mime": "image/png", "url": "image://mumble/two?g=1" }
            ]
        }
        card.previewIdentity = "message:1|gallery"
        card.userExpanded = true
        card.selectedMediaIndex = 1
        verify(card.expanded)
        compare(card.selectedMediaIndex, 1)
		card.preview = {
			"state": "ready",
			"title": "Hydrated gallery",
			"description": "Updated metadata for the same preview",
			"previewSize": "compact",
			"mediaItems": [
				{ "kind": "image", "mime": "image/png", "url": "image://mumble/one?g=2" },
				{ "kind": "image", "mime": "image/png", "url": "image://mumble/two?g=2" }
			]
		}
		compare(card.selectedMediaIndex, 1)
        card.previewIdentity = "message:2|gallery"
        verify(!card.userExpanded)
        compare(card.selectedMediaIndex, 0)
        card.userExpanded = true
        card.selectedMediaIndex = 1
        card.resetForReuse()
        verify(!card.userExpanded)
        compare(card.selectedMediaIndex, 0)
    }

    function test_preview_images_never_render_remote_or_data_urls_directly() {
        const card = previewLoader.item
        compare(card.safeRenderImageSource("https://cdn.example.com/image.png"), "")
        compare(card.safeRenderImageSource("data:image/png;base64,AAAA"), "")
        compare(card.safeRenderImageSource("image://other/image"), "")
        compare(card.safeRenderImageSource("image://mumble/image?g=1"), "image://mumble/image?g=1")

        card.preview = {
            "state": "ready",
            "title": "Remote image",
            "mediaKind": "image",
            "mediaMime": "image/png",
            "mediaItems": [{
                "kind": "image",
                "mime": "image/png",
                "url": "https://cdn.example.com/image.png"
            }]
        }
        compare(card.currentMediaUrl, "")
        compare(card.imageSource, "")
        verify(card.hasExternalImage)
        card.requestCurrentMedia()
        compare(externalOpenSpy.count, 1)
        compare(externalOpenSpy.signalArguments[0][0], "https://cdn.example.com/image.png")
        compare(imageOpenSpy.count, 0)

        card.preview = {
            "state": "ready",
            "title": "Pipeline image",
            "mediaKind": "image",
            "mediaMime": "image/png",
            "mediaItems": [{
                "kind": "image",
                "mime": "image/png",
                "url": "image://mumble/image?g=2",
                "externalUrl": "https://cdn.example.com/image.png"
            }]
        }
        compare(card.currentMediaUrl, "image://mumble/image?g=2")
        compare(card.imageSource, "image://mumble/image?g=2")
        verify(!card.hasExternalImage)
        card.requestCurrentMedia()
        compare(imageOpenSpy.count, 1)
        compare(imageOpenSpy.signalArguments[0][0], "image://mumble/image?g=2")
    }

    function test_direct_media_gallery_is_bounded_and_typed() {
        const card = previewLoader.item
        const items = []
        for (let index = 0; index < 24; ++index) {
            items.push({
                "kind": index === 0 ? "video" : "image",
                "mime": index === 0 ? "video/mp4" : "image/jpeg",
                "url": index === 0 ? "data:video/mp4;base64,AAAA"
                                     : "https://cdn.example.com/image-" + index + ".jpg"
            })
        }
        card.preview = {
            "state": "ready",
            "title": "Direct gallery",
            "mediaUrl": "data:video/mp4;base64,AAAA",
            "mediaMime": "video/mp4",
            "mediaKind": "video",
            "mediaAudioUrl": "data:audio/mp4;base64,AAAA",
            "mediaAudioMime": "audio/mp4",
            "mediaItems": items
        }
        card.previewIdentity = "message:3|direct"
        compare(card.mediaItems.length, 16)
        compare(card.currentMediaKind, "video")
        verify(card.hasDirectMedia)
        card.requestCurrentMedia()
        compare(directMediaSpy.count, 1)
        compare(directMediaSpy.signalArguments[0][0], "data:video/mp4;base64,AAAA")
        compare(directMediaSpy.signalArguments[0][1], "video/mp4")
        compare(directMediaSpy.signalArguments[0][2], "data:audio/mp4;base64,AAAA")
        compare(directMediaSpy.signalArguments[0][3], "audio/mp4")
    }

    function test_watch_together_is_disabled_while_session_active() {
        const card = previewLoader.item
        card.preview = {
            "state": "ready",
            "title": "Video",
            "embedUrl": "https://www.youtube.com/embed/test",
            "embedKind": "youtube"
        }
        card.previewIdentity = "message:4|youtube"
        const watchButton = findChild(card, "previewWatchTogetherButton")
        verify(watchButton !== null)
        card.watchTogetherAvailable = false
        verify(watchButton.visible)
        verify(!watchButton.enabled)
        compare(watchButton.text, "Session active")
        card.watchTogetherAvailable = true
        verify(watchButton.enabled)
    }

	function test_managed_animation_unloads_when_preview_is_inactive() {
		const card = previewLoader.item
		const managedGif = "file:///C:/Temp/mumble-qml-images-a1/"
			+ "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
			+ "-12345678-1234-1234-1234-123456789abc.gif"
		card.preview = {
			"state": "ready",
			"title": "Managed animation",
			"previewSize": "compact",
			"mediaItems": [{
				"kind": "image", "mime": "image/gif", "url": managedGif,
				"managedAnimated": true
			}]
		}
		card.previewIdentity = "message:animated|managed"
		card.renderActive = true
		card.userExpanded = true
		const animation = findChild(card, "previewExpandedAnimatedImage")
		verify(animation !== null)
		tryCompare(animation, "requestedSource", managedGif)
		card.renderActive = false
		compare(animation.requestedSource, "")
		compare(animation.source.toString(), "")
		verify(!animation.playing)
		card.renderActive = true
	}

    function test_attachment_is_keyboard_and_pointer_actionable() {
        const action = findChild(attachmentLoader.item, "attachmentAction_asset:1")
        verify(action !== null)
        const tile = findChild(attachmentLoader.item, "attachment_asset:1")
        verify(tile !== null)
        verify(attachmentLoader.visible,
               "attachment loader was hidden at y=" + attachmentLoader.y + " height=" + attachmentLoader.height)
        verify(attachmentLoader.item.visible,
               "attachment gallery was hidden with " + attachmentLoader.item.attachments.length + " attachments")
        verify(tile.visible,
               "attachment tile was hidden at " + tile.x + "," + tile.y + " size=" + tile.width + "x" + tile.height)
        verify(action.visible, "attachment action must be visible")
        verify(action.enabled, "attachment action must be enabled")
        verify(action.width > 0 && action.height > 0,
               "attachment action geometry was " + action.width + "x" + action.height)
        const sceneCenter = action.mapToItem(null, action.width / 2, action.height / 2)
        verify(sceneCenter.x >= 0 && sceneCenter.x < testCase.width
               && sceneCenter.y >= 0 && sceneCenter.y < testCase.height,
               "attachment action center was outside the test window at " + sceneCenter.x + "," + sceneCenter.y)
        mouseClick(action, action.width / 2, action.height / 2, Qt.LeftButton)
        compare(attachmentSpy.count, 1)
        compare(attachmentSpy.signalArguments[0][0].id, "asset:1")
        action.forceActiveFocus()
        tryCompare(action, "activeFocus", true)
        keyClick(Qt.Key_Space)
        compare(attachmentSpy.count, 2)
        compare(attachmentSpy.signalArguments[1][0].id, "asset:1")
    }
}
