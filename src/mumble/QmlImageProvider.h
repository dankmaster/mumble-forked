#ifndef MUMBLE_MUMBLE_QMLIMAGEPROVIDER_H_
#define MUMBLE_MUMBLE_QMLIMAGEPROVIDER_H_

#include <QtCore/QHash>
#include <QtCore/QMutex>
#include <QtCore/QStringList>
#include <QtGui/QImage>
#include <QtQuick/QQuickAsyncImageProvider>

#include <atomic>
#include <functional>
#include <memory>

class QObject;
class QmlImageRegistrationScheduler;
class QTemporaryDir;
class QmlImageScheduler;

class QmlImagePipeline final : public std::enable_shared_from_this< QmlImagePipeline > {
public:
	struct Limits {
		qint64 maxEncodedBytes = 8 * 1024 * 1024;
		qint64 maxDecodedPixels = 16 * 1024 * 1024;
		int maxDimension = 8192;
		qint64 maxFullResolutionEncodedBytes = 32 * 1024 * 1024;
		qint64 maxFullResolutionDecodedPixels = 40 * 1024 * 1024;
		int maxFullResolutionDimension = 16384;
		qint64 maxCacheBytes = 32 * 1024 * 1024;
		qint64 maxSourceBytes = 32 * 1024 * 1024;
		int maxAnimatedFrames = 128;
		qint64 maxAnimatedDecodedPixels = 16 * 1024 * 1024;
		int registrationConcurrency = 2;
		int maxPendingRegistrations = 16;
	};
	using RegistrationCallback = std::function< void(quint64 requestId, const QString &url) >;

	explicit QmlImagePipeline(Limits limits = {});
	~QmlImagePipeline();
	QString registerEncoded(const QByteArray &bytes, const QByteArray &mimeType, const QString &stableKey);
	QString registerFullResolutionEncoded(const QByteArray &bytes, const QByteArray &mimeType,
										  const QString &stableKey, QSize *pixelSize = nullptr);
	QString registerDataUrl(const QString &dataUrl, const QString &stableKey);
	QString registerImage(const QImage &image, const QString &stableKey);
	QString registerAnimatedEncoded(const QByteArray &bytes, const QByteArray &mimeType, const QString &stableKey);
	QString registerAnimatedDataUrl(const QString &dataUrl, const QString &stableKey);
	quint64 registerDataUrlAsync(const QString &dataUrl, const QString &stableKey, QObject *context,
								 RegistrationCallback callback);
	quint64 registerAnimatedDataUrlAsync(const QString &dataUrl, const QString &stableKey, QObject *context,
									 RegistrationCallback callback);
	quint64 registerAnimatedEncodedAsync(const QByteArray &bytes, const QByteArray &mimeType,
									 const QString &stableKey, QObject *context, RegistrationCallback callback);
	void cancelRegistration(quint64 requestId);
	QString registerLocalFile(const QString &path, const QString &stableKey);
	void invalidate(const QString &stableKey);
	void clear();
	QImage loadForTest(const QString &providerId, const QSize &requestedSize = {},
					   const std::shared_ptr< std::atomic_bool > &cancelled = {});
	qint64 cachedBytes() const;
	qint64 sourceBytes() const;
	int sourceCountForTest() const;
	int activeRegistrationCountForTest() const;
	int pendingRegistrationCountForTest() const;
	bool containsSource(const QString &url);
	bool isCachedForTest(const QString &providerId, const QSize &requestedSize = {}) const;

private:
	friend class QmlAsyncImageProvider;
	friend class QmlImageRegistrationScheduler;
	struct Source {
		QByteArray bytes;
		QByteArray mimeType;
		QString path;
		bool dataUrl = false;
		quint64 generation = 0;
		QImage image;
		QByteArray contentHash;
		qint64 storedBytes = 0;
		bool managedFile = false;
		bool fullResolution = false;
	};
	struct CacheEntry { QImage image; qint64 bytes = 0; };
	QImage load(const QString &providerId, const QSize &requestedSize,
				const std::shared_ptr< std::atomic_bool > &cancelled);
	quint64 reserveRegistrationGeneration(const QString &stableKey, QString *normalizedStableKey = nullptr);
	QString registerDataUrlForGeneration(const QString &dataUrl, const QString &stableKey,
									  const QString &normalizedStableKey, quint64 registrationGeneration,
									  const std::shared_ptr< std::atomic_bool > &cancelled = {});
	QString registerAnimatedDataUrlForGeneration(const QString &dataUrl, const QString &stableKey,
										  const QString &normalizedStableKey, quint64 registrationGeneration,
										  const std::shared_ptr< std::atomic_bool > &cancelled = {});
	QString registerAnimatedEncodedForGeneration(const QByteArray &bytes, const QByteArray &mimeType,
										  const QString &stableKey, const QString &normalizedStableKey,
										  quint64 registrationGeneration,
										  const std::shared_ptr< std::atomic_bool > &cancelled = {});
	QString makeUrl(const QString &key, quint64 generation) const;
	bool insertSourceLocked(const QString &key, Source source);
	void removeSourceLocked(const QString &key);
	void touchSourceLocked(const QString &key);
	void insertCache(const QString &cacheKey, const QImage &image);

	Limits m_limits;
	mutable QMutex m_mutex;
	QHash< QString, Source > m_sources;
	quint64 m_nextGeneration = 0;
	quint64 m_nextRegistrationGeneration = 0;
	QHash< QString, quint64 > m_registrationGenerationByKey;
	QStringList m_sourceLru;
	qint64 m_sourceBytes = 0;
	std::unique_ptr< QTemporaryDir > m_animationDirectory;
	QHash< QString, CacheEntry > m_cache;
	QStringList m_lru;
	qint64 m_cachedBytes = 0;
	std::unique_ptr< QmlImageRegistrationScheduler > m_registrationScheduler;
};

class QmlAsyncImageProvider final : public QQuickAsyncImageProvider {
public:
	explicit QmlAsyncImageProvider(std::shared_ptr< QmlImagePipeline > pipeline,
							   int concurrency = 4, int maximumPending = 64);
	~QmlAsyncImageProvider() override;
	QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;
	int activeRequestCountForTest() const;
	int pendingRequestCountForTest() const;
	quint64 uniqueRequestCountForTest() const;
private:
	std::shared_ptr< QmlImagePipeline > m_pipeline;
	std::unique_ptr< QmlImageScheduler > m_scheduler;
};

#endif
