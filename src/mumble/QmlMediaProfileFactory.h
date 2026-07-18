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
	// Keep the public meta-object boundary frontend-neutral. A typed
	// QQuickWebEngineProfile property creates a PE data import for
	// QQuickWebEngineProfile::staticMetaObject, which cannot be delay-loaded on
	// Windows. QML still receives the concrete profile instance and validates it
	// when assigning WebEngineView.profile after the media component is loaded.
	Q_PROPERTY(QObject *videoProfile READ videoProfile NOTIFY profilesChanged)
	Q_PROPERTY(QObject *audioProfile READ audioProfile NOTIFY profilesChanged)
	Q_PROPERTY(QUrl videoDocumentUrl READ videoDocumentUrl NOTIFY documentUrlChanged)
	Q_PROPERTY(bool runtimeReady READ runtimeReady NOTIFY runtimeStateChanged)
	Q_PROPERTY(bool runtimePreparing READ runtimePreparing NOTIFY runtimeStateChanged)
	Q_PROPERTY(QString runtimeError READ runtimeError NOTIFY runtimeStateChanged)

public:
	explicit QmlMediaProfileFactory(MediaSessionBackend *session, QObject *parent = nullptr);
	~QmlMediaProfileFactory() override;

	QObject *videoProfile();
	QObject *audioProfile();
	QUrl videoDocumentUrl() const;
	bool runtimeReady() const;
	bool runtimePreparing() const;
	QString runtimeError() const;
	Q_INVOKABLE bool isNavigationRequestAllowed(const QUrl &requestUrl, const QUrl &firstPartyUrl) const;
	Q_INVOKABLE void retryRuntime();

	static bool isResourceRequestAllowed(const QString &provider, const QUrl &primaryUrl,
										 const QUrl &audioUrl, const QUrl &requestUrl,
										 const QUrl &firstPartyUrl = {}, const QString &mediaMime = {});

signals:
	void profilesChanged();
	void documentUrlChanged();
	void runtimeStateChanged();

private:
	QQuickWebEngineProfile *createProfile(bool audio);
	void prepareRuntime();
	void updatePolicies();
	void releaseProfiles();

	MediaSessionBackend *m_session = nullptr;
	QPointer< QQuickWebEngineProfile > m_videoProfile;
	QPointer< QQuickWebEngineProfile > m_audioProfile;
	MediaRequestInterceptor *m_videoInterceptor = nullptr;
	MediaRequestInterceptor *m_audioInterceptor = nullptr;
	bool m_runtimeReady = false;
	bool m_runtimePreparing = false;
	QString m_runtimeError;
};

#endif // MUMBLE_MUMBLE_QMLMEDIAPROFILEFACTORY_H_
