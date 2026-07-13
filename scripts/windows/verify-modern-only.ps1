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

$qmlResourcePath = Join-Path $repoRoot 'src\mumble\qml-shell.qrc'
$qmlResource = [xml](Get-Content -LiteralPath $qmlResourcePath -Raw)
$registeredQmlFiles = @($qmlResource.RCC.qresource.file | ForEach-Object { [string]$_.'#text' } | Sort-Object -Unique)
$sourceQmlFiles = @(Get-ChildItem (Join-Path $repoRoot 'src\mumble\qml-shell') -File -Filter '*.qml' |
	ForEach-Object { "qml-shell/$($_.Name)" } | Sort-Object -Unique)
$qmlResourceMismatch = @(Compare-Object -ReferenceObject $sourceQmlFiles -DifferenceObject $registeredQmlFiles |
	ForEach-Object { "$($_.SideIndicator) $($_.InputObject)" })
$retiredClassicThemeFiles = @(
	'src\mumble\ThemeInfo.cpp',
	'src\mumble\ThemeInfo.h',
	'src\mumble\Themes.cpp',
	'src\mumble\Themes.h',
	'themes\DefaultTheme.qrc',
	'themes\CatppuccinTheme.qrc'
) | ForEach-Object { Join-Path $repoRoot $_ } | Where-Object { Test-Path -LiteralPath $_ }
$retiredClassicThemeFiles = @($retiredClassicThemeFiles) + @(
	Get-ChildItem (Join-Path $repoRoot 'themes') -Recurse -File -ErrorAction SilentlyContinue |
		ForEach-Object FullName
)

$nativeStatusIconAliases = @(
	'native/deafened_self.svg',
	'native/deafened_server.svg',
	'native/information.svg',
	'native/muted_pushtomute.svg',
	'native/muted_self.svg',
	'native/muted_server.svg',
	'native/muted_suppressed.svg',
	'native/talking_off.svg',
	'native/talking_on.svg',
	'native/talking_shout.svg',
	'native/talking_whisper.svg'
) | Sort-Object
$mumbleResourcePath = Join-Path $repoRoot 'src\mumble\mumble.qrc'
$mumbleResource = [xml](Get-Content -LiteralPath $mumbleResourcePath -Raw)
$nativeStatusIconEntries = @($mumbleResource.RCC.qresource.file | Where-Object { [string]$_.alias -like 'native/*' })
$registeredNativeStatusIconAliases = @($nativeStatusIconEntries | ForEach-Object { [string]$_.alias } | Sort-Object)
$nativeStatusIconResourceMismatch = @(
	Compare-Object -ReferenceObject $nativeStatusIconAliases -DifferenceObject $registeredNativeStatusIconAliases |
		ForEach-Object { "$($_.SideIndicator) $($_.InputObject)" }
)
$nativeStatusIconResourceMismatch += @(
	$nativeStatusIconEntries | ForEach-Object {
		$sourcePath = Join-Path (Split-Path $mumbleResourcePath -Parent) ([string]$_.'#text')
		if (-not (Test-Path -LiteralPath $sourcePath)) { $sourcePath }
	}
)

$checks = [ordered]@{
	ui_files = @(Get-ChildItem (Join-Path $repoRoot 'src\mumble') -Recurse -Filter '*.ui' -File | ForEach-Object FullName)
	qml_resource_mismatch = $qmlResourceMismatch
	native_status_icon_resource_mismatch = $nativeStatusIconResourceMismatch
	qml_rich_text_outside_structured_renderer = Find-Matches @('-n', 'Text\.(?:RichText|StyledText|MarkdownText)|textFormat\s*:\s*Text\.(?:RichText|StyledText|MarkdownText)', 'src/mumble/qml-shell', '--glob', '*.qml', '--glob', '!RichMessageBody.qml')
	qml_label_autotext_defaults = Find-Matches @('-n', '-U', '--pcre2', '\bLabel\s*\{(?:[ \t]*\r?\n(?![ \t]*textFormat\s*:)|(?![ \t]*\r?\n)(?![^\r\n}]*\btextFormat\s*:))', 'src/mumble/qml-shell', '--glob', '*.qml', '--glob', '!RichMessageBody.qml')
	qml_self_model_bindings = Find-Matches @('-n', '--pcre2', '\b([A-Za-z_]\w*Model)\s*:\s*\1\b', 'src/mumble/qml-shell', '--glob', '*.qml')
	mainwindow_ui = @((Test-Path (Join-Path $repoRoot 'src\mumble\MainWindowUi.h')) | Where-Object { $_ })
	mainwindow_spurious_autoconnect_slots = Find-Matches @('-n', 'on_(?:qmServer_aboutToShow|qaServerSettings_triggered|qaCreateTextRoom_triggered|qmSelf_aboutToShow|qaUserGrantChatHistory_triggered|qmConfig_aboutToShow|muteCuePopup_triggered)', 'src/mumble/MainWindow.cpp', 'src/mumble/MainWindow.h', 'src/mumble/AudioInput.cpp', 'src/mumble/Messages.cpp')
	modern_layout_guards = Find-Matches @('-n', 'MUMBLE_HAS_MODERN_LAYOUT|modern-layout-webengine|activateLegacyShell', 'src', '.github', 'scripts/windows', '--glob', '!*.ts', '--glob', '!verify-modern-only.ps1')
	classic_mainwindow_views = Find-Matches @('-n', 'qtvUsers|qdwLog|qdwChat|qteLog|qteChat|qtIconToolbar', 'src/mumble/MainWindow.cpp', 'src/mumble/MainWindow.h')
	mainwindow_widget_facade = Find-Matches @('-n', 'class MainWindow\s*:\s*public QMainWindow|QMainWindow\s*\(|QMainWindow::|QtWidgets/QMainWindow', 'src/mumble/MainWindow.cpp', 'src/mumble/MainWindow.h')
	mainwindow_product_widget_members = Find-Matches @('-n', 'QtWidgets/(QHBoxLayout|QLabel|QMenu|QMenuBar|QVBoxLayout)|\bQ(Widget|Frame|Label|Menu|MenuBar|PushButton|ToolButton|HBoxLayout|VBoxLayout)\s*\*', 'src/mumble/MainWindow.h')
	compatibility_widgets = Find-Matches @('-n', 'QQuickWidget|ChatbarTextEdit|class UserView|QDockWidget|QToolBar', 'src/mumble', '--glob', '*.cpp', '--glob', '*.h')
	legacy_web_hosts = Find-Matches @('-n', 'ModernShellHost|ModernShellBridge|ModernDialogHost|ModernContextMenuHost|ModernPttToolHost', 'src/mumble', '--glob', '*.cpp', '--glob', '*.h')
	webchannel = Find-Matches @('-n', 'QWebChannel|Qt6::WebChannel', 'src/mumble', '--glob', '*.cpp', '--glob', '*.h', '--glob', 'CMakeLists.txt')
	webengine_widgets = Find-Matches @('-n', 'QWebEngineView|QtWebEngineWidgets|Qt6::WebEngineWidgets', 'src/mumble', '--glob', '*.cpp', '--glob', '*.h', '--glob', 'CMakeLists.txt')
	webengine_quick_outside_media_allowlist = Find-Matches @('-n', 'QtWebEngine|WebEngineView|WebEngineProfile', 'src/mumble', '--glob', '*.cpp', '--glob', '*.h', '--glob', '*.qml', '--glob', '!MediaSessionWindow.qml', '--glob', '!QmlMediaProfileFactory.cpp', '--glob', '!QmlMediaProfileFactory.h', '--glob', '!main.cpp')
	legacy_snapshot_bridge = Find-Matches @('-n', 'buildModernShellSnapshot|queueModernShellSnapshot|ModernShellSnapshotSync|modernShellSnapshot', 'src/mumble', '--glob', '*.cpp', '--glob', '*.h')
	legacy_patch_bridge_symbols = Find-Matches @('-n', 'buildModernShellRoomStatePatch|publishModernShellMessagesPatch|publishModernShellRoomStatePatch|m_modernShell[A-Za-z0-9_]*Patch', 'src/mumble/MainWindow.cpp', 'src/mumble/MainWindow.h')
	server_ping_full_frontend_reconcile = Find-Matches @('-n', '-U', '--pcre2', 'pingRequested[\s\S]{0,600}(?:scheduleQmlRoomStateUpdate|scheduleQmlShellStateSync|syncQmlShellState)|(?:scheduleQmlRoomStateUpdate|scheduleQmlShellStateSync|syncQmlShellState)[\s\S]{0,600}pingRequested', 'src/mumble/MainWindow.cpp', 'src/mumble/MainWindow.h', 'src/mumble/ServerHandler.cpp', 'src/mumble/ServerHandler.h')
	hidden_product_widget_construction = Find-Matches @('-n', 'new (UserView|LogTextBrowser|ChatbarTextEdit|PersistentChatMessageGroupWidget|ResponsiveImageDialog|RichTextEditor)', 'src/mumble', '--glob', '*.cpp')
	usermodel_classic_presentation = Find-Matches @('-n', 'Qt::(?:DecorationRole|FontRole|BackgroundRole|ToolTipRole|WhatsThisRole)|\bQIcon\b|skin:|\b(?:expandAll|collapseEmpty|ensureSelfVisible|forceVisualUpdate)\b', 'src/mumble/UserModel.cpp', 'src/mumble/UserModel.h')
	classic_qss_theme_resources = @($retiredClassicThemeFiles) + @(Find-Matches @('-n', '\bThemeInfo\b|\bThemes::|[<"]Themes\.h[>"]|DefaultTheme\.qrc|CatppuccinTheme\.qrc|requireThemeApplication', 'src/mumble', '--glob', '*.cpp', '--glob', '*.h', 'src/tests/TestSettingsJSONSerialization/CMakeLists.txt', '--glob', 'CMakeLists.txt'))
	classic_skin_resource_refs = Find-Matches @('-n', 'skin:', 'src/mumble', '--glob', '*.cpp', '--glob', '*.h', '--glob', '*.qml', '--glob', '*.qrc', '--glob', 'CMakeLists.txt')
	legacy_hidden_widget_helpers = Find-Matches @('-n', '\b(?:DeveloperConsole|ApplicationPalette|ClickableLabel|EventFilters)\b|new\s+QTextBrowser\s*\(|new\s+QWidget\s*\(', 'src/mumble', '--glob', '*.cpp', '--glob', '*.h', '--glob', 'CMakeLists.txt')
	widget_backed_log_sink = Find-Matches @('-n', 'qt_color_sink|QtLogSink|spdlog/sinks/qt_sinks', 'src/mumble', '--glob', '*.cpp', '--glob', '*.h', '--glob', 'CMakeLists.txt')
	native_product_dialog_fallbacks = Find-Matches @('-n', 'QDialog dialog\(this\)|AboutDialog adAbout|class AboutDialog|new AboutDialog|ServerNavigatorUserMenuPopup', 'src/mumble', '--glob', '*.cpp', '--glob', '*.h')
	viewcert_dialog_launch = Find-Matches @('-n', '\bViewCert\s+[A-Za-z_]\w*\s*\(|\b(?:vc|viewCert|certDialog|certificateDialog)\s*(?:\.|->)\s*(?:exec|open|show)\s*\(', 'src/mumble', '--glob', '*.cpp')
	settings_owned_startup_qdialogs = Find-Matches @('-n', 'execModernStartup(?:Question|Notice)|modernStartup(?:DialogStyleSheet|Label|Button)|\bQDialog\s+dialog\s*;', 'src/mumble/Settings.cpp')
	mainwindow_disallowed_widget_headers = Find-Matches @('-n', 'QtWidgets/Q(?:AbstractItemView|CheckBox|ComboBox|Dialog|DialogButtonBox|FormLayout|Frame|GroupBox|HBoxLayout|Label|LineEdit|PlainTextEdit|PushButton|ScrollBar|SizePolicy|SpinBox|Splitter|StatusBar|Style|StyledItemDelegate|ToolButton|ToolTip|VBoxLayout|WhatsThis|Widget)', 'src/mumble/MainWindow.cpp', 'src/mumble/MainWindow.h')
	legacy_rich_text_editor_widget_headers = Find-Matches @('-n', 'QtWidgets/|QColorDialog|QToolTip', 'src/mumble/RichTextEditor.cpp', 'src/mumble/RichTextEditor.h')
	legacy_global_shortcut_widget_headers = Find-Matches @('-n', 'QtWidgets/|QHelpEvent|QSortFilterProxyModel', 'src/mumble/GlobalShortcut.cpp', 'src/mumble/GlobalShortcut.h')
	legacy_screen_widget_adapters = Find-Matches @('-n', '\b(?:windowFromWidget|screenFromWidget)\b|\bQWidget\b', 'src/mumble/Screen.cpp', 'src/mumble/Screen.h')
	frontend_neutral_updater_parent = Find-Matches @('-n', '\bForkUpdateInstaller\s*\([^\r\n]*QWidget|VersionCheck::(?:install|download)UpdateFromInfo\([^\r\n]*QWidget|@param parent[^\r\n]*QWidget', 'src/mumble/VersionCheck.cpp', 'src/mumble/VersionCheck.h', 'src/mumble/PluginUpdater.h')
	native_whats_this_fallback = Find-Matches @('-n', 'QWhatsThis::enterWhatsThisMode|QtWidgets/QWhatsThis', 'src/mumble/MainWindow.cpp', 'src/mumble/MainWindow.h')
	constant_frontend_branches = Find-Matches @('-n', '\bif\s*\(\s*(?:!\s*)?(?:true|false)(?:\s*(?:&&|\|\|)|\s*\))', 'src/mumble/MainWindow.cpp')
	legacy_chat_widget_renderer = Find-Matches @('-n', 'renderPersistentChatViewImmediately|PersistentChatHistoryRow|Q_UNUSED\(rows\)', 'src/mumble/MainWindow.cpp', 'src/mumble/MainWindow.h')
	qtextdocument_server_log = Find-Matches @('-n', 'm_modernServerLogDocument|modernServerLogHtml|serverLogHtml', 'src/mumble/MainWindow.cpp', 'src/mumble/MainWindow.h')
	windows_static_client_selector = Find-Matches @('-n', '(?:-SharedWebEngine\b|\[switch\]\$SharedWebEngine|SharedWebEngine\s*=)', 'scripts/windows/build-local-windows-client.ps1', '.github/workflows/windows-shared-client.yml', '.github/workflows/mumble-forked.yml')
	compiled_classic_widget_sources = Find-Matches @('-n', '(?:AudioStats|MenuLabel|VolumeSliderWidgetAction|MultiColumnTreeWidget|MultiStyleWidgetWrapper|SemanticSlider|ListenerVolumeSlider|UserLocalVolumeSlider)\.cpp', 'src/mumble/CMakeLists.txt')
	modern_shell_qmenu_serialization = Find-Matches @('-n', '#include\s*[<"]QMenu[>"]|\bserializeMenu\s*\(|\bmenu->actions\s*\(', 'src/mumble/ModernShellMenuSerializer.cpp', 'src/mumble/ModernShellMenuSerializer.h')
	modern_shell_widget_action_serialization = Find-Matches @('-n', '\b(?:MenuLabel|VolumeSliderWidgetAction)\b|qobject_cast\s*<[^>]*(?:MenuLabel|VolumeSliderWidgetAction)', 'src/mumble/ModernShellMenuSerializer.cpp', 'src/mumble/ModernShellMenuSerializer.h')
	legacy_product_widget_types = Find-Matches @('-n', 'class (PersistentChatListWidget|ExternalScreenShareWindowHost|RichTextEditor|ResponsiveImageDialog)', 'src/mumble', '--glob', '*.h')
	legacy_mainwindow_product_state = Find-Matches @('-n', 'm_persistentChat(History|Composer|ConversationPanel|LogPanel|ChannelList)', 'src/mumble/MainWindow.cpp', 'src/mumble/MainWindow.h')
	legacy_web_product_resources = @(Get-ChildItem (Join-Path $repoRoot 'src\mumble\modern-shell') -Recurse -File -ErrorAction SilentlyContinue |
		Where-Object { $_.Extension -in @('.html', '.css', '.js') } |
		ForEach-Object FullName)
	widget_prompts = Find-Matches @('-n', 'QMessageBox|QInputDialog|QProgressDialog', 'src/mumble', '--glob', '*.cpp')
}

$summary = [ordered]@{}
foreach ($entry in $checks.GetEnumerator()) {
	$summary[$entry.Key] = @($entry.Value).Count
}

$summary | ConvertTo-Json

if ($Strict -and ($checks.Values | ForEach-Object { @($_).Count } | Where-Object { $_ -ne 0 })) {
	exit 1
}

exit 0
