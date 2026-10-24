// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ScreenShareInProcessPipeline.h"

#include <QtCore/QByteArray>
#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFileInfo>
#include <QtCore/QLibrary>
#include <QtCore/QTimer>

#ifdef Q_OS_WIN
#	include "win.h"

#	include <atomic>
#	include <memory>
#	include <vector>
#endif

namespace ScreenShareInProcessPipeline {

#ifdef Q_OS_WIN
namespace {

// --- Window geometry (shared with the session planner) -------------------------------------

bool extendedFrameBounds(HWND window, RECT *rect) {
	using DwmGetWindowAttributeFn                   = HRESULT(WINAPI *)(HWND, DWORD, PVOID, DWORD);
	constexpr DWORD DwmExtendedFrameBoundsAttribute = 9;
	static const HMODULE dwmapiModule               = LoadLibraryW(L"dwmapi.dll");
	static const DwmGetWindowAttributeFn getWindowAttribute =
		dwmapiModule
			? reinterpret_cast< DwmGetWindowAttributeFn >(GetProcAddress(dwmapiModule, "DwmGetWindowAttribute"))
			: nullptr;
	if (!getWindowAttribute) {
		return false;
	}

	RECT frameRect   = {};
	const HRESULT hr = getWindowAttribute(window, DwmExtendedFrameBoundsAttribute, &frameRect, sizeof(frameRect));
	if (FAILED(hr) || frameRect.right <= frameRect.left || frameRect.bottom <= frameRect.top) {
		return false;
	}

	*rect = frameRect;
	return true;
}

bool visibleWindowBounds(HWND window, RECT *rect) {
	if (!window || !rect) {
		return false;
	}

	if (extendedFrameBounds(window, rect)) {
		return true;
	}

	return GetWindowRect(window, rect) && rect->right > rect->left && rect->bottom > rect->top;
}

// --- Minimal dynamically-loaded GStreamer surface ------------------------------------------
//
// The screen-share helper does not link GStreamer; it ships and invokes the bundled runtime.
// To drive a live pipeline (retarget the capture crop, follow the congestion controller) we load
// the handful of symbols we need at runtime, mirroring the QLibrary/GetProcAddress pattern already
// used elsewhere in the helper.

using GstElement = void;
using GstBin     = void;
using GstBus     = void;
using GstMessage = void;
using GstEvent   = void;
using GstPad     = void;
using GCallback  = void (*)();

// Matches the public layout of GError; only the message field is read.
struct GErrorLayout {
	quint32 domain;
	qint32 code;
	char *message;
};

constexpr int GST_STATE_NULL    = 1;
constexpr int GST_STATE_PLAYING = 4;
constexpr int GST_MESSAGE_EOS   = 1 << 0;
constexpr int GST_MESSAGE_ERROR = 1 << 1;

constexpr int GST_PAD_PROBE_TYPE_BUFFER = 1 << 4;
constexpr int GST_PAD_PROBE_DROP        = 0;
constexpr int GST_PAD_PROBE_OK          = 1;

struct GstApi {
	using gst_init_fn                     = void (*)(int *, char ***);
	using gst_parse_launchv_fn            = GstElement *(*) (const char **, void **);
	using gst_bin_get_by_name_fn          = GstElement *(*) (GstBin *, const char *);
	using gst_element_set_state_fn        = int (*)(GstElement *, int);
	using gst_element_get_bus_fn          = GstBus *(*) (GstElement *);
	using gst_element_send_event_fn       = int (*)(GstElement *, GstEvent *);
	using gst_event_new_eos_fn            = GstEvent *(*) ();
	using gst_bus_timed_pop_filtered_fn   = GstMessage *(*) (GstBus *, quint64, int);
	using gst_message_parse_error_fn      = void (*)(GstMessage *, void **, char **);
	using gst_mini_object_unref_fn        = void (*)(void *);
	using gst_object_unref_fn             = void (*)(void *);
	using gst_element_get_factory_fn      = void *(*) (GstElement *);
	using gst_object_get_name_fn          = char *(*) (void *);
	using gst_element_get_static_pad_fn   = GstPad *(*) (GstElement *, const char *);
	using gst_pad_probe_callback          = int (*)(GstPad *, void *, void *);
	using gst_pad_add_probe_fn            = unsigned long (*)(GstPad *, int, gst_pad_probe_callback, void *, void *);
	using g_object_set_fn                 = void (*)(void *, const char *, ...);
	using g_object_get_fn                 = void (*)(void *, const char *, ...);
	using g_object_class_find_property_fn = void *(*) (void *, const char *);
	using g_signal_connect_data_fn        = unsigned long (*)(void *, const char *, GCallback, void *, void *, int);
	using g_free_fn                       = void (*)(void *);
	using g_error_free_fn                 = void (*)(void *);

	gst_init_fn gst_init                                         = nullptr;
	gst_parse_launchv_fn gst_parse_launchv                       = nullptr;
	gst_bin_get_by_name_fn gst_bin_get_by_name                   = nullptr;
	gst_element_set_state_fn gst_element_set_state               = nullptr;
	gst_element_get_bus_fn gst_element_get_bus                   = nullptr;
	gst_element_send_event_fn gst_element_send_event             = nullptr;
	gst_event_new_eos_fn gst_event_new_eos                       = nullptr;
	gst_bus_timed_pop_filtered_fn gst_bus_timed_pop_filtered     = nullptr;
	gst_message_parse_error_fn gst_message_parse_error           = nullptr;
	gst_mini_object_unref_fn gst_mini_object_unref               = nullptr;
	gst_object_unref_fn gst_object_unref                         = nullptr;
	gst_element_get_factory_fn gst_element_get_factory           = nullptr;
	gst_object_get_name_fn gst_object_get_name                   = nullptr;
	gst_element_get_static_pad_fn gst_element_get_static_pad     = nullptr;
	gst_pad_add_probe_fn gst_pad_add_probe                       = nullptr;
	g_object_set_fn g_object_set                                 = nullptr;
	g_object_get_fn g_object_get                                 = nullptr;
	g_object_class_find_property_fn g_object_class_find_property = nullptr;
	g_signal_connect_data_fn g_signal_connect_data               = nullptr;
	g_free_fn g_free                                             = nullptr;
	g_error_free_fn g_error_free                                 = nullptr;

	bool valid() const {
		return gst_init && gst_parse_launchv && gst_bin_get_by_name && gst_element_set_state
			   && gst_element_get_bus && gst_element_send_event && gst_event_new_eos
			   && gst_bus_timed_pop_filtered && gst_message_parse_error && gst_object_unref && g_object_set;
	}

	// Entry points only needed to follow the congestion controller of a webrtcsink.
	bool canFollowCongestionControl() const {
		return gst_element_get_factory && gst_object_get_name && gst_element_get_static_pad && gst_pad_add_probe
			   && g_object_get && g_object_class_find_property && g_signal_connect_data && g_free;
	}
};

bool loadLibraryByCandidates(QLibrary &library, const QString &binDir, const QStringList &baseNames) {
	for (const QString &baseName : baseNames) {
		// Prefer the bundled copy by absolute path, then fall back to the (PATH-augmented) name.
		const QStringList paths = { QDir(binDir).filePath(baseName), baseName };
		for (const QString &path : paths) {
			library.setFileName(path);
			if (library.load()) {
				return true;
			}
		}
	}
	return false;
}

template< typename Fn > Fn resolve(QLibrary &library, const char *symbol) {
	return reinterpret_cast< Fn >(library.resolve(symbol));
}

bool loadGstApi(GstApi *api, const QString &binDir, QString *errorMessage) {
	static QLibrary gstreamerLib;
	static QLibrary gobjectLib;
	static QLibrary glibLib;

	if (!loadLibraryByCandidates(gstreamerLib, binDir,
								 { QStringLiteral("gstreamer-1.0-0"), QStringLiteral("libgstreamer-1.0-0") })) {
		if (errorMessage) {
			*errorMessage = QStringLiteral("Unable to load the bundled GStreamer core library.");
		}
		return false;
	}
	if (!loadLibraryByCandidates(gobjectLib, binDir,
								 { QStringLiteral("gobject-2.0-0"), QStringLiteral("libgobject-2.0-0") })) {
		if (errorMessage) {
			*errorMessage = QStringLiteral("Unable to load the bundled GObject library.");
		}
		return false;
	}
	// GLib is optional (only used to free strings and error payloads); ignore load failures.
	loadLibraryByCandidates(glibLib, binDir, { QStringLiteral("glib-2.0-0"), QStringLiteral("libglib-2.0-0") });

	api->gst_init        = resolve< GstApi::gst_init_fn >(gstreamerLib, "gst_init");
	api->gst_parse_launchv = resolve< GstApi::gst_parse_launchv_fn >(gstreamerLib, "gst_parse_launchv");
	api->gst_bin_get_by_name = resolve< GstApi::gst_bin_get_by_name_fn >(gstreamerLib, "gst_bin_get_by_name");
	api->gst_element_set_state = resolve< GstApi::gst_element_set_state_fn >(gstreamerLib, "gst_element_set_state");
	api->gst_element_get_bus = resolve< GstApi::gst_element_get_bus_fn >(gstreamerLib, "gst_element_get_bus");
	api->gst_element_send_event = resolve< GstApi::gst_element_send_event_fn >(gstreamerLib, "gst_element_send_event");
	api->gst_event_new_eos = resolve< GstApi::gst_event_new_eos_fn >(gstreamerLib, "gst_event_new_eos");
	api->gst_bus_timed_pop_filtered =
		resolve< GstApi::gst_bus_timed_pop_filtered_fn >(gstreamerLib, "gst_bus_timed_pop_filtered");
	api->gst_message_parse_error = resolve< GstApi::gst_message_parse_error_fn >(gstreamerLib, "gst_message_parse_error");
	api->gst_mini_object_unref = resolve< GstApi::gst_mini_object_unref_fn >(gstreamerLib, "gst_mini_object_unref");
	api->gst_object_unref = resolve< GstApi::gst_object_unref_fn >(gstreamerLib, "gst_object_unref");
	api->gst_element_get_factory =
		resolve< GstApi::gst_element_get_factory_fn >(gstreamerLib, "gst_element_get_factory");
	api->gst_object_get_name = resolve< GstApi::gst_object_get_name_fn >(gstreamerLib, "gst_object_get_name");
	api->gst_element_get_static_pad =
		resolve< GstApi::gst_element_get_static_pad_fn >(gstreamerLib, "gst_element_get_static_pad");
	api->gst_pad_add_probe = resolve< GstApi::gst_pad_add_probe_fn >(gstreamerLib, "gst_pad_add_probe");
	api->g_object_set     = resolve< GstApi::g_object_set_fn >(gobjectLib, "g_object_set");
	api->g_object_get     = resolve< GstApi::g_object_get_fn >(gobjectLib, "g_object_get");
	api->g_object_class_find_property =
		resolve< GstApi::g_object_class_find_property_fn >(gobjectLib, "g_object_class_find_property");
	api->g_signal_connect_data = resolve< GstApi::g_signal_connect_data_fn >(gobjectLib, "g_signal_connect_data");
	api->g_free           = resolve< GstApi::g_free_fn >(glibLib, "g_free");
	api->g_error_free     = resolve< GstApi::g_error_free_fn >(glibLib, "g_error_free");

	if (!api->valid()) {
		if (errorMessage) {
			*errorMessage = QStringLiteral("The bundled GStreamer runtime is missing required entry points.");
		}
		return false;
	}
	return true;
}

void configureGStreamerEnvironment(const QString &binDir) {
	if (binDir.isEmpty()) {
		return;
	}

	QDir runtimeRoot(binDir);
	runtimeRoot.cdUp();

	const QString pluginPath = runtimeRoot.filePath(QStringLiteral("lib/gstreamer-1.0"));
	if (QFileInfo::exists(pluginPath)) {
		const QByteArray pluginPathUtf8 = QDir::toNativeSeparators(pluginPath).toUtf8();
		qputenv("GST_PLUGIN_SYSTEM_PATH_1_0", pluginPathUtf8);
		qputenv("GST_PLUGIN_PATH", pluginPathUtf8);
	}

	for (const QString &scannerCandidate :
		 { runtimeRoot.filePath(QStringLiteral("libexec/gstreamer-1.0/gst-plugin-scanner.exe")),
		   runtimeRoot.filePath(QStringLiteral("libexec/gstreamer-1.0/gst-plugin-scanner")) }) {
		if (QFileInfo::exists(scannerCandidate)) {
			qputenv("GST_PLUGIN_SCANNER", QDir::toNativeSeparators(scannerCandidate).toUtf8());
			break;
		}
	}

	const QByteArray nativeBinDir = QDir::toNativeSeparators(binDir).toUtf8();
	const QByteArray currentPath  = qgetenv("PATH");
	qputenv("PATH", currentPath.isEmpty() ? nativeBinDir : nativeBinDir + ';' + currentPath);
}

std::atomic< bool > g_stopRequested{ false };

BOOL WINAPI consoleControlHandler(DWORD) {
	g_stopRequested.store(true);
	return TRUE;
}

// --- Congestion-control follower -----------------------------------------------------------
//
// We feed webrtcsink pre-encoded H.264, so it has no encoder of its own to apply its GCC
// estimate to. Its per-consumer pipeline still runs rtpgccbwe on the TWCC feedback (and paces
// the RTP to that estimate), so we pick the estimate up there and apply it to our encoder.
// There is a single consumer (the LiveKit SFU), so one estimate drives the one encoder.

// Lowest bitrate handed to the encoder, however low the estimate drops.
constexpr int MIN_ADAPTIVE_BITRATE_KBPS = 300;

// The signal handlers below run on webrtcsink's threads and can still fire while the pipeline
// shuts down, so their state lives for the whole process.
struct ConsumerWatch {
	GstApi api;
	std::atomic< quint32 > estimatedBitrate{ 0 }; // bit/s, 0 until rtpgccbwe reports one
	// Test-only: drop every Nth outgoing RTP packet before SRTP, so the loss reaches GCC and NACK.
	int testRtpDropEvery = 0;
	std::atomic< quint64 > testRtpCount{ 0 };
	std::atomic< bool > testRtpDropAttached{ false };
};

ConsumerWatch g_consumerWatch;

QString takeObjectName(void *object) {
	char *name          = g_consumerWatch.api.gst_object_get_name(object);
	const QString value = QString::fromUtf8(name);
	g_consumerWatch.api.g_free(name);
	return value;
}

void onEstimatedBitrate(void *bandwidthEstimator, void *, void *) {
	unsigned int bitrate = 0;
	g_consumerWatch.api.g_object_get(bandwidthEstimator, "estimated-bitrate", &bitrate,
									 static_cast< const char * >(nullptr));
	g_consumerWatch.estimatedBitrate.store(bitrate);
}

int dropTestRtp(GstPad *, void *, void *) {
	const quint64 count = ++g_consumerWatch.testRtpCount;
	return count % static_cast< quint64 >(g_consumerWatch.testRtpDropEvery) == 0 ? GST_PAD_PROBE_DROP
																				 : GST_PAD_PROBE_OK;
}

void attachTestRtpDrop(GstPad *pad) {
	// With max-bundle there is one RTP pad per consumer; attach to it once.
	if (!g_consumerWatch.testRtpDropAttached.exchange(true)) {
		g_consumerWatch.api.gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, dropTestRtp, nullptr, nullptr);
		qInfo().noquote() << QStringLiteral("ScreenShareInProcessPipeline: test mode, dropping RTP on srtpenc %1.")
								 .arg(takeObjectName(pad));
	}
}

void onSrtpEncoderPadAdded(GstElement *, GstPad *pad, void *) {
	if (takeObjectName(pad).startsWith(QLatin1String("rtp_sink_"))) {
		attachTestRtpDrop(pad);
	}
}

void onConsumerElementAdded(GstBin *, GstBin *, GstElement *element, void *) {
	const GstApi &api = g_consumerWatch.api;
	void *factory     = api.gst_element_get_factory(element);
	if (!factory) {
		return;
	}

	const QString factoryName = takeObjectName(factory);
	if (factoryName == QLatin1String("rtpgccbwe")) {
		// Only take values from notifications: webrtcsink configures the start bitrate after
		// adding the element, so its current value may still be the element default.
		api.g_signal_connect_data(element, "notify::estimated-bitrate",
								  reinterpret_cast< GCallback >(&onEstimatedBitrate), nullptr, nullptr, 0);
		qInfo().noquote() << QStringLiteral("ScreenShareInProcessPipeline: following the GCC bandwidth estimate.");
	} else if (factoryName == QLatin1String("srtpenc") && g_consumerWatch.testRtpDropEvery > 0) {
		api.g_signal_connect_data(element, "pad-added", reinterpret_cast< GCallback >(&onSrtpEncoderPadAdded),
								  nullptr, nullptr, 0);
		// dtlssrtpenc usually requests the pad before srtpenc reaches the consumer pipeline.
		if (GstPad *pad = api.gst_element_get_static_pad(element, "rtp_sink_0")) {
			attachTestRtpDrop(pad);
			api.gst_object_unref(pad);
		}
	}
}

void onConsumerPipelineCreated(GstElement *, const char *peerID, GstElement *consumerPipeline, void *) {
	g_consumerWatch.testRtpDropAttached.store(false);
	g_consumerWatch.api.g_signal_connect_data(consumerPipeline, "deep-element-added",
											  reinterpret_cast< GCallback >(&onConsumerElementAdded), nullptr,
											  nullptr, 0);
	qInfo().noquote() << QStringLiteral("ScreenShareInProcessPipeline: consumer pipeline created for %1.")
							 .arg(QString::fromUtf8(peerID));
}

// Applies the latest GCC estimate to the encoder. Runs on the main thread.
class EncoderBitrateFollower {
public:
	EncoderBitrateFollower(const GstApi &api, GstElement *encoder, const Options &options)
		: m_api(api), m_encoder(encoder), m_options(options), m_appliedKbps(options.bitrateKbps) {
		// G_OBJECT_GET_CLASS: the class pointer is the first field of every GTypeInstance.
		m_setMaxBitrate = options.maxBitrateKbps > 0
						  && api.g_object_class_find_property(*static_cast< void ** >(encoder), "max-bitrate");
		m_sinceApplied.start();
	}

	void update() {
		const quint32 estimatedBitrate = g_consumerWatch.estimatedBitrate.load();
		if (estimatedBitrate == 0) {
			return;
		}

		const int ceilingKbps = m_options.bitrateKbps;
		const int floorKbps   = qMin(MIN_ADAPTIVE_BITRATE_KBPS, ceilingKbps);
		const int targetKbps  = qBound(floorKbps, static_cast< int >(estimatedBitrate / 1000), ceilingKbps);
		if (targetKbps == m_appliedKbps) {
			return;
		}

		// Encoders that re-open on every change (mfh264enc drains and re-creates its MFT, which
		// also starts with a keyframe) only follow larger and less frequent steps.
		const bool costly        = m_options.costlyBitrateReconfigure;
		const qint64 minInterval = costly ? 5000 : 500;
		const int minStepPercent = costly ? 20 : 5;
		const bool atLimit       = targetKbps == floorKbps || targetKbps == ceilingKbps;
		const int stepPercent    = qAbs(targetKbps - m_appliedKbps) * 100 / qMax(1, m_appliedKbps);
		if (m_sinceApplied.elapsed() < minInterval || (stepPercent < minStepPercent && !atLimit)) {
			return;
		}

		const unsigned int scale   = static_cast< unsigned int >(qMax(1, m_options.bitratePropertyScale));
		const unsigned int bitrate = static_cast< unsigned int >(targetKbps) * scale;
		if (m_setMaxBitrate) {
			// Keep the plan's peak-to-average ratio.
			const qint64 maxKbps =
				qMax< qint64 >(targetKbps, static_cast< qint64 >(targetKbps) * m_options.maxBitrateKbps / ceilingKbps);
			m_api.g_object_set(m_encoder, "bitrate", bitrate, "max-bitrate",
							   static_cast< unsigned int >(maxKbps) * scale, static_cast< const char * >(nullptr));
		} else {
			m_api.g_object_set(m_encoder, "bitrate", bitrate, static_cast< const char * >(nullptr));
		}

		qInfo().noquote()
			<< QStringLiteral("ScreenShareInProcessPipeline: GCC estimate %1 kbit/s; encoder bitrate %2 -> %3 kbit/s.")
				   .arg(estimatedBitrate / 1000)
				   .arg(m_appliedKbps)
				   .arg(targetKbps);
		m_appliedKbps = targetKbps;
		m_sinceApplied.restart();
	}

private:
	const GstApi &m_api;
	GstElement *m_encoder;
	const Options &m_options;
	int m_appliedKbps;
	bool m_setMaxBitrate = false;
	QElapsedTimer m_sinceApplied;
};

} // namespace

WindowCrop computeWindowCrop(quint64 windowHandle) {
	WindowCrop crop;

	HWND window = reinterpret_cast< HWND >(windowHandle);
	if (!window || !IsWindow(window)) {
		return crop;
	}

	RECT windowRect = {};
	if (!visibleWindowBounds(window, &windowRect)) {
		return crop;
	}

	const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
	if (!monitor) {
		return crop;
	}

	MONITORINFO monitorInfo = {};
	monitorInfo.cbSize      = sizeof(monitorInfo);
	if (!GetMonitorInfoW(monitor, &monitorInfo)) {
		return crop;
	}

	const LONG left   = qMax(windowRect.left, monitorInfo.rcMonitor.left);
	const LONG top    = qMax(windowRect.top, monitorInfo.rcMonitor.top);
	const LONG right  = qMin(windowRect.right, monitorInfo.rcMonitor.right);
	const LONG bottom = qMin(windowRect.bottom, monitorInfo.rcMonitor.bottom);
	if (right <= left || bottom <= top) {
		return crop;
	}

	crop.valid         = true;
	crop.monitorHandle = reinterpret_cast< qulonglong >(monitor);
	crop.cropX         = static_cast< int >(left - monitorInfo.rcMonitor.left);
	crop.cropY         = static_cast< int >(top - monitorInfo.rcMonitor.top);
	crop.cropWidth     = static_cast< int >(right - left);
	crop.cropHeight    = static_cast< int >(bottom - top);
	return crop;
}

int run(const Options &options) {
	const bool followWindow = options.windowHandle != 0;
	if (options.pipelineTokens.isEmpty() || (followWindow && options.sourceName.trimmed().isEmpty())) {
		qCritical().noquote() << QStringLiteral("ScreenShareInProcessPipeline: missing pipeline or capture source name.");
		return 1;
	}

	configureGStreamerEnvironment(options.gstBinDir);

	GstApi api;
	QString loadError;
	if (!loadGstApi(&api, options.gstBinDir, &loadError)) {
		qCritical().noquote() << QStringLiteral("ScreenShareInProcessPipeline: %1").arg(loadError);
		return 1;
	}

	api.gst_init(nullptr, nullptr);

	g_consumerWatch.api              = api;
	g_consumerWatch.testRtpDropEvery = qMax(0, qEnvironmentVariableIntValue("MUMBLE_SCREENSHARE_TEST_RTP_DROP_EVERY"));

	// gst_parse_launchv expects a NULL-terminated argv array. Keeping the tokens separate (rather
	// than joining into one string) preserves embedded spaces, e.g. the participant name.
	std::vector< QByteArray > tokenStorage;
	tokenStorage.reserve(options.pipelineTokens.size());
	std::vector< const char * > argv;
	argv.reserve(options.pipelineTokens.size() + 1);
	for (const QString &token : options.pipelineTokens) {
		tokenStorage.push_back(token.toUtf8());
		argv.push_back(tokenStorage.back().constData());
	}
	argv.push_back(nullptr);

	void *errorPayload  = nullptr;
	GstElement *pipeline = api.gst_parse_launchv(argv.data(), &errorPayload);
	// Like gst-launch, treat a recoverable parse error (for example a property the element does not
	// have) as fatal too: the partly built pipeline would be missing that element.
	if (!pipeline || errorPayload) {
		if (pipeline) {
			api.gst_object_unref(pipeline);
		}
		QString detail = QStringLiteral("unknown error");
		if (errorPayload) {
			const GErrorLayout *error = reinterpret_cast< GErrorLayout * >(errorPayload);
			if (error->message) {
				detail = QString::fromUtf8(error->message);
			}
			if (api.g_error_free) {
				api.g_error_free(errorPayload);
			}
		}
		qCritical().noquote() << QStringLiteral("ScreenShareInProcessPipeline: failed to build pipeline: %1").arg(detail);
		return 1;
	}

	GstElement *source = nullptr;
	if (followWindow) {
		source = api.gst_bin_get_by_name(pipeline, options.sourceName.toUtf8().constData());
		if (!source) {
			qCritical().noquote()
				<< QStringLiteral("ScreenShareInProcessPipeline: capture element '%1' was not found in the pipeline.")
					   .arg(options.sourceName);
			api.gst_element_set_state(pipeline, GST_STATE_NULL);
			api.gst_object_unref(pipeline);
			return 1;
		}
	}

	// Without the follower the stream keeps the plan's fixed bitrate, so a missing piece only warns.
	GstElement *encoder = nullptr;
	std::unique_ptr< EncoderBitrateFollower > bitrateFollower;
	if (!options.sinkName.isEmpty() && !options.encoderName.isEmpty() && options.bitrateKbps > 0) {
		GstElement *sink = nullptr;
		if (api.canFollowCongestionControl()) {
			sink    = api.gst_bin_get_by_name(pipeline, options.sinkName.toUtf8().constData());
			encoder = api.gst_bin_get_by_name(pipeline, options.encoderName.toUtf8().constData());
		}
		if (sink && encoder) {
			api.g_signal_connect_data(sink, "consumer-pipeline-created",
									  reinterpret_cast< GCallback >(&onConsumerPipelineCreated), nullptr, nullptr, 0);
			bitrateFollower = std::make_unique< EncoderBitrateFollower >(api, encoder, options);
		} else {
			qWarning().noquote() << QStringLiteral(
				"ScreenShareInProcessPipeline: cannot follow the congestion controller; the bitrate stays fixed.");
			if (encoder) {
				api.gst_object_unref(encoder);
				encoder = nullptr;
			}
		}
		if (sink) {
			api.gst_object_unref(sink);
		}
	}
	if (g_consumerWatch.testRtpDropEvery > 0) {
		qWarning().noquote() << QStringLiteral("ScreenShareInProcessPipeline: test mode, dropping every %1th RTP packet.")
									.arg(g_consumerWatch.testRtpDropEvery);
	}

	GstBus *bus = api.gst_element_get_bus(pipeline);

	if (api.gst_element_set_state(pipeline, GST_STATE_PLAYING) == 0 /* GST_STATE_CHANGE_FAILURE */) {
		qCritical().noquote() << QStringLiteral("ScreenShareInProcessPipeline: failed to start the pipeline.");
		api.gst_element_set_state(pipeline, GST_STATE_NULL);
		if (bus) {
			api.gst_object_unref(bus);
		}
		if (encoder) {
			api.gst_object_unref(encoder);
		}
		if (source) {
			api.gst_object_unref(source);
		}
		api.gst_object_unref(pipeline);
		return 1;
	}

	if (followWindow) {
		qInfo().noquote() << QStringLiteral("ScreenShareInProcessPipeline: tracking window 0x%1 for live crop.")
								 .arg(options.windowHandle, 0, 16);
	}

	SetConsoleCtrlHandler(consoleControlHandler, TRUE);

	int exitCode = 0;
	WindowCrop applied; // last crop pushed to the element

	QTimer tickTimer;
	tickTimer.setInterval(80);
	QObject::connect(&tickTimer, &QTimer::timeout, [&]() {
		if (g_stopRequested.load()) {
			api.gst_element_send_event(pipeline, api.gst_event_new_eos());
			QCoreApplication::quit();
			return;
		}

		if (followWindow) {
			// Stop following once the tracked window is gone; otherwise monitor capture would keep
			// streaming whatever now occupies that region of the desktop.
			if (!IsWindow(reinterpret_cast< HWND >(options.windowHandle))) {
				qInfo().noquote() << QStringLiteral("ScreenShareInProcessPipeline: tracked window closed; ending capture.");
				api.gst_element_send_event(pipeline, api.gst_event_new_eos());
				QCoreApplication::quit();
				return;
			}

			const WindowCrop crop = computeWindowCrop(options.windowHandle);
			if (crop.valid) {
				// d3d11screencapturesrc registers monitor-handle as guint64 and crop-* as guint, both
				// GST_PARAM_MUTABLE_PLAYING, so these can be retargeted live without a state change.
				if (crop.monitorHandle != applied.monitorHandle) {
					api.g_object_set(source, "monitor-handle", static_cast< quint64 >(crop.monitorHandle),
									 static_cast< const char * >(nullptr));
				}
				if (crop.cropX != applied.cropX || crop.cropY != applied.cropY || crop.cropWidth != applied.cropWidth
					|| crop.cropHeight != applied.cropHeight || crop.monitorHandle != applied.monitorHandle) {
					api.g_object_set(source, "crop-x", static_cast< unsigned int >(crop.cropX), "crop-y",
									 static_cast< unsigned int >(crop.cropY), "crop-width",
									 static_cast< unsigned int >(crop.cropWidth), "crop-height",
									 static_cast< unsigned int >(crop.cropHeight), static_cast< const char * >(nullptr));
				}
				applied = crop;
			}
		}

		if (bitrateFollower) {
			bitrateFollower->update();
		}

		if (GstMessage *message = api.gst_bus_timed_pop_filtered(bus, 0, GST_MESSAGE_ERROR)) {
			void *errPayload = nullptr;
			char *debugInfo  = nullptr;
			api.gst_message_parse_error(message, &errPayload, &debugInfo);
			QString detail = QStringLiteral("unknown error");
			if (errPayload) {
				const GErrorLayout *error = reinterpret_cast< GErrorLayout * >(errPayload);
				if (error->message) {
					detail = QString::fromUtf8(error->message);
				}
				if (api.g_error_free) {
					api.g_error_free(errPayload);
				}
			}
			if (debugInfo && api.g_free) {
				api.g_free(debugInfo);
			}
			if (api.gst_mini_object_unref) {
				api.gst_mini_object_unref(message);
			}
			qCritical().noquote() << QStringLiteral("ScreenShareInProcessPipeline: pipeline error: %1").arg(detail);
			exitCode = 1;
			QCoreApplication::quit();
			return;
		}

		if (GstMessage *message = api.gst_bus_timed_pop_filtered(bus, 0, GST_MESSAGE_EOS)) {
			if (api.gst_mini_object_unref) {
				api.gst_mini_object_unref(message);
			}
			QCoreApplication::quit();
			return;
		}
	});
	tickTimer.start();

	QCoreApplication::exec();

	api.gst_element_set_state(pipeline, GST_STATE_NULL);
	bitrateFollower.reset();
	if (bus) {
		api.gst_object_unref(bus);
	}
	if (encoder) {
		api.gst_object_unref(encoder);
	}
	if (source) {
		api.gst_object_unref(source);
	}
	api.gst_object_unref(pipeline);
	return exitCode;
}

#else // !Q_OS_WIN

WindowCrop computeWindowCrop(quint64) {
	return WindowCrop();
}

int run(const Options &) {
	qCritical("ScreenShareInProcessPipeline: the in-process pipeline is only available on Windows.");
	return 1;
}

#endif

} // namespace ScreenShareInProcessPipeline
