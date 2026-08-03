pragma ComponentBehavior: Bound

import QtQuick

Item {
	id: root

	property bool pressed: false
	property bool dragActive: false
	property bool releasePending: false
	property bool moving: false
	property bool dragging: false
	property bool flicking: false
	property bool customScrolling: false
	property bool performanceScrolling: false
	property bool scopeBlocked: false
	property bool prependBlocked: false
	property bool membershipBlocked: false
	property real position: 0
	property var visibleStableIdProvider: null
	property var canAdmitStableId: null
	property var revalidateActiveStableId: null
	// A long thumb drag may visit several non-overlapping viewports before the
	// button is released. Bound work to the current stationary viewport rather
	// than spending one lifetime budget near the press origin and starving every
	// later stop. Admissions remain strictly serial.
	readonly property int maximumAdmissionsPerDwell: 8
	readonly property int maximumNoCandidateScansPerGeneration: 2
	readonly property int dwellIntervalMs: 48
	readonly property int serialIntervalMs: 16
	readonly property int activeWatchdogMs: 384

	readonly property bool hardInputEligible: pressed && dragActive && !releasePending
		&& !moving && !dragging && !flicking && !customScrolling
		&& !performanceScrolling && !scopeBlocked && !prependBlocked
	readonly property bool inputEligible: hardInputEligible && !membershipBlocked
	property int pressEpoch: 0
	property int dwellGeneration: 0
	property int admittedCount: 0
	property string activeStableId: ""
	property int activePressEpoch: 0
	property int activeDwellGeneration: 0
	property real stationaryPosition: 0
	property var admittedStableIds: ({})
	property var retainedAdmissions: ({})
	property var pendingStableIds: []
	property int pendingIndex: 0
	property int noCandidateScanCount: 0
	property bool activeMembershipValidated: true

	signal admissionRequested(string stableId, int pressEpoch, int dwellGeneration)
	signal admissionFinished(string stableId, int pressEpoch, int dwellGeneration,
		string outcome)
	signal admissionCancelled(string stableId, int pressEpoch, int dwellGeneration,
		string reason)
	signal retentionsCleared()
	signal membershipRevisionDrained()

	visible: false
	width: 0
	height: 0

	function owns(stableId, epoch, generation) {
		return inputEligible && activeMembershipValidated
			&& activeStableId.length > 0
			&& activeStableId === String(stableId || "")
			&& activePressEpoch === Number(epoch)
			&& activePressEpoch === pressEpoch
			&& activeDwellGeneration === Number(generation)
			&& activeDwellGeneration === dwellGeneration
	}

	function retains(stableId, epoch, generation) {
		const key = String(stableId || "")
		const retained = retainedAdmissions[key]
		return hardInputEligible && !!retained
			&& Number(retained.pressEpoch) === Number(epoch)
			&& Number(retained.pressEpoch) === pressEpoch
			&& Number(retained.dwellGeneration) === Number(generation)
			&& Number(retained.dwellGeneration) === dwellGeneration
	}

	function clearRetentions(reopenDedupe) {
		const retainedKeys = Object.keys(retainedAdmissions)
		if (retainedKeys.length === 0)
			return
		if (reopenDedupe === true) {
			const nextAdmitted = Object.assign({}, admittedStableIds)
			for (let index = 0; index < retainedKeys.length; ++index)
				delete nextAdmitted[retainedKeys[index]]
			admittedStableIds = nextAdmitted
		}
		retainedAdmissions = ({})
		retentionsCleared()
	}

	function resetPendingSnapshot() {
		pendingStableIds = []
		pendingIndex = 0
	}

	function resetDwellBudget() {
		admittedCount = 0
		admittedStableIds = ({})
	}

	function invalidateActiveAdmission(reason) {
		if (activeStableId.length === 0)
			return false
		const cancelledId = activeStableId
		const cancelledEpoch = activePressEpoch
		const cancelledGeneration = activeDwellGeneration
		const cancellationReason = String(reason || "invalidated")
		activeWatchdog.stop()
		activeStableId = ""
		activePressEpoch = 0
		activeDwellGeneration = 0
		activeMembershipValidated = true
		if (cancellationReason === "movement"
				|| cancellationReason === "membership-revalidation") {
			const nextAdmitted = Object.assign({}, admittedStableIds)
			delete nextAdmitted[cancelledId]
			admittedStableIds = nextAdmitted
		}
		const retained = retainedAdmissions[cancelledId]
		if (retained
				&& Number(retained.pressEpoch) === cancelledEpoch
				&& Number(retained.dwellGeneration) === cancelledGeneration) {
			const nextRetained = Object.assign({}, retainedAdmissions)
			delete nextRetained[cancelledId]
			retainedAdmissions = nextRetained
		}
		admissionCancelled(cancelledId, cancelledEpoch, cancelledGeneration,
			cancellationReason)
		return true
	}

	function snapshotVisibleStableIds() {
		if (typeof visibleStableIdProvider !== "function")
			return []
		const supplied = visibleStableIdProvider() || []
		const unique = []
		const seen = ({})
		for (let index = 0; index < supplied.length; ++index) {
			const key = String(supplied[index] || "")
			if (key.length === 0 || seen[key] === true)
				continue
			seen[key] = true
			unique.push(key)
		}
		return unique
	}

	function beginPress() {
		invalidateActiveAdmission("new-press")
		++pressEpoch
		++dwellGeneration
		resetDwellBudget()
		clearRetentions()
		noCandidateScanCount = 0
		stationaryPosition = position
		resetPendingSnapshot()
		dwellTimer.stop()
		serialTimer.stop()
		activeWatchdog.stop()
		if (inputEligible)
			dwellTimer.restart()
	}

	function endPress() {
		++dwellGeneration
		dwellTimer.stop()
		serialTimer.stop()
		resetPendingSnapshot()
		invalidateActiveAdmission("press-ended")
		clearRetentions()
	}

	function invalidateDwellForMovement() {
		stationaryPosition = position
		++dwellGeneration
		dwellTimer.stop()
		serialTimer.stop()
		resetPendingSnapshot()
		noCandidateScanCount = 0
		invalidateActiveAdmission("movement")
		clearRetentions()
		resetDwellBudget()
		if (inputEligible
				&& admittedCount < maximumAdmissionsPerDwell)
			dwellTimer.restart()
	}

	function handleHardEligibilityChange() {
		if (!hardInputEligible) {
			++dwellGeneration
			dwellTimer.stop()
			serialTimer.stop()
			resetPendingSnapshot()
			noCandidateScanCount = 0
			invalidateActiveAdmission("eligibility")
			clearRetentions()
			return
		}
		stationaryPosition = position
		resetDwellBudget()
		if (!membershipBlocked && activeStableId.length === 0
				&& admittedCount < maximumAdmissionsPerDwell)
			dwellTimer.restart()
	}

	function handleMembershipChange() {
		dwellTimer.stop()
		serialTimer.stop()
		resetPendingSnapshot()
		noCandidateScanCount = 0
		if (membershipBlocked) {
			if (activeStableId.length > 0) {
				activeMembershipValidated = false
				activeWatchdog.stop()
			}
			return
		}
		if (!hardInputEligible)
			return
		revalidateRetentions(snapshotVisibleStableIds())
		membershipRevisionDrained()
		if (activeStableId.length > 0) {
			membershipRevalidationTimer.restart()
			return
		}
		if (admittedCount < maximumAdmissionsPerDwell)
			dwellTimer.restart()
	}

	function revalidateRetentions(visibleIds) {
		const supplied = visibleIds || snapshotVisibleStableIds()
		const nextRetained = ({})
		const retainedKeys = Object.keys(retainedAdmissions)
		for (let index = 0; index < retainedKeys.length; ++index) {
			const retainedId = retainedKeys[index]
			if (supplied.indexOf(retainedId) >= 0
					&& (typeof canAdmitStableId !== "function"
						|| canAdmitStableId(retainedId) === true))
				nextRetained[retainedId] = retainedAdmissions[retainedId]
		}
		retainedAdmissions = nextRetained
	}

	function pauseActiveForMembershipRevision() {
		if (activeStableId.length === 0)
			return false
		activeMembershipValidated = false
		activeWatchdog.stop()
		noCandidateScanCount = 0
		if (!membershipBlocked && hardInputEligible)
			membershipRevalidationTimer.restart()
		return true
	}

	function revalidateActiveAdmission() {
		if (!inputEligible || activeStableId.length === 0)
			return false
		const key = activeStableId
		const visibleIds = snapshotVisibleStableIds()
		revalidateRetentions(visibleIds)
		if (Math.abs(position - stationaryPosition) > 0.5
				|| visibleIds.indexOf(key) < 0
				|| (typeof revalidateActiveStableId === "function"
					&& revalidateActiveStableId(key, activePressEpoch,
						activeDwellGeneration) !== true)) {
			invalidateActiveAdmission("membership-revalidation")
			if (inputEligible && admittedCount < maximumAdmissionsPerDwell)
				dwellTimer.restart()
			return false
		}
		activeMembershipValidated = true
		activeWatchdog.restart()
		return true
	}

	function reopenAdmission(stableId, epoch, generation) {
		const key = String(stableId || "")
		if (!inputEligible || key.length === 0
				|| Number(epoch) !== pressEpoch
				|| Number(generation) !== dwellGeneration
				|| admittedStableIds[key] !== true
				|| activeStableId === key)
			return false
		const nextAdmitted = Object.assign({}, admittedStableIds)
		delete nextAdmitted[key]
		admittedStableIds = nextAdmitted
		// A matching late hydration replaces its consumed attempt; the visual
		// retry must not spend a second slot or make the cap's last row impossible.
		admittedCount = Math.max(0, admittedCount - 1)
		noCandidateScanCount = 0
		resetPendingSnapshot()
		if (activeStableId.length === 0) {
			dwellTimer.stop()
			serialTimer.restart()
		}
		return true
	}

	function noteAdmissionProgress(stableId, epoch, generation) {
		if (!owns(stableId, epoch, generation))
			return false
		activeWatchdog.restart()
		return true
	}

	function admitNextVisible() {
		if (!inputEligible || activeStableId.length > 0
				|| admittedCount >= maximumAdmissionsPerDwell)
			return false
		// Every admission owns a fresh actual-visible snapshot. The serial turn
		// must never inherit candidates captured before a thumb move or blocker.
		pendingStableIds = snapshotVisibleStableIds()
		pendingIndex = 0
		while (pendingIndex < pendingStableIds.length) {
			const key = String(pendingStableIds[pendingIndex++] || "")
			if (key.length === 0 || admittedStableIds[key] === true)
				continue
			if (typeof canAdmitStableId === "function"
					&& canAdmitStableId(key) !== true)
				continue
			const nextAdmitted = Object.assign({}, admittedStableIds)
			nextAdmitted[key] = true
			admittedStableIds = nextAdmitted
			const nextRetained = Object.assign({}, retainedAdmissions)
			nextRetained[key] = {
				"pressEpoch": pressEpoch,
				"dwellGeneration": dwellGeneration
			}
			retainedAdmissions = nextRetained
			++admittedCount
			activeStableId = key
			activePressEpoch = pressEpoch
			activeDwellGeneration = dwellGeneration
			activeMembershipValidated = true
			activeWatchdog.restart()
			admissionRequested(key, activePressEpoch, activeDwellGeneration)
			return true
		}
		resetPendingSnapshot()
		++noCandidateScanCount
		if (inputEligible && admittedCount < maximumAdmissionsPerDwell
				&& noCandidateScanCount < maximumNoCandidateScansPerGeneration)
			dwellTimer.restart()
		return false
	}

	function finishAdmission(stableId, epoch, generation, outcome) {
		const key = String(stableId || "")
		const completion = String(outcome || "")
		if (key.length === 0 || key !== activeStableId
				|| Number(epoch) !== activePressEpoch
				|| Number(generation) !== activeDwellGeneration
				|| (completion !== "painted" && completion !== "terminal"
					&& completion !== "watchdog"))
			return false
		activeWatchdog.stop()
		const completedEpoch = activePressEpoch
		const completedGeneration = activeDwellGeneration
		activeStableId = ""
		activePressEpoch = 0
		activeDwellGeneration = 0
		activeMembershipValidated = true
		if (completion !== "painted") {
			const nextRetained = Object.assign({}, retainedAdmissions)
			delete nextRetained[key]
			retainedAdmissions = nextRetained
		}
		admissionFinished(key, completedEpoch, completedGeneration, completion)
		if (!inputEligible || admittedCount >= maximumAdmissionsPerDwell)
			return true
		if (completedGeneration === dwellGeneration) {
			resetPendingSnapshot()
			serialTimer.restart()
		} else {
			resetPendingSnapshot()
			stationaryPosition = position
			dwellTimer.restart()
		}
		return true
	}

	onPressedChanged: {
		if (pressed)
			beginPress()
		else
			endPress()
	}
	onHardInputEligibleChanged: handleHardEligibilityChange()
	onMembershipBlockedChanged: handleMembershipChange()
	onPositionChanged: {
		if (pressed && Math.abs(position - stationaryPosition) > 0.5)
			invalidateDwellForMovement()
	}

	Timer {
		id: dwellTimer
		interval: root.dwellIntervalMs
		repeat: false
		onTriggered: {
			if (!root.inputEligible
					|| Math.abs(root.position - root.stationaryPosition) > 0.5)
				return
			root.resetPendingSnapshot()
			root.admitNextVisible()
		}
	}

	Timer {
		id: serialTimer
		interval: root.serialIntervalMs
		repeat: false
		onTriggered: root.admitNextVisible()
	}

	Timer {
		id: membershipRevalidationTimer
		interval: 0
		repeat: false
		onTriggered: root.revalidateActiveAdmission()
	}

	Timer {
		id: activeWatchdog
		interval: root.activeWatchdogMs
		repeat: false
		onTriggered: root.finishAdmission(root.activeStableId,
			root.activePressEpoch, root.activeDwellGeneration, "watchdog")
	}
}
