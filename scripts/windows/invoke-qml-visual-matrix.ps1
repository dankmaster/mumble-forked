[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)][string]$Executable,
	[Parameter(Mandatory = $true)][string]$ConfigPath,
	[Parameter(Mandatory = $true, ParameterSetName = "Gate")][string]$BaselineManifestPath,
	[Parameter(Mandatory = $true, ParameterSetName = "Candidate")][switch]$CandidateOnly,
	[string]$MatrixPath = "$PSScriptRoot\qml-visual-gate-matrix.json",
	[string]$OutputDirectory = ".tmp\qml-visual-matrix",
	[int]$StartupTimeoutSeconds = 30
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
Import-Module "$PSScriptRoot\QmlVisualGate.Common.psm1" -Force

function Get-FreeTcpPort {
	$listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
	$listener.Start()
	try { return ([Net.IPEndPoint]$listener.LocalEndpoint).Port } finally { $listener.Stop() }
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

$executablePath = (Resolve-Path -LiteralPath $Executable).Path
$sourceConfig = (Resolve-Path -LiteralPath $ConfigPath).Path
$matrixFile = (Resolve-Path -LiteralPath $MatrixPath).Path
$baselineFile = if ($CandidateOnly) { "" } else { (Resolve-Path -LiteralPath $BaselineManifestPath).Path }
$matrix = Get-Content -Raw -LiteralPath $matrixFile | ConvertFrom-Json
$allCases = @($matrix.cases)
$dprGroups = @($allCases | Group-Object { ([double]$_.device_pixel_ratio).ToString('0.###', [Globalization.CultureInfo]::InvariantCulture) })
if ($dprGroups.Count -eq 0) { throw "Visual matrix contains no DPR groups." }
$root = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $root | Out-Null
$saved = @{
	QT_SCALE_FACTOR = $env:QT_SCALE_FACTOR
	QT_QUICK_BACKEND = $env:QT_QUICK_BACKEND
	QSG_RHI_BACKEND = $env:QSG_RHI_BACKEND
	QSG_RENDER_LOOP = $env:QSG_RENDER_LOOP
	MUMBLE_MODERN_AUTOMATION_PORT = $env:MUMBLE_MODERN_AUTOMATION_PORT
	MUMBLE_MODERN_AUTOMATION_TOKEN = $env:MUMBLE_MODERN_AUTOMATION_TOKEN
}
$combinedCases = [Collections.Generic.List[object]]::new()
try {
	foreach ($group in $dprGroups) {
		$dpr = [double]::Parse($group.Name, [Globalization.CultureInfo]::InvariantCulture)
		$groupId = "dpr-$($group.Name.Replace('.', '-'))"
		$groupDirectory = Join-Path $root $groupId
		New-Item -ItemType Directory -Force -Path $groupDirectory | Out-Null
		$configCopy = Join-Path $groupDirectory "mumble_settings.json"
		Copy-Item -LiteralPath $sourceConfig -Destination $configCopy -Force
		$port = Get-FreeTcpPort
		$token = [Guid]::NewGuid().ToString('N')
		$env:QT_SCALE_FACTOR = $dpr.ToString('0.###', [Globalization.CultureInfo]::InvariantCulture)
		# Screenshot gates need deterministic complete frames across Windows GPU/driver
		# combinations. Production and performance runs keep the normal GPU backend.
		$env:QT_QUICK_BACKEND = 'software'
		$env:QSG_RHI_BACKEND = 'software'
		$env:QSG_RENDER_LOOP = 'basic'
		$env:MUMBLE_MODERN_AUTOMATION_PORT = [string]$port
		$env:MUMBLE_MODERN_AUTOMATION_TOKEN = $token
		$process = Start-Process -FilePath $executablePath -ArgumentList @('--multiple', '--config', $configCopy) -PassThru
		try {
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
				Copy-Item -LiteralPath (Join-Path $groupDirectory "$($case.id).png") -Destination $root -Force
				Copy-Item -LiteralPath (Join-Path $groupDirectory "$($case.id).accessibility.json") -Destination $root -Force
				$combinedCases.Add($case)
			}
		} finally {
			if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue; $process.WaitForExit(5000) | Out-Null }
		}
	}
} finally {
	$env:QT_SCALE_FACTOR = $saved.QT_SCALE_FACTOR
	$env:QT_QUICK_BACKEND = $saved.QT_QUICK_BACKEND
	$env:QSG_RHI_BACKEND = $saved.QSG_RHI_BACKEND
	$env:QSG_RENDER_LOOP = $saved.QSG_RENDER_LOOP
	$env:MUMBLE_MODERN_AUTOMATION_PORT = $saved.MUMBLE_MODERN_AUTOMATION_PORT
	$env:MUMBLE_MODERN_AUTOMATION_TOKEN = $saved.MUMBLE_MODERN_AUTOMATION_TOKEN
}

$expectedIds = @($allCases | ForEach-Object { [string]$_.id } | Sort-Object)
$actualIds = @($combinedCases | ForEach-Object { [string]$_.id } | Sort-Object)
if ($expectedIds.Count -ne $actualIds.Count -or (Compare-Object $expectedIds $actualIds)) {
	throw "Per-process DPR runs did not cover the complete visual matrix."
}
$manifest = [ordered]@{
	schema_version = 1; frontend = 'qml'; process_isolation = 'per-dpr'; renderer = 'software'
	mode = if ($CandidateOnly) { 'candidate-only' } else { 'gate' }
	matrix_sha256 = Get-QmlVisualFileSha256 $matrixFile; cases = $combinedCases
}
$manifest | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath (Join-Path $root 'manifest.json') -Encoding utf8NoBOM
Assert-QmlVisualManifest $manifest | Out-Null
if ($CandidateOnly) {
	Write-Warning "Candidate-only matrix completed with $($combinedCases.Count) cases. This is NOT a passing gate and no baseline was updated."
} else {
	Write-Host "Qt Quick visual matrix passed $($combinedCases.Count) cases across $($dprGroups.Count) isolated client processes."
}
