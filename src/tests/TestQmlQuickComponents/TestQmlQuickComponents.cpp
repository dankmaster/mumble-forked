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
	Q_PROPERTY(QString kind READ kind NOTIFY stateChanged)
	Q_PROPERTY(QString activePage READ activePage NOTIFY stateChanged)
	Q_PROPERTY(QVariantList pages READ pages CONSTANT)
	Q_PROPERTY(QVariantList sections READ sections NOTIFY stateChanged)
	Q_PROPERTY(QVariantList actions READ actions CONSTANT)
	Q_PROPERTY(QVariantMap state READ state NOTIFY stateChanged)
	Q_PROPERTY(qulonglong revision READ revision NOTIFY stateChanged)
	Q_PROPERTY(int closeRequests READ closeRequests NOTIFY closeRequestsChanged)
	Q_PROPERTY(QString lastAction READ lastAction NOTIFY lastActionChanged)

public:
	explicit FakeDialogState(QObject *parent = nullptr) : QObject(parent) {
		m_sections = { QVariantMap {
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
		emit stateChanged();
	}
	QString title() const { return QStringLiteral("Test dialog"); }
	QString subtitle() const { return QStringLiteral("Qt Quick component test"); }
	QString kind() const { return m_kind; }
	QString activePage() const { return m_activePage; }
	QVariantList pages() const {
		return { QVariantMap { { QStringLiteral("id"), QStringLiteral("general") },
								   { QStringLiteral("label"), QStringLiteral("General") } },
				 QVariantMap { { QStringLiteral("id"), QStringLiteral("advanced") },
								   { QStringLiteral("label"), QStringLiteral("Advanced") } } };
	}
	QVariantList sections() const { return m_sections; }
	QVariantList actions() const {
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
	Q_INVOKABLE void invokeAction(const QString &id, const QVariantMap &payload = {}) {
		if (id == QLatin1String("selectPage")) {
			m_activePage = payload.value(QStringLiteral("pageId")).toString();
			++m_revision;
			emit stateChanged();
		}
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
	Q_INVOKABLE void setSpecialState(const QString &kind, const QVariantMap &state) {
		m_kind = kind.trimmed().isEmpty() ? QStringLiteral("generic") : kind.trimmed();
		m_state = state;
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
	QString m_activePage = QStringLiteral("general");
	QString m_kind = QStringLiteral("generic");
	QVariantMap m_state;
	QVariantList m_sections;
	QVariantMap m_values { { QStringLiteral("name"), QStringLiteral("Alice") } };
	QVariantMap m_errors;
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
		engine->rootContext()->setContextProperty(QStringLiteral("uiCommands"), new FakeUiCommands(engine));
		engine->rootContext()->setContextProperty(QStringLiteral("manualPlugin"),
											 new FakeManualPluginController(engine));
	}
};

QUICK_TEST_MAIN_WITH_SETUP(qmlquickcomponents, QmlQuickComponentsSetup)

#include "TestQmlQuickComponents.moc"
