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

	SignalSpy {
		id: playSpy
		target: previewLoader.item
		signalName: "playRequested"
	}

	SignalSpy {
		id: watchTogetherSpy
		target: previewLoader.item
		signalName: "watchTogetherRequested"
	}

    function init() {
        tryVerify(function() {
            return bodyLoader.item !== null && previewLoader.item !== null && attachmentLoader.item !== null
        })
        attachmentSpy.clear()
        directMediaSpy.clear()
        externalOpenSpy.clear()
        imageOpenSpy.clear()
		playSpy.clear()
		watchTogetherSpy.clear()
		linkSpy.clear()
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
        previewLoader.item.resetForReuse()
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

    function test_preview_loading_error_and_expansion_states() {
        const card = previewLoader.item
        compare(card.previewState, "loading")
        card.preview = {
            "state": "error",
            "failed": true,
            "title": "Preview unavailable",
			"description": "<b>Provider request failed</b>",
            "url": "https://example.com/card"
        }
        compare(card.previewState, "error")
		compare(card.errorDescription, "<b>Provider request failed</b>")
		verify(card.Accessible.description.indexOf("<b>Provider request failed</b>") >= 0)
		const errorText = findChild(card, "previewErrorText")
		verify(errorText !== null && errorText.visible)
		compare(errorText.textFormat, Text.PlainText)
		verify(errorText.text.indexOf("<b>Provider request failed</b>") >= 0)
		compare(card.canExpand, false)
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
		compare(findChild(card, "previewExpandButton").visible, true)
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
		compare(findChild(card, "previewWatchTogetherButton").visible, false)
		card.requestCurrentMedia()
		compare(directMediaSpy.count, 0)
		compare(externalOpenSpy.count, 0)
		compare(playSpy.count, 0)
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

	function test_preview_actions_stack_and_remain_bounded_at_long_responsive_widths() {
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
		for (const width of [340, 420, 680, 760, 1082]) {
			previewLoader.width = width
			previewLoader.height = 640
			tryCompare(card, "width", width)
			compare(card.narrowLayout, width < 440)
			const flow = findChild(card, "previewActionFlow")
			verify(flow !== null)
			tryCompare(flow, "width", card.actionAvailableWidth)
			let previousBottom = -1
			for (const name of [ "previewOpenButton", "previewPlayButton",
					"previewWatchTogetherButton", "previewExpandButton" ]) {
				const button = findChild(card, name)
				verify(button !== null && button.visible)
				verify(button.x >= -0.5 && button.x + button.width <= flow.width + 0.5,
					name + " bounds " + button.x + "+" + button.width + " within " + flow.width)
				verify(button.Accessible.name.length > 0)
				if (width < 440) {
					compare(button.width, flow.width)
					verify(button.y >= previousBottom - 0.5)
					previousBottom = button.y + button.height
				}
			}
		}
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
		verify(reveal !== null && reveal.visible)
		verify(reveal.Accessible.description.indexOf("Sensitive imagery") >= 0)
		compare(findChild(card, "previewExpandedRevealButton").visible, false)
		reveal.forceActiveFocus()
		tryCompare(reveal, "activeFocus", true)
		tryCompare(findChild(card, "previewCompactRevealFocus"), "visible", true)
		keyClick(Qt.Key_Space)
		verify(!card.mediaRequiresReveal)
		compare(findChild(card, "previewCompactImage").source.toString(),
			"image://mumble/sensitive?g=1")
		card.previewIdentity = "message:sensitive|two"
		verify(card.mediaRequiresReveal)
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
		verify(compactAction !== null)
		tryVerify(function() { return compactAction.enabled })
		compactAction.forceActiveFocus()
		tryCompare(compactAction, "activeFocus", true)
		tryCompare(findChild(card, "previewCompactMediaFocus"), "visible", true)

		card.userExpanded = true
		const expandedAction = findChild(card, "previewExpandedMediaButton")
		const expandedImage = findChild(card, "previewExpandedStaticImage")
		const expandedPanel = findChild(card, "previewExpandedMediaPanel")
		tryCompare(expandedImage, "status", Image.Ready)
		verify(expandedAction.visible && expandedAction.enabled,
			"expanded=" + card.expanded + " kind=" + card.currentMediaKind
			+ " source=" + expandedImage.source + " status=" + expandedImage.status
			+ " panel=" + expandedPanel.visible + " panelSize=" + expandedPanel.width + "x" + expandedPanel.height
			+ " actionVisible=" + expandedAction.visible + " actionEnabled=" + expandedAction.enabled)
		expandedAction.forceActiveFocus()
		tryCompare(findChild(card, "previewExpandedMediaFocus"), "visible", true)
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
		verify(expandedError.Accessible.description.indexOf("Sanitized image decoder error") >= 0)
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
