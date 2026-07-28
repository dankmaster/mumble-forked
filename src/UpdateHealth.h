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

constexpr std::uint32_t SchemaVersion          = 3;
constexpr std::uint32_t UpdaterProtocolVersion = 4;
constexpr std::uint32_t InstallerFallbackRequestSchemaVersion = 1;
constexpr std::uint64_t MinimumStableRuntimeMilliseconds = 10'000;
constexpr std::uint64_t DefaultHealthTimeoutMilliseconds = 45'000;

enum class TransactionState : std::uint8_t { RollbackArmed, AwaitingHealth, Committed, RolledBack };
enum class TransactionMode : std::uint8_t { NativePackage, WindowsInstaller };

struct RollbackFile {
	std::string path;
	bool existed       = false;
	std::uint64_t size = 0;
	std::string sha256;
};

struct PendingUpdate {
	std::uint32_t updaterProtocolVersion = UpdaterProtocolVersion;
	TransactionMode mode                 = TransactionMode::NativePackage;
	std::string transactionId;
	TransactionState state = TransactionState::RollbackArmed;
	std::string packageIdentity;
	std::string previousPackageIdentity;
	/// SHA-256 of the exact mumble executable that is allowed to publish the
	/// health marker. This is independent of the package/MSI identity.
	std::string expectedExecutableSha256;
	std::filesystem::path appPath;
	std::filesystem::path backupRoot;
	std::filesystem::path recoveryInstallerPath;
	std::uint64_t recoveryInstallerSize = 0;
	std::string recoveryInstallerSha256;
	/// Windows boot-session identity in which a reboot-required MSI transaction
	/// was armed. Recovery may only become terminal after this identity changes.
	std::string bootSessionIdentity;
	bool restartRequired = false;
	bool previousInstalledManifestExisted       = false;
	std::uint64_t previousInstalledManifestSize = 0;
	std::string previousInstalledManifestSha256;
	std::vector< RollbackFile > rollbackFiles;
	std::uint64_t minimumStableRuntimeMilliseconds = MinimumStableRuntimeMilliseconds;
	std::uint64_t healthTimeoutMilliseconds        = DefaultHealthTimeoutMilliseconds;
};

struct InstallerFallbackRequest {
	std::string packageIdentity;
	std::uint32_t updateExitCode = 0;
};

/// Returns a stable, case-insensitive key for an installation directory.
std::string installationKey(const std::filesystem::path &appDirectory);

std::filesystem::path pendingStatePath(const std::filesystem::path &updateRoot, const std::filesystem::path &appPath);
std::filesystem::path healthMarkerPath(const std::filesystem::path &updateRoot, const PendingUpdate &pending);
std::filesystem::path installerFallbackRequestPath(const std::filesystem::path &updateRoot,
												   const std::filesystem::path &appPath);

bool writePendingState(const std::filesystem::path &updateRoot, const PendingUpdate &pending,
					   std::string *error = nullptr);
std::optional< PendingUpdate > readPendingState(const std::filesystem::path &updateRoot,
												const std::filesystem::path &appPath, std::string *error = nullptr);

/// Writes the health marker only when the pending payload matches appPath and all
/// startup gates have remained healthy for at least ten seconds.
bool writeHealthMarker(const std::filesystem::path &updateRoot, const std::filesystem::path &appPath,
					   std::uint64_t stableRuntimeMilliseconds, bool settingsLoaded, bool audioInitialized,
					   const std::string &runningExecutableSha256,
					   std::string *error = nullptr);

bool markerConfirmsHealthy(const std::filesystem::path &updateRoot, const PendingUpdate &pending,
						   std::string *error = nullptr);

bool removePendingState(const std::filesystem::path &updateRoot, const std::filesystem::path &appPath,
						std::string *error = nullptr);

/// Persists a same-installation request to use the verified MSI on the next
/// update attempt. This is written only after a native package failure has no
/// unresolved rollback journal.
bool writeInstallerFallbackRequest(const std::filesystem::path &updateRoot, const std::filesystem::path &appPath,
								   const std::string &packageIdentity, std::uint32_t updateExitCode,
								   std::string *error = nullptr);
std::optional< InstallerFallbackRequest >
	readInstallerFallbackRequest(const std::filesystem::path &updateRoot, const std::filesystem::path &appPath,
								 std::string *error = nullptr);
bool removeInstallerFallbackRequest(const std::filesystem::path &updateRoot, const std::filesystem::path &appPath,
									std::string *error = nullptr);

} // namespace Mumble::UpdateHealth
