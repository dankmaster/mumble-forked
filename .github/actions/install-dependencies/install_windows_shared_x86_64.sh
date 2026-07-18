#!/usr/bin/env bash

set -e
set -x

source "$( dirname "$0" )/common.sh"

verify_required_env_variables_set

shared_environment_has_webengine_required_features() {
	local webengine_targets_file="$1"

	python - "$webengine_targets_file" <<'PY'
import re
import sys
from pathlib import Path

targets_file = Path(sys.argv[1])
text = targets_file.read_text(encoding="utf-8")

enabled_private_match = re.search(r'QT_ENABLED_PRIVATE_FEATURES\s+"([^"]*)"', text)
disabled_private_match = re.search(r'QT_DISABLED_PRIVATE_FEATURES\s+"([^"]*)"', text)

if (
    enabled_private_match is None
    or disabled_private_match is None
):
    raise SystemExit(1)

enabled_private_features = {feature for feature in enabled_private_match.group(1).split(";") if feature}
disabled_private_features = {feature for feature in disabled_private_match.group(1).split(";") if feature}

has_proprietary_codecs = (
    "webengine_proprietary_codecs" in enabled_private_features
    and "webengine_proprietary_codecs" not in disabled_private_features
)

raise SystemExit(0 if has_proprietary_codecs else 1)
PY
}

shared_environment_has_webengine_runtime() {
	local triplet_dir="$MUMBLE_ENVIRONMENT_DIR/installed/x64-windows"
	local webengine_targets_file="$triplet_dir/share/Qt6WebEngineCore/Qt6WebEngineCoreTargets.cmake"
	local multimedia_targets_file="$triplet_dir/share/Qt6Multimedia/Qt6MultimediaTargets.cmake"
	local multimedia_qml_plugin="$triplet_dir/Qt6/qml/QtMultimedia/quickmultimediaplugin.dll"

	[[ -f "$MUMBLE_ENVIRONMENT_DIR/vcpkg.exe" ]] \
		&& [[ -f "$MUMBLE_ENVIRONMENT_DIR/scripts/buildsystems/vcpkg.cmake" ]] \
		&& [[ -f "$webengine_targets_file" ]] \
		&& shared_environment_has_webengine_required_features "$webengine_targets_file" \
		&& [[ -f "$multimedia_targets_file" ]] \
		&& [[ -f "$multimedia_qml_plugin" ]] \
		&& [[ -f "$triplet_dir/tools/Qt6/bin/windeployqt.exe" ]] \
		&& [[ -f "$triplet_dir/share/Qt6/resources/icudtl.dat" ]] \
		&& [[ -f "$triplet_dir/share/Qt6/resources/qtwebengine_resources.pak" ]]
}

require_shared_environment_bootstrap_allowed() {
	local missing_reason="$1"

	if [[ "${MUMBLE_ALLOW_ENVIRONMENT_BOOTSTRAP:-}" = "ON" ]]; then
		return 0
	fi

	echo "$missing_reason" 1>&2
	echo "Local shared environment bootstrap is disabled for this job." 1>&2
	echo "Publish ${MUMBLE_ENVIRONMENT_VERSION}.7z to ${MUMBLE_ENVIRONMENT_SOURCE}, or restore a cache with the same key." 1>&2
	exit 1
}

if have_archive_extractor; then
	archive_url="$MUMBLE_ENVIRONMENT_SOURCE/$MUMBLE_ENVIRONMENT_VERSION.7z"
	split_archive_url="$archive_url.001"
	if remote_file_exists "$archive_url" || remote_file_exists "$split_archive_url"; then
		make_build_env_available "7z"
	fi
fi

if ! shared_environment_has_webengine_runtime; then
	require_shared_environment_bootstrap_allowed "Shared Windows WebEngine environment is missing required WebEngine/proprietary-codec runtime content."
	ensure_build_env_repo_checkout
	ensure_vcpkg_bootstrapped
	install_mumble_vcpkg_dependencies "x64-windows"
fi

if ! environment_has_triplet "x86-windows"; then
	require_shared_environment_bootstrap_allowed "Shared Windows WebEngine environment is missing the x86-windows helper triplet."
	ensure_build_env_repo_checkout
	ensure_vcpkg_bootstrapped
	"$MUMBLE_ENVIRONMENT_DIR/vcpkg.exe" install --triplet "x86-windows" boost
fi

rm -rf "C:/WixSharp"
download_file "https://github.com/oleg-shilo/wixsharp/releases/download/v1.19.0.0/WixSharp.1.19.0.0.7z" "WixSharp.7z"
extract_with_progress "WixSharp.7z" "C:/WixSharp"

if [[ ! -d "C:/vcvars-bash/.git" ]]; then
	rm -rf "C:/vcvars-bash"
	git_clone_with_retry "https://github.com/nathan818fr/vcvars-bash.git" "C:/vcvars-bash"
fi

if [[ "${MUMBLE_SKIP_DATABASE_SETUP:-}" = "ON" ]]; then
	echo "Skipping local database setup for Windows dependencies"
	exit 0
fi

echo -e "[mysqld]\nlog-bin-trust-function-creators = 1" >> "C:/Windows/my.ini"

mysqld --initialize-insecure --console

powershell -Command "Start-Process mysqld"

sleep 5

configure_database_tables "mysql"
