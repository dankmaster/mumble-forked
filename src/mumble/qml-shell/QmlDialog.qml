import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

Dialog {
    id: dialog
	property var recorderController: typeof recorder !== "undefined" ? recorder : null
	// Settings is hosted by a real top-level QML Window. The generic product
	// dialog remains a modal Popup; these switches let both hosts reuse the same
	// typed editor and focus/navigation implementation without duplicating it.
	property bool excludeSettings: false
	property bool settingsOnly: false
	property bool nativeWindowHosted: false
	property Item popupParent: null
	readonly property bool ownsCurrentState: dialogState.kind !== "imageViewer"
		&& (!excludeSettings || dialogState.kind !== "settings")
		&& (!settingsOnly || dialogState.kind === "settings")
	readonly property int densityInset: Theme.spacing + 6
	readonly property int sectionPadding: Theme.spacing + 2
	readonly property bool compactDialogLayout: width < 760
	readonly property bool connectEditorBackAction: dialogState.kind === "connect"
		&& dialogState.editorOpen
	onCompactDialogLayoutChanged: handleCompactLayoutChanged()
	readonly property int footerActionSpacing: Math.max(6, Math.round(Theme.spacing / 2))
	readonly property var presentedFooterActions: {
		const source = dialogState.actions || []
		const primaryId = String(dialogState.primaryActionId || "")
		if (primaryId.length === 0)
			return source
		const secondary = []
		const primary = []
		for (let index = 0; index < source.length; ++index) {
			const action = source[index]
			if (String((action || {}).id || "") === primaryId)
				primary.push(action)
			else
				secondary.push(action)
		}
		return secondary.concat(primary)
	}
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
	readonly property int settingsPageHeadingHeight: dialogState.kind === "settings"
		&& dialogState.pages.length > 0 ? 62 : 0
	readonly property int requestedHeight: Math.max(240,
		Math.min(dialogState.preferredHeight || 700, 860))
	readonly property int maximumHeightForParent: parent
		? Math.max(220, parent.height - (Theme.space5 * 2)) : 760
	// Multi-page surfaces keep a stable viewport as the user changes pages. A
	// single-surface dialog instead follows its actual content: sparse states no
	// longer inherit an oversized DTO canvas, while long forms can grow before
	// they need to scroll.
	readonly property bool compactContentFitPage: {
		dialogState.revision
		if (dialogState.kind !== "settings" || !compactDialogLayout)
			return false
		return !!activeSettingsPage().contentFitCompact
	}
	readonly property bool stablePageViewport: dialogState.pages.length > 0
		&& !compactContentFitPage
	readonly property int minimumContentSizedHeight: headerHeight + footerHeight
		+ (Theme.rowHeight * 2)
	property int measuredContentHeight: 0
	readonly property int naturalContentSizedHeight: headerHeight + statusHeight
		+ compactPageBarHeight + settingsPageHeadingHeight + measuredContentHeight + footerHeight
	readonly property int responsiveHeight: stablePageViewport ? requestedHeight
		: Math.max(minimumContentSizedHeight, naturalContentSizedHeight)
	readonly property string dialogTone: String(dialogState.tone || "").toLowerCase()
	readonly property color dialogToneColor: toneColor(dialogTone)
	property bool showAdvanced: !!dialogState.state.showAdvanced
	property string focusedDialogId: ""
	property string focusedSurfaceSignature: ""
	property string focusedValidationSignature: ""
	property string pendingFocusRestoreId: ""
	property string pendingFocusRestoreDialogId: ""
	property int pendingFocusRestoreRevision: -1
	property string handledFocusRequestId: ""
	property int focusRequestGeneration: 0
	property int activeFocusRevealGeneration: 0
	property var dialogPresentationMemory: ({})
	property var settingsSectionExpansionMemory: ({})
	property bool nestedModalOpen: false
	property Item nestedModalFocusReturnItem: null
	property string nestedModalFocusReturnName: ""
	property var beforeOpen: null
	// Screenshot automation keeps the real keyboard-focus contract, but a
	// blinking insertion cursor would make two otherwise identical frames hash
	// differently. The product host enables this only for deterministic visual
	// fixtures; interactive dialogs retain the normal cursor.
	property bool visualFixtureMode: false
	Component {
		id: dialogTextCursorDelegate
		// Qt owns the outer delegate's visibility for focus and blink cadence.
		// Keeping the fixture switch on an inner paint item prevents an
		// accessibility focus request from overriding the deterministic gate.
		Item {
			width: 1
			Rectangle {
				objectName: "dialogTextCursorPaint"
				anchors.fill: parent
				visible: !dialog.visualFixtureMode
				color: Theme.textStrong
			}
		}
	}
	Connections {
		target: dialog.contentItem && dialog.contentItem.Window.window
			? dialog.contentItem.Window.window : null
		ignoreUnknownSignals: true
		function onActiveFocusItemChanged() {
			dialog.scheduleActiveFocusReveal()
		}
	}
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
	function activeDialogFocusItem() {
		const window = contentItem && contentItem.Window.window ? contentItem.Window.window : null
		return window ? window.activeFocusItem : null
	}
	function stableFocusObjectName(target) {
		let current = target
		while (current && isVisualDescendantOf(current, contentItem)) {
			const name = String(current.objectName || "")
			if (name.length > 0)
				return name
			if (current === contentItem)
				break
			current = current.parent
		}
		return ""
	}
	function hasActiveDialogFocus() {
		return isVisualDescendantOf(activeDialogFocusItem(), contentItem)
	}
	function rememberNestedModalFocus(preferredItem) {
		const candidate = preferredItem && preferredItem.forceActiveFocus
			? preferredItem : activeDialogFocusItem()
		nestedModalFocusReturnItem = isVisualDescendantOf(candidate, contentItem)
			? candidate : null
		nestedModalFocusReturnName = stableFocusObjectName(candidate)
	}
	function restoreNestedModalFocus(preferredItem) {
		const candidate = preferredItem && preferredItem.forceActiveFocus
			? preferredItem : nestedModalFocusReturnItem
		const objectName = nestedModalFocusReturnName
		nestedModalFocusReturnItem = null
		nestedModalFocusReturnName = ""
		Qt.callLater(function() {
			if (!dialog.visible || dialog.nestedModalOpen)
				return
			if (candidate && candidate.forceActiveFocus && candidate.visible && candidate.enabled
					&& dialog.isVisualDescendantOf(candidate, dialog.contentItem)) {
				candidate.forceActiveFocus(Qt.PopupFocusReason)
				return
			}
			if (objectName.length > 0) {
				dialog.scheduleFocusByObjectName(objectName, true)
				return
			}
			dialog.applyInitialFocus()
		})
	}
	function scheduleFocusWork(callback) {
		const generation = ++focusRequestGeneration
		Qt.callLater(function() {
			if (dialog.visible && generation === dialog.focusRequestGeneration)
				callback(generation)
		})
		return generation
	}
	function scheduleInitialFocus() {
		scheduleFocusWork(function() { dialog.applyInitialFocus() })
	}
	function scheduleFocusByObjectName(objectName, allowRetry) {
		const requested = String(objectName || "")
		if (requested.length === 0) return
		scheduleFocusWork(function(generation) {
			dialog.applyFocusByObjectName(requested, allowRetry, generation)
		})
	}
	function armFocusRestore(objectName) {
		pendingFocusRestoreId = String(objectName || "")
		pendingFocusRestoreDialogId = String(dialogState.state.id || "")
		pendingFocusRestoreRevision = Number(dialogState.revision || 0)
		pendingFocusRestoreExpiry.restart()
	}
	function clearPendingFocusRestore() {
		pendingFocusRestoreExpiry.stop()
		pendingFocusRestoreId = ""
		pendingFocusRestoreDialogId = ""
		pendingFocusRestoreRevision = -1
	}
	function consumePendingFocusRestore(forceRestore) {
		const requested = pendingFocusRestoreId
		if (requested.length === 0)
			return false
		const currentDialogId = String(dialogState.state.id || "")
		if (currentDialogId !== pendingFocusRestoreDialogId) {
			clearPendingFocusRestore()
			return false
		}
		const activeName = stableFocusObjectName(activeDialogFocusItem())
		// A user who has already moved to another named control wins over a late
		// DTO refresh. Null focus is expected while a delegate is being rebuilt.
		if (!forceRestore && activeName.length > 0 && activeName !== requested) {
			clearPendingFocusRestore()
			return false
		}
		const stateRepublished = Number(dialogState.revision || 0) !== pendingFocusRestoreRevision
		scheduleFocusByObjectName(requested, true)
		if (stateRepublished)
			clearPendingFocusRestore()
		return true
	}
	function invokeNativePickerAction(actionId, payload, focusObjectName) {
		armFocusRestore(focusObjectName)
		// Native file dialogs are synchronous. This line resumes after both an
		// accepted choice and Cancel, so the initiating QML control can recover
		// focus even when the backend has no new DTO to publish.
		dialogState.invokeAction(actionId, payload)
		Qt.callLater(function() {
			if (dialog.pendingFocusRestoreId.length > 0)
				dialog.consumePendingFocusRestore(false)
		})
	}
	Timer {
		id: pendingFocusRestoreExpiry
		interval: 8000
		repeat: false
		onTriggered: dialog.clearPendingFocusRestore()
	}
	function explicitMetadataFocusId() {
		const state = dialogState.state || {}
		let requested = state.initialFocusId || state.defaultFocusId
			|| state.initialFocus || state.defaultFocus || ""
		// Settings is a navigation surface first. When its controller does not
		// explicitly nominate a field, put keyboard users on the active page
		// navigator instead of the sticky Done action.
		if (!requested && dialogState.kind === "settings" && dialogState.pages.length > 0)
			requested = compactDialogLayout ? "dialogCompactPageSelector" : "dialogPageList"
		if (!requested)
			requested = dialogState.initialFocusId || ""
		if (requested && typeof requested === "object")
			requested = requested.id || requested.fieldId || requested.actionId || ""
		return String(requested || "").trim()
	}
	function activeSettingsPageIndex() {
		const activePageId = String(dialogState.activePage || "")
		for (let index = 0; index < dialogState.pages.length; ++index) {
			if (String((dialogState.pages[index] || {}).id || "") === activePageId)
				return index
		}
		return dialogState.pages.length > 0 ? 0 : -1
	}
	function alignSettingsNavigationFocus(target) {
		if (dialogState.kind !== "settings" || dialogState.pages.length === 0)
			return
		const activeIndex = activeSettingsPageIndex()
		if (activeIndex < 0)
			return
		if (target === dialogPageList) {
			// ListView may initialize its current item to row zero when it first
			// receives focus. Set the current row immediately before the focus
			// handoff so the keyboard cursor and the selected settings page cannot
			// describe different pages to accessibility clients.
			dialogPageList.currentIndex = activeIndex
			dialogPageList.positionViewAtIndex(activeIndex, ListView.Contain)
		} else if (target === compactPageSelector) {
			compactPageSelector.currentIndex = activeIndex
		}
	}
	function invalidFieldIds() {
		dialogState.revision
		const invalid = []
		const sections = dialogState.sections || []
		for (let sectionIndex = 0; sectionIndex < sections.length; ++sectionIndex) {
			const fields = (sections[sectionIndex] || {}).fields || []
			for (let fieldIndex = 0; fieldIndex < fields.length; ++fieldIndex) {
				const fieldId = String((fields[fieldIndex] || {}).id || "")
				if (fieldId.length > 0 && dialogState.fieldError(fieldId).length > 0)
					invalid.push(fieldId)
			}
		}
		return invalid
	}
	function validationSignature() {
		const invalid = invalidFieldIds()
		// Message copy may become more specific while a user is typing. Focus is
		// tied to the invalid field set, not to every wording update.
		return invalid.join("|")
	}
	function invalidFieldIsAdvanced(fieldId) {
		const requested = String(fieldId || "")
		const sections = dialogState.sections || []
		for (let sectionIndex = 0; sectionIndex < sections.length; ++sectionIndex) {
			const section = sections[sectionIndex] || {}
			const fields = section.fields || []
			for (let fieldIndex = 0; fieldIndex < fields.length; ++fieldIndex) {
				const field = fields[fieldIndex] || {}
				if (String(field.id || "") === requested)
					return !!section.advanced || !!field.advanced
			}
		}
		return false
	}
	function settingsContainsField(fieldId) {
		const requested = String(fieldId || "")
		if (requested.length === 0)
			return false
		const sections = dialogState.sections || []
		for (let sectionIndex = 0; sectionIndex < sections.length; ++sectionIndex) {
			const fields = (sections[sectionIndex] || {}).fields || []
			for (let fieldIndex = 0; fieldIndex < fields.length; ++fieldIndex) {
				if (String((fields[fieldIndex] || {}).id || "") === requested)
					return true
			}
		}
		return false
	}
	function settingsSectionExpansionKey(section, sectionIndex) {
		const dialogId = String(dialogState.state.id || "dialog")
		const pageId = String(dialogState.activePage || "single")
		const sectionId = String((section || {}).id || sectionIndex)
		return dialogId + "|" + pageId + "|" + sectionId
	}
	function settingsSectionExpanded(section, sectionIndex) {
		const sectionData = section || {}
		if (!sectionData.collapsible)
			return true
		const key = settingsSectionExpansionKey(sectionData, sectionIndex)
		const remembered = settingsSectionExpansionMemory[key]
		if (remembered !== undefined)
			return Boolean(remembered)
		return sectionData.expandedByDefault === undefined
			? true : Boolean(sectionData.expandedByDefault)
	}
	function setSettingsSectionExpanded(section, sectionIndex, expanded) {
		const key = settingsSectionExpansionKey(section, sectionIndex)
		const memory = Object.assign({}, settingsSectionExpansionMemory)
		memory[key] = Boolean(expanded)
		settingsSectionExpansionMemory = memory
		scheduleContentMeasurement()
		Qt.callLater(dialog.scheduleContentMeasurement)
	}
	function expandSettingsSectionForField(fieldId) {
		const requested = String(fieldId || "")
		const sections = dialogState.sections || []
		for (let sectionIndex = 0; sectionIndex < sections.length; ++sectionIndex) {
			const section = sections[sectionIndex] || {}
			const fields = section.fields || []
			for (let fieldIndex = 0; fieldIndex < fields.length; ++fieldIndex) {
				if (String((fields[fieldIndex] || {}).id || "") !== requested)
					continue
				if (section.collapsible && !settingsSectionExpanded(section, sectionIndex)) {
					setSettingsSectionExpanded(section, sectionIndex, true)
					return true
				}
				return false
			}
		}
		return false
	}
	function focusFirstInvalidField(allowRetry, generation) {
		if (generation !== undefined && generation !== focusRequestGeneration) return
		if (!visible || !contentItem) return
		const invalid = invalidFieldIds()
		if (invalid.length === 0) return
		let missingDelegate = false
		let hiddenAdvancedError = false
		let revealedCollapsedSection = false
		for (let index = 0; index < invalid.length; ++index) {
			const fieldId = invalid[index]
			const target = focusObjectInTree(contentItem, "dialogField_" + fieldId)
			if (!target) {
				missingDelegate = true
				hiddenAdvancedError = hiddenAdvancedError || invalidFieldIsAdvanced(fieldId)
				revealedCollapsedSection = expandSettingsSectionForField(fieldId)
					|| revealedCollapsedSection
				continue
			}
			if (target.enabled !== false && target.visible !== false && target.forceActiveFocus) {
				target.forceActiveFocus(Qt.TabFocusReason)
				ensureContentVisible(target)
				return
			}
			hiddenAdvancedError = hiddenAdvancedError || invalidFieldIsAdvanced(fieldId)
		}
		if (!showAdvanced && hiddenAdvancedError) {
			showAdvanced = true
			Qt.callLater(function() {
				dialog.focusFirstInvalidField(false, generation)
			})
		} else if ((allowRetry && missingDelegate) || revealedCollapsedSection) {
			Qt.callLater(function() {
				dialog.focusFirstInvalidField(false, generation)
			})
		}
	}
	function scheduleValidationFocus() {
		scheduleFocusWork(function(generation) {
			dialog.focusFirstInvalidField(true, generation)
		})
	}
	function validationShouldTakeFocus(dialogChanged, presentationToRestore,
		previousSignature, nextSignature) {
		if (presentationToRestore)
			return false
		if (dialogChanged)
			return true
		const activeName = stableFocusObjectName(activeDialogFocusItem())
		const fieldPrefix = "dialogField_"
		if (activeName.indexOf(fieldPrefix) === 0) {
			const activeFieldId = activeName.slice(fieldPrefix.length)
			const previousInvalid = String(previousSignature || "").split("|")
			const nextInvalid = String(nextSignature || "").split("|")
			// Continue an explicit validation walk when the focused invalid field
			// has just become valid. An unrelated live editor still keeps its caret.
			if (previousInvalid.indexOf(activeFieldId) >= 0
					&& nextInvalid.indexOf(activeFieldId) < 0)
				return true
		}
		return activeName.length === 0 || activeName === "dialogCloseButton"
			|| activeName.indexOf("dialogAction_") === 0
	}
	function currentSurfaceSignature() {
		const dialogId = String(dialogState.state.id || "")
		if (dialogState.kind === "connect")
			return dialogId + "|" + (dialogState.editorOpen ? "editor" : "source:"
				+ String(dialogState.state.activeSource || "favorites"))
		if (dialogState.kind === "settings")
			return dialogId + "|settings:" + String(dialogState.activePage || "")
				+ ":" + (compactDialogLayout ? "compact" : "regular")
		return dialogId + "|default"
	}
	function handleCompactLayoutChanged() {
		if (!visible || dialogState.kind !== "settings" || dialogState.pages.length === 0)
			return
		focusedSurfaceSignature = currentSurfaceSignature()
		const activeName = stableFocusObjectName(activeDialogFocusItem())
		if (activeName.length === 0 || activeName === "dialogPageList"
				|| activeName === "dialogCompactPageSelector") {
			scheduleInitialFocus()
		}
	}
	function metadataFocusId() {
		const requested = explicitMetadataFocusId()
		if (requested.length > 0) return normalizedFocusObjectName(requested)
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
	function initialFocusTarget() {
		if (!contentItem) return null
		// Some composite fields expose a stable, nested product objectName (for
		// example pluginInstallButton) rather than a dialogField_* wrapper. Honor
		// that exact DTO target before applying the legacy field-name shorthand.
		const explicitRequested = explicitMetadataFocusId()
		const requested = metadataFocusId()
		let target = focusObjectInTree(contentItem, explicitRequested)
		if (!target)
			target = focusObjectInTree(contentItem, requested)
		if (!target && requested.indexOf("dialogField_") !== 0)
			target = focusObjectInTree(contentItem, "dialogField_" + requested)
		if (!target)
			target = focusObjectInTree(contentItem, "dialogAction_" + String(dialogState.primaryActionId || ""))
		if (!target)
			target = focusObjectInTree(contentItem, "dialogCloseButton")
		return target
	}
	function ensureActiveInitialFocusVisible() {
		if (!visible) return
		const target = initialFocusTarget()
		if (target && target.activeFocus)
			ensureContentVisible(target)
	}
	function scheduleInitialFocusReveal() {
		Qt.callLater(dialog.ensureActiveInitialFocusVisible)
	}
	function applyInitialFocus(retriesRemaining) {
		if (!visible || !contentItem) return ""
		const explicitRequested = explicitMetadataFocusId()
		if (dialogState.kind === "settings" && settingsContainsField(explicitRequested)) {
			if (!showAdvanced && invalidFieldIsAdvanced(explicitRequested))
				showAdvanced = true
			const expandedSection = expandSettingsSectionForField(explicitRequested)
			const explicitTarget = focusObjectInTree(
				contentItem, normalizedFocusObjectName(explicitRequested))
			if (!explicitTarget) {
				const retries = Number.isFinite(Number(retriesRemaining))
					? Math.max(0, Number(retriesRemaining)) : 3
				if (expandedSection || retries > 0) {
					Qt.callLater(function() {
						dialog.applyInitialFocus(Math.max(0, retries - 1))
					})
				}
				return ""
			}
		}
		const target = initialFocusTarget()
		if (target && target.enabled !== false && target.visible !== false && target.forceActiveFocus) {
			alignSettingsNavigationFocus(target)
			target.forceActiveFocus(Qt.TabFocusReason)
			// A long ScrollView may publish its final contentHeight one layout turn
			// after the field delegate itself becomes focusable. Reveal on both
			// queued turns so initial keyboard focus never remains behind the sticky
			// footer merely because the first content-height calculation was stale.
			Qt.callLater(function() {
				if (!target || !target.activeFocus) return
				dialog.ensureContentVisible(target)
				Qt.callLater(function() {
					if (target && target.activeFocus)
						dialog.ensureContentVisible(target)
				})
			})
			// Controls such as SpinBox delegate active focus to an unnamed internal
			// editor. Return the stable product control name instead of making visual
			// automation infer it from Qt Quick Controls implementation details.
			return String(target.objectName || "")
		}
		return ""
	}
	function applyFocusByObjectName(objectName, allowRetry, generation) {
		if (generation !== undefined && generation !== focusRequestGeneration) return
		if (!visible || !contentItem || String(objectName || "").length === 0) return
		const target = focusObjectInTree(contentItem, String(objectName))
		if (target && target.enabled !== false && target.visible !== false && target.forceActiveFocus) {
			target.forceActiveFocus(Qt.TabFocusReason)
			ensureContentVisible(target)
		} else if (allowRetry) {
			Qt.callLater(function() { dialog.applyFocusByObjectName(objectName, false, generation) })
		}
	}
	function focusResultListByFieldId(fieldId, activateCurrent, allowRetry) {
		if (!visible || !contentItem || String(fieldId || "").length === 0) return
		const target = focusObjectInTree(contentItem, "dialogResultList_" + String(fieldId))
		if (target && target.visible && target.count > 0 && target.forceActiveFocus) {
			if (target.currentIndex < 0)
				target.currentIndex = 0
			target.forceActiveFocus(Qt.TabFocusReason)
			target.positionViewAtIndex(target.currentIndex, ListView.Contain)
			if (activateCurrent && target.activateCurrent)
				target.activateCurrent()
		} else if (allowRetry) {
			Qt.callLater(function() { dialog.focusResultListByFieldId(fieldId, activateCurrent, false) })
		}
	}
	function isVisualDescendantOf(target, ancestor) {
		let current = target
		while (current) {
			if (current === ancestor) return true
			current = current.parent
		}
		return false
	}
	function isRootDialogId(dialogId) {
		return dialogId === "settings" || dialogId === "connect" || dialogId === "failedConnection"
	}
	function rememberDialogPresentation(dialogId) {
		const normalizedId = String(dialogId || "")
		if (normalizedId.length === 0 || !dialogContentScroll || !dialogContentScroll.contentItem)
			return
		const focusObjectName = stableFocusObjectName(activeDialogFocusItem())
		const memory = Object.assign({}, dialogPresentationMemory)
		memory[normalizedId] = {
			"focusObjectName": focusObjectName,
			"contentY": Number(dialogContentScroll.contentItem.contentY || 0)
		}
		dialogPresentationMemory = memory
	}
	function dialogPresentation(dialogId) {
		const normalizedId = String(dialogId || "")
		return dialogPresentationMemory[normalizedId] || null
	}
	function forgetDialogPresentation(dialogId) {
		const normalizedId = String(dialogId || "")
		if (!dialogPresentationMemory[normalizedId]) return
		const memory = Object.assign({}, dialogPresentationMemory)
		delete memory[normalizedId]
		dialogPresentationMemory = memory
	}
	function restoreDialogPresentation(dialogId, entry, retriesRemaining) {
		if (!entry || !visible || !contentItem) return
		const generation = ++focusRequestGeneration
		restoreDialogPresentationAttempt(dialogId, entry, retriesRemaining, generation)
	}
	function restoreDialogPresentationAttempt(dialogId, entry, retriesRemaining, generation) {
		Qt.callLater(function() {
			if (!dialog.visible || generation !== dialog.focusRequestGeneration)
				return
			const focusObjectName = String(entry.focusObjectName || "")
			const target = focusObjectName.length > 0
				? dialog.focusObjectInTree(dialog.contentItem, focusObjectName) : null
			if (target && target.enabled !== false && target.visible !== false && target.forceActiveFocus) {
				target.forceActiveFocus(Qt.TabFocusReason)
				dialog.ensureContentVisible(target)
			} else if (focusObjectName.length > 0 && retriesRemaining > 0) {
				dialog.restoreDialogPresentationAttempt(dialogId, entry, retriesRemaining - 1, generation)
				return
			} else {
				dialog.applyInitialFocus()
			}
			dialog.forgetDialogPresentation(dialogId)
			Qt.callLater(function() {
				if (generation !== dialog.focusRequestGeneration) return
				if (!dialogContentScroll || !dialogContentScroll.contentItem) return
				const flickable = dialogContentScroll.contentItem
				const maximum = Math.max(0, flickable.contentHeight - flickable.height)
				flickable.contentY = Math.max(0, Math.min(maximum, Number(entry.contentY || 0)))
			})
		})
	}
	function clearDialogPresentationMemory() {
		dialogPresentationMemory = ({})
	}
	function ensureContentVisible(target) {
		if (!target || !dialogContentScroll || !dialogContentScroll.contentItem) return
		const flickable = dialogContentScroll.contentItem
		const contentRoot = flickable.contentItem || flickable
		// Header and footer controls share the dialog focus tree but do not belong
		// to its scroll viewport. Mapping one of those controls into content space
		// can yield a plausible out-of-range coordinate and spuriously scroll the
		// body after a delayed initial-focus callback.
		if (!isVisualDescendantOf(target, contentRoot)) return
		// Normal fields are exposed to accessibility only when their complete
		// label/control/hint container is in the viewport. Reveal that same owner
		// when one of its nested controls receives keyboard focus, otherwise the
		// focused control can be visible while its semantic subtree is suppressed.
		let revealTarget = target
		let ancestor = target
		while (ancestor && ancestor !== contentRoot) {
			if (ancestor.accessibilityAllowsPartialExposure !== undefined
					&& !ancestor.accessibilityAllowsPartialExposure) {
				revealTarget = ancestor
				break
			}
			ancestor = ancestor.parent
		}
		const point = revealTarget.mapToItem(contentRoot, 0, 0)
		const margin = Theme.spacing
		const top = flickable.contentY
		const bottom = top + flickable.height
		if (point.y < top + margin)
			flickable.contentY = Math.max(0, point.y - margin)
		else if (point.y + revealTarget.height > bottom - margin)
			flickable.contentY = Math.min(Math.max(0, flickable.contentHeight - flickable.height),
				point.y + revealTarget.height - flickable.height + margin)
	}
	function scheduleActiveFocusReveal() {
		const generation = ++activeFocusRevealGeneration
		Qt.callLater(function() {
			if (generation !== dialog.activeFocusRevealGeneration || !dialog.visible
					|| dialog.nestedModalOpen)
				return
			const target = dialog.activeDialogFocusItem()
			if (!target || !target.activeFocus)
				return
			dialog.ensureContentVisible(target)
			Qt.callLater(function() {
				if (generation !== dialog.activeFocusRevealGeneration || !target.activeFocus)
					return
				dialog.ensureContentVisible(target)
			})
		})
	}
	function resetContentPosition() {
		if (dialogContentScroll && dialogContentScroll.contentItem)
			dialogContentScroll.contentItem.contentY = 0
	}
	function itemFullyInsideContentViewport(item) {
		if (!item || !dialogContentScroll || item.width <= 0 || item.height <= 0
				|| dialogContentScroll.width <= 0 || dialogContentScroll.height <= 0)
			return false
		try {
			// mapToItem() is not itself a notifying binding dependency. Read the
			// scrolling and geometry inputs explicitly so a clipped semantic heading
			// enters/leaves UIA together with the pixels visible in the dialog body.
			const flickable = dialogContentScroll.contentItem
			const dependency = Number(flickable ? flickable.contentY || 0 : 0)
				+ Number(item.x || 0) + Number(item.y || 0)
			if (!Number.isFinite(dependency))
				return false
			const point = item.mapToItem(dialogContentScroll, 0, 0)
			const tolerance = 0.5
			return point.x >= -tolerance && point.y >= -tolerance
				&& point.x + item.width <= dialogContentScroll.width + tolerance
				&& point.y + item.height <= dialogContentScroll.height + tolerance
		} catch (error) {
			return false
		}
	}
	function itemIntersectsContentViewport(item) {
		if (!item || !dialogContentScroll || item.width <= 0 || item.height <= 0
				|| dialogContentScroll.width <= 0 || dialogContentScroll.height <= 0)
			return false
		try {
			const flickable = dialogContentScroll.contentItem
			const dependency = Number(flickable ? flickable.contentY || 0 : 0)
				+ Number(item.x || 0) + Number(item.y || 0)
			if (!Number.isFinite(dependency))
				return false
			const point = item.mapToItem(dialogContentScroll, 0, 0)
			return point.x < dialogContentScroll.width && point.y < dialogContentScroll.height
				&& point.x + item.width > 0 && point.y + item.height > 0
		} catch (error) {
			return false
		}
	}
	function itemFullyInsideDialogBounds(item) {
		if (!item || !contentItem || item.width <= 0 || item.height <= 0
				|| contentItem.width <= 0 || contentItem.height <= 0)
			return false
		try {
			const flickable = dialogContentScroll ? dialogContentScroll.contentItem : null
			const dependency = Number(flickable ? flickable.contentY || 0 : 0)
				+ Number(item.x || 0) + Number(item.y || 0)
			if (!Number.isFinite(dependency))
				return false
			const point = item.mapToItem(contentItem, 0, 0)
			const tolerance = 0.5
			return point.x >= -tolerance && point.y >= -tolerance
				&& point.x + item.width <= contentItem.width + tolerance
				&& point.y + item.height <= contentItem.height + tolerance
		} catch (error) {
			return false
		}
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
	function safeDialogImageSource(value) {
		const source = String(value === undefined || value === null ? "" : value).trim()
		if (/^(image:\/\/mumble\/|qrc:\/)/i.test(source))
			return source
		// Image-picker DTOs can carry an already-normalized, bounded PNG while
		// older controllers migrate to managed image-provider URLs. Never accept
		// arbitrary paths or remote origins here.
		if (source.length <= 3 * 1024 * 1024
				&& /^data:image\/(?:png|jpe?g|webp);base64,/i.test(source))
			return source
		return ""
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
		if (value.indexOf("stonks") >= 0) return "activity"
		if (value.indexOf("key") >= 0 || value.indexOf("binding") >= 0
			|| value.indexOf("shortcut") >= 0) return "key"
		if (value.indexOf("network") >= 0) return "connect"
		if (value.indexOf("screen") >= 0 || value.indexOf("share") >= 0) return "screen-share"
		if (value.indexOf("plugin") >= 0) return "plugin"
		if (value.indexOf("motd") >= 0 || value.indexOf("message of the day") >= 0) return "message"
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
	function normalizedHeadingText(value) {
		return String(value || "").trim().replace(/\s+/g, " ").toLowerCase()
	}
	function sectionTitleDuplicatesActiveSettingsPage(sectionTitle) {
		if (dialogState.kind !== "settings") return false
		const normalizedSectionTitle = normalizedHeadingText(sectionTitle)
		if (normalizedSectionTitle.length === 0) return false
		const page = activeSettingsPage()
		return normalizedSectionTitle === normalizedHeadingText(page.label || page.title || "")
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
		if (id === "messages" || id === "stonks" || id === "keys" || value.indexOf("message") >= 0
			|| value.indexOf("stonks") >= 0
			|| value.indexOf("binding") >= 0)
			return qsTr("Interaction")
		if (id === "network" || id === "screenshare" || id === "plugins"
			|| value.indexOf("network") >= 0 || value.indexOf("screen") >= 0 || value.indexOf("plugin") >= 0)
			return qsTr("Connectivity")
		if (id === "motd" || value.indexOf("motd") >= 0)
			return qsTr("Server")
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
			|| id === "look.modernClassicUserIcons"
			|| id === "look.modernRailSide"
			|| id === "look.modernAccent"
			|| id === "look.modernCustomAccent"
			|| id === "look.modernCustomAccentStrength"
	}
	function updateFieldValue(fieldId, value) {
		if (String(fieldId || "").indexOf("stonks.client.") === 0) {
			dialogState.invokeAction("stonks.updateClient", {
				"fieldId": String(fieldId),
				"value": value
			})
			return
		}
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
		const shouldBeVisible = dialogState.open && ownsCurrentState
		if (shouldBeVisible && !visible) {
			if (dialog.beforeOpen && dialog.beforeOpen() === false)
				return
			dialog.open()
		} else if (!shouldBeVisible && visible)
			dialog.close()
	}
	parent: popupParent ? popupParent : Overlay.overlay
	modal: !nativeWindowHosted
	dim: !nativeWindowHosted
	focus: true
	title: dialogState.title
	// Dialog creates a style-owned title bar and button box when these are left
	// unspecified. Product dialogs render both surfaces below so keep the native
	// Control scaffolding out of the visual and accessibility trees.
	header: null
	footer: null
	width: nativeWindowHosted && parent ? parent.width
		: Math.min(parent ? Math.max(240, parent.width - (Theme.space5 * 2)) : 1040,
			Math.max(320, Math.min(dialogState.preferredWidth || 920, 1180)))
	height: nativeWindowHosted && parent ? parent.height
		: Math.min(maximumHeightForParent, responsiveHeight)
	x: nativeWindowHosted ? 0 : (parent ? Math.round((parent.width - width) / 2) : 0)
	y: nativeWindowHosted ? 0 : (parent ? Math.round((parent.height - height) / 2) : 0)
    padding: 0
	closePolicy: Popup.NoAutoClose
	onOpened: {
		nestedModalOpen = false
		settingsSectionExpansionMemory = ({})
		showAdvanced = !!dialogState.state.showAdvanced
		resetContentPosition()
		scheduleContentMeasurement()
		// State publication may already have queued a higher-priority validation
		// or parent-presentation restore. Only provide opening fallback focus when
		// no such request ran and the dialog still has no focused descendant.
		const observedGeneration = focusRequestGeneration
		Qt.callLater(function() {
			if (dialog.visible && observedGeneration === dialog.focusRequestGeneration
					&& !dialog.hasActiveDialogFocus())
				dialog.applyInitialFocus()
		})
	}
	function syncConnectConfirmation() {
		const confirmation = dialogState.state.pendingConfirmation || ({})
		const shouldOpen = dialog.visible && dialogState.kind === "connect"
			&& String(confirmation.kind || "").length > 0
		if (shouldOpen && !connectConfirmationPopup.visible)
			connectConfirmationPopup.open()
		else if (!shouldOpen && connectConfirmationPopup.visible)
			connectConfirmationPopup.close()
	}
	function activeConnectSourceState() {
		dialogState.revision
		const sources = dialogState.state.sources || []
		const active = String(dialogState.state.activeSource || "favorites")
		for (let index = 0; index < sources.length; ++index) {
			if (String((sources[index] || {}).id || "") === active)
				return sources[index] || ({})
		}
		return ({ "id": active, "status": "ready", "canRetry": false, "canCancel": false })
	}
	onVisibleChanged: {
		if (!visible)
			return
		showAdvanced = !!dialogState.state.showAdvanced
	}
	Component.onCompleted: {
		syncVisibility()
		syncConnectConfirmation()
	}
	Connections {
		target: dialogState
		function onStateChanged() {
			dialog.syncVisibility()
			dialog.syncConnectConfirmation()
			const nextId = String(dialogState.state.id || "")
			const nextFocusRequestId = String(dialogState.state.focusRequestId || "")
			const explicitFocusRequest = nextFocusRequestId.length > 0
				&& nextFocusRequestId !== dialog.handledFocusRequestId
			const previousId = dialog.focusedDialogId
			const dialogChanged = previousId !== nextId
			let presentationToRestore = null
			if (dialogChanged) {
				if (nextId.length === 0) {
					dialog.clearDialogPresentationMemory()
				} else {
					presentationToRestore = dialog.dialogPresentation(nextId)
					if (!presentationToRestore) {
						if (dialog.isRootDialogId(nextId))
							dialog.clearDialogPresentationMemory()
						else
							dialog.rememberDialogPresentation(previousId)
					}
				}
			}
			const nextSurfaceSignature = dialog.currentSurfaceSignature()
			const surfaceChanged = dialog.focusedSurfaceSignature !== nextSurfaceSignature
			if (dialogChanged) {
				dialog.focusedDialogId = nextId
				dialog.focusedValidationSignature = ""
				dialog.showAdvanced = !!dialogState.state.showAdvanced
				// A newly presented dialog must not inherit the previous surface's
				// viewport while its delegates are being replaced asynchronously.
				dialog.resetContentPosition()
			}
			if (surfaceChanged && !dialogChanged && dialogState.kind === "settings")
				dialog.resetContentPosition()
			dialog.focusedSurfaceSignature = nextSurfaceSignature
			dialog.scheduleContentMeasurement()
			// State updates are also emitted for every live field edit. Reapplying
			// the initial focus in that path steals keyboard focus from sliders,
			// segmented controls and editors after their first interaction.
			if (surfaceChanged) {
				dialog.clearPendingFocusRestore()
				if (presentationToRestore)
					dialog.restoreDialogPresentation(nextId, presentationToRestore, 4)
				else
					dialog.scheduleInitialFocus()
			} else if (explicitFocusRequest) {
				dialog.clearPendingFocusRestore()
				dialog.scheduleInitialFocus()
			} else if (dialog.pendingFocusRestoreId.length > 0) {
				dialog.consumePendingFocusRestore(false)
			}
			dialog.handledFocusRequestId = nextFocusRequestId
			const nextValidationSignature = dialog.validationSignature()
			if (nextValidationSignature.length === 0) {
				dialog.focusedValidationSignature = ""
			} else if (dialog.focusedValidationSignature !== nextValidationSignature) {
				const previousValidationSignature = dialog.focusedValidationSignature
				dialog.focusedValidationSignature = nextValidationSignature
				if (dialog.validationShouldTakeFocus(dialogChanged, presentationToRestore,
						previousValidationSignature, nextValidationSignature)) {
					dialog.forgetDialogPresentation(nextId)
					// Run after any surface-level focus handoff so an explicit rejected
					// submission owns the final focus and scroll position. Background
					// validation and parent-dialog restoration preserve user focus.
					dialog.scheduleValidationFocus()
				}
			}
		}
	}
	Shortcut {
		objectName: "dialogCancelShortcut"
		sequence: StandardKey.Cancel
		enabled: dialog.visible && !dialog.nestedModalOpen
			onActivated: {
			if (dialogState.kind === "connect" && dialogState.editorOpen)
				dialogState.invokeAction("backToFavorites", {})
			else
				dialogState.requestClose()
		}
	}
	Popup {
		id: connectConfirmationPopup
		objectName: "connectConfirmationPopup"
		parent: Overlay.overlay
		modal: true
		dim: false
		focus: true
		closePolicy: Popup.NoAutoClose
		width: parent ? Math.min(460, Math.max(300, parent.width - Theme.space5 * 2)) : 420
		x: parent ? Math.round((parent.width - width) / 2) : 0
		y: parent ? Math.round((parent.height - height) / 2) : 0
		padding: Theme.space4
		onAboutToShow: dialog.rememberNestedModalFocus(null)
		onOpened: {
			dialog.nestedModalOpen = true
			Qt.callLater(function() { connectConfirmationCancel.forceActiveFocus(Qt.PopupFocusReason) })
		}
		onClosed: {
			dialog.nestedModalOpen = false
			dialog.restoreNestedModalFocus(null)
		}
		background: Rectangle {
			color: Theme.surfaceRaised
			border.color: Theme.surfaceBorder
			border.width: 1
			radius: Theme.shellRadius
		}
		contentItem: ColumnLayout {
			Accessible.role: Accessible.Dialog
			Accessible.name: String((dialogState.state.pendingConfirmation || {}).title || qsTr("Confirm action"))
			spacing: Theme.space3
			Keys.priority: Keys.BeforeItem
			Keys.onEscapePressed: event => {
				dialogState.invokeAction("dismissConfirmation", {})
				event.accepted = true
			}
			Label {
				objectName: "connectConfirmationTitle"
				Layout.fillWidth: true
				textFormat: Text.PlainText
				text: String((dialogState.state.pendingConfirmation || {}).title || qsTr("Confirm action"))
				color: Theme.textStrong
				font.pixelSize: Theme.fontTitle
				font.weight: Font.DemiBold
				wrapMode: Text.Wrap
			}
			Label {
				objectName: "connectConfirmationMessage"
				Layout.fillWidth: true
				textFormat: Text.PlainText
				text: String((dialogState.state.pendingConfirmation || {}).message || "")
				color: Theme.textMain
				wrapMode: Text.Wrap
			}
			RowLayout {
				Layout.fillWidth: true
				Layout.alignment: Qt.AlignRight
				spacing: Theme.space2
				ModernButton {
					id: connectConfirmationCancel
					objectName: "connectConfirmationCancel"
					text: String((dialogState.state.pendingConfirmation || {}).cancelLabel || qsTr("Keep editing"))
					tone: "secondary"
					activeFocusOnTab: true
					KeyNavigation.tab: connectConfirmationConfirm
					KeyNavigation.backtab: connectConfirmationConfirm
					Keys.priority: Keys.BeforeItem
					Keys.onPressed: event => {
						if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) {
							connectConfirmationConfirm.forceActiveFocus(Qt.TabFocusReason)
							event.accepted = true
						}
					}
					onClicked: dialogState.invokeAction("dismissConfirmation", {})
				}
				ModernButton {
					id: connectConfirmationConfirm
					objectName: "connectConfirmationConfirm"
					text: String((dialogState.state.pendingConfirmation || {}).confirmLabel || qsTr("Confirm"))
					tone: String((dialogState.state.pendingConfirmation || {}).confirmTone || "danger")
					activeFocusOnTab: true
					KeyNavigation.tab: connectConfirmationCancel
					KeyNavigation.backtab: connectConfirmationCancel
					Keys.priority: Keys.BeforeItem
					Keys.onPressed: event => {
						if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) {
							connectConfirmationCancel.forceActiveFocus(Qt.TabFocusReason)
							event.accepted = true
						}
					}
					onClicked: dialogState.invokeAction(
						String((dialogState.state.pendingConfirmation || {}).confirmActionId || "dismissConfirmation"), {})
				}
			}
		}
	}

	ModalAccessibilityBarrier {
		id: nestedModalAccessibilityBarrier
		objectName: "dialogNestedModalAccessibilityBarrier"
		active: dialog.nestedModalOpen
		// Nested popup content is reparented to Overlay.overlay. Suppress each
		// branch of the owning product dialog so ignored ancestors cannot promote
		// their children beside the active confirmation or color picker.
		targets: [ dialogHeader, dialogStatusBanner, dialogBodyLayout, dialogFooter ]
	}

	Overlay.modal: Rectangle {
		objectName: "dialogModalScrim"
		color: Theme.modalScrim
		Behavior on color { ColorAnimation { duration: Theme.motionNormal } }
	}

	background: Item {
		Rectangle {
			objectName: "dialogElevationShadow"
			visible: !dialog.nativeWindowHosted
			x: -Theme.space2
			y: Theme.elevationMenuOffset
			width: parent.width + Theme.space2 * 2
			height: parent.height + Theme.space2
			color: Theme.modalShadow
			radius: Theme.shellRadius + Theme.space1
		}
		Rectangle {
			objectName: "dialogSurface"
			anchors.fill: parent
			color: Theme.shellBackground
			border.color: dialog.dialogTone.length > 0 ? dialog.dialogToneColor : Theme.modalBorder
			border.width: dialog.nativeWindowHosted ? 0 : (dialog.dialogTone.length > 0 ? 2 : 1)
			radius: dialog.nativeWindowHosted ? 0 : Theme.shellRadius
			Rectangle {
				anchors.left: parent.left
				anchors.right: parent.right
				anchors.top: parent.top
				anchors.margins: 1
				height: 1
				color: Theme.elevationHighlight
				radius: dialog.nativeWindowHosted ? 0 : 1
			}
		}
    }

	contentItem: ColumnLayout {
		enabled: !dialog.nestedModalOpen
		Accessible.ignored: dialog.nestedModalOpen
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
					visible: !dialog.nativeWindowHosted
					iconName: dialog.connectEditorBackAction ? "previous" : "close"
					text: dialog.connectEditorBackAction ? qsTr("Back") : qsTr("Close")
					Accessible.name: dialog.connectEditorBackAction
						? qsTr("Back to favorites") : qsTr("Close dialog")
					onClicked: {
						if (dialog.connectEditorBackAction)
							dialogState.invokeAction("backToFavorites", {})
						else
							dialogState.requestClose()
					}
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
			id: dialogBodyLayout
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
					function activateCurrentPage() {
						if (currentIndex < 0 || currentIndex >= dialogState.pages.length)
							return false
						const page = dialogState.pages[currentIndex] || {}
						const pageId = String(page.id || "")
						if (pageId.length === 0)
							return false
						dialogState.invokeAction("selectPage", { "pageId": pageId })
						return true
					}
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
					MiddleDragScrollHandler {
						targetFlickable: dialogPageList
						horizontalEnabled: false
					}
                    spacing: Math.max(2, Math.round(Theme.spacing / 4))
					activeFocusOnTab: activeFocus || count > 0
					keyNavigationEnabled: true
					currentIndex: {
						dialogState.revision
						return dialog.activeSettingsPageIndex()
					}
					Accessible.role: Accessible.List
					Accessible.name: qsTr("Settings pages")
					Keys.onReturnPressed: event => {
						event.accepted = dialogPageList.activateCurrentPage()
					}
					Keys.onEnterPressed: event => {
						event.accepted = dialogPageList.activateCurrentPage()
					}
					Keys.onSpacePressed: event => {
						event.accepted = dialogPageList.activateCurrentPage()
					}
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
							return dialog.activeSettingsPageIndex()
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
					onContentHeightChanged: dialog.scheduleInitialFocusReveal()
					onHeightChanged: dialog.scheduleInitialFocusReveal()
					ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
					ScrollBar.vertical: ModernScrollBar {
						objectName: "dialogContentScrollBar"
						policy: ScrollBar.AsNeeded
						parent: dialogContentScroll
						anchors.top: dialogContentScroll.top
						anchors.right: dialogContentScroll.right
						anchors.bottom: dialogContentScroll.bottom
					}
					MiddleDragScrollHandler {
						parent: dialogContentScroll.contentItem
						targetFlickable: dialogContentScroll.contentItem
						horizontalEnabled: false
					}
					Column {
						id: dialogContentColumn
						width: parent.width
						spacing: Theme.spacing + 4
						onImplicitHeightChanged: {
							dialog.scheduleContentMeasurement()
							dialog.scheduleInitialFocusReveal()
						}
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
						id: connectSourceBar
						objectName: "connectSourceBar"
						width: parent.width - (dialog.densityInset * 2)
						x: dialog.densityInset
						height: visible ? connectSourceBarLayout.implicitHeight + (Theme.space3 * 2) : 0
						visible: dialogState.kind === "connect" && !dialogState.loading
							&& !dialogState.editorOpen
						color: Theme.strip
						border.color: Theme.divider
						radius: Theme.innerRadius
						Accessible.role: Accessible.Pane
						Accessible.name: qsTr("Server sources")
						RowLayout {
							id: connectSourceBarLayout
							anchors.left: parent.left
							anchors.right: parent.right
							anchors.top: parent.top
							anchors.margins: Theme.space3
							spacing: Theme.space3
							ModernSegmentedControl {
								id: connectSourceSelector
								objectName: "connectSourceSelector"
								readonly property string publishedSourceValue: String(dialogState.state.activeSource || "favorites")
								Layout.preferredWidth: dialog.compactDialogLayout ? 230 : 310
								model: {
									dialogState.revision
									const rows = dialogState.state.sources || []
									const options = []
									for (let index = 0; index < rows.length; ++index) {
										const row = rows[index] || {}
										options.push({ "value": row.id, "label": row.label,
											"hint": row.status === "unavailable" ? row.error : "" })
									}
									return options
								}
								onPublishedSourceValueChanged: setCurrentValue(publishedSourceValue)
								Component.onCompleted: setCurrentValue(publishedSourceValue)
								accessibleName: qsTr("Server source")
								accessibleDescription: qsTr("Choose saved, public, or local network servers")
								optionObjectNamePrefix: "connectSource"
								onActivated: function(index, value) {
									dialogState.invokeAction("selectSource", { "sourceId": String(value) })
								}
							}
							ModernTextField {
								id: connectFilter
								objectName: "connectServerFilter"
								Layout.fillWidth: true
								visible: !dialog.compactDialogLayout || !dialogState.editorOpen
								placeholderText: qsTr("Filter servers")
								text: String(dialogState.state.filter || "")
								Accessible.name: qsTr("Filter servers")
								onTextEdited: dialogState.updateField("connect.filter", text)
								KeyNavigation.down: connectFavoriteList
								KeyNavigation.priority: KeyNavigation.BeforeItem
								Keys.priority: Keys.BeforeItem
								Keys.forwardTo: [connectFavoriteList]
								Keys.onPressed: event => {
									if (event.key === Qt.Key_Down && connectFavoriteList.visible
											&& connectFavoriteList.count > 0) {
										++dialog.focusRequestGeneration
										connectFavoriteList.forceActiveFocus(Qt.TabFocusReason)
										event.accepted = true
									}
								}
								Shortcut {
									sequence: "Down"
									context: Qt.WindowShortcut
									enabled: connectFilter.activeFocus && connectFavoriteList.visible
										&& connectFavoriteList.count > 0 && !dialog.nestedModalOpen
									onActivated: {
									++dialog.focusRequestGeneration
									connectFavoriteList.forceActiveFocus(Qt.TabFocusReason)
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
							&& !dialogState.editorOpen
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
									Label {
										textFormat: Text.PlainText
										text: {
											dialogState.revision
											const sources = dialogState.state.sources || []
											for (let index = 0; index < sources.length; ++index)
												if (String((sources[index] || {}).id) === String(dialogState.state.activeSource || "favorites"))
													return String((sources[index] || {}).label || qsTr("Servers"))
											return qsTr("Servers")
										}
										color: Theme.textStrong; font.bold: true; font.pixelSize: 13
									}
									Label {
										textFormat: Text.PlainText
										text: {
											const rows = dialogState.state.sourceRows || []
											const filter = String(dialogState.state.filter || "")
											return filter.length > 0 ? qsTr("%n matching server(s)", "", rows.length)
												: qsTr("%n server(s)", "", rows.length)
										}
										color: Theme.textMuted
										font.pixelSize: 11
									}
								}
								ModernButton {
					objectName: "connectNewFavoriteButton"
					text: qsTr("Add server")
					tone: "secondary"
					visible: String(dialogState.state.activeSource || "favorites") === "favorites"
									onClicked: dialogState.invokeAction("newFavorite", {})
								}
							}
							Rectangle {
								id: connectSourceStatus
								objectName: "connectSourceStatus"
								readonly property var source: dialog.activeConnectSourceState()
								readonly property string status: String(source.status || "ready")
								Layout.fillWidth: true
								Layout.preferredHeight: visible ? Math.max(48, connectStatusRow.implicitHeight + Theme.space3 * 2) : 0
								visible: status === "idle" || status === "loading" || status === "error" || status === "unavailable"
									|| status === "cancelled"
								color: status === "error" || status === "unavailable"
									? Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.08) : Theme.strip
								border.color: status === "error" || status === "unavailable" ? Theme.danger : Theme.divider
								radius: Theme.innerRadius
								Accessible.role: status === "error" || status === "unavailable"
									? Accessible.AlertMessage : Accessible.StaticText
								Accessible.name: connectStatusCopy.text
								RowLayout {
									id: connectStatusRow
									anchors.fill: parent
									anchors.margins: Theme.space3
									spacing: Theme.space3
									BusyIndicator {
										objectName: "connectSourceBusy"
										visible: connectSourceStatus.status === "loading"
										running: visible
										Layout.preferredWidth: 24
										Layout.preferredHeight: 24
									}
									ModernIcon {
										visible: connectSourceStatus.status !== "loading"
										name: connectSourceStatus.status === "error" || connectSourceStatus.status === "unavailable"
											? "warning" : "info"
										size: 20
										color: connectSourceStatus.status === "error" || connectSourceStatus.status === "unavailable"
											? Theme.danger : Theme.textMuted
									}
									Label {
										id: connectStatusCopy
										Layout.fillWidth: true
										textFormat: Text.PlainText
										wrapMode: Text.Wrap
										text: connectSourceStatus.status === "idle" ? qsTr("Load this server list to discover available servers.")
											: connectSourceStatus.status === "loading" ? qsTr("Looking for servers…")
											: connectSourceStatus.status === "cancelled" ? qsTr("Server discovery was cancelled.")
											: String(connectSourceStatus.source.error || qsTr("The server list is unavailable."))
										color: connectSourceStatus.status === "error" || connectSourceStatus.status === "unavailable"
											? Theme.danger : Theme.textMain
									}
									ModernButton {
										objectName: "connectSourceCancelButton"
										visible: !!connectSourceStatus.source.canCancel
										text: qsTr("Cancel")
										tone: "secondary"
										onClicked: dialogState.invokeAction("cancelSource", {
											"sourceId": String(connectSourceStatus.source.id || "") })
									}
									ModernButton {
										objectName: "connectSourceRetryButton"
										visible: !!connectSourceStatus.source.canRetry
										text: qsTr("Retry")
										tone: "secondary"
										onClicked: dialogState.invokeAction("retrySource", {
											"sourceId": String(connectSourceStatus.source.id || "") })
									}
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
								model: dialogState.state.sourceRows === undefined
									? dialogState.favorites : (dialogState.state.sourceRows || [])
								MiddleDragScrollHandler {
									targetFlickable: connectFavoriteList
									horizontalEnabled: false
								}
								currentIndex: Number(dialogState.state.selectedServerIndex === undefined
									? dialogState.selectedFavoriteIndex : dialogState.state.selectedServerIndex)
								activeFocusOnTab: activeFocus || (visible && count > 0)
								Accessible.role: Accessible.List
								Accessible.name: qsTr("Saved servers")
								function requestKeyboardSelection(index) {
									if (count <= 0) return false
									const nextIndex = Math.max(0, Math.min(count - 1, index))
									++dialog.focusRequestGeneration
									forceActiveFocus(Qt.TabFocusReason)
									const row = model[nextIndex] || {}
									dialogState.invokeAction("selectServer", { "sourceId": row.sourceId,
										"id": row.id, "index": row.index, "edit": false })
									positionViewAtIndex(nextIndex, ListView.Contain)
									return true
								}
								Keys.onPressed: event => {
									if (event.key === Qt.Key_Up) {
										event.accepted = requestKeyboardSelection(currentIndex - 1)
									} else if (event.key === Qt.Key_Down) {
										event.accepted = requestKeyboardSelection(currentIndex + 1)
									} else if (event.key === Qt.Key_Space && currentIndex >= 0) {
										event.accepted = requestKeyboardSelection(currentIndex)
									} else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
											&& currentIndex >= 0) {
										const row = model[currentIndex] || {}
										dialogState.invokeAction("connectServer", { "sourceId": row.sourceId,
											"id": row.id, "index": row.index })
										event.accepted = true
									}
								}
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
									highlighted: !!modelData.selected || String(modelData.id || "")
										=== String(dialogState.state.selectedServerId || "")
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
									onClicked: dialogState.invokeAction("selectServer", { "sourceId": modelData.sourceId,
										"id": modelData.id, "index": modelData.index, "edit": false })
									onDoubleClicked: dialogState.invokeAction("connectServer", { "sourceId": modelData.sourceId,
										"id": modelData.id, "index": modelData.index })
								}
								Label {
									textFormat: Text.PlainText
									anchors.centerIn: parent
									visible: connectFavoriteList.count === 0
									text: String(dialogState.state.filter || "").length > 0
										? qsTr("No servers match your filter.")
										: String(dialogState.state.activeSource || "favorites") === "favorites"
											? qsTr("Add a server to get started.") : qsTr("No servers found.")
									color: Theme.textMuted
								}
							}
						}
					}
					Rectangle {
						id: connectEditorSurface
						objectName: "connectEditorSurface"
						width: parent.width - (dialog.densityInset * 2)
						x: dialog.densityInset
						height: visible ? connectEditorHeader.implicitHeight + (dialog.sectionPadding * 2) : 0
						visible: dialogState.kind === "connect" && !dialogState.loading
							&& dialogState.editorOpen
						color: Theme.panel
						border.color: Theme.divider
						radius: Theme.innerRadius
						Accessible.role: Accessible.Pane
						Accessible.name: dialogState.editorTitle
						RowLayout {
							id: connectEditorHeader
							anchors.fill: parent
							anchors.margins: dialog.sectionPadding
							spacing: Theme.spacing
							ModernIcon {
								name: "connect"
								size: 22
								color: Theme.accent
							}
							ColumnLayout {
								Layout.fillWidth: true
								spacing: 1
								Label {
									objectName: "connectEditorTitle"
									Layout.fillWidth: true
									textFormat: Text.PlainText
									text: dialogState.editorTitle
									color: Theme.textStrong
									font.bold: true
									font.pixelSize: 13
								}
								Label {
									Layout.fillWidth: true
									textFormat: Text.PlainText
									text: qsTr("Server details")
									color: Theme.textMuted
									font.pixelSize: 11
								}
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
					Loader {
						id: recorderLoader
						objectName: "recorderEditorLoader"
						width: parent.width - (dialog.densityInset * 2)
						x: dialog.densityInset
						active: dialogState.kind === "recorder" && dialog.recorderController !== null
						visible: active
						sourceComponent: recorderEditorComponent
						onLoaded: {
							dialog.scheduleContentMeasurement()
							Qt.callLater(function() {
								if (dialog.visible) dialog.applyInitialFocus()
							})
						}
					}
					Connections {
						target: recorderLoader.item
						enabled: recorderLoader.status === Loader.Ready && recorderLoader.item !== null
						function onBrowseRequested(currentDirectory) {
							dialog.invokeNativePickerAction("browseRecordingDirectory",
								{ "currentDirectory": currentDirectory }, "recorderBrowseButton")
						}
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
						function onSourceSelected(sourceId) {
							dialogState.invokeAction("screenShare.selectSource", { "sourceId": sourceId })
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
					Binding {
						target: stonksLoader.item
						property: "modalHost"
						value: dialog
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
								 && dialogState.kind !== "recorder"
								 && dialogState.kind !== "certificate" && !dialogState.loading
						model: visible ? dialogState.sections : []
						delegate: Rectangle {
							id: sectionContainer
                            required property var modelData
							required property int index
							objectName: "dialogSection_" + String(modelData.id || index)
							property bool sectionAdvanced: !!modelData.advanced
							property bool sectionVisible: !modelData.advanced || dialog.showAdvanced
							property bool sectionCollapsible: !!modelData.collapsible
							property bool sectionExpanded: !sectionCollapsible
								|| dialog.settingsSectionExpanded(modelData, index)
							property bool sectionDetailsVisible: !sectionCollapsible || sectionExpanded
							readonly property bool accessibilityFullyVisible: visible
								&& dialog.itemFullyInsideDialogBounds(sectionContainer)
							readonly property string sectionPresentation: String(modelData.presentation || "form").toLowerCase()
							readonly property bool listPresentation: sectionPresentation === "list"
							readonly property bool recordsPresentation: sectionPresentation === "records"
							readonly property int presentationPadding: listPresentation || recordsPresentation
								? Math.max(8, Theme.space2) : dialog.sectionPadding
							width: parent.width - (dialog.densityInset * 2)
							x: dialog.densityInset
							height: sectionVisible ? sectionColumn.implicitHeight + (presentationPadding * 2) : 0
							visible: sectionVisible
							color: recordsPresentation ? Theme.surfaceRaised
								: listPresentation ? Theme.strip : Theme.panel
							border.color: Theme.divider
							radius: Theme.innerRadius
							Accessible.role: Accessible.Pane
							Accessible.name: String(modelData.title || qsTr("Dialog section"))
							Accessible.description: sectionCollapsible
								? (sectionExpanded ? qsTr("Expanded section") : qsTr("Collapsed section"))
								: sectionPresentation
							Accessible.ignored: !accessibilityFullyVisible
                            Column {
                                id: sectionColumn
								Accessible.ignored: true
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
								anchors.margins: parent.presentationPadding
								spacing: parent.listPresentation || parent.recordsPresentation
									? Theme.space2 : Math.max(6, Theme.spacing - 2)
								RowLayout {
									width: parent.width
									spacing: Theme.space2
									Label {
										id: sectionTitleLabel
										objectName: "dialogSectionHeading_" + String(modelData.id || index)
										textFormat: Text.PlainText
										Layout.fillWidth: true
										text: modelData.title || ""
										visible: text.length > 0
											&& !dialog.sectionTitleDuplicatesActiveSettingsPage(text)
										color: Theme.textStrong
										font.pixelSize: 13
										font.bold: true
										Accessible.role: Accessible.Heading
										Accessible.name: text
										Accessible.ignored: !visible
											|| !dialog.itemFullyInsideContentViewport(sectionTitleLabel)
									}
									ModernIconButton {
										id: sectionExpandButton
										objectName: "dialogSectionToggle_" + String(modelData.id || index)
										visible: sectionContainer.sectionCollapsible
										dense: true
										iconName: sectionContainer.sectionExpanded ? "chevron-up" : "chevron-down"
										text: sectionContainer.sectionExpanded ? qsTr("Collapse") : qsTr("Expand")
										Accessible.name: sectionContainer.sectionExpanded
											? qsTr("Collapse %1").arg(String(modelData.title || qsTr("section")))
											: qsTr("Expand %1").arg(String(modelData.title || qsTr("section")))
										Accessible.description: sectionContainer.sectionExpanded
											? qsTr("Hide these settings") : qsTr("Show these settings")
										onClicked: dialog.setSettingsSectionExpanded(
											modelData, sectionContainer.index, !sectionContainer.sectionExpanded)
									}
								}
								Label {
									id: sectionSubtitleLabel
									textFormat: Text.PlainText
									width: parent.width
									text: modelData.subtitle || ""
									visible: text.length > 0
									color: Theme.textMuted
									font.pixelSize: 11
									wrapMode: Text.Wrap
									Accessible.ignored: !visible
										|| !dialog.itemFullyInsideContentViewport(sectionSubtitleLabel)
								}
                                Repeater {
                                    model: modelData.fields || []
                                    delegate: Item {
                                        id: fieldContainer
										objectName: "dialogFieldContainer_" + String((modelData || {}).id || "")
										Accessible.ignored: true
                                        required property var modelData
                                        width: sectionColumn.width
                                        property var field: modelData
										readonly property string fieldType: String((field || {}).type || "text")
										readonly property bool accessibilityAllowsPartialExposure:
											[ "pluginEditor", "messageEventEditor", "shortcutEditor", "motdEditor",
											  "aclEditor", "serverAdminEditor", "resultList" ]
												.indexOf(fieldType) >= 0
                                        property bool conditionVisible: {
                                            dialogState.revision
                                            return !field.visibleWhen
                                                || (field.visibleWhen.values || []).indexOf(
                                                    String(dialogState.fieldValue(field.visibleWhen.fieldId))) >= 0
                                        }
									property bool advancedVisible: sectionColumn.parent.sectionVisible
										&& sectionColumn.parent.sectionDetailsVisible
										&& (!field.advanced || dialog.showAdvanced)
										visible: conditionVisible && advancedVisible
										readonly property bool accessibilityExposed: visible
											&& (accessibilityAllowsPartialExposure
												? dialog.itemIntersectsContentViewport(fieldContainer)
												: dialog.itemFullyInsideContentViewport(fieldContainer))
										height: visible ? fieldLoader.height
											+ (fieldErrorLabel.visible ? fieldErrorLabel.implicitHeight + Theme.space1 : 0) : 0
										onFieldChanged: if (fieldLoader.item) fieldLoader.item.field = field

						Loader {
							id: fieldLoader
							width: parent.width
							active: fieldContainer.visible
							onLoaded: {
								item.field = fieldContainer.field
								dialog.scheduleContentMeasurement()
								// A delayed initial-focus target can be created after the dialog
								// opens. Only that target may request the handoff; loading an
								// advanced or conditionally-visible field must not steal focus
								// from the control the user is already operating.
								const explicitFocus = dialog.explicitMetadataFocusId()
								if (String(item.objectName || "") === dialog.metadataFocusId()
										|| (explicitFocus.length > 0
											&& dialog.focusObjectInTree(item, explicitFocus) !== null))
									Qt.callLater(dialog.applyInitialFocus)
							}
											onItemChanged: if (item) item.field = fieldContainer.field
											sourceComponent: {
												const field = fieldContainer.field
                                            const type = field.type || "text"
                                            if (type === "hidden") return hiddenField
                                            if (type === "note") return noteField
											if (type === "voiceMeter") return voiceMeterField
											if (type === "inputEnhancementCalibration") return inputEnhancementCalibrationField
											if (type === "devicePriorityList") return devicePriorityListField
											if (type === "readonly" || type === "status") return readonlyField
                                            if (type === "checkbox" || type === "toggle") return checkboxField
											if (type === "select" || type === "combo" || type === "dropdown") {
												if (field.presentation === "themeGrid") return themeGridField
												if (field.presentation === "accentGrid") return accentGridField
												if (field.presentation === "segmented") return segmentedField
												return selectField
											}
											if (type === "slider" || type === "range") return rangeField
											if (type === "number" || type === "integer") return numberField
                                            if (type === "action" || type === "button") return actionField
                                            if (type === "pluginEditor") return pluginEditorField
                                            if (type === "messageEventEditor") return messageEventEditorField
                                            if (type === "shortcutEditor") return shortcutEditorField
                                            if (type === "aclEditor") return aclEditorField
											if (type === "serverAdminEditor") return serverAdminEditorField
											if (type === "motdEditor") return motdEditorField
                                            if (type === "textarea") return textareaField
                                            if (type === "resultList") return resultListField
                                            if (type === "color") return colorField
                                            if (type === "profile") return profileField
											if (type === "imagePicker") return imageField
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
										ModalAccessibilityBarrier {
											objectName: "dialogFieldViewportAccessibilityBarrier_"
												+ fieldContainer.field.id
											active: fieldContainer.visible && !fieldContainer.accessibilityExposed
											targets: [ fieldLoader, fieldErrorLabel ]
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
			id: dialogFooter
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
					text: dialog.showAdvanced ? qsTr("Advanced settings visible")
						: qsTr("Advanced settings hidden")
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
			Component {
				id: footerActionDelegate
				ModernButton {
					required property var modelData
					readonly property bool primaryAction: String(modelData.id || "")
						=== String(dialogState.primaryActionId || "")
					objectName: "dialogAction_" + modelData.id
					text: modelData.label || modelData.text || modelData.id
					width: dialogState.kind === "connect" && !dialogState.editorOpen
						? Math.floor((footerActions.width - Math.max(0,
							dialog.presentedFooterActions.length - 1) * dialog.footerActionSpacing)
							/ Math.max(1, dialog.presentedFooterActions.length)) : implicitWidth
					dense: dialogState.kind === "settings"
					enabled: (!dialogState.loading || modelData.id === "close" || modelData.id === "cancel")
						&& (modelData.enabled === undefined || modelData.enabled)
					highlighted: primaryAction
					tone: String(modelData.tone || (primaryAction ? "accent" : "secondary"))
					Accessible.description: primaryAction ? qsTr("Primary action") : ""
					onClicked: {
						const payload = dialogState.kind === "screenShare"
							&& screenShareLoader.item && modelData.id === "screenShare.start"
							? screenShareLoader.item.actionPayload() : ({})
						dialogState.invokeAction(modelData.id, payload)
					}
					// AbstractButton's Space handling is intercepted by the modal
					// footer focus scope on Windows. Handle only Space locally;
					// Return/Enter are centralized in ModernButton.
					Keys.onSpacePressed: event => {
						event.accepted = true
						if (enabled) clicked()
					}
				}
			}
			Component {
				id: wideFooterActions
				Row {
					spacing: dialog.footerActionSpacing
					layoutDirection: Qt.LeftToRight
					Repeater {
						model: dialog.presentedFooterActions
						delegate: footerActionDelegate
					}
				}
			}
			Component {
				id: compactFooterActions
				Flow {
					spacing: dialog.footerActionSpacing
					layoutDirection: Qt.LeftToRight
					Repeater {
						model: dialog.presentedFooterActions
						delegate: footerActionDelegate
					}
				}
			}
			Loader {
				id: footerActions
				anchors.right: parent.right
				anchors.rightMargin: Theme.spacing
				anchors.verticalCenter: parent.verticalCenter
				readonly property real availableWidth: Math.max(0, parent.width - (Theme.spacing * 2)
					- (settingsAdvancedFooter.visible ? settingsAdvancedFooter.width + Theme.spacing : 0))
				width: dialog.compactDialogLayout || (dialogState.kind === "connect"
					&& !dialogState.editorOpen) ? availableWidth
					: Math.min(availableWidth, item ? item.implicitWidth : availableWidth)
				height: item ? item.implicitHeight : 0
				sourceComponent: dialog.compactDialogLayout ? compactFooterActions : wideFooterActions
			}
        }
    }

	Component {
		id: certificateSectionCardComponent
		Rectangle {
			id: certificateSectionCard
			property var sectionData: ({})
			readonly property string sectionPresentation: String(sectionData.presentation || "").toLowerCase()
			objectName: "dialogSection_" + String(sectionData.id || sectionData.presentation || "certificate")
			width: parent ? parent.width : 0
			implicitHeight: certificateSectionColumn.implicitHeight + (dialog.sectionPadding * 2)
			height: implicitHeight
			color: sectionPresentation === "certificate-current" ? Theme.strip : Theme.panel
			border.color: Theme.divider
			radius: Theme.innerRadius
			Accessible.role: Accessible.Pane
			Accessible.name: String(sectionData.title || qsTr("Certificate section"))
			Accessible.description: sectionPresentation
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
							onLoaded: {
								item.field = certificateFieldContainer.field
								dialog.scheduleContentMeasurement()
								Qt.callLater(dialog.applyInitialFocus)
							}
							onItemChanged: if (item) item.field = certificateFieldContainer.field
							sourceComponent: {
								const field = certificateFieldContainer.field
								const type = field.type || "text"
								if (type === "hidden") return hiddenField
								if (type === "note") return noteField
								if (type === "readonly" || type === "status") return readonlyField
								if (type === "select" || type === "combo" || type === "dropdown")
									return certificateFieldContainer.field.presentation === "segmented"
										? segmentedField : selectField
								if (type === "slider" || type === "range") return rangeField
								if (type === "number" || type === "integer") return numberField
								if (type === "imagePicker") return imageField
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
	Component {
		id: recorderEditorComponent
		RecorderEditor {
			recorderController: dialog.recorderController
			visualFixtureMode: dialog.visualFixtureMode
		}
	}
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
			readonly property var safeField: field || ({})
			readonly property bool certificateDetail: dialogState.kind === "certificate"
				&& String(safeField.id || "").indexOf("cert.") === 0
            width: parent ? parent.width : 0
			columns: certificateDetail ? 2 : 1
			columnSpacing: Theme.space3
			rowSpacing: 2
			Label {
				textFormat: Text.PlainText
				text: readonlyRoot.safeField.label || ""
				visible: text.length > 0
				color: Theme.textMuted
				font.pixelSize: certificateDetail ? 9 : 11
				font.bold: certificateDetail
				font.letterSpacing: certificateDetail ? 0.45 : 0
				Layout.preferredWidth: certificateDetail ? 100 : implicitWidth
				Layout.alignment: Qt.AlignTop
			}
            Label {
				objectName: "dialogReadonlyValue_" + String(readonlyRoot.safeField.id || "")
				Layout.fillWidth: true
				textFormat: Text.PlainText
				text: String(readonlyRoot.safeField.value ?? "")
				color: Theme.textMain
				wrapMode: Text.WrapAnywhere
				// On Windows, an empty family can produce an accessibility node without
				// a painted glyph run. Keep a concrete application-font fallback.
				font.family: readonlyRoot.safeField.monospace ? "Consolas" : dialog.font.family
				font.pixelSize: readonlyRoot.safeField.monospace ? 11 : Theme.fontBody
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
				// Live microphone samples are presentation-only state. Deterministic
				// visual fixtures must render the field DTO instead so a running audio
				// device cannot keep screenshot capture from ever stabilizing. Keep the
				// live map behind the branch so fixture mode has no reactive dependency
				// on presentationFieldValuesChanged at all.
				let sourceValue = field.value
				if (!dialog.visualFixtureMode) {
					const presentationValues = dialogState.presentationFieldValues || ({})
					const presentationValue = presentationValues[fieldId]
					if (presentationValue !== undefined)
						sourceValue = presentationValue
				}
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
			readonly property bool thresholdEditorAvailable: field.active !== false
				&& String(field.silenceThresholdFieldId || "").length > 0
				&& String(field.speechThresholdFieldId || "").length > 0
			property real previewSilenceThreshold: 0
			property real previewSpeechThreshold: 100
			readonly property int silenceThreshold: Math.round(Math.max(0,
				Math.min(100, previewSilenceThreshold)))
			readonly property int speechThreshold: Math.round(Math.max(silenceThreshold,
				Math.min(100, previewSpeechThreshold)))
			readonly property bool replayActive: Number(field.loopbackMode || meter.loopbackMode || 0) !== 0
			function syncThresholdPreview() {
				if (voiceStopThresholdControl.pressed || voiceStartThresholdControl.pressed)
					return
				const stopValue = Math.max(0, Math.min(100, Number(field.silenceThreshold || 0)))
				const startValue = Math.max(stopValue,
					Math.min(100, Number(field.speechThreshold === undefined ? 100 : field.speechThreshold)))
				previewSilenceThreshold = stopValue
				previewSpeechThreshold = startValue
			}
			onFieldChanged: Qt.callLater(syncThresholdPreview)
			Component.onCompleted: syncThresholdPreview()
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
						enabled: !voiceMeterRoot.field.staticMeter && !dialog.visualFixtureMode
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
			ColumnLayout {
				id: voiceThresholdEditor
				objectName: "voiceThresholdEditor_" + String(voiceMeterRoot.field.id || "")
				Layout.fillWidth: true
				visible: voiceMeterRoot.thresholdEditorAvailable
				spacing: Theme.space1
				property bool stopDirty: false
				property bool startDirty: false
				function commitStopThreshold() {
					if (!stopDirty)
						return
					stopDirty = false
					dialog.updateFieldValue(String(voiceMeterRoot.field.silenceThresholdFieldId),
						Math.round(voiceMeterRoot.previewSilenceThreshold))
				}
				function commitStartThreshold() {
					if (!startDirty)
						return
					startDirty = false
					dialog.updateFieldValue(String(voiceMeterRoot.field.speechThresholdFieldId),
						Math.round(voiceMeterRoot.previewSpeechThreshold))
				}
				Rectangle {
					Layout.fillWidth: true
					Layout.preferredHeight: 1
					color: Theme.divider
				}
				RowLayout {
					Layout.fillWidth: true
					Label {
						Layout.fillWidth: true
						textFormat: Text.PlainText
						text: qsTr("Manual thresholds")
						color: Theme.textStrong
						font.bold: true
					}
					Label {
						textFormat: Text.PlainText
						text: qsTr("Adjust while speaking")
						color: Theme.textMuted
						font.pixelSize: 10
					}
				}
				Label {
					Layout.fillWidth: true
					textFormat: Text.PlainText
					text: qsTr("The meter follows the handles immediately. The setting is applied once you release a handle.")
					color: Theme.textMuted
					font.pixelSize: 10
					wrapMode: Text.Wrap
				}
				RowLayout {
					Layout.fillWidth: true
					Label {
						Layout.fillWidth: true
						textFormat: Text.PlainText
						text: qsTr("Stop · closes the microphone")
						color: Theme.textMuted
						font.pixelSize: 11
					}
					Label {
						objectName: "dialogRangeValue_" + String(voiceMeterRoot.field.silenceThresholdFieldId || "")
						textFormat: Text.PlainText
						text: qsTr("%1%").arg(voiceMeterRoot.silenceThreshold)
						color: Theme.textStrong
						font.pixelSize: Theme.fontLabel
						font.weight: Font.DemiBold
						Accessible.ignored: true
					}
				}
				ModernSlider {
					id: voiceStopThresholdControl
					objectName: "dialogField_" + String(voiceMeterRoot.field.silenceThresholdFieldId || "")
					Layout.fillWidth: true
					from: 0
					to: Math.max(0, voiceMeterRoot.previewSpeechThreshold)
					stepSize: 1
					value: voiceMeterRoot.previewSilenceThreshold
					snapMode: Slider.SnapOnRelease
					live: true
					Accessible.name: qsTr("Stop threshold")
					Accessible.description: qsTr("Closes the microphone below %1 percent").arg(
						voiceMeterRoot.silenceThreshold)
					onMoved: {
						voiceMeterRoot.previewSilenceThreshold = Math.min(value,
							voiceMeterRoot.previewSpeechThreshold)
						voiceThresholdEditor.stopDirty = true
						if (!pressed)
							voiceThresholdEditor.commitStopThreshold()
					}
					onPressedChanged: {
						if (!pressed)
							voiceThresholdEditor.commitStopThreshold()
					}
				}
				RowLayout {
					Layout.fillWidth: true
					Label {
						Layout.fillWidth: true
						textFormat: Text.PlainText
						text: qsTr("Start · opens the microphone")
						color: Theme.textMuted
						font.pixelSize: 11
					}
					Label {
						objectName: "dialogRangeValue_" + String(voiceMeterRoot.field.speechThresholdFieldId || "")
						textFormat: Text.PlainText
						text: qsTr("%1%").arg(voiceMeterRoot.speechThreshold)
						color: Theme.textStrong
						font.pixelSize: Theme.fontLabel
						font.weight: Font.DemiBold
						Accessible.ignored: true
					}
				}
				ModernSlider {
					id: voiceStartThresholdControl
					objectName: "dialogField_" + String(voiceMeterRoot.field.speechThresholdFieldId || "")
					Layout.fillWidth: true
					from: Math.min(100, voiceMeterRoot.previewSilenceThreshold)
					to: 100
					stepSize: 1
					value: voiceMeterRoot.previewSpeechThreshold
					snapMode: Slider.SnapOnRelease
					live: true
					Accessible.name: qsTr("Start threshold")
					Accessible.description: qsTr("Opens the microphone above %1 percent").arg(
						voiceMeterRoot.speechThreshold)
					onMoved: {
						voiceMeterRoot.previewSpeechThreshold = Math.max(value,
							voiceMeterRoot.previewSilenceThreshold)
						voiceThresholdEditor.startDirty = true
						if (!pressed)
							voiceThresholdEditor.commitStartThreshold()
					}
					onPressedChanged: {
						if (!pressed)
							voiceThresholdEditor.commitStartThreshold()
					}
				}
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
			VoiceActivationSetup {
				field: voiceMeterRoot.field
				meter: voiceMeterRoot.meter
				controller: dialogState
			}
			Flow {
				Layout.fillWidth: true
				spacing: Math.max(6, Math.round(Theme.spacing / 2))
				ModernButton {
					objectName: "voiceMeterReplay_" + String(voiceMeterRoot.field.id || "")
					visible: String(voiceMeterRoot.field.replayStartActionId || voiceMeterRoot.field.replayStopActionId || "").length > 0
					enabled: !voiceMeterRoot.field.inputEnhancementCalibrationTransmissionBlocked
						&& String(voiceMeterRoot.field.inputEnhancementCalibrationWorkerState || "idle") !== "running"
						&& String(voiceMeterRoot.field.inputEnhancementCalibrationWorkerState || "idle") !== "cancelling"
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
		id: inputEnhancementCalibrationField
		ColumnLayout {
			id: calibrationFieldRoot
			objectName: "dialogField_" + String((field || {}).id || "")
			property var field: ({})
			width: parent ? parent.width : 0
			InputEnhancementCalibration {
				field: calibrationFieldRoot.field
				controller: dialogState
			}
		}
	}
    Component {
        id: checkboxField
		ColumnLayout {
			id: checkboxRoot
			property var field
			width: parent ? parent.width : 0
			spacing: Theme.space1
			ModernCheckBox {
				objectName: "dialogField_" + String((checkboxRoot.field || {}).id || "")
				Layout.fillWidth: true
				text: checkboxRoot.field.label || ""
				checked: !!checkboxRoot.field.value
				enabled: checkboxRoot.field.enabled === undefined || checkboxRoot.field.enabled
				Accessible.description: String(checkboxRoot.field.hint || checkboxRoot.field.unavailableReason || "")
				onToggled: dialog.updateFieldValue(checkboxRoot.field.id, checked)
			}
			Label {
				Layout.fillWidth: true
				objectName: "dialogFieldHint_" + String((checkboxRoot.field || {}).id || "")
				visible: String(checkboxRoot.field.hint || checkboxRoot.field.unavailableReason || "").length > 0
				textFormat: Text.PlainText
				text: String(checkboxRoot.field.hint || checkboxRoot.field.unavailableReason || "")
				color: Theme.textMuted
				font.pixelSize: 10
				wrapMode: Text.Wrap
				Accessible.ignored: true
			}
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
				toolTipText: String(selectRoot.field.tooltip || "")
				Accessible.name: String(selectRoot.field.label || "")
				Accessible.description: String(selectRoot.field.tooltip || selectRoot.field.hint
					|| selectRoot.field.unavailableReason || "")
                Component.onCompleted: selectRoot.syncCurrentIndex()
                onModelChanged: selectRoot.syncCurrentIndex()
				onActivated: {
					if (currentIndex >= 0 && optionEnabled(currentIndex))
						dialog.updateFieldValue(selectRoot.field.id, currentValue)
				}
            }
			Label {
				Layout.fillWidth: true
				objectName: "dialogFieldHint_" + String((selectRoot.field || {}).id || "")
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
		id: devicePriorityListField
		ColumnLayout {
			id: priorityRoot
			property var field: ({})
			width: parent ? parent.width : 0
			spacing: Theme.space2

			function values() {
				return Array.from(field.value || [])
			}
			function optionFor(value) {
				const options = field.options || []
				for (let i = 0; i < options.length; ++i) {
					if (String(options[i].value) === String(value))
						return options[i]
				}
				return ({ "label": qsTr("Remembered device"), "value": value, "enabled": false })
			}
			function commit(values) {
				dialog.updateFieldValue(field.id, values)
			}
			function move(from, delta) {
				const next = values()
				const to = from + delta
				if (from < 0 || from >= next.length || to < 0 || to >= next.length)
					return
				const moved = next.splice(from, 1)[0]
				next.splice(to, 0, moved)
				commit(next)
			}
			function removeAt(index) {
				const next = values()
				if (index < 0 || index >= next.length)
					return
				next.splice(index, 1)
				commit(next)
			}
			function contains(value) {
				const current = values()
				for (let i = 0; i < current.length; ++i) {
					if (String(current[i]) === String(value))
						return true
				}
				return false
			}

			Label {
				Layout.fillWidth: true
				textFormat: Text.PlainText
				text: priorityRoot.field.label || qsTr("Device priority")
				color: Theme.textMuted
				font.pixelSize: 11
			}
			Repeater {
				model: priorityRoot.field.value || []
				delegate: Rectangle {
					id: priorityRow
					required property int index
					required property var modelData
					readonly property var deviceOption: priorityRoot.optionFor(modelData)
					readonly property string deviceLabel: String(deviceOption.label || qsTr("Remembered device"))
					Layout.fillWidth: true
					implicitHeight: priorityRowLayout.implicitHeight + Theme.space2 * 2
					radius: Theme.innerRadius
					color: Theme.surfaceRaised
					border.color: Theme.surfaceBorder
					RowLayout {
						id: priorityRowLayout
						anchors.fill: parent
						anchors.margins: Theme.space2
						spacing: Theme.space2
						Label {
							textFormat: Text.PlainText
							text: String(priorityRow.index + 1)
							color: Theme.accent
							font.bold: true
							Layout.preferredWidth: 18
							Accessible.ignored: true
						}
						Label {
							Layout.fillWidth: true
							textFormat: Text.PlainText
							text: priorityRow.deviceLabel
							color: priorityRow.deviceOption.enabled === false ? Theme.textMuted : Theme.textMain
							elide: Text.ElideRight
							Accessible.name: qsTr("Priority %1: %2").arg(priorityRow.index + 1).arg(text)
						}
						ModernButton {
							objectName: "devicePriorityUp_" + priorityRow.index
							text: qsTr("Up")
							dense: true
							enabled: priorityRow.index > 0
							Accessible.name: qsTr("Move %1 up").arg(priorityRow.deviceLabel)
							onClicked: priorityRoot.move(priorityRow.index, -1)
						}
						ModernButton {
							objectName: "devicePriorityDown_" + priorityRow.index
							text: qsTr("Down")
							dense: true
							enabled: priorityRow.index + 1 < priorityRoot.values().length
							Accessible.name: qsTr("Move %1 down").arg(priorityRow.deviceLabel)
							onClicked: priorityRoot.move(priorityRow.index, 1)
						}
						ModernButton {
							objectName: "devicePriorityRemove_" + priorityRow.index
							text: qsTr("Remove")
							dense: true
							Accessible.name: qsTr("Remove %1 from priority list").arg(priorityRow.deviceLabel)
							onClicked: priorityRoot.removeAt(priorityRow.index)
						}
					}
				}
			}
			RowLayout {
				Layout.fillWidth: true
				spacing: Theme.space2
				ModernComboBox {
					id: priorityAddCombo
					objectName: "devicePriorityAddChoice_" + String(priorityRoot.field.id || "")
					Layout.fillWidth: true
					model: priorityRoot.field.options || []
					textRole: "label"
					valueRole: "value"
					Accessible.name: qsTr("Known device to add")
					Accessible.description: String(priorityRoot.field.tooltip || priorityRoot.field.hint || "")
				}
				ModernButton {
					objectName: "devicePriorityAdd_" + String(priorityRoot.field.id || "")
					text: qsTr("Add")
					dense: true
					enabled: priorityAddCombo.currentIndex >= 0
						&& priorityAddCombo.optionEnabled(priorityAddCombo.currentIndex)
						&& !priorityRoot.contains(priorityAddCombo.currentValue)
					Accessible.name: qsTr("Add device to priority list")
					onClicked: {
						const next = priorityRoot.values()
						next.push(priorityAddCombo.currentValue)
						priorityRoot.commit(next)
					}
				}
			}
			Label {
				Layout.fillWidth: true
				textFormat: Text.PlainText
				text: String(priorityRoot.field.hint || "")
				visible: text.length > 0
				color: Theme.textMuted
				font.pixelSize: 10
				wrapMode: Text.Wrap
				Accessible.ignored: true
			}
		}
	}
	Component {
		id: segmentedField
		ColumnLayout {
			id: segmentedRoot
			property var field
			width: parent ? parent.width : 0
			spacing: Theme.space1
			onFieldChanged: Qt.callLater(function() {
				segmentedControl.setCurrentValue(segmentedRoot.field.value)
			})
			Label {
				Layout.fillWidth: true
				textFormat: Text.PlainText
				text: segmentedRoot.field.label || ""
				color: Theme.textMuted
				font.pixelSize: 11
			}
			ModernSegmentedControl {
				id: segmentedControl
				objectName: "dialogField_" + String((segmentedRoot.field || {}).id || "")
				Layout.fillWidth: true
				model: segmentedRoot.field.options || []
				enabled: segmentedRoot.field.enabled === undefined || Boolean(segmentedRoot.field.enabled)
				accessibleName: String(segmentedRoot.field.label || "")
				accessibleDescription: String(segmentedRoot.field.hint || segmentedRoot.field.unavailableReason || "")
				optionObjectNamePrefix: "dialogSegmentOption_"
					+ String((segmentedRoot.field || {}).id || "")
			onActivated: function(index, value) {
					dialog.updateFieldValue(segmentedRoot.field.id, value)
				}
			}
			Label {
				Layout.fillWidth: true
				objectName: "dialogFieldHint_" + String((segmentedRoot.field || {}).id || "")
				visible: String(segmentedRoot.field.hint || segmentedRoot.field.unavailableReason || "").length > 0
				textFormat: Text.PlainText
				text: String(segmentedRoot.field.hint || segmentedRoot.field.unavailableReason || "")
				color: Theme.textMuted
				font.pixelSize: 10
				wrapMode: Text.Wrap
				Accessible.ignored: true
			}
		}
	}
	Component {
		id: rangeField
		ColumnLayout {
			id: rangeRoot
			property var field: ({})
			property real pendingValue: minimumValue
			property bool pendingValueDirty: false
			readonly property real minimumValue: Number(field.minimum ?? field.min ?? 0)
			readonly property real maximumValue: Number(field.maximum ?? field.max ?? 100)
			readonly property real fieldStep: Math.max(0.000001, Number(field.step ?? field.stepSize ?? 1))
			readonly property bool integralValue: Math.round(fieldStep) === fieldStep
				&& Math.round(minimumValue) === minimumValue && Math.round(maximumValue) === maximumValue
			function typedValue(value) {
				return integralValue ? Math.round(Number(value)) : Number(value)
			}
			function formattedValue(value) {
				const decimals = field.decimals === undefined
					? (integralValue ? 0 : Math.min(6, Math.max(1,
						String(fieldStep).indexOf(".") >= 0 ? String(fieldStep).split(".")[1].length : 1)))
					: Math.max(0, Math.min(6, Number(field.decimals)))
				return Number(value).toLocaleString(Qt.locale(), "f", decimals) + String(field.suffix || "")
			}
			function queueValue(value) {
				pendingValue = typedValue(value)
				pendingValueDirty = true
				rangeUpdateTimer.restart()
			}
			function flushValue() {
				if (!pendingValueDirty)
					return
				rangeUpdateTimer.stop()
				pendingValueDirty = false
				dialog.updateFieldValue(field.id, pendingValue)
			}
			width: parent ? parent.width : 0
			spacing: Theme.space1
			Timer {
				id: rangeUpdateTimer
				interval: 32
				repeat: false
				onTriggered: rangeRoot.flushValue()
			}
			RowLayout {
				Layout.fillWidth: true
				Label {
					Layout.fillWidth: true
					textFormat: Text.PlainText
					text: rangeRoot.field.label || ""
					color: Theme.textMuted
					font.pixelSize: 11
					elide: Text.ElideRight
				}
				Label {
					id: rangeValueLabel
					objectName: "dialogRangeValue_" + String((rangeRoot.field || {}).id || "")
					textFormat: Text.PlainText
					text: rangeRoot.formattedValue(rangeControl.value)
					color: rangeControl.enabled ? Theme.textStrong : Theme.textMuted
					font.pixelSize: Theme.fontLabel
					font.weight: Font.DemiBold
					Accessible.ignored: true
				}
			}
			ModernSlider {
				id: rangeControl
				objectName: "dialogField_" + String((rangeRoot.field || {}).id || "")
				Layout.fillWidth: true
				from: rangeRoot.minimumValue
				to: rangeRoot.maximumValue
				stepSize: rangeRoot.fieldStep
				value: Number(rangeRoot.field.value ?? rangeRoot.minimumValue)
				snapMode: Slider.SnapOnRelease
				live: true
				enabled: rangeRoot.field.enabled === undefined || Boolean(rangeRoot.field.enabled)
				Accessible.name: String(rangeRoot.field.label || "")
				Accessible.description: {
					const hint = String(rangeRoot.field.hint || rangeRoot.field.unavailableReason || "")
					const valueText = qsTr("%1; minimum %2; maximum %3")
						.arg(rangeRoot.formattedValue(value))
						.arg(rangeRoot.formattedValue(from))
						.arg(rangeRoot.formattedValue(to))
					return hint.length > 0 ? valueText + ". " + hint : valueText
				}
				onMoved: {
					if (pressed)
						rangeRoot.queueValue(value)
					else
						dialog.updateFieldValue(rangeRoot.field.id, rangeRoot.typedValue(value))
				}
				onPressedChanged: {
					if (!pressed)
						rangeRoot.flushValue()
				}
			}
			Label {
				Layout.fillWidth: true
				objectName: "dialogFieldHint_" + String((rangeRoot.field || {}).id || "")
				visible: String(rangeRoot.field.hint || rangeRoot.field.unavailableReason || "").length > 0
				textFormat: Text.PlainText
				text: String(rangeRoot.field.hint || rangeRoot.field.unavailableReason || "")
				color: Theme.textMuted
				font.pixelSize: 10
				wrapMode: Text.Wrap
				Accessible.ignored: true
			}
		}
	}
	Component {
		id: numberField
		ColumnLayout {
			id: numberRoot
			property var field: ({})
            width: parent ? parent.width : 0
			spacing: Theme.space1
			Label { textFormat: Text.PlainText; text: numberRoot.field.label || ""; color: Theme.textMuted; font.pixelSize: 11 }
            ModernSpinBox {
				id: numberControl
				objectName: "dialogField_" + String((numberRoot.field || {}).id || "")
				cursorPaintEnabled: !dialog.visualFixtureMode
                from: numberRoot.field.minimum ?? numberRoot.field.min ?? -100000
                to: numberRoot.field.maximum ?? numberRoot.field.max ?? 100000
				stepSize: Math.max(1, Number(numberRoot.field.step ?? numberRoot.field.stepSize ?? 1))
                value: Number(numberRoot.field.value || 0)
                editable: true
				enabled: numberRoot.field.enabled === undefined || Boolean(numberRoot.field.enabled)
				invalid: {
					dialogState.revision
					return dialogState.fieldError(String((numberRoot.field || {}).id || "")).length > 0
				}
				Accessible.name: String(numberRoot.field.label || "")
				Accessible.description: String(numberRoot.field.hint || numberRoot.field.unavailableReason || "")
				textFromValue: function(value, locale) {
					const fieldData = numberRoot.field || {}
					const useGrouping = fieldData.useGrouping === undefined
						? true : Boolean(fieldData.useGrouping)
					const numericText = useGrouping
						? Number(value).toLocaleString(locale, "f", 0)
						: String(Math.round(Number(value)))
					return numericText + String(fieldData.suffix || "")
				}
				valueFromText: function(text, locale) {
					const fieldData = numberRoot.field || {}
					let source = String(text)
					const suffix = String(fieldData.suffix || "")
					if (suffix.length > 0 && source.endsWith(suffix))
						source = source.slice(0, source.length - suffix.length)
					return Number.fromLocaleString(locale, source.trim())
				}
                onValueModified: dialog.updateFieldValue(numberRoot.field.id, value)
            }
			Label {
				Layout.fillWidth: true
				objectName: "dialogFieldHint_" + String((numberRoot.field || {}).id || "")
				visible: String(numberRoot.field.hint || numberRoot.field.unavailableReason || "").length > 0
				textFormat: Text.PlainText
				text: String(numberRoot.field.hint || numberRoot.field.unavailableReason || "")
				color: Theme.textMuted
				font.pixelSize: 10
				wrapMode: Text.Wrap
				Accessible.ignored: true
			}
        }
    }
    Component {
        id: textField
        ColumnLayout {
            id: textRoot
            property var field
            width: parent ? parent.width : 0
			function commitValue(restoreFocus) {
				const fieldId = String((field || {}).id || "")
				const nextValue = String(textInput.text)
				if (fieldId.length === 0 || nextValue === String((field || {}).value ?? ""))
					return false
				if (restoreFocus)
					dialog.armFocusRestore(textInput.objectName)
				dialog.updateFieldValue(fieldId, nextValue)
				return true
			}
			function handoffToResults(activateCurrent) {
				const resultListId = String((field || {}).resultListId || "")
				if (resultListId.length === 0) return false
				liveUpdateTimer.stop()
				commitValue(false)
				Qt.callLater(function() {
					dialog.focusResultListByFieldId(resultListId, activateCurrent, true)
				})
				return true
			}
			Timer {
				id: liveUpdateTimer
				interval: Math.max(80, Number((textRoot.field || {}).updateDelayMs || 180))
				repeat: false
				onTriggered: textRoot.commitValue(true)
			}
			Label { textFormat: Text.PlainText; text: textRoot.field.label || ""; color: Theme.textMuted; font.pixelSize: 11 }
			ModernTextField {
				id: textInput
				objectName: "dialogField_" + String((textRoot.field || {}).id || "")
                Layout.fillWidth: true
                text: String(textRoot.field.value ?? "")
				cursorDelegate: dialogTextCursorDelegate
                enabled: textRoot.field.enabled === undefined || textRoot.field.enabled
				placeholderText: String(textRoot.field.placeholder || "")
				Accessible.name: String(textRoot.field.label || "")
				Accessible.description: String(textRoot.field.hint || textRoot.field.unavailableReason || "")
				invalid: {
					dialogState.revision
					return dialogState.fieldError(String((textRoot.field || {}).id || "")).length > 0
				}
                echoMode: textRoot.field.type === "password" ? TextInput.Password : TextInput.Normal
				onTextEdited: {
					if ((textRoot.field || {}).liveUpdate === true)
						liveUpdateTimer.restart()
				}
				onEditingFinished: {
					liveUpdateTimer.stop()
					textRoot.commitValue(false)
				}
				Keys.onPressed: event => {
					if (event.key === Qt.Key_Down) {
						event.accepted = textRoot.handoffToResults(false)
					} else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
						event.accepted = textRoot.handoffToResults(true)
					}
				}
            }
			Label {
				Layout.fillWidth: true
				objectName: "dialogFieldHint_" + String((textRoot.field || {}).id || "")
				visible: String(textRoot.field.hint || textRoot.field.unavailableReason || "").length > 0
				textFormat: Text.PlainText
				text: String(textRoot.field.hint || textRoot.field.unavailableReason || "")
				color: Theme.textMuted
				font.pixelSize: 10
				wrapMode: Text.Wrap
				Accessible.ignored: true
			}
        }
    }
    Component {
        id: pathField
        ColumnLayout {
            id: pathRoot
            property var field
            width: parent ? parent.width : 0
			spacing: Theme.space1
			Label { textFormat: Text.PlainText; text: pathRoot.field.label || ""; color: Theme.textMuted; font.pixelSize: 11 }
            RowLayout {
				ModernTextField {
					objectName: "dialogField_" + String((pathRoot.field || {}).id || "")
                    Layout.fillWidth: true
                    text: String(pathRoot.field.value ?? "")
					cursorDelegate: dialogTextCursorDelegate
					enabled: pathRoot.field.enabled === undefined || Boolean(pathRoot.field.enabled)
					placeholderText: String(pathRoot.field.placeholder || "")
					Accessible.name: String(pathRoot.field.label || "")
					Accessible.description: String(pathRoot.field.hint || pathRoot.field.unavailableReason || "")
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
					objectName: "dialogBrowse_" + String((pathRoot.field || {}).id || "")
                    text: pathRoot.field.browseLabel || qsTr("Browse…")
					enabled: pathRoot.field.enabled === undefined || Boolean(pathRoot.field.enabled)
					visible: String(pathRoot.field.browseActionId || "").length > 0
					Accessible.description: String(pathRoot.field.hint || pathRoot.field.unavailableReason || "")
					onClicked: {
						dialog.invokeNativePickerAction(pathRoot.field.browseActionId,
							{ "fieldId": pathRoot.field.id }, objectName)
					}
                }
            }
			Label {
				Layout.fillWidth: true
				objectName: "dialogFieldHint_" + String((pathRoot.field || {}).id || "")
				visible: String(pathRoot.field.hint || pathRoot.field.unavailableReason || "").length > 0
				textFormat: Text.PlainText
				text: String(pathRoot.field.hint || pathRoot.field.unavailableReason || "")
				color: Theme.textMuted
				font.pixelSize: 10
				wrapMode: Text.Wrap
				Accessible.ignored: true
			}
        }
    }
	Component {
		id: motdEditorField
		MotdEditor {
			dialogHost: dialog
			dialogStateHost: dialogState
		}
	}
	Component {
		id: imageField
		ColumnLayout {
			id: imageRoot
			property var field
			readonly property string requestedPreviewSource: String(
				field.previewSource || field.previewUrl || field.imageUrl || field.value || "")
			readonly property string previewSource: dialog.safeDialogImageSource(requestedPreviewSource)
			readonly property bool hasImage: previewSource.length > 0
			readonly property bool previewDecodeFailed: hasImage && imagePreview.status === Image.Error
			readonly property string localError: requestedPreviewSource.length > 0 && !hasImage
				? qsTr("This image preview is not from an approved local source.")
				: previewDecodeFailed ? qsTr("This image preview could not be decoded.")
				: String(field.error || field.errorMessage || "")
			width: parent ? parent.width : 0
			spacing: Theme.space2
			Label {
				Layout.fillWidth: true
				textFormat: Text.PlainText
				text: imageRoot.field.label || ""
				color: Theme.textMuted
				font.pixelSize: 11
			}
			RowLayout {
				Layout.fillWidth: true
				spacing: Theme.space3
				Rectangle {
					id: imagePreviewFrame
					objectName: "dialogImagePreview_" + String((imageRoot.field || {}).id || "")
					Layout.preferredWidth: 88
					Layout.preferredHeight: 88
					radius: Theme.innerRadius
					color: Theme.strip
					border.color: imageRoot.localError.length > 0 ? Theme.danger : Theme.divider
					border.width: imageRoot.localError.length > 0 ? Theme.focusRingWidth : 1
					clip: true
					Accessible.role: Accessible.Graphic
					Accessible.name: imageRoot.hasImage
						? qsTr("Preview of %1").arg(imageRoot.field.label || qsTr("image"))
						: qsTr("No image selected")
					Accessible.description: imageRoot.localError
					Image {
						id: imagePreview
						objectName: "dialogImagePreviewContent_" + String((imageRoot.field || {}).id || "")
						anchors.fill: parent
						source: imageRoot.previewSource
						visible: imageRoot.hasImage
						asynchronous: true
						cache: false
						sourceSize: Qt.size(width * Screen.devicePixelRatio, height * Screen.devicePixelRatio)
						fillMode: Image.PreserveAspectCrop
						Accessible.ignored: true
					}
					ModernIcon {
						anchors.centerIn: parent
						visible: !imageRoot.hasImage
						name: "attach"
						color: Theme.textMuted
						size: 28
						Accessible.ignored: true
					}
				}
				ColumnLayout {
					Layout.fillWidth: true
					spacing: Theme.space2
					Label {
						Layout.fillWidth: true
						textFormat: Text.PlainText
						text: imageRoot.hasImage ? qsTr("Image ready") : qsTr("No image selected")
						color: imageRoot.hasImage ? Theme.textMain : Theme.textMuted
						font.pixelSize: Theme.fontBody
						font.weight: imageRoot.hasImage ? Font.DemiBold : Font.Normal
					}
					Flow {
						Layout.fillWidth: true
						spacing: Theme.space2
						ModernButton {
							objectName: "dialogImageBrowse_" + String((imageRoot.field || {}).id || "")
							visible: String(imageRoot.field.browseActionId || "").length > 0
							enabled: imageRoot.field.enabled === undefined || Boolean(imageRoot.field.enabled)
							text: imageRoot.field.browseLabel || qsTr("Choose image…")
							tone: "secondary"
							Accessible.description: String(imageRoot.field.hint || "")
							onClicked: {
								dialog.invokeNativePickerAction(imageRoot.field.browseActionId,
									{ "fieldId": imageRoot.field.id }, objectName)
							}
						}
						ModernButton {
							objectName: "dialogImageRemove_" + String((imageRoot.field || {}).id || "")
							visible: imageRoot.hasImage
							enabled: imageRoot.field.enabled === undefined || Boolean(imageRoot.field.enabled)
							text: imageRoot.field.removeLabel || qsTr("Remove")
							tone: "danger"
							onClicked: {
								if (String(imageRoot.field.removeActionId || "").length > 0)
									dialogState.invokeAction(imageRoot.field.removeActionId,
										{ "fieldId": imageRoot.field.id })
								else
									dialog.updateFieldValue(imageRoot.field.id, "")
							}
						}
					}
				}
			}
			Label {
				id: imageValidationLabel
				objectName: "dialogImageValidation_" + String((imageRoot.field || {}).id || "")
				Layout.fillWidth: true
				visible: imageRoot.localError.length > 0
				textFormat: Text.PlainText
				text: imageRoot.localError
				color: Theme.danger
				font.pixelSize: 10
				wrapMode: Text.Wrap
				Accessible.role: Accessible.AlertMessage
				Accessible.name: text
			}
			Label {
				Layout.fillWidth: true
				objectName: "dialogFieldHint_" + String((imageRoot.field || {}).id || "")
				visible: String(imageRoot.field.hint || imageRoot.field.unavailableReason || "").length > 0
				textFormat: Text.PlainText
				text: String(imageRoot.field.hint || imageRoot.field.unavailableReason || "")
				color: Theme.textMuted
				font.pixelSize: 10
				wrapMode: Text.Wrap
				Accessible.ignored: true
			}
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
				onAboutToShow: dialog.rememberNestedModalFocus(colorButton)
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
					dialog.restoreNestedModalFocus(colorButton)
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
							cursorDelegate: dialogTextCursorDelegate
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
				MiddleDragScrollHandler {
					targetFlickable: resultList
					horizontalEnabled: false
				}
				currentIndex: count > 0 ? 0 : -1
				activeFocusOnTab: activeFocus || visible
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
				function activateCurrent() {
					if (currentIndex < 0 || currentIndex >= count) return false
					resultRoot.invokePrimary(resultRoot.resultItems[currentIndex])
					return true
				}
				onCountChanged: {
					if (count === 0) currentIndex = -1
					else if (currentIndex < 0 || currentIndex >= count) currentIndex = 0
				}
				Keys.onPressed: event => {
					if (event.key === Qt.Key_Up && currentIndex <= 0
							&& String(resultRoot.field.inputFieldId || "").length > 0) {
						dialog.applyFocusByObjectName(
							"dialogField_" + String(resultRoot.field.inputFieldId), true)
						event.accepted = true
					} else if (event.key === Qt.Key_Home && count > 0) {
						currentIndex = 0
						positionViewAtBeginning()
						event.accepted = true
					} else if (event.key === Qt.Key_End && count > 0) {
						currentIndex = count - 1
						positionViewAtEnd()
						event.accepted = true
					} else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter
							|| event.key === Qt.Key_Space) && currentIndex >= 0) {
						activateCurrent()
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
						ToolTip.visible: hovered && String(modelData.hint || "").length > 0
						ToolTip.text: String(modelData.hint || "")
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
							ColumnLayout {
								Layout.fillWidth: true
								spacing: 0
								Label {
									Layout.fillWidth: true
									textFormat: Text.PlainText
									text: accentCard.modelData.label || String(accentCard.modelData.value)
									color: Theme.textStrong
									font.pixelSize: Theme.fontCaption
									font.bold: accentCard.selected
									elide: Text.ElideRight
								}
								Label {
									Layout.fillWidth: true
									objectName: "dialogAccentAutomaticTheme_"
										+ String(accentCard.modelData.value || accentCard.index)
									visible: !!accentCard.modelData.automatic
									textFormat: Text.PlainText
									text: String(accentCard.modelData.themeLabel || qsTr("Theme accent"))
									color: Theme.textMuted
									font.pixelSize: 8
									elide: Text.ElideRight
								}
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
			spacing: Theme.space1
			Label { textFormat: Text.PlainText; text: textareaRoot.field.label || ""; color: Theme.textMuted; font.pixelSize: 11 }
            ModernTextArea {
				objectName: "dialogField_" + String((textareaRoot.field || {}).id || "")
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(90, (textareaRoot.field.rows || 4) * 22)
                text: String(textareaRoot.field.value ?? "")
				cursorDelegate: dialogTextCursorDelegate
                enabled: textareaRoot.field.enabled === undefined || textareaRoot.field.enabled
				placeholderText: String(textareaRoot.field.placeholder || "")
				invalid: {
					dialogState.revision
					return dialogState.fieldError(String((textareaRoot.field || {}).id || "")).length > 0
				}
				Accessible.name: String(textareaRoot.field.label || "")
				Accessible.description: String(textareaRoot.field.hint || textareaRoot.field.unavailableReason || "")
                wrapMode: TextEdit.Wrap
				onActiveFocusChanged: {
					if (!activeFocus && text !== String(textareaRoot.field.value ?? ""))
						dialog.updateFieldValue(textareaRoot.field.id, text)
				}
            }
			Label {
				Layout.fillWidth: true
				objectName: "dialogFieldHint_" + String((textareaRoot.field || {}).id || "")
				visible: String(textareaRoot.field.hint || textareaRoot.field.unavailableReason || "").length > 0
				textFormat: Text.PlainText
				text: String(textareaRoot.field.hint || textareaRoot.field.unavailableReason || "")
				color: Theme.textMuted
				font.pixelSize: 10
				wrapMode: Text.Wrap
				Accessible.ignored: true
			}
        }
    }
	Component {
		id: aclEditorField
		AclEditor {
			visualFixtureMode: dialog.visualFixtureMode
			accessibilityViewport: dialogContentScroll
		}
	}
	Component {
		id: serverAdminEditorField
		ServerAdminEditor {
			visualFixtureMode: dialog.visualFixtureMode
			accessibilityViewport: dialogContentScroll
		}
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
		PluginEditor {
			animationsEnabled: !dialog.visualFixtureMode
			asyncOperationController: typeof operationModel !== "undefined" ? operationModel : null
			accessibilityViewport: dialogContentScroll
		}
    }
    Component {
        id: actionField
		ColumnLayout {
			id: actionRoot
			property var field
			width: parent ? parent.width : 0
			spacing: Theme.space1
			RowLayout {
				Layout.fillWidth: true
				spacing: Theme.space3
				Label {
					Layout.fillWidth: true
					visible: text.length > 0
					textFormat: Text.PlainText
					text: actionRoot.field.label || ""
					color: Theme.textMain
					font.pixelSize: Theme.fontBody
					wrapMode: Text.Wrap
				}
				ModernButton {
					objectName: "dialogField_" + String((actionRoot.field || {}).id || "")
					text: actionRoot.field.buttonLabel || actionRoot.field.text
						|| actionRoot.field.label || actionRoot.field.id
					tone: String(actionRoot.field.tone || "neutral")
					enabled: actionRoot.field.enabled === undefined || actionRoot.field.enabled
					Accessible.description: String(actionRoot.field.hint || actionRoot.field.unavailableReason || "")
					onClicked: dialogState.invokeAction(actionRoot.field.actionId || actionRoot.field.id,
						actionRoot.field.payload || {})
				}
			}
			Label {
				Layout.fillWidth: true
				objectName: "dialogFieldHint_" + String((actionRoot.field || {}).id || "")
				visible: String(actionRoot.field.hint || actionRoot.field.unavailableReason || "").length > 0
				textFormat: Text.PlainText
				text: String(actionRoot.field.hint || actionRoot.field.unavailableReason || "")
				color: Theme.textMuted
				font.pixelSize: 10
				wrapMode: Text.Wrap
				Accessible.ignored: true
			}
		}
    }
}
