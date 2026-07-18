import QtQuick
import QtTest
import Mumble.Theme 1.0

TestCase {
	id: testCase
	name: "ScreenShareScopeCard"
	when: windowShown
	visible: true
	width: 760
	height: 220

	property var publishingShare: ({
		"visible": true,
		"streamId": "stream:lobby",
		"mode": "publishing",
		"ownerLabel": "Alex",
		"statusLabel": "You are sharing in this room",
		"statusTone": "success",
		"resolutionLabel": "1920x1080 @ 30 fps",
		"bitrateKbps": 4500,
		"qualityProfile": "high_quality",
		"runtimeLabel": "GStreamer GPU",
		"primaryActionId": "screenShareOpenWindow",
		"primaryLabel": "Manage share",
		"primaryTone": "success",
		"primaryEnabled": true,
		"overflowActions": [{
			"kind": "action", "id": "screenShareStop",
			"label": "Stop sharing", "enabled": true, "tone": "danger"
		}]
	})

	function createCard(cardWidth, narrow) {
		const component = Qt.createComponent("qrc:/qml-shell/ScreenShareScopeCard.qml")
		compare(component.status, Component.Ready, component.errorString())
		const card = component.createObject(testCase, {
			"share": publishingShare,
			"scopeLabel": "Lobby",
			"narrowLayout": !!narrow,
			"width": cardWidth
		})
		verify(card !== null, component.errorString())
		return card
	}

	function verifyInside(item, container, label) {
		verify(item !== null, label + " exists")
		const origin = item.mapToItem(container, 0, 0)
		verify(origin.x >= -0.5, label + " starts inside")
		verify(origin.x + item.width <= container.width + 0.5, label + " ends inside")
	}

	function test_rich_room_state_is_visible_and_actionable() {
		const card = createCard(720, false)
		const title = findChild(card, "activeScopeScreenShareTitle")
		const detail = findChild(card, "activeScopeScreenShareDetail")
		const badge = findChild(card, "activeScopeScreenShareStateBadge")
		const primary = findChild(card, "activeScopeScreenSharePrimaryAction")
		const overflow = findChild(card, "activeScopeScreenShareMoreActions")
		verify(title !== null && detail !== null && badge !== null)
		compare(title.text, "Your screen share")
		verify(detail.text.indexOf("1920x1080 @ 30 fps") >= 0)
		verify(detail.text.indexOf("4500 kbps") >= 0)
		verify(detail.text.indexOf("GStreamer GPU") >= 0)
		verify(primary.visible && overflow.visible)
		compare(card.Accessible.role, Accessible.Pane)
		verify(card.Accessible.description.indexOf("Alex") < 0,
			"Self-owned share uses the localized self identity instead of duplicating the owner")
		const actions = []
		card.actionRequested.connect(function(actionId) { actions.push(actionId) })
		primary.clicked()
		compare(actions.length, 1)
		compare(actions[0], "screenShareOpenWindow")
		card.destroy()
	}

	function test_remote_share_and_narrow_layout_preserve_identity_without_clipping() {
		const remoteShare = Object.assign({}, publishingShare, {
			"mode": "viewing",
			"ownerLabel": "Kira",
			"statusLabel": "Watching Kira's share"
		})
		const component = Qt.createComponent("qrc:/qml-shell/ScreenShareScopeCard.qml")
		compare(component.status, Component.Ready, component.errorString())
		const card = component.createObject(testCase, {
			"share": remoteShare, "scopeLabel": "Lobby", "narrowLayout": true, "width": 390
		})
		verify(card !== null, component.errorString())
		const title = findChild(card, "activeScopeScreenShareTitle")
		const primary = findChild(card, "activeScopeScreenSharePrimaryAction")
		const overflow = findChild(card, "activeScopeScreenShareMoreActions")
		compare(title.text, "Kira is sharing")
		verify(card.Accessible.description.indexOf("Kira") >= 0)
		verifyInside(title, card, "title")
		verifyInside(primary, card, "primary action")
		verifyInside(overflow, card, "overflow action")
		card.destroy()
	}

	function test_idle_state_does_not_reserve_layout_space() {
		const component = Qt.createComponent("qrc:/qml-shell/ScreenShareScopeCard.qml")
		compare(component.status, Component.Ready, component.errorString())
		const card = component.createObject(testCase, {
			"share": { "visible": true, "mode": "idle", "streamId": "" }, "width": 720
		})
		verify(card !== null, component.errorString())
		verify(!card.visible)
		compare(card.implicitHeight, 0)
		card.destroy()
	}
}
