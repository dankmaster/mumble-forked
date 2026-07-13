#include "QmlImageProvider.h"

#include <QtCore/QBuffer>
#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QFuture>
#include <QtCore/QFutureWatcher>
#include <QtCore/QPromise>
#include <QtCore/QPointer>
#include <QtCore/QRegularExpression>
#include <QtCore/QRunnable>
#include <QtCore/QSaveFile>
#include <QtCore/QTemporaryDir>
#include <QtCore/QThread>
#include <QtCore/QThreadPool>
#include <QtCore/QUrl>
#include <QtCore/QUrlQuery>
#include <QtCore/QUuid>
#include <QtGui/QImageReader>
#include <QtQuick/QQuickTextureFactory>

#include <algorithm>
#include <deque>

class QmlImageScheduler final : public QObject {
public:
	struct Job;
	struct Subscription {
		QFuture< QImage > future;
		std::weak_ptr< Job > job;
	};

	explicit QmlImageScheduler(std::shared_ptr< QmlImagePipeline > pipeline, int concurrency = 4,
							   int maximumPending = 64)
		: m_pipeline(std::move(pipeline)),
		  m_maximumPending(std::max(1, maximumPending)) {
		m_pool.setMaxThreadCount(std::max(1, concurrency));
		m_pool.setExpiryTimeout(30000);
	}

	~QmlImageScheduler() override {
		shutdownNonBlocking();
		m_pool.waitForDone();
	}

	bool shutdownNonBlocking() {
		QList< std::shared_ptr< Job > > pending;
		bool idle = false;
		{
			QMutexLocker locker(&m_mutex);
			m_stopping = true;
			for (const auto &job : m_pending) {
				job->cancelled->store(true);
				job->finished = true;
				pending.push_back(job);
			}
			m_pending.clear();
			m_jobs.clear();
			for (const auto &job : m_runningJobs) job->cancelled->store(true);
			idle = m_runningJobs.isEmpty();
		}
		for (const auto &job : pending) finishJob(job, {});
		return idle;
	}

	void deleteWhenIdle() {
		bool idle = false;
		{
			QMutexLocker locker(&m_mutex);
			m_deleteWhenIdle = true;
			idle = m_runningJobs.isEmpty();
		}
		if (idle) QMetaObject::invokeMethod(this, &QObject::deleteLater, Qt::QueuedConnection);
	}

	Subscription request(const QString &id, const QSize &size) {
		QList< std::shared_ptr< Job > > evicted;
		Subscription result;
		{
			QMutexLocker locker(&m_mutex);
			const QString key = requestKey(id, size);
			if (!m_stopping) {
				if (const auto existing = m_jobs.value(key); existing && !existing->finished
					&& !existing->cancelled->load()) {
					++existing->subscribers;
					return Subscription{ existing->future, existing };
				}

				while (static_cast< int >(m_pending.size()) >= m_maximumPending) {
					const auto job = m_pending.front();
					m_pending.pop_front();
					job->cancelled->store(true);
					job->finished = true;
					if (m_jobs.value(job->key) == job) m_jobs.remove(job->key);
					evicted.push_back(job);
				}

				auto job = std::make_shared< Job >();
				job->key = key;
				job->id = id;
				job->size = size;
				job->cancelled = std::make_shared< std::atomic_bool >(false);
				job->promise.start();
				job->future = job->promise.future();
				job->subscribers = 1;
				m_jobs.insert(key, job);
				m_pending.push_back(job);
				++m_uniqueRequests;
				result = Subscription{ job->future, job };
				dispatchLocked();
			}
		}
		for (const auto &job : evicted) finishJob(job, {});
		if (!result.future.isValid()) {
			QPromise< QImage > promise;
			promise.start();
			result.future = promise.future();
			promise.addResult(QImage());
			promise.finish();
		}
		return result;
	}

	void unsubscribe(const std::weak_ptr< Job > &weakJob) {
		const auto job = weakJob.lock();
		if (!job) return;
		bool finishPending = false;
		{
			QMutexLocker locker(&m_mutex);
			if (job->subscribers > 0) --job->subscribers;
			if (job->subscribers != 0 || job->finished) return;
			job->cancelled->store(true);
			if (m_jobs.value(job->key) == job) m_jobs.remove(job->key);
			if (!job->running) {
				const auto it = std::find(m_pending.begin(), m_pending.end(), job);
				if (it != m_pending.end()) m_pending.erase(it);
				job->finished = true;
				finishPending = true;
			}
		}
		if (finishPending) finishJob(job, {});
	}

	int activeCount() const {
		QMutexLocker locker(&m_mutex);
		return m_runningJobs.size();
	}
	int pendingCount() const {
		QMutexLocker locker(&m_mutex);
		return static_cast< int >(m_pending.size());
	}
	quint64 uniqueRequestCount() const {
		QMutexLocker locker(&m_mutex);
		return m_uniqueRequests;
	}

	struct Job {
		QString key;
		QString id;
		QSize size;
		std::shared_ptr< std::atomic_bool > cancelled;
		QPromise< QImage > promise;
		QFuture< QImage > future;
		int subscribers = 0;
		bool running = false;
		bool finished = false;
	};

private:
	static QString requestKey(const QString &id, const QSize &size) {
		return id + QStringLiteral("@%1x%2").arg(size.width()).arg(size.height());
	}

	void dispatchLocked() {
		while (!m_stopping && m_runningJobs.size() < m_pool.maxThreadCount() && !m_pending.empty()) {
			// Prefer the newest request. During a fast ListView fling these are the
			// delegates that are still visible; stale queued requests are completed
			// empty when the bounded queue fills.
			const auto job = m_pending.back();
			m_pending.pop_back();
			job->running = true;
			m_runningJobs.push_back(job);
			m_pool.start(QRunnable::create([this, job]() {
				QImage image;
				if (!job->cancelled->load()) image = m_pipeline->loadForTest(job->id, job->size, job->cancelled);
				complete(job, std::move(image));
			}));
		}
	}

	void complete(const std::shared_ptr< Job > &job, QImage image) {
		bool deleteWhenIdle = false;
		{
			QMutexLocker locker(&m_mutex);
			m_runningJobs.removeAll(job);
			job->running = false;
			job->finished = true;
			if (m_jobs.value(job->key) == job) m_jobs.remove(job->key);
			dispatchLocked();
			deleteWhenIdle = m_stopping && m_deleteWhenIdle && m_runningJobs.isEmpty();
		}
		if (job->cancelled->load()) image = {};
		finishJob(job, std::move(image));
		if (deleteWhenIdle) QMetaObject::invokeMethod(this, &QObject::deleteLater, Qt::QueuedConnection);
	}

	static void finishJob(const std::shared_ptr< Job > &job, QImage image) {
		job->promise.addResult(std::move(image));
		job->promise.finish();
	}

	std::shared_ptr< QmlImagePipeline > m_pipeline;
	mutable QMutex m_mutex;
	QThreadPool m_pool;
	const int m_maximumPending;
	std::deque< std::shared_ptr< Job > > m_pending;
	QHash< QString, std::shared_ptr< Job > > m_jobs;
	QList< std::shared_ptr< Job > > m_runningJobs;
	quint64 m_uniqueRequests = 0;
	bool m_stopping = false;
	bool m_deleteWhenIdle = false;
};

namespace {
	class AsyncPipelineResponse final : public QQuickImageResponse {
	public:
		AsyncPipelineResponse(QmlImageScheduler *scheduler, const QString &id, const QSize &size)
			: m_scheduler(scheduler), m_subscription(scheduler->request(id, size)) {
			auto *watcher = new QFutureWatcher< QImage >(this);
			QObject::connect(watcher, &QFutureWatcher< QImage >::finished, this, [this, watcher]() {
				if (m_cancelled.load()) return;
				QMutexLocker locker(&m_mutex);
				if (watcher->future().resultCount() > 0) m_image = watcher->result();
				locker.unlock();
				emit finished();
			});
			watcher->setFuture(m_subscription.future);
		}
		~AsyncPipelineResponse() override { cancel(); }
		QQuickTextureFactory *textureFactory() const override {
			QMutexLocker locker(&m_mutex);
			return m_image.isNull() ? nullptr : QQuickTextureFactory::textureFactoryForImage(m_image);
		}
		void cancel() override {
			if (m_cancelled.exchange(true)) return;
			if (m_scheduler) m_scheduler->unsubscribe(m_subscription.job);
		}
	private:
		QPointer< QmlImageScheduler > m_scheduler;
		QmlImageScheduler::Subscription m_subscription;
		std::atomic_bool m_cancelled = false;
		mutable QMutex m_mutex;
		QImage m_image;
	};

	QString normalizedKey(const QString &key) {
		return QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256).toHex());
	}
}

class QmlImageRegistrationScheduler final : public QObject {
public:
	enum class Kind { DataUrl, AnimatedDataUrl, AnimatedEncoded };

	explicit QmlImageRegistrationScheduler(QmlImagePipeline *pipeline, const int concurrency,
										   const int maximumPending)
		: m_pipeline(pipeline), m_maximumPending(std::max(1, maximumPending)) {
		m_pool.setMaxThreadCount(std::max(1, concurrency));
		m_pool.setExpiryTimeout(30000);
	}

	~QmlImageRegistrationScheduler() override {
		{
			QMutexLocker locker(&m_mutex);
			m_stopping = true;
			for (const auto &job : m_jobs) job->cancelled->store(true);
			m_pending.clear();
			m_jobs.clear();
			m_latestJobByStableKey.clear();
		}
		m_pool.waitForDone();
		QMutexLocker locker(&m_mutex);
		m_running.clear();
	}

	quint64 request(const Kind kind, const QString &dataUrl, const QByteArray &bytes, const QByteArray &mimeType,
					  const QString &stableKey, QObject *context, QmlImagePipeline::RegistrationCallback callback) {
		if (!m_pipeline || stableKey.trimmed().isEmpty() || !context || !callback
			|| context->thread() != thread() || QThread::currentThread() != thread()) {
			return 0;
		}

		QString normalizedStableKey;
		const quint64 registrationGeneration =
			m_pipeline->reserveRegistrationGeneration(stableKey, &normalizedStableKey);
		if (registrationGeneration == 0 || normalizedStableKey.isEmpty()) return 0;

		QList< std::shared_ptr< Job > > completedEmpty;
		std::shared_ptr< Job > job = std::make_shared< Job >();
		{
			QMutexLocker locker(&m_mutex);
			if (m_stopping) return 0;
			if (++m_nextRequestId == 0) ++m_nextRequestId;
			job->requestId = m_nextRequestId;
			job->kind = kind;
			job->dataUrl = dataUrl;
			job->bytes = bytes;
			job->mimeType = mimeType;
			job->stableKey = stableKey;
			job->normalizedStableKey = normalizedStableKey;
			job->registrationGeneration = registrationGeneration;
			job->context = context;
			job->callback = std::move(callback);
			job->cancelled = std::make_shared< std::atomic_bool >(false);
			// Product pipelines are shared-owned. Holding that owner in every async
			// job defers pipeline/scheduler destruction until the worker has completed,
			// so the UI thread never has to wait in QThreadPool::waitForDone(). Stack-
			// allocated test pipelines retain the synchronous destructor safety net.
			job->pipelineOwner = m_pipeline->weak_from_this().lock();

			if (const auto previous = m_latestJobByStableKey.value(normalizedStableKey)) {
				previous->cancelled->store(true);
				const auto pendingIt = std::find(m_pending.begin(), m_pending.end(), previous);
				if (pendingIt != m_pending.end()) {
					m_pending.erase(pendingIt);
					m_jobs.remove(previous->requestId);
					completedEmpty.push_back(previous);
				}
			}

			while (static_cast< int >(m_pending.size()) >= m_maximumPending) {
				const auto evicted = m_pending.front();
				m_pending.pop_front();
				evicted->cancelled->store(true);
				m_jobs.remove(evicted->requestId);
				if (m_latestJobByStableKey.value(evicted->normalizedStableKey) == evicted)
					m_latestJobByStableKey.remove(evicted->normalizedStableKey);
				completedEmpty.push_back(evicted);
			}

			m_jobs.insert(job->requestId, job);
			m_latestJobByStableKey.insert(normalizedStableKey, job);
			m_pending.push_back(job);
			dispatchLocked();
		}

		for (const auto &completed : completedEmpty) deliver(completed, {});
		return job->requestId;
	}

	void cancel(const quint64 requestId) {
		std::shared_ptr< Job > completed;
		{
			QMutexLocker locker(&m_mutex);
			const auto job = m_jobs.value(requestId);
			if (!job) return;
			job->cancelled->store(true);
			const auto pendingIt = std::find(m_pending.begin(), m_pending.end(), job);
			if (pendingIt != m_pending.end()) {
				m_pending.erase(pendingIt);
				m_jobs.remove(requestId);
				if (m_latestJobByStableKey.value(job->normalizedStableKey) == job)
					m_latestJobByStableKey.remove(job->normalizedStableKey);
				completed = job;
			}
		}
		if (completed) deliver(completed, {});
	}

	int activeCount() const {
		QMutexLocker locker(&m_mutex);
		return m_running.size();
	}

	int pendingCount() const {
		QMutexLocker locker(&m_mutex);
		return static_cast< int >(m_pending.size());
	}

private:
	struct Job {
		quint64 requestId = 0;
		Kind kind = Kind::DataUrl;
		QString dataUrl;
		QByteArray bytes;
		QByteArray mimeType;
		QString stableKey;
		QString normalizedStableKey;
		quint64 registrationGeneration = 0;
		QPointer< QObject > context;
		QmlImagePipeline::RegistrationCallback callback;
		std::shared_ptr< std::atomic_bool > cancelled;
		std::shared_ptr< QmlImagePipeline > pipelineOwner;
	};

	void dispatchLocked() {
		while (!m_stopping && m_running.size() < m_pool.maxThreadCount() && !m_pending.empty()) {
			const auto job = m_pending.back();
			m_pending.pop_back();
			m_running.push_back(job);
			m_pool.start(QRunnable::create([this, job] {
				QString result;
				if (!job->cancelled->load()) {
					switch (job->kind) {
						case Kind::DataUrl:
							result = m_pipeline->registerDataUrlForGeneration(
								job->dataUrl, job->stableKey, job->normalizedStableKey,
								job->registrationGeneration, job->cancelled);
							break;
						case Kind::AnimatedDataUrl:
							result = m_pipeline->registerAnimatedDataUrlForGeneration(
								job->dataUrl, job->stableKey, job->normalizedStableKey,
								job->registrationGeneration, job->cancelled);
							break;
						case Kind::AnimatedEncoded:
							result = m_pipeline->registerAnimatedEncodedForGeneration(
								job->bytes, job->mimeType, job->stableKey, job->normalizedStableKey,
								job->registrationGeneration, job->cancelled);
							break;
					}
				}
				QMetaObject::invokeMethod(this, [this, job, result = std::move(result)]() mutable {
					complete(job, std::move(result));
				}, Qt::QueuedConnection);
			}));
		}
	}

	void complete(const std::shared_ptr< Job > &job, QString result) {
		{
			QMutexLocker locker(&m_mutex);
			if (m_stopping) return;
			m_running.removeAll(job);
			m_jobs.remove(job->requestId);
			if (m_latestJobByStableKey.value(job->normalizedStableKey) == job)
				m_latestJobByStableKey.remove(job->normalizedStableKey);
			dispatchLocked();
		}
		if (job->cancelled->load()) result.clear();
		deliver(job, std::move(result));
	}

	static void deliver(const std::shared_ptr< Job > &job, QString result) {
		QObject *context = job->context.data();
		if (!context || !job->callback) return;
		const quint64 requestId = job->requestId;
		auto callback = job->callback;
		QPointer< QObject > guardedContext(context);
		QMetaObject::invokeMethod(context,
			[guardedContext, callback = std::move(callback), requestId, result = std::move(result)]() mutable {
				if (guardedContext) callback(requestId, result);
			}, Qt::QueuedConnection);
	}

	QmlImagePipeline *m_pipeline = nullptr;
	mutable QMutex m_mutex;
	QThreadPool m_pool;
	const int m_maximumPending;
	std::deque< std::shared_ptr< Job > > m_pending;
	QList< std::shared_ptr< Job > > m_running;
	QHash< quint64, std::shared_ptr< Job > > m_jobs;
	QHash< QString, std::shared_ptr< Job > > m_latestJobByStableKey;
	quint64 m_nextRequestId = 0;
	bool m_stopping = false;
};

QmlImagePipeline::QmlImagePipeline(Limits limits)
	: m_limits(limits),
	  m_animationDirectory(std::make_unique< QTemporaryDir >(
		  QDir::tempPath() + QStringLiteral("/mumble-qml-images-XXXXXX"))),
	  m_registrationScheduler(std::make_unique< QmlImageRegistrationScheduler >(
		  this, limits.registrationConcurrency, limits.maxPendingRegistrations)) {}

QmlImagePipeline::~QmlImagePipeline() {
	m_registrationScheduler.reset();
}

QString QmlImagePipeline::makeUrl(const QString &key, quint64 generation) const {
	return QStringLiteral("image://mumble/%1?g=%2").arg(key).arg(generation);
}

quint64 QmlImagePipeline::reserveRegistrationGeneration(const QString &stableKey,
												 QString *normalizedStableKey) {
	const QString trimmedKey = stableKey.trimmed();
	if (trimmedKey.isEmpty()) return 0;
	const QString key = normalizedKey(trimmedKey);
	QMutexLocker locker(&m_mutex);
	if (++m_nextRegistrationGeneration == 0) ++m_nextRegistrationGeneration;
	m_registrationGenerationByKey.insert(key, m_nextRegistrationGeneration);
	if (normalizedStableKey) *normalizedStableKey = key;
	return m_nextRegistrationGeneration;
}

void QmlImagePipeline::touchSourceLocked(const QString &key) {
	m_sourceLru.removeAll(key);
	m_sourceLru.push_back(key);
}

void QmlImagePipeline::removeSourceLocked(const QString &key) {
	const auto it = m_sources.find(key);
	if (it == m_sources.end()) return;
	m_sourceBytes -= it->storedBytes;
	if (it->managedFile && !it->path.isEmpty()) QFile::remove(it->path);
	m_sources.erase(it);
	m_sourceLru.removeAll(key);
}

bool QmlImagePipeline::insertSourceLocked(const QString &key, Source source) {
	if (source.storedBytes <= 0 || source.storedBytes > m_limits.maxSourceBytes) return false;
	removeSourceLocked(key);
	while (m_sourceBytes + source.storedBytes > m_limits.maxSourceBytes && !m_sourceLru.isEmpty()) {
		removeSourceLocked(m_sourceLru.front());
	}
	if (m_sourceBytes + source.storedBytes > m_limits.maxSourceBytes) return false;
	if (++m_nextGeneration == 0) ++m_nextGeneration;
	source.generation = m_nextGeneration;
	m_sourceBytes += source.storedBytes;
	m_sources.insert(key, std::move(source));
	touchSourceLocked(key);
	return true;
}

QString QmlImagePipeline::registerEncoded(const QByteArray &bytes, const QByteArray &mimeType, const QString &stableKey) {
	if (stableKey.trimmed().isEmpty() || bytes.isEmpty() || bytes.size() > m_limits.maxEncodedBytes) return {};
	const QByteArray mime = mimeType.trimmed().toLower();
	if (!QList< QByteArray >{ "image/png", "image/jpeg", "image/jpg", "image/webp", "image/gif" }.contains(mime)) return {};
	QString key;
	const quint64 registrationGeneration = reserveRegistrationGeneration(stableKey, &key);
	if (registrationGeneration == 0) return {};
	QMutexLocker locker(&m_mutex);
	if (m_registrationGenerationByKey.value(key) != registrationGeneration) return {};
	if (const auto it = m_sources.constFind(key); it != m_sources.cend()
		&& it->bytes == bytes && it->mimeType == mime && it->path.isEmpty() && it->image.isNull()) {
		touchSourceLocked(key);
		return makeUrl(key, it->generation);
	}
	Source source;
	source.bytes       = bytes;
	source.mimeType    = mime;
	source.storedBytes = bytes.size();
	if (!insertSourceLocked(key, std::move(source))) return {};
	return makeUrl(key, m_sources.value(key).generation);
}

QString QmlImagePipeline::registerDataUrl(const QString &dataUrl, const QString &stableKey) {
	QString key;
	const quint64 registrationGeneration = reserveRegistrationGeneration(stableKey, &key);
	if (registrationGeneration == 0) return {};
	return registerDataUrlForGeneration(dataUrl, stableKey, key, registrationGeneration);
}

QString QmlImagePipeline::registerDataUrlForGeneration(
	const QString &dataUrl, const QString &stableKey, const QString &normalizedStableKey,
	const quint64 registrationGeneration, const std::shared_ptr< std::atomic_bool > &cancelled) {
	if (stableKey.trimmed().isEmpty() || normalizedStableKey.isEmpty() || registrationGeneration == 0
		|| (cancelled && cancelled->load())) return {};
	const qint64 maximumBase64Characters = ((m_limits.maxEncodedBytes + 2) / 3) * 4;
	// Reject oversized input before trimming or running the regular expression.
	// Data URLs can originate from chat content, so parsing an attacker-sized
	// QString would otherwise consume UI-thread CPU and allocate a second copy.
	if (dataUrl.size() > maximumBase64Characters + 128) return {};
	static const QRegularExpression expression(QStringLiteral("^data:(image/(?:png|jpeg|jpg|webp|gif));base64,([A-Za-z0-9+/=\\r\\n]+)$"), QRegularExpression::CaseInsensitiveOption);
	const QRegularExpressionMatch match = expression.match(dataUrl.trimmed());
	if (!match.hasMatch()) return {};
	QByteArray encoded = match.captured(2).toLatin1();
	encoded.replace("\r", "");
	encoded.replace("\n", "");
	if (encoded.isEmpty() || encoded.size() > maximumBase64Characters) return {};
	const QByteArray mime = match.captured(1).toLatin1().toLower();
	if (cancelled && cancelled->load()) return {};
	QMutexLocker locker(&m_mutex);
	if (m_registrationGenerationByKey.value(normalizedStableKey) != registrationGeneration
		|| (cancelled && cancelled->load())) return {};
	if (const auto it = m_sources.constFind(normalizedStableKey); it != m_sources.cend()
		&& it->bytes == encoded && it->mimeType == mime && it->dataUrl) {
		touchSourceLocked(normalizedStableKey);
		return makeUrl(normalizedStableKey, it->generation);
	}
	Source source;
	source.bytes       = encoded;
	source.mimeType    = mime;
	source.dataUrl     = true;
	source.storedBytes = encoded.size();
	if (!insertSourceLocked(normalizedStableKey, std::move(source))) return {};
	return makeUrl(normalizedStableKey, m_sources.value(normalizedStableKey).generation);
}

QString QmlImagePipeline::registerImage(const QImage &image, const QString &stableKey) {
	if (stableKey.trimmed().isEmpty() || image.isNull() || image.width() > m_limits.maxDimension
		|| image.height() > m_limits.maxDimension
		|| qint64(image.width()) * image.height() > m_limits.maxDecodedPixels) {
		return {};
	}

	QString key;
	const quint64 registrationGeneration = reserveRegistrationGeneration(stableKey, &key);
	if (registrationGeneration == 0) return {};
	QMutexLocker locker(&m_mutex);
	if (m_registrationGenerationByKey.value(key) != registrationGeneration) return {};
	if (const auto it = m_sources.constFind(key); it != m_sources.cend()
		&& !it->image.isNull() && it->image.cacheKey() == image.cacheKey()) {
		touchSourceLocked(key);
		return makeUrl(key, it->generation);
	}
	Source source;
	source.image = image;
	source.storedBytes = image.sizeInBytes();
	if (!insertSourceLocked(key, std::move(source))) return {};
	return makeUrl(key, m_sources.value(key).generation);
}

QString QmlImagePipeline::registerAnimatedEncoded(const QByteArray &bytes, const QByteArray &mimeType,
												  const QString &stableKey) {
	QString key;
	const quint64 registrationGeneration = reserveRegistrationGeneration(stableKey, &key);
	if (registrationGeneration == 0) return {};
	return registerAnimatedEncodedForGeneration(bytes, mimeType, stableKey, key, registrationGeneration);
}

QString QmlImagePipeline::registerAnimatedEncodedForGeneration(
	const QByteArray &bytes, const QByteArray &mimeType, const QString &stableKey,
	const QString &normalizedStableKey, const quint64 registrationGeneration,
	const std::shared_ptr< std::atomic_bool > &cancelled) {
	if (stableKey.trimmed().isEmpty() || bytes.isEmpty() || bytes.size() > m_limits.maxEncodedBytes
		|| bytes.size() > m_limits.maxSourceBytes || mimeType.trimmed().toLower() != QByteArrayLiteral("image/gif")
		|| normalizedStableKey.isEmpty() || registrationGeneration == 0
		|| !m_animationDirectory || !m_animationDirectory->isValid() || (cancelled && cancelled->load())) {
		return {};
	}
	QBuffer buffer;
	buffer.setData(bytes);
	buffer.open(QIODevice::ReadOnly);
	QImageReader reader(&buffer, "gif");
	reader.setDecideFormatFromContent(false);
	if (!reader.canRead()) return {};
	const QSize size = reader.size();
	const int frameCount = reader.imageCount();
	if (!size.isValid() || size.width() > m_limits.maxDimension
		|| size.height() > m_limits.maxDimension || qint64(size.width()) * size.height() > m_limits.maxDecodedPixels
		|| frameCount <= 0 || frameCount > m_limits.maxAnimatedFrames
		|| qint64(size.width()) * size.height() * frameCount > m_limits.maxAnimatedDecodedPixels) {
		return {};
	}

	if (cancelled && cancelled->load()) return {};
	const QByteArray contentHash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
	{
		QMutexLocker locker(&m_mutex);
		if (m_registrationGenerationByKey.value(normalizedStableKey) != registrationGeneration
			|| (cancelled && cancelled->load())) return {};
		if (const auto it = m_sources.constFind(normalizedStableKey); it != m_sources.cend() && it->managedFile
			&& it->contentHash == contentHash && QFileInfo::exists(it->path)) {
			touchSourceLocked(normalizedStableKey);
			return QUrl::fromLocalFile(it->path).toString(QUrl::FullyEncoded);
		}
	}

	const QString path = m_animationDirectory->filePath(
		QStringLiteral("%1-%2.gif").arg(normalizedStableKey, QUuid::createUuid().toString(QUuid::WithoutBraces)));
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) return {};
	if (cancelled && cancelled->load()) {
		QFile::remove(path);
		return {};
	}

	QMutexLocker locker(&m_mutex);
	if (m_registrationGenerationByKey.value(normalizedStableKey) != registrationGeneration
		|| (cancelled && cancelled->load())) {
		locker.unlock();
		QFile::remove(path);
		return {};
	}
	if (const auto it = m_sources.constFind(normalizedStableKey); it != m_sources.cend() && it->managedFile
		&& it->contentHash == contentHash && QFileInfo::exists(it->path)) {
		const QString existingPath = it->path;
		touchSourceLocked(normalizedStableKey);
		locker.unlock();
		QFile::remove(path);
		return QUrl::fromLocalFile(existingPath).toString(QUrl::FullyEncoded);
	}
	Source source;
	source.mimeType    = QByteArrayLiteral("image/gif");
	source.path        = path;
	source.contentHash = contentHash;
	source.storedBytes = bytes.size();
	source.managedFile = true;
	if (!insertSourceLocked(normalizedStableKey, std::move(source))) {
		locker.unlock();
		QFile::remove(path);
		return {};
	}
	return QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded);
}

QString QmlImagePipeline::registerAnimatedDataUrl(const QString &dataUrl, const QString &stableKey) {
	QString key;
	const quint64 registrationGeneration = reserveRegistrationGeneration(stableKey, &key);
	if (registrationGeneration == 0) return {};
	return registerAnimatedDataUrlForGeneration(dataUrl, stableKey, key, registrationGeneration);
}

QString QmlImagePipeline::registerAnimatedDataUrlForGeneration(
	const QString &dataUrl, const QString &stableKey, const QString &normalizedStableKey,
	const quint64 registrationGeneration, const std::shared_ptr< std::atomic_bool > &cancelled) {
	if (stableKey.trimmed().isEmpty() || normalizedStableKey.isEmpty() || registrationGeneration == 0
		|| (cancelled && cancelled->load())) return {};
	const qint64 maximumBase64Characters = ((m_limits.maxEncodedBytes + 2) / 3) * 4;
	if (dataUrl.size() > maximumBase64Characters + 128) return {};
	static const QRegularExpression expression(QStringLiteral("^data:(image/gif);base64,([A-Za-z0-9+/=\\r\\n]+)$"),
												QRegularExpression::CaseInsensitiveOption);
	const QRegularExpressionMatch match = expression.match(dataUrl.trimmed());
	if (!match.hasMatch()) return {};
	QByteArray encoded = match.captured(2).toLatin1();
	encoded.replace("\r", "");
	encoded.replace("\n", "");
	if (encoded.isEmpty() || encoded.size() > maximumBase64Characters) return {};
	const QByteArray bytes = QByteArray::fromBase64(encoded, QByteArray::AbortOnBase64DecodingErrors);
	if (bytes.isEmpty() || (cancelled && cancelled->load())) return {};
	return registerAnimatedEncodedForGeneration(bytes, QByteArrayLiteral("image/gif"), stableKey,
											 normalizedStableKey, registrationGeneration, cancelled);
}

quint64 QmlImagePipeline::registerDataUrlAsync(const QString &dataUrl, const QString &stableKey,
												QObject *context, RegistrationCallback callback) {
	const qint64 maximumBase64Characters = ((m_limits.maxEncodedBytes + 2) / 3) * 4;
	if (!m_registrationScheduler || dataUrl.isEmpty() || dataUrl.size() > maximumBase64Characters + 128) return 0;
	return m_registrationScheduler->request(QmlImageRegistrationScheduler::Kind::DataUrl, dataUrl, {}, {},
		stableKey, context, std::move(callback));
}

quint64 QmlImagePipeline::registerAnimatedDataUrlAsync(const QString &dataUrl, const QString &stableKey,
														QObject *context, RegistrationCallback callback) {
	const qint64 maximumBase64Characters = ((m_limits.maxEncodedBytes + 2) / 3) * 4;
	if (!m_registrationScheduler || dataUrl.isEmpty() || dataUrl.size() > maximumBase64Characters + 128) return 0;
	return m_registrationScheduler->request(QmlImageRegistrationScheduler::Kind::AnimatedDataUrl, dataUrl, {}, {},
		stableKey, context, std::move(callback));
}

quint64 QmlImagePipeline::registerAnimatedEncodedAsync(const QByteArray &bytes, const QByteArray &mimeType,
														 const QString &stableKey, QObject *context,
														 RegistrationCallback callback) {
	if (!m_registrationScheduler || bytes.isEmpty() || bytes.size() > m_limits.maxEncodedBytes
		|| bytes.size() > m_limits.maxSourceBytes) return 0;
	return m_registrationScheduler->request(QmlImageRegistrationScheduler::Kind::AnimatedEncoded, {}, bytes,
		mimeType, stableKey, context, std::move(callback));
}

void QmlImagePipeline::cancelRegistration(const quint64 requestId) {
	if (m_registrationScheduler && requestId != 0) m_registrationScheduler->cancel(requestId);
}

QString QmlImagePipeline::registerLocalFile(const QString &path, const QString &stableKey) {
	QString key;
	const quint64 registrationGeneration = reserveRegistrationGeneration(stableKey, &key);
	if (registrationGeneration == 0) return {};
	const QFileInfo info(path);
	if (!info.exists() || !info.isFile() || info.size() <= 0 || info.size() > m_limits.maxEncodedBytes) return {};
	QByteArray format = info.suffix().toLatin1().toLower(); if (format == "jpg") format = "jpeg";
	if (!QList< QByteArray >{ "png", "jpeg", "webp", "gif" }.contains(format)) return {};
	const QString canonical = info.canonicalFilePath();
	if (canonical.isEmpty()) return {};
	QMutexLocker locker(&m_mutex);
	if (m_registrationGenerationByKey.value(key) != registrationGeneration) return {};
	if (const auto it = m_sources.constFind(key); it != m_sources.cend()
		&& it->path == canonical && it->bytes.isEmpty() && !it->managedFile) {
		touchSourceLocked(key);
		return makeUrl(key, it->generation);
	}
	Source source;
	source.mimeType    = QByteArrayLiteral("image/") + format;
	source.path        = canonical;
	// Local files are lazy-decoded, but their encoded payload still belongs to the
	// bounded source store. Charging only the path would allow an unbounded number
	// of large files to remain registered behind a nominal byte budget.
	source.storedBytes = info.size();
	if (!insertSourceLocked(key, std::move(source))) return {};
	return makeUrl(key, m_sources.value(key).generation);
}

void QmlImagePipeline::invalidate(const QString &stableKey) {
	QMutexLocker locker(&m_mutex);
	const QString key = normalizedKey(stableKey);
	if (++m_nextRegistrationGeneration == 0) ++m_nextRegistrationGeneration;
	m_registrationGenerationByKey.insert(key, m_nextRegistrationGeneration);
	if (++m_nextGeneration == 0) ++m_nextGeneration;
	const quint64 generation = m_nextGeneration;
	if (auto it = m_sources.find(key); it != m_sources.end()) {
		if (it->managedFile) removeSourceLocked(key);
		else it->generation = generation;
	}
}

void QmlImagePipeline::clear() {
	QMutexLocker locker(&m_mutex);
	const QStringList keys = m_sources.keys();
	for (const QString &key : keys) removeSourceLocked(key);
	m_sourceLru.clear();
	m_sourceBytes = 0;
	m_registrationGenerationByKey.clear();
	if (++m_nextRegistrationGeneration == 0) ++m_nextRegistrationGeneration;
	m_cache.clear();
	m_lru.clear();
	m_cachedBytes = 0;
}

qint64 QmlImagePipeline::cachedBytes() const { QMutexLocker locker(&m_mutex); return m_cachedBytes; }
qint64 QmlImagePipeline::sourceBytes() const { QMutexLocker locker(&m_mutex); return m_sourceBytes; }
int QmlImagePipeline::sourceCountForTest() const { QMutexLocker locker(&m_mutex); return m_sources.size(); }
int QmlImagePipeline::activeRegistrationCountForTest() const {
	return m_registrationScheduler ? m_registrationScheduler->activeCount() : 0;
}
int QmlImagePipeline::pendingRegistrationCountForTest() const {
	return m_registrationScheduler ? m_registrationScheduler->pendingCount() : 0;
}
bool QmlImagePipeline::containsSource(const QString &value) {
	const QUrl url(value);
	QMutexLocker locker(&m_mutex);
	if (url.scheme() == QLatin1String("image") && url.host() == QLatin1String("mumble")) {
		const QString key = url.path().mid(1);
		bool ok = false;
		const quint64 generation = QUrlQuery(url).queryItemValue(QStringLiteral("g")).toULongLong(&ok);
		const auto it = m_sources.constFind(key);
		const bool found = ok && it != m_sources.cend() && it->generation == generation;
		if (found) touchSourceLocked(key);
		return found;
	}
	if (url.isLocalFile()) {
		const QString path = QFileInfo(url.toLocalFile()).absoluteFilePath();
		for (auto it = m_sources.cbegin(); it != m_sources.cend(); ++it) {
			if (it->managedFile && QFileInfo(it->path).absoluteFilePath() == path && QFileInfo::exists(it->path)) {
				touchSourceLocked(it.key());
				return true;
			}
		}
	}
	return false;
}
bool QmlImagePipeline::isCachedForTest(const QString &providerId, const QSize &requestedSize) const {
	const QString cacheKey = providerId + QStringLiteral("@%1x%2").arg(requestedSize.width()).arg(requestedSize.height());
	QMutexLocker locker(&m_mutex);
	if (requestedSize.isValid()) return m_cache.contains(cacheKey);
	const QString prefix = providerId + QLatin1Char('@');
	for (auto it = m_cache.cbegin(); it != m_cache.cend(); ++it) {
		if (it.key().startsWith(prefix)) return true;
	}
	return false;
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
	Source source;
	{
		QMutexLocker locker(&m_mutex);
		const auto it = m_sources.find(key); if (it == m_sources.end() || it->generation != generation) return {}; source = *it;
		touchSourceLocked(key);
		if (cancelled && cancelled->load()) return {};
	}
	const auto boundedTargetSize = [this, &requestedSize](const QSize &sourceSize) {
		if (!requestedSize.isValid() || !sourceSize.isValid()) return sourceSize;
		QSize target = sourceSize;
		target.scale(requestedSize, Qt::KeepAspectRatio);
		// Image provider requests are render hints, not permission to allocate an
		// arbitrarily large decoded surface. Never upscale sender-controlled data.
		if (target.width() > sourceSize.width() || target.height() > sourceSize.height()) target = sourceSize;
		if (target.width() > m_limits.maxDimension || target.height() > m_limits.maxDimension
			|| qint64(target.width()) * target.height() > m_limits.maxDecodedPixels) {
			return QSize();
		}
		return target;
	};
	const auto cachedImage = [this](const QString &cacheKey) {
		QMutexLocker locker(&m_mutex);
		const auto cached = m_cache.constFind(cacheKey);
		if (cached == m_cache.cend()) return QImage();
		const QImage image = cached->image;
		m_lru.removeAll(cacheKey);
		m_lru.push_back(cacheKey);
		return image;
	};
	if (cancelled && cancelled->load()) return {};
	if (!source.image.isNull()) {
		const QSize targetSize = boundedTargetSize(source.image.size());
		if (!targetSize.isValid()) return {};
		const QString cacheKey = providerId + QStringLiteral("@%1x%2").arg(targetSize.width()).arg(targetSize.height());
		if (const QImage cached = cachedImage(cacheKey); !cached.isNull()) return cached;
		QImage image = source.image;
		if (image.size() != targetSize) {
			image = image.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
		}
		if (image.isNull() || (cancelled && cancelled->load())) return {};
		{
			QMutexLocker locker(&m_mutex);
			const auto it = m_sources.constFind(key);
			if (it == m_sources.cend() || it->generation != generation) return {};
		}
		insertCache(cacheKey, image);
		return image;
	}
	QByteArray bytes = source.bytes;
	if (source.dataUrl) bytes = QByteArray::fromBase64(bytes, QByteArray::AbortOnBase64DecodingErrors);
	if (!source.path.isEmpty()) { QFile file(source.path); if (!file.open(QFile::ReadOnly)) return {}; bytes = file.read(m_limits.maxEncodedBytes + 1); }
	if (bytes.isEmpty() || bytes.size() > m_limits.maxEncodedBytes) return {};
	QBuffer buffer(&bytes); buffer.open(QIODevice::ReadOnly); QImageReader reader(&buffer);
	QByteArray format = source.mimeType.mid(QByteArrayLiteral("image/").size()); if (format == "jpg") format = "jpeg";
	reader.setFormat(format); reader.setDecideFormatFromContent(false); reader.setAutoTransform(true);
	const QSize sourceSize = reader.size();
	if (!sourceSize.isValid() || sourceSize.width() > m_limits.maxDimension || sourceSize.height() > m_limits.maxDimension
		|| qint64(sourceSize.width()) * sourceSize.height() > m_limits.maxDecodedPixels) return {};
	const QSize targetSize = boundedTargetSize(sourceSize);
	if (!targetSize.isValid()) return {};
	const QString cacheKey = providerId + QStringLiteral("@%1x%2").arg(targetSize.width()).arg(targetSize.height());
	if (const QImage cached = cachedImage(cacheKey); !cached.isNull()) return cached;
	if (targetSize != sourceSize) reader.setScaledSize(targetSize);
	QImage image = reader.read(); if (image.isNull() || (cancelled && cancelled->load())) return {};
	{
		QMutexLocker locker(&m_mutex); const auto it = m_sources.constFind(key); if (it == m_sources.cend() || it->generation != generation) return {};
	}
	insertCache(cacheKey, image); return image;
}

QmlAsyncImageProvider::QmlAsyncImageProvider(std::shared_ptr< QmlImagePipeline > pipeline,
											 int concurrency, int maximumPending)
	: m_pipeline(std::move(pipeline)),
	  m_scheduler(std::make_unique< QmlImageScheduler >(m_pipeline, concurrency, maximumPending)) {}
QmlAsyncImageProvider::~QmlAsyncImageProvider() {
	if (!m_scheduler || m_scheduler->shutdownNonBlocking()) return;
	// A running decode owns only the scheduler and the shared pipeline. Let it
	// finish after the provider/engine is gone, then delete the scheduler on its
	// QObject thread instead of blocking QML teardown on image decoding.
	m_scheduler->deleteWhenIdle();
	m_scheduler.release();
}
QQuickImageResponse *QmlAsyncImageProvider::requestImageResponse(const QString &id, const QSize &requestedSize) {
	return new AsyncPipelineResponse(m_scheduler.get(), id, requestedSize);
}
int QmlAsyncImageProvider::activeRequestCountForTest() const { return m_scheduler->activeCount(); }
int QmlAsyncImageProvider::pendingRequestCountForTest() const { return m_scheduler->pendingCount(); }
quint64 QmlAsyncImageProvider::uniqueRequestCountForTest() const { return m_scheduler->uniqueRequestCount(); }
