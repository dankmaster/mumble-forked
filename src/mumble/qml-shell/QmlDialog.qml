import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

Dialog {
    id: dialog
	readonly property int densityInset: Theme.spacing + 6
	readonly property int sectionPadding: Theme.spacing + 2
	readonly property bool compactDialogLayout: width < 760
	readonly property int headerHeight: Theme.densityId === "compact" ? 68
		: Theme.densityId === "spacious" ? 84 : 76
	readonly property int footerHeight: Math.max(Theme.densityId === "compact" ? 56
		: Theme.densityId === "spacious" ? 72 : 64,
		footerActions.childrenRect.height + (Theme.spacing * 2),
		settingsAdvancedFooter.implicitHeight + (Theme.spacing * 2))
	readonly property int statusHeight: String(dialogState.statusMessage || "").length > 0
		? statusLabel.implicitHeight + (Theme.spacing * 2) : 0
	readonly property int compactPageBarHeight: compactDialogLayout && dialogState.pages.length > 0
		? compactPageSelector.implicitHeight + (Theme.spacing * 2) : 0
	readonly property int requestedHeight: Math.max(240,
		Math.min(dialogState.preferredHeight || 700, 860))
	readonly property int maximumHeightForParent: parent
		? Math.max(220, parent.height - (Theme.space5 * 2)) : 760
	// Multi-page surfaces keep a stable viewport as the user changes pages. A
	// single-surface dialog instead follows its actual content: sparse states no
	// longer inherit an oversized DTO canvas, while long forms can grow before
	// they need to scroll.
	readonly property bool stablePageViewport: dialogState.pages.length > 0
	readonly property int minimumContentSizedHeight: headerHeight + footerHeight
		+ (Theme.rowHeight * 2)
	property int measuredContentHeight: 0
	readonly property int naturalContentSizedHeight: headerHeight + statusHeight
		+ compactPageBarHeight + measuredContentHeight + footerHeight
	readonly property int responsiveHeight: stablePageViewport ? requestedHeight
		: Math.max(minimumContentSizedHeight, naturalContentSizedHeight)
	readonly property string dialogTone: String(dialogState.tone || "").toLowerCase()
	readonly property color dialogToneColor: toneColor(dialogTone)
	property bool showAdvanced: !!dialogState.state.showAdvanced
	property string focusedDialogId: ""
	property bool nestedModalOpen: false
	property var beforeOpen: null
	// Controls inherit this palette when a specialized product component does
	// not provide its own background. Keep Qt's Basic/native fallback colors
	// from leaking into themed dialogs as bright selection rectangles.
	palette.window: Theme.panel
	palette.active.base: Theme.panel
	palette.inactive.base: Theme.panel
	palette.alternateBase: Theme.surfaceRaised
	palette.active.button: Theme.surfaceRaised
	palette.inactive.button: Theme.surfaceRaised
	palette.active.text: Theme.textMain
	palette.inactive.text: Theme.textMain
	palette.active.windowText: Theme.textMain
	palette.inactive.windowText: Theme.textMain
	palette.active.buttonText: Theme.textMain
	palette.inactive.buttonText: Theme.textMain
	palette.active.brightText: Theme.textStrong
	palette.inactive.brightText: Theme.textStrong
	palette.active.highlight: Theme.selected
	palette.inactive.highlight: Theme.selected
	palette.active.highlightedText: Theme.textStrong
	palette.inactive.highlightedText: Theme.textStrong
	palette.placeholderText: Theme.textMuted
	palette.active.link: Theme.accent
	palette.inactive.link: Theme.accent
	palette.active.linkVisited: Theme.accentHover
	palette.inactive.linkVisited: Theme.accentHover
	palette.active.toolTipBase: Theme.surfaceRaised
	palette.inactive.toolTipBase: Theme.surfaceRaised
	palette.active.toolTipText: Theme.textStrong
	palette.inactive.toolTipText: Theme.textStrong
	palette.active.light: Theme.surfaceHover
	palette.inactive.light: Theme.surfaceHover
	palette.active.midlight: Theme.surfaceRaised
	palette.inactive.midlight: Theme.surfaceRaised
	palette.active.mid: Theme.surfaceBorder
	palette.inactive.mid: Theme.surfaceBorder
	palette.dark: Theme.rail
	palette.shadow: Theme.strip
	palette.disabled.window: Theme.panel
	palette.disabled.base: Theme.panel
	palette.disabled.alternateBase: Theme.panel
	palette.disabled.button: Theme.panel
	palette.disabled.text: Theme.textMuted
	palette.disabled.windowText: Theme.textMuted
	palette.disabled.buttonText: Theme.textMuted
	palette.disabled.brightText: Theme.textMuted
	palette.disabled.highlight: Theme.surfaceBorder
	palette.disabled.highlightedText: Theme.textMuted
	palette.disabled.placeholderText: Theme.textMuted
	palette.disabled.light: Theme.surfaceBorder
	palette.disabled.midlight: Theme.panel
	palette.disabled.mid: Theme.divider
	palette.disabled.dark: Theme.rail
	palette.disabled.shadow: Theme.strip
	palette.disabled.link: Theme.textMuted
	palette.disabled.linkVisited: Theme.textMuted
	palette.disabled.toolTipBase: Theme.panel
	palette.disabled.toolTipText: Theme.textMuted
	readonly property bool hasAdvancedContent: {
		dialogState.revision
		const sections = dialogState.sections || []
		for (let sectionIndex = 0; sectionIndex < sections.length; ++sectionIndex) {
			const section = sections[sectionIndex] || {}
			if (section.advanced)
				return true
			const fields = section.fields || []
			for (let fieldIndex = 0; fieldIndex < fields.length; ++fieldIndex) {
				if (fields[fieldIndex] && fields[fieldIndex].advanced)
					return true
			}
		}
		return false
	}
	function toneColor(tone) {
		const normalized = String(tone || "").toLowerCase()
		if (normalized === "danger" || normalized === "error") return Theme.danger
		if (normalized === "warning" || normalized === "retry") return Theme.warning
		if (normalized === "success") return Theme.success
		if (normalized === "accent") return Theme.accent
		return Theme.divider
	}
	function normalizedFocusObjectName(value) {
		const requested = String(value || "").trim()
		if (requested.length === 0) return ""
		if (requested.indexOf("dialogField_") === 0 || requested.indexOf("dialogAction_") === 0
				|| requested.indexOf("connect") === 0)
			return requested
		return "dialogField_" + requested
	}
	function focusObjectInTree(root, objectName) {
		if (!root || objectName.length === 0) return null
		if (String(root.objectName || "") === objectName) return root
		const children = root.children || []
		for (let index = 0; index < children.length; ++index) {
			const match = focusObjectInTree(children[index], objectName)
			if (match) return match
		}
		return null
	}
	function metadataFocusId() {
		const state = dialogState.state || {}
		let requested = dialogState.initialFocusId || state.initialFocus || state.defaultFocus || ""
		if (requested && typeof requested === "object")
			requested = requested.id || requested.fieldId || requested.actionId || ""
		if (String(requested).length > 0) return normalizedFocusObjectName(requested)
		const sections = dialogState.sections || []
		for (let sectionIndex = 0; sectionIndex < sections.length; ++sectionIndex) {
			const fields = (sections[sectionIndex] || {}).fields || []
			for (let fieldIndex = 0; fieldIndex < fields.length; ++fieldIndex) {
				const field = fields[fieldIndex] || {}
				if (field.initialFocus || field.defaultFocus)
					return normalizedFocusObjectName(field.id)
			}
		}
		const primary = String(dialogState.primaryActionId || "")
		return primary.length > 0 ? "dialogAction_" + primary : "dialogCloseButton"
	}
	function applyInitialFocus() {
		if (!visible || !contentItem) return
		const requested = metadataFocusId()
		let target = focusObjectInTree(contentItem, requested)
		if (!target && requested.indexOf("dialogField_") !== 0)
			target = focusObjectInTree(contentItem, "dialogField_" + requested)
		if (!target)
			target = focusObjectInTree(contentItem, "dialogAction_" + String(dialogState.primaryActionId || ""))
		if (!target)
			target = focusObjectInTree(contentItem, "dialogCloseButton")
		if (target && target.enabled !== false && target.visible !== false && target.forceActiveFocus) {
			target.forceActiveFocus(Qt.TabFocusReason)
			Qt.callLater(function() { dialog.ensureContentVisible(target) })
		}
	}
	function ensureContentVisible(target) {
		if (!target || !dialogContentScroll || !dialogContentScroll.contentItem) return
		const flickable = dialogContentScroll.contentItem
		const contentRoot = flickable.contentItem || flickable
		const point = target.mapToItem(contentRoot, 0, 0)
		const margin = Theme.spacing
		const top = flickable.contentY
		const bottom = top + flickable.height
		if (point.y < top + margin)
			flickable.contentY = Math.max(0, point.y - margin)
		else if (point.y + target.height > bottom - margin)
			flickable.contentY = Math.min(Math.max(0, flickable.contentHeight - flickable.height),
				point.y + target.height - flickable.height + margin)
	}
	function resetContentPosition() {
		if (dialogContentScroll && dialogContentScroll.contentItem)
			dialogContentScroll.contentItem.contentY = 0
	}
	function measureContentHeight() {
		if (!dialogContentColumn) return
		const nextHeight = Math.max(0, Math.ceil(dialogContentColumn.implicitHeight))
		if (measuredContentHeight !== nextHeight)
			measuredContentHeight = nextHeight
	}
	function scheduleContentMeasurement() {
		Qt.callLater(measureContentHeight)
	}
	function safeRenderImageSource(value) {
		const source = String(value === undefined || value === null ? "" : value).trim()
		return /^(image:\/\/mumble\/|qrc:\/)/i.test(source) ? source : ""
	}
	function optionForFieldValue(fieldId, value) {
		const sections = dialogState.sections || []
		for (let sectionIndex = 0; sectionIndex < sections.length; ++sectionIndex) {
			const fields = (sections[sectionIndex] || {}).fields || []
			for (let fieldIndex = 0; fieldIndex < fields.length; ++fieldIndex) {
				const field = fields[fieldIndex] || {}
				if (String(field.id || "") !== String(fieldId || "")) continue
				const options = field.options || []
				for (let optionIndex = 0; optionIndex < options.length; ++optionIndex) {
					if (String((options[optionIndex] || {}).value) === String(value))
						return options[optionIndex]
				}
			}
		}
		return ({})
	}
	function settingsPageIcon(pageId, pageLabel) {
		const value = (String(pageId || "") + " " + String(pageLabel || "")).toLowerCase()
		if (value.indexOf("audio input") >= 0 || value.indexOf("input") >= 0) return "microphone"
		if (value.indexOf("audio output") >= 0 || value.indexOf("output") >= 0) return "volume"
		if (value.indexOf("appearance") >= 0 || value.indexOf("look") >= 0) return "eye"
		if (value.indexOf("message") >= 0 || value.indexOf("sound") >= 0) return "message"
		if (value.indexOf("key") >= 0 || value.indexOf("binding") >= 0
			|| value.indexOf("shortcut") >= 0) return "key"
		if (value.indexOf("network") >= 0) return "connect"
		if (value.indexOf("screen") >= 0 || value.indexOf("share") >= 0) return "screen-share"
		if (value.indexOf("plugin") >= 0) return "plugin"
		if (value.indexOf("about") >= 0) return "info"
		return "settings"
	}
	function activeSettingsPage() {
		dialogState.revision
		const pages = dialogState.pages || []
		for (let index = 0; index < pages.length; ++index) {
			const page = pages[index] || {}
			if (String(page.id || "") === String(dialogState.activePage || "")) return page
		}
		return pages.length > 0 ? (pages[0] || {}) : ({})
	}
	function settingsPageCategory(page) {
		const id = String(page.id || "").toLowerCase()
		const value = (String(page.id || "") + " " + String(page.label || page.title || "")).toLowerCase()
		if (id === "audioinput" || id === "audiooutput" || value.indexOf("audio") >= 0
			|| value.indexOf("input") >= 0 || value.indexOf("output") >= 0)
			return qsTr("Audio")
		if (id === "look" || id === "ui" || value.indexOf("appearance") >= 0
			|| value.indexOf("interface") >= 0)
			return qsTr("Experience")
		if (id === "messages" || id === "keys" || value.indexOf("message") >= 0
			|| value.indexOf("binding") >= 0)
			return qsTr("Interaction")
		if (id === "network" || id === "screenshare" || id === "plugins"
			|| value.indexOf("network") >= 0 || value.indexOf("screen") >= 0 || value.indexOf("plugin") >= 0)
			return qsTr("Connectivity")
		return qsTr("Settings")
	}
	function advancedFieldCount() {
		dialogState.revision
		let count = 0
		const sections = dialogState.sections || []
		for (let sectionIndex = 0; sectionIndex < sections.length; ++sectionIndex) {
			const section = sections[sectionIndex] || {}
			const fields = section.fields || []
			if (section.advanced) count += Math.max(1, fields.length)
			else for (let fieldIndex = 0; fieldIndex < fields.length; ++fieldIndex)
				if (fields[fieldIndex] && fields[fieldIndex].advanced) ++count
		}
		return count
	}
	function highlightColor(tone) {
		const normalized = String(tone || "").toLowerCase()
		if (normalized === "good" || normalized === "success") return Theme.success
		if (normalized === "warning") return Theme.warning
		if (normalized === "danger" || normalized === "error") return Theme.danger
		return Theme.accent
	}
	function isAppearancePreviewField(fieldId) {
		const id = String(fieldId || "")
		return id === "look.modernTheme"
			|| id === "look.modernDensity"
			|| id === "look.modernAccent"
			|| id === "look.modernCustomAccent"
			|| id === "look.modernCustomAccentStrength"
	}
	function updateFieldValue(fieldId, value) {
		if (isAppearancePreviewField(fieldId)) {
			dialogState.invokeAction("look.previewAppearance", {
				"fieldId": String(fieldId),
				"value": value
			})
			return
		}
		dialogState.updateField(fieldId, value)
	}
	function syncVisibility() {
		const shouldBeVisible = dialogState.open && dialogState.kind !== "imageViewer"
		if (shouldBeVisible && !visible) {
			if (dialog.beforeOpen && dialog.beforeOpen() === false)
				return
			dialog.open()
		} else if (!shouldBeVisible && visible)
			dialog.close()
	}
    parent: Overlay.overlay
	modal: true
	focus: true
	title: dialogState.title
	// Dialog creates a style-owned title bar and button box when these are left
	// unspecified. Product dialogs render both surfaces below so keep the native
	// Control scaffolding out of the visual and accessibility trees.
	header: null
	footer: null
	width: Math.min(parent ? Math.max(240, parent.width - (Theme.space5 * 2)) : 1040,
		Math.max(320, Math.min(dialogState.preferredWidth || 920, 1180)))
	height: Math.min(maximumHeightForParent, responsiveHeight)
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    padding: 0
	closePolicy: Popup.NoAutoClose
	onOpened: {
		nestedModalOpen = false
		resetContentPosition()
		scheduleContentMeasurement()
		Qt.callLater(applyInitialFocus)
	}
	onVisibleChanged: if (visible) Qt.callLater(applyInitialFocus)
	Component.onCompleted: syncVisibility()
	Connections {
		target: dialogState
		function onStateChanged() {
			dialog.syncVisibility()
			const nextId = String(dialogState.state.id || "")
			if (dialog.focusedDialogId !== nextId) {
				dialog.focusedDialogId = nextId
				dialog.showAdvanced = !!dialogState.state.showAdvanced
			}
			dialog.scheduleContentMeasurement()
			Qt.callLater(dialog.applyInitialFocus)
		}
	}
	Shortcut {
		sequence: StandardKey.Cancel
		onActivated: dialogState.requestClose()
	}

    background: Rectangle {
        color: Theme.shellBackground
		border.color: dialog.dialogTone.length > 0 ? dialog.dialogToneColor : Theme.divider
		border.width: dialog.dialogTone.length > 0 ? 2 : 1
        radius: Theme.shellRadius
    }

	contentItem: ColumnLayout {
		enabled: !dialog.nestedModalOpen
		Accessible.role: Accessible.Dialog
		Accessible.name: dialogState.title
		Accessible.description: dialogState.subtitle
		spacing: 0

        Rectangle {
			id: dialogHeader
            Layout.fillWidth: true
			Layout.preferredHeight: dialog.headerHeight
            color: Theme.panel
			border.color: dialog.dialogTone.length > 0 ? dialog.dialogToneColor : Theme.divider
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: dialog.densityInset + 2
                anchors.rightMargin: Theme.spacing
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 3
					Label {
						objectName: "dialogProductEyebrow"
						visible: dialogState.kind === "certificate" || dialogState.kind === "connect"
						textFormat: Text.PlainText
						text: qsTr("Mumble").toUpperCase()
						color: Theme.accent
						font.pixelSize: 9
						font.bold: true
						font.letterSpacing: 0.8
						Accessible.ignored: true
					}
					Label {
						objectName: "dialogTitleLabel"
						textFormat: Text.PlainText
						text: dialogState.title
						color: Theme.textStrong
						font.pixelSize: 19
						font.bold: true
						Accessible.role: Accessible.Heading
						Accessible.name: text
					}
                    Label {
						textFormat: Text.PlainText
                        Layout.fillWidth: true
                        text: dialogState.subtitle
                        visible: text.length > 0
                        color: Theme.textMuted
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                }
				ModernButton {
					visible: dialog.hasAdvancedContent
						&& (dialogState.kind !== "settings" || dialog.compactDialogLayout)
					dense: true
					objectName: "dialogAdvancedToggle"
					text: dialog.showAdvanced ? qsTr("Basic") : qsTr("Advanced")
					tone: "secondary"
					checkable: true
					checked: dialog.showAdvanced
					Accessible.name: dialog.showAdvanced ? qsTr("Hide advanced settings") : qsTr("Show advanced settings")
					onClicked: dialog.showAdvanced = !dialog.showAdvanced
				}
				ModernIconButton {
					objectName: "dialogCloseButton"
					iconName: "close"
					text: qsTr("Close")
					Accessible.name: qsTr("Close dialog")
					onClicked: dialogState.requestClose()
				}
            }
        }

		Rectangle {
			id: dialogStatusBanner
			objectName: "dialogStatusBanner"
			Layout.fillWidth: true
			Layout.preferredHeight: visible ? statusLabel.implicitHeight + (Theme.spacing * 2) : 0
			visible: String(dialogState.statusMessage || "").length > 0
			color: Qt.rgba(dialog.dialogToneColor.r, dialog.dialogToneColor.g, dialog.dialogToneColor.b, 0.12)
			border.color: dialog.dialogToneColor
			Accessible.role: dialog.dialogTone === "danger" || dialog.dialogTone === "error"
				|| dialog.dialogTone === "warning" || dialog.dialogTone === "retry"
				? Accessible.AlertMessage : Accessible.StatusBar
			Accessible.name: String(dialogState.statusMessage || "")
			Label {
				id: statusLabel
				objectName: "dialogStatusMessage"
				anchors.fill: parent
				anchors.margins: Theme.spacing
				textFormat: Text.PlainText
				text: dialogState.statusMessage
				color: Theme.textMain
				wrapMode: Text.Wrap
				Accessible.ignored: true
			}
		}

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Rectangle {
				objectName: "dialogPageRail"
				Layout.preferredWidth: visible ? 214 : 0
                Layout.fillHeight: true
				visible: dialogState.pages.length > 0 && !dialog.compactDialogLayout
                color: Theme.rail
                border.color: Theme.divider
				Label {
					id: settingsRailLabel
					objectName: "dialogPageRailHeading"
					anchors.left: parent.left
					anchors.right: parent.right
					anchors.top: parent.top
					anchors.leftMargin: Theme.space3
					anchors.rightMargin: Theme.space3
					anchors.topMargin: Theme.space3
					visible: dialogState.kind === "settings"
					textFormat: Text.PlainText
					text: qsTr("Settings").toUpperCase()
					color: Theme.textMuted
					font.pixelSize: 10
					font.bold: true
					font.letterSpacing: 1.1
					Accessible.role: Accessible.Heading
					Accessible.name: qsTr("Settings categories")
				}
                ListView {
					id: dialogPageList
					objectName: "dialogPageList"
					anchors.left: parent.left
					anchors.right: parent.right
					anchors.bottom: parent.bottom
					anchors.top: settingsRailLabel.visible ? settingsRailLabel.bottom : parent.top
					anchors.leftMargin: Math.max(8, Theme.spacing - 2)
					anchors.rightMargin: Math.max(8, Theme.spacing - 2)
					anchors.bottomMargin: Math.max(8, Theme.spacing - 2)
					anchors.topMargin: settingsRailLabel.visible ? Theme.space2 : Math.max(8, Theme.spacing - 2)
                    model: dialogState.pages
                    clip: true
                    spacing: Math.max(2, Math.round(Theme.spacing / 4))
					activeFocusOnTab: count > 0
					keyNavigationEnabled: true
					currentIndex: {
						dialogState.revision
						for (let index = 0; index < dialogState.pages.length; ++index) {
							if (String(dialogState.pages[index].id || "") === String(dialogState.activePage || ""))
								return index
						}
						return count > 0 ? 0 : -1
					}
					Accessible.role: Accessible.List
					Accessible.name: qsTr("Settings pages")
                    delegate: ItemDelegate {
						id: pageDelegate
                        required property var modelData
						required property int index
						objectName: "dialogPage_" + String(modelData.id || index)
						readonly property bool current: ListView.isCurrentItem
						readonly property bool keyboardFocused: activeFocus
							|| (ListView.view.activeFocus && current)
                        width: ListView.view.width
                        height: Theme.densityId === "compact" ? 36
                                : Theme.densityId === "spacious" ? 46 : 40
                        text: modelData.label || modelData.title || modelData.id
                        highlighted: modelData.selected || modelData.id === dialogState.activePage
						hoverEnabled: true
						Accessible.role: Accessible.ListItem
						Accessible.name: text
						Accessible.selected: highlighted
						contentItem: RowLayout {
							spacing: Theme.space2
							ModernIcon {
								objectName: "dialogPageIcon_" + String(pageDelegate.modelData.id || pageDelegate.index)
								name: dialog.settingsPageIcon(pageDelegate.modelData.id, pageDelegate.text)
								size: 17
								color: pageDelegate.highlighted ? Theme.accent : Theme.textMuted
							}
							Label {
								Layout.fillWidth: true
								textFormat: Text.PlainText
								text: pageDelegate.text
								color: pageDelegate.highlighted ? Theme.textStrong : Theme.textMain
								font.pixelSize: 12
								font.bold: pageDelegate.highlighted
								verticalAlignment: Text.AlignVCenter
								elide: Text.ElideRight
							}
						}
						background: Rectangle {
							color: pageDelegate.highlighted ? Theme.selected
								: pageDelegate.hovered ? Theme.surfaceHover : "transparent"
							border.color: pageDelegate.keyboardFocused ? Theme.focus
								: pageDelegate.highlighted ? Theme.accent : "transparent"
							border.width: pageDelegate.keyboardFocused ? Theme.focusRingWidth : 1
							radius: 6
						}
                        onClicked: dialogState.invokeAction("selectPage", { "pageId": modelData.id })
						Keys.onReturnPressed: event => {
							event.accepted = true
							dialogState.invokeAction("selectPage", { "pageId": modelData.id })
						}
                    }
                }
            }

			ColumnLayout {
				objectName: "dialogBody"
				Layout.fillWidth: true
				Layout.fillHeight: true
				spacing: 0
				Rectangle {
					objectName: "dialogCompactPageBar"
					Layout.fillWidth: true
					Layout.preferredHeight: visible ? compactPageSelector.implicitHeight + (Theme.spacing * 2) : 0
					visible: dialog.compactDialogLayout && dialogState.pages.length > 0
					color: Theme.rail
					border.color: Theme.divider
					ModernComboBox {
						id: compactPageSelector
						objectName: "dialogCompactPageSelector"
						anchors.fill: parent
						anchors.margins: Theme.spacing
						model: dialogState.pages
						textRole: "label"
						displayText: {
							dialogState.revision
							const page = currentIndex >= 0 && currentIndex < dialogState.pages.length
								? dialogState.pages[currentIndex] : null
							return page ? (page.label || page.title || page.id || "") : ""
						}
						currentIndex: {
							dialogState.revision
							for (let index = 0; index < dialogState.pages.length; ++index) {
								if (String(dialogState.pages[index].id || "") === String(dialogState.activePage || ""))
									return index
							}
							return dialogState.pages.length > 0 ? 0 : -1
						}
						onActivated: index => {
							const page = index >= 0 && index < dialogState.pages.length ? dialogState.pages[index] : null
							if (page)
								dialogState.invokeAction("selectPage", { "pageId": page.id })
						}
						Accessible.name: qsTr("Settings page")
					}
				}
				Rectangle {
					id: settingsPageHeading
					objectName: "dialogSettingsPageHeading"
					readonly property var page: dialog.activeSettingsPage()
					Layout.fillWidth: true
					Layout.preferredHeight: visible ? 62 : 0
					visible: dialogState.kind === "settings" && dialogState.pages.length > 0
					color: Theme.shellBackground
					border.color: Theme.divider
					Accessible.role: Accessible.Pane
					Accessible.name: String(page.label || page.title || page.id || qsTr("Settings page"))
					RowLayout {
						anchors.fill: parent
						anchors.leftMargin: dialog.densityInset
						anchors.rightMargin: dialog.densityInset
						spacing: Theme.space3
						Rectangle {
							Layout.preferredWidth: 34
							Layout.preferredHeight: 34
							radius: 9
							color: Theme.selected
							ModernIcon {
								anchors.centerIn: parent
								name: dialog.settingsPageIcon(settingsPageHeading.page.id,
									settingsPageHeading.page.label || settingsPageHeading.page.title)
								size: 18
								color: Theme.accent
							}
						}
						ColumnLayout {
							Layout.fillWidth: true
							spacing: 1
							Label {
								objectName: "dialogSettingsPageCategory"
								textFormat: Text.PlainText
								text: dialog.settingsPageCategory(settingsPageHeading.page).toUpperCase()
								color: Theme.textMuted
								font.pixelSize: 9
								font.bold: true
								font.letterSpacing: 0.9
							}
							Label {
								objectName: "dialogSettingsPageTitle"
								Layout.fillWidth: true
								textFormat: Text.PlainText
								text: settingsPageHeading.page.label || settingsPageHeading.page.title || ""
								color: Theme.textStrong
								font.pixelSize: 17
								font.bold: true
								elide: Text.ElideRight
								Accessible.role: Accessible.Heading
								Accessible.name: text
							}
						}
					}
				}
				ScrollView {
					id: dialogContentScroll
					objectName: "dialogContentScroll"
					Layout.fillWidth: true
					Layout.fillHeight: true
					clip: true
					contentWidth: availableWidth
					contentHeight: dialogContentColumn.implicitHeight
					ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
					ScrollBar.vertical.policy: ScrollBar.AsNeeded
					Column {
						id: dialogContentColumn
						width: parent.width
						spacing: Theme.spacing + 4
						onImplicitHeightChanged: dialog.scheduleContentMeasurement()
					Rectangle {
						id: loadingScaffold
						objectName: "dialogLoadingScaffold"
						width: parent.width - (dialog.densityInset * 2)
						x: dialog.densityInset
						height: visible ? loadingColumn.implicitHeight + (dialog.sectionPadding * 2) : 0
						visible: dialogState.loading
						color: Theme.panel
						border.color: Theme.divider
						radius: Theme.innerRadius
						Accessible.role: Accessible.Pane
						Accessible.name: qsTr("Loading %1").arg(dialogState.loadingScaffold || qsTr("content"))
						ColumnLayout {
							id: loadingColumn
							anchors.left: parent.left
							anchors.right: parent.right
							anchors.top: parent.top
							anchors.margins: dialog.sectionPadding
							spacing: Theme.spacing
							RowLayout {
								ModernBusyIndicator {
									objectName: "dialogLoadingIndicator"
									running: loadingScaffold.visible
									Layout.preferredWidth: 28
									Layout.preferredHeight: 28
									Accessible.name: qsTr("Loading dialog content")
								}
								ColumnLayout {
									Layout.fillWidth: true
									Label { textFormat: Text.PlainText; text: qsTr("Loading…"); color: Theme.textStrong; font.bold: true }
									Label {
										textFormat: Text.PlainText
										Layout.fillWidth: true
										text: dialogState.loadingScaffold === "acl" ? qsTr("Retrieving room permissions and user groups")
											  : dialogState.loadingScaffold === "records" ? qsTr("Retrieving server records")
											  : qsTr("Preparing content")
										color: Theme.textMuted
										font.pixelSize: 11
									}
								}
							}
							Repeater {
								model: dialogState.loadingScaffold === "acl" ? 4 : 3
								delegate: Rectangle {
									required property int index
									Layout.fillWidth: true
									Layout.preferredHeight: dialogState.loadingScaffold === "acl" ? 48 : 38
									radius: 7
									color: index % 2 === 0 ? Theme.strip : Theme.rail
									opacity: 0.72
								}
							}
						}
					}
					Rectangle {
						id: certificateHighlights
						objectName: "certificateHighlights"
						readonly property var highlightRows: dialogState.state.highlights || []
						width: parent.width - (dialog.densityInset * 2)
						x: dialog.densityInset
						height: visible ? 66 : 0
						visible: dialogState.kind === "certificate" && highlightRows.length > 0
						color: "transparent"
						Accessible.role: Accessible.Pane
						Accessible.name: qsTr("Certificate summary")
						RowLayout {
							anchors.fill: parent
							spacing: Theme.space2
							Repeater {
								model: certificateHighlights.highlightRows
								delegate: Rectangle {
									required property var modelData
									required property int index
									objectName: "certificateHighlight_" + index
									readonly property color highlightTone: dialog.highlightColor(modelData.tone)
									Layout.fillWidth: true
									Layout.fillHeight: true
									radius: Theme.innerRadius
									color: Qt.rgba(highlightTone.r, highlightTone.g, highlightTone.b,
										String(modelData.tone || "").length > 0 ? 0.1 : 0.035)
									border.color: String(modelData.tone || "").length > 0
										? Qt.rgba(highlightTone.r, highlightTone.g, highlightTone.b, 0.58)
										: Theme.surfaceBorder
									Accessible.role: Accessible.Pane
									Accessible.name: qsTr("%1: %2").arg(modelData.label || "").arg(modelData.value || "")
									ColumnLayout {
										anchors.fill: parent
										anchors.margins: Theme.space2
										spacing: 2
										Label {
											Layout.fillWidth: true
											textFormat: Text.PlainText
											text: String(modelData.label || "").toUpperCase()
											color: Theme.textMuted
											font.pixelSize: 9
											font.bold: true
											font.letterSpacing: 0.7
											Accessible.ignored: true
										}
										Label {
											Layout.fillWidth: true
											textFormat: Text.PlainText
											text: modelData.value || ""
											color: String(modelData.tone || "").length > 0 ? highlightTone : Theme.textStrong
											font.pixelSize: 13
											font.bold: true
											elide: Text.ElideRight
											Accessible.ignored: true
										}
									}
								}
							}
						}
					}
					Rectangle {
						id: connectSurface
						objectName: "connectFavoriteSurface"
						width: parent.width - (dialog.densityInset * 2)
						x: dialog.densityInset
						height: visible ? connectSurfaceLayout.implicitHeight + (dialog.sectionPadding * 2) : 0
						visible: dialogState.kind === "connect" && !dialogState.loading
						color: Theme.panel
						border.color: Theme.divider
						radius: Theme.innerRadius
						ColumnLayout {
							id: connectSurfaceLayout
							anchors.fill: parent
							anchors.margins: dialog.sectionPadding
							spacing: Math.max(6, Theme.spacing - 2)
							RowLayout {
								Layout.fillWidth: true
								ColumnLayout {
									Layout.fillWidth: true
									spacing: 1
									Label { textFormat: Text.PlainText; text: qsTr("Saved servers"); color: Theme.textStrong; font.bold: true; font.pixelSize: 13 }
									Label {
										textFormat: Text.PlainText
										text: dialogState.favorites.length === 0 ? qsTr("No saved servers yet")
											  : qsTr("%n saved server(s)", "", dialogState.favorites.length)
										color: Theme.textMuted
										font.pixelSize: 11
									}
								}
								ModernButton {
					objectName: "connectNewFavoriteButton"
					text: qsTr("Add server")
					tone: "secondary"
									onClicked: dialogState.invokeAction("newFavorite", {})
								}
							}
							ListView {
								id: connectFavoriteList
								objectName: "connectFavoriteList"
								readonly property int favoriteRowHeight: Theme.rowHeight + Theme.space4 + 2
								readonly property int visibleRowCount: Math.max(1, Math.min(count, 3))
								Layout.fillWidth: true
								Layout.preferredHeight: count === 0 ? Theme.rowHeight * 2
									: visibleRowCount * favoriteRowHeight + Math.max(0, visibleRowCount - 1) * spacing
								clip: true
								spacing: 5
								model: dialogState.favorites
								currentIndex: dialogState.selectedFavoriteIndex
								activeFocusOnTab: visible && count > 0
								Accessible.role: Accessible.List
								Accessible.name: qsTr("Saved servers")
								delegate: ItemDelegate {
									id: favoriteDelegate
									required property var modelData
									required property int index
									objectName: "connectFavorite_" + index
									readonly property bool current: ListView.isCurrentItem
									readonly property bool keyboardFocused: activeFocus
										|| (ListView.view.activeFocus && current)
									width: ListView.view.width
									height: connectFavoriteList.favoriteRowHeight
									highlighted: !!modelData.selected || index === dialogState.selectedFavoriteIndex
									hoverEnabled: true
									Accessible.role: Accessible.ListItem
									Accessible.name: String(modelData.label || modelData.host || qsTr("Saved server"))
									Accessible.description: String(modelData.subtitle || modelData.tooltip || "")
									Accessible.selected: highlighted
									background: Rectangle {
										objectName: "connectFavoriteBackground_" + favoriteDelegate.index
										radius: 8
										color: favoriteDelegate.highlighted ? Theme.selected
											: favoriteDelegate.hovered ? Theme.surfaceHover : "transparent"
										border.color: favoriteDelegate.keyboardFocused ? Theme.focus
											: favoriteDelegate.highlighted ? Theme.accent : "transparent"
										border.width: favoriteDelegate.keyboardFocused ? Theme.focusRingWidth : 1
										Behavior on color { ColorAnimation { duration: Theme.motionFast } }
									}
									contentItem: RowLayout {
										spacing: Theme.spacing
										ColumnLayout {
											Layout.fillWidth: true
											spacing: 2
											Label { Layout.fillWidth: true; textFormat: Text.PlainText; text: modelData.label || modelData.host || ""; color: Theme.textStrong; font.bold: true; elide: Text.ElideRight }
										Label { Layout.fillWidth: true; textFormat: Text.PlainText; text: modelData.subtitle || modelData.tooltip || ""; color: Theme.textMuted; font.pixelSize: 10; elide: Text.ElideRight }
										}
										Label { textFormat: Text.PlainText; text: modelData.usersValue || "–"; color: Theme.textMuted; Accessible.name: modelData.usersLabel || "" }
										Label { textFormat: Text.PlainText; text: modelData.pingValue || "–"; color: Theme.textMuted; Accessible.name: modelData.pingLabel || "" }
										ModernIcon {
											objectName: "connectFavoriteSelection_" + index
											name: favoriteDelegate.highlighted ? "check" : "next"
											size: 16
											color: favoriteDelegate.highlighted ? Theme.accent : Theme.textMuted
										}
									}
									onClicked: dialogState.invokeAction("selectFavorite", { "index": index, "edit": false })
									onDoubleClicked: dialogState.invokeAction("connectFavorite", { "index": index })
								}
								Label {
									textFormat: Text.PlainText
									anchors.centerIn: parent
									visible: connectFavoriteList.count === 0
									text: qsTr("Add a server to get started.")
									color: Theme.textMuted
								}
							}
							Label {
								textFormat: Text.PlainText
								objectName: "connectEditorTitle"
								Layout.fillWidth: true
								visible: dialogState.editorOpen
								text: dialogState.editorTitle
								color: Theme.accent
								font.bold: true
							}
						}
					}
                    Loader {
                        id: screenShareLoader
						objectName: "screenShareEditorLoader"
						width: parent.width - (dialog.densityInset * 2)
						x: dialog.densityInset
                        active: dialogState.kind === "screenShare"
                        visible: active
                        sourceComponent: screenShareEditorComponent
                    }
					Binding {
						target: screenShareLoader.item
						property: "shareState"
						value: dialogState.state.screenShare || ({})
						when: screenShareLoader.status === Loader.Ready && screenShareLoader.item !== null
						restoreMode: Binding.RestoreNone
					}
					Connections {
						target: screenShareLoader.item
						enabled: screenShareLoader.status === Loader.Ready && screenShareLoader.item !== null
						function onThumbnailRequested(sourceId) {
							dialogState.invokeAction("screenShare.thumbnail", { "sourceId": sourceId })
						}
					}
                    Loader {
						id: stonksLoader
						objectName: "stonksEditorLoader"
						width: parent.width - (dialog.densityInset * 2)
						x: dialog.densityInset
                        active: dialogState.kind === "stonks"
                        visible: active
                        sourceComponent: stonksEditorComponent
                    }
					Binding {
						target: stonksLoader.item
						property: "stonks"
						value: dialogState.state.stonks || ({})
						when: stonksLoader.status === Loader.Ready && stonksLoader.item !== null
						restoreMode: Binding.RestoreNone
					}
					GridLayout {
						id: certificateSectionRow
						objectName: "dialogCertificateColumns"
						width: parent.width - (dialog.densityInset * 2)
						x: dialog.densityInset
						height: visible ? implicitHeight : 0
						visible: dialogState.kind === "certificate" && !dialogState.loading
						columns: dialog.compactDialogLayout ? 1 : 2
						columnSpacing: Theme.spacing + 4
						rowSpacing: Theme.spacing + 4
						Loader {
							id: certificateCurrentSection
							readonly property var sectionData: dialogState.sections.length > 0
								? dialogState.sections[0] : ({})
							Layout.fillWidth: true
							Layout.alignment: Qt.AlignTop
							Layout.preferredHeight: item ? item.implicitHeight : 0
							active: certificateSectionRow.visible && dialogState.sections.length > 0
							sourceComponent: certificateSectionCardComponent
							onLoaded: item.sectionData = sectionData
							Binding {
								target: certificateCurrentSection.item
								property: "sectionData"
								value: certificateCurrentSection.sectionData
								when: certificateCurrentSection.status === Loader.Ready
									&& certificateCurrentSection.item !== null
								restoreMode: Binding.RestoreNone
							}
						}
						Loader {
							id: certificateActionSection
							readonly property var sectionData: dialogState.sections.length > 1
								? dialogState.sections[1] : ({})
							Layout.fillWidth: true
							Layout.alignment: Qt.AlignTop
							Layout.preferredHeight: item ? item.implicitHeight : 0
							active: certificateSectionRow.visible && dialogState.sections.length > 1
							sourceComponent: certificateSectionCardComponent
							onLoaded: item.sectionData = sectionData
							Binding {
								target: certificateActionSection.item
								property: "sectionData"
								value: certificateActionSection.sectionData
								when: certificateActionSection.status === Loader.Ready
									&& certificateActionSection.item !== null
								restoreMode: Binding.RestoreNone
							}
						}
					}
					Repeater {
						visible: dialogState.kind !== "screenShare" && dialogState.kind !== "stonks"
								 && dialogState.kind !== "certificate" && !dialogState.loading
						model: visible ? dialogState.sections : []
						delegate: Rectangle {
                            required property var modelData
							required property int index
							objectName: "dialogSection_" + String(modelData.id || index)
							property bool sectionAdvanced: !!modelData.advanced
							property bool sectionVisible: !modelData.advanced || dialog.showAdvanced
							width: parent.width - (dialog.densityInset * 2)
							x: dialog.densityInset
							height: sectionVisible ? sectionColumn.implicitHeight + (dialog.sectionPadding * 2) : 0
							visible: sectionVisible
                            color: Theme.panel
                            border.color: Theme.divider
                            radius: Theme.innerRadius
							Accessible.role: Accessible.Pane
							Accessible.name: String(modelData.title || qsTr("Dialog section"))
                            Column {
                                id: sectionColumn
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: dialog.sectionPadding
                                spacing: Math.max(6, Theme.spacing - 2)
                                Label {
									textFormat: Text.PlainText
                                    width: parent.width
                                    text: modelData.title || ""
                                    visible: text.length > 0
                                    color: Theme.textStrong
                                    font.pixelSize: 13
                                    font.bold: true
                                }
								Label {
									textFormat: Text.PlainText
									width: parent.width
									text: modelData.subtitle || ""
									visible: text.length > 0
									color: Theme.textMuted
									font.pixelSize: 11
									wrapMode: Text.Wrap
								}
                                Repeater {
                                    model: modelData.fields || []
                                    delegate: Item {
                                        id: fieldContainer
                                        required property var modelData
                                        width: sectionColumn.width
                                        property var field: modelData
                                        property bool conditionVisible: {
                                            dialogState.revision
                                            return !field.visibleWhen
                                                || (field.visibleWhen.values || []).indexOf(
                                                    String(dialogState.fieldValue(field.visibleWhen.fieldId))) >= 0
                                        }
										property bool advancedVisible: sectionColumn.parent.sectionVisible
																	 && (!field.advanced || dialog.showAdvanced)
										visible: conditionVisible && advancedVisible
										height: visible ? fieldLoader.height
											+ (fieldErrorLabel.visible ? fieldErrorLabel.implicitHeight + Theme.space1 : 0) : 0
										onFieldChanged: if (fieldLoader.item) fieldLoader.item.field = field

										Loader {
											id: fieldLoader
											width: parent.width
											active: fieldContainer.visible
											onLoaded: item.field = fieldContainer.field
											onItemChanged: if (item) item.field = fieldContainer.field
											sourceComponent: {
												const field = fieldContainer.field
                                            const type = field.type || "text"
                                            if (type === "hidden") return hiddenField
                                            if (type === "note") return noteField
											if (type === "voiceMeter") return voiceMeterField
											if (type === "readonly" || type === "status") return readonlyField
                                            if (type === "checkbox" || type === "toggle") return checkboxField
											if (type === "select" || type === "combo" || type === "dropdown") {
												if (field.presentation === "themeGrid") return themeGridField
												if (field.presentation === "accentGrid") return accentGridField
												return selectField
											}
                                            if (type === "slider" || type === "range" || type === "number" || type === "integer") return numberField
                                            if (type === "action" || type === "button") return actionField
                                            if (type === "pluginEditor") return pluginEditorField
                                            if (type === "messageEventEditor") return messageEventEditorField
                                            if (type === "shortcutEditor") return shortcutEditorField
                                            if (type === "aclEditor") return aclEditorField
                                            if (type === "textarea") return textareaField
                                            if (type === "resultList") return resultListField
                                            if (type === "color") return colorField
                                            if (type === "profile") return profileField
                                            if (type === "imagePicker") return pathField
                                            if (type === "manualPositionPreview") return manualPreviewField
                                            if (type === "pathPicker" || type === "filePicker" || type === "folderPicker") return pathField
                                            return textField
                                        }
										}
                                        Label {
											textFormat: Text.PlainText
                                            id: fieldErrorLabel
											objectName: "dialogFieldError_" + fieldContainer.field.id
                                            anchors.left: parent.left
                                            anchors.right: parent.right
											anchors.top: fieldLoader.bottom
                                            anchors.topMargin: Theme.space1
                                            text: {
                                                dialogState.revision
												return dialogState.fieldError(fieldContainer.field.id)
                                            }
                                            visible: text.length > 0
                                            color: Theme.danger
                                            font.pixelSize: Theme.fontCaption
                                            wrapMode: Text.Wrap
											Accessible.role: Accessible.AlertMessage
											Accessible.name: text
                                        }
                                    }
                                }
                            }
						}
					}
					Item {
						objectName: "dialogContentEndPadding"
						width: 1
						height: dialog.densityInset
					}
                }
            }
        }
	}

		Rectangle {
			objectName: "dialogFooter"
            Layout.fillWidth: true
			Layout.preferredHeight: dialog.footerHeight
            color: Theme.strip
            border.color: Theme.divider
			border.width: 1
			Row {
				id: settingsAdvancedFooter
				objectName: "dialogSettingsAdvancedFooter"
				anchors.left: parent.left
				anchors.leftMargin: Theme.spacing
				anchors.verticalCenter: parent.verticalCenter
				visible: dialogState.kind === "settings" && dialog.hasAdvancedContent
					&& !dialog.compactDialogLayout
				spacing: Theme.space2
				Accessible.role: Accessible.Pane
				Accessible.name: dialog.showAdvanced ? qsTr("Advanced settings are visible")
					: qsTr("Advanced settings are hidden")
				Label {
					anchors.verticalCenter: parent.verticalCenter
					textFormat: Text.PlainText
					text: dialog.showAdvanced ? qsTr("Advanced options visible")
						: qsTr("%n advanced option(s) hidden", "", dialog.advancedFieldCount())
					color: Theme.textMuted
					font.pixelSize: 10
					Accessible.ignored: true
				}
				ModernButton {
					objectName: "dialogSettingsAdvancedToggle"
					dense: true
					text: dialog.showAdvanced ? qsTr("Hide advanced") : qsTr("Show advanced")
					tone: "secondary"
					checkable: true
					checked: dialog.showAdvanced
					Accessible.name: dialog.showAdvanced ? qsTr("Hide advanced settings") : qsTr("Show advanced settings")
					onClicked: dialog.showAdvanced = !dialog.showAdvanced
				}
			}
			Flow {
				id: footerActions
				anchors.right: parent.right
				anchors.rightMargin: Theme.spacing
				anchors.verticalCenter: parent.verticalCenter
				width: Math.max(0, parent.width - (Theme.spacing * 2)
					- (settingsAdvancedFooter.visible ? settingsAdvancedFooter.width + Theme.spacing : 0))
				spacing: Math.max(6, Math.round(Theme.spacing / 2))
				layoutDirection: Qt.RightToLeft
                Repeater {
                    model: dialogState.actions
                    delegate: ModernButton {
                        required property var modelData
						readonly property bool primaryAction: String(modelData.id || "") === String(dialogState.primaryActionId || "")
                        objectName: "dialogAction_" + modelData.id
                        text: modelData.label || modelData.text || modelData.id
						width: dialogState.kind === "connect" && !dialogState.editorOpen
							? Math.floor((footerActions.width - Math.max(0, dialogState.actions.length - 1)
								* footerActions.spacing) / Math.max(1, dialogState.actions.length)) : implicitWidth
						dense: dialogState.kind === "settings"
						enabled: (!dialogState.loading || modelData.id === "close" || modelData.id === "cancel")
								 && (modelData.enabled === undefined || modelData.enabled)
						highlighted: primaryAction
						tone: String(modelData.tone || (primaryAction ? "accent" : "secondary"))
						Accessible.description: primaryAction ? qsTr("Primary action") : ""
						onClicked: {
                            const payload = dialogState.kind === "screenShare"
                                    && screenShareLoader.item
                                    && modelData.id === "screenShare.start"
                                ? screenShareLoader.item.actionPayload() : ({})
                            dialogState.invokeAction(modelData.id, payload)
                        }
						Keys.onSpacePressed: event => {
							event.accepted = true
							if (enabled) clicked()
						}
						Keys.onReturnPressed: event => {
							event.accepted = true
							if (enabled) clicked()
						}
                    }
                }
            }
        }
    }

	Component {
		id: certificateSectionCardComponent
		Rectangle {
			id: certificateSectionCard
			property var sectionData: ({})
			objectName: "dialogSection_" + String(sectionData.id || "certificate")
			width: parent ? parent.width : 0
			implicitHeight: certificateSectionColumn.implicitHeight + (dialog.sectionPadding * 2)
			height: implicitHeight
			color: Theme.panel
			border.color: Theme.divider
			radius: Theme.innerRadius
			Accessible.role: Accessible.Pane
			Accessible.name: String(sectionData.title || qsTr("Certificate section"))
			Column {
				id: certificateSectionColumn
				anchors.left: parent.left
				anchors.right: parent.right
				anchors.top: parent.top
				anchors.margins: dialog.sectionPadding
				spacing: Math.max(6, Theme.spacing - 2)
				Label {
					width: parent.width
					textFormat: Text.PlainText
					text: certificateSectionCard.sectionData.title || ""
					visible: text.length > 0
					color: Theme.textStrong
					font.pixelSize: 13
					font.bold: true
					Accessible.role: Accessible.Heading
					Accessible.name: text
				}
				Label {
					width: parent.width
					textFormat: Text.PlainText
					text: certificateSectionCard.sectionData.subtitle || ""
					visible: text.length > 0
					color: Theme.textMuted
					font.pixelSize: 11
					wrapMode: Text.Wrap
				}
				Repeater {
					model: certificateSectionCard.sectionData.fields || []
					delegate: Item {
						id: certificateFieldContainer
						required property var modelData
						property var field: modelData
						property bool conditionVisible: {
							dialogState.revision
							return !field.visibleWhen || (field.visibleWhen.values || []).indexOf(
								String(dialogState.fieldValue(field.visibleWhen.fieldId))) >= 0
						}
						width: certificateSectionColumn.width
						visible: conditionVisible
						height: visible ? certificateFieldLoader.height
							+ (certificateFieldError.visible
								? certificateFieldError.implicitHeight + Theme.space1 : 0) : 0
						onFieldChanged: if (certificateFieldLoader.item) certificateFieldLoader.item.field = field
						Loader {
							id: certificateFieldLoader
							width: parent.width
							active: certificateFieldContainer.visible
							onLoaded: item.field = certificateFieldContainer.field
							onItemChanged: if (item) item.field = certificateFieldContainer.field
							sourceComponent: {
								const field = certificateFieldContainer.field
								const type = field.type || "text"
								if (type === "hidden") return hiddenField
								if (type === "note") return noteField
								if (type === "readonly" || type === "status") return readonlyField
								if (type === "select" || type === "combo" || type === "dropdown") return selectField
								if (type === "pathPicker" || type === "filePicker"
									|| type === "folderPicker" || type === "imagePicker") return pathField
								return textField
							}
						}
						Label {
							id: certificateFieldError
							objectName: "dialogFieldError_" + certificateFieldContainer.field.id
							anchors.left: parent.left
							anchors.right: parent.right
							anchors.top: certificateFieldLoader.bottom
							anchors.topMargin: Theme.space1
							textFormat: Text.PlainText
							text: {
								dialogState.revision
								return dialogState.fieldError(certificateFieldContainer.field.id)
							}
							visible: text.length > 0
							color: Theme.danger
							font.pixelSize: Theme.fontCaption
							wrapMode: Text.Wrap
							Accessible.role: Accessible.AlertMessage
							Accessible.name: text
						}
					}
				}
			}
		}
	}

    Component { id: hiddenField; Item { property var field; width: parent ? parent.width : 0; height: 0 } }
    Component { id: screenShareEditorComponent; ScreenShareEditor { } }
    Component { id: stonksEditorComponent; StonksEditor { } }
    Component {
        id: noteField
        Label {
			textFormat: Text.PlainText
            property var field
            width: parent ? parent.width : 0
            text: field.text || field.label || ""
            color: Theme.textMuted
            wrapMode: Text.Wrap
        }
    }
    Component {
        id: readonlyField
        GridLayout {
            id: readonlyRoot
            property var field
			readonly property bool certificateDetail: dialogState.kind === "certificate"
				&& String(field.id || "").indexOf("cert.") === 0
            width: parent ? parent.width : 0
			columns: certificateDetail ? 2 : 1
			columnSpacing: Theme.space3
			rowSpacing: 2
			Label {
				textFormat: Text.PlainText
				text: readonlyRoot.field.label || ""
				visible: text.length > 0
				color: Theme.textMuted
				font.pixelSize: certificateDetail ? 9 : 11
				font.bold: certificateDetail
				font.letterSpacing: certificateDetail ? 0.45 : 0
				Layout.preferredWidth: certificateDetail ? 100 : implicitWidth
				Layout.alignment: Qt.AlignTop
			}
            Label {
				Layout.fillWidth: true
				textFormat: Text.PlainText
				text: String(readonlyRoot.field.value ?? "")
				color: Theme.textMain
				wrapMode: Text.WrapAnywhere
				font.family: readonlyRoot.field.monospace ? "Consolas" : ""
				font.pixelSize: readonlyRoot.field.monospace ? 11 : Theme.fontBody
			}
        }
    }
	Component {
		id: voiceMeterField
		ColumnLayout {
			id: voiceMeterRoot
			objectName: "voiceMeter_" + String((field || {}).id || "")
			property var field: ({})
			readonly property var meter: {
				const fieldId = String(field.id || "")
				const presentationValues = dialogState.presentationFieldValues || ({})
				const presentationValue = presentationValues[fieldId]
				const sourceValue = presentationValue !== undefined ? presentationValue : field.value
				return sourceValue && typeof sourceValue === "object" ? sourceValue : ({
					"available": Number(sourceValue || 0) > 0,
					"amplitude": Number(sourceValue || 0),
					"signalToNoise": Number(sourceValue || 0),
					"hybrid": Number(sourceValue || 0)
				})
			}
			readonly property int vadSource: Number(field.vadSource || 0)
			readonly property real meterValue: Math.max(0, Math.min(100,
				vadSource === 1 ? Number(meter.signalToNoise || 0)
					: vadSource === 2 ? Number(meter.hybrid || 0) : Number(meter.amplitude || 0)))
			readonly property int silenceThreshold: Math.max(0, Math.min(100, Number(field.silenceThreshold || 0)))
			readonly property int speechThreshold: Math.max(silenceThreshold,
				Math.min(100, Number(field.speechThreshold === undefined ? 100 : field.speechThreshold)))
			readonly property bool replayActive: Number(field.loopbackMode || meter.loopbackMode || 0) !== 0
			function setupPayload() {
				return {
					"silenceThreshold": silenceThreshold,
					"speechThreshold": speechThreshold,
					"vadSource": Number(field.recommendedVadSource === undefined
						? vadSource : field.recommendedVadSource),
					"voiceHold": Number(field.voiceHold || 20),
					"maxAmplification": Number(field.recommendedMaxAmplification === undefined
						? (field.maxAmplification || 0) : field.recommendedMaxAmplification),
					"noiseCancelMode": Number(field.recommendedNoiseCancelMode === undefined
						? (field.noiseCancelMode || 0) : field.recommendedNoiseCancelMode),
					"inputGateMode": Number(field.recommendedInputGateMode === undefined
						? (field.inputGateMode || 0) : field.recommendedInputGateMode),
					"speexNoiseStrength": Number(field.speexNoiseStrength || 14)
				}
			}
			width: parent ? parent.width : 0
			spacing: Math.max(6, Theme.spacing - 3)
			RowLayout {
				Layout.fillWidth: true
				ColumnLayout {
					Layout.fillWidth: true
					spacing: 1
					Label { textFormat: Text.PlainText; text: voiceMeterRoot.field.label || qsTr("Voice input"); color: Theme.textStrong; font.bold: true }
					Label {
						textFormat: Text.PlainText
						Layout.fillWidth: true
						text: voiceMeterRoot.field.sourceLabel || qsTr("Current microphone")
						color: Theme.textMuted
						font.pixelSize: 11
						elide: Text.ElideRight
					}
				}
				Rectangle {
					Layout.preferredWidth: 9
					Layout.preferredHeight: 9
					radius: 5
					color: voiceMeterRoot.meter.transmitting ? Theme.success
						  : voiceMeterRoot.field.active === false ? Theme.textMuted : Theme.accent
				}
				Label {
					textFormat: Text.PlainText
					text: voiceMeterRoot.meter.transmitting ? qsTr("Transmitting")
						  : voiceMeterRoot.meter.available === false ? qsTr("Waiting for input") : qsTr("Listening")
					color: voiceMeterRoot.meter.transmitting ? Theme.success : Theme.textMuted
					font.pixelSize: 11
				}
			}
			Rectangle {
				id: voiceMeterTrack
				objectName: "voiceMeterTrack_" + String(voiceMeterRoot.field.id || "")
				Layout.fillWidth: true
				Layout.preferredHeight: 22
				radius: 7
				color: Theme.strip
				border.color: Theme.divider
				clip: true
				Accessible.role: Accessible.ProgressBar
				Accessible.name: voiceMeterRoot.field.label || qsTr("Voice input level")
				Accessible.description: qsTr("%1 percent").arg(Math.round(voiceMeterRoot.meterValue))
				Rectangle {
					id: voiceMeterFill
					objectName: "voiceMeterFill_" + String(voiceMeterRoot.field.id || "")
					height: parent.height
					width: Math.round(parent.width * voiceMeterRoot.meterValue / 100)
					color: voiceMeterRoot.meter.transmitting ? Theme.success : Theme.accent
					opacity: voiceMeterRoot.field.active === false ? 0.42 : 0.9
					Behavior on width {
						enabled: !voiceMeterRoot.field.staticMeter
						NumberAnimation { duration: 75; easing.type: Easing.OutCubic }
					}
				}
				Rectangle {
					x: Math.round(parent.width * voiceMeterRoot.silenceThreshold / 100)
					width: 2; height: parent.height; color: Theme.warning; opacity: 0.9
				}
				Rectangle {
					x: Math.min(parent.width - width, Math.round(parent.width * voiceMeterRoot.speechThreshold / 100))
					width: 2; height: parent.height; color: Theme.success; opacity: 0.9
				}
			}
			RowLayout {
				Layout.fillWidth: true
				Label { textFormat: Text.PlainText; text: qsTr("Quiet %1%").arg(voiceMeterRoot.silenceThreshold); color: Theme.textMuted; font.pixelSize: 10 }
				Item { Layout.fillWidth: true }
				Label {
					textFormat: Text.PlainText
					text: voiceMeterRoot.meter.peakCleanMicDb !== undefined
						  ? qsTr("%1 dB · %2%").arg(Number(voiceMeterRoot.meter.peakCleanMicDb).toFixed(0)).arg(Math.round(voiceMeterRoot.meterValue))
						  : qsTr("%1%").arg(Math.round(voiceMeterRoot.meterValue))
					color: Theme.textMain
					font.pixelSize: 10
				}
				Item { Layout.fillWidth: true }
				Label { textFormat: Text.PlainText; text: qsTr("Voice %1%").arg(voiceMeterRoot.speechThreshold); color: Theme.textMuted; font.pixelSize: 10 }
			}
			Label {
				textFormat: Text.PlainText
				Layout.fillWidth: true
				visible: String(voiceMeterRoot.field.calibrationStatusText || "").length > 0
				text: voiceMeterRoot.field.calibrationStatusText || ""
				color: Theme.textMuted
				font.pixelSize: 11
				wrapMode: Text.Wrap
			}
			InputEnhancementCalibration {
				field: voiceMeterRoot.field
				controller: dialogState
			}
			Flow {
				Layout.fillWidth: true
				spacing: Math.max(6, Math.round(Theme.spacing / 2))
				ModernButton {
					objectName: "voiceMeterCalibration_" + String(voiceMeterRoot.field.id || "")
					visible: String(voiceMeterRoot.field.calibrationActionId || "").length > 0
					text: voiceMeterRoot.field.calibrationLabel || qsTr("Audio setup")
					ToolTip.visible: hovered && String(voiceMeterRoot.field.calibrationTooltip || "").length > 0
					ToolTip.text: voiceMeterRoot.field.calibrationTooltip || ""
					onClicked: dialogState.invokeAction(voiceMeterRoot.field.calibrationActionId,
														 voiceMeterRoot.setupPayload())
				}
				ModernButton {
					objectName: "voiceMeterReplay_" + String(voiceMeterRoot.field.id || "")
					visible: String(voiceMeterRoot.field.replayStartActionId || voiceMeterRoot.field.replayStopActionId || "").length > 0
					text: voiceMeterRoot.replayActive ? qsTr("Stop replay") : (voiceMeterRoot.field.replayLabel || qsTr("Replay"))
					tone: voiceMeterRoot.replayActive ? "warning" : ""
					ToolTip.visible: hovered && String(voiceMeterRoot.field.replayTooltip || "").length > 0
					ToolTip.text: voiceMeterRoot.field.replayTooltip || ""
					onClicked: {
						if (voiceMeterRoot.replayActive)
							dialogState.invokeAction(voiceMeterRoot.field.replayStopActionId, {})
						else
							dialogState.invokeAction(voiceMeterRoot.field.replayStartActionId,
														 { "mode": voiceMeterRoot.meter.connected ? "server" : "local" })
					}
				}
			}
		}
	}
    Component {
        id: checkboxField
		ModernCheckBox {
            property var field
			objectName: "dialogField_" + String((field || {}).id || "")
            width: parent ? parent.width : 0
            text: field.label || ""
            checked: !!field.value
            enabled: field.enabled === undefined || field.enabled
            onToggled: dialog.updateFieldValue(field.id, checked)
        }
    }
    Component {
        id: selectField
        ColumnLayout {
            id: selectRoot
            property var field
            function syncCurrentIndex() {
                Qt.callLater(function() {
                    const index = selectControl.indexOfValue(field.value)
                    selectControl.currentIndex = index
                })
            }
            onFieldChanged: syncCurrentIndex()
            width: parent ? parent.width : 0
			Label { textFormat: Text.PlainText; text: selectRoot.field.label || ""; color: Theme.textMuted; font.pixelSize: 11 }
            ModernComboBox {
                id: selectControl
				objectName: "dialogField_" + String((selectRoot.field || {}).id || "")
                Layout.fillWidth: true
				enabled: selectRoot.field.enabled === undefined || Boolean(selectRoot.field.enabled)
                model: selectRoot.field.options || []
                textRole: "label"
                valueRole: "value"
				Accessible.name: String(selectRoot.field.label || "")
				Accessible.description: String(selectRoot.field.hint || selectRoot.field.unavailableReason || "")
                Component.onCompleted: selectRoot.syncCurrentIndex()
                onModelChanged: selectRoot.syncCurrentIndex()
				onActivated: {
					if (currentIndex >= 0 && optionEnabled(currentIndex))
						dialog.updateFieldValue(selectRoot.field.id, currentValue)
				}
            }
			Label {
				Layout.fillWidth: true
				visible: String(selectRoot.field.hint || selectRoot.field.unavailableReason || "").length > 0
				textFormat: Text.PlainText
				text: String(selectRoot.field.hint || selectRoot.field.unavailableReason || "")
				color: Theme.textMuted
				font.pixelSize: 10
				wrapMode: Text.Wrap
			}
        }
    }
    Component {
        id: numberField
        ColumnLayout {
            id: numberRoot
            property var field
            width: parent ? parent.width : 0
			Label { textFormat: Text.PlainText; text: numberRoot.field.label || ""; color: Theme.textMuted; font.pixelSize: 11 }
            ModernSpinBox {
				objectName: "dialogField_" + String((numberRoot.field || {}).id || "")
                from: numberRoot.field.minimum ?? numberRoot.field.min ?? -100000
                to: numberRoot.field.maximum ?? numberRoot.field.max ?? 100000
                value: Number(numberRoot.field.value || 0)
                editable: true
                onValueModified: dialog.updateFieldValue(numberRoot.field.id, value)
            }
        }
    }
    Component {
        id: textField
        ColumnLayout {
            id: textRoot
            property var field
            width: parent ? parent.width : 0
			Label { textFormat: Text.PlainText; text: textRoot.field.label || ""; color: Theme.textMuted; font.pixelSize: 11 }
			ModernTextField {
				objectName: "dialogField_" + String((textRoot.field || {}).id || "")
                Layout.fillWidth: true
                text: String(textRoot.field.value ?? "")
                enabled: textRoot.field.enabled === undefined || textRoot.field.enabled
				invalid: {
					dialogState.revision
					return dialogState.fieldError(String((textRoot.field || {}).id || "")).length > 0
				}
                echoMode: textRoot.field.type === "password" ? TextInput.Password : TextInput.Normal
				onEditingFinished: {
					if (text !== String(textRoot.field.value ?? ""))
						dialog.updateFieldValue(textRoot.field.id, text)
				}
            }
        }
    }
    Component {
        id: pathField
        ColumnLayout {
            id: pathRoot
            property var field
            width: parent ? parent.width : 0
			Label { textFormat: Text.PlainText; text: pathRoot.field.label || ""; color: Theme.textMuted; font.pixelSize: 11 }
            RowLayout {
				ModernTextField {
					objectName: "dialogField_" + String((pathRoot.field || {}).id || "")
                    Layout.fillWidth: true
                    text: String(pathRoot.field.value ?? "")
					invalid: {
						dialogState.revision
						return dialogState.fieldError(String((pathRoot.field || {}).id || "")).length > 0
					}
					onEditingFinished: {
						if (text !== String(pathRoot.field.value ?? ""))
							dialog.updateFieldValue(pathRoot.field.id, text)
					}
                }
                ModernButton {
                    text: pathRoot.field.browseLabel || qsTr("Browse…")
                    onClicked: dialogState.invokeAction(pathRoot.field.browseActionId, { "fieldId": pathRoot.field.id })
                }
            }
        }
    }
    Component {
        id: manualPreviewField
        Rectangle {
            property var field
            width: parent ? parent.width : 0
            height: 170
            implicitHeight: 170
            color: Theme.strip
            radius: Theme.innerRadius
			Canvas {
				id: manualPositionCanvas
				anchors.fill: parent
                anchors.margins: 12
                onPaint: {
                    const ctx = getContext("2d"); ctx.reset();
                    ctx.strokeStyle = Theme.divider; ctx.lineWidth = 1
                    ctx.beginPath(); ctx.moveTo(width / 2, 0); ctx.lineTo(width / 2, height)
                    ctx.moveTo(0, height / 2); ctx.lineTo(width, height / 2); ctx.stroke()
                    const x = Number(dialogState.fieldValue("manual.x") || 0)
                    const z = Number(dialogState.fieldValue("manual.z") || 0)
                    const scale = Math.min(width, height) / 20
                    ctx.fillStyle = Theme.accent; ctx.beginPath()
                    ctx.arc(width / 2 + x * scale, height / 2 - z * scale, 7, 0, Math.PI * 2); ctx.fill()
                }
				Connections {
					target: dialogState
					function onStateChanged() { manualPositionCanvas.requestPaint() }
				}
			}
			Label { anchors.left: parent.left; anchors.top: parent.top; anchors.margins: 8; textFormat: Text.PlainText; text: qsTr("Top view · X / Z"); color: Theme.textMuted; font.pixelSize: 10 }
        }
    }
    Component {
        id: profileField
        RowLayout {
            property var field
            width: parent ? parent.width : 0
            property var profile: field.value || ({})
            Rectangle {
                Layout.preferredWidth: 56; Layout.preferredHeight: 56; radius: 28; color: Theme.strip; clip: true
                Image { anchors.fill: parent; source: dialog.safeRenderImageSource(parent.parent.profile.avatarUrl || ""); asynchronous: true; cache: false; sourceSize: Qt.size(width * Screen.devicePixelRatio, height * Screen.devicePixelRatio); fillMode: Image.PreserveAspectCrop }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Label { textFormat: Text.PlainText; text: parent.parent.profile.name || ""; color: Theme.textStrong; font.bold: true }
                Label { Layout.fillWidth: true; textFormat: Text.PlainText; text: parent.parent.profile.subtitle || ""; color: Theme.textMuted; elide: Text.ElideRight }
            }
            ModernButton {
                visible: (parent.profile.avatarActionId || "").length > 0
                text: parent.profile.avatarActionLabel || qsTr("Change avatar")
                onClicked: dialogState.invokeAction(parent.profile.avatarActionId, {})
            }
        }
    }
    Component {
        id: colorField
		RowLayout {
			id: colorRoot
			property var field: ({})
			readonly property bool fieldEnabled: field.enabled === undefined || !!field.enabled
            width: parent ? parent.width : 0
            Label { Layout.fillWidth: true; textFormat: Text.PlainText; text: colorRoot.field.label || ""; color: Theme.textMain }
			Button {
				id: colorButton
				objectName: "dialogColorButton_" + String(colorRoot.field.id || "")
				Layout.preferredWidth: 42
				Layout.preferredHeight: 30
				text: ""
				enabled: colorRoot.fieldEnabled
				hoverEnabled: true
				activeFocusOnTab: true
				padding: 0
				readonly property var picker: colorPicker
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Choose %1").arg(colorRoot.field.label || qsTr("color"))
				Accessible.description: String(colorRoot.field.value || "")
				background: Rectangle {
					radius: Theme.innerRadius
					color: String(colorRoot.field.value || "#000000")
					opacity: colorButton.enabled ? 1.0 : 0.55
					border.color: !colorButton.enabled ? Theme.divider
						: colorButton.activeFocus ? Theme.focus
						: colorButton.down ? Theme.accent
						: colorButton.hovered ? Theme.surfaceBorder : Theme.divider
					border.width: colorButton.activeFocus ? Theme.focusRingWidth : 1
				}
				onClicked: colorPicker.open()
            }
			Label {
				textFormat: Text.PlainText
				text: String(colorRoot.field.value || "")
				color: Theme.textMuted
				opacity: colorRoot.fieldEnabled ? 1.0 : 0.65
				font.pixelSize: 11

			Popup {
				id: colorPicker
				objectName: "dialogColorPicker_" + String(colorRoot.field.id || "")
				parent: Overlay.overlay
				modal: true
				dim: false
				focus: true
				padding: Theme.space4
				width: parent ? Math.max(248, Math.min(360, parent.width - (Theme.space4 * 2))) : 340
				x: parent ? Math.round((parent.width - width) / 2) : 0
				y: parent ? Math.round((parent.height - height) / 2) : 0
				closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
				palette.window: Theme.surfaceRaised
				palette.active.base: Theme.surfaceRaised
				palette.inactive.base: Theme.surfaceRaised
				palette.alternateBase: Theme.panel
				palette.active.button: Theme.surfaceRaised
				palette.inactive.button: Theme.surfaceRaised
				palette.active.text: Theme.textMain
				palette.inactive.text: Theme.textMain
				palette.active.windowText: Theme.textMain
				palette.inactive.windowText: Theme.textMain
				palette.active.buttonText: Theme.textStrong
				palette.inactive.buttonText: Theme.textStrong
				palette.active.brightText: Theme.textStrong
				palette.inactive.brightText: Theme.textStrong
				palette.active.highlight: Theme.selected
				palette.inactive.highlight: Theme.selected
				palette.active.highlightedText: Theme.textStrong
				palette.inactive.highlightedText: Theme.textStrong
				palette.placeholderText: Theme.textMuted
				palette.active.link: Theme.accent
				palette.inactive.link: Theme.accent
				palette.active.linkVisited: Theme.accentHover
				palette.inactive.linkVisited: Theme.accentHover
				palette.active.toolTipBase: Theme.surfaceRaised
				palette.inactive.toolTipBase: Theme.surfaceRaised
				palette.active.toolTipText: Theme.textStrong
				palette.inactive.toolTipText: Theme.textStrong
				palette.active.light: Theme.surfaceHover
				palette.inactive.light: Theme.surfaceHover
				palette.active.midlight: Theme.surfaceRaised
				palette.inactive.midlight: Theme.surfaceRaised
				palette.active.mid: Theme.surfaceBorder
				palette.inactive.mid: Theme.surfaceBorder
				palette.dark: Theme.rail
				palette.shadow: Theme.strip
				palette.disabled.window: Theme.surfaceRaised
				palette.disabled.base: Theme.panel
				palette.disabled.alternateBase: Theme.panel
				palette.disabled.button: Theme.panel
				palette.disabled.text: Theme.textMuted
				palette.disabled.windowText: Theme.textMuted
				palette.disabled.buttonText: Theme.textMuted
				palette.disabled.brightText: Theme.textMuted
				palette.disabled.highlight: Theme.surfaceBorder
				palette.disabled.highlightedText: Theme.textMuted
				palette.disabled.placeholderText: Theme.textMuted
				palette.disabled.light: Theme.surfaceBorder
				palette.disabled.midlight: Theme.panel
				palette.disabled.mid: Theme.divider
				palette.disabled.dark: Theme.rail
				palette.disabled.shadow: Theme.strip
				palette.disabled.link: Theme.textMuted
				palette.disabled.linkVisited: Theme.textMuted
				palette.disabled.toolTipBase: Theme.panel
				palette.disabled.toolTipText: Theme.textMuted
				property string draftColor: "#000000"
				function isValidHex(value) {
					return /^#[0-9a-fA-F]{6}$/.test(String(value || "").trim())
				}
				function normalizedHex(value) {
					const candidate = String(value || "").trim()
					return isValidHex(candidate) ? candidate.toUpperCase() : "#000000"
				}
				function setDraft(value) {
					draftColor = normalizedHex(value)
					hexInput.text = draftColor
				}
				function applyDraft() {
					if (!isValidHex(draftColor)) return
					dialog.updateFieldValue(colorRoot.field.id, normalizedHex(draftColor))
					close()
				}
				onOpened: {
					dialog.nestedModalOpen = true
					setDraft(colorRoot.field.value || "#000000")
					Qt.callLater(function() {
						hexInput.forceActiveFocus(Qt.PopupFocusReason)
						hexInput.selectAll()
					})
				}
				onClosed: {
					dialog.nestedModalOpen = false
					if (colorButton.visible && colorButton.enabled)
						Qt.callLater(function() { colorButton.forceActiveFocus(Qt.PopupFocusReason) })
				}
				background: Rectangle {
					radius: Theme.shellRadius
					color: Theme.surfaceRaised
					border.color: Theme.surfaceBorder
					border.width: 1
				}
				contentItem: ColumnLayout {
					objectName: "dialogColorPickerDialog_" + String(colorRoot.field.id || "")
					Accessible.role: Accessible.Dialog
					Accessible.name: qsTr("Choose %1").arg(colorRoot.field.label || qsTr("color"))
					Accessible.description: qsTr("Select a preset or enter a hexadecimal color value")
					spacing: Theme.space3
					Label {
						Layout.fillWidth: true
						textFormat: Text.PlainText
						text: colorRoot.field.label || qsTr("Choose color")
						Accessible.ignored: true
						color: Theme.textStrong
						font.pixelSize: Theme.fontTitle
						font.weight: Font.DemiBold
					}
					RowLayout {
						Layout.fillWidth: true
						spacing: Theme.space2
						Rectangle {
							Layout.preferredWidth: Theme.controlHeight
							Layout.preferredHeight: Theme.controlHeight
							radius: Theme.innerRadius
							color: colorPicker.isValidHex(colorPicker.draftColor)
								? colorPicker.draftColor : Theme.panel
							border.color: colorPicker.isValidHex(colorPicker.draftColor)
								? Theme.surfaceBorder : Theme.danger
						}
						ModernTextField {
							id: hexInput
							objectName: "dialogColorHex_" + String(colorRoot.field.id || "")
							Layout.fillWidth: true
							placeholderText: "#RRGGBB"
							maximumLength: 7
							invalid: text.length > 0 && !colorPicker.isValidHex(text)
							inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhPreferUppercase
							Accessible.name: qsTr("Hex color")
							onTextEdited: colorPicker.draftColor = text.trim()
							onAccepted: colorPicker.applyDraft()
						}
					}
					Flow {
						Layout.fillWidth: true
						spacing: Theme.space2
						Repeater {
							model: [Theme.accent, Theme.accentHover, Theme.success, Theme.warning,
								Theme.danger, Theme.textStrong, Theme.textMuted]
							delegate: Button {
								id: presetButton
								objectName: "dialogColorPreset_" + index
								width: 32
								height: 32
								padding: 0
								text: ""
								hoverEnabled: true
								activeFocusOnTab: true
								Accessible.role: Accessible.Button
								Accessible.name: qsTr("Use color %1").arg(String(modelData))
								background: Rectangle {
									radius: Theme.innerRadius
									color: modelData
									border.color: presetButton.activeFocus ? Theme.focus
										: presetButton.down ? Theme.accent
										: presetButton.hovered ? Theme.textMuted : Theme.surfaceBorder
									border.width: presetButton.activeFocus ? Theme.focusRingWidth : 1
								}
								onClicked: colorPicker.setDraft(modelData)
							}
						}
					}
					RowLayout {
						Layout.fillWidth: true
						Item { Layout.fillWidth: true }
						ModernButton {
							objectName: "dialogColorCancel_" + String(colorRoot.field.id || "")
							text: qsTr("Cancel")
							onClicked: colorPicker.close()
						}
						ModernButton {
							objectName: "dialogColorApply_" + String(colorRoot.field.id || "")
							text: qsTr("Apply")
							tone: "primary"
							enabled: colorPicker.isValidHex(colorPicker.draftColor)
							onClicked: colorPicker.applyDraft()
						}
					}
				}
			}
			}
        }
    }
    Component {
        id: resultListField
        ColumnLayout {
			id: resultRoot
			objectName: "dialogResultSurface_" + String((field || {}).id || "results")
			property var field: ({})
			readonly property var resultItems: field.items || field.value || []
			readonly property string resultState: String(field.state || field.status || "").toLowerCase()
			readonly property bool resultsLoading: field.loading === true || resultState === "loading"
			readonly property string resultsError: {
				const error = field.errorText || field.errorMessage || field.error
				if (error !== undefined && error !== null && typeof error !== "object")
					return String(error)
				if (error && typeof error === "object")
					return String(error.message || error.userMessage || "")
				return resultState === "error" ? String(field.message || qsTr("Unable to load results.")) : ""
			}
			readonly property int resultRowHeight: Math.max(46, Number(field.rowHeight || 54))
			readonly property int maximumListHeight: Math.max(resultRowHeight,
				Math.min(520, Number(field.maxHeight || field.maximumHeight || 340)))
			function stableIdFor(item, index) {
				const candidate = item && item.stableId !== undefined ? item.stableId
					: item && item.key !== undefined ? item.key
					: item && item.messageId !== undefined ? item.messageId
					: item && item.id !== undefined ? item.id : null
				const prefix = item && item.type ? String(item.type) + ":" : ""
				return candidate !== null && candidate !== undefined && String(candidate).length > 0
					? prefix + String(candidate) : "index:" + index
			}
			function objectNameToken(value) {
				return String(value || "item").replace(/[^A-Za-z0-9_.:-]/g, "_")
			}
			function actionPayload(item) {
				return item.payload || { "id": item.id, "type": item.type }
			}
			function invokePrimary(item) {
				if (!item) return
				if (String(item.primaryActionId || item.primaryAction || "").length === 0) return
				const inferred = item.type === "user" ? "messageSearchResult" : "selectSearchResult"
				const actionId = item.primaryActionId || inferred
				if (String(actionId).length > 0)
					dialogState.invokeAction(actionId, actionPayload(item))
			}
            width: parent ? parent.width : 0
			spacing: Math.max(6, Theme.spacing - 2)
			Label {
				textFormat: Text.PlainText
				text: resultRoot.field.label || ""
				color: Theme.textStrong
				font.bold: true
				visible: text.length > 0
			}
			ListView {
				id: resultList
				objectName: "dialogResultList_" + String(resultRoot.field.id || "results")
				Layout.fillWidth: true
				Layout.preferredHeight: visible ? Math.min(resultRoot.maximumListHeight,
					Math.max(resultRoot.resultRowHeight,
						Math.min(count, 6) * resultRoot.resultRowHeight + Math.max(0, Math.min(count, 6) - 1) * spacing)) : 0
				Layout.maximumHeight: resultRoot.maximumListHeight
				visible: !resultRoot.resultsLoading && resultRoot.resultsError.length === 0 && count > 0
				model: resultRoot.resultItems
				clip: true
				spacing: Math.max(4, Math.round(Theme.spacing / 2))
				cacheBuffer: resultRoot.resultRowHeight * 2
				reuseItems: true
				boundsBehavior: Flickable.StopAtBounds
				currentIndex: count > 0 ? 0 : -1
				activeFocusOnTab: visible
				keyNavigationEnabled: true
				keyNavigationWraps: false
				Accessible.role: Accessible.List
				Accessible.name: resultRoot.field.label || qsTr("Results")
				Accessible.description: qsTr("%n result(s)", "", count)
				function liveDelegateCount() {
					let live = 0
					const objects = contentItem ? contentItem.children : []
					for (let index = 0; index < objects.length; ++index) {
						if (objects[index].isResultListDelegate)
							++live
					}
					return live
				}
				onCountChanged: {
					if (count === 0) currentIndex = -1
					else if (currentIndex < 0 || currentIndex >= count) currentIndex = 0
				}
				Keys.onPressed: event => {
					if (event.key === Qt.Key_Home && count > 0) {
						currentIndex = 0
						positionViewAtBeginning()
						event.accepted = true
					} else if (event.key === Qt.Key_End && count > 0) {
						currentIndex = count - 1
						positionViewAtEnd()
						event.accepted = true
					} else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter
							|| event.key === Qt.Key_Space) && currentIndex >= 0) {
						resultRoot.invokePrimary(resultRoot.resultItems[currentIndex])
						event.accepted = true
					}
				}
				delegate: ItemDelegate {
					id: resultDelegate
					required property var modelData
					required property int index
					property bool isResultListDelegate: true
					readonly property string stableId: resultRoot.stableIdFor(modelData, index)
					objectName: "dialogResultItem_" + resultRoot.objectNameToken(resultRoot.field.id)
							+ "_" + resultRoot.objectNameToken(stableId)
					width: ListView.view.width
					height: resultRoot.resultRowHeight
					hoverEnabled: true
					highlighted: ListView.isCurrentItem || hovered
					readonly property bool current: ListView.isCurrentItem
					readonly property bool keyboardFocused: activeFocus
						|| (ListView.view.activeFocus && current)
					Accessible.role: Accessible.ListItem
					Accessible.name: String(modelData.label || modelData.title || modelData.name || stableId)
					Accessible.description: String(modelData.subtitle || modelData.description || "")
					Accessible.selected: current
					background: Rectangle {
						radius: 6
						color: resultDelegate.current ? Theme.selected
							: resultDelegate.hovered ? Theme.surfaceHover : Theme.strip
						border.color: resultDelegate.keyboardFocused ? Theme.focus
							: resultDelegate.current ? Theme.accent : Theme.divider
						border.width: resultDelegate.keyboardFocused ? Theme.focusRingWidth : 1
					}
					contentItem: RowLayout {
						spacing: Math.max(6, Theme.spacing - 2)
						ColumnLayout {
							Layout.fillWidth: true
							Layout.minimumWidth: 96
							spacing: 2
							Label {
								objectName: "dialogResultTitle_" + resultRoot.objectNameToken(resultRoot.field.id)
									+ "_" + resultRoot.objectNameToken(resultDelegate.stableId)
								Layout.fillWidth: true
								textFormat: Text.PlainText
								text: modelData.label || modelData.title || modelData.name || ""
								color: Theme.textMain
								elide: Text.ElideRight
							}
							Label {
								objectName: "dialogResultSubtitle_" + resultRoot.objectNameToken(resultRoot.field.id)
									+ "_" + resultRoot.objectNameToken(resultDelegate.stableId)
								Layout.fillWidth: true
								textFormat: Text.PlainText
								text: modelData.subtitle || modelData.description || ""
								visible: text.length > 0
								color: Theme.textMuted
								font.pixelSize: 10
								elide: Text.ElideRight
							}
						}
						ModernButton {
							dense: true
							visible: String(modelData.primaryActionId || modelData.primaryAction || "").length > 0
							text: modelData.primaryActionLabel || modelData.primaryAction || qsTr("Open")
							onClicked: resultRoot.invokePrimary(modelData)
						}
						ModernButton {
							dense: true
							visible: String(modelData.secondaryActionId || modelData.secondaryAction || "").length > 0
							text: modelData.secondaryActionLabel || modelData.secondaryAction
							onClicked: {
								const inferred = modelData.type === "channel" ? "joinSearchResult" : "selectSearchResult"
								dialogState.invokeAction(modelData.secondaryActionId || inferred,
									resultRoot.actionPayload(modelData))
							}
						}
					}
					onClicked: resultList.currentIndex = index
					onDoubleClicked: resultRoot.invokePrimary(modelData)
				}
			}
			Rectangle {
				id: resultStatusSurface
				objectName: resultRoot.resultsLoading ? "dialogResultLoading_" + String(resultRoot.field.id || "results")
					: resultRoot.resultsError.length > 0 ? "dialogResultError_" + String(resultRoot.field.id || "results")
					: "dialogResultEmpty_" + String(resultRoot.field.id || "results")
				Layout.fillWidth: true
				Layout.preferredHeight: visible ? Math.max(72, resultStatusLayout.implicitHeight + (Theme.spacing * 2)) : 0
				visible: resultRoot.resultsLoading || resultRoot.resultsError.length > 0 || resultList.count === 0
				color: Theme.strip
				border.color: resultRoot.resultsError.length > 0 ? Theme.danger : Theme.divider
				border.width: 1
				radius: 6
				Accessible.role: Accessible.Pane
				Accessible.name: resultStateLabel.text
				RowLayout {
					id: resultStatusLayout
					anchors.fill: parent
					anchors.margins: Theme.spacing
					spacing: Theme.spacing
					ModernBusyIndicator {
						objectName: "dialogResultBusy_" + String(resultRoot.field.id || "results")
						visible: resultRoot.resultsLoading
						running: visible
						Layout.preferredWidth: 28
						Layout.preferredHeight: 28
						Accessible.name: String(resultRoot.field.loadingText || qsTr("Loading results"))
					}
					Label {
						id: resultStateLabel
						objectName: "dialogResultStatusText_" + String(resultRoot.field.id || "results")
						Layout.fillWidth: true
						textFormat: Text.PlainText
						text: resultRoot.resultsLoading ? String(resultRoot.field.loadingText || qsTr("Loading results…"))
							: resultRoot.resultsError.length > 0 ? resultRoot.resultsError
							: String(resultRoot.field.emptyText || qsTr("No results."))
						color: resultRoot.resultsError.length > 0 ? Theme.danger : Theme.textMuted
						wrapMode: Text.Wrap
					}
					ModernButton {
						objectName: "dialogResultRetry_" + String(resultRoot.field.id || "results")
						visible: resultRoot.resultsError.length > 0
							&& String(resultRoot.field.retryActionId || resultRoot.field.errorActionId || "").length > 0
						text: resultRoot.field.retryLabel || resultRoot.field.errorActionLabel || qsTr("Retry")
						tone: "accent"
						onClicked: dialogState.invokeAction(resultRoot.field.retryActionId || resultRoot.field.errorActionId,
							{ "fieldId": resultRoot.field.id })
					}
				}
			}
        }
    }
	Component {
		id: themeGridField
		ColumnLayout {
			id: themeGridRoot
			property var field: ({})
			readonly property var selectedOption: dialog.optionForFieldValue(field.id, field.value)
			readonly property var selectedPreview: selectedOption.preview || selectedOption.swatch || ({})
			readonly property string accentId: String(dialogState.fieldValue("look.modernAccent") || "auto")
			readonly property var accentOption: dialog.optionForFieldValue("look.modernAccent", accentId)
			readonly property color selectedAccent: (accentOption.swatch || {}).accent
				|| selectedPreview.accent || Theme.accent
			width: parent ? parent.width : 0
			spacing: Theme.space3

			RowLayout {
				Layout.fillWidth: true
				Label {
					textFormat: Text.PlainText
					text: themeGridRoot.field.label || qsTr("Theme")
					color: Theme.textStrong
					font.bold: true
				}
				Item { Layout.fillWidth: true }
				Label {
					textFormat: Text.PlainText
					text: qsTr("%n theme(s)", "", (themeGridRoot.field.options || []).length)
					color: Theme.textMuted
					font.pixelSize: Theme.fontCaption
				}
			}

			Rectangle {
				id: appearanceOverview
				objectName: "appearanceThemeOverview"
				Layout.fillWidth: true
				Layout.preferredHeight: 132
				radius: Theme.innerRadius
				color: themeGridRoot.selectedPreview.shell || themeGridRoot.selectedPreview.bg
					|| Theme.shellBackground
				border.color: themeGridRoot.selectedAccent
				border.width: 1
				clip: true
				Accessible.role: Accessible.Pane
				Accessible.name: qsTr("Current appearance preview: %1").arg(themeGridRoot.selectedOption.label || "")

				Rectangle {
					anchors.left: parent.left
					anchors.top: parent.top
					anchors.bottom: parent.bottom
					width: 54
					color: themeGridRoot.selectedPreview.rail || Theme.rail
					Column {
						anchors.centerIn: parent
						spacing: 10
						Repeater {
							model: 4
							Rectangle {
								width: 22; height: 22; radius: 7
								color: index === 0 ? themeGridRoot.selectedAccent
									: themeGridRoot.selectedPreview.surface || Theme.surfaceRaised
								opacity: index === 0 ? 1 : 0.78
							}
						}
					}
				}
				Rectangle {
					anchors.left: parent.left
					anchors.leftMargin: 54
					anchors.right: parent.right
					anchors.top: parent.top
					height: 28
					color: themeGridRoot.selectedPreview.strip || Theme.strip
				}
				Rectangle {
					anchors.left: parent.left
					anchors.leftMargin: 66
					anchors.right: parent.right
					anchors.rightMargin: 12
					anchors.top: parent.top
					anchors.topMargin: 40
					anchors.bottom: parent.bottom
					anchors.bottomMargin: 12
					radius: 8
					color: themeGridRoot.selectedPreview.panel || Theme.panel
					border.color: themeGridRoot.selectedPreview.border || Theme.surfaceBorder
					RowLayout {
						anchors.fill: parent
						anchors.margins: 12
						spacing: 12
						ColumnLayout {
							Layout.fillWidth: true
							spacing: 7
							Label {
								Layout.fillWidth: true
								textFormat: Text.PlainText
								text: themeGridRoot.selectedOption.label || qsTr("Selected theme")
								color: themeGridRoot.selectedPreview.text || Theme.textStrong
								font.bold: true
							}
							Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 6; radius: 3; color: themeGridRoot.selectedAccent }
							Rectangle { Layout.preferredWidth: parent.width * 0.68; Layout.preferredHeight: 5; radius: 3; color: themeGridRoot.selectedPreview.textMuted || Theme.textMuted }
						}
						Row {
							spacing: 6
							Repeater {
								model: [themeGridRoot.selectedPreview.success || Theme.success,
									themeGridRoot.selectedPreview.warning || Theme.warning,
									themeGridRoot.selectedPreview.danger || Theme.danger]
								Rectangle { required property color modelData; width: 10; height: 10; radius: 5; color: modelData }
							}
						}
					}
				}
			}

			Flow {
				id: themeFlow
				Layout.fillWidth: true
				Layout.preferredHeight: childrenRect.height
				spacing: Theme.space2
				readonly property int columnCount: width >= 680 ? 4 : width >= 500 ? 3 : width >= 320 ? 2 : 1
				readonly property real cardWidth: Math.floor((width - ((columnCount - 1) * spacing)) / columnCount)
				Repeater {
					model: themeGridRoot.field.options || []
					delegate: ItemDelegate {
						id: themeCard
						required property var modelData
						required property int index
						hoverEnabled: true
						readonly property var palette: modelData.preview || modelData.swatch || ({})
						readonly property bool selected: String(modelData.value) === String(themeGridRoot.field.value)
						readonly property color cardAccent: themeGridRoot.accentId === "auto"
							? (palette.accent || Theme.accent) : themeGridRoot.selectedAccent
						objectName: "dialogThemeOption_" + String(modelData.value || index)
						width: themeFlow.cardWidth
						height: 112
						enabled: modelData.enabled === undefined || modelData.enabled
						opacity: enabled ? 1 : 0.5
						padding: 0
						Accessible.role: Accessible.RadioButton
						Accessible.name: modelData.label || String(modelData.value)
						Accessible.description: modelData.source === "custom" ? qsTr("Custom theme") : qsTr("Built-in theme")
						Accessible.checked: selected
						onClicked: dialog.updateFieldValue(themeGridRoot.field.id, modelData.value)
						background: Rectangle {
							radius: 9
							color: themeCard.palette.shell || themeCard.palette.bg || Theme.shellBackground
							border.color: themeCard.activeFocus ? Theme.focus
								: themeCard.selected ? themeCard.cardAccent
								: themeCard.hovered ? (themeCard.palette.textMuted || Theme.textMuted)
								: (themeCard.palette.border || Theme.surfaceBorder)
							border.width: themeCard.activeFocus || themeCard.selected ? 2 : 1
						}
						contentItem: Item {
							Rectangle {
								x: 8; y: 8; width: 24; height: 66; radius: 6
								color: themeCard.palette.rail || Theme.rail
								Rectangle { anchors.centerIn: parent; width: 12; height: 12; radius: 4; color: themeCard.cardAccent }
							}
							Rectangle {
								x: 38; y: 8; width: parent.width - 46; height: 66; radius: 6
								color: themeCard.palette.panel || Theme.panel
								border.color: themeCard.palette.border || Theme.surfaceBorder
								Column {
									anchors.fill: parent; anchors.margins: 8; spacing: 6
									Rectangle { width: parent.width * 0.72; height: 5; radius: 3; color: themeCard.palette.text || Theme.textStrong }
									Rectangle { width: parent.width; height: 13; radius: 4; color: themeCard.cardAccent; opacity: 0.72 }
									Rectangle { width: parent.width * 0.58; height: 5; radius: 3; color: themeCard.palette.textMuted || Theme.textMuted }
								}
							}
							RowLayout {
								x: 9; y: 81; width: parent.width - 18
								Label {
									Layout.fillWidth: true
									textFormat: Text.PlainText
									text: themeCard.modelData.label || String(themeCard.modelData.value)
									color: themeCard.palette.text || Theme.textStrong
									font.pixelSize: Theme.fontCaption
									font.bold: themeCard.selected
									elide: Text.ElideRight
								}
								Label {
									visible: themeCard.modelData.source === "custom"
									textFormat: Text.PlainText
									text: qsTr("Custom")
									color: themeCard.cardAccent
									font.pixelSize: 9
								}
							}
						}
					}
				}
			}
			Label {
				Layout.fillWidth: true
				visible: String(themeGridRoot.field.hint || "").length > 0
				textFormat: Text.PlainText
				text: themeGridRoot.field.hint || ""
				color: Theme.textMuted
				font.pixelSize: Theme.fontCaption
				wrapMode: Text.Wrap
			}
		}
	}
	Component {
		id: accentGridField
		ColumnLayout {
			id: accentGridRoot
			property var field: ({})
			width: parent ? parent.width : 0
			spacing: Theme.space2
			Label {
				Layout.fillWidth: true
				textFormat: Text.PlainText
				text: accentGridRoot.field.label || qsTr("Accent")
				color: Theme.textStrong
				font.bold: true
			}
			Flow {
				id: accentFlow
				Layout.fillWidth: true
				Layout.preferredHeight: childrenRect.height
				spacing: Theme.space2
				Repeater {
					model: accentGridRoot.field.options || []
					delegate: ItemDelegate {
						id: accentCard
						required property var modelData
						required property int index
						hoverEnabled: true
						readonly property bool selected: String(modelData.value) === String(accentGridRoot.field.value)
						readonly property color swatchColor: (modelData.swatch || {}).accent || Theme.accent
						objectName: "dialogAccentOption_" + String(modelData.value || index)
						width: Math.max(98, Math.min(126, Math.floor((accentFlow.width - (Theme.space2 * 3)) / 4)))
						height: 58
						padding: 0
						enabled: modelData.enabled === undefined || modelData.enabled
						opacity: enabled ? 1 : 0.5
						Accessible.role: Accessible.RadioButton
						Accessible.name: modelData.label || String(modelData.value)
						Accessible.description: modelData.hint || ""
						Accessible.checked: selected
						onClicked: dialog.updateFieldValue(accentGridRoot.field.id, modelData.value)
						background: Rectangle {
							radius: 9
							color: accentCard.selected ? Theme.selected
								: accentCard.hovered ? Theme.surfaceHover : Theme.surfaceRaised
							border.color: accentCard.activeFocus ? Theme.focus
								: accentCard.selected ? accentCard.swatchColor : Theme.surfaceBorder
							border.width: accentCard.activeFocus || accentCard.selected ? 2 : 1
						}
						contentItem: RowLayout {
							anchors.fill: parent
							anchors.margins: 9
							spacing: 8
							Rectangle {
								Layout.preferredWidth: 20; Layout.preferredHeight: 20
								radius: 10
								color: accentCard.swatchColor
								border.color: Theme.textStrong
								border.width: accentCard.modelData.automatic ? 1 : 0
							}
							Label {
								Layout.fillWidth: true
								textFormat: Text.PlainText
								text: accentCard.modelData.label || String(accentCard.modelData.value)
								color: Theme.textStrong
								font.pixelSize: Theme.fontCaption
								font.bold: accentCard.selected
								elide: Text.ElideRight
							}
						}
					}
				}
			}
		}
	}
    Component {
        id: textareaField
        ColumnLayout {
            id: textareaRoot
            property var field
            width: parent ? parent.width : 0
			Label { textFormat: Text.PlainText; text: textareaRoot.field.label || ""; color: Theme.textMuted; font.pixelSize: 11 }
            ModernTextArea {
				objectName: "dialogField_" + String((textareaRoot.field || {}).id || "")
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(90, (textareaRoot.field.rows || 4) * 22)
                text: String(textareaRoot.field.value ?? "")
                enabled: textareaRoot.field.enabled === undefined || textareaRoot.field.enabled
                wrapMode: TextEdit.Wrap
				onActiveFocusChanged: {
					if (!activeFocus && text !== String(textareaRoot.field.value ?? ""))
						dialog.updateFieldValue(textareaRoot.field.id, text)
				}
            }
        }
    }
    Component {
        id: aclEditorField
        AclEditor { }
    }
    Component {
        id: shortcutEditorField
        ShortcutEditor { }
    }
    Component {
        id: messageEventEditorField
        MessageEventEditor { }
    }
    Component {
        id: pluginEditorField
        PluginEditor { }
    }
    Component {
        id: actionField
        ModernButton {
            property var field
			objectName: "dialogField_" + String((field || {}).id || "")
            width: parent ? parent.width : implicitWidth
            text: field.buttonLabel || field.label || field.text || field.id
            enabled: field.enabled === undefined || field.enabled
            onClicked: dialogState.invokeAction(field.actionId || field.id, field.payload || {})
        }
    }
}
