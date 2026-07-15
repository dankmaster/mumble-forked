// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "InputEnhancementCalibrationWorker.h"

#include "AudioInput.h"

#include <exception>
#include <utility>

namespace Mumble::InputEnhancement {

CalibrationEvaluationWorker::~CalibrationEvaluationWorker() {
	cancel();
	if (m_thread.joinable()) {
		m_thread.join();
	}
}

bool CalibrationEvaluationWorker::start(std::shared_ptr< AudioInput > input,
										std::array< CalibrationSession::Selection, 4 > candidates) {
	if (!input || snapshot().active()) {
		return false;
	}
	joinFinished();
	CalibrationRuntimeBridge *runtime = input->inputEnhancementCalibrationRuntime();
	if (!runtime || runtime->state() != CalibrationSession::State::Evaluating) {
		return false;
	}

	setError({});
	m_cancelRequested.store(false, std::memory_order_release);
	m_progressPercent.store(0, std::memory_order_release);
	m_state.store(State::Running, std::memory_order_release);
	try {
		m_thread = std::jthread([this, input, candidates = std::move(candidates)]() mutable {
			CalibrationRuntimeBridge *jobRuntime = input->inputEnhancementCalibrationRuntime();
			if (!jobRuntime) {
				setError(QStringLiteral("The audio input closed before analysis could start."));
				input->synchronizeInputEnhancementCalibrationTransmissionBlock();
				m_state.store(State::Failed, std::memory_order_release);
				return;
			}

			CalibrationEvaluationObserver observer;
			observer.cancelRequested = &m_cancelRequested;
			observer.progress        = &CalibrationEvaluationWorker::reportProgress;
			observer.context         = this;
			bool succeeded           = false;
			try {
				succeeded = jobRuntime->evaluateCandidates(candidates, observer);
			} catch (const std::exception &exception) {
				jobRuntime->abort();
				setError(QString::fromUtf8(exception.what()));
			} catch (...) {
				jobRuntime->abort();
				setError(QStringLiteral("Local candidate analysis failed unexpectedly."));
			}

			auto finishCancelled = [this, &input, jobRuntime]() noexcept {
				if (jobRuntime->transmissionBlocked()) {
					jobRuntime->abort();
				}
				input->synchronizeInputEnhancementCalibrationTransmissionBlock();
				m_state.store(State::Cancelled, std::memory_order_release);
			};
			if (m_cancelRequested.load(std::memory_order_acquire)) {
				finishCancelled();
				return;
			}
			if (!succeeded) {
				if (snapshot().error.isEmpty()) {
					setError(QStringLiteral("No safe pair of local candidates could be prepared."));
				}
				input->synchronizeInputEnhancementCalibrationTransmissionBlock();
				State expected = State::Running;
				if (!m_state.compare_exchange_strong(expected, State::Failed, std::memory_order_acq_rel,
													 std::memory_order_acquire)) {
					finishCancelled();
				}
				return;
			}
			State expected = State::Running;
			if (!m_state.compare_exchange_strong(expected, State::Succeeded, std::memory_order_acq_rel,
												 std::memory_order_acquire)) {
				finishCancelled();
				return;
			}
			m_progressPercent.store(100, std::memory_order_release);
		});
	} catch (const std::exception &exception) {
		runtime->abort();
		input->synchronizeInputEnhancementCalibrationTransmissionBlock();
		setError(QString::fromUtf8(exception.what()));
		m_state.store(State::Failed, std::memory_order_release);
		return false;
	} catch (...) {
		runtime->abort();
		input->synchronizeInputEnhancementCalibrationTransmissionBlock();
		setError(QStringLiteral("The local analysis worker could not be started."));
		m_state.store(State::Failed, std::memory_order_release);
		return false;
	}
	return true;
}

bool CalibrationEvaluationWorker::cancel() noexcept {
	const State current = m_state.load(std::memory_order_acquire);
	if (current != State::Running && current != State::Cancelling) {
		return false;
	}
	m_cancelRequested.store(true, std::memory_order_release);
	m_state.store(State::Cancelling, std::memory_order_release);
	return true;
}

void CalibrationEvaluationWorker::reset() {
	cancel();
	if (m_thread.joinable()) {
		m_thread.join();
	}
	setError({});
	m_cancelRequested.store(false, std::memory_order_release);
	m_progressPercent.store(0, std::memory_order_release);
	m_state.store(State::Idle, std::memory_order_release);
}

CalibrationEvaluationWorker::Snapshot CalibrationEvaluationWorker::snapshot() const {
	Snapshot value;
	value.state           = m_state.load(std::memory_order_acquire);
	value.progressPercent = m_progressPercent.load(std::memory_order_acquire);
	{
		const std::lock_guard lock(m_errorMutex);
		value.error = m_error;
	}
	return value;
}

void CalibrationEvaluationWorker::reportProgress(void *context, const std::size_t completed,
												 const std::size_t total) noexcept {
	auto *worker = static_cast< CalibrationEvaluationWorker * >(context);
	if (!worker || total == 0) {
		return;
	}
	const int percent = static_cast< int >((completed * 100U) / total);
	worker->m_progressPercent.store(percent, std::memory_order_release);
}

void CalibrationEvaluationWorker::setError(QString error) {
	const std::lock_guard lock(m_errorMutex);
	m_error = std::move(error);
}

void CalibrationEvaluationWorker::joinFinished() {
	if (!m_thread.joinable()) {
		return;
	}
	const State current = m_state.load(std::memory_order_acquire);
	if (current != State::Running && current != State::Cancelling) {
		m_thread.join();
	}
}

} // namespace Mumble::InputEnhancement
