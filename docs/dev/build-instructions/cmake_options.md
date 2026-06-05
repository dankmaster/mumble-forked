# CMake options

Using CMake the build can be customized in a number of ways. The most prominent examples for this is the usage of different
options (flags). These can be set by using `-D<optionName>=<value>` where `<optionName>` is the name of the respective option
as listed below and `<value>` is either `ON` or `OFF` depending on whether the option shall be activated or inactivated.

An example would be `cmake -Dtests=ON ..`.


## Available options

### alsa

Build support for ALSA.
(Default: ON)

### benchmarks

Build benchmarks
(Default: OFF)

### bundle-qt-translations

Bundle Qt's translations as well
(Default: ${static})

### bundled-cli11

Use the bundled CLI11 version instead of looking for one on the system
(Default: ON)

### bundled-json

Build the included version of nlohmann_json instead of looking for one on the system
(Default: ON)

### bundled-rnnoise

Build the included version of RNNoise instead of looking for one on the system.
(Default: ${rnnoise})

### bundled-soci

Build the included version of SOCI instead of looking for one on the system
(Default: ON)

### bundled-spdlog

Use the bundled spdlog version instead of looking for one on the system
(Default: ON)

### bundled-speex

Build the included version of Speex instead of looking for one on the system.
(Default: ON)

### bundled-utfcpp

Use the bundled utf8cpp version instead of looking for one on the system
(Default: ON)

### chat-perf-trace

Include the developer chat performance tracer (mumble::chatperf).
(Default: ${MUMBLE_MODERN_LAYOUT_TOOLING_DEFAULT})

### client

Build the client (Mumble)
(Default: ON)

### coreaudio

Build support for CoreAudio.
(Default: ON)

### crash-report

Include support for reporting crashes to the Mumble developers.
(Default: ON)

### database-mysql-tests

Whether to include the MySQL database tests (requires special setup)
(Default: OFF)

### database-postgresql-tests

Whether to include the PostgreSQL database tests (requires special setup)
(Default: OFF)

### database-sqlite-tests

Whether to include the SQLite database tests
(Default: ON)

### dbus

Enable DBus support/interface
(Default: ON)

### debug-dependency-search

Prints extended information during the search for the needed dependencies
(Default: OFF)

### deepfilternet

Use DeepFilterNet for machine learning speech cleanup.
(Default: OFF)

### display-install-paths

Print out base install paths during project configuration
(Default: OFF)

### dtln

Use DTLN for machine learning speech cleanup.
(Default: OFF)

### elevation

Set \"uiAccess=true\", required for global shortcuts to work with privileged applications. Requires the client's executable to be signed with a trusted code signing certificate.
(Default: OFF)

### enable-mysql

Whether or not to enable the MySQL database backend
(Default: ${server})

### enable-postgresql

Whether or not to enable the PostgreSQL database backend
(Default: ${server})

### enable-sqlite

Whether or not to enable the SQLite database backend
(Default: ON)

### gkey

Build support for Logitech G-Keys. Note: This feature does not require any build-time dependencies, and requires Logitech Gaming Software to be installed to have any effect at runtime.
(Default: ON)

### ice

Build support for Ice RPC.
(Default: ON)

### jackaudio

Build support for JackAudio.
(Default: ON)

### lto

Enables link-time optimizations for release builds
(Default: ${LTO_DEFAULT})

### manual-plugin

Include the built-in "manual" positional audio plugin.
(Default: ON)

### modern-layout-automation

Include the Modern UI automation server and JavaScript automation hooks.
(Default: ${MUMBLE_MODERN_LAYOUT_TOOLING_DEFAULT})

### modern-layout-mockups

Include Modern shell mockup and walkthrough functionality.
(Default: ${MUMBLE_MODERN_LAYOUT_TOOLING_DEFAULT})

### modern-layout-webengine

Enable the Qt WebEngine-based relay runtime.
(Default: OFF)

### online-tests

Whether or not tests that need a working internet connection should be included
(Default: OFF)

### optimize

Build a heavily optimized version, specific to the machine it's being compiled on.
(Default: OFF)

### oss

Build support for OSS.
(Default: ON)

### packaging

Build package.
(Default: OFF)

### pipewire

Build support for PipeWire.
(Default: ON)

### plugin-callback-debug

Build Mumble with debug output for plugin callbacks inside of Mumble.
(Default: OFF)

### plugin-debug

Build Mumble with debug output for plugin developers.
(Default: OFF)

### plugins

Build bundled plugins.
(Default: OFF)

### portaudio

Build support for PortAudio
(Default: ON)

### pulseaudio

Build support for PulseAudio.
(Default: ON)

### qssldiffiehellmanparameters

Build support for custom Diffie-Hellman parameters.
(Default: ON)

### qtspeech

Use Qt's text-to-speech system (part of the Qt Speech module) instead of Mumble's own OS-specific text-to-speech implementations.
(Default: OFF)

### retracted-plugins

Build redacted (outdated) plugins as well
(Default: OFF)

### rnnoise

Use RNNoise for machine learning noise reduction.
(Default: ON)

### server

Build the server (Murmur)
(Default: ON)

### skip-msi-rebuild

Prevent rebuilding installer MSI files from source. Used to be able to include signed MSI files in the bundle.
(Default: OFF)

### speechd

Build support for Speech Dispatcher.
(Default: ON)

### static

Build static binaries.
(Default: OFF)

### symbols

Build binaries in a way that allows easier debugging.
(Default: OFF)

### test-lto

Whether to use LTO when building test cases
(Default: ${lto})

### tests

Build tests.
(Default: ${packaging})

### tracy

Enable the tracy profiler.
(Default: OFF)

### translations

Include languages other than English.
(Default: ON)

### update

Check for updates by default.
(Default: ON)

### use-pkgconf-install-paths

Try to query install paths from pkgconf - this is incompatible to using CMAKE_INSTALL_PREFIX
(Default: OFF)

### use-timestamps

Allow using compile-time timestamps
(Default: ON)

### warnings-as-errors

All warnings are treated as errors.
(Default: ON)

### wasapi

Build support for WASAPI.
(Default: ON)

### webrtc-aec

Expose WebRTC AEC as an experimental echo cancellation option.
(Default: OFF)

### windows-installer-all-languages

Build Windows installers with all translated MSI transforms instead of the default English-only MSI.
(Default: OFF)

### xboxinput

Build support for global shortcuts from Xbox controllers via the XInput DLL.
(Default: ON)

### xinput2

Build support for XI2.
(Default: ON)

### zeroconf

Build support for zeroconf (mDNS/DNS-SD).
(Default: ON)


