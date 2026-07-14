import QtQuick
import QtQuick.Controls
import Mumble.Theme 1.0

ProgressBar {
	id: control

	property string tone: "accent"
	property int trackHeight: Theme.compact ? 4 : 5
	readonly property color indicatorColor: tone === "danger" ? Theme.danger
		: tone === "warning" ? Theme.warning
		: tone === "success" ? Theme.success : Theme.accent

	implicitWidth: 160
	implicitHeight: Math.max(trackHeight, 6)
	Accessible.role: Accessible.ProgressBar

	background: Rectangle {
		x: 0
		y: Math.round((control.height - height) / 2)
		implicitWidth: 160
		implicitHeight: control.trackHeight
		width: control.width
		height: control.trackHeight
		radius: height / 2
		color: Theme.surfaceBorder
		opacity: control.enabled ? 0.72 : 0.42
	}

	contentItem: Item {
		implicitWidth: 160
		implicitHeight: control.trackHeight
		clip: true

		Rectangle {
			objectName: "modernProgressDeterminateFill"
			x: 0
			y: Math.round((parent.height - height) / 2)
			width: Math.max(0, parent.width * control.visualPosition)
			height: control.trackHeight
			radius: height / 2
			visible: !control.indeterminate
			color: control.indicatorColor
			opacity: control.enabled ? 1 : 0.55
			Behavior on width {
				NumberAnimation { duration: Theme.motionNormal; easing.type: Easing.OutCubic }
			}
		}

		Rectangle {
			id: indeterminatePill
			objectName: "modernProgressIndeterminatePill"
			y: Math.round((parent.height - height) / 2)
			width: Math.max(28, Math.min(parent.width * 0.32, 72))
			height: control.trackHeight
			radius: height / 2
			visible: control.indeterminate
			color: control.indicatorColor
			opacity: control.enabled ? 1 : 0.55

			SequentialAnimation on x {
				running: control.visible && control.indeterminate
				loops: Animation.Infinite
				NumberAnimation {
					from: -indeterminatePill.width
					to: indeterminatePill.parent ? indeterminatePill.parent.width : 0
					duration: Math.max(900, Theme.motionSlow * 5)
					easing.type: Easing.InOutCubic
				}
				PauseAnimation { duration: Theme.motionFast }
			}
		}
	}
}
