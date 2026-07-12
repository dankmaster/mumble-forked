// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_CUSTOMELEMENTS_H_
#define MUMBLE_MUMBLE_CUSTOMELEMENTS_H_

#include <QtCore/QObject>
#include <QtCore/QUrl>
#include <QtWidgets/QLabel>
#include <QtWidgets/QTextBrowser>
#include <QtWidgets/QTextEdit>

class QMouseEvent;
class QWheelEvent;

class LogTextBrowser : public QTextBrowser {
private:
	Q_OBJECT
	Q_DISABLE_COPY(LogTextBrowser)

public:
	LogTextBrowser(QWidget *p = nullptr);

	int getLogScroll();
	void setLogScroll(int scroll_pos);
	bool isScrolledToBottom();
	void resetViewportChrome();
	QTextCursor imageCursorAt(const QPoint &position) const;
	QSize minimumSizeHint() const Q_DECL_OVERRIDE;
	QSize sizeHint() const Q_DECL_OVERRIDE;
	bool hasHeightForWidth() const Q_DECL_OVERRIDE;
	int heightForWidth(int width) const Q_DECL_OVERRIDE;

signals:
	void imageActivated(const QTextCursor &cursor);
	void contentWidthChanged(int width);

protected:
	void mouseReleaseEvent(QMouseEvent *event) Q_DECL_OVERRIDE;
	void mouseDoubleClickEvent(QMouseEvent *event) Q_DECL_OVERRIDE;
	void resizeEvent(QResizeEvent *event) Q_DECL_OVERRIDE;
	QSize viewportSizeHint() const Q_DECL_OVERRIDE;
	void wheelEvent(QWheelEvent *event) Q_DECL_OVERRIDE;
};

#endif // CUSTOMELEMENTS_H_
