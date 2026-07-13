import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

ColumnLayout {
	id: root

	property var field
	readonly property var rows: field && field.rows ? field.rows : []
	readonly property int rowCount: rows.length || 0
	readonly property bool compactLayout: width < 620
	readonly property bool loading: !!(field && field.loading)
	readonly property string errorText: field && field.error ? String(field.error) : ""
	readonly property var operation: field && field.operation ? field.operation : ({})
	readonly property bool operationVisible: operation && String(operation.status || "").length > 0
	readonly property bool showLoadingState: loading && rowCount === 0 && errorText.length === 0
	readonly property bool showEmptyState: !loading && rowCount === 0 && errorText.length === 0
	readonly property bool showErrorState: errorText.length > 0
	readonly property real operationProgress: {
		const raw = Number(operation.progress)
		if (!Number.isFinite(raw))
			return -1
		return Math.max(0, Math.min(1, raw > 1 ? raw / 100 : raw))
	}

	width: parent ? parent.width : 0
	spacing: Theme.space4
	Accessible.role: Accessible.Pane
	Accessible.name: field && field.label ? String(field.label) : qsTr("Installed plugins")

	ColumnLayout {
		Layout.fillWidth: true
		spacing: Theme.space1

		RowLayout {
			Layout.fillWidth: true
			spacing: Theme.space3

			ColumnLayout {
				Layout.fillWidth: true
				spacing: 2

				Label {
					textFormat: Text.PlainText
					Layout.fillWidth: true
					text: root.field && root.field.label
						? String(root.field.label) : qsTr("Installed plugins")
					color: Theme.textStrong
					font.pixelSize: Theme.fontTitle
					font.weight: Font.DemiBold
					Accessible.role: Accessible.Heading
				}

				Label {
					textFormat: Text.PlainText
					Layout.fillWidth: true
					text: root.loading ? qsTr("Refreshing plugin information…")
						: root.rowCount === 1 ? qsTr("1 plugin available")
						: qsTr("%1 plugins available").arg(root.rowCount)
					color: Theme.textMuted
					font.pixelSize: Theme.fontCaption
				}
			}

			Rectangle {
				visible: !root.compactLayout && root.rowCount > 0
				implicitWidth: pluginCountLabel.implicitWidth + Theme.space3 * 2
				implicitHeight: 28
				radius: height / 2
				color: Theme.accentSubtle
				border.color: Theme.accent

				Label {
					id: pluginCountLabel
					textFormat: Text.PlainText
					anchors.centerIn: parent
					text: String(root.rowCount)
					color: Theme.textStrong
					font.pixelSize: Theme.fontLabel
					font.weight: Font.DemiBold
				}
			}
		}

		Flow {
			id: toolbar
			objectName: "pluginToolbar"
			Layout.fillWidth: true
			Layout.preferredHeight: childrenRect.height
			spacing: Theme.space2

			ModernButton {
				objectName: "pluginInstallButton"
				text: qsTr("Install plugin…")
				highlighted: true
				tone: "accent"
				onClicked: dialogState.invokeAction("plugins.install", {})
			}
			ModernButton {
				objectName: "pluginRescanButton"
				text: qsTr("Rescan")
				onClicked: dialogState.invokeAction("plugins.rescan", {})
			}
			ModernButton {
				objectName: "pluginCheckUpdatesButton"
				text: qsTr("Check for updates")
				onClicked: dialogState.invokeAction("plugins.checkUpdates", {})
			}
		}
	}

	Rectangle {
		id: operationCard
		objectName: "pluginOperationCard"
		Layout.fillWidth: true
		implicitHeight: operationContent.implicitHeight + Theme.space4 * 2
		visible: root.operationVisible
		radius: Theme.innerRadius
		color: Theme.surfaceRaised
		border.color: Theme.surfaceBorder
		Accessible.role: Accessible.StatusBar
		Accessible.name: operationTitle.text

		ColumnLayout {
			id: operationContent
			anchors.left: parent.left
			anchors.right: parent.right
			anchors.top: parent.top
			anchors.margins: Theme.space4
			spacing: Theme.space2

			RowLayout {
				Layout.fillWidth: true
				spacing: Theme.space2

				BusyIndicator {
					running: root.operationVisible
					visible: running
					implicitWidth: 22
					implicitHeight: 22
				}

				Label {
					id: operationTitle
					textFormat: Text.PlainText
					Layout.fillWidth: true
					text: root.operation.label || root.operation.message
						|| qsTr("Plugin operation in progress")
					color: Theme.textStrong
					font.weight: Font.DemiBold
					elide: Text.ElideRight
				}

				Label {
					textFormat: Text.PlainText
					visible: root.operationProgress >= 0
					text: qsTr("%1%").arg(Math.round(root.operationProgress * 100))
					color: Theme.textMuted
					font.pixelSize: Theme.fontCaption
				}
			}

			ProgressBar {
				id: operationProgress
				objectName: "pluginOperationProgress"
				Layout.fillWidth: true
				from: 0
				to: 1
				value: root.operationProgress < 0 ? 0 : root.operationProgress
				indeterminate: root.operationProgress < 0
				Accessible.name: operationTitle.text
				Accessible.description: root.operationProgress < 0
					? qsTr("Progress is not yet available")
					: qsTr("%1 percent complete").arg(Math.round(root.operationProgress * 100))
				background: Rectangle {
					implicitHeight: 5
					radius: height / 2
					color: Theme.panel
				}
				contentItem: Item {
					implicitHeight: 5
					clip: true
					Rectangle {
						width: parent.width * operationProgress.visualPosition
						height: parent.height
						radius: height / 2
						color: Theme.accent
					}
				}
			}
		}
	}

	Rectangle {
		id: errorCard
		objectName: "pluginErrorCard"
		Layout.fillWidth: true
		implicitHeight: errorContent.implicitHeight + Theme.space4 * 2
		visible: root.showErrorState
		radius: Theme.innerRadius
		color: Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.12)
		border.color: Theme.danger
		Accessible.role: Accessible.AlertMessage
		Accessible.name: errorLabel.text

		RowLayout {
			id: errorContent
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
					text: qsTr("Plugin information could not be loaded")
					color: Theme.textStrong
					font.weight: Font.DemiBold
				}
				Label {
					id: errorLabel
					Layout.fillWidth: true
					textFormat: Text.PlainText
					text: root.errorText
					color: Theme.textMain
					wrapMode: Text.WordWrap
				}
			}

			ModernButton {
				objectName: "pluginRetryButton"
				text: qsTr("Try again")
				onClicked: dialogState.invokeAction("plugins.rescan", {})
			}
		}
	}

	Item {
		id: loadingState
		objectName: "pluginLoadingState"
		Layout.fillWidth: true
		Layout.preferredHeight: 150
		visible: root.showLoadingState
		Accessible.role: Accessible.StatusBar
		Accessible.name: qsTr("Loading installed plugins")

		Column {
			anchors.centerIn: parent
			spacing: Theme.space2
			BusyIndicator {
				anchors.horizontalCenter: parent.horizontalCenter
				running: loadingState.visible
			}
			Label {
				textFormat: Text.PlainText
				anchors.horizontalCenter: parent.horizontalCenter
				text: qsTr("Loading installed plugins…")
				color: Theme.textMuted
			}
		}
	}

	Rectangle {
		id: emptyState
		objectName: "pluginEmptyState"
		Layout.fillWidth: true
		implicitHeight: emptyContent.implicitHeight + Theme.space5 * 2
		visible: root.showEmptyState
		radius: Theme.innerRadius
		color: Theme.panel
		border.color: Theme.divider
		Accessible.role: Accessible.Pane
		Accessible.name: qsTr("No plugins installed")

		ColumnLayout {
			id: emptyContent
			anchors.centerIn: parent
			width: Math.min(parent.width - Theme.space5 * 2, 420)
			spacing: Theme.space2

			Label {
				textFormat: Text.PlainText
				Layout.fillWidth: true
				text: qsTr("No plugins found")
				color: Theme.textStrong
				font.pixelSize: Theme.fontTitle
				font.weight: Font.DemiBold
				horizontalAlignment: Text.AlignHCenter
			}
			Label {
				textFormat: Text.PlainText
				Layout.fillWidth: true
				text: qsTr("Install a Mumble plugin or rescan the configured plugin folders.")
				color: Theme.textMuted
				wrapMode: Text.WordWrap
				horizontalAlignment: Text.AlignHCenter
			}
			ModernButton {
				Layout.alignment: Qt.AlignHCenter
				text: qsTr("Install plugin…")
				highlighted: true
				tone: "accent"
				onClicked: dialogState.invokeAction("plugins.install", {})
			}
		}
	}

	ListView {
		id: pluginList
		objectName: "pluginList"
		Layout.fillWidth: true
		Layout.preferredHeight: Math.min(Math.max(contentHeight, 112), root.compactLayout ? 390 : 470)
		implicitHeight: Layout.preferredHeight
		visible: root.rowCount > 0
		model: root.rows
		clip: true
		spacing: Theme.space3
		reuseItems: true
		boundsBehavior: Flickable.StopAtBounds
		keyNavigationEnabled: true
		Accessible.role: Accessible.List
		Accessible.name: qsTr("Installed plugins")

		ScrollBar.vertical: ScrollBar { policy: pluginList.contentHeight > pluginList.height
			? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }

		delegate: Rectangle {
			id: pluginCard
			required property var modelData
			readonly property bool cardCompact: width < 560
			readonly property string pluginName: modelData.name || qsTr("Unnamed plugin")
			readonly property string pluginId: String(modelData.id === undefined ? index : modelData.id)

			objectName: "pluginCard_" + pluginId
			width: pluginList.width
			height: pluginColumn.implicitHeight + Theme.space4 * 2
			radius: Theme.innerRadius
			color: Theme.surfaceRaised
			border.color: activeFocus ? Theme.focus : Theme.surfaceBorder
			border.width: activeFocus ? Theme.focusRingWidth : 1
			focus: ListView.isCurrentItem
			activeFocusOnTab: true
			Accessible.role: Accessible.ListItem
			Accessible.name: pluginName
			Accessible.description: modelData.loaded ? qsTr("Loaded plugin") : qsTr("Plugin is not loaded")
			Keys.onUpPressed: pluginList.decrementCurrentIndex()
			Keys.onDownPressed: pluginList.incrementCurrentIndex()

			ColumnLayout {
				id: pluginColumn
				anchors.left: parent.left
				anchors.right: parent.right
				anchors.top: parent.top
				anchors.margins: Theme.space4
				spacing: Theme.space3

				RowLayout {
					Layout.fillWidth: true
					spacing: Theme.space3

					Rectangle {
						implicitWidth: 38
						implicitHeight: 38
						radius: 12
						color: modelData.loaded ? Theme.accentSubtle : Theme.panel
						border.color: modelData.loaded ? Theme.accent : Theme.divider

						Label {
							textFormat: Text.PlainText
							anchors.centerIn: parent
							text: pluginCard.pluginName.length > 0
								? pluginCard.pluginName.charAt(0).toUpperCase() : "P"
							color: Theme.textStrong
							font.pixelSize: Theme.fontTitle
							font.weight: Font.DemiBold
						}
					}

					ColumnLayout {
						Layout.fillWidth: true
						spacing: 2

						Label {
							Layout.fillWidth: true
							textFormat: Text.PlainText
							text: pluginCard.pluginName
							color: Theme.textStrong
							font.pixelSize: Theme.fontTitle
							font.weight: Font.DemiBold
							elide: Text.ElideRight
						}
						Label {
							Layout.fillWidth: true
							textFormat: Text.PlainText
							text: [modelData.author || "", modelData.version || ""]
								.filter(function(part) { return String(part).length > 0 }).join(" · ")
							color: Theme.textMuted
							font.pixelSize: Theme.fontCaption
							elide: Text.ElideRight
							visible: text.length > 0
						}
					}

					Rectangle {
						implicitWidth: loadStateLabel.implicitWidth + Theme.space3 * 2
						implicitHeight: 26
						radius: height / 2
						color: modelData.loaded ? Theme.accentSubtle : Theme.panel
						border.color: modelData.loaded ? Theme.accent : Theme.divider

						Label {
							id: loadStateLabel
							textFormat: Text.PlainText
							anchors.centerIn: parent
							text: modelData.loaded ? qsTr("Loaded") : qsTr("Not loaded")
							color: modelData.loaded ? Theme.accentHover : Theme.textMuted
							font.pixelSize: Theme.fontCaption
							font.weight: Font.DemiBold
						}
					}
				}

				Label {
					Layout.fillWidth: true
					textFormat: Text.PlainText
					text: modelData.description || qsTr("No description supplied by this plugin.")
					color: Theme.textMain
					font.pixelSize: Theme.fontBody
					wrapMode: Text.WordWrap
					maximumLineCount: pluginCard.cardCompact ? 3 : 2
					elide: Text.ElideRight
				}

				Label {
					Layout.fillWidth: true
					textFormat: Text.PlainText
					text: modelData.path || ""
					visible: text.length > 0
					color: Theme.textMuted
					font.pixelSize: Theme.fontCaption
					elide: Text.ElideMiddle
					Accessible.name: qsTr("Plugin path: %1").arg(text)
				}

				Flow {
					id: permissionFlow
					objectName: "pluginPermissionFlow_" + pluginCard.pluginId
					Layout.fillWidth: true
					Layout.preferredHeight: childrenRect.height
					spacing: Theme.space2

					ModernCheckBox {
						objectName: "pluginEnable_" + pluginCard.pluginId
						dense: true
						text: qsTr("Enabled")
						checked: !!modelData.enabled
						onToggled: dialogState.invokeAction("plugins.toggle",
							{ "pluginId": modelData.id, "property": "enabled", "value": checked })
					}
					ModernCheckBox {
						objectName: "pluginPositional_" + pluginCard.pluginId
						dense: true
						text: qsTr("Positional audio")
						visible: !!modelData.positionalAvailable
						checked: !!modelData.positionalEnabled
						onToggled: dialogState.invokeAction("plugins.toggle",
							{ "pluginId": modelData.id, "property": "positional", "value": checked })
					}
					ModernCheckBox {
						objectName: "pluginKeyboard_" + pluginCard.pluginId
						dense: true
						text: qsTr("Keyboard monitoring")
						checked: !!modelData.keyboardMonitoringAllowed
						Accessible.description: qsTr("Allow this plugin to observe keyboard input")
						onToggled: dialogState.invokeAction("plugins.toggle",
							{ "pluginId": modelData.id, "property": "keyboard", "value": checked })
					}
				}

				Flow {
					id: actionFlow
					objectName: "pluginActionFlow_" + pluginCard.pluginId
					Layout.fillWidth: true
					Layout.preferredHeight: childrenRect.height
					spacing: Theme.space2

					ModernButton {
						objectName: "pluginConfigure_" + pluginCard.pluginId
						dense: true
						text: qsTr("Configure")
						visible: !!modelData.loaded && !!modelData.canConfigure
						onClicked: dialogState.invokeAction("plugins.configure", { "pluginId": modelData.id })
					}
					ModernButton {
						objectName: "pluginAbout_" + pluginCard.pluginId
						dense: true
						text: qsTr("About")
						visible: !!modelData.loaded && !!modelData.canShowAbout
						onClicked: dialogState.invokeAction("plugins.about", { "pluginId": modelData.id })
					}
					ModernButton {
						objectName: "pluginUnload_" + pluginCard.pluginId
						dense: true
						text: qsTr("Unload")
						visible: !!modelData.loaded && !modelData.builtIn
						onClicked: dialogState.invokeAction("plugins.unload", { "pluginId": modelData.id })
					}
				}
			}
		}
	}
}
