$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

Describe "Windows helper runtime capability gate" {
	BeforeAll {
		$buildScriptPath = Join-Path $PSScriptRoot "..\build-local-windows-client.ps1"
		$scriptText = Get-Content -Raw -LiteralPath $buildScriptPath
		$tokens = $null
		$parseErrors = $null
		$ast = [System.Management.Automation.Language.Parser]::ParseFile(
			$buildScriptPath,
			[ref]$tokens,
			[ref]$parseErrors
		)
		$parseErrors.Count | Should Be 0

		foreach ($functionName in @(
			"Test-ObjectHasProperty",
			"Get-ObjectBooleanProperty",
			"Test-UsableH264EncoderCapability"
		)) {
			$function = $ast.Find(
				{
					param($node)
					$node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
						$node.Name -eq $functionName
				},
				$true
			)
			$function | Should Not BeNullOrEmpty
			. ([scriptblock]::Create($function.Extent.Text))
		}
	}

	It "accepts an executable GStreamer H.264 backend when ffmpeg encoders are unavailable" {
		$capabilities = [pscustomobject]@{
			encoder_backends = @(
				[pscustomobject]@{ id = "gstreamer-x264"; available = $true; codecs = @(1) }
			)
		}
		$runtimeSupport = [pscustomobject]@{
			h264_mf_available = $false
			h264_qsv_available = $false
			libx264_available = $false
			gst_x264enc_available = $true
		}

		(Test-UsableH264EncoderCapability -Capabilities $capabilities -RuntimeSupport $runtimeSupport) |
			Should Be $true
	}

	It "rejects the non-executable planning stub" {
		$capabilities = [pscustomobject]@{
			encoder_backends = @(
				[pscustomobject]@{ id = "stub"; available = $true; codecs = @(1, 2, 3, 4) }
			)
		}
		$runtimeSupport = [pscustomobject]@{}

		(Test-UsableH264EncoderCapability -Capabilities $capabilities -RuntimeSupport $runtimeSupport) |
			Should Be $false
	}

	It "keeps compatibility with the legacy ffmpeg runtime flags" {
		$capabilities = [pscustomobject]@{}
		$runtimeSupport = [pscustomobject]@{ h264_nvenc_available = $true }

		(Test-UsableH264EncoderCapability -Capabilities $capabilities -RuntimeSupport $runtimeSupport) |
			Should Be $true
	}

	It "uses the shared capability helper in the release probe" {
		$scriptText | Should Match 'Test-UsableH264EncoderCapability -Capabilities \$capabilities -RuntimeSupport \$runtimeSupport'
	}
}
