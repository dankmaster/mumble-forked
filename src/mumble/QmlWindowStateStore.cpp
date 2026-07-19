// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "QmlWindowStateStore.h"

#include "QmlWindowStateController.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSize>
#include <QtGui/QWindow>

#include <algorithm>

namespace {
constexpr auto StoreStateFormat = "mumble-qml-auxiliary-window-states";
constexpr int StoreStateVersion = 1;
constexpr int MaximumStateCount = 256;
constexpr int MaximumEncodedSize = 256 * 1024;
constexpr int MaximumKeyLength = 192;
}

QmlWindowStateStore::QmlWindowStateStore(const QByteArray &encodedStates, QObject *parent)
	: QObject(parent), m_states(decode(encodedStates)) {
}

QmlWindowStateStore::~QmlWindowStateStore() {
	flush();
	const QList< QWindow * > windows = m_attachments.keys();
	for (QWindow *window : windows) detachWindow(window, false);
}

QString QmlWindowStateStore::normalizedKey(const QString &logicalKey) {
	QString key = logicalKey.trimmed();
	if (key.isEmpty() || key.contains(QChar::Null)) return {};
	if (key.size() > MaximumKeyLength) key.truncate(MaximumKeyLength);
	return key;
}

bool QmlWindowStateStore::restoreWindow(QObject *windowObject, const QString &logicalKey,
										int minimumWidth, int minimumHeight) {
	QWindow *window = qobject_cast< QWindow * >(windowObject);
	const QString key = normalizedKey(logicalKey);
	if (!window || key.isEmpty()) return false;

	const bool hasSavedState = m_states.contains(key)
		&& QmlWindowStateController::decode(m_states.value(key)).has_value();
	auto existing = m_attachments.find(window);
	if (existing != m_attachments.end() && existing->key == key && existing->controller) {
		return hasSavedState;
	}
	if (existing != m_attachments.end()) detachWindow(window, true);

	auto *controller = new QmlWindowStateController(this);
	Attachment attachment { key, controller, {} };
	connect(controller, &QmlWindowStateController::encodedStateChanged, this,
			[this, key](const QByteArray &state) { recordState(key, state); });
	attachment.destroyedConnection =
		connect(window, &QObject::destroyed, this, [this, window]() { detachWindow(window, false); });
	m_attachments.insert(window, attachment);
	controller->attach(window, hasSavedState ? m_states.value(key) : QByteArray(),
		QSize(std::max(1, minimumWidth), std::max(1, minimumHeight)));
	return hasSavedState;
}

void QmlWindowStateStore::flushWindow(QObject *windowObject) {
	if (QWindow *window = qobject_cast< QWindow * >(windowObject)) {
		auto entry = m_attachments.find(window);
		if (entry != m_attachments.end() && entry->controller) entry->controller->flush();
	}
}

void QmlWindowStateStore::flush() {
	const QList< Attachment > attachments = m_attachments.values();
	for (const Attachment &attachment : attachments) {
		if (attachment.controller) attachment.controller->flush();
	}
}

QByteArray QmlWindowStateStore::encodedStates() const {
	return encode(m_states);
}

QByteArray QmlWindowStateStore::encode(const QHash< QString, QByteArray > &states) {
	QJsonObject values;
	QStringList keys = states.keys();
	std::sort(keys.begin(), keys.end());
	for (const QString &rawKey : keys) {
		const QString key = normalizedKey(rawKey);
		const QByteArray state = states.value(rawKey);
		if (key.isEmpty() || !QmlWindowStateController::decode(state).has_value()) continue;
		values.insert(key, QString::fromLatin1(state.toBase64()));
		if (values.size() >= MaximumStateCount) break;
	}
	const QJsonObject root { { QStringLiteral("format"), QString::fromLatin1(StoreStateFormat) },
							 { QStringLiteral("version"), StoreStateVersion },
							 { QStringLiteral("states"), values } };
	return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QHash< QString, QByteArray > QmlWindowStateStore::decode(const QByteArray &encodedStates) {
	QHash< QString, QByteArray > states;
	if (encodedStates.isEmpty() || encodedStates.size() > MaximumEncodedSize) return states;
	QJsonParseError error;
	const QJsonDocument document = QJsonDocument::fromJson(encodedStates, &error);
	if (error.error != QJsonParseError::NoError || !document.isObject()) return states;
	const QJsonObject root = document.object();
	if (root.value(QStringLiteral("format")).toString() != QLatin1String(StoreStateFormat)
		|| root.value(QStringLiteral("version")).toInt() != StoreStateVersion
		|| !root.value(QStringLiteral("states")).isObject()) {
		return states;
	}
	const QJsonObject values = root.value(QStringLiteral("states")).toObject();
	for (auto it = values.constBegin(); it != values.constEnd() && states.size() < MaximumStateCount; ++it) {
		const QString key = normalizedKey(it.key());
		if (key.isEmpty() || !it.value().isString()) continue;
		const QByteArray state = QByteArray::fromBase64(it.value().toString().toLatin1());
		if (!QmlWindowStateController::decode(state).has_value()) continue;
		states.insert(key, state);
	}
	return states;
}

void QmlWindowStateStore::detachWindow(QWindow *window, const bool flushFirst) {
	auto entry = m_attachments.find(window);
	if (entry == m_attachments.end()) return;
	QmlWindowStateController *controller = entry->controller;
	const QMetaObject::Connection destroyedConnection = entry->destroyedConnection;
	m_attachments.erase(entry);
	disconnect(destroyedConnection);
	if (!controller) return;
	if (flushFirst) controller->flush();
	delete controller;
}

void QmlWindowStateStore::recordState(const QString &key, const QByteArray &state) {
	if (!QmlWindowStateController::decode(state).has_value() || m_states.value(key) == state) return;
	if (!m_states.contains(key) && m_states.size() >= MaximumStateCount) {
		m_states.erase(m_states.begin());
	}
	m_states.insert(key, state);
	emit encodedStatesChanged(encodedStates());
}
