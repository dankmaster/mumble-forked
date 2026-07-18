pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0
import Mumble.ProviderPresentation 1.0

FocusScope {
	id: root

	property var metadata: ({})
	property string previewKind: ""
	property string providerHint: ""
	property string previewTitle: ""
	property string previewSubtitle: ""
	property string previewDescription: ""
	property string previewImageSource: ""
	property bool expanded: false
	readonly property bool compactLayout: width < 440
	readonly property string stableKind: normalizedStableKind()
	readonly property string stableProvider: normalizedStableProvider()
	readonly property string providerToken: normalizedVisualProvider()
	readonly property var providerPresentation: ProviderPresentation.resolve(providerToken)
	readonly property string providerDisplayName: buildProviderDisplayName()
	readonly property color providerAccent: buildProviderAccent()
	readonly property color providerAccentSubtle: withAlpha(providerAccent, 0.14)
	readonly property color providerAccentBorder: withAlpha(providerAccent, 0.46)
	readonly property color providerForeground: contrastSafeForeground(providerAccent)
	readonly property string providerStateLabel: buildProviderStateLabel()
	readonly property color providerStateColor: buildProviderStateColor()
	readonly property color providerStateForeground: contrastSafeForeground(providerStateColor)
	readonly property string family: detectFamily()
	readonly property string variant: detectVariant()
	readonly property bool financeLayout: family === "finance"
	readonly property bool commerceLayout: family === "commerce"
	readonly property bool steamPresentation: variant === "game" && providerToken === "steam"
	readonly property bool googlePresentation: variant === "googleSearch"
	readonly property bool flashbackPresentation: variant === "forum" && providerToken === "flashback"
	readonly property bool xPresentation: variant === "x"
	readonly property bool instagramPresentation: variant === "instagram"
	readonly property bool githubPresentation: variant === "github"
	readonly property bool twitchPresentation: variant === "twitch"
	readonly property bool socialBespokePresentation: xPresentation || instagramPresentation
		|| githubPresentation || twitchPresentation
	readonly property bool genericSocialPostPresentation: family === "social"
		&& ["bluesky", "mastodon", "reddit"].indexOf(providerToken) >= 0
		&& !socialBespokePresentation
	readonly property bool bespokeSemanticOwner: steamPresentation || googlePresentation
		|| flashbackPresentation || socialBespokePresentation
	readonly property bool dedicatedSemanticOwner: bespokeSemanticOwner
		|| genericSocialPostPresentation
	readonly property bool ownsHeader: dedicatedSemanticOwner || identityOwnsGenericHeader()
	readonly property string flashbackAuthorAvatarSource: safeManagedImageSource(firstValue([
		"forumPostAuthorAvatarUrl"
	]))
	readonly property string xAvatarSource: safeManagedImageSource(firstValue(["xAvatarUrl"]))
	readonly property string instagramAvatarSource: safeManagedImageSource(firstValue([
		"instagramAvatarUrl"
	]))
	readonly property string githubOwnerAvatarSource: safeManagedImageSource(firstValue([
		"githubOwnerAvatarUrl"
	]))
	readonly property bool identityPresentation: ["audio", "x", "instagram", "github", "twitch"]
		.indexOf(variant) >= 0
	readonly property string presentation: genericSocialPostPresentation ? "socialPost"
		: financeLayout ? "market"
		: commerceLayout ? "commerce" : identityPresentation ? "identity" : "details"
	readonly property string heading: familyHeading()
	readonly property string primaryValue: buildPrimaryValue()
	readonly property string secondaryValue: buildSecondaryValue()
	readonly property string identityTitle: buildIdentityTitle()
	readonly property string identitySubtitle: buildIdentitySubtitle()
	readonly property string socialPostAuthor: buildSocialPostAuthor()
	readonly property string socialPostText: buildSocialPostText()
	readonly property string socialPostDescription: buildSocialPostDescription()
	readonly property string bodyText: genericSocialPostPresentation ? socialPostText : buildBodyText()
	readonly property string providerMark: buildProviderMark()
	readonly property string summaryTitle: buildSummaryTitle()
	readonly property string summarySubtitle: buildSummarySubtitle()
	readonly property string commerceStatus: buildCommerceStatus()
	readonly property string financeRangeSummary: joinedValue([
		"financeRangeChange", "financeRangeChangePercent"
	])
	readonly property string providerErrorText: variant === "twitch"
		? safeText(firstValue(["twitchStateFailure", "twitchMetadataFailure"]), 1024) : ""
	readonly property string trend: safeText(firstValue([
		"financeDayTrend", "financeRangeTrend"
	]), 16).toLowerCase()
	readonly property color trendColor: trend === "up" ? Theme.success
		: trend === "down" ? Theme.danger : Theme.textMuted
	readonly property string warningText: safeText(firstValue([
		"contentWarning", "vehicleWarning"
	]), 512)
	readonly property var rawStats: buildStats()
	readonly property var rawChips: buildChips()
	readonly property var allStats: deduplicatedStats(rawStats, rawChips)
	readonly property var githubMetrics: buildGithubMetrics()
	readonly property int maximumExpandedStatCount: 24
	readonly property var visibleStats: allStats.slice(0,
		expanded ? maximumExpandedStatCount : (compactLayout ? 2 : 3))
	readonly property var allChips: deduplicatedChips(rawChips, allStats)
	readonly property var visibleChips: allChips.slice(0, expanded ? 8 : 4)
	readonly property var contextPosts: buildContextPosts()
	readonly property var sparklinePoints: buildSparklinePoints()
	readonly property var releaseInfo: buildReleaseInfo()
	readonly property var githubTopics: buildGithubTopics()
	readonly property int collapsedStatCount: compactLayout ? 2 : 3
	readonly property int collapsedChipCount: 4
	readonly property int collapsedBodyCharacterBudget: compactLayout ? 96
		: commerceLayout ? 160 : 220
	readonly property bool bodyTextCanExpand: bodyText.length > collapsedBodyCharacterBudget
	readonly property bool socialPostCanExpand: genericSocialPostPresentation
		&& socialPostText.length + socialPostDescription.length > collapsedBodyCharacterBudget
	readonly property bool releaseCanExpand: releaseInfo.notes.length > 0
		|| releaseInfo.url.length > 0 || releaseInfo.assetUrl.length > 0
		|| releaseInfo.assetName.length > 0 || releaseInfo.assetCount.length > 0
		|| releaseInfo.downloadCount.length > 0
	readonly property bool hasDetails: warningText.length > 0 || primaryValue.length > 0
		|| allStats.length > 0 || allChips.length > 0 || contextPosts.length > 0
		|| sparklinePoints.length > 1 || identityTitle.length > 0
		|| identitySubtitle.length > 0 || bodyText.length > 0 || releaseInfo.hasSummary
		|| genericSocialPostPresentation
	readonly property bool canExpand: allStats.length > collapsedStatCount
		|| allChips.length > collapsedChipCount || contextPosts.length > 0
		|| releaseCanExpand || socialCanExpand || bodyTextCanExpand || socialPostCanExpand
	readonly property bool socialCanExpand: xPresentation
		? allStats.length > 3 || contextPosts.length > 0
		: instagramPresentation ? allStats.length > 2 || hasValue("instagramMediaKind")
		: githubPresentation ? githubExpandedMeta().length > 0 || githubTopics.length > 0
			|| releaseInfo.hasSummary || (metadata
				&& (metadata.githubArchived === true || metadata.githubFork === true))
		: twitchPresentation ? allStats.length > 2 || bodyText.length > 0 : false
	readonly property bool ownsDescription: genericSocialPostPresentation || bodyText.length > 0
		|| structuredDescriptionCoversPreview()
	readonly property string steamReviewSummary: safeText(firstValue([
		"steamReviewSummary", "gameStoreRating"
	]), 128)
	readonly property string steamReviewDetail: buildSteamReviewDetail()
	readonly property string steamDiscountLabel: normalizedDiscount(firstValue([
		"steamDiscountPercent", "gameStoreDiscount"
	]))
	readonly property string googleMode: normalizedGoogleMode()
	readonly property string googleModeLabel: safeText(firstValue([
		"googleSearchModeLabel"
	]), 128) || googleMode
	readonly property string googleQuery: safeText(firstValue([
		"googleSearchQuery"
	]), 1024) || safeText(previewDescription, 1024) || qsTr("Search")
	readonly property var googleTabs: [qsTr("All"), qsTr("Images"), qsTr("News"),
		qsTr("Videos"), qsTr("Shopping")]

	signal externalOpenRequested(string url)

	objectName: "providerDetails"
	implicitHeight: hasDetails ? detailsColumn.implicitHeight : 0
	visible: hasDetails
	activeFocusOnTab: false
	Accessible.role: Accessible.Grouping
	Accessible.name: heading.length > 0 ? heading : qsTr("Provider details")
	Accessible.description: accessibleSummary()
	// Sparse link previews do not have a visible details surface. Keeping their
	// zero-height helper in the platform tree creates an empty duplicate group.
	// Bespoke cards own their own single semantic group for the same reason.
	Accessible.ignored: !hasDetails || dedicatedSemanticOwner

	function hasValue(key) {
		if (!metadata || metadata[key] === undefined || metadata[key] === null)
			return false
		if (typeof metadata[key] === "string")
			return metadata[key].trim().length > 0
		return true
	}

	function hasAny(keys) {
		for (let index = 0; index < keys.length; ++index) {
			if (hasValue(keys[index]))
				return true
		}
		return false
	}

	function firstValue(keys) {
		for (let index = 0; index < keys.length; ++index) {
			if (hasValue(keys[index]))
				return metadata[keys[index]]
		}
		return ""
	}

	function safeText(value, maximum) {
		if (value === undefined || value === null || typeof value === "object")
			return ""
		return String(value).trim().slice(0, maximum || 512)
	}

	function safeManagedImageSource(value) {
		const source = safeText(value, 2048)
		return source.indexOf("image://mumble/") === 0 ? source : ""
	}

	function identityInitials(value, fallback) {
		const words = safeText(value, 256).replace(/^@/, "").split(/\s+/).filter(function(word) {
			return word.length > 0
		})
		if (words.length === 0)
			return safeText(fallback, 2).toUpperCase()
		if (words.length === 1)
			return words[0].slice(0, 2).toUpperCase()
		return (words[0].slice(0, 1) + words[words.length - 1].slice(0, 1)).toUpperCase()
	}

	function withAlpha(color, alpha) {
		return Qt.rgba(color.r, color.g, color.b, alpha)
	}

	function compositeOver(foreground, background) {
		const alpha = Math.max(0, Math.min(1, foreground.a))
		return Qt.rgba(foreground.r * alpha + background.r * (1 - alpha),
			foreground.g * alpha + background.g * (1 - alpha),
			foreground.b * alpha + background.b * (1 - alpha), 1)
	}

	function contrastRatio(foreground, background) {
		const foregroundLuminance = Theme.relativeLuminance(foreground)
		const backgroundLuminance = Theme.relativeLuminance(background)
		const lighter = Math.max(foregroundLuminance, backgroundLuminance)
		const darker = Math.min(foregroundLuminance, backgroundLuminance)
		return (lighter + 0.05) / (darker + 0.05)
	}

	function minimumProviderContrast(foreground, accent) {
		const backgrounds = [Theme.panel, Theme.previewCardBackground,
			Theme.embedRevealSurface, compositeOver(withAlpha(accent, 0.14), Theme.panel)]
		let minimum = Number.POSITIVE_INFINITY
		for (let index = 0; index < backgrounds.length; ++index)
			minimum = Math.min(minimum, contrastRatio(foreground, backgrounds[index]))
		return minimum
	}

	function contrastSafeForeground(accent) {
		if (minimumProviderContrast(accent, accent) >= 4.5)
			return accent
		const dark = Qt.rgba(16 / 255, 21 / 255, 28 / 255, 1)
		const light = Qt.rgba(1, 1, 1, 1)
		const target = minimumProviderContrast(dark, accent)
			>= minimumProviderContrast(light, accent) ? dark : light
		for (let step = 1; step <= 20; ++step) {
			const candidate = Theme.mixColors(accent, target, step / 20)
			if (minimumProviderContrast(candidate, accent) >= 4.5)
				return candidate
		}
		return target
	}

	function chipForeground(tone) {
		if (tone === "accent")
			return providerForeground
		if (tone === "warning")
			return contrastSafeForeground(Theme.warning)
		if (tone === "success")
			return contrastSafeForeground(Theme.success)
		if (tone === "danger")
			return contrastSafeForeground(Theme.danger)
		if (commerceLayout)
			return providerForeground
		return Theme.textMain
	}

	function normalizedToken(value) {
		return safeText(value, 96).toLowerCase().replace(/[^a-z0-9]/g, "")
	}

	function normalizedStableKind() {
		const metadataKind = normalizedToken(firstValue(["previewKind", "swedishPreviewKind"]))
		const propertyKind = normalizedToken(previewKind)
		const known = ["product", "systembolagetproduct", "gamestoreproduct", "marketplacelisting",
			"vehiclelisting", "realestate", "article", "forum", "audio", "linkdigest", "x",
			"instagram", "github", "twitch", "weather", "place", "traffic", "googlesearch", "search"]
		if (known.indexOf(metadataKind) >= 0)
			return metadataKind
		if (known.indexOf(propertyKind) >= 0)
			return propertyKind
		return ""
	}

	function normalizedStableProvider() {
		const candidates = [firstValue(["previewProvider"]), firstValue(["provider"]), providerHint,
			firstValue(["providerName"])]
		for (let index = 0; index < candidates.length; ++index) {
			const token = normalizedToken(candidates[index])
			if (["x", "twitter", "instagram", "github", "twitch", "flashback", "existenz",
				"google", "googlesearch"].indexOf(token) >= 0)
				return token === "twitter" ? "x" : token
		}
		return ""
	}

	function normalizedVisualProvider() {
		const primary = normalizedToken(firstValue(["previewProvider", "provider"]))
		const gameStore = normalizedToken(firstValue(["gameStoreProvider", "gameStoreName"]))
		if (primary === "gamestore" && gameStore.length > 0)
			return gameStore
		const candidates = [primary, normalizedToken(firstValue(["marketplaceProvider"])),
			gameStore, normalizedToken(firstValue(["productProvider", "vehicleProvider"])),
			normalizedToken(firstValue(["articlePublisher", "audioProvider"])),
			normalizedToken(firstValue(["providerName"])), normalizedToken(providerHint)]
		for (let index = 0; index < candidates.length; ++index) {
			const token = candidates[index]
			if (token.length === 0)
				continue
			if (token === "twitter")
				return "x"
			if (token === "googlesearch" || token === "googleimages" || token === "googlevideos")
				return "google"
			return token
		}
		return ""
	}

	function buildProviderDisplayName() {
		const explicitName = safeText(firstValue(["gameStoreName", "providerName", "productProvider",
			"vehicleProvider", "articlePublisher", "audioProvider", "marketplaceProvider"]), 128)
		return providerPresentation.label
			|| (normalizedToken(explicitName) !== "gamestore" ? explicitName : "")
			|| safeText(providerHint, 128)
	}

	function buildProviderAccent() {
		return providerPresentation.accent || Theme.accent
	}

	function buildProviderStateLabel() {
		if (providerToken !== "twitch" && variant !== "twitch")
			return ""
		const state = safeText(firstValue(["twitchLiveState"]), 32).toLowerCase()
		const mode = safeText(firstValue(["twitchEmbedMode"]), 32).toLowerCase()
		if (state === "rerun" || mode === "rerun")
			return qsTr("Rerun")
		if (state === "offline" && mode === "latest-vod")
			return qsTr("Offline · Latest VOD")
		if (state === "offline" && mode === "clip")
			return qsTr("Offline · Clip")
		if (state === "offline")
			return qsTr("Offline")
		if (state === "unavailable")
			return qsTr("Unavailable")
		const value = safeText(firstValue(["twitchBadge", "twitchLiveState"]), 32)
		return value.length > 0 ? value.charAt(0).toUpperCase() + value.slice(1) : ""
	}

	function buildProviderStateColor() {
		const state = safeText(firstValue(["twitchLiveState"]), 32).toLowerCase()
		if (state === "live")
			return Theme.success
		if (state === "rerun")
			return Theme.warning
		if (state === "unavailable")
			return Theme.danger
		return Theme.textMuted
	}

	function canonicalFamilyForPresentation() {
		const presentationFamily = providerPresentation && providerPresentation.known
			? String(providerPresentation.family || "") : ""
		switch (presentationFamily) {
		case "finance": return "finance"
		case "marketplace":
		case "vehicle":
		case "property":
		case "product":
		case "game": return "commerce"
		case "article":
		case "forum":
		case "audio":
		case "links": return "editorial"
		case "social": return "social"
		case "search": return "search"
		case "weather":
		case "place":
		case "traffic": return "geo"
		default: return ""
		}
	}

	function canonicalVariantForPresentation() {
		const presentationFamily = providerPresentation && providerPresentation.known
			? String(providerPresentation.family || "") : ""
		switch (presentationFamily) {
		case "finance": return "finance"
		case "marketplace": return "marketplace"
		case "vehicle": return "vehicle"
		case "property": return "realEstate"
		case "product": return "product"
		case "game": return "game"
		case "article": return "article"
		case "forum": return "forum"
		case "audio": return "audio"
		case "links": return "linkDigest"
		case "search": return "googleSearch"
		case "weather": return "weather"
		case "place": return "place"
		case "traffic": return "traffic"
		default: return ""
		}
	}

	function detectFamily() {
		if (["product", "systembolagetproduct", "gamestoreproduct", "marketplacelisting",
				"vehiclelisting", "realestate"].indexOf(stableKind) >= 0)
			return "commerce"
		if (["article", "forum", "audio", "linkdigest"].indexOf(stableKind) >= 0)
			return "editorial"
		if (["x", "instagram", "github", "twitch"].indexOf(stableKind) >= 0)
			return "social"
		if (["weather", "place", "traffic"].indexOf(stableKind) >= 0)
			return "geo"
		if (stableKind === "googlesearch" || stableKind === "search")
			return "search"
		if (["x", "instagram", "github", "twitch"].indexOf(stableProvider) >= 0)
			return "social"
		if (stableProvider === "flashback")
			return "editorial"
		if (stableProvider === "existenz")
			return "editorial"
		if (stableProvider === "google" || stableProvider === "googlesearch")
			return "search"
		if (hasAny(["financePrice", "financeSparkline", "tickerSymbol"]))
			return "finance"
		if (hasAny(["xDisplayName", "xHandle", "xLikeCount", "instagramHandle",
				"instagramLikeCount", "githubRepo", "githubStars"]))
			return "social"
		if (hasAny(["productPrice", "productSpecs", "gameStorePrice", "steamPrice",
				"listingPrice", "listingSpecs", "vehiclePrice", "vehicleSpecs",
				"realEstatePrice", "realEstateArea"]))
			return "commerce"
		if (hasAny(["articleSection", "articleAuthor", "forumProvider", "forumThreadId",
				"forumPostAuthor", "audioProvider", "audioProgram"]))
			return "editorial"
		if (hasAny(["twitchLiveState", "twitchBadge", "twitchChannel"]))
			return "social"
		if (hasAny(["googleSearchQuery", "googleSearchMode"]))
			return "search"
		if (hasAny(["locationLabel", "statusLabel"]))
			return "geo"
		const presentationFamily = canonicalFamilyForPresentation()
		if (presentationFamily.length > 0)
			return presentationFamily
		return warningText.length > 0 ? "warning" : ""
	}

	function detectVariant() {
		switch (stableKind) {
		case "product":
		case "systembolagetproduct": return "product"
		case "gamestoreproduct": return "game"
		case "marketplacelisting": return "marketplace"
		case "vehiclelisting": return "vehicle"
		case "realestate": return "realEstate"
		case "article": return "article"
		case "forum": return "forum"
		case "audio": return "audio"
		case "linkdigest": return "linkDigest"
		case "x": return "x"
		case "instagram": return "instagram"
		case "github": return "github"
		case "twitch": return "twitch"
		case "weather": return "weather"
		case "place": return "place"
		case "traffic": return "traffic"
		case "googlesearch":
		case "search": return "googleSearch"
		}
		if (stableProvider.length > 0) {
			if (stableProvider === "flashback") return "forum"
			if (stableProvider === "existenz") return "linkDigest"
			if (stableProvider === "google" || stableProvider === "googlesearch") return "googleSearch"
			return stableProvider
		}
		const presentationVariant = canonicalVariantForPresentation()
		if (family === "commerce") {
			if (hasAny(["vehiclePrice", "vehicleSpecs", "vehicleKind"]))
				return "vehicle"
			if (hasAny(["realEstatePrice", "realEstateArea", "realEstateRooms"])
					|| hasValue("realEstateFee"))
				return "realEstate"
			if (hasAny(["gameStorePrice", "steamPrice", "steamAppName"]))
				return "game"
			if (hasAny(["listingPrice", "listingSpecs", "listingCondition"]))
				return "marketplace"
			return presentationVariant.length > 0 ? presentationVariant : "product"
		}
		if (family === "editorial") {
			if (hasAny(["forumProvider", "forumThreadId", "forumPostAuthor"]))
				return "forum"
			if (hasAny(["audioProvider", "audioProgram"]))
				return "audio"
			return presentationVariant.length > 0 ? presentationVariant : "article"
		}
		if (family === "social") {
			if (hasAny(["twitchLiveState", "twitchBadge", "twitchChannel"]))
				return "twitch"
			if (hasAny(["githubRepo", "githubStars"]))
				return "github"
			if (hasAny(["instagramHandle", "instagramLikeCount"]))
				return "instagram"
			return presentationVariant.length > 0 ? presentationVariant
				: providerToken.length > 0 ? providerToken : "x"
		}
		if (family === "geo") {
			return presentationVariant.length > 0 ? presentationVariant : "weather"
		}
		if (family === "search")
			return presentationVariant.length > 0 ? presentationVariant : "googleSearch"
		if (family === "finance" && presentationVariant.length > 0)
			return presentationVariant
		return family
	}

	function familyHeading() {
		if (genericSocialPostPresentation)
			return qsTr("Social post")
		switch (variant) {
		case "finance": return qsTr("Market snapshot")
		case "product": return qsTr("Product details")
		case "game": return qsTr("Store details")
		case "marketplace": return qsTr("Listing details")
		case "vehicle": return qsTr("Vehicle details")
		case "realEstate": return qsTr("Property details")
		case "article": return qsTr("Article details")
		case "forum": return qsTr("Discussion details")
		case "audio": return qsTr("Audio details")
		case "x": return qsTr("Post activity")
		case "instagram": return qsTr("Post activity")
		case "github": return qsTr("Repository details")
		case "twitch": return qsTr("Stream details")
		case "weather": return qsTr("Weather")
		case "place": return qsTr("Place")
		case "traffic": return qsTr("Traffic")
		case "linkDigest": return qsTr("Link digest")
		case "googleSearch": return qsTr("Search")
		case "warning": return qsTr("Content notice")
		default: return ""
		}
	}

	function joinedValue(keys, separator) {
		const parts = []
		for (let index = 0; index < keys.length; ++index) {
			const text = safeText(firstValue([keys[index]]), 256)
			if (text.length > 0 && parts.indexOf(text) < 0)
				parts.push(text)
		}
		return parts.join(separator || " · ")
	}

	function normalizedPresentationText(value) {
		return safeText(value, 2048).toLocaleLowerCase()
			.replace(/[\s·:;,.#\/\\_()\[\]{}|\-\u2013\u2014]+/g, " ").trim()
	}

	function appendPresentationFact(result, value) {
		const text = safeText(value, 2048)
		const normalized = normalizedPresentationText(text)
		if (normalized.length === 0)
			return
		for (let index = 0; index < result.length; ++index) {
			if (normalizedPresentationText(result[index]) === normalized)
				return
		}
		result.push(text)
	}

	function joinedPresentationFact(values) {
		const result = []
		for (let index = 0; index < values.length; ++index) {
			const text = safeText(values[index], 1024)
			if (text.length > 0)
				result.push(text)
		}
		return result.join(" · ")
	}

	function presentationDatumCovered(facts, value) {
		const datum = normalizedPresentationText(value)
		if (datum.length === 0)
			return true
		for (let index = 0; index < facts.length; ++index) {
			const existing = normalizedPresentationText(facts[index])
			if (existing === datum)
				return true
			if (datum.length >= 5 && (" " + existing + " ").indexOf(" " + datum + " ") >= 0)
				return true
		}
		return false
	}

	function structuredPresentationFacts() {
		const result = []
		const fields = [heading, summaryTitle, summarySubtitle, primaryValue, secondaryValue,
			commerceStatus, identityTitle, identitySubtitle, bodyText]
		for (let index = 0; index < fields.length; ++index)
			appendPresentationFact(result, fields[index])
		appendPresentationFact(result, joinedPresentationFact([summaryTitle, summarySubtitle]))
		appendPresentationFact(result, joinedPresentationFact([primaryValue, secondaryValue]))
		appendPresentationFact(result, joinedPresentationFact([
			summaryTitle, summarySubtitle, primaryValue, secondaryValue
		]))
		appendPresentationFact(result, joinedPresentationFact([identityTitle, identitySubtitle]))
		appendPresentationFact(result, joinedPresentationFact([commerceStatus, bodyText]))
		if (financeLayout && sparklinePoints.length > 1) {
			const rangeLabel = safeText(firstValue(["financeRangeLabel"]), 32)
			appendPresentationFact(result, rangeLabel)
			appendPresentationFact(result, financeRangeSummary)
			appendPresentationFact(result, joinedPresentationFact([rangeLabel, financeRangeSummary]))
			appendPresentationFact(result, joinedValue([
				"financeRangeLabel", "financeRangeChangePercent"
			]))
		}
		return result
	}

	function presentationLevelFacts() {
		const result = structuredPresentationFacts()
		appendPresentationFact(result, providerDisplayName)
		if (!ownsHeader) {
			appendPresentationFact(result, previewTitle)
			appendPresentationFact(result, previewSubtitle)
			appendPresentationFact(result, joinedPresentationFact([
				providerDisplayName, previewTitle, previewSubtitle
			]))
		}
		return result
	}

	function identityOwnsGenericHeader() {
		if (variant !== "audio" && variant !== "linkDigest")
			return false
		if (identityTitle.length === 0 || safeText(previewImageSource, 2048).length > 0)
			return false
		const facts = [heading, identityTitle, identitySubtitle,
			joinedPresentationFact([identitySubtitle, heading])]
		if (!presentationDatumCovered(facts, previewTitle))
			return false
		if (safeText(previewSubtitle, 512).length > 0
				&& !presentationDatumCovered(facts, previewSubtitle))
			return false
		const provider = safeText(providerDisplayName, 256)
		return provider.length === 0 || presentationDatumCovered(facts, provider)
	}

	function structuredDescriptionCoversPreview() {
		if (!financeLayout && family !== "geo")
			return false
		const description = safeText(previewDescription, 2048)
		return description.length > 0
			&& presentationDatumCovered(structuredPresentationFacts(), description)
	}

	function deduplicatedStats(source, preferredChips) {
		const result = []
		const facts = presentationLevelFacts()
		if (variant === "marketplace") {
			for (let index = 0; index < preferredChips.length; ++index)
				appendPresentationFact(facts, preferredChips[index].text)
		}
		for (let index = 0; index < source.length; ++index) {
			const stat = source[index]
			if (!stat || presentationDatumCovered(facts, stat.value))
				continue
			result.push(stat)
		}
		return result
	}

	function deduplicatedChips(source, stats) {
		const result = []
		const facts = presentationLevelFacts()
		for (let index = 0; index < stats.length; ++index) {
			const stat = stats[index]
			appendPresentationFact(facts, stat.value)
			appendPresentationFact(facts, joinedPresentationFact([stat.label, stat.value]))
		}
		for (let index = 0; index < source.length; ++index) {
			const chip = source[index]
			if (!chip || presentationDatumCovered(facts, chip.text))
				continue
			result.push(chip)
			appendPresentationFact(facts, chip.text)
		}
		return result
	}

	function safeExternalUrl(value) {
		const url = safeText(value, 2048)
		return /^https:\/\//i.test(url) ? url : ""
	}

	function releaseAccessibleDescription() {
		if (releaseInfo.loading)
			return qsTr("Checking for the latest release")
		if (releaseInfo.missing)
			return qsTr("No published release found")
		const parts = [releaseInfo.name, releaseInfo.tag, releaseInfo.publishedAt]
		if (expanded && releaseInfo.notes.length > 0)
			parts.push(releaseInfo.notes)
		return parts.filter(function(value) { return value.length > 0 }).join(". ")
	}

	function socialPostValueIsProvider(value) {
		const text = safeText(value, 512)
		if (text.length === 0)
			return false
		const identity = ProviderPresentation.resolve(text)
		if (identity.known && identity.token === providerToken)
			return true
		return presentationDatumCovered([providerDisplayName, providerHint], text)
	}

	function buildSocialPostAuthor() {
		if (!genericSocialPostPresentation)
			return ""
		const author = safeText(previewSubtitle, 512)
		return socialPostValueIsProvider(author) ? "" : author
	}

	function buildSocialPostText() {
		return genericSocialPostPresentation ? safeText(previewTitle, 2048) : ""
	}

	function buildSocialPostDescription() {
		if (!genericSocialPostPresentation)
			return ""
		const description = safeText(previewDescription, 2048)
		if (socialPostValueIsProvider(description)
				|| presentationDatumCovered([socialPostText, socialPostAuthor], description))
			return ""
		return description
	}

	function genericSocialAccessibleSummary() {
		const parts = []
		for (const value of [providerDisplayName, socialPostAuthor,
				socialPostText, socialPostDescription]) {
			if (!presentationDatumCovered(parts, value))
				parts.push(value)
		}
		return joinAccessibleSentences(parts)
	}

	function buildIdentityTitle() {
		if (variant === "x")
			return safeText(firstValue(["xDisplayName", "xHandle"]), 256)
		if (variant === "twitch")
			return safeText(firstValue(["twitchDisplayName", "twitchChannel"]), 256)
		if (variant === "instagram") {
			const displayName = safeText(firstValue(["instagramDisplayName"]), 256)
			const handle = safeText(firstValue(["instagramHandle"]), 128)
			return displayName.length > 0 ? displayName : handle
		}
		if (variant === "github")
			return safeText(firstValue(["githubFullName", "githubRepo"]), 256)
		if (variant === "audio")
			return safeText(firstValue(["audioProgram", "audioProvider"]), 256)
		if (variant === "forum")
			return safeText(firstValue(["forumThreadTitle"]), 512)
		if (variant === "linkDigest")
			return safeText(firstValue(["linkDigestTitle"]), 512) || safeText(previewTitle, 512)
		if (variant === "googleSearch")
			return safeText(firstValue(["googleSearchModeLabel"]), 256) || safeText(previewTitle, 256)
		return ""
	}

	function buildIdentitySubtitle() {
		if (variant === "x") {
			const handle = safeText(firstValue(["xHandle"]), 128)
			const createdAt = safeText(firstValue(["xCreatedAt"]), 128)
			return [handle !== identityTitle ? handle : "", createdAt]
				.filter(function(value) { return value.length > 0 }).join(" · ")
		}
		if (variant === "twitch") {
			const channel = safeText(firstValue(["twitchChannel"]), 128)
			const game = safeText(firstValue(["twitchGame"]), 256)
			return [channel !== identityTitle ? channel : "", game]
				.filter(function(value) { return value.length > 0 }).join(" · ")
		}
		if (variant === "instagram") {
			const handle = safeText(firstValue(["instagramHandle"]), 128)
			const createdAt = safeText(firstValue(["instagramCreatedAt"]), 128)
			return [handle !== identityTitle ? handle : "", createdAt]
				.filter(function(value) { return value.length > 0 }).join(" · ")
		}
		if (variant === "github")
			return joinedValue(["githubLanguage", "githubLicense"], " · ")
		if (variant === "audio") {
			const provider = safeText(firstValue(["audioProvider", "providerName"]), 256)
			const published = safeText(firstValue(["articlePublishedAt"]), 128)
			return [provider !== identityTitle ? provider : "", published]
				.filter(function(value) { return value.length > 0 }).join(" · ")
		}
		if (variant === "forum")
			return joinedValue(["forumCategory", "forumName", "forumPage"], " · ")
		if (variant === "linkDigest")
			return safeText(firstValue(["linkDigestSource", "providerName"]), 256)
				|| safeText(previewSubtitle, 256)
		if (variant === "googleSearch")
			return safeText(firstValue(["googleSearchMode"]), 128)
		return ""
	}

	function buildBodyText() {
		if (variant === "x")
			return safeText(previewDescription, 1024)
		if (variant === "twitch")
			return safeText(firstValue(["twitchStateFailure", "twitchMetadataFailure",
				"twitchPlaybackNote", "twitchDisclaimer"]), 1024)
		if (variant === "instagram")
			return safeText(firstValue(["instagramCaption"]), 1024) || safeText(previewDescription, 1024)
		if (variant === "github")
			return safeText(firstValue(["githubDescription"]), 1024)
				|| safeText(previewDescription, 1024)
		if (variant === "audio")
			return safeText(previewDescription, 1024)
		if (commerceLayout)
			return safeText(firstValue(["vehicleDescription", "listingDescription",
				"productDescription", "gameStoreDescription"]), 1024)
		if (variant === "linkDigest")
			return safeText(firstValue(["linkDigestCaption"]), 1024)
				|| safeText(previewDescription, 1024)
		if (variant === "googleSearch")
			return safeText(firstValue(["googleSearchQuery"]), 1024)
				|| safeText(previewDescription, 1024)
		return ""
	}

	function buildProviderMark() {
		if (providerPresentation.mark)
			return providerPresentation.mark
		switch (variant) {
		case "audio": return qsTr("Audio")
		case "x": return "X"
		case "instagram": return "IG"
		case "github": return "GH"
		case "twitch": return safeText(firstValue(["twitchLiveState"]), 5) || "TV"
		case "forum": return qsTr("Forum")
		case "linkDigest": return qsTr("Links")
		case "googleSearch": return qsTr("Search")
		default: return ""
		}
	}

	function normalizedDiscount(value) {
		const text = safeText(value, 64)
		if (text.length === 0)
			return ""
		const number = Number(text.replace(/[^0-9.+-]/g, ""))
		if (isFinite(number) && number !== 0)
			return "-" + Math.abs(number) + "%"
		return text.charAt(0) === "-" ? text : "-" + text
	}

	function percentageLabel(value, suffix) {
		const text = safeText(value, 64)
		if (text.length === 0)
			return ""
		return text.indexOf("%") >= 0 ? text + (suffix || "") : text + "%" + (suffix || "")
	}

	function steamReviewTone() {
		const summary = safeText(firstValue(["steamReviewSummary", "gameStoreRating"]), 128).toLowerCase()
		const percent = Number(firstValue(["steamReviewPercent"]))
		const score = Number(firstValue(["steamReviewScore"]))
		const hasPercent = hasValue("steamReviewPercent")
		const hasScore = hasValue("steamReviewScore")
		if ((hasPercent && isFinite(percent) && percent >= 70) || (hasScore && isFinite(score) && score >= 7)
				|| summary.indexOf("positive") >= 0)
			return "success"
		if ((hasPercent && isFinite(percent) && percent >= 40) || (hasScore && isFinite(score) && score >= 5)
				|| summary.indexOf("mixed") >= 0)
			return "warning"
		if ((hasPercent && isFinite(percent) && percent >= 0) || (hasScore && isFinite(score) && score > 0)
				|| summary.indexOf("negative") >= 0)
			return "danger"
		return "normal"
	}

	function buildSteamReviewDetail() {
		const details = []
		const percent = percentageLabel(firstValue(["steamReviewPercent"]), qsTr(" positive"))
		if (percent.length > 0)
			details.push(percent)
		const total = countLabel(firstValue(["steamReviewTotal", "gameStoreReviewCount"]))
		if (total.length > 0)
			details.push(qsTr("%1 reviews").arg(total))
		return details.join(" · ")
	}

	function normalizedGoogleMode() {
		const token = normalizedToken(firstValue(["googleSearchMode", "googleSearchModeLabel"]))
		if (token === "image" || token === "images")
			return qsTr("Images")
		if (token === "video" || token === "videos")
			return qsTr("Videos")
		if (token === "news")
			return qsTr("News")
		if (token === "shopping" || token === "shop")
			return qsTr("Shopping")
		if (token === "books")
			return qsTr("Books")
		return qsTr("All")
	}

	function buildSummaryTitle() {
		if (financeLayout)
			return safeText(firstValue(["tickerSymbol"]), 32)
		return ""
	}

	function buildSummarySubtitle() {
		if (financeLayout)
			return joinedValue(["financeName", "financeExchange", "financeInstrument"], " · ")
		return ""
	}

	function buildCommerceStatus() {
		if (!commerceLayout)
			return ""
		return joinedValue(["productAvailability", "gameStoreAvailability", "steamPlatforms",
			"listingCondition", "vehicleKind", "realEstateArea", "realEstateRooms"], " · ")
	}

	function buildReleaseInfo() {
		if (variant !== "github" || !metadata)
			return { "hasSummary": false, "loading": false, "missing": false, "prerelease": false,
				"tag": "", "name": "", "publishedAt": "", "notes": "", "url": "",
				"assetName": "", "assetUrl": "", "assetCount": "", "downloadCount": "" }
		const loading = metadata.githubLatestReleaseLoading === true
		const missing = metadata.githubLatestReleaseMissing === true
		const tag = safeText(firstValue(["githubLatestReleaseTag"]), 128)
		const name = safeText(firstValue(["githubLatestReleaseName"]), 256)
		const publishedAt = safeText(firstValue(["githubLatestReleasePublishedAt"]), 128)
		const notes = safeText(firstValue(["githubLatestReleaseNotes"]), 1024)
		const assetName = safeText(firstValue(["githubLatestReleaseAssetName"]), 256)
		const assetCount = countLabel(firstValue(["githubLatestReleaseAssetCount"]))
		const downloadCount = countLabel(firstValue(["githubLatestReleaseDownloadCount"]))
		const releaseUrl = safeExternalUrl(firstValue(["githubLatestReleaseUrl"]))
		const assetUrl = safeExternalUrl(firstValue(["githubLatestReleaseAssetUrl"]))
		return {
			"hasSummary": loading || missing || tag.length > 0 || name.length > 0
				|| publishedAt.length > 0 || notes.length > 0 || releaseUrl.length > 0
				|| assetName.length > 0 || assetUrl.length > 0 || assetCount.length > 0
				|| downloadCount.length > 0,
			"loading": loading,
			"missing": missing,
			"prerelease": metadata.githubLatestReleasePrerelease === true,
			"tag": tag,
			"name": name,
			"publishedAt": publishedAt,
			"notes": notes,
			"url": releaseUrl,
			"assetName": assetName,
			"assetUrl": assetUrl,
			"assetCount": assetCount,
			"downloadCount": downloadCount
		}
	}

	function buildGithubTopics() {
		if (variant !== "github" || !metadata || !Array.isArray(metadata.githubTopics))
			return []
		const result = []
		for (let index = 0; index < metadata.githubTopics.length && result.length < 8; ++index) {
			const topic = safeText(metadata.githubTopics[index], 64)
			if (topic.length > 0 && result.indexOf(topic) < 0)
				result.push(topic)
		}
		return result
	}

	function githubExpandedMeta() {
		const parts = [safeText(firstValue(["githubDefaultBranch"]), 128),
			safeText(firstValue(["githubPushedAt"]), 128)]
		if (metadata && metadata.githubArchived === true)
			parts.push(qsTr("Archived"))
		if (metadata && metadata.githubFork === true)
			parts.push(qsTr("Fork"))
		return parts.filter(function(value) { return value.length > 0 }).join(" · ")
	}

	function buildGithubMetrics() {
		if (variant !== "github")
			return []
		const result = []
		const identityMetadata = [identitySubtitle, githubExpandedMeta()]
		for (let index = 0; index < allStats.length; ++index) {
			if (!accessibleDatumCovered(identityMetadata, allStats[index].value))
				result.push(allStats[index])
		}
		return result
	}

	function buildPrimaryValue() {
		if (family === "finance") {
			const price = safeText(firstValue(["financePrice"]), 128)
			const currency = safeText(firstValue(["financeCurrency"]), 32)
			return price.length > 0 ? (price + (currency.length > 0 ? " " + currency : "")) : ""
		}
		if (family === "commerce")
			return safeText(firstValue(["vehiclePrice", "realEstatePrice", "listingPrice",
				"steamPrice", "gameStorePrice", "productPrice"]), 128)
		if (family === "geo")
			return safeText(firstValue(["locationLabel"]), 256)
		return ""
	}

	function buildSecondaryValue() {
		if (family === "finance") {
			const change = safeText(firstValue(["financeDayChange", "financeRangeChange"]), 64)
			const percent = safeText(firstValue(["financeDayChangePercent", "financeRangeChangePercent"]), 64)
			return [change, percent].filter(function(value) { return value.length > 0 }).join("  ")
		}
		if (family === "commerce")
			return [safeText(firstValue(["productOriginalPrice", "gameStoreOriginalPrice",
				"steamOriginalPrice", "listingOriginalPrice"]), 128),
				normalizedDiscount(firstValue(["productDiscount", "gameStoreDiscount",
					"steamDiscountPercent"]))].filter(function(value) { return value.length > 0 }).join("  ")
		if (family === "geo")
			return safeText(firstValue(["statusLabel"]), 512)
		return ""
	}

	function addStat(result, label, value, tone) {
		const text = safeText(value, 512)
		if (text.length === 0 || result.length >= maximumExpandedStatCount)
			return
		result.push({ "label": label, "value": text, "tone": tone || "normal" })
	}

	function addSpecStats(result, key) {
		const source = metadata && Array.isArray(metadata[key]) ? metadata[key] : []
		for (let index = 0; index < source.length && result.length < maximumExpandedStatCount; ++index) {
			const entry = source[index] || {}
			addStat(result, safeText(entry.label, 128), safeText(entry.value, 256))
		}
	}

	function countLabel(value) {
		if (value === undefined || value === null || value === "")
			return ""
		const number = Number(value)
		if (!isFinite(number))
			return safeText(value, 64)
		if (Math.abs(number) >= 1000000)
			return (number / 1000000).toFixed(number >= 10000000 ? 0 : 1).replace(".0", "") + "M"
		if (Math.abs(number) >= 1000)
			return (number / 1000).toFixed(number >= 10000 ? 0 : 1).replace(".0", "") + "K"
		return String(number)
	}

	function buildStats() {
		const result = []
		if (family === "finance") {
			addStat(result, qsTr("Exchange"), firstValue(["financeExchange"]))
			addStat(result, qsTr("Instrument"), firstValue(["financeInstrument"]))
			addStat(result, qsTr("Range"), joinedValue(["financeRangeLabel", "financeRangeChangePercent"]))
			addStat(result, qsTr("Updated"), firstValue(["financeUpdatedAt"]))
		} else if (variant === "product") {
			addStat(result, qsTr("Availability"), firstValue(["productAvailability"]))
			addStat(result, qsTr("Delivery"), firstValue(["productDelivery"]))
			addStat(result, qsTr("Rating"), joinedValue(["productRating", "productReviewCount"]))
			addStat(result, qsTr("Brand"), firstValue(["productBrand"]))
			addStat(result, qsTr("SKU"), firstValue(["productSku", "productId"]))
			addStat(result, qsTr("Volume"), firstValue(["productVolume"]))
			addStat(result, qsTr("Alcohol"), firstValue(["productAlcohol"]))
			addSpecStats(result, "productSpecs")
		} else if (variant === "game") {
			addStat(result, qsTr("Platform"), firstValue(["gameStorePlatform", "steamPlatforms"]))
			addStat(result, qsTr("Availability"), firstValue(["gameStoreAvailability"]))
			const reviewSummary = safeText(firstValue(["steamReviewSummary", "gameStoreRating"]), 128)
			const reviewPercent = percentageLabel(firstValue(["steamReviewPercent"]), qsTr(" positive"))
			const reviewCount = countLabel(firstValue(["steamReviewTotal", "gameStoreReviewCount"]))
			const reviewValue = [reviewSummary, reviewPercent,
				reviewCount.length > 0 ? qsTr("%1 reviews").arg(reviewCount) : ""]
				.filter(function(value) { return value.length > 0 }).join(" · ")
			addStat(result, qsTr("Reviews"), reviewValue, steamReviewTone())
			addStat(result, qsTr("Developer"), firstValue(["steamDeveloper", "gameStoreBrand"]))
			addStat(result, qsTr("Released"), firstValue(["steamReleaseDate"]))
			addStat(result, qsTr("Recommendations"), countLabel(firstValue(["steamRecommendationsTotal"])))
			const score = Number(firstValue(["steamMetacriticScore"]))
			addStat(result, qsTr("Score"), firstValue(["steamMetacriticScore"]),
				isFinite(score) && score >= 75 ? "success" : isFinite(score) && score >= 50
					? "warning" : isFinite(score) && score > 0 ? "danger" : "normal")
		} else if (variant === "marketplace") {
			addStat(result, qsTr("Condition"), firstValue(["listingCondition"]))
			addStat(result, qsTr("Location"), firstValue(["listingLocation"]))
			addStat(result, qsTr("Sale"), firstValue(["listingSaleType"]))
			addStat(result, qsTr("Ends"), firstValue(["listingEndsAt"]))
			addStat(result, qsTr("Listing"), firstValue(["listingId"]))
			addSpecStats(result, "listingSpecs")
		} else if (variant === "vehicle") {
			addStat(result, qsTr("Year"), firstValue(["vehicleYear"]))
			addStat(result, qsTr("Mileage"), firstValue(["vehicleMileage"]))
			addStat(result, qsTr("Fuel"), firstValue(["vehicleFuel"]))
			addStat(result, qsTr("Transmission"), firstValue(["vehicleTransmission"]))
			addStat(result, qsTr("Dealer"), firstValue(["vehicleDealer"]))
			addStat(result, qsTr("Location"), firstValue(["vehicleLocation", "listingLocation"]))
			addStat(result, qsTr("Excl. VAT"), firstValue(["vehiclePriceExVat"]))
			addSpecStats(result, "vehicleSpecs")
		} else if (variant === "realEstate") {
			addStat(result, qsTr("Area"), firstValue(["realEstateArea"]))
			addStat(result, qsTr("Rooms"), firstValue(["realEstateRooms"]))
			addStat(result, qsTr("Fee"), firstValue(["realEstateFee"]))
			addStat(result, qsTr("Location"), firstValue(["listingLocation", "locationLabel"]))
		} else if (variant === "article") {
			addStat(result, qsTr("Section"), firstValue(["articleSection"]))
			addStat(result, qsTr("Author"), firstValue(["articleAuthor"]))
			addStat(result, qsTr("Published"), firstValue(["articlePublishedAt"]))
			addStat(result, qsTr("Updated"), firstValue(["articleModifiedAt"]))
			addStat(result, qsTr("Access"), firstValue(["articleAccess"]))
			addStat(result, qsTr("Publisher"), firstValue(["articlePublisher"]))
		} else if (variant === "forum") {
			addStat(result, qsTr("Forum"), firstValue(["forumProvider"]))
			addStat(result, qsTr("Category"), joinedValue(["forumCategory", "forumName"]))
			addStat(result, qsTr("Page"), joinedValue(["forumPage", "forumPageCount"], " / "))
			addStat(result, qsTr("Author"), firstValue(["forumPostAuthor", "forumFirstPostAuthor"]))
			addStat(result, qsTr("Posted"), firstValue(["forumPostTime", "forumFirstPostTime"]))
			addStat(result, qsTr("Posts"), firstValue(["forumPostCount"]))
			addStat(result, qsTr("Quoted"), firstValue(["forumQuoteAuthor"]))
			addStat(result, qsTr("Thread"), firstValue(["forumThreadId", "threadId"]))
		} else if (variant === "audio") {
			const identityMetadata = [identityTitle, identitySubtitle]
			const provider = firstValue(["audioProvider"])
			const program = firstValue(["audioProgram"])
			const published = firstValue(["articlePublishedAt"])
			if (!accessibleDatumCovered(identityMetadata, provider))
				addStat(result, qsTr("Provider"), provider)
			if (!accessibleDatumCovered(identityMetadata, program))
				addStat(result, qsTr("Program"), program)
			if (!accessibleDatumCovered(identityMetadata, published))
				addStat(result, qsTr("Published"), published)
		} else if (variant === "x") {
			addStat(result, qsTr("Replies"), countLabel(firstValue(["xReplyCount"])))
			addStat(result, qsTr("Reposts"), countLabel(firstValue(["xRepostCount"])))
			addStat(result, qsTr("Quotes"), countLabel(firstValue(["xQuoteCount"])))
			addStat(result, qsTr("Likes"), countLabel(firstValue(["xLikeCount"])))
			addStat(result, qsTr("Views"), countLabel(firstValue(["xViewCount"])))
			addStat(result, qsTr("Bookmarks"), countLabel(firstValue(["xBookmarkCount"])))
			addStat(result, qsTr("Posted"), firstValue(["xCreatedAt"]))
		} else if (variant === "instagram") {
			addStat(result, qsTr("Likes"), countLabel(firstValue(["instagramLikeCount"])))
			addStat(result, qsTr("Comments"), countLabel(firstValue(["instagramCommentCount"])))
			addStat(result, qsTr("Media"), firstValue(["instagramMediaKind"]))
			addStat(result, qsTr("Posted"), firstValue(["instagramCreatedAt"]))
		} else if (variant === "github") {
			addStat(result, qsTr("Stars"), countLabel(firstValue(["githubStars"])))
			addStat(result, qsTr("Forks"), countLabel(firstValue(["githubForks"])))
			addStat(result, qsTr("Open issues"), countLabel(firstValue(["githubOpenIssues"])))
			addStat(result, qsTr("Language"), firstValue(["githubLanguage"]))
			addStat(result, qsTr("License"), firstValue(["githubLicense"]))
			addStat(result, qsTr("Branch"), firstValue(["githubDefaultBranch"]))
			addStat(result, qsTr("Updated"), firstValue(["githubPushedAt"]))
		} else if (variant === "twitch") {
			addStat(result, qsTr("Channel"), firstValue(["twitchChannel"]))
			addStat(result, qsTr("Game"), firstValue(["twitchGame"]))
			addStat(result, qsTr("Viewers"), countLabel(firstValue(["twitchViewerCount"])))
			addStat(result, qsTr("Playback"), firstValue(["twitchEmbedMode", "twitchKind"]))
		} else if (family === "geo") {
			addStat(result, qsTr("Provider"), firstValue(["providerName"]))
		}
		return result.slice(0, maximumExpandedStatCount)
	}

	function addChip(result, value, tone) {
		const text = safeText(value, 128)
		if (text.length === 0 || result.length >= 8)
			return
		for (let index = 0; index < result.length; ++index) {
			if (result[index].text.toLowerCase() === text.toLowerCase())
				return
		}
		result.push({ "text": text, "tone": tone || "normal" })
	}

	function addChipList(result, key) {
		const source = metadata && Array.isArray(metadata[key]) ? metadata[key] : []
		for (let index = 0; index < source.length && result.length < 8; ++index)
			addChip(result, source[index])
	}

	function buildChips() {
		const result = []
		if (family === "finance") {
			addChip(result, firstValue(["tickerSymbol"]), "accent")
		} else if (variant === "game") {
			addChipList(result, "gameStoreTags")
			addChip(result, firstValue(["steamGenres"]))
			addChip(result, firstValue(["steamPlatforms"]))
		} else if (variant === "vehicle") {
			addChipList(result, "vehicleHighlights")
			addChip(result, firstValue(["vehicleKind"]))
		} else if (variant === "marketplace") {
			addChip(result, firstValue(["listingSaleType"]), "accent")
			const endsAt = safeText(firstValue(["listingEndsAt"]), 96)
			addChip(result, endsAt.length > 0 ? qsTr("Ends %1").arg(endsAt) : "")
			const listingId = safeText(firstValue(["listingId"]), 96)
			addChip(result, listingId.length > 0 ? "#" + listingId.replace(/^#+/, "") : "")
		} else if (variant === "x") {
			addChip(result, firstValue(["xHandle"]), "accent")
			if (metadata && metadata.xVerified === true)
				addChip(result, qsTr("Verified"), "success")
		} else if (variant === "github") {
			addChipList(result, "githubTopics")
			if (metadata && metadata.githubPrivate === true)
				addChip(result, qsTr("Private"), "warning")
			if (metadata && metadata.githubArchived === true)
				addChip(result, qsTr("Archived"), "warning")
			if (metadata && metadata.githubFork === true)
				addChip(result, qsTr("Fork"))
		} else if (variant === "twitch") {
			const state = safeText(firstValue(["twitchLiveState"]), 32).toLowerCase()
			const tone = state === "live" ? "success" : state === "rerun" ? "warning"
				: state === "unavailable" ? "danger" : "normal"
			addChip(result, firstValue(["twitchBadge", "twitchLiveState"]), tone)
		} else if (variant === "article" && metadata && metadata.articlePremium === true) {
			addChip(result, qsTr("Premium"), "warning")
		}
		return result.slice(0, 8)
	}

	function normalizedContextPost(value, label) {
		if (!value || typeof value !== "object" || Array.isArray(value))
			return null
		const name = safeText(value.displayName, 256)
		const handle = safeText(value.handle, 128)
		const text = safeText(value.text, 1024)
		if (name.length === 0 && handle.length === 0 && text.length === 0)
			return null
		return { "label": safeText(label, 128), "name": name, "handle": handle,
			"text": text, "verified": value.verified === true }
	}

	function buildContextPosts() {
		if (!metadata)
			return []
		const result = []
		if (variant === "x") {
			const source = Array.isArray(metadata.xReplyContext) ? metadata.xReplyContext : []
			const first = Math.max(0, source.length - 3)
			for (let index = first; index < source.length && result.length < 3; ++index) {
				const post = normalizedContextPost(source[index], qsTr("Conversation"))
				if (post)
					result.push(post)
			}
			const quoted = normalizedContextPost(metadata.xQuotedPost, qsTr("Quoted post"))
			if (quoted && result.length < 3)
				result.push(quoted)
		} else if (variant === "forum") {
			const post = normalizedContextPost({
				"displayName": firstValue(["forumPostAuthor", "forumFirstPostAuthor"]),
				"handle": joinedValue(["forumPostNumber", "forumFirstPostNumber",
					"forumPostTime", "forumFirstPostTime"], " · "),
				"text": firstValue(["forumPostExcerpt", "forumFirstPostExcerpt"])
			}, qsTr("Linked post"))
			if (post)
				result.push(post)
			const quote = normalizedContextPost({
				"displayName": firstValue(["forumQuoteAuthor"]),
				"handle": joinedValue(["forumQuotePostNumber", "forumQuotePostId"], " · "),
				"text": firstValue(["forumQuoteExcerpt"])
			}, qsTr("Quoted post"))
			if (quote && result.length < 3)
				result.push(quote)
		}
		return result.slice(0, 3)
	}

	function buildSparklinePoints() {
		if (!metadata || !Array.isArray(metadata.financeSparkline))
			return []
		const result = []
		for (let index = 0; index < metadata.financeSparkline.length && result.length < 64; ++index) {
			const entry = metadata.financeSparkline[index]
			const value = typeof entry === "number" ? entry : (entry && entry.close)
			const number = Number(value)
			if (isFinite(number))
				result.push(number)
		}
		return result
	}

	function statColumnCount(availableWidth) {
		if (financeLayout || identityPresentation) {
			if (availableWidth < 400)
				return 2
			return availableWidth < 620 ? 3 : 4
		}
		if (compactLayout || availableWidth < 400)
			return 1
		return availableWidth < 620 ? 2 : 3
	}

	function statWidth(availableWidth) {
		const columns = statColumnCount(availableWidth)
		return Math.max(1, (availableWidth - Theme.space2 * (columns - 1)) / columns)
	}

	function accessibleSummary() {
		const parts = []
		if (primaryValue.length > 0)
			parts.push(primaryValue)
		if (secondaryValue.length > 0)
			parts.push(secondaryValue)
		if (identityTitle.length > 0)
			parts.push(identityTitle)
		if (identitySubtitle.length > 0)
			parts.push(identitySubtitle)
		if (bodyText.length > 0)
			parts.push(bodyText)
		for (let index = 0; index < visibleStats.length; ++index) {
			if (!accessibleDatumCovered(parts, visibleStats[index].value))
				parts.push(visibleStats[index].label + ": " + visibleStats[index].value)
		}
		for (let index = 0; index < visibleChips.length; ++index) {
			if (!accessibleDatumCovered(parts, visibleChips[index].text))
				parts.push(visibleChips[index].text)
		}
		return joinAccessibleSentences(parts)
	}

	function accessibleDatumCovered(parts, value) {
		const datum = safeText(value, 512).toLocaleLowerCase()
			.replace(/[\s·:;,.#\/_-]+/g, " ").trim()
		if (datum.length === 0)
			return true
		for (let index = 0; index < parts.length; ++index) {
			const existing = safeText(parts[index], 1024).toLocaleLowerCase()
				.replace(/[\s·:;,.#\/_-]+/g, " ").trim()
			if (existing === datum || existing.indexOf(datum) >= 0)
				return true
		}
		return false
	}

	function joinAccessibleSentences(values) {
		let result = ""
		for (let index = 0; index < values.length; ++index) {
			const text = safeText(values[index], 1024)
			if (text.length === 0)
				continue
			if (result.length > 0)
				result += /[.!?]$/.test(result) ? " " : ". "
			result += text
		}
		return result.slice(0, 1024)
	}

	function flashbackAccessibleSummary() {
		const author = joinedValue(["forumPostAuthor", "forumPostAuthorTitle",
			"forumPostTime", "forumPostNumber"], " · ")
		const quote = [safeText(firstValue(["forumQuotePostNumber"]), 64),
			safeText(firstValue(["forumQuoteAuthor"]), 256),
			safeText(firstValue(["forumQuoteExcerpt"]), 1024)]
			.filter(function(value) { return value.length > 0 }).join(" · ")
		const reply = safeText(firstValue(["forumPostExcerpt", "forumFirstPostExcerpt"]), 1024)
			|| bodyText
		return joinAccessibleSentences([identitySubtitle, author, quote, reply])
	}

	function socialAccessibleSummary() {
		const parts = [identitySubtitle]
		if (variant !== "twitch" || expanded)
			parts.push(bodyText)
		if (variant === "x") {
			const limit = expanded ? 6 : 3
			for (let index = 0; index < allStats.length && index < limit; ++index)
				parts.push(allStats[index].label + ": " + allStats[index].value)
		} else if (variant === "instagram") {
			if (hasValue("instagramLikeCount"))
				parts.push(qsTr("Likes: %1").arg(countLabel(firstValue(["instagramLikeCount"]))))
			if (hasValue("instagramCommentCount"))
				parts.push(qsTr("Comments: %1").arg(countLabel(firstValue(["instagramCommentCount"]))))
			if (expanded && hasValue("instagramMediaKind"))
				parts.push(qsTr("Media: %1").arg(safeText(firstValue(["instagramMediaKind"]), 64)))
		} else if (variant === "github") {
			for (let index = 0; index < githubMetrics.length && index < 3; ++index)
				parts.push(githubMetrics[index].label + ": " + githubMetrics[index].value)
			if (expanded)
				parts.push(githubExpandedMeta())
		} else if (variant === "twitch") {
			if (hasValue("twitchViewerCount"))
				parts.push(qsTr("Viewers: %1").arg(countLabel(firstValue(["twitchViewerCount"]))))
			if (expanded && hasAny(["twitchEmbedMode", "twitchKind"]))
				parts.push(qsTr("Playback: %1").arg(safeText(firstValue([
					"twitchEmbedMode", "twitchKind"
				]), 128)))
		}
		return joinAccessibleSentences(parts)
	}

	ColumnLayout {
		id: detailsColumn
		anchors.left: parent.left
		anchors.right: parent.right
		spacing: Theme.space2

		Rectangle {
			id: warningBanner
			objectName: "providerDetailsWarning"
			Layout.fillWidth: true
			Layout.preferredHeight: root.warningText.length > 0
				? warningLayout.implicitHeight + Theme.space3 * 2 : 0
			visible: root.warningText.length > 0
			radius: Theme.innerRadius
			color: root.withAlpha(Theme.warning, 0.12)
			border.color: root.withAlpha(Theme.warning, 0.55)
			Accessible.role: Accessible.AlertMessage
			Accessible.name: qsTr("Content notice")
			Accessible.description: root.warningText

			RowLayout {
				id: warningLayout
				anchors.fill: parent
				anchors.margins: Theme.space3
				spacing: Theme.space2

				ModernIcon {
					name: "warning"
					size: Theme.avatarSmall
					color: Theme.warning
					Accessible.ignored: true
				}
				Label {
					objectName: "providerWarningText"
					Layout.fillWidth: true
					text: root.warningText
					textFormat: Text.PlainText
					color: Theme.textStrong
					font.pixelSize: Theme.fontLabel
					wrapMode: Text.Wrap
				}
			}
		}

		Rectangle {
			id: steamCard
			objectName: "providerSteamCard"
			Layout.fillWidth: true
			Layout.preferredHeight: visible ? steamLayout.implicitHeight + Theme.space3 * 2 : 0
			visible: root.steamPresentation
			radius: Theme.innerRadius
			color: root.withAlpha(root.providerAccent, 0.08)
			border.color: root.providerAccentBorder
			Accessible.role: Accessible.Grouping
			Accessible.name: root.heading
			Accessible.description: root.accessibleSummary()

			ColumnLayout {
				id: steamLayout
				anchors.fill: parent
				anchors.margins: Theme.space3
				spacing: Theme.space3

				RowLayout {
					Layout.fillWidth: true
					spacing: Theme.space2
					Rectangle {
						Layout.preferredWidth: 34
						Layout.preferredHeight: 34
						radius: Theme.innerRadius
						color: root.providerAccent
						Label {
							anchors.centerIn: parent
							text: "S"
							textFormat: Text.PlainText
							color: Theme.contrastText(parent.color)
							font.pixelSize: Theme.fontTitle
							font.bold: true
							Accessible.ignored: true
						}
					}
					ColumnLayout {
						Layout.fillWidth: true
						Layout.minimumWidth: 0
						spacing: 0
						Label {
							Layout.fillWidth: true
							text: qsTr("STEAM STORE")
							textFormat: Text.PlainText
							color: root.providerForeground
							font.pixelSize: Theme.fontCaption
							font.bold: true
							font.letterSpacing: 0.8
							Accessible.ignored: true
						}
						Label {
							objectName: "providerSteamProductName"
							Layout.fillWidth: true
							text: root.safeText(root.firstValue(["steamAppName"]), 256)
								|| root.previewTitle || qsTr("Steam app")
							textFormat: Text.PlainText
							color: Theme.textStrong
							font.pixelSize: Theme.fontTitle
							font.bold: true
							elide: Text.ElideRight
							Accessible.ignored: true
						}
					}
				}

				Rectangle {
					objectName: "providerSteamHero"
					Layout.fillWidth: true
					Layout.preferredHeight: visible ? (root.compactLayout ? 132 : 176) : 0
					visible: root.safeManagedImageSource(root.previewImageSource).length > 0
					radius: Theme.innerRadius
					color: Theme.embedSurface
					border.color: Theme.embedBorder
					clip: true
					Accessible.ignored: true

					Image {
						objectName: "providerSteamHeroImage"
						anchors.fill: parent
						source: root.safeManagedImageSource(root.previewImageSource)
						asynchronous: true
						cache: false
						sourceSize: Qt.size(Math.min(960, width * Screen.devicePixelRatio),
							Math.min(540, height * Screen.devicePixelRatio))
						fillMode: Image.PreserveAspectCrop
						visible: status === Image.Ready
						Accessible.ignored: true
					}
					Rectangle {
						anchors.fill: parent
						color: root.withAlpha(Theme.mediaCanvas, 0.08)
						Accessible.ignored: true
					}
				}

				Label {
					Layout.fillWidth: true
					visible: root.bodyText.length > 0
					text: root.bodyText
					textFormat: Text.PlainText
					color: Theme.textMain
					font.pixelSize: Theme.fontLabel
					lineHeight: 1.25
					wrapMode: Text.Wrap
					maximumLineCount: root.expanded ? 5 : 2
					elide: Text.ElideRight
					Accessible.ignored: true
				}

				GridLayout {
					objectName: "providerSteamReviews"
					Layout.fillWidth: true
					visible: root.steamReviewSummary.length > 0
						|| root.hasValue("steamMetacriticScore")
					columns: root.compactLayout ? 1 : 2
					columnSpacing: Theme.space2
					rowSpacing: Theme.space2

					Rectangle {
						objectName: "providerSteamUserReviews"
						Layout.fillWidth: true
						Layout.preferredHeight: steamReviewCopy.implicitHeight + Theme.space2 * 2
						visible: root.steamReviewSummary.length > 0
						radius: Theme.innerRadius
						color: Theme.panel
						border.color: root.steamReviewTone() === "success"
							? root.withAlpha(Theme.success, 0.55)
							: root.steamReviewTone() === "danger" ? root.withAlpha(Theme.danger, 0.55)
							: root.withAlpha(Theme.warning, 0.55)
						ColumnLayout {
							id: steamReviewCopy
							anchors.fill: parent
							anchors.margins: Theme.space2
							spacing: 0
							Label {
								text: qsTr("USER REVIEWS")
								textFormat: Text.PlainText
								color: Theme.textMuted
								font.pixelSize: Theme.fontCaption
								font.bold: true
								Accessible.ignored: true
							}
							Label {
								objectName: "providerSteamReviewSummary"
								Layout.fillWidth: true
								text: root.steamReviewSummary
								textFormat: Text.PlainText
								color: root.steamReviewTone() === "success" ? Theme.success
									: root.steamReviewTone() === "danger" ? Theme.danger : Theme.warning
								font.pixelSize: Theme.fontLabel
								font.bold: true
								elide: Text.ElideRight
								Accessible.ignored: true
							}
							Label {
								objectName: "providerSteamReviewDetail"
								Layout.fillWidth: true
								visible: root.steamReviewDetail.length > 0
								text: root.steamReviewDetail
								textFormat: Text.PlainText
								color: Theme.textMuted
								font.pixelSize: Theme.fontCaption
								elide: Text.ElideRight
								Accessible.ignored: true
							}
						}
					}

					Rectangle {
						objectName: "providerSteamMetacritic"
						Layout.fillWidth: true
						Layout.preferredHeight: steamMetacriticCopy.implicitHeight + Theme.space2 * 2
						visible: root.hasValue("steamMetacriticScore")
						radius: Theme.innerRadius
						color: Theme.panel
						border.color: Theme.surfaceBorder
						RowLayout {
							id: steamMetacriticCopy
							anchors.fill: parent
							anchors.margins: Theme.space2
							spacing: Theme.space2
							Rectangle {
								Layout.preferredWidth: 32
								Layout.preferredHeight: 32
								radius: Theme.space1
								color: Number(root.firstValue(["steamMetacriticScore"])) >= 75
									? Theme.success : Theme.warning
								Label {
									objectName: "providerSteamMetacriticScore"
									anchors.centerIn: parent
									text: root.safeText(root.firstValue(["steamMetacriticScore"]), 8)
									textFormat: Text.PlainText
									color: Theme.contrastText(parent.color)
									font.pixelSize: Theme.fontLabel
									font.bold: true
									Accessible.ignored: true
								}
							}
							Label {
								Layout.fillWidth: true
								text: qsTr("Metacritic")
								textFormat: Text.PlainText
								color: Theme.textMain
								font.pixelSize: Theme.fontLabel
								font.bold: true
								Accessible.ignored: true
							}
						}
					}
				}

				Rectangle {
					objectName: "providerSteamPurchase"
					Layout.fillWidth: true
					Layout.preferredHeight: steamPurchaseRow.implicitHeight + Theme.space2 * 2
					visible: root.primaryValue.length > 0 || root.steamDiscountLabel.length > 0
					radius: Theme.innerRadius
					color: Theme.panel
					border.color: root.providerAccentBorder
					RowLayout {
						id: steamPurchaseRow
						anchors.fill: parent
						anchors.margins: Theme.space2
						spacing: Theme.space2
						Rectangle {
							objectName: "providerSteamDiscount"
							Layout.preferredWidth: steamDiscountText.implicitWidth + Theme.space2 * 2
							Layout.preferredHeight: 30
							visible: root.steamDiscountLabel.length > 0
							radius: Theme.space1
							color: Theme.success
							Label {
								id: steamDiscountText
								anchors.centerIn: parent
								text: root.steamDiscountLabel
								textFormat: Text.PlainText
								color: Theme.contrastText(parent.color)
								font.pixelSize: Theme.fontLabel
								font.bold: true
								Accessible.ignored: true
							}
						}
						ColumnLayout {
							Layout.fillWidth: true
							Layout.minimumWidth: 0
							spacing: 0
							Label {
								objectName: "providerSteamOriginalPrice"
								Layout.fillWidth: true
								visible: root.safeText(root.firstValue([
									"steamOriginalPrice", "gameStoreOriginalPrice"
								]), 128).length > 0
								text: root.safeText(root.firstValue([
									"steamOriginalPrice", "gameStoreOriginalPrice"
								]), 128)
								textFormat: Text.PlainText
								color: Theme.textMuted
								font.pixelSize: Theme.fontCaption
								font.strikeout: true
								elide: Text.ElideRight
								Accessible.ignored: true
							}
							Label {
								objectName: "providerSteamFinalPrice"
								Layout.fillWidth: true
								text: root.primaryValue.length > 0 ? root.primaryValue : qsTr("Open on Steam")
								textFormat: Text.PlainText
								color: Theme.textStrong
								font.pixelSize: Theme.fontTitle
								font.bold: true
								elide: Text.ElideRight
								Accessible.ignored: true
							}
						}
						ModernIcon {
							name: "external"
							size: Theme.avatarSmall
							color: root.providerForeground
							Accessible.ignored: true
						}
					}
				}

				Label {
					objectName: "providerSteamProductMeta"
					Layout.fillWidth: true
					visible: text.length > 0
					text: [root.safeText(root.firstValue(["steamDeveloper"]), 256),
						root.safeText(root.firstValue(["steamReleaseDate"]), 128),
						root.safeText(root.firstValue(["steamPlatforms", "steamGenres"]), 256)]
						.filter(function(value) { return value.length > 0 }).join(" · ")
					textFormat: Text.PlainText
					color: Theme.textMuted
					font.pixelSize: Theme.fontCaption
					wrapMode: Text.Wrap
					Accessible.ignored: true
				}
			}
		}

		Rectangle {
			id: googleCard
			objectName: "providerGoogleSearch"
			Layout.fillWidth: true
			Layout.preferredHeight: visible ? googleLayout.implicitHeight + Theme.space3 * 2 : 0
			visible: root.googlePresentation
			radius: Theme.innerRadius
			color: Theme.panel
			border.color: root.providerAccentBorder
			Accessible.role: Accessible.Grouping
			Accessible.name: root.googleModeLabel
			Accessible.description: root.googleQuery

			ColumnLayout {
				id: googleLayout
				anchors.fill: parent
				anchors.margins: Theme.space3
				spacing: Theme.space3

				RowLayout {
					Layout.fillWidth: true
					spacing: Theme.space2
					Row {
						Layout.fillWidth: true
						spacing: 0
						Repeater {
							model: [
								{ "letter": "G", "color": "#4285f4" },
								{ "letter": "o", "color": "#ea4335" },
								{ "letter": "o", "color": "#fbbc05" },
								{ "letter": "g", "color": "#4285f4" },
								{ "letter": "l", "color": "#34a853" },
								{ "letter": "e", "color": "#ea4335" }
							]
							delegate: Label {
								required property var modelData
								text: modelData.letter
								textFormat: Text.PlainText
								color: modelData.color
								font.pixelSize: Theme.fontHeading
								font.bold: true
								Accessible.ignored: true
							}
						}
					}
					Rectangle {
						Layout.preferredWidth: googleModeText.implicitWidth + Theme.space2 * 2
						Layout.preferredHeight: Theme.space5
						radius: height / 2
						color: root.providerAccentSubtle
						Label {
							id: googleModeText
							objectName: "providerGoogleModeLabel"
							anchors.centerIn: parent
							text: root.googleModeLabel
							textFormat: Text.PlainText
							color: root.providerForeground
							font.pixelSize: Theme.fontCaption
							font.bold: true
							Accessible.ignored: true
						}
					}
				}

				Rectangle {
					objectName: "providerGoogleQuery"
					Layout.fillWidth: true
					Layout.preferredHeight: Theme.controlHeight + Theme.space1
					radius: height / 2
					color: Theme.surfaceRaised
					border.color: Theme.surfaceBorder
					RowLayout {
						anchors.fill: parent
						anchors.leftMargin: Theme.space3
						anchors.rightMargin: Theme.space3
						spacing: Theme.space2
						Label {
							objectName: "providerGoogleQueryText"
							Layout.fillWidth: true
							text: root.googleQuery
							textFormat: Text.PlainText
							color: Theme.textStrong
							font.pixelSize: Theme.fontLabel
							font.bold: true
							elide: Text.ElideRight
							Accessible.ignored: true
						}
						ModernIcon {
							name: "search"
							size: Theme.avatarSmall
							color: root.providerForeground
							Accessible.ignored: true
						}
					}
				}

				Flow {
					objectName: "providerGoogleTabs"
					Layout.fillWidth: true
					Layout.preferredHeight: implicitHeight
					spacing: Theme.space3
					Repeater {
						model: root.googleTabs
						delegate: Column {
							required property string modelData
							required property int index
							objectName: "providerGoogleTab_" + index
							property bool active: root.normalizedToken(modelData)
								=== root.normalizedToken(root.googleMode)
							spacing: Theme.space1
							Label {
								text: parent.modelData
								textFormat: Text.PlainText
								color: parent.active ? root.providerForeground : Theme.textMuted
								font.pixelSize: Theme.fontCaption
								font.bold: parent.active
								Accessible.ignored: true
							}
							Rectangle {
								width: parent.width
								height: 2
								radius: 1
								color: parent.active ? root.providerAccent : "transparent"
							}
						}
					}
				}
			}
		}

		Rectangle {
			id: flashbackCard
			objectName: "providerFlashbackThread"
			Layout.fillWidth: true
			Layout.preferredHeight: visible ? flashbackLayout.implicitHeight : 0
			visible: root.flashbackPresentation
			radius: Theme.innerRadius
			color: Theme.embedSurface
			border.color: Theme.embedBorder
			clip: true
			Accessible.role: Accessible.Grouping
			Accessible.name: root.heading
			Accessible.description: root.flashbackAccessibleSummary()

			ColumnLayout {
				id: flashbackLayout
				anchors.left: parent.left
				anchors.right: parent.right
				spacing: 0

				Rectangle {
					objectName: "providerFlashbackMasthead"
					Layout.fillWidth: true
					Layout.preferredHeight: 52
					color: Theme.embedRevealSurface
					RowLayout {
						anchors.fill: parent
						anchors.leftMargin: Theme.space3
						anchors.rightMargin: Theme.space3
						spacing: Theme.space3
						Label {
							objectName: "providerFlashbackLogo"
							text: "FLASHBACK"
							textFormat: Text.PlainText
							color: Theme.textStrong
							font.pixelSize: Theme.fontHeading
							font.bold: true
							font.letterSpacing: -0.6
							Accessible.ignored: true
						}
						Rectangle {
							Layout.preferredWidth: 1
							Layout.preferredHeight: 24
							color: Theme.quietBorder
						}
						Label {
							Layout.fillWidth: true
							text: qsTr("Forum · Aktuellt · Populärt")
							textFormat: Text.PlainText
							color: Theme.secondaryText
							font.pixelSize: Theme.fontCaption
							font.bold: true
							elide: Text.ElideRight
							Accessible.ignored: true
						}
					}
				}

				ColumnLayout {
					Layout.fillWidth: true
					Layout.margins: Theme.space3
					spacing: Theme.space2
					Label {
						objectName: "providerFlashbackTitle"
						Layout.fillWidth: true
						text: root.identityTitle.length > 0 ? root.identityTitle : root.previewTitle
						textFormat: Text.PlainText
						color: Theme.textStrong
						font.pixelSize: Theme.fontTitle
						font.bold: true
						wrapMode: Text.Wrap
						maximumLineCount: 2
						elide: Text.ElideRight
						Accessible.ignored: true
					}
					Label {
						objectName: "providerFlashbackContext"
						Layout.fillWidth: true
						visible: root.identitySubtitle.length > 0
						text: root.identitySubtitle
						textFormat: Text.PlainText
						color: Theme.secondaryText
						font.pixelSize: Theme.fontCaption
						font.bold: true
						elide: Text.ElideRight
						Accessible.ignored: true
					}

					RowLayout {
						Layout.fillWidth: true
						visible: root.hasAny(["forumPostAuthor", "forumPostAuthorAvatarUrl"])
						spacing: Theme.space2
						Rectangle {
							objectName: "providerFlashbackAuthorAvatarBackground"
							Layout.preferredWidth: 30
							Layout.preferredHeight: 30
							radius: width / 2
							color: root.providerAccent
							clip: true
							Label {
								anchors.centerIn: parent
								text: root.safeText(root.firstValue(["forumPostAuthor"]), 2).toUpperCase()
								textFormat: Text.PlainText
								color: Theme.contrastText(parent.color)
								font.pixelSize: Theme.fontCaption
								font.bold: true
								Accessible.ignored: true
							}
							Image {
								objectName: "providerFlashbackAuthorAvatar"
								anchors.fill: parent
								source: root.flashbackAuthorAvatarSource
								asynchronous: true
								cache: false
								sourceSize.width: 128
								sourceSize.height: 128
								fillMode: Image.PreserveAspectCrop
								visible: status === Image.Ready
								Accessible.ignored: true
							}
						}
						ColumnLayout {
							Layout.fillWidth: true
							Layout.minimumWidth: 0
							spacing: 0
							Label {
								objectName: "providerFlashbackAuthor"
								Layout.fillWidth: true
								text: root.safeText(root.firstValue(["forumPostAuthor"]), 256)
								textFormat: Text.PlainText
								color: Theme.textStrong
								font.pixelSize: Theme.fontLabel
								font.bold: true
								elide: Text.ElideRight
								Accessible.ignored: true
							}
							Label {
								Layout.fillWidth: true
								text: root.joinedValue(["forumPostAuthorTitle", "forumPostTime",
									"forumPostNumber"], " · ")
								textFormat: Text.PlainText
								color: Theme.secondaryText
								font.pixelSize: Theme.fontCaption
								elide: Text.ElideRight
								Accessible.ignored: true
							}
						}
					}

					Rectangle {
						objectName: "providerFlashbackQuote"
						Layout.fillWidth: true
						Layout.preferredHeight: visible ? flashbackQuoteLayout.implicitHeight + Theme.space2 * 2 : 0
						visible: root.hasAny(["forumQuoteAuthor", "forumQuoteExcerpt", "forumQuotePostNumber"])
						radius: Theme.space1
						color: Theme.embedRevealSurface
						border.color: Theme.embedBorder
						Rectangle {
							anchors.left: parent.left
							anchors.top: parent.top
							anchors.bottom: parent.bottom
							width: 3
							color: root.providerAccent
						}
						ColumnLayout {
							id: flashbackQuoteLayout
							anchors.fill: parent
							anchors.margins: Theme.space2
							anchors.leftMargin: Theme.space3
							spacing: Theme.space1
							Label {
								Layout.fillWidth: true
								text: qsTr("Replying to %1").arg([
									root.safeText(root.firstValue(["forumQuotePostNumber"]), 64),
									root.safeText(root.firstValue(["forumQuoteAuthor"]), 256)
								].filter(function(value) { return value.length > 0 }).join(" · "))
								textFormat: Text.PlainText
								color: root.providerForeground
								font.pixelSize: Theme.fontCaption
								font.bold: true
								elide: Text.ElideRight
								Accessible.ignored: true
							}
							Label {
								objectName: "providerFlashbackQuoteText"
								Layout.fillWidth: true
								visible: text.length > 0
								text: root.safeText(root.firstValue(["forumQuoteExcerpt"]), 1024)
								textFormat: Text.PlainText
								color: Theme.secondaryText
								font.pixelSize: Theme.fontCaption
								wrapMode: Text.Wrap
								maximumLineCount: root.expanded ? 4 : 2
								elide: Text.ElideRight
								Accessible.ignored: true
							}
						}
					}

					Label {
						objectName: "providerFlashbackReply"
						Layout.fillWidth: true
						visible: text.length > 0
						text: root.safeText(root.firstValue([
							"forumPostExcerpt", "forumFirstPostExcerpt"
						]), 1024) || root.bodyText
						textFormat: Text.PlainText
						color: Theme.textMain
						font.pixelSize: Theme.fontLabel
						lineHeight: 1.35
						wrapMode: Text.Wrap
						maximumLineCount: root.expanded ? 7 : 3
						elide: Text.ElideRight
						Accessible.ignored: true
					}
				}

				Rectangle {
					objectName: "providerFlashbackFooter"
					Layout.fillWidth: true
					Layout.preferredHeight: 38
					color: Theme.embedRevealSurface
					border.color: Theme.embedBorder
					RowLayout {
						anchors.fill: parent
						anchors.leftMargin: Theme.space3
						anchors.rightMargin: Theme.space3
						spacing: Theme.space2
						Label {
							objectName: "providerFlashbackMeta"
							Layout.fillWidth: true
							text: [root.safeText(root.firstValue(["forumPage"]), 64),
								root.safeText(root.firstValue(["forumPageCount"]), 64),
								root.safeText(root.firstValue(["forumPostCount"]), 64)]
								.filter(function(value) { return value.length > 0 }).join(" · ")
							textFormat: Text.PlainText
							color: Theme.secondaryText
							font.pixelSize: Theme.fontCaption
							font.bold: true
							elide: Text.ElideRight
							Accessible.ignored: true
						}
						Label {
							objectName: "providerFlashbackLinkContext"
							text: root.hasValue("postId") ? qsTr("Linked post") : qsTr("Thread")
							textFormat: Text.PlainText
							color: root.providerForeground
							font.pixelSize: Theme.fontCaption
							font.bold: true
							Accessible.ignored: true
						}
					}
				}
			}
		}

		Component {
			id: xCardComponent
			Rectangle {
			id: xCard
			objectName: "providerXPost"
			implicitHeight: xLayout.implicitHeight + Theme.space3 * 2
			radius: Theme.innerRadius
			color: Theme.panel
			border.color: Theme.surfaceBorder
			Accessible.role: Accessible.Grouping
			Accessible.name: root.identityTitle.length > 0 ? root.identityTitle : qsTr("X post")
			Accessible.description: root.socialAccessibleSummary()

			ColumnLayout {
				id: xLayout
				anchors.fill: parent
				anchors.margins: Theme.space3
				spacing: Theme.space3

				RowLayout {
					Layout.fillWidth: true
					spacing: Theme.space2
					Rectangle {
						objectName: "providerXAvatarBackground"
						Layout.preferredWidth: 38
						Layout.preferredHeight: 38
						radius: width / 2
						color: Theme.textStrong
						clip: true
						Label {
							objectName: "providerXAvatarFallback"
							anchors.centerIn: parent
							text: root.identityInitials(root.identityTitle || root.identitySubtitle, "X")
							textFormat: Text.PlainText
							color: Theme.contrastText(parent.color)
							font.pixelSize: Theme.fontLabel
							font.bold: true
							Accessible.ignored: true
						}
						Image {
							objectName: "providerXAvatar"
							anchors.fill: parent
							source: root.xAvatarSource
							asynchronous: true
							cache: false
							sourceSize.width: 128
							sourceSize.height: 128
							fillMode: Image.PreserveAspectCrop
							visible: status === Image.Ready
							Accessible.ignored: true
						}
					}
					ColumnLayout {
						Layout.fillWidth: true
						Layout.minimumWidth: 0
						spacing: 0
						Label {
							objectName: "providerXDisplayName"
							Layout.fillWidth: true
							text: root.identityTitle
							textFormat: Text.PlainText
							color: Theme.textStrong
							font.pixelSize: Theme.fontLabel
							font.bold: true
							elide: Text.ElideRight
							Accessible.ignored: true
						}
						Label {
							objectName: "providerXByline"
							Layout.fillWidth: true
							text: root.identitySubtitle
							textFormat: Text.PlainText
							color: Theme.textMuted
							font.pixelSize: Theme.fontCaption
							elide: Text.ElideRight
							Accessible.ignored: true
						}
					}
					Rectangle {
						objectName: "providerXVerified"
						Layout.preferredWidth: 18
						Layout.preferredHeight: 18
						visible: root.metadata && root.metadata.xVerified === true
						radius: width / 2
						color: root.providerAccent
						Label {
							anchors.centerIn: parent
							text: "✓"
							textFormat: Text.PlainText
							color: Theme.contrastText(parent.color)
							font.pixelSize: 11
							font.bold: true
							Accessible.ignored: true
						}
					}
				}

				Label {
					objectName: "providerXPostText"
					Layout.fillWidth: true
					visible: root.bodyText.length > 0
					text: root.bodyText
					textFormat: Text.PlainText
					color: Theme.textMain
					font.pixelSize: Theme.fontTitle
					lineHeight: 1.25
					wrapMode: Text.Wrap
					maximumLineCount: root.expanded ? 8 : 4
					elide: Text.ElideRight
					Accessible.ignored: true
				}

				Flow {
					id: xMetrics
					objectName: "providerXMetrics"
					Layout.fillWidth: true
					Layout.preferredHeight: implicitHeight
					spacing: Theme.space3
					Repeater {
						model: root.allStats.slice(0, root.expanded ? 6 : 3)
						delegate: Label {
							required property var modelData
							required property int index
							objectName: "providerXMetric_" + index
							text: modelData.value + " " + modelData.label
							textFormat: Text.PlainText
							color: Theme.textMuted
							font.pixelSize: Theme.fontCaption
							font.bold: true
							Accessible.ignored: true
						}
					}
				}
			}
			}
		}

		Component {
			id: instagramCardComponent
			Rectangle {
			id: instagramCard
			objectName: "providerInstagramPost"
			implicitHeight: instagramLayout.implicitHeight + Theme.space3 * 2
			radius: Theme.innerRadius
			color: Theme.panel
			border.color: root.providerAccentBorder
			clip: true
			Accessible.role: Accessible.Grouping
			Accessible.name: root.identityTitle.length > 0 ? root.identityTitle : qsTr("Instagram post")
			Accessible.description: root.socialAccessibleSummary()

			Rectangle {
				anchors.left: parent.left
				anchors.right: parent.right
				anchors.top: parent.top
				height: 3
				gradient: Gradient {
					orientation: Gradient.Horizontal
					GradientStop { position: 0; color: "#f9ce34" }
					GradientStop { position: 0.5; color: "#ee2a7b" }
					GradientStop { position: 1; color: "#6228d7" }
				}
			}

			ColumnLayout {
				id: instagramLayout
				anchors.fill: parent
				anchors.margins: Theme.space3
				anchors.topMargin: Theme.space3 + 3
				spacing: Theme.space3

				RowLayout {
					Layout.fillWidth: true
					spacing: Theme.space2
					Rectangle {
						objectName: "providerInstagramAvatarBackground"
						Layout.preferredWidth: 42
						Layout.preferredHeight: 42
						radius: width / 2
						color: root.providerAccentSubtle
						border.color: root.providerAccentBorder
						clip: true
						Label {
							objectName: "providerInstagramMarkLabel"
							anchors.centerIn: parent
							text: root.identityInitials(root.identityTitle || root.identitySubtitle, "IG")
							textFormat: Text.PlainText
							color: root.providerForeground
							font.pixelSize: Theme.fontLabel
							font.bold: true
							Accessible.ignored: true
						}
						Image {
							objectName: "providerInstagramAvatar"
							anchors.fill: parent
							source: root.instagramAvatarSource
							asynchronous: true
							cache: false
							sourceSize.width: 128
							sourceSize.height: 128
							fillMode: Image.PreserveAspectCrop
							visible: status === Image.Ready
							Accessible.ignored: true
						}
					}
					ColumnLayout {
						Layout.fillWidth: true
						Layout.minimumWidth: 0
						spacing: 0
						Label {
							objectName: "providerInstagramDisplayName"
							Layout.fillWidth: true
							text: root.identityTitle
							textFormat: Text.PlainText
							color: Theme.textStrong
							font.pixelSize: Theme.fontLabel
							font.bold: true
							elide: Text.ElideRight
							Accessible.ignored: true
						}
						Label {
							objectName: "providerInstagramByline"
							Layout.fillWidth: true
							text: root.identitySubtitle
							textFormat: Text.PlainText
							color: Theme.textMuted
							font.pixelSize: Theme.fontCaption
							elide: Text.ElideRight
							Accessible.ignored: true
						}
					}
					Label {
						objectName: "providerInstagramBrand"
						text: "Instagram"
						textFormat: Text.PlainText
						color: root.providerForeground
						font.pixelSize: Theme.fontCaption
						font.bold: true
						font.capitalization: Font.AllUppercase
						Accessible.ignored: true
					}
				}

				Label {
					objectName: "providerInstagramCaption"
					Layout.fillWidth: true
					visible: root.bodyText.length > 0
					text: root.bodyText
					textFormat: Text.PlainText
					color: Theme.textMain
					font.pixelSize: Theme.fontLabel
					lineHeight: 1.3
					wrapMode: Text.Wrap
					maximumLineCount: root.expanded ? 8 : 3
					elide: Text.ElideRight
					Accessible.ignored: true
				}

				RowLayout {
					objectName: "providerInstagramEngagement"
					Layout.fillWidth: true
					visible: root.hasAny(["instagramLikeCount", "instagramCommentCount"])
					spacing: Theme.space4
					Label {
						objectName: "providerInstagramLikes"
						visible: root.hasValue("instagramLikeCount")
						text: qsTr("%1 likes").arg(root.countLabel(root.firstValue(["instagramLikeCount"])))
						textFormat: Text.PlainText
						color: Theme.textStrong
						font.pixelSize: Theme.fontCaption
						font.bold: true
						Accessible.ignored: true
					}
					Label {
						objectName: "providerInstagramComments"
						visible: root.hasValue("instagramCommentCount")
						text: qsTr("%1 comments").arg(root.countLabel(root.firstValue(["instagramCommentCount"])))
						textFormat: Text.PlainText
						color: Theme.textMuted
						font.pixelSize: Theme.fontCaption
						font.bold: true
						Accessible.ignored: true
					}
					Item { Layout.fillWidth: true }
				}

				Label {
					objectName: "providerInstagramExpandedMeta"
					Layout.fillWidth: true
					visible: root.expanded && root.hasValue("instagramMediaKind")
					text: qsTr("Media: %1").arg(root.safeText(root.firstValue(["instagramMediaKind"]), 64))
					textFormat: Text.PlainText
					color: Theme.textMuted
					font.pixelSize: Theme.fontCaption
					Accessible.ignored: true
				}
			}
			}
		}

		Component {
			id: githubCardComponent
			Rectangle {
			id: githubCard
			objectName: "providerGitHubRepository"
			implicitHeight: githubLayout.implicitHeight + Theme.space3 * 2
			radius: Theme.innerRadius
			color: Theme.panel
			border.color: Theme.surfaceBorder
			Accessible.role: Accessible.Grouping
			Accessible.name: qsTr("Repository details")
			Accessible.description: root.socialAccessibleSummary()

			ColumnLayout {
				id: githubLayout
				anchors.fill: parent
				anchors.margins: Theme.space3
				spacing: Theme.space3

				RowLayout {
					Layout.fillWidth: true
					spacing: Theme.space2
					Rectangle {
						objectName: "providerGitHubOwnerAvatarBackground"
						Layout.preferredWidth: 34
						Layout.preferredHeight: 34
						radius: Theme.innerRadius
						color: Theme.surfaceRaised
						border.color: Theme.surfaceBorder
						clip: true
						Label {
							objectName: "providerGitHubOwnerAvatarFallback"
							anchors.centerIn: parent
							text: root.identityInitials(root.firstValue([
								"githubOwnerLogin", "githubOwner"
							]), "GH")
							textFormat: Text.PlainText
							color: Theme.textStrong
							font.pixelSize: Theme.fontCaption
							font.bold: true
							Accessible.ignored: true
						}
						Image {
							objectName: "providerGitHubOwnerAvatar"
							anchors.fill: parent
							source: root.githubOwnerAvatarSource
							asynchronous: true
							cache: false
							sourceSize.width: 128
							sourceSize.height: 128
							fillMode: Image.PreserveAspectCrop
							visible: status === Image.Ready
							Accessible.ignored: true
						}
					}
					ColumnLayout {
						Layout.fillWidth: true
						Layout.minimumWidth: 0
						spacing: 0
						Label {
							text: "GitHub"
							textFormat: Text.PlainText
							color: Theme.textMuted
							font.pixelSize: Theme.fontCaption
							font.bold: true
							Accessible.ignored: true
						}
						Label {
							objectName: "providerGitHubRepoName"
							Layout.fillWidth: true
							text: root.identityTitle
							textFormat: Text.PlainText
							color: Theme.textStrong
							font.pixelSize: Theme.fontTitle
							font.bold: true
							elide: Text.ElideRight
							Accessible.ignored: true
						}
						Label {
							objectName: "providerGitHubIdentitySubtitle"
							Layout.fillWidth: true
							visible: root.identitySubtitle.length > 0
							text: root.identitySubtitle
							textFormat: Text.PlainText
							color: Theme.textMuted
							font.pixelSize: Theme.fontCaption
							elide: Text.ElideRight
							Accessible.ignored: true
						}
					}
					Rectangle {
						Layout.preferredWidth: githubVisibility.implicitWidth + Theme.space2 * 2
						Layout.preferredHeight: 24
						radius: height / 2
						color: "transparent"
						border.color: root.metadata && root.metadata.githubPrivate === true
							? root.withAlpha(Theme.warning, 0.6) : Theme.surfaceBorder
						Label {
							id: githubVisibility
							anchors.centerIn: parent
							text: root.metadata && root.metadata.githubPrivate === true
								? qsTr("Private") : qsTr("Public")
							textFormat: Text.PlainText
							color: root.metadata && root.metadata.githubPrivate === true
								? root.contrastSafeForeground(Theme.warning) : Theme.textMuted
							font.pixelSize: Theme.fontCaption
							font.bold: true
							Accessible.ignored: true
						}
					}
				}

				Label {
					objectName: "providerGitHubDescription"
					Layout.fillWidth: true
					visible: root.bodyText.length > 0
					text: root.bodyText
					textFormat: Text.PlainText
					color: Theme.textMain
					font.pixelSize: Theme.fontLabel
					lineHeight: 1.25
					wrapMode: Text.Wrap
					maximumLineCount: root.expanded ? 6 : 2
					elide: Text.ElideRight
					Accessible.ignored: true
				}

				Flow {
					objectName: "providerGitHubMetrics"
					Layout.fillWidth: true
					Layout.preferredHeight: implicitHeight
					spacing: Theme.space3
					Repeater {
						model: root.githubMetrics.slice(0, 3)
						delegate: Label {
							required property var modelData
							required property int index
							objectName: "providerGitHubMetric_" + index
							text: modelData.value + " " + modelData.label
							textFormat: Text.PlainText
							color: Theme.textMuted
							font.pixelSize: Theme.fontCaption
							font.bold: true
							Accessible.ignored: true
						}
					}
				}

				Label {
					objectName: "providerGitHubExpandedMeta"
					Layout.fillWidth: true
					visible: root.expanded && text.length > 0
					text: root.githubExpandedMeta()
					textFormat: Text.PlainText
					color: Theme.textMuted
					font.pixelSize: Theme.fontCaption
					wrapMode: Text.Wrap
					Accessible.ignored: true
				}

				Flow {
					objectName: "providerGitHubTopics"
					Layout.fillWidth: true
					Layout.preferredHeight: visible ? implicitHeight : 0
					visible: root.expanded && root.githubTopics.length > 0
					spacing: Theme.space1
					Repeater {
						model: root.githubTopics
						delegate: Rectangle {
							required property var modelData
							required property int index
							objectName: "providerGitHubTopic_" + index
							width: githubTopicLabel.implicitWidth + Theme.space2 * 2
							height: 24
							radius: height / 2
							color: root.providerAccentSubtle
							border.color: root.providerAccentBorder
							Label {
								id: githubTopicLabel
								objectName: "providerGitHubTopicLabel_" + parent.index
								anchors.centerIn: parent
								text: root.safeText(parent.modelData, 64)
								textFormat: Text.PlainText
								color: root.providerForeground
								font.pixelSize: Theme.fontCaption
								Accessible.ignored: true
							}
							Accessible.ignored: true
						}
					}
				}
			}
			}
		}

		Component {
			id: twitchCardComponent
			Rectangle {
			id: twitchCard
			objectName: "providerTwitchStream"
			implicitHeight: twitchLayout.implicitHeight + Theme.space3 * 2
			radius: Theme.innerRadius
			color: root.providerErrorText.length > 0
				? root.withAlpha(Theme.danger, 0.08) : root.providerAccentSubtle
			border.color: root.providerErrorText.length > 0
				? root.withAlpha(Theme.danger, 0.6) : root.providerAccentBorder
			Accessible.role: root.providerErrorText.length > 0
				? Accessible.AlertMessage : Accessible.Grouping
			Accessible.name: root.identityTitle.length > 0
				? qsTr("Twitch stream: %1").arg(root.identityTitle) : qsTr("Twitch stream")
			Accessible.description: root.socialAccessibleSummary()

			Rectangle {
				anchors.left: parent.left
				anchors.top: parent.top
				anchors.bottom: parent.bottom
				width: 4
				color: root.providerErrorText.length > 0 ? Theme.danger : root.providerAccent
			}

			ColumnLayout {
				id: twitchLayout
				anchors.fill: parent
				anchors.margins: Theme.space3
				anchors.leftMargin: Theme.space3 + 4
				spacing: Theme.space2

				RowLayout {
					Layout.fillWidth: true
					spacing: Theme.space2
					Rectangle {
						Layout.preferredWidth: 36
						Layout.preferredHeight: 36
						radius: Theme.innerRadius
						color: root.providerAccent
						Label {
							anchors.centerIn: parent
							text: "TV"
							textFormat: Text.PlainText
							color: Theme.contrastText(parent.color)
							font.pixelSize: Theme.fontCaption
							font.bold: true
							Accessible.ignored: true
						}
					}
					ColumnLayout {
						Layout.fillWidth: true
						Layout.minimumWidth: 0
						spacing: 0
						Label {
							objectName: "providerTwitchDisplayName"
							Layout.fillWidth: true
							text: root.identityTitle
							textFormat: Text.PlainText
							color: Theme.textStrong
							font.pixelSize: Theme.fontTitle
							font.bold: true
							elide: Text.ElideRight
							Accessible.ignored: true
						}
						Label {
							objectName: "providerTwitchByline"
							Layout.fillWidth: true
							text: root.identitySubtitle
							textFormat: Text.PlainText
							color: Theme.textMuted
							font.pixelSize: Theme.fontCaption
							elide: Text.ElideRight
							Accessible.ignored: true
						}
					}
					Rectangle {
						objectName: "providerTwitchState"
						Layout.preferredWidth: twitchStateText.implicitWidth + Theme.space2 * 2
						Layout.preferredHeight: 24
						visible: root.providerStateLabel.length > 0
						radius: height / 2
						color: root.withAlpha(root.providerStateColor, 0.15)
						border.color: root.withAlpha(root.providerStateColor, 0.55)
						Label {
							id: twitchStateText
							objectName: "providerTwitchStateLabel"
							anchors.centerIn: parent
							text: root.providerStateLabel
							textFormat: Text.PlainText
							color: root.providerStateForeground
							font.pixelSize: Theme.fontCaption
							font.bold: true
							Accessible.ignored: true
						}
					}
				}

				Label {
					objectName: "providerTwitchAudience"
					Layout.fillWidth: true
					visible: root.hasValue("twitchViewerCount")
					text: qsTr("%1 watching now").arg(root.countLabel(root.firstValue(["twitchViewerCount"])))
					textFormat: Text.PlainText
					color: root.providerErrorText.length > 0 ? Theme.danger : root.providerForeground
					font.pixelSize: Theme.fontLabel
					font.bold: true
					Accessible.ignored: true
				}

				Label {
					objectName: "providerTwitchPlayback"
					Layout.fillWidth: true
					visible: root.expanded && root.hasAny(["twitchEmbedMode", "twitchKind"])
					text: qsTr("Playback: %1").arg(root.safeText(root.firstValue([
						"twitchEmbedMode", "twitchKind"
					]), 128))
					textFormat: Text.PlainText
					color: Theme.textMuted
					font.pixelSize: Theme.fontCaption
					Accessible.ignored: true
				}

				Label {
					objectName: "providerTwitchNote"
					Layout.fillWidth: true
					visible: root.expanded && root.bodyText.length > 0
					text: root.bodyText
					textFormat: Text.PlainText
					color: root.providerErrorText.length > 0 ? Theme.danger : Theme.textMain
					font.pixelSize: Theme.fontLabel
					wrapMode: Text.Wrap
					maximumLineCount: 5
					elide: Text.ElideRight
					Accessible.ignored: true
				}
			}
			}
		}

		Loader {
			id: socialPresentationLoader
			objectName: "providerSocialPresentationLoader"
			Layout.fillWidth: true
			Layout.preferredHeight: item ? item.implicitHeight : 0
			active: root.socialBespokePresentation
			sourceComponent: root.xPresentation ? xCardComponent
				: root.instagramPresentation ? instagramCardComponent
				: root.githubPresentation ? githubCardComponent
				: root.twitchPresentation ? twitchCardComponent : null
		}

		Rectangle {
			id: genericSocialPostCard
			objectName: "providerSocialPost"
			Layout.fillWidth: true
			Layout.preferredHeight: visible ? genericSocialPostLayout.implicitHeight
				+ Theme.space3 * 2 : 0
			visible: root.genericSocialPostPresentation
			radius: Theme.innerRadius
			color: root.withAlpha(root.providerAccent, 0.06)
			border.color: root.withAlpha(root.providerAccent, 0.32)
			Accessible.role: Accessible.Grouping
			Accessible.name: qsTr("Social post")
			Accessible.description: root.genericSocialAccessibleSummary()

			ColumnLayout {
				id: genericSocialPostLayout
				anchors.fill: parent
				anchors.margins: Theme.space3
				spacing: Theme.space2

				RowLayout {
					Layout.fillWidth: true
					spacing: Theme.space2

					ProviderIdentityBadge {
						objectName: "providerSocialIdentityBadge"
						labelObjectName: "providerSocialIdentityLabel"
						providerToken: root.providerToken
						badgeText: root.providerDisplayName
						presentation: "inline"
						accent: root.providerAccent
						foreground: root.providerForeground
						Layout.maximumWidth: genericSocialPostCard.width - Theme.space3 * 2
					}

					Label {
						objectName: "providerSocialAuthor"
						Layout.fillWidth: true
						visible: root.socialPostAuthor.length > 0
						text: root.socialPostAuthor
						textFormat: Text.PlainText
						color: Theme.textMuted
						font.pixelSize: Theme.fontCaption
						font.bold: true
						wrapMode: Text.Wrap
						maximumLineCount: 2
						elide: Text.ElideRight
						Accessible.ignored: true
					}
				}

				Label {
					objectName: "providerSocialPostText"
					Layout.fillWidth: true
					visible: root.socialPostText.length > 0
					text: root.socialPostText
					textFormat: Text.PlainText
					color: Theme.textStrong
					font.pixelSize: Theme.fontLabel
					font.bold: true
					lineHeight: 1.3
					wrapMode: Text.Wrap
					maximumLineCount: root.expanded ? 10 : 4
					elide: root.expanded ? Text.ElideNone : Text.ElideRight
					Accessible.ignored: true
				}

				Label {
					objectName: "providerSocialPostDescription"
					Layout.fillWidth: true
					visible: root.socialPostDescription.length > 0
					text: root.socialPostDescription
					textFormat: Text.PlainText
					color: Theme.textMain
					font.pixelSize: Theme.fontCaption
					lineHeight: 1.25
					wrapMode: Text.Wrap
					maximumLineCount: root.expanded ? 8 : 2
					elide: root.expanded ? Text.ElideNone : Text.ElideRight
					Accessible.ignored: true
				}
			}
		}

		ColumnLayout {
			id: summaryBlock
			objectName: "providerSummary"
			Layout.fillWidth: true
			Layout.preferredHeight: visible ? implicitHeight : 0
			visible: !root.steamPresentation && !root.googlePresentation
				&& !root.genericSocialPostPresentation
				&& !root.flashbackPresentation && (root.financeLayout || root.commerceLayout
				|| (root.identityTitle.length === 0 && root.identitySubtitle.length === 0
					&& root.bodyText.length === 0))
				&& (root.heading.length > 0 || root.primaryValue.length > 0
					|| root.summaryTitle.length > 0 || root.summarySubtitle.length > 0)
			spacing: Theme.space2

			Label {
				Layout.fillWidth: true
				visible: root.heading.length > 0
				text: root.heading
				textFormat: Text.PlainText
				color: Theme.textMuted
				font.pixelSize: Theme.fontCaption
				font.bold: true
				font.capitalization: Font.AllUppercase
				font.letterSpacing: 0.7
				elide: Text.ElideRight
				Accessible.ignored: true
			}

			GridLayout {
				Layout.fillWidth: true
				columns: root.financeLayout && !root.compactLayout ? 2 : 1
				columnSpacing: Theme.space4
				rowSpacing: Theme.space2

				ColumnLayout {
					Layout.fillWidth: true
					Layout.minimumWidth: 0
					visible: root.financeLayout
					spacing: Theme.space1

					Label {
						objectName: "providerSummaryTitle"
						Layout.fillWidth: true
						text: root.summaryTitle
						textFormat: Text.PlainText
						color: Theme.textStrong
						font.pixelSize: Theme.fontHeading
						font.bold: true
						elide: Text.ElideRight
						Accessible.ignored: true
					}
					Label {
						objectName: "providerSummarySubtitle"
						Layout.fillWidth: true
						visible: root.summarySubtitle.length > 0
						text: root.summarySubtitle
						textFormat: Text.PlainText
						color: Theme.textMuted
						font.pixelSize: Theme.fontCaption
						font.bold: true
						elide: Text.ElideRight
						Accessible.ignored: true
					}
				}

				ColumnLayout {
					Layout.fillWidth: true
					Layout.minimumWidth: 0
					Layout.alignment: root.financeLayout && !root.compactLayout
						? Qt.AlignRight | Qt.AlignTop : Qt.AlignLeft | Qt.AlignTop
					spacing: Theme.space1

					Label {
						objectName: "providerDetailsPrimary"
						Layout.fillWidth: true
						visible: root.primaryValue.length > 0
						text: root.primaryValue
						textFormat: Text.PlainText
						color: root.commerceLayout ? root.providerForeground : Theme.textStrong
						font.pixelSize: root.compactLayout ? Theme.fontTitle
							: (root.financeLayout || root.commerceLayout ? Theme.fontHeading + 2 : Theme.fontHeading)
						font.bold: true
						wrapMode: root.compactLayout ? Text.Wrap : Text.NoWrap
						maximumLineCount: 2
						elide: Text.ElideRight
						horizontalAlignment: root.financeLayout && !root.compactLayout
							? Text.AlignRight : Text.AlignLeft
						Accessible.ignored: true
					}
					Label {
						objectName: "providerDetailsSecondary"
						Layout.fillWidth: true
						Layout.preferredHeight: visible ? implicitHeight : 0
						visible: root.secondaryValue.length > 0
						text: root.secondaryValue
						textFormat: Text.PlainText
						color: root.financeLayout ? root.trendColor : Theme.textMuted
						font.pixelSize: Theme.fontLabel
						font.bold: root.financeLayout
						wrapMode: Text.Wrap
						horizontalAlignment: root.financeLayout && !root.compactLayout
							? Text.AlignRight : Text.AlignLeft
						Accessible.ignored: true
					}
					Label {
						objectName: "providerCommerceStatus"
						Layout.fillWidth: true
						visible: root.commerceStatus.length > 0
						text: root.commerceStatus
						textFormat: Text.PlainText
						color: Theme.textMain
						font.pixelSize: Theme.fontCaption
						font.bold: true
						wrapMode: Text.Wrap
						Accessible.ignored: true
					}
				}
			}

			Label {
				objectName: "providerSummaryBody"
				Layout.fillWidth: true
				Layout.preferredHeight: visible ? implicitHeight : 0
				visible: root.commerceLayout && root.bodyText.length > 0
				text: root.bodyText
				textFormat: Text.PlainText
				color: Theme.textMain
				font.pixelSize: Theme.fontLabel
				lineHeight: 1.25
				wrapMode: Text.Wrap
				maximumLineCount: root.expanded ? 5 : 2
				elide: Text.ElideRight
				Accessible.ignored: true
			}
		}

		Rectangle {
			id: identityCard
			objectName: "providerIdentity"
			Layout.fillWidth: true
			Layout.preferredHeight: visible ? identityLayout.implicitHeight + Theme.space3 * 2 : 0
			visible: !root.financeLayout && !root.commerceLayout
				&& !root.googlePresentation && !root.flashbackPresentation
				&& !root.socialBespokePresentation && !root.genericSocialPostPresentation
				&& (root.identityTitle.length > 0
				|| root.identitySubtitle.length > 0 || root.bodyText.length > 0)
			radius: Theme.innerRadius
			color: root.providerErrorText.length > 0
				? root.withAlpha(Theme.danger, 0.08) : "transparent"
			border.color: root.providerErrorText.length > 0 ? root.withAlpha(Theme.danger, 0.65)
				: "transparent"
			Accessible.role: root.providerErrorText.length > 0
				? Accessible.AlertMessage : Accessible.StaticText
			Accessible.name: root.identityTitle.length > 0 ? root.identityTitle : root.heading
			Accessible.description: root.joinAccessibleSentences([root.identitySubtitle, root.bodyText])
			Accessible.ignored: true

			RowLayout {
				id: identityLayout
				anchors.fill: parent
				anchors.margins: Theme.space3
				spacing: Theme.space3

				ProviderIdentityBadge {
					objectName: "providerIdentityMark"
					Layout.preferredWidth: root.variant === "audio" ? 48 : 42
					Layout.preferredHeight: Layout.preferredWidth
					Layout.alignment: Qt.AlignTop
					visible: root.providerMark.length > 0
					providerToken: root.providerToken
					badgeText: root.providerMark
					presentation: "mark"
					labelObjectName: "providerIdentityMarkLabel"
					markExtent: root.variant === "audio" ? 48 : 42
					accent: root.providerErrorText.length > 0 ? Theme.danger : root.providerAccent
					foreground: root.providerErrorText.length > 0 ? Theme.danger : root.providerForeground
					fillOpacity: root.providerErrorText.length > 0 ? 0.12 : 0.14
					borderOpacity: root.providerErrorText.length > 0 ? 0.5 : 0.46
				}

				ColumnLayout {
					Layout.fillWidth: true
					Layout.minimumWidth: 0
					spacing: Theme.space1

					Label {
						Layout.fillWidth: true
						visible: root.heading.length > 0
						text: root.heading
						textFormat: Text.PlainText
						color: Theme.textMuted
						font.pixelSize: Theme.fontCaption
						font.bold: true
						font.capitalization: Font.AllUppercase
						font.letterSpacing: 0.7
						elide: Text.ElideRight
						Accessible.ignored: true
					}
					RowLayout {
						Layout.fillWidth: true
						spacing: Theme.space1
						Label {
							objectName: "providerIdentityTitle"
							Layout.fillWidth: true
							visible: root.identityTitle.length > 0
							text: root.identityTitle
							textFormat: Text.PlainText
							color: Theme.textStrong
							font.pixelSize: Theme.fontTitle
							font.bold: true
							wrapMode: Text.Wrap
							Accessible.ignored: true
						}
						Rectangle {
							objectName: "providerVerifiedBadge"
							Layout.preferredWidth: 18
							Layout.preferredHeight: 18
							visible: (root.variant === "x" && root.metadata && root.metadata.xVerified === true)
								|| (root.variant === "twitch" && root.metadata
									&& root.metadata.twitchLiveState === "live")
							radius: width / 2
							color: root.variant === "twitch" ? Theme.success : root.providerAccent
							Accessible.ignored: true
							Label {
								anchors.centerIn: parent
								text: "✓"
								textFormat: Text.PlainText
								color: Theme.contrastText(parent.color)
								font.pixelSize: 11
								font.bold: true
								Accessible.ignored: true
							}
						}
					}
					Label {
						objectName: "providerIdentitySubtitle"
						Layout.fillWidth: true
						visible: root.identitySubtitle.length > 0
						text: root.identitySubtitle
						textFormat: Text.PlainText
						color: Theme.textMuted
						font.pixelSize: Theme.fontCaption
						wrapMode: Text.Wrap
						Accessible.ignored: true
					}
					Label {
						objectName: "providerIdentityBody"
						Layout.fillWidth: true
						visible: root.bodyText.length > 0
						text: root.bodyText
						textFormat: Text.PlainText
						color: root.providerErrorText.length > 0 ? Theme.danger : Theme.textMain
						font.pixelSize: Theme.fontLabel
						lineHeight: 1.25
						wrapMode: Text.Wrap
						maximumLineCount: root.expanded ? 6 : 3
						elide: Text.ElideRight
						Accessible.ignored: true
					}
				}
			}
		}

		Rectangle {
			id: financeChart
			objectName: "providerFinanceChart"
			Layout.fillWidth: true
			Layout.preferredHeight: visible ? 102 : 0
			visible: sparkline.pointCount > 1
			radius: Theme.innerRadius
			color: Theme.panel
			border.color: Theme.surfaceBorder

			Label {
				anchors.left: parent.left
				anchors.top: parent.top
				anchors.margins: Theme.space2
				text: root.safeText(root.firstValue(["financeRangeLabel"]), 32) || qsTr("Trend")
				textFormat: Text.PlainText
				color: Theme.textMuted
				font.pixelSize: Theme.fontCaption
				font.bold: true
				Accessible.ignored: true
			}
			Label {
				anchors.right: parent.right
				anchors.top: parent.top
				anchors.margins: Theme.space2
				visible: root.financeRangeSummary.length > 0
				text: root.financeRangeSummary
				textFormat: Text.PlainText
				color: root.trendColor
				font.pixelSize: Theme.fontCaption
				font.bold: true
				Accessible.ignored: true
			}

			Canvas {
				id: sparkline
				objectName: "providerDetailsSparkline"
				readonly property int pointCount: root.sparklinePoints.length
				anchors.left: parent.left
				anchors.right: parent.right
				anchors.top: parent.top
				anchors.bottom: parent.bottom
				anchors.leftMargin: Theme.space2
				anchors.rightMargin: Theme.space2
				anchors.topMargin: Theme.space4
				anchors.bottomMargin: Theme.space2
				renderTarget: Canvas.FramebufferObject
				onPointCountChanged: requestPaint()
				onWidthChanged: requestPaint()
				onHeightChanged: requestPaint()
				Connections {
					target: root
					function onTrendColorChanged() { sparkline.requestPaint() }
					function onSparklinePointsChanged() { sparkline.requestPaint() }
				}
				onPaint: {
					const context = sparkline.getContext("2d")
					context.clearRect(0, 0, sparkline.width, sparkline.height)
					if (root.sparklinePoints.length < 2 || sparkline.width <= 2 || sparkline.height <= 2)
						return
					let minimum = root.sparklinePoints[0]
					let maximum = root.sparklinePoints[0]
					for (let index = 1; index < root.sparklinePoints.length; ++index) {
						minimum = Math.min(minimum, root.sparklinePoints[index])
						maximum = Math.max(maximum, root.sparklinePoints[index])
					}
					const range = Math.max(0.000001, maximum - minimum)
					const inset = 3
					const points = []
					for (let index = 0; index < root.sparklinePoints.length; ++index) {
						points.push({
							"x": inset + index * (sparkline.width - inset * 2)
								/ (root.sparklinePoints.length - 1),
							"y": inset + (maximum - root.sparklinePoints[index])
								* (sparkline.height - inset * 2) / range
						})
					}
					context.beginPath()
					context.moveTo(points[0].x, sparkline.height - inset)
					for (let index = 0; index < points.length; ++index)
						context.lineTo(points[index].x, points[index].y)
					context.lineTo(points[points.length - 1].x, sparkline.height - inset)
					context.closePath()
					context.fillStyle = root.withAlpha(root.trendColor, 0.10)
					context.fill()
					context.beginPath()
					for (let index = 0; index < points.length; ++index) {
						if (index === 0)
							context.moveTo(points[index].x, points[index].y)
						else
							context.lineTo(points[index].x, points[index].y)
					}
					context.strokeStyle = root.trendColor
					context.lineWidth = 2.5
					context.lineJoin = "round"
					context.lineCap = "round"
					context.stroke()
				}
				Accessible.role: Accessible.Chart
				Accessible.name: qsTr("Price trend with %1 points").arg(sparkline.pointCount)
				Accessible.description: [
					root.safeText(root.firstValue(["financeRangeLabel"]), 32) || qsTr("Trend"),
					root.financeRangeSummary
				].filter(function(value) { return value.length > 0 }).join(" · ")
			}
		}

		GridLayout {
			id: statsFlow
			objectName: "providerDetailsStats"
			Layout.fillWidth: true
			Layout.preferredHeight: statsFlow.visible ? statsFlow.implicitHeight : 0
			visible: root.visibleStats.length > 0 && !root.steamPresentation
				&& !root.googlePresentation && !root.flashbackPresentation
				&& !root.socialBespokePresentation && !root.genericSocialPostPresentation
			columns: root.statColumnCount(width)
			columnSpacing: Theme.space2
			rowSpacing: Theme.space2

			Repeater {
				model: root.visibleStats
				delegate: Rectangle {
					id: statTile
					required property var modelData
					required property int index
					readonly property string presentation: root.financeLayout ? "market"
						: root.identityPresentation ? "metric"
						: root.commerceLayout ? "spec" : "detail"
					objectName: "providerStat_" + statTile.index
					Layout.fillWidth: true
					Layout.preferredWidth: root.statWidth(statsFlow.width)
					Layout.preferredHeight: statColumn.implicitHeight
						+ (statTile.presentation === "detail" ? Theme.space2 * 2 : Theme.space1 * 2)
					radius: Theme.innerRadius
					color: statTile.presentation === "detail" ? Theme.panel : "transparent"
					border.color: statTile.presentation === "detail" ? Theme.surfaceBorder : "transparent"
					Accessible.role: Accessible.StaticText
					Accessible.name: statTile.modelData.label
					Accessible.description: statTile.modelData.value
					Accessible.ignored: true

					ColumnLayout {
						id: statColumn
						anchors.left: parent.left
						anchors.right: parent.right
						anchors.verticalCenter: parent.verticalCenter
						anchors.margins: statTile.presentation === "detail"
							? Theme.space2 : Theme.space1
						spacing: Theme.space1

						Label {
							objectName: "providerStatLabel_" + statTile.index
							Layout.fillWidth: true
							text: statTile.modelData.label
							textFormat: Text.PlainText
							color: Theme.textMuted
							font.pixelSize: Theme.fontCaption
							font.bold: statTile.presentation !== "detail"
							font.capitalization: statTile.presentation !== "detail"
								? Font.AllUppercase : Font.MixedCase
							font.letterSpacing: statTile.presentation !== "detail" ? 0.35 : 0
							elide: Text.ElideRight
							horizontalAlignment: statTile.presentation === "metric"
								? Text.AlignHCenter : Text.AlignLeft
							Accessible.ignored: true
						}
						Label {
							objectName: "providerStatValue_" + statTile.index
							Layout.fillWidth: true
							text: statTile.modelData.value
							textFormat: Text.PlainText
							color: statTile.modelData.tone === "success" ? Theme.success
								: statTile.modelData.tone === "danger" ? Theme.danger
								: statTile.modelData.tone === "warning" ? Theme.warning : Theme.textStrong
							font.pixelSize: Theme.fontLabel
							font.bold: true
							wrapMode: Text.Wrap
							maximumLineCount: 2
							elide: Text.ElideRight
							horizontalAlignment: statTile.presentation === "metric"
								? Text.AlignHCenter : Text.AlignLeft
							Accessible.ignored: true
						}
					}

					Rectangle {
						anchors.left: parent.left
						anchors.right: parent.right
						anchors.bottom: parent.bottom
						height: 1
						visible: statTile.presentation !== "detail"
						color: root.withAlpha(Theme.surfaceBorder, 0.72)
					}
				}
			}
		}

		Flow {
			id: chipFlow
			objectName: "providerDetailsChips"
			Layout.fillWidth: true
			Layout.preferredHeight: chipFlow.visible ? chipFlow.implicitHeight : 0
			visible: root.visibleChips.length > 0 && !root.steamPresentation
				&& !root.googlePresentation && !root.flashbackPresentation
				&& !root.socialBespokePresentation && !root.genericSocialPostPresentation
			spacing: Theme.space1

			Repeater {
				model: root.visibleChips
				delegate: Rectangle {
					id: chip
					required property var modelData
					required property int index
					objectName: "providerChip_" + chip.index
					width: Math.min(chipFlow.width, chipLabel.implicitWidth + Theme.space2 * 2)
					height: 24
					radius: height / 2
					color: chip.modelData.tone === "accent" ? root.providerAccentSubtle
						: chip.modelData.tone === "warning" ? root.withAlpha(Theme.warning, 0.13)
						: chip.modelData.tone === "success" ? root.withAlpha(Theme.success, 0.13)
						: chip.modelData.tone === "danger" ? root.withAlpha(Theme.danger, 0.13)
						: Theme.panel
					border.color: chip.modelData.tone === "warning" ? root.withAlpha(Theme.warning, 0.5)
						: chip.modelData.tone === "success" ? root.withAlpha(Theme.success, 0.5)
						: chip.modelData.tone === "danger" ? root.withAlpha(Theme.danger, 0.5)
						: chip.modelData.tone === "accent" ? root.providerAccentBorder
						: Theme.surfaceBorder
					Accessible.role: Accessible.StaticText
					Accessible.name: chip.modelData.text
					Accessible.ignored: true

					Label {
						id: chipLabel
						objectName: "providerChipLabel_" + chip.index
						anchors.fill: parent
						anchors.leftMargin: Theme.space2
						anchors.rightMargin: Theme.space2
						text: chip.modelData.text
						textFormat: Text.PlainText
						color: root.chipForeground(chip.modelData.tone)
						font.pixelSize: Theme.fontCaption
						font.bold: chip.modelData.tone !== "normal"
						elide: Text.ElideRight
						verticalAlignment: Text.AlignVCenter
						Accessible.ignored: true
					}
				}
			}
		}

		Rectangle {
			id: releaseCard
			objectName: "providerGitHubRelease"
			Layout.fillWidth: true
			Layout.preferredHeight: visible ? releaseLayout.implicitHeight + Theme.space3 * 2 : 0
			visible: root.releaseInfo.hasSummary && (!root.githubPresentation || root.expanded)
			radius: Theme.innerRadius
			color: Theme.panel
			border.color: Theme.surfaceBorder
			Accessible.role: Accessible.Grouping
			Accessible.name: qsTr("Latest release")
			Accessible.description: root.releaseAccessibleDescription()
			Accessible.ignored: root.githubPresentation

			ColumnLayout {
				id: releaseLayout
				anchors.fill: parent
				anchors.margins: Theme.space3
				spacing: Theme.space2

				RowLayout {
					Layout.fillWidth: true
					spacing: Theme.space2

					ColumnLayout {
						Layout.fillWidth: true
						spacing: Theme.space1
						Label {
							Layout.fillWidth: true
							text: qsTr("Latest release")
							textFormat: Text.PlainText
							color: Theme.textMuted
							font.pixelSize: Theme.fontCaption
							font.bold: true
						}
						Label {
							objectName: "providerReleaseTitle"
							Layout.fillWidth: true
							visible: !root.releaseInfo.loading && !root.releaseInfo.missing
							text: [root.releaseInfo.name, root.releaseInfo.tag]
								.filter(function(value) { return value.length > 0 }).join(" · ")
							textFormat: Text.PlainText
							color: Theme.textStrong
							font.pixelSize: Theme.fontLabel
							font.bold: true
							wrapMode: Text.Wrap
						}
						Label {
							Layout.fillWidth: true
							visible: root.releaseInfo.loading || root.releaseInfo.missing
							text: root.releaseInfo.loading ? qsTr("Checking for a release…")
								: qsTr("No published release found")
							textFormat: Text.PlainText
							color: Theme.textMuted
							font.pixelSize: Theme.fontLabel
						}
					}
					ModernBusyIndicator {
						objectName: "providerReleaseBusyIndicator"
						visible: root.releaseInfo.loading
						running: visible
						Layout.preferredWidth: Theme.controlHeight
						Layout.preferredHeight: Theme.controlHeight
						Accessible.name: qsTr("Checking for the latest release")
					}
				}

				Label {
					Layout.fillWidth: true
					visible: root.releaseInfo.publishedAt.length > 0
					text: root.releaseInfo.prerelease
						? qsTr("Prerelease · %1").arg(root.releaseInfo.publishedAt)
						: root.releaseInfo.publishedAt
					textFormat: Text.PlainText
					color: root.releaseInfo.prerelease ? Theme.warning : Theme.textMuted
					font.pixelSize: Theme.fontCaption
					wrapMode: Text.Wrap
				}

				Label {
					objectName: "providerReleaseNotes"
					Layout.fillWidth: true
					visible: root.expanded && root.releaseInfo.notes.length > 0
					text: root.releaseInfo.notes
					textFormat: Text.PlainText
					color: Theme.textMain
					font.pixelSize: Theme.fontLabel
					wrapMode: Text.Wrap
				}

				Label {
					Layout.fillWidth: true
					visible: root.expanded && (root.releaseInfo.assetName.length > 0
						|| root.releaseInfo.assetCount.length > 0 || root.releaseInfo.downloadCount.length > 0)
					text: [root.releaseInfo.assetName,
						root.releaseInfo.assetCount.length > 0
							? qsTr("%1 assets").arg(root.releaseInfo.assetCount) : "",
						root.releaseInfo.downloadCount.length > 0
							? qsTr("%1 downloads").arg(root.releaseInfo.downloadCount) : ""]
						.filter(function(value) { return value.length > 0 }).join(" · ")
					textFormat: Text.PlainText
					color: Theme.textMuted
					font.pixelSize: Theme.fontCaption
					wrapMode: Text.Wrap
				}

				Flow {
					Layout.fillWidth: true
					Layout.preferredHeight: visible ? implicitHeight : 0
					visible: root.expanded && (root.releaseInfo.url.length > 0
						|| root.releaseInfo.assetUrl.length > 0)
					spacing: Theme.space2

					ModernButton {
						objectName: "providerReleaseOpenButton"
						visible: root.releaseInfo.url.length > 0
						text: qsTr("Open release")
						dense: true
						Accessible.description: root.releaseInfo.name
						onClicked: root.externalOpenRequested(root.releaseInfo.url)
					}
					ModernButton {
						objectName: "providerReleaseAssetButton"
						visible: root.releaseInfo.assetUrl.length > 0
						text: root.releaseInfo.assetName.length > 0
							? qsTr("Open %1").arg(root.releaseInfo.assetName) : qsTr("Open asset")
						dense: true
						Accessible.description: qsTr("Open the release asset in the default browser")
						onClicked: root.externalOpenRequested(root.releaseInfo.assetUrl)
					}
				}
			}
		}

		ColumnLayout {
			id: contextColumn
			objectName: "providerDetailsContext"
			Layout.fillWidth: true
			Layout.preferredHeight: contextColumn.visible ? contextColumn.implicitHeight : 0
			visible: root.expanded && root.contextPosts.length > 0
				&& !root.flashbackPresentation
			spacing: Theme.space2

			Label {
				text: root.variant === "forum" ? qsTr("Post context") : qsTr("Conversation context")
				textFormat: Text.PlainText
				color: Theme.textMuted
				font.pixelSize: Theme.fontCaption
				font.bold: true
			}
			Repeater {
				model: root.contextPosts
				delegate: Rectangle {
					id: contextCard
					required property var modelData
					required property int index
					objectName: "providerContext_" + contextCard.index
					Layout.fillWidth: true
					Layout.preferredHeight: contextLayout.implicitHeight + Theme.space3 * 2
					radius: Theme.innerRadius
					color: Theme.panel
					border.color: Theme.surfaceBorder
					Accessible.role: Accessible.StaticText
					Accessible.name: [contextCard.modelData.name, contextCard.modelData.handle].filter(function(value) {
						return value.length > 0
					}).join(" ")
					Accessible.description: contextCard.modelData.text
					Accessible.ignored: root.socialBespokePresentation

					ColumnLayout {
						id: contextLayout
						anchors.fill: parent
						anchors.margins: Theme.space3
						spacing: Theme.space1

						Label {
							Layout.fillWidth: true
							visible: contextCard.modelData.label.length > 0
							text: contextCard.modelData.label
							textFormat: Text.PlainText
							color: Theme.textMuted
							font.pixelSize: Theme.fontCaption
							font.bold: true
						}
						Label {
							objectName: "providerContextHeader_" + contextCard.index
							Layout.fillWidth: true
							text: [contextCard.modelData.name, contextCard.modelData.handle,
								contextCard.modelData.verified ? qsTr("Verified") : ""].filter(function(value) {
									return value.length > 0
								}).join(" · ")
							textFormat: Text.PlainText
							color: Theme.textStrong
							font.pixelSize: Theme.fontLabel
							font.bold: true
							elide: Text.ElideRight
						}
						Label {
							objectName: "providerContextText_" + contextCard.index
							Layout.fillWidth: true
							visible: contextCard.modelData.text.length > 0
							text: contextCard.modelData.text
							textFormat: Text.PlainText
							color: Theme.textMain
							font.pixelSize: Theme.fontLabel
							wrapMode: Text.Wrap
							maximumLineCount: 4
							elide: Text.ElideRight
						}
					}
				}
			}
		}
	}
}
