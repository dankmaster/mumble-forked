function Get-CppFunctionBody {
	param(
		[Parameter(Mandatory = $true)][string]$Text,
		[Parameter(Mandatory = $true)][string]$SignaturePattern
	)
	$match = [regex]::Match($Text, $SignaturePattern, [Text.RegularExpressions.RegexOptions]::Multiline)
	if (-not $match.Success) { throw "C++ function signature was not found: $SignaturePattern" }
	$openBrace = $Text.IndexOf('{', $match.Index + $match.Length)
	if ($openBrace -lt 0) { throw "C++ function body has no opening brace: $SignaturePattern" }
	$depth = 0
	$state = 'code'
	for ($index = $openBrace; $index -lt $Text.Length; ++$index) {
		$character = $Text[$index]
		$next = if ($index + 1 -lt $Text.Length) { $Text[$index + 1] } else { [char]0 }
		switch ($state) {
			'line-comment' {
				if ($character -eq "`n") { $state = 'code' }
				continue
			}
			'block-comment' {
				if ($character -eq '*' -and $next -eq '/') { ++$index; $state = 'code' }
				continue
			}
			'double-quote' {
				if ($character -eq '\') { ++$index; continue }
				if ($character -eq '"') { $state = 'code' }
				continue
			}
			'single-quote' {
				if ($character -eq '\') { ++$index; continue }
				if ($character -eq "'") { $state = 'code' }
				continue
			}
		}
		if ($character -eq '/' -and $next -eq '/') { ++$index; $state = 'line-comment'; continue }
		if ($character -eq '/' -and $next -eq '*') { ++$index; $state = 'block-comment'; continue }
		if ($character -eq '"') { $state = 'double-quote'; continue }
		if ($character -eq "'") { $state = 'single-quote'; continue }
		if ($character -eq '{') { ++$depth; continue }
		if ($character -eq '}') {
			--$depth
			if ($depth -eq 0) { return $Text.Substring($match.Index, $index - $match.Index + 1) }
		}
	}
	throw "C++ function body did not terminate: $SignaturePattern"
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$serverCpp = Join-Path $repoRoot 'src\mumble\ModernUiAutomationServer.cpp'
$serverHeader = Join-Path $repoRoot 'src\mumble\ModernUiAutomationServer.h'
$mainQml = Join-Path $repoRoot 'src\mumble\qml-shell\Main.qml'
$mainWindowCpp = Join-Path $repoRoot 'src\mumble\MainWindow.cpp'
$messagesCpp = Join-Path $repoRoot 'src\mumble\Messages.cpp'
$serverHandlerCpp = Join-Path $repoRoot 'src\mumble\ServerHandler.cpp'
$pluginManagerCpp = Join-Path $repoRoot 'src\mumble\PluginManager.cpp'
$chatPerfTraceHeader = Join-Path $repoRoot 'src\mumble\ChatPerfTrace.h'
$measureScript = Join-Path $repoRoot 'scripts\windows\measure-qml-client-performance.ps1'

Describe 'Qt Quick performance automation contract' {
	BeforeAll {
		$cpp = Get-Content -Raw -LiteralPath $serverCpp
		$header = Get-Content -Raw -LiteralPath $serverHeader
		$qml = Get-Content -Raw -LiteralPath $mainQml
		$mainWindowSource = Get-Content -Raw -LiteralPath $mainWindowCpp
		$messagesSource = Get-Content -Raw -LiteralPath $messagesCpp
		$serverHandlerSource = Get-Content -Raw -LiteralPath $serverHandlerCpp
		$pluginManagerSource = Get-Content -Raw -LiteralPath $pluginManagerCpp
		$chatPerfTraceSource = Get-Content -Raw -LiteralPath $chatPerfTraceHeader
		$script = Get-Content -Raw -LiteralPath $measureScript
		$tokens = $null
		$parseErrors = $null
		$scriptAst = [System.Management.Automation.Language.Parser]::ParseFile(
			$measureScript,
			[ref]$tokens,
			[ref]$parseErrors
		)
		$parseErrors.Count | Should Be 0
		foreach ($functionName in @(
			'Get-ByteArraySha256',
			'Get-FrozenProfileSeed',
			'New-FrozenRunProfile',
			'Test-FrozenProfileSourceUnchanged',
			'Get-ValidatedWebPerformanceBaseline',
			'Get-ProcessTreeIds',
			'Get-ProcessTreeMetrics',
			'Initialize-WindowsJobProcessStartTracker',
			'Start-ProcessStartTrace',
			'Start-ProcessInStartTraceJob',
			'Stop-ProcessStartTrace',
			'Wait-FrozenProfileFilesReady',
			'Remove-IsolatedPerformanceStateRoot'
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
		$modelResetAnalysisFunction = $scriptAst.Find(
			{
				param($node)
				$node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
					$node.Name -eq 'Get-QmlModelResetAnalysis'
			},
			$true
		)
		$modelResetAnalysisFunction | Should Not BeNullOrEmpty
		. ([scriptblock]::Create($modelResetAnalysisFunction.Extent.Text))
		$syncUiAnalysisFunction = $scriptAst.Find(
			{
				param($node)
				$node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
					$node.Name -eq 'Get-QmlSyncUiOperationAnalysis'
			},
			$true
		)
		$syncUiAnalysisFunction | Should Not BeNullOrEmpty
		. ([scriptblock]::Create($syncUiAnalysisFunction.Extent.Text))
		$processLatchFunction = $scriptAst.Find(
			{
				param($node)
				$node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
					$node.Name -eq 'Get-LatchedWebEngineProcessStarts'
			},
			$true
		)
		$processLatchFunction | Should Not BeNullOrEmpty
		. ([scriptblock]::Create($processLatchFunction.Extent.Text))
	}

	It 'uses persistent asynchronous talk-state commands' {
		$cpp | Should Match 'qmlPerformanceTalkStart'
		$cpp | Should Match 'qmlPerformanceTalkRun'
		$cpp | Should Match 'qmlPerformanceTalkTransition'
		$cpp | Should Match 'qmlPerformanceTalkStatus'
		$cpp | Should Match 'qmlPerformanceTalkFinalize'
		$header | Should Match 'TalkPerformanceWorkloadState'
		$header | Should Match 'QMetaObject::Connection frameConnection'
		$cpp | Should Match 'frameSwapped[\s\S]*advanceTalkPerformanceWorkload'
		$cpp | Should Match 'advanceTalkPerformanceWorkload[\s\S]*monitor->markInput[\s\S]*updatePresence'
		$cpp | Should Not Match 'runTalkStatePerformanceWorkload'
		$cpp | Should Not Match 'qmlPerformanceTalkStateWorkload'
	}

	It 'contains no nested event-loop helpers in the performance automation server' {
		$cpp | Should Not Match 'processEvents'
		$cpp | Should Not Match 'QEventLoop'
		$cpp | Should Not Match 'QElapsedTimer'
		$cpp | Should Not Match 'runChatScrollPerformanceWorkload'
	}

	It 'keeps connect and performance trace writes off the calling UI path' {
		$traceFunctions = @(
			[pscustomobject]@{ source = $mainWindowSource; signature = 'void\s+appendModernShellConnectTrace\s*\([^)]*\)'; async = 'appendFileLineAsync' },
			[pscustomobject]@{ source = $mainWindowSource; signature = 'void\s+MainWindow::customEvent\s*\([^)]*\)'; async = 'appendFileLineAsync' },
			[pscustomobject]@{ source = $messagesSource; signature = 'void\s+MainWindow::msgServerSync\s*\([^)]*\)'; async = 'appendFileLineAsync' },
			[pscustomobject]@{ source = $messagesSource; signature = 'void\s+MainWindow::msgUserState\s*\([^)]*\)'; async = 'appendFileLineAsync' },
			[pscustomobject]@{ source = $serverHandlerSource; signature = 'void\s+appendServerHandlerTrace\s*\([^)]*\)'; async = 'appendFileLineAsync' },
			[pscustomobject]@{ source = $chatPerfTraceSource; signature = 'inline\s+void\s+appendLineLocked\s*\([^)]*\)'; async = 'traceWriter\(\)\.enqueue' }
		)
		foreach ($target in $traceFunctions) {
			$body = Get-CppFunctionBody -Text $target.source -SignaturePattern $target.signature
			$body | Should Match $target.async
			$body | Should Not Match '\bQFile\b|\bQSaveFile\b|Database::blob|Global::get\(\)\.db->blob'
		}

		$userStateBody = Get-CppFunctionBody -Text $messagesSource `
			-SignaturePattern 'void\s+MainWindow::msgUserState\s*\([^)]*\)'
		$userStateBody | Should Match 'userLocalPreferenceLoader\(this, pmModel\).*loader->request\(pDst\)'
		$userStateBody | Should Not Match 'Global::get\(\)\.db\s*->'
		$localPreferenceLoaderBody = Get-CppFunctionBody -Text $messagesSource `
			-SignaturePattern 'void\s+launchNextBatch\s*\([^)]*\)'
		$workerIndex = $localPreferenceLoaderBody.IndexOf('QtConcurrent::run')
		$databaseIndex = $localPreferenceLoaderBody.IndexOf('Database database')
		($workerIndex -ge 0 -and $databaseIndex -gt $workerIndex) | Should Be $true
		$localPreferenceLoaderBody.Substring(0, $workerIndex) | Should Not Match '\bDatabase\s+database\b|Global::get\(\)\.db\s*->'
	}

	It 'keeps room-state construction and positional plugin ABI work free of direct UI-thread I/O' {
		$roomBody = Get-CppFunctionBody -Text $mainWindowSource -SignaturePattern 'QVariantMap\s+MainWindow::buildQmlRoomState\s*\([^)]*\)'
		$roomBody | Should Not Match '\bQFile\b|\bQSaveFile\b|Database::blob|Global::get\(\)\.db->blob'

		$syncBody = Get-CppFunctionBody -Text $pluginManagerSource -SignaturePattern 'void\s+PluginManager::on_syncPositionalData\s*\([^)]*\)'
		$queueIndex = $syncBody.IndexOf('sharedPluginAbiWorker().enqueue')
		$fetchIndex = $syncBody.IndexOf('fetchPositionalData()')
		($queueIndex -ge 0 -and $fetchIndex -gt $queueIndex) | Should Be $true
		$syncBody.Substring(0, $queueIndex) | Should Not Match 'fetchPositionalData|activePlugin->|m_activePositionalDataPlugin->'
		$syncBody | Should Not Match '\bQFile\b|\bQSaveFile\b|Database::blob|Global::get\(\)\.db->blob'
	}

	It 'keeps server-scoped and comment or ACL blob reads on bounded workers' {
		$serverSyncBody = Get-CppFunctionBody -Text $messagesSource `
			-SignaturePattern 'void\s+MainWindow::msgServerSync\s*\([^)]*\)'
		$serverSyncBody | Should Match 'serverScopedDatabaseLoader\(this, pmModel\)'
		$serverSyncBody | Should Match 'requestShortcuts\('
		$serverSyncBody | Should Not Match 'Global::get\(\)\.db\s*->|\bgetShortcuts\s*\('

		$channelStateBody = Get-CppFunctionBody -Text $messagesSource `
			-SignaturePattern 'void\s+MainWindow::msgChannelState\s*\([^)]*\)'
		$channelStateBody | Should Match 'requestChannelFilter\('
		$channelStateBody | Should Not Match 'Global::get\(\)\.db\s*->|\bgetChannelFilterMode\s*\('

		$serverLoaderStart = $messagesSource.IndexOf('class ServerScopedDatabaseLoader')
		$serverLoaderEnd = $messagesSource.IndexOf(
			'ServerScopedDatabaseLoader *serverScopedDatabaseLoader', $serverLoaderStart)
		($serverLoaderStart -ge 0 -and $serverLoaderEnd -gt $serverLoaderStart) | Should Be $true
		$serverLoader = $messagesSource.Substring($serverLoaderStart, $serverLoaderEnd - $serverLoaderStart)
		$serverWorkerIndex = $serverLoader.IndexOf('QtConcurrent::run')
		$shortcutReadIndex = $serverLoader.IndexOf('database.getShortcuts')
		$filterReadIndex = $serverLoader.IndexOf('database.getChannelFilterMode')
		($serverWorkerIndex -ge 0 -and $shortcutReadIndex -gt $serverWorkerIndex `
			-and $filterReadIndex -gt $serverWorkerIndex) | Should Be $true
		$serverLoader.Substring(0, $serverWorkerIndex) | Should Not Match '\bDatabase\s+database\b'

		$mainWindowSource | Should Not Match 'Global::get\(\)\.db->blob\s*\('
		$blobReadBody = Get-CppFunctionBody -Text $mainWindowSource `
			-SignaturePattern 'void\s+MainWindow::flushMainWindowBlobReads\s*\([^)]*\)'
		$submitIndex = $blobReadBody.IndexOf('persistentChatPreviewWorkerQueue().submit')
		$databaseIndex = $blobReadBody.IndexOf('Database database')
		($submitIndex -ge 0 -and $databaseIndex -gt $submitIndex) | Should Be $true
		$blobReadBody.Substring(0, $submitIndex) | Should Not Match '\bDatabase\s+database\b|Global::get\(\)\.db\s*->'
	}

	It 'keeps avatar hydration database reads off the UI-thread render path' {
		$renderFunctions = @(
			'bool\s+MainWindow::ensureUserTextureAvailable\s*\([^)]*\)',
			'QString\s+MainWindow::modernShellAvatarDataUrl\s*\([^)]*\)',
			'QString\s+MainWindow::modernShellAvatarDataUrlForTextureHash\s*\([^)]*\)',
			'QString\s+MainWindow::modernShellActorAvatarDataUrl\s*\([^)]*\)',
			'QVariantMap\s+MainWindow::buildModernShellCachedMessageState\s*\([^)]*\)',
			'QVariantMap\s+MainWindow::buildQmlParticipantState\s*\([^)]*\)'
		)
		foreach ($signature in $renderFunctions) {
			$body = Get-CppFunctionBody -Text $mainWindowSource -SignaturePattern $signature
			$body | Should Not Match '\bQFile\b|\bQSaveFile\b|Database::blob|Global::get\(\)\.db->blob'
		}

		$textureHashBody = Get-CppFunctionBody -Text $mainWindowSource `
			-SignaturePattern 'QString\s+MainWindow::modernShellAvatarDataUrlForTextureHash\s*\([^)]*\)'
		$textureHashBody | Should Match 'queueQmlAvatarHydration\(textureHash'

		$hydrationBody = Get-CppFunctionBody -Text $mainWindowSource `
			-SignaturePattern 'void\s+MainWindow::queueQmlAvatarHydration\s*\([^)]*\)'
		$queueIndex = $hydrationBody.IndexOf('persistentChatPreviewWorkerQueue().submit')
		$databaseIndex = $hydrationBody.IndexOf('Database database')
		($queueIndex -ge 0 -and $databaseIndex -gt $queueIndex) | Should Be $true
		$hydrationBody.Substring(0, $queueIndex) | Should Not Match `
			'\bQFile\b|\bQSaveFile\b|\bDatabase\s+database\b|Database::blob|Global::get\(\)\.db->blob'
	}

	It 'requires at least forty talk transitions, presented frames, and input samples' {
		$script | Should Match 'TalkStateTransitions = 40'
		$script | Should Match 'TalkStateTransitions -lt 40'
		$script | Should Match 'talkStatus\.presentedFrameDelta -lt \$TalkStateTransitions'
		$script | Should Match 'frameSampleCount -lt \$TalkStateTransitions'
		$script | Should Match 'inputSampleCount -lt \$TalkStateTransitions'
	}

	It 'always finalizes the isolated talk fixture' {
		$script | Should Match 'finally\s*\{[\s\S]*qmlPerformanceTalkFinalize'
		$cpp | Should Match '~ModernUiAutomationServer\(\)[\s\S]*finalizeTalkPerformanceWorkload'
	}

	It 'gates the specified median of five runs while retaining worst-run diagnostics' {
		$script | Should Match 'Runs = 5'
		$script | Should Match 'if \(\$Runs -ne 5\)'
		$script | Should Match 'median_input_p95_ms = Get-Percentile'
		$script | Should Match 'worst_phase_median_input_to_visual_p95_ms = if \(\$phaseMedianInputValues\.Count -gt 0\) \{ \(\$phaseMedianInputValues \| Measure-Object -Maximum\)\.Maximum'
		$script | Should Match 'worst_input_to_visual_p95_ms = '
		$script | Should Match 'input_to_visual_p95_at_most_50_ms[\s\S]*worst_phase_median_input_to_visual_p95_ms -le 50\.0'
		$script | Should Match 'exactly_five_runs_measured = \$Runs -eq 5 -and \$measurements\.Count -eq 5'
	}

	It 'measures startup to the interactive QML readiness state' {
		$script | Should Match 'function Wait-QmlInteractiveShell'
		$script | Should Match 'command = "qmlReadinessState"'
		$script | Should Match 'windowReady.*mainCaptureReady'
		$readyIndex = $script.IndexOf('$interactiveState = Wait-QmlInteractiveShell')
		$timestampIndex = $script.IndexOf('$startupInteractiveMilliseconds = $stopwatch.Elapsed.TotalMilliseconds')
		($readyIndex -ge 0 -and $timestampIndex -gt $readyIndex) | Should Be $true
		$script | Should Match 'startup_to_interactive_median_ms'
		$script | Should Match 'startup_to_window_median_ms = Get-Percentile[\s\S]*diagnostics'
	}

	It 'polls measured room selection through bounded readiness state' {
		$waitSelectedFunction = $scriptAst.Find({
			param($node)
			$node -is [System.Management.Automation.Language.FunctionDefinitionAst] `
				-and $node.Name -eq 'Wait-SelectedScope'
		}, $true)
		$waitSelectedFunction | Should Not BeNullOrEmpty
		$waitSelectedBody = $waitSelectedFunction.Extent.Text
		$waitSelectedBody | Should Match 'Get-QmlReadinessState'
		$waitSelectedBody | Should Match 'activeScopeToken'
		$waitSelectedBody | Should Not Match 'Get-QmlSnapshot'
	}

	It 'clones one frozen config and database seed into five isolated run profiles' {
		$root = Join-Path ([IO.Path]::GetTempPath()) ('qml-perf-frozen-' + [Guid]::NewGuid().ToString('N'))
		New-Item -ItemType Directory -Path $root | Out-Null
		try {
			$databasePath = Join-Path $root 'source.sqlite'
			[IO.File]::WriteAllBytes($databasePath, [byte[]](1, 2, 3, 4, 5))
			$configPath = Join-Path $root 'source.json'
			$configJson = [pscustomobject]@{
				misc = [pscustomobject]@{ database_location = $databasePath }
				marker = 'frozen-source'
			} | ConvertTo-Json -Depth 10
			[IO.File]::WriteAllText($configPath, $configJson, [Text.UTF8Encoding]::new($false))

			$seed = Get-FrozenProfileSeed -SourceConfigPath $configPath
			$expectedSeed = Get-ByteArraySha256 -Bytes ([Text.Encoding]::UTF8.GetBytes(
				"$($seed.source_config_sha256)|$($seed.source_database_sha256)"))
			$seed.profile_seed_sha256 | Should Be $expectedSeed
			$run1 = New-FrozenRunProfile -Seed $seed -RunDirectory (Join-Path $root 'run-01') -Run 1
			$run2 = New-FrozenRunProfile -Seed $seed -RunDirectory (Join-Path $root 'run-02') -Run 2
			$run1.profile_seed_sha256 | Should Be $run2.profile_seed_sha256
			$run1.database_seed_sha256 | Should Be $seed.source_database_sha256
			$run2.database_seed_sha256 | Should Be $seed.source_database_sha256
			$run1.config_path | Should Not Be $run2.config_path
			$run1.database_path | Should Not Be $run2.database_path
			(Get-Content -Raw -LiteralPath $run1.config_path | ConvertFrom-Json).misc.database_location | Should Be ($run1.database_path -replace '\\', '/')
			(Get-Content -Raw -LiteralPath $run2.config_path | ConvertFrom-Json).misc.database_location | Should Be ($run2.database_path -replace '\\', '/')

			[IO.File]::WriteAllBytes($run1.database_path, [byte[]](9, 9, 9))
			(Get-FileHash -LiteralPath $run2.database_path -Algorithm SHA256).Hash.ToLowerInvariant() | Should Be $seed.source_database_sha256
			$sourceCheck = Test-FrozenProfileSourceUnchanged -Seed $seed
			$sourceCheck.unchanged | Should Be $true
		} finally {
			Remove-Item -LiteralPath $root -Recurse -Force
		}

		$script | Should Match 'Start-ProcessInStartTraceJob -Trace \$processStartTrace -FilePath \$executablePath'
		$script | Should Match '-ArgumentList @\("--multiple", "--config", \$runProfile\.config_path\)'
		$script | Should Not Match '-ArgumentList @\("--multiple", "--config", \$configFilePath\)'
		$script | Should Match 'frozen_profile_isolation_measured'
		$script | Should Match 'frozen_profile_source_unchanged'
		$script | Should Match 'profile_seed_sha256'
	}

	It 'fails closed when the frozen SQLite source has WAL state' {
		$root = Join-Path ([IO.Path]::GetTempPath()) ('qml-perf-wal-' + [Guid]::NewGuid().ToString('N'))
		New-Item -ItemType Directory -Path $root | Out-Null
		try {
			$databasePath = Join-Path $root 'source.sqlite'
			[IO.File]::WriteAllBytes($databasePath, [byte[]](1, 2, 3))
			[IO.File]::WriteAllBytes("$databasePath-wal", [byte[]](4, 5, 6))
			$configPath = Join-Path $root 'source.json'
			[IO.File]::WriteAllText($configPath, (@{ misc = @{ database_location = $databasePath } } | ConvertTo-Json), [Text.UTF8Encoding]::new($false))
			(Test-Path -LiteralPath "$databasePath-wal") | Should Be $true
			$threw = $false
			try { Get-FrozenProfileSeed -SourceConfigPath $configPath | Out-Null } catch { $threw = $true }
			$threw | Should Be $true
		} finally {
			Remove-Item -LiteralPath $root -Recurse -Force
		}
	}

	It 'samples connected quiescent no-media idle before warmup or workloads' {
		$script | Should Match 'Wait-QmlStateQuiescence -Port \$runPort -Token \$runToken'
		$script | Should Match 'Wait-QmlReadinessQuiescence -Port \$runPort -Token \$runToken'
		$script | Should Match 'mediaActive.*before connected-idle sampling|media session was active before connected-idle sampling'
		$script | Should Match 'media session became active during connected-idle sampling'
		$idleIndex = $script.IndexOf('$connectedIdle.process_metrics = Get-ProcessTreeMetrics')
		$warmupIndex = $script.IndexOf('Invoke-RoomSwitchWorkload -Port $runPort -Token $runToken -ScopeTokens $ready.tokens -Iterations $ready.tokens.Count')
		($idleIndex -ge 0 -and $warmupIndex -gt $idleIndex) | Should Be $true
		$script | Should Match 'connected_idle_working_set_median_bytes'
		$quiescenceIndex = $script.IndexOf('$idleStablePolls = Wait-QmlStateQuiescence')
		$beginIndex = $script.IndexOf('command = "qmlPerformanceBegin"', $quiescenceIndex)
		($quiescenceIndex -ge 0 -and $beginIndex -gt $quiescenceIndex) | Should Be $true
	}

	It 'keeps each measured Quick window visible, exposed, and unoccluded' {
		$script | Should Match 'ShowWindowAsync\(\$window, 9\)'
		$script | Should Match 'SetWindowPos\(\$window, \$insertAfter'
		$script | Should Match 'Assert-QmlMeasurementWindowReady'
		$script | Should Match 'windowVisible.*windowExposed'
		$cpp | Should Match '"windowVisible"[\s\S]*isVisible\(\)'
		$cpp | Should Match '"windowExposed"[\s\S]*isExposed\(\)'
	}

	It 'latches every descendant process start without elevation and reconciles notifications against Job accounting' {
		$script | Should Match 'CREATE_SUSPENDED'
		$script | Should Match 'CreateJobObject'
		$script | Should Match 'CreateIoCompletionPort'
		$script | Should Match 'JobObjectAssociateCompletionPortInformation'
		$script | Should Match 'JOB_OBJECT_MSG_NEW_PROCESS'
		$script | Should Match 'QueryInformationJobObject'
		$script | Should Match 'snapshotRecords\.Length == accounting\.TotalProcesses'
		$script | Should Match 'UnresolvedProcessCount'
		$script | Should Not Match 'Register-CimIndicationEvent'
		$script | Should Not Match 'Win32_ProcessStartTrace'
		$script | Should Not Match 'Win32_Process\.__InstanceCreationEvent'
		$traceIndex = $script.IndexOf('$processStartTrace = Start-ProcessStartTrace')
		$launchIndex = $script.IndexOf('$process = Start-ProcessInStartTraceJob')
		($traceIndex -ge 0 -and $launchIndex -gt $traceIndex) | Should Be $true
		$assignIndex = $script.IndexOf('if (!AssignProcessToJobObject(job, process.hProcess))')
		$resumeIndex = $script.IndexOf('if (ResumeThread(process.hThread) == UInt32.MaxValue)')
		($assignIndex -ge 0 -and $resumeIndex -gt $assignIndex) | Should Be $true
		$script | Should Match 'root_start_observed'
		$script | Should Match 'latched_qtwebengine_count'
		$script | Should Match '(?s)\$latchedWebEngineStarts\s*=\s*@\(\s*if \(\$rootStartObserved\)'
		$script | Should Match 'QtWebEngineProcess\.exe'
		$script | Should Match 'function Get-WebEngineModuleResidency'
		$script | Should Match 'process_name_counts'
		$script | Should Match 'accounting_reconciled'
		$script | Should Match 'job_total_processes'
		$script | Should Match 'all_processes_exited'
		$script | Should Match 'termination_wait_ms'
		$script | Should Match 'termination_timed_out'
		$script | Should Match 'The Job Object process-exit deadline expired'
		$script | Should Match 'notification_count'
		$script | Should Match 'unresolved_process_count'
		$script | Should Match '\$processStartTraceMeasured = \$null -ne \$processStartSnapshot -and \[bool\]\$processStartSnapshot\.measured -and'
		$records = @(
			[pscustomobject]@{ process_id = 100; parent_process_id = 50; process_name = 'mumble.exe' },
			[pscustomobject]@{ process_id = 101; parent_process_id = 100; process_name = 'helper.exe' },
			[pscustomobject]@{ process_id = 102; parent_process_id = 101; process_name = 'QtWebEngineProcess.exe' },
			[pscustomobject]@{ process_id = 103; parent_process_id = 100; process_name = 'QtWebEngineProcess.exe' },
			[pscustomobject]@{ process_id = 200; parent_process_id = 50; process_name = 'QtWebEngineProcess.exe' }
		)
		$latched = @(Get-LatchedWebEngineProcessStarts -RootProcessId 100 -ProcessStartRecords $records)
		$latched.Count | Should Be 2
		@($latched.process_id | Sort-Object) -join ',' | Should Be '102,103'

		$jobRecords = @(
			[pscustomobject]@{ process_id = 300; parent_process_id = 0; process_name = 'QtWebEngineProcess.exe'; job_descendant = $true }
		)
		@(Get-LatchedWebEngineProcessStarts -RootProcessId 100 -ProcessStartRecords $jobRecords).Count | Should Be 1
	}

	It 'runs the reconciled Job Object latch as a non-admin process' {
		$trace = Start-ProcessStartTrace
		$process = $null
		$snapshot = $null
		try {
			$hostExecutable = (Get-Process -Id $PID -ErrorAction Stop).Path
			$childCommand = '$childPath = Join-Path $PSHOME ([IO.Path]::GetFileName((Get-Process -Id $PID).Path)); ' +
				'Start-Process -FilePath $childPath -ArgumentList @(''-NoProfile'',''-Command'',''Start-Sleep -Seconds 30'') | Out-Null; ' +
				'Start-Sleep -Seconds 30'
			$process = Start-ProcessInStartTraceJob -Trace $trace -FilePath $hostExecutable `
				-ArgumentList @('-NoProfile', '-Command', $childCommand) `
				-WorkingDirectory ([IO.Path]::GetTempPath())
			Start-Sleep -Milliseconds 750
			$snapshot = Stop-ProcessStartTrace -Trace $trace
		} finally {
			if (-not [bool]$trace.stopped) { Stop-ProcessStartTrace -Trace $trace | Out-Null }
		}
		$snapshot.measured | Should Be $true
		$snapshot.accounting_reconciled | Should Be $true
		$snapshot.job_active_processes_after_termination | Should Be 0
		$snapshot.all_processes_exited | Should Be $true
		$snapshot.termination_timed_out | Should Be $false
		$snapshot.notification_count | Should Be $snapshot.job_total_processes
		$snapshot.unresolved_process_count | Should Be 0
		@($snapshot.records | Where-Object { $_.process_id -eq $process.Id }).Count | Should Be 1
		$snapshot.job_total_processes | Should BeGreaterThan 1
		@($snapshot.records | Where-Object { $_.parent_process_id -eq $process.Id }).Count | Should BeGreaterThan 0
	}

	It 'waits for Job-owned frozen-profile handles before hashing and bounded cleanup' {
		$parent = Join-Path ([IO.Path]::GetTempPath()) ('qml-perf-teardown-' + [Guid]::NewGuid().ToString('N'))
		$stateRoot = Join-Path $parent 'isolated-state'
		New-Item -ItemType Directory -Path $stateRoot -Force | Out-Null
		$databasePath = Join-Path $stateRoot 'mumble.sqlite'
		$consolePath = Join-Path $stateRoot 'Console.txt'
		$readyPath = Join-Path $stateRoot 'locks-ready'
		[IO.File]::WriteAllText($databasePath, 'database-seed')
		[IO.File]::WriteAllText($consolePath, 'console-seed')
		$trace = Start-ProcessStartTrace
		$process = $null
		try {
			$db64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($databasePath))
			$console64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($consolePath))
			$ready64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($readyPath))
			$childCommand = @"
`$db = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('$db64'))
`$console = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('$console64'))
`$ready = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('$ready64'))
`$dbStream = [IO.File]::Open(`$db, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
`$consoleStream = [IO.File]::Open(`$console, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
try {
    [IO.File]::WriteAllText(`$ready, 'ready')
    Start-Sleep -Seconds 30
} finally {
    `$consoleStream.Dispose()
    `$dbStream.Dispose()
}
"@
			$encodedCommand = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($childCommand))
			$hostExecutable = (Get-Process -Id $PID -ErrorAction Stop).Path
			$process = Start-ProcessInStartTraceJob -Trace $trace -FilePath $hostExecutable `
				-ArgumentList @('-NoProfile', '-EncodedCommand', $encodedCommand) `
				-WorkingDirectory $stateRoot
			$deadline = [DateTime]::UtcNow.AddSeconds(5)
			do { Start-Sleep -Milliseconds 25 } while (-not (Test-Path -LiteralPath $readyPath) -and [DateTime]::UtcNow -lt $deadline)
			(Test-Path -LiteralPath $readyPath) | Should Be $true
			$process.Refresh()
			$process.HasExited | Should Be $false
			$probe = $null
			$probeFailed = $false
			try {
				$probe = [IO.File]::Open($databasePath, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::None)
			} catch {
				$probeFailed = $true
			} finally {
				if ($null -ne $probe) { $probe.Dispose() }
			}
			$probeFailed | Should Be $true

			$snapshot = Stop-ProcessStartTrace -Trace $trace
			$snapshot.measured | Should Be $true
			$snapshot.all_processes_exited | Should Be $true
			$snapshot.termination_timed_out | Should Be $false
			$ready = Wait-FrozenProfileFilesReady -Paths @($databasePath, $consolePath) `
				-TimeoutMilliseconds 3000 -PollIntervalMilliseconds 20 -RequiredStablePolls 2
			$ready.ready | Should Be $true
			$ready.stable_polls | Should BeGreaterThan 1
			(Get-FileHash -LiteralPath $databasePath -Algorithm SHA256).Hash | Should Not BeNullOrEmpty
			(Get-FileHash -LiteralPath $consolePath -Algorithm SHA256).Hash | Should Not BeNullOrEmpty

			$cleanup = Remove-IsolatedPerformanceStateRoot -RootPath $stateRoot -AllowedParentPath $parent `
				-TimeoutMilliseconds 3000 -PollIntervalMilliseconds 20
			$cleanup.removed | Should Be $true
			(Test-Path -LiteralPath $stateRoot) | Should Be $false
		} finally {
			if (-not [bool]$trace.stopped) { Stop-ProcessStartTrace -Trace $trace | Out-Null }
			if (Test-Path -LiteralPath $parent) { Remove-Item -LiteralPath $parent -Recurse -Force -ErrorAction SilentlyContinue }
		}
	}

	It 'orders process exit, exclusive file quiescence, post-run hashes, and bounded root cleanup' {
		$script | Should Match '\[IO\.FileShare\]::None'
		$script | Should Match 'RequiredStablePolls = 2'
		$script | Should Match 'Remove-IsolatedPerformanceStateRoot -RootPath \$isolatedStateRoot'
		$script | Should Match '\$notMeasured\.Add\("frozen_profile_cleanup:'
		$stopIndex = $script.LastIndexOf('$processStartSnapshot = Stop-ProcessStartTrace -Trace $processStartTrace')
		$quiescenceIndex = $script.LastIndexOf('$fileQuiescence = Wait-FrozenProfileFilesReady -Paths $profileFiles')
		$postHashIndex = $script.LastIndexOf('$postConfigHash = (Get-FileHash')
		($stopIndex -ge 0 -and $quiescenceIndex -gt $stopIndex -and $postHashIndex -gt $quiescenceIndex) | Should Be $true
	}

	It 'emits named process-count keys instead of grouping OrderedDictionary rows under an empty key' {
		$script | Should Match '\$rows \+= \[pscustomobject\]\[ordered\]@\{'
		$metrics = Get-ProcessTreeMetrics -RootProcessId $PID
		@($metrics.process_name_counts.Keys | Where-Object { [string]::IsNullOrWhiteSpace([string]$_) }).Count | Should Be 0
		@($metrics.process_name_counts.Keys).Count | Should BeGreaterThan 0
	}

	It 'requires independent room, chat-scroll, and talk input samples' {
		$script | Should Match 'MinimumInputSamples = 40'
		$script | Should Match 'RoomSwitchIterations -lt 40'
		$script | Should Match 'requiredChatScrollInputSamples = 40'
		$script | Should Match 'command = "qmlPerformanceChatScrollRun"; stepCount = \$requiredChatScrollInputSamples'
		$script | Should Match 'command = "qmlPerformanceChatScrollStatus"'
		$script | Should Match 'scrollStatus\.stepCount -ge \$requiredChatScrollInputSamples'
		$script | Should Match 'scrollStatus\.presentedFrameDelta -ge \$requiredChatScrollInputSamples'
		$script | Should Match 'scrollStatus\.performance\.frameSampleCount -ge \$requiredChatScrollInputSamples'
		$script | Should Match 'scrollStatus\.performance\.inputSampleCount -ge \$requiredChatScrollInputSamples'
		$script | Should Not Match 'chatScrollInputIntervalMilliseconds'
		$script | Should Match 'room_switch = \$MinimumInputSamples\s+chat_scroll = \$requiredChatScrollInputSamples\s+talk_state = \$TalkStateTransitions'
		$script | Should Match 'input_to_visual_room_switch_measured'
		$script | Should Match 'input_to_visual_chat_scroll_measured'
		$script | Should Match 'input_to_visual_talk_state_measured'
		$qml | Should Match 'preparePerformanceChatScrollWorkload'
		$qml | Should Match 'advancePerformanceChatScrollWorkload'
		$script | Should Match 'command = "qmlPerformanceTalkRun"; transitionCount = \$TalkStateTransitions'
		$script | Should Not Match 'qmlPerformanceTalkTransition"[\s\S]{0,180}Start-Sleep -Milliseconds 20'
		$script | Should Match 'talkStatus\.performance\.frameSampleCount -ge \$TalkStateTransitions'
		$cpp | Should Match 'advanceChatPerformanceWorkload[\s\S]*monitor->markInput'
		$cpp | Should Match 'frameSwapped[\s\S]*advanceChatPerformanceWorkload'
		$cpp | Should Match 'advanceTalkPerformanceWorkload[\s\S]*monitor->markInput'
	}

	It 'requires the chat fixture layout to settle before scrolling' {
		$qml | Should Match 'performanceChatFixtureState[\s\S]*"contentY"[\s\S]*"settled"'
		$cpp | Should Match 'layout\.value\(QStringLiteral\("settled"\)\)\.toBool\(\)'
		$cpp | Should Match 'layout\.value\(QStringLiteral\("firstVisibleId"\)\)'
		$script | Should Match '\$stableSeedPolls -ge 3'
		$script | Should Match 'Chat seed did not become stably render-ready'
	}

	It 'bounds the timeline cache and exposes delegate materialization evidence' {
		$qml | Should Match 'cacheBuffer:\s*Math\.max\(256,\s*Math\.min\(720,\s*height\)\)'
		$qml | Should Match 'id:\s*messageAttachmentLoader[\s\S]*active:\s*messageDelegate\.hasAttachmentContent'
		$qml | Should Match 'id:\s*messagePreviewLoader[\s\S]*active:\s*messageDelegate\.hasPreviewContent'
		$qml | Should Match 'timelineDelegateDiagnostics[\s\S]*"materialized"[\s\S]*"previewItems"[\s\S]*"attachmentItems"'
		$qml | Should Match 'performanceChatScrollState[\s\S]*"delegateDiagnosticsDelta"'
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
		$analysis.model_reset_total | Should Be 0
	}

	It 'normalizes and gates every supported model-reset trace counter' {
		$analysis = Get-ChatPerfTraceAnalysis -TraceLines @(
			'[chat-perf][value] qml.rooms.model_reset count=1 total=2 avg=2.00 max=2',
			'[chat-perf][value] qml.participant.model_reset count=1 total=1 avg=1.00 max=1',
			'[chat-perf][value] qml.navigation.model_reset count=1 total=3 avg=3.00 max=3',
			'[chat-perf][value] qml.chat.model_reset count=1 total=4 avg=4.00 max=4',
			'[chat-perf][value] qml.actions.model_reset count=1 total=5 avg=5.00 max=5',
			'[chat-perf][value] qml.operations.model_reset count=1 total=6 avg=6.00 max=6'
		)

		$analysis.model_reset_line_count | Should Be 6
		$analysis.model_reset_total | Should Be 21
		$analysis.model_reset_counts.room | Should Be 2
		$analysis.model_reset_counts.participant | Should Be 1
		$analysis.model_reset_counts.navigation | Should Be 3
		$analysis.model_reset_counts.chat | Should Be 4
		$analysis.model_reset_counts.action | Should Be 5
		$analysis.model_reset_counts.operation | Should Be 6
		$script | Should Match 'no_model_reset_trace = \$traceAnalysis\.model_reset_total -eq 0'
	}

	It 'fails closed unless all model-reset counters exist in every phase snapshot' {
		$required = @('room', 'participant', 'navigation', 'chat', 'action', 'operation')
		$missing = Get-QmlModelResetAnalysis -Performance ([pscustomobject]@{}) -RequiredCounters $required
		$missing.measured | Should Be $false
		$missing.reason | Should Match 'does not expose modelResetCounts'

		$complete = Get-QmlModelResetAnalysis -Performance ([pscustomobject]@{
			modelResetCounts = [pscustomobject]@{
				room = 0; participant = 0; navigation = 0; chat = 0; action = 0; operation = 0
			}
		}) -RequiredCounters $required
		$complete.measured | Should Be $true
		$complete.total | Should Be 0
		$script | Should Match 'no_room_switch_model_resets'
		$script | Should Match 'no_chat_scroll_model_resets'
		$script | Should Match 'no_talk_state_model_resets'
		$script | Should Match 'model_reset_counter_run_count -ne 5'
	}

	It 'fails closed on synchronous UI operations in every measured phase and idle' {
		$missing = Get-QmlSyncUiOperationAnalysis -Performance ([pscustomobject]@{})
		$missing.measured | Should Be $false
		$missing.passed | Should Be $false

		$clean = Get-QmlSyncUiOperationAnalysis -Performance ([pscustomobject]@{
			syncUiOperationViolationCounts = [pscustomobject]@{ network = 0; plugin = 0; file = 0 }
			noSyncUiOperationsPassed = $true
		})
		$clean.measured | Should Be $true
		$clean.passed | Should Be $true
		$clean.total | Should Be 0

		$violation = Get-QmlSyncUiOperationAnalysis -Performance ([pscustomobject]@{
			syncUiOperationViolationCounts = [pscustomobject]@{ network = 1; plugin = 2; file = 3 }
			noSyncUiOperationsPassed = $false
		})
		$violation.measured | Should Be $true
		$violation.passed | Should Be $false
		$violation.total | Should Be 6

		$inconsistent = Get-QmlSyncUiOperationAnalysis -Performance ([pscustomobject]@{
			syncUiOperationViolationCounts = [pscustomobject]@{ network = 0; plugin = 0; file = 0 }
			noSyncUiOperationsPassed = $false
		})
		$inconsistent.measured | Should Be $true
		$inconsistent.passed | Should Be $false

		$extraCounter = Get-QmlSyncUiOperationAnalysis -Performance ([pscustomobject]@{
			syncUiOperationViolationCounts = [pscustomobject]@{ network = 0; plugin = 0; file = 0; other = 0 }
			noSyncUiOperationsPassed = $true
		})
		$extraCounter.measured | Should Be $false

		$script | Should Match 'connectedIdle\.performance = Get-QmlPerformanceSnapshot'
		$script | Should Match 'no_connected_idle_sync_ui_operations'
		$script | Should Match 'no_room_switch_sync_ui_operations'
		$script | Should Match 'no_chat_scroll_sync_ui_operations'
		$script | Should Match 'no_talk_state_sync_ui_operations'
		$script | Should Match 'no_sync_ui_network_operations'
		$script | Should Match 'no_sync_ui_plugin_operations'
		$script | Should Match 'no_sync_ui_file_operations'
	}

	It 'reports the bounded static and dynamic synchronous-I/O detection scope honestly' {
		$script | Should Match 'sync_ui_operation_detection_scope = \$syncUiOperationDetectionScope'
		$script | Should Match 'classification = "bounded-hot-path-contract"'
		$script | Should Match 'dynamic_counter_external_call_sites = 0'
		$script | Should Match 'does not provide whole-process interception'

		$syncReporterMatches = @(Get-ChildItem -LiteralPath (Join-Path $repoRoot 'src\mumble') -Recurse -File `
			-Include '*.cpp', '*.h' | Select-String -Pattern 'recordSyncUiOperationViolation')
		$externalSyncReporterCallSites = @($syncReporterMatches | Where-Object {
			$_.Line -notmatch '^\s*void\s+(?:QmlPerformanceMonitor::)?recordSyncUiOperationViolation\s*\('
		})
		$externalSyncReporterCallSites.Count | Should Be 0
	}

	It 'binds performance evidence to a stable candidate executable and source revision' {
		$script | Should Match '\[string\]\$CandidateId = ""'
		$script | Should Match '\[string\]\$SourceCommit = ""'
		$script | Should Match '\$executableSha256 = \(Get-FileHash -LiteralPath \$executablePath -Algorithm SHA256\)\.Hash\.ToLowerInvariant\(\)'
		$script | Should Match '\$resolvedSourceCommit = \$SourceCommit\.Trim\(\)\.ToLowerInvariant\(\)'
		$script | Should Match 'git -C \$repoRoot rev-parse HEAD'
		$script | Should Match '\$resolvedCandidateId = "windows-qml-\$sourceToken-\$\(\$executableSha256\.Substring\(0, 12\)\)"'
		$script | Should Match 'candidate_id = \$resolvedCandidateId'
		$script | Should Match 'source_commit = if \(\[string\]::IsNullOrWhiteSpace\(\$resolvedSourceCommit\)\)'
		$script | Should Match 'executable_sha256 = \$executableSha256'
		$script | Should Match 'executable_sha256_after = \$executableSha256After'
		$script | Should Match 'executable_unchanged_during_runs = \$executableUnchangedDuringRuns'
	}

	It 'requires a same-machine schema-v2 Web baseline before the first client launch' {
		$script | Should Match 'performanceSchemaVersion = 2'
		$script | Should Match 'performanceContractId = "windows-qml-performance-v2"'
		$script | Should Match 'function Get-WindowsReferenceMachineFingerprint'
		$script | Should Match 'kind must be ''webengine_reference_baseline'''
		$script | Should Match 'frontend must be ''web-reference'''
		$script | Should Match 'machine_fingerprint_sha256 does not match this reference machine'
		$script | Should Match 'profile_seed_sha256 does not match the candidate''s frozen config/database snapshot'
		$script | Should Match 'summary\.chromium_renderer_confirmed_runs must be exactly five'
		$script | Should Match 'frozen_profile_provenance\.profile_seed_sha256 does not match the candidate''s frozen config/database snapshot'
		$script | Should Match 'frozen_profile_provenance\.source_config_sha256 does not match the candidate config snapshot'
		$script | Should Match 'frozen_profile_provenance\.source_database_sha256 does not match the candidate SQLite seed'
		$script | Should Match 'summary\.runs must be exactly five'
		$script | Should Match 'summary\.startup_to_interactive_median_ms must be a positive finite number'
		$script | Should Match 'summary\.connected_idle_working_set_median_bytes must be a positive finite number'
		$script | Should Match 'startup_to_interactive_median_ms -le \(\$baselineStartupMedian \* 0\.8\)'
		$script | Should Match 'connected_idle_working_set_median_bytes -le \(\$baselineIdleWorkingSetMedian \* 0\.75\)'
		$script | Should Match 'legacy_startup_to_window_median_ms'
		$script | Should Match 'legacy_idle_working_set_median_bytes'
		$script | Should Match 'schema_version = \$performanceSchemaVersion'
		$script | Should Match 'profile_seed_sha256 = \[string\]\$frozenProfileSeed\.profile_seed_sha256'
		$script | Should Match 'source_config_sha256 = \[string\]\$frozenProfileSeed\.source_config_sha256'
		$script | Should Match 'source_database_sha256 = \[string\]\$frozenProfileSeed\.source_database_sha256'
		$script | Should Match 'baseline = \$baselineDiagnostics'
		$preflightIndex = $script.IndexOf('$validatedWebBaseline = Get-ValidatedWebPerformanceBaseline')
		$launchIndex = $script.IndexOf('$process = Start-ProcessInStartTraceJob')
		($preflightIndex -ge 0 -and $launchIndex -gt $preflightIndex) | Should Be $true
	}

	It 'cryptographically binds the Web baseline to all five renderer runs and the frozen QML profile' {
		$root = Join-Path ([IO.Path]::GetTempPath()) ('qml-perf-web-baseline-' + [Guid]::NewGuid().ToString('N'))
		New-Item -ItemType Directory -Path $root | Out-Null
		try {
			$databasePath = Join-Path $root 'source.sqlite'
			[IO.File]::WriteAllBytes($databasePath, [byte[]](1, 3, 3, 7))
			$configPath = Join-Path $root 'source.json'
			[IO.File]::WriteAllText(
				$configPath,
				(@{ misc = @{ database_location = $databasePath }; marker = 'candidate' } | ConvertTo-Json -Depth 10),
				[Text.UTF8Encoding]::new($false)
			)
			$seed = Get-FrozenProfileSeed -SourceConfigPath $configPath
			$baselinePath = Join-Path $root 'baseline.json'
			$document = [ordered]@{
				schema_version = 2
				contract_id = 'windows-qml-performance-v2'
				kind = 'webengine_reference_baseline'
				frontend = 'web-reference'
				machine_fingerprint_sha256 = 'machine-fixture'
				profile_seed_sha256 = [string]$seed.profile_seed_sha256
				frozen_profile_provenance = [ordered]@{
					profile_seed_sha256 = [string]$seed.profile_seed_sha256
					source_config_sha256 = [string]$seed.source_config_sha256
					source_database_sha256 = [string]$seed.source_database_sha256
				}
				summary = [ordered]@{
					runs = 5
					chromium_renderer_confirmed_runs = 5
					startup_to_interactive_median_ms = 1000.0
					connected_idle_working_set_median_bytes = 400000000
				}
			}
			[IO.File]::WriteAllText($baselinePath, ($document | ConvertTo-Json -Depth 10), [Text.UTF8Encoding]::new($false))
			$validated = Get-ValidatedWebPerformanceBaseline -Path $baselinePath `
				-MachineFingerprint 'machine-fixture' -FrozenProfileSeed $seed `
				-ExpectedSchemaVersion 2 -ExpectedContractId 'windows-qml-performance-v2'
			$validated.diagnostics.kind | Should Be 'webengine_reference_baseline'
			$validated.diagnostics.chromium_renderer_confirmed_runs | Should Be 5
			$validated.diagnostics.frozen_profile_provenance.matches_candidate | Should Be $true

			$document['kind'] = 'qml_candidate'
			[IO.File]::WriteAllText($baselinePath, ($document | ConvertTo-Json -Depth 10), [Text.UTF8Encoding]::new($false))
			(Get-Content -Raw -LiteralPath $baselinePath | ConvertFrom-Json).kind | Should Be 'qml_candidate'
			$threw = $false
			try { Get-ValidatedWebPerformanceBaseline -Path $baselinePath -MachineFingerprint 'machine-fixture' -FrozenProfileSeed $seed -ExpectedSchemaVersion 2 -ExpectedContractId 'windows-qml-performance-v2' | Out-Null } catch { $threw = $true }
			$threw | Should Be $true
			$document['kind'] = 'webengine_reference_baseline'

			$document['summary']['chromium_renderer_confirmed_runs'] = 4
			[IO.File]::WriteAllText($baselinePath, ($document | ConvertTo-Json -Depth 10), [Text.UTF8Encoding]::new($false))
			$threw = $false
			try { Get-ValidatedWebPerformanceBaseline -Path $baselinePath -MachineFingerprint 'machine-fixture' -FrozenProfileSeed $seed -ExpectedSchemaVersion 2 -ExpectedContractId 'windows-qml-performance-v2' | Out-Null } catch { $threw = $true }
			$threw | Should Be $true
			$document['summary']['chromium_renderer_confirmed_runs'] = 5

			$document['frozen_profile_provenance']['source_config_sha256'] = ('0' * 64)
			[IO.File]::WriteAllText($baselinePath, ($document | ConvertTo-Json -Depth 10), [Text.UTF8Encoding]::new($false))
			$threw = $false
			try { Get-ValidatedWebPerformanceBaseline -Path $baselinePath -MachineFingerprint 'machine-fixture' -FrozenProfileSeed $seed -ExpectedSchemaVersion 2 -ExpectedContractId 'windows-qml-performance-v2' | Out-Null } catch { $threw = $true }
			$threw | Should Be $true
			$document['frozen_profile_provenance']['source_config_sha256'] = [string]$seed.source_config_sha256

			$document['frozen_profile_provenance']['source_database_sha256'] = ('1' * 64)
			[IO.File]::WriteAllText($baselinePath, ($document | ConvertTo-Json -Depth 10), [Text.UTF8Encoding]::new($false))
			$threw = $false
			try { Get-ValidatedWebPerformanceBaseline -Path $baselinePath -MachineFingerprint 'machine-fixture' -FrozenProfileSeed $seed -ExpectedSchemaVersion 2 -ExpectedContractId 'windows-qml-performance-v2' | Out-Null } catch { $threw = $true }
			$threw | Should Be $true
			$document['frozen_profile_provenance']['source_database_sha256'] = [string]$seed.source_database_sha256

			$document['frozen_profile_provenance']['profile_seed_sha256'] = ('2' * 64)
			[IO.File]::WriteAllText($baselinePath, ($document | ConvertTo-Json -Depth 10), [Text.UTF8Encoding]::new($false))
			$threw = $false
			try { Get-ValidatedWebPerformanceBaseline -Path $baselinePath -MachineFingerprint 'machine-fixture' -FrozenProfileSeed $seed -ExpectedSchemaVersion 2 -ExpectedContractId 'windows-qml-performance-v2' | Out-Null } catch { $threw = $true }
			$threw | Should Be $true
		} finally {
			Remove-Item -LiteralPath $root -Recurse -Force
		}
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
