$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$serverCpp = Join-Path $repoRoot 'src\mumble\ModernUiAutomationServer.cpp'
$serverHeader = Join-Path $repoRoot 'src\mumble\ModernUiAutomationServer.h'
$measureScript = Join-Path $repoRoot 'scripts\windows\measure-qml-client-performance.ps1'

Describe 'Qt Quick performance automation contract' {
	BeforeAll {
		$cpp = Get-Content -Raw -LiteralPath $serverCpp
		$header = Get-Content -Raw -LiteralPath $serverHeader
		$script = Get-Content -Raw -LiteralPath $measureScript
	}

	It 'uses persistent asynchronous talk-state commands' {
		$cpp | Should Match 'qmlPerformanceTalkStart'
		$cpp | Should Match 'qmlPerformanceTalkTransition'
		$cpp | Should Match 'qmlPerformanceTalkStatus'
		$cpp | Should Match 'qmlPerformanceTalkFinalize'
		$header | Should Match 'TalkPerformanceWorkloadState'
		$cpp | Should Not Match 'runTalkStatePerformanceWorkload'
		$cpp | Should Not Match 'qmlPerformanceTalkStateWorkload'
	}

	It 'contains no nested event-loop helpers in the performance automation server' {
		$cpp | Should Not Match 'processEvents'
		$cpp | Should Not Match 'QEventLoop'
		$cpp | Should Not Match 'QElapsedTimer'
		$cpp | Should Not Match 'runChatScrollPerformanceWorkload'
	}

	It 'requires at least forty talk transitions and presented frames' {
		$script | Should Match 'TalkStateTransitions = 40'
		$script | Should Match 'TalkStateTransitions -lt 40'
		$script | Should Match 'presentedFrameDelta -lt \$TalkStateTransitions'
		$script | Should Match 'frameSampleCount -lt \$TalkStateTransitions'
	}

	It 'always finalizes the isolated talk fixture' {
		$script | Should Match 'finally\s*\{[\s\S]*qmlPerformanceTalkFinalize'
		$cpp | Should Match '~ModernUiAutomationServer\(\)[\s\S]*finalizeTalkPerformanceWorkload'
	}

	It 'gates the specified median of five runs while retaining worst-run diagnostics' {
		$script | Should Match 'Runs = 5'
		$script | Should Match 'median_input_to_visual_p95_ms = Get-Percentile'
		$script | Should Match 'worst_input_to_visual_p95_ms = '
		$script | Should Match 'input_to_visual_p95_at_most_50_ms[\s\S]*median_input_to_visual_p95_ms -le 50\.0'
	}

	It 'warms every measured scope and requires stable controller state before steady-state sampling' {
		$script | Should Match 'Iterations \$ready\.tokens\.Count'
		$script | Should Match 'Wait-QmlStateQuiescence'
		$script | Should Match 'room_warmup_measured'
	}
}
