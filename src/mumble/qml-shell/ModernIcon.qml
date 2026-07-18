import QtQuick
import QtQuick.Shapes
import Mumble.Theme 1.0

Item {
	id: root
	property string name: ""
	property color color: Theme.textStrong
	property int size: 18
	Accessible.ignored: true

	function pathForName(iconName) {
		switch (iconName) {
		case "activity":
			return "M3 12 H7 L9.5 5 L14.5 19 L17 12 H21"
		case "action":
			return "M13 2 L4 14 H11 L10 22 L20 9 H13 Z"
		case "add":
			return "M12 5 V19 M5 12 H19"
		case "attach":
			return "M21.4 11.6 L12 21 A6 6 0 0 1 3.5 12.5 L13 3 A4 4 0 0 1 18.7 8.7 L9.2 18.2 A2 2 0 0 1 6.4 15.4 L15.2 6.6"
		case "certificate":
			return "M6 3 H18 V14 H6 Z M9 7 H15 M9 10 H13 M10 14 V21 L12 19 L14 21 V14"
		case "check":
			return "M5 12 L9 16 L19 6"
		case "chevron-down":
			return "M6 9 L12 15 L18 9"
		case "chevron-up":
			return "M6 15 L12 9 L18 15"
		case "close":
			return "M6 6 L18 18 M18 6 L6 18"
		case "connect":
			return "M8 3 V8 M16 3 V8 M6 8 H18 V11 A6 6 0 0 1 6 11 Z M12 17 V21"
		case "copy":
			return "M8 8 H20 V20 H8 Z M4 16 V4 H16"
		case "deafen":
			return "M4 14 V12 A8 8 0 0 1 20 12 V14 M4 14 H7 V20 H5 A1 1 0 0 1 4 19 Z M20 14 H17 V20 H19 A1 1 0 0 0 20 19 Z"
		case "delete":
			return "M4 7 H20 M9 7 V4 H15 V7 M7 7 L8 21 H16 L17 7 M10 11 V17 M14 11 V17"
		case "direct":
			return "M20 11.5 A8 8 0 0 1 11.5 19.5 A8.5 8.5 0 1 1 20 11.5 Z M7 20 L4 21 L5 18"
		case "disconnect":
			return "M5 5 L19 19 M8 3 V7 M16 3 V7 M7 8 H17 V10 M12 17 V21"
		case "download":
			return "M12 3 V15 M7 10 L12 15 L17 10 M5 20 H19"
		case "edit":
			return "M4 20 L8.5 19 L19 8.5 L15.5 5 L5 15.5 Z M13.5 7 L17 10.5"
		case "external":
			return "M14 5 H19 V10 M19 5 L11 13 M18 13 V19 H5 V6 H11"
		case "fullscreen":
			return "M8 3 H3 V8 M16 3 H21 V8 M21 16 V21 H16 M3 16 V21 H8"
		case "fullscreen-exit":
			return "M3 8 H8 V3 M21 8 H16 V3 M16 21 V16 H21 M8 21 V16 H3"
		case "history":
			return "M12 3 A9 9 0 1 1 3 12 A9 9 0 0 1 12 3 Z M12 7 V12 L16 14"
		case "eye":
			return "M2.5 12 S6 6 12 6 S21.5 12 21.5 12 S18 18 12 18 S2.5 12 2.5 12 Z M12 9 A3 3 0 1 1 12 15 A3 3 0 0 1 12 9 Z"
		case "eye-off":
			return "M3 3 L21 21 M10.6 6.2 A9.8 9.8 0 0 1 12 6 C18 6 21.5 12 21.5 12 A15 15 0 0 1 18.8 15.3 M14.2 14.2 A3 3 0 0 1 9.8 9.8 M6.1 6.8 A15 15 0 0 0 2.5 12 S6 18 12 18 A9 9 0 0 0 14 17.8"
		case "info":
			return "M12 3 A9 9 0 1 1 12 21 A9 9 0 0 1 12 3 Z M12 10 V17 M12 7 H12.01"
		case "join":
			return "M13 5 H20 V19 H13 M4 12 H15 M11 8 L15 12 L11 16"
		case "key":
			return "M14 8 A5 5 0 1 1 9 13 L3 19 V21 H6 L8 19 H10 L12 17 V14"
		case "link":
			return "M9 15 L7.5 16.5 A4 4 0 0 1 1.8 10.8 L5.3 7.3 A4 4 0 0 1 11 7 M15 9 L16.5 7.5 A4 4 0 0 1 22.2 13.2 L18.7 16.7 A4 4 0 0 1 13 17 M8 12 H16"
		case "menu":
			return "M4 7 H20 M4 12 H20 M4 17 H20"
		case "message":
			return "M4 4 H20 V17 H9 L4 21 Z M8 9 H16 M8 13 H14"
		case "minimize":
			return "M5 12 H19"
		case "microphone":
			return "M12 3 A3 3 0 0 1 15 6 V12 A3 3 0 0 1 9 12 V6 A3 3 0 0 1 12 3 Z M5 11 A7 7 0 0 0 19 11 M12 18 V21 M8 21 H16"
		case "more":
			return "M5 12 H5.01 M12 12 H12.01 M19 12 H19.01"
		case "move":
			return "M12 3 V21 M8 7 L12 3 L16 7 M8 17 L12 21 L16 17 M3 12 H21 M7 8 L3 12 L7 16 M17 8 L21 12 L17 16"
		case "mute":
			return "M9 9 V6 A3 3 0 0 1 14.7 4.7 M15 10 V11 A3 3 0 0 1 14.6 12.5 M5 11 A7 7 0 0 0 16.5 16.4 M19 11 A7 7 0 0 1 18.3 14 M12 18 V21 M8 21 H16 M3 3 L21 21"
		case "next":
			return "M9 6 L15 12 L9 18"
		case "pin":
			return "M8 3 H16 L15 8 L19 12 H13 V21 L11 19 V12 H5 L9 8 Z"
		case "pause":
			return "M9 5 V19 M15 5 V19"
		case "play":
			return "M8 5 L19 12 L8 19 Z"
		case "plugin":
			return "M8 3 H13 V7 A2 2 0 1 0 17 7 V12 H21 V17 H16 V21 H11 V17 A2 2 0 1 0 7 17 V12 H3 V7 H8 Z"
		case "previous":
			return "M15 6 L9 12 L15 18"
		case "quit":
			return "M12 3 V12 M7.1 5.8 A8 8 0 1 0 16.9 5.8"
		case "record":
			return "M12 5 A7 7 0 1 1 12 19 A7 7 0 0 1 12 5 Z M12 9 A3 3 0 1 1 12 15 A3 3 0 0 1 12 9 Z"
		case "refresh":
		case "retry":
			return "M20 7 V12 H15 M19 12 A7 7 0 1 1 17.9 6.2 L20 8"
		case "reply":
			return "M9 8 L4 12 L9 16 M5 12 H13 A7 7 0 0 1 20 19"
		case "screen-share":
			return "M3 4 H21 V16 H3 Z M8 21 H16 M12 16 V21 M9 10 L12 7 L15 10 M12 7 V14"
		case "search":
			return "M18 11 A7 7 0 1 1 4 11 A7 7 0 0 1 18 11 Z M16 16 L20 20"
		case "send":
			return "M3 4 L22 12 L3 20 L7 12 Z M7 12 H16"
		case "settings":
			return "M4 7 H10 M14 7 H20 M4 12 H15 M19 12 H20 M4 17 H7 M11 17 H20 M10 5 V9 M15 10 V14 M7 15 V19"
		case "shield":
			return "M12 3 L20 6 V11 C20 16 16.5 19.5 12 21 C7.5 19.5 4 16 4 11 V6 Z M9 12 L11 14 L15 10"
		case "terminal":
			return "M4 5 H20 V19 H4 Z M7 9 L10 12 L7 15 M12 15 H17"
		case "text-room":
			return "M9 3 L7 21 M17 3 L15 21 M4 9 H20 M3 15 H19"
		case "unlink":
			return "M3 3 L21 21 M8.5 15.5 L7.5 16.5 A4 4 0 0 1 1.8 10.8 L5.3 7.3 A4 4 0 0 1 8.4 6.2 M15.6 17.8 A4 4 0 0 0 18.7 16.7 L22.2 13.2 A4 4 0 0 0 16.5 7.5 L15.5 8.5"
		case "user":
			return "M12 4 A4 4 0 1 1 12 12 A4 4 0 0 1 12 4 Z M4 21 A8 7 0 0 1 20 21"
		case "user-add":
			return "M9 4 A4 4 0 1 1 9 12 A4 4 0 0 1 9 4 Z M2 21 A7 7 0 0 1 14 16 M18 8 V16 M14 12 H22"
		case "user-remove":
			return "M9 4 A4 4 0 1 1 9 12 A4 4 0 0 1 9 4 Z M2 21 A7 7 0 0 1 14 16 M14 12 H22"
		case "voice-room":
		case "volume":
			return "M4 10 V14 H8 L13 18 V6 L8 10 Z M16 9 A4 4 0 0 1 16 15 M18.5 6.5 A8 8 0 0 1 18.5 17.5"
		case "volume-off":
			return "M4 10 V14 H8 L13 18 V6 L8 10 Z M17 9 L21 13 M21 9 L17 13"
		case "warning":
			return "M12 3 L22 20 H2 Z M12 9 V14 M12 17 H12.01"
		default:
			return ""
		}
	}

	readonly property string pathData: pathForName(name)

	implicitWidth: size
	implicitHeight: size

	Shape {
		id: iconShape
		objectName: "modernIconShape"
		anchors.centerIn: parent
		width: 24
		height: 24
		scale: Math.max(0, root.size) / 24
		visible: root.pathData.length > 0 && root.name !== "more" && root.size > 0
		antialiasing: true
		Accessible.ignored: true

		ShapePath {
			strokeColor: root.color
			strokeWidth: 2
			capStyle: ShapePath.RoundCap
			joinStyle: ShapePath.RoundJoin
			fillColor: "transparent"

			PathSvg {
				path: root.pathData
			}
		}
	}

	Row {
		id: moreIcon
		objectName: "modernMoreIcon"
		anchors.centerIn: parent
		visible: root.name === "more" && root.size > 0
		spacing: Math.max(2, Math.round(root.size / 6))
		Accessible.ignored: true

		Repeater {
			model: 3
			delegate: Rectangle {
				width: Math.max(2, Math.round(root.size / 8))
				height: width
				radius: width / 2
				color: root.color
			}
		}
	}
}
