import QtQuick
import QtQuick.Controls
import QtTest
import Mumble.Theme 1.0

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

	function setFixture(metadata, kind, expanded, provider, title, subtitle, description) {
		const item = details()
		item.metadata = metadata
		item.previewKind = kind || ""
		item.providerHint = provider || ""
		item.previewTitle = title || ""
		item.previewSubtitle = subtitle || ""
		item.previewDescription = description || ""
		item.expanded = expanded === undefined ? true : expanded
		wait(0)
		return item
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
		verify(item.hasDetails)
		verify(item.Accessible.name.length > 0)
		tryVerify(function() { return item.implicitHeight > 0 })
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
		verify(item.allStats.length <= 8)
		compare(item.visibleStats.length, 8)

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
			"financePrice": "448.37", "tickerSymbol": "MSFT"
		}, "finance", true)
		detailsLoader.width = 680
		tryCompare(item, "width", 680)
		const chip = findChild(item, "providerChip_0")
		const label = findChild(item, "providerChipLabel_0")
		verify(chip !== null && label !== null)
		compare(label.text, "MSFT")
		verify(chip.width >= label.implicitWidth + Theme.space2 * 2,
			"provider chip must reserve both shared token margins")
		verify(chip.width <= detailsLoader.width)
	}

	function test_can_expand_only_when_renderable_content_is_hidden() {
		let item = setFixture({ "productPrice": "10 kr", "productAvailability": "In stock" },
			"product", false)
		verify(item.hasDetails)
		compare(item.allStats.length, 1)
		compare(item.canExpand, false)

		item = setFixture({
			"productPrice": "10 kr", "productAvailability": "In stock", "productDelivery": "Tomorrow",
			"productRating": "4.8", "productBrand": "Example"
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
		compare(item.allChips.length, 1)

		item = setFixture({ "previewKind": "weather", "locationLabel": "Stockholm",
			"statusLabel": "12 °C and clear", "providerName": "SMHI" }, "link", true)
		compare(item.primaryValue, "Stockholm")
		compare(item.secondaryValue, "12 °C and clear")
		compare(item.allStats.length, 1)
		compare(item.allStats[0].label, "Provider")
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
		verify(findChild(item, "providerIdentity") !== null)

		item.metadata = {
			"provider": "twitch", "twitchChannel": "mumbledev", "twitchLiveState": "unavailable",
			"twitchBadge": "Unavailable"
		}
		wait(0)
		compare(item.allChips[0].tone, "danger")
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
		verify(findChild(item, "providerContext_1").visible)
	}

	function test_release_actions_require_https_and_loading_states_are_accessible() {
		let item = setFixture({
			"provider": "github", "previewKind": "github", "githubRepo": "mumble",
			"githubLatestReleaseLoading": true
		}, "link", false)
		let releaseCard = findChild(item, "providerGitHubRelease")
		verify(releaseCard.visible)
		compare(releaseCard.Accessible.description, "Checking for the latest release")
		const releaseIndicator = findChild(item, "providerReleaseBusyIndicator")
		verify(releaseIndicator !== null)
		compare(releaseIndicator.running, true)
		compare(releaseIndicator.palette.dark, Theme.accent)

		item = setFixture({
			"provider": "github", "previewKind": "github", "githubRepo": "mumble",
			"githubLatestReleaseMissing": true
		}, "link", false)
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
		compare(findChild(item, "providerIdentityBody").textFormat, Text.PlainText)
		compare(item.bodyText, "<b>Native identity stays plain text.</b>")
	}

	function test_plain_text_focus_and_accessibility_contract() {
		const item = setFixture({
			"contentWarning": "<b>Do not parse this</b>",
			"productPrice": "<script>10 kr</script>",
			"productAvailability": "<img src=x>"
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
}
