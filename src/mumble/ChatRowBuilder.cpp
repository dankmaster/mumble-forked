// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ChatRowBuilder.h"

#include "Markdown.h"
#include "Mumble.pb.h"

#include <QtCore/QDateTime>
#include <QtCore/QRegularExpression>
#include <QtCore/QUrl>
#include <QtGui/QTextBlock>
#include <QtGui/QTextCharFormat>
#include <QtGui/QTextDocument>
#include <QtGui/QTextDocumentFragment>
#include <QtGui/QTextFragment>

#include <algorithm>
#include <optional>

namespace {
	constexpr qint64 GroupGapMs                = 5 * 60 * 1000;
	constexpr qsizetype MaxBodyCharacters      = 100000;
	constexpr qsizetype MaxLegacyBodySegments  = 512;

	QString fromUtf8(const std::string &text) {
		return QString::fromUtf8(text.data(), static_cast< qsizetype >(text.size()));
	}

	QString normalizedText(QString text) {
		return text.replace(QLatin1String("\r\n"), QLatin1String("\n")).replace(QLatin1Char('\r'), QLatin1Char('\n'));
	}

	bool isSafeLink(const QString &value) {
		static const QRegularExpression allowed(QStringLiteral(R"(^(https?://|mailto:|mumble://))"),
												QRegularExpression::CaseInsensitiveOption);
		return allowed.match(value.trimmed()).hasMatch();
	}

	QVariantMap block(const QString &kind, const QString &format, const QString &text, int level = 0) {
		return { { QStringLiteral("kind"), kind },
				 { QStringLiteral("level"), level },
				 { QStringLiteral("format"), format },
				 { QStringLiteral("text"), text } };
	}

	// markdownToHTML only emits <h*>, <a>, <b>, <i>, <s>, <code>, <pre>, <div> and
	// <br/> around escaped text. Links with a scheme outside the allowlist keep
	// their text and lose the anchor.
	QString sanitizedLinks(const QString &html) {
		static const QRegularExpression anchor(QStringLiteral(R"re(<a href="([^"]*)">(.*?)</a>)re"),
											   QRegularExpression::DotMatchesEverythingOption);
		QString out;
		qsizetype last = 0;
		for (QRegularExpressionMatchIterator it = anchor.globalMatch(html); it.hasNext();) {
			const QRegularExpressionMatch match = it.next();
			out += html.mid(last, match.capturedStart() - last);
			const QString href = QTextDocumentFragment::fromHtml(match.captured(1)).toPlainText();
			out += isSafeLink(href) ? match.captured(0) : match.captured(2);
			last = match.capturedEnd();
		}
		out += html.mid(last);
		return out;
	}

	bool hasInlineSyntax(const QString &text) {
		static const QRegularExpression syntax(QStringLiteral(R"([*~`\[]|https?://|www\.)"));
		return syntax.match(text).hasMatch();
	}

	// One paragraph, heading or quote. Lines keep their breaks (markdownToHTML
	// would otherwise drop single newlines). StyledText has no monospace, so a
	// paragraph with inline code is emitted as RichText.
	QVariantMap inlineBlock(const QString &kind, const QString &text, int level = 0) {
		if (!hasInlineSyntax(text)) {
			return block(kind, QStringLiteral("plain"), text, level);
		}
		QStringList lines;
		for (const QString &line : text.split(QLatin1Char('\n'))) {
			lines << sanitizedLinks(Markdown::markdownToHTML(line));
		}
		const QString html = lines.join(QStringLiteral("<br>"));
		const bool hasCode = html.contains(QLatin1String("<code>"));
		return block(kind, hasCode ? QStringLiteral("rich") : QStringLiteral("styled"), html, level);
	}

	QVariantList markdownBlocks(const QString &markdown) {
		static const QRegularExpression heading(QStringLiteral(R"(^(#{1,6}) (.*)$)"));
		QVariantList blocks;
		QStringList paragraph;
		const auto flush = [&]() {
			if (paragraph.isEmpty()) {
				return;
			}
			blocks << inlineBlock(QStringLiteral("p"), paragraph.join(QLatin1Char('\n')));
			paragraph.clear();
		};

		const QStringList lines = markdown.split(QLatin1Char('\n'));
		for (int i = 0; i < lines.size(); ++i) {
			const QString &line = lines.at(i);
			if (line.startsWith(QLatin1String("```"))) {
				flush();
				QStringList code;
				for (++i; i < lines.size() && !lines.at(i).startsWith(QLatin1String("```")); ++i) {
					code << lines.at(i);
				}
				blocks << block(QStringLiteral("code"), QStringLiteral("plain"), code.join(QLatin1Char('\n')));
			} else if (line.startsWith(QLatin1String("> ")) || line == QLatin1String(">")) {
				flush();
				QStringList quote;
				for (; i < lines.size()
					   && (lines.at(i).startsWith(QLatin1String("> ")) || lines.at(i) == QLatin1String(">"));
					 ++i) {
					quote << lines.at(i).mid(2);
				}
				--i;
				blocks << inlineBlock(QStringLiteral("quote"), quote.join(QLatin1Char('\n')));
			} else if (const QRegularExpressionMatch match = heading.match(line); match.hasMatch()) {
				flush();
				blocks << inlineBlock(QStringLiteral("h"), match.captured(2), static_cast< int >(match.captured(1).size()));
			} else if (line.trimmed().isEmpty()) {
				flush();
			} else {
				paragraph << line;
			}
		}
		flush();
		return blocks;
	}

	QString linkedPlainTextHtml(const QString &content) {
		static const QRegularExpression urlPattern(QStringLiteral(R"((https?://[^\s<>"']+))"),
												   QRegularExpression::CaseInsensitiveOption);
		QString html;
		qsizetype last = 0;
		const auto appendEscaped = [&html](const QString &segment) {
			html += segment.toHtmlEscaped().replace(QLatin1Char('\n'), QLatin1String("<br>"));
		};
		for (QRegularExpressionMatchIterator it = urlPattern.globalMatch(content); it.hasNext();) {
			const QRegularExpressionMatch match = it.next();
			appendEscaped(content.mid(last, match.capturedStart() - last));
			const QString url = match.captured(1).toHtmlEscaped();
			html += QStringLiteral("<a href=\"%1\">%1</a>").arg(url);
			last = match.capturedEnd();
		}
		appendEscaped(content.mid(last));
		return html;
	}

	int removeInlineDataImages(QString &html) {
		static const QRegularExpression dataImage(
			QStringLiteral(R"(<img[^>]*src\s*=\s*["']data:image/[^"']*["'][^>]*>)"),
			QRegularExpression::CaseInsensitiveOption);
		int count = 0;
		for (QRegularExpressionMatchIterator it = dataImage.globalMatch(html); it.hasNext(); it.next()) {
			++count;
		}
		if (count > 0) {
			html.remove(dataImage);
		}
		return count;
	}

	bool hasMonospaceFamily(const QTextCharFormat &format) {
		// A QTextDocument round trip keeps only the family Qt assigns to <code>/<pre>.
		static const QRegularExpression monospace(QStringLiteral("courier|mono|consolas"),
												  QRegularExpression::CaseInsensitiveOption);
		const QStringList families = format.fontFamilies().toStringList();
		return format.fontFixedPitch()
			   || std::any_of(families.cbegin(), families.cend(),
							  [](const QString &family) { return monospace.match(family).hasMatch(); });
	}

	// Legacy HTML from older clients: parse it and re-emit only escaped text with
	// bold, italic, strike, code and allowlisted links.
	ChatRowBuilder::Body legacyHtmlBody(QString html) {
		ChatRowBuilder::Body body;
		body.inlineImages = removeInlineDataImages(html);
		QTextDocument document;
		document.setDocumentMargin(0);
		document.setHtml(html.left(MaxBodyCharacters));
		QString rich;
		QString plain;
		bool formatted = false;
		int segments   = 0;
		for (QTextBlock textBlock = document.begin(); textBlock.isValid() && segments < MaxLegacyBodySegments;
			 textBlock            = textBlock.next()) {
			if (textBlock != document.begin()) {
				rich += QLatin1String("<br>");
				plain += QLatin1Char('\n');
			}
			for (QTextBlock::iterator it = textBlock.begin(); !it.atEnd() && segments < MaxLegacyBodySegments; ++it) {
				const QTextFragment fragment = it.fragment();
				if (!fragment.isValid()) {
					continue;
				}
				QString text = fragment.text();
				text.remove(QChar::ObjectReplacementCharacter);
				if (text.isEmpty()) {
					continue;
				}
				++segments;
				plain += text;
				const QTextCharFormat format = fragment.charFormat();
				QString part = text.toHtmlEscaped().replace(QChar::LineSeparator, QLatin1String("<br>"));
				const auto wrap = [&part, &formatted](const QString &tag) {
					part      = QStringLiteral("<%1>%2</%1>").arg(tag, part);
					formatted = true;
				};
				if (hasMonospaceFamily(format)) {
					wrap(QStringLiteral("code"));
				}
				if (format.fontWeight() >= QFont::DemiBold) {
					wrap(QStringLiteral("b"));
				}
				if (format.fontItalic()) {
					wrap(QStringLiteral("i"));
				}
				if (format.fontStrikeOut()) {
					wrap(QStringLiteral("s"));
				}
				if (format.isAnchor() && isSafeLink(format.anchorHref())) {
					part = QStringLiteral("<a href=\"%1\">%2</a>").arg(format.anchorHref().toHtmlEscaped(), part);
					formatted = true;
				}
				rich += part;
			}
		}
		body.plainText = plain.trimmed();
		if (!body.plainText.isEmpty()) {
			body.blocks << (formatted ? block(QStringLiteral("p"), QStringLiteral("rich"), rich)
									  : block(QStringLiteral("p"), QStringLiteral("plain"), body.plainText));
		}
		return body;
	}

	std::optional< QString > systemText(const QString &sourceText) {
		static const QRegularExpression systemPrefix(QStringLiteral(R"(^\[(stack|scope|system)\]\s*)"),
													 QRegularExpression::CaseInsensitiveOption);
		const QString trimmed = sourceText.trimmed();
		if (!systemPrefix.match(trimmed).hasMatch()) {
			return std::nullopt;
		}
		QString text = trimmed;
		text.remove(systemPrefix);
		return text.isEmpty() ? QObject::tr("System event") : text;
	}

	QString messageSourceText(const MumbleProto::ChatMessage &message) {
		if (message.has_body_text()) {
			return normalizedText(fromUtf8(message.body_text()));
		}
		return QTextDocumentFragment::fromHtml(fromUtf8(message.message())).toPlainText();
	}

	ChatTimelineMedia::Kind mediaKind(MumbleProto::ChatAssetKind kind) {
		switch (kind) {
			case MumbleProto::ChatAssetKindImage:
				return ChatTimelineMedia::Kind::Image;
			case MumbleProto::ChatAssetKindVideo:
				return ChatTimelineMedia::Kind::Video;
			case MumbleProto::ChatAssetKindAudio:
				return ChatTimelineMedia::Kind::Audio;
			case MumbleProto::ChatAssetKindDocument:
				return ChatTimelineMedia::Kind::Document;
			case MumbleProto::ChatAssetKindBinary:
			case MumbleProto::ChatAssetKindUnknown:
			default:
				return ChatTimelineMedia::Kind::Binary;
		}
	}

	bool isVisualMedia(ChatTimelineMedia::Kind kind) {
		return kind == ChatTimelineMedia::Kind::Image || kind == ChatTimelineMedia::Kind::Video
			   || kind == ChatTimelineMedia::Kind::InlineImage;
	}

	QString actorKey(const ChatTimelineRow &row) {
		return row.authorUserId > 0 ? QStringLiteral("u:%1").arg(row.authorUserId)
									: QStringLiteral("n:%1").arg(row.authorName);
	}
} // namespace

ChatRowBuilder::ChatRowBuilder(Options options) : m_options(std::move(options)) {
}

ChatRowBuilder::Body ChatRowBuilder::buildBody(const QString &text, bool markdown) {
	const QString normalized = normalizedText(text).left(MaxBodyCharacters);
	Body body;
	if (normalized.trimmed().isEmpty()) {
		return body;
	}
	if (markdown) {
		body.blocks    = markdownBlocks(normalized);
		body.plainText = normalized;
		return body;
	}
	if (Qt::mightBeRichText(normalized)) {
		return legacyHtmlBody(normalized);
	}
	body.plainText = normalized;
	body.blocks << (normalized.contains(QLatin1String("http"), Qt::CaseInsensitive)
						? block(QStringLiteral("p"), QStringLiteral("styled"), linkedPlainTextHtml(normalized))
						: block(QStringLiteral("p"), QStringLiteral("plain"), normalized));
	return body;
}

ChatTimelineRow ChatRowBuilder::build(const MumbleProto::ChatMessage &message) const {
	ChatTimelineRow row;
	row.key.createdAt     = message.has_created_at() ? message.created_at() : 0;
	row.key.threadId      = message.thread_id();
	row.key.messageId     = message.message_id();
	row.authorUserId      = message.actor_user_id();
	row.authorSession     = message.actor();
	row.authorName        = fromUtf8(message.actor_name());
	row.authorTextureHash = QByteArray::fromStdString(message.actor_texture_hash());
	row.own               = (m_options.ownUserId > 0 && row.authorUserId == m_options.ownUserId)
			  || (row.authorUserId == 0 && m_options.ownSession > 0 && row.authorSession == m_options.ownSession);
	row.editedAt = message.edited_at();
	if (message.reply_to_message_id() > 0) {
		row.replyToMessageId = message.reply_to_message_id();
		row.replyActor       = fromUtf8(message.reply_actor_name());
		row.replySnippet     = fromUtf8(message.reply_snippet());
	}

	const QString source = messageSourceText(message);
	if (const std::optional< QString > system = systemText(source)) {
		row.kind       = ChatTimelineRowKind::System;
		row.plainText  = *system;
		row.bodyBlocks = { block(QStringLiteral("p"), QStringLiteral("plain"), *system) };
		return row;
	}

	const bool markdown =
		message.has_body_format() && message.body_format() == MumbleProto::ChatBodyFormatMarkdownLite;
	const Body body = buildBody(message.has_body_text() ? source : fromUtf8(message.message()),
								markdown && message.has_body_text());
	row.bodyBlocks = body.blocks;
	row.plainText  = body.plainText;

	for (const MumbleProto::ChatAssetRef &asset : message.attachments()) {
		ChatTimelineMedia media;
		media.kind           = mediaKind(asset.kind());
		media.assetId        = asset.asset_id();
		media.previewAssetId = asset.preview_asset_id();
		media.fileName       = fromUtf8(asset.filename());
		media.mime           = fromUtf8(asset.mime());
		media.byteSize       = asset.byte_size();
		media.durationMs     = asset.duration_ms();
		media.width          = static_cast< int >(asset.width());
		media.height         = static_cast< int >(asset.height());
		media.geometryKnown  = media.width > 0 && media.height > 0;
		media.inlineSafe     = asset.inline_safe();
		row.media << media;
	}
	for (int i = 0; i < body.inlineImages; ++i) {
		ChatTimelineMedia media;
		media.kind = ChatTimelineMedia::Kind::InlineImage;
		row.media << media;
	}

	const auto visual = std::count_if(row.media.cbegin(), row.media.cend(),
									  [](const ChatTimelineMedia &media) { return isVisualMedia(media.kind); });
	if (visual > 1) {
		row.kind = ChatTimelineRowKind::Gallery;
	} else if (visual == 1) {
		const auto it = std::find_if(row.media.cbegin(), row.media.cend(),
									 [](const ChatTimelineMedia &media) { return isVisualMedia(media.kind); });
		row.kind      = it->kind == ChatTimelineMedia::Kind::Video ? ChatTimelineRowKind::Video
																   : ChatTimelineRowKind::Media;
	} else if (!row.media.isEmpty()) {
		row.kind = ChatTimelineRowKind::File;
	}

	if (row.kind == ChatTimelineRowKind::Text) {
		for (const MumbleProto::ChatEmbedRef &embed : message.embeds()) {
			if (embed.status() != MumbleProto::ChatEmbedStatusReady
				&& embed.status() != MumbleProto::ChatEmbedStatusPending) {
				continue;
			}
			ChatTimelineCard &card = row.card;
			card.status            = embed.status() == MumbleProto::ChatEmbedStatusReady
							  ? ChatTimelineCard::Status::Ready
							  : ChatTimelineCard::Status::Pending;
			card.url            = fromUtf8(embed.canonical_url());
			card.siteName       = fromUtf8(embed.site_name());
			card.title          = fromUtf8(embed.title());
			card.description    = fromUtf8(embed.description());
			card.previewAssetId = embed.preview_asset_id();
			// Embeds carry no preview dimensions yet, so a thumbnail gets the fixed
			// small slot; the image is fitted into it.
			card.layout = card.previewAssetId > 0 ? ChatTimelineCard::Layout::SmallThumbnail
												  : ChatTimelineCard::Layout::Compact;
			row.kind    = ChatTimelineRowKind::LinkCard;
			break;
		}
	}

	for (const MumbleProto::ChatReactionAggregate &aggregate : message.reactions()) {
		ChatTimelineReaction reaction;
		reaction.emoji = fromUtf8(aggregate.emoji());
		reaction.count = aggregate.count();
		reaction.self  = aggregate.self_reacted();
		for (const std::string &actor : aggregate.actor_names()) {
			reaction.actorNames << fromUtf8(actor);
		}
		row.reactions << reaction;
	}
	return row;
}

void ChatRowBuilder::decorate(ChatTimelineRow &row, const ChatTimelineRow *older) const {
	const qint64 createdAtMs = static_cast< qint64 >(row.key.createdAt) * 1000;
	const QDateTime when     = QDateTime::fromMSecsSinceEpoch(createdAtMs).toLocalTime();
	row.timeText             = row.key.createdAt > 0 ? m_options.locale.toString(when.time(), QLocale::ShortFormat)
														  : QString();

	bool dateBoundary = !older;
	if (older && row.key.createdAt > 0 && older->key.createdAt > 0) {
		const QDate olderDate =
			QDateTime::fromMSecsSinceEpoch(static_cast< qint64 >(older->key.createdAt) * 1000).toLocalTime().date();
		dateBoundary = olderDate != when.date();
	} else if (row.key.createdAt == 0) {
		dateBoundary = false;
	}
	row.dateLabel = dateBoundary ? m_options.locale.toString(when.date(), QLocale::LongFormat) : QString();

	row.groupStart = !older || dateBoundary || row.kind == ChatTimelineRowKind::System
					 || older->kind == ChatTimelineRowKind::System || actorKey(row) != actorKey(*older)
					 || row.own != older->own
					 || (row.key.createdAt > 0 && older->key.createdAt > 0
						 && (static_cast< qint64 >(row.key.createdAt) - static_cast< qint64 >(older->key.createdAt))
									* 1000
								> GroupGapMs);
}

QVector< ChatTimelineRow > ChatRowBuilder::buildRange(const QVector< MumbleProto::ChatMessage > &messages, int first,
													  int last) const {
	QVector< ChatTimelineRow > rows;
	first = std::max(0, first);
	last  = std::min(last, static_cast< int >(messages.size()) - 1);
	if (first > last) {
		return rows;
	}
	rows.reserve(last - first + 1);
	ChatTimelineRow context;
	const bool hasContext = first > 0;
	if (hasContext) {
		context = build(messages.at(first - 1));
	}
	for (int i = first; i <= last; ++i) {
		ChatTimelineRow row = build(messages.at(i));
		decorate(row, rows.isEmpty() ? (hasContext ? &context : nullptr) : &rows.constLast());
		rows.push_back(std::move(row));
	}
	return rows;
}
