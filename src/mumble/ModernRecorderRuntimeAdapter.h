// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_MODERNRECORDERRUNTIMEADAPTER_H_
#define MUMBLE_MUMBLE_MODERNRECORDERRUNTIMEADAPTER_H_

#include "ModernRecorderController.h"

#include <QtCore/QHash>
#include <QtCore/QObject>

#include <memory>

class ServerHandler;

namespace Mumble {

/// Production composition adapter for the existing VoiceRecorder backend.
/// Each session retains the ServerHandler on which it was created, preventing
/// a paused recorder from being attached to a replacement connection.
class ModernRecorderRuntimeAdapter final : public QObject, public ModernRecorderRuntime {
	Q_OBJECT

public:
	explicit ModernRecorderRuntimeAdapter(QObject *parent = nullptr);

	bool transportSupported() const override;
	QVariantList formatOptions() const override;
	QString defaultExtension(int format) const override;
	ModernRecorderRuntimeResult preflight(const ModernRecorderConfiguration &configuration) const override;
	ModernRecorderSession *createSession(const ModernRecorderConfiguration &configuration,
										 QObject *parent, ModernRecorderRuntimeResult *result) override;
	ModernRecorderRuntimeResult attach(ModernRecorderSession *session) override;
	ModernRecorderRuntimeResult detach(ModernRecorderSession *session) override;
	void announceRecordingState(ModernRecorderSession *session, bool recording) override;
	void persistConfiguration(const ModernRecorderConfiguration &configuration) override;

private:
	std::shared_ptr< ServerHandler > retainedHandler(ModernRecorderSession *session) const;
	ModernRecorderRuntimeResult validateSession(ModernRecorderSession *session) const;

	QHash< ModernRecorderSession *, std::shared_ptr< ServerHandler > > m_sessionHandlers;
};

} // namespace Mumble

#endif // MUMBLE_MUMBLE_MODERNRECORDERRUNTIMEADAPTER_H_
