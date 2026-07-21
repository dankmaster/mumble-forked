// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "WASAPIDeviceRouting.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QCryptographicHash>

namespace Mumble::WASAPI {
namespace {
	QString normalized(const QString &value) {
		return value.trimmed().toCaseFolded();
	}

	bool sameNonEmpty(const QString &first, const QString &second) {
		return !first.trimmed().isEmpty() && normalized(first) == normalized(second);
	}
} // namespace

bool DeviceDescriptor::hasStableFingerprint() const {
	return dataFlow >= 0 && (!containerId.trimmed().isEmpty() || !parentInstanceId.trimmed().isEmpty()
						   || (!adapterName.trimmed().isEmpty() && !association.trimmed().isEmpty()));
}

bool DeviceDescriptor::operator==(const DeviceDescriptor &other) const {
	return endpointId == other.endpointId && displayName == other.displayName && containerId == other.containerId
		   && parentInstanceId == other.parentInstanceId && adapterName == other.adapterName
		   && association == other.association && dataFlow == other.dataFlow && formFactor == other.formFactor;
}

QString routingPolicyName(const RoutingPolicy policy) {
	switch (policy) {
		case RoutingPolicy::FollowDefault:
			return QStringLiteral("follow");
		case RoutingPolicy::StrictSelected:
			return QStringLiteral("strict");
		case RoutingPolicy::PreferSelected:
		default:
			return QStringLiteral("prefer");
	}
}

RoutingPolicy routingPolicyFromName(const QString &name, const bool configuredEndpointPresent) {
	const QString normalizedName = normalized(name);
	if (normalizedName == QLatin1String("follow") || !configuredEndpointPresent) {
		return RoutingPolicy::FollowDefault;
	}
	if (normalizedName == QLatin1String("strict")) {
		return RoutingPolicy::StrictSelected;
	}
	return RoutingPolicy::PreferSelected;
}

QString latencyProfileName(const LatencyProfile profile) {
	switch (profile) {
		case LatencyProfile::Balanced:
			return QStringLiteral("balanced");
		case LatencyProfile::Low:
			return QStringLiteral("low");
		case LatencyProfile::Stable:
		default:
			return QStringLiteral("stable");
	}
}

LatencyProfile latencyProfileFromName(const QString &name) {
	const QString normalizedName = normalized(name);
	if (normalizedName == QLatin1String("balanced")) {
		return LatencyProfile::Balanced;
	}
	if (normalizedName == QLatin1String("low")) {
		return LatencyProfile::Low;
	}
	return LatencyProfile::Stable;
}

QString serializeDeviceDescriptor(const DeviceDescriptor &descriptor) {
	if (!descriptor.hasStableFingerprint()) {
		return QString();
	}
	QJsonObject object;
	object.insert(QStringLiteral("endpoint_id"), descriptor.endpointId);
	object.insert(QStringLiteral("display_name"), descriptor.displayName);
	object.insert(QStringLiteral("container_id"), descriptor.containerId);
	object.insert(QStringLiteral("parent_instance_id"), descriptor.parentInstanceId);
	object.insert(QStringLiteral("adapter_name"), descriptor.adapterName);
	object.insert(QStringLiteral("association"), descriptor.association);
	object.insert(QStringLiteral("data_flow"), descriptor.dataFlow);
	object.insert(QStringLiteral("form_factor"), descriptor.formFactor);
	return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

DeviceDescriptor deserializeDeviceDescriptor(const QString &json) {
	DeviceDescriptor descriptor;
	QJsonParseError error;
	const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8(), &error);
	if (error.error != QJsonParseError::NoError || !document.isObject()) {
		return descriptor;
	}
	const QJsonObject object = document.object();
	descriptor.endpointId      = object.value(QStringLiteral("endpoint_id")).toString();
	descriptor.displayName     = object.value(QStringLiteral("display_name")).toString();
	descriptor.containerId     = object.value(QStringLiteral("container_id")).toString();
	descriptor.parentInstanceId = object.value(QStringLiteral("parent_instance_id")).toString();
	descriptor.adapterName     = object.value(QStringLiteral("adapter_name")).toString();
	descriptor.association     = object.value(QStringLiteral("association")).toString();
	descriptor.dataFlow        = object.value(QStringLiteral("data_flow")).toInt(-1);
	descriptor.formFactor      = object.value(QStringLiteral("form_factor")).toInt(-1);
	return descriptor;
}

QString stableHardwareId(const DeviceDescriptor &descriptor) {
	if (!descriptor.hasStableFingerprint()) {
		return QString();
	}
	QCryptographicHash hash(QCryptographicHash::Sha256);
	auto add = [&hash](const QString &value) {
		hash.addData(value.trimmed().toCaseFolded().toUtf8());
		hash.addData(QByteArrayView("\0", 1));
	};
	add(QString::number(descriptor.dataFlow));
	if (!descriptor.parentInstanceId.trimmed().isEmpty()) {
		add(QStringLiteral("parent"));
		add(descriptor.parentInstanceId);
	} else if (!descriptor.containerId.trimmed().isEmpty()) {
		add(QStringLiteral("container"));
		add(descriptor.containerId);
	} else {
		add(QStringLiteral("adapter"));
		add(descriptor.adapterName);
		add(descriptor.association);
	}
	add(QString::number(descriptor.formFactor));
	return QStringLiteral("wasapi-hw:") + QString::fromLatin1(hash.result().toHex());
}

int deviceMatchScore(const DeviceDescriptor &preferred, const DeviceDescriptor &candidate) {
	if (preferred.dataFlow < 0 || candidate.dataFlow != preferred.dataFlow) {
		return 0;
	}
	if (sameNonEmpty(preferred.endpointId, candidate.endpointId)) {
		return 1000;
	}

	int score = 0;
	const bool sameParent    = sameNonEmpty(preferred.parentInstanceId, candidate.parentInstanceId);
	const bool sameContainer = sameNonEmpty(preferred.containerId, candidate.containerId);
	const bool sameAdapter   = sameNonEmpty(preferred.adapterName, candidate.adapterName);
	const bool sameAssociation = sameNonEmpty(preferred.association, candidate.association);
	const bool sameName      = sameNonEmpty(preferred.displayName, candidate.displayName);

	if (sameParent) {
		score += 700;
	}
	if (sameContainer) {
		score += 500;
	}
	if (sameAdapter && sameAssociation) {
		score += 350;
	}
	if (preferred.formFactor >= 0 && preferred.formFactor == candidate.formFactor) {
		score += 40;
	}
	if (sameName) {
		score += 25;
	}

	// Never rebind from a friendly-name/form-factor coincidence alone.
	return score >= 350 ? score : 0;
}

MatchResult findUniqueBestDeviceMatch(const DeviceDescriptor &preferred,
									 const QList< DeviceDescriptor > &candidates) {
	MatchResult result;
	for (int index = 0; index < candidates.size(); ++index) {
		const int score = deviceMatchScore(preferred, candidates.at(index));
		if (score <= 0) {
			continue;
		}
		if (score > result.score) {
			result.index     = index;
			result.score     = score;
			result.ambiguous = false;
		} else if (score == result.score) {
			result.ambiguous = true;
		}
	}
	if (result.ambiguous) {
		result.index = -1;
	}
	return result;
}

} // namespace Mumble::WASAPI
