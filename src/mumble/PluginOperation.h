// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef MUMBLE_MUMBLE_PLUGINOPERATION_H_
#define MUMBLE_MUMBLE_PLUGINOPERATION_H_

#include <QtCore/QHash>
#include <QtCore/QList>
#include <QtCore/QMutex>
#include <QtCore/QSet>
#include <QtCore/QString>
#include <QtCore/QStringList>

#include <atomic>
#include <cstdint>
#include <memory>

/// A one-way, thread-safe cancellation boundary for operations that become
/// non-cancellable immediately before mutating runtime or filesystem state.
/// Exactly one of requestCancellation() and seal() can win from Active.
class PluginCancellationGate final {
public:
	enum class State : std::uint8_t { Active, CancellationRequested, Sealed };

	PluginCancellationGate();
	std::shared_ptr< std::atomic< bool > > cancellationToken() const;
	bool requestCancellation();
	bool seal();
	bool isCancellationRequested() const;
	bool isSealed() const;

private:
	std::atomic< State > m_state { State::Active };
	std::shared_ptr< std::atomic< bool > > m_cancelToken;
};

/// Thread-safe bookkeeping shared by plugin discovery, installation and update controllers.
///
/// This class deliberately contains no plugin ABI calls. It gives every asynchronous operation a stable ID,
/// a cancellation token and exactly one terminal result per item. Keeping this state independent of QML makes
/// partial success and cancellation deterministic even when worker completions arrive out of order.
class PluginOperation final {
public:
	struct ItemResult {
		QString itemID;
		qulonglong pluginID = 0;
		bool success         = false;
		bool cancelled       = false;
		QString errorCode;
		QString message;
	};

	struct Summary {
		QString operationID;
		QString kind;
		QString status;
		int totalItems     = 0;
		int completedItems = 0;
		int successfulItems = 0;
		int failedItems     = 0;
		int cancelledItems  = 0;
		bool cancellationRequested = false;
		QList< ItemResult > results;
	};

	explicit PluginOperation(QString kind, QString operationID = {});

	QString operationID() const;
	QString kind() const;
	std::shared_ptr< std::atomic< bool > > cancellationToken() const;

	/// Registers the complete item set before work starts. Duplicate and empty IDs are ignored.
	void setItems(const QStringList &itemIDs);
	bool requestCancellation();
	bool isCancellationRequested() const;
	/// Marks an item as having crossed its final cancellation boundary while its terminal result is in flight.
	/// A cancellation request is ignored once every unfinished item is sealed.
	bool sealItem(const QString &itemID);

	/// Records a terminal item result. Returns false for an unknown item or a duplicate completion.
	bool completeItem(const ItemResult &result);
	QStringList pendingItems() const;
	Summary summary() const;

private:
	QString statusLocked() const;

	mutable QMutex m_mutex;
	QString m_operationID;
	QString m_kind;
	QStringList m_itemOrder;
	QHash< QString, ItemResult > m_results;
	QSet< QString > m_sealedItems;
	std::shared_ptr< std::atomic< bool > > m_cancelToken;
};

#endif // MUMBLE_MUMBLE_PLUGINOPERATION_H_
