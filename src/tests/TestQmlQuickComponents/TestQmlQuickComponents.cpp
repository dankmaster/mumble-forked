#include <QtCore/QObject>
#include <QtCore/QVariantList>
#include <QtCore/QVariantMap>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlEngine>
#include <QtQml/qqml.h>
#include <QtQuickTest/quicktest.h>

class FakeDialogState final : public QObject {
	Q_OBJECT
	Q_PROPERTY(bool open READ open WRITE setOpen NOTIFY stateChanged)
	Q_PROPERTY(QString title READ title CONSTANT)
	Q_PROPERTY(QString subtitle READ subtitle CONSTANT)
	Q_PROPERTY(QString kind READ kind CONSTANT)
	Q_PROPERTY(QString activePage READ activePage CONSTANT)
	Q_PROPERTY(QVariantList pages READ pages CONSTANT)
	Q_PROPERTY(QVariantList sections READ sections NOTIFY stateChanged)
	Q_PROPERTY(QVariantList actions READ actions CONSTANT)
	Q_PROPERTY(QVariantMap state READ state CONSTANT)
	Q_PROPERTY(qulonglong revision READ revision NOTIFY stateChanged)
	Q_PROPERTY(int closeRequests READ closeRequests NOTIFY closeRequestsChanged)
	Q_PROPERTY(QString lastAction READ lastAction NOTIFY lastActionChanged)

public:
	explicit FakeDialogState(QObject *parent = nullptr) : QObject(parent) {
		m_sections = { QVariantMap {
			{ QStringLiteral("title"), QStringLiteral("Validation") },
			{ QStringLiteral("fields"), QVariantList { QVariantMap {
				{ QStringLiteral("id"), QStringLiteral("name") },
				{ QStringLiteral("type"), QStringLiteral("text") },
				{ QStringLiteral("label"), QStringLiteral("Name") },
				{ QStringLiteral("value"), QStringLiteral("Alice") } } } } } };
	}

	bool open() const { return m_open; }
	void setOpen(const bool open) {
		if (m_open == open) return;
		m_open = open;
		emit stateChanged();
	}
	QString title() const { return QStringLiteral("Test dialog"); }
	QString subtitle() const { return QStringLiteral("Qt Quick component test"); }
	QString kind() const { return QStringLiteral("generic"); }
	QString activePage() const { return {}; }
	QVariantList pages() const { return {}; }
	QVariantList sections() const { return m_sections; }
	QVariantList actions() const {
		return { QVariantMap { { QStringLiteral("id"), QStringLiteral("save") },
							 { QStringLiteral("label"), QStringLiteral("Save") },
							 { QStringLiteral("enabled"), true } } };
	}
	QVariantMap state() const { return {}; }
	qulonglong revision() const { return m_revision; }
	int closeRequests() const { return m_closeRequests; }
	QString lastAction() const { return m_lastAction; }

	Q_INVOKABLE QVariant fieldValue(const QString &id) const { return m_values.value(id); }
	Q_INVOKABLE QString fieldError(const QString &id) const { return m_errors.value(id).toString(); }
	Q_INVOKABLE void updateField(const QString &id, const QVariant &value) {
		m_values.insert(id, value);
		m_errors.remove(id);
		++m_revision;
		emit stateChanged();
	}
	Q_INVOKABLE void invokeAction(const QString &id, const QVariantMap & = {}) {
		m_lastAction = id;
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

signals:
	void stateChanged();
	void closeRequestsChanged();
	void lastActionChanged();

private:
	bool m_open = false;
	int m_closeRequests = 0;
	QString m_lastAction;
	qulonglong m_revision = 0;
	QVariantList m_sections;
	QVariantMap m_values { { QStringLiteral("name"), QStringLiteral("Alice") } };
	QVariantMap m_errors;
};

class QmlQuickComponentsSetup final : public QObject {
	Q_OBJECT

public slots:
	void qmlEngineAvailable(QQmlEngine *engine) {
		const int themeType = qmlRegisterSingletonType(QUrl(QStringLiteral("qrc:/qml-shell/Theme.qml")),
												 "Mumble.Theme", 1, 0, "Theme");
		Q_UNUSED(themeType)
		engine->rootContext()->setContextProperty(QStringLiteral("uiTheme"), QVariant::fromValue< QObject * >(nullptr));
		auto *dialogState = new FakeDialogState(engine);
		engine->rootContext()->setContextProperty(QStringLiteral("dialogState"), dialogState);
	}
};

QUICK_TEST_MAIN_WITH_SETUP(qmlquickcomponents, QmlQuickComponentsSetup)

#include "TestQmlQuickComponents.moc"
