#ifndef MUMBLE_MUMBLE_COMPOSERCONTROLLER_H_
#define MUMBLE_MUMBLE_COMPOSERCONTROLLER_H_

#include <QtCore/QAbstractListModel>
#include <QtCore/QObject>
#include <QtCore/QUrl>

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
	void updateAutocomplete();
	std::shared_ptr< QmlImagePipeline > m_pipeline;
	DraftAttachmentModel m_attachments;
	QString m_text;
	bool m_canSend = false;
	bool m_sending = false;
	QStringList m_participants;
	QStringList m_commands;
	QVariantList m_autocompleteItems;
};

#endif
