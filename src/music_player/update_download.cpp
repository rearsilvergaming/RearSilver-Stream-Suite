#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <winhttp.h>

#include "update_download.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <thread>
#include <vector>

namespace {
using WinHttpHandle = std::unique_ptr<void, decltype(&WinHttpCloseHandle)>;

std::wstring wide(const std::string &value)
{
	if (value.empty()) return {};
	const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), int(value.size()), nullptr, 0);
	if (count <= 0) return {};
	std::wstring output(count, L'\0');
	MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), int(value.size()), output.data(), count);
	return output;
}

std::wstring updateFolder()
{
	wchar_t localAppData[32768]{};
	const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, DWORD(std::size(localAppData)));
	if (!length || length >= std::size(localAppData)) return {};
	return std::wstring(localAppData, length) + L"\\RearSilver Stream Suite\\Updates";
}

std::string sha256(const std::wstring &path)
{
	BCRYPT_ALG_HANDLE algorithm = nullptr;
	BCRYPT_HASH_HANDLE hash = nullptr;
	DWORD objectBytes = 0, hashBytes = 0, returned = 0;
	std::vector<unsigned char> object, digest;
	if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return {};
	auto closeAlgorithm = [&] { BCryptCloseAlgorithmProvider(algorithm, 0); };
	if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectBytes), sizeof(objectBytes), &returned, 0) < 0 ||
		BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashBytes), sizeof(hashBytes), &returned, 0) < 0) {
		closeAlgorithm(); return {};
	}
	object.resize(objectBytes); digest.resize(hashBytes);
	if (BCryptCreateHash(algorithm, &hash, object.data(), DWORD(object.size()), nullptr, 0, 0) < 0) {
		closeAlgorithm(); return {};
	}
	std::ifstream input(std::filesystem::path(path), std::ios::binary);
	// Keep the hashing workspace off the Windows worker-thread stack. A 1 MiB
	// local array can exhaust the thread's default stack immediately after a
	// download reaches 100%.
	std::vector<unsigned char> buffer(1024 * 1024);
	while (input) {
		input.read(reinterpret_cast<char *>(buffer.data()), buffer.size());
		const std::streamsize count = input.gcount();
		if (count > 0 && BCryptHashData(hash, buffer.data(), ULONG(count), 0) < 0) {
			BCryptDestroyHash(hash); closeAlgorithm(); return {};
		}
	}
	if (input.bad() || BCryptFinishHash(hash, digest.data(), DWORD(digest.size()), 0) < 0) {
		BCryptDestroyHash(hash); closeAlgorithm(); return {};
	}
	BCryptDestroyHash(hash); closeAlgorithm();
	std::ostringstream text;
	text << std::hex << std::setfill('0');
	for (const unsigned char byte : digest) text << std::setw(2) << unsigned(byte);
	return text.str();
}

void report(const UpdateDownloadCallback &callback, UpdateDownloadStatus status, const std::string &message,
	std::uint64_t transferred = 0, std::uint64_t total = 0, const std::wstring &path = {})
{
	callback(UpdateDownloadProgress{status, transferred, total, path, message});
}

bool downloadFile(const std::string &url, const std::string &bearerToken, const std::wstring &partial,
	std::uint64_t expectedSize, std::atomic<bool> &cancelRequested, const UpdateDownloadCallback &callback,
	std::string &error)
{
	const std::wstring address = wide(url);
	URL_COMPONENTS parts{}; parts.dwStructSize = sizeof(parts);
	parts.dwHostNameLength = DWORD(-1); parts.dwUrlPathLength = DWORD(-1); parts.dwExtraInfoLength = DWORD(-1);
	if (address.empty() || !WinHttpCrackUrl(address.c_str(), DWORD(address.size()), 0, &parts) ||
		parts.nScheme != INTERNET_SCHEME_HTTPS || parts.dwExtraInfoLength) {
		error = "The update download address is invalid."; return false;
	}
	const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
	const std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
	std::error_code fileError;
	std::uint64_t offset = std::filesystem::exists(partial, fileError) ? std::filesystem::file_size(partial, fileError) : 0;
	if (fileError || offset > expectedSize) { std::filesystem::remove(partial, fileError); offset = 0; }
	if (offset == expectedSize) return true;

	WinHttpHandle session(WinHttpOpen(L"RearSilver-Stream-Suite-Update-Download/1.0",
		WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0), WinHttpCloseHandle);
	if (!session) { error = "Windows could not initialise the update download."; return false; }
	WinHttpSetTimeouts(session.get(), 5000, 5000, 15000, 30000);
	WinHttpHandle connection(WinHttpConnect(session.get(), host.c_str(), parts.nPort, 0), WinHttpCloseHandle);
	WinHttpHandle request(connection ? WinHttpOpenRequest(connection.get(), L"GET", path.c_str(), nullptr,
		WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr, WinHttpCloseHandle);
	if (!connection || !request) { error = "The update service could not be reached."; return false; }
	std::wstring headers = L"Authorization: Bearer " + wide(bearerToken) + L"\r\n";
	if (offset) headers += L"Range: bytes=" + std::to_wstring(offset) + L"-\r\n";
	if (!WinHttpSendRequest(request.get(), headers.c_str(), DWORD(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
		!WinHttpReceiveResponse(request.get(), nullptr)) {
		error = "The update service did not respond."; return false;
	}
	DWORD status = 0, bytes = sizeof(status);
	WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
		WINHTTP_HEADER_NAME_BY_INDEX, &status, &bytes, WINHTTP_NO_HEADER_INDEX);
	if ((offset && status != 206) || (!offset && status != 200 && status != 206)) {
		error = status == 401 ? "The update download was not authorised." : "The update service rejected the download.";
		return false;
	}
	std::ofstream output(std::filesystem::path(partial), std::ios::binary | (offset ? std::ios::app : std::ios::trunc));
	if (!output) { error = "The partial update file could not be opened."; return false; }
	std::array<char, 256 * 1024> buffer{};
	std::uint64_t transferred = offset;
	for (;;) {
		if (cancelRequested.load()) { output.close(); std::filesystem::remove(partial, fileError); error = "cancelled"; return false; }
		DWORD read = 0;
		if (!WinHttpReadData(request.get(), buffer.data(), DWORD(buffer.size()), &read)) {
			error = "The update download was interrupted. Retry to resume it."; return false;
		}
		if (!read) break;
		output.write(buffer.data(), read);
		if (!output) { error = "The update could not be written to disk."; return false; }
		transferred += read;
		if (transferred > expectedSize) { error = "The update download exceeded its expected size."; return false; }
		report(callback, UpdateDownloadStatus::Downloading, "Downloading update…", transferred, expectedSize);
	}
	output.close();
	if (transferred != expectedSize) { error = "The update download ended before it was complete. Retry to resume it."; return false; }
	return true;
}
}

void downloadAndVerifySuiteUpdate(const UpdateCheckResult &update, const std::string &bearerToken,
	std::atomic<bool> &cancelRequested, const UpdateDownloadCallback &callback)
{
	if (!update.downloadAvailable || bearerToken.empty()) {
		report(callback, UpdateDownloadStatus::Error, "The authenticated update download is not configured."); return;
	}
	const std::wstring folder = updateFolder();
	const std::wstring filename = wide(update.installerFilename);
	const std::wstring remote = wide(update.downloadRequestUrl);
	if (folder.empty() || filename.empty() || remote.empty()) {
		report(callback, UpdateDownloadStatus::Error, "The update download details are invalid."); return;
	}
	std::error_code filesystemError;
	std::filesystem::create_directories(folder, filesystemError);
	if (filesystemError) { report(callback, UpdateDownloadStatus::Error, "The update folder could not be created."); return; }
	const std::wstring partial = folder + L"\\" + filename + L".partial";
	const std::wstring verified = folder + L"\\" + filename;
	std::string downloadError;
	if (!downloadFile(update.downloadRequestUrl, bearerToken, partial, update.installerSize,
		cancelRequested, callback, downloadError)) {
		if (downloadError == "cancelled") report(callback, UpdateDownloadStatus::Cancelled, "Update download cancelled.");
		else report(callback, UpdateDownloadStatus::Error, downloadError);
		return;
	}

	const auto actualSize = std::filesystem::file_size(partial, filesystemError);
	if (filesystemError || actualSize != update.installerSize) {
		std::filesystem::remove(partial, filesystemError);
		report(callback, UpdateDownloadStatus::Error, "The downloaded update had an unexpected size."); return;
	}
	report(callback, UpdateDownloadStatus::Verifying, "Verifying downloaded update…", actualSize, actualSize);
	if (sha256(partial) != update.installerSha256) {
		std::filesystem::remove(partial, filesystemError);
		report(callback, UpdateDownloadStatus::Error, "The downloaded update failed SHA-256 verification."); return;
	}
	std::filesystem::remove(verified, filesystemError); filesystemError.clear();
	std::filesystem::rename(partial, verified, filesystemError);
	if (filesystemError) {
		report(callback, UpdateDownloadStatus::Error, "The verified update could not be saved."); return;
	}
	report(callback, UpdateDownloadStatus::Verified,
		"Update downloaded and verified. Ready to install.", actualSize, actualSize, verified);
}
