import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

ColumnLayout {
    id: root
    property var stonks: ({})
    width: parent ? parent.width : 0
    spacing: 14

    RowLayout {
        Layout.fillWidth: true
        Label { Layout.fillWidth: true; text: stonks.error || stonks.status || stonks.leaderboardDescription || qsTr("Portfolio overview"); color: stonks.error ? "#f87171" : Theme.textMuted; wrapMode: Text.Wrap }
        ModernButton { visible: !stonks.registered; text: qsTr("Register"); onClicked: dialogState.invokeAction("register", {}) }
        ModernButton { text: qsTr("Refresh"); onClicked: dialogState.invokeAction("refresh", {}) }
    }

    Flow {
        Layout.fillWidth: true
        spacing: 7
        Repeater {
            model: stonks.periods || []
            delegate: ModernButton {
                required property var modelData
                checkable: true
                text: String(modelData).toUpperCase()
                checked: String(modelData) === String(root.stonks.selectedPeriod || "30d")
                onClicked: dialogState.invokeAction("selectPeriod", { "period": String(modelData) })
            }
        }
    }

    Label { text: qsTr("Leaderboard"); color: Theme.textStrong; font.bold: true; font.pixelSize: 14 }
    ListView {
        id: leaderboard
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(contentHeight, 250)
        model: root.stonks.leaderboard || []
        clip: true
        spacing: 4
        delegate: ItemDelegate {
            required property var modelData
            width: leaderboard.width
            height: 44
            text: qsTr("%1. %2   %3%").arg(modelData.rank || index + 1)
                    .arg(modelData.userName || qsTr("User"))
                    .arg(Number(modelData.returnPercent || 0).toFixed(2))
            onClicked: dialogState.invokeAction("selectUser", { "userId": modelData.userId })
        }
    }
    Label { visible: leaderboard.count === 0; text: qsTr("No leaderboard data yet"); color: Theme.textMuted }

    Label { text: qsTr("Tickers"); color: Theme.textStrong; font.bold: true; font.pixelSize: 14 }
    Flow {
        Layout.fillWidth: true
        spacing: 7
        Repeater {
            model: root.stonks.popularTickers || []
            delegate: Rectangle {
                required property var modelData
                width: tickerRow.implicitWidth + 18; height: 36; radius: 8; color: Theme.strip; border.color: Theme.divider
                Row {
                    id: tickerRow; anchors.centerIn: parent; spacing: 7
                    Label { text: modelData.symbol || ""; color: Theme.textStrong; font.bold: true }
                    Label { text: qsTr("%1 holders").arg(modelData.holderCount || 0); color: Theme.textMuted; font.pixelSize: 9 }
                    ToolButton {
                        text: "+"; Accessible.name: qsTr("Pin %1").arg(modelData.symbol || qsTr("ticker"))
                        onClicked: dialogState.invokeAction("setTickerPin", { "symbol": modelData.symbol, "pinned": true })
                    }
                }
            }
        }
    }

    Label { text: qsTr("Feed"); color: Theme.textStrong; font.bold: true; font.pixelSize: 14 }
    RowLayout {
        id: feedRow
        property var preferences: root.stonks.feedPreferences || ({})
        CheckBox {
            text: qsTr("Mine"); checked: feedRow.preferences.showMine !== false
            onToggled: dialogState.invokeAction("setFeedPreferences", { "showMine": checked, "showPopular": feedRow.preferences.showPopular !== false, "showPins": feedRow.preferences.showPins !== false })
        }
        CheckBox {
            text: qsTr("Popular"); checked: feedRow.preferences.showPopular !== false
            onToggled: dialogState.invokeAction("setFeedPreferences", { "showMine": feedRow.preferences.showMine !== false, "showPopular": checked, "showPins": feedRow.preferences.showPins !== false })
        }
        CheckBox {
            text: qsTr("Pinned"); checked: feedRow.preferences.showPins !== false
            onToggled: dialogState.invokeAction("setFeedPreferences", { "showMine": feedRow.preferences.showMine !== false, "showPopular": feedRow.preferences.showPopular !== false, "showPins": checked })
        }
    }
}
