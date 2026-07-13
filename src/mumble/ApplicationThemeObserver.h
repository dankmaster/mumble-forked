// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_APPLICATIONTHEMEOBSERVER_H_
#define MUMBLE_APPLICATIONTHEMEOBSERVER_H_

#include <QObject>

class QEvent;

/// Observes application-wide palette and theme changes without creating a
/// compatibility widget. QML observes the same events through its theme
/// controller; this observer keeps allowlisted native windows in sync.
class ApplicationThemeObserver final : public QObject {
	Q_OBJECT

public:
	explicit ApplicationThemeObserver(QObject *parent = nullptr);
	~ApplicationThemeObserver() override;

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	void scheduleNativeThemeRefresh();

	bool m_refreshPending = false;
};

#endif
