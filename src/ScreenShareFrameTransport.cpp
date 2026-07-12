// Copyright The Mumble Developers. All rights reserved.

#include "ScreenShareFrameTransport.h"

#include <cstring>
#include <limits>

namespace Mumble::ScreenShare {
namespace {
constexpr quint32 Magic = 0x4d534632; // MSF2
struct Header {
	quint32 magic;
	quint32 slotBytes;
	quint32 slotCount;
	quint32 latestSlot;
	quint64 generation;
	quint64 sequence;
	qint64 timestampUsec;
	quint32 width;
	quint32 height;
	quint32 stride;
	quint32 payloadBytes;
};

bool validDimensions(const quint32 width, const quint32 height, const quint32 stride, const quint32 payloadBytes,
					 const quint32 slotBytes) {
	if (width == 0 || height == 0 || stride == 0 || payloadBytes == 0 || payloadBytes > slotBytes) return false;
	const quint64 minimumStride = static_cast< quint64 >(width) * 4U;
	const quint64 expectedBytes = static_cast< quint64 >(stride) * height;
	return minimumStride <= stride && expectedBytes == payloadBytes && expectedBytes <= slotBytes;
}
}

FrameTransport::FrameTransport() = default;
FrameTransport::~FrameTransport() { detach(); }

bool FrameTransport::create(const QString &key, quint32 slotBytes) {
	detach();
	if (key.trimmed().isEmpty() || slotBytes == 0) return false;
	m_memory.setKey(key);
	if (!m_memory.create(static_cast< qsizetype >(sizeof(Header)) + static_cast< qsizetype >(slotBytes) * SlotCount))
		return false;
	if (!m_memory.lock()) {
		detach();
		return false;
	}
	std::memset(m_memory.data(), 0, static_cast< size_t >(m_memory.size()));
	auto *header = static_cast< Header * >(m_memory.data());
	header->magic = Magic;
	header->slotBytes = slotBytes;
	header->slotCount = SlotCount;
	m_memory.unlock();
	return true;
}

bool FrameTransport::attach(const QString &key) {
	detach();
	if (key.trimmed().isEmpty()) return false;
	m_memory.setKey(key);
	if (!m_memory.attach(QSharedMemory::ReadWrite)) return false;
	if (!m_memory.lock()) {
		detach();
		return false;
	}
	const auto *header = static_cast< const Header * >(m_memory.constData());
	const bool valid = header && header->magic == Magic && header->slotCount == SlotCount && header->slotBytes > 0
		&& m_memory.size() >= static_cast< qsizetype >(sizeof(Header))
			+ static_cast< qsizetype >(header->slotBytes) * header->slotCount;
	m_memory.unlock();
	if (!valid) detach();
	return valid;
}

void FrameTransport::detach() {
	if (m_memory.isAttached()) m_memory.detach();
	m_lastSequence = 0;
	m_generation = 0;
}

bool FrameTransport::publish(const NativeFrame &frame) {
	if (!m_memory.isAttached() || frame.bgra.isEmpty())
		return false;
	if (!m_memory.lock()) return false;
	auto *header = static_cast< Header * >(m_memory.data());
	const bool validHeader = header && header->magic == Magic && header->slotCount == SlotCount
		&& header->slotBytes > 0 && m_memory.size() >= static_cast< qsizetype >(sizeof(Header))
			+ static_cast< qsizetype >(header->slotBytes) * header->slotCount;
	const bool validFrame = frame.bgra.size() <= std::numeric_limits< quint32 >::max()
		&& validDimensions(frame.width, frame.height, frame.stride, static_cast< quint32 >(frame.bgra.size()),
						   validHeader ? header->slotBytes : 0);
	if (!validHeader || !validFrame) {
		++m_droppedFrames;
		m_memory.unlock();
		return false;
	}
	const quint64 sequence = frame.generation != header->generation ? qMax< quint64 >(frame.sequence, 1)
																			 : qMax(frame.sequence, header->sequence + 1);
	const quint32 slot = static_cast< quint32 >(sequence % header->slotCount);
	auto *destination = reinterpret_cast< char * >(header + 1) + static_cast< qsizetype >(slot) * header->slotBytes;
	std::memcpy(destination, frame.bgra.constData(), static_cast< size_t >(frame.bgra.size()));
	header->latestSlot = slot;
	header->generation = frame.generation;
	header->timestampUsec = frame.timestampUsec;
	header->width = frame.width;
	header->height = frame.height;
	header->stride = frame.stride;
	header->payloadBytes = static_cast< quint32 >(frame.bgra.size());
	header->sequence = sequence;
	m_memory.unlock();
	return true;
}

bool FrameTransport::readLatest(NativeFrame *frame) {
	if (!frame || !m_memory.isAttached() || !m_memory.lock()) return false;
	const auto *header = static_cast< const Header * >(m_memory.constData());
	const bool validHeader = header && header->magic == Magic && header->slotCount == SlotCount
		&& header->latestSlot < header->slotCount && header->slotBytes > 0
		&& m_memory.size() >= static_cast< qsizetype >(sizeof(Header))
			+ static_cast< qsizetype >(header->slotBytes) * header->slotCount;
	if (!validHeader || header->sequence == 0
		|| (header->generation == m_generation && header->sequence == m_lastSequence)
		|| !validDimensions(header->width, header->height, header->stride, header->payloadBytes, header->slotBytes)) {
		m_memory.unlock();
		return false;
	}
	const auto *source = reinterpret_cast< const char * >(header + 1)
		+ static_cast< qsizetype >(header->latestSlot) * header->slotBytes;
	frame->generation = header->generation;
	frame->sequence = header->sequence;
	frame->timestampUsec = header->timestampUsec;
	frame->width = header->width;
	frame->height = header->height;
	frame->stride = header->stride;
	frame->bgra = QByteArray(source, static_cast< qsizetype >(header->payloadBytes));
	if (m_generation != 0 && header->generation != m_generation) m_lastSequence = 0;
	if (m_lastSequence != 0 && header->sequence > m_lastSequence + 1)
		m_droppedFrames += header->sequence - m_lastSequence - 1;
	m_generation = header->generation;
	m_lastSequence = header->sequence;
	m_memory.unlock();
	return true;
}

QString FrameTransport::key() const { return m_memory.key(); }
quint64 FrameTransport::droppedFrames() const { return m_droppedFrames; }

RawBgraFrameAssembler::RawBgraFrameAssembler(const qsizetype frameBytes, const int maximumBufferedFrames)
	: m_frameBytes(frameBytes), m_maximumBufferedFrames(qMax(1, maximumBufferedFrames)) {
}

QList< QByteArray > RawBgraFrameAssembler::push(const QByteArray &bytes) {
	QList< QByteArray > frames;
	if (m_frameBytes <= 0 || bytes.isEmpty()) return frames;
	if (bytes.size() > std::numeric_limits< qsizetype >::max() - m_buffer.size()) {
		m_buffer.clear();
		++m_droppedFrames;
		return frames;
	}
	const qsizetype logicalBytes = m_buffer.size() + bytes.size();
	const qsizetype completeFrames = logicalBytes / m_frameBytes;
	qsizetype inputOffset = 0;
	if (completeFrames > m_maximumBufferedFrames) {
		const qsizetype dropCount = completeFrames - m_maximumBufferedFrames;
		qsizetype bytesToDrop = dropCount * m_frameBytes;
		const qsizetype bufferedDrop = qMin(bytesToDrop, m_buffer.size());
		m_buffer.remove(0, bufferedDrop);
		bytesToDrop -= bufferedDrop;
		inputOffset = qMin(bytesToDrop, bytes.size());
		m_droppedFrames += static_cast< quint64 >(dropCount);
	}
	if (inputOffset < bytes.size()) m_buffer.append(bytes.constData() + inputOffset, bytes.size() - inputOffset);
	while (m_buffer.size() >= m_frameBytes) {
		frames.push_back(m_buffer.left(m_frameBytes));
		m_buffer.remove(0, m_frameBytes);
	}
	return frames;
}

quint64 RawBgraFrameAssembler::droppedFrames() const { return m_droppedFrames; }
qsizetype RawBgraFrameAssembler::bufferedBytes() const { return m_buffer.size(); }

QStringList ffmpegRawBgraOutputArguments(const quint32 width, const quint32 height) {
	if (width == 0 || height == 0) return {};
	return { QStringLiteral("-vf"),
			 QStringLiteral("scale=%1:%2:flags=fast_bilinear,format=bgra").arg(width).arg(height),
			 QStringLiteral("-pix_fmt"), QStringLiteral("bgra"), QStringLiteral("-f"), QStringLiteral("rawvideo"),
			 QStringLiteral("pipe:1") };
}

QStringList gstreamerRawBgraSinkArguments(const quint32 width, const quint32 height, const bool inputAlreadyDecoded) {
	if (width == 0 || height == 0) return {};
	QStringList arguments;
	if (!inputAlreadyDecoded) arguments << QStringLiteral("decodebin") << QStringLiteral("!");
	arguments << QStringLiteral("videoconvert") << QStringLiteral("!") << QStringLiteral("videoscale")
			  << QStringLiteral("!")
			  << QStringLiteral("video/x-raw,format=BGRA,width=%1,height=%2").arg(width).arg(height)
			  << QStringLiteral("!") << QStringLiteral("fdsink") << QStringLiteral("fd=1")
			  << QStringLiteral("sync=false");
	return arguments;
}

} // namespace Mumble::ScreenShare
