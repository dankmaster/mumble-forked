// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "ServerConnectionUtils.h"

#include "Net.h"

#include <QtCore/QMimeData>
#include <QtCore/QUrl>
#include <QtCore/QUrlQuery>

QMimeData *ServerConnectionUtils::createServerUrlMimeData(const QString &name, const QString &host,
												   unsigned short port, const QString &channel) {
	QUrl url;
	url.setScheme(QStringLiteral("mumble"));
	url.setHost(host);
	if (port != DEFAULT_MUMBLE_PORT) {
		url.setPort(port);
	}
	url.setPath(channel);

	QUrlQuery query;
	query.addQueryItem(QStringLiteral("title"), name);
	query.addQueryItem(QStringLiteral("version"), QStringLiteral("1.2.0"));
	url.setQuery(query);

	const QString encoded = QString::fromLatin1(url.toEncoded());
	auto *mime            = new QMimeData;
	mime->setUrls({ url });
	mime->setText(encoded);
	mime->setHtml(QStringLiteral("<a href=\"%1\">%2</a>").arg(encoded, name.toHtmlEscaped()));
	return mime;
}
