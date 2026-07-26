// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ACL.h"
#include "Channel.h"
#include "ChannelListenerManager.h"
#include "ChatFeature.h"
#include "ClientType.h"
#include "Connection.h"
#include "FeedbackReport.h"
#include "FinanceQuote.h"
#include "ForkFeature.h"
#include "Group.h"
#include "Meta.h"
#include "MumbleConstants.h"
#include "ProtoUtils.h"
#include "QtUtils.h"
#include "ScreenShare.h"
#include "Server.h"
#include "ServerUser.h"
#include "StonksCommand.h"
#include "StonksLedger.h"
#include "User.h"
#include "Version.h"
#include "WatchTogetherSession.h"
#include "crypto/CryptState.h"

#include "murmur/database/ChronoUtils.h"
#include "murmur/database/DBChatHistoryGrant.h"
#include "murmur/database/DBChatMessage.h"
#include "murmur/database/DBChatReadState.h"
#include "murmur/database/DBStonksFeedPreferences.h"
#include "murmur/database/DBStonksFollow.h"
#include "murmur/database/DBStonksPinnedTicker.h"
#include "murmur/database/DBStonksScore.h"
#include "murmur/database/DBStonksSnapshot.h"
#include "murmur/database/DBStonksSnapshotPosition.h"
#include "murmur/database/DBStonksValuation.h"
#include "murmur/database/UserProperty.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <utility>
#include <unordered_map>
#include <vector>

#include <QtCore/QBuffer>
#include <QtCore/QCryptographicHash>
#include <QtCore/QDate>
#include <QtCore/QDateTime>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QLocale>
#include <QtCore/QObject>
#include <QtCore/QRandomGenerator>
#include <QtCore/QRegularExpression>
#include <QtCore/QRegularExpressionMatchIterator>
#include <QtCore/QRect>
#include <QtCore/QSaveFile>
#include <QtCore/QSet>
#include <QtCore/QStack>
#include <QtCore/QStringList>
#include <QtCore/QTime>
#include <QtCore/QTimeZone>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtCore/QUuid>
#include <QtCore/QtEndian>
#include <QtGui/QImage>
#include <QtGui/QImageReader>
#include <QtGui/QImageWriter>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QHostInfo>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#include <tracy/Tracy.hpp>

namespace msdb = ::mumble::server::db;

namespace {
constexpr std::size_t MAX_CHAT_HISTORY_LEGACY_MIRROR_BYTES = 64 * 1024;
constexpr std::size_t MAX_CHAT_HISTORY_RESPONSE_BYTES      = 2 * 1024 * 1024;
constexpr int MAX_CHAT_HISTORY_WARMUP_SCOPES               = 20;
constexpr unsigned int MAX_CHAT_HISTORY_WARMUP_MESSAGES_PER_SCOPE = 20;
constexpr int MAX_CHAT_HISTORY_SERVER_CACHED_THREADS              = 128;
constexpr unsigned int CHAT_HISTORY_SERVER_CACHE_PAGE_MESSAGES    = 50;
constexpr unsigned int CHAT_HISTORY_SERVER_CACHE_STORED_MESSAGES  = CHAT_HISTORY_SERVER_CACHE_PAGE_MESSAGES + 1;
constexpr int MAX_STONKS_LEDGER_POSITIONS                  = 64;
constexpr unsigned int MAX_STONKS_LEDGER_SNAPSHOTS         = 50;
constexpr unsigned int MAX_STONKS_VALUATIONS_PER_QUERY     = 20000;
constexpr qint64 FEEDBACK_REPORT_RATE_LIMIT_MS             = 5 * 60 * 1000;
constexpr int SERVER_IDENTITY_IMAGE_SIZE                   = 256;
constexpr qint64 SERVER_IDENTITY_IMAGE_MAX_INPUT_BYTES     = 4 * 1024 * 1024;
constexpr qint64 SERVER_IDENTITY_IMAGE_MAX_STORED_BYTES    = 512 * 1024;

std::size_t serializedChatHistoryResponseBytes(const MumbleProto::ChatHistoryResponse &response) {
#if GOOGLE_PROTOBUF_VERSION >= 3004000
	return response.ByteSizeLong();
#else
	return response.ByteSize();
#endif
}

void constrainChatHistoryResponseSize(MumbleProto::ChatHistoryResponse &response) {
	while (response.messages_size() > 1
		   && serializedChatHistoryResponseBytes(response) > MAX_CHAT_HISTORY_RESPONSE_BYTES) {
		response.mutable_messages()->DeleteSubrange(0, 1);
		response.set_has_more(true);
		response.set_has_older(true);
		response.set_oldest_message_id(response.messages(0).message_id());
	}
}

MumbleProto::ChatHistoryResponse emptyChatHistoryResponseForRequest(ServerUser *uSource,
																	const MumbleProto::ChatHistoryRequest &request) {
	const MumbleProto::ChatScope scope = request.has_scope() ? request.scope() : MumbleProto::Channel;
	const unsigned int scopeID =
		request.has_scope_id() ? request.scope_id() : (uSource->cChannel ? uSource->cChannel->iId : Mumble::ROOT_CHANNEL_ID);

	MumbleProto::ChatHistoryResponse response;
	response.set_scope(scope);
	response.set_scope_id(scopeID);
	response.set_start_offset(request.has_start_offset() ? request.start_offset() : 0);
	response.set_has_more(false);
	response.set_has_older(false);
	return response;
}

bool isValidGitHubPathComponent(const QString &value) {
	static const QRegularExpression pattern(QLatin1String("^[A-Za-z0-9_.-]+$"));
	return pattern.match(value.trimmed()).hasMatch();
}

QUrl feedbackGitHubIssueUrl(const QString &apiUrl, const QString &owner, const QString &repo) {
	QUrl url(apiUrl.trimmed());
	if (!url.isValid() || url.scheme().isEmpty() || url.host().isEmpty()) {
		return QUrl();
	}

	QString path = url.path();
	if (path.endsWith(QLatin1Char('/'))) {
		path.chop(1);
	}
	path += QStringLiteral("/repos/%1/%2/issues")
				.arg(QString::fromLatin1(QUrl::toPercentEncoding(owner.trimmed())),
					 QString::fromLatin1(QUrl::toPercentEncoding(repo.trimmed())));
	url.setPath(path);
	url.setQuery(QString());
	url.setFragment(QString());
	return url;
}

QString feedbackKindLabels(const Server *server, const MumbleProto::FeedbackReportKind kind) {
	switch (kind) {
		case MumbleProto::FeedbackReportBug:
			return server->qsFeedbackBugLabels;
		case MumbleProto::FeedbackReportSuggestion:
			return server->qsFeedbackSuggestionLabels;
		case MumbleProto::FeedbackReportSupport:
			return server->qsFeedbackSupportLabels;
	}

	return QString();
}

QStringList feedbackLabelsForKind(const Server *server, const MumbleProto::FeedbackReportKind kind) {
	QStringList labels = Mumble::Feedback::splitLabels(server->qsFeedbackCommonLabels);
	for (const QString &label : Mumble::Feedback::splitLabels(feedbackKindLabels(server, kind))) {
		if (!labels.contains(label)) {
			labels << label;
		}
	}
	return labels;
}
} // namespace

#define RATELIMIT(user)                   \
	if (user->leakyBucket.ratelimit(1)) { \
		return;                           \
	}

#define MSG_SETUP(st)            \
	if (uSource->sState != st) { \
		return;                  \
	}                            \
	uSource->bwr.resetIdleSeconds()

#define MSG_SETUP_NO_UNIDLE(st) \
	if (uSource->sState != st)  \
	return

#define VICTIM_SETUP                                   \
	ServerUser *pDstServerUser = uSource;              \
	if (msg.has_session())                             \
		pDstServerUser = qhUsers.value(msg.session()); \
	if (!pDstServerUser)                               \
		return;                                        \
	Q_UNUSED(pDstServerUser)

#define PERM_DENIED(who, where, what)                                                                                \
	{                                                                                                                \
		MumbleProto::PermissionDenied mppd;                                                                          \
		mppd.set_permission(static_cast< unsigned int >(what));                                                      \
		mppd.set_channel_id(where->iId);                                                                             \
		mppd.set_session(who->uiSession);                                                                            \
		mppd.set_type(MumbleProto::PermissionDenied_DenyType_Permission);                                            \
		sendMessage(uSource, mppd);                                                                                  \
		log(uSource,                                                                                                 \
			QString("%1 not allowed to %2 in %3").arg(who->qsName).arg(ChanACL::permName(what)).arg(where->qsName)); \
	}

#define PERM_DENIED_TYPE(type)                                        \
	{                                                                 \
		MumbleProto::PermissionDenied mppd;                           \
		mppd.set_type(MumbleProto::PermissionDenied_DenyType_##type); \
		sendMessage(uSource, mppd);                                   \
	}

#define PERM_DENIED_FALLBACK(type, version, text)                     \
	{                                                                 \
		MumbleProto::PermissionDenied mppd;                           \
		mppd.set_type(MumbleProto::PermissionDenied_DenyType_##type); \
		if (uSource->m_version < version)                             \
			mppd.set_reason(u8(text));                                \
		sendMessage(uSource, mppd);                                   \
	}

#define PERM_DENIED_HASH(user)                                                    \
	{                                                                             \
		MumbleProto::PermissionDenied mppd;                                       \
		mppd.set_type(MumbleProto::PermissionDenied_DenyType_MissingCertificate); \
		if (user)                                                                 \
			mppd.set_session(user->uiSession);                                    \
		sendMessage(uSource, mppd);                                               \
	}

static void broadcastTextChannelSync(Server *server, const QHash< unsigned int, ServerUser * > &users) {
	for (ServerUser *currentUser : users) {
		if (currentUser && currentUser->sState == ServerUser::Authenticated) {
			server->sendTextChannelSync(currentUser);
		}
	}
}

/// A helper class for managing temporary access tokens.
/// It will add the tokens in the comstructor and remove them again in the destructor effectively
/// turning the tokens into a scope-based property.
class TemporaryAccessTokenHelper {
protected:
	ServerUser *affectedUser;
	QStringList qslTemporaryTokens;
	Server *server;

public:
	TemporaryAccessTokenHelper(ServerUser *affectedUser, const QStringList &tokens, Server *server)
		: affectedUser(affectedUser), qslTemporaryTokens(tokens), server(server) {
		// Add the temporary tokens
		QMutableStringListIterator it(this->qslTemporaryTokens);

		{
			QMutexLocker qml(&server->qmCache);

			while (it.hasNext()) {
				QString &token = it.next();

				// If tokens are treated case-insensitively, transform all temp. tokens to lowercase first
				if (Group::accessTokenCaseSensitivity == Qt::CaseInsensitive) {
					token = token.toLower();
				}

				if (!this->affectedUser->qslAccessTokens.contains(token, Group::accessTokenCaseSensitivity)) {
					// Add token
					this->affectedUser->qslAccessTokens << token;
				} else {
					// It appears, as if the user already has this token set -> it's not a temporary one or a duplicate
					it.remove();
				}
			}
		}

		if (!this->qslTemporaryTokens.isEmpty()) {
			// Clear the cache in order for tokens to take effect
			server->clearACLCache(this->affectedUser);
		}
	}

	~TemporaryAccessTokenHelper() {
		if (!this->qslTemporaryTokens.isEmpty()) {
			{
				QMutexLocker qml(&server->qmCache);

				// remove the temporary tokens
				for (const QString &token : this->qslTemporaryTokens) {
					this->affectedUser->qslAccessTokens.removeOne(token);
				}
			}

			// Clear cache to actually get rid of the temporary tokens
			server->clearACLCache(this->affectedUser);
		}
	}
};

namespace {
std::optional< unsigned int > persistedUserID(const ServerUser *user) {
	if (!user || user->iId < 0) {
		return std::nullopt;
	}

	return static_cast< unsigned int >(user->iId);
}

std::optional< std::string > connectedUserNameForPersistentID(const QHash< unsigned int, ServerUser * > &users,
															  unsigned int userID) {
	for (ServerUser *currentUser : users) {
		if (!currentUser || currentUser->iId < 0 || static_cast< unsigned int >(currentUser->iId) != userID) {
			continue;
		}

		const QString displayName = currentUser->qsName.trimmed();
		if (!displayName.isEmpty()) {
			return u8(displayName);
		}
	}

	return std::nullopt;
}

QByteArray registeredUserTextureHash(Server *server, unsigned int userID, QHash< unsigned int, QByteArray > &cache) {
	const auto cachedIt = cache.constFind(userID);
	if (cachedIt != cache.cend()) {
		return cachedIt.value();
	}

	QByteArray textureHash;
	if (server && userID <= static_cast< unsigned int >(std::numeric_limits< int >::max())) {
		const QByteArray texture = server->getTexture(static_cast< int >(userID));
		if (!texture.isEmpty()) {
			textureHash = sha1(texture);
		}
	}

	cache.insert(userID, textureHash);
	return textureHash;
}

std::string privateChatScopeKey(unsigned int firstUserID, unsigned int secondUserID) {
	if (firstUserID == secondUserID) {
		return {};
	}

	const unsigned int lowerUserID = std::min(firstUserID, secondUserID);
	const unsigned int upperUserID = std::max(firstUserID, secondUserID);
	return "private:" + std::to_string(lowerUserID) + ":" + std::to_string(upperUserID);
}

std::optional< std::pair< unsigned int, unsigned int > > privateChatParticipantsFromScopeKey(
	const std::string &scopeKey) {
	const QString key = QString::fromStdString(scopeKey);
	if (!key.startsWith(QStringLiteral("private:"))) {
		return std::nullopt;
	}

	const QStringList parts = key.mid(QStringLiteral("private:").size()).split(QLatin1Char(':'));
	if (parts.size() != 2) {
		return std::nullopt;
	}

	bool firstOK              = false;
	bool secondOK             = false;
	const unsigned int first  = parts.at(0).toUInt(&firstOK);
	const unsigned int second = parts.at(1).toUInt(&secondOK);
	if (!firstOK || !secondOK || first == second) {
		return std::nullopt;
	}

	return std::make_pair(std::min(first, second), std::max(first, second));
}

std::optional< unsigned int > privateChatPeerIDForViewer(const std::string &scopeKey, unsigned int viewerUserID) {
	const std::optional< std::pair< unsigned int, unsigned int > > participants =
		privateChatParticipantsFromScopeKey(scopeKey);
	if (!participants) {
		return std::nullopt;
	}

	if (participants->first == viewerUserID) {
		return participants->second;
	}
	if (participants->second == viewerUserID) {
		return participants->first;
	}

	return std::nullopt;
}

QSet< ServerUser * > connectedPrivateChatParticipants(const QHash< unsigned int, ServerUser * > &connectedUsers,
													  unsigned int firstUserID, unsigned int secondUserID) {
	QSet< ServerUser * > recipients;
	for (ServerUser *currentUser : connectedUsers) {
		const std::optional< unsigned int > currentUserID = persistedUserID(currentUser);
		if (currentUserID && (*currentUserID == firstUserID || *currentUserID == secondUserID)) {
			recipients.insert(currentUser);
		}
	}
	return recipients;
}

std::string chatScopeKey(MumbleProto::ChatScope scope, unsigned int scopeID) {
	switch (scope) {
		case MumbleProto::Channel:
			return "channel:" + std::to_string(scopeID);
		case MumbleProto::ServerGlobal:
			return "global";
		case MumbleProto::Aggregate:
			return {};
		case MumbleProto::TextChannel:
			return "text:" + std::to_string(scopeID);
		case MumbleProto::Private:
			return {};
	}

	return {};
}

std::optional< unsigned int > scopeIDFromPrefixedScopeKey(const std::string &scopeKey, const QString &prefix) {
	const QString key = QString::fromStdString(scopeKey);
	if (!key.startsWith(prefix)) {
		return std::nullopt;
	}

	bool ok                    = false;
	const unsigned int scopeID = key.mid(prefix.size()).toUInt(&ok);
	if (!ok) {
		return std::nullopt;
	}

	return scopeID;
}

std::optional< unsigned int > channelIDFromScopeKey(const std::string &scopeKey) {
	return scopeIDFromPrefixedScopeKey(scopeKey, QStringLiteral("channel:"));
}

std::optional< unsigned int > textChannelIDFromScopeKey(const std::string &scopeKey) {
	return scopeIDFromPrefixedScopeKey(scopeKey, QStringLiteral("text:"));
}

bool resolveStoredChatThread(const ::msdb::DBChatThread &thread, const QHash< unsigned int, Channel * > &channels,
							 const QHash< unsigned int, ::msdb::DBTextChannel > &textChannels,
							 MumbleProto::ChatScope &scope, unsigned int &scopeID, Channel *&permissionChannel) {
	switch (thread.scope) {
		case ::msdb::ChatThreadScope::Channel: {
			std::optional< unsigned int > channelID = channelIDFromScopeKey(thread.scopeKey);
			if (!channelID) {
				return false;
			}

			scope             = MumbleProto::Channel;
			scopeID           = channelID.value();
			permissionChannel = channels.value(scopeID);
			return permissionChannel != nullptr;
		}
		case ::msdb::ChatThreadScope::ServerGlobal:
			scope             = MumbleProto::ServerGlobal;
			scopeID           = 0;
			permissionChannel = channels.value(Mumble::ROOT_CHANNEL_ID);
			return permissionChannel != nullptr;
		case ::msdb::ChatThreadScope::Private:
			return false;
		case ::msdb::ChatThreadScope::TextChannel: {
			std::optional< unsigned int > textChannelID = textChannelIDFromScopeKey(thread.scopeKey);
			if (!textChannelID) {
				return false;
			}

			const auto it = textChannels.constFind(textChannelID.value());
			if (it == textChannels.cend()) {
				return false;
			}

			scope             = MumbleProto::TextChannel;
			scopeID           = textChannelID.value();
			permissionChannel = channels.value(it->aclChannelID);
			return permissionChannel != nullptr;
		}
	}

	return false;
}

bool messageVisibleInWindow(const ::msdb::DBChatMessage &message,
							const std::chrono::system_clock::time_point &visibleAfter) {
	return visibleAfter == std::chrono::system_clock::time_point() || message.createdAt >= visibleAfter;
}

std::chrono::system_clock::time_point chatTimePointFromEpochSeconds(std::uint64_t seconds) {
	using Clock   = std::chrono::system_clock;
	using Seconds = std::chrono::seconds;

	const auto maxSeconds = std::chrono::duration_cast< Seconds >(Clock::time_point::max().time_since_epoch()).count();
	const auto cappedSeconds =
		std::min< std::uint64_t >(seconds, maxSeconds > 0 ? static_cast< std::uint64_t >(maxSeconds) : 0);
	return Clock::time_point(Seconds(static_cast< Seconds::rep >(cappedSeconds)));
}

std::optional< msdb::ChatThreadScope > dbScopeFromProto(MumbleProto::ChatScope scope) {
	switch (scope) {
		case MumbleProto::Channel:
			return msdb::ChatThreadScope::Channel;
		case MumbleProto::ServerGlobal:
			return msdb::ChatThreadScope::ServerGlobal;
		case MumbleProto::TextChannel:
			return msdb::ChatThreadScope::TextChannel;
		case MumbleProto::Private:
			return msdb::ChatThreadScope::Private;
		case MumbleProto::Aggregate:
			return std::nullopt;
	}

	return std::nullopt;
}

std::optional< MumbleProto::ChatScope > protoScopeFromDB(msdb::ChatThreadScope scope) {
	switch (scope) {
		case msdb::ChatThreadScope::Channel:
			return MumbleProto::Channel;
		case msdb::ChatThreadScope::ServerGlobal:
			return MumbleProto::ServerGlobal;
		case msdb::ChatThreadScope::TextChannel:
			return MumbleProto::TextChannel;
		case msdb::ChatThreadScope::Private:
			return std::nullopt;
	}

	return std::nullopt;
}

std::optional< MumbleProto::ChatHistoryGrantInfo > protoGrantInfoFromDB(const msdb::DBChatHistoryGrant &grant) {
	const std::optional< MumbleProto::ChatScope > scope = protoScopeFromDB(grant.scope);
	if (!scope) {
		return std::nullopt;
	}

	MumbleProto::ChatHistoryGrantInfo info;
	info.set_scope(*scope);
	info.set_scope_id(grant.scopeID);
	info.set_user_id(grant.userID);
	info.set_visible_after(msdb::toEpochSeconds(grant.visibleAfter));
	info.set_granted_at(msdb::toEpochSeconds(grant.grantedAt));
	if (grant.grantedByUserID) {
		info.set_granted_by_user_id(*grant.grantedByUserID);
	}
	return info;
}

QSet< ServerUser * > recipientsWithChatHistoryAccess(Server *server,
													 const QHash< unsigned int, ServerUser * > &connectedUsers,
													 MumbleProto::ChatScope scope, unsigned int scopeID,
													 Channel *channel, ChanACL::ACLCache &cache,
													 std::optional< std::chrono::system_clock::time_point > messageCreatedAt = std::nullopt) {
	QSet< ServerUser * > recipients;

	for (ServerUser *currentUser : connectedUsers) {
		if (!currentUser) {
			continue;
		}

		const Server::ChatHistoryAccess access =
			server->resolveChatHistoryAccess(currentUser, scope, scopeID, channel, &cache);
		if (!access.allowed) {
			continue;
		}

		if (messageCreatedAt && access.visibleAfter != std::chrono::system_clock::time_point()
			&& *messageCreatedAt < access.visibleAfter) {
			continue;
		}

		recipients.insert(currentUser);
	}

	return recipients;
}

std::optional< unsigned int > configuredDefaultTextChannelID(DBWrapper &dbWrapper, unsigned int serverID) {
	unsigned int configuredTextChannelID = 0;
	dbWrapper.getConfigurationTo(serverID, "defaulttextchannel", configuredTextChannelID);
	if (configuredTextChannelID == 0) {
		return std::nullopt;
	}

	return configuredTextChannelID;
}

void storeDefaultTextChannelID(DBWrapper &dbWrapper, unsigned int serverID,
							   std::optional< unsigned int > textChannelID) {
	if (textChannelID && *textChannelID > 0) {
		dbWrapper.setConfiguration(serverID, "defaulttextchannel", std::to_string(*textChannelID));
	} else {
		dbWrapper.clearConfiguration(serverID, "defaulttextchannel");
	}
}

bool sortTextChannelsForPresentation(const ::msdb::DBTextChannel &lhs, const ::msdb::DBTextChannel &rhs) {
	if (lhs.position != rhs.position) {
		return lhs.position < rhs.position;
	}

	if (lhs.name != rhs.name) {
		return lhs.name < rhs.name;
	}

	return lhs.textChannelID < rhs.textChannelID;
}

std::optional< unsigned int > firstTextChannelID(const std::vector<::msdb::DBTextChannel > &textChannels) {
	if (textChannels.empty()) {
		return std::nullopt;
	}

	const auto firstTextChannel =
		std::min_element(textChannels.cbegin(), textChannels.cend(), sortTextChannelsForPresentation);
	return firstTextChannel != textChannels.cend() ? std::optional< unsigned int >(firstTextChannel->textChannelID)
												   : std::nullopt;
}

bool containsTextChannelID(const std::vector<::msdb::DBTextChannel > &textChannels, unsigned int textChannelID) {
	return std::find_if(textChannels.cbegin(), textChannels.cend(),
						[textChannelID](const ::msdb::DBTextChannel &textChannel) {
							return textChannel.textChannelID == textChannelID;
						})
		   != textChannels.cend();
}

bool clientSupportsChatFeature(const ServerUser *user, const MumbleProto::ChatFeature feature) {
	if (!user || !Mumble::ChatFeatures::isKnownFeature(feature)) {
		return false;
	}
	if (!Mumble::ChatFeatures::availableAtProtocolVersion(feature, user->uiPersistentChatProtocolVersion)) {
		return false;
	}

	if (!user->qlSupportedChatFeatures.isEmpty()) {
		return Mumble::ChatFeatures::contains(user->qlSupportedChatFeatures, feature);
	}

	if (feature == MumbleProto::ChatFeatureHistoryGrants || feature == MumbleProto::ChatFeatureHistoryGrantAcks
		|| feature == MumbleProto::ChatFeatureDirectMessages
		|| feature == MumbleProto::ChatFeatureHistoryWarmup || feature == MumbleProto::ChatFeatureActorAvatars) {
		return false;
	}

	return user->bSupportsPersistentChat;
}

bool clientSupportsPersistentChat(const ServerUser *user) {
	return clientSupportsChatFeature(user, MumbleProto::ChatFeaturePersistentHistory);
}

bool clientSupportsForkFeature(const ServerUser *user, const MumbleProto::ForkFeature feature) {
	return user && Mumble::ForkFeatures::contains(user->qlSupportedForkFeatures, feature);
}

bool canReceiveLivePersistentChat(ServerUser *user, MumbleProto::ChatScope scope, Channel *channel,
								  ChanACL::ACLCache &cache) {
	if (!user || !channel || !clientSupportsPersistentChat(user)) {
		return false;
	}

	if (scope == MumbleProto::TextChannel
		&& !clientSupportsChatFeature(user, MumbleProto::ChatFeatureTextChannels)) {
		return false;
	}

	return ChanACL::hasPermission(user, channel, ChanACL::TextMessage, &cache)
		   || ChanACL::hasPermission(user, channel, ChanACL::ViewTextMessageHistory, &cache);
}

std::chrono::system_clock::time_point currentConnectionVisibleAfter(const ServerUser *user) {
	const int onlineSeconds = user ? std::max(0, user->bwr.onlineSeconds()) : 0;
	return std::chrono::system_clock::now() - std::chrono::seconds(onlineSeconds + 1);
}

std::optional< std::chrono::system_clock::time_point > liveSessionVisibleAfterForUser(
	Server *server, ServerUser *user, MumbleProto::ChatScope scope, unsigned int scopeID, Channel *channel,
	const ChannelListenerManager &channelListenerManager, ChanACL::ACLCache &cache) {
	if (!user || !channel || !clientSupportsPersistentChat(user)) {
		return std::nullopt;
	}
	if (scope == MumbleProto::TextChannel && server && server->isStonksTextChannelID(scopeID)
		&& !server->hasStonksAccess(user, &cache)) {
		return std::nullopt;
	}

	if (scope == MumbleProto::Channel) {
		if (user->cChannel == channel) {
			return currentConnectionVisibleAfter(user);
		}

		for (unsigned int session : channelListenerManager.getListenersForChannel(channel->iId)) {
			if (session == user->uiSession) {
				return currentConnectionVisibleAfter(user);
			}
		}
		return std::nullopt;
	}

	if (scope == MumbleProto::ServerGlobal || scope == MumbleProto::TextChannel) {
		return canReceiveLivePersistentChat(user, scope, channel, cache)
				   ? std::optional< std::chrono::system_clock::time_point >(currentConnectionVisibleAfter(user))
				   : std::nullopt;
	}

	return std::nullopt;
}

QSet< ServerUser * > recipientsWithLivePersistentChatAccess(
	Server *server, const QHash< unsigned int, ServerUser * > &connectedUsers, MumbleProto::ChatScope scope,
	unsigned int scopeID, Channel *channel, ChanACL::ACLCache &cache,
	const QSet< ServerUser * > &channelAudience = {}) {
	QSet< ServerUser * > recipients;

	if (!channel) {
		return recipients;
	}

	if (scope == MumbleProto::Channel) {
		for (ServerUser *currentUser : channelAudience) {
			if (currentUser && clientSupportsPersistentChat(currentUser)) {
				recipients.insert(currentUser);
			}
		}
		return recipients;
	}

	if (scope != MumbleProto::ServerGlobal && scope != MumbleProto::TextChannel) {
		return recipients;
	}
	const bool requiresStonksAccess =
		scope == MumbleProto::TextChannel && server && server->isStonksTextChannelID(scopeID);

	for (ServerUser *currentUser : connectedUsers) {
		if (requiresStonksAccess && !server->hasStonksAccess(currentUser, &cache)) {
			continue;
		}
		if (canReceiveLivePersistentChat(currentUser, scope, channel, cache)) {
			recipients.insert(currentUser);
		}
	}

	return recipients;
}

QList< int > effectiveChatFeatures(const ServerUser *user) {
	if (!user) {
		return {};
	}

	if (!user->qlSupportedChatFeatures.isEmpty()) {
		return user->qlSupportedChatFeatures;
	}

	if (!user->bSupportsPersistentChat) {
		return {};
	}

	QList< int > legacyFeatures = Mumble::ChatFeatures::supportedFeatureList();
	legacyFeatures.removeAll(static_cast< int >(MumbleProto::ChatFeatureHistoryGrants));
	legacyFeatures.removeAll(static_cast< int >(MumbleProto::ChatFeatureDirectMessages));
	legacyFeatures.removeAll(static_cast< int >(MumbleProto::ChatFeatureHistoryWarmup));
	legacyFeatures.removeAll(static_cast< int >(MumbleProto::ChatFeatureActorAvatars));
	legacyFeatures.removeAll(static_cast< int >(MumbleProto::ChatFeatureHistoryGrantAcks));
	legacyFeatures.removeAll(static_cast< int >(MumbleProto::ChatFeatureAttachments));
	return legacyFeatures;
}

QList< int > screenShareCodecListFromVersion(const MumbleProto::Version &msg) {
	QList< int > codecs;
	codecs.reserve(msg.supported_screen_share_codecs_size());
	for (int i = 0; i < msg.supported_screen_share_codecs_size(); ++i) {
		codecs.append(static_cast< int >(msg.supported_screen_share_codecs(i)));
	}

	return Mumble::ScreenShare::sanitizeCodecList(codecs);
}

QList< int > screenShareCodecListFromCreate(const MumbleProto::ScreenShareCreate &msg) {
	QList< int > codecs;
	codecs.reserve(msg.requested_codecs_size());
	for (int i = 0; i < msg.requested_codecs_size(); ++i) {
		codecs.append(static_cast< int >(msg.requested_codecs(i)));
	}

	return Mumble::ScreenShare::sanitizeCodecList(codecs);
}

QString sanitizeScreenShareQualityProfile(const QString &profile) {
	const QString normalized = profile.trimmed().toLower();
	if (normalized == QLatin1String("sharp_text") || normalized == QLatin1String("sharp-text")
		|| normalized == QLatin1String("text")) {
		return QStringLiteral("sharp_text");
	}
	if (normalized == QLatin1String("smooth_motion") || normalized == QLatin1String("smooth-motion")
		|| normalized == QLatin1String("motion")) {
		return QStringLiteral("smooth_motion");
	}
	if (normalized == QLatin1String("data_saver") || normalized == QLatin1String("data-saver")
		|| normalized == QLatin1String("low")) {
		return QStringLiteral("data_saver");
	}

	return QStringLiteral("auto");
}

constexpr quint64 MESSAGE_SCREEN_SHARE_RELAY_TOKEN_LIFETIME_MSEC = 5ULL * 60ULL * 1000ULL;

QString randomMessageRelayCredential() {
	return QUuid::createUuid().toString(QUuid::WithoutBraces) + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QSet< ServerUser * > legacyChannelRecipients(const QHash< unsigned int, ServerUser * > &connectedUsers,
											 const ChannelListenerManager &channelListenerManager,
											 const Channel *channel) {
	QSet< ServerUser * > recipients;
	if (!channel) {
		return recipients;
	}

	for (User *currentUser : channel->qlUsers) {
		if (currentUser) {
			recipients.insert(static_cast< ServerUser * >(currentUser));
		}
	}

	for (unsigned int session : channelListenerManager.getListenersForChannel(channel->iId)) {
		ServerUser *currentUser = connectedUsers.value(session);
		if (currentUser) {
			recipients.insert(currentUser);
		}
	}

	return recipients;
}

struct AggregateChatEntry {
	::msdb::DBChatMessage message;
	MumbleProto::ChatScope scope = MumbleProto::Channel;
	unsigned int scopeID         = 0;
	std::chrono::system_clock::time_point visibleAfter = {};
};

bool newerAggregateEntry(const AggregateChatEntry &lhs, const AggregateChatEntry &rhs) {
	const std::size_t lhsCreatedAt = ::msdb::toEpochSeconds(lhs.message.createdAt);
	const std::size_t rhsCreatedAt = ::msdb::toEpochSeconds(rhs.message.createdAt);
	if (lhsCreatedAt != rhsCreatedAt) {
		return lhsCreatedAt > rhsCreatedAt;
	}
	if (lhs.message.messageID != rhs.message.messageID) {
		return lhs.message.messageID > rhs.message.messageID;
	}

	return lhs.message.threadID > rhs.message.threadID;
}

msdb::ChatMessageBodyFormat dbBodyFormatFromProto(MumbleProto::ChatBodyFormat format) {
	switch (format) {
		case MumbleProto::ChatBodyFormatMarkdownLite:
			return msdb::ChatMessageBodyFormat::MarkdownLite;
		case MumbleProto::ChatBodyFormatPlainText:
		default:
			return msdb::ChatMessageBodyFormat::PlainText;
	}
}

MumbleProto::ChatBodyFormat protoBodyFormatFromDB(msdb::ChatMessageBodyFormat format) {
	switch (format) {
		case msdb::ChatMessageBodyFormat::MarkdownLite:
			return MumbleProto::ChatBodyFormatMarkdownLite;
		case msdb::ChatMessageBodyFormat::PlainText:
		default:
			return MumbleProto::ChatBodyFormatPlainText;
	}
}

MumbleProto::ChatAssetKind protoAssetKindFromDB(msdb::ChatAssetKind kind) {
	switch (kind) {
		case msdb::ChatAssetKind::Image:
			return MumbleProto::ChatAssetKindImage;
		case msdb::ChatAssetKind::Video:
			return MumbleProto::ChatAssetKindVideo;
		case msdb::ChatAssetKind::Document:
			return MumbleProto::ChatAssetKindDocument;
		case msdb::ChatAssetKind::Binary:
			return MumbleProto::ChatAssetKindBinary;
		case msdb::ChatAssetKind::Audio:
			return MumbleProto::ChatAssetKindAudio;
		case msdb::ChatAssetKind::Unknown:
		default:
			return MumbleProto::ChatAssetKindUnknown;
	}
}

msdb::ChatAssetKind dbAssetKindFromProto(MumbleProto::ChatAssetKind kind) {
	switch (kind) {
		case MumbleProto::ChatAssetKindImage:
			return msdb::ChatAssetKind::Image;
		case MumbleProto::ChatAssetKindVideo:
			return msdb::ChatAssetKind::Video;
		case MumbleProto::ChatAssetKindDocument:
			return msdb::ChatAssetKind::Document;
		case MumbleProto::ChatAssetKindBinary:
			return msdb::ChatAssetKind::Binary;
		case MumbleProto::ChatAssetKindAudio:
			return msdb::ChatAssetKind::Audio;
		case MumbleProto::ChatAssetKindUnknown:
		default:
			return msdb::ChatAssetKind::Unknown;
	}
}

MumbleProto::ChatAssetTransferState protoTransferStateFromInt(MumbleProto::ChatAssetTransferState state) {
	return state;
}

MumbleProto::ChatEmbedStatus protoEmbedStatusFromDB(msdb::ChatEmbedStatus status) {
	switch (status) {
		case msdb::ChatEmbedStatus::Ready:
			return MumbleProto::ChatEmbedStatusReady;
		case msdb::ChatEmbedStatus::Blocked:
			return MumbleProto::ChatEmbedStatusBlocked;
		case msdb::ChatEmbedStatus::Failed:
			return MumbleProto::ChatEmbedStatusFailed;
		case msdb::ChatEmbedStatus::Pending:
		default:
			return MumbleProto::ChatEmbedStatusPending;
	}
}

QString structuredChatLegacyHtml(const QString &bodyText, msdb::ChatMessageBodyFormat bodyFormat) {
	Q_UNUSED(bodyFormat);
	return bodyText.toHtmlEscaped().replace(QLatin1Char('\n'), QLatin1String("<br/>"));
}

MumbleProto::ChatAssetRef protoAssetRefFromDB(const msdb::DBChatMessageAttachment &attachment) {
	MumbleProto::ChatAssetRef protoAttachment;
	protoAttachment.set_asset_id(attachment.assetID);
	if (!attachment.filename.empty()) {
		protoAttachment.set_filename(attachment.filename);
	}
	if (!attachment.mime.empty()) {
		protoAttachment.set_mime(attachment.mime);
	}
	protoAttachment.set_byte_size(attachment.byteSize);
	protoAttachment.set_kind(protoAssetKindFromDB(attachment.kind));
	if (attachment.width > 0) {
		protoAttachment.set_width(attachment.width);
	}
	if (attachment.height > 0) {
		protoAttachment.set_height(attachment.height);
	}
	if (attachment.durationMs > 0) {
		protoAttachment.set_duration_ms(attachment.durationMs);
	}
	protoAttachment.set_inline_safe(attachment.inlineSafe);
	if (attachment.previewAssetID) {
		protoAttachment.set_preview_asset_id(attachment.previewAssetID.value());
	}
	return protoAttachment;
}

QString chatAttachmentFallbackLine(const QString &filename, const unsigned int assetID) {
	const QString displayName = filename.trimmed().isEmpty()
		? QObject::tr("Attachment %1").arg(assetID) : filename.trimmed();
	return QObject::tr("[Attachment: %1]").arg(displayName);
}

QString appendChatAttachmentFallbacks(QString bodyText,
									 const std::vector< msdb::DBChatMessageAttachment > &attachments) {
	for (const msdb::DBChatMessageAttachment &attachment : attachments) {
		if (!bodyText.isEmpty()) bodyText.append(QLatin1Char('\n'));
		bodyText.append(chatAttachmentFallbackLine(QString::fromStdString(attachment.filename), attachment.assetID));
	}
	return bodyText;
}

QString appendChatAttachmentFallbacks(QString bodyText,
									 const google::protobuf::RepeatedPtrField< MumbleProto::ChatAssetRef > &attachments) {
	for (const MumbleProto::ChatAssetRef &attachment : attachments) {
		if (!bodyText.isEmpty()) bodyText.append(QLatin1Char('\n'));
		bodyText.append(chatAttachmentFallbackLine(
			attachment.has_filename() ? u8(attachment.filename()) : QString(),
			attachment.has_asset_id() ? attachment.asset_id() : 0));
	}
	return bodyText;
}

MumbleProto::ChatEmbedRef protoEmbedRefFromDB(const msdb::DBChatMessageEmbed &embed) {
	MumbleProto::ChatEmbedRef protoEmbed;
	protoEmbed.set_canonical_url(embed.canonicalUrl);
	protoEmbed.set_status(protoEmbedStatusFromDB(embed.status));
	if (!embed.title.empty()) {
		protoEmbed.set_title(embed.title);
	}
	if (!embed.description.empty()) {
		protoEmbed.set_description(embed.description);
	}
	if (!embed.siteName.empty()) {
		protoEmbed.set_site_name(embed.siteName);
	}
	if (embed.previewAssetID) {
		protoEmbed.set_preview_asset_id(embed.previewAssetID.value());
	}
	if (!embed.errorCode.empty()) {
		protoEmbed.set_error_code(embed.errorCode);
	}
	return protoEmbed;
}

MumbleProto::ChatAssetRef protoAssetRefFromAsset(const msdb::DBChatAsset &asset, const QString &filename,
												 bool inlineSafe) {
	MumbleProto::ChatAssetRef ref;
	ref.set_asset_id(asset.assetID);
	if (!filename.isEmpty()) {
		ref.set_filename(u8(filename));
	}
	ref.set_mime(asset.mime);
	ref.set_byte_size(asset.byteSize);
	ref.set_kind(protoAssetKindFromDB(asset.kind));
	if (asset.width > 0) {
		ref.set_width(asset.width);
	}
	if (asset.height > 0) {
		ref.set_height(asset.height);
	}
	if (asset.durationMs > 0) {
		ref.set_duration_ms(asset.durationMs);
	}
	ref.set_inline_safe(inlineSafe);
	if (asset.previewAssetID) {
		ref.set_preview_asset_id(asset.previewAssetID.value());
	}
	return ref;
}

struct ChatReactionAggregateState {
	unsigned int count = 0;
	bool selfReacted   = false;
	std::vector< std::string > actorNames;
};

using ChatReactionActorNameResolver = std::function< std::optional< std::string >(unsigned int) >;
using ChatActorTextureHashResolver  = std::function< QByteArray(unsigned int) >;

struct ChatReplyPreview {
	std::optional< std::string > actorName;
	std::optional< std::string > snippet;
};

std::vector< std::pair< std::string, ChatReactionAggregateState > >
	aggregateChatReactions(const std::vector< msdb::DBChatMessageReaction > &reactions,
						   std::optional< unsigned int > viewerUserID,
						   const ChatReactionActorNameResolver &resolveActorName = {}) {
	std::map< std::string, ChatReactionAggregateState > grouped;
	for (const msdb::DBChatMessageReaction &reaction : reactions) {
		ChatReactionAggregateState &aggregate = grouped[reaction.emoji];
		aggregate.count++;
		if (viewerUserID && reaction.actorUserID == viewerUserID.value()) {
			aggregate.selfReacted = true;
		}
		if (resolveActorName) {
			const std::optional< std::string > actorName = resolveActorName(reaction.actorUserID);
			if (actorName && !actorName->empty()) {
				aggregate.actorNames.push_back(actorName.value());
			}
		}
	}

	for (auto &aggregate : grouped) {
		std::vector< std::string > &actorNames = aggregate.second.actorNames;
		std::sort(actorNames.begin(), actorNames.end());
		actorNames.erase(std::unique(actorNames.begin(), actorNames.end()), actorNames.end());
	}

	std::vector< std::pair< std::string, ChatReactionAggregateState > > ordered(grouped.begin(), grouped.end());
	std::sort(ordered.begin(), ordered.end(), [](const auto &lhs, const auto &rhs) {
		if (lhs.second.count != rhs.second.count) {
			return lhs.second.count > rhs.second.count;
		}
		return lhs.first < rhs.first;
	});
	return ordered;
}

void appendProtoReactionAggregates(google::protobuf::RepeatedPtrField< MumbleProto::ChatReactionAggregate > *target,
								   const std::vector< msdb::DBChatMessageReaction > &reactions,
								   std::optional< unsigned int > viewerUserID,
								   const ChatReactionActorNameResolver &resolveActorName = {}) {
	if (!target) {
		return;
	}

	for (const auto &aggregate : aggregateChatReactions(reactions, viewerUserID, resolveActorName)) {
		MumbleProto::ChatReactionAggregate *protoReaction = target->Add();
		protoReaction->set_emoji(aggregate.first);
		protoReaction->set_count(aggregate.second.count);
		protoReaction->set_self_reacted(aggregate.second.selfReacted);
		for (const std::string &actorName : aggregate.second.actorNames) {
			protoReaction->add_actor_names(actorName);
		}
	}
}

QString chatReplySnippetText(const QString &text) {
	QString normalized = text;
	normalized.replace(QLatin1String("\r\n"), QLatin1String("\n"));
	normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));
	normalized.replace(QLatin1Char('\n'), QLatin1Char(' '));
	normalized = normalized.simplified();
	if (normalized.size() > 140) {
		normalized = normalized.left(137).trimmed() + QLatin1String("...");
	}
	return normalized;
}

template< typename AuthorResolver >
std::optional< ChatReplyPreview > resolveReplyPreview(DBWrapper &dbWrapper, unsigned int serverID,
													  const ::msdb::DBChatMessage &message,
													  const AuthorResolver &resolveAuthorName,
													  const std::chrono::system_clock::time_point &visibleAfter = {}) {
	if (!message.replyToMessageID) {
		return std::nullopt;
	}

	const std::optional<::msdb::DBChatMessage > replyTarget =
		dbWrapper.getChatMessage(serverID, message.replyToMessageID.value());
	if (!replyTarget || replyTarget->threadID != message.threadID
		|| replyTarget->deletedAt > std::chrono::system_clock::time_point()
		|| !messageVisibleInWindow(*replyTarget, visibleAfter)) {
		return std::nullopt;
	}

	ChatReplyPreview preview;
	preview.actorName = resolveAuthorName(*replyTarget);

	const QString snippetText = chatReplySnippetText(u8(replyTarget->bodyText));
	if (!snippetText.isEmpty()) {
		preview.snippet = u8(snippetText);
	}

	return preview;
}

QString normalizedMime(const QString &mime) {
	return mime.section(QLatin1Char(';'), 0, 0).trimmed().toLower();
}

QString safeChatAttachmentFilename(QString filename, const unsigned int assetID,
								   const msdb::ChatAssetKind kind, const QString &mime) {
	filename.replace(QLatin1Char('\\'), QLatin1Char('/'));
	filename = QFileInfo(filename).fileName().trimmed();
	filename.remove(QRegularExpression(QStringLiteral("[\\x00-\\x1f\\x7f]+")));
	if (filename == QLatin1String(".") || filename == QLatin1String("..")) filename.clear();
	if (filename.isEmpty()) filename = QObject::tr("Attachment %1").arg(assetID);
	if (kind == msdb::ChatAssetKind::Image) {
		QString extension;
		if (mime == QLatin1String("image/jpeg")) extension = QStringLiteral("jpg");
		else if (mime == QLatin1String("image/png")) extension = QStringLiteral("png");
		else if (mime == QLatin1String("image/webp")) extension = QStringLiteral("webp");
		else if (mime == QLatin1String("image/gif")) extension = QStringLiteral("gif");
		else if (mime == QLatin1String("image/bmp")) extension = QStringLiteral("bmp");
		if (!extension.isEmpty()) {
			QString baseName = QFileInfo(filename).completeBaseName().trimmed();
			if (baseName.isEmpty() || baseName == QLatin1String(".") || baseName == QLatin1String("..")) {
				baseName = QObject::tr("Image %1").arg(assetID);
			}
			filename = QStringLiteral("%1.%2").arg(baseName.left(230), extension);
		}
	}
	return filename.left(240);
}

msdb::ChatAssetKind inferredAssetKind(const QString &mime) {
	if (mime.startsWith(QLatin1String("image/"))) {
		return msdb::ChatAssetKind::Image;
	}
	if (mime.startsWith(QLatin1String("video/"))) {
		return msdb::ChatAssetKind::Video;
	}
	if (mime.startsWith(QLatin1String("audio/"))) {
		return msdb::ChatAssetKind::Audio;
	}
	if (mime == QLatin1String("application/pdf") || mime.startsWith(QLatin1String("text/"))) {
		return msdb::ChatAssetKind::Document;
	}
	return msdb::ChatAssetKind::Binary;
}

bool isAllowedChatAssetMime(msdb::ChatAssetKind kind, const QString &mime) {
	static const QSet< QString > imageMimes = {
		QStringLiteral("image/png"), QStringLiteral("image/jpeg"), QStringLiteral("image/webp"),
		QStringLiteral("image/gif"), QStringLiteral("image/bmp"),
	};
	static const QSet< QString > videoMimes = {
		QStringLiteral("video/mp4"),
		QStringLiteral("video/webm"),
		QStringLiteral("video/quicktime"),
	};
	static const QSet< QString > documentMimes = {
		QStringLiteral("application/pdf"),
		QStringLiteral("text/plain"),
		QStringLiteral("text/markdown"),
	};
	static const QSet< QString > audioMimes = {
		QStringLiteral("audio/aac"), QStringLiteral("audio/flac"), QStringLiteral("audio/mp4"),
		QStringLiteral("audio/mpeg"), QStringLiteral("audio/ogg"), QStringLiteral("audio/wav"),
		QStringLiteral("audio/webm"), QStringLiteral("audio/x-wav"),
	};
	static const QSet< QString > binaryMimes = {
		QStringLiteral("application/octet-stream"),
		QStringLiteral("application/zip"),
	};

	switch (kind) {
		case msdb::ChatAssetKind::Image:
			return imageMimes.contains(mime);
		case msdb::ChatAssetKind::Video:
			return videoMimes.contains(mime);
		case msdb::ChatAssetKind::Document:
			return documentMimes.contains(mime);
		case msdb::ChatAssetKind::Binary:
			return binaryMimes.contains(mime);
		case msdb::ChatAssetKind::Audio:
			return audioMimes.contains(mime);
		case msdb::ChatAssetKind::Unknown:
		default:
			return false;
	}
}

bool isInlineSafeAsset(msdb::ChatAssetKind kind, const QString &mime, bool requestInline) {
	if (!requestInline) {
		return false;
	}

	return kind == msdb::ChatAssetKind::Image && mime != QLatin1String("image/svg+xml")
		   && mime != QLatin1String("image/svg");
}

bool isValidSha256Hex(const QString &sha256) {
	if (sha256.size() != 64) {
		return false;
	}

	for (const QChar ch : sha256) {
		if (!ch.isDigit() && (ch.toLower() < QLatin1Char('a') || ch.toLower() > QLatin1Char('f'))) {
			return false;
		}
	}

	return true;
}

constexpr int CHAT_PREVIEW_TIMEOUT_MSEC        = 8000;
constexpr qint64 CHAT_PREVIEW_MAX_PAGE_BYTES   = 512 * 1024;
constexpr qint64 CHAT_PREVIEW_MAX_IMAGE_BYTES  = 4 * 1024 * 1024;
constexpr qint64 CHAT_PREVIEW_MAX_PLAYABLE_MEDIA_BYTES = 16 * 1024 * 1024;
constexpr int CHAT_PREVIEW_MAX_REDIRECTS       = 3;
constexpr int CHAT_PREVIEW_MAX_CONCURRENT_HOST = 2;
// Keep enough detail for the wider Qt Quick attachment cards and high-DPI
// displays. The original asset remains separate and is fetched only when the
// user opens the image viewer.
constexpr int CHAT_PREVIEW_THUMBNAIL_WIDTH     = 1024;
constexpr int CHAT_PREVIEW_THUMBNAIL_HEIGHT    = 768;
static const QByteArray s_chatPreviewBrowserUserAgent =
	QByteArrayLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
					  "(KHTML, like Gecko) Chrome/135.0.0.0 Safari/537.36");
static const QByteArray s_instagramChatPreviewMetadataUserAgent =
	QByteArrayLiteral("facebookexternalhit/1.1 (+http://www.facebook.com/externalhit_uatext.php)");
static const QByteArray s_chatPreviewAcceptHeader =
	QByteArrayLiteral("text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/*,*/*;q=0.8");
static const QByteArray s_chatPreviewAcceptLanguageHeader = QByteArrayLiteral("en-US,en;q=0.9");

bool isInstagramPreviewUrl(const QUrl &url) {
	QString host = url.host().trimmed().toLower();
	if (host.startsWith(QLatin1String("www."))) {
		host.remove(0, 4);
	}
	return host == QLatin1String("instagram.com") || host == QLatin1String("instagr.am");
}

bool chatPreviewImageReaderSupportsFormat(const QByteArray &format) {
	const QList< QByteArray > supportedFormats = QImageReader::supportedImageFormats();
	for (const QByteArray &supportedFormat : supportedFormats) {
		if (supportedFormat.compare(format, Qt::CaseInsensitive) == 0) {
			return true;
		}
	}
	return false;
}

QByteArray chatPreviewImageAcceptHeader() {
	static const QByteArray header = []() {
		QList< QByteArray > mimes = { QByteArrayLiteral("image/jpeg"), QByteArrayLiteral("image/png"),
									  QByteArrayLiteral("image/gif"), QByteArrayLiteral("image/bmp") };
		if (chatPreviewImageReaderSupportsFormat(QByteArrayLiteral("webp"))) {
			mimes.prepend(QByteArrayLiteral("image/webp"));
		}
		if (chatPreviewImageReaderSupportsFormat(QByteArrayLiteral("avif"))) {
			mimes.prepend(QByteArrayLiteral("image/avif"));
		}

		QByteArray value;
		for (const QByteArray &mime : mimes) {
			if (!value.isEmpty()) {
				value.append(',');
			}
			value.append(mime);
		}
		value.append(QByteArrayLiteral(",*/*;q=0.5"));
		return value;
	}();
	return header;
}

void prepareChatPreviewRequest(QNetworkRequest &request) {
	request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
	request.setRawHeader(QByteArrayLiteral("User-Agent"), s_chatPreviewBrowserUserAgent);
	request.setRawHeader(QByteArrayLiteral("Accept"), s_chatPreviewAcceptHeader);
	request.setRawHeader(QByteArrayLiteral("Accept-Language"), s_chatPreviewAcceptLanguageHeader);
}

void prepareInstagramChatPreviewRequest(QNetworkRequest &request) {
	prepareChatPreviewRequest(request);
	request.setRawHeader(QByteArrayLiteral("User-Agent"), s_instagramChatPreviewMetadataUserAgent);
}

void prepareChatPreviewImageRequest(QNetworkRequest &request) {
	prepareChatPreviewRequest(request);
	request.setRawHeader(QByteArrayLiteral("Accept"), chatPreviewImageAcceptHeader());
}

bool previewContentTypeLooksHtml(const QString &contentType) {
	return contentType.isEmpty() || contentType.contains(QLatin1String("html"))
		   || contentType.contains(QLatin1String("xml"));
}

QString previewImageMetaTag(const QHash< QString, QString > &metaTags) {
	static const QStringList preferredKeys = {
		QStringLiteral("og:image"),      QStringLiteral("og:image:url"),      QStringLiteral("og:image:secure_url"),
		QStringLiteral("twitter:image"), QStringLiteral("twitter:image:src"),
	};

	for (const QString &key : preferredKeys) {
		const QString value = metaTags.value(key).trimmed();
		if (!value.isEmpty()) {
			return value;
		}
	}

	return QString();
}

QString previewPlayableMediaMetaTag(const QHash< QString, QString > &metaTags) {
	static const QStringList preferredKeys = {
		QStringLiteral("og:video:secure_url"),      QStringLiteral("og:video:url"),
		QStringLiteral("og:video"),                 QStringLiteral("twitter:player:stream"),
		QStringLiteral("twitter:player:stream:url"),
	};

	for (const QString &key : preferredKeys) {
		const QString value = metaTags.value(key).trimmed();
		if (!value.isEmpty()) {
			return value;
		}
	}

	return QString();
}

bool isSanitizableImageMime(const QString &mime) {
	return mime == QLatin1String("image/png") || mime == QLatin1String("image/jpeg")
		   || mime == QLatin1String("image/webp") || mime == QLatin1String("image/gif")
		   || mime == QLatin1String("image/bmp");
}

bool isDirectImageUrl(const QUrl &url) {
	const QString path = url.path().toLower();
	return path.endsWith(QLatin1String(".png")) || path.endsWith(QLatin1String(".jpg"))
		   || path.endsWith(QLatin1String(".jpeg")) || path.endsWith(QLatin1String(".webp"))
		   || path.endsWith(QLatin1String(".gif")) || path.endsWith(QLatin1String(".bmp"));
}

bool isDirectPlayableMediaMime(const QString &mime) {
	return mime == QLatin1String("image/gif") || mime == QLatin1String("video/mp4")
		   || mime == QLatin1String("video/webm");
}

bool isDirectPlayableMediaUrl(const QUrl &url) {
	const QString path = url.path().toLower();
	return path.endsWith(QLatin1String(".gif")) || path.endsWith(QLatin1String(".gifv"))
		   || path.endsWith(QLatin1String(".m4v")) || path.endsWith(QLatin1String(".mp4"))
		   || path.endsWith(QLatin1String(".webm"));
}

QString playableMediaMimeForUrl(const QUrl &url) {
	const QString path = url.path().toLower();
	if (path.endsWith(QLatin1String(".gif"))) {
		return QStringLiteral("image/gif");
	}
	if (path.endsWith(QLatin1String(".gifv")) || path.endsWith(QLatin1String(".m4v"))
		|| path.endsWith(QLatin1String(".mp4"))) {
		return QStringLiteral("video/mp4");
	}
	if (path.endsWith(QLatin1String(".webm"))) {
		return QStringLiteral("video/webm");
	}
	return QString();
}

QUrl normalizedPlayableMediaFetchUrl(QUrl url) {
	QString path = url.path();
	if (path.toLower().endsWith(QLatin1String(".gifv"))) {
		path.chop(5);
		path.append(QLatin1String(".mp4"));
		url.setPath(path);
	}
	return url;
}

QString playableMediaMimeFromResponse(const QString &contentType, const QUrl &sourceUrl) {
	const QString normalizedContentType = contentType.section(QLatin1Char(';'), 0, 0).trimmed().toLower();
	if (isDirectPlayableMediaMime(normalizedContentType)) {
		return normalizedContentType;
	}
	if (!normalizedContentType.isEmpty() && normalizedContentType != QLatin1String("application/octet-stream")) {
		return QString();
	}
	return playableMediaMimeForUrl(sourceUrl);
}

msdb::ChatAssetKind playableMediaAssetKind(const QString &mime) {
	return mime.startsWith(QLatin1String("video/")) ? msdb::ChatAssetKind::Video : msdb::ChatAssetKind::Image;
}

QString previewTitleForMediaUrl(const QUrl &url, const QString &fallback) {
	const QString fileName = QFileInfo(url.path()).fileName();
	return fileName.isEmpty() ? fallback : fileName;
}

struct DecodedChatImage {
	QImage image;
	QSize sourceSize;
};

std::optional< DecodedChatImage > decodeChatImageBounded(const QByteArray &bytes,
													 const QSize &maximumDecodeSize = QSize()) {
	QBuffer buffer;
	buffer.setData(bytes);
	if (!buffer.open(QIODevice::ReadOnly)) {
		return std::nullopt;
	}

	QImageReader reader(&buffer);
	reader.setAutoTransform(true);
	const QSize sourceSize = reader.size();
	constexpr int maximumDimension = 16384;
	constexpr qint64 maximumPixels = 40LL * 1024LL * 1024LL;
	if (!sourceSize.isValid() || sourceSize.width() <= 0 || sourceSize.height() <= 0
		|| sourceSize.width() > maximumDimension || sourceSize.height() > maximumDimension
		|| static_cast< qint64 >(sourceSize.width()) * static_cast< qint64 >(sourceSize.height()) > maximumPixels) {
		return std::nullopt;
	}

	if (maximumDecodeSize.isValid()) {
		QSize scaledSize = sourceSize;
		scaledSize.scale(maximumDecodeSize, Qt::KeepAspectRatio);
		if (scaledSize != sourceSize) {
			// Formats with native scaled decoding (notably JPEG) avoid materializing the full source image.
			// Other handlers may ignore this hint, but the dimension/pixel limits above still bound the decode.
			reader.setScaledSize(scaledSize);
		}
	}

	DecodedChatImage result;
	result.image      = reader.read();
	result.sourceSize = sourceSize;
	if (result.image.isNull()) {
		return std::nullopt;
	}
	return result;
}

QImage decodeChatImage(const QByteArray &bytes) {
	const auto decoded = decodeChatImageBounded(bytes);
	return decoded ? decoded->image : QImage();
}

QString detectedChatImageMime(const QByteArray &bytes) {
	QBuffer buffer;
	buffer.setData(bytes);
	if (!buffer.open(QIODevice::ReadOnly)) return QString();

	QImageReader reader(&buffer);
	const QByteArray format = reader.format().trimmed().toLower();
	if (format == QByteArrayLiteral("jpg") || format == QByteArrayLiteral("jpeg")) {
		return QStringLiteral("image/jpeg");
	}
	if (format == QByteArrayLiteral("png")) return QStringLiteral("image/png");
	if (format == QByteArrayLiteral("webp")) return QStringLiteral("image/webp");
	if (format == QByteArrayLiteral("gif")) return QStringLiteral("image/gif");
	if (format == QByteArrayLiteral("bmp")) return QStringLiteral("image/bmp");
	return QString();
}

QByteArray encodeChatImage(const QImage &image, const char *format, int quality = -1) {
	QByteArray encoded;
	QBuffer buffer(&encoded);
	if (!buffer.open(QIODevice::WriteOnly)) {
		return QByteArray();
	}

	QImageWriter writer(&buffer, format);
	writer.setQuality(quality);
	if (!writer.write(image)) {
		return QByteArray();
	}

	return encoded;
}

struct SanitizedChatImage {
	QByteArray bytes;
	QString mime;
	unsigned int width  = 0;
	unsigned int height = 0;
};

std::optional< SanitizedChatImage > sanitizeDecodedChatImage(const QImage &image, bool thumbnailOnly = false) {
	if (image.isNull()) {
		return std::nullopt;
	}

	QImage normalized = thumbnailOnly ? image.scaled(CHAT_PREVIEW_THUMBNAIL_WIDTH, CHAT_PREVIEW_THUMBNAIL_HEIGHT,
													 Qt::KeepAspectRatio, Qt::SmoothTransformation)
									  : image;
	SanitizedChatImage result;
	result.width  = static_cast< unsigned int >(normalized.width());
	result.height = static_cast< unsigned int >(normalized.height());

	if (normalized.hasAlphaChannel()) {
		result.bytes = encodeChatImage(normalized, "PNG");
		result.mime  = QStringLiteral("image/png");
	} else {
		result.bytes = encodeChatImage(normalized, "JPEG", 90);
		result.mime  = QStringLiteral("image/jpeg");
	}

	if (result.bytes.isEmpty()) {
		return std::nullopt;
	}

	return result;
}

std::optional< SanitizedChatImage > sanitizeChatImageBytes(const QByteArray &bytes, bool thumbnailOnly = false) {
	const QSize maximumDecodeSize =
		thumbnailOnly ? QSize(CHAT_PREVIEW_THUMBNAIL_WIDTH, CHAT_PREVIEW_THUMBNAIL_HEIGHT) : QSize();
	const auto decoded = decodeChatImageBounded(bytes, maximumDecodeSize);
	return decoded ? sanitizeDecodedChatImage(decoded->image, thumbnailOnly) : std::nullopt;
}

bool ensureContentAddressedObject(const QString &path, const QByteArray &bytes, const QString &expectedSha256,
								 QString *error = nullptr) {
	QFile existing(path);
	if (existing.exists() && existing.open(QIODevice::ReadOnly)) {
		const bool sizeMatches = static_cast< quint64 >(existing.size()) == static_cast< quint64 >(bytes.size());
		const QByteArray existingBytes = sizeMatches ? existing.readAll() : QByteArray();
		existing.close();
		if (sizeMatches
			&& QString::fromLatin1(QCryptographicHash::hash(existingBytes, QCryptographicHash::Sha256).toHex())
				   == expectedSha256) {
			return true;
		}
	}

	QDir directory;
	if (!directory.mkpath(QFileInfo(path).absolutePath())) {
		if (error) *error = QStringLiteral("Failed to create the content-addressed asset directory.");
		return false;
	}

	QSaveFile destination(path);
	destination.setDirectWriteFallback(false);
	if (!destination.open(QIODevice::WriteOnly) || destination.write(bytes) != bytes.size()
		|| !destination.commit()) {
		destination.cancelWriting();
		if (error) *error = QStringLiteral("Failed to write the content-addressed asset atomically.");
		return false;
	}

	return true;
}

std::optional< QByteArray > sanitizeServerIdentityImageBytes(const QByteArray &bytes, QString *error = nullptr) {
	if (bytes.isEmpty()) {
		return QByteArray();
	}
	if (bytes.size() > SERVER_IDENTITY_IMAGE_MAX_INPUT_BYTES) {
		if (error) {
			*error = QObject::tr("Choose a server image smaller than 4 MB.");
		}
		return std::nullopt;
	}

	const QImage image = decodeChatImage(bytes);
	if (image.isNull()) {
		if (error) {
			*error = QObject::tr("Choose a readable image file supported by this Mumble build.");
		}
		return std::nullopt;
	}

	const int side = qMin(image.width(), image.height());
	if (side <= 0) {
		if (error) {
			*error = QObject::tr("Choose a readable server image.");
		}
		return std::nullopt;
	}

	QImage normalized = image.convertToFormat(QImage::Format_ARGB32);
	normalized        = normalized.copy(QRect((normalized.width() - side) / 2, (normalized.height() - side) / 2, side, side))
					 .scaled(SERVER_IDENTITY_IMAGE_SIZE, SERVER_IDENTITY_IMAGE_SIZE, Qt::KeepAspectRatioByExpanding,
							 Qt::SmoothTransformation);
	const QByteArray encoded = encodeChatImage(normalized, "PNG");
	if (encoded.isEmpty() || encoded.size() > SERVER_IDENTITY_IMAGE_MAX_STORED_BYTES) {
		if (error) {
			*error = QObject::tr("The server image could not be stored safely.");
		}
		return std::nullopt;
	}

	return encoded;
}

bool isBlockedPreviewAddress(const QHostAddress &address) {
	if (address.isNull() || address.isLoopback() || address.isBroadcast() || address.isMulticast()) {
		return true;
	}

	if (address.protocol() == QAbstractSocket::IPv4Protocol) {
		const quint32 ip          = address.toIPv4Address();
		const quint32 firstOctet  = (ip >> 24) & 0xffU;
		const quint32 secondOctet = (ip >> 16) & 0xffU;
		if (firstOctet == 0 || firstOctet == 10 || firstOctet == 127) {
			return true;
		}
		if (firstOctet == 169 && secondOctet == 254) {
			return true;
		}
		if (firstOctet == 172 && secondOctet >= 16 && secondOctet <= 31) {
			return true;
		}
		if (firstOctet == 192 && secondOctet == 168) {
			return true;
		}
		if (firstOctet == 100 && secondOctet >= 64 && secondOctet <= 127) {
			return true;
		}
		return false;
	}

	const Q_IPV6ADDR ipv6 = address.toIPv6Address();
	if ((ipv6[0] & 0xfeU) == 0xfcU) {
		return true;
	}
	if (ipv6[0] == 0xfeU && (ipv6[1] & 0xc0U) == 0x80U) {
		return true;
	}
	if (ipv6[0] == 0xfeU && (ipv6[1] & 0xc0U) == 0xc0U) {
		return true;
	}
	return address == QHostAddress::AnyIPv6 || address == QHostAddress::Any;
}

bool isSafePreviewUrl(const QUrl &url) {
	if (!url.isValid() || url.scheme() != QLatin1String("https") || url.host().trimmed().isEmpty()) {
		return false;
	}

	const QHostAddress directAddress(url.host());
	if (!directAddress.isNull()) {
		return !isBlockedPreviewAddress(directAddress);
	}

	const QHostInfo resolved = QHostInfo::fromName(url.host());
	if (resolved.error() != QHostInfo::NoError || resolved.addresses().isEmpty()) {
		return false;
	}

	for (const QHostAddress &address : resolved.addresses()) {
		if (isBlockedPreviewAddress(address)) {
			return false;
		}
	}

	return true;
}

void setChatPreviewAbortReason(QNetworkReply *reply, const QString &reason) {
	if (reply) {
		reply->setProperty("chatPreviewAbortReason", reason);
	}
}

QString chatPreviewAbortReason(const QNetworkReply *reply) {
	return reply ? reply->property("chatPreviewAbortReason").toString() : QString();
}

QList< QUrl > extractPreviewableUrls(const QString &text) {
	static const QRegularExpression urlPattern(QStringLiteral(R"((https://[^\s<>()\[\]{}"']+))"),
											   QRegularExpression::CaseInsensitiveOption);
	QList< QUrl > urls;
	QRegularExpressionMatchIterator it = urlPattern.globalMatch(text);
	while (it.hasNext()) {
		const QRegularExpressionMatch match = it.next();
		const QUrl url(match.captured(1));
		if (!url.isValid()) {
			continue;
		}
		urls.append(url);
	}
	return urls;
}

QString normalizedChatPreviewHost(QString host) {
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

QString firstChatPreviewPathSegment(const QUrl &url) {
	const QStringList segments = url.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
	return segments.isEmpty() ? QString() : segments.front();
}

std::optional< QString > redditVideoIdFromUrl(const QUrl &url) {
	const QString host = normalizedChatPreviewHost(url.host());
	QString videoId;
	if (host == QLatin1String("v.redd.it")) {
		videoId = firstChatPreviewPathSegment(url);
	} else if (host == QLatin1String("reddit.com")) {
		const QStringList segments = url.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
		if (segments.size() >= 2 && segments.at(0) == QLatin1String("video")) {
			videoId = segments.at(1);
		}
	}

	static const QRegularExpression s_redditVideoIdPattern(
		QRegularExpression::anchoredPattern(QLatin1String("[A-Za-z0-9_-]{5,64}")));
	if (!s_redditVideoIdPattern.match(videoId).hasMatch()) {
		return std::nullopt;
	}

	return videoId;
}

QString decodeHtmlEntityPreviewText(QString text) {
	text.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
	text.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
	text.replace(QStringLiteral("&#39;"), QStringLiteral("'"));
	text.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
	text.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
	return text.trimmed();
}

QString extractHtmlTitle(const QString &html) {
	static const QRegularExpression titlePattern(QStringLiteral(R"(<title[^>]*>(.*?)</title>)"),
												 QRegularExpression::CaseInsensitiveOption
													 | QRegularExpression::DotMatchesEverythingOption);
	const QRegularExpressionMatch match = titlePattern.match(html);
	return match.hasMatch() ? decodeHtmlEntityPreviewText(match.captured(1)) : QString();
}

QHash< QString, QString > extractMetaTags(const QString &html) {
	QHash< QString, QString > tags;
	static const QRegularExpression metaPattern(QStringLiteral(R"(<meta\b([^>]+)>)"),
												QRegularExpression::CaseInsensitiveOption
													| QRegularExpression::DotMatchesEverythingOption);
	static const QRegularExpression attrPattern(QStringLiteral(R"(([A-Za-z_:.-]+)\s*=\s*(['"])(.*?)\2)"),
												QRegularExpression::CaseInsensitiveOption
													| QRegularExpression::DotMatchesEverythingOption);

	QRegularExpressionMatchIterator metaIt = metaPattern.globalMatch(html);
	while (metaIt.hasNext()) {
		const QString attrs = metaIt.next().captured(1);
		QHash< QString, QString > parsedAttrs;
		QRegularExpressionMatchIterator attrIt = attrPattern.globalMatch(attrs);
		while (attrIt.hasNext()) {
			const QRegularExpressionMatch attr = attrIt.next();
			parsedAttrs.insert(attr.captured(1).toLower(), decodeHtmlEntityPreviewText(attr.captured(3)));
		}

		const QString key =
			parsedAttrs.value(QStringLiteral("property"), parsedAttrs.value(QStringLiteral("name"))).toLower();
		const QString content = parsedAttrs.value(QStringLiteral("content"));
		if (!key.isEmpty() && !content.isEmpty()) {
			tags.insert(key, content);
		}
	}

	return tags;
}

quint64 randomUploadID(const QHash< quint64, Server::PendingChatAssetUpload > &pendingUploads) {
	quint64 uploadID = 0;
	do {
		uploadID = QRandomGenerator::global()->generate64();
	} while (uploadID == 0 || pendingUploads.contains(uploadID));

	return uploadID;
}

QString chatEmbedAssistKey(unsigned int messageID, const QString &urlHash) {
	return QStringLiteral("%1:%2").arg(messageID).arg(urlHash);
}

quint64 randomChatEmbedAssistLeaseID(const QHash< QString, Server::PendingChatEmbedAssist > &pendingAssists) {
	quint64 leaseID = 0;
	do {
		leaseID = QRandomGenerator::global()->generate64();
	} while (leaseID == 0
			 || std::any_of(pendingAssists.cbegin(), pendingAssists.cend(),
							[leaseID](const Server::PendingChatEmbedAssist &assist) {
								return assist.leaseID == leaseID;
							}));

	return leaseID;
}

quint64 toEpochMilliseconds(std::chrono::system_clock::time_point timePoint) {
	return static_cast< quint64 >(
		std::chrono::duration_cast< std::chrono::milliseconds >(timePoint.time_since_epoch()).count());
}

void sendChatAssetState(Server *server, ServerUser *recipient, quint64 uploadID,
						MumbleProto::ChatAssetTransferState state, const QString &reason = QString(),
						quint64 acceptedByteSize                                = 0,
						const std::optional< MumbleProto::ChatAssetRef > &asset = std::nullopt) {
	MumbleProto::ChatAssetState response;
	response.set_upload_id(uploadID);
	response.set_state(protoTransferStateFromInt(state));
	if (!reason.isEmpty()) {
		response.set_reason(u8(reason));
	}
	if (acceptedByteSize > 0) {
		response.set_accepted_byte_size(acceptedByteSize);
	}
	if (asset) {
		*response.mutable_asset() = asset.value();
	}
	server->sendMessage(recipient, response);
}

MumbleProto::ChatMessage protoChatMessageFromDB(const ::msdb::DBChatMessage &message, MumbleProto::ChatScope scope,
												unsigned int scopeID,
												const std::optional< std::string > &resolvedAuthorName = std::nullopt,
												std::optional< unsigned int > viewerUserID             = std::nullopt,
												const std::optional< ChatReplyPreview > &replyPreview  = std::nullopt,
												const QList< int > &supportedChatFeatures =
													Mumble::ChatFeatures::supportedFeatureList(),
												const ChatReactionActorNameResolver &resolveReactionActorName = {},
												const ChatActorTextureHashResolver &resolveActorTextureHash  = {}) {
	const bool deleted = message.deletedAt > std::chrono::system_clock::time_point();
	MumbleProto::ChatMessage protoMessage;
	protoMessage.set_scope(scope);
	protoMessage.set_scope_id(scopeID);
	protoMessage.set_thread_id(message.threadID);
	protoMessage.set_message_id(message.messageID);
	if (message.authorSession) {
		protoMessage.set_actor(message.authorSession.value());
	}
	if (message.authorUserID) {
		protoMessage.set_actor_user_id(message.authorUserID.value());
	}
	if (resolvedAuthorName && !resolvedAuthorName->empty()) {
		protoMessage.set_actor_name(*resolvedAuthorName);
	}
	if (message.authorUserID && resolveActorTextureHash
		&& Mumble::ChatFeatures::contains(supportedChatFeatures, MumbleProto::ChatFeatureActorAvatars)) {
		const QByteArray textureHash = resolveActorTextureHash(message.authorUserID.value());
		if (!textureHash.isEmpty()) {
			protoMessage.set_actor_texture_hash(blob(textureHash));
		}
	}
	if (!deleted && message.replyToMessageID && replyPreview) {
		protoMessage.set_reply_to_message_id(message.replyToMessageID.value());
	}
	if (!deleted && replyPreview) {
		if (replyPreview->actorName && !replyPreview->actorName->empty()) {
			protoMessage.set_reply_actor_name(replyPreview->actorName.value());
		}
		if (replyPreview->snippet && !replyPreview->snippet->empty()) {
			protoMessage.set_reply_snippet(replyPreview->snippet.value());
		}
	}
	if (deleted) {
		protoMessage.set_message(std::string());
	} else {
		const bool supportsAttachments =
			Mumble::ChatFeatures::contains(supportedChatFeatures, MumbleProto::ChatFeatureAttachments);
		const QString renderedBody = supportsAttachments
			? u8(message.bodyText) : appendChatAttachmentFallbacks(u8(message.bodyText), message.attachments);
		protoMessage.set_body_text(u8(renderedBody));
		protoMessage.set_body_format(protoBodyFormatFromDB(message.bodyFormat));
		const std::string legacyMessage = u8(structuredChatLegacyHtml(renderedBody, message.bodyFormat));
		if (legacyMessage.size() <= MAX_CHAT_HISTORY_LEGACY_MIRROR_BYTES) {
			protoMessage.set_message(legacyMessage);
		} else {
			protoMessage.set_message(std::string());
		}
		if (supportsAttachments) {
			for (const msdb::DBChatMessageAttachment &attachment : message.attachments) {
				*protoMessage.add_attachments() = protoAssetRefFromDB(attachment);
			}
		}
		if (Mumble::ChatFeatures::contains(supportedChatFeatures, MumbleProto::ChatFeatureEmbeds)) {
			for (const msdb::DBChatMessageEmbed &embed : message.embeds) {
				*protoMessage.add_embeds() = protoEmbedRefFromDB(embed);
			}
		}
		if (Mumble::ChatFeatures::contains(supportedChatFeatures, MumbleProto::ChatFeatureReactions)) {
			appendProtoReactionAggregates(protoMessage.mutable_reactions(), message.reactions, viewerUserID,
										  resolveReactionActorName);
		}
	}
	protoMessage.set_created_at(::msdb::toEpochSeconds(message.createdAt));
	if (!deleted) {
		protoMessage.set_edited_at(::msdb::toEpochSeconds(message.editedAt));
	}
	if (deleted) {
		protoMessage.set_deleted_at(::msdb::toEpochSeconds(message.deletedAt));
	}

	return protoMessage;
}

MumbleProto::ChatReactionState protoReactionStateForMessage(const ::msdb::DBChatMessage &message,
															MumbleProto::ChatScope scope, unsigned int scopeID,
															std::optional< unsigned int > viewerUserID,
															const ChatReactionActorNameResolver &resolveActorName = {}) {
	MumbleProto::ChatReactionState state;
	state.set_scope(scope);
	state.set_scope_id(scopeID);
	state.set_thread_id(message.threadID);
	state.set_message_id(message.messageID);
	appendProtoReactionAggregates(state.mutable_reactions(), message.reactions, viewerUserID, resolveActorName);
	return state;
}

MumbleProto::ChatReadStateUpdate protoReadStateFromDB(const ::msdb::DBChatReadState &readState,
													  MumbleProto::ChatScope scope, unsigned int scopeID) {
	MumbleProto::ChatReadStateUpdate protoUpdate;
	protoUpdate.set_scope(scope);
	protoUpdate.set_scope_id(scopeID);
	protoUpdate.set_thread_id(readState.threadID);
	protoUpdate.set_user_id(readState.userID);
	protoUpdate.set_last_read_message_id(readState.lastReadMessageID);
	protoUpdate.set_updated_at(::msdb::toEpochSeconds(readState.updatedAt));

	return protoUpdate;
}

void clearHistoricChatActorSessions(MumbleProto::ChatHistoryResponse &response) {
	for (int i = 0; i < response.messages_size(); ++i) {
		response.mutable_messages(i)->clear_actor();
	}
}

MumbleProto::TextMessage legacyTextMessageFromPersistent(const MumbleProto::ChatMessage &message) {
	MumbleProto::TextMessage legacyMessage;
	if (message.has_actor()) {
		legacyMessage.set_actor(message.actor());
	}
	const QString bodyText = appendChatAttachmentFallbacks(
		message.has_body_text() ? u8(message.body_text()) : u8(message.message()), message.attachments());
	const MumbleProto::ChatBodyFormat bodyFormat =
		message.has_body_format() ? message.body_format() : MumbleProto::ChatBodyFormatPlainText;
	legacyMessage.set_message(u8(structuredChatLegacyHtml(bodyText, dbBodyFormatFromProto(bodyFormat))));

	switch (message.has_scope() ? message.scope() : MumbleProto::Channel) {
		case MumbleProto::Channel:
			legacyMessage.add_channel_id(message.has_scope_id() ? message.scope_id() : Mumble::ROOT_CHANNEL_ID);
			break;
		case MumbleProto::ServerGlobal:
			legacyMessage.add_tree_id(Mumble::ROOT_CHANNEL_ID);
			break;
		case MumbleProto::Aggregate:
		case MumbleProto::TextChannel:
		case MumbleProto::Private:
			break;
	}

	return legacyMessage;
}

void sendPersistentChatTextDenied(Server *server, ServerUser *user, const QString &reason) {
	if (!server || !user) {
		return;
	}

	MumbleProto::PermissionDenied denied;
	denied.set_session(user->uiSession);
	denied.set_type(MumbleProto::PermissionDenied_DenyType_Text);
	denied.set_reason(u8(reason));
	server->sendMessage(user, denied);
}

QString normalizedStonksRoomName(QString name) {
	name = name.trimmed().toLower();
	if (name.startsWith(QLatin1Char('#'))) {
		name.remove(0, 1);
	}
	return name;
}

bool isStonksTextChannel(const ::msdb::DBTextChannel &textChannel) {
	return normalizedStonksRoomName(u8(textChannel.name)) == QLatin1String("stonks");
}

QString formatStonksPercent(double value) {
	QLocale locale = QLocale::c();
	const QString formatted = locale.toString(value, 'f', 2);
	return value > 0.0 ? QStringLiteral("+%1%").arg(formatted) : QStringLiteral("%1%").arg(formatted);
}

struct StonksLeaderboardEntry {
	unsigned int userID = 0;
	QString userName;
	double score = 0.0;
	Mumble::Stonks::ReturnWindow window;
	bool insufficientHistory = true;
};

bool higherStonksScore(const StonksLeaderboardEntry &lhs, const StonksLeaderboardEntry &rhs) {
	if (lhs.insufficientHistory != rhs.insufficientHistory) {
		return !lhs.insufficientHistory;
	}
	if (lhs.window.partialPeriod != rhs.window.partialPeriod) {
		return !lhs.window.partialPeriod;
	}
	if (lhs.score != rhs.score) {
		return lhs.score > rhs.score;
	}
	return lhs.userName.localeAwareCompare(rhs.userName) < 0;
}

QString stonksHelpText() {
	return QStringLiteral(
		"Stonks commands\n"
		"`rklb` or `quote rklb` - latest quote card\n"
		"`score 30d +12.3` - publish a manual score\n"
		"`leaderboard 30d` - compare PnL\n"
		"`follow <user>` / `unfollow <user>`\n"
		"`following` / `me`");
}

QString normalizedStonksLedgerPeriod(QString period) {
	period = period.trimmed().toLower();
	return Mumble::Stonks::ledgerPeriods().contains(period) ? period : QStringLiteral("30d");
}

QString formatStonksMoney(double value, const QString &currency) {
	QLocale locale = QLocale::c();
	return QStringLiteral("%1 %2").arg(currency.trimmed().isEmpty() ? QStringLiteral("USD") : currency.trimmed().toUpper(),
									   locale.toString(value, 'f', 2));
}

QString stonksRegisteredDisplayName(Server *server, unsigned int userID) {
	if (!server) {
		return QStringLiteral("user %1").arg(userID);
	}

	const QString name = server->getRegisteredUserName(static_cast< int >(userID)).trimmed();
	return name.isEmpty() ? QStringLiteral("user %1").arg(userID) : name;
}

std::chrono::system_clock::time_point stonksCutoffForPeriod(
	const QString &period, const std::chrono::system_clock::time_point &latestAt) {
	const QString normalizedPeriod = normalizedStonksLedgerPeriod(period);
	if (normalizedPeriod == QLatin1String("ytd")) {
		const qint64 latestEpoch = static_cast< qint64 >(::msdb::toEpochSeconds(latestAt));
		const QDateTime latestDateTime = QDateTime::fromSecsSinceEpoch(latestEpoch, QTimeZone::utc());
		const QDateTime yearStart(QDate(latestDateTime.date().year(), 1, 1), QTime(0, 0), QTimeZone::utc());
		return std::chrono::system_clock::time_point(std::chrono::seconds(yearStart.toSecsSinceEpoch()));
	}

	const std::optional< qint64 > seconds = Mumble::Stonks::periodSeconds(normalizedPeriod);
	return latestAt - std::chrono::seconds(seconds.value_or(30 * 24 * 60 * 60));
}

std::optional< unsigned int > resolvedStonksTextChannelID(unsigned int configuredTextChannelID,
														  const std::vector<::msdb::DBTextChannel > &textChannels) {
	if (configuredTextChannelID > 0 && containsTextChannelID(textChannels, configuredTextChannelID)) {
		return configuredTextChannelID;
	}

	std::vector<::msdb::DBTextChannel > sortedTextChannels = textChannels;
	std::sort(sortedTextChannels.begin(), sortedTextChannels.end(), sortTextChannelsForPresentation);
	for (const ::msdb::DBTextChannel &textChannel : sortedTextChannels) {
		if (isStonksTextChannel(textChannel)) {
			return textChannel.textChannelID;
		}
	}

	return std::nullopt;
}

void appendStonksTextChannelInfo(MumbleProto::StonksState &state, const ::msdb::DBTextChannel &textChannel) {
	MumbleProto::TextChannelInfo *protoChannel = state.add_text_channels();
	protoChannel->set_text_channel_id(textChannel.textChannelID);
	protoChannel->set_name(textChannel.name);
	protoChannel->set_description(textChannel.description);
	protoChannel->set_acl_channel_id(textChannel.aclChannelID);
	protoChannel->set_position(textChannel.position);
}

MumbleProto::StonksSnapshot protoStonksSnapshotFromDB(
	const ::msdb::DBStonksSnapshot &snapshot,
	const std::vector< ::msdb::DBStonksSnapshotPosition > &positions,
	const QString &userName,
	bool includePositions) {
	MumbleProto::StonksSnapshot protoSnapshot;
	protoSnapshot.set_snapshot_id(snapshot.snapshotID);
	protoSnapshot.set_user_id(snapshot.userID);
	protoSnapshot.set_user_name(u8(userName));
	protoSnapshot.set_created_at(::msdb::toEpochSeconds(snapshot.createdAt));
	protoSnapshot.set_currency(snapshot.currency);
	protoSnapshot.set_total_value(snapshot.totalValue);
	protoSnapshot.set_note(snapshot.note);
	protoSnapshot.set_positions_redacted(!includePositions);

	if (includePositions) {
		for (const ::msdb::DBStonksSnapshotPosition &position : positions) {
			MumbleProto::StonksPosition *protoPosition = protoSnapshot.add_positions();
			protoPosition->set_symbol(position.symbol);
			protoPosition->set_quantity(position.quantity);
			protoPosition->set_price(position.price);
			protoPosition->set_market_value(position.marketValue);
			protoPosition->set_currency(position.currency);
			protoPosition->set_display_name(position.displayName);
			protoPosition->set_provider_id(position.providerID);
			protoPosition->set_provider_symbol(position.providerSymbol);
			protoPosition->set_exchange(position.exchange);
			const uint64_t quoteTime =
				position.quoteTime > 0 ? static_cast< uint64_t >(position.quoteTime) : static_cast< uint64_t >(0);
			protoPosition->set_quote_time(quoteTime);
			protoPosition->set_quote_source_url(position.quoteSourceURL);
			protoPosition->set_quote_confidence(position.quoteConfidence);
		}
	}

	return protoSnapshot;
}

MumbleProto::StonksPopularTicker protoStonksPopularTickerFromSummary(
	const Mumble::Stonks::PopularTickerSummary &ticker, bool includePositionTotals) {
	MumbleProto::StonksPopularTicker protoTicker;
	protoTicker.set_symbol(u8(ticker.symbol));
	if (!ticker.displayName.trimmed().isEmpty()) {
		protoTicker.set_display_name(u8(ticker.displayName.trimmed()));
	}
	protoTicker.set_holder_count(ticker.holderCount);
	if (includePositionTotals) {
		protoTicker.set_total_quantity(ticker.totalQuantity);
		protoTicker.set_total_market_value(ticker.totalMarketValue);
	}
	if (!ticker.currency.trimmed().isEmpty()) {
		protoTicker.set_currency(u8(ticker.currency.trimmed().toUpper()));
	}
	if (!ticker.providerID.trimmed().isEmpty()) {
		protoTicker.set_provider_id(u8(ticker.providerID.trimmed()));
	}
	if (!ticker.providerSymbol.trimmed().isEmpty()) {
		protoTicker.set_provider_symbol(u8(ticker.providerSymbol.trimmed()));
	}
	if (!ticker.exchange.trimmed().isEmpty()) {
		protoTicker.set_exchange(u8(ticker.exchange.trimmed()));
	}
	if (!ticker.quoteSourceURL.trimmed().isEmpty()) {
		protoTicker.set_quote_source_url(u8(ticker.quoteSourceURL.trimmed()));
	}
	if (ticker.latestUpdatedAt > 0) {
		protoTicker.set_latest_updated_at(static_cast< uint64_t >(ticker.latestUpdatedAt));
	}
	return protoTicker;
}

MumbleProto::StonksPinnedTicker protoStonksPinnedTickerFromDB(const ::msdb::DBStonksPinnedTicker &ticker) {
	MumbleProto::StonksPinnedTicker protoTicker;
	protoTicker.set_symbol(ticker.symbol);
	if (!ticker.displayName.empty()) {
		protoTicker.set_display_name(ticker.displayName);
	}
	if (!ticker.providerID.empty()) {
		protoTicker.set_provider_id(ticker.providerID);
	}
	if (!ticker.providerSymbol.empty()) {
		protoTicker.set_provider_symbol(ticker.providerSymbol);
	}
	if (!ticker.exchange.empty()) {
		protoTicker.set_exchange(ticker.exchange);
	}
	if (!ticker.quoteSourceURL.empty()) {
		protoTicker.set_quote_source_url(ticker.quoteSourceURL);
	}
	protoTicker.set_display_order(ticker.displayOrder);
	if (ticker.createdAt != std::chrono::system_clock::time_point()) {
		protoTicker.set_created_at(::msdb::toEpochSeconds(ticker.createdAt));
	}
	if (ticker.updatedAt != std::chrono::system_clock::time_point()) {
		protoTicker.set_updated_at(::msdb::toEpochSeconds(ticker.updatedAt));
	}
	return protoTicker;
}

MumbleProto::StonksFeedPreferences protoStonksFeedPreferencesFromDB(
	const ::msdb::DBStonksFeedPreferences &preferences) {
	MumbleProto::StonksFeedPreferences protoPreferences;
	protoPreferences.set_show_mine(preferences.showMine);
	protoPreferences.set_show_popular(preferences.showPopular);
	protoPreferences.set_show_pins(preferences.showPins);
	if (preferences.updatedAt != std::chrono::system_clock::time_point()) {
		protoPreferences.set_updated_at(::msdb::toEpochSeconds(preferences.updatedAt));
	}
	return protoPreferences;
}

Mumble::Stonks::ValuationSample stonksValuationSampleFromDB(const ::msdb::DBStonksValuation &valuation) {
	Mumble::Stonks::ValuationSample sample;
	sample.revisionID      = valuation.portfolioSnapshotID;
	sample.valuedAt        = static_cast< qint64 >(::msdb::toEpochSeconds(valuation.valuedAt));
	sample.totalValue      = valuation.totalValue;
	sample.currency        = u8(valuation.currency);
	sample.source          = u8(valuation.source);
	sample.pricedPositions = valuation.pricedPositions;
	sample.totalPositions  = valuation.totalPositions;
	return sample;
}

MumbleProto::StonksValuationPoint protoStonksValuationFromDB(const ::msdb::DBStonksValuation &valuation) {
	MumbleProto::StonksValuationPoint point;
	point.set_portfolio_snapshot_id(valuation.portfolioSnapshotID);
	point.set_valued_at(::msdb::toEpochSeconds(valuation.valuedAt));
	point.set_total_value(valuation.totalValue);
	point.set_currency(valuation.currency);
	point.set_source(valuation.source);
	point.set_priced_positions(valuation.pricedPositions);
	point.set_total_positions(valuation.totalPositions);
	point.set_estimated(valuation.estimated);
	return point;
}

std::vector< Mumble::Stonks::PopularTickerSummary > stonksPopularTickers(Server *server,
																		 unsigned int maxTickers = 5) {
	std::vector< Mumble::Stonks::PopularTickerPosition > positions;
	if (!server) {
		return {};
	}

	for (const ::msdb::DBStonksSnapshot &snapshot :
		 server->m_dbWrapper.getLatestStonksSnapshotsByUser(server->iServerNum)) {
		for (const ::msdb::DBStonksSnapshotPosition &position :
			 server->m_dbWrapper.getStonksSnapshotPositions(server->iServerNum, snapshot.snapshotID)) {
			Mumble::Stonks::PopularTickerPosition popularPosition;
			popularPosition.holderID       = snapshot.userID;
			popularPosition.symbol         = u8(position.symbol);
			popularPosition.quantity       = position.quantity;
			popularPosition.marketValue    = position.marketValue;
			popularPosition.currency       = u8(position.currency);
			popularPosition.displayName    = u8(position.displayName);
			popularPosition.providerID     = u8(position.providerID);
			popularPosition.providerSymbol = u8(position.providerSymbol);
			popularPosition.exchange       = u8(position.exchange);
			popularPosition.quoteSourceURL = u8(position.quoteSourceURL);
			popularPosition.updatedAt      = static_cast< qint64 >(::msdb::toEpochSeconds(snapshot.createdAt));
			positions.push_back(std::move(popularPosition));
		}
	}

	return Mumble::Stonks::popularTickers(positions, maxTickers);
}

std::vector< Mumble::Stonks::PopularTickerSummary > stonksPersonalTickers(Server *server, unsigned int userID,
																		  unsigned int maxTickers = 5) {
	std::vector< Mumble::Stonks::PopularTickerPosition > positions;
	if (!server) {
		return {};
	}

	const std::optional< ::msdb::DBStonksSnapshot > latestSnapshot =
		server->m_dbWrapper.getLatestStonksSnapshotForUser(server->iServerNum, userID);
	if (!latestSnapshot) {
		return {};
	}

	for (const ::msdb::DBStonksSnapshotPosition &position :
		 server->m_dbWrapper.getStonksSnapshotPositions(server->iServerNum, latestSnapshot->snapshotID)) {
		Mumble::Stonks::PopularTickerPosition personalPosition;
		personalPosition.holderID       = userID;
		personalPosition.symbol         = u8(position.symbol);
		personalPosition.quantity       = position.quantity;
		personalPosition.marketValue    = position.marketValue;
		personalPosition.currency       = u8(position.currency);
		personalPosition.displayName    = u8(position.displayName);
		personalPosition.providerID     = u8(position.providerID);
		personalPosition.providerSymbol = u8(position.providerSymbol);
		personalPosition.exchange       = u8(position.exchange);
		personalPosition.quoteSourceURL = u8(position.quoteSourceURL);
		personalPosition.updatedAt      = static_cast< qint64 >(::msdb::toEpochSeconds(latestSnapshot->createdAt));
		positions.push_back(std::move(personalPosition));
	}

	return Mumble::Stonks::popularTickers(positions, maxTickers);
}

std::vector< StonksLeaderboardEntry > stonksLedgerLeaderboard(Server *server, const QString &period,
															  unsigned int maxEntries = 100) {
	std::vector< StonksLeaderboardEntry > entries;
	if (!server) {
		return entries;
	}

	const QString normalizedPeriod = normalizedStonksLedgerPeriod(period);
	const qint64 latestAt          = QDateTime::currentSecsSinceEpoch();
	const auto latestTime          = std::chrono::system_clock::time_point(std::chrono::seconds(latestAt));
	const auto cutoffTime          = stonksCutoffForPeriod(normalizedPeriod, latestTime);
	const qint64 cutoffAt          = static_cast< qint64 >(::msdb::toEpochSeconds(cutoffTime));
	const auto queryStart          = cutoffTime - std::chrono::days(4);
	for (const ::msdb::DBStonksSnapshot &latestSnapshot :
		 server->m_dbWrapper.getLatestStonksSnapshotsByUser(server->iServerNum)) {
		if (latestSnapshot.totalValue <= 0.0 || !std::isfinite(latestSnapshot.totalValue)) {
			continue;
		}

		StonksLeaderboardEntry entry;
		entry.userID   = latestSnapshot.userID;
		entry.userName = stonksRegisteredDisplayName(server, latestSnapshot.userID);
		std::vector< Mumble::Stonks::ValuationSample > samples;
		for (const ::msdb::DBStonksValuation &valuation : server->m_dbWrapper.getStonksValuationsForUser(
				 server->iServerNum, latestSnapshot.userID, queryStart, MAX_STONKS_VALUATIONS_PER_QUERY)) {
			samples.push_back(stonksValuationSampleFromDB(valuation));
		}
		const std::optional< Mumble::Stonks::ReturnWindow > window =
			Mumble::Stonks::timeWeightedReturn(samples, cutoffAt, latestAt);
		if (window) {
			entry.score               = window->returnPercent;
			entry.window              = *window;
			entry.insufficientHistory = false;
		}
		entries.push_back(std::move(entry));
	}

	std::sort(entries.begin(), entries.end(), higherStonksScore);
	if (entries.size() > maxEntries) {
		entries.resize(maxEntries);
	}
	return entries;
}

MumbleProto::StonksState buildStonksState(Server *server, ServerUser *user, const QString &period,
										  ChanACL::ACLCache &aclCache, const QString &status = QString(),
										  const QString &error = QString(),
										  std::optional< unsigned int > requestedUserID = std::nullopt) {
	MumbleProto::StonksState state;
	if (!server || !user) {
		state.set_supported(false);
		state.set_error("No active server session.");
		return state;
	}

	const QString normalizedPeriod = normalizedStonksLedgerPeriod(period);
	state.set_supported(true);
	state.set_enabled(server->bStonksEnabled);
	state.set_selected_period(u8(normalizedPeriod));
	state.set_allowed(server->hasStonksAccess(user, &aclCache));
	if (!state.allowed()) {
		state.set_error("Use Stonks permission is required on the root channel.");
		return state;
	}

	const std::optional< unsigned int > selfUserID = persistedUserID(user);
	const bool registered =
		selfUserID && server->m_dbWrapper.registeredUserExists(server->iServerNum, *selfUserID);

	Channel *rootChannel = server->qhChannels.value(Mumble::ROOT_CHANNEL_ID);
	const bool canAdmin = rootChannel && ChanACL::hasPermission(user, rootChannel, ChanACL::Write, &aclCache);

	state.set_registered(registered);
	if (selfUserID) {
		state.set_self_user_id(*selfUserID);
	}
	state.set_can_admin(canAdmin);
	state.set_social_announcements_enabled(server->bStonksSocialAnnouncementsEnabled);
	state.set_automatic_valuation_enabled(server->bStonksAutoValuationEnabled);
	state.set_valuation_interval_minutes(server->uiStonksValuationIntervalMinutes);
	state.set_valuation_history_days(server->uiStonksValuationHistoryDays);
	state.set_valuation_status(u8(server->m_stonksValuationStatus));
	if (server->m_stonksValuationLastRunAt > 0) {
		state.set_valuation_last_run_at(static_cast< uint64_t >(server->m_stonksValuationLastRunAt));
		state.set_valuation_next_run_at(static_cast< uint64_t >(
			server->m_stonksValuationLastRunAt
			+ static_cast< qint64 >(server->uiStonksValuationIntervalMinutes) * 60));
	}
	if (server->m_stonksValuationLastBackfillAt > 0) {
		state.set_history_backfilled_from(static_cast< uint64_t >(server->m_stonksValuationLastBackfillAt));
	}
	state.set_leaderboard_description(
		"Time-weighted return: portfolio edits and contributions are excluded from investment performance. "
		"Estimated or partial coverage is labelled.");
	if (!status.trimmed().isEmpty()) {
		state.set_status(u8(status.trimmed()));
	}
	if (!error.trimmed().isEmpty()) {
		state.set_error(u8(error.trimmed()));
	}

	for (const QString &supportedPeriod : Mumble::Stonks::ledgerPeriods()) {
		state.add_periods(u8(supportedPeriod));
	}

	for (const Mumble::Stonks::PopularTickerSummary &ticker : stonksPopularTickers(server, 5)) {
		*state.add_popular_tickers() = protoStonksPopularTickerFromSummary(ticker, false);
	}
	if (registered && selfUserID) {
		for (const Mumble::Stonks::PopularTickerSummary &ticker : stonksPersonalTickers(server, *selfUserID, 5)) {
			*state.add_personal_tickers() = protoStonksPopularTickerFromSummary(ticker, true);
		}
		for (const ::msdb::DBStonksPinnedTicker &ticker :
			 server->m_dbWrapper.getStonksPinnedTickers(server->iServerNum, *selfUserID)) {
			*state.add_pinned_tickers() = protoStonksPinnedTickerFromDB(ticker);
		}
		const ::msdb::DBStonksFeedPreferences defaultPreferences(server->iServerNum, *selfUserID);
		const std::optional< ::msdb::DBStonksFeedPreferences > preferences =
			server->m_dbWrapper.getStonksFeedPreferences(server->iServerNum, *selfUserID);
		*state.mutable_feed_preferences() =
			protoStonksFeedPreferencesFromDB(preferences.value_or(defaultPreferences));
	} else {
		::msdb::DBStonksFeedPreferences defaultPreferences;
		*state.mutable_feed_preferences() = protoStonksFeedPreferencesFromDB(defaultPreferences);
	}

	std::vector<::msdb::DBTextChannel > textChannels = server->m_dbWrapper.getTextChannels(server->iServerNum);
	std::sort(textChannels.begin(), textChannels.end(), sortTextChannelsForPresentation);
	const std::optional< unsigned int > stonksTextChannelID =
		resolvedStonksTextChannelID(server->uiStonksTextChannelID, textChannels);
	if (stonksTextChannelID) {
		state.set_text_channel_id(*stonksTextChannelID);
	}
	if (canAdmin) {
		for (const ::msdb::DBTextChannel &textChannel : textChannels) {
			appendStonksTextChannelInfo(state, textChannel);
		}
	}

	QSet< unsigned int > followedUsers;
	if (registered) {
		for (unsigned int followedUserID :
			 server->m_dbWrapper.getStonksFollowedUsers(server->iServerNum, *selfUserID)) {
			followedUsers.insert(followedUserID);

			MumbleProto::StonksUserRef *followedRef = state.add_following();
			followedRef->set_user_id(followedUserID);
			followedRef->set_user_name(u8(stonksRegisteredDisplayName(server, followedUserID)));
			followedRef->set_followed(true);
		}
	}

	std::vector< std::pair< QString, unsigned int > > registeredUsers;
	for (unsigned int userID : server->m_dbWrapper.getRegisteredUserIDs(server->iServerNum)) {
		const QString userName = stonksRegisteredDisplayName(server, userID);
		if (!userName.trimmed().isEmpty()) {
			registeredUsers.push_back({ userName, userID });
		}
	}
	std::sort(registeredUsers.begin(), registeredUsers.end(), [](const auto &lhs, const auto &rhs) {
		const int byName = lhs.first.localeAwareCompare(rhs.first);
		return byName == 0 ? lhs.second < rhs.second : byName < 0;
	});
	for (const auto &registeredUser : registeredUsers) {
		MumbleProto::StonksUserRef *userRef = state.add_users();
		userRef->set_user_id(registeredUser.second);
		userRef->set_user_name(u8(registeredUser.first));
		userRef->set_followed(followedUsers.contains(registeredUser.second));
	}

	unsigned int rank = 1;
	qint64 leaderboardUpdatedAt = 0;
	for (const StonksLeaderboardEntry &entry : stonksLedgerLeaderboard(server, normalizedPeriod, 100)) {
		MumbleProto::StonksLeaderboardRow *row = state.add_leaderboard();
		row->set_rank(rank++);
		row->set_user_id(entry.userID);
		row->set_user_name(u8(entry.userName));
		row->set_period(u8(normalizedPeriod));
		row->set_return_percent(entry.score);
		row->set_followed(followedUsers.contains(entry.userID));
		row->set_insufficient_history(entry.insufficientHistory);
		if (!entry.insufficientHistory) {
			row->set_start_value(entry.window.startValue);
			row->set_end_value(entry.window.endValue);
			row->set_start_snapshot_at(static_cast< uint64_t >(entry.window.startAt));
			row->set_end_snapshot_at(static_cast< uint64_t >(entry.window.endAt));
			row->set_partial_period(entry.window.partialPeriod);
			row->set_coverage_seconds(static_cast< uint64_t >(entry.window.coverageSeconds));
			row->set_requested_seconds(static_cast< uint64_t >(entry.window.requestedSeconds));
			row->set_sample_count(entry.window.sampleCount);
			row->set_method("time-weighted-return");
			row->set_estimated(entry.window.estimated);
			leaderboardUpdatedAt = std::max(leaderboardUpdatedAt, entry.window.endAt);
		}
	}
	if (leaderboardUpdatedAt > 0) {
		state.set_leaderboard_updated_at(static_cast< uint64_t >(leaderboardUpdatedAt));
	}

	std::optional< unsigned int > ledgerUserID;
	if (canAdmin && requestedUserID) {
		ledgerUserID = requestedUserID;
	} else if (registered) {
		ledgerUserID = selfUserID;
	}
	if (ledgerUserID && server->m_dbWrapper.registeredUserExists(server->iServerNum, *ledgerUserID)) {
		state.set_selected_user_id(*ledgerUserID);
		state.set_selected_user_name(u8(stonksRegisteredDisplayName(server, *ledgerUserID)));

		const bool includePositions = canAdmin || (registered && selfUserID && *ledgerUserID == *selfUserID);
		for (const ::msdb::DBStonksSnapshot &snapshot : server->m_dbWrapper.getStonksSnapshotsForUser(
				 server->iServerNum, *ledgerUserID, MAX_STONKS_LEDGER_SNAPSHOTS)) {
			std::vector< ::msdb::DBStonksSnapshotPosition > positions;
			if (includePositions) {
				positions = server->m_dbWrapper.getStonksSnapshotPositions(server->iServerNum, snapshot.snapshotID);
			}
			*state.add_snapshots() = protoStonksSnapshotFromDB(
				snapshot, positions, stonksRegisteredDisplayName(server, snapshot.userID), includePositions);
		}

		const auto now = std::chrono::system_clock::now();
		const auto cutoff = stonksCutoffForPeriod(normalizedPeriod, now) - std::chrono::days(4);
		const std::vector< ::msdb::DBStonksValuation > valuations =
			server->m_dbWrapper.getStonksValuationsForUser(
				server->iServerNum, *ledgerUserID, cutoff, MAX_STONKS_VALUATIONS_PER_QUERY);
		constexpr std::size_t maxChartPoints = 500;
		if (valuations.size() <= maxChartPoints) {
			for (const ::msdb::DBStonksValuation &valuation : valuations) {
				*state.add_valuation_points() = protoStonksValuationFromDB(valuation);
			}
		} else {
			for (std::size_t i = 0; i < maxChartPoints; ++i) {
				const std::size_t index = i * (valuations.size() - 1) / (maxChartPoints - 1);
				*state.add_valuation_points() = protoStonksValuationFromDB(valuations[index]);
			}
		}
	}

	return state;
}

struct ValidatedStonksSnapshot {
	::msdb::DBStonksSnapshot snapshot;
	std::vector< ::msdb::DBStonksSnapshotPosition > positions;
};

QString normalizedStonksCurrency(const QString &currencyText) {
	const QString currency = currencyText.trimmed().toUpper().left(16);
	return currency.isEmpty() ? QStringLiteral("USD") : currency;
}

QString normalizedStonksProviderID(QString providerID) {
	providerID = providerID.trimmed().toLower().left(64);
	providerID.replace(QRegularExpression(QStringLiteral("[^a-z0-9_.-]+")), QStringLiteral("-"));
	while (providerID.contains(QLatin1String("--"))) {
		providerID.replace(QLatin1String("--"), QLatin1String("-"));
	}
	providerID = providerID.trimmed();
	if (providerID.startsWith(QLatin1Char('-'))) {
		providerID.remove(0, 1);
	}
	if (providerID.endsWith(QLatin1Char('-'))) {
		providerID.chop(1);
	}
	return providerID.isEmpty() ? QStringLiteral("manual") : providerID;
}

QString validatedStonksQuoteSourceURL(QString sourceURL) {
	sourceURL = sourceURL.trimmed().left(2048);
	if (sourceURL.isEmpty()) {
		return QString();
	}

	const QUrl url(sourceURL);
	const QString scheme = url.scheme().toLower();
	return url.isValid() && (scheme == QLatin1String("https") || scheme == QLatin1String("http"))
			   ? sourceURL
			   : QString();
}

double boundedStonksQuoteConfidence(double confidence) {
	if (!std::isfinite(confidence)) {
		return 0.0;
	}
	return std::clamp(confidence, 0.0, 1.0);
}

std::optional< ::msdb::DBStonksPinnedTicker > validatedStonksPinnedTickerFromProto(
	unsigned int serverID, unsigned int userID, const MumbleProto::StonksPinnedTicker &protoTicker, QString *error) {
	const QString symbol = Mumble::Finance::normalizeTickerSymbol(u8(protoTicker.symbol()));
	if (symbol.isEmpty()) {
		if (error) {
			*error = QStringLiteral("Choose a valid ticker symbol to pin.");
		}
		return std::nullopt;
	}

	::msdb::DBStonksPinnedTicker ticker(serverID, userID, u8(symbol));
	const QString displayName = protoTicker.has_display_name() ? u8(protoTicker.display_name()).trimmed().left(128)
															   : QString();
	const QString providerID =
		normalizedStonksProviderID(protoTicker.has_provider_id() ? u8(protoTicker.provider_id()) : QString());
	ticker.displayName = u8(displayName);
	ticker.providerID  = u8(providerID);
	const QString providerSymbol =
		protoTicker.has_provider_symbol() ? u8(protoTicker.provider_symbol()).trimmed().left(64) : symbol;
	ticker.providerSymbol = u8(providerSymbol.isEmpty() ? symbol : providerSymbol);
	ticker.exchange       = u8(protoTicker.has_exchange() ? u8(protoTicker.exchange()).trimmed().left(64) : QString());
	ticker.quoteSourceURL =
		u8(validatedStonksQuoteSourceURL(protoTicker.has_quote_source_url() ? u8(protoTicker.quote_source_url())
																		: QString()));
	ticker.displayOrder = protoTicker.has_display_order()
							  ? std::min(protoTicker.display_order(), static_cast< uint32_t >(1000))
							  : 0u;
	ticker.createdAt = std::chrono::system_clock::now();
	ticker.updatedAt = ticker.createdAt;
	return ticker;
}

::msdb::DBStonksFeedPreferences stonksFeedPreferencesFromProto(
	unsigned int serverID, unsigned int userID, const MumbleProto::StonksFeedPreferences &protoPreferences) {
	::msdb::DBStonksFeedPreferences preferences(serverID, userID);
	preferences.showMine    = !protoPreferences.has_show_mine() || protoPreferences.show_mine();
	preferences.showPopular = !protoPreferences.has_show_popular() || protoPreferences.show_popular();
	preferences.showPins    = !protoPreferences.has_show_pins() || protoPreferences.show_pins();
	preferences.updatedAt   = std::chrono::system_clock::now();
	return preferences;
}

bool stonksValuesClose(double expected, double actual) {
	if (!std::isfinite(expected) || !std::isfinite(actual)) {
		return false;
	}
	const double tolerance = std::max(0.01, std::abs(expected) * 0.001);
	return std::abs(expected - actual) <= tolerance;
}

std::optional< ValidatedStonksSnapshot > validatedStonksSnapshotFromProto(
	unsigned int serverID, unsigned int userID, const MumbleProto::StonksSnapshot &protoSnapshot, QString *error) {
	if (protoSnapshot.positions_size() <= 0) {
		if (error) {
			*error = QStringLiteral("Add at least one position before updating your ledger.");
		}
		return std::nullopt;
	}
	if (protoSnapshot.positions_size() > MAX_STONKS_LEDGER_POSITIONS) {
		if (error) {
			*error = QStringLiteral("Ledgers are capped at %1 positions.").arg(MAX_STONKS_LEDGER_POSITIONS);
		}
		return std::nullopt;
	}

	const QString snapshotCurrency =
		normalizedStonksCurrency(protoSnapshot.has_currency() ? u8(protoSnapshot.currency()) : QString());

	ValidatedStonksSnapshot validated;
	validated.snapshot              = ::msdb::DBStonksSnapshot(serverID, 0, userID);
	validated.snapshot.createdAt    = std::chrono::system_clock::now();
	validated.snapshot.currency     = u8(snapshotCurrency);
	validated.snapshot.note         = u8(u8(protoSnapshot.note()).trimmed().left(512));
	validated.snapshot.totalValue   = 0.0;
	validated.positions.reserve(static_cast< std::size_t >(protoSnapshot.positions_size()));

	QSet< QString > seenSymbols;
	for (int i = 0; i < protoSnapshot.positions_size(); ++i) {
		const MumbleProto::StonksPosition &protoPosition = protoSnapshot.positions(i);
		const QString symbol = Mumble::Finance::normalizeTickerSymbol(u8(protoPosition.symbol()));
		if (symbol.isEmpty()) {
			if (error) {
				*error = QStringLiteral("Position %1 has an invalid ticker symbol.").arg(i + 1);
			}
			return std::nullopt;
		}
		if (seenSymbols.contains(symbol)) {
			if (error) {
				*error = QStringLiteral("Position %1 duplicates ticker %2.").arg(i + 1).arg(symbol);
			}
			return std::nullopt;
		}
		seenSymbols.insert(symbol);

		const double quantity = protoPosition.has_quantity() ? protoPosition.quantity() : 0.0;
		const double price    = protoPosition.has_price() ? protoPosition.price() : 0.0;
		const double computedMarketValue = quantity * price;
		const double submittedMarketValue =
			protoPosition.has_market_value() ? protoPosition.market_value() : computedMarketValue;
		if (!std::isfinite(quantity) || !std::isfinite(price) || !std::isfinite(computedMarketValue)
			|| !std::isfinite(submittedMarketValue) || quantity <= 0.0 || price <= 0.0
			|| computedMarketValue <= 0.0 || std::abs(quantity) > 1000000000000.0
			|| price > 1000000000000.0 || computedMarketValue > 1000000000000000.0) {
			if (error) {
				*error = QStringLiteral("Position %1 has invalid numeric values.").arg(i + 1);
			}
			return std::nullopt;
		}
		if (!stonksValuesClose(computedMarketValue, submittedMarketValue)) {
			if (error) {
				*error = QStringLiteral("Position %1 value must match quantity x price.").arg(i + 1);
			}
			return std::nullopt;
		}

		const QString currency =
			normalizedStonksCurrency(protoPosition.has_currency() ? u8(protoPosition.currency()) : snapshotCurrency);
		if (currency != snapshotCurrency) {
			if (error) {
				*error = QStringLiteral("Position %1 uses %2, but the ledger currency is %3.")
							 .arg(i + 1)
							 .arg(currency, snapshotCurrency);
			}
			return std::nullopt;
		}

		::msdb::DBStonksSnapshotPosition position(serverID, 0, static_cast< unsigned int >(validated.positions.size()),
												  u8(symbol));
		position.quantity    = quantity;
		position.price       = price;
		position.marketValue = computedMarketValue;
		position.currency    = u8(currency);
		position.displayName = u8(u8(protoPosition.display_name()).trimmed().left(256));
		position.providerID =
			u8(normalizedStonksProviderID(protoPosition.has_provider_id() ? u8(protoPosition.provider_id()) : QString()));
		const QString providerSymbol =
			protoPosition.has_provider_symbol() ? u8(protoPosition.provider_symbol()).trimmed().left(64) : symbol;
		position.providerSymbol = u8(providerSymbol.isEmpty() ? symbol : providerSymbol);
		position.exchange =
			u8(protoPosition.has_exchange() ? u8(protoPosition.exchange()).trimmed().left(64) : QString());
		position.quoteTime = protoPosition.has_quote_time()
								 ? static_cast< long long >(std::min< uint64_t >(protoPosition.quote_time(),
																				  std::numeric_limits< long long >::max()))
								 : 0LL;
		position.quoteSourceURL = u8(validatedStonksQuoteSourceURL(
			protoPosition.has_quote_source_url() ? u8(protoPosition.quote_source_url()) : QString()));
		const double defaultConfidence = position.providerID == "manual" ? 0.35 : 0.75;
		position.quoteConfidence       = boundedStonksQuoteConfidence(
			protoPosition.has_quote_confidence() ? protoPosition.quote_confidence() : defaultConfidence);
		validated.snapshot.totalValue += computedMarketValue;
		validated.positions.push_back(std::move(position));
	}

	if (!std::isfinite(validated.snapshot.totalValue) || validated.snapshot.totalValue <= 0.0) {
		if (error) {
			*error = QStringLiteral("Ledger total must be greater than zero.");
		}
		return std::nullopt;
	}
	if (protoSnapshot.has_total_value() && !stonksValuesClose(validated.snapshot.totalValue, protoSnapshot.total_value())) {
		if (error) {
			*error = QStringLiteral("Ledger total must match the sum of accepted positions.");
		}
		return std::nullopt;
	}

	return validated;
}

std::map< QString, ::msdb::DBStonksSnapshotPosition >
	stonksPositionsBySymbol(const std::vector< ::msdb::DBStonksSnapshotPosition > &positions) {
	std::map< QString, ::msdb::DBStonksSnapshotPosition > bySymbol;
	for (const ::msdb::DBStonksSnapshotPosition &position : positions) {
		const QString symbol = Mumble::Finance::normalizeTickerSymbol(u8(position.symbol));
		if (!symbol.isEmpty()) {
			bySymbol[symbol] = position;
		}
	}
	return bySymbol;
}

QString stonksPricePerShareText(const ::msdb::DBStonksSnapshotPosition &position) {
	if (!std::isfinite(position.price) || position.price <= 0.0) {
		return QString();
	}
	return QStringLiteral("%1/share").arg(formatStonksMoney(position.price, u8(position.currency)));
}

QString stonksSymbolWithPrice(const QString &symbol, const ::msdb::DBStonksSnapshotPosition &position) {
	const QString priceText = stonksPricePerShareText(position);
	return priceText.isEmpty() ? symbol : QStringLiteral("%1 at %2").arg(symbol, priceText);
}

QString joinedStonksLedgerActions(QStringList actions) {
	actions.removeAll(QString());
	const qsizetype maxVisibleActions = 4;
	if (actions.size() > maxVisibleActions) {
		actions = actions.mid(0, maxVisibleActions);
		actions << QStringLiteral("made more ledger updates");
	}
	if (actions.isEmpty()) {
		return QString();
	}
	if (actions.size() == 1) {
		return actions.front();
	}
	if (actions.size() == 2) {
		return QStringLiteral("%1 and %2").arg(actions.at(0), actions.at(1));
	}

	const QString last = actions.takeLast();
	return QStringLiteral("%1, and %2").arg(actions.join(QStringLiteral(", ")), last);
}

QString stonksSocialAnnouncement(Server *server, const ::msdb::DBStonksSnapshot &snapshot,
								 const std::vector< ::msdb::DBStonksSnapshotPosition > &positions,
								 const std::vector< ::msdb::DBStonksSnapshotPosition > &previousPositions) {
	const QString userName = stonksRegisteredDisplayName(server, snapshot.userID);
	const std::map< QString, ::msdb::DBStonksSnapshotPosition > currentBySymbol = stonksPositionsBySymbol(positions);
	const std::map< QString, ::msdb::DBStonksSnapshotPosition > previousBySymbol =
		stonksPositionsBySymbol(previousPositions);

	QStringList actions;
	for (const auto &currentEntry : currentBySymbol) {
		const QString &symbol = currentEntry.first;
		const ::msdb::DBStonksSnapshotPosition &position = currentEntry.second;
		const auto previousIt = previousBySymbol.find(symbol);
		if (previousIt == previousBySymbol.cend()) {
			actions << QStringLiteral("entered %1").arg(stonksSymbolWithPrice(symbol, position));
			continue;
		}

		const double previousQuantity = previousIt->second.quantity;
		if (!stonksValuesClose(previousQuantity, position.quantity)) {
			actions << QStringLiteral("%1 %2")
						   .arg(position.quantity > previousQuantity ? QStringLiteral("added more")
																	  : QStringLiteral("removed some"),
								stonksSymbolWithPrice(symbol, position));
			continue;
		}
		if (!stonksValuesClose(previousIt->second.price, position.price)) {
			actions << QStringLiteral("updated %1").arg(stonksSymbolWithPrice(symbol, position));
		}
	}
	for (const auto &previousEntry : previousBySymbol) {
		if (currentBySymbol.find(previousEntry.first) == currentBySymbol.cend()) {
			actions << QStringLiteral("exited %1").arg(previousEntry.first);
		}
	}

	const QString actionSummary = joinedStonksLedgerActions(actions);
	if (!actionSummary.isEmpty()) {
		return QStringLiteral("Stonks: %1 %2.").arg(userName, actionSummary);
	}

	return QStringLiteral("Stonks: %1 updated their ledger.").arg(userName);
}

QString stonksClearAnnouncement(Server *server, const ::msdb::DBStonksSnapshot &snapshot) {
	return QStringLiteral("Stonks: %1 cleared their ledger.")
		.arg(stonksRegisteredDisplayName(server, snapshot.userID));
}
} // namespace

void Server::sendPersistentChatUnsupported(ServerUser *uSource) {
	MumbleProto::PermissionDenied denied;
	denied.set_session(uSource->uiSession);
	denied.set_type(MumbleProto::PermissionDenied_DenyType_Text);
	denied.set_reason(u8(QStringLiteral(
		"This client version did not advertise support for the required persistent chat feature.")));
	sendMessage(uSource, denied);
}

void Server::sendTextChannelSync(ServerUser *uSource) {
	if (!clientSupportsChatFeature(uSource, MumbleProto::ChatFeatureTextChannels)) {
		return;
	}

	QMutexLocker qml(&qmCache);

	MumbleProto::TextChannelSync sync;
	std::vector<::msdb::DBTextChannel > textChannels = m_dbWrapper.getTextChannels(iServerNum);
	std::sort(textChannels.begin(), textChannels.end(), sortTextChannelsForPresentation);
	const std::optional< unsigned int > configuredDefaultTextChannel =
		configuredDefaultTextChannelID(m_dbWrapper, iServerNum);
	std::optional< unsigned int > fallbackDefaultTextChannelID;

	for (const ::msdb::DBTextChannel &currentTextChannel : textChannels) {
		Channel *permissionChannel = qhChannels.value(currentTextChannel.aclChannelID);
		if (!permissionChannel) {
			continue;
		}
		if (isStonksTextChannelID(currentTextChannel.textChannelID)
			&& !hasStonksAccess(uSource, &acCache)) {
			continue;
		}

		if (!canReceiveLivePersistentChat(uSource, MumbleProto::TextChannel, permissionChannel, acCache)
			&& !resolveChatHistoryAccess(uSource, MumbleProto::TextChannel, currentTextChannel.textChannelID,
										 permissionChannel, &acCache).allowed) {
			continue;
		}

		MumbleProto::TextChannelInfo *protoChannel = sync.add_channels();
		protoChannel->set_text_channel_id(currentTextChannel.textChannelID);
		protoChannel->set_name(currentTextChannel.name);
		protoChannel->set_description(currentTextChannel.description);
		protoChannel->set_acl_channel_id(currentTextChannel.aclChannelID);
		protoChannel->set_position(currentTextChannel.position);

		if (!fallbackDefaultTextChannelID) {
			fallbackDefaultTextChannelID = currentTextChannel.textChannelID;
		}
		if (configuredDefaultTextChannel && *configuredDefaultTextChannel == currentTextChannel.textChannelID) {
			sync.set_default_text_channel_id(currentTextChannel.textChannelID);
		}
	}

	if (!sync.has_default_text_channel_id() && fallbackDefaultTextChannelID) {
		sync.set_default_text_channel_id(*fallbackDefaultTextChannelID);
	}

	sendMessage(uSource, sync);
}

void Server::persistAndBroadcastChatMessage(ServerUser *uSource, const QString &bodyText,
											::msdb::ChatMessageBodyFormat bodyFormat, MumbleProto::ChatScope scope,
											unsigned int scopeID, Channel *permissionChannel,
											::msdb::ChatThreadScope dbScope,
											const std::vector<::msdb::DBChatMessageAttachment > &attachments,
											std::optional< unsigned int > replyToMessageID,
											const QSet< ServerUser * > &legacyFallbackRecipients) {
	const std::optional< unsigned int > authorUserID = persistedUserID(uSource);
	const std::optional< std::string > authorName =
		uSource->qsName.isEmpty() ? std::nullopt : std::optional< std::string >(u8(uSource->qsName));
	const std::string scopeKey =
		scope == MumbleProto::Private && authorUserID ? privateChatScopeKey(authorUserID.value(), scopeID)
													  : chatScopeKey(scope, scopeID);
	if (scopeKey.empty()) {
		return;
	}

	::msdb::DBChatThread thread = m_dbWrapper.ensureChatThread(iServerNum, dbScope, scopeKey, authorUserID);
	::msdb::DBChatMessage storedMessage =
		m_dbWrapper.addChatMessage(iServerNum, thread.threadID, u8(bodyText), bodyFormat, attachments, replyToMessageID,
								   authorUserID, uSource->uiSession, authorName);

	const auto now = std::chrono::system_clock::now();
	std::vector<::msdb::DBChatMessageEmbed > initialEmbeds;
	if (bChatPreviewFetchEnabled) {
		QSet< QString > seenUrls;
		const auto appendInitialEmbed = [&](const QUrl &previewUrl) -> bool {
			const QString canonicalUrl = previewUrl.adjusted(QUrl::RemoveFragment).toString();
			if (canonicalUrl.isEmpty() || seenUrls.contains(canonicalUrl)) {
				return initialEmbeds.size() < 3;
			}
			seenUrls.insert(canonicalUrl);

			::msdb::DBChatMessageEmbed embed(iServerNum, storedMessage.messageID);
			embed.urlHash      = u8(QString::fromLatin1(
				QCryptographicHash::hash(canonicalUrl.toUtf8(), QCryptographicHash::Sha256).toHex()));
			embed.canonicalUrl = u8(canonicalUrl);
			embed.status =
				isSafePreviewUrl(previewUrl) ? ::msdb::ChatEmbedStatus::Pending : ::msdb::ChatEmbedStatus::Blocked;
			embed.errorCode = embed.status == ::msdb::ChatEmbedStatus::Blocked ? "blocked_target" : "";
			embed.fetchedAt = now;
			embed.expiresAt = now + std::chrono::hours(24 * 7);
			initialEmbeds.push_back(std::move(embed));

			return initialEmbeds.size() < 3;
		};

		const QList< QUrl > previewUrls = extractPreviewableUrls(bodyText);
		for (const QUrl &previewUrl : previewUrls) {
			if (!appendInitialEmbed(previewUrl)) {
				break;
			}
		}

		if (initialEmbeds.size() < 3) {
			for (const Mumble::Finance::TickerMention &mention : Mumble::Finance::extractTickerMentions(bodyText)) {
				if (!appendInitialEmbed(mention.yahooFinanceUrl)) {
					break;
				}
			}
		}
	}

	if (!initialEmbeds.empty()) {
		m_dbWrapper.setChatMessageEmbeds(iServerNum, storedMessage.messageID, initialEmbeds);
		storedMessage.embeds = initialEmbeds;
	}
	rememberLatestChatHistoryMessage(storedMessage);

	const auto resolvedAuthorName = [this](const ::msdb::DBChatMessage &message) -> std::optional< std::string > {
		if (message.authorName && !message.authorName->empty()) {
			return message.authorName;
		}

		if (message.authorSession) {
			ServerUser *currentUser = qhUsers.value(message.authorSession.value());
			if (currentUser && !currentUser->qsName.isEmpty()) {
				return u8(currentUser->qsName);
			}
		}

		if (message.authorUserID && m_dbWrapper.registeredUserExists(iServerNum, message.authorUserID.value())) {
			return m_dbWrapper.getUserName(iServerNum, message.authorUserID.value());
		}

		return std::nullopt;
	};
	const auto resolvedReactionActorName = [this](unsigned int actorUserID) -> std::optional< std::string > {
		const std::optional< std::string > connectedName = connectedUserNameForPersistentID(qhUsers, actorUserID);
		if (connectedName) {
			return connectedName;
		}

		if (actorUserID <= static_cast< unsigned int >(std::numeric_limits< int >::max())) {
			const QString registeredName = getRegisteredUserName(static_cast< int >(actorUserID)).trimmed();
			if (!registeredName.isEmpty()) {
				return u8(registeredName);
			}
		}

		return std::nullopt;
	};
	QHash< unsigned int, QByteArray > actorTextureHashCache;
	const auto resolvedActorTextureHash = [this, &actorTextureHashCache](unsigned int actorUserID) {
		return registeredUserTextureHash(this, actorUserID, actorTextureHashCache);
	};
	QSet< ServerUser * > persistentRecipients;
	if (scope == MumbleProto::Private && authorUserID) {
		persistentRecipients = connectedPrivateChatParticipants(qhUsers, authorUserID.value(), scopeID);
	} else if (scope == MumbleProto::Channel) {
		persistentRecipients = recipientsWithChatHistoryAccess(this, qhUsers, scope, scopeID, permissionChannel, acCache,
														   storedMessage.createdAt);
		persistentRecipients.unite(recipientsWithLivePersistentChatAccess(
			this, qhUsers, scope, scopeID, permissionChannel, acCache, legacyFallbackRecipients));
		persistentRecipients.insert(uSource);
	} else {
		persistentRecipients = recipientsWithChatHistoryAccess(this, qhUsers, scope, scopeID, permissionChannel, acCache,
															   storedMessage.createdAt);
		persistentRecipients.unite(
			recipientsWithLivePersistentChatAccess(this, qhUsers, scope, scopeID, permissionChannel, acCache));
	}

	for (ServerUser *currentUser : persistentRecipients) {
		if (clientSupportsPersistentChat(currentUser)
			&& (scope != MumbleProto::Private
				|| clientSupportsChatFeature(currentUser, MumbleProto::ChatFeatureDirectMessages))) {
			unsigned int messageScopeID = scopeID;
			if (scope == MumbleProto::Private) {
				const std::optional< unsigned int > currentUserID = persistedUserID(currentUser);
				const std::optional< unsigned int > peerUserID =
					currentUserID ? privateChatPeerIDForViewer(scopeKey, currentUserID.value()) : std::nullopt;
				if (!peerUserID) {
					continue;
				}
				messageScopeID = peerUserID.value();
			}
			const ChatHistoryAccess access =
				resolveChatHistoryAccess(currentUser, scope, messageScopeID, permissionChannel, &acCache);
			std::optional< ChatReplyPreview > replyPreview;
			if (access.allowed) {
				replyPreview = resolveReplyPreview(m_dbWrapper, iServerNum, storedMessage, resolvedAuthorName,
												   access.visibleAfter);
			}
			const MumbleProto::ChatMessage protoMessage = protoChatMessageFromDB(
				storedMessage, scope, messageScopeID, authorName, persistedUserID(currentUser), replyPreview,
				effectiveChatFeatures(currentUser), resolvedReactionActorName, resolvedActorTextureHash);
			sendMessage(currentUser, protoMessage);
		}
	}

	if (scope == MumbleProto::Private && authorUserID) {
		MumbleProto::TextMessage legacyDirectMessage;
		legacyDirectMessage.set_actor(uSource->uiSession);
		legacyDirectMessage.set_message(
			u8(structuredChatLegacyHtml(appendChatAttachmentFallbacks(bodyText, attachments), bodyFormat)));
		for (ServerUser *currentUser : connectedPrivateChatParticipants(qhUsers, authorUserID.value(), scopeID)) {
			if (!currentUser || currentUser == uSource
				|| clientSupportsChatFeature(currentUser, MumbleProto::ChatFeatureDirectMessages)) {
				continue;
			}
			legacyDirectMessage.clear_session();
			legacyDirectMessage.add_session(currentUser->uiSession);
			sendMessage(currentUser, legacyDirectMessage);
		}
	}

	if (!legacyFallbackRecipients.isEmpty()) {
		const MumbleProto::ChatMessage protoMessage =
			protoChatMessageFromDB(storedMessage, scope, scopeID, authorName, std::nullopt, std::nullopt);
		MumbleProto::TextMessage legacyMessage = legacyTextMessageFromPersistent(protoMessage);
		for (ServerUser *currentUser : legacyFallbackRecipients) {
			if (!currentUser || currentUser == uSource
				|| (clientSupportsPersistentChat(currentUser) && persistentRecipients.contains(currentUser))) {
				continue;
			}

			sendMessage(currentUser, legacyMessage);
		}
	}

	if (authorUserID) {
		::msdb::DBChatReadState readState(iServerNum, thread.threadID, authorUserID.value());
		readState.lastReadMessageID = storedMessage.messageID;
		readState.updatedAt         = std::chrono::system_clock::now();
		m_dbWrapper.setChatReadState(readState);

		if (clientSupportsChatFeature(uSource, MumbleProto::ChatFeatureReadState)) {
			std::optional<::msdb::DBChatReadState > persistedReadState =
				m_dbWrapper.getChatReadState(iServerNum, thread.threadID, authorUserID.value());
			if (persistedReadState) {
				sendMessage(uSource, protoReadStateFromDB(*persistedReadState, scope, scopeID));
			}
		}
	}

	if (bChatPreviewFetchEnabled && permissionChannel && scope != MumbleProto::Private) {
		for (const ::msdb::DBChatMessageEmbed &embed : initialEmbeds) {
			if (embed.status == ::msdb::ChatEmbedStatus::Pending) {
				scheduleChatEmbedFetch(uSource, thread.threadID, storedMessage.messageID, scope, scopeID,
									   static_cast< unsigned int >(permissionChannel->iId), embed);
			}
		}
	}
}

void Server::persistAndBroadcastServerChatMessage(const QString &bodyText, MumbleProto::ChatScope scope,
												  unsigned int scopeID, Channel *permissionChannel,
												  ::msdb::ChatThreadScope dbScope, const QString &authorName) {
	if (bodyText.trimmed().isEmpty()) {
		return;
	}

	const std::string scopeKey = chatScopeKey(scope, scopeID);
	if (scopeKey.empty()) {
		return;
	}

	::msdb::DBChatThread thread = m_dbWrapper.ensureChatThread(iServerNum, dbScope, scopeKey, std::nullopt);
	const std::optional< std::string > resolvedAuthorName =
		authorName.trimmed().isEmpty() ? std::nullopt : std::optional< std::string >(u8(authorName.trimmed()));
	::msdb::DBChatMessage storedMessage = m_dbWrapper.addChatMessage(
		iServerNum, thread.threadID, u8(bodyText), ::msdb::ChatMessageBodyFormat::MarkdownLite, {}, std::nullopt,
		std::nullopt, std::nullopt, resolvedAuthorName);

	const auto now = std::chrono::system_clock::now();
	std::vector<::msdb::DBChatMessageEmbed > initialEmbeds;
	if (bChatPreviewFetchEnabled) {
		QSet< QString > seenUrls;
		const auto appendInitialEmbed = [&](const QUrl &previewUrl) -> bool {
			const QString canonicalUrl = previewUrl.adjusted(QUrl::RemoveFragment).toString();
			if (canonicalUrl.isEmpty() || seenUrls.contains(canonicalUrl)) {
				return initialEmbeds.size() < 3;
			}
			seenUrls.insert(canonicalUrl);

			::msdb::DBChatMessageEmbed embed(iServerNum, storedMessage.messageID);
			embed.urlHash      = u8(QString::fromLatin1(
				QCryptographicHash::hash(canonicalUrl.toUtf8(), QCryptographicHash::Sha256).toHex()));
			embed.canonicalUrl = u8(canonicalUrl);
			embed.status =
				isSafePreviewUrl(previewUrl) ? ::msdb::ChatEmbedStatus::Pending : ::msdb::ChatEmbedStatus::Blocked;
			embed.errorCode = embed.status == ::msdb::ChatEmbedStatus::Blocked ? "blocked_target" : "";
			embed.fetchedAt = now;
			embed.expiresAt = now + std::chrono::hours(24 * 7);
			initialEmbeds.push_back(std::move(embed));

			return initialEmbeds.size() < 3;
		};

		const QList< QUrl > previewUrls = extractPreviewableUrls(bodyText);
		for (const QUrl &previewUrl : previewUrls) {
			if (!appendInitialEmbed(previewUrl)) {
				break;
			}
		}

		if (initialEmbeds.size() < 3) {
			for (const Mumble::Finance::TickerMention &mention : Mumble::Finance::extractTickerMentions(bodyText)) {
				if (!appendInitialEmbed(mention.yahooFinanceUrl)) {
					break;
				}
			}
		}
	}

	if (!initialEmbeds.empty()) {
		m_dbWrapper.setChatMessageEmbeds(iServerNum, storedMessage.messageID, initialEmbeds);
		storedMessage.embeds = initialEmbeds;
	}
	rememberLatestChatHistoryMessage(storedMessage);

	const auto resolvedReactionActorName = [this](unsigned int actorUserID) -> std::optional< std::string > {
		const std::optional< std::string > connectedName = connectedUserNameForPersistentID(qhUsers, actorUserID);
		if (connectedName) {
			return connectedName;
		}

		if (actorUserID <= static_cast< unsigned int >(std::numeric_limits< int >::max())) {
			const QString registeredName = getRegisteredUserName(static_cast< int >(actorUserID)).trimmed();
			if (!registeredName.isEmpty()) {
				return u8(registeredName);
			}
		}

		return std::nullopt;
	};
	QHash< unsigned int, QByteArray > actorTextureHashCache;
	const auto resolvedActorTextureHash = [this, &actorTextureHashCache](unsigned int actorUserID) {
		return registeredUserTextureHash(this, actorUserID, actorTextureHashCache);
	};

	QSet< ServerUser * > persistentRecipients;
	if (scope == MumbleProto::Channel) {
		persistentRecipients = recipientsWithChatHistoryAccess(this, qhUsers, scope, scopeID, permissionChannel, acCache,
															   storedMessage.createdAt);
		persistentRecipients.unite(
			recipientsWithLivePersistentChatAccess(this, qhUsers, scope, scopeID, permissionChannel, acCache, {}));
	} else {
		persistentRecipients = recipientsWithChatHistoryAccess(this, qhUsers, scope, scopeID, permissionChannel, acCache,
															   storedMessage.createdAt);
		persistentRecipients.unite(
			recipientsWithLivePersistentChatAccess(this, qhUsers, scope, scopeID, permissionChannel, acCache));
	}

	for (ServerUser *currentUser : persistentRecipients) {
		if (clientSupportsPersistentChat(currentUser)) {
			const MumbleProto::ChatMessage protoMessage =
				protoChatMessageFromDB(storedMessage, scope, scopeID, resolvedAuthorName, persistedUserID(currentUser),
									   std::nullopt, effectiveChatFeatures(currentUser), resolvedReactionActorName,
									   resolvedActorTextureHash);
			sendMessage(currentUser, protoMessage);
		}
	}

	if (bChatPreviewFetchEnabled && permissionChannel) {
		for (const ::msdb::DBChatMessageEmbed &embed : initialEmbeds) {
			if (embed.status == ::msdb::ChatEmbedStatus::Pending) {
				scheduleChatEmbedFetch(nullptr, thread.threadID, storedMessage.messageID, scope, scopeID,
									   static_cast< unsigned int >(permissionChannel->iId), embed);
			}
		}
	}
}

std::optional< unsigned int > Server::persistChatPreviewAsset(const QByteArray &bytes, const QString &mime,
													  msdb::ChatAssetKind kind, unsigned int width,
													  unsigned int height) {
	if (bytes.isEmpty() || mime.trimmed().isEmpty()) {
		return std::nullopt;
	}

	QString storageError;
	if (!ensureChatAssetStorageReady(&storageError)) {
		return std::nullopt;
	}

	const QString sha256 = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
	const QString storageKey = chatAssetStorageKey(0, sha256);
	const QString objectPath = chatAssetAbsolutePath(storageKey);
	const QFileInfo existingObject(objectPath);
	const quint64 existingBytes = existingObject.exists() && existingObject.size() > 0
		? static_cast< quint64 >(existingObject.size()) : 0;
	const quint64 requiredBytes = static_cast< quint64 >(bytes.size()) > existingBytes
		? static_cast< quint64 >(bytes.size()) - existingBytes : 0;
	const quint64 storedBytes = chatAssetStoredBytes();
	if (uiChatAssetTotalQuotaBytes > 0
		&& (storedBytes > uiChatAssetTotalQuotaBytes || requiredBytes > uiChatAssetTotalQuotaBytes - storedBytes)) {
		return std::nullopt;
	}
	if (!ensureContentAddressedObject(objectPath, bytes, sha256)) {
		return std::nullopt;
	}

	msdb::DBChatAsset asset;
	asset.serverID       = iServerNum;
	asset.sha256         = u8(sha256);
	asset.storageKey     = u8(storageKey);
	asset.mime           = u8(mime);
	asset.byteSize       = static_cast< std::uint64_t >(bytes.size());
	asset.kind           = kind;
	asset.width          = width;
	asset.height         = height;
	asset.retentionClass = msdb::ChatAssetRetentionClass::PreviewCache;

	return m_dbWrapper.addChatAsset(asset).assetID;
}

void Server::applyChatEmbedFetchResult(unsigned int threadID, unsigned int messageID, MumbleProto::ChatScope scope,
									   unsigned int scopeID, unsigned int permissionChannelID,
									   const ::msdb::DBChatMessageEmbed &embed) {
	QMutexLocker qml(&qmCache);

	std::optional<::msdb::DBChatMessage > message = m_dbWrapper.getChatMessage(iServerNum, messageID);
	if (!message || message->deletedAt > std::chrono::system_clock::time_point()) {
		return;
	}

	std::vector<::msdb::DBChatMessageEmbed > embeds = m_dbWrapper.getChatMessageEmbeds(iServerNum, messageID);
	const QString assistKey = chatEmbedAssistKey(messageID, QString::fromStdString(embed.urlHash));
	bool replaced                                   = false;
	for (auto &currentEmbed : embeds) {
		if (currentEmbed.urlHash == embed.urlHash) {
			if (currentEmbed.status != ::msdb::ChatEmbedStatus::Pending) {
				qhPendingChatEmbedAssists.remove(assistKey);
				return;
			}
			currentEmbed = embed;
			replaced     = true;
			break;
		}
	}
	if (!replaced) {
		embeds.push_back(embed);
	}
	qhPendingChatEmbedAssists.remove(assistKey);

	m_dbWrapper.setChatMessageEmbeds(iServerNum, messageID, embeds);
	invalidateChatHistoryCache(message->threadID);

	Channel *permissionChannel = qhChannels.value(permissionChannelID);
	if (!permissionChannel) {
		return;
	}

	MumbleProto::ChatEmbedState state;
	state.set_scope(scope);
	state.set_scope_id(scopeID);
	state.set_thread_id(threadID);
	state.set_message_id(messageID);
	for (const auto &currentEmbed : embeds) {
		*state.add_embeds() = protoEmbedRefFromDB(currentEmbed);
	}

	QSet< ServerUser * > recipients =
		recipientsWithChatHistoryAccess(this, qhUsers, scope, scopeID, permissionChannel, acCache, message->createdAt);
	if (scope == MumbleProto::Channel) {
		recipients.unite(recipientsWithLivePersistentChatAccess(
			this, qhUsers, scope, scopeID, permissionChannel, acCache,
			legacyChannelRecipients(qhUsers, m_channelListenerManager, permissionChannel)));
	} else {
		recipients.unite(
			recipientsWithLivePersistentChatAccess(this, qhUsers, scope, scopeID, permissionChannel, acCache));
	}

	for (ServerUser *currentUser : recipients) {
		if (clientSupportsChatFeature(currentUser, MumbleProto::ChatFeatureEmbeds)) {
			sendMessage(currentUser, state);
		}
	}
}

void Server::scheduleChatEmbedFetch(ServerUser *preferredHelper, unsigned int threadID, unsigned int messageID,
									MumbleProto::ChatScope scope, unsigned int scopeID,
									unsigned int permissionChannelID,
									const ::msdb::DBChatMessageEmbed &initialEmbed) {
	if (!bChatPreviewClientAssistEnabled) {
		scheduleServerChatEmbedFetch(threadID, messageID, scope, scopeID, permissionChannelID, initialEmbed);
		return;
	}

	const QString canonicalUrl = QString::fromStdString(initialEmbed.canonicalUrl);
	const QString urlHash      = QString::fromStdString(initialEmbed.urlHash);
	const QString assistKey    = chatEmbedAssistKey(messageID, urlHash);
	Channel *permissionChannel = qhChannels.value(permissionChannelID);
	const std::optional<::msdb::DBChatMessage > message = m_dbWrapper.getChatMessage(iServerNum, messageID);
	const std::optional<::msdb::DBChatThread > thread =
		message ? std::optional<::msdb::DBChatThread >(m_dbWrapper.getChatThread(iServerNum, message->threadID))
				: std::nullopt;
	if (canonicalUrl.isEmpty() || urlHash.isEmpty() || !permissionChannel || !message || !thread
		|| message->deletedAt > std::chrono::system_clock::time_point()) {
		scheduleServerChatEmbedFetch(threadID, messageID, scope, scopeID, permissionChannelID, initialEmbed);
		return;
	}

	const auto helperIsEligible = [&](ServerUser *user) {
		return user && user->sState == ServerUser::Authenticated
			   && clientSupportsChatFeature(user, MumbleProto::ChatFeatureEmbeds)
			   && clientSupportsForkFeature(user, MumbleProto::ForkFeatureClientAssistedLinkPreviews)
			   && canAccessChatMessage(user, *message, *thread, permissionChannel, &acCache);
	};

	ServerUser *helper = helperIsEligible(preferredHelper) ? preferredHelper : nullptr;
	if (!helper) {
		QList< unsigned int > sessions = qhUsers.keys();
		std::sort(sessions.begin(), sessions.end());
		for (unsigned int session : sessions) {
			ServerUser *candidate = qhUsers.value(session);
			if (helperIsEligible(candidate)) {
				helper = candidate;
				break;
			}
		}
	}
	if (!helper) {
		scheduleServerChatEmbedFetch(threadID, messageID, scope, scopeID, permissionChannelID, initialEmbed);
		return;
	}

	PendingChatEmbedAssist assist;
	assist.leaseID             = randomChatEmbedAssistLeaseID(qhPendingChatEmbedAssists);
	assist.helperSession       = helper->uiSession;
	assist.scope               = scope;
	assist.scopeID             = scopeID;
	assist.threadID            = threadID;
	assist.messageID           = messageID;
	assist.permissionChannelID = permissionChannelID;
	assist.canonicalUrl        = canonicalUrl;
	assist.urlHash             = urlHash;
	assist.expiresAt           = std::chrono::system_clock::now()
					   + std::chrono::milliseconds(uiChatPreviewClientAssistLeaseMs);
	qhPendingChatEmbedAssists.insert(assistKey, assist);

	MumbleProto::ChatEmbedAssistRequest request;
	request.set_scope(scope);
	request.set_scope_id(scopeID);
	request.set_thread_id(threadID);
	request.set_message_id(messageID);
	request.set_canonical_url(u8(canonicalUrl));
	request.set_url_hash(u8(urlHash));
	request.set_lease_id(assist.leaseID);
	request.set_lease_expires_at(toEpochMilliseconds(assist.expiresAt));
	request.set_max_thumbnail_bytes(uiChatPreviewClientAssistThumbnailMaxBytes);
	sendMessage(helper, request);

	QTimer::singleShot(static_cast< int >(uiChatPreviewClientAssistFallbackMs), this,
					   [this, assistKey, threadID, messageID, scope, scopeID, permissionChannelID, initialEmbed]() {
						   bool shouldFallback = false;
						   {
							   QMutexLocker qml(&qmCache);
							   auto assistIt = qhPendingChatEmbedAssists.find(assistKey);
							   if (assistIt != qhPendingChatEmbedAssists.end()) {
								   const std::vector<::msdb::DBChatMessageEmbed > embeds =
									   m_dbWrapper.getChatMessageEmbeds(iServerNum, messageID);
								   const auto pendingIt = std::find_if(
									   embeds.cbegin(), embeds.cend(), [&initialEmbed](const ::msdb::DBChatMessageEmbed &embed) {
										   return embed.urlHash == initialEmbed.urlHash
												  && embed.status == ::msdb::ChatEmbedStatus::Pending;
									   });
								   shouldFallback = pendingIt != embeds.cend();
								   if (shouldFallback) {
									   assistIt->fallbackStarted = true;
								   } else {
									   qhPendingChatEmbedAssists.erase(assistIt);
								   }
							   }
						   }
						   if (shouldFallback) {
							   scheduleServerChatEmbedFetch(threadID, messageID, scope, scopeID, permissionChannelID,
															initialEmbed);
						   }
					   });
	QTimer::singleShot(static_cast< int >(uiChatPreviewClientAssistLeaseMs), this, [this, assistKey, leaseID = assist.leaseID]() {
		QMutexLocker qml(&qmCache);
		auto assistIt = qhPendingChatEmbedAssists.find(assistKey);
		if (assistIt != qhPendingChatEmbedAssists.end() && assistIt->leaseID == leaseID) {
			qhPendingChatEmbedAssists.erase(assistIt);
		}
	});
}

void Server::scheduleServerChatEmbedFetch(unsigned int threadID, unsigned int messageID, MumbleProto::ChatScope scope,
										  unsigned int scopeID, unsigned int permissionChannelID,
										  const ::msdb::DBChatMessageEmbed &initialEmbed) {
	if (!qnamNetwork) {
		return;
	}

	const auto persistPreviewImage = [this](const SanitizedChatImage &thumbnail) -> std::optional< unsigned int > {
		return persistChatPreviewAsset(thumbnail.bytes, thumbnail.mime, msdb::ChatAssetKind::Image, thumbnail.width,
									   thumbnail.height);
	};

	const auto finish = [this, threadID, messageID, scope, scopeID,
						 permissionChannelID](::msdb::DBChatMessageEmbed embed) {
		embed.fetchedAt = std::chrono::system_clock::now();
		QMetaObject::invokeMethod(
			this,
			[this, threadID, messageID, scope, scopeID, permissionChannelID, embed]() {
				applyChatEmbedFetchResult(threadID, messageID, scope, scopeID, permissionChannelID, embed);
			},
			Qt::QueuedConnection);
	};

	const auto updateHostCount = [this](const QString &hostKey, int delta) {
		if (hostKey.isEmpty()) {
			return;
		}

		const unsigned int current = qhChatPreviewFetchesByHost.value(hostKey, 0);
		if (delta > 0) {
			qhChatPreviewFetchesByHost.insert(hostKey, current + static_cast< unsigned int >(delta));
		} else {
			const unsigned int decrease = static_cast< unsigned int >(-delta);
			if (current <= decrease) {
				qhChatPreviewFetchesByHost.remove(hostKey);
			} else {
				qhChatPreviewFetchesByHost.insert(hostKey, current - decrease);
			}
		}
	};

	auto embedState      = std::make_shared<::msdb::DBChatMessageEmbed >(initialEmbed);
	auto pageTitle       = std::make_shared< QString >();
	auto pageDescription = std::make_shared< QString >();
	auto pageSiteName    = std::make_shared< QString >();

	auto fetchImage         = std::make_shared< std::function< void(QUrl, unsigned int) > >();
	auto fetchPlayableMedia = std::make_shared< std::function< void(QUrl, unsigned int, QUrl) > >();
	*fetchPlayableMedia = [this, fetchPlayableMedia, fetchImage, embedState, pageTitle, pageDescription, pageSiteName,
						   finish, updateHostCount](QUrl mediaUrl, unsigned int redirectCount,
													QUrl fallbackImageUrl) mutable {
		mediaUrl = normalizedPlayableMediaFetchUrl(mediaUrl);
		const auto fallbackToImage = [&]() -> bool {
			if (fallbackImageUrl.isValid() && isSafePreviewUrl(fallbackImageUrl) && *fetchImage) {
				(*fetchImage)(fallbackImageUrl, 0);
				return true;
			}
			return false;
		};

		if (redirectCount > CHAT_PREVIEW_MAX_REDIRECTS || !isSafePreviewUrl(mediaUrl)) {
			if (fallbackToImage()) {
				return;
			}
			embedState->status    = ::msdb::ChatEmbedStatus::Blocked;
			embedState->errorCode = "blocked_target";
			finish(*embedState);
			return;
		}

		const QString hostKey = mediaUrl.host().trimmed().toLower();
		if (qhChatPreviewFetchesByHost.value(hostKey, 0) >= CHAT_PREVIEW_MAX_CONCURRENT_HOST) {
			if (fallbackToImage()) {
				return;
			}
			embedState->status    = ::msdb::ChatEmbedStatus::Failed;
			embedState->errorCode = "host_busy";
			finish(*embedState);
			return;
		}

		updateHostCount(hostKey, +1);
		QNetworkRequest request(mediaUrl);
		prepareChatPreviewRequest(request);
		request.setRawHeader(QByteArrayLiteral("Accept"),
							 QByteArrayLiteral("video/mp4,video/webm,image/gif,video/*;q=0.9,image/*;q=0.8,*/*;q=0.5"));
		request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
		request.setTransferTimeout(CHAT_PREVIEW_TIMEOUT_MSEC);
		QNetworkReply *reply = qnamNetwork->get(request);
		reply->setReadBufferSize(CHAT_PREVIEW_MAX_PLAYABLE_MEDIA_BYTES + 1);
		connect(reply, &QNetworkReply::metaDataChanged, reply, [reply]() {
			const qint64 contentLength = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
			if (contentLength > CHAT_PREVIEW_MAX_PLAYABLE_MEDIA_BYTES && !reply->isFinished()) {
				setChatPreviewAbortReason(reply, QLatin1String("too_large"));
				reply->abort();
			}
		});
		connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 received, qint64) {
			if (received > CHAT_PREVIEW_MAX_PLAYABLE_MEDIA_BYTES && !reply->isFinished()) {
				setChatPreviewAbortReason(reply, QLatin1String("too_large"));
				reply->abort();
			}
		});
		connect(reply, &QNetworkReply::finished, this,
				[this, reply, hostKey, fetchPlayableMedia, fetchImage, embedState, finish, updateHostCount,
				 pageTitle, pageDescription, pageSiteName, redirectCount, fallbackImageUrl]() mutable {
					updateHostCount(hostKey, -1);
					const QVariant redirectTarget = reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
					const QString contentType =
						reply->header(QNetworkRequest::ContentTypeHeader).toString().section(';', 0, 0).trimmed().toLower();
					const QByteArray bytes = reply->readAll();
					const bool success     = reply->error() == QNetworkReply::NoError;
					const QString abortReason = chatPreviewAbortReason(reply);
					const QUrl sourceUrl   = reply->request().url();
					reply->deleteLater();

					if (redirectTarget.isValid()) {
						(*fetchPlayableMedia)(sourceUrl.resolved(redirectTarget.toUrl()), redirectCount + 1, fallbackImageUrl);
						return;
					}

					const QString mime = playableMediaMimeFromResponse(contentType, sourceUrl);
					if (!success || bytes.isEmpty() || bytes.size() > CHAT_PREVIEW_MAX_PLAYABLE_MEDIA_BYTES
						|| !isDirectPlayableMediaMime(mime)) {
						if (fallbackImageUrl.isValid() && isSafePreviewUrl(fallbackImageUrl)) {
							embedState->title       = u8(*pageTitle);
							embedState->description = u8(*pageDescription);
							embedState->siteName    = u8(*pageSiteName);
							(*fetchImage)(fallbackImageUrl, 0);
							return;
						}
						embedState->status = ::msdb::ChatEmbedStatus::Failed;
						embedState->errorCode =
							abortReason == QLatin1String("too_large") ? "media_too_large" : "media_fetch_failed";
						finish(*embedState);
						return;
					}

					unsigned int width  = 0;
					unsigned int height = 0;
					if (mime == QLatin1String("image/gif")) {
						const QImage decoded = decodeChatImage(bytes);
						if (!decoded.isNull()) {
							width  = static_cast< unsigned int >(decoded.width());
							height = static_cast< unsigned int >(decoded.height());
						}
					}

					const auto assetID = persistChatPreviewAsset(bytes, mime, playableMediaAssetKind(mime), width, height);
					if (!assetID) {
						embedState->status    = ::msdb::ChatEmbedStatus::Failed;
						embedState->errorCode = "media_cache_failed";
						finish(*embedState);
						return;
					}

					const bool isGif = mime == QLatin1String("image/gif");
					embedState->title =
						u8(pageTitle->isEmpty()
							   ? previewTitleForMediaUrl(sourceUrl,
														 isGif ? QObject::tr("Animated GIF") : QObject::tr("Video media"))
							   : *pageTitle);
					embedState->description =
						u8(pageDescription->isEmpty()
							   ? (isGif ? QObject::tr("Animated image preview") : QObject::tr("Video preview"))
							   : *pageDescription);
					embedState->siteName       = u8(pageSiteName->isEmpty() ? sourceUrl.host() : *pageSiteName);
					embedState->previewAssetID = assetID.value();
					embedState->status         = ::msdb::ChatEmbedStatus::Ready;
					embedState->errorCode      = "";
					finish(*embedState);
				});
	};

	*fetchImage = [this, fetchImage, embedState, pageTitle, pageDescription, pageSiteName, persistPreviewImage, finish,
				   updateHostCount](QUrl imageUrl, unsigned int redirectCount) mutable {
		if (redirectCount > CHAT_PREVIEW_MAX_REDIRECTS || !isSafePreviewUrl(imageUrl)) {
			embedState->status    = ::msdb::ChatEmbedStatus::Blocked;
			embedState->errorCode = "blocked_target";
			finish(*embedState);
			return;
		}

		const QString hostKey = imageUrl.host().trimmed().toLower();
		if (qhChatPreviewFetchesByHost.value(hostKey, 0) >= CHAT_PREVIEW_MAX_CONCURRENT_HOST) {
			embedState->status    = ::msdb::ChatEmbedStatus::Failed;
			embedState->errorCode = "host_busy";
			finish(*embedState);
			return;
		}

		updateHostCount(hostKey, +1);
		QNetworkRequest request(imageUrl);
		prepareChatPreviewImageRequest(request);
		request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
		request.setTransferTimeout(CHAT_PREVIEW_TIMEOUT_MSEC);
		QNetworkReply *reply = qnamNetwork->get(request);
		reply->setReadBufferSize(CHAT_PREVIEW_MAX_IMAGE_BYTES);
		connect(
			reply, &QNetworkReply::finished, this,
			[this, reply, hostKey, fetchImage, embedState, pageTitle, pageDescription, pageSiteName,
			 persistPreviewImage, finish, updateHostCount, redirectCount]() mutable {
				updateHostCount(hostKey, -1);
				const QVariant redirectTarget = reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
				const QString contentType =
					reply->header(QNetworkRequest::ContentTypeHeader).toString().section(';', 0, 0).trimmed().toLower();
				const QByteArray bytes = reply->readAll();
				const bool success     = reply->error() == QNetworkReply::NoError;
				const QUrl sourceUrl   = reply->request().url();
				reply->deleteLater();

				if (redirectTarget.isValid()) {
					(*fetchImage)(sourceUrl.resolved(redirectTarget.toUrl()), redirectCount + 1);
					return;
				}

				embedState->title       = u8(*pageTitle);
				embedState->description = u8(*pageDescription);
				embedState->siteName    = u8(*pageSiteName);

				if (!success || bytes.size() > CHAT_PREVIEW_MAX_IMAGE_BYTES
					|| (!contentType.startsWith(QLatin1String("image/")) && !contentType.isEmpty())) {
					embedState->status    = ::msdb::ChatEmbedStatus::Ready;
					embedState->errorCode = success ? "" : "image_fetch_failed";
					finish(*embedState);
					return;
				}

				if (const auto thumbnail = sanitizeChatImageBytes(bytes, true); thumbnail) {
					embedState->previewAssetID = persistPreviewImage(*thumbnail);
				}

				embedState->status    = ::msdb::ChatEmbedStatus::Ready;
				embedState->errorCode = "";
				finish(*embedState);
			});
	};

	auto fetchPage = std::make_shared< std::function< void(QUrl, unsigned int) > >();
	*fetchPage     = [this, fetchPage, fetchImage, fetchPlayableMedia, embedState, pageTitle, pageDescription,
                  pageSiteName, finish, updateHostCount](QUrl pageUrl, unsigned int redirectCount) mutable {
        if (redirectCount > CHAT_PREVIEW_MAX_REDIRECTS || !isSafePreviewUrl(pageUrl)) {
            embedState->status    = ::msdb::ChatEmbedStatus::Blocked;
            embedState->errorCode = "blocked_target";
            finish(*embedState);
            return;
        }

		if (isDirectPlayableMediaUrl(pageUrl)) {
			(*fetchPlayableMedia)(pageUrl, redirectCount, QUrl());
			return;
		}

        const QString hostKey = pageUrl.host().trimmed().toLower();
        if (qhChatPreviewFetchesByHost.value(hostKey, 0) >= CHAT_PREVIEW_MAX_CONCURRENT_HOST) {
            embedState->status    = ::msdb::ChatEmbedStatus::Failed;
            embedState->errorCode = "host_busy";
            finish(*embedState);
            return;
        }

        updateHostCount(hostKey, +1);
        QNetworkRequest request(pageUrl);
        if (isInstagramPreviewUrl(pageUrl)) {
            prepareInstagramChatPreviewRequest(request);
        } else {
            prepareChatPreviewRequest(request);
        }
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        request.setTransferTimeout(CHAT_PREVIEW_TIMEOUT_MSEC);
        QNetworkReply *reply = qnamNetwork->get(request);
        reply->setReadBufferSize(CHAT_PREVIEW_MAX_PAGE_BYTES);
        connect(
            reply, &QNetworkReply::finished, this,
            [this, reply, fetchPage, fetchImage, fetchPlayableMedia, embedState, pageTitle, pageDescription,
             pageSiteName, finish, updateHostCount, hostKey, redirectCount]() mutable {
                updateHostCount(hostKey, -1);
                const QVariant redirectTarget = reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
                const QString contentType =
                    reply->header(QNetworkRequest::ContentTypeHeader).toString().section(';', 0, 0).trimmed().toLower();
                const QByteArray bytes = reply->readAll();
                const bool success     = reply->error() == QNetworkReply::NoError;
                const QUrl sourceUrl   = reply->request().url();
                reply->deleteLater();

                if (redirectTarget.isValid()) {
                    (*fetchPage)(sourceUrl.resolved(redirectTarget.toUrl()), redirectCount + 1);
                    return;
                }

                if (!success || bytes.size() > CHAT_PREVIEW_MAX_PAGE_BYTES) {
                    if (!success) {
                        embedState->status    = ::msdb::ChatEmbedStatus::Failed;
                        embedState->errorCode = "fetch_failed";
                        finish(*embedState);
                        return;
                    }
                }

				if (isDirectPlayableMediaMime(playableMediaMimeFromResponse(contentType, sourceUrl))) {
					(*fetchPlayableMedia)(sourceUrl, 0, QUrl());
					return;
				}

                if (contentType.startsWith(QLatin1String("image/")) || isDirectImageUrl(sourceUrl)) {
                    if (bytes.size() > CHAT_PREVIEW_MAX_PAGE_BYTES) {
                        embedState->status    = ::msdb::ChatEmbedStatus::Failed;
                        embedState->errorCode = "fetch_failed";
                        finish(*embedState);
                        return;
                    }
                    *pageTitle       = QFileInfo(sourceUrl.path()).fileName();
                    *pageDescription = QObject::tr("Direct image preview");
                    *pageSiteName    = sourceUrl.host();
                    (*fetchImage)(sourceUrl, 0);
                    return;
                }

                if (!previewContentTypeLooksHtml(contentType)) {
                    embedState->status    = ::msdb::ChatEmbedStatus::Failed;
                    embedState->errorCode = "unsupported_content_type";
                    finish(*embedState);
                    return;
                }

                // Large SPA responses often keep the useful preview tags near the start of <head>.
                // Parse the prefix we have instead of discarding the preview solely due to size.
                const QByteArray htmlBytes =
                    bytes.size() > CHAT_PREVIEW_MAX_PAGE_BYTES ? bytes.left(CHAT_PREVIEW_MAX_PAGE_BYTES) : bytes;
                const QString html                       = QString::fromUtf8(htmlBytes);
                const QHash< QString, QString > metaTags = extractMetaTags(html);
                *pageTitle                               = metaTags.value(QLatin1String("og:title"),
                                            metaTags.value(QLatin1String("twitter:title"), extractHtmlTitle(html)));
                *pageDescription                         = metaTags.value(
                    QLatin1String("og:description"),
                    metaTags.value(QLatin1String("twitter:description"), metaTags.value(QLatin1String("description"))));
                *pageSiteName = metaTags.value(QLatin1String("og:site_name"), sourceUrl.host());

                embedState->title       = u8(pageTitle->isEmpty() ? sourceUrl.host() : *pageTitle);
                embedState->description = u8(*pageDescription);
                embedState->siteName    = u8(pageSiteName->isEmpty() ? sourceUrl.host() : *pageSiteName);

                const QString imageUrlString = previewImageMetaTag(metaTags);
				const QUrl fallbackImageUrl =
					imageUrlString.isEmpty() ? QUrl() : sourceUrl.resolved(QUrl(imageUrlString));
				const QString mediaUrlString = previewPlayableMediaMetaTag(metaTags);
				if (!mediaUrlString.isEmpty()) {
					(*fetchPlayableMedia)(sourceUrl.resolved(QUrl(mediaUrlString)), 0, fallbackImageUrl);
					return;
				}

                if (imageUrlString.isEmpty()) {
                    embedState->status    = ::msdb::ChatEmbedStatus::Ready;
                    embedState->errorCode = "";
                    finish(*embedState);
                    return;
                }

                (*fetchImage)(sourceUrl.resolved(QUrl(imageUrlString)), 0);
            });
		};

	const auto finishYahooQuoteFallback = [embedState, finish](const QString &symbol) {
		const QString normalizedSymbol = Mumble::Finance::normalizeTickerSymbol(symbol);
		embedState->title =
			u8(normalizedSymbol.isEmpty() ? QObject::tr("Yahoo Finance quote")
										  : QObject::tr("%1 on Yahoo Finance").arg(normalizedSymbol));
		embedState->description = u8(QObject::tr("Open on Yahoo Finance for the latest quote."));
		embedState->siteName    = "Yahoo Finance";
		embedState->status      = ::msdb::ChatEmbedStatus::Ready;
		embedState->errorCode   = "";
		finish(*embedState);
	};

	auto fetchYahooQuote = std::make_shared< std::function< void(const QString &, int) > >();
	*fetchYahooQuote = [this, fetchYahooQuote, embedState, finish, updateHostCount, finishYahooQuoteFallback](
							const QString &originalSymbol, int symbolIndex) {
		const QList< QString > symbols = Mumble::Finance::yahooFinanceSymbolCandidates(originalSymbol);
		if (symbolIndex < 0 || symbolIndex >= symbols.size()) {
			finishYahooQuoteFallback(originalSymbol);
			return;
		}

		const QString symbol = symbols.at(symbolIndex);
		const QUrl chartUrl  = Mumble::Finance::yahooFinanceChartUrl(symbol);
		if (!chartUrl.isValid() || !isSafePreviewUrl(chartUrl)) {
			(*fetchYahooQuote)(originalSymbol, symbolIndex + 1);
			return;
		}

		const QString hostKey = chartUrl.host().trimmed().toLower();
		if (qhChatPreviewFetchesByHost.value(hostKey, 0) >= CHAT_PREVIEW_MAX_CONCURRENT_HOST) {
			finishYahooQuoteFallback(originalSymbol);
			return;
		}

		updateHostCount(hostKey, +1);
		QNetworkRequest request(chartUrl);
		prepareChatPreviewRequest(request);
		request.setRawHeader(QByteArrayLiteral("Accept"), QByteArrayLiteral("application/json,*/*;q=0.5"));
		request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
		request.setTransferTimeout(CHAT_PREVIEW_TIMEOUT_MSEC);
		QNetworkReply *reply = qnamNetwork->get(request);
		reply->setReadBufferSize(CHAT_PREVIEW_MAX_PAGE_BYTES);
		connect(reply, &QNetworkReply::finished, this,
				[reply, originalSymbol, symbolIndex, fetchYahooQuote, embedState, finish, updateHostCount,
				 hostKey]() mutable {
					updateHostCount(hostKey, -1);
					const QVariant redirectTarget = reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
					const QByteArray bytes         = reply->readAll();
					const bool success             = reply->error() == QNetworkReply::NoError;
					reply->deleteLater();

					QString parseError;
					const std::optional< Mumble::Finance::YahooChartQuote > quote =
						success && !redirectTarget.isValid()
							? Mumble::Finance::parseYahooChartQuote(bytes, &parseError)
							: std::nullopt;
					if (!quote) {
						(*fetchYahooQuote)(originalSymbol, symbolIndex + 1);
						return;
					}

					const QString quoteSymbol = Mumble::Finance::normalizeTickerSymbol(
						quote->symbol.trimmed().isEmpty() ? originalSymbol : quote->symbol);
					const QUrl canonicalUrl = Mumble::Finance::yahooFinanceQuoteUrl(quoteSymbol);
					if (canonicalUrl.isValid()) {
						embedState->canonicalUrl = u8(canonicalUrl.toString(QUrl::FullyEncoded));
					}

					embedState->title       = u8(Mumble::Finance::yahooFinanceQuoteTitle(*quote));
					embedState->description = u8(Mumble::Finance::yahooFinanceQuoteDescription(*quote));
					embedState->siteName    = "Yahoo Finance";
					embedState->status      = ::msdb::ChatEmbedStatus::Ready;
					embedState->errorCode   = "";
					finish(*embedState);
				});
	};

	const QUrl initialUrl(QString::fromStdString(initialEmbed.canonicalUrl));
	if (redditVideoIdFromUrl(initialUrl)) {
		embedState->title       = u8(QObject::tr("Reddit video"));
		embedState->description = u8(QObject::tr("Video preview"));
		embedState->siteName    = "Reddit";
		embedState->status      = ::msdb::ChatEmbedStatus::Ready;
		embedState->errorCode   = "";
		finish(*embedState);
		return;
	}

	QString yahooSymbol;
	if (Mumble::Finance::symbolFromYahooFinanceQuoteUrl(initialUrl, &yahooSymbol)) {
		(*fetchYahooQuote)(yahooSymbol, 0);
		return;
	}

	(*fetchPage)(initialUrl, 0);
}

/// Checks whether the given channel has restrictions affecting the ENTER privilege
///
/// @param c A pointer to the Channel that should be checked
/// @return Whether the provided channel has an ACL denying ENTER
bool isChannelEnterRestricted(Channel *c) {
	// A channel is enter restricted if there's an ACL denying enter privileges
	for (ChanACL *acl : c->qlACL) {
		if (acl->pDeny & ChanACL::Enter) {
			return true;
		}
	}

	return false;
}

void Server::msgAuthenticate(ServerUser *uSource, MumbleProto::Authenticate &msg) {
	ZoneScoped;

	if (uSource->sState == ServerUser::Authenticated && (msg.tokens_size() > 0 || !uSource->qslAccessTokens.empty())) {
		// Process a change in access tokens for already authenticated users
		QStringList qsl;

		for (int i = 0; i < msg.tokens_size(); ++i) {
			qsl << u8(msg.tokens(i));
		}

		{
			QMutexLocker qml(&qmCache);
			uSource->qslAccessTokens = qsl;
		}

		clearACLCache(uSource);

		// Send back updated enter states of all channels
		MumbleProto::ChannelState mpcs;

		for (Channel *chan : qhChannels) {
			mpcs.set_channel_id(static_cast< unsigned int >(chan->iId));
			mpcs.set_can_enter(hasPermission(uSource, chan, ChanACL::Enter));
			// As no ACLs have changed, we don't need to update the is_access_restricted message field

			sendMessage(uSource, mpcs);
		}

		sendTextChannelSync(uSource);
	}
	MSG_SETUP(ServerUser::Connected);

	// As the first thing, assign a session ID to this client. Given that the client initiated
	// the authentication procedure we can be sure that this is not just a random TCP connection.
	// Thus it is about time we assign the ID to this client in order to be able to reference it
	// in the following.
	{
		QWriteLocker wl(&qrwlVoiceThread);
		uSource->uiSession = qqIds.dequeue();
		qhUsers.insert(uSource->uiSession, uSource);
		qhHostUsers[uSource->haAddress].insert(uSource);
	}

	Channel *root = qhChannels.value(0);
	Channel *c;

	uSource->qsName = u8(msg.username()).trimmed();

	bool ok     = false;
	bool nameok = validateUserName(uSource->qsName);
	QString pw  = u8(msg.password());

	// Fetch ID and stored username.
	// This function needs to support the fact that sessions may go away.
	int id = authenticate(uSource->qsName, pw, static_cast< int >(uSource->uiSession), uSource->qslEmail,
						  uSource->qsHash, uSource->bVerified, uSource->peerCertificateChain());

	uSource->iId = id >= 0 ? id : -1;

	QString reason;
	MumbleProto::Reject_RejectType rtType = MumbleProto::Reject_RejectType_None;

	if (id == -2 && !nameok) {
		reason = "Invalid username";
		rtType = MumbleProto::Reject_RejectType_InvalidUsername;
	} else if (id == -1) {
		reason = "Wrong certificate or password for existing user";
		rtType = MumbleProto::Reject_RejectType_WrongUserPW;
	} else if (id == -2 && !qsPassword.isEmpty() && qsPassword != pw) {
		reason = "Invalid server password";
		rtType = MumbleProto::Reject_RejectType_WrongServerPW;
	} else if (id == -3) {
		reason = "Your account information can not be verified currently. Please try again later";
		rtType = MumbleProto::Reject_RejectType_AuthenticatorFail;
	} else {
		ok = true;
	}

	ServerUser *uOld = nullptr;
	for (ServerUser *u : qhUsers) {
		if (u == uSource)
			continue;
		if (((u->iId >= 0) && (u->iId == uSource->iId)) || (u->qsName.toLower() == uSource->qsName.toLower())) {
			uOld = u;
			break;
		}
	}

	// Allow reuse of name from same IP
	if (ok && uOld && (uSource->iId == -1)) {
		if ((uOld->peerAddress() != uSource->peerAddress())
			&& (uSource->qsHash.isEmpty() || (uSource->qsHash != uOld->qsHash))) {
			reason = "Username already in use";
			rtType = MumbleProto::Reject_RejectType_UsernameInUse;
			ok     = false;
		}
	}

	if ((id != 0) && (static_cast< unsigned int >(qhUsers.count()) > iMaxUsers)) {
		reason = QString::fromLatin1("Server is full (max %1 users)").arg(iMaxUsers);
		rtType = MumbleProto::Reject_RejectType_ServerFull;
		ok     = false;
	}

	if ((id != 0) && (uSource->qsHash.isEmpty() && bCertRequired)) {
		reason = QString::fromLatin1("A certificate is required to connect to this server");
		rtType = MumbleProto::Reject_RejectType_NoCertificate;
		ok     = false;
	}

	if (ok) {
		if (msg.tokens_size() > 0) {
			// Set the access tokens for the newly connected user
			QStringList qsl;

			for (int i = 0; i < msg.tokens_size(); ++i) {
				qsl << u8(msg.tokens(i));
			}

			{
				QMutexLocker qml(&qmCache);
				uSource->qslAccessTokens = qsl;
			}
		}

		// Clear cache as the "auth" ACL depends on the user id
		// and any cached ACL won't have taken that into account yet
		// Also, if we set access tokens above, we have to reset
		// the ACL cache anyway.
		clearACLCache(uSource);
	}

	Channel *lc = nullptr;
	const bool reconnectToLastChannel = msg.reconnect_to_last_channel();
	if (uSource->iId >= 0 && Meta::mp->bRememberChan && reconnectToLastChannel) {
		unsigned int lastChannelID = m_dbWrapper.getLastChannelID(iServerNum, static_cast< unsigned int >(uSource->iId),
																  static_cast< unsigned int >(iRememberChanDuration),
																  tUptime.elapsed< std::chrono::seconds >());
		lc                         = qhChannels.value(lastChannelID);
	}

	if (!lc || !hasPermission(uSource, lc, ChanACL::Enter) || isChannelFull(lc, uSource)) {
		lc = qhChannels.value(iDefaultChan);
		if (!lc || !hasPermission(uSource, lc, ChanACL::Enter) || isChannelFull(lc, uSource)) {
			lc = root;
			if (isChannelFull(lc, uSource)) {
				reason = QString::fromLatin1("Server channels are full");
				rtType = MumbleProto::Reject_RejectType_ServerFull;
				ok     = false;
			}
		}
	}

	if (!ok) {
		log(uSource, QString("Rejected connection from %1: %2")
						 .arg(addressToString(uSource->peerAddress(), uSource->peerPort()), reason));
		MumbleProto::Reject mpr;
		mpr.set_reason(u8(reason));
		mpr.set_type(rtType);
		sendMessage(uSource, mpr);
		uSource->disconnectSocket();
		return;
	}

	startThread();

	// Kick ghost
	if (uOld) {
		log(uSource, "Disconnecting ghost");
		MumbleProto::UserRemove mpur;
		mpur.set_session(uOld->uiSession);
		mpur.set_reason("You connected to the server from another device");
		sendMessage(uOld, mpur);
		uOld->forceFlush();
		uOld->disconnectSocket(true);
	}

	// Setup UDP encryption
	{
		QMutexLocker l(&uSource->qmCrypt);

		uSource->csCrypt->genKey();

		MumbleProto::CryptSetup mpcrypt;
		mpcrypt.set_key(uSource->csCrypt->getRawKey());
		mpcrypt.set_server_nonce(uSource->csCrypt->getEncryptIV());
		mpcrypt.set_client_nonce(uSource->csCrypt->getDecryptIV());
		sendMessage(uSource, mpcrypt);
	}

	bool fake_celt_support = false;
	if (msg.celt_versions_size() > 0) {
		for (int i = 0; i < msg.celt_versions_size(); ++i)
			uSource->qlCodecs.append(msg.celt_versions(i));
	} else {
		uSource->qlCodecs.append(static_cast< qint32 >(0x8000000b));
		fake_celt_support = true;
	}
	uSource->bOpus = msg.opus();
	recheckCodecVersions(uSource);

	MumbleProto::CodecVersion mpcv;
	mpcv.set_alpha(iCodecAlpha);
	mpcv.set_beta(iCodecBeta);
	mpcv.set_prefer_alpha(bPreferAlpha);
	mpcv.set_opus(bOpus);
	sendMessage(uSource, mpcv);

	if (!bOpus && uSource->bOpus && fake_celt_support) {
		sendTextMessage(
			nullptr, uSource, false,
			QLatin1String("<strong>WARNING:</strong> Your client doesn't support the CELT codec, you won't be able to "
						  "talk to or hear most clients. Please make sure your client was built with CELT support."));
	}

	// Transmit channel tree
	QQueue< Channel * > q;
	QSet< Channel * > chans;
	q << root;
	MumbleProto::ChannelState mpcs;

	while (!q.isEmpty()) {
		c = q.dequeue();
		chans.insert(c);

		mpcs.Clear();

		mpcs.set_channel_id(c->iId);
		if (c->cParent)
			mpcs.set_parent(c->cParent->iId);
		if (c->iId == 0)
			mpcs.set_name(u8(qsRegName.isEmpty() ? QLatin1String("Root") : qsRegName));
		else
			mpcs.set_name(u8(c->qsName));

		mpcs.set_position(c->iPosition);

		if ((uSource->m_version >= Version::fromComponents(1, 2, 2)) && !c->qbaDescHash.isEmpty())
			mpcs.set_description_hash(blob(c->qbaDescHash));
		else if (!c->qsDesc.isEmpty())
			mpcs.set_description(u8(c->qsDesc));

		mpcs.set_max_users(c->uiMaxUsers);

		// Include info about enter restrictions of this channel
		mpcs.set_is_enter_restricted(isChannelEnterRestricted(c));
		mpcs.set_can_enter(hasPermission(uSource, c, ChanACL::Enter));

		sendMessage(uSource, mpcs);

		for (Channel *chan : c->qlChannels) {
			q.enqueue(chan);
		}
	}

	// Transmit links
	for (Channel *chan : chans) {
		if (chan->qhLinks.count() > 0) {
			mpcs.Clear();
			mpcs.set_channel_id(chan->iId);

			for (Channel *l : chan->qhLinks.keys()) {
				mpcs.add_links(l->iId);
			}
			sendMessage(uSource, mpcs);
		}
	}

	if (uSource->iId >= 0) {
		m_dbWrapper.loadChannelListenersOf(iServerNum, *uSource, m_channelListenerManager);
	}

	// Transmit user profile
	MumbleProto::UserState mpus;

	userEnterChannel(uSource, lc, mpus);

	{
		QWriteLocker wl(&qrwlVoiceThread);
		uSource->sState = ServerUser::Authenticated;
	}

	mpus.set_session(uSource->uiSession);
	mpus.set_name(u8(uSource->qsName));
	if (uSource->iId >= 0) {
		mpus.set_user_id(static_cast< unsigned int >(uSource->iId));

		loadTexture(*uSource);

		if (!uSource->qbaTextureHash.isEmpty()) {
			mpus.set_texture_hash(blob(uSource->qbaTextureHash));
		} else if (!uSource->qbaTexture.isEmpty()) {
			mpus.set_texture(blob(uSource->qbaTexture));
		}

		loadComment(*uSource);

		if (!uSource->qbaCommentHash.isEmpty()) {
			mpus.set_comment_hash(blob(uSource->qbaCommentHash));
		} else if (!uSource->qsComment.isEmpty()) {
			mpus.set_comment(u8(uSource->qsComment));
		}
	}
	if (!uSource->qsHash.isEmpty())
		mpus.set_hash(u8(uSource->qsHash));

	mpus.set_channel_id(uSource->cChannel->iId);

	sendAll(mpus, Version::fromComponents(1, 2, 2), Version::CompareMode::AtLeast);

	if ((uSource->qbaTexture.length() >= 4)
		&& (qFromBigEndian< unsigned int >(reinterpret_cast< const unsigned char * >(uSource->qbaTexture.constData()))
			== 600 * 60 * 4))
		mpus.set_texture(blob(uSource->qbaTexture));
	if (!uSource->qsComment.isEmpty())
		mpus.set_comment(u8(uSource->qsComment));
	sendAll(mpus, Version::fromComponents(1, 2, 2), Version::CompareMode::LessThan);

	// Transmit other users profiles
	for (ServerUser *u : qhUsers) {
		if (u->sState != ServerUser::Authenticated)
			continue;

		if (u == uSource)
			continue;

		mpus.Clear();
		mpus.set_session(u->uiSession);
		mpus.set_name(u8(u->qsName));
		if (u->iId >= 0)
			mpus.set_user_id(static_cast< unsigned int >(u->iId));
		if (uSource->m_version >= Version::fromComponents(1, 2, 2)) {
			if (!u->qbaTextureHash.isEmpty())
				mpus.set_texture_hash(blob(u->qbaTextureHash));
			else if (!u->qbaTexture.isEmpty())
				mpus.set_texture(blob(u->qbaTexture));
		} else if ((uSource->qbaTexture.length() >= 4)
				   && (qFromBigEndian< unsigned int >(
						   reinterpret_cast< const unsigned char * >(uSource->qbaTexture.constData()))
					   == 600 * 60 * 4)) {
			mpus.set_texture(blob(u->qbaTexture));
		}
		if (u->cChannel->iId != 0)
			mpus.set_channel_id(u->cChannel->iId);
		if (u->bDeaf)
			mpus.set_deaf(true);
		else if (u->bMute)
			mpus.set_mute(true);
		if (u->bSuppress)
			mpus.set_suppress(true);
		if (u->bPrioritySpeaker)
			mpus.set_priority_speaker(true);
		if (u->bRecording)
			mpus.set_recording(true);
		if (u->bSelfDeaf)
			mpus.set_self_deaf(true);
		else if (u->bSelfMute)
			mpus.set_self_mute(true);
		if ((uSource->m_version >= Version::fromComponents(1, 2, 2)) && !u->qbaCommentHash.isEmpty())
			mpus.set_comment_hash(blob(u->qbaCommentHash));
		else if (!u->qsComment.isEmpty())
			mpus.set_comment(u8(u->qsComment));
		if (!u->qsHash.isEmpty())
			mpus.set_hash(u8(u->qsHash));


		for (unsigned int channelID : m_channelListenerManager.getListenedChannelsForUser(u->uiSession)) {
			mpus.add_listening_channel_add(channelID);

			if (broadcastListenerVolumeAdjustments) {
				VolumeAdjustment volume = m_channelListenerManager.getListenerVolumeAdjustment(u->uiSession, channelID);
				MumbleProto::UserState::VolumeAdjustment *adjustment = mpus.add_listening_volume_adjustment();
				adjustment->set_listening_channel(channelID);
				adjustment->set_volume_adjustment(volume.factor);
			}
		}

		sendMessage(uSource, mpus);
	}

	// Send synchronisation packet
	MumbleProto::ServerSync mpss;
	mpss.set_session(uSource->uiSession);
	if (!qsWelcomeText.isEmpty())
		mpss.set_welcome_text(u8(qsWelcomeText));
	mpss.set_max_bandwidth(static_cast< unsigned int >(iMaxBandwidth));

	if (uSource->iId == 0) {
		mpss.set_permissions(ChanACL::All);
	} else {
		QMutexLocker qml(&qmCache);
		ChanACL::hasPermission(uSource, root, ChanACL::Enter, &acCache);
		mpss.set_permissions(acCache.value(uSource)->value(root));
	}

	sendMessage(uSource, mpss);

	// Transmit user's listeners - this has to be done AFTER the server-sync message has been sent to uSource as the
	// client may require its own session ID for processing the listeners properly.
	mpus.Clear();
	mpus.set_session(uSource->uiSession);
	for (unsigned int channelID : m_channelListenerManager.getListenedChannelsForUser(uSource->uiSession)) {
		mpus.add_listening_channel_add(channelID);
	}

	// If we are not intending to broadcast the volume adjustments to everyone, we have to send the message to all but
	// uSource without the volume adjustments. Then append the adjustments, but only send them to uSource. If we are in
	// fact broadcasting, just append the adjustments and send to everyone.
	if (!broadcastListenerVolumeAdjustments && mpus.listening_channel_add_size() > 0) {
		sendExcept(uSource, mpus, Version::fromComponents(1, 2, 2), Version::CompareMode::AtLeast);
	}

	std::unordered_map< unsigned int, VolumeAdjustment > volumeAdjustments =
		m_channelListenerManager.getAllListenerVolumeAdjustments(uSource->uiSession);
	for (auto it = volumeAdjustments.begin(); it != volumeAdjustments.end(); ++it) {
		MumbleProto::UserState::VolumeAdjustment *adjustment = mpus.add_listening_volume_adjustment();
		adjustment->set_listening_channel(it->first);
		adjustment->set_volume_adjustment(it->second.factor);
	}

	if (mpus.listening_channel_add_size() > 0 || mpus.listening_volume_adjustment_size() > 0) {
		if (!broadcastListenerVolumeAdjustments) {
			if (uSource->m_version >= Version::fromComponents(1, 2, 2)) {
				sendMessage(uSource, mpus);
			}
		} else {
			sendAll(mpus, Version::fromComponents(1, 2, 2), Version::CompareMode::AtLeast);
		}
	}

	MumbleProto::ServerConfig mpsc;
	mpsc.set_allow_html(bAllowHTML);
	mpsc.set_message_length(static_cast< unsigned int >(iMaxTextMessageLength));
	mpsc.set_image_message_length(static_cast< unsigned int >(iMaxImageMessageLength));
	mpsc.set_max_users(static_cast< unsigned int >(iMaxUsers));
	mpsc.set_recording_allowed(allowRecording);
	mpsc.set_persistent_global_chat_enabled(bPersistentGlobalChatEnabled);
	Mumble::ChatFeatures::addSupportedFeatures(mpsc);
	Mumble::ForkFeatures::addSupportedFeatures(mpsc);
	mpsc.set_screen_share_enabled(bScreenShareEnabled);
	mpsc.set_screen_share_recording_enabled(bScreenShareRecordingEnabled);
	mpsc.set_screen_share_helper_required(bScreenShareHelperRequired);
	for (const int codec : qlPreferredScreenShareCodecs) {
		mpsc.add_preferred_screen_share_codecs(static_cast< MumbleProto::ScreenShareCodec >(codec));
	}
	mpsc.set_screen_share_max_width(uiScreenShareMaxWidth);
	mpsc.set_screen_share_max_height(uiScreenShareMaxHeight);
	mpsc.set_screen_share_max_fps(uiScreenShareMaxFps);
	if (!qsScreenShareRelayUrl.isEmpty()) {
		mpsc.set_screen_share_relay_url(u8(qsScreenShareRelayUrl));
	}
	mpsc.set_stonks_enabled(bStonksEnabled);
	const std::optional< unsigned int > stonksTextChannelID =
		resolvedStonksTextChannelID(uiStonksTextChannelID, m_dbWrapper.getTextChannels(iServerNum));
	if (stonksTextChannelID) {
		mpsc.set_stonks_text_channel_id(*stonksTextChannelID);
	}
	mpsc.set_stonks_social_announcements_enabled(bStonksSocialAnnouncementsEnabled);
	mpsc.set_stonks_auto_valuation_enabled(bStonksAutoValuationEnabled);
	mpsc.set_stonks_valuation_interval_minutes(uiStonksValuationIntervalMinutes);
	mpsc.set_stonks_valuation_history_days(uiStonksValuationHistoryDays);
	mpsc.set_feedback_enabled(feedbackGitHubConfigured());
	mpsc.set_feedback_max_log_bytes(uiFeedbackMaxLogBytes);
	mpsc.set_feedback_max_body_bytes(uiFeedbackMaxBodyBytes);
	mpsc.set_server_display_name(u8(qsServerDisplayName));
	mpsc.set_server_monogram(u8(qsServerMonogram));
	mpsc.set_server_image(blob(qbaServerImage));
	mpsc.set_chat_asset_max_bytes(uiChatAssetMaxBytes);
	mpsc.set_chat_attachment_limit(uiChatAttachmentLimit);
	sendMessage(uSource, mpsc);
	// The client must learn the feature contract before receiving the first
	// permission-gated backlog replacement.
	syncServerLogStateForUser(uSource, true);
	syncScreenShareStateForUser(uSource);
	syncWatchTogetherStateForUser(uSource);

	sendTextChannelSync(uSource);

	MumbleProto::SuggestConfig mpsug;
	if (m_suggestVersion != Version::UNKNOWN) {
		MumbleProto::setSuggestedVersion(mpsug, m_suggestVersion);
	}
	if (m_suggestPositional)
		mpsug.set_positional(m_suggestPositional.value());
	if (m_suggestPushToTalk)
		mpsug.set_push_to_talk(m_suggestPushToTalk.value());
#if GOOGLE_PROTOBUF_VERSION >= 3004000
	if (mpsug.ByteSizeLong() > 0) {
#else
	// ByteSize() has been deprecated as of protobuf v3.4
	if (mpsug.ByteSize() > 0) {
#endif
		sendMessage(uSource, mpsug);
	}

	if (uSource->m_version < Version::fromComponents(1, 4, 0) && Meta::mp->iMaxListenersPerChannel != 0
		&& Meta::mp->iMaxListenerProxiesPerUser != 0) {
		// The server has the ChannelListener feature enabled but the client that connects doesn't have version 1.4.0 or
		// newer meaning that this client doesn't know what ChannelListeners are. Thus we'll send that user a
		// text-message informing about this.
		MumbleProto::TextMessage mptm;

		if (Meta::mp->bAllowHTML) {
			mptm.set_message("<b>[WARNING]</b>: This server has the <b>ChannelListener</b> feature enabled but your "
							 "client version does not support it. "
							 "This means that users <b>might be listening to what you are saying in your channel "
							 "without you noticing!</b> "
							 "You can solve this issue by upgrading to Mumble 1.4.0 or newer.");
		} else {
			mptm.set_message(
				"[WARNING]: This server has the ChannelListener feature enabled but your client version does not "
				"support it. "
				"This means that users might be listening to what you are saying in your channel without you noticing! "
				"You can solve this issue by upgrading to Mumble 1.4.0 or newer.");
		}

		sendMessage(uSource, mptm);
	}

	switch (msg.client_type()) {
		case static_cast< int >(ClientType::BOT):
			uSource->m_clientType = ClientType::BOT;
			m_botCount++;
			break;
		case static_cast< int >(ClientType::REGULAR):
			// No-op (also applies to unknown values of msg.client_type())
			// (The default client type is regular anyway, so we don't need to change anything here)
			break;
	}

	log(uSource, "Authenticated");

	emit userConnected(uSource);
}

void Server::msgBanList(ServerUser *uSource, MumbleProto::BanList &msg) {
	ZoneScoped;

	MSG_SETUP(ServerUser::Authenticated);

	if (!hasPermission(uSource, qhChannels.value(0), ChanACL::Ban)) {
		PERM_DENIED(uSource, qhChannels.value(0), ChanACL::Ban);
		return;
	}

	std::set< Ban > previousBans;

	if (msg.query()) {
		msg.clear_query();
		msg.clear_bans();
		for (const Ban &b : m_bans) {
			MumbleProto::BanList_BanEntry *be = msg.add_bans();
			be->set_address(b.haAddress.toStdString());
			be->set_mask(static_cast< unsigned int >(b.iMask));
			be->set_name(u8(b.qsUsername));
			be->set_hash(u8(b.qsHash));
			be->set_reason(u8(b.qsReason));
			be->set_start(u8(b.qdtStart.toString(Qt::ISODate)));
			be->set_duration(b.iDuration);
		}
		sendMessage(uSource, msg);
	} else {
		previousBans = std::set< Ban >(m_bans.begin(), m_bans.end());
		std::set< QString > uniqueBans;

		m_bans.clear();
		for (int i = 0; i < msg.bans_size(); ++i) {
			const MumbleProto::BanList_BanEntry &be = msg.bans(i);

			Ban b;
			b.haAddress  = be.address();
			b.iMask      = static_cast< int >(be.mask());
			b.qsUsername = u8(be.name());
			b.qsHash     = u8(be.hash());
			b.qsReason   = u8(be.reason());
			if (be.has_start()) {
				b.qdtStart = QDateTime::fromString(u8(be.start()), Qt::ISODate);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
				b.qdtStart.setTimeZone(QTimeZone::utc());
#else
				b.qdtStart.setTimeSpec(Qt::UTC);
#endif
			} else {
				b.qdtStart = QDateTime::currentDateTime().toUTC();
			}

			b.iDuration = be.duration();

			QString repr = b.toKey();

			// server-side de-duplication
			if (uniqueBans.contains(repr)) {
				continue;
			}

			if (b.isValid()) {
				m_bans.push_back(std::move(b));
				uniqueBans.insert(repr);
			}
		}
		// m_bans needs to be sorted in order for it to be used in the set_difference functions below
		std::sort(m_bans.begin(), m_bans.end());

		std::vector< Ban > removed, added;
		std::set_difference(previousBans.begin(), previousBans.end(), m_bans.begin(), m_bans.end(),
							std::back_inserter(removed));
		std::set_difference(m_bans.begin(), m_bans.end(), previousBans.begin(), previousBans.end(),
							std::back_inserter(added));

		for (const Ban &b : removed) {
			log(uSource, QString("Removed ban: %1").arg(b.toString()));
		}

		for (const Ban &b : added) {
			log(uSource, QString("New ban: %1").arg(b.toString()));
		}

		m_dbWrapper.saveBans(iServerNum, m_bans);
		log(uSource, "Updated banlist");
	}
}

void Server::msgReject(ServerUser *, MumbleProto::Reject &) {
}

void Server::msgServerSync(ServerUser *, MumbleProto::ServerSync &) {
}

void Server::msgPermissionDenied(ServerUser *, MumbleProto::PermissionDenied &) {
}

void Server::msgUDPTunnel(ServerUser *, MumbleProto::UDPTunnel &) {
	// This code should be unreachable
	assert(false);
	qWarning("Messages: Reached theoretically unreachable function msgUDPTunnel");
}

void Server::msgUserState(ServerUser *uSource, MumbleProto::UserState &msg) {
	ZoneScoped;

	MSG_SETUP(ServerUser::Authenticated);
	VICTIM_SETUP;

	Channel *root = qhChannels.value(0);

	/*
		First check all permissions involved
	*/
	if ((pDstServerUser->iId == 0) && (uSource->iId != 0)) {
		// Don't allow any action on the SuperUser except initiated directly by the SuperUser himself/herself
		PERM_DENIED_TYPE(SuperUser);
		return;
	}

	msg.set_session(pDstServerUser->uiSession);
	msg.set_actor(uSource->uiSession);

	if (msg.has_name()) {
		PERM_DENIED_TYPE(UserName);
		return;
	}

	if (uSource == pDstServerUser) {
		RATELIMIT(uSource);
	}

	// Handle potential temporary access tokens
	QStringList temporaryAccessTokens;
	for (int i = 0; i < msg.temporary_access_tokens_size(); i++) {
		temporaryAccessTokens << u8(msg.temporary_access_tokens(i));
	}
	TemporaryAccessTokenHelper tempTokenHelper(uSource, temporaryAccessTokens, this);

	if (msg.has_channel_id()) {
		Channel *c = qhChannels.value(msg.channel_id());
		if (!c || (c == pDstServerUser->cChannel))
			return;

		if ((uSource != pDstServerUser) && (!hasPermission(uSource, pDstServerUser->cChannel, ChanACL::Move))) {
			PERM_DENIED(uSource, pDstServerUser->cChannel, ChanACL::Move);
			return;
		}

		if (!hasPermission(uSource, c, ChanACL::Move) && !hasPermission(pDstServerUser, c, ChanACL::Enter)) {
			PERM_DENIED(pDstServerUser, c, ChanACL::Enter);
			return;
		}
		if (isChannelFull(c, uSource)) {
			PERM_DENIED_FALLBACK(ChannelFull, Version::fromComponents(1, 2, 1), QLatin1String("Channel is full"));
			return;
		}
	}

	QList< Channel * > listeningChannelsAdd;
	int passedChannelListener = 0;
	// Check permission for each channel
	for (int i = 0; i < msg.listening_channel_add_size(); i++) {
		Channel *c = qhChannels.value(msg.listening_channel_add(i));

		if (!c) {
			continue;
		}

		if (!hasPermission(pDstServerUser, c, ChanACL::Listen)) {
			PERM_DENIED(pDstServerUser, c, ChanACL::Listen);
			continue;
		}

		if (Meta::mp->iMaxListenersPerChannel >= 0
			&& Meta::mp->iMaxListenersPerChannel - m_channelListenerManager.getListenerCountForChannel(c->iId) - 1
				   < 0) {
			// A limit for the amount of listener proxies per channel is set and it has been reached already
			PERM_DENIED_FALLBACK(ChannelListenerLimit, Version::fromComponents(1, 4, 0),
								 QLatin1String("No more listeners allowed in this channel"));
			continue;
		}

		if (Meta::mp->iMaxListenerProxiesPerUser >= 0
			&& Meta::mp->iMaxListenerProxiesPerUser
					   - m_channelListenerManager.getListenedChannelCountForUser(uSource->uiSession)
					   - passedChannelListener - 1
				   < 0) {
			// A limit for the amount of listener proxies per user is set and it has been reached already
			PERM_DENIED_FALLBACK(UserListenerLimit, Version::fromComponents(1, 4, 0),
								 QLatin1String("No more listeners allowed in this channel"));
			continue;
		}

		passedChannelListener++;

		listeningChannelsAdd << c;
	}

	if (msg.has_mute() || msg.has_deaf() || msg.has_suppress() || msg.has_priority_speaker()) {
		if (pDstServerUser->iId == 0) {
			PERM_DENIED_TYPE(SuperUser);
			return;
		}
		if (!hasPermission(uSource, pDstServerUser->cChannel, ChanACL::MuteDeafen) || msg.suppress()) {
			PERM_DENIED(uSource, pDstServerUser->cChannel, ChanACL::MuteDeafen);
			return;
		}
	}

	if (msg.has_mute() || msg.has_deaf() || msg.has_suppress()) {
		if (pDstServerUser->cChannel->bTemporary) {
			// If the destination user is inside a temporary channel,
			// the source user needs to have the MuteDeafen ACL in the first
			// non-temporary parent channel.

			Channel *c = pDstServerUser->cChannel;
			while (c && c->bTemporary) {
				c = c->cParent;
			}

			if (!c || !hasPermission(uSource, c, ChanACL::MuteDeafen)) {
				PERM_DENIED_TYPE(TemporaryChannel);
				return;
			}
		}
	}

	QString comment;

	if (msg.has_comment()) {
		bool changed = false;
		comment      = u8(msg.comment());
		if (uSource != pDstServerUser) {
			if (!hasPermission(uSource, root, ChanACL::ResetUserContent)) {
				PERM_DENIED(uSource, root, ChanACL::ResetUserContent);
				return;
			}
			if (comment.length() > 0) {
				PERM_DENIED_TYPE(TextTooLong);
				return;
			}
		}


		if (!isTextAllowed(comment, changed)) {
			PERM_DENIED_TYPE(TextTooLong);
			return;
		}
		if (changed)
			msg.set_comment(u8(comment));
	}

	if (msg.has_texture()) {
		if (iMaxImageMessageLength > 0
			&& (msg.texture().length() > static_cast< unsigned int >(iMaxImageMessageLength))) {
			PERM_DENIED_TYPE(TextTooLong);
			return;
		}
		if (uSource != pDstServerUser) {
			if (!hasPermission(uSource, root, ChanACL::ResetUserContent)) {
				PERM_DENIED(uSource, root, ChanACL::ResetUserContent);
				return;
			}
			if (msg.texture().length() > 0) {
				PERM_DENIED_TYPE(TextTooLong);
				return;
			}
		}
	}


	if (msg.has_user_id()) {
		ChanACL::Perm p = (uSource == pDstServerUser) ? ChanACL::SelfRegister : ChanACL::Register;
		if ((pDstServerUser->iId >= 0) || !hasPermission(uSource, root, p)) {
			PERM_DENIED(uSource, root, p);
			return;
		}
		if (pDstServerUser->qsHash.isEmpty()) {
			PERM_DENIED_HASH(pDstServerUser);
			return;
		}
	}

	// Prevent self-targeting state changes from being applied to others
	if ((pDstServerUser != uSource)
		&& (msg.has_self_deaf() || msg.has_self_mute() || msg.has_plugin_context() || msg.has_plugin_identity()
			|| msg.has_recording() || msg.listening_channel_add_size() > 0
			|| msg.listening_channel_remove_size() > 0)) {
		return;
	}

	/*
		-------------------- Permission checks done. Now act --------------------
	*/
	bool bBroadcast = false;

	if (msg.has_texture()) {
		QByteArray qba = blob(msg.texture());
		if (pDstServerUser->iId >= 0) {
			// For registered users store the texture we just received in the database
			if (!setTexture(*pDstServerUser, qba)) {
				return;
			}
		} else {
			// For unregistered users or SuperUser only get the hash
			hashAssign(pDstServerUser->qbaTexture, pDstServerUser->qbaTextureHash, qba);
		}

		// The texture will be sent out later in this function
		bBroadcast = true;
	}

	// Writing to bSelfMute, bSelfDeaf and ssContext
	// requires holding a write lock on qrwlVoiceThread.
	{
		QWriteLocker wl(&qrwlVoiceThread);

		if (msg.has_self_deaf()) {
			pDstServerUser->bSelfDeaf = msg.self_deaf();
			if (pDstServerUser->bSelfDeaf)
				msg.set_self_mute(true);
			bBroadcast = true;
		}

		if (msg.has_self_mute()) {
			pDstServerUser->bSelfMute = msg.self_mute();
			if (!pDstServerUser->bSelfMute) {
				msg.set_self_deaf(false);
				pDstServerUser->bSelfDeaf = false;
			}
			bBroadcast = true;
		}

		if (msg.has_plugin_context()) {
			pDstServerUser->ssContext = msg.plugin_context();

			// Make sure to clear this from the packet so we don't broadcast it
			msg.clear_plugin_context();
		}
	}

	if (msg.has_plugin_identity()) {
		pDstServerUser->qsIdentity = u8(msg.plugin_identity());
		// Make sure to clear this from the packet so we don't broadcast it
		msg.clear_plugin_identity();
	}

	if (!comment.isNull()) {
		setComment(*uSource, comment);

		bBroadcast = true;
	}



	if (msg.has_mute() || msg.has_deaf() || msg.has_suppress() || msg.has_priority_speaker()) {
		// Writing to bDeaf, bMute and bSuppress requires
		// holding a write lock on qrwlVoiceThread.
		QWriteLocker wl(&qrwlVoiceThread);

		if (msg.has_deaf()) {
			pDstServerUser->bDeaf = msg.deaf();
			if (pDstServerUser->bDeaf)
				msg.set_mute(true);
		}
		if (msg.has_mute()) {
			pDstServerUser->bMute = msg.mute();
			if (!pDstServerUser->bMute) {
				msg.set_deaf(false);
				pDstServerUser->bDeaf = false;
			}
		}
		if (msg.has_suppress())
			pDstServerUser->bSuppress = msg.suppress();

		if (msg.has_priority_speaker())
			pDstServerUser->bPrioritySpeaker = msg.priority_speaker();

		log(uSource, QString("Changed speak-state of %1 (%2 %3 %4 %5)")
						 .arg(QString(*pDstServerUser), QString::number(pDstServerUser->bMute),
							  QString::number(pDstServerUser->bDeaf), QString::number(pDstServerUser->bSuppress),
							  QString::number(pDstServerUser->bPrioritySpeaker)));

		bBroadcast = true;
	}

	if (msg.has_recording() && (pDstServerUser->bRecording != msg.recording())) {
		assert(uSource == pDstServerUser);

		pDstServerUser->bRecording = msg.recording();

		MumbleProto::TextMessage mptm;
		mptm.add_tree_id(0);
		if (pDstServerUser->bRecording) {
			if (!allowRecording) {
				// User tried to start recording even though this server forbids it
				// -> Kick user
				MumbleProto::UserRemove mpur;
				mpur.set_session(uSource->uiSession);
				mpur.set_reason("Recording is not allowed on this server");
				sendMessage(uSource, mpur);
				uSource->forceFlush();
				uSource->disconnectSocket(true);

				// We just kicked this user, so there is no point in further processing his/her message
				return;
			} else {
				mptm.set_message(u8(QString(QLatin1String("User '%1' started recording")).arg(pDstServerUser->qsName)));
			}
		} else {
			mptm.set_message(u8(QString(QLatin1String("User '%1' stopped recording")).arg(pDstServerUser->qsName)));
		}

		sendAll(mptm, Version::fromComponents(1, 2, 3), Version::CompareMode::LessThan);

		bBroadcast = true;
	}

	if (msg.has_channel_id()) {
		Channel *c = qhChannels.value(msg.channel_id());

		userEnterChannel(pDstServerUser, c, msg);
		log(uSource, QString("Moved %1 to %2").arg(QString(*pDstServerUser), QString(*c)));
		bBroadcast = true;
	}

	// Handle channel listening
	// Note that it is important to handle the listening channels after channel-joins
	std::set< unsigned int > additionalVolumeAdjustedChannels;
	for (Channel *c : listeningChannelsAdd) {
		addChannelListener(*pDstServerUser, *c);

		log(QString::fromLatin1("\"%1\" is now listening to channel \"%2\"")
				.arg(QString(*pDstServerUser))
				.arg(QString(*c)));

		float volumeFactor =
			m_channelListenerManager.getListenerVolumeAdjustment(pDstServerUser->uiSession, c->iId).factor;

		if (volumeFactor != 1.0f) {
			additionalVolumeAdjustedChannels.insert(c->iId);
		}
	}
	for (int i = 0; i < msg.listening_volume_adjustment_size(); i++) {
		const MumbleProto::UserState::VolumeAdjustment &adjustment = msg.listening_volume_adjustment(i);

		const Channel *channel = qhChannels.value(adjustment.listening_channel());

		if (channel) {
			setChannelListenerVolume(*pDstServerUser, *channel, adjustment.volume_adjustment());

			// If the message contains a new volume adjustment for this channel anyway, we don't have to
			// add this adjustment again
			additionalVolumeAdjustedChannels.erase(channel->iId);
		} else {
			log(uSource, QString::fromLatin1("Invalid channel ID \"%1\" in volume adjustment")
							 .arg(adjustment.listening_channel()));
		}
	}
	for (int i = 0; i < msg.listening_channel_remove_size(); i++) {
		Channel *c = qhChannels.value(msg.listening_channel_remove(i));

		if (c) {
			disableChannelListener(*pDstServerUser, *c);

			log(QString::fromLatin1("\"%1\" is no longer listening to \"%2\"")
					.arg(QString(*pDstServerUser))
					.arg(QString(*c)));

			// If the channel is no longer listened to anyway, we don't need to broadcast its volume adjustment
			additionalVolumeAdjustedChannels.erase(c->iId);
		}
	}
	// For the channels that are listened to and for which no explicit volume adjustment is part of the message yet,
	// but which had a volume adjustment != 1 (restored from the DB), we ensure that this adjustment is broadcast
	// as if it was part of the message all along.
	for (unsigned int channelID : additionalVolumeAdjustedChannels) {
		const Channel *channel = qhChannels.value(channelID);

		if (channel) {
			const float factor =
				m_channelListenerManager.getListenerVolumeAdjustment(pDstServerUser->uiSession, channelID).factor;

			MumbleProto::UserState::VolumeAdjustment *adjustment = msg.add_listening_volume_adjustment();
			adjustment->set_listening_channel(channel->iId);
			adjustment->set_volume_adjustment(factor);
		}
	}

	bool listenerVolumeChanged = msg.listening_volume_adjustment_size() > 0;
	bool listenerChanged       = !listeningChannelsAdd.isEmpty() || msg.listening_channel_remove_size() > 0;

	bool broadcastingBecauseOfVolumeChange = !bBroadcast && listenerVolumeChanged;
	bBroadcast                             = bBroadcast || listenerChanged || listenerVolumeChanged;

	if (listenerChanged || listenerVolumeChanged) {
		// As whisper targets also contain information about ChannelListeners and
		// their associated volume adjustment, we have to clear the target cache
		clearWhisperTargetCache();
	}


	bool bDstAclChanged = false;
	if (msg.has_user_id()) {
		// Handle user (Self-)Registration
		if (registerUser(*pDstServerUser)) {
			assert(pDstServerUser->iId >= 0);
			msg.set_user_id(static_cast< unsigned int >(pDstServerUser->iId));
			bDstAclChanged = true;
		} else {
			// Registration failed
			msg.clear_user_id();
		}

		bBroadcast = true;
	}

	if (bBroadcast) {
		// Texture handling for clients < 1.2.2.
		// Send the texture data in the message.
		if (msg.has_texture() && (pDstServerUser->qbaTexture.length() >= 4)
			&& (qFromBigEndian< unsigned int >(
					reinterpret_cast< const unsigned char * >(pDstServerUser->qbaTexture.constData()))
				!= 600 * 60 * 4)) {
			// This is a new style texture, don't send it because the client doesn't handle it correctly / crashes.
			msg.clear_texture();
			sendAll(msg, Version::fromComponents(1, 2, 2), Version::CompareMode::LessThan);
			msg.set_texture(blob(pDstServerUser->qbaTexture));
		} else {
			// This is an old style texture, empty texture or there was no texture in this packet,
			// send the message unchanged.
			sendAll(msg, Version::fromComponents(1, 2, 2), Version::CompareMode::LessThan);
		}

		// Texture / comment handling for clients >= 1.2.2.
		// Send only a hash of the texture / comment text. The client will request the actual data if necessary.
		if (msg.has_texture() && !pDstServerUser->qbaTextureHash.isEmpty()) {
			msg.clear_texture();
			msg.set_texture_hash(blob(pDstServerUser->qbaTextureHash));
		}
		if (msg.has_comment() && !pDstServerUser->qbaCommentHash.isEmpty()) {
			msg.clear_comment();
			msg.set_comment_hash(blob(pDstServerUser->qbaCommentHash));
		}

		if (uSource->m_version >= Version::fromComponents(1, 2, 2)) {
			sendMessage(uSource, msg);
		}
		if (!broadcastListenerVolumeAdjustments) {
			// Don't broadcast the volume adjustments to everyone
			msg.clear_listening_volume_adjustment();
		}

		if (broadcastListenerVolumeAdjustments || !broadcastingBecauseOfVolumeChange) {
			sendExcept(uSource, msg, Version::fromComponents(1, 2, 2), Version::CompareMode::AtLeast);
		}

		if (bDstAclChanged) {
			clearACLCache(pDstServerUser);
		} else if (listenerChanged || listenerVolumeChanged) {
			// We only have to do this if the ACLs didn't change as
			// clearACLCache calls clearWhisperTargetChache anyways
			clearWhisperTargetCache();
		}
	}

	if (msg.has_channel_id()) syncWatchTogetherStateForUser(pDstServerUser);
	emit userStateChanged(pDstServerUser);
}

void Server::msgUserRemove(ServerUser *uSource, MumbleProto::UserRemove &msg) {
	ZoneScoped;

	MSG_SETUP(ServerUser::Authenticated);
	VICTIM_SETUP;

	msg.set_actor(uSource->uiSession);

	bool ban = msg.has_ban() && msg.ban();

	Channel *c                   = qhChannels.value(0);
	QFlags< ChanACL::Perm > perm = ban ? ChanACL::Ban : (ChanACL::Ban | ChanACL::Kick);

	if ((pDstServerUser->iId == 0) || !hasPermission(uSource, c, perm)) {
		PERM_DENIED(uSource, c, perm);
		return;
	}

	if (ban) {
		// Before Mumble 1.6, a ban meant certificate and IP ban. This is the fallback.
		// Starting with Mumble 1.6, an admin must specify which method to use.
		bool banCertificate = !msg.has_ban_certificate() || msg.ban_certificate();
		bool banIP          = !msg.has_ban_ip() || msg.ban_ip();

		// User might not even have a certificate
		banCertificate &= !pDstServerUser->qsHash.isEmpty();

		if (!banIP && !banCertificate) {
			// No ban method specified
			return;
		}

		Ban b;
		if (banIP) {
			b.haAddress = pDstServerUser->haAddress;
			b.iMask     = 128;
		}
		if (banCertificate) {
			b.qsHash = pDstServerUser->qsHash;
		}
		b.qsReason   = u8(msg.reason());
		b.qsUsername = pDstServerUser->qsName;
		b.qdtStart   = QDateTime::currentDateTime().toUTC();
		b.iDuration  = 0;

		m_bans.push_back(std::move(b));
		m_dbWrapper.saveBans(iServerNum, m_bans);
	}

	sendAll(msg);
	if (ban)
		log(uSource, QString("Kickbanned %1 (%2)").arg(QString(*pDstServerUser), u8(msg.reason())));
	else
		log(uSource, QString("Kicked %1 (%2)").arg(QString(*pDstServerUser), u8(msg.reason())));
	pDstServerUser->disconnectSocket();
}

void Server::msgChannelState(ServerUser *uSource, MumbleProto::ChannelState &msg) {
	ZoneScoped;

	MSG_SETUP(ServerUser::Authenticated);

	Channel *c = nullptr;
	Channel *p = nullptr;

	// If this message relates to an existing channel check if the id is really valid
	if (msg.has_channel_id()) {
		c = qhChannels.value(msg.channel_id());
		if (!c)
			return;
	} else {
		RATELIMIT(uSource);
	}

	// Check if the parent exists
	if (msg.has_parent()) {
		p = qhChannels.value(msg.parent());
		if (!p)
			return;
	}

	msg.clear_links();

	QString qsName;
	QString qsDesc;
	if (msg.has_description()) {
		qsDesc       = u8(msg.description());
		bool changed = false;
		if (!isTextAllowed(qsDesc, changed)) {
			PERM_DENIED_TYPE(TextTooLong);
			return;
		}
		if (changed)
			msg.set_description(u8(qsDesc));
	}

	if (msg.has_name()) {
		// If we are sent a channel name this means we want to create this channel so
		// check if the name is valid and not already in use.
		qsName = u8(msg.name());

		if (!validateChannelName(qsName)) {
			PERM_DENIED_TYPE(ChannelName);
			return;
		}

		if (qsName.length() == 0) {
			PERM_DENIED_TYPE(ChannelName);
			return;
		}

		if (p || (c && c->iId != 0)) {
			Channel *cp = p ? p : c->cParent;
			for (Channel *sibling : cp->qlChannels) {
				if (sibling->qsName == qsName) {
					PERM_DENIED_TYPE(ChannelName);
					return;
				}
			}
		}
	}

	if (p) {
		// Having a parent channel given means we either want to create
		// a channel in or move a channel into this parent.

		if (!canNest(p, c)) {
			PERM_DENIED_FALLBACK(NestingLimit, Version::fromComponents(1, 2, 4),
								 QLatin1String("Channel nesting limit reached"));
			return;
		}
	}

	if (!c) {
		// If we don't have a channel handle up to now we want to create a new channel
		// so check if the user has enough rights and we got everything we need.
		if (!p || qsName.isNull())
			return;

		if (iChannelCountLimit != 0 && qhChannels.count() >= iChannelCountLimit) {
			PERM_DENIED_FALLBACK(ChannelCountLimit, Version::fromComponents(1, 3, 0),
								 QLatin1String("Channel count limit reached"));
			return;
		}

		ChanACL::Perm perm = msg.temporary() ? ChanACL::MakeTempChannel : ChanACL::MakeChannel;
		if (!hasPermission(uSource, p, perm)) {
			PERM_DENIED(uSource, p, perm);
			return;
		}

		if ((uSource->iId < 0) && uSource->qsHash.isEmpty()) {
			PERM_DENIED_HASH(uSource);
			return;
		}

		if (p->bTemporary) {
			PERM_DENIED_TYPE(TemporaryChannel);
			return;
		}

		c = createNewChannel(p, qsName, msg.temporary(), msg.position(), msg.max_users());
		hashAssign(c->qsDesc, c->qbaDescHash, qsDesc);

		if (uSource->iId >= 0) {
			Group *g = new Group(c, "admin");
			g->qsAdd << uSource->iId;
		}

		if (!hasPermission(uSource, c, ChanACL::Write)) {
			ChanACL *a    = new ChanACL(c);
			a->bApplyHere = true;
			a->bApplySubs = false;
			if (uSource->iId >= 0)
				a->iUserId = uSource->iId;
			else
				a->qsGroup = QLatin1Char('$') + uSource->qsHash;
			a->pDeny  = ChanACL::None;
			a->pAllow = ChanACL::Write | ChanACL::Traverse;

			clearACLCache();
		}

		if (!c->bTemporary) {
			m_dbWrapper.updateChannelData(iServerNum, *c);
		}

		msg.set_channel_id(c->iId);
		log(uSource, QString("Added channel %1 under %2").arg(QString(*c), QString(*p)));
		emit channelCreated(c);

		sendAll(msg, Version::fromComponents(1, 2, 2), Version::CompareMode::LessThan);
		if (!c->qbaDescHash.isEmpty()) {
			msg.clear_description();
			msg.set_description_hash(blob(c->qbaDescHash));
		}
		sendAll(msg, Version::fromComponents(1, 2, 2), Version::CompareMode::AtLeast);

		if (c->bTemporary) {
			// If a temporary channel has been created move the creator right in there
			MumbleProto::UserState mpus;
			mpus.set_session(uSource->uiSession);
			mpus.set_channel_id(c->iId);
			userEnterChannel(uSource, c, mpus);
			sendAll(mpus);
			syncWatchTogetherStateForUser(uSource);
			emit userStateChanged(uSource);
		}
	} else {
		// The message is related to an existing channel c so check if the user is allowed to modify it
		// and perform the modifications
		if (!qsName.isNull()) {
			if (!hasPermission(uSource, c, ChanACL::Write) || (c->iId == 0)) {
				PERM_DENIED(uSource, c, ChanACL::Write);
				return;
			}
		}
		if (!qsDesc.isNull()) {
			if (!hasPermission(uSource, c, ChanACL::Write)) {
				PERM_DENIED(uSource, c, ChanACL::Write);
				return;
			}
		}
		if (msg.has_position()) {
			if (!hasPermission(uSource, c, ChanACL::Write)) {
				PERM_DENIED(uSource, c, ChanACL::Write);
				return;
			}
		}
		if (p) {
			// If we received a parent channel check if it differs from the old one and is not
			// Temporary. If that is the case check if the user has enough rights and if the
			// channel name is not used in the target location. Abort otherwise.
			if (p == c->cParent)
				return;

			Channel *ip = p;
			while (ip) {
				if (ip == c)
					return;
				ip = ip->cParent;
			}

			if (p->bTemporary) {
				PERM_DENIED_TYPE(TemporaryChannel);
				return;
			}

			if (!hasPermission(uSource, c, ChanACL::Write)) {
				PERM_DENIED(uSource, c, ChanACL::Write);
				return;
			}

			QFlags< ChanACL::Perm > parentMakePermission =
				c->bTemporary ? ChanACL::MakeTempChannel : ChanACL::MakeChannel;
			if (!hasPermission(uSource, p, parentMakePermission)) {
				PERM_DENIED(uSource, p, parentMakePermission);
				return;
			}

			QString name = qsName.isNull() ? c->qsName : qsName;

			for (Channel *sibling : p->qlChannels) {
				if (sibling->qsName == name) {
					PERM_DENIED_TYPE(ChannelName);
					return;
				}
			}
		}
		QList< Channel * > qlAdd;
		QList< Channel * > qlRemove;

		if (msg.links_add_size() || msg.links_remove_size()) {
			if (!hasPermission(uSource, c, ChanACL::LinkChannel)) {
				PERM_DENIED(uSource, c, ChanACL::LinkChannel);
				return;
			}
			if (msg.links_remove_size()) {
				for (int i = 0; i < msg.links_remove_size(); ++i) {
					unsigned int link = msg.links_remove(i);
					Channel *l        = qhChannels.value(link);
					if (!l) {
						return;
					}
					qlRemove << l;
				}
			}
			if (msg.links_add_size()) {
				for (int i = 0; i < msg.links_add_size(); ++i) {
					unsigned int link = msg.links_add(i);
					Channel *l        = qhChannels.value(link);
					if (!l) {
						return;
					}
					if (!hasPermission(uSource, l, ChanACL::LinkChannel)) {
						PERM_DENIED(uSource, l, ChanACL::LinkChannel);
						return;
					}
					qlAdd << l;
				}
			}
		}

		if (msg.has_max_users()) {
			if (!hasPermission(uSource, c, ChanACL::Write)) {
				PERM_DENIED(uSource, c, ChanACL::Write);
				return;
			}
		}

		// All permission checks done -- the update is good.

		if (p) {
			log(uSource, QString("Moved channel %1 from %2 to %3").arg(QString(*c), QString(*c->cParent), QString(*p)));

			{
				QWriteLocker wl(&qrwlVoiceThread);
				c->cParent->removeChannel(c);
				p->addChannel(c);
			}
		}
		if (!qsName.isNull()) {
			log(uSource, QString("Renamed channel %1 to %2").arg(QString(*c), QString(qsName)));
			c->qsName = qsName;
		}
		if (!qsDesc.isNull())
			hashAssign(c->qsDesc, c->qbaDescHash, qsDesc);

		if (msg.has_position())
			c->iPosition = msg.position();

		for (Channel *l : qlAdd) {
			linkChannels(*c, *l);
		}

		for (Channel *l : qlRemove) {
			unlinkChannels(*c, *l);
		}

		if (msg.has_max_users())
			c->uiMaxUsers = msg.max_users();

		if (!c->bTemporary) {
			m_dbWrapper.updateChannelData(iServerNum, *c);
		}
		emit channelStateChanged(c);

		sendAll(msg, Version::fromComponents(1, 2, 2), Version::CompareMode::LessThan);
		if (msg.has_description() && !c->qbaDescHash.isEmpty()) {
			msg.clear_description();
			msg.set_description_hash(blob(c->qbaDescHash));
		}
		sendAll(msg, Version::fromComponents(1, 2, 2), Version::CompareMode::AtLeast);
	}
}

void Server::msgChannelRemove(ServerUser *uSource, MumbleProto::ChannelRemove &msg) {
	ZoneScoped;

	MSG_SETUP(ServerUser::Authenticated);

	Channel *c = qhChannels.value(msg.channel_id());
	if (!c)
		return;

	if (!hasPermission(uSource, c, ChanACL::Write) || (c->iId == 0)) {
		PERM_DENIED(uSource, c, ChanACL::Write);
		return;
	}

	log(uSource, QString("Removed channel %1").arg(*c));

	removeChannel(c);
}

void Server::msgTextMessage(ServerUser *uSource, MumbleProto::TextMessage &msg) {
	ZoneScoped;

	MSG_SETUP(ServerUser::Authenticated);
	QMutexLocker qml(&qmCache);

	// For signal userTextMessage (RPC consumers)
	TextMessage tm;

	// List of users to route the message to
	QSet< ServerUser * > users;
	// List of channels used if dest is a tree of channels
	QQueue< Channel * > q;

	RATELIMIT(uSource);

	int res = 0;
	emit textMessageFilterSig(res, uSource, msg);
	switch (res) {
		// Accept
		case 0:
			// No-op.
			break;
		// Reject
		case 1:
			PERM_DENIED(uSource, uSource->cChannel, ChanACL::TextMessage);
			return;
		// Drop
		case 2:
			return;
	}

	QString text = u8(msg.message());
	bool changed = false;

	if (!isTextAllowed(text, changed)) {
		PERM_DENIED_TYPE(TextTooLong);
		return;
	}
	if (text.isEmpty()) {
		return;
	}
	if (changed) {
		msg.set_message(u8(text));
	}

	tm.qsText = text;

	{ // Happy easter
		char m[29] = { 0117, 0160, 0145, 0156, 040,  0164, 0150, 0145, 040, 0160, 0157, 0144, 040, 0142, 0141,
					   0171, 040,  0144, 0157, 0157, 0162, 0163, 054,  040, 0110, 0101, 0114, 056, 0 };
		if (msg.channel_id_size() == 1 && msg.channel_id(0) == 0 && msg.message() == m) {
			PERM_DENIED_TYPE(H9K);
			return;
		}
	}

	msg.set_actor(uSource->uiSession);

	// Send the message to all users that are in (= have joined) OR are
	// "listening" to channels to which the message has been directed to
	for (int i = 0; i < msg.channel_id_size(); ++i) {
		unsigned int id = msg.channel_id(i);

		Channel *c = qhChannels.value(id);
		if (!c) {
			return;
		}

		if (!ChanACL::hasPermission(uSource, c, ChanACL::TextMessage, &acCache)) {
			PERM_DENIED(uSource, c, ChanACL::TextMessage);
			return;
		}

		// Users directly in that channel
		for (User *p : c->qlUsers) {
			users.insert(static_cast< ServerUser * >(p));
		}

		// Users only listening in that channel
		for (unsigned int session : m_channelListenerManager.getListenersForChannel(c->iId)) {
			ServerUser *currentUser = qhUsers.value(session);
			if (currentUser) {
				users.insert(currentUser);
			}
		}

		tm.qlChannels.append(id);
	}

	// If the message is sent to trees of channels, find all affected channels
	// and append them to q
	for (int i = 0; i < msg.tree_id_size(); ++i) {
		unsigned int id = msg.tree_id(i);

		Channel *c = qhChannels.value(id);
		if (!c) {
			return;
		}

		if (!ChanACL::hasPermission(uSource, c, ChanACL::TextMessage, &acCache)) {
			PERM_DENIED(uSource, c, ChanACL::TextMessage);
			return;
		}

		q.enqueue(c);

		tm.qlTrees.append(id);
	}

	// Go through all channels in q and append all users in those channels
	// to the list of recipients
	// Sub-channels are enqued so they are also checked by a later loop-iteration
	while (!q.isEmpty()) {
		Channel *c = q.dequeue();
		if (ChanACL::hasPermission(uSource, c, ChanACL::TextMessage, &acCache)) {
			for (Channel *sub : c->qlChannels) {
				q.enqueue(sub);
			}
			// Users directly in that channel
			for (User *p : c->qlUsers) {
				users.insert(static_cast< ServerUser * >(p));
			}
			// Users only listening in that channel
			for (unsigned int session : m_channelListenerManager.getListenersForChannel(c->iId)) {
				ServerUser *currentUser = qhUsers.value(session);
				if (currentUser) {
					users.insert(currentUser);
				}
			}
		}
	}

	// Go through all users the message is sent to directly
	for (int i = 0; i < msg.session_size(); ++i) {
		unsigned int session = msg.session(i);
		ServerUser *u        = qhUsers.value(session);
		if (u) {
			if (!ChanACL::hasPermission(uSource, u->cChannel, ChanACL::TextMessage, &acCache)) {
				PERM_DENIED(uSource, u->cChannel, ChanACL::TextMessage);
				return;
			}
			users.insert(u);
		}

		tm.qlSessions.append(session);
	}

	// Remove the message sender from the list of users to send the message to
	users.remove(uSource);

	if (msg.channel_id_size() == 1 && msg.tree_id_size() == 0 && msg.session_size() == 0) {
		Channel *channel = qhChannels.value(msg.channel_id(0));
		if (channel) {
			persistAndBroadcastChatMessage(uSource, text, ::msdb::ChatMessageBodyFormat::PlainText,
										   MumbleProto::Channel, channel->iId, channel,
										   ::msdb::ChatThreadScope::Channel, {}, std::nullopt, users);
			emit userTextMessage(uSource, tm);
			return;
		}
	}

	// Actually send the original message to the affected users
	for (ServerUser *u : users) {
		sendMessage(u, msg);
	}

	// Emit the signal for RPC consumers
	emit userTextMessage(uSource, tm);
}

void Server::msgChatSend(ServerUser *uSource, MumbleProto::ChatSend &msg) {
	ZoneScoped;

	MSG_SETUP(ServerUser::Authenticated);
	QMutexLocker qml(&qmCache);

	RATELIMIT(uSource);

	if (!clientSupportsPersistentChat(uSource)) {
		sendPersistentChatUnsupported(uSource);
		return;
	}

	QString bodyText = msg.has_body_text() ? u8(msg.body_text()) : u8(msg.message());
	bool bodyChanged = false;
	const msdb::ChatMessageBodyFormat bodyFormat =
		dbBodyFormatFromProto(msg.has_body_format() ? msg.body_format() : MumbleProto::ChatBodyFormatPlainText);
	if (!isTextAllowed(bodyText, bodyChanged)) {
		PERM_DENIED_TYPE(TextTooLong);
		return;
	}
	if (bodyText.isEmpty() && msg.attachment_asset_ids_size() == 0) {
		return;
	}
	const std::optional< unsigned int > replyToMessageID =
		msg.has_reply_to_message_id() && msg.reply_to_message_id() > 0
			? std::optional< unsigned int >(msg.reply_to_message_id())
			: std::nullopt;

	MumbleProto::ChatScope scope = msg.has_scope() ? msg.scope() : MumbleProto::Channel;
	unsigned int scopeID =
		msg.has_scope_id() ? msg.scope_id() : (uSource->cChannel ? uSource->cChannel->iId : Mumble::ROOT_CHANNEL_ID);
	Channel *permissionChannel      = nullptr;
	::msdb::ChatThreadScope dbScope = ::msdb::ChatThreadScope::Channel;
	std::optional<::msdb::DBTextChannel > selectedTextChannel;
	const std::optional< unsigned int > sourceUserID = persistedUserID(uSource);

	switch (scope) {
		case MumbleProto::Channel:
			permissionChannel = qhChannels.value(scopeID);
			dbScope           = ::msdb::ChatThreadScope::Channel;
			break;
		case MumbleProto::ServerGlobal:
			scopeID           = 0;
			permissionChannel = qhChannels.value(Mumble::ROOT_CHANNEL_ID);
			dbScope           = ::msdb::ChatThreadScope::ServerGlobal;
			break;
		case MumbleProto::Aggregate:
			return;
		case MumbleProto::TextChannel: {
			std::optional<::msdb::DBTextChannel > textChannel = m_dbWrapper.getTextChannel(iServerNum, scopeID);
			if (!textChannel) {
				return;
			}

			selectedTextChannel = textChannel;
			permissionChannel = qhChannels.value(textChannel->aclChannelID);
			dbScope           = ::msdb::ChatThreadScope::TextChannel;
			break;
		}
		case MumbleProto::Private:
			if (!clientSupportsChatFeature(uSource, MumbleProto::ChatFeatureDirectMessages)) {
				sendPersistentChatTextDenied(this, uSource,
											 tr("This client did not advertise persistent direct-message support."));
				return;
			}
			if (!sourceUserID) {
				sendPersistentChatTextDenied(this, uSource,
											 tr("Register your user before keeping direct-message history."));
				return;
			}
			if (!msg.has_scope_id() || scopeID == sourceUserID.value()
				|| !m_dbWrapper.registeredUserExists(iServerNum, scopeID)) {
				sendPersistentChatTextDenied(this, uSource, tr("That direct-message recipient is unavailable."));
				return;
			}
			permissionChannel = qhChannels.value(Mumble::ROOT_CHANNEL_ID);
			dbScope           = ::msdb::ChatThreadScope::Private;
			break;
		default:
			return;
	}

	if (scope == MumbleProto::ServerGlobal && !bPersistentGlobalChatEnabled) {
		MumbleProto::PermissionDenied denied;
		denied.set_permission(static_cast< unsigned int >(ChanACL::TextMessage));
		denied.set_channel_id(Mumble::ROOT_CHANNEL_ID);
		denied.set_session(uSource->uiSession);
		denied.set_type(MumbleProto::PermissionDenied_DenyType_Permission);
		denied.set_reason(u8(QStringLiteral("Global chat is disabled by this server.")));
		sendMessage(uSource, denied);
		return;
	}

	if (!permissionChannel) {
		return;
	}
	if (scope == MumbleProto::TextChannel && isStonksTextChannelID(scopeID)
		&& !hasStonksAccess(uSource, &acCache)) {
		Channel *rootChannel = qhChannels.value(Mumble::ROOT_CHANNEL_ID);
		if (rootChannel) {
			PERM_DENIED(uSource, rootChannel, ChanACL::UseStonks);
		}
		return;
	}

	if (scope != MumbleProto::Private
		&& !ChanACL::hasPermission(uSource, permissionChannel, ChanACL::TextMessage, &acCache)) {
		PERM_DENIED(uSource, permissionChannel, ChanACL::TextMessage);
		return;
	}

	const std::string scopeKey =
		scope == MumbleProto::Private && sourceUserID ? privateChatScopeKey(sourceUserID.value(), scopeID)
													  : chatScopeKey(scope, scopeID);
	if (scopeKey.empty()) {
		return;
	}

	const std::optional<::msdb::DBChatThread > existingThread =
		m_dbWrapper.getChatThreadByScope(iServerNum, dbScope, scopeKey);
	if (replyToMessageID) {
		if (!existingThread) {
			sendPersistentChatTextDenied(this, uSource, tr("The selected reply target is unavailable."));
			return;
		}

		const std::optional<::msdb::DBChatMessage > replyTarget =
			m_dbWrapper.getChatMessage(iServerNum, replyToMessageID.value());
		if (!replyTarget || replyTarget->threadID != existingThread->threadID
			|| replyTarget->deletedAt > std::chrono::system_clock::time_point()
			|| !canAccessChatMessage(uSource, *replyTarget, *existingThread, permissionChannel, &acCache)) {
			sendPersistentChatTextDenied(this, uSource, tr("The selected reply target is unavailable."));
			return;
		}
	}

	if (scope == MumbleProto::TextChannel
		&& !clientSupportsChatFeature(uSource, MumbleProto::ChatFeatureTextChannels)) {
		sendPersistentChatTextDenied(this, uSource, tr("This client did not advertise text-channel chat support."));
		return;
	}

	QSet< ServerUser * > legacyFallbackRecipients;
	if (scope == MumbleProto::Channel) {
		legacyFallbackRecipients = legacyChannelRecipients(qhUsers, m_channelListenerManager, permissionChannel);
		legacyFallbackRecipients.remove(uSource);
	}

	std::vector< msdb::DBChatMessageAttachment > attachments;
	if (msg.attachment_asset_ids_size() > 0
		&& !clientSupportsChatFeature(uSource, MumbleProto::ChatFeatureAttachments)) {
		sendPersistentChatTextDenied(this, uSource, tr("This client did not advertise chat attachment support."));
		return;
	}

	if (msg.attachment_asset_ids_size() > 0
		&& static_cast< unsigned int >(msg.attachment_asset_ids_size()) > uiChatAttachmentLimit) {
		MumbleProto::PermissionDenied denied;
		denied.set_session(uSource->uiSession);
		denied.set_type(MumbleProto::PermissionDenied_DenyType_Text);
		denied.set_reason(u8(QStringLiteral("Too many attachments for one message.")));
		sendMessage(uSource, denied);
		return;
	}

	attachments.reserve(static_cast< std::size_t >(msg.attachment_asset_ids_size()));
	for (int i = 0; i < msg.attachment_asset_ids_size(); ++i) {
		const unsigned int assetID = msg.attachment_asset_ids(i);
		if (assetID == 0 || !m_dbWrapper.chatAssetExists(iServerNum, assetID)) {
			sendPersistentChatTextDenied(this, uSource, tr("One or more attachments are unavailable."));
			return;
		}

		const msdb::DBChatAsset asset = m_dbWrapper.getChatAsset(iServerNum, assetID);
		const bool ownedBySender = asset.ownerUserID
			? (uSource->iId >= 0 && asset.ownerUserID.value() == static_cast< unsigned int >(uSource->iId))
			: qhEphemeralChatAssetOwners.value(assetID) == uSource;
		if (!ownedBySender && !canAccessChatAsset(uSource, assetID)) {
			sendPersistentChatTextDenied(this, uSource, tr("One or more attachments are not accessible."));
			return;
		}

		msdb::DBChatMessageAttachment attachment(iServerNum, 0, assetID);
		attachment.displayOrder   = static_cast< unsigned int >(attachments.size());
		const QString declaredFilename =
			i < msg.attachment_filenames_size() ? u8(msg.attachment_filenames(i)) : QString();
		attachment.filename       = u8(safeChatAttachmentFilename(
			declaredFilename, assetID, asset.kind, QString::fromStdString(asset.mime)));
		attachment.mime           = asset.mime;
		attachment.byteSize       = asset.byteSize;
		attachment.kind           = asset.kind;
		attachment.width          = asset.width;
		attachment.height         = asset.height;
		attachment.durationMs     = asset.durationMs;
		attachment.inlineSafe     = isInlineSafeAsset(asset.kind, QString::fromStdString(asset.mime), true);
		attachment.previewAssetID = asset.previewAssetID;
		attachments.push_back(std::move(attachment));
	}

	persistAndBroadcastChatMessage(uSource, bodyText, bodyFormat, scope, scopeID, permissionChannel, dbScope,
								   attachments, replyToMessageID, legacyFallbackRecipients);
	for (const msdb::DBChatMessageAttachment &attachment : attachments) {
		qhEphemeralChatAssetOwners.remove(attachment.assetID);
	}

	const std::optional< unsigned int > stonksCommandTextChannelID =
		selectedTextChannel
			? resolvedStonksTextChannelID(uiStonksTextChannelID, m_dbWrapper.getTextChannels(iServerNum))
			: std::nullopt;
	if (scope == MumbleProto::TextChannel && selectedTextChannel
		&& (isStonksTextChannel(*selectedTextChannel)
			|| (stonksCommandTextChannelID && *stonksCommandTextChannelID == selectedTextChannel->textChannelID))) {
		const std::optional< Mumble::Stonks::Command > command = Mumble::Stonks::parseCommand(bodyText);
		if (!command) {
			return;
		}

		const auto respond = [&](const QString &responseBody) {
			persistAndBroadcastServerChatMessage(responseBody, scope, scopeID, permissionChannel, dbScope,
												 QStringLiteral("Stonks"));
		};
		const auto currentUserID = persistedUserID(uSource);
		const auto registeredDisplayName = [this](unsigned int userID) {
			const QString name = getRegisteredUserName(static_cast< int >(userID)).trimmed();
			return name.isEmpty() ? QStringLiteral("user %1").arg(userID) : name;
		};
		const auto requireRegisteredUser = [&]() -> bool {
			if (currentUserID) {
				return true;
			}
			respond(QStringLiteral("Register your user first to use social stonks commands."));
			return false;
		};
		const auto followedUserNames = [&](unsigned int followerUserID) {
			QStringList names;
			for (unsigned int userID : m_dbWrapper.getStonksFollowedUsers(iServerNum, followerUserID)) {
				const QString userName = registeredDisplayName(userID);
				if (!userName.isEmpty()) {
					names << userName;
				}
			}
			names.sort(Qt::CaseInsensitive);
			return names;
		};

		switch (command->type) {
			case Mumble::Stonks::CommandType::Help:
				respond(stonksHelpText());
				break;
			case Mumble::Stonks::CommandType::Quote:
				respond(QStringLiteral("Quote: $%1").arg(command->symbol));
				break;
			case Mumble::Stonks::CommandType::SetScore: {
				if (!requireRegisteredUser()) {
					break;
				}

				::msdb::DBStonksScore score(iServerNum, currentUserID.value(), u8(command->period));
				score.scorePercent = command->scorePercent;
				score.updatedAt    = std::chrono::system_clock::now();
				m_dbWrapper.setStonksScore(score);
				respond(QStringLiteral("Recorded %1 at %2 for %3.")
							.arg(registeredDisplayName(currentUserID.value()),
								 formatStonksPercent(command->scorePercent), command->period));
				break;
			}
			case Mumble::Stonks::CommandType::Leaderboard: {
				std::vector< StonksLeaderboardEntry > entries = stonksLedgerLeaderboard(this, command->period, 100);
				entries.erase(std::remove_if(entries.begin(), entries.end(), [](const StonksLeaderboardEntry &entry) {
					return entry.insufficientHistory;
				}), entries.end());
				if (entries.empty()) {
					for (const ::msdb::DBStonksScore &score :
						 m_dbWrapper.getStonksLeaderboard(iServerNum, u8(command->period), 100)) {
						StonksLeaderboardEntry entry;
						entry.userID              = score.userID;
						entry.userName            = registeredDisplayName(score.userID);
						entry.score               = score.scorePercent;
						entry.insufficientHistory = false;
						entries.push_back(std::move(entry));
					}
					std::sort(entries.begin(), entries.end(), higherStonksScore);
				}

				if (entries.empty()) {
					respond(QStringLiteral("No stonks ledger returns for %1 yet. Use the Stonks panel or `score %1 +4.2` to join.")
								.arg(command->period));
					break;
				}

				QStringList lines;
				lines << QStringLiteral("Stonks leaderboard - %1").arg(command->period);
				const std::size_t count = std::min< std::size_t >(entries.size(), 10);
				for (std::size_t i = 0; i < count; ++i) {
					const StonksLeaderboardEntry &entry = entries.at(i);
					lines << QStringLiteral("%1. %2  %3%4")
								 .arg(static_cast< int >(i + 1))
								 .arg(entry.userName, formatStonksPercent(entry.score),
									  entry.window.partialPeriod ? QStringLiteral(" (partial)") : QString());
				}
				respond(lines.join(QLatin1Char('\n')));
				break;
			}
			case Mumble::Stonks::CommandType::Me: {
				if (!requireRegisteredUser()) {
					break;
				}

				QStringList lines;
				std::map< QString, double > scoresByPeriod;
				for (const ::msdb::DBStonksScore &score :
					 m_dbWrapper.getStonksScores(iServerNum, currentUserID.value())) {
					scoresByPeriod[u8(score.period)] = score.scorePercent;
				}

				lines << QStringLiteral("%1's stonks profile").arg(registeredDisplayName(currentUserID.value()));
				const std::optional< ::msdb::DBStonksSnapshot > latestSnapshot =
					m_dbWrapper.getLatestStonksSnapshotForUser(iServerNum, currentUserID.value());
				if (latestSnapshot) {
					lines << QStringLiteral("Ledger: active");
				}
				for (const QString &period : { QStringLiteral("1d"), QStringLiteral("7d"), QStringLiteral("30d"),
											   QStringLiteral("ytd") }) {
					if (latestSnapshot) {
						const std::vector< StonksLeaderboardEntry > periodEntries =
							stonksLedgerLeaderboard(this, period, 1000);
						const auto entry = std::find_if(periodEntries.cbegin(), periodEntries.cend(), [&](const auto &candidate) {
							return candidate.userID == currentUserID.value() && !candidate.insufficientHistory;
						});
						if (entry != periodEntries.cend()) {
							lines << QStringLiteral("%1: %2%3")
									 .arg(period, formatStonksPercent(entry->score),
										  entry->window.partialPeriod ? QStringLiteral(" (partial)") : QString());
							continue;
						}
					}
					const auto score = scoresByPeriod.find(period);
					if (score != scoresByPeriod.cend()) {
						lines << QStringLiteral("%1: %2 manual").arg(period, formatStonksPercent(score->second));
					}
				}

				const QStringList following = followedUserNames(currentUserID.value());
				lines << QStringLiteral("Following: %1").arg(following.isEmpty() ? QStringLiteral("nobody yet")
																				: following.join(QStringLiteral(", ")));
				respond(lines.join(QLatin1Char('\n')));
				break;
			}
			case Mumble::Stonks::CommandType::Follow:
			case Mumble::Stonks::CommandType::Unfollow: {
				if (!requireRegisteredUser()) {
					break;
				}

				const int targetUserID = getRegisteredUserID(command->targetName);
				if (targetUserID < 0) {
					respond(QStringLiteral("I couldn't find registered user `%1`.").arg(command->targetName));
					break;
				}
				if (static_cast< unsigned int >(targetUserID) == currentUserID.value()) {
					respond(QStringLiteral("You are already you. Very bullish, but not followable."));
					break;
				}

				const unsigned int targetRegisteredUserID = static_cast< unsigned int >(targetUserID);
				if (command->type == Mumble::Stonks::CommandType::Follow) {
					::msdb::DBStonksFollow follow(iServerNum, currentUserID.value(), targetRegisteredUserID);
					follow.createdAt = std::chrono::system_clock::now();
					m_dbWrapper.setStonksFollow(follow);
					respond(QStringLiteral("Now following %1.").arg(registeredDisplayName(targetRegisteredUserID)));
				} else {
					m_dbWrapper.removeStonksFollow(iServerNum, currentUserID.value(), targetRegisteredUserID);
					respond(QStringLiteral("Unfollowed %1.").arg(registeredDisplayName(targetRegisteredUserID)));
				}
				break;
			}
			case Mumble::Stonks::CommandType::Following: {
				if (!requireRegisteredUser()) {
					break;
				}

				const QStringList following = followedUserNames(currentUserID.value());
				respond(following.isEmpty() ? QStringLiteral("You are not following anyone yet.")
											 : QStringLiteral("Following: %1").arg(following.join(QStringLiteral(", "))));
				break;
			}
			case Mumble::Stonks::CommandType::None:
				break;
		}
	}
}

void Server::msgChatMessage(ServerUser *, MumbleProto::ChatMessage &) {
}

void Server::msgChatMessageDelete(ServerUser *uSource, MumbleProto::ChatMessageDelete &msg) {
	ZoneScoped;

	MSG_SETUP(ServerUser::Authenticated);
	QMutexLocker qml(&qmCache);

	RATELIMIT(uSource);

	if (!clientSupportsChatFeature(uSource, MumbleProto::ChatFeatureMessageDelete)) {
		sendPersistentChatUnsupported(uSource);
		return;
	}

	if (!msg.has_message_id() || msg.message_id() == 0) {
		return;
	}

	std::optional<::msdb::DBChatMessage > message = m_dbWrapper.getChatMessage(iServerNum, msg.message_id());
	if (!message) {
		sendPersistentChatTextDenied(this, uSource, tr("That message is no longer available."));
		return;
	}

	if (msg.has_thread_id() && msg.thread_id() != message->threadID) {
		sendPersistentChatTextDenied(this, uSource, tr("That delete target belongs to a different conversation."));
		return;
	}

	if (message->deletedAt > std::chrono::system_clock::time_point()) {
		return;
	}

	const ::msdb::DBChatThread thread = m_dbWrapper.getChatThread(iServerNum, message->threadID);
	QHash< unsigned int, ::msdb::DBTextChannel > textChannelsByID;
	for (const ::msdb::DBTextChannel &currentTextChannel : m_dbWrapper.getTextChannels(iServerNum)) {
		textChannelsByID.insert(currentTextChannel.textChannelID, currentTextChannel);
	}

	MumbleProto::ChatScope scope = MumbleProto::Channel;
	unsigned int scopeID         = 0;
	Channel *permissionChannel   = nullptr;
	if (!resolveStoredChatThread(thread, qhChannels, textChannelsByID, scope, scopeID, permissionChannel)
		|| !permissionChannel) {
		sendPersistentChatTextDenied(this, uSource, tr("That message is no longer available."));
		return;
	}

	if ((msg.has_scope() && msg.scope() != scope) || (msg.has_scope_id() && msg.scope_id() != scopeID)) {
		sendPersistentChatTextDenied(this, uSource, tr("That delete target belongs to a different conversation."));
		return;
	}

	if (scope == MumbleProto::ServerGlobal && !bPersistentGlobalChatEnabled) {
		sendPersistentChatTextDenied(this, uSource, tr("Global chat is disabled by this server."));
		return;
	}

	const bool ownRegisteredMessage =
		message->authorUserID && uSource->iId >= 0
		&& message->authorUserID.value() == static_cast< unsigned int >(uSource->iId);
	const bool ownLiveSessionMessage = message->authorSession && message->authorSession.value() == uSource->uiSession;
	if (!ownRegisteredMessage && !ownLiveSessionMessage
		&& !ChanACL::hasPermission(uSource, permissionChannel, ChanACL::Write, &acCache)) {
		PERM_DENIED(uSource, permissionChannel, ChanACL::Write);
		return;
	}

	if (!canAccessChatMessage(uSource, *message, thread, permissionChannel, &acCache)) {
		sendPersistentChatTextDenied(this, uSource, tr("That message is no longer available."));
		return;
	}

	std::optional<::msdb::DBChatMessage > deletedMessage =
		m_dbWrapper.deleteChatMessage(iServerNum, message->messageID);
	if (!deletedMessage) {
		return;
	}
	invalidateChatHistoryCache(deletedMessage->threadID);

	const auto resolvedAuthorName = [this](const ::msdb::DBChatMessage &storedMessage) -> std::optional< std::string > {
		if (storedMessage.authorName && !storedMessage.authorName->empty()) {
			return storedMessage.authorName;
		}

		if (storedMessage.authorSession) {
			ServerUser *currentUser = qhUsers.value(storedMessage.authorSession.value());
			if (currentUser && !currentUser->qsName.isEmpty()) {
				return u8(currentUser->qsName);
			}
		}

		if (storedMessage.authorUserID
			&& m_dbWrapper.registeredUserExists(iServerNum, storedMessage.authorUserID.value())) {
			return m_dbWrapper.getUserName(iServerNum, storedMessage.authorUserID.value());
		}

		return std::nullopt;
	};

	const MumbleProto::ChatMessage protoMessage =
		protoChatMessageFromDB(*deletedMessage, scope, scopeID, resolvedAuthorName(*deletedMessage), std::nullopt);

	QSet< ServerUser * > persistentRecipients;
	if (scope == MumbleProto::Channel) {
		persistentRecipients = recipientsWithChatHistoryAccess(this, qhUsers, scope, scopeID, permissionChannel, acCache,
														   message->createdAt);
		persistentRecipients.unite(recipientsWithLivePersistentChatAccess(
			this, qhUsers, scope, scopeID, permissionChannel, acCache,
			legacyChannelRecipients(qhUsers, m_channelListenerManager, permissionChannel)));
		persistentRecipients.insert(uSource);
	} else {
		persistentRecipients = recipientsWithChatHistoryAccess(this, qhUsers, scope, scopeID, permissionChannel, acCache,
															   message->createdAt);
		persistentRecipients.unite(
			recipientsWithLivePersistentChatAccess(this, qhUsers, scope, scopeID, permissionChannel, acCache));
	}

	for (ServerUser *currentUser : persistentRecipients) {
		if (clientSupportsChatFeature(currentUser, MumbleProto::ChatFeatureMessageDelete)) {
			sendMessage(currentUser, protoMessage);
		}
	}
}

std::vector<::msdb::DBChatMessage > Server::latestChatHistoryMessagesForThread(unsigned int threadID,
																			   unsigned int amount) {
	if (amount == 0) {
		return {};
	}

	auto cachedIt = qhChatHistoryLatestPageCache.find(threadID);
	if (cachedIt != qhChatHistoryLatestPageCache.end()) {
		cachedIt->lastAccessSerial = ++uiChatHistoryLatestPageCacheAccessSerial;
		if (amount <= cachedIt->messages.size()) {
			return std::vector<::msdb::DBChatMessage >(
				cachedIt->messages.end() - static_cast< std::ptrdiff_t >(amount), cachedIt->messages.end());
		}
		if (!cachedIt->hasOlderMessages) {
			return cachedIt->messages;
		}
	}

	const unsigned int cacheFetchAmount = CHAT_HISTORY_SERVER_CACHE_STORED_MESSAGES + 1;
	std::vector<::msdb::DBChatMessage > fetchedMessages =
		m_dbWrapper.getChatMessages(iServerNum, threadID, 0, static_cast< int >(cacheFetchAmount));
	ChatHistoryLatestPageCacheEntry entry;
	entry.hasOlderMessages = fetchedMessages.size() > CHAT_HISTORY_SERVER_CACHE_STORED_MESSAGES;
	if (entry.hasOlderMessages) {
		fetchedMessages.erase(fetchedMessages.begin(),
							  fetchedMessages.begin()
								  + static_cast< std::ptrdiff_t >(fetchedMessages.size()
																  - CHAT_HISTORY_SERVER_CACHE_STORED_MESSAGES));
	}
	entry.messages = fetchedMessages;
	entry.lastAccessSerial = ++uiChatHistoryLatestPageCacheAccessSerial;
	qhChatHistoryLatestPageCache.insert(threadID, entry);
	pruneChatHistoryLatestPageCache(threadID);

	const ChatHistoryLatestPageCacheEntry &storedEntry = qhChatHistoryLatestPageCache[threadID];
	if (amount <= storedEntry.messages.size()) {
		return std::vector<::msdb::DBChatMessage >(
			storedEntry.messages.end() - static_cast< std::ptrdiff_t >(amount), storedEntry.messages.end());
	}
	if (!storedEntry.hasOlderMessages) {
		return storedEntry.messages;
	}

	return m_dbWrapper.getChatMessages(iServerNum, threadID, 0, static_cast< int >(amount));
}

void Server::rememberLatestChatHistoryMessage(const ::msdb::DBChatMessage &message) {
	auto cachedIt = qhChatHistoryLatestPageCache.find(message.threadID);
	if (cachedIt == qhChatHistoryLatestPageCache.end()) {
		return;
	}

	cachedIt->messages.push_back(message);
	cachedIt->lastAccessSerial = ++uiChatHistoryLatestPageCacheAccessSerial;
	while (cachedIt->messages.size() > CHAT_HISTORY_SERVER_CACHE_STORED_MESSAGES) {
		cachedIt->messages.erase(cachedIt->messages.begin());
		cachedIt->hasOlderMessages = true;
	}
}

void Server::invalidateChatHistoryCache(unsigned int threadID) {
	qhChatHistoryLatestPageCache.remove(threadID);
}

void Server::pruneChatHistoryLatestPageCache(unsigned int preserveThreadID) {
	while (qhChatHistoryLatestPageCache.size() > MAX_CHAT_HISTORY_SERVER_CACHED_THREADS) {
		auto evictionIt = qhChatHistoryLatestPageCache.end();
		for (auto it = qhChatHistoryLatestPageCache.begin(); it != qhChatHistoryLatestPageCache.end(); ++it) {
			if (it.key() == preserveThreadID && qhChatHistoryLatestPageCache.size() > 1) {
				continue;
			}
			if (evictionIt == qhChatHistoryLatestPageCache.end()
				|| it->lastAccessSerial < evictionIt->lastAccessSerial) {
				evictionIt = it;
			}
		}
		if (evictionIt == qhChatHistoryLatestPageCache.end()) {
			break;
		}
		qhChatHistoryLatestPageCache.erase(evictionIt);
	}
}

void Server::sendChatHistoryResponseForRequest(ServerUser *uSource, const MumbleProto::ChatHistoryRequest &msg,
											   bool silentPermissionDenied, unsigned int limitCap) {
	MumbleProto::ChatScope scope = msg.has_scope() ? msg.scope() : MumbleProto::Channel;
	unsigned int scopeID =
		msg.has_scope_id() ? msg.scope_id() : (uSource->cChannel ? uSource->cChannel->iId : Mumble::ROOT_CHANNEL_ID);
	Channel *permissionChannel      = nullptr;
	::msdb::ChatThreadScope dbScope = ::msdb::ChatThreadScope::Channel;
	const std::optional< unsigned int > sourceUserID = persistedUserID(uSource);
	const unsigned int effectiveLimitCap             = std::max(1U, limitCap);
	const unsigned int limit =
		msg.has_limit() ? std::clamp(msg.limit(), 1U, effectiveLimitCap) : std::min(50U, effectiveLimitCap);
	const unsigned int startOffset                      = msg.has_start_offset() ? msg.start_offset() : 0;
	const std::optional< unsigned int > beforeMessageID = msg.has_before_message_id() && msg.before_message_id() > 0
															  ? std::optional< unsigned int >(msg.before_message_id())
															  : std::nullopt;

	MumbleProto::ChatHistoryResponse response;
	response.set_scope(scope);
	response.set_scope_id(scopeID);
	response.set_start_offset(startOffset);
	response.set_has_more(false);
	response.set_has_older(false);
	const auto sendEmptyWarmupResponse = [&]() {
		if (!silentPermissionDenied) {
			return false;
		}

		sendMessage(uSource, response);
		return true;
	};

	switch (scope) {
		case MumbleProto::Channel:
			permissionChannel = qhChannels.value(scopeID);
			dbScope           = ::msdb::ChatThreadScope::Channel;
			break;
		case MumbleProto::ServerGlobal:
			scopeID           = 0;
			permissionChannel = qhChannels.value(Mumble::ROOT_CHANNEL_ID);
			dbScope           = ::msdb::ChatThreadScope::ServerGlobal;
			break;
		case MumbleProto::Aggregate:
			break;
		case MumbleProto::TextChannel: {
			std::optional<::msdb::DBTextChannel > textChannel = m_dbWrapper.getTextChannel(iServerNum, scopeID);
			if (!textChannel) {
				sendEmptyWarmupResponse();
				return;
			}

			permissionChannel = qhChannels.value(textChannel->aclChannelID);
			dbScope           = ::msdb::ChatThreadScope::TextChannel;
			break;
		}
		case MumbleProto::Private:
			if (!clientSupportsChatFeature(uSource, MumbleProto::ChatFeatureDirectMessages)) {
				if (sendEmptyWarmupResponse()) {
					return;
				}
				sendPersistentChatTextDenied(this, uSource,
											 tr("This client did not advertise persistent direct-message support."));
				return;
			}
			if (!sourceUserID) {
				if (sendEmptyWarmupResponse()) {
					return;
				}
				sendPersistentChatTextDenied(this, uSource,
											 tr("Register your user before loading direct-message history."));
				return;
			}
			if (!msg.has_scope_id() || scopeID == sourceUserID.value()
				|| !m_dbWrapper.registeredUserExists(iServerNum, scopeID)) {
				if (sendEmptyWarmupResponse()) {
					return;
				}
				sendPersistentChatTextDenied(this, uSource, tr("That direct-message recipient is unavailable."));
				return;
			}
			permissionChannel = qhChannels.value(Mumble::ROOT_CHANNEL_ID);
			dbScope           = ::msdb::ChatThreadScope::Private;
			break;
		default:
			sendEmptyWarmupResponse();
			return;
	}

	response.set_scope(scope);
	response.set_scope_id(scopeID);
	response.set_start_offset(startOffset);

	const auto resolvedAuthorName = [this](const ::msdb::DBChatMessage &message) -> std::optional< std::string > {
		if (message.authorName && !message.authorName->empty()) {
			return message.authorName;
		}

		if (message.authorSession) {
			ServerUser *currentUser = qhUsers.value(message.authorSession.value());
			if (currentUser && !currentUser->qsName.isEmpty()) {
				return u8(currentUser->qsName);
			}
		}

		if (message.authorUserID && m_dbWrapper.registeredUserExists(iServerNum, message.authorUserID.value())) {
			return m_dbWrapper.getUserName(iServerNum, message.authorUserID.value());
		}

		return std::nullopt;
	};
	const auto resolvedReactionActorName = [this](unsigned int actorUserID) -> std::optional< std::string > {
		const std::optional< std::string > connectedName = connectedUserNameForPersistentID(qhUsers, actorUserID);
		if (connectedName) {
			return connectedName;
		}

		if (actorUserID <= static_cast< unsigned int >(std::numeric_limits< int >::max())) {
			const QString registeredName = getRegisteredUserName(static_cast< int >(actorUserID)).trimmed();
			if (!registeredName.isEmpty()) {
				return u8(registeredName);
			}
		}

		return std::nullopt;
	};
	QHash< unsigned int, QByteArray > actorTextureHashCache;
	const auto resolvedActorTextureHash = [this, &actorTextureHashCache](unsigned int actorUserID) {
		return registeredUserTextureHash(this, actorUserID, actorTextureHashCache);
	};
	if (scope == MumbleProto::ServerGlobal && !bPersistentGlobalChatEnabled) {
		if (sendEmptyWarmupResponse()) {
			return;
		}

		MumbleProto::PermissionDenied denied;
		denied.set_permission(static_cast< unsigned int >(ChanACL::ViewTextMessageHistory));
		denied.set_channel_id(Mumble::ROOT_CHANNEL_ID);
		denied.set_session(uSource->uiSession);
		denied.set_type(MumbleProto::PermissionDenied_DenyType_Permission);
		denied.set_reason(u8(QStringLiteral("Global chat is disabled by this server.")));
		sendMessage(uSource, denied);
		return;
	}

	if (scope == MumbleProto::Aggregate) {
		const QList< int > chatFeatures = effectiveChatFeatures(uSource);
		QHash< unsigned int, ::msdb::DBTextChannel > textChannelsByID;
		for (const ::msdb::DBTextChannel &currentTextChannel : m_dbWrapper.getTextChannels(iServerNum)) {
			textChannelsByID.insert(currentTextChannel.textChannelID, currentTextChannel);
		}

		std::vector< AggregateChatEntry > entries;
		for (const ::msdb::DBChatThread &currentThread : m_dbWrapper.getChatThreads(iServerNum)) {
			MumbleProto::ChatScope messageScope = MumbleProto::Channel;
			unsigned int messageScopeID         = 0;
			Channel *messagePermissionChannel   = nullptr;

			if (!resolveStoredChatThread(currentThread, qhChannels, textChannelsByID, messageScope, messageScopeID,
										 messagePermissionChannel)) {
				continue;
			}

			if (messageScope == MumbleProto::ServerGlobal && !bPersistentGlobalChatEnabled) {
				continue;
			}

			const ChatHistoryAccess access =
				resolveChatHistoryAccess(uSource, messageScope, messageScopeID, messagePermissionChannel, &acCache);
			ChatHistoryAccess effectiveAccess = access;
			if (!effectiveAccess.allowed) {
				const std::optional< std::chrono::system_clock::time_point > sessionVisibleAfter =
					liveSessionVisibleAfterForUser(this, uSource, messageScope, messageScopeID, messagePermissionChannel,
												   m_channelListenerManager, acCache);
				if (!sessionVisibleAfter) {
					continue;
				}
				effectiveAccess.allowed      = true;
				effectiveAccess.visibleAfter = *sessionVisibleAfter;
			}

			for (const ::msdb::DBChatMessage &currentMessage :
				 m_dbWrapper.getChatMessages(iServerNum, currentThread.threadID, 0, -1, effectiveAccess.visibleAfter)) {
				AggregateChatEntry entry;
				entry.message      = currentMessage;
				entry.scope        = messageScope;
				entry.scopeID      = messageScopeID;
				entry.visibleAfter = effectiveAccess.visibleAfter;
				entries.push_back(std::move(entry));
			}
		}

		std::sort(entries.begin(), entries.end(), newerAggregateEntry);

		const std::size_t offset = startOffset;
		const std::size_t pageEnd =
			std::min< std::size_t >(entries.size(), static_cast< std::size_t >(startOffset) + limit);
		response.set_has_more(entries.size() > static_cast< std::size_t >(startOffset) + limit);

		if (offset < pageEnd) {
			std::vector< AggregateChatEntry > page(entries.begin() + static_cast< std::ptrdiff_t >(offset),
												   entries.begin() + static_cast< std::ptrdiff_t >(pageEnd));
			std::reverse(page.begin(), page.end());

			for (const AggregateChatEntry &entry : page) {
				const std::optional< ChatReplyPreview > replyPreview =
					resolveReplyPreview(m_dbWrapper, iServerNum, entry.message, resolvedAuthorName, entry.visibleAfter);
				*response.add_messages() =
					protoChatMessageFromDB(entry.message, entry.scope, entry.scopeID, resolvedAuthorName(entry.message),
										   persistedUserID(uSource), replyPreview, chatFeatures,
										   resolvedReactionActorName, resolvedActorTextureHash);
			}
		}

		clearHistoricChatActorSessions(response);
		constrainChatHistoryResponseSize(response);
		sendMessage(uSource, response);
		return;
	}

	if (!permissionChannel) {
		sendEmptyWarmupResponse();
		return;
	}

	const ChatHistoryAccess access = resolveChatHistoryAccess(uSource, scope, scopeID, permissionChannel, &acCache);
	ChatHistoryAccess effectiveAccess = access;
	if (!effectiveAccess.allowed) {
		const std::optional< std::chrono::system_clock::time_point > sessionVisibleAfter =
			liveSessionVisibleAfterForUser(this, uSource, scope, scopeID, permissionChannel,
									   m_channelListenerManager, acCache);
		if (sessionVisibleAfter) {
			effectiveAccess.allowed      = true;
			effectiveAccess.visibleAfter = *sessionVisibleAfter;
		}
	}
	if (!effectiveAccess.allowed) {
		if (sendEmptyWarmupResponse()) {
			return;
		}

		PERM_DENIED(uSource, permissionChannel, ChanACL::ViewTextMessageHistory);
		return;
	}

	const std::string scopeKey =
		scope == MumbleProto::Private && sourceUserID ? privateChatScopeKey(sourceUserID.value(), scopeID)
													  : chatScopeKey(scope, scopeID);
	std::optional<::msdb::DBChatThread > thread = m_dbWrapper.getChatThreadByScope(iServerNum, dbScope, scopeKey);
	if (!thread) {
		sendMessage(uSource, response);
		return;
	}

	response.set_thread_id(thread->threadID);

	const bool canUseLatestPageCache = !beforeMessageID && startOffset == 0
									   && effectiveAccess.visibleAfter
											  == std::chrono::system_clock::time_point();
	std::vector<::msdb::DBChatMessage > messages =
		canUseLatestPageCache
			? latestChatHistoryMessagesForThread(thread->threadID, limit + 1)
			: (beforeMessageID
				   ? m_dbWrapper.getChatMessagesBefore(iServerNum, thread->threadID, beforeMessageID.value(),
														limit + 1, effectiveAccess.visibleAfter)
				   : m_dbWrapper.getChatMessages(iServerNum, thread->threadID, startOffset,
												 static_cast< int >(limit + 1), effectiveAccess.visibleAfter));
	if (messages.size() > limit) {
		messages.erase(messages.begin());
		response.set_has_more(true);
		response.set_has_older(true);
	}

	if (!messages.empty()) {
		response.set_oldest_message_id(messages.front().messageID);
	}

	for (const ::msdb::DBChatMessage &currentMessage : messages) {
		const std::optional< ChatReplyPreview > replyPreview =
			resolveReplyPreview(m_dbWrapper, iServerNum, currentMessage, resolvedAuthorName,
								effectiveAccess.visibleAfter);
		*response.add_messages() = protoChatMessageFromDB(
			currentMessage, scope, scopeID, resolvedAuthorName(currentMessage), persistedUserID(uSource), replyPreview,
			effectiveChatFeatures(uSource), resolvedReactionActorName, resolvedActorTextureHash);
	}

	const std::optional< unsigned int > userID = persistedUserID(uSource);
	if (userID && clientSupportsChatFeature(uSource, MumbleProto::ChatFeatureReadState)) {
		std::optional<::msdb::DBChatReadState > readState =
			m_dbWrapper.getChatReadState(iServerNum, thread->threadID, userID.value());
		if (readState) {
			const std::optional<::msdb::DBChatMessage > lastReadMessage =
				readState->lastReadMessageID > 0
					? m_dbWrapper.getChatMessage(iServerNum, readState->lastReadMessageID)
					: std::optional<::msdb::DBChatMessage >();
			if (readState->lastReadMessageID == 0
				|| (lastReadMessage && lastReadMessage->threadID == thread->threadID
					&& messageVisibleInWindow(*lastReadMessage, effectiveAccess.visibleAfter))) {
				response.set_last_read_message_id(readState->lastReadMessageID);
			}
		}
	}

	clearHistoricChatActorSessions(response);
	constrainChatHistoryResponseSize(response);
	sendMessage(uSource, response);
}

void Server::msgChatHistoryRequest(ServerUser *uSource, MumbleProto::ChatHistoryRequest &msg) {
	ZoneScoped;

	MSG_SETUP(ServerUser::Authenticated);
	QMutexLocker qml(&qmCache);

	if (uSource->leakyBucket.ratelimit(1)) {
		// History is request/response from the UI. A silent throttle leaves clients stuck in loading.
		sendMessage(uSource, emptyChatHistoryResponseForRequest(uSource, msg));
		return;
	}

	if (!clientSupportsPersistentChat(uSource)) {
		sendPersistentChatUnsupported(uSource);
		return;
	}

	sendChatHistoryResponseForRequest(uSource, msg);
}

void Server::msgChatHistoryWarmupRequest(ServerUser *uSource, MumbleProto::ChatHistoryWarmupRequest &msg) {
	ZoneScoped;

	MSG_SETUP(ServerUser::Authenticated);
	QMutexLocker qml(&qmCache);

	if (uSource->leakyBucket.ratelimit(1)) {
		QSet< QString > seenScopes;
		int warmedScopes = 0;
		for (int i = 0; i < msg.requests_size() && warmedScopes < MAX_CHAT_HISTORY_WARMUP_SCOPES; ++i) {
			MumbleProto::ChatHistoryRequest request = msg.requests(i);
			const MumbleProto::ChatScope scope      = request.has_scope() ? request.scope() : MumbleProto::Channel;
			const unsigned int scopeID =
				request.has_scope_id() ? request.scope_id()
									   : (uSource->cChannel ? uSource->cChannel->iId : Mumble::ROOT_CHANNEL_ID);
			if (scope != MumbleProto::Channel && scope != MumbleProto::TextChannel && scope != MumbleProto::Private) {
				continue;
			}

			const QString scopeKey = QString::fromLatin1("%1:%2").arg(static_cast< int >(scope)).arg(scopeID);
			if (seenScopes.contains(scopeKey)) {
				continue;
			}
			seenScopes.insert(scopeKey);

			request.set_scope(scope);
			request.set_scope_id(scopeID);
			request.set_start_offset(0);
			request.clear_before_message_id();
			sendMessage(uSource, emptyChatHistoryResponseForRequest(uSource, request));
			++warmedScopes;
		}
		return;
	}

	if (!clientSupportsPersistentChat(uSource)
		|| !clientSupportsChatFeature(uSource, MumbleProto::ChatFeatureHistoryWarmup)) {
		sendPersistentChatUnsupported(uSource);
		return;
	}

	QSet< QString > seenScopes;
	int warmedScopes = 0;
	for (int i = 0; i < msg.requests_size() && warmedScopes < MAX_CHAT_HISTORY_WARMUP_SCOPES; ++i) {
		MumbleProto::ChatHistoryRequest request = msg.requests(i);
		const MumbleProto::ChatScope scope      = request.has_scope() ? request.scope() : MumbleProto::Channel;
		const unsigned int scopeID =
			request.has_scope_id() ? request.scope_id()
								   : (uSource->cChannel ? uSource->cChannel->iId : Mumble::ROOT_CHANNEL_ID);
		if (scope != MumbleProto::Channel && scope != MumbleProto::TextChannel && scope != MumbleProto::Private) {
			continue;
		}

		const QString scopeKey = QString::fromLatin1("%1:%2").arg(static_cast< int >(scope)).arg(scopeID);
		if (seenScopes.contains(scopeKey)) {
			continue;
		}
		seenScopes.insert(scopeKey);

		request.set_scope(scope);
		request.set_scope_id(scopeID);
		request.set_start_offset(0);
		request.clear_before_message_id();
		request.set_limit(request.has_limit()
							  ? std::min(request.limit(), MAX_CHAT_HISTORY_WARMUP_MESSAGES_PER_SCOPE)
							  : MAX_CHAT_HISTORY_WARMUP_MESSAGES_PER_SCOPE);
		sendChatHistoryResponseForRequest(uSource, request, true, MAX_CHAT_HISTORY_WARMUP_MESSAGES_PER_SCOPE);
		++warmedScopes;
	}
}

void Server::msgChatHistoryResponse(ServerUser *, MumbleProto::ChatHistoryResponse &) {
}

void Server::msgChatReadStateUpdate(ServerUser *uSource, MumbleProto::ChatReadStateUpdate &msg) {
	ZoneScoped;

	MSG_SETUP(ServerUser::Authenticated);
	QMutexLocker qml(&qmCache);

	RATELIMIT(uSource);

	if (!clientSupportsChatFeature(uSource, MumbleProto::ChatFeatureReadState)) {
		sendPersistentChatUnsupported(uSource);
		return;
	}

	const std::optional< unsigned int > userID = persistedUserID(uSource);
	if (!userID) {
		return;
	}

	MumbleProto::ChatScope scope = msg.has_scope() ? msg.scope() : MumbleProto::Channel;
	unsigned int scopeID =
		msg.has_scope_id() ? msg.scope_id() : (uSource->cChannel ? uSource->cChannel->iId : Mumble::ROOT_CHANNEL_ID);
	Channel *permissionChannel      = nullptr;
	::msdb::ChatThreadScope dbScope = ::msdb::ChatThreadScope::Channel;

	switch (scope) {
		case MumbleProto::Channel:
			permissionChannel = qhChannels.value(scopeID);
			dbScope           = ::msdb::ChatThreadScope::Channel;
			break;
		case MumbleProto::ServerGlobal:
			scopeID           = 0;
			permissionChannel = qhChannels.value(Mumble::ROOT_CHANNEL_ID);
			dbScope           = ::msdb::ChatThreadScope::ServerGlobal;
			break;
		case MumbleProto::Aggregate:
			return;
		case MumbleProto::TextChannel: {
			std::optional<::msdb::DBTextChannel > textChannel = m_dbWrapper.getTextChannel(iServerNum, scopeID);
			if (!textChannel) {
				return;
			}

			permissionChannel = qhChannels.value(textChannel->aclChannelID);
			dbScope           = ::msdb::ChatThreadScope::TextChannel;
			break;
		}
		case MumbleProto::Private:
			if (!clientSupportsChatFeature(uSource, MumbleProto::ChatFeatureDirectMessages)) {
				sendPersistentChatTextDenied(this, uSource,
											 tr("This client did not advertise persistent direct-message support."));
				return;
			}
			if (!msg.has_scope_id() || scopeID == userID.value()
				|| !m_dbWrapper.registeredUserExists(iServerNum, scopeID)) {
				sendPersistentChatTextDenied(this, uSource, tr("That direct-message recipient is unavailable."));
				return;
			}
			permissionChannel = qhChannels.value(Mumble::ROOT_CHANNEL_ID);
			dbScope           = ::msdb::ChatThreadScope::Private;
			break;
		default:
			return;
	}

	if (scope == MumbleProto::ServerGlobal && !bPersistentGlobalChatEnabled) {
		MumbleProto::PermissionDenied denied;
		denied.set_permission(static_cast< unsigned int >(ChanACL::ViewTextMessageHistory));
		denied.set_channel_id(Mumble::ROOT_CHANNEL_ID);
		denied.set_session(uSource->uiSession);
		denied.set_type(MumbleProto::PermissionDenied_DenyType_Permission);
		denied.set_reason(u8(QStringLiteral("Global chat is disabled by this server.")));
		sendMessage(uSource, denied);
		return;
	}

	if (!permissionChannel) {
		return;
	}

	const ChatHistoryAccess access = resolveChatHistoryAccess(uSource, scope, scopeID, permissionChannel, &acCache);
	if (!access.allowed) {
		PERM_DENIED(uSource, permissionChannel, ChanACL::ViewTextMessageHistory);
		return;
	}

	const std::string scopeKey =
		scope == MumbleProto::Private ? privateChatScopeKey(userID.value(), scopeID) : chatScopeKey(scope, scopeID);
	std::optional<::msdb::DBChatThread > thread = m_dbWrapper.getChatThreadByScope(iServerNum, dbScope, scopeKey);
	if (!thread) {
		return;
	}

	const unsigned int requestedLastReadMessageID = msg.has_last_read_message_id() ? msg.last_read_message_id() : 0;
	if (requestedLastReadMessageID > 0) {
		const std::optional<::msdb::DBChatMessage > lastReadMessage =
			m_dbWrapper.getChatMessage(iServerNum, requestedLastReadMessageID);
		if (!lastReadMessage || lastReadMessage->threadID != thread->threadID
			|| !messageVisibleInWindow(*lastReadMessage, access.visibleAfter)) {
			sendPersistentChatTextDenied(this, uSource, tr("That message is no longer available."));
			return;
		}
	}

	::msdb::DBChatReadState readState(iServerNum, thread->threadID, userID.value());
	readState.lastReadMessageID = requestedLastReadMessageID;
	readState.updatedAt         = std::chrono::system_clock::now();

	m_dbWrapper.setChatReadState(readState);

	std::optional<::msdb::DBChatReadState > persistedReadState =
		m_dbWrapper.getChatReadState(iServerNum, thread->threadID, userID.value());
	if (persistedReadState) {
		sendMessage(uSource, protoReadStateFromDB(*persistedReadState, scope, scopeID));
	}
}

void Server::scheduleStonksValuationRefresh(bool fullHistory) {
	if (!bStonksEnabled || !bStonksAutoValuationEnabled || !qnamNetwork) {
		return;
	}

	m_stonksValuationFullHistoryQueued = m_stonksValuationFullHistoryQueued || fullHistory;
	if (m_stonksValuationRefreshInFlight) {
		m_stonksValuationRefreshQueued = true;
		m_stonksValuationStatus = fullHistory ? tr("Historical valuation backfill queued.")
											 : tr("Portfolio valuation refresh queued.");
		return;
	}
	if (m_stonksValuationRefreshQueued) {
		return;
	}

	m_stonksValuationRefreshQueued = true;
	m_stonksValuationStatus = fullHistory ? tr("Historical valuation backfill queued.")
										 : tr("Portfolio valuation refresh queued.");
	QTimer::singleShot(0, this, [this]() { runStonksValuationRefresh(); });
}

void Server::runStonksValuationRefresh() {
	if (!m_stonksValuationRefreshQueued || m_stonksValuationRefreshInFlight) {
		return;
	}
	m_stonksValuationRefreshQueued = false;
	if (!bStonksEnabled || !bStonksAutoValuationEnabled || !qnamNetwork) {
		m_stonksValuationFullHistoryQueued = false;
		return;
	}

	struct UserWork {
		unsigned int userID = 0;
		std::vector< Mumble::Stonks::PortfolioRevision > revisions;
	};
	struct RefreshContext {
		bool fullHistory = false;
		qint64 now = 0;
		qint64 earliestAt = 0;
		std::vector< UserWork > users;
		QStringList symbols;
		QHash< QString, Mumble::Stonks::QuoteSeries > quotes;
		int pending = 0;
		qsizetype nextSymbol = 0;
		int failures = 0;
	};

	const auto context       = std::make_shared< RefreshContext >();
	context->fullHistory     = m_stonksValuationFullHistoryQueued;
	context->now             = QDateTime::currentSecsSinceEpoch();
	context->earliestAt      = context->now
		- static_cast< qint64 >(context->fullHistory ? uiStonksValuationHistoryDays : 5U) * 24 * 60 * 60;
	m_stonksValuationFullHistoryQueued = false;
	m_stonksValuationRefreshInFlight   = true;
	m_stonksValuationStatus = context->fullHistory ? tr("Backfilling portfolio history…")
											 : tr("Updating portfolio values…");

	try {
		QSet< QString > symbols;
		std::vector< ::msdb::DBStonksValuation > submittedValuations;
		for (const ::msdb::DBStonksSnapshot &latest : m_dbWrapper.getLatestStonksSnapshotsByUser(iServerNum)) {
			std::vector< ::msdb::DBStonksSnapshot > snapshots =
				m_dbWrapper.getStonksSnapshotsForUser(iServerNum, latest.userID, 1000);
			std::reverse(snapshots.begin(), snapshots.end());
			std::size_t firstRelevant = 0;
			for (std::size_t i = 0; i < snapshots.size(); ++i) {
				const qint64 createdAt = static_cast< qint64 >(::msdb::toEpochSeconds(snapshots[i].createdAt));
				if (createdAt <= context->earliestAt) {
					firstRelevant = i;
				} else {
					break;
				}
			}

			UserWork work;
			work.userID = latest.userID;
			for (std::size_t i = firstRelevant; i < snapshots.size(); ++i) {
				const ::msdb::DBStonksSnapshot &snapshot = snapshots[i];
				const std::vector< ::msdb::DBStonksSnapshotPosition > positions =
					m_dbWrapper.getStonksSnapshotPositions(iServerNum, snapshot.snapshotID);
				Mumble::Stonks::PortfolioRevision revision;
				revision.revisionID  = snapshot.snapshotID;
				revision.userID      = snapshot.userID;
				revision.effectiveAt = static_cast< qint64 >(::msdb::toEpochSeconds(snapshot.createdAt));
				revision.currency    = u8(snapshot.currency);
				for (const ::msdb::DBStonksSnapshotPosition &position : positions) {
					const QString symbol = Mumble::Finance::normalizeTickerSymbol(
						u8(position.providerSymbol.empty() ? position.symbol : position.providerSymbol));
					if (symbol.isEmpty() || !std::isfinite(position.quantity) || position.quantity <= 0.0) {
						continue;
					}
					revision.positions.push_back({ symbol, u8(position.currency), position.quantity });
					symbols.insert(symbol);
				}
				work.revisions.push_back(std::move(revision));

				// Existing installations already have trustworthy values at each portfolio edit.
				// Seed those exact points during startup backfill; the upsert is idempotent.
				if (context->fullHistory && !positions.empty() && snapshot.totalValue > 0.0
					&& std::isfinite(snapshot.totalValue)) {
					::msdb::DBStonksValuation submitted(iServerNum, snapshot.userID, snapshot.snapshotID);
					submitted.valuedAt        = snapshot.createdAt;
					submitted.totalValue      = snapshot.totalValue;
					submitted.currency        = snapshot.currency;
					submitted.source          = "submitted";
					submitted.pricedPositions = static_cast< unsigned int >(positions.size());
					submitted.totalPositions  = static_cast< unsigned int >(positions.size());
					submitted.estimated        = false;
					submittedValuations.push_back(std::move(submitted));
				}
			}
			if (!work.revisions.empty()) {
				context->users.push_back(std::move(work));
			}
		}
		context->symbols = symbols.values();
		context->symbols.sort(Qt::CaseInsensitive);
		m_dbWrapper.setStonksValuations(submittedValuations);
	} catch (const std::exception &error) {
		m_stonksValuationRefreshInFlight = false;
		m_stonksValuationStatus = tr("Valuation refresh could not read the ledger: %1")
										 .arg(QString::fromUtf8(error.what()));
		log(QStringLiteral("Stonks valuation refresh failed before quote fetch: %1")
				.arg(QString::fromUtf8(error.what())));
		if (m_stonksValuationRefreshQueued) {
			QTimer::singleShot(0, this, [this]() { runStonksValuationRefresh(); });
		}
		return;
	}

	constexpr qsizetype maxSymbolsPerRefresh = 128;
	if (context->symbols.size() > maxSymbolsPerRefresh) {
		context->failures += static_cast< int >(context->symbols.size() - maxSymbolsPerRefresh);
		context->symbols = context->symbols.mid(0, maxSymbolsPerRefresh);
	}

	const auto finish = std::make_shared< std::function< void() > >();
	*finish = [this, context]() {
		unsigned int storedPoints = 0;
		try {
			std::vector< Mumble::Stonks::QuoteSeries > quoteSeries;
			quoteSeries.reserve(static_cast< std::size_t >(context->quotes.size()));
			for (auto it = context->quotes.cbegin(); it != context->quotes.cend(); ++it) {
				quoteSeries.push_back(it.value());
			}

			const qint64 bucketSeconds = context->fullHistory
				? 24 * 60 * 60
				: static_cast< qint64 >(uiStonksValuationIntervalMinutes) * 60;
			// Persist the completed bucket so retries have the same primary key and no point is future-dated.
			const qint64 latestBucketEnd = (context->now / bucketSeconds) * bucketSeconds - 1;
			const QString source = context->fullHistory ? QStringLiteral("historical")
												 : QStringLiteral("automatic");
			std::vector< ::msdb::DBStonksValuation > valuations;
			for (const UserWork &work : context->users) {
				for (const Mumble::Stonks::ValuationSample &sample : Mumble::Stonks::buildValuationTimeline(
						 work.revisions, quoteSeries, context->earliestAt, latestBucketEnd, bucketSeconds,
						 4 * 24 * 60 * 60, source)) {
					::msdb::DBStonksValuation valuation(iServerNum, work.userID, sample.revisionID);
					valuation.valuedAt =
						std::chrono::system_clock::time_point(std::chrono::seconds(sample.valuedAt));
					valuation.totalValue      = sample.totalValue;
					valuation.currency        = u8(sample.currency);
					valuation.source          = u8(sample.source);
					valuation.pricedPositions = sample.pricedPositions;
					valuation.totalPositions  = sample.totalPositions;
					valuation.estimated        = true;
					valuations.push_back(std::move(valuation));
				}
			}
			m_dbWrapper.setStonksValuations(valuations);
			storedPoints = static_cast< unsigned int >(valuations.size());

			const auto retentionCutoff = std::chrono::system_clock::time_point(
				std::chrono::seconds(context->now
					- static_cast< qint64 >(uiStonksValuationHistoryDays) * 24 * 60 * 60));
			m_dbWrapper.pruneStonksValuations(iServerNum, retentionCutoff);
			m_stonksValuationLastRunAt = context->now;
			if (context->fullHistory) {
				m_stonksValuationLastBackfillAt = context->earliestAt;
			}
			m_stonksValuationStatus = context->symbols.isEmpty()
				? tr("Automatic history is ready; no active ticker positions need pricing.")
				: tr("Stored %1 valuation points from %2 tickers%3.")
					  .arg(storedPoints)
					  .arg(context->quotes.size())
					  .arg(context->failures > 0 ? tr("; %1 unavailable").arg(context->failures) : QString());
			log(QStringLiteral("Stonks valuation refresh stored %1 points from %2 symbols (%3 unavailable)")
					.arg(storedPoints)
					.arg(context->quotes.size())
					.arg(context->failures));
		} catch (const std::exception &error) {
			m_stonksValuationStatus = tr("Valuation refresh failed while storing data: %1")
										 .arg(QString::fromUtf8(error.what()));
			log(QStringLiteral("Stonks valuation persistence failed: %1").arg(QString::fromUtf8(error.what())));
		}

		m_stonksValuationRefreshInFlight = false;
		broadcastStonksStates();
		if (m_stonksValuationRefreshQueued) {
			QTimer::singleShot(0, this, [this]() { runStonksValuationRefresh(); });
		}
	};

	if (context->symbols.isEmpty()) {
		(*finish)();
		return;
	}

	context->pending = static_cast< int >(context->symbols.size());
	const auto fetchSymbol = std::make_shared< std::function< void(const QString &, int) > >();
	const std::weak_ptr< std::function< void(const QString &, int) > > weakFetchSymbol = fetchSymbol;
	const auto startNextSymbol = std::make_shared< std::function< void() > >();
	*startNextSymbol = [context, weakFetchSymbol]() {
		if (context->nextSymbol >= context->symbols.size()) {
			return;
		}
		const QString symbol = context->symbols.at(context->nextSymbol++);
		if (const auto fetch = weakFetchSymbol.lock()) {
			(*fetch)(symbol, 0);
		}
	};
	const auto completeSymbol = [context, finish, startNextSymbol]() {
		--context->pending;
		if (context->pending == 0) {
			(*finish)();
		} else {
			(*startNextSymbol)();
		}
	};
	*fetchSymbol = [this, context, completeSymbol, weakFetchSymbol](const QString &originalSymbol,
															 int candidateIndex) {
		const QList< QString > candidates = Mumble::Finance::yahooFinanceSymbolCandidates(originalSymbol);
		if (candidateIndex < 0 || candidateIndex >= candidates.size()) {
			++context->failures;
			completeSymbol();
			return;
		}

		const QString candidate = candidates.at(candidateIndex);
		const QString range = context->fullHistory
			? (uiStonksValuationHistoryDays <= 366 ? QStringLiteral("1y") : QStringLiteral("2y"))
			: QStringLiteral("5d");
		const QString interval = context->fullHistory ? QStringLiteral("1d") : QStringLiteral("1h");
		const QUrl url = Mumble::Finance::yahooFinanceChartUrl(candidate, range, interval);
		if (!url.isValid() || url.scheme() != QLatin1String("https")
			|| url.host() != QLatin1String("query1.finance.yahoo.com")) {
			if (const auto fetch = weakFetchSymbol.lock()) {
				(*fetch)(originalSymbol, candidateIndex + 1);
			} else {
				++context->failures;
				completeSymbol();
			}
			return;
		}

		QNetworkRequest request(url);
		prepareChatPreviewRequest(request);
		request.setRawHeader(QByteArrayLiteral("Accept"), QByteArrayLiteral("application/json"));
		request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
		request.setTransferTimeout(12000);
		const auto activeFetch = weakFetchSymbol.lock();
		if (!activeFetch) {
			++context->failures;
			completeSymbol();
			return;
		}
		QNetworkReply *reply = qnamNetwork->get(request);
		reply->setReadBufferSize(4 * 1024 * 1024);
		connect(reply, &QNetworkReply::finished, this,
				[reply, originalSymbol, candidateIndex, context, completeSymbol, activeFetch]() {
					const QVariant redirectTarget = reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
					const QByteArray bytes = reply->readAll();
					const bool success = reply->error() == QNetworkReply::NoError && !redirectTarget.isValid()
						&& bytes.size() <= 4 * 1024 * 1024;
					reply->deleteLater();

					QString parseError;
					const std::optional< Mumble::Finance::YahooChartQuote > quote =
						success ? Mumble::Finance::parseYahooChartQuote(bytes, &parseError) : std::nullopt;
					if (!quote || quote->points.isEmpty()) {
						(*activeFetch)(originalSymbol, candidateIndex + 1);
						return;
					}

					Mumble::Stonks::QuoteSeries series;
					series.symbol   = originalSymbol;
					series.currency = quote->currency.trimmed().toUpper();
					for (const Mumble::Finance::YahooChartQuote::Point &point : quote->points) {
						series.points.push_back({ point.timestamp, point.close });
					}
					if (quote->hasRegularMarketPrice && quote->regularMarketTime > 0
						&& std::isfinite(quote->regularMarketPrice) && quote->regularMarketPrice > 0.0) {
						series.points.push_back({ quote->regularMarketTime, quote->regularMarketPrice });
					}
					context->quotes.insert(originalSymbol, std::move(series));
					completeSymbol();
				});
	};

	constexpr int maxConcurrentQuoteRequests = 8;
	const int initialRequests = std::min(maxConcurrentQuoteRequests, context->pending);
	for (int i = 0; i < initialRequests; ++i) {
		(*startNextSymbol)();
	}
}

void Server::broadcastStonksStates() {
	QMutexLocker locker(&qmCache);
	for (ServerUser *user : qhUsers) {
		if (!user || user->sState != ServerUser::Authenticated
			|| !clientSupportsForkFeature(user, MumbleProto::ForkFeatureStonksLedger)) {
			continue;
		}
		ChanACL::ACLCache cache;
		if (hasStonksAccess(user, &cache)) {
			const QString period = m_stonksSelectedPeriodBySession.value(user->uiSession, QStringLiteral("30d"));
			const std::optional< unsigned int > requestedUserID = m_stonksSelectedUserBySession.contains(user->uiSession)
				? std::optional< unsigned int >(m_stonksSelectedUserBySession.value(user->uiSession))
				: std::nullopt;
			MumbleProto::StonksState state =
				buildStonksState(this, user, period, cache, QString(), QString(), requestedUserID);
			if (state.has_selected_user_id()) {
				m_stonksSelectedUserBySession.insert(user->uiSession, state.selected_user_id());
			} else {
				m_stonksSelectedUserBySession.remove(user->uiSession);
			}
			sendMessage(user, state);
		}
	}
}

void Server::msgStonksRequest(ServerUser *uSource, MumbleProto::StonksRequest &msg) {
	ZoneScoped;

	MSG_SETUP(ServerUser::Authenticated);
	QMutexLocker qml(&qmCache);

	// Building leaderboard history can touch many users and valuation rows. Keep
	// repeated read requests under the same per-session throttle as mutations.
	RATELIMIT(uSource);

	const QString period = normalizedStonksLedgerPeriod(msg.has_period() ? u8(msg.period()) : QString());
	if (!clientSupportsForkFeature(uSource, MumbleProto::ForkFeatureStonksLedger)) {
		MumbleProto::StonksState state;
		state.set_supported(false);
		state.set_enabled(false);
		state.set_selected_period(u8(period));
		state.set_error("This client did not advertise Stonks ledger support.");
		sendMessage(uSource, state);
		return;
	}
	if (!hasStonksAccess(uSource, &acCache)) {
		sendMessage(uSource, buildStonksState(this, uSource, period, acCache));
		return;
	}

	const std::optional< unsigned int > requestedUserID =
		msg.has_user_id() ? std::optional< unsigned int >(msg.user_id()) : std::nullopt;
	MumbleProto::StonksState state =
		buildStonksState(this, uSource, period, acCache, QString(), QString(), requestedUserID);
	m_stonksSelectedPeriodBySession.insert(uSource->uiSession, period);
	if (state.has_selected_user_id()) {
		m_stonksSelectedUserBySession.insert(uSource->uiSession, state.selected_user_id());
	} else {
		m_stonksSelectedUserBySession.remove(uSource->uiSession);
	}
	sendMessage(uSource, state);
}

void Server::msgStonksAction(ServerUser *uSource, MumbleProto::StonksAction &msg) {
	ZoneScoped;

	MSG_SETUP(ServerUser::Authenticated);
	QMutexLocker qml(&qmCache);

	// SECURITY: ledger mutations write to the database and broadcast state; throttle to limit spam.
	RATELIMIT(uSource);

	const QString period = normalizedStonksLedgerPeriod(msg.has_period() ? u8(msg.period()) : QString());
	const auto sendState = [&](const QString &status = QString(), const QString &error = QString(),
							   std::optional< unsigned int > requestedUserID = std::nullopt) {
		if (!requestedUserID && m_stonksSelectedUserBySession.contains(uSource->uiSession)) {
			requestedUserID = m_stonksSelectedUserBySession.value(uSource->uiSession);
		}
		MumbleProto::StonksState state =
			buildStonksState(this, uSource, period, acCache, status, error, requestedUserID);
		m_stonksSelectedPeriodBySession.insert(uSource->uiSession, period);
		if (state.has_selected_user_id()) {
			m_stonksSelectedUserBySession.insert(uSource->uiSession, state.selected_user_id());
		} else {
			m_stonksSelectedUserBySession.remove(uSource->uiSession);
		}
		sendMessage(uSource, state);
	};

	if (!clientSupportsForkFeature(uSource, MumbleProto::ForkFeatureStonksLedger)) {
		MumbleProto::StonksState state;
		state.set_supported(false);
		state.set_enabled(false);
		state.set_selected_period(u8(period));
		state.set_error("This client did not advertise Stonks ledger support.");
		sendMessage(uSource, state);
		return;
	}

	Channel *rootChannel = qhChannels.value(Mumble::ROOT_CHANNEL_ID);
	if (!hasStonksAccess(uSource, &acCache)) {
		if (rootChannel) {
			PERM_DENIED(uSource, rootChannel, ChanACL::UseStonks);
		}
		sendState(QString(), tr("Use Stonks permission is required on the root channel."));
		return;
	}
	const bool canAdmin  = rootChannel && ChanACL::hasPermission(uSource, rootChannel, ChanACL::Write, &acCache);
	const MumbleProto::StonksActionKind action =
		msg.has_action() ? msg.action() : MumbleProto::StonksActionSubmitSnapshot;

	if (!bStonksEnabled && action != MumbleProto::StonksActionConfigure) {
		sendState(QString(), tr("Stonks ledger is disabled on this server."));
		return;
	}

	const auto selfUserID = persistedUserID(uSource);
	const bool registered = selfUserID && m_dbWrapper.registeredUserExists(iServerNum, *selfUserID);
	const auto requireRegistered = [&]() -> bool {
		if (registered) {
			return true;
		}
		sendState(QString(), tr("Register your user before using the Stonks ledger."));
		return false;
	};
	const auto requireStonksAdmin = [&]() -> bool {
		if (canAdmin) {
			return true;
		}
		if (rootChannel) {
			PERM_DENIED(uSource, rootChannel, ChanACL::Write);
		}
		sendState(QString(), tr("Root Write permission is required to manage another user's Stonks ledger."));
		return false;
	};
	const auto resolvePortfolioTarget = [&]() -> std::optional< unsigned int > {
		std::optional< unsigned int > targetUserID =
			msg.has_target_user_id() ? std::optional< unsigned int >(msg.target_user_id()) : std::nullopt;
		if (!targetUserID && msg.has_target_name()) {
			const int resolvedUserID = getRegisteredUserID(u8(msg.target_name()));
			if (resolvedUserID >= 0) {
				targetUserID = static_cast< unsigned int >(resolvedUserID);
			}
		}
		if (!targetUserID) {
			if (!requireRegistered()) {
				return std::nullopt;
			}
			targetUserID = *selfUserID;
		}
		if (!m_dbWrapper.registeredUserExists(iServerNum, *targetUserID)) {
			sendState(QString(), tr("That registered user could not be found."));
			return std::nullopt;
		}

		const unsigned int resolvedTargetUserID = *targetUserID;
		const bool targetsSelf                  = registered && selfUserID && resolvedTargetUserID == *selfUserID;
		if (!targetsSelf && !requireStonksAdmin()) {
			return std::nullopt;
		}

		return resolvedTargetUserID;
	};
	const auto broadcastStonksAnnouncement = [&](const QString &announcement) {
		if (!bStonksSocialAnnouncementsEnabled || announcement.trimmed().isEmpty()) {
			return;
		}

		std::vector<::msdb::DBTextChannel > textChannels = m_dbWrapper.getTextChannels(iServerNum);
		const std::optional< unsigned int > stonksTextChannelID =
			resolvedStonksTextChannelID(uiStonksTextChannelID, textChannels);
		if (!stonksTextChannelID) {
			return;
		}

		const auto textChannel = m_dbWrapper.getTextChannel(iServerNum, *stonksTextChannelID);
		Channel *permissionChannel = textChannel ? qhChannels.value(textChannel->aclChannelID) : nullptr;
		if (!permissionChannel) {
			return;
		}

		persistAndBroadcastServerChatMessage(announcement, MumbleProto::TextChannel, *stonksTextChannelID,
											 permissionChannel, ::msdb::ChatThreadScope::TextChannel,
											 QStringLiteral("Stonks"));
	};

	switch (action) {
		case MumbleProto::StonksActionSubmitSnapshot: {
			const std::optional< unsigned int > targetUserID = resolvePortfolioTarget();
			if (!targetUserID) {
				return;
			}
			if (!msg.has_snapshot()) {
				sendState(QString(), tr("Ledger update payload is missing."), targetUserID);
				return;
			}

			std::vector< ::msdb::DBStonksSnapshotPosition > previousPositions;
			if (const std::optional< ::msdb::DBStonksSnapshot > latestSnapshot =
					m_dbWrapper.getLatestStonksSnapshotForUser(iServerNum, *targetUserID)) {
				previousPositions = m_dbWrapper.getStonksSnapshotPositions(iServerNum, latestSnapshot->snapshotID);
			}

			QString error;
			std::optional< ValidatedStonksSnapshot > validated =
				validatedStonksSnapshotFromProto(iServerNum, *targetUserID, msg.snapshot(), &error);
			if (!validated) {
				sendState(QString(), error, targetUserID);
				return;
			}

			const ::msdb::DBStonksSnapshot storedSnapshot =
				m_dbWrapper.addStonksSnapshot(validated->snapshot, validated->positions);
			scheduleStonksValuationRefresh(false);

			broadcastStonksAnnouncement(
				stonksSocialAnnouncement(this, storedSnapshot, validated->positions, previousPositions));

			const QString targetName = stonksRegisteredDisplayName(this, *targetUserID);
			const bool targetsSelf = registered && selfUserID && *targetUserID == *selfUserID;
			sendState(targetsSelf ? tr("Ledger updated.") : tr("Ledger updated for %1.").arg(targetName),
					  QString(), targetUserID);
			return;
		}
		case MumbleProto::StonksActionClearPortfolio: {
			const std::optional< unsigned int > targetUserID = resolvePortfolioTarget();
			if (!targetUserID) {
				return;
			}

			QString currency = QStringLiteral("USD");
			if (const std::optional< ::msdb::DBStonksSnapshot > latest =
					m_dbWrapper.getLatestStonksSnapshotForUser(iServerNum, *targetUserID)) {
				currency = normalizedStonksCurrency(u8(latest->currency));
			}
			if (msg.has_snapshot() && msg.snapshot().has_currency()) {
				currency = normalizedStonksCurrency(u8(msg.snapshot().currency()));
			}

			::msdb::DBStonksSnapshot clearedSnapshot(iServerNum, 0, *targetUserID);
			clearedSnapshot.createdAt  = std::chrono::system_clock::now();
			clearedSnapshot.currency   = u8(currency);
			clearedSnapshot.totalValue = 0.0;
			clearedSnapshot.note       = u8(msg.has_snapshot() && msg.snapshot().has_note()
												? u8(msg.snapshot().note()).trimmed().left(512)
												: tr("Ledger cleared"));

			const ::msdb::DBStonksSnapshot storedSnapshot = m_dbWrapper.addStonksSnapshot(clearedSnapshot, {});
			scheduleStonksValuationRefresh(false);
			broadcastStonksAnnouncement(stonksClearAnnouncement(this, storedSnapshot));

			const QString targetName = stonksRegisteredDisplayName(this, *targetUserID);
			const bool targetsSelf = registered && selfUserID && *targetUserID == *selfUserID;
			sendState(targetsSelf ? tr("Ledger cleared.") : tr("Ledger cleared for %1.").arg(targetName),
					  QString(), targetUserID);
			return;
		}
		case MumbleProto::StonksActionDeleteSnapshot: {
			const unsigned int snapshotID =
				msg.has_snapshot_id() ? msg.snapshot_id()
									  : (msg.has_snapshot() && msg.snapshot().has_snapshot_id()
											 ? msg.snapshot().snapshot_id()
											 : 0u);
			if (snapshotID == 0) {
				sendState(QString(), tr("Ledger update payload is missing."));
				return;
			}

			const std::optional< ::msdb::DBStonksSnapshot > snapshot =
				m_dbWrapper.getStonksSnapshot(iServerNum, snapshotID);
			if (!snapshot) {
				sendState(QString(), tr("That ledger update could not be found."));
				return;
			}

			const bool targetsSelf = registered && selfUserID && snapshot->userID == *selfUserID;
			if (!targetsSelf && !requireStonksAdmin()) {
				return;
			}
			if (msg.has_target_user_id() && msg.target_user_id() != snapshot->userID) {
				sendState(QString(), tr("Ledger update owner did not match the selected user."), snapshot->userID);
				return;
			}

			m_dbWrapper.removeStonksSnapshot(iServerNum, snapshotID);
			scheduleStonksValuationRefresh(true);

			const QString targetName = stonksRegisteredDisplayName(this, snapshot->userID);
			sendState(targetsSelf ? tr("Ledger update deleted.") : tr("Ledger update deleted for %1.").arg(targetName),
					  QString(), snapshot->userID);
			return;
		}
		case MumbleProto::StonksActionFollow:
		case MumbleProto::StonksActionUnfollow: {
			if (!requireRegistered()) {
				return;
			}

			std::optional< unsigned int > targetUserID =
				msg.has_target_user_id() ? std::optional< unsigned int >(msg.target_user_id()) : std::nullopt;
			if (!targetUserID && msg.has_target_name()) {
				const int resolvedUserID = getRegisteredUserID(u8(msg.target_name()));
				if (resolvedUserID >= 0) {
					targetUserID = static_cast< unsigned int >(resolvedUserID);
				}
			}
			if (!targetUserID || !m_dbWrapper.registeredUserExists(iServerNum, *targetUserID)) {
				sendState(QString(), tr("That registered user could not be found."));
				return;
			}
			const unsigned int targetRegisteredUserID = *targetUserID;
			if (targetRegisteredUserID == *selfUserID) {
				sendState(QString(), tr("You cannot follow yourself."));
				return;
			}

			if (action == MumbleProto::StonksActionFollow) {
				::msdb::DBStonksFollow follow(iServerNum, *selfUserID, targetRegisteredUserID);
				follow.createdAt = std::chrono::system_clock::now();
				m_dbWrapper.setStonksFollow(follow);
				sendState(tr("Following %1.").arg(stonksRegisteredDisplayName(this, targetRegisteredUserID)));
			} else {
				m_dbWrapper.removeStonksFollow(iServerNum, *selfUserID, targetRegisteredUserID);
				sendState(tr("Unfollowed %1.").arg(stonksRegisteredDisplayName(this, targetRegisteredUserID)));
			}
			return;
		}
		case MumbleProto::StonksActionSetTickerPin: {
			if (!requireRegistered()) {
				return;
			}
			if (!msg.has_pinned_ticker()) {
				sendState(QString(), tr("Ticker pin payload is missing."));
				return;
			}

			QString error;
			std::optional< ::msdb::DBStonksPinnedTicker > ticker =
				validatedStonksPinnedTickerFromProto(iServerNum, *selfUserID, msg.pinned_ticker(), &error);
			if (!ticker) {
				sendState(QString(), error);
				return;
			}

			const bool pinned = !msg.has_pinned() || msg.pinned();
			if (pinned) {
				m_dbWrapper.setStonksPinnedTicker(*ticker);
				sendState(tr("Pinned %1.").arg(u8(ticker->symbol)));
			} else {
				m_dbWrapper.removeStonksPinnedTicker(iServerNum, *selfUserID, ticker->symbol);
				sendState(tr("Unpinned %1.").arg(u8(ticker->symbol)));
			}
			return;
		}
		case MumbleProto::StonksActionSetFeedPreferences: {
			if (!requireRegistered()) {
				return;
			}
			if (!msg.has_feed_preferences()) {
				sendState(QString(), tr("Ticker feed preferences payload is missing."));
				return;
			}

			m_dbWrapper.setStonksFeedPreferences(
				stonksFeedPreferencesFromProto(iServerNum, *selfUserID, msg.feed_preferences()));
			sendState(tr("Ticker feed preferences saved."));
			return;
		}
		case MumbleProto::StonksActionConfigure: {
			if (!canAdmin) {
				if (rootChannel) {
					PERM_DENIED(uSource, rootChannel, ChanACL::Write);
				}
				sendState(QString(), tr("Root Write permission is required to change Stonks config."));
				return;
			}

			const auto applyConfig = [this](const char *key, const QString &value) {
				if (value.trimmed().isEmpty()) {
					m_dbWrapper.clearConfiguration(iServerNum, key);
				} else {
					m_dbWrapper.setConfiguration(iServerNum, key, u8(value));
				}
				setLiveConf(QLatin1String(key), value);
			};
			if (msg.has_enabled()) {
				applyConfig("stonks_enabled", msg.enabled() ? QLatin1String("true") : QLatin1String("false"));
			}
			if (msg.has_social_announcements_enabled()) {
				applyConfig("stonks_social_announcements_enabled",
							msg.social_announcements_enabled() ? QLatin1String("true") : QLatin1String("false"));
			}
			if (msg.has_automatic_valuation_enabled()) {
				applyConfig("stonks_auto_valuation_enabled",
							msg.automatic_valuation_enabled() ? QLatin1String("true") : QLatin1String("false"));
			}
			if (msg.has_valuation_interval_minutes()) {
				const unsigned int minutes = std::clamp(msg.valuation_interval_minutes(), 15U, 1440U);
				applyConfig("stonks_valuation_interval_minutes", QString::number(minutes));
			}
			if (msg.has_valuation_history_days()) {
				const unsigned int days = std::clamp(msg.valuation_history_days(), 31U, 730U);
				applyConfig("stonks_valuation_history_days", QString::number(days));
			}
			const bool textChannelChanged = msg.has_text_channel_id();
			if (textChannelChanged) {
				const unsigned int textChannelID = msg.text_channel_id();
				if (textChannelID > 0 && !m_dbWrapper.getTextChannel(iServerNum, textChannelID)) {
					sendState(QString(), tr("Selected Stonks text channel could not be found."));
					return;
				}
				applyConfig("stonks_text_channel_id", textChannelID > 0 ? QString::number(textChannelID) : QString());
			}

			sendState(tr("Stonks settings saved."));
			if (textChannelChanged) {
				// sendTextChannelSync takes qmCache itself. Release this handler's
				// lock before rebuilding every connected user's permission-filtered list.
				qml.unlock();
				broadcastTextChannelSync(this, qhUsers);
			}
			return;
		}
	}

	sendState(QString(), tr("Unsupported Stonks action."));
}

void Server::msgStonksState(ServerUser *, MumbleProto::StonksState &) {
}

bool Server::feedbackGitHubConfigured() const {
	return bFeedbackGitHubEnabled && !qsFeedbackGitHubToken.trimmed().isEmpty()
		   && isValidGitHubPathComponent(qsFeedbackGitHubOwner)
		   && isValidGitHubPathComponent(qsFeedbackGitHubRepo)
		   && feedbackGitHubIssueUrl(qsFeedbackGitHubAPIUrl, qsFeedbackGitHubOwner, qsFeedbackGitHubRepo).isValid();
}

void Server::sendFeedbackReportState(const unsigned int session, const QString &clientReportID,
									 const MumbleProto::FeedbackReportKind kind, const bool accepted,
									 const QString &issueUrl, const unsigned int issueNumber,
									 const QString &error) {
	ServerUser *user = qhUsers.value(session);
	if (!user || user->sState != ServerUser::Authenticated) {
		return;
	}

	MumbleProto::FeedbackReportState state;
	state.set_client_report_id(u8(clientReportID));
	state.set_kind(kind);
	state.set_accepted(accepted);
	state.set_created_at(static_cast< uint64_t >(QDateTime::currentSecsSinceEpoch()));
	if (!issueUrl.isEmpty()) {
		state.set_issue_url(u8(issueUrl));
	}
	if (issueNumber > 0) {
		state.set_issue_number(issueNumber);
	}
	if (!error.isEmpty()) {
		state.set_error(u8(error));
	}
	sendMessage(user, state);
}

void Server::submitFeedbackReportToGitHub(ServerUser *uSource, const MumbleProto::FeedbackReport &msg,
										  const QString &issueTitle, const QString &issueBody,
										  const QStringList &labels) {
	const unsigned int session = uSource->uiSession;
	const QString clientReportID =
		msg.has_client_report_id() ? u8(msg.client_report_id()) : QUuid::createUuid().toString(QUuid::WithoutBraces);
	const MumbleProto::FeedbackReportKind kind =
		msg.has_kind() ? msg.kind() : MumbleProto::FeedbackReportBug;

	const QUrl url = feedbackGitHubIssueUrl(qsFeedbackGitHubAPIUrl, qsFeedbackGitHubOwner, qsFeedbackGitHubRepo);
	if (!qnamNetwork || !url.isValid()) {
		sendFeedbackReportState(session, clientReportID, kind, false, QString(), 0,
								tr("Feedback submission is not configured on this server."));
		return;
	}

	QJsonObject payload;
	payload.insert(QStringLiteral("title"), issueTitle);
	payload.insert(QStringLiteral("body"), issueBody);
	QJsonArray labelArray;
	for (const QString &label : labels) {
		if (!label.trimmed().isEmpty()) {
			labelArray.append(label.trimmed());
		}
	}
	if (!labelArray.isEmpty()) {
		payload.insert(QStringLiteral("labels"), labelArray);
	}

	QNetworkRequest request(url);
	request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
	request.setRawHeader(QByteArrayLiteral("Accept"), QByteArrayLiteral("application/vnd.github+json"));
	request.setRawHeader(QByteArrayLiteral("Authorization"),
						 QByteArrayLiteral("Bearer ") + qsFeedbackGitHubToken.toUtf8());
	request.setRawHeader(QByteArrayLiteral("X-GitHub-Api-Version"), QByteArrayLiteral("2022-11-28"));
	request.setRawHeader(QByteArrayLiteral("User-Agent"),
						 QByteArrayLiteral("mumble-forked-in-app-feedback/") + Version::getRelease().toUtf8());
	request.setTransferTimeout(30000);

	QNetworkReply *reply = qnamNetwork->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
	connect(reply, &QNetworkReply::finished, this,
			[this, reply, session, clientReportID, kind]() {
				const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
				const QByteArray bytes = reply->readAll();
				const QJsonDocument doc = QJsonDocument::fromJson(bytes);
				reply->deleteLater();

				if (status == 201 && doc.isObject()) {
					const QJsonObject obj = doc.object();
					const QString issueUrl = obj.value(QStringLiteral("html_url")).toString();
					const unsigned int issueNumber =
						static_cast< unsigned int >(obj.value(QStringLiteral("number")).toInt());
					sendFeedbackReportState(session, clientReportID, kind, true, issueUrl, issueNumber, QString());
					return;
				}

				QString detail;
				if (doc.isObject()) {
					detail = doc.object().value(QStringLiteral("message")).toString();
				}
				if (detail.isEmpty() && reply->error() != QNetworkReply::NoError) {
					detail = reply->errorString();
				}
				const QString error = detail.isEmpty()
										  ? tr("GitHub issue creation failed (HTTP %1).").arg(status)
										  : tr("GitHub issue creation failed (HTTP %1): %2").arg(status).arg(detail);
				sendFeedbackReportState(session, clientReportID, kind, false, QString(), 0, error);
			});
}

void Server::msgFeedbackReport(ServerUser *uSource, MumbleProto::FeedbackReport &msg) {
	ZoneScoped;

	MSG_SETUP(ServerUser::Authenticated);

	const QString clientReportID =
		msg.has_client_report_id() && !u8(msg.client_report_id()).trimmed().isEmpty()
			? u8(msg.client_report_id()).trimmed().left(128)
			: QUuid::createUuid().toString(QUuid::WithoutBraces);
	MumbleProto::FeedbackReportKind kind = msg.has_kind() ? msg.kind() : MumbleProto::FeedbackReportBug;
	if (kind != MumbleProto::FeedbackReportBug && kind != MumbleProto::FeedbackReportSuggestion
		&& kind != MumbleProto::FeedbackReportSupport) {
		kind = MumbleProto::FeedbackReportBug;
	}
	const auto reject = [&](const QString &error) {
		sendFeedbackReportState(uSource->uiSession, clientReportID, kind, false, QString(), 0, error);
	};

	if (!clientSupportsForkFeature(uSource, MumbleProto::ForkFeatureInAppFeedback)) {
		reject(tr("This client did not advertise in-app feedback support."));
		return;
	}
	if (!feedbackGitHubConfigured()) {
		reject(tr("Feedback submission is not configured on this server."));
		return;
	}
#if GOOGLE_PROTOBUF_VERSION >= 3004000
	const uint64_t payloadBytes = msg.ByteSizeLong();
#else
	const uint64_t payloadBytes = static_cast< uint64_t >(msg.ByteSize());
#endif
	const uint64_t maxPayloadBytes =
		static_cast< uint64_t >(uiFeedbackMaxLogBytes) + static_cast< uint64_t >(uiFeedbackMaxBodyBytes) + 8192;
	if (payloadBytes > maxPayloadBytes) {
		reject(tr("Feedback report is too large."));
		return;
	}
	if (uSource->m_feedbackReportTimer.isValid()
		&& uSource->m_feedbackReportTimer.elapsed() < FEEDBACK_REPORT_RATE_LIMIT_MS) {
		const qint64 secondsLeft =
			(FEEDBACK_REPORT_RATE_LIMIT_MS - uSource->m_feedbackReportTimer.elapsed() + 999) / 1000;
		reject(tr("Please wait %1 seconds before submitting another report.").arg(secondsLeft));
		return;
	}

	const QString title       = msg.has_title() ? u8(msg.title()).trimmed() : QString();
	const QString description = msg.has_description() ? u8(msg.description()).trimmed() : QString();
	if (title.isEmpty() || description.isEmpty()) {
		reject(tr("Title and description are required."));
		return;
	}

	Mumble::Feedback::ReportFields fields;
	fields.kind                    = kind;
	fields.title                   = Mumble::Feedback::truncateUtf8Bytes(title, 240, QString());
	fields.description             = description;
	fields.reproductionSteps       = msg.has_reproduction_steps() ? u8(msg.reproduction_steps()) : QString();
	fields.diagnosticsIncluded     = msg.has_diagnostics_included() && msg.diagnostics_included();
	fields.diagnostics             = fields.diagnosticsIncluded && msg.has_diagnostics()
										 ? Mumble::Feedback::redactedDiagnostics(u8(msg.diagnostics()), uiFeedbackMaxLogBytes)
										 : QString();
	fields.clientRelease           = msg.has_client_release() ? u8(msg.client_release()) : QString();
	fields.clientArch              = msg.has_client_arch() ? u8(msg.client_arch()) : QString();
	fields.clientOS                = msg.has_client_os() ? u8(msg.client_os()) : QString();
	fields.clientQt                = msg.has_client_qt() ? u8(msg.client_qt()) : QString();
	fields.serverCapabilitySummary = tr("Server accepted via Murmur in-app feedback relay.");
	fields.pastedEvidence          = msg.has_pasted_evidence()
										 ? Mumble::Feedback::truncateUtf8Bytes(
											   u8(msg.pasted_evidence()), uiFeedbackMaxBodyBytes,
											   QStringLiteral("[pasted evidence truncated]"))
										 : QString();

	const QString body = Mumble::Feedback::issueBody(fields, uiFeedbackMaxBodyBytes, uiFeedbackMaxLogBytes);
	uSource->m_feedbackReportTimer.restart();
	submitFeedbackReportToGitHub(uSource, msg, Mumble::Feedback::issueTitle(fields), body,
								 feedbackLabelsForKind(this, kind));
}

void Server::msgFeedbackReportState(ServerUser *, MumbleProto::FeedbackReportState &) {
}

void Server::msgServerLogState(ServerUser *, MumbleProto::ServerLogState &) {
	// ServerLogState is server-to-client only.
}

void Server::msgChatAssetUploadInit(ServerUser *uSource, MumbleProto::ChatAssetUploadInit &msg) {
	ZoneScoped;

	MSG_SETUP(ServerUser::Authenticated);
	QMutexLocker qml(&qmCache);

	// SECURITY: throttle how fast a client can open new upload sessions to bound asset-upload spam.
	// Keep this separate from ordinary messages: a valid upload must retain the normal chat token
	// needed for the ChatSend that references the committed asset.
	if (uSource->m_chatAttachmentUploadBucket.ratelimit(1)) {
		sendChatAssetState(this, uSource, 0, MumbleProto::ChatAssetTransferStateRejected,
						   QStringLiteral("Too many attachment uploads. Try again shortly."));
		return;
	}

	if (!clientSupportsChatFeature(uSource, MumbleProto::ChatFeatureAttachments)) {
		sendPersistentChatUnsupported(uSource);
		return;
	}

	const QString mime = normalizedMime(msg.has_mime() ? u8(msg.mime()) : QString());
	if (mime.isEmpty() || !msg.has_byte_size() || msg.byte_size() == 0) {
		sendChatAssetState(this, uSource, 0, MumbleProto::ChatAssetTransferStateRejected,
						   QStringLiteral("Missing asset metadata."));
		return;
	}
	if (uiChatAssetMaxBytes > 0 && msg.byte_size() > uiChatAssetMaxBytes) {
		sendChatAssetState(this, uSource, 0, MumbleProto::ChatAssetTransferStateRejected,
						   QStringLiteral("Asset exceeds the configured size limit."));
		return;
	}
	quint64 pendingBytes = 0;
	unsigned int ownerPendingUploads = 0;
	for (const Server::PendingChatAssetUpload &pending : std::as_const(qhPendingChatAssetUploads)) {
		if (pendingBytes > std::numeric_limits< quint64 >::max() - pending.expectedByteSize) {
			pendingBytes = std::numeric_limits< quint64 >::max();
		} else {
			pendingBytes += pending.expectedByteSize;
		}
		if (pending.owner == uSource) ++ownerPendingUploads;
	}
	if (ownerPendingUploads >= 2) {
		sendChatAssetState(this, uSource, 0, MumbleProto::ChatAssetTransferStateRejected,
						   QStringLiteral("Too many attachment uploads are already pending."));
		return;
	}
	if (uiChatAssetTotalQuotaBytes > 0
		&& (pendingBytes > uiChatAssetTotalQuotaBytes
			|| msg.byte_size() > uiChatAssetTotalQuotaBytes - pendingBytes
			|| chatAssetStoredBytes() > uiChatAssetTotalQuotaBytes - pendingBytes - msg.byte_size())) {
		sendChatAssetState(this, uSource, 0, MumbleProto::ChatAssetTransferStateRejected,
						   QStringLiteral("Server chat asset quota exceeded."));
		return;
	}

	msdb::ChatAssetKind kind = dbAssetKindFromProto(msg.has_kind() ? msg.kind() : MumbleProto::ChatAssetKindUnknown);
	if (kind == msdb::ChatAssetKind::Unknown) {
		kind = inferredAssetKind(mime);
	}
	if (!isAllowedChatAssetMime(kind, mime)) {
		sendChatAssetState(this, uSource, 0, MumbleProto::ChatAssetTransferStateRejected,
						   QStringLiteral("Asset MIME type is not allowed by server policy."));
		return;
	}

	const QString declaredHash = msg.has_sha256() ? u8(msg.sha256()).trimmed().toLower() : QString();
	if (!declaredHash.isEmpty() && !isValidSha256Hex(declaredHash)) {
		sendChatAssetState(this, uSource, 0, MumbleProto::ChatAssetTransferStateRejected,
						   QStringLiteral("Invalid SHA-256 checksum."));
		return;
	}

	QString storageError;
	if (!ensureChatAssetStorageReady(&storageError)) {
		sendChatAssetState(this, uSource, 0, MumbleProto::ChatAssetTransferStateRejected, storageError);
		return;
	}

	PendingChatAssetUpload upload;
	upload.uploadID         = randomUploadID(qhPendingChatAssetUploads);
	upload.owner            = uSource;
	upload.ownerSession     = uSource->uiSession;
	upload.ownerUserID      = persistedUserID(uSource);
	upload.filename         = msg.has_filename() ? u8(msg.filename()) : QString();
	upload.mime             = mime;
	upload.sha256           = declaredHash;
	upload.kind             = kind;
	upload.requestInline    = msg.has_request_inline() && msg.request_inline();
	upload.expectedByteSize = msg.byte_size();
	upload.tempFilePath =
		QDir(chatAssetIncomingRootPath())
			.absoluteFilePath(QStringLiteral("%1-%2.part").arg(upload.ownerSession).arg(upload.uploadID));

	QFile tempFile(upload.tempFilePath);
	if (tempFile.exists() && !tempFile.remove()) {
		sendChatAssetState(this, uSource, 0, MumbleProto::ChatAssetTransferStateRejected,
						   QStringLiteral("Failed to reset temporary upload state."));
		return;
	}
	if (!tempFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		sendChatAssetState(this, uSource, 0, MumbleProto::ChatAssetTransferStateRejected,
						   QStringLiteral("Failed to open temporary upload file."));
		return;
	}
	tempFile.close();

	qhPendingChatAssetUploads.insert(upload.uploadID, upload);
	sendChatAssetState(this, uSource, upload.uploadID, MumbleProto::ChatAssetTransferStateAccepted, QString(),
					   upload.expectedByteSize);
}

void Server::msgChatAssetUploadChunk(ServerUser *uSource, MumbleProto::ChatAssetUploadChunk &msg) {
	ZoneScoped;

	MSG_SETUP(ServerUser::Authenticated);
	QMutexLocker qml(&qmCache);

	if (!clientSupportsChatFeature(uSource, MumbleProto::ChatFeatureAttachments)) {
		sendPersistentChatUnsupported(uSource);
		return;
	}

	if (!msg.has_upload_id() || !qhPendingChatAssetUploads.contains(msg.upload_id())) {
		return;
	}

	PendingChatAssetUpload &upload = qhPendingChatAssetUploads[msg.upload_id()];
	if (upload.owner != uSource) {
		return;
	}
	if (!msg.has_offset() || msg.offset() != upload.receivedByteSize) {
		sendChatAssetState(this, uSource, upload.uploadID, MumbleProto::ChatAssetTransferStateRejected,
						   QStringLiteral("Upload chunks must be sent sequentially."));
		QFile::remove(upload.tempFilePath);
		qhPendingChatAssetUploads.remove(upload.uploadID);
		return;
	}

	const QByteArray data = blob(msg.data());
	if (data.isEmpty()) {
		return;
	}
	if (data.size() > 256 * 1024) {
		sendChatAssetState(this, uSource, upload.uploadID, MumbleProto::ChatAssetTransferStateRejected,
						   QStringLiteral("Upload chunk exceeds the maximum allowed size."));
		QFile::remove(upload.tempFilePath);
		qhPendingChatAssetUploads.remove(upload.uploadID);
		return;
	}
	if (upload.receivedByteSize + static_cast< quint64 >(data.size()) > upload.expectedByteSize) {
		sendChatAssetState(this, uSource, upload.uploadID, MumbleProto::ChatAssetTransferStateRejected,
						   QStringLiteral("Upload exceeds declared asset size."));
		QFile::remove(upload.tempFilePath);
		qhPendingChatAssetUploads.remove(upload.uploadID);
		return;
	}

	QFile tempFile(upload.tempFilePath);
	if (!tempFile.open(QIODevice::WriteOnly | QIODevice::Append)) {
		sendChatAssetState(this, uSource, upload.uploadID, MumbleProto::ChatAssetTransferStateRejected,
						   QStringLiteral("Failed to append upload chunk to temporary file."));
		QFile::remove(upload.tempFilePath);
		qhPendingChatAssetUploads.remove(upload.uploadID);
		return;
	}
	if (static_cast< quint64 >(tempFile.size()) != upload.receivedByteSize) {
		tempFile.close();
		sendChatAssetState(this, uSource, upload.uploadID, MumbleProto::ChatAssetTransferStateRejected,
						   QStringLiteral("Temporary upload file is out of sync."));
		QFile::remove(upload.tempFilePath);
		qhPendingChatAssetUploads.remove(upload.uploadID);
		return;
	}
	if (tempFile.write(data) != data.size()) {
		tempFile.close();
		sendChatAssetState(this, uSource, upload.uploadID, MumbleProto::ChatAssetTransferStateRejected,
						   QStringLiteral("Failed to write upload chunk."));
		QFile::remove(upload.tempFilePath);
		qhPendingChatAssetUploads.remove(upload.uploadID);
		return;
	}
	tempFile.close();

	upload.receivedByteSize += static_cast< quint64 >(data.size());
	upload.finalChunkReceived = upload.finalChunkReceived || (msg.has_final_chunk() && msg.final_chunk());
}

void Server::msgChatAssetUploadCommit(ServerUser *uSource, MumbleProto::ChatAssetUploadCommit &msg) {
	ZoneScoped;

	MSG_SETUP(ServerUser::Authenticated);
	QMutexLocker qml(&qmCache);

	// Upload initialization is rate-limited and each accepted initialization can be committed only once.
	// Charging the shared message bucket again here would make a valid multi-attachment send exhaust the
	// bucket before its final ChatSend. The per-owner pending cap and one-shot take() below bound commit work.

	if (!clientSupportsChatFeature(uSource, MumbleProto::ChatFeatureAttachments)) {
		sendPersistentChatUnsupported(uSource);
		return;
	}

	if (!msg.has_upload_id() || !qhPendingChatAssetUploads.contains(msg.upload_id())) {
		return;
	}

	PendingChatAssetUpload upload = qhPendingChatAssetUploads.take(msg.upload_id());
	if (upload.owner != uSource) {
		qhPendingChatAssetUploads.insert(upload.uploadID, upload);
		return;
	}

	auto reject = [&](const QString &reason) {
		sendChatAssetState(this, uSource, upload.uploadID, MumbleProto::ChatAssetTransferStateRejected, reason);
		QFile::remove(upload.tempFilePath);
	};

	if (upload.receivedByteSize != upload.expectedByteSize) {
		reject(QStringLiteral("Upload size does not match declared asset size."));
		return;
	}
	if (!upload.finalChunkReceived) {
		reject(QStringLiteral("Upload is missing a final chunk marker."));
		return;
	}

	const QString commitFilename = msg.has_filename() ? u8(msg.filename()) : QString();
	if (!commitFilename.isEmpty()) {
		upload.filename = commitFilename;
	}

	const QString commitHash = msg.has_sha256() ? u8(msg.sha256()).trimmed().toLower() : QString();
	if (!commitHash.isEmpty()) {
		if (!isValidSha256Hex(commitHash)) {
			reject(QStringLiteral("Invalid SHA-256 checksum."));
			return;
		}
		upload.sha256 = commitHash;
	}

	QFile tempFile(upload.tempFilePath);
	if (!tempFile.open(QIODevice::ReadOnly)) {
		reject(QStringLiteral("Failed to read back temporary upload file."));
		return;
	}
	const QByteArray fileBytes = tempFile.readAll();
	tempFile.close();
	if (static_cast< quint64 >(fileBytes.size()) != upload.expectedByteSize) {
		reject(QStringLiteral("Temporary upload file size is invalid."));
		return;
	}

	const QString computedHash =
		QString::fromLatin1(QCryptographicHash::hash(fileBytes, QCryptographicHash::Sha256).toHex());
	if (!upload.sha256.isEmpty() && computedHash != upload.sha256) {
		reject(QStringLiteral("SHA-256 checksum mismatch."));
		return;
	}
	upload.sha256 = computedHash;

	QByteArray storedBytes    = fileBytes;
	QString storedMime        = upload.mime;
	unsigned int storedWidth  = 0;
	unsigned int storedHeight = 0;
	std::optional< SanitizedChatImage > previewThumbnail;
	if (upload.kind == msdb::ChatAssetKind::Image && isSanitizableImageMime(upload.mime)) {
		const QString detectedMime = detectedChatImageMime(fileBytes);
		if (detectedMime.isEmpty() || detectedMime != upload.mime) {
			reject(QStringLiteral("Uploaded image data does not match its declared MIME type."));
			return;
		}
		const auto decodedImage = decodeChatImageBounded(
			fileBytes, QSize(CHAT_PREVIEW_THUMBNAIL_WIDTH, CHAT_PREVIEW_THUMBNAIL_HEIGHT));
		if (!decodedImage) {
			reject(QStringLiteral("Uploaded image could not be decoded safely."));
			return;
		}

		// Preserve the validated original bytes and MIME for downloads. Only the inline preview is
		// re-encoded, while dimensions come from the source header validated during the bounded decode.
		storedWidth      = static_cast< unsigned int >(decodedImage->sourceSize.width());
		storedHeight     = static_cast< unsigned int >(decodedImage->sourceSize.height());
		previewThumbnail = sanitizeDecodedChatImage(decodedImage->image, true);
		if (!previewThumbnail) {
			reject(QStringLiteral("Uploaded image preview could not be created safely."));
			return;
		}
	}
	const quint64 storedByteSize = static_cast< quint64 >(storedBytes.size());
	const quint64 previewByteSize =
		previewThumbnail ? static_cast< quint64 >(previewThumbnail->bytes.size()) : 0;
	const QString storedHash =
		QString::fromLatin1(QCryptographicHash::hash(storedBytes, QCryptographicHash::Sha256).toHex());
	const QString storageKey = chatAssetStorageKey(0, storedHash);
	const QString objectPath = chatAssetAbsolutePath(storageKey);
	QString previewHash;
	QString previewStorageKey;
	QString previewObjectPath;
	if (previewThumbnail) {
		previewHash = QString::fromLatin1(
			QCryptographicHash::hash(previewThumbnail->bytes, QCryptographicHash::Sha256).toHex());
		previewStorageKey = chatAssetStorageKey(0, previewHash);
		previewObjectPath = chatAssetAbsolutePath(previewStorageKey);
	}

	const auto additionalPhysicalBytes = [](const QString &path, const quint64 replacementBytes) {
		const QFileInfo existing(path);
		if (!existing.exists()) return replacementBytes;
		const quint64 existingBytes = existing.size() > 0 ? static_cast< quint64 >(existing.size()) : 0;
		return replacementBytes > existingBytes ? replacementBytes - existingBytes : 0ULL;
	};
	const quint64 additionalStoredBytes = additionalPhysicalBytes(objectPath, storedByteSize);
	const quint64 additionalPreviewBytes = previewThumbnail && previewStorageKey != storageKey
		? additionalPhysicalBytes(previewObjectPath, previewByteSize) : 0;
	const quint64 storedPhysicalBytes = chatAssetStoredBytes();
	const bool quotaExceeded = uiChatAssetTotalQuotaBytes > 0
		&& (storedPhysicalBytes > uiChatAssetTotalQuotaBytes
			|| additionalStoredBytes > uiChatAssetTotalQuotaBytes - storedPhysicalBytes
			|| additionalPreviewBytes
				   > uiChatAssetTotalQuotaBytes - storedPhysicalBytes - additionalStoredBytes);
	if ((uiChatAssetMaxBytes > 0 && storedByteSize > uiChatAssetMaxBytes) || quotaExceeded) {
		reject(QStringLiteral("Normalized asset exceeds the configured storage limit."));
		return;
	}

	QString objectError;
	if (!ensureContentAddressedObject(objectPath, storedBytes, storedHash, &objectError)) {
		reject(objectError);
		return;
	}
	if (!QFile::remove(upload.tempFilePath) && QFile::exists(upload.tempFilePath)) {
		reject(QStringLiteral("Failed to discard the completed temporary upload file."));
		return;
	}

	msdb::DBChatAsset storedAsset;
	storedAsset.serverID       = iServerNum;
	storedAsset.ownerUserID    = upload.ownerUserID;
	storedAsset.ownerSession   = upload.ownerSession;
	storedAsset.sha256         = u8(storedHash);
	storedAsset.storageKey     = u8(storageKey);
	storedAsset.mime           = u8(storedMime);
	storedAsset.byteSize       = static_cast< std::uint64_t >(storedBytes.size());
	storedAsset.kind           = upload.kind;
	storedAsset.width          = storedWidth;
	storedAsset.height         = storedHeight;
	storedAsset.retentionClass = msdb::ChatAssetRetentionClass::DefaultStorage;

	storedAsset = m_dbWrapper.addChatAsset(storedAsset);
	if (!storedAsset.ownerUserID) {
		qhEphemeralChatAssetOwners.insert(storedAsset.assetID, uSource);
	}
	if (previewThumbnail) {
		const bool previewStored = ensureContentAddressedObject(
			previewObjectPath, previewThumbnail->bytes, previewHash);

		// Never publish a database reference to an object that failed to reach durable storage.
		// The original attachment remains valid even if its optional thumbnail could not be cached.
		if (previewStored) {
			msdb::DBChatAsset previewAsset;
			previewAsset.serverID       = iServerNum;
			previewAsset.sha256         = u8(previewHash);
			previewAsset.storageKey     = u8(previewStorageKey);
			previewAsset.mime           = u8(previewThumbnail->mime);
			previewAsset.byteSize       = static_cast< std::uint64_t >(previewThumbnail->bytes.size());
			previewAsset.kind           = msdb::ChatAssetKind::Image;
			previewAsset.width          = previewThumbnail->width;
			previewAsset.height         = previewThumbnail->height;
			previewAsset.retentionClass = msdb::ChatAssetRetentionClass::PreviewCache;
			previewAsset                = m_dbWrapper.addChatAsset(previewAsset);
			storedAsset.previewAssetID  = previewAsset.assetID;
			m_dbWrapper.updateChatAssetPreviewAssetID(iServerNum, storedAsset.assetID, previewAsset.assetID);
		}
	}

	sendChatAssetState(this, uSource, upload.uploadID, MumbleProto::ChatAssetTransferStateComplete, QString(),
					   storedAsset.byteSize,
					   protoAssetRefFromAsset(storedAsset, upload.filename,
											  isInlineSafeAsset(upload.kind, storedMime, upload.requestInline)));
}

void Server::msgChatAssetState(ServerUser *, MumbleProto::ChatAssetState &) {
}

void Server::msgChatAssetRequest(ServerUser *uSource, MumbleProto::ChatAssetRequest &msg) {
	ZoneScoped;

	MSG_SETUP(ServerUser::Authenticated);
	QMutexLocker qml(&qmCache);

	if (!clientSupportsChatFeature(uSource, MumbleProto::ChatFeatureAttachments)) {
		sendPersistentChatUnsupported(uSource);
		return;
	}

	if (!msg.has_asset_id() || msg.asset_id() == 0 || !m_dbWrapper.chatAssetExists(iServerNum, msg.asset_id())
		|| !canAccessChatAsset(uSource, msg.asset_id())) {
		MumbleProto::PermissionDenied denied;
		denied.set_session(uSource->uiSession);
		denied.set_type(MumbleProto::PermissionDenied_DenyType_Text);
		denied.set_reason(u8(QStringLiteral("Access to this chat asset is not permitted.")));
		sendMessage(uSource, denied);
		return;
	}

	const msdb::DBChatAsset asset = m_dbWrapper.getChatAsset(iServerNum, msg.asset_id());
	const QString assetPath       = chatAssetAbsolutePath(u8(asset.storageKey));
	QFile assetFile(assetPath);
	if (!assetFile.open(QIODevice::ReadOnly)) {
		MumbleProto::PermissionDenied denied;
		denied.set_session(uSource->uiSession);
		denied.set_type(MumbleProto::PermissionDenied_DenyType_Text);
		denied.set_reason(u8(QStringLiteral("Chat asset is unavailable on the server.")));
		sendMessage(uSource, denied);
		return;
	}

	const quint64 offset         = msg.has_offset() ? msg.offset() : 0;
	const quint32 requestedBytes = msg.has_max_bytes() ? msg.max_bytes() : 65536U;
	const quint32 maxBytes       = std::clamp(requestedBytes, 1024U, 262144U);
	if (offset > static_cast< quint64 >(assetFile.size())) {
		return;
	}

	if (!assetFile.seek(static_cast< qint64 >(offset))) {
		return;
	}

	MumbleProto::ChatAssetChunk chunk;
	chunk.set_asset_id(asset.assetID);
	chunk.set_offset(offset);
	chunk.set_data(blob(assetFile.read(static_cast< qint64 >(maxBytes))));
	chunk.set_eof(offset + static_cast< quint64 >(chunk.data().size()) >= asset.byteSize);
	chunk.set_total_size(asset.byteSize);
	chunk.set_mime(asset.mime);
	if (asset.width > 0) {
		chunk.set_width(asset.width);
	}
	if (asset.height > 0) {
		chunk.set_height(asset.height);
	}
	chunk.set_kind(protoAssetKindFromDB(asset.kind));
	sendMessage(uSource, chunk);
	m_dbWrapper.touchChatAsset(iServerNum, asset.assetID);
}

void Server::msgChatAssetChunk(ServerUser *, MumbleProto::ChatAssetChunk &) {
}

void Server::msgChatEmbedState(ServerUser *, MumbleProto::ChatEmbedState &) {
}

void Server::msgChatEmbedAssistRequest(ServerUser *, MumbleProto::ChatEmbedAssistRequest &) {
}

void Server::msgChatEmbedAssistResult(ServerUser *uSource, MumbleProto::ChatEmbedAssistResult &msg) {
	ZoneScoped;

	MSG_SETUP(ServerUser::Authenticated);

	std::optional< msdb::DBChatMessageEmbed > fallbackEmbed;
	std::optional< msdb::DBChatMessageEmbed > resolvedEmbed;
	MumbleProto::ChatScope scope = MumbleProto::Channel;
	unsigned int scopeID         = 0;
	unsigned int threadID        = 0;
	unsigned int messageID       = 0;
	unsigned int permissionChannelID = 0;
	bool fallbackAlreadyStarted = false;

	{
		QMutexLocker qml(&qmCache);

		if (!bChatPreviewFetchEnabled || !bChatPreviewClientAssistEnabled || !msg.has_lease_id()
			|| !msg.has_message_id() || !msg.has_canonical_url() || !msg.has_url_hash()) {
			return;
		}

		messageID                  = msg.message_id();
		const QString canonicalUrl = u8(msg.canonical_url());
		const QString urlHash      = u8(msg.url_hash());
		const QString assistKey    = chatEmbedAssistKey(messageID, urlHash);
		auto assistIt              = qhPendingChatEmbedAssists.find(assistKey);
		if (assistIt == qhPendingChatEmbedAssists.end()) {
			return;
		}

		const PendingChatEmbedAssist assist = assistIt.value();
		if (assist.leaseID != msg.lease_id() || assist.helperSession != uSource->uiSession
			|| assist.canonicalUrl != canonicalUrl || assist.urlHash != urlHash) {
			return;
		}

		scope               = assist.scope;
		scopeID             = assist.scopeID;
		threadID            = assist.threadID;
		permissionChannelID = assist.permissionChannelID;
		fallbackAlreadyStarted = assist.fallbackStarted;

		const std::optional< msdb::DBChatMessage > message = m_dbWrapper.getChatMessage(iServerNum, messageID);
		if (!message || message->deletedAt > std::chrono::system_clock::time_point()) {
			qhPendingChatEmbedAssists.erase(assistIt);
			return;
		}

		const msdb::DBChatThread thread = m_dbWrapper.getChatThread(iServerNum, message->threadID);
		Channel *permissionChannel      = qhChannels.value(permissionChannelID);
		if (!permissionChannel || !canAccessChatMessage(uSource, *message, thread, permissionChannel, &acCache)) {
			qhPendingChatEmbedAssists.erase(assistIt);
			return;
		}

		std::vector< msdb::DBChatMessageEmbed > embeds = m_dbWrapper.getChatMessageEmbeds(iServerNum, messageID);
		auto pendingIt = std::find_if(embeds.begin(), embeds.end(), [&](const msdb::DBChatMessageEmbed &embed) {
			return embed.urlHash == u8(urlHash) && embed.canonicalUrl == u8(canonicalUrl);
		});
		if (pendingIt == embeds.end() || pendingIt->status != msdb::ChatEmbedStatus::Pending) {
			qhPendingChatEmbedAssists.erase(assistIt);
			return;
		}

		const bool leaseExpired = std::chrono::system_clock::now() > assist.expiresAt;
		const MumbleProto::ChatEmbedStatus status =
			msg.has_status() ? msg.status() : MumbleProto::ChatEmbedStatusPending;
		if (leaseExpired || status != MumbleProto::ChatEmbedStatusReady) {
			if (!fallbackAlreadyStarted) {
				fallbackEmbed = *pendingIt;
			}
		} else {
			msdb::DBChatMessageEmbed updated = *pendingIt;
			const QString title              = msg.has_title() ? u8(msg.title()).trimmed().left(512) : QString();
			const QString description =
				msg.has_description() ? u8(msg.description()).trimmed().left(4096) : QString();
			const QString siteName = msg.has_site_name() ? u8(msg.site_name()).trimmed().left(255) : QString();
			updated.title         = u8(title);
			updated.description   = u8(description);
			updated.siteName      = u8(siteName);

			const QByteArray thumbnailBytes = msg.has_thumbnail() ? blob(msg.thumbnail()) : QByteArray();
			if (!thumbnailBytes.isEmpty()
				&& thumbnailBytes.size() <= static_cast< int >(uiChatPreviewClientAssistThumbnailMaxBytes)) {
				const QString thumbnailMime =
					normalizedMime(msg.has_thumbnail_mime() ? u8(msg.thumbnail_mime()) : QString());
				if (isSanitizableImageMime(thumbnailMime)) {
					if (const auto thumbnail = sanitizeChatImageBytes(thumbnailBytes, true); thumbnail) {
						if (thumbnail->bytes.size()
							<= static_cast< int >(uiChatPreviewClientAssistThumbnailMaxBytes)) {
							updated.previewAssetID =
								persistChatPreviewAsset(thumbnail->bytes, thumbnail->mime, msdb::ChatAssetKind::Image,
														thumbnail->width, thumbnail->height);
						}
					}
				}
			}

			if (updated.title.empty() && updated.description.empty() && updated.siteName.empty()
				&& !updated.previewAssetID) {
				if (!fallbackAlreadyStarted) {
					fallbackEmbed = *pendingIt;
				}
			} else {
				const QUrl previewUrl(canonicalUrl);
				if (updated.title.empty()) {
					updated.title = u8(previewUrl.host());
				}
				if (updated.siteName.empty()) {
					updated.siteName = u8(previewUrl.host());
				}
				updated.status    = msdb::ChatEmbedStatus::Ready;
				updated.errorCode = "";
				updated.fetchedAt = std::chrono::system_clock::now();
				updated.expiresAt = updated.fetchedAt + std::chrono::hours(24 * 7);

				resolvedEmbed = updated;
			}
		}
		qhPendingChatEmbedAssists.erase(assistIt);
	}

	if (fallbackEmbed) {
		scheduleServerChatEmbedFetch(threadID, messageID, scope, scopeID, permissionChannelID, *fallbackEmbed);
		return;
	}
	if (resolvedEmbed) {
		applyChatEmbedFetchResult(threadID, messageID, scope, scopeID, permissionChannelID, *resolvedEmbed);
	}
}

void Server::msgChatReactionToggle(ServerUser *uSource, MumbleProto::ChatReactionToggle &msg) {
	ZoneScoped;

	MSG_SETUP(ServerUser::Authenticated);
	QMutexLocker qml(&qmCache);

	RATELIMIT(uSource);

	if (!clientSupportsChatFeature(uSource, MumbleProto::ChatFeatureReactions)) {
		sendPersistentChatUnsupported(uSource);
		return;
	}

	const std::optional< unsigned int > actorUserID = persistedUserID(uSource);
	if (!actorUserID) {
		sendPersistentChatTextDenied(
			this, uSource, tr("Persisted reactions currently require a registered user identity on this server."));
		return;
	}

	if (!msg.has_message_id() || msg.message_id() == 0 || !msg.has_emoji()) {
		return;
	}

	const QString emoji = u8(msg.emoji()).trimmed();
	if (emoji.isEmpty() || emoji.size() > 32) {
		return;
	}

	MumbleProto::ChatScope scope = msg.has_scope() ? msg.scope() : MumbleProto::Channel;
	unsigned int scopeID =
		msg.has_scope_id() ? msg.scope_id() : (uSource->cChannel ? uSource->cChannel->iId : Mumble::ROOT_CHANNEL_ID);
	Channel *permissionChannel      = nullptr;
	::msdb::ChatThreadScope dbScope = ::msdb::ChatThreadScope::Channel;

	switch (scope) {
		case MumbleProto::Channel:
			permissionChannel = qhChannels.value(scopeID);
			dbScope           = ::msdb::ChatThreadScope::Channel;
			break;
		case MumbleProto::ServerGlobal:
			scopeID           = 0;
			permissionChannel = qhChannels.value(Mumble::ROOT_CHANNEL_ID);
			dbScope           = ::msdb::ChatThreadScope::ServerGlobal;
			break;
		case MumbleProto::Aggregate:
			return;
		case MumbleProto::TextChannel: {
			std::optional<::msdb::DBTextChannel > textChannel = m_dbWrapper.getTextChannel(iServerNum, scopeID);
			if (!textChannel) {
				return;
			}

			permissionChannel = qhChannels.value(textChannel->aclChannelID);
			dbScope           = ::msdb::ChatThreadScope::TextChannel;
			break;
		}
		default:
			return;
	}

	if (scope == MumbleProto::ServerGlobal && !bPersistentGlobalChatEnabled) {
		sendPersistentChatTextDenied(this, uSource, tr("Global chat is disabled by this server."));
		return;
	}

	if (!permissionChannel) {
		return;
	}

	if (!ChanACL::hasPermission(uSource, permissionChannel, ChanACL::TextMessage, &acCache)) {
		PERM_DENIED(uSource, permissionChannel, ChanACL::TextMessage);
		return;
	}

	const std::string scopeKey = chatScopeKey(scope, scopeID);
	if (scopeKey.empty()) {
		return;
	}

	const std::optional<::msdb::DBChatThread > thread = m_dbWrapper.getChatThreadByScope(iServerNum, dbScope, scopeKey);
	if (!thread) {
		sendPersistentChatTextDenied(this, uSource, tr("That message is no longer available."));
		return;
	}
	if (msg.has_thread_id() && msg.thread_id() != thread->threadID) {
		sendPersistentChatTextDenied(this, uSource, tr("That reaction target belongs to a different conversation."));
		return;
	}

	std::optional<::msdb::DBChatMessage > message = m_dbWrapper.getChatMessage(iServerNum, msg.message_id());
	if (!message || message->threadID != thread->threadID) {
		sendPersistentChatTextDenied(this, uSource, tr("That message is no longer available."));
		return;
	}
	if (message->deletedAt > std::chrono::system_clock::time_point()) {
		sendPersistentChatTextDenied(this, uSource, tr("That message is no longer available."));
		return;
	}
	if (!canAccessChatMessage(uSource, *message, *thread, permissionChannel, &acCache)) {
		sendPersistentChatTextDenied(this, uSource, tr("That message is no longer available."));
		return;
	}

	const bool active = msg.has_active() ? msg.active() : true;
	const bool reactionChanged =
		m_dbWrapper.setChatMessageReactionActive(iServerNum, message->messageID, actorUserID.value(), u8(emoji), active);
	if (reactionChanged) {
		log(uSource,
			QString::fromLatin1("%1 reaction %2 on persistent chat message %3 (thread %4, scope %5:%6)")
				.arg(active ? QStringLiteral("Added") : QStringLiteral("Removed"))
				.arg(emoji)
				.arg(message->messageID)
				.arg(message->threadID)
				.arg(static_cast< int >(scope))
				.arg(scopeID));
	}

	message = m_dbWrapper.getChatMessage(iServerNum, msg.message_id());
	if (!message) {
		return;
	}
	if (reactionChanged) {
		invalidateChatHistoryCache(message->threadID);
	}

	QSet< ServerUser * > persistentRecipients;
	if (scope == MumbleProto::Channel) {
		persistentRecipients = recipientsWithChatHistoryAccess(this, qhUsers, scope, scopeID, permissionChannel, acCache,
														   message->createdAt);
		persistentRecipients.unite(recipientsWithLivePersistentChatAccess(
			this, qhUsers, scope, scopeID, permissionChannel, acCache,
			legacyChannelRecipients(qhUsers, m_channelListenerManager, permissionChannel)));
		persistentRecipients.insert(uSource);
	} else {
		persistentRecipients = recipientsWithChatHistoryAccess(this, qhUsers, scope, scopeID, permissionChannel, acCache,
															   message->createdAt);
		persistentRecipients.unite(
			recipientsWithLivePersistentChatAccess(this, qhUsers, scope, scopeID, permissionChannel, acCache));
	}

	const auto resolvedReactionActorName = [this](unsigned int actorUserID) -> std::optional< std::string > {
		const std::optional< std::string > connectedName = connectedUserNameForPersistentID(qhUsers, actorUserID);
		if (connectedName) {
			return connectedName;
		}

		if (actorUserID <= static_cast< unsigned int >(std::numeric_limits< int >::max())) {
			const QString registeredName = getRegisteredUserName(static_cast< int >(actorUserID)).trimmed();
			if (!registeredName.isEmpty()) {
				return u8(registeredName);
			}
		}

		return std::nullopt;
	};

	for (ServerUser *currentUser : persistentRecipients) {
		if (!clientSupportsChatFeature(currentUser, MumbleProto::ChatFeatureReactions)) {
			continue;
		}

		sendMessage(currentUser, protoReactionStateForMessage(*message, scope, scopeID, persistedUserID(currentUser),
															  resolvedReactionActorName));
	}
}

void Server::msgChatReactionState(ServerUser *, MumbleProto::ChatReactionState &) {
}

void Server::msgChatHistoryGrantSync(ServerUser *uSource, MumbleProto::ChatHistoryGrantSync &msg) {
	ZoneScoped;

	MSG_SETUP(ServerUser::Authenticated);
	const MumbleProto::ChatHistoryGrantSync_Action action =
		msg.has_action() ? msg.action() : MumbleProto::ChatHistoryGrantSync_Action_Sync;
	const bool mutation = action == MumbleProto::ChatHistoryGrantSync_Action_Grant
		|| action == MumbleProto::ChatHistoryGrantSync_Action_Revoke;

	auto makeResponse = [&msg, action]() {
		MumbleProto::ChatHistoryGrantSync response;
		response.set_action(action);
		if (msg.has_request_id()) {
			response.set_request_id(msg.request_id());
		}
		return response;
	};
	auto rejectMutation = [this, uSource, &msg, action, &makeResponse](const QString &errorCode,
																	 const QString &displayMessage) {
		MumbleProto::ChatHistoryGrantSync response = makeResponse();
		response.set_result(MumbleProto::ChatHistoryGrantSync_Result_Rejected);
		response.set_error_code(u8(errorCode));
		response.set_message(u8(displayMessage));
		if (msg.grants_size() == 1) {
			*response.add_grants() = msg.grants(0);
		}
		sendMessage(uSource, response);
		const unsigned int targetID = msg.grants_size() == 1 && msg.grants(0).has_user_id()
			? msg.grants(0).user_id() : 0;
		log(uSource, QStringLiteral("Chat history grant rejected action=%1 target=%2 reason=%3")
						 .arg(static_cast< int >(action)).arg(targetID).arg(errorCode));
	};

	if (uSource->leakyBucket.ratelimit(1)) {
		if (mutation) {
			rejectMutation(QStringLiteral("rate_limited"),
				tr("Too many chat history grant requests. Wait a moment and try again."));
		}
		return;
	}

	if (!clientSupportsChatFeature(uSource, MumbleProto::ChatFeatureHistoryGrants)) {
		if (mutation) {
			rejectMutation(QStringLiteral("unsupported"), tr("This server does not support chat history grants."));
		} else {
			sendPersistentChatUnsupported(uSource);
		}
		return;
	}

	if (mutation && clientSupportsChatFeature(uSource, MumbleProto::ChatFeatureHistoryGrantAcks)
		&& !msg.has_request_id()) {
		rejectMutation(QStringLiteral("missing_request_id"), tr("The grant request did not include a request ID."));
		return;
	}
	if (mutation && msg.grants_size() != 1) {
		rejectMutation(QStringLiteral("invalid_item_count"),
			tr("A chat history grant request must contain exactly one target."));
		return;
	}

	Channel *permissionChannel = nullptr;
	Channel *rootChannel       = nullptr;
	unsigned int targetUserID  = 0;
	MumbleProto::ChatScope scope = MumbleProto::Channel;
	unsigned int scopeID       = Mumble::ROOT_CHANNEL_ID;
	std::optional< msdb::ChatThreadScope > dbScope;
	bool changed = false;
	MumbleProto::ChatHistoryGrantSync response = makeResponse();

	{
		QMutexLocker qml(&qmCache);
		rootChannel = qhChannels.value(Mumble::ROOT_CHANNEL_ID);
		if (!rootChannel) {
			if (mutation) {
				rejectMutation(QStringLiteral("missing_root_channel"), tr("The server root channel is unavailable."));
			}
			return;
		}

		if (action == MumbleProto::ChatHistoryGrantSync_Action_Sync) {
			// qmCache is already held. Calling Server::hasPermission here would self-lock.
			if (!ChanACL::hasPermission(uSource, rootChannel, ChanACL::Write, &acCache)) {
				PERM_DENIED(uSource, rootChannel, ChanACL::Write);
				return;
			}

			for (const msdb::DBChatHistoryGrant &grant : m_dbWrapper.getChatHistoryGrants(iServerNum)) {
				const std::optional< MumbleProto::ChatHistoryGrantInfo > info = protoGrantInfoFromDB(grant);
				if (info) {
					*response.add_grants() = *info;
				}
			}
			response.set_result(MumbleProto::ChatHistoryGrantSync_Result_Accepted);
			sendMessage(uSource, response);
			return;
		}

		if (!mutation) {
			rejectMutation(QStringLiteral("invalid_action"), tr("The chat history grant action is invalid."));
			return;
		}

		const MumbleProto::ChatHistoryGrantInfo &info = msg.grants(0);
		if (!info.has_user_id() || info.user_id() > static_cast< unsigned int >(std::numeric_limits< int >::max())
			|| !m_dbWrapper.registeredUserExists(iServerNum, info.user_id())) {
			rejectMutation(QStringLiteral("target_not_registered"),
				tr("The target no longer has a registered server identity. Reopen the user menu and try again."));
			return;
		}
		targetUserID = info.user_id();
		scope        = info.has_scope() ? info.scope() : MumbleProto::Channel;
		scopeID      = info.has_scope_id() ? info.scope_id() : Mumble::ROOT_CHANNEL_ID;
		dbScope      = dbScopeFromProto(scope);
		if (!dbScope) {
			rejectMutation(QStringLiteral("invalid_scope"), tr("The selected chat history scope is not supported."));
			return;
		}

		switch (scope) {
			case MumbleProto::Channel:
				permissionChannel = qhChannels.value(scopeID);
				break;
			case MumbleProto::ServerGlobal:
				scopeID           = 0;
				permissionChannel = rootChannel;
				break;
			case MumbleProto::TextChannel: {
				const std::optional< msdb::DBTextChannel > textChannel = m_dbWrapper.getTextChannel(iServerNum, scopeID);
				if (textChannel) {
					permissionChannel = qhChannels.value(textChannel->aclChannelID);
				}
				break;
			}
			case MumbleProto::Aggregate:
			case MumbleProto::Private:
				break;
		}
		if (!permissionChannel || permissionChannel->bTemporary) {
			rejectMutation(QStringLiteral("scope_not_found"), tr("The selected chat history scope no longer exists."));
			return;
		}
		MumbleProto::ChatHistoryGrantInfo normalizedInfo = info;
		normalizedInfo.set_user_id(targetUserID);
		normalizedInfo.set_scope(scope);
		normalizedInfo.set_scope_id(scopeID);

		// qmCache is already held. Keep all permission checks on the lock-aware ACL path.
		if (!ChanACL::hasPermission(uSource, permissionChannel, ChanACL::Write, &acCache)
			&& !ChanACL::hasPermission(uSource, rootChannel, ChanACL::Write, &acCache)) {
			rejectMutation(QStringLiteral("permission_denied"),
				tr("You no longer have permission to manage chat history for this scope."));
			PERM_DENIED(uSource, permissionChannel, ChanACL::Write);
			return;
		}

		const std::optional< msdb::DBChatHistoryGrant > existingGrant =
			m_dbWrapper.getChatHistoryGrant(iServerNum, targetUserID, *dbScope, scopeID);
		const auto requestedVisibleAfter =
			chatTimePointFromEpochSeconds(info.has_visible_after() ? info.visible_after() : 0);
		if ((action == MumbleProto::ChatHistoryGrantSync_Action_Revoke && !existingGrant)
			|| (action == MumbleProto::ChatHistoryGrantSync_Action_Grant && existingGrant
				&& existingGrant->visibleAfter == requestedVisibleAfter)) {
			response.set_result(MumbleProto::ChatHistoryGrantSync_Result_NoOp);
			if (existingGrant) {
				if (const auto normalized = protoGrantInfoFromDB(*existingGrant)) {
					*response.add_grants() = *normalized;
				}
			} else {
				*response.add_grants() = normalizedInfo;
			}
			sendMessage(uSource, response);
			log(uSource, QStringLiteral("Chat history grant no-op action=%1 target=%2 scope=%3 scope_id=%4")
						 .arg(static_cast< int >(action)).arg(targetUserID)
						 .arg(static_cast< int >(scope)).arg(scopeID));
			return;
		}

		static const QString canonicalGroupName = QStringLiteral("chathistory");
		QString groupName = canonicalGroupName;
		for (const Group *candidate : permissionChannel->qhGroups) {
			if (candidate && candidate->qsName.compare(canonicalGroupName, Qt::CaseInsensitive) == 0) {
				groupName = candidate->qsName;
				break;
			}
		}
		Group *group = permissionChannel->qhGroups.value(groupName);
		const bool groupCreated = action == MumbleProto::ChatHistoryGrantSync_Action_Grant && !group;
		if (groupCreated) {
			group = new Group(permissionChannel, groupName);
		}
		const QSet< int > previousAdd = group ? group->qsAdd : QSet< int >{};
		const QSet< int > previousRemove = group ? group->qsRemove : QSet< int >{};
		ChanACL *createdAcl = nullptr;

		if (action == MumbleProto::ChatHistoryGrantSync_Action_Grant) {
			group->qsRemove.remove(static_cast< int >(targetUserID));
			group->qsAdd.insert(static_cast< int >(targetUserID));
			const bool hasGrantAcl = std::any_of(permissionChannel->qlACL.cbegin(), permissionChannel->qlACL.cend(),
				[&groupName](const ChanACL *acl) {
					return acl && acl->iUserId < 0 && acl->qsGroup.compare(groupName, Qt::CaseInsensitive) == 0
						&& (acl->pAllow & ChanACL::ViewTextMessageHistory) == ChanACL::ViewTextMessageHistory;
				});
			if (!hasGrantAcl) {
				createdAcl             = new ChanACL(permissionChannel);
				createdAcl->bApplyHere = true;
				createdAcl->bApplySubs = false;
				createdAcl->qsGroup    = groupName;
				createdAcl->pDeny      = ChanACL::None;
				createdAcl->pAllow     = ChanACL::ViewTextMessageHistory;
			}
		} else if (group) {
			group->qsAdd.remove(static_cast< int >(targetUserID));
			group->qsRemove.remove(static_cast< int >(targetUserID));
		}

		msdb::DBChatHistoryGrant grant(iServerNum, targetUserID, *dbScope, scopeID);
		grant.visibleAfter = requestedVisibleAfter;
		grant.grantedAt    = std::chrono::system_clock::now();
		if (uSource->iId >= 0) {
			grant.grantedByUserID = static_cast< unsigned int >(uSource->iId);
		}
		try {
			m_dbWrapper.applyChatHistoryGrantChange(
				grant, *permissionChannel, action == MumbleProto::ChatHistoryGrantSync_Action_Revoke);
		} catch (const std::exception &) {
			if (group) {
				group->qsAdd    = previousAdd;
				group->qsRemove = previousRemove;
			}
			if (createdAcl) {
				permissionChannel->qlACL.removeAll(createdAcl);
				delete createdAcl;
			}
			if (groupCreated) {
				permissionChannel->qhGroups.remove(groupName);
				delete group;
			}
			rejectMutation(QStringLiteral("database_error"),
				tr("The server could not save the chat history grant. No changes were applied."));
			return;
		}

		changed = true;
		response.set_result(MumbleProto::ChatHistoryGrantSync_Result_Accepted);
		if (action == MumbleProto::ChatHistoryGrantSync_Action_Grant) {
			if (const auto normalized = protoGrantInfoFromDB(grant)) {
				*response.add_grants() = *normalized;
			}
		} else {
			*response.add_grants() = normalizedInfo;
		}
	}

	// The ACL mutation and both durable writes are complete before the cache is invalidated.
	// clearACLCache owns qmCache itself, so it must run after the guarded mutation scope.
	if (changed) {
		clearACLCache();
	}
	sendMessage(uSource, response);
	log(uSource, QStringLiteral("Chat history grant accepted action=%1 target=%2 scope=%3 scope_id=%4")
				 .arg(static_cast< int >(action)).arg(targetUserID).arg(static_cast< int >(scope)).arg(scopeID));

	for (ServerUser *currentUser : qhUsers) {
		if (currentUser && currentUser->iId >= 0 && static_cast< unsigned int >(currentUser->iId) == targetUserID) {
			sendTextChannelSync(currentUser);
		}
	}
}

void Server::msgTextChannelSync(ServerUser *uSource, MumbleProto::TextChannelSync &msg) {
	MSG_SETUP(ServerUser::Authenticated);

	RATELIMIT(uSource);

	if (!clientSupportsChatFeature(uSource, MumbleProto::ChatFeatureTextChannels)) {
		sendPersistentChatUnsupported(uSource);
		return;
	}

	Channel *rootChannel = qhChannels.value(Mumble::ROOT_CHANNEL_ID);
	if (!rootChannel) {
		return;
	}

	const MumbleProto::TextChannelSync_Action action =
		msg.has_action() ? msg.action() : MumbleProto::TextChannelSync_Action_Sync;
	if (action == MumbleProto::TextChannelSync_Action_Sync) {
		sendTextChannelSync(uSource);
		return;
	}

	if (!hasPermission(uSource, rootChannel, ChanACL::Write)) {
		PERM_DENIED(uSource, rootChannel, ChanACL::Write);
		return;
	}

	auto refreshStoredDefaultTextChannel = [this]() {
		std::vector<::msdb::DBTextChannel > textChannels = m_dbWrapper.getTextChannels(iServerNum);
		std::sort(textChannels.begin(), textChannels.end(), sortTextChannelsForPresentation);
		const std::optional< unsigned int > configuredDefaultTextChannel =
			configuredDefaultTextChannelID(m_dbWrapper, iServerNum);
		if (configuredDefaultTextChannel && containsTextChannelID(textChannels, *configuredDefaultTextChannel)) {
			return;
		}

		storeDefaultTextChannelID(m_dbWrapper, iServerNum, firstTextChannelID(textChannels));
	};

	if (action == MumbleProto::TextChannelSync_Action_Delete) {
		if (!msg.has_target_text_channel_id()) {
			return;
		}

		const unsigned int textChannelID = msg.target_text_channel_id();
		if (!m_dbWrapper.getTextChannel(iServerNum, textChannelID)) {
			return;
		}

		m_dbWrapper.removeTextChannel(iServerNum, textChannelID);
		refreshStoredDefaultTextChannel();
		broadcastTextChannelSync(this, qhUsers);
		return;
	}

	if (action == MumbleProto::TextChannelSync_Action_SetDefault) {
		if (!msg.has_target_text_channel_id()) {
			return;
		}

		const unsigned int textChannelID = msg.target_text_channel_id();
		if (!m_dbWrapper.getTextChannel(iServerNum, textChannelID)) {
			return;
		}

		storeDefaultTextChannelID(m_dbWrapper, iServerNum, textChannelID);
		broadcastTextChannelSync(this, qhUsers);
		return;
	}

	if (msg.channels_size() <= 0) {
		return;
	}

	const MumbleProto::TextChannelInfo &channelInfo = msg.channels(0);
	if (!channelInfo.has_name()) {
		return;
	}

	const QString name = u8(channelInfo.name()).trimmed();
	if (name.isEmpty() || !validateChannelName(name)) {
		return;
	}

	const unsigned int aclChannelID =
		channelInfo.has_acl_channel_id() ? channelInfo.acl_channel_id() : Mumble::ROOT_CHANNEL_ID;
	Channel *permissionChannel = qhChannels.value(aclChannelID);
	if (!permissionChannel) {
		return;
	}

	const QString description   = channelInfo.has_description() ? u8(channelInfo.description()) : QString();
	const unsigned int position = channelInfo.has_position() ? channelInfo.position() : 0;

	if (action == MumbleProto::TextChannelSync_Action_Create) {
		const ::msdb::DBTextChannel createdTextChannel =
			m_dbWrapper.addTextChannel(iServerNum, u8(name), u8(description), aclChannelID, position);
		const std::optional< unsigned int > configuredDefaultTextChannel =
			configuredDefaultTextChannelID(m_dbWrapper, iServerNum);
		const std::vector<::msdb::DBTextChannel > textChannels = m_dbWrapper.getTextChannels(iServerNum);
		if (!configuredDefaultTextChannel || !containsTextChannelID(textChannels, *configuredDefaultTextChannel)) {
			storeDefaultTextChannelID(m_dbWrapper, iServerNum, createdTextChannel.textChannelID);
		}
		broadcastTextChannelSync(this, qhUsers);
		return;
	}

	if (action != MumbleProto::TextChannelSync_Action_Update || !msg.has_target_text_channel_id()) {
		return;
	}

	std::optional<::msdb::DBTextChannel > existing =
		m_dbWrapper.getTextChannel(iServerNum, msg.target_text_channel_id());
	if (!existing) {
		return;
	}

	existing->name         = u8(name);
	existing->description  = u8(description);
	existing->aclChannelID = aclChannelID;
	existing->position     = position;
	m_dbWrapper.updateTextChannel(*existing);
	broadcastTextChannelSync(this, qhUsers);
}

/// Helper function to log the groups of the given channel.
///
/// @param server A pointer to the server object the provided channel lives on
/// @param c A pointer to the channel the groups should be logged for
/// @param prefix An optional QString that is being printed before the groups
void logGroups(Server *server, const Channel *c, QString prefix = QString()) {
	if (!prefix.isEmpty()) {
		server->log(prefix);
	}

	if (c->qhGroups.isEmpty()) {
		server->log(QString::fromLatin1("Channel %1 (%2) has no groups set").arg(c->qsName).arg(c->iId));
		return;
	} else {
		server->log(QString::fromLatin1("%1Listing groups specified for channel \"%2\" (%3)...")
						.arg(prefix.isEmpty() ? QLatin1String("") : QLatin1String("\t"))
						.arg(c->qsName)
						.arg(c->iId));
	}

	for (Group *currentGroup : c->qhGroups) {
		QString memberList;
		for (int m : currentGroup->members()) {
			memberList += QString::fromLatin1("\"%1\"").arg(server->getRegisteredUserName(m));
			memberList += ", ";
		}

		if (currentGroup->members().size() > 0) {
			memberList.remove(memberList.length() - 2, 2);
			server->log(QString::fromLatin1("%1Group: \"%2\" contains following users: %3")
							.arg(prefix.isEmpty() ? QLatin1String("\t") : QLatin1String("\t\t"))
							.arg(currentGroup->qsName)
							.arg(memberList));
		} else {
			server->log(QString::fromLatin1("%1Group \"%2\" doesn't contain any users")
							.arg(prefix.isEmpty() ? QLatin1String("\t") : QLatin1String("\t\t"))
							.arg(currentGroup->qsName));
		}
	}
}

/// Helper function to log the ACLs of the given channel.
///
/// @param server A pointer to the server object the provided channel lives on
/// @param c A pointer to the channel the ACLs should be logged for
/// @param prefix An optional QString that is being printed before the ACLs
void logACLs(Server *server, const Channel *c, QString prefix = QString()) {
	if (!prefix.isEmpty()) {
		server->log(prefix);
	}

	for (const ChanACL *a : c->qlACL) {
		server->log(QString::fromLatin1("%1%2")
						.arg(prefix.isEmpty() ? QLatin1String("") : QLatin1String("\t"))
						.arg(static_cast< QString >(*a)));
	}
}


void Server::msgACL(ServerUser *uSource, MumbleProto::ACL &msg) {
	ZoneScoped;

	MSG_SETUP(ServerUser::Authenticated);

	Channel *c = qhChannels.value(msg.channel_id());
	if (!c)
		return;
	const bool toolsAclCapableClient =
		clientSupportsForkFeature(uSource, MumbleProto::ForkFeatureToolsAcl);
	const bool stonksAclCapableClient =
		clientSupportsForkFeature(uSource, MumbleProto::ForkFeatureStonksAcl);
	ChanACL::Permissions unsupportedRootFeaturePermissions = ChanACL::None;
	if (!toolsAclCapableClient) {
		unsupportedRootFeaturePermissions |= ChanACL::UseTools;
	}
	if (!stonksAclCapableClient) {
		unsupportedRootFeaturePermissions |= ChanACL::UseStonks;
	}

	// For changing channel properties (the 'Write') ACL we allow two things:
	// 1) As per regular ACL propagating mechanism, we check if the user has been
	// granted Write in the channel they try to edit
	// 2) We allow all users who have been granted 'Write' on the root channel
	// to be able to edit _all_ channels, independent of actual propagated ACLs
	// This is done to prevent users who have permission to create (temporary)
	// channels being able to "lock-out" admins by denying them 'Write' in their
	// channel effectively becoming ungovernable.
	if (!hasPermission(uSource, c, ChanACL::Write) && !hasPermission(uSource, qhChannels.value(0), ChanACL::Write)) {
		PERM_DENIED(uSource, c, ChanACL::Write);
		return;
	}

	RATELIMIT(uSource);

	if (msg.has_query() && msg.query()) {
		QStack< Channel * > chans;
		Channel *p;

		QSet< unsigned int > qsId;

		msg.clear_groups();
		msg.clear_acls();
		msg.clear_query();
		msg.set_inherit_acls(c->bInheritACL);

		p = c;
		while (p) {
			chans.push(p);
			if ((p == c) || p->bInheritACL)
				p = p->cParent;
			else
				p = nullptr;
		}

		while (!chans.isEmpty()) {
			p = chans.pop();
			for (ChanACL *acl : p->qlACL) {
				if ((p == c) || (acl->bApplySubs)) {
					MumbleProto::ACL_ChanACL *mpacl = msg.add_acls();

					mpacl->set_inherited(p != c);
					mpacl->set_apply_here(acl->bApplyHere);
					mpacl->set_apply_subs(acl->bApplySubs);
					if (acl->iUserId >= 0) {
						mpacl->set_user_id(static_cast< unsigned int >(acl->iUserId));
						qsId.insert(static_cast< unsigned int >(acl->iUserId));
					} else
						mpacl->set_group(u8(acl->qsGroup));
					ChanACL::Permissions grant = acl->pAllow;
					ChanACL::Permissions deny  = acl->pDeny;
					if (c->iId == Mumble::ROOT_CHANNEL_ID) {
						grant &= ~unsupportedRootFeaturePermissions;
						deny &= ~unsupportedRootFeaturePermissions;
					}
					mpacl->set_grant(static_cast< unsigned int >(grant));
					mpacl->set_deny(static_cast< unsigned int >(deny));
				}
			}
		}

		p                        = c->cParent;
		QSet< QString > allnames = Group::groupNames(c);
		for (const QString &name : allnames) {
			Group *g  = c->qhGroups.value(name);
			Group *pg = p ? Group::getGroup(p, name) : nullptr;

			MumbleProto::ACL_ChanGroup *group = msg.add_groups();
			group->set_name(u8(name));
			group->set_inherit(g ? g->bInherit : true);
			group->set_inheritable(g ? g->bInheritable : true);
			group->set_inherited(pg && pg->bInheritable);
			if (g) {
				for (int id : g->qsAdd) {
					qsId.insert(static_cast< unsigned int >(id));
					group->add_add(static_cast< unsigned int >(id));
				}
				for (int id : g->qsRemove) {
					qsId.insert(static_cast< unsigned int >(id));
					group->add_remove(static_cast< unsigned int >(id));
				}
			}
			if (pg) {
				for (int id : pg->members()) {
					qsId.insert(static_cast< unsigned int >(id));
					group->add_inherited_members(static_cast< unsigned int >(id));
				}
			}
		}

		sendMessage(uSource, msg);

		MumbleProto::QueryUsers mpqu;
		for (unsigned int id : qsId) {
			QString uname = getRegisteredUserName(static_cast< int >(id));
			if (!uname.isEmpty()) {
				mpqu.add_ids(id);
				mpqu.add_names(u8(uname));
			}
		}
		if (mpqu.ids_size())
			sendMessage(uSource, mpqu);
	} else {
		{
			QWriteLocker wl(&qrwlVoiceThread);
			struct PreservedRootFeatureAclRule {
				bool applyHere;
				bool applySubs;
				int userID;
				QString group;
				ChanACL::Permissions allow;
				ChanACL::Permissions deny;
				bool consumed = false;
			};
			QList< PreservedRootFeatureAclRule > preservedRootFeatureAclRules;
			if (c->iId == Mumble::ROOT_CHANNEL_ID
				&& unsupportedRootFeaturePermissions != ChanACL::None) {
				for (const ChanACL *acl : c->qlACL) {
					const ChanACL::Permissions allow = acl->pAllow & unsupportedRootFeaturePermissions;
					const ChanACL::Permissions deny  = acl->pDeny & unsupportedRootFeaturePermissions;
					if (allow == ChanACL::None && deny == ChanACL::None) {
						continue;
					}
					preservedRootFeatureAclRules.push_back({ acl->bApplyHere, acl->bApplySubs, acl->iUserId,
												   acl->qsGroup, allow, deny });
				}
			}

			QHash< QString, QSet< int > > hOldTemp;

			if (Meta::mp->bLogGroupChanges || Meta::mp->bLogACLChanges) {
				log(uSource, QString::fromLatin1("Updating ACL in channel %1").arg(*c));
			}

			if (Meta::mp->bLogGroupChanges) {
				logGroups(this, c, QLatin1String("These are the groups before applying the change:"));
			}

			for (Group *g : c->qhGroups) {
				hOldTemp.insert(g->qsName, g->qsTemporary);
				delete g;
			}

			if (Meta::mp->bLogACLChanges) {
				logACLs(this, c, QLatin1String("These are the ACLs before applying the changed:"));
			}

			// Clear old ACLs
			for (ChanACL *a : c->qlACL) {
				delete a;
			}

			c->qhGroups.clear();
			c->qlACL.clear();

			c->bInheritACL = msg.inherit_acls();

			// Add new groups
			for (int i = 0; i < msg.groups_size(); ++i) {
				const MumbleProto::ACL_ChanGroup &group = msg.groups(i);
				Group *g                                = new Group(c, u8(group.name()));
				g->bInherit                             = group.inherit();
				g->bInheritable                         = group.inheritable();
				for (int j = 0; j < group.add_size(); ++j)
					if (!getRegisteredUserName(static_cast< int >(group.add(j))).isEmpty())
						g->qsAdd << static_cast< int >(group.add(j));
				for (int j = 0; j < group.remove_size(); ++j)
					if (!getRegisteredUserName(static_cast< int >(group.remove(j))).isEmpty())
						g->qsRemove << static_cast< int >(group.remove(j));

				g->qsTemporary = hOldTemp.value(g->qsName);
			}

			if (Meta::mp->bLogGroupChanges) {
				logGroups(this, c, QLatin1String("And these are the new groups:"));
			}

			// Add new ACLs
			for (int i = 0; i < msg.acls_size(); ++i) {
				const MumbleProto::ACL_ChanACL &mpacl = msg.acls(i);
				if (mpacl.has_user_id() && getRegisteredUserName(static_cast< int >(mpacl.user_id())).isEmpty())
					continue;

				ChanACL *a    = new ChanACL(c);
				a->bApplyHere = mpacl.apply_here();
				a->bApplySubs = mpacl.apply_subs();
				if (mpacl.has_user_id())
					a->iUserId = static_cast< int >(mpacl.user_id());
				else
					a->qsGroup = u8(mpacl.group());
				a->pDeny  = static_cast< ChanACL::Permissions >(mpacl.deny()) & ChanACL::All;
				a->pAllow = static_cast< ChanACL::Permissions >(mpacl.grant()) & ChanACL::All;
				if (c->iId == Mumble::ROOT_CHANNEL_ID
					&& unsupportedRootFeaturePermissions != ChanACL::None) {
					a->pAllow &= ~unsupportedRootFeaturePermissions;
					a->pDeny &= ~unsupportedRootFeaturePermissions;
					const auto preserved = std::find_if(
						preservedRootFeatureAclRules.begin(), preservedRootFeatureAclRules.end(),
						[a](const PreservedRootFeatureAclRule &rule) {
							return !rule.consumed && rule.applyHere == a->bApplyHere
								   && rule.applySubs == a->bApplySubs && rule.userID == a->iUserId
								   && rule.group == a->qsGroup;
						});
					if (preserved != preservedRootFeatureAclRules.end()) {
						a->pAllow |= preserved->allow;
						a->pDeny |= preserved->deny;
						preserved->consumed = true;
					}
				}
			}

			for (const PreservedRootFeatureAclRule &rule : preservedRootFeatureAclRules) {
				if (rule.consumed) {
					continue;
				}
				ChanACL *a    = new ChanACL(c);
				a->bApplyHere = rule.applyHere;
				a->bApplySubs = rule.applySubs;
				a->iUserId    = rule.userID;
				a->qsGroup    = rule.group;
				a->pAllow     = rule.allow;
				a->pDeny      = rule.deny;
			}

			if (Meta::mp->bLogACLChanges) {
				logACLs(this, c, QLatin1String("And these are the new ACLs:"));
			}
		}

		clearACLCache();

		if (!hasPermission(uSource, c, ChanACL::Write) && ((uSource->iId >= 0) || !uSource->qsHash.isEmpty())) {
			{
				QWriteLocker wl(&qrwlVoiceThread);

				ChanACL *a    = new ChanACL(c);
				a->bApplyHere = true;
				a->bApplySubs = false;
				if (uSource->iId >= 0)
					a->iUserId = uSource->iId;
				else
					a->qsGroup = QLatin1Char('$') + uSource->qsHash;
				a->iUserId = uSource->iId;
				a->pDeny   = ChanACL::None;
				a->pAllow  = ChanACL::Write | ChanACL::Traverse;
			}

			clearACLCache();
		}


		if (!c->bTemporary) {
			m_dbWrapper.updateChannelData(iServerNum, *c);
		}
		log(uSource, QString("Updated ACL in channel %1").arg(*c));

		// Send refreshed enter states of this channel to all clients
		MumbleProto::ChannelState mpcs;
		mpcs.set_channel_id(c->iId);

		for (ServerUser *user : qhUsers) {
			mpcs.set_is_enter_restricted(isChannelEnterRestricted(c));
			mpcs.set_can_enter(hasPermission(user, c, ChanACL::Enter));

			sendMessage(user, mpcs);
		}

		broadcastTextChannelSync(this, qhUsers);

		if (c->iId == Mumble::ROOT_CHANNEL_ID) {
			QList< QPair< ServerUser *, unsigned int > > rootPermissionUpdates;
			{
				QMutexLocker qml(&qmCache);
				for (ServerUser *user : qhUsers) {
					if (!user || user->sState != ServerUser::Authenticated) {
						continue;
					}
					ChanACL::Permissions permissions = user->iId == 0
						? ChanACL::Permissions(ChanACL::All)
						: ChanACL::effectivePermissions(user, c, &acCache);
					permissions &= ChanACL::All;
					const unsigned int serializedPermissions = static_cast< unsigned int >(permissions);
					user->qmPermissionSent.insert(static_cast< int >(c->iId), serializedPermissions);
					rootPermissionUpdates.push_back(qMakePair(user, serializedPermissions));
				}
			}

			for (const auto &update : rootPermissionUpdates) {
				MumbleProto::PermissionQuery permissionUpdate;
				permissionUpdate.set_channel_id(c->iId);
				permissionUpdate.set_permissions(update.second);
				sendMessage(update.first, permissionUpdate);
			}
		}
	}
}

void Server::msgQueryUsers(ServerUser *uSource, MumbleProto::QueryUsers &msg) {
	ZoneScoped;

	MSG_SETUP(ServerUser::Authenticated);

	// User needs Write permission on at least one channel in the tree
	bool hasWritePermission = false;
	for (Channel *chan : qhChannels) {
		if (hasPermission(uSource, chan, ChanACL::Write)) {
			hasWritePermission = true;
			break;
		}
	}

	if (!hasWritePermission) {
		return;
	}

	MumbleProto::QueryUsers reply;

	for (int i = 0; i < msg.ids_size(); ++i) {
		unsigned int id     = msg.ids(i);
		const QString &name = getRegisteredUserName(static_cast< int >(id));
		if (!name.isEmpty()) {
			reply.add_ids(id);
			reply.add_names(u8(name));
		}
	}

	for (int i = 0; i < msg.names_size(); ++i) {
		QString name = u8(msg.names(i));
		int id       = getRegisteredUserID(name);
		if (id >= 0) {
			name = getRegisteredUserName(id);
			reply.add_ids(static_cast< unsigned int >(id));
			reply.add_names(u8(name));
		}
	}

	sendMessage(uSource, reply);
}

void Server::msgPing(ServerUser *uSource, MumbleProto::Ping &msg) {
	ZoneScoped;

	MSG_SETUP_NO_UNIDLE(ServerUser::Authenticated);

	QMutexLocker l(&uSource->qmCrypt);

	uSource->csCrypt->m_statsRemote.good   = msg.good();
	uSource->csCrypt->m_statsRemote.late   = msg.late();
	uSource->csCrypt->m_statsRemote.lost   = msg.lost();
	uSource->csCrypt->m_statsRemote.resync = msg.resync();

	uSource->dUDPPingAvg  = msg.udp_ping_avg();
	uSource->dUDPPingVar  = msg.udp_ping_var();
	uSource->uiUDPPackets = msg.udp_packets();
	uSource->dTCPPingAvg  = msg.tcp_ping_avg();
	uSource->dTCPPingVar  = msg.tcp_ping_var();
	uSource->uiTCPPackets = msg.tcp_packets();

	quint64 ts = msg.timestamp();

	msg.Clear();
	msg.set_timestamp(ts);
	msg.set_good(uSource->csCrypt->m_statsLocal.good);
	msg.set_late(uSource->csCrypt->m_statsLocal.late);
	msg.set_lost(uSource->csCrypt->m_statsLocal.lost);
	msg.set_resync(uSource->csCrypt->m_statsLocal.resync);

	sendMessage(uSource, msg);
}

void Server::msgCryptSetup(ServerUser *uSource, MumbleProto::CryptSetup &msg) {
	ZoneScoped;

	MSG_SETUP_NO_UNIDLE(ServerUser::Authenticated);

	QMutexLocker l(&uSource->qmCrypt);

	if (!msg.has_client_nonce()) {
		log(uSource, "Requested crypt-nonce resync");
		msg.set_server_nonce(uSource->csCrypt->getEncryptIV());
		sendMessage(uSource, msg);
	} else {
		const std::string &str = msg.client_nonce();
		uSource->csCrypt->m_statsLocal.resync++;
		if (!uSource->csCrypt->setDecryptIV(str)) {
			qWarning("Messages: Cipher resync failed: Invalid nonce from the client!");
		}
	}
}

void Server::msgContextActionModify(ServerUser *, MumbleProto::ContextActionModify &) {
}

void Server::msgContextAction(ServerUser *uSource, MumbleProto::ContextAction &msg) {
	ZoneScoped;

	MSG_SETUP(ServerUser::Authenticated);

	unsigned int session = msg.has_session() ? msg.session() : 0;
	int id               = msg.has_channel_id() ? static_cast< int >(msg.channel_id()) : -1;

	if (session && !qhUsers.contains(session))
		return;
	if ((id >= 0) && !qhChannels.contains(static_cast< unsigned int >(id)))
		return;
	emit contextAction(uSource, u8(msg.action()), session, id);
}

/// @param str The std::string to convert
/// @param maxSize The maximum allowed size for this string
/// @returns The given std::string converted to a QString, if its size is less
///  	than or equal to the given maxSize. If it is bigger, "[[Invalid]]"
///  	is returned.
QString convertWithSizeRestriction(const std::string &str, size_t maxSize) {
	if (str.size() > maxSize) {
		return QLatin1String("[[Invalid]]");
	}

	return QString::fromStdString(str);
}

void Server::msgVersion(ServerUser *uSource, MumbleProto::Version &msg) {
	ZoneScoped;

	RATELIMIT(uSource);

	uSource->m_version                     = MumbleProto::getVersion(msg);
	uSource->qlSupportedChatFeatures       = Mumble::ChatFeatures::featuresFromVersion(msg);
	uSource->qlSupportedForkFeatures       = Mumble::ForkFeatures::featuresFromVersion(msg);
	uSource->bSupportsPersistentChat       = Mumble::ChatFeatures::contains(
		uSource->qlSupportedChatFeatures, MumbleProto::ChatFeaturePersistentHistory);
	uSource->uiPersistentChatProtocolVersion = uSource->bSupportsPersistentChat
		? (msg.has_persistent_chat_protocol_version() ? msg.persistent_chat_protocol_version() : 1U)
		: 0U;
	uSource->uiForkExtensionProtocolVersion =
		msg.has_fork_extension_protocol_version() ? msg.fork_extension_protocol_version() : 0;
	uSource->bSupportsScreenShareSignaling =
		msg.has_supports_screen_share_signaling() && msg.supports_screen_share_signaling();
	uSource->bSupportsScreenShareCapture =
		msg.has_supports_screen_share_capture() && msg.supports_screen_share_capture();
	uSource->bSupportsScreenShareView     = msg.has_supports_screen_share_view() && msg.supports_screen_share_view();
	uSource->qlSupportedScreenShareCodecs = screenShareCodecListFromVersion(msg);
	uSource->uiMaxScreenShareWidth        = Mumble::ScreenShare::sanitizeLimit(
        msg.has_max_screen_share_width() ? msg.max_screen_share_width() : 0, 0, Mumble::ScreenShare::HARD_MAX_WIDTH);
	uSource->uiMaxScreenShareHeight = Mumble::ScreenShare::sanitizeLimit(
		msg.has_max_screen_share_height() ? msg.max_screen_share_height() : 0, 0, Mumble::ScreenShare::HARD_MAX_HEIGHT);
	uSource->uiMaxScreenShareFps = Mumble::ScreenShare::sanitizeLimit(
		msg.has_max_screen_share_fps() ? msg.max_screen_share_fps() : 0, 0, Mumble::ScreenShare::HARD_MAX_FPS);
	if (msg.has_release()) {
		uSource->qsRelease = convertWithSizeRestriction(msg.release(), 100);
	}
	if (msg.has_os()) {
		uSource->qsOS = convertWithSizeRestriction(msg.os(), 40);

		if (msg.has_os_version()) {
			uSource->qsOSVersion = convertWithSizeRestriction(msg.os_version(), 60);
		}
	}

	log(uSource, QString("Client version %1 (%2 %3: %4)")
					 .arg(Version::toString(uSource->m_version))
					 .arg(uSource->qsOS)
					 .arg(uSource->qsOSVersion)
					 .arg(uSource->qsRelease));
	if (uSource->bSupportsScreenShareSignaling) {
		screenShareDiagnosticLog(
			QStringLiteral("Client %1 advertised screen-share support capture=%2 view=%3 codecs=%4 max=%5x%6@%7")
				.arg(uSource->uiSession)
				.arg(uSource->bSupportsScreenShareCapture)
				.arg(uSource->bSupportsScreenShareView)
				.arg(Mumble::ScreenShare::codecPreferenceString(uSource->qlSupportedScreenShareCodecs))
				.arg(uSource->uiMaxScreenShareWidth)
				.arg(uSource->uiMaxScreenShareHeight)
				.arg(uSource->uiMaxScreenShareFps));
	}
}

void Server::msgUserList(ServerUser *uSource, MumbleProto::UserList &msg) {
	ZoneScoped;

	MSG_SETUP(ServerUser::Authenticated);

	// The register permission is required on the root channel to be allowed to
	// view the registered users.
	if (!hasPermission(uSource, qhChannels.value(0), ChanACL::Register)) {
		PERM_DENIED(uSource, qhChannels.value(0), ChanACL::Register);
		return;
	}

	if (msg.users_size() == 0) {
		// Query mode.
		std::vector< UserInfo > users = getAllRegisteredUserProperties();
		for (const UserInfo &info : users) {
			// Skip the SuperUser
			if (info.user_id > 0 && static_cast< unsigned int >(info.user_id) != Mumble::SUPERUSER_ID) {
				::MumbleProto::UserList_User *user = msg.add_users();
				user->set_user_id(static_cast< unsigned int >(info.user_id));
				user->set_name(u8(info.name));
				if (info.last_channel) {
					user->set_last_channel(static_cast< unsigned int >(info.last_channel.value()));
				}
				user->set_last_seen(u8(info.last_active.toString(Qt::ISODate)));
			}
		}
		sendMessage(uSource, msg);
	} else {
		// Update mode
		for (int i = 0; i < msg.users_size(); ++i) {
			const MumbleProto::UserList_User &user = msg.users(i);

			unsigned int id = user.user_id();
			if (id == 0)
				continue;

			if (!user.has_name()) {
				log(uSource, QString::fromLatin1("Unregistered user %1").arg(id));
				unregisterUser(static_cast< int >(id));
			} else {
				const QString &name = u8(user.name()).trimmed();
				if (validateUserName(name)) {
					log(uSource, QString::fromLatin1("Renamed user %1 to '%2'").arg(QString::number(id), name));

					QMap< int, QString > info;
					info.insert(static_cast< int >(::mumble::server::db::UserProperty::Name), name);
					setUserProperties(static_cast< int >(id), info);

					MumbleProto::UserState mpus;
					for (ServerUser *serverUser : qhUsers) {
						if (serverUser->iId == static_cast< int >(id)) {
							serverUser->qsName = name;
							mpus.set_session(serverUser->uiSession);
							break;
						}
					}
					if (mpus.has_session()) {
						mpus.set_actor(uSource->uiSession);
						mpus.set_name(u8(name));
						sendAll(mpus);
					}
				} else {
					MumbleProto::PermissionDenied mppd;
					mppd.set_type(MumbleProto::PermissionDenied_DenyType_UserName);
					if (uSource->m_version < Version::fromComponents(1, 2, 1))
						mppd.set_reason(u8(QString::fromLatin1("%1 is not a valid username").arg(name)));
					else
						mppd.set_name(u8(name));
					sendMessage(uSource, mppd);
				}
			}
		}
	}
}

void Server::msgVoiceTarget(ServerUser *uSource, MumbleProto::VoiceTarget &msg) {
	ZoneScoped;

	MSG_SETUP_NO_UNIDLE(ServerUser::Authenticated);

	int target = static_cast< int >(msg.id());
	if ((target < 1) || (target >= 0x1f))
		return;

	QWriteLocker lock(&qrwlVoiceThread);

	uSource->qmTargetCache.remove(target);

	int count = msg.targets_size();
	if (count == 0) {
		uSource->qmTargets.remove(target);
	} else {
		WhisperTarget wt;
		for (int i = 0; i < count; ++i) {
			const MumbleProto::VoiceTarget_Target &t = msg.targets(i);
			for (int j = 0; j < t.session_size(); ++j) {
				unsigned int s = t.session(j);
				if (qhUsers.contains(s)) {
					wt.sessions.push_back(s);
				}
			}
			if (t.has_channel_id()) {
				unsigned int id = t.channel_id();
				if (qhChannels.contains(id)) {
					WhisperTarget::Channel wtc;
					wtc.id              = id;
					wtc.includeChildren = t.children();
					wtc.includeLinks    = t.links();
					if (t.has_group()) {
						wtc.targetGroup = u8(t.group());
					}

					wt.channels.push_back(wtc);
				}
			}
		}
		if (wt.sessions.empty() && wt.channels.empty()) {
			uSource->qmTargets.remove(target);
		} else {
			uSource->qmTargets.insert(target, std::move(wt));
		}
	}
}

void Server::msgPermissionQuery(ServerUser *uSource, MumbleProto::PermissionQuery &msg) {
	ZoneScoped;

	MSG_SETUP_NO_UNIDLE(ServerUser::Authenticated);

	Channel *c = qhChannels.value(msg.channel_id());
	if (!c)
		return;

	sendClientPermission(uSource, c, true);
}

void Server::msgCodecVersion(ServerUser *, MumbleProto::CodecVersion &) {
}

void Server::msgUserStats(ServerUser *uSource, MumbleProto::UserStats &msg) {
	ZoneScoped;

	MSG_SETUP_NO_UNIDLE(ServerUser::Authenticated);
	VICTIM_SETUP;
	const BandwidthRecord &bwr            = pDstServerUser->bwr;
	const QList< QSslCertificate > &certs = pDstServerUser->peerCertificateChain();

	bool extend = (uSource == pDstServerUser) || hasPermission(uSource, qhChannels.value(0), ChanACL::Ban);

	if (!extend && !hasPermission(uSource, pDstServerUser->cChannel, ChanACL::Enter)) {
		PERM_DENIED(uSource, pDstServerUser->cChannel, ChanACL::Enter);
		return;
	}

	bool details = extend;
	bool local   = extend || (pDstServerUser->cChannel == uSource->cChannel);

	if (msg.stats_only())
		details = false;

	msg.Clear();
	msg.set_session(pDstServerUser->uiSession);

	if (details) {
		for (const QSslCertificate &cert : certs) {
			const QByteArray &der = cert.toDer();
			msg.add_certificates(blob(der));
		}
		msg.set_strong_certificate(pDstServerUser->bVerified);
	}

	if (local) {
		MumbleProto::UserStats_Stats *mpusss;

		QMutexLocker l(&pDstServerUser->qmCrypt);

		mpusss = msg.mutable_from_client();
		mpusss->set_good(pDstServerUser->csCrypt->m_statsLocal.good);
		mpusss->set_late(pDstServerUser->csCrypt->m_statsLocal.late);
		mpusss->set_lost(pDstServerUser->csCrypt->m_statsLocal.lost);
		mpusss->set_resync(pDstServerUser->csCrypt->m_statsLocal.resync);

		mpusss = msg.mutable_from_server();
		mpusss->set_good(pDstServerUser->csCrypt->m_statsRemote.good);
		mpusss->set_late(pDstServerUser->csCrypt->m_statsRemote.late);
		mpusss->set_lost(pDstServerUser->csCrypt->m_statsRemote.lost);
		mpusss->set_resync(pDstServerUser->csCrypt->m_statsRemote.resync);

		bool outsideInitialWindow =
			static_cast< unsigned int >(bwr.onlineSeconds()) > pDstServerUser->csCrypt->m_rollingWindow.count();

		MumbleProto::UserStats_RollingStats *mpussrs = msg.mutable_rolling_stats();

		mpusss = mpussrs->mutable_from_client();
		if (outsideInitialWindow) {
			mpusss->set_good(pDstServerUser->csCrypt->m_statsLocalRolling.good);
			mpusss->set_late(pDstServerUser->csCrypt->m_statsLocalRolling.late);
			mpusss->set_lost(pDstServerUser->csCrypt->m_statsLocalRolling.lost);
			mpusss->set_resync(pDstServerUser->csCrypt->m_statsLocalRolling.resync);
		} else {
			mpusss->CopyFrom(*msg.mutable_from_client());
		}

		mpusss = mpussrs->mutable_from_server();
		if (outsideInitialWindow) {
			mpusss->set_good(pDstServerUser->csCrypt->m_statsRemoteRolling.good);
			mpusss->set_late(pDstServerUser->csCrypt->m_statsRemoteRolling.late);
			mpusss->set_lost(pDstServerUser->csCrypt->m_statsRemoteRolling.lost);
			mpusss->set_resync(pDstServerUser->csCrypt->m_statsRemoteRolling.resync);
		} else {
			mpusss->CopyFrom(*msg.mutable_from_server());
		}

		mpussrs->set_time_window(pDstServerUser->csCrypt->m_rollingWindow.count());
	}

	msg.set_udp_packets(pDstServerUser->uiUDPPackets);
	msg.set_tcp_packets(pDstServerUser->uiTCPPackets);
	msg.set_udp_ping_avg(pDstServerUser->dUDPPingAvg);
	msg.set_udp_ping_var(pDstServerUser->dUDPPingVar);
	msg.set_tcp_ping_avg(pDstServerUser->dTCPPingAvg);
	msg.set_tcp_ping_var(pDstServerUser->dTCPPingVar);

	if (details) {
		MumbleProto::Version *mpv;

		mpv = msg.mutable_version();
		if (pDstServerUser->m_version != Version::UNKNOWN) {
			MumbleProto::setVersion(*mpv, pDstServerUser->m_version);
		}
		if (!pDstServerUser->qsRelease.isEmpty()) {
			mpv->set_release(u8(pDstServerUser->qsRelease));
		}
		if (!pDstServerUser->qsOS.isEmpty()) {
			mpv->set_os(u8(pDstServerUser->qsOS));
			if (!pDstServerUser->qsOSVersion.isEmpty())
				mpv->set_os_version(u8(pDstServerUser->qsOSVersion));
		}

		for (int v : pDstServerUser->qlCodecs) {
			msg.add_celt_versions(v);
		}
		msg.set_opus(pDstServerUser->bOpus);

		msg.set_address(pDstServerUser->haAddress.toStdString());
	}

	if (local)
		msg.set_bandwidth(static_cast< unsigned int >(bwr.bandwidth()));
	msg.set_onlinesecs(static_cast< unsigned int >(bwr.onlineSeconds()));
	if (local)
		msg.set_idlesecs(static_cast< unsigned int >(bwr.idleSeconds()));

	sendMessage(uSource, msg);
}

void Server::msgRequestBlob(ServerUser *uSource, MumbleProto::RequestBlob &msg) {
	ZoneScoped;

	MSG_SETUP_NO_UNIDLE(ServerUser::Authenticated);

	int ntextures     = msg.session_texture_size();
	int ncomments     = msg.session_comment_size();
	int ndescriptions = msg.channel_description_size();

	if (ndescriptions) {
		MumbleProto::ChannelState mpcs;
		for (int i = 0; i < ndescriptions; ++i) {
			unsigned int id = msg.channel_description(i);
			Channel *c      = qhChannels.value(id);
			if (c && !c->qsDesc.isEmpty()) {
				mpcs.set_channel_id(id);
				mpcs.set_description(u8(c->qsDesc));
				sendMessage(uSource, mpcs);
			}
		}
	}
	if (ntextures || ncomments) {
		MumbleProto::UserState mpus;
		for (int i = 0; i < ntextures; ++i) {
			unsigned int session = msg.session_texture(i);
			ServerUser *su       = qhUsers.value(session);
			if (su && !su->qbaTexture.isEmpty()) {
				mpus.set_session(session);
				mpus.set_texture(blob(su->qbaTexture));
				sendMessage(uSource, mpus);
			}
		}
		if (ntextures)
			mpus.clear_texture();
		for (int i = 0; i < ncomments; ++i) {
			unsigned int session = msg.session_comment(i);
			ServerUser *su       = qhUsers.value(session);
			if (su && !su->qsComment.isEmpty()) {
				mpus.set_session(session);
				mpus.set_comment(u8(su->qsComment));
				sendMessage(uSource, mpus);
			}
		}
	}
}

void Server::msgServerConfig(ServerUser *uSource, MumbleProto::ServerConfig &msg) {
	MSG_SETUP(ServerUser::Authenticated);

	Channel *rootChannel = qhChannels.value(Mumble::ROOT_CHANNEL_ID);
	if (!rootChannel) {
		return;
	}

	if (!hasPermission(uSource, rootChannel, ChanACL::Write)) {
		PERM_DENIED(uSource, rootChannel, ChanACL::Write);
		return;
	}

	auto applyConfig = [this](const char *key, const QString &value) {
		if (value.trimmed().isEmpty()) {
			m_dbWrapper.clearConfiguration(iServerNum, key);
		} else {
			m_dbWrapper.setConfiguration(iServerNum, key, u8(value));
		}
		setLiveConf(QLatin1String(key), value);
	};
	auto applyBoolConfig = [&applyConfig](const char *key, bool value) {
		applyConfig(key, value ? QLatin1String("true") : QLatin1String("false"));
	};
	auto applyPositiveIntConfig = [&applyConfig](const char *key, unsigned int value) {
		const unsigned int cappedValue =
			std::min(value, static_cast< unsigned int >(std::numeric_limits< int >::max()));
		applyConfig(key, cappedValue > 0 ? QString::number(cappedValue) : QString());
	};

	if (msg.has_welcome_text()) {
		applyConfig("welcometext", u8(msg.welcome_text()));
	}
	if (msg.has_max_bandwidth()) {
		applyPositiveIntConfig("bandwidth", msg.max_bandwidth());
	}
	if (msg.has_allow_html()) {
		applyBoolConfig("allowhtml", msg.allow_html());
	}
	if (msg.has_message_length()) {
		applyPositiveIntConfig("textmessagelength", msg.message_length());
	}
	if (msg.has_image_message_length()) {
		applyPositiveIntConfig("imagemessagelength", msg.image_message_length());
	}
	if (msg.has_max_users()) {
		applyPositiveIntConfig("users", msg.max_users());
	}
	if (msg.has_recording_allowed()) {
		applyBoolConfig("allowrecording", msg.recording_allowed());
	}
	if (msg.has_persistent_global_chat_enabled()) {
		applyBoolConfig("persistentglobalchat", msg.persistent_global_chat_enabled());
	}
	if (msg.has_screen_share_enabled()) {
		applyBoolConfig("screen_share_enabled", msg.screen_share_enabled());
	}
	if (msg.has_screen_share_recording_enabled()) {
		applyBoolConfig("screen_share_recording_enabled", msg.screen_share_recording_enabled());
	}
	if (msg.has_screen_share_helper_required()) {
		applyBoolConfig("screen_share_helper_required", msg.screen_share_helper_required());
	}
	if (msg.preferred_screen_share_codecs_size() > 0) {
		QStringList codecTokens;
		for (int i = 0; i < msg.preferred_screen_share_codecs_size(); ++i) {
			const MumbleProto::ScreenShareCodec codec = msg.preferred_screen_share_codecs(i);
			if (Mumble::ScreenShare::isValidCodec(codec)) {
				codecTokens << Mumble::ScreenShare::codecToConfigToken(codec);
			}
		}
		if (!codecTokens.isEmpty()) {
			applyConfig("screen_share_codec_preferences", codecTokens.join(QLatin1Char(' ')));
		}
	}
	if (msg.has_screen_share_max_width()) {
		applyPositiveIntConfig("screen_share_max_width", msg.screen_share_max_width());
	}
	if (msg.has_screen_share_max_height()) {
		applyPositiveIntConfig("screen_share_max_height", msg.screen_share_max_height());
	}
	if (msg.has_screen_share_max_fps()) {
		applyPositiveIntConfig("screen_share_max_fps", msg.screen_share_max_fps());
	}
	if (msg.has_screen_share_relay_url()) {
		applyConfig("screen_share_relay_url", u8(msg.screen_share_relay_url()).trimmed());
	}
	if (msg.has_stonks_enabled()) {
		applyBoolConfig("stonks_enabled", msg.stonks_enabled());
	}
	if (msg.has_stonks_text_channel_id()) {
		applyPositiveIntConfig("stonks_text_channel_id", msg.stonks_text_channel_id());
	}
	if (msg.has_stonks_social_announcements_enabled()) {
		applyBoolConfig("stonks_social_announcements_enabled", msg.stonks_social_announcements_enabled());
	}
	if (msg.has_stonks_auto_valuation_enabled()) {
		applyBoolConfig("stonks_auto_valuation_enabled", msg.stonks_auto_valuation_enabled());
	}
	if (msg.has_stonks_valuation_interval_minutes()) {
		applyPositiveIntConfig("stonks_valuation_interval_minutes", msg.stonks_valuation_interval_minutes());
	}
	if (msg.has_stonks_valuation_history_days()) {
		applyPositiveIntConfig("stonks_valuation_history_days", msg.stonks_valuation_history_days());
	}
	if (msg.has_feedback_enabled()) {
		applyBoolConfig("feedback_github_enabled", msg.feedback_enabled());
	}
	if (msg.has_feedback_max_log_bytes()) {
		applyPositiveIntConfig("feedback_max_log_bytes", msg.feedback_max_log_bytes());
	}
	if (msg.has_feedback_max_body_bytes()) {
		applyPositiveIntConfig("feedback_max_body_bytes", msg.feedback_max_body_bytes());
	}
	if (msg.has_server_display_name()) {
		applyConfig("server_display_name", u8(msg.server_display_name()).trimmed().left(128));
	}
	if (msg.has_server_monogram()) {
		applyConfig("server_monogram", u8(msg.server_monogram()).trimmed().left(12));
	}
	if (msg.has_server_image()) {
		QString error;
		const std::optional< QByteArray > sanitizedImage =
			sanitizeServerIdentityImageBytes(blob(msg.server_image()), &error);
		if (!sanitizedImage) {
			MumbleProto::PermissionDenied denied;
			denied.set_type(MumbleProto::PermissionDenied_DenyType_Text);
			denied.set_reason(u8(error.isEmpty() ? tr("Server image could not be saved.") : error));
			sendMessage(uSource, denied);
			return;
		}
		applyConfig("server_image", QString::fromLatin1(sanitizedImage->toBase64()));
	}
}

void Server::msgSuggestConfig(ServerUser *, MumbleProto::SuggestConfig &) {
}

void Server::msgPluginDataTransmission(ServerUser *sender, MumbleProto::PluginDataTransmission &msg) {
	ZoneScoped;

	// A client's plugin has sent us a message that we shall delegate to its receivers

	if (sender->m_pluginMessageBucket.ratelimit(1)) {
		qWarning("Dropping plugin message sent from \"%s\" (%d)", qUtf8Printable(sender->qsName), sender->uiSession);
		return;
	}

	if (!msg.has_data() || !msg.has_dataid()) {
		// Messages without data and/or without a data ID can't be used by the clients. Thus we don't even have to send
		// them
		return;
	}

	if (msg.data().size() > Mumble::Plugins::PluginMessage::MAX_DATA_LENGTH) {
		qWarning("Dropping plugin message sent from \"%s\" (%d) - data too large", qUtf8Printable(sender->qsName),
				 sender->uiSession);
		return;
	}
	if (msg.dataid().size() > Mumble::Plugins::PluginMessage::MAX_DATA_ID_LENGTH) {
		qWarning("Dropping plugin message sent from \"%s\" (%d) - data ID too long", qUtf8Printable(sender->qsName),
				 sender->uiSession);
		return;
	}

	// Always set the sender's session and don't rely on it being set correctly (would
	// allow spoofing the sender's session)
	msg.set_sendersession(sender->uiSession);

	// Copy needed data from message in order to be able to remove info about receivers from the message as this doesn't
	// matter for the client
	size_t receiverAmount = static_cast< std::size_t >(msg.receiversessions_size());
	const ::google::protobuf::RepeatedField<::google::protobuf::uint32 > receiverSessions = msg.receiversessions();

	msg.clear_receiversessions();

	QSet< uint32_t > uniqueReceivers;
	uniqueReceivers.reserve(receiverSessions.size());

	for (int i = 0; static_cast< size_t >(i) < receiverAmount; i++) {
		uint32_t userSession = receiverSessions.Get(i);

		if (!uniqueReceivers.contains(userSession)) {
			uniqueReceivers.insert(userSession);
		} else {
			// Duplicate entry -> ignore
			continue;
		}

		ServerUser *receiver = qhUsers.value(receiverSessions.Get(i));

		if (receiver) {
			// We can simply redirect the message we have received to the clients
			sendMessage(receiver, msg);
		}
	}
}

void Server::msgWatchTogetherSync(ServerUser *uSource, MumbleProto::WatchTogetherSync &msg) {
	ZoneScoped;

	MSG_SETUP(ServerUser::Authenticated);
	RATELIMIT(uSource);

	auto deny = [&](const QString &reason) {
		sendPersistentChatTextDenied(this, uSource, reason);
	};

	if (!clientSupportsForkFeature(uSource, MumbleProto::ForkFeatureWatchTogetherRooms)) {
		deny(QStringLiteral("This client does not advertise watch-together support."));
		return;
	}

	const MumbleProto::ChatScope scope = msg.has_scope() ? msg.scope() : MumbleProto::Channel;
	if (scope != MumbleProto::Channel) {
		deny(QStringLiteral("Watch-together sessions are currently channel-scoped."));
		return;
	}

	Channel *channel = nullptr;
	if (msg.has_scope_id() && msg.scope_id() != 0) {
		channel = qhChannels.value(msg.scope_id());
	} else {
		channel = uSource->cChannel;
	}
	if (!channel || channel != uSource->cChannel) {
		deny(QStringLiteral("Watch-together sessions can only be controlled in your current room."));
		return;
	}
	if (!ChanACL::hasPermission(uSource, channel, ChanACL::TextMessage, &acCache)) {
		PERM_DENIED(uSource, channel, ChanACL::TextMessage);
		return;
	}

	const MumbleProto::WatchTogetherEvent event =
		msg.has_event() ? msg.event() : MumbleProto::WatchTogetherEventState;
	QString sessionID = msg.has_session_id() ? u8(msg.session_id()).trimmed() : QString();
	if (event == MumbleProto::WatchTogetherEventStart && sessionID.isEmpty()) {
		sessionID = QUuid::createUuid().toString(QUuid::WithoutBraces);
	}
	if (sessionID.isEmpty() || sessionID.size() > 128) {
		deny(QStringLiteral("Invalid watch-together session."));
		return;
	}

	const Mumble::WatchTogether::SessionEventAdmission admission =
		Mumble::WatchTogether::validateSessionEvent(qhWatchTogetherSessions, sessionID,
			static_cast< unsigned int >(channel->iId), event);
	switch (admission) {
		case Mumble::WatchTogether::SessionEventAdmission::Allowed: break;
		case Mumble::WatchTogether::SessionEventAdmission::UnknownSession:
			deny(QStringLiteral("Unknown watch-together session."));
			return;
		case Mumble::WatchTogether::SessionEventAdmission::DuplicateSessionID:
			deny(QStringLiteral("A watch-together session with this ID already exists."));
			return;
		case Mumble::WatchTogether::SessionEventAdmission::RoomOccupied:
			deny(QStringLiteral("A watch-together session is already active in this room."));
			return;
		case Mumble::WatchTogether::SessionEventAdmission::ScopeMismatch:
			deny(QStringLiteral("This watch-together session is not active in your current room."));
			return;
	}

	const bool hasStoredSession = qhWatchTogetherSessions.contains(sessionID);
	MumbleProto::WatchTogetherSync stored =
		hasStoredSession ? qhWatchTogetherSessions.value(sessionID) : MumbleProto::WatchTogetherSync();
	const unsigned int storedHost = stored.has_host_session() ? stored.host_session() : 0;
	const bool sourceIsHost       = storedHost == 0 || storedHost == uSource->uiSession;
	if ((event == MumbleProto::WatchTogetherEventState || event == MumbleProto::WatchTogetherEventEnd
		 || event == MumbleProto::WatchTogetherEventHostTransfer)
		&& !sourceIsHost) {
		deny(QStringLiteral("Only the host can control this watch-together session."));
		return;
	}
	if (event == MumbleProto::WatchTogetherEventHostTransfer && !msg.has_host_session()) {
		deny(QStringLiteral("Host transfer requires a target host."));
		return;
	}
	if (event == MumbleProto::WatchTogetherEventLeave && sourceIsHost) {
		deny(QStringLiteral("The host must end the session or transfer hosting before leaving."));
		return;
	}

	if (event == MumbleProto::WatchTogetherEventStart) {
		if (!msg.has_source_url() || msg.source_url().empty() || msg.source_url().size() > 2048
			|| (msg.has_title() && msg.title().size() > 256)) {
			deny(QStringLiteral("Invalid watch-together source."));
			return;
		}

		const QUrl sourceUrl(u8(msg.source_url()));
		static const QSet< QString > youtubeHosts {
			QStringLiteral("youtube.com"), QStringLiteral("www.youtube.com"),
			QStringLiteral("youtube-nocookie.com"), QStringLiteral("www.youtube-nocookie.com")
		};
		static const QSet< QString > providerEmbedHosts {
			QStringLiteral("youtube.com"), QStringLiteral("www.youtube.com"),
			QStringLiteral("youtube-nocookie.com"), QStringLiteral("www.youtube-nocookie.com"),
			QStringLiteral("player.twitch.tv"), QStringLiteral("streamable.com"),
			QStringLiteral("player.vimeo.com"), QStringLiteral("geo.dailymotion.com"),
			QStringLiteral("open.spotify.com"), QStringLiteral("www.facebook.com"),
			QStringLiteral("www.tiktok.com"), QStringLiteral("www.instagram.com"),
			QStringLiteral("w.soundcloud.com")
		};
		const QString sourceHost = sourceUrl.host().toLower();
		const bool validTransport = sourceUrl.isValid() && sourceUrl.scheme() == QLatin1String("https")
			&& !sourceHost.isEmpty() && sourceUrl.userInfo().isEmpty()
			&& (sourceUrl.port(-1) == -1 || sourceUrl.port(-1) == 443);
		const bool validProvider = msg.source_kind() == MumbleProto::WatchTogetherSourceYouTube
			? youtubeHosts.contains(sourceHost)
			: msg.source_kind() == MumbleProto::WatchTogetherSourceDirectMedia
				&& providerEmbedHosts.contains(sourceHost);
		if (!validTransport || !validProvider) {
			deny(QStringLiteral("Watch-together sources must use an approved https provider embed URL."));
			return;
		}
	}

	if (event == MumbleProto::WatchTogetherEventStateRequest) {
		if (hasStoredSession) {
			stored.set_event(MumbleProto::WatchTogetherEventState);
			sendMessage(uSource, stored);
		}
		return;
	}

	msg.set_session_id(u8(sessionID));
	msg.set_scope(MumbleProto::Channel);
	msg.set_scope_id(static_cast< unsigned int >(channel->iId));
	msg.set_actor_session(uSource->uiSession);
	if (event != MumbleProto::WatchTogetherEventHostTransfer) {
		msg.set_host_session(storedHost != 0 ? storedHost : uSource->uiSession);
	}
	if (event == MumbleProto::WatchTogetherEventHostTransfer) {
		ServerUser *newHost = qhUsers.value(msg.host_session());
		if (!newHost || newHost->cChannel != channel
			|| !clientSupportsForkFeature(newHost, MumbleProto::ForkFeatureWatchTogetherRooms)) {
			deny(QStringLiteral("The new host is not available in this room."));
			return;
		}
	}

	if (hasStoredSession && event != MumbleProto::WatchTogetherEventEnd) {
		if (!msg.has_source_url() && stored.has_source_url()) {
			msg.set_source_url(stored.source_url());
		}
		if (!msg.has_title() && stored.has_title()) {
			msg.set_title(stored.title());
		}
		if (!msg.has_source_kind() && stored.has_source_kind()) {
			msg.set_source_kind(stored.source_kind());
		}
		if (!msg.has_position_seconds() && stored.has_position_seconds()) {
			msg.set_position_seconds(stored.position_seconds());
		}
		if (!msg.has_paused() && stored.has_paused()) {
			msg.set_paused(stored.paused());
		}
		if (!msg.has_playback_rate() && stored.has_playback_rate()) {
			msg.set_playback_rate(stored.playback_rate());
		}
		if (!msg.has_updated_at() && event != MumbleProto::WatchTogetherEventState && stored.has_updated_at()) {
			msg.set_updated_at(stored.updated_at());
		}
	}
	if (!msg.has_updated_at()) {
		msg.set_updated_at(static_cast< quint64 >(QDateTime::currentMSecsSinceEpoch()));
	}

	QSet< unsigned int > participants;
	if (hasStoredSession) {
		for (const unsigned int participant : stored.participant_sessions()) participants.insert(participant);
	}
	if (event == MumbleProto::WatchTogetherEventStart || event == MumbleProto::WatchTogetherEventJoin) {
		participants.insert(uSource->uiSession);
	} else if (event == MumbleProto::WatchTogetherEventLeave) {
		participants.remove(uSource->uiSession);
	} else if (event == MumbleProto::WatchTogetherEventHostTransfer) {
		if (!participants.contains(msg.host_session())) {
			deny(QStringLiteral("The new host must join the watch-together session first."));
			return;
		}
	}
	if (msg.has_host_session()) participants.insert(msg.host_session());
	msg.clear_participant_sessions();
	QList< unsigned int > orderedParticipants = participants.values();
	std::sort(orderedParticipants.begin(), orderedParticipants.end());
	for (const unsigned int participant : orderedParticipants) msg.add_participant_sessions(participant);

	if (event == MumbleProto::WatchTogetherEventEnd) {
		qhWatchTogetherSessions.remove(sessionID);
	} else {
		qhWatchTogetherSessions.insert(sessionID, msg);
	}

	QSet< ServerUser * > recipients;
	for (User *user : channel->qlUsers) {
		if (user) {
			recipients.insert(static_cast< ServerUser * >(user));
		}
	}
	for (unsigned int session : m_channelListenerManager.getListenersForChannel(channel->iId)) {
		ServerUser *listener = qhUsers.value(session);
		if (listener) {
			recipients.insert(listener);
		}
	}

	for (ServerUser *recipient : recipients) {
		if (clientSupportsForkFeature(recipient, MumbleProto::ForkFeatureWatchTogetherRooms)) {
			sendMessage(recipient, msg);
		}
	}
}

void Server::msgScreenShareCreate(ServerUser *uSource, MumbleProto::ScreenShareCreate &msg) {
	ZoneScoped;

	MSG_SETUP(ServerUser::Authenticated);

	auto deny = [&](const QString &reason) {
		screenShareDiagnosticLog(QStringLiteral("Denied create from session %1 in channel %2: %3")
									 .arg(uSource->uiSession)
									 .arg(uSource->cChannel ? uSource->cChannel->iId : 0)
									 .arg(reason));
		MumbleProto::PermissionDenied denied;
		denied.set_session(uSource->uiSession);
		denied.set_type(MumbleProto::PermissionDenied_DenyType_Text);
		denied.set_reason(u8(reason));
		sendMessage(uSource, denied);
	};

	if (!bScreenShareEnabled) {
		deny(QStringLiteral("Screen sharing is disabled on this server."));
		return;
	}
	if (!supportsScreenShareCapture(uSource)) {
		deny(QStringLiteral("This client does not advertise screen-share capture support."));
		return;
	}

	const MumbleProto::ScreenShareScope scope = msg.has_scope() ? msg.scope() : MumbleProto::ScreenShareScopeChannel;
	if (scope != MumbleProto::ScreenShareScopeChannel) {
		deny(QStringLiteral("Only channel-scoped screen sharing is supported in this build."));
		return;
	}

	Channel *scopeChannel = msg.has_scope_id() ? screenShareScopeChannel(scope, msg.scope_id()) : uSource->cChannel;
	if (!scopeChannel || scopeChannel != uSource->cChannel) {
		deny(QStringLiteral("Screen shares can only be published in the publisher's current channel."));
		return;
	}
	if (!hasPermission(uSource, scopeChannel, ChanACL::Speak)) {
		PERM_DENIED(uSource, scopeChannel, ChanACL::Speak);
		return;
	}
	if (qhScreenShareStreamByOwnerSession.contains(uSource->uiSession)) {
		deny(QStringLiteral("Only one active screen share per user is allowed."));
		return;
	}
	if (qhScreenShareStreamByChannel.contains(scopeChannel->iId)) {
		deny(QStringLiteral("This channel already has an active screen share."));
		return;
	}
	if (!Mumble::ScreenShare::isValidRelayUrl(qsScreenShareRelayUrl)) {
		deny(QStringLiteral("Screen sharing is unavailable because no relay endpoint is configured."));
		return;
	}

	QList< int > requestedCodecs = screenShareCodecListFromCreate(msg);
	if (requestedCodecs.isEmpty()) {
		requestedCodecs = uSource->qlSupportedScreenShareCodecs;
	}
	requestedCodecs = Mumble::ScreenShare::sanitizeCodecList(requestedCodecs);
	const MumbleProto::ScreenShareRelayTransport relayTransport =
		Mumble::ScreenShare::relayTransportFromUrl(qsScreenShareRelayUrl);
	const QList< int > preferredCodecs = Mumble::ScreenShare::isWebRtcRelayTransport(relayTransport)
											 ? Mumble::ScreenShare::webRtcRelayCodecPreferenceList()
											 : qlPreferredScreenShareCodecs;
	const MumbleProto::ScreenShareCodec codec =
		Mumble::ScreenShare::selectPreferredCodec(preferredCodecs, requestedCodecs);
	if (codec == MumbleProto::ScreenShareCodecUnknown) {
		deny(QStringLiteral("No compatible screen-share codec could be negotiated."));
		return;
	}

	QList< int > codecFallbackOrder;
	for (const int preferredCodec : preferredCodecs) {
		if (requestedCodecs.contains(preferredCodec)) {
			codecFallbackOrder.append(preferredCodec);
		}
	}
	for (const int requestedCodec : requestedCodecs) {
		if (!codecFallbackOrder.contains(requestedCodec)) {
			codecFallbackOrder.append(requestedCodec);
		}
	}

	ScreenShareStream stream;
	stream.qsStreamID          = QUuid::createUuid().toString(QUuid::WithoutBraces);
	stream.uiOwnerSession      = uSource->uiSession;
	stream.scope               = scope;
	stream.uiScopeID           = scopeChannel->iId;
	stream.qsRelayRoomID       = QStringLiteral("screen-share-%1-%2").arg(iServerNum).arg(stream.qsStreamID);
	stream.qsRelayUrl          = qsScreenShareRelayUrl;
	stream.qsRelaySessionID    = QUuid::createUuid().toString(QUuid::WithoutBraces);
	stream.qsRelayPublishToken = randomMessageRelayCredential();
	stream.qsRelayViewToken    = randomMessageRelayCredential();
	stream.uiRelayTokenExpiresAt =
		static_cast< quint64 >(QDateTime::currentMSecsSinceEpoch()) + MESSAGE_SCREEN_SHARE_RELAY_TOKEN_LIFETIME_MSEC;
	stream.relayTransport       = relayTransport;
	stream.uiCreatedAt          = static_cast< quint64 >(QDateTime::currentMSecsSinceEpoch());
	stream.state                = MumbleProto::ScreenShareLifecycleStateActive;
	stream.codec                = codec;
	stream.qlCodecFallbackOrder = codecFallbackOrder;
	stream.uiWidth              = Mumble::ScreenShare::negotiateLimit(
        msg.has_requested_width() ? msg.requested_width() : 0, uSource->uiMaxScreenShareWidth, uiScreenShareMaxWidth,
        Mumble::ScreenShare::DEFAULT_MAX_WIDTH, Mumble::ScreenShare::HARD_MAX_WIDTH);
	stream.uiHeight = Mumble::ScreenShare::negotiateLimit(
		msg.has_requested_height() ? msg.requested_height() : 0, uSource->uiMaxScreenShareHeight,
		uiScreenShareMaxHeight, Mumble::ScreenShare::DEFAULT_MAX_HEIGHT, Mumble::ScreenShare::HARD_MAX_HEIGHT);
	stream.uiFps = Mumble::ScreenShare::negotiateLimit(
		msg.has_requested_fps() ? msg.requested_fps() : 0, uSource->uiMaxScreenShareFps, uiScreenShareMaxFps,
		Mumble::ScreenShare::DEFAULT_MAX_FPS, Mumble::ScreenShare::HARD_MAX_FPS);
	stream.uiBitrateKbps =
		Mumble::ScreenShare::sanitizeBitrateKbps(msg.has_requested_bitrate_kbps() ? msg.requested_bitrate_kbps() : 0,
												 stream.codec, stream.uiWidth, stream.uiHeight, stream.uiFps);
	stream.qsQualityProfile =
		sanitizeScreenShareQualityProfile(msg.has_quality_profile() ? u8(msg.quality_profile()) : QString());
	stream.qsCaptureSourceID = msg.has_capture_source_id() ? u8(msg.capture_source_id()).trimmed() : QString();
	stream.bCaptureAudio     = msg.has_capture_audio() && msg.capture_audio();
	stream.qsAudioSourceID =
		stream.bCaptureAudio && msg.has_audio_source_id() ? u8(msg.audio_source_id()).trimmed() : QString();
	const unsigned int requestedMinBitrate =
		msg.has_requested_min_bitrate_kbps() ? msg.requested_min_bitrate_kbps() : 0;
	const unsigned int requestedMaxBitrate =
		msg.has_requested_max_bitrate_kbps() ? msg.requested_max_bitrate_kbps() : 0;
	stream.uiMinBitrateKbps =
		requestedMinBitrate > 0
			? qMin(Mumble::ScreenShare::sanitizeBitrateKbps(requestedMinBitrate, stream.codec, stream.uiWidth,
															stream.uiHeight, stream.uiFps),
				   stream.uiBitrateKbps)
			: qMin(1200U, stream.uiBitrateKbps);
	stream.uiMaxBitrateKbps =
		requestedMaxBitrate > 0
			? qMax(Mumble::ScreenShare::sanitizeBitrateKbps(requestedMaxBitrate, stream.codec, stream.uiWidth,
															stream.uiHeight, stream.uiFps),
				   stream.uiBitrateKbps)
			: qMax(stream.uiBitrateKbps, qMin(Mumble::ScreenShare::HARD_MAX_BITRATE_KBPS, stream.uiBitrateKbps + 2000U));

	qhScreenShareStreams.insert(stream.qsStreamID, stream);
	qhScreenShareStreamByOwnerSession.insert(stream.uiOwnerSession, stream.qsStreamID);
	qhScreenShareStreamByChannel.insert(stream.uiScopeID, stream.qsStreamID);

	ScreenShareStream &storedStream = qhScreenShareStreams[stream.qsStreamID];
	sendScreenShareStateToAudience(storedStream);
	screenShareDiagnosticLog(QStringLiteral("Created stream %1 owner=%2 channel=%3 codec=%4 requested_codecs=%5 "
											"preferred_codecs=%6 size=%7x%8@%9 bitrate=%10 min=%11 max=%12 "
											"profile=%13 relay=%14 source=%15 audio=%16 audio_source=%17")
								 .arg(storedStream.qsStreamID)
								 .arg(storedStream.uiOwnerSession)
								 .arg(storedStream.uiScopeID)
								 .arg(Mumble::ScreenShare::codecToConfigToken(storedStream.codec))
								 .arg(Mumble::ScreenShare::codecPreferenceString(requestedCodecs))
								 .arg(Mumble::ScreenShare::codecPreferenceString(preferredCodecs))
								 .arg(storedStream.uiWidth)
								 .arg(storedStream.uiHeight)
								 .arg(storedStream.uiFps)
								 .arg(storedStream.uiBitrateKbps)
								 .arg(storedStream.uiMinBitrateKbps)
								 .arg(storedStream.uiMaxBitrateKbps)
								 .arg(storedStream.qsQualityProfile)
								 .arg(Mumble::ScreenShare::relayTransportToConfigToken(storedStream.relayTransport))
								 .arg(storedStream.qsCaptureSourceID.isEmpty() ? QStringLiteral("-")
																			   : storedStream.qsCaptureSourceID)
								 .arg(storedStream.bCaptureAudio ? QStringLiteral("1") : QStringLiteral("0"))
								 .arg(storedStream.qsAudioSourceID.isEmpty() ? QStringLiteral("-")
																			 : storedStream.qsAudioSourceID));
	log(uSource, QString::fromLatin1("Started screen share %1 (%2 %3x%4@%5 %6 kbps)")
					 .arg(storedStream.qsStreamID)
					 .arg(Mumble::ScreenShare::codecToConfigToken(storedStream.codec))
					 .arg(storedStream.uiWidth)
					 .arg(storedStream.uiHeight)
					 .arg(storedStream.uiFps)
					 .arg(storedStream.uiBitrateKbps));
}

void Server::msgScreenShareState(ServerUser *, MumbleProto::ScreenShareState &) {
}

void Server::msgScreenShareOffer(ServerUser *uSource, MumbleProto::ScreenShareOffer &msg) {
	ZoneScoped;

	MSG_SETUP(ServerUser::Authenticated);

	if (!msg.has_stream_id()) {
		return;
	}

	const QString streamID = u8(msg.stream_id());
	if (!qhScreenShareStreams.contains(streamID)) {
		return;
	}

	const ScreenShareStream &stream = qhScreenShareStreams.value(streamID);
	Channel *channel                = screenShareScopeChannel(stream.scope, stream.uiScopeID);
	if (!channel || uSource->cChannel != channel) {
		return;
	}

	const bool isOwner = uSource->uiSession == stream.uiOwnerSession;
	ServerUser *target = nullptr;
	if (isOwner) {
		if (!msg.has_viewer_session()) {
			return;
		}

		target = qhUsers.value(msg.viewer_session());
		if (!target || target->cChannel != channel || !supportsScreenShareView(target)
			|| !target->qlSupportedScreenShareCodecs.contains(static_cast< int >(stream.codec))) {
			return;
		}
	} else {
		if (!supportsScreenShareView(uSource)
			|| !uSource->qlSupportedScreenShareCodecs.contains(static_cast< int >(stream.codec))) {
			return;
		}

		target = qhUsers.value(stream.uiOwnerSession);
		if (!target || target->cChannel != channel || !supportsScreenShareCapture(target)) {
			return;
		}

		msg.set_viewer_session(uSource->uiSession);
	}

	msg.set_stream_id(u8(stream.qsStreamID));
	msg.set_owner_session(stream.uiOwnerSession);
	if (!stream.qsRelayRoomID.isEmpty()) {
		msg.set_relay_room_id(u8(stream.qsRelayRoomID));
	}
	screenShareDiagnosticLog(QStringLiteral("Forwarding offer stream=%1 from=%2 to=%3 viewer=%4 sdp_bytes=%5")
								 .arg(stream.qsStreamID)
								 .arg(uSource->uiSession)
								 .arg(target ? target->uiSession : 0)
								 .arg(msg.has_viewer_session() ? msg.viewer_session() : 0)
								 .arg(msg.has_sdp() ? msg.sdp().size() : 0));
	sendMessage(target, msg);
}

void Server::msgScreenShareAnswer(ServerUser *uSource, MumbleProto::ScreenShareAnswer &msg) {
	ZoneScoped;

	MSG_SETUP(ServerUser::Authenticated);

	if (!msg.has_stream_id()) {
		return;
	}

	const QString streamID = u8(msg.stream_id());
	if (!qhScreenShareStreams.contains(streamID)) {
		return;
	}

	const ScreenShareStream &stream = qhScreenShareStreams.value(streamID);
	Channel *channel                = screenShareScopeChannel(stream.scope, stream.uiScopeID);
	if (!channel || uSource->cChannel != channel) {
		return;
	}

	const bool isOwner = uSource->uiSession == stream.uiOwnerSession;
	ServerUser *target = nullptr;
	if (isOwner) {
		if (!msg.has_viewer_session()) {
			return;
		}

		target = qhUsers.value(msg.viewer_session());
		if (!target || target->cChannel != channel || !supportsScreenShareView(target)) {
			return;
		}
	} else {
		if (!supportsScreenShareView(uSource)) {
			return;
		}

		target = qhUsers.value(stream.uiOwnerSession);
		if (!target || target->cChannel != channel || !supportsScreenShareCapture(target)) {
			return;
		}

		msg.set_viewer_session(uSource->uiSession);
	}

	msg.set_stream_id(u8(stream.qsStreamID));
	msg.set_owner_session(stream.uiOwnerSession);
	if (!stream.qsRelayRoomID.isEmpty()) {
		msg.set_relay_room_id(u8(stream.qsRelayRoomID));
	}
	screenShareDiagnosticLog(QStringLiteral("Forwarding answer stream=%1 from=%2 to=%3 viewer=%4 sdp_bytes=%5")
								 .arg(stream.qsStreamID)
								 .arg(uSource->uiSession)
								 .arg(target ? target->uiSession : 0)
								 .arg(msg.has_viewer_session() ? msg.viewer_session() : 0)
								 .arg(msg.has_sdp() ? msg.sdp().size() : 0));
	sendMessage(target, msg);
}

void Server::msgScreenShareIceCandidate(ServerUser *uSource, MumbleProto::ScreenShareIceCandidate &msg) {
	ZoneScoped;

	MSG_SETUP(ServerUser::Authenticated);

	if (!msg.has_stream_id()) {
		return;
	}

	const QString streamID = u8(msg.stream_id());
	if (!qhScreenShareStreams.contains(streamID)) {
		return;
	}

	const ScreenShareStream &stream = qhScreenShareStreams.value(streamID);
	Channel *channel                = screenShareScopeChannel(stream.scope, stream.uiScopeID);
	if (!channel || uSource->cChannel != channel) {
		return;
	}

	const bool isOwner = uSource->uiSession == stream.uiOwnerSession;
	ServerUser *target = nullptr;
	if (isOwner) {
		if (!msg.has_viewer_session()) {
			return;
		}

		target = qhUsers.value(msg.viewer_session());
		if (!target || target->cChannel != channel || !supportsScreenShareView(target)) {
			return;
		}
	} else {
		if (!supportsScreenShareView(uSource)) {
			return;
		}

		target = qhUsers.value(stream.uiOwnerSession);
		if (!target || target->cChannel != channel || !supportsScreenShareCapture(target)) {
			return;
		}

		msg.set_viewer_session(uSource->uiSession);
	}

	msg.set_stream_id(u8(stream.qsStreamID));
	msg.set_owner_session(stream.uiOwnerSession);
	screenShareDiagnosticLog(QStringLiteral("Forwarding ICE stream=%1 from=%2 to=%3 viewer=%4 candidate_bytes=%5")
								 .arg(stream.qsStreamID)
								 .arg(uSource->uiSession)
								 .arg(target ? target->uiSession : 0)
								 .arg(msg.has_viewer_session() ? msg.viewer_session() : 0)
								 .arg(msg.has_candidate() ? msg.candidate().size() : 0));
	sendMessage(target, msg);
}

void Server::msgScreenShareStop(ServerUser *uSource, MumbleProto::ScreenShareStop &msg) {
	ZoneScoped;

	MSG_SETUP(ServerUser::Authenticated);

	if (!msg.has_stream_id()) {
		return;
	}

	const QString streamID = u8(msg.stream_id());
	if (!qhScreenShareStreams.contains(streamID)) {
		return;
	}

	const ScreenShareStream &stream = qhScreenShareStreams.value(streamID);
	if (stream.uiOwnerSession != uSource->uiSession) {
		return;
	}

	screenShareDiagnosticLog(
		QStringLiteral("Stopping stream %1 by owner %2 reason=%3")
			.arg(streamID)
			.arg(uSource->uiSession)
			.arg(msg.has_reason() ? u8(msg.reason()) : QStringLiteral("Screen share stopped by publisher")));
	stopScreenShare(streamID, uSource->uiSession, MumbleProto::ScreenShareLifecycleStateStopped,
					msg.has_reason() ? u8(msg.reason()) : QStringLiteral("Screen share stopped by publisher"));
}

#undef RATELIMIT
#undef MSG_SETUP
#undef MSG_SETUP_NO_UNIDLE
#undef VICTIM_SETUP
#undef PERM_DENIED
#undef PERM_DENIED_TYPE
#undef PERM_DENIED_FALLBACK
#undef PERM_DENIED_HASH
