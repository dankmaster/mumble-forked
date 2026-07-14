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

	readonly property int space1: 4
	readonly property int space2: compact ? 6 : 8
	readonly property int space3: compact ? 8 : 12
	readonly property int space4: compact ? 12 : 16
	readonly property int space5: compact ? 16 : 24
	readonly property int space6: compact ? 24 : 32
	readonly property int controlHeight: compact ? 32 : 36
	readonly property int rowHeight: compact ? 38 : 44
	readonly property int avatarSmall: compact ? 24 : 28
	readonly property int avatarMedium: compact ? 32 : 36
	readonly property int fontCaption: 11
	readonly property int fontBody: 13
	readonly property int fontLabel: 12
	readonly property int fontTitle: 15
	readonly property int fontHeading: 20
	readonly property int focusRingWidth: 2
	readonly property int elevationLowOffset: compact ? 1 : 2
	readonly property int elevationMenuOffset: compact ? 3 : 4
	readonly property int motionFast: 90
	readonly property int motionNormal: 160
	readonly property int motionSlow: 240

	function linearColorChannel(channel) {
		return channel <= 0.04045 ? channel / 12.92 : Math.pow((channel + 0.055) / 1.055, 2.4)
	}

	function withAlpha(source, alpha) {
		return Qt.rgba(source.r, source.g, source.b, Math.max(0, Math.min(1, alpha)))
	}

	function contrastText(background) {
		const luminance = 0.2126 * linearColorChannel(background.r)
			+ 0.7152 * linearColorChannel(background.g)
			+ 0.0722 * linearColorChannel(background.b)
		const dark = "#10151c"
		const light = "#ffffff"
		const darkLuminance = 0.007
		const darkContrast = (luminance + 0.05) / (darkLuminance + 0.05)
		const lightContrast = 1.05 / (luminance + 0.05)
		return darkContrast >= lightContrast ? dark : light
	}
}
