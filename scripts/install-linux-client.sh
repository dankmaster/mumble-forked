#!/usr/bin/env bash
#
# Native install for the mumble-forked Linux client + screen-share helper.
#
# This installs natively (into /usr/local by default) rather than bundling or
# sandboxing the GStreamer runtime. That keeps the screen-share pipeline as
# close as possible to the system's hardware-encoding stack (VAAPI / NVENC) and
# the compositor's PipeWire/portal node, which is the lowest-latency, highest-fps
# client arrangement for this feature.
#
# The client auto-launches "mumble-screen-helper" from its own dir or PATH, so
# the three binaries are installed next to each other and the helper is found
# automatically; it inherits the client environment, so a system-installed
# GStreamer + LiveKit (rswebrtc) plugin is picked up with no extra env.
#
# Usage:
#   ./scripts/install-linux-client.sh [--prefix /usr/local] [--from build]

set -euo pipefail

PREFIX="/usr/local"
SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN_SRC="$SRC_DIR/build"

while [[ $# -gt 0 ]]; do
	case "$1" in
		--prefix) PREFIX="${2:?--prefix requires an argument}"; shift 2 ;;
		--from)   BIN_SRC="${2:?--from requires an argument}"; shift 2 ;;
		*) echo "Unknown option: $1" >&2; exit 2 ;;
	esac
done

BIN_DEST="$PREFIX/bin"
APPS_DEST="$PREFIX/share/applications"

distro() {
	if command -v dnf >/dev/null 2>&1; then echo dnf
	elif command -v apt-get >/dev/null 2>&1; then echo apt
	else echo unknown; fi
}

install_gstreamer_deps() {
	case "$(distro)" in
		dnf)
			sudo dnf install -y \
				gstreamer1 gstreamer1-plugins-base gstreamer1-plugins-good \
				gstreamer1-plugins-bad-free gstreamer1-plugins-good-va \
				libnice-gstreamer1 gstreamer1-plugin-pipewire pipewire \
				gst-inspect-1.0
			;;
		apt)
			sudo apt-get update && sudo apt-get install -y \
				gstreamer1.0-tools gstreamer1.0-plugins-base \
				gstreamer1.0-plugins-good gstreamer1.0-plugins-bad \
				gstreamer1.0-vaapi gstreamer1.0-libnice \
				gstreamer1.0-pipewire pipewire
			;;
		*)
			echo "Unsupported package manager; install GStreamer + libnice + PipeWire manually." >&2
			;;
	esac
}

# The LiveKit publish path needs the GStreamer elements livekitwebrtcsink (for
# publishing) and livekitwebrtcsrc (for viewing), provided by libgstrswebrtc.so
# built from gitlab.freedesktop.org/gstreamer/gst-plugins-rs. It is not packaged
# by Fedora yet, so it is only detected here, never installed.
ensure_livekit_plugin() {
	if gst-inspect-1.0 --exists livekitwebrtcsink 2>/dev/null \
		&& gst-inspect-1.0 --exists livekitwebrtcsrc 2>/dev/null; then
		echo "LiveKit GStreamer plugin (livekitwebrtcsink/src) found."
		return 0
	fi

	cat <<EOF >&2
WARNING: LiveKit GStreamer elements livekitwebrtcsink/livekitwebrtcsrc were not
found. Screen sharing will not publish/view until libgstrswebrtc.so is installed.
It is not packaged by Fedora; build the "livekit" feature of
gitlab.freedesktop.org/gstreamer/gst-plugins-rs and place libgstrswebrtc.so in a
GStreamer plugin directory (e.g. /usr/lib64/gstreamer-1.0/).
EOF
	return 1
}

install_binaries() {
	sudo install -d "$BIN_DEST" "$APPS_DEST"
	for bin in mumble mumble-server mumble-screen-helper; do
		[[ -x "$BIN_SRC/$bin" ]] || { echo "Missing $bin at $BIN_SRC/$bin" >&2; exit 1; }
		echo "Installing $bin -> $BIN_DEST/$bin"
		sudo install -m 0755 "$BIN_SRC/$bin" "$BIN_DEST/$bin"
	done
}

write_desktop() {
	local icon="$SRC_DIR/icons/mumble.svg"
	local desktop="$APPS_DEST/mumble-forked.desktop"
	local content="[Desktop Entry]
Type=Application
Name=mumble-forked
Comment=Mumble client with screen sharing
Exec=$BIN_DEST/mumble %U
Icon=$icon
Terminal=false
Categories=Network;AudioVideo;
"
	echo "$content" | sudo tee "$desktop" >/dev/null
	echo "Wrote $desktop"
}

echo "==> Installing GStreamer + screen-share dependencies"
install_gstreamer_deps

echo "==> Checking LiveKit (rswebrtc) GStreamer plugin"
ensure_livekit_plugin || true

echo "==> Installing binaries to $BIN_DEST"
install_binaries

echo "==> Writing desktop entry"
write_desktop

echo
echo "Done. Launch 'mumble-forked' from your app menu, or run: $BIN_DEST/mumble"
echo "The helper (mumble-screen-helper) is auto-started by the client when you share."
