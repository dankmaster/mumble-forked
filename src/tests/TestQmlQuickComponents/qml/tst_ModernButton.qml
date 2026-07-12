import QtQuick
import QtQuick.Controls
import QtTest
import Mumble.Theme 1.0

TestCase {
    id: testCase
    name: "ModernButton"
    when: windowShown
    width: 360
    height: 180

    Loader {
        id: loader
        anchors.centerIn: parent
        source: "qrc:/qml-shell/ModernButton.qml"
        onLoaded: {
            item.text = "Continue";
            item.Accessible.name = "Continue";
        }
    }

    SignalSpy {
        id: clickedSpy
        target: loader.item
        signalName: "clicked"
    }

    function init() {
        verify(loader.item !== null);
        clickedSpy.clear();
        loader.item.forceActiveFocus();
        tryCompare(loader.item, "activeFocus", true);
    }

    function test_focus_keyboard_and_accessibility() {
        compare(loader.item.Accessible.role, Accessible.Button);
        compare(loader.item.Accessible.name, "Continue");
        keyClick(Qt.Key_Space);
        compare(clickedSpy.count, 1);
    }
}
