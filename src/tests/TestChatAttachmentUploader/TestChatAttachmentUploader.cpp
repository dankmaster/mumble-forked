#include "ChatAttachmentUploader.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QFile>
#include <QtCore/QTemporaryDir>
#include <QtTest/QtTest>

namespace {
	struct CapturedTransport {
		QList< MumbleProto::ChatAssetUploadInit > inits;
		QList< MumbleProto::ChatAssetUploadChunk > chunks;
		QList< MumbleProto::ChatAssetUploadCommit > commits;

		ChatAttachmentUploader::Transport callbacks() {
			return {
				[this](const MumbleProto::ChatAssetUploadInit &message) { inits.push_back(message); },
				[this](const MumbleProto::ChatAssetUploadChunk &message) { chunks.push_back(message); },
				[this](const MumbleProto::ChatAssetUploadCommit &message) { commits.push_back(message); },
			};
		}
	};

	Mumble::ChatAttachments::Source writeSource(QTemporaryDir &dir, const QString &name, const QByteArray &bytes,
										   const Mumble::ChatAttachments::Kind kind,
										   const QString &mime) {
		const QString path = dir.filePath(name);
		QFile file(path);
		if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
			return {};
		}
		file.close();
		return {
			QStringLiteral("draft-1"),
			path,
			name,
			mime,
			QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()),
			static_cast< quint64 >(bytes.size()),
			kind,
		};
	}
}

class TestChatAttachmentUploader : public QObject {
	Q_OBJECT

private slots:
	void uploadsChunkedFileAndCompletes();
	void stopsWhenServerRejectsUpload();
	void rejectsUnexpectedAcceptedSize();
};

void TestChatAttachmentUploader::uploadsChunkedFileAndCompletes() {
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	QByteArray bytes(600 * 1024, '\0');
	for (qsizetype index = 0; index < bytes.size(); ++index) {
		bytes[index] = static_cast< char >(index % 251);
	}
	const Mumble::ChatAttachments::Source source =
		writeSource(dir, QStringLiteral("recording.bin"), bytes, Mumble::ChatAttachments::Kind::Binary,
					QStringLiteral("application/octet-stream"));
	QVERIFY(!source.path.isEmpty());

	CapturedTransport transport;
	ChatAttachmentUploader uploader(transport.callbacks());
	QList< bool > busyStates;
	QList< unsigned int > completedAssetIDs;
	QStringList completedFileNames;
	connect(&uploader, &ChatAttachmentUploader::busyChanged, &uploader,
			[&]() { busyStates.push_back(uploader.busy()); });
	connect(&uploader, &ChatAttachmentUploader::completed, &uploader,
			[&](const QList< unsigned int > &assetIDs, const QStringList &fileNames) {
				completedAssetIDs = assetIDs;
				completedFileNames = fileNames;
			});

	QVERIFY(uploader.start({ source }));
	QVERIFY(uploader.busy());
	QCOMPARE(busyStates, QList< bool > { true });
	QCOMPARE(transport.inits.size(), 1);
	const MumbleProto::ChatAssetUploadInit &init = transport.inits.constFirst();
	QCOMPARE(QString::fromStdString(init.filename()), source.fileName);
	QCOMPARE(QString::fromStdString(init.mime()), source.mime);
	QCOMPARE(init.byte_size(), source.byteSize);
	QCOMPARE(init.kind(), MumbleProto::ChatAssetKindBinary);
	QCOMPARE(QString::fromStdString(init.sha256()), source.sha256);
	QVERIFY(!init.request_inline());

	MumbleProto::ChatAssetState accepted;
	accepted.set_state(MumbleProto::ChatAssetTransferStateAccepted);
	accepted.set_upload_id(91);
	accepted.set_accepted_byte_size(source.byteSize);
	uploader.handleState(accepted);
	QTRY_COMPARE_WITH_TIMEOUT(transport.commits.size(), 1, 5000);
	QCOMPARE(transport.chunks.size(), 3);

	QByteArray uploaded;
	quint64 expectedOffset = 0;
	for (int index = 0; index < transport.chunks.size(); ++index) {
		const MumbleProto::ChatAssetUploadChunk &chunk = transport.chunks.at(index);
		QCOMPARE(chunk.upload_id(), 91ULL);
		QCOMPARE(chunk.offset(), expectedOffset);
		const QByteArray chunkBytes(chunk.data().data(), static_cast< qsizetype >(chunk.data().size()));
		QVERIFY(!chunkBytes.isEmpty());
		QVERIFY(chunkBytes.size() <= 256 * 1024);
		uploaded.append(chunkBytes);
		expectedOffset += static_cast< quint64 >(chunkBytes.size());
		QCOMPARE(chunk.final_chunk(), index == transport.chunks.size() - 1);
	}
	QCOMPARE(uploaded, bytes);
	const MumbleProto::ChatAssetUploadCommit &commit = transport.commits.constFirst();
	QCOMPARE(commit.upload_id(), 91ULL);
	QCOMPARE(QString::fromStdString(commit.filename()), source.fileName);
	QCOMPARE(QString::fromStdString(commit.sha256()), source.sha256);

	MumbleProto::ChatAssetState complete;
	complete.set_state(MumbleProto::ChatAssetTransferStateComplete);
	complete.set_upload_id(91);
	complete.mutable_asset()->set_asset_id(412);
	uploader.handleState(complete);
	QVERIFY(!uploader.busy());
	QCOMPARE(completedAssetIDs, QList< unsigned int > { 412 });
	QCOMPARE(completedFileNames, QStringList { source.fileName });
	QCOMPARE(busyStates, QList< bool >({ true, false }));
}

void TestChatAttachmentUploader::stopsWhenServerRejectsUpload() {
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	const Mumble::ChatAttachments::Source source =
		writeSource(dir, QStringLiteral("notes.txt"), QByteArray("hello"),
					Mumble::ChatAttachments::Kind::Document, QStringLiteral("text/plain"));
	QVERIFY(!source.path.isEmpty());

	CapturedTransport transport;
	ChatAttachmentUploader uploader(transport.callbacks());
	QString failure;
	connect(&uploader, &ChatAttachmentUploader::failed, &uploader,
			[&](const QString &reason) { failure = reason; });
	QVERIFY(uploader.start({ source }));

	MumbleProto::ChatAssetState rejected;
	rejected.set_state(MumbleProto::ChatAssetTransferStateRejected);
	rejected.set_reason("quota exhausted");
	uploader.handleState(rejected);
	QVERIFY(!uploader.busy());
	QCOMPARE(failure, QStringLiteral("quota exhausted"));
	QCOMPARE(transport.inits.size(), 1);
	QVERIFY(transport.chunks.isEmpty());
	QVERIFY(transport.commits.isEmpty());
}

void TestChatAttachmentUploader::rejectsUnexpectedAcceptedSize() {
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	const Mumble::ChatAttachments::Source source =
		writeSource(dir, QStringLiteral("clip.mp4"), QByteArray("not-a-real-video"),
					Mumble::ChatAttachments::Kind::Video, QStringLiteral("video/mp4"));
	QVERIFY(!source.path.isEmpty());

	CapturedTransport transport;
	ChatAttachmentUploader uploader(transport.callbacks());
	QString failure;
	connect(&uploader, &ChatAttachmentUploader::failed, &uploader,
			[&](const QString &reason) { failure = reason; });
	QVERIFY(uploader.start({ source }));

	MumbleProto::ChatAssetState accepted;
	accepted.set_state(MumbleProto::ChatAssetTransferStateAccepted);
	accepted.set_upload_id(7);
	accepted.set_accepted_byte_size(source.byteSize + 1);
	uploader.handleState(accepted);
	QVERIFY(!uploader.busy());
	QVERIFY(failure.contains(QStringLiteral("unexpected attachment size"), Qt::CaseInsensitive));
	QVERIFY(transport.chunks.isEmpty());
	QVERIFY(transport.commits.isEmpty());
}

QTEST_GUILESS_MAIN(TestChatAttachmentUploader)
#include "TestChatAttachmentUploader.moc"
