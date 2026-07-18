// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "VoiceRecorderSessionAdapter.h"

#include <QtCore/QMetaObject>

namespace Mumble {

VoiceRecorderSessionAdapter::VoiceRecorderSessionAdapter(const VoiceRecorder::Config &config, QObject *parent)
	: ModernRecorderSession(parent), m_recorder(std::make_shared< VoiceRecorder >(nullptr, config)) {
	VoiceRecorder *recorder = m_recorder.get();
	connect(recorder, &VoiceRecorder::recording_started, this, [this]() {
		QMetaObject::invokeMethod(this, [this]() { emit started(); }, Qt::QueuedConnection);
	}, Qt::DirectConnection);
	connect(recorder, &VoiceRecorder::error, this, [this](const int error, const QString &message) {
		QMetaObject::invokeMethod(this, [this, error, message]() {
			emit failed(QStringLiteral("voice_recorder_%1").arg(error), message);
		}, Qt::QueuedConnection);
	}, Qt::DirectConnection);
	connect(recorder, &VoiceRecorder::recording_stopped, this, [this]() {
		QMetaObject::invokeMethod(this, [this]() {
			if (m_terminalSignalDelivered) return;
			m_terminalSignalDelivered = true;
			emit stopped();
		}, Qt::QueuedConnection);
	}, Qt::DirectConnection);
	connect(recorder, &QThread::finished, this, [this]() {
		if (m_terminalSignalDelivered) return;
		m_terminalSignalDelivered = true;
		emit stopped();
	});
}

VoiceRecorderSessionAdapter::~VoiceRecorderSessionAdapter() {
	if (!m_recorder) return;
	m_recorder->stop(true);
	m_recorder->wait();
}

void VoiceRecorderSessionAdapter::start() { m_recorder->start(); }
void VoiceRecorderSessionAdapter::stop(const bool force) { m_recorder->stop(force); }
quint64 VoiceRecorderSessionAdapter::elapsedMicroseconds() const { return m_recorder->getElapsedTime(); }
VoiceRecorderPtr VoiceRecorderSessionAdapter::recorder() const { return m_recorder; }

} // namespace Mumble
