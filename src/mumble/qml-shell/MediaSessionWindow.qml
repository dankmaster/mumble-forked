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
    title: qsTr("Media session")
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
            profile: WebEngineProfile { offTheRecord: true }
            url: mediaSession.url
            settings.playbackRequiresUserGesture: false
            onLoadingChanged: function(request) {
                if (request.status === WebEngineView.LoadFailedStatus)
                    mediaSession.reportError(request.errorString)
            }
            onRenderProcessTerminated: function(status, exitCode) {
                mediaSession.reportError(qsTr("The media renderer stopped unexpectedly."))
            }
            onNewWindowRequested: function(request) { request.reject() }
            onFileDialogRequested: function(request) { request.reject() }
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
            ModernButton { text: mediaSession.state === "playing" ? qsTr("Pause") : qsTr("Play"); onClicked: mediaSession.state === "playing" ? mediaSession.pause() : mediaSession.play() }
            Slider { Layout.fillWidth: true; from: 0; to: Math.max(1, mediaSession.duration); value: mediaSession.position; onMoved: mediaSession.seek(value) }
            Label { text: Math.floor(mediaSession.position) + " / " + Math.floor(mediaSession.duration) + " s"; color: Theme.textMuted }
            ModernButton { text: qsTr("Close"); onClicked: mediaSession.close() }
        }
    }
}
