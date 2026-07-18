#!/usr/bin/env python3

# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.


import argparse
import re
from collections import OrderedDict

defaultValues = {}
structs = {}

def extractFields(classDefinition):
    # Bring all declarations on a single line
    classDefinition = re.sub(r",\s+", ",", classDefinition)
    # Make sure there are no spaces in templates
    classDefinition = classDefinition.replace("< ", "<").replace(" >", ">")
    # Remove function definitions (aka: lines that contain a parenthesis)
    classDefinition = re.sub(r".*\(.*", "", classDefinition)
    # Remove enum declarations
    classDefinition = re.sub(r"enum\s*\w+\s*\{.*?\};", "", classDefinition, flags=re.DOTALL)
    # Remove "unsigned" type specifier
    classDefinition = classDefinition.replace("unsigned ", "")
    # Remove "mutable" keyword
    classDefinition = classDefinition.replace("mutable ", "")
    # Remove comments
    classDefinition = re.sub(r"//.*", "", classDefinition)
    # Remove default value assignments
    classDefinition = re.sub(r"\s*=[^;]+;", ";", classDefinition)
    # remove semicolons
    classDefinition = classDefinition.replace(";", "")

    fields = OrderedDict()
    for currentLine in classDefinition.split("\n"):
        currentLine = currentLine.strip()

        if currentLine.startswith("//") or currentLine.startswith("#") or currentLine in ["private:", "public:", "}"]:
            continue

        words = currentLine.split()

        if len(words) == 0:
            continue

        if words[0] in ["enum", "typedef", "struct"] or "static" in words:
            continue

        if len(words) > 2:
            # Comma-separated lists
            names = "".join(words[1:])
            words = [words[0], names]

        assert len(words) == 2, "Expected remaining lines to be of form <type> <name(s)>"

        for currentFieldName in words[1].split(","):
            currentFieldName = currentFieldName.strip()

            if not currentFieldName:
                continue

            fields[currentFieldName] = words[0]

    return fields


def extractClassDefinition(className, contents, classIdentifier = "class"):
    definition = contents[contents.find(classIdentifier + " " + className) :]
    curlyBrackets = 0
    endIndex = 0
    for c in definition:
        if c == '{':
            curlyBrackets += 1
        elif c == '}':
            curlyBrackets -= 1

            if curlyBrackets == 0:
                break
        
        endIndex += 1

    assert curlyBrackets == 0

    definition = definition[0 : endIndex]

    return definition


def getTemplateArguments(templateDef):
    relevantContent = templateDef[templateDef.find("<") + 1 : templateDef.rfind(">")]

    # Nested templates not yet supported
    assert not "<" in relevantContent

    args = relevantContent.split(",")
    args = [x.strip() for x in args]

    return args


def getDefaultValueForType(dataType):
    if dataType in ["int", "short", "long", "float", "double", "qreal"] or dataType.startswith("qint") or dataType.startswith("quint") or \
        dataType.startswith("uint"):
        return "42"
    elif match := re.search(r"optional<(.*)>", dataType):
        return getDefaultValueForType(match.group(1))
    elif dataType in ["bool"]:
        return "true"
    elif dataType in ["QString", "std::string"]:
        return "\"My String\""
    elif dataType in ["QByteArray"]:
        return "QByteArray::fromStdString(\"My ByteArray\")"
    elif dataType in ["QPoint"]:
        return "{ 4, 2 }"
    elif dataType in ["QVariant"]:
        return "QVariant(15)"
    elif dataType in ["QStringList"]:
        return "QStringList({ QStringLiteral(\"Another string\") })"
    elif dataType in ["QColor"]:
        return "QColor(QLatin1String(\"indigo\"))"
    elif dataType in ["QFont"]:
        return "QFont(QLatin1String(\"Helvetica\"))"
    elif dataType in ["QRect", "QRectF"]:
        return "{ 3, 5, 10, 7 }"
    elif dataType in ["QSize", "QSizeF"]:
        return "{ 8, 12 }"
    elif dataType in ["AudioTransmit"]:
        return "Settings::PushToTalk"
    elif dataType in ["VADSource"]:
        return "Settings::SignalToNoise"
    elif dataType in ["InputGateMode"]:
        return "Settings::InputGateStrict"
    elif dataType in ["LoopMode"]:
        return "Settings::Server"
    elif dataType in ["ChannelExpand"]:
        return "Settings::AllChannels"
    elif dataType in ["ChannelDrag"]:
        return "Settings::DoNothing"
    elif dataType in ["ServerShow"]:
        return "Settings::ShowPopulated"
    elif dataType in ["IdleAction"]:
        return "Settings::Deafen"
    elif dataType in ["NoiseCancel"]:
        return "Settings::NoiseCancelOff"
    elif dataType in ["SpeechCleanupBackend"]:
        return "Settings::DeepFilterNetBackend"
    elif dataType in ["RemoteSpeechCleanupPreset"]:
        return "Settings::Aggressive"
    elif dataType in ["EchoCancelOptionID"]:
        return "EchoCancelOptionID::SPEEX_MULTICHANNEL"
    elif dataType in ["QuitBehavior"]:
        return "QuitBehavior::ALWAYS_QUIT"
    elif dataType in ["Qt::Alignment"]:
        return "Qt::AlignJustify | Qt::AlignBaseline"
    elif dataType in ["WindowLayout"]:
        return "Settings::LayoutStacked"
    elif dataType in ["ModernLayoutPolicy"]:
        return "Settings::ModernLayoutFollowLegacy"
    elif dataType in ["AlwaysOnTopBehaviour"]:
        return "Settings::OnTopAlways"
    elif dataType in ["Search::SearchDialog::UserAction"]:
        return "Search::SearchDialog::UserAction::NONE"
    elif dataType in ["Search::SearchDialog::ChannelAction"]:
        return "Search::SearchDialog::ChannelAction::NONE"
    elif dataType in ["ProxyType"]:
        return "Settings::Socks5Proxy"
    elif dataType in ["RecordingMode"]:
        return "Settings::RecordingMultichannel"
    elif dataType in ["StyleType"]:
        return "StyleType::Dark"
    elif dataType.startswith("QMap") or dataType.startswith("QHash"):
        types = getTemplateArguments(dataType)

        assert len(types) == 2

        return "{ {" + getDefaultValueForType(types[0]) + ", " + getDefaultValueForType(types[1]) + "} }"
    elif dataType.startswith("QList"):
        types = getTemplateArguments(dataType)

        assert len(types) == 1

        return "{ " + getDefaultValueForType(types[0]) + " }"
    elif dataType.startswith("std::array"):
        args = getTemplateArguments(dataType)

        # std::array< Type, Size >
        assert len(args) == 2

        args[1] = int(args[1])

        string = "{ "
        for _ in range(args[1]):
            string += getDefaultValueForType(args[0]) + ", "

        if args[1] > 0:
            # remove trailing comma
            string = string[:-len(", ")]

        return string + " }"
    elif dataType in ["KeyPair"]:
        # We can't really create a certificate here, so we have to use a default-constructed value
        return "{}"
    elif dataType in ["Mumble::InputEnhancement::Settings"]:
        return "[] { Mumble::InputEnhancement::Settings value; value.defaultPreference.reduction = 42; return value; }()"

    if dataType in defaultValues:
        return defaultValues[dataType]

    raise RuntimeError("No known default value for type " + dataType)



def generateTestBody(settingsFields, settingsClassName, excludeFields = []):
    contents = "#include <QObject>\n"
    contents += "#include <QList>\n"
    contents += "#include <QPair>\n"
    contents += "#include <QSettings>\n"
    contents += "#include <QString>\n"
    contents += "#include <QTemporaryDir>\n"
    contents += "#include <QtTest>\n"
    contents += "#include \"" + settingsClassName + ".h\"\n"
    contents += "#include \"JSONSerialization.h\"\n"
    contents += "#include \"SpeechCleanup.h\"\n"
    contents += "#include <nlohmann/json.hpp>\n"
    contents += "\n"
    contents += "class TestSettingsJSONSerialization : public QObject {\n"
    contents += "\tQ_OBJECT\n"
    contents += "\t" + settingsClassName + " createSettingsInstance() const {\n"
    contents += "\t\t" + settingsClassName + " settings;\n"
    for fieldName in settingsFields:
        if fieldName in excludeFields:
            continue
        if settingsFields[fieldName] == "bool":
            # For boolean values, we simply use the inverse of whatever the default is
            contents += "\t\tsettings." + fieldName + " = !settings." + fieldName + ";\n"
        else:
            contents += "\t\tsettings." + fieldName + " = " + getDefaultValueForType(settingsFields[fieldName]) + ";\n"
    contents += "\n"
    contents += "\t\treturn settings;\n"
    contents += "\t}\n"
    contents += "\n"

    contents += "private slots:\n"
    contents += "\tvoid noDefaultValuesMatched() {\n"
    contents += "\t\tconst " + settingsClassName + " defaults;\n"
    contents += "\t\tconst " + settingsClassName + " myInstance = createSettingsInstance();\n"
    for fieldName in settingsFields:
        if fieldName in excludeFields:
            continue
        contents += "\t\tQVERIFY2(defaults." + fieldName + "!= myInstance." + fieldName  + ", \"Field '" \
                + fieldName + "' was set to its default value (breaking underlying assumption of the following test)\");\n"
    contents += "\t}\n"
    contents += "\n"
    contents += "\tvoid testJSONSerialization() {\n"
    contents += "\t\tconst " + settingsClassName + " original = createSettingsInstance();\n"
    contents += "\t\tnlohmann::json jsonRepresentation = original;\n"
    contents += "\t\tconst " + settingsClassName + " deserialized = jsonRepresentation;\n"
    contents += "\t\t" + settingsClassName + " expected = original;\n"
    contents += "\t\tif (!Mumble::SpeechCleanup::isBackendAvailable(expected.noiseCancelBackend)) {\n"
    contents += "\t\t\texpected.noiseCancelBackend = Mumble::SpeechCleanup::fallbackBackend();\n"
    contents += "\t\t}\n"
    contents += "\t\tif (!Mumble::SpeechCleanup::isBackendAvailable(expected.remoteSpeechCleanupBackend)) {\n"
    contents += "\t\t\texpected.remoteSpeechCleanupBackend = Mumble::SpeechCleanup::fallbackBackend();\n"
    contents += "\t\t}\n"
    contents += "\t\texpected.noiseCancelModelId = Mumble::SpeechCleanup::normalizedModelId(expected.noiseCancelBackend, expected.noiseCancelModelId);\n"
    contents += "\t\tif (!Mumble::SpeechCleanup::usesCustomModelPath(expected.noiseCancelBackend, expected.noiseCancelModelId)) {\n"
    contents += "\t\t\texpected.noiseCancelCustomModelPath.clear();\n"
    contents += "\t\t}\n"
    contents += "\t\texpected.remoteSpeechCleanupModelId = Mumble::SpeechCleanup::normalizedModelId(expected.remoteSpeechCleanupBackend, expected.remoteSpeechCleanupModelId);\n"
    contents += "\t\tif (!Mumble::SpeechCleanup::usesCustomModelPath(expected.remoteSpeechCleanupBackend, expected.remoteSpeechCleanupModelId)) {\n"
    contents += "\t\t\texpected.remoteSpeechCleanupCustomModelPath.clear();\n"
    contents += "\t\t}\n"
    contents += "\t\tif ((expected.noiseCancelMode == Settings::NoiseCancelRNN\n"
    contents += "\t\t\t || expected.noiseCancelMode == Settings::NoiseCancelBoth)\n"
    contents += "\t\t\t&& !Mumble::SpeechCleanup::isBackendAvailable(expected.noiseCancelBackend)) {\n"
    contents += "\t\t\texpected.noiseCancelMode = Settings::NoiseCancelSpeex;\n"
    contents += "\t\t}\n"
    contents += "\t\tif (!Mumble::SpeechCleanup::isBackendAvailable(expected.remoteSpeechCleanupBackend)) {\n"
    contents += "\t\t\texpected.remoteSpeechCleanupEnabled = false;\n"
    contents += "\t\t}\n"
    contents += "\t\texpected.normalizeModernOnlyFrontendState();\n"
    contents += "\n"
    for fieldName in settingsFields:
        if fieldName in excludeFields:
            continue
        else:
            contents += "\t\tQCOMPARE(expected." + fieldName + ", deserialized." + fieldName + ");\n"
    contents += "\t}\n"
    contents += "\n"
    contents += "\tvoid modernWindowLayoutStaysModern() {\n"
    contents += "\t\tnlohmann::json jsonRepresentation;\n"
    contents += "\t\tjsonRepresentation[\"settings_version\"] = 1;\n"
    contents += "\t\tjsonRepresentation[\"ui\"][\"window_layout\"] = \"Modern\";\n"
    contents += "\n"
    contents += "\t\tconst " + settingsClassName + " deserialized = jsonRepresentation;\n"
    contents += "\t\tQCOMPARE(deserialized.wlWindowLayout, Settings::LayoutModern);\n"
    contents += "\t\tQCOMPARE(deserialized.modernLayoutPolicy, Settings::ModernLayoutForced);\n"
    contents += "\n"
    contents += "\t\tnlohmann::json savedRepresentation = deserialized;\n"
    contents += "\t\tQVERIFY(!savedRepresentation.contains(\"ui\") || !savedRepresentation[\"ui\"].contains(\"window_layout\"));\n"
    contents += "\t}\n"
    contents += "\n"
    contents += "\tvoid legacyWindowLayoutsMigrateToModern() {\n"
    contents += "\t\tconst QStringList cases = {\n"
    contents += "\t\t\tQStringLiteral(\"Classic\"),\n"
    contents += "\t\t\tQStringLiteral(\"Stacked\"),\n"
    contents += "\t\t\tQStringLiteral(\"Hybrid\"),\n"
    contents += "\t\t\tQStringLiteral(\"Custom\"),\n"
    contents += "\t\t};\n"
    contents += "\n"
    contents += "\t\tfor (const QString &current : cases) {\n"
    contents += "\t\t\tnlohmann::json jsonRepresentation;\n"
    contents += "\t\t\tjsonRepresentation[\"settings_version\"] = 1;\n"
    contents += "\t\t\tjsonRepresentation[\"ui\"][\"window_layout\"] = current.toStdString();\n"
    contents += "\n"
    contents += "\t\t\tconst " + settingsClassName + " deserialized = jsonRepresentation;\n"
    contents += "\t\t\tQCOMPARE(deserialized.wlWindowLayout, Settings::LayoutModern);\n"
    contents += "\t\t\tQCOMPARE(deserialized.modernLayoutPolicy, Settings::ModernLayoutForced);\n"
    contents += "\t\t}\n"
    contents += "\t}\n"
    contents += "\n"
    contents += "\tvoid retiredFrontendStateIsNotResaved() {\n"
    contents += "\t\t" + settingsClassName + " settings;\n"
    contents += "\t\tsettings.bLockLayout = true;\n"
    contents += "\t\tsettings.bMinimalView = true;\n"
    contents += "\t\tsettings.bHideFrame = true;\n"
    contents += "\t\tsettings.bAutoSwitchModernOnCompatibleServers = true;\n"
    contents += "\t\tsettings.bShowContextMenuInMenuBar = true;\n"
    contents += "\t\tsettings.bShowTransmitModeComboBox = true;\n"
    contents += "\t\tsettings.styleType = StyleType::Dark;\n"
    contents += "\t\tsettings.themeName = QStringLiteral(\"classic-theme\");\n"
    contents += "\t\tsettings.themeStyleName = QStringLiteral(\"classic-style\");\n"
    contents += "\t\tsettings.themeDarkName = QStringLiteral(\"classic-dark-theme\");\n"
    contents += "\t\tsettings.themeDarkStyleName = QStringLiteral(\"classic-dark-style\");\n"
    contents += "\t\tsettings.wlWindowLayout = Settings::LayoutClassic;\n"
    contents += "\t\tsettings.modernLayoutPolicy = Settings::ModernLayoutFollowLegacy;\n"
    contents += "\t\tsettings.qbaMainWindowGeometry = QByteArrayLiteral(\"classic-geometry\");\n"
    contents += "\t\tsettings.qbaMainWindowState = QByteArrayLiteral(\"classic-state\");\n"
    contents += "\t\tsettings.qbaMinimalViewGeometry = QByteArrayLiteral(\"minimal-geometry\");\n"
    contents += "\t\tsettings.qbaMinimalViewState = QByteArrayLiteral(\"minimal-state\");\n"
    contents += "\t\tsettings.qbaModernMinimalViewGeometry = QByteArrayLiteral(\"old-modern-minimal-geometry\");\n"
    contents += "\t\tsettings.qbaModernMainWindowState = QByteArrayLiteral(\"old-modern-state\");\n"
    contents += "\t\tsettings.qbaModernMinimalViewState = QByteArrayLiteral(\"old-modern-minimal-state\");\n"
    contents += "\t\tsettings.qbaConfigGeometry = QByteArrayLiteral(\"config-geometry\");\n"
    contents += "\t\tsettings.qbaImagePreviewGeometry = QByteArrayLiteral(\"preview-geometry\");\n"
    contents += "\t\tsettings.qbaConnectDialogGeometry = QByteArrayLiteral(\"connect-geometry\");\n"
    contents += "\t\tsettings.qbaConnectDialogHeader = QByteArrayLiteral(\"connect-header\");\n"
    contents += "\t\tsettings.searchDialogPosition = QPoint(42, 84);\n"
    contents += "\t\tsettings.qbaModernMainWindowGeometry = QByteArrayLiteral(\"qml-window-state\");\n"
    contents += "\n"
    contents += "\t\tconst nlohmann::json saved = settings;\n"
    contents += "\t\tQVERIFY(saved.contains(\"ui\"));\n"
    contents += "\t\tconst nlohmann::json &ui = saved.at(\"ui\");\n"
    contents += "\t\tconst QStringList retiredKeys = {\n"
    contents += "\t\t\tQStringLiteral(\"theme\"), QStringLiteral(\"theme_style\"),\n"
    contents += "\t\t\tQStringLiteral(\"theme_dark\"), QStringLiteral(\"theme_dark_style\"),\n"
    contents += "\t\t\tQStringLiteral(\"theme_method\"),\n"
    contents += "\t\t\tQStringLiteral(\"lock_layout\"), QStringLiteral(\"minimal_view\"),\n"
    contents += "\t\t\tQStringLiteral(\"hide_frame\"), QStringLiteral(\"window_geometry\"),\n"
    contents += "\t\t\tQStringLiteral(\"minimal_view_window_geometry\"), QStringLiteral(\"window_state\"),\n"
    contents += "\t\t\tQStringLiteral(\"minimal_view_window_state\"),\n"
    contents += "\t\t\tQStringLiteral(\"minimal_view_window_geometry_modern\"),\n"
    contents += "\t\t\tQStringLiteral(\"window_state_modern\"),\n"
    contents += "\t\t\tQStringLiteral(\"minimal_view_window_state_modern\"),\n"
    contents += "\t\t\tQStringLiteral(\"config_geometry\"), QStringLiteral(\"image_preview_geometry\"),\n"
    contents += "\t\t\tQStringLiteral(\"window_layout\"),\n"
    contents += "\t\t\tQStringLiteral(\"auto_switch_modern_on_compatible_servers\"),\n"
    contents += "\t\t\tQStringLiteral(\"display_context_menu_entries_in_menu_bar\"),\n"
    contents += "\t\t\tQStringLiteral(\"connect_dialog_geometry\"),\n"
    contents += "\t\t\tQStringLiteral(\"connect_dialog_header_state\"),\n"
    contents += "\t\t\tQStringLiteral(\"display_transmit_mode_combobox\")\n"
    contents += "\t\t};\n"
    contents += "\t\tfor (const QString &key : retiredKeys)\n"
    contents += "\t\t\tQVERIFY2(!ui.contains(key.toStdString()), qPrintable(key));\n"
    contents += "\t\tQVERIFY(ui.contains(\"window_geometry_modern\"));\n"
    contents += "\t\tQVERIFY(!saved.contains(\"dank_mumble\")\n"
    contents += "\t\t\t|| !saved.at(\"dank_mumble\").contains(\"modern_layout_policy\"));\n"
    contents += "\t\tQVERIFY(!saved.contains(\"search\")\n"
    contents += "\t\t\t|| !saved.at(\"search\").contains(\"search_window_position\"));\n"
    contents += "\n"
    contents += "\t\tconst " + settingsClassName + " loaded = saved;\n"
    contents += "\t\tQCOMPARE(loaded.qbaModernMainWindowGeometry, settings.qbaModernMainWindowGeometry);\n"
    contents += "\t\tQCOMPARE(loaded.wlWindowLayout, Settings::LayoutModern);\n"
    contents += "\t\tQCOMPARE(loaded.modernLayoutPolicy, Settings::ModernLayoutForced);\n"
    contents += "\t}\n"
    contents += "\n"
    contents += "\tvoid mixedLegacyJsonPreservesOnlyQmlState() {\n"
    contents += "\t\tnlohmann::json legacy;\n"
    contents += "\t\tlegacy[\"settings_version\"] = 1;\n"
    contents += "\t\tauto &ui = legacy[\"ui\"];\n"
    contents += "\t\tui[\"theme\"] = \"legacy-theme\";\n"
    contents += "\t\tui[\"theme_style\"] = \"legacy-style\";\n"
    contents += "\t\tui[\"theme_dark\"] = \"legacy-dark-theme\";\n"
    contents += "\t\tui[\"theme_dark_style\"] = \"legacy-dark-style\";\n"
    contents += "\t\tui[\"theme_method\"] = \"Dark\";\n"
    contents += "\t\tui[\"lock_layout\"] = true;\n"
    contents += "\t\tui[\"minimal_view\"] = true;\n"
    contents += "\t\tui[\"hide_frame\"] = true;\n"
    contents += "\t\tui[\"window_layout\"] = \"Classic\";\n"
    contents += "\t\tui[\"always_on_top\"] = \"InNormal\";\n"
    contents += "\t\tui[\"window_geometry\"] = \"Y2xhc3NpYw==\";\n"
    contents += "\t\tui[\"window_state\"] = \"Y2xhc3NpYw==\";\n"
    contents += "\t\tui[\"minimal_view_window_geometry\"] = \"bWluaW1hbA==\";\n"
    contents += "\t\tui[\"minimal_view_window_state\"] = \"bWluaW1hbA==\";\n"
    contents += "\t\tui[\"minimal_view_window_geometry_modern\"] = \"bWluaW1hbA==\";\n"
    contents += "\t\tui[\"window_state_modern\"] = \"bGVnYWN5LXN0YXRl\";\n"
    contents += "\t\tui[\"minimal_view_window_state_modern\"] = \"bWluaW1hbA==\";\n"
    contents += "\t\tui[\"config_geometry\"] = \"Y29uZmln\";\n"
    contents += "\t\tui[\"image_preview_geometry\"] = \"cHJldmlldw==\";\n"
    contents += "\t\tui[\"connect_dialog_geometry\"] = \"Y29ubmVjdA==\";\n"
    contents += "\t\tui[\"connect_dialog_header_state\"] = \"aGVhZGVy\";\n"
    contents += "\t\tui[\"auto_switch_modern_on_compatible_servers\"] = true;\n"
    contents += "\t\tui[\"display_context_menu_entries_in_menu_bar\"] = true;\n"
    contents += "\t\tui[\"display_transmit_mode_combobox\"] = true;\n"
    contents += "\t\tui[\"window_geometry_modern\"] = \"cW1sLWdlb21ldHJ5\";\n"
    contents += "\t\tui[\"modern_shell_theme\"] = \"mocha\";\n"
    contents += "\t\tui[\"modern_shell_density\"] = \"compact\";\n"
    contents += "\t\tui[\"modern_shell_rail_side\"] = \"left\";\n"
    contents += "\t\tui[\"modern_shell_accent\"] = \"purple\";\n"
    contents += "\t\tui[\"modern_shell_custom_accent\"] = \"#123456\";\n"
    contents += "\t\tui[\"modern_shell_custom_accent_strength\"] = 73;\n"
    contents += "\t\tlegacy[\"dank_mumble\"][\"modern_layout_policy\"] = \"FollowLegacy\";\n"
    contents += "\t\tlegacy[\"search\"][\"search_window_position\"] = { { \"x\", 42 }, { \"y\", 84 } };\n"
    contents += "\n"
    contents += "\t\tconst " + settingsClassName + " loaded = legacy;\n"
    contents += "\t\tQCOMPARE(loaded.wlWindowLayout, Settings::LayoutModern);\n"
    contents += "\t\tQCOMPARE(loaded.modernLayoutPolicy, Settings::ModernLayoutForced);\n"
    contents += "\t\tQCOMPARE(loaded.aotbAlwaysOnTop, Settings::OnTopAlways);\n"
    contents += "\t\tQCOMPARE(loaded.styleType, StyleType::Auto);\n"
    contents += "\t\tQVERIFY(loaded.themeName.isEmpty() && loaded.themeStyleName.isEmpty());\n"
    contents += "\t\tQVERIFY(loaded.themeDarkName.isEmpty() && loaded.themeDarkStyleName.isEmpty());\n"
    contents += "\t\tQVERIFY(!loaded.bLockLayout && !loaded.bMinimalView && !loaded.bHideFrame);\n"
    contents += "\t\tQVERIFY(!loaded.bAutoSwitchModernOnCompatibleServers);\n"
    contents += "\t\tQVERIFY(!loaded.bShowContextMenuInMenuBar && !loaded.bShowTransmitModeComboBox);\n"
    contents += "\t\tQVERIFY(loaded.qbaMainWindowGeometry.isEmpty() && loaded.qbaMainWindowState.isEmpty());\n"
    contents += "\t\tQVERIFY(loaded.qbaMinimalViewGeometry.isEmpty() && loaded.qbaMinimalViewState.isEmpty());\n"
    contents += "\t\tQVERIFY(loaded.qbaModernMinimalViewGeometry.isEmpty());\n"
    contents += "\t\tQVERIFY(loaded.qbaModernMainWindowState.isEmpty() && loaded.qbaModernMinimalViewState.isEmpty());\n"
    contents += "\t\tQVERIFY(loaded.qbaConfigGeometry.isEmpty() && loaded.qbaImagePreviewGeometry.isEmpty());\n"
    contents += "\t\tQVERIFY(loaded.qbaConnectDialogGeometry.isEmpty() && loaded.qbaConnectDialogHeader.isEmpty());\n"
    contents += "\t\tQCOMPARE(loaded.searchDialogPosition, Settings::UNSPECIFIED_POSITION);\n"
    contents += "\t\tQCOMPARE(loaded.qbaModernMainWindowGeometry, QByteArrayLiteral(\"qml-geometry\"));\n"
    contents += "\t\tQCOMPARE(loaded.qsModernShellTheme, QStringLiteral(\"mocha\"));\n"
    contents += "\t\tQCOMPARE(loaded.qsModernShellDensity, QStringLiteral(\"compact\"));\n"
    contents += "\t\tQCOMPARE(loaded.qsModernShellRailSide, QStringLiteral(\"left\"));\n"
    contents += "\t\tQCOMPARE(loaded.qsModernShellAccent, QStringLiteral(\"purple\"));\n"
    contents += "\t\tQCOMPARE(loaded.qsModernShellCustomAccent, QStringLiteral(\"#123456\"));\n"
    contents += "\t\tQCOMPARE(loaded.iModernShellCustomAccentStrength, 73);\n"
    contents += "\n"
    contents += "\t\tconst nlohmann::json saved = loaded;\n"
    contents += "\t\tQVERIFY(saved.at(\"ui\").contains(\"window_geometry_modern\"));\n"
    contents += "\t\tQVERIFY(!saved.at(\"ui\").contains(\"window_geometry\"));\n"
    contents += "\t\tQVERIFY(!saved.at(\"ui\").contains(\"window_layout\"));\n"
    contents += "\t\tQVERIFY(!saved.at(\"ui\").contains(\"theme\"));\n"
    contents += "\t\tQVERIFY(!saved.contains(\"search\") || !saved.at(\"search\").contains(\"search_window_position\"));\n"
    contents += "\t\tQVERIFY(!saved.contains(\"dank_mumble\") || !saved.at(\"dank_mumble\").contains(\"modern_layout_policy\"));\n"
    contents += "\t}\n"
    contents += "\n"
    contents += "\tvoid legacyAlwaysOnTopModesMigrate() {\n"
    contents += "\t\tconst QList<QPair<QString, Settings::AlwaysOnTopBehaviour>> cases = {\n"
    contents += "\t\t\t{ QStringLiteral(\"InMinimal\"), Settings::OnTopNever },\n"
    contents += "\t\t\t{ QStringLiteral(\"InNormal\"), Settings::OnTopAlways },\n"
    contents += "\t\t};\n"
    contents += "\t\tfor (const auto &current : cases) {\n"
    contents += "\t\t\tnlohmann::json jsonRepresentation;\n"
    contents += "\t\t\tjsonRepresentation[\"settings_version\"] = 1;\n"
    contents += "\t\t\tjsonRepresentation[\"ui\"][\"always_on_top\"] = current.first.toStdString();\n"
    contents += "\t\t\tconst " + settingsClassName + " loaded = jsonRepresentation;\n"
    contents += "\t\t\tQCOMPARE(loaded.aotbAlwaysOnTop, current.second);\n"
    contents += "\t\t}\n"
    contents += "\t}\n"
    contents += "\n"
    contents += "\tvoid explicitLegacyConfigMigratesFrontendState() {\n"
    contents += "\t\tQTemporaryDir directory;\n"
    contents += "\t\tQVERIFY(directory.isValid());\n"
    contents += "\t\tconst QString path = directory.filePath(QStringLiteral(\"legacy.ini\"));\n"
    contents += "\t\t{\n"
    contents += "\t\t\tQSettings legacy(path, QSettings::IniFormat);\n"
    contents += "\t\t\tlegacy.setValue(QStringLiteral(\"ui/minimalview\"), true);\n"
    contents += "\t\t\tlegacy.setValue(QStringLiteral(\"ui/hideframe\"), true);\n"
    contents += "\t\t\tlegacy.setValue(QStringLiteral(\"ui/alwaysontop\"), static_cast<int>(Settings::OnTopInNormal));\n"
    contents += "\t\t\tlegacy.setValue(QStringLiteral(\"ui/geometry\"), QByteArrayLiteral(\"classic\"));\n"
    contents += "\t\t\tlegacy.setValue(QStringLiteral(\"ui/modernShellTheme\"), QStringLiteral(\"mocha\"));\n"
    contents += "\t\t\tlegacy.setValue(QStringLiteral(\"ui/modernShellDensity\"), QStringLiteral(\"compact\"));\n"
    contents += "\t\t\tlegacy.setValue(QStringLiteral(\"ui/modernShellAccent\"), QStringLiteral(\"purple\"));\n"
    contents += "\t\t\tlegacy.setValue(QStringLiteral(\"ui/pttbuttonwindowgeometry\"), QByteArrayLiteral(\"ptt-qml\"));\n"
    contents += "\t\t\tlegacy.setValue(QStringLiteral(\"search/search_dialog_position\"), QPoint(42, 84));\n"
    contents += "\t\t\tlegacy.sync();\n"
    contents += "\t\t\tQCOMPARE(legacy.status(), QSettings::NoError);\n"
    contents += "\t\t}\n"
    contents += "\t\t" + settingsClassName + " loaded;\n"
    contents += "\t\tloaded.load(path, true);\n"
    contents += "\t\tQCOMPARE(loaded.wlWindowLayout, Settings::LayoutModern);\n"
    contents += "\t\tQCOMPARE(loaded.modernLayoutPolicy, Settings::ModernLayoutForced);\n"
    contents += "\t\tQCOMPARE(loaded.aotbAlwaysOnTop, Settings::OnTopAlways);\n"
    contents += "\t\tQVERIFY(!loaded.bMinimalView);\n"
    contents += "\t\tQVERIFY(!loaded.bHideFrame);\n"
    contents += "\t\tQVERIFY(loaded.qbaMainWindowGeometry.isEmpty());\n"
    contents += "\t\tQCOMPARE(loaded.searchDialogPosition, Settings::UNSPECIFIED_POSITION);\n"
    contents += "\t\tQCOMPARE(loaded.qsModernShellTheme, QStringLiteral(\"mocha\"));\n"
    contents += "\t\tQCOMPARE(loaded.qsModernShellDensity, QStringLiteral(\"compact\"));\n"
    contents += "\t\tQCOMPARE(loaded.qsModernShellAccent, QStringLiteral(\"purple\"));\n"
    contents += "\t\tQCOMPARE(loaded.qbaPTTButtonWindowGeometry, QByteArrayLiteral(\"ptt-qml\"));\n"
    contents += "\t}\n"
    contents += "\n"
    contents += "\tvoid unknownWindowLayoutFallsBackToDefault() {\n"
    contents += "\t\tnlohmann::json jsonRepresentation;\n"
    contents += "\t\tjsonRepresentation[\"settings_version\"] = 1;\n"
    contents += "\t\tjsonRepresentation[\"ui\"][\"window_layout\"] = \"FutureLayout\";\n"
    contents += "\n"
    contents += "\t\tconst " + settingsClassName + " defaults;\n"
    contents += "\t\tconst " + settingsClassName + " deserialized = jsonRepresentation;\n"
    contents += "\t\tQCOMPARE(deserialized.wlWindowLayout, defaults.wlWindowLayout);\n"
    contents += "\t}\n"
    contents += "};\n"
    contents += "\n"
    contents += "QTEST_MAIN(TestSettingsJSONSerialization)\n"
    contents += "#include \"TestSettingsJSONSerialization.moc\"\n"

    return contents


def generateDefaultConstruction(structName, fields):
    contents =  structName + "{"
    for currentField in fields:
        contents += "/*" + currentField + "*/ " + getDefaultValueForType(fields[currentField]) + ","

    if len(fields) > 0:
        # remove trailing comma
        contents = contents[:-1]

    return contents + "}"



def main():
    parser = argparse.ArgumentParser("Generates the test case for the JSON (de)serialization of the settings struct")
    parser.add_argument("--settings-header", help="The path to the header file containing the definition of the Settings struct", metavar="PATH",
            required = True)
    parser.add_argument("--settings-struct-name", help="The name of the settings struct", default="Settings")
    parser.add_argument("--ignore-fields", help="A comma-separated list of fields in the settings struct to exclude from the test", default="kpCertificate")
    parser.add_argument("--output-file", help="Path to where the output shall be written. If none is given, the result is written to stdout", metavar="PATH")

    args = parser.parse_args()

    headerContents = open(args.settings_header, "r").read()

    for currentMatch in re.finditer(r"struct\s*(\w+)\s\{", headerContents):
        definition = extractClassDefinition(currentMatch.group(1), headerContents[currentMatch.start() : ], classIdentifier="struct")

        fields = extractFields(definition)

        defaultValues[currentMatch.group(1)] = generateDefaultConstruction(currentMatch.group(1), fields)

        structs[currentMatch.group(1)] = fields

    ignoredFields = args.ignore_fields.split(",")
    ignoredFields = [x.strip() for x in ignoredFields]

    contents = generateTestBody(structs[args.settings_struct_name], args.settings_struct_name, excludeFields=ignoredFields)

    if args.output_file:
        outFile = open(args.output_file, "w")
        outFile.write(contents)
    else:
        print(contents)




if __name__ == "__main__":
    main()
