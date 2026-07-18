import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

Item {
	id: root
	property var controller: null
	property bool animationsEnabled: true
	property int maximumWidth: 680
	readonly property bool controllerVisible: !!controller && !!controller.visible
	readonly property string toastTitle: controller ? String(controller.title || "") : ""
	readonly property string toastMessage: controller ? String(controller.message || "") : ""
	readonly property string toastTone: controller ? String(controller.tone || "info").toLowerCase() : "info"
	readonly property color toneColor: toastTone === "danger" || toastTone === "error" ? Theme.danger
		: toastTone === "warning" ? Theme.warning
		: toastTone === "success" ? Theme.success : Theme.accent
	readonly property string toneIcon: toastTone === "danger" || toastTone === "error" || toastTone === "warning"
		? "warning" : toastTone === "success" ? "check" : "info"
	readonly property bool interactionActive: hoverHandler.hovered
		|| actionButton.activeFocus || dismissButton.activeFocus
	readonly property string accessibilityText: {
		const parts = [toastTitle, toastMessage,
			controller && controller.repeatCount > 1 ? qsTr("Repeated %1 times").arg(controller.repeatCount) : ""]
			.filter(value => String(value).length > 0)
		let text = ""
		for (let index = 0; index < parts.length; ++index) {
			if (text.length > 0)
				text += /[.!?]$/.test(text) ? " " : ". "
			text += String(parts[index])
		}
		return text
	}

	signal actionRequested(string actionId)

	objectName: "modernToastPill"
	implicitWidth: Math.min(maximumWidth, Math.max(280, contentRow.implicitWidth + Theme.space4 * 2))
	implicitHeight: Math.max(46, contentRow.implicitHeight + Theme.space2 * 2)
	visible: controllerVisible || opacity > 0.01
	enabled: controllerVisible
	opacity: controllerVisible ? 1 : 0
	property real revealOffset: controllerVisible ? 0 : 8
	z: 96
	Accessible.role: Accessible.AlertMessage
	Accessible.name: accessibilityText
	Accessible.focusable: false

	onInteractionActiveChanged: {
		if (controller && typeof controller.setInteractionActive === "function")
			controller.setInteractionActive(interactionActive)
	}
	Component.onDestruction: {
		if (controller && typeof controller.setInteractionActive === "function")
			controller.setInteractionActive(false)
	}

	Behavior on opacity {
		enabled: root.animationsEnabled
		NumberAnimation { duration: Theme.motionNormal; easing.type: Easing.OutCubic }
	}
	Behavior on revealOffset {
		enabled: root.animationsEnabled
		NumberAnimation { duration: Theme.motionNormal; easing.type: Easing.OutCubic }
	}
	transform: Translate { y: root.revealOffset }

	Rectangle {
		x: pillBackground.x
		y: pillBackground.y + Theme.elevationMenuOffset
		width: pillBackground.width
		height: pillBackground.height
		radius: pillBackground.radius
		color: Theme.elevationShadow
		opacity: 0.42
		Accessible.ignored: true
	}

	Rectangle {
		id: pillBackground
		objectName: "toastPillBackground"
		anchors.fill: parent
		radius: height / 2
		color: Theme.surfaceRaised
		border.color: root.activeFocus ? Theme.focus : Theme.surfaceBorder
		border.width: root.activeFocus ? Theme.focusRingWidth : 1
	}

	RowLayout {
		id: contentRow
		anchors.fill: parent
		anchors.leftMargin: Theme.space3
		anchors.rightMargin: Theme.space2
		anchors.topMargin: Theme.space2
		anchors.bottomMargin: Theme.space2
		spacing: Theme.space2

		Rectangle {
			Layout.preferredWidth: 24
			Layout.preferredHeight: 24
			radius: 12
			color: Theme.withAlpha(root.toneColor, 0.16)
			border.color: Theme.withAlpha(root.toneColor, 0.45)
			border.width: 1
			ModernIcon {
				anchors.centerIn: parent
				name: root.toneIcon
				size: 14
				color: root.toneColor
			}
		}

		Label {
			id: titleLabel
			objectName: "toastTitleLabel"
			visible: text.length > 0
			Layout.maximumWidth: visible ? Math.min(220, implicitWidth) : 0
			text: root.toastTitle
			textFormat: Text.PlainText
			color: Theme.textStrong
			font.pixelSize: Theme.fontLabel
			font.weight: Font.DemiBold
			elide: Text.ElideRight
			maximumLineCount: 1
			Accessible.ignored: true
		}

		Rectangle {
			visible: titleLabel.visible && messageLabel.visible
			Layout.preferredWidth: 1
			Layout.preferredHeight: 16
			color: Theme.divider
			Accessible.ignored: true
		}

		Label {
			id: messageLabel
			objectName: "toastMessageLabel"
			visible: text.length > 0
			Layout.fillWidth: true
			Layout.minimumWidth: visible ? 72 : 0
			text: root.toastMessage
			textFormat: Text.PlainText
			color: Theme.textMain
			font.pixelSize: Theme.fontBody
			elide: Text.ElideRight
			maximumLineCount: 1
			Accessible.ignored: true
		}

		Label {
			id: repeatBadge
			objectName: "toastRepeatBadge"
			visible: controller && controller.repeatCount > 1
			Layout.preferredHeight: 24
			Layout.minimumWidth: 30
			leftPadding: Theme.space2
			rightPadding: Theme.space2
			text: "\u00d7" + String(controller ? controller.repeatCount : 1)
			textFormat: Text.PlainText
			color: Theme.textStrong
			font.pixelSize: Theme.fontCaption
			font.weight: Font.DemiBold
			horizontalAlignment: Text.AlignHCenter
			verticalAlignment: Text.AlignVCenter
			background: Rectangle {
				radius: height / 2
				color: Theme.surfaceHover
				border.color: Theme.quietBorder
			}
			Accessible.ignored: true
		}

		ModernButton {
			id: actionButton
			objectName: "toastActionButton"
			visible: controller && String(controller.actionId || "").length > 0
			text: controller && String(controller.actionLabel || "").length > 0
				? String(controller.actionLabel) : qsTr("Open")
			dense: true
			tone: "neutral"
			Accessible.description: qsTr("Action for this notification")
			onClicked: {
				const actionId = controller ? String(controller.actionId || "") : ""
				if (actionId.length === 0)
					return
				root.actionRequested(actionId)
				controller.dismiss()
			}
		}

		ModernIconButton {
			id: dismissButton
			objectName: "toastDismissButton"
			dense: true
			iconName: "close"
			text: qsTr("Dismiss notification")
			Accessible.description: root.toastTitle
			ToolTip.visible: hovered
			ToolTip.text: text
			onClicked: if (controller) controller.dismiss()
		}
	}

	HoverHandler { id: hoverHandler }
}
