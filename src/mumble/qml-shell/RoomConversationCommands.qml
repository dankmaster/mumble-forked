import QtQml

// What ConversationView does with a room: actions, media hydration, paging and
// read state through uiCommands and activeScope. DirectMessageWindow gives the
// view its own object with the same members for a direct message conversation.
QtObject {
	readonly property bool localScope: String(chatTimelineV2.scopeToken || "").startsWith("local:")
	readonly property bool canLoadOlder: !localScope && activeScope.canLoadOlder
	readonly property bool loadingOlder: !localScope && activeScope.loadingState === "older"
	readonly property real unreadCount: Number(activeScope.unreadCount || 0)
	readonly property bool sending: composer.sending
	readonly property bool hydrationAvailable: !localScope

	function reply(stableId) { uiCommands.replyToMessage(stableId) }
	function toggleReaction(stableId, emoji) { uiCommands.toggleMessageReaction(stableId, emoji) }
	function retry(stableId) { uiCommands.retryMessage(stableId) }
	function remove(stableId) { uiCommands.deleteMessage(stableId) }
	function retryAttachmentPreview(stableId, assetId) {
		uiCommands.retryChatAttachmentPreview(activeScope.scopeToken, stableId, assetId)
	}
	// Room hydration takes protocol message ids only.
	function validHydrationId(stableId) { return /^[1-9][0-9]*$/.test(String(stableId || "")) }
	function hydrate(stableIds, highPriority) {
		const scopeToken = String(activeScope.scopeToken || "")
		if (scopeToken.length > 0)
			uiCommands.requestPreviewHydration(scopeToken, stableIds, highPriority)
	}
	function loadOlder() { uiCommands.requestOlderMessages() }
	function markRead() { uiCommands.markActiveScopeRead() }
}
