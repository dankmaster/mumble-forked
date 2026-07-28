[CmdletBinding()]
param(
	[string]$RepoRoot = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$scriptDir = Split-Path -Parent $PSCommandPath
. (Join-Path $scriptDir "..\mumble-git-state.ps1")

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
	$RepoRoot = (Resolve-Path (Join-Path $scriptDir "..\..\..")).Path
} else {
	$RepoRoot = (Resolve-Path -LiteralPath $RepoRoot).Path
}

$script:Passed = 0

function Assert-Equal {
	param(
		$Actual,
		$Expected,
		[string]$Message
	)

	if ($Actual -cne $Expected) {
		throw "$Message Expected '$Expected', got '$Actual'."
	}
	$script:Passed++
}

function Invoke-TestGitSingleLine {
	param(
		[string[]]$Arguments
	)

	$outputLines = @(& git -C $RepoRoot @Arguments 2>$null)
	$exitCode = $LASTEXITCODE
	if ($exitCode -ne 0) {
		throw "git $($Arguments -join ' ') failed with exit code $exitCode"
	}
	if ($outputLines.Count -eq 0) {
		throw "git $($Arguments -join ' ') returned no output"
	}

	return ([string]$outputLines[0]).Trim()
}

$expectedHead = Invoke-TestGitSingleLine -Arguments @("rev-parse", "HEAD")
$expectedBranch = Invoke-TestGitSingleLine -Arguments @("symbolic-ref", "--quiet", "--short", "HEAD")
$rawCommonDirectory = Invoke-TestGitSingleLine -Arguments @("rev-parse", "--git-common-dir")
$expectedCommonDirectory = if ([System.IO.Path]::IsPathRooted($rawCommonDirectory)) {
	[System.IO.Path]::GetFullPath($rawCommonDirectory)
} else {
	[System.IO.Path]::GetFullPath((Join-Path $RepoRoot $rawCommonDirectory))
}

$state = Get-MumbleGitState -RepoRoot $RepoRoot
Assert-Equal -Actual $state.Head -Expected $expectedHead `
	-Message "Git HEAD resolution is not native-exit-safe."
Assert-Equal -Actual $state.Branch -Expected $expectedBranch `
	-Message "Git branch resolution is not native-exit-safe."
Assert-Equal -Actual $state.GitCommonDir -Expected $expectedCommonDirectory `
	-Message "Git common-directory resolution is not native-exit-safe."

Write-Host "mumble-git-state regression checks passed ($script:Passed assertions) in $($PSVersionTable.PSEdition) $($PSVersionTable.PSVersion)."
