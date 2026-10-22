import QtQuick
import QtQuick.Controls
import Mumble.Theme 1.0

// The detail surface behind a link card (docs/embed-cards-redesign.md): every
// picture of the card to browse, the whole text, every fact, and the provider's
// rich view (Steam store page, Flashback thread, GitHub repository...). Videos
// play in the card, where the conversation's player is, so the sheet closes.
Popup {
	id: sheet
	objectName: "embedDetailSheet"

	property var preview: ({})
	property string stableId: ""
	property int mediaIndex: 0

	signal openRequested(string url)
	signal playRequested(string stableId, int mediaIndex)
	signal imageOpenRequested(string source, string title, string stableId)

	readonly property var document: preview && preview.document ? preview.document : ({})
	readonly property var providerInfo: document.provider || ({})
	readonly property var content: document.content || ({})
	readonly property var author: content.author || ({})
	readonly property var media: (document.media || []).filter(item => item && item.kind)
	readonly property var facts: document.facts || []
	readonly property var playback: document.playback || ({})
	readonly property var threadItems: document.thread ? (document.thread.items || []) : []
	readonly property string providerLabel: String(providerInfo.label || providerInfo.host || preview.host || "")
	readonly property string sourceUrl: {
		const url = String(document.source ? document.source.url : (preview.url || "")).trim()
		return /^https?:\/\//i.test(url) ? url : ""
	}
	readonly property string title: {
		const value = String(content.title || "").trim()
		return value === providerLabel ? "" : value
	}
	readonly property string description: String(content.description || "").trim()
	readonly property bool post: ["social", "thread"].indexOf(String(document.presentation || "")) >= 0
	readonly property var current: media.length > 0 ? media[Math.max(0, Math.min(mediaIndex, media.length - 1))] : ({})
	// The provider's own view covers the facts (and for stores, the text).
	readonly property bool richShown: richLoader.status === Loader.Ready && !!richLoader.item
		&& richLoader.item.richView
	readonly property bool currentPlayable: !!current.kind && (current.kind === "video" || current.kind === "audio")
		&& (!!current.directPlayable || String(playback.mode || "") === "provider")

	// Opens the sheet on a card's preview at one of its pictures.
	function show(previewValue, id, index) {
		preview = previewValue || ({})
		stableId = String(id || "")
		mediaIndex = Math.max(0, Number(index) || 0)
		open()
	}
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
	function thumbSource(item) {
		if (!item || !item.kind)
			return ""
		return managedImage(item.thumbnailUrl) || stillSource(item)
	}
	function step(delta) {
		if (media.length > 1)
			mediaIndex = (mediaIndex + delta + media.length) % media.length
	}
	function factText(fact) {
		const value = fact.value
		return typeof value === "number" ? Number(value).toLocaleString(Qt.locale(), "f", 0) : String(value)
	}
	function initials(name) {
		const words = String(name || "").replace(/^@/, "").trim().split(/\s+/).filter(word => word.length > 0)
		return words.slice(0, 2).map(word => word[0].toUpperCase()).join("") || "?"
	}

	parent: Overlay.overlay
	x: Math.round((parent.width - width) / 2)
	y: Math.round((parent.height - height) / 2)
	width: Math.min(parent.width - Theme.space6 * 2, 760)
	height: Math.min(parent.height - Theme.space6 * 2, header.height + body.contentHeight + Theme.space4 * 2)
	padding: 0
	modal: true
	dim: true
	focus: true
	closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
	onClosed: preview = ({})

	background: Rectangle {
		radius: Theme.shellRadius
		color: Theme.surfaceRaised
		border.color: Theme.surfaceBorder
		border.width: 1
	}

	contentItem: Item {
		focus: true
		Keys.onLeftPressed: sheet.step(-1)
		Keys.onRightPressed: sheet.step(1)
		Accessible.role: Accessible.Dialog
		Accessible.name: [sheet.providerLabel, sheet.title || sheet.description].filter(part => part.length > 0)
			.join(": ")

		// Header: where it comes from, what it is, and the way out.
		Item {
			id: header
			width: parent.width
			height: headerColumn.implicitHeight + Theme.space4 * 2

			Column {
				id: headerColumn
				x: Theme.space4
				y: Theme.space4
				width: parent.width - Theme.space4 * 2 - headerButtons.width - Theme.space3
				spacing: 2

				Text {
					width: parent.width
					text: sheet.providerLabel
					textFormat: Text.PlainText
					elide: Text.ElideRight
					color: Theme.textMuted
					font.pixelSize: Theme.fontCaption
					font.weight: Font.DemiBold
				}
				Text {
					width: parent.width
					visible: text.length > 0
					// A post's author has its own row below.
					text: sheet.post ? "" : sheet.title
					textFormat: Text.PlainText
					wrapMode: Text.Wrap
					maximumLineCount: 2
					elide: Text.ElideRight
					color: Theme.textStrong
					font.pixelSize: Theme.fontTitle
					font.weight: Font.DemiBold
				}
			}
			Row {
				id: headerButtons
				anchors.right: parent.right
				anchors.rightMargin: Theme.space3
				anchors.top: parent.top
				anchors.topMargin: Theme.space3
				spacing: Theme.space1

				ModernIconButton {
					objectName: "embedDetailOpenLink"
					visible: sheet.sourceUrl.length > 0
					iconName: "external"
					Accessible.name: qsTr("Open link")
					onClicked: sheet.openRequested(sheet.sourceUrl)
				}
				ModernIconButton {
					objectName: "embedDetailClose"
					iconName: "close"
					Accessible.name: qsTr("Close")
					onClicked: sheet.close()
				}
			}
		}

		Flickable {
			id: body
			anchors.top: header.bottom
			anchors.left: parent.left
			anchors.right: parent.right
			anchors.bottom: parent.bottom
			anchors.bottomMargin: Theme.space4
			contentWidth: width
			contentHeight: bodyColumn.implicitHeight
			clip: true
			boundsBehavior: Flickable.StopAtBounds
			ScrollBar.vertical: ScrollBar {}

			Column {
				id: bodyColumn
				x: Theme.space4
				width: body.width - Theme.space4 * 2
				spacing: Theme.space3

				// The picture being looked at.
				Rectangle {
					id: viewer
					objectName: "embedDetailViewer"
					visible: sheet.media.length > 0
					width: parent.width
					height: visible ? Math.min(480, Math.round(width * 9 / 16)) : 0
					radius: Theme.innerRadius
					color: Theme.mediaCanvas
					clip: true

					Image {
						id: viewerImage
						objectName: "embedDetailImage"
						anchors.fill: parent
						source: sheet.visible ? sheet.stillSource(sheet.current) : ""
						sourceSize: Qt.size(1920, 1440)
						fillMode: Image.PreserveAspectFit
						asynchronous: true
					}
					ModernBusyIndicator {
						anchors.centerIn: parent
						running: viewerImage.status === Image.Loading
						visible: running
					}
					MouseArea {
						anchors.fill: parent
						cursorShape: Qt.PointingHandCursor
						onClicked: {
							if (sheet.currentPlayable) {
								sheet.playRequested(sheet.stableId, sheet.mediaIndex)
								sheet.close()
							} else if (viewerImage.source.toString().length > 0) {
								sheet.imageOpenRequested(viewerImage.source.toString(),
									String(sheet.current.title || sheet.title), sheet.stableId)
							}
						}
					}
					Rectangle {
						objectName: "embedDetailPlay"
						anchors.centerIn: parent
						visible: sheet.currentPlayable
						width: 64
						height: 64
						radius: 32
						color: Theme.withAlpha(Theme.mediaCanvas, 0.66)
						border.color: Theme.withAlpha(Theme.mediaOverlayTextStrong, 0.24)

						ModernIcon {
							anchors.centerIn: parent
							anchors.horizontalCenterOffset: 2
							name: sheet.current.kind === "audio" ? "volume" : "play"
							size: 28
							color: Theme.mediaOverlayTextStrong
						}
					}
					ModernIconButton {
						objectName: "embedDetailPrevious"
						anchors.left: parent.left
						anchors.leftMargin: Theme.space2
						anchors.verticalCenter: parent.verticalCenter
						visible: sheet.media.length > 1
						overlay: true
						iconName: "previous"
						Accessible.name: qsTr("Previous picture")
						onClicked: sheet.step(-1)
					}
					ModernIconButton {
						objectName: "embedDetailNext"
						anchors.right: parent.right
						anchors.rightMargin: Theme.space2
						anchors.verticalCenter: parent.verticalCenter
						visible: sheet.media.length > 1
						overlay: true
						iconName: "next"
						Accessible.name: qsTr("Next picture")
						onClicked: sheet.step(1)
					}
					Rectangle {
						anchors.right: parent.right
						anchors.bottom: parent.bottom
						anchors.margins: Theme.space2
						visible: sheet.media.length > 1
						width: counter.implicitWidth + Theme.space2 * 2
						height: counter.implicitHeight + Theme.space1
						radius: height / 2
						color: Theme.withAlpha(Theme.mediaCanvas, 0.66)

						Text {
							id: counter
							objectName: "embedDetailCounter"
							anchors.centerIn: parent
							text: (sheet.mediaIndex + 1) + " / " + sheet.media.length
							color: Theme.mediaOverlayTextStrong
							font.pixelSize: Theme.fontCaption
						}
					}
				}

				// Every picture, to jump to.
				ListView {
					id: strip
					objectName: "embedDetailStrip"
					visible: sheet.media.length > 1
					width: parent.width
					height: visible ? 64 : 0
					orientation: ListView.Horizontal
					spacing: Theme.space1
					clip: true
					model: visible ? sheet.media : []
					currentIndex: sheet.mediaIndex
					highlightFollowsCurrentItem: false
					onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)

					delegate: Rectangle {
						required property var modelData
						required property int index
						width: 96
						height: 64
						radius: Theme.innerRadius - 4
						color: Theme.mediaCanvas
						border.color: index === sheet.mediaIndex ? Theme.focus : "transparent"
						border.width: 2
						clip: true

						Image {
							anchors.fill: parent
							anchors.margins: 2
							source: sheet.thumbSource(modelData)
							sourceSize: Qt.size(192, 128)
							fillMode: Image.PreserveAspectCrop
							asynchronous: true
						}
						ModernIcon {
							anchors.centerIn: parent
							visible: modelData.kind === "video"
							name: "play"
							size: 16
							color: Theme.mediaOverlayTextStrong
						}
						MouseArea {
							anchors.fill: parent
							cursorShape: Qt.PointingHandCursor
							onClicked: sheet.mediaIndex = index
						}
					}
				}

				// A post's author.
				Row {
					visible: sheet.post
					spacing: Theme.space2

					Rectangle {
						width: 40
						height: 40
						radius: 20
						color: Theme.surfaceHover
						clip: true

						Text {
							anchors.centerIn: parent
							visible: avatar.status !== Image.Ready
							text: sheet.initials(sheet.author.name || sheet.author.handle)
							color: Theme.textMain
							font.pixelSize: Theme.fontLabel
							font.weight: Font.DemiBold
						}
						Image {
							id: avatar
							anchors.fill: parent
							source: sheet.post ? sheet.managedImage(sheet.author.avatarUrl) : ""
							sourceSize: Qt.size(80, 80)
							fillMode: Image.PreserveAspectCrop
							asynchronous: true
						}
					}
					Column {
						anchors.verticalCenter: parent.verticalCenter

						Text {
							text: String(sheet.author.name || "")
							color: Theme.textStrong
							font.pixelSize: Theme.fontBody
							font.weight: Font.DemiBold
						}
						Text {
							visible: text.length > 0
							text: String(sheet.author.handle || "")
							color: Theme.textMuted
							font.pixelSize: Theme.fontCaption
						}
					}
				}

				// The thread around a post.
				Repeater {
					model: sheet.post ? sheet.threadItems : []

					Column {
						required property var modelData
						width: bodyColumn.width
						spacing: 2

						Text {
							width: parent.width
							text: modelData.role === "reply-context"
								? qsTr("Replying to %1").arg(modelData.authorHandle || modelData.authorName || "")
								: qsTr("Quoting %1").arg(modelData.authorHandle || modelData.authorName || "")
							color: Theme.textMuted
							font.pixelSize: Theme.fontCaption
							font.weight: Font.DemiBold
						}
						Text {
							width: parent.width
							visible: text.length > 0
							text: String(modelData.text || "")
							textFormat: Text.PlainText
							wrapMode: Text.Wrap
							color: Theme.textMuted
							font.pixelSize: Theme.fontLabel
						}
					}
				}

				// The whole text.
				Text {
					objectName: "embedDetailDescription"
					width: parent.width
					visible: text.length > 0 && !(sheet.richShown && richLoader.item.ownsDescription)
					text: sheet.description
					textFormat: Text.PlainText
					wrapMode: Text.Wrap
					color: Theme.textMain
					font.pixelSize: Theme.fontBody
					lineHeight: 1.15
				}

				// Every fact, label beside value.
				Grid {
					objectName: "embedDetailFacts"
					visible: sheet.facts.length > 0 && !sheet.richShown
					columns: 2
					columnSpacing: Theme.space4
					rowSpacing: Theme.space1

					Repeater {
						model: sheet.facts.length * 2

						Text {
							required property int index
							readonly property var fact: sheet.facts[Math.floor(index / 2)]
							text: index % 2 === 0 ? String(fact.label || "") : sheet.factText(fact)
							textFormat: Text.PlainText
							color: index % 2 === 0 ? Theme.textMuted : Theme.textStrong
							font.pixelSize: Theme.fontLabel
						}
					}
				}

				// The provider's own view, when it has one beyond these facts.
				Loader {
					id: richLoader
					objectName: "embedDetailRich"
					width: parent.width
					active: sheet.visible && !!sheet.preview && !!sheet.preview.metadata
					visible: sheet.richShown
					sourceComponent: ProviderDetails {
						readonly property bool richView: steamPresentation || flashbackPresentation
							|| githubPresentation || twitchPresentation || commercePresentation
							|| googlePresentation || financeLayout
						width: richLoader.width
						metadata: sheet.preview.metadata || ({})
						previewKind: String(sheet.preview.kind || "")
						providerHint: String(sheet.preview.embedKind || sheet.providerLabel || "")
						previewTitle: sheet.title
						previewDescription: sheet.description
						expanded: true
						// The sheet shows the pictures itself.
						mediaStageActive: true
						onExternalOpenRequested: url => sheet.openRequested(url)
					}
				}

				Text {
					width: parent.width
					visible: text.length > 0
					text: sheet.sourceUrl
					textFormat: Text.PlainText
					elide: Text.ElideMiddle
					color: Theme.textFaint
					font.pixelSize: Theme.fontCaption
				}
			}
		}
	}
}
