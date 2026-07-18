[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$scriptsRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Import-Module (Join-Path $scriptsRoot "InputEnhancementReleaseTools.psm1") -Force

function Invoke-NativeChecked {
	param(
		[Parameter(Mandatory = $true)]
		[string]$Command,
		[Parameter(Mandatory = $true)]
		[string[]]$Arguments
	)

	& $Command @Arguments | Out-Null
	if ($LASTEXITCODE -ne 0) {
		throw "Native command '$Command $($Arguments -join ' ')' failed with exit code $LASTEXITCODE."
	}
}

function Assert-Throws {
	param(
		[Parameter(Mandatory = $true)]
		[scriptblock]$Script,
		[Parameter(Mandatory = $true)]
		[string]$Description
	)

	$threw = $false
	try {
		& $Script
	} catch {
		$threw = $true
		Write-Host "Expected rejection ($Description): $($_.Exception.Message)"
	}
	if (-not $threw) {
		throw "Expected failure did not occur: $Description."
	}
}

function Get-TestUtf8Sha256 {
	param([Parameter(Mandatory = $true)][string]$Value)
	$sha = [Security.Cryptography.SHA256]::Create()
	try {
		$bytes = [Text.UTF8Encoding]::new($false).GetBytes($Value)
		return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant()
	} finally {
		$sha.Dispose()
	}
}

function Get-TestRawPublicKeySha256 {
	param([Parameter(Mandatory = $true)][string]$PublicKeyHex)
	$normalized = Assert-Ed25519PublicKeyHex -PublicKeyHex $PublicKeyHex
	[byte[]]$bytes = [byte[]]::new(32)
	for ($index = 0; $index -lt $bytes.Length; $index++) {
		$bytes[$index] = [Convert]::ToByte($normalized.Substring($index * 2, 2), 16)
	}
	$sha = [Security.Cryptography.SHA256]::Create()
	try {
		return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant()
	} finally {
		$sha.Dispose()
	}
}

function Initialize-TestQtQuickPayload {
	param([Parameter(Mandatory = $true)][string]$Root)

	$requiredPaths = @(
		'mumble.exe',
		'mumble-updater.exe',
		'Qt6Core.dll',
		'Qt6Gui.dll',
		'Qt6Qml.dll',
		'Qt6Quick.dll',
		'Qt6QuickControls2.dll',
		'Qt6QuickControls2Basic.dll',
		'Qt6QuickControls2BasicStyleImpl.dll',
		'Qt6QuickControls2Impl.dll',
		'Qt6QuickDialogs2.dll',
		'Qt6QuickDialogs2QuickImpl.dll',
		'Qt6QuickLayouts.dll',
		'Qt6QuickShapes.dll',
		'Qt6QuickTemplates2.dll',
		'Qt6WebEngineCore.dll',
		'Qt6WebEngineQuick.dll',
		'QtWebEngineProcess.exe',
		'platforms/qwindows.dll',
		'tls/qopensslbackend.dll',
		'qml/QtQuick/qmldir',
		'qml/QtQuick/Controls/qmldir',
		'qml/QtQuick/Controls/qtquickcontrols2plugin.dll',
		'qml/QtQuick/Controls/Basic/qmldir',
		'qml/QtQuick/Controls/Basic/qtquickcontrols2basicstyleplugin.dll',
		'qml/QtQuick/Controls/Basic/impl/qmldir',
		'qml/QtQuick/Controls/Basic/impl/qtquickcontrols2basicstyleimplplugin.dll',
		'qml/QtQuick/Controls/impl/qmldir',
		'qml/QtQuick/Controls/impl/qtquickcontrols2implplugin.dll',
		'qml/QtQuick/Layouts/qmldir',
		'qml/QtQuick/Layouts/qquicklayoutsplugin.dll',
		'qml/QtQuick/Dialogs/qmldir',
		'qml/QtQuick/Dialogs/qtquickdialogsplugin.dll',
		'qml/QtQuick/Dialogs/quickimpl/qmldir',
		'qml/QtQuick/Dialogs/quickimpl/qtquickdialogs2quickimplplugin.dll',
		'qml/QtQuick/Shapes/qmldir',
		'qml/QtQuick/Shapes/qmlshapesplugin.dll',
		'qml/QtQuick/Templates/qmldir',
		'qml/QtQuick/Templates/qtquicktemplates2plugin.dll',
		'qml/QtWebEngine/qmldir',
		'qml/QtWebEngine/qtwebenginequickplugin.dll',
		'resources/icudtl.dat',
		'resources/qtwebengine_resources.pak',
		'translations/qtwebengine_locales/en-US.pak',
		'qt.conf',
		'direct-runtime-dependencies.txt'
	)

	foreach ($relativePath in $requiredPaths) {
		$path = Join-Path $Root $relativePath
		$parent = Split-Path -Parent $path
		New-Item -ItemType Directory -Force -Path $parent | Out-Null
		if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
			[IO.File]::WriteAllBytes($path, [byte[]](1))
		}
	}
	[IO.File]::WriteAllLines(
		(Join-Path $Root 'direct-runtime-dependencies.txt'),
		@('Qt6Quick.dll', 'Qt6Qml.dll', 'Qt6WebEngineQuick.dll', 'Qt6WebEngineCore.dll'),
		[Text.UTF8Encoding]::new($false)
	)

	& (Join-Path $scriptsRoot 'new-windows-runtime-manifest.ps1') -StageRoot $Root
}

function Get-TestRolloutWindowSha256 {
	param(
		[string]$QuerySha256,
		[string]$SourceSnapshotSha256,
		[DateTimeOffset]$WindowStartUtc,
		[DateTimeOffset]$WindowEndUtc,
		[int]$ObservationDays,
		[string]$SourceChannel,
		[string]$RolloutAudience,
		[string]$RecipeSetVersion,
		[string[]]$TestedBuildIds
	)
	$buildIds = @($TestedBuildIds)
	[Array]::Sort($buildIds, [StringComparer]::Ordinal)
	$canonical = @(
		'input-enhancement-rollout-window-v2',
		"querySha256=$QuerySha256",
		"sourceSnapshotSha256=$SourceSnapshotSha256",
		"startUtc=$($WindowStartUtc.ToUniversalTime().ToString("yyyy-MM-dd'T'HH:mm:ss'Z'"))",
		"endUtc=$($WindowEndUtc.ToUniversalTime().ToString("yyyy-MM-dd'T'HH:mm:ss'Z'"))",
		"observationDays=$ObservationDays",
		"sourceChannel=$SourceChannel",
		"rolloutAudience=$RolloutAudience",
		"recipeSetVersion=$RecipeSetVersion",
		"testedBuildIds=$([string]::Join(',', $buildIds))"
	) -join "`n"
	return Get-TestUtf8Sha256 -Value "$canonical`n"
}

function Write-SignedRolloutAggregateFixture {
	param(
		[string]$Path,
		[string]$SignaturePath,
		[string]$PrivateKeyBase64,
		[string]$PublicKeyHex,
		[string]$OpenSslPath,
		[string]$QuerySha256,
		[string]$SourceSnapshotSha256,
		[string]$SourceChannel,
		[string]$RolloutAudience = 'private-community',
		[string[]]$TestedBuildIds,
		[string]$RecipeSetVersion,
		[DateTimeOffset]$WindowStartUtc,
		[DateTimeOffset]$WindowEndUtc,
		[int]$ObservationDays,
		[int]$DistinctUsers,
		[int]$DistinctDevices,
		[int]$IntendedCommunityDevices = 1,
		[double]$TalkHours,
		[DateTimeOffset]$GeneratedAtUtc = [DateTimeOffset]::UtcNow,
		[int]$P0Count = 0,
		[int]$P1Count = 0,
		[int]$ModelHashMismatchCount = 0,
		[int]$RecurrentCallbackRegressionCount = 0,
		[double]$CrashFreeSessionRate = 1.0,
		[double]$FallbackSessionRate = 0.0,
		[double]$CallbackOverrunFrameRate = 0.0,
		[double]$ManualRollbackOrOptOutRate = 0.0,
		[int]$BlindAbResponses = 0,
		[double]$SelectedOverOriginalRate = 0.0,
		[string]$WindowSha256Override = ''
	)
	$sortedBuildIds = @($TestedBuildIds)
	[Array]::Sort($sortedBuildIds, [StringComparer]::Ordinal)
	$windowSha256 = Get-TestRolloutWindowSha256 `
		-QuerySha256 $QuerySha256 -SourceSnapshotSha256 $SourceSnapshotSha256 `
		-WindowStartUtc $WindowStartUtc -WindowEndUtc $WindowEndUtc -ObservationDays $ObservationDays `
		-SourceChannel $SourceChannel -RolloutAudience $RolloutAudience `
		-RecipeSetVersion $RecipeSetVersion -TestedBuildIds $sortedBuildIds
	if (-not [string]::IsNullOrWhiteSpace($WindowSha256Override)) { $windowSha256 = $WindowSha256Override }
	$aggregate = [ordered]@{
		schemaVersion = 2
		kind = 'input-enhancement-telemetry-aggregate-export'
		generatedAtUtc = $GeneratedAtUtc.ToUniversalTime().ToString("yyyy-MM-dd'T'HH:mm:ss'Z'")
		sourceChannel = $SourceChannel
		rolloutAudience = $RolloutAudience
		testedBuildIds = $sortedBuildIds
		recipeSetVersion = $RecipeSetVersion
		window = [ordered]@{
			startUtc = $WindowStartUtc.ToUniversalTime().ToString("yyyy-MM-dd'T'HH:mm:ss'Z'")
			endUtc = $WindowEndUtc.ToUniversalTime().ToString("yyyy-MM-dd'T'HH:mm:ss'Z'")
			observationDays = $ObservationDays
		}
		query = [ordered]@{
			id = 'input-enhancement-rollout-v2'
			sha256 = $QuerySha256
			sourceEventCount = 1000
			sourceSnapshotSha256 = $SourceSnapshotSha256
			windowSha256 = $windowSha256
		}
		population = [ordered]@{
			distinctUsers = $DistinctUsers
			distinctDevices = $DistinctDevices
			intendedCommunityDevices = $IntendedCommunityDevices
			talkHours = $TalkHours
		}
		reliability = [ordered]@{
			p0Count = $P0Count
			p1Count = $P1Count
			modelHashMismatchCount = $ModelHashMismatchCount
			recurrentCallbackRegressionCount = $RecurrentCallbackRegressionCount
			crashFreeSessionRate = $CrashFreeSessionRate
			fallbackSessionRate = $FallbackSessionRate
			callbackOverrunFrameRate = $CallbackOverrunFrameRate
			manualRollbackOrOptOutRate = $ManualRollbackOrOptOutRate
		}
		preference = [ordered]@{
			blindAbResponses = $BlindAbResponses
			selectedOverOriginalRate = $SelectedOverOriginalRate
		}
		privacy = [ordered]@{
			optInOnly = $true
			rawAudioIncluded = $false
			transcriptsIncluded = $false
			voiceprintsIncluded = $false
			rawDeviceIdsIncluded = $false
			retentionDays = 30
		}
	}
	Write-ReleaseJson -Path $Path -Value $aggregate
	Protect-FileWithEd25519 -InputPath $Path -SignaturePath $SignaturePath `
		-PrivateKeyBase64 $PrivateKeyBase64 -ExpectedPublicKeyHex $PublicKeyHex -OpenSslPath $OpenSslPath
}

function Write-SignedRnnoiseDecisionFixture {
	param(
		[string]$Path,
		[string]$SignaturePath,
		[string]$PrivateKeyBase64,
		[string]$PublicKeyHex,
		[string]$OpenSslPath,
		[ValidateSet('embedded-retained', 'custom-selected')]
		[string]$Status = 'embedded-retained'
	)
	$customSelected = $Status -ceq 'custom-selected'
	[object[]]$reasonCodes = @()
	if (-not $customSelected) {
		$reasonCodes = [object[]]@('bootstrap.ovrl_lower_bound_not_positive')
	}
	$decision = [ordered]@{
		bootstrap = [ordered]@{
			confidence = 0.95
			iterations = 20000
			lower_bound = if ($customSelected) { 0.01 } else { -0.01 }
			median_improvement = if ($customSelected) { 0.02 } else { 0.0 }
			metric = 'paired per-case OVRL improvement, median bootstrap'
			sampler = 'splitmix64-v1'
			seed_sha256 = ('1' * 64)
		}
		custom_model = if ($customSelected) {
			[ordered]@{
				candidate_id = 'seed-0001'
				manifest_relative_path = 'rnnoise/custom/self-test/seed-0001.weights_blob.bin'
				model_id = 'rnnoise:custom:self-test:seed-0001'
				sha256 = ('2' * 64)
				size_bytes = 1234
			}
		} else { $null }
		embedded_reference = [ordered]@{
			license_spdx = 'BSD-3-Clause'
			model_id = 'rnnoise:embedded'
			sha256 = ('3' * 64)
			size_bytes = 4321
		}
		holdout_mixture_plan_sha256 = ('4' * 64)
		holdout_results_sha256 = ('5' * 64)
		one_shot_receipt_sha256 = ('6' * 64)
		reason_codes = $reasonCodes
		schema_version = 1
		status = $Status
		training_plan_sha256 = ('7' * 64)
		validation_selection_sha256 = ('8' * 64)
	}
	Write-ReleaseJson -Path $Path -Value $decision
	Protect-FileWithEd25519 -InputPath $Path -SignaturePath $SignaturePath `
		-PrivateKeyBase64 $PrivateKeyBase64 -ExpectedPublicKeyHex $PublicKeyHex -OpenSslPath $OpenSslPath
}

$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) "mumble-input-enhancement-release-$([guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Force -Path $tempRoot | Out-Null
try {
	$openSsl = Resolve-InputEnhancementOpenSsl
	$privateKeyPath = Join-Path $tempRoot 'release-ed25519-private.pem'
	Invoke-NativeChecked -Command $openSsl -Arguments @('genpkey', '-algorithm', 'Ed25519', '-out', $privateKeyPath)
	$privateKeyBase64 = [Convert]::ToBase64String([IO.File]::ReadAllBytes($privateKeyPath))
	$publicKeyHex = Get-Ed25519PublicKeyHexFromPrivateKey `
		-PrivateKeyBase64 $privateKeyBase64 -OpenSslPath $openSsl
	$aggregatePrivateKeyPath = Join-Path $tempRoot 'aggregate-ed25519-private.pem'
	Invoke-NativeChecked -Command $openSsl -Arguments @('genpkey', '-algorithm', 'Ed25519', '-out', $aggregatePrivateKeyPath)
	$aggregatePrivateKeyBase64 = [Convert]::ToBase64String([IO.File]::ReadAllBytes($aggregatePrivateKeyPath))
	$aggregatePublicKeyHex = Get-Ed25519PublicKeyHexFromPrivateKey `
		-PrivateKeyBase64 $aggregatePrivateKeyBase64 -OpenSslPath $openSsl
	if ($aggregatePublicKeyHex -ceq $publicKeyHex) { throw 'Aggregate and release fixture keys unexpectedly match.' }

	$sourceRoot = Join-Path $tempRoot "source"
	New-Item -ItemType Directory -Force -Path $sourceRoot | Out-Null
	Invoke-NativeChecked -Command "git" -Arguments @("-C", $sourceRoot, "init", "-q")
	Invoke-NativeChecked -Command "git" -Arguments @("-C", $sourceRoot, "config", "user.email", "release-tools@example.invalid")
	Invoke-NativeChecked -Command "git" -Arguments @("-C", $sourceRoot, "config", "user.name", "Release Tools Test")
	Invoke-NativeChecked -Command "git" -Arguments @("-C", $sourceRoot, "config", "commit.gpgsign", "false")
	[System.IO.File]::WriteAllText((Join-Path $sourceRoot "source.txt"), "clean`n")
	Invoke-NativeChecked -Command "git" -Arguments @("-C", $sourceRoot, "add", "source.txt")
	Invoke-NativeChecked -Command "git" -Arguments @("-C", $sourceRoot, "commit", "-q", "-m", "fixture")
	$sourceSha = (& git -C $sourceRoot rev-parse HEAD).Trim()
	$embeddedKeyDiagnostic = [ordered]@{
		schemaVersion = 1
		kind = 'mumble-input-enhancement-build-identity'
		buildNumber = 1
		packageVerificationMode = 'managed-signed'
		configuredPublicKeySha256 = Get-TestRawPublicKeySha256 -PublicKeyHex $publicKeyHex
	}
	$embeddedKeyDiagnosticBytes = [Text.UTF8Encoding]::new($false).GetBytes(
		($embeddedKeyDiagnostic | ConvertTo-Json -Depth 5 -Compress))
	$embeddedKeyDiagnosticSha = Get-TestUtf8Sha256 -Value (
		[Text.UTF8Encoding]::new($false).GetString($embeddedKeyDiagnosticBytes))
	$embeddedKeyAttestationPath = Join-Path $tempRoot 'embedded-key-attestation.json'
	Write-ReleaseJson -Path $embeddedKeyAttestationPath -Value ([ordered]@{
		schemaVersion = 1; kind = 'input-enhancement-embedded-key-attestation'; passed = $true; audioFree = $true
		createdAtUtc = '2026-07-15T12:00:00Z'; candidateExecutableSha256 = ('f' * 64)
		generatorSha256 = Get-ReleaseFileSha256 -Path (Join-Path $scriptsRoot 'new-input-enhancement-embedded-key-attestation.ps1')
		runtimeDiagnosticSha256 = $embeddedKeyDiagnosticSha
		runtimeDiagnosticBase64 = [Convert]::ToBase64String($embeddedKeyDiagnosticBytes)
	})
	$embeddedKeyAssert = @{
		EvidencePath = $embeddedKeyAttestationPath; ExpectedCandidateExecutableSha256 = ('f' * 64)
		ExpectedBuildNumber = 1; ExpectedPublicKeyHex = $publicKeyHex
	}
	& (Join-Path $scriptsRoot 'assert-input-enhancement-embedded-key-attestation.ps1') @embeddedKeyAssert

	$unmanagedDiagnostic = $embeddedKeyDiagnostic | ConvertTo-Json -Depth 5 -Compress | ConvertFrom-Json
	$unmanagedDiagnostic.buildNumber = 0
	$unmanagedDiagnostic.packageVerificationMode = 'unmanaged-build-zero'
	$unmanagedDiagnostic.configuredPublicKeySha256 = ''
	[byte[]]$unmanagedBytes = [Text.UTF8Encoding]::new($false).GetBytes(
		($unmanagedDiagnostic | ConvertTo-Json -Depth 5 -Compress))
	$unmanagedAttestation = Read-ReleaseJson -Path $embeddedKeyAttestationPath
	$unmanagedAttestation.runtimeDiagnosticBase64 = [Convert]::ToBase64String($unmanagedBytes)
	$unmanagedAttestation.runtimeDiagnosticSha256 = Get-TestUtf8Sha256 -Value (
		[Text.UTF8Encoding]::new($false).GetString($unmanagedBytes))
	$unmanagedAttestationPath = Join-Path $tempRoot 'embedded-key-attestation-unmanaged.json'
	Write-ReleaseJson -Path $unmanagedAttestationPath -Value $unmanagedAttestation
	Assert-Throws -Description 'build-0 unmanaged rehearsal candidate' -Script {
		$badEmbeddedKeyAssert = $embeddedKeyAssert.Clone(); $badEmbeddedKeyAssert.EvidencePath = $unmanagedAttestationPath
		& (Join-Path $scriptsRoot 'assert-input-enhancement-embedded-key-attestation.ps1') @badEmbeddedKeyAssert
	}
	$positiveUnmanagedDiagnostic = $embeddedKeyDiagnostic | ConvertTo-Json -Depth 5 -Compress | ConvertFrom-Json
	$positiveUnmanagedDiagnostic.packageVerificationMode = 'unmanaged-build-zero'
	[byte[]]$positiveUnmanagedBytes = [Text.UTF8Encoding]::new($false).GetBytes(
		($positiveUnmanagedDiagnostic | ConvertTo-Json -Depth 5 -Compress))
	$positiveUnmanagedAttestation = Read-ReleaseJson -Path $embeddedKeyAttestationPath
	$positiveUnmanagedAttestation.runtimeDiagnosticBase64 = [Convert]::ToBase64String($positiveUnmanagedBytes)
	$positiveUnmanagedAttestation.runtimeDiagnosticSha256 = Get-TestUtf8Sha256 -Value (
		[Text.UTF8Encoding]::new($false).GetString($positiveUnmanagedBytes))
	$positiveUnmanagedAttestationPath = Join-Path $tempRoot 'embedded-key-attestation-positive-unmanaged.json'
	Write-ReleaseJson -Path $positiveUnmanagedAttestationPath -Value $positiveUnmanagedAttestation
	Assert-Throws -Description 'positive build reporting unmanaged rehearsal mode' -Script {
		$badEmbeddedKeyAssert = $embeddedKeyAssert.Clone()
		$badEmbeddedKeyAssert.EvidencePath = $positiveUnmanagedAttestationPath
		& (Join-Path $scriptsRoot 'assert-input-enhancement-embedded-key-attestation.ps1') @badEmbeddedKeyAssert
	}

	$mismatchedDiagnostic = $embeddedKeyDiagnostic | ConvertTo-Json -Depth 5 -Compress | ConvertFrom-Json
	$mismatchedDiagnostic.configuredPublicKeySha256 = ('0' * 64)
	[byte[]]$mismatchedBytes = [Text.UTF8Encoding]::new($false).GetBytes(
		($mismatchedDiagnostic | ConvertTo-Json -Depth 5 -Compress))
	$mismatchedAttestation = Read-ReleaseJson -Path $embeddedKeyAttestationPath
	$mismatchedAttestation.runtimeDiagnosticBase64 = [Convert]::ToBase64String($mismatchedBytes)
	$mismatchedAttestation.runtimeDiagnosticSha256 = Get-TestUtf8Sha256 -Value (
		[Text.UTF8Encoding]::new($false).GetString($mismatchedBytes))
	$mismatchedAttestationPath = Join-Path $tempRoot 'embedded-key-attestation-mismatched.json'
	Write-ReleaseJson -Path $mismatchedAttestationPath -Value $mismatchedAttestation
	Assert-Throws -Description 'rehearsal candidate with mismatched embedded key' -Script {
		$badEmbeddedKeyAssert = $embeddedKeyAssert.Clone(); $badEmbeddedKeyAssert.EvidencePath = $mismatchedAttestationPath
		& (Join-Path $scriptsRoot 'assert-input-enhancement-embedded-key-attestation.ps1') @badEmbeddedKeyAssert
	}
	$protocolSimulationPath = Join-Path $tempRoot 'updater-protocol-v4-simulation.json'
	& (Join-Path $scriptsRoot 'test-input-enhancement-update-protocol-v4.ps1') `
		-OutputPath $protocolSimulationPath -SourceSha $sourceSha `
		-BuildId "mumble-forked-build-1-$($sourceSha.Substring(0, 12))"
	& (Join-Path $scriptsRoot 'assert-input-enhancement-updater-protocol-evidence.ps1') `
		-EvidencePath $protocolSimulationPath -ExpectedSourceSha $sourceSha `
		-ExpectedBuildId "mumble-forked-build-1-$($sourceSha.Substring(0, 12))" `
		-ExpectedCandidatePayloadSha256 ('a' * 64)
	$vmCases = New-Object System.Collections.Generic.List[object]
	foreach ($target in @(
		[ordered]@{ version = 'N-2'; payload = ('b' * 64) },
		[ordered]@{ version = 'N-1'; payload = ('c' * 64) }
	)) {
		foreach ($modeAndTriggers in @(
			[ordered]@{ mode = 'native'; triggers = @('install-failure', 'crash-before-marker', 'audio-init-failure', 'process-kill', 'power-loss-after-journal', 'power-loss-after-mutation') },
			[ordered]@{ mode = 'msi'; triggers = @('install-failure', 'crash-before-marker', 'audio-init-failure', 'process-kill', 'power-loss', 'candidate-3010', 'recovery-3010') }
		)) {
			foreach ($trigger in $modeAndTriggers.triggers) {
				$vmCases.Add([ordered]@{
					mode = $modeAndTriggers.mode; fromVersion = $target.version; trigger = $trigger
					exitCode = 1; observedPayloadSha256 = $target.payload; journalFinal = 'cleared'
					rebootCycles = if ($trigger -match '3010|power-loss') { 1 } else { 0 }
					mixedPayloadObserved = $false; residualJournalCount = 0; residualManagedFileCount = 0; passed = $true
				})
			}
		}
	}
	$vmEvidencePath = Join-Path $tempRoot 'updater-vm-evidence.json'
	Write-ReleaseJson -Path $vmEvidencePath -Value ([ordered]@{
		schemaVersion = 2; kind = 'updater-v4-vm-rollback-matrix'; passed = $true; audioFree = $true
		sourceSha = $sourceSha; buildId = "mumble-forked-build-1-$($sourceSha.Substring(0, 12))"
		challengeId = ('0' * 64)
		createdAtUtc = '2026-07-15T12:00:00Z'
		candidate = [ordered]@{ payloadSha256 = ('a' * 64); installerSha256 = ('e' * 64); executableSha256 = ('f' * 64) }
		recoveryTargets = @(
			[ordered]@{ fromVersion = 'N-2'; buildId = 'mumble-forked-build-8-111111111111'; installerSha256 = ('1' * 64); payloadSha256 = ('b' * 64) },
			[ordered]@{ fromVersion = 'N-1'; buildId = 'mumble-forked-build-9-222222222222'; installerSha256 = ('2' * 64); payloadSha256 = ('c' * 64) }
		)
		runner = [ordered]@{ class = 'protected-windows-update-vm'; isolated = $true; imageSha256 = ('3' * 64); snapshotSha256 = ('4' * 64); hardwareFingerprintSha256 = ('5' * 64); harnessSha256 = ('d' * 64); vmExecutorSha256 = ('6' * 64) }
		cases = $vmCases.ToArray()
	})
	$vmReceiptPath = Join-Path $tempRoot 'updater-vm-receipt.json'
	Write-ReleaseJson -Path $vmReceiptPath -Value ([ordered]@{
		schemaVersion = 2; kind = 'updater-v4-protected-vm-receipt'; passed = $true; audioFree = $true
		sourceSha = $sourceSha; buildId = "mumble-forked-build-1-$($sourceSha.Substring(0, 12))"
		challengeId = ('0' * 64)
		createdAtUtc = '2026-07-15T12:01:00Z'; evidenceSha256 = Get-ReleaseFileSha256 -Path $vmEvidencePath
		vmExecutorSha256 = ('6' * 64); imageSha256 = ('3' * 64); snapshotSha256 = ('4' * 64)
		hardwareFingerprintSha256 = ('5' * 64)
	})
	$vmAssert = @{
		EvidencePath = $vmEvidencePath; ExpectedSourceSha = $sourceSha
		ExpectedBuildId = "mumble-forked-build-1-$($sourceSha.Substring(0, 12))"
		ExpectedChallengeId = ('0' * 64)
		ExpectedCandidatePayloadSha256 = ('a' * 64); ExpectedCandidateInstallerSha256 = ('e' * 64)
		ExpectedCandidateExecutableSha256 = ('f' * 64); ExpectedHarnessSha256 = ('d' * 64)
		ReceiptPath = $vmReceiptPath; ExpectedReceiptSha256 = Get-ReleaseFileSha256 -Path $vmReceiptPath
		ExpectedVmExecutorSha256 = ('6' * 64); ExpectedImageSha256 = ('3' * 64)
		ExpectedSnapshotSha256 = ('4' * 64); ExpectedHardwareFingerprintSha256 = ('5' * 64)
	}
	& (Join-Path $scriptsRoot 'assert-input-enhancement-updater-vm-evidence.ps1') @vmAssert
	$tamperedVm = Read-ReleaseJson -Path $vmEvidencePath
	$tamperedVm.cases[0].mixedPayloadObserved = $true
	$tamperedVmPath = Join-Path $tempRoot 'updater-vm-evidence-tampered.json'
	Write-ReleaseJson -Path $tamperedVmPath -Value $tamperedVm
	Assert-Throws -Description 'mixed updater VM payload' -Script {
		$badVmAssert = $vmAssert.Clone(); $badVmAssert.EvidencePath = $tamperedVmPath
		& (Join-Path $scriptsRoot 'assert-input-enhancement-updater-vm-evidence.ps1') @badVmAssert
	}

	$killTracePath = Join-Path $tempRoot 'kill-switch-runtime-trace.json'
	$killClient = [ordered]@{
		pid = 4242; executableSha256 = ('f' * 64)
		startedAtUtc = '2026-07-15T12:00:00Z'; endedAtUtc = '2026-07-15T12:10:00Z'
	}
	Write-ReleaseJson -Path $killTracePath -Value ([ordered]@{
		schemaVersion = 3; kind = 'input-enhancement-policy-runtime-trace'; passed = $true; audioFree = $true
		sourceSha = $sourceSha; buildId = "mumble-forked-build-1-$($sourceSha.Substring(0, 12))"
		challengeId = ('0' * 64)
		startedAtUtc = '2026-07-15T12:00:00Z'; observationNonce = ('9' * 64)
		testedBinarySha256 = ('f' * 64); stagedPayloadSha256 = ('a' * 64); policySha256 = ('7' * 64)
		clientProcess = $killClient; events = @()
	})
	$killReceiptPath = Join-Path $tempRoot 'kill-switch-observer-receipt.json'
	Write-ReleaseJson -Path $killReceiptPath -Value ([ordered]@{
		schemaVersion = 2; kind = 'input-enhancement-policy-observer-receipt'; passed = $true; audioFree = $true
		sourceSha = $sourceSha; buildId = "mumble-forked-build-1-$($sourceSha.Substring(0, 12))"
		challengeId = ('0' * 64)
		observationNonce = ('9' * 64); observerSha256 = ('8' * 64)
		runtimeTraceSha256 = Get-ReleaseFileSha256 -Path $killTracePath
		testedBinarySha256 = ('f' * 64); stagedPayloadSha256 = ('a' * 64); policySha256 = ('7' * 64)
		clientProcess = $killClient
	})
	$killAssert = @{
		RuntimeTracePath = $killTracePath; ReceiptPath = $killReceiptPath
		ExpectedReceiptSha256 = Get-ReleaseFileSha256 -Path $killReceiptPath
		ExpectedSourceSha = $sourceSha; ExpectedBuildId = "mumble-forked-build-1-$($sourceSha.Substring(0, 12))"
		ExpectedChallengeId = ('0' * 64)
		ExpectedObserverSha256 = ('8' * 64); ExpectedTestedBinarySha256 = ('f' * 64)
		ExpectedStagedPayloadSha256 = ('a' * 64); ExpectedPolicySha256 = ('7' * 64)
	}
	& (Join-Path $scriptsRoot 'assert-input-enhancement-kill-switch-observation.ps1') @killAssert
	$tamperedKillTrace = Read-ReleaseJson -Path $killTracePath
	$tamperedKillTrace.clientProcess.pid = 4243
	$tamperedKillTracePath = Join-Path $tempRoot 'kill-switch-runtime-trace-tampered.json'
	Write-ReleaseJson -Path $tamperedKillTracePath -Value $tamperedKillTrace
	Assert-Throws -Description 'kill-switch trace without independent observer receipt binding' -Script {
		$badKillAssert = $killAssert.Clone(); $badKillAssert.RuntimeTracePath = $tamperedKillTracePath
		& (Join-Path $scriptsRoot 'assert-input-enhancement-kill-switch-observation.ps1') @badKillAssert
	}

	$previewPolicyGate = Assert-InputEnhancementPromotionPolicy `
		-Channel preview -Available $true -ForceOriginal $false `
		-RecommendedProfile Balanced -RolloutAudience private-community -RolloutEvidenceAvailable $false
	if ([bool]$previewPolicyGate.emergencyPolicy -or [bool]$previewPolicyGate.rolloutRequired -or
		[string]$previewPolicyGate.targetStage -cne 'none') {
		throw 'Preview promotion policy gate returned an invalid decision.'
	}
	Assert-Throws -Description 'preview Auto recommendation without stable evidence' -Script {
		Assert-InputEnhancementPromotionPolicy `
			-Channel preview -Available $true -ForceOriginal $false `
			-RecommendedProfile Auto -RolloutAudience public -RolloutEvidenceAvailable $false | Out-Null
	}
	$emergencyPreviewGate = Assert-InputEnhancementPromotionPolicy `
		-Channel preview -Available $false -ForceOriginal $true `
		-RecommendedProfile Auto -RolloutAudience private-community -RolloutEvidenceAvailable $false
	if (-not [bool]$emergencyPreviewGate.emergencyPolicy -or [bool]$emergencyPreviewGate.rolloutRequired) {
		throw 'Emergency preview policy gate returned an invalid decision.'
	}
	Assert-Throws -Description 'stable promotion without signed rollout evidence' -Script {
		Assert-InputEnhancementPromotionPolicy `
			-Channel stable -Available $true -ForceOriginal $false `
			-RecommendedProfile Balanced -RolloutAudience private-community -RolloutEvidenceAvailable $false | Out-Null
	}
	$communityStableGate = Assert-InputEnhancementPromotionPolicy `
		-Channel stable -Available $true -ForceOriginal $false `
		-RecommendedProfile Balanced -RolloutAudience private-community -RolloutEvidenceAvailable $true
	if (-not [bool]$communityStableGate.rolloutRequired -or
		[string]$communityStableGate.targetStage -cne 'community-stable') {
		throw 'Private community promotion did not select the isolated community-stable gate.'
	}
	$publicStableGate = Assert-InputEnhancementPromotionPolicy `
		-Channel stable -Available $true -ForceOriginal $false `
		-RecommendedProfile Balanced -RolloutAudience public -RolloutEvidenceAvailable $true
	if ([string]$publicStableGate.targetStage -cne 'stable-opt-in') {
		throw 'Public stable promotion did not preserve the later stable-opt-in gate.'
	}
	Assert-Throws -Description 'private community Auto recommendation' -Script {
		Assert-InputEnhancementPromotionPolicy `
			-Channel stable -Available $true -ForceOriginal $false `
			-RecommendedProfile Auto -RolloutAudience private-community -RolloutEvidenceAvailable $true | Out-Null
	}
	$stableAutoGate = Assert-InputEnhancementPromotionPolicy `
		-Channel stable -Available $true -ForceOriginal $false `
		-RecommendedProfile Auto -RolloutAudience public -RolloutEvidenceAvailable $true
	if (-not [bool]$stableAutoGate.rolloutRequired -or
		[string]$stableAutoGate.targetStage -cne 'auto-recommended') {
		throw 'Stable Auto promotion policy gate returned an invalid decision.'
	}
	$promotionWorkflowPath = Join-Path $scriptsRoot '..\..\.github\workflows\input-enhancement-promote.yml'
	$promotionWorkflowSource = Get-Content -LiteralPath $promotionWorkflowPath -Raw
	if ([regex]::Matches($promotionWorkflowSource, 'Assert-InputEnhancementPromotionPolicy').Count -lt 3) {
		throw 'Promotion workflow must enforce the shared policy gate before download, evidence handling, and publication.'
	}
	foreach ($bootstrapMarker in @(
		'INPUT_ENHANCEMENT_BOOTSTRAP_RECOVERY_SET_URL',
		'INPUT_ENHANCEMENT_BOOTSTRAP_RECOVERY_SET_SHA256',
		'Get-VerifiedBootstrapRecoverySet',
		'$arguments.BootstrapRecoverySetPath',
		'Get-FileHash -LiteralPath $msi.FullName -Algorithm SHA256'
	)) {
		if (-not $promotionWorkflowSource.Contains($bootstrapMarker)) {
			throw "Promotion workflow is missing protected v1-to-v2 bootstrap marker '$bootstrapMarker'."
		}
	}
	if ($promotionWorkflowSource.Contains('AllowIncompleteRecoverySet')) {
		throw 'Promotion workflow must never emit an incomplete schema-v2 recovery set.'
	}
	foreach ($rolloutAggregateMarker in @(
		'INPUT_ENHANCEMENT_TELEMETRY_AGGREGATE_PUBLIC_KEY_HEX',
		'INPUT_ENHANCEMENT_ROLLOUT_QUERY_SHA256',
		'rollout_audience',
		'-RolloutAudience',
		'input-enhancement-aggregate-export.json',
		'input-enhancement-aggregate-export.json.sig',
		'rnnoise-selection-decision.json',
		'rnnoise-selection-decision.json.sig',
		'AggregateExportPath =',
		'AggregateExportSignaturePath =',
		'AggregatePublicKeyHex =',
		'ExpectedQuerySha256 =',
		'incomplete rollout/aggregate evidence quartet'
	)) {
		if (-not $promotionWorkflowSource.Contains($rolloutAggregateMarker)) {
			throw "Promotion workflow is missing signed aggregate-export marker '$rolloutAggregateMarker'."
		}
	}
	if ($promotionWorkflowSource -notmatch '\[int\]\$pointer[.]schemaVersion -ne 2' -or
		$promotionWorkflowSource -match '\[int\]\$pointer[.]schemaVersion -ne 1' -or
		$promotionWorkflowSource.Contains('elseif (@($pointer.knownGoodTags).Count -ne 1)') -or
		-not $promotionWorkflowSource.Contains('-OutputRoot .\publisher-current-recovery-verify') -or
		-not $promotionWorkflowSource.Contains('$previousPointerForPublication.schemaVersion -eq 1')) {
		throw 'Promotion publisher must accept only the generated schema-v2 pointer, reverify its full recovery set, and support signed v1 bootstrap lineage.'
	}
	$qualifiedWorkflowPath = Join-Path $scriptsRoot '..\..\.github\workflows\input-enhancement-qualified-build.yml'
	$qualifiedWorkflowSource = Get-Content -LiteralPath $qualifiedWorkflowPath -Raw
	if (-not $qualifiedWorkflowSource.Contains("if: `${{ github.repository == '__disabled__/until-pre-azure-evidence-verifier-v1' }}") -or
		-not $qualifiedWorkflowSource.Contains('pre-Azure rehearsal') -or
		-not $qualifiedWorkflowSource.Contains('dogfood evidence')) {
		throw 'Azure qualified-build workflow must remain fail-closed until rehearsal, protected receipt, and dogfood evidence are verified.'
	}
	$compatibilityMarker = '# Existing installed builds still request the legacy stable manifest.'
	$compatibilityOffset = $promotionWorkflowSource.IndexOf($compatibilityMarker, [StringComparison]::Ordinal)
	if ($compatibilityOffset -lt 0) {
		throw 'Promotion workflow is missing the stable compatibility publication section.'
	}
	$compatibilitySource = $promotionWorkflowSource.Substring($compatibilityOffset)
	$binaryUploadOffset = $compatibilitySource.IndexOf(
		'gh release upload $legacyTag @binaryUploads', [StringComparison]::Ordinal)
	$manifestUploadOffset = $compatibilitySource.IndexOf(
		'gh release upload $legacyTag .\mumble-forked-update.json', [StringComparison]::Ordinal)
	$modernCommitOffset = $compatibilitySource.IndexOf('& $publishModernChannel', [StringComparison]::Ordinal)
	if ($binaryUploadOffset -lt 0 -or $manifestUploadOffset -le $binaryUploadOffset -or
		$modernCommitOffset -le $manifestUploadOffset -or
		$compatibilitySource.Contains('delete-asset $legacyTag $installerName') -or
		$compatibilitySource.Contains('delete-asset $legacyTag ([string]$pointer.artifact.fileName)') -or
		-not $compatibilitySource.Contains('& $restoreCompatibility') -or
		$compatibilitySource.Substring($binaryUploadOffset, $manifestUploadOffset - $binaryUploadOffset).Contains('--clobber')) {
		throw 'Stable publication must retain prior binaries, publish compatibility first, and commit the modern signed pointer last with rollback.'
	}
	$qualityWorkflowPath = Join-Path $scriptsRoot '..\..\.github\workflows\input-enhancement-quality.yml'
	$qualityWorkflowSource = Get-Content -LiteralPath $qualityWorkflowPath -Raw
	foreach ($requiredMarker in @(
		'vars.INPUT_ENHANCEMENT_QUALITY_HARNESS_SHA256',
		'$harnessSha256After',
		'quality-harness-provenance.json'
	)) {
		if (-not $qualityWorkflowSource.Contains($requiredMarker)) {
			throw "Measured quality workflow is missing protected harness binding marker '$requiredMarker'."
		}
	}
	$qualifiedWorkflowPath = Join-Path $scriptsRoot '..\..\.github\workflows\input-enhancement-qualified-build.yml'
	$qualifiedWorkflowSource = Get-Content -LiteralPath $qualifiedWorkflowPath -Raw
	if (-not $qualifiedWorkflowSource.Contains(
		'-ExpectedHarnessSha256 $env:INPUT_ENHANCEMENT_QUALITY_HARNESS_SHA256')) {
		throw 'Qualified build does not enforce the configured protected quality-harness SHA256.'
	}
	foreach ($runnerEvidenceName in @(
		'quality-master_quality-low-performance.json', 'original-voice-master_quality-low-performance.json',
		'quality-master_quality-mainstream.json', 'original-voice-master_quality-mainstream.json',
		'quality-nightly-low-performance.json', 'original-voice-nightly-low-performance.json',
		'quality-nightly-mainstream.json', 'original-voice-nightly-mainstream.json'
	)) {
		if (-not $qualifiedWorkflowSource.Contains($runnerEvidenceName)) {
			throw "Qualified build handoff is missing measured runner evidence '$runnerEvidenceName'."
		}
	}
	$rehearsalWorkflowPath = Join-Path $scriptsRoot '..\..\.github\workflows\input-enhancement-release-rehearsal.yml'
	$rehearsalWorkflowSource = Get-Content -LiteralPath $rehearsalWorkflowPath -Raw
	foreach ($requiredMarker in @(
		'actions: read', 'contents: read', 'prepare-input-enhancement-release-rehearsal.ps1',
		'finalize-input-enhancement-release-rehearsal.ps1',
		'prepare-immutable-signed-challenge', 'independently-observe-prepared-candidate',
		'finalize-once-and-stage-local-draft',
		'actions/upload-artifact@', 'actions/download-artifact@', 'remote-reverification.json',
		'INPUT_ENHANCEMENT_REHEARSAL_PREPARE_EXECUTOR_SHA256',
		'INPUT_ENHANCEMENT_REHEARSAL_FINALIZE_EXECUTOR_SHA256',
		'INPUT_ENHANCEMENT_REHEARSAL_REPLAY_LEDGER_ROOT',
		'INPUT_ENHANCEMENT_UPDATER_VM_EXECUTOR_SHA256', 'INPUT_ENHANCEMENT_UPDATER_VM_HARNESS_SHA256',
		'INPUT_ENHANCEMENT_UPDATER_VM_IMAGE_SHA256', 'INPUT_ENHANCEMENT_UPDATER_VM_SNAPSHOT_SHA256',
		'INPUT_ENHANCEMENT_UPDATER_VM_HARDWARE_FINGERPRINT_SHA256',
		'-AllowedOutputParent $env:RUNNER_TEMP'
	)) {
		if (-not $rehearsalWorkflowSource.Contains($requiredMarker)) {
			throw "Pre-Azure rehearsal workflow is missing '$requiredMarker'."
		}
	}
	$rehearsalPrepareSource = Get-Content -LiteralPath (
		Join-Path $scriptsRoot 'prepare-input-enhancement-release-rehearsal.ps1') -Raw
	foreach ($requiredMarker in @(
		'genpkey', 'EphemeralEd25519PrivateKeyPath', 'candidate_build_receipt.py',
		'privateMaterialDeleted', 'rehearsal-challenge.json'
	)) {
		if (-not $rehearsalPrepareSource.Contains($requiredMarker)) {
			throw "Pre-Azure rehearsal prepare phase is missing '$requiredMarker'."
		}
	}
	$rehearsalFinalizeSource = Get-Content -LiteralPath (
		Join-Path $scriptsRoot 'finalize-input-enhancement-release-rehearsal.ps1') -Raw
	foreach ($requiredMarker in @(
		'ReplayLedgerRoot', 'FileMode]::CreateNew', 'ExpectedChallengeId',
		'assert-input-enhancement-updater-vm-evidence.ps1',
		'assert-input-enhancement-kill-switch-observation.ps1'
	)) {
		if (-not $rehearsalFinalizeSource.Contains($requiredMarker)) {
			throw "Pre-Azure rehearsal finalize phase is missing '$requiredMarker'."
		}
	}
	$legacyInvokeSource = Get-Content -LiteralPath (
		Join-Path $scriptsRoot 'invoke-input-enhancement-release-rehearsal.ps1') -Raw
	if (-not $legacyInvokeSource.Contains('one-phase input-enhancement release rehearsal has been retired')) {
		throw 'Legacy one-phase rehearsal entry point must fail closed.'
	}

	# Narrow, audio-free fixture for the prepare/finalize boundary. This proves
	# that the challenge covers both complete trees, rejects a post-prepare byte
	# mutation, rejects a different candidate receipt and rejects replay.
	$challengeRoot = Join-Path $tempRoot 'rehearsal-challenge-fixture'
	$unsignedRoot = Join-Path $challengeRoot 'unsigned'
	$signedRoot = Join-Path $challengeRoot 'signed'
	New-Item -ItemType Directory -Force -Path $unsignedRoot, $signedRoot | Out-Null
	[IO.File]::WriteAllText((Join-Path $unsignedRoot 'mumble.exe'), 'unsigned-executable', [Text.UTF8Encoding]::new($false))
	[IO.File]::WriteAllText((Join-Path $signedRoot 'mumble.exe'), 'signed-executable-longer', [Text.UTF8Encoding]::new($false))
	Write-ReleaseJson -Path (Join-Path $unsignedRoot 'candidate-build-receipt.json') -Value ([ordered]@{ fixture = $true })
	Write-ReleaseJson -Path (Join-Path $unsignedRoot 'measured-evidence.json') -Value ([ordered]@{ passed = $true })
	Copy-Item -LiteralPath (Join-Path $unsignedRoot 'candidate-build-receipt.json') -Destination $signedRoot
	Copy-Item -LiteralPath (Join-Path $unsignedRoot 'measured-evidence.json') -Destination $signedRoot
	foreach ($name in @('candidate.msi', 'candidate.mumble-update', 'qualification.json', 'policy.json', 'release-smoke.json')) {
		[IO.File]::WriteAllText((Join-Path $signedRoot $name), "fixture-$name", [Text.UTF8Encoding]::new($false))
	}
	$getChallengeInventory = {
		param([string]$Root)
		@(Get-ChildItem -LiteralPath $Root -Recurse -File | ForEach-Object {
			[ordered]@{
				path = $_.FullName.Substring($Root.Length).TrimStart('\', '/').Replace('\', '/')
				sha256 = Get-ReleaseFileSha256 -Path $_.FullName
				size = [int64]$_.Length
			}
		} | Sort-Object -Property @{ Expression = { $_.path }; Ascending = $true })
	}
	$unsignedFiles = @(& $getChallengeInventory $unsignedRoot)
	$signedFiles = @(& $getChallengeInventory $signedRoot)
	$unsignedTreeSha = Get-TestUtf8Sha256 -Value (([ordered]@{ files = $unsignedFiles } | ConvertTo-Json -Depth 4 -Compress))
	$signedTreeSha = Get-TestUtf8Sha256 -Value (([ordered]@{ files = $signedFiles } | ConvertTo-Json -Depth 4 -Compress))
	$unsignedByPath = @{}; foreach ($record in $unsignedFiles) { $unsignedByPath[$record.path] = $record }
	$signedByPath = @{}; foreach ($record in $signedFiles) { $signedByPath[$record.path] = $record }
	$transformations = New-Object System.Collections.Generic.List[object]
	foreach ($path in @($unsignedByPath.Keys + $signedByPath.Keys | Sort-Object -Unique)) {
		$before = if ($unsignedByPath.ContainsKey($path)) { $unsignedByPath[$path] } else { $null }
		$after = if ($signedByPath.ContainsKey($path)) { $signedByPath[$path] } else { $null }
		$mode = if ($null -eq $before) { 'packaged-output' } elseif ($path -ceq 'mumble.exe') { 'authenticode-pe' } else { 'unchanged' }
		$transformations.Add([ordered]@{
			mode = $mode; path = $path
			unsignedSha256 = if ($before) { $before.sha256 } else { $null }
			unsignedSize = if ($before) { [int64]$before.size } else { $null }
			signedSha256 = if ($after) { $after.sha256 } else { $null }
			signedSize = if ($after) { [int64]$after.size } else { $null }
		})
	}
	$challengeId = ('4' * 64)
	$challengePath = Join-Path $challengeRoot 'rehearsal-challenge.json'
	$challengeDocument = [ordered]@{
		schemaVersion = 1; kind = 'input-enhancement-pre-azure-rehearsal-challenge'; phase = 'prepared'
		challengeId = $challengeId; createdAtUtc = '2026-07-18T12:00:00Z'; sourceSha = $sourceSha
		buildId = "mumble-forked-build-1-$($sourceSha.Substring(0, 12))"
		unsigned = [ordered]@{
			root = 'unsigned'; treeSha256 = $unsignedTreeSha; files = $unsignedFiles
			testedBinaryPath = 'mumble.exe'; testedBinarySha256 = $unsignedByPath['mumble.exe'].sha256
			stagedPayloadSha256 = $unsignedTreeSha
			candidateBuildReceiptPath = 'candidate-build-receipt.json'
			candidateBuildReceiptSha256 = $unsignedByPath['candidate-build-receipt.json'].sha256
			measuredEvidencePath = 'measured-evidence.json'; measuredEvidenceSha256 = $unsignedByPath['measured-evidence.json'].sha256
		}
		signed = [ordered]@{
			root = 'signed'; treeSha256 = $signedTreeSha; files = $signedFiles
			testedBinaryPath = 'mumble.exe'; testedBinarySha256 = $signedByPath['mumble.exe'].sha256
			stagedPayloadSha256 = $signedTreeSha
			installerPath = 'candidate.msi'; installerSha256 = $signedByPath['candidate.msi'].sha256
			updatePackagePath = 'candidate.mumble-update'; updatePackageSha256 = $signedByPath['candidate.mumble-update'].sha256
			qualificationPath = 'qualification.json'; qualificationSha256 = $signedByPath['qualification.json'].sha256
			policyPath = 'policy.json'; policySha256 = $signedByPath['policy.json'].sha256
			releaseSmokePath = 'release-smoke.json'; releaseSmokeSha256 = $signedByPath['release-smoke.json'].sha256
		}
		transformation = [ordered]@{ kind = 'authenticode-sign-and-package-v1'; records = $transformations.ToArray() }
		bindings = [ordered]@{
			prepareExecutorSha256 = ('1' * 64); unsignedHandoffSha256 = ('2' * 64)
			measuredEvidenceSha256 = ('3' * 64); listeningQualificationSha256 = ('5' * 64)
			releaseSmokeHarnessSha256 = ('6' * 64); fixtureManifestSha256 = ('7' * 64)
			caseSetSha256 = ('8' * 64); serverExecutableSha256 = ('9' * 64)
		}
		ephemeralSigning = [ordered]@{
			testOnly = $true; certificateSubject = 'CN=Mumble Input Enhancement Rehearsal 1-0123456789ab'
			certificateThumbprint = ('A' * 40); ed25519PublicKeyHex = ('a' * 64); privateMaterialDeleted = $true
		}
		security = [ordered]@{
			azureUsed = $false; contentsWrite = $false; draftCreated = $false
			privateMaterialIncluded = $false; productionCredentialsUsed = $false
		}
	}
	Write-ReleaseJson -Path $challengePath -Value $challengeDocument
	$challengeAssert = @{
		PreparedRoot = $challengeRoot; ChallengePath = $challengePath; ExpectedSourceSha = $sourceSha
		ExpectedBuildId = "mumble-forked-build-1-$($sourceSha.Substring(0, 12))"
		ExpectedPrepareExecutorSha256 = ('1' * 64); ExpectedUnsignedHandoffSha256 = ('2' * 64)
		ExpectedMeasuredEvidenceSha256 = ('3' * 64); ExpectedListeningQualificationSha256 = ('5' * 64)
		ExpectedReleaseSmokeHarnessSha256 = ('6' * 64); ExpectedFixtureManifestSha256 = ('7' * 64)
		ExpectedCaseSetSha256 = ('8' * 64); ExpectedServerExecutableSha256 = ('9' * 64)
		ExpectedChallengeId = $challengeId
		ExpectedCandidateBuildReceiptSha256 = $unsignedByPath['candidate-build-receipt.json'].sha256
		RequireCanonicalJson = $true
	}
	& (Join-Path $scriptsRoot 'assert-input-enhancement-rehearsal-challenge.ps1') @challengeAssert | Out-Null
	[IO.File]::AppendAllText((Join-Path $signedRoot 'mumble.exe'), '-tampered', [Text.UTF8Encoding]::new($false))
	Assert-Throws -Description 'signed candidate changed after prepare challenge' -Script {
		& (Join-Path $scriptsRoot 'assert-input-enhancement-rehearsal-challenge.ps1') @challengeAssert | Out-Null
	}
	[IO.File]::WriteAllText((Join-Path $signedRoot 'mumble.exe'), 'signed-executable-longer', [Text.UTF8Encoding]::new($false))
	Assert-Throws -Description 'candidate build receipt mismatch across phases' -Script {
		$wrongReceipt = $challengeAssert.Clone(); $wrongReceipt.ExpectedCandidateBuildReceiptSha256 = ('f' * 64)
		& (Join-Path $scriptsRoot 'assert-input-enhancement-rehearsal-challenge.ps1') @wrongReceipt | Out-Null
	}
	$replayRoot = Join-Path $tempRoot 'rehearsal-replay-ledger'
	New-Item -ItemType Directory -Path $replayRoot | Out-Null
	& (Join-Path $scriptsRoot 'assert-input-enhancement-rehearsal-replay.ps1') `
		-ReplayLedgerRoot $replayRoot -ChallengeId $challengeId | Out-Null
	Write-ReleaseJson -Path (Join-Path $replayRoot "$challengeId.finalized.json") -Value ([ordered]@{ challengeId = $challengeId })
	Assert-Throws -Description 'replayed rehearsal challenge' -Script {
		& (Join-Path $scriptsRoot 'assert-input-enhancement-rehearsal-replay.ps1') `
			-ReplayLedgerRoot $replayRoot -ChallengeId $challengeId | Out-Null
	}
	$challengeDraftRoot = Join-Path $tempRoot 'rehearsal-challenge-draft-fixture'
	New-Item -ItemType Directory -Path $challengeDraftRoot | Out-Null
	Copy-Item -LiteralPath $challengePath -Destination $challengeDraftRoot
	Copy-Item -LiteralPath $unsignedRoot, $signedRoot -Destination $challengeDraftRoot -Recurse
	Write-ReleaseJson -Path (Join-Path $challengeDraftRoot 'rehearsal.json') -Value ([ordered]@{
		artifacts = [ordered]@{
			rehearsalChallenge = [ordered]@{ fileName = 'rehearsal-challenge.json' }
		}
	})
	& (Join-Path $scriptsRoot 'new-input-enhancement-rehearsal-draft-manifest.ps1') `
		-Root $challengeDraftRoot -ArtifactName 'self-test-two-phase-draft'
	& (Join-Path $scriptsRoot 'assert-input-enhancement-rehearsal-draft-manifest.ps1') `
		-Root $challengeDraftRoot -ExpectedArtifactName 'self-test-two-phase-draft'
	foreach ($forbiddenPattern in @(
		'(?im)^\s*contents:\s*write\s*$', '(?im)^\s*id-token:\s*write\s*$',
		'(?im)^\s*environment\s*:', '(?i)\bgh\s+release\b', '(?i)api\.github\.com/.*/releases',
		'(?i)azure/login', '(?i)artifact[- ]?signing', '(?i)trusted[- ]?signing', '\$\{\{\s*secrets\.'
	)) {
		if ($rehearsalWorkflowSource -match $forbiddenPattern) {
			throw "Pre-Azure rehearsal workflow contains forbidden production capability '$forbiddenPattern'."
		}
	}
	$outputParent = Join-Path $tempRoot 'rehearsal-output-parent'
	New-Item -ItemType Directory -Path $outputParent | Out-Null
	$validOutputRoot = Join-Path $outputParent 'run-1'
	$initializedOutputRoot = Initialize-InputEnhancementRehearsalOutputRoot `
		-OutputRoot $validOutputRoot -AllowedOutputParent $outputParent -SourceRoot $sourceRoot
	if (-not $initializedOutputRoot.Equals([IO.Path]::GetFullPath($validOutputRoot),
		[StringComparison]::OrdinalIgnoreCase) -or
		-not (Test-Path -LiteralPath $initializedOutputRoot -PathType Container)) {
		throw 'Safe rehearsal output-root initialization did not create the expected directory.'
	}
	Assert-Throws -Description 'pre-existing rehearsal output root' -Script {
		Initialize-InputEnhancementRehearsalOutputRoot `
			-OutputRoot $validOutputRoot -AllowedOutputParent $outputParent -SourceRoot $sourceRoot
	}
	Assert-Throws -Description 'rehearsal output outside its approved parent' -Script {
		Initialize-InputEnhancementRehearsalOutputRoot `
			-OutputRoot (Join-Path $tempRoot 'outside-approved-parent') `
			-AllowedOutputParent $outputParent -SourceRoot $sourceRoot
	}
	Assert-Throws -Description 'rehearsal output overlapping source checkout' -Script {
		Initialize-InputEnhancementRehearsalOutputRoot `
			-OutputRoot (Join-Path $sourceRoot 'must-not-create') `
			-AllowedOutputParent $sourceRoot -SourceRoot $sourceRoot
	}
	Assert-Throws -Description 'filesystem root as rehearsal output' -Script {
		Initialize-InputEnhancementRehearsalOutputRoot `
			-OutputRoot ([IO.Path]::GetPathRoot($tempRoot)) `
			-AllowedOutputParent $outputParent -SourceRoot $sourceRoot
	}

	$draftManifestRoot = Join-Path $tempRoot 'rehearsal-draft-manifest-fixture'
	New-Item -ItemType Directory -Force -Path $draftManifestRoot | Out-Null
	Write-ReleaseJson -Path (Join-Path $draftManifestRoot 'rehearsal.json') -Value ([ordered]@{
		artifacts = [ordered]@{
			installer = [ordered]@{ fileName = 'candidate.msi' }
		}
	})
	$candidatePath = Join-Path $draftManifestRoot 'candidate.msi'
	$candidateBytes = [byte[]](1, 2, 3)
	[IO.File]::WriteAllBytes($candidatePath, $candidateBytes)
	& (Join-Path $scriptsRoot 'new-input-enhancement-rehearsal-draft-manifest.ps1') `
		-Root $draftManifestRoot -ArtifactName 'self-test-draft'
	& (Join-Path $scriptsRoot 'assert-input-enhancement-rehearsal-draft-manifest.ps1') `
		-Root $draftManifestRoot -ExpectedArtifactName 'self-test-draft'
	$unexpectedPath = Join-Path $draftManifestRoot 'unmanifested-extra.bin'
	[IO.File]::WriteAllBytes($unexpectedPath, [byte[]](4, 5, 6))
	Assert-Throws -Description 'unallowlisted rehearsal draft file during manifest creation' -Script {
		& (Join-Path $scriptsRoot 'new-input-enhancement-rehearsal-draft-manifest.ps1') `
			-Root $draftManifestRoot -ArtifactName 'self-test-draft'
	}
	Assert-Throws -Description 'unallowlisted rehearsal draft file after remote download' -Script {
		& (Join-Path $scriptsRoot 'assert-input-enhancement-rehearsal-draft-manifest.ps1') `
			-Root $draftManifestRoot -ExpectedArtifactName 'self-test-draft'
	}
	Remove-Item -LiteralPath $unexpectedPath -Force
	$unexpectedDirectory = Join-Path $draftManifestRoot 'unmanifested-directory'
	New-Item -ItemType Directory -Path $unexpectedDirectory | Out-Null
	Assert-Throws -Description 'unallowlisted rehearsal draft directory' -Script {
		& (Join-Path $scriptsRoot 'new-input-enhancement-rehearsal-draft-manifest.ps1') `
			-Root $draftManifestRoot -ArtifactName 'self-test-draft'
	}
	Remove-Item -LiteralPath $unexpectedDirectory -Force
	[IO.File]::WriteAllText($candidatePath,
		'-----BEGIN PRIVATE KEY-----`nprivate-test-material`n-----END PRIVATE KEY-----',
		[Text.UTF8Encoding]::new($false))
	Assert-Throws -Description 'PEM private key hidden under an allowed artifact name' -Script {
		& (Join-Path $scriptsRoot 'new-input-enhancement-rehearsal-draft-manifest.ps1') `
			-Root $draftManifestRoot -ArtifactName 'self-test-draft'
	}
	[IO.File]::WriteAllBytes($candidatePath, $candidateBytes)

	$latePemDraftRoot = Join-Path $tempRoot 'rehearsal-late-pem-fixture'
	New-Item -ItemType Directory -Path $latePemDraftRoot | Out-Null
	Write-ReleaseJson -Path (Join-Path $latePemDraftRoot 'rehearsal.json') -Value ([ordered]@{
		artifacts = [ordered]@{
			diagnostics = [ordered]@{ fileName = 'late-private-material.json' }
		}
	})
	$latePemPath = Join-Path $latePemDraftRoot 'late-private-material.json'
	$latePemPadding = 'x' * (72 * 1024)
	[IO.File]::WriteAllText($latePemPath,
		('{"padding":"' + $latePemPadding + '","material":"-----BEGIN PRIVATE KEY-----"}'),
		[Text.UTF8Encoding]::new($false))
	Assert-Throws -Description 'PEM private key after the first 70 KiB of an allowed text artifact' -Script {
		& (Join-Path $scriptsRoot 'new-input-enhancement-rehearsal-draft-manifest.ps1') `
			-Root $latePemDraftRoot -ArtifactName 'self-test-late-pem-draft'
	}

	$privateDraftRoot = Join-Path $tempRoot 'rehearsal-private-material-fixture'
	New-Item -ItemType Directory -Path $privateDraftRoot | Out-Null
	Write-ReleaseJson -Path (Join-Path $privateDraftRoot 'rehearsal.json') -Value ([ordered]@{
		artifacts = [ordered]@{
			installer = [ordered]@{ fileName = 'ephemeral-test-signing.p12' }
		}
	})
	[IO.File]::WriteAllBytes((Join-Path $privateDraftRoot 'ephemeral-test-signing.p12'), [byte[]](7, 8, 9))
	Assert-Throws -Description 'allowlisted private-key container in rehearsal draft' -Script {
		& (Join-Path $scriptsRoot 'new-input-enhancement-rehearsal-draft-manifest.ps1') `
			-Root $privateDraftRoot -ArtifactName 'self-test-private-draft'
	}

	$listeningDraftRoot = Join-Path $tempRoot 'rehearsal-listening-evidence-fixture'
	$listeningEvidenceName = 'listening-fixture.evidence'
	$listeningEvidenceRoot = Join-Path $listeningDraftRoot $listeningEvidenceName
	$listeningPrivateRoot = Join-Path $listeningEvidenceRoot 'private'
	$listeningSessionRoot = Join-Path $listeningEvidenceRoot 'sessions'
	New-Item -ItemType Directory -Path $listeningPrivateRoot, $listeningSessionRoot | Out-Null
	$listeningSourcePath = Join-Path $listeningEvidenceRoot 'source-manifest.json'
	$listeningAnswerPath = Join-Path $listeningPrivateRoot 'answer-key.json'
	$listeningSessionPath = Join-Path $listeningSessionRoot '000000-session.json'
	[IO.File]::WriteAllText($listeningSourcePath, '{}', [Text.UTF8Encoding]::new($false))
	[IO.File]::WriteAllText($listeningAnswerPath, '{}', [Text.UTF8Encoding]::new($false))
	[IO.File]::WriteAllText($listeningSessionPath, '{}', [Text.UTF8Encoding]::new($false))
	$listeningQualificationPath = Join-Path $listeningDraftRoot 'listening-qualification.json'
	Write-ReleaseJson -Path $listeningQualificationPath -Value ([ordered]@{
		session_manifest = [ordered]@{
			evidence_root = $listeningEvidenceName
			source_manifest = [ordered]@{
				relative_path = "$listeningEvidenceName/source-manifest.json"
				sha256 = Get-ReleaseFileSha256 -Path $listeningSourcePath
			}
			answer_key = [ordered]@{
				relative_path = "$listeningEvidenceName/private/answer-key.json"
				sha256 = Get-ReleaseFileSha256 -Path $listeningAnswerPath
			}
			sessions = @([ordered]@{
				relative_path = "$listeningEvidenceName/sessions/000000-session.json"
				sha256 = Get-ReleaseFileSha256 -Path $listeningSessionPath
			})
		}
	})
	$listeningCandidatePath = Join-Path $listeningDraftRoot 'candidate.msi'
	[IO.File]::WriteAllBytes($listeningCandidatePath, $candidateBytes)
	Write-ReleaseJson -Path (Join-Path $listeningDraftRoot 'rehearsal.json') -Value ([ordered]@{
		artifacts = [ordered]@{
			installer = [ordered]@{ fileName = 'candidate.msi' }
			listeningQualification = [ordered]@{ fileName = 'listening-qualification.json' }
		}
	})
	& (Join-Path $scriptsRoot 'new-input-enhancement-rehearsal-draft-manifest.ps1') `
		-Root $listeningDraftRoot -ArtifactName 'self-test-listening-draft'
	& (Join-Path $scriptsRoot 'assert-input-enhancement-rehearsal-draft-manifest.ps1') `
		-Root $listeningDraftRoot -ExpectedArtifactName 'self-test-listening-draft'

	[IO.File]::AppendAllText($candidatePath, 'tamper')
	Assert-Throws -Description 'remote rehearsal draft byte tampering' -Script {
		& (Join-Path $scriptsRoot 'assert-input-enhancement-rehearsal-draft-manifest.ps1') `
			-Root $draftManifestRoot -ExpectedArtifactName 'self-test-draft'
	}

	$stageRoot = Join-Path $tempRoot "stage"
	$modelDir = Join-Path $stageRoot "rnnoise"
	$qualityModelDir = Join-Path $stageRoot "deepfilternet"
	New-Item -ItemType Directory -Force -Path $modelDir, $qualityModelDir | Out-Null
	$modelAsset = Join-Path $modelDir "test-model.bin"
	$qualityModelAsset = Join-Path $qualityModelDir "test-model.bin"
	[System.IO.File]::WriteAllBytes($modelAsset, [byte[]](1, 2, 3, 4, 5))
	[System.IO.File]::WriteAllBytes($qualityModelAsset, [byte[]](6, 7, 8, 9, 10))
	$modelDescriptorPath = Join-Path $tempRoot "model-descriptor.json"
	$recipeDescriptorPath = Join-Path $tempRoot "recipe-descriptor.json"
	Write-ReleaseJson -Path $modelDescriptorPath -Value ([ordered]@{
		schemaVersion = 1
		catalogRevision = "self-test-r1"
		models = @(
			[ordered]@{
				id = "rnnoise:embedded"
				version = "1"
				backend = "rnnoise"
				path = "rnnoise/test-model.bin"
				licenseSpdx = "BSD-3-Clause"
				sampleRateHz = 48000
				algorithmicLatencyMs = 30
				recipeCompatibility = @("input.balanced.rnnoise-embedded")
			},
			[ordered]@{
				id = "deepfilternet:low-latency"
				version = "1"
				backend = "deepfilternet"
				path = "deepfilternet/test-model.bin"
				licenseSpdx = "MIT OR Apache-2.0"
				sampleRateHz = 48000
				algorithmicLatencyMs = 20
				recipeCompatibility = @("input.quality.deepfilternet-low-latency", "input.voice-focus.deepfilternet-low-latency")
			}
		)
	})
	$recipes = @(
		[ordered]@{ id = "input.original"; revision = 1; profile = "Original"; modelIds = @() },
		[ordered]@{ id = "input.light.speex"; revision = 1; profile = "Light"; modelIds = @() },
		[ordered]@{ id = "input.balanced.rnnoise-embedded"; revision = 1; profile = "Balanced"; modelIds = @("rnnoise:embedded") },
		[ordered]@{ id = "input.quality.deepfilternet-low-latency"; revision = 1; profile = "Quality"; modelIds = @("deepfilternet:low-latency") },
		[ordered]@{ id = "input.voice-focus.deepfilternet-low-latency"; revision = 1; profile = "VoiceFocus"; modelIds = @("deepfilternet:low-latency") },
		[ordered]@{ id = "input.auto.light.speex"; revision = 1; profile = "Auto"; modelIds = @() }
	)
	Write-ReleaseJson -Path $recipeDescriptorPath -Value ([ordered]@{
		schemaVersion = 2
		catalogRevision = "self-test-r1"
		recipes = $recipes
	})

	& (Join-Path $scriptsRoot "new-input-enhancement-package-manifests.ps1") `
		-StageRoot $stageRoot `
		-ModelDescriptorPath $modelDescriptorPath `
		-RecipeDescriptorPath $recipeDescriptorPath
	& (Join-Path $scriptsRoot "assert-input-enhancement-package-manifests.ps1") -StageRoot $stageRoot
	$modelManifestPath = Join-Path $stageRoot "input-models.json"
	$recipeManifestPath = Join-Path $stageRoot "input-recipes.json"
	$unsignedModelManifestPath = Join-Path $tempRoot "unsigned-input-models.json"
	$unsignedRecipeManifestPath = Join-Path $tempRoot "unsigned-input-recipes.json"
	Copy-Item -LiteralPath $modelManifestPath -Destination $unsignedModelManifestPath
	Copy-Item -LiteralPath $recipeManifestPath -Destination $unsignedRecipeManifestPath
	$modelManifestSignaturePath = "$modelManifestPath.sig"
	$recipeManifestSignaturePath = "$recipeManifestPath.sig"
	& (Join-Path $scriptsRoot 'protect-input-enhancement-json.ps1') `
		-InputPath $modelManifestPath -SignaturePath $modelManifestSignaturePath `
		-PrivateKeyBase64 $privateKeyBase64 -ExpectedPublicKeyHex $publicKeyHex -OpenSslPath $openSsl
	& (Join-Path $scriptsRoot 'protect-input-enhancement-json.ps1') `
		-InputPath $recipeManifestPath -SignaturePath $recipeManifestSignaturePath `
		-PrivateKeyBase64 $privateKeyBase64 -ExpectedPublicKeyHex $publicKeyHex -OpenSslPath $openSsl
	& (Join-Path $scriptsRoot 'assert-input-enhancement-package-signatures.ps1') `
		-StageRoot $stageRoot -PublicKeyHex $publicKeyHex -OpenSslPath $openSsl
	$tamperedSignaturePath = Join-Path $tempRoot 'tampered-model-signature.raw'
	$tamperedSignature = [IO.File]::ReadAllBytes($modelManifestSignaturePath)
	$tamperedSignature[0] = $tamperedSignature[0] -bxor 1
	[IO.File]::WriteAllBytes($tamperedSignaturePath, $tamperedSignature)
	Assert-Throws -Description 'detached signature byte tampering' -Script {
		& (Join-Path $scriptsRoot 'assert-input-enhancement-detached-signature.ps1') `
			-InputPath $modelManifestPath -SignaturePath $tamperedSignaturePath `
			-PublicKeyHex $publicKeyHex -OpenSslPath $openSsl
	}
	$modelManifest = Read-ReleaseJson -Path $modelManifestPath
	if ([string]$modelManifest.models[0].sha256 -cne (Get-ReleaseFileSha256 -Path $modelAsset)) {
		throw "Generated model manifest did not hash the actual staged asset."
	}
	[System.IO.File]::WriteAllBytes($modelAsset, [byte[]](5, 4, 3, 2, 1))
	Assert-Throws -Description "model asset hash mismatch" -Script {
		& (Join-Path $scriptsRoot "assert-input-enhancement-package-manifests.ps1") -StageRoot $stageRoot
	}
	[System.IO.File]::WriteAllBytes($modelAsset, [byte[]](1, 2, 3, 4, 5))

	$artifactPath = Join-Path $tempRoot "qualified-fixture.zip"
	$installerPath = Join-Path $tempRoot "mumble-forked-1.7.42.msi"
	[System.IO.File]::WriteAllBytes($installerPath, [byte[]](1, 3, 3, 7))
	$artifactFixtureRoot = Join-Path $tempRoot 'qualified-fixture-root'
	New-Item -ItemType Directory -Force -Path (Join-Path $artifactFixtureRoot 'installer') | Out-Null
	Copy-Item -LiteralPath $installerPath -Destination (Join-Path $artifactFixtureRoot 'installer')
	Compress-Archive -Path (Join-Path $artifactFixtureRoot '*') -DestinationPath $artifactPath
	$buildNumber = 42
	$buildId = Get-InputEnhancementBuildId -BuildNumber $buildNumber -SourceSha $sourceSha
	$rolloutRoot = Join-Path $tempRoot 'rollout'
	New-Item -ItemType Directory -Force -Path $rolloutRoot | Out-Null
	$rolloutPath = Join-Path $rolloutRoot 'input-enhancement-rollout.json'
	$rolloutSignaturePath = "$rolloutPath.sig"
	$aggregatePath = Join-Path $rolloutRoot 'input-enhancement-aggregate-export.json'
	$aggregateSignaturePath = "$aggregatePath.sig"
	$aggregateSchema = Read-ReleaseJson -Path (Join-Path $scriptsRoot 'input-enhancement-aggregate-export.schema.json')
	$rolloutSchema = Read-ReleaseJson -Path (Join-Path $scriptsRoot 'input-enhancement-rollout-qualification.schema.json')
	$rnnoiseDecisionSchema = Read-ReleaseJson `
		-Path (Join-Path $scriptsRoot 'input-enhancement-rnnoise-selection-decision.schema.json')
	if ([string]$aggregateSchema.'$id' -cne
		'https://mumble.info/schemas/input-enhancement-telemetry-aggregate-export-v2.json' -or
		[int]$aggregateSchema.properties.schemaVersion.const -ne 2 -or
		[string]$rolloutSchema.'$id' -cne
		'https://mumble.info/schemas/input-enhancement-rollout-qualification-v2.json' -or
		[int]$rolloutSchema.properties.schemaVersion.const -ne 2 -or
		[string]$rnnoiseDecisionSchema.'$id' -cne
		'https://mumble.info/schemas/input-enhancement-rnnoise-selection-decision-v1.json') {
		throw 'Rollout/aggregate JSON schemas do not expose the expected fail-closed versions.'
	}
	$rolloutEnd = [DateTimeOffset]::UtcNow.AddMinutes(-1)
	$rolloutQuerySha256 = 'a' * 64
	$rolloutSnapshotSha256 = 'b' * 64
	$aggregateVerification = @{
		AggregateExportPath = $aggregatePath
		AggregateExportSignaturePath = $aggregateSignaturePath
		AggregatePublicKeyHex = $aggregatePublicKeyHex
		ExpectedQuerySha256 = $rolloutQuerySha256
		OpenSslPath = $openSsl
	}
	$rolloutVerification = @{
		EvidencePath = $rolloutPath
		SignaturePath = $rolloutSignaturePath
		PublicKeyHex = $publicKeyHex
		AggregateExportPath = $aggregatePath
		AggregateExportSignaturePath = $aggregateSignaturePath
		AggregatePublicKeyHex = $aggregatePublicKeyHex
		ExpectedQuerySha256 = $rolloutQuerySha256
		ExpectedBuildId = $buildId
		ExpectedRecipeSetVersion = 'self-test-r1'
		OpenSslPath = $openSsl
	}
	$removedManualParameters = @(
		'SourceChannel', 'TestedBuildIds', 'RecipeSetVersion', 'WindowStartUtc', 'WindowEndUtc',
		'ObservationDays', 'DistinctUsers', 'DistinctDevices', 'TalkHours', 'CrashFreeSessionRate',
		'DomainRnnoiseStatus', 'DomainRnnoiseOutcome'
	)
	$rolloutGeneratorParameters = (Get-Command (Join-Path $scriptsRoot 'new-input-enhancement-rollout-qualification.ps1')).Parameters
	if (@($removedManualParameters | Where-Object { $rolloutGeneratorParameters.ContainsKey($_) }).Count -ne 0) {
		throw 'Rollout generator still exposes manually supplied aggregate totals or filters.'
	}

	Write-SignedRolloutAggregateFixture `
		-Path $aggregatePath -SignaturePath $aggregateSignaturePath `
		-PrivateKeyBase64 $aggregatePrivateKeyBase64 -PublicKeyHex $aggregatePublicKeyHex -OpenSslPath $openSsl `
		-QuerySha256 $rolloutQuerySha256 -SourceSnapshotSha256 $rolloutSnapshotSha256 `
		-SourceChannel preview -RolloutAudience private-community `
		-TestedBuildIds @($buildId) -RecipeSetVersion 'self-test-r1' `
		-WindowStartUtc $rolloutEnd.AddDays(-8) -WindowEndUtc $rolloutEnd -ObservationDays 7 `
		-DistinctUsers 2 -DistinctDevices 10 -IntendedCommunityDevices 10 -TalkHours 20
	& (Join-Path $scriptsRoot 'assert-input-enhancement-aggregate-export.ps1') @aggregateVerification
	& (Join-Path $scriptsRoot 'new-input-enhancement-rollout-qualification.ps1') `
		-AggregateExportPath $aggregatePath -AggregateExportSignaturePath $aggregateSignaturePath `
		-AggregatePublicKeyHex $aggregatePublicKeyHex -ExpectedQuerySha256 $rolloutQuerySha256 `
		-PrivateKeyBase64 $privateKeyBase64 -ExpectedPublicKeyHex $publicKeyHex `
		-OutputPath $rolloutPath -SignaturePath $rolloutSignaturePath -OpenSslPath $openSsl
	& (Join-Path $scriptsRoot 'assert-input-enhancement-rollout-qualification.ps1') `
		@rolloutVerification -TargetStage community-stable

	Write-SignedRolloutAggregateFixture `
		-Path $aggregatePath -SignaturePath $aggregateSignaturePath `
		-PrivateKeyBase64 $aggregatePrivateKeyBase64 -PublicKeyHex $aggregatePublicKeyHex -OpenSslPath $openSsl `
		-QuerySha256 $rolloutQuerySha256 -SourceSnapshotSha256 $rolloutSnapshotSha256 `
		-SourceChannel preview -RolloutAudience private-community `
		-TestedBuildIds @($buildId) -RecipeSetVersion 'self-test-r1' `
		-WindowStartUtc $rolloutEnd.AddDays(-8) -WindowEndUtc $rolloutEnd -ObservationDays 7 `
		-DistinctUsers 2 -DistinctDevices 9 -IntendedCommunityDevices 10 -TalkHours 20
	& (Join-Path $scriptsRoot 'new-input-enhancement-rollout-qualification.ps1') `
		-AggregateExportPath $aggregatePath -AggregateExportSignaturePath $aggregateSignaturePath `
		-AggregatePublicKeyHex $aggregatePublicKeyHex -ExpectedQuerySha256 $rolloutQuerySha256 `
		-PrivateKeyBase64 $privateKeyBase64 -ExpectedPublicKeyHex $publicKeyHex `
		-OutputPath $rolloutPath -SignaturePath $rolloutSignaturePath -OpenSslPath $openSsl
	Assert-Throws -Description 'private community rollout without every intended device' -Script {
		& (Join-Path $scriptsRoot 'assert-input-enhancement-rollout-qualification.ps1') `
			@rolloutVerification -TargetStage community-stable
	}

	Write-SignedRolloutAggregateFixture `
		-Path $aggregatePath -SignaturePath $aggregateSignaturePath `
		-PrivateKeyBase64 $aggregatePrivateKeyBase64 -PublicKeyHex $aggregatePublicKeyHex -OpenSslPath $openSsl `
		-QuerySha256 $rolloutQuerySha256 -SourceSnapshotSha256 $rolloutSnapshotSha256 `
		-SourceChannel preview -RolloutAudience public `
		-TestedBuildIds @($buildId) -RecipeSetVersion 'self-test-r1' `
		-WindowStartUtc $rolloutEnd.AddDays(-8) -WindowEndUtc $rolloutEnd -ObservationDays 7 `
		-DistinctUsers 10 -DistinctDevices 10 -IntendedCommunityDevices 10 -TalkHours 50
	& (Join-Path $scriptsRoot 'new-input-enhancement-rollout-qualification.ps1') `
		-AggregateExportPath $aggregatePath -AggregateExportSignaturePath $aggregateSignaturePath `
		-AggregatePublicKeyHex $aggregatePublicKeyHex -ExpectedQuerySha256 $rolloutQuerySha256 `
		-PrivateKeyBase64 $privateKeyBase64 -ExpectedPublicKeyHex $publicKeyHex `
		-OutputPath $rolloutPath -SignaturePath $rolloutSignaturePath -OpenSslPath $openSsl
	& (Join-Path $scriptsRoot 'assert-input-enhancement-rollout-qualification.ps1') `
		@rolloutVerification -TargetStage stable-opt-in

	$validPublicAggregateBytes = [IO.File]::ReadAllBytes($aggregatePath)
	$validPublicAggregateSignatureBytes = [IO.File]::ReadAllBytes($aggregateSignaturePath)
	$validPublicAggregateText = [Text.UTF8Encoding]::new($false, $true).GetString($validPublicAggregateBytes)
	$rawSchemaMutations = @(
		[pscustomobject]@{
			description = 'boolean aggregate schemaVersion'
			pattern = '"schemaVersion"\s*:\s*2'
			replacement = '"schemaVersion": true'
		},
		[pscustomobject]@{
			description = 'scalar testedBuildIds instead of an array'
			pattern = '(?s)("testedBuildIds"\s*:\s*)\[\s*"[^"\r\n]+"\s*\]'
			replacement = "`$1`"$buildId`""
		},
		[pscustomobject]@{
			description = 'numeric string aggregate population'
			pattern = '"distinctUsers"\s*:\s*10'
			replacement = '"distinctUsers": "10"'
		},
		[pscustomobject]@{
			description = 'string aggregate privacy boolean'
			pattern = '"optInOnly"\s*:\s*true'
			replacement = '"optInOnly": "true"'
		},
		[pscustomobject]@{
			description = 'legacy aggregate schema v1'
			pattern = '"schemaVersion"\s*:\s*2'
			replacement = '"schemaVersion": 1'
		},
		[pscustomobject]@{
			description = 'aggregate pooled across multiple builds'
			pattern = "`"$buildId`""
			replacement = "`"$buildId`",`n    `"mumble-forked-build-43-bbbbbbbbbbbb`""
		}
	)
	foreach ($mutation in $rawSchemaMutations) {
		$mutatedText = [regex]::Replace($validPublicAggregateText, [string]$mutation.pattern,
			[string]$mutation.replacement, 1)
		if ($mutatedText -ceq $validPublicAggregateText) {
			throw "Self-test could not apply raw-schema mutation '$($mutation.description)'."
		}
		[IO.File]::WriteAllText($aggregatePath, $mutatedText, [Text.UTF8Encoding]::new($false))
		Protect-FileWithEd25519 -InputPath $aggregatePath -SignaturePath $aggregateSignaturePath `
			-PrivateKeyBase64 $aggregatePrivateKeyBase64 -ExpectedPublicKeyHex $aggregatePublicKeyHex `
			-OpenSslPath $openSsl
		Assert-Throws -Description ([string]$mutation.description) -Script {
			& (Join-Path $scriptsRoot 'assert-input-enhancement-aggregate-export.ps1') @aggregateVerification
		}
	}
	[IO.File]::WriteAllBytes($aggregatePath, $validPublicAggregateBytes)
	[IO.File]::WriteAllBytes($aggregateSignaturePath, $validPublicAggregateSignatureBytes)

	$staleWindowEnd = [DateTimeOffset]::UtcNow.AddDays(-30)
	Write-SignedRolloutAggregateFixture `
		-Path $aggregatePath -SignaturePath $aggregateSignaturePath `
		-PrivateKeyBase64 $aggregatePrivateKeyBase64 -PublicKeyHex $aggregatePublicKeyHex -OpenSslPath $openSsl `
		-QuerySha256 $rolloutQuerySha256 -SourceSnapshotSha256 $rolloutSnapshotSha256 `
		-SourceChannel preview -RolloutAudience public -TestedBuildIds @($buildId) `
		-RecipeSetVersion 'self-test-r1' -WindowStartUtc $staleWindowEnd.AddDays(-8) `
		-WindowEndUtc $staleWindowEnd -ObservationDays 7 -DistinctUsers 10 -DistinctDevices 10 `
		-IntendedCommunityDevices 10 -TalkHours 50
	Assert-Throws -Description 'freshly signed aggregate with a stale observation window' -Script {
		& (Join-Path $scriptsRoot 'assert-input-enhancement-aggregate-export.ps1') @aggregateVerification
	}
	$lateIngestEnd = [DateTimeOffset]::UtcNow.AddHours(-25)
	Write-SignedRolloutAggregateFixture `
		-Path $aggregatePath -SignaturePath $aggregateSignaturePath `
		-PrivateKeyBase64 $aggregatePrivateKeyBase64 -PublicKeyHex $aggregatePublicKeyHex -OpenSslPath $openSsl `
		-QuerySha256 $rolloutQuerySha256 -SourceSnapshotSha256 $rolloutSnapshotSha256 `
		-SourceChannel preview -RolloutAudience public -TestedBuildIds @($buildId) `
		-RecipeSetVersion 'self-test-r1' -WindowStartUtc $lateIngestEnd.AddDays(-8) `
		-WindowEndUtc $lateIngestEnd -ObservationDays 7 -DistinctUsers 10 -DistinctDevices 10 `
		-IntendedCommunityDevices 10 -TalkHours 50
	Assert-Throws -Description 'aggregate whose exporter ingest gap exceeds 24 hours' -Script {
		& (Join-Path $scriptsRoot 'assert-input-enhancement-aggregate-export.ps1') @aggregateVerification
	}
	[IO.File]::WriteAllBytes($aggregatePath, $validPublicAggregateBytes)
	[IO.File]::WriteAllBytes($aggregateSignaturePath, $validPublicAggregateSignatureBytes)

	Assert-Throws -Description 'aggregate signature verified with the rollout key' -Script {
		$wrongAggregateKeyVerification = $rolloutVerification.Clone()
		$wrongAggregateKeyVerification.AggregatePublicKeyHex = $publicKeyHex
		& (Join-Path $scriptsRoot 'assert-input-enhancement-rollout-qualification.ps1') `
			@wrongAggregateKeyVerification -TargetStage stable-opt-in
	}
	$originalAggregateBytes = [IO.File]::ReadAllBytes($aggregatePath)
	Add-Content -LiteralPath $aggregatePath -Value ' ' -NoNewline
	Assert-Throws -Description 'tampered telemetry aggregate bytes' -Script {
		& (Join-Path $scriptsRoot 'assert-input-enhancement-rollout-qualification.ps1') `
			@rolloutVerification -TargetStage stable-opt-in
	}
	[IO.File]::WriteAllBytes($aggregatePath, $originalAggregateBytes)

	Write-SignedRolloutAggregateFixture `
		-Path $aggregatePath -SignaturePath $aggregateSignaturePath `
		-PrivateKeyBase64 $aggregatePrivateKeyBase64 -PublicKeyHex $aggregatePublicKeyHex -OpenSslPath $openSsl `
		-QuerySha256 $rolloutQuerySha256 -SourceSnapshotSha256 $rolloutSnapshotSha256 `
		-SourceChannel preview -TestedBuildIds @($buildId) -RecipeSetVersion 'self-test-r1' `
		-WindowStartUtc $rolloutEnd.AddDays(-8) -WindowEndUtc $rolloutEnd -ObservationDays 7 `
		-DistinctUsers 10 -DistinctDevices 10 -TalkHours 51
	Assert-Throws -Description 'stale rollout reference after a validly re-signed aggregate change' -Script {
		& (Join-Path $scriptsRoot 'assert-input-enhancement-rollout-qualification.ps1') `
			@rolloutVerification -TargetStage stable-opt-in
	}

	Write-SignedRolloutAggregateFixture `
		-Path $aggregatePath -SignaturePath $aggregateSignaturePath `
		-PrivateKeyBase64 $aggregatePrivateKeyBase64 -PublicKeyHex $aggregatePublicKeyHex -OpenSslPath $openSsl `
		-QuerySha256 $rolloutQuerySha256 -SourceSnapshotSha256 $rolloutSnapshotSha256 `
		-SourceChannel preview -TestedBuildIds @($buildId) -RecipeSetVersion 'self-test-r1' `
		-WindowStartUtc $rolloutEnd.AddDays(-8) -WindowEndUtc $rolloutEnd -ObservationDays 7 `
		-DistinctUsers 10 -DistinctDevices 10 -TalkHours 50 -WindowSha256Override ('f' * 64)
	Assert-Throws -Description 'validly signed aggregate with a fabricated window hash' -Script {
		& (Join-Path $scriptsRoot 'new-input-enhancement-rollout-qualification.ps1') `
			-AggregateExportPath $aggregatePath -AggregateExportSignaturePath $aggregateSignaturePath `
			-AggregatePublicKeyHex $aggregatePublicKeyHex -ExpectedQuerySha256 $rolloutQuerySha256 `
			-PrivateKeyBase64 $privateKeyBase64 -ExpectedPublicKeyHex $publicKeyHex `
			-OutputPath $rolloutPath -SignaturePath $rolloutSignaturePath -OpenSslPath $openSsl
	}
	Write-SignedRolloutAggregateFixture `
		-Path $aggregatePath -SignaturePath $aggregateSignaturePath `
		-PrivateKeyBase64 $aggregatePrivateKeyBase64 -PublicKeyHex $aggregatePublicKeyHex -OpenSslPath $openSsl `
		-QuerySha256 ('c' * 64) -SourceSnapshotSha256 $rolloutSnapshotSha256 `
		-SourceChannel preview -TestedBuildIds @($buildId) -RecipeSetVersion 'self-test-r1' `
		-WindowStartUtc $rolloutEnd.AddDays(-8) -WindowEndUtc $rolloutEnd -ObservationDays 7 `
		-DistinctUsers 10 -DistinctDevices 10 -TalkHours 50
	Assert-Throws -Description 'validly signed aggregate from an unpinned query' -Script {
		& (Join-Path $scriptsRoot 'new-input-enhancement-rollout-qualification.ps1') `
			-AggregateExportPath $aggregatePath -AggregateExportSignaturePath $aggregateSignaturePath `
			-AggregatePublicKeyHex $aggregatePublicKeyHex -ExpectedQuerySha256 $rolloutQuerySha256 `
			-PrivateKeyBase64 $privateKeyBase64 -ExpectedPublicKeyHex $publicKeyHex `
			-OutputPath $rolloutPath -SignaturePath $rolloutSignaturePath -OpenSslPath $openSsl
	}

	Write-SignedRolloutAggregateFixture `
		-Path $aggregatePath -SignaturePath $aggregateSignaturePath `
		-PrivateKeyBase64 $aggregatePrivateKeyBase64 -PublicKeyHex $aggregatePublicKeyHex -OpenSslPath $openSsl `
		-QuerySha256 $rolloutQuerySha256 -SourceSnapshotSha256 $rolloutSnapshotSha256 `
		-SourceChannel stable -RolloutAudience public -TestedBuildIds @($buildId) -RecipeSetVersion 'self-test-r1' `
		-WindowStartUtc $rolloutEnd.AddDays(-31) -WindowEndUtc $rolloutEnd -ObservationDays 30 `
		-DistinctUsers 50 -DistinctDevices 50 -IntendedCommunityDevices 50 -TalkHours 500 -CrashFreeSessionRate 0.999 `
		-FallbackSessionRate 0.0009 -CallbackOverrunFrameRate 0.00009 `
		-ManualRollbackOrOptOutRate 0.099 -BlindAbResponses 25 -SelectedOverOriginalRate 0.60
	$rnnoiseDecisionPath = Join-Path $rolloutRoot 'rnnoise-selection-decision.json'
	$rnnoiseDecisionSignaturePath = "$rnnoiseDecisionPath.sig"
	Write-SignedRnnoiseDecisionFixture `
		-Path $rnnoiseDecisionPath -SignaturePath $rnnoiseDecisionSignaturePath `
		-PrivateKeyBase64 $privateKeyBase64 -PublicKeyHex $publicKeyHex -OpenSslPath $openSsl `
		-Status embedded-retained
	& (Join-Path $scriptsRoot 'assert-input-enhancement-rnnoise-selection-decision.ps1') `
		-DecisionPath $rnnoiseDecisionPath -DecisionSignaturePath $rnnoiseDecisionSignaturePath `
		-PublicKeyHex $publicKeyHex -OpenSslPath $openSsl | Out-Null
	& (Join-Path $scriptsRoot 'new-input-enhancement-rollout-qualification.ps1') `
		-AggregateExportPath $aggregatePath -AggregateExportSignaturePath $aggregateSignaturePath `
		-AggregatePublicKeyHex $aggregatePublicKeyHex -ExpectedQuerySha256 $rolloutQuerySha256 `
		-RnnoiseDecisionPath $rnnoiseDecisionPath -RnnoiseDecisionSignaturePath $rnnoiseDecisionSignaturePath `
		-PrivateKeyBase64 $privateKeyBase64 -ExpectedPublicKeyHex $publicKeyHex `
		-OutputPath $rolloutPath -SignaturePath $rolloutSignaturePath -OpenSslPath $openSsl
	$autoRolloutVerification = $rolloutVerification.Clone()
	$autoRolloutVerification.RnnoiseDecisionPath = $rnnoiseDecisionPath
	$autoRolloutVerification.RnnoiseDecisionSignaturePath = $rnnoiseDecisionSignaturePath
	Assert-Throws -Description 'completed RNNoise rollout without its signed decision files' -Script {
		& (Join-Path $scriptsRoot 'assert-input-enhancement-rollout-qualification.ps1') `
			@rolloutVerification -TargetStage auto-recommended
	}
	& (Join-Path $scriptsRoot 'assert-input-enhancement-rollout-qualification.ps1') `
		@autoRolloutVerification -TargetStage auto-recommended
	& (Join-Path $scriptsRoot 'assert-input-enhancement-rollout-qualification.ps1') `
		@autoRolloutVerification -TargetStage auto-default
	$embeddedRollout = Read-ReleaseJson -Path $rolloutPath
	if ([string]$embeddedRollout.domainRnnoiseTrack.outcome -cne 'embedded-retained') {
		throw 'Embedded RNNoise decision was not mapped to embedded-retained rollout outcome.'
	}

	Write-SignedRnnoiseDecisionFixture `
		-Path $rnnoiseDecisionPath -SignaturePath $rnnoiseDecisionSignaturePath `
		-PrivateKeyBase64 $privateKeyBase64 -PublicKeyHex $publicKeyHex -OpenSslPath $openSsl `
		-Status custom-selected
	& (Join-Path $scriptsRoot 'new-input-enhancement-rollout-qualification.ps1') `
		-AggregateExportPath $aggregatePath -AggregateExportSignaturePath $aggregateSignaturePath `
		-AggregatePublicKeyHex $aggregatePublicKeyHex -ExpectedQuerySha256 $rolloutQuerySha256 `
		-RnnoiseDecisionPath $rnnoiseDecisionPath -RnnoiseDecisionSignaturePath $rnnoiseDecisionSignaturePath `
		-PrivateKeyBase64 $privateKeyBase64 -ExpectedPublicKeyHex $publicKeyHex `
		-OutputPath $rolloutPath -SignaturePath $rolloutSignaturePath -OpenSslPath $openSsl
	$customRollout = Read-ReleaseJson -Path $rolloutPath
	if ([string]$customRollout.domainRnnoiseTrack.outcome -cne 'custom-promoted') {
		throw 'Custom-selected RNNoise decision was not mapped to custom-promoted rollout outcome.'
	}
	& (Join-Path $scriptsRoot 'assert-input-enhancement-rollout-qualification.ps1') `
		@autoRolloutVerification -TargetStage auto-default
	$validDecisionBytes = [IO.File]::ReadAllBytes($rnnoiseDecisionPath)
	$validDecisionSignatureBytes = [IO.File]::ReadAllBytes($rnnoiseDecisionSignaturePath)
	$validDecisionText = [Text.UTF8Encoding]::new($false, $true).GetString($validDecisionBytes)
	[IO.File]::WriteAllText($rnnoiseDecisionPath, "$validDecisionText ", [Text.UTF8Encoding]::new($false))
	Assert-Throws -Description 'completed RNNoise rollout with tampered decision bytes' -Script {
		& (Join-Path $scriptsRoot 'assert-input-enhancement-rollout-qualification.ps1') `
			@autoRolloutVerification -TargetStage auto-default
	}
	[IO.File]::WriteAllBytes($rnnoiseDecisionPath, $validDecisionBytes)
	[IO.File]::WriteAllBytes($rnnoiseDecisionSignaturePath, $validDecisionSignatureBytes)

	$replacedDecisionText = $validDecisionText.Replace(('5' * 64), ('9' * 64))
	[IO.File]::WriteAllText($rnnoiseDecisionPath, $replacedDecisionText, [Text.UTF8Encoding]::new($false))
	Protect-FileWithEd25519 -InputPath $rnnoiseDecisionPath -SignaturePath $rnnoiseDecisionSignaturePath `
		-PrivateKeyBase64 $privateKeyBase64 -ExpectedPublicKeyHex $publicKeyHex -OpenSslPath $openSsl
	Assert-Throws -Description 'completed RNNoise rollout with re-signed but unbound decision' -Script {
		& (Join-Path $scriptsRoot 'assert-input-enhancement-rollout-qualification.ps1') `
			@autoRolloutVerification -TargetStage auto-default
	}
	[IO.File]::WriteAllBytes($rnnoiseDecisionPath, $validDecisionBytes)
	[IO.File]::WriteAllBytes($rnnoiseDecisionSignaturePath, $validDecisionSignatureBytes)

	$validAutoRolloutBytes = [IO.File]::ReadAllBytes($rolloutPath)
	$validAutoRolloutSignatureBytes = [IO.File]::ReadAllBytes($rolloutSignaturePath)
	$validAutoRolloutText = [Text.UTF8Encoding]::new($false, $true).GetString($validAutoRolloutBytes)
	$stringVersionRollout = [regex]::Replace($validAutoRolloutText, '"schemaVersion"\s*:\s*2',
		'"schemaVersion": "2"', 1)
	[IO.File]::WriteAllText($rolloutPath, $stringVersionRollout, [Text.UTF8Encoding]::new($false))
	Protect-FileWithEd25519 -InputPath $rolloutPath -SignaturePath $rolloutSignaturePath `
		-PrivateKeyBase64 $privateKeyBase64 -ExpectedPublicKeyHex $publicKeyHex -OpenSslPath $openSsl
	Assert-Throws -Description 'string rollout envelope schemaVersion' -Script {
		& (Join-Path $scriptsRoot 'assert-input-enhancement-rollout-qualification.ps1') `
			@autoRolloutVerification -TargetStage auto-default
	}
	[IO.File]::WriteAllBytes($rolloutPath, $validAutoRolloutBytes)
	[IO.File]::WriteAllBytes($rolloutSignaturePath, $validAutoRolloutSignatureBytes)

	$tamperedRolloutText = $validAutoRolloutText.Replace($rolloutQuerySha256, ('e' * 64))
	[IO.File]::WriteAllText($rolloutPath, $tamperedRolloutText, [Text.UTF8Encoding]::new($false))
	Protect-FileWithEd25519 -InputPath $rolloutPath -SignaturePath $rolloutSignaturePath `
		-PrivateKeyBase64 $privateKeyBase64 -ExpectedPublicKeyHex $publicKeyHex -OpenSslPath $openSsl
	Assert-Throws -Description 're-signed rollout reference that differs from aggregate provenance' -Script {
		& (Join-Path $scriptsRoot 'assert-input-enhancement-rollout-qualification.ps1') `
			@autoRolloutVerification -TargetStage auto-default
	}

	$legacyRolloutText = [regex]::Replace($validAutoRolloutText, '"schemaVersion"\s*:\s*2',
		'"schemaVersion": 1', 1)
	[IO.File]::WriteAllText($rolloutPath, $legacyRolloutText, [Text.UTF8Encoding]::new($false))
	Protect-FileWithEd25519 -InputPath $rolloutPath -SignaturePath $rolloutSignaturePath `
		-PrivateKeyBase64 $privateKeyBase64 -ExpectedPublicKeyHex $publicKeyHex -OpenSslPath $openSsl
	Assert-Throws -Description 'legacy manual rollout schema v1' -Script {
		& (Join-Path $scriptsRoot 'assert-input-enhancement-rollout-qualification.ps1') `
			@autoRolloutVerification -TargetStage auto-default
	}
	[System.IO.File]::WriteAllBytes((Join-Path $stageRoot 'mumble.exe'), [byte[]](11, 12, 13, 14, 15))
	$testUpdaterSourcePath = (Get-Process -Id $PID -ErrorAction Stop).Path
	if ([string]::IsNullOrWhiteSpace($testUpdaterSourcePath) -or
		-not (Test-Path -LiteralPath $testUpdaterSourcePath -PathType Leaf) -or
		[IO.Path]::GetExtension($testUpdaterSourcePath) -cne '.exe') {
		throw "The executing PowerShell process is not a usable deterministic PE fixture: '$testUpdaterSourcePath'."
	}
	$testUpdaterSource = Get-Item -LiteralPath $testUpdaterSourcePath -ErrorAction Stop
	if ($testUpdaterSource.PSIsContainer -or $testUpdaterSource.Length -le 0) {
		throw "The executing PowerShell process is not a non-empty regular-file PE fixture: '$testUpdaterSourcePath'."
	}
	$testUpdaterSourceHash = Get-ReleaseFileSha256 -Path $testUpdaterSource.FullName
	$testUpdaterPath = Join-Path $stageRoot 'mumble-updater.exe'
	Copy-Item -LiteralPath $testUpdaterSource.FullName -Destination $testUpdaterPath -Force
	if ((Get-ReleaseFileSha256 -Path $testUpdaterPath) -cne $testUpdaterSourceHash) {
		throw 'The deterministic updater PE fixture changed while it was copied into the staged payload.'
	}
	& (Join-Path $scriptsRoot 'assert-mumble-updater-static-runtime.ps1') -UpdaterPath $testUpdaterPath
	[System.IO.File]::WriteAllBytes((Join-Path $stageRoot 'zlib1.dll'), [byte[]](31, 32, 33))
	Initialize-TestQtQuickPayload -Root $stageRoot
	$updatePackagePath = Join-Path $tempRoot "mumble-forked-1.7.42.mumble-update"
	& (Join-Path $scriptsRoot 'create-windows-update-package.ps1') `
		-StageRoot $stageRoot -OutputPath $updatePackagePath -Version '1.7.42' `
		-Build $buildNumber -Commit $sourceSha -RequireUpdaterRuntime -Validate
	$dumpbinPassPath = Join-Path $tempRoot 'dumpbin-static-pass.cmd'
	[IO.File]::WriteAllText($dumpbinPassPath, "@echo off`r`necho KERNEL32.dll`r`nexit /b 0`r`n",
		[Text.ASCIIEncoding]::new())
	$dumpbinDynamicPath = Join-Path $tempRoot 'dumpbin-static-missing.cmd'
	[IO.File]::WriteAllText($dumpbinDynamicPath, "@echo off`r`necho     zlib1.dll`r`nexit /b 0`r`n",
		[Text.ASCIIEncoding]::new())
	Assert-Throws -Description 'update package whose updater is not statically linked' -Script {
		& (Join-Path $scriptsRoot 'assert-windows-update-package.ps1') `
			-PackagePath $updatePackagePath -ExpectedCommit $sourceSha -ExpectedBuild $buildNumber `
			-ExpectedVersion '1.7.42' -RequireUpdaterRuntime -DumpbinPath $dumpbinDynamicPath
	}
	& (Join-Path $scriptsRoot 'assert-windows-update-package.ps1') `
		-PackagePath $updatePackagePath -ExpectedCommit $sourceSha -ExpectedBuild $buildNumber `
		-ExpectedVersion '1.7.42' -RequireUpdaterRuntime -DumpbinPath $dumpbinPassPath
	$administrativeImageRoot = Join-Path $tempRoot 'administrative-image'
	$administrativePayloadRoot = Join-Path $administrativeImageRoot 'Program Files\Mumble\client'
	New-Item -ItemType Directory -Force -Path $administrativePayloadRoot | Out-Null
	Copy-Item -Path (Join-Path $stageRoot '*') -Destination $administrativePayloadRoot -Recurse -Force
	Copy-Item -LiteralPath $installerPath -Destination (Join-Path $administrativeImageRoot 'administrative-source.msi')
	[IO.File]::WriteAllBytes((Join-Path $administrativeImageRoot 'Mumble.cab'), [byte[]](41, 42, 43))
	$msiPayloadVerificationPath = Join-Path $tempRoot 'msi-payload-verification.json'
	& (Join-Path $scriptsRoot 'assert-input-enhancement-msi-payload.ps1') `
		-MsiPath $installerPath `
		-QualifiedPayloadRoot $stageRoot `
		-AdministrativeImageRoot $administrativeImageRoot `
		-OutputPath $msiPayloadVerificationPath
	$msiPayloadVerification = Read-ReleaseJson -Path $msiPayloadVerificationPath
	$expectedMsiPayloadFiles = @(Get-ChildItem -LiteralPath $stageRoot -Recurse -File).Count
	if ($msiPayloadVerification.passed -ne $true -or
		[int]$msiPayloadVerification.verifiedFileCount -ne $expectedMsiPayloadFiles -or
		[int]$msiPayloadVerification.allowedAdministrativeArtifactCount -ne 2) {
		throw 'MSI administrative-image fixture did not verify complete qualified payload parity.'
	}
	$administrativeModelPath = Join-Path $administrativePayloadRoot 'rnnoise\test-model.bin'
	$administrativeModelBytes = [IO.File]::ReadAllBytes($administrativeModelPath)
	[IO.File]::WriteAllBytes($administrativeModelPath, [byte[]](9, 9, 9))
	try {
		Assert-Throws -Description 'MSI administrative-image model mismatch' -Script {
			& (Join-Path $scriptsRoot 'assert-input-enhancement-msi-payload.ps1') `
				-MsiPath $installerPath `
				-QualifiedPayloadRoot $stageRoot `
				-AdministrativeImageRoot $administrativeImageRoot
		}
	} finally {
		[IO.File]::WriteAllBytes($administrativeModelPath, $administrativeModelBytes)
	}
	$administrativeUpdaterPath = Join-Path $administrativePayloadRoot 'mumble-updater.exe'
	$administrativeUpdaterBytes = [IO.File]::ReadAllBytes($administrativeUpdaterPath)
	Remove-Item -LiteralPath $administrativeUpdaterPath -Force
	try {
		Assert-Throws -Description 'MSI administrative-image missing managed file' -Script {
			& (Join-Path $scriptsRoot 'assert-input-enhancement-msi-payload.ps1') `
				-MsiPath $installerPath `
				-QualifiedPayloadRoot $stageRoot `
				-AdministrativeImageRoot $administrativeImageRoot
		}
	} finally {
		[IO.File]::WriteAllBytes($administrativeUpdaterPath, $administrativeUpdaterBytes)
	}
	$unexpectedManagedPath = Join-Path $administrativePayloadRoot 'unexpected-managed.dll'
	[IO.File]::WriteAllBytes($unexpectedManagedPath, [byte[]](51, 52, 53))
	try {
		Assert-Throws -Description 'MSI administrative-image unexpected managed file' -Script {
			& (Join-Path $scriptsRoot 'assert-input-enhancement-msi-payload.ps1') `
				-MsiPath $installerPath `
				-QualifiedPayloadRoot $stageRoot `
				-AdministrativeImageRoot $administrativeImageRoot
		}
	} finally {
		Remove-Item -LiteralPath $unexpectedManagedPath -Force -ErrorAction SilentlyContinue
	}
	$undocumentedAdministrativePath = Join-Path $administrativeImageRoot 'undocumented-metadata.txt'
	[IO.File]::WriteAllText($undocumentedAdministrativePath, 'not allowed')
	try {
		Assert-Throws -Description 'MSI administrative-image undocumented outer artifact' -Script {
			& (Join-Path $scriptsRoot 'assert-input-enhancement-msi-payload.ps1') `
				-MsiPath $installerPath `
				-QualifiedPayloadRoot $stageRoot `
				-AdministrativeImageRoot $administrativeImageRoot
		}
	} finally {
		Remove-Item -LiteralPath $undocumentedAdministrativePath -Force -ErrorAction SilentlyContinue
	}
	# Qualification accepts only the report shape emitted by a real msiexec /a
	# run. The payload and administrative-artifact comparison above used a
	# deterministic pre-extracted fixture, so promote that fixture report to the
	# production method only after all negative parity tests have run.
	$qualificationMsiPayloadVerification = Read-ReleaseJson -Path $msiPayloadVerificationPath
	$qualificationMsiPayloadVerification.method = 'msiexec-administrative-image'
	Write-ReleaseJson -Path $msiPayloadVerificationPath -Value $qualificationMsiPayloadVerification
	& (Join-Path $scriptsRoot 'assert-input-enhancement-msi-payload-verification.ps1') `
		-VerificationPath $msiPayloadVerificationPath `
		-MsiPath $installerPath `
		-UpdatePackagePath $updatePackagePath
	$incompleteMsiVerification = Read-ReleaseJson -Path $msiPayloadVerificationPath
	$incompleteMsiVerification.files = @($incompleteMsiVerification.files | Select-Object -Skip 1)
	$incompleteMsiVerification.verifiedFileCount = @($incompleteMsiVerification.files).Count
	$incompleteMsiVerificationPath = Join-Path $tempRoot 'incomplete-msi-payload-verification.json'
	Write-ReleaseJson -Path $incompleteMsiVerificationPath -Value $incompleteMsiVerification
	Assert-Throws -Description 'MSI verification attestation omits a managed update file' -Script {
		& (Join-Path $scriptsRoot 'assert-input-enhancement-msi-payload-verification.ps1') `
			-VerificationPath $incompleteMsiVerificationPath `
			-MsiPath $installerPath `
			-UpdatePackagePath $updatePackagePath
	}
	New-Item -ItemType Directory -Force -Path (Join-Path $artifactFixtureRoot 'metadata') | Out-Null
	Copy-Item -LiteralPath $msiPayloadVerificationPath `
		-Destination (Join-Path $artifactFixtureRoot 'metadata\msi-payload-verification.json') -Force
	Remove-Item -LiteralPath $artifactPath -Force
	Compress-Archive -Path (Join-Path $artifactFixtureRoot '*') -DestinationPath $artifactPath
	$testedBinarySha256 = Get-ReleaseFileSha256 -Path (Join-Path $stageRoot 'mumble.exe')
	$corpusLockSha256 = ("cd" * 32)
	$mixturePlanSha256 = ("ef" * 32)
	$nightlyMixturePlanSha256 = ("f0" * 32)
	$masterCaseSetSha256 = ("a1" * 32)
	$nightlyCaseSetSha256 = ("a2" * 32)
	$stagedPayloadSha256 = ("a3" * 32)
	$serverExecutableSha256 = ("a4" * 32)
	$corpusInventorySha256 = ("a5" * 32)
	$releaseFixturesSha256 = ("a6" * 32)
	$metricsRuntimeSha256 = ("a7" * 32)
	$productModelHashes = @(
		(Get-ReleaseFileSha256 -Path $modelAsset),
		(Get-ReleaseFileSha256 -Path $qualityModelAsset)
	) | Sort-Object
	$legacyExecutableSha256 = ("12" * 32)
	$qualityHarnessSha256 = ("34" * 32)
	$hardwareFingerprints = @{
		'low-performance' = ("35" * 32)
		'mainstream' = ("36" * 32)
	}
	$qualityArtifactFileNames = [ordered]@{
		case_evidence_jsonl       = 'case-evidence.jsonl'
		failure_spectrogram_index = 'failure-spectrogram-index.json'
		junit                     = 'junit.xml'
		measurement_index_json    = 'measurement-index.json'
		per_case_csv               = 'per-case.csv'
		per_case_parquet           = 'per-case.parquet'
		summary_html               = 'summary.html'
		summary_json               = 'summary.json'
	}
	$originalCases = @(1..45 | ForEach-Object {
		[ordered]@{
			enhancement_profile = 'Original'
			model_initialization_attempts = 0
			algorithmic_latency_samples = 0
			fallback_count = 0
			deadline_miss_count = 0
			candidate_executable_sha256 = $testedBinarySha256
			legacy_executable_sha256 = $legacyExecutableSha256
			original_receiver_fixed_timeline_passed = $true
		}
	})
	$qualityEvidence = {
		param([string]$Suite, [string]$RunnerClass)
		$caseCount = if ($Suite -ceq 'nightly') { 5000 } else { 500 }
		$perProfileCases = [int]($caseCount / 5)
		$mixtureHash = if ($Suite -ceq 'nightly') { $nightlyMixturePlanSha256 } else { $mixturePlanSha256 }
		$caseSetHash = if ($Suite -ceq 'nightly') { $nightlyCaseSetSha256 } else { $masterCaseSetSha256 }
		$latencies = @{ Original = 0.0; Light = 10.0; Balanced = 30.0; Quality = 50.0; VoiceFocus = 50.0 }
		$profiles = @('Original', 'Light', 'Balanced', 'Quality', 'VoiceFocus') | ForEach-Object {
			$profile = $_
			[ordered]@{
				profile = $profile
				case_count = $perProfileCases
				passed = $true
				metrics = [ordered]@{
					algorithmic_latency_ms_max = [double]$latencies[$profile]
					worst_language_clean_estoi_loss_median = 0.005
					worst_language_clean_dnsmos_sig_loss_median = 0.02
					worst_language_wer_loss_percentage_points = 0.5
					noisy_dnsmos_ovrl_improvement_median = 0.25
					noisy_dnsmos_bak_improvement_median = 0.55
					severe_noise_bak_improvement_over_quality_median = if ($profile -ceq 'VoiceFocus') { 0.10 } else { 0.0 }
					worst_cohort_ovrl_loss_median = 0.05
					catastrophe_rate_percent = 0.0
					max_speech_edge_loss_ms = 10.0
					nan_or_inf_count = 0
					new_clipping_cases = 0
					unexplained_fallbacks = 0
					model_hash_errors = 0
					deadline_misses = 0
					latency_attestation_failures = 0
					tail_drain_failures = 0
				}
				performance = [ordered]@{
					average_rtf = 0.10
					p99_callback_ms = 4.0
					p99_worker_ms = 4.0
					max_internal_processing_ms = 9.0
					memory_growth_bytes = 0
					soak_duration_seconds = if ($Suite -ceq 'nightly' -and $profile -cin @('Balanced', 'Quality', 'VoiceFocus')) { 3600 } else { 0 }
				}
			}
		}
		$artifactRecords = [ordered]@{}
		foreach ($artifactName in $qualityArtifactFileNames.Keys) {
			$artifactRecords[$artifactName] = [ordered]@{
				path = "artifacts/$Suite-$RunnerClass/$($qualityArtifactFileNames[$artifactName])"
				sha256 = ("ab" * 32)
				size_bytes = 0
				contains_audio_samples = $false
			}
		}
		return [ordered]@{
			schema_version = 3
			qualification_scope = 'core'
			suite = $Suite
			status = 'passed'
			generated_at_utc = '2026-07-14T12:00:00Z'
			build = [ordered]@{
				git_sha = $sourceSha
				tested_binary_sha256 = $testedBinarySha256
				staged_payload_sha256 = $stagedPayloadSha256
				legacy_binary_sha256 = $legacyExecutableSha256
				server_binary_sha256 = $serverExecutableSha256
				harness_sha256 = $qualityHarnessSha256
				hardware_fingerprint_sha256 = $hardwareFingerprints[$RunnerClass]
				runner_class = $RunnerClass
				corpus_lock_sha256 = $corpusLockSha256
				corpus_inventory_sha256 = $corpusInventorySha256
				mixture_plan_sha256 = $mixtureHash
				case_set_sha256 = $caseSetHash
				release_fixtures_sha256 = $releaseFixturesSha256
				metrics_runtime_sha256 = $metricsRuntimeSha256
				model_manifest_sha256 = Get-ReleaseFileSha256 -Path $unsignedModelManifestPath
				recipe_manifest_sha256 = Get-ReleaseFileSha256 -Path $unsignedRecipeManifestPath
				recipe_set_version = 'self-test-r1'
				model_hashes = $productModelHashes
			}
			coverage = [ordered]@{
				case_count = $caseCount
				failed_case_count = 0
				cold_start_cases = [int]($caseCount / 2)
				warm_start_cases = [int]($caseCount / 2)
				fixed_timeline_cases = $caseCount
				receiver_cleanup_cases = 0
				languages = @('en-US', 'sv-SE')
				objective_signal_stages = @('receiver-capture')
				wer_reference_kinds = @('clean-asr-consistency')
				source_diversity = [ordered]@{
					speaker_groups = if ($Suite -ceq 'nightly') { 16 } else { 8 }
					languages = 2
					noise_groups = if ($Suite -ceq 'nightly') { 16 } else { 8 }
					noise_classes = if ($Suite -ceq 'nightly') { 10 } else { 6 }
					rir_groups = if ($Suite -ceq 'nightly') { 12 } else { 6 }
					device_groups = if ($Suite -ceq 'nightly') { 8 } else { 4 }
				}
			}
			profiles = $profiles
			auto_transitions = $null
			artifacts = $artifactRecords
			violations = @()
		}
	}
	$qualityCaseRecords = {
		param([string]$Suite, [string]$RunnerClass, [string]$ArtifactRoot)
		$caseCount = if ($Suite -ceq 'nightly') { 5000 } else { 500 }
		$perProfileCases = [int]($caseCount / 5)
		$speakerCount = if ($Suite -ceq 'nightly') { 16 } else { 8 }
		$noiseGroupCount = if ($Suite -ceq 'nightly') { 16 } else { 8 }
		$noiseClassCount = if ($Suite -ceq 'nightly') { 10 } else { 6 }
		$roomCount = if ($Suite -ceq 'nightly') { 12 } else { 6 }
		$deviceCount = if ($Suite -ceq 'nightly') { 8 } else { 4 }
		$latencies = @{ Original = 0.0; Light = 10.0; Balanced = 30.0; Quality = 50.0; VoiceFocus = 50.0 }
		$hashA = ('aa' * 32)
		$hashB = ('bb' * 32)
		$hashC = ('cc' * 32)
		$runtimeFile = [ordered]@{ relative_path = 'pinned/file.bin'; sha256 = $hashA; size_bytes = 1 }
		$audioFile = [ordered]@{ sha256 = $hashA; size_bytes = 1; channels = 1; frames = 52000; sample_rate_hz = 48000 }
		$runtimeModels = [ordered]@{}
		foreach ($modelId in @('dnsmos', 'estoi', 'wer-en', 'wer-sv')) {
			$runtimeModels[$modelId] = [ordered]@{ id = $modelId; relative_path = "models/$modelId.bin"; sha256 = $hashA; size_bytes = 1 }
		}
		foreach ($profile in @('Original', 'Light', 'Balanced', 'Quality', 'VoiceFocus')) {
			for ($caseIndex = 0; $caseIndex -lt $perProfileCases; $caseIndex++) {
				$condition = switch ($caseIndex % 6) {
					0 { 'clean' }
					1 { 'clean' }
					2 { 'noisy' }
					3 { 'noisy' }
					4 { 'severe' }
					default { 'severe' }
				}
				$cohortId = switch ($condition) {
					'clean' { 'clean-room' }
					'noisy' { 'noisy-hvac' }
					default { 'severe-babble' }
				}
				$caseId = "case-$($caseIndex.ToString('D6'))"
				$language = if (($caseIndex % 2) -eq 0) { 'en-US' } else { 'sv-SE' }
				$werLanguage = if ($language -clike 'sv-*') { 'sv' } else { 'en' }
				$noiseClassId = if ($condition -ceq 'clean') {
					$null
				} else {
					$noiseOrdinal = ([math]::Floor($caseIndex / 6) * 4) + ($caseIndex % 6) - 2
					"noise-class-$([int]($noiseOrdinal % $noiseClassCount))"
				}
				$candidateLatencySamples = [int]([double]$latencies[$profile] * 48.0)
				$routeOffsetSamples = 480
				$ovrlDelta = if ($condition -ceq 'clean') { -0.05 } else { 0.25 }
				$bakDelta = if ($profile -ceq 'VoiceFocus' -and $condition -ceq 'severe') { 0.65 } else { 0.55 }
				$objectiveRelativePath = "artifacts/$Suite-$RunnerClass/objective-scores/$profile/$caseId.json"
				$objectivePath = Join-Path $ArtifactRoot $objectiveRelativePath
				New-Item -ItemType Directory -Force -Path (Split-Path -Parent $objectivePath) | Out-Null
				$objectiveScore = [ordered]@{
					schema_version = 2
					scorer = 'mumble-objective-quality-v2'
					status = 'passed'
					case_id = $caseId
					profile = $profile
					condition = $condition
					dataset_split = 'validation'
					alignment = [ordered]@{
						method = 'caller-declared-fixed-latency'
						correlation_search_used = $false
						signal_stage = 'receiver-capture'
						sample_rate_hz = 48000
						reference_samples = 48000
						original_latency_samples = 0
						candidate_latency_samples = $candidateLatencySamples
						original_window_start_samples = $routeOffsetSamples
						candidate_window_start_samples = $routeOffsetSamples + $candidateLatencySamples
						qualified_route_binding = [ordered]@{
							route_offset_samples = $routeOffsetSamples
							control_wav = [ordered]@{ sha256 = $hashA; size_bytes = 1 }
							control_fixed_timeline_score = [ordered]@{ sha256 = $hashA; size_bytes = 1 }
							candidate_fixed_timeline_score = [ordered]@{ sha256 = $hashB; size_bytes = 1 }
							e2e_manifest = [ordered]@{ sha256 = $hashC; size_bytes = 1 }
							stable_execution_identity = [ordered]@{
								client_binary_sha256 = $hashA
								model_manifest_sha256 = $hashA
								recipe_manifest_sha256 = $hashA
								run_provenance_sha256 = $hashA
								runtime_payload_sha256 = $hashA
								server_binary_sha256 = $hashA
							}
							edge_tail_gate = [ordered]@{
								candidate_passed = $true
								control_passed = $true
								pre_opus_complete_tail_required = $true
								pre_opus_max_end_loss_samples = 480
								pre_opus_max_onset_loss_samples = 480
							}
						}
					}
					inputs = [ordered]@{
						clean_reference = [ordered]@{ sha256 = $hashA; size_bytes = 1; channels = 1; frames = 48000; sample_rate_hz = 48000 }
						noisy_original = $audioFile
						candidate = $audioFile
					}
					runtime = [ordered]@{
						id = 'self-test-runtime'
						version = '1'
						manifest = $runtimeFile
						lock = $runtimeFile
						inventory = $runtimeFile
						sources = [ordered]@{
							dnsmos = [ordered]@{ repository = 'microsoft/DNS-Challenge'; revision = ('11' * 20) }
							wer = [ordered]@{ repository = 'Systran/faster-whisper-small'; revision = ('22' * 20) }
						}
						assets_tree_sha256 = $hashA
						distributions_tree_sha256 = $hashA
						whisper_tree_sha256 = $hashA
						models = $runtimeModels
						legacy_local_scorer_pin = $runtimeFile
					}
					scorer_files = [ordered]@{
						cli = [ordered]@{ name = 'score-objective-quality.py'; sha256 = $hashA; size_bytes = 1 }
						implementation = [ordered]@{ name = 'objective_quality_score.py'; sha256 = $hashA; size_bytes = 1 }
					}
					wer_reference = [ordered]@{
						kind = 'clean-asr-consistency'
						label = 'clean-ASR-consistency WER'
						language = $werLanguage
						normalization = 'unicode-nfkc-casefold-alnum-v1'
						text_sha256 = $hashA
						word_count = 200
						artifact = [ordered]@{ sha256 = $hashA; size_bytes = 1 }
						attestation = $null
					}
					metrics = [ordered]@{
						original = [ordered]@{
							dnsmos_bak = 2.0; dnsmos_ovrl = 2.0; dnsmos_sig = 2.0; estoi = 0.8
							wer = [ordered]@{ errors = 0; reference_words = 200; rate = 0.0; hypothesis_sha256 = $hashA }
						}
						candidate = [ordered]@{
							dnsmos_bak = 2.0 + $bakDelta; dnsmos_ovrl = 2.0 + $ovrlDelta; dnsmos_sig = 1.98; estoi = 0.795
							wer = [ordered]@{ errors = 1; reference_words = 200; rate = 0.005; hypothesis_sha256 = $hashB }
						}
					}
					candidate_minus_original = [ordered]@{
						dnsmos_bak = $bakDelta; dnsmos_ovrl = $ovrlDelta; dnsmos_sig = -0.02; estoi = -0.005
						wer_delta_kind = 'clean-asr-consistency'; wer_delta_percentage_points = 0.5
					}
				}
				Write-ReleaseJson -Path $objectivePath -Value $objectiveScore
				[ordered]@{
					record_type = 'case'
					case_id = $caseId
					profile = $profile
					condition = $condition
					dataset_split = 'validation'
					cohort_id = $cohortId
					speaker_group_id = "speaker-$($caseIndex % $speakerCount)"
					noise_group_id = if ($condition -ceq 'clean') { $null } else { "noise-$($caseIndex % $noiseGroupCount)" }
					noise_class = $noiseClassId
					rir_group_id = "room-$($caseIndex % $roomCount)"
					device_group_id = "device-$($caseIndex % $deviceCount)"
					language = $language
					startup_preroll_ms = if (($caseIndex % 2) -eq 0) { 0 } else { 300 }
					fixed_timeline = $true
					receiver_cleanup_enabled = $false
					failed = $false
					quality_pair_case_id = if ($profile -ceq 'VoiceFocus' -and $condition -ceq 'severe') { $caseId } else { $null }
					objective_score = [ordered]@{
						path = $objectiveRelativePath
						sha256 = Get-ReleaseFileSha256 -Path $objectivePath
						signal_stage = 'receiver-capture'
						size_bytes = [int64](Get-Item -LiteralPath $objectivePath).Length
						wer_reference_kind = 'clean-asr-consistency'
						wer_reference_text_sha256 = $hashA
					}
					metrics = [ordered]@{
						algorithmic_latency_ms = [double]$latencies[$profile]
						dnsmos_bak_improvement = $bakDelta
						dnsmos_ovrl_improvement = $ovrlDelta
						dnsmos_sig_loss = 0.02
						estoi_loss = 0.005
						severe_noise_bak_improvement_over_quality = if ($profile -ceq 'VoiceFocus' -and $condition -ceq 'severe') { 0.10 } else { 0.0 }
						speech_edge_loss_ms = 10.0
						wer_loss_percentage_points = 0.5
					}
					counters = [ordered]@{
						deadline_misses = 0; latency_attestation_failures = 0; model_hash_errors = 0
						nan_or_inf_count = 0; new_clipping_cases = 0; tail_drain_failures = 0; unexplained_fallbacks = 0
					}
					performance = [ordered]@{
						audio_duration_seconds = 10.0; processing_duration_seconds = 1.0
						callback_durations_ms = @(4.0); worker_durations_ms = @(4.0)
						max_internal_processing_ms = 9.0; memory_growth_bytes = 0
						soak_duration_seconds = if ($Suite -ceq 'nightly' -and $caseIndex -eq 0 -and $profile -cin @('Balanced', 'Quality', 'VoiceFocus')) { 3600 } else { 0 }
					}
				}
			}
		}
	}
	$measurementFixtureGeneratorPath = Join-Path $tempRoot 'materialize-measurement-fixture.py'
	$measurementFixtureGeneratorSource = @'
from __future__ import annotations

import copy
import hashlib
import json
import sys
from pathlib import Path, PurePosixPath

quality_path = Path(sys.argv[1])
records_path = Path(sys.argv[2])
artifact_root = Path(sys.argv[3])
audio_quality_root = Path(sys.argv[4])
alias_mode = sys.argv[5] if len(sys.argv) > 5 else 'none'
if alias_mode not in ('none', 'original-candidate', 'enhanced-edge-control'):
    raise RuntimeError(f'unsupported role-alias fixture mode: {alias_mode}')
sys.path.insert(0, str(audio_quality_root))

from measurement_evidence import (  # noqa: E402
    INDEX_KIND,
    METRICS_RUNTIME_KIND,
    SOAK_KIND,
    canonical_json_bytes,
    canonical_json_sha256,
)
from quality_case_evidence import (  # noqa: E402
    qualification_binding_sha256,
    summarize_case_evidence,
    write_case_evidence,
)


def load_json(path: Path):
    return json.loads(path.read_text(encoding='utf-8'))


def sha256_text(value: str) -> str:
    return hashlib.sha256(value.encode('utf-8')).hexdigest()


def write_json(relative: str, value, *, canonical: bool = False):
    path = artifact_root.joinpath(*PurePosixPath(relative).parts)
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = (
        canonical_json_bytes(value) + b'\n'
        if canonical
        else (json.dumps(value, sort_keys=True, separators=(',', ':')) + '\n').encode('utf-8')
    )
    path.write_bytes(payload)
    return {
        'contains_audio_samples': False,
        'path': relative,
        'sha256': hashlib.sha256(payload).hexdigest(),
        'size_bytes': len(payload),
    }


def existing_reference(relative: str):
    path = artifact_root.joinpath(*PurePosixPath(relative).parts)
    payload = path.read_bytes()
    return {
        'contains_audio_samples': False,
        'path': relative,
        'sha256': hashlib.sha256(payload).hexdigest(),
        'size_bytes': len(payload),
    }


quality = load_json(quality_path)
record_document = load_json(records_path)
cases = record_document['cases']
transitions = record_document['auto_transitions']
build = quality['build']
suite = quality['suite']
runner_class = build['runner_class']
prefix = f'artifacts/{suite}-{runner_class}/'
profiles = ('Original', 'Light', 'Balanced', 'Quality', 'VoiceFocus')
latency_samples = {'Original': 0, 'Light': 480, 'Balanced': 1440, 'Quality': 2400, 'VoiceFocus': 2400}
engines = {'Original': 'None', 'Light': 'Speex', 'Balanced': 'RNNoise', 'Quality': 'DeepFilterNet', 'VoiceFocus': 'DeepFilterNet'}
recipe_ids = {
    'Original': 'input.original',
    'Light': 'input.light.speex',
    'Balanced': 'input.balanced.self-test',
    'Quality': 'input.quality.self-test',
    'VoiceFocus': 'input.voice-focus.self-test',
}
model_ids = {'Balanced': 'rnnoise:self-test', 'Quality': 'deepfilternet:self-test', 'VoiceFocus': 'deepfilternet:self-test'}
model_hashes = list(build['model_hashes'])


def profile_binding(profile: str):
    engine = engines[profile]
    models = []
    if engine == 'RNNoise':
        models = [{'id': model_ids[profile], 'sha256': model_hashes[0], 'version': '1'}]
    elif engine == 'DeepFilterNet':
        models = [{'id': model_ids[profile], 'sha256': model_hashes[-1], 'version': '1'}]
    return {
        'profile': profile,
        'engine': engine,
        'recipe': {
            'catalog_revision': 'self-test-v2',
            'id': recipe_ids[profile],
            'manifest_sha256': build['recipe_manifest_sha256'],
            'revision': 1,
        },
        'models': models,
    }


profile_bindings = [profile_binding(profile) for profile in profiles]
binding_by_profile = {binding['profile']: binding for binding in profile_bindings}
objective_by_key = {}
for case in cases:
    key = (case['profile'], case['case_id'])
    objective_path = artifact_root.joinpath(*PurePosixPath(case['objective_score']['path']).parts)
    objective_by_key[key] = load_json(objective_path)

runtime_bindings = {
    canonical_json_sha256({'runtime': score['runtime'], 'scorer_files': score['scorer_files']})
    for score in objective_by_key.values()
}
if len(runtime_bindings) != 1:
    raise RuntimeError('fixture objective reports do not share one metrics runtime binding')
objective_runtime_binding_sha256 = next(iter(runtime_bindings))
first_objective = next(iter(objective_by_key.values()))
runtime_manifest = first_objective['runtime']['manifest']
runtime_files = [{
    'path': runtime_manifest['relative_path'],
    'sha256': runtime_manifest['sha256'],
    'size_bytes': runtime_manifest['size_bytes'],
}]
build['metrics_runtime_sha256'] = canonical_json_sha256(runtime_files)

run_provenance_sha256 = sha256_text(f'{suite}:{runner_class}:release-fixture-provenance')


def execution_identity(*, contract: str | None = None):
    value = {
        'client_binary_sha256': build['tested_binary_sha256'],
        'model_manifest_sha256': build['model_manifest_sha256'],
        'recipe_manifest_sha256': build['recipe_manifest_sha256'],
        'run_provenance_sha256': run_provenance_sha256,
        'runtime_payload_sha256': build['staged_payload_sha256'],
        'server_binary_sha256': build['server_binary_sha256'],
    }
    if contract is not None:
        value['contract_file_sha256'] = sha256_text(f'{suite}:{runner_class}:{contract}:adapter-contract')
    return value


def adapter_document(role: str, profile: str, input_sha256: str, capture_sha256: str, sender_sha256: str):
    binding = binding_by_profile[profile]
    return {
        'schema_version': 3,
        'status': 'passed',
        'role': role,
        'profile': profile,
        'receiver_cleanup': False,
        'input_sha256': input_sha256,
        'execution_identity': execution_identity(contract=f'{profile}:{role}'),
        'transport': {'opus_bitrate_bps': 40000, 'frames_per_packet': 2, 'transmit_mode': 'VAD'},
        'capture': {'relative_path': f'private/{profile}-{role}-capture.wav', 'sha256': capture_sha256, 'size_bytes': 1},
        'sender_pre_opus': {'relative_path': f'private/{profile}-{role}-sender.wav', 'sha256': sender_sha256, 'size_bytes': 1},
        'diagnostics': {
            'active_engine': binding['engine'],
            'active_models': copy.deepcopy(binding['models']),
            'active_profile': profile,
            'active_recipe': copy.deepcopy(binding['recipe']),
            'callback_frame_count': 1000,
            'callback_p99_ms': 4.0,
            'deadline_miss_count': 0,
            'declared_latency_samples': latency_samples[profile],
            'fallback_count': 0,
            'invalid_output_count': 0,
            'mean_rtf': 0.1,
            'model_initialization_attempts': 1 if binding['models'] else 0,
            'tail_drained': True,
            'worker_frame_count': 1000,
            'worker_p99_ms': 4.0,
        },
    }


source_hashes = {score['inputs']['noisy_original']['sha256'] for score in objective_by_key.values()}
clean_hashes = {score['inputs']['clean_reference']['sha256'] for score in objective_by_key.values()}
if len(source_hashes) != 1 or len(clean_hashes) != 1:
    raise RuntimeError('release-tool fixture expects one reusable source and clean hash')
source_sha256 = next(iter(source_hashes))
clean_sha256 = next(iter(clean_hashes))

shared_root = prefix + 'measurements/shared/'
original_document = adapter_document('original_comparison', 'Original', source_sha256, source_sha256, sha256_text('original-sender'))
control_document = adapter_document('control', 'Original', clean_sha256, clean_sha256, sha256_text('control-sender'))
original_reference = write_json(shared_root + 'original-adapter-result.json', original_document)
control_reference = write_json(shared_root + 'control-adapter-result.json', control_document)


def fixed_score(received_sha256: str, latency: int, alignment: str, complete_tail: bool, limit: int):
    return {
        'schema_version': 3,
        'scorer': 'mumble-fixed-timeline-v3',
        'timeline_alignment': alignment,
        'sample_rate_hz': 48000,
        'frame_samples': 480,
        'declared_latency_samples': latency,
        'reference_sha256': clean_sha256,
        'received_sha256': received_sha256,
        'onset_loss_samples': 0,
        'end_loss_samples': 0,
        'missing_tail_samples': 0 if complete_tail else 480,
        'reference_clipped_samples': 0,
        'received_clipped_samples': 0,
        'qualification_limits': {
            'max_onset_loss_samples': limit,
            'max_end_loss_samples': limit,
            'require_complete_tail': complete_tail,
            'fail_on_new_clipping': True,
        },
        'passed': True,
    }


control_fixed_reference = write_json(
    shared_root + 'control-fixed-timeline-score.json',
    fixed_score(control_document['capture']['sha256'], 0, 'fixed', False, 2880),
)
control_pre_opus_reference = write_json(
    shared_root + 'control-pre-opus-fixed-timeline-score.json',
    fixed_score(control_document['sender_pre_opus']['sha256'], 0, 'fixed', True, 480),
)

profile_reports = {}
for profile in profiles:
    if profile == 'Original':
        candidate_document = adapter_document(
            'candidate', 'Original', source_sha256, source_sha256,
            sha256_text('Original-candidate-sender'),
        )
        edge_document = adapter_document(
            'candidate_edge', 'Original', clean_sha256, clean_sha256,
            sha256_text('Original-edge-sender'),
        )
        if alias_mode == 'original-candidate':
            candidate_document = original_document
        candidate_reference = write_json(
            shared_root + 'Original-candidate-adapter-result.json', candidate_document,
        )
        edge_reference = write_json(
            shared_root + 'Original-edge-adapter-result.json', edge_document,
        )
        route_reference = write_json(
            shared_root + 'Original-route-fixed-timeline-score.json',
            fixed_score(
                candidate_document['capture']['sha256'], 0,
                'fixed-paired-original-route', False, 2880,
            ),
        )
        edge_fixed_reference = write_json(
            shared_root + 'Original-edge-fixed-timeline-score.json',
            fixed_score(
                edge_document['sender_pre_opus']['sha256'], 0, 'fixed', True, 480,
            ),
        )
    else:
        profile_scores = [score for (candidate_profile, _), score in objective_by_key.items() if candidate_profile == profile]
        candidate_hashes = {score['inputs']['candidate']['sha256'] for score in profile_scores}
        if len(candidate_hashes) != 1:
            raise RuntimeError(f'{profile} fixture cases do not share one candidate signal hash')
        candidate_sha256 = next(iter(candidate_hashes))
        candidate_document = adapter_document('candidate', profile, source_sha256, candidate_sha256, sha256_text(f'{profile}-candidate-sender'))
        edge_document = adapter_document('candidate_edge', profile, clean_sha256, clean_sha256, sha256_text(f'{profile}-edge-sender'))
        if alias_mode == 'enhanced-edge-control' and profile == 'Light':
            edge_document = control_document
        candidate_reference = write_json(shared_root + f'{profile}-candidate-adapter-result.json', candidate_document)
        edge_reference = write_json(shared_root + f'{profile}-edge-adapter-result.json', edge_document)
        route_reference = write_json(
            shared_root + f'{profile}-route-fixed-timeline-score.json',
            fixed_score(candidate_document['capture']['sha256'], latency_samples[profile], 'fixed-paired-original-route', False, 2880),
        )
        edge_fixed_reference = write_json(
            shared_root + f'{profile}-edge-fixed-timeline-score.json',
            fixed_score(edge_document['sender_pre_opus']['sha256'], latency_samples[profile], 'fixed', True, 480),
        )
    profile_reports[profile] = {
        'candidate_document': candidate_document,
        'candidate_reference': candidate_reference,
        'edge_document': edge_document,
        'edge_reference': edge_reference,
        'route_reference': route_reference,
        'edge_fixed_reference': edge_fixed_reference,
    }

if alias_mode == 'none':
    for profile, reports in profile_reports.items():
        candidate = reports['candidate_document']
        edge = reports['edge_document']
        if (candidate['role'], candidate['profile']) != ('candidate', profile):
            raise RuntimeError(f'{profile} candidate role/profile alias regression')
        if (edge['role'], edge['profile']) != ('candidate_edge', profile):
            raise RuntimeError(f'{profile} candidate-edge role/profile alias regression')
        if reports['candidate_reference']['sha256'] == original_reference['sha256']:
            raise RuntimeError(f'{profile} candidate reference aliases original-comparison bytes')
        if reports['edge_reference']['sha256'] == control_reference['sha256']:
            raise RuntimeError(f'{profile} candidate-edge reference aliases control bytes')
    if (original_document['role'], original_document['profile']) != ('original_comparison', 'Original'):
        raise RuntimeError('original-comparison role/profile fixture is invalid')
    if (control_document['role'], control_document['profile']) != ('control', 'Original'):
        raise RuntimeError('control role/profile fixture is invalid')


def manifest_result(role: str, document, reference, route_reference, edge_reference):
    diagnostics = document['diagnostics']
    return {
        'adapter_contract_sha256': document['execution_identity']['contract_file_sha256'],
        'adapter_result_sha256': reference['sha256'],
        'capture_sha256': document['capture']['sha256'],
        'sender_pre_opus_sha256': document['sender_pre_opus']['sha256'],
        'execution_identity': copy.deepcopy(document['execution_identity']),
        'active_recipe': copy.deepcopy(diagnostics['active_recipe']),
        'active_models': copy.deepcopy(diagnostics['active_models']),
        'performance': {
            'callback_frame_count': diagnostics['callback_frame_count'],
            'callback_p99_ms': diagnostics['callback_p99_ms'],
            'model_initialization_attempts': diagnostics['model_initialization_attempts'],
            'worker_frame_count': diagnostics['worker_frame_count'],
            'worker_p99_ms': diagnostics['worker_p99_ms'],
            'mean_rtf': diagnostics['mean_rtf'],
        },
        'fixed_timeline_score_sha256': route_reference['sha256'] if role in ('candidate', 'control') else None,
        'pre_opus_fixed_timeline_score_sha256': edge_reference['sha256'] if role in ('candidate_edge', 'control') else None,
        'qualification_purpose': {
            'candidate': 'noisy-enhanced-candidate',
            'candidate_edge': 'clean-enhanced-input-edge-probe',
            'original_comparison': 'noisy-original-quality-comparison',
            'control': 'clean-original-route-control',
        }[role],
    }


case_index_entries = []
for case in cases:
    profile = case['profile']
    case_id = case['case_id']
    reports = profile_reports[profile]
    plan_case_sha256 = sha256_text(f'{suite}:{profile}:{case_id}:plan-case')
    render_entry_sha256 = sha256_text(f'{suite}:{profile}:{case_id}:render-entry')
    render_manifest_sha256 = sha256_text(f'{suite}:render-manifest')
    original_result = manifest_result('original_comparison', original_document, original_reference, control_fixed_reference, control_pre_opus_reference)
    control_result = manifest_result('control', control_document, control_reference, control_fixed_reference, control_pre_opus_reference)
    results = {
        'candidate': manifest_result('candidate', reports['candidate_document'], reports['candidate_reference'], reports['route_reference'], reports['edge_fixed_reference']),
        'candidate_edge': manifest_result('candidate_edge', reports['edge_document'], reports['edge_reference'], reports['route_reference'], reports['edge_fixed_reference']),
        'original_comparison': original_result,
        'control': control_result,
    }
    manifest = {
        'schema_version': 3,
        'status': 'passed',
        'case_id': case_id,
        'profile': profile,
        'run_provenance_sha256': run_provenance_sha256,
        'receiver_cleanup': False,
        'qualification_binding': {
            'mixture_plan_sha256': build['mixture_plan_sha256'],
            'case_set_sha256': build['case_set_sha256'],
            'corpus_inventory_sha256': build['corpus_inventory_sha256'],
            'corpus_lock_sha256': build['corpus_lock_sha256'],
            'case_id': case_id,
            'profile': profile,
            'dataset_split': case['dataset_split'],
            'plan_case_sha256': plan_case_sha256,
            'render_manifest_sha256': render_manifest_sha256,
            'render_entry_sha256': render_entry_sha256,
            'source_input_sha256': source_sha256,
            'clean_reference_sha256': clean_sha256,
            'input_enhancement_policy_manifest_sha256': (
                None if profile == 'Original' else sha256_text(f'{suite}:qualification-policy-manifest')
            ),
            'input_enhancement_policy_signature_sha256': (
                None if profile == 'Original' else sha256_text(f'{suite}:qualification-policy-signature')
            ),
        },
        'input_timeline_gate': {
            'artifact': 'sender_pre_opus',
            'alignment': 'fixed-declared-latency',
            'roles': ['control', 'candidate_edge'],
            'max_onset_loss_samples': 480,
            'max_end_loss_samples': 480,
            'complete_tail_required': True,
        },
        'route_control': {
            'onset_budget_samples': 2880,
            'end_loss_budget_samples': 2880,
            'receiver_edge_gate': 'route-bounded-not-input-latency',
            'capture_tail_rule': 'vad-speech-edge',
            'causal_tail_drain_required': True,
            'legacy_original_parity_required': True,
        },
        'results': results,
        'private_audio_do_not_upload': True,
    }
    manifest_reference = write_json(
        prefix + f'measurements/cases/{profile}/{case_id}/e2e-manifest.json',
        manifest,
    )

    objective = objective_by_key[(profile, case_id)]
    objective['alignment']['qualified_route_binding'] = {
        'route_offset_samples': 480,
        'control_wav': {'sha256': control_document['capture']['sha256'], 'size_bytes': control_document['capture']['size_bytes']},
        'control_fixed_timeline_score': {'sha256': control_fixed_reference['sha256'], 'size_bytes': control_fixed_reference['size_bytes']},
        'candidate_fixed_timeline_score': {'sha256': reports['route_reference']['sha256'], 'size_bytes': reports['route_reference']['size_bytes']},
        'e2e_manifest': {'sha256': manifest_reference['sha256'], 'size_bytes': manifest_reference['size_bytes']},
        'stable_execution_identity': execution_identity(),
        'edge_tail_gate': {
            'candidate_passed': True,
            'control_passed': True,
            'pre_opus_complete_tail_required': True,
            'pre_opus_max_end_loss_samples': 480,
            'pre_opus_max_onset_loss_samples': 480,
        },
    }
    objective_reference = write_json(case['objective_score']['path'], objective)
    case['objective_score'].update({
        'sha256': objective_reference['sha256'],
        'size_bytes': objective_reference['size_bytes'],
    })
    case['metrics']['speech_edge_loss_ms'] = 0.0
    case['performance'] = {
        'audio_duration_seconds': 10.0,
        'processing_duration_seconds': 1.0,
        'callback_durations_ms': [4.0],
        'worker_durations_ms': [4.0],
        'max_internal_processing_ms': 4.0,
        'memory_growth_bytes': 0,
        'soak_duration_seconds': 0,
    }
    case_index_entries.append({
        'case_id': case_id,
        'profile': profile,
        'condition': case['condition'],
        'dataset_split': case['dataset_split'],
        'measurement_mode': 'e2e',
        'plan_case_sha256': plan_case_sha256,
        'render_entry_sha256': render_entry_sha256,
        'source_input_sha256': source_sha256,
        'clean_reference_sha256': clean_sha256,
        'reports': {
            'candidate_adapter_result': reports['candidate_reference'],
            'control_adapter_result': control_reference,
            'control_fixed_timeline_score': control_fixed_reference,
            'control_pre_opus_fixed_timeline_score': control_pre_opus_reference,
            'e2e_manifest': manifest_reference,
            'edge_adapter_result': reports['edge_reference'],
            'edge_fixed_timeline_score': reports['edge_fixed_reference'],
            'objective_score': objective_reference,
            'original_adapter_result': original_reference,
            'route_fixed_timeline_score': reports['route_reference'],
        },
    })

metrics_runtime_attestation = write_json(
    shared_root + 'metrics-runtime-attestation.json',
    {
        'schema_version': 1,
        'kind': METRICS_RUNTIME_KIND,
        'payload_kind': 'directory',
        'payload_sha256': build['metrics_runtime_sha256'],
        'objective_runtime_binding_sha256': objective_runtime_binding_sha256,
        'files': runtime_files,
    },
)

soak_reports = []
if suite == 'nightly':
    for profile in ('Balanced', 'Quality', 'VoiceFocus'):
        report = {
            'schema_version': 2,
            'kind': SOAK_KIND,
            'status': 'completed',
            'profile': profile,
            'active_bindings': [copy.deepcopy(binding_by_profile[profile])],
            'execution_identity': execution_identity(),
            'declared_latency_samples': latency_samples[profile],
            'audio_duration_seconds': 3600,
            'wall_duration_seconds': 3600,
            'mean_rtf': 0.1,
            'callback_p99_ms': 4.0,
            'worker_p99_ms': 4.0,
            'maximum_internal_processing_ms': 9.0,
            'memory_growth_bytes_after_warmup': 0,
            'rss_warmup_bytes': 100000,
            'rss_end_bytes': 100000,
            'rss_peak_bytes': 110000,
            'deadline_miss_count': 0,
            'fallback_count': 0,
            'invalid_output_count': 0,
            'new_clipping_count': 0,
            'tail_drain_failure_count': 0,
        }
        reference = write_json(shared_root + f'{profile}-soak-report.json', report)
        soak_reports.append({'profile': profile, 'report': reference})
        target = min(
            (case for case in cases if case['profile'] == profile),
            key=lambda case: case['case_id'],
        )
        target['performance']['audio_duration_seconds'] += 3600.0
        target['performance']['processing_duration_seconds'] += 360.0
        target['performance']['callback_durations_ms'].append(4.0)
        target['performance']['worker_durations_ms'].append(4.0)
        target['performance']['max_internal_processing_ms'] = 9.0
        target['performance']['soak_duration_seconds'] = 3600

case_evidence_relative = quality['artifacts']['case_evidence_jsonl']['path']
case_evidence_path = artifact_root.joinpath(*PurePosixPath(case_evidence_relative).parts)
case_evidence_path.parent.mkdir(parents=True, exist_ok=True)
if case_evidence_path.exists():
    case_evidence_path.unlink()
write_case_evidence(case_evidence_path, build, quality['qualification_scope'], suite, cases, transitions)

summary = summarize_case_evidence(cases, transitions, quality['qualification_scope'], suite)
quality['coverage'] = summary['coverage']
quality['profiles'] = [
    {
        'profile': profile,
        'case_count': summary['profiles'][profile]['case_count'],
        'passed': True,
        'metrics': summary['profiles'][profile]['metrics'],
        'performance': summary['profiles'][profile]['performance'],
    }
    for profile in profiles
]
quality['status'] = 'passed'
quality['violations'] = []

for name, artifact in quality['artifacts'].items():
    if name == 'measurement_index_json':
        continue
    quality['artifacts'][name] = existing_reference(artifact['path'])

index = {
    'schema_version': 1,
    'kind': INDEX_KIND,
    'qualification_scope': quality['qualification_scope'],
    'suite': suite,
    'qualification_binding_sha256': qualification_binding_sha256(build, quality['qualification_scope'], suite),
    'objective_runtime_binding_sha256': objective_runtime_binding_sha256,
    'metrics_runtime_attestation': metrics_runtime_attestation,
    'build': copy.deepcopy(build),
    'plan_binding': {
        field: build[field]
        for field in ('case_set_sha256', 'corpus_inventory_sha256', 'corpus_lock_sha256', 'mixture_plan_sha256')
    },
    'profile_bindings': profile_bindings,
    'published_artifacts': [
        {'name': name, 'artifact': copy.deepcopy(quality['artifacts'][name])}
        for name in sorted(quality['artifacts'])
        if name != 'measurement_index_json'
    ],
    'release_holdout_approval_public_key_sha256': None,
    'release_holdout_openings': [],
    'cases': sorted(case_index_entries, key=lambda entry: (profiles.index(entry['profile']), entry['case_id'])),
    'soak_reports': soak_reports,
    'transitions': [],
}
index_reference = write_json(quality['artifacts']['measurement_index_json']['path'], index, canonical=True)
quality['artifacts']['measurement_index_json'] = index_reference
quality_path.write_text(json.dumps(quality, sort_keys=True, separators=(',', ':')) + '\n', encoding='utf-8')
'@
	[System.IO.File]::WriteAllText(
		$measurementFixtureGeneratorPath,
		$measurementFixtureGeneratorSource,
		[System.Text.UTF8Encoding]::new($false)
	)
	$originalEvidence = {
		param([string]$Suite, [string]$RunnerClass)
		return [ordered]@{
			schema_version = 1
			candidate_build_sha = $sourceSha
			legacy_build_sha = $sourceSha
			candidate_executable_sha256 = $testedBinarySha256
			legacy_executable_sha256 = $legacyExecutableSha256
			profile = 'Original'
			receiver_cleanup_enabled = $false
			server_host = '127.0.0.1'
			transport_path = 'client1-opus-server-client2'
			corpus_sha256 = $corpusLockSha256
			runnerClass = $RunnerClass
			cases = $originalCases
		}
	}
	$fakePythonPath = Join-Path $tempRoot 'fake-python.cmd'
	[System.IO.File]::WriteAllText($fakePythonPath, "@exit /b 0`r`n", [System.Text.Encoding]::ASCII)
	$measuredRunnerRoots = @{}
	$artifactRootRegressionCovered = $false
	$roleAliasRegressionCovered = $false
	foreach ($suite in @('master_quality', 'nightly')) {
		foreach ($runnerClass in @('low-performance', 'mainstream')) {
		$runnerKey = "$suite|$runnerClass"
		$runnerRoot = Join-Path $tempRoot "measured-$suite-$runnerClass"
		$measuredRunnerRoots[$runnerKey] = $runnerRoot
		New-Item -ItemType Directory -Force -Path $runnerRoot | Out-Null
		$qualityPath = Join-Path $runnerRoot 'qualification.json'
		$quality = & $qualityEvidence $suite $runnerClass
		Write-ReleaseJson -Path $qualityPath -Value $quality
		$caseRecordsPath = Join-Path $runnerRoot 'case-records.json'
		Write-ReleaseJson -Path $caseRecordsPath -Value ([ordered]@{
			schema_version = 3
			cases = @(& $qualityCaseRecords $suite $runnerClass $runnerRoot)
			auto_transitions = @()
		})
		$artifactNamespace = Join-Path (Join-Path $runnerRoot 'artifacts') "$suite-$runnerClass"
		New-Item -ItemType Directory -Force -Path $artifactNamespace | Out-Null
		foreach ($artifactName in $qualityArtifactFileNames.Keys) {
			if ($artifactName -cin @('case_evidence_jsonl', 'measurement_index_json')) {
				continue
			}
			$qualityEvidenceArtifactPath = Join-Path $artifactNamespace $qualityArtifactFileNames[$artifactName]
			[System.IO.File]::WriteAllText(
				$qualityEvidenceArtifactPath,
				"audio-free self-test artifact: $artifactName`n",
				[System.Text.UTF8Encoding]::new($false)
			)
		}
		$audioQualityRoot = Join-Path (Split-Path -Parent $scriptsRoot) 'audio-quality'
		$null = & python $measurementFixtureGeneratorPath `
			$qualityPath $caseRecordsPath $runnerRoot $audioQualityRoot
		if ($LASTEXITCODE -ne 0) {
			throw "Unable to materialize transitive measurement evidence for '$suite/$runnerClass'."
		}
		$qualityValidator = Join-Path (Split-Path -Parent $scriptsRoot) `
			'audio-quality\validate-quality-qualification.py'
		$null = & python $qualityValidator $qualityPath --artifact-root $runnerRoot
		if ($LASTEXITCODE -ne 0) {
			throw "Schema-v3 quality fixture failed semantic validation for '$suite/$runnerClass'."
		}
		if (-not $roleAliasRegressionCovered) {
			foreach ($aliasCase in @(
				[ordered]@{ mode = 'original-candidate'; expected = 'candidate_adapter_result: role/profile mismatch' },
				[ordered]@{ mode = 'enhanced-edge-control'; expected = 'edge_adapter_result: role/profile mismatch' }
			)) {
				$null = & python $measurementFixtureGeneratorPath `
					$qualityPath $caseRecordsPath $runnerRoot $audioQualityRoot $aliasCase.mode
				if ($LASTEXITCODE -ne 0) {
					throw "Unable to materialize role-alias regression fixture '$($aliasCase.mode)'."
				}
				$aliasValidation = @(& python $qualityValidator $qualityPath --artifact-root $runnerRoot 2>&1)
				if ($LASTEXITCODE -eq 0 -or
					-not (($aliasValidation -join "`n").Contains([string]$aliasCase.expected))) {
					throw "Schema-v3 validator did not reject role alias '$($aliasCase.mode)' with '$($aliasCase.expected)'."
				}
				Write-Host "Expected rejection (schema-v3 $($aliasCase.mode) role alias): $($aliasValidation -join ' ')"
			}
			$null = & python $measurementFixtureGeneratorPath `
				$qualityPath $caseRecordsPath $runnerRoot $audioQualityRoot
			if ($LASTEXITCODE -ne 0) {
				throw 'Unable to restore the valid schema-v3 role fixture after negative checks.'
			}
			$null = & python $qualityValidator $qualityPath --artifact-root $runnerRoot
			if ($LASTEXITCODE -ne 0) {
				throw 'Valid schema-v3 role fixture did not recover after role-alias negative checks.'
			}
			$roleAliasRegressionCovered = $true
		}
		Remove-Item -LiteralPath $caseRecordsPath -Force
		if (-not $artifactRootRegressionCovered) {
			$artifactRootProbe = @'
import json
import importlib.util
import sys
from pathlib import Path

module_root = Path(sys.argv[4])
sys.path.insert(0, str(module_root))
spec = importlib.util.spec_from_file_location('validate_quality_qualification', module_root / 'validate-quality-qualification.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

document = json.loads(Path(sys.argv[2]).read_text(encoding='utf-8'))
root = None if sys.argv[1] == 'missing' else Path(sys.argv[3])
try:
    module.validate_qualification(document, root)
except module.QualificationError:
    raise SystemExit(0)
raise SystemExit(1)
'@
			$wrongArtifactRoot = Join-Path $runnerRoot 'wrong-artifact-root'
			New-Item -ItemType Directory -Force -Path $wrongArtifactRoot | Out-Null
			$null = & python -c $artifactRootProbe missing $qualityPath $wrongArtifactRoot $audioQualityRoot
			if ($LASTEXITCODE -ne 0) {
				throw 'Quality validator accepted a missing artifact root.'
			}
			$null = & python -c $artifactRootProbe wrong $qualityPath $wrongArtifactRoot $audioQualityRoot
			if ($LASTEXITCODE -ne 0) {
				throw 'Quality validator accepted the wrong artifact root.'
			}
			$artifactRootRegressionCovered = $true
		}
		Write-ReleaseJson -Path (Join-Path $runnerRoot 'original-voice-qualification.json') `
			-Value (& $originalEvidence $suite $runnerClass)
		Write-ReleaseJson -Path (Join-Path $runnerRoot 'quality-harness-provenance.json') -Value ([ordered]@{
			schemaVersion = 1
			kind = 'input-enhancement-quality-harness-provenance'
			sourceSha = $sourceSha
			qualityWorkflowRunId = if ($suite -ceq 'nightly') { '123457' } else { '123456' }
			runnerClass = $runnerClass
			harnessFileName = 'protected-quality-harness.ps1'
			harnessSha256 = $qualityHarnessSha256
			hardwareFingerprintSha256 = $hardwareFingerprints[$runnerClass]
		})
		}
	}
	$measuredEvidencePath = Join-Path $tempRoot 'measured-quality-attestation.json'
	$measuredScriptArguments = @{
		SourceRoot = $sourceRoot
		SourceSha = $sourceSha
		TestedBinaryPath = (Join-Path $stageRoot 'mumble.exe')
		LowPerformanceEvidenceRoot = $measuredRunnerRoots['master_quality|low-performance']
		MainstreamEvidenceRoot = $measuredRunnerRoots['master_quality|mainstream']
		LowPerformanceNightlyEvidenceRoot = $measuredRunnerRoots['nightly|low-performance']
		MainstreamNightlyEvidenceRoot = $measuredRunnerRoots['nightly|mainstream']
		UnsignedModelManifestPath = $unsignedModelManifestPath
		UnsignedRecipeManifestPath = $unsignedRecipeManifestPath
		QualityWorkflowRunId = '123456'
		NightlyQualityWorkflowRunId = '123457'
		ExpectedHarnessSha256 = $qualityHarnessSha256
		PythonPath = $fakePythonPath
		OutputPath = $measuredEvidencePath
	}
	& (Join-Path $scriptsRoot 'new-input-enhancement-measured-attestation.ps1') @measuredScriptArguments
	$measuredScriptAttestation = Read-ReleaseJson -Path $measuredEvidencePath
	if ([string]$measuredScriptAttestation.harnessSha256 -cne $qualityHarnessSha256 -or
		@($measuredScriptAttestation.runners + $measuredScriptAttestation.nightlyRunners | Where-Object {
			[string]$_.harnessProvenanceSha256 -cnotmatch '^[0-9a-f]{64}$'
		}).Count -ne 0) {
		throw 'Measured-attestation tool did not bind all master/nightly runners to protected harness provenance.'
	}
	$mergedMeasuredArtifacts = Join-Path $tempRoot 'artifacts'
	New-Item -ItemType Directory -Force -Path $mergedMeasuredArtifacts | Out-Null
	foreach ($runner in @($measuredScriptAttestation.runners + $measuredScriptAttestation.nightlyRunners)) {
		$runnerRoot = $measuredRunnerRoots["$($runner.suite)|$($runner.runnerClass)"]
		Copy-Item -LiteralPath (Join-Path $runnerRoot 'qualification.json') `
			-Destination (Join-Path $tempRoot ([string]$runner.qualityQualification.fileName)) -Force
		Copy-Item -LiteralPath (Join-Path $runnerRoot 'original-voice-qualification.json') `
			-Destination (Join-Path $tempRoot ([string]$runner.originalVoiceQualification.fileName)) -Force
		$artifactNamespace = "$($runner.suite)-$($runner.runnerClass)"
		Copy-Item -LiteralPath (Join-Path (Join-Path $runnerRoot 'artifacts') $artifactNamespace) `
			-Destination (Join-Path $mergedMeasuredArtifacts $artifactNamespace) -Recurse
	}
	$wrongHarnessArguments = $measuredScriptArguments.Clone()
	$wrongHarnessArguments.ExpectedHarnessSha256 = ('37' * 32)
	Assert-Throws -Description 'measured attestation configured harness mismatch' -Script {
		& (Join-Path $scriptsRoot 'new-input-enhancement-measured-attestation.ps1') @wrongHarnessArguments
	}
	$gatePath = Join-Path $tempRoot "test-gates.json"
	Write-ReleaseJson -Path $gatePath -Value ([ordered]@{
		schemaVersion = 1
		passed = $true
		gates = @(
			[ordered]@{ name = "DeepFilterNetCapiTests"; passed = $true; exitCode = 0; durationMs = 1 },
			[ordered]@{ name = "TestInputEnhancement"; passed = $true; exitCode = 0; durationMs = 1 },
			[ordered]@{ name = "TestInputEnhancementAuto"; passed = $true; exitCode = 0; durationMs = 1 },
			[ordered]@{ name = "TestInputEnhancementAutoV2"; passed = $true; exitCode = 0; durationMs = 1 },
			[ordered]@{ name = "TestInputEnhancementCalibration"; passed = $true; exitCode = 0; durationMs = 1 },
			[ordered]@{ name = "TestInputEnhancementCalibrationRuntime"; passed = $true; exitCode = 0; durationMs = 1 },
			[ordered]@{ name = "TestInputEnhancementPolicy"; passed = $true; exitCode = 0; durationMs = 1 },
			[ordered]@{ name = "TestInputEnhancementPolicyConfiguredKey"; passed = $true; exitCode = 0; durationMs = 1 },
			[ordered]@{ name = "TestInputEnhancementPolicyController"; passed = $true; exitCode = 0; durationMs = 1 },
			[ordered]@{ name = "TestInputEnhancementPackageVerifier"; passed = $true; exitCode = 0; durationMs = 1 },
			[ordered]@{ name = "TestInputEnhancementSettings"; passed = $true; exitCode = 0; durationMs = 1 },
			[ordered]@{ name = "TestInputEnhancementCalibrationPlayback"; passed = $true; exitCode = 0; durationMs = 1 },
			[ordered]@{ name = "TestModernDialogControllers"; passed = $true; exitCode = 0; durationMs = 1 },
			[ordered]@{ name = "TestQmlQuickComponents"; passed = $true; exitCode = 0; durationMs = 1 },
			[ordered]@{ name = "TestUpdateHealth"; passed = $true; exitCode = 0; durationMs = 1 },
			[ordered]@{ name = "TestUpdaterHealthIntegration"; passed = $true; exitCode = 0; durationMs = 1 },
			[ordered]@{ name = "TestUpdaterProtocolV4Simulation"; passed = $true; exitCode = 0; durationMs = 1 },
			[ordered]@{ name = "TestSpeechCleanup"; passed = $true; exitCode = 0; durationMs = 1 },
			[ordered]@{ name = "SpeechCleanupBenchmarkSelfTest"; passed = $true; exitCode = 0; durationMs = 1 }
		)
	})
	$missingProtocolGate = Read-ReleaseJson -Path $gatePath
	$missingProtocolGate.gates = @($missingProtocolGate.gates | Where-Object {
		[string]$_.name -cne 'TestUpdaterProtocolV4Simulation'
	})
	Assert-Throws -Description 'missing mandatory updater protocol-v4 simulation gate' -Script {
		Assert-TestGateResults -GateResults $missingProtocolGate
	}
	$signerSubject = "CN=Mumble Input Enhancement Release Test"
	$signingPath = Join-Path $tempRoot "signing-results.json"
	Write-ReleaseJson -Path $signingPath -Value ([ordered]@{
		schemaVersion = 1
		verified = $true
		expectedSignerSubject = $signerSubject
		files = @(
			[ordered]@{
				path = "app/mumble.exe"
				sha256 = Get-ReleaseFileSha256 -Path (Join-Path $stageRoot 'mumble.exe')
				status = "Valid"
				timestamped = $true
				signerSubject = $signerSubject
			},
			[ordered]@{
				path = "installer/$((Get-Item -LiteralPath $installerPath).Name)"
				sha256 = Get-ReleaseFileSha256 -Path $installerPath
				status = "Valid"
				timestamped = $true
				signerSubject = $signerSubject
			}
		)
	})
	$qualificationPath = Join-Path $tempRoot "qualification.json"
	$buildNumber = 42
	$buildId = Get-InputEnhancementBuildId -BuildNumber $buildNumber -SourceSha $sourceSha

	& (Join-Path $scriptsRoot "new-input-enhancement-qualification.ps1") `
		-SourceRoot $sourceRoot `
		-SourceSha $sourceSha `
		-BuildNumber $buildNumber `
		-ArtifactPath $artifactPath `
		-InstallerPath $installerPath `
		-MsiPayloadVerificationPath $msiPayloadVerificationPath `
		-UpdatePackagePath $updatePackagePath `
		-ModelManifestPath $modelManifestPath `
		-RecipeManifestPath $recipeManifestPath `
		-UnsignedModelManifestPath $unsignedModelManifestPath `
		-UnsignedRecipeManifestPath $unsignedRecipeManifestPath `
		-ModelManifestSignaturePath $modelManifestSignaturePath `
		-RecipeManifestSignaturePath $recipeManifestSignaturePath `
		-Ed25519PublicKeyHex $publicKeyHex `
		-TestGateResultsPath $gatePath `
		-SigningResultsPath $signingPath `
		-MeasuredEvidencePath $measuredEvidencePath `
		-OutputPath $qualificationPath

	$assertArguments = @{
		QualificationPath = $qualificationPath
		ArtifactPath = $artifactPath
		InstallerPath = $installerPath
		MsiPayloadVerificationPath = $msiPayloadVerificationPath
		UpdatePackagePath = $updatePackagePath
		ModelManifestPath = $modelManifestPath
		RecipeManifestPath = $recipeManifestPath
		UnsignedModelManifestPath = $unsignedModelManifestPath
		UnsignedRecipeManifestPath = $unsignedRecipeManifestPath
		ModelManifestSignaturePath = $modelManifestSignaturePath
		RecipeManifestSignaturePath = $recipeManifestSignaturePath
		ExpectedEd25519PublicKeyHex = $publicKeyHex
		OpenSslPath = $openSsl
		TestGateResultsPath = $gatePath
		SigningResultsPath = $signingPath
		MeasuredEvidencePath = $measuredEvidencePath
		ExpectedSourceSha = $sourceSha
		ExpectedBuildId = $buildId
		ExpectedSignerSubject = $signerSubject
		DumpbinPath = $dumpbinPassPath
	}
	& (Join-Path $scriptsRoot "assert-input-enhancement-qualification.ps1") @assertArguments

	$originalMeasuredEvidenceBytes = [System.IO.File]::ReadAllBytes($measuredEvidencePath)
	$tamperedMeasuredEvidence = Read-ReleaseJson -Path $measuredEvidencePath
	$tamperedMeasuredEvidence.harnessSha256 = ("37" * 32)
	Write-ReleaseJson -Path $measuredEvidencePath -Value $tamperedMeasuredEvidence
	Assert-Throws -Description "tampered protected quality-harness provenance" -Script {
		& (Join-Path $scriptsRoot "assert-input-enhancement-qualification.ps1") @assertArguments
	}
	[System.IO.File]::WriteAllBytes($measuredEvidencePath, $originalMeasuredEvidenceBytes)

	$originalQualificationBytes = [System.IO.File]::ReadAllBytes($qualificationPath)
	$tamperedQualification = Read-ReleaseJson -Path $qualificationPath
	$tamperedQualification.measuredQuality.harnessSha256 = ("38" * 32)
	Write-ReleaseJson -Path $qualificationPath -Value $tamperedQualification
	Assert-Throws -Description "tampered qualified quality-harness provenance" -Script {
		& (Join-Path $scriptsRoot "assert-input-enhancement-qualification.ps1") @assertArguments
	}
	[System.IO.File]::WriteAllBytes($qualificationPath, $originalQualificationBytes)

	$transportEvidencePath = Join-Path $tempRoot "original-voice-master_quality-low-performance.json"
	$transportEvidence = Read-ReleaseJson -Path $transportEvidencePath
	$transportEvidence.transport_path = "mumble-opus"
	Write-ReleaseJson -Path $transportEvidencePath -Value $transportEvidence
	Assert-Throws -Description "quality/release Original transport contract mismatch" -Script {
		& (Join-Path $scriptsRoot "assert-input-enhancement-qualification.ps1") @assertArguments
	}
	$transportEvidence.transport_path = "client1-opus-server-client2"
	Write-ReleaseJson -Path $transportEvidencePath -Value $transportEvidence

	$expandedArtifactRoot = Join-Path $tempRoot "qualified-expanded"
	& (Join-Path $scriptsRoot 'assert-windows-update-package.ps1') `
		-PackagePath $updatePackagePath -ExpectedCommit $sourceSha -ExpectedBuild $buildNumber `
		-ExpectedVersion '1.7.42' -RequireUpdaterRuntime -ExpandedPayloadPath $expandedArtifactRoot `
		-DumpbinPath $dumpbinPassPath
	$packagedClientPath = Join-Path $expandedArtifactRoot "mumble.exe"
	$artifactHash = Get-ReleaseFileSha256 -Path $updatePackagePath
	$packagedClientHash = Get-ReleaseFileSha256 -Path $packagedClientPath
	$qualificationHash = Get-ReleaseFileSha256 -Path $qualificationPath
	$fixedModelHashes = @{
		Light = ('0' * 64)
		Balanced = Get-ReleaseFileSha256 -Path $modelAsset
		Quality = Get-ReleaseFileSha256 -Path $qualityModelAsset
		VoiceFocus = Get-ReleaseFileSha256 -Path $qualityModelAsset
	}
	$fixedModelIds = @{
		Light = ""
		Balanced = "rnnoise:embedded"
		Quality = "deepfilternet:low-latency"
		VoiceFocus = "deepfilternet:low-latency"
	}
	$fixedRecipeIds = @{
		Light = "input.light.speex"
		Balanced = "input.balanced.rnnoise-embedded"
		Quality = "input.quality.deepfilternet-low-latency"
		VoiceFocus = "input.voice-focus.deepfilternet-low-latency"
	}
	$releaseSmokeHarnessHash = ("13" * 32)
	$releaseSmokeServerHash = ("16" * 32)
	$releaseSmokeFixtureHashes = @{
		"stationary-hvac" = ("17" * 32)
		"transient-keyboard" = ("18" * 32)
		"competing-speech" = ("19" * 32)
	}
	$releaseSmokeFixtureManifestPath = Join-Path $tempRoot 'release-smoke-fixtures.json'
	Write-ReleaseJson -Path $releaseSmokeFixtureManifestPath -Value ([ordered]@{
		schemaVersion = 1
		audioFree = $true
		fixtures = @($releaseSmokeFixtureHashes.Keys | Sort-Object | ForEach-Object {
			[ordered]@{ id = "$_-fixture"; sha256 = $releaseSmokeFixtureHashes[$_] }
		})
	})
	$releaseSmokeFixtureManifestHash = Get-ReleaseFileSha256 -Path $releaseSmokeFixtureManifestPath
	$caseSetControls = New-Object System.Collections.Generic.List[object]
	$caseSetCases = New-Object System.Collections.Generic.List[object]
	foreach ($scene in @("stationary-hvac", "transient-keyboard", "competing-speech")) {
		foreach ($startup in @("cold", "warm")) {
			$preRollMs = if ($startup -ceq "cold") { 0 } else { 300 }
			$caseSetControls.Add([ordered]@{
				id = "$scene-original-$startup"; scene = $scene; startup = $startup
				preRollMs = $preRollMs; fixtureId = "$scene-fixture"
			})
		}
		foreach ($profile in @("Light", "Balanced", "Quality", "VoiceFocus")) {
			foreach ($startup in @("cold", "warm")) {
				$preRollMs = if ($startup -ceq "cold") { 0 } else { 300 }
				$caseSetCases.Add([ordered]@{
					id = "$scene-$($profile.ToLowerInvariant())-$startup"; scene = $scene; profile = $profile
					startup = $startup; preRollMs = $preRollMs; fixtureId = "$scene-fixture"
				})
			}
		}
	}
	$releaseSmokeCaseSetPath = Join-Path $tempRoot 'release-smoke-case-set.json'
	Write-ReleaseJson -Path $releaseSmokeCaseSetPath -Value ([ordered]@{
		schemaVersion = 1
		suite = 'input-enhancement-release-smoke-v1'
		originalControls = $caseSetControls.ToArray()
		cases = $caseSetCases.ToArray()
	})
	$releaseSmokeCaseSetHash = Get-ReleaseFileSha256 -Path $releaseSmokeCaseSetPath
	$releaseSmokeOriginalControls = New-Object System.Collections.Generic.List[object]
	foreach ($scene in @("stationary-hvac", "transient-keyboard", "competing-speech")) {
		foreach ($startup in @("cold", "warm")) {
			$controlId = "$scene-original-$startup"
			$releaseSmokeOriginalControls.Add([ordered]@{
				id = $controlId
				scene = $scene
				startup = $startup
				preRollMs = if ($startup -ceq "cold") { 0 } else { 300 }
				passed = $true
				fixtureId = "$scene-fixture"
				fixtureSha256 = $releaseSmokeFixtureHashes[$scene]
				artifactSha256 = ("ab" * 32)
				fixtureManifestSha256 = $releaseSmokeFixtureManifestHash
				caseSetSha256 = $releaseSmokeCaseSetHash
				senderExecutableSha256 = $packagedClientHash
				receiverExecutableSha256 = $packagedClientHash
				serverExecutableSha256 = $releaseSmokeServerHash
				harnessSha256 = $releaseSmokeHarnessHash
				routeVerified = $true
				encodedOpusPackets = 10
				receivedPcmFrames = 20
				receiverCleanupEnabled = $false
				postDecodeCleanupEnabled = $false
				deadlineMissCount = 0
				fixedTimelinePassed = $true
				timelineAlignment = 'fixed-original-no-correlation'
				onsetLossSamples = 0
				endLossSamples = 0
				missingTailSamples = 0
				receivedClippedSamples = 0
				referenceClippedSamples = 0
			})
		}
	}
	$releaseSmokeCases = New-Object System.Collections.Generic.List[object]
	foreach ($scene in @("stationary-hvac", "transient-keyboard", "competing-speech")) {
		foreach ($profile in @("Light", "Balanced", "Quality", "VoiceFocus")) {
			foreach ($startup in @("cold", "warm")) {
				$releaseSmokeCases.Add([ordered]@{
					id = "$scene-$($profile.ToLowerInvariant())-$startup"
					scene = $scene
					profile = $profile
					startup = $startup
					preRollMs = if ($startup -ceq "cold") { 0 } else { 300 }
					passed = $true
					artifactSha256 = $artifactHash
					fixtureId = "$scene-fixture"
					fixtureSha256 = $releaseSmokeFixtureHashes[$scene]
					fixtureManifestSha256 = $releaseSmokeFixtureManifestHash
					caseSetSha256 = $releaseSmokeCaseSetHash
					harnessSha256 = $releaseSmokeHarnessHash
					serverExecutableSha256 = $releaseSmokeServerHash
					senderExecutableSha256 = $packagedClientHash
					receiverExecutableSha256 = $packagedClientHash
					routeVerified = $true
					encodedOpusPackets = 10
					receivedPcmFrames = 20
					receiverCleanupEnabled = $false
					postDecodeCleanupEnabled = $false
					expectedModelSha256 = $fixedModelHashes[$profile]
					activeModelSha256 = $fixedModelHashes[$profile]
					expectedModelId = $fixedModelIds[$profile]
					activeModelId = $fixedModelIds[$profile]
					expectedRecipeId = $fixedRecipeIds[$profile]
					activeRecipeId = $fixedRecipeIds[$profile]
					expectedRecipeRevision = 1
					activeRecipeRevision = 1
					fallbackCount = 0
					modelHashMismatchCount = 0
					invalidOutputCount = 0
					tailErrorCount = 0
					latencyErrorCount = 0
					tailDrainExpectedFrames = if ($profile -ceq 'Light') { 1 } else { 3 }
					tailDrainActualFrames = if ($profile -ceq 'Light') { 1 } else { 3 }
					enhancementLatencyMs = if ($profile -ceq 'Light') { 10.0 } elseif ($profile -ceq "Balanced") { 30.0 } else { 40.0 }
					fixedTimelinePassed = $true
					onsetLossSamples = 0
					endLossSamples = 0
					originalControlId = "$scene-original-$startup"
					originalControlArtifactSha256 = (("ab" * 32) -join "")
					timelineAlignment = 'fixed-paired-original-onset'
					missingTailSamples = 0
					receivedClippedSamples = 0
					referenceClippedSamples = 0
					callbackP99Ms = if ($profile -cin @('Light', 'Balanced')) { 4.0 } else { 7.0 }
					workerP99Ms = if ($profile -cin @('Quality', 'VoiceFocus')) { 7.0 } else { 0.0 }
					workerRtf = if ($profile -cin @('Quality', 'VoiceFocus')) { 0.30 } else { 0.0 }
					workerPendingFrames = 0
					deadlineMissCount = 0
				})
			}
		}
	}
	$releaseSmokePath = Join-Path $tempRoot "release-smoke.json"
	Write-ReleaseJson -Path $releaseSmokePath -Value ([ordered]@{
		schemaVersion = 1
		suite = "input-enhancement-release-smoke-v1"
		passed = $true
		audioFree = $true
		sourceSha = $sourceSha
		buildId = $buildId
		createdAtUtc = "2026-07-14T12:00:00.0000000+00:00"
		artifact = [ordered]@{
			fileName = (Get-Item -LiteralPath $updatePackagePath).Name
			sha256 = $artifactHash
			size = [int64](Get-Item -LiteralPath $updatePackagePath).Length
		}
		qualification = [ordered]@{
			fileName = (Get-Item -LiteralPath $qualificationPath).Name
			sha256 = $qualificationHash
		}
		packagedClient = [ordered]@{
			relativePath = "mumble.exe"
			sha256 = $packagedClientHash
		}
		provenance = [ordered]@{
			harnessSha256 = $releaseSmokeHarnessHash
			fixtureManifestSha256 = $releaseSmokeFixtureManifestHash
			caseSetSha256 = $releaseSmokeCaseSetHash
			serverExecutableSha256 = $releaseSmokeServerHash
		}
		topology = [ordered]@{
			senderClient = "packaged-client-1"
			serverHost = "127.0.0.1"
			transport = "client1-opus-server-client2"
			receiverClient = "packaged-client-2"
			voiceProtocolModified = $false
			receiverCleanupEnabled = $false
			postDecodeCleanupEnabled = $false
		}
		originalControls = $releaseSmokeOriginalControls.ToArray()
		cases = $releaseSmokeCases.ToArray()
	})
	$releaseSmokeArguments = @{
		ReleaseSmokePath = $releaseSmokePath
		QualificationPath = $qualificationPath
		ModelManifestPath = $modelManifestPath
		RecipeManifestPath = $recipeManifestPath
		ModelManifestSignaturePath = $modelManifestSignaturePath
		RecipeManifestSignaturePath = $recipeManifestSignaturePath
		ArtifactPath = $updatePackagePath
		ExpandedArtifactRoot = $expandedArtifactRoot
		ExpectedSourceSha = $sourceSha
		ExpectedBuildId = $buildId
		ExpectedHarnessSha256 = $releaseSmokeHarnessHash
		FixtureManifestPath = $releaseSmokeFixtureManifestPath
		CaseSetPath = $releaseSmokeCaseSetPath
		ExpectedFixtureManifestSha256 = $releaseSmokeFixtureManifestHash
		ExpectedCaseSetSha256 = $releaseSmokeCaseSetHash
		ExpectedServerExecutableSha256 = $releaseSmokeServerHash
	}
	& (Join-Path $scriptsRoot "assert-input-enhancement-release-smoke.ps1") @releaseSmokeArguments

	$packagedModelSignaturePath = Join-Path $expandedArtifactRoot 'input-models.json.sig'
	$packagedModelSignatureBytes = [IO.File]::ReadAllBytes($packagedModelSignaturePath)
	$tamperedPackagedSignatureBytes = [byte[]]$packagedModelSignatureBytes.Clone()
	$tamperedPackagedSignatureBytes[0] = $tamperedPackagedSignatureBytes[0] -bxor 1
	[IO.File]::WriteAllBytes($packagedModelSignaturePath, $tamperedPackagedSignatureBytes)
	try {
		Assert-Throws -Description 'update-internal detached signature differs from qualified external signature' -Script {
			& (Join-Path $scriptsRoot 'assert-input-enhancement-package-manifest-binding.ps1') `
				-ExpandedPayloadRoot $expandedArtifactRoot `
				-ModelManifestPath $modelManifestPath `
				-RecipeManifestPath $recipeManifestPath `
				-ModelManifestSignaturePath $modelManifestSignaturePath `
				-RecipeManifestSignaturePath $recipeManifestSignaturePath
		}
	} finally {
		[IO.File]::WriteAllBytes($packagedModelSignaturePath, $packagedModelSignatureBytes)
	}

	$badSmoke = Read-ReleaseJson -Path $releaseSmokePath
	$badSmoke.cases[0].fallbackCount = 1
	$badSmokePath = Join-Path $tempRoot "release-smoke-fallback.json"
	Write-ReleaseJson -Path $badSmokePath -Value $badSmoke
	Assert-Throws -Description "release smoke fallback" -Script {
		$badArguments = $releaseSmokeArguments.Clone()
		$badArguments.ReleaseSmokePath = $badSmokePath
		& (Join-Path $scriptsRoot "assert-input-enhancement-release-smoke.ps1") @badArguments
	}

	$badSmoke = Read-ReleaseJson -Path $releaseSmokePath
	$badSmoke.cases[0].activeModelSha256 = (("ef" * 32) -join "")
	$badSmokePath = Join-Path $tempRoot "release-smoke-model-hash.json"
	Write-ReleaseJson -Path $badSmokePath -Value $badSmoke
	Assert-Throws -Description "release smoke active model hash mismatch" -Script {
		$badArguments = $releaseSmokeArguments.Clone()
		$badArguments.ReleaseSmokePath = $badSmokePath
		& (Join-Path $scriptsRoot "assert-input-enhancement-release-smoke.ps1") @badArguments
	}

	$badSmoke = Read-ReleaseJson -Path $releaseSmokePath
	$badSmoke.cases[0].tailDrainActualFrames = 2
	$badSmokePath = Join-Path $tempRoot "release-smoke-tail.json"
	Write-ReleaseJson -Path $badSmokePath -Value $badSmoke
	Assert-Throws -Description "release smoke tail mismatch" -Script {
		$badArguments = $releaseSmokeArguments.Clone()
		$badArguments.ReleaseSmokePath = $badSmokePath
		& (Join-Path $scriptsRoot "assert-input-enhancement-release-smoke.ps1") @badArguments
	}

	$badSmoke = Read-ReleaseJson -Path $releaseSmokePath
	$badSmoke.cases[0].enhancementLatencyMs = 30.1
	$badSmokePath = Join-Path $tempRoot "release-smoke-latency.json"
	Write-ReleaseJson -Path $badSmokePath -Value $badSmoke
	Assert-Throws -Description "release smoke latency budget" -Script {
		$badArguments = $releaseSmokeArguments.Clone()
		$badArguments.ReleaseSmokePath = $badSmokePath
		& (Join-Path $scriptsRoot "assert-input-enhancement-release-smoke.ps1") @badArguments
	}

	$badSmoke = Read-ReleaseJson -Path $releaseSmokePath
	$badSmoke.cases[0].onsetLossSamples = 481
	$badSmokePath = Join-Path $tempRoot "release-smoke-fixed-timeline-onset.json"
	Write-ReleaseJson -Path $badSmokePath -Value $badSmoke
	Assert-Throws -Description "release smoke fixed-timeline onset loss" -Script {
		$badArguments = $releaseSmokeArguments.Clone()
		$badArguments.ReleaseSmokePath = $badSmokePath
		& (Join-Path $scriptsRoot "assert-input-enhancement-release-smoke.ps1") @badArguments
	}

	$badSmoke = Read-ReleaseJson -Path $releaseSmokePath
	$badSmoke.cases[2].originalControlArtifactSha256 = (("cd" * 32) -join "")
	$badSmokePath = Join-Path $tempRoot "release-smoke-unpaired-original-baseline.json"
	Write-ReleaseJson -Path $badSmokePath -Value $badSmoke
	Assert-Throws -Description "release smoke mismatched paired Original baseline" -Script {
		$badArguments = $releaseSmokeArguments.Clone()
		$badArguments.ReleaseSmokePath = $badSmokePath
		& (Join-Path $scriptsRoot "assert-input-enhancement-release-smoke.ps1") @badArguments
	}

	$badArguments = $releaseSmokeArguments.Clone()
	$badArguments.ExpectedHarnessSha256 = ("20" * 32)
	Assert-Throws -Description "release smoke protected harness provenance mismatch" -Script {
		& (Join-Path $scriptsRoot "assert-input-enhancement-release-smoke.ps1") @badArguments
	}

	$badArguments = $releaseSmokeArguments.Clone()
	$badArguments.ExpectedFixtureManifestSha256 = ("21" * 32)
	Assert-Throws -Description "release smoke fixture manifest provenance mismatch" -Script {
		& (Join-Path $scriptsRoot "assert-input-enhancement-release-smoke.ps1") @badArguments
	}

	$badArguments = $releaseSmokeArguments.Clone()
	$badArguments.ExpectedCaseSetSha256 = ("22" * 32)
	Assert-Throws -Description "release smoke fixed case-set provenance mismatch" -Script {
		& (Join-Path $scriptsRoot "assert-input-enhancement-release-smoke.ps1") @badArguments
	}

	$badArguments = $releaseSmokeArguments.Clone()
	$badArguments.ExpectedServerExecutableSha256 = ("23" * 32)
	Assert-Throws -Description "release smoke OG server binary provenance mismatch" -Script {
		& (Join-Path $scriptsRoot "assert-input-enhancement-release-smoke.ps1") @badArguments
	}

	$badSmoke = Read-ReleaseJson -Path $releaseSmokePath
	$badSmoke.cases[0].fixtureSha256 = ("24" * 32)
	$badSmokePath = Join-Path $tempRoot "release-smoke-original-control-fixture.json"
	Write-ReleaseJson -Path $badSmokePath -Value $badSmoke
	Assert-Throws -Description "release smoke enhanced fixture differs from paired Original control" -Script {
		$badArguments = $releaseSmokeArguments.Clone()
		$badArguments.ReleaseSmokePath = $badSmokePath
		& (Join-Path $scriptsRoot "assert-input-enhancement-release-smoke.ps1") @badArguments
	}

	$badSmoke = Read-ReleaseJson -Path $releaseSmokePath
	$badSmoke.cases[0].serverExecutableSha256 = ("25" * 32)
	$badSmokePath = Join-Path $tempRoot "release-smoke-case-server-provenance.json"
	Write-ReleaseJson -Path $badSmokePath -Value $badSmoke
	Assert-Throws -Description "release smoke case OG server provenance mismatch" -Script {
		$badArguments = $releaseSmokeArguments.Clone()
		$badArguments.ReleaseSmokePath = $badSmokePath
		& (Join-Path $scriptsRoot "assert-input-enhancement-release-smoke.ps1") @badArguments
	}

	$badSmoke = Read-ReleaseJson -Path $releaseSmokePath
	$badSmoke.cases[0].originalControlId = "stationary-hvac-original-warm"
	$badSmokePath = Join-Path $tempRoot "release-smoke-wrong-original-control.json"
	Write-ReleaseJson -Path $badSmokePath -Value $badSmoke
	Assert-Throws -Description "release smoke wrong paired Original control" -Script {
		$badArguments = $releaseSmokeArguments.Clone()
		$badArguments.ReleaseSmokePath = $badSmokePath
		& (Join-Path $scriptsRoot "assert-input-enhancement-release-smoke.ps1") @badArguments
	}

	$badSmoke = Read-ReleaseJson -Path $releaseSmokePath
	$badSmoke.cases[0].deadlineMissCount = 1
	$badSmokePath = Join-Path $tempRoot "release-smoke-deadline-miss.json"
	Write-ReleaseJson -Path $badSmokePath -Value $badSmoke
	Assert-Throws -Description "release smoke enhanced deadline miss" -Script {
		$badArguments = $releaseSmokeArguments.Clone()
		$badArguments.ReleaseSmokePath = $badSmokePath
		& (Join-Path $scriptsRoot "assert-input-enhancement-release-smoke.ps1") @badArguments
	}

	$badSmoke = Read-ReleaseJson -Path $releaseSmokePath
	$badSmoke.originalControls[0].deadlineMissCount = 1
	$badSmokePath = Join-Path $tempRoot "release-smoke-original-control-deadline-miss.json"
	Write-ReleaseJson -Path $badSmokePath -Value $badSmoke
	Assert-Throws -Description "release smoke Original-control deadline miss" -Script {
		$badArguments = $releaseSmokeArguments.Clone()
		$badArguments.ReleaseSmokePath = $badSmokePath
		& (Join-Path $scriptsRoot "assert-input-enhancement-release-smoke.ps1") @badArguments
	}

	$badSmoke = Read-ReleaseJson -Path $releaseSmokePath
	$badSmoke.originalControls[0].onsetLossSamples = 481
	$badSmokePath = Join-Path $tempRoot "release-smoke-original-control-onset-loss.json"
	Write-ReleaseJson -Path $badSmokePath -Value $badSmoke
	Assert-Throws -Description "release smoke Original-control onset loss" -Script {
		$badArguments = $releaseSmokeArguments.Clone()
		$badArguments.ReleaseSmokePath = $badSmokePath
		& (Join-Path $scriptsRoot "assert-input-enhancement-release-smoke.ps1") @badArguments
	}

	$badSmoke = Read-ReleaseJson -Path $releaseSmokePath
	$badSmoke.cases[0].callbackP99Ms = 5.1
	$badSmokePath = Join-Path $tempRoot "release-smoke-callback-p99.json"
	Write-ReleaseJson -Path $badSmokePath -Value $badSmoke
	Assert-Throws -Description "release smoke callback p99 budget" -Script {
		$badArguments = $releaseSmokeArguments.Clone()
		$badArguments.ReleaseSmokePath = $badSmokePath
		& (Join-Path $scriptsRoot "assert-input-enhancement-release-smoke.ps1") @badArguments
	}

	$badSmoke = Read-ReleaseJson -Path $releaseSmokePath
	$badSmoke.cases = @($badSmoke.cases | Select-Object -Skip 1)
	$badSmokePath = Join-Path $tempRoot "release-smoke-missing-case.json"
	Write-ReleaseJson -Path $badSmokePath -Value $badSmoke
	Assert-Throws -Description "release smoke missing fixed case" -Script {
		$badArguments = $releaseSmokeArguments.Clone()
		$badArguments.ReleaseSmokePath = $badSmokePath
		& (Join-Path $scriptsRoot "assert-input-enhancement-release-smoke.ps1") @badArguments
	}

	$badSmoke = Read-ReleaseJson -Path $releaseSmokePath
	$badSmoke.topology.receiverCleanupEnabled = $true
	$badSmokePath = Join-Path $tempRoot "release-smoke-receiver-cleanup.json"
	Write-ReleaseJson -Path $badSmokePath -Value $badSmoke
	Assert-Throws -Description "release smoke receiver cleanup enabled" -Script {
		$badArguments = $releaseSmokeArguments.Clone()
		$badArguments.ReleaseSmokePath = $badSmokePath
		& (Join-Path $scriptsRoot "assert-input-enhancement-release-smoke.ps1") @badArguments
	}

	$badSmoke = Read-ReleaseJson -Path $releaseSmokePath
	$badSmoke | Add-Member -NotePropertyName "rawAudioPath" -NotePropertyValue "forbidden.wav"
	$badSmokePath = Join-Path $tempRoot "release-smoke-audio-bearing.json"
	Write-ReleaseJson -Path $badSmokePath -Value $badSmoke
	Assert-Throws -Description "release smoke audio-bearing evidence" -Script {
		$badArguments = $releaseSmokeArguments.Clone()
		$badArguments.ReleaseSmokePath = $badSmokePath
		& (Join-Path $scriptsRoot "assert-input-enhancement-release-smoke.ps1") @badArguments
	}

	$originalArtifactBytes = [System.IO.File]::ReadAllBytes($artifactPath)
	$tamperedArtifactBytes = [byte[]]$originalArtifactBytes.Clone()
	$tamperedArtifactBytes[$tamperedArtifactBytes.Length - 1] = $tamperedArtifactBytes[$tamperedArtifactBytes.Length - 1] -bxor 1
	[System.IO.File]::WriteAllBytes($artifactPath, $tamperedArtifactBytes)
	Assert-Throws -Description "tampered artifact hash" -Script {
		& (Join-Path $scriptsRoot "assert-input-enhancement-qualification.ps1") @assertArguments
	}
	[System.IO.File]::WriteAllBytes($artifactPath, $originalArtifactBytes)
	Assert-Throws -Description "mismatched immutable build ID" -Script {
		$wrongArguments = $assertArguments.Clone()
		$wrongArguments.ExpectedBuildId = "mumble-forked-build-43-$($sourceSha.Substring(0, 12))"
		& (Join-Path $scriptsRoot "assert-input-enhancement-qualification.ps1") @wrongArguments
	}

	$previousPointerPath = Join-Path $tempRoot "previous-pointer.json"
	$previousTag = "mumble-forked-build-41-000000000000"
	$olderTag = "mumble-forked-build-40-111111111111"
	$oldestTag = "mumble-forked-build-39-222222222222"
	Write-ReleaseJson -Path $previousPointerPath -Value ([ordered]@{
		schemaVersion = 2
		channel = "preview"
		immutableTag = $previousTag
		installer = [ordered]@{
			fileName = 'mumble-forked-1.7.41.msi'
			sha256 = ('1' * 64)
			size = 4100
			executableSha256 = ('4' * 64)
			url = "https://github.com/example/mumble/releases/download/$previousTag/mumble-forked-1.7.41.msi"
		}
		recoveryInstallers = @(
			[ordered]@{
				immutableTag = $olderTag
				fileName = 'mumble-forked-1.7.40.msi'
				sha256 = ('2' * 64)
				size = 4000
				url = "https://github.com/example/mumble/releases/download/$olderTag/mumble-forked-1.7.40.msi"
			},
			[ordered]@{
				immutableTag = $oldestTag
				fileName = 'mumble-forked-1.7.39.msi'
				sha256 = ('3' * 64)
				size = 3900
				url = "https://github.com/example/mumble/releases/download/$oldestTag/mumble-forked-1.7.39.msi"
			}
		)
		knownGoodTags = @($previousTag, $olderTag, $oldestTag)
	})
	$previousPointerSignaturePath = "$previousPointerPath.sig"
	Protect-FileWithEd25519 -InputPath $previousPointerPath -SignaturePath $previousPointerSignaturePath `
		-PrivateKeyBase64 $privateKeyBase64 -ExpectedPublicKeyHex $publicKeyHex -OpenSslPath $openSsl
	$policyPath = Join-Path $tempRoot 'input-enhancement-policy.json'
	$policyExpiration = [DateTimeOffset]::UtcNow.AddHours(1)
	$policyExpiration = [DateTimeOffset]::new(
		$policyExpiration.Year, $policyExpiration.Month, $policyExpiration.Day,
		$policyExpiration.Hour, $policyExpiration.Minute, $policyExpiration.Second,
		[TimeSpan]::Zero)
	& (Join-Path $scriptsRoot 'new-signed-input-enhancement-policy.ps1') `
		-Available $true -ForceOriginal $false -RecommendedProfile Balanced `
		-RecipeSetVersion 'self-test-r1' -MinBuild $buildNumber `
		-ExpiresAtUtc $policyExpiration -PrivateKeyBase64 $privateKeyBase64 `
		-ExpectedPublicKeyHex $publicKeyHex -OutputPath $policyPath -OpenSslPath $openSsl
	$expiredPolicyTime = [DateTimeOffset]::UtcNow
	$expiredPolicyTime = [DateTimeOffset]::new(
		$expiredPolicyTime.Year, $expiredPolicyTime.Month, $expiredPolicyTime.Day,
		$expiredPolicyTime.Hour, $expiredPolicyTime.Minute, $expiredPolicyTime.Second,
		[TimeSpan]::Zero).AddSeconds(-1)
	Assert-Throws -Description 'expired channel policy' -Script {
		& (Join-Path $scriptsRoot 'new-signed-input-enhancement-policy.ps1') `
			-Available $true -ForceOriginal $false -RecommendedProfile Balanced `
			-RecipeSetVersion 'self-test-r1' -MinBuild $buildNumber `
			-ExpiresAtUtc $expiredPolicyTime `
			-PrivateKeyBase64 $privateKeyBase64 -ExpectedPublicKeyHex $publicKeyHex `
			-OutputPath (Join-Path $tempRoot 'expired-policy.json') -OpenSslPath $openSsl
	}
	$channelPointerPath = Join-Path $tempRoot "channel-pointer.json"
	& (Join-Path $scriptsRoot "new-input-enhancement-channel-pointer.ps1") `
		-Channel preview `
		-Repository "example/mumble" `
		-QualificationPath $qualificationPath `
		-ReleaseSmokePath $releaseSmokePath `
		-Announcement "Self-test preview" `
		-PreviousPointerPath $previousPointerPath `
		-PreviousPointerSignaturePath $previousPointerSignaturePath `
		-PrivateKeyBase64 $privateKeyBase64 `
		-ExpectedPublicKeyHex $publicKeyHex `
		-PolicyPath $policyPath `
		-PolicySignaturePath "$policyPath.sig" `
		-OpenSslPath $openSsl `
		-OutputPath $channelPointerPath
	& (Join-Path $scriptsRoot 'assert-input-enhancement-detached-signature.ps1') `
		-InputPath $channelPointerPath -SignaturePath "$channelPointerPath.sig" `
		-PublicKeyHex $publicKeyHex -OpenSslPath $openSsl
	$channelPointer = Read-ReleaseJson -Path $channelPointerPath
	if ([string]$channelPointer.artifact.fileName -cne (Get-Item -LiteralPath $updatePackagePath).Name -or
		[string]$channelPointer.artifact.sha256 -cne (Get-ReleaseFileSha256 -Path $updatePackagePath) -or
		[int64]$channelPointer.artifact.size -ne [int64](Get-Item -LiteralPath $updatePackagePath).Length) {
		throw "Signed channel pointer did not target the exact qualified .mumble-update package."
	}
	if ([string]$channelPointer.releaseSmoke.sha256 -cne (Get-ReleaseFileSha256 -Path $releaseSmokePath)) {
		throw "Signed channel pointer did not attest the exact release-smoke evidence."
	}
	if ([int]$channelPointer.schemaVersion -ne 2 -or
		@($channelPointer.knownGoodTags).Count -ne 3 -or
		[string]$channelPointer.knownGoodTags[0] -cne $buildId -or
		[string]$channelPointer.knownGoodTags[1] -cne $previousTag -or
		[string]$channelPointer.knownGoodTags[2] -cne $olderTag -or
		@($channelPointer.recoveryInstallers).Count -ne 2) {
		throw "Channel pointer did not retain the candidate plus two previous recovery MSI releases."
	}
	if ([string]$channelPointer.installer.fileName -cne (Get-Item -LiteralPath $installerPath).Name -or
		[string]$channelPointer.installer.sha256 -cne (Get-ReleaseFileSha256 -Path $installerPath) -or
		[string]$channelPointer.installer.executableSha256 -cne $testedBinarySha256) {
		throw "Channel pointer did not attest the exact candidate MSI."
	}
	if ([string]$channelPointer.inputEnhancementPolicy.sha256 -cne (Get-ReleaseFileSha256 -Path $policyPath) -or
		[string]$channelPointer.inputEnhancementPolicy.signatureSha256 -cne (Get-ReleaseFileSha256 -Path "$policyPath.sig")) {
		throw "Signed channel pointer did not attest the exact signed policy files."
	}
	if ($channelPointer.PSObject.Properties['signedVerified']) {
		throw "Channel pointer still contains the obsolete non-cryptographic signedVerified claim."
	}
	$bootstrapRecoveryPath = Join-Path $tempRoot 'bootstrap-recovery-set.json'
	Write-ReleaseJson -Path $bootstrapRecoveryPath -Value ([ordered]@{
		schemaVersion = 1
		recoveryInstallers = @(
			[ordered]@{ immutableTag = $previousTag; fileName = 'mumble-forked-1.7.41.msi'; sha256 = ('1' * 64); size = 4100 },
			[ordered]@{ immutableTag = $olderTag; fileName = 'mumble-forked-1.7.40.msi'; sha256 = ('2' * 64); size = 4000 }
		)
	})
	$bootstrapPointerPath = Join-Path $tempRoot 'bootstrap-channel-pointer.json'
	& (Join-Path $scriptsRoot 'new-input-enhancement-channel-pointer.ps1') `
		-Channel stable -Repository 'example/mumble' `
		-QualificationPath $qualificationPath -ReleaseSmokePath $releaseSmokePath `
		-Announcement 'Schema v1 to v2 bootstrap' -BootstrapRecoverySetPath $bootstrapRecoveryPath `
		-PrivateKeyBase64 $privateKeyBase64 -ExpectedPublicKeyHex $publicKeyHex `
		-PolicyPath $policyPath -PolicySignaturePath "$policyPath.sig" `
		-OpenSslPath $openSsl -OutputPath $bootstrapPointerPath
	$bootstrapPointer = Read-ReleaseJson -Path $bootstrapPointerPath
	if (@($bootstrapPointer.recoveryInstallers).Count -ne 2 -or
		[string]$bootstrapPointer.recoveryInstallers[0].immutableTag -cne $previousTag -or
		[string]$bootstrapPointer.recoveryInstallers[1].immutableTag -cne $olderTag) {
		throw 'Explicit v1-to-v2 bootstrap did not preserve both hash-attested recovery MSI inputs.'
	}
	$incompleteBootstrap = Read-ReleaseJson -Path $bootstrapRecoveryPath
	$incompleteBootstrap.recoveryInstallers = @($incompleteBootstrap.recoveryInstallers | Select-Object -First 1)
	$incompleteBootstrapPath = Join-Path $tempRoot 'incomplete-bootstrap-recovery-set.json'
	Write-ReleaseJson -Path $incompleteBootstrapPath -Value $incompleteBootstrap
	Assert-Throws -Description 'incomplete v1-to-v2 recovery bootstrap' -Script {
		& (Join-Path $scriptsRoot 'new-input-enhancement-channel-pointer.ps1') `
			-Channel stable -Repository 'example/mumble' `
			-QualificationPath $qualificationPath -ReleaseSmokePath $releaseSmokePath `
			-Announcement 'Must fail' -BootstrapRecoverySetPath $incompleteBootstrapPath `
			-PrivateKeyBase64 $privateKeyBase64 -ExpectedPublicKeyHex $publicKeyHex `
			-PolicyPath $policyPath -PolicySignaturePath "$policyPath.sig" `
			-OpenSslPath $openSsl -OutputPath (Join-Path $tempRoot 'incomplete-bootstrap-pointer.json')
	}
	$expectedPolicyBytes = '{"available":true,"expiresAt":"' +
		$policyExpiration.ToString("yyyy-MM-dd'T'HH:mm:ss'Z'", [Globalization.CultureInfo]::InvariantCulture) +
		'","forceOriginal":false,"minBuild":42,"recipeSetVersion":"self-test-r1","recommendedProfile":"Balanced"}'
	if ([IO.File]::ReadAllText($policyPath, [Text.Encoding]::UTF8) -cne $expectedPolicyBytes) {
		throw "Signed policy generator did not emit the strict six-field canonical byte layout."
	}
	& (Join-Path $scriptsRoot 'assert-input-enhancement-detached-signature.ps1') `
		-InputPath $policyPath -SignaturePath "$policyPath.sig" `
		-PublicKeyHex $publicKeyHex -OpenSslPath $openSsl
	[IO.File]::AppendAllText($policyPath, "`n", [Text.UTF8Encoding]::new($false))
	Assert-Throws -Description 'policy exact-byte tampering' -Script {
		& (Join-Path $scriptsRoot 'assert-input-enhancement-detached-signature.ps1') `
			-InputPath $policyPath -SignaturePath "$policyPath.sig" `
			-PublicKeyHex $publicKeyHex -OpenSslPath $openSsl
	}
	$unsignedQualificationPath = Join-Path $tempRoot "unsigned-qualification.json"
	$unsignedQualification = Read-ReleaseJson -Path $qualificationPath
	$unsignedQualification.artifact.signed = $false
	Write-ReleaseJson -Path $unsignedQualificationPath -Value $unsignedQualification
	Assert-Throws -Description "unsigned channel promotion" -Script {
		& (Join-Path $scriptsRoot "new-input-enhancement-channel-pointer.ps1") `
			-Channel stable -Repository "example/mumble" `
			-QualificationPath $unsignedQualificationPath `
			-ReleaseSmokePath $releaseSmokePath `
			-Announcement "Must fail" `
			-PrivateKeyBase64 $privateKeyBase64 -ExpectedPublicKeyHex $publicKeyHex `
			-PolicyPath $policyPath -PolicySignaturePath "$policyPath.sig" `
			-OpenSslPath $openSsl `
			-OutputPath (Join-Path $tempRoot "unsigned-pointer.json")
	}

	[System.IO.File]::WriteAllText((Join-Path $sourceRoot "source.txt"), "dirty`n")
	Assert-Throws -Description "dirty tracked source" -Script {
		& (Join-Path $scriptsRoot "new-input-enhancement-qualification.ps1") `
			-SourceRoot $sourceRoot -SourceSha $sourceSha -BuildNumber $buildNumber `
			-ArtifactPath $artifactPath -InstallerPath $installerPath -UpdatePackagePath $updatePackagePath `
			-MsiPayloadVerificationPath $msiPayloadVerificationPath `
			-ModelManifestPath $modelManifestPath `
			-RecipeManifestPath $recipeManifestPath `
			-UnsignedModelManifestPath $unsignedModelManifestPath `
			-UnsignedRecipeManifestPath $unsignedRecipeManifestPath `
			-ModelManifestSignaturePath $modelManifestSignaturePath `
			-RecipeManifestSignaturePath $recipeManifestSignaturePath `
			-Ed25519PublicKeyHex $publicKeyHex -OpenSslPath $openSsl `
			-TestGateResultsPath $gatePath `
			-SigningResultsPath $signingPath -MeasuredEvidencePath $measuredEvidencePath `
			-OutputPath (Join-Path $tempRoot "dirty-qualification.json")
	}

	$validSigningConfig = @{
		AzureClientId = "11111111-1111-1111-1111-111111111111"
		AzureTenantId = "22222222-2222-2222-2222-222222222222"
		AzureSubscriptionId = "33333333-3333-3333-3333-333333333333"
		Endpoint = "https://neu.codesigning.azure.net/"
		SigningAccountName = "mumble-signing"
		CertificateProfileName = "public-release"
		ExpectedSignerSubject = $signerSubject
		Ed25519PrivateKeyBase64 = $privateKeyBase64
		Ed25519PublicKeyHex = $publicKeyHex
		OpenSslPath = $openSsl
	}
	& (Join-Path $scriptsRoot "assert-input-enhancement-signing-config.ps1") @validSigningConfig
	Assert-Throws -Description "mismatched Ed25519 private/public release key" -Script {
		$badKeyConfig = $validSigningConfig.Clone()
		$badKeyConfig.Ed25519PublicKeyHex = ('00' * 32)
		& (Join-Path $scriptsRoot "assert-input-enhancement-signing-config.ps1") @badKeyConfig
	}
	Assert-Throws -Description "missing fail-closed signing subject" -Script {
		$missingConfig = $validSigningConfig.Clone()
		$missingConfig.ExpectedSignerSubject = ""
		& (Join-Path $scriptsRoot "assert-input-enhancement-signing-config.ps1") @missingConfig
	}

	$repositoryRoot = (Resolve-Path (Join-Path $scriptsRoot '..\..')).Path
	Assert-Throws -Description 'release CMake configuration without embedded Ed25519 public key' -Script {
		Invoke-NativeChecked -Command 'cmake' -Arguments @(
			'-S', $repositoryRoot,
			'-B', (Join-Path $tempRoot 'cmake-missing-policy-key'),
			'-Dinput-enhancement-signed-policy-required=ON'
		)
	}
	Assert-Throws -Description 'release CMake configuration with malformed Ed25519 public key' -Script {
		Invoke-NativeChecked -Command 'cmake' -Arguments @(
			'-S', $repositoryRoot,
			'-B', (Join-Path $tempRoot 'cmake-invalid-policy-key'),
			'-DMUMBLE_INPUT_ENHANCEMENT_POLICY_PUBLIC_KEY_HEX=not-a-key'
		)
	}

	if ($env:OS -eq "Windows_NT") {
		$fakeBuildRoot = Join-Path $tempRoot "fake-build"
		New-Item -ItemType Directory -Force -Path $fakeBuildRoot | Out-Null
		[System.IO.File]::WriteAllLines(
			(Join-Path $fakeBuildRoot "CMakeCache.txt"),
			@("tests:BOOL=ON", "benchmarks:BOOL=ON", "speech-cleanup-e2e:BOOL=ON"),
			[System.Text.UTF8Encoding]::new($false)
		)
		$fakeCTest = Join-Path $tempRoot "fake-ctest.cmd"
		[System.IO.File]::WriteAllLines(
			$fakeCTest,
			@(
				"@echo off",
				"echo %* | findstr /C:`"--show-only=json-v1`" >nul",
				"if %errorlevel%==0 (",
				"  echo {`"kind`":`"ctestInfo`",`"version`":{`"major`":1,`"minor`":0},`"tests`":[{`"name`":`"DeepFilterNetCapiTests`"},{`"name`":`"TestInputEnhancement`"},{`"name`":`"TestInputEnhancementAuto`"},{`"name`":`"TestInputEnhancementAutoV2`"},{`"name`":`"TestInputEnhancementCalibration`"},{`"name`":`"TestInputEnhancementCalibrationRuntime`"},{`"name`":`"TestInputEnhancementPolicy`"},{`"name`":`"TestInputEnhancementPolicyConfiguredKey`"},{`"name`":`"TestInputEnhancementPolicyController`"},{`"name`":`"TestInputEnhancementPackageVerifier`"},{`"name`":`"TestInputEnhancementSettings`"},{`"name`":`"TestInputEnhancementCalibrationPlayback`"},{`"name`":`"TestModernDialogControllers`"},{`"name`":`"TestQmlQuickComponents`"},{`"name`":`"TestUpdateHealth`"},{`"name`":`"TestUpdaterHealthIntegration`"},{`"name`":`"TestUpdaterProtocolV4Simulation`"},{`"name`":`"TestSpeechCleanup`"},{`"name`":`"SpeechCleanupBenchmarkSelfTest`"}]}",
				"  exit /b 0",
				")",
				"exit /b 0"
			),
			[System.Text.UTF8Encoding]::new($false)
		)
		$fakeGatePath = Join-Path $tempRoot "fake-test-gates.json"
		& (Join-Path $scriptsRoot "invoke-input-enhancement-release-tests.ps1") `
			-BuildRoot $fakeBuildRoot `
			-CTestPath $fakeCTest `
			-OutputPath $fakeGatePath
		Assert-TestGateResults -GateResults (Read-ReleaseJson -Path $fakeGatePath)
		[System.IO.File]::WriteAllLines(
			(Join-Path $fakeBuildRoot "CMakeCache.txt"),
			@("tests:BOOL=ON", "benchmarks:BOOL=OFF", "speech-cleanup-e2e:BOOL=ON"),
			[System.Text.UTF8Encoding]::new($false)
		)
		Assert-Throws -Description "release build with benchmarks disabled" -Script {
			& (Join-Path $scriptsRoot "invoke-input-enhancement-release-tests.ps1") `
				-BuildRoot $fakeBuildRoot `
				-CTestPath $fakeCTest `
				-OutputPath (Join-Path $tempRoot "must-not-pass-gates.json")
		}
		[System.IO.File]::WriteAllLines(
			(Join-Path $fakeBuildRoot "CMakeCache.txt"),
			@("tests:BOOL=ON", "benchmarks:BOOL=ON", "speech-cleanup-e2e:BOOL=OFF"),
			[System.Text.UTF8Encoding]::new($false)
		)
		Assert-Throws -Description "release build without the exact-binary E2E backend" -Script {
			& (Join-Path $scriptsRoot "invoke-input-enhancement-release-tests.ps1") `
				-BuildRoot $fakeBuildRoot `
				-CTestPath $fakeCTest `
				-OutputPath (Join-Path $tempRoot "must-not-pass-without-e2e.json")
		}
	}

	if ($env:OS -eq "Windows_NT") {
		$unsignedRoot = Join-Path $tempRoot "unsigned"
		New-Item -ItemType Directory -Force -Path $unsignedRoot | Out-Null
		[System.IO.File]::WriteAllText((Join-Path $unsignedRoot "mumble.exe"), "not signed")
		Assert-Throws -Description "unsigned stable candidate" -Script {
			& (Join-Path $scriptsRoot "assert-input-enhancement-signatures.ps1") `
				-Root $unsignedRoot `
				-ExpectedSignerSubject $signerSubject `
				-OutputPath (Join-Path $tempRoot "unsigned-results.json")
		}
	}

	Write-Host "All input-enhancement release tooling self-tests passed."
} finally {
	Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
}
