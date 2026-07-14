// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "QmlVisualFixtureController.h"

#include "QmlClientModels.h"
#include "QmlImageProvider.h"
#include "QmlShellHost.h"
#include "QmlThemeController.h"

#include <QtCore/QElapsedTimer>
#include <QtCore/QEventLoop>
#include <QtCore/QTimer>
#include <QtGui/QLinearGradient>
#include <QtGui/QPainter>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>

namespace {
	QQuickItem *quickItemByObjectName(QQuickItem *root, const QString &objectName) {
		if (!root || objectName.isEmpty()) return nullptr;
		if (root->objectName() == objectName) return root;
		for (QQuickItem *child : root->childItems()) {
			if (QQuickItem *match = quickItemByObjectName(child, objectName)) return match;
		}
		return nullptr;
	}

	QStringList supportedMotdVariants() {
		return { QStringLiteral("none"), QStringLiteral("expanded"), QStringLiteral("collapsed"),
				 QStringLiteral("changed"), QStringLiteral("history-hidden") };
	}

	QStringList supportedRichPreviewVariants() {
		return { QStringLiteral("none"), QStringLiteral("youtube"), QStringLiteral("spotify"),
				 QStringLiteral("tiktok"), QStringLiteral("instagram"), QStringLiteral("finance"),
				 QStringLiteral("audio"), QStringLiteral("product"), QStringLiteral("steam"),
				 QStringLiteral("google"), QStringLiteral("twitch"), QStringLiteral("flashback"),
				 QStringLiteral("loading"), QStringLiteral("error") };
	}

	QString expectedRichPreviewAspect(const QString &variant) {
		if (variant == QLatin1String("youtube")) return QStringLiteral("wide");
		if (variant == QLatin1String("spotify")) return QStringLiteral("compact-audio");
		if (variant == QLatin1String("tiktok")) return QStringLiteral("short");
		if (variant == QLatin1String("instagram")) return QStringLiteral("square");
		if (variant == QLatin1String("twitch")) return QStringLiteral("wide");
		return {};
	}

	QString registerVisualPreviewImage(QmlShellHost *host, const QString &variant, const QString &title,
									 const QString &subtitle, const QColor &start, const QColor &end,
									 const QSize &size = QSize(960, 540)) {
		Q_UNUSED(title);
		Q_UNUSED(subtitle);
		if (!host || !host->imagePipeline() || !size.isValid()) return {};
		QImage image(size, QImage::Format_ARGB32_Premultiplied);
		image.fill(QColor(QStringLiteral("#05070a")));
		QPainter painter(&image);
		painter.setRenderHint(QPainter::Antialiasing, true);
		QLinearGradient gradient(QPointF(0, 0), QPointF(size.width(), size.height()));
		gradient.setColorAt(0.0, start);
		gradient.setColorAt(1.0, end);
		painter.fillRect(image.rect(), gradient);
		painter.setPen(QColor(255, 255, 255, 42));
		painter.setBrush(Qt::NoBrush);
		const qreal margin = qMax< qreal >(24.0, qMin(size.width(), size.height()) * 0.055);
		painter.drawRoundedRect(QRectF(margin, margin, size.width() - margin * 2,
									 size.height() - margin * 2), 24, 24);
		const qreal unit = qMin(size.width(), size.height());
		painter.setPen(Qt::NoPen);
		painter.setBrush(QColor(255, 255, 255, 24));
		painter.drawEllipse(QRectF(size.width() * 0.58, -unit * 0.16, unit * 0.72, unit * 0.72));
		painter.setBrush(QColor(255, 255, 255, 34));
		painter.drawRoundedRect(QRectF(margin * 1.7, size.height() * 0.66,
									 qMin(size.width() * 0.34, unit * 0.82), qMax< qreal >(8.0, unit * 0.035)),
							 8, 8);
		painter.setBrush(QColor(255, 255, 255, 18));
		painter.drawRoundedRect(QRectF(margin * 1.7, size.height() * 0.73,
									 qMin(size.width() * 0.22, unit * 0.58), qMax< qreal >(6.0, unit * 0.024)),
							 6, 6);
		painter.end();
		return host->imagePipeline()->registerImage(image, QStringLiteral("visual-preview:%1").arg(variant));
	}

	QVariantMap visualRichPreview(QmlShellHost *host, const QString &variant, const QString &size) {
		if (variant == QLatin1String("none")) return {};
		QVariantMap preview {
			{ QStringLiteral("kind"), QStringLiteral("link") },
			{ QStringLiteral("state"), QStringLiteral("ready") },
			{ QStringLiteral("loading"), false }, { QStringLiteral("failed"), false },
			{ QStringLiteral("previewSize"), size }
		};
		const auto attachImage = [&preview](const QString &source, const QString &title) {
			if (source.isEmpty()) return;
			preview.insert(QStringLiteral("thumbnailUrl"), source);
			preview.insert(QStringLiteral("mediaItems"), QVariantList { QVariantMap {
				{ QStringLiteral("kind"), QStringLiteral("image") },
				{ QStringLiteral("mime"), QStringLiteral("image/png") },
				{ QStringLiteral("url"), source }, { QStringLiteral("title"), title }
			} });
		};

		if (variant == QLatin1String("loading") || variant == QLatin1String("error")) {
			const bool failed = variant == QLatin1String("error");
			preview.insert(QStringLiteral("url"), failed
				? QStringLiteral("https://example.com/preview-unavailable")
				: QStringLiteral("https://example.com/preview-loading"));
			preview.insert(QStringLiteral("title"), failed
				? QStringLiteral("Preview unavailable") : QStringLiteral("Fetching link preview"));
			preview.insert(QStringLiteral("subtitle"), QStringLiteral("example.com"));
			preview.insert(QStringLiteral("description"), failed
				? QStringLiteral("The provider returned a deterministic error.") : QString());
			preview.insert(QStringLiteral("loadingLabel"), QStringLiteral("Fetching preview"));
			preview.insert(QStringLiteral("loading"), !failed);
			preview.insert(QStringLiteral("failed"), failed);
			preview.insert(QStringLiteral("openLabel"), QStringLiteral("Open link"));
			return preview;
		}

		if (variant == QLatin1String("youtube")) {
			const QString title = QStringLiteral("Qt Quick media preview");
			preview.insert(QStringLiteral("url"), QStringLiteral("https://www.youtube.com/watch?v=fixture"));
			preview.insert(QStringLiteral("title"), title);
			preview.insert(QStringLiteral("subtitle"), QStringLiteral("YouTube · Mumble Design"));
			preview.insert(QStringLiteral("description"), QStringLiteral("A media-first card with native playback actions."));
			preview.insert(QStringLiteral("openLabel"), QStringLiteral("Open on YouTube"));
			preview.insert(QStringLiteral("embedKind"), QStringLiteral("youtube"));
			preview.insert(QStringLiteral("embedUrl"), QStringLiteral("https://www.youtube.com/embed/fixture"));
			preview.insert(QStringLiteral("embedAspect"), QStringLiteral("wide"));
			attachImage(registerVisualPreviewImage(host, variant, title, QStringLiteral("Media-first 16:9 fixture"),
				QColor(QStringLiteral("#8b1538")), QColor(QStringLiteral("#243b55"))), title);
			return preview;
		}
		if (variant == QLatin1String("spotify")) {
			const QString title = QStringLiteral("Native client mix");
			preview.insert(QStringLiteral("url"), QStringLiteral("https://open.spotify.com/track/fixture"));
			preview.insert(QStringLiteral("title"), title);
			preview.insert(QStringLiteral("subtitle"), QStringLiteral("Spotify · Mumble Sessions"));
			preview.insert(QStringLiteral("description"), QStringLiteral("Provider-owned audio controls stay inside the embed."));
			preview.insert(QStringLiteral("openLabel"), QStringLiteral("Open on Spotify"));
			preview.insert(QStringLiteral("embedKind"), QStringLiteral("spotify"));
			preview.insert(QStringLiteral("embedUrl"), QStringLiteral("https://open.spotify.com/embed/track/fixture123"));
			preview.insert(QStringLiteral("embedAspect"), QStringLiteral("compact-audio"));
			attachImage(registerVisualPreviewImage(host, variant, title, QStringLiteral("Audio provider fixture"),
				QColor(QStringLiteral("#12633c")), QColor(QStringLiteral("#10151c")), QSize(720, 720)), title);
			return preview;
		}
		if (variant == QLatin1String("tiktok")) {
			const QString title = QStringLiteral("Vertical creator preview");
			preview.insert(QStringLiteral("url"), QStringLiteral("https://www.tiktok.com/@mumble/video/fixture"));
			preview.insert(QStringLiteral("title"), title);
			preview.insert(QStringLiteral("subtitle"), QStringLiteral("TikTok · @mumble"));
			preview.insert(QStringLiteral("description"), QStringLiteral("A centered 9:16 provider surface."));
			preview.insert(QStringLiteral("openLabel"), QStringLiteral("Open on TikTok"));
			preview.insert(QStringLiteral("embedKind"), QStringLiteral("tiktok"));
			preview.insert(QStringLiteral("embedUrl"), QStringLiteral("https://www.tiktok.com/player/v1/fixture"));
			preview.insert(QStringLiteral("embedAspect"), QStringLiteral("short"));
			attachImage(registerVisualPreviewImage(host, variant, title, QStringLiteral("Vertical 9:16 fixture"),
				QColor(QStringLiteral("#18181b")), QColor(QStringLiteral("#a11c59")), QSize(720, 1280)), title);
			return preview;
		}
		if (variant == QLatin1String("instagram")) {
			const QString title = QStringLiteral("Post by @mumblequick");
			const QString image = registerVisualPreviewImage(host, variant, title, QStringLiteral("Square social fixture"),
				QColor(QStringLiteral("#5b247a")), QColor(QStringLiteral("#d17c45")), QSize(900, 900));
			preview.insert(QStringLiteral("url"), QStringLiteral("https://www.instagram.com/p/fixture/"));
			preview.insert(QStringLiteral("title"), title);
			preview.insert(QStringLiteral("subtitle"), QStringLiteral("Instagram"));
			preview.insert(QStringLiteral("description"), QStringLiteral("Identity, caption, media, and actions share one hierarchy."));
			preview.insert(QStringLiteral("openLabel"), QStringLiteral("Open on Instagram"));
			preview.insert(QStringLiteral("embedKind"), QStringLiteral("instagram"));
			preview.insert(QStringLiteral("embedUrl"), QStringLiteral("https://www.instagram.com/p/fixture/embed"));
			preview.insert(QStringLiteral("embedAspect"), QStringLiteral("square"));
			preview.insert(QStringLiteral("metadata"), QVariantMap {
				{ QStringLiteral("provider"), QStringLiteral("instagram") },
				{ QStringLiteral("instagramDisplayName"), QStringLiteral("Mumble Quick") },
				{ QStringLiteral("instagramHandle"), QStringLiteral("mumblequick") },
				{ QStringLiteral("instagramCaption"), QStringLiteral("Native previews, consistent in every theme.") },
				{ QStringLiteral("instagramMediaKind"), QStringLiteral("post") },
				{ QStringLiteral("instagramLikeCount"), 2480 },
				{ QStringLiteral("instagramCommentCount"), 86 },
				{ QStringLiteral("instagramAvatarUrl"), image }
			});
			attachImage(image, title);
			return preview;
		}
		if (variant == QLatin1String("finance")) {
			preview.insert(QStringLiteral("url"), QStringLiteral("https://finance.yahoo.com/quote/MSFT"));
			preview.insert(QStringLiteral("title"), QStringLiteral("Microsoft Corporation (MSFT)"));
			preview.insert(QStringLiteral("subtitle"), QStringLiteral("Yahoo Finance"));
			preview.insert(QStringLiteral("description"), QStringLiteral("448.37 USD · +5.21 (+1.18%)"));
			preview.insert(QStringLiteral("openLabel"), QStringLiteral("Open on Yahoo Finance"));
			preview.insert(QStringLiteral("metadata"), QVariantMap {
				{ QStringLiteral("provider"), QStringLiteral("yahoo-finance") },
				{ QStringLiteral("previewProvider"), QStringLiteral("yahoo-finance") },
				{ QStringLiteral("providerName"), QStringLiteral("Yahoo Finance") },
				{ QStringLiteral("tickerSymbol"), QStringLiteral("MSFT") },
				{ QStringLiteral("financeName"), QStringLiteral("Microsoft Corporation") },
				{ QStringLiteral("financePrice"), QStringLiteral("448.37") },
				{ QStringLiteral("financeCurrency"), QStringLiteral("USD") },
				{ QStringLiteral("financeDayChange"), QStringLiteral("+5.21") },
				{ QStringLiteral("financeDayChangePercent"), QStringLiteral("+1.18%") },
				{ QStringLiteral("financeDayTrend"), QStringLiteral("up") },
				{ QStringLiteral("financeExchange"), QStringLiteral("NasdaqGS") },
				{ QStringLiteral("financeInstrument"), QStringLiteral("EQUITY") },
				{ QStringLiteral("financeRangeLabel"), QStringLiteral("1M") },
				{ QStringLiteral("financeRangeChangePercent"), QStringLiteral("+2.85%") },
				{ QStringLiteral("financeRangeTrend"), QStringLiteral("up") },
				{ QStringLiteral("financeSparkline"), QVariantList {
					QVariantMap { { QStringLiteral("timestamp"), 1 }, { QStringLiteral("close"), 435.95 } },
					QVariantMap { { QStringLiteral("timestamp"), 2 }, { QStringLiteral("close"), 441.10 } },
					QVariantMap { { QStringLiteral("timestamp"), 3 }, { QStringLiteral("close"), 438.72 } },
					QVariantMap { { QStringLiteral("timestamp"), 4 }, { QStringLiteral("close"), 448.37 } }
				} }
			});
			return preview;
		}
		if (variant == QLatin1String("audio")) {
			const QString title = QStringLiteral("Vetenskapsradion");
			const QString artwork = registerVisualPreviewImage(host, variant, title,
				QStringLiteral("Sveriges Radio audio fixture"), QColor(QStringLiteral("#e65100")),
				QColor(QStringLiteral("#ffb74d")), QSize(720, 720));
			preview.insert(QStringLiteral("url"), QStringLiteral("https://sverigesradio.se/avsnitt/fixture"));
			preview.insert(QStringLiteral("title"), QStringLiteral("Så blir framtidens datacenter mer energieffektiva"));
			preview.insert(QStringLiteral("subtitle"), QStringLiteral("Vetenskapsradion · Sveriges Radio"));
			preview.insert(QStringLiteral("description"), QStringLiteral("A native audio card with clear programme hierarchy."));
			preview.insert(QStringLiteral("openLabel"), QStringLiteral("Open on Sveriges Radio"));
			preview.insert(QStringLiteral("metadata"), QVariantMap {
				{ QStringLiteral("provider"), QStringLiteral("sveriges-radio") },
				{ QStringLiteral("previewKind"), QStringLiteral("audio") },
				{ QStringLiteral("providerName"), QStringLiteral("Sveriges Radio") },
				{ QStringLiteral("audioProvider"), QStringLiteral("Sveriges Radio") },
				{ QStringLiteral("audioProgram"), QStringLiteral("Vetenskapsradion") },
				{ QStringLiteral("articlePublishedAt"), QStringLiteral("28 May 2026") }
			});
			attachImage(artwork, title);
			return preview;
		}
		if (variant == QLatin1String("steam")) {
			const QString title = QStringLiteral("Hades II");
			const QString hero = registerVisualPreviewImage(host, variant, title,
				QStringLiteral("Steam store fixture"), QColor(QStringLiteral("#123c69")),
				QColor(QStringLiteral("#66c0f4")));
			preview.insert(QStringLiteral("url"), QStringLiteral("https://store.steampowered.com/app/fixture"));
			preview.insert(QStringLiteral("title"), title);
			preview.insert(QStringLiteral("subtitle"), QStringLiteral("Steam"));
			preview.insert(QStringLiteral("description"),
				QStringLiteral("A native store card with price, reviews, platforms, and release details."));
			preview.insert(QStringLiteral("openLabel"), QStringLiteral("Open on Steam"));
			preview.insert(QStringLiteral("metadata"), QVariantMap {
				{ QStringLiteral("previewProvider"), QStringLiteral("game-store") },
				{ QStringLiteral("previewKind"), QStringLiteral("gameStoreProduct") },
				{ QStringLiteral("gameStoreProvider"), QStringLiteral("steam") },
				{ QStringLiteral("gameStoreName"), QStringLiteral("Steam") },
				{ QStringLiteral("gameStoreDescription"),
					QStringLiteral("Battle beyond the Underworld in this deterministic visual fixture.") },
				{ QStringLiteral("steamPrice"), QStringLiteral("29,99 €") },
				{ QStringLiteral("steamOriginalPrice"), QStringLiteral("39,99 €") },
				{ QStringLiteral("steamDiscountPercent"), 25 },
				{ QStringLiteral("steamPlatforms"), QStringLiteral("Windows") },
				{ QStringLiteral("steamReviewSummary"), QStringLiteral("Very Positive") },
				{ QStringLiteral("steamReviewPercent"), 92 },
				{ QStringLiteral("steamReviewTotal"), 58420 },
				{ QStringLiteral("steamDeveloper"), QStringLiteral("Supergiant Games") },
				{ QStringLiteral("steamReleaseDate"), QStringLiteral("6 May 2024") },
				{ QStringLiteral("steamMetacriticScore"), 86 },
				{ QStringLiteral("gameStoreTags"),
					QVariantList { QStringLiteral("Action roguelike"), QStringLiteral("Mythology") } }
			});
			attachImage(hero, title);
			return preview;
		}
		if (variant == QLatin1String("google")) {
			preview.insert(QStringLiteral("url"),
				QStringLiteral("https://www.google.com/search?q=Qt+Quick+model+performance"));
			preview.insert(QStringLiteral("title"), QStringLiteral("Qt Quick model performance"));
			preview.insert(QStringLiteral("subtitle"), QStringLiteral("Google Search"));
			preview.insert(QStringLiteral("description"),
				QStringLiteral("Search results open externally without loading remote content in the fixture."));
			preview.insert(QStringLiteral("openLabel"), QStringLiteral("Open Google Search"));
			preview.insert(QStringLiteral("metadata"), QVariantMap {
				{ QStringLiteral("provider"), QStringLiteral("google-search") },
				{ QStringLiteral("previewKind"), QStringLiteral("googleSearch") },
				{ QStringLiteral("providerName"), QStringLiteral("Google") },
				{ QStringLiteral("googleSearchQuery"), QStringLiteral("Qt Quick model performance") },
				{ QStringLiteral("googleSearchMode"), QStringLiteral("web") },
				{ QStringLiteral("googleSearchModeLabel"), QStringLiteral("Google Search") }
			});
			return preview;
		}
		if (variant == QLatin1String("twitch")) {
			const QString title = QStringLiteral("Mumble Dev is live");
			preview.insert(QStringLiteral("url"), QStringLiteral("https://www.twitch.tv/mumbledev"));
			preview.insert(QStringLiteral("title"), title);
			preview.insert(QStringLiteral("subtitle"), QStringLiteral("Twitch · mumbledev"));
			preview.insert(QStringLiteral("description"),
				QStringLiteral("A live provider card with native identity and playback affordances."));
			preview.insert(QStringLiteral("openLabel"), QStringLiteral("Open on Twitch"));
			preview.insert(QStringLiteral("embedKind"), QStringLiteral("twitch"));
			preview.insert(QStringLiteral("embedUrl"),
				QStringLiteral("https://player.twitch.tv/?channel=mumbledev&parent=localhost"));
			preview.insert(QStringLiteral("embedAspect"), QStringLiteral("wide"));
			preview.insert(QStringLiteral("metadata"), QVariantMap {
				{ QStringLiteral("provider"), QStringLiteral("twitch") },
				{ QStringLiteral("previewKind"), QStringLiteral("twitch") },
				{ QStringLiteral("providerName"), QStringLiteral("Twitch") },
				{ QStringLiteral("twitchDisplayName"), QStringLiteral("Mumble Dev") },
				{ QStringLiteral("twitchChannel"), QStringLiteral("mumbledev") },
				{ QStringLiteral("twitchLiveState"), QStringLiteral("live") },
				{ QStringLiteral("twitchBadge"), QStringLiteral("Live") },
				{ QStringLiteral("twitchGame"), QStringLiteral("Software and Game Development") },
				{ QStringLiteral("twitchViewerCount"), 12500 },
				{ QStringLiteral("twitchEmbedMode"), QStringLiteral("Live player") },
				{ QStringLiteral("twitchPlaybackNote"),
					QStringLiteral("Provider playback starts only after explicit interaction.") }
			});
			attachImage(registerVisualPreviewImage(host, variant, title,
				QStringLiteral("Twitch live fixture"), QColor(QStringLiteral("#3d1f70")),
				QColor(QStringLiteral("#a970ff"))), title);
			return preview;
		}
		if (variant == QLatin1String("flashback")) {
			preview.insert(QStringLiteral("url"),
				QStringLiteral("https://www.flashback.org/tfixturep42"));
			preview.insert(QStringLiteral("title"), QStringLiteral("Qt Quick render loops and fluency"));
			preview.insert(QStringLiteral("subtitle"), QStringLiteral("Flashback · Programmering"));
			preview.insert(QStringLiteral("description"),
				QStringLiteral("Thread context, linked post, and quoted reply stay in one native card."));
			preview.insert(QStringLiteral("openLabel"), QStringLiteral("Open on Flashback"));
			preview.insert(QStringLiteral("metadata"), QVariantMap {
				{ QStringLiteral("provider"), QStringLiteral("flashback") },
				{ QStringLiteral("previewKind"), QStringLiteral("forum") },
				{ QStringLiteral("providerName"), QStringLiteral("Flashback") },
				{ QStringLiteral("forumProvider"), QStringLiteral("Flashback") },
				{ QStringLiteral("forumThreadId"), QStringLiteral("fixture") },
				{ QStringLiteral("forumThreadTitle"), QStringLiteral("Qt Quick render loops and fluency") },
				{ QStringLiteral("forumCategory"), QStringLiteral("Dator") },
				{ QStringLiteral("forumName"), QStringLiteral("Programmering") },
				{ QStringLiteral("forumPage"), QStringLiteral("42") },
				{ QStringLiteral("forumPageCount"), QStringLiteral("73") },
				{ QStringLiteral("forumPostNumber"), QStringLiteral("#628") },
				{ QStringLiteral("forumPostAuthor"), QStringLiteral("rendernisse") },
				{ QStringLiteral("forumPostTime"), QStringLiteral("Today 12:00") },
				{ QStringLiteral("forumPostExcerpt"),
					QStringLiteral("Frame pacing stays smooth when model updates remain incremental.") },
				{ QStringLiteral("forumQuoteAuthor"), QStringLiteral("qmlvän") },
				{ QStringLiteral("forumQuotePostNumber"), QStringLiteral("#627") },
				{ QStringLiteral("forumQuoteExcerpt"),
					QStringLiteral("Keep every provider card in the same visual system.") },
				{ QStringLiteral("forumPostCount"), 628 }
			});
			return preview;
		}

		const QString title = QStringLiteral("Logitech G Pro X Superlight 2");
		const QString product = registerVisualPreviewImage(host, variant, title, QStringLiteral("Product fixture"),
			QColor(QStringLiteral("#086f83")), QColor(QStringLiteral("#49c5b6")));
		preview.insert(QStringLiteral("url"), QStringLiteral("https://www.inet.se/produkt/fixture"));
		preview.insert(QStringLiteral("title"), title);
		preview.insert(QStringLiteral("subtitle"), QStringLiteral("Inet"));
		preview.insert(QStringLiteral("description"), QStringLiteral("Wireless mouse · 60 g · LIGHTSPEED"));
		preview.insert(QStringLiteral("openLabel"), QStringLiteral("Open on Inet"));
		preview.insert(QStringLiteral("metadata"), QVariantMap {
			{ QStringLiteral("provider"), QStringLiteral("inet") },
			{ QStringLiteral("previewProvider"), QStringLiteral("inet") },
			{ QStringLiteral("previewKind"), QStringLiteral("product") },
			{ QStringLiteral("providerName"), QStringLiteral("Inet") },
			{ QStringLiteral("productTitle"), title },
			{ QStringLiteral("productPrice"), QStringLiteral("1 499 kr") },
			{ QStringLiteral("productAvailability"), QStringLiteral("In stock online") },
			{ QStringLiteral("productRating"), QStringLiteral("4.7/5 · 128 reviews") },
			{ QStringLiteral("productSku"), QStringLiteral("910-006630") },
			{ QStringLiteral("productImage"), product }
		});
		attachImage(product, title);
		return preview;
	}
}

QmlVisualFixtureController::QmlVisualFixtureController(QmlShellHost *host) : m_host(host) {
}

double QmlVisualFixtureController::actualDevicePixelRatio() const {
	return m_host && m_host->window() ? m_host->window()->devicePixelRatio() : 0.0;
}

QVariantMap QmlVisualFixtureController::capabilities() const {
	const bool available = m_host && m_host->window();
	return { { QStringLiteral("capture"), available }, { QStringLiteral("state_injection"), available },
			 { QStringLiteral("window_resize"), available }, { QStringLiteral("theme_override"), available },
			 { QStringLiteral("accessibility_snapshot"), available },
			 { QStringLiteral("supported_states"), QStringList { QStringLiteral("empty"), QStringLiteral("loading"),
														 QStringLiteral("error"), QStringLiteral("connected") } },
			 { QStringLiteral("supported_motd_variants"), supportedMotdVariants() },
			 { QStringLiteral("supported_rich_preview_variants"), supportedRichPreviewVariants() },
			 { QStringLiteral("actual_device_pixel_ratio"), actualDevicePixelRatio() } };
}

QVariantMap QmlVisualFixtureController::apply(const QVariantMap &request, QString *error) {
	if (!m_host || !m_host->window()) {
		if (error) *error = QStringLiteral("The Qt Quick frontend is not active.");
		return {};
	}
	const QString state = request.value(QStringLiteral("state")).toString().trimmed().toLower();
	const QString theme = request.value(QStringLiteral("theme")).toString().trimmed().toLower();
	const QString layout = request.value(QStringLiteral("layout")).toString().trimmed().toLower();
	const QString requestedMotdVariant = request.value(QStringLiteral("motd_variant")).toString().trimmed().toLower();
	const QString motdVariant = requestedMotdVariant.isEmpty() ? QStringLiteral("none") : requestedMotdVariant;
	const QString requestedRichPreviewVariant =
		request.value(QStringLiteral("rich_preview_variant")).toString().trimmed().toLower();
	const QString richPreviewVariant = requestedRichPreviewVariant.isEmpty()
		? QStringLiteral("none") : requestedRichPreviewVariant;
	const QString requestedRichPreviewSize =
		request.value(QStringLiteral("rich_preview_size")).toString().trimmed().toLower();
	const QString richPreviewSize = requestedRichPreviewSize.isEmpty()
		? QStringLiteral("default") : requestedRichPreviewSize;
	const QString caseId = request.value(QStringLiteral("case_id")).toString().trimmed();
	const int width = request.value(QStringLiteral("width")).toInt();
	const int height = request.value(QStringLiteral("height")).toInt();
	if (caseId.isEmpty() || !QStringList { QStringLiteral("empty"), QStringLiteral("loading"),
										 QStringLiteral("error"), QStringLiteral("connected") }.contains(state)
		|| width < 420 || height < 520 || width > 4096 || height > 2160
		|| !QStringList { QStringLiteral("light"), QStringLiteral("dark"), QStringLiteral("custom") }.contains(theme)
		|| !QStringList { QStringLiteral("regular"), QStringLiteral("compact") }.contains(layout)
		|| !supportedMotdVariants().contains(motdVariant)
		|| !supportedRichPreviewVariants().contains(richPreviewVariant)
		|| !QStringList { QStringLiteral("compact"), QStringLiteral("default"), QStringLiteral("large") }
			.contains(richPreviewSize)
		|| (motdVariant != QLatin1String("none") && state != QLatin1String("connected"))
		|| (richPreviewVariant != QLatin1String("none") && state != QLatin1String("connected"))) {
		if (error) *error = QStringLiteral("The visual fixture request is invalid or unsupported.");
		return {};
	}

	const bool previousFixtureOverride = m_host->visualFixtureOverrideActive();
	m_host->setVisualFixtureOverrideActive(true);
	struct FixtureOverrideRollback {
		QmlShellHost *host;
		bool previous;
		bool committed = false;
		~FixtureOverrideRollback() {
			if (!committed) host->setVisualFixtureOverrideActive(previous);
		}
	} fixtureOverrideRollback { m_host, previousFixtureOverride };
	QQuickWindow *window = m_host->window();
	const int expectedMessageCount = state == QLatin1String("connected") ? 2 : 0;
	{
		struct MutationScope {
			QmlShellHost *host;
			explicit MutationScope(QmlShellHost *value) : host(value) { host->setVisualFixtureMutationActive(true); }
			~MutationScope() { host->setVisualFixtureMutationActive(false); }
		} mutationScope(m_host);
		if (!m_host->themeController()->applyVisualGateAppearance(theme, layout)) {
			if (error) *error = QStringLiteral("The visual fixture appearance is unsupported.");
			return {};
		}
		window->setWidth(width);
		window->setHeight(height);
		applyState(state, motdVariant, richPreviewVariant, richPreviewSize);
		if (m_host->chatModel()->rowCount() != expectedMessageCount) {
			if (error) {
				*error = QStringLiteral("Visual fixture timeline contains %1 messages immediately after injection; expected %2.")
						 .arg(m_host->chatModel()->rowCount())
						 .arg(expectedMessageCount);
			}
			return {};
		}
		const bool expectMotd = motdVariant != QLatin1String("none");
		const bool expectExpanded = expectMotd && motdVariant != QLatin1String("collapsed");
		const bool expectChanged = motdVariant == QLatin1String("changed");
		const bool expectUserHistory = state == QLatin1String("connected")
			&& (motdVariant == QLatin1String("none") || motdVariant == QLatin1String("history-hidden"));
		if (m_host->sessionController()->hasMotd() != expectMotd
			|| (expectMotd && m_host->sessionController()->motdExpanded() != expectExpanded)
			|| m_host->sessionController()->motdChanged() != expectChanged
			|| m_host->chatModel()->hasUserHistory() != expectUserHistory) {
			if (error) *error = QStringLiteral("The visual fixture could not establish the requested MOTD variant.");
			return {};
		}
	}
	window->requestActivate();
	// Resizing from a compact saved geometry to a regular fixture can start the
	// Drawer's real close transition. The product surface is intentionally
	// disabled while any part of that modal remains on screen, so wait for the
	// transition to finish before establishing the case's deterministic focus.
	QElapsedTimer navigationCloseTimer;
	navigationCloseTimer.start();
	while (window->property("navigationModalActive").toBool() && navigationCloseTimer.elapsed() < 2000) {
		if (!waitForPresentedFrame(error)) return {};
	}
	if (window->property("navigationModalActive").toBool()) {
		if (error) *error = QStringLiteral("The visual fixture navigation drawer did not close before focus setup.");
		return {};
	}
	QQuickItem *requestedFocusItem = nullptr;
	QString requestedFocusName;
	bool requestedTargetOwnsFocus = false;
	constexpr int maximumFocusAttempts = 3;
	for (int attempt = 0; attempt < maximumFocusAttempts && !requestedTargetOwnsFocus; ++attempt) {
		QVariant focusTarget;
		if (!QMetaObject::invokeMethod(window, "focusVisualFixture", Q_RETURN_ARG(QVariant, focusTarget),
									  Q_ARG(QVariant, QVariant(state)))) {
			if (error) *error = QStringLiteral("The visual fixture could not establish a deterministic focus target.");
			return {};
		}
		requestedFocusName = focusTarget.toString().trimmed();
		requestedFocusItem = quickItemByObjectName(window->contentItem(), requestedFocusName);
		if (!requestedFocusItem || requestedFocusName.isEmpty()) {
			if (error) {
				*error = QStringLiteral("The visual fixture returned an invalid focus target "
								"(variant type=%1, object name='%2').")
						 .arg(QString::fromLatin1(focusTarget.typeName() ? focusTarget.typeName() : "<unknown>"),
							  requestedFocusName);
			}
			return {};
		}
		if (!waitForPresentedFrame(error)) return {};
		for (QQuickItem *item = window->activeFocusItem(); item; item = item->parentItem()) {
			if (item == requestedFocusItem) {
				requestedTargetOwnsFocus = true;
				break;
			}
		}
	}
	if (!requestedTargetOwnsFocus) {
		if (error) {
			QStringList activeFocusPath;
			for (QQuickItem *item = window->activeFocusItem(); item; item = item->parentItem()) {
				const QString name = item->objectName().trimmed();
				activeFocusPath.append(name.isEmpty()
					? QString::fromLatin1(item->metaObject()->className()) : name);
			}
			*error = QStringLiteral("The visual fixture focus target '%1' did not receive active focus "
								"(target visible=%2, enabled=%3; modal=%4, navigation modal=%5; active path: %6).")
					 .arg(requestedFocusName,
						  requestedFocusItem && requestedFocusItem->isVisible() ? QStringLiteral("true") : QStringLiteral("false"),
						  requestedFocusItem && requestedFocusItem->isEnabled() ? QStringLiteral("true") : QStringLiteral("false"),
						  window->property("modalUiActive").toBool() ? QStringLiteral("true") : QStringLiteral("false"),
						  window->property("navigationModalActive").toBool() ? QStringLiteral("true") : QStringLiteral("false"),
						  activeFocusPath.isEmpty() ? QStringLiteral("<none>") : activeFocusPath.join(QStringLiteral(" > ")));
		}
		return {};
	}
	if (m_host->chatModel()->rowCount() != expectedMessageCount) {
		if (error) {
			*error = QStringLiteral("Visual fixture timeline was clobbered before presentation: observed %1 messages, expected %2.")
					 .arg(m_host->chatModel()->rowCount())
					 .arg(expectedMessageCount);
		}
		return {};
	}
	const int richPreviewRow = 1;
	const QVariantMap normalizedRichPreview = richPreviewVariant == QLatin1String("none")
		? QVariantMap() : m_host->chatModel()->get(richPreviewRow).value(QStringLiteral("preview")).toMap();
	const bool expectRichPreview = richPreviewVariant != QLatin1String("none");
	const QString expectedAspect = expectedRichPreviewAspect(richPreviewVariant);
	const bool expectGeneratedImage = QStringList { QStringLiteral("youtube"), QStringLiteral("spotify"),
		QStringLiteral("tiktok"), QStringLiteral("instagram"), QStringLiteral("audio"),
		QStringLiteral("product"), QStringLiteral("steam"), QStringLiteral("twitch") }
		.contains(richPreviewVariant);
	if (normalizedRichPreview.isEmpty() != !expectRichPreview
		|| (expectRichPreview && normalizedRichPreview.value(QStringLiteral("title")).toString().isEmpty())
		|| (expectRichPreview
			&& normalizedRichPreview.value(QStringLiteral("previewSize")).toString() != richPreviewSize)
		|| (!expectedAspect.isEmpty()
			&& (normalizedRichPreview.value(QStringLiteral("embedKind")).toString() != richPreviewVariant
				|| normalizedRichPreview.value(QStringLiteral("embedAspect")).toString() != expectedAspect))
		|| (expectGeneratedImage
			&& (normalizedRichPreview.value(QStringLiteral("thumbnailUrl")).toString().isEmpty()
				|| normalizedRichPreview.value(QStringLiteral("mediaItems")).toList().isEmpty()))
		|| (richPreviewVariant == QLatin1String("loading")
			&& normalizedRichPreview.value(QStringLiteral("state")).toString() != QLatin1String("loading"))
		|| (richPreviewVariant == QLatin1String("error")
			&& normalizedRichPreview.value(QStringLiteral("state")).toString() != QLatin1String("error"))) {
		if (error) *error = QStringLiteral("The visual fixture could not establish the normalized rich-preview contract.");
		return {};
	}
	fixtureOverrideRollback.committed = true;
	++m_generation;
	const bool motdHiddenForHistory = m_host->sessionController()->hasMotd()
		&& m_host->chatModel()->hasUserHistory();
	return { { QStringLiteral("case_id"), caseId }, { QStringLiteral("state"), state },
			 { QStringLiteral("theme"), theme }, { QStringLiteral("layout"), layout },
			 { QStringLiteral("motd_variant"), motdVariant },
			 { QStringLiteral("rich_preview_variant"), richPreviewVariant },
			 { QStringLiteral("rich_preview_size"), richPreviewSize },
			 { QStringLiteral("rich_preview_present"), !normalizedRichPreview.isEmpty() },
			 { QStringLiteral("rich_preview_message_id"), normalizedRichPreview.isEmpty()
				 ? QString() : m_host->chatModel()->get(richPreviewRow).value(QStringLiteral("id")).toString() },
			 { QStringLiteral("rich_preview_title"), normalizedRichPreview.value(QStringLiteral("title")).toString() },
			 { QStringLiteral("rich_preview_open_label"), normalizedRichPreview.value(QStringLiteral("openLabel")).toString() },
			 { QStringLiteral("rich_preview_embed_provider"), normalizedRichPreview.value(QStringLiteral("embedKind")).toString() },
			 { QStringLiteral("rich_preview_embed_aspect"), normalizedRichPreview.value(QStringLiteral("embedAspect")).toString() },
			 { QStringLiteral("rich_preview_media_count"), normalizedRichPreview.value(QStringLiteral("mediaItems")).toList().size() },
			 { QStringLiteral("rich_preview_has_thumbnail"),
				 !normalizedRichPreview.value(QStringLiteral("thumbnailUrl")).toString().isEmpty() },
			 { QStringLiteral("width"), window->width() }, { QStringLiteral("height"), window->height() },
			 { QStringLiteral("message_count"), m_host->chatModel()->rowCount() },
			 { QStringLiteral("motd_present"), m_host->sessionController()->hasMotd() },
			 { QStringLiteral("motd_expanded"), m_host->sessionController()->hasMotd()
				&& m_host->sessionController()->motdExpanded() },
			 { QStringLiteral("motd_changed"), m_host->sessionController()->motdChanged() },
			 { QStringLiteral("motd_has_user_history"), m_host->chatModel()->hasUserHistory() },
			 { QStringLiteral("motd_visible"), m_host->sessionController()->hasMotd()
				&& !m_host->sessionController()->motdDismissed() && !motdHiddenForHistory },
			 { QStringLiteral("focus_target"), requestedFocusName },
			 { QStringLiteral("actual_device_pixel_ratio"), actualDevicePixelRatio() },
			 { QStringLiteral("generation"), m_generation } };
}

void QmlVisualFixtureController::applyState(const QString &state, const QString &motdVariant,
										 const QString &richPreviewVariant, const QString &richPreviewSize) {
	ClientSessionController *session = m_host->sessionController();
	ActiveScopeController *scope = m_host->activeScopeController();
	RoomModel *rooms = m_host->roomModel();
	NavigationRailModel *navigation = m_host->navigationModel();
	ParticipantModel *participants = m_host->participantModel();
	ChatTimelineModel *chat = m_host->chatModel();
	AsyncOperationModel *operations = m_host->operationModel();
	DialogStateController *dialog = m_host->dialogController();
	rooms->replaceDirectMessageStates({});
	navigation->replaceDirectMessageStates({});
	participants->replaceParticipantStates({});
	chat->replaceMessages({});
	operations->clear();
	dialog->applyState({ { QStringLiteral("open"), false } });
	session->setUpdateBanner({});
	session->setMotdContent({}, {});
	session->setMotdSummary({});
	session->setMotdExpanded(true);
	session->setMotdDismissedSignature({});
	session->setMotdLastSeenSignature({});
	session->setSelfMuted(false);
	session->setSelfDeafened(false);

	if (state == QLatin1String("connected")) {
		session->setConnected(true);
		session->setServerName(QStringLiteral("Mumble Visual Fixture"));
		session->setConnectionLabel(QStringLiteral("Connected"));
		session->setConnectionState(QStringLiteral("connected"));
		session->setConnectionTone(QStringLiteral("success"));
		session->setConnectionDetail({});
		session->setConnectionRetryRemainingMs(0);
		session->setCanConnect(false);
		session->setCanCancel(false);
		session->setSelfStatusLabel(QStringLiteral("Online"));
		session->setSelfName(QStringLiteral("Demo User"));
		if (motdVariant != QLatin1String("none")) {
			const QString fixtureMotd = QStringLiteral(
				"<h2>Welcome to Mumble</h2>"
				"<p>This deterministic server message verifies the native Qt Quick welcome surface.</p>"
				"<p><b>Tip:</b> Choose a room on the left, then say hello.</p>");
			session->setMotdContent(fixtureMotd, QStringLiteral("qml-visual-motd-v1"));
			session->setMotdSummary(QStringLiteral("Welcome to Mumble. Choose a room, then say hello."));
			session->setMotdExpanded(motdVariant != QLatin1String("collapsed"));
			if (motdVariant == QLatin1String("changed"))
				session->setMotdLastSeenSignature(QStringLiteral("qml-visual-motd-previous"));
		}
		const QVariantList fixtureParticipants {
			QVariantMap { { QStringLiteral("session"), QStringLiteral("101") },
						{ QStringLiteral("participantKey"), QStringLiteral("user:101") },
						{ QStringLiteral("name"), QStringLiteral("Demo User") },
						{ QStringLiteral("statusLabel"), QStringLiteral("Listening") },
						{ QStringLiteral("talkState"), QStringLiteral("passive") } },
			QVariantMap { { QStringLiteral("session"), QStringLiteral("102") },
						{ QStringLiteral("participantKey"), QStringLiteral("user:102") },
						{ QStringLiteral("name"), QStringLiteral("Alex") },
						{ QStringLiteral("statusLabel"), QStringLiteral("Talking") },
						{ QStringLiteral("talkState"), QStringLiteral("talking") } }
		};
		const QVariantList voiceRooms {
			QVariantMap { { QStringLiteral("token"), QStringLiteral("0:1") }, { QStringLiteral("label"), QStringLiteral("Lobby") },
						  { QStringLiteral("selected"), true }, { QStringLiteral("joined"), true }, { QStringLiteral("depth"), 0 },
						  { QStringLiteral("unreadCount"), 0 }, { QStringLiteral("participantCount"), 2 },
						  { QStringLiteral("participants"), fixtureParticipants } },
			QVariantMap { { QStringLiteral("token"), QStringLiteral("0:2") }, { QStringLiteral("label"), QStringLiteral("Studio") },
						  { QStringLiteral("depth"), 0 }, { QStringLiteral("unreadCount"), 0 } }
		};
		const QVariantList textRoomActions {
			QVariantMap { { QStringLiteral("kind"), QStringLiteral("action") },
						  { QStringLiteral("id"), QStringLiteral("markRead") },
						  { QStringLiteral("label"), QStringLiteral("Mark read") },
						  { QStringLiteral("enabled"), true }, { QStringLiteral("visible"), true },
						  { QStringLiteral("checkable"), false }, { QStringLiteral("checked"), false } }
		};
		const QVariantList textRooms {
			QVariantMap { { QStringLiteral("token"), QStringLiteral("3:1") },
						  { QStringLiteral("label"), QStringLiteral("#general") },
						  { QStringLiteral("description"), QStringLiteral("Text room") },
						  { QStringLiteral("selected"), false }, { QStringLiteral("depth"), 0 },
						  { QStringLiteral("unreadCount"), 0 },
						  { QStringLiteral("actions"), textRoomActions } }
		};
		// Scope changes can synchronously publish live room, participant, and
		// conversation state. Apply every fixture model after the scope so those
		// signal side-effects cannot clobber deterministic state while fixture
		// writes are temporarily enabled.
		scope->applyState({ { QStringLiteral("scopeToken"), QStringLiteral("0:1") }, { QStringLiteral("label"), QStringLiteral("Lobby") },
							{ QStringLiteral("description"), QStringLiteral("Voice room") }, { QStringLiteral("kindLabel"), QStringLiteral("VOICE") },
							{ QStringLiteral("composerPlaceholder"), QStringLiteral("Message Lobby") }, { QStringLiteral("canSend"), true },
							{ QStringLiteral("canAttachImages"), true } });
		participants->replaceParticipantStates(fixtureParticipants);
		const bool systemMessages = motdVariant != QLatin1String("none")
			&& motdVariant != QLatin1String("history-hidden");
		const QVariantMap richPreview = visualRichPreview(m_host, richPreviewVariant, richPreviewSize);
		const QString fixtureGeneration = QString::number(m_generation + 1);
		const QVariantList fixtureMessages {
			QVariantMap { { QStringLiteral("messageKey"), QStringLiteral("fixture:%1:1").arg(fixtureGeneration) }, { QStringLiteral("actor"), QStringLiteral("Alex") },
							{ QStringLiteral("bodyText"), QStringLiteral("Welcome to the deterministic visual fixture.") },
							{ QStringLiteral("timeLabel"), QStringLiteral("10:24") }, { QStringLiteral("own"), false },
							{ QStringLiteral("canReply"), true },
							{ QStringLiteral("system"), systemMessages },
							{ QStringLiteral("preview"), QVariantMap() }, { QStringLiteral("attachments"), QVariantList() },
							{ QStringLiteral("reactions"), QVariantList() } },
			QVariantMap { { QStringLiteral("messageKey"), QStringLiteral("fixture:%1:2").arg(fixtureGeneration) }, { QStringLiteral("actor"), QStringLiteral("Demo User") },
							{ QStringLiteral("bodyText"), QStringLiteral("Qt Quick is ready for review.") },
							{ QStringLiteral("timeLabel"), QStringLiteral("10:25") }, { QStringLiteral("own"), true },
							{ QStringLiteral("system"), systemMessages },
							{ QStringLiteral("preview"), richPreview }, { QStringLiteral("attachments"), QVariantList() },
							{ QStringLiteral("reactions"), QVariantList() } }
		};
		chat->replaceMessages(fixtureMessages);
		// Timeline publication can synchronously refresh persistent unread counts.
		// Keep the synthetic room rows last so no live badge leaks into the fixture.
		rooms->replaceRoomStates(voiceRooms, textRooms);
		navigation->replaceRoomStates(voiceRooms, textRooms);
		return;
	}

	session->setConnected(false);
	session->setServerName(QStringLiteral("Mumble"));
	session->setConnectionLabel(state == QLatin1String("loading") ? QStringLiteral("Connecting…") : QStringLiteral("Disconnected"));
	session->setConnectionState(state == QLatin1String("loading") ? QStringLiteral("connecting")
															 : QStringLiteral("disconnected"));
	session->setConnectionTone(state == QLatin1String("error") ? QStringLiteral("danger") : QStringLiteral("muted"));
	session->setConnectionDetail(state == QLatin1String("error")
		? QStringLiteral("The test server could not be reached. Try again.") : QString());
	session->setConnectionRetryRemainingMs(0);
	session->setCanConnect(state != QLatin1String("loading"));
	session->setCanCancel(state == QLatin1String("loading"));
	session->setSelfStatusLabel(QStringLiteral("Offline"));
	session->setSelfName(QStringLiteral("You"));
	rooms->replaceRoomStates({}, {});
	navigation->replaceRoomStates({}, {});
	scope->applyState({ { QStringLiteral("label"), state == QLatin1String("empty") ? QStringLiteral("No conversation selected")
																								 : QStringLiteral("Connection") },
						 { QStringLiteral("description"), QStringLiteral("Choose a server to begin") },
						 { QStringLiteral("kindLabel"), QStringLiteral("STATUS") }, { QStringLiteral("canSend"), false } });
}

bool QmlVisualFixtureController::waitForPresentedFrame(QString *error) {
	QQuickWindow *window = m_host ? m_host->window() : nullptr;
	if (!window || !window->isExposed()) {
		if (error) *error = QStringLiteral("The Qt Quick window is not exposed for visual capture.");
		return false;
	}
	QEventLoop loop;
	QTimer timeout;
	timeout.setSingleShot(true);
	bool presented = false;
	const QMetaObject::Connection frameConnection = QObject::connect(window, &QQuickWindow::frameSwapped, &loop, [&]() {
		presented = true;
		loop.quit();
	}, Qt::QueuedConnection);
	QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
	timeout.start(5000);
	window->requestUpdate();
	loop.exec();
	QObject::disconnect(frameConnection);
	if (!presented && error) *error = QStringLiteral("Timed out waiting for a presented Qt Quick frame.");
	return presented;
}
