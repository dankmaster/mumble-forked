[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)][string]$Executable,
	[Parameter(Mandatory = $true)][string]$ConfigPath,
	[Parameter(Mandatory = $true, ParameterSetName = "Gate")][string]$BaselineManifestPath,
	[Parameter(Mandatory = $true, ParameterSetName = "Candidate")][switch]$CandidateOnly,
	[string]$MatrixPath = "$PSScriptRoot\qml-visual-gate-matrix.json",
	[string]$OutputDirectory = ".tmp\qml-visual-matrix",
	[int]$StartupTimeoutSeconds = 30,
	[Parameter(ParameterSetName = "Candidate")]
	[ValidateRange(0, [int]::MaxValue)][int]$ShardIndex = 0,
	[Parameter(ParameterSetName = "Candidate")]
	[ValidateRange(1, [int]::MaxValue)][int]$ShardCount = 1,
	[Parameter(ParameterSetName = "Candidate")]
	[ValidateRange(0, 65535)][int]$AutomationPortBase = 0
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
Import-Module "$PSScriptRoot\QmlVisualGate.Common.psm1" -Force
Import-Module "$PSScriptRoot\QmlVisualMatrix.Common.psm1" -Force

function Get-FreeTcpPort {
	$listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
	$listener.Start()
	try { return ([Net.IPEndPoint]$listener.LocalEndpoint).Port } finally { $listener.Stop() }
}

function Enter-AutomationPortReservation {
	param([ValidateRange(1024, 65535)][int]$Port)
	$mutex = [Threading.Mutex]::new($false, "Local\MumbleQmlVisualAutomationPort-$Port")
	$ownsMutex = $false
	try {
		try { $ownsMutex = $mutex.WaitOne(0) }
		catch [Threading.AbandonedMutexException] { $ownsMutex = $true }
		if (-not $ownsMutex) {
			throw "Automation port $Port is already reserved by another visual-matrix runner."
		}

		$listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, $Port)
		$listener.Server.ExclusiveAddressUse = $true
		try { $listener.Start() }
		catch { throw "Automation port $Port is unavailable: $($_.Exception.Message)" }
		finally { $listener.Stop() }
		return $mutex
	} catch {
		if ($ownsMutex) { $mutex.ReleaseMutex() }
		$mutex.Dispose()
		throw
	}
}

function Wait-AutomationPort {
	param([int]$Port, [Diagnostics.Process]$Process, [int]$TimeoutSeconds)
	$deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
	do {
		$Process.Refresh()
		if ($Process.HasExited) { throw "Mumble exited during visual-gate startup with code $($Process.ExitCode)." }
		$client = [Net.Sockets.TcpClient]::new()
		try {
			$pending = $client.BeginConnect("127.0.0.1", $Port, $null, $null)
			if ($pending.AsyncWaitHandle.WaitOne(100)) { $client.EndConnect($pending); return }
		} catch { } finally { $client.Dispose() }
		Start-Sleep -Milliseconds 100
	} while ([DateTime]::UtcNow -lt $deadline)
	throw "Timed out waiting for automation port $Port."
}

$root = [IO.Path]::GetFullPath($OutputDirectory)
$rootParent = Split-Path -Parent $root
$rootLeaf = Split-Path -Leaf $root
if ([string]::IsNullOrWhiteSpace($rootParent) -or [string]::IsNullOrWhiteSpace($rootLeaf) -or
	[string]::Equals($root, [IO.Path]::GetPathRoot($root), [StringComparison]::OrdinalIgnoreCase) -or
	[string]::Equals($root, [IO.Path]::GetFullPath('.'), [StringComparison]::OrdinalIgnoreCase)) {
	throw "Refusing to use unsafe visual matrix output directory '$root'."
}

# A rerun must never inherit an older success manifest or a partially captured
# DPR group. Build beside the requested directory and publish it only after the
# complete manifest has been validated and written last.
New-Item -ItemType Directory -Force -Path $rootParent | Out-Null
if (Test-Path -LiteralPath $root) {
	Remove-Item -LiteralPath $root -Recurse -Force
}
Get-ChildItem -LiteralPath $rootParent -Directory -Filter "$rootLeaf.incomplete-*" -ErrorAction SilentlyContinue |
	Remove-Item -Recurse -Force
Get-ChildItem -LiteralPath $rootParent -Directory -Filter "$rootLeaf.publishing-*" -ErrorAction SilentlyContinue |
	Remove-Item -Recurse -Force
$runId = [Guid]::NewGuid().ToString('N')
$workingRoot = Join-Path $rootParent "$rootLeaf.incomplete-$runId"
$publishRoot = Join-Path $rootParent "$rootLeaf.publishing-$runId"
New-Item -ItemType Directory -Path $workingRoot | Out-Null

$saved = @{
	QT_SCALE_FACTOR = $env:QT_SCALE_FACTOR
	QT_QUICK_BACKEND = $env:QT_QUICK_BACKEND
	QSG_RHI_BACKEND = $env:QSG_RHI_BACKEND
	QSG_RENDER_LOOP = $env:QSG_RENDER_LOOP
	MUMBLE_MODERN_AUTOMATION_PORT = $env:MUMBLE_MODERN_AUTOMATION_PORT
	MUMBLE_MODERN_AUTOMATION_TOKEN = $env:MUMBLE_MODERN_AUTOMATION_TOKEN
}
$combinedCases = [Collections.Generic.List[object]]::new()
$automationPorts = [Collections.Generic.List[int]]::new()
$published = $false
$fixtureStarted = $false
try {
	$executablePath = (Resolve-Path -LiteralPath $Executable).Path
	$sourceConfig = (Resolve-Path -LiteralPath $ConfigPath).Path
	$sourceMatrixFile = (Resolve-Path -LiteralPath $MatrixPath).Path
	$matrixFile = $sourceMatrixFile
	$baselineFile = if ($CandidateOnly) { "" } else { (Resolve-Path -LiteralPath $BaselineManifestPath).Path }
	$matrix = Get-Content -Raw -LiteralPath $sourceMatrixFile | ConvertFrom-Json
	if (-not $CandidateOnly) {
		$baseline = Get-Content -Raw -LiteralPath $baselineFile | ConvertFrom-Json
		Assert-QmlVisualManifestMatchesMatrix -Manifest $baseline -MatrixPath $sourceMatrixFile | Out-Null
	}
	$sourceCases = @($matrix.cases)
	$isShard = $CandidateOnly -and $ShardCount -gt 1
	if ($isShard -and $AutomationPortBase -eq 0) {
		throw "Candidate shards require -AutomationPortBase so parallel runners cannot reuse an ephemeral automation port."
	}
	if ($AutomationPortBase -gt 0 -and $AutomationPortBase -lt 1024) {
		throw "Automation port base must be zero (automatic) or at least 1024."
	}
	$sourceDprKeys = @($sourceCases | ForEach-Object {
		([double]$_.device_pixel_ratio).ToString('0.###', [Globalization.CultureInfo]::InvariantCulture)
	} | Sort-Object -Unique)
	if ($CandidateOnly) {
		$selectedCases = @(Get-QmlVisualMatrixShardCases -Cases $sourceCases `
			-ShardIndex $ShardIndex -ShardCount $ShardCount)
		if ($isShard) {
			$matrix.cases = $selectedCases
			$matrixFile = Join-Path $workingRoot "matrix.shard-$ShardIndex-of-$ShardCount.json"
			$matrix | ConvertTo-Json -Depth 20 |
				Set-Content -LiteralPath $matrixFile -Encoding utf8NoBOM
		}
	}
	$allCases = @($matrix.cases)
	$dprGroups = @($allCases | Group-Object {
		([double]$_.device_pixel_ratio).ToString('0.###', [Globalization.CultureInfo]::InvariantCulture)
	})
	if ($dprGroups.Count -eq 0) { throw "Visual matrix contains no DPR groups." }

	foreach ($group in $dprGroups) {
		$dpr = [double]::Parse($group.Name, [Globalization.CultureInfo]::InvariantCulture)
		$groupId = "dpr-$($group.Name.Replace('.', '-'))"
		$groupDirectory = Join-Path $workingRoot $groupId
		New-Item -ItemType Directory -Force -Path $groupDirectory | Out-Null
		$configCopy = Join-Path $groupDirectory "mumble_settings.json"
		Copy-Item -LiteralPath $sourceConfig -Destination $configCopy -Force
		$sourceDprIndex = [Array]::IndexOf([string[]]$sourceDprKeys, [string]$group.Name)
		if ($sourceDprIndex -lt 0) { throw "Visual matrix DPR group '$($group.Name)' is absent from the source matrix." }
		$port = if ($AutomationPortBase -gt 0) {
			Get-QmlVisualAutomationPort -BasePort $AutomationPortBase -ShardIndex $ShardIndex `
				-SourceDprCount $sourceDprKeys.Count -SourceDprIndex $sourceDprIndex
		} else { Get-FreeTcpPort }
		$automationPorts.Add($port)
		$token = [Guid]::NewGuid().ToString('N')
		$env:QT_SCALE_FACTOR = $dpr.ToString('0.###', [Globalization.CultureInfo]::InvariantCulture)
		# Screenshot gates need deterministic complete frames across Windows GPU/driver
		# combinations. Production and performance runs keep the normal GPU backend.
		$env:QT_QUICK_BACKEND = 'software'
		$env:QSG_RHI_BACKEND = 'software'
		$env:QSG_RENDER_LOOP = 'basic'
		$env:MUMBLE_MODERN_AUTOMATION_PORT = [string]$port
		$env:MUMBLE_MODERN_AUTOMATION_TOKEN = $token
		$process = $null
		$portReservation = $null
		try {
			if ($AutomationPortBase -gt 0) { $portReservation = Enter-AutomationPortReservation -Port $port }
			$process = Start-Process -FilePath $executablePath -ArgumentList @('--multiple', '--config', $configCopy) `
				-RedirectStandardOutput (Join-Path $groupDirectory 'fixture.stdout.log') `
				-RedirectStandardError (Join-Path $groupDirectory 'fixture.stderr.log') -PassThru
			$fixtureStarted = $true
			Wait-AutomationPort -Port $port -Process $process -TimeoutSeconds $StartupTimeoutSeconds
			$workerArguments = @{
				AutomationPort = $port; AutomationToken = $token; MatrixPath = $matrixFile
				OutputDirectory = $groupDirectory; ExpectedDevicePixelRatio = $dpr
			}
			if ($CandidateOnly) { $workerArguments.CandidateOnly = $true }
			else { $workerArguments.BaselineManifestPath = $baselineFile }
			& "$PSScriptRoot\invoke-qml-visual-gate.ps1" @workerArguments
			$groupManifest = Get-Content -Raw -LiteralPath (Join-Path $groupDirectory 'manifest.json') | ConvertFrom-Json
			foreach ($case in @($groupManifest.cases)) {
				Copy-Item -LiteralPath (Join-Path $groupDirectory "$($case.id).png") -Destination $workingRoot -Force
				Copy-Item -LiteralPath (Join-Path $groupDirectory "$($case.id).accessibility.json") -Destination $workingRoot -Force
				$combinedCases.Add($case)
			}
		} finally {
			if ($null -ne $process -and -not $process.HasExited) {
				Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
				$process.WaitForExit(5000) | Out-Null
			}
			if ($null -ne $portReservation) {
				$portReservation.ReleaseMutex()
				$portReservation.Dispose()
			}
		}
	}

	$expectedIds = @($allCases | ForEach-Object { [string]$_.id } | Sort-Object)
	$actualIds = @($combinedCases | ForEach-Object { [string]$_.id } | Sort-Object)
	if ($expectedIds.Count -ne $actualIds.Count -or (Compare-Object $expectedIds $actualIds)) {
		throw "Per-process DPR runs did not cover the complete visual matrix."
	}
	$gitSha = [string]$env:GITHUB_SHA
	if ($gitSha -notmatch '^[0-9a-fA-F]{40}$') {
		$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
		try { $gitSha = [string](& git -C $repositoryRoot rev-parse HEAD 2>$null) }
		catch { $gitSha = '' }
	}
	if ($gitSha -notmatch '^[0-9a-fA-F]{40}$') { $gitSha = 'unknown' }
	else { $gitSha = $gitSha.ToLowerInvariant() }
	$manifest = [ordered]@{
		schema_version = 1
		frontend = 'qml'
		process_isolation = if ($isShard) { 'per-dpr-candidate-shard' } else { 'per-dpr' }
		renderer = 'software'
		mode = if ($CandidateOnly) { 'candidate-only' } else { 'gate' }
		matrix_sha256 = Get-QmlVisualFileSha256 $matrixFile
		executable_sha256 = Get-QmlVisualFileSha256 $executablePath
		source_git_sha = $gitSha
		cases = $combinedCases
	}
	if ($isShard) {
		$manifest['shard_index'] = $ShardIndex
		$manifest['shard_count'] = $ShardCount
		$manifest['source_case_count'] = $sourceCases.Count
		$manifest['source_matrix_sha256'] = Get-QmlVisualFileSha256 $sourceMatrixFile
	}
	if ($AutomationPortBase -gt 0) {
		$manifest['automation_port_base'] = $AutomationPortBase
		$manifest['automation_ports'] = @($automationPorts)
		$manifest['automation_port_dpr_stride'] = $sourceDprKeys.Count
	}
	Assert-QmlVisualManifestMatchesMatrix -Manifest $manifest -MatrixPath $matrixFile | Out-Null
	$manifest | ConvertTo-Json -Depth 20 |
		Set-Content -LiteralPath (Join-Path $workingRoot 'manifest.json') -Encoding utf8NoBOM
	# Runtime profiles, logs and media caches inside the DPR directories can keep
	# short-lived Windows file handles after the fixture process exits. Publish
	# only the reviewed evidence through a clean sibling directory so those
	# implementation files can never make an otherwise successful gate fail.
	New-Item -ItemType Directory -Path $publishRoot | Out-Null
	foreach ($case in $combinedCases) {
		Copy-Item -LiteralPath (Join-Path $workingRoot "$($case.id).png") -Destination $publishRoot
		Copy-Item -LiteralPath (Join-Path $workingRoot "$($case.id).accessibility.json") -Destination $publishRoot
	}
	Copy-Item -LiteralPath (Join-Path $workingRoot 'manifest.json') -Destination $publishRoot
	Move-Item -LiteralPath $publishRoot -Destination $root
	$published = $true

	if ($CandidateOnly) {
		$shardDescription = if ($isShard) { " shard $ShardIndex of $ShardCount" } else { "" }
		Write-Warning "Candidate-only matrix$shardDescription completed with $($combinedCases.Count) cases. This is NOT a passing gate and no baseline was updated."
	} else {
		Write-Host "Qt Quick visual matrix passed $($combinedCases.Count) cases across $($dprGroups.Count) isolated client processes."
	}
} finally {
	$env:QT_SCALE_FACTOR = $saved.QT_SCALE_FACTOR
	$env:QT_QUICK_BACKEND = $saved.QT_QUICK_BACKEND
	$env:QSG_RHI_BACKEND = $saved.QSG_RHI_BACKEND
	$env:QSG_RENDER_LOOP = $saved.QSG_RENDER_LOOP
	$env:MUMBLE_MODERN_AUTOMATION_PORT = $saved.MUMBLE_MODERN_AUTOMATION_PORT
	$env:MUMBLE_MODERN_AUTOMATION_TOKEN = $saved.MUMBLE_MODERN_AUTOMATION_TOKEN
	if ($published -and (Test-Path -LiteralPath $workingRoot)) {
		Remove-Item -LiteralPath $workingRoot -Recurse -Force -ErrorAction SilentlyContinue
	} elseif ($fixtureStarted -and (Test-Path -LiteralPath $workingRoot)) {
		Write-Warning "Incomplete visual-matrix evidence was preserved at '$workingRoot'."
	} elseif (Test-Path -LiteralPath $workingRoot) {
		Remove-Item -LiteralPath $workingRoot -Recurse -Force -ErrorAction SilentlyContinue
	}
	if (Test-Path -LiteralPath $publishRoot) {
		Remove-Item -LiteralPath $publishRoot -Recurse -Force -ErrorAction SilentlyContinue
	}
}
