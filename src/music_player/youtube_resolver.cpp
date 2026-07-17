#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

#include "youtube_resolver.hpp"
#include "include/cef_parser.h"
#include "include/cef_values.h"

#include <algorithm>
#include <memory>

namespace {
constexpr wchar_t kHost[] = L"rearsilver-youtube-resolver.rearsilver.workers.dev";

std::wstring wide(const std::string &value)
{
	if (value.empty()) return {};
	const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), int(value.size()), nullptr, 0);
	std::wstring output(size, L'\0'); MultiByteToWideChar(CP_UTF8, 0, value.data(), int(value.size()), output.data(), size); return output;
}

std::wstring encode(const std::string &value)
{
	static const wchar_t hex[] = L"0123456789ABCDEF"; std::wstring output;
	for (unsigned char c : value) {
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') output.push_back(wchar_t(c));
		else { output.push_back(L'%'); output.push_back(hex[c >> 4]); output.push_back(hex[c & 15]); }
	}
	return output;
}

std::string get(const std::wstring &path, std::string &error)
{
	using Handle = std::unique_ptr<void, decltype(&WinHttpCloseHandle)>;
	Handle session(WinHttpOpen(L"RearSilver-Stream-Suite/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
		WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0), WinHttpCloseHandle);
	if (!session) { error = "Could not initialise the network client."; return {}; }
	WinHttpSetTimeouts(session.get(), 8000, 8000, 15000, 30000);
	Handle connection(WinHttpConnect(session.get(), kHost, INTERNET_DEFAULT_HTTPS_PORT, 0), WinHttpCloseHandle);
	if (!connection) { error = "Could not connect to the Suite YouTube resolver."; return {}; }
	Handle request(WinHttpOpenRequest(connection.get(), L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
		WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE), WinHttpCloseHandle);
	if (!request || !WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0,
		WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(request.get(), nullptr)) {
		error = "The Suite YouTube resolver did not respond."; return {};
	}
	DWORD status = 0, statusSize = sizeof(status);
	WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
		WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
	std::string body;
	for (;;) {
		DWORD available = 0; if (!WinHttpQueryDataAvailable(request.get(), &available) || available == 0) break;
		const size_t offset = body.size(); body.resize(offset + available); DWORD read = 0;
		if (!WinHttpReadData(request.get(), body.data() + offset, available, &read)) { error = "Could not read the resolver response."; return {}; }
		body.resize(offset + read);
	}
	if (status < 200 || status >= 300) { error = "The Suite YouTube resolver returned an error."; }
	return body;
}

HubTrack track(CefRefPtr<CefDictionaryValue> value, bool request, const std::string &requestedBy)
{
	HubTrack output; if (!value) return output;
	output.providerId = value->GetString("videoId").ToString(); output.id = "youtube_" + output.providerId;
	output.title = value->GetString("title").ToString(); output.artist = value->GetString("artist").ToString();
	output.artworkUrl = value->GetString("thumbnail").ToString(); output.durationSeconds = value->GetInt("durationSeconds");
	output.request = request; output.requestedBy = requestedBy; return output;
}
}

HubPlaylistResult resolveHubPlaylist(const std::string &playlistUrl)
{
	HubPlaylistResult result; result.sourceUrl = playlistUrl;
	const std::string body = get(L"/v1/youtube/playlist?url=" + encode(playlistUrl), result.error);
	if (body.empty()) return result;
	CefRefPtr<CefValue> root = CefParseJSON(body, JSON_PARSER_RFC);
	if (!root || root->GetType() != VTYPE_DICTIONARY) { result.error = "The resolver returned invalid playlist data."; return result; }
	CefRefPtr<CefDictionaryValue> object = root->GetDictionary();
	const std::string apiError = object->GetString("error").ToString(); if (!apiError.empty()) { result.error = apiError; return result; }
	result.label = object->GetString("title").ToString(); CefRefPtr<CefListValue> tracks = object->GetList("tracks");
	if (tracks) for (size_t i = 0; i < tracks->GetSize(); ++i) { HubTrack parsed = track(tracks->GetDictionary(i), false, {}); if (!parsed.providerId.empty()) result.tracks.push_back(std::move(parsed)); }
	if (result.tracks.empty() && result.error.empty()) result.error = "The playlist contained no playable videos.";
	return result;
}

HubSearchResult resolveHubSearch(const std::string &query, const std::string &requestedBy)
{
	HubSearchResult result; const std::string body = get(L"/v1/youtube/search?q=" + encode(query), result.error);
	if (body.empty()) return result;
	CefRefPtr<CefValue> root = CefParseJSON(body, JSON_PARSER_RFC);
	if (!root || root->GetType() != VTYPE_DICTIONARY) { result.error = "The resolver returned invalid search data."; return result; }
	CefRefPtr<CefDictionaryValue> object = root->GetDictionary();
	const std::string apiError = object->GetString("error").ToString(); if (!apiError.empty()) { result.error = apiError; return result; }
	result.track = track(object->GetDictionary("track"), true, requestedBy);
	if (result.track.providerId.empty()) result.error = "No playable YouTube result was returned.";
	return result;
}
