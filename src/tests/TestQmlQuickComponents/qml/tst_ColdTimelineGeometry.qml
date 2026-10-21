import QtQuick
import QtTest

TestCase {
	id: testCase
	name: "ColdTimelineGeometry"
	when: windowShown
	visible: true

	function productFunction(name) {
		const source = String(mainQmlSource)
		const start = source.indexOf("function " + name + "(")
		verify(start >= 0, name)
		const open = source.indexOf("{", start)
		let depth = 1
		let end = open + 1
		while (end < source.length && depth > 0) {
			if (source[end] === "{") ++depth
			if (source[end] === "}") --depth
			++end
		}
		compare(depth, 0)
		return source.slice(start, end)
	}

	function test_readonly_author_without_avatar_is_not_a_system_message() {
		const probe = Qt.createQmlObject('import QtQuick; QtObject {'
			+ productFunction("isSystemChatMessage") + '}', testCase)
		compare(probe.isSystemChatMessage({actor: "Alex"}, "", false, false, false), false)
		compare(probe.isSystemChatMessage({actorKey: "user:2"}, "", false, false, false), false)
		compare(probe.isSystemChatMessage({system: true, actor: "Server"}, "", false, false, false), true)
		compare(probe.isSystemChatMessage({}, "", false, false, false), true)
		compare(probe.isSystemChatMessage({}, "", false, false, true), false)
		probe.destroy()
	}

	function cachedPlainQueueProbe() {
		return Qt.createQmlObject(`import QtQuick
			QtObject {
				id: probe
				property var timeline: probe
				property var activeScope: ({activity: false})
				property bool richScrollInProgress: false
				property bool prependAnchorActive: false
				property bool scopePresentationPending: false
				property bool scopePresentationFinalizing: false
				property int scopePresentationGeneration: 7
				property real height: 400
				property real cacheBuffer: 800
				property var contentItem: ({children: []})
				property var headerItem: null
				property var deferredMaterializationQueue: []
				property var plainIds: ({})
				property var promotedIds: []
				property string interruptAfterFirst: ""
				property var plainDeferredMaterializationActive: ({})
				property string deferredMaterializationActiveId: ""
				property int deferredMaterializationActiveGeneration: -1
				property int plainDeferredMaterializationsPerFrame: 2
				property int deferredMaterializationsPerFrame: 4
				property int plainDeferredSourceAdmissionCount: 0
				property int serializedDeferredSourceAdmissionCount: 0
				property bool topAnchorActive: false
				property var chatModel: probe
				property var coldScopeMaterializationDelayTimer: ({running: false})
				property var deferredMaterializationTimer: ({running: false, start: function() {}})
				function deferredMaterializationOwnershipActive() {
					return Object.keys(plainDeferredMaterializationActive).length > 0
						|| deferredMaterializationActiveId.length > 0
				}
				function finishRichContentRecoveryIfIdle() {}
				function scheduleRichContentResume() {}
				function queueRichLoaderResumes() {}
				function finishDeferredMaterializationCohort(generation) {}
				function retirePlainDeferredMaterialization(id, generation) {
					if (plainDeferredMaterializationActive[id] !== generation) return false
					const next = Object.assign({}, plainDeferredMaterializationActive)
					delete next[id]
					plainDeferredMaterializationActive = next
					return true
				}
				function deferredMessagePlainForMaterialization(id) { return plainIds[id] === true }
				function materializeDeferredMessage(id) {
					promotedIds = promotedIds.concat([id])
					if (promotedIds.length === 1) {
						if (interruptAfterFirst === "scroll") richScrollInProgress = true
						if (interruptAfterFirst === "scope") ++scopePresentationGeneration
						if (interruptAfterFirst === "rich") plainIds = {a: true}
					}
					return true
				}
				${productFunction("cachedPlainPresentationEligible")}
				${productFunction("queueVisibleDeferredMaterialization")}
				${productFunction("deferredPresentationStillEligible")}
				${productFunction("materializeNextDeferredRow")}
			}`, testCase, "cachedPlainQueueProbe")
	}

	function test_idle_prepares_cached_plain_rows_after_visible_rows() {
		const probe = cachedPlainQueueProbe()
		function row(id, y) {
			return {stableId: id, performanceTimelineDelegate: true,
				lightweightDeferredPresentation: true, contentNeedsHydration: true,
				accessibilityPooled: false, visible: true, height: 70,
				mapToItem: function() { return {y: y} }}
		}
		const rows = [row("cached-plain", -200), row("cached-rich", -100),
			row("visible", 200), row("beyond-cache", -1000), row("pooled", -300)]
		rows[4].accessibilityPooled = true
		probe.contentItem = {children: rows}
		probe.plainIds = {"cached-plain": true, "beyond-cache": true, "pooled": true}
		probe.queueVisibleDeferredMaterialization(false)
		compare(probe.deferredMaterializationQueue.join(","), "visible,cached-plain")
		verify(probe.deferredPresentationStillEligible("cached-plain"))
		probe.plainIds = {}
		verify(!probe.deferredPresentationStillEligible("cached-plain"))
		probe.deferredMaterializationQueue = []
		probe.richScrollInProgress = true
		probe.queueVisibleDeferredMaterialization(false)
		compare(probe.deferredMaterializationQueue.length, 0)
		probe.destroy()
	}

	function test_cached_plain_cohort_rechecks_reentrant_changes() {
		for (const interruption of ["", "scroll", "scope", "rich"]) {
			const probe = cachedPlainQueueProbe()
			const rows = ["a", "b", "c"].map(function(id) {
				return {stableId: id, performanceTimelineDelegate: true,
					lightweightDeferredPresentation: true, contentNeedsHydration: true,
					accessibilityPooled: false, visible: true, height: 70,
					mapToItem: function() { return {y: -200} }}
			})
			probe.contentItem = {children: rows}
			probe.plainIds = {a: true, b: true, c: true}
			probe.deferredMaterializationQueue = ["a", "b", "c"]
			probe.interruptAfterFirst = interruption
			probe.materializeNextDeferredRow()
			compare(probe.promotedIds.join(","), interruption.length ? "a" : "a,b", interruption)
			verify(Object.keys(probe.plainDeferredMaterializationActive).length <= 2)
			// Flush the generation-guarded deferred callback before destroying its owner.
			wait(0)
			probe.destroy()
		}
	}

	function test_promoted_cached_plain_recovers_after_scroll_clears_source_ownership() {
		const probe = cachedPlainQueueProbe()
		const row = {stableId: "promoted", performanceTimelineDelegate: true,
			lightweightDeferredPresentation: false, plainScopeResumeEligible: true,
			accessibilityPooled: false, height: 70,
			mapToItem: function() { return {y: -200} }}
		probe.richScrollInProgress = true
		verify(!probe.cachedPlainPresentationEligible(row))
		probe.richScrollInProgress = false
		verify(probe.cachedPlainPresentationEligible(row))
		row.plainScopeResumeEligible = false
		verify(!probe.cachedPlainPresentationEligible(row))
		row.plainScopeResumeEligible = true
		row.accessibilityPooled = true
		verify(!probe.cachedPlainPresentationEligible(row))
		probe.destroy()
	}

	function geometryProbe() {
		const source = String(mainQmlSource)
		const committedStart = source.indexOf("readonly property real presentationHeightCommittedHeight:")
		const committedEnd = source.indexOf("function viewportProbeBaseHeight()", committedStart)
		verify(committedStart >= 0 && committedEnd > committedStart)
		return Qt.createQmlObject(`import QtQuick
			QtObject {
				id: probe
				property var timeline: probe
				property var activeScope: ({activity: false})
				property int scopePresentationGeneration: 7
				property real deferredMessageRowHeight: 58
				property int deferredPresentationHeight: 741
				property string stableId: "message-a"
				property var item: ({previewLoaderIncubationIdentity: "preview-1"})
				property string presentationHeightStableId: "message-a"
				property string presentationHeightPreviewIdentity: "preview-1"
				property string presentationHeightSettledIdentity: ""
				property real presentationHeightSettledHeight: 0
				property real retainedPresentationHeight: 0
				property int retainedPresentationGeneration: -1
				property real presentationHeightObservedHeight: 0
				property int presentationHeightQuietPolishObservations: 0
				property bool presentationHeightReleasePending: false
				property bool presentationHeightReleased: false
				property bool presentationHeightTailIntent: false
				${source.slice(committedStart, committedEnd)}
				${productFunction("coldMessageRowHeight")}
				${productFunction("viewportProbeBaseHeight")}
				${productFunction("clearPresentationHeightReleaseState")}
				${productFunction("resetPresentationHeightIdentity")}
				${productFunction("presentationHeightIdentityFor")}
				${productFunction("currentPresentationHeightIdentity")}
				${productFunction("reevaluatePresentationHeightIdentity")}
			}`, testCase, "coldTimelineGeometryProbe")
	}

	function test_cold_rich_rows_reserve_provider_geometry_before_scroll() {
		const probe = geometryProbe()
		for (const estimate of [320, 370, 741, 838, 1200])
			compare(probe.coldMessageRowHeight(estimate), estimate)
		compare(probe.coldMessageRowHeight(58), 58)
		compare(probe.coldMessageRowHeight(120), 120)
		compare(probe.coldMessageRowHeight(0), 58)
		compare(probe.coldMessageRowHeight(NaN), 58)
		compare(probe.coldMessageRowHeight(Infinity), 58)
		probe.activeScope = {activity: true}
		compare(probe.coldMessageRowHeight(741), 741)
		probe.destroy()
	}

	function test_metadata_revision_invalidates_content_but_holds_same_scope_geometry() {
		const probe = geometryProbe()
		compare(probe.viewportProbeBaseHeight(), 741)
		probe.presentationHeightSettledIdentity = "message-a|preview-1"
		probe.presentationHeightSettledHeight = 741
		probe.retainedPresentationHeight = 741
		probe.retainedPresentationGeneration = 7
		probe.presentationHeightReleased = true
		compare(probe.presentationHeightCommittedHeight, 741)
		probe.item = {previewLoaderIncubationIdentity: "preview-2"}
		verify(probe.reevaluatePresentationHeightIdentity())
		compare(probe.presentationHeightCommittedHeight, 0)
		compare(probe.viewportProbeBaseHeight(), 741)
		probe.scopePresentationGeneration = 8
		probe.deferredPresentationHeight = 370
		compare(probe.viewportProbeBaseHeight(), 370)
		probe.scopePresentationGeneration = 7
		probe.stableId = "message-b"
		verify(probe.reevaluatePresentationHeightIdentity())
		compare(probe.retainedPresentationHeight, 0)
		compare(probe.viewportProbeBaseHeight(), 370)
		probe.destroy()
	}

	function test_height_commit_cannot_steal_reading_intent_at_transient_tail() {
		const probe = Qt.createQmlObject(`import QtQuick
			QtObject {
				property bool presentationHeightTailIntent: false
				property var timeline: ({stickToBottom: false, viewportAnchorActive: true,
					isAtBottom: function() { return true }, captureViewportAnchor: function() { return true }})
				${productFunction("preparePresentationHeightCommit")}
			}`, testCase)
		verify(probe.preparePresentationHeightCommit())
		verify(!probe.presentationHeightTailIntent)
		probe.timeline = {stickToBottom: true}
		verify(probe.preparePresentationHeightCommit())
		verify(probe.presentationHeightTailIntent)
		probe.destroy()
	}

	function test_reading_anchor_uses_visible_header_after_partial_cold_card() {
		const probe = Qt.createQmlObject(`import QtQuick
			QtObject {
				id: probe
				property var timeline: probe
				property real height: 600
				property var contentItem: ({children: []})
				function firstVisibleMessageDelegate() { return contentItem.children[0] }
				${productFunction("readingAnchorDelegate")}
			}`, testCase)
		function row(id, y, ready) {
			return {stableId: id, visible: true, performanceTimelineDelegate: true,
				messageRowReady: ready, presentationHeightCommittedHeight: ready ? 500 : 0,
				mapToItem: function() { return {y: y} }}
		}
		const cold = row("partial-cold", -390, false)
		const header = row("visible-header", 150, false)
		probe.contentItem = {children: [cold, row("later", 400, true), header]}
		compare(probe.readingAnchorDelegate().stableId, "visible-header")
		cold.messageRowReady = true
		cold.presentationHeightCommittedHeight = 500
		compare(probe.readingAnchorDelegate().stableId, "partial-cold")
		cold.messageRowReady = false
		probe.contentItem = {children: [cold]}
		compare(probe.readingAnchorDelegate().stableId, "partial-cold")
		probe.destroy()
	}

	function test_prefetch_skips_retained_stills_and_selects_next_cold_card() {
		const probe = Qt.createQmlObject(`import QtQuick
			QtObject {
				property real height: 600
				property real contentY: 2000
				property real originY: 0
				property real nearViewportPrefetchDistance: 1200
				property var timeline: this
				${productFunction("nearViewportPrefetchCandidateFromItem")}
			}`, testCase)
		const warm = {stableId: "warm", visible: true, height: 400,
			staticPreviewPresentationReady: true, richResourcesRequired: true,
			richResourcesActive: false, mapToItem: function() { return {y: -500} }}
		const cold = {stableId: "cold", visible: true, height: 700,
			lightweightDeferredPresentation: true, deferredPresentationHeight: 700,
			mapToItem: function() { return {y: -1200} }}
		compare(probe.nearViewportPrefetchCandidateFromItem(warm, null), null)
		compare(probe.nearViewportPrefetchCandidateFromItem(cold, null).id, "cold")
		cold.mapToItem = function() { return {y: -2000} }
		compare(probe.nearViewportPrefetchCandidateFromItem(cold, null), null)
		probe.destroy()
	}

	function test_cached_room_retires_empty_hydration_pacing_before_idle_prefetch() {
		const probe = Qt.createQmlObject(`import QtQuick
			QtObject {
				id: probe
				property var timeline: probe
				property var root: probe
				property var chatModel: ({presentationBusy: true})
				property var pendingPreviewHydrationIds: ({})
				property bool coldPreviewHydrationPacingActive: true
				property bool richScrollInProgress: false
				property bool scopePresentationPending: false
				property bool topAnchorActive: false
				property var richContentResumeTimer: ({running: false})
				property var richLoaderResumeTimer: ({running: false})
				property var deferredMaterializationTimer: ({running: false})
				property string richLoaderResumeActiveId: ""
				property var richLoaderResumeQueue: []
				property var deferredMaterializationQueue: []
				property bool prefetchSawColdPacing: true
				function deferredMaterializationOwnershipActive() { return false }
				function scheduleNearViewportPrefetch() { prefetchSawColdPacing = coldPreviewHydrationPacingActive }
				function scheduleStaticPreviewSweep() {}
				${productFunction("finishRichContentRecoveryIfIdle")}
			}`, testCase)
		probe.finishRichContentRecoveryIfIdle()
		verify(!probe.prefetchSawColdPacing)
		probe.coldPreviewHydrationPacingActive = true
		probe.pendingPreviewHydrationIds = {message: {id: "message"}}
		probe.finishRichContentRecoveryIfIdle()
		verify(probe.prefetchSawColdPacing)
		probe.destroy()
	}

	function test_measured_native_link_fallback_is_visible_before_optional_provider() {
		const source = String(mainQmlSource)
		const handoff = source.indexOf("id: asyncRichHandoff")
		const start = source.indexOf("visible:", handoff)
		const end = source.indexOf("// This item retains", start)
		verify(handoff >= 0 && start > handoff && end > start)
		const predicate = source.slice(start + "visible:".length, end)
		const probe = Qt.createQmlObject(`import QtQuick
			QtObject {
				id: probe
				property var asyncMessageDelegate: probe
				property var activeScope: ({activity: false})
				property bool deadlinePresentationFallback: false
				property bool active: true
				property real presentationHeightCommittedHeight: 108
				property var item: ({currentMessagePresentationReady: true,
					boundedRichGeometryReady: true, richResourcesRequired: true,
					richResourcesActive: false, staticPreviewPresentationReady: false,
					richLoaderSuspended: true, previewNeedsHydration: true})
				property bool handoffVisible: ${predicate}
			}`, testCase, "nativeLinkHandoffProbe")
		// The compact native row is already measured and paints the real link;
		// its optional provider must not hide it or force the scope deadline.
		compare(probe.handoffVisible, false)
		probe.presentationHeightCommittedHeight = 0
		compare(probe.handoffVisible, true)
		probe.presentationHeightCommittedHeight = 108
		probe.item = {currentMessagePresentationReady: false,
			boundedRichGeometryReady: false, richResourcesRequired: true,
			richResourcesActive: false, richLoaderSuspended: true}
		compare(probe.handoffVisible, true)
		probe.item = {currentMessagePresentationReady: true,
			boundedRichGeometryReady: false, richResourcesRequired: true,
			richResourcesActive: false, staticPreviewPresentationReady: true,
			richLoaderSuspended: false}
		compare(probe.handoffVisible, false)
		probe.deadlinePresentationFallback = true
		compare(probe.handoffVisible, true)
		probe.destroy()
	}

	function test_cold_link_text_fits_short_reserved_rows() {
		const source = String(mainQmlSource)
		const name = source.indexOf('objectName: "coldMessageLinkHandoff"')
		const start = source.lastIndexOf("Rectangle {", name)
		verify(start >= 0 && name > start)
		const open = source.indexOf("{", start)
		let depth = 1
		let end = open + 1
		while (end < source.length && depth > 0) {
			if (source[end] === "{") ++depth
			if (source[end] === "}") --depth
			++end
		}
		const probe = Qt.createQmlObject(`import QtQuick
			import QtQuick.Controls
			import Mumble.Theme 1.0
			Item {
				width: 800; height: 108
				property var asyncRichHandoff: ({visible: true})
				property var asyncMessageDelegate: ({cachedStaticHandoffCardX: 40,
					accessibilityPooled: false, presentationHeightCommittedHeight: 0,
					preview: {url: "https://example.invalid/track"},
					cachedStaticHandoffReady: false, cachedStaticHandoffSource: "",
					cachedStaticHandoffProvider: "SoundCloud",
					cachedStaticHandoffTitle: "A real track title"})
				${source.slice(start, end)}
			}`, testCase, "coldLinkLayoutProbe")
		const card = findChild(probe, "coldMessageLinkHandoff")
		verify(card !== null)
		verify(card.visible)
		for (const rowHeight of [108, 129, 141, 142, 165, 166, 180]) {
			for (const withStill of [false, true]) {
				probe.asyncMessageDelegate = Object.assign({}, probe.asyncMessageDelegate, {
					cachedStaticHandoffReady: withStill,
					cachedStaticHandoffSource: "data:image/gif;base64,R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7",
					cachedStaticHandoffTitle: "First title line\nSecond title line\nThird title line"
				})
				probe.height = rowHeight
				wait(0)
				// Every visible label must fit the actual card, even while an
				// attached scrollbar prevents committing the provider's full height.
				let labelCount = 0
				for (const child of card.children) {
					if (child.source !== undefined) {
						compare(child.visible, withStill)
						verify(child.y >= 0 && child.y + child.height <= card.height,
							"thumbnail fits card")
					}
					for (const label of child.children) {
						if (label.text === undefined || !label.visible) continue
						++labelCount
						const position = label.mapToItem(card, 0, 0)
						verify(position.y >= 0, "label begins inside card")
						verify(position.y + label.height <= card.height,
							"label fits card at row height " + rowHeight)
						if (label.text.startsWith("First title line"))
							compare(label.lineCount, 1)
					}
				}
				verify(labelCount > 0, "real title remains visible")
			}
		}
		probe.destroy()
	}

	function test_completed_native_link_without_art_survives_viewport_exit() {
		const component = Qt.createComponent("qrc:/qml-shell/RichPreviewCard.qml")
		compare(component.status, Component.Ready, component.errorString())
		const card = component.createObject(testCase, {width: 580, preview: {
			state: "ready", title: "Example Domain", host: "example.com",
			url: "https://example.com", description: "A native link card without artwork."
		}})
		verify(card !== null)
		tryCompare(card, "materializationSettled", true)
		compare(card.paintedSurface, true)
		compare(card.staticPreviewCacheActive, false)
		const source = String(mainQmlSource)
		const start = source.lastIndexOf("readonly property bool staticPreviewPresentationReady:")
		const end = source.indexOf("onStaticPreviewPresentationReadyChanged:", start)
		verify(start >= 0 && end > start)
		const probe = Qt.createQmlObject(`import QtQuick
			QtObject {
				property var messageRowLoader: ({})
				property var timeline: ({scopePresentationGeneration: 3})
				property var activeScope: ({scopeToken: "3:0"})
				property var asyncMessageDelegate: ({presentationHeightCommittedHeight: 284})
				property bool accessibilityPooled: false
				property bool hasAttachmentContent: false
				property bool hasInlineImageContent: false
				property int hydrationOwnerGeneration: 3
				property string hydrationOwnerScopeToken: "3:0"
				property bool previewMaterializationAdmitted: true
				property bool previewNeedsHydration: false
				property bool previewTerminalUnavailable: false
				${source.slice(start, end)}
			}`, testCase, "nativeCardRetentionProbe")
		probe.messageRowLoader = {item: {previewLoader: {status: Loader.Ready, item: card}}}
		compare(probe.staticPreviewPresentationReady, true)
		const height = card.implicitHeight
		card.renderActive = false
		wait(0)
		compare(probe.staticPreviewPresentationReady, true)
		compare(card.paintedSurface, true)
		compare(card.implicitHeight, height)
		probe.hydrationOwnerGeneration = 2
		compare(probe.staticPreviewPresentationReady, false)
		probe.hydrationOwnerGeneration = 3
		probe.accessibilityPooled = true
		compare(probe.staticPreviewPresentationReady, false)
		probe.accessibilityPooled = false
		probe.previewNeedsHydration = true
		compare(probe.staticPreviewPresentationReady, false)
		probe.destroy()
		card.destroy()
	}
}
