import QtQuick
import QtQuick.Controls
import QtTest

TestCase {
    id: testCase
    name: "MenuKeyboard"
    when: windowShown
    width: 420
    height: 240
    property string invoked: ""

    Button {
        id: opener
        text: "Actions"
        onClicked: menu.open()
        Menu {
            id: menu
            MenuItem {
                text: "Reply"
                onTriggered: testCase.invoked = "reply"
            }
            MenuItem {
                text: "Delete"
                onTriggered: testCase.invoked = "delete"
            }
        }
    }

    function init() {
        invoked = "";
        opener.forceActiveFocus();
    }

    function test_popup_keyboard_navigation() {
        menu.open();
        tryCompare(menu, "opened", true);
        menu.contentItem.forceActiveFocus();
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Return);
        tryCompare(testCase, "invoked", "delete");
        tryCompare(menu, "opened", false);
    }

    function test_escape_closes_without_action() {
        menu.open();
        tryCompare(menu, "opened", true);
        keyClick(Qt.Key_Escape);
        tryCompare(menu, "opened", false);
        compare(invoked, "");
    }
}
