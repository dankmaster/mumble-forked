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

#include <chrono>

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
	bool hasRestartTimestamp = false;
	std::chrono::steady_clock::time_point lastRestartTimestamp;
	static constexpr auto restartDebounceWindow = std::chrono::milliseconds(500);
	IMMDeviceEnumerator *pEnumerator;
	LONG _cRef;
	QMutex listsMutex;

signals:
	void doResetAudio();
};

#endif // WASAPINOTIFICATIONCLIENT_H_
