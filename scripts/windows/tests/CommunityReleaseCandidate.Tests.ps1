$script:repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$script:modulePath = Join-Path $script:repoRoot 'scripts\windows\CommunityReleaseCandidate.Common.psm1'

Describe 'Community release candidate provenance' {
	BeforeAll {
		Import-Module $script:modulePath -Force
	}

	BeforeEach {
		$script:testRepo = Join-Path $TestDrive ([Guid]::NewGuid().ToString('N'))
		New-Item -ItemType Directory -Path $script:testRepo | Out-Null
		& git -C $script:testRepo init --quiet
		& git -C $script:testRepo config user.name 'Mumble Gate Test'
		& git -C $script:testRepo config user.email 'gate@example.invalid'
		Set-Content -LiteralPath (Join-Path $script:testRepo 'tracked.txt') -Value 'frozen' -Encoding utf8
		& git -C $script:testRepo add tracked.txt
		& git -C $script:testRepo commit --quiet -m 'test candidate'
		if ($LASTEXITCODE -ne 0) { throw 'Could not create candidate test repository.' }
		$script:testExecutable = Join-Path $TestDrive 'mumble.exe'
		[IO.File]::WriteAllBytes($script:testExecutable, [byte[]](1, 2, 3, 4, 5))
	}

	It 'creates a release candidate only from a clean source tree' {
		$manifest = New-CommunityReleaseCandidateManifest -RepositoryRoot $script:testRepo `
			-Executable $script:testExecutable
		$manifest.schema_version | Should Be 1
		$manifest.candidate_kind | Should Be 'release'
		$manifest.source.clean | Should Be $true
		$manifest.candidate_id | Should Match '^[0-9a-f]{12}-[0-9a-f]{12}-[0-9a-f]{12}$'
		$manifest.windows.executable_sha256 | Should Be (Get-CommunityReleaseFileSha256 $script:testExecutable)
	}

	It 'fails closed for a dirty release source tree' {
		Set-Content -LiteralPath (Join-Path $script:testRepo 'untracked.txt') -Value 'dirty' -Encoding utf8
		$state = Get-CommunityReleaseSourceState -RepositoryRoot $script:testRepo
		$state.clean | Should Be $false
		$state.untracked_file_count | Should Be 1
		$threw = $false
		try {
			New-CommunityReleaseCandidateManifest -RepositoryRoot $script:testRepo `
				-Executable $script:testExecutable | Out-Null
		} catch {
			$threw = $true
		}
		$threw | Should Be $true
	}

	It 'allows a fingerprinted dirty development candidate without calling it release-ready' {
		Set-Content -LiteralPath (Join-Path $script:testRepo 'new-file.txt') -Value 'untracked' -Encoding utf8
		$manifest = New-CommunityReleaseCandidateManifest -RepositoryRoot $script:testRepo `
			-Executable $script:testExecutable -AllowDirtySource
		$manifest.candidate_kind | Should Be 'development'
		$manifest.source.clean | Should Be $false
		$manifest.source.untracked_file_count | Should Be 1
		$manifest.source.worktree_sha256 | Should Match '^[0-9a-f]{64}$'
	}

	It 'changes the candidate identity when executable bytes change' {
		$first = New-CommunityReleaseCandidateManifest -RepositoryRoot $script:testRepo `
			-Executable $script:testExecutable
		[IO.File]::WriteAllBytes($script:testExecutable, [byte[]](5, 4, 3, 2, 1))
		$second = New-CommunityReleaseCandidateManifest -RepositoryRoot $script:testRepo `
			-Executable $script:testExecutable
		$second.candidate_id | Should Not Be $first.candidate_id
	}
}
