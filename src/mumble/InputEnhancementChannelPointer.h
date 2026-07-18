// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_INPUTENHANCEMENTCHANNELPOINTER_H_
#define MUMBLE_MUMBLE_INPUTENHANCEMENTCHANNELPOINTER_H_

#include "InputEnhancementPolicy.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>

#include <cstdint>

namespace Mumble::InputEnhancement {

enum class ChannelPointerRejection : std::uint8_t {
	None,
	TooLarge,
	InvalidPublicKey,
	CryptoUnavailable,
	InvalidSignature,
	InvalidJson,
	InvalidSchema,
	WrongChannel,
	UnsafeArtifact
};

struct ChannelPointerDecision final {
	QJsonObject updateInfo;
	ChannelPointerRejection rejection = ChannelPointerRejection::None;
	QString detail;
	bool accepted = false;
};

/// Verifies the exact downloaded pointer bytes before parsing them, then
/// converts the strict channel schema into the existing updater's normalized
/// package description. No field from an unsigned pointer is consumed.
ChannelPointerDecision verifyAndNormalizeChannelPointer(const QByteArray &exactPointerBytes,
														const QByteArray &detachedSignature,
														const QByteArray &rawPublicKey,
														const DetachedSignatureVerifier &verifier,
														const QString &expectedChannel) noexcept;

} // namespace Mumble::InputEnhancement

#endif // MUMBLE_MUMBLE_INPUTENHANCEMENTCHANNELPOINTER_H_
