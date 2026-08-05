[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)][int]$AutomationPort,
	[string]$AutomationToken = "",
	[string]$MatrixPath = "$PSScriptRoot\qml-visual-gate-matrix.json",
	[Parameter(Mandatory = $true, ParameterSetName = "Gate")][string]$BaselineManifestPath,
	[Parameter(Mandatory = $true, ParameterSetName = "Candidate")][switch]$CandidateOnly,
	[string]$OutputDirectory = ".tmp\qml-visual-gate",
	[Parameter(Mandatory = $true)][double]$ExpectedDevicePixelRatio,
	[Parameter(Mandatory = $true)][ValidateRange(1, [int]::MaxValue)][int]$ExpectedProcessId,
	[int]$TimeoutMilliseconds = 30000
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

function Assert-QmlVisualProcessIdentity {
	param(
		[Parameter(Mandatory = $true)]$PingResponse,
		[Parameter(Mandatory = $true)][ValidateRange(1, [int]::MaxValue)][int]$ExpectedProcessId
	)
	if (-not ($PingResponse.PSObject.Properties.Name -contains "pid") -or
		[int]$PingResponse.pid -ne $ExpectedProcessId) {
		$actualPid = if ($PingResponse.PSObject.Properties.Name -contains "pid") {
			[string]$PingResponse.pid
		} else { "missing" }
		throw "Visual worker attached to automation PID $actualPid, expected exact fixture PID $ExpectedProcessId."
	}
	return $true
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

function Get-QmlAccessibilityEntries {
	param([Parameter(Mandatory = $true)]$Root)
	$entries = [Collections.Generic.List[object]]::new()
	function Add-QmlAccessibilityEntry {
		param(
			[Parameter(Mandatory = $true)]$Node,
			[Parameter(Mandatory = $true)][string]$Path,
			[AllowNull()]$Parent,
			[AllowEmptyString()][string]$ParentPath
		)
		$entries.Add([pscustomobject]@{
			node = $Node
			path = $Path
			parent = $Parent
			parent_path = $ParentPath
		})
		$childIndex = 0
		foreach ($child in @($Node.children)) {
			if ($null -ne $child) {
				Add-QmlAccessibilityEntry $child "$Path/$childIndex" $Node $Path
			}
			++$childIndex
		}
	}
	Add-QmlAccessibilityEntry $Root "0" $null ""
	return @($entries)
}

function Test-QmlAccessibilityPathContains {
	param(
		[Parameter(Mandatory = $true)][string]$Path,
		[Parameter(Mandatory = $true)][string]$Candidate
	)
	return $Candidate -eq $Path -or
		$Candidate.StartsWith("$Path/", [StringComparison]::Ordinal)
}

function Test-QmlAccessibilityPositiveRect {
	param([AllowNull()]$Rect)
	return $null -ne $Rect -and [double]$Rect.width -gt 0.0 -and [double]$Rect.height -gt 0.0
}

function Test-QmlTimelineViewportGeometry {
	param(
		[Parameter(Mandatory = $true)]$Timeline,
		[bool]$RequireCustomHeader = $false,
		[bool]$RequireHistoryPrependLayout = $false
	)
	$requiredProperties = @(
		"viewportY", "viewportBottom", "viewportHeight", "composerTop",
		"jumpToLatestVisible", "jumpToLatestTop", "jumpToLatestHeight",
		"jumpToLatestBottom", "jumpToLatestTopMargin", "jumpToLatestBottomMargin",
		"aboveComposerTickerVisible", "aboveComposerTickerTop",
		"aboveComposerTickerHeight", "aboveComposerTickerBottom"
	)
	foreach ($propertyName in $requiredProperties) {
		if (-not ($Timeline.PSObject.Properties.Name -contains $propertyName)) {
			return $false
		}
	}
	$viewportY = [double]$Timeline.viewportY
	$viewportBottom = [double]$Timeline.viewportBottom
	$viewportHeight = [double]$Timeline.viewportHeight
	$composerTop = [double]$Timeline.composerTop
	if ($viewportHeight -le 1.0 -or
		[Math]::Abs($viewportBottom - ($viewportY + $viewportHeight)) -gt 1.0) {
		return $false
	}
	$layoutBoundary = $viewportBottom
	if ([bool]$Timeline.jumpToLatestVisible) {
		$jumpTop = [double]$Timeline.jumpToLatestTop
		$jumpHeight = [double]$Timeline.jumpToLatestHeight
		$jumpBottom = [double]$Timeline.jumpToLatestBottom
		$jumpTopMargin = [double]$Timeline.jumpToLatestTopMargin
		$jumpBottomMargin = [double]$Timeline.jumpToLatestBottomMargin
		if ($jumpHeight -le 1.0 -or $jumpTopMargin -lt 0 -or $jumpBottomMargin -lt 0 -or
			[Math]::Abs($jumpTop - ($layoutBoundary + $jumpTopMargin)) -gt 1.0 -or
			[Math]::Abs($jumpBottom - ($jumpTop + $jumpHeight)) -gt 1.0) {
			return $false
		}
		$layoutBoundary = $jumpBottom + $jumpBottomMargin
	}
	if ([bool]$Timeline.aboveComposerTickerVisible) {
		$tickerTop = [double]$Timeline.aboveComposerTickerTop
		$tickerHeight = [double]$Timeline.aboveComposerTickerHeight
		$tickerBottom = [double]$Timeline.aboveComposerTickerBottom
		if ($tickerHeight -le 1.0 -or
			[Math]::Abs($tickerTop - $layoutBoundary) -gt 1.0 -or
			[Math]::Abs($tickerBottom - ($tickerTop + $tickerHeight)) -gt 1.0) {
			return $false
		}
		$layoutBoundary = $tickerBottom
	}
	if ($composerTop -lt $viewportBottom - 1.0 -or
		[Math]::Abs($composerTop - $layoutBoundary) -gt 1.0) {
		return $false
	}
	if ($RequireCustomHeader -and
		([double]$Timeline.roomHeaderHeight -le 1.0 -or
			[Math]::Abs($viewportY - [double]$Timeline.roomHeaderBottom) -gt 1.0)) {
		return $false
	}
	if ($RequireHistoryPrependLayout) {
		foreach ($propertyName in @("historyLoaderVisible", "historyLoaderTop",
			"historyLoaderHeight", "historyLoaderBottom", "timelineHeaderHeight")) {
			if (-not ($Timeline.PSObject.Properties.Name -contains $propertyName)) {
				return $false
			}
		}
		$historyLoaderHeight = [double]$Timeline.historyLoaderHeight
		if ([bool]$Timeline.historyLoaderVisible -ne ($historyLoaderHeight -gt 1.0) -or
			([bool]$Timeline.historyLoaderVisible -and
				[Math]::Abs($historyLoaderHeight - 48.0) -gt 1.0) -or
			[Math]::Abs([double]$Timeline.historyLoaderBottom -
				([double]$Timeline.historyLoaderTop + $historyLoaderHeight)) -gt 1.0 -or
			[double]$Timeline.timelineHeaderHeight + 1.0 -lt $historyLoaderHeight) {
			return $false
		}
	}
	return $true
}

function Test-QmlTimelineFixtureIdentity {
	param(
		[Parameter(Mandatory = $true)]$Timeline,
		[Parameter(Mandatory = $true)][string]$ExpectedScopeToken,
		[Parameter(Mandatory = $true)][long]$ExpectedGeneration,
		[Parameter(Mandatory = $true)][AllowEmptyString()][string]$ExpectedStableIdPrefix,
		[Parameter(Mandatory = $true)][AllowEmptyCollection()][object[]]$ExpectedStableIds,
		[Parameter(Mandatory = $true)][int]$ExpectedMessageCount
	)
	if ([string]$Timeline.scopeToken -cne $ExpectedScopeToken -or
		[long]$Timeline.generation -ne $ExpectedGeneration) {
		return $false
	}
	$visibleRows = @($Timeline.visibleRows)
	$stableIds = @($ExpectedStableIds | ForEach-Object { [string]$_ })
	if ($stableIds.Count -ne $ExpectedMessageCount) {
		return $false
	}
	foreach ($stableId in $stableIds) {
		if ([string]::IsNullOrWhiteSpace($stableId)) {
			return $false
		}
	}
	if ($ExpectedMessageCount -eq 0) {
		return $visibleRows.Count -eq 0
	}
	if ($visibleRows.Count -eq 0) {
		return $false
	}
	$expectedIdSet = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
	foreach ($stableId in $stableIds) {
		if ((-not [string]::IsNullOrEmpty($ExpectedStableIdPrefix) -and
				-not $stableId.StartsWith($ExpectedStableIdPrefix, [StringComparison]::Ordinal)) -or
			-not $expectedIdSet.Add($stableId)) {
			return $false
		}
	}
	$seenIds = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
	$previousModelIndex = -1
	foreach ($row in $visibleRows) {
		$visibleId = [string]$row.id
		$modelIndex = [int]$row.modelIndex
		$expectedIndex = [Array]::IndexOf($stableIds, $visibleId)
		if ([string]::IsNullOrWhiteSpace($visibleId) -or
			$expectedIndex -lt 0 -or $expectedIndex -le $previousModelIndex -or
			$modelIndex -lt -1 -or
			($modelIndex -ge 0 -and $modelIndex -ne $expectedIndex) -or
			-not $seenIds.Add($visibleId)) {
			return $false
		}
		$previousModelIndex = $expectedIndex
	}
	return $true
}

function Get-QmlPresentationBudgetFailures {
	param(
		[Parameter(Mandatory = $true)]$Timeline,
		[Parameter(Mandatory = $true)][bool]$ExtendedPresentationBudgetExercised,
		[Parameter(Mandatory = $true)][bool]$RequireComposer,
		[double]$PrependStableDurationMs = -1.0
	)
	$failures = [Collections.Generic.List[string]]::new()
	$requiredMilestones = @(
		"startedAt", "dataReadyAt", "stableReadyAt", "firstStableFrameCommittedAt",
		"secondMatchingStableFrameCommittedAt", "firstVisibleStableRowsCommittedAt"
	)
	foreach ($milestone in $requiredMilestones) {
		if (-not ($Timeline.PSObject.Properties.Name -contains $milestone) -or
			[double]$Timeline.$milestone -le 0) {
			$failures.Add("missing_$milestone")
		}
	}
	if ($failures.Count -eq 0) {
		$ordered = @($requiredMilestones | ForEach-Object { [double]$Timeline.$_ })
		for ($index = 1; $index -lt $ordered.Count; ++$index) {
			if ($ordered[$index] -lt $ordered[$index - 1]) {
				$failures.Add("milestone_order")
				break
			}
		}
	}
	if (-not [bool]$Timeline.loadingFrameCommitted -or
		[double]$Timeline.loadingFrameDurationMs -gt 100.0) {
		$failures.Add("loading_feedback")
	}
	$visibleRowsBudgetMs = if ($ExtendedPresentationBudgetExercised) { 1000.0 } else { 300.0 }
	if (-not [bool]$Timeline.stablePresentationReady -or
		[double]$Timeline.stableReadyDurationMs -gt $visibleRowsBudgetMs) {
		$failures.Add("stable_presentation")
	}
	if (-not [bool]$Timeline.firstVisibleStableRowsCommitted -or
		[double]$Timeline.firstVisibleStableRowsDurationMs -gt $visibleRowsBudgetMs) {
		$failures.Add("first_visible_stable_rows")
	}
	if ($RequireComposer -and (-not [bool]$Timeline.composerInteractive -or
			[double]$Timeline.composerInteractiveDurationMs -gt 500.0)) {
		$failures.Add("composer_interactive")
	}
	if ($PrependStableDurationMs -ge 0 -and $PrependStableDurationMs -gt 300.0) {
		$failures.Add("stable_prepend")
	}
	$providerAppliedAt = [double]$Timeline.providerResultAppliedAt
	if ($providerAppliedAt -gt 0 -and
		([double]$Timeline.providerResultVisiblePaintAt -lt $providerAppliedAt -or
			[double]$Timeline.providerResultToVisiblePaintMs -lt 0)) {
		$failures.Add("provider_result_visible_paint")
	}
	return @($failures)
}

function Write-QmlPresentationTimingDiagnostic {
	param(
		[Parameter(Mandatory = $true)][string]$OutputDirectory,
		[Parameter(Mandatory = $true)][string]$CaseId,
		[Parameter(Mandatory = $true)]$Diagnostic
	)
	$diagnosticsDirectory = Join-Path $OutputDirectory "diagnostics"
	New-Item -ItemType Directory -Force -Path $diagnosticsDirectory | Out-Null
	$path = Join-Path $diagnosticsDirectory "$CaseId.presentation-timing.json"
	$Diagnostic | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $path -Encoding utf8NoBOM
	return $path
}

function Assert-QmlAccessibilityModalSubtree {
	param(
		[Parameter(Mandatory = $true)]$Snapshot,
		[Parameter(Mandatory = $true)][string]$CaseId,
		[string]$ExpectedDialogName = ""
	)
	$entries = @(Get-QmlAccessibilityEntries $Snapshot)
	$entryByPath = @{}
	foreach ($entry in $entries) { $entryByPath[[string]$entry.path] = $entry }

	$topLevelDialogs = @($entries | Where-Object {
		$entry = $_
		if (-not ([string]$entry.node.role).Equals("Dialog", [StringComparison]::OrdinalIgnoreCase)) {
			return $false
		}
		if (-not [string]::IsNullOrWhiteSpace($ExpectedDialogName) -and
			[string]$entry.node.name -ne $ExpectedDialogName) {
			return $false
		}
		$ancestorPath = [string]$entry.parent_path
		while (-not [string]::IsNullOrWhiteSpace($ancestorPath)) {
			$ancestor = $entryByPath[$ancestorPath]
			if ($null -ne $ancestor -and
				([string]$ancestor.node.role).Equals("Dialog", [StringComparison]::OrdinalIgnoreCase)) {
				return $false
			}
			$ancestorPath = if ($null -ne $ancestor) { [string]$ancestor.parent_path } else { "" }
		}
		return $true
	})
	if ($topLevelDialogs.Count -ne 1) {
		$expected = if ([string]::IsNullOrWhiteSpace($ExpectedDialogName)) {
			"one top-level dialog"
		} else { "one top-level '$ExpectedDialogName' dialog" }
		throw "Modal case '$CaseId' exposes $($topLevelDialogs.Count) matching dialog roots; expected $expected."
	}

	$modalPath = [string]$topLevelDialogs[0].path
	$outsideEntries = @($entries | Where-Object {
		$path = [string]$_.path
		-not (Test-QmlAccessibilityPathContains -Path $path -Candidate $modalPath) -and
			-not (Test-QmlAccessibilityPathContains -Path $modalPath -Candidate $path)
	})
	if ($outsideEntries.Count -ne 0) {
		$sample = @($outsideEntries | Select-Object -First 4 | ForEach-Object {
			"$($_.node.role) '$($_.node.name)' at $($_.path)"
		}) -join "; "
		throw "Modal case '$CaseId' exposes $($outsideEntries.Count) accessibility nodes outside its active dialog subtree: $sample."
	}
}

function Assert-QmlAccessibilityHeadingLayout {
	param(
		[Parameter(Mandatory = $true)]$Snapshot,
		[Parameter(Mandatory = $true)][string]$CaseId,
		[double]$Tolerance = 1.0
	)
	$headings = @(Get-QmlAccessibilityNodes $Snapshot | Where-Object {
		([string]$_.role).Equals("Heading", [StringComparison]::OrdinalIgnoreCase) -and
		-not [string]::IsNullOrWhiteSpace([string]$_.name) -and
		(Test-QmlAccessibilityPositiveRect $_.rect)
	})
	for ($leftIndex = 0; $leftIndex -lt $headings.Count; ++$leftIndex) {
		$left = $headings[$leftIndex]
		for ($rightIndex = $leftIndex + 1; $rightIndex -lt $headings.Count; ++$rightIndex) {
			$right = $headings[$rightIndex]
			if ([string]$left.name -eq [string]$right.name) { continue }
			$overlapWidth = [Math]::Min(
				[double]$left.rect.x + [double]$left.rect.width,
				[double]$right.rect.x + [double]$right.rect.width
			) - [Math]::Max([double]$left.rect.x, [double]$right.rect.x)
			$overlapHeight = [Math]::Min(
				[double]$left.rect.y + [double]$left.rect.height,
				[double]$right.rect.y + [double]$right.rect.height
			) - [Math]::Max([double]$left.rect.y, [double]$right.rect.y)
			if ($overlapWidth -gt $Tolerance -and $overlapHeight -gt $Tolerance) {
				throw "Case '$CaseId' exposes overlapping headings '$($left.name)' and '$($right.name)' " +
					"($([int]$overlapWidth)x$([int]$overlapHeight) px overlap)."
			}
		}
	}
}

function Assert-QmlAccessibilityViewportBounds {
	param(
		[Parameter(Mandatory = $true)]$Snapshot,
		[Parameter(Mandatory = $true)][string]$CaseId,
		[double]$Tolerance = 2.0
	)
	$structuralRoles = @(
		"Window", "Client", "Dialog", "Pane", "Grouping", "List", "PopupMenu",
		"MenuBar", "Table", "Tree"
	)
	function Test-QmlAccessibilityNodeBounds {
		param(
			[Parameter(Mandatory = $true)]$Node,
			[Parameter(Mandatory = $true)][string]$Path,
			[Parameter(Mandatory = $true)][AllowEmptyCollection()][object[]]$AncestorRects
		)
		$rectProperty = $Node.PSObject.Properties["rect"]
		$rect = if ($null -ne $rectProperty) { $rectProperty.Value } else { $null }
		$positiveRect = Test-QmlAccessibilityPositiveRect $rect
		$semanticLeaf = $positiveRect -and @($Node.children).Count -eq 0 -and
			-not [string]::IsNullOrWhiteSpace([string]$Node.role) -and
			[string]$Node.role -notin $structuralRoles
		if ($semanticLeaf) {
			foreach ($ancestorEntry in $AncestorRects) {
				$ancestor = $ancestorEntry.rect
				$outside = [double]$rect.x -lt [double]$ancestor.x - $Tolerance -or
					[double]$rect.y -lt [double]$ancestor.y - $Tolerance -or
					([double]$rect.x + [double]$rect.width) -gt
						([double]$ancestor.x + [double]$ancestor.width + $Tolerance) -or
					([double]$rect.y + [double]$rect.height) -gt
						([double]$ancestor.y + [double]$ancestor.height + $Tolerance)
				if ($outside) {
					throw "Case '$CaseId' exposes $($Node.role) '$($Node.name)' at $Path outside " +
						"ancestor $($ancestorEntry.path) bounds by more than $Tolerance px."
				}
			}
		}

		$nextAncestors = @($AncestorRects)
		if ($positiveRect) {
			$nextAncestors += [pscustomobject]@{ path = $Path; rect = $rect }
		}
		$childIndex = 0
		foreach ($child in @($Node.children)) {
			if ($null -ne $child) {
				Test-QmlAccessibilityNodeBounds $child "$Path/$childIndex" $nextAncestors
			}
			++$childIndex
		}
	}
	Test-QmlAccessibilityNodeBounds $Snapshot "0" @()
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
		[string]$CaseVariant = "none",
		[string]$SurfaceVariant = "none",
		[bool]$NavigationOpen = $false
	)
	$nodes = @(Get-QmlAccessibilityNodes $Snapshot)
	$names = @($nodes | ForEach-Object { [string]$_.name })
	$semanticStrings = @($names) + @($nodes | ForEach-Object {
		$description = $_.PSObject.Properties["description"]
		if ($null -ne $description) { [string]$description.Value } else { "" }
	})
	$surface = [string]$SurfaceVariant
	$isMediaSurface = $surface.StartsWith("media-", [StringComparison]::Ordinal)
	$isChatSurface = $surface.StartsWith("chat-", [StringComparison]::Ordinal)
	$surfaceOwnsAccessibilityTree = $surface -in @(
		"direct-message-window", "screen-share-view-loading", "screen-share-view-error",
		"screen-share-view-active", "screen-share-view-paused", "manual-plugin", "ptt-idle", "ptt-active",
		"attachment-viewer", "image-viewer",
		"media-detached-loading", "media-detached-active", "media-detached-error",
		"media-detached-retry", "media-detached-external", "media-detached-controls"
	) -or $surface.StartsWith("settings-", [StringComparison]::Ordinal) `
		-or $surface.StartsWith("dialog-", [StringComparison]::Ordinal) `
		-or $surface.StartsWith("screen-share-editor", [StringComparison]::Ordinal)
	$modalSurface = $surface.StartsWith("settings-", [StringComparison]::Ordinal) `
		-or $surface.StartsWith("dialog-", [StringComparison]::Ordinal) `
		-or $surface.StartsWith("screen-share-editor", [StringComparison]::Ordinal)
	$modalAccessibilityActive = $NavigationOpen -or $modalSurface
	$focus = Get-QmlAccessibilityFocusSummary $Snapshot
	if (-not $focus.valid) {
		throw "Case '$CaseId' accessibility tree contains no focus owner or multiple independent focus branches."
	}
	if ([string]::IsNullOrWhiteSpace([string]$focus.leaf.name)) {
		throw "Case '$CaseId' leaf focus owner has no semantic name."
	}
	$expectedPluginFocus = switch ($surface) {
		"settings-plugins" { "Plugins" }
		"settings-plugins-updating" { "Cancel plugin update" }
		"settings-plugins-partial" { "Check for updates" }
		default { "" }
	}
	if (-not [string]::IsNullOrWhiteSpace($expectedPluginFocus) -and
		[string]$focus.leaf.name -ne $expectedPluginFocus) {
		throw "Plugin settings case '$CaseId' focused '$($focus.leaf.name)' instead of '$expectedPluginFocus'."
	}
	if ($surface -eq "async-running") {
		$focusedCancelNodes = @($nodes | Where-Object {
			([string]$_.role).Equals("Button", [StringComparison]::OrdinalIgnoreCase) -and
			[string]$_.name -eq "Cancel Saving attachment" -and
			@($_.states) -contains "focused"
		})
		if ($focusedCancelNodes.Count -ne 1 -or [string]$focus.leaf.name -ne "Cancel Saving attachment") {
			throw "Async-running case '$CaseId' does not expose its cancel action as the focused accessibility node."
		}
	}
	if ($surface.StartsWith("toast-", [StringComparison]::Ordinal)) {
		$focusedDismissNodes = @($nodes | Where-Object {
			([string]$_.role).Equals("Button", [StringComparison]::OrdinalIgnoreCase) -and
			[string]$_.name -eq "Dismiss notification" -and
			@($_.states) -contains "focused"
		})
		if ($focusedDismissNodes.Count -ne 1 -or [string]$focus.leaf.name -ne "Dismiss notification") {
			throw "Toast case '$CaseId' does not expose its dismiss action as the focused accessibility node."
		}
	}
	$expectedViewerFocus = switch ($surface) {
		"attachment-viewer" { "Save original Qt Quick attachment artwork" }
		"image-viewer" { "Fit image to window" }
		"screen-share-view-active" { "Pause" }
		default { "" }
	}
	if (-not [string]::IsNullOrWhiteSpace($expectedViewerFocus)) {
		$focusedViewerActions = @($nodes | Where-Object {
			([string]$_.role).Equals("Button", [StringComparison]::OrdinalIgnoreCase) -and
			[string]$_.name -eq $expectedViewerFocus -and
			@($_.states) -contains "focused"
		})
		if ($focusedViewerActions.Count -ne 1 -or [string]$focus.leaf.name -ne $expectedViewerFocus) {
			throw "Viewer case '$CaseId' does not expose '$expectedViewerFocus' as its single focused action."
		}
	}
	$expectedViewerGraphic = switch ($surface) {
		"attachment-viewer" { "Qt Quick attachment artwork" }
		"image-viewer" { "Qt Quick image canvas" }
		"screen-share-view-active" { "Live shared screen frame" }
		default { "" }
	}
	if (-not [string]::IsNullOrWhiteSpace($expectedViewerGraphic)) {
		$viewerGraphics = @($nodes | Where-Object {
			([string]$_.role).Equals("Graphic", [StringComparison]::OrdinalIgnoreCase) -and
			[string]$_.name -eq $expectedViewerGraphic
		})
		if ($viewerGraphics.Count -ne 1) {
			throw "Viewer case '$CaseId' does not expose exactly one '$expectedViewerGraphic' graphic."
		}
	}
	$hidden = @($nodes | Where-Object {
		$states = @($_.states)
		($states -contains "invisible") -or ($states -contains "offscreen")
	})
	if ($hidden.Count -ne 0) {
		throw "Case '$CaseId' accessibility tree exposes $($hidden.Count) invisible or offscreen nodes."
	}
	Assert-QmlAccessibilityHeadingLayout -Snapshot $Snapshot -CaseId $CaseId
	Assert-QmlAccessibilityViewportBounds -Snapshot $Snapshot -CaseId $CaseId
	if ($modalAccessibilityActive) {
		$expectedDialogName = if ($NavigationOpen) { "Rooms and participants" }
			elseif ($surface.StartsWith("settings-", [StringComparison]::Ordinal)) { "Settings" }
			else { "" }
		Assert-QmlAccessibilityModalSubtree -Snapshot $Snapshot -CaseId $CaseId `
			-ExpectedDialogName $expectedDialogName
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
	$secondFixtureMessage = if ($CaseVariant -eq "rich-image-link") {
		"Open the linked Qt Quick release artwork."
	} else { "Qt Quick is ready for review." }
	$connectedFixtureMessages = @(
		"Welcome to the deterministic visual fixture.",
		$secondFixtureMessage
	)
	if ($surface -eq "direct-message-main") {
		foreach ($message in @(
			"The native DM surface feels fast.",
			"And private mode keeps this conversation local."
		)) {
			if ($message -notin $names) {
				throw "Direct-message case '$CaseId' accessibility tree lacks '$message'."
			}
		}
	} elseif ($State -eq "connected" -and -not $isChatSurface -and -not $surfaceOwnsAccessibilityTree -and
		-not $modalAccessibilityActive) {
		$conversationTimelines = @($nodes | Where-Object {
			([string]$_.role).Equals("List", [StringComparison]::OrdinalIgnoreCase) -and
			[string]$_.name -eq "Conversation messages"
		})
		if ($conversationTimelines.Count -ne 1) {
			throw "Connected case '$CaseId' does not expose exactly one semantic conversation timeline."
		}

		# The timeline exposes only delegates that intersect the viewport. A full-height
		# rich preview can legitimately displace both fixture text rows, and UIA must not
		# retain those offscreen delegates. In that case the provider grouping and its
		# actions below are the visible message evidence. Plain-chat cases still require
		# both deterministic text rows.
		$expectedMessages = @()
		if ($RichPreviewVariant -eq "none" -and $CaseVariant -eq "none") {
			$expectedMessages = @(
				"Welcome to the deterministic visual fixture.",
				$secondFixtureMessage
			)
		}
		foreach ($message in $expectedMessages) {
			if ($message -notin $names) {
				throw "Connected case '$CaseId' accessibility tree lacks expected fixture message '$message'."
			}
		}
	} elseif ($State -ne "connected") {
		foreach ($message in $connectedFixtureMessages) {
			if ($message -in $names) {
				throw "Non-connected case '$CaseId' exposes stale connected fixture message '$message'."
			}
		}
	}

	if ($surface.StartsWith("menu-", [StringComparison]::Ordinal)) {
		$menuContainers = @($nodes | Where-Object {
			([string]$_.role).Equals("PopupMenu", [StringComparison]::OrdinalIgnoreCase)
		})
		$expectedMenuContainerCount = 1
		if ($menuContainers.Count -ne $expectedMenuContainerCount) {
			throw "Menu surface '$CaseId' exposes $($menuContainers.Count) PopupMenu containers; " +
				"expected exactly $expectedMenuContainerCount."
		}
		$menuItems = @($nodes | Where-Object {
			([string]$_.role).Equals("MenuItem", [StringComparison]::OrdinalIgnoreCase)
		})
		if ($menuItems.Count -lt 1) {
			throw "Menu surface '$CaseId' exposes no semantic MenuItem nodes."
		}
	}
	$requiredSurfaceNames = switch ($surface) {
		"menu-app" { @(
			"Server information…", "Search…", "Connect to a server…",
			"Disconnect…", "Access tokens…", "Root room access & settings..."
		) }
		"menu-app-server" { @(
			"Server information…", "Search…", "Connect to a server…",
			"Disconnect…", "Access tokens…", "Root room access & settings..."
		) }
		# The profile surface presents grouped submenus first. Nested actions such
		# as Disconnect are covered by component/controller tests; they are not
		# visible semantic nodes until their top-level group is opened.
		"menu-profile" { @("Demo User", "Presence", "Profile") }
		"menu-room" { @("Send room message…", "Copy room URL", "Room access & settings...", "Share your screen…") }
		"menu-text-room" { @("Mark read", "Edit source room access...", "Go to source room") }
		"menu-chat-background" { @("Send room message…", "Copy room URL") }
		"conversation-search-match" { @(
			"Search this conversation", "Search messages in this conversation", "1 of 1",
			"Previous match", "Next match", "Close conversation search"
		) }
		"conversation-search-empty" { @(
			"Search this conversation", "Search messages in this conversation", "No matches",
			"Close conversation search"
		) }
		"settings-audio-input" { @(
			"Settings", "Audio Input", "Detection guide", "Set up voice activation",
			"Cancel", "Apply", "Done"
		) }
		"settings-audio-input-advanced" { @(
			"Settings", "Audio Input", "Input gate", "Stop threshold",
			"Hide advanced settings", "Cancel", "Apply", "Done"
		) }
		"settings-audio-output" { @("Settings", "Audio Output", "Device", "Cancel", "Apply", "Done") }
		"settings-audio-output-advanced" { @(
			"Settings", "Audio Output", "Output delay", "Jitter buffer",
			"Hide advanced settings", "Cancel", "Apply", "Done"
		) }
		"settings-appearance" { @("Settings", "Appearance", "Native interface", "Cancel", "Apply", "Done") }
		"settings-user-interface" { @("Settings", "User Interface", "Window behavior", "Cancel", "Apply", "Done") }
		"settings-messages-sounds" { @("Settings", "Messages & Sounds", "Messages", "Cancel", "Apply", "Done") }
		"settings-messages-events" { @("Settings", "Messages & Sounds", "Per-event behavior", "Event behavior", "Debug", "Critical", "Warning", "Cancel", "Apply", "Done") }
		"settings-messages-events-compact" { @("Settings", "Messages & Sounds", "Per-event behavior", "Event behavior", "Debug", "Critical", "Warning", "Cancel", "Apply", "Done") }
		"settings-key-bindings" { @("Settings", "Key Bindings", "Shortcut controls", "Cancel", "Apply", "Done") }
		"settings-key-bindings-populated" { @("Settings", "Key Bindings", "Configured shortcuts", "Shortcuts", "Cancel capture", "Cancel", "Apply", "Done") }
		"settings-network" { @("Settings", "Network", "Client connection", "Cancel", "Apply", "Done") }
		"settings-network-advanced" { @(
			"Settings", "Network", "Force TCP mode", "Use Quality of Service",
			"Hide advanced settings", "Cancel", "Apply", "Done"
		) }
		"settings-screen-sharing" { @("Settings", "Screen Sharing", "Behavior", "Cancel", "Apply", "Done") }
		"settings-plugins" { @(
			"Settings", "Plugins", "Installed plugins", "Install plugin…", "Rescan", "Check for updates",
			"Manual placement", "Enabled", "Positional audio", "Configure", "About", "Cancel", "Apply", "Done"
		) }
		"settings-plugins-updating" { @(
			"Settings", "Plugins", "Updating plugins: Downloading Game telemetry",
			"Plugin update progress", "Cancel plugin update", "Cancel", "Apply", "Done"
		) }
		"settings-plugins-partial" { @(
			"Settings", "Plugins", "Plugin update finished: 2 updated · 1 failed",
			"Updated: Manual placement — Already current",
			"Failed: Game telemetry — Signature verification failed",
			"Updated: Stream Deck controls — Updated to 0.9.9", "Check for updates", "Cancel", "Apply", "Done"
		) }
		"settings-about" { @("Settings", "About", "Mumble", "Windows", "Cancel", "Apply", "Done") }
		"dialog-connect" { @(
			"Connect to a server", "Saved servers", "Mumble Community", "Studio",
			"Users: 18/128", "Ping: 28 ms", "Cancel", "Edit", "Connect"
		) }
		"dialog-connect-editor" { @(
			"Connect to a server", "Edit server", "Server details",
			"Details", "Server title", "Server address", "Port", "Username", "Server password",
			"Back", "Remove", "Save", "Connect"
		) }
		"dialog-connect-validation" { @(
			"Connect to a server", "Add server", "Server details", "Details", "Server address", "Username",
			"Enter a server host.", "Enter a username.", "Back", "Save", "Connect"
		) }
		"dialog-connect-empty" { @(
			"Connect to a server", "Saved servers", "Favorites", "0 server(s)", "Add a server to get started.",
			"Add server", "Cancel", "Edit", "Connect"
		) }
		"dialog-search-empty" { @(
			"Search", "Find users and rooms on the current server.", "Users", "Rooms",
			"Regular expression", "Results", "Start typing to search users and rooms.", "Close"
		) }
		"dialog-search-results" { @(
			"Search", "Find users and rooms on the current server.", "Results", "Relay Ops",
			"#relay", "Relay_Bot", "Open", "Join", "Message", "Select", "Close"
		) }
		"dialog-search-regex-error" { @(
			"Search", "Find users and rooms on the current server.", "Regular expression",
			"Invalid regular expression.", "No matching users or rooms.", "Close"
		) }
		"dialog-certificate" { @(
			"Certificate", "Current certificate", "Certificate action", "Status: Installed", "Export file", "Close", "Apply"
		) }
		"dialog-certificate-create" { @(
			"Certificate", "Current certificate", "Certificate action", "Action: Create", "Name", "Email", "Close", "Apply"
		) }
		"dialog-acl-populated" { @(
			"Room access & settings",
			"Changes apply to Root / Lobby (room ID 1). Access rules inherit through the room tree unless overridden here.",
			"Access rules and groups", "These permissions belong to the target room shown above.",
			"Inherit access rules from parent room", "Room password", "Groups", "scrim-team",
			"Added members", "Kira (#2)", "Access rules", "Rule for all", "Save changes"
		) }
		"dialog-stonks-populated" { @(
			"Stonks", "Portfolio data is ready.", "Overview", "Portfolio", "Leaderboard",
			"Following", "Audit", "Admin", "Refresh"
		) }
		"dialog-recorder" { @(
			"Voice recorder", "Record the current voice session.", "Ready, 00:00:00", "Output",
			"Target directory", "Filename", "Format", "Recording mode", "Start recording", "Close"
		) }
		"dialog-recorder-recording" { @(
			"Voice recorder", "Record the current voice session.", "Recording, 00:14:32",
			"Elapsed 00:14:32", "Output", "Pause", "Stop", "Close"
		) }
		"dialog-server-users-loading" { @(
			"Registered users", "Inspect, rename, or unregister server accounts.",
			"Search name, ID or last room", "Refresh", "Loading registered users", "Close"
		) }
		"dialog-server-users-ready" { @(
			"Registered users", "Search name, ID or last room", "Demo Admin <ops>", "Demo Moderator",
			"Account", "Rename", "Unregister", "Close"
		) }
		"dialog-server-users-edit" { @(
			"Registered users", "Search name, ID or last room", "Demo Moderator", "Registered user ID 42",
			"User name", "Rename", "Unregister", "Close"
		) }
		"dialog-server-users-confirm" { @(
			"Registered users", "Rename registered user?", "Rename Demo Moderator to Community Moderator on this server?",
			"Cancel", "Rename", "Close"
		) }
		"dialog-server-bans-empty" { @(
			"Ban list", "Inspect and manage active server bans.", "Search user, address, hash or reason",
			"No active bans", "IP address", "Certificate hash", "Add ban", "Clear", "Close"
		) }
		"dialog-server-bans-edit" { @(
			"Ban list", "Search user, address, hash or reason", "Demo Spammer", "Repeated channel spam",
			"Edit ban", "Update", "Remove", "Clear", "Close"
		) }
		"dialog-server-bans-error" { @(
			"Ban list", "Search user, address, hash or reason", "The server rejected the ban-list request.",
			"Try again", "Close"
		) }
		"screen-share-editor" { @(
			"Start screen share", "Share to Lobby", "Screens", "Open windows", "Resolution", "Frame rate", "Audio", "Start sharing"
		) }
		"screen-share-editor-compact" { @(
			"Start screen share", "Share to Lobby", "Open windows", "Qt Quick Design Review", "Start sharing"
		) }
		"chat-message-states" { @(
			"The latest community review build is ready.",
			"Ship the Qt Quick candidate after the gate.",
			"👍 reaction, 3", "Sending…", "Couldn’t send", "Retry", "Message deleted"
		) }
		"chat-composer-states" { @(
			"Replying to Alex", "Use the community test build once this check passes.",
			"community-preview.png", "Uploading · 54%", "release-notes.pdf",
			"Upload interrupted", "Retry release-notes.pdf", "Alex", "Message Lobby"
		) }
		"chat-attachment-states" { @(
			"Alex", "10:36", "Here are the assets from the latest community-test pass.",
			"Message attachments", "Community test dashboard",
			"Save original Community test dashboard", "Performance trace",
			"Save original Performance trace",
			"Loading Performance trace", "Failed attachment preview",
			"Save original Failed attachment preview", "Preview unavailable",
			"Retry preview for Failed attachment preview"
		) }
		"chat-history-prepend-anchor" { @(
			"Anchor message 12 remains in place after older history loads.", "Jump to latest"
		) }
		"chat-sparse-0" { @("This conversation is quiet", "Message Lobby") }
		"chat-sparse-1" { @("Message Lobby") }
		"chat-sparse-2" { @("Message Lobby") }
		"chat-sparse-5" { @("Message Lobby") }
		"chat-sparse-10" { @("Message Lobby") }
		"chat-sparse-stonks-5" { @("Stonks room dashboard", "Message #stonks") }
		"direct-message-main" { @(
			"Alex", "The native DM surface feels fast.", "Message attachments", "Release checklist attachment"
		) }
		"direct-message-tray" { @(
			"Direct messages", "Private and persistent conversations", "Direct-message conversations",
			"Alex: The native DM surface feels fast.", "Private conversation · Lobby. 2 unread messages",
			"2 unread messages", "Mark all read", "Close direct messages"
		) }
		"direct-message-window" { @(
			"Direct message · Alex", "Messages with Alex",
			"Alex: The native DM surface feels fast.",
			"You: And private mode keeps this conversation local.",
			"Message attachments", "Release checklist attachment"
		) }
		"attachment-viewer" { @(
			"Qt Quick attachment artwork", "Save original Qt Quick attachment artwork",
			"Close attachment viewer"
		) }
		"image-viewer" { @(
			"Qt Quick image canvas",
			"Image viewer. Use plus and minus to zoom, arrow keys to pan, and zero to fit.",
			"Image zoom controls", "Fit image to window"
		) }
		"screen-share-view-loading" { @("Connecting to the live share") }
		"screen-share-view-error" { @("Screen share unavailable") }
		"screen-share-view-active" { @("Live shared screen frame", "Pause", "Mute", "Stream volume", "Enter full screen", "Close viewer") }
		"screen-share-view-paused" { @("Paused locally") }
		"manual-plugin" { @("Position and orientation", "Identity and link state") }
		"ptt-idle" { @("Hold to push to talk") }
		"ptt-active" { @("Transmitting, release push to talk") }
		"async-running" { @("Saving attachment: Downloading original image") }
		"async-error" { @("Attachment operation results: One attachment could not be saved") }
		"async-success" { @("Attachment operation results: All attachments saved") }
		"update-banner" { @("Mumble update ready") }
		"watch-together-hosting" { @(
			"Watch Together: Community release watch party", "HOSTING", "2 participants",
			"Transfer host", "End"
		) }
		"toast-single" { @(
			"Settings saved. Your Modern client preferences are ready.",
			"Review", "Dismiss notification"
		) }
		"toast-duplicate" { @(
			"Settings saved. Your Modern client preferences are ready. Repeated 4 times",
			"Review", "Dismiss notification"
		) }
		"media-inline-loading" { @("YouTube inline media player", "Loading inline media") }
		"media-inline-active" { @("YouTube inline media player", "Deterministic media playback preview") }
		"media-inline-error" { @("YouTube inline media player", "YouTube playback failed") }
		"media-inline-retry" { @("YouTube inline media player", "YouTube playback failed", "Retry") }
		"media-inline-external" { @("YouTube inline media player", "YouTube playback failed", "Open externally") }
		"media-inline-controls" { @("YouTube inline media player", "Deterministic media playback preview") }
		"media-detached-loading" { @("Loading YouTube media", "Loading media player") }
		"media-detached-active" { @("Deterministic media playback preview") }
		"media-detached-error" { @("YouTube playback failed") }
		"media-detached-retry" { @("YouTube playback failed", "Retry") }
		"media-detached-external" { @("YouTube playback failed", "Open externally") }
		"media-detached-controls" { @("Deterministic media playback preview") }
		default { @() }
	}
	foreach ($requiredName in @($requiredSurfaceNames)) {
		if ($requiredName -notin $semanticStrings) {
			throw "Surface '$CaseId' lacks required semantic node '$requiredName'."
		}
	}
	if ($surface -eq "menu-profile") {
		$profileHeaders = @($nodes | Where-Object {
			([string]$_.role).Equals("MenuItem", [StringComparison]::OrdinalIgnoreCase) -and
			([string]$_.name).Equals("Demo User", [StringComparison]::Ordinal) -and
			([string]$_.description).Equals("Online", [StringComparison]::Ordinal)
		})
		if ($profileHeaders.Count -ne 1) {
			throw "Profile menu '$CaseId' must expose Online once as the Demo User header description."
		}
	}
	if ($surface -eq "dialog-recorder" -and "Stop" -in $names) {
		throw "Idle recorder surface '$CaseId' exposes the recording-only Stop action."
	}
	if ($surface -eq "dialog-recorder-recording" -and "Start recording" -in $names) {
		throw "Active recorder surface '$CaseId' exposes the idle-only Start action."
	}
	if ($surface -eq "settings-plugins-updating" -and
		"Failed: Game telemetry — Signature verification failed" -in $names) {
		throw "Updating plugin case '$CaseId' exposes terminal per-item results before completion."
	}
	if ($surface -eq "settings-plugins-partial" -and "Cancel plugin update" -in $names) {
		throw "Partial plugin case '$CaseId' exposes a stale cancellation action."
	}
	if ($surface.StartsWith("dialog-connect", [StringComparison]::Ordinal)) {
		$expectedFavorites = if ($surface -in @("dialog-connect", "dialog-connect-empty")) {
			if ($surface -eq "dialog-connect-empty") { @{} } else {
			@{
				"Mumble Community" = "voice.example.invalid:64738 / Demo User"
				"Studio" = "studio.example.invalid:64739 / Producer"
			}
			}
		} else {
			@{}
		}
		foreach ($favoriteName in @($expectedFavorites.Keys)) {
			$favoriteRows = @($nodes | Where-Object {
				([string]$_.role).Equals("ListItem", [StringComparison]::OrdinalIgnoreCase) -and
				[string]$_.name -eq $favoriteName -and
				[string]$_.description -eq [string]$expectedFavorites[$favoriteName]
			})
			if ($favoriteRows.Count -ne 1) {
				throw "Connect surface '$CaseId' does not expose exactly one production-shaped '$favoriteName' favorite row."
			}
		}
		$placeholderRows = @($nodes | Where-Object {
			([string]$_.role).Equals("ListItem", [StringComparison]::OrdinalIgnoreCase) -and
			[string]$_.name -eq "Saved server"
		})
		if ($placeholderRows.Count -ne 0) {
			throw "Connect surface '$CaseId' exposes a favorite row without its production label."
		}
	}
	if ($surface.StartsWith("dialog-search", [StringComparison]::Ordinal)) {
		$searchResults = @($nodes | Where-Object {
			([string]$_.role).Equals("ListItem", [StringComparison]::OrdinalIgnoreCase) -and
			[string]$_.name -in @("Relay Ops", "#relay", "Relay_Bot")
		})
		if ($surface -eq "dialog-search-results") {
			if ($searchResults.Count -ne 3) {
				throw "Search results surface '$CaseId' exposes $($searchResults.Count) deterministic result rows; expected three."
			}
			$relayRoom = @($searchResults | Where-Object {
				[string]$_.name -eq "Relay Ops" -and
				[string]$_.description -eq "Root / Operations - room name match"
			})
			if ($relayRoom.Count -ne 1) {
				throw "Search results surface '$CaseId' lacks the production-shaped Relay Ops row."
			}
		} elseif ($searchResults.Count -ne 0) {
			throw "Search non-result surface '$CaseId' exposes stale deterministic result rows."
		}
	}
	$motdPanes = @($nodes | Where-Object {
		[string]$_.name -eq "Server message of the day" -and
		([string]$_.role).Equals("Pane", [StringComparison]::OrdinalIgnoreCase)
	})
	$motdShouldBeVisible = $MotdVariant -in @("expanded", "collapsed", "changed", "history-visible")
	if ($motdShouldBeVisible -and $motdPanes.Count -ne 1) {
		throw "MOTD case '$CaseId' does not expose exactly one welcome pane to accessibility."
	}
	if ($motdShouldBeVisible) {
		$outgoingTimestampNodes = @($nodes | Where-Object { [string]$_.name -eq "10:25" })
		if ($outgoingTimestampNodes.Count -ne 1) {
			throw "MOTD case '$CaseId' exposes its outgoing timestamp $($outgoingTimestampNodes.Count) times; expected exactly one semantic owner."
		}
	}
	if ($motdShouldBeVisible -and "Hide welcome message" -notin $names) {
		throw "MOTD case '$CaseId' does not expose its hide-message affordance to accessibility."
	}
	if (-not $motdShouldBeVisible -and $motdPanes.Count -ne 0) {
		throw "Case '$CaseId' exposes a welcome pane for MOTD variant '$MotdVariant' while it should be hidden."
	}
	if ($RichPreviewVariant -ne "none" -and -not $isMediaSurface) {
		$matchingPreviewCards = @($nodes | Where-Object {
			([string]$_.role).Equals("Grouping", [StringComparison]::OrdinalIgnoreCase) -and
			([string]$_.name -eq $RichPreviewTitle -or
				([string]$_.name).StartsWith($RichPreviewTitle + ":", [StringComparison]::Ordinal))
		})
		if ([string]::IsNullOrWhiteSpace($RichPreviewTitle) -or $matchingPreviewCards.Count -ne 1) {
			throw "Rich-preview case '$CaseId' does not expose exactly one '$RichPreviewTitle' grouping."
		}
		$typedProviderOpenLabel = if (
			-not [string]::IsNullOrWhiteSpace($RichPreviewEmbedProvider) -and
			$RichPreviewOpenLabel.StartsWith("Open on ", [StringComparison]::Ordinal)) {
			"Open source on " + $RichPreviewOpenLabel.Substring(8)
		} else { "" }
		$matchingPreviewLinks = @($nodes | Where-Object {
			([string]$_.role).Equals("Link", [StringComparison]::OrdinalIgnoreCase) -and
			([string]$_.name -eq ($RichPreviewOpenLabel + ": " + $RichPreviewTitle) -or
				(-not [string]::IsNullOrWhiteSpace($RichPreviewEmbedProvider) -and
				 ([string]$_.name -eq $RichPreviewOpenLabel -or
				  [string]$_.name -eq $typedProviderOpenLabel)))
		})
		if ($RichPreviewVariant -in @("loading", "error")) {
			$boundedNativeShells = @($matchingPreviewCards | Where-Object {
				(Test-QmlAccessibilityPositiveRect $_.rect) -and
				[double]$_.rect.height -ge 40.0 -and [double]$_.rect.height -le 96.0
			})
			$boundedOriginLinks = @($matchingPreviewLinks | Where-Object {
				Test-QmlAccessibilityPositiveRect $_.rect
			})
			if ($boundedNativeShells.Count -ne 1 -or $boundedOriginLinks.Count -ne 1) {
				throw "Deferred rich-preview case '$CaseId' does not expose one bounded native shell and origin link."
			}
		}
		if ($RichPreviewVariant -eq "loading") {
			if ([string]::IsNullOrWhiteSpace($RichPreviewOpenLabel) -or $matchingPreviewLinks.Count -ne 1) {
				throw "Loading rich-preview case '$CaseId' does not expose exactly one safe origin link."
			}
			$prematurePreviewActions = @($nodes | Where-Object {
				([string]$_.role).Equals("Button", [StringComparison]::OrdinalIgnoreCase) -and
				[string]$_.name -in @("Play video in chat", "Preview actions", "Watch together")
			})
			if ($prematurePreviewActions.Count -ne 0) {
				throw "Loading rich-preview case '$CaseId' exposes playback or overflow actions before hydration completes."
			}
		}
		if ($RichPreviewVariant -eq "direct-media") {
			if ($matchingPreviewLinks.Count -ne 0) {
				throw "Direct-media case '$CaseId' exposes a competing open link beside its primary playback action."
			}
			if ([string]::IsNullOrWhiteSpace($RichPreviewPlayName)) {
				throw "Direct-media case '$CaseId' does not publish its title-bound playback action name."
			}
			foreach ($requiredAction in @($RichPreviewPlayName, "Preview actions")) {
				$matchingActions = @($nodes | Where-Object {
					([string]$_.role).Equals("Button", [StringComparison]::OrdinalIgnoreCase) -and
					[string]$_.name -eq $requiredAction
				})
				if ($matchingActions.Count -ne 1) {
					throw "Direct-media case '$CaseId' does not expose exactly one '$requiredAction' action."
				}
			}
		} elseif (
			([string]::IsNullOrWhiteSpace($RichPreviewOpenLabel) -or $matchingPreviewLinks.Count -ne 1)) {
			throw "Rich-preview case '$CaseId' does not expose exactly one explicit open link."
		}
		if (-not [string]::IsNullOrWhiteSpace($RichPreviewEmbedProvider)) {
			$typedProviderPlayName = if ($RichPreviewPlayName.EndsWith(
					" here", [StringComparison]::Ordinal)) {
				$RichPreviewPlayName.Substring(0, $RichPreviewPlayName.Length - 5)
			} else { $RichPreviewPlayName }
			$matchingPlayActions = @($nodes | Where-Object {
				([string]$_.role).Equals("Button", [StringComparison]::OrdinalIgnoreCase) -and
				[string]$_.name -in @($RichPreviewPlayName, $typedProviderPlayName)
			})
			if ([string]::IsNullOrWhiteSpace($RichPreviewPlayName) -or $matchingPlayActions.Count -ne 1) {
				throw "Rich-preview case '$CaseId' does not expose exactly one provider or typed-document play action."
			}
		}
		$expectedProviderGroupName = switch ($RichPreviewVariant) {
			"steam" { "Store details"; break }
			"google" { "Google Search"; break }
			# The provider-owned prefix stays stable while the fixture identity remains
			# visible after it, e.g. "Twitch stream: Mumble Dev".
			"twitch" { "Twitch stream"; break }
			"flashback" { "Discussion details"; break }
			"x" { "Social post"; break }
			"github" { "Repository details"; break }
			"vehicle" { "Vehicle details"; break }
			"property" { "Property details"; break }
			"article" { "Article details"; break }
			"marketplace" { "Listing details"; break }
			"weather" { "Weather"; break }
			"place" { "Place"; break }
			"traffic" { "Traffic"; break }
			"link-digest" { "Link digest"; break }
			default { "" }
		}
		if (-not [string]::IsNullOrWhiteSpace($expectedProviderGroupName)) {
			$matchingProviderGroups = @($nodes | Where-Object {
				([string]$_.role).Equals("Grouping", [StringComparison]::OrdinalIgnoreCase) -and
				(($RichPreviewVariant -eq "twitch" -and
					([string]$_.name).StartsWith($expectedProviderGroupName, [StringComparison]::Ordinal)) -or
				 ($RichPreviewVariant -ne "twitch" -and [string]$_.name -eq $expectedProviderGroupName))
			})
			if ($matchingProviderGroups.Count -ne 1) {
				throw "Rich-preview case '$CaseId' does not expose exactly one '$expectedProviderGroupName' provider grouping."
			}
		}
		if ($RichPreviewVariant -eq "sensitive" -and "Reveal sensitive preview media" -notin $names) {
			throw "Sensitive rich-preview case '$CaseId' does not expose its explicit reveal action."
		}
	}
	if ($CaseVariant -eq "rich-image-link") {
		$linkedImages = @($nodes | Where-Object {
			([string]$_.role).Equals("Link", [StringComparison]::OrdinalIgnoreCase) -and
			[string]$_.name -eq "Qt Quick release artwork"
		})
		if ($linkedImages.Count -ne 1) {
			throw "Rich-message case '$CaseId' does not expose exactly one linked-image semantic owner."
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

$pingResponse = Invoke-Automation @{ command = "ping" }
Assert-QmlVisualProcessIdentity -PingResponse $pingResponse `
	-ExpectedProcessId $ExpectedProcessId | Out-Null

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
foreach ($capabilityName in @("supported_presentation_families", "supported_case_variants",
	"supported_surface_variants", "supported_densities")) {
	if (-not ($capabilities.PSObject.Properties.Name -contains $capabilityName)) {
		throw "Qt Quick visual gate does not advertise '$capabilityName'. The gate fails closed."
	}
}
$supportedPresentationFamilies = @($capabilities.supported_presentation_families)
$supportedCaseVariants = @($capabilities.supported_case_variants)
$supportedSurfaceVariants = @($capabilities.supported_surface_variants)
$supportedDensities = @($capabilities.supported_densities)
$requiredPresentationFamilies = @($selectedCases | ForEach-Object {
	if ($_.PSObject.Properties.Name -contains "presentation_family") { [string]$_.presentation_family } else { "shell" }
} | Sort-Object -Unique)
$requiredCaseVariants = @($selectedCases | ForEach-Object {
	if ($_.PSObject.Properties.Name -contains "case_variant") { [string]$_.case_variant }
	elseif ($_.PSObject.Properties.Name -contains "rich_preview_variant") { [string]$_.rich_preview_variant }
	else { "none" }
} | Sort-Object -Unique)
$requiredSurfaceVariants = @($selectedCases | ForEach-Object {
	if ($_.PSObject.Properties.Name -contains "surface_variant") { [string]$_.surface_variant } else { "none" }
} | Sort-Object -Unique)
$requiredDensities = @($selectedCases | ForEach-Object {
	if ($_.PSObject.Properties.Name -contains "density") { [string]$_.density }
	elseif ([string]$_.layout -eq "compact") { "compact" } else { "comfortable" }
} | Sort-Object -Unique)
foreach ($family in $requiredPresentationFamilies) {
	if ($family -notin $supportedPresentationFamilies) {
		throw "Required presentation family '$family' is not supported. The gate fails closed."
	}
}
foreach ($variant in $requiredCaseVariants) {
	if ($variant -notin $supportedCaseVariants) {
		throw "Required visual case variant '$variant' is not supported. The gate fails closed."
	}
}
foreach ($variant in $requiredSurfaceVariants) {
	if ($variant -notin $supportedSurfaceVariants) {
		throw "Required product-surface visual variant '$variant' is not supported. The gate fails closed."
	}
}
foreach ($density in $requiredDensities) {
	if ($density -notin $supportedDensities) {
		throw "Required visual density '$density' is not supported. The gate fails closed."
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
	$presentationFamily = if ($case.PSObject.Properties.Name -contains "presentation_family") {
		[string]$case.presentation_family
	} else { "shell" }
	$caseVariant = if ($case.PSObject.Properties.Name -contains "case_variant") {
		[string]$case.case_variant
	} elseif ($richPreviewVariant -ne "none") { $richPreviewVariant } else { "none" }
	$surfaceVariant = if ($case.PSObject.Properties.Name -contains "surface_variant") {
		[string]$case.surface_variant
	} else { "none" }
	$density = if ($case.PSObject.Properties.Name -contains "density") {
		[string]$case.density
	} elseif ([string]$case.layout -eq "compact") { "compact" } else { "comfortable" }
	$isMediaSurface = $surfaceVariant.StartsWith("media-", [StringComparison]::Ordinal)
	if ($motdVariant -ne "none" -and [string]$case.state -ne "connected") {
		throw "Case '$($case.id)' requests MOTD variant '$motdVariant' outside connected state."
	}
	if ($caseVariant -ne "none" -and [string]$case.state -ne "connected") {
		throw "Case '$($case.id)' requests visual variant '$caseVariant' outside connected state."
	}
	$requiresTimelineIdentity = [string]$case.state -eq "connected"
	$preApplyTimelineGeneration = $null
	$applyTimer = [Diagnostics.Stopwatch]::StartNew()
	try {
		$apply = Invoke-Automation @{
			command = "setQmlVisualGateState"; case_id = [string]$case.id; state = [string]$case.state
			theme = [string]$case.theme; layout = [string]$case.layout; width = [int]$case.width
			height = [int]$case.height; motd_variant = $motdVariant; rich_preview_variant = $richPreviewVariant
			rich_preview_size = $richPreviewSize; presentation_family = $presentationFamily; case_variant = $caseVariant
			surface_variant = $surfaceVariant; density = $density
		}
	} catch {
		$applyTimer.Stop()
		$applyFailureMessage = [string]$_.Exception.Message
		if (-not [string]::IsNullOrWhiteSpace($AutomationToken)) {
			$applyFailureMessage = $applyFailureMessage.Replace($AutomationToken, "<redacted>")
		}
		if ($applyFailureMessage.Length -gt 1200) {
			$applyFailureMessage = $applyFailureMessage.Substring(0, 1200)
		}
		Write-QmlPresentationTimingDiagnostic -OutputDirectory $output `
			-CaseId ([string]$case.id) -Diagnostic ([ordered]@{
				case_id = [string]$case.id
				status = "apply_rpc_failed"
				apply_duration_ms = [Math]::Round($applyTimer.Elapsed.TotalMilliseconds, 3)
				pre_apply_generation = $preApplyTimelineGeneration
				error = $applyFailureMessage
			}) | Out-Null
		throw
	} finally {
		if ($applyTimer.IsRunning) { $applyTimer.Stop() }
	}
	$applyDurationMs = [Math]::Round($applyTimer.Elapsed.TotalMilliseconds, 3)
	$applied = $apply.applied
	foreach ($property in @("case_id", "state", "theme", "layout", "density", "motd_variant", "rich_preview_variant",
		"rich_preview_size", "rich_preview_present", "rich_preview_message_id", "rich_body_message_id", "rich_preview_title",
		"presentation_family", "case_variant", "surface_variant", "surface_present", "capture_window",
		"screen_share_native_frame_ready", "conversation_search_state",
		"chat_fixture_state", "fixture_scope_token", "fixture_message_id_prefix",
		"fixture_message_ids",
		"pre_apply_presentation_generation", "presentation_generation", "timeline_presentation_wait_ms",
		"rich_preview_open_label", "rich_preview_embed_provider", "rich_preview_embed_aspect",
		"rich_preview_media_count", "rich_preview_has_thumbnail", "width", "height", "message_count",
		"motd_present", "motd_expanded", "motd_changed", "motd_has_user_history", "motd_visible",
		"manual_plugin_state", "recorder_state", "toast_state", "focus_target", "actual_device_pixel_ratio", "generation")) {
		if (-not ($applied.PSObject.Properties.Name -contains $property)) { throw "Case '$($case.id)' lacks applied '$property'." }
	}
	if ($requiresTimelineIdentity) {
		$preApplyTimelineGeneration = [long]$applied.pre_apply_presentation_generation
	}
	if ([string]::IsNullOrWhiteSpace([string]$applied.focus_target)) {
		throw "Case '$($case.id)' returned an empty deterministic focus target."
	}
	if ($surfaceVariant -eq "async-running" -and [string]$applied.focus_target -ne "operationCancelButton") {
		throw "Case '$($case.id)' focused '$($applied.focus_target)' instead of the running operation's cancel action."
	}
	if ($surfaceVariant.StartsWith("toast-", [StringComparison]::Ordinal)) {
		$toastState = $applied.toast_state
		$expectedRepeats = if ($surfaceVariant -eq "toast-duplicate") { 4 } else { 1 }
		if ([string]$applied.focus_target -ne "toastDismissButton") {
			throw "Toast case '$($case.id)' focused '$($applied.focus_target)' instead of its dismiss action."
		}
		if ($null -eq $toastState -or -not [bool]$toastState.visible -or
			[int]$toastState.repeat_count -ne $expectedRepeats -or
			[string]$toastState.action_id -ne "configure.settings") {
			$toastJson = $toastState | ConvertTo-Json -Compress -Depth 5
			throw "Toast case '$($case.id)' did not retain its typed fixture state: $toastJson"
		}
	}
	if ($surfaceVariant -eq "screen-share-view-active") {
		if (-not [bool]$applied.screen_share_native_frame_ready) {
			throw "Active screen-share case '$($case.id)' did not expose a ready native decoded-frame surface."
		}
		if ([string]$applied.focus_target -ne "screenSharePauseButton") {
			throw "Active screen-share case '$($case.id)' did not focus the primary Pause control."
		}
	}
	if ($surfaceVariant.StartsWith("conversation-search-", [StringComparison]::Ordinal)) {
		$searchState = $applied.conversation_search_state
		$expectMatch = $surfaceVariant -eq "conversation-search-match"
		$expectedQuery = if ($expectMatch) { "Qt Quick" } else { "missing constellation" }
		$expectedCount = if ($expectMatch) { 1 } else { 0 }
		if ([string]$applied.focus_target -ne "conversationSearchField" -or
			$null -eq $searchState -or [string]$searchState.query -ne $expectedQuery -or
			[int]$searchState.match_count -ne $expectedCount -or
			($expectMatch -and ([int]$searchState.current_match_index -ne 0 -or
				[int]$searchState.current_match_row -lt 0 -or
				[string]::IsNullOrWhiteSpace([string]$searchState.current_match_stable_id))) -or
			(-not $expectMatch -and ([int]$searchState.current_match_index -ne -1 -or
				[int]$searchState.current_match_row -ne -1 -or
				-not [string]::IsNullOrEmpty([string]$searchState.current_match_stable_id)))) {
			$searchJson = $searchState | ConvertTo-Json -Compress -Depth 5
			throw "Conversation-search case '$($case.id)' did not retain its typed search/focus state " +
				"(focus='$([string]$applied.focus_target)'): $searchJson"
		}
	}
	if ($surfaceVariant -eq "manual-plugin") {
		$manualState = $applied.manual_plugin_state
		$requiredManualProperties = @("x", "y", "z", "azimuth", "elevation", "context", "identity",
			"stale_seconds", "active", "linked")
		foreach ($property in $requiredManualProperties) {
			if ($null -eq $manualState -or -not ($manualState.PSObject.Properties.Name -contains $property)) {
				throw "Case '$($case.id)' lacks Manual Plugin applied state '$property'."
			}
		}
		$manualStateMatches = [Math]::Abs([double]$manualState.x - 2.75) -le 0.001 -and
			[Math]::Abs([double]$manualState.y - 1.4) -le 0.001 -and
			[Math]::Abs([double]$manualState.z - -4.25) -le 0.001 -and
			[int]$manualState.azimuth -eq 32 -and [int]$manualState.elevation -eq -8 -and
			[string]$manualState.context -eq "visual-fixture:lobby" -and
			[string]$manualState.identity -eq "Demo User · Qt Quick" -and
			[int]$manualState.stale_seconds -eq 15 -and [bool]$manualState.active -and [bool]$manualState.linked
		if (-not $manualStateMatches) {
			$manualJson = $manualState | ConvertTo-Json -Compress -Depth 5
			throw "Case '$($case.id)' did not retain the deterministic Manual Plugin state after opening its tool: $manualJson"
		}
	}
	$captureWindow = [string]$applied.capture_window
	if ([string]::IsNullOrWhiteSpace($captureWindow)) {
		throw "Case '$($case.id)' returned no capture-window contract."
	}
	$expectedCaptureWindow = if ($surfaceVariant -eq "direct-message-window") { "direct-message" }
		elseif ($surfaceVariant.StartsWith("settings-", [StringComparison]::Ordinal)) { "settings" }
		elseif ($surfaceVariant.StartsWith("dialog-", [StringComparison]::Ordinal) -or
			$surfaceVariant.StartsWith("screen-share-editor", [StringComparison]::Ordinal)) { "product-dialog" }
		elseif ($surfaceVariant -eq "manual-plugin") { "manual-plugin" }
		elseif ($surfaceVariant -eq "attachment-viewer") { "attachment-viewer" }
		elseif ($surfaceVariant -eq "image-viewer") { "image-viewer" }
		elseif ($surfaceVariant.StartsWith("ptt-", [StringComparison]::Ordinal)) { "ptt" }
		elseif ($surfaceVariant.StartsWith("screen-share-view-", [StringComparison]::Ordinal)) { "screen-share" }
		elseif ($surfaceVariant.StartsWith("media-detached-", [StringComparison]::Ordinal)) { "media-session" }
		else { "main" }
	if ($captureWindow -ne $expectedCaptureWindow) {
		throw "Case '$($case.id)' targeted window '$captureWindow'; expected '$expectedCaptureWindow'."
	}
	# Main-window sizing has a dedicated setHostViewport handshake below that
	# waits for both the logical dimensions and the navigation endpoint. Windows
	# can report the previous width in this earlier state-injection response while
	# the resize is still being presented, so defer only the main-window size
	# assertion to that stronger handshake. Separate windows have no later
	# viewport contract and must still match here.
	$appliedSizeMatches = $captureWindow -eq "main" -or
		([int]$applied.width -eq [int]$case.width -and [int]$applied.height -eq [int]$case.height)
	if ([string]$applied.case_id -ne [string]$case.id -or [string]$applied.state -ne [string]$case.state -or
		[string]$applied.theme -ne [string]$case.theme -or [string]$applied.layout -ne [string]$case.layout -or
		[string]$applied.density -ne $density -or
		[string]$applied.motd_variant -ne $motdVariant -or
		[string]$applied.rich_preview_variant -ne $richPreviewVariant -or
		[string]$applied.rich_preview_size -ne $richPreviewSize -or
		[string]$applied.presentation_family -ne $presentationFamily -or
		[string]$applied.case_variant -ne $caseVariant -or
		[string]$applied.surface_variant -ne $surfaceVariant -or
		[bool]$applied.surface_present -ne ($surfaceVariant -ne "none") -or
		-not $appliedSizeMatches -or
		[Math]::Abs([double]$applied.actual_device_pixel_ratio - $actualDevicePixelRatio) -gt 0.001) {
		$appliedJson = $applied | ConvertTo-Json -Compress -Depth 10
		throw "Automation did not apply visual case '$($case.id)' exactly. Expected $([int]$case.width)x$([int]$case.height) at DPR $actualDevicePixelRatio; applied: $appliedJson"
	}
	$isSparseChatSurface = $surfaceVariant -like "chat-sparse-*"
	$expectedMessageCount = if ([string]$case.state -ne "connected") { 0 } else {
		switch ($surfaceVariant) {
			"chat-message-states" { 4 }
			"chat-attachment-states" { 1 }
			"chat-history-prepend-anchor" { 30 }
			"chat-sparse-0" { 0 }
			"chat-sparse-1" { 1 }
			"chat-sparse-2" { 2 }
			"chat-sparse-5" { 5 }
			"chat-sparse-10" { 10 }
			"chat-sparse-stonks-5" { 5 }
			default { 2 }
		}
	}
	if ([int]$applied.message_count -ne $expectedMessageCount) {
		throw "Visual case '$($case.id)' exposed $($applied.message_count) timeline messages; expected $expectedMessageCount."
	}
	if ($surfaceVariant.StartsWith("chat-", [StringComparison]::Ordinal)) {
		$chatState = $applied.chat_fixture_state
		$requiredChatStateProperties = @(
			"variant", "message_count", "reply_message_count", "reaction_message_count",
			"sending_message_count", "failed_retry_message_count", "deleted_message_count",
			"ready_attachment_count", "loading_attachment_count", "error_attachment_count",
			"composer_text", "composer_has_pending_reply", "composer_reply_actor", "composer_reply_snippet",
			"composer_attachment_count", "composer_uploading_count", "composer_failed_count",
			"composer_autocomplete_count", "composer_upload_progress_percent", "prepend_count",
			"anchor_id", "anchor_before_row", "anchor_after_row", "anchor_before_offset",
			"anchor_after_offset", "anchor_offset_delta", "anchor_preserved", "pure_prepend_applied",
			"prepend_pending_started", "prepend_pending_settled",
			"prepend_scope_token_unchanged", "prepend_generation_unchanged",
			"prepend_queue_pending", "prepend_anchor_active", "prepend_presentation_busy",
			"prepend_presentation_pending", "prepend_presentation_finalizing",
			"prepend_scope_reset_pending", "prepend_stable_frame_committed",
			"prepend_visible_frame_committed", "prepend_history_loader_visible",
			"prepend_history_loader_height", "prepend_jump_to_latest_visible",
			"prepend_composer_height", "prepend_composer_contiguous",
			"prepend_exact_message_order",
			"prepend_start_frame_serial", "prepend_first_committed_frame_serial",
			"prepend_second_committed_frame_serial",
			"prepend_matching_committed_frame_count",
			"prepend_stable_duration_ms"
		)
		foreach ($property in $requiredChatStateProperties) {
			if ($null -eq $chatState -or -not ($chatState.PSObject.Properties.Name -contains $property)) {
				throw "Chat fixture '$($case.id)' lacks normalized state '$property'."
			}
		}
		if ([string]$chatState.variant -ne $surfaceVariant -or
			[int]$chatState.message_count -ne $expectedMessageCount) {
			$chatJson = $chatState | ConvertTo-Json -Compress -Depth 8
			throw "Chat fixture '$($case.id)' returned stale variant or message-count state: $chatJson"
		}

		$expectedNumericState = switch ($surfaceVariant) {
			"chat-message-states" { @{
				reply_message_count = 1; reaction_message_count = 1; sending_message_count = 1
				failed_retry_message_count = 1; deleted_message_count = 1
				ready_attachment_count = 0; loading_attachment_count = 0; error_attachment_count = 0
				composer_attachment_count = 0; composer_uploading_count = 0; composer_failed_count = 0
				composer_autocomplete_count = 0; composer_upload_progress_percent = 0; prepend_count = 0
			} }
			"chat-composer-states" { @{
				reply_message_count = 0; reaction_message_count = 0; sending_message_count = 0
				failed_retry_message_count = 0; deleted_message_count = 0
				ready_attachment_count = 0; loading_attachment_count = 0; error_attachment_count = 0
				composer_attachment_count = 2; composer_uploading_count = 1; composer_failed_count = 1
				composer_autocomplete_count = 1; composer_upload_progress_percent = 54; prepend_count = 0
			} }
			"chat-attachment-states" { @{
				reply_message_count = 0; reaction_message_count = 0; sending_message_count = 0
				failed_retry_message_count = 0; deleted_message_count = 0
				ready_attachment_count = 1; loading_attachment_count = 1; error_attachment_count = 1
				composer_attachment_count = 0; composer_uploading_count = 0; composer_failed_count = 0
				composer_autocomplete_count = 0; composer_upload_progress_percent = 0; prepend_count = 0
			} }
			"chat-history-prepend-anchor" { @{
				reply_message_count = 0; reaction_message_count = 0; sending_message_count = 0
				failed_retry_message_count = 0; deleted_message_count = 0
				ready_attachment_count = 0; loading_attachment_count = 0; error_attachment_count = 0
				composer_attachment_count = 0; composer_uploading_count = 0; composer_failed_count = 0
				composer_autocomplete_count = 0; composer_upload_progress_percent = 0; prepend_count = 6
			} }
			default {
				if ($isSparseChatSurface) { @{
					reply_message_count = 0; reaction_message_count = 0; sending_message_count = 0
					failed_retry_message_count = 0; deleted_message_count = 0
					ready_attachment_count = 0; loading_attachment_count = 0; error_attachment_count = 0
					composer_attachment_count = 0; composer_uploading_count = 0; composer_failed_count = 0
					composer_autocomplete_count = 0; composer_upload_progress_percent = 0; prepend_count = 0
				} } else { $null }
			}
		}
		if ($null -eq $expectedNumericState) {
			throw "Chat fixture '$($case.id)' uses unsupported surface '$surfaceVariant'."
		}
		foreach ($property in $expectedNumericState.Keys) {
			if ([int]$chatState.$property -ne [int]$expectedNumericState[$property]) {
				$chatJson = $chatState | ConvertTo-Json -Compress -Depth 8
				throw "Chat fixture '$($case.id)' returned unexpected '$property': $chatJson"
			}
		}

		switch ($surfaceVariant) {
			"chat-message-states" {
				if (-not [string]::IsNullOrEmpty([string]$chatState.composer_text) -or
					-not [string]::IsNullOrEmpty([string]$chatState.composer_reply_actor)) {
					throw "Message-state fixture '$($case.id)' leaked composer state."
				}
			}
			"chat-composer-states" {
				if (-not [bool]$chatState.composer_has_pending_reply -or
					[string]$chatState.composer_text -ne "@Al" -or
					[string]$chatState.composer_reply_actor -ne "Alex" -or
					[string]$chatState.composer_reply_snippet -ne
						"Use the community test build once this check passes.") {
					$chatJson = $chatState | ConvertTo-Json -Compress -Depth 8
					throw "Composer fixture '$($case.id)' did not preserve its typed draft/reply state: $chatJson"
				}
				if ([string]$applied.focus_target -ne "visualFixtureComposer") {
					throw "Composer fixture '$($case.id)' focused '$($applied.focus_target)' instead of the composer."
				}
			}
			"chat-history-prepend-anchor" {
				$expectedAnchor = [string]@($applied.fixture_message_ids)[17]
				$reportedOffsetDelta = [double]$chatState.anchor_offset_delta
				$calculatedOffsetDelta = [Math]::Abs(
					[double]$chatState.anchor_after_offset - [double]$chatState.anchor_before_offset)
				if ([string]$chatState.anchor_id -ne $expectedAnchor -or
					[int]$chatState.anchor_before_row -ne 11 -or [int]$chatState.anchor_after_row -ne 17 -or
					-not [bool]$chatState.anchor_preserved -or -not [bool]$chatState.pure_prepend_applied -or
					$reportedOffsetDelta -lt 0 -or $reportedOffsetDelta -gt 0.5 -or
					-not [bool]$chatState.prepend_pending_started -or
					-not [bool]$chatState.prepend_pending_settled -or
					-not [bool]$chatState.prepend_scope_token_unchanged -or
					-not [bool]$chatState.prepend_generation_unchanged -or
					[bool]$chatState.prepend_queue_pending -or
					[bool]$chatState.prepend_anchor_active -or
					[bool]$chatState.prepend_presentation_busy -or
					[bool]$chatState.prepend_presentation_pending -or
					[bool]$chatState.prepend_presentation_finalizing -or
					[bool]$chatState.prepend_scope_reset_pending -or
					-not [bool]$chatState.prepend_stable_frame_committed -or
					-not [bool]$chatState.prepend_visible_frame_committed -or
					-not [bool]$chatState.prepend_history_loader_visible -or
					[Math]::Abs([double]$chatState.prepend_history_loader_height - 48.0) -gt 1.0 -or
					-not [bool]$chatState.prepend_jump_to_latest_visible -or
					[double]$chatState.prepend_composer_height -le 1.0 -or
					-not [bool]$chatState.prepend_composer_contiguous -or
					-not [bool]$chatState.prepend_exact_message_order -or
					[long]$chatState.prepend_first_committed_frame_serial -le
						[long]$chatState.prepend_start_frame_serial -or
					[long]$chatState.prepend_second_committed_frame_serial -le
						[long]$chatState.prepend_first_committed_frame_serial -or
					[int]$chatState.prepend_matching_committed_frame_count -lt 2 -or
					[double]$chatState.prepend_stable_duration_ms -gt 300.0 -or
					[Math]::Abs($reportedOffsetDelta - $calculatedOffsetDelta) -gt 0.001) {
					$chatJson = $chatState | ConvertTo-Json -Compress -Depth 8
					throw "History fixture '$($case.id)' did not execute a stable six-row production prepend: $chatJson"
				}
			}
		}
	}
	$expectMotd = $motdVariant -ne "none"
	$expectMotdExpanded = $expectMotd -and $motdVariant -ne "collapsed"
	$expectMotdChanged = $motdVariant -eq "changed"
	$expectUserHistory = [string]$case.state -eq "connected" -and
		$motdVariant -in @("none", "history-visible") -and $expectedMessageCount -gt 0
	$expectMotdVisible = $expectMotd
	if ([bool]$applied.motd_present -ne $expectMotd -or
		[bool]$applied.motd_expanded -ne $expectMotdExpanded -or
		[bool]$applied.motd_changed -ne $expectMotdChanged -or
		[bool]$applied.motd_has_user_history -ne $expectUserHistory -or
		[bool]$applied.motd_visible -ne $expectMotdVisible) {
		$appliedJson = $applied | ConvertTo-Json -Compress -Depth 10
		throw "Automation did not establish MOTD variant '$motdVariant' for '$($case.id)': $appliedJson"
	}
	$expectRichPreview = $richPreviewVariant -ne "none"
	$expectDeferredRichPreviewShell = $richPreviewVariant -in @("loading", "error")
	$expectedRichPreviewAspect = switch ($richPreviewVariant) {
		"youtube" { "wide" }
		"spotify" { "compact-audio" }
		"tiktok" { "short" }
		"vimeo" { "wide" }
		"dailymotion" { "wide" }
		"soundcloud" { "compact-audio" }
		"instagram" { "square" }
		"twitch" { "wide" }
		default { "" }
	}
	$expectRichPreviewImage = $richPreviewVariant -in @(
		"youtube", "spotify", "tiktok", "vimeo", "dailymotion", "soundcloud", "instagram",
		"audio", "product", "steam", "twitch", "vehicle", "property", "marketplace", "direct-media"
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
	if ($captureWindow -eq "main") {
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
	} elseif ($navigationOpen) {
		throw "Separate-window case '$($case.id)' cannot request the main navigation drawer."
	}

	# Message models are populated synchronously, but the production timeline keeps
	# rich rows behind its bounded presentation surface while delegates hydrate and
	# the tail anchor settles. A visually stable loading pane is not the requested
	# case, so wait for that production handshake before accepting any main-window
	# capture instead of relying on unrelated earlier matrix cases for timing.
	$requiresProductionTimelineHandshake = $captureWindow -eq "main" -and
		[string]$case.state -eq "connected"
	$timelinePresentation = $null
	$timelineHandshakeDurationMs = $null
	$containsRichOrAttachmentContent = $richPreviewVariant -ne "none" -or
		$caseVariant -ne "none" -or $surfaceVariant -eq "chat-attachment-states"
	# Static fixture content is already terminal when it is injected. The extended
	# allowance is available only to a case that proves a real uncached transition.
	$extendedPresentationBudgetExercised = $false
	$timelinePresentationBudgetMs = if ($extendedPresentationBudgetExercised) { 1000.0 } else { 300.0 }
	$applyRpcBudgetMs = 1000.0
	$presentationTimingDiagnosticPath = $null
	if ($requiresProductionTimelineHandshake) {
		$expectedFixtureScopeToken = "0:$([long]1000000 + [long]$applied.generation)"
		$expectedFixtureGeneration = [long]$preApplyTimelineGeneration + 1
		$expectedFixtureStableIdPrefix = [string]$applied.fixture_message_id_prefix
		$expectedFixtureStableIds = @($applied.fixture_message_ids)
		$presentationTimingDiagnostic = [ordered]@{
			case_id = [string]$case.id
			status = "pending"
			presentation_classification = if ($extendedPresentationBudgetExercised) { "uncached" }
				elseif ($containsRichOrAttachmentContent) { "cached-rich" } else { "cached-simple" }
			visible_rows_budget_ms = $timelinePresentationBudgetMs
			loading_feedback_budget_ms = 100.0
			composer_interactive_budget_ms = 500.0
			prepend_stable_budget_ms = 300.0
			apply_rpc_budget_ms = $applyRpcBudgetMs
			uncached_budget_ms = 1000.0
			uncached_exercised = $extendedPresentationBudgetExercised
			apply_duration_ms = $applyDurationMs
			controller_wait_ms = [double]$applied.timeline_presentation_wait_ms
			pre_apply_generation = $preApplyTimelineGeneration
			expected_scope_token = $expectedFixtureScopeToken
			expected_generation = $expectedFixtureGeneration
			expected_stable_ids = $expectedFixtureStableIds
			applied_identity = [ordered]@{
				scope_token = [string]$applied.fixture_scope_token
				presentation_generation = [long]$applied.presentation_generation
				stable_id_prefix = [string]$applied.fixture_message_id_prefix
				stable_ids = @($applied.fixture_message_ids)
			}
			timeline_handshake_duration_ms = $null
			timeline = $null
		}
		if ([string]$applied.fixture_scope_token -cne $expectedFixtureScopeToken -or
			[long]$applied.presentation_generation -ne $expectedFixtureGeneration -or
			$expectedFixtureGeneration -le 0 -or
			($surfaceVariant -ne "chat-history-prepend-anchor" -and
				[string]::IsNullOrWhiteSpace($expectedFixtureStableIdPrefix)) -or
			$expectedFixtureStableIds.Count -ne $expectedMessageCount) {
			$presentationTimingDiagnostic["status"] = "identity_invalid"
			$presentationTimingDiagnosticPath = Write-QmlPresentationTimingDiagnostic `
				-OutputDirectory $output -CaseId ([string]$case.id) `
				-Diagnostic $presentationTimingDiagnostic
			throw "Case '$($case.id)' did not return a complete production timeline identity."
		}
		$timelinePresentationReady = $false
		$timelineHandshakeTimer = [Diagnostics.Stopwatch]::StartNew()
		$timelinePresentationDeadline = [DateTime]::UtcNow.AddSeconds(5)
		do {
			$timelineResponse = Invoke-Automation @{
				command = "qmlTimelinePresentationState"
			}
			$timelinePresentation = $timelineResponse.timeline
			$timelineIdentityReady = Test-QmlTimelineFixtureIdentity `
				-Timeline $timelinePresentation `
				-ExpectedScopeToken $expectedFixtureScopeToken `
				-ExpectedGeneration $expectedFixtureGeneration `
				-ExpectedStableIdPrefix $expectedFixtureStableIdPrefix `
				-ExpectedStableIds $expectedFixtureStableIds `
				-ExpectedMessageCount $expectedMessageCount
			$sparseTimelineReady = $true
			if ($isSparseChatSurface) {
				if ($expectedMessageCount -eq 0) {
					$sparseTimelineReady = [bool]$timelinePresentation.emptyConversationVisible
				} else {
					$sparseTimelineReady = [bool]$timelinePresentation.presentationFastPath -and
						[bool]$timelinePresentation.sparseBottomAlignmentActive -and
						[double]$timelinePresentation.sparseBottomAlignmentError -le 2.0 -and
						[double]$timelinePresentation.sparseTailContentYError -le 0.5 -and
						[Math]::Abs([double]$timelinePresentation.visibleTailGap -
							[double]$timelinePresentation.footerHeight) -le 2.0 -and
						[int]$timelinePresentation.pendingHydrationCount -eq 0
				}
			}
			$timelineGeometryReady = Test-QmlTimelineViewportGeometry `
				-Timeline $timelinePresentation `
				-RequireCustomHeader ($surfaceVariant -eq "chat-sparse-stonks-5") `
				-RequireHistoryPrependLayout `
					($surfaceVariant -eq "chat-history-prepend-anchor")
			$timelinePresentationReady = $null -ne $timelinePresentation -and
				$timelineIdentityReady -and
				[int]$timelinePresentation.count -eq $expectedMessageCount -and
				-not [bool]$timelinePresentation.presentationPending -and
				-not [bool]$timelinePresentation.presentationFinalizing -and
				-not [bool]$timelinePresentation.scopeResetPending -and
				[bool]$timelinePresentation.stablePresentationReady -and
				[bool]$timelinePresentation.firstStableFrameCommitted -and
				[bool]$timelinePresentation.secondMatchingStableFrameCommitted -and
				[bool]$timelinePresentation.firstVisibleStableRowsCommitted -and
				[bool]$timelinePresentation.stableFrameCommitted -and
				[bool]$timelinePresentation.visibleFrameCommitted -and
				[bool]$timelinePresentation.loadingFrameCommitted -and
				[bool]$timelinePresentation.composerInteractive -and
				[int]$timelinePresentation.stableFrameConsecutiveCount -ge 2 -and
				[bool]$timelinePresentation.settled -and
				[int]$timelinePresentation.visibleDeferredSummaryCount -eq 0 -and
				([int]$timelinePresentation.visibleDeadlineFallbackCount -eq 0 -or
					[bool]$timelinePresentation.degradedReady) -and
				[int]$timelinePresentation.visibleBlankPresentationCount -eq 0 -and
				[int]$timelinePresentation.visibleBlankPreviewSurfaceCount -eq 0 -and
				[int]$timelinePresentation.visibleDuplicateHandoffSummaryCount -eq 0 -and
				[int]$timelinePresentation.visiblePresentationHeightPendingCount -eq 0 -and
				[int]$timelinePresentation.visiblePresentationHeightMismatchCount -eq 0 -and
				[double]$timelinePresentation.maximumVisiblePresentationHeightError -le 1.0 -and
				[double]$timelinePresentation.composerHeight -gt 1.0 -and
				$sparseTimelineReady -and $timelineGeometryReady
			if (-not $timelinePresentationReady) {
				Start-Sleep -Milliseconds 25
			}
		} while (-not $timelinePresentationReady -and
			[DateTime]::UtcNow -lt $timelinePresentationDeadline)
		$timelineHandshakeTimer.Stop()
		$timelineHandshakeDurationMs = [Math]::Round(
			$timelineHandshakeTimer.Elapsed.TotalMilliseconds, 3)
		$presentationTimingDiagnostic["timeline_handshake_duration_ms"] =
			$timelineHandshakeDurationMs
		$presentationTimingDiagnostic["timeline"] = $timelinePresentation
		if (-not $timelinePresentationReady) {
			$presentationTimingDiagnostic["status"] = "presentation_timeout"
			$presentationTimingDiagnosticPath = Write-QmlPresentationTimingDiagnostic `
				-OutputDirectory $output -CaseId ([string]$case.id) `
				-Diagnostic $presentationTimingDiagnostic
			$timelineJson = $timelinePresentation | ConvertTo-Json -Compress -Depth 8
			throw "Case '$($case.id)' did not finish its production timeline presentation: $timelineJson"
		}
		$providerResultTransitionExercised =
			($applied.PSObject.Properties.Name -contains "provider_result_transition_exercised") -and
			[bool]$applied.provider_result_transition_exercised
		$expectsProviderResultReceipt = $providerResultTransitionExercised
		if ($expectsProviderResultReceipt -and
			([double]$timelinePresentation.providerResultAppliedAt -le 0 -or
				[double]$timelinePresentation.providerResultVisiblePaintAt -le 0)) {
			$providerReceiptDeadline = [DateTime]::UtcNow.AddSeconds(2)
			do {
				$providerTimelineResponse = Invoke-Automation @{
					command = "qmlTimelinePresentationState"
				}
				$timelinePresentation = $providerTimelineResponse.timeline
				if ([double]$timelinePresentation.providerResultAppliedAt -gt 0 -and
					[double]$timelinePresentation.providerResultVisiblePaintAt -gt 0) {
					break
				}
				Start-Sleep -Milliseconds 25
			} while ([DateTime]::UtcNow -lt $providerReceiptDeadline)
		}
		$prependStableDurationMs = if ($surfaceVariant -eq "chat-history-prepend-anchor") {
			[double]$chatState.prepend_stable_duration_ms
		} else { -1.0 }
		$budgetFailures = @(Get-QmlPresentationBudgetFailures `
			-Timeline $timelinePresentation `
			-ExtendedPresentationBudgetExercised $extendedPresentationBudgetExercised `
			-RequireComposer $true `
			-PrependStableDurationMs $prependStableDurationMs)
		if ($expectsProviderResultReceipt -and
			([double]$timelinePresentation.providerResultAppliedAt -le 0 -or
				[double]$timelinePresentation.providerResultVisiblePaintAt -lt
					[double]$timelinePresentation.providerResultAppliedAt)) {
			$budgetFailures += "provider_result_visible_paint"
		}
		if ($richPreviewVariant -eq "error" -and -not [bool]$timelinePresentation.degradedReady) {
			$budgetFailures += "degraded_ready"
		} elseif ($richPreviewVariant -notin @("none", "loading", "error") -and
			-not [bool]$timelinePresentation.richHydrated) {
			$budgetFailures += "rich_hydrated"
		}
		$budgetFailures = @($budgetFailures | Select-Object -Unique)
		if ($budgetFailures.Count -gt 0) {
			$presentationTimingDiagnostic["status"] = "milestone_budget_failed"
			$presentationTimingDiagnostic["failures"] = $budgetFailures
			$presentationTimingDiagnostic["timeline"] = $timelinePresentation
			$presentationTimingDiagnosticPath = Write-QmlPresentationTimingDiagnostic `
				-OutputDirectory $output -CaseId ([string]$case.id) `
				-Diagnostic $presentationTimingDiagnostic
			throw "Case '$($case.id)' failed readiness milestones: $($budgetFailures -join ', '): " +
				($timelinePresentation | ConvertTo-Json -Compress -Depth 8)
		}
		$presentationTimingDiagnostic["status"] = "passed"
		$presentationTimingDiagnostic["failures"] = @()
		$presentationTimingDiagnostic["timeline"] = $timelinePresentation
		$presentationTimingDiagnosticPath = Write-QmlPresentationTimingDiagnostic `
			-OutputDirectory $output -CaseId ([string]$case.id) `
			-Diagnostic $presentationTimingDiagnostic
	}

	$richBodyState = $null
	if ($caseVariant -eq "rich-image-link") {
		$richBodyMessageId = [string]$applied.rich_body_message_id
		if ([string]::IsNullOrWhiteSpace($richBodyMessageId)) {
			throw "Rich-message case '$($case.id)' returned no body-message identity."
		}
		$expectedImageHref = "https://example.com/qt-quick-release"
		$expectedImageLabel = "Qt Quick release artwork"
		$richBodyDeadline = [DateTime]::UtcNow.AddSeconds(8)
		$richBodyReady = $false
		do {
			$bodyResponse = Invoke-Automation @{
				command = "qmlVisualGateRichBodyState"
				generation = $applied.generation
				messageId = $richBodyMessageId
			}
			$richBodyState = $bodyResponse.body
			$modelImages = @($richBodyState.modelImages)
			$matchingModelImages = @($modelImages | Where-Object {
				[string]$_.kind -eq "image" -and [string]$_.alt -eq $expectedImageLabel -and
				[string]$_.href -eq $expectedImageHref -and
				([string]$_.source).StartsWith("image://mumble/", [StringComparison]::OrdinalIgnoreCase)
			})
			$modelImageSource = if ($matchingModelImages.Count -eq 1) {
				[string]$matchingModelImages[0].source
			} else { "" }
			$cardInsideTimeline = [bool]$richBodyState.cardVisible -and
				[double]$richBodyState.cardWidth -gt 0 -and [double]$richBodyState.cardHeight -gt 0 -and
				[bool]$richBodyState.timelineVisible -and [double]$richBodyState.timelineWidth -gt 0 -and
				[double]$richBodyState.timelineHeight -gt 0 -and
				[double]$richBodyState.cardX -ge [double]$richBodyState.timelineX - 1 -and
				([double]$richBodyState.cardX + [double]$richBodyState.cardWidth) -le
					([double]$richBodyState.timelineX + [double]$richBodyState.timelineWidth + 1) -and
				[double]$richBodyState.cardY -ge [double]$richBodyState.timelineY - 1 -and
				([double]$richBodyState.cardY + [double]$richBodyState.cardHeight) -le
					([double]$richBodyState.timelineY + [double]$richBodyState.timelineHeight + 1)
			$imageRect = $richBodyState.imageVisibleSceneRect
			$richBodyReady = [bool]$richBodyState.modelPresent -and
				[int]$richBodyState.modelImageCount -eq 1 -and $matchingModelImages.Count -eq 1 -and
				[bool]$richBodyState.rendered -and $cardInsideTimeline -and
				[string]$richBodyState.cardHref -eq $expectedImageHref -and
				[string]$richBodyState.cardLabel -eq $expectedImageLabel -and
				[bool]$richBodyState.imagePresent -and [bool]$richBodyState.imageEffectiveVisible -and
				[string]$richBodyState.imageStatusName -eq "ready" -and
				[string]$richBodyState.imageSource -eq $modelImageSource -and
				[double]$imageRect.width -gt 0 -and [double]$imageRect.height -gt 0
			if (-not $richBodyReady) { Start-Sleep -Milliseconds 25 }
		} while (-not $richBodyReady -and [DateTime]::UtcNow -lt $richBodyDeadline)
		if (-not $richBodyReady) {
			$bodyJson = $richBodyState | ConvertTo-Json -Compress -Depth 10
			throw "Rich-message case '$($case.id)' did not finish its model parse and live image delegate: $bodyJson"
		}
	}

	$richPreviewCardState = $null
	if ($expectRichPreview -and -not $expectDeferredRichPreviewShell -and -not $isMediaSurface) {
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
			"x" {
				[pscustomobject]@{ variant = "x"; token = "x"; family = "social"; presentation = "socialPost" }
				break
			}
			"github" {
				[pscustomobject]@{ variant = "github"; token = "github"; family = "social"; presentation = "identity" }
				break
			}
			"vehicle" {
				[pscustomobject]@{ variant = "vehicle"; token = "bytbil"; family = "commerce"; presentation = "commerce" }
				break
			}
			"property" {
				[pscustomobject]@{ variant = "realEstate"; token = "hemnet"; family = "commerce"; presentation = "commerce" }
				break
			}
			"article" {
				[pscustomobject]@{ variant = "article"; token = "svt"; family = "editorial"; presentation = "details" }
				break
			}
			"marketplace" {
				[pscustomobject]@{ variant = "marketplace"; token = "blocket"; family = "commerce"; presentation = "commerce" }
				break
			}
			"weather" {
				[pscustomobject]@{ variant = "weather"; token = "smhi"; family = "geo"; presentation = "details" }
				break
			}
			"place" {
				[pscustomobject]@{ variant = "place"; token = "openstreetmap"; family = "geo"; presentation = "details" }
				break
			}
			"traffic" {
				[pscustomobject]@{ variant = "traffic"; token = "trafikverket"; family = "geo"; presentation = "details" }
				break
			}
			"link-digest" {
				[pscustomobject]@{ variant = "linkDigest"; token = "existenz"; family = "editorial"; presentation = "details" }
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
			$cardProperty = $previewResponse.PSObject.Properties["card"]
			$richPreviewCardState = if ($null -ne $cardProperty) { $cardProperty.Value } else { $null }
			$requiredRichPreviewStateProperties = @(
				"cardVisible", "cardWidth", "cardHeight", "cardX", "cardY",
				"timelineVisible", "timelineWidth", "timelineHeight", "timelineX", "timelineY",
				"previewState", "compact", "expanded", "userExpanded", "rendered",
				"visibleImages", "visibleImageCount", "imageReadyCount", "imageLoadingCount", "imageErrorCount",
				"mediaVisible", "mediaWidth", "mediaHeight", "mediaX", "mediaY", "playVisible",
				"documentGalleryVisible", "documentGalleryPaintedSurface",
				"documentGalleryCurrentVisualStatusName", "documentViewportVisible",
				"documentViewportWidth", "documentViewportHeight", "documentViewportX", "documentViewportY",
				"documentPlaybackPromptVisible",
				"openSurfaceVisible", "providerDetailsVisible", "providerVariant", "providerToken",
				"providerFamily", "providerPresentation"
			)
			$missingRichPreviewStateProperties = @(if ($null -eq $richPreviewCardState) {
				$requiredRichPreviewStateProperties
			} else {
				$requiredRichPreviewStateProperties | Where-Object {
					-not ($richPreviewCardState.PSObject.Properties.Name -contains $_)
				}
			})
			if ($missingRichPreviewStateProperties.Count -gt 0) {
				Start-Sleep -Milliseconds 25
				continue
			}
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
			$inlineMediaStageExpected = $richPreviewVariant -eq "direct-media" -or
				-not [string]::IsNullOrWhiteSpace([string]$applied.rich_preview_embed_provider)
			$expectedImageObjectNames = if ($inlineMediaStageExpected) {
				@("previewEmbedPoster", "embedDocumentMediaImage")
			} elseif ($richPreviewVariant -eq "steam") { @("providerSteamHeroImage") }
				elseif ($richPreviewVariant -in @(
					"product", "vehicle", "property", "marketplace"
				)) { @("providerCommerceHeroImage") }
				elseif ($richPreviewVariant -eq "article") { @("providerArticleHeroImage") }
				else { @("previewCompactImage") }
			$matchingVisibleImages = @($visibleImages | Where-Object {
				[string]$_.objectName -in $expectedImageObjectNames -and [string]$_.statusName -eq "ready" -and
				[bool]$_.effectiveVisible -and [bool]$_.intersectsCard -and
				[double]$_.visibleSceneRect.width -gt 0 -and [double]$_.visibleSceneRect.height -gt 0
			})
			$imageReady = -not $expectRichPreviewImage -or
				([int]$richPreviewCardState.visibleImageCount -eq $visibleImages.Count -and
				 [int]$richPreviewCardState.imageReadyCount -eq $visibleImages.Count -and
				 [int]$richPreviewCardState.imageLoadingCount -eq 0 -and
				 [int]$richPreviewCardState.imageErrorCount -eq 0 -and
				 $matchingVisibleImages.Count -ge 1)
			$legacyMediaInsideCard = [bool]$richPreviewCardState.mediaVisible -and
				 [double]$richPreviewCardState.mediaWidth -gt 0 -and [double]$richPreviewCardState.mediaHeight -gt 0 -and
				 [double]$richPreviewCardState.mediaX -ge [double]$richPreviewCardState.cardX - 1 -and
				 ([double]$richPreviewCardState.mediaX + [double]$richPreviewCardState.mediaWidth) -le
					([double]$richPreviewCardState.cardX + [double]$richPreviewCardState.cardWidth + 1) -and
				 [double]$richPreviewCardState.mediaY -ge [double]$richPreviewCardState.cardY - 1 -and
				 ([double]$richPreviewCardState.mediaY + [double]$richPreviewCardState.mediaHeight) -le
					([double]$richPreviewCardState.cardY + [double]$richPreviewCardState.cardHeight + 1)
			$typedProviderDocumentInsideCard = [bool]$richPreviewCardState.documentGalleryVisible -and
				[bool]$richPreviewCardState.documentViewportVisible -and
				[double]$richPreviewCardState.documentViewportWidth -gt 0 -and
				[double]$richPreviewCardState.documentViewportHeight -gt 0 -and
				[double]$richPreviewCardState.documentViewportX -ge [double]$richPreviewCardState.cardX - 1 -and
				([double]$richPreviewCardState.documentViewportX +
				 [double]$richPreviewCardState.documentViewportWidth) -le
					([double]$richPreviewCardState.cardX + [double]$richPreviewCardState.cardWidth + 1) -and
				[double]$richPreviewCardState.documentViewportY -ge [double]$richPreviewCardState.cardY - 1 -and
				([double]$richPreviewCardState.documentViewportY +
				 [double]$richPreviewCardState.documentViewportHeight) -le
					([double]$richPreviewCardState.cardY + [double]$richPreviewCardState.cardHeight + 1)
			$typedProviderDocumentReady = $typedProviderDocumentInsideCard -and
				[bool]$richPreviewCardState.documentGalleryPaintedSurface -and
				[bool]$richPreviewCardState.documentPlaybackPromptVisible -and
				[string]$richPreviewCardState.documentGalleryCurrentVisualStatusName -eq "ready"
			$mediaInsideCard = -not $inlineMediaStageExpected -or
				$legacyMediaInsideCard -or $typedProviderDocumentInsideCard
			$embedReady = -not $inlineMediaStageExpected -or
				($mediaInsideCard -and
				 ([bool]$richPreviewCardState.playVisible -or $typedProviderDocumentReady))
			$openSurfaceReady = if ($inlineMediaStageExpected) {
				-not [bool]$richPreviewCardState.openSurfaceVisible
			} else {
				[bool]$richPreviewCardState.openSurfaceVisible
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
	$minimumMainGridNonBlackFraction = 0.10
	$requiredStableFrameMatches = 2
	$minimumStableFrameDurationMs = 300
	# A capture RPC can legitimately consume most of its first five-second budget
	# while several software-rendered matrix shards are active. Preserve the strict
	# five-second stability window by starting it only after the first correctly
	# sized, non-black frame is observable; the initial frame itself remains bounded.
	$initialCaptureDeadline = [DateTime]::UtcNow.AddSeconds(5)
	$captureDeadline = $initialCaptureDeadline
	$stabilityDeadline = [DateTime]::MinValue
	$previousImageHash = ""
	$previousDistinctImageHash = ""
	$previousDistinctImageBytes = $null
	$lastDistinctImageHash = ""
	$lastDistinctImageBytes = $null
	$stableFrameSamples = 0
	$stableFrameSince = [DateTime]::MinValue
	$dimensions = $null
	$lastCoverage = 0.0
	$lastGridCoverage = $null
	do {
		$capture = Invoke-Automation @{
			command = "captureQml"; path = $imagePath; generation = $applied.generation; window = $captureWindow
		}
		if ([string]$capture.frontend -ne "qml" -or -not ($capture.PSObject.Properties.Name -contains "generation") -or
			[long]$capture.generation -ne [long]$applied.generation -or [string]$capture.window -ne $captureWindow) {
			throw "Case '$($case.id)' returned a stale or non-QML capture."
		}
		if (-not (Test-Path -LiteralPath $imagePath -PathType Leaf)) {
			throw "Case '$($case.id)' produced no capture."
		}
		$dimensions = Get-QmlVisualPngDimensions $imagePath
		if ($dimensions.width -ne $expectedWidth -or $dimensions.height -ne $expectedHeight) {
			# QQuickWindow::grabWindow can briefly expose the previous render target
			# after a large responsive resize even though QWindow already reports the
			# requested logical geometry. Treat that frame as not-yet-presented and
			# keep polling within the same bounded capture deadline.
			$stableFrameSamples = 0
			$previousImageHash = ""
			$stableFrameSince = [DateTime]::MinValue
			Start-Sleep -Milliseconds 25
			continue
		}
		$coverage = Get-QmlVisualPngCoverage $imagePath
		$lastCoverage = [double]$coverage.non_black_fraction
		$lastGridCoverage = if ($captureWindow -eq "main") {
			Get-QmlVisualPngGridCoverage -Path $imagePath -Columns 3 -Rows 3
		} else { $null }
		$mainGridComplete = $null -eq $lastGridCoverage -or
			[double]$lastGridCoverage.minimum_non_black_fraction -ge $minimumMainGridNonBlackFraction
		if ($lastCoverage -ge $minimumNonBlackFraction -and $mainGridComplete) {
			if ($stabilityDeadline -eq [DateTime]::MinValue) {
				$stabilityDeadline = [DateTime]::UtcNow.AddSeconds(5)
				$captureDeadline = $stabilityDeadline
			}
			$currentImageHash = Get-QmlVisualFileSha256 $imagePath
			if ($currentImageHash -ceq $previousImageHash) {
				++$stableFrameSamples
			} else {
				$stableFrameSamples = 0
				$previousDistinctImageHash = $lastDistinctImageHash
				$previousDistinctImageBytes = $lastDistinctImageBytes
				$lastDistinctImageHash = $currentImageHash
				$lastDistinctImageBytes = [IO.File]::ReadAllBytes($imagePath)
				$previousImageHash = $currentImageHash
				$stableFrameSince = [DateTime]::UtcNow
			}
			$stableFrameDurationMs = if ($stableFrameSince -eq [DateTime]::MinValue) { 0 }
				else { ([DateTime]::UtcNow - $stableFrameSince).TotalMilliseconds }
			if ($stableFrameSamples -ge $requiredStableFrameMatches -and
				$stableFrameDurationMs -ge $minimumStableFrameDurationMs) { break }
		} else {
			$stableFrameSamples = 0
			$previousImageHash = ""
			$stableFrameSince = [DateTime]::MinValue
		}
		Start-Sleep -Milliseconds 25
	} while ([DateTime]::UtcNow -lt $captureDeadline)
	$stableFrameDurationMs = if ($stableFrameSince -eq [DateTime]::MinValue) { 0 }
		else { ([DateTime]::UtcNow - $stableFrameSince).TotalMilliseconds }
	if ($null -eq $dimensions -or $dimensions.width -ne $expectedWidth -or
		$dimensions.height -ne $expectedHeight) {
		$observedDimensions = if ($null -eq $dimensions) { "no frame" }
			else { "$($dimensions.width)x$($dimensions.height)" }
		throw "Case '$($case.id)' did not present the requested ${expectedWidth}x${expectedHeight} render target " +
			"within five seconds (last capture $observedDimensions)."
	}
	if ($stableFrameSamples -lt $requiredStableFrameMatches -or
		$stableFrameDurationMs -lt $minimumStableFrameDurationMs) {
		$diagnosticsDirectory = Join-Path $output "diagnostics"
		New-Item -ItemType Directory -Force -Path $diagnosticsDirectory | Out-Null
		if ($null -ne $previousDistinctImageBytes) {
			$previousDiagnosticPath = Join-Path -Path $diagnosticsDirectory -ChildPath (
				"$($case.id).unstable-previous.png")
			[IO.File]::WriteAllBytes($previousDiagnosticPath, $previousDistinctImageBytes)
		}
		if ($null -ne $lastDistinctImageBytes) {
			$lastDiagnosticPath = Join-Path -Path $diagnosticsDirectory -ChildPath (
				"$($case.id).unstable-last.png")
			[IO.File]::WriteAllBytes($lastDiagnosticPath, $lastDistinctImageBytes)
		}
		$unstableFrameDiagnosticPath = Join-Path -Path $diagnosticsDirectory -ChildPath (
			"$($case.id).unstable-frame.json")
		[ordered]@{
			case_id = [string]$case.id
			previous_sha256 = $previousDistinctImageHash
			last_sha256 = $lastDistinctImageHash
			stable_samples = $stableFrameSamples
			stable_duration_ms = [Math]::Round($stableFrameDurationMs, 1)
		} | ConvertTo-Json | Set-Content -LiteralPath $unstableFrameDiagnosticPath -Encoding utf8NoBOM
		$gridCoverageText = if ($null -eq $lastGridCoverage) { "not applicable" }
			else { "$([Math]::Round([double]$lastGridCoverage.minimum_non_black_fraction * 100, 2))%" }
		throw "Case '$($case.id)' did not keep three identical non-black frames stable for " +
			"at least $minimumStableFrameDurationMs ms within five seconds " +
			"(last non-black coverage $([Math]::Round($lastCoverage * 100, 2))%, " +
			"minimum main-grid coverage $gridCoverageText; required $($minimumNonBlackFraction * 100)% global " +
			"and $($minimumMainGridNonBlackFraction * 100)% per main-window grid cell)."
	}
	$acceptedImageHash = $previousImageHash
	$acceptedImageBytes = [IO.File]::ReadAllBytes($imagePath)
	$acceptedGridCoverage = $lastGridCoverage

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
		$accessibility = Invoke-Automation @{
			command = "qmlAccessibilitySnapshot"; generation = $applied.generation; window = $captureWindow
		}
		if (($accessibility.PSObject.Properties.Name -contains "generation") -and
			[long]$accessibility.generation -eq [long]$applied.generation -and
			[string]$accessibility.window -eq $captureWindow -and $accessibility.snapshot -and
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
		-RichPreviewPlayName $richPreviewPlayName -CaseVariant $caseVariant `
		-SurfaceVariant $surfaceVariant -NavigationOpen $navigationOpen

	$finalCapture = Invoke-Automation @{
		command = "captureQml"; path = $imagePath; generation = $applied.generation; window = $captureWindow
	}
	if ([string]$finalCapture.frontend -ne "qml" -or
		-not ($finalCapture.PSObject.Properties.Name -contains "generation") -or
		[long]$finalCapture.generation -ne [long]$applied.generation -or
		[string]$finalCapture.window -ne $captureWindow) {
		throw "Case '$($case.id)' returned a stale or non-QML final capture."
	}
	$finalCoverage = Get-QmlVisualPngCoverage $imagePath
	$finalGridCoverage = if ($captureWindow -eq "main") {
		Get-QmlVisualPngGridCoverage -Path $imagePath -Columns 3 -Rows 3
	} else { $null }
	$finalMainGridComplete = $null -eq $finalGridCoverage -or
		[double]$finalGridCoverage.minimum_non_black_fraction -ge $minimumMainGridNonBlackFraction
	$finalImageHash = Get-QmlVisualFileSha256 $imagePath
	$finalPixelsExact = $finalImageHash -ceq $acceptedImageHash
	if (-not $finalPixelsExact) {
		# Qt's PNG writer may choose a different lossless scanline filter for an
		# otherwise identical frame after accessibility has queried the scene.
		# Compare decoded pixels before reporting a rendering mutation; raw PNG
		# bytes are still retained in the manifest for artifact integrity.
		$acceptedFrameCheckPath = Join-Path $output "$($case.id).accepted-frame-check.png"
		try {
			[IO.File]::WriteAllBytes($acceptedFrameCheckPath, $acceptedImageBytes)
			$frameComparison = Compare-QmlVisualPng `
				-BaselinePath $acceptedFrameCheckPath -CandidatePath $imagePath
			$finalPixelsExact = [long]$frameComparison.changed_pixels -eq 0 -and
				[int]$frameComparison.maximum_channel_delta -eq 0
		} finally {
			if (Test-Path -LiteralPath $acceptedFrameCheckPath -PathType Leaf) {
				Remove-Item -LiteralPath $acceptedFrameCheckPath -Force
			}
		}
	}
	if ([double]$finalCoverage.non_black_fraction -lt $minimumNonBlackFraction -or
		-not $finalMainGridComplete -or -not $finalPixelsExact) {
		$diagnosticsDirectory = Join-Path $output "diagnostics"
		New-Item -ItemType Directory -Force -Path $diagnosticsDirectory | Out-Null
		$acceptedDiagnosticPath = Join-Path $diagnosticsDirectory "$($case.id).accepted.png"
		$finalDiagnosticPath = Join-Path $diagnosticsDirectory "$($case.id).after-accessibility.png"
		[IO.File]::WriteAllBytes($acceptedDiagnosticPath, $acceptedImageBytes)
		Copy-Item -LiteralPath $imagePath -Destination $finalDiagnosticPath -Force
		$frameChangeDiagnostic = [ordered]@{
			case_id = [string]$case.id
			accepted_sha256 = $acceptedImageHash
			final_sha256 = $finalImageHash
			decoded_pixels_exact = $finalPixelsExact
			accepted_non_black_fraction = [double]$lastCoverage
			final_non_black_fraction = [double]$finalCoverage.non_black_fraction
			accepted_main_grid_minimum = if ($null -eq $acceptedGridCoverage) { $null }
				else { [double]$acceptedGridCoverage.minimum_non_black_fraction }
			final_main_grid_minimum = if ($null -eq $finalGridCoverage) { $null }
				else { [double]$finalGridCoverage.minimum_non_black_fraction }
			accepted_main_grid_cells = if ($null -eq $acceptedGridCoverage) { @() }
				else { @($acceptedGridCoverage.cell_non_black_fractions) }
			final_main_grid_cells = if ($null -eq $finalGridCoverage) { @() }
				else { @($finalGridCoverage.cell_non_black_fractions) }
		}
		$frameChangeDiagnostic | ConvertTo-Json | Set-Content -LiteralPath (
			Join-Path $diagnosticsDirectory "$($case.id).frame-change.json") -Encoding utf8NoBOM
		throw "Case '$($case.id)' scene changed after accessibility stabilization or produced a partial frame."
	}
	$results.Add([ordered]@{
		id = [string]$case.id; state = [string]$case.state; theme = [string]$case.theme; layout = [string]$case.layout
		density = $density; surface_variant = $surfaceVariant; capture_window = $captureWindow
		navigation_open = $navigationOpen; motd_variant = $motdVariant
		rich_preview_variant = $richPreviewVariant; rich_preview_size = $richPreviewSize
		presentation_family = $presentationFamily; case_variant = $caseVariant
		logical_width = [int]$case.width; logical_height = [int]$case.height; device_pixel_ratio = [double]$case.device_pixel_ratio
		minimum_main_grid_non_black_fraction = if ($null -eq $finalGridCoverage) { $null }
			else { [double]$finalGridCoverage.minimum_non_black_fraction }
		visual_coherence_passed = $true
		apply_duration_ms = $applyDurationMs
		presentation_classification = if (-not $requiresProductionTimelineHandshake) { $null }
			elseif ($extendedPresentationBudgetExercised) { "uncached" }
			elseif ($containsRichOrAttachmentContent) { "cached-rich" } else { "cached-simple" }
		visible_rows_budget_ms = if ($requiresProductionTimelineHandshake) {
			$timelinePresentationBudgetMs
		} else { $null }
		loading_feedback_budget_ms = if ($requiresProductionTimelineHandshake) { 100.0 } else { $null }
		composer_interactive_budget_ms = if ($requiresProductionTimelineHandshake) { 500.0 } else { $null }
		prepend_stable_budget_ms = if ($surfaceVariant -eq "chat-history-prepend-anchor") { 300.0 } else { $null }
		cached_presentation_budget_ms = if ($requiresProductionTimelineHandshake) { 300.0 } else { $null }
		apply_rpc_budget_ms = if ($requiresProductionTimelineHandshake) {
			$applyRpcBudgetMs
		} else { $null }
		uncached_presentation_budget_ms = 1000.0
		uncached_presentation_exercised = $extendedPresentationBudgetExercised
		provider_result_transition_exercised = if ($requiresProductionTimelineHandshake) {
			$providerResultTransitionExercised
		} else { $false }
		presentation_timing_diagnostic = $presentationTimingDiagnosticPath
		timeline_presentation_wait_ms = [double]$applied.timeline_presentation_wait_ms
		timeline_handshake_duration_ms = $timelineHandshakeDurationMs
		fixture_scope_token = [string]$applied.fixture_scope_token
		pre_apply_presentation_generation = $preApplyTimelineGeneration
		fixture_presentation_generation = [long]$applied.presentation_generation
		fixture_message_id_prefix = [string]$applied.fixture_message_id_prefix
		data_ready_duration_ms = if ($null -eq $timelinePresentation) { $null }
			else { [double]$timelinePresentation.dataReadyDurationMs }
		stable_ready_duration_ms = if ($null -eq $timelinePresentation) { $null }
			else { [double]$timelinePresentation.stableReadyDurationMs }
		first_stable_frame_duration_ms = if ($null -eq $timelinePresentation) { $null }
			else { [double]$timelinePresentation.firstStableFrameDurationMs }
		second_matching_stable_frame_duration_ms = if ($null -eq $timelinePresentation) { $null }
			else { [double]$timelinePresentation.secondMatchingStableFrameDurationMs }
		first_visible_stable_rows_duration_ms = if ($null -eq $timelinePresentation) { $null }
			else { [double]$timelinePresentation.firstVisibleStableRowsDurationMs }
		loading_frame_duration_ms = if ($null -eq $timelinePresentation) { $null }
			else { [double]$timelinePresentation.loadingFrameDurationMs }
		composer_interactive_duration_ms = if ($null -eq $timelinePresentation) { $null }
			else { [double]$timelinePresentation.composerInteractiveDurationMs }
		rich_hydrated_duration_ms = if ($null -eq $timelinePresentation -or
			-not [bool]$timelinePresentation.richHydrated) { $null }
			else { [double]$timelinePresentation.richHydratedDurationMs }
		degraded_ready_duration_ms = if ($null -eq $timelinePresentation -or
			-not [bool]$timelinePresentation.degradedReady) { $null }
			else { [double]$timelinePresentation.degradedReadyDurationMs }
		provider_result_to_visible_paint_ms = if ($null -eq $timelinePresentation -or
			[double]$timelinePresentation.providerResultAppliedAt -le 0) { $null }
			else { [double]$timelinePresentation.providerResultToVisiblePaintMs }
		prepend_stable_duration_ms = if ($surfaceVariant -eq "chat-history-prepend-anchor") {
			[double]$chatState.prepend_stable_duration_ms
		} else { $null }
		prepend_start_frame_serial = if ($surfaceVariant -eq "chat-history-prepend-anchor") {
			[long]$chatState.prepend_start_frame_serial
		} else { $null }
		prepend_first_committed_frame_serial = if ($surfaceVariant -eq "chat-history-prepend-anchor") {
			[long]$chatState.prepend_first_committed_frame_serial
		} else { $null }
		prepend_second_committed_frame_serial = if ($surfaceVariant -eq "chat-history-prepend-anchor") {
			[long]$chatState.prepend_second_committed_frame_serial
		} else { $null }
		prepend_matching_committed_frame_count = if ($surfaceVariant -eq "chat-history-prepend-anchor") {
			[int]$chatState.prepend_matching_committed_frame_count
		} else { $null }
		stable_frame_duration_ms = if ($null -eq $timelinePresentation) { $null }
			else { [double]$timelinePresentation.stableFrameDurationMs }
		visible_frame_duration_ms = if ($null -eq $timelinePresentation) { $null }
			else { [double]$timelinePresentation.visibleFrameDurationMs }
		image_width = $dimensions.width; image_height = $dimensions.height
		image_sha256 = Get-QmlVisualFileSha256 $imagePath
		accessibility_sha256 = Get-QmlVisualFileSha256 $accessibilityPath
	})
}

$manifest = [ordered]@{
	schema_version = 1; frontend = "qml"; mode = if ($CandidateOnly) { "candidate-only" } else { "gate" }
	matrix_sha256 = Get-QmlVisualFileSha256 $MatrixPath
	process_id = $ExpectedProcessId
	cases = $results
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
