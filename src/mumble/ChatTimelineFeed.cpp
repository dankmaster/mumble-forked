// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ChatTimelineFeed.h"

#include "ChatPerfTrace.h"
#include "ChatRowModel.h"
#include "PersistentChatController.h"

#include <QtCore/QElapsedTimer>
#include <QtCore/QHash>
#include <QtCore/QSet>

#include <algorithm>
#include <tuple>

namespace {
	// Same order as PersistentChatStore.
	bool messageLessThan(const MumbleProto::ChatMessage &lhs, const MumbleProto::ChatMessage &rhs) {
		const quint64 lhsCreatedAt = lhs.has_created_at() ? lhs.created_at() : 0;
		const quint64 rhsCreatedAt = rhs.has_created_at() ? rhs.created_at() : 0;
		return std::make_tuple(lhsCreatedAt, lhs.thread_id(), lhs.message_id())
			   < std::make_tuple(rhsCreatedAt, rhs.thread_id(), rhs.message_id());
	}
} // namespace

namespace {
	quint64 localIdentity(const ChatLocalEntry &entry) {
		return entry.persistent ? PersistentChatMessageChange::identity(*entry.persistent)
								: static_cast< quint64 >(static_cast< quint32 >(entry.sequence));
	}

	quint64 localFingerprint(const ChatLocalEntry &entry) {
		size_t seed = qHashMulti(0, entry.sequence, entry.createdAtMs, entry.authorName, entry.authorSession,
								 entry.own, entry.logLine, entry.text, entry.html, entry.deliveryState,
								 entry.deliveryLabel, entry.deliveryCanRetry);
		if (entry.persistent) {
			const std::string bytes = entry.persistent->SerializeAsString();
			seed = qHashMulti(seed, QByteArray::fromRawData(bytes.data(), static_cast< qsizetype >(bytes.size())));
		}
		return static_cast< quint64 >(seed);
	}
} // namespace

// Touched only by jobs on the single worker thread.
struct ChatTimelineFeed::LocalState {
	QVector< quint64 > order;
	QHash< quint64, quint64 > fingerprints;
};

struct ChatTimelineFeed::Result {
	QVector< ChatTimelineRow > rows;
	QVector< quint64 > removed;
	double workerMs = 0;
};

ChatTimelineFeed::ChatTimelineFeed(PersistentChatController *controller, ChatRowModel *model, QObject *parent)
	: QObject(parent), m_controller(controller), m_model(model), m_localState(std::make_shared< LocalState >()),
	  m_liveGeneration(std::make_shared< std::atomic< quint64 > >(0)) {
	// One thread keeps jobs, and therefore their results, in submission order.
	m_worker.setMaxThreadCount(1);
	m_worker.setExpiryTimeout(30000);
	if (m_controller) {
		connect(m_controller, &PersistentChatController::activeMessagesChanged, this,
				&ChatTimelineFeed::handleChange);
	}
}

ChatTimelineFeed::~ChatTimelineFeed() {
	// Queued jobs see the new generation and return at once; results posted to
	// this object are dropped with it.
	m_liveGeneration->fetch_add(1);
	m_worker.waitForDone();
}

void ChatTimelineFeed::enterScope(const QString &scopeToken, bool local) {
	// Jobs for the previous scope stop as soon as they start.
	m_generation = m_liveGeneration->fetch_add(1) + 1;
	m_localScope = local;
	m_localEntries.clear();
	m_localState = std::make_shared< LocalState >();
	m_lastMessages.clear();
	if (m_model) {
		m_model->resetScope(scopeToken);
	}
}

void ChatTimelineFeed::syncLocal(const QString &scopeToken, QVector< ChatLocalEntry > entries) {
	if (!m_model) {
		return;
	}
	if (!m_localScope || scopeToken != m_model->scopeToken()) {
		enterScope(scopeToken, true);
	}
	m_localEntries = std::move(entries);
	scheduleLocalSync();
}

void ChatTimelineFeed::upsertLocal(const QString &scopeToken, const QVector< ChatLocalEntry > &entries) {
	if (!m_model || !m_localScope || scopeToken != m_model->scopeToken() || entries.isEmpty()) {
		return;
	}
	QHash< quint64, int > indexByIdentity;
	indexByIdentity.reserve(m_localEntries.size());
	for (int i = 0; i < m_localEntries.size(); ++i) {
		indexByIdentity.insert(localIdentity(m_localEntries.at(i)), i);
	}
	for (const ChatLocalEntry &entry : entries) {
		const int index = indexByIdentity.value(localIdentity(entry), -1);
		if (index >= 0) {
			m_localEntries[index] = entry;
		} else {
			indexByIdentity.insert(localIdentity(entry), static_cast< int >(m_localEntries.size()));
			m_localEntries.push_back(entry);
		}
	}
	scheduleLocalSync();
}

void ChatTimelineFeed::scheduleLocalSync() {
	const ChatRowBuilder builder(currentOptions());
	submit([builder, entries = m_localEntries, state = m_localState]() {
		Result result;
		QVector< quint64 > order;
		QHash< quint64, quint64 > fingerprints;
		order.reserve(entries.size());
		fingerprints.reserve(entries.size());
		QSet< int > indices;
		for (int i = 0; i < entries.size(); ++i) {
			const quint64 identity    = localIdentity(entries.at(i));
			const quint64 fingerprint = localFingerprint(entries.at(i));
			order.push_back(identity);
			fingerprints.insert(identity, fingerprint);
			const auto previous = state->fingerprints.constFind(identity);
			if (previous == state->fingerprints.cend() || previous.value() != fingerprint) {
				indices.insert(i);
				// Its newer neighbour's grouping and date break are decided against it.
				if (i + 1 < entries.size()) {
					indices.insert(i + 1);
				}
			}
		}
		// An entry that disappeared leaves a gap: re-decorate what now follows it.
		QHash< quint64, int > indexByIdentity;
		for (int i = 0; i < order.size(); ++i) {
			indexByIdentity.insert(order.at(i), i);
		}
		for (int i = 0; i < state->order.size(); ++i) {
			const quint64 identity = state->order.at(i);
			if (fingerprints.contains(identity)) {
				continue;
			}
			result.removed.push_back(identity);
			for (int next = i + 1; next < state->order.size(); ++next) {
				const int index = indexByIdentity.value(state->order.at(next), -1);
				if (index >= 0) {
					indices.insert(index);
					break;
				}
			}
		}
		state->order        = std::move(order);
		state->fingerprints = std::move(fingerprints);

		QList< int > sorted(indices.cbegin(), indices.cend());
		std::sort(sorted.begin(), sorted.end());
		QHash< int, ChatTimelineRow > built;
		const auto rowAt = [&](int index) -> const ChatTimelineRow & {
			auto it = built.find(index);
			if (it == built.end()) {
				it = built.insert(index, builder.buildLocal(entries.at(index)));
			}
			return it.value();
		};
		for (const int index : sorted) {
			ChatTimelineRow row = rowAt(index);
			if (index > 0) {
				const ChatTimelineRow older = rowAt(index - 1);
				builder.decorate(row, &older);
			} else {
				builder.decorate(row, nullptr);
			}
			result.rows.push_back(std::move(row));
		}
		return result;
	});
}

void ChatTimelineFeed::setOptionsProvider(std::function< ChatRowBuilder::Options() > provider) {
	m_optionsProvider = std::move(provider);
}

ChatRowBuilder::Options ChatTimelineFeed::currentOptions() const {
	return m_optionsProvider ? m_optionsProvider() : ChatRowBuilder::Options();
}

void ChatTimelineFeed::handleChange(const PersistentChatMessageChange &change) {
	if (!m_model || !m_controller) {
		return;
	}
	const QVector< MumbleProto::ChatMessage > messages =
		change.key.valid ? m_controller->activeSnapshot().messages : QVector< MumbleProto::ChatMessage >();
	if (change.kind == PersistentChatMessageChange::Kind::Reset) {
		if (!change.key.valid && m_localScope) {
			// The controller let go of its room while a log or direct message is shown.
			return;
		}
		enterScope(change.key.valid ? change.key.cacheKey() : QString(), false);
		m_lastMessages = messages;
		if (change.key.valid) {
			scheduleFullBuild(messages);
		}
		return;
	}
	if (!change.key.valid || change.key.cacheKey() != m_model->scopeToken()) {
		return;
	}
	scheduleChangedMessages(m_lastMessages, messages, change.identities);
	m_lastMessages = messages;
}

template< typename Job > void ChatTimelineFeed::submit(Job job) {
	const quint64 generation = m_generation;
	++m_pendingJobs;
	QPointer< ChatTimelineFeed > self(this);
	m_worker.start([self, generation, live = m_liveGeneration, job = std::move(job)]() {
		Result result;
		if (live->load() == generation) {
			QElapsedTimer timer;
			timer.start();
			result          = job();
			result.workerMs = static_cast< double >(timer.nsecsElapsed()) / 1e6;
			mumble::chatperf::recordDuration("chat.v2.worker.build", timer.nsecsElapsed());
		}
		if (!self) {
			return;
		}
		QMetaObject::invokeMethod(
			self.data(),
			[self, generation, result = std::move(result)]() {
				if (self) {
					self->apply(result, generation);
				}
			},
			Qt::QueuedConnection);
	});
}

void ChatTimelineFeed::scheduleFullBuild(const QVector< MumbleProto::ChatMessage > &messages) {
	const int total = static_cast< int >(messages.size());
	if (total == 0) {
		return;
	}
	const ChatRowBuilder builder(currentOptions());
	// Newest page first so the bottom of the conversation is ready at once; older
	// history follows in chunks that append above it without moving the reader.
	int last = total - 1;
	int size = InitialPageSize;
	while (last >= 0) {
		const int first = std::max(0, last - size + 1);
		submit([builder, messages, first, last]() {
			Result result;
			result.rows = builder.buildRange(messages, first, last);
			return result;
		});
		last = first - 1;
		size = BackfillChunkSize;
	}
}

void ChatTimelineFeed::scheduleChangedMessages(const QVector< MumbleProto::ChatMessage > &previous,
											   const QVector< MumbleProto::ChatMessage > &messages,
											   const QVector< quint64 > &identities) {
	if (identities.isEmpty()) {
		return;
	}
	const ChatRowBuilder builder(currentOptions());
	submit([builder, previous, messages, identities]() {
		Result result;
		QHash< quint64, int > indexByIdentity;
		indexByIdentity.reserve(messages.size());
		for (int i = 0; i < messages.size(); ++i) {
			indexByIdentity.insert(PersistentChatMessageChange::identity(messages.at(i)), i);
		}
		QSet< int > indices;
		for (const quint64 identity : identities) {
			const int index = indexByIdentity.value(identity, -1);
			if (index >= 0) {
				indices.insert(index);
				// Its newer neighbour's grouping and date break are decided against it.
				if (index + 1 < messages.size()) {
					indices.insert(index + 1);
				}
				continue;
			}
			// Deleted: re-decorate the message that now follows the gap.
			const auto removed = std::find_if(previous.cbegin(), previous.cend(), [identity](const auto &message) {
				return PersistentChatMessageChange::identity(message) == identity;
			});
			if (removed == previous.cend()) {
				continue;
			}
			result.removed.push_back(identity);
			const auto next = std::upper_bound(messages.cbegin(), messages.cend(), *removed, messageLessThan);
			if (next != messages.cend()) {
				indices.insert(static_cast< int >(next - messages.cbegin()));
			}
		}
		QList< int > sorted(indices.cbegin(), indices.cend());
		std::sort(sorted.begin(), sorted.end());
		for (const int index : sorted) {
			result.rows += builder.buildRange(messages, index, index);
		}
		return result;
	});
}

void ChatTimelineFeed::apply(const Result &result, quint64 generation) {
	--m_pendingJobs;
	if (generation == m_generation && m_model) {
		QElapsedTimer timer;
		timer.start();
		if (!result.removed.isEmpty()) {
			m_model->removeMessages(result.removed);
		}
		if (!result.rows.isEmpty()) {
			m_model->upsertRows(result.rows);
		}
		const double applyMs = static_cast< double >(timer.nsecsElapsed()) / 1e6;
		mumble::chatperf::recordDuration("chat.v2.ui.apply", timer.nsecsElapsed());
		mumble::chatperf::recordValue("chat.v2.ui.apply.rows", result.rows.size());
		emit batchApplied(static_cast< int >(result.rows.size()), static_cast< int >(result.removed.size()),
						  result.workerMs, applyMs);
	}
	if (m_pendingJobs == 0) {
		emit idle();
	}
}
