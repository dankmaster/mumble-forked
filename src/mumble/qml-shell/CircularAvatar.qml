import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import Mumble.Theme 1.0

Item {
	id: root

	property url source: ""
	property string fallbackText: "?"
	property color backgroundColor: Theme.strip
	property color fallbackColor: Theme.textStrong
	property color borderColor: Theme.quietBorder
	property real borderWidth: 1
	property bool highlighted: false
	property color highlightColor: Theme.success
	property bool avatarVisible: true
	property bool animationsEnabled: true

	// Callers can keep their existing automation identifiers while sharing the
	// actual avatar implementation.
	property string imageObjectName: ""
	property string imageEffectObjectName: ""
	property string fallbackObjectName: ""
	property string maskObjectName: ""
	property string haloObjectName: ""
	property string ringObjectName: ""

	readonly property real radius: Math.min(width, height) / 2
	readonly property bool imageReady: avatarImage.status === Image.Ready
	readonly property int imageStatus: avatarImage.status
	readonly property int sourcePixelSize: Math.max(1,
		Math.ceil(Math.min(width, height) * 2))

	implicitWidth: Theme.avatarMedium
	implicitHeight: implicitWidth

	Rectangle {
		id: activityHalo
		objectName: root.haloObjectName
		anchors.centerIn: parent
		width: Math.min(root.width, root.height) + 6
		height: width
		radius: width / 2
		color: Theme.withAlpha(root.highlightColor, 0.15)
		border.width: 1
		border.color: Theme.withAlpha(root.highlightColor, 0.42)
		opacity: root.highlighted ? 1 : 0
		scale: root.highlighted ? 1 : 0.86
		visible: root.avatarVisible
		antialiasing: true
		Accessible.ignored: true

		Behavior on opacity {
			enabled: root.animationsEnabled
			NumberAnimation {
				duration: root.highlighted ? Theme.motionNormal : Theme.motionFast
				easing.type: Easing.OutCubic
			}
		}
		Behavior on scale {
			enabled: root.animationsEnabled
			NumberAnimation {
				duration: Theme.motionNormal
				easing.type: Easing.OutCubic
			}
		}
	}

	Rectangle {
		id: avatarRing
		objectName: root.ringObjectName
		anchors.fill: parent
		radius: root.radius
		color: root.backgroundColor
		border.width: root.highlighted ? 2 : root.borderWidth
		border.color: root.highlighted ? root.highlightColor : root.borderColor
		visible: root.avatarVisible
		antialiasing: true
		Accessible.ignored: true

		Behavior on border.color {
			enabled: root.animationsEnabled
			ColorAnimation { duration: Theme.motionFast }
		}
		Behavior on border.width {
			enabled: root.animationsEnabled
			NumberAnimation { duration: Theme.motionFast }
		}
	}

	Item {
		id: avatarViewport
		anchors.fill: parent
		anchors.margins: root.highlighted ? 3 : Math.max(1, root.borderWidth)
		visible: root.avatarVisible
		Accessible.ignored: true

		Image {
			id: avatarImage
			objectName: root.imageObjectName
			anchors.fill: parent
			source: root.source
			sourceSize: Qt.size(root.sourcePixelSize, root.sourcePixelSize)
			asynchronous: true
			cache: false
			smooth: true
			fillMode: Image.PreserveAspectCrop
			visible: false
			Accessible.ignored: true
		}

		Rectangle {
			id: avatarMask
			objectName: root.maskObjectName
			anchors.fill: parent
			radius: Math.min(width, height) / 2
			color: "white"
			visible: false
			antialiasing: true
			layer.enabled: true
			Accessible.ignored: true
		}

		MultiEffect {
			id: avatarImageEffect
			objectName: root.imageEffectObjectName
			anchors.fill: parent
			autoPaddingEnabled: false
			visible: root.avatarVisible && avatarImage.status === Image.Ready
			source: avatarImage
			maskEnabled: true
			maskSource: avatarMask
		}

		Label {
			objectName: root.fallbackObjectName
			anchors.centerIn: parent
			visible: root.avatarVisible && avatarImage.status !== Image.Ready
			textFormat: Text.PlainText
			text: root.fallbackText
			color: root.fallbackColor
			font.pixelSize: Math.max(10, Math.round(Math.min(root.width, root.height) * 0.38))
			font.bold: true
			Accessible.ignored: true
		}
	}
}
