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
		verify(/id:\s*messageFooter[\s\S]*Layout\.rightMargin:\s*messageDelegate\.compactActionTextInset/.test(mainSource))
		verify(/objectName:\s*"chatCompactMessageActionTray"[\s\S]*visible:\s*messageDelegate\.usesCompactActionOverlay/.test(mainSource))
		verify(/id:\s*compactMessageActionTray[\s\S]*anchors\.bottom:\s*parent\.bottom/.test(mainSource))
		verify(/id:\s*messageBody[\s\S]*Layout\.rightMargin:\s*messageDelegate\.compactActionTextInset/.test(mainSource))
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

	function test_url_only_messages_keep_the_source_inside_any_preview_card_once() {
		verify(/function\s+normalizedComparableSourceUrl\(value\)/.test(mainSource))
		verify(/function\s+messageBodyIsPreviewSourceOnly\(segments,\s*preview\)/.test(mainSource))
		verify(/document\.commonPresentation\s*===\s*true/.test(mainSource))
		verify(/preview\s*\?\s*preview\.url/.test(mainSource))
		verify(/bodyUrl\s*!==\s*sourceUrl/.test(mainSource))
		verify(/id:\s*messageBody[\s\S]{0,220}!root\.messageBodyIsPreviewSourceOnly\(/.test(mainSource))
	}

	function test_enter_sends_typed_pasted_dropped_or_attachment_drafts_without_breaking_ime() {
		verify(/const returnKey = event\.key === Qt\.Key_Return \|\| event\.key === Qt\.Key_Enter/.test(mainSource))
		verify(/!returnKey \|\| \(event\.modifiers & Qt\.ShiftModifier\)[\s\S]{0,100}composerInput\.inputMethodComposing/.test(mainSource))
		verify(/text\.trim\(\)\.length > 0 \|\| composer\.attachments\.count > 0[\s\S]{0,80}composer\.send\(\)/.test(mainSource))
		verify(/Empty Return is deliberately a no-op[\s\S]{0,180}event\.accepted = true/.test(mainSource))
		verify(/event\.matches\(StandardKey\.Paste\)[\s\S]{0,220}composer\.pasteFromClipboard\(\)[\s\S]{0,160}composerInput\.paste\(\)/.test(mainSource))
	}

	function test_desktop_rail_and_conversation_dividers_share_geometry_on_both_sides() {
		verify(/id:\s*shellHeader[\s\S]*Layout\.preferredHeight:\s*root\.compactNavigation[\s\S]*Theme\.railHeaderHeight/.test(mainSource))
		verify(/id:\s*desktopNavigationRail[\s\S]*alignedHeaderHeight:\s*shellHeader\.height[\s\S]*alignedFooterHeight:\s*Math\.max\(Theme\.railFooterHeight,\s*composerSurface\.height\)/.test(mainSource))
		verify(/layoutDirection:\s*Theme\.railSide === "left"\s*\?\s*Qt\.RightToLeft\s*:\s*Qt\.LeftToRight/.test(mainSource))
	}

	function test_window_edge_stonks_tickers_span_both_conversation_and_profile_columns() {
		verify(/RowLayout\s*\{[\s\S]{0,180}anchors\.fill:\s*parent[\s\S]{0,180}anchors\.topMargin:\s*windowTopStonksTickerLoader\.height[\s\S]{0,120}anchors\.bottomMargin:\s*bottomStonksTickerLoader\.height/.test(mainSource))
		verify(/id:\s*windowTopStonksTickerLoader[\s\S]{0,240}anchors\.left:\s*parent\.left[\s\S]{0,120}anchors\.right:\s*parent\.right[\s\S]{0,120}anchors\.top:\s*parent\.top/.test(mainSource))
		verify(/id:\s*bottomStonksTickerLoader[\s\S]{0,240}anchors\.left:\s*parent\.left[\s\S]{0,120}anchors\.right:\s*parent\.right[\s\S]{0,120}anchors\.bottom:\s*parent\.bottom/.test(mainSource))
		verify(/anchors\.bottomMargin:\s*composerSurface\.height\s*\+\s*bottomStonksTickerLoader\.height\s*\+\s*22/.test(mainSource))
		verify(/anchors\.bottomMargin:\s*Theme\.space4\s*\+\s*bottomStonksTickerLoader\.height/.test(mainSource))
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
		verify(/const point = item\.mapToItem\(timeline, 0, 0\)[\s\S]*point\.y \+ item\.height <= 0\.5/.test(mainSource))
		verify(/function\s+takeExplicitNavigationOwnership\(classification\)[\s\S]*stickToBottom\s*=\s*false[\s\S]*releasePrependAnchor\(\)/.test(mainSource))
		verify(/FrameAnimation[\s\S]*prependAnchorCorrectionFrames > 0[\s\S]*timeline\.restorePrependAnchor\(\)/.test(mainSource))
		verify(/FrameAnimation[\s\S]*deferredPrependAnchorActive[\s\S]*timeline\.restoreDeferredPrependAnchor\(\)[\s\S]*timeline\.restorePrependAnchor\(\)/.test(mainSource))
		verify(/onRowsInserted[\s\S]*prependAnchorCorrectionFrames\s*=\s*Math\.max\([\s\S]*prependAnchorCorrectionFrames,\s*4\)/.test(mainSource))
		verify(/objectName:\s*"chatTimelineScrollBar"[\s\S]*visible:\s*root\.visualFixtureSurfaceVariant\s*!==\s*"chat-history-prepend-anchor"[\s\S]*Accessible\.ignored:\s*!visible/.test(mainSource))
	}

	function test_delayed_rich_card_height_changes_preserve_the_visible_message() {
		verify(/property string viewportAnchorId:\s*""[\s\S]*property bool viewportAnchorActive:\s*false[\s\S]*property bool viewportAnchorReleasePending:\s*false/.test(mainSource))
		verify(/function\s+captureViewportAnchor\(\)[\s\S]*firstVisibleMessageDelegate\(\)[\s\S]*viewportAnchorOffset\s*=\s*item\.mapToItem\(timeline,\s*0,\s*0\)\.y/.test(mainSource))
		verify(/function\s+timelineDelegateForStableId\(stableId\)[\s\S]*contentItem\.children[\s\S]*headerItem[\s\S]*deferredRepeater\.itemAt\(childIndex\)[\s\S]*String\(item\.stableId\s*\|\|\s*""\)\s*===\s*key/.test(mainSource))
		verify(/function\s+restoreViewportAnchor\(releaseWhenRestored\)[\s\S]*releaseWhenRestored\s*===\s*true[\s\S]*viewportAnchorReleasePending\s*=\s*true[\s\S]*timelineDelegateForStableId\(viewportAnchorId\)[\s\S]*viewportAnchorRetries\s*<=\s*8[\s\S]*currentOffset\s*=\s*item\.mapToItem\(timeline,\s*0,\s*0\)\.y[\s\S]*contentY\s*\+\s*currentOffset\s*-\s*viewportAnchorOffset[\s\S]*contentY\s*=\s*desiredY[\s\S]*viewportAnchorReleasePending[\s\S]*releaseViewportAnchor\(\)/.test(mainSource))
		verify(/onContentHeightChanged:[\s\S]*viewportAnchorActive[\s\S]*Qt\.callLater\(function\(\)\s*\{\s*timeline\.restoreViewportAnchor\(\)/.test(mainSource))
		verify(/function\s+takeExplicitNavigationOwnership\(classification\)[\s\S]*releaseViewportAnchor\(\)[\s\S]*onMovementEnded:[\s\S]*!stickToBottom[\s\S]*captureViewportAnchor\(\)/.test(mainSource))
	}

	function test_compact_message_actions_reserve_current_row_width() {
		verify(/readonly property int compactActionButtonCount:[\s\S]{0,420}readonly property real compactActionTextInset:/.test(mainSource))
		compare((mainSource.match(/Layout\.rightMargin:\s*messageDelegate\.compactActionTextInset/g) || []).length, 2)
		verify(/id:\s*compactMessageActionTray[\s\S]{0,320}width:\s*messageDelegate\.compactActionTrayWidth/.test(mainSource))
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
		verify(/function\s+onScopeReplacementPresentationRequested\(\)[\s\S]*timeline\.beginScopeChange\(\)/.test(mainSource))
	}

	function test_performance_chat_fixture_waits_for_a_stable_scroll_surface() {
		verify(/function\s+performanceChatFixtureState\(\)[\s\S]*firstVisibleId[\s\S]*settled/.test(mainSource))
		verify(/"settled":\s*!bottomFollowTimer\.running[\s\S]*!timeline\.scopeResetPending[\s\S]*!performanceChatScrollRunning/.test(mainSource))
		verify(/"contentY":\s*timeline\.contentY/.test(mainSource))
	}

	function test_scope_history_exposes_real_summaries_while_tail_geometry_settles() {
		verify(/property bool scopePresentationPending:\s*false[\s\S]*scopePresentationContentVisible:[\s\S]*!scopePresentationPending\s*\|\|\s*scopePresentationFinalizing[\s\S]*property int scopePresentationExposedHeightChangeCount:\s*0/.test(mainSource))
		verify(/function\s+beginScopeChange\(\)[\s\S]*scopePresentationPending\s*=\s*true[\s\S]*scopePresentationQuietTimer\.start\(\)[\s\S]*scopePresentationDeadlineTimer\.start\(\)/.test(mainSource))
		verify(/onContentHeightChanged:\s*\{[\s\S]*if \(scopePresentationPending\)[\s\S]*noteScopePresentationMutation\(\)[\s\S]*return/.test(mainSource))
		verify(/onContentHeightChanged:\s*\{[\s\S]*scopePresentationObservationActive\s*&&\s*stickToBottom[\s\S]*!dragging\s*&&\s*!flicking[\s\S]*setTailContentY\(maximumY\)[\s\S]*containTailMessageWhenPossible\(\)/.test(mainSource))
		verify(/onMovementStarted:\s*\{[\s\S]*const\s+explicitNavigation\s*=\s*dragging[\s\S]*if\s*\(!explicitNavigation\)[\s\S]*if\s*\(stickToBottom\)[\s\S]*requestBottomFollow\(\)[\s\S]*return/.test(mainSource))
		verify(!/onMovementStarted:\s*\{[\s\S]{0,900}scopePresentationObservationActive\s*&&\s*stickToBottom/.test(mainSource))
		verify(/onMovementEnded:\s*\{[\s\S]*if\s*\(!restoringBottom\)[\s\S]*if\s*\(!stickToBottom\)[\s\S]*stickToBottom\s*=\s*isNearBottom\(\)[\s\S]*requestBottomFollow\(\)/.test(mainSource))
		verify(/id:\s*scopePresentationQuietTimer[\s\S]*interval:\s*120[\s\S]*finishScopePresentation\(false\)/.test(mainSource))
		verify(/function\s+pendingScopeHydrationCount\(\)[\s\S]*contentNeedsHydration[\s\S]*inHydrationWindow/.test(mainSource))
		verify(/function\s+finishScopePresentation\(forcedByDeadline\)[\s\S]{0,420}scopePresentationFinalizing\s*=\s*true/.test(mainSource))
		verify(!/function\s+finishScopePresentation\(forcedByDeadline\)[\s\S]{0,260}pendingScopeHydrationCount\(\)\s*>\s*0/.test(mainSource))
		verify(/function\s+queueVisiblePreviewHydration\(allowDuringFinalization\)[\s\S]*pendingAllowed[\s\S]*scopePresentationFinalizing[\s\S]*inVisibleViewport[\s\S]*requestPreviewHydrationIfNeeded\(pendingAllowed\)/.test(mainSource))
		verify(/property bool coldPreviewHydrationPacingActive:\s*false[\s\S]*function\s+flushPreviewHydrationQueue\(\)[\s\S]*coldPreviewHydrationPacingActive[\s\S]*richLoaderResumeTimer\.running[\s\S]*richLoaderResumeQueue\.length\s*>\s*0[\s\S]*maxBatch\s*=\s*coldPreviewHydrationPacingActive\s*\?\s*1\s*:\s*32[\s\S]*batch\.length\s*<\s*maxBatch[\s\S]*coldPreviewHydrationPacingActive\s*=\s*false/.test(mainSource))
		verify(/required property bool bodyHydrationPending/.test(mainSource))
		verify(/property bool bodyNeedsHydration:\s*bodyHydrationPending[\s\S]*property bool contentNeedsHydration:[\s\S]*bodyNeedsHydration/.test(mainSource))
		verify(/property bool backendHydrationRequested:\s*false/.test(mainSource))
		verify(/function\s+requestPreviewHydrationIfNeeded\(allowDuringFinalization\)[\s\S]*pendingAllowed[\s\S]*backendContentNeedsHydration\s*&&\s*!backendHydrationRequested[\s\S]*richContentViewportEligible[\s\S]*!timeline\.scopePresentationPending\s*\|\|\s*pendingAllowed[\s\S]*queuePreviewHydration\(stableId,\s*inVisibleViewport\)[\s\S]*backendHydrationRequested\s*=\s*true/.test(mainSource))
		verify(/function\s+completeScopePresentationFinalization\(forcedByDeadline\)[\s\S]*scopePresentationPending\s*=\s*false[\s\S]*queueVisiblePreviewHydration\(\)/.test(mainSource))
		verify(/id:\s*scopePresentationDeadlineTimer[\s\S]*interval:\s*2000[\s\S]*finishScopePresentation\(true\)/.test(mainSource))
		verify(/function\s+noteScopePresentationMutation\(\)[\s\S]*scopePresentationFinalizing[\s\S]*scopePresentationFinalizeQuietTimer\.restart\(\)/.test(mainSource))
		verify(/function\s+finishScopePresentation\(forcedByDeadline\)[\s\S]*scopePresentationFinalizeStartTimer\.restart\(\)[\s\S]*function\s+continueScopePresentationFinalization\(\)[\s\S]*containTailMessageWhenPossible\(\)[\s\S]*scopePresentationFinalizeQuietTimer\.start\(\)[\s\S]*scopePresentationFinalizeDeadlineTimer\.start\(\)/.test(mainSource))
		verify(/id:\s*scopePresentationFinalizeStartTimer[\s\S]*interval:\s*16[\s\S]*timeline\.continueScopePresentationFinalization\(\)/.test(mainSource))
		verify(/function\s+completeScopePresentationFinalization\(forcedByDeadline\)[\s\S]*scopePresentationPending\s*=\s*false[\s\S]*coldPreviewHydrationPacingActive\s*=\s*true[\s\S]*scheduleRichContentResume\(\)[\s\S]*queueVisiblePreviewHydration\(\)/.test(mainSource))
		verify(/id:\s*scopePresentationFinalizeQuietTimer[\s\S]*interval:\s*16[\s\S]*completeScopePresentationFinalization\(false\)/.test(mainSource))
		verify(/id:\s*scopePresentationFinalizeDeadlineTimer[\s\S]*interval:\s*1500[\s\S]*completeScopePresentationFinalization\(true\)/.test(mainSource))
		verify(/function\s+completeScopePresentationFinalization\(forcedByDeadline\)[\s\S]*positionTailImmediately\(\)[\s\S]*containTailMessageWhenPossible\(\)[\s\S]*scopePresentationPending\s*=\s*false[\s\S]*scopePresentationObservationTimer\.restart\(\)/.test(mainSource))
		verify(/delegate:\s*DelegateChooser[\s\S]*id:\s*asyncMessageDelegate[\s\S]*required property bool deferredPresentation[\s\S]*opacity:\s*timeline\.scopePresentationContentVisible\s*\?\s*1\s*:\s*0[\s\S]*asynchronous:\s*true[\s\S]*active:\s*!deferredPresentation[\s\S]*sourceComponent:\s*Component\s*\{[\s\S]*ChatMessageFrame\s*\{/.test(mainSource))
		verify(/role:\s*"deferredPresentation"[\s\S]{0,180}roleValue:\s*true[\s\S]*id:\s*deferredInlineMessageDelegate[\s\S]*required property int deferredPresentationHeight[\s\S]*performanceTimelineDelegate:\s*true[\s\S]*lightweightDeferredPresentation:\s*true[\s\S]*contentNeedsHydration:\s*true[\s\S]*presentationSummary:\s*\[title,\s*subtitle,\s*timestamp\][\s\S]*height:\s*Math\.max\(timeline\.deferredMessageRowHeight,\s*deferredPresentationHeight\)/.test(mainSource))
		verify(/id:\s*deferredInlineMessageDelegate[\s\S]*text:\s*deferredInlineMessageDelegate\.title[\s\S]*text:\s*deferredInlineMessageDelegate\.subtitle[\s\S]*text:\s*deferredInlineMessageDelegate\.timestamp/.test(mainSource))
		const inlineDelegateStart =
			mainSource.indexOf("id: deferredInlineMessageDelegate")
		const inlineDelegateEnd =
			mainSource.indexOf("id: asyncMessageDelegate", inlineDelegateStart)
		verify(inlineDelegateStart >= 0 && inlineDelegateEnd > inlineDelegateStart)
		const inlineDelegateSource =
			mainSource.slice(inlineDelegateStart, inlineDelegateEnd)
		verify(/height:\s*Math\.max\(timeline\.deferredMessageRowHeight,\s*deferredPresentationHeight\)[\s\S]*color:\s*"transparent"[\s\S]*border\.width:\s*0/.test(
			inlineDelegateSource))
		verify(/RowLayout\s*\{[\s\S]*anchors\.left:\s*parent\.left[\s\S]*anchors\.right:\s*parent\.right[\s\S]*anchors\.top:\s*parent\.top[\s\S]*height:\s*Math\.min\(48,/.test(
			inlineDelegateSource))
		verify(!inlineDelegateSource.includes("anchors.fill: parent"))
		verify(!inlineDelegateSource.includes("completeDeferredMaterialization"))
		verify(!inlineDelegateSource.includes("releaseDeferredMaterialization"))
		verify(/id:\s*chatMessageFrameWarmupLoader[\s\S]*active:\s*true[\s\S]*asynchronous:\s*false[\s\S]*visible:\s*false[\s\S]*stableId:\s*"__shell_warmup__"[\s\S]*accessibilityBarrierEnabled:\s*false/.test(mainSource))
		verify(!/id:\s*chatMessageFrameWarmupLoader[\s\S]{0,360}active\s*=\s*false/.test(mainSource))
		verify(/id:\s*chatMessageDelegateWarmupModel[\s\S]*"stableId":\s*"__delegate_warmup__"[\s\S]*"preview":\s*\(\{\}\)[\s\S]*"attachments":\s*\[\][\s\S]*id:\s*chatMessageDelegateWarmupList[\s\S]*visible:\s*false[\s\S]*model:\s*chatMessageDelegateWarmupModel[\s\S]*delegate:\s*timeline\.delegate[\s\S]*Accessible\.ignored:\s*true/.test(mainSource))
		verify(/id:\s*asyncMessageDelegate[\s\S]*required property int deferredPresentationHeight[\s\S]*readonly property bool richGeometrySettling:[\s\S]*richResourceActivationPending[\s\S]*height:\s*Math\.max\(timeline\.deferredMessageRowHeight,[\s\S]*deferredPresentationHeight,[\s\S]*item\s*\?\s*item\.lightweightMessageRowHeight\s*:\s*0,[\s\S]*!richGeometrySettling\s*&&\s*item\s*\?\s*item\.implicitHeight\s*:\s*0\)[\s\S]*clip:\s*richGeometrySettling[\s\S]*function\s+resumeRichContentAfterScroll[\s\S]*resumeRequested\s*=\s*true[\s\S]*onLoaded:[\s\S]*item\.resumeRichContentAfterScroll/.test(mainSource))
		verify(/id:\s*asyncMessageDelegate[\s\S]*readonly property string presentationSummary:[\s\S]*\[title,\s*subtitle,\s*timestamp\][\s\S]*visible:\s*asyncMessageDelegate\.active[\s\S]*!asyncMessageDelegate\.item[\s\S]*!asyncMessageDelegate\.item\.currentMessagePresentationReady[\s\S]*richResourcesRequired\s*&&\s*!asyncMessageDelegate\.item\.richResourcesActive[\s\S]*richLoaderSuspended[\s\S]*richResourcesRequired[\s\S]*previewNeedsHydration[\s\S]*previewLoaderIncubationSuppressed[\s\S]*text:\s*asyncMessageDelegate\.title[\s\S]*text:\s*asyncMessageDelegate\.subtitle[\s\S]*text:\s*asyncMessageDelegate\.timestamp/.test(mainSource))
		verify(/accessibilitySuppressed:\s*root\.backgroundAccessibilitySuppressed[\s\S]*timeline\.scopePresentationPending[\s\S]*accessibilityBarrierEnabled:\s*logEntry\s*\|\|\s*messageRowReady/.test(mainSource))
		verify(/id:\s*emptyConversationState[\s\S]*visualLoading:\s*!activeScope\.activity\s*&&\s*\(activeScope\.loading[\s\S]*timeline\.scopePresentationPending[\s\S]*!timeline\.scopePresentationFinalizing[\s\S]*chatModel\.count\s*>\s*0[\s\S]*visible:[\s\S]*!timeline\.scopePresentationFinalizing[\s\S]*Preparing conversation/.test(mainSource))
		verify(/function\s+timelinePresentationState\(\)[\s\S]*"scopeResetPending":\s*timeline\.scopeResetPending[\s\S]*exposedHeightChangeCount[\s\S]*exposedTailCorrectionCount[\s\S]*visibleBlankPresentationCount[\s\S]*"visibleRows":\s*visiblePresentation\.rows[\s\S]*"settled":[\s\S]*visiblePresentation\.visibleCount\s*>\s*0[\s\S]*visiblePresentation\.blankCount\s*===\s*0/.test(mainSource))
		verify(/function\s+timelinePresentationState\(\)[\s\S]*"prependQueuePending":\s*chatModel\.prependPending[\s\S]*"settled":[\s\S]*!chatModel\.prependPending/.test(mainSource))
		verify(/bodyNeedsHydration[\s\S]*prependRichContentActive\(stableId,[\s\S]*chatModel\.requestBodyHydration\(stableId\)/.test(mainSource))
		verify(/function\s+queueVisibleDeferredMaterialization\(allowDuringFinalization\)[\s\S]*pendingAllowed[\s\S]*scopePresentationFinalizing[\s\S]*item\.lightweightDeferredPresentation\s*!==\s*true[\s\S]*item\.accessibilityPooled\s*===\s*true[\s\S]*item\.contentNeedsHydration\s*!==\s*true[\s\S]*item\.mapToItem\(timeline,[\s\S]*point\.y\s*\+\s*item\.height\s*<=\s*0\.5[\s\S]*deferredRepeater[\s\S]*item\.mapToItem\(timeline,[\s\S]*point\.y\s*\+\s*item\.height\s*<=\s*0\.5[\s\S]*deferredMaterializationTimer\.start\(\)/.test(mainSource))
		const visibleMaterializationStart =
			mainSource.indexOf("function queueVisibleDeferredMaterialization")
		const visibleMaterializationEnd =
			mainSource.indexOf("function queueVisiblePreviewHydration",
				visibleMaterializationStart)
		verify(visibleMaterializationStart >= 0
			&& visibleMaterializationEnd > visibleMaterializationStart)
		const visibleMaterializationSource =
			mainSource.slice(visibleMaterializationStart, visibleMaterializationEnd)
		verify(/const\s+candidates\s*=\s*\[\]/.test(visibleMaterializationSource))
		verify(!/deferredMaterializationQueue\.slice\(\)/.test(
			visibleMaterializationSource))
		verify(!/\bitem\.y\s*\+\s*item\.height|\bdeferredHeaderVisible\b|\bheaderVisible\b|\brequestedItem\b/.test(
			visibleMaterializationSource))
		verify(/coldScopeMaterializationDelayMs:\s*0[\s\S]*function\s+beginScopeChange\(\)[\s\S]*coldScopeMaterializationDelayTimer\.restart\(\)/.test(mainSource))
		verify(/function\s+scheduleRichContentResume\(\)[\s\S]*coldScopeMaterializationDelayTimer\.running[\s\S]*richContentResumeTimer\.restart\(\)/.test(mainSource))
		verify(/function\s+completeScopePresentationFinalization\(forcedByDeadline\)[\s\S]*coldScopeMaterializationDelayTimer\.running[\s\S]*forceLayout\(\)[\s\S]*positionTailImmediately\(\)[\s\S]*queueVisibleDeferredMaterialization\(true\)[\s\S]*queueRichLoaderResumes\(\)[\s\S]*visiblePresentationDiagnostics\(\)[\s\S]*visiblePresentationReady\(diagnostics\)[\s\S]*scopePresentationFinalizeQuietTimer\.restart\(\)/.test(mainSource))
		verify(/function\s+visiblePresentationReady\(diagnostics\)[\s\S]*visibleCount\s*>\s*0[\s\S]*readyCount\s*===\s*diagnostics\.visibleCount[\s\S]*fullMessageCount\s*===\s*diagnostics\.visibleCount[\s\S]*deferredSummaryCount\s*===\s*0[\s\S]*blankCount\s*===\s*0[\s\S]*inactiveRichResourceCount\s*===\s*0/.test(mainSource))
		const continueStart = mainSource.indexOf("function continueScopePresentationFinalization")
		const continueEnd = mainSource.indexOf("function completeScopePresentationFinalization", continueStart)
		verify(continueStart >= 0 && continueEnd > continueStart)
		verify(/queueVisibleDeferredMaterialization\(true\)/.test(
			mainSource.slice(continueStart, continueEnd)))
		verify(!/queueRichLoaderResumes|queueVisiblePreviewHydration/.test(
			mainSource.slice(continueStart, continueEnd)))
		const finalizeStart = mainSource.indexOf("function completeScopePresentationFinalization")
		const finalizeEnd = mainSource.indexOf("const generation =", finalizeStart)
		verify(finalizeStart >= 0 && finalizeEnd > finalizeStart)
		verify(!/deferredMaterializationTimer\.running|deferredMaterializationQueue\.length/.test(
			mainSource.slice(finalizeStart, finalizeEnd)))
		verify(!/deferredMaterializationActiveId\s*=/.test(
			mainSource.slice(finalizeStart, finalizeEnd)))
		verify(/scopePresentationFinalizeDeadlineTimer\.stop\(\)[\s\S]*deferredMaterializationTimer\.stop\(\)[\s\S]*deferredMaterializationQueue\s*=\s*\[\][\s\S]*richScrollRetentionTarget\s*=\s*Math\.min\([\s\S]*contentHeight\s*\+\s*timeline\.height[\s\S]*richScrollRetentionBuffer\s*=[\s\S]*richScrollRetentionTarget[\s\S]*scopePresentationPending\s*=\s*false/.test(mainSource))
		verify(/id:\s*coldScopeMaterializationDelayTimer[\s\S]*interval:\s*timeline\.coldScopeMaterializationDelayMs[\s\S]*scopePresentationFinalizing[\s\S]*queueVisibleDeferredMaterialization\(true\)[\s\S]*scopePresentationFinalizeQuietTimer\.restart\(\)[\s\S]*timeline\.scheduleRichContentResume\(\)/.test(mainSource))
		const rowsInsertedStart = mainSource.indexOf("function onRowsInserted(parentIndex, first, last)")
		const rowsInsertedEnd = mainSource.indexOf("function onModelReset", rowsInsertedStart)
		verify(rowsInsertedStart >= 0 && rowsInsertedEnd > rowsInsertedStart)
		verify(!/coldScopeMaterializationDelayTimer\.(?:start|restart)\(\)/.test(
			mainSource.slice(rowsInsertedStart, rowsInsertedEnd)))
		verify(/function\s+queueVisibleDeferredMaterialization\(allowDuringFinalization\)[\s\S]*candidates\.sort[\s\S]*right\.viewportY\s*-\s*left\.viewportY[\s\S]*deferredMaterializationQueue\s*=\s*queued/.test(mainSource))
		verify(/function\s+deferredPresentationStillEligible\(stableId\)[\s\S]*lightweightDeferredPresentation\s*!==\s*true[\s\S]*accessibilityPooled\s*===\s*true[\s\S]*mapToItem\(timeline,[\s\S]*deferredRepeater[\s\S]*String\(item\.stableId\s*\|\|\s*""\)\s*!==\s*key[\s\S]*return\s+point\.y\s*\+\s*item\.height\s*>\s*0\.5[\s\S]*return\s+false/.test(mainSource))
		verify(/property string deferredMaterializationActiveId:\s*""[\s\S]*function\s+queueVisibleDeferredMaterialization\(allowDuringFinalization\)[\s\S]*deferredMaterializationActiveId\.length\s*>\s*0/.test(mainSource))
		verify(/deferredMaterializationsPerFrame:\s*scopePresentationFinalizing\s*\?\s*1\s*:\s*2[\s\S]*function\s+materializeNextDeferredRow\(\)[\s\S]*serializeSourcePromotion\s*=\s*scopePresentationFinalizing[\s\S]*materialized\s*<\s*deferredMaterializationsPerFrame[\s\S]*deferredPresentationStillEligible\(stableId\)[\s\S]*if\s*\(serializeSourcePromotion\)[\s\S]*deferredMaterializationActiveId\s*=\s*stableId[\s\S]*chatModel\.materializeDeferredMessage\(stableId\)[\s\S]*deferredMaterializationQueue\s*=\s*\[\][\s\S]*topAnchorActive[\s\S]*topAnchorSettleTimer\.restart\(\)[\s\S]*topAnchorFrameTimer\.restart\(\)[\s\S]*finalizationGeneration[\s\S]*scopePresentationGeneration[\s\S]*!timeline\.scopePresentationPending[\s\S]*!timeline\.scopePresentationFinalizing[\s\S]*queueRichLoaderResumes\(\)[\s\S]*!timeline\.scopePresentationFinalizing[\s\S]*queueVisibleDeferredMaterialization\(false\)/.test(mainSource))
		verify(/function\s+materializeNextDeferredRow\(\)[\s\S]*deferredMaterializationQueue\s*=\s*\[\][\s\S]*if\s*\(materialized\s*===\s*0\)[\s\S]*finishRichContentRecoveryIfIdle\(\)[\s\S]*return[\s\S]*const\s+finalizationGeneration/.test(mainSource))
		verify(/function\s+completeDeferredMaterialization\(stableId\)[\s\S]*key\s*!==\s*deferredMaterializationActiveId[\s\S]*deferredMaterializationActiveId\s*=\s*""[\s\S]*positionTailImmediately\(\)[\s\S]*queueVisibleDeferredMaterialization\(/.test(mainSource))
		verify(/function\s+completeDeferredMaterialization\(stableId\)[\s\S]*if\s*\(key\.length\s*===\s*0\s*\|\|\s*key\s*!==\s*deferredMaterializationActiveId\)\s*return\s*false/.test(mainSource))
		verify(/onContentHeightChanged:[\s\S]*scopePresentationPending[\s\S]*scopePresentationFinalizing\s*&&\s*stickToBottom[\s\S]*positionTailImmediately\(\)/.test(mainSource))
		verify(/function\s+requestBottomFollow\(\)[\s\S]*const maximumY\s*=\s*Math\.max\([\s\S]*!scopeResetPending[\s\S]*Math\.abs\(maximumY\s*-\s*contentY\)\s*<=\s*0\.5[\s\S]*markVisibleScopeReadIfEligible\(\)[\s\S]*return[\s\S]*bottomFollowTimer\.start\(\)/.test(mainSource))
		const deferredStart = mainSource.indexOf("id: deferredHistoryRow")
		const deferredEnd = mainSource.indexOf("DelegateChoice {", deferredStart + 1)
		verify(deferredStart >= 0 && deferredEnd > deferredStart)
		const deferredDelegateSource = mainSource.slice(deferredStart, deferredEnd)
		verify(/required property string stableId[\s\S]*contentNeedsHydration:\s*true/.test(deferredDelegateSource))
		verify(!/\bLoader\s*\{/.test(deferredDelegateSource))
		verify(!/\bConnections\s*\{/.test(deferredDelegateSource))
	}

	function test_chat_normalizes_qabstractitemmodel_list_roles_before_rendering() {
		verify(/function\s+normalizedModelList\(value\)[\s\S]*Array\.isArray\(value\)[\s\S]*value\.count\s*!==\s*undefined[\s\S]*typeof value\.get\s*===\s*"function"[\s\S]*result\.push\(value\.get\(index\)\)/.test(mainSource))
		verify(/property var reactions:\s*root\.normalizedModelList\(asyncMessageDelegate\.reactions\)/.test(mainSource))
		verify(/property var bodySegments:\s*root\.normalizedModelList\(asyncMessageDelegate\.bodySegments\)/.test(mainSource))
		verify(/property var attachments:\s*root\.normalizedModelList\(asyncMessageDelegate\.attachments\)/.test(mainSource))
		verify(/readonly property bool attachmentNeedsHydration:\s*\(attachments\s*\|\|\s*\[\]\)\.some/.test(mainSource))
	}

	function test_activity_is_a_read_only_log_surface_without_conversation_loading_chrome() {
		verify(/function\s+canCompleteScopePresentationFastPath\(\)[\s\S]*if\s*\(activeScope\.activity\)[\s\S]*return\s+true/.test(mainSource))
		verify(/id:\s*emptyConversationState[\s\S]*activeScope\.activity\s*\?\s*chatModel\.count\s*===\s*0[\s\S]*qsTr\("No server log entries yet"\)[\s\S]*qsTr\("New Murmur server-log entries will appear here live\."\)/.test(mainSource))
		verify(/id:\s*composerSurface[\s\S]*visible:\s*!activeScope\.activity[\s\S]*Layout\.preferredHeight:\s*visible\s*\?/.test(mainSource))
		verify(/id:\s*activityLogColumnHeader[\s\S]{0,2400}visible:\s*activeScope\.activity[\s\S]{0,1200}qsTr\("TIME"\)[\s\S]{0,800}qsTr\("EVENT"\)[\s\S]{0,1200}qsTr\("LIVE"\)/.test(mainSource))
		verify(/id:\s*timeline[\s\S]{0,760}Accessible\.name:\s*activeScope\.activity\s*\?\s*qsTr\("Activity log"\)/.test(mainSource))
		verify(/function\s+stableAlternatingRow\(stableId\)[\s\S]*parity\s*\^=\s*value\.charCodeAt\(index\)\s*&\s*1[\s\S]*return\s+parity\s*===\s*1/.test(mainSource))
		verify(/ChatMessageFrame\s*\{[\s\S]{0,400}logEntry:\s*activeScope\.activity[\s\S]{0,200}logAlternating:\s*logEntry\s*&&\s*root\.stableAlternatingRow\(stableId\)/.test(mainSource))
		verify(!/id:\s*messageDelegate[\s\S]{0,360}required property int index/.test(mainSource))
		verify(/toggleQuickReactions\(messageDelegate\.stableId,[\s\S]{0,180}chatModel\.presentationModel\.rowForStableId\([\s\S]{0,80}messageDelegate\.stableId\)/.test(mainSource))
		verify(/id:\s*activityLogRowLoader[\s\S]{0,180}active:\s*messageDelegate\.logEntry[\s\S]{0,180}asynchronous:\s*true[\s\S]{0,200}id:\s*activityLogRow[\s\S]{0,1000}objectName:\s*"activityLogTimestamp"[\s\S]{0,1400}objectName:\s*"activityLogGutter"[\s\S]{0,1600}objectName:\s*"activityLogEvent"/.test(mainSource))
		verify(/function\s+resumeRichContentAfterScroll\(allowDuringFinalization,\s*admitPreviewMaterialization\)[\s\S]*!timeline\.richScrollInProgress[\s\S]*!logEntry\s*&&\s*richContentViewportEligible[\s\S]*messageRowMaterialized\s*=\s*true/.test(mainSource))
		verify(/id:\s*messageRowLoader[\s\S]{0,260}active:\s*!messageDelegate\.logEntry[\s\S]*messageDelegate\.messageRowMaterialized/.test(mainSource))
		verify(!/active:\s*!messageDelegate\.logEntry\s*&&\s*\(messageDelegate\.messageRowMaterialized[\s\S]{0,160}!timeline\.richScrollInProgress[\s\S]{0,120}messageDelegate\.inVisibleViewport\)/.test(mainSource))
	}

	function test_composer_keeps_its_destination_visible_while_typing() {
		verify(/objectName:\s*"composerTargetRow"[\s\S]*objectName:\s*"composerTargetLabel"[\s\S]*qsTr\("To: %1"\)\.arg\(activeScope\.label\)/.test(mainSource))
		verify(/objectName:\s*"composerTargetKindLabel"[\s\S]*text:\s*activeScope\.kindLabel/.test(mainSource))
		verify(/Accessible\.description:\s*qsTr\("Sending to %1\. %2"\)[\s\S]*arg\(activeScope\.label\)\.arg\(activeScope\.composerHint\)/.test(mainSource))
	}

	function test_empty_and_lightweight_scopes_skip_the_presentation_delay() {
		verify(/function\s+scheduleScopePresentationFastPath\(\)[\s\S]*scopePresentationFastPathTimer\.restart\(\)/.test(mainSource))
		verify(/function\s+rowNeedsDeferredPresentation\(row\)[\s\S]*bodyHydrationPending[\s\S]*Object\.keys\(preview\)[\s\S]*attachments\.length/.test(mainSource))
		verify(/function\s+canCompleteScopePresentationFastPath\(\)[\s\S]*activeScope\.loading[\s\S]*chatModel\.count\s*===\s*0[\s\S]*return\s+true[\s\S]*count\s*===\s*0[\s\S]*return\s+false[\s\S]*contentHeight\s*>\s*height\s*\+\s*0\.5[\s\S]*itemAtIndex\(0\)[\s\S]*itemAtIndex\(count\s*-\s*1\)/.test(mainSource))
		verify(/function\s+canCompleteScopePresentationFastPath\(\)[\s\S]*rowNeedsDeferredPresentation\(chatModel\.get\(index\)\)[\s\S]*return\s+false[\s\S]*forceLayout\(\)/.test(mainSource))
		verify(/function\s+completeScopePresentationFastPath\(\)[\s\S]*scopePresentationQuietTimer\.stop\(\)[\s\S]*scopeResetPending\s*=\s*false[\s\S]*scopePresentationFastPath\s*=\s*true[\s\S]*scopePresentationPending\s*=\s*false/.test(mainSource))
		verify(/id:\s*scopePresentationFastPathTimer[\s\S]*interval:\s*0[\s\S]*completeScopePresentationFastPath\(\)/.test(mainSource))
		verify(/function\s+onAppendPendingChanged\(\)[\s\S]*!chatModel\.appendPending[\s\S]*scheduleScopePresentationFastPath\(\)[\s\S]*timeline\.scopePresentationPending[\s\S]*!timeline\.scopePresentationFinalizing[\s\S]*!activeScope\.loading[\s\S]*timeline\.finishScopePresentation\(false\)/.test(mainSource))
		verify(/function\s+onLoadingChanged\(\)[\s\S]*scheduleScopePresentationFastPath\(\)/.test(mainSource))
		verify(/function\s+onCountChanged\(\)[\s\S]*scheduleScopePresentationFastPath\(\)/.test(mainSource))
		verify(/"presentationFastPath":\s*timeline\.scopePresentationFastPath/.test(mainSource))
	}

	function test_performance_chat_scroll_is_frame_driven_without_a_qml_animation() {
		verify(/function\s+preparePerformanceChatScrollWorkload\(stepCount,\s*fullRangeToStart\)[\s\S]*bottomFollowTimer\.stop\(\)[\s\S]*timeline\.stickToBottom\s*=\s*false[\s\S]*performanceChatScrollRunning\s*=\s*true/.test(mainSource))
		verify(/fullRangeToStart\s*===\s*true[\s\S]{0,280}performanceChatScrollTargetY\s*=\s*minimumY/.test(mainSource))
		verify(/performanceChatScrollFullRangeToStart[\s\S]*currentOriginY\s*=\s*timeline\.originY[\s\S]*Math\.max\(currentOriginY,\s*previousY\s*-\s*wheelStep\)[\s\S]*reachedTarget/.test(mainSource))
		verify(/const\s+availableUp\s*=\s*Math\.max\(0,\s*timeline\.contentY\s*-\s*minimumY\)[\s\S]*const\s+availableDown\s*=\s*Math\.max\(0,\s*maximumY\s*-\s*timeline\.contentY\)[\s\S]*maximumDirectionalTravel/.test(mainSource))
		verify(/const\s+minimumGestureTravel\s*=\s*Math\.min\(80,\s*desiredGestureTravel\)[\s\S]*maximumDirectionalTravel[\s\S]*Math\.max\(requestedSteps\s*\*\s*2,\s*minimumGestureTravel\)/.test(mainSource))
		verify(/const travelDistance\s*=\s*Math\.min\(maximumDirectionalTravel,[\s\S]*desiredGestureTravel\)[\s\S]*const\s+scrollUp\s*=\s*availableUp\s*>=\s*travelDistance[\s\S]*availableDown\s*<\s*travelDistance[\s\S]*performanceChatScrollTargetY\s*=\s*scrollUp/.test(mainSource))
		verify(/function\s+advancePerformanceChatScrollWorkload\(step,\s*totalSteps\)[\s\S]*timeline\.contentY\s*=\s*performanceChatScrollStartY/.test(mainSource))
		verify(mainSource.includes("\"reachedTarget\": Math.abs(timeline.contentY"))
		verify(mainSource.includes("- performanceChatScrollTargetY) <= 0.5"))
		verify(/requestedSteps\s*\*\s*2/.test(mainSource))
		verify(/const liveRangeUnavailable\s*=\s*fullRangeToStart\s*===\s*true[\s\S]*timeline\.count\s*<\s*2[\s\S]*loadedTravel\s*<\s*8[\s\S]*const fixtureRangeUnavailable\s*=\s*fullRangeToStart\s*!==\s*true[\s\S]*timeline\.count\s*<\s*2/.test(mainSource))
		verify(/function\s+completePerformanceChatScrollWorkload\(\)[\s\S]*timeline\.captureViewportAnchor\(\)[\s\S]*performanceChatScrollRunning\s*=\s*false[\s\S]*Qt\.callLater[\s\S]*timeline\.scheduleRichContentResume\(\)/.test(mainSource))
		verify(!/id:\s*timelineScrollWorkload/.test(mainSource))
	}

	function test_history_load_probe_requires_a_complete_real_scroll_to_the_origin() {
		const probeMatch = /function\s+prepareHistoryLoadPerformanceProbe\(\)\s*\{([\s\S]*?)\n\t\}/.exec(mainSource)
		verify(probeMatch !== null)
		const probeSource = probeMatch[1]
		verify(/Math\.abs\(timeline\.contentY\s*-\s*timeline\.originY\)\s*>\s*0\.5/.test(probeSource))
		verify(/has not been scrolled completely to the start/.test(probeSource))
		verify(!/positionViewAtIndex/.test(probeSource))
		verify(!/currentIndex\s*=/.test(probeSource))
	}

	function test_older_history_request_captures_the_visible_anchor_before_loading_state_changes() {
		verify(/function\s+requestOlderMessagesPreservingAnchor\(\)[\s\S]*capturePrependAnchor\(\)[\s\S]*deferredPrependAnchorActive\s*=\s*timeline\.prependMutationInProgress[\s\S]*uiCommands\.requestOlderMessages\(\)/.test(mainSource))
		verify(/function\s+restoreDeferredPrependAnchor\(\)[\s\S]*restorePrependAnchor\(\)/.test(mainSource))
		verify(/actionId\s*===\s*"activeScope\.loadOlder"[\s\S]{0,120}root\.requestOlderMessagesPreservingAnchor\(\)/.test(mainSource))
		verify(/Loading older messages[\s\S]*onClicked:\s*root\.requestOlderMessagesPreservingAnchor\(\)/.test(mainSource))
	}

	function test_chat_scroll_materializes_only_rows_that_have_heavy_content() {
		verify(/id:\s*timeline[\s\S]*model:\s*chatModel\.presentationModel[\s\S]*reuseItems:\s*true[\s\S]*baseCacheBuffer:[\s\S]*height\s*\*\s*0\.75[\s\S]*richScrollRetentionBuffer:\s*0[\s\S]*richScrollRetentionTarget:\s*0[\s\S]*richScrollRetentionRampStep:\s*128[\s\S]*maximumRichScrollRetentionBuffer:\s*18000[\s\S]*cacheBuffer:\s*scopePresentationPending[\s\S]*maximumRichScrollRetentionBuffer[\s\S]*Math\.max\(baseCacheBuffer,\s*richScrollRetentionBuffer\)/.test(mainSource))
		verify(/onRichScrollInProgressChanged:[\s\S]*if\s*\(richScrollInProgress\)[\s\S]*richScrollRetentionTarget\s*=\s*Math\.min\(maximumRichScrollRetentionBuffer,[\s\S]*contentHeight\s*\+\s*height[\s\S]*richScrollRetentionRampTimer\.start\(\)/.test(mainSource))
		verify(/function\s+beginScopeChange\(\)[\s\S]*richScrollRetentionRampTimer\.stop\(\)[\s\S]*richScrollRetentionBuffer\s*=\s*0[\s\S]*richScrollRetentionTarget\s*=\s*0/.test(mainSource))
		verify(/function\s+beginScopeChange\(\)[\s\S]{0,900}chatModel\.presentationBusy\s*=\s*true/.test(mainSource))
		verify(/function\s+beginScopeChange\(\)[\s\S]*releaseTopAnchor\(\)[\s\S]*richContentResumeTimer\.stop\(\)[\s\S]*richLoaderResumeTimer\.stop\(\)[\s\S]*richLoaderResumeQueue\s*=\s*\[\][\s\S]*deferredMaterializationTimer\.stop\(\)[\s\S]*deferredMaterializationQueue\s*=\s*\[\]/.test(mainSource))
		verify(/function\s+completeScopePresentationFastPath\(\)[\s\S]*scopePresentationPending\s*=\s*false[\s\S]*chatModel\.presentationBusy\s*=\s*false/.test(mainSource))
		verify(/function\s+finishRichContentRecoveryIfIdle\(\)[\s\S]{0,220}if\s*\(scopePresentationPending\)[\s\S]{0,80}return/.test(mainSource))
		verify(/id:\s*richScrollRetentionRampTimer[\s\S]*interval:\s*16[\s\S]*repeat:\s*true[\s\S]*richScrollRetentionBuffer\s*=\s*Math\.min\([\s\S]*richScrollRetentionRampStep[\s\S]*stop\(\)/.test(mainSource))
		const viewportMembershipFunctionsStart =
			mainSource.indexOf("function updateViewportMemberships()")
		const viewportMembershipFunctionsEnd =
			mainSource.indexOf("function nearViewportPrefetchSignature()",
				viewportMembershipFunctionsStart)
		verify(viewportMembershipFunctionsStart >= 0
			&& viewportMembershipFunctionsEnd > viewportMembershipFunctionsStart)
		const viewportMembershipFunctionsSource = mainSource.slice(
			viewportMembershipFunctionsStart, viewportMembershipFunctionsEnd)
		verify(/function\s+updateViewportMemberships\(\)[\s\S]*if\s*\(!contentItem\)\s*return[\s\S]*const children\s*=\s*contentItem\.children[\s\S]*item\.performanceTimelineDelegate\s*===\s*true[\s\S]*item\.updateViewportMembership[\s\S]*item\.updateViewportMembership\(\)[\s\S]*function\s+scheduleViewportMembershipUpdate\(\)[\s\S]*viewportMembershipUpdatePending\s*=\s*true[\s\S]*!viewportMembershipTimer\.running[\s\S]*viewportMembershipTimer\.start\(\)/.test(
			viewportMembershipFunctionsSource))
		verify(/id:\s*viewportMembershipTimer[\s\S]*interval:\s*0[\s\S]*repeat:\s*false[\s\S]*onTriggered:\s*\{\s*timeline\.viewportMembershipUpdatePending\s*=\s*false\s*timeline\.updateViewportMemberships\(\)[\s\S]*Qt\.callLater\(function\(\)\s*\{\s*if\s*\(timeline\.viewportMembershipUpdatePending\)[\s\S]*!viewportMembershipTimer\.running[\s\S]*viewportMembershipTimer\.start\(\)\s*return\s*\}\s*if\s*\(!timeline\.richScrollInProgress\)\s*timeline\.scheduleRichContentResume\(\)\s*timeline\.scheduleNearViewportPrefetch\(\)/.test(
			mainSource))
		verify(/id:\s*viewportMembershipTimer[\s\S]*if\s*\(!timeline\.richScrollInProgress\)\s*timeline\.scheduleRichContentResume\(\)[\s\S]*timeline\.scheduleNearViewportPrefetch\(\)/.test(
			mainSource))
		verify(/"settled":[\s\S]*!viewportMembershipTimer\.running[\s\S]*!timeline\.viewportMembershipUpdatePending/.test(
			mainSource))
		verify(/function\s+nearViewportPrefetchAllowed\(\)[\s\S]*viewportMembershipTimer\.running\s*!==\s*true[\s\S]*viewportMembershipUpdatePending\s*!==\s*true/.test(
			mainSource))
		verify(/if\s*\(viewportMembershipTimer\.running\s*\|\|\s*(?:timeline\.)?viewportMembershipUpdatePending\)\s*\{\s*scopePresentationFinalizeQuietTimer\.restart\(\)\s*return/.test(
			mainSource))
		verify(/onContentYChanged:\s*\{\s*scheduleViewportMembershipUpdate\(\)/.test(
			mainSource))
		verify(/onContentHeightChanged:\s*\{\s*scheduleViewportMembershipUpdate\(\)/.test(
			mainSource))
		verify(/onHeightChanged:\s*scheduleViewportMembershipUpdate\(\)/.test(
			mainSource))
		verify(/onOriginYChanged:\s*scheduleViewportMembershipUpdate\(\)/.test(
			mainSource))
		verify(/function onCountChanged\(\)\s*\{\s*timeline\.scheduleViewportMembershipUpdate\(\)/.test(
			mainSource))
		verify(/onScopeResetPendingChanged:\s*scheduleViewportMembershipUpdate\(\)/.test(
			mainSource))
		verify(/id:\s*deferredHistoryRepeater[\s\S]*model:\s*chatModel\.deferredPresentationModel[\s\S]*required property int deferredPresentationHeight[\s\S]*deferredPrefixItem:\s*true[\s\S]*contentNeedsHydration:\s*true/.test(mainSource))
		verify(/id:\s*deferredHistoryRow[\s\S]{0,900}summaryText:\s*\[title,\s*subtitle,\s*timestamp\][\s\S]{0,260}join\("  ·  "\)[\s\S]{0,900}text:\s*deferredHistoryRow\.summaryText/.test(mainSource))
		verify(!/id:\s*deferredHistoryRow[\s\S]{0,350}required property var source/.test(mainSource))
		verify(!/id:\s*deferredHistoryRow[\s\S]{0,1600}ColumnLayout\s*\{/.test(mainSource))
		verify(/performanceChatScrollFullRangeToStart[\s\S]*currentOriginY\s*=\s*timeline\.originY[\s\S]*const wheelStep\s*=\s*Math\.max\(80,\s*Math\.min\(112,\s*timeline\.height \* 0\.14\)\)/.test(mainSource))
		verify(/id:\s*timelineScrollHandler[\s\S]*targetFlickable:\s*timeline[\s\S]*horizontalEnabled:\s*false[\s\S]*smoothWheelEnabled:\s*true[\s\S]*wheelStep:\s*Math\.max\(80,\s*Math\.min\(112,\s*timeline\.height \* 0\.14\)\)/.test(mainSource))
		verify(/id:\s*timelineScrollHandler[\s\S]*onScrollingStarted:[\s\S]*releasePrependAnchor\(\)[\s\S]*releaseViewportAnchor\(\)[\s\S]*releaseTopAnchor\(\)/.test(mainSource))
		const scrollbarStart = mainSource.indexOf("id: timelineScrollBar")
		const scrollbarEnd = mainSource.indexOf("MiddleDragScrollHandler", scrollbarStart)
		verify(scrollbarStart >= 0 && scrollbarEnd > scrollbarStart)
		verify(/onPressedChanged:\s*\{[\s\S]*if\s*\(!timeline\.stickToBottom\)[\s\S]*timeline\.captureViewportAnchor\(\)[\s\S]*timeline\.scheduleViewportMembershipUpdate\(\)/.test(
			mainSource.slice(scrollbarStart, scrollbarEnd)))
		verify(/property bool richScrollInProgress:[\s\S]*timelineScrollHandler\.scrolling[\s\S]*performanceChatScrollRunning/.test(mainSource))
		verify(/onRichScrollInProgressChanged:[\s\S]*if\s*\(richScrollInProgress\)[\s\S]*chatModel\.presentationBusy\s*=\s*true[\s\S]*richLoaderResumeTimer\.stop\(\)[\s\S]*deferredMaterializationTimer\.stop\(\)[\s\S]*suspendRichContentForScroll\(\)/.test(mainSource))
		const richScrollChangeStart =
			mainSource.indexOf("onRichScrollInProgressChanged:")
		const richScrollChangeEnd =
			mainSource.indexOf("property string prependAnchorId", richScrollChangeStart)
		verify(richScrollChangeStart >= 0 && richScrollChangeEnd > richScrollChangeStart)
		const richScrollChangeSource =
			mainSource.slice(richScrollChangeStart, richScrollChangeEnd)
		verify(/if\s*\(richScrollInProgress\)[\s\S]*cancelActiveNearViewportPrefetchForScroll\(\)[\s\S]*suspendRichContentForScroll\(\)/.test(
			richScrollChangeSource))
		verify(/onRichScrollInProgressChanged:[\s\S]*if\s*\(richScrollInProgress\)[\s\S]*previewHydrationTimer\.stop\(\)[\s\S]*Object\.keys\(root\.pendingPreviewHydrationIds\)\.length\s*>\s*0[\s\S]*previewHydrationTimer\.start\(\)/.test(mainSource))
		const suspendMatch = /function\s+suspendRichContentForScroll\(\)\s*\{([\s\S]*?)\n\t\t\t\t\t\}/.exec(mainSource)
		verify(suspendMatch !== null)
		verify(/richLoaderResumeQueue\s*=\s*\[\][\s\S]*item\.inVisibleViewport\s*===\s*true[\s\S]*continue[\s\S]*item\.suspendRichResourcesForScroll\(\)/.test(suspendMatch[1]))
		verify(!/item\.richLoaderSuspended\s*=\s*true/.test(suspendMatch[1]))
		verify(!/item\.richResourcesActive\s*=/.test(suspendMatch[1]))
		verify(/item\.richLoaderSuspended\s*!==\s*true[\s\S]*item\.messageRowReady\s*===\s*true/.test(mainSource))
		verify(/function\s+richResourceRecoveryPending\(item\)[\s\S]*item\.richResourcesRequired\s*===\s*true[\s\S]*item\.contentNeedsHydration\s*!==\s*true[\s\S]*item\.richResourcesActive\s*!==\s*true/.test(mainSource))
		verify(/function\s+pendingVisibleHydrationCount\(\)[\s\S]*item\.contentNeedsHydration\s*===\s*true[\s\S]*item\.inVisibleViewport\s*===\s*true[\s\S]*item\.accessibilityPooled\s*!==\s*true/.test(mainSource))
		verify(/"pendingVisibleHydrationCount":[\s\S]*timeline\.pendingVisibleHydrationCount\(\)/.test(mainSource))
		verify(/function\s+pendingVisibleRichRecoveryCount\(\)[\s\S]*richResourceRecoveryPending\(item\)/.test(mainSource))
		verify(/function\s+timelinePresentationState\(\)[\s\S]*const\s+pendingVisibleRichRecoveryCount\s*=[\s\S]*timeline\.pendingVisibleRichRecoveryCount\(\)/.test(mainSource))
		verify(/"pendingVisibleRichRecoveryCount":\s*[\s\S]*pendingVisibleRichRecoveryCount/.test(mainSource))
		verify(/"settled":[\s\S]*pendingVisibleRichRecoveryCount\s*===\s*0/.test(mainSource))
		verify(/function\s+scheduleRichContentResume\(\)[\s\S]*richContentResumeTimer\.restart\(\)[\s\S]*function\s+queueRichLoaderResumes\(\)[\s\S]*candidates\.sort[\s\S]*richLoaderResumeQueue/.test(mainSource))
		verify(/function\s+scheduleRichContentResume\(\)[\s\S]*if\s*\(richScrollInProgress\s*\|\|\s*coldScopeMaterializationDelayTimer\.running\)\s*return/.test(mainSource))
		verify(/const visible = point\.y \+ item\.height > 0\.5[\s\S]*prefetchEligible[\s\S]*nearViewportWarmId[\s\S]*if \(!visible && !prefetchEligible\)\s*continue[\s\S]*candidates\.push/.test(mainSource))
		verify(/function\s+queueRichLoaderResumes\(\)[\s\S]*item\.messageRowReady\s*===\s*true[\s\S]*item\.previewPresentationReady\s*===\s*true[\s\S]*!richResourceRecoveryPending\(item\)[\s\S]*stableId\s*===\s*richLoaderResumeActiveId/.test(mainSource))
		verify(/function\s+resumeNextRichLoader\(\)[\s\S]*resumeRichContentAfterScroll\([\s\S]*scopePresentationFinalizing[\s\S]*finishRichContentRecoveryIfIdle\(\)[\s\S]*id:\s*richLoaderResumeTimer[\s\S]*interval:\s*16/.test(mainSource))
		verify(/function\s+resumeNextRichLoader\(\)[\s\S]*resumeRichContentAfterScroll\(\s*timeline\.scopePresentationFinalizing,\s*true\)/.test(mainSource))
		verify(/property string richLoaderResumeActiveId:\s*""/.test(mainSource))
		verify(/function\s+resumeNextRichLoader\(\)[\s\S]*if\s*\(richLoaderResumeActiveId\.length\s*>\s*0\)[\s\S]*stillVisible[\s\S]*prefetchEligible[\s\S]*if\s*\(!stillVisible\s*&&\s*!prefetchEligible\)[\s\S]*break[\s\S]*richLoaderResumeActiveId\s*=\s*stableId[\s\S]*resumeRichContentAfterScroll[\s\S]*!==\s*false[\s\S]*!resumed[\s\S]*richLoaderResumeActiveId\s*=\s*""[\s\S]*item\.messageRowReady\s*===\s*true/.test(mainSource))
		verify(/function\s+completeRichLoaderResume\(stableId\)[\s\S]*richLoaderResumeActiveId\s*=\s*""[\s\S]*richLoaderResumeTimer\.start\(\)[\s\S]*Qt\.callLater[\s\S]*queueRichLoaderResumes\(\)/.test(mainSource))
		verify(/function\s+cancelRichLoaderResume\(stableId\)[\s\S]*completeRichLoaderResume\(stableId\)/.test(mainSource))
		verify(/function\s+completeRichLoaderResumeWhenReady\(\)[\s\S]*!messageRowReady\s*\|\|\s*!previewPresentationReady[\s\S]*scheduleRichResourceActivation\(\)[\s\S]*const rowId = stableId[\s\S]*Qt\.callLater[\s\S]*timeline\.completeRichLoaderResume\(rowId\)/.test(mainSource))
		const completeResumeStart =
			mainSource.indexOf("function completeRichLoaderResumeWhenReady()")
		const completeResumeEnd =
			mainSource.indexOf("readonly property bool inHydrationWindow:",
				completeResumeStart)
		verify(completeResumeStart >= 0 && completeResumeEnd > completeResumeStart)
		const completeResumeSource =
			mainSource.slice(completeResumeStart, completeResumeEnd)
		verify(/scheduleRichResourceActivation\(\)[\s\S]*if\s*\(richResourceActivationPending\)\s*return[\s\S]*completeNearViewportPrefetchIfReady\(stableId\)/.test(
			completeResumeSource))
		const activationTimerStart =
			mainSource.indexOf("id: richResourceActivationTimer")
		const activationTimerEnd =
			mainSource.indexOf("id: richResourceActivationSettleTimer",
				activationTimerStart)
		verify(activationTimerStart >= 0 && activationTimerEnd > activationTimerStart)
		const activationTimerSource =
			mainSource.slice(activationTimerStart, activationTimerEnd)
		const activationSettleRestart =
			activationTimerSource.indexOf("richResourceActivationSettleTimer.restart()")
		const activationElse =
			activationTimerSource.indexOf("else", activationSettleRestart)
		const activationDeferredCompletion =
			activationTimerSource.indexOf("Qt.callLater(function()", activationElse)
		const activationCompletion =
			activationTimerSource.indexOf(
				"messageDelegate.completeRichLoaderResumeWhenReady()",
				activationDeferredCompletion)
		verify(activationTimerSource.includes("interval: 16")
			&& activationTimerSource.includes("messageDelegate.hasPreviewContent")
			&& activationTimerSource.includes("!messageDelegate.previewLoaderFailed")
			&& activationSettleRestart >= 0
			&& activationElse > activationSettleRestart
			&& activationDeferredCompletion > activationElse
			&& activationCompletion > activationDeferredCompletion)
		verify(/id:\s*richResourceActivationSettleTimer[\s\S]*interval:\s*48[\s\S]*onTriggered:\s*Qt\.callLater\(function\(\)\s*\{\s*messageDelegate\.completeRichLoaderResumeWhenReady\(\)\s*\}\)/.test(mainSource))
		verify(/function\s+completeRichLoaderResumeWhenReady\(\)[\s\S]*messageRowReady[\s\S]*completeDeferredMaterialization\(stableId\)/.test(mainSource))
		verify(/function\s+completeRichLoaderResumeWhenReady\(\)[\s\S]*completeNearViewportPrefetchIfReady\(stableId\)/.test(mainSource))
		verify(/function\s+finishRichContentRecoveryIfIdle\(\)[\s\S]*Qt\.callLater[\s\S]*richLoaderResumeQueue\.length\s*>\s*0[\s\S]*deferredMaterializationQueue\.length\s*>\s*0[\s\S]*chatModel\.presentationBusy\s*=\s*false/.test(mainSource))
		const finishRecoveryStart =
			mainSource.indexOf("function finishRichContentRecoveryIfIdle")
		const finishRecoveryEnd =
			mainSource.indexOf("function restoreViewportAnchor", finishRecoveryStart)
		verify(finishRecoveryStart >= 0 && finishRecoveryEnd > finishRecoveryStart)
		const finishRecoverySource =
			mainSource.slice(finishRecoveryStart, finishRecoveryEnd)
		verify(/chatModel\.presentationBusy\s*=\s*false[\s\S]*scheduleNearViewportPrefetch\(\)/.test(
			finishRecoverySource))
		verify(/id:\s*richContentResumeTimer[\s\S]*captureViewportAnchor\(\)[\s\S]*queueRichLoaderResumes\(\)[\s\S]*queueVisibleDeferredMaterialization\(\)[\s\S]*queueVisiblePreviewHydration\(\)/.test(mainSource))
		verify(/richScrollRetentionRampStep:\s*128[\s\S]*property string nearViewportPrefetchActiveId:\s*""[\s\S]*property string nearViewportWarmId:\s*""[\s\S]*property string nearViewportPrefetchReadyId:\s*""/.test(mainSource))
		const prefetchCandidateStart =
			mainSource.indexOf("function nearViewportPrefetchCandidateFromItem")
		const prefetchCandidateEnd =
			mainSource.indexOf("function nearestDeferredPrefetchCandidate",
				prefetchCandidateStart)
		verify(prefetchCandidateStart >= 0 && prefetchCandidateEnd > prefetchCandidateStart)
		const prefetchCandidateSource =
			mainSource.slice(prefetchCandidateStart, prefetchCandidateEnd)
		verify(/if\s*\(item\.nearViewportPrefetchTerminal\s*===\s*true\)\s*return candidate/.test(
			prefetchCandidateSource))
		verify(/const needsWarmRecovery\s*=\s*!deferred\s*&&\s*item\.richResourcesRequired\s*===\s*true\s*&&\s*item\.previewLoaderIncubationSuppressed\s*!==\s*true\s*&&\s*item\.previewLoaderFailed\s*!==\s*true\s*&&\s*\(item\.richResourcesActive\s*!==\s*true/.test(
			prefetchCandidateSource))
		verify(/const deferredRich\s*=\s*deferred[\s\S]*Number\(item\.deferredPresentationHeight\s*\|\|\s*0\)\s*>\s*180[\s\S]*!deferredRich\s*&&\s*!needsWarmRecovery[\s\S]*const above[\s\S]*const below[\s\S]*distance\s*>\s*richScrollRetentionRampStep\s*\+\s*0\.5[\s\S]*"deferred":\s*deferredRich/.test(
			prefetchCandidateSource))
		verify(/function\s+nearViewportPrefetchAllowed\(\)[\s\S]*!stickToBottom[\s\S]*!scopePresentationObservationActive[\s\S]*!prependAnchorActive[\s\S]*!chatModel\.appendPending[\s\S]*!chatModel\.prependPending[\s\S]*pendingVisibleHydrationCount\(\)\s*===\s*0[\s\S]*pendingVisibleRichRecoveryCount\(\)\s*===\s*0/.test(mainSource))
		const nearestCandidateStart =
			mainSource.indexOf("function nearestDeferredPrefetchCandidate")
		const nearestCandidateEnd =
			mainSource.indexOf("function nearViewportPrefetchAllowed", nearestCandidateStart)
		verify(nearestCandidateStart >= 0 && nearestCandidateEnd > nearestCandidateStart)
		const nearestCandidateSource =
			mainSource.slice(nearestCandidateStart, nearestCandidateEnd)
		verify(/contentItem\.children[\s\S]*nearViewportPrefetchCandidateFromItem\([\s\S]*headerItem[\s\S]*deferredRepeater[\s\S]*deferredRepeater\.itemAt\(childIndex\)[\s\S]*return candidate/.test(
			nearestCandidateSource))
		verify(/id:\s*nearViewportPrefetchTimer[\s\S]*interval:\s*16[\s\S]*repeat:\s*false[\s\S]*startNearViewportPrefetch\(\)/.test(mainSource))
		const startPrefetchStart =
			mainSource.indexOf("function startNearViewportPrefetch")
		const startPrefetchEnd =
			mainSource.indexOf("function cancelActiveNearViewportPrefetchForScroll",
				startPrefetchStart)
		verify(startPrefetchStart >= 0 && startPrefetchEnd > startPrefetchStart)
		const startPrefetchSource =
			mainSource.slice(startPrefetchStart, startPrefetchEnd)
		verify(/nearViewportPrefetchViewportSignature[\s\S]*nearViewportPrefetchActiveId\s*=\s*candidate\.id[\s\S]*nearViewportWarmId\s*=\s*candidate\.id[\s\S]*nearViewportPrefetchDeadlineTimer\.restart\(\)[\s\S]*chatModel\.materializeDeferredMessage\(candidate\.id\)[\s\S]*queueRichLoaderResumes\(\)[\s\S]*queueVisiblePreviewHydration\(\)/.test(
			startPrefetchSource))
		verify(/if\s*\(completeNearViewportPrefetchIfReady\(candidate\.id\)\)\s*return\s*Qt\.callLater\(function\(\)\s*\{[\s\S]*if\s*\(timeline\.completeNearViewportPrefetchIfReady\(candidate\.id\)\)\s*return[\s\S]*timeline\.queueRichLoaderResumes\(\)[\s\S]*timeline\.queueVisiblePreviewHydration\(\)[\s\S]*timeline\.completeNearViewportPrefetchIfReady\(candidate\.id\)/.test(
			startPrefetchSource))
		const cancelPrefetchStart =
			mainSource.indexOf("function cancelActiveNearViewportPrefetchForScroll")
		const cancelPrefetchEnd =
			mainSource.indexOf("function abandonNearViewportPrefetch", cancelPrefetchStart)
		verify(cancelPrefetchStart >= 0 && cancelPrefetchEnd > cancelPrefetchStart)
		const cancelPrefetchSource =
			mainSource.slice(cancelPrefetchStart, cancelPrefetchEnd)
		verify(/timelineDelegateForStableId\(activeId\)[\s\S]*item\.cancelNearViewportPrefetchForScroll\(\)[\s\S]*abandonNearViewportPrefetch\(activeId,\s*false\)/.test(
			cancelPrefetchSource))
		verify(/function\s+expireNearViewportPrefetch\(\)[\s\S]*completeNearViewportPrefetchIfReady\(activeId\)[\s\S]*nearViewportPrefetchActiveId\s*!==\s*activeId[\s\S]*timelineDelegateForStableId\(activeId\)[\s\S]*cancelNearViewportPrefetchForScroll\(\)[\s\S]*nearViewportPrefetchActiveId\s*!==\s*activeId[\s\S]*retireTerminalNearViewportPrefetch\(activeId\)/.test(mainSource))
		verify(/id:\s*nearViewportPrefetchDeadlineTimer[\s\S]*interval:\s*2400[\s\S]*repeat:\s*false[\s\S]*expireNearViewportPrefetch\(\)/.test(mainSource))
		const abandonPrefetchStart =
			mainSource.indexOf("function abandonNearViewportPrefetch")
		const abandonPrefetchEnd =
			mainSource.indexOf("function invalidateNearViewportPrefetchReady",
				abandonPrefetchStart)
		verify(abandonPrefetchStart >= 0 && abandonPrefetchEnd > abandonPrefetchStart)
		const abandonPrefetchSource =
			mainSource.slice(abandonPrefetchStart, abandonPrefetchEnd)
		verify(/nearViewportPrefetchDeadlineTimer\.stop\(\)[\s\S]*richLoaderResumeQueue\.filter[\s\S]*deferredMaterializationActiveId\s*===\s*key[\s\S]*nearViewportPrefetchActiveId\s*=\s*""[\s\S]*nearViewportWarmId\s*=\s*""[\s\S]*nearViewportPrefetchReadyId\s*=\s*""[\s\S]*restoreOwnedAnchor\s*===\s*true[\s\S]*restoreViewportAnchor\(true\)[\s\S]*else[\s\S]*releaseViewportAnchor\(\)/.test(
			abandonPrefetchSource))
		const invalidatePrefetchStart =
			mainSource.indexOf("function invalidateNearViewportPrefetchReady")
		const invalidatePrefetchEnd =
			mainSource.indexOf("function retireTerminalNearViewportPrefetch",
				invalidatePrefetchStart)
		verify(invalidatePrefetchStart >= 0
			&& invalidatePrefetchEnd > invalidatePrefetchStart)
		const invalidatePrefetchSource =
			mainSource.slice(invalidatePrefetchStart, invalidatePrefetchEnd)
		verify(/const key\s*=\s*String\(stableId\s*\|\|\s*""\)\s*if\s*\(key\.length\s*===\s*0\s*\|\|\s*key\s*!==\s*nearViewportPrefetchReadyId\)\s*return false\s*if\s*\(!abandonNearViewportPrefetch\(key,\s*true\)\)\s*return false[\s\S]*nearViewportPrefetchViewportSignature\s*=\s*""\s*return true/.test(
			invalidatePrefetchSource))
		const retirePrefetchStart =
			mainSource.indexOf("function retireTerminalNearViewportPrefetch")
		const retirePrefetchEnd =
			mainSource.indexOf("function completeNearViewportPrefetchIfReady",
				retirePrefetchStart)
		verify(retirePrefetchStart >= 0 && retirePrefetchEnd > retirePrefetchStart)
		const retirePrefetchSource =
			mainSource.slice(retirePrefetchStart, retirePrefetchEnd)
		verify(/const key\s*=\s*String\(stableId\s*\|\|\s*""\)\s*const item\s*=\s*timelineDelegateForStableId\(key\)\s*if\s*\(item\s*&&\s*item\.markNearViewportPrefetchTerminal\)\s*item\.markNearViewportPrefetchTerminal\(\)\s*if\s*\(!abandonNearViewportPrefetch\(key,\s*true\)\)\s*return false[\s\S]*?nearViewportPrefetchViewportSignature\s*=\s*""\s*Qt\.callLater\(function\(\)\s*\{\s*if\s*\(!timeline\.richScrollInProgress\)\s*timeline\.scheduleNearViewportPrefetch\(\)\s*\}\)\s*return true/.test(
			retirePrefetchSource))
		const completePrefetchStart =
			mainSource.indexOf("function completeNearViewportPrefetchIfReady")
		const completePrefetchEnd =
			mainSource.indexOf("function suspendRichContentForScroll",
				completePrefetchStart)
		verify(completePrefetchStart >= 0 && completePrefetchEnd > completePrefetchStart)
		const completePrefetchSource =
			mainSource.slice(completePrefetchStart, completePrefetchEnd)
		verify(/messageRowReady\s*!==\s*true[\s\S]*previewPresentationReady\s*!==\s*true[\s\S]*contentNeedsHydration\s*===\s*true/.test(
			completePrefetchSource))
		verify(/if\s*\(item\.previewLoaderIncubationSuppressed\s*===\s*true\s*\|\|\s*item\.previewLoaderFailed\s*===\s*true\s*\|\|\s*item\.richResourcesRequired\s*!==\s*true\)\s*\{\s*retireTerminalNearViewportPrefetch\(key\)\s*return false\s*\}/.test(
			completePrefetchSource))
		verify(/if\s*\(item\.richResourcesActive\s*!==\s*true\s*\|\|\s*item\.richResourceActivationPending\s*===\s*true\)\s*return false\s*nearViewportPrefetchDeadlineTimer\.stop\(\)[\s\S]*nearViewportPrefetchReadyId\s*=\s*key[\s\S]*restoreViewportAnchor\(true\)/.test(
			completePrefetchSource))
		verify(/function\s+recoverOuterLoaderIncubationTimeout\(\)[\s\S]*abandonNearViewportPrefetch\([\s\S]*ownedStableId,\s*true\)/.test(mainSource))
		verify(/function\s+recoverOuterLoaderIncubationTimeout\(\)[\s\S]*const ownedStableId\s*=\s*resumeOwnedStableId[\s\S]*outerLoaderIncubationWatchdogStartedAt\s*=\s*0[\s\S]*?if\s*\(!inVisibleViewport\s*&&\s*timeline\.nearViewportPrefetchActiveId\s*===\s*ownedStableId\)\s*return\s*outerLoaderIncubationSuppressed\s*=\s*true/.test(mainSource))
		verify(/id:\s*asyncMessageDelegate[\s\S]*Component\.onDestruction:[\s\S]*retiredStableId[\s\S]*abandonNearViewportPrefetch\([\s\S]*retiredStableId,\s*true\)[\s\S]*resumeOwnedStableId\.length\s*>\s*0\s*&&\s*!abandoned[\s\S]*cancelRichLoaderResume\(resumeOwnedStableId\)/.test(mainSource))
		verify(/id:\s*messageRowLoader[\s\S]*onStatusChanged:[\s\S]*status\s*===\s*Loader\.Error[\s\S]*abandonNearViewportPrefetch\([\s\S]*messageDelegate\.stableId,\s*true\)/.test(mainSource))
		verify(/function\s+suspendRichContentForScroll\(\)[\s\S]*nearViewportPrefetchReadyId[\s\S]*continue[\s\S]*suspendRichResourcesForScroll\(\)/.test(mainSource))
		verify(/function\s+beginScopeChange\(\)[\s\S]*nearViewportPrefetchTimer\.stop\(\)[\s\S]*nearViewportPrefetchDeadlineTimer\.stop\(\)[\s\S]*nearViewportPrefetchActiveId\s*=\s*""[\s\S]*nearViewportWarmId\s*=\s*""[\s\S]*nearViewportPrefetchReadyId\s*=\s*""/.test(mainSource))
		verify(/function\s+timelinePresentationState\(\)[\s\S]*"nearViewportPrefetchActiveId":[\s\S]*"nearViewportWarmId":[\s\S]*"nearViewportPrefetchReadyId":/.test(mainSource))
		verify(/function\s+nearViewportPrefetchDiagnostics\(\)[\s\S]*timelineDelegateForStableId\(stableId\)[\s\S]*"viewportProbeHeight":[\s\S]*"richResourcesActive":[\s\S]*"richResourceActivationPending":[\s\S]*"previewMaterializationAdmitted":[\s\S]*"previewLoaderSurfaceState":[\s\S]*"previewLoaderIncubationWatchdogRunning":[\s\S]*"nearViewportPrefetchTerminal":[\s\S]*function\s+timelinePresentationState\(\)[\s\S]*"nearViewportPrefetch":\s*nearViewportPrefetchDiagnostics\(\)/.test(mainSource))
		verify(/onRichContentViewportEligibleChanged:\s*\{[\s\S]*requestPreviewHydrationIfNeeded\(\)/.test(mainSource))
		const previewHydrationStart =
			mainSource.indexOf("function queueVisiblePreviewHydration")
		const previewHydrationEnd =
			mainSource.indexOf("function materializeNextDeferredRow", previewHydrationStart)
		verify(previewHydrationStart >= 0 && previewHydrationEnd > previewHydrationStart)
		const previewHydrationSource =
			mainSource.slice(previewHydrationStart, previewHydrationEnd)
		verify(/item\.inVisibleViewport\s*===\s*true[\s\S]*String\(item\.stableId\s*\|\|\s*""\)[\s\S]*===\s*nearViewportWarmId[\s\S]*requestPreviewHydrationIfNeeded\(pendingAllowed\)/.test(
			previewHydrationSource))
		const richResourcesChangedStart =
			mainSource.indexOf("onRichResourcesActiveChanged:")
		const richResourcesChangedEnd =
			mainSource.indexOf("function scheduleRichResourceActivation",
				richResourcesChangedStart)
		verify(richResourcesChangedStart >= 0
			&& richResourcesChangedEnd > richResourcesChangedStart)
		const richResourcesChangedSource =
			mainSource.slice(richResourcesChangedStart, richResourcesChangedEnd)
		verify(/const retainedId\s*=\s*String\(stableId\s*\|\|\s*""\)\s*if\s*\(!richResourcesActive\s*&&\s*richResourcesRequired\s*&&\s*timeline\.nearViewportPrefetchReadyId\s*===\s*retainedId\)\s*timeline\.abandonNearViewportPrefetch\(retainedId,\s*true\)/.test(
			richResourcesChangedSource))
		verify(/!richResourcesActive\s*&&\s*!inVisibleViewport[\s\S]*!timeline\.richScrollInProgress[\s\S]*timeline\.scheduleNearViewportPrefetch\(\)/.test(
			richResourcesChangedSource))
		verify(/function\s+timelinePresentationState\(\)[\s\S]*"visibleInactiveRichResourceCount":[\s\S]*inactiveRichResourceCount[\s\S]*"settled":[\s\S]*!timeline\.prependAnchorActive[\s\S]*!chatModel\.prependPending[\s\S]*inactiveRichResourceCount\s*===\s*0/.test(mainSource))
		verify(!/"settled":[\s\S]{0,400}!timeline\.topAnchorActive/.test(mainSource))
		verify(!/"settled":[\s\S]{0,400}!timeline\.scopePresentationObservationActive/.test(mainSource))
		verify(/function\s+completePerformanceChatScrollWorkload\(\)[\s\S]*!timeline\.topAnchorActive[\s\S]*performanceChatScrollFullRangeToStart[\s\S]*timeline\.beginTopAnchor\(true\)/.test(mainSource))
		verify(/property var performanceChatScrollRetainedRichIds:\s*\(\{\}\)[\s\S]*property int performanceChatScrollRetainedRichResourceDropCount:\s*0/.test(mainSource))
		verify(/function\s+preparePerformanceChatScrollWorkload\(stepCount,\s*fullRangeToStart\)[\s\S]*retainedRichIds[\s\S]*item\.richResourcesRequired\s*!==\s*true[\s\S]*item\.richResourcesActive\s*!==\s*true[\s\S]*visibleNow[\s\S]*visibleAtTarget[\s\S]*performanceChatScrollRetainedRichIds\s*=\s*retainedRichIds/.test(mainSource))
		verify(/onRichResourcesActiveChanged:[\s\S]*!richResourcesActive[\s\S]*timeline\.richScrollInProgress[\s\S]*performanceChatScrollRetainedRichIds[\s\S]*performanceChatScrollRetainedRichResourceDropCount/.test(mainSource))
		verify(/function\s+performanceChatScrollState\(\)[\s\S]*"retainedRichResourceIds":[\s\S]*Object\.keys\(performanceChatScrollRetainedRichIds[\s\S]*"retainedRichResourceDropCount":[\s\S]*performanceChatScrollRetainedRichResourceDropCount/.test(mainSource))
		verify(/function\s+captureViewportAnchor\(\)[\s\S]*Math\.abs\(contentY\s*-\s*originY\)\s*<=\s*0\.5[\s\S]*beginTopAnchor\(\)[\s\S]*function\s+beginTopAnchor\(guardProgrammaticMovement\)[\s\S]*topAnchorActive\s*=\s*true[\s\S]*topAnchorFrameTimer\.restart\(\)[\s\S]*function\s+restoreTopAnchor\(\)[\s\S]*contentY\s*=\s*originY/.test(mainSource))
		verify(/function\s+capturePrependAnchor\(\)[\s\S]*releaseTopAnchor\(\)[\s\S]*prependAnchorOffset[\s\S]*prependAnchorActive\s*=\s*true/.test(mainSource))
		verify(/function\s+restoreTopAnchor\(\)[\s\S]*timelineScrollHandler\.scrolling[\s\S]*contentY\s*=\s*originY/.test(mainSource))
		verify(/onContentHeightChanged:[\s\S]*topAnchorActive[\s\S]*topAnchorSettleTimer\.restart\(\)[\s\S]*topAnchorFrameTimer\.restart\(\)[\s\S]*restoreTopAnchor\(\)/.test(mainSource))
		verify(/id:\s*topAnchorProgrammaticMovementGuardTimer[\s\S]*interval:\s*96[\s\S]*topAnchorProgrammaticMovementGuard\s*=\s*false/.test(mainSource))
		verify(/onWheelInputObserved:\s*timeline\.noteExplicitWheelInput\(\)[\s\S]*property bool explicitWheelInputPending:\s*false[\s\S]*property bool explicitNavigationMovement:\s*false/.test(mainSource))
		verify(/function\s+noteExplicitWheelInput\(\)[\s\S]*\+\+explicitWheelInputGeneration[\s\S]*explicitWheelInputStartY\s*=\s*contentY[\s\S]*explicitWheelInputPending\s*=\s*true[\s\S]*Qt\.callLater[\s\S]*generation\s*===\s*timeline\.explicitWheelInputGeneration[\s\S]*explicitWheelInputPending\s*=\s*false/.test(mainSource))
		verify(/function\s+takeExplicitNavigationOwnership\(classification\)[\s\S]*explicitNavigationMovement\s*=\s*true[\s\S]*explicitWheelInputPending\s*=\s*false[\s\S]*stickToBottom\s*=\s*false[\s\S]*releasePrependAnchor\(\)[\s\S]*releaseViewportAnchor\(\)[\s\S]*releaseTopAnchor\(\)/.test(mainSource))
		verify(/onContentYChanged:[\s\S]*explicitWheelInputPending[\s\S]*explicitWheelInputStartY[\s\S]*takeExplicitNavigationOwnership\("native-wheel"\)/.test(mainSource))
		verify(/property bool explicitNavigationMovement:\s*false[\s\S]*onMovementStarted:[\s\S]*const\s+pendingNativeWheel\s*=\s*explicitWheelInputPending[\s\S]*const\s+explicitNavigation\s*=\s*dragging[\s\S]*timelineScrollHandler\.scrolling[\s\S]*timelineScrollBar\.pressed[\s\S]*performanceChatScrollRunning[\s\S]*pendingNativeWheel[\s\S]*explicitWheelInputPending\s*=\s*false[\s\S]*takeExplicitNavigationOwnership[\s\S]*if\s*\(!explicitNavigation\)[\s\S]*viewportAnchorActive[\s\S]*restoreViewportAnchor\(\)/.test(mainSource))
		verify(!/const\s+explicitNavigation\s*=\s*dragging\s*\|\|\s*flicking/.test(mainSource))
		verify(/function\s+timelinePresentationState\(\)[\s\S]*"stickToBottom"[\s\S]*"moving"[\s\S]*"dragging"[\s\S]*"flicking"[\s\S]*"explicitWheelInputPending"[\s\S]*"explicitNavigationMovement"[\s\S]*"lastMovementClassification"[\s\S]*"automaticLayoutMovementCount"/.test(mainSource))
		verify(/onMovementEnded:[\s\S]*const\s+explicitNavigation\s*=\s*explicitNavigationMovement[\s\S]*explicitNavigationMovement\s*=\s*false[\s\S]*if\s*\(!explicitNavigation\)[\s\S]*viewportAnchorActive[\s\S]*restoreViewportAnchor\(\)[\s\S]*else[\s\S]*stickToBottom\s*=\s*isNearBottom\(\)[\s\S]*requestBottomFollow\(\)[\s\S]*captureViewportAnchor\(\)[\s\S]*return[\s\S]*stickToBottom\s*=\s*isNearBottom\(\)/.test(mainSource))
		verify(/function\s+revealConversationSearchMatch\(row,\s*stableId\)[\s\S]*releasePrependAnchor\(\)[\s\S]*releaseViewportAnchor\(\)[\s\S]*releaseTopAnchor\(\)[\s\S]*positionViewAtIndex\(presentationRow,\s*ListView\.Center\)/.test(mainSource))
		verify(/function\s+positionVisualFixtureTimelineAt\(stableId\)[\s\S]*releasePrependAnchor\(\)[\s\S]*releaseViewportAnchor\(\)[\s\S]*releaseTopAnchor\(\)[\s\S]*positionViewAtIndex\(row,\s*ListView\.Beginning\)/.test(mainSource))
		verify(/id:\s*topAnchorFrameTimer[\s\S]*interval:\s*16[\s\S]*repeat:\s*false[\s\S]*id:\s*topAnchorSettleTimer[\s\S]*interval:\s*180[\s\S]*recoveryPending[\s\S]*pendingScopeHydrationCount\(\)[\s\S]*pendingVisibleRichRecoveryCount\(\)[\s\S]*restoreTopAnchor\(\)[\s\S]*restart\(\)[\s\S]*releaseTopAnchor\(\)/.test(mainSource))
		verify(/id:\s*topAnchorSettleTimer[\s\S]*timeline\.releaseTopAnchor\(\)[\s\S]{0,120}timeline\.scheduleNearViewportPrefetch\(\)/.test(mainSource))
		verify(/id:\s*scopePresentationObservationTimer[\s\S]*scopePresentationObservationActive\s*=\s*false[\s\S]{0,120}scheduleNearViewportPrefetch\(\)/.test(mainSource))
		verify(!/function\s+restoreTopAnchor\(\)[\s\S]{0,240}positionViewAtBeginning\(\)/.test(mainSource))
		verify(!/function\s+restoreTopAnchor\(\)[\s\S]{0,240}forceLayout\(\)/.test(mainSource))
		verify(/property bool richResourcesActive:\s*false[\s\S]*function\s+updateRichResourceActivity\(\)[\s\S]*!timeline\.richScrollInProgress[\s\S]*richResourcesActive\s*=\s*true/.test(mainSource))
		verify(/readonly property bool inHydrationWindow:\s*asyncMessageDelegate\.inHydrationWindow/.test(
			mainSource))
		verify(/readonly property bool inVisibleViewport:\s*asyncMessageDelegate\.inVisibleViewport/.test(
			mainSource))
		verify(!/property bool inVisibleViewport:[\s\S]{0,120}\by\s*\+\s*height\s*>=\s*timeline\.contentY/.test(mainSource.slice(
			mainSource.indexOf("id: messageDelegate"), mainSource.indexOf("id: messageRowLoader"))))
		verify(/function\s+suspendRichResourcesForScroll\(\)[\s\S]*richResourceActivationTimer\.stop\(\)[\s\S]*richResourceActivationSettleTimer\.stop\(\)[\s\S]*richResourcesActive\s*=\s*false/.test(mainSource))
		verify(/readonly property bool richResourcesRequired:[\s\S]*hasPreviewContent\s*&&\s*!previewTerminalUnavailable[\s\S]*hasAttachmentContent[\s\S]*hasInlineImageContent/.test(mainSource))
		verify(/readonly property bool richResourceActivationReady:[\s\S]*!backendContentNeedsHydration[\s\S]*!hasPreviewContent\s*\|\|\s*previewTerminalUnavailable[\s\S]*previewMaterializationAdmitted[\s\S]*Loader\.Ready/.test(mainSource))
		verify(/function\s+completeRichLoaderResumeWhenReady\(\)[\s\S]*richResourcesRequired\s*&&\s*richResourceActivationReady[\s\S]*scheduleRichResourceActivation\(\)/.test(mainSource))
		const richResourcesRequiredStart =
			mainSource.indexOf("onRichResourcesRequiredChanged:")
		const richResourcesRequiredEnd =
			mainSource.indexOf("onPreviewLoaderSurfaceStateChanged:",
				richResourcesRequiredStart)
		verify(richResourcesRequiredStart >= 0
			&& richResourcesRequiredEnd > richResourcesRequiredStart)
		const richResourcesRequiredSource =
			mainSource.slice(richResourcesRequiredStart, richResourcesRequiredEnd)
		verify(/const terminalId\s*=\s*String\(stableId\s*\|\|\s*""\)\s*if\s*\(!richResourcesRequired\s*&&\s*\(timeline\.nearViewportPrefetchReadyId\s*===\s*terminalId\s*\|\|\s*\(timeline\.nearViewportPrefetchActiveId\s*===\s*terminalId\s*&&\s*messageRowReady\s*&&\s*previewPresentationReady\s*&&\s*!contentNeedsHydration\)\)\)\s*timeline\.retireTerminalNearViewportPrefetch\(terminalId\)\s*updateRichResourceActivity\(\)/.test(
			richResourcesRequiredSource))
		const previewLoaderFailedStart =
			mainSource.indexOf("onPreviewLoaderFailedChanged:")
		const previewLoaderFailedEnd =
			mainSource.indexOf("onPreviewLoaderSurfaceStateChanged:",
				previewLoaderFailedStart)
		verify(previewLoaderFailedStart >= 0
			&& previewLoaderFailedEnd > previewLoaderFailedStart)
		const previewLoaderFailedSource =
			mainSource.slice(previewLoaderFailedStart, previewLoaderFailedEnd)
		verify(/const row\s*=\s*messageRowLoader\.item\s*const loader\s*=\s*row\s*\?\s*row\.previewLoader\s*:\s*null\s*if\s*\(!loader\s*\|\|\s*loader\.status\s*!==\s*Loader\.Error\)\s*return\s*previewLoaderTerminalErrorIdentity\s*=\s*previewLoaderIncubationIdentity\s*retireOwnedTerminalPreviewPrefetch\(\)\s*messagePresentationReadyLatched\s*=\s*true\s*completeRichLoaderResumeWhenReady\(\)/.test(
			previewLoaderFailedSource))
		verify(!previewLoaderFailedSource.includes(
			"markNearViewportPrefetchTerminal()"))
		const previewSuppressedStart =
			mainSource.indexOf("onPreviewLoaderIncubationSuppressedChanged:")
		const previewSuppressedEnd =
			mainSource.indexOf("onPreviewLoaderFailedChanged:",
				previewSuppressedStart)
		verify(previewSuppressedStart >= 0
			&& previewSuppressedEnd > previewSuppressedStart)
		const previewSuppressedSource =
			mainSource.slice(previewSuppressedStart, previewSuppressedEnd)
		verify(/if\s*\(!previewLoaderIncubationSuppressed\)\s*return\s*retireOwnedTerminalPreviewPrefetch\(\)/.test(
			previewSuppressedSource))
		verify(!previewSuppressedSource.includes(
			"markNearViewportPrefetchTerminal()"))
		verify(/function\s+scheduleRichResourceActivation\(\)[\s\S]*!richResourcesRequired[\s\S]*!richResourceActivationReady/.test(mainSource))
		verify(/function\s+updateRichResourceActivity\(\)[\s\S]*accessibilityPooled\s*\|\|\s*!richResourcesRequired[\s\S]*richResourcesActive\s*=\s*false/.test(mainSource))
		const contentHydrationChangedStart =
			mainSource.indexOf("onContentNeedsHydrationChanged:")
		const contentHydrationChangedEnd =
			mainSource.indexOf("onInHydrationWindowChanged:",
				contentHydrationChangedStart)
		verify(contentHydrationChangedStart >= 0
			&& contentHydrationChangedEnd > contentHydrationChangedStart)
		const contentHydrationChangedSource =
			mainSource.slice(contentHydrationChangedStart, contentHydrationChangedEnd)
		verify(/const hydrationId\s*=\s*String\(stableId\s*\|\|\s*""\)\s*if\s*\(contentNeedsHydration\)\s*timeline\.invalidateNearViewportPrefetchReady\(\s*hydrationId\)\s*requestPreviewHydrationIfNeeded\(\)\s*if\s*\(!contentNeedsHydration\)\s*\{\s*completeRichLoaderResumeWhenReady\(\)\s*timeline\.scheduleRichContentResume\(\)\s*timeline\.scheduleNearViewportPrefetch\(\)\s*\}/.test(
			contentHydrationChangedSource))
		verify(/property bool richLoaderSuspended:\s*false[\s\S]*function\s+handleListViewPooled\(\)[\s\S]*richLoaderSuspended\s*=\s*true[\s\S]*function\s+handleListViewReused\(\)[\s\S]*richLoaderSuspended\s*=\s*timeline\.richScrollInProgress[\s\S]*timeline\.scopePresentationPending/.test(mainSource))
		verify(/property bool messageRowMaterialized:\s*false[\s\S]*function\s+handleListViewPooled\(\)[\s\S]*messageRowMaterialized\s*=\s*false/.test(mainSource))
		verify(/id:\s*messageRowLoader[\s\S]*active:\s*!messageDelegate\.logEntry[\s\S]*messageDelegate\.messageRowMaterialized[\s\S]*asynchronous:\s*true[\s\S]*onLoaded:[\s\S]*messageDelegate\.messageRowMaterialized\s*=\s*true/.test(mainSource))
		verify(/property bool messagePresentationReadyLatched:\s*false[\s\S]*readonly property bool currentMessagePresentationReady:[\s\S]*messageRowLoader\.status\s*===\s*Loader\.Error[\s\S]*messageRowLoader\.status\s*===\s*Loader\.Ready[\s\S]*previewPresentationReady[\s\S]*readonly property bool messageRowReady:[\s\S]*currentMessagePresentationReady[\s\S]*messagePresentationReadyLatched[\s\S]*onCurrentMessagePresentationReadyChanged:[\s\S]*messagePresentationReadyLatched\s*=\s*true/.test(mainSource))
		verify(/function\s+handleListViewPooled\(\)[\s\S]*messageRowMaterialized\s*=\s*false[\s\S]*messagePresentationReadyLatched\s*=\s*false[\s\S]*onStableIdChanged:[\s\S]*messagePresentationReadyLatched\s*=\s*false/.test(mainSource))
		verify(/id:\s*messageRowLoader[\s\S]*onLoaded:[\s\S]*messageDelegate\.completeRichLoaderResumeWhenReady\(\)[\s\S]*onStatusChanged:[\s\S]*status\s*===\s*Loader\.Error/.test(mainSource))
		verify(/readonly property real richPresentationMetadataHeight:[\s\S]*hasReplyContent\s*\?\s*44[\s\S]*hasEmbeddedFooterContent\s*\?\s*34[\s\S]*previewReservedHeight:[\s\S]*previewTerminalUnavailable\s*\?\s*previewStagingHeight[\s\S]*previewPlaceholderHeight[\s\S]*lightweightMessageRowHeight:[\s\S]*hasPreviewContent\s*\?\s*previewReservedHeight\s*\+\s*72[\s\S]*richPresentationMetadataHeight[\s\S]*hasAttachmentContent\s*\?\s*attachmentPlaceholderHeight\s*\+\s*72[\s\S]*richPresentationMetadataHeight[\s\S]*hasInlineImageContent\s*\?\s*360\s*\+\s*richPresentationMetadataHeight/.test(mainSource))
		verify(/readonly property string previewPlaceholderKind:[\s\S]*preview\.metadata[\s\S]*previewKind/.test(mainSource))
		const previewMediaClassification =
			/readonly property bool previewPlaceholderHasMedia:[\s\S]{0,420}/.exec(mainSource)
		verify(previewMediaClassification !== null)
		verify(/preview\.mediaItems[\s\S]*preview\.embedKind[\s\S]*preview\.mediaUrl[\s\S]*previewPlaceholderKind\.length\s*>\s*0/.test(
			previewMediaClassification[0]))
		verify(!/preview\.thumbnailUrl/.test(previewMediaClassification[0]))
		verify(/property bool previewMaterializationAdmitted:\s*false/.test(mainSource))
		verify(/property bool previewLoaderIncubationSuppressed:\s*false[\s\S]*property string previewLoaderIncubationSuppressedIdentity:\s*""[\s\S]*readonly property bool previewLoaderIncubationWatchdogRunning:[\s\S]*previewLoaderIncubationWatchdogTimer\.running/.test(mainSource))
		verify(/property string previewLoaderTerminalErrorIdentity:\s*""\s*property string nearViewportPrefetchTerminalIdentity:\s*""[\s\S]*readonly property bool nearViewportPrefetchTerminal:\s*nearViewportPrefetchTerminalIdentity\.length\s*>\s*0\s*&&\s*nearViewportPrefetchTerminalIdentity\s*===\s*previewLoaderIncubationIdentity/.test(mainSource))
		verify(/readonly property bool previewLoaderFailed:\s*\(previewLoaderTerminalErrorIdentity\.length\s*>\s*0\s*&&\s*previewLoaderTerminalErrorIdentity\s*===\s*previewLoaderIncubationIdentity\)\s*\|\|\s*\(messageRowLoader\.item\s*&&\s*messageRowLoader\.item\.previewLoader\s*&&\s*messageRowLoader\.item\.previewLoader\.status\s*===\s*Loader\.Error\)/.test(mainSource))
		verify(/readonly property bool previewBackendError:[\s\S]*state === "error"[\s\S]*state === "failed"[\s\S]*preview\.failed === true/.test(mainSource))
		verify(/readonly property bool previewTerminalUnavailable:\s*previewBackendError\s*\|\|\s*previewLoaderFailed\s*\|\|\s*previewLoaderIncubationSuppressed\s*readonly property bool terminalPreviewFallbackUnavailable:\s*hasPreviewContent\s*&&\s*previewTerminalUnavailable\s*&&\s*\(!messageRowLoader\.item\s*\|\|\s*richLoaderSuspended\s*\|\|\s*!timeline\.prependRichContentActive\(\s*stableId,\s*richContentViewportEligible\)\)/.test(mainSource))
		verify(/property bool previewBackendErrorEpochActive:\s*false\s*property string previewBackendErrorStableId:\s*""/.test(mainSource))
		verify(/objectName:\s*"asyncRichHandoffSummary"[\s\S]*anchors\.top:\s*parent\.top[\s\S]*color:\s*"transparent"/.test(mainSource))
		const backendErrorChangedStart =
			mainSource.indexOf("onPreviewBackendErrorChanged:")
		const backendErrorChangedEnd =
			mainSource.indexOf("onPreviewLoaderFailedChanged:", backendErrorChangedStart)
		verify(backendErrorChangedStart >= 0
			&& backendErrorChangedEnd > backendErrorChangedStart)
		const backendErrorChangedSource =
			mainSource.slice(backendErrorChangedStart, backendErrorChangedEnd)
		verify(/previewBackendErrorEpochActive\s*=\s*true[\s\S]*previewBackendErrorStableId\s*=\s*String\(stableId\s*\|\|\s*""\)[\s\S]*backendHydrationRequested\s*=\s*false[\s\S]*retireOwnedTerminalPreviewPrefetch\(\)[\s\S]*return/.test(
			backendErrorChangedSource))
		verify(!backendErrorChangedSource.includes("suspendRichResourcesForScroll()"))
		verify(/const previousBackendError\s*=\s*previewBackendErrorEpochActive[\s\S]*const previousStableId\s*=\s*previewBackendErrorStableId[\s\S]*previewBackendRecoveryAction\(previousStableId,[\s\S]*previewNeedsHydration/.test(
			backendErrorChangedSource))
		verify(/richResourcesRequired:[\s\S]*hasAttachmentContent\s*\|\|\s*hasInlineImageContent/.test(mainSource))
		const previewWatchdogResetStart =
			mainSource.indexOf("function resetPreviewLoaderIncubationWatchdog")
		const previewWatchdogResetEnd =
			mainSource.indexOf("function previewBackendRecoveryAction",
				previewWatchdogResetStart)
		const previewTerminalMarkStart =
			mainSource.indexOf("function markNearViewportPrefetchTerminal",
				previewWatchdogResetStart)
		verify(previewWatchdogResetStart >= 0
			&& previewWatchdogResetEnd > previewWatchdogResetStart
			&& previewTerminalMarkStart > previewWatchdogResetStart)
		const previewWatchdogResetSource =
			mainSource.slice(previewWatchdogResetStart, previewWatchdogResetEnd)
		verify(/if\s*\(clearSuppression\s*===\s*true\)\s*\{\s*previewLoaderIncubationSuppressed\s*=\s*false\s*previewLoaderIncubationSuppressedIdentity\s*=\s*""\s*\}/.test(
			previewWatchdogResetSource))
		verify(!previewWatchdogResetSource.includes(
			"nearViewportPrefetchTerminalIdentity"))
		verify(!previewWatchdogResetSource.includes(
			"previewLoaderTerminalErrorIdentity"))
		const previewTerminalMarkEnd =
			mainSource.indexOf("function armPreviewLoaderIncubationWatchdog",
				previewTerminalMarkStart)
		verify(previewTerminalMarkEnd > previewTerminalMarkStart)
		const previewTerminalMarkSource =
			mainSource.slice(previewTerminalMarkStart, previewTerminalMarkEnd)
		verify(/nearViewportPrefetchTerminalIdentity\s*=\s*previewLoaderIncubationIdentity/.test(
			previewTerminalMarkSource))
		verify(/function retireOwnedTerminalPreviewPrefetch\(\)[\s\S]*timeline\.retireTerminalNearViewportPrefetch\(terminalId\)[\s\S]*updateRichResourceActivity\(\)/.test(
			previewTerminalMarkSource))
		const backendRecoveryActionStart =
			mainSource.indexOf("function previewBackendRecoveryAction(")
		const backendRecoveryActionEnd =
			mainSource.indexOf("function recoverPreviewBackendGeneration(",
				backendRecoveryActionStart)
		verify(backendRecoveryActionStart >= 0
			&& backendRecoveryActionEnd > backendRecoveryActionStart)
		const backendRecoveryActionSource = mainSource.slice(
			backendRecoveryActionStart, backendRecoveryActionEnd)
		const backendRecoveryActionProbe = Qt.createQmlObject(
			"import QtQuick\nQtObject {\n"
				+ backendRecoveryActionSource + "\n}",
			testCase, "backendRecoveryActionProbe")
		verify(backendRecoveryActionProbe !== null)
		compare(backendRecoveryActionProbe.previewBackendRecoveryAction(
			"message-42", "message-42", true, false, false), "resume")
		compare(backendRecoveryActionProbe.previewBackendRecoveryAction(
			"message-42", "message-42", true, false, true), "hydrate")
		compare(backendRecoveryActionProbe.previewBackendRecoveryAction(
			"message-41", "message-42", true, false, false), "none")
		compare(backendRecoveryActionProbe.previewBackendRecoveryAction(
			"message-42", "message-42", false, false, false), "none")
		backendRecoveryActionProbe.destroy()
		verify(/readonly property string previewLoaderSurfaceState:[\s\S]*Loader\.Ready[\s\S]*materializationSettled\s*!==\s*true[\s\S]*return\s*"blank"[\s\S]*performanceEmbedSurfaceState/.test(mainSource))
		verify(/readonly property bool previewPresentationReady:\s*!hasPreviewContent\s*\|\|\s*previewNeedsHydration\s*\|\|\s*previewTerminalUnavailable\s*\|\|\s*previewLoaderFailed\s*\|\|\s*previewLoaderIncubationSuppressed[\s\S]*previewMaterializationAdmitted[\s\S]*previewLoaderSurfaceNonBlank[\s\S]*materializationSettled/.test(mainSource))
		verify(!mainSource.includes("previewMaterializationWatchdog"))
		verify(/onPreviewLoaderSurfaceStateChanged:[\s\S]*previewLoaderSurfaceNonBlank[\s\S]*armPreviewLoaderIncubationWatchdog\(\)[\s\S]*richLoaderResumeActiveId\s*!==\s*stableId[\s\S]*scheduleRichContentResume\(\)[\s\S]*completeRichLoaderResumeWhenReady\(\)/.test(mainSource))
		const previewSurfaceChangedStart =
			mainSource.indexOf("onPreviewLoaderSurfaceStateChanged:")
		const previewSurfaceChangedEnd =
			mainSource.indexOf("onCurrentMessagePresentationReadyChanged:",
				previewSurfaceChangedStart)
		verify(previewSurfaceChangedStart >= 0
			&& previewSurfaceChangedEnd > previewSurfaceChangedStart)
		const previewSurfaceChangedSource =
			mainSource.slice(previewSurfaceChangedStart, previewSurfaceChangedEnd)
		verify(/if\s*\(previewLoaderSurfaceNonBlank\)\s*\{[\s\S]*resetPreviewLoaderIncubationWatchdog\(true\)\s*previewLoaderTerminalErrorIdentity\s*=\s*""\s*nearViewportPrefetchTerminalIdentity\s*=\s*""\s*\}\s*else if\s*\(previewMaterializationAdmitted\)/.test(
			previewSurfaceChangedSource))
		verify(/function\s+onMaterializationSettledChanged\(\)[\s\S]*completeRichLoaderResumeWhenReady\(\)/.test(mainSource))
		verify(/function\s+completeRichLoaderResumeWhenReady\(\)[\s\S]*const previewGeneration\s*=\s*previewLoaderIncubationIdentity[\s\S]*Qt\.callLater[\s\S]*previewGeneration[\s\S]*previewLoaderIncubationIdentity[\s\S]*richLoaderResumeActiveId\s*===\s*rowId[\s\S]*currentMessagePresentationReady[\s\S]*previewLoaderIncubationSuppressed[\s\S]*previewPresentationReady[\s\S]*completeRichLoaderResume\(rowId\)/.test(mainSource))
		verify(/function\s+recoverPreviewLoaderIncubationTimeout\(\)[\s\S]*if\s*\(!inVisibleViewport\s*&&\s*timeline\.nearViewportPrefetchActiveId\s*===\s*stableId\)\s*\{\s*previewLoaderIncubationWatchdogStartedAt\s*=\s*0\s*return\s*\}\s*previewLoaderIncubationSuppressed\s*=\s*true[\s\S]*previewLoaderIncubationSuppressedIdentity\s*=[\s\S]*previewLoaderIncubationIdentity[\s\S]*previewMaterializationAdmitted\s*=\s*false[\s\S]*messagePresentationReadyLatched\s*=\s*true[\s\S]*completeRichLoaderResumeWhenReady\(\)/.test(mainSource))
		verify(/id:\s*previewLoaderIncubationWatchdogTimer[\s\S]*interval:\s*192[\s\S]*recoverPreviewLoaderIncubationTimeout\(\)/.test(mainSource))
		verify(/function\s+armPreviewLoaderIncubationWatchdog\(\)[\s\S]*loader\.status\s*===\s*Loader\.Error[\s\S]*loader\.status\s*===\s*Loader\.Ready[\s\S]*previewLoaderSurfaceNonBlank[\s\S]*previewLoaderIncubationWatchdogTimer\.start\(\)/.test(mainSource))
		verify(/function\s+resumeRichContentAfterScroll\(allowDuringFinalization,\s*admitPreviewMaterialization\)[\s\S]*pendingAllowed[\s\S]*scopePresentationFinalizing[\s\S]*scopePresentationPending\s*&&\s*!pendingAllowed[\s\S]*return\s+false[\s\S]*richLoaderSuspended\s*=\s*false[\s\S]*admitPreviewMaterialization\s*===\s*true[\s\S]*previewAdmissionAccepted\s*=\s*!previewNeedsHydration\s*&&\s*!previewLoaderIncubationSuppressed\s*&&\s*!previewLoaderFailed\s*&&\s*richContentViewportEligible[\s\S]*previewMaterializationAdmitted\s*=\s*true[\s\S]*armPreviewLoaderIncubationWatchdog\(\)[\s\S]*hasPreviewContent\s*&&\s*!previewNeedsHydration\s*&&\s*!previewMaterializationAdmitted\s*&&\s*!previewLoaderFailed\s*&&\s*richContentViewportEligible[\s\S]*previewAdmissionAccepted/.test(mainSource))
		verify(/function\s+queueRichLoaderResumes\(\)[\s\S]*item\.previewLoaderIncubationSuppressed\s*===\s*true[\s\S]*item\.previewNeedsHydration\s*===\s*true[\s\S]*item\.messageRowMaterialized\s*===\s*true/.test(mainSource))
		const previewIdentityChangedStart =
			mainSource.indexOf("onPreviewLoaderIncubationIdentityChanged:")
		const previewIdentityChangedEnd =
			mainSource.indexOf("onContentNeedsHydrationChanged:",
				previewIdentityChangedStart)
		verify(previewIdentityChangedStart >= 0
			&& previewIdentityChangedEnd > previewIdentityChangedStart)
		const previewIdentityChangedSource =
			mainSource.slice(previewIdentityChangedStart, previewIdentityChangedEnd)
		verify(/const terminalErrorChanged\s*=\s*previewLoaderTerminalErrorIdentity\.length\s*>\s*0\s*&&\s*previewLoaderIncubationIdentity\s*!==\s*previewLoaderTerminalErrorIdentity[\s\S]*const prefetchTerminalChanged\s*=\s*nearViewportPrefetchTerminalIdentity\.length\s*>\s*0\s*&&\s*previewLoaderIncubationIdentity\s*!==\s*nearViewportPrefetchTerminalIdentity[\s\S]*if\s*\(!suppressionChanged\s*&&\s*!terminalErrorChanged\s*&&\s*!prefetchTerminalChanged\)\s*return[\s\S]*if\s*\(terminalErrorChanged\)\s*previewLoaderTerminalErrorIdentity\s*=\s*""\s*if\s*\(prefetchTerminalChanged\)\s*nearViewportPrefetchTerminalIdentity\s*=\s*""[\s\S]*scheduleRichContentResume\(\)/.test(
			previewIdentityChangedSource))
		const innerDelegateStart = mainSource.indexOf("id: messageDelegate")
		const pooledLifecycleStart =
			mainSource.indexOf("function handleListViewPooled", innerDelegateStart)
		const reusedLifecycleStart =
			mainSource.indexOf("function handleListViewReused", pooledLifecycleStart)
		const reusedLifecycleEnd =
			mainSource.indexOf("onHasPreviewContentChanged:", reusedLifecycleStart)
		verify(pooledLifecycleStart > innerDelegateStart
			&& reusedLifecycleStart > pooledLifecycleStart
			&& reusedLifecycleEnd > reusedLifecycleStart)
		const pooledLifecycleSource =
			mainSource.slice(pooledLifecycleStart, reusedLifecycleStart)
		const reusedLifecycleSource =
			mainSource.slice(reusedLifecycleStart, reusedLifecycleEnd)
		verify(/resetPreviewLoaderIncubationWatchdog\(true\)\s*previewLoaderTerminalErrorIdentity\s*=\s*""\s*nearViewportPrefetchTerminalIdentity\s*=\s*""/.test(
			pooledLifecycleSource))
		verify(/resetPreviewLoaderIncubationWatchdog\(true\)\s*previewLoaderTerminalErrorIdentity\s*=\s*""\s*nearViewportPrefetchTerminalIdentity\s*=\s*""/.test(
			reusedLifecycleSource))
		const innerEligibilityStart =
			mainSource.indexOf("onRichContentViewportEligibleChanged:",
				innerDelegateStart)
		const innerEligibilityEnd =
			mainSource.indexOf("onStableIdChanged:", innerEligibilityStart)
		verify(innerDelegateStart >= 0 && innerEligibilityStart > innerDelegateStart
			&& innerEligibilityEnd > innerEligibilityStart)
		const innerEligibilitySource =
			mainSource.slice(innerEligibilityStart, innerEligibilityEnd)
		verify(!innerEligibilitySource.includes(
			"nearViewportPrefetchTerminalIdentity = \"\""))
		verify(!innerEligibilitySource.includes(
			"previewLoaderTerminalErrorIdentity = \"\""))
		const innerStableIdEnd =
			mainSource.indexOf("Component.onCompleted:", innerEligibilityEnd)
		verify(innerStableIdEnd > innerEligibilityEnd)
		const innerStableIdSource =
			mainSource.slice(innerEligibilityEnd, innerStableIdEnd)
		verify(/onStableIdChanged:\s*\{[\s\S]*previewLoaderTerminalErrorIdentity\s*=\s*""[\s\S]*nearViewportPrefetchTerminalIdentity\s*=\s*""/.test(
			innerStableIdSource))
		verify(/property bool outerLoaderReady:\s*false[\s\S]*property bool outerLoaderIncubationSuppressed:\s*false[\s\S]*function\s+recoverOuterLoaderIncubationTimeout\(\)[\s\S]*!inVisibleViewport[\s\S]*nearViewportPrefetchActiveId[\s\S]*===\s*ownedStableId[\s\S]*return[\s\S]*outerLoaderIncubationSuppressed\s*=\s*true[\s\S]*cancelRichLoaderResume\(ownedStableId\)[\s\S]*id:\s*outerLoaderIncubationWatchdog[\s\S]*interval:\s*192/.test(mainSource))
		verify(/onLoaded:\s*\{[\s\S]*outerIncubationRecovered[\s\S]*resetOuterLoaderIncubationWatchdog\(true\)[\s\S]*outerLoaderReady\s*=\s*true[\s\S]*scheduleRichContentResume\(\)/.test(mainSource))
		const deferredDelegateSource = mainSource.slice(
			mainSource.indexOf("id: deferredInlineMessageDelegate"),
			mainSource.indexOf("id: asyncMessageDelegate"))
		verify(!deferredDelegateSource.includes("resetOuterLoaderIncubationWatchdog"))
		const asyncDelegateSource = mainSource.slice(
			mainSource.indexOf("id: asyncMessageDelegate"),
			mainSource.indexOf("id: messageDelegate"))
		verify(/property string deferredMaterializationCleanupId:\s*""[\s\S]*function releaseDeferredMaterialization\(stableIdOverride\)[\s\S]*timeline\.completeDeferredMaterialization\(key\)/.test(
			asyncDelegateSource))
		verify(/function recoverOuterLoaderIncubationTimeout\(\)[\s\S]*cancelRichLoaderResume\(ownedStableId\)[\s\S]*releaseDeferredMaterialization\(ownedStableId\)/.test(
			asyncDelegateSource))
		verify(/ListView\.onPooled:\s*\{[\s\S]*const retiredStableId[\s\S]*releaseDeferredMaterialization\(retiredStableId\)/.test(
			asyncDelegateSource))
		verify(/onStableIdChanged:\s*\{[\s\S]*deferredMaterializationCleanupId[\s\S]*releaseDeferredMaterialization\(retiredMaterializationId\)/.test(
			asyncDelegateSource))
		verify(/Component\.onDestruction:\s*\{[\s\S]*releaseDeferredMaterialization\(retiredStableId\)/.test(
			asyncDelegateSource))
		verify(/property string viewportProbeStableId:\s*""[\s\S]*property real viewportProbeHeight:\s*Math\.max\(\s*timeline\.deferredMessageRowHeight,\s*deferredPresentationHeight\)[\s\S]*property bool inVisibleViewport:\s*false[\s\S]*property bool inHydrationWindow:\s*false/.test(
			asyncDelegateSource))
		const viewportProbeSeedMatch =
			/property real viewportProbeHeight:[\s\S]*?\n\s*function viewportProbeBaseHeight\(\)/.exec(
				asyncDelegateSource)
		verify(viewportProbeSeedMatch !== null)
		verify(!/\bitem\b|\brichGeometrySettling\b|\bheight\b/.test(
			viewportProbeSeedMatch[0]))
		const viewportProbeUpdateStart =
			asyncDelegateSource.indexOf("function updateViewportProbeHeight()")
		const viewportProbeUpdateEnd =
			asyncDelegateSource.indexOf("function scheduleViewportProbeHeightUpdate()",
				viewportProbeUpdateStart)
		verify(viewportProbeUpdateStart >= 0
			&& viewportProbeUpdateEnd > viewportProbeUpdateStart)
		const viewportProbeUpdateSource =
			asyncDelegateSource.slice(viewportProbeUpdateStart, viewportProbeUpdateEnd)
		verify(/viewportProbeStableId\s*!==\s*identity[\s\S]*resetViewportProbeHeight\(\)[\s\S]*accessibilityPooled\s*\|\|\s*richGeometrySettling\s*\|\|\s*!item[\s\S]*return[\s\S]*const settledHeight\s*=\s*Math\.max\([\s\S]*item\.lightweightMessageRowHeight[\s\S]*item\.implicitHeight[\s\S]*Number\(height\s*\|\|\s*0\)[\s\S]*settledHeight\s*>\s*viewportProbeHeight[\s\S]*viewportProbeHeight\s*=\s*settledHeight/.test(
			viewportProbeUpdateSource))
		const viewportMembershipUpdateStart =
			asyncDelegateSource.indexOf("function updateViewportMembership()")
		const viewportMembershipUpdateEnd =
			asyncDelegateSource.indexOf("function scheduleViewportProbeHeightUpdate()",
				viewportMembershipUpdateStart)
		verify(viewportMembershipUpdateStart >= 0
			&& viewportMembershipUpdateEnd > viewportMembershipUpdateStart)
		const viewportMembershipUpdateSource = asyncDelegateSource.slice(
			viewportMembershipUpdateStart, viewportMembershipUpdateEnd)
		verify(/const rowTop\s*=\s*Number\(y\s*\|\|\s*0\)[\s\S]*const extent\s*=\s*Math\.max\(0,[\s\S]*Number\(viewportProbeHeight\s*\|\|\s*0\)[\s\S]*const viewportTop\s*=\s*Number\(timeline\.contentY\s*\|\|\s*0\)[\s\S]*const viewportBottom\s*=\s*viewportTop[\s\S]*Number\(timeline\.height\s*\|\|\s*0\)[\s\S]*const warmDistance\s*=\s*Math\.min\([\s\S]*timeline\.baseCacheBuffer[\s\S]*Number\(timeline\.height\s*\|\|\s*0\)\s*\*\s*1\.5/.test(
			viewportMembershipUpdateSource))
		verify(/const nextVisible\s*=\s*!accessibilityPooled[\s\S]*rowTop\s*<=\s*viewportBottom[\s\S]*rowTop\s*\+\s*extent\s*>=\s*viewportTop[\s\S]*const nextHydration\s*=\s*!accessibilityPooled[\s\S]*rowTop\s*<=\s*viewportBottom\s*\+\s*warmDistance[\s\S]*rowTop\s*\+\s*extent\s*>=\s*viewportTop\s*-\s*warmDistance[\s\S]*const visibleChanged\s*=\s*inVisibleViewport\s*!==\s*nextVisible[\s\S]*const hydrationChanged\s*=\s*inHydrationWindow\s*!==\s*nextHydration[\s\S]*if\s*\(!visibleChanged\s*&&\s*!hydrationChanged\)\s*return[\s\S]*viewportMembershipUpdating\s*=\s*true\s*inHydrationWindow\s*=\s*nextHydration\s*inVisibleViewport\s*=\s*nextVisible\s*viewportMembershipUpdating\s*=\s*false\s*reconcileViewportMembership\(\)\s*if\s*\(item\s*&&\s*item\.reconcileViewportMembership\)\s*item\.reconcileViewportMembership\(\)/.test(
			viewportMembershipUpdateSource))
		verify(/property bool viewportMembershipUpdating:\s*false/.test(
			asyncDelegateSource))
		verify(/id:\s*viewportProbeUpdateTimer[\s\S]*interval:\s*0[\s\S]*repeat:\s*false[\s\S]*updateViewportProbeHeight\(\)/.test(
			asyncDelegateSource))
		verify(/onRichGeometrySettlingChanged:[\s\S]*!richGeometrySettling[\s\S]*scheduleViewportProbeHeightUpdate\(\)[\s\S]*height:\s*Math\.max[\s\S]*onHeightChanged:[\s\S]*item\.richResourcesRequired[\s\S]*!richGeometrySettling[\s\S]*scheduleViewportProbeHeightUpdate\(\)/.test(
			asyncDelegateSource))
		verify(/function scheduleViewportProbeReset\(\)[\s\S]*viewportProbeStableId[\s\S]*!==\s*identity[\s\S]*resetViewportProbeHeight\(\)[\s\S]*scheduleViewportProbeHeightUpdate\(\)/.test(
			asyncDelegateSource))
		verify(/onViewportProbeHeightChanged:\s*timeline\.scheduleViewportMembershipUpdate\(\)[\s\S]*onYChanged:\s*timeline\.scheduleViewportMembershipUpdate\(\)/.test(
			asyncDelegateSource))
		verify(/ListView\.onPooled:\s*\{\s*accessibilityPooled\s*=\s*true\s*updateViewportMembership\(\)[\s\S]*viewportProbeUpdateTimer\.stop\(\)[\s\S]*ListView\.onReused:\s*\{[\s\S]*viewportProbeUpdateTimer\.stop\(\)\s*resetViewportProbeHeight\(\)\s*if\s*\(item\)\s*item\.handleListViewReused\(\)\s*accessibilityPooled\s*=\s*false\s*updateViewportMembership\(\)\s*scheduleViewportProbeHeightUpdate\(\)/.test(
			asyncDelegateSource))
		verify(/onStableIdChanged:\s*\{\s*scheduleViewportProbeReset\(\)\s*timeline\.scheduleViewportMembershipUpdate\(\)[\s\S]*Component\.onCompleted:\s*\{\s*(?:deferredMaterializationCleanupId\s*=\s*String\(stableId\s*\|\|\s*""\)\s*)?scheduleViewportProbeReset\(\)\s*updateViewportMembership\(\)[\s\S]*onLoaded:[\s\S]*scheduleViewportProbeHeightUpdate\(\)/.test(
			asyncDelegateSource))
		verify(!/property bool inVisibleViewport:\s*!accessibilityPooled/.test(
			asyncDelegateSource))
		verify(!/property bool inHydrationWindow:\s*!accessibilityPooled/.test(
			asyncDelegateSource))
		const hydrationWindowStart =
			mainSource.indexOf("readonly property bool inHydrationWindow:")
		const hydrationWindowEnd =
			mainSource.indexOf("readonly property bool richContentViewportEligible:",
				hydrationWindowStart)
		verify(hydrationWindowStart >= 0 && hydrationWindowEnd > hydrationWindowStart)
		const hydrationWindowSource =
			mainSource.slice(hydrationWindowStart, hydrationWindowEnd)
		verify(/readonly property bool inHydrationWindow:\s*asyncMessageDelegate\.inHydrationWindow[\s\S]*readonly property bool inVisibleViewport:\s*asyncMessageDelegate\.inVisibleViewport/.test(
			hydrationWindowSource))
		verify(!/asyncMessageDelegate\.(?:y|height|viewportProbeHeight)|timeline\.contentY/.test(
			hydrationWindowSource))
		verify(/visible:\s*asyncMessageDelegate\.active\s*&&\s*\(!asyncMessageDelegate\.item\s*\|\|\s*!asyncMessageDelegate\.item\.currentMessagePresentationReady\s*\|\|\s*\(asyncMessageDelegate\.item\.richResourcesRequired\s*&&\s*!asyncMessageDelegate\.item\.richResourcesActive\)\s*\|\|\s*asyncMessageDelegate\.item\s*\.terminalPreviewFallbackUnavailable/.test(
			asyncDelegateSource))
		verify(/readonly property bool previewLoaderFailed:\s*item\s*\?\s*item\.previewLoaderFailed\s*:\s*false\s*readonly property bool nearViewportPrefetchTerminal:\s*item\s*\?\s*item\.nearViewportPrefetchTerminal\s*:\s*false/.test(
			asyncDelegateSource))
		verify(/function markNearViewportPrefetchTerminal\(\)\s*\{\s*if\s*\(item\)\s*item\.markNearViewportPrefetchTerminal\(\)\s*\}/.test(
			asyncDelegateSource))
		verify(/property bool inVisibleViewport:\s*false[\s\S]*readonly property bool richContentViewportEligible:[\s\S]*nearViewportWarmId[\s\S]*function reconcileViewportMembership\(\)[\s\S]*richContentViewportEligible\s*\|\|\s*item[\s\S]*resetOuterLoaderIncubationWatchdog\(true\)[\s\S]*cancelRichLoaderResume\(ownedStableId\)[\s\S]*onRichContentViewportEligibleChanged:\s*\{\s*if\s*\(viewportMembershipUpdating\)\s*return\s*reconcileViewportMembership\(\)/.test(
			asyncDelegateSource))
		const innerViewportReconcileStart =
			mainSource.indexOf("function reconcileViewportMembership()", innerDelegateStart)
		const innerViewportReconcileEnd =
			mainSource.indexOf("onStableIdChanged:", innerViewportReconcileStart)
		verify(innerViewportReconcileStart > innerDelegateStart
			&& innerViewportReconcileEnd > innerViewportReconcileStart)
		const innerViewportReconcileSource = mainSource.slice(
			innerViewportReconcileStart, innerViewportReconcileEnd)
		verify(/function reconcileViewportMembership\(\)\s*\{\s*updateRichResourceActivity\(\)\s*requestPreviewHydrationIfNeeded\(\)[\s\S]*!richContentViewportEligible[\s\S]*cancelRichLoaderResume\(stableId\)[\s\S]*previewMaterializationAdmitted\s*=\s*false[\s\S]*suspendRichResourcesForScroll\(\)[\s\S]*else\s*\{\s*timeline\.scheduleRichContentResume\(\)[\s\S]*onInHydrationWindowChanged:\s*\{\s*if\s*\(!asyncMessageDelegate\.viewportMembershipUpdating\)\s*reconcileViewportMembership\(\)[\s\S]*onRichContentViewportEligibleChanged:\s*\{\s*if\s*\(!asyncMessageDelegate\.viewportMembershipUpdating\)\s*reconcileViewportMembership\(\)/.test(
			innerViewportReconcileSource))
		verify(/active:\s*!deferredPresentation[\s\S]*onActiveChanged:\s*\{[\s\S]*!active[\s\S]*resumeRequested\s*=\s*false[\s\S]*resumeAdmitsPreview\s*=\s*false[\s\S]*cancelRichLoaderResume\(ownedStableId\)/.test(asyncDelegateSource))
		verify(/onStableIdChanged:\s*\{[\s\S]*cancelRichLoaderResume\(ownedStableId\)[\s\S]*resumeRequested\s*=\s*false[\s\S]*resumeDuringFinalization\s*=\s*false[\s\S]*resumeAdmitsPreview\s*=\s*false[\s\S]*resumeOwnedStableId\s*=\s*""/.test(asyncDelegateSource))
		verify(/"outerReady":\s*item\.outerLoaderReady\s*===\s*true[\s\S]*item\.previewLoaderStatus[\s\S]*item\.previewLoaderIncubationSuppressed/.test(mainSource))
		const previewPlaceholderLoaderStart =
			mainSource.indexOf("id: messagePreviewHydrationPlaceholderLoader")
		const previewPlaceholderVisibilityEnd =
			mainSource.indexOf("Layout.fillWidth:", previewPlaceholderLoaderStart)
		verify(previewPlaceholderLoaderStart >= 0
			&& previewPlaceholderVisibilityEnd > previewPlaceholderLoaderStart)
		const previewPlaceholderVisibilitySource = mainSource.slice(
			previewPlaceholderLoaderStart, previewPlaceholderVisibilityEnd)
		verify(/active:\s*messageDelegate\.hasPreviewContent\s*&&\s*\(messageDelegate\.previewNeedsHydration\s*\|\|\s*messageDelegate\.previewTerminalUnavailable\s*\|\|\s*!messageDelegate\.previewMaterializationAdmitted\s*\|\|\s*messagePreviewLoader\.status\s*!==\s*Loader\.Ready\)[\s\S]*visible:\s*messageDelegate\.hasPreviewContent\s*&&\s*\(messageDelegate\.previewNeedsHydration\s*\|\|\s*messageDelegate\.previewTerminalUnavailable\s*\|\|\s*!messageDelegate\.previewMaterializationAdmitted\s*\|\|\s*messagePreviewLoader\.status\s*!==\s*Loader\.Ready\)/.test(
			previewPlaceholderVisibilitySource))
		verify(!previewPlaceholderVisibilitySource.includes(
			"previewLoaderSurfaceNonBlank"))
		const previewCardLoaderStart =
			mainSource.indexOf("id: messagePreviewLoader", previewPlaceholderVisibilityEnd)
		const previewCardVisibilityEnd =
			mainSource.indexOf("Layout.fillWidth:", previewCardLoaderStart)
		verify(previewCardLoaderStart > previewPlaceholderVisibilityEnd
			&& previewCardVisibilityEnd > previewCardLoaderStart)
		const previewCardVisibilitySource = mainSource.slice(
			previewCardLoaderStart, previewCardVisibilityEnd)
		verify(/visible:\s*active\s*&&\s*status\s*===\s*Loader\.Ready\s*&&\s*!!item\s*&&\s*messageDelegate\.hasPreviewContent\s*&&\s*!messageDelegate\.previewNeedsHydration/.test(
			previewCardVisibilitySource))
		verify(!previewCardVisibilitySource.includes(
			"previewLoaderSurfaceNonBlank"))
		verify(/ListView\.onPooled:[\s\S]*const\s+retiredStableId\s*=\s*resumeOwnedStableId\.length\s*>\s*0[\s\S]*abandonNearViewportPrefetch\([\s\S]*retiredStableId,\s*true\)[\s\S]*cancelRichLoaderResume\(retiredStableId\)[\s\S]*resumeOwnedStableId\s*=\s*""/.test(mainSource))
		verify(/Component\.onDestruction:[\s\S]*resumeOwnedStableId\.length\s*>\s*0[\s\S]*cancelRichLoaderResume\(resumeOwnedStableId\)/.test(mainSource))
		verify(/function\s+handleListViewPooled\(\)[\s\S]*cancelRichLoaderResume\(stableId\)[\s\S]*previewMaterializationAdmitted\s*=\s*false/.test(mainSource))
		verify(/onPreviewNeedsHydrationChanged:[\s\S]*backendHydrationRequested\s*=\s*false[\s\S]*cancelRichLoaderResume\(stableId\)[\s\S]*suspendRichResourcesForScroll\(\)[\s\S]*previewMaterializationAdmitted\s*=\s*false[\s\S]*requestPreviewHydrationIfNeeded\(\)[\s\S]*scheduleRichContentResume\(\)/.test(mainSource))
		verify(/onRichContentViewportEligibleChanged:[\s\S]*!richContentViewportEligible[\s\S]*cancelRichLoaderResume\(stableId\)[\s\S]*suspendRichResourcesForScroll\(\)[\s\S]*scheduleRichContentResume\(\)/.test(mainSource))
		verify(/cancelRichLoaderResume\(stableId\)[\s\S]{0,320}previewMaterializationAdmitted\s*=\s*false[\s\S]{0,160}suspendRichResourcesForScroll\(\)/.test(mainSource))
		verify(/function\s+suspendRichResourcesForScroll\(\)[\s\S]*resetPreviewLoaderIncubationWatchdog\(\)[\s\S]*richResourceActivationTimer\.stop\(\)/.test(mainSource))
		verify(/function\s+requestPreviewHydrationIfNeeded\(allowDuringFinalization\)[\s\S]*!backendHydrationRequested[\s\S]*richContentViewportEligible[\s\S]*!timeline\.scopePresentationPending\s*\|\|\s*pendingAllowed[\s\S]*!timeline\.richScrollInProgress[\s\S]*queuePreviewHydration\(stableId,\s*inVisibleViewport\)[\s\S]*backendHydrationRequested\s*=\s*true/.test(mainSource))
		verify(/ListView\.onPooled:[\s\S]*backendHydrationRequested\s*=\s*false[\s\S]*onStableIdChanged:[\s\S]*backendHydrationRequested\s*=\s*false/.test(mainSource))
		verify(/function\s+beginScopeChange\(\)[\s\S]*scopePresentationPending\s*=\s*true[\s\S]*function\s+finishScopePresentation\(forcedByDeadline\)[\s\S]*scopePresentationFinalizeStartTimer\.restart\(\)/.test(mainSource))
		verify(!mainSource.includes("scopeReuseResetActive"))
		verify(/id:\s*messageAttachmentLoader[\s\S]*active:\s*messageDelegate\.hasAttachmentContent[\s\S]*!messageDelegate\.attachmentNeedsHydration[\s\S]*sourceComponent:\s*Component\s*\{[\s\S]*AttachmentGallery\s*\{[\s\S]*resourceActive:\s*messageDelegate\.richResourcesActive/.test(mainSource))
		verify(/id:\s*messageAttachmentHydrationPlaceholder[\s\S]*visible:\s*messageDelegate\.hasAttachmentContent[\s\S]*messageDelegate\.attachmentNeedsHydration/.test(mainSource))
		verify(/function\s+deferPrependedRichRows\(first,\s*last\)[\s\S]*row\.deferredPresentation\s*!==\s*true[\s\S]*deferred\[stableId\]\s*=\s*true[\s\S]*deferredPrependRichIds\s*=\s*deferred/.test(mainSource))
		verify(/function\s+onPrependPendingChanged\(\)[\s\S]*if\s*\(chatModel\.prependPending\)[\s\S]*chatModel\.presentationBusy\s*=\s*true[\s\S]*if\s*\(!timeline\.deferredPrependAnchorActive\)[\s\S]*timeline\.scheduleRichContentResume\(\)/.test(mainSource))
		verify(/function\s+onRowsAboutToBePrepended\(count\)[\s\S]*capturePrependAnchor\(\)/.test(mainSource))
		verify(!/prependCacheConstrained/.test(mainSource))
		verify(/function\s+prependRichContentActive\(stableId,\s*rowVisible\)[\s\S]*rowVisible\s*&&\s*!richScrollInProgress/.test(mainSource))
		verify(/function\s+markPrependRichRowMaterialized\(stableId\)[\s\S]*delete\s+deferred\[key\][\s\S]*deferredPrependRichIds\s*=\s*deferred/.test(mainSource))
		verify(/function\s+onRowsInserted\(parentIndex,\s*first,\s*last\)[\s\S]*extendDeferredPrependAnchorTarget\(last\s*-\s*first\s*\+\s*1\)[\s\S]*Qt\.callLater[\s\S]*restoreDeferredPrependAnchor\(\)/.test(mainSource))
		verify(/function\s+capturePrependAnchor\(\)[\s\S]*prependAnchorActive\s*=\s*true[\s\S]*prependAnchorSettleTimer\.restart\(\)/.test(mainSource))
		verify(/function\s+releasePrependAnchor\(preserveViewportAnchor\)[\s\S]*preserveViewportAnchor\s*===\s*true[\s\S]*preservedAnchorId\s*=\s*prependAnchorId[\s\S]*prependAnchorActive\s*=\s*false[\s\S]*viewportAnchorId\s*=\s*preservedAnchorId[\s\S]*viewportAnchorOffset\s*=\s*preservedAnchorOffset[\s\S]*viewportAnchorActive\s*=\s*true[\s\S]*scheduleRichContentResume\(\)/.test(mainSource))
		verify(/id:\s*richContentResumeTimer[\s\S]*!timeline\.stickToBottom\s*&&\s*!timeline\.viewportAnchorActive[\s\S]*timeline\.captureViewportAnchor\(\)[\s\S]*timeline\.queueVisibleDeferredMaterialization\(\)/.test(mainSource))
		verify(/function\s+timelinePresentationState\(\)[\s\S]*"viewportAnchorActive":\s*timeline\.viewportAnchorActive[\s\S]*"viewportAnchorId":\s*timeline\.viewportAnchorId[\s\S]*"viewportAnchorOffset":\s*timeline\.viewportAnchorOffset/.test(mainSource))
		verify(/id:\s*prependAnchorSettleTimer[\s\S]*interval:\s*48[\s\S]*correctionPending[\s\S]*prependAnchorCorrectionFrames\s*>\s*0[\s\S]*restoreDeferredPrependAnchor\(\)[\s\S]*restorePrependAnchor\(\)[\s\S]*releasePrependAnchor\(true\)/.test(mainSource))
		verify(!/id:\s*prependAnchorSettleTimer[\s\S]{0,900}pendingScopeHydrationCount\(\)/.test(mainSource))
		verify(/function\s+onRowsAboutToChange\(first,\s*last\)[\s\S]*!chatModel\.presentationBusy[\s\S]*!timeline\.topAnchorActive[\s\S]*!timeline\.prependAnchorActive[\s\S]*!timeline\.viewportAnchorActive[\s\S]*capturePrependAnchor\(\)/.test(mainSource))
		verify(/id:\s*messagePreviewHydrationPlaceholderLoader[\s\S]*active:\s*messageDelegate\.hasPreviewContent[\s\S]*messageDelegate\.previewNeedsHydration[\s\S]*!messageDelegate\.previewMaterializationAdmitted[\s\S]*messagePreviewLoader\.status\s*!==\s*Loader\.Ready[\s\S]*objectName:\s*"previewHydrationPlaceholder"/.test(mainSource))
		const previewHydrationPlaceholderStart =
			mainSource.indexOf("id: messagePreviewHydrationPlaceholderLoader")
		const previewHydrationPlaceholderEnd =
			mainSource.indexOf("id: messagePreviewLoader", previewHydrationPlaceholderStart)
		verify(previewHydrationPlaceholderStart >= 0
			&& previewHydrationPlaceholderEnd > previewHydrationPlaceholderStart)
		const previewHydrationPlaceholderSource = mainSource.slice(
			previewHydrationPlaceholderStart, previewHydrationPlaceholderEnd)
		verify(/objectName:\s*"previewHydrationPlaceholder"[\s\S]*objectName:\s*"previewHydrationCompactRow"[\s\S]*objectName:\s*"previewHydrationProviderBadge"[\s\S]*objectName:\s*"previewHydrationOpenButton"/.test(
			previewHydrationPlaceholderSource))
		verify(/previewTerminalUnavailable[\s\S]*previewStagingHeight[\s\S]*previewPlaceholderHeight/.test(
			previewHydrationPlaceholderSource))
		verify(!previewHydrationPlaceholderSource.includes("ModernBusyIndicator"))
		verify(!previewHydrationPlaceholderSource.includes("previewHydrationMediaSkeleton"))
		verify(/id:\s*messagePreviewLoader[\s\S]*!messageDelegate\.previewNeedsHydration[\s\S]*!messageDelegate\.previewTerminalUnavailable[\s\S]*messageDelegate\.previewMaterializationAdmitted/.test(mainSource))
		verify(/id:\s*messagePreviewLoader[\s\S]*active:\s*messageDelegate\.hasPreviewContent[\s\S]*!messageDelegate\.previewNeedsHydration[\s\S]*!messageDelegate\.richLoaderSuspended[\s\S]*timeline\.prependRichContentActive\([\s\S]*sourceComponent:\s*Component\s*\{[\s\S]*RichPreviewCard\s*\{[\s\S]*renderActive:\s*messageDelegate\.richResourcesActive/.test(mainSource))
		verify(/readonly property string performancePreviewSurfaceState:[\s\S]*messageRowLoader\.status\s*===\s*Loader\.Ready[\s\S]*messageRowLoader\.item\.previewSurfaceState/.test(mainSource))
		const messageRowPreviewSurfaceStart = mainSource.indexOf(
			"readonly property string previewSurfaceState:", innerDelegateStart)
		const messageRowPreviewSurfaceEnd = mainSource.indexOf(
			"function openActions()", messageRowPreviewSurfaceStart)
		verify(messageRowPreviewSurfaceStart > innerDelegateStart
			&& messageRowPreviewSurfaceEnd > messageRowPreviewSurfaceStart)
		const messageRowPreviewSurfaceSource = mainSource.slice(
			messageRowPreviewSurfaceStart, messageRowPreviewSurfaceEnd)
		verify(/messagePreviewLoader\.status\s*===\s*Loader\.Error\s*\?\s*"error"/.test(
			messageRowPreviewSurfaceSource))
		verify(/messageDelegate\.previewNeedsHydration[\s\S]*messagePreviewLoader\.status\s*!==\s*Loader\.Ready[\s\S]*messagePreviewHydrationPlaceholderLoader\.visible[\s\S]*messagePreviewLoader\.visible[\s\S]*messagePreviewLoader\.status\s*===\s*Loader\.Ready[\s\S]*!!messagePreviewLoader\.item[\s\S]*\?\s*messageDelegate\.previewLoaderSurfaceNonBlank[\s\S]*performanceEmbedSurfaceState\s*\|\|\s*"blank"[\s\S]*:\s*"loading"/.test(
			messageRowPreviewSurfaceSource))
		verify(!/\|\|\s*!messageDelegate\.previewLoaderSurfaceNonBlank/.test(
			messageRowPreviewSurfaceSource))
		verify(/id:\s*messageAttachmentLoader[\s\S]*active:\s*messageDelegate\.hasAttachmentContent[\s\S]*!messageDelegate\.richLoaderSuspended[\s\S]*timeline\.prependRichContentActive\(/.test(mainSource))
		verify(/previewPlaceholderHeight[\s\S]*attachmentPlaceholderHeight/.test(mainSource))
		verify(/id:\s*messagePreviewHydrationPlaceholderLoader[\s\S]*Layout\.preferredHeight:\s*visible[\s\S]*previewTerminalUnavailable[\s\S]*previewStagingHeight[\s\S]*previewPlaceholderHeight/.test(mainSource))
		verify(/id:\s*messageAttachmentLoader[\s\S]*Layout\.preferredHeight:[\s\S]*attachmentPlaceholderHeight/.test(mainSource))
		verify(/id:\s*messagePreviewLoader[\s\S]*active:\s*messageDelegate\.hasPreviewContent[\s\S]*messageDelegate\.previewMaterializationAdmitted[\s\S]*visible:\s*active\s*&&\s*status\s*===\s*Loader\.Ready\s*&&\s*!!item[\s\S]*Layout\.preferredHeight:\s*visible\s*&&\s*item[\s\S]*item\.implicitHeight\s*:\s*0/.test(mainSource))
		verify(/id:\s*messagePreviewLoader[\s\S]*onActiveChanged:[\s\S]*resetPreviewLoaderIncubationWatchdog\(\)[\s\S]*onLoaded:[\s\S]*armPreviewLoaderIncubationWatchdog\(\)[\s\S]*onStatusChanged:[\s\S]*Loader\.Error[\s\S]*Loader\.Ready/.test(mainSource))
		verify(/"visibleInactiveRichResourceCount":[\s\S]*visiblePresentation\.inactiveRichResourceCount/.test(mainSource))
		verify(/"richResourcesActive":[\s\S]*item\.richResourcesActive\s*===\s*true[\s\S]*"inactiveRichResources":\s*inactiveRichResources/.test(mainSource))
		verify(/"settled":[\s\S]*visiblePresentation\.inactiveRichResourceCount\s*===\s*0/.test(mainSource))
		verify(/objectName:\s*"previewHydrationStateLabel"[\s\S]*previewTerminalUnavailable[\s\S]*qsTr\("Preview unavailable"\)[\s\S]*objectName:\s*"previewHydrationStateIcon"[\s\S]*\?\s*"warning"\s*:\s*"link"[\s\S]*objectName:\s*"previewHydrationOpenButton"/.test(mainSource))
		verify(/id:\s*messageAttachmentLoader[\s\S]*visible:\s*messageDelegate\.hasAttachmentContent[\s\S]*Layout\.preferredHeight:[\s\S]*attachmentPlaceholderHeight/.test(mainSource))
		verify(/id:\s*messageAttachmentLoader[\s\S]*asynchronous:\s*true/.test(mainSource))
		verify(/id:\s*messagePreviewLoader[\s\S]*asynchronous:\s*true/.test(mainSource))
		verify(!/RichPreviewCard\s*\{\s*id:\s*messagePreviewCard/.test(mainSource))
	}

	function test_visible_tail_marks_the_active_scope_read_without_clearing_scrolled_history() {
		verify(/function\s+markVisibleScopeReadIfEligible\(\)[\s\S]*!root\.active[\s\S]*scopePresentationPending[\s\S]*unreadCount[\s\S]*!isNearBottom\(\)[\s\S]*uiCommands\.markActiveScopeRead\(\)/.test(mainSource))
		verify(/onUnreadCountChanged\(\)[\s\S]*markVisibleScopeReadIfEligible\(\)/.test(mainSource))
		verify(/onMovementEnded:[\s\S]*stickToBottom\s*=\s*isNearBottom\(\)[\s\S]*markVisibleScopeReadIfEligible\(\)/.test(mainSource))
		verify(/onActiveChanged:[\s\S]*timeline\.markVisibleScopeReadIfEligible\(\)/.test(mainSource))
	}

	function test_chat_scroll_reports_delegate_materialization_diagnostics() {
		verify(/function\s+visiblePresentationDiagnostics\(\)[\s\S]*visibleCount[\s\S]*readyCount[\s\S]*fullMessageCount[\s\S]*deferredSummaryCount[\s\S]*blankCount[\s\S]*outerLoaderReadyCount[\s\S]*rowMaterializedCount[\s\S]*rowLoaderReadyCount[\s\S]*previewReadyCount[\s\S]*blankPreviewSurfaceCount[\s\S]*previewSurfaceState[\s\S]*blankPreviewSurface/.test(mainSource))
		verify(/const\s+blankPreviewSurface\s*=[\s\S]{0,320}item\.lightweightDeferredPresentation\s*!==\s*true[\s\S]{0,120}item\.messageRowReady\s*===\s*true[\s\S]{0,120}previewKeyCount\s*>\s*0[\s\S]{0,120}previewSurfaceState\s*===\s*"blank"/.test(mainSource))
		verify(/function\s+timelinePresentationState\(\)[\s\S]*"richLoaderResumeQueueCount"[\s\S]*"richLoaderResumeActiveId"[\s\S]*"visibleOuterLoaderReadyCount"[\s\S]*"visibleRowMaterializedCount"[\s\S]*"visibleRowLoaderReadyCount"[\s\S]*"visiblePreviewReadyCount"[\s\S]*"visibleBlankPreviewSurfaceCount"/.test(mainSource))
		verify(/"deferredMaterializationActiveId":\s*timeline\.deferredMaterializationActiveId/.test(mainSource))
		verify(/function\s+timelineDelegateDiagnostics\(\)[\s\S]*"materialized"[\s\S]*"previewItems"[\s\S]*"attachmentItems"[\s\S]*"pooledEvents"[\s\S]*"reusedEvents"[\s\S]*"visibleFullMessageCount"[\s\S]*"visibleBlankPresentationCount"/.test(mainSource))
		verify(/function\s+performanceChatFixtureState\(\)[\s\S]*"delegateDiagnostics":\s*timelineDelegateDiagnostics\(\)/.test(mainSource))
		verify(/function\s+performanceChatScrollState\(\)[\s\S]*"delegateDiagnosticsDelta"[\s\S]*"previewsLoaded"[\s\S]*"attachmentsLoaded"/.test(mainSource))
	}
}
