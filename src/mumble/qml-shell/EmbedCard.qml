import QtQuick

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
	signal playRequested()
	signal imageOpenRequested(string source, string title)
	signal imageRefreshRequested()
	signal menuRequested(Item anchor)

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
		return value === providerLabel ? "" : value
	}
	readonly property string description: String(content.description || "").trim()
	readonly property string aspect: normalizedAspect(preview ? preview.embedAspect : "")
	readonly property var firstMedia: media.length > 0 ? media[0] : ({})
	readonly property string imageSource: stillSource(firstMedia)
		|| managedImage(preview ? preview.thumbnailUrl : "")
	readonly property string family: familyFor()
	readonly property bool canPlay: String(playback.mode || "none") !== "none"
		&& (family === "video" || family === "audio" || family === "post" || family === "product")
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
		if (presentation === "game" || presentation === "marketplace" || providerId === "amazon")
			return "product"
		if (kind === "audio" || aspect === "compact-audio" || providerId === "soundcloud")
			return "audio"
		if (kind === "video" || String(playback.mode || "") === "provider")
			return "video"
		if (presentation === "media")
			return "image"
		if (presentation === "article" && imageSource.length > 0)
			return "article"
		return "link"
	}
	function factText(fact) {
		const value = fact.value
		return typeof value === "number" ? Number(value).toLocaleString(Qt.locale(), "f", 0) : String(value)
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
		cursorShape: root.sourceUrl.length > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor
		onClicked: if (root.sourceUrl.length > 0) root.openRequested(root.sourceUrl)
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
				anchors.right: menuButton.left
				anchors.rightMargin: Theme.space2
				anchors.verticalCenter: parent.verticalCenter
				text: {
					const parts = [root.providerLabel]
					const name = String(root.author.name || "")
					if (root.family !== "post" && name.length > 0 && name !== root.providerLabel)
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
			visible: root.family === "post"
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

				Text {
					width: parent.width
					text: String(root.author.name || root.providerLabel)
					textFormat: Text.PlainText
					elide: Text.ElideRight
					maximumLineCount: 1
					color: Theme.textStrong
					font.pixelSize: Theme.fontBody
					font.weight: Font.DemiBold
				}
				Text {
					width: parent.width
					visible: text.length > 0
					text: String(root.author.handle || "")
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
			model: root.family === "post" ? root.threadItems.slice(0, 2) : []

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

		// Audio: art, title and artist on one row; the player opens below.
		Row {
			visible: root.family === "audio"
			width: parent.width
			spacing: Theme.space3

			Rectangle {
				width: 72
				height: 72
				radius: Theme.innerRadius - 4
				color: Theme.mediaCanvas
				clip: true

				Image {
					anchors.fill: parent
					source: root.family === "audio" ? root.imageSource : ""
					sourceSize: Qt.size(144, 144)
					fillMode: Image.PreserveAspectCrop
					asynchronous: true
				}
				Rectangle {
					objectName: "embedCardAudioPlay"
					anchors.centerIn: parent
					width: 36
					height: 36
					radius: 18
					visible: root.canPlay && !root.playing
					color: Theme.withAlpha(Theme.mediaCanvas, 0.66)

					ModernIcon {
						anchors.centerIn: parent
						name: "play"
						size: 18
						color: Theme.mediaOverlayTextStrong
					}
					MouseArea {
						anchors.fill: parent
						cursorShape: Qt.PointingHandCursor
						onClicked: root.playRequested()
					}
				}
			}
			Column {
				width: parent.width - 72 - Theme.space3
				anchors.verticalCenter: parent.verticalCenter
				spacing: 2

				Text {
					width: parent.width
					text: root.title || root.description
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
					text: String(root.author.name || "")
					textFormat: Text.PlainText
					elide: Text.ElideRight
					maximumLineCount: 1
					color: Theme.textMuted
					font.pixelSize: Theme.fontLabel
				}
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

			// A picture of another shape is fitted over a softened copy of itself.
			Image {
				id: backdrop
				anchors.fill: parent
				visible: still.visible && still.fillMode === Image.PreserveAspectFit && still.status === Image.Ready
				source: visible ? still.source : ""
				sourceSize: still.sourceSize
				fillMode: Image.PreserveAspectFit
				scale: paintedWidth > 0 && paintedHeight > 0
					? Math.max(width / paintedWidth, height / paintedHeight) : 1
				asynchronous: true
				opacity: 0.45
				layer.enabled: visible
				layer.textureSize: Qt.size(48, 36)
				layer.smooth: true
			}
			Image {
				id: still
				objectName: "embedCardMediaImage"
				anchors.fill: parent
				visible: !root.playing && (root.family !== "post" || root.media.length <= 1)
				source: mediaBox.shown && !root.playing && (root.family !== "post" || root.media.length <= 1)
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
				visible: root.family !== "post" && root.media.length > 1 && !root.playing
				width: countText.implicitWidth + Theme.space2 * 2
				height: countText.implicitHeight + Theme.space1
				radius: height / 2
				color: Theme.withAlpha(Theme.mediaCanvas, 0.66)

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
						root.playRequested()
					else if (root.imageSource.length > 0)
						root.imageOpenRequested(root.imageSource, root.title || root.description)
					else if (root.sourceUrl.length > 0)
						root.openRequested(root.sourceUrl)
				}
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

		// Title and text under the media.
		Text {
			width: parent.width
			visible: root.family !== "link" && root.family !== "audio" && text.length > 0
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
				&& text.length > 0
			text: root.description
			textFormat: Text.PlainText
			wrapMode: Text.Wrap
			elide: Text.ElideRight
			maximumLineCount: root.family === "post" ? 6 : 3
			color: root.family === "post" ? Theme.textMain : Theme.textMuted
			font.pixelSize: root.family === "post" ? Theme.fontBody : Theme.fontLabel
		}

		// Facts: one quiet line.
		Text {
			objectName: "embedCardFacts"
			width: parent.width
			visible: text.length > 0
			text: root.facts.slice(0, 4).map(fact => fact.key === "price"
				? root.factText(fact) : String(fact.label || "") + " " + root.factText(fact)).join("  ·  ")
			textFormat: Text.PlainText
			elide: Text.ElideRight
			maximumLineCount: 1
			color: Theme.textMuted
			font.pixelSize: Theme.fontCaption
		}
	}
}
