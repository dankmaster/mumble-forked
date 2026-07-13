[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)][int]$AutomationPort,
	[string]$AutomationToken = "",
	[string]$MatrixPath = "$PSScriptRoot\qml-visual-gate-matrix.json",
	[Parameter(Mandatory = $true, ParameterSetName = "Gate")][string]$BaselineManifestPath,
	[Parameter(Mandatory = $true, ParameterSetName = "Candidate")][switch]$CandidateOnly,
	[string]$OutputDirectory = ".tmp\qml-visual-gate",
	[Parameter(Mandatory = $true)][double]$ExpectedDevicePixelRatio,
	[int]$TimeoutMilliseconds = 10000
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
Import-Module "$PSScriptRoot\QmlVisualGate.Common.psm1" -Force

function Invoke-Automation {
	param([Parameter(Mandatory = $true)][hashtable]$Request)
	$Request.token = $AutomationToken
	$client = [Net.Sockets.TcpClient]::new()
	$pending = $client.BeginConnect("127.0.0.1", $AutomationPort, $null, $null)
	if (-not $pending.AsyncWaitHandle.WaitOne($TimeoutMilliseconds)) {
		$client.Dispose()
		throw "Timed out connecting to automation port $AutomationPort."
	}
	$client.EndConnect($pending)
	try {
		$client.ReceiveTimeout = $TimeoutMilliseconds
		$stream = $client.GetStream()
		$writer = [IO.StreamWriter]::new($stream, [Text.UTF8Encoding]::new($false))
		$reader = [IO.StreamReader]::new($stream, [Text.UTF8Encoding]::new($false))
		try {
			$writer.NewLine = "`n"
			$writer.WriteLine(($Request | ConvertTo-Json -Depth 30 -Compress))
			$writer.Flush()
			$line = $reader.ReadLine()
			if ([string]::IsNullOrWhiteSpace($line)) { throw "Automation returned an empty response." }
			$response = $line | ConvertFrom-Json
			if (-not [bool]$response.ok) {
				$errorText = if ($response.PSObject.Properties.Name -contains "error") { $response.error } else { "unknown error" }
				throw "Automation command '$($Request.command)' failed: $errorText"
			}
			return $response
		} finally { $writer.Dispose(); $reader.Dispose() }
	} finally { $client.Dispose() }
}

function Assert-Capability {
	param($Capabilities, [string]$Name)
	if (-not ($Capabilities.PSObject.Properties.Name -contains $Name) -or -not [bool]$Capabilities.$Name) {
		throw "Qt Quick visual gate capability '$Name' is unavailable. The gate fails closed."
	}
}

$matrix = Get-Content -Raw -LiteralPath (Resolve-Path -LiteralPath $MatrixPath).Path | ConvertFrom-Json
if ([int]$matrix.schema_version -ne 1 -or @($matrix.cases).Count -eq 0) { throw "Invalid or empty visual gate matrix." }

$capabilityResponse = Invoke-Automation @{ command = "qmlVisualGateCapabilities" }
$capabilities = $capabilityResponse.capabilities
foreach ($name in @("capture", "state_injection", "window_resize", "theme_override", "accessibility_snapshot")) {
	Assert-Capability $capabilities $name
}
$actualDevicePixelRatio = [double]$capabilities.actual_device_pixel_ratio
if ($actualDevicePixelRatio -le 0 -or [Math]::Abs($actualDevicePixelRatio - $ExpectedDevicePixelRatio) -gt 0.001) {
	throw "Attached QQuickWindow reports DPR $actualDevicePixelRatio, expected $ExpectedDevicePixelRatio. DPR is process/screen state and is never mutated in attach mode."
}
$selectedCases = @($matrix.cases | Where-Object {
	[Math]::Abs([double]$_.device_pixel_ratio - $actualDevicePixelRatio) -le 0.001
})
if ($selectedCases.Count -eq 0) { throw "The matrix contains no cases for actual DPR $actualDevicePixelRatio." }
$supportedStates = @($capabilities.supported_states)
$requiredStates = @($selectedCases | ForEach-Object { [string]$_.state } | Sort-Object -Unique)
foreach ($state in $requiredStates) {
	if ($state -notin $supportedStates) { throw "Required visual state '$state' is not supported. The gate fails closed." }
}
$baseline = $null
if (-not $CandidateOnly) {
	$baseline = Get-Content -Raw -LiteralPath (Resolve-Path -LiteralPath $BaselineManifestPath).Path | ConvertFrom-Json
	Assert-QmlVisualManifest $baseline | Out-Null
}

$output = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $output | Out-Null
$results = [Collections.Generic.List[object]]::new()
foreach ($case in $selectedCases) {
	$apply = Invoke-Automation @{
		command = "setQmlVisualGateState"; case_id = [string]$case.id; state = [string]$case.state
		theme = [string]$case.theme; layout = [string]$case.layout; width = [int]$case.width
		height = [int]$case.height
	}
	$applied = $apply.applied
	foreach ($property in @("case_id", "state", "theme", "layout", "width", "height", "actual_device_pixel_ratio", "generation")) {
		if (-not ($applied.PSObject.Properties.Name -contains $property)) { throw "Case '$($case.id)' lacks applied '$property'." }
	}
	if ([string]$applied.case_id -ne [string]$case.id -or [string]$applied.state -ne [string]$case.state -or
		[string]$applied.theme -ne [string]$case.theme -or [string]$applied.layout -ne [string]$case.layout -or
		[int]$applied.width -ne [int]$case.width -or [int]$applied.height -ne [int]$case.height -or
		[Math]::Abs([double]$applied.actual_device_pixel_ratio - $actualDevicePixelRatio) -gt 0.001) {
		throw "Automation did not apply visual case '$($case.id)' exactly."
	}

	$accessibility = Invoke-Automation @{ command = "qmlAccessibilitySnapshot"; generation = $applied.generation }
	if (-not ($accessibility.PSObject.Properties.Name -contains "generation") -or
		[long]$accessibility.generation -ne [long]$applied.generation -or -not $accessibility.snapshot -or
		[string]::IsNullOrWhiteSpace([string]$accessibility.snapshot.role)) {
		throw "Case '$($case.id)' returned no accessibility tree."
	}
	$accessibilityPath = Join-Path $output "$($case.id).accessibility.json"
	$accessibility.snapshot | ConvertTo-Json -Depth 50 | Set-Content -LiteralPath $accessibilityPath -Encoding utf8NoBOM

	$imagePath = Join-Path $output "$($case.id).png"
	$capture = Invoke-Automation @{ command = "captureQml"; path = $imagePath; generation = $applied.generation }
	if ([string]$capture.frontend -ne "qml" -or -not ($capture.PSObject.Properties.Name -contains "generation") -or
		[long]$capture.generation -ne [long]$applied.generation) {
		throw "Case '$($case.id)' returned a stale or non-QML capture."
	}
	if (-not (Test-Path -LiteralPath $imagePath -PathType Leaf)) { throw "Case '$($case.id)' produced no capture." }
	$dimensions = Get-QmlVisualPngDimensions $imagePath
	$expectedWidth = [int][Math]::Round([int]$case.width * [double]$case.device_pixel_ratio)
	$expectedHeight = [int][Math]::Round([int]$case.height * [double]$case.device_pixel_ratio)
	if ($dimensions.width -ne $expectedWidth -or $dimensions.height -ne $expectedHeight) {
		throw "Case '$($case.id)' captured $($dimensions.width)x$($dimensions.height), expected ${expectedWidth}x${expectedHeight}."
	}
	$results.Add([ordered]@{
		id = [string]$case.id; state = [string]$case.state; theme = [string]$case.theme; layout = [string]$case.layout
		logical_width = [int]$case.width; logical_height = [int]$case.height; device_pixel_ratio = [double]$case.device_pixel_ratio
		image_width = $dimensions.width; image_height = $dimensions.height
		image_sha256 = Get-QmlVisualFileSha256 $imagePath
		accessibility_sha256 = Get-QmlVisualFileSha256 $accessibilityPath
	})
}

$manifest = [ordered]@{
	schema_version = 1; frontend = "qml"; mode = if ($CandidateOnly) { "candidate-only" } else { "gate" }
	matrix_sha256 = Get-QmlVisualFileSha256 $MatrixPath; cases = $results
}
$manifestPath = Join-Path $output "manifest.json"
$manifest | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $manifestPath -Encoding utf8NoBOM
Assert-QmlVisualManifest $manifest | Out-Null

if (-not $CandidateOnly) {
	$baselineById = @{}; foreach ($case in @($baseline.cases)) { $baselineById[[string]$case.id] = $case }
	foreach ($case in $results) {
		if (-not $baselineById.ContainsKey([string]$case.id)) { throw "Baseline is missing case '$($case.id)'." }
		$expected = $baselineById[[string]$case.id]
		if ($case.image_sha256 -ne $expected.image_sha256 -or $case.accessibility_sha256 -ne $expected.accessibility_sha256 -or
			$case.image_width -ne $expected.image_width -or $case.image_height -ne $expected.image_height) {
			throw "Visual or accessibility baseline mismatch for '$($case.id)'. Candidate artifacts remain in '$output'."
		}
	}
	Write-Host "Qt Quick visual gate passed $($results.Count) cases at DPR $actualDevicePixelRatio. Manifest: $manifestPath"
} else {
	Write-Warning "Candidate-only capture completed. This is NOT a passing visual gate and no baseline was updated. Manifest: $manifestPath"
}
