// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "MainWindow.h"

#include "WASAPINotificationClient.h"
#include "Global.h"

#include <QtCore/QMutexLocker>

#include <mutex>

static_assert(WASAPINotificationClient::defaultDeviceNotificationMatches(
	eCapture, eConsole, eCapture, eConsole));
static_assert(WASAPINotificationClient::defaultDeviceNotificationMatches(
	eCapture, eMultimedia, eCapture, eMultimedia));
static_assert(WASAPINotificationClient::defaultDeviceNotificationMatches(
	eCapture, eCommunications, eCapture, eCommunications));
static_assert(!WASAPINotificationClient::defaultDeviceNotificationMatches(
	eCapture, eConsole, eCapture, eCommunications));

HRESULT STDMETHODCALLTYPE WASAPINotificationClient::OnDefaultDeviceChanged(EDataFlow flow, ERole role,
																		   LPCWSTR pwstrDefaultDevice) {
	const QString device = QString::fromWCharArray(pwstrDefaultDevice);

	qDebug() << "WASAPINotificationClient: Default device changed flow=" << flow << "role=" << role << "device"
			 << device;

	QMutexLocker lock(&listsMutex);
	const AudioConsumers consumers = usedDefaultDevicesByFlowAndRole.value(defaultDeviceNotificationKey(flow, role));
	if (consumers != NoConsumer) {
		requestRecoveryLocked(consumers, QStringLiteral("default_device_changed"), 350);
	}
	return S_OK;
}

HRESULT STDMETHODCALLTYPE WASAPINotificationClient::OnPropertyValueChanged(LPCWSTR pwstrDeviceId,
																		   const PROPERTYKEY key) {
	const QString device = QString::fromWCharArray(pwstrDeviceId);

	const bool formatChanged        = (key == PKEY_AudioEngine_DeviceFormat);
	const bool channelConfigChanged = (key == PKEY_AudioEndpoint_PhysicalSpeakers);

	QMutexLocker lock(&listsMutex);
	const AudioConsumers consumers = usedDevices.value(device);
	if ((formatChanged || channelConfigChanged) && consumers != NoConsumer) {
		qDebug() << "WASAPINotificationClient: Property changed device=" << device << "formatChanged=" << formatChanged
				 << "channelConfigChanged=" << channelConfigChanged;

		requestRecoveryLocked(consumers, QStringLiteral("device_format_changed"), 350);
	}
	return S_OK;
}

HRESULT STDMETHODCALLTYPE WASAPINotificationClient::OnDeviceAdded(LPCWSTR pwstrDeviceId) {
	const QString device = QString::fromWCharArray(pwstrDeviceId);
	qDebug() << "WASAPINotificationClient: Device added=" << device;

	QMutexLocker lock(&listsMutex);
	AudioConsumers consumers = usedDevices.value(device);
	for (const AudioConsumers recoveryConsumers : consumersNeedingPreferredRecoveryByFlow) {
		consumers |= recoveryConsumers;
	}
	if (consumers != NoConsumer) {
		requestRecoveryLocked(consumers, QStringLiteral("device_added"), 750);
	}

	return S_OK;
}

HRESULT STDMETHODCALLTYPE WASAPINotificationClient::OnDeviceRemoved(LPCWSTR pwstrDeviceId) {
	const QString device = QString::fromWCharArray(pwstrDeviceId);
	qDebug() << "WASAPINotificationClient: Device removed=" << device;
	QMutexLocker lock(&listsMutex);
	const AudioConsumers consumers = usedDevices.value(device);
	if (consumers != NoConsumer) {
		requestRecoveryLocked(consumers, QStringLiteral("device_removed"), 750);
	}
	return S_OK;
}

HRESULT STDMETHODCALLTYPE WASAPINotificationClient::OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState) {
	const QString device = QString::fromWCharArray(pwstrDeviceId);

	qDebug() << "WASAPINotificationClient: Device state changed newState=" << dwNewState << "device=" << device;

	switch (dwNewState) {
		case DEVICE_STATE_ACTIVE:
			return OnDeviceAdded(pwstrDeviceId);
		case DEVICE_STATE_DISABLED:
		case DEVICE_STATE_NOTPRESENT:
		case DEVICE_STATE_UNPLUGGED:
			return OnDeviceRemoved(pwstrDeviceId);
	}

	return S_OK;
}

HRESULT STDMETHODCALLTYPE WASAPINotificationClient::QueryInterface(REFIID riid, VOID **ppvInterface) {
	if (IID_IUnknown == riid) {
		*ppvInterface = (IUnknown *) this;
		AddRef();
	} else if (__uuidof(IMMNotificationClient) == riid) {
		*ppvInterface = (IMMNotificationClient *) this;
		AddRef();
	} else {
		*ppvInterface = nullptr;
		return E_NOINTERFACE;
	}
	return S_OK;
}

ULONG STDMETHODCALLTYPE WASAPINotificationClient::AddRef() {
	return InterlockedIncrement(&_cRef);
}

ULONG STDMETHODCALLTYPE WASAPINotificationClient::Release() {
	// We hold a ref to ourselves all the time (static singleton) so no
	// need to clean ourselves up or anything.
	ULONG ulRef = InterlockedDecrement(&_cRef);
	Q_ASSERT(ulRef > 0);
	return ulRef;
}

void WASAPINotificationClient::enlistDefaultDeviceAsUsed(LPCWSTR pwstrDefaultDevice, const EDataFlow flow,
														 const ERole role, const AudioConsumers consumers) {
	const QString device = QString::fromWCharArray(pwstrDefaultDevice);
	QMutexLocker lock(&listsMutex);
	usedDefaultDevicesByFlowAndRole[defaultDeviceNotificationKey(flow, role)] |= consumers;
	_enlistDeviceAsUsed(device, consumers);
}

void WASAPINotificationClient::enlistDeviceAsUsed(LPCWSTR pwstrDevice, const AudioConsumers consumers) {
	const QString device = QString::fromWCharArray(pwstrDevice);
	QMutexLocker lock(&listsMutex);
	_enlistDeviceAsUsed(device, consumers);
}

void WASAPINotificationClient::_enlistDeviceAsUsed(const QString &device, const AudioConsumers consumers) {
	usedDevices[device] |= consumers;
}

void WASAPINotificationClient::enlistDeviceAsUsed(const QString &device, const AudioConsumers consumers) {
	QMutexLocker lock(&listsMutex);
	_enlistDeviceAsUsed(device, consumers);
}

void WASAPINotificationClient::setConsumersNeedingPreferredRecovery(const EDataFlow flow,
													 const AudioConsumers consumers, const bool needed) {
	QMutexLocker lock(&listsMutex);
	AudioConsumers &flowConsumers = consumersNeedingPreferredRecoveryByFlow[static_cast< int >(flow)];
	if (needed) {
		flowConsumers |= consumers;
	} else {
		flowConsumers &= ~consumers;
		if (flowConsumers == NoConsumer) {
			consumersNeedingPreferredRecoveryByFlow.remove(static_cast< int >(flow));
		}
	}
}

void WASAPINotificationClient::markConsumersHealthy(const AudioConsumers consumers) {
	QMutexLocker lock(&listsMutex);
	if (consumers.testFlag(InputConsumer)) {
		inputRecoveryFailures = 0;
	}
	if (consumers.testFlag(OutputConsumer)) {
		outputRecoveryFailures = 0;
	}
}

void WASAPINotificationClient::requestRecovery(const AudioConsumers consumers, const QString &reason,
														const int delayMs) {
	QMutexLocker lock(&listsMutex);
	requestRecoveryLocked(consumers, reason, delayMs);
}

void WASAPINotificationClient::unlistDevice(LPCWSTR pwstrDevice) {
	const QString device = QString::fromWCharArray(pwstrDevice);
	QMutexLocker lock(&listsMutex);
	usedDevices.remove(device);
}

void WASAPINotificationClient::clearUsedDefaultDeviceList() {
	QMutexLocker lock(&listsMutex);
	usedDefaultDevicesByFlowAndRole.clear();
}

void WASAPINotificationClient::_clearUsedDeviceLists() {
	usedDevices.clear();
	usedDefaultDevicesByFlowAndRole.clear();
}

void WASAPINotificationClient::_clearConsumers(const AudioConsumers consumers) {
	for (auto iterator = usedDevices.begin(); iterator != usedDevices.end();) {
		iterator.value() &= ~consumers;
		if (iterator.value() == NoConsumer) {
			iterator = usedDevices.erase(iterator);
		} else {
			++iterator;
		}
	}
	for (auto iterator = usedDefaultDevicesByFlowAndRole.begin();
		 iterator != usedDefaultDevicesByFlowAndRole.end();) {
		iterator.value() &= ~consumers;
		if (iterator.value() == NoConsumer) {
			iterator = usedDefaultDevicesByFlowAndRole.erase(iterator);
		} else {
			++iterator;
		}
	}
}

void WASAPINotificationClient::clearUsedDeviceLists() {
	QMutexLocker lock(&listsMutex);
	_clearUsedDeviceLists();
}

WASAPINotificationClient::AudioConsumers WASAPINotificationClient::beginAudioResetRebuild() {
	QMutexLocker lock(&listsMutex);
	const AudioConsumers consumers = queuedConsumers == NoConsumer ? AudioConsumers(AllConsumers) : queuedConsumers;
	queuedConsumers = NoConsumer;
	_clearConsumers(consumers);
	restartDispatchGate.beginRebuildAfterOldAudioStopped();
	return consumers;
}

void WASAPINotificationClient::finishAudioResetRebuild() {
	bool dispatchFollowup = false;
	int followupDelayMs   = 350;
	{
		QMutexLocker lock(&listsMutex);
		dispatchFollowup = restartDispatchGate.finishRebuildStartAndTakeFollowup();
		if (dispatchFollowup) {
			queuedConsumers   = followupConsumers == NoConsumer ? AudioConsumers(AllConsumers) : followupConsumers;
			followupConsumers = NoConsumer;
			queuedDelayMs     = 350;
			followupDelayMs   = queuedDelayMs;
		}
	}
	if (dispatchFollowup) {
		qWarning("WASAPINotificationClient: Triggering follow-up audio reset");
		emit doResetAudio(followupDelayMs);
	}
}

void WASAPINotificationClient::doGetOnce() {
	(void) WASAPINotificationClient::doGet();
}

WASAPINotificationClient &WASAPINotificationClient::doGet() {
	static WASAPINotificationClient instance;
	return instance;
}

static std::once_flag notification_client_init_once;

WASAPINotificationClient &WASAPINotificationClient::get() {
	// Hacky way of making sure we get a thread-safe yet lazy initialization of the static.
	std::call_once(notification_client_init_once, &WASAPINotificationClient::doGetOnce);
	return doGet();
}

WASAPINotificationClient::WASAPINotificationClient() : QObject(), pEnumerator(0), listsMutex() {
	AddRef(); // Static singleton, always has a self-reference

	HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
								  reinterpret_cast< void ** >(&pEnumerator));
	if (!pEnumerator || FAILED(hr)) {
		if (pEnumerator) {
			pEnumerator->Release();
			pEnumerator = 0;
		}
		qWarning() << "WASAPINotificationClient: Failed to create enumerator, will not receive notifications";
		return;
	}

	Global::get().mw->connect(this, SIGNAL(doResetAudio(int)), SLOT(scheduleWASAPIAudioReset(int)),
							 Qt::QueuedConnection);

	pEnumerator->RegisterEndpointNotificationCallback(this);
}

WASAPINotificationClient::~WASAPINotificationClient() {
	if (pEnumerator) {
		pEnumerator->UnregisterEndpointNotificationCallback(this);
		pEnumerator->Release();
	}
}

void WASAPINotificationClient::requestRecoveryLocked(const AudioConsumers consumers, const QString &reason,
															  const int delayMs) {
	if (consumers == NoConsumer) {
		return;
	}
	int effectiveDelay = qMax(0, delayMs);
	if (reason.contains(QLatin1String("stream_failed"))) {
		int exponent = 0;
		if (consumers.testFlag(InputConsumer)) {
			exponent = qMax(exponent, qMin(inputRecoveryFailures++, 5));
		}
		if (consumers.testFlag(OutputConsumer)) {
			exponent = qMax(exponent, qMin(outputRecoveryFailures++, 5));
		}
		effectiveDelay = qMin(30000, qMax(effectiveDelay, 750 * (1 << exponent)));
	}
	const bool wasIdle = !restartDispatchGate.restartPending();
	if (restartDispatchGate.rebuildInProgress()) {
		followupConsumers |= consumers;
	} else {
		queuedConsumers |= consumers;
		queuedDelayMs = wasIdle ? effectiveDelay : qMin(queuedDelayMs, effectiveDelay);
	}
	if (!restartDispatchGate.requestRestart()) {
		return;
	}
	qWarning() << "WASAPINotificationClient: Scheduling audio recovery consumers=" << int(consumers)
			   << "delayMs=" << effectiveDelay << "reason=" << reason;
	emit doResetAudio(effectiveDelay);
}
