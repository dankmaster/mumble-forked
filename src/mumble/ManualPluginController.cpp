// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "ManualPluginController.h"

#include "ManualPlugin.h"

#include <QtCore/QVariantMap>
#include <QtCore/QTimer>

#include <algorithm>
#include <cmath>

namespace {
double finiteOr(const double value, const double fallback) {
	return std::isfinite(value) ? value : fallback;
}
}

ManualPluginController::ManualPluginController(QObject *parent) : QObject(parent) {
	m_speakerRefreshTimer = new QTimer(this);
	m_speakerRefreshTimer->setInterval(100);
	m_speakerRefreshTimer->setTimerType(Qt::CoarseTimer);
	connect(m_speakerRefreshTimer, &QTimer::timeout, this, [this] {
		const QVariantList speakers = ManualPlugin_modernSpeakers();
		if (m_speakers == speakers) return;
		m_speakers = speakers;
		emit speakersChanged();
	});
	refresh();
}

double ManualPluginController::x() const { return m_x; }
double ManualPluginController::y() const { return m_y; }
double ManualPluginController::z() const { return m_z; }
int ManualPluginController::azimuth() const { return m_azimuth; }
int ManualPluginController::elevation() const { return m_elevation; }
QString ManualPluginController::context() const { return m_context; }
QString ManualPluginController::identity() const { return m_identity; }
int ManualPluginController::staleSeconds() const { return m_staleSeconds; }
bool ManualPluginController::active() const { return m_active; }
bool ManualPluginController::linked() const { return m_linked; }
QVariantList ManualPluginController::speakers() const { return m_speakers; }

void ManualPluginController::setX(const double value) {
	const double normalized = finiteOr(value, m_x);
	if (qFuzzyCompare(m_x, normalized)) return;
	m_x = normalized;
	emit stateChanged();
}

void ManualPluginController::setY(const double value) {
	const double normalized = finiteOr(value, m_y);
	if (qFuzzyCompare(m_y, normalized)) return;
	m_y = normalized;
	emit stateChanged();
}

void ManualPluginController::setZ(const double value) {
	const double normalized = finiteOr(value, m_z);
	if (qFuzzyCompare(m_z, normalized)) return;
	m_z = normalized;
	emit stateChanged();
}

void ManualPluginController::setAzimuth(const int value) {
	const int normalized = std::clamp(value, 0, 360);
	if (m_azimuth == normalized) return;
	m_azimuth = normalized;
	emit stateChanged();
}

void ManualPluginController::setElevation(const int value) {
	const int normalized = std::clamp(value, -90, 90);
	if (m_elevation == normalized) return;
	m_elevation = normalized;
	emit stateChanged();
}

void ManualPluginController::setContext(const QString &value) {
	const QString normalized = value.left(4096);
	if (m_context == normalized) return;
	m_context = normalized;
	emit stateChanged();
}

void ManualPluginController::setIdentity(const QString &value) {
	const QString normalized = value.left(4096);
	if (m_identity == normalized) return;
	m_identity = normalized;
	emit stateChanged();
}

void ManualPluginController::setStaleSeconds(const int value) {
	const int normalized = std::clamp(value, 0, 3600);
	if (m_staleSeconds == normalized) return;
	m_staleSeconds = normalized;
	emit stateChanged();
}

void ManualPluginController::setActive(const bool value) {
	if (m_active == value) return;
	m_active = value;
	emit stateChanged();
}

void ManualPluginController::setLinked(const bool value) {
	if (m_linked == value) return;
	m_linked = value;
	emit stateChanged();
}

void ManualPluginController::refresh() {
	const QVariantMap state = ManualPlugin_modernState();
	m_x = state.value(QStringLiteral("manual.x")).toDouble();
	m_y = state.value(QStringLiteral("manual.y")).toDouble();
	m_z = state.value(QStringLiteral("manual.z")).toDouble();
	m_azimuth = state.value(QStringLiteral("manual.azimuth")).toInt();
	m_elevation = state.value(QStringLiteral("manual.elevation")).toInt();
	m_context = state.value(QStringLiteral("manual.context")).toString();
	m_identity = state.value(QStringLiteral("manual.identity")).toString();
	m_staleSeconds = state.value(QStringLiteral("manual.staleSeconds")).toInt();
	m_active = state.value(QStringLiteral("manual.active")).toBool();
	m_linked = state.value(QStringLiteral("manual.linked")).toBool();
	const QVariantList speakers = state.value(QStringLiteral("manual.speakers")).toList();
	const bool speakerListChanged = m_speakers != speakers;
	m_speakers = speakers;
	emit stateChanged();
	if (speakerListChanged) emit speakersChanged();
}

void ManualPluginController::setSpeakerUpdatesEnabled(const bool enabled) {
	if (!m_speakerRefreshTimer) return;
	if (!enabled) {
		m_speakerRefreshTimer->stop();
		return;
	}
	const QVariantList speakers = ManualPlugin_modernSpeakers();
	if (m_speakers != speakers) {
		m_speakers = speakers;
		emit speakersChanged();
	}
	if (!m_speakerRefreshTimer->isActive()) m_speakerRefreshTimer->start();
}

void ManualPluginController::apply() {
	ManualPlugin_applyModernState(QVariantMap {
		{ QStringLiteral("manual.x"), m_x },
		{ QStringLiteral("manual.y"), m_y },
		{ QStringLiteral("manual.z"), m_z },
		{ QStringLiteral("manual.azimuth"), m_azimuth },
		{ QStringLiteral("manual.elevation"), m_elevation },
		{ QStringLiteral("manual.context"), m_context },
		{ QStringLiteral("manual.identity"), m_identity },
		{ QStringLiteral("manual.staleSeconds"), m_staleSeconds },
		{ QStringLiteral("manual.active"), m_active },
		{ QStringLiteral("manual.linked"), m_linked }
	});
	refresh();
	emit applied();
}

void ManualPluginController::reset() {
	ManualPlugin_resetModernState();
	refresh();
	emit resetCompleted();
}
