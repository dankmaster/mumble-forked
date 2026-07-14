Set-StrictMode -Version Latest

if (-not ('QmlVisualPngComparer' -as [type])) {
	Add-Type -AssemblyName System.Drawing.Common
	$drawingAssemblyDirectory = Split-Path -Parent ([System.Drawing.Bitmap].Assembly.Location)
	$drawingReferences = @(
		[System.Drawing.Bitmap].Assembly.Location,
		[System.Drawing.Rectangle].Assembly.Location,
		[System.Runtime.InteropServices.Marshal].Assembly.Location,
		(Join-Path $drawingAssemblyDirectory 'System.Private.Windows.Core.dll'),
		(Join-Path $drawingAssemblyDirectory 'System.Private.Windows.GdiPlus.dll')
	) | Sort-Object -Unique
	foreach ($reference in $drawingReferences) {
		if (-not (Test-Path -LiteralPath $reference -PathType Leaf)) {
			throw "Required System.Drawing compiler reference is unavailable: $reference"
		}
	}
	Add-Type -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Runtime.InteropServices;

public sealed class QmlVisualPngComparisonResult {
    public int Width { get; set; }
    public int Height { get; set; }
    public long ChangedPixels { get; set; }
    public long AllowedChangedPixels { get; set; }
    public int MaximumChannelDelta { get; set; }
    public bool Passed { get; set; }
}

public sealed class QmlVisualPngCoverageResult {
    public int Width { get; set; }
    public int Height { get; set; }
    public long PixelCount { get; set; }
    public long NonBlackPixels { get; set; }
    public double NonBlackFraction { get; set; }
}

public static class QmlVisualPngComparer {
    private static Bitmap LoadCanonical(string path) {
        using (Image decoded = Image.FromFile(path, true)) {
            if (decoded.RawFormat.Guid != ImageFormat.Png.Guid)
                throw new InvalidDataException("Visual artifact is not a PNG image.");
            var canonical = new Bitmap(decoded.Width, decoded.Height, PixelFormat.Format32bppArgb);
            using (Graphics graphics = Graphics.FromImage(canonical)) {
                graphics.CompositingMode = System.Drawing.Drawing2D.CompositingMode.SourceCopy;
                graphics.DrawImageUnscaled(decoded, 0, 0);
            }
            return canonical;
        }
    }

    public static QmlVisualPngComparisonResult Compare(string expectedPath, string actualPath) {
        using (Bitmap expected = LoadCanonical(expectedPath))
        using (Bitmap actual = LoadCanonical(actualPath)) {
            if (expected.Width != actual.Width || expected.Height != actual.Height)
                throw new InvalidDataException("Visual artifact dimensions do not match the baseline.");
            var rect = new Rectangle(0, 0, expected.Width, expected.Height);
            BitmapData expectedData = null;
            BitmapData actualData = null;
            try {
                expectedData = expected.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
                actualData = actual.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
                int rowBytes = checked(expected.Width * 4);
                byte[] expectedRow = new byte[rowBytes];
                byte[] actualRow = new byte[rowBytes];
                long changed = 0;
                int maximumDelta = 0;
                for (int y = 0; y < expected.Height; ++y) {
                    Marshal.Copy(IntPtr.Add(expectedData.Scan0, y * expectedData.Stride), expectedRow, 0, rowBytes);
                    Marshal.Copy(IntPtr.Add(actualData.Scan0, y * actualData.Stride), actualRow, 0, rowBytes);
                    for (int x = 0; x < rowBytes; x += 4) {
                        bool pixelChanged = false;
                        for (int channel = 0; channel < 4; ++channel) {
                            int delta = Math.Abs(expectedRow[x + channel] - actualRow[x + channel]);
                            if (delta != 0) pixelChanged = true;
                            if (delta > maximumDelta) maximumDelta = delta;
                        }
                        if (pixelChanged) ++changed;
                    }
                }
                long pixelCount = checked((long)expected.Width * expected.Height);
                long allowed = Math.Max(16L, (long)Math.Floor(pixelCount * 0.0001d));
                return new QmlVisualPngComparisonResult {
                    Width = expected.Width,
                    Height = expected.Height,
                    ChangedPixels = changed,
                    AllowedChangedPixels = allowed,
                    MaximumChannelDelta = maximumDelta,
                    Passed = changed <= allowed && maximumDelta <= 32
                };
            } finally {
                if (expectedData != null) expected.UnlockBits(expectedData);
                if (actualData != null) actual.UnlockBits(actualData);
            }
        }
    }

    public static QmlVisualPngCoverageResult AnalyzeCoverage(string path, int blackThreshold) {
        if (blackThreshold < 0 || blackThreshold > 255)
            throw new ArgumentOutOfRangeException("blackThreshold");
        using (Bitmap image = LoadCanonical(path)) {
            var rect = new Rectangle(0, 0, image.Width, image.Height);
            BitmapData imageData = null;
            try {
                imageData = image.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
                int rowBytes = checked(image.Width * 4);
                byte[] row = new byte[rowBytes];
                long nonBlack = 0;
                for (int y = 0; y < image.Height; ++y) {
                    Marshal.Copy(IntPtr.Add(imageData.Scan0, y * imageData.Stride), row, 0, rowBytes);
                    for (int x = 0; x < rowBytes; x += 4) {
                        if (row[x + 3] != 0 && (row[x] > blackThreshold || row[x + 1] > blackThreshold
                                               || row[x + 2] > blackThreshold))
                            ++nonBlack;
                    }
                }
                long pixelCount = checked((long)image.Width * image.Height);
                return new QmlVisualPngCoverageResult {
                    Width = image.Width,
                    Height = image.Height,
                    PixelCount = pixelCount,
                    NonBlackPixels = nonBlack,
                    NonBlackFraction = pixelCount == 0 ? 0.0d : (double)nonBlack / pixelCount
                };
            } finally {
                if (imageData != null) image.UnlockBits(imageData);
            }
        }
    }
}
'@ -ReferencedAssemblies $drawingReferences
}

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

function Compare-QmlVisualPng {
	param(
		[Parameter(Mandatory = $true)][string]$BaselinePath,
		[Parameter(Mandatory = $true)][string]$CandidatePath
	)
	$baseline = (Resolve-Path -LiteralPath $BaselinePath).Path
	$candidate = (Resolve-Path -LiteralPath $CandidatePath).Path
	if ((Get-QmlVisualFileSha256 $baseline) -eq (Get-QmlVisualFileSha256 $candidate)) {
		$dimensions = Get-QmlVisualPngDimensions $baseline
		return [pscustomobject]@{
			passed = $true; exact = $true; width = $dimensions.width; height = $dimensions.height
			changed_pixels = 0L; allowed_changed_pixels = [Math]::Max(16L, [long][Math]::Floor(([long]$dimensions.width * $dimensions.height) * 0.0001))
			maximum_channel_delta = 0
		}
	}
	$result = [QmlVisualPngComparer]::Compare($baseline, $candidate)
	return [pscustomobject]@{
		passed = [bool]$result.Passed; exact = $false; width = [int]$result.Width; height = [int]$result.Height
		changed_pixels = [long]$result.ChangedPixels; allowed_changed_pixels = [long]$result.AllowedChangedPixels
		maximum_channel_delta = [int]$result.MaximumChannelDelta
	}
}

function Get-QmlVisualPngCoverage {
	param(
		[Parameter(Mandatory = $true)][string]$Path,
		[ValidateRange(0, 255)][int]$BlackThreshold = 8
	)
	$result = [QmlVisualPngComparer]::AnalyzeCoverage(
		(Resolve-Path -LiteralPath $Path).Path, $BlackThreshold)
	return [pscustomobject]@{
		width = [int]$result.Width; height = [int]$result.Height
		pixel_count = [long]$result.PixelCount; non_black_pixels = [long]$result.NonBlackPixels
		non_black_fraction = [double]$result.NonBlackFraction
	}
}

function Assert-QmlVisualManifest {
	param([Parameter(Mandatory = $true)]$Manifest)
	if ([int]$Manifest.schema_version -ne 1) { throw "Unsupported visual manifest schema." }
	$manifestProperties = if ($Manifest -is [Collections.IDictionary]) {
		@($Manifest.Keys | ForEach-Object { [string]$_ })
	} else {
		@($Manifest.PSObject.Properties.Name)
	}
	if ($manifestProperties -contains "executable_sha256" -and
		[string]$Manifest.executable_sha256 -notmatch '^[0-9a-fA-F]{64}$') {
		throw "Visual manifest contains an invalid executable SHA256."
	}
	if ($manifestProperties -contains "source_git_sha" -and
		[string]$Manifest.source_git_sha -ne 'unknown' -and
		[string]$Manifest.source_git_sha -notmatch '^[0-9a-fA-F]{40}$') {
		throw "Visual manifest contains an invalid source Git SHA."
	}
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

function Assert-QmlVisualManifestMatchesMatrix {
	param(
		[Parameter(Mandatory = $true)]$Manifest,
		[Parameter(Mandatory = $true)][string]$MatrixPath,
		[switch]$RequireCombinedCandidate
	)

	Assert-QmlVisualManifest $Manifest | Out-Null
	$resolvedMatrixPath = (Resolve-Path -LiteralPath $MatrixPath).Path
	$matrix = Get-Content -Raw -LiteralPath $resolvedMatrixPath | ConvertFrom-Json
	if ([int]$matrix.schema_version -ne 1) { throw "Unsupported visual matrix schema." }
	$matrixCases = @($matrix.cases)
	if ($matrixCases.Count -eq 0) { throw "Visual matrix contains no cases." }

	$matrixIds = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
	foreach ($case in $matrixCases) {
		$caseId = [string]$case.id
		if ([string]::IsNullOrWhiteSpace($caseId) -or -not $matrixIds.Add($caseId)) {
			throw "Visual matrix contains an empty or duplicate case ID."
		}
	}

	$manifestProperties = if ($Manifest -is [Collections.IDictionary]) {
		@($Manifest.Keys | ForEach-Object { [string]$_ })
	} else {
		@($Manifest.PSObject.Properties.Name)
	}
	$expectedMatrixHash = Get-QmlVisualFileSha256 $resolvedMatrixPath
	if ($manifestProperties -notcontains "matrix_sha256" -or
		-not [string]::Equals([string]$Manifest.matrix_sha256, $expectedMatrixHash,
			[StringComparison]::OrdinalIgnoreCase)) {
		throw "Visual manifest matrix hash does not match the current visual matrix."
	}

	$manifestIds = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
	foreach ($case in @($Manifest.cases)) { $null = $manifestIds.Add([string]$case.id) }
	if (-not $manifestIds.SetEquals($matrixIds)) {
		$missing = @($matrixIds | Where-Object { -not $manifestIds.Contains($_) } | Sort-Object)
		$unexpected = @($manifestIds | Where-Object { -not $matrixIds.Contains($_) } | Sort-Object)
		$missingText = if ($missing.Count -eq 0) { "<none>" } else { $missing -join ", " }
		$unexpectedText = if ($unexpected.Count -eq 0) { "<none>" } else { $unexpected -join ", " }
		throw "Visual manifest case set does not match the current matrix. Missing: $missingText. Unexpected: $unexpectedText."
	}

	if ($RequireCombinedCandidate) {
		if ($manifestProperties -notcontains "mode" -or
			[string]$Manifest.mode -cne "candidate-only" -or
			$manifestProperties -notcontains "process_isolation" -or
			[string]$Manifest.process_isolation -cne "per-dpr") {
			throw "Baseline updates require a complete candidate-only manifest produced by the per-DPR matrix runner."
		}
	}

	return $true
}

Export-ModuleMember -Function Get-QmlVisualFileSha256, Get-QmlVisualPngDimensions, Compare-QmlVisualPng, Get-QmlVisualPngCoverage, Assert-QmlVisualManifest, Assert-QmlVisualManifestMatchesMatrix
