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

function Get-QmlAccessibilityNodes {
	param([Parameter(Mandatory = $true)]$Root)
	$nodes = [Collections.Generic.List[object]]::new()
	function Add-QmlAccessibilityNode {
		param([Parameter(Mandatory = $true)]$Node)
		$nodes.Add($Node)
		foreach ($child in @($Node.children)) {
			if ($null -ne $child) { Add-QmlAccessibilityNode $child }
		}
	}
	Add-QmlAccessibilityNode $Root
	return @($nodes)
}

function Assert-QmlAccessibilityEvidence {
	param(
		[Parameter(Mandatory = $true)]$Snapshot,
		[Parameter(Mandatory = $true)][string]$CaseId,
		[Parameter(Mandatory = $true)][string]$State,
		[bool]$NavigationOpen = $false
	)
	$nodes = @(Get-QmlAccessibilityNodes $Snapshot)
	$focused = @($nodes | Where-Object { @($_.states) -contains "focused" })
	if ($focused.Count -ne 1) {
		throw "Case '$CaseId' accessibility tree contains $($focused.Count) focused nodes; expected exactly one."
	}
	if ([string]::IsNullOrWhiteSpace([string]$focused[0].name)) {
		throw "Case '$CaseId' focused accessibility node has no semantic name."
	}
	$hidden = @($nodes | Where-Object {
		$states = @($_.states)
		($states -contains "invisible") -or ($states -contains "offscreen")
	})
	if ($hidden.Count -ne 0) {
		throw "Case '$CaseId' accessibility tree exposes $($hidden.Count) invisible or offscreen nodes."
	}
	$navigationDialogs = @($nodes | Where-Object {
		[string]$_.name -eq "Rooms and participants" -and
		([string]$_.role).Equals("Dialog", [StringComparison]::OrdinalIgnoreCase)
	})
	if ($NavigationOpen -and $navigationDialogs.Count -ne 1) {
		throw "Case '$CaseId' does not expose exactly one open navigation drawer to accessibility."
	}
	if (-not $NavigationOpen -and $navigationDialogs.Count -ne 0) {
		throw "Case '$CaseId' exposes the navigation drawer while it should be closed."
	}
	if ($State -eq "connected") {
		$expectedMessages = @(
			"Welcome to the deterministic visual fixture.",
			"Qt Quick is ready for review."
		)
		$names = @($nodes | ForEach-Object { [string]$_.name })
		foreach ($message in $expectedMessages) {
			if ($message -notin $names) {
				throw "Connected case '$CaseId' accessibility tree lacks expected fixture message '$message'."
			}
		}
	} else {
		$connectedFixtureMessages = @(
			"Welcome to the deterministic visual fixture.",
			"Qt Quick is ready for review."
		)
		$names = @($nodes | ForEach-Object { [string]$_.name })
		foreach ($message in $connectedFixtureMessages) {
			if ($message -in $names) {
				throw "Non-connected case '$CaseId' exposes stale connected fixture message '$message'."
			}
		}
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
	$navigationOpen = ($case.PSObject.Properties.Name -contains "navigation_open") -and [bool]$case.navigation_open
	$apply = Invoke-Automation @{
		command = "setQmlVisualGateState"; case_id = [string]$case.id; state = [string]$case.state
		theme = [string]$case.theme; layout = [string]$case.layout; width = [int]$case.width
		height = [int]$case.height
	}
	$applied = $apply.applied
	foreach ($property in @("case_id", "state", "theme", "layout", "width", "height", "message_count", "focus_target", "actual_device_pixel_ratio", "generation")) {
		if (-not ($applied.PSObject.Properties.Name -contains $property)) { throw "Case '$($case.id)' lacks applied '$property'." }
	}
	if ([string]::IsNullOrWhiteSpace([string]$applied.focus_target)) {
		throw "Case '$($case.id)' returned an empty deterministic focus target."
	}
	if ([string]$applied.case_id -ne [string]$case.id -or [string]$applied.state -ne [string]$case.state -or
		[string]$applied.theme -ne [string]$case.theme -or [string]$applied.layout -ne [string]$case.layout -or
		[int]$applied.width -ne [int]$case.width -or [int]$applied.height -ne [int]$case.height -or
		[Math]::Abs([double]$applied.actual_device_pixel_ratio - $actualDevicePixelRatio) -gt 0.001) {
		$appliedJson = $applied | ConvertTo-Json -Compress -Depth 10
		throw "Automation did not apply visual case '$($case.id)' exactly. Expected $([int]$case.width)x$([int]$case.height) at DPR $actualDevicePixelRatio; applied: $appliedJson"
	}
	$expectedMessageCount = if ([string]$case.state -eq "connected") { 2 } else { 0 }
	if ([int]$applied.message_count -ne $expectedMessageCount) {
		throw "Visual case '$($case.id)' exposed $($applied.message_count) timeline messages; expected $expectedMessageCount."
	}
	$viewportRequest = @{
		command = "setHostViewport"; width = [int]$case.width; height = [int]$case.height
	}
	if ($case.PSObject.Properties.Name -contains "navigation_open") {
		$viewportRequest.railOpen = $navigationOpen
	}
	$viewport = Invoke-Automation $viewportRequest
	$expectedRailPosition = if ($navigationOpen) { 1.0 } else { 0.0 }
	function Test-NavigationEndpoint {
		param($Viewport)
		return ($Viewport.PSObject.Properties.Name -contains "railPosition") -and
			[bool]$Viewport.railOpen -eq $navigationOpen -and
			[Math]::Abs([double]$Viewport.railPosition - $expectedRailPosition) -le 0.001
	}
	$viewportDeadline = [DateTime]::UtcNow.AddSeconds(2)
	while (-not (Test-NavigationEndpoint $viewport) -and [DateTime]::UtcNow -lt $viewportDeadline) {
		Start-Sleep -Milliseconds 25
		$viewport = Invoke-Automation @{
			command = "setHostViewport"; width = [int]$case.width; height = [int]$case.height
		}
	}
	if ([int]$viewport.width -ne [int]$case.width -or [int]$viewport.height -ne [int]$case.height -or
		-not (Test-NavigationEndpoint $viewport)) {
		$position = if ($viewport.PSObject.Properties.Name -contains "railPosition") {
			[double]$viewport.railPosition
		} else { [double]::NaN }
		throw "Case '$($case.id)' did not reach the requested compact navigation endpoint " +
			"(open=$navigationOpen, position=$expectedRailPosition); observed open=$([bool]$viewport.railOpen), " +
			"position=$position."
	}

	# Focus propagation in Qt Quick is queued behind the fixture update and can
	# land one or two scene turns later under the software renderer. Poll the
	# semantic tree, rather than sleeping for a machine-dependent duration.
	$accessibility = $null
	$accessibilityDeadline = [DateTime]::UtcNow.AddSeconds(2)
	$previousAccessibilityJson = ""
	$stableAccessibilitySamples = 0
	do {
		$accessibility = Invoke-Automation @{ command = "qmlAccessibilitySnapshot"; generation = $applied.generation }
		if (($accessibility.PSObject.Properties.Name -contains "generation") -and
			[long]$accessibility.generation -eq [long]$applied.generation -and $accessibility.snapshot -and
			-not [string]::IsNullOrWhiteSpace([string]$accessibility.snapshot.role)) {
			$focusedNodes = @(Get-QmlAccessibilityNodes $accessibility.snapshot | Where-Object { @($_.states) -contains "focused" })
			if ($focusedNodes.Count -eq 1) {
				$currentAccessibilityJson = $accessibility.snapshot | ConvertTo-Json -Depth 50 -Compress
				if ($currentAccessibilityJson -ceq $previousAccessibilityJson) {
					++$stableAccessibilitySamples
				} else {
					$stableAccessibilitySamples = 0
					$previousAccessibilityJson = $currentAccessibilityJson
				}
				# Require five identical observations across queued scene turns. This
				# prevents asynchronous ListView delegate incubation from producing a
				# timing-dependent accessibility baseline.
				if ($stableAccessibilitySamples -ge 4) { break }
			} else {
				$stableAccessibilitySamples = 0
				$previousAccessibilityJson = ""
			}
		}
		Start-Sleep -Milliseconds 25
	} while ([DateTime]::UtcNow -lt $accessibilityDeadline)
	if (-not $accessibility -or -not $accessibility.snapshot -or
		[string]::IsNullOrWhiteSpace([string]$accessibility.snapshot.role)) {
		throw "Case '$($case.id)' returned no accessibility tree."
	}
	if ($stableAccessibilitySamples -lt 4) {
		throw "Case '$($case.id)' accessibility tree did not stabilize across five scene observations."
	}
	$accessibilityPath = Join-Path $output "$($case.id).accessibility.json"
	$accessibility.snapshot | ConvertTo-Json -Depth 50 | Set-Content -LiteralPath $accessibilityPath -Encoding utf8NoBOM
	Assert-QmlAccessibilityEvidence -Snapshot $accessibility.snapshot -CaseId ([string]$case.id) -State ([string]$case.state) -NavigationOpen $navigationOpen

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
		navigation_open = $navigationOpen
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
	$baselineDirectory = Split-Path -Parent (Resolve-Path -LiteralPath $BaselineManifestPath).Path
	foreach ($case in $results) {
		if (-not $baselineById.ContainsKey([string]$case.id)) { throw "Baseline is missing case '$($case.id)'." }
		$expected = $baselineById[[string]$case.id]
		if ($case.accessibility_sha256 -ne $expected.accessibility_sha256) {
			throw "Accessibility baseline mismatch for '$($case.id)'. Candidate artifacts remain in '$output'."
		}
		if ($case.image_width -ne $expected.image_width -or $case.image_height -ne $expected.image_height) {
			throw "Visual baseline dimensions mismatch for '$($case.id)'. Candidate artifacts remain in '$output'."
		}
		$baselineImagePath = Join-Path $baselineDirectory "$($case.id).png"
		if (-not (Test-Path -LiteralPath $baselineImagePath -PathType Leaf)) {
			throw "Baseline image is missing for '$($case.id)': $baselineImagePath"
		}
		if ((Get-QmlVisualFileSha256 $baselineImagePath) -ne [string]$expected.image_sha256) {
			throw "Baseline image hash does not match its manifest for '$($case.id)'."
		}
		if ($case.image_sha256 -ne $expected.image_sha256) {
			$comparison = Compare-QmlVisualPng -BaselinePath $baselineImagePath -CandidatePath (Join-Path $output "$($case.id).png")
			if (-not $comparison.passed) {
				throw "Visual baseline mismatch for '$($case.id)': $($comparison.changed_pixels)/$($comparison.allowed_changed_pixels) changed pixels, maximum channel delta $($comparison.maximum_channel_delta)/32. Candidate artifacts remain in '$output'."
			}
		}
	}
	Write-Host "Qt Quick visual gate passed $($results.Count) cases at DPR $actualDevicePixelRatio. Manifest: $manifestPath"
} else {
	Write-Warning "Candidate-only capture completed. This is NOT a passing visual gate and no baseline was updated. Manifest: $manifestPath"
}
