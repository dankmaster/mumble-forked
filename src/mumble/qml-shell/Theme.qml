pragma Singleton
// uiTheme is a C++-owned root context property installed before this singleton loads.
// qmllint disable unqualified
import QtQuick

QtObject {
    readonly property color shellBackground: uiTheme ? uiTheme.shellBackground : "#20262f"
    readonly property color panel: uiTheme ? uiTheme.panel : "#262d38"
	readonly property color surfaceRaised: uiTheme ? uiTheme.surfaceRaised : "#2e3742"
	readonly property color surfaceHover: uiTheme ? uiTheme.surfaceHover : "#384453"
	readonly property color surfaceBorder: uiTheme ? uiTheme.surfaceBorder : "#384453"
	readonly property color mediaCanvas: uiTheme ? uiTheme.mediaCanvas : "#05070a"
    readonly property color rail: uiTheme ? uiTheme.rail : "#1b2027"
    readonly property color strip: uiTheme ? uiTheme.strip : "#14181f"
    readonly property color divider: uiTheme ? uiTheme.divider : "#12ffffff"
    readonly property color textStrong: uiTheme ? uiTheme.textStrong : "#e7ecf3"
    readonly property color textMain: uiTheme ? uiTheme.textMain : "#c3cbd6"
    readonly property color textMuted: uiTheme ? uiTheme.textMuted : "#8b94a3"
    readonly property color accent: uiTheme ? uiTheme.accent : "#5ec8b0"
	readonly property color accentHover: uiTheme ? uiTheme.accentHover : "#82ddca"
	readonly property color accentSubtle: uiTheme ? uiTheme.accentSubtle : "#295ec8b0"
    readonly property color selected: uiTheme ? uiTheme.selected : "#295ec8b0"
    readonly property color danger: uiTheme ? uiTheme.danger : "#ef4444"
    readonly property color success: uiTheme ? uiTheme.success : "#5fd0a3"
    readonly property color warning: uiTheme ? uiTheme.warning : "#e0c574"
	readonly property color focus: uiTheme ? uiTheme.focus : accent

	// Product-level semantic roles. Keep these derived from the canonical
	// uiTheme palette so built-in, accent-overridden and custom themes all gain
	// the same component vocabulary without growing their manifest schema.
	readonly property color chatCanvas: shellBackground
	readonly property color chatSurface: panel
	readonly property color chatIncomingSurface: panel
	readonly property color chatIncomingBorder: divider
	readonly property color chatHover: surfaceHover
	readonly property color chatOwnSurface: accentSubtle
	readonly property color chatOwnBorder: withAlpha(accent, 0.30)
	readonly property color chatMetadata: textMuted
	readonly property color chatReplySurface: strip
	readonly property color composerBackground: surfaceRaised
	readonly property color composerBorder: surfaceBorder
	readonly property color composerFocusBorder: focus
	readonly property color composerShadow: withAlpha(mediaCanvas, 0.28)
	readonly property color popupBackground: surfaceRaised
	readonly property color popupBorder: surfaceBorder
	readonly property color popupHover: surfaceHover
	readonly property color popupSelected: selected
	readonly property color selfCardBackground: rail
	readonly property color selfCardHover: surfaceRaised
	readonly property color selfCardBorder: divider
	readonly property color previewCardBackground: surfaceRaised
	readonly property color previewCardHover: surfaceHover
	readonly property color previewCardBorder: surfaceBorder
	readonly property color embedCanvas: mediaCanvas
	readonly property color embedSurface: panel
	readonly property color embedBorder: surfaceBorder
	readonly property color embedHover: surfaceHover
	readonly property color embedRevealSurface: strip
	readonly property color embedSelection: selected
	readonly property color embedOverlayBase: strip
	readonly property color mediaOverlayTextStrong: contrastText(mediaCanvas)
	readonly property color mediaOverlayTextMuted: mixColors(mediaOverlayTextStrong, mediaCanvas, 0.18)
	readonly property color secondaryText: mixColors(textMuted, textMain, 0.28)
	readonly property color microLabelText: mixColors(textMuted, textMain, 0.18)
	readonly property color quietBorder: withAlpha(surfaceBorder, 0.58)
	readonly property color bannerBackground: panel
	readonly property color bannerBorder: quietBorder
	readonly property bool lightAppearance: relativeLuminance(shellBackground) > 0.45
	readonly property color modalScrim: withAlpha(mediaCanvas, lightAppearance ? 0.42 : 0.54)
	readonly property color modalShadow: withAlpha(mediaCanvas, 0.64)
	readonly property color modalBorder: withAlpha(textStrong, 0.16)
	readonly property color elevationShadow: withAlpha(mediaCanvas, 0.46)
	readonly property color elevationHighlight: withAlpha(textStrong, 0.08)
	readonly property color onAccent: contrastText(accent)
	readonly property color textFaint: withAlpha(textMuted, 0.72)
    readonly property int shellRadius: uiTheme ? uiTheme.shellRadius : 16
    readonly property int innerRadius: uiTheme ? uiTheme.innerRadius : 11
    readonly property int spacing: uiTheme ? uiTheme.spacing : 12
	readonly property string themeId: uiTheme ? uiTheme.themeId : "dark"
	readonly property string densityId: uiTheme ? uiTheme.densityId : "comfortable"
	readonly property string accentId: uiTheme ? uiTheme.accentId : "auto"
	readonly property string railSide: uiTheme ? uiTheme.railSide : "right"
	readonly property bool compact: uiTheme ? uiTheme.compact : false
	readonly property bool spacious: densityId === "spacious"
	readonly property bool comfortable: !compact && !spacious

	// Density is a product-level choice, not only a dialog preference. Keep
	// comfortable at the established production measurements while giving
	// spacious its own rhythm across navigation, controls, type and avatars.
	readonly property int space1: densityMetric(4, 4, 6)
	readonly property int space2: densityMetric(6, 8, 10)
	readonly property int space3: densityMetric(8, 12, 16)
	readonly property int space4: densityMetric(12, 16, 22)
	readonly property int space5: densityMetric(16, 24, 32)
	readonly property int space6: densityMetric(24, 32, 42)
	readonly property int controlHeight: densityMetric(32, 36, 40)
	readonly property int rowHeight: densityMetric(38, 44, 50)
	readonly property int roomRowHeight: densityMetric(40, 40, 46)
	readonly property int roomRowHeightDetailed: densityMetric(48, 48, 56)
	readonly property int participantRowHeight: densityMetric(31, 36, 42)
	readonly property int railHeaderHeight: densityMetric(72, 80, 88)
	readonly property int railHeaderTopInset: densityMetric(8, 10, 12)
	readonly property int railHeaderSpacing: densityMetric(2, 3, 4)
	readonly property int railBadgeSize: densityMetric(32, 34, 38)
	readonly property int railBadgeRadius: densityMetric(10, 12, 14)
	readonly property int railFooterHeight: densityMetric(64, 68, 76)
	readonly property int avatarSmall: densityMetric(24, 28, 32)
	readonly property int avatarMedium: densityMetric(32, 36, 40)
	// Conversation geometry is shared by the main timeline and detached direct
	// messages. Plain text should remain easy to scan instead of stretching to
	// the complete shell, while previews and attachments get a wider media lane.
	readonly property int chatLaneMaximumWidth: 840
	readonly property int chatPlainMaximumWidth: 620
	readonly property int chatRichMaximumWidth: 760
	readonly property int chatBubbleHorizontalPadding: densityMetric(10, 12, 14)
	readonly property int chatBubbleVerticalPadding: densityMetric(7, 8, 10)
	readonly property int chatContentSpacing: densityMetric(4, 6, 8)
	readonly property int chatMetadataSpacing: densityMetric(3, 4, 6)
	readonly property real chatBodyLineHeight: compact ? 1.16 : spacious ? 1.26 : 1.21
	readonly property int fontCaption: densityMetric(11, 11, 12)
	readonly property int fontBody: densityMetric(13, 13, 14)
	readonly property int fontLabel: densityMetric(12, 12, 13)
	readonly property int fontTitle: densityMetric(15, 15, 16)
	readonly property int fontHeading: densityMetric(20, 20, 22)
	readonly property int composerLineHeight: densityMetric(17, 18, 20)
	readonly property int focusRingWidth: 2
	readonly property int elevationLowOffset: compact ? 1 : 2
	readonly property int elevationMenuOffset: compact ? 3 : 4
	readonly property int motionFast: 90
	readonly property int motionNormal: 160
	readonly property int motionSlow: 240

	function densityMetricFor(targetDensityId, compactValue, comfortableValue, spaciousValue) {
		return targetDensityId === "compact" ? compactValue
			: targetDensityId === "spacious" ? spaciousValue : comfortableValue
	}

	function densityMetric(compactValue, comfortableValue, spaciousValue) {
		return densityMetricFor(densityId, compactValue, comfortableValue, spaciousValue)
	}

	function linearColorChannel(channel) {
		return channel <= 0.04045 ? channel / 12.92 : Math.pow((channel + 0.055) / 1.055, 2.4)
	}

	function relativeLuminance(color) {
		return 0.2126 * linearColorChannel(color.r)
			+ 0.7152 * linearColorChannel(color.g)
			+ 0.0722 * linearColorChannel(color.b)
	}

	function withAlpha(source, alpha) {
		return Qt.rgba(source.r, source.g, source.b, Math.max(0, Math.min(1, alpha)))
	}

	function mixColors(source, target, amount) {
		const ratio = Math.max(0, Math.min(1, amount))
		return Qt.rgba(source.r + (target.r - source.r) * ratio,
			source.g + (target.g - source.g) * ratio,
			source.b + (target.b - source.b) * ratio,
			source.a + (target.a - source.a) * ratio)
	}

	function contrastText(background) {
		const luminance = relativeLuminance(background)
		const dark = "#10151c"
		const light = "#ffffff"
		const darkLuminance = 0.007
		const darkContrast = (luminance + 0.05) / (darkLuminance + 0.05)
		const lightContrast = 1.05 / (luminance + 0.05)
		return darkContrast >= lightContrast ? dark : light
	}
}
