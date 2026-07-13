#include "QmlImageProvider.h"

#include <QtCore/QBuffer>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QTemporaryDir>
#include <QtCore/QUrl>
#include <QtCore/QUrlQuery>
#include <QtGui/QImageReader>
#include <QtGui/QMovie>
#include <QtTest>

class TestQmlImageProvider : public QObject {
	Q_OBJECT
private slots:
	void enforcesEncodedAndDimensionLimits();
	void cachesWithinBudget();
	void rejectsEmptyKeysAndRefreshesLruRecency();
	void rejectsStaleGenerationAndCancellation();
	void registersDecodedImageWithoutUiThreadEncoding();
	void boundsSourceStoreAndDataUrlPayloads();
	void preservesMultiFrameGifThroughManagedAnimationSource();
	void loadsLocalFileThroughPipeline();
	void neverUpscalesUntrustedRequests();
	void boundsAndCoalescesAsyncWork();
	void asyncImageProviderTeardownDoesNotWaitForDecode();
	void registersStaticAndAnimatedSourcesAsynchronously();
	void boundsAndSupersedesAsyncRegistrations();
	void sharedPipelineTeardownDoesNotWaitForRegistration();
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

void TestQmlImageProvider::registersDecodedImageWithoutUiThreadEncoding() {
	QmlImagePipeline::Limits limits;
	limits.maxDimension = 32;
	limits.maxDecodedPixels = 512;
	QmlImagePipeline pipeline(limits);
	QImage image(QSize(16, 8), QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::red);
	const QString first = pipeline.registerImage(image, QStringLiteral("preview:thumbnail"));
	QVERIFY(first.startsWith(QLatin1String("image://mumble/")));
	QCOMPARE(pipeline.registerImage(image, QStringLiteral("preview:thumbnail")), first);
	const QImage scaled = pipeline.loadForTest(providerId(first), QSize(8, 8));
	QCOMPARE(scaled.size(), QSize(8, 4));
	QCOMPARE(scaled.pixelColor(0, 0), QColor(Qt::red));

	image.fill(Qt::blue);
	const QString second = pipeline.registerImage(image, QStringLiteral("preview:thumbnail"));
	QVERIFY(second != first);
	QVERIFY(pipeline.loadForTest(providerId(first)).isNull());
	QCOMPARE(pipeline.loadForTest(providerId(second)).pixelColor(0, 0), QColor(Qt::blue));
	QVERIFY(pipeline.registerImage(image, QString()).isEmpty());
	QImage oversized(QSize(33, 1), QImage::Format_ARGB32_Premultiplied);
	oversized.fill(Qt::green);
	QVERIFY(pipeline.registerImage(oversized, QStringLiteral("oversized")).isEmpty());
	auto cancelled = std::make_shared< std::atomic_bool >(true);
	QVERIFY(pipeline.loadForTest(providerId(second), {}, cancelled).isNull());
}

void TestQmlImageProvider::boundsSourceStoreAndDataUrlPayloads() {
	QmlImagePipeline::Limits limits;
	limits.maxEncodedBytes = 512;
	limits.maxSourceBytes = 600;
	QmlImagePipeline pipeline(limits);
	const QString first = pipeline.registerEncoded(QByteArray(350, 'a'), "image/png", "source:first");
	const QString second = pipeline.registerEncoded(QByteArray(350, 'b'), "image/png", "source:second");
	QVERIFY(!first.isEmpty());
	QVERIFY(!second.isEmpty());
	QVERIFY(pipeline.sourceBytes() <= limits.maxSourceBytes);
	QCOMPARE(pipeline.sourceCountForTest(), 1);
	QVERIFY(!pipeline.containsSource(first));
	QVERIFY(pipeline.containsSource(second));

	QImage tooLargeForStore(QSize(16, 16), QImage::Format_ARGB32_Premultiplied);
	tooLargeForStore.fill(Qt::red);
	QVERIFY(pipeline.registerImage(tooLargeForStore, QStringLiteral("source:image")).isEmpty());

	QmlImagePipeline::Limits dataLimits;
	dataLimits.maxEncodedBytes = 6;
	dataLimits.maxSourceBytes = 64;
	QmlImagePipeline dataPipeline(dataLimits);
	QVERIFY(!dataPipeline.registerDataUrl(QStringLiteral("data:image/png;base64,QUJDREVG"),
												 QStringLiteral("data:within-ceiling")).isEmpty());
	QVERIFY(dataPipeline.registerDataUrl(QStringLiteral("data:image/png;base64,QUJDREVGR0hJ"),
										QStringLiteral("data:over-ceiling")).isEmpty());
	const QString oversizedDataUrl = QStringLiteral("data:image/png;base64,") + QString(256, QLatin1Char('A'));
	QVERIFY(dataPipeline.registerDataUrl(oversizedDataUrl, QStringLiteral("data:oversized-input")).isEmpty());
	const QString oversizedAnimatedDataUrl = QStringLiteral("data:image/gif;base64,") + QString(256, QLatin1Char('A'));
	QVERIFY(dataPipeline.registerAnimatedDataUrl(oversizedAnimatedDataUrl,
												QStringLiteral("data:oversized-animation")).isEmpty());

	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const QString firstPath = directory.filePath(QStringLiteral("first.png"));
	const QString secondPath = directory.filePath(QStringLiteral("second.png"));
	for (const QString &path : { firstPath, secondPath }) {
		QFile file(path);
		QVERIFY(file.open(QIODevice::WriteOnly));
		QCOMPARE(file.write(QByteArray(350, 'x')), qint64(350));
	}
	QmlImagePipeline::Limits localLimits;
	localLimits.maxEncodedBytes = 512;
	localLimits.maxSourceBytes = 600;
	QmlImagePipeline localPipeline(localLimits);
	const QString firstLocal = localPipeline.registerLocalFile(firstPath, QStringLiteral("local:first"));
	const QString secondLocal = localPipeline.registerLocalFile(secondPath, QStringLiteral("local:second"));
	QVERIFY(!firstLocal.isEmpty());
	QVERIFY(!secondLocal.isEmpty());
	QCOMPARE(localPipeline.sourceCountForTest(), 1);
	QVERIFY(!localPipeline.containsSource(firstLocal));
	QVERIFY(localPipeline.containsSource(secondLocal));
	QVERIFY(localPipeline.sourceBytes() <= localLimits.maxSourceBytes);
}

void TestQmlImageProvider::preservesMultiFrameGifThroughManagedAnimationSource() {
	const QByteArray gif = QByteArray::fromHex(
		"47494638396101000100800000000000ffffff"
		"21f90400050000002c0000000001000100000202440100"
		"21f90400050000002c00000000010001000002024c0100"
		"3b");
	QmlImagePipeline pipeline;
	const QString source = pipeline.registerAnimatedEncoded(gif, "image/gif", "animated:two-frame");
	QVERIFY(QUrl(source).isLocalFile());
	QVERIFY(pipeline.containsSource(source));
	QImageReader reader(QUrl(source).toLocalFile(), "gif");
	QVERIFY(reader.canRead());
	QCOMPARE(reader.imageCount(), 2);
	QMovie movie(QUrl(source).toLocalFile(), "gif");
	QVERIFY(movie.isValid());
	QCOMPARE(movie.frameCount(), 2);
	QVERIFY(movie.jumpToFrame(0));
	QVERIFY(movie.jumpToFrame(1));
}

void TestQmlImageProvider::loadsLocalFileThroughPipeline() {
	QTemporaryDir directory; QVERIFY(directory.isValid());
	const QString path = directory.filePath(QStringLiteral("local.png")); QFile file(path); QVERIFY(file.open(QFile::WriteOnly)); file.write(png(QSize(5, 3), Qt::blue)); file.close();
	QmlImagePipeline pipeline; const QString url = pipeline.registerLocalFile(path, QStringLiteral("attachment:1"));
	const QImage image = pipeline.loadForTest(providerId(url)); QCOMPARE(image.size(), QSize(5, 3));
}

void TestQmlImageProvider::neverUpscalesUntrustedRequests() {
	QmlImagePipeline pipeline;
	QImage decoded(QSize(32, 16), QImage::Format_ARGB32_Premultiplied);
	decoded.fill(Qt::red);
	const QString decodedUrl = pipeline.registerImage(decoded, QStringLiteral("bounded:decoded"));
	QCOMPARE(pipeline.loadForTest(providerId(decodedUrl), QSize(100000, 100000)).size(), QSize(32, 16));
	QCOMPARE(pipeline.loadForTest(providerId(decodedUrl), QSize(100000, 1)).size(), QSize(2, 1));

	const QString encodedUrl = pipeline.registerEncoded(png(QSize(40, 20), Qt::blue), "image/png",
		QStringLiteral("bounded:encoded"));
	QCOMPARE(pipeline.loadForTest(providerId(encodedUrl), QSize(100000, 100000)).size(), QSize(40, 20));
	QCOMPARE(pipeline.loadForTest(providerId(encodedUrl), QSize(1, 100000)).size(), QSize(1, 1));
	auto cancelled = std::make_shared< std::atomic_bool >(true);
	QVERIFY(pipeline.loadForTest(providerId(encodedUrl), QSize(100000, 100000), cancelled).isNull());
}

void TestQmlImageProvider::boundsAndCoalescesAsyncWork() {
	auto pipeline = std::make_shared< QmlImagePipeline >();
	const QString largeUrl = pipeline->registerEncoded(png(QSize(2048, 2048), Qt::darkBlue), "image/png",
		QStringLiteral("async:large"));
	QVERIFY(!largeUrl.isEmpty());
	QmlAsyncImageProvider provider(pipeline, 1, 2);
	const QString id = providerId(largeUrl);
	QQuickImageResponse *first = provider.requestImageResponse(id, QSize(512, 512));
	QQuickImageResponse *duplicate = provider.requestImageResponse(id, QSize(512, 512));
	QCOMPARE(provider.uniqueRequestCountForTest(), quint64(1));
	QVERIFY(provider.activeRequestCountForTest() <= 1);
	QVERIFY(provider.pendingRequestCountForTest() <= 2);

	QList< QQuickImageResponse * > extra;
	for (int index = 0; index < 8; ++index) {
		const QString url = pipeline->registerEncoded(png(QSize(512, 512), QColor::fromHsv(index * 30, 255, 180)),
			"image/png", QStringLiteral("async:%1").arg(index));
		extra.push_back(provider.requestImageResponse(providerId(url), QSize(256, 256)));
		QVERIFY(provider.activeRequestCountForTest() <= 1);
		QVERIFY(provider.pendingRequestCountForTest() <= 2);
	}
	duplicate->cancel();
	for (QQuickImageResponse *response : extra) response->cancel();
	first->cancel();
	qDeleteAll(extra);
	delete duplicate;
	delete first;
}

void TestQmlImageProvider::asyncImageProviderTeardownDoesNotWaitForDecode() {
	auto pipeline = std::make_shared< QmlImagePipeline >();
	const QString imageUrl = pipeline->registerEncoded(png(QSize(4096, 4096), Qt::darkBlue), "image/png",
		QStringLiteral("async:teardown"));
	QVERIFY(!imageUrl.isEmpty());
	auto *provider = new QmlAsyncImageProvider(pipeline, 1, 2);
	QQuickImageResponse *response = provider->requestImageResponse(providerId(imageUrl), QSize(4096, 4096));

	QElapsedTimer teardownTimer;
	teardownTimer.start();
	delete provider;
	QVERIFY2(teardownTimer.elapsed() < 100,
			 "Destroying the QML provider must not wait for an in-flight image decode");
	delete response;
	QTRY_COMPARE_WITH_TIMEOUT(pipeline.use_count(), long(1), 5000);
}

void TestQmlImageProvider::registersStaticAndAnimatedSourcesAsynchronously() {
	QmlImagePipeline pipeline;
	QHash< quint64, QString > results;
	const auto completed = [&results](const quint64 requestId, const QString &url) {
		results.insert(requestId, url);
	};

	const QByteArray pngBytes = png(QSize(9, 5), Qt::magenta);
	const QString pngDataUrl = QStringLiteral("data:image/png;base64,")
		+ QString::fromLatin1(pngBytes.toBase64());
	const quint64 pngRequest = pipeline.registerDataUrlAsync(
		pngDataUrl, QStringLiteral("registration:png"), this, completed);
	QVERIFY(pngRequest != 0);
	QTRY_VERIFY_WITH_TIMEOUT(results.contains(pngRequest), 5000);
	QVERIFY(!results.value(pngRequest).isEmpty());
	QVERIFY(pipeline.containsSource(results.value(pngRequest)));
	QCOMPARE(pipeline.loadForTest(providerId(results.value(pngRequest))).size(), QSize(9, 5));

	const QByteArray gif = QByteArray::fromHex(
		"47494638396101000100800000000000ffffff"
		"21f90400050000002c0000000001000100000202440100"
		"21f90400050000002c00000000010001000002024c0100"
		"3b");
	const QString gifDataUrl = QStringLiteral("data:image/gif;base64,")
		+ QString::fromLatin1(gif.toBase64());
	const quint64 gifRequest = pipeline.registerAnimatedDataUrlAsync(
		gifDataUrl, QStringLiteral("registration:gif-data"), this, completed);
	QVERIFY(gifRequest != 0);
	QTRY_VERIFY_WITH_TIMEOUT(results.contains(gifRequest), 5000);
	const QUrl gifUrl(results.value(gifRequest));
	QVERIFY(gifUrl.isLocalFile());
	QVERIFY(pipeline.containsSource(results.value(gifRequest)));
	QImageReader reader(gifUrl.toLocalFile(), "gif");
	QVERIFY(reader.canRead());
	QCOMPARE(reader.imageCount(), 2);

	const quint64 encodedGifRequest = pipeline.registerAnimatedEncodedAsync(
		gif, QByteArrayLiteral("image/gif"), QStringLiteral("registration:gif-encoded"), this, completed);
	QVERIFY(encodedGifRequest != 0);
	QTRY_VERIFY_WITH_TIMEOUT(results.contains(encodedGifRequest), 5000);
	QVERIFY(QUrl(results.value(encodedGifRequest)).isLocalFile());
}

void TestQmlImageProvider::boundsAndSupersedesAsyncRegistrations() {
	QmlImagePipeline::Limits limits;
	limits.registrationConcurrency = 1;
	limits.maxPendingRegistrations = 2;
	QmlImagePipeline pipeline(limits);
	QHash< quint64, QString > results;
	const auto completed = [&results](const quint64 requestId, const QString &url) {
		results.insert(requestId, url);
	};

	QList< quint64 > requests;
	for (int index = 0; index < 8; ++index) {
		const QByteArray bytes = png(QSize(64, 64), QColor::fromHsv(index * 30, 255, 180));
		const QString dataUrl = QStringLiteral("data:image/png;base64,")
			+ QString::fromLatin1(bytes.toBase64());
		const quint64 requestId = pipeline.registerDataUrlAsync(
			dataUrl, QStringLiteral("registration:bounded:%1").arg(index), this, completed);
		QVERIFY(requestId != 0);
		requests.push_back(requestId);
		QVERIFY(pipeline.activeRegistrationCountForTest() <= 1);
		QVERIFY(pipeline.pendingRegistrationCountForTest() <= 2);
	}
	QTRY_COMPARE_WITH_TIMEOUT(results.size(), requests.size(), 10000);
	QVERIFY(!results.value(requests.constLast()).isEmpty());
	QVERIFY(pipeline.containsSource(results.value(requests.constLast())));

	const QString firstDataUrl = QStringLiteral("data:image/png;base64,")
		+ QString::fromLatin1(png(QSize(16, 16), Qt::red).toBase64());
	const QString newestDataUrl = QStringLiteral("data:image/png;base64,")
		+ QString::fromLatin1(png(QSize(16, 16), Qt::green).toBase64());
	const quint64 firstRequest = pipeline.registerDataUrlAsync(
		firstDataUrl, QStringLiteral("registration:same"), this, completed);
	const quint64 newestRequest = pipeline.registerDataUrlAsync(
		newestDataUrl, QStringLiteral("registration:same"), this, completed);
	QVERIFY(firstRequest != 0);
	QVERIFY(newestRequest != 0);
	QTRY_VERIFY_WITH_TIMEOUT(results.contains(firstRequest) && results.contains(newestRequest), 5000);
	QVERIFY(!results.value(newestRequest).isEmpty());
	QVERIFY(pipeline.containsSource(results.value(newestRequest)));
	if (!results.value(firstRequest).isEmpty()) {
		QVERIFY(!pipeline.containsSource(results.value(firstRequest)));
	}
}

void TestQmlImageProvider::sharedPipelineTeardownDoesNotWaitForRegistration() {
	auto pipeline = std::make_shared< QmlImagePipeline >();
	std::weak_ptr< QmlImagePipeline > weakPipeline = pipeline;
	bool callbackDelivered = false;
	const QByteArray pngBytes = png(QSize(1024, 1024), Qt::darkCyan);
	const QString dataUrl = QStringLiteral("data:image/png;base64,")
		+ QString::fromLatin1(pngBytes.toBase64());
	const quint64 requestId = pipeline->registerDataUrlAsync(
		dataUrl, QStringLiteral("registration:deferred-teardown"), this,
		[&callbackDelivered](const quint64, const QString &) { callbackDelivered = true; });
	QVERIFY(requestId != 0);

	QElapsedTimer releaseTimer;
	releaseTimer.start();
	pipeline.reset();
	QVERIFY2(releaseTimer.elapsed() < 50,
			 "Dropping the product pipeline must not wait for sender-controlled image registration");
	QVERIFY(!weakPipeline.expired());
	QTRY_VERIFY_WITH_TIMEOUT(callbackDelivered, 5000);
	QTRY_VERIFY_WITH_TIMEOUT(weakPipeline.expired(), 5000);
}

QTEST_GUILESS_MAIN(TestQmlImageProvider)
#include "TestQmlImageProvider.moc"
