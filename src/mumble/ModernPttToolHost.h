// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef MUMBLE_MUMBLE_MODERNPTTTOOLHOST_H_
#define MUMBLE_MUMBLE_MODERNPTTTOOLHOST_H_

#include <QtWidgets/QWidget>

class QCloseEvent;
class QFocusEvent;
class QHideEvent;
class QWebChannel;
class QWebEngineView;

class ModernPttToolHost : public QWidget {
	Q_OBJECT

public:
	explicit ModernPttToolHost(QWidget *parent = nullptr);
	Q_INVOKABLE void setPressed(bool pressed);

signals:
	void triggered(bool pressed, const QVariant &data);

protected:
	void closeEvent(QCloseEvent *event) override;
	void focusOutEvent(QFocusEvent *event) override;
	void hideEvent(QHideEvent *event) override;

private:
	QWebEngineView *m_view = nullptr;
	QWebChannel *m_channel = nullptr;
	bool m_pressed = false;

	void release();
};

#endif // MUMBLE_MUMBLE_MODERNPTTTOOLHOST_H_
