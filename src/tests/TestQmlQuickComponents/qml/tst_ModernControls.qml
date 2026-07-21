import QtQuick
import QtQuick.Controls
import QtTest
import Mumble.Theme 1.0

TestCase {
	id: testCase
	name: "ModernControls"
	when: windowShown
	visible: true
	width: 420
	height: 260

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
		Loader { id: progressLoader; width: 240; source: "qrc:/qml-shell/ModernProgressBar.qml" }
	}

	function initTestCase() {
		tryVerify(function() {
			return iconLoader.item !== null && fieldLoader.item !== null && checkLoader.item !== null
				&& comboLoader.item !== null && sliderLoader.item !== null && areaLoader.item !== null
				&& spinLoader.item !== null && progressLoader.item !== null
		})
	}

	function test_icon_button_selection_and_focus() {
		fieldLoader.item.forceActiveFocus(Qt.OtherFocusReason)
		iconLoader.item.text = "⋯"
		iconLoader.item.selected = true
		iconLoader.item.forceActiveFocus(Qt.TabFocusReason)
		tryCompare(iconLoader.item, "activeFocus", true)
		compare(iconLoader.item.visualFocus, true)
		tryCompare(iconLoader.item.background, "color", Theme.accentSubtle, 500)
		compare(iconLoader.item.background.border.width, Theme.focusRingWidth)
	}

	function test_icon_button_pointer_focus_does_not_linger_visually() {
		const button = iconLoader.item
		button.enabled = true
		button.selected = false
		button.overlay = false
		testCase.forceActiveFocus(Qt.OtherFocusReason)
		mouseClick(button)
		tryCompare(button, "activeFocus", true)
		compare(button.visualFocus, false)
		tryCompare(button.background.border, "color", Qt.rgba(0, 0, 0, 0), 500)
		compare(button.background.border.width, 1)
	}

	function test_disabled_icon_button_overrides_selected_accent() {
		iconLoader.item.selected = true
		iconLoader.item.enabled = false
		tryCompare(iconLoader.item.background, "color", Theme.panel, 500)
		compare(iconLoader.item.background.border.color, Theme.divider)
		compare(iconLoader.item.contentItem.foreground, Theme.textMuted)
		iconLoader.item.enabled = true
		iconLoader.item.selected = false
	}

	function test_icon_button_overlay_opt_in_uses_media_foreground_tokens() {
		const button = iconLoader.item
		button.overlay = true
		button.enabled = true
		button.selected = true
		compare(String(button.contentItem.foreground), String(Theme.mediaOverlayTextStrong))

		button.enabled = false
		compare(String(button.contentItem.foreground), String(Theme.mediaOverlayTextMuted))
		tryCompare(button.background, "color", Qt.rgba(0, 0, 0, 0), 500)
		compare(String(button.background.border.color), String(Qt.rgba(0, 0, 0, 0)))

		button.enabled = true
		button.selected = false
		button.overlay = false
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

	function test_spin_box_focus_leaf_inherits_product_semantics() {
		const spin = spinLoader.item
		spin.Accessible.name = "Jitter buffer"
		spin.Accessible.description = "Playback buffering steps"
		spin.editable = true
		spin.forceActiveFocus()
		tryCompare(spin, "activeFocus", true)
		tryCompare(spin.contentItem, "activeFocus", true)
		compare(spin.contentItem.Accessible.role, Accessible.SpinBox)
		compare(spin.contentItem.Accessible.name, "Jitter buffer")
		compare(spin.contentItem.Accessible.description, "Playback buffering steps")
	}

	function test_combo_popup_delegate_enables_explicit_hover_state() {
		comboLoader.item.enabled = true
		comboLoader.item.toolTipText = "Explains the selected control"
		compare(comboLoader.item.toolTipText, "Explains the selected control")
		comboLoader.item.model = ["One", "Two"]
		comboLoader.item.popup.open()
		tryVerify(function() {
			return comboLoader.item.popup.contentItem.itemAtIndex(0) !== null
		})
		const delegate = comboLoader.item.popup.contentItem.itemAtIndex(0)
		verify(delegate.hoverEnabled)
		comboLoader.item.popup.close()
	}

	function test_disabled_controls_use_muted_theme_tokens() {
		fieldLoader.item.enabled = false
		areaLoader.item.enabled = false
		checkLoader.item.checked = true
		checkLoader.item.enabled = false
		sliderLoader.item.enabled = false
		comboLoader.item.enabled = false
		spinLoader.item.enabled = false

		compare(fieldLoader.item.color, Theme.textMuted)
		compare(areaLoader.item.color, Theme.textMuted)
		tryCompare(checkLoader.item.indicator, "color", Theme.surfaceBorder, 500)
		compare(sliderLoader.item.handle.color, Theme.textMuted)
		compare(comboLoader.item.contentItem.color, Theme.textMuted)
		compare(spinLoader.item.contentItem.color, Theme.textMuted)
		compare(spinLoader.item.background.border.color, Theme.divider)
		compare(spinLoader.item.up.indicator.color.a, 0)
		compare(spinLoader.item.down.indicator.color.a, 0)

		fieldLoader.item.enabled = true
		areaLoader.item.enabled = true
		checkLoader.item.enabled = true
		sliderLoader.item.enabled = true
		comboLoader.item.enabled = true
		spinLoader.item.enabled = true
	}

	function test_progress_uses_bounded_tokenized_indicators() {
		const progress = progressLoader.item
		progress.from = 0
		progress.to = 100
		progress.value = 40
		progress.indeterminate = false
		const fill = findChild(progress, "modernProgressDeterminateFill")
		const pill = findChild(progress, "modernProgressIndeterminatePill")
		verify(fill !== null && pill !== null)
		tryVerify(function() { return fill.width > 90 && fill.width < 102 })
		compare(fill.color, Theme.accent)
		verify(!pill.visible)

		progress.tone = "warning"
		progress.indeterminate = true
		tryCompare(progress, "indeterminate", true)
		tryCompare(pill, "visible", true)
		verify(pill.width >= 28, "The indeterminate indicator must keep a usable minimum width")
		verify(pill.width <= 72, "The indeterminate indicator must stay bounded")
		compare(pill.color, Theme.warning)
		compare(progress.Accessible.role, Accessible.ProgressBar)
	}

	function test_progress_animation_defaults_on_and_fixture_mode_is_deterministic() {
		const progress = progressLoader.item
		const pill = findChild(progress, "modernProgressIndeterminatePill")
		verify(pill !== null)
		compare(progress.animated, true)

		progress.indeterminate = true
		tryCompare(pill, "visible", true)
		progress.animated = false
		const expectedCenter = Math.round((pill.parent.width - pill.width) / 2)
		tryCompare(pill, "x", expectedCenter)
		const frozenX = pill.x
		wait(120)
		compare(pill.x, frozenX)

		progress.indeterminate = false
		progress.value = 75
		const fill = findChild(progress, "modernProgressDeterminateFill")
		verify(fill !== null)
		tryVerify(function() {
			return Math.abs(fill.width - fill.parent.width * progress.visualPosition) < 0.5
		})
		progress.animated = true
	}

	function test_density_tokens_define_three_distinct_product_rhythms() {
		compare(Theme.densityMetricFor("compact", 31, 36, 42), 31)
		compare(Theme.densityMetricFor("comfortable", 31, 36, 42), 36)
		compare(Theme.densityMetricFor("spacious", 31, 36, 42), 42)
		compare(Theme.densityMetricFor("unknown", 31, 36, 42), 36)
		verify(Theme.densityMetricFor("compact", 32, 36, 40)
			< Theme.densityMetricFor("comfortable", 32, 36, 40))
		verify(Theme.densityMetricFor("comfortable", 32, 36, 40)
			< Theme.densityMetricFor("spacious", 32, 36, 40))
	}
}
