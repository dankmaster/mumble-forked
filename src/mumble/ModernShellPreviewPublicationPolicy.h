// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_MODERNSHELLPREVIEWPUBLICATIONPOLICY_H_
#define MUMBLE_MUMBLE_MODERNSHELLPREVIEWPUBLICATIONPOLICY_H_

#include <QtCore/QString>
#include <QtCore/QUrl>
#include <QtCore/QtGlobal>

namespace mumble::modernshell {

enum class PreviewPublicationDecision : unsigned char { Hold, PublishSelected, PublishFinal };

struct PreviewSelectedMediaFingerprint {
	int logicalSlot = -1;
	QString sourceIdentity;
	quint64 requestToken = 0;
	int renderLogicalSlot = -1;
	QString renderSourceIdentity;
	quint64 renderRequestToken = 0;

	bool isValid() const { return logicalSlot >= 0 && !sourceIdentity.isEmpty() && requestToken != 0; }

	bool operator==(const PreviewSelectedMediaFingerprint &other) const {
		return logicalSlot == other.logicalSlot && sourceIdentity == other.sourceIdentity
			&& requestToken == other.requestToken && renderLogicalSlot == other.renderLogicalSlot
			&& renderSourceIdentity == other.renderSourceIdentity
			&& renderRequestToken == other.renderRequestToken;
	}

	bool operator!=(const PreviewSelectedMediaFingerprint &other) const { return !(*this == other); }
};

struct PreviewPublicationInput {
	bool corePending = false;
	int fanoutPendingUnits = 0;
	PreviewSelectedMediaFingerprint selectedFingerprint;
	QString selectedManagedRenderSource;
	bool selectedManagedAnimated = false;
	bool selectedManagedRenderSourceVerified = false;
};

struct PreviewArtworkCandidate {
	int logicalSlot = -1;
	QString sourceIdentity;
	quint64 requestToken = 0;
	QString managedRenderSource;
	bool terminalFailure = false;
};

struct PreviewArtworkSelection {
	PreviewSelectedMediaFingerprint fingerprint;
	QString managedRenderSource;
};

struct PreviewPublicationTicket {
	PreviewPublicationDecision decision = PreviewPublicationDecision::Hold;
	quint64 generation = 0;
	PreviewSelectedMediaFingerprint selectedFingerprint;
	QString selectedManagedRenderSource;
	bool selectedManagedAnimated = false;

	bool isValid() const {
		return decision != PreviewPublicationDecision::Hold && generation != 0;
	}

	bool isReadyFor(const PreviewPublicationInput &input) const {
		if (!isValid() || input.selectedFingerprint != selectedFingerprint || input.corePending) {
			return false;
		}
		if (decision == PreviewPublicationDecision::PublishFinal) {
			return input.fanoutPendingUnits <= 0;
		}
		return input.fanoutPendingUnits > 0
			&& input.selectedManagedRenderSource == selectedManagedRenderSource
			&& input.selectedManagedAnimated == selectedManagedAnimated
			&& input.selectedManagedRenderSourceVerified;
	}

	void clear() { *this = {}; }
};

class ModernShellPreviewPublicationPolicy final {
public:
	PreviewPublicationDecision decide(const PreviewPublicationInput &input) {
		if (input.selectedFingerprint.isValid()
			&& (!m_hasSelectedFingerprint || input.selectedFingerprint != m_selectedFingerprint)) {
			beginGeneration(input.selectedFingerprint);
		} else if (!input.selectedFingerprint.isValid() && m_hasSelectedFingerprint) {
			beginNoSelectionGeneration();
		}

		if (input.corePending) {
			return PreviewPublicationDecision::Hold;
		}
		if (m_finalScheduled || m_finalPublished) {
			return PreviewPublicationDecision::Hold;
		}
		if (input.fanoutPendingUnits <= 0) {
			m_selectedStageScheduled = false;
			m_finalScheduled = true;
			return PreviewPublicationDecision::PublishFinal;
		}
		if (!input.selectedFingerprint.isValid() || !m_hasSelectedFingerprint
			|| input.selectedFingerprint != m_selectedFingerprint || m_selectedStagePublished
			|| m_selectedStageScheduled
			|| !input.selectedManagedRenderSourceVerified
			|| !isManagedRenderSource(input.selectedManagedRenderSource, input.selectedManagedAnimated)) {
			return PreviewPublicationDecision::Hold;
		}

		m_selectedStageScheduled = true;
		return PreviewPublicationDecision::PublishSelected;
	}

	void reset() {
		advanceGeneration();
		m_selectedFingerprint = {};
		m_hasSelectedFingerprint = false;
		m_selectedStageScheduled = false;
		m_selectedStagePublished = false;
		m_finalScheduled = false;
		m_finalPublished = false;
	}

	quint64 generation() const { return m_generation; }
	bool hasSelectedFingerprint() const { return m_hasSelectedFingerprint; }
	PreviewSelectedMediaFingerprint selectedFingerprint() const {
		return m_hasSelectedFingerprint ? m_selectedFingerprint : PreviewSelectedMediaFingerprint {};
	}

	bool canApplyScheduled(const PreviewPublicationDecision decision, const quint64 generation,
						   const PreviewSelectedMediaFingerprint &fingerprint) const {
		if (generation == 0 || generation != m_generation || !matchesCurrentFingerprint(fingerprint)) {
			return false;
		}
		switch (decision) {
			case PreviewPublicationDecision::PublishSelected:
				return m_selectedStageScheduled && !m_selectedStagePublished && !m_finalScheduled
					&& !m_finalPublished;
			case PreviewPublicationDecision::PublishFinal:
				return m_finalScheduled && !m_finalPublished;
			case PreviewPublicationDecision::Hold:
				return false;
		}
		return false;
	}

	bool markScheduledApplied(const PreviewPublicationDecision decision, const quint64 generation,
							  const PreviewSelectedMediaFingerprint &fingerprint) {
		if (!canApplyScheduled(decision, generation, fingerprint)) return false;
		if (decision == PreviewPublicationDecision::PublishSelected) {
			m_selectedStageScheduled = false;
			m_selectedStagePublished = true;
		} else {
			m_selectedStageScheduled = false;
			m_finalScheduled = false;
			m_finalPublished = true;
		}
		return true;
	}

	bool cancelScheduled(const PreviewPublicationDecision decision, const quint64 generation,
						 const PreviewSelectedMediaFingerprint &fingerprint) {
		if (!canApplyScheduled(decision, generation, fingerprint)) return false;
		if (decision == PreviewPublicationDecision::PublishSelected) {
			m_selectedStageScheduled = false;
		} else {
			m_finalScheduled = false;
		}
		return true;
	}

	static bool isManagedRenderSource(const QString &source, const bool managedAnimated = false) {
		const QUrl url(source);
		if (!url.isValid()) return false;
		if (url.scheme() == QLatin1String("image") && url.host() == QLatin1String("mumble")
			&& !url.path().isEmpty()) {
			return true;
		}
		return managedAnimated && url.isLocalFile() && url.path().endsWith(QLatin1String(".gif"), Qt::CaseInsensitive);
	}

	static bool completionMatches(const QString &currentSource, const quint64 currentToken,
								  const QString &completionSource, const quint64 completionToken) {
		return currentToken != 0 && completionToken != 0 && currentToken == completionToken
			&& currentSource == completionSource;
	}

	static int remotePendingUnits(const bool requested, const bool pipelineStarted, const bool finished) {
		return (requested || pipelineStarted) && !finished ? 1 : 0;
	}

	static PreviewArtworkSelection selectNonImageArtwork(const PreviewArtworkCandidate &poster,
												 const PreviewArtworkCandidate &thumbnail,
												 const PreviewArtworkCandidate &topThumbnail) {
		const auto canOwn = [](const PreviewArtworkCandidate &candidate) {
			return candidate.logicalSlot >= 0 && !candidate.sourceIdentity.isEmpty();
		};
		const auto canRender = [](const PreviewArtworkCandidate &candidate) {
			return candidate.logicalSlot >= 0 && !candidate.sourceIdentity.isEmpty()
				&& candidate.requestToken != 0 && !candidate.terminalFailure
				&& !candidate.managedRenderSource.isEmpty();
		};
		const PreviewArtworkCandidate *owner = canOwn(poster) ? &poster
			: canOwn(thumbnail) ? &thumbnail : canOwn(topThumbnail) ? &topThumbnail : nullptr;
		const PreviewArtworkCandidate *render = canRender(poster) ? &poster
			: canRender(thumbnail) ? &thumbnail : canRender(topThumbnail) ? &topThumbnail : nullptr;
		if (!owner) return {};
		return {
			PreviewSelectedMediaFingerprint {
				owner->logicalSlot, owner->sourceIdentity, owner->requestToken,
				render ? render->logicalSlot : -1,
				render ? render->sourceIdentity : QString(),
				render ? render->requestToken : 0 },
			render ? render->managedRenderSource : QString()
		};
	}

	template< typename Eligible > static int firstLogicalSlot(const int count, Eligible eligible) {
		for (int index = 0; index < count; ++index) {
			if (eligible(index)) return index;
		}
		return -1;
	}

private:
	void advanceGeneration() {
		++m_generation;
		if (m_generation == 0) ++m_generation;
	}

	void beginGeneration(const PreviewSelectedMediaFingerprint &fingerprint) {
		const bool selectedStagePublished = m_selectedStagePublished;
		advanceGeneration();
		m_selectedFingerprint = fingerprint;
		m_hasSelectedFingerprint = true;
		m_selectedStageScheduled = false;
		// A provider/source generation change invalidates the final DTO, but a
		// preview hydration epoch still emits at most one progressive selected
		// stage. Only explicit reset() starts a new hydration epoch.
		m_selectedStagePublished = selectedStagePublished;
		m_finalScheduled = false;
		m_finalPublished = false;
	}

	void beginNoSelectionGeneration() {
		const bool selectedStagePublished = m_selectedStagePublished;
		advanceGeneration();
		m_selectedFingerprint = {};
		m_hasSelectedFingerprint = false;
		m_selectedStageScheduled = false;
		m_selectedStagePublished = selectedStagePublished;
		m_finalScheduled = false;
		m_finalPublished = false;
	}

	bool matchesCurrentFingerprint(const PreviewSelectedMediaFingerprint &fingerprint) const {
		return m_hasSelectedFingerprint ? fingerprint.isValid() && fingerprint == m_selectedFingerprint
									: !fingerprint.isValid();
	}

	PreviewSelectedMediaFingerprint m_selectedFingerprint;
	quint64 m_generation = 1;
	bool m_hasSelectedFingerprint = false;
	bool m_selectedStageScheduled = false;
	bool m_selectedStagePublished = false;
	bool m_finalScheduled = false;
	bool m_finalPublished = false;
};

} // namespace mumble::modernshell

#endif
