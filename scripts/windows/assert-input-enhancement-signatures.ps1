[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string]$Root,

	[Parameter(Mandatory = $true)]
	[string]$ExpectedSignerSubject,

	[Parameter(Mandatory = $true)]
	[string]$OutputPath
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

Import-Module (Join-Path $PSScriptRoot "InputEnhancementReleaseTools.psm1") -Force

if (-not (Test-Path -LiteralPath $Root -PathType Container)) {
	throw "Signed artifact root does not exist: '$Root'."
}
if ([string]::IsNullOrWhiteSpace($ExpectedSignerSubject)) {
	throw "ExpectedSignerSubject is required; unsigned or unknown publishers cannot be qualified."
}

$rootPath = (Resolve-Path -LiteralPath $Root).Path.TrimEnd('\', '/')
$files = @(Get-ChildItem -LiteralPath $rootPath -Recurse -File | Where-Object {
	$_.Extension -in @(".exe", ".dll", ".msi")
} | Sort-Object -Property FullName)
if ($files.Count -eq 0) {
	throw "No PE or MSI files were found under '$rootPath'."
}

$results = New-Object System.Collections.Generic.List[object]
$failures = New-Object System.Collections.Generic.List[string]
foreach ($file in $files) {
	$signature = Get-AuthenticodeSignature -LiteralPath $file.FullName
	$signerSubject = if ($signature.SignerCertificate) { [string]$signature.SignerCertificate.Subject } else { "" }
	$timestamped = $null -ne $signature.TimeStamperCertificate
	$relativePath = $file.FullName.Substring($rootPath.Length).TrimStart('\', '/') -replace '\\', '/'
	$results.Add([ordered]@{
		path                 = $relativePath
		sha256               = Get-ReleaseFileSha256 -Path $file.FullName
		status               = [string]$signature.Status
		signerSubject        = $signerSubject
		signerThumbprint     = if ($signature.SignerCertificate) { [string]$signature.SignerCertificate.Thumbprint } else { "" }
		timestamped          = $timestamped
		timestampThumbprint  = if ($signature.TimeStamperCertificate) { [string]$signature.TimeStamperCertificate.Thumbprint } else { "" }
	})

	if ($signature.Status -ne [System.Management.Automation.SignatureStatus]::Valid) {
		$failures.Add("$relativePath has Authenticode status '$($signature.Status)'")
	}
	if ($signerSubject -cne $ExpectedSignerSubject) {
		$failures.Add("$relativePath signer '$signerSubject' does not match '$ExpectedSignerSubject'")
	}
	if (-not $timestamped) {
		$failures.Add("$relativePath has no trusted timestamp certificate")
	}
}

$document = [ordered]@{
	schemaVersion         = 1
	verified              = $failures.Count -eq 0
	expectedSignerSubject = $ExpectedSignerSubject
	fileCount             = $results.Count
	files                 = $results.ToArray()
}
Write-ReleaseJson -Value $document -Path $OutputPath

if ($failures.Count -gt 0) {
	throw "Artifact signature verification failed: $($failures -join '; ')."
}

Assert-SigningResults -SigningResults $document -ExpectedSignerSubject $ExpectedSignerSubject
Write-Host "Verified $($results.Count) timestamped Authenticode signatures under '$rootPath'."
