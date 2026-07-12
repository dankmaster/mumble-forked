#ifndef MUMBLE_MUMBLE_QMLIMAGEPROVIDER_H_
#define MUMBLE_MUMBLE_QMLIMAGEPROVIDER_H_

#include <QtCore/QHash>
#include <QtCore/QMutex>
#include <QtCore/QStringList>
#include <QtGui/QImage>
#include <QtQuick/QQuickAsyncImageProvider>

#include <atomic>
#include <memory>

class QmlImagePipeline final {
public:
	struct Limits {
		qint64 maxEncodedBytes = 8 * 1024 * 1024;
		qint64 maxDecodedPixels = 16 * 1024 * 1024;
		int maxDimension = 8192;
		qint64 maxCacheBytes = 32 * 1024 * 1024;
	};

	explicit QmlImagePipeline(Limits limits = {});
	QString registerEncoded(const QByteArray &bytes, const QByteArray &mimeType, const QString &stableKey);
	QString registerDataUrl(const QString &dataUrl, const QString &stableKey);
	QString registerLocalFile(const QString &path, const QString &stableKey);
	void invalidate(const QString &stableKey);
	void clear();
	QImage loadForTest(const QString &providerId, const QSize &requestedSize = {},
					   const std::shared_ptr< std::atomic_bool > &cancelled = {});
	qint64 cachedBytes() const;
	bool isCachedForTest(const QString &providerId, const QSize &requestedSize = {}) const;

private:
	friend class QmlAsyncImageProvider;
	struct Source {
		QByteArray bytes;
		QByteArray mimeType;
		QString path;
		bool dataUrl = false;
		quint64 generation = 0;
	};
	struct CacheEntry { QImage image; qint64 bytes = 0; };
	QImage load(const QString &providerId, const QSize &requestedSize,
				const std::shared_ptr< std::atomic_bool > &cancelled);
	QString makeUrl(const QString &key, quint64 generation) const;
	void insertCache(const QString &cacheKey, const QImage &image);

	Limits m_limits;
	mutable QMutex m_mutex;
	QHash< QString, Source > m_sources;
	QHash< QString, CacheEntry > m_cache;
	QStringList m_lru;
	qint64 m_cachedBytes = 0;
};

class QmlAsyncImageProvider final : public QQuickAsyncImageProvider {
public:
	explicit QmlAsyncImageProvider(std::shared_ptr< QmlImagePipeline > pipeline);
	QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;
private:
	std::shared_ptr< QmlImagePipeline > m_pipeline;
};

#endif
