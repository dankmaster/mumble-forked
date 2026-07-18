import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

ColumnLayout {
	id: root

	property string title: ""
	property string pickerLabel: ""
	property string objectNamePrefix: "aclMembers"
	property string excludedListName: ""
	property var memberIds: []
	property var excludedIds: []
	property var userOptions: []
	property bool editable: true
	property bool compact: false
	readonly property var normalizedMemberIds: normalizedIds(memberIds)
	readonly property var normalizedExcludedIds: normalizedIds(excludedIds)
	readonly property var pickerOptions: buildPickerOptions()
	readonly property int memberCount: normalizedMemberIds.length

	signal membersEdited(var memberIds, string requestedFocusObjectName)

	objectName: objectNamePrefix
	Layout.fillWidth: true
	Layout.minimumWidth: 0
	spacing: Theme.space1

	function normalizedId(value) {
		const number = Number(value)
		return isFinite(number) && number >= 0 && Math.floor(number) === number ? number : -1
	}

	function normalizedIds(values) {
		const result = []
		const seen = ({})
		const source = values || []
		for (let index = 0; index < source.length; ++index) {
			const id = normalizedId(source[index])
			const key = String(id)
			if (id >= 0 && !seen[key]) {
				seen[key] = true
				result.push(id)
			}
		}
		return result
	}

	function containsId(values, userId) {
		const id = normalizedId(userId)
		const source = values || []
		for (let index = 0; index < source.length; ++index) {
			if (normalizedId(source[index]) === id)
				return true
		}
		return false
	}

	function optionId(option) {
		const candidate = option && option.value !== undefined ? option.value
			: option && option.id !== undefined ? option.id : -1
		return normalizedId(candidate)
	}

	function optionForId(userId) {
		const id = normalizedId(userId)
		const options = userOptions || []
		for (let index = 0; index < options.length; ++index) {
			const option = options[index] || ({})
			if (optionId(option) === id)
				return option
		}
		return ({})
	}

	function userName(userId) {
		const option = optionForId(userId)
		const label = String(option.label || "").trim()
		return label.length > 0 ? label : qsTr("Unknown user")
	}

	function isKnownUser(userId) {
		return optionId(optionForId(userId)) >= 0
	}

	function tokenObjectName(userId) {
		return objectNamePrefix + "_token_" + String(normalizedId(userId))
	}

	function removeObjectName(userId) {
		return objectNamePrefix + "_remove_" + String(normalizedId(userId))
	}

	function buildPickerOptions() {
		const result = []
		const seen = ({})
		const options = userOptions || []
		for (let index = 0; index < options.length; ++index) {
			const option = options[index] || ({})
			const id = optionId(option)
			const key = String(id)
			if (id < 0 || seen[key])
				continue
			seen[key] = true
			const alreadySelected = containsId(normalizedMemberIds, id)
			const excluded = containsId(normalizedExcludedIds, id)
			const label = String(option.label || qsTr("Unknown user"))
			let hint = String(option.hint || "")
			if (alreadySelected)
				hint = qsTr("Already in %1").arg(title)
			else if (excluded)
				hint = qsTr("Already in %1").arg(excludedListName)
			result.push({
				"id": id,
				"value": id,
				"label": qsTr("%1 · ID %2").arg(label).arg(id),
				"userLabel": label,
				"subtitle": qsTr("ID %1").arg(id),
				"enabled": editable && !alreadySelected && !excluded,
				"hint": hint
			})
		}
		return result
	}

	function addOption(option) {
		const id = optionId(option)
		if (!editable || id < 0 || option.enabled === false
				|| containsId(normalizedMemberIds, id) || containsId(normalizedExcludedIds, id))
			return
		const next = normalizedMemberIds.slice(0)
		next.push(id)
		membersEdited(next, objectNamePrefix + "_picker")
	}

	function removeAt(index) {
		if (!editable || index < 0 || index >= normalizedMemberIds.length)
			return
		const next = normalizedMemberIds.slice(0)
		next.splice(index, 1)
		const nextIndex = Math.min(index, next.length - 1)
		const focusName = nextIndex >= 0
			? removeObjectName(next[nextIndex]) : objectNamePrefix + "_picker"
		membersEdited(next, focusName)
	}

	RowLayout {
		Layout.fillWidth: true
		spacing: Theme.space2

		Label {
			Layout.fillWidth: true
			textFormat: Text.PlainText
			text: root.title
			color: Theme.textMuted
			font.pixelSize: Theme.fontCaption
			font.weight: Font.Medium
		}
		Label {
			visible: root.memberCount > 0
			textFormat: Text.PlainText
			text: String(root.memberCount)
			color: Theme.textMuted
			font.pixelSize: Theme.fontCaption
			Accessible.ignored: true
		}
	}

	Flow {
		id: memberFlow
		objectName: root.objectNamePrefix + "_tokens"
		Layout.fillWidth: true
		Layout.minimumWidth: 0
		Layout.preferredHeight: Math.max(emptyMembersLabel.visible ? emptyMembersLabel.implicitHeight : 0,
			childrenRect.height)
		spacing: Theme.space1
		Accessible.role: Accessible.List
		Accessible.name: root.title

		Label {
			id: emptyMembersLabel
			visible: root.memberCount === 0
			textFormat: Text.PlainText
			text: qsTr("No users selected")
			color: Theme.textFaint
			font.pixelSize: Theme.fontCaption
			Accessible.ignored: true
		}

		Repeater {
			model: root.normalizedMemberIds
			delegate: Rectangle {
				id: token
				required property var modelData
				required property int index
				readonly property int userId: root.normalizedId(modelData)
				readonly property string userLabel: root.userName(userId)
				readonly property bool knownUser: root.isKnownUser(userId)

				objectName: root.tokenObjectName(userId)
				width: root.compact ? memberFlow.width
					: Math.min(memberFlow.width, Math.max(132, tokenContent.implicitWidth + Theme.space2 * 2))
				height: Math.max(38, tokenContent.implicitHeight + Theme.space1 * 2)
				radius: Theme.innerRadius
				color: Theme.surfaceRaised
				border.color: knownUser ? Theme.divider : Theme.warning
				border.width: 1
				Accessible.role: Accessible.ListItem
				Accessible.name: qsTr("%1, ID %2").arg(userLabel).arg(userId)
				Accessible.description: knownUser ? qsTr("Registered user")
					: qsTr("Unknown user retained from the server")

				RowLayout {
					id: tokenContent
					anchors.fill: parent
					anchors.leftMargin: Theme.space2
					anchors.rightMargin: Theme.space1
					spacing: Theme.space2

					ModernIcon {
						name: "user"
						size: 16
						color: token.knownUser ? Theme.textMain : Theme.warning
					}
					ColumnLayout {
						Layout.fillWidth: true
						Layout.minimumWidth: 0
						spacing: 0
						Label {
							Layout.fillWidth: true
							textFormat: Text.PlainText
							text: token.userLabel
							color: Theme.textStrong
							font.pixelSize: Theme.fontLabel
							font.weight: Font.Medium
							elide: Text.ElideRight
						}
						Label {
							Layout.fillWidth: true
							textFormat: Text.PlainText
							text: qsTr("ID %1").arg(token.userId)
							color: Theme.textMuted
							font.pixelSize: Theme.fontCaption
							elide: Text.ElideRight
						}
					}
					ModernIconButton {
						objectName: root.removeObjectName(token.userId)
						visible: root.editable
						enabled: root.editable
						dense: true
						iconName: "close"
						text: qsTr("Remove %1 (ID %2) from %3")
							.arg(token.userLabel).arg(token.userId).arg(root.title)
						ToolTip.visible: hovered
						ToolTip.text: text
						Keys.onReturnPressed: event => {
							if (enabled) clicked()
							event.accepted = true
						}
						Keys.onEnterPressed: event => {
							if (enabled) clicked()
							event.accepted = true
						}
						onClicked: root.removeAt(token.index)
					}
				}
			}
		}
	}

	Label {
		objectName: root.objectNamePrefix + "_pickerLabel"
		Layout.fillWidth: true
		visible: root.editable
		textFormat: Text.PlainText
		text: root.pickerLabel.length > 0 ? root.pickerLabel : qsTr("Add a member")
		color: Theme.textMuted
		font.pixelSize: Theme.fontCaption
		font.weight: Font.Medium
	}

	ModernComboBox {
		id: picker
		objectName: root.objectNamePrefix + "_picker"
		Layout.fillWidth: true
		Layout.minimumWidth: 0
		model: root.pickerOptions
		textRole: "label"
		valueRole: "value"
		currentIndex: -1
		dense: true
		enabled: root.editable && root.pickerOptions.length > 0
		displayText: currentIndex >= 0 ? currentText : qsTr("Add a registered user…")
		Accessible.name: qsTr("Add user to %1").arg(root.title)
		Accessible.description: qsTr("Choose a registered user by name and numeric ID")
		onActivated: index => {
			const selected = optionAt(index)
			currentIndex = -1
			root.addOption(selected)
		}
	}
}
