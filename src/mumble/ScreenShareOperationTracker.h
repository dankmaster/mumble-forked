// Copyright The Mumble Developers. All rights reserved.
#ifndef MUMBLE_MUMBLE_SCREENSHAREOPERATIONTRACKER_H_
#define MUMBLE_MUMBLE_SCREENSHAREOPERATIONTRACKER_H_

#include <QtCore/QHash>
#include <QtCore/QSet>
#include <QtCore/QString>

class ScreenShareOperationTracker final {
public:
	quint64 begin(const QString &streamID) {
		const quint64 generation = ++m_nextGeneration;
		m_generations.insert(streamID, generation);
		return generation;
	}
	bool isCurrent(const QString &streamID, const quint64 generation) const {
		return generation != 0 && m_generations.value(streamID) == generation;
	}
	quint64 invalidate(const QString &streamID) { return begin(streamID); }
	QSet< QString > streamIDs() const { return QSet< QString >(m_generations.keyBegin(), m_generations.keyEnd()); }
private:
	QHash< QString, quint64 > m_generations;
	quint64 m_nextGeneration = 0;
};

#endif
