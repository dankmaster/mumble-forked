// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "EsportsPreview.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QLocale>
#include <QtCore/QRegularExpression>
#include <QtCore/QStringList>
#include <QtCore/QUrlQuery>
#include <QtCore/QVariantList>

#include <algorithm>
#include <cmath>

namespace EsportsPreview {
namespace {
	const QString LeetifyApi = QStringLiteral("https://api-public.cs-prod.leetify.com");

	bool isSteamId64(const QString &value) {
		static const QRegularExpression s_steamId(QStringLiteral("^7656119[0-9]{10}$"));
		return s_steamId.match(value).hasMatch();
	}

	bool isUuid(const QString &value) {
		static const QRegularExpression s_uuid(
			QStringLiteral("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$"));
		return s_uuid.match(value).hasMatch();
	}

	// Page text without its tags and entities, on one line.
	QString plainText(QString html, qsizetype maximum = 1024) {
		static const QRegularExpression s_tag(QStringLiteral("<[^>]*>"));
		static const QRegularExpression s_numericEntity(QStringLiteral("&#(x?)([0-9a-fA-F]{1,6});"));
		html.replace(s_tag, QStringLiteral(" "));
		QString text;
		text.reserve(html.size());
		qsizetype last = 0;
		QRegularExpressionMatchIterator it = s_numericEntity.globalMatch(html);
		while (it.hasNext()) {
			const QRegularExpressionMatch match = it.next();
			text += QStringView(html).mid(last, match.capturedStart() - last);
			bool ok = false;
			const uint code = match.captured(2).toUInt(&ok, match.captured(1).isEmpty() ? 10 : 16);
			if (ok && code > 0 && code <= 0x10FFFF) {
				const char32_t character = code;
				text += QString::fromUcs4(&character, 1);
			}
			last = match.capturedEnd();
		}
		text += QStringView(html).mid(last);
		text.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "));
		text.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
		text.replace(QStringLiteral("&apos;"), QStringLiteral("'"));
		text.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
		text.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
		text.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
		return text.simplified().left(maximum);
	}

	QString captured(const QString &text, const QRegularExpression &pattern, int group = 1) {
		const QRegularExpressionMatch match = pattern.match(text);
		return match.hasMatch() ? plainText(match.captured(group), 512) : QString();
	}

	// "natus-vincere" -> "Natus Vincere"; "vs" stays as it is.
	QString titleFromSlug(const QString &slug) {
		QStringList words = QString(slug).replace(QLatin1Char('_'), QLatin1Char('-'))
								.split(QLatin1Char('-'), Qt::SkipEmptyParts);
		for (QString &word : words) {
			if (word != QLatin1String("vs")) {
				word[0] = word.at(0).toUpper();
			}
		}
		return words.join(QLatin1Char(' ')).left(256);
	}

	QString thousands(double value) {
		return QLocale(QLocale::English).toString(static_cast< qlonglong >(std::llround(value)));
	}

	// "de_ancient" -> "Ancient".
	QString mapLabel(QString map) {
		static const QRegularExpression s_prefix(QStringLiteral("^(?:de|cs|ar|dz|gd)_"));
		map = map.trimmed().remove(s_prefix).replace(QLatin1Char('_'), QLatin1Char(' '));
		if (!map.isEmpty()) {
			map[0] = map.at(0).toUpper();
		}
		return map.left(64);
	}

	void appendFact(QVariantList &facts, const QString &label, const QString &value) {
		const QString text = value.trimmed();
		if (text.isEmpty() || facts.size() >= 6) {
			return;
		}
		facts.push_back(QVariantMap { { QStringLiteral("label"), label }, { QStringLiteral("value"), text.left(128) } });
	}

	void insertText(QVariantMap &metadata, const QString &key, const QString &value, qsizetype maximum = 512) {
		const QString text = value.trimmed().left(maximum);
		if (!text.isEmpty()) {
			metadata.insert(key, text);
		}
	}

	QVariantMap baseMetadata(const Target &target) {
		return QVariantMap { { QStringLiteral("provider"), target.provider },
							 { QStringLiteral("previewProvider"), target.provider },
							 { QStringLiteral("esportsKind"), target.kind } };
	}

	void finish(QVariantMap &metadata, const QVariantList &facts) {
		if (!facts.isEmpty()) {
			metadata.insert(QStringLiteral("esportsFacts"), facts);
		}
	}

	QString liquipediaGame(const QString &wiki) {
		static const QHash< QString, QString > games {
			{ QStringLiteral("counterstrike"), QStringLiteral("Counter-Strike") },
			{ QStringLiteral("dota2"), QStringLiteral("Dota 2") },
			{ QStringLiteral("leagueoflegends"), QStringLiteral("League of Legends") },
			{ QStringLiteral("valorant"), QStringLiteral("VALORANT") },
			{ QStringLiteral("rocketleague"), QStringLiteral("Rocket League") },
			{ QStringLiteral("overwatch"), QStringLiteral("Overwatch") },
			{ QStringLiteral("rainbowsix"), QStringLiteral("Rainbow Six") },
			{ QStringLiteral("starcraft2"), QStringLiteral("StarCraft II") },
			{ QStringLiteral("starcraft"), QStringLiteral("StarCraft") },
			{ QStringLiteral("apexlegends"), QStringLiteral("Apex Legends") },
			{ QStringLiteral("fortnite"), QStringLiteral("Fortnite") },
			{ QStringLiteral("pubg"), QStringLiteral("PUBG") },
			{ QStringLiteral("pubgmobile"), QStringLiteral("PUBG Mobile") },
			{ QStringLiteral("mobilelegends"), QStringLiteral("Mobile Legends") },
			{ QStringLiteral("hearthstone"), QStringLiteral("Hearthstone") },
			{ QStringLiteral("smash"), QStringLiteral("Smash") },
			{ QStringLiteral("fighters"), QStringLiteral("Fighting Games") },
			{ QStringLiteral("ageofempires"), QStringLiteral("Age of Empires") },
			{ QStringLiteral("warcraft"), QStringLiteral("Warcraft") },
			{ QStringLiteral("deadlock"), QStringLiteral("Deadlock") },
			{ QStringLiteral("marvelrivals"), QStringLiteral("Marvel Rivals") },
			{ QStringLiteral("callofduty"), QStringLiteral("Call of Duty") },
			{ QStringLiteral("halo"), QStringLiteral("Halo") },
			{ QStringLiteral("chess"), QStringLiteral("Chess") },
			{ QStringLiteral("esports"), QStringLiteral("Esports") }
		};
		return games.value(wiki, wiki);
	}

	QString leetifySourceLabel(const QString &source) {
		static const QHash< QString, QString > labels {
			{ QStringLiteral("matchmaking"), QStringLiteral("Matchmaking") },
			{ QStringLiteral("matchmaking_competitive"), QStringLiteral("Competitive") },
			{ QStringLiteral("matchmaking_wingman"), QStringLiteral("Wingman") },
			{ QStringLiteral("premier"), QStringLiteral("Premier") },
			{ QStringLiteral("faceit"), QStringLiteral("FACEIT") },
			{ QStringLiteral("hltv"), QStringLiteral("HLTV") },
			{ QStringLiteral("renown"), QStringLiteral("Renown") }
		};
		return labels.value(source.toLower(), titleFromSlug(source));
	}

	QList< QJsonObject > jsonLdObjects(const QString &html) {
		static const QRegularExpression s_script(
			QStringLiteral("<script[^>]*type=[\"']application/ld\\+json[\"'][^>]*>([\\s\\S]*?)</script>"),
			QRegularExpression::CaseInsensitiveOption);
		QList< QJsonObject > objects;
		QRegularExpressionMatchIterator it = s_script.globalMatch(html);
		while (it.hasNext() && objects.size() < 8) {
			const QJsonDocument document = QJsonDocument::fromJson(it.next().captured(1).trimmed().toUtf8());
			if (document.isObject()) {
				objects.push_back(document.object());
			} else {
				for (const QJsonValue &value : document.array()) {
					if (value.isObject()) {
						objects.push_back(value.toObject());
					}
				}
			}
		}
		return objects;
	}

	QJsonObject jsonLdOfType(const QString &html, const QString &type) {
		for (const QJsonObject &object : jsonLdObjects(html)) {
			if (object.value(QStringLiteral("@type")).toString() == type) {
				return object;
			}
		}
		return {};
	}

	QString jsonText(const QJsonObject &object, const QString &key, qsizetype maximum = 512) {
		return plainText(object.value(key).toString(), maximum);
	}

	QVariantMap hltvMetadata(const Target &target, const QString &html, const QHash< QString, QString > &metaTags) {
		QVariantMap metadata = metadataFromUrl(target);
		QVariantList facts;
		const QString ogTitle = metaTags.value(QStringLiteral("og:title")).trimmed();
		const QString ogImage = metaTags.value(QStringLiteral("og:image")).trimmed();
		// HLTV's own logo is what pages without a picture of their own share.
		const bool ownImage = !ogImage.isEmpty() && !ogImage.contains(QLatin1String("openGraphHltvLogo"));

		if (target.kind == QLatin1String("player")) {
			// "Oleksandr 's1mple' Kostyliev": the nickname, and the name around it.
			static const QRegularExpression s_nickname(QStringLiteral("'([^']+)'"));
			const QRegularExpressionMatch nickname = s_nickname.match(ogTitle);
			if (nickname.hasMatch()) {
				insertText(metadata, QStringLiteral("esportsTitle"), nickname.captured(1), 128);
				insertText(metadata, QStringLiteral("esportsSection"),
						   QString(ogTitle).remove(nickname.capturedStart(), nickname.capturedLength()).simplified(),
						   128);
			}
			insertText(metadata, QStringLiteral("esportsDescription"), metaTags.value(QStringLiteral("description")),
					   360);
			static const QRegularExpression s_team(
				QStringLiteral("playerTeam\"[\\s\\S]{0,1600}?itemprop=\"text\"[^>]*>([^<]+)<"));
			static const QRegularExpression s_rating(
				QStringLiteral("<b>Rating [0-9.]+</b>\\s*<span class=\"statsVal\">\\s*<p>([0-9.]+)</p>"));
			static const QRegularExpression s_age(
				QStringLiteral("playerAge\"[\\s\\S]{0,600}?itemprop=\"text\">([0-9]+) years"));
			appendFact(facts, QStringLiteral("Team"), captured(html, s_team));
			appendFact(facts, QStringLiteral("Rating"), captured(html, s_rating));
			appendFact(facts, QStringLiteral("Age"), captured(html, s_age));
			if (ownImage) {
				metadata.insert(QStringLiteral("esportsImage"), ogImage);
			}
		} else if (target.kind == QLatin1String("team")) {
			const QJsonObject team = jsonLdOfType(html, QStringLiteral("SportsTeam"));
			insertText(metadata, QStringLiteral("esportsTitle"), jsonText(team, QStringLiteral("name"), 128), 128);
			QStringList lineup;
			for (const QJsonValue &athlete : team.value(QStringLiteral("athlete")).toArray()) {
				const QString nickname = jsonText(athlete.toObject(), QStringLiteral("alternateName"), 64);
				if (!nickname.isEmpty() && lineup.size() < 7) {
					lineup << nickname;
				}
			}
			insertText(metadata, QStringLiteral("esportsDescription"), lineup.join(QStringLiteral(", ")), 360);
			static const QRegularExpression s_ranking(
				QStringLiteral("World ranking</b><span class=\"right\"><a[^>]*>(#[0-9]+)</a>"));
			static const QRegularExpression s_valveRanking(
				QStringLiteral("Valve ranking</span>[\\s\\S]{0,240}?<span class=\"right\"><a[^>]*>(#[0-9]+)</a>"));
			static const QRegularExpression s_region(
				QStringLiteral("class=\"team-country[^\"]*\"[^>]*>(?:<img[^>]*>)?([^<]+)<"));
			appendFact(facts, QStringLiteral("World ranking"), captured(html, s_ranking));
			appendFact(facts, QStringLiteral("VRS"), captured(html, s_valveRanking));
			appendFact(facts, QStringLiteral("Region"), captured(html, s_region));
			if (ownImage) {
				metadata.insert(QStringLiteral("esportsImage"), ogImage);
				metadata.insert(QStringLiteral("esportsImageFit"), QStringLiteral("contain"));
			}
		} else if (target.kind == QLatin1String("news")) {
			const QJsonObject article = jsonLdOfType(html, QStringLiteral("NewsArticle"));
			insertText(metadata, QStringLiteral("esportsTitle"),
					   !article.isEmpty() ? jsonText(article, QStringLiteral("headline")) : ogTitle);
			insertText(metadata, QStringLiteral("esportsDescription"),
					   !article.isEmpty() ? jsonText(article, QStringLiteral("description"))
										  : metaTags.value(QStringLiteral("og:description")),
					   360);
			insertText(metadata, QStringLiteral("esportsAuthor"),
					   jsonText(article.value(QStringLiteral("author")).toObject(), QStringLiteral("name"), 128), 128);
			insertText(metadata, QStringLiteral("esportsPublishedAt"),
					   jsonText(article, QStringLiteral("datePublished"), 64), 64);
			metadata.remove(QStringLiteral("esportsSection"));
			appendFact(facts, QStringLiteral("Event"), jsonText(article, QStringLiteral("articleSection"), 128));
			if (ownImage) {
				metadata.insert(QStringLiteral("esportsImage"), ogImage);
				metadata.insert(QStringLiteral("esportsLarge"), true);
			}
		} else if (target.kind == QLatin1String("event")) {
			insertText(metadata, QStringLiteral("esportsTitle"), ogTitle, 256);
			static const QRegularExpression s_dates(QStringLiteral("<td class=\"eventdate\">([\\s\\S]{0,600}?)</td>"));
			static const QRegularExpression s_prize(QStringLiteral("<td class=\"prizepool[^\"]*\"[^>]*>([^<]+)<"));
			static const QRegularExpression s_teams(QStringLiteral("<td class=\"teamsNumber\">([^<]+)<"));
			static const QRegularExpression s_location(
				QStringLiteral("<td class=\"location[^\"]*\">[\\s\\S]{0,600}?<span class=\"text-ellipsis\">([^<]+)<"));
			appendFact(facts, QStringLiteral("Dates"),
					   captured(html, s_dates).replace(QStringLiteral(" - "), QStringLiteral(" – ")));
			appendFact(facts, QStringLiteral("Prize pool"), captured(html, s_prize));
			appendFact(facts, QStringLiteral("Teams"), captured(html, s_teams));
			appendFact(facts, QStringLiteral("Location"), captured(html, s_location));
			if (ownImage) {
				metadata.insert(QStringLiteral("esportsImage"), ogImage);
				metadata.insert(QStringLiteral("esportsImageFit"), QStringLiteral("contain"));
			}
		}
		finish(metadata, facts);
		return metadata;
	}

	QVariantMap csstatMetadata(const Target &target, const QString &html, const QHash< QString, QString > &metaTags) {
		QVariantMap metadata = metadataFromUrl(target);
		// "Rabscuttle — CS2 Stats & FACEIT Elo | csst.at"
		QString name = metaTags.value(QStringLiteral("og:title")).section(QChar(0x2014), 0, 0).trimmed();
		if (name.isEmpty()) {
			static const QRegularExpression s_name(QStringLiteral("class=\"identity-card__name\">([^<]+)<"));
			name = captured(html, s_name);
		}
		insertText(metadata, QStringLiteral("esportsTitle"), name, 128);
		// The site draws the player's ranks into its share picture.
		const QString image = metaTags.value(QStringLiteral("og:image")).trimmed();
		if (!image.isEmpty()) {
			metadata.insert(QStringLiteral("esportsImage"), image);
			metadata.insert(QStringLiteral("esportsLarge"), true);
		}
		return metadata;
	}

	QVariantMap liquipediaMetadata(const Target &target, const QJsonObject &root) {
		const QJsonObject query = root.value(QStringLiteral("query")).toObject();
		const auto resolved = [&query](QString title) {
			for (const QString &list : { QStringLiteral("normalized"), QStringLiteral("redirects") }) {
				for (const QJsonValue &entry : query.value(list).toArray()) {
					if (entry.toObject().value(QStringLiteral("from")).toString() == title) {
						title = entry.toObject().value(QStringLiteral("to")).toString();
					}
				}
			}
			return title;
		};
		const QString pageTitle = resolved(target.id);
		const qsizetype parentEnd = target.id.lastIndexOf(QLatin1Char('/'));
		const QString parentTitle = parentEnd > 0 ? resolved(target.id.left(parentEnd)) : QString();
		QJsonObject page;
		QJsonObject parent;
		for (const QJsonValue &entry : query.value(QStringLiteral("pages")).toArray()) {
			const QJsonObject candidate = entry.toObject();
			const QString title = candidate.value(QStringLiteral("title")).toString();
			if (title == pageTitle) {
				page = candidate;
			} else if (!parentTitle.isEmpty() && title == parentTitle) {
				parent = candidate;
			}
		}
		if (page.isEmpty() || page.contains(QStringLiteral("missing")) || page.contains(QStringLiteral("invalid"))) {
			return {};
		}

		const QJsonObject props = page.value(QStringLiteral("pageprops")).toObject();
		const QJsonObject parentProps = parent.value(QStringLiteral("pageprops")).toObject();
		const auto first = [](std::initializer_list< QString > values) {
			for (const QString &value : values) {
				if (!value.isEmpty()) {
					return value;
				}
			}
			return QString();
		};
		// A subpage (a stage or a season) has little of its own: its event fills in.
		const QString summary = first({ jsonText(props, QStringLiteral("metadescl"), 1024),
										jsonText(parentProps, QStringLiteral("metadescl"), 1024) });
		QStringList categories;
		for (const QJsonObject &source : { page, parent }) {
			for (const QJsonValue &entry : source.value(QStringLiteral("categories")).toArray()) {
				categories << entry.toObject().value(QStringLiteral("title")).toString();
			}
			if (!categories.isEmpty()) {
				break;
			}
		}

		Target typed = target;
		if (categories.contains(QStringLiteral("Category:Players"))) {
			typed.kind = QStringLiteral("player");
		} else if (categories.contains(QStringLiteral("Category:Teams"))) {
			typed.kind = QStringLiteral("team");
		} else if (std::any_of(categories.cbegin(), categories.cend(), [](const QString &category) {
					   return category.endsWith(QLatin1String(" Tournaments"));
				   })) {
			typed.kind = QStringLiteral("event");
		}
		QVariantMap metadata = metadataFromUrl(typed);
		insertText(metadata, QStringLiteral("esportsTitle"),
				   first({ jsonText(props, QStringLiteral("displaytitle"), 256), pageTitle }), 256);
		insertText(metadata, QStringLiteral("esportsDescription"),
				   first({ jsonText(page, QStringLiteral("extract"), 1024),
						   jsonText(props, QStringLiteral("metadescl"), 1024),
						   jsonText(parent, QStringLiteral("extract"), 1024), summary }),
				   360);
		const QString image = first({ jsonText(props, QStringLiteral("metaimageurl"), 2048),
									  jsonText(parentProps, QStringLiteral("metaimageurl"), 2048) });
		if (!image.isEmpty()) {
			metadata.insert(QStringLiteral("esportsImage"), image);
			// Player photos fill the square; team and event logos fit inside it.
			if (typed.kind != QLatin1String("player")) {
				metadata.insert(QStringLiteral("esportsImageFit"), QStringLiteral("contain"));
			}
		}

		QVariantList facts;
		if (typed.kind == QLatin1String("event")) {
			static const QRegularExpression s_tier(QStringLiteral("^Category:((?:Tier [0-9]|[SABCD]-Tier)) Tournaments$"));
			static const QRegularExpression s_dates(QStringLiteral(
				"(?:took|takes|will take|is taking|is set to take) place (?:from|on|between) (.+?)(?: featuring| with|\\.|$)"));
			static const QRegularExpression s_prize(QStringLiteral("prize pool of \\$([0-9][0-9,.]*)"));
			static const QRegularExpression s_teams(QStringLiteral("featuring ([0-9]+) (?:teams|players)"));
			QString tier;
			QString status;
			for (const QString &category : categories) {
				const QRegularExpressionMatch match = s_tier.match(category);
				if (match.hasMatch() && tier.isEmpty()) {
					tier = match.captured(1);
				}
				if (category == QLatin1String("Category:Finished Tournaments")) {
					status = QStringLiteral("Finished");
				} else if (category == QLatin1String("Category:Ongoing Tournaments")) {
					status = QStringLiteral("Ongoing");
				} else if (category == QLatin1String("Category:Upcoming Tournaments")) {
					status = QStringLiteral("Upcoming");
				}
			}
			appendFact(facts, QStringLiteral("Tier"), tier.startsWith(QLatin1String("Tier ")) ? tier.mid(5) : tier);
			appendFact(facts, QStringLiteral("Dates"), captured(summary, s_dates));
			const QString prize = captured(summary, s_prize).remove(QLatin1Char(','));
			bool prizeOk = false;
			const double prizeValue = prize.toDouble(&prizeOk);
			appendFact(facts, QStringLiteral("Prize pool"),
					   prizeOk && prizeValue > 0 ? QStringLiteral("$") + thousands(prizeValue) : QString());
			appendFact(facts, QStringLiteral("Teams"), captured(summary, s_teams));
			appendFact(facts, QStringLiteral("Status"), status);
		}
		finish(metadata, facts);
		return metadata;
	}

	QVariantMap faceitPlayerMetadata(const Target &target, const QJsonObject &root) {
		const QJsonObject payload = root.value(QStringLiteral("payload")).toObject();
		const QString nickname = jsonText(payload, QStringLiteral("nickname"), 64);
		if (nickname.isEmpty()) {
			return {};
		}
		QVariantMap metadata = metadataFromUrl(target);
		metadata.insert(QStringLiteral("esportsTitle"), nickname);
		const QJsonObject games = payload.value(QStringLiteral("games")).toObject();
		const bool cs2 = games.contains(QStringLiteral("cs2"));
		const QJsonObject game = games.value(cs2 ? QStringLiteral("cs2") : QStringLiteral("csgo")).toObject();
		if (!game.isEmpty()) {
			metadata.insert(QStringLiteral("esportsSection"), cs2 ? QStringLiteral("CS2 player")
																  : QStringLiteral("CS:GO player"));
		}
		insertText(metadata, QStringLiteral("esportsImage"), jsonText(payload, QStringLiteral("avatar"), 2048), 2048);

		QVariantList facts;
		const int level = game.value(QStringLiteral("skill_level")).toInt();
		const double elo = game.value(QStringLiteral("faceit_elo")).toDouble();
		appendFact(facts, QStringLiteral("Level"), level > 0 ? QString::number(level) : QString());
		appendFact(facts, QStringLiteral("Elo"), elo > 0 ? thousands(elo) : QString());
		appendFact(facts, QStringLiteral("Region"), jsonText(game, QStringLiteral("region"), 16));
		appendFact(facts, QStringLiteral("Country"), jsonText(payload, QStringLiteral("country"), 8).toUpper());
		finish(metadata, facts);
		return metadata;
	}

	QVariantMap faceitMatchMetadata(const Target &target, const QJsonObject &root) {
		const QJsonObject payload = root.value(QStringLiteral("payload")).toObject();
		const QJsonObject teams = payload.value(QStringLiteral("teams")).toObject();
		const QString first = jsonText(teams.value(QStringLiteral("faction1")).toObject(), QStringLiteral("name"), 64);
		const QString second = jsonText(teams.value(QStringLiteral("faction2")).toObject(), QStringLiteral("name"), 64);
		if (first.isEmpty() || second.isEmpty()) {
			return {};
		}
		QVariantMap metadata = metadataFromUrl(target);
		metadata.insert(QStringLiteral("esportsTitle"), QStringLiteral("%1 vs %2").arg(first, second));
		insertText(metadata, QStringLiteral("esportsSection"),
				   jsonText(payload.value(QStringLiteral("entity")).toObject(), QStringLiteral("name"), 128), 128);

		QVariantList facts;
		QJsonObject results = payload.value(QStringLiteral("summaryResults")).toObject();
		if (results.isEmpty()) {
			results = payload.value(QStringLiteral("results")).toArray().first().toObject();
		}
		const QJsonObject factions = results.value(QStringLiteral("factions")).toObject();
		const QJsonValue firstScore = factions.value(QStringLiteral("faction1")).toObject().value(QStringLiteral("score"));
		const QJsonValue secondScore = factions.value(QStringLiteral("faction2")).toObject().value(QStringLiteral("score"));
		if (firstScore.isDouble() && secondScore.isDouble()) {
			appendFact(facts, QStringLiteral("Score"),
					   QStringLiteral("%1 – %2").arg(firstScore.toInt()).arg(secondScore.toInt()));
		}
		const QJsonObject mapVoting = payload.value(QStringLiteral("voting")).toObject().value(QStringLiteral("map")).toObject();
		const QString pick = mapVoting.value(QStringLiteral("pick")).toArray().first().toString();
		QString mapName = mapLabel(pick);
		for (const QJsonValue &entry : mapVoting.value(QStringLiteral("entities")).toArray()) {
			const QJsonObject map = entry.toObject();
			if (!pick.isEmpty()
				&& (map.value(QStringLiteral("game_map_id")).toString() == pick
					|| map.value(QStringLiteral("guid")).toString() == pick)) {
				const QString name = jsonText(map, QStringLiteral("name"), 64);
				mapName = name.isEmpty() ? mapName : name;
				insertText(metadata, QStringLiteral("esportsImage"), jsonText(map, QStringLiteral("image_sm"), 2048),
						   2048);
				break;
			}
		}
		appendFact(facts, QStringLiteral("Map"), mapName);
		static const QHash< QString, QString > states {
			{ QStringLiteral("FINISHED"), QStringLiteral("Finished") },
			{ QStringLiteral("ONGOING"), QStringLiteral("Live") },
			{ QStringLiteral("READY"), QStringLiteral("Starting") },
			{ QStringLiteral("VOTING"), QStringLiteral("Map vote") },
			{ QStringLiteral("CONFIGURING"), QStringLiteral("Starting") },
			{ QStringLiteral("CANCELLED"), QStringLiteral("Cancelled") }
		};
		appendFact(facts, QStringLiteral("Status"),
				   states.value(payload.value(QStringLiteral("status")).toString().toUpper()));
		finish(metadata, facts);
		return metadata;
	}

	QVariantMap leetifyProfileMetadata(const Target &target, const QJsonObject &profile) {
		const QString name = jsonText(profile, QStringLiteral("name"), 64);
		if (name.isEmpty()) {
			return {};
		}
		QVariantMap metadata = metadataFromUrl(target);
		metadata.insert(QStringLiteral("esportsTitle"), name);
		const QJsonObject ranks = profile.value(QStringLiteral("ranks")).toObject();
		QVariantList facts;
		const double premier = ranks.value(QStringLiteral("premier")).toDouble();
		const int faceitLevel = ranks.value(QStringLiteral("faceit")).toInt();
		const double faceitElo = ranks.value(QStringLiteral("faceit_elo")).toDouble();
		appendFact(facts, QStringLiteral("Premier"), premier > 0 ? thousands(premier) : QString());
		appendFact(facts, QStringLiteral("FACEIT level"), faceitLevel > 0 ? QString::number(faceitLevel) : QString());
		if (target.provider == QLatin1String("leetify")) {
			// Leetify's own numbers belong on its own card; csstats.gg only borrows
			// the ranks every site shows alike.
			const QJsonValue rating = ranks.value(QStringLiteral("leetify"));
			if (rating.isDouble()) {
				appendFact(facts, QStringLiteral("Rating"),
						   (rating.toDouble() > 0 ? QStringLiteral("+") : QString())
							   + QString::number(rating.toDouble(), 'f', 2));
			}
			const QJsonValue winRate = profile.value(QStringLiteral("winrate"));
			if (winRate.isDouble()) {
				appendFact(facts, QStringLiteral("Win rate"),
						   QStringLiteral("%1%").arg(std::lround(winRate.toDouble() * 100)));
			}
			const double matches = profile.value(QStringLiteral("total_matches")).toDouble();
			appendFact(facts, QStringLiteral("Matches"), matches > 0 ? thousands(matches) : QString());
			const QString steamId = jsonText(profile, QStringLiteral("steam64_id"), 32);
			if (isSteamId64(steamId)) {
				metadata.insert(QStringLiteral("esportsImage"),
								QString(QStringLiteral("https://api.cs-prod.leetify.com/preview-image/profile/") + steamId));
				metadata.insert(QStringLiteral("esportsLarge"), true);
			}
		} else {
			appendFact(facts, QStringLiteral("FACEIT Elo"), faceitElo > 0 ? thousands(faceitElo) : QString());
		}
		finish(metadata, facts);
		return metadata;
	}

	QVariantMap leetifyMatchMetadata(const Target &target, const QJsonObject &match) {
		const QString map = mapLabel(jsonText(match, QStringLiteral("map_name"), 64));
		const QJsonArray scores = match.value(QStringLiteral("team_scores")).toArray();
		if (map.isEmpty() && scores.isEmpty()) {
			return {};
		}
		QVariantMap metadata = metadataFromUrl(target);
		if (!map.isEmpty()) {
			metadata.insert(QStringLiteral("esportsTitle"), QStringLiteral("Match on %1").arg(map));
		}
		insertText(metadata, QStringLiteral("esportsPublishedAt"), jsonText(match, QStringLiteral("finished_at"), 64),
				   64);
		QVariantList facts;
		if (scores.size() == 2) {
			appendFact(facts, QStringLiteral("Score"),
					   QStringLiteral("%1 – %2")
						   .arg(scores.at(0).toObject().value(QStringLiteral("score")).toInt())
						   .arg(scores.at(1).toObject().value(QStringLiteral("score")).toInt()));
		}
		const QString source = jsonText(match, QStringLiteral("data_source"), 32);
		appendFact(facts, QStringLiteral("Source"), source.isEmpty() ? QString() : leetifySourceLabel(source));
		finish(metadata, facts);
		return metadata;
	}
}

std::optional< Target > targetFromUrl(const QUrl &url) {
	const QString scheme = url.scheme().toLower();
	if (!url.isValid() || (scheme != QLatin1String("https") && scheme != QLatin1String("http"))) {
		return std::nullopt;
	}
	QString host = url.host().toLower();
	if (host.startsWith(QLatin1String("www."))) {
		host.remove(0, 4);
	}
	const QStringList segments = url.path(QUrl::FullyDecoded).split(QLatin1Char('/'), Qt::SkipEmptyParts);
	const QString first = segments.value(0).toLower();
	const QString second = segments.value(1);
	Target target;

	if (host == QLatin1String("hltv.org")) {
		static const QHash< QString, QString > kinds {
			{ QStringLiteral("player"), QStringLiteral("player") }, { QStringLiteral("team"), QStringLiteral("team") },
			{ QStringLiteral("news"), QStringLiteral("news") },     { QStringLiteral("events"), QStringLiteral("event") },
			{ QStringLiteral("matches"), QStringLiteral("match") }
		};
		static const QRegularExpression s_id(QStringLiteral("^[0-9]{1,12}$"));
		static const QRegularExpression s_slug(QStringLiteral("^[A-Za-z0-9-]{1,160}$"));
		if (!kinds.contains(first) || !s_id.match(second).hasMatch()) {
			return std::nullopt;
		}
		target.provider = QStringLiteral("hltv");
		target.kind     = kinds.value(first);
		target.id       = second;
		target.slug     = s_slug.match(segments.value(2)).hasMatch() ? segments.value(2) : QString();
		// Match pages answer every fetch with a bot check; their address names the teams.
		if (target.kind != QLatin1String("match")) {
			QUrl page;
			page.setScheme(QStringLiteral("https"));
			page.setHost(QStringLiteral("www.hltv.org"));
			page.setPath(QStringLiteral("/%1/%2%3").arg(first, second,
														target.slug.isEmpty() ? QString() : QLatin1Char('/') + target.slug));
			target.requestUrl = page;
		}
		return target;
	}

	if (host == QLatin1String("liquipedia.net")) {
		static const QRegularExpression s_wiki(QStringLiteral("^[a-z0-9]{2,32}$"));
		static const QRegularExpression s_namespace(QStringLiteral(
			"^(?:Special|Category|User|Template|File|Help|Module|Talk|MediaWiki|Liquipedia|Property|Data|Image)(?: talk)?:"),
			QRegularExpression::CaseInsensitiveOption);
		const QString title = segments.mid(1).join(QLatin1Char('/')).replace(QLatin1Char('_'), QLatin1Char(' ')).trimmed();
		if (!s_wiki.match(first).hasMatch() || title.isEmpty() || title.size() > 255
			|| title.contains(QLatin1String(".php")) || s_namespace.match(title).hasMatch()) {
			return std::nullopt;
		}
		target.provider = QStringLiteral("liquipedia");
		target.kind     = QStringLiteral("page");
		target.wiki     = first;
		target.id       = title;
		QStringList titles { title };
		if (title.contains(QLatin1Char('/'))) {
			titles << title.left(title.lastIndexOf(QLatin1Char('/')));
		}
		// One query for the page and its parent: Liquipedia allows one API call
		// every two seconds per client.
		QUrlQuery query;
		query.addQueryItem(QStringLiteral("action"), QStringLiteral("query"));
		query.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
		query.addQueryItem(QStringLiteral("formatversion"), QStringLiteral("2"));
		query.addQueryItem(QStringLiteral("redirects"), QStringLiteral("1"));
		query.addQueryItem(QStringLiteral("prop"), QStringLiteral("pageprops|extracts|categories"));
		query.addQueryItem(QStringLiteral("ppprop"), QStringLiteral("displaytitle|metadescl|metaimageurl"));
		query.addQueryItem(QStringLiteral("exintro"), QStringLiteral("1"));
		query.addQueryItem(QStringLiteral("explaintext"), QStringLiteral("1"));
		query.addQueryItem(QStringLiteral("exsentences"), QStringLiteral("2"));
		query.addQueryItem(QStringLiteral("exlimit"), QStringLiteral("2"));
		query.addQueryItem(QStringLiteral("cllimit"), QStringLiteral("50"));
		query.addQueryItem(QStringLiteral("titles"), titles.join(QLatin1Char('|')));
		QUrl api(QStringLiteral("https://liquipedia.net/%1/api.php").arg(first));
		api.setQuery(query);
		target.requestUrl    = api;
		target.requestIsJson = true;
		return target;
	}

	if (host == QLatin1String("faceit.com")) {
		// /en/players/<nickname>, /en/cs2/room/<match id>; the language is optional.
		static const QRegularExpression s_language(QStringLiteral("^[a-z]{2}(?:-[A-Za-z]{2})?$"));
		static const QRegularExpression s_nickname(QStringLiteral("^[A-Za-z0-9_.-]{2,32}$"));
		static const QRegularExpression s_matchId(QStringLiteral("^[0-9a-fA-F-]{20,48}$"));
		const int offset = s_language.match(first).hasMatch() ? 1 : 0;
		const QString section = segments.value(offset).toLower();
		target.provider = QStringLiteral("faceit");
		target.requestIsJson = true;
		if (section == QLatin1String("players") && s_nickname.match(segments.value(offset + 1)).hasMatch()) {
			target.kind       = QStringLiteral("player");
			target.id         = segments.value(offset + 1);
			target.requestUrl = QUrl(QStringLiteral("https://www.faceit.com/api/users/v1/nicknames/") + target.id);
			return target;
		}
		if (segments.value(offset + 1).toLower() == QLatin1String("room")
			&& s_matchId.match(segments.value(offset + 2)).hasMatch()) {
			target.kind       = QStringLiteral("match");
			target.id         = segments.value(offset + 2);
			target.requestUrl = QUrl(QStringLiteral("https://www.faceit.com/api/match/v2/match/") + target.id);
			return target;
		}
		return std::nullopt;
	}

	if (host == QLatin1String("leetify.com") || host == QLatin1String("csstats.gg")) {
		const bool leetify = host == QLatin1String("leetify.com");
		target.provider = leetify ? QStringLiteral("leetify") : QStringLiteral("csstats");
		target.requestIsJson = true;
		// leetify.com/app/profile/<id>, /public/profile/<id>; csstats.gg/player/<SteamID64>.
		const QString profileId = leetify
			? ((first == QLatin1String("app") || first == QLatin1String("public")) && second == QLatin1String("profile")
				   ? segments.value(2) : QString())
			: (first == QLatin1String("player") ? second : QString());
		if (isSteamId64(profileId) || (leetify && isUuid(profileId))) {
			target.kind = QStringLiteral("player");
			target.id   = profileId;
			QUrl api(LeetifyApi + QStringLiteral("/v3/profile"));
			api.setQuery(QStringLiteral("%1=%2").arg(isSteamId64(profileId) ? QStringLiteral("steam64_id")
																			 : QStringLiteral("id"),
													 profileId));
			target.requestUrl = api;
			return target;
		}
		if (leetify && first == QLatin1String("app") && second == QLatin1String("match-details")
			&& isUuid(segments.value(2))) {
			target.kind       = QStringLiteral("match");
			target.id         = segments.value(2);
			target.requestUrl = QUrl(LeetifyApi + QStringLiteral("/v2/matches/") + target.id);
			return target;
		}
		static const QRegularExpression s_number(QStringLiteral("^[0-9]{1,12}$"));
		if (!leetify && first == QLatin1String("match") && s_number.match(second).hasMatch()) {
			// csstats.gg walls its pages; a match's number is all the address says.
			target.kind          = QStringLiteral("match");
			target.id            = second;
			target.requestIsJson = false;
			return target;
		}
		return std::nullopt;
	}

	if (host == QLatin1String("csst.at")) {
		static const QRegularExpression s_profile(QStringLiteral("^[A-Za-z0-9_-]{2,64}$"));
		if (first != QLatin1String("profile") || !s_profile.match(second).hasMatch()) {
			return std::nullopt;
		}
		target.provider   = QStringLiteral("csstat");
		target.kind       = QStringLiteral("player");
		target.id         = second;
		target.requestUrl = QUrl(QStringLiteral("https://csst.at/profile/") + second);
		return target;
	}
	return std::nullopt;
}

QVariantMap metadataFromUrl(const Target &target) {
	QVariantMap metadata = baseMetadata(target);
	static const QHash< QString, QString > hltvSections {
		{ QStringLiteral("player"), QStringLiteral("Player") }, { QStringLiteral("team"), QStringLiteral("Team") },
		{ QStringLiteral("event"), QStringLiteral("Event") },   { QStringLiteral("match"), QStringLiteral("Match") }
	};
	if (target.provider == QLatin1String("hltv")) {
		insertText(metadata, QStringLiteral("esportsSection"), hltvSections.value(target.kind));
		if (target.kind == QLatin1String("match")) {
			// "faze-vs-natus-vincere-iem-cologne-2026": where the second team's name
			// ends and the event's begins is not in the address.
			const qsizetype versus = target.slug.indexOf(QLatin1String("-vs-"));
			insertText(metadata, QStringLiteral("esportsTitle"),
					   versus > 0 ? titleFromSlug(target.slug.left(versus)) + QStringLiteral(" vs ")
										+ titleFromSlug(target.slug.mid(versus + 4))
								  : titleFromSlug(target.slug));
		} else if (target.kind == QLatin1String("player")) {
			// Nicknames keep their own case; the address has them in lower case.
			insertText(metadata, QStringLiteral("esportsTitle"), target.slug, 128);
		} else {
			insertText(metadata, QStringLiteral("esportsTitle"), titleFromSlug(target.slug));
		}
	} else if (target.provider == QLatin1String("liquipedia")) {
		insertText(metadata, QStringLiteral("esportsTitle"), target.id, 256);
		insertText(metadata, QStringLiteral("esportsSection"), liquipediaGame(target.wiki), 128);
	} else if (target.provider == QLatin1String("faceit")) {
		if (target.kind == QLatin1String("player")) {
			insertText(metadata, QStringLiteral("esportsTitle"), target.id, 64);
			metadata.insert(QStringLiteral("esportsSection"), QStringLiteral("Player"));
		} else {
			metadata.insert(QStringLiteral("esportsSection"), QStringLiteral("Match room"));
		}
	} else if (target.kind == QLatin1String("match")) {
		metadata.insert(QStringLiteral("esportsSection"), QStringLiteral("CS2 match"));
		if (target.provider == QLatin1String("csstats")) {
			metadata.insert(QStringLiteral("esportsTitle"), QStringLiteral("Match %1").arg(target.id));
		}
	} else {
		metadata.insert(QStringLiteral("esportsSection"), QStringLiteral("CS2 player"));
	}
	return metadata;
}

QVariantMap metadataFromJson(const Target &target, const QByteArray &json) {
	QJsonParseError error;
	const QJsonDocument document = QJsonDocument::fromJson(json, &error);
	if (error.error != QJsonParseError::NoError || !document.isObject()) {
		return {};
	}
	const QJsonObject root = document.object();
	if (target.provider == QLatin1String("liquipedia")) {
		return liquipediaMetadata(target, root);
	}
	if (target.provider == QLatin1String("faceit")) {
		return target.kind == QLatin1String("match") ? faceitMatchMetadata(target, root)
													 : faceitPlayerMetadata(target, root);
	}
	if (target.provider == QLatin1String("leetify") || target.provider == QLatin1String("csstats")) {
		return target.kind == QLatin1String("match") ? leetifyMatchMetadata(target, root)
													 : leetifyProfileMetadata(target, root);
	}
	return {};
}

QVariantMap metadataFromHtml(const Target &target, const QString &html, const QHash< QString, QString > &metaTags) {
	if (target.provider == QLatin1String("hltv")) {
		return hltvMetadata(target, html, metaTags);
	}
	if (target.provider == QLatin1String("csstat")) {
		return csstatMetadata(target, html, metaTags);
	}
	return {};
}
}
