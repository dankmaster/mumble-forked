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

    function init() {
        verify(loader.item !== null);
        dialogState.setValidationError("name", "");
        dialogState.open = true;
        tryCompare(loader.item, "visible", true);
    }

    function cleanup() {
		loader.item.beforeOpen = null;
        dialogState.open = false;
        tryCompare(loader.item, "visible", false);
		dialogState.resetSections();
		dialogState.setSpecialState("generic", {});
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

	function test_page_rail_is_keyboard_accessible_at_medium_width() {
		dialogState.setSpecialState("generic", { "id": "medium", "width": 760, "height": 620 });
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
		compare(firstPage.Accessible.selected, true);
		compare(firstPage.background.color, Theme.selected);
		firstPage.forceActiveFocus();
		tryCompare(firstPage, "activeFocus", true);
		keyClick(Qt.Key_Return);
		compare(dialogState.lastAction, "selectPage");
		compare(dialogState.lastPayload.pageId, "general");
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

		const railHeading = findChild(loader.item.contentItem, "dialogPageRailHeading");
		const pageHeading = findChild(loader.item.contentItem, "dialogSettingsPageHeading");
		const pageTitle = findChild(loader.item.contentItem, "dialogSettingsPageTitle");
		const pageCategory = findChild(loader.item.contentItem, "dialogSettingsPageCategory");
		const inputIcon = findChild(loader.item.contentItem, "dialogPageIcon_general");
		const appearanceIcon = findChild(loader.item.contentItem, "dialogPageIcon_appearance");
		const headerAdvanced = findChild(loader.item.contentItem, "dialogAdvancedToggle");
		const footerAdvanced = findChild(loader.item.contentItem, "dialogSettingsAdvancedFooter");
		const footerToggle = findChild(loader.item.contentItem, "dialogSettingsAdvancedToggle");
		tryVerify(function() {
			return railHeading !== null && railHeading.visible
				&& pageHeading !== null && pageHeading.visible
				&& pageTitle !== null && pageTitle.text === "Audio Input"
				&& inputIcon !== null && appearanceIcon !== null
				&& footerAdvanced !== null && footerAdvanced.visible
				&& footerToggle !== null && footerToggle.visible;
		});
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
		mouseClick(footerToggle);
		tryVerify(function() { return findChild(loader.item.contentItem, "dialogField_expert") !== null; });
	}

	function test_initial_focus_scrolls_long_content_clear_of_footer() {
		const fields = [];
		for (let index = 0; index < 18; ++index) {
			fields.push({ "id": "field" + index, "type": "text",
				"label": "Field " + index, "value": "Value " + index });
		}
		dialogState.setSections([{ "title": "Long settings page", "fields": fields }]);
		dialogState.setSpecialState("generic", {
			"id": "long-content", "width": 760, "height": 420,
			"initialFocusId": "field17", "primaryActionId": "save"
		});
		const scroll = findChild(loader.item.contentItem, "dialogContentScroll");
		const footer = findChild(loader.item.contentItem, "dialogFooter");
		const lastField = findChild(loader.item.contentItem, "dialogField_field17");
		tryVerify(function() { return scroll !== null && footer !== null && lastField !== null && lastField.activeFocus; });
		tryVerify(function() { return scroll.contentItem.contentY > 0; });
		const scrollBottom = scroll.mapToItem(loader.item.contentItem, 0, scroll.height).y;
		verify(scrollBottom <= footer.y + 1,
			"Scrollable dialog content must end before the persistent action footer");
		const fieldBottom = lastField.mapToItem(loader.item.contentItem, 0, lastField.height).y;
		verify(fieldBottom <= footer.y,
			"Focused settings fields must be scrolled above the persistent action footer");
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
		verify(colorButton !== null);
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
		compare(picker.palette.highlight, Theme.selected);
		compare(picker.palette.toolTipBase, Theme.surfaceRaised);
		compare(picker.palette.disabled.link, Theme.textMuted);
		const pickerDialog = findChild(picker, "dialogColorPickerDialog_accent");
		const hexInput = findChild(picker, "dialogColorHex_accent");
		const applyButton = findChild(picker, "dialogColorApply_accent");
		verify(pickerDialog !== null && hexInput !== null && applyButton !== null);
		compare(pickerDialog.Accessible.role, Accessible.Dialog);
		compare(pickerDialog.Accessible.name, "Choose Accent");
		tryCompare(hexInput, "activeFocus", true);

		picker.draftColor = "#12";
		tryCompare(applyButton, "enabled", false);
		picker.setDraft("#A1B2C3");
		compare(hexInput.text, "#A1B2C3");
		tryCompare(applyButton, "enabled", true);
		applyButton.clicked();
		tryCompare(picker, "visible", false);
		compare(loader.item.contentItem.enabled, true);
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
						"swatch": { "accent": "#5ec8b0" } },
					{ "value": "blue", "label": "Blue", "swatch": { "accent": "#73b7ff" } }
				]
			}
		] }])

		const overview = findChild(loader.item.contentItem, "appearanceThemeOverview")
		const darkCard = findChild(loader.item.contentItem, "dialogThemeOption_dark")
		const lightCard = findChild(loader.item.contentItem, "dialogThemeOption_light")
		const autoAccent = findChild(loader.item.contentItem, "dialogAccentOption_auto")
		const blueAccent = findChild(loader.item.contentItem, "dialogAccentOption_blue")
		tryVerify(function() {
			return overview !== null && overview.visible && overview.height >= 120
				&& darkCard !== null && lightCard !== null && autoAccent !== null && blueAccent !== null
		})
		compare(darkCard.Accessible.role, Accessible.RadioButton)
		compare(darkCard.Accessible.checked, true)
		verify(lightCard.enabled)
		lightCard.forceActiveFocus()
		tryCompare(lightCard, "activeFocus", true)
		compare(lightCard.background.border.width, 2)
		lightCard.clicked()
		compare(String(dialogState.fieldValue("look.modernTheme")), "light")
		verify(blueAccent.enabled)
		blueAccent.forceActiveFocus()
		tryCompare(blueAccent, "activeFocus", true)
		compare(blueAccent.background.border.width, 2)
		blueAccent.clicked()
		compare(String(dialogState.fieldValue("look.modernAccent")), "blue")
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
		dialogState.setSpecialState("connect", {
			"id": "connect",
			"pages": [],
			"width": 860,
			"height": 640,
			"primaryActionId": "save",
			"initialFocusId": "connectFavoriteList",
			"selectedFavoriteIndex": 0,
			"editorOpen": false,
			"editorTitle": "Edit server",
			"favorites": [
				{ "index": 0, "label": "Production", "subtitle": "voice.example.test:64738 / Alice", "selected": true, "usersValue": "5/20", "pingValue": "24 ms" },
				{ "index": 1, "label": "Testing", "subtitle": "test.example.test:64738 / Bob", "selected": false, "usersValue": "2/20", "pingValue": "31 ms" }
			]
		});
		const list = findChild(loader.item.contentItem, "connectFavoriteList");
		const surface = findChild(loader.item.contentItem, "connectFavoriteSurface");
		const connectEyebrow = findChild(loader.item.contentItem, "dialogProductEyebrow");
		const addServer = findChild(loader.item.contentItem, "connectNewFavoriteButton");
		verify(list !== null);
		verify(surface !== null);
		verify(connectEyebrow !== null && connectEyebrow.visible && connectEyebrow.text === "MUMBLE");
		verify(addServer !== null && addServer.tone === "secondary");
		tryCompare(list, "count", 2);
		tryVerify(function() { return surface.height < 260 && loader.item.height < 500; });
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
		wait(0);
		mouseClick(secondFavorite, 8, Math.round(secondFavorite.height / 2));
		compare(dialogState.lastAction, "selectFavorite");
		compare(dialogState.lastPayload.index, 1);
		compare(dialogState.lastPayload.edit, false);

		const connectAction = findChild(list.itemAtIndex(0), "connectFavoriteAction_0");
		compare(connectAction, null);
		const selectionIndicator = findChild(list.itemAtIndex(0), "connectFavoriteSelection_0");
		verify(selectionIndicator !== null);
		compare(selectionIndicator.name, "check");
		firstFavorite.doubleClicked();
		compare(dialogState.lastAction, "connectFavorite");
		compare(dialogState.lastPayload.index, 0);

		dialogState.setSpecialState("connect", {
			"id": "connect", "pages": [], "width": 860, "height": 640,
			"selectedFavoriteIndex": 0, "editorOpen": true,
			"editorTitle": "Edit server", "favorites": [{ "index": 0, "label": "Production", "selected": true }]
		});
		const editorTitle = findChild(loader.item.contentItem, "connectEditorTitle");
		tryVerify(function() { return editorTitle !== null && editorTitle.visible && editorTitle.text === "Edit server"; });
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
			"vadSource": 2, "silenceThreshold": 18, "speechThreshold": 62, "active": true,
			"recommendedVadSource": 1, "recommendedInputGateMode": 1,
			"recommendedNoiseCancelMode": 3, "recommendedMaxAmplification": 6400,
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

		const setup = findChild(loader.item.contentItem, "voiceMeterCalibration_audio.inputMeter");
		verify(setup !== null && setup.visible);
		mouseClick(setup);
		compare(dialogState.lastAction, "finishAudioSetupWizard");
		compare(dialogState.lastPayload.silenceThreshold, 18);
		compare(dialogState.lastPayload.speechThreshold, 62);
		compare(dialogState.lastPayload.vadSource, 1);
		compare(dialogState.lastPayload.inputGateMode, 1);
		compare(dialogState.lastPayload.noiseCancelMode, 3);
		compare(dialogState.lastPayload.maxAmplification, 6400);

		const replay = findChild(loader.item.contentItem, "voiceMeterReplay_audio.inputMeter");
		verify(replay !== null && replay.visible);
		mouseClick(replay);
		compare(dialogState.lastAction, "startVoiceReplay");
		compare(dialogState.lastPayload.mode, "server");
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
