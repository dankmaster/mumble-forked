#include "QmlImageProvider.h"

#include <QtCore/QBuffer>
#include <QtCore/QFile>
#include <QtCore/QTemporaryDir>
#include <QtCore/QUrl>
#include <QtCore/QUrlQuery>
#include <QtTest>

class TestQmlImageProvider : public QObject {
	Q_OBJECT
private slots:
	void enforcesEncodedAndDimensionLimits();
	void cachesWithinBudget();
	void rejectsEmptyKeysAndRefreshesLruRecency();
	void rejectsStaleGenerationAndCancellation();
	void loadsLocalFileThroughPipeline();
private:
	QByteArray png(const QSize &size, const QColor &color) const;
	QString providerId(const QString &url) const;
};

QByteArray TestQmlImageProvider::png(const QSize &size, const QColor &color) const {
	QImage image(size, QImage::Format_ARGB32_Premultiplied); image.fill(color);
	QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly); image.save(&buffer, "PNG"); return bytes;
}

QString TestQmlImageProvider::providerId(const QString &value) const {
	const QUrl url(value); return url.path().mid(1) + QStringLiteral("?") + url.query();
}

void TestQmlImageProvider::enforcesEncodedAndDimensionLimits() {
	QmlImagePipeline::Limits limits; limits.maxEncodedBytes = 1024; limits.maxDimension = 32;
	QmlImagePipeline pipeline(limits);
	QVERIFY(pipeline.registerEncoded(QByteArray(1025, 'x'), "image/png", "large").isEmpty());
	const QString url = pipeline.registerEncoded(png(QSize(64, 2), Qt::red), "image/png", "wide");
	QVERIFY(!url.isEmpty());
	QVERIFY(pipeline.loadForTest(providerId(url)).isNull());
	QVERIFY(pipeline.registerEncoded(png(QSize(2, 2), Qt::red), "application/octet-stream", "mime").isEmpty());
}

void TestQmlImageProvider::cachesWithinBudget() {
	QmlImagePipeline::Limits limits; limits.maxCacheBytes = 1024;
	QmlImagePipeline pipeline(limits);
	for (int i = 0; i < 4; ++i) {
		const QString url = pipeline.registerEncoded(png(QSize(8, 8), QColor::fromRgb(i * 40, 20, 30)), "image/png", QString::number(i));
		QVERIFY(!pipeline.loadForTest(providerId(url)).isNull());
	}
	QVERIFY(pipeline.cachedBytes() <= limits.maxCacheBytes);
}

void TestQmlImageProvider::rejectsEmptyKeysAndRefreshesLruRecency() {
	QmlImagePipeline::Limits limits; limits.maxCacheBytes = 512;
	QmlImagePipeline pipeline(limits);
	QVERIFY(pipeline.registerEncoded(png(QSize(4, 4), Qt::red), "image/png", "").isEmpty());
	const QString a = pipeline.registerEncoded(png(QSize(8, 8), Qt::red), "image/png", "a");
	const QString b = pipeline.registerEncoded(png(QSize(8, 8), Qt::green), "image/png", "b");
	const QString c = pipeline.registerEncoded(png(QSize(8, 8), Qt::blue), "image/png", "c");
	const QString aid = providerId(a), bid = providerId(b), cid = providerId(c);
	QVERIFY(!pipeline.loadForTest(aid).isNull());
	QVERIFY(!pipeline.loadForTest(bid).isNull());
	QVERIFY(!pipeline.loadForTest(aid).isNull());
	QVERIFY(!pipeline.loadForTest(cid).isNull());
	QVERIFY(pipeline.isCachedForTest(aid));
	QVERIFY(!pipeline.isCachedForTest(bid));
	QVERIFY(pipeline.isCachedForTest(cid));
}

void TestQmlImageProvider::rejectsStaleGenerationAndCancellation() {
	QmlImagePipeline pipeline;
	const QString stale = pipeline.registerEncoded(png(QSize(4, 4), Qt::red), "image/png", "same");
	const QString current = pipeline.registerEncoded(png(QSize(4, 4), Qt::green), "image/png", "same");
	QVERIFY(pipeline.loadForTest(providerId(stale)).isNull());
	QVERIFY(!pipeline.loadForTest(providerId(current)).isNull());
	auto cancelled = std::make_shared< std::atomic_bool >(true);
	QVERIFY(pipeline.loadForTest(providerId(current), {}, cancelled).isNull());
}

void TestQmlImageProvider::loadsLocalFileThroughPipeline() {
	QTemporaryDir directory; QVERIFY(directory.isValid());
	const QString path = directory.filePath(QStringLiteral("local.png")); QFile file(path); QVERIFY(file.open(QFile::WriteOnly)); file.write(png(QSize(5, 3), Qt::blue)); file.close();
	QmlImagePipeline pipeline; const QString url = pipeline.registerLocalFile(path, QStringLiteral("attachment:1"));
	const QImage image = pipeline.loadForTest(providerId(url)); QCOMPARE(image.size(), QSize(5, 3));
}

QTEST_APPLESS_MAIN(TestQmlImageProvider)
#include "TestQmlImageProvider.moc"
