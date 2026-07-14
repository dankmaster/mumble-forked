// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef MUMBLE_MUMBLE_QMLVISUALFIXTURECONTROLLER_H_
#define MUMBLE_MUMBLE_QMLVISUALFIXTURECONTROLLER_H_

#include <QtCore/QString>
#include <QtCore/QVariantMap>

class QmlShellHost;

class QmlVisualFixtureController {
public:
	explicit QmlVisualFixtureController(QmlShellHost *host);
	void setHost(QmlShellHost *host) { m_host = host; }

	QVariantMap capabilities() const;
	QVariantMap apply(const QVariantMap &request, QString *error = nullptr);
	qulonglong generation() const { return m_generation; }
	double actualDevicePixelRatio() const;

private:
	bool waitForPresentedFrame(QString *error);
	void applyState(const QString &state, const QString &motdVariant, const QString &richPreviewVariant,
					const QString &richPreviewSize);

	QmlShellHost *m_host = nullptr;
	qulonglong m_generation = 0;
};

#endif // MUMBLE_MUMBLE_QMLVISUALFIXTURECONTROLLER_H_
