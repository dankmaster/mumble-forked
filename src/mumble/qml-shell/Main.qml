import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

ApplicationWindow {
    id: root
    property bool pttToolVisible: false
    property var pttToolPopup: null
    visible: true
    width: 1280
    height: 820
    minimumWidth: 760
    minimumHeight: 520
    title: clientSession.serverName
    color: Theme.strip

    onPttToolVisibleChanged: {
        if (pttToolVisible) {
            if (!pttToolPopup)
                pttToolPopup = pttToolComponent.createObject(root.contentItem)
            pttToolPopup.open()
        } else if (!pttToolVisible && pttToolPopup) {
            pttToolPopup.close()
        }
    }

    Component {
        id: pttToolComponent
        PttTool { }
    }

    QmlDialog { }
    MediaSessionWindow { }

    Column {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 20
        width: Math.min(380, parent.width - 40)
        spacing: 8
        z: 40
        Repeater {
            model: operationModel
            delegate: Rectangle {
                required property string stableId
                required property string title
                required property string subtitle
                required property string status
                required property var payload
                width: parent.width
                height: operationContent.implicitHeight + 24
                radius: Theme.innerRadius
                color: Theme.panel
                border.color: status === "failed" ? "#ef4444" : Theme.divider
                Accessible.role: Accessible.AlertMessage
                Accessible.name: title + (subtitle.length > 0 ? ": " + subtitle : "")
                ColumnLayout {
                    id: operationContent
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 6
                    RowLayout {
                        Layout.fillWidth: true
                        Label { Layout.fillWidth: true; text: title; color: Theme.textStrong; font.bold: true; elide: Text.ElideRight }
                        ModernButton {
                            visible: status === "running" && !!payload.cancellable
                            text: qsTr("Cancel")
                            Accessible.name: qsTr("Cancel %1").arg(title)
                            onClicked: operationModel.cancel(stableId)
                        }
                        ModernButton {
                            visible: status !== "running"
                            text: qsTr("Dismiss")
                            Accessible.name: qsTr("Dismiss %1").arg(title)
                            onClicked: operationModel.dismiss(stableId)
                        }
                    }
                    Label { Layout.fillWidth: true; text: subtitle; color: Theme.textMuted; wrapMode: Text.Wrap }
                    ProgressBar {
                        Layout.fillWidth: true
                        visible: status === "running" || Number(payload.progress) >= 0
                        indeterminate: !!payload.indeterminate
                        from: 0
                        to: 100
                        value: Number(payload.progress) >= 0 ? Number(payload.progress) : 0
                    }
                    Label {
                        visible: status !== "running"
                        text: status === "succeeded" ? qsTr("Completed") : qsTr("Failed")
                        color: status === "succeeded" ? "#34d399" : "#f87171"
                        font.pixelSize: 10
                    }
                }
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: 8
        radius: Theme.shellRadius
        color: Theme.shellBackground
        border.color: Theme.divider
        clip: true

        RowLayout {
            anchors.fill: parent
            spacing: 0

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                Rectangle {
                    id: shellHeader
                    Layout.fillWidth: true
                    Layout.preferredHeight: 76
                    color: Theme.panel
                    border.color: Theme.divider
                    function isShellAction(actionId) {
                        return ["qaServerConnect", "qaServerDisconnect", "qaConfigDialog", "qaConfigCert",
                                "qaRecording", "qaAudioStats", "qaHelpVersionCheck", "qaQuit"].indexOf(actionId) >= 0
                    }
                    Column {
                        anchors.left: parent.left
                        anchors.leftMargin: 20
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 4
                        Label {
                            text: activeScope.label.length > 0 ? activeScope.label : clientSession.serverName
                            color: Theme.textStrong
                            font.pixelSize: 20
                            font.bold: true
                        }
                        Label {
                            text: activeScope.description.length > 0 ? activeScope.description : activeScope.kindLabel
                            color: Theme.textMuted
                            font.pixelSize: 12
                            elide: Text.ElideRight
                            width: Math.max(0, root.width - 390)
                        }
                    }
                    ToolButton {
                        id: appMenuButton
                        anchors.right: parent.right
                        anchors.rightMargin: 16
                        anchors.verticalCenter: parent.verticalCenter
                        text: "⋯"
                        font.pixelSize: 22
                        Accessible.name: qsTr("Application menu")
                        onClicked: appMenuPopup.open()
                    }
                    Popup {
                        id: appMenuPopup
                        x: parent.width - width - 12
                        y: appMenuButton.y + appMenuButton.height
                        width: 260
                        padding: 8
                        modal: false
                        focus: true
                        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                        background: Rectangle {
                            color: Theme.panel
                            border.color: Theme.divider
                            radius: Theme.innerRadius
                        }
                        contentItem: Column {
                            Repeater {
                                model: actionModel
                                delegate: ItemDelegate {
                                    required property string stableId
                                    required property string title
                                    required property var payload
                                    width: appMenuPopup.availableWidth
                                    height: shellHeader.isShellAction(stableId) && payload.visible ? 38 : 0
                                    opacity: height > 0 ? 1 : 0
                                    enabled: height > 0 && payload.enabled
                                    text: (payload.checked ? "✓  " : "") + title
                                          + (payload.shortcut.length > 0 ? "    " + payload.shortcut : "")
                                    onClicked: {
                                        actionModel.trigger(stableId)
                                        appMenuPopup.close()
                                    }
                                }
                            }
                        }
                    }
                }

                UpdateBanner {
                    Layout.fillWidth: true
                    state: clientSession.updateBanner
                    onActionRequested: actionId => uiCommands.invokeAction(actionId)
                }

                ListView {
                    id: timeline
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: chatModel
                    clip: true
                    spacing: 8
                    leftMargin: 28
                    rightMargin: 28
                    topMargin: 20
                    bottomMargin: 20
                    reuseItems: true
                    delegate: Rectangle {
                        required property string stableId
                        required property string title
                        required property string subtitle
                        required property string status
                        required property string avatarUrl
                        required property string timestamp
                        required property string replyActor
                        required property string replySnippet
                        required property var reactions
                        required property var preview
                        required property bool own
                        required property bool deleted
                        width: Math.min(timeline.width - 56, 680)
                        height: messageRow.implicitHeight + 24
                        radius: Theme.innerRadius
                        color: own ? Theme.selected : Theme.panel
                        RowLayout {
                            id: messageRow
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 10
                            Rectangle {
                                Layout.preferredWidth: 36
                                Layout.preferredHeight: 36
                                Layout.alignment: Qt.AlignTop
                                radius: 18
                                color: Theme.strip
                                clip: true
                                Image {
                                    id: avatarImage
                                    anchors.fill: parent
                                    source: avatarUrl
                                    asynchronous: true
                                    fillMode: Image.PreserveAspectCrop
                                    visible: avatarImage.status === Image.Ready
                                }
                                Label {
                                    anchors.centerIn: parent
                                    visible: avatarUrl.length === 0
                                    text: (title || "S").slice(0, 1).toUpperCase()
                                    color: Theme.textStrong
                                    font.bold: true
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 5
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Label { text: title || qsTr("System"); color: Theme.accent; font.bold: true; font.pixelSize: 11 }
                                    Label { text: timestamp; color: Theme.textMuted; font.pixelSize: 9; visible: timestamp.length > 0 }
                                    Item { Layout.fillWidth: true }
                                    Label { text: status; color: Theme.textMuted; font.pixelSize: 9; visible: status.length > 0 }
                                }
                                Rectangle {
                                    id: previewCard
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: replyColumn.implicitHeight + 12
                                    visible: replyActor.length > 0 || replySnippet.length > 0
                                    radius: 6
                                    color: Theme.strip
                                    Column {
                                        id: replyColumn
                                        anchors.fill: parent
                                        anchors.margins: 6
                                        Label { text: replyActor; color: Theme.accent; font.pixelSize: 9; font.bold: true }
                                        Label { width: parent.width; text: replySnippet; color: Theme.textMuted; font.pixelSize: 10; elide: Text.ElideRight }
                                    }
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: deleted ? qsTr("Message deleted") : subtitle
                                    color: deleted ? Theme.textMuted : Theme.textMain
                                    wrapMode: Text.Wrap
                                    font.pixelSize: 12
                                    font.italic: deleted
                                }
                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: previewContent.implicitHeight + 16
                                    visible: preview && ((preview.title || "").length > 0
                                                         || (preview.url || "").length > 0)
                                    radius: 8
                                    color: Theme.strip
                                    RowLayout {
                                        id: previewContent
                                        anchors.fill: parent
                                        anchors.margins: 8
                                        spacing: 10
                                        Image {
                                            Layout.preferredWidth: 72
                                            Layout.preferredHeight: 54
                                            source: preview ? (preview.thumbnailUrl || "") : ""
                                            asynchronous: true
                                            fillMode: Image.PreserveAspectCrop
                                            visible: source.toString().length > 0
                                        }
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            Label { Layout.fillWidth: true; text: preview ? (preview.title || preview.host || qsTr("Link preview")) : ""; color: Theme.textStrong; font.bold: true; elide: Text.ElideRight }
                                            Label { Layout.fillWidth: true; text: preview ? (preview.description || preview.url || "") : ""; color: Theme.textMuted; font.pixelSize: 10; elide: Text.ElideRight }
                                        }
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: mediaSession.open(preview.url || preview.href || "",
                                                                     preview.provider || "provider", stableId)
                                    }
                                }
                                Flow {
                                    Layout.fillWidth: true
                                    spacing: 5
                                    visible: reactions && reactions.length > 0
                                    Repeater {
                                        model: reactions || []
                                        delegate: Rectangle {
                                            required property var modelData
                                            width: reactionLabel.implicitWidth + 12
                                            height: 24
                                            radius: 12
                                            color: modelData.selfReacted ? Theme.selected : Theme.strip
                                            Label {
                                                id: reactionLabel
                                                anchors.centerIn: parent
                                                text: (modelData.emoji || "") + " " + (modelData.count || 0)
                                                color: Theme.textMain
                                                font.pixelSize: 10
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    Label {
                        anchors.centerIn: parent
                        visible: chatModel.count === 0
                        text: clientSession.connected ? "Select a room to start chatting" : "Connect to load rooms and messages"
                        color: Theme.textMuted
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 76
                    color: Theme.strip
                    border.color: Theme.divider
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 14
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            radius: Theme.innerRadius
                            color: Theme.panel
                            border.color: Theme.divider
                            TextArea {
                                id: composer
                                anchors.fill: parent
                                anchors.margins: 5
                                placeholderText: activeScope.composerPlaceholder.length > 0
                                                 ? activeScope.composerPlaceholder
                                                 : qsTr("Connect to send messages")
                                enabled: activeScope.canSend
                                color: Theme.textMain
                                placeholderTextColor: Theme.textMuted
                                background: null
                                wrapMode: TextEdit.Wrap
                                Keys.onReturnPressed: event => {
                                    if (!(event.modifiers & Qt.ShiftModifier) && text.trim().length > 0) {
                                        uiCommands.sendMessage(text)
                                        text = ""
                                        event.accepted = true
                                    }
                                }
                            }
                        }
                        ModernButton {
                            text: "Send"
                            enabled: activeScope.canSend && composer.text.trim().length > 0
                            onClicked: { uiCommands.sendMessage(composer.text); composer.text = "" }
                        }
                    }
                }
            }

            Rectangle {
                Layout.preferredWidth: 310
                Layout.fillHeight: true
                color: Theme.rail
                border.color: Theme.divider
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 76
                        color: Theme.panel
                        border.color: Theme.divider
                        Column {
                            anchors.left: parent.left
                            anchors.leftMargin: 18
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 3
                            Label { text: clientSession.serverName; color: Theme.textStrong; font.bold: true; font.pixelSize: 14 }
                            Label { text: clientSession.connectionLabel; color: clientSession.connected ? Theme.accent : Theme.textMuted; font.pixelSize: 11 }
                        }
                    }
                    Label { Layout.margins: 18; text: qsTr("ROOMS"); color: Theme.textMuted; font.pixelSize: 10; font.bold: true }
                    ListView {
                        id: rooms
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        model: roomModel
                        clip: true
                        reuseItems: true
                        spacing: 2
                        leftMargin: 10
                        rightMargin: 10
                        delegate: Rectangle {
                            required property string stableId
                            required property string scopeToken
                            required property string title
                            required property string subtitle
                            required property string kind
                            required property bool selected
                            required property int depth
                            required property int unreadCount
                            width: rooms.width - 20
                            height: subtitle.length > 0 ? 48 : 38
                            radius: 8
                            color: selected ? Theme.selected : roomMouse.containsMouse ? Theme.panel : "transparent"
                            activeFocusOnTab: true
                            Accessible.role: Accessible.ListItem
                            Accessible.name: title
                            Accessible.description: subtitle
                            Accessible.selected: selected
                            Column {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.leftMargin: 10 + Math.min(depth, 5) * 12
                                anchors.rightMargin: 10
                                spacing: 2
                                Label { width: parent.width; text: title; color: Theme.textStrong; font.bold: true; font.pixelSize: 12; elide: Text.ElideRight }
                                Label { width: parent.width; visible: subtitle.length > 0; text: subtitle; color: Theme.textMuted; font.pixelSize: 10; elide: Text.ElideRight }
                            }
                            Rectangle {
                                visible: unreadCount > 0
                                anchors.right: parent.right
                                anchors.rightMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                width: Math.max(20, unreadLabel.implicitWidth + 10)
                                height: 20
                                radius: 10
                                color: Theme.accent
                                Label {
                                    id: unreadLabel
                                    anchors.centerIn: parent
                                    text: unreadCount > 99 ? "99+" : unreadCount
                                    color: Theme.strip
                                    font.pixelSize: 9
                                    font.bold: true
                                }
                            }
                            MouseArea {
                                id: roomMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: uiCommands.selectScope(scopeToken)
                                onDoubleClicked: if (kind === "voice") uiCommands.joinVoiceChannel(scopeToken)
                            }
                            Keys.onReturnPressed: event => {
                                uiCommands.selectScope(scopeToken)
                                event.accepted = true
                            }
                            Keys.onEnterPressed: event => {
                                uiCommands.selectScope(scopeToken)
                                event.accepted = true
                            }
                            Keys.onSpacePressed: event => {
                                if (kind === "voice")
                                    uiCommands.joinVoiceChannel(scopeToken)
                                else
                                    uiCommands.selectScope(scopeToken)
                                event.accepted = true
                            }
                        }
                    }
                    Label {
                        Layout.leftMargin: 18
                        Layout.topMargin: 10
                        Layout.bottomMargin: 6
                        text: qsTr("PARTICIPANTS")
                        color: Theme.textMuted
                        font.pixelSize: 10
                        font.bold: true
                        visible: participantModel.count > 0
                    }
                    ListView {
                        id: participants
                        Layout.fillWidth: true
                        Layout.preferredHeight: visible ? Math.min(contentHeight, 180) : 0
                        visible: participantModel.count > 0
                        model: participantModel
                        clip: true
                        reuseItems: true
                        leftMargin: 10
                        rightMargin: 10
                        delegate: Rectangle {
                            required property string stableId
                            required property string title
                            required property string subtitle
                            required property string status
                            width: participants.width - 20
                            height: 42
                            radius: 8
                            color: selectionState.selectedUserSession !== undefined
                                   && String(selectionState.selectedUserSession) === stableId
                                   ? Theme.selected
                                   : participantMouse.containsMouse ? Theme.panel : "transparent"
                            activeFocusOnTab: true
                            Accessible.role: Accessible.ListItem
                            Accessible.name: title
                            Accessible.description: subtitle
                            Accessible.selected: selectionState.selectedUserSession !== undefined
                                                 && String(selectionState.selectedUserSession) === stableId
                            Rectangle {
                                id: presenceDot
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                width: 8
                                height: 8
                                radius: 4
                                color: status.length > 0 && status !== "passive" ? Theme.accent : Theme.textMuted
                            }
                            Column {
                                anchors.left: presenceDot.right
                                anchors.leftMargin: 10
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                Label { width: parent.width; text: title; color: Theme.textStrong; font.pixelSize: 11; elide: Text.ElideRight }
                                Label { width: parent.width; text: subtitle; color: Theme.textMuted; font.pixelSize: 9; elide: Text.ElideRight; visible: subtitle.length > 0 }
                            }
                            MouseArea {
                                id: participantMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: uiCommands.selectParticipant(stableId)
                                onDoubleClicked: uiCommands.openDirectMessage(stableId)
                            }
                            Keys.onReturnPressed: event => {
                                uiCommands.selectParticipant(stableId)
                                event.accepted = true
                            }
                            Keys.onEnterPressed: event => {
                                uiCommands.selectParticipant(stableId)
                                event.accepted = true
                            }
                            Keys.onSpacePressed: event => {
                                uiCommands.openDirectMessage(stableId)
                                event.accepted = true
                            }
                        }
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 76
                        color: Theme.strip
                        border.color: Theme.divider
                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            ColumnLayout {
                                Layout.fillWidth: true
                                Label { text: clientSession.selfName; color: Theme.textStrong; font.bold: true }
                                Label { text: clientSession.connectionLabel; color: Theme.textMuted; font.pixelSize: 10 }
                            }
                            ModernButton { text: clientSession.selfMuted ? "Unmute" : "Mute"; onClicked: uiCommands.toggleSelfMute() }
                        }
                    }
                }
            }
        }
    }
}
