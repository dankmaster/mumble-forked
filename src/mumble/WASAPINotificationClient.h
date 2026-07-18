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

/// Coalesces endpoint notifications until the queued audio reset has actually
/// rebuilt and re-enlisted a device. Unlike a wall-clock debounce, a second
/// default-device change immediately after that lifecycle boundary is retained.
class WASAPIRestartDispatchGate final {
public:
	bool requestRestart() noexcept {
		if (m_restartPending) {
			return false;
		}
		m_restartPending          = true;
		m_acceptingRebuildEnlists = false;
		m_rebuildStartReturned    = false;
		m_rebuildDeviceEnlisted   = false;
		return true;
	}
	void beginRebuildAfterOldAudioStopped() noexcept {
		if (!m_restartPending) {
			return;
		}
		m_acceptingRebuildEnlists = true;
		m_rebuildStartReturned    = false;
		m_rebuildDeviceEnlisted   = false;
	}
	void finishRebuildStart() noexcept {
		if (!m_restartPending || !m_acceptingRebuildEnlists) {
			return;
		}
		m_rebuildStartReturned = true;
		rearmIfRebuilt();
	}
	void rebuiltDeviceEnlisted() noexcept {
		if (!m_restartPending || !m_acceptingRebuildEnlists) {
			return;
		}
		m_rebuildDeviceEnlisted = true;
		rearmIfRebuilt();
	}
	bool restartPending() const noexcept { return m_restartPending; }

private:
	void rearmIfRebuilt() noexcept {
		if (m_rebuildStartReturned && m_rebuildDeviceEnlisted) {
			m_restartPending          = false;
			m_acceptingRebuildEnlists = false;
		}
	}

	bool m_restartPending          = false;
	bool m_acceptingRebuildEnlists = false;
	bool m_rebuildStartReturned    = false;
	bool m_rebuildDeviceEnlisted   = false;
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
	/// joined the old backends. Only enlistments from that rebuild generation
	/// may rearm notification dispatch.
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
