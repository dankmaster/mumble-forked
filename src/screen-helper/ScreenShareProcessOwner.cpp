// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.
#include "ScreenShareProcessOwner.h"

#ifdef Q_OS_WIN
# ifndef NOMINMAX
#  define NOMINMAX
# endif
# include <QtCore/QWinEventNotifier>
# include <windows.h>
#endif

#include <limits>

struct ScreenShareProcessOwner::Implementation {
#ifdef Q_OS_WIN
	HANDLE handle = nullptr;
	QWinEventNotifier *notifier = nullptr;
	~Implementation() {
		delete notifier;
		if (handle) CloseHandle(handle);
	}
#endif
};

#ifdef Q_OS_WIN
namespace {
QString processIdentity(HANDLE handle) {
	FILETIME created {}, exited {}, kernel {}, user {};
	if (!GetProcessTimes(handle, &created, &exited, &kernel, &user)) return {};
	const quint64 timestamp = (static_cast< quint64 >(created.dwHighDateTime) << 32) | created.dwLowDateTime;
	return QString::number(timestamp, 16);
}

HANDLE openOwner(qint64 processID) {
	if (processID <= 0 || processID > std::numeric_limits< DWORD >::max()) return nullptr;
	return OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast< DWORD >(processID));
}
}
#endif

ScreenShareProcessOwner::ScreenShareProcessOwner(QObject *parent)
	: QObject(parent), m_impl(std::make_unique< Implementation >()) {}

ScreenShareProcessOwner::~ScreenShareProcessOwner() = default;

QString ScreenShareProcessOwner::identityForProcess(qint64 processID) {
#ifdef Q_OS_WIN
	HANDLE handle = openOwner(processID);
	if (!handle) return {};
	const QString identity = processIdentity(handle);
	CloseHandle(handle);
	return identity;
#else
	(void) processID;
	return {};
#endif
}

bool ScreenShareProcessOwner::watch(qint64 processID, const QString &identity,
									std::function< void() > exited, QString *error) {
#ifdef Q_OS_WIN
	if (m_impl->handle) {
		if (error) *error = QStringLiteral("A screen sharing job cannot change its owning process.");
		return false;
	}
	HANDLE handle = openOwner(processID);
	if (!handle || identity.isEmpty() || processIdentity(handle) != identity
		|| WaitForSingleObject(handle, 0) != WAIT_TIMEOUT) {
		if (handle) CloseHandle(handle);
		if (error) *error = QStringLiteral("The screen sharing client process has exited or its identity is invalid.");
		return false;
	}
	m_impl->handle = handle;
	m_impl->notifier = new QWinEventNotifier(handle, this);
	connect(m_impl->notifier, &QWinEventNotifier::activated, this, [this, exited = std::move(exited)]() {
		m_impl->notifier->setEnabled(false);
		if (exited) exited();
	});
	// A process exiting between the check and registration leaves the same
	// handle signalled. The notifier will still deliver its termination.
	return true;
#else
	(void) processID;
	(void) identity;
	(void) exited;
	if (error) *error = QStringLiteral("Process ownership is unavailable on this platform.");
	return false;
#endif
}
