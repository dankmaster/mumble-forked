Describe "Qt Quick direct-message tray accessibility contract" {
	It "names the bulk unread action precisely in product QML and component coverage" {
		$tray = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\qml-shell\DirectMessageTray.qml"
		$componentTest = Get-Content -Raw `
			"$PSScriptRoot\..\..\..\src\tests\TestQmlQuickComponents\qml\tst_DirectMessageSurfaces.qml"

		$tray | Should Match 'objectName:\s*"directMessageTrayMarkAllRead"[\s\S]*text:\s*qsTr\("Mark all read"\)[\s\S]*Accessible\.name:\s*qsTr\("Mark all read"\)'
		$componentTest | Should Match 'compare\(markRead\.text,\s*"Mark all read"\)'
		$componentTest | Should Match 'compare\(markRead\.Accessible\.name,\s*"Mark all read"\)'
	}

	It "requires the precise bulk-action name in the visual gate" {
		$gate = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		$start = $gate.IndexOf('"direct-message-tray"', [StringComparison]::Ordinal)
		$end = $gate.IndexOf('"direct-message-window"', $start, [StringComparison]::Ordinal)
		($start -ge 0) | Should Be $true
		($end -gt $start) | Should Be $true
		$contract = $gate.Substring($start, $end - $start)

		$contract.Contains('"Mark all read"') | Should Be $true
		$contract.Contains('"Mark read"') | Should Be $false
	}
}
