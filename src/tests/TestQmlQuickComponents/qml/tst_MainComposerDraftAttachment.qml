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

	function test_failed_original_image_stays_terminal_until_explicit_retry() {
		verify(/reportedOriginalState[\s\S]*reportedOriginalState !== "error"/.test(mainSource))
		verify(/function\s+retryAttachmentOriginal\(attachment\)[\s\S]*originalState = "loading"[\s\S]*requestChat(?:Attachment|Inline)Image/.test(mainSource))
		verify(/onOriginalRetryRequested:\s*attachment => root\.retryAttachmentOriginal\(attachment\)/.test(mainSource))
	}

	function test_attachment_viewer_tracks_the_model_that_opened_it() {
		verify(/property var attachmentViewerHydrationModel:\s*null/.test(mainSource))
		verify(/target:\s*root\.attachmentViewerHydrationModel \|\| chatModel/.test(mainSource))
		verify(/function\s+openAttachment\(attachment, titleOverride, hydrationMessageId, hydrationModel\)/.test(mainSource))
		verify(/const sourceModel = attachmentViewerHydrationModel \|\| chatModel[\s\S]*sourceModel\.rowForStableId[\s\S]*sourceModel\.get/.test(mainSource))
		verify(/onManagedAttachmentOpenRequested:[\s\S]*directMessages \? directMessages\.timelineModel : null/.test(mainSource))
	}

	function test_message_reaction_separates_emoji_from_count_typography() {
		verify(/objectName:\s*"messageReactionEmoji"[\s\S]*font\.family:\s*Qt\.platform\.os === "windows" \? "Segoe UI Emoji"/.test(mainSource))
		verify(/objectName:\s*"messageReactionCount"[\s\S]*font\.weight:\s*Font\.DemiBold/.test(mainSource))
	}

	function test_message_footer_keeps_reaction_chips_next_to_the_action_tray() {
		verify(/id:\s*messageFooter[\s\S]*id:\s*messageReactionFlow[\s\S]*id:\s*messageActionTray/.test(mainSource))
		verify(/id:\s*messageActionTray[\s\S]*radius:\s*height\s*\/\s*2[\s\S]*border\.color:\s*Theme\.divider/.test(mainSource))
	}

	function test_message_actions_are_direct_without_an_overflow_button() {
		verify(!/objectName:\s*"(?:message|compactMessage)ActionsButton"/.test(mainSource))
		verify(/id:\s*messageReactButton\s*\n\s*objectName:\s*"messageReactButton"[\s\S]*iconName:\s*"reaction"/.test(mainSource))
		verify(/objectName:\s*"messageReplyButton"[\s\S]*visible:\s*messageDelegate\.canReply[\s\S]*messageFooter\.quickReactionsExpanded/.test(mainSource))
		verify(/objectName:\s*"compactMessageReplyButton"[\s\S]*visible:\s*messageDelegate\.canReply/.test(mainSource))
		verify(/id:\s*compactMessageReactButton\s*\n\s*objectName:\s*"compactMessageReactButton"[\s\S]*iconName:\s*"reaction"/.test(mainSource))
	}

	function test_message_actions_use_layout_neutral_overlay_for_text_and_rich_content() {
		verify(/readonly property bool hasEmbeddedFooterContent:[\s\S]*readonly property bool usesCompactActionOverlay:/.test(mainSource))
		verify(/readonly property bool hasEmbeddedFooterContent:\s*hasReactions[\s\S]*\|\|\s*hasReplyContent/.test(mainSource))
		verify(!/readonly property bool hasEmbeddedFooterContent:[^\n]*(?:\n[^\n]*){0,4}\bwideContent\b/.test(mainSource))
		verify(/readonly property bool usesCompactActionOverlay:\s*hasMessageActions\s*(?:\r?\n)/.test(mainSource))
		verify(!/readonly property bool usesCompactActionOverlay:[^\n]*(?:\n[^\n]*){0,2}!wideContent/.test(mainSource))
		verify(/id:\s*messageFooter[\s\S]*visible:\s*messageDelegate\.hasEmbeddedFooterContent/.test(mainSource))
		verify(/id:\s*messageFooter[\s\S]*Layout\.rightMargin:\s*messageDelegate\.usesCompactActionOverlay[\s\S]*compactMessageActionTray\.width/.test(mainSource))
		verify(/objectName:\s*"chatCompactMessageActionTray"[\s\S]*visible:\s*messageDelegate\.usesCompactActionOverlay/.test(mainSource))
		verify(/id:\s*compactMessageActionTray[\s\S]*anchors\.bottom:\s*parent\.bottom/.test(mainSource))
		verify(/id:\s*messageBody[\s\S]*Layout\.rightMargin:\s*messageDelegate\.usesCompactActionOverlay[\s\S]*compactMessageActionTray\.width/.test(mainSource))
		verify(/objectName:\s*"compactMessageReactButton"[\s\S]*iconName:\s*"reaction"/.test(mainSource))
	}

	function test_all_quick_reactions_use_one_anchored_popup_without_scrolling() {
		verify(/property var quickReactionAnchorItem:\s*null/.test(mainSource))
		verify(/id:\s*globalQuickReactionPopup[\s\S]*parent:\s*Overlay\.overlay[\s\S]*id:\s*globalQuickReactionBar/.test(mainSource))
		verify(/function positionQuickReactionPopup\(\)[\s\S]*anchor\.mapToItem\(overlay[\s\S]*const aboveY/.test(mainSource))
		verify(/function showQuickReactions\(messageId, row, anchorItem, activeReactions\)/.test(mainSource))
		verify(!/function showQuickReactions[\s\S]{0,900}positionViewAtIndex/.test(mainSource))
		verify(!/id:\s*(?:inlineQuickReactions|floatingQuickReactionPopup)/.test(mainSource))
	}

	function test_draft_upload_exposes_localized_status_and_progress() {
		verify(/visible:\s*!activeScope\.activity/.test(mainSource))
		verify(/readonly property int baseHeight:\s*Math\.max\([\s\S]{0,240}outerMargin \* 2[\s\S]{0,240}inputRowMinimumHeight,[\s\S]{0,80}Theme\.railFooterHeight\)/.test(mainSource))
		verify(/Layout\.preferredHeight:\s*visible\s*\?\s*\(activeScope\.hasPendingReply\s*\?\s*baseHeight \+ 44\s*:\s*baseHeight\)/.test(mainSource))
		verify(/id:\s*composerInputRow[\s\S]{0,160}Layout\.minimumHeight:\s*composerSurface\.inputRowMinimumHeight/.test(mainSource))
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
		verify(/id:\s*desktopNavigationRail[\s\S]*alignedHeaderHeight:\s*shellHeader\.height[\s\S]*alignedFooterHeight:\s*Math\.max\(Theme\.railFooterHeight,\s*composerSurface\.height\)/.test(mainSource))
		verify(/layoutDirection:\s*Theme\.railSide === "left"\s*\?\s*Qt\.RightToLeft\s*:\s*Qt\.LeftToRight/.test(mainSource))
	}

	function test_window_edge_stonks_tickers_span_both_conversation_and_profile_columns() {
		verify(/RowLayout\s*\{[\s\S]{0,180}anchors\.fill:\s*parent[\s\S]{0,180}anchors\.topMargin:\s*windowTopStonksTicker\.height[\s\S]{0,100}anchors\.bottomMargin:\s*bottomStonksTicker\.height/.test(mainSource))
		verify(/id:\s*windowTopStonksTicker[\s\S]{0,240}anchors\.left:\s*parent\.left[\s\S]{0,120}anchors\.right:\s*parent\.right[\s\S]{0,120}anchors\.top:\s*parent\.top/.test(mainSource))
		verify(/id:\s*bottomStonksTicker[\s\S]{0,240}anchors\.left:\s*parent\.left[\s\S]{0,120}anchors\.right:\s*parent\.right[\s\S]{0,120}anchors\.bottom:\s*parent\.bottom/.test(mainSource))
		verify(/id:\s*topStonksTicker[\s\S]{0,220}Layout\.fillWidth:\s*true[\s\S]{0,260}root\.stonksTickerPlacement === "top"/.test(mainSource))
		verify(/id:\s*aboveComposerStonksTicker[\s\S]{0,320}root\.stonksTickerPlacement === "aboveComposer"/.test(mainSource))
	}

	function test_stonks_profile_shortcut_respects_the_client_preference() {
		verify(/readonly property bool stonksShortcutEnabled:[\s\S]{0,320}stonksTickerState\.profileShortcutVisible !== false/.test(mainSource))
		verify(/id:\s*desktopNavigationRail[\s\S]{0,900}stonksEnabled:\s*root\.stonksShortcutEnabled/.test(mainSource))
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
		verify(/objectName:\s*"chatTimelineScrollBar"[\s\S]*visible:\s*root\.visualFixtureSurfaceVariant\s*!==\s*"chat-history-prepend-anchor"[\s\S]*Accessible\.ignored:\s*!visible/.test(mainSource))
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
		for (const labelId of ["messageSenderLabel", "messageTimestampLabel", "messageStatusLabel"]) {
			const escapedId = labelId.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")
			verify(new RegExp("id:\\s*" + escapedId
				+ "[\\s\\S]{0,520}Accessible\\.ignored:\\s*root\\.backgroundAccessibilitySuppressed"
				+ "[\\s\\S]{0,180}!messageDelegate\\.itemContainedInViewport\\(" + escapedId + "\\)").test(mainSource))
		}
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

	function test_scope_history_is_presented_only_after_tail_geometry_settles() {
		verify(/property bool scopePresentationPending:\s*false[\s\S]*property int scopePresentationExposedHeightChangeCount:\s*0/.test(mainSource))
		verify(/function\s+beginScopeChange\(\)[\s\S]*scopePresentationPending\s*=\s*true[\s\S]*scopePresentationQuietTimer\.start\(\)[\s\S]*scopePresentationDeadlineTimer\.start\(\)/.test(mainSource))
		verify(/onContentHeightChanged:\s*\{[\s\S]*if \(scopePresentationPending\)[\s\S]*noteScopePresentationMutation\(\)[\s\S]*return/.test(mainSource))
		verify(/id:\s*scopePresentationQuietTimer[\s\S]*interval:\s*120[\s\S]*finishScopePresentation\(false\)/.test(mainSource))
		verify(/function\s+pendingScopeHydrationCount\(\)[\s\S]*contentNeedsHydration[\s\S]*inHydrationWindow/.test(mainSource))
		verify(/property bool bodyNeedsHydration:\s*!!source\.bodyHydrationPending[\s\S]*property bool contentNeedsHydration:[\s\S]*bodyNeedsHydration/.test(mainSource))
		verify(/function\s+requestPreviewHydrationIfNeeded\(\)[\s\S]*backendContentNeedsHydration[\s\S]*queuePreviewHydration/.test(mainSource))
		verify(/function\s+finishScopePresentation\(forcedByDeadline\)[\s\S]*!forcedByDeadline\s*&&\s*pendingScopeHydrationCount\(\)\s*>\s*0[\s\S]*scopePresentationQuietTimer\.restart\(\)/.test(mainSource))
		verify(/id:\s*scopePresentationDeadlineTimer[\s\S]*interval:\s*2000[\s\S]*finishScopePresentation\(true\)/.test(mainSource))
		verify(/function\s+noteScopePresentationMutation\(\)[\s\S]*scopePresentationFinalizing[\s\S]*scopePresentationFinalizeQuietTimer\.restart\(\)/.test(mainSource))
		verify(/function\s+finishScopePresentation\(forcedByDeadline\)[\s\S]*scopeReuseResetActive\s*=\s*false[\s\S]*containTailMessageWhenPossible\(\)[\s\S]*scopePresentationFinalizeQuietTimer\.start\(\)[\s\S]*scopePresentationFinalizeDeadlineTimer\.start\(\)/.test(mainSource))
		verify(/id:\s*scopePresentationFinalizeQuietTimer[\s\S]*interval:\s*120[\s\S]*completeScopePresentationFinalization\(false\)/.test(mainSource))
		verify(/id:\s*scopePresentationFinalizeDeadlineTimer[\s\S]*interval:\s*1500[\s\S]*completeScopePresentationFinalization\(true\)/.test(mainSource))
		verify(/function\s+completeScopePresentationFinalization\(forcedByDeadline\)[\s\S]*forceLayout\(\)[\s\S]*positionTailImmediately\(\)[\s\S]*containTailMessageWhenPossible\(\)[\s\S]*scopePresentationPending\s*=\s*false[\s\S]*scopePresentationObservationTimer\.restart\(\)/.test(mainSource))
		verify(/delegate:\s*ChatMessageFrame[\s\S]*opacity:\s*timeline\.scopePresentationPending\s*\?\s*0\s*:\s*1/.test(mainSource))
		verify(/id:\s*emptyConversationState[\s\S]*visualLoading:\s*!activeScope\.activity\s*&&\s*\(activeScope\.loading[\s\S]*timeline\.scopePresentationPending\s*&&\s*chatModel\.count\s*>\s*0[\s\S]*Preparing conversation/.test(mainSource))
		verify(/function\s+timelinePresentationState\(\)[\s\S]*"scopeResetPending":\s*timeline\.scopeResetPending[\s\S]*exposedHeightChangeCount[\s\S]*exposedTailCorrectionCount[\s\S]*"settled":[\s\S]*!timeline\.scopeResetPending/.test(mainSource))
	}

	function test_activity_is_a_read_only_log_surface_without_conversation_loading_chrome() {
		verify(/function\s+canCompleteScopePresentationFastPath\(\)[\s\S]*if\s*\(activeScope\.activity\)[\s\S]*return\s+true/.test(mainSource))
		verify(/id:\s*emptyConversationState[\s\S]*activeScope\.activity\s*\?\s*chatModel\.count\s*===\s*0[\s\S]*qsTr\("No server log entries yet"\)[\s\S]*qsTr\("New Murmur server-log entries will appear here live\."\)/.test(mainSource))
		verify(/id:\s*composerSurface[\s\S]*visible:\s*!activeScope\.activity[\s\S]*Layout\.preferredHeight:\s*visible\s*\?/.test(mainSource))
		verify(/id:\s*activityLogColumnHeader[\s\S]{0,2400}visible:\s*activeScope\.activity[\s\S]{0,1200}qsTr\("TIME"\)[\s\S]{0,800}qsTr\("EVENT"\)[\s\S]{0,1200}qsTr\("LIVE"\)/.test(mainSource))
		verify(/id:\s*timeline[\s\S]{0,500}Accessible\.name:\s*activeScope\.activity\s*\?\s*qsTr\("Activity log"\)/.test(mainSource))
		verify(/delegate:\s*ChatMessageFrame\s*\{[\s\S]{0,400}logEntry:\s*activeScope\.activity[\s\S]{0,160}logAlternating:\s*logEntry\s*&&\s*index\s*%\s*2\s*===\s*1/.test(mainSource))
		verify(/id:\s*activityLogRow[\s\S]{0,300}visible:\s*messageDelegate\.logEntry[\s\S]{0,1000}objectName:\s*"activityLogTimestamp"[\s\S]{0,1400}objectName:\s*"activityLogGutter"[\s\S]{0,1600}objectName:\s*"activityLogEvent"/.test(mainSource))
		verify(/id:\s*messageRow[\s\S]{0,180}visible:\s*!messageDelegate\.logEntry/.test(mainSource))
	}

	function test_composer_keeps_its_destination_visible_while_typing() {
		verify(/objectName:\s*"composerTargetRow"[\s\S]*objectName:\s*"composerTargetLabel"[\s\S]*qsTr\("To: %1"\)\.arg\(activeScope\.label\)/.test(mainSource))
		verify(/objectName:\s*"composerTargetKindLabel"[\s\S]*text:\s*activeScope\.kindLabel/.test(mainSource))
		verify(/Accessible\.description:\s*qsTr\("Sending to %1\. %2"\)[\s\S]*arg\(activeScope\.label\)\.arg\(activeScope\.composerHint\)/.test(mainSource))
	}

	function test_empty_and_lightweight_scopes_skip_the_presentation_delay() {
		verify(/function\s+scheduleScopePresentationFastPath\(\)[\s\S]*scopePresentationFastPathTimer\.restart\(\)/.test(mainSource))
		verify(/function\s+rowNeedsDeferredPresentation\(row\)[\s\S]*bodyHydrationPending[\s\S]*Object\.keys\(preview\)[\s\S]*attachments\.length/.test(mainSource))
		verify(/function\s+canCompleteScopePresentationFastPath\(\)[\s\S]*activeScope\.loading[\s\S]*count\s*===\s*0[\s\S]*return\s+true[\s\S]*contentHeight\s*>\s*height\s*\+\s*0\.5[\s\S]*itemAtIndex\(0\)[\s\S]*itemAtIndex\(count\s*-\s*1\)/.test(mainSource))
		verify(/function\s+completeScopePresentationFastPath\(\)[\s\S]*scopePresentationQuietTimer\.stop\(\)[\s\S]*scopeResetPending\s*=\s*false[\s\S]*scopePresentationFastPath\s*=\s*true[\s\S]*scopePresentationPending\s*=\s*false/.test(mainSource))
		verify(/id:\s*scopePresentationFastPathTimer[\s\S]*interval:\s*0[\s\S]*completeScopePresentationFastPath\(\)/.test(mainSource))
		verify(/function\s+onLoadingChanged\(\)[\s\S]*scheduleScopePresentationFastPath\(\)/.test(mainSource))
		verify(/function\s+onCountChanged\(\)[\s\S]*scheduleScopePresentationFastPath\(\)/.test(mainSource))
		verify(/"presentationFastPath":\s*timeline\.scopePresentationFastPath/.test(mainSource))
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
		verify(/id:\s*timelineScrollHandler[\s\S]*targetFlickable:\s*timeline[\s\S]*horizontalEnabled:\s*false[\s\S]*smoothWheelEnabled:\s*true[\s\S]*wheelStep:\s*Math\.max\(80,\s*Math\.min\(112,\s*timeline\.height \* 0\.14\)\)/.test(mainSource))
		verify(/function\s+beginScopeChange\(\)[\s\S]*scopeReuseResetActive\s*=\s*true[\s\S]*function\s+finishScopePresentation\(forcedByDeadline\)[\s\S]*scopeReuseResetActive\s*=\s*false[\s\S]*forceLayout\(\)/.test(mainSource))
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
