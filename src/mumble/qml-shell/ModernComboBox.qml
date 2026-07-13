import QtQuick
import QtQuick.Controls
import Mumble.Theme 1.0

ComboBox {
	id: control
	property bool invalid: false
	property bool dense: false

	Accessible.role: Accessible.ComboBox
	hoverEnabled: true
	implicitHeight: dense ? Math.max(30, Theme.controlHeight - 4) : Theme.controlHeight
	leftPadding: Theme.space3
	rightPadding: indicator.width + Theme.space3
	font.pixelSize: Theme.fontBody

	delegate: ItemDelegate {
		required property int index
		width: ListView.view ? ListView.view.width : control.width
		height: Theme.rowHeight
		highlighted: control.highlightedIndex === index
		contentItem: Text {
			text: control.textAt(index)
			color: parent.highlighted ? Theme.textStrong : Theme.textMain
			font.pixelSize: Theme.fontBody
			verticalAlignment: Text.AlignVCenter
			elide: Text.ElideRight
		}
		background: Rectangle {
			radius: Theme.innerRadius
			color: parent.highlighted ? Theme.accentSubtle : parent.hovered ? Theme.surfaceHover : "transparent"
		}
	}

	indicator: Text {
		x: control.width - width - Theme.space3
		y: Math.round((control.height - height) / 2)
		text: "⌄"
		color: control.enabled ? Theme.textMain : Theme.textMuted
		font.pixelSize: 16
	}

	contentItem: Text {
		leftPadding: 0
		rightPadding: 0
		text: control.displayText
		font: control.font
		color: control.enabled ? Theme.textStrong : Theme.textMuted
		verticalAlignment: Text.AlignVCenter
		elide: Text.ElideRight
	}

	background: Rectangle {
		radius: Theme.innerRadius
		color: control.enabled ? Theme.surfaceRaised : Theme.panel
		border.color: control.invalid ? Theme.danger
			: control.activeFocus ? Theme.focus
			: control.hovered ? Theme.surfaceBorder : Theme.divider
		border.width: control.invalid || control.activeFocus ? Theme.focusRingWidth : 1
		Behavior on border.color { ColorAnimation { duration: Theme.motionFast } }
	}

	popup: Popup {
		y: control.height + Theme.space1
		width: control.width
		implicitHeight: Math.min(contentItem.implicitHeight + padding * 2, 300)
		padding: Theme.space1
		closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
		contentItem: ListView {
			clip: true
			implicitHeight: contentHeight
			model: control.popup.visible ? control.delegateModel : null
			currentIndex: control.highlightedIndex
			ScrollIndicator.vertical: ScrollIndicator {}
		}
		background: Rectangle {
			color: Theme.surfaceRaised
			border.color: Theme.surfaceBorder
			border.width: 1
			radius: Theme.innerRadius
		}
	}
}
