// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "UpdateHealth.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cwctype>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#	include <windows.h>
#endif

#include <nlohmann/json.hpp>

namespace Mumble::UpdateHealth {
namespace {

	using json = nlohmann::json;

	void setError(std::string *error, const std::string &message) {
		if (error) {
			*error = message;
		}
	}

	std::wstring normalizedPath(const std::filesystem::path &path) {
		std::error_code error;
		std::filesystem::path absolute = std::filesystem::absolute(path, error);
		if (error) {
			absolute = path;
		}
		std::wstring value = absolute.lexically_normal().wstring();
		if (value.rfind(L"\\\\?\\UNC\\", 0) == 0) {
			value = L"\\\\" + value.substr(8);
		} else if (value.rfind(L"\\\\?\\", 0) == 0) {
			value.erase(0, 4);
		}
		std::transform(value.begin(), value.end(), value.begin(),
					   [](const wchar_t ch) { return static_cast< wchar_t >(std::towlower(ch)); });
		return value;
	}

	bool samePath(const std::filesystem::path &left, const std::filesystem::path &right) {
		return normalizedPath(left) == normalizedPath(right);
	}

	bool safeIdentity(const std::string &identity) {
		return !identity.empty() && identity.size() <= 160
			   && std::all_of(identity.begin(), identity.end(), [](const unsigned char ch) {
					  return std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.';
				  });
	}

	bool validBootSessionIdentity(const std::string &identity) {
		return identity.size() == 32
			   && std::all_of(identity.begin(), identity.end(), [](const unsigned char ch) {
					  return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
				  });
	}

	bool validSha256(const std::string &value) {
		return value.size() == 64 && std::all_of(value.begin(), value.end(), [](const unsigned char ch) {
				   return std::isxdigit(ch) != 0;
			   });
	}

	bool validTransactionId(const std::string &value) {
		return value.size() == 32 && std::all_of(value.begin(), value.end(), [](const unsigned char ch) {
			return std::isxdigit(ch) != 0;
		});
	}

	const char *stateName(const TransactionState state) {
		switch (state) {
			case TransactionState::RollbackArmed:
				return "rollback-armed";
			case TransactionState::AwaitingHealth:
				return "awaiting-health";
			case TransactionState::Committed:
				return "committed";
			case TransactionState::RolledBack:
				return "rolled-back";
		}
		return "invalid";
	}

	std::optional< TransactionState > parseState(const std::string &value) {
		if (value == "rollback-armed") {
			return TransactionState::RollbackArmed;
		}
		if (value == "awaiting-health") {
			return TransactionState::AwaitingHealth;
		}
		if (value == "committed") {
			return TransactionState::Committed;
		}
		if (value == "rolled-back") {
			return TransactionState::RolledBack;
		}
		return std::nullopt;
	}

	const char *modeName(const TransactionMode mode) {
		return mode == TransactionMode::WindowsInstaller ? "windows-installer" : "native-package";
	}

	std::optional< TransactionMode > parseMode(const std::string &value) {
		if (value == "native-package") {
			return TransactionMode::NativePackage;
		}
		if (value == "windows-installer") {
			return TransactionMode::WindowsInstaller;
		}
		return std::nullopt;
	}

	bool safeRelativePath(const std::string &value) {
		if (value.empty()) {
			return false;
		}
		std::string normalized = value;
		std::replace(normalized.begin(), normalized.end(), '\\', '/');
		const std::filesystem::path path(normalized);
		if (path.is_absolute() || path.has_root_path()) {
			return false;
		}
		for (const auto &component : path) {
			if (component == "..") {
				return false;
			}
		}
		return true;
	}

	std::string utf8Path(const std::filesystem::path &path) {
		const std::u8string value = path.u8string();
		return std::string(reinterpret_cast< const char * >(value.data()), value.size());
	}

	std::filesystem::path pathFromUtf8(const std::string &value) {
		const auto *begin = reinterpret_cast< const char8_t * >(value.data());
		return std::filesystem::path(std::u8string(begin, begin + value.size()));
	}

	std::int64_t unixMillisecondsNow() {
		return std::chrono::duration_cast< std::chrono::milliseconds >(
				   std::chrono::system_clock::now().time_since_epoch())
			.count();
	}

	bool writeJsonAtomic(const std::filesystem::path &path, const json &document, std::string *error) {
		std::error_code filesystemError;
		std::filesystem::create_directories(path.parent_path(), filesystemError);
		if (filesystemError) {
			setError(error, "Unable to create update-health directory.");
			return false;
		}

		std::filesystem::path temporary = path;
		temporary += ".tmp";
		const std::string contents = document.dump(2) + '\n';
#ifdef _WIN32
		HANDLE file = CreateFileW(temporary.wstring().c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
								  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
		if (file == INVALID_HANDLE_VALUE) {
			setError(error, "Unable to write update-health state.");
			return false;
		}
		std::size_t offset = 0;
		bool written       = true;
		while (offset < contents.size()) {
			const DWORD chunk = static_cast< DWORD >(
				std::min< std::size_t >(contents.size() - offset, std::numeric_limits< DWORD >::max()));
			DWORD completed = 0;
			if (!WriteFile(file, contents.data() + offset, chunk, &completed, nullptr) || completed != chunk) {
				written = false;
				break;
			}
			offset += completed;
		}
		const bool flushed = written && FlushFileBuffers(file);
		CloseHandle(file);
		if (!flushed) {
			std::filesystem::remove(temporary, filesystemError);
			setError(error, "Unable to durably finish update-health state.");
			return false;
		}
		if (!MoveFileExW(temporary.wstring().c_str(), path.wstring().c_str(),
						 MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
			std::filesystem::remove(temporary, filesystemError);
			setError(error, "Unable to durably commit update-health state.");
			return false;
		}
#else
		{
			std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
			if (!stream) {
				setError(error, "Unable to write update-health state.");
				return false;
			}
			stream << contents;
			if (!stream) {
				setError(error, "Unable to finish update-health state.");
				return false;
			}
		}

		std::filesystem::rename(temporary, path, filesystemError);
		if (filesystemError) {
			std::filesystem::remove(path, filesystemError);
			filesystemError.clear();
			std::filesystem::rename(temporary, path, filesystemError);
		}
		if (filesystemError) {
			std::filesystem::remove(temporary, filesystemError);
			setError(error, "Unable to commit update-health state.");
			return false;
		}
#endif
		return true;
	}

	std::optional< PendingUpdate > parsePending(const json &document, std::string *error) {
		if (document.value("schema", 0u) != SchemaVersion
			|| document.value("updaterProtocolVersion", 0u) != UpdaterProtocolVersion) {
			setError(error, "Unsupported update-health pending state.");
			return std::nullopt;
		}

		PendingUpdate pending;
		pending.updaterProtocolVersion          = document.value("updaterProtocolVersion", 0u);
		const auto parsedMode                   = parseMode(document.value("mode", ""));
		pending.transactionId                  = document.value("transactionId", "");
		const auto parsedState                 = parseState(document.value("state", ""));
		pending.packageIdentity                  = document.value("packageIdentity", "");
		pending.previousPackageIdentity          = document.value("previousPackageIdentity", "");
		pending.expectedExecutableSha256         = document.value("expectedExecutableSha256", "");
		pending.appPath                          = pathFromUtf8(document.value("appPath", ""));
		pending.backupRoot                       = pathFromUtf8(document.value("backupRoot", ""));
		const json recoveryInstaller             = document.value("recoveryInstaller", json::object());
		pending.recoveryInstallerPath            = pathFromUtf8(recoveryInstaller.value("path", ""));
		pending.recoveryInstallerSize = recoveryInstaller.value("size", static_cast< std::uint64_t >(0));
		pending.recoveryInstallerSha256          = recoveryInstaller.value("sha256", "");
		pending.bootSessionIdentity              = document.value("bootSessionIdentity", "");
		pending.restartRequired                  = document.value("restartRequired", false);
		pending.minimumStableRuntimeMilliseconds = std::max< std::uint64_t >(
			MinimumStableRuntimeMilliseconds,
			document.value("minimumStableRuntimeMilliseconds", MinimumStableRuntimeMilliseconds));
		pending.healthTimeoutMilliseconds =
			std::max(pending.minimumStableRuntimeMilliseconds,
					 document.value("healthTimeoutMilliseconds", DefaultHealthTimeoutMilliseconds));

		const json previousManifest              = document.value("previousInstalledManifest", json::object());
		pending.previousInstalledManifestExisted = previousManifest.value("existed", false);
		pending.previousInstalledManifestSize    = previousManifest.value("size", static_cast< std::uint64_t >(0));
		pending.previousInstalledManifestSha256  = previousManifest.value("sha256", "");

		if (!validTransactionId(pending.transactionId) || !parsedState || !parsedMode
			|| !safeIdentity(pending.packageIdentity) || !validSha256(pending.expectedExecutableSha256)
			|| pending.appPath.empty() || pending.backupRoot.empty()) {
			setError(error, "Invalid update-health pending identity or path.");
			return std::nullopt;
		}
		pending.state = *parsedState;
		pending.mode  = *parsedMode;
		if (pending.mode == TransactionMode::WindowsInstaller
			&& (pending.recoveryInstallerPath.empty() || pending.recoveryInstallerSize == 0
				|| !validSha256(pending.recoveryInstallerSha256)
				|| !validBootSessionIdentity(pending.bootSessionIdentity)
				|| !samePath(pending.recoveryInstallerPath.parent_path(), pending.backupRoot))) {
			setError(error, "Invalid Windows Installer recovery artifact.");
			return std::nullopt;
		}
		if (pending.previousInstalledManifestExisted && !validSha256(pending.previousInstalledManifestSha256)) {
			setError(error, "Invalid previous installed-manifest checksum.");
			return std::nullopt;
		}

		for (const json &entry : document.value("rollbackFiles", json::array())) {
			RollbackFile file;
			file.path    = entry.value("path", "");
			file.existed = entry.value("existed", false);
			file.size    = entry.value("size", static_cast< std::uint64_t >(0));
			file.sha256  = entry.value("sha256", "");
			if (!safeRelativePath(file.path) || (file.existed && !validSha256(file.sha256))) {
				setError(error, "Invalid rollback file entry.");
				return std::nullopt;
			}
			pending.rollbackFiles.push_back(std::move(file));
		}
		return pending;
	}

} // namespace

std::string installationKey(const std::filesystem::path &appDirectory) {
	const std::wstring text = normalizedPath(appDirectory);
	std::uint64_t hash      = 14695981039346656037ull;
	for (const wchar_t ch : text) {
		hash ^= static_cast< std::uint64_t >(ch);
		hash *= 1099511628211ull;
	}
	std::ostringstream stream;
	stream << std::hex << std::setw(16) << std::setfill('0') << hash;
	return stream.str();
}

std::filesystem::path pendingStatePath(const std::filesystem::path &updateRoot, const std::filesystem::path &appPath) {
	return updateRoot / "pending-health" / (installationKey(appPath.parent_path()) + ".json");
}

std::filesystem::path healthMarkerPath(const std::filesystem::path &updateRoot, const PendingUpdate &pending) {
	return updateRoot / "health-markers"
		   / (installationKey(pending.appPath.parent_path()) + "-" + pending.packageIdentity + "-"
			  + pending.transactionId + ".json");
}

bool writePendingState(const std::filesystem::path &updateRoot, const PendingUpdate &pending, std::string *error) {
	if (pending.updaterProtocolVersion != UpdaterProtocolVersion || !validTransactionId(pending.transactionId)
		|| !safeIdentity(pending.packageIdentity) || !validSha256(pending.expectedExecutableSha256)
		|| pending.appPath.empty()
		|| pending.backupRoot.empty()) {
		setError(error, "Refusing to write invalid update-health pending state.");
		return false;
	}
	if (pending.mode == TransactionMode::WindowsInstaller
		&& (pending.recoveryInstallerPath.empty() || pending.recoveryInstallerSize == 0
			|| !validSha256(pending.recoveryInstallerSha256)
			|| !validBootSessionIdentity(pending.bootSessionIdentity)
			|| !samePath(pending.recoveryInstallerPath.parent_path(), pending.backupRoot))) {
		setError(error, "Refusing invalid Windows Installer recovery state.");
		return false;
	}

	json rollbackFiles = json::array();
	for (const RollbackFile &file : pending.rollbackFiles) {
		if (!safeRelativePath(file.path) || (file.existed && !validSha256(file.sha256))) {
			setError(error, "Refusing to write invalid rollback file entry.");
			return false;
		}
		rollbackFiles.push_back(json{
			{ "path", file.path },
			{ "existed", file.existed },
			{ "size", file.size },
			{ "sha256", file.sha256 },
		});
	}

	const json document{
		{ "schema", SchemaVersion },
		{ "updaterProtocolVersion", pending.updaterProtocolVersion },
		{ "state", stateName(pending.state) },
		{ "mode", modeName(pending.mode) },
		{ "transactionId", pending.transactionId },
		{ "packageIdentity", pending.packageIdentity },
		{ "previousPackageIdentity", pending.previousPackageIdentity },
		{ "expectedExecutableSha256", pending.expectedExecutableSha256 },
		{ "appPath", utf8Path(pending.appPath) },
		{ "backupRoot", utf8Path(pending.backupRoot) },
		{ "recoveryInstaller", json{ { "path", utf8Path(pending.recoveryInstallerPath) },
										{ "size", pending.recoveryInstallerSize },
										{ "sha256", pending.recoveryInstallerSha256 } } },
		{ "bootSessionIdentity", pending.bootSessionIdentity },
		{ "restartRequired", pending.restartRequired },
		{ "createdAtUnixMilliseconds", unixMillisecondsNow() },
		{ "minimumStableRuntimeMilliseconds",
		  std::max(MinimumStableRuntimeMilliseconds, pending.minimumStableRuntimeMilliseconds) },
		{ "healthTimeoutMilliseconds",
		  std::max(std::max(MinimumStableRuntimeMilliseconds, pending.minimumStableRuntimeMilliseconds),
				   pending.healthTimeoutMilliseconds) },
		{ "previousInstalledManifest", json{ { "existed", pending.previousInstalledManifestExisted },
											 { "size", pending.previousInstalledManifestSize },
											 { "sha256", pending.previousInstalledManifestSha256 } } },
		{ "rollbackFiles", rollbackFiles },
	};
	// Health markers are transaction-ID scoped, so a marker from an older
	// attempt can never qualify this journal. In particular, do not delete the
	// current marker here: terminal-state persistence must complete before any
	// cleanup, otherwise a power loss could leave an awaiting-health journal
	// whose already-qualified marker has disappeared.
	return writeJsonAtomic(pendingStatePath(updateRoot, pending.appPath), document, error);
}

std::optional< PendingUpdate > readPendingState(const std::filesystem::path &updateRoot,
												const std::filesystem::path &appPath, std::string *error) {
	const std::filesystem::path path = pendingStatePath(updateRoot, appPath);
	std::ifstream stream(path, std::ios::binary);
	if (!stream) {
		return std::nullopt;
	}
	try {
		auto pending = parsePending(json::parse(stream), error);
		if (pending && !samePath(pending->appPath, appPath)) {
			setError(error, "Pending update belongs to a different application path.");
			return std::nullopt;
		}
		return pending;
	} catch (const std::exception &exception) {
		setError(error, std::string("Unable to parse update-health pending state: ") + exception.what());
		return std::nullopt;
	}
}

bool writeHealthMarker(const std::filesystem::path &updateRoot, const std::filesystem::path &appPath,
					   const std::uint64_t stableRuntimeMilliseconds, const bool settingsLoaded,
					   const bool audioInitialized, const std::string &runningExecutableSha256,
					   std::string *error) {
	auto pending = readPendingState(updateRoot, appPath, error);
	if (!pending) {
		setError(error, "No valid pending update-health state exists.");
		return false;
	}
	if (pending->state != TransactionState::AwaitingHealth) {
		setError(error, "Pending update is not awaiting a health marker.");
		return false;
	}
	if (pending->restartRequired) {
		setError(error, "A reboot-required update cannot publish a health marker before recovery completes.");
		return false;
	}
	if (!validSha256(runningExecutableSha256)
		|| runningExecutableSha256 != pending->expectedExecutableSha256) {
		setError(error, "The running executable does not match the candidate executable SHA256.");
		return false;
	}
	if (!settingsLoaded || !audioInitialized
		|| stableRuntimeMilliseconds
			   < std::max(MinimumStableRuntimeMilliseconds, pending->minimumStableRuntimeMilliseconds)) {
		setError(error, "Update-health startup gates are not satisfied.");
		return false;
	}

	const json marker{
		{ "schema", SchemaVersion },
		{ "updaterProtocolVersion", UpdaterProtocolVersion },
		{ "state", "healthy" },
		{ "transactionId", pending->transactionId },
		{ "packageIdentity", pending->packageIdentity },
		{ "executableSha256", runningExecutableSha256 },
		{ "appPath", utf8Path(pending->appPath) },
		{ "settingsLoaded", true },
		{ "audioInitialized", true },
		{ "stableRuntimeMilliseconds", stableRuntimeMilliseconds },
		{ "markedAtUnixMilliseconds", unixMillisecondsNow() },
	};
	return writeJsonAtomic(healthMarkerPath(updateRoot, *pending), marker, error);
}

bool markerConfirmsHealthy(const std::filesystem::path &updateRoot, const PendingUpdate &pending, std::string *error) {
	if (pending.state != TransactionState::AwaitingHealth || pending.restartRequired
		|| !validSha256(pending.expectedExecutableSha256)) {
		return false;
	}
	std::ifstream stream(healthMarkerPath(updateRoot, pending), std::ios::binary);
	if (!stream) {
		return false;
	}
	try {
		const json marker                         = json::parse(stream);
		const std::filesystem::path markerAppPath = pathFromUtf8(marker.value("appPath", ""));
		const std::uint64_t stableRuntime = marker.value("stableRuntimeMilliseconds", static_cast< std::uint64_t >(0));
		return marker.value("schema", 0u) == SchemaVersion
			   && marker.value("updaterProtocolVersion", 0u) == UpdaterProtocolVersion
			   && marker.value("state", "") == "healthy"
			   && marker.value("transactionId", "") == pending.transactionId
			   && marker.value("packageIdentity", "") == pending.packageIdentity
			   && marker.value("executableSha256", "") == pending.expectedExecutableSha256
			   && samePath(markerAppPath, pending.appPath) && marker.value("settingsLoaded", false)
			   && marker.value("audioInitialized", false)
			   && stableRuntime >= std::max(MinimumStableRuntimeMilliseconds, pending.minimumStableRuntimeMilliseconds);
	} catch (const std::exception &exception) {
		setError(error, std::string("Unable to parse update-health marker: ") + exception.what());
		return false;
	}
}

bool removePendingState(const std::filesystem::path &updateRoot, const std::filesystem::path &appPath,
						std::string *error) {
	std::error_code filesystemError;
	const std::filesystem::path path = pendingStatePath(updateRoot, appPath);
	if (!std::filesystem::exists(path, filesystemError)) {
		return !filesystemError;
	}
	if (!std::filesystem::remove(path, filesystemError) || filesystemError) {
		setError(error, "Unable to remove update-health pending state.");
		return false;
	}
	return true;
}

} // namespace Mumble::UpdateHealth
