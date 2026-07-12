// Copyright The Mumble Developers. All rights reserved.
#include "ScreenShareVideoItem.h"
#include "ScreenShareViewBackend.h"

#include <QtQuick/QQuickWindow>
#include <QtQuick/QSGSimpleTextureNode>

ScreenShareVideoItem::ScreenShareVideoItem(QQuickItem *parent) : QQuickItem(parent) {
	setFlag(ItemHasContents, true);
}
QObject *ScreenShareVideoItem::backend() const { return m_backend; }
void ScreenShareVideoItem::setBackend(QObject *backend) {
	ScreenShareViewBackend *viewBackend = qobject_cast< ScreenShareViewBackend * >(backend);
	if (m_backend == viewBackend) return;
	if (m_backend) disconnect(m_backend, nullptr, this, nullptr);
	m_backend = viewBackend;
	if (m_backend) connect(m_backend, &ScreenShareViewBackend::frameChanged, this, &ScreenShareVideoItem::refreshFrame);
	refreshFrame();
	emit backendChanged();
}
void ScreenShareVideoItem::refreshFrame() {
	m_frame = m_backend ? m_backend->currentFrame() : QImage();
	update();
}
QSGNode *ScreenShareVideoItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) {
	auto *node = static_cast< QSGSimpleTextureNode * >(oldNode);
	const QImage frame = m_frame;
	if (frame.isNull() || !window()) {
		if (node) {
			QSGTexture *texture = node->texture();
			node->setOwnsTexture(false);
			delete node;
			delete texture;
		}
		return nullptr;
	}
	if (!node) node = new QSGSimpleTextureNode();
	QSGTexture *texture = window()->createTextureFromImage(frame);
	QSGTexture *previousTexture = node->texture();
	node->setOwnsTexture(false);
	node->setTexture(texture);
	delete previousTexture;
	node->setOwnsTexture(true);
	QSizeF fitted = frame.size();
	fitted.scale(boundingRect().size(), Qt::KeepAspectRatio);
	node->setRect(QRectF((width() - fitted.width()) / 2.0, (height() - fitted.height()) / 2.0, fitted.width(),
						 fitted.height()));
	node->setFiltering(QSGTexture::Linear);
	return node;
}
