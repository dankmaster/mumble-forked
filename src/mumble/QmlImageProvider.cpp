#include "QmlImageProvider.h"

#include <QtConcurrent/QtConcurrentRun>
#include <QtCore/QBuffer>
#include <QtCore/QCryptographicHash>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QFuture>
#include <QtCore/QFutureWatcher>
#include <QtCore/QPointer>
#include <QtCore/QUrl>
#include <QtCore/QUrlQuery>
#include <QtGui/QImageReader>
#include <QtQuick/QQuickTextureFactory>

namespace {
	class AsyncPipelineResponse final : public QQuickImageResponse {
	public:
		struct SharedState {
			std::atomic_bool cancelled = false;
			QMutex mutex;
			QImage image;
		};

		AsyncPipelineResponse(std::shared_ptr< QmlImagePipeline > pipeline, QString id, QSize size)
			: m_state(std::make_shared< SharedState >()) {
			auto *watcher = new QFutureWatcher< void >();
			QPointer< AsyncPipelineResponse > guardedThis(this);
			QObject::connect(watcher, &QFutureWatcher< void >::finished, watcher,
				[guardedThis, watcher, state = m_state]() {
					if (guardedThis && !state->cancelled.load()) emit guardedThis->finished();
					watcher->deleteLater();
				});
			watcher->setFuture(QtConcurrent::run(
				[pipeline = std::move(pipeline), id = std::move(id), size, state = m_state]() {
					QImage image = pipeline->loadForTest(id, size, std::shared_ptr< std::atomic_bool >(state, &state->cancelled));
					if (state->cancelled.load()) return;
					QMutexLocker locker(&state->mutex);
					state->image = std::move(image);
				}));
		}
		~AsyncPipelineResponse() override { cancel(); }
		QQuickTextureFactory *textureFactory() const override {
			QMutexLocker locker(&m_state->mutex);
			return m_state->image.isNull() ? nullptr : QQuickTextureFactory::textureFactoryForImage(m_state->image);
		}
		void cancel() override { m_state->cancelled.store(true); }
	private:
		std::shared_ptr< SharedState > m_state;
	};

	QString normalizedKey(const QString &key) {
		return QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256).toHex());
	}
}

QmlImagePipeline::QmlImagePipeline(Limits limits) : m_limits(limits) {}

QString QmlImagePipeline::makeUrl(const QString &key, quint64 generation) const {
	return QStringLiteral("image://mumble/%1?g=%2").arg(key).arg(generation);
}

QString QmlImagePipeline::registerEncoded(const QByteArray &bytes, const QByteArray &mimeType, const QString &stableKey) {
	if (stableKey.trimmed().isEmpty() || bytes.isEmpty() || bytes.size() > m_limits.maxEncodedBytes) return {};
	const QByteArray mime = mimeType.trimmed().toLower();
	if (!QList< QByteArray >{ "image/png", "image/jpeg", "image/jpg", "image/webp", "image/gif" }.contains(mime)) return {};
	const QString key = normalizedKey(stableKey);
	QMutexLocker locker(&m_mutex);
	Source &source = m_sources[key];
	if (source.bytes == bytes && source.mimeType == mime && source.path.isEmpty()) return makeUrl(key, source.generation);
	source = Source{ bytes, mime, {}, source.generation + 1 };
	return makeUrl(key, source.generation);
}

QString QmlImagePipeline::registerLocalFile(const QString &path, const QString &stableKey) {
	if (stableKey.trimmed().isEmpty()) return {};
	const QFileInfo info(path);
	if (!info.exists() || !info.isFile() || info.size() <= 0 || info.size() > m_limits.maxEncodedBytes) return {};
	QByteArray format = info.suffix().toLatin1().toLower(); if (format == "jpg") format = "jpeg";
	if (!QList< QByteArray >{ "png", "jpeg", "webp", "gif" }.contains(format)) return {};
	const QString key = normalizedKey(stableKey);
	QMutexLocker locker(&m_mutex);
	Source &source = m_sources[key];
	const QString canonical = info.canonicalFilePath();
	if (source.path == canonical && source.bytes.isEmpty()) return makeUrl(key, source.generation);
	source = Source{ {}, QByteArrayLiteral("image/") + format, canonical, source.generation + 1 };
	return makeUrl(key, source.generation);
}

void QmlImagePipeline::invalidate(const QString &stableKey) {
	QMutexLocker locker(&m_mutex);
	const QString key = normalizedKey(stableKey);
	if (auto it = m_sources.find(key); it != m_sources.end()) ++it->generation;
}

void QmlImagePipeline::clear() {
	QMutexLocker locker(&m_mutex); m_sources.clear(); m_cache.clear(); m_lru.clear(); m_cachedBytes = 0;
}

qint64 QmlImagePipeline::cachedBytes() const { QMutexLocker locker(&m_mutex); return m_cachedBytes; }
bool QmlImagePipeline::isCachedForTest(const QString &providerId, const QSize &requestedSize) const {
	const QString cacheKey = providerId + QStringLiteral("@%1x%2").arg(requestedSize.width()).arg(requestedSize.height());
	QMutexLocker locker(&m_mutex); return m_cache.contains(cacheKey);
}

void QmlImagePipeline::insertCache(const QString &cacheKey, const QImage &image) {
	const qint64 bytes = image.sizeInBytes();
	if (bytes <= 0 || bytes > m_limits.maxCacheBytes) return;
	QMutexLocker locker(&m_mutex);
	while (m_cachedBytes + bytes > m_limits.maxCacheBytes && !m_lru.isEmpty()) {
		const QString oldest = m_lru.takeFirst();
		m_cachedBytes -= m_cache.take(oldest).bytes;
	}
	if (const auto existing = m_cache.constFind(cacheKey); existing != m_cache.cend()) {
		m_cachedBytes -= existing->bytes;
	}
	m_cache.insert(cacheKey, CacheEntry{ image, bytes }); m_lru.removeAll(cacheKey); m_lru.push_back(cacheKey); m_cachedBytes += bytes;
}

QImage QmlImagePipeline::loadForTest(const QString &providerId, const QSize &requestedSize,
									 const std::shared_ptr< std::atomic_bool > &cancelled) {
	return load(providerId, requestedSize, cancelled);
}

QImage QmlImagePipeline::load(const QString &providerId, const QSize &requestedSize,
							 const std::shared_ptr< std::atomic_bool > &cancelled) {
	const QUrl url(QStringLiteral("image://mumble/") + providerId);
	const QString key = url.path().mid(1);
	bool ok = false; const quint64 generation = QUrlQuery(url).queryItemValue(QStringLiteral("g")).toULongLong(&ok);
	if (!ok) return {};
	const QString cacheKey = providerId + QStringLiteral("@%1x%2").arg(requestedSize.width()).arg(requestedSize.height());
	Source source;
	{
		QMutexLocker locker(&m_mutex);
		const auto it = m_sources.constFind(key); if (it == m_sources.cend() || it->generation != generation) return {}; source = *it;
		if (cancelled && cancelled->load()) return {};
		if (const auto cached = m_cache.constFind(cacheKey); cached != m_cache.cend()) {
			const QImage image = cached->image;
			m_lru.removeAll(cacheKey);
			m_lru.push_back(cacheKey);
			return image;
		}
	}
	if (cancelled && cancelled->load()) return {};
	QByteArray bytes = source.bytes;
	if (!source.path.isEmpty()) { QFile file(source.path); if (!file.open(QFile::ReadOnly)) return {}; bytes = file.read(m_limits.maxEncodedBytes + 1); }
	if (bytes.isEmpty() || bytes.size() > m_limits.maxEncodedBytes) return {};
	QBuffer buffer(&bytes); buffer.open(QIODevice::ReadOnly); QImageReader reader(&buffer);
	QByteArray format = source.mimeType.mid(QByteArrayLiteral("image/").size()); if (format == "jpg") format = "jpeg";
	reader.setFormat(format); reader.setDecideFormatFromContent(false); reader.setAutoTransform(true);
	const QSize sourceSize = reader.size();
	if (!sourceSize.isValid() || sourceSize.width() > m_limits.maxDimension || sourceSize.height() > m_limits.maxDimension
		|| qint64(sourceSize.width()) * sourceSize.height() > m_limits.maxDecodedPixels) return {};
	if (requestedSize.isValid()) { QSize scaled = sourceSize; scaled.scale(requestedSize, Qt::KeepAspectRatio); reader.setScaledSize(scaled); }
	QImage image = reader.read(); if (image.isNull() || (cancelled && cancelled->load())) return {};
	{
		QMutexLocker locker(&m_mutex); const auto it = m_sources.constFind(key); if (it == m_sources.cend() || it->generation != generation) return {};
	}
	insertCache(cacheKey, image); return image;
}

QmlAsyncImageProvider::QmlAsyncImageProvider(std::shared_ptr< QmlImagePipeline > pipeline) : m_pipeline(std::move(pipeline)) {}
QQuickImageResponse *QmlAsyncImageProvider::requestImageResponse(const QString &id, const QSize &requestedSize) {
	return new AsyncPipelineResponse(m_pipeline, id, requestedSize);
}
