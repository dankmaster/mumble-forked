// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef MUMBLE_MUMBLE_ESPORTSPREVIEW_H_
#define MUMBLE_MUMBLE_ESPORTSPREVIEW_H_

#include <QtCore/QHash>
#include <QtCore/QString>
#include <QtCore/QUrl>
#include <QtCore/QVariantMap>

#include <optional>

/**
 * Link cards for esports and Counter-Strike stats sites: HLTV, Liquipedia,
 * FACEIT, Leetify, csstats.gg and csst.at.
 *
 * Only public, unauthenticated data is read: HLTV and csst.at pages, the
 * Liquipedia MediaWiki API, the JSON the FACEIT site itself loads and Leetify's
 * public API. Pages behind a bot wall (HLTV matches, csstats.gg, FACEIT pages)
 * are not fetched; their card says what the address alone says.
 *
 * The functions are pure so they can run on a worker thread and be tested
 * without a network. Their metadata uses the keys EmbedDocument reads:
 * esportsKind, esportsTitle, esportsSection, esportsDescription, esportsAuthor,
 * esportsPublishedAt, esportsImage, esportsImageFit, esportsLarge and
 * esportsFacts ({ label, value } pairs, most telling first).
 */
namespace EsportsPreview {
	// Stored as esportsMetadataVersion once a card is complete; a cached card of
	// another version is read again.
	inline constexpr int MetadataVersion = 1;

	struct Target {
		// hltv, liquipedia, faceit, leetify, csstats or csstat.
		QString provider;
		// player, team, match, news, event or page.
		QString kind;
		// The player's or match's id, the page's title on a wiki.
		QString id;
		// HLTV's readable part of the address ("natus-vincere").
		QString slug;
		// The Liquipedia wiki ("dota2", "counterstrike").
		QString wiki;
		// What the client fetches for the card; empty when nothing public says more.
		QUrl requestUrl;
		bool requestIsJson = false;
	};

	std::optional< Target > targetFromUrl(const QUrl &url);

	// The card the address alone gives, shown until (or instead of) a fetch.
	QVariantMap metadataFromUrl(const Target &target);
	// The card from the fetched JSON; empty when the answer says nothing usable.
	QVariantMap metadataFromJson(const Target &target, const QByteArray &json);
	// The card from a fetched page and its decoded meta tags.
	QVariantMap metadataFromHtml(const Target &target, const QString &html,
								 const QHash< QString, QString > &metaTags);
}

#endif
