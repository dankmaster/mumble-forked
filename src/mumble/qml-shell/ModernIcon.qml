import QtQuick
import QtQuick.Shapes
import Mumble.Theme 1.0

Item {
	id: root
	property string name: ""
	property color color: Theme.textStrong
	property int size: 18
	Accessible.ignored: true

	// Paths are adapted from Tabler Icons 3.45.0 (MIT), using its native
	// 24 x 24 grid and rounded 2 px outline language. Keeping the paths in one
	// component preserves theme-driven colors without rasterizing SVG assets.
	function pathForName(iconName) {
		switch (iconName) {
		case "activity":
			return "M3 12h4.5l1.5 -6l4 12l2 -9l1.5 3h4.5"
		case "action":
			return "M13 3l0 7l6 0l-8 11l0 -7l-6 0l8 -11"
		case "add":
			return "M12 5l0 14 M5 12l14 0"
		case "attach":
			return "M15 7l-6.5 6.5a1.5 1.5 0 0 0 3 3l6.5 -6.5a3 3 0 0 0 -6 -6l-6.5 6.5a4.5 4.5 0 0 0 9 9l6.5 -6.5"
		case "certificate":
			return "M12 15a3 3 0 1 0 6 0a3 3 0 1 0 -6 0 M13 17.5v4.5l2 -1.5l2 1.5v-4.5 M10 19h-5a2 2 0 0 1 -2 -2v-10c0 -1.1 .9 -2 2 -2h14a2 2 0 0 1 2 2v10a2 2 0 0 1 -1 1.73 M6 9l12 0 M6 12l3 0 M6 15l2 0"
		case "check":
			return "M5 12l5 5l10 -10"
		case "chevron-down":
			return "M6 9l6 6l6 -6"
		case "chevron-up":
			return "M6 15l6 -6l6 6"
		case "close":
			return "M18 6l-12 12 M6 6l12 12"
		case "connect":
			return "M7 12l5 5l-1.5 1.5a3.536 3.536 0 1 1 -5 -5l1.5 -1.5 M17 12l-5 -5l1.5 -1.5a3.536 3.536 0 1 1 5 5l-1.5 1.5 M3 21l2.5 -2.5 M18.5 5.5l2.5 -2.5 M10 11l-2 2 M13 14l-2 2"
		case "copy":
			return "M7 9.667a2.667 2.667 0 0 1 2.667 -2.667h8.666a2.667 2.667 0 0 1 2.667 2.667v8.666a2.667 2.667 0 0 1 -2.667 2.667h-8.666a2.667 2.667 0 0 1 -2.667 -2.667l0 -8.666 M4.012 16.737a2.005 2.005 0 0 1 -1.012 -1.737v-10c0 -1.1 .9 -2 2 -2h10c.75 0 1.158 .385 1.5 1"
		case "deafen":
			return "M4 15a2 2 0 0 1 2 -2h1a2 2 0 0 1 2 2v3a2 2 0 0 1 -2 2h-1a2 2 0 0 1 -2 -2l0 -3 M15 15a2 2 0 0 1 2 -2h1a2 2 0 0 1 2 2v3a2 2 0 0 1 -2 2h-1a2 2 0 0 1 -2 -2l0 -3 M4 15v-3a8 8 0 0 1 16 0v3"
		case "delete":
			return "M4 7l16 0 M10 11l0 6 M14 11l0 6 M5 7l1 12a2 2 0 0 0 2 2h8a2 2 0 0 0 2 -2l1 -12 M9 7v-3a1 1 0 0 1 1 -1h4a1 1 0 0 1 1 1v3"
		case "direct":
			return "M3 20l1.3 -3.9c-2.324 -3.437 -1.426 -7.872 2.1 -10.374c3.526 -2.501 8.59 -2.296 11.845 .48c3.255 2.777 3.695 7.266 1.029 10.501c-2.666 3.235 -7.615 4.215 -11.574 2.293l-4.7 1"
		case "disconnect":
			return "M20 16l-4 4 M7 12l5 5l-1.5 1.5a3.536 3.536 0 1 1 -5 -5l1.5 -1.5 M17 12l-5 -5l1.5 -1.5a3.536 3.536 0 1 1 5 5l-1.5 1.5 M3 21l2.5 -2.5 M18.5 5.5l2.5 -2.5 M10 11l-2 2 M13 14l-2 2 M16 16l4 4"
		case "download":
			return "M4 17v2a2 2 0 0 0 2 2h12a2 2 0 0 0 2 -2v-2 M7 11l5 5l5 -5 M12 4l0 12"
		case "edit":
			return "M7 7h-1a2 2 0 0 0 -2 2v9a2 2 0 0 0 2 2h9a2 2 0 0 0 2 -2v-1 M20.385 6.585a2.1 2.1 0 0 0 -2.97 -2.97l-8.415 8.385v3h3l8.385 -8.415 M16 5l3 3"
		case "external":
			return "M12 6h-6a2 2 0 0 0 -2 2v10a2 2 0 0 0 2 2h10a2 2 0 0 0 2 -2v-6 M11 13l9 -9 M15 4h5v5"
		case "fullscreen":
			return "M4 8v-2a2 2 0 0 1 2 -2h2 M4 16v2a2 2 0 0 0 2 2h2 M16 4h2a2 2 0 0 1 2 2v2 M16 20h2a2 2 0 0 0 2 -2v-2"
		case "fullscreen-exit":
			return "M15 19v-2a2 2 0 0 1 2 -2h2 M15 5v2a2 2 0 0 0 2 2h2 M5 15h2a2 2 0 0 1 2 2v2 M5 9h2a2 2 0 0 0 2 -2v-2"
		case "history":
			return "M12 8l0 4l2 2 M3.05 11a9 9 0 1 1 .5 4m-.5 5v-5h5"
		case "eye":
			return "M10 12a2 2 0 1 0 4 0a2 2 0 0 0 -4 0 M21 12c-2.4 4 -5.4 6 -9 6c-3.6 0 -6.6 -2 -9 -6c2.4 -4 5.4 -6 9 -6c3.6 0 6.6 2 9 6"
		case "eye-off":
			return "M10.585 10.587a2 2 0 0 0 2.829 2.828 M16.681 16.673a8.717 8.717 0 0 1 -4.681 1.327c-3.6 0 -6.6 -2 -9 -6c1.272 -2.12 2.712 -3.678 4.32 -4.674m2.86 -1.146a9.055 9.055 0 0 1 1.82 -.18c3.6 0 6.6 2 9 6c-.666 1.11 -1.379 2.067 -2.138 2.87 M3 3l18 18"
		case "info":
			return "M3 12a9 9 0 1 0 18 0a9 9 0 0 0 -18 0 M12 9h.01 M11 12h1v4h1"
		case "join":
			return "M9 8v-2a2 2 0 0 1 2 -2h7a2 2 0 0 1 2 2v12a2 2 0 0 1 -2 2h-7a2 2 0 0 1 -2 -2v-2 M3 12h13l-3 -3 M13 15l3 -3"
		case "key":
			return "M16.555 3.843l3.602 3.602a2.877 2.877 0 0 1 0 4.069l-2.643 2.643a2.877 2.877 0 0 1 -4.069 0l-.301 -.301l-6.558 6.558a2 2 0 0 1 -1.239 .578l-.175 .008h-1.172a1 1 0 0 1 -.993 -.883l-.007 -.117v-1.172a2 2 0 0 1 .467 -1.284l.119 -.13l.414 -.414h2v-2h2v-2l2.144 -2.144l-.301 -.301a2.877 2.877 0 0 1 0 -4.069l2.643 -2.643a2.877 2.877 0 0 1 4.069 0 M15 9h.01"
		case "link":
			return "M9 15l6 -6 M11 6l.463 -.536a5 5 0 0 1 7.071 7.072l-.534 .464 M13 18l-.397 .534a5.068 5.068 0 0 1 -7.127 0a4.972 4.972 0 0 1 0 -7.071l.524 -.463"
		case "menu":
			return "M4 6l16 0 M4 12l16 0 M4 18l16 0"
		case "message":
			return "M8 9h8 M8 13h6 M18 4a3 3 0 0 1 3 3v8a3 3 0 0 1 -3 3h-5l-5 3v-3h-2a3 3 0 0 1 -3 -3v-8a3 3 0 0 1 3 -3h12"
		case "minimize":
			return "M5 12l14 0"
		case "microphone":
			return "M9 5a3 3 0 0 1 3 -3a3 3 0 0 1 3 3v5a3 3 0 0 1 -3 3a3 3 0 0 1 -3 -3l0 -5 M5 10a7 7 0 0 0 14 0 M8 21l8 0 M12 17l0 4"
		case "more":
			return "M4 12a1 1 0 1 0 2 0a1 1 0 1 0 -2 0 M11 12a1 1 0 1 0 2 0a1 1 0 1 0 -2 0 M18 12a1 1 0 1 0 2 0a1 1 0 1 0 -2 0"
		case "move":
			return "M18 9l3 3l-3 3 M15 12h6 M6 9l-3 3l3 3 M3 12h6 M9 18l3 3l3 -3 M12 15v6 M15 6l-3 -3l-3 3 M12 3v6"
		case "mute":
			return "M3 3l18 18 M9 5a3 3 0 0 1 6 0v5a3 3 0 0 1 -.13 .874m-2 2a3 3 0 0 1 -3.87 -2.872v-1 M5 10a7 7 0 0 0 10.846 5.85m2 -2a6.967 6.967 0 0 0 1.152 -3.85 M8 21l8 0 M12 17l0 4"
		case "next":
			return "M9 6l6 6l-6 6"
		case "pin":
			return "M15 4.5l-4 4l-4 1.5l-1.5 1.5l7 7l1.5 -1.5l1.5 -4l4 -4 M9 15l-4.5 4.5 M14.5 4l5.5 5.5"
		case "pause":
			return "M6 6a1 1 0 0 1 1 -1h2a1 1 0 0 1 1 1v12a1 1 0 0 1 -1 1h-2a1 1 0 0 1 -1 -1l0 -12 M14 6a1 1 0 0 1 1 -1h2a1 1 0 0 1 1 1v12a1 1 0 0 1 -1 1h-2a1 1 0 0 1 -1 -1l0 -12"
		case "play":
			return "M7 4v16l13 -8l-13 -8"
		case "plugin":
			return "M4 7h3a1 1 0 0 0 1 -1v-1a2 2 0 0 1 4 0v1a1 1 0 0 0 1 1h3a1 1 0 0 1 1 1v3a1 1 0 0 0 1 1h1a2 2 0 0 1 0 4h-1a1 1 0 0 0 -1 1v3a1 1 0 0 1 -1 1h-3a1 1 0 0 1 -1 -1v-1a2 2 0 0 0 -4 0v1a1 1 0 0 1 -1 1h-3a1 1 0 0 1 -1 -1v-3a1 1 0 0 1 1 -1h1a2 2 0 0 0 0 -4h-1a1 1 0 0 1 -1 -1v-3a1 1 0 0 1 1 -1"
		case "previous":
			return "M15 6l-6 6l6 6"
		case "quit":
			return "M7 6a7.75 7.75 0 1 0 10 0 M12 4l0 8"
		case "record":
			return "M11 12a1 1 0 1 0 2 0a1 1 0 1 0 -2 0 M3 12a9 9 0 1 0 18 0a9 9 0 1 0 -18 0"
		case "reaction":
			return "M8 14s1.5 2 4 2s4 -2 4 -2 M9 9h.01 M15 9h.01 M16 5h6 M19 2v6 M21 12a9 9 0 1 1 -9 -9"
		case "refresh":
		case "retry":
			return "M20 11a8.1 8.1 0 0 0 -15.5 -2m-.5 -4v4h4 M4 13a8.1 8.1 0 0 0 15.5 2m.5 4v-4h-4"
		case "reply":
			return "M9 14l-4 -4l4 -4 M5 10h11a4 4 0 1 1 0 8h-1"
		case "screen-share":
			return "M21 12v3a1 1 0 0 1 -1 1h-16a1 1 0 0 1 -1 -1v-10a1 1 0 0 1 1 -1h9 M7 20l10 0 M9 16l0 4 M15 16l0 4 M17 4h4v4 M16 9l5 -5"
		case "search":
			return "M3 10a7 7 0 1 0 14 0a7 7 0 1 0 -14 0 M21 21l-6 -6"
		case "send":
			return "M10 14l11 -11 M21 3l-6.5 18a.55 .55 0 0 1 -1 0l-3.5 -7l-7 -3.5a.55 .55 0 0 1 0 -1l18 -6.5"
		case "settings":
			return "M12 6a2 2 0 1 0 4 0a2 2 0 1 0 -4 0 M4 6l8 0 M16 6l4 0 M6 12a2 2 0 1 0 4 0a2 2 0 1 0 -4 0 M4 12l2 0 M10 12l10 0 M15 18a2 2 0 1 0 4 0a2 2 0 1 0 -4 0 M4 18l11 0 M19 18l1 0"
		case "shield":
			return "M11.46 20.846a12 12 0 0 1 -7.96 -14.846a12 12 0 0 0 8.5 -3a12 12 0 0 0 8.5 3a12 12 0 0 1 -.09 7.06 M15 19l2 2l4 -4"
		case "terminal":
			return "M8 9l3 3l-3 3 M13 15l3 0 M3 6a2 2 0 0 1 2 -2h14a2 2 0 0 1 2 2v12a2 2 0 0 1 -2 2h-14a2 2 0 0 1 -2 -2l0 -12"
		case "text-room":
			return "M5 9l14 0 M5 15l14 0 M11 4l-4 16 M17 4l-4 16"
		case "unlink":
			return "M17 22v-2 M9 15l6 -6 M11 6l.463 -.536a5 5 0 0 1 7.071 7.072l-.534 .464 M13 18l-.397 .534a5.068 5.068 0 0 1 -7.127 0a4.972 4.972 0 0 1 0 -7.071l.524 -.463 M20 17h2 M2 7h2 M7 2v2"
		case "user":
			return "M8 7a4 4 0 1 0 8 0a4 4 0 0 0 -8 0 M6 21v-2a4 4 0 0 1 4 -4h4a4 4 0 0 1 4 4v2"
		case "user-add":
			return "M8 7a4 4 0 1 0 8 0a4 4 0 0 0 -8 0 M16 19h6 M19 16v6 M6 21v-2a4 4 0 0 1 4 -4h4"
		case "user-remove":
			return "M8 7a4 4 0 1 0 8 0a4 4 0 0 0 -8 0 M6 21v-2a4 4 0 0 1 4 -4h4c.348 0 .686 .045 1.009 .128 M16 19h6"
		case "voice-room":
		case "volume":
			return "M15 8a5 5 0 0 1 0 8 M17.7 5a9 9 0 0 1 0 14 M6 15h-2a1 1 0 0 1 -1 -1v-4a1 1 0 0 1 1 -1h2l3.5 -4.5a.8 .8 0 0 1 1.5 .5v14a.8 .8 0 0 1 -1.5 .5l-3.5 -4.5"
		case "volume-off":
			return "M15 8a5 5 0 0 1 1.912 4.934m-1.377 2.602a5 5 0 0 1 -.535 .464 M17.7 5a9 9 0 0 1 2.362 11.086m-1.676 2.299a9 9 0 0 1 -.686 .615 M9.069 5.054l.431 -.554a.8 .8 0 0 1 1.5 .5v2m0 4v8a.8 .8 0 0 1 -1.5 .5l-3.5 -4.5h-2a1 1 0 0 1 -1 -1v-4a1 1 0 0 1 1 -1h2l1.294 -1.664 M3 3l18 18"
		case "warning":
			return "M12 9v4 M10.363 3.591l-8.106 13.534a1.914 1.914 0 0 0 1.636 2.871h16.214a1.914 1.914 0 0 0 1.636 -2.87l-8.106 -13.536a1.914 1.914 0 0 0 -3.274 0 M12 16h.01"
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
		visible: root.pathData.length > 0 && root.size > 0
		antialiasing: true
		preferredRendererType: Shape.CurveRenderer
		Accessible.ignored: true

		ShapePath {
			strokeColor: root.color
			// Optical compensation keeps 11-16 px glyphs from becoming hairlines.
			strokeWidth: root.size <= 16 ? 2.25 : 2
			capStyle: ShapePath.RoundCap
			joinStyle: ShapePath.RoundJoin
			fillColor: "transparent"

			PathSvg {
				path: root.pathData
			}
		}
	}

}
