// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "NetworkConfig.h"

#include "OSInfo.h"
#include "Version.h"
#include "Global.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkProxy>

namespace {

QString startWithPCRunValueName() {
	return QStringLiteral("mumble-forked");
}

QString startWithPCCommand() {
	return QStringLiteral("\"%1\" --hidden").arg(QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
}

} // namespace

static QNetworkProxy::ProxyType local_to_qt_proxy(Settings::ProxyType pt) {
	switch (pt) {
		case Settings::NoProxy:
			return QNetworkProxy::NoProxy;
		case Settings::HttpProxy:
			return QNetworkProxy::HttpProxy;
		case Settings::Socks5Proxy:
			return QNetworkProxy::Socks5Proxy;
	}

	return QNetworkProxy::NoProxy;
}

void NetworkConfig::SetupProxy() {
	QNetworkProxy proxy;
	proxy.setType(local_to_qt_proxy(Global::get().s.ptProxyType));
	proxy.setHostName(Global::get().s.qsProxyHost);
	proxy.setPort(Global::get().s.usProxyPort);
	proxy.setUser(Global::get().s.qsProxyUsername);
	proxy.setPassword(Global::get().s.qsProxyPassword);
	QNetworkProxy::setApplicationProxy(proxy);
}

bool NetworkConfig::ApplyStartWithPCRegistration(const bool enabled) {
#ifdef Q_OS_WIN
	QSettings runKey(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
					 QSettings::NativeFormat);
	if (enabled) {
		runKey.setValue(startWithPCRunValueName(), startWithPCCommand());
	} else {
		runKey.remove(startWithPCRunValueName());
	}
	runKey.sync();
	return runKey.status() == QSettings::NoError;
#else
	Q_UNUSED(enabled);
	return false;
#endif
}

bool NetworkConfig::TcpModeEnabled() {
	/*
	 * We force TCP mode for both HTTP and SOCKS5 proxies, even though SOCKS5 supports UDP.
	 *
	 * This is because Qt's automatic application-wide proxying fails when we're in UDP
	 * mode since the datagram transmission code assumes that its socket is created in its
	 * own thread. Due to the automatic proxying, this assumption is incorrect, because of
	 * Qt's behind-the-scenes magic.
	 *
	 * However, TCP mode uses Qt events to make sure packets are sent off from the right
	 * thread, and this is what we utilize here.
	 *
	 * This is probably not even something that should even be taken care of, as proxying
	 * itself already is a potential latency killer.
	 */

	return Global::get().s.ptProxyType != Settings::NoProxy || Global::get().s.bTCPCompat;
}

QNetworkReply *Network::get(const QUrl &url) {
	QNetworkRequest req(url);
	prepareRequest(req);
	return Global::get().nam->get(req);
}

void Network::prepareRequest(QNetworkRequest &req) {
	req.setAttribute(QNetworkRequest::HttpPipeliningAllowedAttribute, true);
	req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);

	// Do not send OS information if the corresponding privacy setting is enabled
	if (Global::get().s.bHideOS) {
		req.setRawHeader(QString::fromLatin1("User-Agent").toUtf8(),
						 QString::fromLatin1("Mozilla/5.0 Mumble/%1").arg(Version::getRelease()).toUtf8());
	} else {
		req.setRawHeader(QString::fromLatin1("User-Agent").toUtf8(),
						 QString::fromLatin1("Mozilla/5.0 (%1; %2) Mumble/%3")
							 .arg(OSInfo::getOS(), OSInfo::getOSVersion(), Version::getRelease())
							 .toUtf8());
	}
}
