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
	// A stationary pressed scrollbar may warm the selected still and a bounded
	// set of actually instantiated thumbnail stills. Animated media, playback
	// and provider resources remain owned by renderActive.
	property bool warmStaticActive: false
	readonly property int maximumWarmThumbnailAdmissions: 8
	readonly property int warmThumbnailAdmissionIntervalMs: 16
	property var warmThumbnailAdmissions: ({})
	property var warmThumbnailExpiredAdmissions: ({})
	property var warmThumbnailStatuses: ({})
	property var warmThumbnailSources: ({})
	property var warmThumbnailIdentities: ({})
	property int warmThumbnailAdmissionCount: 0
	property int warmThumbnailNoCandidateScans: 0
	readonly property int warmThumbnailLayoutWaitMs: 128
	property double warmThumbnailScanStartedAt: 0
	property bool warmThumbnailScanComplete: false
	property bool warmThumbnailCompletionLatched: false
	property int warmThumbnailActiveIndex: -1
	readonly property int warmThumbnailActiveWatchdogMs: 256
	property int warmThumbnailProgressRevision: 0
	property string warmCurrentKind: ""
	property string warmCurrentImageSource: ""
	property bool warmCurrentSnapshotInitialized: false
	readonly property int warmThumbnailPendingCount: {
		let count = 0
		const keys = Object.keys(warmThumbnailAdmissions)
		for (let index = 0; index < keys.length; ++index) {
			const status = Number(warmThumbnailStatuses[keys[index]])
			if (status !== Image.Ready && status !== Image.Error)
				++count
		}
		return count
	}
	readonly property bool warmThumbnailRailComplete: !warmStaticActive
		|| mediaItems.length <= 1
		|| (warmThumbnailScanComplete && warmThumbnailPendingCount === 0)
	// Retain only the selected still artwork while the owning rich card is
	// outside the viewport. AnimatedImage and every playable/provider resource
	// continue to follow renderActive strictly.
	property bool retainStaticVisual: false
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
	readonly property string currentKind: thumbnailPresentationKind(currentItem)
	readonly property string currentImageSource: currentKind === "image"
		|| currentKind === "animated-image" ? String(currentItem.url || "")
		: String(currentItem.posterUrl || currentItem.poster
			|| currentItem.thumbnailUrl || currentItem.thumbnail || "")
	readonly property bool warmCurrentSnapshotActive: warmStaticActive && !renderActive
		&& warmCurrentSnapshotInitialized
	readonly property string effectiveCurrentKind: warmCurrentSnapshotActive
		? warmCurrentKind : currentKind
	readonly property string effectiveCurrentImageSource: warmCurrentSnapshotActive
		? warmCurrentImageSource : currentImageSource
	readonly property int currentVisualStatus: effectiveCurrentKind === "animated-image"
		? animatedImageLoader.mediaStatus : stillImage.status
	readonly property bool currentVisualReady: currentVisualStatus === Image.Ready
	readonly property bool currentVisualLoading: effectiveCurrentKind === "animated-image"
		? animatedImageLoader.active && currentVisualStatus === Image.Loading
		: currentVisualStatus === Image.Loading
	readonly property bool retainedStaticVisualReady: retainStaticVisual
		&& effectiveCurrentKind !== "animated-image" && currentVisualReady
	readonly property bool managedPaintableSourceAvailable:
		effectiveCurrentImageSource.length > 0
	readonly property bool staticVisualPending: (retainStaticVisual || warmStaticActive)
		&& effectiveCurrentKind !== "animated-image"
		&& effectiveCurrentImageSource.length > 0
		&& (currentVisualStatus === Image.Null || currentVisualStatus === Image.Loading)
	readonly property bool paintedLoading: viewportVisible && !mediaRequiresReveal
		&& effectiveCurrentKind !== "animated-image"
		&& effectiveCurrentImageSource.length > 0
		&& String(stillImage.source).length > 0
		&& (stillImage.status === Image.Null || stillImage.status === Image.Loading)
	readonly property bool paintedTerminal: viewportVisible && !mediaRequiresReveal
		&& managedPaintableSourceAvailable && currentVisualStatus === Image.Error
	readonly property bool paintedActionableFallback: viewportVisible
		&& !mediaRequiresReveal && !managedPaintableSourceAvailable
		&& mediaItems.length > 0
	readonly property bool paintedSurface: viewportVisible && !mediaRequiresReveal
		&& managedPaintableSourceAvailable
		&& (currentVisualReady || paintedTerminal)
	readonly property string performanceMediaSurfaceState: !viewportVisible
		? "not-required" : mediaRequiresReveal ? "reveal"
		: currentVisualReady ? "poster"
		: currentVisualLoading ? "loading"
		: mediaFallbackIcon.visible ? "fallback" : "blank"
	readonly property bool compactFallback: viewportVisible
		&& effectiveCurrentImageSource.length === 0 && !mediaRequiresReveal
	readonly property real resolvedViewportHeight: !viewportVisible ? 0
		: compactFallback ? Theme.controlHeight + Theme.space5
		: viewportPreferredHeight > 0 ? viewportPreferredHeight
		: expanded ? Math.min(420, Math.max(240, width * 9 / 16))
		: Math.min(320, Math.max(180, width * 9 / 16))
	signal selectionRequested(int index)
	signal mediaRequested(int index)
	signal revealRequested()

	function clearWarmThumbnailAdmissions(preserveCompletion) {
		const keepCompletion = preserveCompletion === true
			&& warmThumbnailCompletionLatched
		warmThumbnailAdmissionTimer.stop()
		if (!keepCompletion) {
			warmThumbnailAdmissions = ({})
			warmThumbnailExpiredAdmissions = ({})
			warmThumbnailStatuses = ({})
			warmThumbnailSources = ({})
			warmThumbnailIdentities = ({})
			warmThumbnailAdmissionCount = 0
		}
		warmThumbnailNoCandidateScans = 0
		warmThumbnailScanStartedAt = 0
		warmThumbnailScanComplete = keepCompletion
		warmThumbnailCompletionLatched = keepCompletion
		warmThumbnailActiveIndex = -1
		warmThumbnailActiveWatchdog.stop()
	}

	function thumbnailPresentationKind(mediaItem) {
		const item = mediaItem || ({})
		const kind = String(item.kind || "").toLowerCase()
		const mime = String(item.mime || item.mediaMime || "").toLowerCase()
		const presentation = String(item.presentation
			|| item.mediaPresentation || "").toLowerCase()
		const contentBranch = String(item.contentBranch || "").toLowerCase()
		const imageTransport = kind.length === 0 || kind === "image"
		if (item.managedAnimated === true || kind === "gif"
				|| kind === "animated-image"
				|| (imageTransport && (mime === "image/gif"
					|| presentation === "animated-image"
					|| contentBranch.indexOf("animated") >= 0)))
			return "animated-image"
		return kind
	}

	function staticThumbnailSource(mediaItem) {
		const item = mediaItem || ({})
		const kind = thumbnailPresentationKind(item)
		if (kind === "animated-image")
			return String(item.thumbnailUrl || item.thumbnail
				|| item.posterUrl || item.poster || "")
		if (kind === "image")
			return String(item.thumbnailUrl || item.thumbnail
				|| item.posterUrl || item.poster || item.url || "")
		return String(item.posterUrl || item.poster
			|| item.thumbnailUrl || item.thumbnail || "")
	}

	function renderedThumbnailSource(mediaItem) {
		const item = mediaItem || ({})
		const stillSource = staticThumbnailSource(item)
		if (stillSource.length > 0)
			return stillSource
		return thumbnailPresentationKind(item) === "animated-image"
			? String(item.url || "") : ""
	}

	function warmThumbnailIdentity(mediaItem, index) {
		const item = mediaItem || ({})
		const stablePart = String(item.id || item.mediaId || item.key || index)
		return stablePart + "|" + staticThumbnailSource(item)
	}

	function scheduleWarmThumbnailAdmission() {
		if (!warmStaticActive || renderActive || warmThumbnailCompletionLatched
				|| !currentVisualReady
				|| mediaRequiresReveal || mediaItems.length <= 1
				|| warmThumbnailAdmissionCount >= maximumWarmThumbnailAdmissions
				|| warmThumbnailActiveIndex >= 0)
			return
		warmThumbnailAdmissionTimer.restart()
	}

	function admitNextWarmThumbnail() {
		if (!warmStaticActive || renderActive || warmThumbnailCompletionLatched
				|| !currentVisualReady
				|| mediaRequiresReveal || !thumbnailList.visible)
			return false
		const viewportY = Math.max(0, thumbnailList.height / 2)
		const firstVisible = thumbnailList.indexAt(
			thumbnailList.contentX + 1, viewportY)
		const startIndex = firstVisible >= 0 ? firstVisible
			: Math.max(0, Math.min(selectedIndex, mediaItems.length - 1))
		for (let index = startIndex; index < mediaItems.length; ++index) {
			if (warmThumbnailAdmissionCount >= maximumWarmThumbnailAdmissions)
				break
			const item = thumbnailList.itemAtIndex(index)
			if (!item || item.inThumbnailViewport !== true)
				continue
			if (index === selectedIndex
					|| warmThumbnailAdmissions[String(index)] === true
					|| warmThumbnailExpiredAdmissions[String(index)] === true)
				continue
			const key = String(index)
			const admittedSource = staticThumbnailSource(mediaItems[index])
			if (admittedSource.length === 0) {
				const nextExpired = Object.assign({}, warmThumbnailExpiredAdmissions)
				nextExpired[key] = true
				warmThumbnailExpiredAdmissions = nextExpired
				const terminalStatuses = Object.assign({}, warmThumbnailStatuses)
				terminalStatuses[key] = Image.Error
				warmThumbnailStatuses = terminalStatuses
				++warmThumbnailAdmissionCount
				++warmThumbnailProgressRevision
				continue
			}
			const nextStatuses = Object.assign({}, warmThumbnailStatuses)
			nextStatuses[key] = Image.Null
			warmThumbnailStatuses = nextStatuses
			++warmThumbnailAdmissionCount
			warmThumbnailNoCandidateScans = 0
			warmThumbnailActiveIndex = index
			warmThumbnailActiveWatchdog.restart()
			++warmThumbnailProgressRevision
			const nextSources = Object.assign({}, warmThumbnailSources)
			nextSources[key] = admittedSource
			warmThumbnailSources = nextSources
			const nextIdentities = Object.assign({}, warmThumbnailIdentities)
			nextIdentities[key] = warmThumbnailIdentity(mediaItems[index], index)
			warmThumbnailIdentities = nextIdentities
			// Publish the admission last. This binding activates Image.source; all
			// ownership and timeout state must already exist if a cache hit reports
			// Ready synchronously.
			const nextAdmissions = Object.assign({}, warmThumbnailAdmissions)
			nextAdmissions[key] = true
			warmThumbnailAdmissions = nextAdmissions
			Qt.callLater(function() {
				const admittedItem = thumbnailList.itemAtIndex(index)
				if (admittedItem && warmThumbnailAdmissions[key] === true)
					updateWarmThumbnailStatus(index, admittedItem.thumbnailStatus)
			})
			return true
		}
		if (warmThumbnailAdmissionCount >= maximumWarmThumbnailAdmissions) {
			warmThumbnailScanComplete = true
			return false
		}
		++warmThumbnailNoCandidateScans
		if (warmThumbnailScanStartedAt <= 0)
			warmThumbnailScanStartedAt = Date.now()
		if (Date.now() - warmThumbnailScanStartedAt
				< warmThumbnailLayoutWaitMs)
			scheduleWarmThumbnailAdmission()
		else
			warmThumbnailScanComplete = true
		return false
	}

	function updateWarmThumbnailStatus(index, status) {
		const key = String(index)
		if (warmThumbnailAdmissions[key] !== true)
			return
		if (warmThumbnailExpiredAdmissions[key] === true)
			return
		const nextStatuses = Object.assign({}, warmThumbnailStatuses)
		nextStatuses[key] = Number(status)
		warmThumbnailStatuses = nextStatuses
		if (Number(index) !== warmThumbnailActiveIndex
				|| (Number(status) !== Image.Ready
					&& Number(status) !== Image.Error))
			return
		warmThumbnailActiveWatchdog.stop()
		warmThumbnailActiveIndex = -1
		++warmThumbnailProgressRevision
		if (warmThumbnailAdmissionCount >= maximumWarmThumbnailAdmissions)
			warmThumbnailScanComplete = true
		else
			scheduleWarmThumbnailAdmission()
	}

	function expireWarmThumbnailAdmission() {
		if (warmThumbnailActiveIndex < 0)
			return
		const key = String(warmThumbnailActiveIndex)
		const nextStatuses = Object.assign({}, warmThumbnailStatuses)
		nextStatuses[key] = Image.Error
		warmThumbnailStatuses = nextStatuses
		const nextExpired = Object.assign({}, warmThumbnailExpiredAdmissions)
		nextExpired[key] = true
		warmThumbnailExpiredAdmissions = nextExpired
		warmThumbnailActiveIndex = -1
		++warmThumbnailProgressRevision
		if (warmThumbnailAdmissionCount >= maximumWarmThumbnailAdmissions)
			warmThumbnailScanComplete = true
		else
			scheduleWarmThumbnailAdmission()
	}

	function rearmWarmThumbnailScan() {
		if (!warmStaticActive || renderActive || warmThumbnailCompletionLatched
				|| !currentVisualReady
				|| mediaRequiresReveal
				|| warmThumbnailAdmissionCount >= maximumWarmThumbnailAdmissions)
			return
		warmThumbnailNoCandidateScans = 0
		warmThumbnailScanComplete = false
		scheduleWarmThumbnailAdmission()
	}

	onWarmStaticActiveChanged: {
		if (!warmStaticActive) {
			warmCurrentSnapshotInitialized = false
			warmCurrentKind = ""
			warmCurrentImageSource = ""
			clearWarmThumbnailAdmissions()
		} else {
			warmCurrentImageSource = staticThumbnailSource(currentItem)
			warmCurrentKind = currentKind === "animated-image"
				&& warmCurrentImageSource.length > 0 ? "image" : currentKind
			warmCurrentSnapshotInitialized = true
			if (mediaItems.length <= 1) {
			warmThumbnailScanComplete = true
			warmThumbnailCompletionLatched = true
			} else
				scheduleWarmThumbnailAdmission()
		}
	}
	onRenderActiveChanged: {
		if (renderActive)
			clearWarmThumbnailAdmissions()
		else
			scheduleWarmThumbnailAdmission()
	}
	onCurrentVisualReadyChanged: {
		if (!currentVisualReady) {
			const preserveCompletion = warmStaticActive && !renderActive
				&& warmThumbnailCompletionLatched
			clearWarmThumbnailAdmissions(preserveCompletion)
		}
		else {
			if (warmStaticActive && !renderActive)
				++warmThumbnailProgressRevision
			scheduleWarmThumbnailAdmission()
		}
	}
	onSelectedIndexChanged: {
		const preserveCompletion = warmStaticActive && !renderActive
			&& warmThumbnailCompletionLatched
		clearWarmThumbnailAdmissions(preserveCompletion)
		if (!preserveCompletion)
			scheduleWarmThumbnailAdmission()
	}
	onMediaItemsChanged: {
		const preserveCompletion = warmStaticActive && !renderActive
			&& warmThumbnailCompletionLatched
		clearWarmThumbnailAdmissions(preserveCompletion)
		if (!preserveCompletion)
			scheduleWarmThumbnailAdmission()
	}
	onWarmThumbnailRailCompleteChanged: {
		if (warmStaticActive && !renderActive && warmThumbnailRailComplete)
			warmThumbnailCompletionLatched = true
	}

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

		// This underlay is deliberately always present behind the renderer. A
		// virtualized row clears its Image source to release decoded data, while
		// Image.Ready may also precede the first scene-graph texture frame. In
		// both cases the provider viewport must remain visibly actionable rather
		// than exposing the black media canvas.
		ModernIcon {
			id: mediaFallbackIcon
			objectName: "embedDocumentMediaFallbackIcon"
			anchors.centerIn: parent
			visible: !root.mediaRequiresReveal
			name: root.currentKind === "audio" ? "volume"
				: root.currentKind === "video" ? "play" : "eye"
			size: Theme.space6
			color: Theme.mediaOverlayTextMuted
			Accessible.ignored: true
		}

		Image {
			id: stillImage
			objectName: "embedDocumentMediaImage"
			anchors.fill: parent
			source: (root.renderActive || root.retainStaticVisual
				|| root.warmStaticActive)
				&& !root.mediaRequiresReveal
				&& root.effectiveCurrentKind !== "animated-image"
				? root.effectiveCurrentImageSource : ""
			asynchronous: true
			cache: true
			// Share one exact decoded-size cache key with Main's bounded preheater.
			sourceSize: Qt.size(1280, 960)
			fillMode: Image.PreserveAspectFit
			// Leave the Image item mounted throughout the source handoff; the
			// fallback underlay remains visible when no texture exists.
			visible: String(source).length > 0
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

	Timer {
		id: warmThumbnailAdmissionTimer
		interval: root.warmThumbnailAdmissionIntervalMs
		repeat: false
		onTriggered: root.admitNextWarmThumbnail()
	}
	Timer {
		id: warmThumbnailActiveWatchdog
		interval: root.warmThumbnailActiveWatchdogMs
		repeat: false
		onTriggered: root.expireWarmThumbnailAdmission()
	}

	ListView {
		id: thumbnailList
		objectName: "embedDocumentMediaThumbnailList"
		Layout.fillWidth: true
		Layout.preferredHeight: visible ? 64 : 0
		// Activation owns image work, not the gallery's layout. Collapsing the
		// rail while suspended changes the card height as it enters the viewport.
		visible: root.mediaItems.length > 1
		enabled: root.renderActive
		orientation: ListView.Horizontal
		spacing: Theme.space2
		clip: true
		reuseItems: true
		// Keep one neighbouring thumbnail warm, not the provider's complete
		// gallery. A Steam preview may expose 16 media items; eager construction
		// made every hidden button and image binding join the same GUI polish.
		cacheBuffer: 96
		boundsBehavior: Flickable.StopAtBounds
		model: root.mediaItems
		ScrollBar.horizontal: ScrollBar {
			policy: ScrollBar.AsNeeded
		}

		delegate: Button {
			id: thumbnailButton
			required property int index
			required property var modelData
			readonly property bool inThumbnailViewport:
				x + width > thumbnailList.contentX
				&& x < thumbnailList.contentX + thumbnailList.width
			readonly property int thumbnailStatus: thumbnailImage.status
			objectName: "embedDocumentMediaThumbnail_" + index

			width: 82
			height: 56
			hoverEnabled: root.renderActive
			Accessible.name: qsTr("Show media %1 of %2")
				.arg(index + 1).arg(root.mediaItems.length)
			Accessible.ignored: !root.renderActive || !inThumbnailViewport
			onClicked: root.selectionRequested(index)
			onInThumbnailViewportChanged: {
				if (inThumbnailViewport)
					root.rearmWarmThumbnailScan()
			}
			Component.onCompleted: root.rearmWarmThumbnailScan()

			background: Rectangle {
				radius: Theme.innerRadius
				color: thumbnailButton.down ? Theme.embedSelection : Theme.embedSurface
				border.color: root.selectedIndex === thumbnailButton.index
					? root.accent : Theme.embedBorder
				border.width: root.selectedIndex === thumbnailButton.index ? 2 : 1
			}

			contentItem: Item {
				Image {
					id: thumbnailImage
					objectName: "embedDocumentMediaThumbnailImage_" + thumbnailButton.index
					anchors.fill: parent
					anchors.margins: 2
					source: {
						if (!thumbnailButton.inThumbnailViewport)
							return ""
						const key = String(thumbnailButton.index)
						if (!root.renderActive) {
							if (root.warmThumbnailAdmissions[key] !== true
									|| root.warmThumbnailExpiredAdmissions[key] === true)
								return ""
							if (root.warmThumbnailIdentities[key]
									!== root.warmThumbnailIdentity(
										thumbnailButton.modelData, thumbnailButton.index))
								return ""
							return String(root.warmThumbnailSources[key] || "")
						}
						return root.renderedThumbnailSource(thumbnailButton.modelData)
					}
					asynchronous: true
					cache: true
					sourceSize: Qt.size(Math.ceil(width * Screen.devicePixelRatio),
						Math.ceil(height * Screen.devicePixelRatio))
					fillMode: Image.PreserveAspectCrop
					onStatusChanged: root.updateWarmThumbnailStatus(
						thumbnailButton.index, status)
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

		Connections {
			target: root
			function onSelectedIndexChanged() {
				thumbnailList.positionViewAtIndex(root.selectedIndex, ListView.Contain)
			}
		}
		onContentXChanged: {
			root.warmThumbnailNoCandidateScans = 0
			root.scheduleWarmThumbnailAdmission()
		}
		onWidthChanged: {
			root.warmThumbnailNoCandidateScans = 0
			root.scheduleWarmThumbnailAdmission()
		}
	}
}
