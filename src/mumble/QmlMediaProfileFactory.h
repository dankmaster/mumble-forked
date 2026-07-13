// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef MUMBLE_MUMBLE_QMLMEDIAPROFILEFACTORY_H_
#define MUMBLE_MUMBLE_QMLMEDIAPROFILEFACTORY_H_

#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QUrl>
#include <QtWebEngineQuick/QQuickWebEngineProfile>

class MediaRequestInterceptor;
class MediaSessionBackend;

class QmlMediaProfileFactory final : public QObject {
	Q_OBJECT
	Q_PROPERTY(QQuickWebEngineProfile *videoProfile READ videoProfile NOTIFY profilesChanged)
	Q_PROPERTY(QQuickWebEngineProfile *audioProfile READ audioProfile NOTIFY profilesChanged)

public:
	explicit QmlMediaProfileFactory(MediaSessionBackend *session, QObject *parent = nullptr);
	~QmlMediaProfileFactory() override;

	QQuickWebEngineProfile *videoProfile();
	QQuickWebEngineProfile *audioProfile();

	static bool isResourceRequestAllowed(const QString &provider, const QUrl &primaryUrl,
										 const QUrl &audioUrl, const QUrl &requestUrl,
										 const QUrl &firstPartyUrl = {});

signals:
	void profilesChanged();

private:
	QQuickWebEngineProfile *createProfile(bool audio);
	void updatePolicies();
	void releaseProfiles();

	MediaSessionBackend *m_session = nullptr;
	QPointer< QQuickWebEngineProfile > m_videoProfile;
	QPointer< QQuickWebEngineProfile > m_audioProfile;
	MediaRequestInterceptor *m_videoInterceptor = nullptr;
	MediaRequestInterceptor *m_audioInterceptor = nullptr;
};

#endif // MUMBLE_MUMBLE_QMLMEDIAPROFILEFACTORY_H_
