// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_WASAPI_H_
#define MUMBLE_MUMBLE_WASAPI_H_

#include "AudioInput.h"
#include "AudioOutput.h"
#include "WASAPIDeviceRouting.h"

#include "win.h"

#include <QtCore/QObject>
#include <QtCore/QUuid>

#include <audioclient.h>
#include <avrt.h>
#include <functiondiscoverykeys.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <strsafe.h>
#ifdef _INC_FUNCTIONDISCOVERYKEYS
#	undef _INC_FUNCTIONDISCOVERYKEYS
#endif
#include <audiopolicy.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propidl.h>

class WASAPISystem : public QObject {
private:
	Q_OBJECT
	Q_DISABLE_COPY(WASAPISystem)
public:
	static const QHash< QString, QString > getDevices(EDataFlow dataflow);
	static const QHash< QString, QString > getInputDevices();
	static const QHash< QString, QString > getOutputDevices();
	static QList< Mumble::WASAPI::DeviceDescriptor > getDeviceDescriptors(EDataFlow dataflow,
															 DWORD stateMask = DEVICE_STATE_ACTIVE);
	static Mumble::WASAPI::DeviceDescriptor descriptorForEndpoint(const QString &endpointId, EDataFlow dataflow);
	static Mumble::WASAPI::DeviceDescriptor defaultDeviceDescriptor(EDataFlow dataflow, ERole role);
	static Mumble::WASAPI::RuntimeState runtimeState(EDataFlow dataflow);
	static UINT32 selectSharedModePeriod(Mumble::WASAPI::LatencyProfile profile, UINT32 defaultFrames,
									 UINT32 fundamentalFrames, UINT32 minimumFrames, UINT32 maximumFrames);
	static Mumble::InputEnhancement::DeviceIdentity resolveInputDeviceIdentity(const QString &configuredDevice,
														 ERole role, const QString &persistedIdentity = QString(),
														 const QString &routingPolicy = QStringLiteral("prefer"));
};

class WASAPIInput : public AudioInput {
private:
	Q_OBJECT
	Q_DISABLE_COPY(WASAPIInput)
public:
	WASAPIInput();
	~WASAPIInput() Q_DECL_OVERRIDE;
	void run() Q_DECL_OVERRIDE;
};

class WASAPIOutput : public AudioOutput {
private:
	Q_OBJECT
	Q_DISABLE_COPY(WASAPIOutput)

	bool setVolumeForSessionControl(IAudioSessionControl *control, const DWORD mumblePID, QSet< QUuid > &seen);
	bool setVolumeForSessionControl2(IAudioSessionControl2 *control2, const DWORD mumblePID, QSet< QUuid > &seen);

protected:
	typedef QPair< float, float > VolumePair;
	QMap< ISimpleAudioVolume *, VolumePair > qmVolumes;
	void setVolumes(IMMDevice *, bool talking);

public:
	WASAPIOutput();
	~WASAPIOutput() Q_DECL_OVERRIDE;
	void run() Q_DECL_OVERRIDE;
};

#endif
