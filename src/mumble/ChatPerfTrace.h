// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_CHATPERFTRACE_H_
#define MUMBLE_MUMBLE_CHATPERFTRACE_H_

namespace mumble {
namespace chatperf {
	// Small always-on correctness monitor. Production tracing may be compiled out, but tests and
	// debug gates still need to detect accidental full frontend bootstraps after synchronization.
	class FullBootstrapMonitor {
	public:
		void enterSteadyState() { m_steadyState = true; }
		void leaveSteadyState() { m_steadyState = false; }
		bool recordBootstrap() {
			if (!m_steadyState) return false;
			++m_steadyStateViolations;
			return true;
		}
		bool isSteadyState() const { return m_steadyState; }
		unsigned int steadyStateViolations() const { return m_steadyStateViolations; }

	private:
		bool m_steadyState = false;
		unsigned int m_steadyStateViolations = 0;
	};

	inline FullBootstrapMonitor &fullBootstrapMonitor() {
		static FullBootstrapMonitor monitor;
		return monitor;
	}
} // namespace chatperf
} // namespace mumble

// The chat performance tracer is a developer-only facility. It is compiled into
// local dev clients (CMake option `chat-perf-trace`, defined as
// MUMBLE_HAS_CHAT_PERF_TRACE) but deliberately kept out of GitHub/CI packaging
// builds. When the macro is absent every entry point below collapses to a
// zero-cost no-op so call sites compile unchanged and produce no code.
#if defined(MUMBLE_HAS_CHAT_PERF_TRACE)

#include <QtCore/QDebug>
#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QtGlobal>

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mumble {
namespace chatperf {
	namespace detail {
		struct TimingStats {
			quint64 count = 0;
			qint64 totalNs = 0;
			qint64 maxNs = 0;
		};

		struct ValueStats {
			quint64 count = 0;
			qint64 total = 0;
			qint64 max = std::numeric_limits< qint64 >::lowest();
		};

		inline bool enabled() {
			static const bool kEnabled = qEnvironmentVariableIntValue("MUMBLE_CHAT_PERF_TRACE") > 0;
			return kEnabled;
		}

		inline QElapsedTimer &sessionTimer() {
			static QElapsedTimer timer;
			static const bool started = []() {
				timer.start();
				return true;
			}();
			Q_UNUSED(started);
			return timer;
		}

		inline std::mutex &traceMutex() {
			static std::mutex mutex;
			return mutex;
		}

		inline std::unordered_map< std::string, TimingStats > &timingStats() {
			static std::unordered_map< std::string, TimingStats > stats;
			return stats;
		}

		inline std::unordered_map< std::string, ValueStats > &valueStats() {
			static std::unordered_map< std::string, ValueStats > stats;
			return stats;
		}

		inline qint64 &lastFlushMs() {
			static qint64 value = 0;
			return value;
		}

		inline quint64 &pendingRecordCount() {
			static quint64 value = 0;
			return value;
		}

		inline QString logPath() {
			static const QString path = []() {
				const QString configuredPath = qEnvironmentVariable("MUMBLE_CHAT_PERF_TRACE_PATH");
				return configuredPath.isEmpty() ? QDir(QDir::tempPath()).filePath(QLatin1String("mumble-chat-perf.log"))
											   : configuredPath;
			}();
			return path;
		}

		class AsyncTraceWriter final {
		public:
			AsyncTraceWriter() : m_worker([this]() { run(); }) {}

			~AsyncTraceWriter() {
				{
					std::lock_guard< std::mutex > lock(m_mutex);
					m_stopping = true;
				}
				m_ready.notify_one();
				if (m_worker.joinable()) {
					m_worker.join();
				}
			}

			void enqueue(const QString &path, const QString &line) {
				enqueue(path, line.toUtf8());
			}

			void enqueue(const QString &path, QByteArray bytes) {
				PendingItem pending;
				pending.path  = path;
				pending.bytes = std::move(bytes);
				if (!pending.bytes.endsWith('\n')) {
					pending.bytes.append('\n');
				}
				{
					std::lock_guard< std::mutex > lock(m_mutex);
					m_pending.push_back(std::move(pending));
				}
				m_ready.notify_one();
			}

			void enqueueSnapshot(const QString &path, qint64 windowMs,
							 std::unordered_map< std::string, TimingStats > timings,
							 std::unordered_map< std::string, ValueStats > values) {
				PendingItem pending;
				pending.path = path;
				pending.windowMs = windowMs;
				pending.timings = std::move(timings);
				pending.values = std::move(values);
				pending.isSnapshot = true;
				{
					std::lock_guard< std::mutex > lock(m_mutex);
					m_pending.push_back(std::move(pending));
				}
				m_ready.notify_one();
			}

		private:
			struct PendingItem {
				QString path;
				QByteArray bytes;
				qint64 windowMs = 0;
				std::unordered_map< std::string, TimingStats > timings;
				std::unordered_map< std::string, ValueStats > values;
				bool isSnapshot = false;
			};

			static QByteArray formatSnapshot(PendingItem &snapshot) {
				struct SortedTimingEntry {
					std::string name;
					TimingStats stats;
				};
				struct SortedValueEntry {
					std::string name;
					ValueStats stats;
				};

				std::vector< SortedTimingEntry > sortedTimings;
				sortedTimings.reserve(snapshot.timings.size());
				for (const auto &entry : snapshot.timings) {
					sortedTimings.push_back(SortedTimingEntry { entry.first, entry.second });
				}
				std::sort(sortedTimings.begin(), sortedTimings.end(),
						  [](const SortedTimingEntry &lhs, const SortedTimingEntry &rhs) {
							  return lhs.stats.totalNs > rhs.stats.totalNs;
						  });

				std::vector< SortedValueEntry > sortedValues;
				sortedValues.reserve(snapshot.values.size());
				for (const auto &entry : snapshot.values) {
					sortedValues.push_back(SortedValueEntry { entry.first, entry.second });
				}
				std::sort(sortedValues.begin(), sortedValues.end(),
						  [](const SortedValueEntry &lhs, const SortedValueEntry &rhs) {
							  return lhs.stats.total > rhs.stats.total;
						  });

				QByteArray bytes;
				auto appendLine = [&bytes](const QString &line) {
					bytes.append(line.toUtf8());
					bytes.append('\n');
				};
				appendLine(QString::fromLatin1("[chat-perf] window_ms=%1 timing_entries=%2 value_entries=%3")
							   .arg(snapshot.windowMs)
							   .arg(sortedTimings.size())
							   .arg(sortedValues.size()));

				const std::size_t maxTimingEntries = std::min< std::size_t >(sortedTimings.size(), 12);
				for (std::size_t index = 0; index < maxTimingEntries; ++index) {
					const SortedTimingEntry &entry = sortedTimings[index];
					const double totalMs = static_cast< double >(entry.stats.totalNs) / 1000000.0;
					const double averageMs = entry.stats.count > 0
						? totalMs / static_cast< double >(entry.stats.count)
						: 0.0;
					const double maximumMs = static_cast< double >(entry.stats.maxNs) / 1000000.0;
					appendLine(
						QString::fromLatin1("[chat-perf][timing] %1 count=%2 total_ms=%3 avg_ms=%4 max_ms=%5")
							.arg(QString::fromStdString(entry.name))
							.arg(entry.stats.count)
							.arg(totalMs, 0, 'f', 3)
							.arg(averageMs, 0, 'f', 3)
							.arg(maximumMs, 0, 'f', 3));
				}

				const std::size_t maxValueEntries = std::min< std::size_t >(sortedValues.size(), 12);
				for (std::size_t index = 0; index < maxValueEntries; ++index) {
					const SortedValueEntry &entry = sortedValues[index];
					const double averageValue = entry.stats.count > 0
						? static_cast< double >(entry.stats.total) / static_cast< double >(entry.stats.count)
						: 0.0;
					appendLine(QString::fromLatin1("[chat-perf][value] %1 count=%2 total=%3 avg=%4 max=%5")
							   .arg(QString::fromStdString(entry.name))
							   .arg(entry.stats.count)
							   .arg(entry.stats.total)
							   .arg(averageValue, 0, 'f', 2)
							   .arg(entry.stats.max));
				}
				return bytes;
			}

			void run() {
				for (;;) {
					std::deque< PendingItem > pending;
					{
						std::unique_lock< std::mutex > lock(m_mutex);
						m_ready.wait(lock, [this]() { return m_stopping || !m_pending.empty(); });
						if (m_stopping && m_pending.empty()) {
							return;
						}
						pending.swap(m_pending);
					}

					while (!pending.empty()) {
						const QString path = pending.front().path;
						QByteArray bytes;
						do {
							PendingItem item = std::move(pending.front());
							pending.pop_front();
							bytes.append(item.isSnapshot ? formatSnapshot(item) : item.bytes);
						} while (!pending.empty() && pending.front().path == path);

						QFile file(path);
						if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
							file.write(bytes);
							file.close();
						}
					}
				}
			}

			std::mutex m_mutex;
			std::condition_variable m_ready;
			std::deque< PendingItem > m_pending;
			bool m_stopping = false;
			std::thread m_worker;
		};

		inline AsyncTraceWriter &traceWriter() {
			static AsyncTraceWriter writer;
			return writer;
		}

		inline void appendLineLocked(const QString &line) {
			// Trace call sites run predominantly on the GUI thread. File creation,
			// writes and flushes must never become part of the measured UI workload.
			traceWriter().enqueue(logPath(), line);
		}

		inline void flushLocked(qint64 elapsedMs) {
			const qint64 windowMs = elapsedMs - lastFlushMs();
			if (windowMs < 1000 && pendingRecordCount() < 200) {
				return;
			}

			if (timingStats().empty() && valueStats().empty()) {
				lastFlushMs() = elapsedMs;
				pendingRecordCount() = 0;
				return;
			}

			std::unordered_map< std::string, TimingStats > timings;
			std::unordered_map< std::string, ValueStats > values;
			timings.swap(timingStats());
			values.swap(valueStats());
			lastFlushMs() = elapsedMs;
			pendingRecordCount() = 0;
			// Sorting, formatting and file access all happen on the writer thread. The
			// measured caller only swaps two small maps and enqueues the immutable window.
			traceWriter().enqueueSnapshot(logPath(), windowMs, std::move(timings), std::move(values));
		}
	} // namespace detail

	inline bool enabled() {
		return detail::enabled();
	}

	inline void appendFileLineAsync(const QString &path, QByteArray line) {
		// This helper is also used by opt-in connection diagnostics. Keeping all
		// diagnostic file access behind the same worker prevents an enabled trace
		// from turning a GUI-thread code path into synchronous file I/O.
		detail::traceWriter().enqueue(path, std::move(line));
	}

	inline void recordDuration(const char *name, qint64 elapsedNs) {
		if (!detail::enabled()) {
			return;
		}

		std::lock_guard< std::mutex > lock(detail::traceMutex());
		detail::TimingStats &stats = detail::timingStats()[name];
		++stats.count;
		stats.totalNs += elapsedNs;
		stats.maxNs = std::max(stats.maxNs, elapsedNs);
		++detail::pendingRecordCount();
		detail::flushLocked(detail::sessionTimer().elapsed());
	}

	inline void recordValue(const char *name, qint64 value) {
		if (!detail::enabled()) {
			return;
		}

		std::lock_guard< std::mutex > lock(detail::traceMutex());
		detail::ValueStats &stats = detail::valueStats()[name];
		++stats.count;
		stats.total += value;
		stats.max = std::max(stats.max, value);
		++detail::pendingRecordCount();
		detail::flushLocked(detail::sessionTimer().elapsed());
	}

	inline void recordNote(const char *name, const QString &value) {
		if (!detail::enabled()) {
			return;
		}

		QString sanitized = value;
		sanitized.replace(QLatin1Char('\r'), QLatin1Char(' '));
		sanitized.replace(QLatin1Char('\n'), QLatin1Char(' '));
		if (sanitized.size() > 512) {
			sanitized = sanitized.left(512);
		}

		std::lock_guard< std::mutex > lock(detail::traceMutex());
		detail::appendLineLocked(QString::fromLatin1("[chat-perf][note] t_ms=%1 %2 %3")
									 .arg(detail::sessionTimer().elapsed())
									 .arg(QString::fromLatin1(name))
									 .arg(sanitized));
	}

	class ScopedDuration {
	public:
		explicit ScopedDuration(const char *name) : m_name(name), m_enabled(detail::enabled()) {
			if (m_enabled) {
				m_timer.start();
			}
		}

		~ScopedDuration() {
			if (m_enabled) {
				recordDuration(m_name, m_timer.nsecsElapsed());
			}
		}

	private:
		const char *m_name;
		bool m_enabled = false;
		QElapsedTimer m_timer;
	};
} // namespace chatperf
} // namespace mumble

#else // MUMBLE_HAS_CHAT_PERF_TRACE

#include <QtCore/QByteArray>
#include <QtCore/QString>
#include <QtCore/QtGlobal>

// No-op fallback used by GitHub/CI builds (and any build without the
// chat-perf-trace option). Every function is an empty inline definition and
// enabled() is constexpr false, so the optimizer eliminates guarded blocks and
// argument evaluation entirely. ScopedDuration keeps a user-provided constructor
// so MSVC does not flag the RAII locals as unreferenced under /WX.
namespace mumble {
namespace chatperf {
	constexpr bool enabled() {
		return false;
	}

	inline void recordDuration(const char *, qint64) {}
	inline void recordValue(const char *, qint64) {}
	inline void recordNote(const char *, const QString &) {}
	inline void appendFileLineAsync(const QString &, QByteArray) {}

	class ScopedDuration {
	public:
		explicit ScopedDuration(const char *) {}
	};
} // namespace chatperf
} // namespace mumble

#endif // MUMBLE_HAS_CHAT_PERF_TRACE

#endif // MUMBLE_MUMBLE_CHATPERFTRACE_H_
