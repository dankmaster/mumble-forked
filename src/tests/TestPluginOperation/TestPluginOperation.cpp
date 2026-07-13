#include "PluginOperation.h"

#include <QtConcurrent/QtConcurrentRun>
#include <QtCore/QSemaphore>
#include <QtTest/QtTest>

class TestPluginOperation : public QObject {
	Q_OBJECT
private slots:
	void stableIdentityAndDuplicateProtection();
	void partialSuccessSummary();
	void cancellationCompletesEveryItem();
	void failureIsNotMaskedByLaterCancellation();
	void lateCancellationIsIgnoredAfterTerminalSeal();
	void cancellationGateHasAtomicTerminalBoundary();
	void acceptsOutOfOrderWorkerResults();
	void preservesPerItemFailureMatrix();
};

void TestPluginOperation::stableIdentityAndDuplicateProtection() {
	PluginOperation operation(QStringLiteral("plugin-update"));
	QVERIFY(operation.operationID().startsWith(QStringLiteral("plugin-update:")));
	operation.setItems({ QStringLiteral("7"), QStringLiteral("7"), QString(), QStringLiteral("8") });
	QCOMPARE(operation.summary().totalItems, 2);
	QVERIFY(!operation.completeItem({ QStringLiteral("missing"), 9, true }));
	QVERIFY(operation.completeItem({ QStringLiteral("7"), 7, true }));
	QVERIFY(!operation.completeItem({ QStringLiteral("7"), 7, false, false, QStringLiteral("late") }));
	QCOMPARE(operation.summary().completedItems, 1);
}

void TestPluginOperation::partialSuccessSummary() {
	PluginOperation operation(QStringLiteral("plugin-update"), QStringLiteral("plugin-update:test"));
	operation.setItems({ QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3") });
	QVERIFY(operation.completeItem({ QStringLiteral("1"), 1, true, false, {}, QStringLiteral("Updated") }));
	QVERIFY(operation.completeItem(
		{ QStringLiteral("2"), 2, false, false, QStringLiteral("network-error"), QStringLiteral("Offline") }));
	QVERIFY(operation.completeItem(
		{ QStringLiteral("3"), 3, false, false, QStringLiteral("load-error"), QStringLiteral("Rolled back") }));
	const PluginOperation::Summary summary = operation.summary();
	QCOMPARE(summary.status, QStringLiteral("partial"));
	QCOMPARE(summary.successfulItems, 1);
	QCOMPARE(summary.failedItems, 2);
	QCOMPARE(summary.cancelledItems, 0);
	QCOMPARE(summary.results.at(1).errorCode, QStringLiteral("network-error"));
}

void TestPluginOperation::preservesPerItemFailureMatrix() {
	PluginOperation operation(QStringLiteral("plugin-update"), QStringLiteral("plugin-update:error-matrix"));
	operation.setItems({ QStringLiteral("incompatible"), QStringLiteral("archive"), QStringLiteral("overwrite"),
		QStringLiteral("load"), QStringLiteral("network"), QStringLiteral("cancelled"), QStringLiteral("updated") });

	const QVector< PluginOperation::ItemResult > results {
		{ QStringLiteral("incompatible"), 1, false, false, QStringLiteral("incompatible-package"),
		  QStringLiteral("Unsupported runtime") },
		{ QStringLiteral("archive"), 2, false, false, QStringLiteral("package-error"),
		  QStringLiteral("Malformed archive") },
		{ QStringLiteral("overwrite"), 3, false, false, QStringLiteral("overwrite-required"),
		  QStringLiteral("Confirmation required") },
		{ QStringLiteral("load"), 4, false, false, QStringLiteral("load-error"),
		  QStringLiteral("Previous file restored") },
		{ QStringLiteral("network"), 5, false, false, QStringLiteral("network-error"),
		  QStringLiteral("Offline") },
		{ QStringLiteral("cancelled"), 6, false, true, QStringLiteral("cancelled"),
		  QStringLiteral("Cancelled") },
		{ QStringLiteral("updated"), 7, true, false, {}, QStringLiteral("Updated") },
	};
	for (const PluginOperation::ItemResult &result : results) {
		QVERIFY(operation.completeItem(result));
	}

	const PluginOperation::Summary summary = operation.summary();
	QCOMPARE(summary.status, QStringLiteral("partial"));
	QCOMPARE(summary.successfulItems, 1);
	QCOMPARE(summary.failedItems, 5);
	QCOMPARE(summary.cancelledItems, 1);
	QCOMPARE(summary.results.size(), results.size());
	for (qsizetype i = 0; i < results.size(); ++i) {
		QCOMPARE(summary.results.at(i).itemID, results.at(i).itemID);
		QCOMPARE(summary.results.at(i).errorCode, results.at(i).errorCode);
		QCOMPARE(summary.results.at(i).message, results.at(i).message);
	}
}

void TestPluginOperation::cancellationCompletesEveryItem() {
	PluginOperation operation(QStringLiteral("plugin-update"));
	operation.setItems({ QStringLiteral("1"), QStringLiteral("2") });
	QVERIFY(operation.requestCancellation());
	QVERIFY(operation.cancellationToken()->load());
	QVERIFY(!operation.requestCancellation());
	for (const QString &itemID : operation.pendingItems()) {
		QVERIFY(operation.completeItem({ itemID, itemID.toULongLong(), false, true }));
	}
	const PluginOperation::Summary summary = operation.summary();
	QCOMPARE(summary.status, QStringLiteral("cancelled"));
	QCOMPARE(summary.completedItems, 2);
	QCOMPARE(summary.cancelledItems, 2);
	for (const PluginOperation::ItemResult &result : summary.results) {
		QCOMPARE(result.errorCode, QStringLiteral("cancelled"));
	}
}

void TestPluginOperation::failureIsNotMaskedByLaterCancellation() {
	PluginOperation operation(QStringLiteral("plugin-update"));
	operation.setItems({ QStringLiteral("1"), QStringLiteral("2") });
	QVERIFY(operation.completeItem(
		{ QStringLiteral("1"), 1, false, false, QStringLiteral("network-error"), QStringLiteral("Offline") }));
	QVERIFY(operation.requestCancellation());
	QVERIFY(operation.completeItem(
		{ QStringLiteral("2"), 2, false, true, QStringLiteral("cancelled"), QStringLiteral("Cancelled") }));

	const PluginOperation::Summary summary = operation.summary();
	QCOMPARE(summary.status, QStringLiteral("failed"));
	QCOMPARE(summary.failedItems, 1);
	QCOMPARE(summary.cancelledItems, 1);
}

void TestPluginOperation::lateCancellationIsIgnoredAfterTerminalSeal() {
	PluginOperation operation(QStringLiteral("plugin-update"));
	operation.setItems({ QStringLiteral("1") });
	QVERIFY(operation.sealItem(QStringLiteral("1")));
	QVERIFY(!operation.requestCancellation());
	QVERIFY(!operation.isCancellationRequested());
	QVERIFY(operation.pendingItems().isEmpty());
	QVERIFY(operation.completeItem(
		{ QStringLiteral("1"), 1, true, false, {}, QStringLiteral("Updated") }));
	const PluginOperation::Summary summary = operation.summary();
	QCOMPARE(summary.status, QStringLiteral("succeeded"));
	QCOMPARE(summary.successfulItems, 1);
	QVERIFY(!summary.cancellationRequested);
}

void TestPluginOperation::cancellationGateHasAtomicTerminalBoundary() {
	{
		PluginCancellationGate gate;
		QVERIFY(gate.requestCancellation());
		QVERIFY(!gate.seal());
		QVERIFY(gate.isCancellationRequested());
		QVERIFY(gate.cancellationToken()->load());
	}
	{
		PluginCancellationGate gate;
		QVERIFY(gate.seal());
		QVERIFY(!gate.requestCancellation());
		QVERIFY(gate.isSealed());
		QVERIFY(!gate.cancellationToken()->load());
	}

	for (int iteration = 0; iteration < 64; ++iteration) {
		PluginCancellationGate gate;
		QSemaphore ready;
		QSemaphore start;
		QFuture< bool > cancelFuture = QtConcurrent::run([&gate, &ready, &start]() {
			ready.release();
			start.acquire();
			return gate.requestCancellation();
		});
		QFuture< bool > sealFuture = QtConcurrent::run([&gate, &ready, &start]() {
			ready.release();
			start.acquire();
			return gate.seal();
		});
		ready.acquire(2);
		start.release(2);
		const bool cancelled = cancelFuture.result();
		const bool sealed = sealFuture.result();
		QVERIFY(cancelled != sealed);
		QCOMPARE(gate.isCancellationRequested(), cancelled);
		QCOMPARE(gate.isSealed(), sealed);
		QCOMPARE(gate.cancellationToken()->load(), cancelled);
	}
}

void TestPluginOperation::acceptsOutOfOrderWorkerResults() {
	PluginOperation operation(QStringLiteral("plugin-files"));
	QStringList itemIDs;
	for (int i = 0; i < 64; ++i) {
		itemIDs.push_back(QString::number(i));
	}
	operation.setItems(itemIDs);
	QList< QFuture< bool > > futures;
	for (int i = itemIDs.size() - 1; i >= 0; --i) {
		futures.push_back(QtConcurrent::run([&operation, itemID = itemIDs.at(i), i]() {
			return operation.completeItem({ itemID, static_cast< qulonglong >(i), true });
		}));
	}
	for (QFuture< bool > &future : futures) {
		QVERIFY(future.result());
	}
	const PluginOperation::Summary summary = operation.summary();
	QCOMPARE(summary.status, QStringLiteral("succeeded"));
	QCOMPARE(summary.completedItems, itemIDs.size());
	QCOMPARE(summary.successfulItems, itemIDs.size());
}

QTEST_GUILESS_MAIN(TestPluginOperation)
#include "TestPluginOperation.moc"
