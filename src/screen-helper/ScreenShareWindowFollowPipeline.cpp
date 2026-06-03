// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ScreenShareWindowFollowPipeline.h"

#include <QtCore/QByteArray>
#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QLibrary>
#include <QtCore/QTimer>

#ifdef Q_OS_WIN
#	include "win.h"

#	include <atomic>
#	include <vector>
#endif

namespace ScreenShareWindowFollow {

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
// To retarget the capture crop on a live pipeline we load the handful of symbols we need at
// runtime, mirroring the QLibrary/GetProcAddress pattern already used elsewhere in the helper.

using GstElement = void;
using GstBin     = void;
using GstBus     = void;
using GstMessage = void;
using GstEvent   = void;

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

struct GstApi {
	using gst_init_fn                   = void (*)(int *, char ***);
	using gst_parse_launchv_fn          = GstElement *(*) (const char **, void **);
	using gst_bin_get_by_name_fn        = GstElement *(*) (GstBin *, const char *);
	using gst_element_set_state_fn      = int (*)(GstElement *, int);
	using gst_element_get_bus_fn        = GstBus *(*) (GstElement *);
	using gst_element_send_event_fn     = int (*)(GstElement *, GstEvent *);
	using gst_event_new_eos_fn          = GstEvent *(*) ();
	using gst_bus_timed_pop_filtered_fn = GstMessage *(*) (GstBus *, quint64, int);
	using gst_message_parse_error_fn    = void (*)(GstMessage *, void **, char **);
	using gst_mini_object_unref_fn      = void (*)(void *);
	using gst_object_unref_fn           = void (*)(void *);
	using g_object_set_fn               = void (*)(void *, const char *, ...);
	using g_free_fn                     = void (*)(void *);
	using g_error_free_fn               = void (*)(void *);

	gst_init_fn gst_init                                     = nullptr;
	gst_parse_launchv_fn gst_parse_launchv                  = nullptr;
	gst_bin_get_by_name_fn gst_bin_get_by_name             = nullptr;
	gst_element_set_state_fn gst_element_set_state         = nullptr;
	gst_element_get_bus_fn gst_element_get_bus             = nullptr;
	gst_element_send_event_fn gst_element_send_event       = nullptr;
	gst_event_new_eos_fn gst_event_new_eos                 = nullptr;
	gst_bus_timed_pop_filtered_fn gst_bus_timed_pop_filtered = nullptr;
	gst_message_parse_error_fn gst_message_parse_error     = nullptr;
	gst_mini_object_unref_fn gst_mini_object_unref         = nullptr;
	gst_object_unref_fn gst_object_unref                   = nullptr;
	g_object_set_fn g_object_set                           = nullptr;
	g_free_fn g_free                                       = nullptr;
	g_error_free_fn g_error_free                           = nullptr;

	bool valid() const {
		return gst_init && gst_parse_launchv && gst_bin_get_by_name && gst_element_set_state
			   && gst_element_get_bus && gst_element_send_event && gst_event_new_eos
			   && gst_bus_timed_pop_filtered && gst_message_parse_error && gst_object_unref && g_object_set;
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
	// GLib is optional (only used to free error payloads); ignore load failures.
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
	api->g_object_set     = resolve< GstApi::g_object_set_fn >(gobjectLib, "g_object_set");
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
	if (options.pipelineTokens.isEmpty() || options.sourceName.trimmed().isEmpty() || options.windowHandle == 0) {
		qCritical().noquote() << QStringLiteral("ScreenShareWindowFollow: missing pipeline, source name, or window handle.");
		return 1;
	}

	configureGStreamerEnvironment(options.gstBinDir);

	GstApi api;
	QString loadError;
	if (!loadGstApi(&api, options.gstBinDir, &loadError)) {
		qCritical().noquote() << QStringLiteral("ScreenShareWindowFollow: %1").arg(loadError);
		return 1;
	}

	api.gst_init(nullptr, nullptr);

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
	if (!pipeline) {
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
		qCritical().noquote() << QStringLiteral("ScreenShareWindowFollow: failed to build pipeline: %1").arg(detail);
		return 1;
	}

	GstElement *source = api.gst_bin_get_by_name(pipeline, options.sourceName.toUtf8().constData());
	if (!source) {
		qCritical().noquote()
			<< QStringLiteral("ScreenShareWindowFollow: capture element '%1' was not found in the pipeline.")
				   .arg(options.sourceName);
		api.gst_element_set_state(pipeline, GST_STATE_NULL);
		api.gst_object_unref(pipeline);
		return 1;
	}

	GstBus *bus = api.gst_element_get_bus(pipeline);

	if (api.gst_element_set_state(pipeline, GST_STATE_PLAYING) == 0 /* GST_STATE_CHANGE_FAILURE */) {
		qCritical().noquote() << QStringLiteral("ScreenShareWindowFollow: failed to start the capture pipeline.");
		api.gst_element_set_state(pipeline, GST_STATE_NULL);
		if (source) {
			api.gst_object_unref(source);
		}
		api.gst_object_unref(pipeline);
		return 1;
	}

	qInfo().noquote()
		<< QStringLiteral("ScreenShareWindowFollow: tracking window 0x%1 for live crop.").arg(options.windowHandle, 0, 16);

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

		// Stop following once the tracked window is gone; otherwise monitor capture would keep
		// streaming whatever now occupies that region of the desktop.
		if (!IsWindow(reinterpret_cast< HWND >(options.windowHandle))) {
			qInfo().noquote() << QStringLiteral("ScreenShareWindowFollow: tracked window closed; ending capture.");
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
			qCritical().noquote() << QStringLiteral("ScreenShareWindowFollow: pipeline error: %1").arg(detail);
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
	if (bus) {
		api.gst_object_unref(bus);
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
	qCritical("ScreenShareWindowFollow: window-following capture is only available on Windows.");
	return 1;
}

#endif

} // namespace ScreenShareWindowFollow
