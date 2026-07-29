pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

Button {
	id: root

	property string sourceUrl: ""
	property string providerLabel: ""
	property color accent: Theme.accent
	signal openRequested(string url)

	visible: sourceUrl.length > 0
	focusPolicy: Qt.StrongFocus
	hoverEnabled: true
	// A full source URL must remain readable, but its intrinsic width must not
	// be allowed to widen the owning card. The parent layout supplies the real
	// width and the URL grows vertically inside that bound.
	implicitWidth: Math.min(520, sourceLayout.implicitWidth + Theme.space3 * 2)
	implicitHeight: Math.max(Theme.controlHeight,
		sourceLayout.implicitHeight + Theme.space2 * 2)
	clip: true
	Accessible.role: Accessible.Link
	Accessible.name: providerLabel.length > 0
		? qsTr("Open source on %1").arg(providerLabel) : qsTr("Open source")
	Accessible.description: sourceUrl

	background: Rectangle {
		radius: Theme.innerRadius
		color: root.down ? Theme.embedSelection
			: root.hovered ? Theme.withAlpha(root.accent, 0.10) : Theme.embedSurface
		border.color: root.activeFocus ? Theme.focus : Theme.embedBorder
		border.width: root.activeFocus ? Theme.focusRingWidth : 1
	}

	contentItem: RowLayout {
		id: sourceLayout
		width: root.availableWidth
		spacing: Theme.space2

		ModernIcon {
			Layout.alignment: Qt.AlignTop
			Layout.topMargin: 1
			name: "link"
			size: Theme.avatarSmall
			color: root.accent
			Accessible.ignored: true
		}

		ColumnLayout {
			Layout.fillWidth: true
			Layout.minimumWidth: 0
			spacing: 0

			Label {
				Layout.fillWidth: true
				visible: root.providerLabel.length > 0
				text: root.providerLabel
				textFormat: Text.PlainText
				color: Theme.textMuted
				font.pixelSize: Theme.fontCaption
				font.weight: Font.DemiBold
			}

			Label {
				objectName: "embedSourceUrl"
				Layout.fillWidth: true
				text: root.sourceUrl
				textFormat: Text.PlainText
				color: Theme.accentHover
				font.pixelSize: Theme.fontCaption
				wrapMode: Text.WrapAnywhere
				elide: Text.ElideNone
			}
		}

		ModernIcon {
			Layout.alignment: Qt.AlignTop
			Layout.topMargin: 1
			name: "external"
			size: Theme.avatarSmall
			color: Theme.textMuted
			Accessible.ignored: true
		}
	}

	onClicked: if (sourceUrl.length > 0) openRequested(sourceUrl)
	Keys.onPressed: function(event) {
		if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter
				|| event.key === Qt.Key_Space) {
			if (root.sourceUrl.length > 0)
				root.openRequested(root.sourceUrl)
			event.accepted = true
		}
	}
}
