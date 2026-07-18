// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_INPUTENHANCEMENTCALIBRATIONWORKER_H_
#define MUMBLE_MUMBLE_INPUTENHANCEMENTCALIBRATIONWORKER_H_

#include "InputEnhancementCalibrationRuntime.h"

#include <QtCore/QString>

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

class AudioInput;

namespace Mumble::InputEnhancement {

/// Runs the CPU/model/Opus portion of calibration away from the UI and audio
/// callback threads. The shared AudioInput ownership is held only by the job,
/// preventing its CalibrationRuntimeBridge from disappearing mid-evaluation.
class CalibrationEvaluationWorker final {
public:
	enum class State : std::uint8_t { Idle, Running, Cancelling, Succeeded, Failed, Cancelled };

	struct Snapshot final {
		State state         = State::Idle;
		int progressPercent = 0;
		QString error;

		bool active() const noexcept { return state == State::Running || state == State::Cancelling; }
	};

	CalibrationEvaluationWorker() = default;
	~CalibrationEvaluationWorker();

	CalibrationEvaluationWorker(const CalibrationEvaluationWorker &)            = delete;
	CalibrationEvaluationWorker(CalibrationEvaluationWorker &&)                 = delete;
	CalibrationEvaluationWorker &operator=(const CalibrationEvaluationWorker &) = delete;
	CalibrationEvaluationWorker &operator=(CalibrationEvaluationWorker &&)      = delete;

	bool start(std::shared_ptr< AudioInput > input, std::array< CalibrationSession::Selection, 4 > candidates);
	bool cancel() noexcept;
	void reset();
	Snapshot snapshot() const;

private:
	static void reportProgress(void *context, std::size_t completed, std::size_t total) noexcept;
	void setError(QString error);
	void joinFinished();

	std::jthread m_thread;
	std::atomic< State > m_state{ State::Idle };
	std::atomic_int m_progressPercent{ 0 };
	std::atomic_bool m_cancelRequested{ false };
	mutable std::mutex m_errorMutex;
	QString m_error;
};

} // namespace Mumble::InputEnhancement

#endif // MUMBLE_MUMBLE_INPUTENHANCEMENTCALIBRATIONWORKER_H_
