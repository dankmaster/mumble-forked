// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "PluginOperation.h"

#include <QtCore/QMutexLocker>
#include <QtCore/QUuid>

#include <utility>

PluginCancellationGate::PluginCancellationGate()
	: m_cancelToken(std::make_shared< std::atomic< bool > >(false)) {
}

std::shared_ptr< std::atomic< bool > > PluginCancellationGate::cancellationToken() const {
	return m_cancelToken;
}

bool PluginCancellationGate::requestCancellation() {
	State expected = State::Active;
	if (!m_state.compare_exchange_strong(expected, State::CancellationRequested,
											 std::memory_order_acq_rel, std::memory_order_acquire)) {
		return false;
	}
	m_cancelToken->store(true, std::memory_order_release);
	return true;
}

bool PluginCancellationGate::seal() {
	State expected = State::Active;
	return m_state.compare_exchange_strong(expected, State::Sealed,
										 std::memory_order_acq_rel, std::memory_order_acquire);
}

bool PluginCancellationGate::isCancellationRequested() const {
	return m_state.load(std::memory_order_acquire) == State::CancellationRequested;
}

bool PluginCancellationGate::isSealed() const {
	return m_state.load(std::memory_order_acquire) == State::Sealed;
}

PluginOperation::PluginOperation(QString kind, QString operationID)
	: m_operationID(operationID.trimmed()), m_kind(std::move(kind)),
	  m_cancelToken(std::make_shared< std::atomic< bool > >(false)) {
	if (m_operationID.isEmpty()) {
		const QString prefix = m_kind.trimmed().isEmpty() ? QStringLiteral("plugin-operation") : m_kind.trimmed();
		m_operationID = QStringLiteral("%1:%2")
						.arg(prefix, QUuid::createUuid().toString(QUuid::WithoutBraces));
	}
}

QString PluginOperation::operationID() const {
	QMutexLocker lock(&m_mutex);
	return m_operationID;
}

QString PluginOperation::kind() const {
	QMutexLocker lock(&m_mutex);
	return m_kind;
}

std::shared_ptr< std::atomic< bool > > PluginOperation::cancellationToken() const {
	QMutexLocker lock(&m_mutex);
	return m_cancelToken;
}

void PluginOperation::setItems(const QStringList &itemIDs) {
	QMutexLocker lock(&m_mutex);
	if (!m_itemOrder.isEmpty() || !m_results.isEmpty()) {
		return;
	}
	for (const QString &itemID : itemIDs) {
		const QString normalized = itemID.trimmed();
		if (!normalized.isEmpty() && !m_itemOrder.contains(normalized)) {
			m_itemOrder.push_back(normalized);
		}
	}
}

bool PluginOperation::requestCancellation() {
	QMutexLocker lock(&m_mutex);
	if (m_results.size() + m_sealedItems.size() >= m_itemOrder.size() || m_cancelToken->exchange(true)) {
		return false;
	}
	return true;
}

bool PluginOperation::isCancellationRequested() const {
	return m_cancelToken->load();
}

bool PluginOperation::sealItem(const QString &itemID) {
	QMutexLocker lock(&m_mutex);
	const QString normalized = itemID.trimmed();
	if (normalized.isEmpty() || !m_itemOrder.contains(normalized) || m_results.contains(normalized)
		|| m_sealedItems.contains(normalized)) {
		return false;
	}
	m_sealedItems.insert(normalized);
	return true;
}

bool PluginOperation::completeItem(const ItemResult &result) {
	QMutexLocker lock(&m_mutex);
	if (result.itemID.isEmpty() || !m_itemOrder.contains(result.itemID) || m_results.contains(result.itemID)) {
		return false;
	}
	ItemResult normalized = result;
	if (normalized.cancelled) {
		normalized.success = false;
		if (normalized.errorCode.isEmpty()) {
			normalized.errorCode = QStringLiteral("cancelled");
		}
	}
	m_sealedItems.remove(normalized.itemID);
	m_results.insert(normalized.itemID, normalized);
	return true;
}

QStringList PluginOperation::pendingItems() const {
	QMutexLocker lock(&m_mutex);
	QStringList pending;
	for (const QString &itemID : m_itemOrder) {
		if (!m_results.contains(itemID) && !m_sealedItems.contains(itemID)) {
			pending.push_back(itemID);
		}
	}
	return pending;
}

QString PluginOperation::statusLocked() const {
	if (m_results.size() < m_itemOrder.size()) {
		return m_cancelToken->load() ? QStringLiteral("cancelling") : QStringLiteral("running");
	}
	int successful = 0;
	int cancelled  = 0;
	for (const ItemResult &result : m_results) {
		if (result.success) {
			++successful;
		} else if (result.cancelled) {
			++cancelled;
		}
	}
	if (m_itemOrder.isEmpty()) {
		return m_cancelToken->load() ? QStringLiteral("cancelled") : QStringLiteral("succeeded");
	}
	if (successful == m_itemOrder.size()) {
		return QStringLiteral("succeeded");
	}
	if (successful > 0) {
		return QStringLiteral("partial");
	}
	if (cancelled == m_itemOrder.size()) {
		return QStringLiteral("cancelled");
	}
	return QStringLiteral("failed");
}

PluginOperation::Summary PluginOperation::summary() const {
	QMutexLocker lock(&m_mutex);
	Summary summary;
	summary.operationID           = m_operationID;
	summary.kind                  = m_kind;
	summary.status                = statusLocked();
	summary.totalItems            = m_itemOrder.size();
	summary.completedItems        = m_results.size();
	summary.cancellationRequested = m_cancelToken->load();
	for (const QString &itemID : m_itemOrder) {
		const auto it = m_results.constFind(itemID);
		if (it == m_results.cend()) {
			continue;
		}
		summary.results.push_back(it.value());
		if (it->success) {
			++summary.successfulItems;
		} else if (it->cancelled) {
			++summary.cancelledItems;
		} else {
			++summary.failedItems;
		}
	}
	return summary;
}
