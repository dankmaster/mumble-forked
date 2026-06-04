// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ChatFeature.h"

namespace Mumble {
namespace ChatFeatures {
	bool isKnownFeature(const MumbleProto::ChatFeature feature) {
		switch (feature) {
			case MumbleProto::ChatFeaturePersistentHistory:
			case MumbleProto::ChatFeatureHistoryPagination:
			case MumbleProto::ChatFeatureReadState:
			case MumbleProto::ChatFeatureReactions:
			case MumbleProto::ChatFeatureMessageDelete:
			case MumbleProto::ChatFeatureAttachments:
			case MumbleProto::ChatFeatureEmbeds:
			case MumbleProto::ChatFeatureHistoryGrants:
			case MumbleProto::ChatFeatureTextChannels:
			case MumbleProto::ChatFeatureDirectMessages:
			case MumbleProto::ChatFeatureHistoryWarmup:
			case MumbleProto::ChatFeatureActorAvatars:
				return true;
			default:
				return false;
		}
	}

	QList< int > supportedFeatureList() {
		return { static_cast< int >(MumbleProto::ChatFeaturePersistentHistory),
				 static_cast< int >(MumbleProto::ChatFeatureHistoryPagination),
				 static_cast< int >(MumbleProto::ChatFeatureReadState),
				 static_cast< int >(MumbleProto::ChatFeatureReactions),
				 static_cast< int >(MumbleProto::ChatFeatureMessageDelete),
				 static_cast< int >(MumbleProto::ChatFeatureAttachments),
				 static_cast< int >(MumbleProto::ChatFeatureEmbeds),
				 static_cast< int >(MumbleProto::ChatFeatureHistoryGrants),
				 static_cast< int >(MumbleProto::ChatFeatureTextChannels),
				 static_cast< int >(MumbleProto::ChatFeatureDirectMessages),
				 static_cast< int >(MumbleProto::ChatFeatureHistoryWarmup),
				 static_cast< int >(MumbleProto::ChatFeatureActorAvatars) };
	}

	QList< int > sanitizeFeatureList(const QList< int > &features) {
		QList< int > sanitized;
		for (const int feature : features) {
			const MumbleProto::ChatFeature typedFeature = static_cast< MumbleProto::ChatFeature >(feature);
			if (!isKnownFeature(typedFeature) || sanitized.contains(feature)) {
				continue;
			}

			sanitized.append(feature);
		}

		return sanitized;
	}

	bool contains(const QList< int > &features, const MumbleProto::ChatFeature feature) {
		return isKnownFeature(feature) && features.contains(static_cast< int >(feature));
	}

	bool serverAllowsClientFeature(const QList< int > &features, const MumbleProto::ChatFeature feature) {
		if (!isKnownFeature(feature)) {
			return false;
		}

		if (!features.isEmpty()) {
			return contains(features, feature);
		}

		return feature != MumbleProto::ChatFeatureHistoryGrants
			   && feature != MumbleProto::ChatFeatureDirectMessages
			   && feature != MumbleProto::ChatFeatureHistoryWarmup
			   && feature != MumbleProto::ChatFeatureActorAvatars;
	}

	void addSupportedFeatures(MumbleProto::Version &version) {
		for (const int feature : supportedFeatureList()) {
			version.add_supported_chat_features(static_cast< MumbleProto::ChatFeature >(feature));
		}
	}

	void addSupportedFeatures(MumbleProto::ServerConfig &config) {
		config.set_persistent_chat_protocol_version(CURRENT_PROTOCOL_VERSION);
		for (const int feature : supportedFeatureList()) {
			config.add_supported_chat_features(static_cast< MumbleProto::ChatFeature >(feature));
		}
	}

	QList< int > featuresFromVersion(const MumbleProto::Version &version) {
		QList< int > features;
		features.reserve(version.supported_chat_features_size());
		for (int i = 0; i < version.supported_chat_features_size(); ++i) {
			features.append(static_cast< int >(version.supported_chat_features(i)));
		}

		features = sanitizeFeatureList(features);
		if (features.isEmpty() && version.has_supports_persistent_chat() && version.supports_persistent_chat()) {
			QList< int > legacyFeatures = supportedFeatureList();
			legacyFeatures.removeAll(static_cast< int >(MumbleProto::ChatFeatureHistoryGrants));
			legacyFeatures.removeAll(static_cast< int >(MumbleProto::ChatFeatureDirectMessages));
			legacyFeatures.removeAll(static_cast< int >(MumbleProto::ChatFeatureHistoryWarmup));
			legacyFeatures.removeAll(static_cast< int >(MumbleProto::ChatFeatureActorAvatars));
			return legacyFeatures;
		}

		return features;
	}

	QList< int > featuresFromServerConfig(const MumbleProto::ServerConfig &config) {
		QList< int > features;
		features.reserve(config.supported_chat_features_size());
		for (int i = 0; i < config.supported_chat_features_size(); ++i) {
			features.append(static_cast< int >(config.supported_chat_features(i)));
		}

		return sanitizeFeatureList(features);
	}
} // namespace ChatFeatures
} // namespace Mumble
