// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_WASAPINOTIFICATIONCLIENT_H_
#define MUMBLE_MUMBLE_WASAPINOTIFICATIONCLIENT_H_

#include <QtCore/QHash>
#include <QtCore/QMutex>
#include <QtCore/QObject>
#include <mmdeviceapi.h>

/// Coalesces endpoint notifications against the actual queued/rebuilding audio
/// lifecycle. Notifications received while a reset is merely queued are covered
/// by that reset. A notification received after the rebuild starts is retained as
/// exactly one follow-up reset, so the newest OS default cannot be lost.
class WASAPIRestartDispatchGate final {
public:
	bool requestRestart() noexcept {
		switch (m_phase) {
			case Phase::Idle:
				m_phase = Phase::Queued;
				return true;
			case Phase::Queued:
				return false;
			case Phase::Rebuilding:
				m_followupRequested = true;
				return false;
		}
		return false;
	}
	void beginRebuildAfterOldAudioStopped() noexcept {
		if (m_phase == Phase::Queued) {
			m_phase = Phase::Rebuilding;
		}
	}
	bool finishRebuildStartAndTakeFollowup() noexcept {
		if (m_phase != Phase::Rebuilding) {
			return false;
		}
		if (m_followupRequested) {
			m_followupRequested = false;
			m_phase             = Phase::Queued;
			return true;
		}
		m_phase = Phase::Idle;
		return false;
	}
	bool restartPending() const noexcept { return m_phase != Phase::Idle; }
	bool rebuildInProgress() const noexcept { return m_phase == Phase::Rebuilding; }

private:
	enum class Phase { Idle, Queued, Rebuilding };
	Phase m_phase = Phase::Idle;
	bool m_followupRequested = false;
};

/**
 * @brief Singleton for acting on WASAPINotification events for given devices.
 */
class WASAPINotificationClient : public QObject, public IMMNotificationClient {
	Q_OBJECT
public:
	/* IMMNotificationClient interface */
	HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDefaultDevice);
	HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR pwstrDeviceId, const PROPERTYKEY key);
	HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR pwstrDeviceId);
	HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR pwstrDeviceId);
	HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState);
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, VOID **ppvInterface);
	ULONG STDMETHODCALLTYPE AddRef();
	ULONG STDMETHODCALLTYPE Release();

	/* Enlist/Unlist functionality */
	void enlistDefaultDeviceAsUsed(LPCWSTR pwstrDefaultDevice, EDataFlow flow, ERole role);
	static constexpr bool defaultDeviceNotificationMatches(EDataFlow trackedFlow, ERole trackedRole,
														 EDataFlow changedFlow, ERole changedRole) noexcept {
		return trackedFlow == changedFlow && trackedRole == changedRole;
	}

	void enlistDeviceAsUsed(LPCWSTR pwstrDevice);
	void enlistDeviceAsUsed(const QString &device);

	void unlistDevice(LPCWSTR pwstrDevice);

	void clearUsedDefaultDeviceList();
	void clearUsedDeviceLists();
	/// MainWindow brackets Audio::start with these calls after Audio::stop has
	/// joined the old backends. A default-device event during that generation is
	/// dispatched once the current Audio::start has returned.
	void beginAudioResetRebuild();
	void finishAudioResetRebuild();

	/**
	 * @return Singleton instance reference.
	 */
	static WASAPINotificationClient &get();

private:
	WASAPINotificationClient();
	~WASAPINotificationClient() Q_DECL_OVERRIDE;

	WASAPINotificationClient(const WASAPINotificationClient &);
	WASAPINotificationClient &operator=(const WASAPINotificationClient &);

	static WASAPINotificationClient &doGet();
	static void doGetOnce();

	void restartAudioLocked();
	static constexpr quint32 defaultDeviceNotificationKey(EDataFlow flow, ERole role) noexcept {
		return (static_cast< quint32 >(flow) << 16U) | static_cast< quint32 >(role);
	}

	/* _fu = Non locking versions */
	void _clearUsedDeviceLists();
	void _enlistDeviceAsUsed(const QString &device);

	QStringList usedDefaultDevices;
	QStringList usedDevices;
	QHash< quint32, QString > usedDefaultDevicesByFlowAndRole;
	WASAPIRestartDispatchGate restartDispatchGate;
	IMMDeviceEnumerator *pEnumerator;
	LONG _cRef;
	QMutex listsMutex;

signals:
	void doResetAudio();
};

#endif // WASAPINOTIFICATIONCLIENT_H_
