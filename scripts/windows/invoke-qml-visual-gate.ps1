[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)][int]$AutomationPort,
	[string]$AutomationToken = "",
	[string]$MatrixPath = "$PSScriptRoot\qml-visual-gate-matrix.json",
	[Parameter(Mandatory = $true, ParameterSetName = "Gate")][string]$BaselineManifestPath,
	[Parameter(Mandatory = $true, ParameterSetName = "Candidate")][switch]$CandidateOnly,
	[string]$OutputDirectory = ".tmp\qml-visual-gate",
	[Parameter(Mandatory = $true)][double]$ExpectedDevicePixelRatio,
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
			[string]$_.name -eq "Cancel Updating plugins" -and
			@($_.states) -contains "focused"
		})
		if ($focusedCancelNodes.Count -ne 1 -or [string]$focus.leaf.name -ne "Cancel Updating plugins") {
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
		"settings-audio-input" { @("Settings", "Audio Input", "Quick setup", "Cancel", "Apply", "Done") }
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
			"Room access & settings", "Target room", "Root / Lobby", "Access rules and groups",
			"Rules and groups", "Inherit access rules from parent room", "Room password", "Groups",
			"scrim-team", "Added members", "Kira (#2)", "Save changes"
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
			"Here are the assets from the latest community-test pass.", "Message attachments",
			"Community test dashboard", "Loading Performance trace",
			"Preview unavailable", "Retry preview for Failed attachment preview"
		) }
		"chat-history-prepend-anchor" { @(
			"Anchor message 12 remains in place after older history loads.", "Jump to latest"
		) }
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
		"async-running" { @("Updating plugins: Downloading Positional Audio") }
		"async-error" { @("Plugin update results: One plugin could not be updated") }
		"async-success" { @("Plugin update results: All selected plugins are up to date") }
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
		$matchingPreviewLinks = @($nodes | Where-Object {
			([string]$_.role).Equals("Link", [StringComparison]::OrdinalIgnoreCase) -and
			[string]$_.name -eq ($RichPreviewOpenLabel + ": " + $RichPreviewTitle)
		})
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
			$matchingPlayActions = @($nodes | Where-Object {
				([string]$_.role).Equals("Button", [StringComparison]::OrdinalIgnoreCase) -and
				[string]$_.name -eq $RichPreviewPlayName
			})
			if ([string]::IsNullOrWhiteSpace($RichPreviewPlayName) -or $matchingPlayActions.Count -ne 1) {
				throw "Rich-preview case '$CaseId' does not expose exactly one provider play action."
			}
		}
		$expectedProviderGroupName = switch ($RichPreviewVariant) {
			"steam" { "Store details"; break }
			"google" { "Google Search"; break }
			# The provider-owned prefix stays stable while the fixture identity remains
			# visible after it, e.g. "Twitch stream: Mumble Dev".
			"twitch" { "Twitch stream"; break }
			"flashback" { "Discussion details"; break }
			"x" { "Mumble Design"; break }
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
	$apply = Invoke-Automation @{
		command = "setQmlVisualGateState"; case_id = [string]$case.id; state = [string]$case.state
		theme = [string]$case.theme; layout = [string]$case.layout; width = [int]$case.width
		height = [int]$case.height; motd_variant = $motdVariant; rich_preview_variant = $richPreviewVariant
		rich_preview_size = $richPreviewSize; presentation_family = $presentationFamily; case_variant = $caseVariant
		surface_variant = $surfaceVariant; density = $density
	}
	$applied = $apply.applied
	foreach ($property in @("case_id", "state", "theme", "layout", "density", "motd_variant", "rich_preview_variant",
		"rich_preview_size", "rich_preview_present", "rich_preview_message_id", "rich_body_message_id", "rich_preview_title",
		"presentation_family", "case_variant", "surface_variant", "surface_present", "capture_window",
		"screen_share_native_frame_ready", "conversation_search_state",
		"chat_fixture_state",
		"rich_preview_open_label", "rich_preview_embed_provider", "rich_preview_embed_aspect",
		"rich_preview_media_count", "rich_preview_has_thumbnail", "width", "height", "message_count",
		"motd_present", "motd_expanded", "motd_changed", "motd_has_user_history", "motd_visible",
		"manual_plugin_state", "recorder_state", "toast_state", "focus_target", "actual_device_pixel_ratio", "generation")) {
		if (-not ($applied.PSObject.Properties.Name -contains $property)) { throw "Case '$($case.id)' lacks applied '$property'." }
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
		[int]$applied.width -ne [int]$case.width -or [int]$applied.height -ne [int]$case.height -or
		[Math]::Abs([double]$applied.actual_device_pixel_ratio - $actualDevicePixelRatio) -gt 0.001) {
		$appliedJson = $applied | ConvertTo-Json -Compress -Depth 10
		throw "Automation did not apply visual case '$($case.id)' exactly. Expected $([int]$case.width)x$([int]$case.height) at DPR $actualDevicePixelRatio; applied: $appliedJson"
	}
	$expectedMessageCount = if ([string]$case.state -ne "connected") { 0 } else {
		switch ($surfaceVariant) {
			"chat-message-states" { 4 }
			"chat-attachment-states" { 1 }
			"chat-history-prepend-anchor" { 30 }
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
			"anchor_after_offset", "anchor_offset_delta", "anchor_preserved", "pure_prepend_applied"
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
			default { $null }
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
				$expectedAnchor = "fixture:$($applied.generation):history:12"
				$reportedOffsetDelta = [double]$chatState.anchor_offset_delta
				$calculatedOffsetDelta = [Math]::Abs(
					[double]$chatState.anchor_after_offset - [double]$chatState.anchor_before_offset)
				if ([string]$chatState.anchor_id -ne $expectedAnchor -or
					[int]$chatState.anchor_before_row -ne 11 -or [int]$chatState.anchor_after_row -ne 17 -or
					-not [bool]$chatState.anchor_preserved -or -not [bool]$chatState.pure_prepend_applied -or
					$reportedOffsetDelta -lt 0 -or $reportedOffsetDelta -gt 1.0 -or
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
	$expectUserHistory = [string]$case.state -eq "connected" -and $motdVariant -in @("none", "history-visible")
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
	if ($expectRichPreview -and -not $isMediaSurface) {
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
				[pscustomobject]@{ variant = "x"; token = "x"; family = "social"; presentation = "identity" }
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
			$inlineMediaStageExpected = $richPreviewVariant -eq "direct-media" -or
				-not [string]::IsNullOrWhiteSpace([string]$applied.rich_preview_embed_provider)
			$expectedImageObjectName = if ($inlineMediaStageExpected) { "previewEmbedPoster" }
				elseif ($richPreviewVariant -eq "steam") { "providerSteamHeroImage" }
				elseif ($richPreviewVariant -in @(
					"product", "vehicle", "property", "marketplace"
				)) { "providerCommerceHeroImage" }
				elseif ($richPreviewVariant -eq "article") { "providerArticleHeroImage" }
				else { "previewCompactImage" }
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
			$mediaInsideCard = -not $inlineMediaStageExpected -or
				([bool]$richPreviewCardState.mediaVisible -and
				 [double]$richPreviewCardState.mediaWidth -gt 0 -and [double]$richPreviewCardState.mediaHeight -gt 0 -and
				 [double]$richPreviewCardState.mediaX -ge [double]$richPreviewCardState.cardX - 1 -and
				 ([double]$richPreviewCardState.mediaX + [double]$richPreviewCardState.mediaWidth) -le
					([double]$richPreviewCardState.cardX + [double]$richPreviewCardState.cardWidth + 1) -and
				 [double]$richPreviewCardState.mediaY -ge [double]$richPreviewCardState.cardY - 1 -and
				 ([double]$richPreviewCardState.mediaY + [double]$richPreviewCardState.mediaHeight) -le
					([double]$richPreviewCardState.cardY + [double]$richPreviewCardState.cardHeight + 1))
			$embedReady = -not $inlineMediaStageExpected -or
				($mediaInsideCard -and
				 [bool]$richPreviewCardState.playVisible)
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
