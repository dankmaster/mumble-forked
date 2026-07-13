import QtQuick
import QtQuick.Controls
import QtTest
import Mumble.Theme 1.0

TestCase {
	id: testCase
	name: "ModernControls"
	when: windowShown
	width: 420
	height: 220

	Column {
		anchors.centerIn: parent
		spacing: 12
		Loader { id: iconLoader; source: "qrc:/qml-shell/ModernIconButton.qml" }
		Loader { id: fieldLoader; source: "qrc:/qml-shell/ModernTextField.qml" }
		Loader { id: checkLoader; source: "qrc:/qml-shell/ModernCheckBox.qml" }
		Loader { id: comboLoader; source: "qrc:/qml-shell/ModernComboBox.qml" }
		Loader { id: sliderLoader; source: "qrc:/qml-shell/ModernSlider.qml" }
		Loader { id: areaLoader; source: "qrc:/qml-shell/ModernTextArea.qml" }
		Loader { id: spinLoader; source: "qrc:/qml-shell/ModernSpinBox.qml" }
	}

	function initTestCase() {
		tryVerify(function() {
			return iconLoader.item !== null && fieldLoader.item !== null && checkLoader.item !== null
				&& comboLoader.item !== null && sliderLoader.item !== null && areaLoader.item !== null
				&& spinLoader.item !== null
		})
	}

	function test_icon_button_selection_and_focus() {
		iconLoader.item.text = "⋯"
		iconLoader.item.selected = true
		iconLoader.item.forceActiveFocus()
		tryCompare(iconLoader.item, "activeFocus", true)
		tryCompare(iconLoader.item.background, "color", Theme.accentSubtle, 500)
		compare(iconLoader.item.background.border.width, Theme.focusRingWidth)
	}

	function test_text_field_validation_state() {
		fieldLoader.item.invalid = true
		tryCompare(fieldLoader.item.background.border, "color", Theme.danger, 500)
		compare(fieldLoader.item.background.border.width, Theme.focusRingWidth)
	}

	function test_checkbox_checked_state() {
		checkLoader.item.text = "Enabled"
		checkLoader.item.checked = true
		tryCompare(checkLoader.item.indicator, "color", Theme.accent, 500)
		compare(checkLoader.item.Accessible.role, Accessible.CheckBox)
	}

	function test_combo_slider_and_text_area_share_control_tokens() {
		comboLoader.item.model = ["One", "Two"]
		comboLoader.item.currentIndex = 1
		compare(comboLoader.item.displayText, "Two")
		compare(comboLoader.item.background.color, Theme.surfaceRaised)
		sliderLoader.item.value = 0.5
		compare(sliderLoader.item.handle.color, Theme.accent)
		areaLoader.item.invalid = true
		tryCompare(areaLoader.item.background.border, "color", Theme.danger, 500)
		spinLoader.item.from = 0
		spinLoader.item.to = 10
		spinLoader.item.value = 4
		compare(spinLoader.item.value, 4)
		compare(spinLoader.item.background.color, Theme.surfaceRaised)
	}
}
