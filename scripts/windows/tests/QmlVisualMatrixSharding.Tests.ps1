Import-Module "$PSScriptRoot\..\QmlVisualGate.Common.psm1" -Force
Import-Module "$PSScriptRoot\..\QmlVisualMatrix.Common.psm1" -Force

Describe "Qt Quick visual candidate sharding" {
	It "partitions cases deterministically by original matrix order" {
		$cases = @(0..9 | ForEach-Object { [pscustomobject]@{ id = "case-$_" } })
		$first = @(Get-QmlVisualMatrixShardCases -Cases $cases -ShardIndex 0 -ShardCount 3)
		$second = @(Get-QmlVisualMatrixShardCases -Cases $cases -ShardIndex 1 -ShardCount 3)
		$third = @(Get-QmlVisualMatrixShardCases -Cases $cases -ShardIndex 2 -ShardCount 3)

		@($first.id) | Should Be @("case-0", "case-3", "case-6", "case-9")
		@($second.id) | Should Be @("case-1", "case-4", "case-7")
		@($third.id) | Should Be @("case-2", "case-5", "case-8")
		@($first + $second + $third | ForEach-Object id | Sort-Object) |
			Should Be @($cases.id | Sort-Object)
	}

	It "rejects invalid or empty shard selections" {
		$cases = @([pscustomobject]@{ id = "one" }, [pscustomobject]@{ id = "two" })
		foreach ($selection in @(
			@{ cases = $cases; index = 2; count = 2 },
			@{ cases = $cases; index = 0; count = 3 },
			@{ cases = @(); index = 0; count = 1 }
		)) {
			$threw = $false
			try {
				Get-QmlVisualMatrixShardCases -Cases $selection.cases `
					-ShardIndex $selection.index -ShardCount $selection.count | Out-Null
			} catch { $threw = $true }
			$threw | Should Be $true
		}
	}

	It "exposes shard controls only in candidate-only mode" {
		$command = Get-Command "$PSScriptRoot\..\invoke-qml-visual-matrix.ps1"
		$gate = $command.ParameterSets | Where-Object Name -eq "Gate"
		$candidate = $command.ParameterSets | Where-Object Name -eq "Candidate"

		@($gate.Parameters | Where-Object Name -in @("ShardIndex", "ShardCount", "AutomationPortBase")).Count |
			Should Be 0
		@($candidate.Parameters | Where-Object Name -eq "ShardIndex").Count | Should Be 1
		@($candidate.Parameters | Where-Object Name -eq "ShardCount").Count | Should Be 1
		@($candidate.Parameters | Where-Object Name -eq "AutomationPortBase").Count | Should Be 1
	}

	It "allocates a unique deterministic port for every shard and source DPR" {
		$ports = foreach ($shardIndex in 0..3) {
			foreach ($dprIndex in 0..1) {
				Get-QmlVisualAutomationPort -BasePort 45000 -ShardIndex $shardIndex `
					-SourceDprCount 2 -SourceDprIndex $dprIndex
			}
		}
		@($ports) | Should Be @(45000, 45001, 45002, 45003, 45004, 45005, 45006, 45007)
		@($ports | Sort-Object -Unique).Count | Should Be 8
	}

	It "rejects an invalid DPR index and automation port overflow" {
		foreach ($selection in @(
			@{ base = 45000; shard = 0; count = 2; dpr = 2 },
			@{ base = 65535; shard = 1; count = 2; dpr = 0 }
		)) {
			$threw = $false
			try {
				Get-QmlVisualAutomationPort -BasePort $selection.base -ShardIndex $selection.shard `
					-SourceDprCount $selection.count -SourceDprIndex $selection.dpr | Out-Null
			} catch { $threw = $true }
			$threw | Should Be $true
		}
	}

	It "marks shard evidence as ineligible for baseline import" {
		$root = Join-Path ([IO.Path]::GetTempPath()) ("qml-shard-contract-" + [Guid]::NewGuid().ToString('N'))
		New-Item -ItemType Directory -Path $root | Out-Null
		try {
			$matrixPath = Join-Path $root "matrix.json"
			'{"schema_version":1,"cases":[{"id":"case-0"}]}' |
				Set-Content -LiteralPath $matrixPath -Encoding utf8NoBOM
			$manifest = [pscustomobject]@{
				schema_version = 1
				mode = "candidate-only"
				process_isolation = "per-dpr-candidate-shard"
				matrix_sha256 = Get-QmlVisualFileSha256 $matrixPath
				cases = @([pscustomobject]@{
					id = "case-0"
					image_sha256 = "a" * 64
					accessibility_sha256 = "b" * 64
					image_width = 1
					image_height = 1
				})
			}

			$threw = $false
			try {
				Assert-QmlVisualManifestMatchesMatrix -Manifest $manifest -MatrixPath $matrixPath `
					-RequireCombinedCandidate | Out-Null
			} catch { $threw = $true }
			$threw | Should Be $true
		} finally {
			Remove-Item -LiteralPath $root -Recurse -Force
		}
	}

	It "keeps the unsharded full-run provenance contract unchanged" {
		$runner = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-matrix.ps1"
		$runner.Contains("process_isolation = if (`$isShard) { 'per-dpr-candidate-shard' } else { 'per-dpr' }") |
			Should Be $true
		$runner.Contains('`$isShard = `$CandidateOnly -and `$ShardCount -gt 1'.Replace('`$', '$')) |
			Should Be $true
		$runner.Contains('matrix_sha256 = Get-QmlVisualFileSha256 $matrixFile') | Should Be $true
		$runner.Contains('if ($isShard -and $AutomationPortBase -eq 0)') | Should Be $true
		$runner.Contains('Local\MumbleQmlVisualAutomationPort-$Port') | Should Be $true
		$runner.Contains("`$manifest['automation_ports'] = @(`$automationPorts)") | Should Be $true
	}
}
