Set-StrictMode -Version Latest

function Get-ReleaseFileSha256 {
	param(
		[Parameter(Mandatory = $true)]
		[string]$Path
	)

	if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
		throw "Required release file does not exist: '$Path'."
	}

	return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Read-ReleaseJson {
	param(
		[Parameter(Mandatory = $true)]
		[string]$Path
	)

	if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
		throw "Required JSON file does not exist: '$Path'."
	}

	try {
		return Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
	} catch {
		throw "Invalid JSON in '$Path': $($_.Exception.Message)"
	}
}

function Assert-StrictInputEnhancementRolloutJson {
	param(
		[Parameter(Mandatory = $true)]
		[string]$Path,

		[Parameter(Mandatory = $true)]
		[ValidateSet("aggregate", "rollout", "rnnoise-decision")]
		[string]$Kind,

		[string]$PythonPath = "python"
	)

	$file = Get-Item -LiteralPath $Path -ErrorAction Stop
	$validator = Join-Path $PSScriptRoot "validate-input-enhancement-rollout-json.py"
	if (-not (Test-Path -LiteralPath $validator -PathType Leaf)) {
		throw "Strict rollout JSON validator is missing: '$validator'."
	}
	$validatorOutput = @(& $PythonPath $validator --kind $Kind --path $file.FullName 2>&1)
	$validatorExitCode = $LASTEXITCODE
	foreach ($line in $validatorOutput) { Write-Host ([string]$line) }
	if ($validatorExitCode -ne 0) {
		throw "Strict raw-JSON validation failed for '$($file.FullName)' as '$Kind'."
	}
}

function Write-ReleaseJson {
	param(
		[Parameter(Mandatory = $true)]
		[object]$Value,

		[Parameter(Mandatory = $true)]
		[string]$Path,

		[int]$Depth = 12
	)

	$parent = Split-Path -Parent $Path
	if (-not [string]::IsNullOrWhiteSpace($parent)) {
		New-Item -ItemType Directory -Force -Path $parent | Out-Null
	}

	$json = $Value | ConvertTo-Json -Depth $Depth
	[System.IO.File]::WriteAllText($Path, "$json`n", [System.Text.UTF8Encoding]::new($false))
}

function Assert-ObjectProperty {
	param(
		[Parameter(Mandatory = $true)]
		[object]$Object,

		[Parameter(Mandatory = $true)]
		[string]$Name,

		[Parameter(Mandatory = $true)]
		[string]$Context
	)

	if ($Object -is [System.Collections.IDictionary]) {
		if (-not $Object.Contains($Name)) {
			throw "$Context is missing required property '$Name'."
		}
		return $Object[$Name]
	}

	if (-not $Object.PSObject.Properties[$Name]) {
		throw "$Context is missing required property '$Name'."
	}

	return $Object.PSObject.Properties[$Name].Value
}

function Assert-FullGitSha {
	param(
		[Parameter(Mandatory = $true)]
		[string]$Sha,

		[string]$Context = "Source SHA"
	)

	$normalized = $Sha.Trim().ToLowerInvariant()
	if ($normalized -notmatch '^[0-9a-f]{40}$') {
		throw "$Context must be a full 40-character hexadecimal Git SHA, got '$Sha'."
	}

	return $normalized
}

function Get-InputEnhancementBuildId {
	param(
		[Parameter(Mandatory = $true)]
		[int]$BuildNumber,

		[Parameter(Mandatory = $true)]
		[string]$SourceSha
	)

	if ($BuildNumber -le 0) {
		throw "Build number must be greater than zero."
	}

	$sha = Assert-FullGitSha -Sha $SourceSha
	return "mumble-forked-build-$BuildNumber-$($sha.Substring(0, 12))"
}

function Assert-TestGateResults {
	param(
		[Parameter(Mandatory = $true)]
		[object]$GateResults,

		[string[]]$RequiredGates = @(
			"DeepFilterNetCapiTests",
			"TestInputEnhancement",
			"TestInputEnhancementAuto",
			"TestInputEnhancementAutoV2",
			"TestInputEnhancementCalibration",
			"TestInputEnhancementCalibrationRuntime",
			"TestInputEnhancementPolicy",
			"TestInputEnhancementPolicyConfiguredKey",
			"TestInputEnhancementPolicyController",
			"TestInputEnhancementPackageVerifier",
			"TestInputEnhancementSettings",
			"TestInputEnhancementCalibrationPlayback",
			"TestModernDialogControllers",
			"TestQmlQuickComponents",
			"TestUpdateHealth",
			"TestUpdaterHealthIntegration",
			"TestUpdaterProtocolV4Simulation",
			"TestSpeechCleanup",
			"SpeechCleanupBenchmarkSelfTest"
		)
	)

	$schemaVersion = Assert-ObjectProperty -Object $GateResults -Name "schemaVersion" -Context "Test gate results"
	if ([int]$schemaVersion -ne 1) {
		throw "Unsupported test gate results schema version '$schemaVersion'."
	}
	if ((Assert-ObjectProperty -Object $GateResults -Name "passed" -Context "Test gate results") -ne $true) {
		throw "Test gate results do not report passed=true."
	}

	$gates = @(Assert-ObjectProperty -Object $GateResults -Name "gates" -Context "Test gate results")
	if ($gates.Count -eq 0) {
		throw "Test gate results contain no gates."
	}

	foreach ($gate in $gates) {
		$name = [string](Assert-ObjectProperty -Object $gate -Name "name" -Context "Test gate")
		if ([string]::IsNullOrWhiteSpace($name)) {
			throw "A test gate has an empty name."
		}
		if ((Assert-ObjectProperty -Object $gate -Name "passed" -Context "Test gate '$name'") -ne $true) {
			throw "Test gate '$name' did not pass."
		}
		if ([int](Assert-ObjectProperty -Object $gate -Name "exitCode" -Context "Test gate '$name'") -ne 0) {
			throw "Test gate '$name' has a non-zero exit code."
		}
	}

	foreach ($requiredGate in $RequiredGates) {
		$matches = @($gates | Where-Object { [string]$_.name -ceq $requiredGate })
		if ($matches.Count -ne 1) {
			throw "Test gate results must contain exactly one passing '$requiredGate' gate."
		}
	}
}

function Assert-SigningResults {
	param(
		[Parameter(Mandatory = $true)]
		[object]$SigningResults,

		[string]$ExpectedSignerSubject = ""
	)

	$schemaVersion = Assert-ObjectProperty -Object $SigningResults -Name "schemaVersion" -Context "Signing results"
	if ([int]$schemaVersion -ne 1) {
		throw "Unsupported signing results schema version '$schemaVersion'."
	}
	if ((Assert-ObjectProperty -Object $SigningResults -Name "verified" -Context "Signing results") -ne $true) {
		throw "Signing results do not report verified=true."
	}

	$reportedSubject = [string](Assert-ObjectProperty -Object $SigningResults -Name "expectedSignerSubject" -Context "Signing results")
	if ([string]::IsNullOrWhiteSpace($reportedSubject)) {
		throw "Signing results have an empty expected signer subject."
	}
	if (-not [string]::IsNullOrWhiteSpace($ExpectedSignerSubject) -and $reportedSubject -cne $ExpectedSignerSubject) {
		throw "Signing results signer subject '$reportedSubject' does not match expected '$ExpectedSignerSubject'."
	}

	$files = @(Assert-ObjectProperty -Object $SigningResults -Name "files" -Context "Signing results")
	if ($files.Count -eq 0) {
		throw "Signing results contain no signed files."
	}

	foreach ($file in $files) {
		$path = [string](Assert-ObjectProperty -Object $file -Name "path" -Context "Signed file")
		if ([string]::IsNullOrWhiteSpace($path)) {
			throw "Signing results contain a file with an empty path."
		}
		$sha256 = [string](Assert-ObjectProperty -Object $file -Name "sha256" -Context "Signed file '$path'")
		if ($sha256 -cnotmatch '^[0-9a-f]{64}$') {
			throw "Signed file '$path' has an invalid SHA-256 value."
		}
		if ([string](Assert-ObjectProperty -Object $file -Name "status" -Context "Signed file '$path'") -cne "Valid") {
			throw "Signed file '$path' does not report a Valid Authenticode signature."
		}
		if ((Assert-ObjectProperty -Object $file -Name "timestamped" -Context "Signed file '$path'") -ne $true) {
			throw "Signed file '$path' does not report an RFC3161 timestamp."
		}
		if ([string](Assert-ObjectProperty -Object $file -Name "signerSubject" -Context "Signed file '$path'") -cne $reportedSubject) {
			throw "Signed file '$path' was not signed by the expected subject."
		}
	}
}

function Assert-SafeRelativeReleasePath {
	param(
		[Parameter(Mandatory = $true)]
		[string]$Path,

		[Parameter(Mandatory = $true)]
		[string]$Context
	)

	if ([string]::IsNullOrWhiteSpace($Path) -or [System.IO.Path]::IsPathRooted($Path)) {
		throw "$Context must be a non-empty relative path."
	}

	$segments = @($Path -split '[\\/]')
	if ($segments.Count -eq 0 -or @($segments | Where-Object { $_ -eq "" -or $_ -eq "." -or $_ -eq ".." }).Count -gt 0) {
		throw "$Context contains an unsafe path segment: '$Path'."
	}

	return ($segments -join "/")
}

function Initialize-InputEnhancementRehearsalOutputRoot {
	param(
		[Parameter(Mandatory = $true)]
		[string]$OutputRoot,

		[Parameter(Mandatory = $true)]
		[string]$AllowedOutputParent,

		[Parameter(Mandatory = $true)]
		[string]$SourceRoot
	)

	$sourceItem = Get-Item -LiteralPath $SourceRoot -Force -ErrorAction Stop
	$parentItem = Get-Item -LiteralPath $AllowedOutputParent -Force -ErrorAction Stop
	if (-not $sourceItem.PSIsContainer -or
		($sourceItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
		throw 'Release rehearsal source root must be a regular directory.'
	}
	if (-not $parentItem.PSIsContainer -or
		($parentItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
		throw 'Release rehearsal output parent must be a regular directory.'
	}

	$sourceRootPath = [IO.Path]::GetFullPath($sourceItem.FullName).TrimEnd('\', '/')
	$parentPath = [IO.Path]::GetFullPath($parentItem.FullName).TrimEnd('\', '/')
	$outputRootPath = [IO.Path]::GetFullPath($OutputRoot).TrimEnd('\', '/')
	$outputPathRoot = [IO.Path]::GetPathRoot($outputRootPath).TrimEnd('\', '/')
	if ([string]::IsNullOrWhiteSpace($outputRootPath) -or
		$outputRootPath.Equals($outputPathRoot, [StringComparison]::OrdinalIgnoreCase)) {
		throw 'Release rehearsal output root cannot be a filesystem root.'
	}
	$actualParent = [IO.Path]::GetDirectoryName($outputRootPath).TrimEnd('\', '/')
	if (-not $actualParent.Equals($parentPath, [StringComparison]::OrdinalIgnoreCase)) {
		throw 'Release rehearsal output root must be a direct child of the allowed output parent.'
	}
	if ($outputRootPath.Equals($sourceRootPath, [StringComparison]::OrdinalIgnoreCase) -or
		$outputRootPath.StartsWith("$sourceRootPath\", [StringComparison]::OrdinalIgnoreCase) -or
		$sourceRootPath.StartsWith("$outputRootPath\", [StringComparison]::OrdinalIgnoreCase)) {
		throw 'Release rehearsal output root must not overlap the source checkout.'
	}
	if (Test-Path -LiteralPath $outputRootPath) {
		throw 'Release rehearsal output root must not already exist.'
	}

	New-Item -ItemType Directory -Path $outputRootPath -ErrorAction Stop | Out-Null
	return $outputRootPath
}

function Assert-InputEnhancementRehearsalDraftFileSafety {
	param(
		[Parameter(Mandatory = $true)]
		[IO.FileInfo]$File,

		[Parameter(Mandatory = $true)]
		[string]$RelativePath
	)

	$forbiddenExtensions = @(
		'.aac', '.aif', '.aiff', '.alac', '.amr', '.au', '.avi', '.caf', '.flac', '.key', '.m4a', '.mka',
		'.mkv', '.mov', '.mp3', '.mp4', '.mpeg', '.mpg', '.ogg', '.opus', '.p12', '.p7b', '.p7c', '.p8',
		'.pem', '.pfx', '.ppk', '.raw', '.wav', '.wave', '.weba', '.webm', '.wma', '.jks', '.kdbx', '.keystore'
	)
	if ($File.Extension.ToLowerInvariant() -cin $forbiddenExtensions -or
		$File.Name -match '(?i)(private[-_.]?key|client[-_.]?secret|production[-_.]?credential|credentials?|id_(rsa|dsa|ecdsa|ed25519))') {
		throw "Rehearsal draft contains forbidden private material or audio: '$RelativePath'."
	}

	$textExtensions = @(
		'.csv', '.htm', '.html', '.json', '.log', '.md', '.ps1', '.psd1', '.psm1', '.sha256',
		'.txt', '.xml', '.yaml', '.yml'
	)
	$scanLength = if ($File.Extension.ToLowerInvariant() -cin $textExtensions) {
		[int64]$File.Length
	} else {
		[int64][Math]::Min([int64]65536, [int64]$File.Length)
	}
	if ($scanLength -eq 0) { return }

	$privateKeyPattern = '-----BEGIN (?:ENCRYPTED |RSA |EC |DSA |OPENSSH )?PRIVATE KEY-----'
	$buffer = [byte[]]::new(65536)
	$carry = ''
	$remaining = $scanLength
	$stream = [IO.File]::Open($File.FullName, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
	try {
		while ($remaining -gt 0) {
			$requested = [int][Math]::Min([int64]$buffer.Length, $remaining)
			$bytesRead = $stream.Read($buffer, 0, $requested)
			if ($bytesRead -le 0) { break }
			$remaining -= $bytesRead

			$text = $carry + [Text.Encoding]::ASCII.GetString($buffer, 0, $bytesRead)
			if ($text -match $privateKeyPattern) {
				throw "Rehearsal draft contains PEM private-key material: '$RelativePath'."
			}
			# Retain enough decoded bytes to catch a marker split across read boundaries.
			$carryLength = [Math]::Min(64, $text.Length)
			$carry = $text.Substring($text.Length - $carryLength, $carryLength)
		}
	} finally {
		$stream.Dispose()
	}
}

function Get-ValidatedInputEnhancementRehearsalDraftFiles {
	param(
		[Parameter(Mandatory = $true)]
		[string]$Root,

		[string]$ExcludedPath = ''
	)

	$rootItem = Get-Item -LiteralPath $Root -Force -ErrorAction Stop
	if (-not $rootItem.PSIsContainer -or
		($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
		throw 'Rehearsal draft root must be a regular directory.'
	}
	$rootPath = [IO.Path]::GetFullPath($rootItem.FullName).TrimEnd('\', '/')
	$excludedFullPath = ''
	if (-not [string]::IsNullOrWhiteSpace($ExcludedPath)) {
		$excludedFullPath = [IO.Path]::GetFullPath($ExcludedPath)
		$excludedParent = [IO.Path]::GetDirectoryName($excludedFullPath).TrimEnd('\', '/')
		if (-not $excludedParent.Equals($rootPath, [StringComparison]::OrdinalIgnoreCase)) {
			throw 'Rehearsal draft manifest must be a direct child of the draft root.'
		}
	}

	$rehearsalPath = Join-Path $rootPath 'rehearsal.json'
	$rehearsalItem = Get-Item -LiteralPath $rehearsalPath -Force -ErrorAction Stop
	if ($rehearsalItem.PSIsContainer -or
		($rehearsalItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
		throw 'Rehearsal draft identity must be a regular rehearsal.json file.'
	}
	$rehearsal = Read-ReleaseJson -Path $rehearsalItem.FullName
	$artifacts = Assert-ObjectProperty -Object $rehearsal -Name 'artifacts' -Context 'Release rehearsal'
	$artifactProperties = @($artifacts.PSObject.Properties)
	if ($artifactProperties.Count -eq 0) {
		throw 'Release rehearsal contains no artifact allowlist.'
	}

	$allowedFiles = New-Object System.Collections.Generic.HashSet[string]([StringComparer]::Ordinal)
	$null = $allowedFiles.Add('rehearsal.json')
	foreach ($property in $artifactProperties) {
		$fileName = Assert-SafeRelativeReleasePath `
			-Path ([string](Assert-ObjectProperty -Object $property.Value -Name 'fileName' `
				-Context "Release rehearsal artifact '$($property.Name)'")) `
			-Context "Release rehearsal artifact '$($property.Name)'"
		if ($fileName.Contains('/')) {
			throw "Release rehearsal artifact '$($property.Name)' must be a direct child of the draft root."
		}
		if (-not $allowedFiles.Add($fileName)) {
			throw "Release rehearsal artifact allowlist contains duplicate file '$fileName'."
		}
	}

	# Schema-v2 release rehearsals carry the complete prepared unsigned/signed
	# trees so a remote verifier can recompute the declared transformation. The
	# canonical challenge is itself a direct artifact and is the sole allowlist
	# for those two subtrees.
	$challengeProperty = $artifacts.PSObject.Properties['rehearsalChallenge']
	if ($null -ne $challengeProperty) {
		$challengeFileName = Assert-SafeRelativeReleasePath `
			-Path ([string](Assert-ObjectProperty -Object $challengeProperty.Value -Name 'fileName' `
				-Context 'Release rehearsal challenge')) `
			-Context 'Release rehearsal challenge'
		$challengePath = Join-Path $rootPath $challengeFileName
		$challenge = Read-ReleaseJson -Path $challengePath
		if ([int](Assert-ObjectProperty $challenge 'schemaVersion' 'Rehearsal challenge') -ne 1 -or
			[string](Assert-ObjectProperty $challenge 'kind' 'Rehearsal challenge') -cne
				'input-enhancement-pre-azure-rehearsal-challenge') {
			throw 'Release rehearsal challenge artifact has an unsupported schema.'
		}
		foreach ($treeName in @('unsigned', 'signed')) {
			$tree = Assert-ObjectProperty $challenge $treeName "Rehearsal challenge $treeName tree"
			$treeRoot = Assert-SafeRelativeReleasePath `
				-Path ([string](Assert-ObjectProperty $tree 'root' "Rehearsal challenge $treeName tree")) `
				-Context "Rehearsal challenge $treeName root"
			if ($treeRoot.Contains('/')) { throw 'Rehearsal challenge tree roots must be direct children.' }
			foreach ($record in @(Assert-ObjectProperty $tree 'files' "Rehearsal challenge $treeName tree")) {
				$relative = Assert-SafeRelativeReleasePath `
					-Path ([string](Assert-ObjectProperty $record 'path' "Rehearsal challenge $treeName file")) `
					-Context "Rehearsal challenge $treeName file"
				$path = "$treeRoot/$relative"
				if (-not $allowedFiles.Add($path)) {
					throw "Rehearsal challenge allowlist contains duplicate file '$path'."
				}
			}
		}
	}

	$listeningProperty = $artifacts.PSObject.Properties['listeningQualification']
	if ($null -ne $listeningProperty) {
		$listeningFileName = Assert-SafeRelativeReleasePath `
			-Path ([string](Assert-ObjectProperty -Object $listeningProperty.Value -Name 'fileName' `
				-Context 'Release rehearsal listening qualification')) `
			-Context 'Release rehearsal listening qualification'
		$listeningItem = Get-Item -LiteralPath (Join-Path $rootPath $listeningFileName) -Force -ErrorAction Stop
		if ($listeningItem.PSIsContainer -or
			($listeningItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
			throw 'Listening qualification must be a regular draft file.'
		}
		$listening = Read-ReleaseJson -Path $listeningItem.FullName
		$sessionManifest = Assert-ObjectProperty -Object $listening -Name 'session_manifest' `
			-Context 'Listening qualification'
		$evidenceRoot = Assert-SafeRelativeReleasePath `
			-Path ([string](Assert-ObjectProperty -Object $sessionManifest -Name 'evidence_root' `
				-Context 'Listening session manifest')) `
			-Context 'Listening evidence root'
		if ($evidenceRoot.Contains('/') -or $evidenceRoot -cnotmatch '^[A-Za-z0-9][A-Za-z0-9._-]*\.evidence$') {
			throw 'Listening evidence root must be one direct sibling .evidence directory.'
		}

		$evidenceReferences = New-Object System.Collections.Generic.List[object]
		$evidenceReferences.Add((Assert-ObjectProperty -Object $sessionManifest -Name 'source_manifest' `
			-Context 'Listening session manifest'))
		$evidenceReferences.Add((Assert-ObjectProperty -Object $sessionManifest -Name 'answer_key' `
			-Context 'Listening session manifest'))
		$sessions = @(Assert-ObjectProperty -Object $sessionManifest -Name 'sessions' `
			-Context 'Listening session manifest')
		if ($sessions.Count -eq 0) { throw 'Listening session manifest contains no session evidence.' }
		foreach ($session in $sessions) { $evidenceReferences.Add($session) }
		foreach ($reference in $evidenceReferences) {
			$relativePath = Assert-SafeRelativeReleasePath `
				-Path ([string](Assert-ObjectProperty -Object $reference -Name 'relative_path' `
					-Context 'Listening evidence reference')) `
				-Context 'Listening evidence reference'
			$sha256 = [string](Assert-ObjectProperty -Object $reference -Name 'sha256' `
				-Context "Listening evidence reference '$relativePath'")
			if (-not $relativePath.StartsWith("$evidenceRoot/", [StringComparison]::Ordinal) -or
				$sha256 -cnotmatch '^[0-9a-f]{64}$') {
				throw "Listening evidence reference '$relativePath' escapes its root or has an invalid SHA-256."
			}
			if (-not $allowedFiles.Add($relativePath)) {
				throw "Listening evidence allowlist contains duplicate file '$relativePath'."
			}
		}
	}

	$allowedDirectories = New-Object System.Collections.Generic.HashSet[string]([StringComparer]::Ordinal)
	foreach ($relativePath in $allowedFiles) {
		$parent = [IO.Path]::GetDirectoryName($relativePath.Replace('/', '\'))
		while (-not [string]::IsNullOrWhiteSpace($parent)) {
			$null = $allowedDirectories.Add($parent.Replace('\', '/'))
			$parent = [IO.Path]::GetDirectoryName($parent)
		}
	}

	$actualFiles = New-Object System.Collections.Generic.HashSet[string]([StringComparer]::Ordinal)
	$actualDirectories = New-Object System.Collections.Generic.HashSet[string]([StringComparer]::Ordinal)
	$validatedFiles = New-Object System.Collections.Generic.List[IO.FileInfo]
	foreach ($item in @(Get-ChildItem -LiteralPath $rootPath -Force -Recurse)) {
		if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
			throw "Rehearsal draft contains a reparse point: '$($item.FullName)'."
		}
		$fullPath = [IO.Path]::GetFullPath($item.FullName)
		if (-not $item.PSIsContainer -and -not [string]::IsNullOrWhiteSpace($excludedFullPath) -and
			$fullPath.Equals($excludedFullPath, [StringComparison]::OrdinalIgnoreCase)) {
			continue
		}
		$relativePath = Assert-SafeRelativeReleasePath `
			-Path ([IO.Path]::GetRelativePath($rootPath, $fullPath).Replace('\', '/')) `
			-Context 'Rehearsal draft item'
		if ($item.PSIsContainer) {
			$null = $actualDirectories.Add($relativePath)
			continue
		}
		Assert-InputEnhancementRehearsalDraftFileSafety -File $item -RelativePath $relativePath
		$null = $actualFiles.Add($relativePath)
		$validatedFiles.Add($item)
	}

	$missingFiles = @($allowedFiles | Where-Object { -not $actualFiles.Contains($_) } | Sort-Object)
	$unexpectedFiles = @($actualFiles | Where-Object { -not $allowedFiles.Contains($_) } | Sort-Object)
	if ($missingFiles.Count -ne 0 -or $unexpectedFiles.Count -ne 0) {
		throw "Rehearsal draft file-set mismatch. Missing: [$($missingFiles -join ', ')]; unexpected: [$($unexpectedFiles -join ', ')]."
	}
	$missingDirectories = @($allowedDirectories | Where-Object { -not $actualDirectories.Contains($_) } | Sort-Object)
	$unexpectedDirectories = @($actualDirectories | Where-Object { -not $allowedDirectories.Contains($_) } | Sort-Object)
	if ($missingDirectories.Count -ne 0 -or $unexpectedDirectories.Count -ne 0) {
		throw "Rehearsal draft directory-set mismatch. Missing: [$($missingDirectories -join ', ')]; unexpected: [$($unexpectedDirectories -join ', ')]."
	}

	return @($validatedFiles | Sort-Object FullName)
}

function Assert-Ed25519PublicKeyHex {
	param(
		[Parameter(Mandatory = $true)]
		[string]$PublicKeyHex,

		[string]$Context = "Ed25519 public key"
	)

	$normalized = $PublicKeyHex.Trim().ToLowerInvariant()
	if ($normalized -notmatch '^[0-9a-f]{64}$') {
		throw "$Context must be exactly 32 bytes encoded as 64 hexadecimal characters."
	}
	return $normalized
}

function Assert-CanonicalInputEnhancementPolicy {
	param(
		[Parameter(Mandatory = $true)]
		[string]$Path,

		[uint64]$ExpectedMinBuild = [uint64]::MaxValue,

		[string]$ExpectedRecipeSetVersion = "",

		[switch]$RequireCurrentlyValid
	)

	if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
		throw "Signed policy file does not exist: '$Path'."
	}
	$raw = [IO.File]::ReadAllBytes((Get-Item -LiteralPath $Path).FullName)
	if ($raw.Length -eq 0 -or $raw.Length -gt 2048) {
		throw "Signed policy has an unsafe byte length."
	}
	try {
		$strictUtf8 = New-Object Text.UTF8Encoding($false, $true)
		$text = $strictUtf8.GetString($raw)
		$policy = $text | ConvertFrom-Json
	} catch {
		throw "Signed policy is not strict UTF-8 JSON: $($_.Exception.Message)"
	}
	$expectedFields = @('available', 'expiresAt', 'forceOriginal', 'minBuild', 'recipeSetVersion', 'recommendedProfile')
	$actualFields = @($policy.PSObject.Properties.Name)
	if ($actualFields.Count -ne $expectedFields.Count) {
		throw "Signed policy must contain exactly the six policy fields."
	}
	for ($index = 0; $index -lt $expectedFields.Count; ++$index) {
		if ([string]$actualFields[$index] -cne $expectedFields[$index]) {
			throw "Signed policy fields are not in canonical lexical order."
		}
	}
	if ($policy.available -isnot [bool] -or $policy.forceOriginal -isnot [bool]) {
		throw "Signed policy available and forceOriginal fields must be booleans."
	}
	$profile = [string]$policy.recommendedProfile
	# Voice Focus is deliberately manual-only and must never arrive as a
	# remotely recommended profile.
	if ($profile -cnotin @('Original', 'Light', 'Balanced', 'Quality', 'Auto')) {
		throw "Signed policy has an unsupported recommendedProfile."
	}
	$recipe = [string]$policy.recipeSetVersion
	if ($recipe -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$') {
		throw "Signed policy has an invalid recipeSetVersion."
	}
	$minBuildText = [Convert]::ToString($policy.minBuild, [Globalization.CultureInfo]::InvariantCulture)
	[uint64]$minBuild = 0
	if (-not [uint64]::TryParse($minBuildText, [Globalization.NumberStyles]::None,
		[Globalization.CultureInfo]::InvariantCulture, [ref]$minBuild) -or $minBuild -gt 9007199254740991) {
		throw "Signed policy has an invalid minBuild."
	}
	# PowerShell 7 eagerly converts ISO JSON strings to DateTime. Extract the
	# source token as text so canonical byte validation is version-independent.
	$expirationMatches = [regex]::Matches($text, '"expiresAt":"([^"\\]*)"')
	if ($expirationMatches.Count -ne 1) {
		throw "Signed policy has an invalid expiresAt token."
	}
	$expiresText = $expirationMatches[0].Groups[1].Value
	[DateTimeOffset]$expires = [DateTimeOffset]::MinValue
	if (-not [DateTimeOffset]::TryParseExact($expiresText, "yyyy-MM-dd'T'HH:mm:ss'Z'",
		[Globalization.CultureInfo]::InvariantCulture,
		[Globalization.DateTimeStyles]::AssumeUniversal -bor [Globalization.DateTimeStyles]::AdjustToUniversal,
		[ref]$expires)) {
		throw "Signed policy has a non-canonical expiresAt timestamp."
	}
	if ($RequireCurrentlyValid -and
		($expires -le [DateTimeOffset]::UtcNow -or $expires -gt [DateTimeOffset]::UtcNow.AddDays(31))) {
		throw "Signed policy is expired or exceeds the client's maximum validity window."
	}
	if ($ExpectedMinBuild -ne [uint64]::MaxValue -and $minBuild -ne $ExpectedMinBuild) {
		throw "Signed policy minBuild does not match the qualified build."
	}
	if (-not [string]::IsNullOrWhiteSpace($ExpectedRecipeSetVersion) -and $recipe -cne $ExpectedRecipeSetVersion) {
		throw "Signed policy recipeSetVersion does not match the qualified recipe catalog."
	}

	$canonical = '{"available":' + $(if ($policy.available) { 'true' } else { 'false' }) +
		',"expiresAt":"' + $expiresText +
		'","forceOriginal":' + $(if ($policy.forceOriginal) { 'true' } else { 'false' }) +
		',"minBuild":' + $minBuild.ToString([Globalization.CultureInfo]::InvariantCulture) +
		',"recipeSetVersion":"' + $recipe +
		'","recommendedProfile":"' + $profile + '"}'
	$canonicalBytes = [Text.Encoding]::UTF8.GetBytes($canonical)
	if ($canonicalBytes.Length -ne $raw.Length) {
		throw "Signed policy JSON bytes are not canonical."
	}
	for ($index = 0; $index -lt $raw.Length; ++$index) {
		if ($raw[$index] -ne $canonicalBytes[$index]) {
			throw "Signed policy JSON bytes are not canonical."
		}
	}
	return $policy
}

function Assert-InputEnhancementPromotionPolicy {
	param(
		[Parameter(Mandatory = $true)]
		[ValidateSet("preview", "stable")]
		[string]$Channel,

		[Parameter(Mandatory = $true)]
		[bool]$Available,

		[Parameter(Mandatory = $true)]
		[bool]$ForceOriginal,

		[Parameter(Mandatory = $true)]
		[ValidateSet("Original", "Light", "Balanced", "Quality", "Auto")]
		[string]$RecommendedProfile,

		[Parameter(Mandatory = $true)]
		[ValidateSet("private-community", "public")]
		[string]$RolloutAudience,

		[Parameter(Mandatory = $true)]
		[bool]$RolloutEvidenceAvailable
	)

	$emergencyPolicy = -not $Available -or $ForceOriginal
	if (-not $emergencyPolicy -and $Channel -ceq "preview" -and $RecommendedProfile -ceq "Auto") {
		throw "A non-emergency preview promotion cannot recommend Auto. Auto requires stable rollout evidence."
	}
	if (-not $emergencyPolicy -and $Channel -ceq "stable" -and
		$RolloutAudience -ceq "private-community" -and $RecommendedProfile -ceq "Auto") {
		throw "The private community stage cannot recommend Auto. Auto requires the later public rollout stages."
	}

	$rolloutRequired = $Channel -ceq "stable" -and -not $emergencyPolicy
	if ($rolloutRequired -and -not $RolloutEvidenceAvailable) {
		throw "A non-emergency stable promotion requires signed rollout evidence."
	}

	$targetStage = "none"
	if ($rolloutRequired) {
		$targetStage = if ($RolloutAudience -ceq "private-community") {
			"community-stable"
		} elseif ($RecommendedProfile -ceq "Auto") {
			"auto-recommended"
		} else {
			"stable-opt-in"
		}
	}

	return [pscustomobject][ordered]@{
		emergencyPolicy = $emergencyPolicy
		rolloutRequired = $rolloutRequired
		rolloutAudience = $RolloutAudience
		targetStage = $targetStage
	}
}

function Resolve-InputEnhancementOpenSsl {
	param([string]$OpenSslPath = "")

	$candidates = New-Object System.Collections.Generic.List[string]
	if (-not [string]::IsNullOrWhiteSpace($OpenSslPath)) {
		$candidates.Add($OpenSslPath)
	} else {
		$command = Get-Command openssl -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
		if ($command) {
			$candidates.Add($command.Source)
		}
		if ($env:ProgramFiles) {
			$candidates.Add((Join-Path $env:ProgramFiles 'Git\usr\bin\openssl.exe'))
			$candidates.Add((Join-Path $env:ProgramFiles 'Git\mingw64\bin\openssl.exe'))
		}
	}

	foreach ($candidate in $candidates) {
		if (-not [string]::IsNullOrWhiteSpace($candidate) -and (Test-Path -LiteralPath $candidate -PathType Leaf)) {
			return (Get-Item -LiteralPath $candidate -ErrorAction Stop).FullName
		}
	}
	throw "OpenSSL was not found. Supply -OpenSslPath or install an OpenSSL executable discoverable on PATH."
}

function Convert-HexToReleaseBytes {
	param([Parameter(Mandatory = $true)][string]$Hex)
	if (($Hex.Length % 2) -ne 0 -or $Hex -notmatch '^[0-9a-fA-F]+$') {
		throw "Invalid hexadecimal byte string."
	}
	$bytes = New-Object byte[] ($Hex.Length / 2)
	for ($index = 0; $index -lt $bytes.Length; ++$index) {
		$bytes[$index] = [Convert]::ToByte($Hex.Substring($index * 2, 2), 16)
	}
	return $bytes
}

function Convert-ReleaseBytesToHex {
	param(
		[Parameter(Mandatory = $true)][byte[]]$Bytes,
		[int]$Offset = 0,
		[int]$Count = -1
	)
	if ($Offset -lt 0 -or $Offset -gt $Bytes.Length) { throw "Invalid byte offset." }
	if ($Count -lt 0) { $Count = $Bytes.Length - $Offset }
	if ($Count -lt 0 -or ($Offset + $Count) -gt $Bytes.Length) { throw "Invalid byte count." }
	$builder = New-Object Text.StringBuilder ($Count * 2)
	for ($index = $Offset; $index -lt ($Offset + $Count); ++$index) {
		$null = $builder.Append($Bytes[$index].ToString('x2'))
	}
	return $builder.ToString()
}

function Invoke-InputEnhancementOpenSsl {
	param(
		[Parameter(Mandatory = $true)]
		[string]$OpenSslPath,

		[Parameter(Mandatory = $true)]
		[string[]]$Arguments,

		[Parameter(Mandatory = $true)]
		[string]$Context,

		[switch]$AllowFailure
	)

	$output = @(& $OpenSslPath @Arguments 2>&1)
	$exitCode = $LASTEXITCODE
	if ($exitCode -ne 0 -and -not $AllowFailure) {
		$detail = ($output | ForEach-Object { [string]$_ }) -join ' '
		throw "$Context failed with OpenSSL exit code $exitCode. $detail"
	}
	return [pscustomobject]@{ ExitCode = $exitCode; Output = $output }
}

function New-Ed25519SubjectPublicKeyInfo {
	param(
		[Parameter(Mandatory = $true)]
		[string]$PublicKeyHex
	)

	$normalized = Assert-Ed25519PublicKeyHex -PublicKeyHex $PublicKeyHex
	# RFC 8410 SubjectPublicKeyInfo prefix for id-Ed25519, followed by the
	# 32-byte raw public key. Keeping this construction explicit avoids a PEM
	# parser ambiguity in verification jobs that only receive the public key.
	$prefix = Convert-HexToReleaseBytes -Hex '302a300506032b6570032100'
	$key = Convert-HexToReleaseBytes -Hex $normalized
	$result = New-Object byte[] ($prefix.Length + $key.Length)
	[Array]::Copy($prefix, 0, $result, 0, $prefix.Length)
	[Array]::Copy($key, 0, $result, $prefix.Length, $key.Length)
	return $result
}

function Get-Ed25519PublicKeyHexFromPrivateKey {
	param(
		[Parameter(Mandatory = $true)]
		[string]$PrivateKeyBase64,

		[string]$OpenSslPath = ""
	)

	$openssl = Resolve-InputEnhancementOpenSsl -OpenSslPath $OpenSslPath
	try {
		$privateBytes = [Convert]::FromBase64String($PrivateKeyBase64.Trim())
	} catch {
		throw "The protected Ed25519 private key is not valid base64."
	}
	if ($privateBytes.Length -eq 0) {
		throw "The protected Ed25519 private key is empty."
	}
	$privateText = [Text.Encoding]::UTF8.GetString($privateBytes)
	if ($privateText -notmatch '-----BEGIN PRIVATE KEY-----' -or
		$privateText -notmatch '-----END PRIVATE KEY-----' -or
		$privateText -match '-----BEGIN ENCRYPTED PRIVATE KEY-----') {
		throw "The protected Ed25519 key must be an unencrypted PKCS#8 PEM private key."
	}

	$tempRoot = Join-Path ([IO.Path]::GetTempPath()) "mumble-ed25519-public-$([guid]::NewGuid().ToString('N'))"
	New-Item -ItemType Directory -Force -Path $tempRoot | Out-Null
	try {
		$privatePath = Join-Path $tempRoot 'private.pem'
		$publicDerPath = Join-Path $tempRoot 'public.der'
		[IO.File]::WriteAllBytes($privatePath, $privateBytes)
		$null = Invoke-InputEnhancementOpenSsl -OpenSslPath $openssl `
			-Arguments @('pkey', '-in', $privatePath, '-pubout', '-outform', 'DER', '-out', $publicDerPath) `
			-Context 'Deriving the Ed25519 public key'
		$der = [IO.File]::ReadAllBytes($publicDerPath)
		$expectedPrefix = Convert-HexToReleaseBytes -Hex '302a300506032b6570032100'
		if ($der.Length -ne ($expectedPrefix.Length + 32)) {
			throw "The protected private key is not an Ed25519 key."
		}
		for ($index = 0; $index -lt $expectedPrefix.Length; ++$index) {
			if ($der[$index] -ne $expectedPrefix[$index]) {
				throw "The protected private key is not an Ed25519 key."
			}
		}
		return Convert-ReleaseBytesToHex -Bytes $der -Offset $expectedPrefix.Length -Count 32
	} finally {
		if ($privateBytes) { [Array]::Clear($privateBytes, 0, $privateBytes.Length) }
		Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
	}
}

function Test-Ed25519DetachedSignature {
	param(
		[Parameter(Mandatory = $true)]
		[string]$InputPath,

		[Parameter(Mandatory = $true)]
		[string]$SignaturePath,

		[Parameter(Mandatory = $true)]
		[string]$PublicKeyHex,

		[string]$OpenSslPath = ""
	)

	if (-not (Test-Path -LiteralPath $InputPath -PathType Leaf) -or
		-not (Test-Path -LiteralPath $SignaturePath -PathType Leaf)) {
		return $false
	}
	$signature = [IO.File]::ReadAllBytes((Get-Item -LiteralPath $SignaturePath).FullName)
	if ($signature.Length -ne 64) {
		return $false
	}
	$publicDer = New-Ed25519SubjectPublicKeyInfo -PublicKeyHex $PublicKeyHex
	$openssl = Resolve-InputEnhancementOpenSsl -OpenSslPath $OpenSslPath
	$tempRoot = Join-Path ([IO.Path]::GetTempPath()) "mumble-ed25519-verify-$([guid]::NewGuid().ToString('N'))"
	New-Item -ItemType Directory -Force -Path $tempRoot | Out-Null
	try {
		$publicPath = Join-Path $tempRoot 'public.der'
		[IO.File]::WriteAllBytes($publicPath, $publicDer)
		$result = Invoke-InputEnhancementOpenSsl -OpenSslPath $openssl `
			-Arguments @('pkeyutl', '-verify', '-pubin', '-keyform', 'DER', '-inkey', $publicPath,
				'-rawin', '-in', (Get-Item -LiteralPath $InputPath).FullName,
				'-sigfile', (Get-Item -LiteralPath $SignaturePath).FullName) `
			-Context 'Verifying the detached Ed25519 signature' -AllowFailure
		return $result.ExitCode -eq 0
	} finally {
		Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
	}
}

function Protect-FileWithEd25519 {
	param(
		[Parameter(Mandatory = $true)]
		[string]$InputPath,

		[Parameter(Mandatory = $true)]
		[string]$SignaturePath,

		[Parameter(Mandatory = $true)]
		[string]$PrivateKeyBase64,

		[Parameter(Mandatory = $true)]
		[string]$ExpectedPublicKeyHex,

		[string]$OpenSslPath = ""
	)

	if (-not (Test-Path -LiteralPath $InputPath -PathType Leaf)) {
		throw "The file to sign does not exist: '$InputPath'."
	}
	$expectedKey = Assert-Ed25519PublicKeyHex -PublicKeyHex $ExpectedPublicKeyHex
	$openssl = Resolve-InputEnhancementOpenSsl -OpenSslPath $OpenSslPath
	$derivedKey = Get-Ed25519PublicKeyHexFromPrivateKey -PrivateKeyBase64 $PrivateKeyBase64 -OpenSslPath $openssl
	if ($derivedKey -cne $expectedKey) {
		throw "The protected Ed25519 private key does not match the configured public key."
	}

	try {
		$privateBytes = [Convert]::FromBase64String($PrivateKeyBase64.Trim())
	} catch {
		throw "The protected Ed25519 private key is not valid base64."
	}
	$tempRoot = Join-Path ([IO.Path]::GetTempPath()) "mumble-ed25519-sign-$([guid]::NewGuid().ToString('N'))"
	New-Item -ItemType Directory -Force -Path $tempRoot | Out-Null
	try {
		$privatePath = Join-Path $tempRoot 'private.pem'
		$tempSignaturePath = Join-Path $tempRoot 'signature.raw'
		[IO.File]::WriteAllBytes($privatePath, $privateBytes)
		$null = Invoke-InputEnhancementOpenSsl -OpenSslPath $openssl `
			-Arguments @('pkeyutl', '-sign', '-rawin', '-inkey', $privatePath,
				'-in', (Get-Item -LiteralPath $InputPath).FullName, '-out', $tempSignaturePath) `
			-Context 'Creating the detached Ed25519 signature'
		if ((Get-Item -LiteralPath $tempSignaturePath -ErrorAction Stop).Length -ne 64) {
			throw "OpenSSL did not produce a 64-byte raw Ed25519 signature."
		}
		$signatureParent = Split-Path -Parent $SignaturePath
		if (-not [string]::IsNullOrWhiteSpace($signatureParent)) {
			New-Item -ItemType Directory -Force -Path $signatureParent | Out-Null
		}
		Copy-Item -LiteralPath $tempSignaturePath -Destination $SignaturePath -Force
		if (-not (Test-Ed25519DetachedSignature -InputPath $InputPath -SignaturePath $SignaturePath `
			-PublicKeyHex $expectedKey -OpenSslPath $openssl)) {
			Remove-Item -LiteralPath $SignaturePath -Force -ErrorAction SilentlyContinue
			throw "The generated detached Ed25519 signature did not verify."
		}
	} finally {
		if ($privateBytes) { [Array]::Clear($privateBytes, 0, $privateBytes.Length) }
		Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
	}
}

Export-ModuleMember -Function @(
	"Get-ReleaseFileSha256",
	"Read-ReleaseJson",
	"Assert-StrictInputEnhancementRolloutJson",
	"Write-ReleaseJson",
	"Assert-ObjectProperty",
	"Assert-FullGitSha",
	"Get-InputEnhancementBuildId",
	"Assert-TestGateResults",
	"Assert-SigningResults",
	"Assert-SafeRelativeReleasePath",
	"Initialize-InputEnhancementRehearsalOutputRoot",
	"Assert-InputEnhancementRehearsalDraftFileSafety",
	"Get-ValidatedInputEnhancementRehearsalDraftFiles",
	"Assert-Ed25519PublicKeyHex",
	"Assert-CanonicalInputEnhancementPolicy",
	"Assert-InputEnhancementPromotionPolicy",
	"Resolve-InputEnhancementOpenSsl",
	"Get-Ed25519PublicKeyHexFromPrivateKey",
	"Test-Ed25519DetachedSignature",
	"Protect-FileWithEd25519"
)
