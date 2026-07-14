#ifndef MUMBLE_MUMBLE_COMPOSERCONTROLLER_H_
#define MUMBLE_MUMBLE_COMPOSERCONTROLLER_H_

#include "ChatAttachment.h"

#include <QtCore/QAbstractListModel>
#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtCore/QQueue>
#include <QtCore/QThreadPool>
#include <QtCore/QUrl>
#include <QtGui/QImage>

#include <atomic>
#include <memory>

class QmlImagePipeline;
class QMimeData;
class QTemporaryDir;

class DraftAttachmentModel final : public QAbstractListModel {
	Q_OBJECT
	Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
public:
	enum Role {
		IdRole = Qt::UserRole + 1,
		LocalUrlRole,
		ThumbnailUrlRole,
		FileNameRole,
		MimeRole,
		KindRole,
		ByteSizeRole,
		StatusRole,
		ProgressRole,
		ErrorRole
	};
	struct Item {
		QString id;
		QUrl localUrl;
		QString thumbnailUrl;
		QString fileName;
		QString mime;
		Mumble::ChatAttachments::Kind kind = Mumble::ChatAttachments::Kind::Unknown;
		quint64 byteSize = 0;
		QString sha256;
		QString status;
		qreal progress = 0;
		QString error;
	};
	explicit DraftAttachmentModel(QObject *parent = nullptr);
	int rowCount(const QModelIndex &parent = {}) const override;
	QVariant data(const QModelIndex &index, int role) const override;
	QHash< int, QByteArray > roleNames() const override;
	const QList< Item > &items() const { return m_items; }
	QList< Mumble::ChatAttachments::Source > readyAttachments() const;
	bool append(Item item);
	bool remove(const QString &id);
	bool move(const QString &id, int destination);
	bool update(const QString &id, const QString &thumbnailUrl, const QString &status, qreal progress,
				const QString &error);
	bool resolve(const QString &id, const QUrl &localUrl, const QString &fileName, const QString &mime,
				 Mumble::ChatAttachments::Kind kind, quint64 byteSize, const QString &sha256,
				 const QString &thumbnailUrl, const QString &status, qreal progress, const QString &error);
	Item item(const QString &id) const;
	void clear();
	signals: void countChanged();
private: QList< Item > m_items;
};

class ComposerController final : public QObject {
	Q_OBJECT
	Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged)
	Q_PROPERTY(bool canSend READ canSend WRITE setCanSend NOTIFY canSendChanged)
	Q_PROPERTY(bool sending READ sending NOTIFY sendingChanged)
	Q_PROPERTY(DraftAttachmentModel *attachments READ attachments CONSTANT)
	Q_PROPERTY(QVariantList autocompleteItems READ autocompleteItems NOTIFY autocompleteChanged)
public:
	explicit ComposerController(std::shared_ptr< QmlImagePipeline > pipeline, QObject *parent = nullptr);
	~ComposerController() override;
	QString text() const { return m_text; }
	void setText(const QString &text);
	bool canSend() const { return m_canSend; }
	void setCanSend(bool value);
	bool sending() const { return m_sending; }
	DraftAttachmentModel *attachments() { return &m_attachments; }
	QVariantList autocompleteItems() const { return m_autocompleteItems; }
	Q_INVOKABLE int addUrls(const QVariantList &urls);
	Q_INVOKABLE bool pasteFromClipboard();
	bool ingestMimeData(const QMimeData &mimeData);
	bool addImage(const QImage &image, const QString &suggestedName = {});
	Q_INVOKABLE void removeAttachment(const QString &id);
	Q_INVOKABLE void moveAttachment(const QString &id, int destination);
	Q_INVOKABLE void cancelAttachment(const QString &id);
	Q_INVOKABLE void retryAttachment(const QString &id);
	Q_INVOKABLE void send();
	Q_INVOKABLE void complete(const QString &value);
	void setAutocompleteSources(const QStringList &participants, const QStringList &commands);
	void setAttachmentLimits(int maximumCount, quint64 maximumBytes);
	void setAttachmentUploadProgress(const QString &id, qreal progress);
	void resetAttachmentUploadProgress();
	void finishSend(bool success, const QString &error = {});
signals:
	void textChanged();
	void canSendChanged();
	void sendingChanged();
	void autocompleteChanged();
	void sendFailed(const QString &message);
	void attachmentRejected(const QString &message);
	void sendRequested(const QString &text, const QList< Mumble::ChatAttachments::Source > &attachments);
private:
	struct ValidationRequest {
		QString id;
		QString path;
		quint64 generation = 0;
		quint64 sequence = 0;
		std::shared_ptr< std::atomic_bool > cancelled;
		QImage clipboardImage;
		quint64 maximumBytes = 0;
	};
	struct ValidationResult {
		QString canonicalPath;
		QString fileName;
		QString mime;
		Mumble::ChatAttachments::Kind kind = Mumble::ChatAttachments::Kind::Unknown;
		quint64 byteSize = 0;
		QString sha256;
		QString thumbnailUrl;
		bool sourceExists = false;
		QString error;
	};
	static constexpr int HardMaxAttachmentCount = 16;
	static constexpr int MaxValidationWorkers = 2;
	void queueValidation(const QString &id, const QString &path, quint64 sequence, const QImage &clipboardImage = {});
	void pumpValidationQueue();
	void finishValidation(const ValidationRequest &request, const ValidationResult &result);
	void forgetAttachment(const QString &id);
	void forgetAllAttachments();
	void removeOwnedTemporaryFile(const QString &id);
	void updateAutocomplete();
	std::shared_ptr< QmlImagePipeline > m_pipeline;
	std::unique_ptr< QThreadPool > m_validationPool;
	DraftAttachmentModel m_attachments;
	QQueue< ValidationRequest > m_validationQueue;
	QHash< QString, quint64 > m_validationGenerations;
	QHash< QString, std::shared_ptr< std::atomic_bool > > m_validationCancellations;
	QHash< QString, quint64 > m_attachmentSequences;
	QHash< QString, QString > m_canonicalPaths;
	QHash< QString, QString > m_ownedTemporaryPaths;
	quint64 m_nextValidationGeneration = 0;
	quint64 m_nextAttachmentSequence = 0;
	int m_activeValidations = 0;
	QString m_text;
	bool m_canSend = false;
	bool m_sending = false;
	int m_maximumAttachmentCount = 4;
	quint64 m_maximumAttachmentBytes = 25ULL * 1024ULL * 1024ULL;
	std::unique_ptr< QTemporaryDir > m_clipboardDirectory;
	QStringList m_participants;
	QStringList m_commands;
	QVariantList m_autocompleteItems;
};

#endif
