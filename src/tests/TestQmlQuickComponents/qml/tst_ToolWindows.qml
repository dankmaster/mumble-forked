import QtQuick
import QtTest
import Mumble.Theme 1.0

TestCase {
    id: testCase
    name: "ToolWindows"
    when: windowShown
    width: 320
    height: 240

    function createTool(url, properties) {
        const component = Qt.createComponent(url)
        compare(component.status, Component.Ready, component.errorString())
		const tool = component.createObject(null, properties || {})
        verify(tool !== null, component.errorString())
        return tool
    }

	function verifyThemePalette(target) {
		verify(target !== null)
		compare(target.palette.window, Theme.shellBackground)
		compare(target.palette.base, Theme.surfaceRaised)
		compare(target.palette.alternateBase, Theme.panel)
		compare(target.palette.button, Theme.surfaceRaised)
		compare(target.palette.text, Theme.textMain)
		compare(target.palette.windowText, Theme.textMain)
		compare(target.palette.buttonText, Theme.textStrong)
		compare(target.palette.brightText, Theme.textStrong)
		compare(target.palette.highlight, Theme.accent)
		compare(target.palette.highlightedText, Theme.contrastText(Theme.accent))
		compare(target.palette.placeholderText, Theme.textMuted)
		compare(target.palette.light, Theme.surfaceHover)
		compare(target.palette.midlight, Theme.surfaceRaised)
		compare(target.palette.mid, Theme.surfaceBorder)
		compare(target.palette.dark, Theme.rail)
		compare(target.palette.shadow, Theme.strip)
		compare(target.palette.link, Theme.accent)
		compare(target.palette.linkVisited, Theme.accentHover)
		compare(target.palette.toolTipBase, Theme.surfaceRaised)
		compare(target.palette.toolTipText, Theme.textStrong)
		const disabledProbe = Qt.createQmlObject(
			'import QtQuick.Controls; Control { enabled: false }', target.contentItem)
		verify(disabledProbe !== null)
		compare(disabledProbe.palette.window, Theme.shellBackground)
		compare(disabledProbe.palette.base, Theme.panel)
		compare(disabledProbe.palette.alternateBase, Theme.panel)
		compare(disabledProbe.palette.button, Theme.panel)
		compare(disabledProbe.palette.text, Theme.textMuted)
		compare(disabledProbe.palette.windowText, Theme.textMuted)
		compare(disabledProbe.palette.buttonText, Theme.textMuted)
		compare(disabledProbe.palette.brightText, Theme.textMuted)
		compare(disabledProbe.palette.highlight, Theme.surfaceBorder)
		compare(disabledProbe.palette.highlightedText, Theme.textMuted)
		compare(disabledProbe.palette.placeholderText, Theme.textMuted)
		compare(disabledProbe.palette.light, Theme.surfaceBorder)
		compare(disabledProbe.palette.midlight, Theme.panel)
		compare(disabledProbe.palette.mid, Theme.divider)
		compare(disabledProbe.palette.dark, Theme.rail)
		compare(disabledProbe.palette.shadow, Theme.strip)
		compare(disabledProbe.palette.link, Theme.textMuted)
		compare(disabledProbe.palette.linkVisited, Theme.textMuted)
		compare(disabledProbe.palette.toolTipBase, Theme.panel)
		compare(disabledProbe.palette.toolTipText, Theme.textMuted)
		disabledProbe.destroy()
	}

	function test_standaloneWindowsExposeCompleteThemePalette() {
		dialogState.setSpecialState("imageViewer", {
			"imageViewer": { "src": "", "width": 1, "height": 1 }
		})
		const tools = [
			createTool("qrc:/qml-shell/PttToolWindow.qml"),
			createTool("qrc:/qml-shell/ManualPluginWindow.qml"),
			createTool("qrc:/qml-shell/AttachmentViewer.qml", {
				"attachment": { "url": "", "alt": "Palette fixture" }
			}),
			createTool("qrc:/qml-shell/ImageViewer.qml", {
				"controller": dialogState
			})
		]
		try {
			for (const tool of tools)
				verifyThemePalette(tool)
		} finally {
			for (const tool of tools)
				tool.destroy()
		}
	}

    function test_pttToolReleasesWhenHidden() {
        uiCommands.clearCounts()
        const tool = createTool("qrc:/qml-shell/PttToolWindow.qml")
        tool.show()
        wait(1)
        uiCommands.setPttPressed(true)
        compare(uiCommands.pttPressed, true)
        tool.hide()
        tryCompare(uiCommands, "pttPressed", false)
        verify(uiCommands.releaseCount > 0)
        tool.destroy()
    }

	function test_pttToolReleasesWhenClosedOrDestroyed() {
		uiCommands.clearCounts()
		let tool = createTool("qrc:/qml-shell/PttToolWindow.qml")
		tool.show()
		wait(1)
		tool.beginHold()
		tryCompare(uiCommands, "pttPressed", true)
		tool.close()
		tryCompare(uiCommands, "pttPressed", false)
		verify(uiCommands.releaseCount > 0)
		tool.destroy()

		uiCommands.clearCounts()
		tool = createTool("qrc:/qml-shell/PttToolWindow.qml")
		tool.show()
		wait(1)
		tool.beginHold()
		tryCompare(uiCommands, "pttPressed", true)
		tool.destroy()
		wait(1)
		compare(uiCommands.pttPressed, false)
		verify(uiCommands.releaseCount > 0)
	}

	function test_pttToolSupportsPointerAndKeyboardHold() {
		uiCommands.clearCounts()
		const tool = createTool("qrc:/qml-shell/PttToolWindow.qml")
		tool.show()
		tool.requestActivate()
		const button = findChild(tool.contentItem, "pttHoldButton")
		verify(button !== null)
		verify(button.activeFocusOnTab)
		compare(button.Accessible.role, Accessible.Button)
		verify(button.Accessible.description.indexOf("focus") >= 0)

		// Qt Quick Test cannot reliably route pointer events into a second native
		// Window on every offscreen backend. Exercise the exact hold functions
		// wired to AbstractButton::pressed/released, then cover keyboard routing.
		tool.beginHold()
		tryCompare(uiCommands, "pttPressed", true)
		compare(tool.holding, true)
		const releasesBeforeDuplicatePress = uiCommands.releaseCount
		tool.beginHold()
		compare(uiCommands.releaseCount, releasesBeforeDuplicatePress)
		tool.endHold()
		tryCompare(uiCommands, "pttPressed", false)
		compare(tool.holding, false)

		button.forceActiveFocus()
		tryCompare(button, "activeFocus", true)
		verify(tool.handlePttKeyPressed(Qt.Key_Space, false))
		tryCompare(uiCommands, "pttPressed", true)
		verify(tool.handlePttKeyPressed(Qt.Key_Space, true))
		tryCompare(uiCommands, "pttPressed", true)
		verify(tool.handlePttKeyReleased(Qt.Key_Space, false))
		tryCompare(uiCommands, "pttPressed", false)
		verify(!tool.handlePttKeyPressed(Qt.Key_Return, false))
		tool.destroy()
	}

	function test_pttToolHasBoundedCompactStateAndAccessibleFeedback() {
		uiCommands.clearCounts()
		const tool = createTool("qrc:/qml-shell/PttToolWindow.qml", {
			"width": 270,
			"height": 190
		})
		tool.show()
		tryCompare(tool, "compactLayout", true)
		const surface = findChild(tool.contentItem, "pttToolSurface")
		const button = findChild(tool.contentItem, "pttHoldButton")
		verify(surface !== null && button !== null)
		verify(surface.x >= 0 && surface.x + surface.width <= tool.width)
		verify(surface.y >= 0 && surface.y + surface.height <= tool.height)
		tool.beginHold()
		tryCompare(button.Accessible, "pressed", true)
		compare(button.Accessible.name, "Transmitting, release push to talk")
		tool.hide()
		tryCompare(uiCommands, "pttPressed", false)
		compare(tool.holding, false)
		tool.destroy()
	}

	function test_pttDefaultSafetyHintFitsInsideSurface() {
		const tool = createTool("qrc:/qml-shell/PttToolWindow.qml")
		try {
			tool.show()
			const surface = findChild(tool.contentItem, "pttToolSurface")
			const safetyHint = findChild(tool.contentItem, "pttSafetyHint")
			verify(surface !== null && safetyHint !== null)
			tryVerify(function() { return surface.height > 0 && safetyHint.height > 0 })
			verify(safetyHint.visible)
			const hintBottom = safetyHint.mapToItem(surface, 0, safetyHint.height).y
			verify(hintBottom <= surface.height + 1,
				"PTT safety hint must not be clipped by the default tool window: bottom="
					+ hintBottom + ", surface=" + surface.height)
		} finally {
			tool.hide()
			tool.destroy()
		}
	}

	function test_pttPersistedRuntimeHeightKeepsSafetyHintVisibleAndUnclipped() {
		const tool = createTool("qrc:/qml-shell/PttToolWindow.qml", {
			"width": 340,
			"height": 240
		})
		try {
			tool.show()
			tryCompare(tool, "compactLayout", true)
			const surface = findChild(tool.contentItem, "pttToolSurface")
			const safetyHint = findChild(tool.contentItem, "pttSafetyHint")
			verify(surface !== null && safetyHint !== null)
			tryVerify(function() { return surface.height > 0 && safetyHint.height > 0 })
			verify(safetyHint.visible)
			const hintBottom = safetyHint.mapToItem(surface, 0, safetyHint.height).y
			verify(hintBottom <= surface.height + 1,
				"PTT safety hint must fit a persisted 340x240 window: bottom="
					+ hintBottom + ", surface=" + surface.height)
		} finally {
			tool.hide()
			tool.destroy()
		}
	}

    function test_manualPluginToolLoadsAndRefreshesLazily() {
        manualPlugin.clearCounts()
        const tool = createTool("qrc:/qml-shell/ManualPluginWindow.qml")
        compare(manualPlugin.refreshCount, 0)
        tool.show()
        tryVerify(function() { return manualPlugin.refreshCount > 0 })
		tryCompare(manualPlugin, "speakerUpdatesEnabled", true)
        compare(tool.visible, true)
        tool.hide()
		tryCompare(manualPlugin, "speakerUpdatesEnabled", false)
        tool.destroy()
    }

	function test_manualPluginToolIsResponsive_and_actions_report_status() {
		manualPlugin.clearCounts()
		const tool = createTool("qrc:/qml-shell/ManualPluginWindow.qml", {
			"width": 460,
			"height": 560
		})
		tool.show()
		tryCompare(tool, "compactLayout", true)
		const scroll = findChild(tool.contentItem, "manualPluginScrollView")
		const content = findChild(tool.contentItem, "manualPluginContent")
		const footer = findChild(tool.contentItem, "manualPluginFooter")
		const endPadding = findChild(tool.contentItem, "manualPluginContentEndPadding")
		const preview = findChild(tool.contentItem, "manualPositionPreview")
		const grid = findChild(tool.contentItem, "manualPositionGrid")
		verify(scroll !== null && content !== null && footer !== null && endPadding !== null
			&& preview !== null && grid !== null)
		compare(grid.columns, 2)
		tryVerify(function() { return scroll.width > 0 && content.width > 0 && preview.width > 0 })
		verify(content.width <= scroll.width + 0.5,
			"Manual content " + content.width + " must fit scroll view " + scroll.width)
		verify(preview.width <= content.width + 0.5,
			"Preview " + preview.width + " must fit content " + content.width)
		compare(preview.Accessible.role, Accessible.Canvas)

		const resetButton = findChild(tool.contentItem, "manualResetButton")
		const applyButton = findChild(tool.contentItem, "manualApplyButton")
		const contextField = findChild(tool.contentItem, "manualContextField")
		const status = findChild(tool.contentItem, "manualPluginStatus")
		verify(resetButton !== null && applyButton !== null && contextField !== null && status !== null)
		tryVerify(function() { return scroll.contentHeight > scroll.height })
		const scrollBottom = scroll.mapToItem(tool.contentItem, 0, scroll.height).y
		verify(scrollBottom <= footer.y + 1,
			"Manual Plugin body must stop before its persistent action footer")
		for (const button of [resetButton, applyButton]) {
			const buttonTop = button.mapToItem(tool.contentItem, 0, 0).y
			const buttonBottom = button.mapToItem(tool.contentItem, 0, button.height).y
			verify(buttonTop >= footer.y - 1 && buttonBottom <= footer.y + footer.height + 1,
				"Manual Plugin actions must stay visible inside the persistent footer")
		}
		const flickable = scroll.contentItem
		tryVerify(function() {
			flickable.contentY = Math.max(0, flickable.contentHeight - flickable.height)
			return endPadding.mapToItem(tool.contentItem, 0, endPadding.height).y <= footer.y
		})
		const endBottom = endPadding.mapToItem(tool.contentItem, 0, endPadding.height).y
		verify(endBottom <= footer.y,
			"Manual Plugin's final body content must remain reachable above the footer: end="
				+ endBottom + ", footer=" + footer.y + ", contentY=" + flickable.contentY
				+ ", contentHeight=" + flickable.contentHeight + ", height=" + flickable.height)
		resetButton.clicked()
		compare(manualPlugin.resetCount, 1)
		tryCompare(status, "visible", true)
		compare(status.text, "Position reset")
		contextField.text = "pending-context-from-editor"
		applyButton.clicked()
		compare(manualPlugin.applyCount, 1)
		compare(manualPlugin.context, "pending-context-from-editor")
		compare(status.text, "Position updated")
		tool.hide()
		tryCompare(manualPlugin, "speakerUpdatesEnabled", false)
		tool.destroy()
	}

	function test_manualPluginInitialFocusAndDirtyCloseGuard() {
		manualPlugin.clearCounts()
		const tool = createTool("qrc:/qml-shell/ManualPluginWindow.qml")
		try {
			tool.show()
			tool.requestActivate()
			const xField = findChild(tool.contentItem, "manualXField")
			const contextField = findChild(tool.contentItem, "manualContextField")
			const dirtyStatus = findChild(tool.contentItem, "manualPluginDirtyStatus")
			const discardDialog = findChild(tool.contentItem, "manualDiscardDialog")
			const keepEditing = findChild(tool.contentItem, "manualKeepEditingButton")
			const discardChanges = findChild(tool.contentItem, "manualDiscardChangesButton")
			const applyButton = findChild(tool.contentItem, "manualApplyButton")
			const barrier = findChild(tool.contentItem, "manualDiscardAccessibilityBarrier")
			tryVerify(function() {
				return xField !== null && contextField !== null && dirtyStatus !== null
					&& discardDialog !== null && keepEditing !== null && discardChanges !== null
					&& applyButton !== null && barrier !== null
			})
			tryCompare(xField, "activeFocus", true)
			tryCompare(tool, "dirty", false)
			compare(dirtyStatus.visible, false)

			const baselineContext = contextField.text
			contextField.forceActiveFocus()
			tryCompare(contextField, "activeFocus", true)
			contextField.text = baselineContext + "-draft"
			tryCompare(tool, "dirty", true)
			tryCompare(dirtyStatus, "visible", true)

			tool.requestClose()
			tryCompare(discardDialog, "opened", true)
			compare(tool.visible, true)
			tryCompare(keepEditing, "activeFocus", true)
			tryCompare(contextField.Accessible, "ignored", true)
			tryCompare(applyButton.Accessible, "ignored", true)
			tryCompare(barrier, "active", true)
			verify(!discardDialog.contentItem.Accessible.ignored)
			verify(!keepEditing.Accessible.ignored)
			keepEditing.clicked()
			tryCompare(discardDialog, "opened", false)
			tryCompare(contextField.Accessible, "ignored", false)
			tryCompare(applyButton.Accessible, "ignored", false)
			tryCompare(barrier, "active", false)
			tryCompare(contextField, "activeFocus", true)
			compare(tool.dirty, true)

			// The native title-bar close path must use the same guard.
			tool.close()
			tryCompare(discardDialog, "opened", true)
			compare(tool.visible, true)
			discardChanges.clicked()
			tryCompare(tool, "visible", false)
			compare(manualPlugin.context, baselineContext)

			tool.show()
			tool.requestActivate()
			tryCompare(tool, "dirty", false)
			tryCompare(xField, "activeFocus", true)
		} finally {
			tool.hide()
			tool.destroy()
		}
	}

	function test_manualPluginVisualFixtureKeepsTextFocusWithoutBlinkingCaret() {
		const tool = createTool("qrc:/qml-shell/ManualPluginWindow.qml", {
			"visualFixtureMode": true
		})
		try {
			tool.show()
			const xField = findChild(tool.contentItem, "manualXField")
			verify(xField !== null)
			xField.forceActiveFocus()
			tryCompare(xField, "activeFocus", true)
			compare(xField.readOnly, true)
			compare(xField.cursorVisible, false)
		} finally {
			tool.hide()
			tool.destroy()
		}
	}

	function test_attachmentViewerAcceptsOnlyManagedAnimatedFiles() {
		const digest = "a".repeat(64)
		const managed = "file:///C:/Temp/mumble-qml-images-a1/" + digest
			+ "-12345678-1234-1234-1234-123456789abc.gif"
		const tool = createTool("qrc:/qml-shell/AttachmentViewer.qml", {
			"attachment": { "url": "", "alt": "Managed animation", "assetId": "42",
				"width": 1600, "height": 900, "originalState": "loading" }
		})
		let saveRequests = 0
		let refreshRequests = 0
		tool.saveRequested.connect(function() { saveRequests += 1 })
		tool.refreshRequested.connect(function() { refreshRequests += 1 })
		const originalStatus = findChild(tool.contentItem, "attachmentViewerOriginalStatus")
		verify(originalStatus !== null)
		compare(originalStatus.visible, false,
			"Background original loading must not cover an already usable preview")
		compare(tool.safeRenderImageSource(managed), managed)
		compare(tool.safeRenderImageSource("file:///C:/Temp/unmanaged.gif"), "")
		tool.attachment = { "url": managed, "alt": "Managed animation", "assetId": "42",
			"width": 1600, "height": 900, "originalState": "loading" }
		tool.width = 420
		tool.height = 320
		tryCompare(tool, "compactLayout", true)
		const closeButton = findChild(tool.contentItem, "attachmentViewerCloseButton")
		verify(closeButton !== null)
		verify(closeButton.x >= 0 && closeButton.x + closeButton.width <= tool.width)
		compare(closeButton.Accessible.name, "Close attachment viewer")
		const saveButton = findChild(tool.contentItem, "attachmentViewerSaveButton")
		verify(saveButton !== null)
		tryCompare(saveButton, "visible", true)
		compare(saveButton.Accessible.name, "Save original Managed animation")
		const errorLabel = findChild(tool.contentItem, "attachmentViewerError")
		verify(errorLabel !== null && errorLabel.visible)
		compare(errorLabel.textFormat, Text.PlainText)
		compare(errorLabel.Accessible.role, Accessible.AlertMessage)
		tryVerify(function() { return refreshRequests === 1 })
		const retryButton = findChild(tool.contentItem, "attachmentViewerRetryButton")
		verify(retryButton !== null)
		tool.requestActivate()
		tryCompare(tool, "active", true)
		tryCompare(retryButton, "activeFocus", true)
		compare(retryButton.Accessible.name, "Retry loading Managed animation")
		mouseClick(saveButton)
		compare(saveRequests, 1)
		mouseClick(retryButton)
		tryVerify(function() { return refreshRequests === 2 })
		tryCompare(tool, "retryingSource", false)
		const animation = findChild(tool.contentItem, "attachmentViewerAnimation")
		verify(animation !== null)
		compare(String(animation.source), managed,
			"Retry must only reload the viewer's current sanitized source")
		wait(50)
		compare(refreshRequests, 2,
			"A failed local retry must not enqueue a duplicate hydration request")
		const zoomToolbar = findChild(tool.contentItem, "attachmentViewerZoomToolbar")
		verify(zoomToolbar !== null && zoomToolbar.visible)
		const zoomIn = findChild(tool.contentItem, "attachmentViewerZoomIn")
		const zoomOut = findChild(tool.contentItem, "attachmentViewerZoomOut")
		const actualSize = findChild(tool.contentItem, "attachmentViewerActualSize")
		verify(zoomIn !== null && zoomOut !== null && actualSize !== null)
		compare(tool.zoom, 1)
		mouseClick(zoomIn)
		verify(tool.zoom > 1)
		mouseClick(zoomOut)
		verify(Math.abs(tool.zoom - 1) < 0.001)
		mouseClick(actualSize)
		verify(tool.zoom > 1, "Actual-size mode must enlarge an image currently fitted to the window")
		tool.resetZoom()
		compare(tool.zoom, 1)
		tool.destroy()
	}

	function test_attachmentViewerTimesOutAndRetriesOriginalWithoutDroppingPreview() {
		const tool = createTool("qrc:/qml-shell/AttachmentViewer.qml", {
			"attachment": {
				"url": "",
				"thumbnailUrl": "image://mumble/original-preview?g=1",
				"alt": "Original preview",
				"assetId": "42",
				"hydrationMessageId": "9",
				"width": 1600,
				"height": 900,
				"originalState": "loading"
			},
			"originalLoadTimeout": 100
		})
		let originalRetryRequests = 0
		tool.originalRetryRequested.connect(function() { originalRetryRequests += 1 })
		const originalStatusLabel = findChild(tool.contentItem, "attachmentViewerOriginalStatusLabel")
		const originalStatus = findChild(tool.contentItem, "attachmentViewerOriginalStatus")
		const originalRetryButton = findChild(tool.contentItem, "attachmentViewerOriginalRetryButton")
		verify(originalStatusLabel !== null && originalStatus !== null && originalRetryButton !== null)
		compare(originalStatus.visible, false,
			"The usable preview must stay free of loading chrome while the original swaps in")
		compare(originalRetryButton.visible, false)
		tryCompare(tool, "originalTimedOut", true)
		tryCompare(originalStatus, "visible", true)
		tryCompare(originalRetryButton, "visible", true)
		compare(originalStatusLabel.text, "Preview shown · full resolution delayed")
		compare(originalRetryButton.Accessible.name, "Retry loading the full-resolution image")
		mouseClick(originalRetryButton)
		compare(originalRetryRequests, 1)
		compare(tool.originalTimedOut, false)
		compare(String(findChild(tool.contentItem, "attachmentViewerImage").source),
			"image://mumble/original-preview?g=1")
		tool.destroy()
	}

	function test_attachmentViewerUsesLoadedOriginalPixelsForFitAndActualSize() {
		const tool = createTool("qrc:/qml-shell/AttachmentViewer.qml", {
			"attachment": {
				"url": "image://mumble/viewer-full-resolution?g=1",
				"thumbnailUrl": "image://mumble/viewer-preview?g=1",
				"alt": "Full-resolution fixture",
				"assetId": "42",
				"width": 1600,
				"height": 900,
				"originalState": "ready"
			}
		})
		try {
			const picture = findChild(tool.contentItem, "attachmentViewerImage")
			verify(picture !== null)
			tryCompare(picture, "status", Image.Ready)
			const pixels = tool.declaredImageSize()
			compare(pixels.width, 96)
			compare(pixels.height, 64)
			compare(tool.fitScale(), 1)
			tool.setActualSize()
			compare(tool.zoom, 1)
		} finally {
			tool.destroy()
		}
	}

	function test_imageViewerRejectsRemoteSourcesAndShowsKeyboardFocus() {
		dialogState.setSpecialState("imageViewer", {
			"imageViewer": {
				"src": "https://untrusted.example/image.png",
				"width": 640,
				"height": 480
			}
		})
		const tool = createTool("qrc:/qml-shell/ImageViewer.qml", {
			"controller": dialogState,
			"width": 420,
			"height": 320
		})
		compare(tool.modality, Qt.WindowModal)
		tryCompare(tool, "compactLayout", true)
		const errorLabel = findChild(tool.contentItem, "imageViewerError")
		verify(errorLabel !== null && errorLabel.visible)
		compare(errorLabel.textFormat, Text.PlainText)
		compare(errorLabel.Accessible.role, Accessible.AlertMessage)
		const stage = findChild(tool.contentItem, "imageViewerStage")
		verify(stage !== null)
		const minimizeButton = findChild(tool.contentItem, "imageViewerMinimize")
		verify(minimizeButton !== null)
		compare(minimizeButton.iconName, "minimize")
		stage.forceActiveFocus()
		tryCompare(stage, "activeFocus", true)
		const toolbar = findChild(tool.contentItem, "imageViewerZoomToolbar")
		verify(toolbar !== null)
		tryVerify(function() {
			return toolbar.x >= 0 && toolbar.x + toolbar.width <= stage.width
		})
		tool.destroy()
	}
}
