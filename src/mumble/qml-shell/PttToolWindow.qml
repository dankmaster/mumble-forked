import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Mumble.Theme 1.0

Window {
	id: tool

	property bool hasBeenActive: false
	property bool holding: false
	readonly property bool compactLayout: width < 310 || height < 260
	readonly property bool showSafetyHint: height >= 220

	function beginHold() {
		if (holding)
			return
		holding = true
		uiCommands.setPttPressed(true)
	}

	function endHold() {
		holding = false
		uiCommands.releasePtt()
	}

	function releaseForSafety() {
		holding = false
		uiCommands.releasePtt()
	}

	function handlePttKeyPressed(key, autoRepeat) {
		if (key !== Qt.Key_Space)
			return false
		if (!autoRepeat)
			beginHold()
		return true
	}

	function handlePttKeyReleased(key, autoRepeat) {
		if (key !== Qt.Key_Space)
			return false
		if (!autoRepeat)
			endHold()
		return true
	}

	width: 340
	height: 264
	minimumWidth: 260
	minimumHeight: 180
	visible: false
	title: qsTr("Push to talk")
	color: Theme.shellBackground
	flags: Qt.Tool | Qt.WindowStaysOnTopHint

	onClosing: function(close) {
		releaseForSafety()
		close.accepted = true
	}
	onActiveChanged: {
		if (active) {
			hasBeenActive = true
		} else if (hasBeenActive) {
			releaseForSafety()
		}
	}
	onVisibleChanged: {
		if (!visible) {
			releaseForSafety()
			return
		}
		hasBeenActive = false
		Qt.callLater(function() {
			if (!tool.visible)
				return
			tool.requestActivate()
			pressButton.forceActiveFocus()
		})
	}
	Component.onDestruction: releaseForSafety()

	Shortcut {
		sequence: StandardKey.Cancel
		enabled: tool.visible
		onActivated: {
			tool.releaseForSafety()
			tool.hide()
		}
	}

	Rectangle {
		id: surface
		objectName: "pttToolSurface"
		anchors.fill: parent
		anchors.margins: tool.compactLayout ? Theme.space3 : Theme.space4
		radius: Theme.shellRadius
		color: tool.holding ? Theme.accentSubtle : Theme.panel
		border.color: tool.holding || pressButton.activeFocus ? Theme.focus : Theme.surfaceBorder
		border.width: tool.holding || pressButton.activeFocus ? Theme.focusRingWidth : 1
		Behavior on color { ColorAnimation { duration: Theme.motionFast } }
		Behavior on border.color { ColorAnimation { duration: Theme.motionFast } }

		ColumnLayout {
			anchors.fill: parent
			anchors.margins: tool.compactLayout ? Theme.space3 : Theme.space4
			spacing: tool.compactLayout ? Theme.space2 : Theme.space3

			ColumnLayout {
				Layout.fillWidth: true
				spacing: 2

				Label {
					Layout.fillWidth: true
					textFormat: Text.PlainText
					text: tool.holding ? qsTr("Transmitting") : qsTr("Push to talk")
					color: tool.holding ? Theme.accentHover : Theme.textStrong
					font.pixelSize: tool.compactLayout ? Theme.fontTitle : Theme.fontHeading
					font.weight: Font.DemiBold
					horizontalAlignment: Text.AlignHCenter
					Accessible.role: Accessible.Heading
				}

				Label {
					Layout.fillWidth: true
					textFormat: Text.PlainText
					text: tool.holding ? qsTr("Release to stop transmitting")
						: qsTr("Hold the button or Space key while you speak")
					color: Theme.textMuted
					font.pixelSize: Theme.fontCaption
					horizontalAlignment: Text.AlignHCenter
					wrapMode: Text.WordWrap
				}
			}

		Button {
			id: pressButton
			objectName: "pttHoldButton"
			Layout.fillWidth: true
			Layout.fillHeight: true
			Layout.minimumHeight: tool.compactLayout ? 70 : 92
			hoverEnabled: true
			activeFocusOnTab: true
			focusPolicy: Qt.StrongFocus
			Accessible.name: tool.holding ? qsTr("Transmitting, release push to talk") : qsTr("Hold to push to talk")
			Accessible.description: qsTr("Hold with the pointer or Space key to transmit. Transmission stops if this window loses focus or closes.")
			Accessible.role: Accessible.Button
			Accessible.pressed: tool.holding
			Accessible.onPressAction: {
				tool.beginHold()
				Qt.callLater(function() { tool.endHold() })
			}
			onPressed: tool.beginHold()
			onReleased: tool.endHold()
			onCanceled: tool.endHold()
			Keys.onPressed: function(event) {
				if (tool.handlePttKeyPressed(event.key, event.isAutoRepeat))
					event.accepted = true
			}
			Keys.onReleased: function(event) {
				if (tool.handlePttKeyReleased(event.key, event.isAutoRepeat))
					event.accepted = true
			}

			contentItem: RowLayout {
				spacing: Theme.space3

				Rectangle {
					Layout.preferredWidth: tool.compactLayout ? 34 : 42
					Layout.preferredHeight: width
					radius: width / 2
					color: tool.holding ? Theme.contrastText(Theme.accent) : Theme.accentSubtle
					border.color: tool.holding ? Theme.contrastText(Theme.accent) : Theme.accent
					border.width: 2

					Rectangle {
						anchors.centerIn: parent
						width: parent.width * 0.28
						height: parent.height * 0.46
						radius: width / 2
						color: tool.holding ? Theme.accent : Theme.textStrong
					}
				}

				ColumnLayout {
					Layout.fillWidth: true
					spacing: 1
					Label {
						textFormat: Text.PlainText
						Layout.fillWidth: true
						text: tool.holding ? qsTr("LIVE") : qsTr("HOLD TO TALK")
						color: tool.holding ? Theme.contrastText(Theme.accent) : Theme.textStrong
						font.pixelSize: Theme.fontLabel
						font.weight: Font.Bold
						font.letterSpacing: 0.8
						horizontalAlignment: Text.AlignHCenter
					}
					Label {
						textFormat: Text.PlainText
						Layout.fillWidth: true
						visible: !tool.compactLayout
						text: qsTr("Space")
						color: tool.holding ? Theme.contrastText(Theme.accent) : Theme.textMuted
						font.pixelSize: Theme.fontCaption
						horizontalAlignment: Text.AlignHCenter
					}
				}
			}

			background: Rectangle {
				radius: Theme.innerRadius
				color: !pressButton.enabled ? Theme.panel
					: tool.holding ? Theme.accent
					: pressButton.down ? Theme.accentSubtle
					: pressButton.hovered ? Theme.surfaceHover : Theme.surfaceRaised
				border.color: pressButton.activeFocus ? Theme.focus
					: tool.holding ? Theme.accentHover : Theme.surfaceBorder
				border.width: pressButton.activeFocus ? Theme.focusRingWidth : 1
				Behavior on color { ColorAnimation { duration: Theme.motionFast } }
			}
		}

		Label {
			objectName: "pttSafetyHint"
			textFormat: Text.PlainText
			Layout.fillWidth: true
			visible: tool.showSafetyHint
			text: qsTr("PTT is released automatically on focus loss, close, or cancel.")
			color: Theme.textMuted
			font.pixelSize: Theme.fontCaption
			horizontalAlignment: Text.AlignHCenter
			wrapMode: Text.WordWrap
		}
	}
}
}
