#include "ComposerController.h"
#include "QmlImageProvider.h"

#include <QtCore/QBuffer>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QMimeData>
#include <QtCore/QTemporaryDir>
#include <QtCore/QThread>
#include <QtGui/QColor>
#include <QtGui/QImage>
#include <QtTest/QSignalSpy>
#include <QtTest/QtTest>

namespace {
	QByteArray pngBytes() {
		QImage image(4, 4, QImage::Format_ARGB32);
		image.fill(Qt::red);
		QByteArray bytes;
		QBuffer buffer(&bytes);
		buffer.open(QIODevice::WriteOnly);
		image.save(&buffer, "PNG");
		return bytes;
	}

	QString statusAt(const DraftAttachmentModel *model, const int row) {
		return model->data(model->index(row), DraftAttachmentModel::StatusRole).toString();
	}
}

class TestComposerController : public QObject {
	Q_OBJECT
private slots:
	void initTestCase();
	void preservesDraftUntilSuccessfulSend();
	void removesReordersAndCompletes();
	void retriesFailedAttachment();
	void capsAndCancelsValidationQueue();
	void blocksSendUntilValidationFinishes();
	void ignoresStaleValidationAfterCancelAndRetry();
	void latestRetryGenerationWins();
	void deduplicatesLocalPaths();
	void freezesSubmittedDraftUntilCompletion();
	void resolvesAttachmentsOnTheModelThread();
	void ingestsClipboardImageAndSendsWithoutText();
	void ingestsGenericFileMetadataAndSendsWithoutText();
	void rechecksChangedServerLimitsBeforeSend();
};

void TestComposerController::initTestCase() {
	qRegisterMetaType< QList< Mumble::ChatAttachments::Source > >();
}

void TestComposerController::preservesDraftUntilSuccessfulSend() {
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	QFile file(dir.filePath(QStringLiteral("a.png")));
	QVERIFY(file.open(QFile::WriteOnly));
	QCOMPARE(file.write(pngBytes()) > 0, true);
	file.close();

	auto pipeline = std::make_shared< QmlImagePipeline >();
	ComposerController controller(pipeline);
	controller.setCanSend(true);
	controller.setText(QStringLiteral("hello"));
	controller.addUrls({ QUrl::fromLocalFile(file.fileName()) });
	QCOMPARE(controller.attachments()->rowCount(), 1);
	QTRY_COMPARE_WITH_TIMEOUT(statusAt(controller.attachments(), 0), QStringLiteral("ready"), 5000);

	QSignalSpy sent(&controller, &ComposerController::sendRequested);
	controller.send();
	QCOMPARE(sent.count(), 1);
	QCOMPARE(controller.text(), QStringLiteral("hello"));
	QCOMPARE(controller.attachments()->rowCount(), 1);
	controller.finishSend(true);
	QVERIFY(controller.text().isEmpty());
	QCOMPARE(controller.attachments()->rowCount(), 0);
}

void TestComposerController::removesReordersAndCompletes() {
	auto pipeline = std::make_shared< QmlImagePipeline >();
	ComposerController controller(pipeline);
	controller.setAutocompleteSources({ QStringLiteral("Alice"), QStringLiteral("Bob") },
											{ QStringLiteral("help"), QStringLiteral("mute") });
	controller.setText(QStringLiteral("hi @Al"));
	QCOMPARE(controller.autocompleteItems().size(), 1);
	controller.complete(QStringLiteral("@Alice"));
	QCOMPARE(controller.text(), QStringLiteral("hi @Alice "));
}

void TestComposerController::retriesFailedAttachment() {
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	QFile file(dir.filePath(QStringLiteral("corrupt.png")));
	QVERIFY(file.open(QFile::WriteOnly));
	QVERIFY(file.write(QByteArray::fromHex("89504e470d0a1a0a")) > 0);
	file.close();

	auto pipeline = std::make_shared< QmlImagePipeline >();
	ComposerController controller(pipeline);
	controller.addUrls({ QUrl::fromLocalFile(file.fileName()) });
	QCOMPARE(controller.attachments()->rowCount(), 1);
	QTRY_COMPARE_WITH_TIMEOUT(statusAt(controller.attachments(), 0), QStringLiteral("failed"), 5000);
	controller.addUrls({ QUrl::fromLocalFile(file.fileName()) });
	QCOMPARE(controller.attachments()->rowCount(), 1);

	const QString id = controller.attachments()
						   ->data(controller.attachments()->index(0), DraftAttachmentModel::IdRole)
						   .toString();
	controller.retryAttachment(id);
	QCOMPARE(statusAt(controller.attachments(), 0), QStringLiteral("loading"));
	QTRY_COMPARE_WITH_TIMEOUT(statusAt(controller.attachments(), 0), QStringLiteral("failed"), 5000);
	controller.addUrls({ QUrl::fromLocalFile(dir.filePath(QStringLiteral("missing.png"))) });
	QTRY_COMPARE_WITH_TIMEOUT(statusAt(controller.attachments(), 1), QStringLiteral("failed"), 5000);
	QCOMPARE(controller.attachments()->rowCount(), 2);

	controller.setCanSend(true);
	QSignalSpy sent(&controller, &ComposerController::sendRequested);
	QSignalSpy failed(&controller, &ComposerController::sendFailed);
	controller.send();
	QCOMPARE(sent.count(), 0);
	QCOMPARE(failed.count(), 1);
}

void TestComposerController::capsAndCancelsValidationQueue() {
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	const QByteArray bytes = pngBytes();
	QVariantList urls;
	for (int index = 0; index < 24; ++index) {
		const QString path = dir.filePath(QStringLiteral("attachment-%1.png").arg(index));
		QFile file(path);
		QVERIFY(file.open(QFile::WriteOnly));
		QCOMPARE(file.write(bytes), bytes.size());
		file.close();
		urls.push_back(QUrl::fromLocalFile(path));
	}

	auto pipeline = std::make_shared< QmlImagePipeline >();
	ComposerController controller(pipeline);
	controller.setAttachmentLimits(16, 25ULL * 1024ULL * 1024ULL);
	controller.addUrls(urls);
	QCOMPARE(controller.attachments()->rowCount(), 16);
	const QString cancelledId = controller.attachments()
								->data(controller.attachments()->index(15), DraftAttachmentModel::IdRole)
								.toString();
	controller.cancelAttachment(cancelledId);
	QCOMPARE(controller.attachments()->rowCount(), 15);
	QTRY_VERIFY_WITH_TIMEOUT([&controller]() {
		for (int row = 0; row < controller.attachments()->rowCount(); ++row) {
			if (statusAt(controller.attachments(), row) == QLatin1String("loading")) return false;
		}
		return true;
	}(), 5000);
	QCOMPARE(controller.attachments()->rowCount(), 15);
}

void TestComposerController::blocksSendUntilValidationFinishes() {
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	QFile file(dir.filePath(QStringLiteral("pending.png")));
	QVERIFY(file.open(QFile::WriteOnly));
	QVERIFY(file.write(pngBytes()) > 0);
	file.close();

	auto pipeline = std::make_shared< QmlImagePipeline >();
	ComposerController controller(pipeline);
	controller.setCanSend(true);
	controller.addUrls({ QUrl::fromLocalFile(file.fileName()) });
	QCOMPARE(statusAt(controller.attachments(), 0), QStringLiteral("loading"));
	QSignalSpy sent(&controller, &ComposerController::sendRequested);
	controller.send();
	QCOMPARE(sent.count(), 0);

	QTRY_COMPARE_WITH_TIMEOUT(statusAt(controller.attachments(), 0), QStringLiteral("ready"), 5000);
	controller.send();
	QCOMPARE(sent.count(), 1);
}

void TestComposerController::ignoresStaleValidationAfterCancelAndRetry() {
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	QFile file(dir.filePath(QStringLiteral("retry.png")));
	QVERIFY(file.open(QFile::WriteOnly));
	QVERIFY(file.write(pngBytes()) > 0);
	file.close();

	auto pipeline = std::make_shared< QmlImagePipeline >();
	ComposerController controller(pipeline);
	controller.addUrls({ QUrl::fromLocalFile(file.fileName()) });
	const QString firstId = controller.attachments()
								->data(controller.attachments()->index(0), DraftAttachmentModel::IdRole)
								.toString();
	controller.retryAttachment(firstId);
	controller.cancelAttachment(firstId);
	QCOMPARE(controller.attachments()->rowCount(), 0);

	// A result from either generation of the canceled row must not recreate it.
	QTest::qWait(100);
	QCOMPARE(controller.attachments()->rowCount(), 0);
	controller.addUrls({ QUrl::fromLocalFile(file.fileName()) });
	QCOMPARE(controller.attachments()->rowCount(), 1);
	QTRY_COMPARE_WITH_TIMEOUT(statusAt(controller.attachments(), 0), QStringLiteral("ready"), 5000);
	QCOMPARE(controller.attachments()->rowCount(), 1);
}

void TestComposerController::latestRetryGenerationWins() {
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	QFile file(dir.filePath(QStringLiteral("rapid-retry.png")));
	QVERIFY(file.open(QFile::WriteOnly));
	QVERIFY(file.write(pngBytes()) > 0);
	file.close();

	auto pipeline = std::make_shared< QmlImagePipeline >();
	ComposerController controller(pipeline);
	controller.addUrls({ QUrl::fromLocalFile(file.fileName()) });
	const QString id = controller.attachments()
						   ->data(controller.attachments()->index(0), DraftAttachmentModel::IdRole)
						   .toString();
	for (int attempt = 0; attempt < 8; ++attempt) controller.retryAttachment(id);
	QCOMPARE(controller.attachments()->rowCount(), 1);
	QTRY_COMPARE_WITH_TIMEOUT(statusAt(controller.attachments(), 0), QStringLiteral("ready"), 5000);
	QCOMPARE(controller.attachments()
				 ->data(controller.attachments()->index(0), DraftAttachmentModel::IdRole)
				 .toString(),
			 id);
}

void TestComposerController::deduplicatesLocalPaths() {
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	const QString path = dir.filePath(QStringLiteral("CaseSensitiveName.png"));
	QFile file(path);
	QVERIFY(file.open(QFile::WriteOnly));
	QVERIFY(file.write(pngBytes()) > 0);
	file.close();

	QVariantList urls { QUrl::fromLocalFile(path), QUrl::fromLocalFile(QDir::cleanPath(path)) };
#ifdef Q_OS_WIN
	// Windows local paths are case-insensitive even when different spelling is
	// delivered by drag-and-drop and the native file picker.
	urls.push_back(QUrl::fromLocalFile(path.toUpper()));
#endif
	auto pipeline = std::make_shared< QmlImagePipeline >();
	ComposerController controller(pipeline);
	controller.addUrls(urls);
	QCOMPARE(controller.attachments()->rowCount(), 1);
	QTRY_COMPARE_WITH_TIMEOUT(statusAt(controller.attachments(), 0), QStringLiteral("ready"), 5000);
	controller.addUrls(urls);
	QCOMPARE(controller.attachments()->rowCount(), 1);
}

void TestComposerController::freezesSubmittedDraftUntilCompletion() {
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	const QString firstPath = dir.filePath(QStringLiteral("first.png"));
	const QString secondPath = dir.filePath(QStringLiteral("second.png"));
	for (const QString &path : { firstPath, secondPath }) {
		QFile file(path);
		QVERIFY(file.open(QFile::WriteOnly));
		QVERIFY(file.write(pngBytes()) > 0);
	}

	auto pipeline = std::make_shared< QmlImagePipeline >();
	ComposerController controller(pipeline);
	controller.setCanSend(true);
	controller.setText(QStringLiteral("submitted"));
	controller.addUrls({ QUrl::fromLocalFile(firstPath) });
	QTRY_COMPARE_WITH_TIMEOUT(statusAt(controller.attachments(), 0), QStringLiteral("ready"), 5000);
	const QString id = controller.attachments()
						   ->data(controller.attachments()->index(0), DraftAttachmentModel::IdRole)
						   .toString();
	QSignalSpy sent(&controller, &ComposerController::sendRequested);
	controller.send();
	QCOMPARE(sent.count(), 1);

	controller.setText(QStringLiteral("replacement"));
	controller.addUrls({ QUrl::fromLocalFile(secondPath) });
	controller.retryAttachment(id);
	controller.removeAttachment(id);
	QCOMPARE(controller.text(), QStringLiteral("submitted"));
	QCOMPARE(controller.attachments()->rowCount(), 1);
	QCOMPARE(statusAt(controller.attachments(), 0), QStringLiteral("ready"));

	controller.finishSend(true);
	QVERIFY(controller.text().isEmpty());
	QCOMPARE(controller.attachments()->rowCount(), 0);
}

void TestComposerController::resolvesAttachmentsOnTheModelThread() {
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	QFile file(dir.filePath(QStringLiteral("affinity.png")));
	QVERIFY(file.open(QFile::WriteOnly));
	QVERIFY(file.write(pngBytes()) > 0);
	file.close();

	auto pipeline = std::make_shared< QmlImagePipeline >();
	ComposerController controller(pipeline);
	QThread *const ownerThread = controller.attachments()->thread();
	bool wrongThread = false;
	connect(controller.attachments(), &QAbstractItemModel::dataChanged, &controller,
			[&wrongThread, ownerThread]() {
				if (QThread::currentThread() != ownerThread) wrongThread = true;
			});
	controller.addUrls({ QUrl::fromLocalFile(file.fileName()) });
	QTRY_COMPARE_WITH_TIMEOUT(statusAt(controller.attachments(), 0), QStringLiteral("ready"), 5000);
	QVERIFY(!wrongThread);
}

void TestComposerController::ingestsClipboardImageAndSendsWithoutText() {
	auto pipeline = std::make_shared< QmlImagePipeline >();
	ComposerController controller(pipeline);
	controller.setCanSend(true);

	QImage image(7, 5, QImage::Format_ARGB32_Premultiplied);
	image.fill(QColor(12, 34, 56, 200));
	QMimeData clipboardData;
	clipboardData.setImageData(image);
	QVERIFY(controller.ingestMimeData(clipboardData));
	QCOMPARE(controller.attachments()->rowCount(), 1);
	QTRY_COMPARE_WITH_TIMEOUT(statusAt(controller.attachments(), 0), QStringLiteral("ready"), 5000);

	const QModelIndex index = controller.attachments()->index(0);
	QCOMPARE(controller.attachments()->data(index, DraftAttachmentModel::MimeRole).toString(),
			 QStringLiteral("image/png"));
	QCOMPARE(controller.attachments()->data(index, DraftAttachmentModel::KindRole).toString(),
			 QStringLiteral("image"));
	QVERIFY(controller.attachments()->data(index, DraftAttachmentModel::ByteSizeRole).toULongLong() > 0);
	const QString temporaryPath = controller.attachments()
							  ->data(index, DraftAttachmentModel::LocalUrlRole)
							  .toUrl()
							  .toLocalFile();
	QVERIFY(!temporaryPath.isEmpty());
	QVERIFY(QFileInfo::exists(temporaryPath));

	int requestCount = 0;
	QString submittedText;
	QList< Mumble::ChatAttachments::Source > submittedAttachments;
	connect(&controller, &ComposerController::sendRequested, &controller,
			[&](const QString &text, const QList< Mumble::ChatAttachments::Source > &attachments) {
				++requestCount;
				submittedText        = text;
				submittedAttachments = attachments;
			});
	controller.send();
	QCOMPARE(requestCount, 1);
	QVERIFY(submittedText.isEmpty());
	QCOMPARE(submittedAttachments.size(), 1);
	QCOMPARE(submittedAttachments.constFirst().kind, Mumble::ChatAttachments::Kind::Image);
	QCOMPARE(submittedAttachments.constFirst().mime, QStringLiteral("image/png"));
	QCOMPARE(submittedAttachments.constFirst().sha256.size(), 64);
	QCOMPARE(submittedAttachments.constFirst().path, temporaryPath);

	controller.finishSend(true);
	QCOMPARE(controller.attachments()->rowCount(), 0);
	QVERIFY(!QFileInfo::exists(temporaryPath));
}

void TestComposerController::ingestsGenericFileMetadataAndSendsWithoutText() {
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	const QString path = dir.filePath(QStringLiteral("bundle.zip"));
	QFile file(path);
	QVERIFY(file.open(QIODevice::WriteOnly));
	QByteArray bytes = QByteArray::fromHex("0001");
	bytes.append("opaque archive payload");
	bytes.append('\0');
	QCOMPARE(file.write(bytes), bytes.size());
	file.close();

	auto pipeline = std::make_shared< QmlImagePipeline >();
	ComposerController controller(pipeline);
	controller.setCanSend(true);
	QMimeData droppedData;
	droppedData.setUrls({ QUrl::fromLocalFile(path) });
	QVERIFY(controller.ingestMimeData(droppedData));
	QTRY_COMPARE_WITH_TIMEOUT(statusAt(controller.attachments(), 0), QStringLiteral("ready"), 5000);

	const QModelIndex index = controller.attachments()->index(0);
	QCOMPARE(controller.attachments()->data(index, DraftAttachmentModel::FileNameRole).toString(),
			 QStringLiteral("bundle.zip"));
	QCOMPARE(controller.attachments()->data(index, DraftAttachmentModel::MimeRole).toString(),
			 QStringLiteral("application/zip"));
	QCOMPARE(controller.attachments()->data(index, DraftAttachmentModel::KindRole).toString(),
			 QStringLiteral("binary"));
	QCOMPARE(controller.attachments()->data(index, DraftAttachmentModel::ByteSizeRole).toULongLong(),
			 static_cast< qulonglong >(bytes.size()));
	QVERIFY(controller.attachments()->data(index, DraftAttachmentModel::ThumbnailUrlRole).toString().isEmpty());

	QList< Mumble::ChatAttachments::Source > submittedAttachments;
	connect(&controller, &ComposerController::sendRequested, &controller,
			[&](const QString &, const QList< Mumble::ChatAttachments::Source > &attachments) {
				submittedAttachments = attachments;
			});
	controller.send();
	QCOMPARE(submittedAttachments.size(), 1);
	QCOMPARE(submittedAttachments.constFirst().fileName, QStringLiteral("bundle.zip"));
	QCOMPARE(submittedAttachments.constFirst().mime, QStringLiteral("application/zip"));
	QCOMPARE(submittedAttachments.constFirst().kind, Mumble::ChatAttachments::Kind::Binary);
	QCOMPARE(submittedAttachments.constFirst().byteSize, static_cast< quint64 >(bytes.size()));
}

void TestComposerController::rechecksChangedServerLimitsBeforeSend() {
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	QVariantList urls;
	for (int index = 0; index < 2; ++index) {
		const QString path = dir.filePath(QStringLiteral("limit-%1.png").arg(index));
		QFile file(path);
		QVERIFY(file.open(QIODevice::WriteOnly));
		QVERIFY(file.write(pngBytes()) > 1);
		urls.push_back(QUrl::fromLocalFile(path));
	}

	auto pipeline = std::make_shared< QmlImagePipeline >();
	ComposerController controller(pipeline);
	controller.setCanSend(true);
	controller.setAttachmentLimits(4, 1024 * 1024);
	controller.addUrls(urls);
	QTRY_VERIFY_WITH_TIMEOUT([&controller]() {
		for (int row = 0; row < controller.attachments()->rowCount(); ++row) {
			if (statusAt(controller.attachments(), row) != QLatin1String("ready")) return false;
		}
		return controller.attachments()->rowCount() == 2;
	}(), 5000);

	QSignalSpy sent(&controller, &ComposerController::sendRequested);
	QSignalSpy failed(&controller, &ComposerController::sendFailed);
	controller.setAttachmentLimits(1, 1024 * 1024);
	controller.send();
	QCOMPARE(sent.count(), 0);
	QCOMPARE(failed.count(), 1);

	const QString secondID = controller.attachments()
		->data(controller.attachments()->index(1), DraftAttachmentModel::IdRole).toString();
	controller.removeAttachment(secondID);
	controller.setAttachmentLimits(4, 1);
	controller.send();
	QCOMPARE(sent.count(), 0);
	QCOMPARE(failed.count(), 2);
}

QTEST_GUILESS_MAIN(TestComposerController)
#include "TestComposerController.moc"
