// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "EmbedDocument.h"

#include <QtCore/QHash>
#include <QtCore/QSet>
#include <QtCore/QStringList>
#include <QtCore/QUrl>
#include <QtCore/QVariantList>

#include <algorithm>

namespace {
	QString boundedText(const QVariant &value, qsizetype maximum = 4096) {
		return value.toString().trimmed().left(maximum);
	}

	QString firstText(const QVariantMap &map, std::initializer_list< QString > keys,
					  qsizetype maximum = 4096) {
		for (const QString &key : keys) {
			const QString value = boundedText(map.value(key), maximum);
			if (!value.isEmpty()) {
				return value;
			}
		}
		return {};
	}

	QString canonicalProviderToken(QString token) {
		token = token.trimmed().toLower();
		if (token == QLatin1String("twitter") || token == QLatin1String("twitter-x")) {
			return QStringLiteral("x");
		}
		if (token == QLatin1String("fb")) {
			return QStringLiteral("facebook");
		}
		if (token == QLatin1String("raw") || token == QLatin1String("media")) {
			return QStringLiteral("direct");
		}
		if (token == QLatin1String("store.steampowered.com") || token == QLatin1String("steamcommunity.com")) {
			return QStringLiteral("steam");
		}
		return token.left(64);
	}

	QString providerFromHost(const QString &hostValue) {
		QString host = QUrl(QStringLiteral("https://") + hostValue.trimmed()).host().toLower();
		if (host.startsWith(QLatin1String("www."))) {
			host.remove(0, 4);
		}
		if (host == QLatin1String("youtu.be") || host.endsWith(QLatin1String("youtube.com"))
			|| host.endsWith(QLatin1String("youtube-nocookie.com"))) {
			return QStringLiteral("youtube");
		}
		if (host.endsWith(QLatin1String("steampowered.com")) || host.endsWith(QLatin1String("steamcommunity.com"))) {
			return QStringLiteral("steam");
		}
		if (host.endsWith(QLatin1String("instagram.com")) || host == QLatin1String("instagr.am")) {
			return QStringLiteral("instagram");
		}
		if (host.endsWith(QLatin1String("facebook.com")) || host == QLatin1String("fb.watch")) {
			return QStringLiteral("facebook");
		}
		if (host == QLatin1String("x.com") || host == QLatin1String("twitter.com")
			|| host.endsWith(QLatin1String(".x.com")) || host.endsWith(QLatin1String(".twitter.com"))) {
			return QStringLiteral("x");
		}
		if (host.endsWith(QLatin1String("flashback.org"))) {
			return QStringLiteral("flashback");
		}
		if (host.endsWith(QLatin1String("blocket.se"))) {
			return QStringLiteral("blocket");
		}
		if (host.endsWith(QLatin1String("giphy.com"))) {
			return QStringLiteral("giphy");
		}
		if (host.endsWith(QLatin1String("tenor.com"))) {
			return QStringLiteral("tenor");
		}
		if (host.endsWith(QLatin1String("imgur.com"))) {
			return QStringLiteral("imgur");
		}
		if (host.endsWith(QLatin1String("reddit.com")) || host == QLatin1String("redd.it")
			|| host.endsWith(QLatin1String("redd.it"))) {
			return QStringLiteral("reddit");
		}
		if (host == QLatin1String("open.spotify.com") || host == QLatin1String("spotify.link")) {
			return QStringLiteral("spotify");
		}
		if (host.endsWith(QLatin1String("soundcloud.com"))) {
			return QStringLiteral("soundcloud");
		}
		if (host.endsWith(QLatin1String("amazon.se")) || host.endsWith(QLatin1String("amazon.com"))) {
			return QStringLiteral("amazon");
		}
		return {};
	}

	QString providerLabel(const QString &provider, const QVariantMap &metadata, const QString &host) {
		const QString explicitLabel = firstText(metadata,
			{ QStringLiteral("providerName"), QStringLiteral("gameStoreName"),
			  QStringLiteral("articlePublisher"), QStringLiteral("forumProvider") }, 128);
		if (!explicitLabel.isEmpty()) {
			return explicitLabel;
		}
		static const QHash< QString, QString > labels {
			{ QStringLiteral("youtube"), QStringLiteral("YouTube") },
			{ QStringLiteral("steam"), QStringLiteral("Steam") },
			{ QStringLiteral("instagram"), QStringLiteral("Instagram") },
			{ QStringLiteral("facebook"), QStringLiteral("Facebook") },
			{ QStringLiteral("x"), QStringLiteral("X") },
			{ QStringLiteral("flashback"), QStringLiteral("Flashback") },
			{ QStringLiteral("blocket"), QStringLiteral("Blocket") },
			{ QStringLiteral("giphy"), QStringLiteral("GIPHY") },
			{ QStringLiteral("tenor"), QStringLiteral("Tenor") },
			{ QStringLiteral("imgur"), QStringLiteral("Imgur") },
			{ QStringLiteral("reddit"), QStringLiteral("Reddit") },
			{ QStringLiteral("spotify"), QStringLiteral("Spotify") },
			{ QStringLiteral("soundcloud"), QStringLiteral("SoundCloud") },
			{ QStringLiteral("amazon"), QStringLiteral("Amazon") },
			{ QStringLiteral("direct"), QStringLiteral("Media") }
		};
		return labels.value(provider, host);
	}

	QString normalizedMediaKind(const QVariantMap &item) {
		QString kind = boundedText(item.value(QStringLiteral("kind")), 32).toLower();
		const QString mime = boundedText(item.value(QStringLiteral("mime")), 128).toLower();
		if (kind == QLatin1String("gif") || kind == QLatin1String("animated-image")) {
			return QStringLiteral("animated-image");
		}
		if (kind == QLatin1String("image") || mime.startsWith(QLatin1String("image/"))) {
			return item.value(QStringLiteral("managedAnimated")).toBool()
				? QStringLiteral("animated-image") : QStringLiteral("image");
		}
		if (kind == QLatin1String("audio") || mime.startsWith(QLatin1String("audio/"))) {
			return QStringLiteral("audio");
		}
		if (kind == QLatin1String("video") || mime.startsWith(QLatin1String("video/"))
			|| mime == QLatin1String("application/vnd.apple.mpegurl")
			|| mime == QLatin1String("application/dash+xml")) {
			return QStringLiteral("video");
		}
		return {};
	}

	QVariantMap normalizedMediaItem(const QVariantMap &item, int index) {
		const QString kind = normalizedMediaKind(item);
		if (kind.isEmpty()) {
			return {};
		}
		QVariantMap result {
			{ QStringLiteral("id"), QStringLiteral("media:%1").arg(index) },
			{ QStringLiteral("kind"), kind },
			{ QStringLiteral("mime"), boundedText(item.value(QStringLiteral("mime")), 128) },
			{ QStringLiteral("title"), boundedText(item.value(QStringLiteral("title")), 512) },
			{ QStringLiteral("description"), boundedText(item.value(QStringLiteral("description")), 2048) },
			{ QStringLiteral("url"), boundedText(item.value(QStringLiteral("url")), 4096) },
			{ QStringLiteral("externalUrl"), boundedText(item.value(QStringLiteral("externalUrl")), 4096) },
			{ QStringLiteral("thumbnailUrl"), boundedText(item.value(QStringLiteral("thumbnail")), 4096) },
			{ QStringLiteral("posterUrl"), boundedText(item.value(QStringLiteral("poster")), 4096) },
			{ QStringLiteral("streamKind"), boundedText(item.value(QStringLiteral("streamKind")), 32) },
			{ QStringLiteral("contentBranch"), boundedText(item.value(QStringLiteral("contentBranch")), 64) },
			{ QStringLiteral("presentation"), boundedText(item.value(QStringLiteral("mediaPresentation")), 64) },
			{ QStringLiteral("directPlayable"), item.value(QStringLiteral("directPlayable")).toBool() },
			{ QStringLiteral("managedAnimated"), item.value(QStringLiteral("managedAnimated")).toBool() }
		};
		for (auto it = result.begin(); it != result.end();) {
			if (it.value().metaType().id() == QMetaType::QString && it.value().toString().isEmpty()) {
				it = result.erase(it);
			} else {
				++it;
			}
		}
		return result;
	}

	QVariantList mediaItems(const QVariantMap &preview) {
		QVariantList result;
		QSet< QString > identities;
		auto append = [&](const QVariantMap &source, int sourceIndex) {
			QVariantMap item = normalizedMediaItem(source, sourceIndex);
			if (item.isEmpty()) {
				return;
			}
			const QString identity = firstText(item,
				{ QStringLiteral("url"), QStringLiteral("externalUrl"), QStringLiteral("posterUrl"),
				  QStringLiteral("thumbnailUrl") });
			if (identity.isEmpty() || identities.contains(identity)) {
				return;
			}
			identities.insert(identity);
			result.push_back(item);
		};

		int index = 0;
		for (const QVariant &entry : preview.value(QStringLiteral("mediaItems")).toList()) {
			if (result.size() >= 16) {
				break;
			}
			append(entry.toMap(), index++);
		}
		if (result.size() < 16) {
			QVariantMap primary {
				{ QStringLiteral("kind"), preview.value(QStringLiteral("mediaKind")) },
				{ QStringLiteral("mime"), preview.value(QStringLiteral("mediaMime")) },
				{ QStringLiteral("url"), preview.value(QStringLiteral("mediaUrl")) },
				{ QStringLiteral("externalUrl"), preview.value(QStringLiteral("mediaExternalUrl")) },
				{ QStringLiteral("thumbnail"), preview.value(QStringLiteral("thumbnailUrl")) },
				{ QStringLiteral("poster"), preview.value(QStringLiteral("thumbnailUrl")) },
				{ QStringLiteral("title"), preview.value(QStringLiteral("title")) },
				{ QStringLiteral("contentBranch"),
				  preview.value(QStringLiteral("metadata")).toMap().value(QStringLiteral("contentBranch")) },
				{ QStringLiteral("mediaPresentation"),
				  preview.value(QStringLiteral("metadata")).toMap().value(QStringLiteral("mediaPresentation")) },
				{ QStringLiteral("directPlayable"), !preview.value(QStringLiteral("mediaUrl")).toString().isEmpty() },
				{ QStringLiteral("managedAnimated"), preview.value(QStringLiteral("mediaAnimated")).toBool() }
			};
			append(primary, index);
		}
		return result;
	}

	QString contentType(const QString &provider, const QVariantMap &preview, const QVariantMap &metadata,
						const QVariantList &media) {
		const QString previewKind =
			firstText(metadata, { QStringLiteral("previewKind") }, 64).toLower();
		if (provider == QLatin1String("steam") || previewKind == QLatin1String("gamestoreproduct")) {
			return QStringLiteral("game");
		}
		if (provider == QLatin1String("blocket")
			|| previewKind == QLatin1String("marketplacelisting")) {
			return QStringLiteral("marketplace-listing");
		}
		if (provider == QLatin1String("x") || provider == QLatin1String("instagram")
			|| provider == QLatin1String("facebook")) {
			return QStringLiteral("social-post");
		}
		if (provider == QLatin1String("flashback") || previewKind == QLatin1String("forum")) {
			return QStringLiteral("forum-post");
		}
		if (previewKind == QLatin1String("article") || metadata.contains(QStringLiteral("articleTitle"))
			|| metadata.contains(QStringLiteral("articlePublisher"))) {
			return QStringLiteral("article");
		}
		if (provider == QLatin1String("youtube")) {
			return QStringLiteral("video");
		}
		if (media.size() > 1) {
			QSet< QString > kinds;
			for (const QVariant &entry : media) {
				kinds.insert(entry.toMap().value(QStringLiteral("kind")).toString());
			}
			return kinds.size() > 1 ? QStringLiteral("mixed-media") : QStringLiteral("gallery");
		}
		if (!media.isEmpty()) {
			return media.first().toMap().value(QStringLiteral("kind")).toString();
		}
		Q_UNUSED(preview);
		return QStringLiteral("link");
	}

	QVariantMap author(const QString &provider, const QVariantMap &metadata) {
		QVariantMap result;
		if (provider == QLatin1String("x")) {
			result.insert(QStringLiteral("name"), firstText(metadata, { QStringLiteral("xDisplayName") }, 256));
			result.insert(QStringLiteral("handle"), firstText(metadata, { QStringLiteral("xHandle") }, 128));
			result.insert(QStringLiteral("avatarUrl"), firstText(metadata, { QStringLiteral("xAvatarUrl") }, 4096));
			result.insert(QStringLiteral("verified"), metadata.value(QStringLiteral("xVerified")).toBool());
		} else if (provider == QLatin1String("instagram")) {
			result.insert(QStringLiteral("name"),
						  firstText(metadata, { QStringLiteral("instagramDisplayName") }, 256));
			result.insert(QStringLiteral("handle"),
						  firstText(metadata, { QStringLiteral("instagramHandle") }, 128));
			result.insert(QStringLiteral("avatarUrl"),
						  firstText(metadata, { QStringLiteral("instagramAvatarUrl") }, 4096));
		} else if (provider == QLatin1String("flashback")) {
			result.insert(QStringLiteral("name"),
						  firstText(metadata, { QStringLiteral("forumPostAuthor") }, 256));
			result.insert(QStringLiteral("avatarUrl"),
						  firstText(metadata, { QStringLiteral("forumPostAuthorAvatarUrl") }, 4096));
		} else if (provider == QLatin1String("youtube")) {
			result.insert(QStringLiteral("name"),
						  firstText(metadata, { QStringLiteral("youtubeAuthor") }, 256));
		} else if (provider == QLatin1String("facebook")) {
			result.insert(QStringLiteral("name"),
						  firstText(metadata, { QStringLiteral("facebookAuthor") }, 256));
		} else if (metadata.contains(QStringLiteral("articleAuthor"))) {
			result.insert(QStringLiteral("name"),
						  firstText(metadata, { QStringLiteral("articleAuthor") }, 256));
		}
		for (auto it = result.begin(); it != result.end();) {
			if (it.value().metaType().id() == QMetaType::QString && it.value().toString().isEmpty()) {
				it = result.erase(it);
			} else if (it.key() == QLatin1String("verified") && !it.value().toBool()) {
				it = result.erase(it);
			} else {
				++it;
			}
		}
		return result;
	}

	QVariantMap threadItem(const QVariantMap &source, const QString &role) {
		QVariantMap item {
			{ QStringLiteral("role"), role },
			{ QStringLiteral("id"), firstText(source,
				{ QStringLiteral("id"), QStringLiteral("postId") }, 128) },
			{ QStringLiteral("url"), firstText(source,
				{ QStringLiteral("url"), QStringLiteral("postUrl") }, 4096) },
			{ QStringLiteral("authorName"), firstText(source,
				{ QStringLiteral("displayName"), QStringLiteral("author") }, 256) },
			{ QStringLiteral("authorHandle"), firstText(source,
				{ QStringLiteral("handle") }, 128) },
			{ QStringLiteral("text"), firstText(source,
				{ QStringLiteral("text"), QStringLiteral("excerpt") }, 4096) },
			{ QStringLiteral("publishedAt"), firstText(source,
				{ QStringLiteral("createdAt"), QStringLiteral("publishedAt") }, 128) },
			{ QStringLiteral("verified"), source.value(QStringLiteral("verified")).toBool() }
		};
		for (auto it = item.begin(); it != item.end();) {
			if (it.value().metaType().id() == QMetaType::QString && it.value().toString().isEmpty()) {
				it = item.erase(it);
			} else if (it.key() == QLatin1String("verified") && !it.value().toBool()) {
				it = item.erase(it);
			} else {
				++it;
			}
		}
		return item;
	}

	QVariantMap thread(const QString &provider, const QVariantMap &metadata) {
		QVariantList items;
		if (provider == QLatin1String("x")) {
			for (const QVariant &entry : metadata.value(QStringLiteral("xReplyContext")).toList()) {
				const QVariantMap item = threadItem(entry.toMap(), QStringLiteral("reply-context"));
				if (!item.isEmpty()) {
					items.push_back(item);
				}
			}
			const QVariantMap quoted =
				threadItem(metadata.value(QStringLiteral("xQuotedPost")).toMap(), QStringLiteral("quote"));
			if (!quoted.isEmpty()) {
				items.push_back(quoted);
			}
		} else if (provider == QLatin1String("flashback")) {
			const QVariantMap quotedSource {
				{ QStringLiteral("author"), metadata.value(QStringLiteral("forumQuoteAuthor")) },
				{ QStringLiteral("excerpt"), metadata.value(QStringLiteral("forumQuoteExcerpt")) },
				{ QStringLiteral("postId"), metadata.value(QStringLiteral("forumQuotePostId")) },
				{ QStringLiteral("postUrl"), metadata.value(QStringLiteral("forumQuotePostUrl")) }
			};
			const QVariantMap quoted = threadItem(quotedSource, QStringLiteral("quote"));
			if (!quoted.isEmpty()) {
				items.push_back(quoted);
			}
		}
		if (items.isEmpty()) {
			return {};
		}
		return QVariantMap {
			{ QStringLiteral("kind"), provider == QLatin1String("flashback")
					? QStringLiteral("forum-thread") : QStringLiteral("conversation") },
			{ QStringLiteral("items"), items }
		};
	}

	void appendFact(QVariantList &facts, const QString &key, const QString &label, const QVariant &value) {
		if (!value.isValid() || value.isNull()) {
			return;
		}
		if (value.metaType().id() == QMetaType::QString && value.toString().trimmed().isEmpty()) {
			return;
		}
		facts.push_back(QVariantMap {
			{ QStringLiteral("key"), key },
			{ QStringLiteral("label"), label },
			{ QStringLiteral("value"), value }
		});
	}

	QVariantList facts(const QString &provider, const QString &type, const QVariantMap &metadata) {
		QVariantList result;
		if (provider == QLatin1String("steam")) {
			appendFact(result, QStringLiteral("price"), QStringLiteral("Price"),
					   metadata.value(QStringLiteral("steamPrice")));
			appendFact(result, QStringLiteral("developer"), QStringLiteral("Developer"),
					   metadata.value(QStringLiteral("steamDeveloper")));
			appendFact(result, QStringLiteral("platforms"), QStringLiteral("Platforms"),
					   metadata.value(QStringLiteral("steamPlatforms")));
			appendFact(result, QStringLiteral("release"), QStringLiteral("Released"),
					   metadata.value(QStringLiteral("steamReleaseDate")));
			appendFact(result, QStringLiteral("reviews"), QStringLiteral("Reviews"),
					   metadata.value(QStringLiteral("steamReviewSummary")));
		} else if (provider == QLatin1String("x")) {
			appendFact(result, QStringLiteral("replies"), QStringLiteral("Replies"),
					   metadata.value(QStringLiteral("xReplyCount")));
			appendFact(result, QStringLiteral("reposts"), QStringLiteral("Reposts"),
					   metadata.value(QStringLiteral("xRepostCount")));
			appendFact(result, QStringLiteral("likes"), QStringLiteral("Likes"),
					   metadata.value(QStringLiteral("xLikeCount")));
			appendFact(result, QStringLiteral("views"), QStringLiteral("Views"),
					   metadata.value(QStringLiteral("xViewCount")));
		} else if (provider == QLatin1String("instagram")) {
			appendFact(result, QStringLiteral("likes"), QStringLiteral("Likes"),
					   metadata.value(QStringLiteral("instagramLikeCount")));
			appendFact(result, QStringLiteral("comments"), QStringLiteral("Comments"),
					   metadata.value(QStringLiteral("instagramCommentCount")));
		} else if (provider == QLatin1String("facebook")) {
			appendFact(result, QStringLiteral("views"), QStringLiteral("Views"),
					   metadata.value(QStringLiteral("facebookViews")));
			appendFact(result, QStringLiteral("reactions"), QStringLiteral("Reactions"),
					   metadata.value(QStringLiteral("facebookReactions")));
		} else if (provider == QLatin1String("blocket")) {
			appendFact(result, QStringLiteral("price"), QStringLiteral("Price"),
					   metadata.value(QStringLiteral("listingPrice")));
			appendFact(result, QStringLiteral("location"), QStringLiteral("Location"),
					   metadata.value(QStringLiteral("listingLocation")));
			for (const QVariant &entry : metadata.value(QStringLiteral("listingSpecs")).toList()) {
				const QVariantMap spec = entry.toMap();
				appendFact(result,
						   firstText(spec, { QStringLiteral("key"), QStringLiteral("label") }, 64),
						   firstText(spec, { QStringLiteral("label"), QStringLiteral("name") }, 128),
						   spec.value(QStringLiteral("value")));
				if (result.size() >= 6) {
					break;
				}
			}
		} else if (type == QLatin1String("article")) {
			appendFact(result, QStringLiteral("section"), QStringLiteral("Section"),
					   metadata.value(QStringLiteral("articleSection")));
			appendFact(result, QStringLiteral("access"), QStringLiteral("Access"),
					   metadata.value(QStringLiteral("articleAccess")));
		} else if (provider == QLatin1String("flashback")) {
			appendFact(result, QStringLiteral("post"), QStringLiteral("Post"),
					   metadata.value(QStringLiteral("forumPostNumber")));
			appendFact(result, QStringLiteral("page"), QStringLiteral("Page"),
					   metadata.value(QStringLiteral("forumPage")));
			appendFact(result, QStringLiteral("posts"), QStringLiteral("Thread posts"),
					   metadata.value(QStringLiteral("forumPostCount")));
		}
		while (result.size() > 6) {
			result.removeLast();
		}
		return result;
	}

	QString presentationFor(const QString &type) {
		if (type == QLatin1String("social-post")) {
			return QStringLiteral("social");
		}
		if (type == QLatin1String("forum-post")) {
			return QStringLiteral("thread");
		}
		if (type == QLatin1String("article")) {
			return QStringLiteral("article");
		}
		if (type == QLatin1String("game")) {
			return QStringLiteral("game");
		}
		if (type == QLatin1String("marketplace-listing")) {
			return QStringLiteral("marketplace");
		}
		if (type == QLatin1String("video") || type == QLatin1String("audio")
			|| type == QLatin1String("image") || type == QLatin1String("animated-image")
			|| type == QLatin1String("gallery") || type == QLatin1String("mixed-media")) {
			return QStringLiteral("media");
		}
		return QStringLiteral("link");
	}
}

QVariantMap EmbedDocument::fromNormalizedPreview(const QVariantMap &preview) {
	if (preview.isEmpty()) {
		return {};
	}

	const QVariantMap metadata = preview.value(QStringLiteral("metadata")).toMap();
	const QString sourceUrl = boundedText(preview.value(QStringLiteral("url")), 4096);
	const QString host = !boundedText(preview.value(QStringLiteral("host")), 512).isEmpty()
		? boundedText(preview.value(QStringLiteral("host")), 512)
		: QUrl(sourceUrl).host();
	QString provider = canonicalProviderToken(firstText(metadata,
		{ QStringLiteral("provider"), QStringLiteral("previewProvider"),
		  QStringLiteral("gameStoreProvider") }, 64));
	if (provider.isEmpty()) {
		provider = canonicalProviderToken(firstText(preview,
			{ QStringLiteral("embedKind") }, 64));
	}
	if (provider.isEmpty()) {
		provider = providerFromHost(host);
	}

	const QVariantList media = mediaItems(preview);
	if (provider.isEmpty() && !media.isEmpty()) {
		provider = QStringLiteral("direct");
	}
	const QString type = contentType(provider, preview, metadata, media);
	const QString presentation = presentationFor(type);
	const bool commonPresentation = provider == QLatin1String("youtube")
		|| provider == QLatin1String("steam") || provider == QLatin1String("instagram")
		|| provider == QLatin1String("facebook") || provider == QLatin1String("x")
		|| provider == QLatin1String("flashback") || provider == QLatin1String("blocket")
		|| provider == QLatin1String("giphy") || provider == QLatin1String("tenor")
		|| provider == QLatin1String("imgur") || provider == QLatin1String("reddit")
		|| provider == QLatin1String("spotify") || provider == QLatin1String("soundcloud")
		|| provider == QLatin1String("amazon")
		|| provider == QLatin1String("direct")
		|| type == QLatin1String("article");

	QString title = firstText(preview, { QStringLiteral("title") }, 512);
	QString description = firstText(preview, { QStringLiteral("description") }, 4096);
	if (provider == QLatin1String("steam")) {
		title = firstText(metadata,
			{ QStringLiteral("steamAppName"), QStringLiteral("gameStoreProductTitle") }, 512);
		if (description.isEmpty()) {
			description = firstText(metadata, { QStringLiteral("gameStoreDescription") }, 4096);
		}
	} else if (provider == QLatin1String("instagram")) {
		const QString caption = firstText(metadata, { QStringLiteral("instagramCaption") }, 4096);
		if (!caption.isEmpty()) {
			description = caption;
		}
		title.clear();
	} else if (provider == QLatin1String("x")) {
		description = title;
		title.clear();
	} else if (provider == QLatin1String("facebook")) {
		const QString caption = firstText(metadata, { QStringLiteral("facebookCaption") }, 4096);
		if (!caption.isEmpty()) {
			description = caption;
		}
		title.clear();
	} else if (provider == QLatin1String("flashback")) {
		title = firstText(metadata, { QStringLiteral("forumThreadTitle") }, 512);
		description = firstText(metadata, { QStringLiteral("forumPostExcerpt") }, 4096);
	} else if (provider == QLatin1String("blocket")) {
		title = firstText(metadata, { QStringLiteral("listingTitle") }, 512);
		description = firstText(metadata, { QStringLiteral("listingDescription") }, 4096);
	} else if (type == QLatin1String("article")) {
		title = firstText(metadata, { QStringLiteral("articleTitle") }, 512);
		description = firstText(metadata, { QStringLiteral("articleDescription") }, 4096);
	}
	if (title.isEmpty()) {
		title = providerLabel(provider, metadata, host);
	}

	const QString publishedAt =
		provider == QLatin1String("x")
			? firstText(metadata, { QStringLiteral("xCreatedAt") }, 128)
		: provider == QLatin1String("instagram")
			? firstText(metadata, { QStringLiteral("instagramCreatedAt") }, 128)
		: provider == QLatin1String("flashback")
			? firstText(metadata, { QStringLiteral("forumPostTime") }, 128)
		: type == QLatin1String("article")
			? firstText(metadata, { QStringLiteral("articlePublishedAt") }, 128) : QString();

	QVariantMap content {
		{ QStringLiteral("type"), type },
		{ QStringLiteral("title"), title },
		{ QStringLiteral("description"), description },
		{ QStringLiteral("publishedAt"), publishedAt },
		{ QStringLiteral("author"), author(provider, metadata) }
	};
	for (auto it = content.begin(); it != content.end();) {
		if ((it.value().metaType().id() == QMetaType::QString && it.value().toString().isEmpty())
			|| (it.value().metaType().id() == QMetaType::QVariantMap && it.value().toMap().isEmpty())) {
			it = content.erase(it);
		} else {
			++it;
		}
	}

	const QString embedUrl = boundedText(preview.value(QStringLiteral("embedUrl")), 4096);
	QString playbackMode = QStringLiteral("none");
	const QString instagramKind =
		firstText(metadata, { QStringLiteral("instagramMediaKind") }, 32).toLower();
	const bool providerPlaybackAllowed =
		type != QLatin1String("social-post")
		|| provider == QLatin1String("facebook")
		|| (provider == QLatin1String("instagram")
			&& (instagramKind == QLatin1String("reel") || instagramKind == QLatin1String("tv")));
	if (!embedUrl.isEmpty() && providerPlaybackAllowed) {
		playbackMode = QStringLiteral("provider");
	} else {
		for (const QVariant &entry : media) {
			const QVariantMap item = entry.toMap();
			if (item.value(QStringLiteral("directPlayable")).toBool()
				&& (item.value(QStringLiteral("kind")) == QLatin1String("video")
					|| item.value(QStringLiteral("kind")) == QLatin1String("audio")
					|| item.value(QStringLiteral("kind")) == QLatin1String("animated-image"))) {
				playbackMode = QStringLiteral("native");
				break;
			}
		}
	}

	QVariantMap document {
		{ QStringLiteral("schemaVersion"), SchemaVersion },
		{ QStringLiteral("commonPresentation"), commonPresentation },
		{ QStringLiteral("presentation"), presentation },
		{ QStringLiteral("provider"), QVariantMap {
			{ QStringLiteral("id"), provider },
			{ QStringLiteral("label"), providerLabel(provider, metadata, host) },
			{ QStringLiteral("host"), host }
		} },
		{ QStringLiteral("source"), QVariantMap {
			{ QStringLiteral("url"), sourceUrl },
			{ QStringLiteral("displayUrl"), sourceUrl }
		} },
		{ QStringLiteral("content"), content },
		{ QStringLiteral("thread"), thread(provider, metadata) },
		{ QStringLiteral("media"), media },
		{ QStringLiteral("facts"), facts(provider, type, metadata) },
		{ QStringLiteral("playback"), QVariantMap {
			{ QStringLiteral("mode"), playbackMode },
			{ QStringLiteral("provider"), provider },
			{ QStringLiteral("url"), playbackMode == QLatin1String("provider") ? embedUrl : QString() }
		} },
		{ QStringLiteral("state"), QVariantMap {
			{ QStringLiteral("status"), boundedText(preview.value(QStringLiteral("state")), 32) },
			{ QStringLiteral("error"), firstText(preview,
				{ QStringLiteral("errorDescription"), QStringLiteral("errorMessage"),
				  QStringLiteral("error") }, 1024) }
		} }
	};
	return document;
}

QVariantMap EmbedDocument::fromNormalizedAttachment(const QVariantMap &attachment) {
	if (attachment.isEmpty()) {
		return {};
	}

	QString kind = boundedText(attachment.value(QStringLiteral("kind")), 64).toLower();
	const QString mime = boundedText(attachment.value(QStringLiteral("mime")), 128).toLower();
	if (kind.isEmpty()) {
		kind = mime.startsWith(QLatin1String("image/")) ? QStringLiteral("image")
			 : mime.startsWith(QLatin1String("video/")) ? QStringLiteral("video")
			 : mime.startsWith(QLatin1String("audio/")) ? QStringLiteral("audio")
													   : QStringLiteral("file");
	}
	const QString previewUrl = firstText(attachment,
		{ QStringLiteral("thumbnailUrl"), QStringLiteral("url") }, 4096);
	const bool previewAvailable = !previewUrl.isEmpty();
	const bool originalAvailable = attachment.contains(QStringLiteral("assetId"))
		|| !boundedText(attachment.value(QStringLiteral("inlineToken")), 128).isEmpty();
	const QString state = firstText(attachment, { QStringLiteral("state") }, 32);

	QVariantMap content {
		{ QStringLiteral("type"), QStringLiteral("attachment") },
		{ QStringLiteral("kind"), kind },
		{ QStringLiteral("fileName"), firstText(attachment,
			{ QStringLiteral("fileName"), QStringLiteral("name") }, 1024) },
		{ QStringLiteral("mime"), mime },
		{ QStringLiteral("alt"), boundedText(attachment.value(QStringLiteral("alt")), 4096) },
		{ QStringLiteral("byteSize"), attachment.value(QStringLiteral("byteSize")) },
		{ QStringLiteral("width"), attachment.value(QStringLiteral("width")) },
		{ QStringLiteral("height"), attachment.value(QStringLiteral("height")) },
		{ QStringLiteral("durationMs"), attachment.value(QStringLiteral("durationMs")) }
	};
	for (auto it = content.begin(); it != content.end();) {
		const bool emptyString = it.value().metaType().id() == QMetaType::QString
			&& it.value().toString().isEmpty();
		const bool invalidNumber = (it.key() == QLatin1String("byteSize")
			|| it.key() == QLatin1String("width") || it.key() == QLatin1String("height")
			|| it.key() == QLatin1String("durationMs"))
			&& (!it.value().isValid() || it.value().toLongLong() <= 0);
		if (emptyString || invalidNumber) {
			it = content.erase(it);
		} else {
			++it;
		}
	}

	QVariantList media;
	if (previewAvailable) {
		media.push_back(QVariantMap {
			{ QStringLiteral("id"), QStringLiteral("attachment-preview") },
			{ QStringLiteral("kind"), QStringLiteral("image") },
			{ QStringLiteral("role"), kind == QLatin1String("image")
					? QStringLiteral("preview") : QStringLiteral("poster") },
			{ QStringLiteral("url"), previewUrl },
			{ QStringLiteral("directPlayable"), kind == QLatin1String("image") }
		});
	}

	QVariantMap source {
		{ QStringLiteral("assetId"), attachment.value(QStringLiteral("assetId")) },
		{ QStringLiteral("inlineToken"), boundedText(attachment.value(QStringLiteral("inlineToken")), 128) },
		{ QStringLiteral("previewUrl"), previewUrl }
	};
	for (auto it = source.begin(); it != source.end();) {
		if (!it.value().isValid() || it.value().isNull()
			|| (it.value().metaType().id() == QMetaType::QString && it.value().toString().isEmpty())) {
			it = source.erase(it);
		} else {
			++it;
		}
	}

	return QVariantMap {
		{ QStringLiteral("schemaVersion"), SchemaVersion },
		{ QStringLiteral("presentation"), kind == QLatin1String("image")
				? QStringLiteral("image") : QStringLiteral("file") },
		{ QStringLiteral("source"), source },
		{ QStringLiteral("content"), content },
		{ QStringLiteral("media"), media },
		{ QStringLiteral("capabilities"), QVariantMap {
			{ QStringLiteral("preview"), previewAvailable },
			{ QStringLiteral("open"), originalAvailable },
			{ QStringLiteral("download"), originalAvailable },
			{ QStringLiteral("retryPreview"), attachment.value(QStringLiteral("previewCanRetry")).toBool() }
		} },
		{ QStringLiteral("state"), QVariantMap {
			{ QStringLiteral("status"), state.isEmpty() ? QStringLiteral("ready") : state },
			{ QStringLiteral("error"), boundedText(attachment.value(QStringLiteral("previewError")), 512) }
		} }
	};
}
