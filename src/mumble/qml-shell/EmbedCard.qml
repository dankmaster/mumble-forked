import QtQuick
import QtQuick.Effects

// One card for every link preview (docs/embed-cards-redesign.md): one neutral
// frame, a one-line header that names the source once, then the content of the
// card's family. The card is static and cheap to create; the player, the menu
// and the image viewer live outside it and are asked for through its signals.
Rectangle {
	id: root
	objectName: "embedCard"

	property var preview: ({})
	// The view's player covers the media box; the box keeps the player's height.
	property bool playing: false
	property real playerHeight: 0
	property bool animationsEnabled: true
	property bool hoverEffectsEnabled: true
	readonly property alias mediaBox: mediaBox

	signal openRequested(string url)
	// Play the media at an index in the view's player.
	signal playRequested(int mediaIndex)
	signal imageOpenRequested(string source, string title)
	signal imageRefreshRequested()
	signal menuRequested(Item anchor)
	// The detail sheet: every picture, the whole text and the provider's view.
	signal detailsRequested(int mediaIndex)

	readonly property var document: preview && preview.document ? preview.document : ({})
	readonly property var providerInfo: document.provider || ({})
	readonly property var content: document.content || ({})
	readonly property var author: content.author || ({})
	readonly property var media: (document.media || []).filter(item => item && item.kind)
	readonly property var facts: document.facts || []
	readonly property var playback: document.playback || ({})
	readonly property var threadItems: document.thread ? (document.thread.items || []) : []
	readonly property string providerId: String(providerInfo.id || "")
	readonly property string providerLabel: String(providerInfo.label || providerInfo.host || preview.host || "")
	readonly property string sourceUrl: safeExternalUrl(document.source ? document.source.url : preview.url)
	readonly property string title: {
		const value = String(content.title || "").trim()
		// Neither the provider's name nor a media file's name says anything new.
		return value === providerLabel
			|| /^[^\s/]+\.(jpe?g|png|gif|webp|avif|bmp|mp4|m4v|webm|mov|mkv|gifv|mp3|m4a|ogg|opus|wav|flac)$/i.test(value)
			? "" : value
	}
	readonly property string description: String(content.description || "").trim()
	// Who made it: the author, or the provider's own line (Reddit's subreddit).
	readonly property string byline: String(author.name || content.subtitle || "").trim()
	// What an audio link is, read from its address.
	readonly property string audioKind: {
		const path = sourceUrl.replace(/^https?:\/\/[^/]+/i, "").toLowerCase()
		if (providerId === "spotify") {
			const kind = (path.match(/^\/(?:intl-[a-z-]+\/)?(track|album|playlist|episode|show|artist)\//) || [])[1]
			return kind === "track" ? qsTr("Song") : kind === "album" ? qsTr("Album")
				: kind === "playlist" ? qsTr("Playlist") : kind === "episode" ? qsTr("Podcast episode")
				: kind === "show" ? qsTr("Podcast") : kind === "artist" ? qsTr("Artist") : ""
		}
		if (providerId === "soundcloud")
			return path.indexOf("/sets/") >= 0 ? qsTr("Playlist") : qsTr("Track")
		return ""
	}
	readonly property string aspect: normalizedAspect(preview ? preview.embedAspect : "")
	readonly property var firstMedia: media.length > 0 ? media[0] : ({})
	readonly property string imageSource: stillSource(firstMedia)
		|| managedImage(preview ? preview.thumbnailUrl : "")
	readonly property string family: familyFor()
	readonly property bool spotifyStyle: family === "audio" && providerId === "spotify"
	// A store's or gallery's lead picture is a picture: only a video in front plays.
	readonly property bool canPlay: String(playback.mode || "none") !== "none"
		&& (family === "video" || family === "audio" || family === "post"
			|| (family === "product" && (firstMedia.kind === "video" || firstMedia.kind === "audio")))
	// Further pictures as a row of thumbnails under the lead one.
	// A provider's player (SoundCloud, Spotify, YouTube, a Facebook post) shows the
	// cover, title and author itself: while it plays, the card is its header and
	// the player. Native players (a Steam trailer, a file) keep the card's text.
	readonly property bool playerShowsContent: playing && String(playback.mode || "") === "provider"
	readonly property bool showStrip: (family === "product" || family === "image") && media.length > 1
	readonly property int stripCount: Math.min(media.length, 5)
	readonly property real stripThumbWidth: (contentWidth - Theme.space1 * (stripCount - 1)) / stripCount
	readonly property real stripHeight: showStrip ? Math.round(Math.min(72, stripThumbWidth * 0.62)) : 0
	// More than the card shows: further pictures, a whole post or store page, or a
	// provider with its own view.
	readonly property bool hasDetails: media.length > 1 || family === "post" || family === "product"
		|| ["steam", "flashback", "github", "twitch", "amazon", "blocket", "google", "stonks"].indexOf(providerId) >= 0
	readonly property real contentWidth: Math.max(0, width - padding * 2)
	readonly property int padding: Theme.space3

	implicitHeight: column.implicitHeight + padding * 2
	radius: Theme.innerRadius
	color: hoverArea.containsMouse && hoverEffectsEnabled ? Theme.previewCardHover : Theme.previewCardBackground
	border.color: Theme.previewCardBorder
	border.width: 1
	Accessible.role: Accessible.Link
	Accessible.name: [providerLabel, title || description].filter(part => part.length > 0).join(": ")
	Accessible.onPressAction: root.openRequested(root.sourceUrl)

	function safeExternalUrl(value) {
		const url = String(value || "").trim().slice(0, 2048)
		return /^https?:\/\//i.test(url) ? url : ""
	}
	// Only images the client fetched and checked itself are drawn.
	function managedImage(value) {
		const source = String(value || "").trim()
		return /^(image:\/\/mumble\/|qrc:\/)/i.test(source) ? source : ""
	}
	function stillSource(item) {
		if (!item || !item.kind)
			return ""
		if (item.kind === "image" || item.kind === "animated-image")
			return managedImage(item.url) || managedImage(item.thumbnailUrl)
		return managedImage(item.posterUrl) || managedImage(item.thumbnailUrl)
	}
	function normalizedAspect(value) {
		const token = String(value || "").toLowerCase()
		if (token === "short" || token === "portrait" || token === "9:16")
			return "short"
		if (token === "square" || token === "1:1")
			return "square"
		if (token === "audio" || token === "compact-audio")
			return "compact-audio"
		if (providerId === "tiktok")
			return "short"
		if (providerId === "spotify")
			return "compact-audio"
		return "wide"
	}
	function familyFor() {
		const presentation = String(document.presentation || "")
		const kind = String(firstMedia.kind || "")
		if (presentation === "social" || presentation === "thread")
			return "post"
		if (presentation === "game" || presentation === "marketplace")
			return "product"
		// Amazon walls most fetches; without a picture or a price it is a link.
		if (providerId === "amazon" && (imageSource.length > 0 || facts.length > 0))
			return "product"
		if (kind === "audio" || aspect === "compact-audio" || providerId === "soundcloud")
			return "audio"
		if (kind === "video" || String(playback.mode || "") === "provider")
			return "video"
		if (presentation === "media")
			return "image"
		// A link straight to a picture is the picture.
		if (imageSource.length > 0 && /\.(jpe?g|png|gif|webp|avif|bmp)(\?|#|$)/i.test(sourceUrl))
			return "image"
		if (presentation === "article" && imageSource.length > 0)
			return "article"
		return "link"
	}
	function factText(fact) {
		const value = fact.value
		return typeof value === "number" ? Number(value).toLocaleString(Qt.locale(), "f", 0) : String(value)
	}
	// 1.2K, 312K, 1.2M: a post's counts at a glance.
	function compactCount(value) {
		const number = Number(value)
		if (!isFinite(number))
			return String(value)
		const scaled = (divisor, suffix) => {
			const result = number / divisor
			return (result >= 100 ? Math.round(result) : Math.round(result * 10) / 10) + suffix
		}
		return number >= 1e9 ? scaled(1e9, "B") : number >= 1e6 ? scaled(1e6, "M")
			: number >= 1e3 ? scaled(1e3, "K") : String(Math.round(number))
	}
	// "now", "12m", "3h", "4d", then the date.
	function relativeTime(value) {
		const time = Date.parse(String(value || ""))
		if (!isFinite(time))
			return ""
		const seconds = Math.max(0, (Date.now() - time) / 1000)
		if (seconds < 60)
			return qsTr("now")
		if (seconds < 3600)
			return qsTr("%1m").arg(Math.floor(seconds / 60))
		if (seconds < 86400)
			return qsTr("%1h").arg(Math.floor(seconds / 3600))
		if (seconds < 7 * 86400)
			return qsTr("%1d").arg(Math.floor(seconds / 86400))
		return new Date(time).toLocaleDateString(Qt.locale(), Locale.ShortFormat)
	}
	function initials(name) {
		const words = String(name || "").replace(/^@/, "").trim().split(/\s+/).filter(word => word.length > 0)
		return words.slice(0, 2).map(word => word[0].toUpperCase()).join("") || "?"
	}
	// The media box for a width: a fixed aspect, so the card's height follows its
	// width and never its picture.
	function mediaHeightFor(width) {
		if (family === "audio")
			return 0
		if (family === "post" && media.length > 1)
			return Math.min(360, Math.round(width * 9 / 16))
		// A video without a picture: a short placeholder, not a large black box.
		if (family === "video" && imageSource.length === 0)
			return Math.min(200, Math.round(width * 9 / 16))
		if (aspect === "short")
			return Math.min(420, Math.round(width * 1.25))
		if (aspect === "square")
			return Math.min(420, width)
		return Math.min(360, Math.round(width * 9 / 16))
	}

	MouseArea {
		id: hoverArea
		anchors.fill: parent
		hoverEnabled: root.hoverEffectsEnabled
		cursorShape: root.hasDetails || root.sourceUrl.length > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor
		onClicked: {
			if (root.hasDetails)
				root.detailsRequested(0)
			else if (root.sourceUrl.length > 0)
				root.openRequested(root.sourceUrl)
		}
	}

	Column {
		id: column
		x: root.padding
		y: root.padding
		width: root.contentWidth
		spacing: Theme.space2

		// Header: who it comes from, once.
		Item {
			width: parent.width
			height: Math.max(headerText.implicitHeight, 20)

			Text {
				id: headerText
				objectName: "embedCardHeader"
				anchors.left: parent.left
				anchors.right: openButton.left
				anchors.rightMargin: Theme.space2
				anchors.verticalCenter: parent.verticalCenter
				text: {
					const parts = [root.providerLabel]
					const name = root.byline
					if (root.family !== "post" && root.family !== "audio" && name.length > 0
							&& name !== root.providerLabel)
						parts.push(name)
					return parts.filter(part => part.length > 0).join("  ·  ")
				}
				textFormat: Text.PlainText
				elide: Text.ElideRight
				maximumLineCount: 1
				color: Theme.textMuted
				font.pixelSize: Theme.fontCaption
				font.weight: Font.DemiBold
			}

			// The source's name opens it, like the button beside it.
			MouseArea {
				anchors.left: headerText.left
				anchors.verticalCenter: headerText.verticalCenter
				width: Math.min(headerText.implicitWidth, headerText.width)
				height: headerText.height
				enabled: root.sourceUrl.length > 0
				hoverEnabled: true
				cursorShape: Qt.PointingHandCursor
				onClicked: root.openRequested(root.sourceUrl)
			}
			// Open in the browser: always there, not only on hover, since a card's
			// picture or player takes clicks for itself.
			Item {
				id: openButton
				objectName: "embedCardOpenButton"
				visible: root.sourceUrl.length > 0
				anchors.right: menuButton.left
				anchors.rightMargin: 2
				anchors.verticalCenter: parent.verticalCenter
				width: visible ? 24 : 0
				height: 20
				opacity: hoverArea.containsMouse || openArea.containsMouse || menuArea.containsMouse ? 1 : 0.55
				Accessible.role: Accessible.Button
				Accessible.name: qsTr("Open in browser")
				Accessible.onPressAction: root.openRequested(root.sourceUrl)

				ModernIcon {
					anchors.centerIn: parent
					name: "external"
					size: 15
					color: openArea.containsMouse ? Theme.textStrong : Theme.textMuted
				}
				MouseArea {
					id: openArea
					anchors.fill: parent
					hoverEnabled: true
					cursorShape: Qt.PointingHandCursor
					onClicked: root.openRequested(root.sourceUrl)
				}
			}

			Item {
				id: menuButton
				objectName: "embedCardMenuButton"
				anchors.right: parent.right
				anchors.verticalCenter: parent.verticalCenter
				width: 24
				height: 20
				opacity: hoverArea.containsMouse || menuArea.containsMouse ? 1 : 0
				Accessible.role: Accessible.Button
				Accessible.name: qsTr("Link actions")
				Accessible.onPressAction: root.menuRequested(menuButton)

				ModernIcon {
					anchors.centerIn: parent
					name: "more"
					size: 16
					color: menuArea.containsMouse ? Theme.textStrong : Theme.textMuted
				}
				MouseArea {
					id: menuArea
					anchors.fill: parent
					hoverEnabled: true
					cursorShape: Qt.PointingHandCursor
					onClicked: root.menuRequested(menuButton)
				}
			}
		}

		// Post: who wrote it, then what they wrote.
		Row {
			visible: root.family === "post" && !root.playerShowsContent
			width: parent.width
			spacing: Theme.space2

			Rectangle {
				width: 36
				height: 36
				radius: 18
				color: Theme.surfaceHover
				clip: true

				Text {
					anchors.centerIn: parent
					visible: avatar.status !== Image.Ready
					text: root.initials(root.author.name || root.author.handle)
					textFormat: Text.PlainText
					color: Theme.textMain
					font.pixelSize: Theme.fontLabel
					font.weight: Font.DemiBold
				}
				Image {
					id: avatar
					anchors.fill: parent
					source: root.family === "post" ? root.managedImage(root.author.avatarUrl) : ""
					sourceSize: Qt.size(72, 72)
					fillMode: Image.PreserveAspectCrop
					asynchronous: true
				}
			}

			Column {
				width: parent.width - 36 - Theme.space2
				anchors.verticalCenter: parent.verticalCenter

				Row {
					width: parent.width
					spacing: Theme.space1

					Text {
						id: authorName
						width: Math.min(implicitWidth, parent.width - (verifiedMark.visible ? verifiedMark.width + Theme.space1 : 0))
						text: String(root.author.name || root.providerLabel)
						textFormat: Text.PlainText
						elide: Text.ElideRight
						maximumLineCount: 1
						color: Theme.textStrong
						font.pixelSize: Theme.fontBody
						font.weight: Font.DemiBold
					}
					Rectangle {
						id: verifiedMark
						objectName: "embedCardVerified"
						visible: root.author.verified === true
						anchors.verticalCenter: authorName.verticalCenter
						width: 14
						height: 14
						radius: 7
						color: "#1d9bf0"
						Accessible.name: qsTr("Verified")

						Text {
							anchors.centerIn: parent
							text: "\u2713"
							color: "#ffffff"
							font.pixelSize: 10
							font.weight: Font.Bold
						}
					}
				}
				Text {
					objectName: "embedCardPostHandle"
					width: parent.width
					visible: text.length > 0
					text: [String(root.author.handle || ""), root.relativeTime(root.content.publishedAt)]
						.filter(part => part.length > 0).join("  ·  ")
					textFormat: Text.PlainText
					elide: Text.ElideRight
					maximumLineCount: 1
					color: Theme.textMuted
					font.pixelSize: Theme.fontCaption
				}
			}
		}

		// Reply context or quoted post: one quiet inset line each.
		Repeater {
			model: root.family === "post" && !root.playerShowsContent ? root.threadItems.slice(0, 2) : []

			Row {
				required property var modelData
				width: column.width
				spacing: Theme.space2

				Rectangle {
					width: 2
					height: quoteText.implicitHeight
					color: Theme.surfaceBorder
				}
				Text {
					id: quoteText
					width: parent.width - 2 - Theme.space2
					text: (modelData.role === "reply-context" ? qsTr("Replying to %1").arg(modelData.authorHandle
						|| modelData.authorName || "") : String(modelData.authorName || modelData.authorHandle || ""))
						+ (modelData.text ? ": " + modelData.text : "")
					textFormat: Text.PlainText
					wrapMode: Text.Wrap
					elide: Text.ElideRight
					maximumLineCount: 2
					color: Theme.textMuted
					font.pixelSize: Theme.fontCaption
				}
			}
		}

		// A post's own words, before its pictures.
		Text {
			objectName: "embedCardPostText"
			width: parent.width
			visible: root.family === "post" && !root.playerShowsContent && text.length > 0
			text: root.family === "post" ? root.description : ""
			textFormat: Text.PlainText
			wrapMode: Text.Wrap
			elide: Text.ElideRight
			maximumLineCount: 8
			color: Theme.textMain
			font.pixelSize: Theme.fontBody
			lineHeight: 1.08
		}

		// Link: the text beside a small picture.
		Row {
			visible: root.family === "link"
			width: parent.width
			spacing: Theme.space3

			Column {
				width: parent.width - (linkThumb.visible ? linkThumb.width + Theme.space3 : 0)
				spacing: 2

				Text {
					width: parent.width
					visible: text.length > 0
					text: root.title
					textFormat: Text.PlainText
					wrapMode: Text.Wrap
					elide: Text.ElideRight
					maximumLineCount: 2
					color: Theme.textStrong
					font.pixelSize: Theme.fontBody
					font.weight: Font.DemiBold
				}
				Text {
					width: parent.width
					visible: text.length > 0
					text: root.description
					textFormat: Text.PlainText
					wrapMode: Text.Wrap
					elide: Text.ElideRight
					maximumLineCount: 2
					color: Theme.textMuted
					font.pixelSize: Theme.fontLabel
				}
			}
			Rectangle {
				id: linkThumb
				visible: root.family === "link" && root.imageSource.length > 0
				width: 80
				height: 80
				radius: Theme.innerRadius - 4
				color: Theme.mediaCanvas
				clip: true

				Image {
					anchors.fill: parent
					source: linkThumb.visible ? root.imageSource : ""
					sourceSize: Qt.size(160, 160)
					fillMode: Image.PreserveAspectCrop
					asynchronous: true
					onStatusChanged: if (status === Image.Error) root.imageRefreshRequested()
				}
			}
		}

		// Spotify looks like Spotify's own embed until it plays (then it is that
		// embed): the cover's colours behind it, a large cover, light text and a
		// round green play button.
		Rectangle {
			id: spotifyPanel
			objectName: "embedCardSpotify"
			visible: root.spotifyStyle && !root.playerShowsContent
			width: parent.width
			height: visible ? 136 : 0
			radius: Theme.innerRadius - 2
			color: "#282828"
			clip: true

			MultiEffect {
				anchors.fill: parent
				visible: spotifyCover.status === Image.Ready
				source: spotifyCover
				autoPaddingEnabled: false
				blurEnabled: true
				blur: 1.0
				blurMax: 64
				brightness: -0.38
				saturation: 0.25
			}
			Rectangle {
				id: spotifyCoverFrame
				x: 12
				y: 12
				width: 112
				height: 112
				radius: 6
				color: "#3e3e3e"
				clip: true

				ModernIcon {
					anchors.centerIn: parent
					visible: spotifyCover.status !== Image.Ready
					name: "volume"
					size: 32
					color: "#b3b3b3"
				}
				Image {
					id: spotifyCover
					anchors.fill: parent
					source: root.spotifyStyle ? root.imageSource : ""
					sourceSize: Qt.size(224, 224)
					fillMode: Image.PreserveAspectCrop
					asynchronous: true
				}
			}
			Column {
				anchors.left: spotifyCoverFrame.right
				anchors.leftMargin: 14
				anchors.right: parent.right
				anchors.rightMargin: 12
				y: 14
				spacing: 3

				Text {
					width: parent.width
					visible: text.length > 0
					text: root.audioKind
					textFormat: Text.PlainText
					elide: Text.ElideRight
					color: "#e6ffffff"
					font.pixelSize: Theme.fontCaption
					font.weight: Font.DemiBold
				}
				Text {
					width: parent.width
					text: root.title || root.description
					textFormat: Text.PlainText
					wrapMode: Text.Wrap
					elide: Text.ElideRight
					maximumLineCount: 2
					color: "#ffffff"
					font.pixelSize: Theme.fontTitle + 2
					font.weight: Font.Bold
				}
				Text {
					width: parent.width
					visible: text.length > 0
					text: root.byline
					textFormat: Text.PlainText
					elide: Text.ElideRight
					maximumLineCount: 1
					color: "#b3ffffff"
					font.pixelSize: Theme.fontLabel
				}
			}
			Rectangle {
				objectName: "embedCardSpotifyPlay"
				visible: root.canPlay && !root.playing
				anchors.right: parent.right
				anchors.bottom: parent.bottom
				anchors.margins: 12
				width: 44
				height: 44
				radius: 22
				color: spotifyPlayArea.containsMouse ? "#3be477" : "#1ed760"
				scale: spotifyPlayArea.containsMouse ? 1.05 : 1
				Accessible.role: Accessible.Button
				Accessible.name: qsTr("Play")
				Accessible.onPressAction: root.playRequested(0)

				ModernIcon {
					anchors.centerIn: parent
					anchors.horizontalCenterOffset: 1
					name: "play"
					size: 18
					color: "#000000"
				}
				MouseArea {
					id: spotifyPlayArea
					anchors.fill: parent
					hoverEnabled: true
					cursorShape: Qt.PointingHandCursor
					onClicked: root.playRequested(0)
				}
			}
		}

		// Audio: cover, what it is, title and artist, and a play button; the
		// player opens below.
		Row {
			objectName: "embedCardAudio"
			visible: root.family === "audio" && !root.spotifyStyle && !root.playerShowsContent
			width: parent.width
			spacing: Theme.space3

			Rectangle {
				width: 96
				height: 96
				radius: Theme.innerRadius - 4
				color: Theme.mediaCanvas
				clip: true

				ModernIcon {
					anchors.centerIn: parent
					visible: audioCover.status !== Image.Ready
					name: "volume"
					size: 28
					color: Theme.textMuted
				}
				Image {
					id: audioCover
					anchors.fill: parent
					source: root.family === "audio" ? root.imageSource : ""
					sourceSize: Qt.size(192, 192)
					fillMode: Image.PreserveAspectCrop
					asynchronous: true
				}
			}
			Column {
				width: parent.width - 96 - Theme.space3
				height: 96
				spacing: 2

				Text {
					width: parent.width
					visible: text.length > 0
					text: root.audioKind
					textFormat: Text.PlainText
					elide: Text.ElideRight
					color: Theme.textMuted
					font.pixelSize: Theme.fontCaption
				}
				Text {
					width: parent.width
					text: root.title || root.description
					textFormat: Text.PlainText
					wrapMode: Text.Wrap
					elide: Text.ElideRight
					maximumLineCount: 2
					color: Theme.textStrong
					font.pixelSize: Theme.fontTitle
					font.weight: Font.DemiBold
				}
				Text {
					width: parent.width
					visible: text.length > 0
					text: root.byline
					textFormat: Text.PlainText
					elide: Text.ElideRight
					maximumLineCount: 1
					color: Theme.textMuted
					font.pixelSize: Theme.fontLabel
				}
			}
		}
		Rectangle {
			objectName: "embedCardAudioPlay"
			visible: root.family === "audio" && !root.spotifyStyle && root.canPlay && !root.playing
			width: audioPlayRow.implicitWidth + Theme.space3 * 2
			height: 30
			radius: 15
			// The page's own color stands out from the raised card in light and
			// dark themes alike.
			color: audioPlayArea.containsMouse ? Theme.surfaceHover : Theme.panel
			border.color: Theme.surfaceBorder
			Accessible.role: Accessible.Button
			Accessible.name: qsTr("Play")
			Accessible.onPressAction: root.playRequested(0)

			Row {
				id: audioPlayRow
				anchors.centerIn: parent
				spacing: Theme.space1

				ModernIcon {
					anchors.verticalCenter: parent.verticalCenter
					name: "play"
					size: 14
					color: Theme.textStrong
				}
				Text {
					anchors.verticalCenter: parent.verticalCenter
					text: qsTr("Play")
					color: Theme.textStrong
					font.pixelSize: Theme.fontLabel
					font.weight: Font.DemiBold
				}
			}
			MouseArea {
				id: audioPlayArea
				anchors.fill: parent
				hoverEnabled: true
				cursorShape: Qt.PointingHandCursor
				onClicked: root.playRequested(0)
			}
		}

		// The media box: a fixed aspect for the card's width, the player's height
		// while the view's player covers it.
		Rectangle {
			id: mediaBox
			objectName: "embedCardMediaBox"
			readonly property bool shown: root.family !== "link"
				&& (root.family === "audio" ? root.playing
					: root.imageSource.length > 0 || root.canPlay || root.media.length > 0)
			visible: shown
			width: parent.width
			height: !shown ? 0 : root.playing && root.playerHeight > 0 ? root.playerHeight
				: root.mediaHeightFor(width)
			radius: Theme.innerRadius - 4
			color: Theme.mediaCanvas
			clip: true

			// A picture of another shape is fitted over a blurred, dimmed copy of
			// itself, scaled to cover the box. The blur renders once per picture.
			MultiEffect {
				id: backdrop
				anchors.fill: parent
				// A portrait player keeps it at its sides instead of flat black bars.
				visible: (still.visible || root.playing && root.aspect === "short")
					&& still.fillMode === Image.PreserveAspectFit && still.status === Image.Ready
				source: still
				scale: still.paintedWidth > 0 && still.paintedHeight > 0
					? Math.max(width / still.paintedWidth, height / still.paintedHeight) : 1
				autoPaddingEnabled: false
				blurEnabled: true
				blur: 1.0
				blurMax: 48
				brightness: -0.2
				saturation: -0.1
				opacity: 0.55
			}
			Image {
				id: still
				objectName: "embedCardMediaImage"
				anchors.fill: parent
				visible: !root.playing && (root.family !== "post" || root.media.length <= 1)
				source: mediaBox.shown && (!root.playing || root.aspect === "short")
					&& (root.family !== "post" || root.media.length <= 1)
					&& !(firstAnimated.active) ? root.imageSource : ""
				sourceSize: Qt.size(1280, 960)
				fillMode: root.aspect === "short" || root.family === "image" ? Image.PreserveAspectFit
					: Image.PreserveAspectCrop
				asynchronous: true
				onStatusChanged: if (status === Image.Error) root.imageRefreshRequested()
			}
			Loader {
				id: firstAnimated
				anchors.fill: parent
				active: mediaBox.shown && !root.playing && root.firstMedia.kind === "animated-image"
					&& /^file:\/\//i.test(String(root.firstMedia.url || ""))
				sourceComponent: AnimatedImage {
					source: root.firstMedia.url
					fillMode: Image.PreserveAspectFit
					asynchronous: true
					cache: false
					playing: root.animationsEnabled && status === Image.Ready
				}
			}

			// A post's pictures: two side by side, three as one large beside two
			// stacked, four in a square.
			Item {
				id: postGrid
				visible: root.family === "post" && root.media.length > 1 && !root.playing
				anchors.fill: parent
				readonly property int count: Math.min(root.media.length, 4)
				readonly property real halfWidth: (width - Theme.space1) / 2
				readonly property real halfHeight: (height - Theme.space1) / 2

				Repeater {
					model: postGrid.visible ? root.media.slice(0, 4) : []

					Rectangle {
						required property var modelData
						required property int index
						readonly property bool fullHeight: postGrid.count === 2 || (postGrid.count === 3 && index === 0)
						x: index === 0 || (postGrid.count === 4 && index === 2) ? 0 : postGrid.halfWidth + Theme.space1
						y: fullHeight || (postGrid.count === 3 && index === 1) || (postGrid.count === 4 && index < 2)
							? 0 : postGrid.halfHeight + Theme.space1
						width: postGrid.halfWidth
						height: fullHeight ? postGrid.height : postGrid.halfHeight
						color: Theme.strip
						clip: true

						Image {
							anchors.fill: parent
							source: root.stillSource(modelData)
							sourceSize: Qt.size(640, 480)
							fillMode: Image.PreserveAspectCrop
							asynchronous: true
						}
						MouseArea {
							anchors.fill: parent
							cursorShape: Qt.PointingHandCursor
							onClicked: root.detailsRequested(index)
						}
						Rectangle {
							anchors.fill: parent
							visible: index === 3 && root.media.length > 4
							color: Theme.withAlpha(Theme.mediaCanvas, 0.55)

							Text {
								anchors.centerIn: parent
								text: "+" + (root.media.length - 4)
								color: Theme.mediaOverlayTextStrong
								font.pixelSize: Theme.fontTitle
								font.weight: Font.DemiBold
							}
						}
					}
				}
			}

			// More pictures behind this one.
			Rectangle {
				anchors.top: parent.top
				anchors.right: parent.right
				anchors.margins: Theme.space2
				objectName: "embedCardPictureCount"
				z: 2
				visible: root.family !== "post" && !root.showStrip && root.media.length > 1 && !root.playing
				width: countText.implicitWidth + Theme.space2 * 2
				height: countText.implicitHeight + Theme.space1
				radius: height / 2
				color: countArea.containsMouse ? Theme.withAlpha(Theme.mediaCanvas, 0.85)
					: Theme.withAlpha(Theme.mediaCanvas, 0.66)

				MouseArea {
					id: countArea
					anchors.fill: parent
					anchors.margins: -Theme.space1
					hoverEnabled: true
					cursorShape: Qt.PointingHandCursor
					onClicked: root.detailsRequested(0)
				}

				Text {
					id: countText
					anchors.centerIn: parent
					text: qsTr("%1 pictures").arg(root.media.length)
					color: Theme.mediaOverlayTextStrong
					font.pixelSize: Theme.fontCaption
				}
			}

			MouseArea {
				anchors.fill: parent
				enabled: !root.playing
				cursorShape: Qt.PointingHandCursor
				onClicked: {
					if (root.canPlay)
						root.playRequested(0)
					else if (root.hasDetails)
						root.detailsRequested(0)
					else if (root.imageSource.length > 0)
						root.imageOpenRequested(root.imageSource, root.title || root.description)
					else if (root.sourceUrl.length > 0)
						root.openRequested(root.sourceUrl)
				}
			}

			Text {
				objectName: "embedCardNoPicture"
				anchors.horizontalCenter: parent.horizontalCenter
				anchors.top: parent.verticalCenter
				anchors.topMargin: 36
				visible: root.family === "video" && root.imageSource.length === 0 && !root.playing
				text: qsTr("Video")
				color: Theme.mediaOverlayTextMuted
				font.pixelSize: Theme.fontCaption
			}
			Rectangle {
				objectName: "embedCardPlay"
				anchors.centerIn: parent
				visible: root.canPlay && !root.playing && root.family !== "audio"
				width: 56
				height: 56
				radius: 28
				color: Theme.withAlpha(Theme.mediaCanvas, 0.66)
				border.color: Theme.withAlpha(Theme.mediaOverlayTextStrong, 0.24)

				ModernIcon {
					anchors.centerIn: parent
					anchors.horizontalCenterOffset: 2
					name: "play"
					size: 24
					color: Theme.mediaOverlayTextStrong
				}
			}
		}

		// The card's other pictures; a video plays in the card, a picture opens
		// the detail sheet at it.
		Row {
			objectName: "embedCardStrip"
			visible: root.showStrip
			width: parent.width
			height: root.stripHeight
			spacing: Theme.space1

			Repeater {
				model: root.showStrip ? root.media.slice(0, root.stripCount) : []

				Rectangle {
					required property var modelData
					required property int index
					readonly property bool playable: (modelData.kind === "video" || modelData.kind === "audio")
						&& !!modelData.directPlayable
					readonly property bool more: index === root.stripCount - 1 && root.media.length > root.stripCount
					objectName: "embedCardStripItem" + index
					width: root.stripThumbWidth
					height: root.stripHeight
					radius: Theme.innerRadius - 6
					color: Theme.mediaCanvas
					border.color: thumbArea.containsMouse ? Theme.focus : "transparent"
					border.width: 2
					clip: true

					Image {
						anchors.fill: parent
						anchors.margins: 2
						source: root.managedImage(modelData.thumbnailUrl) || root.stillSource(modelData)
						sourceSize: Qt.size(240, 150)
						fillMode: Image.PreserveAspectCrop
						asynchronous: true
					}
					Rectangle {
						anchors.centerIn: parent
						visible: parent.playable && !parent.more
						width: 26
						height: 26
						radius: 13
						color: Theme.withAlpha(Theme.mediaCanvas, 0.66)

						ModernIcon {
							anchors.centerIn: parent
							anchors.horizontalCenterOffset: 1
							name: "play"
							size: 14
							color: Theme.mediaOverlayTextStrong
						}
					}
					Rectangle {
						anchors.fill: parent
						visible: parent.more
						color: Theme.withAlpha(Theme.mediaCanvas, 0.6)

						Text {
							anchors.centerIn: parent
							text: "+" + (root.media.length - root.stripCount + 1)
							color: Theme.mediaOverlayTextStrong
							font.pixelSize: Theme.fontBody
							font.weight: Font.DemiBold
						}
					}
					MouseArea {
						id: thumbArea
						anchors.fill: parent
						hoverEnabled: root.hoverEffectsEnabled
						cursorShape: Qt.PointingHandCursor
						Accessible.role: Accessible.Button
						Accessible.name: parent.playable ? qsTr("Play video %1").arg(index + 1)
							: qsTr("Show picture %1").arg(index + 1)
						onClicked: {
							if (parent.playable && !parent.more)
								root.playRequested(index)
							else
								root.detailsRequested(index)
						}
					}
				}
			}
		}

		// Title and text under the media.
		Text {
			width: parent.width
			visible: root.family !== "link" && root.family !== "audio" && !root.playerShowsContent
				&& text.length > 0
			text: root.family === "post" ? "" : root.title
			textFormat: Text.PlainText
			wrapMode: Text.Wrap
			elide: Text.ElideRight
			maximumLineCount: 2
			color: Theme.textStrong
			font.pixelSize: Theme.fontBody
			font.weight: Font.DemiBold
		}
		Text {
			width: parent.width
			visible: root.family !== "link" && root.family !== "audio" && root.family !== "video"
				&& root.family !== "post" && !root.playerShowsContent && text.length > 0
			text: root.description
			textFormat: Text.PlainText
			wrapMode: Text.Wrap
			elide: Text.ElideRight
			maximumLineCount: 3
			color: Theme.textMuted
			font.pixelSize: Theme.fontLabel
		}

		// Facts: one quiet line.
		Text {
			objectName: "embedCardFacts"
			width: parent.width
			visible: text.length > 0 && !root.playerShowsContent
			text: root.facts.slice(0, 4).map(fact => fact.key === "price" ? root.factText(fact)
				: root.family === "post" && typeof fact.value === "number"
					? root.compactCount(fact.value) + " " + String(fact.label || "").toLowerCase()
				: String(fact.label || "") + " " + root.factText(fact)).join("  ·  ")
			textFormat: Text.PlainText
			elide: Text.ElideRight
			maximumLineCount: 1
			color: Theme.textMuted
			font.pixelSize: Theme.fontCaption
		}
	}
}
