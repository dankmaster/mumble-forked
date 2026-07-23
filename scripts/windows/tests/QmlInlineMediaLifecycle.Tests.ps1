$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$verifyScript = Join-Path $repoRoot 'scripts\windows\verify-qml-inline-media-lifecycle.ps1'
$automationSource = Join-Path $repoRoot 'src\mumble\ModernUiAutomationServer.cpp'

Describe 'Qt Quick inline-media lifecycle verifier' {
	BeforeAll {
		$scriptText = Get-Content -Raw -LiteralPath $verifyScript
		$automationCpp = Get-Content -Raw -LiteralPath $automationSource
		$tokens = $null
		$parseErrors = $null
		$scriptAst = [System.Management.Automation.Language.Parser]::ParseFile(
			$verifyScript,
			[ref]$tokens,
			[ref]$parseErrors
		)
		$parseErrors.Count | Should Be 0

		foreach ($functionName in @(
			'Get-ObjectPropertyValue',
			'Set-ObjectPropertyValue',
			'Get-OrAddObjectProperty',
			'New-IsolatedConfig',
			'Get-LatchedWebEngineProcessStarts',
			'Get-Median'
		)) {
			$definition = $scriptAst.Find(
				{
					param($node)
					$node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
						$node.Name -eq $functionName
				},
				$true
			)
			$definition | Should Not BeNullOrEmpty
			. ([scriptblock]::Create($definition.Extent.Text))
		}
	}

	It 'locks the reference measurement to five fresh processes and at least two cycles each' {
		$scriptText | Should Match 'Runs = 5'
		$scriptText | Should Match 'Runs -ne 5'
		$scriptText | Should Match 'CyclesPerRun = 2'
		$scriptText | Should Match 'CyclesPerRun -lt 2'
		$scriptText | Should Match 'exactly_five_fresh_process_runs'
		$scriptText | Should Match 'freshProcessLaunchCount -eq 5'
	}

	It 'creates a private offline config without mutating the source' {
		$root = Join-Path ([IO.Path]::GetTempPath()) ('qml-inline-media-config-' + [Guid]::NewGuid().ToString('N'))
		New-Item -ItemType Directory -Path $root | Out-Null
		try {
			$source = Join-Path $root 'source.json'
			$sourceJson = @'
{
  "network": { "auto_connect_to_last_server": true },
  "update": {
    "check_for_updates": true,
    "check_for_plugin_updates": true,
    "auto_update_plugins": true
  },
  "misc": { "database_location": "C:/shared/mumble.sqlite" },
  "plugins": {
    "fixture": { "enabled": true, "positional_data_enabled": true }
  }
}
'@
			[IO.File]::WriteAllText($source, $sourceJson, [Text.UTF8Encoding]::new($false))
			$isolatedRoot = Join-Path $root 'isolated'
			$isolatedPath = New-IsolatedConfig -SourcePath $source -StateDirectory $isolatedRoot
			$isolated = Get-Content -Raw -LiteralPath $isolatedPath | ConvertFrom-Json
			$original = Get-Content -Raw -LiteralPath $source | ConvertFrom-Json

			$isolated.network.auto_connect_to_last_server | Should Be $false
			$isolated.update.check_for_updates | Should Be $false
			$isolated.update.check_for_plugin_updates | Should Be $false
			$isolated.update.auto_update_plugins | Should Be $false
			$isolated.plugins.fixture.enabled | Should Be $false
			$isolated.plugins.fixture.positional_data_enabled | Should Be $false
			$isolated.misc.database_location | Should Match 'isolated/mumble\.sqlite$'
			$original.network.auto_connect_to_last_server | Should Be $true
			$original.misc.database_location | Should Be 'C:/shared/mumble.sqlite'
		} finally {
			Remove-Item -LiteralPath $root -Recurse -Force
		}
	}

	It 'latches a transient descendant QtWebEngine process and ignores unrelated ones' {
		$records = @(
			[pscustomobject]@{ process_id = 101; parent_process_id = 100; process_name = 'helper.exe' },
			[pscustomobject]@{ process_id = 102; parent_process_id = 101; process_name = 'QtWebEngineProcess.exe' },
			[pscustomobject]@{ process_id = 202; parent_process_id = 200; process_name = 'QtWebEngineProcess.exe' }
		)
		$latched = @(Get-LatchedWebEngineProcessStarts -RootProcessId 100 -ProcessStartRecords $records)
		$latched.Count | Should Be 1
		$latched[0].process_id | Should Be 102
	}

	It 'arms continuous process-start monitoring before the Mumble launch' {
		$traceIndex = $scriptText.IndexOf('$trace = Start-ProcessStartTrace')
		$launchIndex = $scriptText.IndexOf('$process = Start-Process')
		$traceIndex | Should BeGreaterThan -1
		$launchIndex | Should BeGreaterThan $traceIndex
		$scriptText | Should Match 'Win32_ProcessStartTrace'
		$scriptText | Should Match 'CreateToolhelp32Snapshot'
		$scriptText | Should Match 'toolhelp-polling-latch'
		$scriptText | Should Match 'latched_qtwebengine_process_start_count'
		$scriptText | Should Match 'chromium_renderer_process_count'
		$scriptText | Should Match 'zero_qtwebengine_before_first_activation'
		$scriptText | Should Match 'all_inline_cycles_avoided_qtwebengine_process'
	}

	It 'uses only a bounded local WAV fixture and explicitly requests inline presentation' {
		$scriptText | Should Match 'data:audio/wav;base64,'
		$scriptText | Should Match 'setQmlVisualGateState'
		$scriptText | Should Match 'rich_preview_variant = "direct-media"'
		$scriptText | Should Match 'rich_preview_message_id'
		$scriptText | Should Match 'sessionId = \$inlineCardFixture\.message_id'
		$scriptText | Should Match 'presentation = "inline"'
		$scriptText | Should Match 'mediaMime = "audio/wav"'
		$scriptText | Should Match 'expected_renderer_backend = "native"'
		$scriptText | Should Match 'qtwebengine_allowed = \$false'
		$scriptText | Should Match 'network_access = \$false'
		$scriptText | Should Not Match 'youtube\.com|youtu\.be|soundcloud\.com|spotify\.com'
		$automationCpp | Should Match 'command == QLatin1String\("openQmlMediaSession"\)'
		$automationCpp | Should Match 'presentation != QLatin1String\("inline"\)'
		$automationCpp | Should Match 'openDirectInline'
		$automationCpp | Should Match 'nativeSurfaceActive'
		$automationCpp | Should Match 'rendererBackend'
	}

	It 'requires a live inline renderer and full session-surface teardown' {
		$scriptText | Should Match 'rendererPresent.*Default \$false'
		$scriptText | Should Match 'rendererActive.*Default \$false'
		$scriptText | Should Match 'rendererReady.*Default \$false'
		$scriptText | Should Match 'surfaceVerified.*Default \$false'
		$scriptText | Should Match 'transportVerified.*Default \$false'
		$scriptText | Should Match 'surfaceVerificationState'
		$scriptText | Should Match 'surfaceVerificationEvidence'
		$scriptText | Should Match 'all_inline_cycles_surface_and_transport_verified'
		$scriptText | Should Match 'backendState -in @\("paused", "playing"\)'
		$scriptText | Should Match 'all_inline_cycles_playback_ready'
		$scriptText | Should Match 'all_inline_cycles_used_native_renderer'
		$scriptText | Should Match 'nativeBackendObserved.*rendererBackend'
		$scriptText | Should Match 'rendererPresent.*Default \$true'
		$scriptText | Should Match 'rendererActive.*Default \$true'
		$scriptText | Should Match 'presentation.*-eq "none"'
		$scriptText | Should Match 'all_inline_cycles_closed_session_and_surface'
	}

	It 'gates the median of five first activations at fifty milliseconds' {
		(Get-Median -Values @(9.0, 100.0, 11.0, 10.0, 12.0)) | Should Be 11.0
		$scriptText | Should Match 'firstActivationTargetMilliseconds = 50\.0'
		$scriptText | Should Match 'firstActivationLatencies\.Count -eq 5'
		$scriptText | Should Match 'median_first_activation_at_most_50_ms'
		$scriptText | Should Match 'medianFirstActivation -le \$firstActivationTargetMilliseconds'
	}

	It 'fails on UI stalls when the QML performance snapshot is supported' {
		$scriptText | Should Match 'qmlPerformanceReset'
		$scriptText | Should Match 'qmlPerformanceBegin'
		$scriptText | Should Match 'qmlPerformanceEnd'
		$scriptText | Should Match 'qmlPerformanceSnapshot'
		$scriptText | Should Match 'uiStallCount'
		$scriptText | Should Match 'performance_after_renderer'
		$scriptText | Should Match 'performance_after_close'
		$scriptText | Should Match 'zero_ui_stalls_when_performance_snapshot_supported'
		$scriptText | Should Match 'uiStallCount -eq 0'
	}

	It 'records post-close WebEngine persistence as a diagnostic instead of weakening surface teardown' {
		$scriptText | Should Match 'qtwebengine_process_persisted_after_close'
		$scriptText | Should Match 'post_close_qtwebengine_persistence_observed'
		$scriptText | Should Not Match 'qtwebengine_process_persisted_after_close\s*=.*passed'
	}
}
