// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ClientUser.h"

#include "AudioOutput.h"
#include "Channel.h"
#include "PluginManager.h"
#include "Global.h"

QHash< unsigned int, ClientUser * > ClientUser::c_qmUsers;
QReadWriteLock ClientUser::c_qrwlUsers;

QList< ClientUser * > ClientUser::c_qlTalking;
QReadWriteLock ClientUser::c_qrwlTalking;

ClientUser::ClientUser(QObject *p)
	: QObject(p), tsState(Settings::Passive), bLocalIgnore(false), bLocalIgnoreTTS(false), bLocalMute(false),
	  volumeMute(false), fPowerMin(0.0f), fPowerMax(0.0f), fAverageAvailable(0.0f), iFrames(0), iSequence(0) {
}

float ClientUser::getLocalVolumeAdjustments() const {
	return m_localVolume;
}

QString ClientUser::getLocalNickname() const {
	return m_localNickname;
}

bool ClientUser::isRemoteSpeechCleanupEnabled() const {
	const std::int8_t override = m_remoteSpeechCleanupOverride.load(std::memory_order_relaxed);
	return override < 0 ? Global::get().s.remoteSpeechCleanupEnabled : override > 0;
}

std::optional< bool > ClientUser::getRemoteSpeechCleanupOverride() const {
	const std::int8_t override = m_remoteSpeechCleanupOverride.load(std::memory_order_relaxed);
	if (override < 0) {
		return std::nullopt;
	}
	return override > 0;
}

void ClientUser::setRemoteSpeechCleanupOverride(std::optional< bool > enabled) {
	m_remoteSpeechCleanupOverride.store(enabled ? (*enabled ? 1 : 0) : -1, std::memory_order_relaxed);
}

ClientUser *ClientUser::get(unsigned int uiSession) {
	QReadLocker lock(&c_qrwlUsers);
	ClientUser *p = c_qmUsers.value(uiSession);
	return p;
}

QList< ClientUser * > ClientUser::getTalking() {
	QReadLocker lock(&c_qrwlTalking);
	return c_qlTalking;
}

bool ClientUser::isValid(unsigned int uiSession) {
	QReadLocker lock(&c_qrwlUsers);

	return c_qmUsers.contains(uiSession);
}

ClientUser *ClientUser::add(unsigned int uiSession, QObject *po) {
	QWriteLocker lock(&c_qrwlUsers);

	ClientUser *p        = new ClientUser(po);
	p->uiSession         = uiSession;
	c_qmUsers[uiSession] = p;

	QObject::connect(p, &ClientUser::talkingStateChanged, Global::get().pluginManager,
					 &PluginManager::on_userTalkingStateChanged);

	return p;
}

ClientUser *ClientUser::match(const ClientUser *other, bool matchname) {
	QReadLocker lock(&c_qrwlUsers);

	for (ClientUser *p : c_qmUsers) {
		if (p == other)
			continue;
		if ((p->iId >= 0) && (p->iId == other->iId))
			return p;
		if (matchname && (p->qsName == other->qsName))
			return p;
	}
	return nullptr;
}

void ClientUser::remove(unsigned int uiSession) {
	ClientUser *p;
	{
		QWriteLocker lock(&c_qrwlUsers);
		p = c_qmUsers.take(uiSession);

		if (p) {
			if (p->cChannel)
				p->cChannel->removeUser(p);

			if (p->tsState != Settings::Passive) {
				QWriteLocker writeLock(&c_qrwlTalking);
				c_qlTalking.removeAll(p);
			}
		}
	}

	if (p) {
		AudioOutputPtr ao = Global::get().ao;
		if (ao) {
			// It is safe to call this function and to give the ClientUser pointer
			// to it even though we don't hold the lock anymore as it will only take
			// the pointer to use as the key in a HashMap lookup. At no point in the
			// code triggered by this function call will the ClientUser pointer be
			// dereferenced.
			// Furthermore ClientUser objects are deleted in UserModel::removeUser which
			// calls this very function before doing so. Thus the object shouldn't be
			// deleted before this function returns anyways.
			ao->removeUser(p);
		}
	}
}

void ClientUser::remove(ClientUser *p) {
	remove(p->uiSession);
}

QString ClientUser::getFlagsString() const {
	QStringList flags;

	if (!qsFriendName.isEmpty())
		flags << ClientUser::tr("Friend");
	if (iId >= 0)
		flags << ClientUser::tr("Authenticated");
	if (bPrioritySpeaker)
		flags << ClientUser::tr("Priority speaker");
	if (bRecording)
		flags << ClientUser::tr("Recording");
	if (bMute)
		flags << ClientUser::tr("Muted (server)");
	if (bDeaf)
		flags << ClientUser::tr("Deafened (server)");
	if (bLocalIgnore)
		flags << ClientUser::tr("Local Ignore (Text messages)");
	if (bLocalIgnoreTTS)
		flags << ClientUser::tr("Local Ignore (Text-To-Speech)");
	if (bLocalMute)
		flags << ClientUser::tr("Local Mute");
	if (bSelfMute)
		flags << ClientUser::tr("Muted (self)");
	if (bSelfDeaf)
		flags << ClientUser::tr("Deafened (self)");

	return flags.join(QLatin1String(", "));
}

void ClientUser::setTalking(Settings::TalkState ts) {
	if (tsState == ts)
		return;

	bool nstate = false;
	if (ts == Settings::Passive)
		nstate = true;
	else if (tsState == Settings::Passive)
		nstate = true;

	tsState = ts;
	emit talkingStateChanged();

	if (nstate && cChannel) {
		QWriteLocker lock(&c_qrwlTalking);
		if (ts == Settings::Passive)
			c_qlTalking.removeAll(this);
		else
			c_qlTalking << this;
	}
}

void ClientUser::setMute(bool mute) {
	if (bMute == mute)
		return;
	bMute = mute;
	if (!bMute)
		bDeaf = false;
	emit muteDeafStateChanged();
}

void ClientUser::setSuppress(bool suppress) {
	if (bSuppress == suppress)
		return;
	bSuppress = suppress;
	emit muteDeafStateChanged();
}

void ClientUser::setLocalIgnore(bool ignore) {
	if (bLocalIgnore == ignore)
		return;
	bLocalIgnore = ignore;
	emit muteDeafStateChanged();
}

void ClientUser::setLocalIgnoreTTS(bool ignoreTTS) {
	bLocalIgnoreTTS = ignoreTTS;
}

void ClientUser::setLocalMute(bool mute) {
	if (bLocalMute == mute)
		return;
	bLocalMute = mute;
	emit muteDeafStateChanged();
}

void ClientUser::setDeaf(bool deaf) {
	bDeaf = deaf;
	if (bDeaf)
		bMute = true;
	emit muteDeafStateChanged();
}

void ClientUser::setSelfMute(bool mute) {
	bSelfMute = mute;
	if (!mute)
		bSelfDeaf = false;
	emit muteDeafStateChanged();
}

void ClientUser::setSelfDeaf(bool deaf) {
	bSelfDeaf = deaf;
	if (deaf)
		bSelfMute = true;
	emit muteDeafStateChanged();
}

void ClientUser::setPrioritySpeaker(bool priority) {
	if (bPrioritySpeaker == priority)
		return;
	bPrioritySpeaker = priority;
	emit prioritySpeakerStateChanged();
}

void ClientUser::setRecording(bool recording) {
	if (bRecording == recording)
		return;
	bRecording = recording;
	emit recordingStateChanged();
}

void ClientUser::setLocalVolumeAdjustment(float adjustment) {
	float oldAdjustment = m_localVolume;
	m_localVolume       = adjustment;

	emit localVolumeAdjustmentsChanged(m_localVolume, oldAdjustment);
}

void ClientUser::setLocalNickname(const QString &nickname) {
	if (m_localNickname != nickname) {
		m_localNickname = nickname;

		emit localNicknameChanged();
	}
}

/* From Channel.h
 */
void Channel::addClientUser(ClientUser *p) {
	addUser(p);
	p->setParent(this);
}
