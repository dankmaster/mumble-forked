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

function Get-QmlAccessibilityFocusSummary {
	param([Parameter(Mandatory = $true)]$Root)
	$focusedEntries = [Collections.Generic.List[object]]::new()
	function Add-QmlAccessibilityFocusEntry {
		param(
			[Parameter(Mandatory = $true)]$Node,
			[Parameter(Mandatory = $true)][string]$Path
		)
		if (@($Node.states) -contains "focused") {
			$focusedEntries.Add([pscustomobject]@{ node = $Node; path = $Path })
		}
		$childIndex = 0
		foreach ($child in @($Node.children)) {
			if ($null -ne $child) {
				Add-QmlAccessibilityFocusEntry $child "$Path/$childIndex"
			}
			++$childIndex
		}
	}
	Add-QmlAccessibilityFocusEntry $Root "0"
	$entries = @($focusedEntries)
	if ($entries.Count -eq 0) {
		return [pscustomobject]@{ valid = $false; nodes = @(); leaf = $null }
	}
	$leafEntry = @($entries | Sort-Object { ([string]$_.path).Length } -Descending)[0]
	$singleBranch = $true
	foreach ($entry in $entries) {
		if ([string]$leafEntry.path -ne [string]$entry.path -and
			-not ([string]$leafEntry.path).StartsWith("$($entry.path)/", [StringComparison]::Ordinal)) {
			$singleBranch = $false
			break
		}
	}
	return [pscustomobject]@{
		valid = $singleBranch
		nodes = @($entries | ForEach-Object { $_.node })
		leaf = $leafEntry.node
	}
}

function Assert-QmlAccessibilityEvidence {
	param(
		[Parameter(Mandatory = $true)]$Snapshot,
		[Parameter(Mandatory = $true)][string]$CaseId,
		[Parameter(Mandatory = $true)][string]$State,
		[string]$MotdVariant = "none",
		[string]$RichPreviewVariant = "none",
		[string]$RichPreviewTitle = "",
		[string]$RichPreviewOpenLabel = "",
		[string]$RichPreviewEmbedProvider = "",
		[string]$RichPreviewPlayName = "",
		[bool]$NavigationOpen = $false
	)
	$nodes = @(Get-QmlAccessibilityNodes $Snapshot)
	$names = @($nodes | ForEach-Object { [string]$_.name })
	$focus = Get-QmlAccessibilityFocusSummary $Snapshot
	if (-not $focus.valid) {
		throw "Case '$CaseId' accessibility tree contains no focus owner or multiple independent focus branches."
	}
	if ([string]::IsNullOrWhiteSpace([string]$focus.leaf.name)) {
		throw "Case '$CaseId' leaf focus owner has no semantic name."
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
	$duplicateListItems = @($nodes | Where-Object {
		([string]$_.role).Equals("ListItem", [StringComparison]::OrdinalIgnoreCase) -and
		-not [string]::IsNullOrWhiteSpace([string]$_.name)
	} | Group-Object {
		$rect = $_.rect
		"$([string]$_.name)|$([int]$rect.x)|$([int]$rect.y)|$([int]$rect.width)|$([int]$rect.height)"
	} | Where-Object { $_.Count -gt 1 })
	if ($duplicateListItems.Count -ne 0) {
		$duplicates = @($duplicateListItems | ForEach-Object { "$($_.Name) x$($_.Count)" }) -join "; "
		throw "Case '$CaseId' exposes duplicate semantic list items: $duplicates."
	}
	$connectedFixtureMessages = @(
		"Welcome to the deterministic visual fixture.",
		"Qt Quick is ready for review."
	)
	if ($NavigationOpen) {
		$backgroundNames = @(
			"Open rooms and participants",
			"Search users and rooms",
			"Application menu",
			"Message Lobby"
		) + $connectedFixtureMessages
		foreach ($name in $backgroundNames) {
			if ($name -in $names) {
				throw "Compact drawer case '$CaseId' exposes background product control '$name' to accessibility."
			}
		}
	} elseif ($State -eq "connected") {
		$expectedMessages = @(
			"Welcome to the deterministic visual fixture.",
			"Qt Quick is ready for review."
		)
		foreach ($message in $expectedMessages) {
			if ($message -notin $names) {
				throw "Connected case '$CaseId' accessibility tree lacks expected fixture message '$message'."
			}
		}
	} else {
		foreach ($message in $connectedFixtureMessages) {
			if ($message -in $names) {
				throw "Non-connected case '$CaseId' exposes stale connected fixture message '$message'."
			}
		}
	}
	$motdPanes = @($nodes | Where-Object {
		[string]$_.name -eq "Server message of the day" -and
		([string]$_.role).Equals("Pane", [StringComparison]::OrdinalIgnoreCase)
	})
	$motdShouldBeVisible = $MotdVariant -in @("expanded", "collapsed", "changed")
	if ($motdShouldBeVisible -and $motdPanes.Count -ne 1) {
		throw "MOTD case '$CaseId' does not expose exactly one welcome pane to accessibility."
	}
	if ($motdShouldBeVisible -and "Hide welcome message" -notin $names) {
		throw "MOTD case '$CaseId' does not expose its hide-message affordance to accessibility."
	}
	if (-not $motdShouldBeVisible -and $motdPanes.Count -ne 0) {
		throw "Case '$CaseId' exposes a welcome pane for MOTD variant '$MotdVariant' while it should be hidden."
	}
	if ($MotdVariant -eq "history-hidden" -and "Show welcome message" -notin $names) {
		throw "History-hidden MOTD case '$CaseId' does not expose its restore affordance to accessibility."
	}
	if ($RichPreviewVariant -ne "none") {
		$matchingPreviewCards = @($nodes | Where-Object {
			([string]$_.role).Equals("Grouping", [StringComparison]::OrdinalIgnoreCase) -and
			([string]$_.name -eq $RichPreviewTitle -or
				([string]$_.name).StartsWith($RichPreviewTitle + ":", [StringComparison]::Ordinal))
		})
		if ([string]::IsNullOrWhiteSpace($RichPreviewTitle) -or $matchingPreviewCards.Count -ne 1) {
			throw "Rich-preview case '$CaseId' does not expose exactly one '$RichPreviewTitle' grouping."
		}
		$matchingPreviewLinks = @($nodes | Where-Object {
			([string]$_.role).Equals("Link", [StringComparison]::OrdinalIgnoreCase) -and
			[string]$_.name -eq ($RichPreviewOpenLabel + ": " + $RichPreviewTitle)
		})
		if ($RichPreviewVariant -eq "loading" -and $matchingPreviewLinks.Count -ne 0) {
			throw "Loading rich-preview case '$CaseId' exposes an open link before hydration completes."
		}
		if ($RichPreviewVariant -ne "loading" -and
			([string]::IsNullOrWhiteSpace($RichPreviewOpenLabel) -or $matchingPreviewLinks.Count -ne 1)) {
			throw "Rich-preview case '$CaseId' does not expose exactly one explicit open link."
		}
		if (-not [string]::IsNullOrWhiteSpace($RichPreviewEmbedProvider)) {
			$matchingPlayActions = @($nodes | Where-Object {
				([string]$_.role).Equals("Button", [StringComparison]::OrdinalIgnoreCase) -and
				[string]$_.name -eq $RichPreviewPlayName
			})
			if ([string]::IsNullOrWhiteSpace($RichPreviewPlayName) -or $matchingPlayActions.Count -ne 1) {
				throw "Rich-preview case '$CaseId' does not expose exactly one provider play action."
			}
		}
		$expectedProviderHeading = switch ($RichPreviewVariant) {
			"steam" { "Store details"; break }
			"google" { "Google Search"; break }
			"twitch" { "Stream details"; break }
			"flashback" { "Discussion details"; break }
			default { "" }
		}
		if (-not [string]::IsNullOrWhiteSpace($expectedProviderHeading)) {
			$matchingProviderGroups = @($nodes | Where-Object {
				([string]$_.role).Equals("Grouping", [StringComparison]::OrdinalIgnoreCase) -and
				[string]$_.name -eq $expectedProviderHeading
			})
			if ($matchingProviderGroups.Count -ne 1) {
				throw "Rich-preview case '$CaseId' does not expose exactly one '$expectedProviderHeading' provider-details grouping."
			}
		}
	}
}

$matrix = Get-Content -Raw -LiteralPath (Resolve-Path -LiteralPath $MatrixPath).Path | ConvertFrom-Json
if ([int]$matrix.schema_version -ne 1 -or @($matrix.cases).Count -eq 0) { throw "Invalid or empty visual gate matrix." }
$baseline = $null
if (-not $CandidateOnly) {
	$baseline = Get-Content -Raw -LiteralPath (Resolve-Path -LiteralPath $BaselineManifestPath).Path | ConvertFrom-Json
	Assert-QmlVisualManifestMatchesMatrix -Manifest $baseline -MatrixPath $MatrixPath | Out-Null
}

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
if (-not ($capabilities.PSObject.Properties.Name -contains "supported_motd_variants")) {
	throw "Qt Quick visual gate does not advertise MOTD fixture variants. The gate fails closed."
}
$supportedMotdVariants = @($capabilities.supported_motd_variants)
$requiredMotdVariants = @($selectedCases | ForEach-Object {
	if ($_.PSObject.Properties.Name -contains "motd_variant") { [string]$_.motd_variant } else { "none" }
} | Sort-Object -Unique)
foreach ($variant in $requiredMotdVariants) {
	if ($variant -notin $supportedMotdVariants) {
		throw "Required MOTD visual variant '$variant' is not supported. The gate fails closed."
	}
}
if (-not ($capabilities.PSObject.Properties.Name -contains "supported_rich_preview_variants")) {
	throw "Qt Quick visual gate does not advertise rich-preview fixture variants. The gate fails closed."
}
$supportedRichPreviewVariants = @($capabilities.supported_rich_preview_variants)
$requiredRichPreviewVariants = @($selectedCases | ForEach-Object {
	if ($_.PSObject.Properties.Name -contains "rich_preview_variant") { [string]$_.rich_preview_variant } else { "none" }
} | Sort-Object -Unique)
foreach ($variant in $requiredRichPreviewVariants) {
	if ($variant -notin $supportedRichPreviewVariants) {
		throw "Required rich-preview visual variant '$variant' is not supported. The gate fails closed."
	}
}
$requiredStates = @($selectedCases | ForEach-Object { [string]$_.state } | Sort-Object -Unique)
foreach ($state in $requiredStates) {
	if ($state -notin $supportedStates) { throw "Required visual state '$state' is not supported. The gate fails closed." }
}
$output = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $output | Out-Null
$results = [Collections.Generic.List[object]]::new()
foreach ($case in $selectedCases) {
	$navigationOpen = ($case.PSObject.Properties.Name -contains "navigation_open") -and [bool]$case.navigation_open
	$motdVariant = if ($case.PSObject.Properties.Name -contains "motd_variant") {
		[string]$case.motd_variant
	} else { "none" }
	$richPreviewVariant = if ($case.PSObject.Properties.Name -contains "rich_preview_variant") {
		[string]$case.rich_preview_variant
	} else { "none" }
	$richPreviewSize = if ($case.PSObject.Properties.Name -contains "rich_preview_size") {
		[string]$case.rich_preview_size
	} else { "default" }
	if ($motdVariant -ne "none" -and [string]$case.state -ne "connected") {
		throw "Case '$($case.id)' requests MOTD variant '$motdVariant' outside connected state."
	}
	if ($richPreviewVariant -ne "none" -and [string]$case.state -ne "connected") {
		throw "Case '$($case.id)' requests rich-preview variant '$richPreviewVariant' outside connected state."
	}
	$apply = Invoke-Automation @{
		command = "setQmlVisualGateState"; case_id = [string]$case.id; state = [string]$case.state
		theme = [string]$case.theme; layout = [string]$case.layout; width = [int]$case.width
		height = [int]$case.height; motd_variant = $motdVariant; rich_preview_variant = $richPreviewVariant
		rich_preview_size = $richPreviewSize
	}
	$applied = $apply.applied
	foreach ($property in @("case_id", "state", "theme", "layout", "motd_variant", "rich_preview_variant",
		"rich_preview_size", "rich_preview_present", "rich_preview_message_id", "rich_preview_title",
		"rich_preview_open_label", "rich_preview_embed_provider", "rich_preview_embed_aspect",
		"rich_preview_media_count", "rich_preview_has_thumbnail", "width", "height", "message_count",
		"motd_present", "motd_expanded", "motd_changed", "motd_has_user_history", "motd_visible",
		"focus_target", "actual_device_pixel_ratio", "generation")) {
		if (-not ($applied.PSObject.Properties.Name -contains $property)) { throw "Case '$($case.id)' lacks applied '$property'." }
	}
	if ([string]::IsNullOrWhiteSpace([string]$applied.focus_target)) {
		throw "Case '$($case.id)' returned an empty deterministic focus target."
	}
	if ([string]$applied.case_id -ne [string]$case.id -or [string]$applied.state -ne [string]$case.state -or
		[string]$applied.theme -ne [string]$case.theme -or [string]$applied.layout -ne [string]$case.layout -or
		[string]$applied.motd_variant -ne $motdVariant -or
		[string]$applied.rich_preview_variant -ne $richPreviewVariant -or
		[string]$applied.rich_preview_size -ne $richPreviewSize -or
		[int]$applied.width -ne [int]$case.width -or [int]$applied.height -ne [int]$case.height -or
		[Math]::Abs([double]$applied.actual_device_pixel_ratio - $actualDevicePixelRatio) -gt 0.001) {
		$appliedJson = $applied | ConvertTo-Json -Compress -Depth 10
		throw "Automation did not apply visual case '$($case.id)' exactly. Expected $([int]$case.width)x$([int]$case.height) at DPR $actualDevicePixelRatio; applied: $appliedJson"
	}
	$expectedMessageCount = if ([string]$case.state -eq "connected") { 2 } else { 0 }
	if ([int]$applied.message_count -ne $expectedMessageCount) {
		throw "Visual case '$($case.id)' exposed $($applied.message_count) timeline messages; expected $expectedMessageCount."
	}
	$expectMotd = $motdVariant -ne "none"
	$expectMotdExpanded = $expectMotd -and $motdVariant -ne "collapsed"
	$expectMotdChanged = $motdVariant -eq "changed"
	$expectUserHistory = [string]$case.state -eq "connected" -and $motdVariant -in @("none", "history-hidden")
	$expectMotdVisible = $expectMotd -and $motdVariant -ne "history-hidden"
	if ([bool]$applied.motd_present -ne $expectMotd -or
		[bool]$applied.motd_expanded -ne $expectMotdExpanded -or
		[bool]$applied.motd_changed -ne $expectMotdChanged -or
		[bool]$applied.motd_has_user_history -ne $expectUserHistory -or
		[bool]$applied.motd_visible -ne $expectMotdVisible) {
		$appliedJson = $applied | ConvertTo-Json -Compress -Depth 10
		throw "Automation did not establish MOTD variant '$motdVariant' for '$($case.id)': $appliedJson"
	}
	$expectRichPreview = $richPreviewVariant -ne "none"
	$expectedRichPreviewAspect = switch ($richPreviewVariant) {
		"youtube" { "wide" }
		"spotify" { "compact-audio" }
		"tiktok" { "short" }
		"instagram" { "square" }
		"twitch" { "wide" }
		default { "" }
	}
	$expectRichPreviewImage = $richPreviewVariant -in @(
		"youtube", "spotify", "tiktok", "instagram", "audio", "product", "steam", "twitch"
	)
	if ([bool]$applied.rich_preview_present -ne $expectRichPreview -or
		($expectRichPreview -and [string]::IsNullOrWhiteSpace([string]$applied.rich_preview_title)) -or
		($expectRichPreview -and [string]::IsNullOrWhiteSpace([string]$applied.rich_preview_message_id)) -or
		(-not $expectRichPreview -and -not [string]::IsNullOrEmpty([string]$applied.rich_preview_title)) -or
		($expectedRichPreviewAspect.Length -gt 0 -and
			([string]$applied.rich_preview_embed_provider -ne $richPreviewVariant -or
			 [string]$applied.rich_preview_embed_aspect -ne $expectedRichPreviewAspect)) -or
		($expectRichPreviewImage -and
			(-not [bool]$applied.rich_preview_has_thumbnail -or [int]$applied.rich_preview_media_count -lt 1))) {
		$appliedJson = $applied | ConvertTo-Json -Compress -Depth 10
		throw "Automation did not establish rich-preview variant '$richPreviewVariant' for '$($case.id)': $appliedJson"
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

	$richPreviewCardState = $null
	if ($expectRichPreview) {
		$expectedProviderDetails = switch ($richPreviewVariant) {
			"steam" {
				[pscustomobject]@{ variant = "game"; token = "steam"; family = "commerce"; presentation = "commerce" }
				break
			}
			"google" {
				[pscustomobject]@{ variant = "googleSearch"; token = "google"; family = "search"; presentation = "details" }
				break
			}
			"twitch" {
				[pscustomobject]@{ variant = "twitch"; token = "twitch"; family = "social"; presentation = "identity" }
				break
			}
			"flashback" {
				[pscustomobject]@{ variant = "forum"; token = "flashback"; family = "editorial"; presentation = "details" }
				break
			}
			default { $null }
		}
		$previewDeadline = [DateTime]::UtcNow.AddSeconds(8)
		$previewReady = $false
		do {
			$previewResponse = Invoke-Automation @{
				command = "qmlVisualGateRichPreviewState"
				generation = $applied.generation
				messageId = [string]$applied.rich_preview_message_id
			}
			$richPreviewCardState = $previewResponse.card
			$cardInsideTimeline = [bool]$richPreviewCardState.cardVisible -and
				[double]$richPreviewCardState.cardWidth -gt 0 -and [double]$richPreviewCardState.cardHeight -gt 0 -and
				[bool]$richPreviewCardState.timelineVisible -and [double]$richPreviewCardState.timelineHeight -gt 0 -and
				[double]$richPreviewCardState.timelineWidth -gt 0 -and
				[double]$richPreviewCardState.cardX -ge [double]$richPreviewCardState.timelineX - 1 -and
				([double]$richPreviewCardState.cardX + [double]$richPreviewCardState.cardWidth) -le
					([double]$richPreviewCardState.timelineX + [double]$richPreviewCardState.timelineWidth + 1) -and
				[double]$richPreviewCardState.cardY -ge [double]$richPreviewCardState.timelineY - 1 -and
				([double]$richPreviewCardState.cardY + [double]$richPreviewCardState.cardHeight) -le
					([double]$richPreviewCardState.timelineY + [double]$richPreviewCardState.timelineHeight + 1)
			$expectedPreviewState = if ($richPreviewVariant -eq "loading") { "loading" }
				elseif ($richPreviewVariant -eq "error") { "error" } else { "ready" }
			$expectedCompact = $richPreviewSize -eq "compact"
			$expectedExpanded = $richPreviewSize -eq "large"
			$stateReady = [string]$richPreviewCardState.previewState -eq $expectedPreviewState -and
				[bool]$richPreviewCardState.compact -eq $expectedCompact -and
				[bool]$richPreviewCardState.expanded -eq $expectedExpanded -and
				-not [bool]$richPreviewCardState.userExpanded
			$visibleImages = @($richPreviewCardState.visibleImages)
			$expectedImageObjectName = if (-not [string]::IsNullOrWhiteSpace(
				[string]$applied.rich_preview_embed_provider)) { "previewEmbedPoster" } else { "previewCompactImage" }
			$matchingVisibleImages = @($visibleImages | Where-Object {
				[string]$_.objectName -eq $expectedImageObjectName -and [string]$_.statusName -eq "ready" -and
				[bool]$_.effectiveVisible -and [bool]$_.intersectsCard -and
				[double]$_.visibleSceneRect.width -gt 0 -and [double]$_.visibleSceneRect.height -gt 0
			})
			$imageReady = -not $expectRichPreviewImage -or
				([int]$richPreviewCardState.visibleImageCount -eq $visibleImages.Count -and
				 [int]$richPreviewCardState.imageReadyCount -eq $visibleImages.Count -and
				 [int]$richPreviewCardState.imageLoadingCount -eq 0 -and
				 [int]$richPreviewCardState.imageErrorCount -eq 0 -and
				 $matchingVisibleImages.Count -ge 1)
			$mediaInsideCard = [string]::IsNullOrWhiteSpace([string]$applied.rich_preview_embed_provider) -or
				([bool]$richPreviewCardState.mediaVisible -and
				 [double]$richPreviewCardState.mediaWidth -gt 0 -and [double]$richPreviewCardState.mediaHeight -gt 0 -and
				 [double]$richPreviewCardState.mediaX -ge [double]$richPreviewCardState.cardX - 1 -and
				 ([double]$richPreviewCardState.mediaX + [double]$richPreviewCardState.mediaWidth) -le
					([double]$richPreviewCardState.cardX + [double]$richPreviewCardState.cardWidth + 1) -and
				 [double]$richPreviewCardState.mediaY -ge [double]$richPreviewCardState.cardY - 1 -and
				 ([double]$richPreviewCardState.mediaY + [double]$richPreviewCardState.mediaHeight) -le
					([double]$richPreviewCardState.cardY + [double]$richPreviewCardState.cardHeight + 1))
			$embedReady = [string]::IsNullOrWhiteSpace([string]$applied.rich_preview_embed_provider) -or
				($mediaInsideCard -and
				 [bool]$richPreviewCardState.playVisible)
			$openSurfaceReady = if ([string]::IsNullOrWhiteSpace(
				[string]$applied.rich_preview_embed_provider)) {
				[bool]$richPreviewCardState.openSurfaceVisible
			} else {
				-not [bool]$richPreviewCardState.openSurfaceVisible
			}
			$providerDetailsReady = $null -eq $expectedProviderDetails -or
				([bool]$richPreviewCardState.providerDetailsVisible -and
				 [string]$richPreviewCardState.providerVariant -eq [string]$expectedProviderDetails.variant -and
				 [string]$richPreviewCardState.providerToken -eq [string]$expectedProviderDetails.token -and
				 [string]$richPreviewCardState.providerFamily -eq [string]$expectedProviderDetails.family -and
				 [string]$richPreviewCardState.providerPresentation -eq [string]$expectedProviderDetails.presentation)
			$previewReady = [bool]$richPreviewCardState.rendered -and $cardInsideTimeline -and
				$openSurfaceReady -and $stateReady -and $imageReady -and
				$embedReady -and $providerDetailsReady
			if (-not $previewReady) { Start-Sleep -Milliseconds 25 }
		} while (-not $previewReady -and [DateTime]::UtcNow -lt $previewDeadline)
		if (-not $previewReady) {
			$cardJson = $richPreviewCardState | ConvertTo-Json -Compress -Depth 10
			throw "Rich-preview case '$($case.id)' did not render a complete in-viewport card: $cardJson"
		}
	}

	$imagePath = Join-Path $output "$($case.id).png"
	$expectedWidth = [int][Math]::Round([int]$case.width * [double]$case.device_pixel_ratio)
	$expectedHeight = [int][Math]::Round([int]$case.height * [double]$case.device_pixel_ratio)
	$minimumNonBlackFraction = 0.25
	$captureDeadline = [DateTime]::UtcNow.AddSeconds(5)
	$previousImageHash = ""
	$stableFrameSamples = 0
	$dimensions = $null
	$lastCoverage = 0.0
	do {
		$capture = Invoke-Automation @{ command = "captureQml"; path = $imagePath; generation = $applied.generation }
		if ([string]$capture.frontend -ne "qml" -or -not ($capture.PSObject.Properties.Name -contains "generation") -or
			[long]$capture.generation -ne [long]$applied.generation) {
			throw "Case '$($case.id)' returned a stale or non-QML capture."
		}
		if (-not (Test-Path -LiteralPath $imagePath -PathType Leaf)) {
			throw "Case '$($case.id)' produced no capture."
		}
		$dimensions = Get-QmlVisualPngDimensions $imagePath
		if ($dimensions.width -ne $expectedWidth -or $dimensions.height -ne $expectedHeight) {
			throw "Case '$($case.id)' captured $($dimensions.width)x$($dimensions.height), expected ${expectedWidth}x${expectedHeight}."
		}
		$coverage = Get-QmlVisualPngCoverage $imagePath
		$lastCoverage = [double]$coverage.non_black_fraction
		if ($lastCoverage -ge $minimumNonBlackFraction) {
			$currentImageHash = Get-QmlVisualFileSha256 $imagePath
			if ($currentImageHash -ceq $previousImageHash) {
				++$stableFrameSamples
			} else {
				$stableFrameSamples = 0
				$previousImageHash = $currentImageHash
			}
			if ($stableFrameSamples -ge 1) { break }
		} else {
			$stableFrameSamples = 0
			$previousImageHash = ""
		}
		Start-Sleep -Milliseconds 25
	} while ([DateTime]::UtcNow -lt $captureDeadline)
	if ($stableFrameSamples -lt 1) {
		throw "Case '$($case.id)' did not produce two identical non-black frames within five seconds " +
			"(last non-black coverage $([Math]::Round($lastCoverage * 100, 2))%, required $($minimumNonBlackFraction * 100)%)."
	}
	$acceptedImageHash = $previousImageHash

	# Capture stabilization advances queued layout and scenegraph work. Sample
	# accessibility afterwards so its geometry describes the accepted frame,
	# rather than a pre-animation layout that happened to be stable briefly.
	$accessibility = $null
	$accessibilityDeadline = [DateTime]::UtcNow.AddSeconds(2)
	$previousAccessibilityJson = ""
	$stableAccessibilitySamples = 0
	$accessibilityChanges = [Collections.Generic.List[string]]::new()
	$lastFocusedNodeCount = -1
	do {
		$accessibility = Invoke-Automation @{ command = "qmlAccessibilitySnapshot"; generation = $applied.generation }
		if (($accessibility.PSObject.Properties.Name -contains "generation") -and
			[long]$accessibility.generation -eq [long]$applied.generation -and $accessibility.snapshot -and
			-not [string]::IsNullOrWhiteSpace([string]$accessibility.snapshot.role)) {
			$focus = Get-QmlAccessibilityFocusSummary $accessibility.snapshot
			$lastFocusedNodeCount = @($focus.nodes).Count
			$currentAccessibilityJson = $accessibility.snapshot | ConvertTo-Json -Depth 50 -Compress
			if ($accessibilityChanges.Count -eq 0 -or
				$currentAccessibilityJson -cne $accessibilityChanges[$accessibilityChanges.Count - 1]) {
				$accessibilityChanges.Add($currentAccessibilityJson)
				if ($accessibilityChanges.Count -gt 12) {
					$accessibilityChanges.RemoveAt(0)
				}
			}
			if ($focus.valid) {
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
	if (-not ($accessibility.PSObject.Properties.Name -contains "truncated") -or [bool]$accessibility.truncated) {
		throw "Case '$($case.id)' returned a truncated accessibility tree."
	}
	if ($stableAccessibilitySamples -lt 4) {
		$diagnosticsDirectory = Join-Path $output "diagnostics"
		New-Item -ItemType Directory -Force -Path $diagnosticsDirectory | Out-Null
		for ($observationIndex = 0; $observationIndex -lt $accessibilityChanges.Count; ++$observationIndex) {
			$observationPath = Join-Path $diagnosticsDirectory (
				"$($case.id).accessibility-observation-{0:D2}.json" -f ($observationIndex + 1))
			$accessibilityChanges[$observationIndex] | Set-Content -LiteralPath $observationPath -Encoding utf8NoBOM
		}
		throw "Case '$($case.id)' accessibility tree did not stabilize across five scene observations " +
			"(last focused-node count $lastFocusedNodeCount, retained changes $($accessibilityChanges.Count))."
	}
	$accessibilityPath = Join-Path $output "$($case.id).accessibility.json"
	$accessibility.snapshot | ConvertTo-Json -Depth 50 | Set-Content -LiteralPath $accessibilityPath -Encoding utf8NoBOM
	$richPreviewPlayName = if ($expectRichPreview -and $null -ne $richPreviewCardState) {
		[string]$richPreviewCardState.playAccessibilityName
	} else { "" }
	Assert-QmlAccessibilityEvidence -Snapshot $accessibility.snapshot -CaseId ([string]$case.id) `
		-State ([string]$case.state) -MotdVariant $motdVariant -RichPreviewVariant $richPreviewVariant `
		-RichPreviewTitle ([string]$applied.rich_preview_title) `
		-RichPreviewOpenLabel ([string]$applied.rich_preview_open_label) `
		-RichPreviewEmbedProvider ([string]$applied.rich_preview_embed_provider) `
		-RichPreviewPlayName $richPreviewPlayName -NavigationOpen $navigationOpen

	$finalCapture = Invoke-Automation @{ command = "captureQml"; path = $imagePath; generation = $applied.generation }
	if ([string]$finalCapture.frontend -ne "qml" -or
		-not ($finalCapture.PSObject.Properties.Name -contains "generation") -or
		[long]$finalCapture.generation -ne [long]$applied.generation) {
		throw "Case '$($case.id)' returned a stale or non-QML final capture."
	}
	$finalCoverage = Get-QmlVisualPngCoverage $imagePath
	$finalImageHash = Get-QmlVisualFileSha256 $imagePath
	if ([double]$finalCoverage.non_black_fraction -lt $minimumNonBlackFraction -or
		$finalImageHash -cne $acceptedImageHash) {
		throw "Case '$($case.id)' scene changed after accessibility stabilization or produced a partial frame."
	}
	$results.Add([ordered]@{
		id = [string]$case.id; state = [string]$case.state; theme = [string]$case.theme; layout = [string]$case.layout
		navigation_open = $navigationOpen; motd_variant = $motdVariant
		rich_preview_variant = $richPreviewVariant; rich_preview_size = $richPreviewSize
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
