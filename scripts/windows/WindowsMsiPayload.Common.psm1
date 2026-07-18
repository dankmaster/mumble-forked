$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Get-WindowsMsiPayloadFileSha256 {
	param([Parameter(Mandatory = $true)][string]$Path)
	return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-WindowsClientMsiPayloadExecutable {
	[CmdletBinding()]
	param([Parameter(Mandatory = $true)][string]$ExtractedPayloadRoot)

	$root = (Resolve-Path -LiteralPath $ExtractedPayloadRoot -ErrorAction Stop).Path
	if (-not (Test-Path -LiteralPath $root -PathType Container)) {
		throw "Extracted MSI payload root is not a directory: $root"
	}
	$matches = @(Get-ChildItem -LiteralPath $root -Recurse -File -ErrorAction Stop |
		Where-Object { $_.Name -ieq 'mumble.exe' })
	if ($matches.Count -ne 1) {
		throw "Administrative MSI extraction must contain exactly one mumble.exe; found $($matches.Count)."
	}
	return $matches[0].FullName
}

function Get-WindowsMsiPayloadVerification {
	[CmdletBinding()]
	param(
		[Parameter(Mandatory = $true)][string]$CandidateClientMsi,
		[Parameter(Mandatory = $true)][string]$CandidateExecutable,
		[string]$WorkingRoot = ''
	)

	if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
		throw 'Windows MSI payload verification can run only on Windows.'
	}
	$msiPath = (Resolve-Path -LiteralPath $CandidateClientMsi -ErrorAction Stop).Path
	$executablePath = (Resolve-Path -LiteralPath $CandidateExecutable -ErrorAction Stop).Path
	if (-not (Test-Path -LiteralPath $msiPath -PathType Leaf)) { throw "CandidateClientMsi is not a file: $msiPath" }
	if (-not (Test-Path -LiteralPath $executablePath -PathType Leaf)) { throw "CandidateExecutable is not a file: $executablePath" }

	$parentRoot = if ([string]::IsNullOrWhiteSpace($WorkingRoot)) {
		Join-Path ([IO.Path]::GetTempPath()) 'mumble-msi-payload-verification'
	} else {
		[IO.Path]::GetFullPath($WorkingRoot)
	}
	New-Item -ItemType Directory -Force -Path $parentRoot | Out-Null
	$parentRoot = (Resolve-Path -LiteralPath $parentRoot).Path
	$operationRoot = Join-Path $parentRoot ([Guid]::NewGuid().ToString('N'))
	$extractRoot = Join-Path $operationRoot 'payload'
	$logPath = Join-Path $operationRoot 'administrative-install.log'
	New-Item -ItemType Directory -Force -Path $extractRoot | Out-Null

	$msiSha256 = Get-WindowsMsiPayloadFileSha256 -Path $msiPath
	$expectedExecutableSha256 = Get-WindowsMsiPayloadFileSha256 -Path $executablePath
	try {
		$msiexec = Join-Path $env:SystemRoot 'System32\msiexec.exe'
		if (-not (Test-Path -LiteralPath $msiexec -PathType Leaf)) {
			throw "Windows Installer executable was not found: $msiexec"
		}
		$arguments = @(
			'/a', ('"{0}"' -f $msiPath), '/qn', '/norestart',
			('TARGETDIR="{0}"' -f $extractRoot), '/l*v', ('"{0}"' -f $logPath)
		)
		$process = Start-Process -FilePath $msiexec -ArgumentList $arguments -Wait -PassThru
		if ($process.ExitCode -notin @(0, 1641, 3010)) {
			throw "Administrative MSI extraction failed with exit code $($process.ExitCode)."
		}

		$embeddedExecutablePath = Get-WindowsClientMsiPayloadExecutable -ExtractedPayloadRoot $extractRoot
		$embeddedExecutableSha256 = Get-WindowsMsiPayloadFileSha256 -Path $embeddedExecutablePath
		$relativePath = $embeddedExecutablePath.Substring($extractRoot.TrimEnd('\').Length + 1).Replace('\', '/')
		$hashMatch = $embeddedExecutableSha256 -ceq $expectedExecutableSha256
		$status = if ($hashMatch) { 'passed' } else { 'failed' }
		$errorText = if ($hashMatch) {
			$null
		} else {
			'The mumble.exe embedded in the candidate MSI does not match the frozen candidate executable SHA-256.'
		}
		return [pscustomobject][ordered]@{
			schema_version = 1
			artifact_kind = 'windows_msi_payload_evidence'
			gate_id = 'windows-qml-msi-payload'
			status = $status
			eligible = $hashMatch
			generated_at_utc = [DateTime]::UtcNow.ToString('o')
			candidate_client_msi = [ordered]@{
				path = $msiPath
				sha256 = $msiSha256
			}
			candidate_executable = [ordered]@{
				path = $executablePath
				sha256 = $expectedExecutableSha256
			}
			embedded_payload = [ordered]@{
				path = $relativePath
				sha256 = $embeddedExecutableSha256
				size = (Get-Item -LiteralPath $embeddedExecutablePath).Length
			}
			extraction = [ordered]@{
				mode = 'windows-installer-administrative-image'
				exit_code = [int]$process.ExitCode
				log_sha256 = if (Test-Path -LiteralPath $logPath -PathType Leaf) {
					Get-WindowsMsiPayloadFileSha256 -Path $logPath
				} else { $null }
			}
			verification = [ordered]@{
				exact_executable_sha256_match = $hashMatch
			}
			error = $errorText
		}
	} finally {
		$resolvedOperationRoot = [IO.Path]::GetFullPath($operationRoot)
		$resolvedParentRoot = [IO.Path]::GetFullPath($parentRoot).TrimEnd('\')
		if (-not $resolvedOperationRoot.StartsWith($resolvedParentRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
			throw "Refusing to remove MSI verification work outside its parent root: $resolvedOperationRoot"
		}
		Remove-Item -LiteralPath $resolvedOperationRoot -Recurse -Force -ErrorAction SilentlyContinue
	}
}

function Assert-WindowsMsiPayloadEvidence {
	[CmdletBinding()]
	param(
		[Parameter(Mandatory = $true)]$Evidence,
		[Parameter(Mandatory = $true)][string]$CandidateClientMsi,
		[Parameter(Mandatory = $true)][string]$CandidateExecutable
	)

	$msiPath = (Resolve-Path -LiteralPath $CandidateClientMsi -ErrorAction Stop).Path
	$executablePath = (Resolve-Path -LiteralPath $CandidateExecutable -ErrorAction Stop).Path
	$actualMsiSha256 = Get-WindowsMsiPayloadFileSha256 -Path $msiPath
	$actualExecutableSha256 = Get-WindowsMsiPayloadFileSha256 -Path $executablePath
	if ([int]$Evidence.schema_version -ne 1 -or
		[string]$Evidence.artifact_kind -cne 'windows_msi_payload_evidence' -or
		[string]$Evidence.gate_id -cne 'windows-qml-msi-payload' -or
		[string]$Evidence.status -cne 'passed' -or $Evidence.eligible -isnot [bool] -or -not [bool]$Evidence.eligible) {
		throw 'MSI payload evidence is not an eligible schema-v1 windows-qml-msi-payload result.'
	}
	if ([string]$Evidence.candidate_client_msi.sha256 -cne $actualMsiSha256) {
		throw 'MSI payload evidence does not match the current candidate MSI SHA-256.'
	}
	if ([string]$Evidence.candidate_executable.sha256 -cne $actualExecutableSha256 -or
		[string]$Evidence.embedded_payload.sha256 -cne $actualExecutableSha256 -or
		$Evidence.verification.exact_executable_sha256_match -isnot [bool] -or
		-not [bool]$Evidence.verification.exact_executable_sha256_match) {
		throw 'MSI payload evidence does not prove that embedded mumble.exe matches the frozen candidate executable SHA-256.'
	}
	if ([IO.Path]::GetFileName([string]$Evidence.embedded_payload.path) -ine 'mumble.exe') {
		throw 'MSI payload evidence does not identify an embedded mumble.exe.'
	}
	return $Evidence
}

Export-ModuleMember -Function Get-WindowsClientMsiPayloadExecutable, Get-WindowsMsiPayloadVerification, Assert-WindowsMsiPayloadEvidence
