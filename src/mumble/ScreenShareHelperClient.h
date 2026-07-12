// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_SCREENSHAREHELPERCLIENT_H_
#define MUMBLE_MUMBLE_SCREENSHAREHELPERCLIENT_H_

#include "Mumble.pb.h"
#include "ScreenShareIPC.h"

#include <QtCore/QJsonObject>
#include <QtCore/QList>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>

struct ScreenShareSession;

class ScreenShareHelperClient : public QObject {
private:
	Q_OBJECT
	Q_DISABLE_COPY(ScreenShareHelperClient)

public:
	struct NativeFrameTransport {
		QString sharedMemoryKey;
		quint64 generation = 0;
		bool feedAvailable = false;
	};
	struct CapabilitySnapshot {
		bool supportsSignaling       = true;
		bool probeComplete           = false;
		bool helperAvailable         = false;
		bool captureSupported        = false;
		bool viewSupported           = false;
		bool hardwareEncodeSupported = false;
		bool hardwareDecodeSupported = false;
		bool zeroCopySupported       = false;
		bool roiSupported            = false;
		bool damageMetadataSupported = false;
		bool gstreamerAvailable      = false;
		bool gstreamerLiveKitPublishAvailable = false;
		bool gstreamerLiveKitViewAvailable    = false;
		QList< int > supportedCodecs;
		QList< int > runtimeRelayTransports;
		unsigned int maxWidth  = 0;
		unsigned int maxHeight = 0;
		unsigned int maxFps    = 0;
		QString helperExecutable;
		QString captureBackend;
		QStringList captureBackends;
		QString gstreamerVersion;
		QStringList missingGStreamerElements;
		QStringList supportedIngestProtocols;
		QStringList drmSystems;
		unsigned int queueBudgetFrames = 0;
	};

	explicit ScreenShareHelperClient(QObject *parent = nullptr);

	static CapabilitySnapshot initialCapabilitySnapshot();
	static CapabilitySnapshot advertisedCapabilities();
	static CapabilitySnapshot detectLocalCapabilities();
	static void applyAdvertisedCapabilities(MumbleProto::Version &msg);

	const CapabilitySnapshot &capabilities() const;
	bool startPublish(const ScreenShareSession &session, QString *errorMessage = nullptr, qint64 *processID = nullptr);
	bool stopPublish(const QString &streamID, QString *errorMessage = nullptr);
	bool startView(const ScreenShareSession &session, QString *errorMessage = nullptr, qint64 *processID = nullptr,
				   NativeFrameTransport *frameTransport = nullptr);
	bool stopView(const QString &streamID, QString *errorMessage = nullptr);

public slots:
	void refreshCapabilities();

signals:
	void capabilitiesChanged();

private:
	static QString defaultHelperExecutablePath();
	static QString diagnosticsLogPath();
	static QStringList helperLaunchArguments();
	static CapabilitySnapshot capabilitySnapshotFromPayload(const QJsonObject &payload,
															const QString &helperExecutable);
	static QJsonObject payloadFromSession(const ScreenShareSession &session);
	static void logReplyWarnings(const QJsonObject &reply, Mumble::ScreenShare::IPC::Command command,
								 const QString &streamID = QString());
	static QJsonObject sendRequest(Mumble::ScreenShare::IPC::Command command, const QJsonObject &payload,
								   const QString &helperExecutable, QString *errorMessage, bool launchIfNeeded = true);
	static bool ensureHelperRunning(const QString &helperExecutable, QString *errorMessage = nullptr);
	static void cacheAdvertisedCapabilities(const CapabilitySnapshot &snapshot);

	CapabilitySnapshot m_capabilities;
	bool m_capabilityRefreshInProgress = false;
};

#endif // MUMBLE_MUMBLE_SCREENSHAREHELPERCLIENT_H_
