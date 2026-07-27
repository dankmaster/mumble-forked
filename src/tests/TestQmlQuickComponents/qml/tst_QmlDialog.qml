import QtQuick
import QtQuick.Controls
import QtTest
import Mumble.Theme 1.0

TestCase {
    id: testCase
    name: "QmlDialog"
    when: windowShown
    width: 1000
    height: 760

    Loader {
        id: loader
        anchors.fill: parent
        source: "qrc:/qml-shell/QmlDialog.qml"
    }
	SignalSpy {
		id: dialogActionSpy
		target: dialogState
		signalName: "lastActionChanged"
	}

    function init() {
        verify(loader.item !== null);
		dialogActionSpy.clear();
		loader.item.visualFixtureMode = false;
        dialogState.setValidationError("name", "");
        dialogState.open = true;
        tryCompare(loader.item, "visible", true);
    }

    function cleanup() {
		testCase.height = 760;
		loader.item.beforeOpen = null;
		loader.item.visualFixtureMode = false;
		dialogState.setValidationError("hiddenAdvanced", "");
		dialogState.setValidationError("visibleField", "");
		dialogState.setValidationError("liveInvalid", "");
		dialogState.setValidationError("parent0", "");
		dialogState.setValidationError("audio.delay", "");
        dialogState.open = false;
        tryCompare(loader.item, "visible", false);
		dialogState.resetSections();
		dialogState.setSpecialState("generic", {});
    }

	function setInputEnhancementCalibrationState(state, overrides) {
		const field = {
			"id": "audio.inputEnhancementCalibration", "type": "inputEnhancementCalibration",
			"label": "Enhancement calibration",
			"inputEnhancementCalibrationState": state,
			"inputEnhancementCalibrationWorkerState": "idle",
			"inputEnhancementCalibrationStartActionId": "startInputEnhancementCalibration",
			"inputEnhancementCalibrationAdvanceActionId": "advanceInputEnhancementCalibration",
			"inputEnhancementCalibrationCancelActionId": "cancelInputEnhancementCalibration",
			"inputEnhancementCalibrationSkipNoiseActionId": "skipInputEnhancementCalibrationNoise",
			"inputEnhancementCalibrationEvaluateActionId": "evaluateInputEnhancementCalibration",
			"inputEnhancementCalibrationRefreshActionId": "refreshInputEnhancementCalibration",
			"inputEnhancementCalibrationSelectActionId": "selectInputEnhancementCalibration",
			"inputEnhancementCalibrationApplyActionId": "applyInputEnhancementCalibration",
			"inputEnhancementProbationUndoActionId": "undoInputEnhancementRollback",
			"replayStartActionId": "startVoiceReplay",
			"replayStopActionId": "stopVoiceReplay",
			"replayLabel": "Replay"
		};
		const additions = overrides || {};
		Object.keys(additions).forEach(function(key) { field[key] = additions[key]; });
		dialogState.setSections([{ "title": "Microphone processing", "fields": [field] }]);
	}

	function test_before_open_can_defer_modal_until_host_is_ready() {
		dialogState.open = false;
		tryCompare(loader.item, "visible", false);
		let attempts = 0;
		loader.item.beforeOpen = function() {
			attempts += 1;
			return false;
		};
		dialogState.open = true;
		tryCompare(loader.item, "visible", false);
		compare(attempts, 1);
		loader.item.beforeOpen = function() { return true; };
		loader.item.syncVisibility();
		tryCompare(loader.item, "visible", true);
	}

	function test_open_focus_and_close_action() {
		compare(loader.item.title, "Test dialog");
		compare(loader.item.header, null);
		compare(loader.item.footer, null);
		const titleLabel = findChild(loader.item.contentItem, "dialogTitleLabel");
		verify(titleLabel !== null);
		compare(titleLabel.text, "Test dialog");
		compare(titleLabel.Accessible.role, Accessible.Heading);
		compare(titleLabel.Accessible.name, "Test dialog");
		compare(loader.item.contentItem.Accessible.role, Accessible.Dialog);
		compare(loader.item.contentItem.Accessible.name, "Test dialog");
		compare(loader.item.palette.highlight, Theme.selected);
		compare(loader.item.palette.highlightedText, Theme.textStrong);
		compare(loader.item.palette.link, Theme.accent);
		compare(loader.item.palette.toolTipBase, Theme.surfaceRaised);
		compare(loader.item.palette.toolTipText, Theme.textStrong);
        tryVerify(function () {
            return loader.item.activeFocus;
        });
		const closeButton = findChild(loader.item.contentItem, "dialogCloseButton");
		verify(closeButton !== null);
		compare(closeButton.iconName, "close");
		compare(closeButton.text, "Close");
		const before = dialogState.closeRequests;
        mouseClick(closeButton);
        compare(dialogState.closeRequests, before + 1);
    }

    function test_field_validation_is_visible_and_clears() {
        dialogState.setValidationError("name", "Name is required");
        tryVerify(function () {
            const errorLabel = findChild(loader.item.contentItem, "dialogFieldError_name");
            return errorLabel !== null && errorLabel.visible && errorLabel.text === "Name is required";
        });
		const errorLabel = findChild(loader.item.contentItem, "dialogFieldError_name");
		compare(errorLabel.Accessible.role, Accessible.AlertMessage);
		compare(errorLabel.Accessible.name, "Name is required");
        dialogState.updateField("name", "Bob");
        tryVerify(function () {
            const errorLabel = findChild(loader.item.contentItem, "dialogFieldError_name");
            return errorLabel !== null && !errorLabel.visible;
        });
    }

	function test_validation_focuses_and_reveals_the_first_invalid_field() {
		const fields = [];
		for (let index = 0; index < 17; ++index) {
			fields.push({ "id": "validation" + index, "type": "text",
				"label": "Field " + index, "value": "Value " + index });
		}
		fields.push({ "id": "name", "type": "text", "label": "Name", "value": "" });
		dialogState.setSections([{ "title": "Validation", "fields": fields }]);
		dialogState.setSpecialState("generic", {
			"id": "validation-focus", "pages": [], "width": 720, "height": 380,
			"initialFocusId": "dialogCloseButton"
		});

		const scroll = findChild(loader.item.contentItem, "dialogContentScroll");
		const nameField = findChild(loader.item.contentItem, "dialogField_name");
		const closeButton = findChild(loader.item.contentItem, "dialogCloseButton");
		tryVerify(function() {
			return scroll !== null && nameField !== null && closeButton !== null
				&& scroll.contentHeight > scroll.height;
		});
		closeButton.forceActiveFocus();
		tryCompare(closeButton, "activeFocus", true);
		dialogState.setValidationError("name", "Name is required");
		tryCompare(nameField, "activeFocus", true);
		tryVerify(function() { return scroll.contentItem.contentY > 0; });
		dialogState.setValidationError("name", "");
	}

	function test_validation_skips_hidden_errors_and_reveals_advanced_when_needed() {
		dialogState.setValidationError("hiddenAdvanced", "Advanced value is required");
		dialogState.setValidationError("visibleField", "Visible value is required");
		dialogState.setSections([{ "title": "Validation", "fields": [
			{ "id": "hiddenAdvanced", "type": "text", "label": "Advanced", "value": "", "advanced": true },
			{ "id": "visibleField", "type": "text", "label": "Visible", "value": "" }
		] }]);
		dialogState.setSpecialState("generic", {
			"id": "validation-hidden", "pages": [], "width": 720, "height": 420,
			"initialFocusId": "dialogCloseButton"
		});
		const visibleField = findChild(loader.item.contentItem, "dialogField_visibleField");
		tryVerify(function() { return visibleField !== null && visibleField.activeFocus; });
		compare(loader.item.showAdvanced, false);

		dialogState.setValidationError("visibleField", "");
		let advancedField = null;
		tryVerify(function() {
			advancedField = findChild(loader.item.contentItem, "dialogField_hiddenAdvanced");
			return loader.item.showAdvanced && advancedField !== null && advancedField.activeFocus;
		});
		dialogState.setValidationError("hiddenAdvanced", "");
	}

	function test_live_validation_copy_does_not_steal_active_editor_focus() {
		dialogState.setSections([{ "title": "Live validation", "fields": [
			{ "id": "liveInvalid", "type": "text", "label": "Validated field", "value": "" },
			{ "id": "liveEditor", "type": "text", "label": "Active editor", "value": "typing" }
		] }]);
		dialogState.setSpecialState("generic", {
			"id": "live-validation-focus", "pages": [], "width": 720, "height": 420
		});
		const editor = findChild(loader.item.contentItem, "dialogField_liveEditor");
		tryVerify(function() { return editor !== null; });
		editor.forceActiveFocus();
		tryCompare(editor, "activeFocus", true);
		dialogState.setValidationError("liveInvalid", "A value is required");
		tryCompare(editor, "activeFocus", true);
		dialogState.setValidationError("liveInvalid", "Enter at least three characters");
		tryCompare(editor, "activeFocus", true);
		dialogState.setValidationError("liveInvalid", "");
	}

    function test_action_keyboard_activation() {
        const saveButton = findChild(loader.item.contentItem, "dialogAction_save");
        verify(saveButton !== null);
        saveButton.forceActiveFocus();
        tryCompare(saveButton, "activeFocus", true);
        keyClick(Qt.Key_Space);
        compare(dialogState.lastAction, "save");
    }

	function test_compact_layout_replaces_page_rail_and_wraps_actions() {
		dialogState.setSpecialState("generic", { "id": "compact", "width": 372, "height": 700 });
		tryCompare(loader.item, "width", 372);
		tryVerify(function() { return loader.item.compactDialogLayout; });
		const rail = findChild(loader.item.contentItem, "dialogPageRail");
		const compactBar = findChild(loader.item.contentItem, "dialogCompactPageBar");
		const selector = findChild(loader.item.contentItem, "dialogCompactPageSelector");
		const scroll = findChild(loader.item.contentItem, "dialogContentScroll");
		const footer = findChild(loader.item.contentItem, "dialogFooter");
		verify(rail !== null && !rail.visible);
		verify(compactBar !== null && compactBar.visible);
		verify(selector !== null && selector.visible);
		tryVerify(function() { return selector.width >= 300; });
		verify(scroll !== null && scroll.y >= compactBar.y + compactBar.height - 1);
		tryVerify(function() { return footer !== null && footer.height > 64; });
	}

	function test_nested_modal_owns_escape_before_the_parent_dialog() {
		const shortcut = findChild(loader.item, "dialogCancelShortcut");
		verify(shortcut !== null);
		const before = dialogState.closeRequests;
		loader.item.nestedModalOpen = true;
		compare(shortcut.enabled, false);
		keyClick(Qt.Key_Escape);
		compare(dialogState.closeRequests, before);

		loader.item.nestedModalOpen = false;
		compare(shortcut.enabled, true);
		keyClick(Qt.Key_Escape);
		tryCompare(dialogState, "closeRequests", before + 1);
	}

	function test_stonks_confirmation_owns_parent_modal_state_and_escape() {
		dialogState.setSections([]);
		dialogState.setSpecialState("stonks", {
			"id": "stonks-modal-host", "pages": [], "width": 820, "height": 700,
			"stonks": { "supported": true, "enabled": true, "registered": true,
				"canAdmin": false, "selfUserId": 1, "selectedUserId": 1, "snapshots": [] }
		});
		const stonksLoader = findChild(loader.item.contentItem, "stonksEditorLoader");
		tryVerify(function() { return stonksLoader !== null && stonksLoader.item !== null; });
		const editor = stonksLoader.item;
		const closeButton = findChild(loader.item.contentItem, "dialogCloseButton");
		const popup = findChild(editor, "stonksConfirmationPopup");
		const cancelButton = findChild(editor, "stonksConfirmCancel");
		const barrier = findChild(loader.item, "dialogNestedModalAccessibilityBarrier");
		verify(closeButton !== null && popup !== null && cancelButton !== null && barrier !== null);
		compare(editor.modalHost, loader.item);
		closeButton.forceActiveFocus();
		tryCompare(closeButton, "activeFocus", true);
		const closeRequestsBeforeConfirmation = dialogState.closeRequests;

		editor.requestDestructiveAction("clearPortfolio", { "userId": 1 },
			"Clear portfolio", "Clear this portfolio?");
		tryVerify(function() {
			return popup.opened && cancelButton.activeFocus && loader.item.nestedModalOpen
				&& barrier.active;
		});
		verify(loader.item.contentItem.Accessible.ignored);
		keyClick(Qt.Key_Escape);
		tryCompare(popup, "opened", false);
		compare(editor.pendingAction, "");
		compare(dialogState.closeRequests, closeRequestsBeforeConfirmation);
		tryVerify(function() {
			return !loader.item.nestedModalOpen && !barrier.active
				&& !loader.item.contentItem.Accessible.ignored;
		});
		tryCompare(closeButton, "activeFocus", true);
	}

	function test_page_rail_is_keyboard_accessible_at_medium_width() {
		dialogState.setSpecialState("settings", {
			"id": "medium", "width": 760, "height": 620,
			"pages": [
				{ "id": "general", "label": "Audio Input" },
				{ "id": "appearance", "label": "Appearance" }
			]
		});
		dialogState.invokeAction("selectPage", { "pageId": "general" });
		tryCompare(loader.item, "width", 760);
		verify(!loader.item.compactDialogLayout);
		const rail = findChild(loader.item.contentItem, "dialogPageRail");
		const pages = findChild(loader.item.contentItem, "dialogPageList");
		const compactBar = findChild(loader.item.contentItem, "dialogCompactPageBar");
		verify(rail !== null && rail.visible);
		verify(compactBar !== null && !compactBar.visible);
		verify(pages !== null);
		compare(pages.Accessible.role, Accessible.List);
		compare(pages.Accessible.name, "Settings pages");
		tryVerify(function() { return pages.itemAtIndex(0) !== null });
		const firstPage = pages.itemAtIndex(0);
		compare(firstPage.Accessible.role, Accessible.ListItem);
		tryVerify(function() { return firstPage.highlighted && firstPage.Accessible.selected; });
		compare(firstPage.background.color, Theme.selected);
		firstPage.forceActiveFocus();
		tryCompare(firstPage, "activeFocus", true);
		keyClick(Qt.Key_Return);
		compare(dialogState.lastAction, "selectPage");
		compare(dialogState.lastPayload.pageId, "general");
	}

	function test_settings_initial_focus_and_plugin_semantics_track_the_active_page() {
		dialogState.setSections([{ "id": "plugins", "title": "Plugins", "fields": [{
			"id": "plugins.editor", "type": "pluginEditor", "label": "Installed plugins",
			"rows": [{
				"id": "manual", "name": "Manual placement", "description": "Manual position tool",
				"version": "1.2.0", "author": "Mumble", "path": "Built in",
				"loaded": true, "enabled": true, "positionalAvailable": true,
				"positionalEnabled": true, "keyboardMonitoringAllowed": false,
				"canConfigure": true, "canShowAbout": true, "builtIn": true
			}]
		}] }]);
		dialogState.setSpecialState("settings", {
			"id": "settings-plugin-focus", "width": 920, "height": 700,
			"pages": [
				{ "id": "audioInput", "label": "Audio Input" },
				{ "id": "plugins", "label": "Plugins" }
			]
		});
		dialogState.invokeAction("selectPage", { "pageId": "plugins" });

		const pages = findChild(loader.item.contentItem, "dialogPageList");
		tryVerify(function() {
			return pages !== null && pages.visible && pages.activeFocus
				&& pages.currentIndex === 1 && pages.itemAtIndex(1) !== null;
		});
		const pluginPage = pages.itemAtIndex(1);
		compare(pluginPage.Accessible.name, "Plugins");
		compare(pluginPage.Accessible.selected, true);
		verify(pluginPage.current);

		const pluginCard = findChild(loader.item.contentItem, "pluginCard_manual");
		const semanticCard = findChild(loader.item.contentItem, "pluginSemanticCard_manual");
		const pluginList = findChild(loader.item.contentItem, "pluginList");
		tryVerify(function() {
			return pluginCard !== null && semanticCard !== null && pluginList !== null
				&& pluginCard.width > 0 && pluginCard.height > 0
				&& pluginCard.accessibilityExposed && !semanticCard.Accessible.ignored;
		}, 5000, "The visible plugin card must enter the Settings accessibility tree after layout settles");
		compare(pluginList.Accessible.role, Accessible.List);
		compare(pluginList.Accessible.name, "Installed plugins");
		compare(semanticCard.Accessible.name, "Manual placement");
		verify(findChild(pluginCard, "pluginConfigure_manual") !== null);
		verify(findChild(pluginCard, "pluginAbout_manual") !== null);
	}

	function test_page_rail_keeps_tab_focus_valid_while_pages_are_replaced() {
		dialogState.setSpecialState("settings", {
			"id": "settings-focus-transition", "width": 760, "height": 620,
			"pages": [
				{ "id": "general", "label": "Audio Input" },
				{ "id": "appearance", "label": "Appearance" }
			]
		});
		const pages = findChild(loader.item.contentItem, "dialogPageList");
		verify(pages !== null);
		pages.forceActiveFocus();
		tryCompare(pages, "activeFocus", true);

		dialogState.setSpecialState("connect", { "id": "connect-focus-transition", "pages": [] });
		tryCompare(pages, "count", 0);
		verify(pages.activeFocusOnTab,
			"The current focus owner must remain tab-valid until focus is handed off");

		const closeButton = findChild(loader.item.contentItem, "dialogCloseButton");
		verify(closeButton !== null);
		closeButton.forceActiveFocus();
		tryCompare(closeButton, "activeFocus", true);
		tryCompare(pages, "activeFocusOnTab", false);
	}

	function test_wide_footer_places_primary_action_at_the_right_edge() {
		dialogState.setSpecialState("generic", {
			"id": "wide-actions", "width": 760, "height": 620,
			"primaryActionId": "save"
		});
		tryCompare(loader.item, "width", 760);
		tryVerify(function() {
			return findChild(loader.item.contentItem, "dialogAction_save") !== null
				&& findChild(loader.item.contentItem, "dialogAction_cancel") !== null;
		});
		const primary = findChild(loader.item.contentItem, "dialogAction_save");
		const cancel = findChild(loader.item.contentItem, "dialogAction_cancel");
		verify(primary.parent === cancel.parent);
		tryVerify(function() {
			return primary.mapToItem(loader.item.contentItem, 0, 0).x
				> cancel.mapToItem(loader.item.contentItem, 0, 0).x;
		}, 1000, "Expected the primary action to settle at the right edge after Row polish");
	}

	function test_settings_uses_icon_navigation_page_hierarchy_and_sticky_advanced_footer() {
		dialogState.setSections([{ "title": "Input device", "fields": [
			{ "id": "device", "type": "text", "label": "Device", "value": "Default" },
			{ "id": "expert", "type": "text", "label": "Expert", "value": "Hidden", "advanced": true }
		] }]);
		dialogState.setSpecialState("settings", {
			"id": "settings", "width": 920, "height": 700,
			"pages": [
				{ "id": "general", "label": "Audio Input" },
				{ "id": "appearance", "label": "Appearance" }
			],
			"primaryActionId": "save"
		});
		dialogState.invokeAction("selectPage", { "pageId": "general" });

		tryVerify(function() {
			const railHeading = findChild(loader.item.contentItem, "dialogPageRailHeading");
			const pageHeading = findChild(loader.item.contentItem, "dialogSettingsPageHeading");
			const pageTitle = findChild(loader.item.contentItem, "dialogSettingsPageTitle");
			const inputIcon = findChild(loader.item.contentItem, "dialogPageIcon_general");
			const appearanceIcon = findChild(loader.item.contentItem, "dialogPageIcon_appearance");
			const footerAdvanced = findChild(loader.item.contentItem, "dialogSettingsAdvancedFooter");
			const footerToggle = findChild(loader.item.contentItem, "dialogSettingsAdvancedToggle");
			return railHeading !== null && railHeading.visible
				&& pageHeading !== null && pageHeading.visible
				&& pageTitle !== null && pageTitle.text === "Audio Input"
				&& inputIcon !== null && appearanceIcon !== null
				&& footerAdvanced !== null && footerAdvanced.visible
				&& footerToggle !== null && footerToggle.visible;
		});
		const railHeading = findChild(loader.item.contentItem, "dialogPageRailHeading");
		const pageHeading = findChild(loader.item.contentItem, "dialogSettingsPageHeading");
		const pageTitle = findChild(loader.item.contentItem, "dialogSettingsPageTitle");
		const pageCategory = findChild(loader.item.contentItem, "dialogSettingsPageCategory");
		const inputIcon = findChild(loader.item.contentItem, "dialogPageIcon_general");
		const appearanceIcon = findChild(loader.item.contentItem, "dialogPageIcon_appearance");
		const headerAdvanced = findChild(loader.item.contentItem, "dialogAdvancedToggle");
		const footerAdvanced = findChild(loader.item.contentItem, "dialogSettingsAdvancedFooter");
		const footerToggle = findChild(loader.item.contentItem, "dialogSettingsAdvancedToggle");
		compare(railHeading.Accessible.role, Accessible.Heading);
		compare(pageTitle.Accessible.role, Accessible.Heading);
		compare(inputIcon.name, "microphone");
		compare(appearanceIcon.name, "eye");
		compare(pageCategory.text, "AUDIO");
		verify(headerAdvanced !== null && !headerAdvanced.visible);
		compare(footerToggle.tone, "secondary");

		const footer = findChild(loader.item.contentItem, "dialogFooter");
		const scroll = findChild(loader.item.contentItem, "dialogContentScroll");
		verify(footer !== null && scroll !== null);
		verify(scroll.y + scroll.height <= footer.y + 1,
			"The Settings action and advanced controls must stay in the sticky footer");
		compare(loader.item.showAdvanced, false);
		// Invoke the control signal directly here: the test window uses an
		// offscreen platform and pointer delivery to sticky popup footers is not
		// deterministic. ModernButton has separate pointer/keyboard coverage.
		footerToggle.clicked();
		compare(loader.item.showAdvanced, true);
		tryVerify(function() { return findChild(loader.item.contentItem, "dialogField_expert") !== null; });
	}

	function test_settings_hides_only_a_section_heading_that_duplicates_the_active_page() {
		dialogState.setSections([
			{ "id": "appearance", "title": "  Appearance  ", "fields": [
				{ "id": "theme", "type": "text", "label": "Theme", "value": "Dark" }
			] },
			{ "id": "behavior", "title": "Interface behavior", "fields": [
				{ "id": "density", "type": "text", "label": "Density", "value": "Comfortable" }
			] }
		]);
		dialogState.setSpecialState("settings", {
			"id": "settings-heading-dedup", "width": 920, "height": 700,
			"pages": [{ "id": "appearance", "label": "APPEARANCE" }]
		});

		const pageTitle = findChild(loader.item.contentItem, "dialogSettingsPageTitle");
		const appearanceSection = findChild(loader.item.contentItem, "dialogSection_appearance");
		const appearanceHeading = findChild(loader.item.contentItem, "dialogSectionHeading_appearance");
		const behaviorHeading = findChild(loader.item.contentItem, "dialogSectionHeading_behavior");
		tryVerify(function() {
			return pageTitle !== null && pageTitle.visible && pageTitle.text === "APPEARANCE"
				&& appearanceSection !== null && appearanceHeading !== null
				&& behaviorHeading !== null && behaviorHeading.visible;
		});
		verify(!appearanceHeading.visible,
			"A normalized section title must not repeat the active Settings page title");
		verify(appearanceHeading.Accessible.ignored);
		compare(appearanceSection.Accessible.role, Accessible.Pane);
		compare(appearanceSection.Accessible.name, "  Appearance  ");
		compare(behaviorHeading.Accessible.role, Accessible.Heading);
		compare(behaviorHeading.Accessible.name, "Interface behavior");
		verify(!behaviorHeading.Accessible.ignored);

		dialogState.setSpecialState("generic", { "id": "non-settings-heading" });
		tryVerify(function() {
			const genericHeading = findChild(loader.item.contentItem, "dialogSectionHeading_appearance");
			return genericHeading !== null && genericHeading.visible;
		});
		const genericHeading = findChild(loader.item.contentItem, "dialogSectionHeading_appearance");
		compare(genericHeading.Accessible.role, Accessible.Heading);
		verify(!genericHeading.Accessible.ignored);
	}

	function test_readonly_values_keep_a_paintable_font_family() {
		dialogState.setSections([{ "id": "about-runtime", "title": "Runtime", "fields": [
			{ "id": "about.version", "type": "readonly", "label": "Version", "value": "1.7.0" },
			{ "id": "about.commit", "type": "readonly", "label": "Commit", "value": "abc123", "monospace": true }
		] }]);
		dialogState.setSpecialState("generic", {
			"id": "readonly-font-contract", "width": 720, "height": 520
		});

		let versionValue = null;
		let commitValue = null;
		tryVerify(function() {
			versionValue = findChild(loader.item.contentItem, "dialogReadonlyValue_about.version");
			commitValue = findChild(loader.item.contentItem, "dialogReadonlyValue_about.commit");
			return versionValue !== null && commitValue !== null
				&& versionValue.text === "1.7.0" && commitValue.text === "abc123";
		});
		verify(versionValue.font.family.length > 0,
			"Readonly values must inherit a concrete application font on Windows");
		compare(versionValue.font.family, loader.item.font.family);
		compare(commitValue.font.family, "Consolas");
		verify(versionValue.implicitWidth > 0 && versionValue.implicitHeight > 0);
	}

	function test_initial_focus_scrolls_long_content_clear_of_footer() {
		const fields = [];
		for (let index = 0; index < 18; ++index) {
			fields.push({ "id": "field" + index, "type": "text",
				"label": "Field " + index, "value": "Value " + index,
				"hint": index === 17 ? "The complete focused field must clear the sticky footer." : "" });
		}
		dialogState.setSections([{ "title": "Long settings page", "fields": fields }]);
		dialogState.setSpecialState("generic", {
			"id": "long-content", "width": 760, "height": 420,
			"initialFocusId": "field17", "primaryActionId": "save"
		});
		const scroll = findChild(loader.item.contentItem, "dialogContentScroll");
		const scrollBar = findChild(loader.item.contentItem, "dialogContentScrollBar");
		const footer = findChild(loader.item.contentItem, "dialogFooter");
		const lastField = findChild(loader.item.contentItem, "dialogField_field17");
		const lastFieldContainer = findChild(loader.item.contentItem, "dialogFieldContainer_field17");
		tryVerify(function() { return scroll !== null && scrollBar !== null && footer !== null
			&& lastField !== null && lastFieldContainer !== null && lastField.activeFocus; });
		tryVerify(function() { return scroll.contentItem.contentY > 0; });
		tryVerify(function() { return scrollBar.size < 1 && scrollBar.opacity > 0; });
		compare(scrollBar.parent, scroll);
		tryVerify(function() {
			const scrollBarPoint = scrollBar.mapToItem(scroll, 0, 0);
			return scrollBarPoint.x >= scroll.width - scrollBar.width - 1
				&& Math.abs(scrollBarPoint.y) <= 1;
		}, 1000, "The dialog scrollbar must stay attached to the right edge of its viewport");
		tryVerify(function() {
			return scroll.mapToItem(loader.item.contentItem, 0, scroll.height).y <= footer.y + 1;
		}, 1000, "Scrollable dialog content must settle before the persistent action footer");
		tryVerify(function() {
			return lastField.mapToItem(loader.item.contentItem, 0, lastField.height).y <= footer.y;
		}, 1000, "Focused settings fields must settle above the persistent action footer");
		tryVerify(function() { return lastFieldContainer.accessibilityExposed; }, 1000,
			"Keyboard reveal must expose the complete focused field subtree to accessibility");
	}

	function test_initial_focus_returns_stable_named_control_for_composite_editor() {
		loader.item.visualFixtureMode = true;
		dialogState.setSections([{ "title": "Playback", "fields": [
			{ "id": "audio.jitterBuffer", "type": "number", "label": "Jitter buffer",
				"value": 2, "minimum": 0, "maximum": 10 }
		] }]);
		dialogState.setSpecialState("generic", {
			"id": "stable-initial-focus", "width": 720, "height": 420,
			"initialFocusId": "audio.jitterBuffer", "primaryActionId": "save"
		});
		const numberField = findChild(loader.item.contentItem,
			"dialogField_audio.jitterBuffer");
		tryVerify(function() { return numberField !== null; });

		compare(numberField.cursorPaintEnabled, false);
		compare(loader.item.applyInitialFocus(), "dialogField_audio.jitterBuffer");
		tryVerify(function() { return numberField.activeFocus; });
	}

	function test_identifier_number_field_does_not_insert_locale_grouping() {
		dialogState.setSections([{ "title": "Details", "fields": [
			{ "id": "port", "type": "number", "label": "Port", "value": 64738,
				"minimum": 1, "maximum": 65535, "useGrouping": false }
		] }]);
		dialogState.setSpecialState("generic", {
			"id": "ungrouped-port", "width": 560, "height": 360,
			"initialFocusId": "port", "primaryActionId": "connect"
		});
		const portField = findChild(loader.item.contentItem, "dialogField_port");
		tryVerify(function() { return portField !== null; });
		compare(portField.textFromValue(64738, Qt.locale("sv_SE")), "64738");
		compare(portField.displayText, "64738");
	}

	function test_compact_settings_page_can_opt_into_content_fit() {
		dialogState.setSections([
			{ "id": "behavior", "title": "Behavior", "fields": [
				{ "id": "share.autoOpen", "type": "checkbox", "label": "Auto-open shares", "value": false }
			] },
			{ "id": "capabilities", "title": "Capabilities", "fields": [
				{ "id": "share.note", "type": "note", "text": "Limits are negotiated with the server." }
			] }
		]);
		dialogState.setSpecialState("settings", {
			"id": "compact-screen-share", "width": 640, "height": 700,
			"pages": [{ "id": "screenShare", "label": "Screen Sharing", "contentFitCompact": true }],
			"primaryActionId": "save"
		});
		dialogState.invokeAction("selectPage", { "pageId": "screenShare" });

		const heading = findChild(loader.item.contentItem, "dialogSettingsPageHeading");
		const scroll = findChild(loader.item.contentItem, "dialogContentScroll");
		const footer = findChild(loader.item.contentItem, "dialogFooter");
		tryVerify(function() {
			return loader.item.compactDialogLayout && loader.item.compactContentFitPage
				&& heading !== null && heading.visible && scroll !== null && footer !== null
				&& loader.item.height < dialogState.preferredHeight;
		});
		verify(loader.item.height < 600,
			"A sparse compact Settings page must not reserve the full multi-page viewport");
		verify(scroll.mapToItem(loader.item.contentItem, 0, scroll.height).y <= footer.y + 1,
			"Content-fit Settings must keep the body above the sticky footer");
	}

	function test_runtime_focus_change_scrolls_control_clear_of_footer() {
		const fields = [];
		for (let index = 0; index < 18; ++index) {
			fields.push({ "id": "runtime" + index, "type": "text",
				"label": "Runtime field " + index, "value": "Value " + index });
		}
		dialogState.setSections([{ "title": "Runtime focus page", "fields": fields }]);
		dialogState.setSpecialState("generic", {
			"id": "runtime-focus-content", "width": 760, "height": 420,
			"initialFocusId": "runtime0", "primaryActionId": "save"
		});
		const scroll = findChild(loader.item.contentItem, "dialogContentScroll");
		const footer = findChild(loader.item.contentItem, "dialogFooter");
		const firstField = findChild(loader.item.contentItem, "dialogField_runtime0");
		const lastField = findChild(loader.item.contentItem, "dialogField_runtime17");
		tryVerify(function() {
			return scroll !== null && footer !== null && firstField !== null
				&& lastField !== null && firstField.activeFocus;
		});
		tryVerify(function() { return scroll.contentItem.contentY === 0; });

		lastField.forceActiveFocus(Qt.TabFocusReason);
		tryCompare(lastField, "activeFocus", true);
		tryVerify(function() { return scroll.contentItem.contentY > 0; });
		tryVerify(function() {
			return lastField.mapToItem(loader.item.contentItem, 0, lastField.height).y <= footer.y;
		}, 1000, "A newly focused body control must scroll above the sticky footer");
	}

	function test_scrolled_section_heading_leaves_and_reenters_accessibility_viewport() {
		const sections = [];
		for (let index = 0; index < 14; ++index) {
			sections.push({ "id": "viewport" + index, "title": "Viewport section " + index,
				"fields": [{ "id": "viewport-note" + index, "type": "note",
					"text": "Bounded content for section " + index }]
			});
		}
		dialogState.setSections(sections);
		dialogState.setSpecialState("generic", {
			"id": "heading-viewport", "pages": [], "width": 720, "height": 420,
			"initialFocusId": "dialogCloseButton"
		});

		const scroll = findChild(loader.item.contentItem, "dialogContentScroll");
		const firstHeading = findChild(loader.item.contentItem, "dialogSectionHeading_viewport0");
		tryVerify(function() {
			return scroll !== null && firstHeading !== null && firstHeading.visible
				&& scroll.contentHeight > scroll.height;
		});
		tryCompare(firstHeading.Accessible, "ignored", false);

		const headingPoint = firstHeading.mapToItem(scroll.contentItem, 0, 0);
		scroll.contentItem.contentY = Math.min(scroll.contentHeight - scroll.height,
			headingPoint.y + Math.max(1, firstHeading.height / 2));
		tryCompare(firstHeading.Accessible, "ignored", true);

		scroll.contentItem.contentY = 0;
		tryCompare(firstHeading.Accessible, "ignored", false);
	}

	function test_scrolled_field_subtree_leaves_and_reenters_accessibility_viewport() {
		const fields = [];
		for (let index = 0; index < 18; ++index) {
			fields.push({ "id": "viewport-field" + index, "type": "checkbox",
				"label": "Viewport option " + index, "value": index % 2 === 0 });
		}
		dialogState.setSections([{ "id": "field-viewport", "title": "Viewport fields",
			"fields": fields }]);
		dialogState.setSpecialState("generic", {
			"id": "field-viewport", "pages": [], "width": 720, "height": 420,
			"initialFocusId": "dialogCloseButton"
		});

		const scroll = findChild(loader.item.contentItem, "dialogContentScroll");
		const firstField = findChild(loader.item.contentItem, "dialogField_viewport-field0");
		const firstBarrier = findChild(loader.item.contentItem,
			"dialogFieldViewportAccessibilityBarrier_viewport-field0");
		tryVerify(function() {
			return scroll !== null && firstField !== null && firstBarrier !== null
				&& firstField.visible && scroll.contentHeight > scroll.height;
		});
		tryCompare(firstField.Accessible, "ignored", false);
		compare(firstBarrier.active, false);

		const fieldPoint = firstField.mapToItem(scroll.contentItem, 0, 0);
		scroll.contentItem.contentY = Math.min(scroll.contentHeight - scroll.height,
			fieldPoint.y + Math.max(1, firstField.height / 2));
		tryCompare(firstBarrier, "active", true);
		tryCompare(firstField.Accessible, "ignored", true);

		scroll.contentItem.contentY = 0;
		tryCompare(firstBarrier, "active", false);
		tryCompare(firstField.Accessible, "ignored", false);
	}

	function test_partially_visible_composite_editor_keeps_its_own_viewport_accessible() {
		// Give this geometry contract an explicitly bounded host viewport. Generic
		// single-page dialogs otherwise grow to their natural content height, so
		// whether the nested editor is partially visible depends on the preceding
		// test's final polish turn.
		testCase.height = 600;
		const rows = [];
		for (let index = 0; index < 12; ++index) {
			rows.push({ "type": index + 1, "name": "Event " + (index + 1),
				"console": index % 2 === 0, "notification": false,
				"highlight": false, "tts": false, "sound": false });
		}
		dialogState.setSections([{ "id": "event-section", "title": "Per-event behavior",
			"fields": [{ "id": "event-editor", "type": "messageEventEditor",
				"label": "Event behavior", "rows": rows }] }]);
		dialogState.setSpecialState("generic", {
			"id": "composite-viewport", "pages": [], "width": 720, "height": 420,
			"initialFocusId": "dialogCloseButton"
		});

		let scroll = null;
		let eventList = null;
		let barrier = null;
		tryVerify(function() {
			// Section replacement is asynchronous. Re-resolve the delegates on
			// each polish turn instead of retaining the outgoing, now-hidden
			// message editor from the preceding test.
			scroll = findChild(loader.item.contentItem, "dialogContentScroll");
			eventList = findChild(loader.item.contentItem, "messageEventList");
			barrier = findChild(loader.item.contentItem,
				"dialogFieldViewportAccessibilityBarrier_event-editor");
			return scroll !== null && eventList !== null && barrier !== null
				&& eventList.visible && eventList.height > scroll.height;
		});
		compare(barrier.active, false,
			"A composite editor owns its nested viewport and must remain semantic while intersecting the dialog body");
		tryCompare(eventList.Accessible, "ignored", false);
	}

	function test_external_initial_focus_does_not_scroll_dialog_content() {
		const fields = [];
		for (let index = 0; index < 18; ++index) {
			fields.push({ "id": "external" + index, "type": "text",
				"label": "Field " + index, "value": "Value " + index });
		}
		dialogState.setSections([{ "title": "Long external-focus page", "fields": fields }]);
		dialogState.setSpecialState("generic", {
			"id": "external-focus", "pages": [], "width": 760, "height": 420,
			"initialFocusId": "dialogCloseButton"
		});

		const scroll = findChild(loader.item.contentItem, "dialogContentScroll");
		const closeButton = findChild(loader.item.contentItem, "dialogCloseButton");
		tryVerify(function() {
			return scroll !== null && closeButton !== null && closeButton.activeFocus
				&& scroll.contentHeight > scroll.height;
		});
		compare(scroll.contentItem.contentY, 0);
		loader.item.ensureContentVisible(closeButton);
		compare(scroll.contentItem.contentY, 0,
			"A footer focus target must never be mapped into the scroll content viewport");
	}

	function test_raw_nested_initial_focus_object_name_precedes_field_shorthand() {
		dialogState.setSections([{ "id": "plugins", "title": "Plugins", "fields": [{
			"id": "plugins.editor", "type": "pluginEditor", "label": "Installed plugins",
			"rows": []
		}] }]);
		dialogState.setSpecialState("settings", {
			"id": "nested-plugin-focus", "pages": [], "width": 760, "height": 620,
			"initialFocusId": "pluginInstallButton"
		});

		const installButton = findChild(loader.item.contentItem, "pluginInstallButton");
		const closeButton = findChild(loader.item.contentItem, "dialogCloseButton");
		tryVerify(function() { return installButton !== null && closeButton !== null; });
		closeButton.forceActiveFocus();
		tryCompare(closeButton, "activeFocus", true);
		loader.item.applyInitialFocus();
		tryCompare(installButton, "activeFocus", true);
	}

	function test_explicit_focus_request_retargets_same_surface_once() {
		dialogState.setSections([{ "title": "Tokens", "fields": [
			{ "id": "token.0", "type": "text", "label": "Token 1", "value": "alpha" },
			{ "id": "token.1", "type": "text", "label": "Token 2", "value": "" }
		] }]);
		dialogState.setSpecialState("generic", {
			"id": "same-surface-focus-request", "width": 620, "height": 460,
			"initialFocusId": "dialogField_token.0"
		});
		const first = findChild(loader.item.contentItem, "dialogField_token.0");
		const second = findChild(loader.item.contentItem, "dialogField_token.1");
		tryVerify(function() { return first !== null && second !== null; });
		tryCompare(first, "activeFocus", true);

		dialogState.setSpecialState("generic", {
			"id": "same-surface-focus-request", "width": 620, "height": 460,
			"initialFocusId": "dialogField_token.1", "focusRequestId": "add-token:1"
		});
		tryCompare(second, "activeFocus", true);

		first.forceActiveFocus();
		tryCompare(first, "activeFocus", true);
		// A normal DTO refresh carrying the same request must not steal the caret.
		dialogState.setSpecialState("generic", {
			"id": "same-surface-focus-request", "width": 620, "height": 460,
			"initialFocusId": "dialogField_token.1", "focusRequestId": "add-token:1"
		});
		wait(0);
		compare(first.activeFocus, true);

		dialogState.setSpecialState("generic", {
			"id": "same-surface-focus-request", "width": 620, "height": 460,
			"initialFocusId": "dialogField_token.1", "focusRequestId": "add-token:2"
		});
		tryCompare(second, "activeFocus", true);
	}

	function test_visual_fixture_keeps_focus_without_a_nondeterministic_text_cursor() {
		const nameField = findChild(loader.item.contentItem, "dialogField_name");
		verify(nameField !== null);
		nameField.forceActiveFocus();
		tryCompare(nameField, "activeFocus", true);
		const cursorPaint = findChild(nameField, "dialogTextCursorPaint");
		verify(cursorPaint !== null);
		tryCompare(cursorPaint, "visible", true);

		loader.item.visualFixtureMode = true;
		tryCompare(nameField, "activeFocus", true);
		tryCompare(cursorPaint, "visible", false);
	}

	function test_single_surface_dialog_fits_sparse_content_and_grows_for_long_content() {
		dialogState.setSections([{ "title": "Status", "fields": [{
			"id": "status", "type": "note", "label": "Nothing to show yet"
		}] }]);
		dialogState.setSpecialState("generic", {
			"id": "sparse-content", "pages": [], "width": 720, "height": 700,
			"primaryActionId": "save"
		});
		tryVerify(function() { return !loader.item.stablePageViewport; });
		tryVerify(function() { return loader.item.height < 500; });
		verify(loader.item.height < dialogState.preferredHeight,
			"A sparse single-surface dialog must not reserve its full DTO height");

		const fields = [];
		for (let index = 0; index < 18; ++index) {
			fields.push({ "id": "long" + index, "type": "text",
				"label": "Field " + index, "value": "A value that keeps the row visible" });
		}
		dialogState.setSections([{ "title": "Long result", "fields": fields }]);
		dialogState.setSpecialState("generic", {
			"id": "long-single-surface", "pages": [], "width": 720, "height": 360,
			"primaryActionId": "save"
		});
		const scroll = findChild(loader.item.contentItem, "dialogContentScroll");
		const footer = findChild(loader.item.contentItem, "dialogFooter");
		const endPadding = findChild(loader.item.contentItem, "dialogContentEndPadding");
		tryVerify(function() {
			return scroll !== null && footer !== null && endPadding !== null
				&& loader.item.height > dialogState.preferredHeight
				&& scroll.contentHeight > scroll.height;
		});
		compare(Math.round(scroll.contentHeight), Math.round(endPadding.parent.implicitHeight));
		const flickable = scroll.contentItem;
		flickable.contentY = Math.max(0, flickable.contentHeight - flickable.height);
		wait(0);
		const endBottom = endPadding.mapToItem(loader.item.contentItem, 0, endPadding.height).y;
		verify(endBottom <= footer.y + 1,
			"The complete body content must remain reachable above the sticky footer");
	}

	function test_color_control_is_named_and_keyboard_focusable() {
		const colorButton = findChild(loader.item.contentItem, "dialogColorButton_accent");
		const closeButton = findChild(loader.item.contentItem, "dialogCloseButton");
		const barrier = findChild(loader.item, "dialogNestedModalAccessibilityBarrier");
		verify(colorButton !== null);
		verify(closeButton !== null && barrier !== null);
		compare(colorButton.Accessible.role, Accessible.Button);
		compare(colorButton.Accessible.name, "Choose Accent");
		// Let the dialog's deferred initial-focus pass finish before taking focus
		// explicitly for the color control state check.
		wait(0);
		colorButton.forceActiveFocus();
		tryCompare(colorButton, "activeFocus", true);
		tryCompare(colorButton.background.border, "color", Theme.focus, 500);

		const picker = colorButton.picker;
		verify(picker !== null);
		colorButton.clicked();
		tryCompare(picker, "visible", true);
		compare(loader.item.contentItem.enabled, false);
		tryVerify(function() {
			return loader.item.contentItem.Accessible.ignored
				&& colorButton.Accessible.ignored && closeButton.Accessible.ignored
				&& barrier.active;
		});
		compare(picker.palette.highlight, Theme.selected);
		compare(picker.palette.toolTipBase, Theme.surfaceRaised);
		compare(picker.palette.disabled.link, Theme.textMuted);
		const pickerDialog = findChild(picker, "dialogColorPickerDialog_accent");
		const hexInput = findChild(picker, "dialogColorHex_accent");
		const applyButton = findChild(picker, "dialogColorApply_accent");
		verify(pickerDialog !== null && hexInput !== null && applyButton !== null);
		compare(pickerDialog.Accessible.role, Accessible.Dialog);
		compare(pickerDialog.Accessible.name, "Choose Accent");
		compare(pickerDialog.Accessible.ignored, false);
		compare(hexInput.Accessible.ignored, false);
		tryCompare(hexInput, "activeFocus", true);

		picker.draftColor = "#12";
		tryCompare(applyButton, "enabled", false);
		picker.setDraft("#A1B2C3");
		compare(hexInput.text, "#A1B2C3");
		tryCompare(applyButton, "enabled", true);
		applyButton.clicked();
		tryCompare(picker, "visible", false);
		compare(loader.item.contentItem.enabled, true);
		tryVerify(function() {
			return !loader.item.contentItem.Accessible.ignored
				&& !colorButton.Accessible.ignored && !closeButton.Accessible.ignored
				&& !barrier.active;
		});
		tryCompare(colorButton, "activeFocus", true);
		compare(String(dialogState.fieldValue("accent")), "#A1B2C3");
	}

	function test_appearance_grids_render_overview_and_route_fast_selection() {
		dialogState.updateField("look.modernTheme", "dark")
		dialogState.updateField("look.modernAccent", "auto")
		dialogState.setSections([{ "title": "Appearance", "fields": [
			{
				"id": "look.modernTheme", "type": "select", "presentation": "themeGrid",
				"label": "Theme", "value": "dark", "options": [
					{ "value": "dark", "label": "Dark", "source": "builtIn", "preview": {
						"shell": "#20262f", "rail": "#1b2027", "strip": "#14181f",
						"panel": "#262d38", "surface": "#2e3742", "border": "#384453",
						"text": "#e7ecf3", "textMuted": "#8b94a3", "accent": "#5ec8b0",
						"success": "#5fd0a3", "warning": "#e0c574", "danger": "#e6736f"
					} },
					{ "value": "light", "label": "Light", "source": "builtIn", "preview": {
						"shell": "#f7f9fc", "rail": "#e6ebf3", "strip": "#d8e0eb",
						"panel": "#ffffff", "surface": "#e9eef6", "border": "#cfd7e0",
						"text": "#1f2937", "textMuted": "#647184", "accent": "#268f7f",
						"success": "#2f9d79", "warning": "#b96b2d", "danger": "#c75f5f"
					} }
				]
			},
			{
				"id": "look.modernAccent", "type": "select", "presentation": "accentGrid",
				"label": "Accent", "value": "auto", "options": [
					{ "value": "auto", "label": "Auto", "automatic": true,
						"themeLabel": "Dark", "hint": "Follows the selected theme",
						"swatch": { "accent": "#5ec8b0" } },
					{ "value": "blue", "label": "Blue", "swatch": { "accent": "#73b7ff" } }
				]
			},
			{
				"id": "look.modernClassicUserIcons", "type": "checkbox",
				"label": "Use classic user icons", "value": false
			},
			{
				"id": "look.modernRailSide", "type": "select", "presentation": "segmented",
				"label": "Rail side", "value": "right", "options": [
					{ "value": "left", "label": "Left" }, { "value": "right", "label": "Right" }
				]
			}
		] }])

		const overview = findChild(loader.item.contentItem, "appearanceThemeOverview")
		const darkCard = findChild(loader.item.contentItem, "dialogThemeOption_dark")
		const lightCard = findChild(loader.item.contentItem, "dialogThemeOption_light")
		const autoAccent = findChild(loader.item.contentItem, "dialogAccentOption_auto")
		const autoThemeLabel = findChild(loader.item.contentItem, "dialogAccentAutomaticTheme_auto")
		const blueAccent = findChild(loader.item.contentItem, "dialogAccentOption_blue")
		tryVerify(function() {
			return overview !== null && overview.visible && overview.height >= 120
				&& darkCard !== null && lightCard !== null && autoAccent !== null
				&& autoThemeLabel !== null && blueAccent !== null
		})
		compare(autoThemeLabel.text, "Dark")
		compare(darkCard.Accessible.role, Accessible.RadioButton)
		compare(darkCard.Accessible.checked, true)
		verify(lightCard.enabled)
		lightCard.forceActiveFocus()
		tryCompare(lightCard, "activeFocus", true)
		compare(lightCard.background.border.width, 2)
		lightCard.clicked()
		compare(dialogState.lastAction, "look.previewAppearance")
		compare(dialogState.lastPayload.fieldId, "look.modernTheme")
		compare(String(dialogState.lastPayload.value), "light")
		compare(String(dialogState.fieldValue("look.modernTheme")), "light")
		tryVerify(function() {
			const selectedLightCard = findChild(loader.item.contentItem, "dialogThemeOption_light")
			const liveOverview = findChild(loader.item.contentItem, "appearanceThemeOverview")
			return selectedLightCard !== null && selectedLightCard.Accessible.checked
				&& liveOverview !== null && String(liveOverview.color).toLowerCase() === "#f7f9fc"
		})
		const liveBlueAccent = findChild(loader.item.contentItem, "dialogAccentOption_blue")
		verify(liveBlueAccent !== null && liveBlueAccent.enabled)
		liveBlueAccent.forceActiveFocus()
		tryCompare(liveBlueAccent, "activeFocus", true)
		compare(liveBlueAccent.background.border.width, 2)
		liveBlueAccent.clicked()
		compare(dialogState.lastAction, "look.previewAppearance")
		compare(dialogState.lastPayload.fieldId, "look.modernAccent")
		compare(String(dialogState.lastPayload.value), "blue")
		compare(String(dialogState.fieldValue("look.modernAccent")), "blue")
		tryVerify(function() {
			const liveOverview = findChild(loader.item.contentItem, "appearanceThemeOverview")
			return liveOverview !== null
				&& String(liveOverview.border.color).toLowerCase() === "#73b7ff"
		})

		loader.item.updateFieldValue("look.modernRailSide", "left")
		compare(dialogState.lastAction, "look.previewAppearance")
		compare(dialogState.lastPayload.fieldId, "look.modernRailSide")
		compare(String(dialogState.lastPayload.value), "left")
		loader.item.updateFieldValue("look.modernClassicUserIcons", true)
		compare(dialogState.lastAction, "look.previewAppearance")
		compare(dialogState.lastPayload.fieldId, "look.modernClassicUserIcons")
		compare(dialogState.lastPayload.value, true)
	}

	function test_stonks_client_fields_route_immediate_settings_actions() {
		dialogState.setSections([{ "title": "Ticker strip", "fields": [{
			"id": "stonks.client.enabled", "type": "checkbox",
			"label": "Show the Stonks ticker", "value": false
		}] }])
		const tickerToggle = findChild(loader.item.contentItem, "dialogField_stonks.client.enabled")
		tryVerify(function() { return tickerToggle !== null && tickerToggle.visible })
		loader.item.updateFieldValue("stonks.client.enabled", true)
		compare(dialogState.lastAction, "stonks.updateClient")
		compare(dialogState.lastPayload.fieldId, "stonks.client.enabled")
		compare(dialogState.lastPayload.value, true)
	}

	function test_range_uses_slider_metadata_live_value_keyboard_and_typed_updates() {
		dialogState.setSections([{ "id": "levels", "title": "Levels", "fields": [{
			"id": "audio.level", "type": "range", "label": "Input level", "value": 40,
			"min": 0, "max": 100, "step": 5, "suffix": "%", "hint": "Adjust the input level"
		}, {
			"id": "audio.delay", "type": "number", "label": "Delay", "value": 30,
			"min": 0, "max": 500, "step": 10, "suffix": " ms", "hint": "Applied before playback"
		}]}])

		const slider = findChild(loader.item.contentItem, "dialogField_audio.level")
		const valueLabel = findChild(loader.item.contentItem, "dialogRangeValue_audio.level")
		const sliderHint = findChild(loader.item.contentItem, "dialogFieldHint_audio.level")
		const number = findChild(loader.item.contentItem, "dialogField_audio.delay")
		const numberHint = findChild(loader.item.contentItem, "dialogFieldHint_audio.delay")
		tryVerify(function() {
			return slider !== null && valueLabel !== null && sliderHint !== null
				&& number !== null && numberHint !== null
		})
		compare(slider.Accessible.role, Accessible.Slider)
		compare(slider.Accessible.name, "Input level")
		verify(String(slider.Accessible.description).indexOf("40%") >= 0)
		compare(slider.from, 0)
		compare(slider.to, 100)
		compare(slider.stepSize, 5)
		compare(slider.value, 40)
		compare(valueLabel.text, "40%")
		compare(sliderHint.text, "Adjust the input level")

		slider.forceActiveFocus()
		tryCompare(slider, "activeFocus", true)
		keyClick(Qt.Key_Right)
		tryCompare(slider, "value", 45)
		tryCompare(valueLabel, "text", "45%")
		tryVerify(function() { return dialogState.fieldValue("audio.level") === 45 })
		compare(typeof dialogState.fieldValue("audio.level"), "number")

		compare(number.stepSize, 10)
		compare(number.displayText, "30 ms")
		compare(number.Accessible.name, "Delay")
		compare(number.Accessible.description, "Applied before playback")
		compare(numberHint.text, "Applied before playback")
	}

	function test_segmented_presentation_is_bounded_keyboard_accessible_and_preserves_types() {
		dialogState.setSpecialState("generic", { "id": "segments", "width": 372, "height": 620 })
		dialogState.setSections([{ "id": "appearance", "title": "Appearance", "fields": [{
			"id": "layout.density", "type": "select", "presentation": "segmented",
			"label": "Density", "value": "compact", "hint": "Choose interface spacing",
			"options": [
				{ "label": "Compact interface", "value": "compact" },
				{ "label": "Comfortable interface", "value": "comfortable" },
				{ "label": "Extra spacious interface", "value": "spacious" }
			]
		}, {
			"id": "layout.columns", "type": "select", "presentation": "segmented",
			"label": "Columns", "value": 1,
			"options": [{ "label": "One", "value": 1 }, { "label": "Two", "value": 2 }]
		}]}])

		const density = findChild(loader.item.contentItem, "dialogField_layout.density")
		const compact = findChild(loader.item.contentItem, "dialogSegmentOption_layout.density_compact")
		const comfortable = findChild(loader.item.contentItem, "dialogSegmentOption_layout.density_comfortable")
		const spacious = findChild(loader.item.contentItem, "dialogSegmentOption_layout.density_spacious")
		const columns = findChild(loader.item.contentItem, "dialogField_layout.columns")
		const twoColumns = findChild(loader.item.contentItem, "dialogSegmentOption_layout.columns_2")
		tryVerify(function() {
			return density !== null && compact !== null && comfortable !== null && spacious !== null
				&& columns !== null && twoColumns !== null && density.width > 0
		})
		compare(density.Accessible.role, Accessible.Grouping)
		compare(density.Accessible.name, "Density")
		compare(density.Accessible.description, "Choose interface spacing")
		compare(compact.Accessible.role, Accessible.RadioButton)
		compare(compact.Accessible.name, "Compact interface")
		compare(compact.Accessible.checked, true)
		compare(comfortable.Accessible.checked, false)
		verify(spacious.x + spacious.width <= density.width,
			"Long segmented labels must remain bounded by the narrow field")
		verify(compact.width > 0 && comfortable.width > 0 && spacious.width > 0)
		verify(compact.contentItem.implicitWidth > compact.contentItem.width,
			"The narrow layout should elide long labels without changing their accessible names")

		density.forceActiveFocus()
		tryCompare(density, "activeFocus", true)
		keyClick(Qt.Key_Right)
		tryCompare(density, "currentIndex", 1)
		compare(density.currentValue, "comfortable")
		compare(dialogState.fieldValue("layout.density"), "comfortable")
		compare(typeof dialogState.fieldValue("layout.density"), "string")
		compare(comfortable.Accessible.checked, true)
		keyClick(Qt.Key_Right)
		compare(density.currentValue, "spacious")
		compare(dialogState.fieldValue("layout.density"), "spacious")

		mouseClick(twoColumns)
		compare(columns.currentValue, 2)
		compare(dialogState.fieldValue("layout.columns"), 2)
		compare(typeof dialogState.fieldValue("layout.columns"), "number")
		compare(twoColumns.Accessible.checked, true)
	}

	function test_field_hints_enabled_actions_and_image_picker_preserve_typed_presentation() {
		dialogState.setSections([{ "id": "behavior", "title": "Behavior", "fields": [
			{ "id": "feature.enabled", "type": "checkbox", "label": "Feature", "value": true,
				"hint": "Controls the feature" },
			{ "id": "profile.name", "type": "text", "label": "Name", "value": "Alice",
				"hint": "Shown to other users" },
			{ "id": "profile.bio", "type": "textarea", "label": "Bio", "value": "Hello",
				"hint": "A short introduction" },
			{ "id": "folder", "type": "pathPicker", "label": "Folder", "value": "C:/data",
				"browseActionId": "browseFolder", "browseLabel": "Choose", "enabled": false,
				"hint": "Stored locally" },
			{ "id": "refresh", "type": "action", "label": "Theme library", "buttonLabel": "Reload",
				"actionId": "reloadThemes", "tone": "warning", "hint": "Reload theme manifests" },
			{ "id": "server.image", "type": "imagePicker", "label": "Server image",
				"value": "managed-image-token", "previewSource": "image://mumble/dialog-image",
				"browseActionId": "browseServerImage", "browseLabel": "Choose image",
				"removeActionId": "removeServerImage", "removeLabel": "Clear image",
				"hint": "Square images work best" }
		]}])

		const checkbox = findChild(loader.item.contentItem, "dialogField_feature.enabled")
		const text = findChild(loader.item.contentItem, "dialogField_profile.name")
		const textarea = findChild(loader.item.contentItem, "dialogField_profile.bio")
		const path = findChild(loader.item.contentItem, "dialogField_folder")
		const browsePath = findChild(loader.item.contentItem, "dialogBrowse_folder")
		const action = findChild(loader.item.contentItem, "dialogField_refresh")
		const preview = findChild(loader.item.contentItem, "dialogImagePreview_server.image")
		const imageContent = findChild(loader.item.contentItem, "dialogImagePreviewContent_server.image")
		const browseImage = findChild(loader.item.contentItem, "dialogImageBrowse_server.image")
		const removeImage = findChild(loader.item.contentItem, "dialogImageRemove_server.image")
		tryVerify(function() {
			return checkbox !== null && text !== null && textarea !== null && path !== null
				&& browsePath !== null && action !== null && preview !== null && imageContent !== null
				&& browseImage !== null && removeImage !== null
		})
		compare(checkbox.Accessible.description, "Controls the feature")
		compare(text.Accessible.description, "Shown to other users")
		compare(textarea.Accessible.description, "A short introduction")
		verify(!path.enabled && !browsePath.enabled)
		compare(path.Accessible.description, "Stored locally")
		compare(action.text, "Reload")
		compare(action.tone, "warning")
		compare(action.Accessible.description, "Reload theme manifests")
		compare(preview.Accessible.role, Accessible.Graphic)
		compare(preview.Accessible.name, "Preview of Server image")
		compare(String(imageContent.source), "image://mumble/dialog-image")
		verify(removeImage.visible)
		browseImage.clicked()
		compare(dialogState.lastAction, "browseServerImage")
		compare(dialogState.lastPayload.fieldId, "server.image")
		removeImage.clicked()
		compare(dialogState.lastAction, "removeServerImage")
		compare(dialogState.lastPayload.fieldId, "server.image")

		dialogState.setSections([{ "id": "value-image", "fields": [{
			"id": "value.only.image", "type": "imagePicker", "label": "Existing server image",
			"value": "image://mumble/value-only"
		}]}])
		const valueImage = findChild(loader.item.contentItem, "dialogImagePreviewContent_value.only.image")
		tryVerify(function() { return valueImage !== null })
		compare(String(valueImage.source), "image://mumble/value-only")

		dialogState.setSections([{ "id": "invalid-image", "fields": [{
			"id": "unsafe.image", "type": "imagePicker", "label": "Unsafe image",
			"value": "token", "previewSource": "https://example.test/image.png"
		}]}])
		const invalidPreview = findChild(loader.item.contentItem, "dialogImagePreview_unsafe.image")
		const validation = findChild(loader.item.contentItem, "dialogImageValidation_unsafe.image")
		tryVerify(function() { return invalidPreview !== null && validation !== null && validation.visible })
		compare(validation.Accessible.role, Accessible.AlertMessage)
		verify(validation.text.length > 0)
		compare(invalidPreview.border.color, Theme.danger)
	}

	function test_motd_editor_switches_between_permission_aware_edit_and_read_only_modes() {
		dialogState.setSections([{ "id": "motd", "fields": [{
			"id": "motd.html", "type": "motdEditor", "label": "Message of the day",
			"value": "<h2>Welcome</h2>", "originalValue": "<h2>Welcome</h2>",
			"canEdit": true, "enabled": true, "showSaveAction": true,
			"maximumLength": 100000,
			"previewSourceHtml": "<h2>Welcome</h2>",
			"previewSummary": "Welcome",
			"previewBlocks": [{ "kind": "heading", "headingLevel": 2,
				"segments": [{ "text": "Welcome", "bold": true }],
				"plainText": "Welcome", "alignment": "center" }]
		}]}])
		let editor = null
		let preview = null
		let save = null
		tryVerify(function() {
			editor = findChild(loader.item.contentItem, "motdSourceEditor")
			preview = findChild(loader.item.contentItem, "motdLivePreview")
			save = findChild(loader.item.contentItem, "motdSaveButton")
			return editor !== null && preview !== null && save !== null
		})
		verify(!editor.readOnly)
		compare(editor.text, "<h2>Welcome</h2>")
		verify(save.visible)
		verify(findChild(loader.item.contentItem, "motdPreviewDocumentBody") !== null)
		editor.text = "<p>Updated</p>"
		tryVerify(function() { return dialogState.fieldValue("motd.html") === "<p>Updated</p>" })

		dialogState.setSections([{ "id": "motd", "fields": [{
			"id": "motd.html", "type": "motdEditor", "label": "Message of the day",
			"value": "<p>Read only</p>", "originalValue": "<p>Read only</p>",
			"canEdit": false, "enabled": false, "showSaveAction": false,
			"maximumLength": 100000,
			"previewSourceHtml": "<p>Read only</p>",
			"previewSummary": "Read only",
			"previewBlocks": [{ "kind": "paragraph",
				"segments": [{ "text": "Read only" }],
				"plainText": "Read only", "alignment": "left" }]
		}]}])
		tryVerify(function() {
			editor = findChild(loader.item.contentItem, "motdSourceEditor")
			save = findChild(loader.item.contentItem, "motdSaveButton")
			return editor !== null && editor.readOnly && save !== null && !save.visible
		})
	}

	function test_section_presentation_metadata_selects_tokenized_surfaces() {
		dialogState.setSections([
			{ "id": "list", "title": "List", "presentation": "list",
				"fields": [{ "type": "note", "text": "List item" }] },
			{ "id": "records", "title": "Records", "presentation": "records",
				"fields": [{ "type": "note", "text": "Record item" }] },
			{ "id": "form", "title": "Form", "presentation": "form",
				"fields": [{ "type": "note", "text": "Form item" }] }
		])
		const list = findChild(loader.item.contentItem, "dialogSection_list")
		const records = findChild(loader.item.contentItem, "dialogSection_records")
		const form = findChild(loader.item.contentItem, "dialogSection_form")
		tryVerify(function() { return list !== null && records !== null && form !== null })
		compare(list.Accessible.description, "list")
		compare(records.Accessible.description, "records")
		compare(form.Accessible.description, "form")
		compare(list.color, Theme.strip)
		compare(records.color, Theme.surfaceRaised)
		compare(form.color, Theme.panel)
	}

	function test_special_editor_state_stays_live() {
		dialogState.setSpecialState("stonks", { "stonks": { "status": "loading-marker" } });
		const editorLoader = findChild(loader.item.contentItem, "stonksEditorLoader");
		verify(editorLoader !== null);
		tryVerify(function() {
			return editorLoader.item !== null && editorLoader.item.stonks.status === "loading-marker";
		});

		dialogState.setSpecialState("stonks", { "stonks": { "status": "ready-marker" } });
		tryVerify(function() { return editorLoader.item.stonks.status === "ready-marker"; });
	}

	function test_connect_favorites_selection_and_editor_rendering() {
		const favoriteState = selectedIndex => {
			const rows = [
				{ "id": "favorites:voice.example.test:64738", "sourceId": "favorites", "index": 0,
					"label": "Production", "subtitle": "voice.example.test:64738 / Alice",
					"selected": selectedIndex === 0, "usersValue": "5/20", "pingValue": "24 ms" },
				{ "id": "favorites:test.example.test:64738", "sourceId": "favorites", "index": 1,
					"label": "Testing", "subtitle": "test.example.test:64738 / Bob",
					"selected": selectedIndex === 1, "usersValue": "2/20", "pingValue": "31 ms" }
			];
			return {
				"id": "connect", "pages": [], "width": 860, "height": 640,
				"primaryActionId": "save", "initialFocusId": "connectFavoriteList",
				"selectedFavoriteIndex": selectedIndex, "selectedServerIndex": selectedIndex,
				"selectedServerId": rows[selectedIndex].id, "activeSource": "favorites", "filter": "",
				"editorOpen": false, "editorTitle": "Edit server", "favorites": rows, "sourceRows": rows,
				"sources": [
					{ "id": "favorites", "label": "Favorites", "status": "ready", "selected": true },
					{ "id": "public", "label": "Public", "status": "idle" },
					{ "id": "lan", "label": "LAN", "status": "idle" }
				]
			};
		};
		dialogState.setSpecialState("connect", favoriteState(0));
		const list = findChild(loader.item.contentItem, "connectFavoriteList");
		const surface = findChild(loader.item.contentItem, "connectFavoriteSurface");
		const connectEyebrow = findChild(loader.item.contentItem, "dialogProductEyebrow");
		const addServer = findChild(loader.item.contentItem, "connectNewFavoriteButton");
		verify(list !== null);
		verify(surface !== null);
		verify(connectEyebrow !== null && connectEyebrow.visible && connectEyebrow.text === "MUMBLE");
		verify(addServer !== null && addServer.tone === "secondary");
		tryCompare(list, "count", 2);
		const sourceSelector = findChild(loader.item.contentItem, "connectSourceSelector");
		verify(sourceSelector !== null && sourceSelector.optionCount === 3);
		tryVerify(function() { return surface.height < 320 && loader.item.height < 620; });
		tryCompare(list, "activeFocus", true);

		tryVerify(function() { return list.itemAtIndex(0) !== null && list.itemAtIndex(1) !== null; });
		const firstFavorite = list.itemAtIndex(0);
		const secondFavorite = list.itemAtIndex(1);
		const firstBackground = firstFavorite.background;
		const secondBackground = secondFavorite.background;
		verify(firstBackground !== null && secondBackground !== null);
		compare(firstFavorite.highlighted, true);
		compare(firstFavorite.Accessible.selected, true);
		verify(firstBackground.border.width > 1,
			"The current server must expose keyboard focus on its themed border");
		verify(String(firstBackground.color).toLowerCase() !== "#ffffff",
			"The selected server must never fall back to Qt's white highlight");
		verify(String(firstBackground.color) !== String(secondBackground.color),
			"Selected and idle server rows must use distinct theme surfaces");
		tryVerify(function() {
			return secondFavorite.visible
				&& secondFavorite.y >= list.contentY - 1
				&& secondFavorite.y + secondFavorite.height <= list.contentY + list.height + 1;
		});
		keyClick(Qt.Key_Down);
		compare(dialogState.lastAction, "selectServer");
		compare(dialogState.lastPayload.id, "favorites:test.example.test:64738");
		dialogState.setSpecialState("connect", favoriteState(1));
		tryCompare(list, "currentIndex", 1);
		tryVerify(function() {
			return list.itemAtIndex(0) !== null && list.itemAtIndex(1) !== null
				&& !list.itemAtIndex(0).highlighted && list.itemAtIndex(1).highlighted;
		});
		keyClick(Qt.Key_Space);
		compare(dialogState.lastAction, "selectServer");
		compare(dialogState.lastPayload.index, 1);
		keyClick(Qt.Key_Return);
		compare(dialogState.lastAction, "connectServer");
		compare(dialogState.lastPayload.index, 1);
		wait(0);
		const firstAfterKeyboard = list.itemAtIndex(0);
		verify(firstAfterKeyboard !== null);
		mouseClick(firstAfterKeyboard, 8, Math.round(firstAfterKeyboard.height / 2));
		compare(dialogState.lastAction, "selectServer");
		compare(dialogState.lastPayload.index, 0);
		compare(dialogState.lastPayload.sourceId, "favorites");
		compare(dialogState.lastPayload.edit, false);
		dialogState.setSpecialState("connect", favoriteState(0));
		tryCompare(list, "currentIndex", 0);
		tryVerify(function() {
			return list.itemAtIndex(0) !== null && list.itemAtIndex(1) !== null
				&& list.itemAtIndex(0).highlighted && !list.itemAtIndex(1).highlighted;
		});

		const connectAction = findChild(list.itemAtIndex(0), "connectFavoriteAction_0");
		compare(connectAction, null);
		const selectionIndicator = findChild(list.itemAtIndex(0), "connectFavoriteSelection_0");
		verify(selectionIndicator !== null);
		compare(selectionIndicator.name, "check");
		list.itemAtIndex(0).doubleClicked();
		compare(dialogState.lastAction, "connectServer");
		compare(dialogState.lastPayload.index, 0);

		dialogState.setSections([{ "title": "Details", "fields": [{
			"id": "host", "type": "text", "label": "Server address", "value": "voice.example.test"
		}] }]);
		dialogState.setSpecialState("connect", {
			"id": "connect", "pages": [], "width": 860, "height": 640,
			"initialFocusId": "dialogField_host",
			"selectedFavoriteIndex": 0, "editorOpen": true,
			"editorTitle": "Edit server", "favorites": [{ "index": 0, "label": "Production", "selected": true }]
		});
		const editorTitle = findChild(loader.item.contentItem, "connectEditorTitle");
		const editorSurface = findChild(loader.item.contentItem, "connectEditorSurface");
		const sourceBar = findChild(loader.item.contentItem, "connectSourceBar");
		const headerAction = findChild(loader.item.contentItem, "dialogCloseButton");
		const hostField = findChild(loader.item.contentItem, "dialogField_host");
		tryVerify(function() {
			return editorTitle !== null && editorTitle.visible && editorTitle.text === "Edit server"
				&& editorSurface !== null && editorSurface.visible
				&& sourceBar !== null && !sourceBar.visible
				&& !surface.visible && !list.visible && headerAction !== null
				&& hostField !== null && hostField.activeFocus;
		});
		compare(headerAction.iconName, "previous");
		compare(headerAction.text, "Back");
		compare(headerAction.Accessible.name, "Back to favorites");
		const actionCountBeforeHeader = dialogActionSpy.count;
		mouseClick(headerAction);
		compare(dialogState.lastAction, "backToFavorites");
		compare(dialogActionSpy.count, actionCountBeforeHeader + 1);
		hostField.forceActiveFocus();
		tryCompare(hostField, "activeFocus", true);
		keyClick(Qt.Key_Escape);
		compare(dialogState.lastAction, "backToFavorites");
		compare(dialogActionSpy.count, actionCountBeforeHeader + 2);
	}

	function test_connect_sources_expose_discovery_states_filter_keyboard_and_confirmations() {
		const sources = status => [
			{ "id": "favorites", "label": "Favorites", "status": "ready", "selected": false },
			{ "id": "public", "label": "Public", "status": status, "selected": true,
				"canCancel": status === "loading", "canRetry": status === "error",
				"error": status === "error" ? "Directory unavailable" : "" },
			{ "id": "lan", "label": "LAN", "status": "idle", "selected": false }
		];
		const publicRows = [
			{ "id": "public:se", "sourceId": "public", "index": 0, "label": "Stockholm",
				"subtitle": "se.example.test:64738 / Sweden", "usersValue": "12/64", "pingValue": "18 ms",
				"selected": true },
			{ "id": "public:de", "sourceId": "public", "index": 1, "label": "Berlin",
				"subtitle": "de.example.test:64738 / Germany", "usersValue": "5/64", "pingValue": "31 ms" }
		];
		const state = (status, rows, confirmation) => ({
			"id": "connect", "pages": [], "width": 860, "height": 640,
			"initialFocusId": "connectSourceSelector", "activeSource": "public", "filter": "",
			"selectedServerIndex": rows.length > 0 ? 0 : -1,
			"selectedServerId": rows.length > 0 ? rows[0].id : "",
			"editorOpen": false, "favorites": [], "sourceRows": rows,
			"sources": sources(status), "pendingConfirmation": confirmation || {}
		});

		dialogState.setSpecialState("connect", state("loading", [], {}));
		const selector = findChild(loader.item.contentItem, "connectSourceSelector");
		const statusPanel = findChild(loader.item.contentItem, "connectSourceStatus");
		let cancel = findChild(loader.item.contentItem, "connectSourceCancelButton");
		tryVerify(function() { return selector !== null && statusPanel !== null && statusPanel.visible
			&& cancel !== null && cancel.visible; });
		compare(selector.currentValue, "public");
		mouseClick(cancel);
		compare(dialogState.lastAction, "cancelSource");
		compare(dialogState.lastPayload.sourceId, "public");

		dialogState.setSpecialState("connect", state("error", [], {}));
		const retry = findChild(loader.item.contentItem, "connectSourceRetryButton");
		tryVerify(function() { return retry !== null && retry.visible; });
		compare(statusPanel.Accessible.role, Accessible.AlertMessage);
		mouseClick(retry);
		compare(dialogState.lastAction, "retrySource");
		compare(dialogState.lastPayload.sourceId, "public");

		dialogState.setSpecialState("connect", state("ready", publicRows, {}));
		const list = findChild(loader.item.contentItem, "connectFavoriteList");
		const filter = findChild(loader.item.contentItem, "connectServerFilter");
		tryVerify(function() { return list !== null && list.count === 2 && filter !== null; });
		filter.forceActiveFocus();
		tryCompare(filter, "activeFocus", true);
		keyClick(Qt.Key_Down);
		tryCompare(list, "activeFocus", true);
		keyClick(Qt.Key_Down);
		compare(dialogState.lastAction, "selectServer");
		compare(dialogState.lastPayload.id, "public:de");
		keyClick(Qt.Key_Return);
		compare(dialogState.lastAction, "connectServer");
		compare(dialogState.lastPayload.sourceId, "public");

		selector.forceActiveFocus();
		tryCompare(selector, "activeFocus", true);
		keyClick(Qt.Key_Right);
		compare(dialogState.lastAction, "selectSource");
		compare(dialogState.lastPayload.sourceId, "lan");

		const discard = {
			"kind": "discard", "title": "Discard server changes?", "message": "Unsaved details will be lost.",
			"confirmActionId": "confirmDiscardEditor", "confirmLabel": "Discard changes"
		};
		dialogState.setSpecialState("connect", state("ready", publicRows, discard));
		const confirmation = findChild(loader.item, "connectConfirmationPopup");
		const confirmationCancel = findChild(loader.item, "connectConfirmationCancel");
		const confirmationConfirm = findChild(loader.item, "connectConfirmationConfirm");
		const barrier = findChild(loader.item, "dialogNestedModalAccessibilityBarrier");
		tryVerify(function() { return confirmation !== null && confirmation.visible
			&& confirmationCancel !== null && confirmationCancel.activeFocus && confirmationConfirm !== null
			&& barrier !== null && barrier.active; });
		compare(loader.item.nestedModalOpen, true);
		verify(loader.item.contentItem.Accessible.ignored);
		verify(selector.Accessible.ignored);
		verify(!confirmation.contentItem.Accessible.ignored);
		verify(!confirmationCancel.Accessible.ignored);
		keyClick(Qt.Key_Tab);
		tryCompare(confirmationConfirm, "activeFocus", true);
		keyClick(Qt.Key_Tab);
		tryCompare(confirmationCancel, "activeFocus", true);
		keyClick(Qt.Key_Backtab);
		tryCompare(confirmationConfirm, "activeFocus", true);
		keyClick(Qt.Key_Backtab);
		tryCompare(confirmationCancel, "activeFocus", true);
		mouseClick(confirmationCancel);
		compare(dialogState.lastAction, "dismissConfirmation");
		dialogState.setSpecialState("connect", state("ready", publicRows, {}));
		tryCompare(confirmation, "visible", false);
		tryVerify(function() {
			return !loader.item.contentItem.Accessible.ignored && !selector.Accessible.ignored
				&& !barrier.active;
		});
		tryCompare(selector, "activeFocus", true);

		const remove = {
			"kind": "remove", "title": "Remove saved server?", "message": "This cannot be undone.",
			"confirmActionId": "confirmRemoveFavorite", "confirmLabel": "Remove server"
		};
		dialogState.setSpecialState("connect", state("ready", publicRows, remove));
		tryCompare(confirmation, "visible", true);
		mouseClick(confirmationConfirm);
		compare(dialogState.lastAction, "confirmRemoveFavorite");

		const privacyConsent = {
			"kind": "publicConsent", "title": "Enable public server discovery?",
			"message": "The registry can see your IP address.",
			"confirmActionId": "confirmEnablePublicSource", "confirmLabel": "Enable public servers",
			"cancelLabel": "Keep disabled", "confirmTone": "accent"
		};
		dialogState.setSpecialState("connect", state("loading", [], privacyConsent));
		tryCompare(confirmation, "visible", true);
		compare(confirmationCancel.text, "Keep disabled");
		compare(confirmationConfirm.text, "Enable public servers");
		compare(confirmationConfirm.tone, "accent");
		verify(findChild(loader.item, "connectConfirmationMessage").text.indexOf("IP address") >= 0);
		const closeRequestsBeforeConfirmationEscape = dialogState.closeRequests;
		keyClick(Qt.Key_Escape);
		compare(dialogState.lastAction, "dismissConfirmation");
		compare(dialogState.closeRequests, closeRequestsBeforeConfirmationEscape);
		dialogState.setSpecialState("connect", state("loading", [], {}));
		tryCompare(confirmation, "visible", false);

		dialogState.setSpecialState("connect", state("loading", [], privacyConsent));
		tryCompare(confirmation, "visible", true);
		mouseClick(confirmationConfirm);
		compare(dialogState.lastAction, "confirmEnablePublicSource");
	}

	function test_screen_share_selection_is_forwarded_to_native_validation() {
		dialogState.setSpecialState("screenShare", {
			"id": "screenShare", "primaryActionId": "screenShare.start",
			"screenShare": {
				"channelId": "7", "selectedSourceId": "",
				"sources": [{ "id": "screens", "section": "Screens", "items": [
					{ "id": "monitor:0", "title": "Screen 1", "thumbnail": "" }
				] }],
				"resolutionOptions": [{ "value": "1920x1080", "label": "1080p" }],
				"resolutionDefault": "1920x1080",
				"frameRateOptions": [{ "value": 30, "label": "30 FPS" }],
				"frameRateDefault": 30,
				"audioOptions": [{ "value": "", "label": "No audio" }],
				"audioDefault": ""
			}
		})
		const editorLoader = findChild(loader.item.contentItem, "screenShareEditorLoader")
		tryVerify(function() { return editorLoader !== null && editorLoader.item !== null })
		const source = findChild(editorLoader.item, "screenShareSource_monitor:0")
		verify(source !== null)
		source.selectSource()
		compare(dialogState.lastAction, "screenShare.selectSource")
		compare(dialogState.lastPayload.sourceId, "monitor:0")
	}

	function test_picker_action_restores_focus_after_native_round_trip() {
		dialogState.setSections([{ "id": "storage", "title": "Storage", "fields": [{
			"id": "folder", "type": "pathPicker", "label": "Folder", "value": "C:/data",
			"browseActionId": "browseFolder", "browseLabel": "Choose folder"
		}]}]);
		const pickerState = { "id": "picker-focus", "pages": [], "width": 720, "height": 460 };
		dialogState.setSpecialState("generic", pickerState);

		let browse = findChild(loader.item.contentItem, "dialogBrowse_folder");
		let closeButton = findChild(loader.item.contentItem, "dialogCloseButton");
		tryVerify(function() { return browse !== null && closeButton !== null && browse.visible; });
		browse.forceActiveFocus();
		tryCompare(browse, "activeFocus", true);
		browse.clicked();
		compare(dialogState.lastAction, "browseFolder");
		// A native picker selection can republish the dialog DTO before the
		// synchronous action returns. Reacquire the delegate and verify its stable
		// product ID receives focus rather than relying on the old QObject pointer.
		dialogState.setSpecialState("generic", Object.assign({ "roundTrip": 1 }, pickerState));
		tryVerify(function() {
			browse = findChild(loader.item.contentItem, "dialogBrowse_folder");
			return browse !== null && browse.activeFocus;
		});

		// A later refresh must not steal focus back when the user has already
		// selected a different named destination.
		browse.clicked();
		closeButton = findChild(loader.item.contentItem, "dialogCloseButton");
		closeButton.forceActiveFocus();
		tryCompare(closeButton, "activeFocus", true);
		dialogState.setSpecialState("generic", Object.assign({ "roundTrip": 2 }, pickerState));
		tryVerify(function() {
			closeButton = findChild(loader.item.contentItem, "dialogCloseButton");
			return closeButton !== null && closeButton.activeFocus;
		});
	}

	function test_settings_starts_on_navigation_and_resets_scroll_between_pages() {
		const fields = [];
		for (let index = 0; index < 18; ++index) {
			fields.push({ "id": "pageField" + index, "type": "text",
				"label": "Page field " + index, "value": "Value " + index });
		}
		dialogState.setSections([{ "title": "Long page", "fields": fields }]);
		dialogState.setSpecialState("settings", {
			"id": "settings-page-focus", "width": 920, "height": 520,
			"pages": [
				{ "id": "general", "label": "Audio Input" },
				{ "id": "appearance", "label": "Appearance" }
			],
			"primaryActionId": "save"
		});
		dialogState.invokeAction("selectPage", { "pageId": "general" });

		const pages = findChild(loader.item.contentItem, "dialogPageList");
		const scroll = findChild(loader.item.contentItem, "dialogContentScroll");
		tryVerify(function() {
			return pages !== null && pages.visible && pages.activeFocus
				&& scroll !== null && scroll.contentHeight > scroll.height;
		});
		keyClick(Qt.Key_Down);
		compare(pages.currentIndex, 1);
		keyClick(Qt.Key_Return);
		compare(dialogState.lastAction, "selectPage");
		compare(dialogState.lastPayload.pageId, "appearance");
		dialogState.invokeAction("selectPage", { "pageId": "general" });
		tryCompare(pages, "activeFocus", true);
		scroll.contentItem.contentY = Math.max(0, scroll.contentItem.contentHeight - scroll.height);
		verify(scroll.contentItem.contentY > 0);

		dialogState.invokeAction("selectPage", { "pageId": "appearance" });
		tryCompare(pages, "activeFocus", true);
		tryCompare(scroll.contentItem, "contentY", 0);

		dialogState.setSpecialState("settings", {
			"id": "settings-page-focus", "width": 640, "height": 520,
			"pages": [
				{ "id": "general", "label": "Audio Input" },
				{ "id": "appearance", "label": "Appearance" }
			],
			"primaryActionId": "save"
		});
		const compactSelector = findChild(loader.item.contentItem, "dialogCompactPageSelector");
		tryVerify(function() {
			return compactSelector !== null && compactSelector.visible && compactSelector.activeFocus;
		});
	}

	function test_transient_child_restores_parent_focus_and_scroll() {
		const parentFields = [];
		for (let index = 0; index < 18; ++index) {
			parentFields.push({ "id": "parent" + index, "type": "text",
				"label": "Parent field " + index, "value": "Value " + index });
		}
		const parentSections = [{ "title": "Parent settings", "fields": parentFields }];
		const parentState = {
			"id": "settings", "width": 920, "height": 500,
			"pages": [{ "id": "general", "label": "Audio Input" }],
			"primaryActionId": "save"
		};
		dialogState.setSections(parentSections);
		dialogState.setSpecialState("settings", parentState);
		dialogState.invokeAction("selectPage", { "pageId": "general" });

		const scroll = findChild(loader.item.contentItem, "dialogContentScroll");
		let parentField = findChild(loader.item.contentItem, "dialogField_parent17");
		tryVerify(function() {
			return scroll !== null && parentField !== null && scroll.contentHeight > scroll.height;
		});
		parentField.forceActiveFocus();
		tryCompare(parentField, "activeFocus", true);
		dialogState.setValidationError("parent0", "Parent value is invalid");
		tryCompare(parentField, "activeFocus", true);
		scroll.contentItem.contentY = Math.min(140,
			Math.max(0, scroll.contentItem.contentHeight - scroll.height));
		const savedContentY = scroll.contentItem.contentY;
		verify(savedContentY > 0);

		// Opening a different generic ID represents the controller's transient
		// child push and must snapshot the parent's presentation state.
		dialogState.setSpecialState("info", {
			"id": "pluginUpdateConfirm", "pages": [], "width": 620, "height": 360
		});
		dialogState.setSections([{ "title": "Plugin update", "fields": [{
			"id": "confirmation", "type": "note", "text": "Install update?"
		}]}]);
		const closeButton = findChild(loader.item.contentItem, "dialogCloseButton");
		closeButton.forceActiveFocus();
		tryCompare(closeButton, "activeFocus", true);

		// Simulate the controller pop: restore the exact parent DTO, then its ID.
		dialogState.setSections(parentSections);
		dialogState.setSpecialState("settings", parentState);
		parentField = findChild(loader.item.contentItem, "dialogField_parent17");
		tryVerify(function() { return parentField !== null && parentField.activeFocus; });
		tryVerify(function() {
			return Math.abs(scroll.contentItem.contentY - savedContentY) < 0.5;
		});
		dialogState.setValidationError("parent0", "");
	}

	function test_search_query_debounces_and_hands_keyboard_to_results() {
		dialogState.setSpecialState("form", {
			"id": "serverSearch", "pages": [], "width": 820, "height": 680,
			"initialFocusId": "search.query", "primaryActionId": "close"
		});
		dialogState.setSections([{ "title": "Search", "fields": [
			{
				"id": "search.query", "type": "text", "label": "Search", "value": "",
				"liveUpdate": true, "updateDelayMs": 80, "resultListId": "search.results"
			},
			{
				"id": "search.results", "type": "resultList", "label": "Results",
				"inputFieldId": "search.query", "items": [
					{ "stableId": "channel:7", "id": 7, "type": "channel", "label": "Lobby",
						"primaryActionId": "selectSearchResult" },
					{ "stableId": "user:11", "id": 11, "type": "user", "label": "Alex",
						"primaryActionId": "messageSearchResult" }
				]
			}
		] }]);

		const query = findChild(loader.item.contentItem, "dialogField_search.query");
		const results = findChild(loader.item.contentItem, "dialogResultList_search.results");
		tryVerify(function() { return query !== null && query.activeFocus && results !== null && results.count === 2; });
		keyClick(Qt.Key_L);
		keyClick(Qt.Key_O);
		keyClick(Qt.Key_B);
		tryVerify(function() { return String(dialogState.fieldValue("search.query")) === "lob"; }, 500);
		tryCompare(query, "activeFocus", true);

		keyClick(Qt.Key_Down);
		tryCompare(results, "activeFocus", true);
		compare(results.currentIndex, 0);
		keyClick(Qt.Key_Return);
		compare(dialogState.lastAction, "selectSearchResult");
		compare(dialogState.lastPayload.id, 7);
		compare(dialogState.lastPayload.type, "channel");

		keyClick(Qt.Key_Up);
		tryCompare(query, "activeFocus", true);
		keyClick(Qt.Key_Return);
		compare(dialogState.lastAction, "selectSearchResult");
		compare(dialogState.lastPayload.id, 7);
	}

	function test_certificate_uses_summary_cards_and_two_column_workflow() {
		dialogState.setSections([
			{ "id": "certificate-current", "title": "Current certificate", "fields": [
				{ "id": "cert.status", "type": "readonly", "label": "Status", "value": "A valid certificate is installed" },
				{ "id": "cert.name", "type": "readonly", "label": "Name", "value": "Alice" },
				{ "id": "cert.expires", "type": "readonly", "label": "Expires", "value": "2042-04-06" }
			] },
			{ "id": "certificate-action", "title": "Certificate action",
				"subtitle": "Choose one operation.", "fields": [
					{ "id": "cert.mode", "type": "select", "label": "Action", "value": "export",
						"options": [{ "value": "export", "label": "Export current certificate" }] },
					{ "id": "cert.exportPath", "type": "text", "label": "Export file", "value": "C:/cert.p12" }
				] }
		]);
		dialogState.setSpecialState("certificate", {
			"id": "certificate", "pages": [], "width": 820, "height": 680,
			"primaryActionId": "save",
			"highlights": [
				{ "label": "Status", "value": "Installed", "tone": "good" },
				{ "label": "Action", "value": "Export" },
				{ "label": "Expires", "value": "2042-04-06" }
			]
		});

		const highlights = findChild(loader.item.contentItem, "certificateHighlights");
		const certificateEyebrow = findChild(loader.item.contentItem, "dialogProductEyebrow");
		const statusCard = findChild(loader.item.contentItem, "certificateHighlight_0");
		const currentSection = findChild(loader.item.contentItem, "dialogSection_certificate-current");
		const actionSection = findChild(loader.item.contentItem, "dialogSection_certificate-action");
		tryVerify(function() {
			return highlights !== null && highlights.visible
				&& certificateEyebrow !== null && certificateEyebrow.visible
				&& statusCard !== null && statusCard.visible
				&& currentSection !== null && actionSection !== null
				&& currentSection.width > 250 && actionSection.width > 250;
		});
		compare(highlights.Accessible.role, Accessible.Pane);
		compare(certificateEyebrow.text, "MUMBLE");
		compare(highlights.Accessible.name, "Certificate summary");
		compare(statusCard.Accessible.name, "Status: Installed");
		compare(currentSection.Accessible.role, Accessible.Pane);
		compare(currentSection.Accessible.name, "Current certificate");
		const certificateColumns = findChild(loader.item.contentItem, "dialogCertificateColumns");
		verify(certificateColumns !== null);
		let currentPoint = currentSection.mapToItem(certificateColumns, 0, 0);
		let actionPoint = actionSection.mapToItem(certificateColumns, 0, 0);
		verify(Math.abs(currentPoint.y - actionPoint.y) <= 1,
			"Certificate details and actions must share a two-column row");
		verify(actionPoint.x >= currentPoint.x + currentSection.width - 1,
			"The certificate workflow belongs beside the current certificate summary; current="
				+ currentPoint.x + "/" + currentSection.width + ", action="
				+ actionPoint.x + "/" + actionSection.width);

		dialogState.setSpecialState("certificate", {
			"id": "certificate-compact", "pages": [], "width": 620, "height": 680,
			"primaryActionId": "save", "highlights": [
				{ "label": "Status", "value": "Installed", "tone": "good" },
				{ "label": "Action", "value": "Export" },
				{ "label": "Expires", "value": "2042-04-06" }
			]
		});
		tryVerify(function() {
			currentPoint = currentSection.mapToItem(certificateColumns, 0, 0);
			actionPoint = actionSection.mapToItem(certificateColumns, 0, 0);
			return loader.item.compactDialogLayout
				&& actionPoint.y >= currentPoint.y + currentSection.height;
		});
	}

	function test_voice_meter_renders_live_level_and_routes_setup_actions() {
		dialogState.setSections([{ "title": "Voice activation", "fields": [{
			"id": "audio.inputMeter", "type": "voiceMeter", "label": "Current voice input",
			"value": { "available": true, "connected": true, "amplitude": 72, "signalToNoise": 48,
				"hybrid": 63, "transmitting": true, "peakCleanMicDb": -17 },
			"vadSource": 2, "silenceThreshold": 18, "speechThreshold": 62, "voiceHold": 37, "active": true,
			"staticMeter": false, "calibrationActionId": "finishAudioSetupWizard",
			"calibrationLabel": "Audio setup", "replayStartActionId": "startVoiceReplay",
			"replayStopActionId": "stopVoiceReplay", "replayLabel": "Replay"
		}]}]);
		const track = findChild(loader.item.contentItem, "voiceMeterTrack_audio.inputMeter");
		const fill = findChild(loader.item.contentItem, "voiceMeterFill_audio.inputMeter");
		const meter = findChild(loader.item.contentItem, "voiceMeter_audio.inputMeter");
		tryVerify(function() { return track !== null && fill !== null && track.width > 0; });
		compare(meter.meterValue, 63);
		tryVerify(function() { return Math.round((fill.width / track.width) * 100) === 63; });
		const structuralRevision = dialogState.revision
		verify(dialogState.updatePresentationFieldValue("audio.inputMeter", {
			"available": true, "connected": true, "amplitude": 28, "signalToNoise": 34,
			"hybrid": 31, "transmitting": false, "peakCleanMicDb": -42
		}))
		compare(dialogState.revision, structuralRevision)
		tryCompare(meter, "meterValue", 31)
		tryVerify(function() { return Math.round((fill.width / track.width) * 100) === 31; })

		// Screenshot fixtures retain the production field but intentionally ignore
		// the audio thread's presentation-only samples. The rendered meter must
		// return to the DTO value and remain unchanged across subsequent updates.
		loader.item.visualFixtureMode = true
		tryCompare(meter, "meterValue", 63)
		tryVerify(function() { return Math.round((fill.width / track.width) * 100) === 63; })
		const fixtureWidth = fill.width
		verify(dialogState.updatePresentationFieldValue("audio.inputMeter", {
			"available": true, "connected": true, "amplitude": 91, "signalToNoise": 89,
			"hybrid": 93, "transmitting": true, "peakCleanMicDb": -5
		}))
		wait(120)
		compare(meter.meterValue, 63)
		compare(fill.width, fixtureWidth)

		const setup = findChild(loader.item.contentItem, "voiceMeterCalibration_audio.inputMeter");
		const guidedSetup = findChild(loader.item.contentItem, "voiceActivationSetup");
		verify(setup !== null && setup.visible && guidedSetup !== null);
		mouseClick(setup);
		compare(guidedSetup.setupState, 1);
		guidedSetup.cancelSetup();
		guidedSetup.selectedMethod = 1;
		guidedSetup.suggestedStopThreshold = 18;
		guidedSetup.suggestedStartThreshold = 62;
		guidedSetup.applySuggestion();
		compare(dialogState.lastAction, "finishAudioSetupWizard");
		compare(dialogState.lastPayload.silenceThreshold, 18);
		compare(dialogState.lastPayload.speechThreshold, 62);
		compare(dialogState.lastPayload.voiceHold, 40);
		compare(dialogState.lastPayload.vadSource, 1);
		compare(dialogState.lastPayload.inputGateMode, 0);
		compare(Object.keys(dialogState.lastPayload).length, 5);

		const replay = findChild(loader.item.contentItem, "voiceMeterReplay_audio.inputMeter");
		verify(replay !== null && replay.visible);
		mouseClick(replay);
		compare(dialogState.lastAction, "startVoiceReplay");
		compare(dialogState.lastPayload.mode, "server");
	}

	function test_guided_voice_setup_recommends_detection_from_room_and_voice_samples() {
		dialogState.setSections([{ "title": "Voice detection", "fields": [{
			"id": "audio.inputMeter", "type": "voiceMeter", "label": "Current voice input",
			"value": { "available": true, "amplitude": 20, "signalToNoise": 10, "hybrid": 9 },
			"staticMeter": true, "calibrationActionId": "finishAudioSetupWizard"
		}]}]);
		const setup = findChild(loader.item.contentItem, "voiceActivationSetup");
		tryVerify(function() { return setup !== null; });

		setup.ambientPeaks = [18, 8, 7];
		setup.voiceSums = [520, 720, 500];
		setup.voicePeaks = [70, 88, 68];
		setup.voiceSamples = 10;
		setup.chooseMethod();
		compare(setup.suggestedMethod, 1);
		verify(setup.suggestedStartThreshold > setup.suggestedStopThreshold);

		setup.ambientPeaks = [12, 7, 6];
		setup.voiceSums = [680, 120, 100];
		setup.voicePeaks = [82, 18, 15];
		setup.voiceSamples = 10;
		setup.chooseMethod();
		compare(setup.suggestedMethod, 0);

		setup.ambientPeaks = [38, 34, 10];
		setup.voiceSums = [650, 540, 520];
		setup.voicePeaks = [80, 72, 70];
		setup.voiceSamples = 10;
		setup.chooseMethod();
		compare(setup.suggestedMethod, 2);

		setup.ambientSamples = [
			[18, 17, 18, 16, 95],
			[8, 7, 8, 6, 96],
			[7, 6, 7, 5, 94]
		];
		setup.ambientPeaks = [95, 96, 94];
		setup.voiceSums = [520, 720, 500];
		setup.voicePeaks = [70, 88, 68];
		setup.voiceSamples = 10;
		setup.chooseMethod();
		compare(setup.ambientBaseline(1), 8);
		compare(setup.suggestedMethod, 1);
		compare(setup.suggestedStopThreshold, 22);
		compare(setup.suggestedStartThreshold, 45);
		compare(setup.suggestedVoiceHold, 40);
	}

	function test_input_enhancement_blind_comparison_routes_only_opaque_tokens() {
		dialogState.setSections([{ "title": "Input enhancement", "fields": [{
			"id": "audio.inputEnhancementCalibration", "type": "inputEnhancementCalibration",
			"label": "Enhancement calibration",
			"inputEnhancementCalibrationState": 10,
			"inputEnhancementCalibrationPlaybackOptions": [
				{ "label": "A", "playbackToken": "18446744073709551001" },
				{ "label": "B", "playbackToken": "18446744073709551002" },
				{ "label": "C", "playbackToken": "18446744073709551003" },
				{ "label": "D", "playbackToken": "18446744073709551004" }
			],
			"inputEnhancementCalibrationSelectActionId": "selectInputEnhancementCalibration"
		}]}]);

		const comparison = findChild(loader.item.contentItem, "inputEnhancementCalibration");
		const playA = findChild(loader.item.contentItem, "inputEnhancementCalibrationPlayA");
		const playB = findChild(loader.item.contentItem, "inputEnhancementCalibrationPlayB");
		const playC = findChild(loader.item.contentItem, "inputEnhancementCalibrationPlayC");
		const playD = findChild(loader.item.contentItem, "inputEnhancementCalibrationPlayD");
		const preferB = findChild(loader.item.contentItem, "inputEnhancementCalibrationPreferB");
		const privacy = findChild(loader.item.contentItem, "inputEnhancementCalibrationPrivacy");
		tryVerify(function() {
			return comparison !== null && comparison.visible && playA !== null && playB !== null
				&& playC !== null && playD !== null
				&& preferB !== null && privacy !== null;
		});
		verify(privacy.text.indexOf("never sent to the server") >= 0);

		playA.clicked();
		compare(dialogState.lastAction, "playInputEnhancementCalibration");
		compare(String(dialogState.lastPayload.playbackToken), "18446744073709551001");
		compare(Object.keys(dialogState.lastPayload).length, 1);
		compare(playA.text, "Stop A");
		compare(comparison.playbackExpiryMs, 12000);
		comparison.expirePlaybackState();
		compare(dialogState.lastAction, "playInputEnhancementCalibration");
		compare(playA.text, "Play A");

		playA.clicked();
		compare(dialogState.lastAction, "playInputEnhancementCalibration");
		compare(playA.text, "Stop A");
		playA.clicked();
		compare(dialogState.lastAction, "stopInputEnhancementCalibrationPlayback");
		compare(playA.text, "Play A");

		playB.clicked();
		compare(dialogState.lastAction, "playInputEnhancementCalibration");
		compare(String(dialogState.lastPayload.playbackToken), "18446744073709551002");
		preferB.clicked();
		compare(dialogState.lastAction, "selectInputEnhancementCalibration");
		compare(String(dialogState.lastPayload.playbackToken), "18446744073709551002");
		compare(playB.text, "Play B");
		playD.clicked();
		compare(dialogState.lastAction, "playInputEnhancementCalibration");
		compare(String(dialogState.lastPayload.playbackToken), "18446744073709551004");
	}

	function test_input_enhancement_calibration_renders_every_runtime_state() {
		const expectedHeadings = [
			"Processing comparison", "Check microphone level", "Check microphone level",
			"Capture room sound", "Capture room sound", "Capture your voice", "Capture your voice",
			"Capture local noise", "Capture local noise", "Compare safe candidates", "Blind comparison",
			"Selection ready", "Calibration applied", "Calibration cancelled", "Calibration stopped",
			"Calibration could not finish"
		];
		for (let state = 0; state <= 15; ++state) {
			const extras = {};
			if (state === 10 || state === 11) {
				extras.inputEnhancementCalibrationLeftPlaybackToken = "90071992547410001";
				extras.inputEnhancementCalibrationRightPlaybackToken = "90071992547410002";
			}
			if (state === 15)
				extras.inputEnhancementCalibrationErrorText = "Safe test failure";
			setInputEnhancementCalibrationState(state, extras);
			tryVerify(function() {
				const calibration = findChild(loader.item.contentItem, "inputEnhancementCalibration");
				const heading = findChild(loader.item.contentItem, "inputEnhancementCalibrationHeading");
				const status = findChild(loader.item.contentItem, "inputEnhancementCalibrationStatus");
				return calibration !== null && calibration.visible && heading !== null && status !== null
					&& heading.text === expectedHeadings[state] && status.text.length > 0;
			});
		}

		setInputEnhancementCalibrationState(5);
		const readingPrompt = findChild(loader.item.contentItem, "inputEnhancementCalibrationReadingPrompt");
		const readingText = findChild(loader.item.contentItem, "inputEnhancementCalibrationReadingText");
		tryVerify(function() {
			return readingPrompt !== null && readingPrompt.visible && readingText !== null
				&& readingText.text.indexOf("five blue boxes") >= 0;
		});
		verify(readingText.text.indexOf("quick footsteps") >= 0);
	}

	function test_input_enhancement_calibration_routes_guided_actions_and_refresh() {
		setInputEnhancementCalibrationState(0);
		let calibration = findChild(loader.item.contentItem, "inputEnhancementCalibration");
		let optionalNoise = findChild(loader.item.contentItem, "inputEnhancementCalibrationOptionalNoise");
		let action = findChild(loader.item.contentItem, "inputEnhancementCalibrationStart");
		tryVerify(function() {
			return calibration !== null && calibration.visible && optionalNoise !== null && optionalNoise.visible
				&& action !== null && action.visible;
		});
		calibration.captureOptionalLocalNoise = true;
		tryCompare(optionalNoise, "checked", true);
		action.clicked();
		compare(dialogState.lastAction, "startInputEnhancementCalibration");
		compare(dialogState.lastPayload.captureOptionalLocalNoise, true);
		compare(Object.keys(dialogState.lastPayload).length, 1);

		setInputEnhancementCalibrationState(1);
		calibration = findChild(loader.item.contentItem, "inputEnhancementCalibration");
		action = findChild(loader.item.contentItem, "inputEnhancementCalibrationCancel");
		tryVerify(function() { return calibration !== null && calibration.refreshTimerRunning && action !== null; });
		calibration.requestRefresh();
		compare(dialogState.lastAction, "refreshInputEnhancementCalibration");
		action.clicked();
		compare(dialogState.lastAction, "cancelInputEnhancementCalibration");

		setInputEnhancementCalibrationState(2, {
			"inputEnhancementCalibrationLevelStatus": 2,
			"inputEnhancementCalibrationLevelPeakPercent": 24,
			"inputEnhancementCalibrationLevelRmsPercent": 8
		});
		action = findChild(loader.item.contentItem, "inputEnhancementCalibrationAdvance");
		tryVerify(function() { return action !== null && action.visible && action.text === "Retry level check"; });
		action.clicked();
		compare(dialogState.lastAction, "advanceInputEnhancementCalibration");

		setInputEnhancementCalibrationState(7);
		action = findChild(loader.item.contentItem, "inputEnhancementCalibrationSkipNoise");
		tryVerify(function() { return action !== null && action.visible; });
		action.clicked();
		compare(dialogState.lastAction, "skipInputEnhancementCalibrationNoise");

		setInputEnhancementCalibrationState(9, {
			"inputEnhancementCalibrationWorkerState": "idle",
			"inputEnhancementCalibrationProgress": 0
		});
		action = findChild(loader.item.contentItem, "inputEnhancementCalibrationEvaluate");
		tryVerify(function() { return action !== null && action.visible; });
		action.clicked();
		compare(dialogState.lastAction, "evaluateInputEnhancementCalibration");

		setInputEnhancementCalibrationState(9, {
			"inputEnhancementCalibrationWorkerState": "running",
			"inputEnhancementCalibrationProgress": 45,
			"inputEnhancementCalibrationTransmissionBlocked": true
		});
		const progress = findChild(loader.item.contentItem, "inputEnhancementCalibrationEvaluationProgress");
		tryVerify(function() {
			return progress !== null && progress.visible && progress.value === 45;
		});

		setInputEnhancementCalibrationState(11, {
			"inputEnhancementCalibrationLeftPlaybackToken": "90071992547410001",
			"inputEnhancementCalibrationRightPlaybackToken": "90071992547410002"
		});
		action = findChild(loader.item.contentItem, "inputEnhancementCalibrationApply");
		tryVerify(function() { return action !== null && action.visible; });
		action.clicked();
		compare(dialogState.lastAction, "applyInputEnhancementCalibration");

		setInputEnhancementCalibrationState(12, {
			"inputEnhancementProbationRunning": true,
			"inputEnhancementProbationUndoAvailable": true
		});
		calibration = findChild(loader.item.contentItem, "inputEnhancementCalibration");
		action = findChild(loader.item.contentItem, "inputEnhancementCalibrationUndo");
		tryVerify(function() {
			return calibration !== null && calibration.refreshTimerRunning && action !== null && action.visible;
		});
		action.clicked();
		compare(dialogState.lastAction, "undoInputEnhancementRollback");
	}

	function test_dialog_metadata_controls_loading_status_size_primary_and_focus() {
		dialogState.setSpecialState("generic", {
			"id": "metadata", "loading": true, "loadingScaffold": "acl", "statusMessage": "Fetching permissions",
			"tone": "warning", "width": 520, "height": 430, "primaryActionId": "save"
		});
		tryCompare(loader.item, "width", 520);
		tryCompare(loader.item, "height", 430);
		const scaffold = findChild(loader.item.contentItem, "dialogLoadingScaffold");
		const loadingIndicator = findChild(loader.item.contentItem, "dialogLoadingIndicator");
		const status = findChild(loader.item.contentItem, "dialogStatusMessage");
		const statusBanner = findChild(loader.item.contentItem, "dialogStatusBanner");
		const primary = findChild(loader.item.contentItem, "dialogAction_save");
		verify(scaffold !== null && scaffold.visible);
		verify(loadingIndicator !== null && loadingIndicator.running);
		compare(loadingIndicator.Accessible.role, Accessible.ProgressBar);
		verify(status !== null && status.visible && status.text === "Fetching permissions");
		verify(statusBanner !== null && statusBanner.visible);
		compare(statusBanner.Accessible.role, Accessible.AlertMessage);
		compare(statusBanner.Accessible.name, "Fetching permissions");
		compare(status.Accessible.ignored, true);
		verify(primary !== null && primary.highlighted && primary.tone === "accent" && !primary.enabled);

		dialogState.setSpecialState("generic", {
			"id": "metadata", "loading": false, "width": 520, "height": 430,
			"primaryActionId": "save", "initialFocusId": "name"
		});
		const nameField = findChild(loader.item.contentItem, "dialogField_name");
		tryVerify(function() { return nameField !== null && nameField.activeFocus; });
	}

	function test_advanced_metadata_is_revealed_explicitly() {
		dialogState.setSections([
			{ "title": "General", "fields": [
				{ "id": "basic", "type": "text", "label": "Basic", "value": "visible" },
				{ "id": "advancedValue", "type": "text", "label": "Advanced value", "value": "hidden", "advanced": true }
			] },
			{ "title": "Expert", "advanced": true, "fields": [
				{ "id": "expertValue", "type": "text", "label": "Expert value", "value": "hidden" }
			] }
		]);
		const toggle = findChild(loader.item.contentItem, "dialogAdvancedToggle");
		verify(toggle !== null && toggle.visible);
		verify(findChild(loader.item.contentItem, "dialogField_basic") !== null);
		verify(findChild(loader.item.contentItem, "dialogField_advancedValue") === null);
		verify(findChild(loader.item.contentItem, "dialogField_expertValue") === null);
		mouseClick(toggle);
		tryVerify(function() {
			return findChild(loader.item.contentItem, "dialogField_advancedValue") !== null
				&& findChild(loader.item.contentItem, "dialogField_expertValue") !== null;
		});
	}

	function test_collapsible_sections_reveal_details_and_validation_expands_them() {
		dialogState.setSections([{
			"id": "codec", "title": "Network voice quality",
			"subtitle": "Defaults work for nearly everyone.",
			"advanced": true, "collapsible": true, "expandedByDefault": false,
			"fields": [{
				"id": "audio.delay", "type": "number", "label": "Release delay",
				"value": 30, "min": 0, "max": 500, "step": 10, "suffix": " ms"
			}]
		}]);

		const advancedToggle = findChild(loader.item.contentItem, "dialogAdvancedToggle");
		verify(advancedToggle !== null && advancedToggle.visible);
		mouseClick(advancedToggle);
		let section = null;
		let sectionToggle = null;
		tryVerify(function() {
			section = findChild(loader.item.contentItem, "dialogSection_codec");
			sectionToggle = findChild(loader.item.contentItem, "dialogSectionToggle_codec");
			return section !== null && section.visible && sectionToggle !== null && sectionToggle.visible;
		});
		compare(section.Accessible.description, "Collapsed section");
		compare(sectionToggle.iconName, "chevron-down");
		verify(findChild(loader.item.contentItem, "dialogField_audio.delay") === null);

		mouseClick(sectionToggle);
		let delayField = null;
		tryVerify(function() {
			delayField = findChild(loader.item.contentItem, "dialogField_audio.delay");
			return delayField !== null && delayField.visible;
		});
		compare(section.Accessible.description, "Expanded section");
		compare(sectionToggle.iconName, "chevron-up");
		compare(delayField.displayText, "30 ms");

		mouseClick(sectionToggle);
		tryVerify(function() {
			return findChild(loader.item.contentItem, "dialogField_audio.delay") === null;
		});
		const closeButton = findChild(loader.item.contentItem, "dialogCloseButton");
		verify(closeButton !== null);
		closeButton.forceActiveFocus();
		tryCompare(closeButton, "activeFocus", true);
		dialogState.setValidationError("audio.delay", "Choose a valid delay");
		tryVerify(function() {
			delayField = findChild(loader.item.contentItem, "dialogField_audio.delay");
			return delayField !== null && delayField.visible && delayField.activeFocus;
		});
		dialogState.setValidationError("audio.delay", "");
	}

	function test_settings_initial_focus_reveals_advanced_collapsible_field() {
		const basicFields = [];
		for (let index = 0; index < 12; ++index) {
			basicFields.push({
				"id": "basic" + index, "type": "text",
				"label": "Basic field " + index, "value": "Value " + index
			});
		}
		dialogState.setSections([
			{ "id": "basic", "title": "Basic", "fields": basicFields },
			{
				"id": "voiceActivation", "title": "Voice activation tuning",
				"advanced": true, "collapsible": true, "expandedByDefault": false,
				"fields": [{
					"id": "audio.vadMin", "type": "range", "label": "Stop threshold",
					"value": 80, "minimum": 0, "maximum": 100
				}]
			}
		]);
		dialogState.setSpecialState("settings", {
			"id": "advanced-initial-focus", "width": 900, "height": 480,
			"pages": [{ "id": "audioInput", "label": "Audio Input" }],
			"activePage": "audioInput", "showAdvanced": true,
			"initialFocusId": "audio.vadMin", "primaryActionId": "save"
		});

		const scroll = findChild(loader.item.contentItem, "dialogContentScroll");
		let section = null;
		let threshold = null;
		tryVerify(function() {
			section = findChild(loader.item.contentItem, "dialogSection_voiceActivation");
			threshold = findChild(loader.item.contentItem, "dialogField_audio.vadMin");
			return scroll !== null && section !== null && threshold !== null
				&& threshold.visible && threshold.activeFocus;
		});
		compare(section.Accessible.description, "Expanded section");
		tryVerify(function() { return scroll.contentItem.contentY > 0; });
	}

	function test_result_list_virtualizes_ten_thousand_stable_rows_and_supports_keyboard() {
		const items = [];
		for (let index = 0; index < 10000; ++index) {
			items.push({
				"stableId": "result-" + index,
				"id": index,
				"label": "Result " + index,
				"subtitle": "Details " + index,
				"primaryActionId": "openResult"
			});
		}
		dialogState.setSections([{ "title": "Search", "fields": [{
			"id": "search.results", "type": "resultList", "label": "Search results",
			"items": items, "emptyText": "Nothing matched"
		}] }]);

		const list = findChild(loader.item.contentItem, "dialogResultList_search.results");
		verify(list !== null);
		tryCompare(list, "count", 10000);
		verify(list.visible);
		verify(list.height <= 340);
		verify(list.contentHeight > list.height);
		compare(list.Accessible.role, Accessible.List);
		compare(list.Accessible.name, "Search results");
		tryVerify(function() { return list.liveDelegateCount() > 0; });
		verify(list.liveDelegateCount() < 100,
			"A virtualized result list must not instantiate one delegate per model row");

		list.positionViewAtBeginning();
		tryVerify(function() { return list.itemAtIndex(0) !== null; });
		const first = list.itemAtIndex(0);
		verify(first !== null);
		compare(first.stableId, "result-0");
		compare(first.Accessible.role, Accessible.ListItem);
		compare(first.Accessible.name, "Result 0");
		const firstTitle = findChild(first, "dialogResultTitle_search.results_result-0");
		const firstSubtitle = findChild(first, "dialogResultSubtitle_search.results_result-0");
		verify(firstTitle !== null && firstTitle.visible && firstTitle.width >= 96);
		compare(firstTitle.text, "Result 0");
		verify(firstSubtitle !== null && firstSubtitle.visible);
		compare(firstSubtitle.text, "Details 0");

		list.forceActiveFocus();
		tryCompare(list, "activeFocus", true);
		compare(list.currentIndex, 0);
		keyClick(Qt.Key_Down);
		compare(list.currentIndex, 1);
		keyClick(Qt.Key_End);
		compare(list.currentIndex, 9999);
		tryVerify(function() { return list.itemAtIndex(9999) !== null; });
		compare(list.itemAtIndex(9999).stableId, "result-9999");
		keyClick(Qt.Key_Return);
		compare(dialogState.lastAction, "openResult");
		compare(dialogState.lastPayload.id, 9999);
	}

	function test_result_list_virtualizes_one_thousand_rows_with_a_bounded_delegate_pool() {
		const items = [];
		for (let index = 0; index < 1000; ++index)
			items.push({ "stableId": "record-" + index, "id": index, "label": "Record " + index });
		dialogState.setSections([{ "fields": [{
			"id": "records.1k", "type": "resultList", "label": "Records", "items": items
		}] }]);

		const list = findChild(loader.item.contentItem, "dialogResultList_records.1k");
		verify(list !== null);
		tryCompare(list, "count", 1000);
		verify(list.height <= 340);
		tryVerify(function() { return list.liveDelegateCount() > 0; });
		verify(list.liveDelegateCount() < 100);
		list.positionViewAtIndex(999, ListView.Contain);
		tryVerify(function() { return list.itemAtIndex(999) !== null; });
		compare(list.itemAtIndex(999).stableId, "record-999");
	}

	function test_message_event_editor_uses_delegate_indices_for_row_striping() {
		dialogState.setSections([{ "fields": [{
			"id": "messages.events", "type": "messageEventEditor", "label": "Event behavior",
			"rows": [
				{ "type": 1, "name": "Debug", "console": true },
				{ "type": 2, "name": "Warning", "notification": true }
			]
		}] }]);
		let first = null;
		let second = null;
		tryVerify(function() {
			first = findChild(loader.item.contentItem, "messageEventRow_1");
			second = findChild(loader.item.contentItem, "messageEventRow_2");
			return first !== null && second !== null;
		});
		compare(String(first.color), String(Theme.strip));
		compare(second.color.a, 0);
	}

	function test_message_event_editor_keeps_labeled_columns_pinned_and_aligned() {
		const rows = [];
		for (let index = 0; index < 80; ++index) {
			rows.push({ "type": index + 1, "name": "Event " + (index + 1),
				"console": index === 0, "notification": false,
				"highlight": false, "tts": false, "sound": false });
		}
		dialogState.setSections([{ "fields": [{
			"id": "messages.events", "type": "messageEventEditor", "label": "Event behavior",
			"rows": rows
		}] }]);

		const list = findChild(loader.item.contentItem, "messageEventList");
		const header = findChild(loader.item.contentItem, "messageEventHeader");
		const columnObjectNames = [
			["messageEventHeaderLogColumn", "messageEventConsoleColumn_1"],
			["messageEventHeaderNotifyColumn", "messageEventNotificationColumn_1"],
			["messageEventHeaderHighlightColumn", "messageEventHighlightColumn_1"],
			["messageEventHeaderTtsColumn", "messageEventTtsColumn_1"],
			["messageEventHeaderSoundColumn", "messageEventSoundColumn_1"]
		];
		const columnPairs = [];
		tryVerify(function() {
			if (list === null || header === null)
				return false;
			columnPairs.length = 0;
			for (let index = 0; index < columnObjectNames.length; ++index) {
				const heading = findChild(loader.item.contentItem, columnObjectNames[index][0]);
				const cell = findChild(loader.item.contentItem, columnObjectNames[index][1]);
				if (heading === null || cell === null)
					return false;
				columnPairs.push([heading, cell]);
			}
			return true;
		});
		verify(list.contentHeight > list.height);
		compare(findChild(loader.item.contentItem, "messageEventHeaderLog").text, "Log");
		for (let index = 0; index < columnPairs.length; ++index) {
			const heading = columnPairs[index][0];
			const cell = columnPairs[index][1];
			compare(heading.width, cell.width);
			const headerCenter = heading.mapToItem(list, heading.width / 2, 0).x;
			const cellCenter = cell.mapToItem(list, cell.width / 2, 0).x;
			verify(Math.abs(headerCenter - cellCenter) < 0.01,
				columnObjectNames[index][0] + " center " + headerCenter
					+ " did not match " + columnObjectNames[index][1] + " center " + cellCenter);
		}

		list.positionViewAtEnd();
		tryVerify(function() { return list.contentY > 0; });
		tryVerify(function() {
			return Math.abs(header.mapToItem(list, 0, 0).y) <= 1.5;
		});
	}

	function test_result_list_has_bounded_empty_loading_and_error_states() {
		dialogState.setSections([{ "fields": [{
			"id": "records", "type": "resultList", "label": "Records",
			"items": [], "loading": true, "loadingText": "Loading records"
		}] }]);
		let list = findChild(loader.item.contentItem, "dialogResultList_records");
		let status = findChild(loader.item.contentItem, "dialogResultLoading_records");
		let statusText = findChild(loader.item.contentItem, "dialogResultStatusText_records");
		const loadingIndicator = findChild(loader.item.contentItem, "dialogResultBusy_records");
		verify(list !== null && !list.visible);
		verify(status !== null && status.visible);
		verify(loadingIndicator !== null && loadingIndicator.running);
		compare(loadingIndicator.Accessible.role, Accessible.ProgressBar);
		compare(statusText.text, "Loading records");

		dialogState.setSections([{ "fields": [{
			"id": "records", "type": "resultList", "label": "Records", "items": [],
			"state": "error", "errorText": "The server did not respond",
			"retryActionId": "retryRecords", "retryLabel": "Try again"
		}] }]);
		tryVerify(function() {
			status = findChild(loader.item.contentItem, "dialogResultError_records");
			statusText = findChild(loader.item.contentItem, "dialogResultStatusText_records");
			return status !== null && status.visible && statusText !== null
				&& statusText.text === "The server did not respond";
		});
		const retryButton = findChild(status, "dialogResultRetry_records");
		verify(retryButton !== null);
		mouseClick(retryButton);
		compare(dialogState.lastAction, "retryRecords");
		compare(dialogState.lastPayload.fieldId, "records");

		dialogState.setSections([{ "fields": [{
			"id": "records", "type": "resultList", "label": "Records",
			"items": [], "emptyText": "No records yet"
		}] }]);
		tryVerify(function() {
			status = findChild(loader.item.contentItem, "dialogResultEmpty_records");
			statusText = findChild(loader.item.contentItem, "dialogResultStatusText_records");
			return status !== null && status.visible && statusText !== null && statusText.text === "No records yet";
		});
		verify(status.height <= 120);
	}
}
