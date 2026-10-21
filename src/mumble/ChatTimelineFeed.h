// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_CHATTIMELINEFEED_H_
#define MUMBLE_MUMBLE_CHATTIMELINEFEED_H_

#include "ChatRowBuilder.h"
#include "PersistentChatState.h"

#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QThreadPool>

#include <atomic>
#include <functional>
#include <memory>

class ChatRowModel;
class PersistentChatController;

// Keeps a ChatRowModel in step with the active persistent chat scope. Change
// sets from the controller become jobs on one ordered worker thread, which
// builds and decorates rows; the UI thread only applies the prepared result.
class ChatTimelineFeed final : public QObject {
	Q_OBJECT

public:
	static constexpr int InitialPageSize   = 50;
	static constexpr int BackfillChunkSize = 200;

	ChatTimelineFeed(PersistentChatController *controller, ChatRowModel *model, QObject *parent = nullptr);
	~ChatTimelineFeed() override;

	// Read whenever rows are built, so own-message detection follows the
	// current connection.
	void setOptionsProvider(std::function< ChatRowBuilder::Options() > provider);
	int pendingJobs() const { return m_pendingJobs; }

	// Shows a scope that is not persistent room history (the server or session
	// log, a direct-message conversation) with these entries, oldest first. A new
	// token replaces whatever was shown; the same token updates it, and only new
	// or changed entries are rebuilt. Room traffic from the controller does not
	// touch a local scope until the controller activates a room.
	void syncLocal(const QString &scopeToken, QVector< ChatLocalEntry > entries);
	// Adds entries to, or replaces entries in, the shown local scope.
	void upsertLocal(const QString &scopeToken, const QVector< ChatLocalEntry > &entries);

signals:
	void batchApplied(int upsertedRows, int removedRows, double workerMs, double applyMs);
	void idle();

private:
	struct Result;

	void handleChange(const PersistentChatMessageChange &change);
	void scheduleFullBuild(const QVector< MumbleProto::ChatMessage > &messages);
	void scheduleChangedMessages(const QVector< MumbleProto::ChatMessage > &previous,
								 const QVector< MumbleProto::ChatMessage > &messages,
								 const QVector< quint64 > &identities);
	template< typename Job > void submit(Job job);
	void apply(const Result &result, quint64 generation);
	ChatRowBuilder::Options currentOptions() const;
	void scheduleLocalSync();
	void enterScope(const QString &scopeToken, bool local);

	QPointer< PersistentChatController > m_controller;
	QPointer< ChatRowModel > m_model;
	std::function< ChatRowBuilder::Options() > m_optionsProvider;
	// The active scope's messages as of the last change, to locate deleted ones.
	QVector< MumbleProto::ChatMessage > m_lastMessages;
	// The shown local scope's entries; m_localState lives on the worker.
	struct LocalState;
	bool m_localScope = false;
	QVector< ChatLocalEntry > m_localEntries;
	std::shared_ptr< LocalState > m_localState;
	QThreadPool m_worker;
	quint64 m_generation = 0;
	// Read by queued jobs so work for a retired scope stops before it starts.
	std::shared_ptr< std::atomic< quint64 > > m_liveGeneration;
	int m_pendingJobs = 0;
};

#endif // MUMBLE_MUMBLE_CHATTIMELINEFEED_H_
