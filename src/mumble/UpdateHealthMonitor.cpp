// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "UpdateHealthMonitor.h"

#include "UpdateHealth.h"

#include <QTimer>

#include <algorithm>

UpdateHealthStableWindow::UpdateHealthStableWindow(const std::uint64_t requiredStableMilliseconds) noexcept
	: m_requiredStableMilliseconds(requiredStableMilliseconds) {
}

std::optional< std::uint64_t >
	UpdateHealthStableWindow::observe(const bool healthy, const std::uint64_t monotonicElapsedMilliseconds) noexcept {
	if (!healthy) {
		m_stableSinceMilliseconds.reset();
		return std::nullopt;
	}

	if (!m_stableSinceMilliseconds || monotonicElapsedMilliseconds < *m_stableSinceMilliseconds) {
		m_stableSinceMilliseconds = monotonicElapsedMilliseconds;
		return std::nullopt;
	}

	const std::uint64_t stableMilliseconds = monotonicElapsedMilliseconds - *m_stableSinceMilliseconds;
	return stableMilliseconds >= m_requiredStableMilliseconds ? std::optional< std::uint64_t >(stableMilliseconds)
															  : std::nullopt;
}

UpdateHealthMonitor *UpdateHealthMonitor::startIfPending(QObject *parent, const std::filesystem::path &updateRoot,
														 const std::filesystem::path &appPath,
														 HealthPredicate audioHealthy) {
	std::string error;
	const auto pending = Mumble::UpdateHealth::readPendingState(updateRoot, appPath, &error);
	if (!pending || pending->state != Mumble::UpdateHealth::TransactionState::AwaitingHealth) {
		return nullptr;
	}

	return new UpdateHealthMonitor(
		parent, updateRoot, appPath, std::move(audioHealthy),
		std::max(Mumble::UpdateHealth::MinimumStableRuntimeMilliseconds, pending->minimumStableRuntimeMilliseconds));
}

UpdateHealthMonitor::UpdateHealthMonitor(QObject *parent, std::filesystem::path updateRoot,
										 std::filesystem::path appPath, HealthPredicate audioHealthy,
										 const std::uint64_t requiredStableMilliseconds)
	: QObject(parent), m_updateRoot(std::move(updateRoot)), m_appPath(std::move(appPath)),
	  m_audioHealthy(std::move(audioHealthy)), m_stableWindow(requiredStableMilliseconds) {
	m_monotonicClock.start();
	m_timer = new QTimer(this);
	m_timer->setInterval(250);
	connect(m_timer, &QTimer::timeout, this, [this]() { poll(); });
	m_timer->start();
	poll();
}

void UpdateHealthMonitor::poll() {
	const qint64 elapsed          = m_monotonicClock.elapsed();
	const auto stableMilliseconds = m_stableWindow.observe(
		m_audioHealthy && m_audioHealthy(), static_cast< std::uint64_t >(std::max< qint64 >(elapsed, 0)));
	if (!stableMilliseconds) {
		return;
	}

	std::string error;
	if (Mumble::UpdateHealth::writeHealthMarker(m_updateRoot, m_appPath, *stableMilliseconds, true, true, &error)) {
		m_timer->stop();
		deleteLater();
	}
}
