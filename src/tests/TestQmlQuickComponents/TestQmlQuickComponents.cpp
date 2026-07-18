#include <QtCore/QByteArray>
#include <QtCore/QAbstractListModel>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QObject>
#include <QtCore/QUrl>
#include <QtCore/QVariantList>
#include <QtCore/QVariantMap>
#include <QtCore/QSet>
#include <QtGui/QAccessible>
#include <QtGui/QGuiApplication>
#include <QtGui/QImage>
#include <QtGui/QWindow>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlEngine>
#include <QtQml/qqml.h>
#include <QtQuick/QQuickImageProvider>
#include <QtQuickTest/quicktest.h>

namespace {
class FixtureImageProvider final : public QQuickImageProvider {
public:
	FixtureImageProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

	QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override {
		const QSize naturalSize(96, 64);
		const bool usableRequestedSize = requestedSize.width() > 0 && requestedSize.height() > 0;
		const QSize imageSize = usableRequestedSize
			? requestedSize.boundedTo(QSize(512, 512)) : naturalSize;
		if (size) {
			*size = naturalSize;
		}

		QImage image(imageSize, QImage::Format_RGBA8888_Premultiplied);
		image.fill(id.contains(QLatin1String("screen")) ? qRgba(65, 92, 122, 255)
												 : qRgba(65, 160, 137, 255));
		return image;
	}
};

QString ensureManagedGifFixture() {
	const QString directoryPath = QDir::temp().filePath(QStringLiteral("mumble-qml-images-a1"));
	if (!QDir().mkpath(directoryPath)) {
		qFatal("Unable to create managed GIF fixture directory");
	}

	const QString filePath = QDir(directoryPath).filePath(
		QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-"
					   "12345678-1234-1234-1234-123456789abc.gif"));
	const QByteArray gif = QByteArray::fromBase64(
		QByteArrayLiteral("R0lGODlhAQABAIAAAAAAAP///ywAAAAAAQABAAACAUwAOw=="));
	QFile file(filePath);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(gif) != gif.size()) {
		qFatal("Unable to write managed GIF fixture");
	}
	file.close();
	return QUrl::fromLocalFile(filePath).toString();
}
} // namespace

class DirectMessageTimelineFixtureModel final : public QAbstractListModel {
	Q_OBJECT
	Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
	Q_PROPERTY(int matchCount READ matchCount NOTIFY matchCountChanged)
	Q_PROPERTY(int currentMatchIndex READ currentMatchIndex NOTIFY currentMatchChanged)
	Q_PROPERTY(int currentMatchRow READ currentMatchRow NOTIFY currentMatchChanged)
	Q_PROPERTY(QString currentMatchStableId READ currentMatchStableId NOTIFY currentMatchChanged)

public:
	enum Role {
		StableIdRole = Qt::UserRole + 1,
		TitleRole,
		TimestampRole,
		BodySegmentsRole,
		OwnRole,
		AvatarUrlRole,
		SubtitleRole,
		StatusRole,
		ReplyActorRole,
		ReplySnippetRole,
		ReactionsRole,
		PreviewRole,
		AttachmentsRole,
		SourceRole,
		DeletedRole,
		CanReplyRole,
		CanReactRole,
		CanDeleteRole
	};

	explicit DirectMessageTimelineFixtureModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}

	QString query() const { return m_query; }
	int matchCount() const { return static_cast< int >(m_matchRows.size()); }
	int currentMatchIndex() const { return m_currentMatchIndex; }
	int currentMatchRow() const {
		return m_currentMatchIndex >= 0 && m_currentMatchIndex < static_cast< int >(m_matchRows.size())
			? m_matchRows.at(m_currentMatchIndex) : -1;
	}
	QString currentMatchStableId() const {
		const int row = currentMatchRow();
		return row < 0 ? QString() : data(index(row), StableIdRole).toString();
	}

	void setQuery(const QString &query) {
		const QString accepted = query.left(512);
		if (m_query == accepted) return;
		m_query = accepted;
		emit queryChanged();
		refreshSearch();
	}

	Q_INVOKABLE bool nextMatch() {
		if (m_matchRows.isEmpty()) return false;
		const int next = m_currentMatchIndex < 0 ? 0
			: (m_currentMatchIndex + 1) % static_cast< int >(m_matchRows.size());
		return selectMatch(next);
	}

	Q_INVOKABLE bool previousMatch() {
		if (m_matchRows.isEmpty()) return false;
		const int count = static_cast< int >(m_matchRows.size());
		const int previous = m_currentMatchIndex < 0 ? count - 1
			: (m_currentMatchIndex + count - 1) % count;
		return selectMatch(previous);
	}

	Q_INVOKABLE void clearSearch() { setQuery(QString()); }

	int rowCount(const QModelIndex &parent = QModelIndex()) const override {
		return parent.isValid() ? 0 : 2;
	}

	QVariant data(const QModelIndex &index, int role) const override {
		if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) return {};
		const bool own = index.row() == 1;
		switch (role) {
			case StableIdRole: return own ? QStringLiteral("cpp-dm-2") : QStringLiteral("cpp-dm-1");
			case TitleRole: return own ? QStringLiteral("You") : QStringLiteral("C++ Alex");
			case TimestampRole: return own ? QStringLiteral("10:42") : QStringLiteral("10:41");
			case BodySegmentsRole:
				return QVariantList { QVariantMap {
					{ QStringLiteral("kind"), QStringLiteral("text") },
					{ QStringLiteral("text"), own ? QStringLiteral("C++ QVariantList reply")
													  : QStringLiteral("C++ QVariantList message") }
				} };
			case OwnRole: return own;
			case AvatarUrlRole: return QString();
			case SubtitleRole:
				return own ? QStringLiteral("C++ QVariantList reply")
						   : QStringLiteral("C++ QVariantList message");
			case StatusRole: return own ? QStringLiteral("Failed to send") : QString();
			case ReplyActorRole: return own ? QStringLiteral("C++ Alex") : QString();
			case ReplySnippetRole: return own ? QStringLiteral("Earlier C++ message") : QString();
			case ReactionsRole:
				return own ? QVariantList {} : QVariantList { QVariantMap {
					{ QStringLiteral("emoji"), QStringLiteral("👍") },
					{ QStringLiteral("count"), 2 },
					{ QStringLiteral("selfReacted"), true }
				} };
			case PreviewRole:
				return own ? QVariantMap {} : QVariantMap {
					{ QStringLiteral("title"), QStringLiteral("C++ rich preview") },
					{ QStringLiteral("url"), QStringLiteral("https://example.com/direct") },
					{ QStringLiteral("host"), QStringLiteral("example.com") },
					{ QStringLiteral("state"), QStringLiteral("ready") }
				};
			case AttachmentsRole:
				return own ? QVariantList {} : QVariantList { QVariantMap {
					{ QStringLiteral("id"), QStringLiteral("asset:52") },
					{ QStringLiteral("assetId"), 52 },
					{ QStringLiteral("kind"), QStringLiteral("file") },
					{ QStringLiteral("mime"), QStringLiteral("application/pdf") },
					{ QStringLiteral("fileName"), QStringLiteral("rich-dm.pdf") },
					{ QStringLiteral("state"), QStringLiteral("ready") }
				} };
			case SourceRole:
				return QVariantMap {
					{ QStringLiteral("messageId"), own ? 42 : 41 },
					{ QStringLiteral("deliveryState"), own ? QStringLiteral("failed") : QString() },
					{ QStringLiteral("deliveryLabel"), own ? QStringLiteral("Failed to send") : QString() },
					{ QStringLiteral("deliveryCanRetry"), own }
				};
			case DeletedRole: return false;
			case CanReplyRole: return true;
			case CanReactRole: return true;
			case CanDeleteRole: return own;
			default: return {};
		}
	}

	QHash< int, QByteArray > roleNames() const override {
		return {
			{ StableIdRole, QByteArrayLiteral("stableId") },
			{ TitleRole, QByteArrayLiteral("title") },
			{ TimestampRole, QByteArrayLiteral("timestamp") },
			{ BodySegmentsRole, QByteArrayLiteral("bodySegments") },
			{ OwnRole, QByteArrayLiteral("own") },
			{ AvatarUrlRole, QByteArrayLiteral("avatarUrl") },
			{ SubtitleRole, QByteArrayLiteral("subtitle") },
			{ StatusRole, QByteArrayLiteral("status") },
			{ ReplyActorRole, QByteArrayLiteral("replyActor") },
			{ ReplySnippetRole, QByteArrayLiteral("replySnippet") },
			{ ReactionsRole, QByteArrayLiteral("reactions") },
			{ PreviewRole, QByteArrayLiteral("preview") },
			{ AttachmentsRole, QByteArrayLiteral("attachments") },
			{ SourceRole, QByteArrayLiteral("source") },
			{ DeletedRole, QByteArrayLiteral("deleted") },
			{ CanReplyRole, QByteArrayLiteral("canReply") },
			{ CanReactRole, QByteArrayLiteral("canReact") },
			{ CanDeleteRole, QByteArrayLiteral("canDelete") }
		};
	}

signals:
	void queryChanged();
	void matchCountChanged();
	void currentMatchChanged();

private:
	QString searchableText(const int row) const {
		QStringList fields {
			data(index(row), TitleRole).toString(),
			data(index(row), SubtitleRole).toString(),
			data(index(row), ReplyActorRole).toString(),
			data(index(row), ReplySnippetRole).toString()
		};
		for (const QVariant &entry : data(index(row), AttachmentsRole).toList()) {
			const QVariantMap attachment = entry.toMap();
			fields.push_back(attachment.value(QStringLiteral("fileName")).toString());
			fields.push_back(attachment.value(QStringLiteral("name")).toString());
		}
		return fields.join(QLatin1Char('\n')).toCaseFolded();
	}

	void refreshSearch() {
		const int previousCount = static_cast< int >(m_matchRows.size());
		const int previousIndex = m_currentMatchIndex;
		const int previousRow = currentMatchRow();
		const QString normalized = m_query.trimmed().toCaseFolded();
		QList< int > matches;
		if (!normalized.isEmpty()) {
			for (int row = 0; row < rowCount(); ++row) {
				if (searchableText(row).contains(normalized)) matches.push_back(row);
			}
		}
		m_matchRows = std::move(matches);
		m_currentMatchIndex = m_matchRows.isEmpty() ? -1 : 0;
		if (previousCount != static_cast< int >(m_matchRows.size())) emit matchCountChanged();
		if (previousIndex != m_currentMatchIndex || previousRow != currentMatchRow()) {
			emit currentMatchChanged();
		}
	}

	bool selectMatch(const int matchIndex) {
		if (matchIndex < 0 || matchIndex >= static_cast< int >(m_matchRows.size())
			|| m_currentMatchIndex == matchIndex) {
			return false;
		}
		m_currentMatchIndex = matchIndex;
		emit currentMatchChanged();
		return true;
	}

	QString m_query;
	QList< int > m_matchRows;
	int m_currentMatchIndex = -1;
};

class FakeDialogState final : public QObject {
	Q_OBJECT
	Q_PROPERTY(bool open READ open WRITE setOpen NOTIFY stateChanged)
	Q_PROPERTY(QString title READ title CONSTANT)
	Q_PROPERTY(QString subtitle READ subtitle CONSTANT)
	Q_PROPERTY(QString dialogId READ dialogId NOTIFY stateChanged)
	Q_PROPERTY(QString kind READ kind NOTIFY stateChanged)
	Q_PROPERTY(QString activePage READ activePage NOTIFY stateChanged)
	Q_PROPERTY(QVariantList pages READ pages NOTIFY stateChanged)
	Q_PROPERTY(QVariantList sections READ sections NOTIFY stateChanged)
	Q_PROPERTY(QVariantList actions READ actions CONSTANT)
	Q_PROPERTY(QVariantList favorites READ favorites NOTIFY stateChanged)
	Q_PROPERTY(int selectedFavoriteIndex READ selectedFavoriteIndex NOTIFY stateChanged)
	Q_PROPERTY(bool editorOpen READ editorOpen NOTIFY stateChanged)
	Q_PROPERTY(QString editorTitle READ editorTitle NOTIFY stateChanged)
	Q_PROPERTY(QString primaryActionId READ primaryActionId NOTIFY stateChanged)
	Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
	Q_PROPERTY(QString loadingScaffold READ loadingScaffold NOTIFY stateChanged)
	Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY stateChanged)
	Q_PROPERTY(QString tone READ tone NOTIFY stateChanged)
	Q_PROPERTY(int preferredWidth READ preferredWidth NOTIFY stateChanged)
	Q_PROPERTY(int preferredHeight READ preferredHeight NOTIFY stateChanged)
	Q_PROPERTY(QString initialFocusId READ initialFocusId NOTIFY stateChanged)
	Q_PROPERTY(QVariantMap state READ state NOTIFY stateChanged)
	Q_PROPERTY(qulonglong revision READ revision NOTIFY stateChanged)
	Q_PROPERTY(QVariantMap presentationFieldValues READ presentationFieldValues NOTIFY presentationFieldValuesChanged)
	Q_PROPERTY(int closeRequests READ closeRequests NOTIFY closeRequestsChanged)
	Q_PROPERTY(QString lastAction READ lastAction NOTIFY lastActionChanged)
	Q_PROPERTY(QVariantMap lastPayload READ lastPayload NOTIFY lastActionChanged)

public:
	explicit FakeDialogState(QObject *parent = nullptr) : QObject(parent) {
		m_sections = defaultSections();
		m_actions = defaultActions();
	}

	QVariantList defaultSections() const {
		return { QVariantMap {
			{ QStringLiteral("title"), QStringLiteral("Validation") },
			{ QStringLiteral("fields"), QVariantList {
				QVariantMap { { QStringLiteral("id"), QStringLiteral("name") },
							  { QStringLiteral("type"), QStringLiteral("text") },
							  { QStringLiteral("label"), QStringLiteral("Name") },
							  { QStringLiteral("value"), QStringLiteral("Alice") } },
				QVariantMap { { QStringLiteral("id"), QStringLiteral("accent") },
							  { QStringLiteral("type"), QStringLiteral("color") },
							  { QStringLiteral("label"), QStringLiteral("Accent") },
							  { QStringLiteral("value"), QStringLiteral("#5ec8b0") } } } } } };
	}

	bool open() const { return m_open; }
	void setOpen(const bool open) {
		if (m_open == open) return;
		m_open = open;
		if (!m_open) clearPresentationFieldValues();
		emit stateChanged();
	}
	QString title() const { return QStringLiteral("Test dialog"); }
	QString subtitle() const { return QStringLiteral("Qt Quick component test"); }
	QString dialogId() const { return m_state.value(QStringLiteral("id"), QStringLiteral("testDialog")).toString(); }
	QString kind() const { return m_kind; }
	QString activePage() const { return m_activePage; }
	QVariantList pages() const {
		if (m_state.contains(QStringLiteral("pages"))) {
			return m_state.value(QStringLiteral("pages")).toList();
		}
		return { QVariantMap { { QStringLiteral("id"), QStringLiteral("general") },
								   { QStringLiteral("label"), QStringLiteral("General") } },
				 QVariantMap { { QStringLiteral("id"), QStringLiteral("advanced") },
								   { QStringLiteral("label"), QStringLiteral("Advanced") } } };
	}
	QVariantList sections() const { return m_sections; }
	QVariantList actions() const { return m_actions; }
	QVariantList favorites() const { return m_state.value(QStringLiteral("favorites")).toList(); }
	int selectedFavoriteIndex() const {
		return m_state.value(QStringLiteral("selectedFavoriteIndex"), -1).toInt();
	}
	bool editorOpen() const { return m_state.value(QStringLiteral("editorOpen")).toBool(); }
	QString editorTitle() const { return m_state.value(QStringLiteral("editorTitle")).toString(); }
	QString primaryActionId() const { return m_state.value(QStringLiteral("primaryActionId")).toString(); }
	bool loading() const { return m_state.value(QStringLiteral("loading")).toBool(); }
	QString loadingScaffold() const { return m_state.value(QStringLiteral("loadingScaffold")).toString(); }
	QString statusMessage() const {
		return m_state.value(QStringLiteral("statusMessage"), m_state.value(QStringLiteral("status"))).toString();
	}
	QString tone() const { return m_state.value(QStringLiteral("tone")).toString(); }
	int preferredWidth() const { return m_state.value(QStringLiteral("width"), 920).toInt(); }
	int preferredHeight() const { return m_state.value(QStringLiteral("height"), 700).toInt(); }
	QString initialFocusId() const {
		return m_state.value(QStringLiteral("initialFocusId"), m_state.value(QStringLiteral("primaryActionId"))).toString();
	}
	QVariantList defaultActions() const {
		return { QVariantMap { { QStringLiteral("id"), QStringLiteral("save") },
								 { QStringLiteral("label"), QStringLiteral("Save") },
								 { QStringLiteral("enabled"), true } },
				 QVariantMap { { QStringLiteral("id"), QStringLiteral("apply") },
								 { QStringLiteral("label"), QStringLiteral("Apply changes") },
								 { QStringLiteral("enabled"), true } },
				 QVariantMap { { QStringLiteral("id"), QStringLiteral("reset") },
								 { QStringLiteral("label"), QStringLiteral("Reset defaults") },
								 { QStringLiteral("enabled"), true } },
				 QVariantMap { { QStringLiteral("id"), QStringLiteral("cancel") },
								 { QStringLiteral("label"), QStringLiteral("Cancel") },
								 { QStringLiteral("enabled"), true } } };
	}
	QVariantMap state() const { return m_state; }
	qulonglong revision() const { return m_revision; }
	QVariantMap presentationFieldValues() const { return m_presentationFieldValues; }
	int closeRequests() const { return m_closeRequests; }
	QString lastAction() const { return m_lastAction; }
	QVariantMap lastPayload() const { return m_lastPayload; }

	Q_INVOKABLE QVariant fieldValue(const QString &id) const { return m_values.value(id); }
	Q_INVOKABLE QVariant presentationFieldValue(const QString &id) const {
		const auto value = m_presentationFieldValues.constFind(id.trimmed());
		return value == m_presentationFieldValues.cend() ? fieldValue(id) : value.value();
	}
	Q_INVOKABLE bool updatePresentationFieldValue(const QString &id, const QVariant &value) {
		const QString normalizedId = id.trimmed();
		bool eligible = false;
		for (const QVariant &sectionValue : m_sections) {
			for (const QVariant &fieldValue : sectionValue.toMap().value(QStringLiteral("fields")).toList()) {
				const QVariantMap field = fieldValue.toMap();
				if (field.value(QStringLiteral("id")).toString() == normalizedId
					&& field.value(QStringLiteral("type")).toString() == QLatin1String("voiceMeter")
					&& !field.value(QStringLiteral("staticMeter")).toBool()) {
					eligible = true;
					break;
				}
			}
			if (eligible) break;
		}
		if (!m_open || !eligible) return false;
		const auto existing = m_presentationFieldValues.constFind(normalizedId);
		if (existing != m_presentationFieldValues.cend() && existing.value() == value) return false;
		if (existing == m_presentationFieldValues.cend() && m_presentationFieldValues.size() >= 32) return false;
		m_presentationFieldValues.insert(normalizedId, value);
		emit presentationFieldValuesChanged();
		return true;
	}
	Q_INVOKABLE QString fieldError(const QString &id) const { return m_errors.value(id).toString(); }
	Q_INVOKABLE void updateField(const QString &id, const QVariant &value) {
		m_values.insert(id, value);
		m_errors.remove(id);
		++m_revision;
		emit stateChanged();
	}
	Q_INVOKABLE void invokeAction(const QString &id, const QVariantMap &payload = {}) {
		if (id == QLatin1String("selectPage")) {
			m_activePage = payload.value(QStringLiteral("pageId")).toString();
			++m_revision;
			emit stateChanged();
		} else if (id == QLatin1String("look.previewAppearance")) {
			const QString fieldId = payload.value(QStringLiteral("fieldId")).toString();
			if (!fieldId.isEmpty() && payload.contains(QStringLiteral("value"))) {
				const QVariant value = payload.value(QStringLiteral("value"));
				m_values.insert(fieldId, value);
				for (int sectionIndex = 0; sectionIndex < m_sections.size(); ++sectionIndex) {
					QVariantMap section = m_sections.at(sectionIndex).toMap();
					QVariantList fields = section.value(QStringLiteral("fields")).toList();
					for (int fieldIndex = 0; fieldIndex < fields.size(); ++fieldIndex) {
						QVariantMap field = fields.at(fieldIndex).toMap();
						if (field.value(QStringLiteral("id")).toString() == fieldId) {
							field.insert(QStringLiteral("value"), value);
							fields[fieldIndex] = field;
							section.insert(QStringLiteral("fields"), fields);
							m_sections[sectionIndex] = section;
							break;
						}
					}
				}
				m_errors.remove(fieldId);
				++m_revision;
				emit stateChanged();
			}
		}
		m_lastAction = id;
		m_lastPayload = payload;
		emit lastActionChanged();
	}
	Q_INVOKABLE void requestClose() {
		++m_closeRequests;
		emit closeRequestsChanged();
	}
	Q_INVOKABLE void setValidationError(const QString &id, const QString &message) {
		if (message.isEmpty())
			m_errors.remove(id);
		else
			m_errors.insert(id, message);
		++m_revision;
		emit stateChanged();
	}
	Q_INVOKABLE void setSpecialState(const QString &kind, const QVariantMap &state) {
		m_kind = kind.trimmed().isEmpty() ? QStringLiteral("generic") : kind.trimmed();
		m_state = state;
		clearPresentationFieldValues();
		++m_revision;
		emit stateChanged();
	}
	Q_INVOKABLE void setSections(const QVariantList &sections) {
		m_sections = sections;
		clearPresentationFieldValues();
		++m_revision;
		emit stateChanged();
	}
	Q_INVOKABLE void resetSections() {
		m_sections = defaultSections();
		m_actions = defaultActions();
		clearPresentationFieldValues();
		++m_revision;
		emit stateChanged();
	}

signals:
	void stateChanged();
	void presentationFieldValuesChanged();
	void closeRequestsChanged();
	void lastActionChanged();

private:
	void clearPresentationFieldValues() {
		if (m_presentationFieldValues.isEmpty()) return;
		m_presentationFieldValues.clear();
		emit presentationFieldValuesChanged();
	}

	bool m_open = false;
	int m_closeRequests = 0;
	QString m_lastAction;
	QVariantMap m_lastPayload;
	qulonglong m_revision = 0;
	QString m_activePage = QStringLiteral("general");
	QString m_kind = QStringLiteral("generic");
	QVariantMap m_state;
	QVariantList m_sections;
	QVariantList m_actions;
	QVariantMap m_values { { QStringLiteral("name"), QStringLiteral("Alice") } };
	QVariantMap m_errors;
	QVariantMap m_presentationFieldValues;
};

class FakeUiCommands final : public QObject {
	Q_OBJECT
	Q_PROPERTY(bool pttPressed READ pttPressed NOTIFY pttPressedChanged)
	Q_PROPERTY(int releaseCount READ releaseCount NOTIFY pttPressedChanged)

public:
	explicit FakeUiCommands(QObject *parent = nullptr) : QObject(parent) {}
	bool pttPressed() const { return m_pttPressed; }
	int releaseCount() const { return m_releaseCount; }
	Q_INVOKABLE void setPttPressed(const bool pressed) {
		if (m_pttPressed == pressed) return;
		m_pttPressed = pressed;
		emit pttPressedChanged();
	}
	Q_INVOKABLE void releasePtt() {
		++m_releaseCount;
		m_pttPressed = false;
		emit pttPressedChanged();
	}
	Q_INVOKABLE void clearCounts() {
		m_releaseCount = 0;
		emit pttPressedChanged();
	}

signals:
	void pttPressedChanged();

private:
	bool m_pttPressed = false;
	int m_releaseCount = 0;
};

class FakeManualPluginController final : public QObject {
	Q_OBJECT
	Q_PROPERTY(double x MEMBER m_x NOTIFY stateChanged)
	Q_PROPERTY(double y MEMBER m_y NOTIFY stateChanged)
	Q_PROPERTY(double z MEMBER m_z NOTIFY stateChanged)
	Q_PROPERTY(int azimuth MEMBER m_azimuth NOTIFY stateChanged)
	Q_PROPERTY(int elevation MEMBER m_elevation NOTIFY stateChanged)
	Q_PROPERTY(QString context MEMBER m_context NOTIFY stateChanged)
	Q_PROPERTY(QString identity MEMBER m_identity NOTIFY stateChanged)
	Q_PROPERTY(int staleSeconds MEMBER m_staleSeconds NOTIFY stateChanged)
	Q_PROPERTY(bool active MEMBER m_active NOTIFY stateChanged)
	Q_PROPERTY(bool linked MEMBER m_linked NOTIFY stateChanged)
	Q_PROPERTY(QVariantList speakers MEMBER m_speakers NOTIFY stateChanged)
	Q_PROPERTY(int refreshCount READ refreshCount NOTIFY countersChanged)
	Q_PROPERTY(int applyCount READ applyCount NOTIFY countersChanged)
	Q_PROPERTY(int resetCount READ resetCount NOTIFY countersChanged)
	Q_PROPERTY(bool speakerUpdatesEnabled READ speakerUpdatesEnabled NOTIFY countersChanged)

public:
	explicit FakeManualPluginController(QObject *parent = nullptr) : QObject(parent) {
		m_speakers = { QVariantMap { { QStringLiteral("session"), 42 }, { QStringLiteral("x"), 3.0 },
										 { QStringLiteral("z"), -2.0 } } };
	}
	int refreshCount() const { return m_refreshCount; }
	int applyCount() const { return m_applyCount; }
	int resetCount() const { return m_resetCount; }
	bool speakerUpdatesEnabled() const { return m_speakerUpdatesEnabled; }
	Q_INVOKABLE void refresh() {
		++m_refreshCount;
		emit countersChanged();
	}
	Q_INVOKABLE void apply() {
		++m_applyCount;
		emit applied();
		emit countersChanged();
	}
	Q_INVOKABLE void setSpeakerUpdatesEnabled(const bool enabled) {
		if (m_speakerUpdatesEnabled == enabled) return;
		m_speakerUpdatesEnabled = enabled;
		emit countersChanged();
	}
	Q_INVOKABLE void reset() {
		++m_resetCount;
		emit resetCompleted();
		emit countersChanged();
	}
	Q_INVOKABLE void clearCounts() {
		m_refreshCount = 0;
		m_applyCount = 0;
		m_resetCount = 0;
		emit countersChanged();
	}

signals:
	void stateChanged();
	void countersChanged();
	void applied();
	void resetCompleted();

private:
	double m_x = 1.0;
	double m_y = 2.0;
	double m_z = 3.0;
	int m_azimuth = 45;
	int m_elevation = 5;
	QString m_context = QStringLiteral("test-context");
	QString m_identity = QStringLiteral("test-identity");
	int m_staleSeconds = 15;
	bool m_active = true;
	bool m_linked = true;
	QVariantList m_speakers;
	int m_refreshCount = 0;
	int m_applyCount = 0;
	int m_resetCount = 0;
	bool m_speakerUpdatesEnabled = false;
};

class AccessibilityProbe final : public QObject {
	Q_OBJECT

public:
	explicit AccessibilityProbe(QObject *parent = nullptr) : QObject(parent) {}

	Q_INVOKABLE QStringList visibleNames() const {
		QStringList names;
		QSet< quintptr > visited;
		for (QWindow *window : QGuiApplication::topLevelWindows()) {
			if (window && window->isVisible()) {
				collectVisibleNames(QAccessible::queryAccessibleInterface(window), names, visited, 0);
			}
		}
		return names;
	}

private:
	static void collectVisibleNames(QAccessibleInterface *interface, QStringList &names,
								QSet< quintptr > &visited, int depth) {
		if (!interface || !interface->isValid() || depth > 64) return;
		const quintptr identity = interface->object()
			? reinterpret_cast< quintptr >(interface->object())
			: reinterpret_cast< quintptr >(interface);
		if (visited.contains(identity)) return;
		visited.insert(identity);
		const QAccessible::State state = interface->state();
		if (state.invisible || state.offscreen) return;
		const QString name = interface->text(QAccessible::Name);
		if (!name.isEmpty()) names.push_back(name);
		for (int index = 0; index < interface->childCount(); ++index) {
			collectVisibleNames(interface->child(index), names, visited, depth + 1);
		}
	}
};

class QmlQuickComponentsSetup final : public QObject {
	Q_OBJECT

public slots:
	void qmlEngineAvailable(QQmlEngine *engine) {
		// Activate accessibility before any QML component is instantiated. Qt
		// Quick decides whether to create attached accessibility objects while
		// constructing the scene, so enabling it lazily from a test probe can leave
		// otherwise semantic delegates absent from the off-screen tree.
		QAccessible::setActive(true);
		// QUICK_TEST_SOURCE_DIR is used for test discovery, but a component loaded
		// from qrc:/ resolves URI imports only through the engine import path. Add
		// the fixture root explicitly so Mumble.ScreenShare resolves to the fake
		// module under qml/Mumble/ScreenShare instead of requiring the production
		// C++ QML registration in this component-only test executable.
		engine->addImportPath(QStringLiteral(QUICK_TEST_SOURCE_DIR));
		const int themeType = qmlRegisterSingletonType(QUrl(QStringLiteral("qrc:/qml-shell/Theme.qml")),
											 "Mumble.Theme", 1, 0, "Theme");
		Q_UNUSED(themeType)
		const int providerPresentationType = qmlRegisterSingletonType(
			QUrl(QStringLiteral("qrc:/qml-shell/ProviderPresentation.qml")),
			"Mumble.ProviderPresentation", 1, 0, "ProviderPresentation");
		Q_UNUSED(providerPresentationType)
		engine->addImageProvider(QStringLiteral("mumble"), new FixtureImageProvider());
		engine->rootContext()->setContextProperty(QStringLiteral("uiTheme"), QVariant::fromValue< QObject * >(nullptr));
		engine->rootContext()->setContextProperty(QStringLiteral("managedGifUrl"), ensureManagedGifFixture());
		QFile mainQmlSource(QStringLiteral(":/qml-shell/Main.qml"));
		const QString mainQmlText = mainQmlSource.open(QIODevice::ReadOnly)
			? QString::fromUtf8(mainQmlSource.readAll()) : QString();
		engine->rootContext()->setContextProperty(QStringLiteral("mainQmlSource"), mainQmlText);
		QFile directMessageQmlSource(QStringLiteral(":/qml-shell/DirectMessageWindow.qml"));
		const QString directMessageQmlText = directMessageQmlSource.open(QIODevice::ReadOnly)
			? QString::fromUtf8(directMessageQmlSource.readAll()) : QString();
		engine->rootContext()->setContextProperty(QStringLiteral("directMessageQmlSource"), directMessageQmlText);
		engine->rootContext()->setContextProperty(QStringLiteral("cppDirectMessageTimelineModel"),
											 new DirectMessageTimelineFixtureModel(engine));
		auto *dialogState = new FakeDialogState(engine);
		engine->rootContext()->setContextProperty(QStringLiteral("dialogState"), dialogState);
		engine->rootContext()->setContextProperty(QStringLiteral("uiCommands"), new FakeUiCommands(engine));
		engine->rootContext()->setContextProperty(QStringLiteral("manualPlugin"),
											 new FakeManualPluginController(engine));
		engine->rootContext()->setContextProperty(QStringLiteral("accessibilityProbe"),
											 new AccessibilityProbe(engine));
	}
};

QUICK_TEST_MAIN_WITH_SETUP(qmlquickcomponents, QmlQuickComponentsSetup)

#include "TestQmlQuickComponents.moc"
