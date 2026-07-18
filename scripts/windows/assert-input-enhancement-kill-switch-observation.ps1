[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)] [string]$RuntimeTracePath,
	[Parameter(Mandatory = $true)] [string]$ReceiptPath,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$ExpectedReceiptSha256,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{40}$')] [string]$ExpectedSourceSha,
	[Parameter(Mandatory = $true)] [ValidatePattern('^mumble-forked-build-[1-9][0-9]*-[0-9a-f]{12}$')] [string]$ExpectedBuildId,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$ExpectedChallengeId,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$ExpectedObserverSha256,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$')] [string]$ExpectedObserverIdentity,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$ExpectedObserverPublicKeyHex,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$ExpectedTestedBinarySha256,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$ExpectedStagedPayloadSha256,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$ExpectedPolicySha256,
	[string]$OpenSslPath = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Import-Module (Join-Path $PSScriptRoot 'InputEnhancementReleaseTools.psm1') -Force

function Assert-ExactProperties([object]$Object, [string[]]$Names, [string]$Context) {
	$actual = @($Object.PSObject.Properties.Name | Sort-Object)
	if (@(Compare-Object -ReferenceObject @($Names | Sort-Object) -DifferenceObject $actual).Count -ne 0) {
		throw "$Context has missing or unexpected properties."
	}
}

function Read-StrictTimestamp([object]$Value, [string]$Context) {
	$result = [datetimeoffset]::MinValue
	if (-not [datetimeoffset]::TryParse([string]$Value, [Globalization.CultureInfo]::InvariantCulture,
		[Globalization.DateTimeStyles]::RoundtripKind, [ref]$result)) {
		throw "$Context is invalid."
	}
	return $result
}

function Assert-ObserverAttestation([object]$Receipt) {
	$clientStartedAtUtc = ([datetimeoffset]$Receipt.clientProcess.startedAtUtc).ToUniversalTime().ToString("yyyy-MM-dd'T'HH:mm:ss'Z'")
	$clientEndedAtUtc = ([datetimeoffset]$Receipt.clientProcess.endedAtUtc).ToUniversalTime().ToString("yyyy-MM-dd'T'HH:mm:ss'Z'")
	$payload = [ordered]@{
		schemaVersion = [int]$Receipt.schemaVersion; kind = [string]$Receipt.kind; passed = [bool]$Receipt.passed
		audioFree = [bool]$Receipt.audioFree; sourceSha = [string]$Receipt.sourceSha; buildId = [string]$Receipt.buildId
		challengeId = [string]$Receipt.challengeId; observationNonce = [string]$Receipt.observationNonce
		observerSha256 = [string]$Receipt.observerSha256; observerIdentity = [string]$Receipt.observerIdentity
		observerPublicKeyHex = [string]$Receipt.observerPublicKeyHex
		runtimeTraceSha256 = [string]$Receipt.runtimeTraceSha256; testedBinarySha256 = [string]$Receipt.testedBinarySha256
		stagedPayloadSha256 = [string]$Receipt.stagedPayloadSha256; policySha256 = [string]$Receipt.policySha256
		clientProcess = [ordered]@{
			pid = [int64]$Receipt.clientProcess.pid; executableSha256 = [string]$Receipt.clientProcess.executableSha256
			startedAtUtc = $clientStartedAtUtc; endedAtUtc = $clientEndedAtUtc
		}
	}
	$payloadText = $payload | ConvertTo-Json -Depth 5 -Compress
	$sha = [Security.Cryptography.SHA256]::HashData([Text.UTF8Encoding]::new($false).GetBytes($payloadText))
	$payloadSha256 = ([BitConverter]::ToString($sha)).Replace('-', '').ToLowerInvariant()
	if ([string]$Receipt.attestation.algorithm -cne 'Ed25519' -or
		[string]$Receipt.attestation.payloadSha256 -cne $payloadSha256) {
		throw 'Kill-switch observer attestation payload binding is invalid.'
	}
	try { [byte[]]$signature = [Convert]::FromBase64String([string]$Receipt.attestation.signatureBase64) } catch {
		throw 'Kill-switch observer attestation signature is not valid base64.'
	}
	if ($signature.Length -ne 64) { throw 'Kill-switch observer attestation signature must be 64 bytes.' }
	$tempRoot = Join-Path ([IO.Path]::GetTempPath()) ('mumble-kill-observer-attestation-' + [guid]::NewGuid().ToString('N'))
	New-Item -ItemType Directory -Path $tempRoot -ErrorAction Stop | Out-Null
	try {
		$payloadPath = Join-Path $tempRoot 'payload.json'; $signaturePath = Join-Path $tempRoot 'signature.raw'
		[IO.File]::WriteAllText($payloadPath, $payloadText, [Text.UTF8Encoding]::new($false))
		[IO.File]::WriteAllBytes($signaturePath, $signature)
		if (-not (Test-Ed25519DetachedSignature -InputPath $payloadPath -SignaturePath $signaturePath `
			-PublicKeyHex $ExpectedObserverPublicKeyHex -OpenSslPath $OpenSslPath)) {
			throw 'Kill-switch observer-held signature did not verify.'
		}
	} finally { Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue }
}

if ((Get-ReleaseFileSha256 -Path $ReceiptPath) -cne $ExpectedReceiptSha256) {
	throw 'Kill-switch observer receipt does not match its protected SHA-256 input.'
}
$traceSha256 = Get-ReleaseFileSha256 -Path $RuntimeTracePath
$trace = Read-ReleaseJson -Path $RuntimeTracePath
$receipt = Read-ReleaseJson -Path $ReceiptPath
Assert-ExactProperties $receipt @(
	'attestation', 'audioFree', 'buildId', 'challengeId', 'clientProcess', 'kind', 'observationNonce', 'observerIdentity',
	'observerPublicKeyHex', 'observerSha256', 'passed',
	'policySha256', 'runtimeTraceSha256', 'schemaVersion', 'sourceSha', 'stagedPayloadSha256',
	'testedBinarySha256'
) 'Kill-switch observer receipt'
Assert-ExactProperties $receipt.attestation @('algorithm', 'payloadSha256', 'signatureBase64') 'Kill-switch observer attestation'
Assert-ExactProperties $receipt.clientProcess @(
	'endedAtUtc', 'executableSha256', 'pid', 'startedAtUtc'
) 'Kill-switch observed client process'
if ([int]$receipt.schemaVersion -ne 3 -or
	[string]$receipt.kind -cne 'input-enhancement-policy-observer-receipt' -or
	$receipt.passed -ne $true -or $receipt.audioFree -ne $true -or
	[string]$receipt.sourceSha -cne $ExpectedSourceSha -or [string]$receipt.buildId -cne $ExpectedBuildId -or
	[string]$receipt.challengeId -cne $ExpectedChallengeId -or
	[string]$receipt.observerSha256 -cne $ExpectedObserverSha256 -or
	[string]$receipt.observerIdentity -cne $ExpectedObserverIdentity -or
	[string]$receipt.observerPublicKeyHex -cne $ExpectedObserverPublicKeyHex -or
	[string]$receipt.runtimeTraceSha256 -cne $traceSha256 -or
	[string]$receipt.testedBinarySha256 -cne $ExpectedTestedBinarySha256 -or
	[string]$receipt.stagedPayloadSha256 -cne $ExpectedStagedPayloadSha256 -or
	[string]$receipt.policySha256 -cne $ExpectedPolicySha256 -or
	[string]$receipt.observationNonce -cnotmatch '^[0-9a-f]{64}$' -or
	[int64]$receipt.clientProcess.pid -le 0 -or
	[string]$receipt.clientProcess.executableSha256 -cne $ExpectedTestedBinarySha256) {
	throw 'Kill-switch observer receipt is invalid or belongs to different client/payload/policy bytes.'
}
Assert-ObserverAttestation -Receipt $receipt
$clientStarted = Read-StrictTimestamp $receipt.clientProcess.startedAtUtc 'Observed client start timestamp'
$clientEnded = Read-StrictTimestamp $receipt.clientProcess.endedAtUtc 'Observed client end timestamp'
if ($clientEnded -lt $clientStarted -or ($clientEnded - $clientStarted).TotalMinutes -gt 20) {
	throw 'Kill-switch observer receipt has an invalid client lifetime.'
}

Assert-ExactProperties $trace @(
	'audioFree', 'buildId', 'challengeId', 'clientProcess', 'events', 'kind', 'observationNonce', 'passed',
	'policySha256', 'schemaVersion', 'sourceSha', 'stagedPayloadSha256', 'startedAtUtc',
	'testedBinarySha256'
) 'Kill-switch runtime trace'
Assert-ExactProperties $trace.clientProcess @(
	'endedAtUtc', 'executableSha256', 'pid', 'startedAtUtc'
) 'Kill-switch trace client process'
if ([int]$trace.schemaVersion -ne 3 -or
	[string]$trace.kind -cne 'input-enhancement-policy-runtime-trace' -or
	$trace.passed -ne $true -or $trace.audioFree -ne $true -or
	[string]$trace.sourceSha -cne $ExpectedSourceSha -or [string]$trace.buildId -cne $ExpectedBuildId -or
	[string]$trace.challengeId -cne $ExpectedChallengeId -or
	[string]$trace.observationNonce -cne [string]$receipt.observationNonce -or
	[string]$trace.testedBinarySha256 -cne $ExpectedTestedBinarySha256 -or
	[string]$trace.stagedPayloadSha256 -cne $ExpectedStagedPayloadSha256 -or
	[string]$trace.policySha256 -cne $ExpectedPolicySha256 -or
	[int64]$trace.clientProcess.pid -ne [int64]$receipt.clientProcess.pid -or
	[string]$trace.clientProcess.executableSha256 -cne [string]$receipt.clientProcess.executableSha256 -or
	[string]$trace.startedAtUtc -cne [string]$receipt.clientProcess.startedAtUtc -or
	[string]$trace.clientProcess.startedAtUtc -cne [string]$receipt.clientProcess.startedAtUtc -or
	[string]$trace.clientProcess.endedAtUtc -cne [string]$receipt.clientProcess.endedAtUtc) {
	throw 'Kill-switch runtime trace is not bound to the independently observed launched client.'
}

Write-Host 'Verified independently observed kill-switch runtime trace and exact launched client binding.'
