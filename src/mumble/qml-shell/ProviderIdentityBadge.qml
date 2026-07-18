pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Mumble.Theme 1.0
import Mumble.ProviderPresentation 1.0

Rectangle {
	id: root

	property string providerToken: ""
	property string badgeText: ""
	property string presentation: "inline"
	property string labelObjectName: ""
	property color accent: resolvedIdentity.known ? resolvedIdentity.accent : Theme.accent
	property color foreground: Theme.contrastText(accent)
	property real fillOpacity: presentation === "overlay" ? 0.92 : 0.14
	property real borderOpacity: presentation === "overlay" ? 0 : 0.46
	property int markExtent: 42
	property bool uppercase: presentation !== "overlay"
	readonly property var resolvedIdentity: ProviderPresentation.resolve(providerToken)
	readonly property string displayText: badgeText.length > 0 ? badgeText
		: presentation === "mark" ? resolvedIdentity.mark : resolvedIdentity.label
	readonly property int horizontalPadding: presentation === "mark" ? Theme.space1 : Theme.space2

	implicitWidth: presentation === "mark" ? markExtent
		: badgeLabel.implicitWidth + horizontalPadding * 2
	implicitHeight: presentation === "mark" ? markExtent : Theme.space5
	radius: presentation === "mark" ? Theme.innerRadius : height / 2
	color: withAlpha(accent, fillOpacity)
	border.width: borderOpacity > 0 ? 1 : 0
	border.color: withAlpha(accent, borderOpacity)
	Accessible.ignored: true

	function withAlpha(value, alpha) {
		return Qt.rgba(value.r, value.g, value.b, alpha)
	}

	Label {
		id: badgeLabel
		objectName: root.labelObjectName
		anchors.fill: parent
		anchors.leftMargin: root.horizontalPadding
		anchors.rightMargin: root.horizontalPadding
		text: root.uppercase ? root.displayText.toUpperCase() : root.displayText
		textFormat: Text.PlainText
		color: root.foreground
		font.pixelSize: Theme.fontCaption
		font.bold: true
		font.letterSpacing: root.presentation === "inline" ? 0.6 : 0
		elide: Text.ElideRight
		horizontalAlignment: Text.AlignHCenter
		verticalAlignment: Text.AlignVCenter
		Accessible.ignored: true
	}
}
