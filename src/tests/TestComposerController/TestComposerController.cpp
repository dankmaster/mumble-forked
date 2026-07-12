#include "ComposerController.h"
#include "QmlImageProvider.h"
#include <QtCore/QBuffer>
#include <QtCore/QFile>
#include <QtCore/QTemporaryDir>
#include <QtTest>

class TestComposerController : public QObject {
	Q_OBJECT
private slots: void preservesDraftUntilSuccessfulSend(); void removesReordersAndCompletes(); void retriesFailedAttachment(); };

void TestComposerController::preservesDraftUntilSuccessfulSend(){QTemporaryDir dir;QImage image(4,4,QImage::Format_ARGB32);image.fill(Qt::red);QFile file(dir.filePath("a.png"));QVERIFY(file.open(QFile::WriteOnly));QByteArray bytes;QBuffer buffer(&bytes);buffer.open(QIODevice::WriteOnly);image.save(&buffer,"PNG");file.write(bytes);file.close();auto pipeline=std::make_shared<QmlImagePipeline>();ComposerController controller(pipeline);controller.setCanSend(true);controller.setText("hello");controller.addUrls({QUrl::fromLocalFile(file.fileName())});QCOMPARE(controller.attachments()->rowCount(),1);QSignalSpy sent(&controller,&ComposerController::sendRequested);controller.send();QCOMPARE(sent.count(),1);QCOMPARE(controller.text(),QString("hello"));QCOMPARE(controller.attachments()->rowCount(),1);controller.finishSend(true);QVERIFY(controller.text().isEmpty());QCOMPARE(controller.attachments()->rowCount(),0);}
void TestComposerController::removesReordersAndCompletes(){auto pipeline=std::make_shared<QmlImagePipeline>();ComposerController controller(pipeline);controller.setAutocompleteSources({"Alice","Bob"},{"help","mute"});controller.setText("hi @Al");QCOMPARE(controller.autocompleteItems().size(),1);controller.complete("@Alice");QCOMPARE(controller.text(),QString("hi @Alice "));}
void TestComposerController::retriesFailedAttachment(){QTemporaryDir dir;QFile file(dir.filePath("unsupported.bmp"));QVERIFY(file.open(QFile::WriteOnly));file.write("BMunsupported");file.close();auto pipeline=std::make_shared<QmlImagePipeline>();ComposerController controller(pipeline);controller.addUrls({QUrl::fromLocalFile(file.fileName())});QCOMPARE(controller.attachments()->rowCount(),1);controller.addUrls({QUrl::fromLocalFile(file.fileName())});QCOMPARE(controller.attachments()->rowCount(),1);const QString id=controller.attachments()->data(controller.attachments()->index(0),DraftAttachmentModel::IdRole).toString();QCOMPARE(controller.attachments()->data(controller.attachments()->index(0),DraftAttachmentModel::StatusRole).toString(),QString("failed"));controller.retryAttachment(id);QCOMPARE(controller.attachments()->data(controller.attachments()->index(0),DraftAttachmentModel::StatusRole).toString(),QString("failed"));controller.addUrls({QUrl::fromLocalFile(dir.filePath("missing.png"))});QCOMPARE(controller.attachments()->rowCount(),1);}
QTEST_APPLESS_MAIN(TestComposerController)
#include "TestComposerController.moc"
