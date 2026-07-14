#include "ComposerController.h"
#include "QmlImageProvider.h"

#include <QtConcurrent/QtConcurrentRun>
#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QFutureWatcher>
#include <QtCore/QMimeData>
#include <QtCore/QRegularExpression>
#include <QtCore/QSaveFile>
#include <QtCore/QSet>
#include <QtCore/QTemporaryDir>
#include <QtCore/QUuid>
#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>
#include <QtGui/QImageReader>
#include <QtGui/QImageWriter>
#include <QtGui/QPixmap>

#include <utility>

DraftAttachmentModel::DraftAttachmentModel(QObject *parent) : QAbstractListModel(parent) {
}

int DraftAttachmentModel::rowCount(const QModelIndex &parent) const {
	return parent.isValid() ? 0 : m_items.size();
}

QVariant DraftAttachmentModel::data(const QModelIndex &index, int role) const {
	if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) return {};
	const Item &item = m_items.at(index.row());
	switch (role) {
		case IdRole: return item.id;
		case LocalUrlRole: return item.localUrl;
		case ThumbnailUrlRole: return item.thumbnailUrl;
		case FileNameRole: return item.fileName;
		case MimeRole: return item.mime;
		case KindRole: return Mumble::ChatAttachments::kindName(item.kind);
		case ByteSizeRole: return QVariant::fromValue< qulonglong >(item.byteSize);
		case StatusRole: return item.status;
		case ProgressRole: return item.progress;
		case ErrorRole: return item.error;
		default: return {};
	}
}

QHash< int, QByteArray > DraftAttachmentModel::roleNames() const {
	return { { IdRole, "stableId" },       { LocalUrlRole, "localUrl" }, { ThumbnailUrlRole, "thumbnailUrl" },
			 { FileNameRole, "fileName" }, { MimeRole, "mime" },         { KindRole, "kind" },
			 { ByteSizeRole, "byteSize" },  { StatusRole, "status" },    { ProgressRole, "progress" },
			 { ErrorRole, "error" } };
}

QList< Mumble::ChatAttachments::Source > DraftAttachmentModel::readyAttachments() const {
	QList< Mumble::ChatAttachments::Source > attachments;
	for (const Item &item : m_items) {
		if (item.status != QLatin1String("ready") || !item.localUrl.isLocalFile()) {
			continue;
		}
		attachments.push_back({ item.id, item.localUrl.toLocalFile(), item.fileName, item.mime, item.sha256,
							 item.byteSize, item.kind });
	}
	return attachments;
}

bool DraftAttachmentModel::append(Item item) {
	if (item.id.isEmpty()) return false;
	beginInsertRows({}, m_items.size(), m_items.size());
	m_items.push_back(std::move(item));
	endInsertRows();
	emit countChanged();
	return true;
}

bool DraftAttachmentModel::remove(const QString &id) {
	for (int row = 0; row < m_items.size(); ++row) {
		if (m_items[row].id != id) continue;
		beginRemoveRows({}, row, row);
		m_items.removeAt(row);
		endRemoveRows();
		emit countChanged();
		return true;
	}
	return false;
}

bool DraftAttachmentModel::move(const QString &id, int destination) {
	int source = -1;
	for (int row = 0; row < m_items.size(); ++row) {
		if (m_items[row].id == id) {
			source = row;
			break;
		}
	}
	if (source < 0) return false;
	destination = qBound(0, destination, m_items.size() - 1);
	if (source == destination) return true;
	const int target = destination > source ? destination + 1 : destination;
	beginMoveRows({}, source, source, {}, target);
	m_items.move(source, destination);
	endMoveRows();
	return true;
}
bool DraftAttachmentModel::update(const QString &id, const QString &thumbnailUrl, const QString &status,
								  const qreal progress, const QString &error) {
	for (int row = 0; row < m_items.size(); ++row) {
		Item &item = m_items[row];
		if (item.id != id) continue;
		item.thumbnailUrl = thumbnailUrl;
		item.status       = status;
		item.progress     = progress;
		item.error        = error;
		emit dataChanged(index(row), index(row), { ThumbnailUrlRole, StatusRole, ProgressRole, ErrorRole });
		return true;
	}
	return false;
}
bool DraftAttachmentModel::resolve(const QString &id, const QUrl &localUrl, const QString &fileName,
								   const QString &mime, const Mumble::ChatAttachments::Kind kind,
								   const quint64 byteSize, const QString &sha256, const QString &thumbnailUrl,
								   const QString &status, const qreal progress, const QString &error) {
	for (int row = 0; row < m_items.size(); ++row) {
		Item &item = m_items[row];
		if (item.id != id) continue;
		item.localUrl = localUrl;
		item.fileName = fileName;
		item.mime = mime;
		item.kind = kind;
		item.byteSize = byteSize;
		item.sha256 = sha256;
		item.thumbnailUrl = thumbnailUrl;
		item.status = status;
		item.progress = progress;
		item.error = error;
		emit dataChanged(index(row), index(row), { LocalUrlRole, FileNameRole, MimeRole, KindRole, ByteSizeRole,
											 ThumbnailUrlRole, StatusRole, ProgressRole, ErrorRole });
		return true;
	}
	return false;
}
DraftAttachmentModel::Item DraftAttachmentModel::item(const QString &id) const {
	for (const Item &item : m_items) if (item.id == id) return item;
	return {};
}
void DraftAttachmentModel::clear(){if(m_items.isEmpty())return;beginResetModel();m_items.clear();endResetModel();emit countChanged();}

ComposerController::ComposerController(std::shared_ptr< QmlImagePipeline > pipeline, QObject *parent)
	: QObject(parent), m_pipeline(std::move(pipeline)), m_validationPool(std::make_unique< QThreadPool >()),
	  m_attachments(this) {
	m_validationPool->setMaxThreadCount(MaxValidationWorkers);
	m_validationPool->setExpiryTimeout(5000);
	m_clipboardDirectory = std::make_unique< QTemporaryDir >(QStringLiteral("mumble-chat-paste-XXXXXX"));
}

ComposerController::~ComposerController() {
	forgetAllAttachments();
	m_validationPool->clear();
	// QFileInfo can remain inside an operating-system metadata call indefinitely
	// for a disconnected UNC path. The controller and all of its model objects
	// must still be destructible without blocking the GUI thread. Active jobs only
	// retain immutable request data and the shared image pipeline, so leaving this
	// one bounded pool alive is safer than waiting during window teardown.
	if (!m_validationPool->waitForDone(0)) {
		(void) m_validationPool.release();
	}
}

void ComposerController::setText(const QString &text) {
	if (m_sending) return;
	if (m_text == text) return;
	m_text = text;
	emit textChanged();
	updateAutocomplete();
}

void ComposerController::setCanSend(const bool value) {
	if (m_canSend == value) return;
	m_canSend = value;
	emit canSendChanged();
}

void ComposerController::setAttachmentLimits(const int maximumCount, const quint64 maximumBytes) {
	m_maximumAttachmentCount = qBound(1, maximumCount, HardMaxAttachmentCount);
	m_maximumAttachmentBytes = maximumBytes > 0 ? maximumBytes : 25ULL * 1024ULL * 1024ULL;
}

void ComposerController::setAttachmentUploadProgress(const QString &id, const qreal progress) {
	const DraftAttachmentModel::Item item = m_attachments.item(id);
	if (item.id.isEmpty()) return;
	m_attachments.update(id, item.thumbnailUrl, QStringLiteral("uploading"), qBound(0.0, progress, 1.0), {});
}

void ComposerController::resetAttachmentUploadProgress() {
	for (const DraftAttachmentModel::Item &item : m_attachments.items()) {
		if (item.status == QLatin1String("uploading")) {
			m_attachments.update(item.id, item.thumbnailUrl, QStringLiteral("ready"), 1.0, {});
		}
	}
}

int ComposerController::addUrls(const QVariantList &urls) {
	if (m_sending) return 0;
	QSet< QString > queuedPaths;
	int addedCount = 0;
	bool capacityRejected = false;
	for (const QVariant &value : urls) {
		if (m_attachments.rowCount() >= m_maximumAttachmentCount) {
			capacityRejected = true;
			break;
		}
		const QUrl url = value.toUrl();
		if (!url.isLocalFile()) continue;
		const QString rawPath = url.toLocalFile();
		if (rawPath.isEmpty()) continue;
		const QString path = QDir::cleanPath(QDir::fromNativeSeparators(rawPath));
		const QString comparisonPath =
#ifdef Q_OS_WIN
			path.toCaseFolded();
#else
			path;
#endif
		if (queuedPaths.contains(comparisonPath)) continue;
		bool duplicate = false;
		for (const DraftAttachmentModel::Item &existing : m_attachments.items()) {
			QString existingPath = QDir::cleanPath(QDir::fromNativeSeparators(existing.localUrl.toLocalFile()));
#ifdef Q_OS_WIN
			existingPath = existingPath.toCaseFolded();
#endif
			if (existingPath == comparisonPath) {
				duplicate = true;
				break;
			}
		}
		if (duplicate) continue;
		queuedPaths.insert(comparisonPath);

		const QString id       = QUuid::createUuid().toString(QUuid::WithoutBraces);
		const QString fileName = path.section(QLatin1Char('/'), -1);
		DraftAttachmentModel::Item item;
		item.id       = id;
		item.localUrl = QUrl::fromLocalFile(path);
		item.fileName = fileName;
		item.status   = QStringLiteral("loading");
		if (!m_attachments.append(std::move(item))) continue;
		++addedCount;
		const quint64 sequence = ++m_nextAttachmentSequence;
		m_attachmentSequences.insert(id, sequence);
		queueValidation(id, path, sequence);
	}
	if (capacityRejected) {
		emit attachmentRejected(tr("A message can contain at most %1 attachments.").arg(m_maximumAttachmentCount));
	}
	return addedCount;
}

bool ComposerController::pasteFromClipboard() {
	const QClipboard *clipboard = QGuiApplication::clipboard();
	const QMimeData *mimeData   = clipboard ? clipboard->mimeData() : nullptr;
	return mimeData && ingestMimeData(*mimeData);
}

bool ComposerController::ingestMimeData(const QMimeData &mimeData) {
	if (m_sending) {
		return false;
	}

	QVariantList localUrls;
	if (mimeData.hasUrls()) {
		for (const QUrl &url : mimeData.urls()) {
			if (url.isLocalFile()) {
				localUrls.push_back(url);
			}
		}
	}
	if (!localUrls.isEmpty()) {
		addUrls(localUrls);
		return true;
	}

	if (!mimeData.hasImage()) {
		return false;
	}
	const QVariant imageData = mimeData.imageData();
	QImage image             = qvariant_cast< QImage >(imageData);
	if (image.isNull()) {
		const QPixmap pixmap = qvariant_cast< QPixmap >(imageData);
		if (!pixmap.isNull()) {
			image = pixmap.toImage();
		}
	}
	if (image.isNull()) {
		return false;
	}
	addImage(image);
	return true;
}

bool ComposerController::addImage(const QImage &image, const QString &suggestedName) {
	if (m_sending || image.isNull()) {
		return false;
	}
	if (m_attachments.rowCount() >= m_maximumAttachmentCount) {
		emit attachmentRejected(tr("A message can contain at most %1 attachments.").arg(m_maximumAttachmentCount));
		return false;
	}
	if (!m_clipboardDirectory || !m_clipboardDirectory->isValid()) {
		emit attachmentRejected(tr("Could not create temporary storage for the pasted image."));
		return false;
	}

	QString baseName = QFileInfo(suggestedName).completeBaseName().trimmed();
	if (baseName.isEmpty()) {
		baseName = tr("Pasted image");
	}
	baseName.remove(QRegularExpression(QStringLiteral("[\\x00-\\x1f<>:\"/\\\\|?*]+")));
	if (baseName.isEmpty()) {
		baseName = QStringLiteral("pasted-image");
	}
	const QString id       = QUuid::createUuid().toString(QUuid::WithoutBraces);
	const QString fileName = baseName.left(180) + QStringLiteral(".png");
	const QString path = QDir(m_clipboardDirectory->path()).filePath(QStringLiteral("%1.png").arg(id));

	DraftAttachmentModel::Item item;
	item.id       = id;
	item.localUrl = QUrl::fromLocalFile(path);
	item.fileName = fileName;
	item.mime     = QStringLiteral("image/png");
	item.kind     = Mumble::ChatAttachments::Kind::Image;
	item.status   = QStringLiteral("loading");
	if (!m_attachments.append(std::move(item))) {
		return false;
	}
	m_ownedTemporaryPaths.insert(id, path);
	const quint64 sequence = ++m_nextAttachmentSequence;
	m_attachmentSequences.insert(id, sequence);
	queueValidation(id, path, sequence, image);
	return true;
}

void ComposerController::removeAttachment(const QString &id) {
	if (m_sending) return;
	forgetAttachment(id);
	m_attachments.remove(id);
}
void ComposerController::moveAttachment(const QString &id, const int destination) {
	if (m_sending) return;
	m_attachments.move(id, destination);
}
void ComposerController::cancelAttachment(const QString &id) { removeAttachment(id); }
void ComposerController::retryAttachment(const QString &id) {
	if (m_sending) return;
	const DraftAttachmentModel::Item item = m_attachments.item(id);
	if (item.id.isEmpty() || !item.localUrl.isLocalFile()) return;
	const QString path = QDir::cleanPath(QDir::fromNativeSeparators(item.localUrl.toLocalFile()));
	m_attachments.update(id, {}, QStringLiteral("loading"), 0.0, {});
	quint64 sequence = m_attachmentSequences.value(id);
	if (sequence == 0) {
		sequence = ++m_nextAttachmentSequence;
		m_attachmentSequences.insert(id, sequence);
	}
	queueValidation(id, path, sequence);
}

void ComposerController::queueValidation(const QString &id, const QString &path, const quint64 sequence,
										 const QImage &clipboardImage) {
	if (id.isEmpty() || path.isEmpty()) return;
	if (const auto previous = m_validationCancellations.value(id)) previous->store(true);
	quint64 generation = ++m_nextValidationGeneration;
	if (generation == 0) generation = ++m_nextValidationGeneration;
	const auto cancelled = std::make_shared< std::atomic_bool >(false);
	m_validationGenerations.insert(id, generation);
	m_validationCancellations.insert(id, cancelled);
	for (auto it = m_validationQueue.begin(); it != m_validationQueue.end();) {
		if (it->id == id) it = m_validationQueue.erase(it);
		else ++it;
	}
	m_validationQueue.enqueue({ id, path, generation, sequence, cancelled, clipboardImage,
								  m_maximumAttachmentBytes });
	pumpValidationQueue();
}

void ComposerController::pumpValidationQueue() {
	while (m_activeValidations < MaxValidationWorkers && !m_validationQueue.isEmpty()) {
		const ValidationRequest request = m_validationQueue.dequeue();
		if (m_validationGenerations.value(request.id) != request.generation) continue;
		++m_activeValidations;
		const std::shared_ptr< QmlImagePipeline > pipeline = m_pipeline;
		auto *watcher = new QFutureWatcher< ValidationResult >(this);
		connect(watcher, &QFutureWatcher< ValidationResult >::finished, this, [this, watcher, request]() {
			const ValidationResult result = watcher->result();
			watcher->deleteLater();
			finishValidation(request, result);
		});
		watcher->setFuture(QtConcurrent::run(m_validationPool.get(), [request, pipeline]() {
			ValidationResult result;
			if (!request.cancelled || request.cancelled->load()) return result;
			if (!request.clipboardImage.isNull()) {
				QSaveFile destination(request.path);
				if (!destination.open(QIODevice::WriteOnly)) {
					result.error = QStringLiteral("prepare");
					return result;
				}
				QImageWriter writer(&destination, "png");
				writer.setCompression(6);
				if (!writer.write(request.clipboardImage) || !destination.commit()) {
					destination.cancelWriting();
					result.error = QStringLiteral("prepare");
					return result;
				}
				if (request.cancelled->load()) {
					QFile::remove(request.path);
					return result;
				}
			}

			const QFileInfo info(request.path);
			result.canonicalPath = info.canonicalFilePath();
			if (result.canonicalPath.isEmpty()) result.canonicalPath = info.absoluteFilePath();
			result.fileName = info.fileName();
			if (!info.exists()) {
				result.error = QStringLiteral("missing");
				return result;
			}
			if (!info.isFile()) {
				result.error = QStringLiteral("not-file");
				return result;
			}
			if (info.size() <= 0) {
				result.error = QStringLiteral("empty");
				return result;
			}
			result.byteSize = static_cast< quint64 >(info.size());
			if (request.maximumBytes > 0 && result.byteSize > request.maximumBytes) {
				result.error = QStringLiteral("too-large");
				return result;
			}

			const Mumble::ChatAttachments::Classification classification =
				Mumble::ChatAttachments::classifyFile(result.canonicalPath);
			result.mime = classification.mime;
			result.kind = classification.kind;
			if (result.kind == Mumble::ChatAttachments::Kind::Image) {
				QImageReader reader(result.canonicalPath);
				const QSize imageSize = reader.size();
				constexpr qint64 maximumPixels = 40LL * 1024LL * 1024LL;
				if (!imageSize.isValid() || imageSize.width() <= 0 || imageSize.height() <= 0
					|| imageSize.width() > 16384 || imageSize.height() > 16384
					|| static_cast< qint64 >(imageSize.width()) * static_cast< qint64 >(imageSize.height())
						   > maximumPixels) {
					result.error = QStringLiteral("invalid-image");
					return result;
				}
			}

			QFile source(result.canonicalPath);
			if (!source.open(QIODevice::ReadOnly)) {
				result.error = QStringLiteral("unreadable");
				return result;
			}
			QCryptographicHash hash(QCryptographicHash::Sha256);
			while (!source.atEnd()) {
				if (request.cancelled->load()) return ValidationResult {};
				const QByteArray block = source.read(1024 * 1024);
				if (block.isEmpty() && source.error() != QFileDevice::NoError) {
					result.error = QStringLiteral("unreadable");
					return result;
				}
				hash.addData(block);
			}
			result.sha256 = QString::fromLatin1(hash.result().toHex());
			if (result.sha256.size() != 64) {
				result.error = QStringLiteral("unreadable");
				return result;
			}

			result.sourceExists = true;
			const QString key = QStringLiteral("draft:%1:%2:%3:%4:%5")
				.arg(request.id)
				.arg(request.generation)
				.arg(result.canonicalPath)
				.arg(info.size())
				.arg(info.lastModified().toMSecsSinceEpoch());
			if (!request.cancelled->load() && pipeline
				&& Mumble::ChatAttachments::supportsInlinePreview(result.kind, result.mime)) {
				result.thumbnailUrl = pipeline->registerLocalFile(result.canonicalPath, key);
			}
			return result;
		}));
	}
}

void ComposerController::finishValidation(const ValidationRequest &request, const ValidationResult &result) {
	if (m_activeValidations > 0) --m_activeValidations;
	if (m_validationGenerations.value(request.id) == request.generation) {
		if (!result.sourceExists) {
			QString error;
			if (result.error == QLatin1String("prepare")) error = tr("Could not prepare the pasted image.");
			else if (result.error == QLatin1String("missing")) error = tr("The file no longer exists.");
			else if (result.error == QLatin1String("not-file")) error = tr("Folders cannot be attached directly.");
			else if (result.error == QLatin1String("empty")) error = tr("Empty files cannot be attached.");
			else if (result.error == QLatin1String("too-large")) {
				error = tr("The file exceeds the server's %1 MB attachment limit.")
						.arg(m_maximumAttachmentBytes / (1024 * 1024));
			} else if (result.error == QLatin1String("invalid-image")) {
				error = tr("The image is unreadable or has unsafe dimensions.");
			} else {
				error = tr("The file could not be read.");
			}
			const DraftAttachmentModel::Item current = m_attachments.item(request.id);
			m_attachments.resolve(request.id, current.localUrl, current.fileName, current.mime, current.kind,
								  current.byteSize, current.sha256, {}, QStringLiteral("failed"), 0.0, error);
		} else {
			QString canonicalKey = QDir::cleanPath(QDir::fromNativeSeparators(result.canonicalPath));
#ifdef Q_OS_WIN
			canonicalKey = canonicalKey.toCaseFolded();
#endif
			QStringList laterDuplicates;
			bool superseded = false;
			for (auto it = m_canonicalPaths.cbegin(); it != m_canonicalPaths.cend(); ++it) {
				if (it.key() == request.id || it.value() != canonicalKey) continue;
				if (m_attachmentSequences.value(it.key()) < request.sequence) superseded = true;
				else laterDuplicates.push_back(it.key());
			}
			if (superseded) {
				forgetAttachment(request.id);
				m_attachments.remove(request.id);
			} else {
				for (const QString &duplicateId : laterDuplicates) {
					forgetAttachment(duplicateId);
					m_attachments.remove(duplicateId);
				}
				m_canonicalPaths.insert(request.id, canonicalKey);
				const DraftAttachmentModel::Item current = m_attachments.item(request.id);
				m_attachments.resolve(request.id, QUrl::fromLocalFile(result.canonicalPath),
					m_ownedTemporaryPaths.contains(request.id) ? current.fileName : result.fileName, result.mime,
					result.kind, result.byteSize, result.sha256, result.thumbnailUrl, QStringLiteral("ready"), 1.0,
					{});
			}
		}
	}
	pumpValidationQueue();
}

void ComposerController::forgetAttachment(const QString &id) {
	if (const auto cancelled = m_validationCancellations.value(id)) cancelled->store(true);
	m_validationGenerations.remove(id);
	m_validationCancellations.remove(id);
	m_attachmentSequences.remove(id);
	m_canonicalPaths.remove(id);
	for (auto it = m_validationQueue.begin(); it != m_validationQueue.end();) {
		if (it->id == id) it = m_validationQueue.erase(it);
		else ++it;
	}
	removeOwnedTemporaryFile(id);
}

void ComposerController::forgetAllAttachments() {
	for (const auto &cancelled : std::as_const(m_validationCancellations)) {
		if (cancelled) cancelled->store(true);
	}
	m_validationGenerations.clear();
	m_validationCancellations.clear();
	m_attachmentSequences.clear();
	m_canonicalPaths.clear();
	m_validationQueue.clear();
	const QStringList temporaryIDs = m_ownedTemporaryPaths.keys();
	for (const QString &id : temporaryIDs) removeOwnedTemporaryFile(id);
}

void ComposerController::removeOwnedTemporaryFile(const QString &id) {
	const QString path = m_ownedTemporaryPaths.take(id);
	if (!path.isEmpty()) QFile::remove(path);
}

void ComposerController::send() {
	if (m_attachments.rowCount() > m_maximumAttachmentCount) {
		emit sendFailed(tr("A message can contain at most %1 attachments.").arg(m_maximumAttachmentCount));
		return;
	}
	for (const DraftAttachmentModel::Item &item : m_attachments.items()) {
		if (item.status == QLatin1String("loading")) return;
		if (item.status != QLatin1String("ready")) {
			emit sendFailed(tr("Remove or retry attachments that could not be prepared."));
			return;
		}
		if (item.byteSize == 0 || item.byteSize > m_maximumAttachmentBytes) {
			emit sendFailed(tr("%1 exceeds the current attachment size limit.").arg(item.fileName));
			return;
		}
	}
	const QList< Mumble::ChatAttachments::Source > attachments = m_attachments.readyAttachments();
	if (m_sending || !m_canSend || (m_text.trimmed().isEmpty() && attachments.isEmpty())) return;
	m_sending = true;
	emit sendingChanged();
	emit sendRequested(m_text, attachments);
}

void ComposerController::finishSend(const bool success, const QString &error) {
	if (!m_sending) return;
	m_sending = false;
	emit sendingChanged();
	if (success) {
		setText({});
		forgetAllAttachments();
		m_attachments.clear();
	} else if (!error.isEmpty()) {
		emit sendFailed(error);
	}
}

void ComposerController::setAutocompleteSources(const QStringList &participants, const QStringList &commands) {
	m_participants = participants;
	m_commands     = commands;
	updateAutocomplete();
}
void ComposerController::updateAutocomplete(){QVariantList next;const int split=m_text.lastIndexOf(QRegularExpression(QStringLiteral("\\s")));const QString token=m_text.mid(split+1);const QStringList source=token.startsWith('@')?m_participants:token.startsWith('/')?m_commands:QStringList{};for(const QString &value:source)if(value.startsWith(token.mid(1),Qt::CaseInsensitive)){QVariantMap item;item.insert(QStringLiteral("label"),value);item.insert(QStringLiteral("value"),QString(token.left(1)+value));next.push_back(item);}if(next==m_autocompleteItems)return;m_autocompleteItems=next;emit autocompleteChanged();}
void ComposerController::complete(const QString &value) {
	const int split = m_text.lastIndexOf(QRegularExpression(QStringLiteral("\\s")));
	setText(m_text.left(split + 1) + value + QLatin1Char(' '));
}
