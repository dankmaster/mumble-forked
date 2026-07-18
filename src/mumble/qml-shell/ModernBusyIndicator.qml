import QtQuick
import Mumble.Theme 1.0

Item {
	id: control

	property bool running: true
	property bool animated: true
	property string tone: "accent"
	property int segmentCount: 10
	readonly property color indicatorColor: tone === "danger" ? Theme.danger
		: tone === "warning" ? Theme.warning
		: tone === "success" ? Theme.success : Theme.accent
	readonly property real segmentWidth: Math.max(2, Math.round(Math.min(width, height) / 10))
	readonly property real segmentHeight: Math.max(segmentWidth * 2,
		Math.round(Math.min(width, height) * 0.24))

	implicitWidth: Theme.compact ? 24 : 28
	implicitHeight: implicitWidth
	opacity: running ? 1 : 0
	transformOrigin: Item.Center
	Accessible.role: Accessible.ProgressBar
	Accessible.description: qsTr("In progress")
	Accessible.ignored: !visible || !running

	Behavior on opacity {
		enabled: control.animated
		NumberAnimation { duration: Theme.motionFast; easing.type: Easing.OutCubic }
	}

	Repeater {
		model: Math.max(6, control.segmentCount)
		delegate: Item {
			required property int index
			anchors.fill: parent
			rotation: index * (360 / Math.max(6, control.segmentCount))

			Rectangle {
				anchors.horizontalCenter: parent.horizontalCenter
				y: 0
				width: control.segmentWidth
				height: control.segmentHeight
				radius: width / 2
				color: control.indicatorColor
				opacity: Math.max(0.22, 1 - (parent.index / Math.max(6, control.segmentCount)) * 0.78)
			}
		}
	}

	RotationAnimator on rotation {
		from: 0
		to: 360
		duration: Math.max(720, Theme.motionSlow * 4)
		loops: Animation.Infinite
		running: control.animated && control.running && control.visible
		onRunningChanged: if (!running) control.rotation = 0
	}

	onAnimatedChanged: if (!animated) rotation = 0
	onRunningChanged: if (!running) rotation = 0
	onVisibleChanged: if (!visible) rotation = 0
}
