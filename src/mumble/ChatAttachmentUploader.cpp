// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ChatAttachmentUploader.h"

#include <QtCore/QFileInfo>
#include <QtCore/QTimer>

#include <limits>
#include <utility>

ChatAttachmentUploader::ChatAttachmentUploader(Transport transport, QObject *parent)
	: QObject(parent), m_transport(std::move(transport)) {
	m_timeout.setSingleShot(true);
	connect(&m_timeout, &QTimer::timeout, this, [this]() { fail(tr("The attachment server did not respond in time.")); });
}

bool ChatAttachmentUploader::busy() const {
	return m_phase != Phase::Idle;
}

bool ChatAttachmentUploader::start(const QList< Mumble::ChatAttachments::Source > &sources) {
	if (busy() || sources.isEmpty() || !m_transport.sendInit || !m_transport.sendChunk || !m_transport.sendCommit) {
		return false;
	}

	m_sources     = sources;
	m_sourceIndex = -1;
	m_assetIDs.clear();
	m_fileNames.clear();
	m_phase = Phase::AwaitingAcceptance;
	emit busyChanged();
	startNext();
	return true;
}

void ChatAttachmentUploader::cancel(const QString &reason) {
	if (!busy()) {
		return;
	}
	if (reason.isEmpty()) {
		reset();
	} else {
		fail(reason);
	}
}

void ChatAttachmentUploader::handleState(const MumbleProto::ChatAssetState &state) {
	if (!busy()) {
		return;
	}

	const MumbleProto::ChatAssetTransferState transferState =
		state.has_state() ? state.state() : MumbleProto::ChatAssetTransferStateUnknown;
	if (transferState == MumbleProto::ChatAssetTransferStateRejected) {
		if (m_phase == Phase::AwaitingAcceptance || !state.has_upload_id() || state.upload_id() == 0
			|| state.upload_id() == m_uploadID) {
			fail(state.has_reason() && !QString::fromStdString(state.reason()).trimmed().isEmpty()
					 ? QString::fromStdString(state.reason())
					 : tr("The server rejected the attachment."));
		}
		return;
	}

	if (m_phase == Phase::AwaitingAcceptance) {
		if (transferState != MumbleProto::ChatAssetTransferStateAccepted || !state.has_upload_id()
			|| state.upload_id() == 0) {
			return;
		}

		const Mumble::ChatAttachments::Source &source = m_sources.at(m_sourceIndex);
		if (state.has_accepted_byte_size() && state.accepted_byte_size() != source.byteSize) {
			fail(tr("The server accepted an unexpected attachment size."));
			return;
		}
		m_uploadID = state.upload_id();
		m_file.setFileName(source.path);
		if (!m_file.open(QIODevice::ReadOnly) || static_cast< quint64 >(m_file.size()) != source.byteSize) {
			fail(tr("The attachment changed or became unreadable before upload."));
			return;
		}
		m_phase = Phase::Uploading;
		restartTimeout();
		QTimer::singleShot(0, this, &ChatAttachmentUploader::pumpChunk);
		return;
	}

	if (m_phase != Phase::AwaitingCommit || !state.has_upload_id() || state.upload_id() != m_uploadID
		|| transferState != MumbleProto::ChatAssetTransferStateComplete) {
		return;
	}
	if (!state.has_asset() || !state.asset().has_asset_id() || state.asset().asset_id() == 0) {
		fail(tr("The server completed an attachment without returning its identifier."));
		return;
	}

	m_timeout.stop();
	m_assetIDs.push_back(state.asset().asset_id());
	m_fileNames.push_back(m_sources.at(m_sourceIndex).fileName);
	emit progressChanged(m_sources.at(m_sourceIndex).draftID, 1.0);
	startNext();
}

void ChatAttachmentUploader::startNext() {
	m_file.close();
	m_uploadID = 0;
	++m_sourceIndex;
	if (m_sourceIndex >= m_sources.size()) {
		const QList< unsigned int > assetIDs = m_assetIDs;
		const QStringList fileNames          = m_fileNames;
		reset();
		emit completed(assetIDs, fileNames);
		return;
	}

	const Mumble::ChatAttachments::Source &source = m_sources.at(m_sourceIndex);
	const QFileInfo info(source.path);
	if (!info.exists() || !info.isFile() || info.size() <= 0 || static_cast< quint64 >(info.size()) != source.byteSize
		|| source.mime.isEmpty() || source.sha256.size() != 64) {
		fail(tr("%1 is no longer a valid attachment.").arg(source.fileName));
		return;
	}

	MumbleProto::ChatAssetUploadInit init;
	init.set_filename(source.fileName.toStdString());
	init.set_mime(source.mime.toStdString());
	init.set_byte_size(source.byteSize);
	init.set_kind(protoKind(source.kind));
	init.set_request_inline(source.kind == Mumble::ChatAttachments::Kind::Image);
	init.set_sha256(source.sha256.toStdString());
	m_phase = Phase::AwaitingAcceptance;
	emit progressChanged(source.draftID, 0.0);
	m_transport.sendInit(init);
	restartTimeout();
}

void ChatAttachmentUploader::pumpChunk() {
	if (m_phase != Phase::Uploading || m_sourceIndex < 0 || m_sourceIndex >= m_sources.size()) {
		return;
	}

	const Mumble::ChatAttachments::Source &source = m_sources.at(m_sourceIndex);
	const quint64 offset = static_cast< quint64 >(m_file.pos());
	const QByteArray bytes = m_file.read(ChunkBytes);
	if (bytes.isEmpty()) {
		fail(tr("Could not read %1 during upload.").arg(source.fileName));
		return;
	}

	const quint64 nextOffset = offset + static_cast< quint64 >(bytes.size());
	const bool finalChunk    = nextOffset == source.byteSize;
	if (nextOffset > source.byteSize || (!finalChunk && m_file.atEnd())) {
		fail(tr("%1 changed while it was being uploaded.").arg(source.fileName));
		return;
	}

	MumbleProto::ChatAssetUploadChunk chunk;
	chunk.set_upload_id(m_uploadID);
	chunk.set_offset(offset);
	chunk.set_data(bytes.constData(), bytes.size());
	chunk.set_final_chunk(finalChunk);
	m_transport.sendChunk(chunk);
	emit progressChanged(source.draftID,
						 0.05 + (static_cast< qreal >(nextOffset) / static_cast< qreal >(source.byteSize)) * 0.9);
	restartTimeout();

	if (!finalChunk) {
		QTimer::singleShot(0, this, &ChatAttachmentUploader::pumpChunk);
		return;
	}

	m_file.close();
	MumbleProto::ChatAssetUploadCommit commit;
	commit.set_upload_id(m_uploadID);
	commit.set_filename(source.fileName.toStdString());
	commit.set_sha256(source.sha256.toStdString());
	m_phase = Phase::AwaitingCommit;
	m_transport.sendCommit(commit);
	restartTimeout();
}

void ChatAttachmentUploader::fail(const QString &reason) {
	const QString failureReason = reason.trimmed().isEmpty() ? tr("Attachment upload failed.") : reason.trimmed();
	reset();
	emit failed(failureReason);
}

void ChatAttachmentUploader::reset() {
	const bool wasBusy = busy();
	m_timeout.stop();
	m_file.close();
	m_sources.clear();
	m_assetIDs.clear();
	m_fileNames.clear();
	m_sourceIndex = -1;
	m_uploadID    = 0;
	m_phase       = Phase::Idle;
	if (wasBusy) {
		emit busyChanged();
	}
}

void ChatAttachmentUploader::restartTimeout() {
	m_timeout.start(ResponseTimeoutMs);
}

MumbleProto::ChatAssetKind ChatAttachmentUploader::protoKind(const Mumble::ChatAttachments::Kind kind) const {
	switch (kind) {
		case Mumble::ChatAttachments::Kind::Image: return MumbleProto::ChatAssetKindImage;
		case Mumble::ChatAttachments::Kind::Video: return MumbleProto::ChatAssetKindVideo;
		case Mumble::ChatAttachments::Kind::Document: return MumbleProto::ChatAssetKindDocument;
		case Mumble::ChatAttachments::Kind::Binary: return MumbleProto::ChatAssetKindBinary;
		case Mumble::ChatAttachments::Kind::Audio: return MumbleProto::ChatAssetKindAudio;
		case Mumble::ChatAttachments::Kind::Unknown:
		default: return MumbleProto::ChatAssetKindUnknown;
	}
}
