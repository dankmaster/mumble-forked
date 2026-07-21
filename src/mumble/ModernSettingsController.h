// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_MODERNSETTINGSCONTROLLER_H_
#define MUMBLE_MUMBLE_MODERNSETTINGSCONTROLLER_H_

#include "ModernTheme.h"
#include "Settings.h"

#include <QtCore/QString>
#include <QtCore/QVariant>

#include <memory>
#include <optional>

namespace Mumble::InputEnhancement {
class CalibrationEvaluationWorker;
}

class ModernSettingsController {
public:
	struct AppearancePreview {
		QString theme;
		QString density;
		QString accent;
		QString customAccent;
		int customAccentStrength = 50;
	};

	ModernSettingsController();
	~ModernSettingsController();

	struct ActionResult {
		bool stateChanged = true;
		bool closeDialog  = false;
		std::optional< Settings > settingsToApply;
		std::optional< AppearancePreview > appearanceToPreview;
		bool accepted = false;
		bool announceApply = true;
		QString externalActionID;
		QVariantMap externalActionPayload;
	};

	void open(const Settings &settings, const QString &pageName = QString(), bool audioInputOnboarding = false,
			  const QVariantMap &stonksContext = QVariantMap(), const QVariantMap &motdContext = QVariantMap());
	QVariantMap state() const;
	void updateField(const QString &fieldID, const QVariant &value);
	ActionResult invokeAction(const QString &actionID, const QVariantMap &payload);
	void setStonksContext(const QVariantMap &stonksContext);
	void setMotdContext(const QVariantMap &motdContext);
	bool setMotdPreview(const QString &sourceHtml, const QVariantList &blocks, const QString &summary);
	/// Reconciles externally completed plugin load/unload operations with both the current draft and
	/// its reset baseline. This keeps an already open settings dialog from presenting a state that the
	/// plugin manager could not actually reach.
	bool reconcilePluginLoadedState(const QString &pluginPath, bool loaded);

	const Settings &draft() const;
	QString activePage() const;

private:
	Settings m_original;
	Settings m_draft;
	QVariantMap m_stonksContext;
	QVariantMap m_motdContext;
	QList< Mumble::ModernTheme::ThemeDefinition > m_modernThemeCatalog;
	QString m_activePage = QStringLiteral("look");
	int m_shortcutCaptureIndex = -1;
	std::optional< Settings::AudioTransmit > m_voiceReplayPreviousTransmitMode;
	std::optional< Settings::LoopMode > m_voiceReplayPreviousLoopMode;
	std::optional< float > m_voiceReplayPreviousPacketLoss;
	std::optional< float > m_voiceReplayPreviousMaxPacketDelay;
	bool m_runtimePreviewDiffersFromOriginal = false;
	bool m_appearancePreviewActive = false;
	bool m_audioInputOnboarding = false;
	std::unique_ptr< Mumble::InputEnhancement::CalibrationEvaluationWorker >
		m_inputEnhancementCalibrationWorker;
	std::optional< Mumble::InputEnhancement::DefaultPreference >
		m_inputEnhancementCalibrationControls;
	std::optional< Mumble::InputEnhancement::DefaultPreference >
		m_inputEnhancementPreAutoPreference;
	QString m_inputEnhancementCalibrationUiError;
	QString m_inputEnhancementReadinessUiError;

	QVariantList pages() const;
	QVariantList sectionsForActivePage() const;
	void setActivePage(const QString &pageID);
	void refreshModernThemeCatalog();
	void forceModernLayout();
	void refreshShortcutRestartFlag();
	void restoreVoiceReplayDraft();
	void cancelInputEnhancementCalibration();
};

#endif // MUMBLE_MUMBLE_MODERNSETTINGSCONTROLLER_H_
