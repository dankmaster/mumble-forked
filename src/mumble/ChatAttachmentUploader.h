// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_CHATATTACHMENTUPLOADER_H_
#define MUMBLE_MUMBLE_CHATATTACHMENTUPLOADER_H_

#include "ChatAttachment.h"
#include "Mumble.pb.h"

#include <QtCore/QFile>
#include <QtCore/QObject>
#include <QtCore/QTimer>

#include <functional>

class ChatAttachmentUploader final : public QObject {
	Q_OBJECT
	Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
	struct Transport {
		std::function< void(const MumbleProto::ChatAssetUploadInit &) > sendInit;
		std::function< void(const MumbleProto::ChatAssetUploadChunk &) > sendChunk;
		std::function< void(const MumbleProto::ChatAssetUploadCommit &) > sendCommit;
	};

	explicit ChatAttachmentUploader(Transport transport, QObject *parent = nullptr);
	bool busy() const;
	bool start(const QList< Mumble::ChatAttachments::Source > &sources);
	void cancel(const QString &reason = {});
	void handleState(const MumbleProto::ChatAssetState &state);

signals:
	void busyChanged();
	void progressChanged(const QString &draftID, qreal progress);
	void completed(const QList< unsigned int > &assetIDs, const QStringList &fileNames);
	void failed(const QString &reason);

private:
	enum class Phase { Idle, AwaitingAcceptance, Uploading, AwaitingCommit };
	static constexpr qint64 ChunkBytes = 256 * 1024;
	static constexpr int ResponseTimeoutMs = 60000;

	void startNext();
	void pumpChunk();
	void fail(const QString &reason);
	void reset();
	void restartTimeout();
	MumbleProto::ChatAssetKind protoKind(Mumble::ChatAttachments::Kind kind) const;

	Transport m_transport;
	QList< Mumble::ChatAttachments::Source > m_sources;
	QList< unsigned int > m_assetIDs;
	QStringList m_fileNames;
	QFile m_file;
	QTimer m_timeout;
	Phase m_phase = Phase::Idle;
	int m_sourceIndex = -1;
	quint64 m_uploadID = 0;
};

#endif // MUMBLE_MUMBLE_CHATATTACHMENTUPLOADER_H_
