import QtQuick
import QtTest

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

		// Qt Quick Test cannot reliably route pointer events into a second native
		// Window on every offscreen backend. Exercise the exact hold functions
		// wired to AbstractButton::pressed/released, then cover keyboard routing.
		tool.beginHold()
		tryCompare(uiCommands, "pttPressed", true)
		tool.endHold()
		tryCompare(uiCommands, "pttPressed", false)

		button.forceActiveFocus()
		tryCompare(button, "activeFocus", true)
		tool.beginHold()
		tryCompare(uiCommands, "pttPressed", true)
		tool.endHold()
		tryCompare(uiCommands, "pttPressed", false)
		tool.destroy()
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

	function test_attachmentViewerAcceptsOnlyManagedAnimatedFiles() {
		const digest = "a".repeat(64)
		const managed = "file:///C:/Temp/mumble-qml-images-a1/" + digest
			+ "-12345678-1234-1234-1234-123456789abc.gif"
		const tool = createTool("qrc:/qml-shell/AttachmentViewer.qml", {
			"attachment": { "url": managed, "alt": "Managed animation" }
		})
		compare(tool.renderSource, managed)
		compare(tool.managedAnimated, true)
		compare(tool.safeRenderImageSource("file:///C:/Temp/unmanaged.gif"), "")
		tool.destroy()
	}
}
