// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ModernDialogHost.h"

#if defined(MUMBLE_HAS_MODERN_LAYOUT)

#include "ModernShellBridge.h"
#include "ModernShellPage.h"

#include <QtCore/QUrl>
#include <QtGui/QCloseEvent>
#include <QtWidgets/QVBoxLayout>
#include <QtWebChannel/QWebChannel>
#include <QtWebEngineCore/QWebEngineSettings>
#include <QtWebEngineWidgets/QWebEngineView>

namespace {
	QUrl modernDialogUrl() {
		return QUrl(QStringLiteral("qrc:/modern-shell/dialog.html"));
	}
} // namespace

ModernDialogHost::ModernDialogHost(ModernShellBridge *bridge, QWidget *parent)
	: QDialog(parent), m_bridge(bridge) {
	setAttribute(Qt::WA_DeleteOnClose, false);
	setWindowModality(Qt::NonModal);
	setWindowTitle(tr("Mumble"));
	setMinimumSize(560, 360);
	resize(760, 560);

	m_layout = new QVBoxLayout(this);
	m_layout->setContentsMargins(0, 0, 0, 0);
	m_layout->setSpacing(0);

	m_view = new QWebEngineView(this);
	m_view->setContextMenuPolicy(Qt::NoContextMenu);
	m_layout->addWidget(m_view);

	m_page = new ModernShellPage(m_view);
	m_view->setPage(m_page);

	m_channel = new QWebChannel(this);
	if (m_bridge) {
		m_channel->registerObject(QStringLiteral("modernBridge"), m_bridge);
	}
	m_page->setWebChannel(m_channel);

	m_view->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);
	m_view->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, false);
	m_view->settings()->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, false);

	connect(m_view, &QWebEngineView::loadFinished, this, &ModernDialogHost::handleLoadFinished);
	connect(m_page, &QWebEnginePage::renderProcessTerminated, this,
			&ModernDialogHost::handleRenderProcessTerminated);
	connect(m_page, &ModernShellPage::externalNavigationRequested, this, [](const QUrl &url) {
		Q_UNUSED(url);
	});
}

bool ModernDialogHost::showDialogState(const QVariantMap &state, QString *errorMessage) {
	if (!state.value(QStringLiteral("open")).toBool()) {
		hideDialog();
		return true;
	}

	if (!m_bridge) {
		if (errorMessage) {
			*errorMessage = tr("The modern dialog bridge is unavailable.");
		}
		return false;
	}

	if (!start(errorMessage)) {
		return false;
	}

	const QString nextDialogID = state.value(QStringLiteral("id")).toString();
	const bool shouldPresent   = !m_open || !isVisible() || m_currentDialogID != nextDialogID;
	m_open                    = true;
	m_currentDialogID         = nextDialogID;
	const QString title = state.value(QStringLiteral("title")).toString().trimmed();
	setWindowTitle(title.isEmpty() ? tr("Mumble") : title);
	applyDialogGeometry(state);

	if (shouldPresent) {
		show();
		raise();
		activateWindow();
	}
	return true;
}

void ModernDialogHost::hideDialog() {
	m_open = false;
	m_currentDialogID.clear();
	hide();
}

void ModernDialogHost::closeEvent(QCloseEvent *event) {
	if (m_open && !m_currentDialogID.isEmpty()) {
		event->ignore();
		const QString dialogID = m_currentDialogID;
		hide();
		emit nativeCloseRequested(dialogID);
		return;
	}

	QDialog::closeEvent(event);
}

bool ModernDialogHost::start(QString *errorMessage) {
	if (m_started) {
		return true;
	}

	if (!m_view) {
		if (errorMessage) {
			*errorMessage = tr("The modern dialog view could not be initialized.");
		}
		return false;
	}

	const QUrl url = modernDialogUrl();
	if (!url.isValid() || url.isEmpty()) {
		if (errorMessage) {
			*errorMessage = tr("The modern dialog URL is invalid.");
		}
		return false;
	}

	m_view->load(url);
	m_started = true;
	return true;
}

void ModernDialogHost::applyDialogGeometry(const QVariantMap &state) {
	const QString kind = state.value(QStringLiteral("kind")).toString();
	QSize desiredSize(760, 560);

	if (kind == QLatin1String("connect")) {
		desiredSize = QSize(880, 620);
	} else if (kind == QLatin1String("settings")) {
		desiredSize = QSize(980, 700);
	} else if (kind == QLatin1String("failedConnection") || kind == QLatin1String("migrationNotice")) {
		desiredSize = QSize(600, 420);
	}

	const int requestedWidth  = state.value(QStringLiteral("width")).toInt();
	const int requestedHeight = state.value(QStringLiteral("height")).toInt();
	if (requestedWidth > 0 && requestedHeight > 0) {
		desiredSize = QSize(requestedWidth, requestedHeight);
	}

	if (!isVisible()) {
		resize(desiredSize);
	}
}

void ModernDialogHost::handleLoadFinished(const bool ok) {
	if (ok) {
		return;
	}

	m_started = false;
	emit hostFailed(tr("The modern dialog window failed to load its local web assets."));
}

void ModernDialogHost::handleRenderProcessTerminated(const QWebEnginePage::RenderProcessTerminationStatus status,
													 const int exitCode) {
	Q_UNUSED(status);
	m_started = false;
	emit hostFailed(tr("The modern dialog renderer stopped unexpectedly with exit code %1.").arg(exitCode));
}

#endif // defined(MUMBLE_HAS_MODERN_LAYOUT)
