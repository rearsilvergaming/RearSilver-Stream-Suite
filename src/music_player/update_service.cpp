#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>

#include "update_service.hpp"
#include "update_version.hpp"
#include "include/cef_parser.h"
#include "include/cef_values.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <vector>

namespace {
constexpr size_t kMaximumManifestBytes = 128 * 1024;

using WinHttpHandle = std::unique_ptr<void, decltype(&WinHttpCloseHandle)>;

bool httpsLocation(const std::string &baseUrl, std::wstring &host, INTERNET_PORT &port, std::wstring &path)
{
	if (baseUrl.empty()) return false;
	const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, baseUrl.data(), int(baseUrl.size()), nullptr, 0);
	if (count <= 0) return false;
	std::wstring wide(count, L'\0');
	MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, baseUrl.data(), int(baseUrl.size()), wide.data(), count);
	URL_COMPONENTS parts{}; parts.dwStructSize = sizeof(parts);
	parts.dwHostNameLength = DWORD(-1); parts.dwUrlPathLength = DWORD(-1); parts.dwExtraInfoLength = DWORD(-1);
	if (!WinHttpCrackUrl(wide.c_str(), DWORD(wide.size()), 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS) return false;
	host.assign(parts.lpszHostName, parts.dwHostNameLength); port = parts.nPort;
	path.assign(parts.lpszUrlPath, parts.dwUrlPathLength);
	if (parts.dwExtraInfoLength) return false;
	while (path.size() > 1 && path.back() == L'/') path.pop_back();
	return !host.empty();
}

std::string fetch(const std::string &baseUrl, const std::string &channel, std::string &error)
{
	std::wstring host, path; INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
	if (!httpsLocation(baseUrl, host, port, path)) { error = "The update service address is invalid."; return {}; }
	if (path.empty()) path = L"/";
	if (path.back() != L'/') path.push_back(L'/');
	const std::string slug = updateChannelSlug(channel);
	path += L"v1/updates/";
	path.append(slug.begin(), slug.end());
	path += L"/windows-x64";

	WinHttpHandle session(WinHttpOpen(L"RearSilver-Stream-Suite-Update/1.0",
		WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0), WinHttpCloseHandle);
	if (!session) { error = "Windows could not initialise the update connection."; return {}; }
	WinHttpSetTimeouts(session.get(), 5000, 5000, 10000, 15000);
	WinHttpHandle connection(WinHttpConnect(session.get(), host.c_str(), port, 0), WinHttpCloseHandle);
	if (!connection) { error = "The update service could not be reached."; return {}; }
	WinHttpHandle request(WinHttpOpenRequest(connection.get(), L"GET", path.c_str(), nullptr,
		WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE), WinHttpCloseHandle);
	if (!request || !WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0,
		WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(request.get(), nullptr)) {
		error = "The update service did not respond."; return {};
	}
	DWORD status = 0, statusBytes = sizeof(status);
	WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
		WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusBytes, WINHTTP_NO_HEADER_INDEX);
	if (status < 200 || status >= 300) { error = "The update service returned an error."; return {}; }
	std::string body;
	for (;;) {
		DWORD available = 0;
		if (!WinHttpQueryDataAvailable(request.get(), &available)) { error = "The update response could not be read."; return {}; }
		if (!available) break;
		if (body.size() + available > kMaximumManifestBytes) { error = "The update response was unexpectedly large."; return {}; }
		const size_t offset = body.size(); body.resize(offset + available); DWORD read = 0;
		if (!WinHttpReadData(request.get(), body.data() + offset, available, &read)) { error = "The update response could not be read."; return {}; }
		body.resize(offset + read);
	}
	return body;
}

bool safeHttpsUrl(const std::string &url)
{
	std::wstring host, path; INTERNET_PORT port = 0;
	return url.empty() || httpsLocation(url, host, port, path);
}

bool safeInstallerFilename(const std::string &filename, const std::string &channel)
{
	if (filename.empty() || filename.size() > 180 || filename.find("..") != std::string::npos) return false;
	if (filename.find('/') != std::string::npos || filename.find('\\') != std::string::npos) return false;
	const std::string expectedPrefix = channel == "owner-build" ? "RearSilver-Stream-Suite-Owner-" : "RearSilver-Stream-Suite-";
	return filename.rfind(expectedPrefix, 0) == 0 && filename.size() > 10 &&
		filename.substr(filename.size() - 10) == "-Setup.exe";
}

bool lowercaseSha256(const std::string &value)
{
	return value.size() == 64 && std::all_of(value.begin(), value.end(), [](const unsigned char character) {
		return std::isdigit(character) || (character >= 'a' && character <= 'f');
	});
}
}

UpdateCheckResult checkForSuiteUpdate(const std::string &baseUrl, const std::string &channel,
	const std::string &currentVersion, bool manual)
{
	UpdateCheckResult result; result.manual = manual;
	if (baseUrl.empty()) { result.message = "Update checks are not configured for this build."; return result; }
	std::string error;
	const std::string body = fetch(baseUrl, channel, error);
	if (body.empty()) { result.status = UpdateCheckStatus::Error; result.message = error; return result; }
	CefRefPtr<CefValue> root = CefParseJSON(body, JSON_PARSER_RFC);
	if (!root || root->GetType() != VTYPE_DICTIONARY) {
		result.status = UpdateCheckStatus::Error; result.message = "The update service returned an invalid manifest."; return result;
	}
	CefRefPtr<CefDictionaryValue> manifest = root->GetDictionary();
	const int schema = manifest->GetInt("schema");
	const int minimumUpdaterSchema = manifest->GetInt("minimum_updater_schema");
	const std::string product = manifest->GetString("product").ToString();
	const std::string manifestChannel = manifest->GetString("channel").ToString();
	const std::string platform = manifest->GetString("platform").ToString();
	result.availableVersion = manifest->GetString("version").ToString();
	result.publishedAt = manifest->GetString("published_at").ToString();
	result.releaseNotesUrl = manifest->GetString("release_notes_url").ToString();
	if (manifest->HasKey("release_notes") && manifest->GetType("release_notes") == VTYPE_LIST) {
		CefRefPtr<CefListValue> notes = manifest->GetList("release_notes");
		const size_t count = std::min<size_t>(notes ? notes->GetSize() : 0, 6);
		for (size_t index = 0; index < count; ++index) {
			if (notes->GetType(index) != VTYPE_STRING)
				continue;
			const std::string note = notes->GetString(index).ToString();
			if (!note.empty() && note.size() <= 180)
				result.releaseNotes.push_back(note);
		}
	}
	result.mandatory = manifest->GetBool("mandatory");
	if (manifest->HasKey("installer")) {
		CefRefPtr<CefDictionaryValue> installer = manifest->GetDictionary("installer");
		if (installer) {
			result.installerFilename = installer->GetString("filename").ToString();
			result.installerSha256 = installer->GetString("sha256").ToString();
			result.downloadRequestUrl = installer->GetString("download_request_url").ToString();
			const cef_value_type_t sizeType = installer->GetType("size");
			const double size = sizeType == VTYPE_INT ? installer->GetInt("size") : installer->GetDouble("size");
			result.installerSize = static_cast<std::uint64_t>(std::max(0.0, size));
		}
	}
	if (!manifest->HasKey("minimum_updater_schema") || schema != 1 || minimumUpdaterSchema > 1 || product != "rearsilver-stream-suite" || manifestChannel != updateChannelSlug(channel) ||
		platform != "windows-x64" || !safeHttpsUrl(result.releaseNotesUrl)) {
		result.status = UpdateCheckStatus::Error; result.message = "The update manifest did not match this Suite build."; return result;
	}
	int comparison = 0;
	if (!compareUpdateVersions(result.availableVersion, currentVersion, comparison)) {
		result.status = UpdateCheckStatus::Error; result.message = "The update manifest contained an invalid version."; return result;
	}
	const std::string minimum = manifest->GetString("minimum_supported_version").ToString();
	if (!minimum.empty()) {
		int minimumComparison = 0;
		if (!compareUpdateVersions(currentVersion, minimum, minimumComparison)) {
			result.status = UpdateCheckStatus::Error; result.message = "The update manifest contained an invalid supported version."; return result;
		}
		result.currentVersionSupported = minimumComparison >= 0;
	}
	if (comparison <= 0) {
		result.status = UpdateCheckStatus::UpToDate;
		result.message = "RearSilver Stream Suite is up to date.";
		return result;
	}
	result.status = UpdateCheckStatus::Available;
	result.message = "RearSilver Stream Suite " + result.availableVersion + " is available.";
	const std::string channelSlug = updateChannelSlug(channel);
	result.downloadAvailable = result.installerSize > 0 && lowercaseSha256(result.installerSha256) &&
		safeInstallerFilename(result.installerFilename, channelSlug) && safeHttpsUrl(result.downloadRequestUrl) &&
		!result.downloadRequestUrl.empty();
	return result;
}
