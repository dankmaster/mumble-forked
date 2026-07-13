import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtWebEngine
import Mumble.Theme 1.0

ApplicationWindow {
    id: mediaWindow
    visible: mediaSession.active
    width: 1040
    height: 700
    minimumWidth: 640
    minimumHeight: 420
    title: mediaSession.sharedAvailable && mediaSession.sharedTitle.length > 0
           ? mediaSession.sharedTitle
           : qsTr("Media session")
    color: Theme.shellBackground
    onClosing: function(close) {
        close.accepted = true
        mediaSession.close()
    }

    Loader {
        id: playerLoader
        anchors.fill: parent
        anchors.bottomMargin: controls.height
        active: mediaSession.active
        sourceComponent: WebEngineView {
            id: player
            profile: WebEngineProfile {
                offTheRecord: true
                onDownloadRequested: function(download) { download.cancel() }
            }
            url: mediaSession.url
            settings.playbackRequiresUserGesture: false
            onLoadingChanged: function(request) {
                if (request.status === WebEngineView.LoadFailedStatus)
                    mediaSession.reportError(request.errorString)
            }
            onRenderProcessTerminated: function(status, exitCode) {
                mediaSession.reportError(qsTr("The media renderer stopped unexpectedly."))
            }
            // A new-window request fails closed unless openIn() is called.
            onNewWindowRequested: function(request) {}
            onFileDialogRequested: function(request) {
                request.accepted = true
                request.dialogReject()
            }
            onAuthenticationDialogRequested: function(request) {
                request.accepted = true
                request.dialogReject()
            }
            onPermissionRequested: function(permission) { permission.deny() }
            onCertificateError: function(error) { error.rejectCertificate() }
            onContextMenuRequested: function(request) { request.accepted = true }
            onNavigationRequested: function(request) {
                if (!mediaSession.isNavigationAllowed(request.url))
                    request.action = WebEngineNavigationRequest.IgnoreRequest
            }
            Connections {
                target: mediaSession
                function onPlayRequested() { player.runJavaScript("document.querySelector('video,audio')?.play()") }
                function onPauseRequested() { player.runJavaScript("document.querySelector('video,audio')?.pause()") }
                function onSeekRequested(seconds) {
                    player.runJavaScript("(function(){const m=document.querySelector('video,audio');if(m)m.currentTime=" + Number(seconds) + "})()")
                }
                function onRetryRequested() { player.reload() }
            }
            Timer {
                interval: 500
                running: true
                repeat: true
                onTriggered: player.runJavaScript(
                    "(function(){const m=document.querySelector('video,audio');return m?{position:m.currentTime||0,duration:isFinite(m.duration)?m.duration:0,paused:m.paused}:null})()",
                    function(value) {
                        if (value) mediaSession.reportPlaybackState(value.position, value.duration, value.paused)
                    })
            }
        }
    }

    Rectangle {
        anchors.fill: playerLoader
        visible: mediaSession.error.length > 0
        color: "#d9161b26"
        z: 5

        ColumnLayout {
            anchors.centerIn: parent
            width: Math.min(parent.width - 48, 520)
            spacing: 14

            Label {
                Layout.fillWidth: true
                text: qsTr("Media playback failed")
                color: Theme.textStrong
                font.bold: true
                font.pixelSize: 20
                horizontalAlignment: Text.AlignHCenter
            }
            Label {
                Layout.fillWidth: true
                text: mediaSession.error
                color: Theme.textMuted
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
            }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                ModernButton { text: qsTr("Retry"); onClicked: mediaSession.retry() }
                ModernButton { text: qsTr("Open externally"); onClicked: Qt.openUrlExternally(mediaSession.url) }
            }
        }
    }

    Rectangle {
        id: controls
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 64
        color: Theme.panel
        border.color: Theme.divider
        RowLayout {
            anchors.fill: parent
            anchors.margins: 10
            ModernButton {
                enabled: !mediaSession.sharedAvailable || mediaSession.sharedHost
                text: mediaSession.state === "playing" ? qsTr("Pause") : qsTr("Play")
                onClicked: mediaSession.state === "playing" ? mediaSession.pause() : mediaSession.play()
            }
            Slider {
                Layout.fillWidth: true
                enabled: !mediaSession.sharedAvailable || mediaSession.sharedHost
                from: 0
                to: Math.max(1, mediaSession.duration)
                value: mediaSession.position
                onMoved: mediaSession.seek(value)
            }
            Label { text: Math.floor(mediaSession.position) + " / " + Math.floor(mediaSession.duration) + " s"; color: Theme.textMuted }
            Label {
                visible: mediaSession.sharedAvailable
                text: mediaSession.sharedHost ? qsTr("Hosting") : qsTr("Synchronized")
                color: mediaSession.sharedHost ? Theme.accent : Theme.textMuted
            }
            ModernButton {
                text: mediaSession.sharedHost ? qsTr("End") : (mediaSession.sharedJoined ? qsTr("Leave") : qsTr("Close"))
                onClicked: mediaSession.close()
            }
        }
    }
}
