pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0
import Mumble.ProviderPresentation 1.0

Rectangle {
	id: root

	required property var session
	property var mediaProfileFactory: null
	property url inlinePlayerComponentUrl: Qt.resolvedUrl("InlineMediaPlayer.qml")
	property string visualMediaFixtureMode: ""
	property bool renderActive: true
	property real maximumStageHeight: 520
	property real maximumWideWidth: 760
	property real maximumShortWidth: 360

	readonly property bool ownsSharedPlayer: !!session
		&& Boolean(session.sharedAvailable) && Boolean(session.sharedJoined)
		&& Boolean(session.active) && !Boolean(session.detached)
		&& String(session.sessionId || "").length > 0
		&& String(session.sessionId || "") === String(session.sharedSessionId || "")
	readonly property string normalizedAspect: normalizeAspect(
		session ? session.sharedAspect : "")
	readonly property var providerPresentation: ProviderPresentation.resolve(
		String(session ? session.provider || "" : "").trim())
	readonly property string providerLabel: providerPresentation.label
		|| String(session ? session.provider || "" : "").trim() || qsTr("Media")
	readonly property string providerMark: providerPresentation.mark
		|| providerLabel.slice(0, 2).toUpperCase()
	readonly property color providerAccent: providerPresentation.accent || Theme.accent
	readonly property real playerAspectRatio: normalizedAspect === "short" ? 9 / 16
		: normalizedAspect === "square" ? 1 : 16 / 9
	readonly property real controlsHeightEstimate: session && session.playbackControllable
		? Theme.controlHeight + Theme.space4 : 0
	readonly property real playerHeightBudget: Math.max(180,
		maximumStageHeight - stageHeader.implicitHeight - Theme.space6)
	readonly property real viewportHeightBudget: Math.max(120,
		playerHeightBudget - controlsHeightEstimate)
	readonly property real availablePlayerWidth: Math.max(1, width - Theme.space4)
	readonly property real playerWidth: Math.max(1, Math.min(
		normalizedAspect === "short" ? maximumShortWidth : maximumWideWidth,
		availablePlayerWidth, viewportHeightBudget * playerAspectRatio))
	readonly property real expectedPlayerHeight: playerWidth / playerAspectRatio
		+ controlsHeightEstimate
	readonly property bool componentLoadFailed: playerLoader.status === Loader.Error
	readonly property string surfaceId: visible ? "watchTogether.stage" : ""
	readonly property var captureRect: ({
		"x": 0, "y": 0, "width": width, "height": height
	})

	visible: ownsSharedPlayer
	implicitHeight: visible ? Math.min(maximumStageHeight,
		stageContent.implicitHeight + Theme.space4) : 0
	radius: Theme.innerRadius
	color: Theme.mediaCanvas
	border.color: Theme.withAlpha(providerAccent, 0.46)
	border.width: 1
	clip: true
	Accessible.role: Accessible.Pane
	Accessible.name: qsTr("Watch Together player: %1").arg(
		session && session.sharedTitle ? session.sharedTitle : providerLabel)
	Accessible.description: session && session.sharedHost
		? qsTr("Room media player. You control synchronized playback.")
		: qsTr("Room media player synchronized to the host.")

	function normalizeAspect(value) {
		const normalized = String(value || "").trim().toLowerCase()
		return [ "wide", "short", "square" ].indexOf(normalized) >= 0
			? normalized : "wide"
	}

	function loadPlayer() {
		if (!playerLoader.active) {
			playerLoader.source = ""
			return
		}
		playerLoader.setSource(root.inlinePlayerComponentUrl, {
			"session": root.session,
			"aspect": root.normalizedAspect,
			"presentationProvider": String(root.session.provider || ""),
			"presentationMode": "watch-together",
			"animationAutoPlayEnabled": true,
			"mediaProfileFactory": root.mediaProfileFactory,
			"visualFixtureMode": root.visualMediaFixtureMode
		})
	}

	function retryPlayerComponent() {
		playerLoader.source = ""
		Qt.callLater(root.loadPlayer)
	}

	function focusInitialControl() {
		if (componentLoadFailed) {
			componentRetryButton.forceActiveFocus()
			return true
		}
		if (playerLoader.item
			&& typeof playerLoader.item.focusInitialControl === "function")
			return playerLoader.item.focusInitialControl()
		return false
	}

	ColumnLayout {
		id: stageContent
		anchors.fill: parent
		anchors.margins: Theme.space2
		spacing: Theme.space2

		RowLayout {
			id: stageHeader
			Layout.fillWidth: true
			spacing: Theme.space2

			Rectangle {
				Layout.preferredWidth: Math.max(32, providerMarkLabel.implicitWidth + Theme.space3)
				Layout.preferredHeight: 24
				radius: height / 2
				color: Theme.withAlpha(root.providerAccent, 0.16)
				border.color: Theme.withAlpha(root.providerAccent, 0.42)
				Accessible.name: root.providerLabel

				Label {
					id: providerMarkLabel
					anchors.centerIn: parent
					text: root.providerMark
					textFormat: Text.PlainText
					color: root.providerAccent
					font.pixelSize: Theme.fontCaption
					font.weight: Font.DemiBold
					Accessible.ignored: true
				}
			}

			ColumnLayout {
				Layout.fillWidth: true
				Layout.minimumWidth: 0
				spacing: 0

				Label {
					Layout.fillWidth: true
					text: root.providerLabel
					textFormat: Text.PlainText
					color: Theme.textStrong
					font.pixelSize: Theme.fontBody
					font.weight: Font.DemiBold
					elide: Text.ElideRight
				}
				Label {
					Layout.fillWidth: true
					text: root.session && root.session.sharedHost
						? qsTr("Room player · You control playback")
						: qsTr("Room player · Synced to host")
					textFormat: Text.PlainText
					color: Theme.textMuted
					font.pixelSize: Theme.fontCaption
					elide: Text.ElideRight
				}
			}
		}

		Item {
			id: playerSlot
			objectName: "watchTogetherPlayerSlot"
			Layout.fillWidth: true
			Layout.preferredHeight: Math.min(root.playerHeightBudget,
				playerLoader.item ? playerLoader.item.implicitHeight : root.expectedPlayerHeight)
			Layout.minimumHeight: Math.min(Layout.preferredHeight, 180)

			Loader {
				id: playerLoader
				objectName: "watchTogetherPlayerLoader"
				anchors.centerIn: parent
				width: root.playerWidth
				height: parent.height
				active: root.visible && root.renderActive
				asynchronous: true
				visible: status !== Loader.Error
				Accessible.ignored: !visible

				onActiveChanged: root.loadPlayer()
				onStatusChanged: {
					if (status === Loader.Null && active)
						root.loadPlayer()
				}
				Component.onCompleted: root.loadPlayer()
			}

			Rectangle {
				id: componentFailureSurface
				objectName: "watchTogetherComponentFailureSurface"
				anchors.centerIn: parent
				width: root.playerWidth
				height: parent.height
				visible: root.componentLoadFailed
				color: Theme.mediaCanvas
				border.color: Theme.withAlpha(Theme.danger, 0.62)
				radius: Theme.innerRadius
				Accessible.role: Accessible.AlertMessage
				Accessible.name: qsTr("The Watch Together player could not be loaded")

				ColumnLayout {
					anchors.centerIn: parent
					width: Math.min(parent.width - Theme.space6 * 2, 420)
					spacing: Theme.space3

					Label {
						Layout.fillWidth: true
						text: qsTr("The room player is unavailable")
						textFormat: Text.PlainText
						color: Theme.textStrong
						font.pixelSize: Theme.fontBody
						font.weight: Font.DemiBold
						horizontalAlignment: Text.AlignHCenter
						wrapMode: Text.Wrap
					}
					Label {
						Layout.fillWidth: true
						text: qsTr("Retry the player without ending the shared session.")
						textFormat: Text.PlainText
						color: Theme.textMuted
						font.pixelSize: Theme.fontCaption
						horizontalAlignment: Text.AlignHCenter
						wrapMode: Text.Wrap
					}
					ModernButton {
						id: componentRetryButton
						objectName: "watchTogetherComponentRetryButton"
						Layout.alignment: Qt.AlignHCenter
						text: qsTr("Retry player")
						tone: "accent"
						highlighted: true
						onClicked: root.retryPlayerComponent()
					}
				}
			}
		}
	}
}
