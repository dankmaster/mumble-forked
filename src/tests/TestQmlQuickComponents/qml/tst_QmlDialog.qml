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
    }

    function test_open_focus_and_close_action() {
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
}
