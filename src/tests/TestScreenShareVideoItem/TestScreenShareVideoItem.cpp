// Copyright The Mumble Developers. All rights reserved.

#include "ScreenShareFrameTransport.h"
#include "ScreenShareVideoItem.h"
#include "ScreenShareViewBackend.h"

#include <QtCore/QUuid>
#include <QtQuick/QQuickWindow>
#include <QtTest>

class TestScreenShareVideoItem : public QObject {
	Q_OBJECT
private slots:
	void rendersTransportFrameIntoSceneGraph();
};

void TestScreenShareVideoItem::rendersTransportFrameIntoSceneGraph() {
	const QString key = QStringLiteral("mumble-qsg-frame-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
	Mumble::ScreenShare::FrameTransport producer;
	QVERIFY(producer.create(key, 4U * 4U * 4U));
	ScreenShareSession session;
	ScreenShareViewBackend backend(session);
	ScreenShareVideoItem item;
	QQuickWindow window;
	window.setColor(Qt::black);
	window.resize(32, 32);
	item.setParentItem(window.contentItem());
	item.setSize(QSizeF(32, 32));
	item.setBackend(&backend);
	window.show();
	backend.setNativeFrameTransport(key, 11);
	QTRY_VERIFY(backend.nativeFrameActive());

	Mumble::ScreenShare::NativeFrame frame;
	frame.generation = 11;
	frame.sequence = 1;
	frame.width = 4;
	frame.height = 4;
	frame.stride = 16;
	frame.bgra.resize(64);
	for (int offset = 0; offset < frame.bgra.size(); offset += 4) {
		frame.bgra[offset] = 0;
		frame.bgra[offset + 1] = 0;
		frame.bgra[offset + 2] = static_cast< char >(0xff);
		frame.bgra[offset + 3] = static_cast< char >(0xff);
	}
	QVERIFY(producer.publish(frame));
	QTRY_VERIFY(!backend.currentFrame().isNull());
	QTRY_VERIFY_WITH_TIMEOUT(window.grabWindow().pixelColor(16, 16).red() > 200, 2000);
}

QTEST_MAIN(TestScreenShareVideoItem)
#include "TestScreenShareVideoItem.moc"
