import QtQuick
import QtQuick.Controls
import Mumble.Theme 1.0

FocusScope {
	id: control

	property var model: []
	property var currentValue: undefined
	property int currentIndex: -1
	property string accessibleName: ""
	property string accessibleDescription: ""
	property string optionObjectNamePrefix: "modernSegmentOption"
	readonly property int optionCount: model && model.length !== undefined ? model.length : 0

	signal activated(int index, var value)

	implicitWidth: 240
	implicitHeight: Theme.controlHeight
	activeFocusOnTab: activeFocus || (enabled && optionCount > 0)
	clip: true

	Accessible.role: Accessible.Grouping
	Accessible.name: accessibleName
	Accessible.description: accessibleDescription

	function optionAt(index) {
		if (index < 0 || index >= optionCount)
			return ({})
		const option = model[index]
		return option && typeof option === "object" ? option : ({ "label": String(option), "value": option })
	}

	function optionEnabled(index) {
		const option = optionAt(index)
		return enabled && (option.enabled === undefined || Boolean(option.enabled))
	}

	function indexOfValue(value) {
		for (let index = 0; index < optionCount; ++index) {
			const optionValue = optionAt(index).value
			if (optionValue === value || (optionValue !== undefined && value !== undefined
					&& String(optionValue) === String(value)))
				return index
		}
		return -1
	}

	function setCurrentValue(value) {
		currentValue = value
		currentIndex = indexOfValue(value)
	}

	function adjacentEnabledIndex(origin, direction) {
		if (optionCount <= 0)
			return -1
		let index = origin >= 0 ? origin : (direction > 0 ? -1 : optionCount)
		for (let attempt = 0; attempt < optionCount; ++attempt) {
			index = (index + direction + optionCount) % optionCount
			if (optionEnabled(index))
				return index
		}
		return -1
	}

	function boundaryEnabledIndex(direction) {
		for (let offset = 0; offset < optionCount; ++offset) {
			const index = direction > 0 ? offset : optionCount - offset - 1
			if (optionEnabled(index))
				return index
		}
		return -1
	}

	function activateIndex(index, reason) {
		if (!optionEnabled(index))
			return
		const value = optionAt(index).value
		currentIndex = index
		currentValue = value
		forceActiveFocus(reason === undefined ? Qt.OtherFocusReason : reason)
		activated(index, value)
	}

	onModelChanged: Qt.callLater(function() { setCurrentValue(currentValue) })
	Component.onCompleted: setCurrentValue(currentValue)

	Keys.onPressed: event => {
		let index = -1
		if (event.key === Qt.Key_Left)
			index = adjacentEnabledIndex(currentIndex, -1)
		else if (event.key === Qt.Key_Right)
			index = adjacentEnabledIndex(currentIndex, 1)
		else if (event.key === Qt.Key_Home)
			index = boundaryEnabledIndex(1)
		else if (event.key === Qt.Key_End)
			index = boundaryEnabledIndex(-1)
		else if (event.key === Qt.Key_Space || event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
			index = currentIndex
		else
			return
		event.accepted = true
		if (index >= 0)
			activateIndex(index, Qt.TabFocusReason)
	}

	Rectangle {
		anchors.fill: parent
		radius: Theme.innerRadius
		color: control.enabled ? Theme.strip : Theme.panel
		border.color: control.activeFocus ? Theme.focus : Theme.divider
		border.width: control.activeFocus ? Theme.focusRingWidth : 1
		Behavior on border.color { ColorAnimation { duration: Theme.motionFast } }
	}

	Row {
		id: optionRow
		anchors.fill: parent
		anchors.margins: 2
		spacing: 2

		function optionWidth(index) {
			if (control.optionCount <= 0)
				return 0
			const totalSpacing = Math.max(0, control.optionCount - 1) * spacing
			const available = Math.max(0, width - totalSpacing)
			const base = Math.floor(available / control.optionCount)
			return index === control.optionCount - 1
				? available - base * (control.optionCount - 1) : base
		}

		Repeater {
			model: control.model || []
			delegate: ModernButton {
				id: segmentButton
				required property int index
				required property var modelData
				readonly property var option: control.optionAt(index)
				readonly property string optionDescription: String(option.hint || option.unavailableReason || "")

				objectName: control.optionObjectNamePrefix + "_" + String(option.value === undefined ? index : option.value)
				width: optionRow.optionWidth(index)
				height: optionRow.height
				text: String(option.label === undefined ? option.value : option.label)
				dense: true
				checkable: true
				checked: control.currentIndex === index
				highlighted: checked
				tone: checked ? "accent" : "neutral"
				enabled: control.optionEnabled(index)
				activeFocusOnTab: false
				Accessible.role: Accessible.RadioButton
				Accessible.name: text
				Accessible.description: optionDescription
				Accessible.checked: checked
				ToolTip.visible: hovered && (optionDescription.length > 0
					|| (contentItem && contentItem.implicitWidth > contentItem.width))
				ToolTip.text: optionDescription.length > 0 ? optionDescription : text
				onClicked: control.activateIndex(index, Qt.MouseFocusReason)
			}
		}
	}
}
