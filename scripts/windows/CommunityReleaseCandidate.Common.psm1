Set-StrictMode -Version Latest

function Get-CommunityReleaseFileSha256 {
	[CmdletBinding()]
	param([Parameter(Mandatory = $true)][string]$Path)

	$resolved = (Resolve-Path -LiteralPath $Path).Path
	return (Get-FileHash -LiteralPath $resolved -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-CommunityReleaseBytesSha256 {
	[CmdletBinding()]
	param([Parameter(Mandatory = $true)][byte[]]$Bytes)

	$sha256 = [Security.Cryptography.SHA256]::Create()
	try {
		return ([BitConverter]::ToString($sha256.ComputeHash($Bytes))).Replace('-', '').ToLowerInvariant()
	} finally {
		$sha256.Dispose()
	}
}

function Invoke-CommunityReleaseGit {
	param(
		[Parameter(Mandatory = $true)][string]$RepositoryRoot,
		[Parameter(Mandatory = $true)][string[]]$Arguments
	)

	$output = @(& git -C $RepositoryRoot @Arguments 2>&1)
	if ($LASTEXITCODE -ne 0) {
		throw "git $($Arguments -join ' ') failed in '$RepositoryRoot': $($output -join [Environment]::NewLine)"
	}
	return $output
}

function Get-CommunityReleaseSourceState {
	[CmdletBinding()]
	param([Parameter(Mandatory = $true)][string]$RepositoryRoot)

	$root = (Resolve-Path -LiteralPath $RepositoryRoot).Path
	$gitSha = [string](Invoke-CommunityReleaseGit -RepositoryRoot $root -Arguments @('rev-parse', 'HEAD') | Select-Object -First 1)
	if ($gitSha -notmatch '^[0-9a-fA-F]{40}$') {
		throw "Repository HEAD is not a full Git commit SHA: '$gitSha'."
	}
	$gitSha = $gitSha.ToLowerInvariant()

	$diffPath = Join-Path ([IO.Path]::GetTempPath()) ("mumble-community-release-diff-{0}.bin" -f [Guid]::NewGuid().ToString('N'))
	try {
		& git -C $root diff --binary --full-index HEAD --output=$diffPath --
		if ($LASTEXITCODE -ne 0) {
			throw "Unable to fingerprint tracked worktree changes in '$root'."
		}
		$trackedDiffSha256 = Get-CommunityReleaseFileSha256 -Path $diffPath
		$trackedDiffLength = (Get-Item -LiteralPath $diffPath).Length
	} finally {
		Remove-Item -LiteralPath $diffPath -Force -ErrorAction SilentlyContinue
	}

	$untrackedPaths = @(Invoke-CommunityReleaseGit -RepositoryRoot $root -Arguments @(
		'ls-files', '--others', '--exclude-standard'
	) | ForEach-Object { [string]$_ } | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Sort-Object)
	$untracked = @($untrackedPaths | ForEach-Object {
		$relativePath = $_.Replace('\', '/')
		$absolutePath = Join-Path $root $_
		if (-not (Test-Path -LiteralPath $absolutePath -PathType Leaf)) {
			throw "Untracked candidate source is not a regular file: '$relativePath'."
		}
		[ordered]@{
			path = $relativePath
			sha256 = Get-CommunityReleaseFileSha256 -Path $absolutePath
		}
	})

	$fingerprintPayload = [ordered]@{
		git_sha = $gitSha
		tracked_diff_sha256 = $trackedDiffSha256
		tracked_diff_length_bytes = [int64]$trackedDiffLength
		untracked_files = $untracked
	}
	$fingerprintJson = $fingerprintPayload | ConvertTo-Json -Depth 6 -Compress
	$fingerprintBytes = [Text.UTF8Encoding]::new($false).GetBytes($fingerprintJson)
	$worktreeSha256 = Get-CommunityReleaseBytesSha256 -Bytes $fingerprintBytes

	$statusLines = @(Invoke-CommunityReleaseGit -RepositoryRoot $root -Arguments @(
		'status', '--porcelain=v1', '--untracked-files=all'
	))
	return [ordered]@{
		git_sha = $gitSha
		worktree_sha256 = $worktreeSha256
		clean = $statusLines.Count -eq 0
		changed_path_count = $statusLines.Count
		tracked_diff_sha256 = $trackedDiffSha256
		tracked_diff_length_bytes = [int64]$trackedDiffLength
		untracked_file_count = $untracked.Count
	}
}

function New-CommunityReleaseCandidateId {
	[CmdletBinding()]
	param(
		[Parameter(Mandatory = $true)][string]$GitSha,
		[Parameter(Mandatory = $true)][string]$WorktreeSha256,
		[Parameter(Mandatory = $true)][string]$ExecutableSha256
	)

	foreach ($value in @($GitSha, $WorktreeSha256, $ExecutableSha256)) {
		if ($value -notmatch '^[0-9a-fA-F]{40,64}$') {
			throw "Candidate identity inputs must be hexadecimal Git/SHA-256 values. Invalid value: '$value'."
		}
	}
	return ('{0}-{1}-{2}' -f $GitSha.Substring(0, 12), $WorktreeSha256.Substring(0, 12), $ExecutableSha256.Substring(0, 12)).ToLowerInvariant()
}

function New-CommunityReleaseCandidateManifest {
	[CmdletBinding()]
	param(
		[Parameter(Mandatory = $true)][string]$RepositoryRoot,
		[Parameter(Mandatory = $true)][string]$Executable,
		[string]$CandidateId = '',
		[switch]$AllowDirtySource
	)

	$root = (Resolve-Path -LiteralPath $RepositoryRoot).Path
	$executablePath = (Resolve-Path -LiteralPath $Executable).Path
	$source = Get-CommunityReleaseSourceState -RepositoryRoot $root
	if (-not $source.clean -and -not $AllowDirtySource) {
		throw "A community release candidate requires a clean worktree. Found $($source.changed_path_count) changed path(s)."
	}
	$executableSha256 = Get-CommunityReleaseFileSha256 -Path $executablePath
	if ([string]::IsNullOrWhiteSpace($CandidateId)) {
		$CandidateId = New-CommunityReleaseCandidateId -GitSha $source.git_sha `
			-WorktreeSha256 $source.worktree_sha256 -ExecutableSha256 $executableSha256
	}
	if ($CandidateId -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]{5,127}$') {
		throw "CandidateId must be 6-128 portable identifier characters."
	}

	return [ordered]@{
		schema_version = 1
		candidate_id = $CandidateId
		candidate_kind = $source.clean ? 'release' : 'development'
		created_at_utc = [DateTime]::UtcNow.ToString('o')
		product_scope = 'windows-client+linux-murmur'
		frontend = 'qml'
		source = $source
		windows = [ordered]@{
			executable_path = $executablePath
			executable_sha256 = $executableSha256
		}
	}
}

Export-ModuleMember -Function @(
	'Get-CommunityReleaseFileSha256',
	'Get-CommunityReleaseSourceState',
	'New-CommunityReleaseCandidateId',
	'New-CommunityReleaseCandidateManifest'
)
