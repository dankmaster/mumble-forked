// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_CLIENTUSER_H_
#define MUMBLE_MUMBLE_CLIENTUSER_H_

#include <QtCore/QHash>
#include <QtCore/QReadWriteLock>

#include "Settings.h"
#include "User.h"

#include <atomic>
#include <cstdint>
#include <optional>

class ClientUser : public QObject, public User {
private:
	Q_OBJECT
	Q_DISABLE_COPY(ClientUser)

protected:
	float m_localVolume = 1.0f;
	QString m_localNickname;
	// -1 inherits the global setting, 0 forces disabled and 1 forces enabled.
	// This is read on the audio thread and written from the UI/network thread.
	std::atomic< std::int8_t > m_remoteSpeechCleanupOverride { -1 };

public:
	Settings::TalkState tsState;
	bool bLocalIgnore;
	bool bLocalIgnoreTTS;
	bool bLocalMute;
	// Whether or not the user's effective output is inaudible due to volume settings, positional audio etc.
	bool volumeMute;

	float fPowerMin, fPowerMax;
	float fAverageAvailable;

	int iFrames;
	int iSequence;

	QByteArray qbaTextureFormat;
	QString qsFriendName;

	QString getFlagsString() const;
	ClientUser(QObject *p = nullptr);

	float getLocalVolumeAdjustments() const;

	QString getLocalNickname() const;
	bool isRemoteSpeechCleanupEnabled() const;
	std::optional< bool > getRemoteSpeechCleanupOverride() const;
	void setRemoteSpeechCleanupOverride(std::optional< bool > enabled);

	static QHash< unsigned int, ClientUser * > c_qmUsers;
	static QReadWriteLock c_qrwlUsers;

	static QList< ClientUser * > c_qlTalking;
	static QReadWriteLock c_qrwlTalking;
	static QList< ClientUser * > getTalking();

	static ClientUser *get(unsigned int);
	static bool isValid(unsigned int);
	static ClientUser *add(unsigned int, QObject *p = nullptr);
	static ClientUser *match(const ClientUser *p, bool matchname = false);
	static void remove(unsigned int);
	static void remove(ClientUser *);

public slots:
	void setTalking(Settings::TalkState ts);
	void setMute(bool mute);
	void setDeaf(bool deaf);
	void setSuppress(bool suppress);
	void setLocalIgnore(bool ignore);
	void setLocalIgnoreTTS(bool ignoreTTS);
	void setLocalMute(bool mute);
	void setSelfMute(bool mute);
	void setSelfDeaf(bool deaf);
	void setPrioritySpeaker(bool priority);
	void setRecording(bool recording);
	void setLocalVolumeAdjustment(float adjustment);
	void setLocalNickname(const QString &nickname);
signals:
	void talkingStateChanged();
	void muteDeafStateChanged();
	void prioritySpeakerStateChanged();
	void recordingStateChanged();
	void localVolumeAdjustmentsChanged(float newAdjustment, float oldAdjustment);
	void localNicknameChanged();
};

#endif
