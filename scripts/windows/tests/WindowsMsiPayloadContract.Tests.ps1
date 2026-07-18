$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$modulePath = Join-Path $repoRoot 'scripts\windows\WindowsMsiPayload.Common.psm1'
$verifierPath = Join-Path $repoRoot 'scripts\windows\verify-windows-msi-payload.ps1'
$artifactPath = Join-Path $repoRoot 'scripts\windows\assert-windows-build-artifacts.ps1'
$sharedWorkflowPath = Join-Path $repoRoot '.github\workflows\windows-shared-client.yml'
$forkedWorkflowPath = Join-Path $repoRoot '.github\workflows\mumble-forked.yml'

Describe 'Windows candidate MSI payload hash preflight' {
	BeforeAll {
		Import-Module $modulePath -Force
		$script:verifierSource = Get-Content -LiteralPath $verifierPath -Raw
		$script:artifactSource = Get-Content -LiteralPath $artifactPath -Raw
	}

	It 'parses the module, verifier, and artifact integration without errors' {
		foreach ($path in @($modulePath, $verifierPath, $artifactPath)) {
			$tokens = $null
			$parseErrors = $null
			[void][Management.Automation.Language.Parser]::ParseFile($path, [ref]$tokens, [ref]$parseErrors)
			$parseErrors.Count | Should Be 0
		}
	}

	It 'requires exactly one recursively extracted mumble executable' {
		$root = Join-Path ([IO.Path]::GetTempPath()) ('mumble-msi-payload-' + [Guid]::NewGuid().ToString('N'))
		$nested = Join-Path $root 'Program Files\Mumble\client'
		New-Item -ItemType Directory -Path $nested -Force | Out-Null
		try {
			$expected = Join-Path $nested 'mumble.exe'
			[IO.File]::WriteAllBytes($expected, [byte[]](1, 2, 3))
			(Get-WindowsClientMsiPayloadExecutable -ExtractedPayloadRoot $root) | Should Be $expected
			$duplicate = Join-Path $root 'duplicate\mumble.exe'
			New-Item -ItemType Directory -Path (Split-Path -Parent $duplicate) -Force | Out-Null
			[IO.File]::WriteAllBytes($duplicate, [byte[]](4))
			{ Get-WindowsClientMsiPayloadExecutable -ExtractedPayloadRoot $root } |
				Should Throw 'Administrative MSI extraction must contain exactly one mumble.exe; found 2.'
		} finally {
			Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
		}
	}

	It 'accepts only evidence that binds both MSI and embedded payload to the candidate hash' {
		$root = Join-Path ([IO.Path]::GetTempPath()) ('mumble-msi-evidence-' + [Guid]::NewGuid().ToString('N'))
		New-Item -ItemType Directory -Path $root | Out-Null
		try {
			$msi = Join-Path $root 'candidate.msi'
			$exe = Join-Path $root 'mumble.exe'
			[IO.File]::WriteAllBytes($msi, [byte[]](9, 8, 7))
			[IO.File]::WriteAllBytes($exe, [byte[]](1, 2, 3, 4))
			$msiHash = (Get-FileHash -LiteralPath $msi -Algorithm SHA256).Hash.ToLowerInvariant()
			$exeHash = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
			$evidence = [pscustomobject]@{
				schema_version = 1
				artifact_kind = 'windows_msi_payload_evidence'
				gate_id = 'windows-qml-msi-payload'
				status = 'passed'
				eligible = $true
				candidate_client_msi = [pscustomobject]@{ sha256 = $msiHash }
				candidate_executable = [pscustomobject]@{ sha256 = $exeHash }
				embedded_payload = [pscustomobject]@{ path = 'Program Files/Mumble/client/mumble.exe'; sha256 = $exeHash }
				verification = [pscustomobject]@{ exact_executable_sha256_match = $true }
			}
			(Assert-WindowsMsiPayloadEvidence -Evidence $evidence -CandidateClientMsi $msi `
				-CandidateExecutable $exe).status | Should Be 'passed'
			$evidence.embedded_payload.sha256 = '0' * 64
			{ Assert-WindowsMsiPayloadEvidence -Evidence $evidence -CandidateClientMsi $msi `
				-CandidateExecutable $exe } |
				Should Throw 'MSI payload evidence does not prove that embedded mumble.exe matches the frozen candidate executable SHA-256.'
		} finally {
			Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
		}
	}

	It 'uses Windows Installer administrative extraction and writes evidence atomically' {
		$script:verifierSource | Should Match 'Get-WindowsMsiPayloadVerification'
		$script:verifierSource | Should Match 'Move-Item -LiteralPath \$temporaryPath'
		$moduleSource = Get-Content -LiteralPath $modulePath -Raw
		$moduleSource | Should Match "'/a'.*'/qn'.*'/norestart'"
		$moduleSource | Should Match 'TARGETDIR='
		$moduleSource | Should Match 'exact_executable_sha256_match'
	}

	It 'keeps legacy artifact calls valid while requiring paired preflight arguments when requested' {
		$script:artifactSource | Should Match 'CandidateExecutablePath and MsiPayloadEvidencePath must be supplied together'
		$script:artifactSource | Should Match 'Candidate MSI payload verification requires RequireClientInstaller'
		$script:artifactSource | Should Match 'verify-windows-msi-payload\.ps1'

		$root = Join-Path ([IO.Path]::GetTempPath()) ('mumble-artifact-legacy-' + [Guid]::NewGuid().ToString('N'))
		New-Item -ItemType Directory -Path $root | Out-Null
		try {
			[IO.File]::WriteAllBytes((Join-Path $root 'mumble.exe'), [byte[]](1))
			[IO.File]::WriteAllBytes((Join-Path $root 'mumble-updater.exe'), [byte[]](2))
			$hostExecutable = (Get-Process -Id $PID).Path
			& $hostExecutable -NoProfile -ExecutionPolicy Bypass -File $artifactPath `
				-BuildRoot $root -RequireClient 2>&1 | Out-Null
			$LASTEXITCODE | Should Be 0
		} finally {
			Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
		}
	}

	It 'enables the payload preflight in both tracked Windows client packaging workflows' {
		foreach ($path in @($sharedWorkflowPath, $forkedWorkflowPath)) {
			$source = Get-Content -LiteralPath $path -Raw
			$source | Should Match 'CandidateExecutablePath .*shared-webengine-stage\\mumble\.exe'
			$source | Should Match 'MsiPayloadEvidencePath .*windows-msi-payload-evidence\.json'
		}
	}
}
