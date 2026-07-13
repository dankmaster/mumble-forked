import QtQuick
import QtQuick.Controls
import QtTest

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
        dialogState.open = false;
        tryCompare(loader.item, "visible", false);
		dialogState.resetSections();
		dialogState.setSpecialState("generic", {});
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
        tryVerify(function () {
            return loader.item.activeFocus;
        });
        const closeButton = findChild(loader.item.contentItem, "dialogCloseButton");
        verify(closeButton !== null);
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
		firstPage.forceActiveFocus();
		tryCompare(firstPage, "activeFocus", true);
		keyClick(Qt.Key_Return);
		compare(dialogState.lastAction, "selectPage");
		compare(dialogState.lastPayload.pageId, "general");
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

	function test_color_control_is_named_and_keyboard_focusable() {
		const colorButton = findChild(loader.item.contentItem, "dialogColorButton_accent");
		verify(colorButton !== null);
		compare(colorButton.Accessible.role, Accessible.Button);
		compare(colorButton.Accessible.name, "Choose Accent");
		colorButton.forceActiveFocus();
		tryCompare(colorButton, "activeFocus", true);
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
		verify(list !== null);
		tryCompare(list, "count", 2);
		tryCompare(list, "activeFocus", true);

		tryVerify(function() { return list.itemAtIndex(1) !== null; });
		const secondFavorite = list.itemAtIndex(1);
		mouseClick(secondFavorite, 8, Math.round(secondFavorite.height / 2));
		compare(dialogState.lastAction, "selectFavorite");
		compare(dialogState.lastPayload.index, 1);
		compare(dialogState.lastPayload.edit, false);

		const connectAction = findChild(list.itemAtIndex(0), "connectFavoriteAction_0");
		verify(connectAction !== null);
		mouseClick(connectAction);
		compare(dialogState.lastAction, "connectFavorite");
		compare(dialogState.lastPayload.index, 0);

		dialogState.setSpecialState("connect", {
			"id": "connect", "selectedFavoriteIndex": 0, "editorOpen": true,
			"editorTitle": "Edit server", "favorites": [{ "index": 0, "label": "Production", "selected": true }]
		});
		const editorTitle = findChild(loader.item.contentItem, "connectEditorTitle");
		tryVerify(function() { return editorTitle !== null && editorTitle.visible && editorTitle.text === "Edit server"; });
	}

	function test_voice_meter_renders_live_level_and_routes_setup_actions() {
		dialogState.setSections([{ "title": "Voice activation", "fields": [{
			"id": "audio.inputMeter", "type": "voiceMeter", "label": "Current voice input",
			"value": { "available": true, "connected": true, "amplitude": 72, "signalToNoise": 48,
				"hybrid": 63, "transmitting": true, "peakCleanMicDb": -17 },
			"vadSource": 2, "silenceThreshold": 18, "speechThreshold": 62, "active": true,
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
		compare(dialogState.lastPayload.vadSource, 2);

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
		const status = findChild(loader.item.contentItem, "dialogStatusMessage");
		const primary = findChild(loader.item.contentItem, "dialogAction_save");
		verify(scaffold !== null && scaffold.visible);
		verify(status !== null && status.visible && status.text === "Fetching permissions");
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
		verify(list !== null && !list.visible);
		verify(status !== null && status.visible);
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
