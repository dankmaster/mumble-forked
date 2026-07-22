// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ForkFeature.h"

namespace Mumble {
namespace ForkFeatures {
	namespace {
		const QList< FeatureDescriptor > &featureTableData() {
			static const QList< FeatureDescriptor > table = {
				{ MumbleProto::ForkFeatureServerLinkPreviewProxy, 1, FallbackPolicy::ServerOnly,
				  "server_link_preview_proxy" },
				{ MumbleProto::ForkFeatureWatchTogetherRooms, 1, FallbackPolicy::ServerOnly,
				  "watch_together_rooms" },
				{ MumbleProto::ForkFeatureScreenShareSessionPresence, 1, FallbackPolicy::ServerOnly,
				  "screen_share_session_presence" },
				{ MumbleProto::ForkFeatureVirtualizedChatPresentation, 1, FallbackPolicy::None,
				  "virtualized_chat_presentation" },
				{ MumbleProto::ForkFeatureStonksLedger, 1, FallbackPolicy::ServerOnly, "stonks_ledger" },
				{ MumbleProto::ForkFeatureClientAssistedLinkPreviews, 2, FallbackPolicy::ServerOnly,
				  "client_assisted_link_previews" },
				{ MumbleProto::ForkFeatureInAppFeedback, 3, FallbackPolicy::ServerOnly, "in_app_feedback" },
				{ MumbleProto::ForkFeatureToolsAcl, 3, FallbackPolicy::ServerOnly, "tools_acl" },
				{ MumbleProto::ForkFeatureServerLogStream, 3, FallbackPolicy::ServerOnly,
				  "server_log_stream" },
			};
			return table;
		}
	} // namespace

	QList< FeatureDescriptor > featureTable() {
		return featureTableData();
	}

	bool isKnownFeature(const MumbleProto::ForkFeature feature) {
		for (const FeatureDescriptor &descriptor : featureTableData()) {
			if (descriptor.feature == feature) {
				return true;
			}
		}

		return false;
	}

	QString featureName(const MumbleProto::ForkFeature feature) {
		for (const FeatureDescriptor &descriptor : featureTableData()) {
			if (descriptor.feature == feature) {
				return QString::fromLatin1(descriptor.name);
			}
		}

		return QStringLiteral("unknown");
	}

	unsigned int minProtocolVersion(const MumbleProto::ForkFeature feature) {
		for (const FeatureDescriptor &descriptor : featureTableData()) {
			if (descriptor.feature == feature) {
				return descriptor.minProtocolVersion;
			}
		}

		return 0;
	}

	FallbackPolicy fallbackPolicy(const MumbleProto::ForkFeature feature) {
		for (const FeatureDescriptor &descriptor : featureTableData()) {
			if (descriptor.feature == feature) {
				return descriptor.fallback;
			}
		}

		return FallbackPolicy::None;
	}

	QString fallbackPolicyName(const FallbackPolicy policy) {
		switch (policy) {
			case FallbackPolicy::None:
				return QStringLiteral("none");
			case FallbackPolicy::PluginData:
				return QStringLiteral("plugin_data");
			case FallbackPolicy::LegacyText:
				return QStringLiteral("legacy_text");
			case FallbackPolicy::ServerOnly:
				return QStringLiteral("server_only");
		}

		return QStringLiteral("unknown");
	}

	QList< int > supportedFeatureList() {
		QList< int > features;
		for (const FeatureDescriptor &descriptor : featureTableData()) {
			if (descriptor.minProtocolVersion <= CURRENT_PROTOCOL_VERSION) {
				features.append(static_cast< int >(descriptor.feature));
			}
		}

		return features;
	}

	QList< int > sanitizeFeatureList(const QList< int > &features, const unsigned int protocolVersion) {
		QList< int > sanitized;
		for (const int feature : features) {
			const MumbleProto::ForkFeature typedFeature = static_cast< MumbleProto::ForkFeature >(feature);
			if (!isKnownFeature(typedFeature) || minProtocolVersion(typedFeature) > protocolVersion
				|| sanitized.contains(feature)) {
				continue;
			}

			sanitized.append(feature);
		}

		return sanitized;
	}

	bool contains(const QList< int > &features, const MumbleProto::ForkFeature feature) {
		return isKnownFeature(feature) && features.contains(static_cast< int >(feature));
	}

	bool serverAllowsClientFeature(const QList< int > &features, const MumbleProto::ForkFeature feature) {
		return !features.isEmpty() && contains(features, feature);
	}

	void addSupportedFeatures(MumbleProto::Version &version) {
		version.set_fork_extension_protocol_version(CURRENT_PROTOCOL_VERSION);
		for (const int feature : supportedFeatureList()) {
			version.add_supported_fork_features(static_cast< MumbleProto::ForkFeature >(feature));
		}
	}

	void addSupportedFeatures(MumbleProto::ServerConfig &config) {
		config.set_fork_extension_protocol_version(CURRENT_PROTOCOL_VERSION);
		for (const int feature : supportedFeatureList()) {
			config.add_supported_fork_features(static_cast< MumbleProto::ForkFeature >(feature));
		}
	}

	QList< int > featuresFromVersion(const MumbleProto::Version &version) {
		QList< int > features;
		features.reserve(version.supported_fork_features_size());
		for (int i = 0; i < version.supported_fork_features_size(); ++i) {
			features.append(static_cast< int >(version.supported_fork_features(i)));
		}

		return sanitizeFeatureList(features,
								   version.has_fork_extension_protocol_version()
									   ? version.fork_extension_protocol_version()
									   : CURRENT_PROTOCOL_VERSION);
	}

	QList< int > featuresFromServerConfig(const MumbleProto::ServerConfig &config) {
		QList< int > features;
		features.reserve(config.supported_fork_features_size());
		for (int i = 0; i < config.supported_fork_features_size(); ++i) {
			features.append(static_cast< int >(config.supported_fork_features(i)));
		}

		return sanitizeFeatureList(features,
								   config.has_fork_extension_protocol_version()
									   ? config.fork_extension_protocol_version()
									   : CURRENT_PROTOCOL_VERSION);
	}
} // namespace ForkFeatures
} // namespace Mumble
