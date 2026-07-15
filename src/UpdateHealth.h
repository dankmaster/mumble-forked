// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Mumble::UpdateHealth {

constexpr std::uint64_t MinimumStableRuntimeMilliseconds = 10'000;
constexpr std::uint64_t DefaultHealthTimeoutMilliseconds = 45'000;

enum class TransactionState : std::uint8_t { RollbackArmed, AwaitingHealth, Committed, RolledBack };

struct RollbackFile {
	std::string path;
	bool existed       = false;
	std::uint64_t size = 0;
	std::string sha256;
};

struct PendingUpdate {
	std::string transactionId;
	TransactionState state = TransactionState::RollbackArmed;
	std::string packageIdentity;
	std::string previousPackageIdentity;
	std::filesystem::path appPath;
	std::filesystem::path backupRoot;
	bool previousInstalledManifestExisted       = false;
	std::uint64_t previousInstalledManifestSize = 0;
	std::string previousInstalledManifestSha256;
	std::vector< RollbackFile > rollbackFiles;
	std::uint64_t minimumStableRuntimeMilliseconds = MinimumStableRuntimeMilliseconds;
	std::uint64_t healthTimeoutMilliseconds        = DefaultHealthTimeoutMilliseconds;
};

/// Returns a stable, case-insensitive key for an installation directory.
std::string installationKey(const std::filesystem::path &appDirectory);

std::filesystem::path pendingStatePath(const std::filesystem::path &updateRoot, const std::filesystem::path &appPath);
std::filesystem::path healthMarkerPath(const std::filesystem::path &updateRoot, const PendingUpdate &pending);

bool writePendingState(const std::filesystem::path &updateRoot, const PendingUpdate &pending,
					   std::string *error = nullptr);
std::optional< PendingUpdate > readPendingState(const std::filesystem::path &updateRoot,
												const std::filesystem::path &appPath, std::string *error = nullptr);

/// Writes the health marker only when the pending payload matches appPath and all
/// startup gates have remained healthy for at least ten seconds.
bool writeHealthMarker(const std::filesystem::path &updateRoot, const std::filesystem::path &appPath,
					   std::uint64_t stableRuntimeMilliseconds, bool settingsLoaded, bool audioInitialized,
					   std::string *error = nullptr);

bool markerConfirmsHealthy(const std::filesystem::path &updateRoot, const PendingUpdate &pending,
						   std::string *error = nullptr);

bool removePendingState(const std::filesystem::path &updateRoot, const std::filesystem::path &appPath,
						std::string *error = nullptr);

} // namespace Mumble::UpdateHealth
