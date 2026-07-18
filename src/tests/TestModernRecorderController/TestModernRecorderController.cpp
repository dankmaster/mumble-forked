// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ModernRecorderController.h"

#include <QtCore/QDir>
#include <QtCore/QPointer>
#include <QtTest/QSignalSpy>
#include <QtTest/QtTest>

namespace {
	class FakeRecorderSession final : public Mumble::ModernRecorderSession {
		Q_OBJECT

	public:
		using ModernRecorderSession::ModernRecorderSession;

		void start() override {
			++startCalls;
			if (stopInsteadOfStart) emit stopped();
			else emit started();
		}

		void stop(const bool force) override {
			++stopCalls;
			lastForce = force;
			emit stopped();
		}

		quint64 elapsedMicroseconds() const override { return elapsed; }
		void advance(const quint64 microseconds) { elapsed += microseconds; }
		void backendFailure(const QString &code, const QString &message) { emit failed(code, message); }
		void unexpectedStop() { emit stopped(); }

		quint64 elapsed = 0;
		int startCalls = 0;
		int stopCalls = 0;
		bool lastForce = false;
		bool stopInsteadOfStart = false;
	};

	class FakeRecorderRuntime final : public Mumble::ModernRecorderRuntime {
	public:
		bool transportSupported() const override { return supportsTransport; }
		QVariantList formatOptions() const override {
			return { QVariantMap { { QStringLiteral("label"), QStringLiteral(".wav - Uncompressed") },
								 { QStringLiteral("value"), 0 }, { QStringLiteral("enabled"), true } },
				QVariantMap { { QStringLiteral("label"), QStringLiteral(".flac - Lossless") },
								 { QStringLiteral("value"), 1 }, { QStringLiteral("enabled"), true } } };
		}
		QString defaultExtension(const int format) const override {
			return format == 1 ? QStringLiteral("flac") : QStringLiteral("wav");
		}
		Mumble::ModernRecorderRuntimeResult preflight(
			const Mumble::ModernRecorderConfiguration &configuration) const override {
			lastPreflightConfiguration = configuration;
			return preflightResult;
		}
		Mumble::ModernRecorderSession *createSession(const Mumble::ModernRecorderConfiguration &configuration,
											 QObject *parent, Mumble::ModernRecorderRuntimeResult *result) override {
			lastCreatedConfiguration = configuration;
			if (!createResult.success) {
				*result = createResult;
				return nullptr;
			}
			auto *created = new FakeRecorderSession(parent);
			created->stopInsteadOfStart = stopInsteadOfStart;
			session = created;
			*result = {};
			return created;
		}
		Mumble::ModernRecorderRuntimeResult attach(Mumble::ModernRecorderSession *candidate) override {
			++attachCalls;
			lastAttached = candidate;
			return attachResult;
		}
		Mumble::ModernRecorderRuntimeResult detach(Mumble::ModernRecorderSession *candidate) override {
			++detachCalls;
			lastDetached = candidate;
			return detachResult;
		}
		void announceRecordingState(Mumble::ModernRecorderSession *candidate, const bool recording) override {
			announcementSessions.push_back(candidate);
			announcements.push_back(recording);
		}
		void persistConfiguration(const Mumble::ModernRecorderConfiguration &configuration) override {
			++persistCalls;
			lastPersistedConfiguration = configuration;
		}

		bool supportsTransport = false;
		bool stopInsteadOfStart = false;
		Mumble::ModernRecorderRuntimeResult preflightResult;
		Mumble::ModernRecorderRuntimeResult createResult;
		Mumble::ModernRecorderRuntimeResult attachResult;
		Mumble::ModernRecorderRuntimeResult detachResult;
		mutable Mumble::ModernRecorderConfiguration lastPreflightConfiguration;
		Mumble::ModernRecorderConfiguration lastCreatedConfiguration;
		Mumble::ModernRecorderConfiguration lastPersistedConfiguration;
		QPointer< FakeRecorderSession > session;
		Mumble::ModernRecorderSession *lastAttached = nullptr;
		Mumble::ModernRecorderSession *lastDetached = nullptr;
		QList< bool > announcements;
		QList< Mumble::ModernRecorderSession * > announcementSessions;
		int attachCalls = 0;
		int detachCalls = 0;
		int persistCalls = 0;
	};
} // namespace

class TestModernRecorderController : public QObject {
	Q_OBJECT

private slots:
	void validatesConfigurationAndPublishesTypedOptions();
	void startPauseResumeStopKeepsLiveElapsedIncremental();
	void runtimeErrorsAreStableAndRecoverable();
	void staleOrUnexpectedBackendStopCannotMasqueradeAsSuccess();
	void visualFixtureStateUsesTypedCapabilitiesWithoutStartingBackend();
};

void TestModernRecorderController::validatesConfigurationAndPublishesTypedOptions() {
	FakeRecorderRuntime runtime;
	Mumble::ModernRecorderController controller;
	controller.setRuntime(&runtime);
	QCOMPARE(controller.state(), QStringLiteral("idle"));
	QVERIFY(controller.canStart());
	QCOMPARE(controller.formatOptions().size(), 2);
	QCOMPARE(controller.modeOptions().size(), 4);
	QVERIFY(!controller.modeOptions().at(Mumble::ModernRecorderController::TransportOnly)
			.toMap().value(QStringLiteral("enabled")).toBool());

	controller.setMode(Mumble::ModernRecorderController::TransportOnly);
	QCOMPARE(controller.mode(), static_cast< int >(Mumble::ModernRecorderController::Mixdown));
	QVERIFY(!controller.start());
	QCOMPARE(controller.state(), QStringLiteral("error"));
	QCOMPARE(controller.operationStatus(), QStringLiteral("failed"));
	QCOMPARE(controller.errorCode(), QStringLiteral("invalid_configuration"));
	QVERIFY(controller.fieldErrors().contains(QStringLiteral("recording.path")));
	QVERIFY(controller.canStart()); // Error is editable and retryable.

	controller.clearError();
	QCOMPARE(controller.state(), QStringLiteral("idle"));
	controller.setOutputDirectory(QStringLiteral("C:/Recordings"));
	controller.setFileName(QStringLiteral("session.wav"));
	QCOMPARE(QDir::fromNativeSeparators(controller.resolvedOutputPath()),
			 QStringLiteral("C:/Recordings/session.wav"));

	runtime.supportsTransport = true;
	controller.refreshCapabilities();
	controller.setMode(Mumble::ModernRecorderController::TransportOnly);
	QCOMPARE(controller.mode(), static_cast< int >(Mumble::ModernRecorderController::TransportOnly));
	QVERIFY(controller.modeOptions().at(Mumble::ModernRecorderController::TransportOnly)
			.toMap().value(QStringLiteral("enabled")).toBool());
}

void TestModernRecorderController::startPauseResumeStopKeepsLiveElapsedIncremental() {
	FakeRecorderRuntime runtime;
	runtime.supportsTransport = true;
	Mumble::ModernRecorderController controller;
	controller.setRuntime(&runtime);
	controller.setInitialConfiguration(QStringLiteral("C:/Recordings"), QStringLiteral("%user"),
		0, Mumble::ModernRecorderController::MultichannelAndTransport);
	QSignalSpy elapsedSpy(&controller, &Mumble::ModernRecorderController::elapsedChanged);
	QSignalSpy operationSpy(&controller, &Mumble::ModernRecorderController::operationFinished);

	QVERIFY(controller.start());
	QCOMPARE(controller.state(), QStringLiteral("recording"));
	QCOMPARE(controller.operationStatus(), QStringLiteral("succeeded"));
	QCOMPARE(controller.operationAction(), QStringLiteral("start"));
	QCOMPARE(runtime.attachCalls, 1);
	QCOMPARE(runtime.persistCalls, 1);
	QCOMPARE(runtime.announcements, QList< bool > { true });
	QCOMPARE(runtime.announcementSessions, QList< Mumble::ModernRecorderSession * > { runtime.session.data() });
	QVERIFY(runtime.lastPersistedConfiguration.transportEnabled);
	QVERIFY(!runtime.lastPersistedConfiguration.mixDown);
	QVERIFY(runtime.session);

	runtime.session->advance(3500000);
	controller.refreshElapsed();
	QCOMPARE(controller.elapsedMilliseconds(), 3500);
	QCOMPARE(controller.elapsedText(), QStringLiteral("00:00:03"));
	QCOMPARE(elapsedSpy.size(), 1);

	QVERIFY(controller.pause());
	QCOMPARE(controller.state(), QStringLiteral("paused"));
	QCOMPARE(runtime.detachCalls, 1);
	QCOMPARE(runtime.announcements, (QList< bool > { true, false }));
	runtime.session->advance(10000000);
	controller.refreshElapsed();
	QCOMPARE(controller.elapsedMilliseconds(), 3500);
	QCOMPARE(elapsedSpy.size(), 1);

	QVERIFY(controller.resume());
	QCOMPARE(controller.state(), QStringLiteral("recording"));
	QCOMPARE(runtime.attachCalls, 2);
	QCOMPARE(runtime.announcements, (QList< bool > { true, false, true }));
	runtime.session->advance(2000000);
	controller.refreshElapsed();
	QCOMPARE(controller.elapsedMilliseconds(), 5500);
	QCOMPARE(elapsedSpy.size(), 2);

	QVERIFY(controller.stop());
	QCOMPARE(controller.state(), QStringLiteral("idle"));
	QCOMPARE(controller.operationStatus(), QStringLiteral("succeeded"));
	QCOMPARE(runtime.detachCalls, 2);
	QCOMPARE(runtime.announcements, (QList< bool > { true, false, true, false }));
	QCOMPARE(operationSpy.size(), 4);
	QVERIFY(controller.canStart());
}

void TestModernRecorderController::visualFixtureStateUsesTypedCapabilitiesWithoutStartingBackend() {
	FakeRecorderRuntime runtime;
	Mumble::ModernRecorderController controller;
	controller.setRuntime(&runtime);
	QVERIFY(controller.applyVisualFixtureState(QStringLiteral("recording"), 872000,
		QStringLiteral("C:/Recordings"), QStringLiteral("community"), 1,
		Mumble::ModernRecorderController::Mixdown, true));
	QCOMPARE(controller.state(), QStringLiteral("recording"));
	QCOMPARE(controller.elapsedText(), QStringLiteral("00:14:32"));
	QVERIFY(!controller.canEdit());
	QVERIFY(controller.canPause());
	QVERIFY(controller.canStop());
	QVERIFY(controller.transportSupported());
	QVERIFY(!controller.pause());
	QCOMPARE(runtime.attachCalls, 0);
	QCOMPARE(runtime.detachCalls, 0);

	controller.clearVisualFixtureState();
	QCOMPARE(controller.state(), QStringLiteral("idle"));
	QCOMPARE(controller.elapsedMilliseconds(), 0);
	QVERIFY(controller.canEdit());
	QVERIFY(controller.canStart());
}

void TestModernRecorderController::runtimeErrorsAreStableAndRecoverable() {
	FakeRecorderRuntime runtime;
	runtime.preflightResult = Mumble::ModernRecorderRuntimeResult::failure(
		QStringLiteral("not_connected"), QStringLiteral("Connect to a server first."));
	Mumble::ModernRecorderController controller;
	controller.setRuntime(&runtime);
	controller.setInitialConfiguration(QStringLiteral("C:/Recordings"), QStringLiteral("session"),
		0, Mumble::ModernRecorderController::Mixdown);

	QVERIFY(!controller.start());
	QCOMPARE(controller.state(), QStringLiteral("error"));
	QCOMPARE(controller.errorCode(), QStringLiteral("not_connected"));
	QCOMPARE(controller.errorMessage(), QStringLiteral("Connect to a server first."));
	QCOMPARE(controller.operationAction(), QStringLiteral("start"));
	QCOMPARE(controller.operationStatus(), QStringLiteral("failed"));

	runtime.preflightResult = {};
	controller.clearError();
	QVERIFY(controller.start());
	QCOMPARE(controller.state(), QStringLiteral("recording"));
	QVERIFY(runtime.session);
	runtime.session->backendFailure(QStringLiteral("disk_full"), QStringLiteral("The disk is full."));
	QCOMPARE(controller.state(), QStringLiteral("error"));
	QCOMPARE(controller.errorCode(), QStringLiteral("disk_full"));
	QCOMPARE(controller.errorMessage(), QStringLiteral("The disk is full."));
	QVERIFY(controller.canStop());
	QVERIFY(controller.stop());
	QCOMPARE(controller.state(), QStringLiteral("idle"));
}

void TestModernRecorderController::staleOrUnexpectedBackendStopCannotMasqueradeAsSuccess() {
	FakeRecorderRuntime runtime;
	Mumble::ModernRecorderController controller;
	controller.setRuntime(&runtime);
	controller.setInitialConfiguration(QStringLiteral("C:/Recordings"), QStringLiteral("session"),
		0, Mumble::ModernRecorderController::Mixdown);
	QVERIFY(controller.start());
	QPointer< FakeRecorderSession > first = runtime.session;
	QVERIFY(first);
	first->unexpectedStop();
	QCOMPARE(controller.state(), QStringLiteral("error"));
	QCOMPARE(controller.errorCode(), QStringLiteral("backend_stopped"));
	QCOMPARE(controller.operationStatus(), QStringLiteral("failed"));
	QCOMPARE(controller.operationAction(), QStringLiteral("backend"));
	QVERIFY(controller.canStart());

	controller.clearError();
	QVERIFY(controller.start());
	QCOMPARE(controller.state(), QStringLiteral("recording"));
	if (first) first->backendFailure(QStringLiteral("stale"), QStringLiteral("Stale session"));
	QCOMPARE(controller.state(), QStringLiteral("recording"));
	QVERIFY(controller.stop());
}

QTEST_MAIN(TestModernRecorderController)
#include "TestModernRecorderController.moc"
