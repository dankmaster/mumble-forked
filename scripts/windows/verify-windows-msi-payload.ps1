[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)][string]$CandidateClientMsi,
	[Parameter(Mandatory = $true)][string]$CandidateExecutable,
	[Parameter(Mandatory = $true)][string]$OutputPath,
	[string]$WorkingRoot = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Import-Module "$PSScriptRoot\WindowsMsiPayload.Common.psm1" -Force

$outputFullPath = [IO.Path]::GetFullPath($OutputPath)
$outputDirectory = Split-Path -Parent $outputFullPath
if (-not [string]::IsNullOrWhiteSpace($outputDirectory)) {
	New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
}
$evidence = Get-WindowsMsiPayloadVerification -CandidateClientMsi $CandidateClientMsi `
	-CandidateExecutable $CandidateExecutable -WorkingRoot $WorkingRoot
$temporaryPath = "$outputFullPath.$([Guid]::NewGuid().ToString('N')).tmp"
try {
	[IO.File]::WriteAllText($temporaryPath, ($evidence | ConvertTo-Json -Depth 20) + [Environment]::NewLine,
		[Text.UTF8Encoding]::new($false))
	Move-Item -LiteralPath $temporaryPath -Destination $outputFullPath -Force
} finally {
	Remove-Item -LiteralPath $temporaryPath -Force -ErrorAction SilentlyContinue
}
$evidence | ConvertTo-Json -Depth 20
if (-not [bool]$evidence.eligible) {
	throw "Candidate MSI payload verification failed closed. Evidence: $outputFullPath"
}
