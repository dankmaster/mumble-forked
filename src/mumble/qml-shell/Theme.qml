pragma Singleton
// uiTheme is a C++-owned root context property installed before this singleton loads.
// qmllint disable unqualified
import QtQuick

QtObject {
    readonly property color shellBackground: uiTheme ? uiTheme.shellBackground : "#20262f"
    readonly property color panel: uiTheme ? uiTheme.panel : "#262d38"
    readonly property color rail: uiTheme ? uiTheme.rail : "#1b2027"
    readonly property color strip: uiTheme ? uiTheme.strip : "#14181f"
    readonly property color divider: uiTheme ? uiTheme.divider : "#12ffffff"
    readonly property color textStrong: uiTheme ? uiTheme.textStrong : "#e7ecf3"
    readonly property color textMain: uiTheme ? uiTheme.textMain : "#c3cbd6"
    readonly property color textMuted: uiTheme ? uiTheme.textMuted : "#8b94a3"
    readonly property color accent: uiTheme ? uiTheme.accent : "#5ec8b0"
    readonly property color selected: uiTheme ? uiTheme.selected : "#295ec8b0"
    readonly property color danger: uiTheme ? uiTheme.danger : "#ef4444"
    readonly property color success: uiTheme ? uiTheme.success : "#5fd0a3"
    readonly property color warning: uiTheme ? uiTheme.warning : "#e0c574"
    readonly property color focus: uiTheme ? uiTheme.focus : accent
    readonly property int shellRadius: uiTheme ? uiTheme.shellRadius : 16
    readonly property int innerRadius: uiTheme ? uiTheme.innerRadius : 11
    readonly property int spacing: uiTheme ? uiTheme.spacing : 12
}
