$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

Describe "Windows helper runtime capability gate" {
	BeforeAll {
		$buildScriptPath = Join-Path $PSScriptRoot "..\build-local-windows-client.ps1"
		$scriptText = Get-Content -Raw -LiteralPath $buildScriptPath
		$artifactAssertionScriptText = Get-Content -Raw -LiteralPath (
			Join-Path $PSScriptRoot "..\assert-windows-build-artifacts.ps1"
		)
		$updatePackageScriptPath = Join-Path $PSScriptRoot "..\create-windows-update-package.ps1"
		$updatePackageScriptText = Get-Content -Raw -LiteralPath $updatePackageScriptPath
		$environmentPublisherScriptText = Get-Content -Raw -LiteralPath (
			Join-Path $PSScriptRoot "..\publish-windows-build-environment.ps1"
		)
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
			"Test-UsableH264EncoderCapability",
			"Get-PeRuntimeDependencies"
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

		$packageTokens = $null
		$packageParseErrors = $null
		$packageAst = [System.Management.Automation.Language.Parser]::ParseFile(
			$updatePackageScriptPath,
			[ref]$packageTokens,
			[ref]$packageParseErrors
		)
		$packageParseErrors.Count | Should Be 0
		$requiredPayloadFunction = $packageAst.Find(
			{
				param($node)
				$node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
					$node.Name -eq "Get-RequiredQtQuickPayloadPaths"
			},
			$true
		)
		$requiredPayloadFunction | Should Not BeNullOrEmpty
		. ([scriptblock]::Create($requiredPayloadFunction.Extent.Text))
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

	It "separates startup imports from media-only delay-load imports" {
		$report = Get-PeRuntimeDependencies -DumpbinOutput @(
			"Image has the following dependencies:",
			"  Qt6Quick.dll",
			"  Qt6Qml.dll",
			"Image has the following delay load dependencies:",
			"  Qt6WebEngineQuick.dll",
			"  Qt6WebEngineCore.dll",
			"  rnnoise.dll",
			"  onnxruntime.dll",
			"Summary",
			"  ignored.dll"
		)

		@($report.Direct).Count | Should Be 2
		($report.Direct -contains "Qt6Quick.dll") | Should Be $true
		($report.Direct -contains "Qt6WebEngineQuick.dll") | Should Be $false
		@($report.DelayLoad).Count | Should Be 4
		($report.DelayLoad -contains "Qt6WebEngineQuick.dll") | Should Be $true
		($report.DelayLoad -contains "Qt6WebEngineCore.dll") | Should Be $true
		($report.DelayLoad -contains "rnnoise.dll") | Should Be $true
		($report.DelayLoad -contains "onnxruntime.dll") | Should Be $true
	}

	It "keeps WebEngine out of the Windows startup import table" {
		$cmakeText = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot "..\..\..\src\mumble\CMakeLists.txt")
		$mainText = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot "..\..\..\src\mumble\main.cpp")

		$cmakeText | Should Match 'mumble_delay_load_qt_runtime\(Qt6::WebEngineQuick\)'
		$cmakeText | Should Match 'mumble_delay_load_qt_runtime\(Qt6::WebEngineCore\)'
		$cmakeText | Should Match 'target_link_options\(mumble_client_object_lib PUBLIC'
		$mainText | Should Match 'QCoreApplication::setAttribute\(Qt::AA_ShareOpenGLContexts\)'
		$mainText | Should Match 'api != QSGRendererInterface::Software'
		$mainText | Should Match 'QQuickWindow::setGraphicsApi\(QSGRendererInterface::Direct3D11\)'
		$mainText | Should Not Match 'QQuickWindow::setGraphicsApi\(QSGRendererInterface::OpenGL\)'
		$scriptText | Should Match 'delay-load-runtime-dependencies\.txt'
		$scriptText | Should Match 'media-only runtimes'
	}

	It "keeps optional neural speech runtimes out of the Windows startup import table" {
		$cmakeText = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot "..\..\..\src\mumble\CMakeLists.txt")

		$cmakeText | Should Match '/DELAYLOAD:\$<TARGET_FILE_NAME:rnnoise>'
		$cmakeText | Should Match '/DELAYLOAD:\$\{ONNXRUNTIME_RUNTIME_NAME\}'
		foreach ($runtime in @('rnnoise.dll', 'onnxruntime.dll')) {
			$scriptText | Should Match ([regex]::Escape($runtime))
			$artifactAssertionScriptText | Should Match ([regex]::Escape($runtime))
			$updatePackageScriptText | Should Match ([regex]::Escape($runtime))
		}
		$scriptText | Should Match 'Test-Path -LiteralPath \(Join-Path \$StageRoot \$optionalNeuralRuntime\)'
		$artifactAssertionScriptText | Should Match 'Test-Path -LiteralPath \(Join-Path \$Root \$optionalNeuralRuntime\)'
		$updatePackageScriptText | Should Match 'Test-Path -LiteralPath \(Join-Path \$Root \$optionalNeuralRuntime\)'
	}

	It "requires the complete native Qt Multimedia QML and WMF runtime" {
		$stagePaths = @(
			"Qt6Multimedia.dll",
			"Qt6MultimediaQuick.dll",
			"multimedia\windowsmediaplugin.dll",
			"qml\QtMultimedia\qmldir",
			"qml\QtMultimedia\plugins.qmltypes",
			"qml\QtMultimedia\quickmultimediaplugin.dll",
			"qml\QtMultimedia\Video.qml"
		)
		$payloadPaths = @($stagePaths | ForEach-Object { $_.Replace('\', '/') })

		foreach ($path in $stagePaths) {
			$scriptText | Should Match ([regex]::Escape($path))
		}
		foreach ($path in $payloadPaths) {
			$artifactAssertionScriptText | Should Match ([regex]::Escape($path))
			$updatePackageScriptText | Should Match ([regex]::Escape($path))
			(@(Get-RequiredQtQuickPayloadPaths) -contains $path) | Should Be $true
		}

		foreach ($path in @(
			"share\Qt6Multimedia\Qt6MultimediaTargets.cmake",
			"bin\Qt6Multimedia.dll",
			"bin\Qt6MultimediaQuick.dll",
			"Qt6\qml\QtMultimedia\quickmultimediaplugin.dll"
		)) {
			$environmentPublisherScriptText | Should Match ([regex]::Escape($path))
		}
	}

	It "rejects the unused Qt Multimedia Widgets runtime from every Windows payload gate" {
		$scriptText | Should Match '(?s)\$forbiddenRuntime\s*=\s*@\(.*?Qt6MultimediaWidgets\.dll'
		$scriptText | Should Match '(?s)\$forbiddenDirectRuntimes\s*=\s*@\(.*?Qt6MultimediaWidgets\.dll'
		$artifactAssertionScriptText | Should Match '(?s)\$forbiddenPayloadRuntimes\s*=.*?Qt6MultimediaWidgets\.dll'
		$artifactAssertionScriptText | Should Match '(?s)\$forbiddenDirectRuntimes\s*=\s*@\(.*?Qt6MultimediaWidgets\.dll'
		$updatePackageScriptText | Should Match '(?s)\$forbiddenPayloadRuntimes\s*=.*?Qt6MultimediaWidgets\.dll'
		$updatePackageScriptText | Should Match '(?s)\$forbiddenDirectRuntimes\s*=\s*@\(.*?Qt6MultimediaWidgets\.dll'
		(@(Get-RequiredQtQuickPayloadPaths) -contains 'Qt6MultimediaWidgets.dll') | Should Be $false
	}
}
