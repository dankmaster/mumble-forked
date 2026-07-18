import QtQuick
import QtQuick.Controls
import QtTest
import Mumble.Theme 1.0
import Mumble.ProviderPresentation 1.0

TestCase {
	id: testCase
	name: "ProviderDetails"
	when: windowShown
	visible: true
	width: 760
	height: 900

	Loader {
		id: detailsLoader
		width: 680
		height: item ? item.implicitHeight : 0
		Component.onCompleted: setSource("qrc:/qml-shell/ProviderDetails.qml", {
			"metadata": { "financePrice": "100", "financeCurrency": "SEK" },
			"previewKind": "finance",
			"expanded": true
		})
	}

	function details() {
		tryVerify(function() { return detailsLoader.item !== null })
		return detailsLoader.item
	}

	function test_provider_presentation_catalog_covers_native_provider_families() {
		const emittedProviders = [
			"direct", "youtube", "spotify", "tiktok", "instagram", "twitch", "streamable",
			"vimeo", "dailymotion", "soundcloud", "bandcamp", "giphy", "bluesky",
			"mastodon", "reddit", "x", "github", "google", "googlefinance", "yahoofinance",
			"avanza", "nordnet", "interactivebrokers", "tenor", "threads", "patreon",
			"facebook", "imgur", "4chan",
			"tradera", "blocket", "bytbil", "bilweb", "flashback", "sweclockers",
			"existenz", "hemnet", "booli", "prisjakt", "pricerunner", "gp", "svt",
			"omni", "aftonbladet", "expressen", "dn", "sverigesradio", "inet",
			"webhallen", "elgiganten", "power", "komplett", "systembolaget", "amazon",
			"smhi", "klart", "yr", "hitta", "eniro", "googlemaps", "sj", "sl",
			"vasttrafik", "steam", "gamestore", "g2a", "kinguin", "epic", "gog",
			"ubisoft", "ea", "humble", "fanatical", "greenmangaming", "itch",
			"battlenet", "xbox"
		]
		const missing = []
		for (const token of emittedProviders) {
			const presentation = ProviderPresentation.resolve(token)
			if (!presentation.known || presentation.label.length === 0
					|| presentation.mark.length === 0 || presentation.accent.length === 0
					|| presentation.family.length === 0)
				missing.push(token)
		}
		compare(missing.length, 0, "Missing provider presentations: " + missing.join(", "))
		compare(ProviderPresentation.resolve("twitter").token, "x")
		compare(ProviderPresentation.resolve("Twitter/X").token, "x")
		compare(ProviderPresentation.resolve("google-search").token, "google")
		compare(ProviderPresentation.resolve("Epic Games Store").token, "epic")
		compare(ProviderPresentation.resolve("Västtrafik").token, "vasttrafik")
		compare(ProviderPresentation.resolve("Ubisoft Store").token, "ubisoft")
		compare(ProviderPresentation.resolve("Humble Store").token, "humble")
		compare(ProviderPresentation.resolve("itch.io").token, "itch")
		compare(ProviderPresentation.resolve("Xbox Store").token, "xbox")
	}

	function setFixture(metadata, kind, expanded, provider, title, subtitle, description) {
		const item = details()
		item.metadata = metadata
		item.previewKind = kind || ""
		item.providerHint = provider || ""
		item.previewTitle = title || ""
		item.previewSubtitle = subtitle || ""
		item.previewDescription = description || ""
		item.previewImageSource = ""
		item.expanded = expanded === undefined ? true : expanded
		wait(0)
		return item
	}

	function exposedProviderGroupingCount(item) {
		let count = item.visible && !item.Accessible.ignored
			&& item.Accessible.role === Accessible.Grouping ? 1 : 0
		const cardNames = ["providerSteamCard", "providerGoogleSearch", "providerFlashbackThread",
			"providerXPost", "providerInstagramPost", "providerGitHubRepository",
			"providerTwitchStream"]
		for (let index = 0; index < cardNames.length; ++index) {
			const card = findChild(item, cardNames[index])
			if (card && card.visible && !card.Accessible.ignored
					&& card.Accessible.role === Accessible.Grouping)
				++count
		}
		return count
	}

	function instantiatedSocialCardCount(item) {
		const cardNames = ["providerXPost", "providerInstagramPost", "providerGitHubRepository",
			"providerTwitchStream"]
		let count = 0
		for (let index = 0; index < cardNames.length; ++index) {
			if (findChild(item, cardNames[index]) !== null)
				++count
		}
		return count
	}

	function occurrenceCount(value, needle) {
		let count = 0
		let offset = 0
		while (needle.length > 0) {
			const found = value.indexOf(needle, offset)
			if (found < 0)
				break
			++count
			offset = found + needle.length
		}
		return count
	}

	function test_family_matrix_data() {
		return [
			{ "tag": "finance", "family": "finance", "variant": "finance", "kind": "finance",
			  "metadata": { "financePrice": "127.40", "financeCurrency": "SEK",
				"financeDayChange": "+2.40", "financeDayChangePercent": "+1.92%",
				"financeDayTrend": "up", "tickerSymbol": "MUM", "financeExchange": "STO",
				"financeSparkline": [100, 101, 99, 103] } },
			{ "tag": "product", "family": "commerce", "variant": "product", "kind": "product",
			  "metadata": { "productPrice": "1 299 kr", "productAvailability": "In stock",
				"productBrand": "Example", "productSpecs": [{ "label": "Memory", "value": "32 GB" }] } },
			{ "tag": "systembolaget", "family": "commerce", "variant": "product", "kind": "systembolagetProduct",
			  "metadata": { "productPrice": "129 kr", "productVolume": "750 ml",
				"productAlcohol": "13%" } },
			{ "tag": "game-store", "family": "commerce", "variant": "game", "kind": "gameStoreProduct",
			  "metadata": { "steamPrice": "29.99 €", "steamDeveloper": "Studio",
				"steamReviewSummary": "Very positive", "gameStoreTags": ["Co-op", "Strategy"] } },
			{ "tag": "marketplace", "family": "commerce", "variant": "marketplace", "kind": "marketplaceListing",
			  "metadata": { "listingPrice": "450 kr", "listingCondition": "Used",
				"listingLocation": "Uppsala", "listingSpecs": [{ "label": "Size", "value": "M" }] } },
			{ "tag": "vehicle", "family": "commerce", "variant": "vehicle", "kind": "vehicleListing",
			  "metadata": { "vehiclePrice": "245 000 kr", "vehicleYear": "2024",
				"vehicleMileage": "1 200 mil", "vehicleHighlights": ["CarPlay", "Tow bar"] } },
			{ "tag": "real-estate", "family": "commerce", "variant": "realEstate", "kind": "realEstate",
			  "metadata": { "realEstatePrice": "4 250 000 kr", "realEstateArea": "82 m²",
				"realEstateRooms": "3 rooms", "realEstateFee": "4 200 kr/month" } },
			{ "tag": "article", "family": "editorial", "variant": "article", "kind": "article",
			  "metadata": { "articleSection": "Technology", "articleAuthor": "Writer",
				"articlePublishedAt": "2026-07-13", "articlePremium": true } },
			{ "tag": "forum", "family": "editorial", "variant": "forum", "kind": "forum",
			  "metadata": { "forumProvider": "Forum", "forumThreadId": "123",
				"forumPostAuthor": "Member", "forumPostCount": "42" } },
			{ "tag": "audio", "family": "editorial", "variant": "audio", "kind": "audio",
			  "metadata": { "audioProvider": "Radio", "audioProgram": "Morning show",
				"articlePublishedAt": "08:30" } },
			{ "tag": "link-digest", "family": "editorial", "variant": "linkDigest", "kind": "linkDigest",
			  "metadata": { "provider": "existenz", "linkDigestTitle": "Daily links",
				"linkDigestCaption": "A bounded plain-text digest" } },
			{ "tag": "x", "family": "social", "variant": "x", "kind": "x",
			  "metadata": { "xDisplayName": "User", "xHandle": "@user", "xVerified": true,
				"xLikeCount": 12500, "xReplyCount": 3 } },
			{ "tag": "instagram", "family": "social", "variant": "instagram", "kind": "instagram",
			  "metadata": { "instagramHandle": "@photo", "instagramLikeCount": 221,
				"instagramCommentCount": 12, "instagramMediaKind": "reel" } },
			{ "tag": "github", "family": "social", "variant": "github", "kind": "github",
			  "metadata": { "githubRepo": "client", "githubStars": 4200, "githubForks": 110,
				"githubLanguage": "C++", "githubTopics": ["qml", "voice"] } },
			{ "tag": "twitch", "family": "social", "variant": "twitch", "kind": "video",
			  "provider": "twitch", "metadata": { "provider": "twitch", "twitchChannel": "mumbledev",
				"twitchBadge": "Live", "twitchLiveState": "live", "twitchViewerCount": 420 } },
			{ "tag": "weather", "family": "geo", "variant": "weather", "kind": "weather",
			  "metadata": { "locationLabel": "Stockholm", "statusLabel": "18 °C and clear" } },
			{ "tag": "place", "family": "geo", "variant": "place", "kind": "place",
			  "metadata": { "locationLabel": "Central station", "statusLabel": "Open" } },
			{ "tag": "traffic", "family": "geo", "variant": "traffic", "kind": "traffic",
			  "metadata": { "locationLabel": "Line 4", "statusLabel": "Minor delays" } },
			{ "tag": "google-search", "family": "search", "variant": "googleSearch", "kind": "search",
			  "provider": "google-search", "metadata": { "previewKind": "search",
				"googleSearchQuery": "Qt Quick model performance", "googleSearchModeLabel": "Google search" } },
			{ "tag": "warning", "family": "warning", "variant": "warning", "kind": "generic",
			  "metadata": { "contentWarning": "Sensitive content" } }
		]
	}

	function test_family_matrix(data) {
		const item = setFixture(data.metadata, data.kind, true, data.provider || "")
		compare(item.family, data.family)
		compare(item.variant, data.variant)
		const expectedPresentation = data.family === "finance" ? "market"
			: data.family === "commerce" ? "commerce"
			: ["audio", "x", "instagram", "github", "twitch"].indexOf(data.variant) >= 0
				? "identity" : "details"
		compare(item.presentation, expectedPresentation)
		verify(item.hasDetails)
		verify(item.Accessible.name.length > 0)
		tryVerify(function() { return item.implicitHeight > 0 })
	}

	function test_family_presentations_prioritize_quote_price_and_identity() {
		detailsLoader.width = 680
		let item = setFixture({
			"financePrice": "448.37", "financeCurrency": "USD",
			"financeDayChange": "+5.21", "financeDayChangePercent": "+1.18%",
			"financeDayTrend": "up", "financeRangeLabel": "1M",
			"financeRangeChangePercent": "+2.85%", "tickerSymbol": "MSFT",
			"financeName": "Microsoft Corporation", "financeExchange": "NasdaqGS",
			"financeInstrument": "EQUITY", "financeUpdatedAt": "15:42",
			"financeSparkline": [435, 438, 436, 442, 448]
		}, "finance", true)
		compare(item.presentation, "market")
		compare(findChild(item, "providerSummaryTitle").text, "MSFT")
		compare(findChild(item, "providerDetailsPrimary").text, "448.37 USD")
		verify(findChild(item, "providerFinanceChart").visible)
		compare(findChild(item, "providerDetailsSparkline").pointCount, 5)
		compare(findChild(item, "providerStat_0").presentation, "market")

		item = setFixture({
			"previewKind": "product", "productPrice": "1 499 kr",
			"productAvailability": "In stock", "productBrand": "Example",
			"productDescription": "A bounded plain-text product description.",
			"productSpecs": [{ "label": "Memory", "value": "32 GB" }]
		}, "product", true, "", "Product")
		compare(item.presentation, "commerce")
		compare(findChild(item, "providerDetailsPrimary").text, "1 499 kr")
		compare(findChild(item, "providerCommerceStatus").text, "In stock")
		compare(findChild(item, "providerSummaryBody").text,
			"A bounded plain-text product description.")
		compare(findChild(item, "providerSummaryBody").textFormat, Text.PlainText)
		compare(findChild(item, "providerStat_0").presentation, "spec")
		compare(findChild(item, "providerStat_0").color.a, 0)
	}

	function test_identity_first_families_keep_provider_accent_small_and_metrics_flat() {
		detailsLoader.width = 680
		let item = setFixture({
			"previewKind": "audio", "audioProvider": "Sveriges Radio",
			"audioProgram": "Vetenskapsradion", "articlePublishedAt": "08:30"
		}, "audio", true, "", "Episode", "Sveriges Radio",
			"A conversation about measurable performance.")
		compare(item.presentation, "identity")
		compare(item.identityTitle, "Vetenskapsradion")
		verify(item.identitySubtitle.indexOf("Sveriges Radio") >= 0)
		compare(findChild(item, "providerIdentityMarkLabel").text, "SR")
		compare(findChild(item, "providerIdentityMark").presentation, "mark")
		compare(findChild(item, "providerIdentityBody").text,
			"A conversation about measurable performance.")
		compare(findChild(item, "providerIdentity").color.a, 0)
		verify(findChild(item, "providerIdentityMark").color.a > 0)
		verify(findChild(item, "providerIdentityMark").width <= 48)
		compare(item.allStats.length, 0)
		compare(findChild(item, "providerDetailsStats").visible, false)
		compare(occurrenceCount(item.Accessible.description, "Sveriges Radio"), 1)
		compare(occurrenceCount(item.Accessible.description, "Vetenskapsradion"), 1)
		compare(occurrenceCount(item.Accessible.description, "08:30"), 1)

		item = setFixture({
			"previewKind": "x", "xDisplayName": "Mumble Design", "xHandle": "@mumbledesign",
			"xVerified": true, "xCreatedAt": "18:30", "xReplyCount": 757,
			"xRepostCount": 12000, "xLikeCount": 362000
		}, "x", true, "", "Post", "X", "Frame pacing is a feature.")
		compare(item.presentation, "identity")
		compare(item.identityTitle, "Mumble Design")
		verify(item.identitySubtitle.indexOf("@mumbledesign") >= 0)
		verify(findChild(item, "providerXPost").visible)
		verify(findChild(item, "providerXVerified").visible)
		compare(findChild(item, "providerXPostText").text, "Frame pacing is a feature.")
		compare(findChild(item, "providerIdentity").visible, false)
		compare(findChild(item, "providerDetailsStats").visible, false)

		item = setFixture({
			"provider": "instagram", "previewKind": "instagram",
			"instagramDisplayName": "Mumble Quick", "instagramHandle": "@mumblequick",
			"instagramCaption": "Stable scrolling with bounded delegates.",
			"instagramLikeCount": 18420, "instagramCommentCount": 318
		}, "instagram", true)
		compare(item.presentation, "identity")
		compare(item.identityTitle, "Mumble Quick")
		verify(findChild(item, "providerInstagramPost").visible)
		compare(findChild(item, "providerInstagramCaption").text,
			"Stable scrolling with bounded delegates.")
		compare(findChild(item, "providerIdentity").visible, false)

		item = setFixture({
			"provider": "twitch", "twitchDisplayName": "Mumble Dev",
			"twitchChannel": "mumbledev", "twitchLiveState": "live",
			"twitchBadge": "Live", "twitchGame": "Software Development",
			"twitchViewerCount": 1842
		}, "video", true, "twitch")
		compare(item.presentation, "identity")
		verify(findChild(item, "providerTwitchStream").visible)
		compare(findChild(item, "providerTwitchState").visible, true)
		compare(findChild(item, "providerTwitchAudience").text, "1.8K watching now")
		compare(findChild(item, "providerIdentity").visible, false)
	}

	function test_stable_kind_and_provider_routing_precedes_field_heuristics() {
		let item = setFixture({
			"previewKind": "vehicleListing", "githubRepo": "must-not-win", "githubStars": 99,
			"vehiclePrice": "200 000 kr"
		}, "link", true, "github")
		compare(item.family, "commerce")
		compare(item.variant, "vehicle")

		item = setFixture({ "provider": "flashback", "xHandle": "@must-not-win",
			"forumThreadId": "42" }, "link", true)
		compare(item.family, "editorial")
		compare(item.variant, "forum")

		item = setFixture({ "provider": "google-search", "googleSearchQuery": "native qml" },
			"link", true, "", "Google search", "Google", "native qml")
		compare(item.family, "search")
		compare(item.variant, "googleSearch")
	}

	function test_sparse_details_do_not_create_empty_accessibility_groups() {
		const item = setFixture({}, "", false, "", "", "", "")
		compare(item.hasDetails, false)
		compare(item.implicitHeight, 0)
		compare(item.Accessible.ignored, true)

		setFixture({
			"previewKind": "audio", "audioProvider": "Sveriges Radio",
			"audioProgram": "Vetenskapsradion"
		}, "audio", false, "Sveriges Radio", "Vetenskapsradion", "Sveriges Radio", "")
		compare(item.hasDetails, true)
		compare(item.bespokeSemanticOwner, false)
		compare(item.Accessible.ignored, false)
		compare(findChild(item, "providerIdentityMarkLabel").Accessible.ignored, true)
	}

	function test_sparse_known_providers_keep_their_product_family_and_variant() {
		const fixtures = [
			{ "provider": "yahoo-finance", "family": "finance", "variant": "finance" },
			{ "provider": "tradera", "family": "commerce", "variant": "marketplace" },
			{ "provider": "hemnet", "family": "commerce", "variant": "realEstate" },
			{ "provider": "inet", "family": "commerce", "variant": "product" },
			{ "provider": "steam", "family": "commerce", "variant": "game" },
			{ "provider": "sweclockers", "family": "editorial", "variant": "article" },
			{ "provider": "sveriges-radio", "family": "editorial", "variant": "audio" },
			{ "provider": "smhi", "family": "geo", "variant": "weather" },
			{ "provider": "hitta", "family": "geo", "variant": "place" },
			{ "provider": "sj", "family": "geo", "variant": "traffic" },
			{ "provider": "reddit", "family": "social", "variant": "reddit" }
		]
		for (const fixture of fixtures) {
			const item = setFixture({ "previewProvider": fixture.provider }, "", false,
				fixture.provider, "Sparse provider", "", "")
			compare(item.family, fixture.family, fixture.provider + " family")
			compare(item.variant, fixture.variant, fixture.provider + " variant")
		}
	}

	function test_generic_social_posts_have_one_visual_and_semantic_owner() {
		const fixtures = [
			{ "provider": "bluesky", "name": "Bluesky",
			  "author": "Ada (@ada.bsky.social)", "post": "Bounded delegates stay smooth.",
			  "description": "Bluesky", "expectedDescription": "" },
			{ "provider": "mastodon", "name": "Mastodon",
			  "author": "@ada@social.example", "post": "Native previews keep their identity.",
			  "description": "Mastodon", "expectedDescription": "" },
			{ "provider": "reddit", "name": "Reddit",
			  "author": "r/mumble", "post": "Modern client preview parity",
			  "description": "A focused discussion excerpt.",
			  "expectedDescription": "A focused discussion excerpt." }
		]

		for (const fixture of fixtures) {
			const item = setFixture({ "previewProvider": fixture.provider }, "link", false,
				fixture.provider, fixture.post, fixture.author, fixture.description)
			const card = findChild(item, "providerSocialPost")
			const badge = findChild(item, "providerSocialIdentityBadge")
			const badgeLabel = findChild(item, "providerSocialIdentityLabel")
			const author = findChild(item, "providerSocialAuthor")
			const post = findChild(item, "providerSocialPostText")
			const description = findChild(item, "providerSocialPostDescription")

			compare(item.genericSocialPostPresentation, true, fixture.provider)
			compare(item.presentation, "socialPost", fixture.provider)
			compare(item.ownsHeader, true, fixture.provider)
			compare(item.ownsDescription, true, fixture.provider)
			compare(item.Accessible.ignored, true, fixture.provider)
			verify(card !== null && card.visible, fixture.provider)
			compare(card.Accessible.role, Accessible.Grouping)
			compare(card.Accessible.name, "Social post")
			compare(badge.presentation, "inline")
			compare(badgeLabel.text, fixture.name.toUpperCase())
			compare(author.text, fixture.author)
			compare(post.text, fixture.post)
			compare(description.text, fixture.expectedDescription)
			compare(description.visible, fixture.expectedDescription.length > 0)
			compare(findChild(item, "providerIdentity").visible, false)
			compare(findChild(item, "providerSummary").visible, false)
			compare(findChild(item, "providerSocialPresentationLoader").active, false)
			compare(occurrenceCount(card.Accessible.description, fixture.name), 1)
			compare(occurrenceCount(card.Accessible.description, fixture.author), 1)
			compare(occurrenceCount(card.Accessible.description, fixture.post), 1)
			if (fixture.expectedDescription.length > 0)
				compare(occurrenceCount(card.Accessible.description, fixture.expectedDescription), 1)
			compare(badgeLabel.Accessible.ignored, true)
			compare(author.Accessible.ignored, true)
			compare(post.Accessible.ignored, true)
		}

		const item = setFixture({ "previewProvider": "bluesky" }, "link", false,
			"bluesky", "Fallback post", "Bluesky", "Bluesky")
		compare(item.socialPostAuthor, "")
		compare(findChild(item, "providerSocialAuthor").visible, false)
	}

	function test_visual_provider_identity_matrix_data() {
		return [
			{ "tag": "youtube", "provider": "youtube", "token": "youtube", "name": "YouTube",
			  "mark": "YT", "accent": "#ff5a5f" },
			{ "tag": "spotify", "provider": "spotify", "token": "spotify", "name": "Spotify",
			  "mark": "SP", "accent": "#1ed760" },
			{ "tag": "tiktok", "provider": "tiktok", "token": "tiktok", "name": "TikTok",
			  "mark": "TT", "accent": "#25f4ee" },
			{ "tag": "instagram", "provider": "instagram", "token": "instagram", "name": "Instagram",
			  "mark": "IG", "accent": "#e45aa5" },
			{ "tag": "twitch", "provider": "twitch", "token": "twitch", "name": "Twitch",
			  "mark": "TV", "accent": "#a970ff" },
			{ "tag": "google", "provider": "google-search", "token": "google", "name": "Google",
			  "mark": "G", "accent": "#4285f4" },
			{ "tag": "finance", "provider": "yahoo-finance", "token": "yahoofinance",
			  "name": "Yahoo Finance", "mark": "YF", "accent": "#78d6a3" },
			{ "tag": "blocket", "provider": "blocket", "token": "blocket", "name": "Blocket",
			  "mark": "B", "accent": "#ff6b61" },
			{ "tag": "tradera", "provider": "tradera", "token": "tradera", "name": "Tradera",
			  "mark": "T", "accent": "#ffd64c" },
			{ "tag": "bytbil", "provider": "bytbil", "token": "bytbil", "name": "Bytbil",
			  "mark": "BB", "accent": "#f4c95d" },
			{ "tag": "bilweb", "provider": "bilweb", "token": "bilweb", "name": "Bilweb",
			  "mark": "BW", "accent": "#f4c95d" },
			{ "tag": "booli", "provider": "booli", "token": "booli", "name": "Booli",
			  "mark": "B", "accent": "#72d7a3" },
			{ "tag": "hemnet", "provider": "hemnet", "token": "hemnet", "name": "Hemnet",
			  "mark": "H", "accent": "#72d7a3" },
			{ "tag": "flashback", "provider": "flashback", "token": "flashback", "name": "Flashback",
			  "mark": "FB", "accent": "#f2c36e" }
		]
	}

	function test_visual_provider_identity_matrix(data) {
		const metadata = { "previewProvider": data.provider }
		if (data.provider === "yahoo-finance") {
			metadata.financePrice = "100"
			metadata.tickerSymbol = "MUM"
		}
		const item = setFixture(metadata, "", true, data.provider)
		compare(item.providerToken, data.token)
		compare(item.providerDisplayName, data.name)
		compare(item.providerMark, data.mark)
		compare(String(item.providerAccent).toLowerCase(), data.accent)
	}

	function test_game_store_uses_actual_store_identity_and_formats_steam_commerce() {
		const item = setFixture({
			"previewProvider": "game-store", "previewKind": "gameStoreProduct",
			"gameStoreProvider": "steam", "gameStoreName": "Steam",
			"steamPrice": "29,99 €", "steamOriginalPrice": "39,99 €",
			"steamDiscountPercent": 25, "steamPlatforms": "Windows / Linux",
			"steamReviewSummary": "Very Positive", "steamReviewPercent": 92,
			"steamReviewTotal": 58420, "steamReviewScore": 8,
			"steamMetacriticScore": 86
		}, "gameStoreProduct", true)
		compare(item.family, "commerce")
		compare(item.variant, "game")
		compare(item.providerToken, "steam")
		compare(item.providerDisplayName, "Steam")
		compare(item.providerMark, "S")
		compare(item.secondaryValue, "39,99 €  -25%")
		const reviews = item.allStats.filter(function(entry) { return entry.label === "Reviews" })[0]
		verify(reviews !== undefined)
		compare(reviews.value, "Very Positive · 92% positive · 58K reviews")
		compare(reviews.tone, "success")
		const score = item.allStats.filter(function(entry) { return entry.label === "Score" })[0]
		verify(score !== undefined)
		compare(score.tone, "success")
		compare(item.steamPresentation, true)
		verify(findChild(item, "providerSteamCard").visible)
		compare(findChild(item, "providerSteamProductName").text, "Steam app")
		compare(findChild(item, "providerSteamReviewSummary").text, "Very Positive")
		compare(findChild(item, "providerSteamReviewDetail").text,
			"92% positive · 58K reviews")
		compare(findChild(item, "providerSteamMetacriticScore").text, "86")
		compare(findChild(item, "providerSteamDiscount").visible, true)
		compare(findChild(item, "providerSteamOriginalPrice").font.strikeout, true)
		compare(findChild(item, "providerSteamFinalPrice").text, "29,99 €")
		compare(findChild(item, "providerSummary").visible, false)
	}

	function test_bespoke_provider_cards_expose_one_semantic_owner() {
		let item = setFixture({
			"previewProvider": "game-store", "previewKind": "gameStoreProduct",
			"gameStoreProvider": "steam", "steamAppName": "Factorio",
			"steamPrice": "29,99 €", "steamReviewSummary": "Very Positive"
		}, "gameStoreProduct", true, "", "Factorio")
		let card = findChild(item, "providerSteamCard")
		compare(item.Accessible.ignored, true)
		compare(card.Accessible.ignored, false)
		compare(card.Accessible.role, Accessible.Grouping)
		compare(card.Accessible.name, "Store details")
		compare(exposedProviderGroupingCount(item), 1)
		compare(findChild(item, "providerSteamProductName").Accessible.ignored, true)
		compare(findChild(item, "providerSteamReviewSummary").Accessible.ignored, true)

		item = setFixture({
			"provider": "google-search", "previewKind": "search",
			"googleSearchModeLabel": "Google Images", "googleSearchQuery": "Qt Quick"
		}, "search", true, "google-search", "Google Images")
		card = findChild(item, "providerGoogleSearch")
		compare(item.Accessible.ignored, true)
		compare(card.Accessible.ignored, false)
		compare(card.Accessible.role, Accessible.Grouping)
		compare(card.Accessible.name, "Google Images")
		compare(exposedProviderGroupingCount(item), 1)
		compare(findChild(item, "providerGoogleModeLabel").Accessible.ignored, true)
		compare(findChild(item, "providerGoogleQueryText").Accessible.ignored, true)

		item = setFixture({
			"provider": "flashback", "previewKind": "forum",
			"forumThreadTitle": "Renderloopar", "forumPostAuthor": "rendernisse",
			"forumPostExcerpt": "The linked post body."
		}, "forum", true, "flashback")
		card = findChild(item, "providerFlashbackThread")
		compare(item.Accessible.ignored, true)
		compare(card.Accessible.ignored, false)
		compare(card.Accessible.role, Accessible.Grouping)
		compare(card.Accessible.name, "Discussion details")
		compare(exposedProviderGroupingCount(item), 1)
		compare(findChild(item, "providerFlashbackLogo").Accessible.ignored, true)
		compare(findChild(item, "providerFlashbackTitle").Accessible.ignored, true)
		compare(findChild(item, "providerFlashbackAuthor").Accessible.ignored, true)
		compare(findChild(item, "providerFlashbackLinkContext").text, "Thread")
		verify(card.Accessible.description.indexOf("..") < 0)

		const socialFixtures = [
			{ "kind": "x", "name": "providerXPost", "metadata": {
				"previewKind": "x", "xDisplayName": "Mumble Design", "xHandle": "@mumbledesign",
				"xLikeCount": 42 }, "description": "Frame pacing." },
			{ "kind": "instagram", "name": "providerInstagramPost", "metadata": {
				"provider": "instagram", "instagramDisplayName": "Mumble Quick",
				"instagramHandle": "@mumblequick", "instagramCaption": "Stable delegates." } },
			{ "kind": "github", "name": "providerGitHubRepository", "metadata": {
				"provider": "github", "githubFullName": "dankmaster/mumble",
				"githubStars": 4200, "githubLanguage": "C++" } },
			{ "kind": "twitch", "name": "providerTwitchStream", "metadata": {
				"provider": "twitch", "twitchDisplayName": "Mumble Dev",
				"twitchChannel": "mumbledev", "twitchPlaybackNote": "Starts after interaction." } }
		]
		for (let index = 0; index < socialFixtures.length; ++index) {
			const fixture = socialFixtures[index]
			item = setFixture(fixture.metadata, fixture.kind, true,
				fixture.metadata.provider || "", "", "", fixture.description || "")
			card = findChild(item, fixture.name)
			compare(item.Accessible.ignored, true)
			compare(card.visible, true)
			compare(card.Accessible.ignored, false)
			verify(card.Accessible.role === Accessible.Grouping
				|| card.Accessible.role === Accessible.AlertMessage)
			compare(exposedProviderGroupingCount(item), 1)
			verify(card.Accessible.name.length > 0)
			if (fixture.kind === "github")
				compare(card.Accessible.name, "Repository details")
			verify(card.Accessible.description.indexOf("..") < 0)
		}

		item = setFixture({
			"previewKind": "product", "productPrice": "1 499 kr"
		}, "product", true, "", "Generic product")
		compare(item.Accessible.ignored, false)
		compare(exposedProviderGroupingCount(item), 1)
	}

	function test_social_provider_presentations_are_lazy_and_mutually_exclusive() {
		let item = setFixture({ "previewKind": "product", "productPrice": "10 kr" },
			"product", false)
		const loader = findChild(item, "providerSocialPresentationLoader")
		verify(loader !== null)
		compare(loader.active, false)
		compare(loader.item, null)
		compare(instantiatedSocialCardCount(item), 0)

		const fixtures = [
			{ "kind": "x", "name": "providerXPost", "metadata": {
				"previewKind": "x", "xHandle": "@mumble" } },
			{ "kind": "instagram", "name": "providerInstagramPost", "metadata": {
				"provider": "instagram", "instagramHandle": "@mumble" } },
			{ "kind": "github", "name": "providerGitHubRepository", "metadata": {
				"provider": "github", "githubRepo": "mumble" } },
			{ "kind": "twitch", "name": "providerTwitchStream", "metadata": {
				"provider": "twitch", "twitchChannel": "mumbledev" } }
		]
		for (let index = 0; index < fixtures.length; ++index) {
			const fixture = fixtures[index]
			item = setFixture(fixture.metadata, fixture.kind, false,
				fixture.metadata.provider || "")
			tryCompare(loader, "active", true)
			tryVerify(function() { return loader.item !== null })
			compare(loader.item.objectName, fixture.name)
			compare(instantiatedSocialCardCount(item), 1)
		}

		item = setFixture({ "previewKind": "audio", "audioProgram": "Morning" },
			"audio", false)
		tryCompare(loader, "active", false)
		tryCompare(loader, "item", null)
		compare(instantiatedSocialCardCount(item), 0)
	}

	function test_google_search_preserves_result_identity_and_active_tab() {
		const item = setFixture({
			"provider": "google-search", "previewKind": "search",
			"googleSearchQuery": "Qt Quick model performance",
			"googleSearchMode": "images", "googleSearchModeLabel": "Images"
		}, "search", true, "google-search", "Google Images")
		compare(item.googlePresentation, true)
		verify(findChild(item, "providerGoogleSearch").visible)
		compare(findChild(item, "providerGoogleQueryText").text,
			"Qt Quick model performance")
		compare(findChild(item, "providerGoogleTab_0").active, false)
		compare(findChild(item, "providerGoogleTab_1").active, true)
		compare(findChild(item, "providerSummary").visible, false)
		compare(findChild(item, "providerIdentity").visible, false)
	}

	function test_marketplace_prefers_scannable_chips_over_repeated_listing_stats() {
		const item = setFixture({
			"previewProvider": "tradera", "previewKind": "marketplaceListing",
			"listingPrice": "450 kr", "listingSaleType": "Auction",
			"listingEndsAt": "18:30", "listingId": "123456789"
		}, "marketplaceListing", true)
		compare(item.providerToken, "tradera")
		compare(item.rawChips.length, 3)
		compare(item.allStats.length, 0)
		compare(item.allChips.map(function(entry) { return entry.text }).join("|"),
			"Auction|Ends 18:30|#123456789")
	}

	function test_structured_surfaces_are_bounded_and_unknown_metadata_is_ignored() {
		const points = []
		const specs = []
		const topics = []
		const context = []
		for (let index = 0; index < 100; ++index) {
			points.push({ "close": 100 + index, "raw": "ignored" })
			specs.push({ "label": "Spec " + index, "value": "Value " + index, "raw": "ignored" })
			topics.push("topic-" + index)
			context.push({ "displayName": "User " + index, "handle": "@user" + index,
				"text": "Context " + index, "raw": { "secret": true } })
		}
		let item = setFixture({
			"financePrice": "100", "financeSparkline": points,
			"rawObject": { "secret": "never render" }, "rawList": ["never render"]
		}, "finance", true)
		compare(item.sparklinePoints.length, 64)
		compare(findChild(item, "providerDetailsSparkline").pointCount, 64)

		item = setFixture({ "productPrice": "10 kr", "productSpecs": specs }, "product", true)
		compare(item.allStats.length, 24)
		compare(item.visibleStats.length, 24)

		item = setFixture({ "githubRepo": "repo", "githubTopics": topics }, "social", true)
		compare(item.allChips.length, 8)

		item = setFixture({ "xHandle": "@user", "xReplyContext": context,
			"xQuotedPost": { "displayName": "Quote", "text": "Quoted" } }, "social", true)
		compare(item.contextPosts.length, 3)
		compare(findChild(item, "providerDetailsContext").visible, true)

		item = setFixture({ "rawObject": { "secret": "never render" },
			"rawList": ["never render"], "diagnostic": "not allowlisted" }, "", true)
		compare(item.family, "")
		compare(item.hasDetails, false)
		compare(item.allStats.length, 0)
		compare(item.allChips.length, 0)
	}

	function test_layout_is_bounded_at_long_340_420_680_760_1082_widths() {
		const item = setFixture({
			"vehiclePrice": "245 000 kr", "vehicleYear": "2024", "vehicleMileage": "1 200 mil",
			"vehicleFuel": "Electric", "vehicleTransmission": "Automatic",
			"vehicleDealer": "A deliberately long example dealership name that remains readable",
			"vehicleLocation": "A deliberately long location label in central Stockholm",
			"vehicleHighlights": ["CarPlay", "Tow bar", "Camera", "Heater"]
		}, "vehicleListing", true)
		for (const width of [340, 420, 680, 760, 1082]) {
			detailsLoader.width = width
			tryCompare(item, "width", width)
			const stats = findChild(item, "providerDetailsStats")
			tryCompare(stats, "width", width)
			compare(item.compactLayout, width < 440)
			for (let index = 0; index < item.visibleStats.length; ++index) {
				const tile = findChild(item, "providerStat_" + index)
				verify(tile !== null)
				const position = tile.mapToItem(item, 0, 0)
				verify(position.x >= -0.5 && position.x + tile.width <= width + 0.5,
					"stat " + index + " bounds " + position.x + "+" + tile.width + " at " + width)
			}
			for (let index = 0; index < item.visibleChips.length; ++index) {
				const chip = findChild(item, "providerChip_" + index)
				verify(chip !== null)
				tryVerify(function() {
					const position = chip.mapToItem(item, 0, 0)
					return position.x >= -0.5 && position.x + chip.width <= width + 0.5
				}, 1000, "chip " + index + " remains bounded at " + width)
			}
		}
	}

	function test_provider_chips_reserve_their_shared_horizontal_padding() {
		const item = setFixture({
			"vehiclePrice": "245 000 kr", "vehicleHighlights": ["CarPlay"]
		}, "vehicleListing", true)
		detailsLoader.width = 680
		tryCompare(item, "width", 680)
		const chip = findChild(item, "providerChip_0")
		const label = findChild(item, "providerChipLabel_0")
		verify(chip !== null && label !== null)
		compare(label.text, "CarPlay")
		verify(chip.width >= label.implicitWidth + Theme.space2 * 2,
			"provider chip must reserve both shared token margins")
		verify(chip.width <= detailsLoader.width)
	}

	function test_can_expand_only_when_renderable_content_is_hidden() {
		detailsLoader.width = 680
		let item = setFixture({ "productPrice": "10 kr", "productAvailability": "In stock" },
			"product", false)
		verify(item.hasDetails)
		compare(item.rawStats.length, 1)
		compare(item.allStats.length, 0)
		compare(item.commerceStatus, "In stock")
		compare(item.canExpand, false)

		item = setFixture({
			"productPrice": "10 kr", "productAvailability": "In stock", "productDelivery": "Tomorrow",
			"productRating": "4.8", "productBrand": "Example", "productSku": "SKU-1",
			"productVolume": "1 l"
		}, "product", false)
		verify(item.allStats.length > item.collapsedStatCount)
		compare(item.canExpand, true)

		item = setFixture({ "previewKind": "forum", "forumThreadId": "42",
			"forumPostAuthor": "rendernisse", "forumPostExcerpt": "A linked post excerpt" },
			"link", false)
		compare(item.contextPosts.length, 1)
		compare(item.canExpand, true)

		item = setFixture({ "rawDiagnostic": "ignored" }, "link", false)
		compare(item.hasDetails, false)
		compare(item.canExpand, false)

		const longDescription = Array(18).join(
			"A long product description that needs a deliberate expanded reading state. ")
		item = setFixture({ "previewKind": "product", "productDescription": longDescription },
			"product", false, "inet")
		verify(item.bodyTextCanExpand)
		verify(item.canExpand)
		const summaryBody = findChild(item, "providerSummaryBody")
		verify(summaryBody !== null && summaryBody.visible)
		compare(summaryBody.maximumLineCount, 2)
		item.expanded = true
		compare(item.canExpand, true, "expanded cards must retain the collapse action")
		compare(summaryBody.maximumLineCount, 5)

		item = setFixture({ "previewKind": "instagram", "instagramHandle": "@mumble",
			"instagramMediaKind": "Carousel" }, "instagram", false, "instagram")
		verify(item.socialCanExpand)
		verify(item.canExpand)
		const instagramMeta = findChild(item, "providerInstagramExpandedMeta")
		verify(instagramMeta !== null && !instagramMeta.visible)
		item.expanded = true
		tryCompare(instagramMeta, "visible", true)
		compare(instagramMeta.text, "Media: Carousel")
	}

	function test_primary_secondary_stack_and_finance_geo_values_are_not_duplicated() {
		let item = setFixture({
			"financePrice": "448.37", "financeCurrency": "USD", "financeDayChange": "+5.21",
			"financeDayChangePercent": "+1.18%", "financeDayTrend": "up", "tickerSymbol": "MSFT",
			"financeExchange": "NASDAQ", "financeRangeLabel": "1M",
			"financeRangeChangePercent": "+2.85%"
		}, "finance", false)
		detailsLoader.width = 340
		tryCompare(item, "width", 340)
		compare(item.compactLayout, true)
		const primary = findChild(item, "providerDetailsPrimary")
		const secondary = findChild(item, "providerDetailsSecondary")
		verify(primary !== null && secondary !== null)
		tryVerify(function() {
			const primaryPosition = primary.mapToItem(item, 0, 0)
			const secondaryPosition = secondary.mapToItem(item, 0, 0)
			return secondaryPosition.y >= primaryPosition.y + primary.height - 0.5
		}, 1000, "compact primary and secondary values stack after responsive relayout")
		compare(item.allStats.some(function(entry) { return entry.label === "Symbol" }), false)
		compare(item.rawStats.map(function(entry) { return entry.label }).join("|"),
			"Exchange|Range")
		compare(item.allStats.map(function(entry) { return entry.label }).join("|"), "Range")
		compare(item.rawChips.length, 1)
		compare(item.allChips.length, 0)

		item = setFixture({ "previewKind": "weather", "locationLabel": "Stockholm",
			"statusLabel": "12 °C and clear", "providerName": "SMHI" }, "link", true)
		compare(item.primaryValue, "Stockholm")
		compare(item.secondaryValue, "12 °C and clear")
		compare(item.rawStats.length, 1)
		compare(item.rawStats[0].label, "Provider")
		compare(item.allStats.length, 0)
	}

	function test_presentation_level_dedupe_and_ownership_cover_product_families() {
		let item = setFixture({
			"financePrice": "448.37", "financeCurrency": "USD",
			"financeDayChange": "+5.21", "financeDayChangePercent": "+1.18%",
			"tickerSymbol": "MSFT", "financeName": "Microsoft Corporation",
			"financeExchange": "NasdaqGS", "financeInstrument": "EQUITY",
			"financeRangeLabel": "1M", "financeRangeChangePercent": "+2.85%",
			"financeUpdatedAt": "15:42", "financeSparkline": [435, 448]
		}, "finance", true, "", "MSFT", "NasdaqGS",
			"448.37 USD · +5.21 +1.18%")
		compare(item.ownsHeader, false)
		compare(item.ownsDescription, true)
		compare(item.rawStats.map(function(entry) { return entry.label }).join("|"),
			"Exchange|Instrument|Range|Updated")
		compare(item.allStats.map(function(entry) { return entry.label }).join("|"), "Updated")
		compare(item.rawChips.length, 1)
		compare(item.allChips.length, 0)
		item.previewDescription = "A unique analyst note"
		wait(0)
		compare(item.ownsDescription, false)

		item = setFixture({
			"previewKind": "weather", "locationLabel": "Stockholm",
			"statusLabel": "12 °C and clear", "providerName": "SMHI"
		}, "weather", true, "", "Stockholm weather", "SMHI",
			"Stockholm · 12 °C and clear")
		compare(item.ownsHeader, false)
		compare(item.ownsDescription, true)
		compare(item.rawStats.map(function(entry) { return entry.label }).join("|"), "Provider")
		compare(item.allStats.length, 0)
		item.previewDescription = "A unique local forecast note"
		wait(0)
		compare(item.ownsDescription, false)

		item = setFixture({
			"previewProvider": "tradera", "previewKind": "marketplaceListing",
			"listingPrice": "450 kr", "listingCondition": "Used",
			"listingLocation": "Uppsala", "listingSaleType": "Auction",
			"listingEndsAt": "18:30", "listingId": "123456789",
			"listingSpecs": [{ "label": "Size", "value": "M" }]
		}, "marketplaceListing", true, "", "Road bike")
		compare(item.ownsHeader, false)
		compare(item.rawStats.map(function(entry) { return entry.label }).join("|"),
			"Condition|Location|Sale|Ends|Listing|Size")
		compare(item.allStats.map(function(entry) { return entry.label }).join("|"),
			"Location|Size")
		compare(item.rawChips.length, 3)
		compare(item.allChips.map(function(entry) { return entry.text }).join("|"),
			"Auction|Ends 18:30|#123456789")

		item = setFixture({
			"previewKind": "product", "providerName": "Acme", "productPrice": "1 499 kr",
			"productAvailability": "In stock", "productDelivery": "Tomorrow",
			"productBrand": "Acme",
			"productSpecs": [{ "label": "Memory", "value": "32 GB" }]
		}, "product", true, "", "Acme Workstation")
		item.previewImageSource = "image://mumble/product-thumb?g=1"
		wait(0)
		compare(item.ownsHeader, false)
		compare(item.previewImageSource, "image://mumble/product-thumb?g=1")
		compare(item.rawStats.map(function(entry) { return entry.label }).join("|"),
			"Availability|Delivery|Brand|Memory")
		compare(item.allStats.map(function(entry) { return entry.label }).join("|"),
			"Delivery|Memory")

		item = setFixture({
			"previewKind": "vehicleListing", "vehiclePrice": "245 000 kr",
			"vehicleKind": "SUV", "vehicleYear": "2024",
			"vehicleHighlights": ["CarPlay", "SUV"]
		}, "vehicleListing", true, "", "Example SUV")
		compare(item.ownsHeader, false)
		compare(item.allStats.map(function(entry) { return entry.label }).join("|"), "Year")
		compare(item.rawChips.map(function(entry) { return entry.text }).join("|"), "CarPlay|SUV")
		compare(item.allChips.map(function(entry) { return entry.text }).join("|"), "CarPlay")

		item = setFixture({
			"previewKind": "realEstate", "realEstatePrice": "4 250 000 kr",
			"realEstateArea": "82 m²", "realEstateRooms": "3 rooms",
			"realEstateFee": "4 200 kr/month", "listingLocation": "Uppsala"
		}, "realEstate", true, "", "Central apartment")
		compare(item.ownsHeader, false)
		compare(item.rawStats.map(function(entry) { return entry.label }).join("|"),
			"Area|Rooms|Fee|Location")
		compare(item.allStats.map(function(entry) { return entry.label }).join("|"),
			"Fee|Location")

		const geoFixtures = [
			{ "kind": "place", "location": "KTH", "status": "Open now",
				"provider": "Hitta", "title": "KTH place" },
			{ "kind": "traffic", "location": "Blue line", "status": "Minor delays",
				"provider": "SL", "title": "Blue line traffic" }
		]
		for (let index = 0; index < geoFixtures.length; ++index) {
			const fixture = geoFixtures[index]
			item = setFixture({
				"previewKind": fixture.kind, "locationLabel": fixture.location,
				"statusLabel": fixture.status, "providerName": fixture.provider
			}, fixture.kind, true, "", fixture.title, fixture.provider,
				fixture.location + " · " + fixture.status)
			compare(item.family, "geo")
			compare(item.ownsHeader, false)
			compare(item.ownsDescription, true)
			compare(item.rawStats.map(function(entry) { return entry.label }).join("|"),
				"Provider")
			compare(item.allStats.length, 0)
		}
	}

	function test_audio_and_link_digest_only_own_complete_identity_headers() {
		let item = setFixture({
			"previewKind": "audio", "audioProvider": "Sveriges Radio",
			"audioProgram": "Vetenskapsradion", "articlePublishedAt": "08:30"
		}, "audio", true, "Sveriges Radio", "Vetenskapsradion", "Sveriges Radio",
			"A conversation about measurable performance.")
		compare(item.ownsHeader, true)
		compare(item.rawStats.length, 0)
		compare(item.allStats.length, 0)
		item.previewTitle = "Episode 42"
		wait(0)
		compare(item.ownsHeader, false)
		item.previewTitle = "Vetenskapsradion"
		item.previewImageSource = "image://mumble/audio-cover?g=1"
		wait(0)
		compare(item.ownsHeader, false)

		item = setFixture({
			"provider": "existenz", "previewKind": "linkDigest",
			"linkDigestTitle": "Daily links", "linkDigestSource": "Existenz",
			"linkDigestCaption": "A bounded plain-text digest"
		}, "linkDigest", true, "existenz", "Daily links", "Existenz")
		compare(item.ownsHeader, true)
		compare(item.allStats.length, 0)
		compare(item.allChips.length, 0)
		item.previewSubtitle = "Existenz · Link digest"
		wait(0)
		compare(item.ownsHeader, true)
		item.previewTitle = "Existenz · 15 July"
		wait(0)
		compare(item.ownsHeader, false)
		item.previewTitle = "Daily links"
		item.previewImageSource = "image://mumble/digest-thumb?g=1"
		wait(0)
		compare(item.ownsHeader, false)
	}

	function test_twitch_state_and_identity_use_shared_token_surfaces() {
		let item = setFixture({
			"provider": "twitch", "twitchDisplayName": "Mumble Dev", "twitchChannel": "mumbledev",
			"twitchLiveState": "live", "twitchBadge": "Live", "twitchGame": "Software and Game Development",
			"twitchViewerCount": 12500, "twitchPlaybackNote": "Live provider playback"
		}, "video", false)
		compare(item.variant, "twitch")
		compare(item.identityTitle, "Mumble Dev")
		compare(item.allChips[0].tone, "success")
		compare(item.allChips.length, 1)
		compare(findChild(item, "providerIdentity").visible, false)
		verify(findChild(item, "providerTwitchStream").visible)
		compare(findChild(item, "providerTwitchNote").visible, false)

		item.metadata = {
			"provider": "twitch", "twitchChannel": "mumbledev", "twitchLiveState": "unavailable",
			"twitchBadge": "Unavailable"
		}
		wait(0)
		compare(item.allChips[0].tone, "danger")
		compare(item.providerStateLabel, "Unavailable")

		item.metadata = {
			"provider": "twitch", "twitchChannel": "mumbledev", "twitchLiveState": "offline",
			"twitchEmbedMode": "latest-vod"
		}
		wait(0)
		compare(item.providerStateLabel, "Offline · Latest VOD")
	}

	function test_provider_identity_does_not_repeat_google_instagram_or_twitch_metadata_as_chips() {
		let item = setFixture({
			"provider": "google-search", "googleSearchModeLabel": "Google Images",
			"googleSearchQuery": "Qt Quick render loop"
		}, "search", true, "google-search", "Google Images")
		compare(item.variant, "googleSearch")
		compare(item.identityTitle, "Google Images")
		compare(item.allStats.length, 0)

		item = setFixture({
			"provider": "instagram", "instagramDisplayName": "Mumble Design",
			"instagramHandle": "@mumbledesign", "instagramMediaKind": "reel",
			"instagramLikeCount": 221
		}, "instagram", true)
		compare(item.variant, "instagram")
		compare(item.identitySubtitle, "@mumbledesign")
		compare(item.allChips.length, 0)
		verify(item.allStats.some(function(entry) { return entry.label === "Media" }))

		item = setFixture({
			"provider": "twitch", "twitchDisplayName": "Mumble Dev",
			"twitchBadge": "Live", "twitchLiveState": "live",
			"twitchGame": "Software and Game Development"
		}, "twitch", true)
		compare(item.variant, "twitch")
		compare(item.allChips.length, 1)
		compare(item.allChips[0].text, "Live")
	}

	function test_social_provider_cards_have_distinct_compact_and_expanded_hierarchy() {
		detailsLoader.width = 680
		let item = setFixture({
			"previewKind": "x", "xDisplayName": "Mumble Design", "xHandle": "@mumbledesign",
			"xVerified": true, "xCreatedAt": "18:30", "xReplyCount": 757,
			"xRepostCount": 12000, "xQuoteCount": 420, "xLikeCount": 362000,
			"xViewCount": 8100000, "xBookmarkCount": 9000
		}, "x", false, "", "", "", "Frame pacing is a product feature.")
		let card = findChild(item, "providerXPost")
		verify(card.visible)
		compare(findChild(item, "providerXDisplayName").text, "Mumble Design")
		verify(findChild(item, "providerXByline").text.indexOf("Mumble Design") < 0)
		verify(findChild(item, "providerXMetric_2") !== null)
		compare(findChild(item, "providerXMetric_3"), null)
		item.expanded = true
		tryVerify(function() { return findChild(item, "providerXMetric_5") !== null })

		item = setFixture({
			"provider": "instagram", "instagramDisplayName": "Mumble Quick",
			"instagramHandle": "@mumblequick", "instagramCreatedAt": "18:30",
			"instagramCaption": "A native caption with stable delegate reuse.",
			"instagramMediaKind": "reel", "instagramLikeCount": 18420,
			"instagramCommentCount": 318
		}, "instagram", false)
		card = findChild(item, "providerInstagramPost")
		verify(card.visible)
		compare(findChild(item, "providerInstagramDisplayName").text, "Mumble Quick")
		verify(findChild(item, "providerInstagramByline").text.indexOf("Mumble Quick") < 0)
		compare(findChild(item, "providerInstagramCaption").textFormat, Text.PlainText)
		compare(findChild(item, "providerInstagramExpandedMeta").visible, false)
		item.expanded = true
		tryCompare(findChild(item, "providerInstagramExpandedMeta"), "visible", true)

		item = setFixture({
			"provider": "github", "githubFullName": "dankmaster/mumble",
			"githubOwnerLogin": "dankmaster", "githubDescription": "Native voice client.",
			"githubLanguage": "C++", "githubLicense": "BSD-3-Clause",
			"githubStars": 4200, "githubForks": 318, "githubOpenIssues": 27,
			"githubDefaultBranch": "master", "githubPushedAt": "18:00",
			"githubTopics": ["qml", "voice"], "githubLatestReleaseTag": "v2.0"
		}, "github", false)
		card = findChild(item, "providerGitHubRepository")
		verify(card.visible)
		compare(findChild(item, "providerGitHubRepoName").text, "dankmaster/mumble")
		verify(item.identitySubtitle.indexOf("dankmaster") < 0)
		const githubSubtitle = findChild(item, "providerGitHubIdentitySubtitle")
		compare(githubSubtitle.visible, true)
		compare(githubSubtitle.text, "C++ · BSD-3-Clause")
		compare(githubSubtitle.textFormat, Text.PlainText)
		compare(githubSubtitle.Accessible.ignored, true)
		compare(item.githubMetrics.length, 3)
		verify(item.githubMetrics.every(function(entry) {
			return entry.value !== "C++" && entry.value !== "BSD-3-Clause"
		}))
		compare(occurrenceCount(card.Accessible.description, "C++"), 1)
		compare(occurrenceCount(card.Accessible.description, "BSD-3-Clause"), 1)
		compare(findChild(item, "providerGitHubTopics").visible, false)
		compare(findChild(item, "providerGitHubRelease").visible, false)
		item.expanded = true
		tryCompare(findChild(item, "providerGitHubTopics"), "visible", true)
		tryCompare(findChild(item, "providerGitHubRelease"), "visible", true)

		item = setFixture({
			"provider": "twitch", "twitchDisplayName": "Mumble Dev",
			"twitchChannel": "mumbledev", "twitchLiveState": "live", "twitchBadge": "Live",
			"twitchGame": "Software Development", "twitchViewerCount": 1842,
			"twitchEmbedMode": "live-player", "twitchPlaybackNote": "Starts after interaction."
		}, "twitch", false)
		card = findChild(item, "providerTwitchStream")
		verify(card.visible)
		compare(findChild(item, "providerTwitchDisplayName").text, "Mumble Dev")
		verify(findChild(item, "providerTwitchByline").text.indexOf("Mumble Dev") < 0)
		compare(findChild(item, "providerTwitchStream").Accessible.name,
			"Twitch stream: Mumble Dev")
		compare(findChild(item, "providerTwitchPlayback").visible, false)
		compare(findChild(item, "providerTwitchNote").visible, false)
		item.expanded = true
		tryCompare(findChild(item, "providerTwitchPlayback"), "visible", true)
		tryCompare(findChild(item, "providerTwitchNote"), "visible", true)
	}

	function test_github_release_summary_actions_and_flashback_context() {
		let item = setFixture({
			"provider": "github", "previewKind": "github", "githubFullName": "dankmaster/mumble",
			"githubLatestReleaseTag": "v2.0", "githubLatestReleaseName": "Native Quick",
			"githubLatestReleasePublishedAt": "2026-07-13", "githubLatestReleaseNotes": "A bounded release summary.",
			"githubLatestReleaseUrl": "https://github.com/dankmaster/mumble/releases/tag/v2.0",
			"githubLatestReleaseAssetName": "mumble-x64.msi",
			"githubLatestReleaseAssetUrl": "https://github.com/dankmaster/mumble/releases/download/v2.0/mumble-x64.msi",
			"githubLatestReleaseAssetCount": 2, "githubLatestReleaseDownloadCount": 18420
		}, "link", false)
		compare(item.variant, "github")
		verify(item.releaseInfo.hasSummary)
		verify(item.releaseCanExpand)
		verify(item.canExpand)
		verify(findChild(item, "providerGitHubRelease") !== null)
		compare(findChild(item, "providerReleaseNotes").visible, false)
		item.expanded = true
		tryCompare(findChild(item, "providerReleaseNotes"), "visible", true)
		verify(findChild(item, "providerReleaseOpenButton").visible)
		verify(findChild(item, "providerReleaseAssetButton").visible)

		item = setFixture({
			"provider": "flashback", "previewKind": "forum", "forumThreadTitle": "Renderloopar",
			"forumCategory": "Dator", "forumName": "Programmering", "forumPage": "42",
			"forumPostNumber": "#628", "forumPostAuthor": "rendernisse",
			"forumPostTime": "2026-07-13 12:00", "forumPostExcerpt": "The linked post body.",
			"forumQuoteAuthor": "qmlvän", "forumQuotePostNumber": "#627",
			"forumQuoteExcerpt": "The quoted context."
		}, "link", true)
		compare(item.variant, "forum")
		compare(item.contextPosts.length, 2)
		compare(item.contextPosts[0].label, "Linked post")
		compare(item.contextPosts[1].label, "Quoted post")
		compare(item.flashbackPresentation, true)
		verify(findChild(item, "providerFlashbackThread").visible)
		compare(findChild(item, "providerFlashbackLogo").text, "FLASHBACK")
		compare(findChild(item, "providerFlashbackTitle").text, "Renderloopar")
		compare(findChild(item, "providerFlashbackAuthor").text, "rendernisse")
		compare(findChild(item, "providerFlashbackQuoteText").text, "The quoted context.")
		compare(findChild(item, "providerFlashbackReply").text, "The linked post body.")
		compare(findChild(item, "providerDetailsContext").visible, false)
	}

	function test_flashback_avatar_accepts_only_managed_image_pipeline_sources() {
		const managed = "image://mumble/flashback-avatar?g=17"
		let item = setFixture({
			"provider": "flashback", "previewKind": "forum",
			"forumThreadTitle": "Managed avatar", "forumPostAuthor": "rendernisse",
			"forumPostAuthorAvatarUrl": managed
		}, "forum", true, "flashback")
		const avatar = findChild(item, "providerFlashbackAuthorAvatar")
		verify(avatar !== null)
		compare(item.flashbackAuthorAvatarSource, managed)
		compare(avatar.source.toString(), managed)
		compare(avatar.asynchronous, true)
		compare(avatar.cache, false)

		const rejected = [
			"https://cdn.example.test/avatar.png",
			"http://cdn.example.test/avatar.png",
			"file:///C:/private/avatar.png",
			"data:image/png;base64,AAAA",
			"qrc:/avatar.png",
			"image://other/avatar",
			"image://mumble.example/avatar"
		]
		for (let index = 0; index < rejected.length; ++index) {
			compare(item.safeManagedImageSource(rejected[index]), "")
			item.metadata = {
				"provider": "flashback", "previewKind": "forum",
				"forumThreadTitle": "Rejected avatar", "forumPostAuthor": "rendernisse",
				"forumPostAuthorAvatarUrl": rejected[index]
			}
			wait(0)
			compare(item.flashbackAuthorAvatarSource, "")
			compare(avatar.source.toString(), "")
		}
	}

	function test_social_provider_avatars_use_managed_sources_and_initial_fallbacks() {
		const xManaged = "image://mumble/x-avatar?g=21"
		let item = setFixture({
			"provider": "x", "previewKind": "x", "xDisplayName": "Mumble Design",
			"xHandle": "@mumbledesign", "xAvatarUrl": xManaged
		}, "x", false)
		let avatar = findChild(item, "providerXAvatar")
		verify(avatar !== null)
		compare(item.xAvatarSource, xManaged)
		compare(avatar.source.toString(), xManaged)
		compare(avatar.asynchronous, true)
		compare(avatar.cache, false)
		compare(findChild(item, "providerXAvatarFallback").text, "MD")
		item.metadata = {
			"provider": "x", "previewKind": "x", "xDisplayName": "Mumble Design",
			"xHandle": "@mumbledesign", "xAvatarUrl": "https://cdn.example.test/x.png"
		}
		wait(0)
		compare(item.xAvatarSource, "")
		compare(avatar.source.toString(), "")

		const instagramManaged = "image://mumble/instagram-avatar?g=22"
		item = setFixture({
			"provider": "instagram", "previewKind": "instagram",
			"instagramDisplayName": "Mumble Quick", "instagramHandle": "@mumblequick",
			"instagramAvatarUrl": instagramManaged
		}, "instagram", false)
		avatar = findChild(item, "providerInstagramAvatar")
		verify(avatar !== null)
		compare(item.instagramAvatarSource, instagramManaged)
		compare(avatar.source.toString(), instagramManaged)
		compare(avatar.asynchronous, true)
		compare(avatar.cache, false)
		compare(findChild(item, "providerInstagramMarkLabel").text, "MQ")
		item.metadata = {
			"provider": "instagram", "previewKind": "instagram",
			"instagramDisplayName": "Mumble Quick", "instagramHandle": "@mumblequick",
			"instagramAvatarUrl": "https://cdn.example.test/instagram.png"
		}
		wait(0)
		compare(item.instagramAvatarSource, "")
		compare(avatar.source.toString(), "")

		const githubManaged = "image://mumble/github-owner?g=23"
		item = setFixture({
			"provider": "github", "previewKind": "github",
			"githubFullName": "dankmaster/mumble", "githubOwnerLogin": "dankmaster",
			"githubOwnerAvatarUrl": githubManaged
		}, "github", false)
		avatar = findChild(item, "providerGitHubOwnerAvatar")
		verify(avatar !== null)
		compare(item.githubOwnerAvatarSource, githubManaged)
		compare(avatar.source.toString(), githubManaged)
		compare(avatar.asynchronous, true)
		compare(avatar.cache, false)
		compare(findChild(item, "providerGitHubOwnerAvatarFallback").text, "DA")

		item.metadata = {
			"provider": "github", "previewKind": "github",
			"githubFullName": "dankmaster/mumble", "githubOwnerLogin": "dankmaster",
			"githubOwnerAvatarUrl": "https://avatars.githubusercontent.com/u/1"
		}
		wait(0)
		compare(item.githubOwnerAvatarSource, "")
		compare(avatar.source.toString(), "")
	}

	function test_release_actions_require_https_and_loading_states_are_accessible() {
		let item = setFixture({
			"provider": "github", "previewKind": "github", "githubRepo": "mumble",
			"githubLatestReleaseLoading": true
		}, "link", true)
		let releaseCard = findChild(item, "providerGitHubRelease")
		verify(releaseCard.visible)
		compare(releaseCard.Accessible.description, "Checking for the latest release")
		const releaseIndicator = findChild(item, "providerReleaseBusyIndicator")
		verify(releaseIndicator !== null)
		compare(releaseIndicator.running, true)
		compare(releaseIndicator.indicatorColor, Theme.accent)
		compare(releaseIndicator.Accessible.role, Accessible.ProgressBar)
		compare(releaseIndicator.Accessible.name, "Checking for the latest release")

		item = setFixture({
			"provider": "github", "previewKind": "github", "githubRepo": "mumble",
			"githubLatestReleaseMissing": true
		}, "link", true)
		releaseCard = findChild(item, "providerGitHubRelease")
		compare(releaseCard.Accessible.description, "No published release found")

		item = setFixture({
			"provider": "github", "previewKind": "github", "githubRepo": "mumble",
			"githubLatestReleaseTag": "v2.0",
			"githubLatestReleaseUrl": "javascript:alert(1)",
			"githubLatestReleaseAssetUrl": "file:///secret"
		}, "link", true)
		compare(item.releaseInfo.url, "")
		compare(item.releaseInfo.assetUrl, "")
		compare(findChild(item, "providerReleaseOpenButton").visible, false)
		compare(findChild(item, "providerReleaseAssetButton").visible, false)
		compare(item.safeExternalUrl("http://github.example/release"), "")
		compare(item.safeExternalUrl("https://github.example/release"),
			"https://github.example/release")
	}

	function test_existenz_search_and_instagram_identity_and_caption_are_plain_text() {
		let item = setFixture({ "provider": "existenz", "previewKind": "linkDigest" }, "link", false,
			"", "Existenz daily link", "Existenz", "A concise linked description")
		compare(item.variant, "linkDigest")
		compare(item.identityTitle, "Existenz daily link")
		compare(findChild(item, "providerIdentityBody").textFormat, Text.PlainText)

		item = setFixture({ "provider": "google-search", "previewKind": "search",
			"googleSearchModeLabel": "Google images", "googleSearchQuery": "Qt Quick render loop" },
			"link", false, "", "Google images", "Google", "Qt Quick render loop")
		compare(item.variant, "googleSearch")
		compare(item.bodyText, "Qt Quick render loop")

		item = setFixture({ "provider": "instagram", "previewKind": "instagram",
			"instagramDisplayName": "Mumble Design", "instagramHandle": "@mumbledesign",
			"instagramCaption": "<b>Native identity stays plain text.</b>" }, "link", false)
		compare(item.variant, "instagram")
		compare(item.identityTitle, "Mumble Design")
		compare(findChild(item, "providerInstagramCaption").textFormat, Text.PlainText)
		compare(item.bodyText, "<b>Native identity stays plain text.</b>")
	}

	function test_plain_text_focus_and_accessibility_contract() {
		const item = setFixture({
			"contentWarning": "<b>Do not parse this</b>",
			"productPrice": "<script>10 kr</script>",
			"productAvailability": "<img src=x>",
			"productDelivery": "<a>Tomorrow</a>"
		}, "product", true)
		compare(findChild(item, "providerWarningText").textFormat, Text.PlainText)
		compare(findChild(item, "providerDetailsPrimary").textFormat, Text.PlainText)
		compare(findChild(item, "providerStatLabel_0").textFormat, Text.PlainText)
		compare(findChild(item, "providerStatValue_0").textFormat, Text.PlainText)
		compare(item.activeFocusOnTab, false)
		compare(item.activeFocus, false)
		compare(item.Accessible.role, Accessible.Grouping)
		verify(item.Accessible.description.indexOf("<script>") >= 0)
	}

	function test_provider_foregrounds_are_contrast_safe_on_token_surfaces() {
		detailsLoader.width = 680
		let item = setFixture({
			"previewProvider": "bytbil", "previewKind": "vehicleListing",
			"vehiclePrice": "245 000 kr", "vehicleHighlights": ["CarPlay"]
		}, "vehicleListing", true)
		verify(item.minimumProviderContrast(item.providerForeground, item.providerAccent) >= 4.5)
		compare(String(findChild(item, "providerDetailsPrimary").color),
			String(item.providerForeground))
		compare(String(findChild(item, "providerChipLabel_0").color),
			String(item.providerForeground))

		item = setFixture({
			"provider": "google-search", "previewKind": "search",
			"googleSearchMode": "images", "googleSearchModeLabel": "Images",
			"googleSearchQuery": "Qt Quick"
		}, "search", true, "google-search")
		compare(String(findChild(item, "providerGoogleModeLabel").color),
			String(item.providerForeground))

		item = setFixture({
			"provider": "instagram", "instagramDisplayName": "Mumble Quick",
			"instagramHandle": "@mumblequick"
		}, "instagram", true)
		compare(String(findChild(item, "providerInstagramMarkLabel").color),
			String(item.providerForeground))
		compare(String(findChild(item, "providerInstagramBrand").color),
			String(item.providerForeground))

		item = setFixture({
			"provider": "twitch", "twitchChannel": "mumbledev",
			"twitchLiveState": "live", "twitchBadge": "Live"
		}, "twitch", true)
		verify(item.minimumProviderContrast(item.providerStateForeground,
			item.providerStateColor) >= 4.5)
		compare(String(findChild(item, "providerTwitchStateLabel").color),
			String(item.providerStateForeground))
	}

	function test_generic_details_expose_each_stat_and_chip_once() {
		detailsLoader.width = 680
		const item = setFixture({
			"previewProvider": "bytbil", "previewKind": "vehicleListing",
			"vehiclePrice": "245 000 kr", "vehicleYear": "2024",
			"vehicleHighlights": ["CarPlay"]
		}, "vehicleListing", true)
		compare(item.ownsHeader, false)
		compare(occurrenceCount(item.Accessible.description, "245 000 kr"), 1)
		compare(occurrenceCount(item.Accessible.description, "2024"), 1)
		compare(occurrenceCount(item.Accessible.description, "CarPlay"), 1)
		compare(findChild(item, "providerDetailsPrimary").Accessible.ignored, true)
		compare(findChild(item, "providerStat_0").Accessible.ignored, true)
		compare(findChild(item, "providerStatLabel_0").Accessible.ignored, true)
		compare(findChild(item, "providerStatValue_0").Accessible.ignored, true)
		compare(findChild(item, "providerChip_0").Accessible.ignored, true)
		compare(findChild(item, "providerChipLabel_0").Accessible.ignored, true)
	}

	function test_flashback_uses_theme_surfaces_with_gold_brand_accent() {
		detailsLoader.width = 680
		const item = setFixture({
			"provider": "flashback", "previewKind": "forum",
			"forumThreadTitle": "Renderloopar", "forumPostAuthor": "rendernisse",
			"forumPostExcerpt": "The linked post body.",
			"forumQuoteAuthor": "qmlvän", "forumQuoteExcerpt": "Quoted context."
		}, "forum", true, "flashback")
		const card = findChild(item, "providerFlashbackThread")
		compare(String(card.color), String(Theme.embedSurface))
		compare(String(card.border.color), String(Theme.embedBorder))
		compare(String(findChild(item, "providerFlashbackMasthead").color),
			String(Theme.embedRevealSurface))
		compare(String(findChild(item, "providerFlashbackQuote").color),
			String(Theme.embedRevealSurface))
		compare(String(findChild(item, "providerFlashbackFooter").color),
			String(Theme.embedRevealSurface))
		compare(String(findChild(item, "providerFlashbackLogo").color),
			String(Theme.textStrong))
		compare(String(findChild(item, "providerFlashbackReply").color),
			String(Theme.textMain))
		compare(String(findChild(item, "providerFlashbackAuthorAvatarBackground").color),
			String(item.providerAccent))
		compare(String(item.providerAccent).toLowerCase(), "#f2c36e")
	}

	function test_bespoke_variants_report_header_ownership() {
		const fixtures = [
			{ "kind": "gameStoreProduct", "provider": "", "metadata": {
				"previewProvider": "game-store", "previewKind": "gameStoreProduct",
				"gameStoreProvider": "steam", "steamPrice": "29,99 €" } },
			{ "kind": "search", "provider": "google-search", "metadata": {
				"provider": "google-search", "googleSearchQuery": "Qt Quick" } },
			{ "kind": "forum", "provider": "flashback", "metadata": {
				"provider": "flashback", "forumThreadTitle": "Renderloopar" } },
			{ "kind": "x", "provider": "", "metadata": {
				"previewKind": "x", "xHandle": "@mumble" } },
			{ "kind": "instagram", "provider": "instagram", "metadata": {
				"provider": "instagram", "instagramHandle": "@mumble" } },
			{ "kind": "github", "provider": "github", "metadata": {
				"provider": "github", "githubRepo": "mumble" } },
			{ "kind": "twitch", "provider": "twitch", "metadata": {
				"provider": "twitch", "twitchChannel": "mumbledev" } }
		]
		for (let index = 0; index < fixtures.length; ++index) {
			const fixture = fixtures[index]
			const item = setFixture(fixture.metadata, fixture.kind, true, fixture.provider)
			compare(item.ownsHeader, true, fixture.kind + " owns its bespoke header")
		}
	}
}
