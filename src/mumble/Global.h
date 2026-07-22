// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_GLOBAL_H_
#define MUMBLE_MUMBLE_GLOBAL_H_

#include <QtCore/QByteArray>
#include <QtCore/QDir>
#include <QtCore/QList>
#include <QtCore/QReadWriteLock>

#include "ACL.h"
#include "ChannelListenerManager.h"
#include "Settings.h"
#include "Timer.h"
#include "Version.h"

#include <memory>

// Global helper class to spread variables around across threads.

class MainWindow;
class ServerHandler;
class AudioInput;
class AudioOutput;
class Database;
class Log;
class PluginManager;
class QSettings;
class Zeroconf;
class TrayIcon;

class QNetworkAccessManager;

namespace Mumble::InputEnhancement {
class InputEnhancementPackageVerifier;
class InputEnhancementPolicyController;
} // namespace Mumble::InputEnhancement

struct MigratedPath {
	QString oldPath;
	QString newPath;
};

struct Global Q_DECL_FINAL {
private:
	Q_DISABLE_COPY(Global)
public:
	static Global *g_global_struct;
	static Global &get();

	MainWindow *mw;
	TrayIcon *trayIcon;
	Settings s;
	// The MainWindow owns publication of the active handler. Code that can run
	// outside the main event-loop thread must obtain a retained snapshot through
	// serverHandlerSnapshot() rather than copying this object directly. All
	// publication goes through replace/take so those readers are synchronized.
	std::shared_ptr< ServerHandler > sh;
	std::shared_ptr< AudioInput > ai;
	std::shared_ptr< AudioOutput > ao;
	/**
	 * @remark Must only be accessed from the main event loop
	 */
	Database *db;
	Log *l;
	/// A pointer to the PluginManager that is used in this session
	PluginManager *pluginManager;
	Zeroconf *zeroconf;
	QNetworkAccessManager *nam;
	Mumble::InputEnhancement::InputEnhancementPackageVerifier *inputEnhancementPackageVerifier;
	Mumble::InputEnhancement::InputEnhancementPolicyController *inputEnhancementPolicyController;
	int iPushToTalk;
	Timer tDoublePush;
	quint64 uiDoublePush;
	/// Holds the current VoiceTarget ID to send audio to
	std::int32_t iTarget;
	/// Holds the value of iTarget before its last change until the current
	/// audio-stream ends (and it has a value > 0). See the comment in
	/// AudioInput::flushCheck for further details on this.
	std::int32_t iPrevTarget;
	bool bPushToMute;
	bool bCenterPosition;
	bool bPosTest;
	bool bInAudioWizard;
	bool inConfigUI;
	int iAudioPathTime;
	/// A unique ID for the current user. It is being assigned by the server right
	/// after connecting to it. An ID of 0 indicates that the user currently isn't
	/// connected to a server.
	unsigned int uiSession;
	ChanACL::Permissions pPermissions;
	int iMaxBandwidth;
	int iAudioBandwidth;
	QDir qdBasePath;
	bool bAttenuateOthers;
	/// If set the AudioOutput::mix will forcefully adjust the volume of all
	/// non-priority speakers.
	bool prioritySpeakerActiveOverride;
	bool bAllowHTML;
	bool bPersistentGlobalChatEnabled;
	QList< int > qlSupportedChatFeatures;
	QList< int > qlSupportedForkFeatures;
	unsigned int uiPersistentChatProtocolVersion;
	unsigned int uiForkExtensionProtocolVersion;
	bool bScreenShareEnabled;
	bool bScreenShareRecordingEnabled;
	bool bScreenShareHelperRequired;
	QList< int > qlPreferredScreenShareCodecs;
	unsigned int uiScreenShareMaxWidth;
	unsigned int uiScreenShareMaxHeight;
	unsigned int uiScreenShareMaxFps;
	QString qsScreenShareRelayUrl;
	bool bStonksEnabled;
	unsigned int uiStonksTextChannelID;
	bool bStonksSocialAnnouncementsEnabled;
	bool bStonksAutoValuationEnabled;
	unsigned int uiStonksValuationIntervalMinutes;
	unsigned int uiStonksValuationHistoryDays;
	bool bFeedbackEnabled;
	unsigned int uiFeedbackMaxLogBytes;
	unsigned int uiFeedbackMaxBodyBytes;
	QString qsServerDisplayName;
	QString qsServerMonogram;
	QByteArray qbaServerImage;
	quint64 uiChatAssetMaxBytes;
	unsigned int uiChatAttachmentLimit;
	unsigned int uiMessageLength;
	unsigned int uiImageLength;
	unsigned int uiMaxUsers;
	bool recordingAllowed;
	bool bQuit;
	QString windowTitlePostfix;
	bool bDebugDumpInput;
	bool bDebugPrintQueue;
	bool bDisableInputEnhancement;
	bool bInputEnhancementRecoveryDisabled;
	std::unique_ptr< ChannelListenerManager > channelListenerManager;

	bool bHappyEaster;
	static const char ccHappyEaster[];

	QString migratedDBPath;
	MigratedPath migratedPluginDirPath;

	Global(const QString &qsConfigPath = QString());
	~Global() = default;

	std::shared_ptr< ServerHandler > serverHandlerSnapshot() const;
	void replaceServerHandler(std::shared_ptr< ServerHandler > handler);
	std::shared_ptr< ServerHandler > takeServerHandler();

private:
	mutable QReadWriteLock m_serverHandlerLock;
	void migrateDataDir(const QDir &toDir);
};

// Class to handle ordered initialization of globals.
// This allows the same link-time magic as used everywhere else
// for globals that need an init before the GUI starts, but
// after we reach main().

class DeferInit {
private:
	Q_DISABLE_COPY(DeferInit)
protected:
	static QMultiMap< int, DeferInit * > *qmDeferers;
	void add(int priority);

public:
	DeferInit(int priority) { add(priority); };
	DeferInit() { add(0); };
	virtual ~DeferInit();
	virtual void initialize(){};
	virtual void destroy(){};
	static void run_initializers();
	static void run_destroyers();
};

/// Special exit code which causes mumble to restart itself. The outward facing return code with be 0
const int MUMBLE_EXIT_CODE_RESTART = 64738;

#endif
