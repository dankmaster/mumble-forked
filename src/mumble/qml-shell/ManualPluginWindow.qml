import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Mumble.Theme 1.0

Window {
	id: tool

	readonly property bool compactLayout: width < 640
	readonly property var speakerRows: manualPlugin.speakers || []
	property string statusMessage: ""

	width: 760
	height: 720
	minimumWidth: 420
	minimumHeight: 480
	visible: false
	title: qsTr("Manual placement")
	color: Theme.shellBackground
	flags: Qt.Window

	onVisibleChanged: {
		if (visible) {
			statusMessage = ""
			manualPlugin.refresh()
		}
		manualPlugin.setSpeakerUpdatesEnabled(visible)
	}
	onClosing: function(close) {
		manualPlugin.setSpeakerUpdatesEnabled(false)
		close.accepted = true
	}
	Component.onDestruction: manualPlugin.setSpeakerUpdatesEnabled(false)

	function updateNumber(propertyName, text, fallbackValue) {
		const parsed = Number.fromLocaleString(Qt.locale(), text)
		manualPlugin[propertyName] = Number.isFinite(parsed) ? parsed : fallbackValue
	}

	function syncFields() {
		if (!xField.activeFocus)
			xField.text = Number(manualPlugin.x).toLocaleString(Qt.locale(), "f", 2)
		if (!yField.activeFocus)
			yField.text = Number(manualPlugin.y).toLocaleString(Qt.locale(), "f", 2)
		if (!zField.activeFocus)
			zField.text = Number(manualPlugin.z).toLocaleString(Qt.locale(), "f", 2)
		if (!azimuthField.activeFocus)
			azimuthField.value = manualPlugin.azimuth
		if (!elevationField.activeFocus)
			elevationField.value = manualPlugin.elevation
		if (!contextField.activeFocus)
			contextField.text = manualPlugin.context
		if (!identityField.activeFocus)
			identityField.text = manualPlugin.identity
		if (!staleField.activeFocus)
			staleField.value = manualPlugin.staleSeconds
		activeField.checked = manualPlugin.active
		linkedField.checked = manualPlugin.linked
	}

	function applyChanges() {
		updateNumber("x", xField.text, manualPlugin.x)
		updateNumber("y", yField.text, manualPlugin.y)
		updateNumber("z", zField.text, manualPlugin.z)
		manualPlugin.azimuth = azimuthField.value
		manualPlugin.elevation = elevationField.value
		manualPlugin.context = contextField.text
		manualPlugin.identity = identityField.text
		manualPlugin.staleSeconds = staleField.value
		manualPlugin.active = activeField.checked
		manualPlugin.linked = linkedField.checked
		manualPlugin.apply()
	}

	Connections {
		target: manualPlugin
		function onStateChanged() { tool.syncFields() }
		function onApplied() {
			tool.statusMessage = qsTr("Position updated")
			statusTimer.restart()
		}
		function onResetCompleted() {
			tool.statusMessage = qsTr("Position reset")
			statusTimer.restart()
		}
	}

	Timer {
		id: statusTimer
		interval: 3000
		onTriggered: tool.statusMessage = ""
	}

	Shortcut {
		sequence: "Ctrl+Return"
		enabled: tool.visible
		onActivated: tool.applyChanges()
	}
	Shortcut {
		sequence: StandardKey.Cancel
		enabled: tool.visible
		onActivated: tool.hide()
	}

	component FieldLabel: Label {
		textFormat: Text.PlainText
		color: Theme.textMuted
		font.pixelSize: Theme.fontLabel
		font.weight: Font.Medium
		Layout.alignment: Qt.AlignVCenter
	}

	ScrollView {
		id: scrollView
		objectName: "manualPluginScrollView"
		anchors.left: parent.left
		anchors.right: parent.right
		anchors.top: parent.top
		anchors.bottom: manualPluginFooter.top
		anchors.leftMargin: tool.compactLayout ? Theme.space3 : Theme.space5
		anchors.rightMargin: tool.compactLayout ? Theme.space3 : Theme.space5
		anchors.topMargin: tool.compactLayout ? Theme.space3 : Theme.space5
		anchors.bottomMargin: Theme.space3
		contentWidth: availableWidth
		contentHeight: contentColumn.implicitHeight
		clip: true
		ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
		ScrollBar.vertical.policy: ScrollBar.AsNeeded

		ColumnLayout {
			id: contentColumn
			objectName: "manualPluginContent"
			width: scrollView.availableWidth
			spacing: Theme.space4

			ColumnLayout {
				Layout.fillWidth: true
				spacing: Theme.space1

				Label {
					textFormat: Text.PlainText
					Layout.fillWidth: true
					text: qsTr("Manual positional audio")
					color: Theme.textStrong
					font.pixelSize: Theme.fontHeading
					font.weight: Font.DemiBold
					Accessible.role: Accessible.Heading
				}

				Label {
					textFormat: Text.PlainText
					Layout.fillWidth: true
					text: qsTr("Place your positional-audio identity and inspect linked speaker positions in real time.")
					color: Theme.textMuted
					font.pixelSize: Theme.fontBody
					wrapMode: Text.WordWrap
				}
			}

			Rectangle {
				id: positionCard
				objectName: "manualPositionCard"
				Layout.fillWidth: true
				implicitHeight: positionColumn.implicitHeight + Theme.space4 * 2
				radius: Theme.shellRadius
				color: Theme.panel
				border.color: Theme.surfaceBorder
				Accessible.role: Accessible.Grouping
				Accessible.name: qsTr("Position and orientation")

				ColumnLayout {
					id: positionColumn
					anchors.left: parent.left
					anchors.right: parent.right
					anchors.top: parent.top
					anchors.margins: Theme.space4
					spacing: Theme.space3

					ColumnLayout {
						Layout.fillWidth: true
						spacing: 2
						Label {
							textFormat: Text.PlainText
							Layout.fillWidth: true
							text: qsTr("Position and orientation")
							color: Theme.textStrong
							font.pixelSize: Theme.fontTitle
							font.weight: Font.DemiBold
						}
						Label {
							textFormat: Text.PlainText
							Layout.fillWidth: true
							text: qsTr("Coordinates use the plugin's world units. Azimuth rotates clockwise on the preview.")
							color: Theme.textMuted
							font.pixelSize: Theme.fontCaption
							wrapMode: Text.WordWrap
						}
					}

					GridLayout {
						id: positionGrid
						objectName: "manualPositionGrid"
						columns: tool.compactLayout ? 2 : 6
						columnSpacing: Theme.space2
						rowSpacing: Theme.space2
						Layout.fillWidth: true

						FieldLabel { text: qsTr("X") }
						ModernTextField {
							id: xField
							objectName: "manualXField"
							text: Number(manualPlugin.x).toLocaleString(Qt.locale(), "f", 2)
							validator: DoubleValidator { notation: DoubleValidator.StandardNotation }
							onEditingFinished: tool.updateNumber("x", text, manualPlugin.x)
							Accessible.name: qsTr("X position")
							Layout.fillWidth: true
							Layout.minimumWidth: tool.compactLayout ? 220 : 90
						}
						FieldLabel { text: qsTr("Y") }
						ModernTextField {
							id: yField
							objectName: "manualYField"
							text: Number(manualPlugin.y).toLocaleString(Qt.locale(), "f", 2)
							validator: DoubleValidator { notation: DoubleValidator.StandardNotation }
							onEditingFinished: tool.updateNumber("y", text, manualPlugin.y)
							Accessible.name: qsTr("Y position")
							Layout.fillWidth: true
							Layout.minimumWidth: tool.compactLayout ? 220 : 90
						}
						FieldLabel { text: qsTr("Z") }
						ModernTextField {
							id: zField
							objectName: "manualZField"
							text: Number(manualPlugin.z).toLocaleString(Qt.locale(), "f", 2)
							validator: DoubleValidator { notation: DoubleValidator.StandardNotation }
							onEditingFinished: tool.updateNumber("z", text, manualPlugin.z)
							Accessible.name: qsTr("Z position")
							Layout.fillWidth: true
							Layout.minimumWidth: tool.compactLayout ? 220 : 90
						}
						FieldLabel { text: qsTr("Azimuth") }
						ModernSpinBox {
							id: azimuthField
							objectName: "manualAzimuthField"
							from: 0
							to: 360
							value: manualPlugin.azimuth
							editable: true
							onValueModified: manualPlugin.azimuth = value
							Accessible.name: qsTr("Azimuth in degrees")
							Layout.fillWidth: true
							Layout.minimumWidth: tool.compactLayout ? 220 : 90
						}
						FieldLabel { text: qsTr("Elevation") }
						ModernSpinBox {
							id: elevationField
							objectName: "manualElevationField"
							from: -90
							to: 90
							value: manualPlugin.elevation
							editable: true
							onValueModified: manualPlugin.elevation = value
							Accessible.name: qsTr("Elevation in degrees")
							Layout.fillWidth: true
							Layout.minimumWidth: tool.compactLayout ? 220 : 90
						}
					}

					Rectangle {
						id: preview
						objectName: "manualPositionPreview"
						Layout.fillWidth: true
						Layout.preferredHeight: tool.compactLayout ? 190 : 230
						color: Theme.strip
						radius: Theme.innerRadius
						border.color: Theme.divider
						clip: true
						Accessible.role: Accessible.Canvas
						Accessible.name: qsTr("Top-down position preview")
						Accessible.description: qsTr("Your position is X %1, Z %2, facing %3 degrees. %4 linked speakers are visible.")
							.arg(manualPlugin.x).arg(manualPlugin.z).arg(manualPlugin.azimuth).arg(tool.speakerRows.length)

						property real scaleFactor: Math.max(0.5,
							Math.min((width - 40) / 100, (height - 40) / 100))
						function displayX(value) {
							return width / 2 + Math.max(-50, Math.min(50, value)) * scaleFactor
						}
						function displayY(value) {
							return height / 2 + Math.max(-50, Math.min(50, value)) * scaleFactor
						}

						Rectangle {
							x: Theme.space3
							y: Math.round(parent.height / 2)
							width: parent.width - Theme.space3 * 2
							height: 1
							color: Theme.divider
						}
						Rectangle {
							x: Math.round(parent.width / 2)
							y: Theme.space3
							width: 1
							height: parent.height - Theme.space3 * 2
							color: Theme.divider
						}

						Label {
							textFormat: Text.PlainText
							anchors.right: parent.right
							anchors.verticalCenter: parent.verticalCenter
							anchors.rightMargin: Theme.space2
							text: "X"
							color: Theme.textMuted
							font.pixelSize: Theme.fontCaption
						}
						Label {
							textFormat: Text.PlainText
							anchors.horizontalCenter: parent.horizontalCenter
							anchors.top: parent.top
							anchors.topMargin: Theme.space2
							text: "Z"
							color: Theme.textMuted
							font.pixelSize: Theme.fontCaption
						}

						Repeater {
							model: tool.speakerRows.slice(0, 64)
							delegate: Rectangle {
								required property var modelData
								width: 9
								height: 9
								radius: width / 2
								x: preview.displayX(Number(modelData.x)) - width / 2
								y: preview.displayY(Number(modelData.z)) - height / 2
								color: Theme.textMuted
								border.color: Theme.textStrong
								border.width: 1
							}
						}

						Rectangle {
							id: avatar
							width: 16
							height: 16
							radius: width / 2
							x: preview.displayX(manualPlugin.x) - width / 2
							y: preview.displayY(manualPlugin.z) - height / 2
							color: Theme.accent
							border.color: Theme.textStrong
							border.width: 2

							Rectangle {
								width: 3
								height: 28
								radius: 2
								color: Theme.accent
								anchors.horizontalCenter: parent.horizontalCenter
								anchors.bottom: parent.verticalCenter
								transformOrigin: Item.Bottom
								rotation: manualPlugin.azimuth
							}
						}

						Rectangle {
							anchors.left: parent.left
							anchors.bottom: parent.bottom
							anchors.margins: Theme.space2
							implicitWidth: speakerCount.implicitWidth + Theme.space3 * 2
							implicitHeight: 26
							radius: height / 2
							color: Theme.panel
							border.color: Theme.divider

							Label {
								id: speakerCount
								textFormat: Text.PlainText
								anchors.centerIn: parent
								text: qsTr("%1 linked speakers").arg(tool.speakerRows.length)
								color: Theme.textMain
								font.pixelSize: Theme.fontCaption
							}
						}
					}
				}
			}

			Rectangle {
				id: identityCard
				objectName: "manualIdentityCard"
				Layout.fillWidth: true
				implicitHeight: identityColumn.implicitHeight + Theme.space4 * 2
				radius: Theme.shellRadius
				color: Theme.panel
				border.color: Theme.surfaceBorder
				Accessible.role: Accessible.Grouping
				Accessible.name: qsTr("Identity and link state")

				ColumnLayout {
					id: identityColumn
					anchors.left: parent.left
					anchors.right: parent.right
					anchors.top: parent.top
					anchors.margins: Theme.space4
					spacing: Theme.space3

					ColumnLayout {
						Layout.fillWidth: true
						spacing: 2
						Label {
							textFormat: Text.PlainText
							text: qsTr("Identity and link state")
							color: Theme.textStrong
							font.pixelSize: Theme.fontTitle
							font.weight: Font.DemiBold
						}
						Label {
							textFormat: Text.PlainText
							Layout.fillWidth: true
							text: qsTr("Context links users from the same game session. Identity describes the current player.")
							color: Theme.textMuted
							font.pixelSize: Theme.fontCaption
							wrapMode: Text.WordWrap
						}
					}

					GridLayout {
						id: identityGrid
						objectName: "manualIdentityGrid"
						Layout.fillWidth: true
						columns: tool.compactLayout ? 1 : 2
						columnSpacing: Theme.space3
						rowSpacing: Theme.space3

						ColumnLayout {
							Layout.fillWidth: true
							spacing: Theme.space1
							FieldLabel { text: qsTr("Context") }
							ModernTextField {
								id: contextField
								objectName: "manualContextField"
								Layout.fillWidth: true
								text: manualPlugin.context
								onEditingFinished: manualPlugin.context = text
								Accessible.name: qsTr("Context")
								placeholderText: qsTr("Game or world context")
							}
						}
						ColumnLayout {
							Layout.fillWidth: true
							spacing: Theme.space1
							FieldLabel { text: qsTr("Identity") }
							ModernTextField {
								id: identityField
								objectName: "manualIdentityField"
								Layout.fillWidth: true
								text: manualPlugin.identity
								onEditingFinished: manualPlugin.identity = text
								Accessible.name: qsTr("Identity")
								placeholderText: qsTr("Player identity")
							}
						}
						ColumnLayout {
							Layout.fillWidth: true
							Layout.columnSpan: tool.compactLayout ? 1 : 2
							spacing: Theme.space1
							FieldLabel { text: qsTr("Stale user display time") }
							ModernSpinBox {
								id: staleField
								objectName: "manualStaleField"
								Layout.fillWidth: true
								from: 0
								to: 3600
								value: manualPlugin.staleSeconds
								editable: true
								onValueModified: manualPlugin.staleSeconds = value
								Accessible.name: qsTr("Stale user display time in seconds")
							}
						}
					}

					Flow {
						Layout.fillWidth: true
						Layout.preferredHeight: childrenRect.height
						spacing: Theme.space4

						ModernCheckBox {
							id: activeField
							objectName: "manualActiveField"
							text: qsTr("Publish position")
							checked: manualPlugin.active
							Accessible.description: qsTr("Make the manual positional data active")
							onToggled: manualPlugin.active = checked
						}
						ModernCheckBox {
							id: linkedField
							objectName: "manualLinkedField"
							text: qsTr("Link to context")
							checked: manualPlugin.linked
							Accessible.description: qsTr("Link positional audio to users with the same context")
							onToggled: manualPlugin.linked = checked
						}
					}
				}
			}

			Item {
				objectName: "manualPluginContentEndPadding"
				Layout.fillWidth: true
				Layout.preferredHeight: Theme.space2
			}
		}
	}

	Rectangle {
		id: manualPluginFooter
		objectName: "manualPluginFooter"
		anchors.left: parent.left
		anchors.right: parent.right
		anchors.bottom: parent.bottom
		height: Math.max(Theme.controlHeight + (Theme.space3 * 2),
			footerColumn.implicitHeight + (Theme.space3 * 2))
		color: Theme.strip
		border.color: Theme.divider

		ColumnLayout {
			id: footerColumn
			anchors.fill: parent
			anchors.margins: Theme.space3
			spacing: Theme.space2

			Label {
				id: statusLabel
				objectName: "manualPluginStatus"
				Layout.fillWidth: true
				textFormat: Text.PlainText
				text: tool.statusMessage
				color: Theme.success
				font.weight: Font.DemiBold
				visible: text.length > 0
				Accessible.role: Accessible.StatusBar
				Accessible.name: text
			}

			RowLayout {
				id: actionRow
				objectName: "manualPluginActions"
				Layout.fillWidth: true
				spacing: Theme.space2

				ModernButton {
					id: resetButton
					objectName: "manualResetButton"
					text: qsTr("Reset")
					tone: "warning"
					onClicked: manualPlugin.reset()
				}
				Item { Layout.fillWidth: true }
				ModernButton {
					id: closeButton
					objectName: "manualCloseButton"
					text: qsTr("Close")
					onClicked: tool.hide()
				}
				ModernButton {
					id: applyButton
					objectName: "manualApplyButton"
					text: qsTr("Apply")
					highlighted: true
					tone: "accent"
					onClicked: tool.applyChanges()
				}
			}
		}
	}
}
