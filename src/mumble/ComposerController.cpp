#include "ComposerController.h"
#include "QmlImageProvider.h"

#include <QtCore/QFileInfo>
#include <QtCore/QRegularExpression>
#include <QtCore/QUuid>

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
DraftAttachmentModel::Item DraftAttachmentModel::item(const QString &id) const {
	for (const Item &item : m_items) if (item.id == id) return item;
	return {};
}
void DraftAttachmentModel::clear(){if(m_items.isEmpty())return;beginResetModel();m_items.clear();endResetModel();emit countChanged();}

ComposerController::ComposerController(std::shared_ptr< QmlImagePipeline > pipeline, QObject *parent)
	: QObject(parent), m_pipeline(std::move(pipeline)), m_attachments(this) {
}

void ComposerController::setText(const QString &text) {
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
	for (const QVariant &value : urls) {
		const QUrl url = value.toUrl();
		if (!url.isLocalFile()) continue;
		const QFileInfo info(url.toLocalFile());
		if (!info.exists() || !info.isFile() || info.size() <= 0) continue;
		const QString canonicalPath = info.canonicalFilePath();
		bool duplicate = false;
		for (const DraftAttachmentModel::Item &existing : m_attachments.items()) {
			if (QFileInfo(existing.localUrl.toLocalFile()).canonicalFilePath() == canonicalPath) {
				duplicate = true;
				break;
			}
		}
		if (duplicate) continue;

		const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
		const QString key = QStringLiteral("draft:%1:%2:%3")
							.arg(canonicalPath)
							.arg(info.size())
							.arg(info.lastModified().toMSecsSinceEpoch());
		const QString thumbnail = m_pipeline ? m_pipeline->registerLocalFile(canonicalPath, key) : QString();
		DraftAttachmentModel::Item item{ id,
										 QUrl::fromLocalFile(canonicalPath),
										 thumbnail,
										 info.fileName(),
										 thumbnail.isEmpty() ? QStringLiteral("failed") : QStringLiteral("ready"),
										 thumbnail.isEmpty() ? 0.0 : 1.0,
										 thumbnail.isEmpty() ? tr("Unsupported or oversized image.") : QString() };
		m_attachments.append(std::move(item));
	}
}
void ComposerController::removeAttachment(const QString &id) { m_attachments.remove(id); }
void ComposerController::moveAttachment(const QString &id, const int destination) { m_attachments.move(id, destination); }
void ComposerController::cancelAttachment(const QString &id) { m_attachments.remove(id); }
void ComposerController::retryAttachment(const QString &id) {
	const DraftAttachmentModel::Item item = m_attachments.item(id);
	if (item.id.isEmpty() || !item.localUrl.isLocalFile()) return;
	const QFileInfo info(item.localUrl.toLocalFile());
	const QString key = QStringLiteral("draft:%1:%2:%3")
							.arg(info.canonicalFilePath())
							.arg(info.size())
							.arg(info.lastModified().toMSecsSinceEpoch());
	const QString thumbnail = m_pipeline ? m_pipeline->registerLocalFile(info.absoluteFilePath(), key) : QString();
	m_attachments.update(id, thumbnail, thumbnail.isEmpty() ? QStringLiteral("failed") : QStringLiteral("ready"),
						 thumbnail.isEmpty() ? 0.0 : 1.0,
						 thumbnail.isEmpty() ? tr("Unsupported or oversized image.") : QString());
}
void ComposerController::send() {
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
