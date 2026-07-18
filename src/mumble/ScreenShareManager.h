// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_SCREENSHAREMANAGER_H_
#define MUMBLE_MUMBLE_SCREENSHAREMANAGER_H_

#include "Mumble.pb.h"
#include "ScreenShareHelperClient.h"
#include "ScreenShareOperationTracker.h"

#include <QtCore/QHash>
#include <QtCore/QList>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QSet>
#include <QtCore/QString>
#include <QtCore/QTimer>
#include <QtCore/QThreadPool>

struct ScreenShareSession {
	QString streamID;
	unsigned int ownerSession = 0;
	MumbleProto::ScreenShareScope scope = MumbleProto::ScreenShareScopeChannel;
	unsigned int scopeID = 0;
	QString relayUrl;
	QString relayRoomID;
	QString relayToken;
	QString relaySessionID;
	MumbleProto::ScreenShareRelayTransport relayTransport = MumbleProto::ScreenShareRelayTransportUnknown;
	MumbleProto::ScreenShareRelayRole relayRole = MumbleProto::ScreenShareRelayRoleViewer;
	quint64 relayTokenExpiresAt = 0;
	quint64 createdAt = 0;
	MumbleProto::ScreenShareLifecycleState state = MumbleProto::ScreenShareLifecycleStatePending;
	MumbleProto::ScreenShareCodec codec = MumbleProto::ScreenShareCodecUnknown;
	QList< int > codecFallbackOrder;
	unsigned int width = 0;
	unsigned int height = 0;
	unsigned int fps = 0;
	unsigned int bitrateKbps = 0;
	QString qualityProfile = QStringLiteral("auto");
	QString captureSourceID;
	bool captureAudio = false;
	QString audioSourceID;
	unsigned int minBitrateKbps = 0;
	unsigned int maxBitrateKbps = 0;
};

struct ScreenShareStartOptions {
	QString captureSourceID;
	bool captureAudio = false;
	QString audioSourceID;
	unsigned int requestedWidth  = 0;
	unsigned int requestedHeight = 0;
	unsigned int requestedFps    = 0;
	QString qualityProfile;
};

class ScreenShareViewBackend;

class ScreenShareManager : public QObject {
private:
	enum class HelperOperationKind { StartPublish, StartView, StopPublish, StopView };
	struct HelperOperationResult {
		HelperOperationKind kind;
		QString streamID;
		quint64 generation = 0;
		bool success = false;
		QString error;
		qint64 processID = 0;
		ScreenShareHelperClient::NativeFrameTransport frameTransport;
	};
	Q_OBJECT
	Q_DISABLE_COPY(ScreenShareManager)

public:
	explicit ScreenShareManager(QObject *parent = nullptr);

	static ScreenShareHelperClient::CapabilitySnapshot detectAdvertisedCapabilities();

	ScreenShareHelperClient &helperClient();
	const ScreenShareHelperClient &helperClient() const;
	void logLocalShareAvailabilityDiagnostic(const QString &context = QString()) const;

	bool canRequestLocalShare() const;
	// Returns whether the foreground share setup may be opened. Unlike
	// canRequestLocalShare(), this deliberately permits the lazy capability
	// probe that is started by opening the picker.
	bool canOpenLocalShareSetup() const;
	QString localShareUnavailableReason() const;
	bool canViewSession(const QString &streamID) const;
	bool isPublishingSession(const QString &streamID) const;
	bool isPublishingSessionPending(const QString &streamID) const;
	bool isViewingSession(const QString &streamID) const;
	bool isViewingSessionPending(const QString &streamID) const;
	bool hasDetachedWindow(const QString &streamID) const;
	bool hasRunningExternalRuntime(const QString &streamID) const;
	bool focusOrReopenDetachedWindow(const QString &streamID);
	bool isUsingFallbackRuntime(const QString &streamID) const;
	bool isUsingNativeGpuRuntime(const QString &streamID) const;
	void requestStartChannelShare(unsigned int channelID = 0,
								  const ScreenShareStartOptions &options = ScreenShareStartOptions());
	void requestStartViewing(const QString &streamID);
	void requestStopViewing(const QString &streamID);
	void requestStopShare(const QString &streamID);

	const QHash< QString, ScreenShareSession > &sessions() const;
	bool hasSession(const QString &streamID) const;

public slots:
	void resetState();
	void handleScreenShareState(const MumbleProto::ScreenShareState &msg);
	void handleScreenShareOffer(const MumbleProto::ScreenShareOffer &msg);
	void handleScreenShareAnswer(const MumbleProto::ScreenShareAnswer &msg);
	void handleScreenShareIceCandidate(const MumbleProto::ScreenShareIceCandidate &msg);
	void handleScreenShareStop(const MumbleProto::ScreenShareStop &msg);

signals:
	void sessionUpdated(const QString &streamID);
	void sessionStopped(const QString &streamID);

private:
	ScreenShareSession sessionFromState(const MumbleProto::ScreenShareState &msg) const;
	void reconcileSession(const ScreenShareSession &session);
	void reconcileSessionsAfterCapabilityRefresh();
	bool canViewSession(const ScreenShareSession &session) const;
	bool canPublishSession(const ScreenShareSession &session) const;
	bool shouldAutoViewSession(const ScreenShareSession &session) const;
	bool restartExternalViewSession(const ScreenShareSession &session);
	void retryExternalViewSession(const QString &streamID);
	void showExternalViewWindow(const ScreenShareSession &session, qint64 processID);
	void updateViewBackendIdentity(ScreenShareViewBackend *backend, const ScreenShareSession &session) const;
	void startLocalPublishSession(const ScreenShareSession &session);
	void startLocalViewSession(const ScreenShareSession &session);
	void stopLocalPublishSession(const QString &streamID);
	void stopLocalViewSession(const QString &streamID);
	void updateExternalRuntimeWatchdog();
	void checkExternalRuntimeLiveness();
	void setExternalViewAudioMuted(const QString &streamID, bool muted);
	void setExternalViewAudioVolume(const QString &streamID, int percent);
	void setExternalViewPaused(const QString &streamID, bool paused);
	QString externalViewAudioPreferenceKey(const ScreenShareSession &session) const;
	void scheduleExternalViewAudioPreferenceSave(const QString &streamID);
	void persistExternalViewAudioPreference(const QString &streamID);
	void flushExternalViewAudioPreferenceSaves();
	void logRemoteViewAvailability(const ScreenShareSession &session);
	void stopLocalHelperSessions(const QString &streamID);
	void notifyServerShareStopped(const QString &streamID);
	quint64 nextHelperOperationGeneration(HelperOperationKind kind, const QString &streamID);
	void scheduleHelperOperation(HelperOperationKind kind, const ScreenShareSession &session, quint64 generation);
	void applyHelperOperationResult(const HelperOperationResult &result);

	ScreenShareHelperClient *m_helperClient;
	QHash< QString, ScreenShareSession > m_sessions;
	QSet< QString > m_activePublishSessions;
	QSet< QString > m_pendingPublishSessions;
	QSet< QString > m_activeViewSessions;
	QSet< QString > m_pendingViewSessions;
	QSet< QString > m_pendingPublishRestarts;
	QSet< QString > m_pendingViewRestarts;
	QSet< QString > m_locallyTerminatedPublishSessions;
	QHash< QString, qint64 > m_externalPublishProcessIDs;
	QHash< QString, qint64 > m_externalViewProcessIDs;
	QHash< QString, int > m_externalPublishRestartAttempts;
	QHash< QString, int > m_externalViewRestartAttempts;
	QHash< QString, ScreenShareViewBackend * > m_viewBackends;
	QHash< QString, QPointer< QObject > > m_qmlViewWindows;
	QSet< QString > m_externalViewAudioMuted;
	QHash< QString, QString > m_externalViewAudioPreferenceKeys;
	QSet< QString > m_pendingExternalViewAudioPreferenceSaves;
	QTimer m_externalViewAudioPreferenceSaveTimer;
	QSet< QString > m_pausedExternalViewSessions;
	QSet< QString > m_manualViewRetryRequired;
	QSet< QString > m_announcedViewableSessions;
	QTimer m_externalRuntimeWatchdogTimer;
	mutable QString m_lastLoggedAvailabilityContext;
	mutable QString m_lastLoggedAvailabilityReason;
	ScreenShareOperationTracker m_publishOperationTracker;
	ScreenShareOperationTracker m_viewOperationTracker;
	QHash< QString, QPair< quint64, int > > m_publishStopRetries;
	QHash< QString, QPair< quint64, int > > m_viewStopRetries;
	QThreadPool m_helperOperationPool;
};

#endif // MUMBLE_MUMBLE_SCREENSHAREMANAGER_H_
