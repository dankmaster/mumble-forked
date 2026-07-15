[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string]$ReleaseSmokePath,

	[Parameter(Mandatory = $true)]
	[string]$QualificationPath,

	[Parameter(Mandatory = $true)]
	[string]$ModelManifestPath,

	[Parameter(Mandatory = $true)]
	[string]$RecipeManifestPath,

	[Parameter(Mandatory = $true)]
	[string]$ModelManifestSignaturePath,

	[Parameter(Mandatory = $true)]
	[string]$RecipeManifestSignaturePath,

	[Parameter(Mandatory = $true)]
	[string]$ArtifactPath,

	[Parameter(Mandatory = $true)]
	[string]$ExpandedArtifactRoot,

	[Parameter(Mandatory = $true)]
	[string]$ExpectedSourceSha,

	[Parameter(Mandatory = $true)]
	[string]$ExpectedBuildId,

	[Parameter(Mandatory = $true)]
	[string]$ExpectedHarnessSha256,

	[Parameter(Mandatory = $true)]
	[string]$FixtureManifestPath,

	[Parameter(Mandatory = $true)]
	[string]$CaseSetPath,

	[Parameter(Mandatory = $true)]
	[string]$ExpectedFixtureManifestSha256,

	[Parameter(Mandatory = $true)]
	[string]$ExpectedCaseSetSha256,

	[Parameter(Mandatory = $true)]
	[string]$ExpectedServerExecutableSha256
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

Import-Module (Join-Path $PSScriptRoot "InputEnhancementReleaseTools.psm1") -Force

$suiteId = "input-enhancement-release-smoke-v1"
$requiredScenes = @("stationary-hvac", "transient-keyboard", "competing-speech")
$requiredProfiles = @("Balanced", "Crisp")
$requiredStartups = @("cold", "warm")
$requiredModelIds = @{
	Balanced = "rnnoise:embedded"
	Crisp = "deepfilternet:balanced"
}
$requiredRecipeIds = @{
	Balanced = "input.balanced.rnnoise-embedded"
	Crisp = "input.crisp.deepfilternet-balanced"
}

function Assert-ExactProperties {
	param(
		[Parameter(Mandatory = $true)]
		[object]$Object,
		[Parameter(Mandatory = $true)]
		[string[]]$Names,
		[Parameter(Mandatory = $true)]
		[string]$Context
	)

	if ($Object -is [System.Collections.IDictionary]) {
		$actualNames = @($Object.Keys | ForEach-Object { [string]$_ })
	} else {
		$actualNames = @($Object.PSObject.Properties.Name)
	}
	$missing = @($Names | Where-Object { $_ -cnotin $actualNames })
	$unexpected = @($actualNames | Where-Object { $_ -cnotin $Names })
	if ($missing.Count -gt 0 -or $unexpected.Count -gt 0) {
		throw "$Context has an invalid schema. Missing: [$($missing -join ', ')]; unexpected: [$($unexpected -join ', ')]."
	}
}

function Assert-TrueValue {
	param(
		[Parameter(Mandatory = $true)]
		[object]$Value,
		[Parameter(Mandatory = $true)]
		[string]$Context
	)
	if ($Value -ne $true) {
		throw "$Context must be true."
	}
}

function Assert-FalseValue {
	param(
		[Parameter(Mandatory = $true)]
		[object]$Value,
		[Parameter(Mandatory = $true)]
		[string]$Context
	)
	if ($Value -ne $false) {
		throw "$Context must be false."
	}
}

function Assert-ZeroCounter {
	param(
		[Parameter(Mandatory = $true)]
		[object]$Object,
		[Parameter(Mandatory = $true)]
		[string]$Name,
		[Parameter(Mandatory = $true)]
		[string]$Context
	)
	$value = Assert-ObjectProperty -Object $Object -Name $Name -Context $Context
	if ([int64]$value -ne 0) {
		throw "$Context reports $Name=$value; release smoke requires zero."
	}
}

function Assert-Sha256Value {
	param(
		[Parameter(Mandatory = $true)]
		[string]$Value,
		[Parameter(Mandatory = $true)]
		[string]$Context
	)
	$normalized = $Value.Trim().ToLowerInvariant()
	if ($normalized -notmatch '^[0-9a-f]{64}$') {
		throw "$Context must be a lowercase-compatible SHA-256 value, got '$Value'."
	}
	return $normalized
}

function Assert-NonnegativeIntegerAtMost {
	param(
		[Parameter(Mandatory = $true)]
		[object]$Value,
		[Parameter(Mandatory = $true)]
		[int64]$Maximum,
		[Parameter(Mandatory = $true)]
		[string]$Context
	)
	$text = [Convert]::ToString($Value, [Globalization.CultureInfo]::InvariantCulture)
	[int64]$parsed = 0
	if (-not [int64]::TryParse($text, [Globalization.NumberStyles]::None,
		[Globalization.CultureInfo]::InvariantCulture, [ref]$parsed) -or $parsed -gt $Maximum) {
		throw "$Context must be a nonnegative integer no greater than $Maximum; got '$text'."
	}
	return $parsed
}

function Assert-FiniteNumberAtMost {
	param(
		[Parameter(Mandatory = $true)]
		[object]$Value,
		[Parameter(Mandatory = $true)]
		[double]$Maximum,
		[Parameter(Mandatory = $true)]
		[string]$Context
	)
	if ($null -eq $Value -or $Value -is [bool]) {
		throw "$Context must be a finite nonnegative number no greater than $Maximum."
	}
	try {
		$parsed = [Convert]::ToDouble($Value, [Globalization.CultureInfo]::InvariantCulture)
	} catch {
		throw "$Context must be a finite nonnegative number no greater than $Maximum."
	}
	if ([double]::IsNaN($parsed) -or [double]::IsInfinity($parsed) -or $parsed -lt 0.0 -or $parsed -gt $Maximum) {
		throw "$Context must be a finite nonnegative number no greater than $Maximum; got '$parsed'."
	}
	return $parsed
}

function Assert-IdentifierValue {
	param(
		[Parameter(Mandatory = $true)]
		[object]$Value,
		[Parameter(Mandatory = $true)]
		[string]$Context
	)
	$text = [string]$Value
	if ($text -cnotmatch '^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$') {
		throw "$Context must be a nonempty portable identifier, got '$text'."
	}
	return $text
}

if (-not (Test-Path -LiteralPath $ExpandedArtifactRoot -PathType Container)) {
	throw "Expanded artifact root does not exist: '$ExpandedArtifactRoot'."
}

$smoke = Read-ReleaseJson -Path $ReleaseSmokePath
Assert-ExactProperties -Object $smoke -Context "Release smoke" -Names @(
	"schemaVersion", "suite", "passed", "audioFree", "sourceSha", "buildId", "createdAtUtc",
	"artifact", "qualification", "packagedClient", "provenance", "topology", "originalControls", "cases"
)
if ([int](Assert-ObjectProperty -Object $smoke -Name "schemaVersion" -Context "Release smoke") -ne 1) {
	throw "Unsupported release-smoke schema version."
}
if ([string](Assert-ObjectProperty -Object $smoke -Name "suite" -Context "Release smoke") -cne $suiteId) {
	throw "Release smoke must identify suite '$suiteId'."
}
Assert-TrueValue -Value (Assert-ObjectProperty -Object $smoke -Name "passed" -Context "Release smoke") -Context "Release smoke passed"
Assert-TrueValue -Value (Assert-ObjectProperty -Object $smoke -Name "audioFree" -Context "Release smoke") -Context "Release smoke audioFree"

$createdAt = [datetimeoffset]::MinValue
$createdAtValid = [datetimeoffset]::TryParse(
		[string](Assert-ObjectProperty -Object $smoke -Name "createdAtUtc" -Context "Release smoke"),
		[System.Globalization.CultureInfo]::InvariantCulture,
		[System.Globalization.DateTimeStyles]::RoundtripKind,
		[ref]$createdAt)
if (-not $createdAtValid) {
	throw "Release smoke createdAtUtc must be a valid UTC timestamp."
}

$expectedSha = Assert-FullGitSha -Sha $ExpectedSourceSha -Context "Expected source SHA"
$reportedSha = Assert-FullGitSha -Sha ([string](Assert-ObjectProperty -Object $smoke -Name "sourceSha" -Context "Release smoke")) -Context "Release-smoke source SHA"
if ($reportedSha -cne $expectedSha) {
	throw "Release-smoke source SHA '$reportedSha' does not match expected '$expectedSha'."
}
$reportedBuildId = [string](Assert-ObjectProperty -Object $smoke -Name "buildId" -Context "Release smoke")
if ($reportedBuildId -cne $ExpectedBuildId) {
	throw "Release-smoke build ID '$reportedBuildId' does not match expected '$ExpectedBuildId'."
}

$artifactItem = Get-Item -LiteralPath $ArtifactPath -ErrorAction Stop
if ($artifactItem.PSIsContainer) {
	throw "Qualified artifact must be a file."
}
$artifactHash = Get-ReleaseFileSha256 -Path $artifactItem.FullName
$artifactRecord = Assert-ObjectProperty -Object $smoke -Name "artifact" -Context "Release smoke"
Assert-ExactProperties -Object $artifactRecord -Names @("fileName", "sha256", "size") -Context "Release-smoke artifact"
if ([string]$artifactRecord.fileName -cne $artifactItem.Name -or
	(Assert-Sha256Value -Value ([string]$artifactRecord.sha256) -Context "Release-smoke artifact hash") -cne $artifactHash -or
	[int64]$artifactRecord.size -ne [int64]$artifactItem.Length) {
	throw "Release-smoke artifact identity does not match the downloaded qualified artifact."
}

$qualificationItem = Get-Item -LiteralPath $QualificationPath -ErrorAction Stop
$qualificationHash = Get-ReleaseFileSha256 -Path $qualificationItem.FullName
$qualificationRecord = Assert-ObjectProperty -Object $smoke -Name "qualification" -Context "Release smoke"
Assert-ExactProperties -Object $qualificationRecord -Names @("fileName", "sha256") -Context "Release-smoke qualification"
if ([string]$qualificationRecord.fileName -cne $qualificationItem.Name -or
	(Assert-Sha256Value -Value ([string]$qualificationRecord.sha256) -Context "Release-smoke qualification hash") -cne $qualificationHash) {
	throw "Release smoke is not bound to the downloaded qualification."
}

$qualification = Read-ReleaseJson -Path $QualificationPath
$qualifiedSource = Assert-ObjectProperty -Object $qualification -Name "source" -Context "Qualification"
if ((Assert-FullGitSha -Sha ([string]$qualifiedSource.sha) -Context "Qualified source SHA") -cne $expectedSha -or
	[string](Assert-ObjectProperty -Object $qualification -Name "buildId" -Context "Qualification") -cne $ExpectedBuildId) {
	throw "Qualification source/build identity does not match release smoke."
}
$qualifiedArtifact = Assert-ObjectProperty -Object $qualification -Name "updatePackage" -Context "Qualification"
if ([string]$qualifiedArtifact.fileName -cne $artifactItem.Name -or
	(Assert-Sha256Value -Value ([string]$qualifiedArtifact.sha256) -Context "Qualified artifact hash") -cne $artifactHash -or
	[int64]$qualifiedArtifact.size -ne [int64]$artifactItem.Length -or
	[string]$qualifiedArtifact.format -cne "mumble-update-v1") {
	throw "Qualification does not identify the same signed update package as release smoke."
}

$modelManifestItem = Get-Item -LiteralPath $ModelManifestPath -ErrorAction Stop
$qualifiedModelManifest = Assert-ObjectProperty -Object $qualification -Name "modelManifest" -Context "Qualification"
if ([string]$qualifiedModelManifest.fileName -cne $modelManifestItem.Name -or
	(Assert-Sha256Value -Value ([string]$qualifiedModelManifest.sha256) -Context "Qualified model-manifest hash") -cne
		(Get-ReleaseFileSha256 -Path $modelManifestItem.FullName)) {
	throw "Release smoke model manifest does not match the qualified model manifest."
}
$modelManifest = Read-ReleaseJson -Path $modelManifestItem.FullName
$packagedModelManifestPath = Join-Path $ExpandedArtifactRoot 'input-models.json'
if (-not (Test-Path -LiteralPath $packagedModelManifestPath -PathType Leaf) -or
	(Get-ReleaseFileSha256 -Path $packagedModelManifestPath) -cne (Get-ReleaseFileSha256 -Path $modelManifestItem.FullName)) {
	throw "Release-smoked update payload does not contain the qualified model-manifest bytes."
}
$manifestModels = @(Assert-ObjectProperty -Object $modelManifest -Name "models" -Context "Model manifest")
$modelsById = @{}
foreach ($model in $manifestModels) {
	$modelId = [string](Assert-ObjectProperty -Object $model -Name "id" -Context "Model manifest entry")
	if ([string]::IsNullOrWhiteSpace($modelId) -or $modelsById.ContainsKey($modelId)) {
		throw "Model manifest contains an empty or duplicate model ID '$modelId'."
	}
	$modelsById[$modelId] = $model
}
foreach ($requiredModelId in $requiredModelIds.Values) {
	if (-not $modelsById.ContainsKey($requiredModelId)) {
		throw "Qualified model manifest is missing release-smoke model '$requiredModelId'."
	}
}
$recipeManifestItem = Get-Item -LiteralPath $RecipeManifestPath -ErrorAction Stop
$qualifiedRecipeManifest = Assert-ObjectProperty -Object $qualification -Name "recipeManifest" -Context "Qualification"
if ([string]$qualifiedRecipeManifest.fileName -cne $recipeManifestItem.Name -or
	(Assert-Sha256Value -Value ([string]$qualifiedRecipeManifest.sha256) -Context "Qualified recipe-manifest hash") -cne
		(Get-ReleaseFileSha256 -Path $recipeManifestItem.FullName)) {
	throw "Release smoke recipe manifest does not match the qualified recipe manifest."
}
$recipeManifest = Read-ReleaseJson -Path $recipeManifestItem.FullName
$packagedRecipeManifestPath = Join-Path $ExpandedArtifactRoot 'input-recipes.json'
if (-not (Test-Path -LiteralPath $packagedRecipeManifestPath -PathType Leaf) -or
	(Get-ReleaseFileSha256 -Path $packagedRecipeManifestPath) -cne (Get-ReleaseFileSha256 -Path $recipeManifestItem.FullName)) {
	throw "Release-smoked update payload does not contain the qualified recipe-manifest bytes."
}
& (Join-Path $PSScriptRoot 'assert-input-enhancement-package-manifest-binding.ps1') `
	-ExpandedPayloadRoot $ExpandedArtifactRoot `
	-ModelManifestPath $ModelManifestPath `
	-RecipeManifestPath $RecipeManifestPath `
	-ModelManifestSignaturePath $ModelManifestSignaturePath `
	-RecipeManifestSignaturePath $RecipeManifestSignaturePath
$recipesById = @{}
foreach ($recipe in @(Assert-ObjectProperty -Object $recipeManifest -Name "recipes" -Context "Recipe manifest")) {
	$recipeId = [string](Assert-ObjectProperty -Object $recipe -Name "id" -Context "Recipe manifest entry")
	if ([string]::IsNullOrWhiteSpace($recipeId) -or $recipesById.ContainsKey($recipeId)) {
		throw "Recipe manifest contains an empty or duplicate recipe ID '$recipeId'."
	}
	$recipesById[$recipeId] = $recipe
}
foreach ($requiredRecipeId in $requiredRecipeIds.Values) {
	if (-not $recipesById.ContainsKey($requiredRecipeId)) {
		throw "Qualified recipe manifest is missing release-smoke recipe '$requiredRecipeId'."
	}
}

$packagedClientRecord = Assert-ObjectProperty -Object $smoke -Name "packagedClient" -Context "Release smoke"
Assert-ExactProperties -Object $packagedClientRecord -Names @("relativePath", "sha256") -Context "Release-smoke packaged client"
if ([string]$packagedClientRecord.relativePath -cne "mumble.exe") {
	throw "Release smoke must exercise update-payload client path 'mumble.exe'."
}
$packagedClientPath = Join-Path $ExpandedArtifactRoot "mumble.exe"
$packagedClientHash = Get-ReleaseFileSha256 -Path $packagedClientPath
if ((Assert-Sha256Value -Value ([string]$packagedClientRecord.sha256) -Context "Packaged client hash") -cne $packagedClientHash) {
	throw "Release smoke did not exercise the mumble.exe expanded from the qualified artifact."
}

$expectedHarnessHash = Assert-Sha256Value -Value $ExpectedHarnessSha256 -Context "Expected release-smoke harness hash"
$expectedFixtureManifestHash = Assert-Sha256Value -Value $ExpectedFixtureManifestSha256 -Context "Expected fixture-manifest hash"
$expectedCaseSetHash = Assert-Sha256Value -Value $ExpectedCaseSetSha256 -Context "Expected case-set hash"
$expectedServerHash = Assert-Sha256Value -Value $ExpectedServerExecutableSha256 -Context "Expected OG server executable hash"
$fixtureManifestItem = Get-Item -LiteralPath $FixtureManifestPath -ErrorAction Stop
$caseSetItem = Get-Item -LiteralPath $CaseSetPath -ErrorAction Stop
if ($fixtureManifestItem.PSIsContainer -or $caseSetItem.PSIsContainer -or
	(Get-ReleaseFileSha256 -Path $fixtureManifestItem.FullName) -cne $expectedFixtureManifestHash -or
	(Get-ReleaseFileSha256 -Path $caseSetItem.FullName) -cne $expectedCaseSetHash) {
	throw "Protected release-smoke fixture manifest or case set does not match its attested SHA-256."
}

$fixtureManifest = Read-ReleaseJson -Path $fixtureManifestItem.FullName
Assert-ExactProperties -Object $fixtureManifest -Context "Release-smoke fixture manifest" `
	-Names @("schemaVersion", "audioFree", "fixtures")
if ([int]$fixtureManifest.schemaVersion -ne 1) {
	throw "Unsupported release-smoke fixture-manifest schema version."
}
Assert-TrueValue -Value $fixtureManifest.audioFree -Context "Release-smoke fixture manifest audioFree"
$fixturesById = @{}
foreach ($fixture in @($fixtureManifest.fixtures)) {
	Assert-ExactProperties -Object $fixture -Context "Release-smoke fixture entry" -Names @("id", "sha256")
	$fixtureId = Assert-IdentifierValue -Value $fixture.id -Context "Release-smoke fixture ID"
	if ($fixturesById.ContainsKey($fixtureId)) {
		throw "Release-smoke fixture manifest contains duplicate fixture '$fixtureId'."
	}
	$fixturesById[$fixtureId] = Assert-Sha256Value -Value ([string]$fixture.sha256) `
		-Context "Release-smoke fixture '$fixtureId' hash"
}
$expectedFixtureIds = @($requiredScenes | ForEach-Object { "$_-fixture" })
if ($fixturesById.Count -ne $expectedFixtureIds.Count -or
	@($expectedFixtureIds | Where-Object { -not $fixturesById.ContainsKey($_) }).Count -ne 0) {
	throw "Release-smoke fixture manifest must contain exactly the three fixed scene fixtures."
}

$caseSet = Read-ReleaseJson -Path $caseSetItem.FullName
Assert-ExactProperties -Object $caseSet -Context "Release-smoke case set" `
	-Names @("schemaVersion", "suite", "originalControls", "cases")
if ([int]$caseSet.schemaVersion -ne 1 -or [string]$caseSet.suite -cne $suiteId) {
	throw "Release-smoke case set has an unsupported schema or suite."
}
$caseSetControlsById = @{}
foreach ($entry in @($caseSet.originalControls)) {
	Assert-ExactProperties -Object $entry -Context "Release-smoke case-set Original entry" `
		-Names @("id", "scene", "startup", "preRollMs", "fixtureId")
	$id = Assert-IdentifierValue -Value $entry.id -Context "Release-smoke case-set Original ID"
	if ($caseSetControlsById.ContainsKey($id)) { throw "Duplicate case-set Original ID '$id'." }
	$caseSetControlsById[$id] = $entry
}
$caseSetCasesById = @{}
foreach ($entry in @($caseSet.cases)) {
	Assert-ExactProperties -Object $entry -Context "Release-smoke case-set enhanced entry" `
		-Names @("id", "scene", "profile", "startup", "preRollMs", "fixtureId")
	$id = Assert-IdentifierValue -Value $entry.id -Context "Release-smoke case-set enhanced ID"
	if ($caseSetCasesById.ContainsKey($id)) { throw "Duplicate case-set enhanced ID '$id'." }
	$caseSetCasesById[$id] = $entry
}
if ($caseSetControlsById.Count -ne 6 -or $caseSetCasesById.Count -ne 12) {
	throw "Release-smoke case set must contain exactly six Original controls and twelve enhanced cases."
}
$provenance = Assert-ObjectProperty -Object $smoke -Name "provenance" -Context "Release smoke"
Assert-ExactProperties -Object $provenance -Context "Release-smoke provenance" -Names @(
	"harnessSha256", "fixtureManifestSha256", "caseSetSha256", "serverExecutableSha256"
)
if ((Assert-Sha256Value -Value ([string]$provenance.harnessSha256) -Context "Release-smoke harness hash") -cne $expectedHarnessHash -or
	(Assert-Sha256Value -Value ([string]$provenance.fixtureManifestSha256) -Context "Release-smoke fixture-manifest hash") -cne $expectedFixtureManifestHash -or
	(Assert-Sha256Value -Value ([string]$provenance.caseSetSha256) -Context "Release-smoke case-set hash") -cne $expectedCaseSetHash -or
	(Assert-Sha256Value -Value ([string]$provenance.serverExecutableSha256) -Context "Release-smoke server executable hash") -cne $expectedServerHash) {
	throw "Release-smoke provenance does not match the exact protected harness, fixtures, case set, and OG server binary."
}

$topology = Assert-ObjectProperty -Object $smoke -Name "topology" -Context "Release smoke"
Assert-ExactProperties -Object $topology -Context "Release-smoke topology" -Names @(
	"senderClient", "serverHost", "transport", "receiverClient", "voiceProtocolModified",
	"receiverCleanupEnabled", "postDecodeCleanupEnabled"
)
if ([string]$topology.senderClient -cne "packaged-client-1" -or
	[string]$topology.serverHost -cne "127.0.0.1" -or
	[string]$topology.transport -cne "client1-opus-server-client2" -or
	[string]$topology.receiverClient -cne "packaged-client-2") {
	throw "Release smoke must use packaged client 1 -> Mumble Opus on 127.0.0.1 -> packaged client 2."
}
Assert-FalseValue -Value $topology.voiceProtocolModified -Context "Release-smoke topology voiceProtocolModified"
Assert-FalseValue -Value $topology.receiverCleanupEnabled -Context "Release-smoke topology receiverCleanupEnabled"
Assert-FalseValue -Value $topology.postDecodeCleanupEnabled -Context "Release-smoke topology postDecodeCleanupEnabled"

$originalControls = @(Assert-ObjectProperty -Object $smoke -Name "originalControls" -Context "Release smoke")
if ($originalControls.Count -ne 6) {
	throw "Release smoke must contain exactly six paired Original controls, got $($originalControls.Count)."
}
$expectedControlIds = New-Object System.Collections.Generic.HashSet[string] ([System.StringComparer]::Ordinal)
foreach ($scene in $requiredScenes) {
	foreach ($startup in $requiredStartups) {
		$null = $expectedControlIds.Add("$scene-original-$startup")
	}
}
$controlsById = @{}
foreach ($control in $originalControls) {
	Assert-ExactProperties -Object $control -Context "Release-smoke Original control" -Names @(
		"id", "scene", "startup", "preRollMs", "passed", "fixtureId", "fixtureSha256", "artifactSha256",
		"fixtureManifestSha256", "caseSetSha256",
		"senderExecutableSha256", "receiverExecutableSha256", "serverExecutableSha256", "harnessSha256",
		"routeVerified", "encodedOpusPackets", "receivedPcmFrames", "receiverCleanupEnabled",
		"postDecodeCleanupEnabled", "deadlineMissCount", "fixedTimelinePassed", "timelineAlignment",
		"onsetLossSamples", "endLossSamples", "missingTailSamples", "receivedClippedSamples",
		"referenceClippedSamples"
	)
	$controlId = Assert-IdentifierValue -Value $control.id -Context "Release-smoke Original control ID"
	$scene = [string]$control.scene
	$startup = [string]$control.startup
	if (-not $expectedControlIds.Contains($controlId) -or $controlsById.ContainsKey($controlId) -or
		$controlId -cne "$scene-original-$startup" -or $scene -cnotin $requiredScenes -or $startup -cnotin $requiredStartups) {
		throw "Release smoke contains unexpected or duplicate Original control '$controlId'."
	}
	$expectedPreRoll = if ($startup -ceq "cold") { 0 } else { 300 }
	if ([int]$control.preRollMs -ne $expectedPreRoll) {
		throw "Release-smoke Original control '$controlId' must use preRollMs=$expectedPreRoll."
	}
	Assert-TrueValue -Value $control.passed -Context "Original control '$controlId' passed"
	Assert-TrueValue -Value $control.routeVerified -Context "Original control '$controlId' routeVerified"
	Assert-FalseValue -Value $control.receiverCleanupEnabled -Context "Original control '$controlId' receiverCleanupEnabled"
	Assert-FalseValue -Value $control.postDecodeCleanupEnabled -Context "Original control '$controlId' postDecodeCleanupEnabled"
	Assert-ZeroCounter -Object $control -Name "deadlineMissCount" -Context "Original control '$controlId'"
	Assert-TrueValue -Value $control.fixedTimelinePassed -Context "Original control '$controlId' fixedTimelinePassed"
	if ([string]$control.timelineAlignment -cne 'fixed-original-no-correlation') {
		throw "Original control '$controlId' must use a fixed timeline without correlation alignment."
	}
	foreach ($lossName in @('onsetLossSamples', 'endLossSamples')) {
		$null = Assert-NonnegativeIntegerAtMost -Value $control.$lossName -Maximum 480 `
			-Context "Original control '$controlId' $lossName"
	}
	$null = Assert-NonnegativeIntegerAtMost -Value $control.missingTailSamples -Maximum 0 `
		-Context "Original control '$controlId' missingTailSamples"
	$controlReceivedClipped = Assert-NonnegativeIntegerAtMost -Value $control.receivedClippedSamples `
		-Maximum ([int64]::MaxValue) -Context "Original control '$controlId' receivedClippedSamples"
	$controlReferenceClipped = Assert-NonnegativeIntegerAtMost -Value $control.referenceClippedSamples `
		-Maximum ([int64]::MaxValue) -Context "Original control '$controlId' referenceClippedSamples"
	if ($controlReceivedClipped -gt $controlReferenceClipped) {
		throw "Original control '$controlId' introduced new clipped samples."
	}
	if ([int64]$control.encodedOpusPackets -le 0 -or [int64]$control.receivedPcmFrames -le 0) {
		throw "Original control '$controlId' did not prove an Opus send/receive route."
	}
	$fixtureId = Assert-IdentifierValue -Value $control.fixtureId -Context "Original control '$controlId' fixture ID"
	$fixtureHash = Assert-Sha256Value -Value ([string]$control.fixtureSha256) -Context "Original control '$controlId' fixture hash"
	$controlArtifactHash = Assert-Sha256Value -Value ([string]$control.artifactSha256) -Context "Original control '$controlId' artifact hash"
	if (-not $fixturesById.ContainsKey($fixtureId) -or [string]$fixturesById[$fixtureId] -cne $fixtureHash) {
		throw "Original control '$controlId' references a fixture absent from the attested fixture manifest."
	}
	if (-not $caseSetControlsById.ContainsKey($controlId)) {
		throw "Original control '$controlId' is absent from the attested fixed case set."
	}
	$caseSetControl = $caseSetControlsById[$controlId]
	if ([string]$caseSetControl.scene -cne $scene -or [string]$caseSetControl.startup -cne $startup -or
		[int]$caseSetControl.preRollMs -ne $expectedPreRoll -or [string]$caseSetControl.fixtureId -cne $fixtureId) {
		throw "Original control '$controlId' disagrees with the attested fixed case set."
	}
	if ((Assert-Sha256Value -Value ([string]$control.fixtureManifestSha256) -Context "Original control '$controlId' fixture-manifest hash") -cne $expectedFixtureManifestHash -or
		(Assert-Sha256Value -Value ([string]$control.caseSetSha256) -Context "Original control '$controlId' case-set hash") -cne $expectedCaseSetHash -or
		(Assert-Sha256Value -Value ([string]$control.senderExecutableSha256) -Context "Original control '$controlId' sender hash") -cne $packagedClientHash -or
		(Assert-Sha256Value -Value ([string]$control.receiverExecutableSha256) -Context "Original control '$controlId' receiver hash") -cne $packagedClientHash -or
		(Assert-Sha256Value -Value ([string]$control.serverExecutableSha256) -Context "Original control '$controlId' server hash") -cne $expectedServerHash -or
		(Assert-Sha256Value -Value ([string]$control.harnessSha256) -Context "Original control '$controlId' harness hash") -cne $expectedHarnessHash) {
		throw "Original control '$controlId' was not produced from the exact fixture/case set by the packaged clients, OG server, and protected harness."
	}
	$controlsById[$controlId] = [pscustomobject]@{
		fixtureId = $fixtureId
		fixtureSha256 = $fixtureHash
		artifactSha256 = $controlArtifactHash
	}
}
if ($controlsById.Count -ne $expectedControlIds.Count) {
	throw "Release smoke does not contain the complete six-control Original matrix."
}

$cases = @(Assert-ObjectProperty -Object $smoke -Name "cases" -Context "Release smoke")
if ($cases.Count -ne 12) {
	throw "Release smoke must contain exactly 12 cases, got $($cases.Count)."
}

$expectedIds = New-Object System.Collections.Generic.HashSet[string] ([System.StringComparer]::Ordinal)
foreach ($scene in $requiredScenes) {
	foreach ($profile in $requiredProfiles) {
		foreach ($startup in $requiredStartups) {
			$null = $expectedIds.Add("$scene-$($profile.ToLowerInvariant())-$startup")
		}
	}
}
$seenIds = New-Object System.Collections.Generic.HashSet[string] ([System.StringComparer]::Ordinal)
$startupBaselinePairs = @{}

foreach ($case in $cases) {
	Assert-ExactProperties -Object $case -Context "Release-smoke case" -Names @(
		"id", "scene", "profile", "startup", "preRollMs", "passed", "artifactSha256", "fixtureId", "fixtureSha256",
		"fixtureManifestSha256", "caseSetSha256", "harnessSha256", "serverExecutableSha256",
		"senderExecutableSha256", "receiverExecutableSha256", "routeVerified", "encodedOpusPackets",
		"receivedPcmFrames", "receiverCleanupEnabled", "postDecodeCleanupEnabled", "expectedModelSha256",
		"activeModelSha256", "expectedModelId", "activeModelId", "fallbackCount", "modelHashMismatchCount", "invalidOutputCount",
		"expectedRecipeId", "activeRecipeId", "expectedRecipeRevision", "activeRecipeRevision",
		"tailErrorCount", "latencyErrorCount", "tailDrainExpectedFrames", "tailDrainActualFrames",
		"enhancementLatencyMs", "fixedTimelinePassed", "onsetLossSamples", "endLossSamples",
		"originalControlId", "originalControlArtifactSha256", "timelineAlignment", "missingTailSamples",
		"receivedClippedSamples", "referenceClippedSamples", "callbackP99Ms",
		"workerP99Ms", "workerRtf", "workerPendingFrames", "deadlineMissCount"
	)
	$id = [string](Assert-ObjectProperty -Object $case -Name "id" -Context "Release-smoke case")
	if (-not $expectedIds.Contains($id) -or -not $seenIds.Add($id)) {
		throw "Release smoke contains unexpected or duplicate case '$id'."
	}
	$scene = [string]$case.scene
	$profile = [string]$case.profile
	$startup = [string]$case.startup
	if ($scene -cnotin $requiredScenes -or $profile -cnotin $requiredProfiles -or $startup -cnotin $requiredStartups -or
		$id -cne "$scene-$($profile.ToLowerInvariant())-$startup") {
		throw "Release-smoke case '$id' does not match the fixed scene/profile/startup matrix."
	}
	$expectedPreRoll = if ($startup -ceq "cold") { 0 } else { 300 }
	if ([int]$case.preRollMs -ne $expectedPreRoll) {
		throw "Release-smoke case '$id' must use preRollMs=$expectedPreRoll."
	}
	Assert-TrueValue -Value $case.passed -Context "Release-smoke case '$id' passed"
	Assert-TrueValue -Value $case.routeVerified -Context "Release-smoke case '$id' routeVerified"
	Assert-TrueValue -Value $case.fixedTimelinePassed -Context "Release-smoke case '$id' fixedTimelinePassed"
	Assert-FalseValue -Value $case.receiverCleanupEnabled -Context "Release-smoke case '$id' receiverCleanupEnabled"
	Assert-FalseValue -Value $case.postDecodeCleanupEnabled -Context "Release-smoke case '$id' postDecodeCleanupEnabled"
	Assert-ZeroCounter -Object $case -Name "deadlineMissCount" -Context "Release-smoke case '$id'"
	if ([int64]$case.encodedOpusPackets -le 0 -or [int64]$case.receivedPcmFrames -le 0) {
		throw "Release-smoke case '$id' did not prove an Opus send/receive route."
	}
	if ([string]$case.timelineAlignment -cne 'fixed-paired-original-onset') {
		throw "Release-smoke case '$id' did not use fixed paired-Original timeline scoring."
	}
	foreach ($lossName in @('onsetLossSamples', 'endLossSamples')) {
		$lossValue = Assert-ObjectProperty -Object $case -Name $lossName -Context "Release-smoke case '$id'"
		$null = Assert-NonnegativeIntegerAtMost -Value $lossValue -Maximum 480 `
			-Context "Release-smoke case '$id' $lossName"
	}
	$controlId = Assert-IdentifierValue -Value $case.originalControlId -Context "Case '$id' Original control ID"
	$expectedControlId = "$scene-original-$startup"
	if ($controlId -cne $expectedControlId -or -not $controlsById.ContainsKey($controlId)) {
		throw "Release-smoke case '$id' is not bound to expected Original control '$expectedControlId'."
	}
	$control = $controlsById[$controlId]
	$caseFixtureId = Assert-IdentifierValue -Value $case.fixtureId -Context "Case '$id' fixture ID"
	$caseFixtureHash = Assert-Sha256Value -Value ([string]$case.fixtureSha256) -Context "Case '$id' fixture hash"
	if (-not $fixturesById.ContainsKey($caseFixtureId) -or
		[string]$fixturesById[$caseFixtureId] -cne $caseFixtureHash) {
		throw "Release-smoke case '$id' references a fixture absent from the attested fixture manifest."
	}
	if (-not $caseSetCasesById.ContainsKey($id)) {
		throw "Release-smoke case '$id' is absent from the attested fixed case set."
	}
	$caseSetCase = $caseSetCasesById[$id]
	if ([string]$caseSetCase.scene -cne $scene -or [string]$caseSetCase.profile -cne $profile -or
		[string]$caseSetCase.startup -cne $startup -or [int]$caseSetCase.preRollMs -ne $expectedPreRoll -or
		[string]$caseSetCase.fixtureId -cne $caseFixtureId) {
		throw "Release-smoke case '$id' disagrees with the attested fixed case set."
	}
	if ($caseFixtureId -cne [string]$control.fixtureId -or $caseFixtureHash -cne [string]$control.fixtureSha256) {
		throw "Release-smoke case '$id' did not use the exact fixture used by Original control '$controlId'."
	}
	if ((Assert-Sha256Value -Value ([string]$case.fixtureManifestSha256) -Context "Case '$id' fixture-manifest hash") -cne $expectedFixtureManifestHash -or
		(Assert-Sha256Value -Value ([string]$case.caseSetSha256) -Context "Case '$id' case-set hash") -cne $expectedCaseSetHash -or
		(Assert-Sha256Value -Value ([string]$case.harnessSha256) -Context "Case '$id' harness hash") -cne $expectedHarnessHash -or
		(Assert-Sha256Value -Value ([string]$case.serverExecutableSha256) -Context "Case '$id' server executable hash") -cne $expectedServerHash) {
		throw "Release-smoke case '$id' is not bound to the exact fixture manifest, case set, harness, and OG server binary."
	}
	$startupBaselineText = [string]$case.originalControlArtifactSha256
	if ($startupBaselineText -cnotmatch '^[0-9a-f]{64}$') {
		throw "Case '$id' paired Original-control artifact hash must be lowercase SHA-256."
	}
	$null = Assert-Sha256Value -Value $startupBaselineText `
		-Context "Case '$id' paired Original-control artifact hash"
	if ($startupBaselineText -cne [string]$control.artifactSha256) {
		throw "Release-smoke case '$id' does not attest the artifact from Original control '$controlId'."
	}
	$startupBaselineKey = "$scene-$startup"
	if ($startupBaselinePairs.ContainsKey($startupBaselineKey)) {
		$pair = $startupBaselinePairs[$startupBaselineKey]
		if ([string]$pair.sha256 -cne $startupBaselineText) {
			throw "Release-smoke case '$id' does not share the same paired Original baseline as the other '$startupBaselineKey' profile."
		}
		$pair.caseCount = [int]$pair.caseCount + 1
	} else {
		$startupBaselinePairs[$startupBaselineKey] = [pscustomobject]@{
			sha256 = $startupBaselineText
			caseCount = 1
		}
	}
	$null = Assert-NonnegativeIntegerAtMost -Value $case.missingTailSamples -Maximum 0 `
		-Context "Release-smoke case '$id' missingTailSamples"
	$receivedClippedSamples = Assert-NonnegativeIntegerAtMost -Value $case.receivedClippedSamples `
		-Maximum ([int64]::MaxValue) -Context "Release-smoke case '$id' receivedClippedSamples"
	$referenceClippedSamples = Assert-NonnegativeIntegerAtMost -Value $case.referenceClippedSamples `
		-Maximum ([int64]::MaxValue) -Context "Release-smoke case '$id' referenceClippedSamples"
	if ($receivedClippedSamples -gt $referenceClippedSamples) {
		throw "Release-smoke case '$id' introduced new clipped samples."
	}
	$callbackBudgetMs = if ($profile -ceq 'Balanced') { 5.0 } else { 8.0 }
	$null = Assert-FiniteNumberAtMost -Value $case.callbackP99Ms -Maximum $callbackBudgetMs `
		-Context "Release-smoke case '$id' callbackP99Ms"
	$null = Assert-NonnegativeIntegerAtMost -Value $case.workerPendingFrames -Maximum 0 `
		-Context "Release-smoke case '$id' workerPendingFrames"
	$workerP99BudgetMs = if ($profile -ceq 'Crisp') { 8.0 } else { [double]::MaxValue }
	$workerRtfBudget = if ($profile -ceq 'Crisp') { 0.35 } else { [double]::MaxValue }
	$null = Assert-FiniteNumberAtMost -Value $case.workerP99Ms -Maximum $workerP99BudgetMs `
		-Context "Release-smoke case '$id' workerP99Ms"
	$null = Assert-FiniteNumberAtMost -Value $case.workerRtf -Maximum $workerRtfBudget `
		-Context "Release-smoke case '$id' workerRtf"

	if ((Assert-Sha256Value -Value ([string]$case.artifactSha256) -Context "Case '$id' artifact hash") -cne $artifactHash -or
		(Assert-Sha256Value -Value ([string]$case.senderExecutableSha256) -Context "Case '$id' sender hash") -cne $packagedClientHash -or
		(Assert-Sha256Value -Value ([string]$case.receiverExecutableSha256) -Context "Case '$id' receiver hash") -cne $packagedClientHash) {
		throw "Release-smoke case '$id' was not run with the exact qualified artifact and packaged clients."
	}
	$requiredModelId = [string]$requiredModelIds[$profile]
	if ([string]$case.expectedModelId -cne $requiredModelId -or [string]$case.activeModelId -cne $requiredModelId) {
		throw "Release-smoke case '$id' did not activate required model '$requiredModelId'."
	}
	$expectedModelHash = Assert-Sha256Value -Value ([string]$case.expectedModelSha256) -Context "Case '$id' expected model hash"
	$activeModelHash = Assert-Sha256Value -Value ([string]$case.activeModelSha256) -Context "Case '$id' active model hash"
	$manifestModelHash = Assert-Sha256Value -Value ([string]$modelsById[$requiredModelId].sha256) -Context "Manifest model '$requiredModelId' hash"
	if ($activeModelHash -cne $expectedModelHash -or $expectedModelHash -cne $manifestModelHash) {
		throw "Release-smoke case '$id' activated an unexpected model hash."
	}
	$requiredRecipeId = [string]$requiredRecipeIds[$profile]
	$requiredRevision = [int](Assert-ObjectProperty -Object $recipesById[$requiredRecipeId] -Name "revision" -Context "Recipe '$requiredRecipeId'")
	if ([string]$case.expectedRecipeId -cne $requiredRecipeId -or [string]$case.activeRecipeId -cne $requiredRecipeId -or
		[int]$case.expectedRecipeRevision -ne $requiredRevision -or [int]$case.activeRecipeRevision -ne $requiredRevision) {
		throw "Release-smoke case '$id' did not activate exact recipe '$requiredRecipeId' revision $requiredRevision."
	}

	foreach ($counter in @("fallbackCount", "modelHashMismatchCount", "invalidOutputCount", "tailErrorCount", "latencyErrorCount")) {
		Assert-ZeroCounter -Object $case -Name $counter -Context "Release-smoke case '$id'"
	}
	if ([int64]$case.tailDrainExpectedFrames -le 0 -or
		[int64]$case.tailDrainActualFrames -ne [int64]$case.tailDrainExpectedFrames) {
		throw "Release-smoke case '$id' did not drain the exact expected enhancement tail."
	}
	$latencyMs = [double]$case.enhancementLatencyMs
	$latencyBudgetMs = if ($profile -ceq "Balanced") { 30.0 } else { 50.0 }
	if ([double]::IsNaN($latencyMs) -or [double]::IsInfinity($latencyMs) -or $latencyMs -le 0.0 -or $latencyMs -gt $latencyBudgetMs) {
		throw "Release-smoke case '$id' enhancement latency '$latencyMs' ms exceeds the $latencyBudgetMs ms profile budget."
	}
}

if ($seenIds.Count -ne $expectedIds.Count) {
	throw "Release smoke does not contain the complete fixed 12-case matrix."
}
if ($startupBaselinePairs.Count -ne 6 -or
	@($startupBaselinePairs.Values | Where-Object { [int]$_.caseCount -ne 2 }).Count -ne 0) {
	throw "Release smoke must contain exactly one shared paired Original baseline for Balanced and Crisp in each scene/startup pair."
}

Write-Host "Release smoke verified: 12 fixed packaged localhost cases for '$ExpectedBuildId'."
