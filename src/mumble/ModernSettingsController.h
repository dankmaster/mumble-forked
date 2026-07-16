// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_MODERNSETTINGSCONTROLLER_H_
#define MUMBLE_MUMBLE_MODERNSETTINGSCONTROLLER_H_

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
	ModernSettingsController();
	~ModernSettingsController();

	struct ActionResult {
		bool stateChanged = true;
		bool closeDialog  = false;
		std::optional< Settings > settingsToApply;
		bool accepted = false;
	};

	void open(const Settings &settings, const QString &pageName = QString());
	QVariantMap state() const;
	void updateField(const QString &fieldID, const QVariant &value);
	ActionResult invokeAction(const QString &actionID, const QVariantMap &payload);

	const Settings &draft() const;
	QString activePage() const;

private:
	Settings m_original;
	Settings m_draft;
	QString m_activePage = QStringLiteral("look");
	int m_shortcutCaptureIndex = -1;
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
	void forceModernLayout();
	void refreshShortcutRestartFlag();
	void cancelInputEnhancementCalibration();
};

#endif // MUMBLE_MUMBLE_MODERNSETTINGSCONTROLLER_H_
