pragma Singleton

import QtQuick

QtObject {
	// Canonical visual identity for every provider emitted by the native preview
	// pipeline. This stays QML-native so the product shell has no JavaScript
	// resource lane and every card shares one immutable singleton catalog.
	readonly property var catalog: ({
		"direct": { label: "Direct media", mark: "Media", accent: "#5ec8b0", family: "media" },
		"youtube": { label: "YouTube", mark: "YT", accent: "#ff5a5f", family: "media" },
		"spotify": { label: "Spotify", mark: "SP", accent: "#1ed760", family: "audio" },
		"tiktok": { label: "TikTok", mark: "TT", accent: "#25f4ee", family: "social" },
		"instagram": { label: "Instagram", mark: "IG", accent: "#e45aa5", family: "social" },
		"twitch": { label: "Twitch", mark: "TV", accent: "#a970ff", family: "media" },
		"streamable": { label: "Streamable", mark: "ST", accent: "#2c8cff", family: "media" },
		"vimeo": { label: "Vimeo", mark: "V", accent: "#1ab7ea", family: "media" },
		"dailymotion": { label: "Dailymotion", mark: "DM", accent: "#00aaff", family: "media" },
		"soundcloud": { label: "SoundCloud", mark: "SC", accent: "#ff7700", family: "audio" },
		"bandcamp": { label: "Bandcamp", mark: "BC", accent: "#629aa9", family: "audio" },
		"giphy": { label: "GIPHY", mark: "GIF", accent: "#ff66cc", family: "media" },
		"bluesky": { label: "Bluesky", mark: "BS", accent: "#1685ff", family: "social" },
		"mastodon": { label: "Mastodon", mark: "M", accent: "#6364ff", family: "social" },
		"reddit": { label: "Reddit", mark: "R", accent: "#ff4500", family: "social" },
		"x": { label: "X", mark: "X", accent: "#8fa3b8", family: "social" },
		"github": { label: "GitHub", mark: "GH", accent: "#8fa3b8", family: "social" },
		"google": { label: "Google", mark: "G", accent: "#4285f4", family: "search" },
		"googlefinance": { label: "Google Finance", mark: "GF", accent: "#4285f4", family: "finance" },
		"yahoofinance": { label: "Yahoo Finance", mark: "YF", accent: "#78d6a3", family: "finance" },
		"avanza": { label: "Avanza", mark: "A", accent: "#58b978", family: "finance" },
		"nordnet": { label: "Nordnet", mark: "N", accent: "#31a4c7", family: "finance" },
		"interactivebrokers": { label: "Interactive Brokers", mark: "IBKR", accent: "#d94b4b", family: "finance" },
		"tenor": { label: "Tenor", mark: "GIF", accent: "#46b3ff", family: "media" },
		"threads": { label: "Threads", mark: "@", accent: "#8fa3b8", family: "social" },
		"patreon": { label: "Patreon", mark: "P", accent: "#ff695e", family: "social" },
		"facebook": { label: "Facebook", mark: "f", accent: "#5b8def", family: "social" },
		"imgur": { label: "Imgur", mark: "I", accent: "#65d895", family: "media" },
		"4chan": { label: "4chan", mark: "4", accent: "#76a96a", family: "forum" },

		"tradera": { label: "Tradera", mark: "T", accent: "#ffd64c", family: "marketplace" },
		"blocket": { label: "Blocket", mark: "B", accent: "#ff6b61", family: "marketplace" },
		"bytbil": { label: "Bytbil", mark: "BB", accent: "#f4c95d", family: "vehicle" },
		"bilweb": { label: "Bilweb", mark: "BW", accent: "#f4c95d", family: "vehicle" },
		"flashback": { label: "Flashback", mark: "FB", accent: "#f2c36e", family: "forum" },
		"sweclockers": { label: "SweClockers", mark: "SC", accent: "#f08b2c", family: "article" },
		"existenz": { label: "Existenz", mark: "E", accent: "#ff9a54", family: "links" },
		"hemnet": { label: "Hemnet", mark: "H", accent: "#72d7a3", family: "property" },
		"booli": { label: "Booli", mark: "B", accent: "#72d7a3", family: "property" },
		"prisjakt": { label: "Prisjakt", mark: "PJ", accent: "#52a8ff", family: "product" },
		"pricerunner": { label: "PriceRunner", mark: "PR", accent: "#6dc7ff", family: "product" },
		"gp": { label: "GP", mark: "GP", accent: "#62b3ff", family: "article" },
		"svt": { label: "SVT", mark: "SVT", accent: "#ff5a5f", family: "article" },
		"omni": { label: "Omni", mark: "O", accent: "#ffd447", family: "article" },
		"aftonbladet": { label: "Aftonbladet", mark: "AB", accent: "#ff5a5f", family: "article" },
		"expressen": { label: "Expressen", mark: "EX", accent: "#71b8ff", family: "article" },
		"dn": { label: "DN", mark: "DN", accent: "#e7edf3", family: "article" },
		"sverigesradio": { label: "Sveriges Radio", mark: "SR", accent: "#f4a3c1", family: "audio" },
		"inet": { label: "Inet", mark: "I", accent: "#8ad14b", family: "product" },
		"webhallen": { label: "Webhallen", mark: "W", accent: "#ef7a32", family: "product" },
		"elgiganten": { label: "Elgiganten", mark: "E", accent: "#47a5ff", family: "product" },
		"power": { label: "POWER", mark: "P", accent: "#ffd84d", family: "product" },
		"komplett": { label: "Komplett", mark: "K", accent: "#4aa7df", family: "product" },
		"systembolaget": { label: "Systembolaget", mark: "SB", accent: "#85c34a", family: "product" },
		"amazon": { label: "Amazon", mark: "A", accent: "#ffb454", family: "product" },
		"smhi": { label: "SMHI", mark: "SMHI", accent: "#58a9e6", family: "weather" },
		"klart": { label: "Klart", mark: "K", accent: "#63b9f4", family: "weather" },
		"yr": { label: "Yr", mark: "YR", accent: "#4ca5d8", family: "weather" },
		"hitta": { label: "Hitta", mark: "H", accent: "#f25f5c", family: "place" },
		"eniro": { label: "Eniro", mark: "E", accent: "#f3c84b", family: "place" },
		"googlemaps": { label: "Google Maps", mark: "G", accent: "#4285f4", family: "place" },
		"sj": { label: "SJ", mark: "SJ", accent: "#61b34f", family: "traffic" },
		"sl": { label: "SL", mark: "SL", accent: "#2f78c4", family: "traffic" },
		"vasttrafik": { label: "Västtrafik", mark: "VT", accent: "#2484c5", family: "traffic" },

		"steam": { label: "Steam", mark: "S", accent: "#66c0f4", family: "game" },
		"gamestore": { label: "Game store", mark: "Game", accent: "#8fa3b8", family: "game" },
		"g2a": { label: "G2A", mark: "G2A", accent: "#f4a340", family: "game" },
		"kinguin": { label: "Kinguin", mark: "K", accent: "#f4b13f", family: "game" },
		"epic": { label: "Epic Games Store", mark: "E", accent: "#8fa3b8", family: "game" },
		"gog": { label: "GOG", mark: "GOG", accent: "#a779d1", family: "game" },
		"ubisoft": { label: "Ubisoft Store", mark: "U", accent: "#4fb6e9", family: "game" },
		"ea": { label: "EA", mark: "EA", accent: "#ff6550", family: "game" },
		"humble": { label: "Humble Store", mark: "H", accent: "#d94c4c", family: "game" },
		"fanatical": { label: "Fanatical", mark: "F", accent: "#ff6d3a", family: "game" },
		"greenmangaming": { label: "Green Man Gaming", mark: "GMG", accent: "#61bb46", family: "game" },
		"itch": { label: "itch.io", mark: "itch", accent: "#fa5c5c", family: "game" },
		"battlenet": { label: "Battle.net", mark: "B", accent: "#4ea4e0", family: "game" },
		"xbox": { label: "Xbox Store", mark: "X", accent: "#65b34b", family: "game" }
	})

	readonly property var aliases: ({
		"twitter": "x",
		"twitterx": "x",
		"x.com": "x",
		"twitter.com": "x",
		"youtube.com": "youtube",
		"youtube-nocookie.com": "youtube",
		"youtubenocookie.com": "youtube",
		"youtu.be": "youtube",
		"spotify.com": "spotify",
		"spotify.link": "spotify",
		"tiktok.com": "tiktok",
		"instagram.com": "instagram",
		"instagr.am": "instagram",
		"twitch.tv": "twitch",
		"streamable.com": "streamable",
		"vimeo.com": "vimeo",
		"dailymotion.com": "dailymotion",
		"dai.ly": "dailymotion",
		"soundcloud.com": "soundcloud",
		"bandcamp.com": "bandcamp",
		"giphy.com": "giphy",
		"bsky.app": "bluesky",
		"bskyapp": "bluesky",
		"wwwbskyapp": "bluesky",
		"mastodon.social": "mastodon",
		"reddit.com": "reddit",
		"redditcom": "reddit",
		"wwwredditcom": "reddit",
		"oldredditcom": "reddit",
		"redd.it": "reddit",
		"reddit": "reddit",
		"vredditcom": "reddit",
		"github.com": "github",
		"google.com": "google",
		"google.se": "google",
		"finance.google.com": "googlefinance",
		"finance.yahoo.com": "yahoofinance",
		"avanza.se": "avanza",
		"nordnet.se": "nordnet",
		"interactivebrokers.com": "interactivebrokers",
		"googlesearch": "google",
		"googleimages": "google",
		"googlevideos": "google",
		"googlefinance": "googlefinance",
		"yahoofinance": "yahoofinance",
		"yahoofinancecom": "yahoofinance",
		"tenor.com": "tenor",
		"threads.net": "threads",
		"patreon.com": "patreon",
		"facebook.com": "facebook",
		"fb.com": "facebook",
		"fb.watch": "facebook",
		"imgur.com": "imgur",
		"redditmedia.com": "reddit",
		"4chan.org": "4chan",
		"4channel.org": "4chan",
		"4cdn.org": "4chan",
		"tradera.com": "tradera",
		"blocket.se": "blocket",
		"bytbil.com": "bytbil",
		"bilweb.se": "bilweb",
		"flashback.org": "flashback",
		"sweclockers.com": "sweclockers",
		"existenz.se": "existenz",
		"hemnet.se": "hemnet",
		"booli.se": "booli",
		"prisjakt.nu": "prisjakt",
		"prisjakt.se": "prisjakt",
		"pricerunner.se": "pricerunner",
		"pricerunner.com": "pricerunner",
		"gp.se": "gp",
		"svt.se": "svt",
		"omni.se": "omni",
		"aftonbladet.se": "aftonbladet",
		"expressen.se": "expressen",
		"dn.se": "dn",
		"sverigesradio.se": "sverigesradio",
		"sr.se": "sverigesradio",
		"inet.se": "inet",
		"webhallen.com": "webhallen",
		"elgiganten.se": "elgiganten",
		"power.se": "power",
		"komplett.se": "komplett",
		"systembolaget.se": "systembolaget",
		"amazon.se": "amazon",
		"amazon.com": "amazon",
		"amazon.de": "amazon",
		"amazon.co.uk": "amazon",
		"amazon.fr": "amazon",
		"amazon.it": "amazon",
		"amazon.es": "amazon",
		"amazon.nl": "amazon",
		"amazon.pl": "amazon",
		"smhi.se": "smhi",
		"klart.se": "klart",
		"yr.no": "yr",
		"hitta.se": "hitta",
		"eniro.se": "eniro",
		"maps.google.com": "googlemaps",
		"maps.app.goo.gl": "googlemaps",
		"sj.se": "sj",
		"sl.se": "sl",
		"vasttrafik.se": "vasttrafik",
		"steampowered.com": "steam",
		"g2a.com": "g2a",
		"kinguin.net": "kinguin",
		"kinguin.com": "kinguin",
		"epicgames.com": "epic",
		"epic.com": "epic",
		"epicgames": "epic",
		"epicgamesstore": "epic",
		"vsttrafik": "vasttrafik",
		"gog.com": "gog",
		"ubisoft.com": "ubisoft",
		"ubi.com": "ubisoft",
		"ubisoftstore": "ubisoft",
		"ea.com": "ea",
		"humblebundle.com": "humble",
		"humblestore": "humble",
		"fanatical.com": "fanatical",
		"greenmangaming.com": "greenmangaming",
		"itch.io": "itch",
		"battle.net": "battlenet",
		"xbox.com": "xbox",
		"xboxstore": "xbox",
		"itchio": "itch",
		"greenmangamingcom": "greenmangaming"
	})

	function normalizedToken(value) {
		const token = String(value === undefined || value === null ? "" : value)
			.toLowerCase().replace(/[^a-z0-9.]/g, "")
		const compactToken = token.replace(/\./g, "")
		const exact = aliases[token] || aliases[compactToken]
		if (exact)
			return exact
		// Metadata fallbacks often contain a real host such as
		// "open.spotify.com". Resolve by complete DNS-label suffixes so
		// provider identity survives subdomains without accepting lookalikes.
		const labels = token.split(".").filter(function(label) { return label.length > 0 })
		for (let index = 1; index < labels.length; ++index) {
			const suffix = labels.slice(index).join(".")
			const resolved = aliases[suffix] || aliases[suffix.replace(/\./g, "")]
			if (resolved)
				return resolved
		}
		return compactToken
	}

	function resolve(value) {
		const token = normalizedToken(value)
		const item = catalog[token]
		if (!item)
			return { token: token, label: "", mark: "", accent: "", family: "", known: false }
		return { token: token, label: item.label, mark: item.mark, accent: item.accent,
			family: item.family, known: true }
	}

	function displayName(value) {
		const item = resolve(value)
		return item.known ? item.label
			: String(value === undefined || value === null ? "" : value).trim()
	}

	function knownTokens() {
		return Object.keys(catalog).sort()
	}
}
