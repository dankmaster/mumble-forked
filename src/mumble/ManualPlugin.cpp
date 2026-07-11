// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "ManualPlugin.h"

#include "Global.h"

#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>
#include <QtCore/QVariantList>

#include <cmath>
#include <cstring>

#define MUMBLE_ALLOW_DEPRECATED_LEGACY_PLUGIN_API
#include "../../plugins/mumble_legacy_plugin.h"

namespace {
	struct ManualState {
		float avatarPosition[3] = { 0.0f, 0.0f, 0.0f };
		float avatarFront[3]    = { 0.0f, 0.0f, 1.0f };
		float avatarTop[3]      = { 0.0f, 1.0f, 0.0f };
		float cameraPosition[3] = { 0.0f, 0.0f, 0.0f };
		float cameraFront[3]    = { 0.0f, 0.0f, 1.0f };
		float cameraTop[3]      = { 0.0f, 1.0f, 0.0f };
		std::string context     = "Mumble";
		std::wstring identity   = L"Agent47";
		int azimuth             = 0;
		int elevation           = 0;
		bool linkable            = false;
		bool active              = true;
	};

	QMutex stateMutex;
	ManualState state;
	QHash< unsigned int, Position2D > speakerPositions;

	void applyOrientation(ManualState &target, const int azimuth, const int elevation) {
		target.azimuth   = azimuth;
		target.elevation = elevation;
		const double azim = azimuth * M_PI / 180.0;
		const double elev = elevation * M_PI / 180.0;
		target.avatarFront[0] = static_cast< float >(std::cos(elev) * std::sin(azim));
		target.avatarFront[1] = static_cast< float >(std::sin(elev));
		target.avatarFront[2] = static_cast< float >(std::cos(elev) * std::cos(azim));
		target.avatarTop[0] = static_cast< float >(-std::sin(elev) * std::sin(azim));
		target.avatarTop[1] = static_cast< float >(std::cos(elev));
		target.avatarTop[2] = static_cast< float >(-std::sin(elev) * std::cos(azim));
		std::memcpy(target.cameraTop, target.avatarTop, sizeof(target.avatarTop));
		std::memcpy(target.cameraFront, target.avatarFront, sizeof(target.avatarFront));
	}

	int tryLock() {
		QMutexLocker lock(&stateMutex);
		return state.linkable;
	}

	void unlock() {
		QMutexLocker lock(&stateMutex);
		state.linkable = false;
	}

	int fetch(float *avatarPosition, float *avatarFront, float *avatarTop, float *cameraPosition,
			  float *cameraFront, float *cameraTop, std::string &context, std::wstring &identity) {
		QMutexLocker lock(&stateMutex);
		if (!state.linkable) {
			return false;
		}
		if (!state.active) {
			std::memset(avatarPosition, 0, sizeof(state.avatarPosition));
			std::memset(cameraPosition, 0, sizeof(state.cameraPosition));
			return true;
		}
		std::memcpy(avatarPosition, state.avatarPosition, sizeof(state.avatarPosition));
		std::memcpy(avatarFront, state.avatarFront, sizeof(state.avatarFront));
		std::memcpy(avatarTop, state.avatarTop, sizeof(state.avatarTop));
		std::memcpy(cameraPosition, state.cameraPosition, sizeof(state.cameraPosition));
		std::memcpy(cameraFront, state.cameraFront, sizeof(state.cameraFront));
		std::memcpy(cameraTop, state.cameraTop, sizeof(state.cameraTop));
		context  = state.context;
		identity = state.identity;
		return true;
	}

	const std::wstring longDescription() {
		return L"This is the manual placement plugin. It allows you to place yourself manually.";
	}

	void modernOnlyAbout(void *) {}
	void modernOnlyConfig(void *) {}

	std::wstring description(L"Manual placement plugin");
	std::wstring shortName(L"Manual placement");
	MumblePlugin manual = { MUMBLE_PLUGIN_MAGIC, description, shortName, nullptr, nullptr, tryLock, unlock,
						longDescription, fetch };
	MumblePluginQt manualQt = { MUMBLE_PLUGIN_MAGIC_QT, modernOnlyAbout, modernOnlyConfig };
}

MumblePlugin *ManualPlugin_getMumblePlugin() {
	return &manual;
}

MumblePluginQt *ManualPlugin_getMumblePluginQt() {
	return &manualQt;
}

QVariantMap ManualPlugin_modernState() {
	QMutexLocker lock(&stateMutex);
	QVariantList speakers;
	for (auto it = speakerPositions.constBegin(); it != speakerPositions.constEnd(); ++it) {
		speakers.push_back(QVariantMap { { QStringLiteral("session"), it.key() },
			{ QStringLiteral("x"), it.value().x }, { QStringLiteral("z"), it.value().y } });
	}
	return QVariantMap { { QStringLiteral("manual.linked"), state.linkable },
		{ QStringLiteral("manual.active"), state.active },
		{ QStringLiteral("manual.x"), state.avatarPosition[0] },
		{ QStringLiteral("manual.y"), state.avatarPosition[1] },
		{ QStringLiteral("manual.z"), state.avatarPosition[2] },
		{ QStringLiteral("manual.azimuth"), state.azimuth },
		{ QStringLiteral("manual.elevation"), state.elevation },
		{ QStringLiteral("manual.context"), QString::fromStdString(state.context) },
		{ QStringLiteral("manual.identity"), QString::fromStdWString(state.identity) },
		{ QStringLiteral("manual.staleSeconds"), Global::get().s.manualPlugin_silentUserDisplaytime },
		{ QStringLiteral("manual.speakers"), speakers } };
}

void ManualPlugin_applyModernState(const QVariantMap &values) {
	QMutexLocker lock(&stateMutex);
	state.linkable = values.value(QStringLiteral("manual.linked"), state.linkable).toBool();
	state.active   = values.value(QStringLiteral("manual.active"), state.active).toBool();
	state.avatarPosition[0] = state.cameraPosition[0] = values.value(QStringLiteral("manual.x"), state.avatarPosition[0]).toFloat();
	state.avatarPosition[1] = state.cameraPosition[1] = values.value(QStringLiteral("manual.y"), state.avatarPosition[1]).toFloat();
	state.avatarPosition[2] = state.cameraPosition[2] = values.value(QStringLiteral("manual.z"), state.avatarPosition[2]).toFloat();
	state.context = values.value(QStringLiteral("manual.context"), QString::fromStdString(state.context)).toString().toStdString();
	state.identity = values.value(QStringLiteral("manual.identity"), QString::fromStdWString(state.identity)).toString().toStdWString();
	Global::get().s.manualPlugin_silentUserDisplaytime = qBound(
		0, values.value(QStringLiteral("manual.staleSeconds"), Global::get().s.manualPlugin_silentUserDisplaytime).toInt(), 3600);
	applyOrientation(state, qBound(0, values.value(QStringLiteral("manual.azimuth"), state.azimuth).toInt(), 360),
		qBound(-90, values.value(QStringLiteral("manual.elevation"), state.elevation).toInt(), 90));
}

void ManualPlugin_resetModernState() {
	QMutexLocker lock(&stateMutex);
	state = ManualState();
}

void ManualPlugin_setSpeakerPositions(const QHash< unsigned int, Position2D > &positions) {
	QMutexLocker lock(&stateMutex);
	speakerPositions = positions;
}

ManualPlugin::ManualPlugin(QObject *parent) : LegacyPlugin(QStringLiteral("manual.builtin"), true, parent) {}
ManualPlugin::~ManualPlugin() = default;

void ManualPlugin::resolveFunctionPointers() {
	m_mumPlug   = &manual;
	m_mumPlugQt = &manualQt;
}
