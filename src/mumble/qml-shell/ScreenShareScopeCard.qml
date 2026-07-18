import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

// A compact room-level summary for an active screen share. This deliberately
// consumes the typed room DTO rather than duplicating stream state in QML.
Rectangle {
	id: root

	required property var share
	property string scopeLabel: ""
	property bool narrowLayout: false
	property bool accessibilitySuppressed: false
	signal actionRequested(string actionId)

	readonly property string mode: String(share.mode || "idle").trim().toLowerCase()
	readonly property bool activeShare: String(share.streamId || "").trim().length > 0
	readonly property string ownerLabel: String(share.ownerLabel || "").trim()
	readonly property string statusLabel: String(share.statusLabel || "").trim()
	readonly property string statusTone: String(share.statusTone || share.badgeTone || "accent").toLowerCase()
	readonly property color toneColor: statusTone === "danger" || statusTone === "error" ? Theme.danger
		: statusTone === "warning" ? Theme.warning
		: statusTone === "success" ? Theme.success
		: statusTone === "muted" ? Theme.textMuted : Theme.accent
	readonly property string modeLabel: mode === "publishing" ? qsTr("PUBLISHING")
		: mode === "viewing" ? qsTr("VIEWING")
		: mode === "connecting" ? qsTr("CONNECTING")
		: mode === "fallback" ? qsTr("FALLBACK")
		: mode === "error" ? qsTr("UNAVAILABLE") : qsTr("LIVE")
	readonly property string titleLabel: mode === "publishing" ? qsTr("Your screen share")
		: ownerLabel.length > 0 ? qsTr("%1 is sharing").arg(ownerLabel)
		: qsTr("Live screen share")
	readonly property var actionItems: {
		const output = []
		const source = share.overflowActions || []
		for (let index = 0; index < source.length; ++index) {
			const item = source[index] || ({})
			if (String(item.kind || "action") === "action" && item.visible !== false)
				output.push(item)
		}
		return output
	}
	readonly property string technicalDetail: {
		if (String(share.fallbackLabel || "").trim().length > 0)
			return String(share.fallbackLabel)
		const parts = []
		function append(value) {
			const normalized = String(value || "").trim()
			if (normalized.length > 0 && parts.indexOf(normalized) < 0)
				parts.push(normalized)
		}
		append(share.resolutionLabel)
		if (Number(share.bitrateKbps || 0) > 0)
			append(qsTr("%1 kbps").arg(Number(share.bitrateKbps)))
		append(String(share.qualityProfile || "").replace(/_/g, " "))
		append(share.runtimeLabel)
		return parts.join(" · ")
	}
	readonly property string accessibleSummary: {
		const parts = [ titleLabel, statusLabel, technicalDetail ]
		return parts.filter(function(part) { return String(part || "").trim().length > 0 }).join(". ")
	}

	objectName: "activeScopeScreenShareCard"
	visible: activeShare
	implicitHeight: visible ? Math.max(58, content.implicitHeight + Theme.space3 * 2) : 0
	radius: Theme.innerRadius
	color: Theme.chatSurface
	border.color: Theme.withAlpha(toneColor, 0.46)
	border.width: 1
	Accessible.ignored: accessibilitySuppressed || !visible
	Accessible.role: mode === "error" ? Accessible.AlertMessage : Accessible.Pane
	Accessible.name: titleLabel
	Accessible.description: accessibleSummary

	RowLayout {
		id: content
		anchors.fill: parent
		anchors.margins: Theme.space3
		spacing: Theme.space3

		Rectangle {
			Layout.preferredWidth: Theme.controlHeight
			Layout.preferredHeight: Theme.controlHeight
			Layout.alignment: Qt.AlignVCenter
			radius: Theme.innerRadius
			color: Theme.withAlpha(root.toneColor, 0.13)
			border.color: Theme.withAlpha(root.toneColor, 0.28)
			Accessible.ignored: true
			ModernIcon {
				anchors.centerIn: parent
				name: "screen-share"
				size: 18
				color: root.toneColor
			}
		}

		ColumnLayout {
			Layout.fillWidth: true
			Layout.minimumWidth: 0
			Layout.alignment: Qt.AlignVCenter
			spacing: 2

			RowLayout {
				Layout.fillWidth: true
				spacing: Theme.space2
				Label {
					objectName: "activeScopeScreenShareTitle"
					Layout.fillWidth: true
					Layout.minimumWidth: 0
					textFormat: Text.PlainText
					text: root.titleLabel
					color: Theme.textStrong
					font.pixelSize: Theme.fontBody
					font.weight: Font.DemiBold
					elide: Text.ElideRight
					Accessible.ignored: true
				}
				Rectangle {
					objectName: "activeScopeScreenShareStateBadge"
					Layout.preferredWidth: stateLabel.implicitWidth + Theme.space2
					Layout.preferredHeight: 20
					radius: height / 2
					color: Theme.withAlpha(root.toneColor, 0.13)
					Accessible.ignored: true
					Label {
						id: stateLabel
						anchors.centerIn: parent
						textFormat: Text.PlainText
						text: root.modeLabel
						color: root.toneColor
						font.pixelSize: Theme.fontCaption
						font.weight: Font.DemiBold
						Accessible.ignored: true
					}
				}
			}

			Label {
				objectName: "activeScopeScreenShareDetail"
				Layout.fillWidth: true
				textFormat: Text.PlainText
				text: root.technicalDetail.length > 0 ? root.technicalDetail
					: (root.statusLabel.length > 0 ? root.statusLabel : root.scopeLabel)
				color: root.mode === "error" ? Theme.danger : Theme.textMuted
				font.pixelSize: Theme.fontCaption
				elide: Text.ElideRight
				visible: !root.narrowLayout || text.length > 0
				Accessible.ignored: true
			}
		}

		ModernButton {
			id: primaryButton
			objectName: "activeScopeScreenSharePrimaryAction"
			Layout.alignment: Qt.AlignVCenter
			visible: String(root.share.primaryActionId || "").length > 0
			enabled: root.share.primaryEnabled === undefined || !!root.share.primaryEnabled
			dense: true
			text: String(root.share.primaryLabel || qsTr("Open share"))
			tone: String(root.share.primaryTone || root.statusTone || "accent")
			highlighted: root.mode !== "error"
			Accessible.description: String(root.share.primaryHint || root.statusLabel || "")
			onClicked: root.actionRequested(String(root.share.primaryActionId || ""))
		}

		ModernIconButton {
			id: overflowButton
			objectName: "activeScopeScreenShareMoreActions"
			Layout.alignment: Qt.AlignVCenter
			visible: root.actionItems.length > 0
			dense: true
			iconName: "more"
			Accessible.name: qsTr("More screen share actions")
			onClicked: overflowMenu.popup(overflowButton, 0, overflowButton.height)
			PayloadMenu {
				id: overflowMenu
				objectName: "activeScopeScreenShareMenu"
				entries: root.actionItems
				onActionRequested: function(actionId) { root.actionRequested(actionId) }
			}
		}
	}
}
