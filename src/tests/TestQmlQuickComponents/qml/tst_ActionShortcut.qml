import QtQuick
import QtTest

Item {
    id: root
    width: 320
    height: 200
    property int triggerCount: 0
    property bool actionEnabled: true

    Shortcut {
        id: actionShortcut
        sequence: "Ctrl+F"
        enabled: root.actionEnabled
        context: Qt.ApplicationShortcut
        onActivated: root.triggerCount += 1
    }

    TestCase {
        name: "ActionShortcut"
        when: windowShown

        function test_enabledAndDisabledTrigger() {
            root.triggerCount = 0
            root.actionEnabled = true
            keyClick(Qt.Key_F, Qt.ControlModifier)
            compare(root.triggerCount, 1)
            root.actionEnabled = false
            keyClick(Qt.Key_F, Qt.ControlModifier)
            compare(root.triggerCount, 1)
        }
    }
}
