import QtQuick
import QtQuick.Controls
import QtTest
import Mumble.Theme 1.0

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
		height: item ? item.implicitHeight : 0
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
		id: attachmentDownloadSpy
		target: attachmentLoader.item
		signalName: "attachmentDownloadRequested"
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

	SignalSpy {
		id: inlinePlaySpy
		target: previewLoader.item
		signalName: "inlinePlayRequested"
	}

	SignalSpy {
		id: popoutPlaySpy
		target: previewLoader.item
		signalName: "popoutPlayRequested"
	}

	SignalSpy {
		id: watchTogetherSpy
		target: previewLoader.item
		signalName: "watchTogetherRequested"
	}

	QtObject {
		id: inlineSession
		property bool active: false
		property bool detached: true
		property string sessionId: ""
		property int detachCalls: 0
		function detach() {
			detachCalls += 1
			detached = true
		}
	}

    function init() {
        tryVerify(function() {
            return bodyLoader.item !== null && previewLoader.item !== null && attachmentLoader.item !== null
        })
        attachmentSpy.clear()
		attachmentDownloadSpy.clear()
        directMediaSpy.clear()
        externalOpenSpy.clear()
        imageOpenSpy.clear()
		inlinePlaySpy.clear()
		popoutPlaySpy.clear()
		watchTogetherSpy.clear()
		linkSpy.clear()
		bodyLoader.item.segments = [
			{ "text": "<img src=x onerror=alert(1)>" },
			{ "text": " bad", "href": "javascript:alert(2)" },
			{ "text": " safe", "href": "https://example.com/path?q=1", "bold": true }
		]
		previewLoader.width = testCase.width
		previewLoader.height = 180
		attachmentLoader.width = testCase.width
        previewLoader.item.preview = {
            "state": "loading",
            "loading": true,
            "title": "Fetching preview",
            "url": "https://example.com/card"
        }
        previewLoader.item.previewIdentity = "fixture:loading"
		previewLoader.item.watchTogetherAvailable = true
		previewLoader.item.renderActive = true
		previewLoader.item.mediaSessionController = null
		previewLoader.item.mediaSessionId = ""
		previewLoader.item.resetForReuse()
		const previewOverflowMenu = findChild(previewLoader.item, "previewOverflowMenu")
		if (previewOverflowMenu && previewOverflowMenu.visible)
			previewOverflowMenu.close()
		inlineSession.active = false
		inlineSession.detached = true
		inlineSession.sessionId = ""
		inlineSession.detachCalls = 0
		attachmentLoader.item.attachments = [{
			"id": "asset:1",
			"kind": "image",
			"url": "image://mumble/fixture-attachment?g=1",
			"thumbnailUrl": "image://mumble/fixture-attachment?g=1",
			"alt": "Test attachment"
		}]
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

	function test_plain_text_fast_path_preserves_rich_formatting() {
		const body = bodyLoader.item
		const renderedText = findChild(body, "richMessageBodyText")
		verify(renderedText !== null)

		body.segments = [{ "text": "Plain <message> & spacing\nnext line" }]
		compare(body.plainTextOnly, true)
		compare(renderedText.textFormat, Text.PlainText)
		compare(renderedText.text, "Plain <message> & spacing\nnext line")

		body.segments = [{ "text": "Bold message", "bold": true }]
		compare(body.plainTextOnly, false)
		compare(renderedText.textFormat, Text.RichText)

		body.segments = [{ "text": "Open link", "href": "https://example.com/path" }]
		compare(body.plainTextOnly, false)
		compare(renderedText.textFormat, Text.RichText)
	}

    function test_preview_loading_error_and_expansion_states() {
        const card = previewLoader.item
        compare(card.previewState, "loading")
        card.preview = {
            "state": "error",
            "failed": true,
            "title": "Preview unavailable",
			"description": "The original provider summary remains ordinary content.",
			"errorDescription": "<b>Provider request failed</b>",
            "url": "https://example.com/card"
        }
        compare(card.previewState, "error")
		compare(card.sanitizedDescription, "The original provider summary remains ordinary content.")
		compare(card.errorDescription, "<b>Provider request failed</b>")
		verify(card.Accessible.description.indexOf("<b>Provider request failed</b>") >= 0)
		verify(card.Accessible.description.indexOf("ordinary content") < 0)
		const errorText = findChild(card, "previewErrorText")
		verify(errorText !== null && errorText.visible)
		compare(errorText.textFormat, Text.PlainText)
		verify(errorText.text.indexOf("<b>Provider request failed</b>") >= 0)
		verify(errorText.text.indexOf("ordinary content") < 0)
		compare(card.canExpand, false)
		card.preview = {
			"state": "error", "title": "Generic failure",
			"description": "This is not an error message.", "url": "https://example.com/card"
		}
		compare(card.errorDescription, "")
		compare(errorText.text, "Preview unavailable. You can still open the original link.")
		verify(card.Accessible.description.indexOf("This is not an error message") < 0)
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

	function test_loading_indicator_uses_the_shared_accent_token() {
		const indicator = findChild(previewLoader.item, "previewCompactBusyIndicator")
		verify(indicator !== null)
		compare(indicator.running, true)
		compare(indicator.indicatorColor, Theme.accent)
		compare(indicator.Accessible.role, Accessible.ProgressBar)
		compare(indicator.Accessible.name, "Loading link preview")
		verify(indicator.segmentCount >= 6)
	}

	function test_more_is_hidden_for_sparse_or_unknown_metadata_and_shown_for_hidden_content() {
		const card = previewLoader.item
		card.preview = {
			"state": "ready", "title": "Sparse", "url": "https://example.com/sparse",
			"previewSize": "compact", "metadata": { "productPrice": "10 kr" }
		}
		card.previewIdentity = "message:sparse"
		compare(card.canExpand, false)
		compare(findChild(card, "previewExpandButton").visible, false)

		card.preview = {
			"state": "ready", "title": "Unknown diagnostics", "url": "https://example.com/unknown",
			"previewSize": "compact", "metadata": { "rawDiagnostic": "not rendered" }
		}
		card.previewIdentity = "message:unknown"
		compare(card.canExpand, false)
		compare(findChild(card, "previewExpandButton").visible, false)

		card.preview = {
			"state": "ready", "title": "Hidden structured details", "url": "https://example.com/details",
			"previewSize": "compact", "metadata": {
				"previewKind": "product", "productPrice": "10 kr", "productAvailability": "In stock",
				"productDelivery": "Tomorrow", "productRating": "4.8", "productBrand": "Example"
			}
		}
		card.previewIdentity = "message:hidden-details"
		compare(card.canExpand, true)
		const overflowButton = findChild(card, "previewOverflowButton")
		const expandAction = findChild(card, "previewExpandButton")
		verify(overflowButton !== null && overflowButton.visible)
		verify(expandAction !== null)
		overflowButton.clicked()
		tryCompare(expandAction, "visible", true)
		findChild(card, "previewOverflowMenu").close()
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
		const animationLoader = findChild(card, "previewExpandedAnimatedLoader")
		verify(animationLoader !== null)
		compare(animationLoader.active, false)
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

	function test_top_level_managed_inline_image_reaches_a_visible_media_surface() {
		const card = previewLoader.item
		card.preview = {
			"state": "ready",
			"kind": "image",
			"title": "Inline image attachment",
			"mediaUrl": "image://mumble/inline-image-fixture?g=1",
			"openLabel": "Open image"
		}
		card.previewIdentity = "message:inline-image-fixture"

		compare(card.mediaItems.length, 1)
		compare(card.currentMediaKind, "image")
		compare(card.currentMediaUrl, "image://mumble/inline-image-fixture?g=1")
		compare(card.imageSource, "image://mumble/inline-image-fixture?g=1")
		const compactImage = findChild(card, "previewCompactImage")
		verify(compactImage !== null)
		tryCompare(compactImage, "status", Image.Ready)
		verify(compactImage.visible)
		compare(compactImage.source.toString(), "image://mumble/inline-image-fixture?g=1")
		compare(findChild(card, "previewExpandedMediaPanel").visible, false)
	}

	function test_expanded_media_is_full_bleed_and_adds_a_distinct_media_stage() {
		const card = previewLoader.item
		card.preview = {
			"state": "ready",
			"kind": "image",
			"title": "Responsive image preview",
			"previewSize": "compact",
			"mediaUrl": "image://mumble/full-bleed-fixture?g=1",
			"mediaMime": "image/png"
		}
		card.previewIdentity = "message:full-bleed-fixture"
		const compactImage = findChild(card, "previewCompactImage")
		const expandedPanel = findChild(card, "previewExpandedMediaPanel")
		const expandedImage = findChild(card, "previewExpandedStaticImage")
		verify(compactImage !== null && expandedPanel !== null && expandedImage !== null)
		tryCompare(compactImage, "status", Image.Ready)
		verify(card.compact)
		verify(!card.expanded)
		verify(!expandedPanel.visible)

		card.userExpanded = true
		tryVerify(function() { return card.expanded && !card.compact && card.implicitHeight > 180 })
		previewLoader.height = Math.ceil(card.implicitHeight)
		wait(0)
		tryCompare(expandedPanel, "visible", true)
		tryCompare(expandedImage, "status", Image.Ready)
		const panelOrigin = expandedPanel.mapToItem(card, 0, 0)
		verify(Math.abs(panelOrigin.x) <= 1,
			"expanded media begins at x=" + panelOrigin.x + " instead of the card edge")
		tryVerify(function() { return Math.abs(expandedPanel.width - card.width) <= 1 }, 5000,
			"expanded media width " + expandedPanel.width + " did not fill card width " + card.width)
		compare(expandedImage.fillMode, Image.PreserveAspectCrop)
		verify(expandedPanel.height > compactImage.height * 3)
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
		const overflowButton = findChild(card, "previewOverflowButton")
		verify(watchButton !== null && overflowButton !== null)
		overflowButton.clicked()
		tryCompare(watchButton, "visible", true)
		card.watchTogetherAvailable = false
		verify(!watchButton.enabled)
		compare(watchButton.text, "Session active")
		card.watchTogetherAvailable = true
		verify(watchButton.enabled)
		findChild(card, "previewOverflowMenu").close()
	}

	function test_provider_embed_offers_inline_and_detached_playback() {
		const card = previewLoader.item
		card.preview = {
			"state": "ready",
			"title": "Video",
			"url": "https://www.youtube.com/watch?v=test",
			"embedUrl": "https://www.youtube.com/embed/test",
			"embedKind": "youtube"
		}
		card.previewIdentity = "message:embed-actions|youtube"
		const inlineButton = findChild(card, "previewPlayButton")
		const posterAction = findChild(card, "previewEmbedPosterAction")
		const cardOpenSurface = findChild(card, "previewCardOpenSurface")
		const popoutButton = findChild(card, "previewPopoutButton")
		const overflowButton = findChild(card, "previewOverflowButton")
		verify(inlineButton !== null && inlineButton.visible)
		verify(posterAction !== null && posterAction.visible && posterAction.enabled)
		verify(cardOpenSurface !== null && !cardOpenSurface.enabled)
		verify(popoutButton !== null && overflowButton !== null)
		compare(inlineButton.text, "Play here")
		compare(inlineButton.posterBacked, false)
		verify(inlineButton.implicitWidth > inlineButton.implicitHeight)
		compare(findChild(card, "previewPlayText").visible, true)
		compare(card.playAccessibilityName, "Play Video here")
		compare(inlineButton.Accessible.name, card.playAccessibilityName)
		overflowButton.clicked()
		tryCompare(popoutButton, "visible", true)
		compare(popoutButton.text, "Open player")
		posterAction.clicked()
		compare(inlinePlaySpy.count, 1)
		compare(inlinePlaySpy.signalArguments[0][0], "https://www.youtube.com/embed/test")
		compare(externalOpenSpy.count, 0)
		popoutButton.triggered()
		compare(popoutPlaySpy.count, 1)
		findChild(card, "previewOverflowMenu").close()
	}

	function test_embed_preview_is_media_first_aspect_aware_and_lazy() {
		const card = previewLoader.item
		const panel = findChild(card, "previewEmbedMediaPanel")
		const poster = findChild(card, "previewEmbedPoster")
		const loader = findChild(card, "previewInlineMediaLoader")
		const playButton = findChild(card, "previewPlayButton")
		verify(panel !== null && poster !== null && loader !== null && playButton !== null)

		const cases = [
			{ "aspect": "", "normalized": "wide", "width": card.actionAvailableWidth,
				"height": card.actionAvailableWidth * 9 / 16 },
			{ "aspect": "short", "normalized": "short", "width": 280, "height": 280 * 16 / 9 },
			{ "aspect": "square", "normalized": "square", "width": 320, "height": 320 },
			{ "aspect": "audio", "normalized": "audio", "width": card.actionAvailableWidth, "height": 352 },
			{ "aspect": "compact-audio", "normalized": "compact-audio",
				"width": card.actionAvailableWidth, "height": 166 }
		]
		for (let index = 0; index < cases.length; ++index) {
			const fixture = cases[index]
			card.preview = {
				"state": "ready", "title": "Provider media", "description": "Visible summary",
				"url": "https://example.com/watch", "embedUrl": "https://www.youtube.com/embed/test",
				"embedKind": "youtube", "embedAspect": fixture.aspect,
				"thumbnailUrl": "image://mumble/embed-poster"
			}
			card.previewIdentity = "message:aspect:" + fixture.normalized
			compare(card.normalizedEmbedAspect, fixture.normalized)
			tryCompare(panel, "visible", true)
			tryVerify(function() { return Math.abs(panel.width - fixture.width) < 1 })
			tryVerify(function() { return Math.abs(panel.height - fixture.height) < 1 })
			tryCompare(poster, "status", Image.Ready)
			compare(poster.fillMode, Image.PreserveAspectCrop)
			verify(playButton.visible)
			tryCompare(playButton, "posterBacked", true)
			compare(playButton.implicitWidth, playButton.implicitHeight)
			verify(findChild(card, "previewPlayIcon").visible)
			compare(findChild(card, "previewPlayText").visible, false)
			compare(findChild(card, "previewProviderChip").visible, false)
			verify(!loader.active)
			compare(loader.item, null)
			compare(findChild(card, "previewCompactImage").visible, false)
		}

		playButton.clicked()
		compare(inlinePlaySpy.count, 1)
		compare(card.restoreInlinePlaybackFocus, true)
		verify(!loader.active)
		compare(loader.item, null)
	}

	function test_loading_embed_reserves_final_media_geometry() {
		const card = previewLoader.item
		const panel = findChild(card, "previewEmbedMediaPanel")
		verify(panel !== null)

		card.preview = {
			"state": "loading", "loading": true,
			"title": "Fetching YouTube preview",
			"url": "https://www.youtube.com/watch?v=test",
			"embedUrl": "https://www.youtube.com/embed/test",
			"embedKind": "youtube", "embedAspect": "wide"
		}
		card.previewIdentity = "message:loading-embed"
		tryCompare(panel, "visible", true)
		tryVerify(function() { return Math.abs(panel.height - card.actionAvailableWidth * 9 / 16) < 1 })
		const loadingMediaHeight = panel.height

		card.preview = {
			"state": "ready", "loading": false,
			"title": "Ready YouTube preview",
			"url": "https://www.youtube.com/watch?v=test",
			"embedUrl": "https://www.youtube.com/embed/test",
			"embedKind": "youtube", "embedAspect": "wide"
		}
		tryCompare(panel, "visible", true)
		compare(panel.height, loadingMediaHeight)
	}

	function test_provider_embed_aspect_fallbacks_preserve_production_shapes() {
		const card = previewLoader.item
		const cases = [
			{ "provider": "tiktok", "metadata": {}, "aspect": "short" },
			{ "provider": "instagram", "metadata": { "instagramMediaKind": "reel" },
			  "aspect": "short" },
			{ "provider": "instagram", "metadata": { "instagramMediaKind": "post" },
			  "aspect": "square" },
			{ "provider": "twitch", "metadata": {}, "aspect": "twitch" },
			{ "provider": "spotify", "metadata": {}, "aspect": "compact-audio" },
			{ "provider": "soundcloud", "metadata": {}, "aspect": "audio" }
		]
		for (let index = 0; index < cases.length; ++index) {
			const fixture = cases[index]
			card.preview = {
				"state": "ready", "title": fixture.provider,
				"embedUrl": "https://example.com/embed/" + fixture.provider,
				"embedKind": fixture.provider, "metadata": fixture.metadata
			}
			card.previewIdentity = "message:aspect-fallback:" + index
			compare(card.normalizedEmbedAspect, fixture.aspect)
		}
	}

	function test_provider_embed_badges_keep_brand_and_twitch_state_visible_before_playback() {
		const card = previewLoader.item
		card.preview = {
			"state": "ready", "title": "Mumble Dev",
			"embedUrl": "https://player.twitch.tv/?channel=mumbledev", "embedKind": "twitch",
			"metadata": { "previewProvider": "twitch", "providerName": "Twitch",
				"twitchLiveState": "live", "twitchBadge": "Live",
				"twitchPlaybackNote": "Twitch may require playback confirmation." }
		}
		card.previewIdentity = "message:twitch-provider-badges"
		const providerBadge = findChild(card, "previewEmbedProviderBadge")
		const stateBadge = findChild(card, "previewEmbedProviderState")
		const details = findChild(card, "providerDetails")
		verify(providerBadge !== null && stateBadge !== null && details !== null)
		tryCompare(providerBadge, "visible", true)
		tryCompare(stateBadge, "visible", true)
		compare(details.providerToken, "twitch")
		compare(details.providerStateLabel, "Live")
		compare(card.normalizedEmbedAspect, "twitch")
		verify(card.embedMediaWidth <= 400)
		verify(Math.abs(card.embedMediaWidth / card.embedMediaHeight - 4 / 3) < 0.01)
		compare(findChild(card, "previewTwitchPosterCopy").visible, true)
		compare(findChild(card, "previewTwitchPosterTitle").text, "Mumble Dev")
		compare(findChild(card, "previewTwitchPosterNote").text,
			"Twitch may require playback confirmation.")
		verify(Math.abs(providerBadge.color.r - details.providerAccent.r) < 0.01)
		verify(Math.abs(providerBadge.color.g - details.providerAccent.g) < 0.01)
		verify(Math.abs(providerBadge.color.b - details.providerAccent.b) < 0.01)
		verify(Math.abs(providerBadge.color.a - 0.92) < 0.01)
		verify(Math.abs(stateBadge.color.r - details.providerStateColor.r) < 0.01)
		verify(Math.abs(stateBadge.color.g - details.providerStateColor.g) < 0.01)
		verify(Math.abs(stateBadge.color.b - details.providerStateColor.b) < 0.01)
		verify(Math.abs(stateBadge.color.a - 0.92) < 0.01)
		compare(findChild(card, "previewProviderChip").visible, false)
	}

	function test_sensitive_embed_keeps_poster_and_web_surface_private_until_reveal() {
		const card = previewLoader.item
		card.preview = {
			"state": "ready", "title": "Sensitive provider media",
			"embedUrl": "https://www.youtube.com/embed/private", "embedKind": "youtube",
			"thumbnailUrl": "image://mumble/private-poster",
			"metadata": { "contentWarning": "Sensitive provider poster", "thumbnailBlur": true }
		}
		card.previewIdentity = "message:sensitive-embed"
		const poster = findChild(card, "previewEmbedPoster")
		const playButton = findChild(card, "previewPlayButton")
		const revealButton = findChild(card, "previewEmbedRevealButton")
		const posterAction = findChild(card, "previewEmbedPosterAction")
		const loader = findChild(card, "previewInlineMediaLoader")
		verify(card.mediaRequiresReveal)
		compare(poster.source.toString(), "")
		verify(!playButton.visible)
		verify(revealButton !== null && revealButton.visible)
		verify(!loader.active)
		verify(posterAction !== null && posterAction.visible && posterAction.enabled)
		posterAction.clicked()
		verify(!card.mediaRequiresReveal)
		tryCompare(poster, "status", Image.Ready)
		compare(poster.source.toString(), "image://mumble/private-poster")
		verify(playButton.visible)
		verify(!loader.active)
		compare(inlinePlaySpy.count, 0)
		compare(externalOpenSpy.count, 0)
	}

	function test_inline_focus_request_is_bound_to_preview_and_session_identity() {
		const card = previewLoader.item
		card.preview = {
			"state": "ready", "title": "Bound focus",
			"embedUrl": "https://www.youtube.com/embed/focus", "embedKind": "youtube"
		}
		card.previewIdentity = "message:focus-owner"
		card.mediaSessionId = "message:focus-owner"
		card.mediaSessionController = inlineSession
		inlineSession.sessionId = "another-message"
		card.requestInlinePlaybackWithFocus()
		verify(card.restoreInlinePlaybackFocus)
		verify(!card.inlineFocusRequestMatchesCurrentSession())

		inlineSession.sessionId = "message:focus-owner"
		verify(card.inlineFocusRequestMatchesCurrentSession())
		card.previewIdentity = "message:reused-delegate"
		verify(!card.restoreInlinePlaybackFocus)
		verify(!card.inlineFocusRequestMatchesCurrentSession())
	}

	function test_preview_reduced_motion_disables_every_card_transition_and_animation() {
		const card = previewLoader.item
		card.preview = {
			"state": "ready", "title": "Reduced motion",
			"embedUrl": "https://www.youtube.com/embed/motion", "embedKind": "youtube"
		}
		card.previewIdentity = "message:reduced-motion"
		card.animationsEnabled = false
		compare(card.animationDuration, 0)
		compare(findChild(card, "previewEmbedBusyIndicator").animated, false)
		card.animationsEnabled = true
		compare(card.animationDuration, Theme.motionFast)
	}

	function test_sensitive_embed_without_poster_still_requires_reveal() {
		const card = previewLoader.item
		card.preview = {
			"state": "ready", "title": "Sensitive provider without poster",
			"embedUrl": "https://www.youtube.com/embed/private-no-poster", "embedKind": "youtube",
			"metadata": { "contentWarning": "Sensitive provider content" }
		}
		card.previewIdentity = "message:sensitive-embed-no-poster"
		const playButton = findChild(card, "previewPlayButton")
		const revealButton = findChild(card, "previewEmbedRevealButton")
		const loader = findChild(card, "previewInlineMediaLoader")
		verify(card.mediaRequiresReveal)
		verify(!playButton.visible)
		verify(revealButton !== null && revealButton.visible)
		verify(!loader.active)
		revealButton.clicked()
		verify(!card.mediaRequiresReveal)
		verify(playButton.visible)
		verify(!loader.active)
	}

	function test_description_and_whole_card_open_are_visible_and_keyboard_accessible() {
		const card = previewLoader.item
		card.preview = {
			"state": "ready", "title": "Release notes",
			"description": "A long provider description that remains visible without expanding the card. "
				+ "It is clamped to keep the timeline compact while still explaining the destination.",
			"url": "https://example.com/release"
		}
		card.previewIdentity = "message:keyboard-open"
		const description = findChild(card, "previewDescription")
		const openSurface = findChild(card, "previewCardOpenSurface")
		const openButton = findChild(card, "previewOpenButton")
		verify(description !== null && description.visible)
		compare(description.maximumLineCount, 3)
		compare(description.elide, Text.ElideRight)
		verify(openSurface !== null && openSurface.visible)
		verify(openButton !== null && openButton.visible)
		compare(openButton.Accessible.role, Accessible.Link)
		compare(openButton.Accessible.name, "Open: Release notes")
		openButton.forceActiveFocus()
		tryCompare(openButton, "activeFocus", true)
		compare(openButton.background.border.width, Theme.focusRingWidth)
		compare(openButton.background.border.color, Theme.focus)
		keyClick(Qt.Key_Return)
		compare(externalOpenSpy.count, 1)
		compare(externalOpenSpy.signalArguments[0][0], "https://example.com/release")

		card.userExpanded = true
		compare(description.maximumLineCount, 12)
		compare(description.elide, Text.ElideNone)
	}

	function test_watch_together_only_appears_for_synchronized_providers() {
		const card = previewLoader.item
		const watchAction = findChild(card, "previewWatchTogetherButton")
		const overflowButton = findChild(card, "previewOverflowButton")
		const overflowMenu = findChild(card, "previewOverflowMenu")
		verify(watchAction !== null && overflowButton !== null && overflowMenu !== null)

		for (const provider of [ "youtube", "twitch", "streamable", "vimeo", "dailymotion", "direct" ]) {
			card.preview = {
				"state": "ready", "title": provider,
				"embedUrl": "https://example.com/embed/" + provider, "embedKind": provider
			}
			card.previewIdentity = "message:sync:" + provider
			verify(card.watchTogetherSupported)
			overflowButton.clicked()
			tryCompare(watchAction, "visible", true)
			overflowMenu.close()
		}

		card.preview = {
			"state": "ready", "title": "TikTok",
			"embedUrl": "https://example.com/embed/tiktok", "embedKind": "tiktok"
		}
		card.previewIdentity = "message:sync:tiktok"
		verify(!card.watchTogetherSupported)
		overflowButton.clicked()
		compare(watchAction.visible, false)
		overflowMenu.close()
	}

	function test_inline_playback_detaches_before_delegate_becomes_inactive() {
		const card = previewLoader.item
		card.renderActive = false
		card.mediaSessionId = "message:inline"
		card.mediaSessionController = inlineSession
		inlineSession.sessionId = "message:inline"
		inlineSession.detached = false
		inlineSession.active = true
		verify(card.inlinePlaybackActive)
		// Entering the render window may schedule the lazy WebEngine component,
		// but leaving it must synchronously preserve playback in the pop-out before
		// a pooled delegate can destroy its inline renderer.
		card.renderActive = true
		card.renderActive = false
		tryCompare(inlineSession, "detachCalls", 1)
		verify(inlineSession.detached)
		compare(card.inlinePlaybackActive, false)
	}

	function test_structured_image_link_has_mouse_keyboard_and_accessible_activation() {
		const body = bodyLoader.item
		body.segments = [{
			"kind": "image",
			"source": "image://mumble/missing-inline-image?g=1",
			"alt": "Embedded release diagram",
			"width": 320,
			"height": 180,
			"href": "https://example.com/release-diagram"
		}]
		tryVerify(function() { return findChild(body, "richMessageImageCard_0") !== null })
		const imageCard = findChild(body, "richMessageImageCard_0")
		const imageState = findChild(body, "richMessageImageState_0")
		verify(imageState !== null)
		compare(imageCard.Accessible.role, Accessible.Link)
		compare(imageCard.Accessible.name, "Embedded release diagram")
		verify(body.plainText.indexOf("Embedded release diagram") >= 0)

		tryVerify(function() { return previewLoader.y >= bodyLoader.y + imageCard.height })
		mouseMove(imageCard, imageCard.width / 2, imageCard.height / 2)
		tryVerify(function() { return imageState.color.a > 0 })
		mousePress(imageCard, imageCard.width / 2, imageCard.height / 2, Qt.LeftButton)
		tryCompare(imageState, "color", Theme.accentSubtle)
		mouseRelease(imageCard, imageCard.width / 2, imageCard.height / 2, Qt.LeftButton)
		compare(linkSpy.count, 1)
		compare(linkSpy.signalArguments[0][0], "https://example.com/release-diagram")

		imageCard.forceActiveFocus()
		tryCompare(imageCard, "activeFocus", true)
		compare(imageState.border.color, Theme.focus)
		compare(imageState.border.width, Theme.focusRingWidth)
		verify(imageCard.Accessible.focused)
		keyClick(Qt.Key_Return)
		compare(linkSpy.count, 2)
		imageCard.Accessible.pressAction()
		compare(linkSpy.count, 3)
	}

	function test_provider_actions_reject_unsafe_embed_and_media_urls() {
		const card = previewLoader.item
		compare(card.safeProviderEmbedUrl("javascript:alert(1)"), "")
		compare(card.safeProviderEmbedUrl("data:text/html,<script>alert(1)</script>"), "")
		compare(card.safeProviderEmbedUrl("https://player.example.com/embed/1"),
			"https://player.example.com/embed/1")
		compare(card.safeDirectMediaUrl("javascript:alert(1)", "video"), "")
		compare(card.safeDirectMediaUrl("data:text/html;base64,AAAA", "video"), "")
		compare(card.safeDirectMediaUrl("data:video/mp4;base64,AAAA", "video"),
			"data:video/mp4;base64,AAAA")

		card.preview = {
			"state": "ready",
			"title": "Unsafe provider payload",
			"embedUrl": "javascript:alert(1)",
			"embedKind": "youtube",
			"mediaItems": [{
				"kind": "video", "mime": "video/mp4", "url": "javascript:alert(2)",
				"externalUrl": "file:///secret"
			}]
		}
		card.previewIdentity = "message:unsafe-provider"
		compare(card.safeEmbedUrl, "")
		compare(card.mediaItems.length, 0)
		compare(findChild(card, "previewPlayButton").visible, false)
		compare(findChild(card, "previewPopoutButton").visible, false)
		compare(findChild(card, "previewWatchTogetherButton").visible, false)
		card.requestCurrentMedia()
		compare(directMediaSpy.count, 0)
		compare(externalOpenSpy.count, 0)
		compare(inlinePlaySpy.count, 0)
		compare(popoutPlaySpy.count, 0)
		compare(watchTogetherSpy.count, 0)

		card.preview = {
			"state": "ready",
			"title": "External provider media",
			"mediaItems": [{
				"kind": "video", "mime": "video/mp4",
				"url": "https://cdn.example.com/video.mp4", "directPlayable": false
			}]
		}
		card.previewIdentity = "message:external-provider"
		compare(card.mediaItems.length, 1)
		verify(card.hasExternalMedia)
		card.requestCurrentMedia()
		compare(externalOpenSpy.count, 1)
		compare(externalOpenSpy.signalArguments[0][0], "https://cdn.example.com/video.mp4")
	}

	function test_preview_actions_stay_single_row_and_card_reports_production_width_caps() {
		const card = previewLoader.item
		card.preview = {
			"state": "ready",
			"title": "A deliberately long provider title that stays readable on compact windows",
			"description": "Additional structured metadata that enables the expand action.",
			"url": "https://example.com/watch",
			"openLabel": "Open this item on the original provider",
			"embedUrl": "https://www.youtube.com/embed/test",
			"embedKind": "youtube"
		}
		card.previewIdentity = "message:responsive|youtube"
		compare(card.targetCardWidth, 580)
		for (const width of [340, 420, 680, 760, 1082]) {
			previewLoader.width = width
			previewLoader.height = 640
			tryCompare(card, "width", width)
			compare(card.narrowLayout, width < 440)
			const flow = findChild(card, "previewActionFlow")
			const primary = findChild(card, "previewOpenButton")
			const overflow = findChild(card, "previewOverflowButton")
			verify(flow !== null)
			verify(primary !== null && primary.visible)
			verify(overflow !== null && overflow.visible)
			tryCompare(flow, "width", card.actionAvailableWidth)
			verify(primary.x >= -0.5 && primary.x + primary.width <= overflow.x + 0.5)
			verify(overflow.x + overflow.width <= flow.width + 0.5)
			verify(Math.abs(primary.y - overflow.y) <= 1)
			verify(primary.Accessible.name.length > 0)
			verify(overflow.Accessible.name.length > 0)
		}

		card.preview = {
			"state": "ready", "title": "Compact", "url": "https://example.com/compact",
			"previewSize": "compact"
		}
		compare(card.targetCardWidth, 460)
		card.preview = {
			"state": "ready", "title": "Large", "url": "https://example.com/large",
			"previewSize": "large"
		}
		compare(card.targetCardWidth, 720)
	}

	function test_short_provider_label_is_not_elided_by_its_chip_padding() {
		const card = previewLoader.item
		card.preview = {
			"state": "ready",
			"title": "Product preview",
			"provider": "Inet",
			"url": "https://example.com/product"
		}
		card.previewIdentity = "message:provider-chip|inet"
		const chip = findChild(card, "previewProviderChip")
		const label = findChild(card, "previewProviderLabel")
		verify(chip !== null && label !== null)
		tryVerify(function() { return chip.width > 0 && label.width > 0 })
		verify(label.width + 0.5 >= label.implicitWidth,
			"provider label width " + label.width + " clipped implicit width " + label.implicitWidth)
	}

	function test_game_store_provider_chip_uses_store_name_instead_of_transport_kind() {
		const card = previewLoader.item
		card.preview = {
			"state": "ready", "title": "Hades II", "url": "https://store.steampowered.com/app/1",
			"metadata": { "previewProvider": "game-store", "previewKind": "gameStoreProduct",
				"gameStoreProvider": "steam", "gameStoreName": "Steam", "gameStorePrice": "29,99 €" }
		}
		card.previewIdentity = "message:game-store-provider"
		compare(card.providerLabel, "Steam")
		compare(findChild(card, "previewProviderLabel").text, "STEAM")
		const details = findChild(card, "providerDetails")
		compare(details.providerToken, "steam")
		compare(details.providerDisplayName, "Steam")
	}

	function test_steam_gallery_keeps_bounded_selectable_thumbnails() {
		const card = previewLoader.item
		card.preview = {
			"state": "ready", "title": "Hades II", "url": "https://store.steampowered.com/app/1",
			"mediaItems": [
				{ "kind": "image", "url": "image://mumble/steam-shot-1?g=1",
				  "thumbnail": "image://mumble/steam-thumb-1?g=1", "title": "Screenshot one" },
				{ "kind": "video", "url": "https://cdn.example.test/trailer.mp4",
				  "thumbnail": "image://mumble/steam-thumb-2?g=1", "title": "Trailer" },
				{ "kind": "image", "url": "image://mumble/steam-shot-2?g=1",
				  "thumbnail": "image://mumble/steam-thumb-3?g=1", "title": "Screenshot two" }
			],
			"metadata": { "provider": "steam", "previewKind": "gameStoreProduct",
				"steamAppName": "Hades II", "steamPrice": "29,99 €" }
		}
		card.previewIdentity = "message:steam-gallery"
		card.userExpanded = true
		tryVerify(function() { return card.expanded && card.implicitHeight > 0 })
		previewLoader.height = Math.ceil(card.implicitHeight)
		wait(0)
		const rail = findChild(card, "previewSteamMediaRail")
		verify(rail !== null)
		tryCompare(rail, "visible", true)
		compare(rail.count, 3)
		rail.forceLayout()
		tryVerify(function() {
			return findChild(card, "previewSteamMediaThumbnail_1") !== null
		})
		const second = findChild(card, "previewSteamMediaThumbnail_1")
		verify(second !== null)
		compare(second.Accessible.name, "Steam trailer 2")
		mouseClick(second)
		compare(card.selectedMediaIndex, 1)
		compare(second.Accessible.selected, true)
	}

	function test_attachment_tile_is_bounded_below_360_width() {
		attachmentLoader.width = 220
		const gallery = attachmentLoader.item
		tryCompare(gallery, "compactLayout", true)
		const tile = findChild(gallery, "attachment_asset:1")
		verify(tile !== null)
		verify(tile.x >= -0.5 && tile.x + tile.width <= gallery.width + 0.5)
		verify(tile.height >= 96 && tile.height <= 240)
		const action = findChild(gallery, "attachmentAction_asset:1")
		compare(action.Accessible.description, "Attachment 1 of 1")
	}

	function test_attachment_gallery_bounds_items_and_surfaces_rejected_images() {
		const gallery = attachmentLoader.item
		const attachments = []
		for (let index = 0; index < 24; ++index) {
			attachments.push({
				"id": "bounded:" + index,
				"kind": "image",
				"thumbnailUrl": "image://mumble/bounded-" + index + "?g=1",
				"alt": "Attachment " + index
			})
		}
		gallery.attachments = attachments
		compare(gallery.visibleAttachments.length, 16)
		compare(gallery.Accessible.description, "16 attachments")

		gallery.attachments = [{
			"id": "rejected",
			"kind": "image",
			"thumbnailUrl": "https://untrusted.example/image.png",
			"alt": "Rejected remote image"
		}]
		const errorLabel = findChild(gallery, "attachmentError_rejected")
		tryCompare(errorLabel, "visible", true)
		compare(errorLabel.textFormat, Text.PlainText)
		compare(errorLabel.Accessible.role, Accessible.AlertMessage)
		const action = findChild(gallery, "attachmentAction_rejected")
		const tile = findChild(gallery, "attachment_rejected")
		action.forceActiveFocus()
		tryCompare(action, "activeFocus", true)
		verify(tile.border.width > 1)
	}

	function test_attachment_loading_state_and_original_download_action() {
		const gallery = attachmentLoader.item
		gallery.attachments = [{
			"id": "asset:42",
			"assetId": "42",
			"kind": "image",
			"mime": "image/png",
			"fileName": "photo.png",
			"state": "loading"
		}]
		const busy = findChild(gallery, "attachmentBusyIndicator_asset:42")
		const error = findChild(gallery, "attachmentError_asset:42")
		const action = findChild(gallery, "attachmentAction_asset:42")
		const download = findChild(gallery, "attachmentDownload_asset:42")
		verify(busy !== null && error !== null && action !== null && download !== null)
		tryCompare(busy, "visible", true)
		compare(error.visible, false)
		compare(action.enabled, false)
		compare(download.visible, true)

		gallery.attachments = [{
			"id": "asset:42",
			"assetId": "42",
			"kind": "image",
			"mime": "image/png",
			"fileName": "photo.png",
			"state": "error"
		}]
		const failedError = findChild(gallery, "attachmentError_asset:42")
		const failedAction = findChild(gallery, "attachmentAction_asset:42")
		const failedDownload = findChild(gallery, "attachmentDownload_asset:42")
		verify(failedError !== null && failedAction !== null && failedDownload !== null)
		tryCompare(failedError, "visible", true)
		tryCompare(failedAction, "enabled", true)
		mouseClick(failedDownload)
		compare(attachmentDownloadSpy.count, 1)
		compare(attachmentDownloadSpy.signalArguments[0][0].assetId, "42")
	}

	function test_managed_animation_unloads_when_preview_is_inactive() {
		const card = previewLoader.item
		const managedGif = managedGifUrl
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
		const animationLoader = findChild(card, "previewExpandedAnimatedLoader")
		verify(animationLoader !== null)
		tryCompare(animationLoader, "active", true)
		const animation = findChild(card, "previewExpandedAnimatedImage")
		verify(animation !== null)
		tryCompare(animation, "requestedSource", managedGif)
		card.renderActive = false
		tryCompare(animationLoader, "active", false)
		tryVerify(function() {
			return findChild(card, "previewExpandedAnimatedImage") === null
		})
		card.renderActive = true
	}

	function test_content_warning_hides_media_until_explicit_keyboard_reveal_and_resets() {
		const card = previewLoader.item
		card.preview = {
			"state": "ready",
			"title": "Sensitive preview",
			"previewSize": "compact",
			"metadata": {
				"contentWarning": "Sensitive imagery",
				"thumbnailBlur": true
			},
			"mediaItems": [{
				"kind": "image", "mime": "image/png",
				"url": "image://mumble/sensitive?g=1"
			}]
		}
		card.previewIdentity = "message:sensitive|one"
		verify(card.mediaRequiresReveal)
		compare(findChild(card, "previewCompactImage").source.toString(), "")
		const reveal = findChild(card, "previewCompactRevealButton")
		const revealState = findChild(card, "previewCompactRevealState")
		const compactRevealSurface = findChild(card, "previewCompactRevealSurface")
		const expandedRevealSurface = findChild(card, "previewExpandedRevealSurface")
		const embedRevealSurface = findChild(card, "previewEmbedRevealSurface")
		const providerWarning = findChild(card, "providerDetailsWarning")
		verify(reveal !== null && reveal.visible)
		verify(revealState !== null)
		verify(compactRevealSurface !== null && compactRevealSurface.visible)
		verify(expandedRevealSurface !== null && !expandedRevealSurface.visible)
		verify(embedRevealSurface !== null && !embedRevealSurface.visible)
		verify(providerWarning !== null && !providerWarning.visible)
		verify(reveal.Accessible.description.indexOf("Sensitive imagery") >= 0)
		compare(findChild(card, "previewExpandedRevealButton").visible, false)
		mouseMove(reveal, reveal.width / 2, reveal.height / 2)
		tryVerify(function() { return revealState.visible && revealState.color.a > 0 })
		mousePress(reveal, reveal.width / 2, reveal.height / 2, Qt.LeftButton)
		tryCompare(revealState, "color", Theme.accentSubtle)
		mouseRelease(reveal, reveal.width / 2, reveal.height / 2, Qt.LeftButton)
		verify(!card.mediaRequiresReveal)
		verify(!compactRevealSurface.visible && !expandedRevealSurface.visible
			&& !embedRevealSurface.visible && !providerWarning.visible)
		card.sensitiveMediaRevealed = false
		reveal.forceActiveFocus()
		tryCompare(reveal, "activeFocus", true)
		tryCompare(findChild(card, "previewCompactRevealFocus"), "visible", true)
		keyClick(Qt.Key_Space)
		verify(!card.mediaRequiresReveal)
		compare(findChild(card, "previewCompactImage").source.toString(),
			"image://mumble/sensitive?g=1")
		card.previewIdentity = "message:sensitive|two"
		verify(card.mediaRequiresReveal)
		card.userExpanded = true
		tryCompare(expandedRevealSurface, "visible", true)
		compare(compactRevealSurface.visible, false)
		compare(providerWarning.visible, false)
		const expandedReveal = findChild(card, "previewExpandedRevealButton")
		verify(expandedReveal !== null && expandedReveal.visible)
		expandedReveal.clicked()
		verify(!card.mediaRequiresReveal)
		verify(!compactRevealSurface.visible && !expandedRevealSurface.visible
			&& !embedRevealSurface.visible && !providerWarning.visible)
		card.sensitiveMediaRevealed = true
		card.resetForReuse()
		verify(card.mediaRequiresReveal)
	}

	function test_transparent_media_actions_have_focus_rings_and_expanded_errors_are_descriptive() {
		const card = previewLoader.item
		card.preview = {
			"state": "ready", "title": "Pipeline image", "description": "Sanitized provider image failure",
			"previewSize": "compact", "mediaItems": [{
				"kind": "image", "mime": "image/png", "url": "image://mumble/image?g=focus"
			}]
		}
		card.previewIdentity = "message:focus-image"
		const compactAction = findChild(card, "previewCompactMediaButton")
		const compactState = findChild(card, "previewCompactMediaState")
		verify(compactAction !== null)
		verify(compactState !== null)
		tryVerify(function() {
			return card.compact && compactAction.enabled
				&& compactAction.width <= 56.5 && compactAction.height <= 56.5
		})
		compare(compactAction.Accessible.ignored, false)
		mouseMove(compactAction, compactAction.width / 2, compactAction.height / 2)
		tryVerify(function() { return compactState.visible && compactState.color.a > 0 })
		mousePress(compactAction, compactAction.width / 2, compactAction.height / 2, Qt.LeftButton)
		tryCompare(compactState, "color", Theme.accentSubtle)
		mouseRelease(compactAction, compactAction.width / 2, compactAction.height / 2, Qt.LeftButton)
		compactAction.forceActiveFocus()
		tryCompare(compactAction, "activeFocus", true)
		const compactFocus = findChild(card, "previewCompactMediaFocus")
		tryCompare(compactFocus, "visible", true)
		compare(compactFocus.border.color, Theme.focus)
		compare(compactFocus.border.width, Theme.focusRingWidth)

		card.userExpanded = true
		tryVerify(function() { return card.expanded && card.implicitHeight > 180 })
		// The expanded media surface grows beyond the compact fixture height. Keep
		// the following attachment fixture below it so pointer tests exercise the
		// media button rather than a later sibling layered over the same pixels.
		previewLoader.height = Math.ceil(card.implicitHeight)
		wait(0)
		const expandedAction = findChild(card, "previewExpandedMediaButton")
		const expandedState = findChild(card, "previewExpandedMediaState")
		const expandedImage = findChild(card, "previewExpandedStaticImage")
		const expandedPanel = findChild(card, "previewExpandedMediaPanel")
		tryCompare(expandedImage, "status", Image.Ready)
		verify(expandedAction.visible && expandedAction.enabled,
			"expanded=" + card.expanded + " kind=" + card.currentMediaKind
			+ " source=" + expandedImage.source + " status=" + expandedImage.status
			+ " panel=" + expandedPanel.visible + " panelSize=" + expandedPanel.width + "x" + expandedPanel.height
			+ " actionVisible=" + expandedAction.visible + " actionEnabled=" + expandedAction.enabled)
		verify(expandedState !== null)
		const expandedCenter = expandedAction.mapToItem(testCase,
			expandedAction.width / 2, expandedAction.height / 2)
		verify(expandedCenter.y < attachmentLoader.y,
			"expanded media action must stay above the attachment fixture")
		mouseMove(expandedAction, expandedAction.width / 2, expandedAction.height / 2)
		tryVerify(function() { return expandedState.visible && expandedState.color.a > 0 })
		expandedAction.forceActiveFocus()
		const expandedFocus = findChild(card, "previewExpandedMediaFocus")
		tryCompare(expandedFocus, "visible", true)
		compare(expandedFocus.border.color, Theme.focus)
		compare(expandedFocus.border.width, Theme.focusRingWidth)
		verify(findChild(card, "previewExpandedMediaScrim") !== null)

		card.preview = {
			"state": "error", "failed": true, "title": "Broken image",
			"errorDescription": "Sanitized image decoder error",
			"previewSize": "large", "mediaItems": [{
				"kind": "image", "mime": "image/png", "url": "image://mumble/error-fixture?g=1"
			}]
		}
		card.previewIdentity = "message:broken-image"
		const expandedError = findChild(card, "previewExpandedError")
		tryCompare(expandedError, "visible", true)
		verify(expandedError.text.indexOf("Sanitized image decoder error") >= 0)
		compare(expandedError.color, Theme.danger)
		compare(expandedError.Accessible.role, Accessible.AlertMessage)
		verify(expandedError.Accessible.description.indexOf("Sanitized image decoder error") >= 0)
	}

	function test_disabled_compact_media_overlay_is_not_exposed() {
		const card = previewLoader.item
		card.preview = {
			"state": "ready", "title": "Metadata-only preview", "previewSize": "compact"
		}
		card.previewIdentity = "message:metadata-only"
		const compactAction = findChild(card, "previewCompactMediaButton")
		verify(compactAction !== null)
		tryCompare(compactAction, "enabled", false)
		compare(compactAction.Accessible.ignored, true)
	}

    function test_attachment_is_keyboard_and_pointer_actionable() {
        const action = findChild(attachmentLoader.item, "attachmentAction_asset:1")
        verify(action !== null)
        const tile = findChild(attachmentLoader.item, "attachment_asset:1")
		const stateOverlay = findChild(attachmentLoader.item, "attachmentStateOverlay_asset:1")
        verify(tile !== null)
		verify(stateOverlay !== null)
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
		mouseMove(action, action.width / 2, action.height / 2)
		tryVerify(function() { return action.hovered && stateOverlay.color.a > 0 })
		mousePress(action, action.width / 2, action.height / 2, Qt.LeftButton)
		tryCompare(stateOverlay, "color", Theme.accentSubtle)
		mouseRelease(action, action.width / 2, action.height / 2, Qt.LeftButton)
        compare(attachmentSpy.count, 1)
        compare(attachmentSpy.signalArguments[0][0].id, "asset:1")
        action.forceActiveFocus()
        tryCompare(action, "activeFocus", true)
		compare(tile.border.color, Theme.focus)
		compare(tile.border.width, Theme.focusRingWidth)
        keyClick(Qt.Key_Space)
        compare(attachmentSpy.count, 2)
        compare(attachmentSpy.signalArguments[1][0].id, "asset:1")

		action.enabled = false
		tryVerify(function() { return stateOverlay.color.a > 0 })
		compare(tile.border.color, Theme.divider)
		action.enabled = true
    }
}
