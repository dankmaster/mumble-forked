// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

/// This file contains callback methods for receiving messages (based on Google's ProtoBuf) from the Server.
/// Further details on what exactly is contained in the "message objects" that are parameters to all functions
/// in this file, can be found in the src/Mumble.proto file.

#include "AudioInput.h"
#include "Channel.h"
#include "ChatFeature.h"
#include "ChatAttachmentUploader.h"
#include "ChatPerfTrace.h"
#include "Connection.h"
#include "ComposerController.h"
#include "Database.h"
#include "FeedbackReport.h"
#include "ForkFeature.h"
#include "Log.h"
#include "MainWindow.h"
#include "ModernDialogController.h"
#include "MumbleConstants.h"
#include "GlobalShortcut.h"
#include "ChannelListenerManager.h"
#include "PluginManager.h"
#include "QmlClientModels.h"
#include "QmlImageProvider.h"
#include "QmlShellHost.h"
#include "ProtoUtils.h"
#include "ScreenShare.h"
#include "ScreenShareManager.h"
#include "ServerHandler.h"
#include "User.h"
#include "UserModel.h"
#include "Utils.h"
#include "VersionCheck.h"
#include "crypto/CryptState.h"
#include "crypto/CryptStateOCB2.h"
#include "Global.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>
#include <QBuffer>
#include <QDateTime>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QImageReader>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTextDocumentFragment>
#include <QFutureWatcher>
#include <QPointer>
#include <QReadLocker>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>

#define ACTOR_INIT                           \
	ClientUser *pSrc = nullptr;              \
	if (msg.has_actor())                     \
		pSrc = ClientUser::get(msg.actor()); \
	Q_UNUSED(pSrc);

#define VICTIM_INIT                                                                \
	ClientUser *pDst = ClientUser::get(msg.session());                             \
	if (!pDst) {                                                                   \
		qWarning("MainWindow: Message for nonexistent victim %d.", msg.session()); \
		return;                                                                    \
	}

#define SELF_INIT                                                                                       \
	ClientUser *pSelf = ClientUser::get(Global::get().uiSession);                                       \
	if (!pSelf) {                                                                                       \
		qWarning("MainWindow: Received message outside of session (sid %d).", Global::get().uiSession); \
		return;                                                                                         \
	}

namespace {
	struct UserLocalPreferenceRequest {
		unsigned int session = 0;
		QString hash;
		QPointer< ClientUser > user;
	};

	struct UserLocalPreferenceSnapshot {
		unsigned int session = 0;
		QString hash;
		bool valid = false;
		QString friendName;
		bool localMuted = false;
		bool localIgnored = false;
		bool localIgnoredTts = false;
		std::optional< bool > remoteSpeechCleanup;
		float localVolume = 1.0f;
		QString localNickname;
	};

	class UserLocalPreferenceLoader final : public QObject {
	public:
		UserLocalPreferenceLoader(QString databasePath, UserModel *userModel, QObject *parent)
			: QObject(parent), m_databasePath(std::move(databasePath)), m_userModel(userModel) {
		}

		void request(ClientUser *user) {
			if (!user || user->qsHash.isEmpty() || m_databasePath.isEmpty()) return;
			const QString hash = user->qsHash;
			if (user->property(LoadedHashProperty).toString() == hash) return;

			m_pending.insert(user->uiSession, UserLocalPreferenceRequest { user->uiSession, hash, user });
			if (!m_workerActive && !m_launchScheduled) {
				m_launchScheduled = true;
				QTimer::singleShot(0, this, [this]() {
					m_launchScheduled = false;
					launchNextBatch();
				});
			}
		}

	private:
		static constexpr auto LoadedHashProperty = "_mumbleLoadedLocalPreferenceHash";
		static constexpr int MaxBatchSize = 64;

		void launchNextBatch() {
			if (m_workerActive || m_pending.isEmpty()) return;

			QList< UserLocalPreferenceRequest > requests;
			requests.reserve(std::min(MaxBatchSize, static_cast< int >(m_pending.size())));
			auto iterator = m_pending.begin();
			while (iterator != m_pending.end() && requests.size() < MaxBatchSize) {
				requests.append(iterator.value());
				iterator = m_pending.erase(iterator);
			}

			QList< QPair< unsigned int, QString > > workerRequests;
			workerRequests.reserve(requests.size());
			for (const UserLocalPreferenceRequest &request : std::as_const(requests)) {
				workerRequests.append({ request.session, request.hash });
			}

			m_workerActive = true;
			auto *watcher = new QFutureWatcher< QList< UserLocalPreferenceSnapshot > >(this);
			connect(watcher, &QFutureWatcher< QList< UserLocalPreferenceSnapshot > >::finished, this,
				[this, watcher, requests = std::move(requests)]() {
					const QList< UserLocalPreferenceSnapshot > snapshots = watcher->result();
					watcher->deleteLater();
					m_workerActive = false;

					for (int index = 0; index < requests.size() && index < snapshots.size(); ++index) {
						const UserLocalPreferenceRequest &request = requests.at(index);
						const UserLocalPreferenceSnapshot &snapshot = snapshots.at(index);
						ClientUser *user = request.user.data();
						if (!snapshot.valid || !user || user->uiSession != request.session
							|| user->qsHash != request.hash || snapshot.session != request.session
							|| snapshot.hash != request.hash || m_pending.contains(request.session)) {
							continue;
						}

						user->setProperty(LoadedHashProperty, request.hash);
						if (!snapshot.friendName.isEmpty() && m_userModel) {
							m_userModel->setFriendName(user, snapshot.friendName);
						}
						if (snapshot.localMuted) user->setLocalMute(true);
						if (snapshot.localIgnored) user->setLocalIgnore(true);
						if (snapshot.localIgnoredTts) user->setLocalIgnoreTTS(true);
						if (snapshot.remoteSpeechCleanup.has_value()) {
							user->setRemoteSpeechCleanupOverride(snapshot.remoteSpeechCleanup);
						}
						user->setLocalVolumeAdjustment(snapshot.localVolume);
						user->setLocalNickname(snapshot.localNickname);
					}

					launchNextBatch();
				});

			const QString databasePath = m_databasePath;
			watcher->setFuture(QtConcurrent::run(
				[databasePath, workerRequests = std::move(workerRequests)]() {
					QList< UserLocalPreferenceSnapshot > snapshots;
					snapshots.reserve(workerRequests.size());
					Database database(
						QStringLiteral("user-local-preferences-%1")
							.arg(QUuid::createUuid().toString(QUuid::WithoutBraces)),
						databasePath, false);
					for (const auto &workerRequest : workerRequests) {
						UserLocalPreferenceSnapshot snapshot;
						snapshot.session = workerRequest.first;
						snapshot.hash = workerRequest.second;
						if (database.isValid()) {
							snapshot.friendName = database.getFriend(snapshot.hash);
							snapshot.localMuted = database.isLocalMuted(snapshot.hash);
							snapshot.localIgnored = database.isLocalIgnored(snapshot.hash);
							snapshot.localIgnoredTts = database.isLocalIgnoredTTS(snapshot.hash);
							snapshot.remoteSpeechCleanup = database.getUserRemoteSpeechCleanup(snapshot.hash);
							snapshot.localVolume = database.getUserLocalVolume(snapshot.hash);
							snapshot.localNickname = database.getUserLocalNickname(snapshot.hash);
							snapshot.valid = true;
						}
						snapshots.append(std::move(snapshot));
					}
					return snapshots;
				}));
		}

		QString m_databasePath;
		QPointer< UserModel > m_userModel;
		QHash< unsigned int, UserLocalPreferenceRequest > m_pending;
		bool m_workerActive = false;
		bool m_launchScheduled = false;
	};

	UserLocalPreferenceLoader *userLocalPreferenceLoader(MainWindow *owner, UserModel *userModel) {
		static constexpr auto LoaderProperty = "_mumbleUserLocalPreferenceLoader";
		if (!owner) return nullptr;
		QObject *loaderObject = owner->property(LoaderProperty).value< QObject * >();
		auto *loader = static_cast< UserLocalPreferenceLoader * >(loaderObject);
		if (!loader) {
			loader = new UserLocalPreferenceLoader(Global::get().s.qsDatabaseLocation, userModel, owner);
			owner->setProperty(LoaderProperty, QVariant::fromValue(static_cast< QObject * >(loader)));
		}
		return loader;
	}

	struct ServerShortcutReadRequest {
		QByteArray digest;
		unsigned int session = 0;
		quint64 serial = 0;
		QPointer< ServerHandler > handler;
		std::function< void() > completion;
	};

	struct ChannelFilterReadRequest {
		QByteArray digest;
		unsigned int channelID = 0;
		ChannelFilterMode initialFilterMode = ChannelFilterMode::NORMAL;
		QPointer< Channel > channel;
		QPointer< ServerHandler > handler;
	};

	struct ServerScopedDatabaseSnapshot {
		bool valid = false;
		QList< Shortcut > shortcuts;
		QList< ChannelFilterMode > channelFilterModes;
	};

	/// Serializes server-scoped UI database reads onto one worker at a time. Channel
	/// reads are coalesced by ID and dispatched in bounded batches. If a server
	/// publishes more channels than the bounded queue can hold, a dynamic property
	/// on the Channel object records the deferred request without growing another
	/// container; subsequent batches discover and drain those markers.
	class ServerScopedDatabaseLoader final : public QObject {
	public:
		ServerScopedDatabaseLoader(QString databasePath, MainWindow *owner, UserModel *userModel)
			: QObject(owner), m_databasePath(std::move(databasePath)), m_owner(owner), m_userModel(userModel) {
		}

		bool requestShortcuts(const ServerHandlerPtr &handler, const unsigned int session,
							  std::function< void() > completion) {
			if (!handler || m_databasePath.isEmpty()) {
				return false;
			}

			const QByteArray digest = handler->serverDigest();
			if (digest.isEmpty()) {
				return false;
			}

			ServerShortcutReadRequest request;
			request.digest     = digest;
			request.session    = session;
			request.serial     = ++m_latestShortcutSerial;
			request.handler    = handler.get();
			request.completion = std::move(completion);
			m_pendingShortcut  = std::move(request);
			scheduleLaunch();
			return true;
		}

		void requestChannelFilter(const ServerHandlerPtr &handler, Channel *channel) {
			if (!handler || !channel || m_databasePath.isEmpty()) {
				return;
			}

			const QByteArray digest = handler->serverDigest();
			if (digest.isEmpty()
				|| channel->property(ChannelFilterPendingDigestProperty).toByteArray() == digest) {
				return;
			}

			if (m_pendingChannelFilters.size() >= MaxPendingChannelFilters) {
				channel->setProperty(ChannelFilterDeferredDigestProperty, digest);
				m_hasDeferredChannelFilters = true;
				return;
			}

			enqueueChannelFilter(handler.get(), channel, digest);
			scheduleLaunch();
		}

	private:
		static constexpr auto ChannelFilterPendingDigestProperty = "_mumbleChannelFilterPendingDigest";
		static constexpr auto ChannelFilterDeferredDigestProperty = "_mumbleChannelFilterDeferredDigest";
		static constexpr int MaxPendingChannelFilters = 256;
		static constexpr int MaxChannelFiltersPerBatch = 64;

		void scheduleLaunch() {
			if (m_workerActive || m_launchScheduled) {
				return;
			}
			m_launchScheduled = true;
			QTimer::singleShot(0, this, [this]() {
				m_launchScheduled = false;
				launchNextBatch();
			});
		}

		void enqueueChannelFilter(ServerHandler *handler, Channel *channel, const QByteArray &digest) {
			ChannelFilterReadRequest request;
			request.digest            = digest;
			request.channelID         = channel->iId;
			request.initialFilterMode = channel->m_filterMode;
			request.channel           = channel;
			request.handler           = handler;
			m_pendingChannelFilters.insert(channel->iId, std::move(request));
			channel->setProperty(ChannelFilterPendingDigestProperty, digest);
			channel->setProperty(ChannelFilterDeferredDigestProperty, QVariant());
		}

		void refillDeferredChannelFilters() {
			if (!m_hasDeferredChannelFilters || m_pendingChannelFilters.size() >= MaxPendingChannelFilters) {
				return;
			}

			const ServerHandlerPtr handler = Global::get().sh;
			if (!handler) {
				m_hasDeferredChannelFilters = false;
				return;
			}
			const QByteArray digest = handler->serverDigest();
			if (digest.isEmpty()) {
				m_hasDeferredChannelFilters = false;
				return;
			}

			QList< QPointer< Channel > > deferredChannels;
			{
				QReadLocker lock(&Channel::c_qrwlChannels);
				for (Channel *channel : std::as_const(Channel::c_qhChannels)) {
					if (channel
						&& channel->property(ChannelFilterDeferredDigestProperty).toByteArray() == digest) {
						deferredChannels.append(channel);
					}
				}
			}

			m_hasDeferredChannelFilters = false;
			for (const QPointer< Channel > &guardedChannel : std::as_const(deferredChannels)) {
				Channel *channel = guardedChannel.data();
				if (!channel) {
					continue;
				}
				if (m_pendingChannelFilters.size() >= MaxPendingChannelFilters) {
					m_hasDeferredChannelFilters = true;
					break;
				}
				enqueueChannelFilter(handler.get(), channel, digest);
			}
		}

		void launchNextBatch() {
			if (m_workerActive) {
				return;
			}

			refillDeferredChannelFilters();
			if (!m_pendingShortcut.has_value() && m_pendingChannelFilters.isEmpty()) {
				return;
			}

			std::optional< ServerShortcutReadRequest > shortcutRequest = std::move(m_pendingShortcut);
			m_pendingShortcut.reset();

			QList< ChannelFilterReadRequest > channelFilterRequests;
			channelFilterRequests.reserve(
				std::min(MaxChannelFiltersPerBatch, static_cast< int >(m_pendingChannelFilters.size())));
			auto iterator = m_pendingChannelFilters.begin();
			while (iterator != m_pendingChannelFilters.end()
				   && channelFilterRequests.size() < MaxChannelFiltersPerBatch) {
				channelFilterRequests.append(iterator.value());
				iterator = m_pendingChannelFilters.erase(iterator);
			}

			const std::optional< QByteArray > workerShortcutDigest =
				shortcutRequest.has_value() ? std::optional< QByteArray >(shortcutRequest->digest) : std::nullopt;
			QList< QPair< QByteArray, unsigned int > > workerChannelFilters;
			workerChannelFilters.reserve(channelFilterRequests.size());
			for (const ChannelFilterReadRequest &request : std::as_const(channelFilterRequests)) {
				workerChannelFilters.append({ request.digest, request.channelID });
			}

			m_workerActive = true;
			auto *watcher = new QFutureWatcher< ServerScopedDatabaseSnapshot >(this);
			connect(watcher, &QFutureWatcher< ServerScopedDatabaseSnapshot >::finished, this,
				[this, watcher, shortcutRequest = std::move(shortcutRequest),
				 channelFilterRequests = std::move(channelFilterRequests)]() {
					const ServerScopedDatabaseSnapshot snapshot = watcher->result();
					watcher->deleteLater();
					m_workerActive = false;

					if (shortcutRequest.has_value() && shortcutRequest->serial == m_latestShortcutSerial) {
						const ServerHandlerPtr currentHandler = Global::get().sh;
						if (currentHandler && currentHandler.get() == shortcutRequest->handler.data()
							&& currentHandler->stateSnapshot() == ServerHandlerState::ConnectionEstablished
							&& currentHandler->serverDigest() == shortcutRequest->digest
							&& Global::get().uiSession == shortcutRequest->session
							&& ClientUser::get(shortcutRequest->session)) {
							if (snapshot.valid && !snapshot.shortcuts.isEmpty()) {
								Global::get().s.qlShortcuts << snapshot.shortcuts;
								if (GlobalShortcutEngine::engine) {
									GlobalShortcutEngine::engine->bNeedRemap = true;
								}
							}
							if (shortcutRequest->completion) {
								shortcutRequest->completion();
							}
						}
					}

					for (int index = 0;
						 index < channelFilterRequests.size() && index < snapshot.channelFilterModes.size(); ++index) {
						const ChannelFilterReadRequest &request = channelFilterRequests.at(index);
						Channel *channel = request.channel.data();
						if (channel
							&& channel->property(ChannelFilterPendingDigestProperty).toByteArray() == request.digest) {
							channel->setProperty(ChannelFilterPendingDigestProperty, QVariant());
						}

						const ServerHandlerPtr currentHandler = Global::get().sh;
						if (!snapshot.valid || !channel || !currentHandler
							|| currentHandler.get() != request.handler.data()
							|| currentHandler->stateSnapshot() != ServerHandlerState::ConnectionEstablished
							|| !currentHandler->isConnected()
							|| currentHandler->serverDigest() != request.digest
							|| Channel::get(request.channelID) != channel || channel->iId != request.channelID) {
							continue;
						}

						const ChannelFilterMode filterMode = snapshot.channelFilterModes.at(index);
						// A local filter action that happened while the read was in flight is
						// authoritative; never replace it with the older snapshot.
						if (channel->m_filterMode != request.initialFilterMode) {
							continue;
						}
						if (channel->m_filterMode == filterMode) {
							continue;
						}
						channel->m_filterMode = filterMode;
						if (m_userModel) {
							const QModelIndex modelIndex = m_userModel->index(channel);
							if (modelIndex.isValid()) {
								emit m_userModel->dataChanged(modelIndex, modelIndex);
							}
						}
						if (m_owner) {
							emit m_owner->channelStateChanged(channel, false);
						}
					}

					refillDeferredChannelFilters();
					launchNextBatch();
				});

			const QString databasePath = m_databasePath;
			watcher->setFuture(QtConcurrent::run(
				[databasePath, workerShortcutDigest,
				 workerChannelFilters = std::move(workerChannelFilters)]() {
					ServerScopedDatabaseSnapshot snapshot;
					Database database(
						QStringLiteral("server-scoped-ui-reads-%1")
							.arg(QUuid::createUuid().toString(QUuid::WithoutBraces)),
						databasePath, false);
					snapshot.valid = database.isValid();
					if (snapshot.valid && workerShortcutDigest.has_value()) {
						snapshot.shortcuts = database.getShortcuts(*workerShortcutDigest);
					}
					snapshot.channelFilterModes.reserve(workerChannelFilters.size());
					for (const auto &workerRequest : workerChannelFilters) {
						snapshot.channelFilterModes.append(
							snapshot.valid
								? database.getChannelFilterMode(workerRequest.first, workerRequest.second)
								: ChannelFilterMode::NORMAL);
					}
					return snapshot;
				}));
		}

		QString m_databasePath;
		QPointer< MainWindow > m_owner;
		QPointer< UserModel > m_userModel;
		std::optional< ServerShortcutReadRequest > m_pendingShortcut;
		QHash< unsigned int, ChannelFilterReadRequest > m_pendingChannelFilters;
		quint64 m_latestShortcutSerial = 0;
		bool m_hasDeferredChannelFilters = false;
		bool m_workerActive = false;
		bool m_launchScheduled = false;
	};

	ServerScopedDatabaseLoader *serverScopedDatabaseLoader(MainWindow *owner, UserModel *userModel) {
		static constexpr auto LoaderProperty = "_mumbleServerScopedDatabaseLoader";
		if (!owner) {
			return nullptr;
		}
		QObject *loaderObject = owner->property(LoaderProperty).value< QObject * >();
		auto *loader = static_cast< ServerScopedDatabaseLoader * >(loaderObject);
		if (!loader) {
			loader = new ServerScopedDatabaseLoader(Global::get().s.qsDatabaseLocation, owner, userModel);
			owner->setProperty(LoaderProperty, QVariant::fromValue(static_cast< QObject * >(loader)));
		}
		return loader;
	}

	QString normalizedChatAssetMime(const QString &mime) {
		return mime.section(QLatin1Char(';'), 0, 0).trimmed().toLower();
	}

	bool isPersistentChatPlayableMediaMime(const QString &mime) {
		const QString normalized = normalizedChatAssetMime(mime);
		return normalized == QLatin1String("image/gif") || normalized == QLatin1String("video/mp4")
			   || normalized == QLatin1String("video/webm");
	}

	QString persistentChatPlayableMediaKind(const QString &mime, MumbleProto::ChatAssetKind kind) {
		const QString normalized = normalizedChatAssetMime(mime);
		if (kind == MumbleProto::ChatAssetKindVideo || normalized.startsWith(QLatin1String("video/"))) {
			return QStringLiteral("video");
		}
		if (normalized == QLatin1String("image/gif")) {
			return QStringLiteral("gif");
		}
		if (kind == MumbleProto::ChatAssetKindImage || normalized.startsWith(QLatin1String("image/"))) {
			return QStringLiteral("image");
		}
		return QStringLiteral("media");
	}

	QString persistentChatPlayableMediaDataUrl(const QString &mime, const QByteArray &bytes) {
		const QString normalized = normalizedChatAssetMime(mime);
		if (!isPersistentChatPlayableMediaMime(normalized) || bytes.isEmpty()) {
			return QString();
		}

		return QString::fromLatin1("data:%1;base64,%2").arg(normalized, QString::fromLatin1(bytes.toBase64()));
	}

	QString trustedChatAttachmentOpenExtension(const QString &mime, const MumbleProto::ChatAssetKind kind,
											 const QString &fileName) {
		const QString normalized = normalizedChatAssetMime(mime);
		if (normalized == QLatin1String("application/pdf")) return QStringLiteral("pdf");
		if (normalized == QLatin1String("text/plain")) return QStringLiteral("txt");
		if (normalized == QLatin1String("text/markdown")) return QStringLiteral("md");
		if (normalized == QLatin1String("audio/mpeg") || normalized == QLatin1String("audio/mp3")) return QStringLiteral("mp3");
		if (normalized == QLatin1String("audio/wav") || normalized == QLatin1String("audio/x-wav")) return QStringLiteral("wav");
		if (normalized == QLatin1String("audio/ogg")) return QStringLiteral("ogg");
		if (normalized == QLatin1String("audio/flac") || normalized == QLatin1String("audio/x-flac")) return QStringLiteral("flac");
		if (normalized == QLatin1String("audio/aac")) return QStringLiteral("aac");
		if (normalized == QLatin1String("audio/mp4")) return QStringLiteral("m4a");
		if (normalized == QLatin1String("audio/webm")) return QStringLiteral("webm");
		if (normalized == QLatin1String("video/mp4")) return QStringLiteral("mp4");
		if (normalized == QLatin1String("video/webm")) return QStringLiteral("webm");
		if (normalized == QLatin1String("video/quicktime")) return QStringLiteral("mov");

		// Older attachment producers may omit MIME metadata. Accept only a small,
		// non-executable extension set that also agrees with the protocol kind.
		if (!normalized.isEmpty() && normalized != QLatin1String("application/octet-stream")) return QString();
		const QString suffix = QFileInfo(fileName).suffix().toLower();
		if (kind == MumbleProto::ChatAssetKindDocument
			&& (suffix == QLatin1String("pdf") || suffix == QLatin1String("txt") || suffix == QLatin1String("md"))) {
			return suffix;
		}
		if (kind == MumbleProto::ChatAssetKindAudio
			&& QStringList { QStringLiteral("mp3"), QStringLiteral("wav"), QStringLiteral("ogg"),
				QStringLiteral("flac"), QStringLiteral("aac"), QStringLiteral("m4a"),
				QStringLiteral("mp4"), QStringLiteral("webm"), QStringLiteral("weba") }.contains(suffix)) {
			return suffix == QLatin1String("mp4") ? QStringLiteral("m4a")
				 : suffix == QLatin1String("weba") ? QStringLiteral("webm") : suffix;
		}
		if (kind == MumbleProto::ChatAssetKindVideo
			&& QStringList { QStringLiteral("mp4"), QStringLiteral("webm"), QStringLiteral("mov") }.contains(suffix)) {
			return suffix;
		}
		return QString();
	}

	QString normalizedPreviewHost(QString host) {
		host = host.trimmed().toLower();
		if (host.startsWith(QLatin1String("www."))) {
			host.remove(0, 4);
		}
		if (host.startsWith(QLatin1String("old."))) {
			host.remove(0, 4);
		}
		if (host.startsWith(QLatin1String("new."))) {
			host.remove(0, 4);
		}
		if (host.startsWith(QLatin1String("m."))) {
			host.remove(0, 2);
		}
		if (host.startsWith(QLatin1String("mobile."))) {
			host.remove(0, 7);
		}
		return host;
	}

	bool isSocialVideoPreviewUrl(const QString &url) {
		const QString host = normalizedPreviewHost(QUrl(url).host());
		return host == QLatin1String("reddit.com") || host == QLatin1String("redd.it")
			   || host == QLatin1String("v.redd.it") || host == QLatin1String("facebook.com")
			   || host == QLatin1String("fb.watch") || host == QLatin1String("instagram.com")
			   || host == QLatin1String("instagr.am");
	}

	QList< int > preferredScreenShareCodecsFromConfig(const MumbleProto::ServerConfig &msg) {
		QList< int > codecs;
		codecs.reserve(msg.preferred_screen_share_codecs_size());
		for (int i = 0; i < msg.preferred_screen_share_codecs_size(); ++i) {
			codecs.append(static_cast< int >(msg.preferred_screen_share_codecs(i)));
		}

		return Mumble::ScreenShare::sanitizeCodecList(codecs);
	}

	QString boolToken(const bool value) {
		return value ? QStringLiteral("true") : QStringLiteral("false");
	}

} // namespace

/// The authenticate message is being used by the client to send the authentication credentials to the server. Therefore
/// the server won't send this message type to the client which is why this implementation does nothing.
void MainWindow::msgAuthenticate(const MumbleProto::Authenticate &) {
}

/// This message is being received after this client has queried for the ban list (probably by using the BanEditor).
///
/// @param msg The message object containing information about the ban list
void MainWindow::msgBanList(const MumbleProto::BanList &msg) {
	openModernServerBanListDialog(msg);
}

/// This message is being received whenever the server rejects the connection of this client.
///
/// @param msg The message object containing the information about why the connection was rejected
void MainWindow::msgReject(const MumbleProto::Reject &msg) {
	rtLast = msg.type();

	QString reason;

	switch (rtLast) {
		case MumbleProto::Reject_RejectType_InvalidUsername:
			reason = tr("According to the server's configuration, your username is considered invalid.");
			break;
		case MumbleProto::Reject_RejectType_UsernameInUse:
			reason = tr("Username in use");
			break;
		case MumbleProto::Reject_RejectType_WrongUserPW:
			reason = tr("Wrong certificate or password");
			break;
		case MumbleProto::Reject_RejectType_WrongServerPW:
			reason = tr("Wrong password");
			break;
		case MumbleProto::Reject_RejectType_AuthenticatorFail:
			reason = tr("Your account information can not be verified currently. Please try again later");
			break;
		default:
			reason = u8(msg.reason()).toHtmlEscaped();
			break;
	}

	Global::get().l->log(Log::ServerDisconnected, tr("Server connection rejected: %1.").arg(reason));
	Global::get().l->setIgnore(Log::ServerDisconnected, 1);
}

/// This message is being received when the server has authenticated the user and finished synchronizing the server
/// state. The message contains the session ID (user ID) for this client that gets assigned to Global::uiSession. It
/// also contains information about the maximum bandwidth the user should use and the user's permissions in the root
/// channel. Furthermore the message may contain a welcome message that is logged to Mumble's console if present.
///
/// @param msg The message object with the respective information
void MainWindow::msgServerSync(const MumbleProto::ServerSync &msg) {
	const ClientUser *user = ClientUser::get(msg.session());
	const bool traceServerSync = qEnvironmentVariableIntValue("MUMBLE_CONNECT_TRACE") != 0;
	const auto appendServerSyncTrace = [traceServerSync, session = msg.session()](const QString &phase) {
		if (!traceServerSync) {
			return;
		}

		const QByteArray line = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toUtf8() + 'Z'
			+ " ServerSync session=" + QByteArray::number(session) + ' ' + phase.toUtf8() + '\n';
		mumble::chatperf::appendFileLineAsync(
			Global::get().qdBasePath.filePath(QLatin1String("shared-modern-connect-trace.log")), line);
	};
	appendServerSyncTrace(QStringLiteral("enter"));
	if (!user) {
		Global::get().l->log(Log::CriticalError, tr("Server sync protocol violation. No user profile received."));
		Global::get().sh->disconnect();
		return;
	}
	appendServerSyncTrace(QStringLiteral("user-found"));
	Global::get().uiSession = msg.session();
	if (m_qmlShellHost) {
		m_qmlShellHost->mediaSession()->setCurrentVoiceScopeId(
			user->cChannel ? static_cast< qulonglong >(user->cChannel->iId) : 0);
	}
	Global::get().bScreenShareEnabled = false;
	Global::get().bScreenShareRecordingEnabled = false;
	Global::get().bScreenShareHelperRequired = true;
	Global::get().qlPreferredScreenShareCodecs.clear();
	Global::get().uiScreenShareMaxWidth = 0;
	Global::get().uiScreenShareMaxHeight = 0;
	Global::get().uiScreenShareMaxFps = 0;
	Global::get().qsScreenShareRelayUrl.clear();
	Global::get().bFeedbackEnabled       = false;
	Global::get().uiFeedbackMaxLogBytes  = Mumble::Feedback::DEFAULT_MAX_LOG_BYTES;
	Global::get().uiFeedbackMaxBodyBytes = Mumble::Feedback::DEFAULT_MAX_BODY_BYTES;
	Global::get().qsServerDisplayName.clear();
	Global::get().qsServerMonogram.clear();
	Global::get().qbaServerImage.clear();
	refreshQmlServerIdentity();

	Global::get().sh->sendPing(); // Send initial ping to establish UDP connection
	appendServerSyncTrace(QStringLiteral("sent-ping"));

	Global::get().pPermissions = ChanACL::Permissions(static_cast< unsigned int >(msg.permissions()));
	syncModernServerAdminPermissions();
	Global::get().l->clearIgnore();
	if (msg.has_welcome_text()) {
		QString str = u8(msg.welcome_text());
		setPersistentChatWelcomeText(str);
	}
	pmModel->recheckLinks();
	appendServerSyncTrace(QStringLiteral("post-model-sync"));

	// Reset the mechanism for using and recycling target IDs for setting up
	// VoiceTargets
	qmTargetUse.clear();
	qmTargets.clear();
	const int uniqueTargetIDCount = 5;
	for (int i = 1; i < uniqueTargetIDCount + 1; ++i) {
		qmTargetUse.insert(i, i);
	}
	iTargetCounter = 100;
	appendServerSyncTrace(QStringLiteral("post-target-reset"));

	AudioInput::setMaxBandwidth(static_cast< int >(msg.max_bandwidth()));
	appendServerSyncTrace(QStringLiteral("post-bandwidth"));

	findDesiredChannel();
	appendServerSyncTrace(QStringLiteral("post-find-desired-channel"));

	QString host, uname, pw;
	unsigned short port;

	Global::get().sh->getConnectionInfo(host, port, uname, pw);

	const ServerHandlerPtr synchronizedHandler = Global::get().sh;
	const QByteArray synchronizedDigest = synchronizedHandler ? synchronizedHandler->serverDigest() : QByteArray();
	const unsigned int synchronizedSession = msg.session();
	const auto completeServerSync =
		[guardedThis = QPointer< MainWindow >(this), synchronizedHandler, synchronizedDigest, synchronizedSession,
		 appendServerSyncTrace]() {
			if (!guardedThis || !synchronizedHandler) {
				return;
			}
			const ServerHandlerPtr currentHandler = Global::get().sh;
			if (!currentHandler || currentHandler.get() != synchronizedHandler.get()
				|| currentHandler->stateSnapshot() != ServerHandlerState::ConnectionEstablished
				|| currentHandler->serverDigest() != synchronizedDigest
				|| Global::get().uiSession != synchronizedSession || !ClientUser::get(synchronizedSession)) {
				return;
			}

			appendServerSyncTrace(QStringLiteral("post-shortcuts"));
			currentHandler->setServerSynchronized(true);
			guardedThis->enterQmlShellSteadyState();
			guardedThis->updateChatBar();
			guardedThis->warmupPersistentChatHistory();
			appendServerSyncTrace(QStringLiteral("exit"));

			emit guardedThis->serverSynchronized();
		};

	bool shortcutLoadQueued = false;
	if (ServerScopedDatabaseLoader *loader = serverScopedDatabaseLoader(this, pmModel)) {
		shortcutLoadQueued =
			loader->requestShortcuts(synchronizedHandler, synchronizedSession, completeServerSync);
	}
	appendServerSyncTrace(QStringLiteral("shortcut-load-dispatched"));

	connect(user, SIGNAL(talkingStateChanged()), this, SLOT(userStateChanged()));
	connect(user, SIGNAL(muteDeafStateChanged()), this, SLOT(userStateChanged()));
	connect(user, SIGNAL(prioritySpeakerStateChanged()), this, SLOT(userStateChanged()));
	connect(user, SIGNAL(recordingStateChanged()), this, SLOT(userStateChanged()));
	appendServerSyncTrace(QStringLiteral("post-self-signal-connect"));

	AudioInputPtr audioIn = Global::get().ai;
	if (audioIn) {
		audioIn->updateUserMuteDeafState(user);
		QObject::connect(user, &ClientUser::muteDeafStateChanged, audioIn.get(),
						 &AudioInput::onUserMuteDeafStateChanged);
	}
	appendServerSyncTrace(QStringLiteral("post-audio-update"));

	// Update QActions and menus
	if (qmServer && qmSelf && qmConfig) {
		refreshServerActions();
		refreshSelfActions();
		if (qmChannel && qmUser) {
			qmChannel_aboutToShow();
			qmUser_aboutToShow();
		}
		refreshConfigActions();
	}
	appendServerSyncTrace(QStringLiteral("post-menu-update"));

	if (!shortcutLoadQueued) {
		completeServerSync();
	}
}

/// This message is being received when the server informs this client about server configuration details. This contains
/// things like the maximum bandwidth, the welcome text, whether HTML in messages is allowed, information about message
/// lengths as well as the maximum amount of users that may be connected to this server.
///
/// @param msg The message object
void MainWindow::msgServerConfig(const MumbleProto::ServerConfig &msg) {
	bool persistentGlobalChanged = false;
	bool screenShareConfigChanged = false;
	bool serverIdentityChanged = false;
	bool modernLayoutCompatibleAdvertised = false;
	if (msg.has_welcome_text()) {
		QString str = u8(msg.welcome_text());
		setPersistentChatWelcomeText(str);
	}
	if (msg.has_max_bandwidth())
		AudioInput::setMaxBandwidth(static_cast< int >(msg.max_bandwidth()));
	if (msg.has_allow_html())
		Global::get().bAllowHTML = msg.allow_html();
	if (msg.has_message_length())
		Global::get().uiMessageLength = msg.message_length();
	if (msg.has_image_message_length())
		Global::get().uiImageLength = msg.image_message_length();
	if (msg.has_chat_asset_max_bytes())
		Global::get().uiChatAssetMaxBytes = msg.chat_asset_max_bytes();
	if (msg.has_chat_attachment_limit())
		Global::get().uiChatAttachmentLimit = msg.chat_attachment_limit();
	if (m_qmlShellHost && (msg.has_chat_asset_max_bytes() || msg.has_chat_attachment_limit())) {
		m_qmlShellHost->composerController()->setAttachmentLimits(
			static_cast< int >(Global::get().uiChatAttachmentLimit), Global::get().uiChatAssetMaxBytes);
	}
	if (msg.has_max_users())
		Global::get().uiMaxUsers = msg.max_users();
	if (msg.has_recording_allowed()) {
		Global::get().mw->enableRecording(msg.recording_allowed());
	}
	if (msg.has_persistent_global_chat_enabled()) {
		markPersistentChatAvailable(false);
		const bool enabled = msg.persistent_global_chat_enabled();
		persistentGlobalChanged = Global::get().bPersistentGlobalChatEnabled != enabled;
		Global::get().bPersistentGlobalChatEnabled = enabled;
		modernLayoutCompatibleAdvertised = true;
	}
	if (msg.has_persistent_chat_protocol_version()) {
		Global::get().uiPersistentChatProtocolVersion = msg.persistent_chat_protocol_version();
		modernLayoutCompatibleAdvertised              = true;
	}
	if (msg.supported_chat_features_size() > 0) {
		Global::get().qlSupportedChatFeatures = Mumble::ChatFeatures::featuresFromServerConfig(msg);
		if (Mumble::ChatFeatures::contains(Global::get().qlSupportedChatFeatures,
										   MumbleProto::ChatFeaturePersistentHistory)) {
			markPersistentChatAvailable(false);
		}
		modernLayoutCompatibleAdvertised = true;
	}
	if (msg.supported_fork_features_size() > 0 || msg.has_fork_extension_protocol_version()) {
		Global::get().qlSupportedForkFeatures      = Mumble::ForkFeatures::featuresFromServerConfig(msg);
		Global::get().uiForkExtensionProtocolVersion =
			msg.has_fork_extension_protocol_version() ? msg.fork_extension_protocol_version() : 0;
		modernLayoutCompatibleAdvertised = true;
	}
	if (msg.has_screen_share_enabled()) {
		Global::get().bScreenShareEnabled = msg.screen_share_enabled();
		screenShareConfigChanged          = true;
		modernLayoutCompatibleAdvertised = true;
	}
	if (msg.has_screen_share_recording_enabled()) {
		Global::get().bScreenShareRecordingEnabled = msg.screen_share_recording_enabled();
		screenShareConfigChanged                   = true;
		modernLayoutCompatibleAdvertised = true;
	}
	if (msg.has_screen_share_helper_required()) {
		Global::get().bScreenShareHelperRequired = msg.screen_share_helper_required();
		screenShareConfigChanged                 = true;
		modernLayoutCompatibleAdvertised = true;
	}
	if (msg.preferred_screen_share_codecs_size() > 0) {
		Global::get().qlPreferredScreenShareCodecs = preferredScreenShareCodecsFromConfig(msg);
		screenShareConfigChanged                   = true;
		modernLayoutCompatibleAdvertised = true;
	}
	if (msg.has_screen_share_max_width()) {
		Global::get().uiScreenShareMaxWidth =
			Mumble::ScreenShare::sanitizeLimit(msg.screen_share_max_width(), 0, Mumble::ScreenShare::HARD_MAX_WIDTH);
		screenShareConfigChanged = true;
		modernLayoutCompatibleAdvertised = true;
	}
	if (msg.has_screen_share_max_height()) {
		Global::get().uiScreenShareMaxHeight =
			Mumble::ScreenShare::sanitizeLimit(msg.screen_share_max_height(), 0, Mumble::ScreenShare::HARD_MAX_HEIGHT);
		screenShareConfigChanged = true;
		modernLayoutCompatibleAdvertised = true;
	}
	if (msg.has_screen_share_max_fps()) {
		Global::get().uiScreenShareMaxFps =
			Mumble::ScreenShare::sanitizeLimit(msg.screen_share_max_fps(), 0, Mumble::ScreenShare::HARD_MAX_FPS);
		screenShareConfigChanged = true;
		modernLayoutCompatibleAdvertised = true;
	}
	if (msg.has_screen_share_relay_url()) {
		Global::get().qsScreenShareRelayUrl =
			Mumble::ScreenShare::normalizeRelayUrl(u8(msg.screen_share_relay_url()));
		screenShareConfigChanged = true;
		modernLayoutCompatibleAdvertised = true;
	}
	if (msg.has_stonks_enabled()) {
		Global::get().bStonksEnabled = msg.stonks_enabled();
		modernLayoutCompatibleAdvertised = true;
	}
	if (msg.has_stonks_text_channel_id()) {
		Global::get().uiStonksTextChannelID = msg.stonks_text_channel_id();
		modernLayoutCompatibleAdvertised = true;
	}
	if (msg.has_stonks_social_announcements_enabled()) {
		Global::get().bStonksSocialAnnouncementsEnabled = msg.stonks_social_announcements_enabled();
		modernLayoutCompatibleAdvertised = true;
	}
	if (msg.has_feedback_enabled()) {
		Global::get().bFeedbackEnabled = msg.feedback_enabled();
		modernLayoutCompatibleAdvertised = true;
	}
	if (msg.has_feedback_max_log_bytes()) {
		Global::get().uiFeedbackMaxLogBytes =
			qMax(1u, msg.feedback_max_log_bytes());
		modernLayoutCompatibleAdvertised = true;
	}
	if (msg.has_feedback_max_body_bytes()) {
		Global::get().uiFeedbackMaxBodyBytes =
			qMax(1u, msg.feedback_max_body_bytes());
		modernLayoutCompatibleAdvertised = true;
	}
	if (msg.has_server_display_name()) {
		Global::get().qsServerDisplayName = u8(msg.server_display_name()).trimmed().left(128);
		modernLayoutCompatibleAdvertised  = true;
		serverIdentityChanged             = true;
	}
	if (msg.has_server_monogram()) {
		Global::get().qsServerMonogram = u8(msg.server_monogram()).trimmed().left(12);
		modernLayoutCompatibleAdvertised = true;
		serverIdentityChanged            = true;
	}
	if (msg.has_server_image()) {
		Global::get().qbaServerImage = blob(msg.server_image());
		modernLayoutCompatibleAdvertised = true;
		serverIdentityChanged            = true;
	}
	if (serverIdentityChanged) refreshQmlServerIdentity();
	if (screenShareConfigChanged && Global::get().s.bScreenShareDiagnostics) {
		qInfo().noquote()
			<< QStringLiteral("MainWindow: received screen-share ServerConfig enabled=%1 recording=%2 helper_required=%3 "
							  "max=%4x%5@%6 relay_url=%7")
				   .arg(boolToken(Global::get().bScreenShareEnabled), boolToken(Global::get().bScreenShareRecordingEnabled),
						boolToken(Global::get().bScreenShareHelperRequired),
						QString::number(Global::get().uiScreenShareMaxWidth),
						QString::number(Global::get().uiScreenShareMaxHeight),
						QString::number(Global::get().uiScreenShareMaxFps),
						Global::get().qsScreenShareRelayUrl.isEmpty() ? QStringLiteral("-")
																	  : Global::get().qsScreenShareRelayUrl);
		if (m_screenShareManager) {
			m_screenShareManager->logLocalShareAvailabilityDiagnostic(QStringLiteral("server-config"));
		}
	}
	if (modernLayoutCompatibleAdvertised) {
		m_modernLayoutCompatibleServer = true;
	}
	if (modernLayoutCompatibleAdvertised && Settings::LayoutModern != m_activeShellLayout) {
		refreshShellLayout();
	}
	if (persistentGlobalChanged) {
		rebuildPersistentChatChannelList();
		updateChatBar();
		const PersistentChatTarget target = currentPersistentChatTarget();
		if (target.scope == MumbleProto::ServerGlobal || target.scope == MumbleProto::Aggregate) {
			refreshPersistentChatView(true);
		}
	}
	scheduleQmlRoomStateUpdate();
	publishQmlActiveScopeState();
}

/// This message is being received when the server denied the permission to perform a requested action. This function
/// basically informs the user about this denial by printing a message to Mumble's console.
///
/// @param msg The message object containing further details as to why and what Permission has been denied
void MainWindow::msgPermissionDenied(const MumbleProto::PermissionDenied &msg) {
	QString adminError = msg.has_reason() && !u8(msg.reason()).trimmed().isEmpty()
		? u8(msg.reason()).trimmed() : tr("The server rejected this administrative change.");
	if (msg.type() == MumbleProto::PermissionDenied_DenyType_UserName) {
		adminError = tr("The server rejected the registered username.");
		failPendingModernServerAdminOperations(adminError, true, false);
	} else if (msg.type() == MumbleProto::PermissionDenied_DenyType_SuperUser) {
		adminError = tr("The SuperUser account cannot be changed this way.");
		failPendingModernServerAdminOperations(adminError, true, false);
	} else if (msg.type() == MumbleProto::PermissionDenied_DenyType_Permission) {
		const ChanACL::Permissions denied = static_cast< ChanACL::Permissions >(msg.permission());
		const bool users = denied & (ChanACL::Register | ChanACL::Write);
		const bool bans = denied & (ChanACL::Ban | ChanACL::Write);
		// Some older servers omit the exact permission. Failing both is safe here:
		// only a controller with a matching pending operation performs a rollback.
		failPendingModernServerAdminOperations(adminError, users || (!users && !bans), bans || (!users && !bans));
	}
	switch (msg.type()) {
		case MumbleProto::PermissionDenied_DenyType_Permission: {
			VICTIM_INIT;
			SELF_INIT;
			Channel *c = Channel::get(msg.channel_id());
			if (!c)
				return;
			ChanACL::Permissions permission = static_cast< ChanACL::Permissions >(msg.permission());
			QString pname                   = ChanACL::permName(permission);

			if ((permission == ChanACL::Perm::Enter) && c->hasEnterRestrictions.load()) {
				Global::get().l->log(
					Log::PermissionDenied,
					tr("Unable to %1 into %2 - Adding the respective access (password) token might grant you access.")
						.arg(Log::msgColor(pname, Log::Privilege))
						.arg(Log::formatChannel(c)));
			} else {
				QString text;
				if (pDst == pSelf)
					text = tr("You were denied %1 privileges in %2.").arg(Log::msgColor(pname, Log::Privilege))
							   .arg(Log::formatChannel(c));
				else
					text = tr("%3 was denied %1 privileges in %2.").arg(Log::msgColor(pname, Log::Privilege))
							   .arg(Log::formatChannel(c))
							   .arg(Log::formatClientUser(pDst, Log::Target));
				if (msg.has_reason() && !u8(msg.reason()).trimmed().isEmpty()) {
					text += QLatin1Char(' ') + u8(msg.reason()).toHtmlEscaped();
				}
				Global::get().l->log(Log::PermissionDenied, text);
			}
		} break;
		case MumbleProto::PermissionDenied_DenyType_SuperUser: {
			Global::get().l->log(Log::PermissionDenied, tr("Denied: Cannot modify SuperUser."));
		} break;
		case MumbleProto::PermissionDenied_DenyType_ChannelName: {
			Global::get().l->log(
				Log::PermissionDenied,
				tr("Denied: According to the server's configuration, the channel name is considered invalid."));
		} break;
		case MumbleProto::PermissionDenied_DenyType_TextTooLong: {
			Global::get().l->log(Log::PermissionDenied, tr("Denied: Text message too long."));
		} break;
		case MumbleProto::PermissionDenied_DenyType_H9K: {
			if (Global::get().bHappyEaster) {
				bool bold             = Global::get().s.bDeaf;
				bool bold2            = Global::get().s.bTTS;
				Global::get().s.bDeaf = false;
				Global::get().s.bTTS  = true;
				quint32 oflags        = Global::get().s.qmMessages.value(Log::PermissionDenied);
				Global::get().s.qmMessages[Log::PermissionDenied] =
					(oflags | Settings::LogTTS) & static_cast< unsigned int >(~Settings::LogSoundfile);
				Global::get().l->log(Log::PermissionDenied, QString::fromUtf8(Global::get().ccHappyEaster + 39)
																.arg(Global::get().s.qsUsername.toHtmlEscaped()));
				Global::get().s.qmMessages[Log::PermissionDenied] = oflags;
				Global::get().s.bDeaf                             = bold;
				Global::get().s.bTTS                              = bold2;
				qApp->setWindowIcon(QIcon(QString::fromUtf8(Global::get().ccHappyEaster)));
				qWarning() << "Happy Easter";
			}
		} break;
		case MumbleProto::PermissionDenied_DenyType_TemporaryChannel: {
			Global::get().l->log(Log::PermissionDenied, tr("Denied: Operation not permitted in temporary channel."));
		} break;
		case MumbleProto::PermissionDenied_DenyType_MissingCertificate: {
			VICTIM_INIT;
			SELF_INIT;
			if (pDst == pSelf)
				Global::get().l->log(Log::PermissionDenied, tr("You need a certificate to perform this operation."));
			else
				Global::get().l->log(
					Log::PermissionDenied,
					tr("%1 does not have a certificate.").arg(Log::formatClientUser(pDst, Log::Target)));
		} break;
		case MumbleProto::PermissionDenied_DenyType_UserName: {
			if (msg.has_name())
				Global::get().l->log(
					Log::PermissionDenied,
					tr("According to the server's configuration, the username %1 is considered invalid.")
						.arg(u8(msg.name()).toHtmlEscaped()));
			else
				Global::get().l->log(
					Log::PermissionDenied,
					tr("According to the server's configuration, the username is considered invalid."));
		} break;
		case MumbleProto::PermissionDenied_DenyType_ChannelFull: {
			Global::get().l->log(Log::PermissionDenied, tr("Channel is full."));
		} break;
		case MumbleProto::PermissionDenied_DenyType_NestingLimit: {
			Global::get().l->log(Log::PermissionDenied, tr("Channel nesting limit reached."));
		} break;
		case MumbleProto::PermissionDenied_DenyType_ChannelCountLimit: {
			Global::get().l->log(Log::PermissionDenied,
								 tr("Channel count limit reached. Need to delete channels before creating new ones."));
		} break;
		case MumbleProto::PermissionDenied_DenyType_ChannelListenerLimit: {
			Global::get().l->log(Log::PermissionDenied, tr("No more listeners allowed in this channel."));
		} break;
		case MumbleProto::PermissionDenied_DenyType_UserListenerLimit: {
			Global::get().l->log(Log::PermissionDenied,
								 tr("You are not allowed to listen to more channels than you currently are."));
		} break;
		default: {
			if (msg.has_reason())
				Global::get().l->log(Log::PermissionDenied, tr("Denied: %1.").arg(u8(msg.reason()).toHtmlEscaped()));
			else
				Global::get().l->log(Log::PermissionDenied, tr("Permission denied."));
		} break;
	}
}

/// This message is not used (anymore). Thus the implementation does nothing
void MainWindow::msgUDPTunnel(const MumbleProto::UDPTunnel &) {
}

/// This message is being received when the server informs this client about changed users. This might be because there
/// is a new user or because an existing user changed somehow (this includes things like a changed ID, changed name,
/// changed priority speaker status, changed channel, etc.). This function will match the local user representation
/// (UserModel) to these changes.
///
/// @param msg The message object containing the respective information
void MainWindow::msgUserState(const MumbleProto::UserState &msg) {
	ACTOR_INIT;
	ClientUser *pSelf = ClientUser::get(Global::get().uiSession);
	ClientUser *pDst  = ClientUser::get(msg.session());
	Channel *channel  = nullptr;
	const bool traceUserState = qEnvironmentVariableIntValue("MUMBLE_CONNECT_TRACE") != 0;
	const auto appendUserStateTrace = [traceUserState, session = msg.session()](const QString &phase) {
		if (!traceUserState) {
			return;
		}

		const QByteArray line = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toUtf8() + 'Z'
			+ " UserState session=" + QByteArray::number(session) + ' ' + phase.toUtf8() + '\n';
		mumble::chatperf::appendFileLineAsync(
			Global::get().qdBasePath.filePath(QLatin1String("shared-modern-connect-trace.log")), line);
	};
	appendUserStateTrace(QStringLiteral("enter"));
	bool createdUser                             = false;
	bool renamedUser                             = false;
	bool movedChannels                           = false;
	bool persistentIdentityChanged               = false;

	if (msg.has_channel_id()) {
		channel = Channel::get(msg.channel_id());
		if (!channel) {
			qWarning("msgUserState(): unknown channel.");
			channel = Channel::get(Mumble::ROOT_CHANNEL_ID);
		}
	}
	appendUserStateTrace(QStringLiteral("channel-resolved"));

	// User just connected
	if (!pDst) {
		if (!msg.has_name()) {
			return;
		}
		createdUser = true;

		pDst = pmModel->addUser(msg.session(), u8(msg.name()));

		appendUserStateTrace(QStringLiteral("connected-signals"));

		if (channel && channel != pDst->cChannel) {
			pmModel->moveUser(pDst, channel);
		}
		appendUserStateTrace(QStringLiteral("initial-channel-applied"));

		if (msg.has_hash()) {
			pmModel->setHash(pDst, u8(msg.hash()));
		}
		appendUserStateTrace(QStringLiteral("initial-hash-applied"));
		appendUserStateTrace(QStringLiteral("created"));

		if (pSelf) {
			if (pDst->cChannel == pSelf->cChannel) {
				Global::get().l->log(
					Log::ChannelJoinConnect,
					tr("%1 connected and entered channel.").arg(Log::formatClientUser(pDst, Log::Source)));
			} else {
				Global::get().l->log(Log::UserJoin, tr("%1 connected.").arg(Log::formatClientUser(pDst, Log::Source)));
			}
		}
	}
	appendUserStateTrace(QStringLiteral("post-create"));

	if (msg.has_user_id()) {
		persistentIdentityChanged = pDst->iId != static_cast< int >(msg.user_id());
		pmModel->setUserId(pDst, static_cast< int >(msg.user_id()));
		if (persistentIdentityChanged && m_pendingChatHistoryGrant
			&& m_pendingChatHistoryGrant->session == pDst->uiSession
			&& m_pendingChatHistoryGrant->persistentUserID != msg.user_id()) {
			failPendingChatHistoryGrant(
				QStringLiteral("stale_target"),
				tr("The target user's registered identity changed. Reopen the user menu and submit the grant again."));
		}
	}
	appendUserStateTrace(QStringLiteral("post-user-id"));

	if (channel) {
		Channel *oldChannel = pDst->cChannel;
		if (channel != oldChannel) {
			movedChannels = true;
			pmModel->moveUser(pDst, channel);

			if (pSelf) {
				if (pDst == pSelf) {
					qsDesiredChannel = channel->getPath();
				}

				if (pDst == pSelf) {
					if (pSrc == pSelf) {
						Global::get().l->log(Log::SelfChannelJoin,
											 tr("You joined %1.").arg(Log::formatChannel(channel)));
					} else {
						Global::get().l->log(Log::SelfChannelJoinOther,
											 tr("You were moved to %1 by %2.")
												 .arg(Log::formatChannel(channel))
												 .arg(Log::formatClientUser(pSrc, Log::Source)));
					}
				} else if (pSrc == pSelf) {
					if (channel == pSelf->cChannel) {
						Global::get().l->log(Log::ChannelJoin, tr("You moved %1 to %2.")
																   .arg(Log::formatClientUser(pDst, Log::Target))
																   .arg(Log::formatChannel(channel)));
					} else {
						Global::get().l->log(Log::ChannelLeave, tr("You moved %1 to %2.")
																	.arg(Log::formatClientUser(pDst, Log::Target))
																	.arg(Log::formatChannel(channel)));
					}
				} else if ((channel == pSelf->cChannel) || (oldChannel == pSelf->cChannel)) {
					if (pDst == pSrc) {
						if (channel == pSelf->cChannel) {
							Global::get().l->log(
								Log::ChannelJoin,
								tr("%1 entered channel.").arg(Log::formatClientUser(pDst, Log::Target)));
						} else {
							Global::get().l->log(Log::ChannelLeave, tr("%1 moved to %2.")
																		.arg(Log::formatClientUser(pDst, Log::Target))
																		.arg(Log::formatChannel(channel)));
						}
					} else {
						if (channel == pSelf->cChannel) {
							Global::get().l->log(Log::ChannelJoin, tr("%1 moved in from %2 by %3.")
																	   .arg(Log::formatClientUser(pDst, Log::Target))
																	   .arg(Log::formatChannel(oldChannel))
																	   .arg(Log::formatClientUser(pSrc, Log::Source)));
						} else {
							Global::get().l->log(Log::ChannelLeave, tr("%1 moved to %2 by %3.")
																		.arg(Log::formatClientUser(pDst, Log::Target))
																		.arg(Log::formatChannel(channel))
																		.arg(Log::formatClientUser(pSrc, Log::Source)));
						}
					}
				}

				if ((channel == pSelf->cChannel) && pDst->bRecording) {
					Global::get().l->log(Log::Recording,
										 tr("%1 is recording").arg(Log::formatClientUser(pDst, Log::Target)));
				}
			}
		}
	}
	if (movedChannels && pDst->uiSession == Global::get().uiSession && m_qmlShellHost) {
		m_qmlShellHost->mediaSession()->setCurrentVoiceScopeId(
			pDst->cChannel ? static_cast< qulonglong >(pDst->cChannel->iId) : 0);
	}
	appendUserStateTrace(QStringLiteral("post-channel"));

	// Handle channel listening
	for (int i = 0; i < msg.listening_channel_add_size(); i++) {
		Channel *c = Channel::get(msg.listening_channel_add(i));

		if (!c) {
			qWarning("msgUserState(): Invalid channel ID encountered");
			continue;
		}

		if (Global::get().channelListenerManager->isListening(pDst->uiSession, c->iId)) {
			// We are already listening to this channel
			continue;
		}

		Global::get().channelListenerManager->addListener(pDst->uiSession, c->iId);
		emit userAddedChannelListener(pDst, c);

		QString logMsg;
		if (pDst == pSelf) {
			logMsg = tr("You started listening to %1").arg(Log::formatChannel(c));
		} else if (pSelf && pSelf->cChannel == c) {
			logMsg = tr("%1 started listening to your channel").arg(Log::formatClientUser(pDst, Log::Target));
		}

		if (!logMsg.isEmpty()) {
			Global::get().l->log(Log::ChannelListeningAdd, logMsg);
		}
	}
	for (int i = 0; i < msg.listening_channel_remove_size(); i++) {
		Channel *c = Channel::get(msg.listening_channel_remove(i));

		if (!c) {
			qWarning("msgUserState(): Invalid channel ID encountered");
			continue;
		}

		Global::get().channelListenerManager->removeListener(pDst->uiSession, c->iId);
		emit userRemovedChannelListener(pDst, c);

		QString logMsg;
		if (pDst == pSelf) {
			logMsg = tr("You stopped listening to %1").arg(Log::formatChannel(c));
		} else if (pSelf && pSelf->cChannel == c) {
			logMsg = tr("%1 stopped listening to your channel").arg(Log::formatClientUser(pDst, Log::Target));
		}

		if (!logMsg.isEmpty()) {
			Global::get().l->log(Log::ChannelListeningRemove, logMsg);
		}
	}
	for (int i = 0; i < msg.listening_volume_adjustment_size(); i++) {
		unsigned int channelID = msg.listening_volume_adjustment(i).listening_channel();
		float adjustment       = msg.listening_volume_adjustment(i).volume_adjustment();

		const Channel *listenedChannel = Channel::get(channelID);
		if (listenedChannel && pSelf && pSelf->uiSession == pDst->uiSession) {
			Global::get().channelListenerManager->setListenerVolumeAdjustment(pDst->uiSession, listenedChannel->iId,
																			  VolumeAdjustment::fromFactor(adjustment));
		} else if (!listenedChannel) {
			qWarning("msgUserState(): Invalid channel ID encountered in volume adjustment");
		}
	}
	appendUserStateTrace(QStringLiteral("post-listening"));

	if (msg.has_name()) {
		QString oldName = pDst->qsName;
		QString newName = u8(msg.name());
		pmModel->renameUser(pDst, newName);
		renamedUser = !oldName.isNull() && oldName != newName;
		if (!oldName.isNull() && oldName != newName) {
			if (pSrc != pDst) {
				Global::get().l->log(Log::UserRenamed, tr("%1 renamed to %2 by %3.")
														   .arg(Log::formatClientUser(pDst, Log::Target, oldName))
														   .arg(Log::formatClientUser(pDst, Log::Target))
														   .arg(Log::formatClientUser(pSrc, Log::Source)));
			} else {
				Global::get().l->log(Log::UserRenamed, tr("%1 renamed to %2.")
														   .arg(Log::formatClientUser(pDst, Log::Target, oldName),
																Log::formatClientUser(pDst, Log::Target)));
			}
		}
	}
	appendUserStateTrace(QStringLiteral("post-name"));

	if (UserLocalPreferenceLoader *loader = userLocalPreferenceLoader(this, pmModel)) loader->request(pDst);
	appendUserStateTrace(QStringLiteral("post-local-flags"));

	if (msg.has_self_deaf() || msg.has_self_mute()) {
		if (msg.has_self_mute()) {
			pDst->setSelfMute(msg.self_mute());
		}

		if (msg.has_self_deaf()) {
			pDst->setSelfDeaf(msg.self_deaf());
		}

		if (pSelf && pDst != pSelf && pDst->cChannel == pSelf->cChannel) {
			if (pDst->bSelfMute && pDst->bSelfDeaf) {
				Global::get().l->log(Log::OtherSelfMute,
									 tr("%1 is now muted and deafened.").arg(Log::formatClientUser(pDst, Log::Target)));
			} else if (pDst->bSelfMute) {
				Global::get().l->log(Log::OtherSelfMute,
									 tr("%1 is now muted.").arg(Log::formatClientUser(pDst, Log::Target)));
			} else {
				Global::get().l->log(Log::OtherSelfMute,
									 tr("%1 is now unmuted.").arg(Log::formatClientUser(pDst, Log::Target)));
			}
		}
	}
	appendUserStateTrace(QStringLiteral("post-self-mute"));

	if (msg.has_recording()) {
		pDst->setRecording(msg.recording());

		// Do nothing during initial sync
		if (pSelf) {
			if (pDst == pSelf) {
				if (pDst->bRecording) {
					Global::get().l->log(Log::Recording, tr("Recording started"));
				} else {
					Global::get().l->log(Log::Recording, tr("Recording stopped"));
				}
			} else if (pDst->cChannel->allLinks().contains(pSelf->cChannel)) {
				if (pDst->bRecording) {
					Global::get().l->log(Log::Recording,
										 tr("%1 started recording.").arg(Log::formatClientUser(pDst, Log::Source)));
				} else {
					Global::get().l->log(Log::Recording,
										 tr("%1 stopped recording.").arg(Log::formatClientUser(pDst, Log::Source)));
				}
			}
		}
	}
	appendUserStateTrace(QStringLiteral("post-recording"));

	if (msg.has_priority_speaker()) {
		if (pSelf
			&& ((pDst->cChannel == pSelf->cChannel) || (pDst->cChannel->allLinks().contains(pSelf->cChannel))
				|| (pSrc == pSelf))) {
			if ((pSrc == pDst) && (pSrc == pSelf)) {
				if (pDst->bPrioritySpeaker) {
					Global::get().l->log(Log::YouMuted, tr("You revoked your priority speaker status."));
				} else {
					Global::get().l->log(Log::YouMuted, tr("You assumed priority speaker status."));
				}
			} else if ((pSrc != pSelf) && (pDst == pSelf)) {
				if (pDst->bPrioritySpeaker) {
					Global::get().l->log(
						Log::YouMutedOther,
						tr("%1 revoked your priority speaker status.").arg(Log::formatClientUser(pSrc, Log::Source)));
				} else {
					Global::get().l->log(
						Log::YouMutedOther,
						tr("%1 gave you priority speaker status.").arg(Log::formatClientUser(pSrc, Log::Source)));
				}
			} else if ((pSrc == pSelf) && (pSrc != pDst)) {
				if (pDst->bPrioritySpeaker) {
					Global::get().l->log(Log::YouMutedOther, tr("You revoked priority speaker status for %1.")
																 .arg(Log::formatClientUser(pDst, Log::Target)));
				} else {
					Global::get().l->log(
						Log::YouMutedOther,
						tr("You gave priority speaker status to %1.").arg(Log::formatClientUser(pDst, Log::Target)));
				}
			} else if ((pSrc == pDst) && (pSrc != pSelf)) {
				if (pDst->bPrioritySpeaker) {
					Global::get().l->log(
						Log::OtherMutedOther,
						tr("%1 revoked own priority speaker status.").arg(Log::formatClientUser(pSrc, Log::Source)));
				} else {
					Global::get().l->log(
						Log::OtherMutedOther,
						tr("%1 assumed priority speaker status.").arg(Log::formatClientUser(pSrc, Log::Source)));
				}
			} else if ((pSrc != pSelf) && (pDst != pSelf)) {
				if (pDst->bPrioritySpeaker) {
					Global::get().l->log(Log::OtherMutedOther, tr("%1 revoked priority speaker status for %2.")
																   .arg(Log::formatClientUser(pSrc, Log::Source),
																		Log::formatClientUser(pDst, Log::Target)));
				} else if (!pDst->bPrioritySpeaker) {
					Global::get().l->log(Log::OtherMutedOther, tr("%1 gave priority speaker status to %2.")
																   .arg(Log::formatClientUser(pSrc, Log::Source),
																		Log::formatClientUser(pDst, Log::Target)));
				}
			}
		}

		pDst->setPrioritySpeaker(msg.priority_speaker());
	}
	appendUserStateTrace(QStringLiteral("post-priority"));

	if (msg.has_deaf() || msg.has_mute() || msg.has_suppress()) {
		if (msg.has_mute())
			pDst->setMute(msg.mute());
		if (msg.has_deaf())
			pDst->setDeaf(msg.deaf());
		if (msg.has_suppress())
			pDst->setSuppress(msg.suppress());

		if (pSelf
			&& ((pDst->cChannel == pSelf->cChannel) || (pDst->cChannel->allLinks().contains(pSelf->cChannel))
				|| (pSrc == pSelf))) {
			if (pDst == pSelf) {
				if (msg.has_mute() && msg.has_deaf() && pDst->bMute && pDst->bDeaf) {
					Global::get().l->log(
						Log::YouMuted,
						tr("You were muted and deafened by %1.").arg(Log::formatClientUser(pSrc, Log::Source)));
				} else if (msg.has_mute() && msg.has_deaf() && !pDst->bMute && !pDst->bDeaf) {
					Global::get().l->log(
						Log::YouMuted,
						tr("You were unmuted and undeafened by %1.").arg(Log::formatClientUser(pSrc, Log::Source)));
				} else {
					if (msg.has_mute()) {
						if (pDst->bMute)
							Global::get().l->log(
								Log::YouMuted,
								tr("You were muted by %1.").arg(Log::formatClientUser(pSrc, Log::Source)));
						else
							Global::get().l->log(
								Log::YouMuted,
								tr("You were unmuted by %1.").arg(Log::formatClientUser(pSrc, Log::Source)));
					}

					if (msg.has_deaf()) {
						if (!pDst->bDeaf)
							Global::get().l->log(
								Log::YouMuted,
								tr("You were undeafened by %1.").arg(Log::formatClientUser(pSrc, Log::Source)));
					}
				}

				if (msg.has_suppress()) {
					if (pDst->bSuppress)
						Global::get().l->log(Log::YouMuted, tr("You were suppressed."));
					else {
						if (msg.has_channel_id())
							Global::get().l->log(Log::YouMuted, tr("You were unsuppressed."));
						else
							Global::get().l->log(
								Log::YouMuted,
								tr("You were unsuppressed by %1.").arg(Log::formatClientUser(pSrc, Log::Source)));
					}
				}
			} else if (pSrc == pSelf) {
				if (msg.has_mute() && msg.has_deaf() && pDst->bMute && pDst->bDeaf) {
					Global::get().l->log(
						Log::YouMutedOther,
						tr("You muted and deafened %1.").arg(Log::formatClientUser(pDst, Log::Target)));
				} else if (msg.has_mute() && msg.has_deaf() && !pDst->bMute && !pDst->bDeaf) {
					Global::get().l->log(
						Log::YouMutedOther,
						tr("You unmuted and undeafened %1.").arg(Log::formatClientUser(pDst, Log::Target)));
				} else {
					if (msg.has_mute()) {
						if (pDst->bMute)
							Global::get().l->log(Log::YouMutedOther,
												 tr("You muted %1.").arg(Log::formatClientUser(pDst, Log::Target)));
						else
							Global::get().l->log(Log::YouMutedOther,
												 tr("You unmuted %1.").arg(Log::formatClientUser(pDst, Log::Target)));
					}

					if (msg.has_deaf()) {
						if (!pDst->bDeaf)
							Global::get().l->log(
								Log::YouMutedOther,
								tr("You undeafened %1.").arg(Log::formatClientUser(pDst, Log::Target)));
					}
				}

				if (msg.has_suppress()) {
					if (!msg.has_channel_id()) {
						if (pDst->bSuppress)
							Global::get().l->log(
								Log::YouMutedOther,
								tr("You suppressed %1.").arg(Log::formatClientUser(pDst, Log::Target)));
						else
							Global::get().l->log(
								Log::YouMutedOther,
								tr("You unsuppressed %1.").arg(Log::formatClientUser(pDst, Log::Target)));
					}
				}
			} else {
				if (msg.has_mute() && msg.has_deaf() && pDst->bMute && pDst->bDeaf) {
					Global::get().l->log(Log::OtherMutedOther, tr("%1 muted and deafened by %2.")
																   .arg(Log::formatClientUser(pDst, Log::Target),
																		Log::formatClientUser(pSrc, Log::Source)));
				} else if (msg.has_mute() && msg.has_deaf() && !pDst->bMute && !pDst->bDeaf) {
					Global::get().l->log(Log::OtherMutedOther, tr("%1 unmuted and undeafened by %2.")
																   .arg(Log::formatClientUser(pDst, Log::Target),
																		Log::formatClientUser(pSrc, Log::Source)));
				} else {
					if (msg.has_mute()) {
						if (pDst->bMute)
							Global::get().l->log(Log::OtherMutedOther,
												 tr("%1 muted by %2.")
													 .arg(Log::formatClientUser(pDst, Log::Target),
														  Log::formatClientUser(pSrc, Log::Source)));
						else
							Global::get().l->log(Log::OtherMutedOther,
												 tr("%1 unmuted by %2.")
													 .arg(Log::formatClientUser(pDst, Log::Target),
														  Log::formatClientUser(pSrc, Log::Source)));
					}

					if (msg.has_deaf()) {
						if (!pDst->bDeaf)
							Global::get().l->log(Log::OtherMutedOther,
												 tr("%1 undeafened by %2.")
													 .arg(Log::formatClientUser(pDst, Log::Target),
														  Log::formatClientUser(pSrc, Log::Source)));
					}
				}

				if (msg.has_suppress()) {
					if (!msg.has_channel_id()) {
						if (pDst->bSuppress)
							Global::get().l->log(Log::OtherMutedOther,
												 tr("%1 suppressed by %2.")
													 .arg(Log::formatClientUser(pDst, Log::Target),
														  Log::formatClientUser(pSrc, Log::Source)));
						else
							Global::get().l->log(Log::OtherMutedOther,
												 tr("%1 unsuppressed by %2.")
													 .arg(Log::formatClientUser(pDst, Log::Target),
														  Log::formatClientUser(pSrc, Log::Source)));
					}
				}
			}
		}
	}
	appendUserStateTrace(QStringLiteral("post-server-mute"));

	const bool textureChanged = msg.has_texture_hash() || msg.has_texture();
	const bool commentChanged = msg.has_comment_hash() || msg.has_comment();

	if (msg.has_texture_hash()) {
		handleUserTextureHash(pDst, blob(msg.texture_hash()));
	}
	if (msg.has_texture()) {
		handleUserTextureBlob(pDst, blob(msg.texture()));
	}
	if (msg.has_comment_hash())
		pmModel->setCommentHash(pDst, blob(msg.comment_hash()));
	if (msg.has_comment()) {
		pmModel->setComment(pDst, u8(msg.comment()));
	}
	if (commentChanged) {
		clearUserCommentRequest(pDst->uiSession);
	}
	appendUserStateTrace(QStringLiteral("post-comment"));

	{
		const PersistentChatTarget activeTarget = currentPersistentChatTarget();
		const bool activeDirectMessageAffected =
			activeTarget.directMessage && activeTarget.user && activeTarget.user->uiSession == pDst->uiSession;
		const bool rebuildConversationList = createdUser || renamedUser || (movedChannels && pDst == pSelf);

		if (rebuildConversationList) {
			rebuildPersistentChatChannelList();
		}
		if (rebuildConversationList || movedChannels || activeDirectMessageAffected || persistentIdentityChanged) {
			updateMenuPermissions();
			if (!rebuildConversationList) {
				publishQmlParticipantState(pDst);
			}
		}
		if (textureChanged || commentChanged) {
			publishQmlParticipantState(pDst);
		}
	}
	appendUserStateTrace(QStringLiteral("exit"));
}

/// This message is being received when a user was removed. This might be because the user disconnected or because
/// of a kick/ban. The affected user might be the local user.
/// This function will update the local user representation (UserModel) to match these removals and potentially inform
/// the local user about a kick/ban.
///
/// @param msg The message object containing further information
void MainWindow::msgUserRemove(const MumbleProto::UserRemove &msg) {
	VICTIM_INIT;
	ACTOR_INIT;
	SELF_INIT;

	QString reason = u8(msg.reason()).toHtmlEscaped();
	if (m_pendingChatHistoryGrant && m_pendingChatHistoryGrant->session == pDst->uiSession) {
		failPendingChatHistoryGrant(
			QStringLiteral("target_disconnected"),
			tr("The target user disconnected before the server confirmed the chat history grant."));
	}

	if (pDst == pSelf) {
		bRetryServer = false;
		if (msg.ban())
			Global::get().l->log(Log::YouKicked, tr("You were kicked and banned from the server by %1: %2.")
													 .arg(Log::formatClientUser(pSrc, Log::Source))
													 .arg(reason));
		else
			Global::get().l->log(Log::YouKicked, tr("You were kicked from the server by %1: %2.")
													 .arg(Log::formatClientUser(pSrc, Log::Source))
													 .arg(reason));
	} else if (pSrc) {
		if (msg.ban())
			Global::get().l->log((pSrc == pSelf) ? Log::YouKicked : Log::UserKicked,
								 tr("%3 was kicked and banned from the server by %1: %2.")
									 .arg(Log::formatClientUser(pSrc, Log::Source))
									 .arg(reason)
									 .arg(Log::formatClientUser(pDst, Log::Target)));
		else
			Global::get().l->log((pSrc == pSelf) ? Log::YouKicked : Log::UserKicked,
								 tr("%3 was kicked from the server by %1: %2.")
									 .arg(Log::formatClientUser(pSrc, Log::Source))
									 .arg(reason)
									 .arg(Log::formatClientUser(pDst, Log::Target)));
	} else {
		if (pDst->cChannel == pSelf->cChannel || pDst->cChannel->allLinks().contains(pSelf->cChannel)) {
			Global::get().l->log(Log::ChannelLeaveDisconnect,
								 tr("%1 left channel and disconnected.").arg(Log::formatClientUser(pDst, Log::Source)));
		} else {
			Global::get().l->log(Log::UserLeave, tr("%1 disconnected.").arg(Log::formatClientUser(pDst, Log::Source)));
		}
	}

	if (pDst != pSelf) {
		clearUserTextureRequest(pDst->uiSession);
		pmModel->removeUser(pDst);
		rebuildPersistentChatChannelList();
		updateMenuPermissions();
	}
}

/// This message is being received when the server informs the local client about channel properties (either during
/// connection/login to the server or whenever these properties changed).
///
/// @param msg The message object containing the details about the channel properties
void MainWindow::msgChannelState(const MumbleProto::ChannelState &msg) {
	if (!msg.has_channel_id())
		return;

	Channel *c = Channel::get(msg.channel_id());
	Channel *p = msg.has_parent() ? Channel::get(msg.parent()) : nullptr;

	if (!c) {
		// Addresses channel does not exist so create it
		if (p && msg.has_name()) {
			c = pmModel->addChannel(msg.channel_id(), p, u8(msg.name()));
			if (!c) {
				qWarning("Server attempted to create an invalid or duplicate channel");
				return;
			}
			c->bTemporary = msg.temporary();
			p             = nullptr; // No need to move it later

			ServerHandlerPtr sh = Global::get().sh;
			if (ServerScopedDatabaseLoader *loader = serverScopedDatabaseLoader(this, pmModel)) {
				loader->requestChannelFilter(sh, c);
			}

		} else {
			qWarning("Server attempted state change on nonexistent channel");
			return;
		}
	}

	if (p) {
		// Channel move
		Channel *pp = p;
		while (pp) {
			if (pp == c) {
				qWarning("Server asked to move a channel into itself or one of its children");
				return;
			}

			pp = pp->cParent;
		}
		pmModel->moveChannel(c, p);
	}

	if (msg.has_name()) {
		pmModel->renameChannel(c, u8(msg.name()));
	}

	if (msg.has_description_hash()) {
		pmModel->setCommentHash(c, blob(msg.description_hash()));
	}
	if (msg.has_description()) {
		pmModel->setComment(c, u8(msg.description()));
	}

	if (msg.has_position()) {
		pmModel->repositionChannel(c, msg.position());
	}

	if (msg.links_size()) {
		QList< Channel * > ql;
		pmModel->unlinkAll(c);
		for (int i = 0; i < msg.links_size(); ++i) {
			Channel *l = Channel::get(msg.links(i));
			if (l)
				ql << l;
		}
		if (!ql.isEmpty()) {
			pmModel->linkChannels(c, ql);
		}
	}
	if (msg.links_remove_size()) {
		QList< Channel * > ql;
		for (int i = 0; i < msg.links_remove_size(); ++i) {
			Channel *l = Channel::get(msg.links_remove(i));
			if (l)
				ql << l;
		}
		if (!ql.isEmpty()) {
			pmModel->unlinkChannels(c, ql);
		}
	}
	if (msg.links_add_size()) {
		QList< Channel * > ql;
		for (int i = 0; i < msg.links_add_size(); ++i) {
			Channel *l = Channel::get(msg.links_add(i));
			if (l)
				ql << l;
		}
		if (!ql.isEmpty()) {
			pmModel->linkChannels(c, ql);
		}
	}

	if (msg.has_max_users()) {
		c->uiMaxUsers = msg.max_users();
	}

	bool forceUpdateTree = false;

	if (msg.has_is_enter_restricted()) {
		c->hasEnterRestrictions.store(msg.is_enter_restricted());
		forceUpdateTree = true;
	}

	if (msg.has_can_enter()) {
		c->localUserCanEnter.store(msg.can_enter());
		forceUpdateTree = true;
	}

	emit channelStateChanged(c, forceUpdateTree);
}

void MainWindow::msgChannelRemove(const MumbleProto::ChannelRemove &msg) {
	Channel *c = Channel::get(msg.channel_id());
	if (c && (c->iId != 0)) {
		c->clearFilterMode();
		if (!pmModel->removeChannel(c, true)) {
			Global::get().l->log(Log::CriticalError,
								 tr("Protocol violation. Server sent remove for occupied channel."));
			Global::get().sh->disconnect();
			return;
		}
	}
}

/// This message is being received because the local client received a text message that should be displayed to
/// the user - which is what this function does.
///
/// @param msg The message object that contains information about the received text message
void MainWindow::msgTextMessage(const MumbleProto::TextMessage &msg) {
	ACTOR_INIT;
	QString target;

	// Silently drop the message if this user is set to "ignore"
	if (pSrc && pSrc->bLocalIgnore)
		return;

	const QString &plainName = pSrc ? pSrc->qsName : tr("Server", "message from");
	const QString &name      = pSrc ? Log::formatClientUser(pSrc, Log::Source) : tr("Server", "message from");
	bool privateMessage      = false;

	if (msg.tree_id_size() > 0) {
		target += tr("Tree");
	} else if (msg.channel_id_size() > 0) {
		target += tr("Channel");
	} else if (msg.session_size() > 0) {
		target += tr("Private");
		privateMessage = true;
	}

	// If NoScope or NoAuthor is selected generate a new string to pass to TTS
	const QString overrideTTS = [&]() {
		if (!Global::get().s.bTTSNoScope && !Global::get().s.bTTSNoAuthor) {
			return QString();
		}
		const QString plainMessage = QTextDocumentFragment::fromHtml(u8(msg.message())).toPlainText();
		if (Global::get().s.bTTSNoScope && Global::get().s.bTTSNoAuthor) {
			return plainMessage;
		}
		const QString prefixTTS = Global::get().s.bTTSNoScope ? plainName : target;
		return tr("%1: %2").arg(prefixTTS).arg(plainMessage);
	}();

	const QString prefixMessage = target.isEmpty() ? name : tr("(%1) %2").arg(target).arg(name);

	Global::get().l->log(privateMessage ? Log::PrivateTextMessage : Log::TextMessage,
						 tr("%1: %2").arg(prefixMessage).arg(u8(msg.message())), tr("Message from %1").arg(plainName),
						 false, overrideTTS, pSrc ? pSrc->bLocalIgnoreTTS : false);

	if (true && privateMessage && pSrc) {
		appendModernDirectMessage(pSrc->uiSession, u8(msg.message()), false);
	}
}

/// This message is being received when the server informs the client about the access control list (ACL) for
/// a channel or multiple channels. It seems like this message will only be received after having queried it.
///
/// @param msg The message object holding the ACL and further details
void MainWindow::msgACL(const MumbleProto::ACL &msg) {
	openModernAclDialog(msg);
}

/// This message is being received when the server informs the local client about user information. This message will
/// only be received after being explicitly queried by the local client.
///
/// @param msg The message object with the respective information
void MainWindow::msgQueryUsers(const MumbleProto::QueryUsers &msg) {
	if (handleModernAclQueryUsers(msg)) {
		return;
	}
}

/// Pings are a method to check the server-client connection. This implementation does nothing.
void MainWindow::msgPing(const MumbleProto::Ping &) {
}

void MainWindow::msgCryptSetup(const MumbleProto::CryptSetup &msg) {
	const ServerHandlerPtr serverHandler = Global::get().serverHandlerSnapshot();
	if (serverHandler) {
		serverHandler->applyCryptSetup(msg);
	}
}

/// This messages is only sent by the client if it wants to instantiate a context action. Thus this implementation
/// does nothing.
void MainWindow::msgContextAction(const MumbleProto::ContextAction &) {
}

/// This message is being received if the server wants to instruct the client to add or remove a given context action.
///
/// @param msg The message object with further details about the respective context action
///
/// @see MainWindow::removeContextAction
void MainWindow::msgContextActionModify(const MumbleProto::ContextActionModify &msg) {
	if (msg.has_operation() && msg.operation() == MumbleProto::ContextActionModify_Operation_Remove) {
		removeContextAction(msg);
		updateMenuPermissions();
		return;
	}

	if (msg.has_operation() && msg.operation() != MumbleProto::ContextActionModify_Operation_Add)
		return;

	QAction *a = new QAction(u8(msg.text()), Global::get().mw);
	a->setData(u8(msg.action()));
	connect(a, SIGNAL(triggered()), this, SLOT(context_triggered()));
	unsigned int ctx = msg.context();
	if (ctx & MumbleProto::ContextActionModify_Context_Server)
		qlServerActions.append(a);
	if (ctx & MumbleProto::ContextActionModify_Context_User)
		qlUserActions.append(a);
	if (ctx & MumbleProto::ContextActionModify_Context_Channel)
		qlChannelActions.append(a);
	updateMenuPermissions();
}

/// Helper method for removing a context action.
///
/// @param msg The message object instructing the deletion of the action with further information about it
///
/// @see MainWindow::msgContextActionModify
void MainWindow::removeContextAction(const MumbleProto::ContextActionModify &msg) {
	QString action = u8(msg.action());

	QSet< QAction * > qs;
	qs += QSet< QAction * >(qlServerActions.begin(), qlServerActions.end());
	qs += QSet< QAction * >(qlChannelActions.begin(), qlChannelActions.end());
	qs += QSet< QAction * >(qlUserActions.begin(), qlUserActions.end());

	for (QAction *a : qs) {
		if (a->data() == action) {
			qlServerActions.removeOne(a);
			qlChannelActions.removeOne(a);
			qlUserActions.removeOne(a);
			delete a;
		}
	}
}

/// This message is being received in order to set the version information of this client.
///
/// @param msg The message object with the respective information
void MainWindow::msgVersion(const MumbleProto::Version &msg) {
	const ServerHandlerPtr serverHandler = Global::get().serverHandlerSnapshot();
	if (msg.supported_chat_features_size() > 0
		|| (msg.has_supports_persistent_chat() && msg.supports_persistent_chat())) {
		Global::get().qlSupportedChatFeatures = Mumble::ChatFeatures::featuresFromVersion(msg);
		if (Mumble::ChatFeatures::contains(Global::get().qlSupportedChatFeatures,
										   MumbleProto::ChatFeaturePersistentHistory)) {
			Global::get().uiPersistentChatProtocolVersion = msg.has_persistent_chat_protocol_version()
				? msg.persistent_chat_protocol_version() : 1U;
			markPersistentChatAvailable(false);
			m_modernLayoutCompatibleServer = true;
		}
	}
	if (msg.supported_fork_features_size() > 0 || msg.has_fork_extension_protocol_version()) {
		Global::get().qlSupportedForkFeatures      = Mumble::ForkFeatures::featuresFromVersion(msg);
		Global::get().uiForkExtensionProtocolVersion =
			msg.has_fork_extension_protocol_version() ? msg.fork_extension_protocol_version() : 0;
		m_modernLayoutCompatibleServer = true;
	}

	if (serverHandler) {
		serverHandler->setServerIdentityDetails(msg.has_release() ? u8(msg.release()) : QString(),
											msg.has_os() ? u8(msg.os()) : QString(),
											msg.has_os() && msg.has_os_version() ? u8(msg.os_version()) : QString());
	}
}

/// This message is being received if the client has queried for the list of all users.
///
/// @param msg The message object containing the user list
void MainWindow::msgUserList(const MumbleProto::UserList &msg) {
	if (handleModernAclUserList(msg)) {
		return;
	}
	openModernServerUserListDialog(msg);
}

/// This message is only sent by the client in order to register/clear whisper targets. Therefore
/// this implementation does nothing.
void MainWindow::msgVoiceTarget(const MumbleProto::VoiceTarget &) {
}

/// This message is being received as an answer to the request for certain permissions or if the
/// server wants the client to resync all channel permissions.
///
/// @param msg The message object containing the respective information
void MainWindow::msgPermissionQuery(const MumbleProto::PermissionQuery &msg) {
	const auto selectedVoiceChannel = selectedModernVoiceChannel();
	Channel *current = selectedVoiceChannel ? Channel::get(*selectedVoiceChannel) : nullptr;
	const PersistentChatTarget activeChatTarget = currentPersistentChatTarget();
	const bool activeChatTargetMatchesPermissionChannel =
		activeChatTarget.valid && !activeChatTarget.serverLog && !activeChatTarget.directMessage
		&& !activeChatTarget.ephemeralTextPath && activeChatTarget.channel
		&& activeChatTarget.channel->iId == msg.channel_id();
	const ChanACL::Permissions previousActiveChatPermissions =
		activeChatTargetMatchesPermissionChannel
			? static_cast< ChanACL::Permissions >(activeChatTarget.channel->uiPermissions)
			: ChanACL::None;

	if (msg.flush()) {
		for (Channel *c : Channel::c_qhChannels) {
			c->uiPermissions = 0;
		}

		// We always need the permissions of the current focus channel
		if (current && current->iId != msg.channel_id()) {
			Global::get().sh->requestChannelPermissions(current->iId);

			current->uiPermissions = ChanACL::All;
		}
	}
	Channel *c = Channel::get(msg.channel_id());
	if (c) {
		c->uiPermissions = msg.permissions();
		if (c->iId == 0)
			Global::get().pPermissions = static_cast< ChanACL::Permissions >(c->uiPermissions);
		if (c->iId == 0) {
			syncModernServerAdminPermissions();
			handleStonksPermissionUpdate();
		}
		if (c == current) updateMenuPermissions();
		scheduleQmlRoomStateUpdate();
		if (activeChatTargetMatchesPermissionChannel && activeChatTarget.channel == c
			&& previousActiveChatPermissions != static_cast< ChanACL::Permissions >(c->uiPermissions)) {
			refreshPersistentChatView(true);
		}
	}
}

/// This message is being received in order for the server to instruct this client which codec it should use.
///
/// @param msg The message object
void MainWindow::msgCodecVersion(const MumbleProto::CodecVersion &msg) {
	if (!msg.opus()) {
		Global::get().l->log(Log::CriticalError, tr("Server instructed us to use an audio codec different from Opus, "
													"which is no longer supported. Disconnecting..."));

		Global::get().sh->disconnect();
	}
}

/// This message is being received in order to communicate user stats from the server to the client.
///
/// @param msg The message object containing the stats
void MainWindow::msgUserStats(const MumbleProto::UserStats &msg) {
	if (msg.has_session()) {
		const std::optional< unsigned int > idleSeconds =
			msg.has_idlesecs() ? std::optional< unsigned int >(msg.idlesecs()) : std::nullopt;
		const auto previousIdleSeconds = userIdleSeconds(msg.session());
		if (idleSeconds) {
			m_userIdleSeconds.insert(msg.session(), *idleSeconds);
		} else {
			m_userIdleSeconds.remove(msg.session());
		}

		if (idleSeconds != previousIdleSeconds && pmModel) {
			if (ClientUser *user = ClientUser::get(msg.session()); user) {
				const QModelIndex idx = pmModel->index(user);
				if (idx.isValid()) {
					emit pmModel->dataChanged(idx, idx);
				}
			}
		}
	}

	if (msg.stats_only()) {
		return;
	}

	if (!m_pendingUserInformationSessions.remove(msg.session())) {
		return;
	}

	openModernUserInformationDialog(msg);
}

/// This message is only ever sent by the client in order to request binary data that otherwise
/// wouldn't be included in the normal messages (e.Global::get(). big images). Thus this implementation does
/// nothing.
void MainWindow::msgRequestBlob(const MumbleProto::RequestBlob &) {
}

/// This message is being received when the server wants to inform the client about suggested client configurations
/// made by the server administrator. These suggestions will be logged to Mumble's console (if unmet).
///
/// @param msg The message object containing the suggestions
void MainWindow::msgSuggestConfig(const MumbleProto::SuggestConfig &msg) {
	Version::full_t requestedVersion = MumbleProto::getSuggestedVersion(msg);
	if (requestedVersion <= Version::get()) {
		requestedVersion = Version::UNKNOWN;
	}
	if (requestedVersion != Version::UNKNOWN) {
		Global::get().l->log(
			Log::Warning, tr("The server requests minimum client version %1").arg(Version::toString(requestedVersion)));
	}
	if (msg.has_positional() && (msg.positional() != Global::get().s.doPositionalAudio())) {
		if (msg.positional())
			Global::get().l->log(Log::Warning, tr("The server requests positional audio be enabled."));
		else
			Global::get().l->log(Log::Warning, tr("The server requests positional audio be disabled."));
	}
	if (msg.has_push_to_talk() && (msg.push_to_talk() != (Global::get().s.atTransmit == Settings::PushToTalk))) {
		if (msg.push_to_talk())
			Global::get().l->log(Log::Warning, tr("The server requests Push-to-Talk be enabled."));
		else
			Global::get().l->log(Log::Warning, tr("The server requests Push-to-Talk be disabled."));
	}
}

void MainWindow::msgPluginDataTransmission(const MumbleProto::PluginDataTransmission &msg) {
	// Another client's plugin has sent us some data. Verify the necessary parts are there and delegate it to the
	// PluginManager

	if (!msg.has_sendersession() || !msg.has_data() || !msg.has_dataid()) {
		// if the message contains no sender session, no data or no ID for the data, it is of no use to us and we
		// discard it
		return;
	}

	const ClientUser *sender   = ClientUser::get(msg.sendersession());
	const std::string &msgData = msg.data();

	if (sender) {
		static_assert(sizeof(unsigned char) == sizeof(uint8_t), "Unsigned char does not have expected 8bit size");
		// As long as above assertion is true, we are only casting away the sign, which is fine
		Global::get().pluginManager->on_receiveData(sender, reinterpret_cast< const uint8_t * >(msgData.c_str()),
													msgData.size(), msg.dataid().c_str());
	}
}

void MainWindow::msgChatSend(const MumbleProto::ChatSend &) {
}

void MainWindow::msgChatMessage(const MumbleProto::ChatMessage &msg) {
	markPersistentChatAvailable();
	m_persistentChatLiveMessageKeys.insert(persistentChatMessageIdentityKey(msg));
	handlePersistentChatMessage(msg);
}

void MainWindow::msgChatMessageDelete(const MumbleProto::ChatMessageDelete &) {
}

void MainWindow::msgChatHistoryRequest(const MumbleProto::ChatHistoryRequest &) {
}

void MainWindow::msgChatHistoryWarmupRequest(const MumbleProto::ChatHistoryWarmupRequest &) {
}

void MainWindow::msgChatHistoryResponse(const MumbleProto::ChatHistoryResponse &msg) {
	markPersistentChatAvailable();
	handlePersistentChatHistory(msg);
}

void MainWindow::msgChatReadStateUpdate(const MumbleProto::ChatReadStateUpdate &msg) {
	markPersistentChatAvailable();
	handlePersistentChatReadState(msg);
}

void MainWindow::msgChatAssetUploadInit(const MumbleProto::ChatAssetUploadInit &) {
}

void MainWindow::msgChatAssetUploadChunk(const MumbleProto::ChatAssetUploadChunk &) {
}

void MainWindow::msgChatAssetUploadCommit(const MumbleProto::ChatAssetUploadCommit &) {
}

void MainWindow::msgChatAssetState(const MumbleProto::ChatAssetState &msg) {
	if (m_chatAttachmentUploader) {
		m_chatAttachmentUploader->handleState(msg);
	}
}

void MainWindow::msgChatAssetRequest(const MumbleProto::ChatAssetRequest &) {
}

void MainWindow::msgChatAssetChunk(const MumbleProto::ChatAssetChunk &msg) {
	mumble::chatperf::ScopedDuration trace("chat.asset_chunk");
	if (!msg.has_asset_id() || msg.asset_id() == 0) {
		return;
	}

	auto it = m_persistentChatAssetDownloads.find(msg.asset_id());
	if (it == m_persistentChatAssetDownloads.end()) {
		// Asset chunks are accepted only for transfers initiated by this client.
		return;
	}
	it->requestPending = false;
	const quint64 configuredLimit = Global::get().uiChatAssetMaxBytes > 0
		? Global::get().uiChatAssetMaxBytes : 25ULL * 1024ULL * 1024ULL;
	const quint64 transferLimit = it->maximumBytes > 0 ? it->maximumBytes : configuredLimit;
	const quint64 memoryLimit = std::min< quint64 >(transferLimit, 64ULL * 1024ULL * 1024ULL);
	const auto rejectDownload = [this, &it](const QString &reason) {
		const bool userRequested = !it->savePaths.isEmpty() || !it->openFileName.isEmpty()
			|| !it->fullImageAssetIDs.isEmpty();
		const QSet< unsigned int > attachmentAssetIDs = it->attachmentAssetIDs;
		const QSet< unsigned int > fullImageAssetIDs = it->fullImageAssetIDs;
		const QSet< QString > attachmentMessageKeys = it->attachmentMessageKeys;
		const QHash< QString, QString > saveOperationIDsByPath = it->saveOperationIDsByPath;
		const QString openOperationID = it->openOperationID;
		m_persistentChatAssetDownloads.erase(it);
		if (m_qmlShellHost && m_qmlShellHost->operationModel()) {
			AsyncOperationModel *operations = m_qmlShellHost->operationModel();
			for (const QString &operationID : saveOperationIDsByPath) {
				operations->finishOperation(operationID, false, QStringLiteral("download-error"), reason);
			}
			if (!openOperationID.isEmpty()) {
				operations->finishOperation(openOperationID, false, QStringLiteral("download-error"), reason);
			}
		}
		m_persistentChatAttachmentPreviewFailures.unite(attachmentAssetIDs);
		m_persistentChatAttachmentFullImageFailures.unite(fullImageAssetIDs);
		publishPersistentChatAttachmentImageUpdate(attachmentMessageKeys);
		schedulePendingPersistentChatAttachmentImageDownloads();
		if (userRequested) {
			publishModernToast(QStringLiteral("error"), tr("Attachment download failed"), reason);
		}
	};

	if (msg.has_offset() && msg.offset() != static_cast< quint64 >(it->bytes.size())) {
		rejectDownload(tr("The server returned attachment data out of order."));
		return;
	}

	if (msg.has_total_size()) {
		if (msg.total_size() == 0 || msg.total_size() > memoryLimit
			|| (it->totalSize > 0 && it->totalSize != msg.total_size())) {
			rejectDownload(tr("The attachment size is invalid or exceeds the download limit."));
			return;
		}
		it->totalSize = msg.total_size();
	}
	if (msg.has_mime()) {
		it->mime = normalizedChatAssetMime(u8(msg.mime()));
	}
	if (msg.has_kind()) {
		it->kind = msg.kind();
	}
	if (msg.has_data() && !msg.data().empty()) {
		const quint64 incomingBytes = static_cast< quint64 >(msg.data().size());
		const quint64 accumulatedBytes = static_cast< quint64 >(it->bytes.size());
		quint64 globallyBufferedBytes = 0;
		for (auto downloadIt = m_persistentChatAssetDownloads.cbegin();
			 downloadIt != m_persistentChatAssetDownloads.cend(); ++downloadIt) {
			globallyBufferedBytes += static_cast< quint64 >(downloadIt->bytes.size());
		}
		if (incomingBytes > memoryLimit || accumulatedBytes > memoryLimit - incomingBytes
			|| globallyBufferedBytes > 64ULL * 1024ULL * 1024ULL - incomingBytes
			|| (it->totalSize > 0 && accumulatedBytes + incomingBytes > it->totalSize)) {
			rejectDownload(tr("The server returned more attachment data than expected."));
			return;
		}
		it->bytes.append(msg.data().data(), static_cast< int >(msg.data().size()));
	}
	it->nextOffset = static_cast< quint64 >(it->bytes.size());
	if (m_qmlShellHost && m_qmlShellHost->operationModel()) {
		AsyncOperationModel *operations = m_qmlShellHost->operationModel();
		const qint64 received = it->bytes.size();
		const qint64 total = static_cast< qint64 >(it->totalSize);
		for (const QString &operationID : std::as_const(it->saveOperationIDsByPath)) {
			operations->updateProgress(operationID, received, total);
		}
		if (!it->openOperationID.isEmpty()) {
			operations->updateProgress(it->openOperationID, received, total);
		}
	}

	const bool complete = (msg.has_eof() && msg.eof())
						  || (it->totalSize > 0 && static_cast< quint64 >(it->bytes.size()) >= it->totalSize);
	if (!complete) {
		if (!msg.has_data() || msg.data().empty()) {
			rejectDownload(tr("The attachment server returned no data for the requested range."));
			return;
		}
		if (Global::get().sh && Global::get().sh->isRunning()) {
			MumbleProto::ChatAssetRequest request;
			request.set_asset_id(msg.asset_id());
			request.set_offset(it->nextOffset);
			request.set_max_bytes(262144);
			it->requestPending = true;
			Global::get().sh->sendMessage(request);
			armPersistentChatAssetDownloadTimeout(msg.asset_id());
		}
		return;
	}
	if (it->totalSize > 0 && static_cast< quint64 >(it->bytes.size()) != it->totalSize) {
		rejectDownload(tr("The attachment download ended before all bytes were received."));
		return;
	}

	mumble::chatperf::recordValue("chat.asset_chunk.bytes", it->bytes.size());
	mumble::chatperf::recordValue("chat.asset_chunk.preview_keys", it->previewKeys.size());

	// Move the completed transfer out of the GUI-owned map before submitting any
	// asynchronous work. This releases the network slot immediately and prevents
	// callbacks from retaining an iterator across model/scope changes.
	PersistentChatAssetDownload download = std::move(*it);
	m_persistentChatAssetDownloads.erase(it);

	const unsigned int assetID = msg.asset_id();
	const QString mime = download.mime;
	const MumbleProto::ChatAssetKind kind = download.kind;
	const QSet< QString > previewKeys = download.previewKeys;
	const QSet< unsigned int > attachmentAssetIDs = download.attachmentAssetIDs;
	const QSet< unsigned int > fullImageAssetIDs = download.fullImageAssetIDs;
	const QSet< QString > attachmentMessageKeys = download.attachmentMessageKeys;
	const QStringList savePaths = download.savePaths;
	const QHash< QString, QString > saveOperationIDsByPath = download.saveOperationIDsByPath;
	const QString openFileName = download.openFileName;
	const QString openOperationID = download.openOperationID;
	QByteArray bytes = std::move(download.bytes);
	const qint64 estimatedBytes = std::max< qint64 >(1, bytes.size());
	const quint64 connectionGeneration = m_persistentChatAssetConnectionGeneration;
	QSet< QString > ioOperationIDs;
	for (const QString &operationID : saveOperationIDsByPath) {
		if (!operationID.isEmpty()) ioOperationIDs.insert(operationID);
	}
	if (!openOperationID.isEmpty()) ioOperationIDs.insert(openOperationID);
	m_persistentChatAttachmentIoOperationIDs.unite(ioOperationIDs);
	const QString workerGroup = QStringLiteral("attachment-io:%1:%2")
		.arg(assetID)
		.arg(QUuid::createUuid().toString(QUuid::WithoutBraces));

	queuePersistentChatAssetIo(
		workerGroup, QStringLiteral("finalize"), estimatedBytes,
		[bytes = std::move(bytes), mime, kind, attachmentAssetIDs, fullImageAssetIDs, previewKeys, savePaths, openFileName,
		 assetID]() mutable {
			PersistentChatAssetIoResult result;
			const bool imageConsumer = !attachmentAssetIDs.isEmpty() || !fullImageAssetIDs.isEmpty()
				|| !previewKeys.isEmpty();
			if (imageConsumer && mime.startsWith(QLatin1String("image/"))) {
				mumble::chatperf::ScopedDuration decodeTrace("chat.asset_chunk.decode");
				QBuffer buffer;
				buffer.setData(bytes);
				if (buffer.open(QIODevice::ReadOnly)) {
					QImageReader reader(&buffer);
					reader.setAutoTransform(true);
					reader.setDecideFormatFromContent(true);
					const QSize sourceSize = reader.size();
					constexpr qint64 maximumPixels = 40LL * 1024LL * 1024LL;
					if (sourceSize.isValid() && sourceSize.width() > 0 && sourceSize.height() > 0
						&& sourceSize.width() <= 16384 && sourceSize.height() <= 16384
						&& static_cast< qint64 >(sourceSize.width()) * sourceSize.height() <= maximumPixels) {
						// Preserve a high-resolution viewer surface while staying inside the
						// QML image pipeline's bounded decoded-image budget. Saved/downloaded
						// bytes remain untouched; only the display copy is downsampled.
						constexpr qint64 maximumDisplayPixels = 8LL * 1024LL * 1024LL;
						if (sourceSize.width() > 8192 || sourceSize.height() > 8192
							|| static_cast< qint64 >(sourceSize.width()) * sourceSize.height()
								> maximumDisplayPixels) {
							const qreal scale = std::min({ 1.0,
								8192.0 / sourceSize.width(), 8192.0 / sourceSize.height(),
								std::sqrt(static_cast< qreal >(maximumDisplayPixels)
									/ (static_cast< qreal >(sourceSize.width()) * sourceSize.height())) });
							reader.setScaledSize(QSize(
								std::max(1, static_cast< int >(std::floor(sourceSize.width() * scale))),
								std::max(1, static_cast< int >(std::floor(sourceSize.height() * scale)))));
						}
						result.image = reader.read();
					}
				}
			}
			if (!previewKeys.isEmpty() && isPersistentChatPlayableMediaMime(mime)) {
				result.mediaDataUrl = persistentChatPlayableMediaDataUrl(mime, bytes);
			}
			if (!result.image.isNull()) {
				result.contentHash = QString::fromLatin1(
					QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
			}
			for (const QString &savePath : savePaths) {
				QSaveFile destination(savePath);
				if (!destination.open(QIODevice::WriteOnly)
					|| destination.write(bytes) != bytes.size() || !destination.commit()) {
					destination.cancelWriting();
					continue;
				}
				result.savedPaths.insert(savePath);
			}
			if (!openFileName.isEmpty()) {
				const QString trustedExtension = trustedChatAttachmentOpenExtension(mime, kind, openFileName);
				if (trustedExtension.isEmpty()) {
					result.openFailureCode = QStringLiteral("unsafe-type");
				} else {
					result.openDirectoryLease =
						mumble::chatattachmentio::TemporaryDirectoryLease::create(
							QDir::temp().filePath(QStringLiteral("mumble-chat-attachments-XXXXXX")),
							static_cast< quint64 >(bytes.size()));
					if (!result.openDirectoryLease) {
						result.openFailureCode = QStringLiteral("temporary-directory-error");
					} else {
						QString baseName = QFileInfo(openFileName).completeBaseName().trimmed();
						baseName.replace(QRegularExpression(QStringLiteral("[\\x00-\\x1f<>:\"/\\\\|?*]+")),
							QStringLiteral("_"));
						if (baseName.isEmpty() || baseName == QLatin1String(".") || baseName == QLatin1String("..")) {
							baseName = QStringLiteral("attachment-%1").arg(assetID);
						}
						result.openPath = QDir(result.openDirectoryLease->path()).filePath(
							QStringLiteral("%1.%2").arg(baseName.left(180), trustedExtension));
						QSaveFile temporaryFile(result.openPath);
						if (!temporaryFile.open(QIODevice::WriteOnly)
							|| temporaryFile.write(bytes) != bytes.size() || !temporaryFile.commit()) {
							temporaryFile.cancelWriting();
							result.openPath.clear();
							result.openDirectoryLease.reset();
							result.openFailureCode = QStringLiteral("temporary-write-error");
						}
					}
				}
			}
			return result;
		},
		[this, assetID, mime, kind, previewKeys, attachmentAssetIDs, fullImageAssetIDs, attachmentMessageKeys, savePaths,
		 saveOperationIDsByPath, openFileName, openOperationID, connectionGeneration, ioOperationIDs](
			std::optional< PersistentChatAssetIoResult > result) {
			if (connectionGeneration != m_persistentChatAssetConnectionGeneration) return;
			for (const QString &operationID : ioOperationIDs) {
				m_persistentChatAttachmentIoOperationIDs.remove(operationID);
			}
			const QImage image = result ? result->image : QImage();
			const QString mediaDataUrl = result ? result->mediaDataUrl : QString();
			if (!attachmentAssetIDs.isEmpty() && !image.isNull() && result
				&& !result->contentHash.isEmpty() && m_qmlShellHost && m_qmlShellHost->imagePipeline()) {
				const QString stableKey = QStringLiteral("chat-attachment:%1:%2")
					.arg(assetID)
					.arg(result->contentHash);
				const QString providerUrl = m_qmlShellHost->imagePipeline()->registerImage(image, stableKey);
				if (!providerUrl.isEmpty()) {
					for (const unsigned int attachmentAssetID : attachmentAssetIDs) {
						m_persistentChatAttachmentProviderUrls.insert(attachmentAssetID, providerUrl);
						m_persistentChatAttachmentPreviewFailures.remove(attachmentAssetID);
					}
				}
			}
			if (!fullImageAssetIDs.isEmpty() && !image.isNull() && result
				&& !result->contentHash.isEmpty() && m_qmlShellHost && m_qmlShellHost->imagePipeline()) {
				const QString stableKey = QStringLiteral("chat-attachment-full:%1:%2")
					.arg(assetID)
					.arg(result->contentHash);
				const QString providerUrl = m_qmlShellHost->imagePipeline()->registerImage(image, stableKey);
				if (!providerUrl.isEmpty()) {
					for (const unsigned int fullImageAssetID : fullImageAssetIDs) {
						m_persistentChatAttachmentFullImageProviderUrls.insert(fullImageAssetID, providerUrl);
						m_persistentChatAttachmentFullImageFailures.remove(fullImageAssetID);
					}
				}
			}
			for (const unsigned int attachmentAssetID : attachmentAssetIDs) {
				if (!m_persistentChatAttachmentProviderUrls.contains(attachmentAssetID)) {
					m_persistentChatAttachmentPreviewFailures.insert(attachmentAssetID);
				}
			}
			for (const unsigned int fullImageAssetID : fullImageAssetIDs) {
				if (!m_persistentChatAttachmentFullImageProviderUrls.contains(fullImageAssetID)) {
					m_persistentChatAttachmentFullImageFailures.insert(fullImageAssetID);
				}
			}

			for (const QString &previewKey : previewKeys) {
				auto previewIt = m_persistentChatPreviews.find(previewKey);
				if (previewIt == m_persistentChatPreviews.end()) continue;
				previewIt->thumbnailFinished = true;
				const QString incomingKind = persistentChatPlayableMediaKind(mime, kind);
				const bool decorativeSocialGif =
					incomingKind == QLatin1String("gif") && isSocialVideoPreviewUrl(previewIt->canonicalUrl);
				if (!mediaDataUrl.isEmpty()) {
					const bool keepExistingVideo =
						previewIt->mediaKind == QLatin1String("video") && incomingKind != QLatin1String("video")
						&& previewIt->mediaDataUrl.startsWith(QLatin1String("https://"), Qt::CaseInsensitive);
					if (!keepExistingVideo && !decorativeSocialGif) {
						previewIt->mediaDataUrl = mediaDataUrl;
						previewIt->mediaMime    = mime;
						previewIt->mediaKind    = incomingKind;
						previewIt->autoplay     = false;
					}
					previewIt->failed = false;
					if (!image.isNull()) previewIt->thumbnailImage = image;
				} else if (!image.isNull()) {
					previewIt->thumbnailImage = image;
					previewIt->failed         = false;
				} else {
					previewIt->failed = true;
					ensurePersistentChatPreviewSiteSnapshot(previewKey);
				}
				ensurePersistentChatPreviewSiteSnapshot(previewKey);
				storePersistentChatPreviewDiskCache(previewKey);
			}

			if (!attachmentMessageKeys.isEmpty()) {
				publishPersistentChatAttachmentImageUpdate(attachmentMessageKeys);
			}
			schedulePendingPersistentChatAttachmentImageDownloads();

			AsyncOperationModel *operations =
				m_qmlShellHost ? m_qmlShellHost->operationModel() : nullptr;
			int savedCount = 0;
			for (const QString &savePath : savePaths) {
				const bool saved = result && result->savedPaths.contains(savePath);
				if (saved) ++savedCount;
				const QString operationID = saveOperationIDsByPath.value(savePath);
				if (operations && !operationID.isEmpty()) {
					operations->finishOperation(operationID, saved,
						saved ? QString() : result ? QStringLiteral("write-error")
											 : QStringLiteral("worker-unavailable"),
						saved ? tr("The chat attachment was saved successfully.")
							  : tr("The attachment could not be written to the selected location."));
				}
			}
			if (!savePaths.isEmpty()) {
				if (savedCount == savePaths.size()) {
					publishModernToast(QStringLiteral("success"), tr("Attachment saved"),
						tr("The chat attachment was saved successfully."));
				} else {
					publishModernToast(QStringLiteral("error"), tr("Attachment download failed"),
						tr("The attachment could not be written to the selected location."));
				}
			}

			if (!openFileName.isEmpty()) {
				bool opened = false;
				QString failureCode = result ? result->openFailureCode : QStringLiteral("worker-unavailable");
				if (result && !result->openPath.isEmpty() && result->openDirectoryLease) {
					opened = QDesktopServices::openUrl(QUrl::fromLocalFile(result->openPath));
					if (opened) {
						m_persistentChatOpenAttachmentDirectoryBytes +=
							result->openDirectoryLease->retainedBytes();
						m_persistentChatOpenAttachmentDirectories.push_back(result->openDirectoryLease);
						constexpr std::size_t maximumRetainedDirectories = 8;
						constexpr quint64 maximumRetainedBytes = 256ULL * 1024ULL * 1024ULL;
						while (m_persistentChatOpenAttachmentDirectories.size() > maximumRetainedDirectories
							|| m_persistentChatOpenAttachmentDirectoryBytes > maximumRetainedBytes) {
							const quint64 evictedBytes =
								m_persistentChatOpenAttachmentDirectories.front()->retainedBytes();
							m_persistentChatOpenAttachmentDirectoryBytes =
								m_persistentChatOpenAttachmentDirectoryBytes > evictedBytes
								? m_persistentChatOpenAttachmentDirectoryBytes - evictedBytes : 0;
							m_persistentChatOpenAttachmentDirectories.erase(
								m_persistentChatOpenAttachmentDirectories.begin());
						}
					} else {
						failureCode = QStringLiteral("no-handler");
					}
				}
				QString failureMessage;
				if (failureCode == QLatin1String("unsafe-type")) {
					failureMessage = tr("This attachment type cannot be opened safely. Save it first instead.");
				} else if (failureCode == QLatin1String("temporary-directory-error")) {
					failureMessage = tr("A temporary folder for the attachment could not be created.");
				} else if (failureCode == QLatin1String("no-handler")) {
					failureMessage = tr("No application is available to open this attachment type.");
				} else if (!opened) {
					failureMessage = tr("The downloaded attachment could not be prepared for opening.");
				}
				if (operations && !openOperationID.isEmpty()) {
					operations->finishOperation(openOperationID, opened, opened ? QString() : failureCode,
						opened ? tr("The attachment was opened successfully.") : failureMessage);
				}
				if (!opened) {
					publishModernToast(QStringLiteral("error"), tr("Attachment could not be opened"), failureMessage);
				}
			}
		});
}

void MainWindow::msgChatEmbedState(const MumbleProto::ChatEmbedState &msg) {
	handlePersistentChatEmbedState(msg);
}

void MainWindow::msgChatEmbedAssistRequest(const MumbleProto::ChatEmbedAssistRequest &msg) {
	handleChatEmbedAssistRequest(msg);
}

void MainWindow::msgChatEmbedAssistResult(const MumbleProto::ChatEmbedAssistResult &) {
}

void MainWindow::msgChatReactionToggle(const MumbleProto::ChatReactionToggle &) {
}

void MainWindow::msgChatReactionState(const MumbleProto::ChatReactionState &msg) {
	handlePersistentChatReactionState(msg);
}

void MainWindow::msgChatHistoryGrantSync(const MumbleProto::ChatHistoryGrantSync &msg) {
	if (!m_pendingChatHistoryGrant || !msg.has_request_id()
		|| msg.request_id() != m_pendingChatHistoryGrant->requestID) {
		return;
	}

	const PendingChatHistoryGrant pending = *m_pendingChatHistoryGrant;
	ClientUser *currentTarget = ClientUser::get(pending.session);
	if (pending.connectionGeneration != m_persistentChatAssetConnectionGeneration || !pending.target
		|| currentTarget != pending.target || currentTarget->iId < 0
		|| static_cast< unsigned int >(currentTarget->iId) != pending.persistentUserID) {
		failPendingChatHistoryGrant(
			QStringLiteral("stale_target"),
			tr("The target user's server identity changed before the grant was confirmed. Reopen the user menu and try again."));
		return;
	}
	if (!msg.has_action() || msg.action() != pending.action || msg.grants_size() != 1
		|| !msg.grants(0).has_user_id() || msg.grants(0).user_id() != pending.persistentUserID) {
		failPendingChatHistoryGrant(
			QStringLiteral("mismatched_ack"),
			tr("The server returned an acknowledgement for a different chat history grant."));
		return;
	}
	const MumbleProto::ChatScope acknowledgedScope = msg.grants(0).has_scope()
		? msg.grants(0).scope() : MumbleProto::Channel;
	const unsigned int acknowledgedScopeID = msg.grants(0).has_scope_id()
		? msg.grants(0).scope_id() : Mumble::ROOT_CHANNEL_ID;
	if (acknowledgedScope != pending.scope || acknowledgedScopeID != pending.scopeID) {
		failPendingChatHistoryGrant(
			QStringLiteral("mismatched_ack"),
			tr("The server acknowledged a different chat history scope. Reopen the dialog and try again."));
		return;
	}

	const MumbleProto::ChatHistoryGrantSync_Result result = msg.has_result()
		? msg.result() : MumbleProto::ChatHistoryGrantSync_Result_ResultUnspecified;
	if (result == MumbleProto::ChatHistoryGrantSync_Result_Rejected) {
		const QString message = msg.has_message() && !u8(msg.message()).trimmed().isEmpty()
			? u8(msg.message()).trimmed()
			: tr("The server rejected the chat history grant. Review your permissions and try again.");
		failPendingChatHistoryGrant(
			msg.has_error_code() ? u8(msg.error_code()) : QStringLiteral("rejected"), message);
		return;
	}
	if (result != MumbleProto::ChatHistoryGrantSync_Result_Accepted
		&& result != MumbleProto::ChatHistoryGrantSync_Result_NoOp) {
		failPendingChatHistoryGrant(
			QStringLiteral("invalid_ack"),
			tr("The server response did not contain a valid chat history grant result."));
		return;
	}

	m_pendingChatHistoryGrant.reset();
	if (m_modernDialogController && m_modernDialogController->activeDialogID() == pending.dialogID) {
		publishModernDialogState(m_modernDialogController->close(pending.dialogID));
	}
	const bool revoked = pending.action == MumbleProto::ChatHistoryGrantSync_Action_Revoke;
	publishModernToast(
		QStringLiteral("success"), tr("Chat history access"),
		result == MumbleProto::ChatHistoryGrantSync_Result_NoOp
			? (revoked ? tr("Chat history access was already revoked.") : tr("Chat history access was already granted."))
			: (revoked ? tr("Chat history access revoked.") : tr("Chat history access granted.")));
}

void MainWindow::msgWatchTogetherSync(const MumbleProto::WatchTogetherSync &msg) {
	if (!m_qmlShellHost || !m_qmlShellHost->window() || !msg.has_session_id()) return;

	MediaSessionBackend *media = m_qmlShellHost->mediaSession();
	if (!media) return;
	const ClientUser *self = ClientUser::get(Global::get().uiSession);
	media->setCurrentVoiceScopeId(
		self && self->cChannel ? static_cast< qulonglong >(self->cChannel->iId) : 0);

	QString provider = QStringLiteral("direct");
	switch (msg.source_kind()) {
		case MumbleProto::WatchTogetherSourceYouTube: provider = QStringLiteral("youtube"); break;
		default: break;
	}
	QString event = QStringLiteral("state");
	switch (msg.event()) {
		case MumbleProto::WatchTogetherEventStart: event = QStringLiteral("start"); break;
		case MumbleProto::WatchTogetherEventJoin: event = QStringLiteral("join"); break;
		case MumbleProto::WatchTogetherEventLeave: event = QStringLiteral("leave"); break;
		case MumbleProto::WatchTogetherEventEnd: event = QStringLiteral("end"); break;
		case MumbleProto::WatchTogetherEventHostTransfer: event = QStringLiteral("host-transfer"); break;
		case MumbleProto::WatchTogetherEventState:
		case MumbleProto::WatchTogetherEventStateRequest:
		default: break;
	}
	QVariantList participants;
	participants.reserve(msg.participant_sessions_size());
	for (const unsigned int participant : msg.participant_sessions())
		participants.push_back(QVariant::fromValue(static_cast< qulonglong >(participant)));
	media->applySharedState(
		u8(msg.session_id()), msg.has_source_url() ? QUrl(u8(msg.source_url())) : QUrl(), provider,
		msg.has_title() ? u8(msg.title()) : QString(), msg.has_scope_id() ? msg.scope_id() : 0,
		msg.has_actor_session() ? msg.actor_session() : 0, msg.has_host_session() ? msg.host_session() : 0,
		participants, event, msg.position_seconds(), msg.paused(), msg.has_updated_at() ? msg.updated_at() : 0,
		Global::get().uiSession);
}

void MainWindow::msgStonksRequest(const MumbleProto::StonksRequest &) {
}

void MainWindow::msgStonksAction(const MumbleProto::StonksAction &) {
}

void MainWindow::msgStonksState(const MumbleProto::StonksState &msg) {
	handleStonksState(msg);
}

void MainWindow::msgFeedbackReport(const MumbleProto::FeedbackReport &) {
}

void MainWindow::msgFeedbackReportState(const MumbleProto::FeedbackReportState &msg) {
	handleFeedbackReportState(msg);
}

void MainWindow::msgServerLogState(const MumbleProto::ServerLogState &msg) {
	applyModernServerLogState(msg);
}

void MainWindow::msgScreenShareCreate(const MumbleProto::ScreenShareCreate &) {
}

void MainWindow::msgScreenShareState(const MumbleProto::ScreenShareState &msg) {
	if (m_screenShareManager) {
		m_screenShareManager->handleScreenShareState(msg);
	}
}

void MainWindow::msgScreenShareOffer(const MumbleProto::ScreenShareOffer &msg) {
	if (m_screenShareManager) {
		m_screenShareManager->handleScreenShareOffer(msg);
	}
}

void MainWindow::msgScreenShareAnswer(const MumbleProto::ScreenShareAnswer &msg) {
	if (m_screenShareManager) {
		m_screenShareManager->handleScreenShareAnswer(msg);
	}
}

void MainWindow::msgScreenShareIceCandidate(const MumbleProto::ScreenShareIceCandidate &msg) {
	if (m_screenShareManager) {
		m_screenShareManager->handleScreenShareIceCandidate(msg);
	}
}

void MainWindow::msgScreenShareStop(const MumbleProto::ScreenShareStop &msg) {
	if (m_screenShareManager) {
		m_screenShareManager->handleScreenShareStop(msg);
	}
}

void MainWindow::msgTextChannelSync(const MumbleProto::TextChannelSync &msg) {
	markPersistentChatAvailable(false);
	handlePersistentTextChannelSync(msg);
}

#undef ACTOR_INIT
#undef VICTIM_INIT
#undef SELF_INIT
