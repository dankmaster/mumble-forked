// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_SERVERHANDLER_H_
#define MUMBLE_MUMBLE_SERVERHANDLER_H_

#include <QtCore/QtGlobal>

#ifdef Q_OS_WIN
#	include "win.h"
#endif

#ifndef Q_MOC_RUN
#	include <boost/accumulators/accumulators.hpp>
#	include <boost/accumulators/statistics/mean.hpp>
#	include <boost/accumulators/statistics/stats.hpp>
#	include <boost/accumulators/statistics/variance.hpp>
#endif

#include <QtCore/QEvent>
#include <QtCore/QList>
#include <QtCore/QMutex>
#include <QtCore/QObject>
#include <QtCore/QPair>
#include <QtCore/QReadWriteLock>
#include <QtCore/QStringList>
#include <QtCore/QThread>
#include <QtCore/QTimer>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QSslCertificate>
#include <QtNetwork/QSslCipher>
#include <QtNetwork/QSslConfiguration>
#include <QtNetwork/QSslError>
#include <QtNetwork/QSslKey>

#define SERVERSEND_EVENT 3501

#include "Mumble.pb.h"
#include "MumbleProtocol.h"
#include "ServerAddress.h"
#include "Timer.h"

#include <atomic>
#include <memory>
#include <optional>
#include <vector>

class Connection;
class Database;
class PacketDataStream;
class QUdpSocket;
class QSslSocket;
class VoiceRecorder;

class ServerHandlerMessageEvent : public QEvent {
public:
	Mumble::Protocol::TCPMessageType type;
	QByteArray qbaMsg;
	bool bFlush;
	ServerHandlerMessageEvent(const QByteArray &msg, Mumble::Protocol::TCPMessageType type, bool flush = false);
};

enum class ServerHandlerState {
	Idle,
	DNSQuery,
	DNSResolved,
	DNSFailed,
	AwaitingConnection,
	TLSHandshake,
	ConnectionEstablished,
	Disconnecting,
	ConnectionOver,
	Aborted
};

using ConnectionPtr = std::shared_ptr< Connection >;

/// Immutable-by-convention value returned to UI-side consumers. The handler
/// may replace its TLS state while a connection attempt is being torn down, so
/// callers must retain this snapshot instead of referencing handler-owned Qt
/// containers directly.
struct ServerTlsDetails {
	QList< QSslError > errors;
	QList< QSslCertificate > certificates;
	QSslCipher cipher;
	QSsl::SslProtocol protocol = QSsl::UnknownProtocol;
	bool perfectForwardSecrecy = false;
};

struct ServerPingMetric {
	quint64 sampleCount = 0;
	double meanMs       = 0.0;
	double varianceMs2  = 0.0;
};

struct ServerPingStats {
	ServerPingMetric tcp;
	ServerPingMetric udp;
};

struct ServerPacketStats {
	unsigned int good   = 0;
	unsigned int late   = 0;
	unsigned int lost   = 0;
	unsigned int resync = 0;
};

struct ServerCryptStats {
	bool available = false;
	ServerPacketStats local;
	ServerPacketStats remote;
};

struct ServerIdentityDetails {
	QString release;
	QString os;
	QString osVersion;
};

class ServerHandler : public QThread {
private:
	Q_OBJECT
	Q_DISABLE_COPY(ServerHandler)
	friend class TestServerHandlerState;

	std::unique_ptr< Database > database;
	std::atomic< ConnectionPtr > m_connection;
	QSslConfiguration m_sslConfiguration;
	QString m_databaseLocation;
	QString m_sslCipherString;
	QPair< QList< QSslCertificate >, QSslKey > m_clientCertificate;
	bool m_suppressIdentity = false;
	bool m_qosEnabled = false;
	std::atomic< Version::full_t > m_version{ Version::UNKNOWN };
	std::atomic< std::shared_ptr< VoiceRecorder > > m_recorder;
	std::atomic_bool m_udpEnabled{ true };
	std::atomic_bool m_strongConnection{ false };
	std::atomic_bool m_serverSynchronized{ false };
	mutable QReadWriteLock m_digestLock;
	QByteArray m_serverDigest;
	mutable QReadWriteLock m_tlsDetailsLock;
	ServerTlsDetails m_tlsDetails;
	mutable QReadWriteLock m_identityDetailsLock;
	ServerIdentityDetails m_identityDetails;
	mutable QMutex m_pingStatsLock;
	boost::accumulators::accumulator_set<
		double, boost::accumulators::stats< boost::accumulators::tag::mean, boost::accumulators::tag::variance,
										boost::accumulators::tag::count > >
		m_tcpPingAccumulator, m_udpPingAccumulator;
	std::vector< unsigned char > m_udpCryptoBuffer;

	static QMutex nextConnectionIDMutex;
	static int nextConnectionID;

	bool initializeThreadResources(QString &errorMessage);
	void releaseThreadResources();
	void publishConnection(ConnectionPtr connection);
	ConnectionPtr takeConnection();
	void setServerDigest(QByteArray digest);
	void clearTlsDetails();
	void setTlsVerificationDetails(QList< QSslCertificate > certificates, QList< QSslError > errors);
	void setTlsSessionDetails(QList< QSslCertificate > certificates, QSslCipher cipher, QSsl::SslProtocol protocol,
							  bool perfectForwardSecrecy);
	void clearIdentityDetails();
	void setUdpEnabled(bool enabled);
	void setStrongConnection(bool strong);
	void resetPingStats();
	void recordTcpPing(double milliseconds);
	void recordUdpPing(double milliseconds);
	bool isAborted();
	void changeState(ServerHandlerState state);

	std::atomic< ServerHandlerState > m_state{ ServerHandlerState::Idle };

protected:
	QString qsHostName;
	QString qsUserName;
	QString qsPassword;
	unsigned short usPort;
	unsigned short usResolvedPort;
	int connectionID;
	Mumble::Protocol::UDPPingEncoder< Mumble::Protocol::Role::Client > m_udpPingEncoder;
	Mumble::Protocol::UDPDecoder< Mumble::Protocol::Role::Client > m_udpDecoder;
	Mumble::Protocol::UDPDecoder< Mumble::Protocol::Role::Client > m_tcpTunnelDecoder;

#ifdef Q_OS_WIN
	HANDLE hQoS;
	DWORD dwFlowUDP;
#endif

	QHostAddress qhaRemote;
	QHostAddress qhaLocal;
	QUdpSocket *qusUdp;
	mutable QMutex qmUdp;

	void handleVoicePacket(const Mumble::Protocol::AudioData &audioData);

public:
	Timer tTimestamp;
	int iInFlightTCPPings;
	QTimer *tConnectionTimeoutTimer;
	QSslSocket *qtsSock;
	QList< ServerAddress > qlAddresses;
	QHash< ServerAddress, QString > qhHostnames;
	ServerAddress saTargetServer;

	ServerHandler();
	~ServerHandler();
	/// Refreshes settings that are consumed during run() startup. Call only from
	/// the owner/UI thread while this handler is stopped.
	void refreshStartConfiguration();
	/// Releases process-wide resources owned by this handler after run() has
	/// returned. This is idempotent and allows a replacement handler to be
	/// installed without waiting for transient shared_ptr readers to disappear.
	void finalizeThreadResources();
	void setConnectionInfo(const QString &host, unsigned short port, const QString &username, const QString &pw);
	void getConnectionInfo(QString &host, unsigned short &port, QString &username, QString &pw) const;
	bool isStrong() const;
	void customEvent(QEvent *evt) Q_DECL_OVERRIDE;
	int getConnectionID() const;

	void setProtocolVersion(Version::full_t version);
	Version::full_t protocolVersion() const;
	ConnectionPtr connectionSnapshot() const;
	QByteArray serverDigest() const;
	ServerTlsDetails tlsDetailsSnapshot() const;
	ServerPingStats pingStatsSnapshot() const;
	ServerCryptStats cryptStatsSnapshot() const;
	ServerIdentityDetails identityDetailsSnapshot() const;
	void setServerIdentityDetails(QString release, QString os, QString osVersion);
	ServerHandlerState stateSnapshot() const;
	bool isUdpEnabled() const;
	std::shared_ptr< VoiceRecorder > voiceRecorder() const;
	void setVoiceRecorder(std::shared_ptr< VoiceRecorder > voiceRecorder);
	std::shared_ptr< VoiceRecorder > takeVoiceRecorder();
	bool clearVoiceRecorder(const VoiceRecorder *expectedRecorder = nullptr);

	void sendProtoMessage(const ::google::protobuf::Message &msg, Mumble::Protocol::TCPMessageType type);
	void sendMessage(const unsigned char *data, int len, bool force = false);
	void sendVersion();
	void applyCryptSetup(const MumbleProto::CryptSetup &message);

	/// @returns Whether this handler is currently connected to a server.
	bool isConnected() const;

	/// @returns Whether the server this handler is currently connected to, has finished
	/// 	synchronizing yet.
	bool hasSynchronized() const;

	/// @param synchronized Whether the server has finished synchronization
	void setServerSynchronized(bool synchronized);

#define PROCESS_MUMBLE_TCP_MESSAGE(name, value) \
	void sendMessage(const MumbleProto::name &msg) { sendProtoMessage(msg, Mumble::Protocol::TCPMessageType::name); }
	MUMBLE_ALL_TCP_MESSAGES
#undef PROCESS_MUMBLE_TCP_MESSAGE

	void requestUserStats(unsigned int uiSession, bool statsOnly);
	void joinChannel(unsigned int uiSession, unsigned int channel);
	void joinChannel(unsigned int uiSession, unsigned int channel, const QStringList &temporaryAccessTokens);
	void startListeningToChannel(unsigned int channel);
	void startListeningToChannels(const QList< unsigned int > &channelIDs);
	void stopListeningToChannel(unsigned int channel);
	void stopListeningToChannels(const QList< unsigned int > &channelIDs);
	void createChannel(unsigned int parent_id, const QString &name, const QString &description, unsigned int position,
					   bool temporary, unsigned int maxUsers);
	void requestBanList();
	void requestUserList();
	void requestACL(unsigned int channel);
	void registerUser(unsigned int uiSession);
	void kickUser(unsigned int uiSession, const QString &reason);
	void banUser(unsigned int uiSession, const QString &reason, bool banCertificate, bool banIP);
	void sendUserTextMessage(unsigned int uiSession, const QString &message_);
	void sendChannelTextMessage(unsigned int channel, const QString &message_, bool tree);
	void sendChatMessage(MumbleProto::ChatScope scope, unsigned int scopeID, const QString &message_,
						 MumbleProto::ChatBodyFormat bodyFormat = MumbleProto::ChatBodyFormatPlainText,
						 std::optional< unsigned int > replyToMessageID = std::nullopt);
	void sendChatReactionToggle(MumbleProto::ChatScope scope, unsigned int scopeID, unsigned int threadID,
								unsigned int messageID, const QString &emoji, bool active);
	void sendChatMessageDelete(MumbleProto::ChatScope scope, unsigned int scopeID, unsigned int threadID,
							   unsigned int messageID);
	void sendChatHistoryGrant(unsigned int userID, MumbleProto::ChatScope scope, unsigned int scopeID,
							  quint64 visibleAfter, bool revoke);
	void upsertTextChannel(unsigned int textChannelID, const QString &name, const QString &description,
						   unsigned int aclChannelID, unsigned int position, bool create);
	void removeTextChannel(unsigned int textChannelID);
	void setDefaultTextChannel(unsigned int textChannelID);
	void requestChatHistory(MumbleProto::ChatScope scope, unsigned int scopeID = 0, unsigned int startOffset = 0,
							unsigned int limit = 50,
							std::optional< unsigned int > beforeMessageID = std::nullopt);
	bool requestChatHistoryWarmup(const QList< QPair< MumbleProto::ChatScope, unsigned int > > &scopes,
								  unsigned int limitPerScope = 20);
	void updateChatReadState(MumbleProto::ChatScope scope, unsigned int scopeID, unsigned int lastReadMessageID);
	void sendWatchTogetherSync(const MumbleProto::WatchTogetherSync &sync);
	void setUserComment(unsigned int uiSession, const QString &comment);
	void setUserTexture(unsigned int uiSession, const QByteArray &qba);
	void setTokens(const QStringList &tokens);
	void removeChannel(unsigned int channel);
	void addChannelLink(unsigned int channel, unsigned int link);
	void removeChannelLink(unsigned int channel, unsigned int link);
	void requestChannelPermissions(unsigned int channel);
	void setSelfMuteDeafState(bool mute, bool deaf);
	void announceRecordingState(bool recording);

	/// Return connection information as a URL
	QUrl getServerURL(bool withPassword = false) const;

	void disconnect();
	void run() Q_DECL_OVERRIDE;
signals:
	void startupFailed(QString reason);
	void error(QAbstractSocket::SocketError, QString reason);
	// This signal is basically the same as disconnected but it will be emitted
	// *right before* disconnected is emitted. Thus this can be used by slots
	// that need to block the disconnected signal from being emitted (using a
	// direct connection) before they're done.
	void aboutToDisconnect(QAbstractSocket::SocketError, QString reason);
	void disconnected(QAbstractSocket::SocketError, QString reason);
	void connected();
	void pingRequested();
protected slots:
	void message(Mumble::Protocol::TCPMessageType type, const QByteArray &);
	void serverConnectionConnected();
	void serverConnectionTimeoutOnConnect();
	void serverConnectionStateChanged(QAbstractSocket::SocketState);
	void serverConnectionClosed(QAbstractSocket::SocketError, const QString &);
	void setSslErrors(const QList< QSslError > &);
	void udpReady();
	void hostnameResolved();
private slots:
	void sendPingInternal();
public slots:
	void sendPing();
};

using ServerHandlerPtr = std::shared_ptr< ServerHandler >;

#endif
