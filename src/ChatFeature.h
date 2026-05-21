// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_CHATFEATURE_H_
#define MUMBLE_CHATFEATURE_H_

#include "Mumble.pb.h"

#include <QList>

namespace Mumble {
namespace ChatFeatures {
	constexpr unsigned int CURRENT_PROTOCOL_VERSION = 1;

	bool isKnownFeature(MumbleProto::ChatFeature feature);
	QList< int > supportedFeatureList();
	QList< int > sanitizeFeatureList(const QList< int > &features);
	bool contains(const QList< int > &features, MumbleProto::ChatFeature feature);
	bool serverAllowsClientFeature(const QList< int > &features, MumbleProto::ChatFeature feature);

	void addSupportedFeatures(MumbleProto::Version &version);
	void addSupportedFeatures(MumbleProto::ServerConfig &config);

	QList< int > featuresFromVersion(const MumbleProto::Version &version);
	QList< int > featuresFromServerConfig(const MumbleProto::ServerConfig &config);
} // namespace ChatFeatures
} // namespace Mumble

#endif // MUMBLE_CHATFEATURE_H_
