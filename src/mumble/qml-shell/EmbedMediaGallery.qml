pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mumble.Theme 1.0

ColumnLayout {
	id: root

	property var mediaItems: []
	property int selectedIndex: 0
	property bool expanded: false
	property bool renderActive: true
	property bool animationsEnabled: true
	property bool mediaRequiresReveal: false
	property bool viewportVisible: true
	property real viewportPreferredWidth: 0
	property real viewportPreferredHeight: 0
	property color accent: Theme.accent
	property string accessibleTitle: ""
	readonly property var currentItem: mediaItems.length > 0
		? mediaItems[Math.max(0, Math.min(selectedIndex, mediaItems.length - 1))] : ({})
	readonly property string currentPresentation: String(currentItem.presentation
		|| currentItem.mediaPresentation || "").toLowerCase()
	readonly property string currentContentBranch: String(currentItem.contentBranch || "").toLowerCase()
	readonly property bool currentAnimatedPresentation:
		currentPresentation === "animated-image"
		|| currentContentBranch.indexOf("animated") >= 0
	readonly property string currentKind: currentItem.managedAnimated
		? "animated-image" : String(currentItem.kind || "")
	readonly property string currentImageSource: currentKind === "image"
		|| currentKind === "animated-image" ? String(currentItem.url || "")
		: String(currentItem.posterUrl || currentItem.poster
			|| currentItem.thumbnailUrl || currentItem.thumbnail || "")
	readonly property bool compactFallback: viewportVisible
		&& currentImageSource.length === 0 && !mediaRequiresReveal
	readonly property real resolvedViewportHeight: !viewportVisible ? 0
		: compactFallback ? Theme.controlHeight + Theme.space5
		: viewportPreferredHeight > 0 ? viewportPreferredHeight
		: expanded ? Math.min(420, Math.max(240, width * 9 / 16))
		: Math.min(320, Math.max(180, width * 9 / 16))
	signal selectionRequested(int index)
	signal mediaRequested(int index)
	signal revealRequested()

	function previousMedia() {
		if (mediaItems.length > 1)
			selectionRequested((selectedIndex + mediaItems.length - 1) % mediaItems.length)
	}

	function nextMedia() {
		if (mediaItems.length > 1)
			selectionRequested((selectedIndex + 1) % mediaItems.length)
	}

	visible: mediaItems.length > 0
	spacing: Theme.space2

	Rectangle {
		id: mediaViewport
		objectName: "embedDocumentMediaViewport"
		Layout.fillWidth: true
		Layout.preferredWidth: root.viewportPreferredWidth > 0
			? root.viewportPreferredWidth : root.width
		Layout.maximumWidth: root.viewportPreferredWidth > 0
			? root.viewportPreferredWidth : Number.POSITIVE_INFINITY
		Layout.preferredHeight: root.resolvedViewportHeight
		Layout.minimumHeight: Layout.preferredHeight
		Layout.maximumHeight: Layout.preferredHeight
		Layout.alignment: Qt.AlignHCenter
		visible: root.viewportVisible
		radius: Theme.innerRadius
		color: Theme.mediaCanvas
		border.color: Theme.withAlpha(root.accent, 0.32)
		border.width: 1
		clip: true
		Accessible.role: Accessible.Graphic
		Accessible.name: root.accessibleTitle.length > 0
			? root.accessibleTitle : qsTr("Preview media")

		Image {
			id: stillImage
			objectName: "embedDocumentMediaImage"
			anchors.fill: parent
			source: root.renderActive && !root.mediaRequiresReveal
				&& root.currentKind !== "animated-image" ? root.currentImageSource : ""
			asynchronous: true
			cache: false
			fillMode: Image.PreserveAspectFit
			visible: status === Image.Ready
		}

		Loader {
			id: animatedImageLoader
			objectName: "embedDocumentAnimatedImageLoader"
			property int mediaStatus: Image.Null
			anchors.fill: parent
			active: root.renderActive && !root.mediaRequiresReveal
				&& root.currentKind === "animated-image"
				&& root.currentImageSource.length > 0
			onActiveChanged: mediaStatus = active ? Image.Loading : Image.Null
			sourceComponent: AnimatedImage {
				source: root.currentImageSource
				asynchronous: true
				cache: false
				playing: root.animationsEnabled && visible && status === Image.Ready
				fillMode: Image.PreserveAspectFit
				onStatusChanged: animatedImageLoader.mediaStatus = status
				Component.onCompleted: animatedImageLoader.mediaStatus = status
			}
		}

		ModernBusyIndicator {
			anchors.centerIn: parent
			running: stillImage.status === Image.Loading
				|| (animatedImageLoader.active
					&& animatedImageLoader.mediaStatus === Image.Loading)
			visible: running
			animated: root.animationsEnabled
			Accessible.name: qsTr("Loading preview media")
		}

		ModernIcon {
			anchors.centerIn: parent
			visible: !root.mediaRequiresReveal && root.currentImageSource.length === 0
			name: root.currentKind === "audio" ? "volume"
				: root.currentKind === "video" ? "play" : "eye"
			size: Theme.space6
			color: Theme.mediaOverlayTextMuted
			Accessible.ignored: true
		}

		Rectangle {
			anchors.fill: parent
			visible: !root.mediaRequiresReveal
				&& (root.currentKind === "video" || root.currentKind === "audio")
			color: Theme.withAlpha(Theme.mediaCanvas, root.currentImageSource.length > 0 ? 0.18 : 0)
		}

		Rectangle {
			id: playbackPrompt
			objectName: "embedDocumentPlaybackPrompt"
			anchors.centerIn: parent
			visible: !root.mediaRequiresReveal
				&& (root.currentKind === "video" || root.currentKind === "audio")
			implicitWidth: playbackPromptContent.implicitWidth + Theme.space4
			implicitHeight: Theme.controlHeight
			width: implicitWidth
			height: implicitHeight
			radius: height / 2
			color: root.accent
			border.color: Theme.withAlpha(Theme.mediaOverlayTextStrong, 0.42)
			border.width: 1
			Accessible.ignored: true

			Row {
				id: playbackPromptContent
				anchors.centerIn: parent
				spacing: Theme.space2
				ModernIcon {
					name: root.currentKind === "audio" ? "volume" : "play"
					size: Theme.avatarSmall
					color: Theme.contrastText(root.accent)
					Accessible.ignored: true
				}
				Label {
					text: root.currentKind === "audio" ? qsTr("Play audio")
						: root.currentAnimatedPresentation ? qsTr("Play animation")
						: qsTr("Play video")
					textFormat: Text.PlainText
					color: Theme.contrastText(root.accent)
					font.pixelSize: Theme.fontLabel
					font.weight: Font.DemiBold
				}
			}
		}

		Button {
			objectName: "embedDocumentMediaPrimaryAction"
			anchors.fill: parent
			visible: !root.mediaRequiresReveal
			hoverEnabled: true
			background: Rectangle {
				color: parent.down ? Theme.withAlpha(root.accent, 0.20)
					: parent.hovered ? Theme.withAlpha(root.accent, 0.10) : "transparent"
			}
			contentItem: Item {}
			Accessible.name: {
				const title = root.accessibleTitle.length > 0
					? root.accessibleTitle : qsTr("preview media")
				if (root.currentKind === "video" && root.currentAnimatedPresentation)
					return qsTr("Play animation: %1").arg(title)
				if (root.currentKind === "video")
					return qsTr("Play %1").arg(title)
				if (root.currentKind === "audio")
					return qsTr("Play audio: %1").arg(title)
				return qsTr("Open %1").arg(title)
			}
			onClicked: root.mediaRequested(root.selectedIndex)
		}

		ModernIconButton {
			objectName: "embedDocumentPreviousMediaButton"
			anchors.left: parent.left
			anchors.leftMargin: Theme.space2
			anchors.verticalCenter: parent.verticalCenter
			z: 4
			visible: root.mediaItems.length > 1 && !root.mediaRequiresReveal
			overlay: true
			iconName: "previous"
			Accessible.name: qsTr("Previous media")
			Accessible.description: qsTr("Show media %1 of %2")
				.arg((root.selectedIndex + root.mediaItems.length - 1)
					% root.mediaItems.length + 1)
				.arg(root.mediaItems.length)
			onClicked: root.previousMedia()
		}

		ModernIconButton {
			objectName: "embedDocumentNextMediaButton"
			anchors.right: parent.right
			anchors.rightMargin: Theme.space2
			anchors.verticalCenter: parent.verticalCenter
			z: 4
			visible: root.mediaItems.length > 1 && !root.mediaRequiresReveal
			overlay: true
			iconName: "next"
			Accessible.name: qsTr("Next media")
			Accessible.description: qsTr("Show media %1 of %2")
				.arg((root.selectedIndex + 1) % root.mediaItems.length + 1)
				.arg(root.mediaItems.length)
			onClicked: root.nextMedia()
		}

		Rectangle {
			anchors.fill: parent
			visible: root.mediaRequiresReveal
			color: Theme.embedRevealSurface

			ModernButton {
				anchors.centerIn: parent
				text: qsTr("Reveal media")
				tone: "accent"
				onClicked: root.revealRequested()
			}
		}

		Rectangle {
			anchors.right: parent.right
			anchors.bottom: parent.bottom
			anchors.margins: Theme.space2
			visible: root.mediaItems.length > 1
			width: mediaCountLabel.implicitWidth + Theme.space2 * 2
			height: Theme.space5
			radius: height / 2
			color: Theme.withAlpha(Theme.mediaCanvas, 0.84)

			Label {
				id: mediaCountLabel
				anchors.centerIn: parent
				text: qsTr("%1 / %2").arg(root.selectedIndex + 1).arg(root.mediaItems.length)
				textFormat: Text.PlainText
				color: Theme.mediaOverlayTextStrong
				font.pixelSize: Theme.fontCaption
			}
		}
	}

	ScrollView {
		Layout.fillWidth: true
		Layout.preferredHeight: visible ? 64 : 0
		visible: root.mediaItems.length > 1
		ScrollBar.vertical.policy: ScrollBar.AlwaysOff
		ScrollBar.horizontal.policy: ScrollBar.AsNeeded
		contentWidth: thumbnailRow.implicitWidth
		contentHeight: height

		Row {
			id: thumbnailRow
			height: parent.height
			spacing: Theme.space2

			Repeater {
				model: root.mediaItems

				delegate: Button {
					id: thumbnailButton
					required property int index
					required property var modelData
					objectName: "embedDocumentMediaThumbnail_" + index

					width: 82
					height: 56
					hoverEnabled: true
					Accessible.name: qsTr("Show media %1 of %2")
						.arg(index + 1).arg(root.mediaItems.length)
					onClicked: root.selectionRequested(index)

					background: Rectangle {
						radius: Theme.innerRadius
						color: thumbnailButton.down ? Theme.embedSelection : Theme.embedSurface
						border.color: root.selectedIndex === thumbnailButton.index
							? root.accent : Theme.embedBorder
						border.width: root.selectedIndex === thumbnailButton.index ? 2 : 1
					}

					contentItem: Item {
						Image {
							objectName: "embedDocumentMediaThumbnailImage_" + thumbnailButton.index
							anchors.fill: parent
							anchors.margins: 2
							source: {
								if (!root.renderActive)
									return ""
								const kind = thumbnailButton.modelData.managedAnimated
									? "animated-image" : String(thumbnailButton.modelData.kind || "")
								if (kind === "image" || kind === "animated-image")
									return String(thumbnailButton.modelData.thumbnailUrl
										|| thumbnailButton.modelData.thumbnail
										|| thumbnailButton.modelData.url || "")
								return String(thumbnailButton.modelData.posterUrl
									|| thumbnailButton.modelData.poster
									|| thumbnailButton.modelData.thumbnailUrl
									|| thumbnailButton.modelData.thumbnail || "")
							}
							asynchronous: true
							// These are intentionally tiny. Caching the bounded decoded
							// thumbnails prevents a multi-image card from decoding the same
							// strip again after every ListView pool/reuse cycle.
							cache: true
							sourceSize: Qt.size(Math.ceil(width * Screen.devicePixelRatio),
								Math.ceil(height * Screen.devicePixelRatio))
							fillMode: Image.PreserveAspectCrop
						}
						ModernIcon {
							anchors.centerIn: parent
							visible: {
								const kind = String(thumbnailButton.modelData.kind || "")
								return kind === "video" || kind === "audio"
							}
							name: String(thumbnailButton.modelData.kind || "") === "audio"
								? "volume" : "play"
							size: Theme.avatarSmall
							color: Theme.mediaOverlayTextStrong
							Accessible.ignored: true
						}
					}
				}
			}
		}
	}
}
