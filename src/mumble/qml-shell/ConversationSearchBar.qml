import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

Rectangle {
	id: root

	property var timelineModel: null
	property bool narrowLayout: false
	property bool accessibilitySuppressed: false
	property bool visualFixtureMode: false
	Component {
		id: searchTextCursorDelegate
		// Keep the real editable-text focus contract in deterministic captures,
		// while preventing the platform cursor blink timer from changing the frame.
		Item {
			width: 1
			Rectangle {
				objectName: "conversationSearchCursorPaint"
				anchors.fill: parent
				visible: !root.visualFixtureMode
				color: Theme.textStrong
			}
		}
	}
	readonly property bool hasQuery: searchField.text.trim().length > 0
	readonly property int matchCount: timelineModel ? Number(timelineModel.matchCount || 0) : 0
	readonly property int currentMatchIndex: timelineModel
		? Number(timelineModel.currentMatchIndex === undefined
			? -1 : timelineModel.currentMatchIndex) : -1
	readonly property int currentMatchRow: timelineModel
		? Number(timelineModel.currentMatchRow === undefined
			? -1 : timelineModel.currentMatchRow) : -1
	readonly property string currentMatchStableId: timelineModel
		? String(timelineModel.currentMatchStableId || "") : ""
	readonly property string resultLabel: !hasQuery ? qsTr("Type to search")
		: matchCount <= 0 ? qsTr("No matches")
		: qsTr("%1 of %2").arg(currentMatchIndex + 1).arg(matchCount)

	signal closeRequested()
	signal currentMatchRequested(int row, string stableId)

	objectName: "conversationSearchBar"
	implicitHeight: 52
	color: Theme.panel
	border.color: Theme.divider
	border.width: 0
	Accessible.role: Accessible.Grouping
	Accessible.name: qsTr("Search this conversation")
	Accessible.ignored: accessibilitySuppressed

	function commitQuery() {
		queryDebounce.stop()
		if (!timelineModel)
			return false
		const nextQuery = searchField.text.slice(0, 512)
		const changed = String(timelineModel.query || "") !== nextQuery
		if (changed)
			timelineModel.query = nextQuery
		return changed
	}

	function requestCurrentMatch() {
		if (currentMatchRow >= 0 && currentMatchStableId.length > 0)
			currentMatchRequested(currentMatchRow, currentMatchStableId)
	}

	function navigateMatch(direction) {
		if (!timelineModel)
			return
		const queryChanged = commitQuery()
		if (!queryChanged && matchCount > 0) {
			if (direction < 0)
				timelineModel.previousMatch()
			else
				timelineModel.nextMatch()
		} else {
			requestCurrentMatch()
		}
	}

	function activate() {
		if (timelineModel)
			searchField.text = String(timelineModel.query || "")
		searchField.forceActiveFocus(Qt.ShortcutFocusReason)
		searchField.selectAll()
	}

	function reset() {
		queryDebounce.stop()
		searchField.text = ""
		if (timelineModel && typeof timelineModel.clearSearch === "function")
			timelineModel.clearSearch()
	}

	Connections {
		target: root.timelineModel
		// Lightweight fixture/read-only list models can render a conversation
		// without implementing the optional typed search contract. The owning
		// surface hides search in that case, so missing search signals are benign.
		ignoreUnknownSignals: true
		function onCurrentMatchChanged() { root.requestCurrentMatch() }
		function onQueryChanged() {
			if (!searchField.activeFocus) {
				const modelQuery = String(root.timelineModel ? root.timelineModel.query || "" : "")
				if (searchField.text !== modelQuery)
					searchField.text = modelQuery
			}
		}
	}

	Timer {
		id: queryDebounce
		interval: 120
		repeat: false
		onTriggered: root.commitQuery()
	}

	Rectangle {
		anchors.left: parent.left
		anchors.right: parent.right
		anchors.bottom: parent.bottom
		height: 1
		color: Theme.divider
		Accessible.ignored: true
	}

	RowLayout {
		anchors.fill: parent
		anchors.leftMargin: root.narrowLayout ? Theme.space3 : Theme.space5
		anchors.rightMargin: root.narrowLayout ? Theme.space2 : Theme.space4
		anchors.topMargin: Theme.space1
		anchors.bottomMargin: Theme.space1
		spacing: Theme.space2

		ModernTextField {
			id: searchField
			objectName: "conversationSearchField"
			cursorDelegate: searchTextCursorDelegate
			Layout.fillWidth: true
			Layout.minimumWidth: 96
			dense: true
			activeFocusOnTab: true
			leftPadding: 34
			rightPadding: Theme.space3
			placeholderText: qsTr("Search messages")
			Accessible.name: qsTr("Search messages in this conversation")
			Accessible.description: root.hasQuery ? root.resultLabel
				: qsTr("Search message text, senders, replies, and attachment names")
			Accessible.ignored: root.accessibilitySuppressed
			onTextEdited: queryDebounce.restart()
			Keys.onPressed: event => {
				if (event.key === Qt.Key_Escape) {
					root.closeRequested()
					event.accepted = true
				} else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
					root.navigateMatch((event.modifiers & Qt.ShiftModifier) ? -1 : 1)
					event.accepted = true
				}
			}

			ModernIcon {
				anchors.left: parent.left
				anchors.leftMargin: 11
				anchors.verticalCenter: parent.verticalCenter
				name: "search"
				size: 14
				color: searchField.activeFocus ? Theme.accent : Theme.textMuted
				Accessible.ignored: true
			}
		}

		Label {
			id: resultCountLabel
			objectName: "conversationSearchResultCount"
			Layout.preferredWidth: root.narrowLayout ? 68 : 86
			Layout.maximumWidth: Layout.preferredWidth
			textFormat: Text.PlainText
			text: root.resultLabel
			color: root.hasQuery && root.matchCount <= 0 ? Theme.warning : Theme.textMuted
			font.pixelSize: Theme.fontCaption
			font.weight: Font.Medium
			horizontalAlignment: Text.AlignRight
			elide: Text.ElideRight
			Accessible.role: Accessible.StaticText
			Accessible.name: text
			Accessible.ignored: root.accessibilitySuppressed
		}

		ModernIconButton {
			id: previousButton
			objectName: "conversationSearchPrevious"
			iconName: "previous"
			dense: true
			enabled: root.matchCount > 0
			Accessible.name: qsTr("Previous match")
			Accessible.description: root.resultLabel
			Accessible.ignored: root.accessibilitySuppressed
			onClicked: root.navigateMatch(-1)
		}

		ModernIconButton {
			id: nextButton
			objectName: "conversationSearchNext"
			iconName: "next"
			dense: true
			enabled: root.matchCount > 0
			Accessible.name: qsTr("Next match")
			Accessible.description: root.resultLabel
			Accessible.ignored: root.accessibilitySuppressed
			onClicked: root.navigateMatch(1)
		}

		ModernIconButton {
			id: closeButton
			objectName: "conversationSearchClose"
			iconName: "close"
			dense: true
			Accessible.name: qsTr("Close conversation search")
			Accessible.ignored: root.accessibilitySuppressed
			onClicked: root.closeRequested()
		}
	}
}
