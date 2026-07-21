import QtQuick
import QtQuick.Controls
import QtTest
import Mumble.Theme 1.0

TestCase {
    id: testCase
    name: "ModernButton"
    when: windowShown
    width: 360
    height: 180

    Loader {
        id: loader
        anchors.centerIn: parent
        source: "qrc:/qml-shell/ModernButton.qml"
        onLoaded: {
            item.text = "Continue";
            item.Accessible.name = "Continue";
        }
    }

    SignalSpy {
        id: clickedSpy
        target: loader.item
        signalName: "clicked"
    }

    function init() {
        verify(loader.item !== null);
        clickedSpy.clear();
		loader.item.tone = "neutral";
		loader.item.highlighted = false;
		loader.item.checked = false;
		loader.item.enabled = true;
		loader.item.forceActiveFocus(Qt.TabFocusReason);
        tryCompare(loader.item, "activeFocus", true);
    }

    function test_focus_keyboard_and_accessibility() {
		compare(loader.item.visualFocus, true);
        compare(loader.item.Accessible.role, Accessible.Button);
        compare(loader.item.Accessible.name, "Continue");
        keyClick(Qt.Key_Space);
        compare(clickedSpy.count, 1);
    }

	function test_pointer_activation_does_not_leave_a_focus_ring() {
		testCase.forceActiveFocus(Qt.OtherFocusReason);
		mouseClick(loader.item);
		// Some Qt Quick styles do not assign mouse focus to text buttons by default.
		// Exercise the mouse-focus visual contract explicitly in either case.
		loader.item.forceActiveFocus(Qt.MouseFocusReason);
		tryCompare(loader.item, "activeFocus", true);
		compare(loader.item.visualFocus, false);
		compare(loader.item.background.border.width, 1);
	}

	function test_return_and_keypad_enter_activate_once() {
		keyClick(Qt.Key_Return)
		compare(clickedSpy.count, 1)
		clickedSpy.clear()
		keyClick(Qt.Key_Enter)
		compare(clickedSpy.count, 1)
	}

	function test_tone_and_checked_states_use_design_tokens() {
		loader.item.tone = "danger"
		verify(loader.item.emphasized)
		compare(loader.item.toneColor, Theme.danger)
		tryCompare(loader.item.background, "color", Theme.danger, 500)
		compare(loader.item.contentItem.color, Theme.contrastText(Theme.danger))
		loader.item.tone = "neutral"
		loader.item.checked = true
		verify(loader.item.emphasized)
		tryCompare(loader.item.background, "color", Theme.accent, 500)
		compare(loader.item.contentItem.color, Theme.contrastText(Theme.accent))
	}

	function test_primary_hover_token_and_disabled_state_are_complete() {
		loader.item.tone = "primary"
		compare(loader.item.hoverToneColor, Theme.accentHover)
		loader.item.enabled = false
		tryCompare(loader.item.background, "color", Theme.panel, 500)
		tryCompare(loader.item.background.border, "color", Theme.divider, 500)
		compare(loader.item.contentItem.color, Theme.textMuted)
		loader.item.enabled = true
	}
}
