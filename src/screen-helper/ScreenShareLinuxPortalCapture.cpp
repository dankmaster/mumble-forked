// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the Mumble
// source tree or at <https://www.mumble.info/LICENSE>.

#include "ScreenShareLinuxPortalCapture.h"

#ifdef Q_OS_LINUX
#	include <QtDBus/QDBusArgument>
#	include <QtDBus/QDBusConnection>
#	include <QtDBus/QDBusConnectionInterface>
#	include <QtDBus/QDBusInterface>
#	include <QtDBus/QDBusMessage>
#	include <QtDBus/QDBusObjectPath>
#	include <QtDBus/QDBusVariant>

#	include <QtCore/QCoreApplication>
#	include <QtCore/QDateTime>
#	include <QtCore/QEventLoop>
#	include <QtCore/QProcess>
#	include <QtCore/QTimer>
#	include <QtCore/QVariantMap>

namespace {
constexpr auto PORTAL_SERVICE          = "org.freedesktop.portal.Desktop";
constexpr auto PORTAL_PATH             = "/org/freedesktop/portal/desktop";
constexpr auto PORTAL_SCREENCAST_IFACE = "org.freedesktop.portal.ScreenCast";
constexpr auto PORTAL_REQUEST_IFACE    = "org.freedesktop.portal.Request";

QString uniqueHandleToken() {
	return QStringLiteral("mumble_%1_%2")
		.arg(QCoreApplication::applicationPid())
		.arg(QDateTime::currentMSecsSinceEpoch() % 1000000);
}

QString senderToPathSegment(const QString &sender) {
	QString path = sender;
	path.remove(':');
	path.replace('.', '_');
	return path;
}

QString predictableRequestPath(const QString &baseService, const QString &handleToken) {
	return QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2")
		.arg(senderToPathSegment(baseService))
		.arg(handleToken);
}

class PortalResponseReceiver : public QObject {
	Q_OBJECT
public:
	uint responseCode = 2;
	QVariantMap results;
	bool received     = false;
	QEventLoop *loop  = nullptr;

public slots:
	void handleResponse(uint response, const QVariantMap &res) {
		responseCode = response;
		results      = res;
		received     = true;
		if (loop) {
			loop->quit();
		}
	}
};

struct PortalCallResult {
	bool ok              = false;
	QString errorMessage;
	QString sessionHandle;
	QVariantMap results;
};

PortalCallResult callPortalMethod(QDBusInterface &iface, const QString &methodName,
								  const QVariantList &args, const QString &handleToken,
								  int timeoutMsec) {
	PortalCallResult result;

	QDBusConnection bus = QDBusConnection::sessionBus();
	if (!bus.isConnected()) {
		result.errorMessage = QStringLiteral("D-Bus session bus is not connected.");
		return result;
	}

	const QString baseService = bus.baseService();
	if (baseService.isEmpty()) {
		result.errorMessage = QStringLiteral("Could not determine the D-Bus base service name.");
		return result;
	}

	const QString predictedPath = predictableRequestPath(baseService, handleToken);

	PortalResponseReceiver receiver;
	QEventLoop loop;
	receiver.loop = &loop;

	bool connected = bus.connect(PORTAL_SERVICE, predictedPath, PORTAL_REQUEST_IFACE, "Response",
								 "ua{sv}", &receiver, SLOT(handleResponse(uint, QVariantMap)));

	const QDBusMessage reply = iface.callWithArgumentList(QDBus::Block, methodName, args);

	if (reply.type() == QDBusMessage::ErrorMessage) {
		if (connected) {
			bus.disconnect(PORTAL_SERVICE, predictedPath, PORTAL_REQUEST_IFACE, "Response", "ua{sv}",
						   &receiver, SLOT(handleResponse(uint, QVariantMap)));
		}
		result.errorMessage =
			QStringLiteral("Portal %1 failed: %2").arg(methodName, reply.errorMessage());
		return result;
	}

	QString actualRequestPath = predictedPath;

	if (!reply.arguments().isEmpty()) {
		const QString returnedPath = reply.arguments().first().value< QDBusObjectPath >().path();
		if (!returnedPath.isEmpty() && returnedPath != predictedPath) {
			if (connected) {
				bus.disconnect(PORTAL_SERVICE, predictedPath, PORTAL_REQUEST_IFACE, "Response", "ua{sv}",
							   &receiver, SLOT(handleResponse(uint, QVariantMap)));
			}
			actualRequestPath = returnedPath;
			connected = bus.connect(PORTAL_SERVICE, actualRequestPath, PORTAL_REQUEST_IFACE, "Response",
									"ua{sv}", &receiver, SLOT(handleResponse(uint, QVariantMap)));
		}
	}

	if (!connected) {
		result.errorMessage =
			QStringLiteral("Failed to connect to the portal Response signal on %1.").arg(actualRequestPath);
		return result;
	}

	QTimer timeoutTimer;
	timeoutTimer.setSingleShot(true);
	QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);

	timeoutTimer.start(timeoutMsec);
	loop.exec();
	timeoutTimer.stop();

	bus.disconnect(PORTAL_SERVICE, actualRequestPath, PORTAL_REQUEST_IFACE, "Response", "ua{sv}",
				   &receiver, SLOT(handleResponse(uint, QVariantMap)));

	if (!receiver.received) {
		result.errorMessage = QStringLiteral("Portal %1 timed out after %2 ms.").arg(methodName).arg(timeoutMsec);
		return result;
	}

	if (receiver.responseCode != 0) {
		result.errorMessage =
			QStringLiteral("Portal %1 was cancelled or failed (response code %2).")
				.arg(methodName)
				.arg(receiver.responseCode);
		return result;
	}

	result.ok      = true;
	result.results = receiver.results;

	const QVariant &sessionVar = receiver.results.value(QStringLiteral("session_handle"));
	if (sessionVar.canConvert< QDBusObjectPath >()) {
		result.sessionHandle = sessionVar.value< QDBusObjectPath >().path();
	} else if (sessionVar.canConvert< QString >()) {
		result.sessionHandle = sessionVar.toString();
	}

	return result;
}

struct StreamInfo {
	quint32 nodeId = 0;
	quint32 width  = 0;
	quint32 height = 0;
	QString sourceType;
};

QVariantMap parseVardictArgument(const QDBusArgument &arg) {
	QVariantMap props;
	arg.beginMap();
	while (!arg.atEnd()) {
		arg.beginMapEntry();
		QString key;
		QDBusVariant value;
		arg >> key >> value;
		props.insert(key, value.variant());
		arg.endMapEntry();
	}
	arg.endMap();
	return props;
}

QVector< StreamInfo > parseStreams(const QVariant &streamsVar) {
	QVector< StreamInfo > streams;
	if (!streamsVar.isValid() || !streamsVar.canConvert< QDBusArgument >()) {
		return streams;
	}

	const QDBusArgument arg = streamsVar.value< QDBusArgument >();
	if (arg.currentType() != QDBusArgument::ArrayType) {
		return streams;
	}

	arg.beginArray();
	while (!arg.atEnd()) {
		arg.beginStructure();

		quint32 nodeId = 0;
		arg >> nodeId;

		QVariantMap props;
		const QVariant propVariant = arg.asVariant();
		if (propVariant.canConvert< QVariantMap >()) {
			props = propVariant.value< QVariantMap >();
		} else if (propVariant.canConvert< QDBusArgument >()) {
			const QDBusArgument propArg = propVariant.value< QDBusArgument >();
			if (propArg.currentType() == QDBusArgument::MapType) {
				props = parseVardictArgument(propArg);
			}
		}

		arg.endStructure();

		StreamInfo info;
		info.nodeId = nodeId;

		const QVariant sizeVar = props.value(QStringLiteral("size"));
		if (sizeVar.canConvert< QDBusArgument >()) {
			const QDBusArgument sizeArg = sizeVar.value< QDBusArgument >();
			if (sizeArg.currentType() == QDBusArgument::StructureType) {
				sizeArg.beginStructure();
				int w = 0, h = 0;
				sizeArg >> w >> h;
				sizeArg.endStructure();
				info.width  = static_cast< quint32 >(w);
				info.height = static_cast< quint32 >(h);
			}
		}

		const uint sourceType = props.value(QStringLiteral("source_type")).toUInt();
		if (sourceType == 1) {
			info.sourceType = QStringLiteral("monitor");
		} else if (sourceType == 2) {
			info.sourceType = QStringLiteral("window");
		}

		streams.append(info);
	}
	arg.endArray();

	return streams;
}
} // namespace

QString ScreenShareLinuxPortalCapture::backendToken() {
	return QStringLiteral("xdg-portal-pipewire");
}

ScreenShareLinuxPortalCapture::Capability ScreenShareLinuxPortalCapture::probe() {
	Capability capability;
	capability.backendToken = backendToken();

	QDBusConnection bus = QDBusConnection::sessionBus();
	if (!bus.isConnected()) {
		capability.detail = QStringLiteral("D-Bus session bus is not connected.");
		return capability;
	}

	QDBusInterface portal(PORTAL_SERVICE, PORTAL_PATH, PORTAL_SCREENCAST_IFACE, bus);
	if (!portal.isValid()) {
		capability.detail = QStringLiteral("The XDG desktop portal ScreenCast interface is not available.");
		return capability;
	}

	const QStringList services = bus.interface()->registeredServiceNames();
	bool portalRegistered = false;
	for (const QString &service : services) {
		if (service == PORTAL_SERVICE) {
			portalRegistered = true;
			break;
		}
	}

	if (!portalRegistered) {
		capability.detail = QStringLiteral("org.freedesktop.portal.Desktop is not registered on the session bus.");
		return capability;
	}

	capability.portalAvailable = true;
	capability.detail          = QStringLiteral("XDG desktop portal ScreenCast interface is available.");
	return capability;
}

ScreenShareLinuxPortalCapture::NegotiationResult
ScreenShareLinuxPortalCapture::negotiate(const QString &captureSourceId, int timeoutMsec) {
	NegotiationResult result;

	QDBusConnection bus = QDBusConnection::sessionBus();
	if (!bus.isConnected()) {
		result.errorMessage = QStringLiteral("D-Bus session bus is not connected.");
		return result;
	}

	QDBusInterface screenCast(PORTAL_SERVICE, PORTAL_PATH, PORTAL_SCREENCAST_IFACE, bus);
	if (!screenCast.isValid()) {
		result.errorMessage = QStringLiteral("The XDG desktop portal ScreenCast interface is not available.");
		return result;
	}

	// Step 1: CreateSession
	const QString sessionToken = uniqueHandleToken();
	QVariantMap createOptions;
	createOptions[QStringLiteral("session_handle_token")] = sessionToken;

	PortalCallResult createResult =
		callPortalMethod(screenCast, QStringLiteral("CreateSession"),
						 QVariantList{ QVariant(createOptions) }, sessionToken, timeoutMsec);
	if (!createResult.ok) {
		result.errorMessage = createResult.errorMessage;
		return result;
	}

	const QString sessionHandle = createResult.sessionHandle;
	if (sessionHandle.isEmpty()) {
		result.errorMessage = QStringLiteral("Portal CreateSession did not return a session handle.");
		return result;
	}

	// Any failure after CreateSession must release the session we created, otherwise the
	// ScreenCast session (and the compositor's "sharing" state / captured PipeWire node) is
	// leaked when the user cancels the picker or a later step fails.
	const auto failWith = [&](const QString &errorMessage) {
		result.errorMessage = errorMessage;
		if (!sessionHandle.trimmed().isEmpty()) {
			close(sessionHandle);
		}
		return result;
	};

	// Step 2: SelectSources
	const QString selectToken = uniqueHandleToken();

	uint sourceTypes = 1 | 2;
	uint cursorMode  = 2;

	const QString normalizedSource = captureSourceId.trimmed().toLower();
	if (normalizedSource.isEmpty() || normalizedSource == QLatin1String("monitor")
		|| normalizedSource == QLatin1String("screen") || normalizedSource == QLatin1String("primary-monitor")
		|| normalizedSource == QLatin1String("primary") || normalizedSource.startsWith(QLatin1String("monitor:"))
		|| normalizedSource.startsWith(QLatin1String("screen:"))) {
		sourceTypes = 1;
	} else if (normalizedSource.startsWith(QLatin1String("window:"))) {
		sourceTypes = 2;
	}

	QVariantMap selectOptions;
	selectOptions[QStringLiteral("types")]       = sourceTypes;
	selectOptions[QStringLiteral("cursor_mode")] = cursorMode;
	selectOptions[QStringLiteral("multiple")]    = false;

	PortalCallResult selectResult =
		callPortalMethod(screenCast, QStringLiteral("SelectSources"),
						 QVariantList{ QVariant(QDBusObjectPath(sessionHandle)), QVariant(selectOptions) },
						 selectToken, timeoutMsec);
	if (!selectResult.ok) {
		return failWith(selectResult.errorMessage);
	}

	// Step 3: Start
	const QString startToken = uniqueHandleToken();

	PortalCallResult startResult =
		callPortalMethod(screenCast, QStringLiteral("Start"),
						 QVariantList{ QVariant(QDBusObjectPath(sessionHandle)), QVariant(QString()),
									   QVariant(QVariantMap()) },
						 startToken, timeoutMsec);
	if (!startResult.ok) {
		return failWith(startResult.errorMessage);
	}

	const QVector< StreamInfo > streams = parseStreams(startResult.results.value(QStringLiteral("streams")));
	if (streams.isEmpty()) {
		return failWith(QStringLiteral("Portal Start did not return any streams."));
	}

	const StreamInfo &first = streams.first();
	result.valid       = true;
	result.nodeId      = first.nodeId;
	result.width       = first.width;
	result.height      = first.height;
	result.sourceType  = first.sourceType;
	result.sessionHandle = sessionHandle;
	return result;
}

void ScreenShareLinuxPortalCapture::close(const QString &sessionHandle) {
	if (sessionHandle.trimmed().isEmpty()) {
		return;
	}

	QDBusConnection bus = QDBusConnection::sessionBus();
	if (!bus.isConnected()) {
		return;
	}

	// org.freedesktop.portal.ScreenCast.Close(session_handle) is a plain, non-interactive method
	// call; closing an already-closed session is a no-op from the portal's perspective.
	QDBusMessage message = QDBusMessage::createMethodCall(
		PORTAL_SERVICE, PORTAL_PATH, PORTAL_SCREENCAST_IFACE, QStringLiteral("Close"));
	message << QVariant(QDBusObjectPath(sessionHandle.trimmed()));
	bus.send(message);
}

#include "ScreenShareLinuxPortalCapture.moc"

#endif // Q_OS_LINUX
