// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_MODERNSETTINGSCONTROLLER_H_
#define MUMBLE_MUMBLE_MODERNSETTINGSCONTROLLER_H_

#include "Settings.h"

#include <QtCore/QString>
#include <QtCore/QVariant>

#include <optional>

class ModernSettingsController {
public:
	struct ActionResult {
		bool stateChanged = true;
		bool closeDialog  = false;
		std::optional< Settings > settingsToApply;
		bool accepted = false;
		QString externalActionID;
		QVariantMap externalActionPayload;
	};

	void open(const Settings &settings, const QString &pageName = QString());
	QVariantMap state() const;
	void updateField(const QString &fieldID, const QVariant &value);
	ActionResult invokeAction(const QString &actionID, const QVariantMap &payload);
	/// Reconciles externally completed plugin load/unload operations with both the current draft and
	/// its reset baseline. This keeps an already open settings dialog from presenting a state that the
	/// plugin manager could not actually reach.
	bool reconcilePluginLoadedState(const QString &pluginPath, bool loaded);

	const Settings &draft() const;
	QString activePage() const;

private:
	Settings m_original;
	Settings m_draft;
	QString m_activePage = QStringLiteral("look");
	int m_shortcutCaptureIndex = -1;

	QVariantList pages() const;
	QVariantList sectionsForActivePage() const;
	void setActivePage(const QString &pageID);
	void forceModernLayout();
	void refreshShortcutRestartFlag();
};

#endif // MUMBLE_MUMBLE_MODERNSETTINGSCONTROLLER_H_
