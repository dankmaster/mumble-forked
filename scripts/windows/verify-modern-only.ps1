param(
	[switch] $Strict
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path

function Find-Matches {
	param([string[]] $Arguments)
	Push-Location $repoRoot
	try {
		$output = & rg @Arguments 2>$null
	} finally {
		Pop-Location
	}
	if ($LASTEXITCODE -gt 1) {
		throw "rg failed while checking: $($Arguments -join ' ')"
	}
	return @($output)
}

$checks = [ordered]@{
	ui_files = @(Get-ChildItem (Join-Path $repoRoot 'src\mumble') -Recurse -Filter '*.ui' -File | ForEach-Object FullName)
	modern_layout_guards = Find-Matches @('-n', 'MUMBLE_HAS_MODERN_LAYOUT|modern-layout-webengine|activateLegacyShell', 'src', '.github', 'scripts/windows', '--glob', '!*.ts', '--glob', '!verify-modern-only.ps1')
	classic_mainwindow_views = Find-Matches @('-n', 'qtvUsers|qdwLog|qdwChat|qteLog|qteChat|qtIconToolbar', 'src/mumble/MainWindow.cpp', 'src/mumble/MainWindow.h', 'src/mumble/MainWindowUi.h')
	widget_prompts = Find-Matches @('-n', 'QMessageBox|QInputDialog|QProgressDialog', 'src/mumble', '--glob', '*.cpp')
}

$summary = [ordered]@{}
foreach ($entry in $checks.GetEnumerator()) {
	$summary[$entry.Key] = @($entry.Value).Count
}

$summary | ConvertTo-Json

if ($Strict -and ($summary.Values | Where-Object { $_ -ne 0 })) {
	exit 1
}
