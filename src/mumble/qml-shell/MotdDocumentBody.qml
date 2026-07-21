import QtQuick
import QtQuick.Layouts
import Mumble.Theme 1.0

Item {
	id: root

	required property var blocks
	property string fallbackText: ""
	property bool compactLayout: width < 560
	property real maximumImageWidth: 640
	property real maximumImageHeight: compactLayout ? 120 : 180
	property bool animationsEnabled: true
	property bool hoverEffectsEnabled: true
	signal linkRequested(string url)

	readonly property var effectiveBlocks: (blocks || []).length > 0 ? blocks
		: fallbackText.length > 0 ? [{ "kind": "paragraph",
			"segments": [{ "text": fallbackText }], "plainText": fallbackText,
			"alignment": "left", "indent": 0 }] : []
	readonly property var imageBlocks: filteredBlocks("image")
	readonly property var textBlocks: nonImageBlocks()
	readonly property bool hasImages: imageBlocks.length > 0
	readonly property int textCharacterCount: textBlocks.reduce(function(total, block) {
		return total + String(block.plainText || "").length
	}, 0)
	readonly property bool heroLayout: !compactLayout && imageBlocks.length === 1
		&& textBlocks.length > 0 && textBlocks.length <= 4 && textCharacterCount <= 420

	objectName: "motdDocumentBody"
	implicitHeight: heroLayout ? heroRow.implicitHeight : documentColumn.implicitHeight
	Accessible.ignored: true

	function filteredBlocks(kind) {
		const result = []
		for (const block of root.effectiveBlocks) {
			if (block && String(block.kind || "paragraph") === kind)
				result.push(block)
		}
		return result
	}

	function nonImageBlocks() {
		const result = []
		for (const block of root.effectiveBlocks) {
			if (block && String(block.kind || "paragraph") !== "image")
				result.push(block)
		}
		return result
	}

	RowLayout {
		id: heroRow
		objectName: "motdHeroLayout"
		width: parent.width
		visible: root.heroLayout
		spacing: Theme.space4

		MotdBlock {
			objectName: "motdHeroImage"
			Layout.preferredWidth: Math.min(180, Math.max(120, heroRow.width * 0.24))
			Layout.alignment: Qt.AlignTop
			block: root.imageBlocks.length > 0 ? root.imageBlocks[0] : ({})
			compactLayout: root.compactLayout
			maximumImageWidth: width
			maximumImageHeight: Math.min(140, root.maximumImageHeight)
			animationsEnabled: root.animationsEnabled
			hoverEffectsEnabled: root.hoverEffectsEnabled
			onLinkRequested: function(url) { root.linkRequested(url) }
		}

		Column {
			id: heroText
			objectName: "motdHeroText"
			Layout.fillWidth: true
			Layout.alignment: Qt.AlignVCenter
			spacing: Theme.space2

			Repeater {
				model: root.textBlocks

				delegate: MotdBlock {
					required property var modelData
					width: heroText.width
					block: modelData
					compactLayout: root.compactLayout
					maximumImageWidth: root.maximumImageWidth
					maximumImageHeight: root.maximumImageHeight
					animationsEnabled: root.animationsEnabled
					hoverEffectsEnabled: root.hoverEffectsEnabled
					onLinkRequested: function(url) { root.linkRequested(url) }
				}
			}
		}
	}

	Column {
		id: documentColumn
		objectName: "motdDocumentColumn"
		width: parent.width
		visible: !root.heroLayout
		spacing: Theme.space2

		Repeater {
			model: root.effectiveBlocks

			delegate: MotdBlock {
				required property var modelData
				width: documentColumn.width
				block: modelData
				compactLayout: root.compactLayout
				maximumImageWidth: root.maximumImageWidth
				maximumImageHeight: root.maximumImageHeight
				animationsEnabled: root.animationsEnabled
				hoverEffectsEnabled: root.hoverEffectsEnabled
				onLinkRequested: function(url) { root.linkRequested(url) }
			}
		}
	}
}
