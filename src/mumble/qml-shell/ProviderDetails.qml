pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

FocusScope {
	id: root

	property var metadata: ({})
	property string previewKind: ""
	property string providerHint: ""
	property string previewTitle: ""
	property string previewSubtitle: ""
	property string previewDescription: ""
	property bool expanded: false
	readonly property bool compactLayout: width < 440
	readonly property string stableKind: normalizedStableKind()
	readonly property string stableProvider: normalizedStableProvider()
	readonly property string family: detectFamily()
	readonly property string variant: detectVariant()
	readonly property string heading: familyHeading()
	readonly property string primaryValue: buildPrimaryValue()
	readonly property string secondaryValue: buildSecondaryValue()
	readonly property string identityTitle: buildIdentityTitle()
	readonly property string identitySubtitle: buildIdentitySubtitle()
	readonly property string bodyText: buildBodyText()
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
	readonly property var allStats: buildStats()
	readonly property var visibleStats: allStats.slice(0, expanded ? 8 : (compactLayout ? 2 : 3))
	readonly property var allChips: buildChips()
	readonly property var visibleChips: allChips.slice(0, expanded ? 8 : 4)
	readonly property var contextPosts: buildContextPosts()
	readonly property var sparklinePoints: buildSparklinePoints()
	readonly property var releaseInfo: buildReleaseInfo()
	readonly property int collapsedStatCount: compactLayout ? 2 : 3
	readonly property int collapsedChipCount: 4
	readonly property bool releaseCanExpand: releaseInfo.notes.length > 0
		|| releaseInfo.url.length > 0 || releaseInfo.assetUrl.length > 0
		|| releaseInfo.assetName.length > 0 || releaseInfo.assetCount.length > 0
		|| releaseInfo.downloadCount.length > 0
	readonly property bool hasDetails: warningText.length > 0 || primaryValue.length > 0
		|| allStats.length > 0 || allChips.length > 0 || contextPosts.length > 0
		|| sparklinePoints.length > 1 || identityTitle.length > 0
		|| identitySubtitle.length > 0 || bodyText.length > 0 || releaseInfo.hasSummary
	readonly property bool canExpand: allStats.length > collapsedStatCount
		|| allChips.length > collapsedChipCount || contextPosts.length > 0
		|| releaseCanExpand
	readonly property bool ownsDescription: bodyText.length > 0

	signal externalOpenRequested(string url)

	objectName: "providerDetails"
	implicitHeight: hasDetails ? detailsColumn.implicitHeight : 0
	visible: hasDetails
	activeFocusOnTab: false
	Accessible.role: Accessible.Grouping
	Accessible.name: heading.length > 0 ? heading : qsTr("Provider details")
	Accessible.description: accessibleSummary()

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

	function withAlpha(color, alpha) {
		return Qt.rgba(color.r, color.g, color.b, alpha)
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
			return "product"
		}
		if (family === "editorial") {
			if (hasAny(["forumProvider", "forumThreadId", "forumPostAuthor"]))
				return "forum"
			if (hasAny(["audioProvider", "audioProgram"]))
				return "audio"
			return "article"
		}
		if (family === "social") {
			if (hasAny(["twitchLiveState", "twitchBadge", "twitchChannel"]))
				return "twitch"
			if (hasAny(["githubRepo", "githubStars"]))
				return "github"
			if (hasAny(["instagramHandle", "instagramLikeCount"]))
				return "instagram"
			return "x"
		}
		if (family === "geo") {
			return "weather"
		}
		if (family === "search")
			return "googleSearch"
		return family
	}

	function familyHeading() {
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

	function buildIdentityTitle() {
		if (variant === "twitch")
			return safeText(firstValue(["twitchDisplayName", "twitchChannel"]), 256)
		if (variant === "instagram") {
			const displayName = safeText(firstValue(["instagramDisplayName"]), 256)
			const handle = safeText(firstValue(["instagramHandle"]), 128)
			return displayName.length > 0 ? displayName : handle
		}
		if (variant === "github")
			return safeText(firstValue(["githubFullName", "githubRepo"]), 256)
		if (variant === "forum")
			return safeText(firstValue(["forumThreadTitle"]), 512)
		if (variant === "linkDigest")
			return safeText(firstValue(["linkDigestTitle"]), 512) || safeText(previewTitle, 512)
		if (variant === "googleSearch")
			return safeText(firstValue(["googleSearchModeLabel"]), 256) || safeText(previewTitle, 256)
		return ""
	}

	function buildIdentitySubtitle() {
		if (variant === "twitch")
			return joinedValue(["twitchBadge", "twitchGame"], " · ")
		if (variant === "instagram") {
			const handle = safeText(firstValue(["instagramHandle"]), 128)
			return handle !== identityTitle ? handle : ""
		}
		if (variant === "github")
			return joinedValue(["githubOwnerLogin", "githubLanguage"], " · ")
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
		if (variant === "twitch")
			return safeText(firstValue(["twitchStateFailure", "twitchMetadataFailure",
				"twitchPlaybackNote", "twitchDisclaimer"]), 1024)
		if (variant === "instagram")
			return safeText(firstValue(["instagramCaption"]), 1024) || safeText(previewDescription, 1024)
		if (variant === "github")
			return safeText(firstValue(["githubDescription"]), 1024)
				|| safeText(previewDescription, 1024)
		if (variant === "linkDigest")
			return safeText(firstValue(["linkDigestCaption"]), 1024)
				|| safeText(previewDescription, 1024)
		if (variant === "googleSearch")
			return safeText(firstValue(["googleSearchQuery"]), 1024)
				|| safeText(previewDescription, 1024)
		return ""
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
			return joinedValue(["productOriginalPrice", "gameStoreOriginalPrice", "steamOriginalPrice",
				"listingOriginalPrice", "productDiscount", "gameStoreDiscount", "steamDiscountPercent"], "  ")
		if (family === "geo")
			return safeText(firstValue(["statusLabel"]), 512)
		return ""
	}

	function addStat(result, label, value, tone) {
		const text = safeText(value, 512)
		if (text.length === 0 || result.length >= 8)
			return
		result.push({ "label": label, "value": text, "tone": tone || "normal" })
	}

	function addSpecStats(result, key) {
		const source = metadata && Array.isArray(metadata[key]) ? metadata[key] : []
		for (let index = 0; index < source.length && result.length < 8; ++index) {
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
			addStat(result, qsTr("Rating"), joinedValue(["gameStoreRating", "gameStoreReviewCount"]))
			addStat(result, qsTr("Developer"), firstValue(["steamDeveloper", "gameStoreBrand"]))
			addStat(result, qsTr("Released"), firstValue(["steamReleaseDate"]))
			addStat(result, qsTr("Reviews"), firstValue(["steamReviewSummary", "steamReviewPercent"]))
			addStat(result, qsTr("Recommendations"), countLabel(firstValue(["steamRecommendationsTotal"])))
			addStat(result, qsTr("Score"), firstValue(["steamMetacriticScore"]))
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
			addStat(result, qsTr("Provider"), firstValue(["audioProvider"]))
			addStat(result, qsTr("Program"), firstValue(["audioProgram"]))
			addStat(result, qsTr("Published"), firstValue(["articlePublishedAt"]))
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
		return result.slice(0, 8)
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

	function statWidth(availableWidth) {
		if (compactLayout || availableWidth < 400)
			return availableWidth
		if (availableWidth < 620)
			return Math.max(1, (availableWidth - Theme.space2) / 2)
		return Math.max(1, (availableWidth - Theme.space2 * 2) / 3)
	}

	function accessibleSummary() {
		const parts = []
		if (warningText.length > 0)
			parts.push(warningText)
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
		for (let index = 0; index < visibleStats.length; ++index)
			parts.push(visibleStats[index].label + ": " + visibleStats[index].value)
		if (releaseInfo.hasSummary)
			parts.push([releaseInfo.name, releaseInfo.tag, releaseInfo.publishedAt]
				.filter(function(value) { return value.length > 0 }).join(" "))
		return parts.join(". ").slice(0, 1024)
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

		RowLayout {
			Layout.fillWidth: true
			Layout.preferredHeight: visible ? implicitHeight : 0
			visible: root.heading.length > 0 || root.primaryValue.length > 0
			spacing: Theme.space3

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
						font.letterSpacing: 0.5
						elide: Text.ElideRight
					}
					Label {
						objectName: "providerDetailsPrimary"
						Layout.fillWidth: true
						visible: root.primaryValue.length > 0
						text: root.primaryValue
						textFormat: Text.PlainText
						color: Theme.textStrong
						font.pixelSize: root.compactLayout ? Theme.fontTitle : Theme.fontHeading
						font.bold: true
						wrapMode: root.compactLayout ? Text.Wrap : Text.NoWrap
						maximumLineCount: 2
						elide: Text.ElideRight
					}
				}
			}

		Label {
				objectName: "providerDetailsSecondary"
				Layout.fillWidth: true
				Layout.preferredHeight: visible ? implicitHeight : 0
				visible: root.secondaryValue.length > 0
				text: root.secondaryValue
				textFormat: Text.PlainText
				color: root.family === "finance" ? root.trendColor : Theme.textMuted
				font.pixelSize: Theme.fontLabel
				font.bold: root.family === "finance"
				wrapMode: Text.Wrap
				horizontalAlignment: root.compactLayout ? Text.AlignLeft : Text.AlignRight
			}

		Rectangle {
			id: identityCard
			objectName: "providerIdentity"
			Layout.fillWidth: true
			Layout.preferredHeight: visible ? identityLayout.implicitHeight + Theme.space3 * 2 : 0
			visible: root.identityTitle.length > 0 || root.identitySubtitle.length > 0
				|| root.bodyText.length > 0
			radius: Theme.innerRadius
			color: Theme.panel
			border.color: root.providerErrorText.length > 0 ? root.withAlpha(Theme.danger, 0.65)
				: Theme.surfaceBorder
			Accessible.role: root.providerErrorText.length > 0
				? Accessible.AlertMessage : Accessible.StaticText
			Accessible.name: root.identityTitle.length > 0 ? root.identityTitle : root.heading
			Accessible.description: [root.identitySubtitle, root.bodyText]
				.filter(function(value) { return value.length > 0 }).join(". ")

			ColumnLayout {
				id: identityLayout
				anchors.fill: parent
				anchors.margins: Theme.space3
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
				}
				Label {
					objectName: "providerIdentityBody"
					Layout.fillWidth: true
					visible: root.bodyText.length > 0
					text: root.bodyText
					textFormat: Text.PlainText
					color: root.providerErrorText.length > 0 ? Theme.danger : Theme.textMain
					font.pixelSize: Theme.fontLabel
					wrapMode: Text.Wrap
				}
			}
		}

		Canvas {
			id: sparkline
			objectName: "providerDetailsSparkline"
			readonly property int pointCount: root.sparklinePoints.length
			Layout.fillWidth: true
			Layout.preferredHeight: sparkline.pointCount > 1 ? 64 : 0
			visible: sparkline.pointCount > 1
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
				context.beginPath()
				for (let index = 0; index < root.sparklinePoints.length; ++index) {
					const x = inset + index * (sparkline.width - inset * 2)
						/ (root.sparklinePoints.length - 1)
					const y = inset + (maximum - root.sparklinePoints[index])
						* (sparkline.height - inset * 2) / range
					if (index === 0)
						context.moveTo(x, y)
					else
						context.lineTo(x, y)
				}
				context.strokeStyle = root.trendColor
				context.lineWidth = 2
				context.lineJoin = "round"
				context.lineCap = "round"
				context.stroke()
			}
			Accessible.role: Accessible.Chart
			Accessible.name: qsTr("Price trend with %1 points").arg(sparkline.pointCount)
		}

		GridLayout {
			id: statsFlow
			objectName: "providerDetailsStats"
			Layout.fillWidth: true
			Layout.preferredHeight: statsFlow.visible ? statsFlow.implicitHeight : 0
			visible: root.visibleStats.length > 0
			columns: root.compactLayout || width < 400 ? 1 : (width < 620 ? 2 : 3)
			columnSpacing: Theme.space2
			rowSpacing: Theme.space2

			Repeater {
				model: root.visibleStats
				delegate: Rectangle {
					id: statTile
					required property var modelData
					required property int index
					objectName: "providerStat_" + statTile.index
					Layout.fillWidth: true
					Layout.preferredWidth: root.statWidth(statsFlow.width)
					Layout.preferredHeight: statColumn.implicitHeight + Theme.space2 * 2
					radius: Theme.innerRadius
					color: Theme.panel
					border.color: Theme.surfaceBorder
					Accessible.role: Accessible.StaticText
					Accessible.name: statTile.modelData.label
					Accessible.description: statTile.modelData.value

					ColumnLayout {
						id: statColumn
						anchors.left: parent.left
						anchors.right: parent.right
						anchors.verticalCenter: parent.verticalCenter
						anchors.margins: Theme.space2
						spacing: Theme.space1

						Label {
							objectName: "providerStatLabel_" + statTile.index
							Layout.fillWidth: true
							text: statTile.modelData.label
							textFormat: Text.PlainText
							color: Theme.textMuted
							font.pixelSize: Theme.fontCaption
							elide: Text.ElideRight
						}
						Label {
							objectName: "providerStatValue_" + statTile.index
							Layout.fillWidth: true
							text: statTile.modelData.value
							textFormat: Text.PlainText
							color: Theme.textStrong
							font.pixelSize: Theme.fontLabel
							font.bold: true
							wrapMode: Text.Wrap
							maximumLineCount: 2
							elide: Text.ElideRight
						}
					}
				}
			}
		}

		Flow {
			id: chipFlow
			objectName: "providerDetailsChips"
			Layout.fillWidth: true
			Layout.preferredHeight: chipFlow.visible ? chipFlow.implicitHeight : 0
			visible: root.visibleChips.length > 0
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
					color: chip.modelData.tone === "accent" ? Theme.accentSubtle
						: chip.modelData.tone === "warning" ? root.withAlpha(Theme.warning, 0.13)
						: chip.modelData.tone === "success" ? root.withAlpha(Theme.success, 0.13)
						: chip.modelData.tone === "danger" ? root.withAlpha(Theme.danger, 0.13)
						: Theme.panel
					border.color: chip.modelData.tone === "warning" ? root.withAlpha(Theme.warning, 0.5)
						: chip.modelData.tone === "success" ? root.withAlpha(Theme.success, 0.5)
						: chip.modelData.tone === "danger" ? root.withAlpha(Theme.danger, 0.5)
						: chip.modelData.tone === "accent" ? root.withAlpha(Theme.accent, 0.5)
						: Theme.surfaceBorder
					Accessible.role: Accessible.StaticText
					Accessible.name: chip.modelData.text

					Label {
						id: chipLabel
						objectName: "providerChipLabel_" + chip.index
						anchors.fill: parent
						anchors.leftMargin: Theme.space2
						anchors.rightMargin: Theme.space2
						text: chip.modelData.text
						textFormat: Text.PlainText
						color: chip.modelData.tone === "accent" ? Theme.accent
							: chip.modelData.tone === "danger" ? Theme.danger : Theme.textMain
						font.pixelSize: Theme.fontCaption
						font.bold: chip.modelData.tone !== "normal"
						elide: Text.ElideRight
						verticalAlignment: Text.AlignVCenter
					}
				}
			}
		}

		Rectangle {
			id: releaseCard
			objectName: "providerGitHubRelease"
			Layout.fillWidth: true
			Layout.preferredHeight: visible ? releaseLayout.implicitHeight + Theme.space3 * 2 : 0
			visible: root.releaseInfo.hasSummary
			radius: Theme.innerRadius
			color: Theme.panel
			border.color: Theme.surfaceBorder
			Accessible.role: Accessible.Grouping
			Accessible.name: qsTr("Latest release")
			Accessible.description: root.releaseAccessibleDescription()

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
