// Copyright The Mumble Developers. All rights reserved.
#ifndef MUMBLE_MUMBLE_SCREENSHAREVIDEOITEM_H_
#define MUMBLE_MUMBLE_SCREENSHAREVIDEOITEM_H_

#include <QtQuick/QQuickItem>
#include <QtCore/QPointer>
#include <QtGui/QImage>

class ScreenShareViewBackend;

class ScreenShareVideoItem : public QQuickItem {
	Q_OBJECT
	Q_PROPERTY(QObject *backend READ backend WRITE setBackend NOTIFY backendChanged)
public:
	explicit ScreenShareVideoItem(QQuickItem *parent = nullptr);
	QObject *backend() const;
	void setBackend(QObject *backend);
signals:
	void backendChanged();
protected:
	QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;
private:
	void refreshFrame();
	QPointer< ScreenShareViewBackend > m_backend;
	QImage m_frame;
};

#endif
