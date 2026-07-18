import QtQuick
import QtTest

TestCase {
	id: testCase
	name: "MainComposerDraftAttachment"
	when: windowShown
	visible: true
	width: 320
	height: 180

	property string mainSource: ""

	function initTestCase() {
		mainSource = String(mainQmlSource || "")
		verify(mainSource.length > 1000, "Main.qml resource was unexpectedly empty")
	}

	function test_main_component_remains_valid_qml() {
		const component = Qt.createComponent("qrc:/qml-shell/Main.qml", Component.PreferSynchronous)
		compare(component.status, Component.Ready, component.errorString())
	}

	function test_draft_upload_exposes_localized_status_and_progress() {
		verify(/readonly property int baseHeight:\s*Math\.max\(72,\s*Theme\.railFooterHeight\)[\s\S]*Layout\.preferredHeight:\s*\(activeScope\.hasPendingReply\s*\?\s*baseHeight \+ 44\s*:\s*baseHeight\)/.test(mainSource))
		verify(/readonly property string normalizedStatus:[\s\S]*readonly property real boundedProgress:/.test(mainSource))
		verify(/qsTr\("Uploading · %1%"\)\.arg\(progressPercent\)/.test(mainSource))
		verify(/qsTr\("Preparing attachment…"\)/.test(mainSource))
		verify(/qsTr\("Ready to send"\)/.test(mainSource))
		verify(/qsTr\("Attachment failed"\)/.test(mainSource))

		verify(/ModernProgressBar\s*\{[\s\S]*objectName:\s*"composerDraftAttachmentProgress_"[\s\S]*visible:\s*draftAttachment\.uploading[\s\S]*value:\s*draftAttachment\.boundedProgress/.test(mainSource))
		verify(/ModernBusyIndicator\s*\{[\s\S]*objectName:\s*"composerDraftAttachmentBusy_"[\s\S]*visible:\s*draftAttachment\.preparing[\s\S]*running:\s*visible/.test(mainSource))
	}

	function test_desktop_rail_and_conversation_dividers_share_geometry_on_both_sides() {
		verify(/id:\s*shellHeader[\s\S]*Layout\.preferredHeight:\s*root\.compactNavigation[\s\S]*Theme\.railHeaderHeight/.test(mainSource))
		verify(/id:\s*desktopNavigationRail[\s\S]*alignedHeaderHeight:\s*shellHeader\.height[\s\S]*alignedFooterHeight:\s*composerSurface\.height/.test(mainSource))
		verify(/layoutDirection:\s*Theme\.railSide === "left"\s*\?\s*Qt\.RightToLeft\s*:\s*Qt\.LeftToRight/.test(mainSource))
	}

	function test_draft_upload_state_is_accessible_and_actions_lock_while_sending() {
		verify(/objectName:\s*"composerDraftAttachment_"[\s\S]*Accessible\.role:\s*Accessible\.Grouping[\s\S]*Accessible\.description:\s*accessibleStatusText/.test(mainSource))
		verify(/objectName:\s*"composerDraftAttachmentProgress_"[\s\S]*Accessible\.name:\s*qsTr\("Uploading %1"\)/.test(mainSource))
		verify(/objectName:\s*"composerDraftAttachmentRetry_"[\s\S]*enabled:\s*!composer\.sending/.test(mainSource))
		verify(/objectName:\s*"composerDraftAttachmentRemove_"[\s\S]*enabled:\s*!composer\.sending/.test(mainSource))
		verify(/id:\s*composerAttachButton[\s\S]*enabled:\s*activeScope\.canSend\s*&&\s*!composer\.sending/.test(mainSource))
	}

	function test_history_prepend_does_not_overwrite_the_pre_mutation_anchor() {
		verify(/onRowsAboutToBePrepended\(count\)[\s\S]*count > 0[\s\S]*timeline\.prependMutationInProgress\s*=\s*timeline\.capturePrependAnchor\(\)/.test(mainSource))
		verify(/onRowsAboutToChange[\s\S]*!timeline\.prependAnchorActive[\s\S]*timeline\.capturePrependAnchor\(\)/.test(mainSource))
		verify(/first === 0\s*&&\s*timeline\.count > 0[\s\S]*&&\s*!timeline\.prependAnchorActive[\s\S]*transitional one-row offset/.test(mainSource))
		verify(/item\.y \+ item\.height <= contentY \+ 0\.5/.test(mainSource))
		verify(/onMovementStarted[\s\S]*!prependMutationInProgress[\s\S]*releasePrependAnchor\(\)/.test(mainSource))
		verify(/FrameAnimation[\s\S]*prependAnchorCorrectionFrames > 0[\s\S]*timeline\.restorePrependAnchor\(\)/.test(mainSource))
	}

	function test_message_reply_card_reserves_its_accessibility_margins() {
		verify(/id:\s*previewCard[\s\S]*Layout\.preferredHeight:\s*replyColumn\.implicitHeight\s*\+\s*Theme\.space2\s*\*\s*2/.test(mainSource))
		verify(/id:\s*replyColumn[\s\S]*anchors\.margins:\s*Theme\.space2/.test(mainSource))
	}

	function test_jump_to_latest_does_not_overlay_a_message() {
		verify(/id:\s*jumpToLatestButton[\s\S]*Layout\.alignment:\s*Qt\.AlignRight[\s\S]*Layout\.preferredHeight:\s*visible\s*\?\s*implicitHeight\s*:\s*0/.test(mainSource))
		verify(!/id:\s*jumpToLatestButton[\s\S]{0,180}parent:\s*timeline/.test(mainSource))
	}

	function test_visual_fixture_disables_only_drawer_transition_motion() {
		verify(/id:\s*navigationDrawer[\s\S]*enter:\s*Transition\s*\{[\s\S]*property:\s*"position"[\s\S]*duration:\s*root\.visualFixtureOverrideActive\s*\?\s*0\s*:\s*Theme\.motionNormal[\s\S]*exit:\s*Transition\s*\{[\s\S]*duration:\s*root\.visualFixtureOverrideActive\s*\?\s*0\s*:\s*Theme\.motionNormal/.test(mainSource))
	}

	function test_async_operation_overlay_has_no_height_width_feedback_loop() {
		verify(/id:\s*operationList[\s\S]*delegate:\s*AsyncOperationCard\s*\{[\s\S]*width:\s*Math\.max\(1,\s*operationList\.width\s*-\s*Theme\.space2\)/.test(mainSource))
		verify(!/width:\s*operationList\.width\s*-\s*\(operationList\.contentHeight\s*>\s*operationList\.height/.test(mainSource))
	}

	function test_partial_chat_rows_do_not_leak_unclipped_accessibility_nodes() {
		verify(/accessibilityViewportVisible:\s*height\s*>\s*timeline\.height[\s\S]*y\s*\+\s*height\s*>\s*timeline\.contentY\s*\+\s*0\.5[\s\S]*y\s*>=\s*timeline\.contentY\s*-\s*0\.5[\s\S]*y\s*\+\s*height\s*<=\s*timeline\.contentY\s*\+\s*timeline\.height\s*\+\s*0\.5/.test(mainSource))
		verify(/id:\s*timeline[\s\S]*Accessible\.role:\s*Accessible\.List[\s\S]*target:\s*timeline\.contentItem[\s\S]*property:\s*"Accessible\.ignored"[\s\S]*value:\s*true/.test(mainSource))
	}

	function test_scope_change_does_not_leave_focus_in_hidden_conversation_search() {
		verify(/function\s+closeConversationSearch\(restoreFocus\)[\s\S]*searchOwnedFocus\s*=\s*root\.itemOwnsActiveFocus\(conversationSearchBar\)[\s\S]*conversationSearchButton\.forceActiveFocus/.test(mainSource))
		verify(/function\s+onScopeTokenChanged\(\)[\s\S]*closeConversationSearch\(false\)/.test(mainSource))
	}

	function test_performance_chat_fixture_waits_for_a_stable_scroll_surface() {
		verify(/function\s+performanceChatFixtureState\(\)[\s\S]*firstVisibleId[\s\S]*settled/.test(mainSource))
		verify(/"settled":\s*!bottomFollowTimer\.running[\s\S]*!timeline\.scopeResetPending[\s\S]*!performanceChatScrollRunning/.test(mainSource))
		verify(/"contentY":\s*timeline\.contentY/.test(mainSource))
	}

	function test_performance_chat_scroll_is_frame_driven_without_a_qml_animation() {
		verify(/function\s+preparePerformanceChatScrollWorkload\(stepCount\)[\s\S]*performanceChatScrollRunning\s*=\s*true/.test(mainSource))
		verify(/const travelDistance\s*=\s*Math\.min\(maximumY - minimumY,\s*requestedSteps \* 16\)[\s\S]*performanceChatScrollStartY - travelDistance/.test(mainSource))
		verify(/function\s+advancePerformanceChatScrollWorkload\(step,\s*totalSteps\)[\s\S]*timeline\.contentY\s*=\s*performanceChatScrollStartY/.test(mainSource))
		verify(/requestedSteps\s*\*\s*2/.test(mainSource))
		verify(!/id:\s*timelineScrollWorkload/.test(mainSource))
	}

	function test_chat_scroll_materializes_only_rows_that_have_heavy_content() {
		verify(/id:\s*timeline[\s\S]*reuseItems:\s*!scopeReuseResetActive[\s\S]*cacheBuffer:\s*Math\.max\(256,\s*Math\.min\(720,\s*height\)\)/.test(mainSource))
		verify(/function\s+beginScopeChange\(\)[\s\S]*scopeReuseResetActive\s*=\s*true[\s\S]*forceLayout\(\)[\s\S]*scopeReuseResetActive\s*=\s*false/.test(mainSource))
		verify(/id:\s*messageAttachmentLoader[\s\S]*active:\s*messageDelegate\.hasAttachmentContent[\s\S]*sourceComponent:\s*Component\s*\{[\s\S]*AttachmentGallery\s*\{/.test(mainSource))
		verify(/id:\s*messagePreviewLoader[\s\S]*active:\s*messageDelegate\.hasPreviewContent[\s\S]*sourceComponent:\s*Component\s*\{[\s\S]*RichPreviewCard\s*\{/.test(mainSource))
		verify(!/RichPreviewCard\s*\{\s*id:\s*messagePreviewCard/.test(mainSource))
	}

	function test_chat_scroll_reports_delegate_materialization_diagnostics() {
		verify(/function\s+timelineDelegateDiagnostics\(\)[\s\S]*"materialized"[\s\S]*"previewItems"[\s\S]*"attachmentItems"[\s\S]*"pooledEvents"[\s\S]*"reusedEvents"/.test(mainSource))
		verify(/function\s+performanceChatFixtureState\(\)[\s\S]*"delegateDiagnostics":\s*timelineDelegateDiagnostics\(\)/.test(mainSource))
		verify(/function\s+performanceChatScrollState\(\)[\s\S]*"delegateDiagnosticsDelta"[\s\S]*"previewsLoaded"[\s\S]*"attachmentsLoaded"/.test(mainSource))
	}
}
