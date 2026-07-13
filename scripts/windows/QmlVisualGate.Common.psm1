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

Export-ModuleMember -Function Get-QmlVisualFileSha256, Get-QmlVisualPngDimensions, Compare-QmlVisualPng, Assert-QmlVisualManifest
