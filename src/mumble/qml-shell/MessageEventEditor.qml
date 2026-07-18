import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

ColumnLayout {
    id: root
    property var field
	readonly property bool compactCardLayout: Theme.compact || width < 600
	readonly property int actionColumnWidth: 56
	readonly property int headerHeight: 34
	readonly property int eventRowHeight: compactCardLayout ? 70 : 38
    width: parent ? parent.width : 0
    spacing: 8

	function itemFullyInsideEventViewport(item) {
		if (!item || !item.visible || !eventList || item.width <= 0 || item.height <= 0
				|| eventList.width <= 0 || eventList.height <= 0)
			return false
		try {
			const dependency = Number(eventList.contentY || 0)
				+ Number(item.x || 0) + Number(item.y || 0)
			if (!Number.isFinite(dependency))
				return false
			const point = item.mapToItem(eventList, 0, 0)
			const tolerance = 0.5
			return point.x >= -tolerance && point.y >= headerHeight - tolerance
				&& point.x + item.width <= eventList.width + tolerance
				&& point.y + item.height <= eventList.height + tolerance
		} catch (error) {
			return false
		}
	}

    ListView {
        id: eventList
        objectName: "messageEventList"
        Layout.fillWidth: true
		Layout.preferredHeight: Math.min(
			Math.max(root.headerHeight + root.eventRowHeight, contentHeight), 459)
        implicitHeight: Layout.preferredHeight
        model: field.rows || []
        clip: true
		// Compact cards keep a full six-row reading page below the pinned header.
		// A two-pixel gap left the sixth 70 px row clipped by four pixels at the
		// bounded 460 px height, which also made UIA publish a partial ListItem.
		spacing: root.compactCardLayout ? 1 : 2
		reuseItems: true
		// Cached ItemView delegates are intentionally not part of the visible
		// accessibility page. These rows are lightweight and reused, so a zero
		// offscreen cache avoids publishing Qt's private delegate interface while
		// preserving smooth bounded scrolling.
		cacheBuffer: 0
        activeFocusOnTab: true
        Accessible.role: Accessible.List
        Accessible.name: qsTr("Event behavior")
		headerPositioning: ListView.OverlayHeader
		header: Rectangle {
			id: eventHeader
			objectName: "messageEventHeader"
			width: eventList.width
			height: root.headerHeight
			z: 2
			color: Theme.panel
			border.color: Theme.divider
			border.width: 1
			radius: Theme.innerRadius
			// OverlayHeader is visually pinned above ListView.contentItem, while Qt's
			// accessibility hierarchy still parents it below that scrolling item.
			// Expose the stable list name and the fully-qualified checkbox names
			// instead of publishing a duplicate heading outside its UIA ancestor.
			Accessible.ignored: true

			RowLayout {
				visible: !root.compactCardLayout
				anchors.fill: parent
				anchors.leftMargin: 10
				anchors.rightMargin: 6
				spacing: 4
				Label {
					Layout.fillWidth: true
					Layout.minimumWidth: 0
					textFormat: Text.PlainText
					text: root.field.label || qsTr("Event behavior")
					color: Theme.textStrong
					font.pixelSize: Theme.fontCaption
					font.weight: Font.DemiBold
					Accessible.ignored: true
				}
				Item {
					objectName: "messageEventHeaderLogColumn"
					Layout.minimumWidth: root.actionColumnWidth
					Layout.preferredWidth: root.actionColumnWidth
					Layout.maximumWidth: root.actionColumnWidth
					Layout.fillHeight: true
					Label { objectName: "messageEventHeaderLog"; anchors.fill: parent; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; textFormat: Text.PlainText; text: qsTr("Log"); color: Theme.textMuted; font.pixelSize: Theme.fontCaption; Accessible.ignored: true }
				}
				Item {
					objectName: "messageEventHeaderNotifyColumn"
					Layout.minimumWidth: root.actionColumnWidth
					Layout.preferredWidth: root.actionColumnWidth
					Layout.maximumWidth: root.actionColumnWidth
					Layout.fillHeight: true
					Label { objectName: "messageEventHeaderNotify"; anchors.fill: parent; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; textFormat: Text.PlainText; text: qsTr("Notify"); color: Theme.textMuted; font.pixelSize: Theme.fontCaption; Accessible.ignored: true }
				}
				Item {
					objectName: "messageEventHeaderHighlightColumn"
					Layout.minimumWidth: root.actionColumnWidth
					Layout.preferredWidth: root.actionColumnWidth
					Layout.maximumWidth: root.actionColumnWidth
					Layout.fillHeight: true
					Label { objectName: "messageEventHeaderHighlight"; anchors.fill: parent; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; textFormat: Text.PlainText; text: qsTr("Highlight"); color: Theme.textMuted; font.pixelSize: Theme.fontCaption; Accessible.ignored: true }
				}
				Item {
					objectName: "messageEventHeaderTtsColumn"
					Layout.minimumWidth: root.actionColumnWidth
					Layout.preferredWidth: root.actionColumnWidth
					Layout.maximumWidth: root.actionColumnWidth
					Layout.fillHeight: true
					Label { objectName: "messageEventHeaderTts"; anchors.fill: parent; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; textFormat: Text.PlainText; text: qsTr("TTS"); color: Theme.textMuted; font.pixelSize: Theme.fontCaption; Accessible.ignored: true }
				}
				Item {
					objectName: "messageEventHeaderSoundColumn"
					Layout.minimumWidth: root.actionColumnWidth
					Layout.preferredWidth: root.actionColumnWidth
					Layout.maximumWidth: root.actionColumnWidth
					Layout.fillHeight: true
					Label { objectName: "messageEventHeaderSound"; anchors.fill: parent; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; textFormat: Text.PlainText; text: qsTr("Sound"); color: Theme.textMuted; font.pixelSize: Theme.fontCaption; Accessible.ignored: true }
				}
			}

			Label {
				objectName: "messageEventCompactHeader"
				visible: root.compactCardLayout
				anchors.fill: parent
				anchors.leftMargin: Theme.space3
				anchors.rightMargin: Theme.space3
				textFormat: Text.PlainText
				text: root.field.label || qsTr("Event behavior")
				color: Theme.textStrong
				font.pixelSize: Theme.fontLabel
				font.weight: Font.DemiBold
				verticalAlignment: Text.AlignVCenter
				Accessible.ignored: true
			}
		}
		ScrollBar.vertical: ModernScrollBar { }
        delegate: Rectangle {
			id: eventRow
			required property int index
			required property var modelData
			readonly property string eventName: modelData.name || qsTr("Event")
			readonly property bool accessibilityExposed: root.itemFullyInsideEventViewport(eventRow)
			function scheduleAccessibilityReassert() {
				if (accessibilityExposed) {
					accessibilityReassertTimer.stop()
					return
				}
				accessibilityReassertTimer.restart()
			}
			Timer {
				id: accessibilityReassertTimer
				interval: 0
				repeat: false
				onTriggered: {
					if (rowAccessibilityBarrier.active)
						rowAccessibilityBarrier.reassertItem(eventRow)
				}
			}
			onAccessibilityExposedChanged: scheduleAccessibilityReassert()
			Component.onCompleted: scheduleAccessibilityReassert()
			ListView.onPooled: scheduleAccessibilityReassert()
			ListView.onReused: scheduleAccessibilityReassert()
            objectName: "messageEventRow_" + String(modelData.type)
            width: eventList.width
			height: root.eventRowHeight
			radius: root.compactCardLayout ? Theme.innerRadius : 6
			color: root.compactCardLayout
				? (index % 2 === 0 ? Theme.surfaceRaised : Theme.panel)
				: (index % 2 === 0 ? Theme.strip : "transparent")
			border.color: root.compactCardLayout ? Theme.divider : "transparent"
			border.width: root.compactCardLayout ? 1 : 0
            Accessible.role: Accessible.ListItem
			Accessible.name: eventName
            RowLayout {
				id: regularEventRow
				visible: !root.compactCardLayout
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 6
                spacing: 4
				Label { Layout.fillWidth: true; Layout.minimumWidth: 0; textFormat: Text.PlainText; text: eventName; color: Theme.textMain; elide: Text.ElideRight; font.pixelSize: Theme.fontCaption }
				Item {
					objectName: "messageEventConsoleColumn_" + String(modelData.type)
					Layout.minimumWidth: root.actionColumnWidth
					Layout.preferredWidth: root.actionColumnWidth
					Layout.maximumWidth: root.actionColumnWidth
					Layout.fillHeight: true
					ModernCheckBox { objectName: "messageEventConsole_" + String(modelData.type); anchors.centerIn: parent; checked: !!modelData.console; Accessible.name: qsTr("%1: log").arg(modelData.name || qsTr("Event")); onToggled: root.toggle(modelData.type, "console", checked) }
				}
				Item {
					objectName: "messageEventNotificationColumn_" + String(modelData.type)
					Layout.minimumWidth: root.actionColumnWidth
					Layout.preferredWidth: root.actionColumnWidth
					Layout.maximumWidth: root.actionColumnWidth
					Layout.fillHeight: true
					ModernCheckBox { objectName: "messageEventNotification_" + String(modelData.type); anchors.centerIn: parent; checked: !!modelData.notification; Accessible.name: qsTr("%1: notification").arg(modelData.name || qsTr("Event")); onToggled: root.toggle(modelData.type, "notification", checked) }
				}
				Item {
					objectName: "messageEventHighlightColumn_" + String(modelData.type)
					Layout.minimumWidth: root.actionColumnWidth
					Layout.preferredWidth: root.actionColumnWidth
					Layout.maximumWidth: root.actionColumnWidth
					Layout.fillHeight: true
					ModernCheckBox { objectName: "messageEventHighlight_" + String(modelData.type); anchors.centerIn: parent; checked: !!modelData.highlight; Accessible.name: qsTr("%1: highlight").arg(modelData.name || qsTr("Event")); onToggled: root.toggle(modelData.type, "highlight", checked) }
				}
				Item {
					objectName: "messageEventTtsColumn_" + String(modelData.type)
					Layout.minimumWidth: root.actionColumnWidth
					Layout.preferredWidth: root.actionColumnWidth
					Layout.maximumWidth: root.actionColumnWidth
					Layout.fillHeight: true
					ModernCheckBox { objectName: "messageEventTts_" + String(modelData.type); anchors.centerIn: parent; checked: !!modelData.tts; Accessible.name: qsTr("%1: text to speech").arg(modelData.name || qsTr("Event")); onToggled: root.toggle(modelData.type, "tts", checked) }
				}
				Item {
					objectName: "messageEventSoundColumn_" + String(modelData.type)
					Layout.minimumWidth: root.actionColumnWidth
					Layout.preferredWidth: root.actionColumnWidth
					Layout.maximumWidth: root.actionColumnWidth
					Layout.fillHeight: true
					ModernCheckBox { objectName: "messageEventSound_" + String(modelData.type); anchors.centerIn: parent; checked: !!modelData.sound; Accessible.name: qsTr("%1: sound").arg(modelData.name || qsTr("Event")); onToggled: root.toggle(modelData.type, "sound", checked) }
				}
            }

			ColumnLayout {
				id: compactEventRow
				visible: root.compactCardLayout
				anchors.fill: parent
				anchors.leftMargin: Theme.space3
				anchors.rightMargin: Theme.space3
				anchors.topMargin: Theme.space1
				anchors.bottomMargin: Theme.space1
				spacing: 0

				Label {
					id: compactEventName
					objectName: "messageEventCompactName_" + String(modelData.type)
					Layout.fillWidth: true
					Layout.minimumWidth: 0
					textFormat: Text.PlainText
					text: eventName
					color: Theme.textStrong
					elide: Text.ElideRight
					font.pixelSize: Theme.fontBody
					font.weight: Font.Medium
					Accessible.role: Accessible.StaticText
					Accessible.name: eventName
				}

				RowLayout {
					id: compactActions
					objectName: "messageEventCompactActions_" + String(modelData.type)
					Layout.fillWidth: true
					Layout.minimumWidth: 0
					spacing: Theme.space1

					ColumnLayout {
						Layout.fillWidth: true
						Layout.minimumWidth: 0
						spacing: 0
						Label {
							objectName: "messageEventCompactConsoleLabel_" + String(modelData.type)
							Layout.fillWidth: true
							text: qsTr("Log")
							textFormat: Text.PlainText
							color: Theme.textMuted
							font.pixelSize: Theme.fontCaption
							horizontalAlignment: Text.AlignHCenter
							elide: Text.ElideRight
							Accessible.ignored: true
						}
						ModernCheckBox {
							id: compactConsole
							objectName: "messageEventCompactConsole_" + String(modelData.type)
							Layout.alignment: Qt.AlignHCenter
							dense: true
							text: ""
							checked: !!modelData.console
							Accessible.name: qsTr("%1: log").arg(eventName)
							KeyNavigation.right: compactNotification
							onToggled: root.toggle(modelData.type, "console", checked)
						}
					}
					ColumnLayout {
						Layout.fillWidth: true
						Layout.minimumWidth: 0
						spacing: 0
						Label {
							objectName: "messageEventCompactNotificationLabel_" + String(modelData.type)
							Layout.fillWidth: true
							text: qsTr("Notify")
							textFormat: Text.PlainText
							color: Theme.textMuted
							font.pixelSize: Theme.fontCaption
							horizontalAlignment: Text.AlignHCenter
							elide: Text.ElideRight
							Accessible.ignored: true
						}
						ModernCheckBox {
							id: compactNotification
							objectName: "messageEventCompactNotification_" + String(modelData.type)
							Layout.alignment: Qt.AlignHCenter
							dense: true
							text: ""
							checked: !!modelData.notification
							Accessible.name: qsTr("%1: notification").arg(eventName)
							KeyNavigation.left: compactConsole
							KeyNavigation.right: compactHighlight
							onToggled: root.toggle(modelData.type, "notification", checked)
						}
					}
					ColumnLayout {
						Layout.fillWidth: true
						Layout.minimumWidth: 0
						spacing: 0
						Label {
							objectName: "messageEventCompactHighlightLabel_" + String(modelData.type)
							Layout.fillWidth: true
							text: qsTr("Highlight")
							textFormat: Text.PlainText
							color: Theme.textMuted
							font.pixelSize: Theme.fontCaption
							horizontalAlignment: Text.AlignHCenter
							elide: Text.ElideRight
							Accessible.ignored: true
						}
						ModernCheckBox {
							id: compactHighlight
							objectName: "messageEventCompactHighlight_" + String(modelData.type)
							Layout.alignment: Qt.AlignHCenter
							dense: true
							text: ""
							checked: !!modelData.highlight
							Accessible.name: qsTr("%1: highlight").arg(eventName)
							KeyNavigation.left: compactNotification
							KeyNavigation.right: compactTts
							onToggled: root.toggle(modelData.type, "highlight", checked)
						}
					}
					ColumnLayout {
						Layout.fillWidth: true
						Layout.minimumWidth: 0
						spacing: 0
						Label {
							objectName: "messageEventCompactTtsLabel_" + String(modelData.type)
							Layout.fillWidth: true
							text: qsTr("TTS")
							textFormat: Text.PlainText
							color: Theme.textMuted
							font.pixelSize: Theme.fontCaption
							horizontalAlignment: Text.AlignHCenter
							elide: Text.ElideRight
							Accessible.ignored: true
						}
						ModernCheckBox {
							id: compactTts
							objectName: "messageEventCompactTts_" + String(modelData.type)
							Layout.alignment: Qt.AlignHCenter
							dense: true
							text: ""
							checked: !!modelData.tts
							Accessible.name: qsTr("%1: text to speech").arg(eventName)
							KeyNavigation.left: compactHighlight
							KeyNavigation.right: compactSound
							onToggled: root.toggle(modelData.type, "tts", checked)
						}
					}
					ColumnLayout {
						Layout.fillWidth: true
						Layout.minimumWidth: 0
						spacing: 0
						Label {
							objectName: "messageEventCompactSoundLabel_" + String(modelData.type)
							Layout.fillWidth: true
							text: qsTr("Sound")
							textFormat: Text.PlainText
							color: Theme.textMuted
							font.pixelSize: Theme.fontCaption
							horizontalAlignment: Text.AlignHCenter
							elide: Text.ElideRight
							Accessible.ignored: true
						}
						ModernCheckBox {
							id: compactSound
							objectName: "messageEventCompactSound_" + String(modelData.type)
							Layout.alignment: Qt.AlignHCenter
							dense: true
							text: ""
							checked: !!modelData.sound
							Accessible.name: qsTr("%1: sound").arg(eventName)
							KeyNavigation.left: compactTts
							onToggled: root.toggle(modelData.type, "sound", checked)
						}
					}
				}
			}
			ModalAccessibilityBarrier {
				id: rowAccessibilityBarrier
				objectName: "messageEventRowAccessibilityBarrier_" + String(modelData.type)
				active: !eventRow.accessibilityExposed
				// Suppress the ListItem owner as well as every promoted descendant.
				// Qt's ItemView can keep one partially clipped delegate materialized.
				targets: [ eventRow ]
			}
        }
    }

    function toggle(messageType, propertyName, value) {
        dialogState.invokeAction("messages.toggleEvent",
                                 { "messageType": messageType, "property": propertyName, "value": value })
    }
}
