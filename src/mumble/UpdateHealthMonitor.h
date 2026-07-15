// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#pragma once

#include <QElapsedTimer>
#include <QObject>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>

class QTimer;

/// Tracks one continuous healthy interval using elapsed time from a monotonic
/// clock. Supplying elapsed time explicitly keeps the safety property unit
/// testable without changing the machine's wall clock.
class UpdateHealthStableWindow final {
public:
	explicit UpdateHealthStableWindow(std::uint64_t requiredStableMilliseconds) noexcept;

	/// Returns the continuous healthy duration once the required interval has
	/// been reached. An unhealthy observation, or an impossible backwards
	/// monotonic reading, starts a new interval.
	std::optional< std::uint64_t > observe(bool healthy, std::uint64_t monotonicElapsedMilliseconds) noexcept;

private:
	std::uint64_t m_requiredStableMilliseconds = 0;
	std::optional< std::uint64_t > m_stableSinceMilliseconds;
};

/// Arms the update health marker only after client settings have loaded and
/// the audio system has remained initialized for a continuous ten seconds.
class UpdateHealthMonitor final : public QObject {
public:
	using HealthPredicate = std::function< bool() >;

	static UpdateHealthMonitor *startIfPending(QObject *parent, const std::filesystem::path &updateRoot,
											   const std::filesystem::path &appPath, HealthPredicate audioHealthy);

private:
	UpdateHealthMonitor(QObject *parent, std::filesystem::path updateRoot, std::filesystem::path appPath,
						HealthPredicate audioHealthy, std::uint64_t requiredStableMilliseconds);

	void poll();

	std::filesystem::path m_updateRoot;
	std::filesystem::path m_appPath;
	HealthPredicate m_audioHealthy;
	QTimer *m_timer = nullptr;
	QElapsedTimer m_monotonicClock;
	UpdateHealthStableWindow m_stableWindow;
};
