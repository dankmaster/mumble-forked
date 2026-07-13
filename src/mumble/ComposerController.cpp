#include "ComposerController.h"
#include "QmlImageProvider.h"

#include <QtConcurrent/QtConcurrentRun>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QFutureWatcher>
#include <QtCore/QRegularExpression>
#include <QtCore/QSet>
#include <QtCore/QUuid>

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
		case StatusRole: return item.status;
		case ProgressRole: return item.progress;
		case ErrorRole: return item.error;
		default: return {};
	}
}

QHash< int, QByteArray > DraftAttachmentModel::roleNames() const {
	return { { IdRole, "stableId" },       { LocalUrlRole, "localUrl" }, { ThumbnailUrlRole, "thumbnailUrl" },
			 { FileNameRole, "fileName" }, { StatusRole, "status" },    { ProgressRole, "progress" },
			 { ErrorRole, "error" } };
}

QStringList DraftAttachmentModel::localPaths() const {
	QStringList paths;
	for (const Item &item : m_items) {
		if (item.status == QLatin1String("ready")) paths << item.localUrl.toLocalFile();
	}
	return paths;
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
								   const QString &thumbnailUrl, const QString &status, const qreal progress,
								   const QString &error) {
	for (int row = 0; row < m_items.size(); ++row) {
		Item &item = m_items[row];
		if (item.id != id) continue;
		item.localUrl = localUrl;
		item.fileName = fileName;
		item.thumbnailUrl = thumbnailUrl;
		item.status = status;
		item.progress = progress;
		item.error = error;
		emit dataChanged(index(row), index(row), { LocalUrlRole, FileNameRole, ThumbnailUrlRole,
													 StatusRole, ProgressRole, ErrorRole });
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
void ComposerController::addUrls(const QVariantList &urls) {
	if (m_sending) return;
	QSet< QString > queuedPaths;
	int examinedUrlCount = 0;
	for (const QVariant &value : urls) {
		if (++examinedUrlCount > MaxAttachmentCount) break;
		if (m_attachments.rowCount() >= MaxAttachmentCount) break;
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

		const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
		const QString fileName = path.section(QLatin1Char('/'), -1);
		DraftAttachmentModel::Item item{ id,
										 QUrl::fromLocalFile(path),
										 {},
										 fileName,
										 QStringLiteral("loading"),
										 0.0,
										 {} };
		if (!m_attachments.append(std::move(item))) continue;
		const quint64 sequence = ++m_nextAttachmentSequence;
		m_attachmentSequences.insert(id, sequence);
		queueValidation(id, path, sequence);
	}
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

void ComposerController::queueValidation(const QString &id, const QString &path, const quint64 sequence) {
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
	m_validationQueue.enqueue({ id, path, generation, sequence, cancelled });
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
			const QFileInfo info(request.path);
			if (!request.cancelled->load() && info.exists() && info.isFile() && info.size() > 0) {
				result.sourceExists = true;
				result.canonicalPath = info.canonicalFilePath();
				if (result.canonicalPath.isEmpty()) result.canonicalPath = info.absoluteFilePath();
				result.fileName = info.fileName();
				const QString key = QStringLiteral("draft:%1:%2:%3:%4:%5")
					.arg(request.id)
					.arg(request.generation)
					.arg(result.canonicalPath)
					.arg(info.size())
					.arg(info.lastModified().toMSecsSinceEpoch());
				if (!request.cancelled->load() && pipeline) {
					result.thumbnailUrl = pipeline->registerLocalFile(result.canonicalPath, key);
				}
			}
			return result;
		}));
	}
}

void ComposerController::finishValidation(const ValidationRequest &request, const ValidationResult &result) {
	if (m_activeValidations > 0) --m_activeValidations;
	if (m_validationGenerations.value(request.id) == request.generation) {
		if (!result.sourceExists) {
			forgetAttachment(request.id);
			m_attachments.remove(request.id);
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
				m_attachments.resolve(request.id, QUrl::fromLocalFile(result.canonicalPath), result.fileName,
					result.thumbnailUrl,
					result.thumbnailUrl.isEmpty() ? QStringLiteral("failed") : QStringLiteral("ready"),
					result.thumbnailUrl.isEmpty() ? 0.0 : 1.0,
					result.thumbnailUrl.isEmpty() ? tr("Unsupported or oversized image.") : QString());
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
}
void ComposerController::send() {
	for (const DraftAttachmentModel::Item &item : m_attachments.items()) {
		if (item.status == QLatin1String("loading")) return;
	}
	const QStringList paths = m_attachments.localPaths();
	if (m_sending || !m_canSend || (m_text.trimmed().isEmpty() && paths.isEmpty())) return;
	m_sending = true;
	emit sendingChanged();
	emit sendRequested(m_text, paths);
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
