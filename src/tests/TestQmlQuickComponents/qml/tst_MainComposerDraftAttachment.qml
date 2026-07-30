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
		verify(/item\.y \+ item\.height <= contentY \+ 0\.5/.test(mainSource))
		verify(/onMovementStarted[\s\S]*!prependMutationInProgress[\s\S]*releasePrependAnchor\(\)/.test(mainSource))
		verify(/FrameAnimation[\s\S]*prependAnchorCorrectionFrames > 0[\s\S]*timeline\.restorePrependAnchor\(\)/.test(mainSource))
		verify(/FrameAnimation[\s\S]*deferredPrependAnchorActive[\s\S]*timeline\.restoreDeferredPrependAnchor\(\)[\s\S]*timeline\.restorePrependAnchor\(\)/.test(mainSource))
		verify(/onRowsInserted[\s\S]*prependAnchorCorrectionFrames\s*=\s*Math\.max\([\s\S]*prependAnchorCorrectionFrames,\s*4\)/.test(mainSource))
		verify(/objectName:\s*"chatTimelineScrollBar"[\s\S]*visible:\s*root\.visualFixtureSurfaceVariant\s*!==\s*"chat-history-prepend-anchor"[\s\S]*Accessible\.ignored:\s*!visible/.test(mainSource))
	}

	function test_delayed_rich_card_height_changes_preserve_the_visible_message() {
		verify(/property string viewportAnchorId:\s*""[\s\S]*property bool viewportAnchorActive:\s*false/.test(mainSource))
		verify(/function\s+captureViewportAnchor\(\)[\s\S]*firstVisibleMessageDelegate\(\)[\s\S]*viewportAnchorOffset\s*=\s*item\.mapToItem\(timeline,\s*0,\s*0\)\.y/.test(mainSource))
		verify(/function\s+restoreViewportAnchor\(\)[\s\S]*presentationModel\.rowForStableId\(viewportAnchorId\)[\s\S]*item\.y\s*-\s*viewportAnchorOffset[\s\S]*contentY\s*=\s*desiredY/.test(mainSource))
		verify(/onContentHeightChanged:[\s\S]*viewportAnchorActive[\s\S]*Qt\.callLater\(function\(\)\s*\{\s*timeline\.restoreViewportAnchor\(\)/.test(mainSource))
		verify(/onMovementStarted:[\s\S]*releaseViewportAnchor\(\)[\s\S]*onMovementEnded:[\s\S]*!stickToBottom[\s\S]*captureViewportAnchor\(\)/.test(mainSource))
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
		verify(/onContentHeightChanged:\s*\{[\s\S]*scopePresentationObservationActive\s*&&\s*stickToBottom[\s\S]*!dragging\s*&&\s*!flicking[\s\S]*setTailContentY\(maximumY\)[\s\S]*containTailMessageWhenPossible\(\)/.test(mainSource))
		verify(/onMovementStarted:\s*\{[\s\S]*scopePresentationObservationActive\s*&&\s*stickToBottom[\s\S]*!dragging\s*&&\s*!flicking[\s\S]*requestBottomFollow\(\)[\s\S]*return/.test(mainSource))
		verify(/id:\s*scopePresentationQuietTimer[\s\S]*interval:\s*120[\s\S]*finishScopePresentation\(false\)/.test(mainSource))
		verify(/function\s+pendingScopeHydrationCount\(\)[\s\S]*contentNeedsHydration[\s\S]*inHydrationWindow/.test(mainSource))
		verify(/function\s+finishScopePresentation\(forcedByDeadline\)[\s\S]{0,420}scopePresentationFinalizing\s*=\s*true/.test(mainSource))
		verify(!/function\s+finishScopePresentation\(forcedByDeadline\)[\s\S]{0,260}pendingScopeHydrationCount\(\)\s*>\s*0/.test(mainSource))
		verify(/function\s+queueVisiblePreviewHydration\(allowDuringFinalization\)[\s\S]*pendingAllowed[\s\S]*scopePresentationFinalizing[\s\S]*inVisibleViewport[\s\S]*requestPreviewHydrationIfNeeded\(pendingAllowed\)/.test(mainSource))
		verify(/property bool coldPreviewHydrationPacingActive:\s*false[\s\S]*function\s+flushPreviewHydrationQueue\(\)[\s\S]*coldPreviewHydrationPacingActive[\s\S]*richLoaderResumeTimer\.running[\s\S]*richLoaderResumeQueue\.length\s*>\s*0[\s\S]*maxBatch\s*=\s*coldPreviewHydrationPacingActive\s*\?\s*1\s*:\s*32[\s\S]*batch\.length\s*<\s*maxBatch[\s\S]*coldPreviewHydrationPacingActive\s*=\s*false/.test(mainSource))
		verify(/required property bool bodyHydrationPending/.test(mainSource))
		verify(/property bool bodyNeedsHydration:\s*bodyHydrationPending[\s\S]*property bool contentNeedsHydration:[\s\S]*bodyNeedsHydration/.test(mainSource))
		verify(/property bool backendHydrationRequested:\s*false/.test(mainSource))
		verify(/function\s+requestPreviewHydrationIfNeeded\(allowDuringFinalization\)[\s\S]*pendingAllowed[\s\S]*backendContentNeedsHydration\s*&&\s*!backendHydrationRequested[\s\S]*inVisibleViewport[\s\S]*!timeline\.scopePresentationPending\s*\|\|\s*pendingAllowed[\s\S]*queuePreviewHydration[\s\S]*backendHydrationRequested\s*=\s*true/.test(mainSource))
		verify(/function\s+completeScopePresentationFinalization\(forcedByDeadline\)[\s\S]*scopePresentationPending\s*=\s*false[\s\S]*queueVisiblePreviewHydration\(\)/.test(mainSource))
		verify(/id:\s*scopePresentationDeadlineTimer[\s\S]*interval:\s*2000[\s\S]*finishScopePresentation\(true\)/.test(mainSource))
		verify(/function\s+noteScopePresentationMutation\(\)[\s\S]*scopePresentationFinalizing[\s\S]*scopePresentationFinalizeQuietTimer\.restart\(\)/.test(mainSource))
		verify(/function\s+finishScopePresentation\(forcedByDeadline\)[\s\S]*scopePresentationFinalizeStartTimer\.restart\(\)[\s\S]*function\s+continueScopePresentationFinalization\(\)[\s\S]*containTailMessageWhenPossible\(\)[\s\S]*queueRichLoaderResumes\(\)[\s\S]*queueVisiblePreviewHydration\(true\)[\s\S]*scopePresentationFinalizeQuietTimer\.start\(\)[\s\S]*scopePresentationFinalizeDeadlineTimer\.start\(\)/.test(mainSource))
		verify(/id:\s*scopePresentationFinalizeStartTimer[\s\S]*interval:\s*16[\s\S]*timeline\.continueScopePresentationFinalization\(\)/.test(mainSource))
		verify(/function\s+finishScopePresentation\(forcedByDeadline\)[\s\S]*coldPreviewHydrationPacingActive\s*=\s*true[\s\S]*queueVisiblePreviewHydration\(true\)/.test(mainSource))
		verify(/id:\s*scopePresentationFinalizeQuietTimer[\s\S]*interval:\s*120[\s\S]*completeScopePresentationFinalization\(false\)/.test(mainSource))
		verify(/id:\s*scopePresentationFinalizeDeadlineTimer[\s\S]*interval:\s*1500[\s\S]*completeScopePresentationFinalization\(true\)/.test(mainSource))
		verify(/function\s+completeScopePresentationFinalization\(forcedByDeadline\)[\s\S]*positionTailImmediately\(\)[\s\S]*containTailMessageWhenPossible\(\)[\s\S]*scopePresentationPending\s*=\s*false[\s\S]*scopePresentationObservationTimer\.restart\(\)/.test(mainSource))
		verify(/delegate:\s*DelegateChooser[\s\S]*ChatMessageFrame\s*\{[\s\S]*opacity:\s*timeline\.scopePresentationPending\s*\?\s*0\s*:\s*1/.test(mainSource))
		verify(/id:\s*emptyConversationState[\s\S]*visualLoading:\s*!activeScope\.activity\s*&&\s*\(activeScope\.loading[\s\S]*timeline\.scopePresentationPending\s*&&\s*chatModel\.count\s*>\s*0[\s\S]*Preparing conversation/.test(mainSource))
		verify(/function\s+timelinePresentationState\(\)[\s\S]*"scopeResetPending":\s*timeline\.scopeResetPending[\s\S]*exposedHeightChangeCount[\s\S]*exposedTailCorrectionCount[\s\S]*"settled":[\s\S]*!timeline\.scopeResetPending/.test(mainSource))
		verify(/function\s+timelinePresentationState\(\)[\s\S]*"prependQueuePending":\s*chatModel\.prependPending[\s\S]*"settled":[\s\S]*!chatModel\.prependPending/.test(mainSource))
		verify(/bodyNeedsHydration[\s\S]*prependRichContentActive\(stableId,[\s\S]*chatModel\.requestBodyHydration\(stableId\)/.test(mainSource))
		verify(/function\s+queueVisibleDeferredMaterialization\(\)[\s\S]*item\.contentNeedsHydration\s*!==\s*true[\s\S]*deferredHeaderVisible[\s\S]*deferredRepeater\.count\s*-\s*1[\s\S]*deferredMaterializationTimer\.start\(\)/.test(mainSource))
		verify(/deferredMaterializationsPerFrame:\s*6[\s\S]*function\s+materializeNextDeferredRow\(\)[\s\S]*materialized\s*<\s*deferredMaterializationsPerFrame[\s\S]*chatModel\.materializeDeferredMessage\(queued\.shift\(\)\)[\s\S]*topAnchorActive[\s\S]*topAnchorSettleTimer\.restart\(\)[\s\S]*topAnchorFrameTimer\.restart\(\)[\s\S]*deferredMaterializationTimer\.start\(\)[\s\S]*Qt\.callLater[\s\S]*queueVisibleDeferredMaterialization\(\)/.test(mainSource))
		const deferredStart = mainSource.indexOf("id: deferredMessageDelegate")
		const deferredEnd = mainSource.indexOf("DelegateChoice {", deferredStart + 1)
		verify(deferredStart >= 0 && deferredEnd > deferredStart)
		const deferredDelegateSource = mainSource.slice(deferredStart, deferredEnd)
		verify(/required property string stableId[\s\S]*contentNeedsHydration:\s*true/.test(deferredDelegateSource))
		verify(!/\bLoader\s*\{/.test(deferredDelegateSource))
		verify(!/\bConnections\s*\{/.test(deferredDelegateSource))
	}

	function test_activity_is_a_read_only_log_surface_without_conversation_loading_chrome() {
		verify(/function\s+canCompleteScopePresentationFastPath\(\)[\s\S]*if\s*\(activeScope\.activity\)[\s\S]*return\s+true/.test(mainSource))
		verify(/id:\s*emptyConversationState[\s\S]*activeScope\.activity\s*\?\s*chatModel\.count\s*===\s*0[\s\S]*qsTr\("No server log entries yet"\)[\s\S]*qsTr\("New Murmur server-log entries will appear here live\."\)/.test(mainSource))
		verify(/id:\s*composerSurface[\s\S]*visible:\s*!activeScope\.activity[\s\S]*Layout\.preferredHeight:\s*visible\s*\?/.test(mainSource))
		verify(/id:\s*activityLogColumnHeader[\s\S]{0,2400}visible:\s*activeScope\.activity[\s\S]{0,1200}qsTr\("TIME"\)[\s\S]{0,800}qsTr\("EVENT"\)[\s\S]{0,1200}qsTr\("LIVE"\)/.test(mainSource))
		verify(/id:\s*timeline[\s\S]{0,500}Accessible\.name:\s*activeScope\.activity\s*\?\s*qsTr\("Activity log"\)/.test(mainSource))
		verify(/function\s+stableAlternatingRow\(stableId\)[\s\S]*parity\s*\^=\s*value\.charCodeAt\(index\)\s*&\s*1[\s\S]*return\s+parity\s*===\s*1/.test(mainSource))
		verify(/ChatMessageFrame\s*\{[\s\S]{0,400}logEntry:\s*activeScope\.activity[\s\S]{0,200}logAlternating:\s*logEntry\s*&&\s*root\.stableAlternatingRow\(stableId\)/.test(mainSource))
		verify(!/id:\s*messageDelegate[\s\S]{0,360}required property int index/.test(mainSource))
		verify(/toggleQuickReactions\(messageDelegate\.stableId,[\s\S]{0,180}chatModel\.presentationModel\.rowForStableId\([\s\S]{0,80}messageDelegate\.stableId\)/.test(mainSource))
		verify(/id:\s*activityLogRowLoader[\s\S]{0,180}active:\s*messageDelegate\.logEntry[\s\S]{0,180}asynchronous:\s*true[\s\S]{0,200}id:\s*activityLogRow[\s\S]{0,1000}objectName:\s*"activityLogTimestamp"[\s\S]{0,1400}objectName:\s*"activityLogGutter"[\s\S]{0,1600}objectName:\s*"activityLogEvent"/.test(mainSource))
		verify(/function\s+resumeRichContentAfterScroll\(allowDuringFinalization\)[\s\S]*!timeline\.richScrollInProgress[\s\S]*!logEntry\s*&&\s*inVisibleViewport[\s\S]*messageRowMaterialized\s*=\s*true/.test(mainSource))
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
		verify(/function\s+canCompleteScopePresentationFastPath\(\)[\s\S]*activeScope\.loading[\s\S]*count\s*===\s*0[\s\S]*return\s+true[\s\S]*contentHeight\s*>\s*height\s*\+\s*0\.5[\s\S]*itemAtIndex\(0\)[\s\S]*itemAtIndex\(count\s*-\s*1\)/.test(mainSource))
		verify(/function\s+canCompleteScopePresentationFastPath\(\)[\s\S]*rowNeedsDeferredPresentation\(chatModel\.get\(index\)\)[\s\S]*return\s+false[\s\S]*forceLayout\(\)/.test(mainSource))
		verify(/function\s+completeScopePresentationFastPath\(\)[\s\S]*scopePresentationQuietTimer\.stop\(\)[\s\S]*scopeResetPending\s*=\s*false[\s\S]*scopePresentationFastPath\s*=\s*true[\s\S]*scopePresentationPending\s*=\s*false/.test(mainSource))
		verify(/id:\s*scopePresentationFastPathTimer[\s\S]*interval:\s*0[\s\S]*completeScopePresentationFastPath\(\)/.test(mainSource))
		verify(/function\s+onLoadingChanged\(\)[\s\S]*scheduleScopePresentationFastPath\(\)/.test(mainSource))
		verify(/function\s+onCountChanged\(\)[\s\S]*scheduleScopePresentationFastPath\(\)/.test(mainSource))
		verify(/"presentationFastPath":\s*timeline\.scopePresentationFastPath/.test(mainSource))
	}

	function test_performance_chat_scroll_is_frame_driven_without_a_qml_animation() {
		verify(/function\s+preparePerformanceChatScrollWorkload\(stepCount,\s*fullRangeToStart\)[\s\S]*bottomFollowTimer\.stop\(\)[\s\S]*timeline\.stickToBottom\s*=\s*false[\s\S]*performanceChatScrollRunning\s*=\s*true/.test(mainSource))
		verify(/fullRangeToStart\s*===\s*true[\s\S]{0,280}performanceChatScrollTargetY\s*=\s*minimumY/.test(mainSource))
		verify(/performanceChatScrollFullRangeToStart[\s\S]*currentOriginY\s*=\s*timeline\.originY[\s\S]*Math\.max\(currentOriginY,\s*previousY\s*-\s*wheelStep\)[\s\S]*reachedTarget/.test(mainSource))
		verify(/const travelDistance\s*=\s*Math\.min\(maximumY - minimumY,\s*requestedSteps \* 16\)[\s\S]*performanceChatScrollStartY - travelDistance/.test(mainSource))
		verify(/function\s+advancePerformanceChatScrollWorkload\(step,\s*totalSteps\)[\s\S]*timeline\.contentY\s*=\s*performanceChatScrollStartY/.test(mainSource))
		verify(/requestedSteps\s*\*\s*2/.test(mainSource))
		verify(/const liveRangeUnavailable\s*=\s*fullRangeToStart\s*===\s*true[\s\S]*timeline\.count\s*<\s*2[\s\S]*loadedTravel\s*<\s*8[\s\S]*const fixtureRangeUnavailable\s*=\s*fullRangeToStart\s*!==\s*true[\s\S]*timeline\.count\s*<\s*20/.test(mainSource))
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
		verify(/id:\s*timeline[\s\S]*model:\s*chatModel\.presentationModel[\s\S]*reuseItems:\s*true[\s\S]*baseCacheBuffer:[\s\S]*height\s*\*\s*0\.75[\s\S]*richScrollRetentionBuffer:\s*0[\s\S]*richScrollRetentionTarget:\s*0[\s\S]*richScrollRetentionRampStep:\s*128[\s\S]*maximumRichScrollRetentionBuffer:\s*18000[\s\S]*cacheBuffer:\s*Math\.max\(baseCacheBuffer,\s*richScrollRetentionBuffer\)/.test(mainSource))
		verify(/onRichScrollInProgressChanged:[\s\S]*if\s*\(richScrollInProgress\)[\s\S]*richScrollRetentionTarget\s*=\s*Math\.min\(maximumRichScrollRetentionBuffer,[\s\S]*contentHeight\s*\+\s*height[\s\S]*richScrollRetentionRampTimer\.start\(\)/.test(mainSource))
		verify(/function\s+beginScopeChange\(\)[\s\S]*richScrollRetentionRampTimer\.stop\(\)[\s\S]*richScrollRetentionBuffer\s*=\s*0[\s\S]*richScrollRetentionTarget\s*=\s*0/.test(mainSource))
		verify(/id:\s*richScrollRetentionRampTimer[\s\S]*interval:\s*16[\s\S]*repeat:\s*true[\s\S]*richScrollRetentionBuffer\s*=\s*Math\.min\([\s\S]*richScrollRetentionRampStep[\s\S]*stop\(\)/.test(mainSource))
		verify(/id:\s*deferredHistoryRepeater[\s\S]*model:\s*chatModel\.deferredPresentationModel[\s\S]*deferredPrefixItem:\s*true[\s\S]*contentNeedsHydration:\s*true/.test(mainSource))
		verify(/id:\s*deferredHistoryRow[\s\S]{0,900}summaryText:\s*\[title,\s*subtitle,\s*timestamp\][\s\S]{0,260}join\("  ·  "\)[\s\S]{0,700}text:\s*deferredHistoryRow\.summaryText/.test(mainSource))
		verify(!/id:\s*deferredHistoryRow[\s\S]{0,350}required property var source/.test(mainSource))
		verify(!/id:\s*deferredHistoryRow[\s\S]{0,1600}ColumnLayout\s*\{/.test(mainSource))
		verify(/performanceChatScrollFullRangeToStart[\s\S]*const wheelStep\s*=\s*Math\.max\(8,\s*Math\.min\(16,\s*timeline\.height \* 0\.02\)\)/.test(mainSource))
		verify(/id:\s*timelineScrollHandler[\s\S]*targetFlickable:\s*timeline[\s\S]*horizontalEnabled:\s*false[\s\S]*smoothWheelEnabled:\s*true[\s\S]*wheelStep:\s*Math\.max\(80,\s*Math\.min\(112,\s*timeline\.height \* 0\.14\)\)/.test(mainSource))
		verify(/property bool richScrollInProgress:[\s\S]*timelineScrollHandler\.scrolling[\s\S]*performanceChatScrollRunning/.test(mainSource))
		verify(/onRichScrollInProgressChanged:[\s\S]*if\s*\(richScrollInProgress\)[\s\S]*chatModel\.presentationBusy\s*=\s*true[\s\S]*richLoaderResumeTimer\.stop\(\)[\s\S]*deferredMaterializationTimer\.stop\(\)[\s\S]*suspendRichContentForScroll\(\)/.test(mainSource))
		verify(/onRichScrollInProgressChanged:[\s\S]*if\s*\(richScrollInProgress\)[\s\S]*previewHydrationTimer\.stop\(\)[\s\S]*Object\.keys\(root\.pendingPreviewHydrationIds\)\.length\s*>\s*0[\s\S]*previewHydrationTimer\.start\(\)/.test(mainSource))
		const suspendMatch = /function\s+suspendRichContentForScroll\(\)\s*\{([\s\S]*?)\n\t\t\t\t\t\}/.exec(mainSource)
		verify(suspendMatch !== null)
		verify(/richLoaderResumeQueue\s*=\s*\[\][\s\S]*item\.richResourcesActive\s*=\s*false/.test(suspendMatch[1]))
		verify(!/item\.richLoaderSuspended\s*=\s*true/.test(suspendMatch[1]))
		verify(/item\.richLoaderSuspended\s*!==\s*true[\s\S]*item\.richResourcesActive\s*===\s*true/.test(mainSource))
		verify(/function\s+scheduleRichContentResume\(\)[\s\S]*richContentResumeTimer\.restart\(\)[\s\S]*function\s+queueRichLoaderResumes\(\)[\s\S]*candidates\.sort[\s\S]*richLoaderResumeQueue/.test(mainSource))
		verify(/const visible = point\.y \+ item\.height > 0\.5[\s\S]*if \(!visible\)\s*continue[\s\S]*candidates\.push/.test(mainSource))
		verify(/function\s+resumeNextRichLoader\(\)[\s\S]*resumeRichContentAfterScroll\([\s\S]*scopePresentationFinalizing[\s\S]*finishRichContentRecoveryIfIdle\(\)[\s\S]*id:\s*richLoaderResumeTimer[\s\S]*interval:\s*16/.test(mainSource))
		verify(/property string richLoaderResumeActiveId:\s*""/.test(mainSource))
		verify(/function\s+resumeNextRichLoader\(\)[\s\S]*if\s*\(richLoaderResumeActiveId\.length\s*>\s*0\)[\s\S]*richLoaderResumeActiveId\s*=\s*stableId[\s\S]*item\.messageRowReady\s*===\s*true/.test(mainSource))
		verify(/function\s+completeRichLoaderResume\(stableId\)[\s\S]*richLoaderResumeActiveId\s*=\s*""[\s\S]*richLoaderResumeTimer\.start\(\)/.test(mainSource))
		verify(/function\s+finishRichContentRecoveryIfIdle\(\)[\s\S]*Qt\.callLater[\s\S]*richLoaderResumeQueue\.length\s*>\s*0[\s\S]*deferredMaterializationQueue\.length\s*>\s*0[\s\S]*chatModel\.presentationBusy\s*=\s*false/.test(mainSource))
		verify(/id:\s*richContentResumeTimer[\s\S]*captureViewportAnchor\(\)[\s\S]*queueRichLoaderResumes\(\)[\s\S]*queueVisibleDeferredMaterialization\(\)/.test(mainSource))
		verify(/function\s+timelinePresentationState\(\)[\s\S]*"settled":[\s\S]*!timeline\.prependAnchorActive[\s\S]*!chatModel\.prependPending/.test(mainSource))
		verify(!/"settled":[\s\S]{0,400}!timeline\.topAnchorActive/.test(mainSource))
		verify(!/"settled":[\s\S]{0,400}!timeline\.scopePresentationObservationActive/.test(mainSource))
		verify(/function\s+completePerformanceChatScrollWorkload\(\)[\s\S]*!timeline\.topAnchorActive[\s\S]*performanceChatScrollFullRangeToStart[\s\S]*timeline\.beginTopAnchor\(\)/.test(mainSource))
		verify(/function\s+captureViewportAnchor\(\)[\s\S]*Math\.abs\(contentY\s*-\s*originY\)\s*<=\s*0\.5[\s\S]*beginTopAnchor\(\)[\s\S]*function\s+beginTopAnchor\(\)[\s\S]*topAnchorActive\s*=\s*true[\s\S]*topAnchorFrameTimer\.restart\(\)[\s\S]*function\s+restoreTopAnchor\(\)[\s\S]*contentY\s*=\s*originY/.test(mainSource))
		verify(/function\s+capturePrependAnchor\(\)[\s\S]*releaseTopAnchor\(\)[\s\S]*prependAnchorOffset[\s\S]*prependAnchorActive\s*=\s*true/.test(mainSource))
		verify(/onContentHeightChanged:[\s\S]*topAnchorActive[\s\S]*restoreTopAnchor\(\)[\s\S]*id:\s*topAnchorFrameTimer[\s\S]*interval:\s*16[\s\S]*repeat:\s*false[\s\S]*id:\s*topAnchorSettleTimer[\s\S]*interval:\s*180[\s\S]*recoveryPending[\s\S]*pendingScopeHydrationCount\(\)[\s\S]*restoreTopAnchor\(\)[\s\S]*restart\(\)[\s\S]*topAnchorActive\s*=\s*false/.test(mainSource))
		verify(!/function\s+restoreTopAnchor\(\)[\s\S]{0,240}positionViewAtBeginning\(\)/.test(mainSource))
		verify(!/function\s+restoreTopAnchor\(\)[\s\S]{0,240}forceLayout\(\)/.test(mainSource))
		verify(/property bool richResourcesActive:\s*false[\s\S]*function\s+updateRichResourceActivity\(\)[\s\S]*!timeline\.richScrollInProgress[\s\S]*richResourcesActive\s*=\s*true/.test(mainSource))
		verify(/hydrationWarmDistance:\s*Math\.min\(timeline\.baseCacheBuffer,[\s\S]*timeline\.height\s*\*\s*1\.5\)/.test(mainSource))
		verify(/property bool richLoaderSuspended:\s*false[\s\S]*ListView\.onPooled:[\s\S]*richLoaderSuspended\s*=\s*true[\s\S]*ListView\.onReused:[\s\S]*richLoaderSuspended\s*=\s*timeline\.richScrollInProgress[\s\S]*timeline\.scopePresentationPending/.test(mainSource))
		verify(/property bool messageRowMaterialized:\s*false[\s\S]*ListView\.onPooled:[\s\S]*messageRowMaterialized\s*=\s*false/.test(mainSource))
		verify(/id:\s*messageRowLoader[\s\S]*active:\s*!messageDelegate\.logEntry[\s\S]*messageDelegate\.messageRowMaterialized[\s\S]*asynchronous:\s*true[\s\S]*onLoaded:[\s\S]*messageDelegate\.messageRowMaterialized\s*=\s*true/.test(mainSource))
		verify(/readonly property bool messageRowReady:[\s\S]*messageRowLoader\.status\s*===\s*Loader\.Ready[\s\S]*Loader\.Error/.test(mainSource))
		verify(/id:\s*messageRowLoader[\s\S]*onLoaded:[\s\S]*messageDelegate\.completeRichLoaderResumeWhenReady\(\)[\s\S]*onStatusChanged:[\s\S]*status\s*===\s*Loader\.Error/.test(mainSource))
		verify(/lightweightMessageRowHeight:[\s\S]*hasPreviewContent\s*\?\s*previewPlaceholderHeight\s*\+\s*72[\s\S]*hasAttachmentContent\s*\?\s*attachmentPlaceholderHeight\s*\+\s*72/.test(mainSource))
		verify(/function\s+resumeRichContentAfterScroll\(allowDuringFinalization\)[\s\S]*pendingAllowed[\s\S]*scopePresentationFinalizing[\s\S]*scopePresentationPending\s*&&\s*!pendingAllowed[\s\S]*richLoaderSuspended\s*=\s*false/.test(mainSource))
		verify(/function\s+requestPreviewHydrationIfNeeded\(allowDuringFinalization\)[\s\S]*!backendHydrationRequested[\s\S]*inVisibleViewport[\s\S]*!timeline\.scopePresentationPending\s*\|\|\s*pendingAllowed[\s\S]*!timeline\.richScrollInProgress[\s\S]*queuePreviewHydration[\s\S]*backendHydrationRequested\s*=\s*true/.test(mainSource))
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
		verify(/id:\s*prependAnchorSettleTimer[\s\S]*interval:\s*180[\s\S]*correctionPending[\s\S]*prependAnchorCorrectionFrames\s*>\s*0[\s\S]*restoreDeferredPrependAnchor\(\)[\s\S]*restorePrependAnchor\(\)[\s\S]*releasePrependAnchor\(\)/.test(mainSource))
		verify(!/id:\s*prependAnchorSettleTimer[\s\S]{0,900}pendingScopeHydrationCount\(\)/.test(mainSource))
		verify(/function\s+onRowsAboutToChange\(first,\s*last\)[\s\S]*!chatModel\.presentationBusy[\s\S]*!timeline\.topAnchorActive[\s\S]*capturePrependAnchor\(\)/.test(mainSource))
		verify(/id:\s*messagePreviewHydrationPlaceholderLoader[\s\S]*active:\s*messageDelegate\.hasPreviewContent[\s\S]*messageDelegate\.previewNeedsHydration[\s\S]*objectName:\s*"previewHydrationPlaceholder"/.test(mainSource))
		verify(/id:\s*messagePreviewLoader[\s\S]*active:\s*messageDelegate\.hasPreviewContent[\s\S]*!messageDelegate\.previewNeedsHydration[\s\S]*!messageDelegate\.richLoaderSuspended[\s\S]*timeline\.prependRichContentActive\([\s\S]*sourceComponent:\s*Component\s*\{[\s\S]*RichPreviewCard\s*\{[\s\S]*renderActive:\s*messageDelegate\.richResourcesActive/.test(mainSource))
		verify(/id:\s*messageAttachmentLoader[\s\S]*active:\s*messageDelegate\.hasAttachmentContent[\s\S]*!messageDelegate\.richLoaderSuspended[\s\S]*timeline\.prependRichContentActive\(/.test(mainSource))
		verify(/previewPlaceholderHeight[\s\S]*attachmentPlaceholderHeight/.test(mainSource))
		verify(/id:\s*messagePreviewLoader[\s\S]*Layout\.preferredHeight:[\s\S]*previewPlaceholderHeight/.test(mainSource))
		verify(/id:\s*messageAttachmentLoader[\s\S]*Layout\.preferredHeight:[\s\S]*attachmentPlaceholderHeight/.test(mainSource))
		verify(/id:\s*messagePreviewLoader[\s\S]*visible:\s*messageDelegate\.hasPreviewContent[\s\S]*!messageDelegate\.previewNeedsHydration[\s\S]*Layout\.preferredHeight:[\s\S]*previewPlaceholderHeight/.test(mainSource))
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
		verify(/function\s+timelineDelegateDiagnostics\(\)[\s\S]*"materialized"[\s\S]*"previewItems"[\s\S]*"attachmentItems"[\s\S]*"pooledEvents"[\s\S]*"reusedEvents"/.test(mainSource))
		verify(/function\s+performanceChatFixtureState\(\)[\s\S]*"delegateDiagnostics":\s*timelineDelegateDiagnostics\(\)/.test(mainSource))
		verify(/function\s+performanceChatScrollState\(\)[\s\S]*"delegateDiagnosticsDelta"[\s\S]*"previewsLoaded"[\s\S]*"attachmentsLoaded"/.test(mainSource))
	}
}
