// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_FORKFEATURE_H_
#define MUMBLE_FORKFEATURE_H_

#include "Mumble.pb.h"

#include <QList>
#include <QString>

namespace Mumble {
namespace ForkFeatures {
	constexpr unsigned int CURRENT_PROTOCOL_VERSION = 3;

	enum class FallbackPolicy { None, PluginData, LegacyText, ServerOnly };

	struct FeatureDescriptor {
		MumbleProto::ForkFeature feature;
		unsigned int minProtocolVersion;
		FallbackPolicy fallback;
		const char *name;
	};

	QList< FeatureDescriptor > featureTable();
	bool isKnownFeature(MumbleProto::ForkFeature feature);
	QString featureName(MumbleProto::ForkFeature feature);
	unsigned int minProtocolVersion(MumbleProto::ForkFeature feature);
	FallbackPolicy fallbackPolicy(MumbleProto::ForkFeature feature);
	QString fallbackPolicyName(FallbackPolicy policy);

	QList< int > supportedFeatureList();
	QList< int > sanitizeFeatureList(const QList< int > &features, unsigned int protocolVersion);
	bool contains(const QList< int > &features, MumbleProto::ForkFeature feature);
	bool serverAllowsClientFeature(const QList< int > &features, MumbleProto::ForkFeature feature);

	void addSupportedFeatures(MumbleProto::Version &version);
	void addSupportedFeatures(MumbleProto::ServerConfig &config);

	QList< int > featuresFromVersion(const MumbleProto::Version &version);
	QList< int > featuresFromServerConfig(const MumbleProto::ServerConfig &config);
} // namespace ForkFeatures
} // namespace Mumble

#endif // MUMBLE_FORKFEATURE_H_
