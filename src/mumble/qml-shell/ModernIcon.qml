import QtQuick
import QtQuick.Shapes
import Mumble.Theme 1.0

Item {
	id: root
	property string name: ""
	property color color: Theme.textStrong
	property int size: 18

	function pathForName(iconName) {
		switch (iconName) {
		case "activity":
			return "M3 12 H7 L9.5 5 L14.5 19 L17 12 H21"
		case "attach":
			return "M21.4 11.6 L12 21 A6 6 0 0 1 3.5 12.5 L13 3 A4 4 0 0 1 18.7 8.7 L9.2 18.2 A2 2 0 0 1 6.4 15.4 L15.2 6.6"
		case "check":
			return "M5 12 L9 16 L19 6"
		case "chevron-down":
			return "M6 9 L12 15 L18 9"
		case "chevron-up":
			return "M6 15 L12 9 L18 15"
		case "close":
			return "M6 6 L18 18 M18 6 L6 18"
		case "deafen":
			return "M4 14 V12 A8 8 0 0 1 20 12 V14 M4 14 H7 V20 H5 A1 1 0 0 1 4 19 Z M20 14 H17 V20 H19 A1 1 0 0 0 20 19 Z"
		case "direct":
			return "M20 11.5 A8 8 0 0 1 11.5 19.5 A8.5 8.5 0 1 1 20 11.5 Z M7 20 L4 21 L5 18"
		case "menu":
			return "M4 7 H20 M4 12 H20 M4 17 H20"
		case "microphone":
			return "M12 3 A3 3 0 0 1 15 6 V12 A3 3 0 0 1 9 12 V6 A3 3 0 0 1 12 3 Z M5 11 A7 7 0 0 0 19 11 M12 18 V21 M8 21 H16"
		case "more":
			return "M5 12 H5.01 M12 12 H12.01 M19 12 H19.01"
		case "next":
			return "M9 6 L15 12 L9 18"
		case "previous":
			return "M15 6 L9 12 L15 18"
		case "external":
			return "M14 5 H19 V10 M19 5 L11 13 M18 13 V19 H5 V6 H11"
		case "play":
			return "M8 5 L19 12 L8 19 Z"
		case "warning":
			return "M12 3 L22 20 H2 Z M12 9 V14 M12 17 H12.01"
		case "mute":
			return "M9 9 V6 A3 3 0 0 1 14.7 4.7 M15 10 V11 A3 3 0 0 1 14.6 12.5 M5 11 A7 7 0 0 0 16.5 16.4 M19 11 A7 7 0 0 1 18.3 14 M12 18 V21 M8 21 H16 M3 3 L21 21"
		case "retry":
			return "M20 7 V12 H15 M19 12 A7 7 0 1 1 17.9 6.2 L20 8"
		case "search":
			return "M18 11 A7 7 0 1 1 4 11 A7 7 0 0 1 18 11 Z M16 16 L20 20"
		case "text-room":
			return "M9 3 L7 21 M17 3 L15 21 M4 9 H20 M3 15 H19"
		case "voice-room":
			return "M4 10 V14 H8 L13 18 V6 L8 10 Z M16 9 A4 4 0 0 1 16 15 M18.5 6.5 A8 8 0 0 1 18.5 17.5"
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
