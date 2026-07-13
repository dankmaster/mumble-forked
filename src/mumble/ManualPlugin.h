// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef MUMBLE_MUMBLE_MANUALPLUGIN_H_
#define MUMBLE_MUMBLE_MANUALPLUGIN_H_

#include "LegacyPlugin.h"

#include <QtCore/QHash>
#include <QtCore/QVariantMap>

struct Position2D {
	float x;
	float y;
};

MumblePlugin *ManualPlugin_getMumblePlugin();
MumblePluginQt *ManualPlugin_getMumblePluginQt();
QVariantMap ManualPlugin_modernState();
QVariantList ManualPlugin_modernSpeakers();
void ManualPlugin_applyModernState(const QVariantMap &state);
void ManualPlugin_resetModernState();
void ManualPlugin_setSpeakerPositions(const QHash< unsigned int, Position2D > &positions);

class ManualPlugin : public LegacyPlugin {
	friend class Plugin;

private:
	Q_OBJECT
	Q_DISABLE_COPY(ManualPlugin)

protected:
	void resolveFunctionPointers() override;
	explicit ManualPlugin(QObject *parent = nullptr);

public:
	~ManualPlugin() override;
};

#endif // MUMBLE_MUMBLE_MANUALPLUGIN_H_
