// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_WASAPIDEVICEROUTING_H_
#define MUMBLE_MUMBLE_WASAPIDEVICEROUTING_H_

#include <QtCore/QList>
#include <QtCore/QString>

namespace Mumble::WASAPI {

enum class RoutingPolicy { FollowDefault, PreferSelected, StrictSelected };
enum class LatencyProfile { Stable, Balanced, Low };

struct DeviceDescriptor {
	QString endpointId;
	QString displayName;
	QString containerId;
	QString parentInstanceId;
	QString adapterName;
	QString association;
	int dataFlow   = -1;
	int formFactor = -1;

	bool hasStableFingerprint() const;
	bool operator==(const DeviceDescriptor &other) const;
};

struct MatchResult {
	int index = -1;
	int score = 0;
	bool ambiguous = false;

	bool matched() const { return index >= 0 && !ambiguous; }
};

struct PriorityMatchResult {
	int priorityIndex  = -1;
	int candidateIndex = -1;
	int score          = 0;
	bool reboundByFingerprint = false;

	bool matched() const { return priorityIndex >= 0 && candidateIndex >= 0; }
};

struct RuntimeState {
	QString configuredEndpointId;
	QString preferredDisplayName;
	QString activeEndpointId;
	QString activeDisplayName;
	QString lastError;
	RoutingPolicy policy = RoutingPolicy::FollowDefault;
	bool preferredAvailable = true;
	bool usingFallback       = false;
	bool usingSecondaryPriority = false;
	bool reboundByFingerprint = false;
	bool streamActive         = false;
	int activePriorityIndex   = -1;
	int priorityCount         = 0;
};

QString routingPolicyName(RoutingPolicy policy);
RoutingPolicy routingPolicyFromName(const QString &name, bool configuredEndpointPresent);
QString latencyProfileName(LatencyProfile profile);
LatencyProfile latencyProfileFromName(const QString &name);

QString serializeDeviceDescriptor(const DeviceDescriptor &descriptor);
DeviceDescriptor deserializeDeviceDescriptor(const QString &json);
QString serializeDevicePriorityList(const QList< DeviceDescriptor > &descriptors);
QList< DeviceDescriptor > deserializeDevicePriorityList(const QString &json);
QString stableHardwareId(const DeviceDescriptor &descriptor);

/// Scores a live endpoint against a persisted physical-device descriptor.
/// Friendly names are only tie-breakers and can never produce a match alone.
int deviceMatchScore(const DeviceDescriptor &preferred, const DeviceDescriptor &candidate);
MatchResult findUniqueBestDeviceMatch(const DeviceDescriptor &preferred,
									 const QList< DeviceDescriptor > &candidates);
/// Resolves the first available persisted device, preserving list order. An
/// ambiguous physical-device match is treated as unavailable and the next
/// priority is considered instead.
PriorityMatchResult findFirstAvailablePriority(const QList< DeviceDescriptor > &priorities,
											 const QList< DeviceDescriptor > &candidates);

} // namespace Mumble::WASAPI

#endif // MUMBLE_MUMBLE_WASAPIDEVICEROUTING_H_
