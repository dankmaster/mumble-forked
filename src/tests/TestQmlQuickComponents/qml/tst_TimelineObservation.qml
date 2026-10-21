import QtQuick
import QtTest

TestCase {
    id: testCase
    name: "TimelineObservation"
    when: windowShown

    // Execute the actual production functions with controlled model/frame input.
    // This avoids both a duplicate algorithm and source-regex-only acceptance.
    function productionFunction(name) {
        const source = String(mainQmlSource)
        const start = source.indexOf("function " + name + "(")
        verify(start >= 0, name + " exists")
        const open = source.indexOf("{", start)
        let depth = 1
        let end = open + 1
        for (; end < source.length && depth > 0; ++end) {
            if (source[end] === "{") ++depth
            else if (source[end] === "}") --depth
        }
        compare(depth, 0)
        return source.slice(start, end)
    }

    function createHarness() {
        const declarations = `
            import QtQuick
            QtObject {
                id: harness
                property var root: harness
                property int updates: 0
                function update() { ++updates }
                property var rows: ["a", "b", "c"]
                property int reads: 0
                property var chatModel: ({ count: rows.length, prependPending: false,
                    get: function(index) { ++harness.reads; return { id: harness.rows[index] } } })
                property var activeScope: ({ scopeToken: "channel:1", canSend: true })
                property int orderedMessageRevision: 0
                property int orderedIdentityRevision: -1
                property int orderedIdentityGeneration: -1
                property var orderedIdentityCache: ({})
                property int scopePresentationGeneration: 1
                property bool expectedHistoryLoaderVisible: false
                property int expectedHistoryLoaderHeight: expectedHistoryLoaderVisible ? 48 : 0
                property string prependSettlementState: "ANCHOR_CORRECTION_ACTIVE"
                property bool prependSettlementPendingObserved: true
                property bool prependAnchorActive: true
                property bool prependMutationInProgress: false
                property int prependAnchorCorrectionFrames: 0
                property string prependSettlementScopeToken: "channel:1"
                property int prependSettlementPresentationGeneration: 1
                property int prependSettlementExpectedMessageCount: 3
                property string prependSettlementExpectedOrderedIdHash: ""
                property real prependSettlementExpectedAnchorOffset: 7
                property int frameSwappedSerial: 1
                property int prependSettlementStartFrameSerial: 0
                property int prependSettlementFirstFrameSerial: 0
                property string prependSettlementFirstGeometryFingerprint: ""
                property int prependSettlementSecondObservationUpdateTransactionId: 0
                property int prependSettlementTransactionId: 1
                property int receipts: 0
                property int geometryReads: 0
                property bool actualLoaderVisible: false
                property real actualLoaderHeight: 0
                function prependSettlementTransactionActive() { return true }
                function markPrependSettlementModelApplied() { return true }
                function publishPrependSettlementReceipt(snapshot, identity) { ++receipts }
                function prependSettlementGeometrySnapshot() {
                    ++geometryReads
                    return { anchorAvailable: true, anchorOffset: 7, blankCount: 0,
                        heightPendingCount: 0, heightMismatchCount: 0,
                        historyLoaderVisible: actualLoaderVisible, historyLoaderHeight: actualLoaderHeight,
                        jumpToLatestVisible: true, composerContiguous: true,
                        fingerprint: String(actualLoaderVisible) + ":" + String(actualLoaderHeight) }
                }
                property bool scopePresentationMilestonesDirty: true
                property bool scopePresentationPending: false
                property real scopePresentationStartedAt: 1
                property real scopePresentationLoadingFrameCommittedAt: 1
                property real scopePresentationComposerInteractiveAt: 1
                property real scopePresentationFirstVisibleStableRowsCommittedAt: 1
                property real scopePresentationProviderResultAppliedAt: 0
                property string scopePresentationProviderResultSerial: ""
                property real scopePresentationProviderResultVisiblePaintAt: 0
                property real scopePresentationRichHydratedAt: 0
                property real scopePresentationDegradedReadyAt: 0
                property int diagnosticReads: 0
                property var diagnostics: ({providerResultAppliedAt: 0, providerResultSerial: "",
                    providerResultPainted: false, blankCount: 0, blankPreviewSurfaceCount: 0,
                    presentationHeightPendingCount: 0, presentationHeightMismatchCount: 0,
                    paintedLoadingCount: 0, paintedTerminalCount: 0, deadlineFallbackCount: 0,
                    inactiveRichResourceCount: 0})
                function visiblePresentationDiagnostics() { ++diagnosticReads; return diagnostics }
                function pendingVisibleHydrationCount() { return 0 }
                function pendingVisibleRichRecoveryCount() { return 0 }
        `
        const functions = ["prependOrderedMessageIdentity", "observePrependSettlementFrame",
            "recordScopePresentationFrameMilestones", "noteScopePresentationMutation"]
        const harness = Qt.createQmlObject(declarations
            + functions.map(productionFunction).join("\n") + "\n}", testCase, "TimelineObservationHarness")
        verify(harness !== null)
        harness.prependSettlementExpectedOrderedIdHash = Qt.md5("a\u001fb\u001fc")
        return harness
    }

    function nextFrame(harness) {
        harness.observePrependSettlementFrame()
        ++harness.frameSwappedSerial
    }

    function test_last_prepend_page_settles_with_hidden_history_loader() {
        const h = createHarness()
        nextFrame(h)
        compare(h.receipts, 0)
        nextFrame(h)
        compare(h.receipts, 1)
        compare(h.reads, 3)
        h.destroy()
    }

    function test_loader_geometry_must_match_expected_state_data() {
        return [ { tag: "last-page-wrong-visible", expected: false, actual: true, height: 48 },
            { tag: "more-pages-wrong-hidden", expected: true, actual: false, height: 0 },
            { tag: "visible-wrong-height", expected: true, actual: true, height: 0 } ]
    }
    function test_loader_geometry_must_match_expected_state(data) {
        const h = createHarness()
        h.expectedHistoryLoaderVisible = data.expected
        h.actualLoaderVisible = data.actual
        h.actualLoaderHeight = data.height
        nextFrame(h)
        nextFrame(h)
        compare(h.receipts, 0)
        h.destroy()
    }

    function test_loader_change_requires_two_matching_frames() {
        const h = createHarness()
        h.expectedHistoryLoaderVisible = true
        h.actualLoaderVisible = true
        h.actualLoaderHeight = 48
        nextFrame(h)
        h.expectedHistoryLoaderVisible = false
        h.actualLoaderVisible = false
        h.actualLoaderHeight = 0
        nextFrame(h)
        compare(h.receipts, 0)
        nextFrame(h)
        compare(h.receipts, 1)
        h.destroy()
    }

    function test_identity_cache_invalidates_same_count_reorder_and_new_scope() {
        const h = createHarness()
        const first = h.prependOrderedMessageIdentity().hash
        h.prependOrderedMessageIdentity()
        compare(h.reads, 3)
        h.rows = ["c", "b", "a"]
        ++h.orderedMessageRevision
        verify(h.prependOrderedMessageIdentity().hash !== first)
        compare(h.reads, 6)
        ++h.scopePresentationGeneration
        h.prependOrderedMessageIdentity()
        compare(h.reads, 9)
        h.destroy()
    }

    function test_correction_frames_skip_expensive_observation() {
        const h = createHarness()
        h.prependAnchorCorrectionFrames = 2
        nextFrame(h)
        nextFrame(h)
        compare(h.reads, 0)
        compare(h.geometryReads, 0)
        h.destroy()
    }

    function test_settled_frames_skip_scans_and_late_provider_rearms() {
        const h = createHarness()
        h.recordScopePresentationFrameMilestones()
        verify(h.scopePresentationRichHydratedAt > 0)
        for (let frame = 0; frame < 12; ++frame)
            h.recordScopePresentationFrameMilestones()
        compare(h.diagnosticReads, 1)
        // Simulate a provider result after the old observation deadline.
        h.scopePresentationStartedAt = Date.now() - 2000
        const diagnostics = h.diagnostics
        diagnostics.providerResultAppliedAt = Date.now() - 10
        diagnostics.providerResultSerial = "late-provider-2"
        diagnostics.providerResultPainted = true
        h.diagnostics = diagnostics
        h.noteScopePresentationMutation()
        h.recordScopePresentationFrameMilestones()
        compare(h.diagnosticReads, 2)
        compare(h.scopePresentationProviderResultSerial, "late-provider-2")
        verify(h.scopePresentationProviderResultVisiblePaintAt > 0)
        h.destroy()
    }
}
