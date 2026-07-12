#!/usr/bin/env bash

set -Eeuo pipefail
set -x

source "$( dirname "$0" )/common.sh"

verify_required_env_variables_set

# Qt is intentionally not installed here. The desktop-client workflow installs
# the pinned official Qt distribution with aqtinstall and puts that prefix first.
brew install \
	ninja \
	pkg-config \
	boost \
	openssl@3 \
	protobuf \
	poco \
	libogg \
	libsndfile \
	opus

echo "OPENSSL_ROOT_DIR=$( brew --prefix openssl@3 )" >> "$GITHUB_ENV"
