// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef MUMBLE_MUMBLE_MANUALPLUGINCONTROLLER_H_
#define MUMBLE_MUMBLE_MANUALPLUGINCONTROLLER_H_

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QVariantList>

class QTimer;

class ManualPluginController final : public QObject {
	Q_OBJECT
	Q_PROPERTY(double x READ x WRITE setX NOTIFY stateChanged)
	Q_PROPERTY(double y READ y WRITE setY NOTIFY stateChanged)
	Q_PROPERTY(double z READ z WRITE setZ NOTIFY stateChanged)
	Q_PROPERTY(int azimuth READ azimuth WRITE setAzimuth NOTIFY stateChanged)
	Q_PROPERTY(int elevation READ elevation WRITE setElevation NOTIFY stateChanged)
	Q_PROPERTY(QString context READ context WRITE setContext NOTIFY stateChanged)
	Q_PROPERTY(QString identity READ identity WRITE setIdentity NOTIFY stateChanged)
	Q_PROPERTY(int staleSeconds READ staleSeconds WRITE setStaleSeconds NOTIFY stateChanged)
	Q_PROPERTY(bool active READ active WRITE setActive NOTIFY stateChanged)
	Q_PROPERTY(bool linked READ linked WRITE setLinked NOTIFY stateChanged)
	Q_PROPERTY(QVariantList speakers READ speakers NOTIFY speakersChanged)

public:
	explicit ManualPluginController(QObject *parent = nullptr);

	double x() const;
	double y() const;
	double z() const;
	int azimuth() const;
	int elevation() const;
	QString context() const;
	QString identity() const;
	int staleSeconds() const;
	bool active() const;
	bool linked() const;
	QVariantList speakers() const;

	void setX(double value);
	void setY(double value);
	void setZ(double value);
	void setAzimuth(int value);
	void setElevation(int value);
	void setContext(const QString &value);
	void setIdentity(const QString &value);
	void setStaleSeconds(int value);
	void setActive(bool value);
	void setLinked(bool value);

	Q_INVOKABLE void refresh();
	Q_INVOKABLE void setSpeakerUpdatesEnabled(bool enabled);
	Q_INVOKABLE void apply();
	Q_INVOKABLE void reset();

signals:
	void stateChanged();
	void speakersChanged();
	void applied();
	void resetCompleted();

private:
	double m_x = 0.0;
	double m_y = 0.0;
	double m_z = 0.0;
	int m_azimuth = 0;
	int m_elevation = 0;
	QString m_context;
	QString m_identity;
	int m_staleSeconds = 0;
	bool m_active = true;
	bool m_linked = false;
	QVariantList m_speakers;
	QTimer *m_speakerRefreshTimer = nullptr;
};

#endif // MUMBLE_MUMBLE_MANUALPLUGINCONTROLLER_H_
