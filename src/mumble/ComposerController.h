#ifndef MUMBLE_MUMBLE_COMPOSERCONTROLLER_H_
#define MUMBLE_MUMBLE_COMPOSERCONTROLLER_H_

#include <QtCore/QAbstractListModel>
#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtCore/QQueue>
#include <QtCore/QThreadPool>
#include <QtCore/QUrl>

#include <atomic>
#include <memory>

class QmlImagePipeline;

class DraftAttachmentModel final : public QAbstractListModel {
	Q_OBJECT
	Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
public:
	enum Role { IdRole = Qt::UserRole + 1, LocalUrlRole, ThumbnailUrlRole, FileNameRole, StatusRole, ProgressRole, ErrorRole };
	struct Item {
		QString id;
		QUrl localUrl;
		QString thumbnailUrl;
		QString fileName;
		QString status;
		qreal progress = 0;
		QString error;
	};
	explicit DraftAttachmentModel(QObject *parent = nullptr);
	int rowCount(const QModelIndex &parent = {}) const override;
	QVariant data(const QModelIndex &index, int role) const override;
	QHash< int, QByteArray > roleNames() const override;
	const QList< Item > &items() const { return m_items; }
	QStringList localPaths() const;
	bool append(Item item);
	bool remove(const QString &id);
	bool move(const QString &id, int destination);
	bool update(const QString &id, const QString &thumbnailUrl, const QString &status, qreal progress,
				const QString &error);
	bool resolve(const QString &id, const QUrl &localUrl, const QString &fileName, const QString &thumbnailUrl,
				 const QString &status, qreal progress, const QString &error);
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
	Q_INVOKABLE void addUrls(const QVariantList &urls);
	Q_INVOKABLE void removeAttachment(const QString &id);
	Q_INVOKABLE void moveAttachment(const QString &id, int destination);
	Q_INVOKABLE void cancelAttachment(const QString &id);
	Q_INVOKABLE void retryAttachment(const QString &id);
	Q_INVOKABLE void send();
	Q_INVOKABLE void complete(const QString &value);
	void setAutocompleteSources(const QStringList &participants, const QStringList &commands);
	void finishSend(bool success, const QString &error = {});
signals:
	void textChanged();
	void canSendChanged();
	void sendingChanged();
	void autocompleteChanged();
	void sendFailed(const QString &message);
	void sendRequested(const QString &text, const QStringList &localPaths);
private:
	struct ValidationRequest {
		QString id;
		QString path;
		quint64 generation = 0;
		quint64 sequence = 0;
		std::shared_ptr< std::atomic_bool > cancelled;
	};
	struct ValidationResult {
		QString canonicalPath;
		QString fileName;
		QString thumbnailUrl;
		bool sourceExists = false;
	};
	static constexpr int MaxAttachmentCount = 16;
	static constexpr int MaxValidationWorkers = 2;
	void queueValidation(const QString &id, const QString &path, quint64 sequence);
	void pumpValidationQueue();
	void finishValidation(const ValidationRequest &request, const ValidationResult &result);
	void forgetAttachment(const QString &id);
	void forgetAllAttachments();
	void updateAutocomplete();
	std::shared_ptr< QmlImagePipeline > m_pipeline;
	std::unique_ptr< QThreadPool > m_validationPool;
	DraftAttachmentModel m_attachments;
	QQueue< ValidationRequest > m_validationQueue;
	QHash< QString, quint64 > m_validationGenerations;
	QHash< QString, std::shared_ptr< std::atomic_bool > > m_validationCancellations;
	QHash< QString, quint64 > m_attachmentSequences;
	QHash< QString, QString > m_canonicalPaths;
	quint64 m_nextValidationGeneration = 0;
	quint64 m_nextAttachmentSequence = 0;
	int m_activeValidations = 0;
	QString m_text;
	bool m_canSend = false;
	bool m_sending = false;
	QStringList m_participants;
	QStringList m_commands;
	QVariantList m_autocompleteItems;
};

#endif
