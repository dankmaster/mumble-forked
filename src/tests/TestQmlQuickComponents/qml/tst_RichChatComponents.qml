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
		id: attachmentRetrySpy
		target: attachmentLoader.item
		signalName: "attachmentRetryRequested"
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
		id: imageRefreshSpy
		target: previewLoader.item
		signalName: "imageRefreshRequested"
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
		id: popoutDirectMediaSpy
		target: previewLoader.item
		signalName: "popoutDirectMediaRequested"
	}

	SignalSpy {
		id: sizePresetSpy
		target: previewLoader.item
		signalName: "sizePresetRequested"
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
		property bool detachedPlaybackSupported: true
		property string sessionId: ""
		property string provider: "youtube"
		property string url: "https://www.youtube.com/embed/test"
		property string audioUrl: ""
		property string state: "idle"
		property string error: ""
		property string errorCode: ""
		property bool playbackControllable: true
		property bool playbackControlAllowed: true
		property bool sharedAvailable: false
		property bool sharedJoined: false
		property bool sharedHost: false
		property int sharedParticipantCount: 0
		property string mediaMime: ""
		property real position: 0
		property real duration: 0
		property int loadProgress: 0
		property int volume: 100
		property bool muted: false
		property int detachCalls: 0
		property int playCalls: 0
		property int pauseCalls: 0
		property int retryCalls: 0
		property int closeCalls: 0
		property int typedErrorCalls: 0
		function detach() {
			detachCalls += 1
			detached = true
		}
		function pause() {
			pauseCalls += 1
			state = "paused"
		}
		function play() {
			playCalls += 1
			state = "playing"
		}
		function retry() {
			retryCalls += 1
			state = "loading"
			error = ""
			errorCode = ""
		}
		function reportError(message) {
			state = "error"
			error = String(message || "")
			errorCode = "playback-failed"
		}
		function reportTypedError(code, message) {
			typedErrorCalls += 1
			state = "error"
			errorCode = String(code || "")
			error = String(message || "")
		}
		function closePlayer() {
			closeCalls += 1
			active = false
			detached = true
		}
	}

	QtObject {
		id: mediaRuntime
		property bool runtimeReady: false
		property bool runtimePreparing: false
		property string runtimeError: ""
		property int retryCalls: 0
		function retryRuntime() {
			retryCalls += 1
			runtimeError = ""
			runtimePreparing = true
		}
	}

    function init() {
        tryVerify(function() {
            return bodyLoader.item !== null && previewLoader.item !== null && attachmentLoader.item !== null
        })
		attachmentSpy.clear()
		attachmentDownloadSpy.clear()
		attachmentRetrySpy.clear()
        directMediaSpy.clear()
        externalOpenSpy.clear()
        imageOpenSpy.clear()
		imageRefreshSpy.clear()
		inlinePlaySpy.clear()
		popoutPlaySpy.clear()
		popoutDirectMediaSpy.clear()
		sizePresetSpy.clear()
		watchTogetherSpy.clear()
		linkSpy.clear()
		bodyLoader.item.segments = [
			{ "text": "<img src=x onerror=alert(1)>" },
			{ "text": " bad", "href": "javascript:alert(2)" },
			{ "text": " safe", "href": "https://example.com/path?q=1", "bold": true }
		]
		bodyLoader.item.accessibilitySuppressed = false
		bodyLoader.item.resourceActive = true
		bodyLoader.item.animationsEnabled = true
		bodyLoader.item.hoverEffectsEnabled = true
		previewLoader.width = testCase.width
		previewLoader.height = 180
		attachmentLoader.width = testCase.width
		attachmentLoader.item.resourceActive = true
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
		previewLoader.item.mediaProfileFactory = null
		previewLoader.item.visualMediaFixtureMode = ""
		previewLoader.item.mediaSessionId = ""
		previewLoader.item.savedSizePreset = ""
		previewLoader.item.inlinePlayerComponentUrl = Qt.resolvedUrl(
			"../../../mumble/qml-shell/InlineMediaPlayer.qml")
		previewLoader.item.resetForReuse()
		const previewOverflowMenu = findChild(previewLoader.item, "previewOverflowMenu")
		if (previewOverflowMenu && previewOverflowMenu.visible)
			previewOverflowMenu.close()
		inlineSession.active = false
		inlineSession.detached = true
		inlineSession.detachedPlaybackSupported = true
		inlineSession.sessionId = ""
		inlineSession.url = "https://www.youtube.com/embed/test"
		inlineSession.audioUrl = ""
		inlineSession.provider = "youtube"
		inlineSession.state = "idle"
		inlineSession.error = ""
		inlineSession.errorCode = ""
		inlineSession.playbackControllable = true
		inlineSession.playbackControlAllowed = true
		inlineSession.sharedAvailable = false
		inlineSession.sharedJoined = false
		inlineSession.sharedHost = false
		inlineSession.detachCalls = 0
		inlineSession.playCalls = 0
		inlineSession.pauseCalls = 0
		inlineSession.retryCalls = 0
		inlineSession.closeCalls = 0
		inlineSession.typedErrorCalls = 0
		mediaRuntime.runtimeReady = false
		mediaRuntime.runtimePreparing = false
		mediaRuntime.runtimeError = ""
		mediaRuntime.retryCalls = 0
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
		compare(body.lineHeight, Theme.chatBodyLineHeight)
		compare(renderedText.lineHeightMode, Text.ProportionalHeight)
		compare(renderedText.lineHeight, Theme.chatBodyLineHeight)
		compare(body.keyboardLinks.length, 1)
		const linkTarget = findChild(body, "richMessageBodyLinkTarget")
		verify(linkTarget !== null)
		linkTarget.forceActiveFocus()
		tryCompare(linkTarget, "activeFocus", true)
		keyClick(Qt.Key_Return)
		compare(linkSpy.count, 1)
		compare(linkSpy.signalArguments[0][0], "https://example.com/path?q=1")
    }

	function test_multiple_text_links_keep_one_visible_named_keyboard_target() {
		const body = bodyLoader.item
		body.segments = [
			{ "text": "Documentation", "href": "https://example.com/docs" },
			{ "text": " and " },
			{ "text": "Support", "href": "https://example.com/support" }
		]
		const renderedText = findChild(body, "richMessageBodyText")
		const linkTarget = findChild(body, "richMessageBodyLinkTarget")
		const focusRing = findChild(body, "richMessageBodyFocusRing")
		verify(renderedText !== null && renderedText.visible && linkTarget !== null && focusRing !== null)
		compare(linkTarget.objectName, "richMessageBodyLinkTarget")
		compare(body.keyboardLinks.length, 2)
		compare(body.keyboardLinkEntries[0].label, "Documentation")
		compare(body.keyboardLinkEntries[1].label, "Support")
		compare(body.Accessible.ignored, true)
		compare(linkTarget.Accessible.role, Accessible.Link)
		compare(linkTarget.Accessible.name, "Documentation and Support")
		verify(linkTarget.Accessible.description.indexOf("Documentation") >= 0)

		linkTarget.forceActiveFocus()
		tryCompare(linkTarget, "activeFocus", true)
		tryCompare(focusRing, "visible", true)
		keyClick(Qt.Key_Right)
		compare(body.keyboardLinkIndex, 1)
		compare(linkTarget.Accessible.name, "Documentation and Support")
		verify(linkTarget.Accessible.description.indexOf("Link 2 of 2") >= 0)
		verify(linkTarget.Accessible.description.indexOf("Support") >= 0)
		keyClick(Qt.Key_Return)
		compare(linkSpy.count, 1)
		compare(linkSpy.signalArguments[0][0], "https://example.com/support")
	}

	function test_modal_owner_suppresses_promoted_rich_message_semantics() {
		const body = bodyLoader.item
		body.segments = [ { "text": "Background message", "href": "https://example.com/background" } ]
		const linkTarget = findChild(body, "richMessageBodyLinkTarget")
		verify(linkTarget !== null)
		compare(linkTarget.Accessible.ignored, false)
		verify(linkTarget.activeFocusOnTab)

		body.accessibilitySuppressed = true
		compare(linkTarget.Accessible.ignored, true)
		compare(linkTarget.activeFocusOnTab, false)
		body.segments = [ {
			"kind": "image", "source": "image://mumble/fixture-inline-image",
			"alt": "Background image", "href": "https://example.com/background-image"
		} ]
		wait(0)
		const imageCard = findChild(body, "richMessageImageCard_0")
		const imagePointer = findChild(body, "richMessageImagePointer_0")
		verify(imageCard !== null)
		verify(imagePointer !== null)
		compare(imageCard.Accessible.ignored, true)
		body.animationsEnabled = false
		body.hoverEffectsEnabled = false
		compare(imagePointer.hoverEnabled, false)

		body.accessibilitySuppressed = false
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
		const stateBadge = findChild(card, "previewStateBadge")
		const stateBadgeLabel = findChild(card, "previewStateBadgeLabel")
		const actionFlow = findChild(card, "previewActionFlow")
		const openButton = findChild(card, "previewOpenButton")
		verify(stateBadge !== null && stateBadge.visible)
		verify(stateBadgeLabel !== null)
		compare(stateBadgeLabel.text, "Loading")
		verify(actionFlow !== null && actionFlow.visible)
		verify(openButton !== null && openButton.visible && openButton.enabled)
		compare(openButton.Accessible.role, Accessible.Link)
		openButton.clicked()
		compare(externalOpenSpy.count, 1)
		compare(externalOpenSpy.signalArguments[0][0], "https://example.com/card")
		card.preview = {
            "state": "error",
            "failed": true,
            "title": "Preview unavailable",
			"description": "The original provider summary remains ordinary content.",
			"errorDescription": "<b>Provider request failed</b>",
            "url": "https://example.com/card"
        }
		compare(card.previewState, "error")
		compare(stateBadgeLabel.text, "Unavailable")
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
				"productDelivery": "Tomorrow", "productRating": "4.8", "productBrand": "Example",
				"productSku": "SKU-42"
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
		card.setSizePreset("large")
        card.selectedMediaIndex = 1
		const animationLoader = findChild(card, "previewExpandedAnimatedLoader")
		verify(animationLoader !== null)
		compare(animationLoader.active, false)
        verify(card.expanded)
		compare(card.sizePresetOverride, "large")
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
		compare(card.sizePresetOverride, "large")
		card.savedSizePreset = "large"
		card.previewIdentity = "message:2|gallery"
		verify(!card.userExpanded)
		compare(card.sizePresetOverride, "")
		compare(card.effectiveSizePreset, "large")
		compare(card.selectedMediaIndex, 0)
		card.setSizePreset("compact")
		card.selectedMediaIndex = 1
		card.resetForReuse()
		verify(!card.userExpanded)
		compare(card.sizePresetOverride, "")
		compare(card.savedSizePreset, "large")
		compare(card.effectiveSizePreset, "large")
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
		tryVerify(function() { return expandedPanel.height > compactImage.height * 3 }, 5000,
			"expanded media height " + expandedPanel.height
			+ " did not settle above compact media height " + compactImage.height)
	}

	function test_pooled_rich_images_release_sources_without_changing_geometry() {
		bodyLoader.item.segments = [{
			"kind": "image", "source": "image://mumble/fixture-inline",
			"width": 320, "height": 180, "alt": "Fixture inline image"
		}]
		tryVerify(function() {
			return findChild(bodyLoader.item, "richMessageInlineImage_0") !== null
		})
		const inlineImage = findChild(bodyLoader.item, "richMessageInlineImage_0")
		const bodyHeight = bodyLoader.item.implicitHeight
		verify(String(inlineImage.source).length > 0)
		bodyLoader.item.resourceActive = false
		tryCompare(inlineImage, "source", "")
		compare(bodyLoader.item.implicitHeight, bodyHeight)

		const attachmentImage = findChild(attachmentLoader.item, "attachmentImage_asset:1")
		verify(attachmentImage !== null)
		const attachmentHeight = attachmentLoader.item.implicitHeight
		verify(String(attachmentImage.source).length > 0)
		attachmentLoader.item.resourceActive = false
		tryCompare(attachmentImage, "source", "")
		compare(attachmentLoader.item.implicitHeight, attachmentHeight)
	}

	function test_product_uses_one_inset_gallery_card_without_duplicate_media() {
		const card = previewLoader.item
		card.preview = {
			"state": "ready",
			"kind": "product",
			"title": "Logitech G Pro X Superlight 2",
			"subtitle": "Inet",
			"description": "Wireless mouse · 60 g · LIGHTSPEED",
			"previewSize": "large",
			"mediaItems": [{
				"kind": "image",
				"mime": "image/png",
				"url": "image://mumble/full-bleed-fixture?g=product"
			}],
			"metadata": {
				"provider": "inet",
				"previewKind": "product",
				"productTitle": "Logitech G Pro X Superlight 2",
				"productPrice": "1 499 kr",
				"productAvailability": "In stock online"
			}
		}
		card.previewIdentity = "message:expanded-product-insets"
		const content = findChild(card, "previewContent")
		const header = findChild(card, "previewGenericHeader")
		const details = findChild(card, "providerDetails")
		const expandedPanel = findChild(card, "previewExpandedMediaPanel")
		const commerceCard = findChild(card, "providerCommerceCard")
		const commerceHero = findChild(card, "providerCommerceHero")
		const commerceTitle = findChild(card, "providerCommerceTitle")
		verify(content !== null && header !== null && details !== null && expandedPanel !== null)
		verify(commerceCard !== null && commerceHero !== null && commerceTitle !== null)
		tryVerify(function() { return card.expanded && card.implicitHeight > 180 })
		previewLoader.height = Math.ceil(card.implicitHeight)
		wait(0)
		compare(header.visible, false)
		tryCompare(commerceCard, "visible", true)
		tryCompare(details, "width", content.width)
		const commerceOrigin = commerceCard.mapToItem(content, 0, 0)
		verify(commerceOrigin.x >= -0.5)
		verify(commerceOrigin.x + commerceCard.width <= content.width + 0.5)
		compare(commerceTitle.text, "Logitech G Pro X Superlight 2")
		tryCompare(commerceHero, "visible", true)
		compare(expandedPanel.visible, false)
	}

	function test_article_uses_compact_image_tldr_and_human_date_without_detail_tiles() {
		const card = previewLoader.item
		card.preview = {
			"state": "ready",
			"kind": "article",
			"title": "Government halves the price of monthly transit passes",
			"description": "Public transit monthly passes will cost half as much for six months.",
			"url": "https://news.example.com/transit",
			"mediaItems": [{
				"kind": "image",
				"mime": "image/jpeg",
				"url": "image://mumble/article-hero?g=1"
			}],
			"metadata": {
				"previewKind": "article",
				"previewProvider": "goteborgsposten",
				"articlePublisher": "Göteborgs-Posten",
				"articleSection": "Politics",
				"articleAuthor": "Karin Jansson",
				"articlePublishedAt": "2026-05-25T18:17:01.000Z",
				"articleTitle": "Government halves the price of monthly transit passes",
				"articleDescription": "Public transit monthly passes will cost half as much for six months."
			}
		}
		card.previewIdentity = "message:article-tldr"

		const header = findChild(card, "previewGenericHeader")
		const article = findChild(card, "providerArticleCard")
		const hero = findChild(card, "providerArticleHero")
		const title = findChild(card, "providerArticleTitle")
		const tldr = findChild(card, "providerArticleTldr")
		const meta = findChild(card, "providerArticleMeta")
		const stats = findChild(card, "providerDetailsStats")
		verify(header !== null && article !== null && hero !== null && title !== null
			&& tldr !== null && meta !== null && stats !== null)
		compare(header.visible, false)
		tryCompare(article, "visible", true)
		tryCompare(hero, "visible", true)
		compare(title.text, "Government halves the price of monthly transit passes")
		compare(tldr.text, "Public transit monthly passes will cost half as much for six months.")
		verify(meta.text.indexOf("Göteborgs-Posten") >= 0)
		verify(meta.text.indexOf("Politics") >= 0)
		verify(meta.text.indexOf("T18:17:01") < 0)
		compare(stats.visible, false)
		compare(findChild(card, "previewDescription").visible, false)
	}

	function test_marketplace_gallery_uses_all_images_and_keeps_navigation_outside_the_photo() {
		const card = previewLoader.item
		card.preview = {
			"state": "ready",
			"kind": "marketplaceListing",
			"title": "MSI RTX 4070 Ti SUPER 16G",
			"description": "Sparingly used and works perfectly.",
			"url": "https://www.blocket.se/recommerce/forsale/item/17926061",
			"mediaItems": [
				{ "kind": "image", "mime": "image/jpeg",
				  "url": "image://mumble/blocket-one?g=1" },
				{ "kind": "image", "mime": "image/jpeg",
				  "url": "image://mumble/blocket-two?g=1" },
				{ "kind": "image", "mime": "image/jpeg",
				  "url": "image://mumble/blocket-three?g=1" }
			],
			"metadata": {
				"previewKind": "marketplaceListing",
				"previewProvider": "blocket",
				"marketplaceProvider": "Blocket",
				"listingPrice": "8 500 kr",
				"listingLocation": "Stockholm",
				"listingCondition": "Used",
				"listingId": "17926061",
				"listingDescription": "Sparingly used and works perfectly."
			}
		}
		card.previewIdentity = "message:blocket-gallery"

		const gallery = findChild(card, "providerCommerceCard")
		const hero = findChild(card, "providerCommerceHero")
		const heroImage = findChild(card, "providerCommerceHeroImage")
		const controls = findChild(card, "providerCommerceGalleryControls")
		const price = findChild(card, "providerCommercePrice")
		verify(gallery !== null && hero !== null && heroImage !== null
			&& controls !== null && price !== null)
		tryCompare(gallery, "visible", true)
		tryCompare(hero, "visible", true)
		tryCompare(controls, "visible", true)
		compare(price.text, "8 500 kr")
		compare(card.selectedMediaIndex, 0)
		card.selectedMediaIndex = 1
		wait(0)
		compare(heroImage.source.toString(), "image://mumble/blocket-two?g=1")
		const controlsOrigin = controls.mapToItem(card, 0, 0)
		const heroOrigin = hero.mapToItem(card, 0, 0)
		verify(controlsOrigin.y >= heroOrigin.y + hero.height - 1)
	}

	function test_active_provider_player_has_only_external_actions_below_media_and_no_duplicate_spotify_details() {
		const card = previewLoader.item
		card.preview = {
			"state": "ready",
			"title": "Bobo & Gileus",
			"url": "https://open.spotify.com/album/example",
			"embedUrl": "https://open.spotify.com/embed/album/example",
			"embedKind": "spotify",
			"embedAspect": "compact-audio",
			"metadata": {
				"previewKind": "audio",
				"previewProvider": "spotify",
				"audioProvider": "Spotify",
				"audioProgram": "Bobo & Gileus"
			}
		}
		card.previewIdentity = "message:spotify-clean-surface"
		card.mediaSessionId = card.previewIdentity
		card.mediaSessionController = inlineSession
		card.renderActive = false
		inlineSession.sessionId = card.mediaSessionId
		inlineSession.provider = "spotify"
		inlineSession.playbackControllable = false
		inlineSession.detached = false

		const panel = findChild(card, "previewEmbedMediaPanel")
		const mediaSlot = findChild(card, "previewEmbedMediaSlot")
		const content = findChild(card, "previewContent")
		const actionFlow = findChild(card, "previewActionFlow")
		const details = findChild(card, "providerDetails")
		const closeButton = findChild(card, "previewInlineCloseButton")
		const originalButton = findChild(card, "previewEmbedOriginalButton")
		verify(panel !== null && mediaSlot !== null && content !== null && actionFlow !== null
			&& details !== null && closeButton !== null && originalButton !== null)
		tryCompare(card, "providerOwnsDetails", true)
		compare(details.visible, false)
		const collapsedHeight = card.implicitHeight

		inlineSession.active = true
		tryCompare(card, "inlinePlaybackActive", true)
		compare(card.inlineProviderOwnsDetails, true)
		compare(details.visible, false)
		tryVerify(function() {
			return Math.abs(card.implicitHeight - collapsedHeight) <= 1
		})
		tryCompare(closeButton, "visible", true)
		tryCompare(originalButton, "visible", true)
		tryVerify(function() {
			const settledPanelOrigin = panel.mapToItem(card, 0, 0)
			const settledCloseOrigin = closeButton.mapToItem(card, 0, 0)
			return mediaSlot.height >= panel.height - 1
				&& settledCloseOrigin.y >= settledPanelOrigin.y + panel.height - 1
		}, 5000, "Provider actions did not settle below the media viewport")
		const panelOrigin = panel.mapToItem(card, 0, 0)
		const closeOrigin = closeButton.mapToItem(card, 0, 0)
		verify(closeOrigin.y >= panelOrigin.y + panel.height - 1,
			"closeOrigin.y=" + closeOrigin.y + ", panelOrigin.y=" + panelOrigin.y
				+ ", panel.height=" + panel.height + ", mediaSlot.height=" + mediaSlot.height
				+ ", actionFlow.y=" + actionFlow.y + ", actionFlow.height=" + actionFlow.height
				+ ", actionFlow.implicitHeight=" + actionFlow.implicitHeight
				+ ", content.height=" + content.height + ", content.implicitHeight=" + content.implicitHeight
				+ ", card.height=" + card.height + ", card.implicitHeight=" + card.implicitHeight)
		verify(findChild(card, "previewEmbedProviderBadge") === null)
		verify(findChild(card, "previewEmbedProviderState") === null)
		closeButton.clicked()
		compare(inlineSession.closeCalls, 1)
		tryCompare(card, "inlinePlaybackActive", false)
	}

	function test_video_backed_animation_pause_action_lives_below_the_media() {
		const card = previewLoader.item
		card.preview = {
			"state": "ready",
			"title": "Animated reaction",
			"url": "https://x.com/example/status/1",
			"mediaUrl": "https://video.twimg.com/tweet_video/example.mp4",
			"mediaMime": "video/mp4",
			"metadata": {
				"previewProvider": "x",
				"contentBranch": "animated-gif-video-backed",
				"mediaPresentation": "animated-image"
			}
		}
		card.previewIdentity = "message:animated-footer"
		card.mediaSessionId = card.previewIdentity
		card.mediaSessionController = inlineSession
		card.renderActive = false
		inlineSession.sessionId = card.mediaSessionId
		inlineSession.provider = "direct"
		inlineSession.state = "playing"
		inlineSession.detached = false
		inlineSession.active = true

		const panel = findChild(card, "previewEmbedMediaPanel")
		const toggle = findChild(card, "previewInlineAnimationToggleButton")
		verify(panel !== null && toggle !== null)
		tryCompare(toggle, "visible", true)
		compare(toggle.text, "Pause animation")
		const panelOrigin = panel.mapToItem(card, 0, 0)
		const toggleOrigin = toggle.mapToItem(card, 0, 0)
		verify(toggleOrigin.y >= panelOrigin.y + panel.height - 1)
		toggle.clicked()
		compare(inlineSession.pauseCalls, 1)
		compare(inlineSession.state, "paused")
		compare(toggle.text, "Resume animation")
		toggle.clicked()
		compare(inlineSession.playCalls, 1)
		compare(inlineSession.state, "playing")
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
		verify(card.localPlaybackSupported)
		verify(!card.sharedPlaybackSupported)
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
		const originalFooterButton = findChild(card, "previewEmbedOriginalButton")
		const posterAction = findChild(card, "previewEmbedPosterAction")
		const cardOpenSurface = findChild(card, "previewCardOpenSurface")
		const externalPrimaryButton = findChild(card, "previewOpenButton")
		const originalButton = findChild(card, "previewOpenOriginalButton")
		const popoutButton = findChild(card, "previewPopoutButton")
		const overflowButton = findChild(card, "previewOverflowButton")
		verify(inlineButton !== null && inlineButton.visible)
		verify(originalFooterButton !== null && originalFooterButton.visible)
		verify(posterAction !== null && posterAction.visible && posterAction.enabled)
		verify(cardOpenSurface !== null && !cardOpenSurface.enabled)
		verify(externalPrimaryButton !== null && !externalPrimaryButton.visible)
		verify(popoutButton !== null && overflowButton !== null)
		compare(inlineButton.text, "Play here")
		verify(card.localPlaybackSupported)
		verify(card.sharedPlaybackSupported)
		compare(inlineButton.posterBacked, false)
		compare(inlineButton.implicitWidth, inlineButton.implicitHeight)
		compare(findChild(card, "previewPlayText").visible, false)
		compare(originalFooterButton.text, "Open")
		compare(originalFooterButton.Accessible.role, Accessible.Link)
		compare(card.playAccessibilityName, "Play Video here")
		compare(inlineButton.Accessible.name, card.playAccessibilityName)
		overflowButton.clicked()
		tryCompare(popoutButton, "visible", true)
		compare(popoutButton.text, "Open in separate player")
		tryCompare(originalButton, "visible", false)
		originalFooterButton.clicked()
		compare(externalOpenSpy.count, 1)
		compare(externalOpenSpy.signalArguments[0][0], "https://www.youtube.com/watch?v=test")
		posterAction.clicked()
		compare(inlinePlaySpy.count, 1)
		compare(inlinePlaySpy.signalArguments[0][0], "https://www.youtube.com/embed/test")
		compare(externalOpenSpy.count, 1)
		compare(card.effectiveSizePreset, "default")
		popoutButton.triggered()
		compare(popoutPlaySpy.count, 1)
		findChild(card, "previewOverflowMenu").close()
	}

	function test_instagram_post_uses_view_semantics_while_reel_and_tv_remain_playback() {
		const card = previewLoader.item
		const inlineButton = findChild(card, "previewPlayButton")
		const inlineIcon = findChild(card, "previewPlayIcon")
		const popoutButton = findChild(card, "previewPopoutButton")
		const watchButton = findChild(card, "previewWatchTogetherButton")
		const overflowButton = findChild(card, "previewOverflowButton")
		const overflowMenu = findChild(card, "previewOverflowMenu")
		verify(inlineButton !== null && inlineIcon !== null && popoutButton !== null
			&& watchButton !== null && overflowButton !== null && overflowMenu !== null)

		const fixtures = [
			{ "path": "p/static-post", "metadataKind": "post", "normalizedKind": "post",
			  "aspect": "square", "label": "View here", "icon": "eye",
			  "accessibleName": "View Instagram item here",
			  "accessibleDescription": "Loads the provider post in this preview",
			  "popoutLabel": "Open in separate viewer",
			  "popoutDescription": "Open the provider post in a separate window",
			  "localPlayback": true, "stageVisible": true },
			// Instagram historically used /p/ for video posts. Typed metadata must
			// be able to recover the playable media kind from that generic path.
			{ "path": "p/legacy-video", "metadataKind": "reel", "normalizedKind": "reel",
			  "aspect": "short", "label": "Play here", "icon": "play",
			  "accessibleName": "Play Instagram item here",
			  "accessibleDescription": "Loads the provider player in this preview",
			  "popoutLabel": "Open in separate player",
			  "popoutDescription": "Open the provider player in a separate window",
			  "localPlayback": true, "stageVisible": true },
			{ "path": "reel/moving-reel", "metadataKind": "reel", "normalizedKind": "reel",
			  "aspect": "short", "label": "Play here", "icon": "play",
			  "accessibleName": "Play Instagram item here",
			  "accessibleDescription": "Loads the provider player in this preview",
			  "popoutLabel": "Open in separate player",
			  "popoutDescription": "Open the provider player in a separate window",
			  "localPlayback": true, "stageVisible": true },
			// The canonical path wins over stale cached metadata. Older cache entries
			// classified /tv/ as a post even though it is a video embed.
			{ "path": "tv/legacy-video", "metadataKind": "post", "normalizedKind": "tv",
			  "aspect": "square", "label": "Play here", "icon": "play",
			  "accessibleName": "Play Instagram item here",
			  "accessibleDescription": "Loads the provider player in this preview",
			  "popoutLabel": "Open in separate player",
			  "popoutDescription": "Open the provider player in a separate window",
			  "localPlayback": true, "stageVisible": true }
		]
		let inlineRequests = 0
		for (let index = 0; index < fixtures.length; ++index) {
			const fixture = fixtures[index]
			const embedUrl = "https://www.instagram.com/" + fixture.path + "/embed/"
			card.preview = {
				"state": "ready", "title": "Instagram item",
				"url": "https://www.instagram.com/" + fixture.path + "/",
				"embedUrl": embedUrl, "embedKind": "instagram",
				"metadata": { "instagramMediaKind": fixture.metadataKind }
			}
			card.previewIdentity = "message:instagram-semantics:" + index
			wait(0)
			compare(card.instagramEmbedMediaKind, fixture.normalizedKind)
			compare(card.normalizedEmbedAspect, fixture.aspect)
			compare(card.localPlaybackSupported, fixture.localPlayback)
			compare(card.inlineMediaStageVisible, fixture.stageVisible)
			compare(card.providerPostPresentation, fixture.normalizedKind === "post")
			verify(!card.sharedPlaybackSupported)
			verify(!card.watchTogetherSupported)
			compare(inlineButton.visible, fixture.stageVisible)
			if (fixture.stageVisible) {
				compare(inlineButton.text, fixture.label)
				compare(inlineIcon.name, fixture.icon)
				compare(inlineButton.Accessible.name, fixture.accessibleName)
				compare(inlineButton.Accessible.description, fixture.accessibleDescription)
			}

			if (overflowButton.visible)
				overflowButton.clicked()
			const popoutExpected = fixture.localPlayback && fixture.normalizedKind !== "post"
			compare(popoutButton.visible, popoutExpected)
			if (popoutExpected) {
				compare(popoutButton.text, fixture.popoutLabel)
				compare(popoutButton.Accessible.description, fixture.popoutDescription)
			}
			compare(watchButton.visible, false)
			overflowMenu.close()

			if (fixture.localPlayback) {
				inlineButton.clicked()
				inlineRequests += 1
				compare(inlinePlaySpy.count, inlineRequests)
				compare(inlinePlaySpy.signalArguments[inlineRequests - 1][0], embedUrl)
				compare(inlinePlaySpy.signalArguments[inlineRequests - 1][1], "instagram")
			}
		}
	}

	function test_embed_preview_is_media_first_aspect_aware_and_lazy() {
		const card = previewLoader.item
		const panel = findChild(card, "previewEmbedMediaPanel")
		const poster = findChild(card, "previewEmbedPoster")
		const loader = findChild(card, "previewInlineMediaLoader")
		const playButton = findChild(card, "previewPlayButton")
		verify(panel !== null && poster !== null && loader !== null && playButton !== null)

		const cases = [
			{ "aspect": "", "normalized": "wide", "width": card.width,
				"height": card.width * 9 / 16 },
			{ "aspect": "short", "normalized": "short", "width": 280, "height": 280 * 16 / 9 },
			{ "aspect": "square", "normalized": "square", "width": card.actionAvailableWidth,
				"height": card.actionAvailableWidth },
			{ "aspect": "audio", "normalized": "audio", "width": card.width, "height": 352 },
			{ "aspect": "compact-audio", "normalized": "compact-audio",
				"width": card.width, "height": 166 }
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
			tryVerify(function() { return Math.abs(panel.width - fixture.width) < 1 }, 1000,
				fixture.normalized + " panel width=" + panel.width + " expected=" + fixture.width
				+ " card=" + card.width + " slot=" + panel.parent.width)
			tryVerify(function() { return Math.abs(panel.height - fixture.height) < 1 }, 1000,
				fixture.normalized + " panel height=" + panel.height + " expected=" + fixture.height
				+ " slot=" + panel.parent.height + " cardHeight=" + card.height)
			tryCompare(poster, "status", Image.Ready, 15000,
				fixture.normalized + " poster did not finish its asynchronous fixture load")
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

	function test_preview_state_visuals_are_compact_and_media_insets_are_not_clipped() {
		const card = previewLoader.item
		card.userExpanded = false
		card.sizePresetOverride = ""
		card.savedSizePreset = ""
		card.preview = {
			"state": "loading", "loading": true,
			"loadingLabel": "Fetching link preview", "host": "example.com"
		}
		card.previewIdentity = "message:loading-layout"
		const compactVisual = findChild(card, "previewCompactVisual")
		verify(compactVisual !== null && compactVisual.visible)
		compare(card.genericHeaderUsesThumbnailGeometry, false)
		tryCompare(compactVisual, "width", 64)
		tryCompare(compactVisual, "height", 64)

		card.preview = {
			"state": "ready", "title": "Vertical creator preview",
			"url": "https://www.tiktok.com/@mumble/video/layout",
			"embedUrl": "https://www.tiktok.com/player/v1/layout",
			"embedKind": "tiktok", "embedAspect": "short"
		}
		card.previewIdentity = "message:short-layout"
		compare(card.fullBleedMediaStage, false)
		compare(card.contentTopInset, Theme.space4)
		compare(card.contentBottomInset, Theme.space4)
		const content = findChild(card, "previewContent")
		const mediaSlot = findChild(card, "previewEmbedMediaSlot")
		verify(content !== null && mediaSlot !== null)
		compare(card.Accessible.ignored, false)
		compare(content.Accessible.ignored, true)
		compare(mediaSlot.Accessible.ignored, true)
		compare(card.implicitHeight, content.implicitHeight
			+ card.contentTopInset + card.contentBottomInset)

		card.preview = {
			"state": "ready", "title": "Wide media preview",
			"url": "https://www.youtube.com/watch?v=layout",
			"embedUrl": "https://www.youtube.com/embed/layout",
			"embedKind": "youtube", "embedAspect": "wide"
		}
		card.previewIdentity = "message:wide-layout"
		compare(card.fullBleedMediaStage, true)
		compare(card.contentTopInset, 0)
		compare(card.implicitHeight, content.implicitHeight + card.contentBottomInset)
	}

	function test_dark_preview_media_surfaces_use_overlay_contrast_tokens() {
		const card = previewLoader.item
		card.preview = {
			"state": "ready", "title": "Provider media",
			"url": "https://www.youtube.com/watch?v=overlay",
			"embedUrl": "https://www.youtube.com/embed/overlay",
			"embedKind": "youtube", "embedAspect": "wide"
		}
		card.previewIdentity = "message:overlay-contract"

		const emptyPosterIcon = findChild(card, "previewEmptyPosterIcon")
		const runtimeStatus = findChild(card, "previewInlineMediaRuntimeStatus")
		const failureHeading = findChild(card, "previewInlineMediaFailureHeading")
		const failureDetail = findChild(card, "previewInlineMediaFailureDetail")
		verify(emptyPosterIcon !== null && runtimeStatus !== null
			&& failureHeading !== null && failureDetail !== null)
		compare(String(emptyPosterIcon.color), String(Theme.mediaOverlayTextMuted))
		compare(String(runtimeStatus.color), String(Theme.mediaOverlayTextMuted))
		compare(String(failureHeading.color), String(Theme.mediaOverlayTextStrong))
		compare(String(failureDetail.color), String(Theme.mediaOverlayTextMuted))
	}

	function test_loading_embed_reserves_final_media_geometry() {
		const card = previewLoader.item
		const panel = findChild(card, "previewEmbedMediaPanel")
		const originalButton = findChild(card, "previewEmbedOriginalButton")
		const overflowButton = findChild(card, "previewOverflowButton")
		verify(panel !== null && originalButton !== null && overflowButton !== null)

		card.preview = {
			"state": "loading", "loading": true,
			"title": "Fetching YouTube preview",
			"url": "https://www.youtube.com/watch?v=test",
			"embedUrl": "https://www.youtube.com/embed/test",
			"embedKind": "youtube", "embedAspect": "wide"
		}
		card.previewIdentity = "message:loading-embed"
		tryCompare(panel, "visible", true)
		tryCompare(originalButton, "visible", true)
		compare(overflowButton.visible, false)
		originalButton.clicked()
		compare(externalOpenSpy.count, 1)
		compare(externalOpenSpy.signalArguments[0][0], "https://www.youtube.com/watch?v=test")
		tryVerify(function() { return Math.abs(panel.height - card.width * 9 / 16) < 1 }, 1000,
			"loading panel height=" + panel.height + " expected=" + card.width * 9 / 16
			+ " slot=" + panel.parent.height + " cardHeight=" + card.height)
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

	function test_active_inline_player_adds_controls_below_the_full_media_viewport() {
		const card = previewLoader.item
		const panel = findChild(card, "previewEmbedMediaPanel")
		card.preview = {
			"state": "ready", "title": "Full-width player",
			"url": "https://www.youtube.com/watch?v=test",
			"embedUrl": "https://www.youtube.com/embed/test",
			"embedKind": "youtube", "embedAspect": "wide"
		}
		card.previewIdentity = "message:active-player-geometry"
		card.renderActive = false
		card.mediaSessionController = inlineSession
		card.mediaSessionId = "message:active-player-geometry"
		inlineSession.sessionId = card.mediaSessionId
		inlineSession.provider = "youtube"
		inlineSession.detached = false
		inlineSession.active = true
		tryCompare(card, "inlinePlaybackActive", true)
		for (const fixture of [
			{ "preset": "compact", "width": card.responsiveCompactCardWidth },
			{ "preset": "default", "width": card.responsiveDefaultCardWidth },
			{ "preset": "large", "width": card.responsiveLargeCardWidth }
		]) {
			card.setSizePreset(fixture.preset)
			previewLoader.width = card.targetCardWidth
			tryCompare(card, "width", fixture.width)
			tryVerify(function() {
				return Math.abs(panel.height - (card.inlineMediaViewportHeight
					+ card.inlineControlsEstimate)) < 1
			})
			verify(Math.abs(card.inlineMediaViewportHeight / card.width - 9 / 16) < 0.01)
			verify(card.inlinePlaybackActive)
			compare(inlineSession.detachCalls, 0)
			compare(inlineSession.closeCalls, 0)
		}
		verify(panel.height > card.inlineMediaViewportHeight)
		compare(findChild(card, "previewInlineMediaLoader").active, false)
	}

	function test_inline_runtime_preparation_is_native_visible_and_defers_webengine_component() {
		const card = previewLoader.item
		card.preview = {
			"state": "ready", "title": "Deferred runtime",
			"url": "https://www.youtube.com/watch?v=runtime",
			"embedUrl": "https://www.youtube.com/embed/runtime",
			"embedKind": "youtube", "embedAspect": "wide"
		}
		card.previewIdentity = "message:runtime-preparation"
		card.mediaSessionId = "message:runtime-preparation"
		card.visualMediaFixtureMode = "active"
		card.mediaProfileFactory = mediaRuntime
		card.mediaSessionController = inlineSession
		inlineSession.sessionId = card.mediaSessionId
		inlineSession.provider = "youtube"
		inlineSession.detached = false
		mediaRuntime.runtimePreparing = true
		inlineSession.active = true

		const loader = findChild(card, "previewInlineMediaLoader")
		const loadingSurface = findChild(card, "previewInlineMediaRuntimeLoadingSurface")
		tryCompare(card, "inlinePlaybackActive", true)
		verify(loadingSurface !== null && loadingSurface.visible)
		compare(loadingSurface.surfaceId, "mediaSession.inline")
		compare(loadingSurface.rendererState, "loading")
		compare(loader.active, false)

		mediaRuntime.runtimePreparing = false
		mediaRuntime.runtimeReady = true
		tryCompare(loader, "active", true)
		tryVerify(function() { return loader.item !== null })
		compare(loadingSurface.visible, false)
	}

	function test_inline_runtime_error_exposes_keyboard_retry_without_loading_provider_surface() {
		const card = previewLoader.item
		card.preview = {
			"state": "ready", "title": "Retryable runtime",
			"url": "https://www.youtube.com/watch?v=runtime-error",
			"embedUrl": "https://www.youtube.com/embed/runtime-error",
			"embedKind": "youtube", "embedAspect": "wide"
		}
		card.previewIdentity = "message:runtime-error"
		card.mediaSessionId = "message:runtime-error"
		card.visualMediaFixtureMode = "active"
		card.mediaProfileFactory = mediaRuntime
		card.mediaSessionController = inlineSession
		inlineSession.sessionId = card.mediaSessionId
		inlineSession.provider = "youtube"
		inlineSession.detached = false
		const playButton = findChild(card, "previewPlayButton")
		verify(playButton !== null)
		playButton.forceActiveFocus()
		tryCompare(playButton, "activeFocus", true)
		card.prepareInlinePlaybackFocus()
		mediaRuntime.runtimeError = "The packaged media runtime is unavailable."
		inlineSession.active = true

		const loader = findChild(card, "previewInlineMediaLoader")
		const runtimeSurface = findChild(card, "previewInlineMediaRuntimeLoadingSurface")
		const status = findChild(card, "previewInlineMediaRuntimeStatus")
		const retry = findChild(card, "previewInlineMediaRuntimeRetryButton")
		verify(loader !== null && runtimeSurface !== null && status !== null && retry !== null)
		tryCompare(card, "inlinePlaybackActive", true)
		tryCompare(runtimeSurface, "visible", true)
		compare(runtimeSurface.rendererState, "error")
		compare(status.text, mediaRuntime.runtimeError)
		tryCompare(retry, "visible", true)
		compare(retry.Accessible.name, "Retry media player setup")
		tryCompare(retry, "activeFocus", true)
		compare(loader.active, false)
		compare(loader.item, null)

		retry.clicked()
		compare(mediaRuntime.retryCalls, 1)
		compare(inlineSession.retryCalls, 1)
		compare(mediaRuntime.runtimePreparing, true)
		compare(mediaRuntime.runtimeError, "")
		compare(runtimeSurface.rendererState, "loading")
		compare(loader.active, false)
		compare(loader.item, null)

		mediaRuntime.runtimePreparing = false
		mediaRuntime.runtimeReady = true
		tryCompare(loader, "active", true)
		tryVerify(function() { return loader.item !== null })
		compare(runtimeSurface.visible, false)
	}

	function test_direct_media_plays_in_the_card_and_keeps_popout_optional() {
		const card = previewLoader.item
		card.preview = {
			"state": "ready", "title": "Direct clip",
			"url": "https://provider.example/watch/direct-clip",
			"openLabel": "Open media source",
			"mediaUrl": "data:video/mp4;base64,AAAA", "mediaMime": "video/mp4",
			"thumbnailUrl": "image://mumble/direct-media-poster"
		}
		card.previewIdentity = "message:direct-inline"
		card.mediaSessionId = "message:direct-inline"
		const directButton = findChild(card, "previewDirectMediaButton")
		const inlineButton = findChild(card, "previewPlayButton")
		const poster = findChild(card, "previewEmbedPoster")
		const compactVisual = findChild(card, "previewCompactVisual")
		const popoutButton = findChild(card, "previewPopoutButton")
		const originalButton = findChild(card, "previewOpenOriginalButton")
		const cardOpenSurface = findChild(card, "previewCardOpenSurface")
		const overflowButton = findChild(card, "previewOverflowButton")
		verify(directButton !== null && originalButton !== null
			&& cardOpenSurface !== null && overflowButton !== null)
		verify(!directButton.visible)
		verify(!cardOpenSurface.enabled && !cardOpenSurface.visible)
		verify(poster !== null)
		verify(compactVisual !== null && !compactVisual.visible)
		verify(card.inlineMediaStageVisible)
		compare(card.inlineMediaStageWidth, card.width)
		tryCompare(poster, "status", Image.Ready)
		verify(poster.visible)
		verify(inlineButton !== null && inlineButton.visible)
		inlineButton.clicked()
		compare(directMediaSpy.count, 1)
		compare(inlinePlaySpy.count, 0)
		compare(popoutDirectMediaSpy.count, 0)
		compare(card.restoreInlinePlaybackFocus, true)

		card.renderActive = false
		card.mediaSessionController = inlineSession
		inlineSession.sessionId = card.mediaSessionId
		inlineSession.provider = "direct"
		inlineSession.url = "data:video/mp4;base64,AAAA"
		inlineSession.detached = false
		inlineSession.active = true
		tryCompare(card, "directInlinePlaybackActive", true)
		tryCompare(directButton, "visible", false)
		const panel = findChild(card, "previewEmbedMediaPanel")
		tryCompare(panel, "visible", true)
		compare(panel.width, card.width)
		verify(panel.height > card.width * 9 / 16)
		compare(findChild(card, "previewCompactImage").visible, false)
		compare(findChild(card, "previewInlineMediaLoader").active, false)

		overflowButton.clicked()
		tryCompare(popoutButton, "visible", true)
		tryCompare(originalButton, "visible", true)
		popoutButton.triggered()
		compare(popoutDirectMediaSpy.count, 1)
		compare(popoutPlaySpy.count, 0)
		findChild(card, "previewOverflowMenu").close()
	}

	function test_platform_capability_hides_and_blocks_detached_media() {
		const card = previewLoader.item
		card.preview = {
			"state": "ready", "title": "Inline-only clip",
			"url": "https://provider.example/watch/inline-only",
			"mediaUrl": "data:video/mp4;base64,AAAA", "mediaMime": "video/mp4"
		}
		card.previewIdentity = "message:inline-only"
		card.mediaSessionId = "message:inline-only"
		card.mediaSessionController = inlineSession
		inlineSession.detachedPlaybackSupported = false
		wait(0)

		const popoutButton = findChild(card, "previewPopoutButton")
		verify(popoutButton !== null)
		verify(card.localPlaybackSupported)
		verify(!card.hasPopoutAction)
		verify(!popoutButton.visible)
		card.requestCurrentDirectMediaPopout()
		compare(popoutDirectMediaSpy.count, 0)
	}

	function test_stalled_real_poster_retries_before_requesting_hydration() {
		const card = previewLoader.item
		card.preview = {
			"state": "ready", "title": "Real Instagram poster",
			"url": "https://www.instagram.com/p/real/",
			"embedUrl": "https://www.instagram.com/p/real/embed/",
			"embedKind": "instagram",
			"thumbnailUrl": "image://mumble/instagram-real-poster?g=10"
		}
		card.previewIdentity = "message:instagram-real-poster"
		wait(0)

		compare(card.effectiveEmbedPosterSource,
			"image://mumble/instagram-real-poster?g=10")
		verify(card.recoverStalledEmbedPoster(Image.Loading))
		compare(card.embedPosterRetryAttempt, 1)
		compare(card.effectiveEmbedPosterSource,
			"image://mumble/instagram-real-poster?g=10&qmlRetry=1")
		verify(card.recoverStalledEmbedPoster(Image.Loading))
		compare(card.embedPosterRetryAttempt, 2)
		compare(card.effectiveEmbedPosterSource,
			"image://mumble/instagram-real-poster?g=10&qmlRetry=2")
		verify(!card.recoverStalledEmbedPoster(Image.Loading))
		compare(imageRefreshSpy.count, 1)
		verify(card.imageRefreshQueued)
		verify(card.embedPosterFallbackActive)
		compare(card.effectiveEmbedPosterSource, "")
		verify(card.embedPosterUnavailable)
		verify(!card.inlineMediaStageVisible)
		verify(findChild(card, "previewOpenButton").visible)

		// A backend hydration response may legitimately keep the same generation
		// when the managed source is still registered. Give that settled source
		// one bounded new QML request instead of permanently retaining local
		// fallback state.
		card.preview = {
			"state": "ready", "title": "Real Instagram poster",
			"url": "https://www.instagram.com/p/real/",
			"embedUrl": "https://www.instagram.com/p/real/embed/",
			"embedKind": "instagram",
			"thumbnailUrl": "image://mumble/instagram-real-poster?g=10"
		}
		tryVerify(function() { return !card.embedPosterFallbackActive })
		compare(card.embedPosterHydrationRetryCount, 1)
		compare(card.effectiveEmbedPosterSource,
			"image://mumble/instagram-real-poster?g=10")
		verify(card.inlineMediaStageVisible)

		card.preview = {
			"state": "ready", "title": "Refreshed Instagram poster",
			"url": "https://www.instagram.com/p/real/",
			"embedUrl": "https://www.instagram.com/p/real/embed/",
			"embedKind": "instagram",
			"thumbnailUrl": "image://mumble/instagram-real-poster?g=11"
		}
		wait(0)
		verify(!card.embedPosterFallbackActive)
		compare(card.effectiveEmbedPosterSource,
			"image://mumble/instagram-real-poster?g=11")
	}

	function test_video_backed_animation_keeps_mp4_transport_but_uses_view_semantics() {
		const card = previewLoader.item
		card.preview = {
			"state": "ready",
			"title": "Slap me with the money",
			"url": "https://www.reddit.com/r/animegifs/comments/g91mkj/slap_me_with_the_money_kon/",
			"openLabel": "Open on Reddit",
			"mediaUrl": "https://v.redd.it/p91cxpzry9v41/DASH_1080?source=fallback",
			"mediaMime": "video/mp4",
			"mediaAudioUrl": "https://v.redd.it/p91cxpzry9v41/DASH_AUDIO_128.mp4",
			"mediaAudioMime": "audio/mp4",
			"metadata": {
				"previewProvider": "reddit",
				"contentBranch": "animated-gif-video-backed",
				"mediaPresentation": "animated-image"
			}
		}
		card.previewIdentity = "message:reddit-gif"
		card.mediaSessionId = "message:reddit-gif"
		wait(0)

		const inlineButton = findChild(card, "previewPlayButton")
		const popoutButton = findChild(card, "previewPopoutButton")
		verify(inlineButton !== null && popoutButton !== null)
		compare(card.contentBranch, "animated-gif-video-backed")
		compare(card.mediaPresentation, "animated-image")
		verify(card.animatedImagePresentation)
		verify(card.hasDirectMedia)
		verify(card.localPlaybackSupported)
		verify(!card.inlineActionUsesPlaybackSemantics)
		compare(card.inlineActionLabel, "View here")
		compare(card.inlineActionIconName, "eye")
		verify(!card.sharedPlaybackSupported)
		verify(!card.hasPopoutAction)
		verify(!popoutButton.visible)
		tryCompare(inlineButton, "visible", true)
		compare(inlineButton.text, "View here")

		inlineButton.clicked()
		compare(directMediaSpy.count, 1)
		compare(directMediaSpy.signalArguments[0][0],
			"https://v.redd.it/p91cxpzry9v41/DASH_1080?source=fallback")
		compare(directMediaSpy.signalArguments[0][1], "video/mp4")
		compare(directMediaSpy.signalArguments[0][2], "")
		compare(directMediaSpy.signalArguments[0][3], "")
		compare(watchTogetherSpy.count, 0)
		compare(popoutDirectMediaSpy.count, 0)
	}

	function test_tiktok_photo_is_a_provider_post_card_without_player_claims() {
		const card = previewLoader.item
		card.preview = {
			"state": "ready",
			"title": "TikTok photo post",
			"description": "Photo post",
			"url": "https://www.tiktok.com/@contextify0/photo/7626953410033585428",
			"openLabel": "Open on TikTok",
			"metadata": {
				"provider": "tiktok",
				"previewProvider": "tiktok",
				"contentBranch": "photo-carousel",
				"mediaPresentation": "provider-post-card"
			}
		}
		card.previewIdentity = "message:tiktok-photo"
		wait(0)

		const openButton = findChild(card, "previewOpenButton")
		const inlineButton = findChild(card, "previewPlayButton")
		const popoutButton = findChild(card, "previewPopoutButton")
		verify(openButton !== null && inlineButton !== null && popoutButton !== null)
		compare(card.contentBranch, "photo-carousel")
		compare(card.mediaPresentation, "provider-post-card")
		verify(card.providerPostPresentation)
		verify(!card.hasEmbedPreview)
		verify(!card.hasDirectMedia)
		verify(!card.localPlaybackSupported)
		verify(!card.inlineMediaStageVisible)
		verify(!card.inlineActionUsesPlaybackSemantics)
		verify(!card.hasPopoutAction)
		verify(!card.sharedPlaybackSupported)
		verify(!inlineButton.visible)
		verify(!popoutButton.visible)
		tryCompare(openButton, "visible", true)
		compare(openButton.text, "Open on TikTok")

		openButton.clicked()
		compare(externalOpenSpy.count, 1)
		compare(externalOpenSpy.signalArguments[0][0],
			"https://www.tiktok.com/@contextify0/photo/7626953410033585428")
		compare(directMediaSpy.count, 0)
		compare(inlinePlaySpy.count, 0)
	}

	function test_embed_fallback_deduplicates_host_title_and_prefers_provider_name() {
		const card = previewLoader.item
		card.preview = {
			"state": "ready", "title": "www.youtube.com", "subtitle": "www.youtube.com",
			"host": "www.youtube.com", "url": "https://youtu.be/test",
			"embedUrl": "https://www.youtube.com/embed/test", "embedKind": "youtube"
		}
		card.previewIdentity = "message:provider-fallback"
		compare(card.displayTitle, "YouTube video")
		compare(card.metadataLine, "www.youtube.com")
		compare(card.providerLabel, "YouTube")
	}

	function test_active_social_provider_keeps_mumble_details_and_stable_geometry() {
		const card = previewLoader.item
		card.preview = {
			"state": "ready",
			"title": "A provider-owned post",
			"subtitle": "@creator",
			"description": "The native summary should return after playback closes.",
			"url": "https://www.instagram.com/reel/123/",
			"embedUrl": "https://www.instagram.com/reel/123/embed/",
			"embedKind": "instagram",
			"metadata": {
				"previewProvider": "instagram",
				"instagramMediaKind": "reel",
				"instagramAuthor": "@creator"
			}
		}
		card.previewIdentity = "message:social-provider-owner"
		card.mediaSessionId = "message:social-provider-owner"
		card.mediaSessionController = inlineSession
		inlineSession.sessionId = card.mediaSessionId
		inlineSession.provider = "instagram"
		inlineSession.playbackControllable = false
		inlineSession.detached = false
		inlineSession.active = true
		wait(0)

		const details = findChild(card, "providerDetails")
		verify(details !== null)
		compare(details.providerToken, "instagram")
		compare(details.presentation, "identity")
		verify(!card.inlineProviderOwnsDetails)
		verify(details.visible)
		verify(details.height > 0)
		compare(card.inlineControlsEstimate, 0)
		const activeCardHeight = card.implicitHeight
		const activeDetailsHeight = details.height

		inlineSession.active = false
		inlineSession.detached = true
		wait(0)
		verify(!card.inlineProviderOwnsDetails)
		verify(details.visible)
		compare(details.height, activeDetailsHeight)
		compare(card.implicitHeight, activeCardHeight)
	}

	function test_provider_post_keeps_native_transition_until_adapter_is_ready() {
		const card = previewLoader.item
		card.preview = {
			"state": "ready",
			"title": "Stable Instagram transition",
			"url": "https://www.instagram.com/reel/stable/",
			"embedUrl": "https://www.instagram.com/reel/stable/embed/",
			"embedKind": "instagram",
			"embedAspect": "short",
			"thumbnailUrl": "image://mumble/preview/stable",
			"metadata": {
				"provider": "instagram",
				"instagramMediaKind": "reel",
				"mediaPresentation": "provider-post-card"
			}
		}
		card.previewIdentity = "message:instagram-stable-transition"
		card.mediaSessionId = "message:instagram-stable-transition"
		card.mediaSessionController = inlineSession
		card.visualMediaFixtureMode = "loading"
		inlineSession.sessionId = card.mediaSessionId
		inlineSession.provider = "instagram"
		inlineSession.detached = false
		inlineSession.active = true
		wait(0)

		const loader = findChild(card, "previewInlineMediaLoader")
		const details = findChild(card, "providerDetails")
		verify(loader !== null && details !== null)
		verify(card.inlineAdapterPending)
		tryCompare(loader, "opacity", 0)
		verify(details.visible)
		const loadingHeight = card.implicitHeight

		loader.item.visualFixtureMode = "active"
		tryVerify(function() { return card.inlineAdapterReady })
		tryCompare(loader, "opacity", 1)
		compare(card.implicitHeight, loadingHeight)
		verify(details.visible)
	}

	function test_x_uses_the_common_mumble_social_post_card() {
		const card = previewLoader.item
		card.preview = {
			"state": "ready",
			"title": "Shared card state keeps scrolling stable.",
			"subtitle": "Ada",
			"description": "@ada",
			"url": "https://x.com/ada/status/123",
			"metadata": {
				"provider": "x",
				"xDisplayName": "Ada",
				"xHandle": "@ada",
				"xVerified": true
			}
		}
		card.previewIdentity = "message:x-common-social-card"
		wait(0)

		const details = findChild(card, "providerDetails")
		const socialPost = findChild(card, "providerSocialPost")
		const xCard = findChild(card, "providerXCard")
		const genericHeader = findChild(card, "previewGenericHeader")
		verify(details !== null && socialPost !== null && genericHeader !== null)
		compare(details.providerToken, "x")
		compare(details.xPresentation, true)
		compare(details.socialBespokePresentation, false)
		compare(details.genericSocialPostPresentation, true)
		compare(details.presentation, "socialPost")
		verify(socialPost.visible)
		verify(xCard === null || !xCard.visible)
		compare(genericHeader.visible, false)
		compare(findChild(card, "providerSocialAuthor").text, "Ada")
		compare(findChild(card, "providerSocialPostText").text,
			"Shared card state keeps scrolling stable.")
	}

	function test_typed_embed_document_keeps_full_source_and_reply_context_visible() {
		const card = previewLoader.item
		const sourceUrl = "https://x.com/historyinmemes/status/2058971862265151767"
		card.preview = {
			"state": "ready",
			"title": "The current post",
			"description": "@historyinmemes",
			"url": sourceUrl,
			"metadata": { "provider": "x" },
			"document": {
				"schemaVersion": 1,
				"commonPresentation": true,
				"presentation": "social",
				"provider": { "id": "x", "label": "X", "host": "x.com" },
				"source": { "url": sourceUrl, "displayUrl": sourceUrl },
				"content": {
					"type": "social-post",
					"description": "The current post",
					"publishedAt": "2026-07-20T10:20:00Z",
					"author": { "name": "History in Memes", "handle": "@historyinmemes" }
				},
				"thread": {
					"kind": "conversation",
					"items": [
						{
							"role": "reply-context",
							"authorName": "Source",
							"authorHandle": "@source",
							"text": "The parent post",
							"url": "https://x.com/source/status/2058970000000000000"
						},
						{
							"role": "quote",
							"authorName": "Quoted",
							"authorHandle": "@quoted",
							"text": "The quoted post",
							"url": "https://x.com/quoted/status/2058960000000000000"
						}
					]
				},
				"facts": [
					{ "key": "likes", "label": "Likes", "value": 1234 }
				],
				"media": [],
				"playback": { "mode": "none", "provider": "x" },
				"state": { "status": "ready" }
			}
		}
		card.previewIdentity = "message:typed-x-document"
		wait(0)

		verify(card.typedDocumentActive)
		const sourceLink = findChild(card, "previewSourceLink")
		const sourceLabel = findChild(card, "embedSourceUrl")
		const body = findChild(card, "previewDocumentBody")
		const genericHeader = findChild(card, "previewGenericHeader")
		const details = findChild(card, "providerDetails")
		verify(sourceLink !== null && sourceLink.visible)
		verify(sourceLabel !== null && sourceLabel.visible)
		compare(sourceLabel.text, sourceUrl)
		compare(sourceLabel.elide, Text.ElideNone)
		compare(sourceLink.Accessible.description, sourceUrl)
		verify(body !== null && body.visible)
		compare(genericHeader.visible, false)
		compare(details.visible, false)
		verify(findChild(card, "embedThreadContextItem_0") !== null)
		verify(findChild(card, "embedThreadContextItem_1") !== null)
		compare(findChild(card, "embedThreadContextUrl_0").text,
			"https://x.com/source/status/2058970000000000000")

		sourceLink.clicked()
		compare(externalOpenSpy.count, 1)
		compare(externalOpenSpy.signalArguments[0][0], sourceUrl)
		const contextUrl = findChild(card, "embedThreadContextUrl_1")
		contextUrl.clicked()
		compare(externalOpenSpy.count, 2)
		compare(externalOpenSpy.signalArguments[1][0],
			"https://x.com/quoted/status/2058960000000000000")
	}

	function test_typed_embed_document_uses_one_mixed_gallery_and_common_facts() {
		const card = previewLoader.item
		const sourceUrl = "https://store.steampowered.com/app/730/CounterStrike_2/"
		card.preview = {
			"state": "ready",
			"title": "Counter-Strike 2",
			"description": "For over two decades, Counter-Strike has offered an elite competitive experience.",
			"url": sourceUrl,
			"metadata": {
				"provider": "steam",
				"steamAppName": "Counter-Strike 2",
				"steamDeveloper": "Valve"
			},
			"document": {
				"schemaVersion": 1,
				"commonPresentation": true,
				"presentation": "game",
				"provider": {
					"id": "steam",
					"label": "Steam",
					"host": "store.steampowered.com"
				},
				"source": { "url": sourceUrl, "displayUrl": sourceUrl },
				"content": {
					"type": "game",
					"title": "Counter-Strike 2",
					"description": "For over two decades, Counter-Strike has offered an elite competitive experience."
				},
				"thread": {},
				"facts": [
					{ "key": "price", "label": "Price", "value": "Free to Play" },
					{ "key": "developer", "label": "Developer", "value": "Valve" },
					{ "key": "platforms", "label": "Platforms", "value": "Windows, Linux" }
				],
				"media": [
					{
						"id": "media:0",
						"kind": "image",
						"url": "image://mumble/steam-cs2-shot-one?g=71",
						"thumbnailUrl": "image://mumble/steam-cs2-thumb-one?g=71",
						"title": "Counter-Strike 2 screenshot one",
						"directPlayable": true
					},
					{
						"id": "media:1",
						"kind": "image",
						"url": "image://mumble/steam-cs2-shot-two?g=72",
						"thumbnailUrl": "image://mumble/steam-cs2-thumb-two?g=72",
						"title": "Counter-Strike 2 screenshot two",
						"directPlayable": true
					}
				],
				"playback": { "mode": "none", "provider": "steam" },
				"state": { "status": "ready" }
			}
		}
		card.previewIdentity = "message:typed-steam-document"
		card.userExpanded = false
		wait(0)

		const gallery = findChild(card, "previewDocumentMediaGallery")
		const sourceLabel = findChild(card, "embedSourceUrl")
		const details = findChild(card, "providerDetails")
		verify(gallery !== null && gallery.visible)
		compare(gallery.mediaItems.length, 2)
		compare(card.mediaItems[1].url, "image://mumble/steam-cs2-shot-two?g=72")
		compare(sourceLabel.text, sourceUrl)
		compare(details.visible, false)
		const first = findChild(card, "embedDocumentMediaThumbnail_0")
		const second = findChild(card, "embedDocumentMediaThumbnail_1")
		verify(first !== null && second !== null)
		compare(card.selectedMediaIndex, 0)
		second.clicked()
		compare(card.selectedMediaIndex, 1)
		gallery.mediaRequested(1)
		compare(imageOpenSpy.count, 1)
		compare(imageOpenSpy.signalArguments[0][0],
			"image://mumble/steam-cs2-shot-two?g=72")
		const title = findChild(card, "embedDocumentTitle")
		const description = findChild(card, "embedDocumentDescription")
		compare(title.text, "Counter-Strike 2")
		verify(description.text.indexOf("elite competitive experience") >= 0)
		card.userExpanded = true
		wait(0)
		compare(findChild(card, "previewExpandedMediaSlot").visible, false)
	}

	function test_direct_reddit_media_preserves_provider_identity_for_inline_player() {
		const card = previewLoader.item
		card.preview = {
			"state": "ready",
			"title": "Reddit video",
			"url": "https://www.reddit.com/r/cats/comments/123/video",
			"mediaUrl": "https://v.redd.it/123/DASH_720.mp4",
			"mediaMime": "video/mp4",
			"metadata": {
				"previewProvider": "reddit",
				"providerName": "Reddit"
			}
		}
		card.previewIdentity = "message:reddit-direct-identity"
		card.mediaSessionId = "message:reddit-direct-identity"
		card.mediaSessionController = inlineSession
		inlineSession.sessionId = card.mediaSessionId
		inlineSession.provider = "direct"
		inlineSession.detached = false
		inlineSession.active = true
		wait(0)

		verify(card.directInlinePlaybackActive)
		compare(card.inlinePresentationProvider, "reddit")
	}

	function test_generic_link_uses_host_identity_mark_and_deduplicates_placeholder_copy() {
		const card = previewLoader.item
		card.preview = {
			"state": "ready", "title": "example.com", "subtitle": "example.com",
			"description": "example.com", "host": "news.example.com",
			"url": "https://news.example.com/release"
		}
		card.previewIdentity = "message:generic-provider-mark"
		wait(0)

		const compactVisual = findChild(card, "previewCompactVisual")
		const providerMark = findChild(card, "previewGenericProviderMark")
		const description = findChild(card, "previewDescription")
		verify(compactVisual !== null && providerMark !== null && description !== null)
		compare(card.displayTitle, "example.com")
		compare(card.metadataLine, "news.example.com")
		compare(card.providerLabel, "news.example.com")
		compare(card.genericProviderMark, "EX")
		compare(card.displayDescription, "")
		compare(card.hasExpandedDescription, false)
		tryCompare(compactVisual, "visible", true)
		tryCompare(providerMark, "visible", true)
		compare(providerMark.presentation, "mark")
		compare(providerMark.badgeText, "EX")
		compare(description.visible, false)
	}

	function test_provider_embed_aspect_fallbacks_preserve_production_shapes() {
		const card = previewLoader.item
		const cases = [
			{ "provider": "tiktok", "metadata": {}, "aspect": "short" },
			{ "provider": "instagram", "metadata": { "instagramMediaKind": "reel" },
			  "aspect": "short" },
			{ "provider": "instagram", "metadata": { "instagramMediaKind": "post" },
			  "aspect": "square" },
			{ "provider": "instagram", "metadata": { "instagramMediaKind": "tv" },
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

	function test_provider_identity_and_state_never_cover_the_media_surface() {
		const card = previewLoader.item
		card.preview = {
			"state": "ready", "title": "Mumble Dev",
			"embedUrl": "https://player.twitch.tv/?channel=mumbledev", "embedKind": "twitch",
			"metadata": { "previewProvider": "twitch", "providerName": "Twitch",
				"twitchLiveState": "live", "twitchBadge": "Live",
				"twitchPlaybackNote": "Twitch may require playback confirmation." }
		}
		card.previewIdentity = "message:twitch-provider-badges"
		previewLoader.width = card.targetCardWidth
		const providerBadge = findChild(card, "previewEmbedProviderBadge")
		const stateBadge = findChild(card, "previewEmbedProviderState")
		const posterScrim = findChild(card, "previewTwitchPosterScrim")
		const details = findChild(card, "providerDetails")
		verify(providerBadge === null && stateBadge === null && posterScrim === null)
		verify(findChild(card, "previewTwitchPosterCopy") === null)
		verify(details !== null)
		compare(details.providerToken, "twitch")
		compare(details.providerStateLabel, "Live")
		compare(card.normalizedEmbedAspect, "twitch")
		verify(card.embedMediaWidth <= 520)
		verify(Math.abs(card.embedMediaWidth / card.embedMediaHeight - 4 / 3) < 0.01)
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
		card.hoverEffectsEnabled = false
		compare(card.animationDuration, 0)
		compare(card.effectiveHovered, false)
		const embedBusy = findChild(card, "previewEmbedBusyIndicator")
		const compactBusy = findChild(card, "previewCompactBusyIndicator")
		compare(embedBusy.animated, false)
		compare(compactBusy.animated, false)
		compare(embedBusy.rotation, 0)
		compare(compactBusy.rotation, 0)
		card.animationsEnabled = true
		card.hoverEffectsEnabled = true
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
		openButton.forceActiveFocus(Qt.TabFocusReason)
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

		card.preview = {
			"state": "ready", "title": "youtube",
			"embedUrl": "https://www.youtube.com/embed/sync-video", "embedKind": "youtube"
		}
		card.previewIdentity = "message:sync:youtube"
		verify(card.localPlaybackSupported)
		verify(card.sharedPlaybackSupported)
		verify(card.watchTogetherSupported)
		overflowButton.clicked()
		tryCompare(watchAction, "visible", true)
		overflowMenu.close()

		card.preview = {
			"state": "ready", "title": "Generic direct embed",
			"embedUrl": "https://example.com/embed/direct", "embedKind": "direct"
		}
		card.previewIdentity = "message:provider-controls:direct"
		verify(!card.hasEmbedPreview)
		verify(!card.localPlaybackSupported)
		verify(!card.sharedPlaybackSupported)
		verify(!card.watchTogetherSupported)
		compare(findChild(card, "previewPlayButton").visible, false)
		compare(watchAction.visible, false)

		for (const provider of [ "twitch", "streamable", "vimeo", "dailymotion" ]) {
			card.preview = {
				"state": "ready", "title": provider,
				"embedUrl": "https://example.com/embed/" + provider, "embedKind": provider
			}
			card.previewIdentity = "message:provider-controls:" + provider
			verify(card.localPlaybackSupported)
			verify(!card.sharedPlaybackSupported)
			verify(!card.watchTogetherSupported)
			overflowButton.clicked()
			compare(watchAction.visible, false)
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

		card.preview = {
			"state": "ready", "title": "Spoofed YouTube provider",
			"embedUrl": "https://example.com/embed/not-youtube", "embedKind": "youtube"
		}
		card.previewIdentity = "message:sync:spoofed-youtube"
		verify(card.localPlaybackSupported)
		verify(!card.sharedPlaybackSupported)
		verify(!card.watchTogetherSupported)
		overflowButton.clicked()
		compare(watchAction.visible, false)
		overflowMenu.close()
	}

	function test_inline_playback_closes_without_implicitly_opening_a_window() {
		const card = previewLoader.item
		card.renderActive = false
		card.mediaSessionId = "message:inline"
		card.mediaSessionController = inlineSession
		inlineSession.sessionId = "message:inline"
		inlineSession.detached = false
		inlineSession.active = true
		verify(card.inlinePlaybackActive)
		// Leaving the render window must never create a popup the user did not ask
		// for. Close the card-local renderer; explicit Pop out remains available.
		card.renderActive = true
		card.renderActive = false
		tryCompare(inlineSession, "closeCalls", 1)
		compare(inlineSession.detachCalls, 0)
		verify(inlineSession.detached)
		verify(!inlineSession.active)
		compare(card.inlinePlaybackActive, false)
	}

	function test_visual_fixture_replacement_leaves_media_reset_to_fixture_controller() {
		const card = previewLoader.item
		card.renderActive = false
		card.visualMediaFixtureMode = "loading"
		card.mediaSessionId = "message:fixture-inline"
		card.mediaSessionController = inlineSession
		inlineSession.sessionId = card.mediaSessionId
		inlineSession.detached = false
		inlineSession.active = true
		verify(card.inlinePlaybackActive)

		card.renderActive = true
		card.renderActive = false
		wait(0)
		compare(inlineSession.closeCalls, 0)
		compare(inlineSession.detachCalls, 0)
		verify(inlineSession.active)
		verify(!inlineSession.detached)
	}

	function test_virtualizing_shared_host_detaches_without_pausing_the_room() {
		const card = previewLoader.item
		card.renderActive = false
		card.mediaSessionId = "message:shared-host"
		card.mediaSessionController = inlineSession
		inlineSession.sessionId = card.mediaSessionId
		inlineSession.detached = false
		inlineSession.sharedHost = true
		inlineSession.state = "playing"
		inlineSession.active = true
		verify(card.inlinePlaybackActive)

		card.renderActive = true
		card.renderActive = false
		tryCompare(inlineSession, "detachCalls", 1)
		compare(inlineSession.pauseCalls, 0)
		compare(inlineSession.closeCalls, 0)
		verify(inlineSession.active)
		verify(inlineSession.detached)
		compare(inlineSession.state, "playing")
		compare(card.inlinePlaybackActive, false)
	}

	function test_missing_inline_player_component_reports_typed_error_and_offers_browser_fallback() {
		const card = previewLoader.item
		card.preview = {
			"state": "ready",
			"title": "Provider clip",
			"url": "https://www.youtube.com/watch?v=component-fallback",
			"embedUrl": "https://www.youtube.com/embed/component-fallback",
			"embedKind": "youtube"
		}
		card.previewIdentity = "message:component-fallback"
		card.mediaSessionId = "message:component-fallback"
		card.mediaSessionController = inlineSession
		card.inlinePlayerComponentUrl = Qt.resolvedUrl("DefinitelyMissingInlineMediaPlayer.qml")
		inlineSession.sessionId = "message:component-fallback"
		inlineSession.url = "https://www.youtube.com/embed/component-fallback"
		inlineSession.detached = false
		card.prepareInlinePlaybackFocus()
		inlineSession.active = true

		const loader = findChild(card, "previewInlineMediaLoader")
		const failure = findChild(card, "previewInlineMediaComponentFailure")
		const retryButton = findChild(card, "previewInlineMediaComponentRetryButton")
		const externalButton = findChild(card, "previewInlineMediaComponentExternalButton")
		const closeButton = findChild(card, "previewInlineMediaComponentCloseButton")
		verify(loader !== null && failure !== null && retryButton !== null
			&& externalButton !== null && closeButton !== null)
		tryCompare(loader, "status", Loader.Error)
		tryCompare(inlineSession, "typedErrorCalls", 1)
		compare(inlineSession.errorCode, "renderer-component-unavailable")
		verify(inlineSession.error.indexOf("unavailable") >= 0)
		verify(failure.visible)
		tryCompare(retryButton, "activeFocus", true)

		externalButton.clicked()
		compare(externalOpenSpy.count, 1)
		compare(externalOpenSpy.signalArguments[0][0],
			"https://www.youtube.com/watch?v=component-fallback")
		retryButton.clicked()
		compare(inlineSession.retryCalls, 1)
		tryVerify(function() { return inlineSession.typedErrorCalls >= 2 })
		closeButton.forceActiveFocus()
		verify(closeButton.activeFocus)
		verify(card.inlineFocusRequestMatchesCurrentSession())
		closeButton.clicked()
		compare(inlineSession.closeCalls, 1)
		verify(!inlineSession.active)
		tryCompare(card, "inlinePlaybackActive", false)
		tryCompare(findChild(card, "previewPlayButton"), "activeFocus", true)
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
		const inlineImage = findChild(body, "richMessageInlineImage_0")
		const imageState = findChild(body, "richMessageImageState_0")
		verify(imageState !== null && inlineImage !== null)
		compare(body.imageSurfaceColor, Theme.embedSurface)
		compare(body.imageBorderColor, Theme.embedBorder)
		compare(body.keyboardLinks.length, 0)
		compare(body.activeFocusOnTab, false)
		compare(body.Accessible.ignored, true)
		compare(imageCard.Accessible.role, Accessible.Link)
		compare(imageCard.Accessible.name, "Embedded release diagram")
		verify(imageCard.Accessible.description.indexOf("Press Enter") >= 0)
		compare(inlineImage.Accessible.ignored, true)
		verify(body.plainText.indexOf("Embedded release diagram") >= 0)

		tryVerify(function() { return previewLoader.y >= bodyLoader.y + imageCard.height })
		// The previous test can leave the synthetic pointer at the same scene
		// coordinate. Move outside first so a newly materialized MouseArea receives
		// a deterministic hover-enter edge on every full-suite run.
		mouseMove(testCase, testCase.width - 1, testCase.height - 1)
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

	function test_structured_z_image_failure_is_exposed_on_the_image_semantic_owner() {
		const body = bodyLoader.item
		body.segments = [{
			"kind": "image",
			"source": "qrc:/qml-shell/definitely-not-present-mumble-rich-image.png",
			"alt": "Unavailable architecture image",
			"width": 320,
			"height": 180
		}]
		tryVerify(function() { return findChild(body, "richMessageInlineImage_0") !== null })
		const imageCard = findChild(body, "richMessageImageCard_0")
		const inlineImage = findChild(body, "richMessageInlineImage_0")
		const errorLabel = findChild(body, "richMessageImageError_0")
		verify(imageCard !== null && inlineImage !== null && errorLabel !== null)
		tryCompare(inlineImage, "status", Image.Error)
		tryCompare(errorLabel, "visible", true)
		compare(imageCard.Accessible.role, Accessible.Graphic)
		verify(imageCard.Accessible.description.indexOf("Image unavailable") >= 0)
		mouseMove(testCase, 1, 1)
	}

	function test_structured_static_text_accessibility_normalizes_boundary_whitespace() {
		const body = bodyLoader.item
		body.segments = [
			{ "text": "  First line\nsecond line  \n" },
			{ "kind": "image", "source": "image://mumble/static-text-owner?g=1",
				"alt": "Supporting artwork", "width": 320, "height": 180 }
		]
		tryVerify(function() {
			return findChild(body, "richMessageTextBlock_0") !== null
				&& findChild(body, "richMessageImageCard_1") !== null
		})
		const textTarget = findChild(body, "richMessageTextBlock_0")
		compare(textTarget.Accessible.role, Accessible.StaticText)
		compare(textTarget.Accessible.name, "First line second line")
	}

	function test_structured_text_links_and_linked_image_have_separate_semantic_owners() {
		const body = bodyLoader.item
		body.segments = [
			{ "text": "Documentation", "href": "https://example.com/docs" },
			{ "text": " and " },
			{ "text": "Support", "href": "https://example.com/support" },
			{ "kind": "image", "source": "image://mumble/semantic-owner?g=1",
				"alt": "Architecture diagram", "width": 320, "height": 180,
				"href": "https://example.com/diagram" }
		]
		tryVerify(function() {
			return findChild(body, "richMessageTextBlock_0") !== null
				&& findChild(body, "richMessageImageCard_1") !== null
		})
		const textTarget = findChild(body, "richMessageTextBlock_0")
		const textFocus = findChild(body, "richMessageTextBlockFocus_0")
		const imageCard = findChild(body, "richMessageImageCard_1")
		const inlineImage = findChild(body, "richMessageInlineImage_1")
		verify(textFocus !== null && inlineImage !== null)
		compare(body.Accessible.ignored, true)
		compare(body.activeFocusOnTab, false)
		compare(body.keyboardLinks.length, 2)
		compare(textTarget.keyboardLinkEntries.length, 2)
		compare(textTarget.Accessible.role, Accessible.Link)
		compare(textTarget.Accessible.name, "Documentation and Support")
		verify(textTarget.Accessible.description.indexOf("Documentation") >= 0)
		compare(imageCard.Accessible.role, Accessible.Link)
		compare(imageCard.Accessible.name, "Architecture diagram")
		compare(inlineImage.Accessible.ignored, true)

		textTarget.forceActiveFocus()
		tryCompare(textTarget, "activeFocus", true)
		tryCompare(textFocus, "visible", true)
		keyClick(Qt.Key_Right)
		compare(textTarget.Accessible.name, "Documentation and Support")
		verify(textTarget.Accessible.description.indexOf("Support") >= 0)
		keyClick(Qt.Key_Return)
		compare(linkSpy.count, 1)
		compare(linkSpy.signalArguments[0][0], "https://example.com/support")
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
		compare(card.responsiveViewportWidth, testCase.width)
		compare(card.responsiveCompactCardWidth, 346)
		compare(card.responsiveDefaultCardWidth, 446)
		compare(card.responsiveLargeCardWidth, 533)
		compare(card.targetCardWidth, card.responsiveDefaultCardWidth)
		for (const width of [340, 420, 680, 760, 1082]) {
			previewLoader.width = width
			previewLoader.height = 640
			tryCompare(card, "width", width)
			compare(card.narrowLayout, card.compact || width < 440)
			const flow = findChild(card, "previewActionFlow")
			const primary = findChild(card, "previewEmbedOriginalButton")
			const externalPrimary = findChild(card, "previewOpenButton")
			const overflow = findChild(card, "previewOverflowButton")
			verify(flow !== null)
			verify(primary !== null && primary.visible)
			verify(externalPrimary !== null && !externalPrimary.visible)
			verify(overflow !== null && overflow.visible)
			tryCompare(flow, "width", card.actionAvailableWidth)
			verify(primary.x >= -0.5 && primary.x + primary.width <= overflow.x + 0.5)
			verify(overflow.x + overflow.width <= flow.width + 0.5)
			verify(Math.abs(primary.y - overflow.y) <= 1)
			compare(primary.Accessible.role, Accessible.Link)
			verify(primary.Accessible.name.length > 0)
			verify(overflow.Accessible.name.length > 0)
		}

		card.preview = {
			"state": "ready", "title": "Compact", "url": "https://example.com/compact",
			"previewSize": "compact"
		}
		compare(card.targetCardWidth, card.responsiveCompactCardWidth)
		verify(card.compact)
		verify(card.narrowLayout)
		card.userExpanded = true
		compare(card.targetCardWidth, card.responsiveLargeCardWidth)
		verify(!card.compact)
		card.userExpanded = false
		card.preview = {
			"state": "ready", "title": "Large", "url": "https://example.com/large",
			"previewSize": "large"
		}
		compare(card.targetCardWidth, card.responsiveLargeCardWidth)
		card.preview = {
			"state": "ready", "title": "Unknown", "url": "https://example.com/unknown-size",
			"previewSize": "unexpected"
		}
		compare(card.effectiveSizePreset, "default")
		compare(card.targetCardWidth, card.responsiveDefaultCardWidth)
	}

	function test_media_card_size_presets_are_exclusive_and_accessible() {
		const card = previewLoader.item
		card.preview = {
			"state": "ready", "title": "Resizable video",
			"url": "https://www.youtube.com/watch?v=sizes",
			"embedUrl": "https://www.youtube.com/embed/sizes", "embedKind": "youtube"
		}
		card.previewIdentity = "message:size-presets"
		const overflow = findChild(card, "previewOverflowButton")
		const menu = findChild(card, "previewOverflowMenu")
		const compact = findChild(card, "previewSizeCompactButton")
		const standard = findChild(card, "previewSizeStandardButton")
		const large = findChild(card, "previewSizeLargeButton")
		verify(overflow !== null && menu !== null && compact !== null
			&& standard !== null && large !== null)
		overflow.clicked()
		tryCompare(standard, "visible", true)
		compare(standard.checked, true)
		compare(compact.checked, false)
		compare(large.checked, false)
		for (const action of [ compact, standard, large ]) {
			compare(action.Accessible.role, Accessible.RadioButton)
			verify(action.Accessible.name.length > 0)
		}

		compact.triggered()
		compare(card.effectiveSizePreset, "compact")
		compare(card.targetCardWidth, card.responsiveCompactCardWidth)
		compare(compact.Accessible.checked, true)
		compare(sizePresetSpy.count, 1)
		standard.triggered()
		compare(card.effectiveSizePreset, "default")
		compare(card.targetCardWidth, card.responsiveDefaultCardWidth)
		large.triggered()
		compare(card.effectiveSizePreset, "large")
		compare(card.targetCardWidth, card.responsiveLargeCardWidth)
		compare(sizePresetSpy.count, 3)
		menu.close()

		card.preview = {
			"state": "ready", "title": "Square",
			"embedUrl": "https://www.instagram.com/p/square/embed/",
			"embedKind": "instagram", "embedAspect": "square"
		}
		card.previewIdentity = "message:size-square"
		for (const fixture of [
			{ "preset": "compact", "width": 340 },
			{ "preset": "default", "width": 460 },
			{ "preset": "large", "width": 520 }
		]) {
			card.setSizePreset(fixture.preset)
			compare(card.targetCardWidth, fixture.width)
			previewLoader.width = fixture.width
			tryCompare(card, "width", fixture.width)
			compare(card.embedMediaWidth, card.actionAvailableWidth)
		}

		card.preview = {
			"state": "ready", "title": "Twitch",
			"embedUrl": "https://player.twitch.tv/?channel=mumbledev",
			"embedKind": "twitch", "embedAspect": "twitch"
		}
		card.previewIdentity = "message:size-twitch"
		for (const fixture of [
			{ "preset": "compact", "width": 420 },
			{ "preset": "default", "width": 520 },
			{ "preset": "large", "width": 620 }
		]) {
			card.setSizePreset(fixture.preset)
			compare(card.targetCardWidth, fixture.width)
			previewLoader.width = fixture.width
			tryCompare(card, "width", fixture.width)
			verify(card.embedMediaWidth >= 400)
			verify(Math.abs(card.embedMediaWidth / card.embedMediaHeight - 4 / 3) < 0.01)
		}

		card.preview = {
			"state": "ready", "title": "Short video",
			"embedUrl": "https://www.tiktok.com/player/v1/size-test",
			"embedKind": "tiktok", "embedAspect": "short"
		}
		card.previewIdentity = "message:size-short"
		for (const fixture of [
			{ "preset": "compact", "width": 320, "mediaWidth": 240 },
			{ "preset": "default", "width": 360, "mediaWidth": 280 },
			{ "preset": "large", "width": 420, "mediaWidth": 340 }
		]) {
			card.setSizePreset(fixture.preset)
			previewLoader.width = fixture.width
			tryCompare(card, "width", fixture.width)
			compare(card.embedMediaWidth, fixture.mediaWidth)
			verify(Math.abs(card.embedMediaWidth / card.embedMediaHeight - 9 / 16) < 0.01)
			compare(findChild(card, "previewContent").anchors.topMargin, 16)
		}

		card.preview = {
			"state": "ready", "title": "Audio",
			"embedUrl": "https://w.soundcloud.com/player/?url=size-test",
			"embedKind": "soundcloud", "embedAspect": "audio"
		}
		card.previewIdentity = "message:size-audio"
		let previousHeight = 0
		for (const fixture of [
			{ "preset": "compact", "width": card.responsiveCompactCardWidth },
			{ "preset": "default", "width": card.responsiveDefaultCardWidth },
			{ "preset": "large", "width": card.responsiveLargeCardWidth }
		]) {
			card.setSizePreset(fixture.preset)
			previewLoader.width = fixture.width
			tryCompare(card, "width", fixture.width)
			compare(card.embedMediaWidth, fixture.width)
			verify(card.embedMediaHeight >= previousHeight)
			verify(card.embedMediaHeight <= 352)
			previousHeight = card.embedMediaHeight
		}
	}

	function test_direct_media_keeps_original_provider_action_in_overflow() {
		const card = previewLoader.item
		card.preview = {
			"state": "ready", "title": "Direct clip",
			"url": "https://provider.example/watch/direct-clip",
			"openLabel": "Open media source",
			"mediaUrl": "data:video/mp4;base64,AAAA", "mediaMime": "video/mp4"
		}
		card.previewIdentity = "message:direct-original"
		const directAction = findChild(card, "previewDirectMediaButton")
		const overflow = findChild(card, "previewOverflowButton")
		const menu = findChild(card, "previewOverflowMenu")
		const original = findChild(card, "previewOpenOriginalButton")
		const popout = findChild(card, "previewPopoutButton")
		const inlineAction = findChild(card, "previewPlayButton")
		verify(directAction !== null && !directAction.visible)
		verify(inlineAction !== null && inlineAction.visible)
		compare(inlineAction.text, "Play here")
		verify(overflow !== null && overflow.visible)
		verify(menu !== null && original !== null && popout !== null)
		overflow.clicked()
		tryCompare(original, "visible", true)
		tryCompare(popout, "visible", true)
		compare(popout.text, "Open in separate player")
		compare(original.text, "Open media source")
		compare(original.Accessible.name, "Open media source: Direct clip")
		popout.triggered()
		compare(popoutDirectMediaSpy.count, 1)
		compare(original.Accessible.description, "Open this item on the original provider")
		original.triggered()
		compare(externalOpenSpy.count, 1)
		compare(externalOpenSpy.signalArguments[0][0],
			"https://provider.example/watch/direct-clip")
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
		compare(chip.presentation, "inline")
		tryVerify(function() { return chip.width > 0 && label.width > 0 })
		verify(label.width + 0.5 >= label.implicitWidth,
			"provider label width " + label.width + " clipped implicit width " + label.implicitWidth)
		const details = findChild(card, "providerDetails")
		compare(String(label.color), String(details.providerForeground))
		compare(String(chip.color), String(details.providerAccentSubtle))
		compare(String(chip.border.color), String(details.providerAccentBorder))
	}

	function test_generic_social_previews_route_to_the_single_social_post_owner() {
		const card = previewLoader.item
		const fixtures = [
			{ "provider": "bluesky", "name": "Bluesky", "url": "https://bsky.app/profile/ada/post/1",
			  "author": "Ada (@ada.bsky.social)", "post": "Bounded delegates stay smooth." },
			{ "provider": "mastodon", "name": "Mastodon", "url": "https://social.example/@ada/2",
			  "author": "@ada@social.example", "post": "Native previews keep their identity." },
			{ "provider": "reddit", "name": "Reddit", "url": "https://www.reddit.com/r/mumble/comments/3",
			  "author": "r/mumble", "post": "Modern client preview parity." }
		]

		for (let index = 0; index < fixtures.length; ++index) {
			const fixture = fixtures[index]
			card.preview = {
				"state": "ready", "title": fixture.post, "subtitle": fixture.author,
				"description": fixture.name, "url": fixture.url
			}
			card.previewIdentity = "message:generic-social:" + index
			wait(0)

			const details = findChild(card, "providerDetails")
			const socialPost = findChild(card, "providerSocialPost")
			const genericHeader = findChild(card, "previewGenericHeader")
			const providerChip = findChild(card, "previewProviderChip")
			const providerLabel = findChild(card, "providerSocialIdentityLabel")
			const author = findChild(card, "providerSocialAuthor")
			const post = findChild(card, "providerSocialPostText")

			compare(card.inferredGenericSocialProviderToken, fixture.provider)
			compare(card.providerLabel, fixture.name)
			compare(details.genericSocialPostPresentation, true)
			compare(details.presentation, "socialPost")
			compare(details.ownsHeader, true)
			compare(genericHeader.visible, false)
			compare(providerChip.visible, false)
			verify(socialPost !== null && socialPost.visible)
			compare(providerLabel.text, fixture.name.toUpperCase())
			compare(author.text, fixture.author)
			compare(post.text, fixture.post)
			compare(socialPost.Accessible.description.indexOf(fixture.name), 0)
			compare(socialPost.Accessible.description.indexOf(fixture.name),
				socialPost.Accessible.description.lastIndexOf(fixture.name))
			compare(socialPost.Accessible.description.indexOf(fixture.author),
				socialPost.Accessible.description.lastIndexOf(fixture.author))
			compare(socialPost.Accessible.description.indexOf(fixture.post),
				socialPost.Accessible.description.lastIndexOf(fixture.post))
		}
	}

	function test_game_store_provider_chip_uses_store_name_instead_of_transport_kind() {
		const card = previewLoader.item
		card.preview = {
			"state": "ready", "title": "Hades II", "url": "https://store.steampowered.com/app/1",
			"mediaItems": [{ "kind": "image", "url": "image://mumble/steam-hero?g=1" }],
			"metadata": { "previewProvider": "game-store", "previewKind": "gameStoreProduct",
				"gameStoreProvider": "steam", "gameStoreName": "Steam", "gameStorePrice": "29,99 €" }
		}
		card.previewIdentity = "message:game-store-provider"
		compare(card.providerLabel, "Steam")
		const genericProviderLabel = findChild(card, "previewProviderLabel")
		compare(genericProviderLabel.text, "STEAM")
		const details = findChild(card, "providerDetails")
		compare(details.providerToken, "steam")
		compare(details.providerDisplayName, "Steam")
		compare(details.ownsHeader, true)
		compare(findChild(card, "previewGenericHeader").visible, false)
		compare(genericProviderLabel.visible, false)
		const hero = findChild(card, "providerSteamHeroImage")
		verify(hero !== null)
		tryCompare(hero, "status", Image.Ready)
		verify(hero.visible)
		compare(hero.source.toString(), "image://mumble/steam-hero?g=1")
	}

	function test_steam_gallery_is_visible_and_selectable_without_expanding_the_card() {
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
		card.userExpanded = false
		tryVerify(function() { return !card.expanded && card.implicitHeight > 0 })
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
		const playLabel = findChild(card, "previewSteamPlayLabel_1")
		verify(second !== null)
		verify(playLabel !== null)
		compare(second.Accessible.name, "Steam trailer 2")
		compare(String(playLabel.color), String(Theme.mediaOverlayTextStrong))
		// This case verifies stable gallery selection rather than pointer hit testing.
		// The expanded provider card can place the horizontal rail outside the test
		// window's current viewport, so invoke the semantic button action directly.
		second.clicked()
		compare(card.selectedMediaIndex, 1)
		compare(second.Accessible.selected, true)
		const counter = findChild(card, "providerSteamMediaCounter")
		compare(counter.text, "2 / 3")
		const heroAction = findChild(card, "providerSteamHeroAction")
		verify(heroAction !== null)
		heroAction.clicked()
		tryCompare(directMediaSpy, "count", 1)
		compare(directMediaSpy.signalArguments[0][0], "https://cdn.example.test/trailer.mp4")
	}

	function test_steam_best_price_opens_isthereanydeal_offer() {
		const card = previewLoader.item
		card.preview = {
			"state": "ready", "title": "Hades II", "url": "https://store.steampowered.com/app/1",
			"mediaItems": [{ "kind": "image", "url": "image://mumble/steam-hero?g=1" }],
			"metadata": { "provider": "steam", "previewKind": "gameStoreProduct",
				"steamAppName": "Hades II", "steamPrice": "29,99 €",
				"steamBestPrice": "24,99 EUR", "steamBestShop": "Fanatical",
				"steamBestDealUrl": "https://itad.link/deal/",
				"steamDealComparisonUrl": "https://isthereanydeal.com/game/hades-ii/",
				"steamHistoricalLowPrice": "19,99 EUR", "steamHistoricalLowShop": "Steam" }
		}
		card.previewIdentity = "message:steam-deal"
		const deal = findChild(card, "providerSteamBestDeal")
		verify(deal !== null)
		tryCompare(deal, "visible", true)
		compare(findChild(card, "providerSteamBestDealPrice").text, "24,99 EUR · Fanatical")
		compare(findChild(card, "providerSteamHistoricalLow").text,
			"Historical low: 19,99 EUR at Steam")
		deal.clicked()
		compare(externalOpenSpy.count, 1)
		compare(externalOpenSpy.signalArguments[0][0], "https://itad.link/deal/")
	}

	function test_steam_manifest_only_trailers_keep_managed_posters_and_lazy_media_contract() {
		const card = previewLoader.item
		card.preview = {
			"state": "ready", "title": "Steam manifest trailers",
			"url": "https://store.steampowered.com/app/1",
			"mediaItems": [
				{ "kind": "video", "mime": "application/vnd.apple.mpegurl",
				  "streamKind": "hls",
				  "url": "https://cdn.cloudflare.steamstatic.com/trailer/master.m3u8",
				  "poster": "image://mumble/steam-hls-poster?g=1",
				  "thumbnail": "image://mumble/steam-hls-thumb?g=1", "title": "HLS trailer" },
				{ "kind": "video", "mime": "application/dash+xml",
				  "streamKind": "dash",
				  "url": "https://cdn.cloudflare.steamstatic.com/trailer/manifest.mpd",
				  "poster": "image://mumble/steam-dash-poster?g=1", "title": "DASH trailer" }
			],
			"metadata": { "provider": "steam", "previewKind": "gameStoreProduct" }
		}
		card.previewIdentity = "message:steam-manifest"
		compare(card.mediaItems.length, 2)
		compare(card.currentMedia.mime, "application/vnd.apple.mpegurl")
		compare(card.imageSource, "image://mumble/steam-hls-poster?g=1")
		verify(card.hasDirectMedia)
		card.requestCurrentMedia()
		compare(directMediaSpy.count, 1)
		compare(directMediaSpy.signalArguments[0][0],
			"https://cdn.cloudflare.steamstatic.com/trailer/master.m3u8")
		compare(directMediaSpy.signalArguments[0][1], "application/vnd.apple.mpegurl")

		card.selectedMediaIndex = 1
		compare(card.currentMedia.mime, "application/dash+xml")
		compare(card.imageSource, "image://mumble/steam-dash-poster?g=1")
		card.requestCurrentDirectMediaPopout()
		compare(popoutDirectMediaSpy.count, 1)
		compare(popoutDirectMediaSpy.signalArguments[0][0],
			"https://cdn.cloudflare.steamstatic.com/trailer/manifest.mpd")
		compare(popoutDirectMediaSpy.signalArguments[0][1], "application/dash+xml")
	}

	function test_attachment_tile_is_bounded_below_360_width() {
		attachmentLoader.width = 220
		const gallery = attachmentLoader.item
		tryCompare(gallery, "compactLayout", true)
		const tile = findChild(gallery, "attachment_asset:1")
		verify(tile !== null)
		verify(tile.x >= -0.5 && tile.x + tile.width <= gallery.width + 0.5)
		verify(tile.height >= 96 && tile.height <= 200)
		const action = findChild(gallery, "attachmentAction_asset:1")
		compare(action.Accessible.description, "Attachment 1 of 1")
	}

	function test_attachment_thumbnail_stays_compact_in_regular_chat() {
		attachmentLoader.width = 620
		const gallery = attachmentLoader.item
		gallery.attachments = [{
			"id": "portrait-preview",
			"kind": "image",
			"thumbnailUrl": "image://mumble/portrait-preview?g=1",
			"width": 1440,
			"height": 1800,
			"alt": "Portrait preview"
		}]
		const tile = findChild(gallery, "attachment_portrait-preview")
		const image = findChild(gallery, "attachmentImage_portrait-preview")
		verify(tile !== null && image !== null)
		verify(tile.width <= 400)
		verify(tile.height <= 240)
		verify(image.width < tile.width && image.height < tile.height,
			"The thumbnail should retain a quiet inset instead of touching the card border")
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
		compare(action.enabled, false)
		compare(tile.border.width, 1)
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
		const permanentRetry = findChild(gallery, "attachmentRetry_asset:42")
		verify(failedError !== null && failedAction !== null && failedDownload !== null
			&& permanentRetry !== null)
		tryCompare(failedError, "visible", true)
		tryCompare(failedAction, "enabled", true)
		compare(permanentRetry.visible, false)
		mouseClick(failedDownload)
		compare(attachmentDownloadSpy.count, 1)
		compare(attachmentDownloadSpy.signalArguments[0][0].assetId, "42")

		gallery.attachments = [{
			"id": "asset:42",
			"assetId": "42",
			"kind": "image",
			"mime": "image/png",
			"fileName": "photo.png",
			"state": "error",
			"previewCanRetry": true,
			"previewError": "The preview timed out. You can try again."
		}]
		const retryableError = findChild(gallery, "attachmentError_asset:42")
		const retryPreview = findChild(gallery, "attachmentRetry_asset:42")
		verify(retryableError !== null && retryPreview !== null)
		tryCompare(retryPreview, "visible", true)
		compare(retryableError.text, "The preview timed out. You can try again.")
		compare(retryPreview.Accessible.name, "Retry preview for photo.png")
		mouseClick(retryPreview)
		compare(attachmentRetrySpy.count, 1)
		compare(attachmentRetrySpy.signalArguments[0][0].assetId, "42")

		gallery.attachments = [{
			"id": "inline:legacy",
			"inlineToken": "abcdef0123456789abcdef01",
			"kind": "image",
			"mime": "image/png",
			"fileName": "embedded-image.png",
			"state": "error"
		}]
		const inlineDownload = findChild(gallery, "attachmentDownload_inline:legacy")
		const inlineRetry = findChild(gallery, "attachmentRetry_inline:legacy")
		verify(inlineDownload !== null && inlineRetry !== null)
		tryCompare(inlineDownload, "visible", true)
		compare(inlineRetry.visible, false)
		mouseClick(inlineDownload)
		compare(attachmentDownloadSpy.count, 2)
		compare(attachmentDownloadSpy.signalArguments[1][0].inlineToken,
			"abcdef0123456789abcdef01")
	}

	function test_attachment_gallery_uses_safe_image_fallback_and_distinct_file_actions() {
		const gallery = attachmentLoader.item
		gallery.attachments = [{
			"id": "fallback-image",
			"assetId": "50",
			"kind": "image",
			"url": "image://mumble/fallback-image?g=1",
			"thumbnailUrl": "https://untrusted.example/masked.png",
			"alt": "Fallback image"
		}]
		const imageTile = findChild(gallery, "attachment_fallback-image")
		verify(imageTile !== null)
		compare(imageTile.sourceUrl, "image://mumble/fallback-image?g=1")
		const imageAction = findChild(gallery, "attachmentAction_fallback-image")
		compare(imageAction.Accessible.name, "Fallback image")
		compare(imageAction.Accessible.description, "Attachment 1 of 1")

		gallery.attachments = [{
			"id": "asset:pdf",
			"assetId": "51",
			"kind": "document",
			"mime": "application/pdf",
			"fileName": "quarterly.pdf",
			"byteSize": 8192
		}]
		const pdfAction = findChild(gallery, "attachmentAction_asset:pdf")
		const pdfSave = findChild(gallery, "attachmentDownload_asset:pdf")
		verify(pdfAction !== null && pdfSave !== null)
		compare(pdfAction.Accessible.name, "Open quarterly.pdf")
		tryCompare(pdfSave, "visible", true)
		mouseClick(pdfAction)
		compare(attachmentSpy.count, 1)
		compare(attachmentSpy.signalArguments[0][0].assetId, "51")
		mouseClick(pdfSave)
		compare(attachmentDownloadSpy.count, 1)
		compare(attachmentDownloadSpy.signalArguments[0][0].assetId, "51")

		gallery.attachments = [{
			"id": "asset:zip",
			"assetId": "52",
			"kind": "binary",
			"mime": "application/zip",
			"fileName": "archive.zip"
		}]
		const zipAction = findChild(gallery, "attachmentAction_asset:zip")
		const zipSave = findChild(gallery, "attachmentDownload_asset:zip")
		verify(zipAction !== null && zipSave !== null)
		compare(zipAction.Accessible.name, "Save archive.zip")
		compare(zipSave.visible, false)
		mouseClick(zipAction)
		compare(attachmentSpy.count, 2)
		compare(attachmentSpy.signalArguments[1][0].assetId, "52")

		gallery.attachments = [{
			"id": "asset:legacy-pdf",
			"assetId": "53",
			"kind": "document",
			"mime": "application/octet-stream",
			"fileName": "legacy.pdf"
		}]
		const legacyPdfAction = findChild(gallery, "attachmentAction_asset:legacy-pdf")
		verify(legacyPdfAction !== null)
		compare(legacyPdfAction.Accessible.name, "Open legacy.pdf")

		gallery.attachments = [{
			"id": "asset:spoofed-pdf",
			"assetId": "54",
			"kind": "binary",
			"mime": "application/octet-stream",
			"fileName": "not-a-document.pdf"
		}]
		const spoofedPdfAction = findChild(gallery, "attachmentAction_asset:spoofed-pdf")
		verify(spoofedPdfAction !== null)
		compare(spoofedPdfAction.Accessible.name, "Save not-a-document.pdf")

		gallery.attachments = [{
			"id": "asset:m4a",
			"assetId": "55",
			"kind": "audio",
			"mime": "audio/mp4",
			"fileName": "voice-note.m4a"
		}]
		const m4aAction = findChild(gallery, "attachmentAction_asset:m4a")
		const m4aSave = findChild(gallery, "attachmentDownload_asset:m4a")
		verify(m4aAction !== null && m4aSave !== null)
		compare(m4aAction.Accessible.name, "Open voice-note.m4a")
		tryCompare(m4aSave, "visible", true)
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
		const stateBadge = findChild(card, "previewStateBadge")
		const stateBadgeLabel = findChild(card, "previewStateBadgeLabel")
		verify(stateBadge !== null && stateBadge.visible)
		verify(stateBadgeLabel !== null)
		compare(stateBadgeLabel.text, "Hidden")
		compare(card.previewStateColor, Theme.warning)
		verify(card.Accessible.description.indexOf("Sensitive imagery") >= 0)
		verify(card.Accessible.description.indexOf("Reveal the media") >= 0)
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
		compare(stateBadge.visible, false)
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
		const expandedSlot = findChild(card, "previewExpandedMediaSlot")
		verify(expandedSlot !== null)
		verify(card.clip, "rich preview media must not paint outside its delegate")
		compare(expandedSlot.Layout.minimumHeight, expandedSlot.Layout.preferredHeight)
		verify(expandedSlot.Layout.minimumHeight >= 180,
			"expanded media layout must reserve the visible panel height")
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
