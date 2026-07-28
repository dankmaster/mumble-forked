if ($null -eq (Get-Variable -Name LASTEXITCODE -Scope Global -ErrorAction SilentlyContinue)) {
	$global:LASTEXITCODE = 0
}

Set-StrictMode -Version Latest

function Invoke-MumbleGitSingleLine {
	param(
		[Parameter(Mandatory = $true)]
		[string]$RepoRoot,

		[Parameter(Mandatory = $true)]
		[string[]]$Arguments
	)

	# Windows PowerShell 5.1 can replace a successful native exit code with -1
	# when native output is piped through Select-Object -First 1. Capture both
	# the complete output and the native exit code before inspecting the result.
	$outputLines = @(& git -C $RepoRoot @Arguments 2>$null)
	$exitCode = $LASTEXITCODE
	$value = ""
	if ($outputLines.Count -gt 0 -and $null -ne $outputLines[0]) {
		$value = ([string]$outputLines[0]).Trim()
	}

	return [pscustomobject]@{
		ExitCode = [int]$exitCode
		Value    = $value
	}
}

function Get-MumbleGitCommonDirectory {
	param(
		[Parameter(Mandatory = $true)]
		[string]$RepoRoot
	)

	$result = Invoke-MumbleGitSingleLine -RepoRoot $RepoRoot -Arguments @("rev-parse", "--git-common-dir")
	$rawPath = $result.Value
	if ($result.ExitCode -ne 0 -or [string]::IsNullOrWhiteSpace($rawPath)) {
		return [System.IO.Path]::GetFullPath($RepoRoot)
	}

	if ([System.IO.Path]::IsPathRooted($rawPath)) {
		return [System.IO.Path]::GetFullPath($rawPath)
	}

	return [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $rawPath))
}

function Get-MumbleGitState {
	param(
		[Parameter(Mandatory = $true)]
		[string]$RepoRoot
	)

	$headResult = Invoke-MumbleGitSingleLine -RepoRoot $RepoRoot -Arguments @("rev-parse", "HEAD")
	$head = $headResult.Value
	if ($headResult.ExitCode -ne 0 -or [string]::IsNullOrWhiteSpace($head)) {
		throw "Could not resolve the Git HEAD for '$RepoRoot'."
	}

	$branchResult = Invoke-MumbleGitSingleLine -RepoRoot $RepoRoot -Arguments @("symbolic-ref", "--quiet", "--short", "HEAD")
	$branch = $branchResult.Value
	if ($branchResult.ExitCode -ne 0 -or [string]::IsNullOrWhiteSpace($branch)) {
		$branch = "(detached)"
	}

	$statusLines = @(& git -C $RepoRoot status --porcelain=v1 --untracked-files=normal)
	$statusExitCode = $LASTEXITCODE
	if ($statusExitCode -ne 0) {
		throw "Could not inspect the Git working tree for '$RepoRoot'."
	}

	return [pscustomobject]@{
		RepoRoot     = [System.IO.Path]::GetFullPath($RepoRoot)
		GitCommonDir = Get-MumbleGitCommonDirectory -RepoRoot $RepoRoot
		Branch       = [string]$branch
		Head         = [string]$head
		IsDirty      = $statusLines.Count -gt 0
		StatusLines  = @($statusLines)
	}
}
