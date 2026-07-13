$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$serverCpp = Join-Path $repoRoot 'src\mumble\ModernUiAutomationServer.cpp'
$serverHeader = Join-Path $repoRoot 'src\mumble\ModernUiAutomationServer.h'
$measureScript = Join-Path $repoRoot 'scripts\windows\measure-qml-client-performance.ps1'

Describe 'Qt Quick performance automation contract' {
	BeforeAll {
		$cpp = Get-Content -Raw -LiteralPath $serverCpp
		$header = Get-Content -Raw -LiteralPath $serverHeader
		$script = Get-Content -Raw -LiteralPath $measureScript
		$tokens = $null
		$parseErrors = $null
		$scriptAst = [System.Management.Automation.Language.Parser]::ParseFile(
			$measureScript,
			[ref]$tokens,
			[ref]$parseErrors
		)
		$parseErrors.Count | Should Be 0
		$traceAnalysisFunction = $scriptAst.Find(
			{
				param($node)
				$node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
					$node.Name -eq 'Get-ChatPerfTraceAnalysis'
			},
			$true
		)
		$traceAnalysisFunction | Should Not BeNullOrEmpty
		. ([scriptblock]::Create($traceAnalysisFunction.Extent.Text))
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

	It 'gates room switching, chat scrolling, and talk-state frames independently' {
		$script | Should Match '\$requiredFramePhases = @\("room_switch", "chat_scroll", "talk_state"\)'
		$script | Should Match '\$framePhaseSummaries\[\$phaseName\]'
		$script | Should Match 'median_frame_p95_ms = \(\$phaseMedianP95Values \| Measure-Object -Maximum\)\.Maximum'
		$script | Should Match 'median_frame_p99_ms = \(\$phaseMedianP99Values \| Measure-Object -Maximum\)\.Maximum'
		$script | Should Match '\$phaseSummary\.run_count -ne \$Runs'
		$script | Should Match '\$phaseSummary\.minimum_frame_sample_count -lt \$MinimumFrameSamples'
	}

	It 'warms every measured scope and requires stable controller state before steady-state sampling' {
		$script | Should Match 'Iterations \$ready\.tokens\.Count'
		$script | Should Match 'Wait-QmlStateQuiescence'
		$script | Should Match 'room_warmup_measured'
	}

	It 'prefers canonical typed room scope tokens and rejects namespaced model row IDs' {
		$script | Should Match 'function Resolve-QmlRoomScopeToken'
		$script | Should Match '\$propertyNames = @\("scopeToken", "token", "id"\)'
		$script | Should Match '\$Room\.PSObject\.Properties\[\$propertyName\]'
		$script | Should Match '\^\-\?\\d\+:\\d\+\$'
		$script | Should Match 'Resolve-QmlRoomScopeToken -Room \$_'
		$script | Should Not Match 'ForEach-Object \{ \[string\]\$_\.token \}'
	}

	It 'passes a clean ChatPerfTrace without steady-state full bootstraps' {
		$analysis = Get-ChatPerfTraceAnalysis -TraceLines @(
			'[chat-perf][timing] qml.participant.talk_state_update count=40 total_ms=3.000 avg_ms=0.075 max_ms=0.120',
			'[chat-perf][value] qml.full_bootstrap count=1 total=1 avg=1.00 max=1'
		)

		$analysis.max_observed_timing_ms | Should Be 0.120
		$analysis.steady_state_full_bootstrap_line_count | Should Be 0
		$analysis.steady_state_full_bootstrap_total | Should Be 0
	}

	It 'fails closed when ChatPerfTrace reports steady-state full bootstraps' {
		$script | Should Match 'no_steady_state_full_bootstrap_trace = \$null'
		$script | Should Match 'no_steady_state_full_bootstrap_trace =\s*\$traceAnalysis\.steady_state_full_bootstrap_line_count -eq 0 -and'
		$analysis = Get-ChatPerfTraceAnalysis -TraceLines @(
			'[chat-perf][timing] qml.participant.talk_state_update count=40 total_ms=3.000 avg_ms=0.075 max_ms=0.120',
			'[chat-perf][value] qml.full_bootstrap.steady_state_violation count=2 total=2 avg=1.00 max=1',
			'[chat-perf][value] qml.full_bootstrap.steady_state_violation count=1 total=1 avg=1.00 max=1'
		)

		$analysis.steady_state_full_bootstrap_line_count | Should Be 2
		$analysis.steady_state_full_bootstrap_total | Should Be 3
		($analysis.steady_state_full_bootstrap_line_count -eq 0 -and
			$analysis.steady_state_full_bootstrap_total -eq 0) | Should Be $false
	}
}
