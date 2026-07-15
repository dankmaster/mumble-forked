// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "UpdateHealth.h"
#include "UpdateHealthMonitor.h"

#include <QTemporaryDir>
#include <QtTest>

#include <filesystem>

namespace {

std::filesystem::path filesystemPath(const QString &path) {
	const QByteArray encoded = path.toUtf8();
	const auto *begin        = reinterpret_cast< const char8_t * >(encoded.constData());
	return std::filesystem::path(std::u8string(begin, begin + encoded.size()));
}

Mumble::UpdateHealth::PendingUpdate pendingFor(const std::filesystem::path &root) {
	Mumble::UpdateHealth::PendingUpdate pending;
	pending.transactionId          = "00112233445566778899aabbccddeeff";
	pending.state                  = Mumble::UpdateHealth::TransactionState::AwaitingHealth;
	pending.packageIdentity         = "0123456789abcdef";
	pending.previousPackageIdentity = "previous";
	pending.appPath                 = root / "Application" / "mumble.exe";
	pending.backupRoot              = root / "known-good" / "snapshot";
	pending.rollbackFiles.push_back(Mumble::UpdateHealth::RollbackFile{ "mumble.exe", true, 42, std::string(64, 'a') });
	return pending;
}

} // namespace

class TestUpdateHealth : public QObject {
	Q_OBJECT

private slots:
	void pendingStateRoundTrips();
	void markerRequiresEveryStartupGate();
	void terminalJournalPrecedesMarkerCleanup();
	void invalidRollbackPathIsRejected();
	void installationKeyIsCaseInsensitive();
	void installationKeyNormalizesExtendedDosPrefix();
	void stableWindowUsesMonotonicElapsedTime();
};

void TestUpdateHealth::pendingStateRoundTrips() {
	QTemporaryDir temporary;
	QVERIFY(temporary.isValid());
	const auto root                          = filesystemPath(temporary.path());
	auto pending                             = pendingFor(root);
	pending.minimumStableRuntimeMilliseconds = 1;

	std::string error;
	QVERIFY2(Mumble::UpdateHealth::writePendingState(root, pending, &error), error.c_str());
	const auto restored = Mumble::UpdateHealth::readPendingState(root, pending.appPath, &error);
	QVERIFY2(restored.has_value(), error.c_str());
	QCOMPARE(QString::fromStdString(restored->packageIdentity), QStringLiteral("0123456789abcdef"));
	QCOMPARE(QString::fromStdString(restored->transactionId), QStringLiteral("00112233445566778899aabbccddeeff"));
	QCOMPARE(restored->state, Mumble::UpdateHealth::TransactionState::AwaitingHealth);
	QCOMPARE(restored->rollbackFiles.size(), std::size_t(1));
	QCOMPARE(restored->minimumStableRuntimeMilliseconds, Mumble::UpdateHealth::MinimumStableRuntimeMilliseconds);
}

void TestUpdateHealth::markerRequiresEveryStartupGate() {
	QTemporaryDir temporary;
	QVERIFY(temporary.isValid());
	const auto root    = filesystemPath(temporary.path());
	const auto pending = pendingFor(root);

	std::string error;
	QVERIFY2(Mumble::UpdateHealth::writePendingState(root, pending, &error), error.c_str());
	QVERIFY(!Mumble::UpdateHealth::writeHealthMarker(root, pending.appPath, 9'999, true, true, &error));
	QVERIFY(!Mumble::UpdateHealth::writeHealthMarker(root, pending.appPath, 10'000, false, true, &error));
	QVERIFY(!Mumble::UpdateHealth::writeHealthMarker(root, pending.appPath, 10'000, true, false, &error));
	QVERIFY2(Mumble::UpdateHealth::writeHealthMarker(root, pending.appPath, 10'000, true, true, &error), error.c_str());
	QVERIFY2(Mumble::UpdateHealth::markerConfirmsHealthy(root, pending, &error), error.c_str());
	auto newAttempt          = pending;
	newAttempt.transactionId = "ffeeddccbbaa99887766554433221100";
	QVERIFY2(Mumble::UpdateHealth::writePendingState(root, newAttempt, &error), error.c_str());
	QVERIFY(!Mumble::UpdateHealth::markerConfirmsHealthy(root, newAttempt, &error));
}

void TestUpdateHealth::terminalJournalPrecedesMarkerCleanup() {
	QTemporaryDir temporary;
	QVERIFY(temporary.isValid());
	const auto root = filesystemPath(temporary.path());
	auto pending    = pendingFor(root);

	std::string error;
	QVERIFY2(Mumble::UpdateHealth::writePendingState(root, pending, &error), error.c_str());
	QVERIFY2(Mumble::UpdateHealth::writeHealthMarker(root, pending.appPath, 10'000, true, true, &error),
			 error.c_str());
	const auto marker = Mumble::UpdateHealth::healthMarkerPath(root, pending);
	QVERIFY(std::filesystem::exists(marker));

	// The updater writes the terminal journal first and only then removes the
	// marker. A power loss between those operations must leave both pieces of
	// evidence, never an awaiting-health journal with its marker erased.
	pending.state = Mumble::UpdateHealth::TransactionState::Committed;
	QVERIFY2(Mumble::UpdateHealth::writePendingState(root, pending, &error), error.c_str());
	QVERIFY(std::filesystem::exists(marker));
	const auto restored = Mumble::UpdateHealth::readPendingState(root, pending.appPath, &error);
	QVERIFY2(restored.has_value(), error.c_str());
	QCOMPARE(restored->state, Mumble::UpdateHealth::TransactionState::Committed);
}

void TestUpdateHealth::invalidRollbackPathIsRejected() {
	QTemporaryDir temporary;
	QVERIFY(temporary.isValid());
	const auto root                    = filesystemPath(temporary.path());
	auto pending                       = pendingFor(root);
	pending.rollbackFiles.front().path = "../outside.exe";

	std::string error;
	QVERIFY(!Mumble::UpdateHealth::writePendingState(root, pending, &error));
	QVERIFY(!error.empty());
}

void TestUpdateHealth::installationKeyIsCaseInsensitive() {
	const auto lower = std::filesystem::path("C:/program files/mumble");
	const auto upper = std::filesystem::path("C:/PROGRAM FILES/MUMBLE");
	QCOMPARE(QString::fromStdString(Mumble::UpdateHealth::installationKey(lower)),
			 QString::fromStdString(Mumble::UpdateHealth::installationKey(upper)));
}

void TestUpdateHealth::installationKeyNormalizesExtendedDosPrefix() {
#ifdef _WIN32
	const auto ordinary = std::filesystem::path(L"C:\\Program Files\\Mumble");
	const auto extended = std::filesystem::path(L"\\\\?\\C:\\Program Files\\Mumble");
	QCOMPARE(QString::fromStdString(Mumble::UpdateHealth::installationKey(ordinary)),
			 QString::fromStdString(Mumble::UpdateHealth::installationKey(extended)));
#endif
}

void TestUpdateHealth::stableWindowUsesMonotonicElapsedTime() {
	UpdateHealthStableWindow window(10'000);

	// Wall-clock movement is deliberately independent from the only clock the
	// production tracker accepts. Large forward/backward jumps therefore cannot
	// qualify a package early or erase genuine steady elapsed time.
	qint64 simulatedWallClock = 1'700'000'000'000;
	QVERIFY(!window.observe(true, 500));
	simulatedWallClock += 86'400'000;
	QVERIFY(!window.observe(true, 5'499));
	simulatedWallClock -= 172'800'000;
	QVERIFY(!window.observe(true, 10'499));
	QCOMPARE(*window.observe(true, 10'500), std::uint64_t{ 10'000 });
	QVERIFY(simulatedWallClock < 1'700'000'000'000);

	QVERIFY(!window.observe(false, 11'000));
	QVERIFY(!window.observe(true, 20'000));
	// Defensively reset if an injected/defective monotonic source goes backwards
	// rather than underflowing into an immediate health marker.
	QVERIFY(!window.observe(true, 19'000));
	QVERIFY(!window.observe(true, 28'999));
	QCOMPARE(*window.observe(true, 29'000), std::uint64_t{ 10'000 });
}

QTEST_GUILESS_MAIN(TestUpdateHealth)
#include "TestUpdateHealth.moc"
