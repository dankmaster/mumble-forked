Set-StrictMode -Version Latest

function Get-QmlVisualFileSha256 {
	param([Parameter(Mandatory = $true)][string]$Path)
	return (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToLowerInvariant()
}

function Get-QmlVisualPngDimensions {
	param([Parameter(Mandatory = $true)][string]$Path)
	Add-Type -AssemblyName System.Drawing
	$image = [System.Drawing.Image]::FromFile((Resolve-Path -LiteralPath $Path).Path)
	try { return @{ width = [int]$image.Width; height = [int]$image.Height } }
	finally { $image.Dispose() }
}

function Assert-QmlVisualManifest {
	param([Parameter(Mandatory = $true)]$Manifest)
	if ([int]$Manifest.schema_version -ne 1) { throw "Unsupported visual manifest schema." }
	$cases = @($Manifest.cases)
	if ($cases.Count -eq 0) { throw "Visual manifest contains no cases." }
	$ids = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
	foreach ($case in $cases) {
		if ([string]::IsNullOrWhiteSpace([string]$case.id) -or -not $ids.Add([string]$case.id)) {
			throw "Visual manifest contains an empty or duplicate case ID."
		}
		if ([string]$case.image_sha256 -notmatch '^[0-9a-fA-F]{64}$' -or
			[string]$case.accessibility_sha256 -notmatch '^[0-9a-fA-F]{64}$') {
			throw "Visual manifest case '$($case.id)' is missing a required hash."
		}
		if ([int]$case.image_width -lt 1 -or [int]$case.image_height -lt 1) {
			throw "Visual manifest case '$($case.id)' has invalid image dimensions."
		}
	}
	return $true
}

Export-ModuleMember -Function Get-QmlVisualFileSha256, Get-QmlVisualPngDimensions, Assert-QmlVisualManifest
