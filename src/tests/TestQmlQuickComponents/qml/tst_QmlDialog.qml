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
		loader.item.width = Math.min(testCase.width - 48, 1040);
		loader.item.height = Math.min(testCase.height - 48, 760);
        dialogState.setValidationError("name", "");
        dialogState.open = true;
        tryCompare(loader.item, "visible", true);
    }

    function cleanup() {
        dialogState.open = false;
        tryCompare(loader.item, "visible", false);
		dialogState.setSpecialState("generic", {});
    }

    function test_open_focus_and_close_action() {
		compare(loader.item.title, "Test dialog");
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
		loader.item.width = 372;
		tryVerify(function() { return loader.item.compactDialogLayout; });
		const rail = findChild(loader.item.contentItem, "dialogPageRail");
		const selector = findChild(loader.item.contentItem, "dialogCompactPageSelector");
		const footer = findChild(loader.item.contentItem, "dialogFooter");
		verify(rail !== null && !rail.visible);
		verify(selector !== null && selector.visible);
		verify(selector.width >= 300);
		tryVerify(function() { return footer !== null && footer.height > 64; });
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
}
