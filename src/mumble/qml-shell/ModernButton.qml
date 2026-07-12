import QtQuick
import QtQuick.Controls
import Mumble.Theme 1.0

Button {
    id: control
    Accessible.role: Accessible.Button
    Accessible.name: text
    implicitHeight: 34
    leftPadding: 14
    rightPadding: 14
    font.pixelSize: 12
    palette.buttonText: Theme.textStrong
    background: Rectangle {
        radius: 9
        color: control.down ? Qt.darker(Theme.accent, 1.25)
                            : control.hovered ? Qt.lighter(Theme.panel, 1.12) : Theme.panel
        border.color: control.activeFocus ? Theme.accent : Theme.divider
        border.width: 1
    }
}
