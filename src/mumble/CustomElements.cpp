// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "CustomElements.h"

#include "ClientUser.h"
#include "Log.h"
#include "MainWindow.h"
#include "QtWidgetUtils.h"
#include "Utils.h"
#include "Global.h"

#include <QMimeData>
#include <QtCore/QTimer>
#include <QtGui/QAbstractTextDocumentLayout>
#include <QtGui/QClipboard>
#include <QtGui/QContextMenuEvent>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QWheelEvent>
#include <QtWidgets/QScrollBar>

namespace {
	constexpr int ChatbarMinimumHeight         = 26;
	constexpr int ChatbarVisibleLineCount      = 5;
	constexpr int ChatbarVerticalChromePadding = 6;
}

LogTextBrowser::LogTextBrowser(QWidget *p) : QTextBrowser(p) {
}

int LogTextBrowser::getLogScroll() {
	return verticalScrollBar()->value();
}

void LogTextBrowser::setLogScroll(int scroll_pos) {
	verticalScrollBar()->setValue(scroll_pos);
}

bool LogTextBrowser::isScrolledToBottom() {
	const QScrollBar *scrollBar = verticalScrollBar();
	return scrollBar->value() == scrollBar->maximum();
}

void LogTextBrowser::resetViewportChrome() {
	setViewportMargins(0, 0, 0, 0);
	setContentsMargins(0, 0, 0, 0);
	if (QWidget *view = viewport()) {
		view->setContentsMargins(0, 0, 0, 0);
	}
}

QTextCursor LogTextBrowser::imageCursorAt(const QPoint &position) const {
	QTextCursor cursor  = cursorForPosition(position);
	QTextCharFormat fmt = cursor.charFormat();

	if (fmt.objectType() == QTextFormat::NoObject) {
		cursor.movePosition(QTextCursor::NextCharacter);
		fmt = cursor.charFormat();
	}

	if (fmt.isImageFormat()) {
		return cursor;
	}

	return QTextCursor();
}

QSize LogTextBrowser::minimumSizeHint() const {
	const QVariant explicitSizeHint = property("persistentChatExplicitSizeHint");
	if (explicitSizeHint.isValid()) {
		const QSize sizeHint = explicitSizeHint.toSize();
		if (sizeHint.isValid()) {
			return sizeHint;
		}
	}

	return QTextBrowser::minimumSizeHint();
}

QSize LogTextBrowser::sizeHint() const {
	const QVariant explicitSizeHint = property("persistentChatExplicitSizeHint");
	if (explicitSizeHint.isValid()) {
		const QSize sizeHint = explicitSizeHint.toSize();
		if (sizeHint.isValid()) {
			return sizeHint;
		}
	}

	return QTextBrowser::sizeHint();
}

bool LogTextBrowser::hasHeightForWidth() const {
	const QVariant explicitSizeHint = property("persistentChatExplicitSizeHint");
	if (explicitSizeHint.isValid()) {
		const QSize sizeHint = explicitSizeHint.toSize();
		if (sizeHint.isValid()) {
			return true;
		}
	}

	return QTextBrowser::hasHeightForWidth();
}

int LogTextBrowser::heightForWidth(int width) const {
	Q_UNUSED(width);

	const QVariant explicitSizeHint = property("persistentChatExplicitSizeHint");
	if (explicitSizeHint.isValid()) {
		const QSize sizeHint = explicitSizeHint.toSize();
		if (sizeHint.isValid()) {
			return sizeHint.height();
		}
	}

	return QTextBrowser::heightForWidth(width);
}

void LogTextBrowser::mouseReleaseEvent(QMouseEvent *event) {
	if (event && event->button() == Qt::LeftButton && anchorAt(event->pos()).isEmpty()) {
		const QTextCursor imageCursor = imageCursorAt(event->pos());
		if (!imageCursor.isNull()) {
			emit imageActivated(imageCursor);
			event->accept();
			return;
		}
	}

	QTextBrowser::mouseReleaseEvent(event);
}

void LogTextBrowser::mouseDoubleClickEvent(QMouseEvent *event) {
	const QTextCursor imageCursor = imageCursorAt(event->pos());
	if (!imageCursor.isNull()) {
		emit imageActivated(imageCursor);
		event->accept();
		return;
	}

	QTextBrowser::mouseDoubleClickEvent(event);
}

void LogTextBrowser::resizeEvent(QResizeEvent *event) {
	QTextBrowser::resizeEvent(event);

	if (event->size().width() != event->oldSize().width()) {
		emit contentWidthChanged(viewport()->width());
	}
}

QSize LogTextBrowser::viewportSizeHint() const {
	const QVariant explicitSizeHint = property("persistentChatExplicitSizeHint");
	if (explicitSizeHint.isValid()) {
		const QSize sizeHint = explicitSizeHint.toSize();
		if (sizeHint.isValid()) {
			return sizeHint;
		}
	}

	return QTextBrowser::viewportSizeHint();
}

void LogTextBrowser::wheelEvent(QWheelEvent *event) {
	if (property("persistentChatEmbeddedBrowser").toBool()) {
		event->ignore();
		return;
	}

	QTextBrowser::wheelEvent(event);
}
