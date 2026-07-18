$ErrorActionPreference = 'Stop'

Describe 'Screen-share capability probing lifecycle' {
	BeforeAll {
		$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
		$helperSource = Get-Content -LiteralPath (Join-Path $repoRoot 'src\mumble\ScreenShareHelperClient.cpp') -Raw
		$managerSource = Get-Content -LiteralPath (Join-Path $repoRoot 'src\mumble\ScreenShareManager.cpp') -Raw
		$mainWindowSource = Get-Content -LiteralPath (Join-Path $repoRoot 'src\mumble\MainWindow.cpp') -Raw

		function Get-FunctionBody {
			param(
				[Parameter(Mandatory)]
				[string] $Source,
				[Parameter(Mandatory)]
				[string] $Signature
			)

			$start = $Source.IndexOf($Signature, [System.StringComparison]::Ordinal)
			if ($start -lt 0) {
				throw "Missing source contract signature: $Signature"
			}
			$openBrace = $Source.IndexOf('{', $start)
			if ($openBrace -lt 0) {
				throw "Missing function body for source contract: $Signature"
			}

			$depth = 0
			for ($index = $openBrace; $index -lt $Source.Length; ++$index) {
				switch ($Source[$index]) {
					'{' { ++$depth }
					'}' {
						--$depth
						if ($depth -eq 0) {
							return $Source.Substring($openBrace, $index - $openBrace + 1)
						}
					}
				}
			}

			throw "Unterminated function body for source contract: $Signature"
		}
	}

	It 'does not probe or launch the helper from ScreenShareHelperClient construction' {
		$constructor = Get-FunctionBody -Source $helperSource -Signature 'ScreenShareHelperClient::ScreenShareHelperClient(QObject *parent)'
		$constructor | Should Not Match 'refreshCapabilities|singleShot|detectLocalCapabilities'
	}

	It 'probes on real share state and reconciles stored sessions after completion' {
		$stateHandler = Get-FunctionBody -Source $managerSource -Signature 'void ScreenShareManager::handleScreenShareState('
		$stateHandler | Should Match '!m_helperClient->capabilities\(\)\.probeComplete'
		$stateHandler | Should Match 'm_helperClient->refreshCapabilities\(\)'
		$stateHandler | Should Match 'reconcileSession\(session\)'

		$managerSource | Should Match '&ScreenShareHelperClient::capabilitiesChanged[\s\S]*&ScreenShareManager::reconcileSessionsAfterCapabilityRefresh'
		$reconcile = Get-FunctionBody -Source $managerSource -Signature 'void ScreenShareManager::reconcileSessionsAfterCapabilityRefresh()'
		$reconcile | Should Match 'm_sessions\.keys\(\)'
		$reconcile | Should Match 'reconcileSession\(sessionIt\.value\(\)\)'
	}

	It 'opens a cancellable picker immediately and completes it by stable channel ID after probing' {
		$openDialog = Get-FunctionBody -Source $mainWindowSource -Signature 'void MainWindow::openModernScreenShareDialog('
		$openDialog | Should Match '!m_screenShareManager->helperClient\(\)\.capabilities\(\)\.probeComplete'
		$openDialog | Should Match 'm_pendingScreenShareDialogChannelID = channel->iId'
		$openDialog | Should Match 'openModernGenericDialog\(buildModernScreenShareDialogDto\(channel\)\)'
		$openDialog | Should Match 'refreshCapabilities\(\)'
		$openDialog | Should Not Match 'publishModernToast'

		$mainWindowSource | Should Match '&ScreenShareHelperClient::capabilitiesChanged[\s\S]*m_pendingScreenShareDialogChannelID[\s\S]*Channel::get\(channelID\)'
		$dialogAction = Get-FunctionBody -Source $mainWindowSource -Signature 'bool MainWindow::handleModernScreenShareDialogAction('
		$dialogAction | Should Match 'm_pendingScreenShareDialogChannelID\.reset\(\)'
		$dialogAction | Should Match 'screenShare\.retryRuntime'
	}

	It 'keeps the foreground share action enabled when the lazy probe is its only blocker' {
		$setupCheck = Get-FunctionBody -Source $managerSource -Signature 'bool ScreenShareManager::canOpenLocalShareSetup() const'
		$setupCheck | Should Match '!m_helperClient->capabilities\(\)\.probeComplete'
		$setupCheck | Should Match 'return true'

		$scopeState = Get-FunctionBody -Source $mainWindowSource -Signature 'QVariantMap MainWindow::buildModernShellVoiceRoomScreenShareState('
		$scopeState | Should Match 'joinedRoom && m_screenShareManager->canOpenLocalShareSetup\(\)'
	}
}
