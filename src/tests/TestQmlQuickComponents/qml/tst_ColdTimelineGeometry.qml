import QtQuick
import QtTest

TestCase {
	id: testCase
	name: "ColdTimelineGeometry"
	when: windowShown

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

	function test_cold_rich_rows_are_bounded_and_plain_rows_keep_their_height() {
		const probe = geometryProbe()
		for (const estimate of [320, 370, 741, 838, 1200])
			compare(probe.coldMessageRowHeight(estimate), 180)
		compare(probe.coldMessageRowHeight(58), 58)
		compare(probe.coldMessageRowHeight(120), 120)
		compare(probe.coldMessageRowHeight(0), 58)
		probe.activeScope = {activity: true}
		compare(probe.coldMessageRowHeight(741), 741)
		probe.destroy()
	}

	function test_metadata_revision_invalidates_content_but_holds_same_scope_geometry() {
		const probe = geometryProbe()
		compare(probe.viewportProbeBaseHeight(), 180)
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
		compare(probe.viewportProbeBaseHeight(), 180)
		probe.scopePresentationGeneration = 7
		probe.stableId = "message-b"
		verify(probe.reevaluatePresentationHeightIdentity())
		compare(probe.retainedPresentationHeight, 0)
		compare(probe.viewportProbeBaseHeight(), 180)
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
}
