// Copyright The Mumble Developers. All rights reserved.

#ifndef MUMBLE_SCREENSHAREFRAMETRANSPORT_H_
#define MUMBLE_SCREENSHAREFRAMETRANSPORT_H_

#include <QtCore/QByteArray>
#include <QtCore/QSharedMemory>
#include <QtCore/QString>

namespace Mumble::ScreenShare {

struct NativeFrame {
	quint64 generation = 0;
	quint64 sequence = 0;
	qint64 timestampUsec = 0;
	quint32 width = 0;
	quint32 height = 0;
	quint32 stride = 0;
	QByteArray bgra;
};

class FrameTransport final {
public:
	static constexpr quint32 SlotCount = 3;
	static constexpr quint32 DefaultSlotBytes = 1920U * 1080U * 4U;

	FrameTransport();
	~FrameTransport();
	FrameTransport(const FrameTransport &) = delete;
	FrameTransport &operator=(const FrameTransport &) = delete;

	bool create(const QString &key, quint32 slotBytes = DefaultSlotBytes);
	bool attach(const QString &key);
	void detach();
	bool publish(const NativeFrame &frame);
	bool readLatest(NativeFrame *frame);
	QString key() const;
	quint64 droppedFrames() const;

private:
	QSharedMemory m_memory;
	quint64 m_lastSequence = 0;
	quint64 m_droppedFrames = 0;
	quint64 m_generation = 0;
};

} // namespace Mumble::ScreenShare

#endif
