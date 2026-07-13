import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

Window {
    id: viewer

    required property var attachment
    readonly property string sourceUrl: attachment ? (attachment.url || attachment.thumbnailUrl || "") : ""
    readonly property string displayTitle: attachment ? (attachment.alt || attachment.name || qsTr("Image attachment")) : qsTr("Image attachment")
	readonly property string renderSource: safeRenderImageSource(sourceUrl)
	readonly property bool managedAnimated: /^file:\/\//i.test(renderSource)
	readonly property int loadStatus: managedAnimated ? animation.status : picture.status

    function safeRenderImageSource(value) {
        const source = String(value === undefined || value === null ? "" : value).trim()
		if (/^(image:\/\/mumble\/|qrc:\/)/i.test(source))
			return source
		return /^file:\/\//i.test(source)
			&& /\/mumble-qml-images-[A-Za-z0-9]+\/[0-9a-f]{64}-[0-9a-f-]{36}\.gif$/i.test(source)
			? source : ""
    }

    width: 900
    height: 680
    minimumWidth: 420
    minimumHeight: 320
    visible: true
    color: Theme.shellBackground
    title: displayTitle

    Rectangle {
        anchors.fill: parent
        color: Theme.shellBackground
        border.color: Theme.divider

        Image {
            id: picture
            anchors.fill: parent
            anchors.margins: 18
			source: viewer.managedAnimated ? "" : viewer.renderSource
            asynchronous: true
            cache: false
            fillMode: Image.PreserveAspectFit
			visible: !viewer.managedAnimated
            Accessible.name: viewer.displayTitle
        }

		AnimatedImage {
			id: animation
			anchors.fill: parent
			anchors.margins: 18
			source: viewer.managedAnimated ? viewer.renderSource : ""
			asynchronous: true
			cache: false
			fillMode: Image.PreserveAspectFit
			playing: viewer.visible && status === AnimatedImage.Ready
			visible: viewer.managedAnimated
			Accessible.name: viewer.displayTitle
		}

        BusyIndicator {
            anchors.centerIn: parent
			running: viewer.loadStatus === Image.Loading
            visible: running
        }

        Label {
			textFormat: Text.PlainText
            anchors.centerIn: parent
			visible: viewer.loadStatus === Image.Error || viewer.renderSource.length === 0
            text: qsTr("Attachment unavailable")
            color: Theme.textMuted
        }

        RowLayout {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 12
            ModernButton {
                text: qsTr("Close")
                onClicked: viewer.close()
            }
        }
    }
}
