import QtQuick

Rectangle {
	id: root

	required property var session
	property string aspect: "wide"
	property string presentationProvider: ""
	property string presentationMode: ""
	property bool animationAutoPlayEnabled: true
	property var mediaProfileFactory: null
	property string visualFixtureMode: ""
	readonly property string surfaceId: "mediaSession.inline"
	readonly property bool webSurfaceActive: true
	readonly property bool nativeSurfaceActive: false
	readonly property string rendererBackend: "fixture"
	readonly property string rendererState: "active"
	readonly property bool rendererHealthy: true
	readonly property bool documentReady: true

	implicitHeight: width * (aspect === "short" ? 16 / 9
		: aspect === "square" ? 1 : 9 / 16)
	color: "transparent"
}
