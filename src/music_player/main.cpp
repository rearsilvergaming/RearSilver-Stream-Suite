#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "rs_beta_config.hpp"
#include <dwmapi.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <objidl.h>
#include <shobjidl.h>
#include <gdiplus.h>
#include <dwrite.h>
#include <wrl.h>

#include "WebView2.h"
#include "WebView2EnvironmentOptions.h"
#include "include/cef_app.h"
#include "include/cef_audio_handler.h"
#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_command_line.h"
#include "include/cef_display_handler.h"
#include "include/cef_devtools_message_observer.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_load_handler.h"
#include "include/cef_render_handler.h"
#include "include/cef_request.h"
#include "include/cef_request_handler.h"
#include "include/cef_resource_handler.h"
#include "include/cef_resource_request_handler.h"
#include "include/cef_sandbox_win.h"
#include "include/cef_parser.h"
#include "include/cef_task.h"
#include "music_hub.hpp"
#include "command_catalogue.hpp"
#include "local_library.hpp"
#include "youtube_resolver.hpp"
#include "system_media_provider.hpp"
#include "spotify_client.hpp"
#include "twitch_accounts.hpp"
#include "twitch_chat_service.hpp"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <deque>
#include <fstream>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace Gdiplus;
using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

static std::string wideToUtf8(const std::wstring &value);

static std::mutex g_traceMutex;
static std::wstring g_tracePath;
static ULONGLONG g_traceStarted = 0;
static std::atomic<unsigned long long> g_traceSequence{0};

static std::wstring traceLogPath()
{
	if (!g_tracePath.empty()) return g_tracePath;
	wchar_t localAppData[MAX_PATH]{};
	if (!GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH)) return L"";
	const std::wstring root = std::wstring(localAppData) + L"\\RearSilver Stream Suite";
	const std::wstring folder = root + L"\\Logs";
	CreateDirectoryW(root.c_str(), nullptr);
	CreateDirectoryW(folder.c_str(), nullptr);
	SYSTEMTIME now{};
	GetLocalTime(&now);
	wchar_t name[160]{};
	swprintf_s(name, L"\\control-hub-trace-%04u%02u%02u-%02u%02u%02u-PID%lu.log",
		now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
		GetCurrentProcessId());
	g_tracePath = folder + name;
	g_traceStarted = GetTickCount64();
	return g_tracePath;
}

static void traceLog(const char *event, const std::string &detail = {})
{
	const std::wstring path = traceLogPath();
	if (path.empty()) return;
	SYSTEMTIME now{};
	GetLocalTime(&now);
	std::lock_guard<std::mutex> lock(g_traceMutex);
	std::ofstream output(path, std::ios::binary | std::ios::app);
	if (!output) return;
	output << std::setfill('0') << now.wYear << '-' << std::setw(2) << now.wMonth << '-'
		<< std::setw(2) << now.wDay << 'T' << std::setw(2) << now.wHour << ':'
		<< std::setw(2) << now.wMinute << ':' << std::setw(2) << now.wSecond << '.'
		<< std::setw(3) << now.wMilliseconds << std::setfill(' ')
		<< " seq=" << ++g_traceSequence
		<< " elapsed_ms=" << (GetTickCount64() - g_traceStarted)
		<< " pid=" << GetCurrentProcessId() << " tid=" << GetCurrentThreadId()
		<< " event=" << event;
	if (!detail.empty()) output << " detail=\"" << detail << '"';
	output << "\r\n";
	output.flush();
}

static void traceProcessMemory()
{
	PROCESS_MEMORY_COUNTERS_EX memory{};
	memory.cb = sizeof(memory);
	if (!GetProcessMemoryInfo(GetCurrentProcess(),
		reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory), sizeof(memory))) return;
	std::ostringstream detail;
	detail << "working_set_mb=" << (memory.WorkingSetSize / (1024 * 1024))
		<< " private_mb=" << (memory.PrivateUsage / (1024 * 1024))
		<< " peak_working_set_mb=" << (memory.PeakWorkingSetSize / (1024 * 1024))
		<< " gdi=" << GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS)
		<< " user=" << GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
	traceLog("process-memory", detail.str());
}

static LONG WINAPI traceUnhandledException(EXCEPTION_POINTERS *exception)
{
	std::ostringstream detail;
	if (exception && exception->ExceptionRecord) {
		detail << "code=0x" << std::hex << exception->ExceptionRecord->ExceptionCode
			<< " address=0x" << reinterpret_cast<uintptr_t>(exception->ExceptionRecord->ExceptionAddress);
	}
	traceLog("unhandled-exception", detail.str());
	return EXCEPTION_CONTINUE_SEARCH;
}

static BOOL CALLBACK traceMonitor(HMONITOR monitor, HDC, LPRECT rect, LPARAM)
{
	MONITORINFOEXW info{}; info.cbSize = sizeof(info);
	GetMonitorInfoW(monitor, &info);
	std::ostringstream detail;
	detail << "rect=" << rect->left << ',' << rect->top << ',' << rect->right << ',' << rect->bottom
		<< " primary=" << ((info.dwFlags & MONITORINFOF_PRIMARY) ? 1 : 0);
	traceLog("monitor", detail.str());
	return TRUE;
}

static void tracedDwmFlush(const char *label)
{
	const ULONGLONG started = GetTickCount64();
	const HRESULT result = DwmFlush();
	std::ostringstream detail;
	detail << "label=" << label << " hr=0x" << std::hex << static_cast<unsigned long>(result)
		<< std::dec << " duration_ms=" << (GetTickCount64() - started);
	traceLog("dwm-flush", detail.str());
}

static std::wstring lifecycleLogPath()
{
	wchar_t localAppData[MAX_PATH]{};
	if (!GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH)) return L"";
	const std::wstring folder = std::wstring(localAppData) + L"\\RearSilver Stream Suite";
	CreateDirectoryW(folder.c_str(), nullptr);
	return folder + L"\\control-hub-lifecycle.log";
}

static void lifecycleLog(const char *event)
{
	const std::wstring path = lifecycleLogPath();
	if (path.empty()) return;
	SYSTEMTIME now{}; GetLocalTime(&now);
	std::ofstream output(path, std::ios::binary | std::ios::app);
	if (!output) return;
	output << now.wYear << '-' << now.wMonth << '-' << now.wDay << ' '
		<< now.wHour << ':' << now.wMinute << ':' << now.wSecond << '.' << now.wMilliseconds
		<< " PID=" << GetCurrentProcessId() << " PPID=";
	DWORD parentId = 0;
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snapshot != INVALID_HANDLE_VALUE) {
		PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
		if (Process32FirstW(snapshot, &entry)) do {
			if (entry.th32ProcessID == GetCurrentProcessId()) { parentId = entry.th32ParentProcessID; break; }
		} while (Process32NextW(snapshot, &entry));
		CloseHandle(snapshot);
	}
	output << parentId << " event=" << event << " command=" << wideToUtf8(GetCommandLineW()) << "\r\n";
}

static bool g_sidebarCollapsed = false;
static std::unique_ptr<Image> g_brandIconImage;
static std::unique_ptr<Image> g_brandHeaderImage;
static std::unique_ptr<Image> g_splashImage;
static int sidebarWidthFor(int clientWidth) { return (g_sidebarCollapsed || clientWidth < 820) ? 120 : 232; }
struct QueueItem { std::wstring title, artist, source; int durationSeconds = 0; bool current = false; };
static std::vector<QueueItem> g_queue;
static MusicHubModel g_hub;
static HWND g_playlistEdit = nullptr, g_importPlaylistButton = nullptr;
static HWND g_addFilesButton = nullptr, g_addFolderButton = nullptr, g_useLocalButton = nullptr,
	g_useYouTubeButton = nullptr, g_clearLocalButton = nullptr;
static HWND g_overlayCustomTextEdit = nullptr;
static HWND g_overlayFontCombo = nullptr;
static HWND g_overlayStyleEdits[8]{};
static HBRUSH g_controlBackgroundBrush = nullptr;
static HFONT g_controlFont = nullptr;
static int g_overlaySection = 0;
static std::wstring g_libraryStatus = L"Paste a YouTube or YouTube Music playlist URL to import it into the Suite Media Player.";
static constexpr UINT WM_HUB_PLAYLIST_RESULT = WM_APP + 40;
static constexpr UINT WM_HUB_REQUEST_RESULT = WM_APP + 41;
static constexpr UINT WM_TWITCH_CHAT = WM_APP + 42;
static constexpr UINT WM_OBS_CONNECTION_CHANGED = WM_APP + 43;
static constexpr int ID_IMPORT_PLAYLIST = 4101;
static constexpr int ID_ADD_LOCAL_FILES = 4102, ID_ADD_LOCAL_FOLDER = 4103, ID_USE_LOCAL = 4104,
	ID_USE_YOUTUBE = 4105, ID_CLEAR_LOCAL = 4106, ID_USE_EXTERNAL = 4107;
static constexpr int ID_OVERLAY_CUSTOM_TEXT = 4110;
static constexpr int ID_OVERLAY_FONT = 4120;
static constexpr int ID_OVERLAY_TITLE_SIZE = 4121, ID_OVERLAY_BODY_SIZE = 4122,
	ID_OVERLAY_OPACITY = 4123, ID_OVERLAY_BACKGROUND_COLOUR = 4124, ID_OVERLAY_TEXT_COLOUR = 4125,
	ID_OVERLAY_ACCENT_COLOUR = 4126, ID_OVERLAY_WIDTH = 4127, ID_OVERLAY_HEIGHT = 4128;
static int g_queuePage = 0;
static RECT g_transportProgress{};
static bool g_transportSeeking = false;
static int64_t g_transportSeekTargetMs = 0;
static bool g_transportSeekWasPlaying = false;
static bool g_transportSeekPending = false;
static ULONGLONG g_transportSeekPendingSince = 0;
static std::mutex g_hostEventMutex;
static std::vector<std::string> g_hostEvents;
static bool g_hubMediaKeysRegistered = false;
static RECT g_queuePreviousPage{}, g_queueNextPage{}, g_queueShuffle{}, g_queuePlayFromBeginning{};

class SuiteCefApp final : public CefApp, public CefBrowserProcessHandler {
public:
	CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override { return this; }
	void OnBeforeCommandLineProcessing(const CefString &, CefRefPtr<CefCommandLine> commandLine) override
	{
		commandLine->AppendSwitch("disable-background-timer-throttling");
		commandLine->AppendSwitch("disable-renderer-backgrounding");
		commandLine->AppendSwitch("disable-backgrounding-occluded-windows");
		commandLine->AppendSwitch("disable-background-media-suspend");
		commandLine->AppendSwitchWithValue("autoplay-policy", "no-user-gesture-required");
		commandLine->AppendSwitchWithValue("disable-features",
			"CalculateNativeWinOcclusion,IntensiveWakeUpThrottling,"
			"ThrottleDisplayNoneAndVisibilityHiddenCrossOriginIframes,UseEcoQoSForBackgroundProcess");
	}
private:
	IMPLEMENT_REFCOUNTING(SuiteCefApp);
};

static std::wstring utf8ToWide(const std::string &value)
{
	if (value.empty()) return {};
	const int count = MultiByteToWideChar(CP_UTF8, 0, value.data(), int(value.size()), nullptr, 0);
	std::wstring result(size_t(count), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, value.data(), int(value.size()), result.data(), count);
	return result;
}

static std::string wideToUtf8(const std::wstring &value)
{
	if (value.empty()) return {};
	const int count = WideCharToMultiByte(CP_UTF8, 0, value.data(), int(value.size()), nullptr, 0, nullptr, nullptr);
	std::string result(size_t(count), '\0');
	WideCharToMultiByte(CP_UTF8, 0, value.data(), int(value.size()), result.data(), count, nullptr, nullptr);
	return result;
}

static bool copyTextToClipboard(HWND owner, const std::wstring &text)
{
	if (!OpenClipboard(owner)) return false;
	EmptyClipboard();
	const SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t);
	HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
	if (!memory) { CloseClipboard(); return false; }
	void *destination = GlobalLock(memory);
	if (!destination) { GlobalFree(memory); CloseClipboard(); return false; }
	memcpy(destination, text.c_str(), bytes);
	GlobalUnlock(memory);
	if (!SetClipboardData(CF_UNICODETEXT, memory)) { GlobalFree(memory); CloseClipboard(); return false; }
	CloseClipboard();
	return true;
}

static std::wstring executableAssetPath(const wchar_t *name)
{
	wchar_t executable[MAX_PATH]{}; GetModuleFileNameW(nullptr, executable, MAX_PATH);
	std::wstring path(executable); const size_t separator = path.find_last_of(L"\\/");
	if (separator != std::wstring::npos) path.resize(separator + 1); else path.clear();
	path += name; return path;
}

static std::wstring suiteDataFolder(bool roaming)
{
	wchar_t value[MAX_PATH]{};
	if (!GetEnvironmentVariableW(roaming ? L"APPDATA" : L"LOCALAPPDATA", value, MAX_PATH)) return {};
	std::wstring folder = std::wstring(value) + L"\\RearSilver Stream Suite";
	CreateDirectoryW(folder.c_str(), nullptr);
	return folder;
}

static std::string localReportTime()
{
	SYSTEMTIME time{}; GetLocalTime(&time);
	wchar_t zone[128]{}; TIME_ZONE_INFORMATION zoneInfo{}; GetTimeZoneInformation(&zoneInfo);
	wcsncpy_s(zone, zoneInfo.StandardName, _TRUNCATE);
	std::ostringstream out;
	out << std::setfill('0') << std::setw(4) << time.wYear << '-' << std::setw(2) << time.wMonth << '-'
		<< std::setw(2) << time.wDay << ' ' << std::setw(2) << time.wHour << ':' << std::setw(2) << time.wMinute
		<< ':' << std::setw(2) << time.wSecond << " local";
	const std::string zoneText = wideToUtf8(zone);
	if (!zoneText.empty()) out << " (" << zoneText << ')';
	return out.str();
}

static std::string windowsDescription()
{
	OSVERSIONINFOW version{}; version.dwOSVersionInfoSize = sizeof(version);
	using RtlGetVersionFn = LONG(WINAPI *)(OSVERSIONINFOW *);
	auto function = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
	if (!function || function(&version) != 0) return "Windows (version unavailable) · x64";
	return "Windows " + std::to_string(version.dwMajorVersion) + '.' + std::to_string(version.dwMinorVersion) +
		" build " + std::to_string(version.dwBuildNumber) + " · x64";
}

static void replaceAll(std::string &text, const std::string &from, const std::string &to)
{
	if (from.empty()) return;
	for (size_t position = 0; (position = text.find(from, position)) != std::string::npos; position += to.size())
		text.replace(position, from.size(), to);
}

static std::string redactDiagnosticText(std::string text)
{
	wchar_t userName[256]{}; DWORD userNameLength = DWORD(_countof(userName));
	if (GetUserNameW(userName, &userNameLength)) replaceAll(text, wideToUtf8(userName), "<windows-user>");
	wchar_t profile[MAX_PATH]{};
	if (GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH)) replaceAll(text, wideToUtf8(profile), "<user-profile>");
	std::istringstream input(text); std::ostringstream output; std::string line;
	while (std::getline(input, line)) {
		std::string lower = line;
		std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char value) { return char(std::tolower(value)); });
		const bool credentialValue = lower.find('=') != std::string::npos || lower.find(':') != std::string::npos;
		const bool sensitive = credentialValue && (lower.find("access_token") != std::string::npos ||
			lower.find("refresh_token") != std::string::npos || lower.find("client_secret") != std::string::npos ||
			lower.find("client id") != std::string::npos || lower.find("client_id") != std::string::npos ||
			lower.find("oauth code") != std::string::npos || lower.find("device code") != std::string::npos ||
			lower.find("stream key") != std::string::npos || lower.find("authorization:") != std::string::npos);
		output << (sensitive ? "[REDACTED SENSITIVE LINE]" : line) << '\n';
	}
	return output.str();
}

static std::string relevantLogExcerpt(const std::wstring &path, const char *label)
{
	std::ifstream input(path, std::ios::binary);
	if (!input) return std::string(label) + ": unavailable\n";
	input.seekg(0, std::ios::end); const std::streamoff size = input.tellg();
	const std::streamoff start = std::max<std::streamoff>(0, size - 65536); input.seekg(start);
	std::string line; std::deque<std::string> selected;
	if (start > 0) std::getline(input, line); // Discard the partial line at the read boundary.
	while (std::getline(input, line)) {
		std::string lower = line;
		std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char value) { return char(std::tolower(value)); });
		if (lower.find("event=process-start") != std::string::npos) continue;
		if (lower.find("warn") == std::string::npos && lower.find("error") == std::string::npos &&
			lower.find("fail") == std::string::npos && lower.find("exception") == std::string::npos &&
			lower.find("reject") == std::string::npos && lower.find("unavailable") == std::string::npos &&
			lower.find("disconnect") == std::string::npos) continue;
		selected.push_back(line); if (selected.size() > 12) selected.pop_front();
	}
	std::ostringstream output; output << label << ":\n";
	if (selected.empty()) output << "  No recent warning or error lines found.\n";
	else for (const std::string &entry : selected) output << "  " << entry << '\n';
	return redactDiagnosticText(output.str());
}

static bool exportTextReport(HWND owner, const std::wstring &suggestedName, const std::string &report)
{
	ComPtr<IFileSaveDialog> dialog;
	if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return false;
	dialog->SetTitle(L"Export RearSilver Stream Suite beta feedback");
	dialog->SetFileName(suggestedName.c_str());
	const COMDLG_FILTERSPEC filters[] = {{L"Text files", L"*.txt"}, {L"All files", L"*.*"}};
	dialog->SetFileTypes(2, filters); dialog->SetDefaultExtension(L"txt");
	if (FAILED(dialog->Show(owner))) return false;
	ComPtr<IShellItem> item; PWSTR rawPath = nullptr;
	if (FAILED(dialog->GetResult(&item)) || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &rawPath))) return false;
	std::ofstream output(rawPath, std::ios::binary | std::ios::trunc); CoTaskMemFree(rawPath);
	if (!output) return false;
	const unsigned char bom[] = {0xef, 0xbb, 0xbf}; output.write(reinterpret_cast<const char *>(bom), sizeof(bom));
	output.write(report.data(), std::streamsize(report.size()));
	return output.good();
}

class WebViewYouTubePlayer {
public:
	~WebViewYouTubePlayer() { shutdown(); }
	void shutdown()
	{
		traceLog("webview-youtube-shutdown-begin", m_controller ? "controller=present" : "controller=absent");
		m_ready = false; m_active = false; m_parent = nullptr;
		if (m_controller) {
			const HRESULT visibleResult = m_controller->put_IsVisible(FALSE);
			{ std::ostringstream detail; detail << "visible=0 hr=0x" << std::hex << static_cast<unsigned long>(visibleResult); traceLog("webview-youtube-visibility", detail.str()); }
			traceLog("webview-youtube-controller-close-begin");
			const HRESULT closeResult = m_controller->Close();
			{ std::ostringstream detail; detail << "hr=0x" << std::hex << static_cast<unsigned long>(closeResult); traceLog("webview-youtube-controller-close-complete", detail.str()); }
		}
		m_webView.Reset(); m_controller.Reset();
		traceLog("webview-youtube-shutdown-complete");
	}
	void initialise(HWND parent)
	{
		m_parent = parent;
		wchar_t localAppData[MAX_PATH]{};
		GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
		std::wstring suiteData = std::wstring(localAppData) + L"\\RearSilver Stream Suite";
		CreateDirectoryW(suiteData.c_str(), nullptr);
		std::wstring webViewData = suiteData + L"\\WebView2";
		CreateDirectoryW(webViewData.c_str(), nullptr);
		wchar_t executable[MAX_PATH]{};
		GetModuleFileNameW(nullptr, executable, MAX_PATH);
		std::wstring folder(executable);
		const size_t separator = folder.find_last_of(L"\\/");
		if (separator != std::wstring::npos) folder.resize(separator);

		auto environmentOptions = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
		// Keep Chromium's audio service in the WebView2 browser process. OBS's
		// Application Audio Capture can then associate embedded YouTube playback
		// with the Suite player instead of losing it to a detached utility process.
		// Keep Chromium's audio service in this process so OBS Application Audio
		// Capture can associate playback with the Control Hub. GPU rendering stays
		// enabled; the blackout investigation isolated Spotify's desktop process.
		environmentOptions->put_AdditionalBrowserArguments(
			L"--disable-features=AudioServiceOutOfProcess");
		traceLog("webview-youtube-gpu-rendering-enabled");

		CreateCoreWebView2EnvironmentWithOptions(
			nullptr, webViewData.c_str(), environmentOptions.Get(),
			Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
				[this, folder](HRESULT result, ICoreWebView2Environment *environment) -> HRESULT {
					{ std::ostringstream detail; detail << "hr=0x" << std::hex << static_cast<unsigned long>(result) << " environment=" << (environment ? 1 : 0); traceLog("webview-youtube-environment-created", detail.str()); }
					if (FAILED(result) || !environment) {
						m_events.emplace_back("ERROR\tMicrosoft WebView2 Runtime is unavailable. Repair the Suite installation.\n");
						return result;
					}
					return environment->CreateCoreWebView2Controller(
						m_parent,
						Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
							[this, folder](HRESULT controllerResult, ICoreWebView2Controller *controller) -> HRESULT {
								{ std::ostringstream detail; detail << "hr=0x" << std::hex << static_cast<unsigned long>(controllerResult) << " controller=" << (controller ? 1 : 0); traceLog("webview-youtube-controller-created", detail.str()); }
								if (FAILED(controllerResult) || !controller) return controllerResult;
								m_controller = controller;
								m_controller->get_CoreWebView2(&m_webView);
								EventRegistrationToken processFailedToken{};
								m_webView->add_ProcessFailed(
									Callback<ICoreWebView2ProcessFailedEventHandler>(
										[](ICoreWebView2 *, ICoreWebView2ProcessFailedEventArgs *args) -> HRESULT {
											COREWEBVIEW2_PROCESS_FAILED_KIND kind{};
											if (args) args->get_ProcessFailedKind(&kind);
											traceLog("webview-youtube-process-failed", "kind=" + std::to_string(static_cast<int>(kind)));
											return S_OK;
										}).Get(), &processFailedToken);
								ComPtr<ICoreWebView2_3> webView3;
								if (SUCCEEDED(m_webView.As(&webView3))) {
									webView3->SetVirtualHostNameToFolderMapping(
										L"rearsilver.local", folder.c_str(),
										COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
								}
								EventRegistrationToken token{};
								m_webView->add_WebMessageReceived(
									Callback<ICoreWebView2WebMessageReceivedEventHandler>(
										[this](ICoreWebView2 *, ICoreWebView2WebMessageReceivedEventArgs *args) -> HRESULT {
											wchar_t *raw = nullptr;
											if (SUCCEEDED(args->TryGetWebMessageAsString(&raw)) && raw) {
												const std::string message = wideToUtf8(raw);
												CoTaskMemFree(raw);
												if (message == "ready") {
													m_ready = true;
													loadPending();
												} else if (message == "state\tended") {
													m_events.emplace_back("EVENT\tyoutube-ended\t" + wideToUtf8(m_currentVideoId) + "\n");
												} else if (message.rfind("metadata\t", 0) == 0) {
													m_events.emplace_back("EVENT\tyoutube-metadata\t" + message.substr(9) + "\n");
												} else if (message.rfind("status\t", 0) == 0) {
													m_events.emplace_back("STATUS\t" + message.substr(7) + "\tyoutube:" + wideToUtf8(m_currentVideoId) + "\n");
												} else if (message.rfind("error\t", 0) == 0) {
													m_events.emplace_back("ERROR\tYouTube could not play this video.\n");
												}
											}
											return S_OK;
										}).Get(),
									&token);
								resize();
								{ const HRESULT hr = m_controller->put_IsVisible(FALSE); std::ostringstream detail; detail << "visible=0 hr=0x" << std::hex << static_cast<unsigned long>(hr); traceLog("webview-youtube-visibility", detail.str()); }
								m_webView->Navigate(L"https://rearsilver.local/youtube-player.html");
								return S_OK;
							}).Get());
				}).Get());
	}

	void resize()
	{
		if (!m_controller || !m_parent) return;
		RECT client{};
		GetClientRect(m_parent, &client);
		const int sidebar = sidebarWidthFor(client.right);
		const int contentLeft = sidebar + 28;
		const int contentRight = std::max(contentLeft + 240, static_cast<int>(client.right) - 28);
		const int transportTop = std::max(400, static_cast<int>(client.bottom) - 106);
		const int availableWidth = std::max(240, contentRight - contentLeft);
		const int availableHeight = std::max(220, transportTop - 126);
		const int videoWidth = std::min(availableWidth, availableHeight * 16 / 9);
		const int videoHeight = videoWidth * 9 / 16;
		const int videoX = contentLeft + (availableWidth - videoWidth) / 2;
		const int videoY = 104 + std::max(0, (availableHeight - videoHeight) / 2);
		RECT bounds{videoX, videoY, videoX + videoWidth, videoY + videoHeight};
		const HRESULT hr = m_controller->put_Bounds(bounds);
		{ std::ostringstream detail; detail << "left=" << bounds.left << " top=" << bounds.top << " right=" << bounds.right << " bottom=" << bounds.bottom << " hr=0x" << std::hex << static_cast<unsigned long>(hr); traceLog("webview-youtube-bounds", detail.str()); }
	}

	void load(const std::string &videoId)
	{
		std::wstring safe;
		for (wchar_t character : utf8ToWide(videoId)) {
			if ((character >= L'a' && character <= L'z') || (character >= L'A' && character <= L'Z') ||
			    (character >= L'0' && character <= L'9') || character == L'_' || character == L'-')
				safe.push_back(character);
		}
		if (safe.empty()) return;
		m_pendingVideoId = safe;
		m_currentVideoId = safe;
		m_active = true;
		setWindowMode(true);
		if (m_controller) { const HRESULT hr=m_controller->put_IsVisible(TRUE); std::ostringstream detail; detail << "visible=1 hr=0x" << std::hex << static_cast<unsigned long>(hr); traceLog("webview-youtube-visibility", detail.str()); }
		loadPending();
	}

	void hide()
	{
		m_active = false;
		if (m_controller) { const HRESULT hr=m_controller->put_IsVisible(FALSE); std::ostringstream detail; detail << "visible=0 hr=0x" << std::hex << static_cast<unsigned long>(hr); traceLog("webview-youtube-visibility", detail.str()); }
		setWindowMode(false);
	}

	void command(const std::string &action, const std::string &argument = {})
	{
		if (!m_webView || !m_active) return;
		std::wstring script;
		if (action == "PLAY") script = L"window.rsMusic.play()";
		else if (action == "PAUSE") script = L"window.rsMusic.pause()";
		else if (action == "STOP") script = L"window.rsMusic.stop()";
		else if (action == "RESTART") script = L"window.rsMusic.restart()";
		else if (action == "SEEK") {
			const double seconds = std::max<int64_t>(0, _strtoi64(argument.c_str(), nullptr, 10)) / 1000.0;
			script = L"window.rsMusic.seek(" + std::to_wstring(seconds) + L")";
		}
		if (!script.empty()) m_webView->ExecuteScript(script.c_str(), nullptr);
	}

	bool active() const { return m_active; }
	std::vector<std::string> takeEvents() { std::vector<std::string> result; result.swap(m_events); return result; }

private:
	void setWindowMode(bool youtube)
	{
		if (!m_parent) return;
		resize();
		InvalidateRect(m_parent, nullptr, FALSE);
	}

	void loadPending()
	{
		if (!m_ready || !m_webView || m_pendingVideoId.empty()) return;
		const std::wstring script = L"window.rsMusic.load(\"" + m_pendingVideoId + L"\")";
		m_webView->ExecuteScript(script.c_str(), nullptr);
		m_pendingVideoId.clear();
	}

	HWND m_parent = nullptr;
	ComPtr<ICoreWebView2Controller> m_controller;
	ComPtr<ICoreWebView2> m_webView;
	bool m_ready = false;
	bool m_active = false;
	std::wstring m_pendingVideoId;
	std::wstring m_currentVideoId;
	std::vector<std::string> m_events;
};

class Player {
public:
	bool initialise() { return true; }
	~Player() { suspend(); }
	void suspend()
	{
		unload();
		if (m_engineReady) { ma_engine_uninit(&m_engine); m_engineReady = false; }
		// Artwork is a GDI+ Image. Release it while GDI+ is still running;
		// otherwise Player's late wWinMain-scope destructor releases it after
		// GdiplusShutdown and faults during process exit.
		m_artwork.reset();
	}

	std::string command(const std::string &line)
	{
		const size_t tab = line.find('\t');
		const std::string action = line.substr(0, tab);
		const std::string argument = tab == std::string::npos ? std::string{} : line.substr(tab + 1);
		if (action == "LOAD") return load(argument);
		if (action == "PLAY" && m_ready) { ma_sound_start(&m_sound); m_paused = false; }
		else if (action == "PAUSE" && m_ready) { ma_sound_stop(&m_sound); m_paused = true; }
		else if (action == "STOP") stop();
		else if (action == "RESTART" && m_ready) {
			ma_sound_seek_to_pcm_frame(&m_sound, 0); ma_sound_start(&m_sound); m_paused = false; m_endedSent = false;
		} else if (action == "SEEK" && m_ready) {
			const float seconds = float(std::max<int64_t>(0, _strtoi64(argument.c_str(), nullptr, 10))) / 1000.0f;
			ma_sound_seek_to_second(&m_sound, seconds); m_endedSent = false;
		}
		return {};
	}

	std::string status()
	{
		float position = 0.0f, duration = 0.0f;
		if (m_ready) { ma_sound_get_cursor_in_seconds(&m_sound, &position); ma_sound_get_length_in_seconds(&m_sound, &duration); }
		m_position = position; m_duration = duration;
		const char *state = !m_ready ? "stopped" : (m_paused ? "paused" : (ma_sound_is_playing(&m_sound) ? "playing" : "stopped"));
		m_state = utf8ToWide(state);
		return "STATUS\t" + std::string(state) + "\t" + std::to_string(int64_t(position * 1000.0f)) + "\t" +
		       std::to_string(int64_t(duration * 1000.0f)) + "\t" + m_path + "\n";
	}

	void setMetadata(const std::string &value)
	{
		std::string fields[4]; size_t start = 0;
		for (int i = 0; i < 4; ++i) {
			const size_t end = value.find('\t', start);
			fields[i] = value.substr(start, end == std::string::npos ? end : end - start);
			start = end == std::string::npos ? value.size() : end + 1;
		}
		m_title = utf8ToWide(fields[0]); m_artist = utf8ToWide(fields[1]); m_album = utf8ToWide(fields[2]);
		m_artwork.reset(fields[3].empty() ? nullptr : Image::FromFile(utf8ToWide(fields[3]).c_str()));
	}

	bool takeEnded() { if (!m_ready || m_endedSent || !ma_sound_at_end(&m_sound)) return false; m_endedSent = true; return true; }
	const std::string &path() const { return m_path; }
	const std::wstring &title() const { return m_title; }
	const std::wstring &artist() const { return m_artist; }
	const std::wstring &album() const { return m_album; }
	const std::wstring &state() const { return m_state; }
	float position() const { return m_position; }
	float duration() const { return m_duration; }
	Image *artwork() const { return m_artwork.get(); }

private:
	std::string load(const std::string &path)
	{
		unload();
		if (!m_engineReady) {
			if (ma_engine_init(nullptr, &m_engine) != MA_SUCCESS)
				return "ERROR\tThe companion player could not open its Windows audio output.\n";
			m_engineReady = true;
		}
		if (ma_sound_init_from_file_w(&m_engine, utf8ToWide(path).c_str(), 0, nullptr, nullptr, &m_sound) != MA_SUCCESS)
			return "ERROR\tThe companion player could not decode this file.\n";
		m_ready = true; m_paused = false; m_endedSent = false; m_path = path;
		ma_sound_start(&m_sound);
		return "EVENT\tloaded\t" + m_path + "\n";
	}
	void stop() { if (m_ready) { ma_sound_stop(&m_sound); ma_sound_seek_to_pcm_frame(&m_sound, 0); m_paused = false; m_endedSent = false; } }
	void unload() { if (m_ready) { ma_sound_stop(&m_sound); ma_sound_uninit(&m_sound); m_ready = false; m_path.clear(); } }
	ma_engine m_engine{}; ma_sound m_sound{}; bool m_engineReady = false;
	bool m_ready = false, m_paused = false, m_endedSent = false;
	float m_position = 0, m_duration = 0;
	std::string m_path;
	std::wstring m_title = L"No track playing", m_artist, m_album, m_state = L"Stopped";
	std::unique_ptr<Image> m_artwork;
};

static RECT youtubeVideoBounds(HWND window)
{
	RECT client{}; GetClientRect(window, &client);
	const int sidebar = sidebarWidthFor(client.right);
	const int left = sidebar + 28, right = std::max(left + 240, static_cast<int>(client.right) - 28);
	const int transportTop = std::max(400, static_cast<int>(client.bottom) - 106);
	const int availableWidth = std::max(240, right - left);
	const int availableHeight = std::max(220, transportTop - 126);
	const int videoWidth = std::min(availableWidth, availableHeight * 16 / 9);
	const int videoHeight = videoWidth * 9 / 16;
	const int x = left + (availableWidth - videoWidth) / 2;
	const int y = 104 + std::max(0, (availableHeight - videoHeight) / 2);
	return RECT{x, y, x + videoWidth, y + videoHeight};
}

class CefAudioPipeline {
public:
	~CefAudioPipeline() { shutdown(); }
	bool initialise()
	{
		shutdown();
		m_stop.store(false);
		{
			std::lock_guard<std::mutex> inputLock(m_inputMutex); m_input.clear();
		}
		{
			std::lock_guard<std::mutex> outputLock(m_outputMutex); m_output.clear();
		}
		ma_device_config config = ma_device_config_init(ma_device_type_playback);
		config.playback.format = ma_format_f32;
		config.playback.channels = 2;
		config.sampleRate = 48000;
		config.periodSizeInFrames = 480;
		config.dataCallback = &CefAudioPipeline::deviceCallback;
		config.pUserData = this;
		if (ma_device_init(nullptr, &config, &m_device) != MA_SUCCESS) return false;
		m_deviceReady = true;
		if (ma_device_start(&m_device) != MA_SUCCESS) { shutdown(); return false; }
		m_clock = std::thread([this] { clockLoop(); });
		return true;
	}
	void shutdown()
	{
		m_stop.store(true);
		if (m_clock.joinable()) m_clock.join();
		if (m_deviceReady) ma_device_uninit(&m_device);
		m_deviceReady = false;
		{
			std::lock_guard<std::mutex> inputLock(m_inputMutex); m_input.clear();
		}
		{
			std::lock_guard<std::mutex> outputLock(m_outputMutex); m_output.clear();
		}
	}
	void write(const float **data, int channels, int frames)
	{
		if (!data || channels <= 0 || frames <= 0) return;
		std::lock_guard<std::mutex> lock(m_inputMutex);
		for (int frame = 0; frame < frames; ++frame) {
			const float left = data[0] ? data[0][frame] : 0.0f;
			const float right = channels > 1 && data[1] ? data[1][frame] : left;
			m_input.push_back(left); m_input.push_back(right);
		}
		while (m_input.size() > 48000) { m_input.pop_front(); m_input.pop_front(); }
	}
private:
	static void deviceCallback(ma_device *device, void *output, const void *, ma_uint32 frames)
	{
		static_cast<CefAudioPipeline *>(device->pUserData)->read(static_cast<float *>(output), frames);
	}
	void read(float *output, uint32_t frames)
	{
		if (!output) return;
		const size_t wanted = static_cast<size_t>(frames) * 2;
		std::fill(output, output + wanted, 0.0f);
		std::lock_guard<std::mutex> lock(m_outputMutex);
		const size_t count = std::min(wanted, m_output.size());
		for (size_t i = 0; i < count; ++i) { output[i] = m_output.front(); m_output.pop_front(); }
	}
	void clockLoop()
	{
		constexpr size_t blockSamples = 960, primeSamples = blockSamples * 6, targetSamples = blockSamples * 10;
		float block[blockSamples]{}; bool primed = false;
		auto next = std::chrono::steady_clock::now();
		while (!m_stop.load()) {
			next += std::chrono::milliseconds(10);
			std::fill(std::begin(block), std::end(block), 0.0f);
			{
				std::lock_guard<std::mutex> lock(m_inputMutex);
				if (m_input.size() > targetSamples * 2)
					while (m_input.size() > targetSamples) m_input.pop_front();
				if (!primed && m_input.size() >= primeSamples) primed = true;
				if (primed && m_input.size() >= blockSamples) {
					for (size_t i = 0; i < blockSamples; ++i) { block[i] = m_input.front(); m_input.pop_front(); }
				} else if (primed) primed = false;
			}
			{
				std::lock_guard<std::mutex> lock(m_outputMutex);
				m_output.insert(m_output.end(), std::begin(block), std::end(block));
				while (m_output.size() > 48000) { m_output.pop_front(); m_output.pop_front(); }
			}
			std::this_thread::sleep_until(next);
			if (std::chrono::steady_clock::now() - next > std::chrono::milliseconds(50))
				next = std::chrono::steady_clock::now();
		}
	}
	ma_device m_device{}; bool m_deviceReady = false;
	std::atomic<bool> m_stop{false}; std::thread m_clock;
	std::mutex m_inputMutex, m_outputMutex; std::deque<float> m_input, m_output;
};

class CefYouTubePlayer final : public CefClient, public CefLifeSpanHandler,
	public CefRenderHandler, public CefLoadHandler, public CefDisplayHandler, public CefDevToolsMessageObserver,
	public CefRequestHandler, public CefResourceRequestHandler {
public:
	CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
	CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
	CefRefPtr<CefRenderHandler> GetRenderHandler() override { return this; }
	CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
	CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
	CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
		CefRefPtr<CefRequest> request, bool, bool, const CefString &, bool &disableDefaultHandling) override
	{
		if (request->GetURL().ToString().rfind("https://rearsilver.local/player", 0) != 0) return nullptr;
		disableDefaultHandling = true; return this;
	}
	CefRefPtr<CefResourceHandler> GetResourceHandler(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
		CefRefPtr<CefRequest> request) override
	{
		if (request->GetURL().ToString().rfind("https://rearsilver.local/player", 0) != 0) return nullptr;
		std::lock_guard<std::mutex> lock(m_wrapperMutex); return new HtmlResourceHandler(m_wrapperHtml);
	}

	bool initialise(HWND parent)
	{
		traceLog("cef-browser-create-begin");
		m_parent = parent;
		m_browserClosed.store(false);
		CefWindowInfo info; info.SetAsWindowless(parent);
		CefBrowserSettings settings; settings.windowless_frame_rate = 30;
		const bool created = CefBrowserHost::CreateBrowser(info, this, "about:blank", settings, nullptr, nullptr);
		if (!created) { m_browserClosed.store(true); traceLog("cef-browser-create-failed"); }
		else traceLog("cef-browser-create-queued");
		return created;
	}
	void setParent(HWND parent) { m_parent = parent; }
	void shutdown()
	{
		m_active.store(false);
		closeBrowserAndWait();
		m_devToolsRegistration = nullptr; m_parent = nullptr;
	}
	void closeBrowserAndWait()
	{
		if (!m_browser) { traceLog("cef-browser-close-skip", "reason=no-browser"); return; }
		traceLog("cef-browser-close-begin");
		m_browserClosed.store(false);
		m_browser->GetHost()->CloseBrowser(true);
		// CEF owns its UI thread because multi_threaded_message_loop is enabled.
		// CefDoMessageLoopWork is mutually exclusive with that mode and can race
		// renderer/GPU teardown.  Keep only the native Win32 queue responsive while
		// OnBeforeClose completes asynchronously on CEF's thread.
		const ULONGLONG deadline = GetTickCount64() + 2000;
		while (!m_browserClosed.load() && GetTickCount64() < deadline) {
			MSG message{};
			while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
				if (message.message == WM_QUIT) {
					PostQuitMessage(static_cast<int>(message.wParam));
					break;
				}
				TranslateMessage(&message);
				DispatchMessageW(&message);
			}
			Sleep(1);
		}
		if (!m_browserClosed.load()) traceLog("cef-browser-close-timeout");
		else traceLog("cef-browser-close-complete");
		m_devToolsRegistration = nullptr;
	}
	void resize() { if (m_browser) m_browser->GetHost()->WasResized(); }
	void load(const std::string &videoId)
	{
		std::string safe;
		for (char c : videoId) if (isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') safe.push_back(c);
		if (safe.empty()) return;
		const bool canReusePlayer = m_browser && m_active.load() && !m_videoId.empty();
		m_videoId = safe; m_active.store(true); m_endedSent = false; m_errorSent = false; m_loadingVideo.store(true);
		m_positionMs.store(0); m_durationMs.store(0);
		// CEF is created lazily only while YouTube owns playback. Local and external
		// providers therefore carry no idle CEF browser/compositor surface.
		if (!m_browser) {
			m_pendingLoad = true;
			if (!m_parent || !initialise(m_parent)) m_pendingLoad = false;
			if (m_parent) InvalidateRect(m_parent, nullptr, FALSE);
			return;
		}
		m_browser->GetHost()->WasHidden(false);
		if (canReusePlayer) command("LOAD_VIDEO", safe);
		else if (m_browser) loadCurrent();
		if (m_parent) InvalidateRect(m_parent, nullptr, FALSE);
	}
	void hide()
	{
		m_active.store(false);
		// Destroy the OSR browser rather than retaining an idle Chromium composition
		// surface. It will be recreated lazily if YouTube is selected again.
		closeBrowserAndWait();
		{
			std::lock_guard<std::mutex> lock(m_paintMutex);
			m_frame.reset();
		}
		if (m_parent) InvalidateRect(m_parent, nullptr, FALSE);
	}
	void command(const std::string &action, const std::string &argument = {})
	{
		if (!m_browser || !m_active.load()) return;
		CefPostTask(TID_UI, new CommandTask(this, action, argument));
	}
	bool active() const { return m_active.load(); }
	void pollStatus()
	{
		if (!m_browser || !active()) return;
		CefPostTask(TID_UI, new PollTask(this));
	}
	std::wstring title() const { std::lock_guard<std::mutex> lock(m_statusMutex); return m_title.empty() ? L"YouTube" : m_title; }
	std::wstring artist() const { std::lock_guard<std::mutex> lock(m_statusMutex); return m_artist; }
	std::wstring state() const { std::lock_guard<std::mutex> lock(m_statusMutex); return m_state; }
	float position() const { return float(m_positionMs.load()) / 1000.0f; }
	float duration() const { return float(m_durationMs.load()) / 1000.0f; }
	bool playing() const { std::lock_guard<std::mutex> lock(m_statusMutex); return m_state == L"playing"; }
	std::vector<std::string> takeEvents()
	{
		std::lock_guard<std::mutex> lock(m_eventMutex); std::vector<std::string> result; result.swap(m_events); return result;
	}
	void paintTo(HDC dc)
	{
		if (!active() || !m_parent) return;
		std::shared_ptr<const VideoFrame> frame;
		{
			std::lock_guard<std::mutex> lock(m_paintMutex);
			frame = m_frame;
		}
		if (!frame || frame->pixels.empty()) return;
		RECT target = youtubeVideoBounds(m_parent);
		BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		info.bmiHeader.biWidth = frame->width; info.bmiHeader.biHeight = -frame->height;
		info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
		StretchDIBits(dc, target.left, target.top, target.right - target.left, target.bottom - target.top,
			0, 0, frame->width, frame->height, frame->pixels.data(), &info, DIB_RGB_COLORS, SRCCOPY);
	}
	bool sendMouse(UINT message, WPARAM wParam, LPARAM lParam)
	{
		if (!m_browser || !active() || !m_parent) return false;
		RECT target = youtubeVideoBounds(m_parent);
		POINT point{static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam))};
		if (!PtInRect(&target, point)) return false;
		CefMouseEvent event; event.x = point.x - target.left; event.y = point.y - target.top; event.modifiers = 0;
		if (wParam & MK_LBUTTON) event.modifiers |= EVENTFLAG_LEFT_MOUSE_BUTTON;
		if (message == WM_MOUSEMOVE) m_browser->GetHost()->SendMouseMoveEvent(event, false);
		else m_browser->GetHost()->SendMouseClickEvent(event, MBT_LEFT, message == WM_LBUTTONUP, 1);
		return true;
	}

	void GetViewRect(CefRefPtr<CefBrowser>, CefRect &rect) override
	{
		RECT bounds = youtubeVideoBounds(m_parent);
		rect = CefRect(0, 0, std::max(1L, bounds.right - bounds.left), std::max(1L, bounds.bottom - bounds.top));
	}
	void OnPaint(CefRefPtr<CefBrowser>, PaintElementType type, const RectList &, const void *buffer, int width, int height) override
	{
		if (type != PET_VIEW || !buffer || width <= 0 || height <= 0) return;
		auto frame = std::make_shared<VideoFrame>();
		frame->width = width; frame->height = height;
		frame->pixels.resize(static_cast<size_t>(width) * height);
		std::memcpy(frame->pixels.data(), buffer, frame->pixels.size() * sizeof(uint32_t));
		{
			std::lock_guard<std::mutex> lock(m_paintMutex);
			m_frame = std::move(frame);
		}
		if (m_parent) {
			RECT target = youtubeVideoBounds(m_parent);
			InvalidateRect(m_parent, &target, FALSE);
		}
	}
	void OnAfterCreated(CefRefPtr<CefBrowser> browser) override
	{
		traceLog("cef-browser-after-created");
		m_browser = browser;
		// The browser is born on about:blank. Do not keep a GPU-backed OSR surface
		// active until YouTube is actually selected.
		browser->GetHost()->WasHidden(!m_active.load());
		browser->GetHost()->SetFocus(m_active.load());
		m_devToolsRegistration = browser->GetHost()->AddDevToolsMessageObserver(this);
		if (m_pendingLoad) { m_pendingLoad = false; loadCurrent(); }
	}
	void OnBeforeClose(CefRefPtr<CefBrowser>) override { traceLog("cef-browser-before-close"); m_devToolsRegistration = nullptr; m_browser = nullptr; m_browserClosed.store(true); }
	void OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int) override
	{
		(void)browser; (void)frame;
	}
	bool OnConsoleMessage(CefRefPtr<CefBrowser>, cef_log_severity_t, const CefString &message,
		const CefString &, int) override
	{
		const std::string value = message.ToString();
		if (value.rfind("RSSTATUS|", 0) != 0) return false;
		handleStatus(value); return true;
	}
	void OnTitleChange(CefRefPtr<CefBrowser>, const CefString &titleValue) override
	{
		const std::string value = titleValue.ToString();
		if (value.rfind("RSSTATUS|", 0) != 0) return;
		handleStatus(value);
	}
	void OnDevToolsMethodResult(CefRefPtr<CefBrowser>, int messageId, bool success,
		const void *result, size_t resultSize) override
	{
		if (messageId < 1000 || messageId >= 1000000) return;
		if (!success || !result || resultSize == 0) return;
		CefRefPtr<CefValue> root = CefParseJSON(std::string(static_cast<const char *>(result), resultSize), JSON_PARSER_RFC);
		if (!root || root->GetType() != VTYPE_DICTIONARY) return;
		CefRefPtr<CefDictionaryValue> value = root->GetDictionary();
		if (!value || !value->HasKey("result")) return;
		CefRefPtr<CefDictionaryValue> evaluated = value->GetDictionary("result");
		if (!evaluated || !evaluated->HasKey("value")) return;
		const std::string payload = evaluated->GetString("value").ToString();
		if (payload.rfind("RSSTATUS|", 0) == 0) handleStatus(payload);
	}
private:
	struct VideoFrame {
		int width = 0, height = 0;
		std::vector<uint32_t> pixels;
	};
	class HtmlResourceHandler final : public CefResourceHandler {
	public:
		explicit HtmlResourceHandler(std::string html) : m_html(std::move(html)) {}
		bool Open(CefRefPtr<CefRequest>, bool &handleRequest, CefRefPtr<CefCallback>) override { handleRequest = true; return true; }
		void GetResponseHeaders(CefRefPtr<CefResponse> response, int64_t &length, CefString &) override
		{
			response->SetStatus(200); response->SetStatusText("OK"); response->SetMimeType("text/html");
			response->SetHeaderByName("Cache-Control", "no-store", true); length = static_cast<int64_t>(m_html.size());
		}
		bool Read(void *output, int bytesToRead, int &bytesRead, CefRefPtr<CefResourceReadCallback>) override
		{
			const size_t count = std::min(static_cast<size_t>(std::max(0, bytesToRead)), m_html.size() - m_offset);
			if (count == 0) { bytesRead = 0; return false; }
			std::memcpy(output, m_html.data() + m_offset, count); m_offset += count; bytesRead = static_cast<int>(count); return true;
		}
		void Cancel() override {}
	private:
		std::string m_html; size_t m_offset = 0;
		IMPLEMENT_REFCOUNTING(HtmlResourceHandler);
	};
	class PollTask final : public CefTask {
	public:
		explicit PollTask(CefRefPtr<CefYouTubePlayer> owner) : m_owner(owner) {}
		void Execute() override { if (m_owner) m_owner->pollOnUi(); }
	private:
		CefRefPtr<CefYouTubePlayer> m_owner;
		IMPLEMENT_REFCOUNTING(PollTask);
	};
	class CommandTask final : public CefTask {
	public:
		CommandTask(CefRefPtr<CefYouTubePlayer> owner, std::string action, std::string argument)
			: m_owner(owner), m_action(std::move(action)), m_argument(std::move(argument)) {}
		void Execute() override { if (m_owner) m_owner->commandOnUi(m_action, m_argument); }
	private:
		CefRefPtr<CefYouTubePlayer> m_owner;
		std::string m_action, m_argument;
		IMPLEMENT_REFCOUNTING(CommandTask);
	};
	void pollOnUi()
	{
		if (!m_browser || !active()) return;
		CefRefPtr<CefDictionaryValue> params = CefDictionaryValue::Create();
		params->SetString("expression", R"JS((()=>{const p=window.rsPlayer;if(!p||typeof p.getPlayerState!=='function')return '';const clean=s=>String(s||'').replace(/[\t\r\n|]+/g,' ').trim();const d=p.getVideoData()||{};const c=p.getPlayerState();const s=window.rsLastError?'error':(c===0?'ended':(c===1?'playing':(c===2?'paused':'stopped')));return ['RSSTATUS',s,Math.round((p.getCurrentTime()||0)*1000),Math.round((p.getDuration()||0)*1000),clean(d.title||'YouTube'),clean(d.author||'YouTube'),String(window.rsLastError||'')].join('|');})())JS");
		params->SetBool("returnByValue", true);
		const int id = m_nextPollId.fetch_add(1);
		m_browser->GetHost()->ExecuteDevToolsMethod(id, "Runtime.evaluate", params);
	}
	void commandOnUi(const std::string &action, const std::string &argument)
	{
		if (!m_browser || !active()) return;
		std::string operation;
		if (action == "PLAY") operation = "p.playVideo();";
		else if (action == "PAUSE") operation = "p.pauseVideo();";
		else if (action == "STOP") operation = "p.pauseVideo();p.seekTo(0,true);";
		else if (action == "RESTART") operation = "p.seekTo(0,true);p.playVideo();";
		else if (action == "SEEK") operation = "p.seekTo(" + std::to_string(std::max<int64_t>(0, _strtoi64(argument.c_str(), nullptr, 10)) / 1000.0) + ",true);";
		else if (action == "LOAD_VIDEO") {
			std::string safe; for (char c : argument) if (isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') safe.push_back(c);
			if (safe.empty()) return;
			operation = "window.rsLastError=0;p.loadVideoById('" + safe + "');";
		}
		else return;
		m_browser->GetMainFrame()->ExecuteJavaScript(
			"(()=>{const p=window.rsPlayer;if(!p)return false;" + operation + "return true;})()",
			m_browser->GetMainFrame()->GetURL(), 0);
	}
	void handleStatus(const std::string &value)
	{
		std::vector<std::string> fields; size_t start = 9;
		while (start <= value.size()) { const size_t end = value.find('|', start); fields.push_back(value.substr(start, end - start)); if (end == std::string::npos) break; start = end + 1; }
		if (fields.size() < 5) return;
		if (m_loadingVideo.load()) {
			if (fields[0] != "playing" && fields[0] != "error") return;
			m_loadingVideo.store(false);
		}
		if (fields[0] == "error") {
			if (!m_errorSent) { m_errorSent = true; pushEvent("EVENT\tyoutube-error\t" + m_videoId + "\t" + (fields.size() > 5 ? fields[5] : "unknown") + "\n"); }
			return;
		}
		m_positionMs.store(_strtoi64(fields[1].c_str(), nullptr, 10));
		m_durationMs.store(_strtoi64(fields[2].c_str(), nullptr, 10));
		{
			std::lock_guard<std::mutex> lock(m_statusMutex);
			m_state = utf8ToWide(fields[0] == "ended" ? "stopped" : fields[0]);
			m_title = utf8ToWide(fields[3]); m_artist = utf8ToWide(fields[4]);
		}
		pushEvent("STATUS\t" + (fields[0] == "ended" ? std::string("stopped") : fields[0]) + "\t" + fields[1] + "\t" + fields[2] + "\tyoutube:" + m_videoId + "\n");
		if (fields[3] != m_lastTitle || fields[4] != m_lastArtist) {
			m_lastTitle = fields[3]; m_lastArtist = fields[4];
			pushEvent("EVENT\tyoutube-metadata\t" + fields[3] + "\t" + fields[4] + "\t" + fields[2] + "\n");
		}
		if (fields[0] == "ended" && !m_endedSent) { m_endedSent = true; pushEvent("EVENT\tyoutube-ended\t" + m_videoId + "\n"); }
		else if (fields[0] != "ended") m_endedSent = false;
		if (m_parent) {
			RECT client{}; GetClientRect(m_parent, &client);
			RECT playbackRegion{0, std::max(0L, client.bottom - 86), client.right, client.bottom};
			InvalidateRect(m_parent, &playbackRegion, FALSE);
		}
	}
	void pushEvent(std::string event) { std::lock_guard<std::mutex> lock(m_eventMutex); m_events.push_back(std::move(event)); }
	void loadCurrent(int64_t startSeconds = 0)
	{
		if (!m_browser || m_videoId.empty()) return;
		const std::string start = std::to_string(std::max<int64_t>(0, startSeconds));
		const std::string html = "<!doctype html><html><head><meta charset='utf-8'><style>html,body,#player{margin:0;width:100%;height:100%;overflow:hidden;background:#000}iframe{width:100%;height:100%}</style></head><body><div id='player'></div><script src='https://www.youtube.com/iframe_api'></script><script>window.rsPlayer=null;window.rsLastError=0;function onYouTubeIframeAPIReady(){window.rsPlayer=new YT.Player('player',{videoId:'" + m_videoId + "',playerVars:{autoplay:1,controls:1,playsinline:1,rel:0,start:" + start + ",origin:'https://rearsilver.local',widget_referrer:'https://rearsilver.local/'},events:{onReady:function(e){e.target.playVideo();},onStateChange:function(e){if(e.data===1)window.rsLastError=0;},onError:function(e){window.rsLastError=e.data||1;}}});}</script></body></html>";
		{
			std::lock_guard<std::mutex> lock(m_wrapperMutex); m_wrapperHtml = html;
		}
		m_browser->GetMainFrame()->LoadURL("https://rearsilver.local/player?v=" + m_videoId + "&start=" + start);
	}
	HWND m_parent = nullptr; CefRefPtr<CefBrowser> m_browser;
	std::atomic<bool> m_browserClosed{true};
	CefRefPtr<CefRegistration> m_devToolsRegistration;
	std::atomic<bool> m_active{false};
	std::atomic<int> m_nextPollId{1000};
	std::atomic<int64_t> m_positionMs{0}, m_durationMs{0};
	std::atomic<bool> m_loadingVideo{false}; bool m_pendingLoad = false, m_endedSent = false, m_errorSent = false;
	std::string m_videoId, m_lastTitle, m_lastArtist;
	std::mutex m_wrapperMutex; std::string m_wrapperHtml;
	mutable std::mutex m_statusMutex; std::wstring m_title = L"YouTube", m_artist, m_state = L"stopped";
	std::mutex m_eventMutex, m_paintMutex; std::vector<std::string> m_events;
	std::shared_ptr<const VideoFrame> m_frame;
	IMPLEMENT_REFCOUNTING(CefYouTubePlayer);
};

static Player *g_player = nullptr;
static CefRefPtr<CefYouTubePlayer> g_youtubePlayer;
static SystemMediaProvider g_systemMedia;
static SystemMediaState g_externalState;
static int64_t g_externalPositionAnchorMs = 0;
static ULONGLONG g_externalPositionAnchorTick = 0;
static ULONGLONG g_externalCommandGraceUntilTick = 0;
static std::string g_externalPositionTrack;
static SpotifyClient g_spotify;
static std::mutex g_recentSpotifyRequestMutex;
static std::unordered_map<std::string, ULONGLONG> g_recentSpotifyRequests;
static constexpr ULONGLONG kSpotifyRequestPropagationGraceMs = 15000;
static TwitchAccount g_streamerTwitch("streamer"), g_botTwitch("bot");
static TwitchChatService g_twitchReader("reader"), g_twitchSender("sender");
static bool g_closeRequested = false;
static int g_page = RsBeta::currentState().expired ? 9 : 7;
static std::string g_streamerAuthState = "disconnected", g_streamerLogin;
static std::string g_botAuthState = "disconnected", g_botLogin, g_authSender = "streamer";
static std::string g_obsStudioVersion, g_pluginVersion;
static bool g_captureExists = false, g_playerAutoStart = false, g_hostPipeConnected = false,
	g_replayBufferActive = false, g_replaySceneExists = false, g_replayPlaced = false,
	g_replayVisible = false, g_replayPlaying = false, g_replayConflict = false, g_replaySetupComplete = false;
static std::string g_replayMessage;
static bool g_quickTextSourceExists = false, g_quickTextPlaced = false,
	g_quickTextVisible = false, g_quickTextConflict = false, g_quickTextSetupComplete = false;
static std::string g_quickTextMessage = "Quick Text has not been added to OBS yet.";
static bool g_timerSourceExists = false, g_timerPlaced = false, g_timerVisible = false, g_timerConflict = false,
	g_timerSetupComplete = false;
static std::string g_timerMessage = "Set up the managed Timer source before starting it.";
static bool g_musicOverlaySourceExists = false, g_musicOverlayPlaced = false, g_musicOverlayVisible = false,
	g_musicOverlayConflict = false, g_musicOverlaySetupComplete = false;
static std::string g_musicOverlayMessage = "Music Overlay has not been added to OBS yet.";
static std::string g_overlayPlacementMode = "advanced";
struct ReplayPreviewGeometry {
	bool available = false;
	int width = 0, height = 0, scalePercent = 0, titlePixelSize = 0;
	int border = 0, outerRadius = 0, innerRadius = 0;
	int apertureX = 0, apertureY = 0, apertureWidth = 0, apertureHeight = 0;
	int titleX = 0, titleY = 0, titleWidth = 0, titleHeight = 0;
};
static ReplayPreviewGeometry g_replayPreviewGeometry;
static std::atomic<uint64_t> g_accountsRevision{0};
static unsigned long long g_sessionRequestNumber = 1;
static bool externalActive();
static std::wstring musicSetting(const wchar_t *name, const wchar_t *fallback);
static bool sendTwitchMessage(const std::string &message)
{
	if (g_authSender == "bot" && g_twitchSender.connected() && g_twitchSender.sendMessage(message))
		return true;
	return g_twitchReader.connected() && g_twitchReader.sendMessage(message);
}

static std::string nowPlayingAnnouncement(const HubTrack &track)
{
	const std::string symbol = wideToUtf8(musicSetting(L"nowPlayingSymbol", L"▶️"));
	return (symbol.empty() ? "" : symbol + " ") + "Now Playing: " + track.title + " - " + track.artist +
	       " - requested by " + track.requestedBy + ".";
}

static void syncHubQueueView()
{
	g_queue.clear();
	if (g_hub.hasCurrent()) {
		const HubTrack current = g_hub.current();
		g_queue.push_back({utf8ToWide(current.title), utf8ToWide(current.artist), L"Now playing", current.durationSeconds, true});
	}
	for (const HubTrack &track : g_hub.playbackOrder())
		g_queue.push_back({utf8ToWide(track.title), utf8ToWide(track.artist),
			track.request ? L"Request" : (track.provider == "local" ? L"Local library" : L"Fallback playlist"), track.durationSeconds, false});
	if (externalActive()) {
		const SpotifyClientState spotify = g_spotify.state();
		if (spotify.connected) {
			g_queue.clear();
			if (!spotify.current.title.empty()) g_queue.push_back({utf8ToWide(spotify.current.title), utf8ToWide(spotify.current.artist), L"Now playing via Spotify", int(spotify.current.durationMs / 1000), true});
			const auto requests = g_hub.requests();
			for (const SpotifyQueueTrack &track : spotify.queue) {
				std::wstring source = L"Spotify queue";
				for (const HubTrack &request : requests)
					if (request.provider == "spotify" && request.providerId == track.uri) { source = request.cancelled ? L"Removed — auto-skip" : L"Request #" + utf8ToWide(request.id); break; }
				g_queue.push_back({utf8ToWide(track.title), utf8ToWide(track.artist), std::move(source), int(track.durationMs / 1000), false});
			}
		}
	}
}

static void saveHubState();
static void setMusicSetting(const wchar_t *name, const std::wstring &value);
static std::wstring overlaySetting(const wchar_t *name, const wchar_t *fallback);
static void setOverlaySetting(const wchar_t *name, const std::wstring &value);

static std::string playerFallbackArtwork()
{
	return wideToUtf8(executableAssetPath(L"music-fallback-vinyl.png"));
}

static void writeTextOutput(const HubTrack &track);
static bool musicBool(const wchar_t *name, bool fallback);

static bool externalActive() { return g_hub.activeSource() == "external"; }
static int64_t currentExternalPositionMs()
{
	int64_t position = g_externalPositionAnchorMs;
	if (g_externalState.playing && g_externalPositionAnchorTick != 0)
		position += int64_t(GetTickCount64() - g_externalPositionAnchorTick);
	if (g_externalState.durationMs > 0) position = std::min(position, g_externalState.durationMs);
	return std::max<int64_t>(0, position);
}
static float currentPosition()
{
	return externalActive() ? float(currentExternalPositionMs()) / 1000.0f : (g_player ? g_player->position() : 0.0f);
}
static float currentDuration()
{
	return externalActive() ? float(g_externalState.durationMs) / 1000.0f : (g_player ? g_player->duration() : 0.0f);
}
static bool currentPlaying()
{
	return externalActive() ? g_externalState.playing : (g_player && g_player->state() == L"playing");
}
static std::wstring currentState()
{
	if (externalActive()) return !g_externalState.available ? L"stopped" : (g_externalState.playing ? L"playing" : L"paused");
	return g_player ? g_player->state() : L"stopped";
}

static void commandExternalPlayer(SystemMediaProvider::Action action, int64_t positionMs = 0)
{
	// GSMTC accepts transport commands quickly, but some applications publish
	// their updated playback state on the following media-session sample. Give
	// the UI immediate, reversible feedback while that authoritative sample is
	// pending so controls never appear unresponsive.
	const int64_t position = currentExternalPositionMs();
	// The Windows media session can briefly return the sample from before a
	// transport command. Keep the optimistic state long enough for Spotify to
	// publish its confirmed state instead of visibly undoing the click.
	g_externalCommandGraceUntilTick = GetTickCount64() + 1500;
	if (action == SystemMediaProvider::Action::Play) {
		g_externalState.playing = true;
		g_externalPositionAnchorMs = position;
		g_externalPositionAnchorTick = GetTickCount64();
	} else if (action == SystemMediaProvider::Action::Pause) {
		g_externalState.playing = false;
		g_externalPositionAnchorMs = position;
		g_externalPositionAnchorTick = GetTickCount64();
	} else if (action == SystemMediaProvider::Action::Restart) {
		g_externalState.positionMs = 0;
		g_externalPositionAnchorMs = 0;
		g_externalPositionAnchorTick = GetTickCount64();
	} else if (action == SystemMediaProvider::Action::Next || action == SystemMediaProvider::Action::Previous) {
		g_externalState.positionMs = 0;
		g_externalPositionAnchorMs = 0;
		g_externalPositionAnchorTick = GetTickCount64();
	} else if (action == SystemMediaProvider::Action::Seek) {
		g_externalState.positionMs = positionMs;
		g_externalPositionAnchorMs = positionMs;
		g_externalPositionAnchorTick = GetTickCount64();
	}
	g_systemMedia.command(action, positionMs);
}

static bool startHubTrack(const HubTrack &track, bool record = true)
{
	if (track.providerId.empty()) return false;
	if (record) g_hub.recordStarted(track);
	if (track.provider == "local") {
		if (g_youtubePlayer) { g_youtubePlayer->command("STOP"); g_youtubePlayer->hide(); }
		if (!g_player) return false;
		g_player->setMetadata(track.title + "\t" + track.artist + "\t" + track.album + "\t" +
			(track.artworkUrl.empty() ? playerFallbackArtwork() : track.artworkUrl));
		const std::string result = g_player->command("LOAD\t" + track.providerId);
		if (result.rfind("ERROR\t", 0) == 0) return false;
	} else {
		if (g_player) g_player->suspend();
		g_youtubePlayer->load(track.providerId);
	}
	g_queuePage = 0; syncHubQueueView();
	writeTextOutput(track);
	if (musicBool(L"announceTrackChanges", false)) {
		sendTwitchMessage(nowPlayingAnnouncement(track));
		std::lock_guard<std::mutex> lock(g_hostEventMutex);
		g_hostEvents.push_back("HOST\tNOW_PLAYING\t" + track.title + "\t" + track.artist + "\t" + track.requestedBy + "\n");
	}
	return true;
}

static bool playHubNext()
{
	HubTrack track;
	if (!g_hub.takeNext(track)) { g_hub.clearCurrent(); syncHubQueueView(); return false; }
	const bool started = startHubTrack(track); saveHubState(); return started;
}

static void refreshExternalPlayer(HWND window)
{
	if (g_hub.activeSource() != "external") return;
	SystemMediaState state = g_systemMedia.state();
	const std::string trackKey = state.sourceAppId + "\n" + state.title + "\n" + state.artist;
	const bool trackChanged = trackKey != g_externalPositionTrack;
	const ULONGLONG now = GetTickCount64();
	const bool commandPending = now < g_externalCommandGraceUntilTick;
	if (commandPending && !trackChanged)
		state.playing = g_externalState.playing;
	const bool playbackChanged = state.playing != g_externalState.playing;
	const int64_t projectedPosition = currentExternalPositionMs();
	const int64_t drift = state.positionMs - projectedPosition;
	// GSMTC positions are snapshots rather than a continuously advancing
	// clock. Correct meaningful seeks/desync, but never redraw around routine
	// sub-second sampling jitter.
	const int64_t driftTolerance = state.playing ? 2500 : 750;
	const bool meaningfulDrift = !commandPending && std::llabs(drift) > driftTolerance;
	if (trackChanged || (!commandPending && playbackChanged) || meaningfulDrift || g_externalPositionAnchorTick == 0) {
		g_externalPositionAnchorMs = state.positionMs;
		g_externalPositionAnchorTick = now;
		g_externalPositionTrack = trackKey;
	}
	g_externalState = state;
	if (!state.available || state.title.empty()) {
		if (g_hub.hasCurrent() && g_hub.current().provider == "external") {
			g_hub.clearCurrent();
			if (g_player) g_player->setMetadata("No track playing\t\t\t");
			syncHubQueueView();
			saveHubState();
			InvalidateRect(window, nullptr, FALSE);
		}
		return;
	}
	HubTrack track;
	track.id = "external:" + state.sourceAppId + ":" + state.title + ":" + state.artist;
	track.providerId = state.sourceAppId; track.provider = "external";
	track.title = state.title; track.artist = state.artist; track.album = state.album;
	track.artworkUrl = state.artworkPath.empty() ? playerFallbackArtwork() : state.artworkPath;
	track.requestedBy = wideToUtf8(musicSetting(L"nonRequestLabel", L"Stream DJ"));
	track.durationSeconds = int(state.durationMs / 1000);
	const bool changed = !g_hub.hasCurrent() || g_hub.current().id != track.id;
	if (changed) {
		g_hub.recordStarted(track); syncHubQueueView();
		if (g_player) g_player->setMetadata(track.title + "\t" + track.artist + "\t" + track.album + "\t" + track.artworkUrl);
		writeTextOutput(track);
		if (musicBool(L"announceTrackChanges", false)) {
			sendTwitchMessage(nowPlayingAnnouncement(track));
			std::lock_guard<std::mutex> lock(g_hostEventMutex);
			g_hostEvents.push_back("HOST\tNOW_PLAYING\t" + track.title + "\t" + track.artist + "\t" + track.requestedBy + "\n");
		}
		saveHubState();
	}
	InvalidateRect(window, nullptr, FALSE);
}

static std::wstring hubStatePath()
{
	wchar_t appData[MAX_PATH]{}; if (!GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH)) return {};
	std::wstring folder = std::wstring(appData) + L"\\RearSilver Stream Suite"; CreateDirectoryW(folder.c_str(), nullptr);
	return folder + L"\\music-hub.json";
}

static void saveHubState()
{
	const std::wstring path = hubStatePath(); if (path.empty()) return;
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	if (output) output << g_hub.persistentJson();
}

static void loadHubState()
{
	const std::wstring path = hubStatePath(); std::ifstream input(path, std::ios::binary);
	if (!input) return; const std::string json((std::istreambuf_iterator<char>(input)), {});
	CefRefPtr<CefValue> root = CefParseJSON(json, JSON_PARSER_RFC);
	if (!root || root->GetType() != VTYPE_DICTIONARY) return;
	CefRefPtr<CefDictionaryValue> object = root->GetDictionary();
	auto readTrack = [](CefRefPtr<CefDictionaryValue> value) {
		HubTrack track; if (!value) return track;
		track.id = value->GetString("id").ToString(); track.providerId = value->GetString("providerId").ToString();
		track.provider = value->GetString("provider").ToString(); if (track.provider.empty()) track.provider = "youtube";
		track.title = value->GetString("title").ToString(); track.artist = value->GetString("artist").ToString();
		track.album = value->GetString("album").ToString(); track.artworkUrl = value->GetString("artworkUrl").ToString();
		track.requestedBy = value->GetString("requestedBy").ToString(); track.requesterId = value->GetString("requesterId").ToString();
		track.requesterLevel = value->GetInt("requesterLevel"); track.durationSeconds = value->GetInt("durationSeconds");
		track.discNumber = value->GetInt("discNumber"); track.trackNumber = value->GetInt("trackNumber");
		track.request = value->GetBool("request"); track.cancelled = value->GetBool("cancelled"); return track;
	};
	auto readTracks = [&readTrack](CefRefPtr<CefListValue> list) {
		std::vector<HubTrack> tracks; if (!list) return tracks;
		for (size_t i = 0; i < list->GetSize(); ++i) {
			HubTrack track = readTrack(list->GetDictionary(i));
			if (!track.providerId.empty()) tracks.push_back(std::move(track));
		} return tracks;
	};
	std::vector<HubTrack> youtube = readTracks(object->GetList("youtubeLibrary"));
	if (youtube.empty()) {
		for (HubTrack &track : readTracks(object->GetList("queue")))
			if (!track.request && track.provider == "youtube") youtube.push_back(std::move(track));
	}
	g_hub.replaceFallback(std::move(youtube), object->GetString("fallbackLabel").ToString(), object->GetString("fallbackUrl").ToString());
	g_hub.replaceLocalLibrary(readTracks(object->GetList("localLibrary")));
	g_hub.clearRequests();
	HubTrack youtubeContinuation = readTrack(object->GetDictionary("youtubeContinuation"));
	HubTrack localContinuation = readTrack(object->GetDictionary("localContinuation"));
	const HubTrack legacyCurrent = readTrack(object->GetDictionary("current"));
	if (!legacyCurrent.request && !legacyCurrent.providerId.empty()) {
		if (legacyCurrent.provider == "local" && localContinuation.providerId.empty()) localContinuation = legacyCurrent;
		if (legacyCurrent.provider == "youtube" && youtubeContinuation.providerId.empty()) youtubeContinuation = legacyCurrent;
	}
	if (!youtubeContinuation.providerId.empty()) g_hub.restoreFallbackPosition(youtubeContinuation);
	if (!localContinuation.providerId.empty()) g_hub.restoreFallbackPosition(localContinuation);
	const std::string savedSource = object->GetString("activeSource").ToString();
	const std::string activeSource = savedSource == "local" || savedSource == "external" ? savedSource : "youtube";
	g_hub.activateSource(activeSource);
	HubTrack current = activeSource == "local" ? localContinuation :
		(activeSource == "youtube" ? youtubeContinuation : HubTrack{});
	const bool continueFallback = musicSetting(L"fallbackStartup", L"continue") != L"beginning";
	const bool shuffleFallback = musicSetting(L"fallbackOrder", L"ordered") == L"shuffle";
	if (activeSource != "external")
		g_hub.prepareFallbackForLaunch(shuffleFallback, continueFallback && !current.providerId.empty() ? &current : nullptr);
	if (continueFallback && !current.providerId.empty()) {
		g_hub.restoreCurrent(current);
		if (g_player) g_player->setMetadata(current.title + "\t" + current.artist + "\t" + current.album + "\t" +
			(current.artworkUrl.empty() ? playerFallbackArtwork() : current.artworkUrl));
	}
	syncHubQueueView(); g_libraryStatus = L"Restored the saved YouTube and local libraries.";
}

static void positionLibraryControls(HWND window)
{
	if (!g_playlistEdit || !g_importPlaylistButton) return;
	RECT client{}; GetClientRect(window, &client);
	const bool visible = false;
	ShowWindow(g_playlistEdit, SW_HIDE);
	for (HWND control : {g_importPlaylistButton, g_addFilesButton, g_addFolderButton, g_useLocalButton, g_useYouTubeButton, g_clearLocalButton})
		ShowWindow(control, SW_HIDE);
	if (!visible) return;
	const int sidebar = sidebarWidthFor(client.right);
	const int x = sidebar + 56, width = std::max(240, int(client.right) - x - 56);
	MoveWindow(g_playlistEdit, x, 205, width, 34, TRUE);
	MoveWindow(g_importPlaylistButton, x, 249, std::min(260, width), 36, TRUE);
	const int half = std::max(120, (width - 12) / 2);
	MoveWindow(g_addFilesButton, x, 440, half, 36, TRUE); MoveWindow(g_addFolderButton, x + half + 12, 440, half, 36, TRUE);
	MoveWindow(g_useLocalButton, x, 486, half, 36, TRUE); MoveWindow(g_useYouTubeButton, x + half + 12, 486, half, 36, TRUE);
	MoveWindow(g_clearLocalButton, x, 532, std::min(260, width), 36, TRUE);
}

static void updateOverlayDesignerSurface();

static void positionOverlayControls(HWND window)
{
	if (!g_overlayCustomTextEdit) return;
	ShowWindow(g_overlayCustomTextEdit, SW_HIDE); ShowWindow(g_overlayFontCombo, SW_HIDE);
	for (HWND edit : g_overlayStyleEdits) ShowWindow(edit, SW_HIDE);
	updateOverlayDesignerSurface();
}

static std::wstring clockText(float seconds)
{
	const int total = std::max(0, int(seconds));
	wchar_t text[32]; swprintf_s(text, L"%d:%02d", total / 60, total % 60); return text;
}

static RECT g_transportButtons[5]{};
static int g_transportPressed = -1;
static ULONGLONG g_transportPressedUntil = 0;
static RECT g_sidebarToggle{};
static RECT g_betaNoticeRect{};
static bool g_betaTooltipVisible = false;
static bool g_trackingMouseLeave = false;
static RECT g_overlayOptions[10]{};
static RECT g_overlayReset{};
static RECT g_overlayTabs[2]{};
static RECT g_overlayStyleOptions[10]{};

static constexpr const wchar_t *kOverlayRegistry =
	L"Software\\RearSilver\\RearSilver-Stream-Suite\\music\\overlay\\main";
static constexpr const wchar_t *kMusicSettingsRegistry = L"Software\\RearSilver\\RearSilver-Stream-Suite\\music";

static std::wstring directWriteLocalizedName(IDWriteLocalizedStrings *names)
{
	if (!names || names->GetCount() == 0) return {};
	UINT32 index = 0;
	BOOL exists = FALSE;
	wchar_t locale[LOCALE_NAME_MAX_LENGTH]{};
	if (GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH))
		names->FindLocaleName(locale, &index, &exists);
	if (!exists) names->FindLocaleName(L"en-gb", &index, &exists);
	if (!exists) names->FindLocaleName(L"en-us", &index, &exists);
	if (!exists) index = 0;
	UINT32 length = 0;
	if (FAILED(names->GetStringLength(index, &length))) return {};
	std::wstring name(size_t(length) + 1, L'\0');
	if (FAILED(names->GetString(index, name.data(), length + 1))) return {};
	name.resize(length);
	return name;
}

static ComPtr<IDWriteFont> directWriteRepresentativeFont(IDWriteFontFamily *family)
{
	ComPtr<IDWriteFont> font;
	if (family) family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL,
		DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &font);
	return font;
}

static std::wstring directWriteFamilyName(IDWriteFontFamily *family, IDWriteFont *font)
{
	if (!family) return {};
	if (font) {
		ComPtr<IDWriteLocalizedStrings> typographicNames;
		BOOL exists = FALSE;
		if (SUCCEEDED(font->GetInformationalStrings(
			DWRITE_INFORMATIONAL_STRING_TYPOGRAPHIC_FAMILY_NAMES, &typographicNames, &exists)) &&
			exists && typographicNames) {
			const std::wstring name = directWriteLocalizedName(typographicNames.Get());
			if (!name.empty()) return name;
		}
	}
	ComPtr<IDWriteLocalizedStrings> familyNames;
	if (FAILED(family->GetFamilyNames(&familyNames)) || !familyNames) return {};
	return directWriteLocalizedName(familyNames.Get());
}

static bool directWriteFontRendersText(IDWriteFont *font)
{
	if (!font) return false;
	ComPtr<IDWriteFontFace> face;
	if (FAILED(font->CreateFontFace(&face)) || !face) return false;
	static constexpr UINT32 characters[] = {L'A', L'a', L'0'};
	UINT16 glyphs[3]{};
	if (FAILED(face->GetGlyphIndices(characters, 3, glyphs))) return false;
	return glyphs[0] != 0 || glyphs[1] != 0 || glyphs[2] != 0;
}

static bool directWriteFontMatchesLocale(IDWriteFont *font, IDWriteGdiInterop *gdiInterop, BYTE systemCharset)
{
	if (!font || !gdiInterop) return false;
	LOGFONTW logFont{};
	BOOL isSystemFont = FALSE;
	if (FAILED(gdiInterop->ConvertFontToLOGFONT(font, &logFont, &isSystemFont))) return false;
	if (logFont.lfCharSet == SYMBOL_CHARSET) return false;
	return logFont.lfCharSet == DEFAULT_CHARSET || logFont.lfCharSet == ANSI_CHARSET ||
		logFont.lfCharSet == systemCharset;
}

static bool containsFontFamily(const std::vector<std::wstring> &families, const std::wstring &candidate)
{
	return std::any_of(families.begin(), families.end(), [&](const std::wstring &family) {
		return _wcsicmp(family.c_str(), candidate.c_str()) == 0;
	});
}

static std::wstring fontWeightBaseFamily(const std::wstring &family)
{
	static constexpr const wchar_t *weightSuffixes[] = {
		L"BdCn BT", L"Bk BT", L"Hv BT", L"LtCn BT", L"Lt BT",
		L"MdCn BT", L"Md BT", L"XBlk BT",
		L"Extra Light", L"ExtraLight", L"Ultra Light", L"UltraLight",
		L"Semi Light", L"SemiLight", L"Semilight", L"Demi Light", L"DemiLight",
		L"Extra Bold", L"ExtraBold", L"Ultra Bold", L"UltraBold",
		L"Semi Bold", L"SemiBold", L"Semibold", L"Demi Bold", L"DemiBold",
		L"Thin", L"Light", L"Regular", L"Medium", L"Bold", L"Black", L"Heavy"
	};
	for (const wchar_t *suffix : weightSuffixes) {
		const size_t suffixLength = wcslen(suffix);
		if (family.size() <= suffixLength + 1) continue;
		const size_t separator = family.size() - suffixLength - 1;
		if (family[separator] == L' ' &&
			_wcsicmp(family.c_str() + separator + 1, suffix) == 0)
			return family.substr(0, separator);
	}
	return family;
}

static const std::vector<std::wstring> &installedFontFamilies()
{
	static const std::vector<std::wstring> families = [] {
		std::vector<std::wstring> found;
		ComPtr<IDWriteFactory> factory;
		if (SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
			reinterpret_cast<IUnknown **>(factory.GetAddressOf()))) && factory) {
			ComPtr<IDWriteFontCollection> collection;
			ComPtr<IDWriteGdiInterop> gdiInterop;
			factory->GetGdiInterop(&gdiInterop);
			CHARSETINFO charsetInfo{};
			DWORD codePage = GetACP();
			const BYTE systemCharset = TranslateCharsetInfo(&codePage, &charsetInfo, TCI_SRCCODEPAGE)
				? BYTE(charsetInfo.ciCharset) : ANSI_CHARSET;
			if (SUCCEEDED(factory->GetSystemFontCollection(&collection, FALSE)) && collection && gdiInterop) {
				const UINT32 count = collection->GetFontFamilyCount();
				found.reserve(count + 1);
				for (UINT32 index = 0; index < count; ++index) {
					ComPtr<IDWriteFontFamily> family;
					if (FAILED(collection->GetFontFamily(index, &family))) continue;
					ComPtr<IDWriteFont> font = directWriteRepresentativeFont(family.Get());
					if (!directWriteFontRendersText(font.Get()) ||
						!directWriteFontMatchesLocale(font.Get(), gdiInterop.Get(), systemCharset)) continue;
					std::wstring name = directWriteFamilyName(family.Get(), font.Get());
					if (!name.empty() && name.front() != L'@') found.emplace_back(std::move(name));
				}
			}
		}
		if (found.empty()) found = {L"Arial", L"Calibri", L"Segoe UI", L"Sora"};
		if (std::none_of(found.begin(), found.end(), [](const std::wstring &family) {
			return _wcsicmp(family.c_str(), L"Sora") == 0;
		})) found.emplace_back(L"Sora");
		const std::vector<std::wstring> installed = found;
		for (std::wstring &family : found) {
			const std::wstring base = fontWeightBaseFamily(family);
			if (base != family && containsFontFamily(installed, base)) family = base;
		}
		std::sort(found.begin(), found.end(), [](const std::wstring &left, const std::wstring &right) {
			return _wcsicmp(left.c_str(), right.c_str()) < 0;
		});
		found.erase(std::unique(found.begin(), found.end(), [](const std::wstring &left, const std::wstring &right) {
			return _wcsicmp(left.c_str(), right.c_str()) == 0;
		}), found.end());
		return found;
	}();
	return families;
}

static std::wstring canonicalFontFamily(const std::wstring &family)
{
	const std::wstring base = fontWeightBaseFamily(family);
	return base != family && containsFontFamily(installedFontFamilies(), base) ? base : family;
}

static std::wstring normalisedMusicFontSetting(const wchar_t *key, const wchar_t *fallback)
{
	const std::wstring stored = musicSetting(key, fallback);
	const std::wstring canonical = canonicalFontFamily(stored);
	if (canonical != stored) setMusicSetting(key, canonical);
	return canonical;
}

static std::wstring normalisedOverlayFontSetting(const wchar_t *key, const wchar_t *fallback)
{
	const std::wstring stored = overlaySetting(key, fallback);
	const std::wstring canonical = canonicalFontFamily(stored);
	if (canonical != stored) setOverlaySetting(key, canonical);
	return canonical;
}

static void setFontFamilies(CefRefPtr<CefDictionaryValue> dictionary, const char *key)
{
	CefRefPtr<CefListValue> list = CefListValue::Create();
	const auto &families = installedFontFamilies();
	list->SetSize(families.size());
	for (size_t index = 0; index < families.size(); ++index)
		list->SetString(index, wideToUtf8(families[index]));
	dictionary->SetList(key, list);
}

static std::wstring musicSetting(const wchar_t *name,const wchar_t *fallback){HKEY key{};if(RegOpenKeyExW(HKEY_CURRENT_USER,kMusicSettingsRegistry,0,KEY_READ,&key)!=ERROR_SUCCESS)return fallback;wchar_t value[1024]{};DWORD type=0,bytes=sizeof(value);const LSTATUS result=RegQueryValueExW(key,name,nullptr,&type,reinterpret_cast<BYTE*>(value),&bytes);RegCloseKey(key);return result==ERROR_SUCCESS&&type==REG_SZ?value:fallback;}
static void setMusicSetting(const wchar_t *name,const std::wstring &value){HKEY key{};DWORD d=0;if(RegCreateKeyExW(HKEY_CURRENT_USER,kMusicSettingsRegistry,0,nullptr,0,KEY_WRITE,nullptr,&key,&d)!=ERROR_SUCCESS)return;RegSetValueExW(key,name,0,REG_SZ,reinterpret_cast<const BYTE*>(value.c_str()),DWORD((value.size()+1)*sizeof(wchar_t)));RegCloseKey(key);}
static bool musicBool(const wchar_t *name,bool fallback){const auto value=musicSetting(name,fallback?L"true":L"false");return value==L"true"||value==L"1";}
static void disableSongRequestsForLocalSource()
{
	if (!musicBool(L"requestsEnabled", true)) return;
	setMusicSetting(L"requestsEnabled", L"false");
	std::lock_guard<std::mutex> lock(g_hostEventMutex);
	g_hostEvents.push_back("HOST\tSETTING\trequestsEnabled\tfalse\n");
}
static void appendChatCommandConfig(CefRefPtr<CefDictionaryValue> config)
{
	CefRefPtr<CefListValue> roles = CefListValue::Create();
	const std::array<std::pair<std::string_view, std::string_view>, 4> roleLabels{{
		{"everyone", "Everyone"}, {"subscriber", "Subscribers"}, {"vip", "VIPs"}, {"moderator", "Moderators"},
	}};
	for (size_t index = 0; index < roleLabels.size(); ++index) {
		CefRefPtr<CefDictionaryValue> role = CefDictionaryValue::Create();
		role->SetString("id", std::string(roleLabels[index].first));
		role->SetString("label", std::string(roleLabels[index].second));
		roles->SetDictionary(index, role);
	}
	config->SetList("commandRoles", roles);

	CefRefPtr<CefListValue> commands = CefListValue::Create();
	for (size_t index = 0; index < kRsChatCommands.size(); ++index) {
		const auto &definition = kRsChatCommands[index];
		CefRefPtr<CefDictionaryValue> command = CefDictionaryValue::Create();
		command->SetString("id", std::string(definition.id));
		command->SetString("category", std::string(definition.category));
		command->SetString("label", std::string(definition.label));
		command->SetString("syntax", std::string(definition.syntax));
		command->SetString("description", std::string(definition.description));
		CefRefPtr<CefListValue> examples = CefListValue::Create();
		size_t exampleIndex = 0;
		for (const auto &entry : kRsChatCommandExamples) {
			if (entry.command != definition.id)
				continue;
			CefRefPtr<CefDictionaryValue> example = CefDictionaryValue::Create();
			example->SetString("label", std::string(entry.label));
			example->SetString("syntax", std::string(entry.syntax));
			example->SetString("provider", std::string(entry.provider));
			examples->SetDictionary(exampleIndex++, example);
		}
		command->SetList("examples", examples);
		CefRefPtr<CefListValue> notes = CefListValue::Create();
		size_t noteIndex = 0;
		for (const auto &entry : kRsChatCommandNotes)
			if (entry.command == definition.id)
				notes->SetString(noteIndex++, std::string(entry.text));
		command->SetList("notes", notes);
		CefRefPtr<CefDictionaryValue> defaults = CefDictionaryValue::Create();
		for (const auto &[role, flag] : kRsChatRoles) {
			const bool fallback = (definition.defaultRoles & flag) != 0;
			const std::string key = "command." + std::string(definition.id) + "." + std::string(role);
			config->SetBool(key, musicBool(utf8ToWide(key).c_str(), fallback));
			defaults->SetBool(std::string(role), fallback);
		}
		command->SetDictionary("defaults", defaults);
		commands->SetDictionary(index, command);
	}
	config->SetList("commandDefinitions", commands);
}
static std::wstring replaySizeStep(){const auto step=musicSetting(L"tool.replaySizeStep",L"");if(step==L"small"||step==L"medium"||step==L"large"||step==L"fullscreen")return step;const int value=_wtoi(musicSetting(L"tool.replayScale",L"80").c_str());return value<50?L"small":value<70?L"medium":value<90?L"large":L"fullscreen";}
static std::wstring replayBorderStep(){const auto step=musicSetting(L"tool.replayBorderStep",L"");if(step==L"none"||step==L"subtle"||step==L"medium"||step==L"bold"||step==L"statement")return step;if(step==L"thin")return L"subtle";if(step==L"thick")return L"bold";const int value=_wtoi(musicSetting(L"tool.replayBorderWidth",L"8").c_str());return value<2?L"none":value<6?L"subtle":value<10?L"medium":L"bold";}
static std::wstring replayRadiusStep(){const auto step=musicSetting(L"tool.replayRadiusStep",L"");if(step==L"square"||step==L"subtle"||step==L"rounded"||step==L"soft"||step==L"dramatic")return step;const int value=_wtoi(musicSetting(L"tool.replayRadius",L"20").c_str());return value<5?L"square":value<15?L"subtle":value<26?L"rounded":L"soft";}
static std::wstring replayFontWeight(){const auto weight=musicSetting(L"tool.replayFontWeight",L"bold");return weight==L"regular"||weight==L"medium"||weight==L"semibold"||weight==L"bold"||weight==L"extrabold"?weight:L"bold";}
static std::wstring overlayPlacementMode(){const auto mode=musicSetting(L"overlayPlacementMode",L"advanced");return mode==L"simple"?L"simple":L"advanced";}
static HubYouTubeSafetyOptions youtubeSafetyOptions(){HubYouTubeSafetyOptions options;const std::wstring safe=musicSetting(L"youtubeSafeSearch",L"strict");options.safeSearch=safe==L"moderate"?"moderate":"strict";options.musicOnly=musicBool(L"youtubeMusicOnly",false);options.rejectAgeRestricted=musicBool(L"youtubeRejectAgeRestricted",true);return options;}
static std::string replayConfigJson(bool frame){CefRefPtr<CefDictionaryValue>d=CefDictionaryValue::Create();if(frame){d->SetString("title",wideToUtf8(musicSetting(L"tool.replayTitle",L"INSTANT REPLAY")));d->SetString("font",wideToUtf8(normalisedMusicFontSetting(L"tool.replayFont",L"Sora")));d->SetString("fontWeight",wideToUtf8(replayFontWeight()));d->SetString("alignment",wideToUtf8(musicSetting(L"tool.replayAlignment",L"left")));d->SetString("background",wideToUtf8(musicSetting(L"tool.replayBackground",L"#0b0f14")));d->SetString("accent",wideToUtf8(musicSetting(L"tool.replayAccent",L"#00d4ff")));d->SetString("textColour",wideToUtf8(musicSetting(L"tool.replayTextColour",L"#e6e8eb")));d->SetInt("opacity",_wtoi(musicSetting(L"tool.replayOpacity",L"92").c_str()));d->SetString("sizeStep",wideToUtf8(replaySizeStep()));d->SetString("borderStep",wideToUtf8(replayBorderStep()));d->SetString("radiusStep",wideToUtf8(replayRadiusStep()));}else{d->SetInt("seconds",_wtoi(musicSetting(L"tool.replaySeconds",L"10").c_str()));d->SetBool("autoStart",musicBool(L"tool.replayAutoStart",false));d->SetBool("autoHide",musicBool(L"tool.replayAutoHide",true));}CefRefPtr<CefValue>root=CefValue::Create();root->SetDictionary(d);return CefWriteJSON(root,JSON_WRITER_DEFAULT).ToString();}
static void queueReplayConfiguration(){std::lock_guard<std::mutex>lock(g_hostEventMutex);g_hostEvents.push_back("HOST\tTOOL\treplayBufferConfig\t"+replayConfigJson(false)+"\n");g_hostEvents.push_back("HOST\tTOOL\treplayFrameConfig\t"+replayConfigJson(true)+"\n");g_hostEvents.push_back("HOST\tTOOL\treplayStatus\t\"\"\n");}
static void queueOverlayPlacementMode(){std::lock_guard<std::mutex>lock(g_hostEventMutex);g_hostEvents.push_back("HOST\tSETTING\toverlayPlacementMode\t"+wideToUtf8(overlayPlacementMode())+"\n");}

static std::wstring normalisedProgramPath(std::wstring path)
{
	std::replace(path.begin(), path.end(), L'/', L'\\');
	wchar_t full[MAX_PATH]{};
	if (GetFullPathNameW(path.c_str(), MAX_PATH, full, nullptr)) path = full;
	std::transform(path.begin(), path.end(), path.begin(), [](wchar_t c) { return wchar_t(towlower(c)); });
	return path;
}

static std::vector<std::wstring> managedPrograms()
{
	std::vector<std::wstring> programs;
	const std::string json = wideToUtf8(musicSetting(L"tool.programs", L"[]"));
	CefRefPtr<CefValue> value = CefParseJSON(json, JSON_PARSER_RFC);
	if (!value || value->GetType() != VTYPE_LIST) return programs;
	CefRefPtr<CefListValue> list = value->GetList();
	for (size_t i = 0; i < list->GetSize(); ++i) {
		if (list->GetType(i) != VTYPE_STRING) continue;
		const std::wstring path = utf8ToWide(list->GetString(i).ToString());
		if (!path.empty() && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) programs.push_back(path);
	}
	return programs;
}

static std::vector<DWORD> processIdsForProgram(const std::wstring &program)
{
	std::vector<DWORD> ids;
	const std::wstring wanted = normalisedProgramPath(program);
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snapshot == INVALID_HANDLE_VALUE) return ids;
	PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
	if (Process32FirstW(snapshot, &entry)) do {
		HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
		if (!process) continue;
		wchar_t path[32768]{}; DWORD length = DWORD(std::size(path));
		if (QueryFullProcessImageNameW(process, 0, path, &length) && normalisedProgramPath(path) == wanted)
			ids.push_back(entry.th32ProcessID);
		CloseHandle(process);
	} while (Process32NextW(snapshot, &entry));
	CloseHandle(snapshot);
	return ids;
}

static void launchManagedProgram(const std::wstring &program)
{
	if (program.empty() || GetFileAttributesW(program.c_str()) == INVALID_FILE_ATTRIBUTES || !processIdsForProgram(program).empty()) return;
	const size_t slash = program.find_last_of(L"\\/");
	const std::wstring folder = slash == std::wstring::npos ? L"" : program.substr(0, slash);
	ShellExecuteW(nullptr, L"open", program.c_str(), nullptr, folder.empty() ? nullptr : folder.c_str(), SW_SHOWNORMAL);
}

struct CloseProgramWindows { DWORD processId; bool foundWindow = false; };
static BOOL CALLBACK closeProgramWindow(HWND window, LPARAM parameter)
{
	auto *state = reinterpret_cast<CloseProgramWindows *>(parameter);
	DWORD processId = 0; GetWindowThreadProcessId(window, &processId);
	// Match the proven OBS Auto-Start Manager: request closure from every
	// visible top-level window owned by this process. Filtering on GW_OWNER can
	// miss applications whose real main window is technically owned.
	if (processId == state->processId && IsWindowVisible(window)) {
		state->foundWindow = true; PostMessageW(window, WM_CLOSE, 0, 0);
	}
	return TRUE;
}

static void closeManagedProgram(const std::wstring &program)
{
	for (DWORD processId : processIdsForProgram(program)) {
		CloseProgramWindows state{processId}; EnumWindows(closeProgramWindow, reinterpret_cast<LPARAM>(&state));
		// Never terminate another application as part of Hub shutdown. Programs
		// without a normal top-level window remain running and can be closed by
		// their owner. A forced GPU-process teardown can blank another display.
	}
}

static void launchManagedPrograms(){for(const auto &program:managedPrograms())launchManagedProgram(program);}
static void closeManagedPrograms(){for(const auto &program:managedPrograms())closeManagedProgram(program);}

static void closeManagedProgramsAndWait()
{
	const auto programs = managedPrograms();
	if (programs.empty()) return;

	// Capture only processes present when shutdown begins. A new instance that
	// the user starts during this grace period must not keep the Hub open.
	std::vector<DWORD> closingIds;
	for (const auto &program : programs) {
		const auto ids = processIdsForProgram(program);
		closingIds.insert(closingIds.end(), ids.begin(), ids.end());
		closeManagedProgram(program);
	}
	if (closingIds.empty()) return;

	// The old dock returned to OBS, which naturally remained alive while apps
	// handled WM_CLOSE. The Hub must explicitly provide that same lifetime.
	// Keep CEF and the Hub window alive for a bounded graceful-close period;
	// never force-kill an application that chooses not to exit.
	const ULONGLONG deadline = GetTickCount64() + 5000;
	while (GetTickCount64() < deadline) {
		bool anyRunning = false;
		for (DWORD processId : closingIds) {
			HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, processId);
			if (!process) continue;
			if (WaitForSingleObject(process, 0) == WAIT_TIMEOUT) anyRunning = true;
			CloseHandle(process);
		}
		if (!anyRunning) break;
		Sleep(20);
	}
}
static std::wstring textOutputFolder(){wchar_t appData[MAX_PATH]{};GetEnvironmentVariableW(L"APPDATA",appData,MAX_PATH);std::wstring folder=std::wstring(appData)+L"\\RearSilver Stream Suite";CreateDirectoryW(folder.c_str(),nullptr);return folder;}
static std::wstring textOutputPath(){return textOutputFolder()+L"\\now-playing.txt";}
static void ensureTextOutputFile(){const std::wstring path=textOutputPath();if(GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES){std::ofstream output(path,std::ios::binary);}}
static void writeTextOutput(const HubTrack &track){if(!musicBool(L"textOutputEnabled",false))return;std::wstring value=musicSetting(L"textOutputFormat",L"{{title}} — {{artist}} — Requested by {{user}}");auto replace=[&](const std::wstring&token,const std::string&text){size_t p=0;const std::wstring w=utf8ToWide(text);while((p=value.find(token,p))!=std::wstring::npos){value.replace(p,token.size(),w);p+=w.size();}};replace(L"{{title}}",track.title);replace(L"{{artist}}",track.artist);replace(L"{{album}}",track.album);replace(L"{{user}}",track.requestedBy);if(value.empty()||value.back()!=L' ')value.push_back(L' ');std::ofstream out(textOutputPath(),std::ios::binary|std::ios::trunc);if(out)out<<wideToUtf8(value);}

static std::wstring overlaySetting(const wchar_t *name, const wchar_t *fallback)
{
	HKEY key{}; if (RegOpenKeyExW(HKEY_CURRENT_USER, kOverlayRegistry, 0, KEY_READ, &key) != ERROR_SUCCESS) return fallback;
	wchar_t value[512]{}; DWORD type = 0, bytes = sizeof(value);
	const LSTATUS result = RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE *>(value), &bytes);
	RegCloseKey(key); return result == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ) ? value : fallback;
}

static bool overlayBool(const wchar_t *name, bool fallback)
{
	const std::wstring value = overlaySetting(name, fallback ? L"true" : L"false");
	return value == L"true" || value == L"1";
}

static void setOverlaySetting(const wchar_t *name, const std::wstring &value)
{
	HKEY key{}; DWORD disposition = 0;
	if (RegCreateKeyExW(HKEY_CURRENT_USER, kOverlayRegistry, 0, nullptr, 0, KEY_WRITE, nullptr, &key, &disposition) != ERROR_SUCCESS) return;
	RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE *>(value.c_str()), DWORD((value.size() + 1) * sizeof(wchar_t)));
	RegCloseKey(key);
}

static void toggleOverlaySetting(const wchar_t *name, bool fallback)
{
	setOverlaySetting(name, overlayBool(name, fallback) ? L"false" : L"true");
}

static DWORD overlayNumber(const wchar_t *name, DWORD fallback)
{
	HKEY key{}; if (RegOpenKeyExW(HKEY_CURRENT_USER, kOverlayRegistry, 0, KEY_READ, &key) != ERROR_SUCCESS) return fallback;
	DWORD value = fallback, type = 0, bytes = sizeof(value);
	if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE *>(&value), &bytes) != ERROR_SUCCESS || type != REG_DWORD) value = fallback;
	RegCloseKey(key); return value;
}

static void setOverlayNumber(const wchar_t *name, DWORD value)
{
	HKEY key{}; DWORD disposition = 0;
	if (RegCreateKeyExW(HKEY_CURRENT_USER, kOverlayRegistry, 0, nullptr, 0, KEY_WRITE, nullptr, &key, &disposition) != ERROR_SUCCESS) return;
	RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE *>(&value), sizeof(value)); RegCloseKey(key);
}

class OverlayDesignerSurface {
public:
	~OverlayDesignerSurface() { shutdown(); }
	void shutdown()
	{
		traceLog("webview-overlay-shutdown-begin", m_controller ? "controller=present" : "controller=absent");
		m_ready = false; m_page = -1; m_parent = nullptr;
		if (m_controller) {
			const HRESULT visibleHr = m_controller->put_IsVisible(FALSE);
			{ std::ostringstream detail; detail << "visible=0 hr=0x" << std::hex << static_cast<unsigned long>(visibleHr); traceLog("webview-overlay-visibility", detail.str()); }
			traceLog("webview-overlay-controller-close-begin");
			const HRESULT closeHr = m_controller->Close();
			{ std::ostringstream detail; detail << "hr=0x" << std::hex << static_cast<unsigned long>(closeHr); traceLog("webview-overlay-controller-close-complete", detail.str()); }
		}
		m_webView.Reset(); m_controller.Reset();
		m_visibilityKnown = false; m_visible = false;
		m_boundsKnown = false; SetRectEmpty(&m_bounds);
		traceLog("webview-overlay-shutdown-complete");
	}
	void initialise(HWND parent)
	{
		traceLog("webview-overlay-environment-create-begin");
		m_parent = parent;
		wchar_t localAppData[MAX_PATH]{}; GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
		const std::wstring suiteData = std::wstring(localAppData) + L"\\RearSilver Stream Suite"; CreateDirectoryW(suiteData.c_str(), nullptr);
		const std::wstring webData = suiteData + L"\\OverlayDesignerWebView2"; CreateDirectoryW(webData.c_str(), nullptr);
		std::wstring folder = executableAssetPath(L""); if (!folder.empty() && (folder.back() == L'\\' || folder.back() == L'/')) folder.pop_back();
		auto environmentOptions = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
		// The Hub now owns one persistent WebView2 presentation surface.  Keep
		// normal GPU rendering enabled; software rendering did not prevent the
		// display blackout and needlessly penalised the commercial UI.
		traceLog("webview-overlay-single-surface-gpu-rendering");
		CreateCoreWebView2EnvironmentWithOptions(nullptr, webData.c_str(), environmentOptions.Get(),
			Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>([this, folder](HRESULT result, ICoreWebView2Environment *environment) -> HRESULT {
				{ std::ostringstream detail; detail << "hr=0x" << std::hex << static_cast<unsigned long>(result) << " environment=" << (environment ? 1 : 0); traceLog("webview-overlay-environment-created", detail.str()); }
				if (FAILED(result) || !environment) return result;
				return environment->CreateCoreWebView2Controller(m_parent,
					Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>([this, folder](HRESULT controllerResult, ICoreWebView2Controller *controller) -> HRESULT {
						{ std::ostringstream detail; detail << "hr=0x" << std::hex << static_cast<unsigned long>(controllerResult) << " controller=" << (controller ? 1 : 0); traceLog("webview-overlay-controller-created", detail.str()); }
						if (FAILED(controllerResult) || !controller) return controllerResult;
						m_controller = controller; m_controller->get_CoreWebView2(&m_webView);
						m_visibilityKnown = false; m_visible = false;
						m_boundsKnown = false; SetRectEmpty(&m_bounds);
						EventRegistrationToken processFailedToken{};
						m_webView->add_ProcessFailed(
							Callback<ICoreWebView2ProcessFailedEventHandler>([](ICoreWebView2 *, ICoreWebView2ProcessFailedEventArgs *args) -> HRESULT {
								COREWEBVIEW2_PROCESS_FAILED_KIND kind{};
								if (args) args->get_ProcessFailedKind(&kind);
								traceLog("webview-overlay-process-failed", "kind=" + std::to_string(static_cast<int>(kind)));
								return S_OK;
							}).Get(), &processFailedToken);
						ComPtr<ICoreWebView2_3> webView3; if (SUCCEEDED(m_webView.As(&webView3))) webView3->SetVirtualHostNameToFolderMapping(L"rearsilver.local", folder.c_str(), COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
						ComPtr<ICoreWebView2Settings> settings; if (SUCCEEDED(m_webView->get_Settings(&settings))) { settings->put_AreDefaultContextMenusEnabled(FALSE); settings->put_AreDevToolsEnabled(FALSE); settings->put_IsZoomControlEnabled(FALSE); }
						EventRegistrationToken token{}; m_webView->add_WebMessageReceived(
							Callback<ICoreWebView2WebMessageReceivedEventHandler>([this](ICoreWebView2 *, ICoreWebView2WebMessageReceivedEventArgs *args) -> HRESULT {
								wchar_t *raw = nullptr; if (FAILED(args->TryGetWebMessageAsString(&raw)) || !raw) return S_OK;
								const std::string message = wideToUtf8(raw); CoTaskMemFree(raw);
								if (message == "hub-ready") { showCurrentPage(); return S_OK; }
								// A message from the document being replaced may arrive after the next
								// page has been selected. Never let that stale message mark the new page
								// ready or it can remain stuck showing its default/disconnected state.
								if (message == "ready") { if (m_page == 3) { m_ready = true; sendConfig(); } return S_OK; }
								if (message == "library-ready") { if (m_page == 2) { m_ready = true; sendLibraryConfig(); } return S_OK; }
								if (message == "commands-ready") { if (m_page == 6) { m_ready = true; sendCommandsConfig(); } return S_OK; }
								if (message == "tools-ready") { if (m_page == 7) { m_ready = true; sendToolsConfig(); } return S_OK; }
								if (message == "suite-settings-ready") { if (m_page == 8) { m_ready = true; sendSuiteSettingsConfig(); } return S_OK; }
								if (message == "feedback-diagnostics-ready") { if (m_page == 9) { m_ready = true; sendFeedbackDiagnosticsConfig(); } return S_OK; }
								CefRefPtr<CefValue> parsed = CefParseJSON(message, JSON_PARSER_RFC); if (!parsed || parsed->GetType() != VTYPE_DICTIONARY) return S_OK;
								CefRefPtr<CefDictionaryValue> object = parsed->GetDictionary();
								if (RsBeta::currentState().expired && object->GetString("page").ToString() != "feedback") return S_OK;
								if (object->GetString("page").ToString() == "library") {
									const std::string action = object->GetString("action").ToString();
									if (action == "import") { const std::wstring value=utf8ToWide(object->GetString("value").ToString()); SetWindowTextW(g_playlistEdit,value.c_str()); SendMessageW(m_parent,WM_COMMAND,MAKEWPARAM(ID_IMPORT_PLAYLIST,BN_CLICKED),reinterpret_cast<LPARAM>(g_importPlaylistButton)); }
									else { int id=action=="addFiles"?ID_ADD_LOCAL_FILES:action=="addFolder"?ID_ADD_LOCAL_FOLDER:action=="useLocal"?ID_USE_LOCAL:action=="useYouTube"?ID_USE_YOUTUBE:action=="useExternal"?ID_USE_EXTERNAL:action=="clearLocal"?ID_CLEAR_LOCAL:0; if(id) SendMessageW(m_parent,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),0); }
									sendLibraryConfig(); return S_OK;
								}
								if(object->GetString("page").ToString()=="settings"){
									const std::string action=object->GetString("action").ToString();
									if(action=="set"){const std::string rawKey=object->GetString("key").ToString();const std::wstring key=utf8ToWide(rawKey);std::wstring value;if(object->GetType("value")==VTYPE_BOOL)value=object->GetBool("value")?L"true":L"false";else if(object->GetType("value")==VTYPE_INT)value=std::to_wstring(object->GetInt("value"));else value=utf8ToWide(object->GetString("value").ToString());const wchar_t *storedKey=key.c_str();if(rawKey=="queueLimit")storedKey=L"maxQueueTotal";else if(rawKey=="userLimit")storedKey=L"maxPerUser";else if(rawKey=="maxTrackMinutes")storedKey=L"maxTrackLengthMinutes";setMusicSetting(storedKey,value);if(rawKey=="textOutputEnabled"&&(value==L"true"||value==L"1"))ensureTextOutputFile();if(rawKey=="nonRequestLabel"){g_hub.setNonRequestLabel(wideToUtf8(value));writeTextOutput(g_hub.current());saveHubState();}if(rawKey=="requestsEnabled"||rawKey=="queueLimit"||rawKey=="userLimit"||rawKey=="maxTrackMinutes"||rawKey=="overlayPlacementMode"||rawKey.rfind("command.",0)==0){std::lock_guard<std::mutex>lock(g_hostEventMutex);g_hostEvents.push_back("HOST\tSETTING\t"+rawKey+"\t"+wideToUtf8(value)+"\n");}}
									else if(action=="createCapture"){std::lock_guard<std::mutex>lock(g_hostEventMutex);g_hostEvents.push_back("HOST\tCREATE_CAPTURE\n");}
									else if(action=="autostart"){std::lock_guard<std::mutex>lock(g_hostEventMutex);g_hostEvents.push_back(std::string("HOST\tAUTOSTART\t")+(object->GetBool("value")?"true":"false")+"\n");}
									else if(action=="setupStatus"){std::lock_guard<std::mutex>lock(g_hostEventMutex);g_hostEvents.push_back("HOST\tSETUP_STATUS\n");}
									else if(action=="openOutputFolder")ShellExecuteW(m_parent,L"open",textOutputFolder().c_str(),nullptr,nullptr,SW_SHOWNORMAL);
									sendSuiteSettingsConfig();return S_OK;
								}
								if(object->GetString("page").ToString()=="suiteSettings"){
									if(object->GetString("action").ToString()=="saveState"){
										CefRefPtr<CefDictionaryValue> value=object->GetDictionary("value");
										if(value){setMusicSetting(L"suiteSettings.setupCompleted",value->GetBool("completed")?L"true":L"false");setMusicSetting(L"suiteSettings.setupStep",std::to_wstring(std::clamp(value->GetInt("step"),0,4)));setMusicSetting(L"suiteSettings.setupSchemaVersion",std::to_wstring(std::max(1,value->GetInt("schemaVersion"))));}
									}
									else if(object->GetString("action").ToString()=="setOpenHubWithObs")setMusicSetting(L"openHubWithObs",object->GetBool("value")?L"true":L"false");
									else if(object->GetString("action").ToString()=="openCommands"){
										g_page=6;showPage(g_page);InvalidateRect(m_parent,nullptr,FALSE);return S_OK;
									}
									sendSuiteSettingsConfig();return S_OK;
								}
								if(object->GetString("page").ToString()=="feedback"){
									const std::string action=object->GetString("action").ToString();
									CefRefPtr<CefDictionaryValue> feedback=object->GetDictionary("value");
									if(action=="refresh"||action=="copy"||action=="export")m_feedbackReport=buildFeedbackReport(feedback);
									if(action=="refresh")m_feedbackStatus="Diagnostics refreshed. Preview the report before sharing it.";
									else if(action=="copy")m_feedbackStatus=copyTextToClipboard(m_parent,utf8ToWide(m_feedbackReport))?"Report copied to the clipboard.":"The report could not be copied to the clipboard.";
									else if(action=="export"){
										SYSTEMTIME time{};GetLocalTime(&time);wchar_t name[192]{};
										swprintf_s(name,L"RearSilver-Stream-Suite-Beta-Feedback-%hs-%04u%02u%02u-%02u%02u%02u.txt",RsBeta::kVersion,time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond);
										m_feedbackStatus=exportTextReport(m_parent,name,m_feedbackReport)?"Report exported successfully.":"Export was cancelled or the report could not be written.";
									}else if(action=="openLogs"){
										const std::wstring folder=suiteDataFolder(false);HINSTANCE result=nullptr;if(!folder.empty())result=ShellExecuteW(m_parent,L"open",folder.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
										m_feedbackStatus=reinterpret_cast<INT_PTR>(result)>32?"Logs folder opened.":"The logs folder could not be opened.";
									}
									sendFeedbackDiagnosticsConfig();
									return S_OK;
								}
								if(object->GetString("page").ToString()=="accounts"){
									const std::string action=object->GetString("action").ToString(),account=object->GetString("account").ToString();
									if(account=="spotify"){
										if(action=="saveClientId")g_spotify.setClientIdAsync(object->GetString("value").ToString());
										else if(action=="login")g_spotify.beginLogin();
										else if(action=="refresh")g_spotify.refreshQueueAsync();
										else if(action=="logout")g_spotify.logoutAsync();
										else if(action=="openSetup")ShellExecuteW(m_parent,L"open",L"https://developer.spotify.com/dashboard",nullptr,nullptr,SW_SHOWNORMAL);
										return S_OK;
									}
									if(action=="sender"){g_authSender=object->GetString("value").ToString();setMusicSetting(L"authSender",utf8ToWide(g_authSender));}
									else {TwitchAccount &auth=account=="bot"?g_botTwitch:g_streamerTwitch;if(action=="login")auth.beginLogin();else if(action=="reconnect")auth.reconnect();else if(action=="logout")auth.logout();} sendSuiteSettingsConfig();return S_OK;
								}
								if(object->GetString("page").ToString()=="overlay"){
									const std::string action=object->GetString("action").ToString();
									if((action=="musicOverlayStatus"||action=="musicOverlayRefresh"||action=="musicOverlayShow"||action=="musicOverlayHide")&&!g_hostPipeConnected)return S_OK;
									std::lock_guard<std::mutex>lock(g_hostEventMutex);
									g_hostEvents.push_back("HOST\tTOOL\t"+action+"\t\"\"\n");
									return S_OK;
								}
								if(object->GetString("page").ToString()=="tools"){
									const std::string action=object->GetString("action").ToString();
									if(action=="pickProgram"||action=="pickTimerSound"){
										ComPtr<IFileOpenDialog> dialog;
										if(SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)))){
											DWORD options=0; dialog->GetOptions(&options);
											const bool sound=action=="pickTimerSound";
											dialog->SetOptions(options|FOS_FORCEFILESYSTEM|FOS_FILEMUSTEXIST);
											dialog->SetTitle(sound?L"Choose Timer completion sound":L"Choose an application");
											if(sound){const COMDLG_FILTERSPEC filters[]={{L"Audio files",L"*.wav;*.mp3;*.flac;*.ogg"},{L"All files",L"*.*"}};dialog->SetFileTypes(2,filters);}
											else {const COMDLG_FILTERSPEC filters[]={{L"Applications",L"*.exe"},{L"All files",L"*.*"}};dialog->SetFileTypes(2,filters);dialog->SetDefaultExtension(L"exe");}
											if(SUCCEEDED(dialog->Show(m_parent))){
												ComPtr<IShellItem> item; PWSTR raw=nullptr;
												if(SUCCEEDED(dialog->GetResult(&item))&&SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&raw))){
													CefRefPtr<CefValue> selected=CefValue::Create(); selected->SetString(wideToUtf8(raw));
													const std::wstring json=utf8ToWide(CefWriteJSON(selected,JSON_WRITER_DEFAULT).ToString());
													if(sound)m_webView->ExecuteScript((L"window.rsInvokeAll&&window.rsInvokeAll('rsSelectedTimerSound',"+json+L")").c_str(),nullptr);
												else m_webView->ExecuteScript((L"window.rsInvokeAll&&window.rsInvokeAll('rsSelectedProgram',"+json+L")").c_str(),nullptr);
													CoTaskMemFree(raw);
												}
											}
										}
										return S_OK;
									}
									if((action=="triggerReplay"||action=="replayShow"||action=="hideReplay"||action=="replayRepair"||action=="replayStart"||action=="replayStop"||action=="openReplayFolder")&&!g_hostPipeConnected)return S_OK;
									std::string value;
									if(object->HasKey("value")){CefRefPtr<CefValue>v=object->GetValue("value");value=CefWriteJSON(v,JSON_WRITER_DEFAULT).ToString();}
									// Optional applications belong to the Control Hub. OBS starts/stops
									// only the Hub and merely mirrors the other Stream Tool actions.
									if(action=="savePrograms")setMusicSetting(L"tool.programs",utf8ToWide(value));
									else if(action=="launchProgram")launchManagedProgram(utf8ToWide(object->GetString("value").ToString()));
									else if(action=="closeProgram")closeManagedProgram(utf8ToWide(object->GetString("value").ToString()));
									else if(action=="launchPrograms")launchManagedPrograms();
									else if(action=="closePrograms")closeManagedPrograms();
									else if(action=="addProgram"||action=="removeProgram"){}
									else {std::lock_guard<std::mutex>lock(g_hostEventMutex);g_hostEvents.push_back("HOST\tTOOL\t"+action+"\t"+value+"\n");}
									if(action=="setAutoLaunch")setMusicSetting(L"tool.autoLaunch",object->GetBool("value")?L"true":L"false");
									else if(action=="setAutoClose")setMusicSetting(L"tool.autoClose",object->GetBool("value")?L"true":L"false");
									else if(action=="dropText"||action=="quickTextConfig"||action=="quickTextShow"){
										CefRefPtr<CefDictionaryValue>v=object->GetDictionary("value");
										if(v){setMusicSetting(L"tool.quickText",utf8ToWide(v->GetString("text").ToString()));setMusicSetting(L"tool.quickSize",std::to_wstring(v->GetInt("size")));setMusicSetting(L"tool.quickColour",utf8ToWide(v->GetString("colour").ToString()));setMusicSetting(L"tool.quickFont",utf8ToWide(v->GetString("font").ToString()));if(v->HasKey("fontWeight"))setMusicSetting(L"tool.quickFontWeight",std::to_wstring(v->GetInt("fontWeight")));}
									}else if(action=="timerConfig"){
										CefRefPtr<CefDictionaryValue>v=object->GetDictionary("value");
										if(v){setMusicSetting(L"tool.timerLingerSeconds",std::to_wstring(v->GetInt("lingerSeconds")));setMusicSetting(L"tool.timerSoundPath",utf8ToWide(v->GetString("soundPath").ToString()));}
										if(v){setMusicSetting(L"tool.timerLabel",utf8ToWide(v->GetString("label").ToString()));setMusicSetting(L"tool.timerMode",utf8ToWide(v->GetString("mode").ToString()));setMusicSetting(L"tool.timerSeconds",std::to_wstring(v->GetInt("seconds")));setMusicSetting(L"tool.timerFont",utf8ToWide(v->GetString("font").ToString()));setMusicSetting(L"tool.timerLabelWeight",std::to_wstring(v->GetInt("labelWeight")));setMusicSetting(L"tool.timerTimeWeight",std::to_wstring(v->GetInt("timeWeight")));setMusicSetting(L"tool.timerTextColour",utf8ToWide(v->GetString("textColour").ToString()));setMusicSetting(L"tool.timerLabelSize",std::to_wstring(v->GetInt("labelSize")));setMusicSetting(L"tool.timerTimeSize",std::to_wstring(v->GetInt("timeSize")));setMusicSetting(L"tool.timerShadow",v->GetBool("shadow")?L"true":L"false");setMusicSetting(L"tool.timerBackground",v->GetBool("background")?L"true":L"false");setMusicSetting(L"tool.timerBgColour",utf8ToWide(v->GetString("backgroundColour").ToString()));setMusicSetting(L"tool.timerBgOpacity",std::to_wstring(v->GetInt("backgroundOpacity")));setMusicSetting(L"tool.timerBgRadius",std::to_wstring(v->GetInt("backgroundRadius")));setMusicSetting(L"tool.timerHideFinished",v->GetBool("hideWhenFinished")?L"true":L"false");}
									}else if(action=="replayBufferConfig"){
										CefRefPtr<CefDictionaryValue>v=object->GetDictionary("value");
										if(v){setMusicSetting(L"tool.replaySeconds",std::to_wstring(v->GetInt("seconds")));setMusicSetting(L"tool.replayAutoStart",v->GetBool("autoStart")?L"true":L"false");setMusicSetting(L"tool.replayAutoHide",v->GetBool("autoHide")?L"true":L"false");}
									}else if(action=="replayFrameConfig"){
										CefRefPtr<CefDictionaryValue>v=object->GetDictionary("value");
							if(v){setMusicSetting(L"tool.replayTitle",utf8ToWide(v->GetString("title").ToString()));setMusicSetting(L"tool.replayFont",utf8ToWide(v->GetString("font").ToString()));setMusicSetting(L"tool.replayFontWeight",utf8ToWide(v->GetString("fontWeight").ToString()));setMusicSetting(L"tool.replayAlignment",utf8ToWide(v->GetString("alignment").ToString()));setMusicSetting(L"tool.replayBackground",utf8ToWide(v->GetString("background").ToString()));setMusicSetting(L"tool.replayAccent",utf8ToWide(v->GetString("accent").ToString()));setMusicSetting(L"tool.replayTextColour",utf8ToWide(v->GetString("textColour").ToString()));setMusicSetting(L"tool.replayOpacity",std::to_wstring(v->GetInt("opacity")));setMusicSetting(L"tool.replaySizeStep",utf8ToWide(v->GetString("sizeStep").ToString()));setMusicSetting(L"tool.replayBorderStep",utf8ToWide(v->GetString("borderStep").ToString()));setMusicSetting(L"tool.replayRadiusStep",utf8ToWide(v->GetString("radiusStep").ToString()));}
									}
									else if(action=="saveQuickPresets")setMusicSetting(L"tool.quickPresets",utf8ToWide(value));
									return S_OK;
								}
								if (object->GetString("action").ToString() == "reset") { RegDeleteTreeW(HKEY_CURRENT_USER, kOverlayRegistry); if(g_hostPipeConnected){std::lock_guard<std::mutex>lock(g_hostEventMutex);g_hostEvents.push_back("HOST\tTOOL\tmusicOverlayRefresh\t\"\"\n");} sendConfig(); return S_OK; }
								const std::string key = object->GetString("key").ToString(), type = object->GetString("type").ToString();
								static const std::vector<std::string> allowed = {"showArtwork","showTitle","showArtist","showAlbum","showRequester","showProgress","artworkBackground","backgroundTransparent","showCustomText","artworkPosition","timingMode","fontFamily","titleOverflow","scrollDirection","customText","backgroundColour","textColour","accentColour","titleSize","bodySize","scrollSpeed","backgroundOpacity","width","height"};
								if (std::find(allowed.begin(), allowed.end(), key) == allowed.end()) return S_OK;
								const std::wstring wideKey = utf8ToWide(key);
								if (type == "number") { int value = object->GetInt("value"), minimum = 0, maximum = 100; if (key == "titleSize") { minimum = 8; maximum = 240; } else if (key == "bodySize") { minimum = 6; maximum = 160; } else if (key == "scrollSpeed") { minimum = 10; maximum = 200; } else if (key == "width") { minimum = 240; maximum = 3840; } else if (key == "height") { minimum = 100; maximum = 2160; } setOverlayNumber(wideKey.c_str(), DWORD(std::clamp(value, minimum, maximum))); }
								else if (type == "bool") setOverlaySetting(wideKey.c_str(), object->GetBool("value") ? L"true" : L"false");
								else { const std::wstring value = utf8ToWide(object->GetString("value").ToString()); if ((key == "backgroundColour" || key == "textColour" || key == "accentColour") && !validColour(value)) return S_OK; setOverlaySetting(wideKey.c_str(), value); }
								if(g_hostPipeConnected){std::lock_guard<std::mutex>lock(g_hostEventMutex);g_hostEvents.push_back("HOST\tTOOL\tmusicOverlayRefresh\t\"\"\n");}
								return S_OK;
							}).Get(), &token);
						m_webView->Navigate(L"https://rearsilver.local/hub-surface.html");
						resize(); m_page=-1; showPage(g_page); return S_OK;
					}).Get());
			}).Get());
	}
	void showPage(int page) {
		if (RsBeta::currentState().expired) page = 9;
		const bool visible = page >= 2 && page <= 9 && page != 4 && page != 5;
		if (visible && page != m_page && m_webView) {
			m_page = page;
			m_ready = false;
			showCurrentPage();
		}
		if (m_controller) {
			resize();
			if (!m_visibilityKnown || m_visible != visible) {
				const HRESULT hr = m_controller->put_IsVisible(visible ? TRUE : FALSE);
				if (SUCCEEDED(hr)) { m_visibilityKnown = true; m_visible = visible; }
				std::ostringstream detail; detail << "page=" << page << " visible=" << (visible ? 1 : 0) << " hr=0x" << std::hex << static_cast<unsigned long>(hr);
				traceLog("webview-overlay-visibility", detail.str());
			}
		}
		if (visible) refresh();
	}
	void refresh() {
		if (!m_webView || m_page < 2 || m_page > 9 || m_page == 4 || m_page == 5) return;
		if (!m_ready) { probePageReady(); return; }
		if (m_page == 2) sendLibraryConfig();
		else if (m_page == 3) sendConfig();
		else if (m_page == 6) sendCommandsConfig();
		else if (m_page == 7) sendToolsConfig();
		else if (m_page == 8) sendSuiteSettingsConfig();
		else if (m_page == 9) sendFeedbackDiagnosticsConfig();
	}
	void resize() {
		if (!m_controller || !m_parent) return;
		RECT client{}; GetClientRect(m_parent, &client); const int sidebar = sidebarWidthFor(client.right);
		const int bottomInset = RsBeta::currentState().expired ? 16 : 112;
		RECT bounds{sidebar + 16, 100, std::max(sidebar + 17, int(client.right) - 16), std::max(101, int(client.bottom) - bottomInset)};
		if (m_boundsKnown && EqualRect(&m_bounds, &bounds)) return;
		const HRESULT hr = m_controller->put_Bounds(bounds);
		if (SUCCEEDED(hr)) { m_bounds = bounds; m_boundsKnown = true; }
		std::ostringstream detail; detail << "left=" << bounds.left << " top=" << bounds.top << " right=" << bounds.right << " bottom=" << bounds.bottom << " hr=0x" << std::hex << static_cast<unsigned long>(hr);
		traceLog("webview-overlay-bounds", detail.str());
	}
private:
	bool m_visibilityKnown = false;
	bool m_visible = false;
	bool m_boundsKnown = false;
	RECT m_bounds{};
	static bool validColour(const std::wstring &value) { return value.size() == 7 && value[0] == L'#' && std::all_of(value.begin() + 1, value.end(), [](wchar_t c) { return iswxdigit(c) != 0; }); }
	void showCurrentPage() {
		if (!m_webView || m_page < 2 || m_page > 9 || m_page == 4 || m_page == 5) return;
		const wchar_t *name = m_page == 2 ? L"library.html" : m_page == 3 ? L"overlay-designer.html" :
			m_page == 6 ? L"commands.html" :
			m_page == 8 ? L"suite-settings.html" : m_page == 9 ? L"feedback-diagnostics.html" : L"stream-tools.html";
		const wchar_t *functionName = m_page == 2 ? L"rsApplyLibrary" : m_page == 3 ? L"rsApplyConfig" :
			m_page == 6 ? L"rsApplyCommands" :
			m_page == 8 ? L"rsApplySuiteSettings" : m_page == 9 ? L"rsApplyFeedbackDiagnostics" : L"rsApplyTools";
		const wchar_t *readyMessage = m_page == 2 ? L"library-ready" : m_page == 3 ? L"ready" :
			m_page == 6 ? L"commands-ready" :
			m_page == 8 ? L"suite-settings-ready" : m_page == 9 ? L"feedback-diagnostics-ready" : L"tools-ready";
		const std::wstring script = L"window.rsShowPage&&window.rsShowPage('https://rearsilver.local/" + std::wstring(name) +
			L"','" + functionName + L"','" + readyMessage + L"')";
		m_webView->ExecuteScript(script.c_str(), nullptr);
	}
	void probePageReady() {
		if (m_page == 4 || m_page == 5) return;
		const wchar_t *functionName = m_page == 2 ? L"rsApplyLibrary" : m_page == 3 ? L"rsApplyConfig" :
			m_page == 6 ? L"rsApplyCommands" :
			m_page == 8 ? L"rsApplySuiteSettings" : m_page == 9 ? L"rsApplyFeedbackDiagnostics" : L"rsApplyTools";
		const wchar_t *readyMessage = m_page == 2 ? L"library-ready" : m_page == 3 ? L"ready" :
			m_page == 6 ? L"commands-ready" :
			m_page == 8 ? L"suite-settings-ready" : m_page == 9 ? L"feedback-diagnostics-ready" : L"tools-ready";
		const std::wstring script = L"if(window.rsActiveHas&&window.rsActiveHas('" + std::wstring(functionName) +
			L"')) window.chrome.webview.postMessage('" + readyMessage + L"');";
		m_webView->ExecuteScript(script.c_str(), nullptr);
	}
	void sendConfig() {
		if (!m_ready || !m_webView) return; CefRefPtr<CefDictionaryValue> d = CefDictionaryValue::Create();
		auto boolean=[&](const char *key,bool fallback){const std::wstring k=utf8ToWide(key);d->SetBool(key,overlayBool(k.c_str(),fallback));}; auto string=[&](const char *key,const wchar_t *fallback){const std::wstring k=utf8ToWide(key);d->SetString(key,wideToUtf8(overlaySetting(k.c_str(),fallback)));}; auto number=[&](const char *key,int fallback){const std::wstring k=utf8ToWide(key);d->SetInt(key,int(overlayNumber(k.c_str(),DWORD(fallback))));};
		boolean("showArtwork",true); boolean("showTitle",true); boolean("showArtist",true); boolean("showAlbum",true); boolean("showRequester",true); boolean("showProgress",true); boolean("artworkBackground",false); boolean("backgroundTransparent",false);
		string("artworkPosition",L"left"); string("timingMode",L"elapsedTotal"); d->SetString("fontFamily",wideToUtf8(normalisedOverlayFontSetting(L"fontFamily",L"Sora"))); string("titleOverflow",L"ellipsis"); string("scrollDirection",L"left"); string("customText",L""); string("backgroundColour",L"#0b0f14"); string("textColour",L"#e6e8eb"); string("accentColour",L"#00d4ff"); number("titleSize",34); number("bodySize",20); number("scrollSpeed",45); number("backgroundOpacity",82); number("width",800); number("height",240);
		d->SetBool("ipcConnected",g_hostPipeConnected); d->SetBool("sourceExists",g_musicOverlaySourceExists);
		d->SetBool("placedInCurrentScene",g_musicOverlayPlaced); d->SetBool("visibleInCurrentScene",g_musicOverlayVisible);
		d->SetBool("conflict",g_musicOverlayConflict); d->SetBool("setupComplete",g_musicOverlaySetupComplete);
		d->SetString("message",g_musicOverlayMessage); d->SetString("placementMode",g_overlayPlacementMode);
		setFontFamilies(d, "fontFamilies");
		CefRefPtr<CefValue> root=CefValue::Create(); root->SetDictionary(d); const std::wstring script=L"window.rsApplyConfig("+utf8ToWide(CefWriteJSON(root,JSON_WRITER_DEFAULT).ToString())+L")"; m_webView->ExecuteScript(script.c_str(),nullptr);
	}
	void sendCommandsConfig(){if(!m_ready||!m_webView||m_page!=6)return;CefRefPtr<CefDictionaryValue>d=CefDictionaryValue::Create();appendChatCommandConfig(d);d->SetBool("spotifyAuthorized",g_spotify.state().authorized);CefRefPtr<CefValue>root=CefValue::Create();root->SetDictionary(d);m_webView->ExecuteScript((L"window.rsApplyCommands("+utf8ToWide(CefWriteJSON(root,JSON_WRITER_DEFAULT).ToString())+L")").c_str(),nullptr);}
	void sendToolsConfig(){
		if(!m_ready||!m_webView||m_page!=7)return;
		CefRefPtr<CefDictionaryValue>d=CefDictionaryValue::Create();
		d->SetString("quickText",wideToUtf8(musicSetting(L"tool.quickText",L"")));
		d->SetInt("quickSize",_wtoi(musicSetting(L"tool.quickSize",L"120").c_str()));
		d->SetString("quickColour",wideToUtf8(musicSetting(L"tool.quickColour",L"#ffffff")));
		d->SetString("quickFont",wideToUtf8(normalisedMusicFontSetting(L"tool.quickFont",L"Sora")));
		d->SetInt("quickFontWeight",_wtoi(musicSetting(L"tool.quickFontWeight",L"700").c_str()));
		d->SetBool("quickTextSourceExists",g_quickTextSourceExists);
		d->SetBool("quickTextPlaced",g_quickTextPlaced);
		d->SetBool("quickTextVisible",g_quickTextVisible);
		d->SetBool("quickTextConflict",g_quickTextConflict);
		d->SetBool("quickTextSetupComplete",g_quickTextSetupComplete);
		d->SetString("quickTextMessage",g_quickTextMessage);
		d->SetString("overlayPlacementMode",g_overlayPlacementMode);
		d->SetString("timerLabel",wideToUtf8(musicSetting(L"tool.timerLabel",L"Timer")));
		d->SetString("timerMode",wideToUtf8(musicSetting(L"tool.timerMode",L"countdown")));
		d->SetInt("timerSeconds",_wtoi(musicSetting(L"tool.timerSeconds",L"300").c_str()));
		d->SetString("timerFont",wideToUtf8(normalisedMusicFontSetting(L"tool.timerFont",L"Segoe UI")));
		d->SetInt("timerLabelWeight",_wtoi(musicSetting(L"tool.timerLabelWeight",L"600").c_str()));
		d->SetInt("timerTimeWeight",_wtoi(musicSetting(L"tool.timerTimeWeight",L"800").c_str()));
		d->SetString("timerTextColour",wideToUtf8(musicSetting(L"tool.timerTextColour",L"#ffffff")));
		d->SetInt("timerLabelSize",_wtoi(musicSetting(L"tool.timerLabelSize",L"28").c_str()));
		d->SetInt("timerTimeSize",_wtoi(musicSetting(L"tool.timerTimeSize",L"84").c_str()));
		d->SetBool("timerShadow",musicBool(L"tool.timerShadow",true));
		d->SetBool("timerBackground",musicBool(L"tool.timerBackground",false));
		d->SetString("timerBgColour",wideToUtf8(musicSetting(L"tool.timerBgColour",L"#000000")));
		d->SetInt("timerBgOpacity",_wtoi(musicSetting(L"tool.timerBgOpacity",L"70").c_str()));
		d->SetInt("timerBgRadius",_wtoi(musicSetting(L"tool.timerBgRadius",L"48").c_str()));
		d->SetBool("timerHideFinished",musicBool(L"tool.timerHideFinished",false));
		d->SetInt("timerLingerSeconds",_wtoi(musicSetting(L"tool.timerLingerSeconds",L"3").c_str()));
		d->SetString("timerSoundPath",wideToUtf8(musicSetting(L"tool.timerSoundPath",L"")));
		d->SetBool("timerSourceExists",g_timerSourceExists);
		d->SetBool("timerPlaced",g_timerPlaced);
		d->SetBool("timerVisible",g_timerVisible);
		d->SetBool("timerConflict",g_timerConflict);
		d->SetBool("timerSetupComplete",g_timerSetupComplete);
		d->SetString("timerMessage",g_timerMessage);
		d->SetInt("replaySeconds",_wtoi(musicSetting(L"tool.replaySeconds",L"10").c_str()));
		d->SetBool("replayAutoStart",musicBool(L"tool.replayAutoStart",false));
		d->SetBool("replayAutoHide",musicBool(L"tool.replayAutoHide",true));
		d->SetBool("replayBufferActive",g_replayBufferActive);
		d->SetBool("replaySceneExists",g_replaySceneExists);
		d->SetBool("replayPlaced",g_replayPlaced);
		d->SetBool("replayVisible",g_replayVisible);
		d->SetBool("replayPlaying",g_replayPlaying);
		d->SetBool("replayConflict",g_replayConflict);
		d->SetBool("replaySetupComplete",g_replaySetupComplete);
		d->SetString("replayMessage",g_replayMessage);
		d->SetString("replayTitle",wideToUtf8(musicSetting(L"tool.replayTitle",L"INSTANT REPLAY")));
		d->SetString("replayFont",wideToUtf8(normalisedMusicFontSetting(L"tool.replayFont",L"Sora")));
		d->SetString("replayFontWeight",wideToUtf8(replayFontWeight()));
		d->SetString("replayAlignment",wideToUtf8(musicSetting(L"tool.replayAlignment",L"left")));
		d->SetString("replayBackground",wideToUtf8(musicSetting(L"tool.replayBackground",L"#0b0f14")));
		d->SetString("replayAccent",wideToUtf8(musicSetting(L"tool.replayAccent",L"#00d4ff")));
		d->SetString("replayTextColour",wideToUtf8(musicSetting(L"tool.replayTextColour",L"#e6e8eb")));
		d->SetInt("replayOpacity",_wtoi(musicSetting(L"tool.replayOpacity",L"92").c_str()));
		d->SetString("replaySizeStep",wideToUtf8(replaySizeStep()));
		d->SetString("replayBorderStep",wideToUtf8(replayBorderStep()));
		d->SetString("replayRadiusStep",wideToUtf8(replayRadiusStep()));
		d->SetBool("replayGeometryAvailable",g_replayPreviewGeometry.available);
		d->SetInt("replayLayoutWidth",g_replayPreviewGeometry.width);
		d->SetInt("replayLayoutHeight",g_replayPreviewGeometry.height);
		d->SetInt("replayLayoutScale",g_replayPreviewGeometry.scalePercent);
		d->SetInt("replayTitlePixelSize",g_replayPreviewGeometry.titlePixelSize);
		d->SetInt("replayLayoutBorder",g_replayPreviewGeometry.border);
		d->SetInt("replayLayoutOuterRadius",g_replayPreviewGeometry.outerRadius);
		d->SetInt("replayLayoutInnerRadius",g_replayPreviewGeometry.innerRadius);
		d->SetInt("replayApertureX",g_replayPreviewGeometry.apertureX);
		d->SetInt("replayApertureY",g_replayPreviewGeometry.apertureY);
		d->SetInt("replayApertureWidth",g_replayPreviewGeometry.apertureWidth);
		d->SetInt("replayApertureHeight",g_replayPreviewGeometry.apertureHeight);
		d->SetInt("replayTitleX",g_replayPreviewGeometry.titleX);
		d->SetInt("replayTitleY",g_replayPreviewGeometry.titleY);
		d->SetInt("replayTitleWidth",g_replayPreviewGeometry.titleWidth);
		d->SetInt("replayTitleHeight",g_replayPreviewGeometry.titleHeight);
		setFontFamilies(d, "fontFamilies");
		auto applyArray=[&](const char*key,const wchar_t*setting,const wchar_t*fallback){
			CefRefPtr<CefValue>parsed=CefParseJSON(wideToUtf8(musicSetting(setting,fallback)),JSON_PARSER_RFC);
			if(parsed&&parsed->GetType()==VTYPE_LIST)d->SetList(key,parsed->GetList());
		};
		applyArray("quickPresets",L"tool.quickPresets",L"[\"BRB\",\"Coffee Break\",\"Back Soon\"]");
		applyArray("programs",L"tool.programs",L"[]");
		d->SetString("status",g_hostPipeConnected?"Connected to OBS. Stream Tool actions are ready.":"OBS is not connected. Settings are saved; live actions become available when OBS opens.");
		d->SetBool("ipcConnected",g_hostPipeConnected);
		CefRefPtr<CefValue>root=CefValue::Create();root->SetDictionary(d);m_webView->ExecuteScript((L"window.rsApplyTools("+utf8ToWide(CefWriteJSON(root,JSON_WRITER_DEFAULT).ToString())+L")").c_str(),nullptr);
	}
	void sendLibraryConfig() {
		if(!m_ready||!m_webView||m_page!=2)return; CefRefPtr<CefDictionaryValue>d=CefDictionaryValue::Create(); d->SetString("url",g_hub.fallbackUrl()); d->SetString("status",wideToUtf8(g_libraryStatus)); d->SetString("label",g_hub.fallbackLabel()); d->SetInt("youtubeCount",int(g_hub.youtubeFallback().size())); d->SetInt("requestCount",int(g_hub.requests().size())); d->SetInt("localCount",int(g_hub.localLibrary().size())); d->SetString("source",g_hub.activeSource()); CefRefPtr<CefValue>root=CefValue::Create();root->SetDictionary(d);const std::wstring script=L"window.rsApplyLibrary("+utf8ToWide(CefWriteJSON(root,JSON_WRITER_DEFAULT).ToString())+L")";m_webView->ExecuteScript(script.c_str(),nullptr);
	}
	void sendSuiteSettingsConfig()
	{
		if (!m_ready || !m_webView || m_page != 8)
			return;

		CefRefPtr<CefDictionaryValue> d = CefDictionaryValue::Create();
		d->SetBool("setupCompleted", musicBool(L"suiteSettings.setupCompleted", false));
		d->SetInt("setupStep", std::clamp(_wtoi(musicSetting(L"suiteSettings.setupStep", L"0").c_str()), 0, 4));
		d->SetInt("setupSchemaVersion", std::max(1, _wtoi(musicSetting(L"suiteSettings.setupSchemaVersion", L"1").c_str())));
		d->SetBool("openHubWithObs", musicBool(L"openHubWithObs", false));
		d->SetString("overlayPlacementMode", wideToUtf8(overlayPlacementMode()));
		d->SetBool("ipcConnected", g_hostPipeConnected);
		d->SetBool("captureExists", g_captureExists);
		auto settingBool = [&](const char *key, bool fallback) { const auto wide = utf8ToWide(key); d->SetBool(key, musicBool(wide.c_str(), fallback)); };
		auto settingString = [&](const char *key, const wchar_t *fallback) { const auto wide = utf8ToWide(key); d->SetString(key, wideToUtf8(musicSetting(wide.c_str(), fallback))); };
		auto settingInt = [&](const char *key, const wchar_t *stored, int fallback) { d->SetInt(key, _wtoi(musicSetting(stored, std::to_wstring(fallback).c_str()).c_str())); };
		settingBool("requestsEnabled", true); settingBool("playlistOnly", false); settingBool("preventDuplicates", true);
		settingBool("announceTrackChanges", false); settingBool("textOutputEnabled", false);
		settingBool("youtubeMusicOnly", false); settingBool("youtubeRejectAgeRestricted", true);
		settingString("minimumRole", L"everyone"); settingString("nonRequestLabel", L"Stream DJ");
		settingString("youtubeSafeSearch", L"strict");
		settingString("fallbackStartup", L"continue"); settingString("fallbackOrder", L"ordered");
		settingString("nowPlayingSymbol", L"▶️"); settingString("textOutputFormat", L"{{title}} - {{artist}} - Requested by {{user}}");
		settingInt("queueLimit", L"maxQueueTotal", 50); settingInt("userLimit", L"maxPerUser", 2);
		settingInt("maxTrackMinutes", L"maxTrackLengthMinutes", 10);
		appendChatCommandConfig(d);
		const std::wstring legacyExemption = musicSetting(L"exemptRole", L"moderator");
		d->SetBool("exemptSubscriber", musicBool(L"exemptSubscriber", legacyExemption == L"subscriber"));
		d->SetBool("exemptVip", musicBool(L"exemptVip", legacyExemption == L"subscriber" || legacyExemption == L"vip"));
		d->SetBool("exemptModerator", musicBool(L"exemptModerator", legacyExemption != L"none" && legacyExemption != L"broadcaster"));
		d->SetBool("exemptBroadcaster", musicBool(L"exemptBroadcaster", legacyExemption != L"none"));
		d->SetString("outputPath", wideToUtf8(textOutputPath()));
		const SpotifyClientState accountSpotify = g_spotify.state();
		const TwitchAccountState accountStreamer = g_streamerTwitch.state(), accountBot = g_botTwitch.state();
		d->SetDouble("accountsRevision", double(++g_accountsRevision));
		const std::string streamerState = accountStreamer.busy ? "connecting" : !accountStreamer.connected ? "disconnected" : g_twitchReader.connected() ? "connected" : "chat-reconnecting";
		const std::string botState = accountBot.busy ? "connecting" : !accountBot.connected ? "disconnected" : g_authSender == "bot" && !g_twitchSender.connected() ? "chat-reconnecting" : "connected";
		d->SetString("streamerState", streamerState);
		d->SetString("streamerLogin", accountStreamer.login); d->SetString("streamerDisplayName", accountStreamer.displayName); d->SetString("streamerError", accountStreamer.error);
		d->SetString("botState", botState);
		d->SetString("botLogin", accountBot.login); d->SetString("botDisplayName", accountBot.displayName); d->SetString("botError", accountBot.error);
		d->SetString("sender", g_authSender); d->SetString("spotifyClientId", accountSpotify.clientId);
		d->SetBool("spotifyAuthorized", accountSpotify.authorized); d->SetBool("spotifyConnected", accountSpotify.connected);
		d->SetBool("spotifyBusy", accountSpotify.busy); d->SetBool("spotifyQueueChecked", accountSpotify.queueChecked);
		d->SetBool("spotifyPlaybackAvailable", accountSpotify.playbackAvailable);
		d->SetString("spotifyDisplayName", accountSpotify.displayName); d->SetString("spotifyError", accountSpotify.error);
		d->SetInt("spotifyQueueCount", int(accountSpotify.queue.size()));
		CefRefPtr<CefValue> programs = CefParseJSON(wideToUtf8(musicSetting(L"tool.programs", L"[]")), JSON_PARSER_RFC);
		if (programs && programs->GetType() == VTYPE_LIST)
			d->SetList("programs", programs->GetList());
		CefRefPtr<CefValue> root = CefValue::Create();
		root->SetDictionary(d);
		m_webView->ExecuteScript((L"window.rsApplySuiteSettings(" +
			utf8ToWide(CefWriteJSON(root, JSON_WRITER_DEFAULT).ToString()) + L")").c_str(), nullptr);
	}
	std::string buildFeedbackReport(CefRefPtr<CefDictionaryValue> feedback)
	{
		auto field = [&](const char *key) -> std::string {
			return feedback && feedback->HasKey(key) ? feedback->GetString(key).ToString() : std::string{};
		};
		const RsBeta::State beta = RsBeta::currentState();
		const SpotifyClientState spotify = g_spotify.state();
		const TwitchAccountState streamer = g_streamerTwitch.state(), bot = g_botTwitch.state();
		std::ostringstream report;
		report << "RearSilver Stream Suite — " << RsBeta::kChannel << " Feedback Report\n"
			<< "======================================================\n\n"
			<< "1. Beta/build information\n"
			<< "-------------------------\n"
			<< "Product: " << RsBeta::kProductName << "\nChannel: " << RsBeta::kChannel
			<< "\nVersion: " << RsBeta::kVersion << "\nBuild ID: " << RsBeta::kBuildId
			<< "\nBuild date: " << RsBeta::kBuildDate
			<< "\nExpiry date: " << (RsBeta::kExpiryEnabled ? RsBeta::kExpiryDisplay : "Not applicable")
			<< "\nExpiry state: " << (!RsBeta::kExpiryEnabled ? "Not applicable" : beta.expired ? "Expired" : beta.warning ? "Warning period" : "Active")
			<< "\nDays remaining: " << (RsBeta::kExpiryEnabled ? std::to_string(beta.daysRemaining) : "Not applicable") << "\nReport created: " << localReportTime() << "\n\n"
			<< "2. Tester feedback\n"
			<< "------------------\n"
			<< "Category: " << field("category") << "\nSummary: " << field("summary")
			<< "\nTrying to do: " << field("trying") << "\nFirst place looked: " << field("firstLook")
			<< "\nFound without help: " << field("foundWithoutHelp") << "\nExpected: " << field("expected")
			<< "\nActually happened: " << field("actual") << "\nReproducible: " << field("reproducible")
			<< "\nReproduction steps:\n" << field("steps") << "\nExpected wording/navigation: " << field("wording")
			<< "\nSetup abandoned/help required: " << field("abandoned") << "\nAdditional feedback:\n" << field("additional") << "\n\n"
			<< "3. System and OBS information\n"
			<< "-----------------------------\n"
			<< "System: " << windowsDescription() << "\nOBS connection: " << (g_hostPipeConnected ? "Connected" : "Not connected")
			<< "\nOBS Studio version: " << (g_obsStudioVersion.empty() ? "Unavailable (OBS has not supplied it)" : g_obsStudioVersion)
			<< "\nPlugin version: " << (g_pluginVersion.empty() ? "Unavailable (plugin has not supplied it)" : g_pluginVersion)
			<< "\nControl Hub version: " << RsBeta::kVersion
			<< "\nPlugin/Hub version comparison: " << (g_pluginVersion.empty() ? "Unavailable" : g_pluginVersion == RsBeta::kVersion ? "Match" : "MISMATCH") << "\n\n"
			<< "4. Connection and feature states\n"
			<< "--------------------------------\n"
			<< "Control Hub process: Running\nControl Hub/OBS IPC: " << (g_hostPipeConnected ? "Connected" : "Disconnected")
			<< "\nFeature access: " << (beta.expired ? "Disabled (private beta expired)" : "Enabled")
			<< "\nActive music provider: " << (g_hub.activeSource().empty() ? "None" : g_hub.activeSource())
			<< "\nYouTube fallback availability: " << (!g_hub.youtubeFallback().empty() ? "Available" : "No playlist loaded")
			<< "\nLocal library availability: " << (!g_hub.localLibrary().empty() ? "Available" : "No local library loaded")
			<< "\nRuntime service evaluation: " << (beta.expired ? "Not started because the private beta expired; inactive states below are not account-status checks" : "Active")
			<< "\nSpotify configured: " << (!spotify.clientId.empty() ? "Yes" : "No")
			<< "\nSpotify authorised: " << (spotify.authorized ? "Yes" : "No")
			<< "\nSpotify connected: " << (spotify.connected ? "Yes" : "No")
			<< "\nSpotify playback available: " << (spotify.playbackAvailable ? "Yes" : "No")
			<< "\nTwitch streamer configured: " << (streamer.authorized ? "Yes" : "No")
			<< "\nTwitch streamer connected: " << (streamer.connected ? "Yes" : "No")
			<< "\nTwitch bot configured: " << (bot.authorized ? "Yes" : "No")
			<< "\nTwitch bot connected: " << (bot.connected ? "Yes" : "No")
			<< "\nSong requests: " << (beta.expired ? (musicBool(L"requestsEnabled", true) ? "Disabled by expiry (saved preference: Enabled)" : "Disabled by expiry (saved preference: Off)") : (musicBool(L"requestsEnabled", true) ? "Enabled" : "Off"))
			<< "\nWaiting requests: " << g_hub.requests().size()
			<< "\nOverlay placement mode: " << g_overlayPlacementMode
			<< "\nMusic Capture exists: " << (g_captureExists ? "Yes" : "No")
			<< "\nQuick Text source: " << (g_quickTextSourceExists ? "Exists" : "Not detected")
			<< "\nQuick Text conflict: " << (g_quickTextConflict ? "Yes" : "No")
			<< "\nTimer source: " << (g_timerSourceExists ? "Exists" : "Not detected")
			<< "\nTimer conflict: " << (g_timerConflict ? "Yes" : "No")
			<< "\nInstant Replay setup: " << (g_replaySetupComplete ? "Complete" : "Incomplete")
			<< "\nInstant Replay conflict: " << (g_replayConflict ? "Yes" : "No")
			<< "\nMusic Overlay setup: " << (g_musicOverlaySetupComplete ? "Complete" : "Incomplete")
			<< "\nMusic Overlay conflict: " << (g_musicOverlayConflict ? "Yes" : "No") << "\n\n"
			<< "5. Recent warnings/errors\n"
			<< "--------------------------\n"
			<< relevantLogExcerpt(traceLogPath(), "Control Hub trace")
			<< relevantLogExcerpt(lifecycleLogPath(), "Control Hub lifecycle")
			<< relevantLogExcerpt(suiteDataFolder(true) + L"\\spotify-diagnostics.log", "Spotify")
			<< relevantLogExcerpt(suiteDataFolder(true) + L"\\twitch-diagnostics.log", "Twitch") << "\n"
			<< "6. Included log excerpts\n"
			<< "-------------------------\n"
			<< "Only the most recent warning/error-related lines (maximum 12 per log, read from at most the last 64 KiB) are included. Routine process-start entries are omitted.\n"
			<< "Full log files remain local and are not uploaded automatically.\n"
			<< "Control Hub/CEF logs: <local-app-data>\\RearSilver Stream Suite\n"
			<< "Spotify/Twitch logs: <roaming-app-data>\\RearSilver Stream Suite\n\n"
			<< "7. Redaction notice\n"
			<< "-------------------\n"
			<< "Authentication tokens, OAuth/device codes, client secrets and IDs, stream keys, raw credentials, Windows usernames and profile paths are excluded or redacted. Chat contents and local music filenames/metadata are not collected. Review the preview before sharing.\n";
		return redactDiagnosticText(report.str());
	}

	void sendFeedbackDiagnosticsConfig()
	{
		if (!m_ready || !m_webView || m_page != 9)
			return;
		CefRefPtr<CefDictionaryValue> d = CefDictionaryValue::Create();
		d->SetString("channel", RsBeta::kChannel);
		d->SetString("version", RsBeta::kVersion);
		d->SetString("buildId", RsBeta::kBuildId);
		d->SetString("buildDate", RsBeta::kBuildDate);
		d->SetString("expiry", RsBeta::kExpiryDisplay);
		d->SetBool("expiryEnabled", RsBeta::kExpiryEnabled);
		d->SetBool("expired", RsBeta::currentState().expired);
		d->SetBool("obsConnected", g_hostPipeConnected);
		d->SetString("obsVersion", g_obsStudioVersion);
		d->SetString("pluginVersion", g_pluginVersion);
		d->SetString("versionComparison", g_pluginVersion.empty() ? "Unavailable" : g_pluginVersion == RsBeta::kVersion ? "Match" : "Mismatch");
		d->SetString("provider", g_hub.activeSource());
		d->SetBool("requestsEnabled", musicBool(L"requestsEnabled", true));
		d->SetString("status", m_feedbackStatus.empty() ? "Diagnostics ready." : m_feedbackStatus);
		d->SetString("report", m_feedbackReport);
		CefRefPtr<CefValue> root = CefValue::Create();
		root->SetDictionary(d);
		m_webView->ExecuteScript((L"window.rsApplyFeedbackDiagnostics(" +
			utf8ToWide(CefWriteJSON(root, JSON_WRITER_DEFAULT).ToString()) + L")").c_str(), nullptr);
	}

	HWND m_parent=nullptr; ComPtr<ICoreWebView2Controller> m_controller; ComPtr<ICoreWebView2> m_webView; bool m_ready=false; int m_page=-1;
	std::string m_feedbackReport;
	std::string m_feedbackStatus;
};

static std::unique_ptr<OverlayDesignerSurface> g_overlayDesigner;

static void updateOverlayDesignerSurface()
{
	if (g_overlayDesigner) { g_overlayDesigner->resize(); g_overlayDesigner->showPage(g_page); }
}

static std::wstring cycleValue(const std::wstring &current, const std::vector<std::wstring> &values)
{
	auto found = std::find(values.begin(), values.end(), current);
	return found == values.end() || ++found == values.end() ? values.front() : *found;
}

static void roundedPanel(Graphics &graphics, const RectF &rect, float radius, const Color &colour)
{
	GraphicsPath path;
	const float d = radius * 2.0f;
	path.AddArc(rect.X, rect.Y, d, d, 180, 90);
	path.AddArc(rect.GetRight() - d, rect.Y, d, d, 270, 90);
	path.AddArc(rect.GetRight() - d, rect.GetBottom() - d, d, d, 0, 90);
	path.AddArc(rect.X, rect.GetBottom() - d, d, d, 90, 90);
	path.CloseFigure();
	SolidBrush brush(colour);
	graphics.FillPath(&brush, &path);
}

static void label(Graphics &graphics, const std::wstring &text, Font &font, const RectF &rect,
	const Color &colour, StringAlignment alignment = StringAlignmentNear)
{
	SolidBrush brush(colour);
	StringFormat format;
	format.SetAlignment(alignment);
	format.SetLineAlignment(StringAlignmentCenter);
	format.SetTrimming(StringTrimmingEllipsisCharacter);
	format.SetFormatFlags(StringFormatFlagsNoWrap);
	graphics.DrawString(text.c_str(), -1, &font, rect, &format, &brush);
}

static void drawTransportIcon(Graphics &graphics, int action, const RECT &bounds, bool playing,
	const Color &colour)
{
	SolidBrush brush(colour);
	const float cx = (float(bounds.left) + float(bounds.right)) * 0.5f;
	const float cy = (float(bounds.top) + float(bounds.bottom)) * 0.5f;
	auto triangle = [&](float centreX, bool pointsRight) {
		const float direction = pointsRight ? 1.0f : -1.0f;
		PointF points[] = {
			PointF(centreX + direction * 7.0f, cy),
			PointF(centreX - direction * 5.0f, cy - 8.0f),
			PointF(centreX - direction * 5.0f, cy + 8.0f),
		};
		graphics.FillPolygon(&brush, points, 3);
	};
	if (action == 0) {
		graphics.FillRectangle(&brush, cx - 9.0f, cy - 8.0f, 3.0f, 16.0f);
		triangle(cx + 2.0f, false);
	} else if (action == 1) {
		triangle(cx - 5.5f, false);
		triangle(cx + 6.5f, false);
	} else if (action == 2 && playing) {
		graphics.FillRectangle(&brush, cx - 7.0f, cy - 8.0f, 4.0f, 16.0f);
		graphics.FillRectangle(&brush, cx + 3.0f, cy - 8.0f, 4.0f, 16.0f);
	} else if (action == 2) {
		triangle(cx, true);
	} else if (action == 3) {
		triangle(cx - 2.0f, true);
		graphics.FillRectangle(&brush, cx + 7.0f, cy - 8.0f, 3.0f, 16.0f);
	} else if (action == 4) {
		graphics.FillRectangle(&brush, cx - 7.0f, cy - 7.0f, 14.0f, 14.0f);
	}
}

static void drawBrandedButton(const DRAWITEMSTRUCT &item)
{
	Graphics graphics(item.hDC); graphics.SetSmoothingMode(SmoothingModeHighQuality);
	const bool pressed = (item.itemState & ODS_SELECTED) != 0;
	const bool disabled = (item.itemState & ODS_DISABLED) != 0;
	const bool selected = (item.CtlID == ID_USE_LOCAL && g_hub.activeSource() == "local") ||
		(item.CtlID == ID_USE_YOUTUBE && g_hub.activeSource() == "youtube");
	const Color fill = pressed ? Color(255, 0, 118, 148) : (selected ? Color(255, 0, 74, 92) : Color(255, 30, 36, 48));
	roundedPanel(graphics, RectF(float(item.rcItem.left), float(item.rcItem.top), float(item.rcItem.right-item.rcItem.left), float(item.rcItem.bottom-item.rcItem.top)), 7.0f, fill);
	Pen border(selected ? Color(255, 0, 212, 255) : Color(255, 55, 70, 91), selected ? 1.5f : 1.0f);
	graphics.DrawRectangle(&border, RectF(float(item.rcItem.left)+0.5f, float(item.rcItem.top)+0.5f, float(item.rcItem.right-item.rcItem.left)-1.0f, float(item.rcItem.bottom-item.rcItem.top)-1.0f));
	wchar_t text[256]{}; GetWindowTextW(item.hwndItem, text, 256); FontFamily family(L"Sora"); Font font(&family, 14, FontStyleBold, UnitPixel);
	label(graphics, text, font, RectF(float(item.rcItem.left)+12, float(item.rcItem.top), float(item.rcItem.right-item.rcItem.left)-24, float(item.rcItem.bottom-item.rcItem.top)), disabled ? Color(255, 93, 111, 139) : Color(255, 230, 232, 235), StringAlignmentCenter);
}

static LRESULT CALLBACK legacyWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (message == WM_CLOSE) { g_closeRequested = true; return 0; }
	if (message == WM_SIZE) {
		if (g_youtubePlayer) g_youtubePlayer->resize();
		if (g_overlayDesigner) g_overlayDesigner->resize();
		return 0;
	}
	if (message == WM_ERASEBKGND) return 1;
	if (message == WM_PAINT) {
		PAINTSTRUCT paint{}; HDC dc = BeginPaint(window, &paint); RECT client{}; GetClientRect(window, &client);
		if (g_youtubePlayer && g_youtubePlayer->active()) {
			HBRUSH background = CreateSolidBrush(RGB(12, 12, 18));
			FillRect(dc, &paint.rcPaint, background);
			DeleteObject(background);
			EndPaint(window, &paint);
			return 0;
		}
		const int clientWidth = client.right - client.left;
		const int dirtyWidth = std::max(1L, paint.rcPaint.right - paint.rcPaint.left);
		const int dirtyHeight = std::max(1L, paint.rcPaint.bottom - paint.rcPaint.top);
		HDC bufferDc = CreateCompatibleDC(dc); HBITMAP bufferBitmap = CreateCompatibleBitmap(dc, dirtyWidth, dirtyHeight);
		HGDIOBJ previousBitmap = SelectObject(bufferDc, bufferBitmap);
		SetViewportOrgEx(bufferDc, -paint.rcPaint.left, -paint.rcPaint.top, nullptr);
		Graphics graphics(bufferDc); graphics.SetSmoothingMode(SmoothingModeHighQuality); graphics.Clear(Color(255, 12, 12, 18));
		SolidBrush white(Color(255, 230, 232, 235)), muted(Color(255, 164, 175, 194)), accent(Color(255, 0, 212, 255));
		FontFamily family(L"Sora"); Font titleFont(&family, 22, FontStyleBold, UnitPixel), bodyFont(&family, 16, FontStyleRegular, UnitPixel), smallFont(&family, 13, FontStyleRegular, UnitPixel);
		const int width = client.right - client.left, artSize = std::min(width - 48, 360), artX = (width - artSize) / 2, artY = 24;
		if (g_player && g_player->artwork() && g_player->artwork()->GetLastStatus() == Ok)
			graphics.DrawImage(g_player->artwork(), artX, artY, artSize, artSize);
		else { SolidBrush panel(Color(255, 30, 30, 40)); graphics.FillRectangle(&panel, artX, artY, artSize, artSize); graphics.DrawString(L"♫", -1, &titleFont, PointF(float(width / 2 - 10), float(artY + artSize / 2 - 15)), &muted); }
		const std::wstring title = g_player ? g_player->title() : L"No track playing";
		const std::wstring artist = g_player ? g_player->artist() : L"";
		const std::wstring album = g_player ? g_player->album() : L"";
		RectF textRect(24, float(artY + artSize + 22), float(width - 48), 34);
		graphics.DrawString(title.c_str(), -1, &titleFont, textRect, nullptr, &white);
		textRect.Y += 38; textRect.Height = 24; graphics.DrawString(artist.c_str(), -1, &bodyFont, textRect, nullptr, &muted);
		if (!album.empty()) { textRect.Y += 26; graphics.DrawString(album.c_str(), -1, &smallFont, textRect, nullptr, &muted); }
		const float barY = float(client.bottom - 62), barX = 24, barWidth = float(width - 48);
		SolidBrush track(Color(255, 55, 55, 68)); graphics.FillRectangle(&track, barX, barY, barWidth, 6.0f);
		const float duration = currentDuration(), position = currentPosition();
		const float progress = duration > 0 ? std::min(1.0f, position / duration) : 0;
		graphics.FillRectangle(&accent, barX, barY, barWidth * progress, 6.0f);
		const std::wstring timing = clockText(position) + L" / " + clockText(duration);
		graphics.DrawString(timing.c_str(), -1, &smallFont, PointF(24, barY + 13), &muted);
		const std::wstring state = currentState();
		graphics.DrawString(state.c_str(), -1, &smallFont, PointF(float(width - 90), barY + 13), &muted);
		graphics.Flush();
		SetViewportOrgEx(bufferDc, 0, 0, nullptr);
		BitBlt(dc, paint.rcPaint.left, paint.rcPaint.top, dirtyWidth, dirtyHeight, bufferDc, 0, 0, SRCCOPY);
		SelectObject(bufferDc, previousBitmap); DeleteObject(bufferBitmap); DeleteDC(bufferDc);
		EndPaint(window, &paint); return 0;
	}
	return DefWindowProcW(window, message, wParam, lParam);
}

static bool chatCommandAllowed(const char *command, const TwitchChatMessage &m)
{
	if (m.broadcaster) return true;
	const std::string base = std::string("command.") + command + ".";
	auto enabled=[&](const char *role){const std::wstring key=utf8ToWide(base+role);return musicBool(key.c_str(),rsChatCommandDefault(command,role));};
	if (enabled("everyone")) return true;
	return (m.subscriber && enabled("subscriber")) || (m.vip && enabled("vip")) ||
		(m.moderator && enabled("moderator"));
}

static std::string nextChatRequestId()
{
	return "R" + std::to_string(g_sessionRequestNumber++);
}

static void runTransportAction(int action)
{
	const bool youtubeActive = g_youtubePlayer && g_youtubePlayer->active();
	const bool currentlyPlaying = youtubeActive ? g_youtubePlayer->playing() : currentPlaying();
	if (externalActive()) {
		const SystemMediaProvider::Action externalAction = action == 0 ? SystemMediaProvider::Action::Previous :
			(action == 1 ? SystemMediaProvider::Action::Restart : (action == 3 ? SystemMediaProvider::Action::Next :
			(action == 4 || currentlyPlaying ? SystemMediaProvider::Action::Pause : SystemMediaProvider::Action::Play)));
		commandExternalPlayer(externalAction);
		return;
	}
	if (action == 2 && !youtubeActive && (!g_player || g_player->state() == L"stopped") && !g_hub.hasCurrent())
		playHubNext();
	else if (action == 0) {
		HubTrack previous;
		if (g_hub.takePrevious(previous)) startHubTrack(previous, false);
	} else if (action == 3)
		playHubNext();
	else {
		const char *command = action == 1 ? "RESTART" :
			(action == 4 ? "STOP" : (currentlyPlaying ? "PAUSE" : "PLAY"));
		if (youtubeActive) g_youtubePlayer->command(command);
		else if (g_player) g_player->command(command);
	}
}

static void runChatTransport(const std::string &action)
{
	if (action == "PREVIOUS") runTransportAction(0);
	else if (action == "RESTART") runTransportAction(1);
	else if (action == "SKIP") runTransportAction(3);
	else if (action == "PLAY") {
		const bool playing = g_youtubePlayer && g_youtubePlayer->active() ? g_youtubePlayer->playing() : currentPlaying();
		if (!playing) runTransportAction(2);
	} else if (action == "PAUSE") {
		const bool playing = g_youtubePlayer && g_youtubePlayer->active() ? g_youtubePlayer->playing() : currentPlaying();
		if (playing) runTransportAction(2);
	}
}

static constexpr int ID_MEDIA_PLAY_PAUSE = 4301;
static constexpr int ID_MEDIA_STOP = 4302;
static constexpr int ID_MEDIA_PREVIOUS = 4303;
static constexpr int ID_MEDIA_NEXT = 4304;

static void releaseHubMediaKeys(HWND window)
{
	if (!g_hubMediaKeysRegistered) return;
	UnregisterHotKey(window, ID_MEDIA_PLAY_PAUSE);
	UnregisterHotKey(window, ID_MEDIA_STOP);
	UnregisterHotKey(window, ID_MEDIA_PREVIOUS);
	UnregisterHotKey(window, ID_MEDIA_NEXT);
	g_hubMediaKeysRegistered = false;
	traceLog("media-keys-released");
}

static void updateHubMediaKeyRegistration(HWND window)
{
	if (externalActive()) {
		releaseHubMediaKeys(window);
		return;
	}
	if (g_hubMediaKeysRegistered) return;
	const bool playPause = RegisterHotKey(window, ID_MEDIA_PLAY_PAUSE, MOD_NOREPEAT, VK_MEDIA_PLAY_PAUSE) != FALSE;
	const bool stop = RegisterHotKey(window, ID_MEDIA_STOP, MOD_NOREPEAT, VK_MEDIA_STOP) != FALSE;
	const bool previous = RegisterHotKey(window, ID_MEDIA_PREVIOUS, MOD_NOREPEAT, VK_MEDIA_PREV_TRACK) != FALSE;
	const bool next = RegisterHotKey(window, ID_MEDIA_NEXT, MOD_NOREPEAT, VK_MEDIA_NEXT_TRACK) != FALSE;
	const DWORD registrationError = GetLastError();
	if (playPause && stop && previous && next) {
		g_hubMediaKeysRegistered = true;
		traceLog("media-keys-registered");
		return;
	}
	if (playPause) UnregisterHotKey(window, ID_MEDIA_PLAY_PAUSE);
	if (stop) UnregisterHotKey(window, ID_MEDIA_STOP);
	if (previous) UnregisterHotKey(window, ID_MEDIA_PREVIOUS);
	if (next) UnregisterHotKey(window, ID_MEDIA_NEXT);
	traceLog("media-keys-registration-failed", "error=" + std::to_string(registrationError));
}

static void beginChatRequest(HWND window, const TwitchChatMessage &m, const std::string &query)
{
	const std::string requestId;int level=0;if(m.subscriber)level|=1;if(m.vip)level|=2;if(m.moderator)level|=4;if(m.broadcaster)level|=8;
	const bool spotifyLink=query.rfind("spotify:track:",0)==0||query.find("open.spotify.com/track/")!=std::string::npos;
	if(spotifyLink&&!externalActive()){sendTwitchMessage("Spotify requests are only available when Spotify is selected in the Hub.");return;}
	if(externalActive()&&!g_spotify.state().authorized){sendTwitchMessage("Spotify requests require a connected Spotify Premium account in Suite Settings.");return;}
	const bool spotifyRequest=externalActive()&&g_spotify.state().authorized;
	if(spotifyRequest){std::thread([window,requestId,m,query,level]{auto*r=new HubSearchResult;SpotifyQueueTrack s;if(g_spotify.searchTrack(query,s)){r->track.id=requestId;r->track.provider="spotify";r->track.providerId=s.uri;r->track.title=s.title;r->track.artist=s.artist;r->track.album=s.album;r->track.artworkUrl=s.artworkUrl;r->track.durationSeconds=int(s.durationMs/1000);r->track.request=true;r->track.requestedBy=m.displayName;r->track.requesterId=m.userId;r->track.requesterLevel=level;}else{r->track.id=requestId;r->error="Spotify could not find that track. Try a more specific title and artist or paste a Spotify track link.";}if(!PostMessageW(window,WM_HUB_REQUEST_RESULT,0,reinterpret_cast<LPARAM>(r)))delete r;}).detach();return;}
	const HubYouTubeSafetyOptions safety=youtubeSafetyOptions();
	std::thread([window,requestId,m,query,level,safety]{auto*r=new HubSearchResult(resolveHubSearch(query,m.displayName,safety));r->track.id=requestId;r->track.requesterId=m.userId;r->track.requesterLevel=level;if(!PostMessageW(window,WM_HUB_REQUEST_RESULT,0,reinterpret_cast<LPARAM>(r)))delete r;}).detach();
}

static void handleTwitchChat(HWND window, const TwitchChatMessage &m)
{
	std::string text=m.text;while(!text.empty()&&std::isspace((unsigned char)text.front()))text.erase(text.begin());while(!text.empty()&&std::isspace((unsigned char)text.back()))text.pop_back();
	std::string lower=text;std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return char(std::tolower(c));});
	if(lower.rfind("!sr",0)==0){if(!musicBool(L"requestsEnabled",true)){sendTwitchMessage("Song requests are turned off for this stream.");return;}if(!chatCommandAllowed("sr",m)){sendTwitchMessage(m.displayName+": you don't have permission to request songs.");return;}std::string q=text.size()>3?text.substr(3):"";while(!q.empty()&&std::isspace((unsigned char)q.front()))q.erase(q.begin());if(q.empty()){sendTwitchMessage(m.displayName+": usage is !sr <song name or supported music link>");return;}beginChatRequest(window,m,q);return;}
	struct Command{const char*text;const char*key;const char*action;const char*reply;};
	for(const Command &c:std::initializer_list<Command>{{"!play","play","PLAY","Playback resumed"},{"!pause","pause","PAUSE","Playback paused"},{"!skip","skip","SKIP","Track skipped"},{"!restart","restart","RESTART","Track restarted"},{"!previous","previous","PREVIOUS","Playing the previous track"},{"!prev","previous","PREVIOUS","Playing the previous track"}}){if(lower==c.text){if(!chatCommandAllowed(c.key,m)){sendTwitchMessage(m.displayName+": you don't have permission to control playback.");return;}runChatTransport(c.action);sendTwitchMessage(c.reply);return;}}
	if(lower=="!remove"||lower.rfind("!remove ",0)==0){if(!chatCommandAllowed("remove",m)){sendTwitchMessage(m.displayName+": you don't have permission to remove requests.");return;}std::string id=text.size()>7?text.substr(7):"";while(!id.empty()&&(id.front()==' '||id.front()=='#'))id.erase(id.begin());if(!id.empty()&&(id.front()=='r'||id.front()=='R'))id.front()='R';else if(!id.empty())id='R'+id;if(id.empty()){sendTwitchMessage("Usage: !remove #<request ID>");return;}HubTrack removed;for(const auto&t:g_hub.requests())if(t.id==id){removed=t;break;}if(removed.provider=="spotify"){g_hub.cancelRequest(id);saveHubState();syncHubQueueView();sendTwitchMessage("Removed request #"+id+": "+removed.title+" - "+removed.artist);return;}if(!removed.id.empty()&&g_hub.removeRequest(id)){saveHubState();syncHubQueueView();sendTwitchMessage("Removed request #"+id+": "+removed.title+" - "+removed.artist);}else sendTwitchMessage("Could not remove request #"+id+": That request is not waiting in the queue.");}
}

static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
	const bool betaExpired = RsBeta::currentState().expired;
	if(message==WM_TWITCH_CHAT){std::unique_ptr<TwitchChatMessage>m(reinterpret_cast<TwitchChatMessage*>(lParam));if(m&&!betaExpired)handleTwitchChat(window,*m);return 0;}
	if(message==WM_OBS_CONNECTION_CHANGED){if(g_overlayDesigner)g_overlayDesigner->refresh();return 0;}
	if (betaExpired && (message == WM_HOTKEY || message == WM_COMMAND || message == WM_LBUTTONDOWN ||
		message == WM_LBUTTONUP || message == WM_LBUTTONDBLCLK || message == WM_RBUTTONDOWN || message == WM_RBUTTONUP)) return 0;
	if (message == WM_HOTKEY && g_hubMediaKeysRegistered) {
		if (wParam == ID_MEDIA_PLAY_PAUSE)
			runTransportAction(2);
		else if (wParam == ID_MEDIA_STOP)
			runTransportAction(4);
		else if (wParam == ID_MEDIA_PREVIOUS)
			runTransportAction(0);
		else if (wParam == ID_MEDIA_NEXT)
			runTransportAction(3);
		else
			return DefWindowProcW(window, message, wParam, lParam);
		InvalidateRect(window, nullptr, FALSE);
		return 0;
	}
	if (message == WM_CLOSE) { traceLog("window-close-request"); g_closeRequested = true; return 0; }
	if (message == WM_DESTROY) traceLog("window-destroy");
	if (message == WM_NCDESTROY) traceLog("window-nc-destroy");
	if (message == WM_ACTIVATE) { std::ostringstream detail; detail << "state=" << LOWORD(wParam) << " minimized=" << HIWORD(wParam) << " other_hwnd=0x" << std::hex << static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(reinterpret_cast<HWND>(lParam))); traceLog("window-activate", detail.str()); }
	if (message == WM_SETFOCUS || message == WM_KILLFOCUS) { std::ostringstream detail; detail << "other_hwnd=0x" << std::hex << static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(reinterpret_cast<HWND>(wParam))); traceLog(message == WM_SETFOCUS ? "window-set-focus" : "window-kill-focus", detail.str()); }
	if (message == WM_SHOWWINDOW) { std::ostringstream detail; detail << "shown=" << (wParam ? 1 : 0) << " status=" << static_cast<long long>(lParam); traceLog("window-show", detail.str()); }
	if (message == WM_SYSCOMMAND) { std::ostringstream detail; detail << "command=0x" << std::hex << (static_cast<unsigned long long>(wParam) & 0xfff0ULL); traceLog("window-system-command", detail.str()); }
	if (message == WM_SETTINGCHANGE) { std::ostringstream detail; detail << "action=" << static_cast<unsigned long long>(wParam); if (lParam) detail << " section=" << wideToUtf8(reinterpret_cast<const wchar_t *>(lParam)); traceLog("window-setting-change", detail.str()); }
	if (message == WM_SIZE) { std::ostringstream detail; detail << "type=" << static_cast<unsigned long long>(wParam) << " width=" << LOWORD(lParam) << " height=" << HIWORD(lParam); traceLog("window-size", detail.str()); }
	if (message == WM_DISPLAYCHANGE) { std::ostringstream detail; detail << "bpp=" << wParam << " width=" << LOWORD(lParam) << " height=" << HIWORD(lParam); traceLog("window-display-change", detail.str()); }
	if (message == WM_DPICHANGED) { std::ostringstream detail; detail << "dpi_x=" << LOWORD(wParam) << " dpi_y=" << HIWORD(wParam); traceLog("window-dpi-change", detail.str()); }
	if (message == WM_DEVICECHANGE) { traceLog("window-device-change", std::to_string(static_cast<unsigned long long>(wParam))); }
	if (message == WM_POWERBROADCAST) { traceLog("window-power-broadcast", std::to_string(static_cast<unsigned long long>(wParam))); }
	if (message == WM_DRAWITEM) {
		auto *item = reinterpret_cast<DRAWITEMSTRUCT *>(lParam);
		if (item && item->CtlType == ODT_BUTTON && item->CtlID >= ID_IMPORT_PLAYLIST && item->CtlID <= ID_CLEAR_LOCAL) { drawBrandedButton(*item); return TRUE; }
	}
	if (message == WM_CTLCOLOREDIT) {
		SetTextColor(reinterpret_cast<HDC>(wParam), RGB(230, 232, 235)); SetBkColor(reinterpret_cast<HDC>(wParam), RGB(17, 24, 33));
		return reinterpret_cast<LRESULT>(g_controlBackgroundBrush);
	}
	if (message == WM_COMMAND && LOWORD(wParam) == ID_OVERLAY_CUSTOM_TEXT && HIWORD(wParam) == EN_CHANGE && g_overlayCustomTextEdit) {
		wchar_t text[512]{}; GetWindowTextW(g_overlayCustomTextEdit, text, 512); setOverlaySetting(L"customText", text);
		setOverlaySetting(L"showCustomText", text[0] ? L"true" : L"false"); return 0;
	}
	if (message == WM_COMMAND && LOWORD(wParam) == ID_OVERLAY_FONT && HIWORD(wParam) == CBN_SELCHANGE && g_overlayFontCombo) {
		wchar_t text[128]{}; GetWindowTextW(g_overlayFontCombo, text, 128); setOverlaySetting(L"fontFamily", text); InvalidateRect(window,nullptr,FALSE); return 0;
	}
	if (message == WM_COMMAND && LOWORD(wParam) >= ID_OVERLAY_TITLE_SIZE && LOWORD(wParam) <= ID_OVERLAY_HEIGHT && HIWORD(wParam) == EN_CHANGE) {
		const int index = LOWORD(wParam) - ID_OVERLAY_TITLE_SIZE; wchar_t text[64]{}; GetWindowTextW(g_overlayStyleEdits[index], text, 64);
		const wchar_t *keys[] = {L"titleSize",L"bodySize",L"backgroundOpacity",L"backgroundColour",L"textColour",L"accentColour",L"width",L"height"};
		if (index == 3 || index == 4 || index == 5) {
			const size_t length=wcslen(text); if(text[0]==L'#' && (length==4 || length==7 || length==9)) setOverlaySetting(keys[index],text);
		} else if (text[0]) {
			wchar_t *end=nullptr; const unsigned long value=wcstoul(text,&end,10);
			if(end && *end==L'\0' && value>0 && (index!=2 || value<=100)) setOverlayNumber(keys[index],DWORD(value));
		}
		InvalidateRect(window,nullptr,FALSE); return 0;
	}
	if (message == WM_COMMAND && HIWORD(wParam) == BN_CLICKED && LOWORD(wParam) >= ID_ADD_LOCAL_FILES && LOWORD(wParam) <= ID_USE_EXTERNAL) {
		const int action = LOWORD(wParam);
		if (action == ID_CLEAR_LOCAL) {
			if (g_hub.activeSource() == "local") { if (g_player) g_player->command("STOP"); g_hub.clearCurrent(); }
			g_hub.clearLocalLibrary(); g_libraryStatus = L"Local library cleared.";
		} else if (action == ID_USE_LOCAL || action == ID_USE_YOUTUBE || action == ID_USE_EXTERNAL) {
			const std::string source = action == ID_USE_LOCAL ? "local" : (action == ID_USE_EXTERNAL ? "external" : "youtube");
			traceLog("provider-switch-request", "from=" + g_hub.activeSource() + " to=" + source);
			if (source == "local" && g_hub.localLibrary().empty()) g_libraryStatus = L"Add local files or a folder before selecting Local files.";
			else if (source == "youtube" && g_hub.youtubeFallback().empty()) g_libraryStatus = L"Import a YouTube fallback playlist before selecting YouTube.";
			else {
				if (g_player) g_player->command("STOP"); if (g_youtubePlayer) { g_youtubePlayer->command("STOP"); g_youtubePlayer->hide(); }
				g_hub.activateSource(source);
				if (source == "local") disableSongRequestsForLocalSource();
				if (source == "external") {
					g_hub.clearCurrent();
					g_externalState = {};
					g_externalPositionTrack.clear();
					if (g_player) g_player->setMetadata("No track playing\t\t\t");
					syncHubQueueView();
				} else {
					syncHubQueueView();
					playHubNext();
				}
				updateHubMediaKeyRegistration(window);
				traceLog("provider-switch-complete", "active=" + g_hub.activeSource());
				g_libraryStatus = source == "local" ? L"Local files are now the active music source. Chat requests are unavailable." : (source == "external" ? L"Spotify selected. Open the Spotify desktop app to begin playback." : L"YouTube is now the active music source.");
			}
		} else {
			std::vector<HubTrack> imported;
			if (action == ID_ADD_LOCAL_FILES) imported = scanLocalAudioFiles(chooseLocalAudioFiles(window));
			else { const std::wstring folder = chooseLocalAudioFolder(window); if (!folder.empty()) imported = scanLocalAudioFolder(folder); }
			if (!imported.empty()) {
				std::vector<HubTrack> library = g_hub.localLibrary();
				for (HubTrack &track : imported) {
					const auto found = std::find_if(library.begin(), library.end(), [&](const HubTrack &item) { return item.providerId == track.providerId; });
					if (found == library.end()) library.push_back(std::move(track));
				}
				g_hub.replaceLocalLibrary(std::move(library));
				g_libraryStatus = L"Added " + std::to_wstring(imported.size()) + L" local track(s).";
			}
		}
		g_queuePage = 0; syncHubQueueView(); saveHubState(); if(g_overlayDesigner)g_overlayDesigner->refresh(); InvalidateRect(window, nullptr, FALSE); return 0;
	}
	if (message == WM_COMMAND && LOWORD(wParam) == ID_IMPORT_PLAYLIST && HIWORD(wParam) == BN_CLICKED) {
		const int length = GetWindowTextLengthW(g_playlistEdit);
		std::wstring value(size_t(length + 1), L'\0');
		if (length > 0) GetWindowTextW(g_playlistEdit, value.data(), length + 1);
		value.resize(size_t(length));
		const std::string url = wideToUtf8(value);
		if (url.empty()) { g_libraryStatus = L"Enter a playlist URL first."; if(g_overlayDesigner)g_overlayDesigner->refresh(); InvalidateRect(window, nullptr, FALSE); return 0; }
		g_libraryStatus = L"Importing playlist into the Suite Media Player...";
		EnableWindow(g_importPlaylistButton, FALSE); if(g_overlayDesigner)g_overlayDesigner->refresh(); InvalidateRect(window, nullptr, FALSE);
		std::thread([window, url] {
			auto *result = new HubPlaylistResult(resolveHubPlaylist(url));
			if (!PostMessageW(window, WM_HUB_PLAYLIST_RESULT, 0, reinterpret_cast<LPARAM>(result))) delete result;
		}).detach();
		return 0;
	}
	if (message == WM_HUB_PLAYLIST_RESULT) {
		std::unique_ptr<HubPlaylistResult> result(reinterpret_cast<HubPlaylistResult *>(lParam));
		EnableWindow(g_importPlaylistButton, TRUE);
		if (!result || !result->error.empty())
			g_libraryStatus = L"Could not import playlist: " + utf8ToWide(result ? result->error : "Unknown error");
		else {
			const size_t count = result->tracks.size();
			g_hub.replaceFallback(std::move(result->tracks), result->label, result->sourceUrl);
			g_queuePage = 0; syncHubQueueView();
			g_libraryStatus = L"Imported " + std::to_wstring(count) + L" tracks. The player now owns this fallback playlist.";
			if (!g_hub.hasCurrent()) playHubNext(); else saveHubState();
		}
		if(g_overlayDesigner)g_overlayDesigner->refresh(); InvalidateRect(window, nullptr, FALSE); return 0;
	}
	if (message == WM_HUB_REQUEST_RESULT) {
		std::unique_ptr<HubSearchResult> result(reinterpret_cast<HubSearchResult *>(lParam));
		if (result && result->error.empty()) {
			if (!musicBool(L"requestsEnabled", true))
				result->error = "Song requests are currently disabled.";
			auto roleLevel = [](const std::wstring &role) {
				if (role == L"subscriber") return 1; if (role == L"vip") return 2;
				if (role == L"moderator") return 3; if (role == L"broadcaster") return 4; return 0;
			};
			const int minimumLevel = roleLevel(musicSetting(L"minimumRole", L"everyone"));
			const int roles = result->track.requesterLevel;
			const int highestLevel = (roles & 8) ? 4 : (roles & 4) ? 3 : (roles & 2) ? 2 : (roles & 1) ? 1 : 0;
			const std::wstring legacyExempt = musicSetting(L"exemptRole", L"moderator");
			const bool exempt = ((roles & 1) && musicBool(L"exemptSubscriber", legacyExempt == L"subscriber")) ||
				((roles & 2) && musicBool(L"exemptVip", legacyExempt == L"subscriber" || legacyExempt == L"vip")) ||
				((roles & 4) && musicBool(L"exemptModerator", legacyExempt != L"none" && legacyExempt != L"broadcaster")) ||
				((roles & 8) && musicBool(L"exemptBroadcaster", legacyExempt != L"none"));
			const auto queuedRequests = g_hub.requests();
			if (result->error.empty() && highestLevel < minimumLevel)
				result->error = "You do not have the required viewer role to request songs.";
			const int queueLimit = _wtoi(musicSetting(L"maxQueueTotal", L"50").c_str());
			if (result->error.empty() && !exempt && queueLimit > 0 && int(queuedRequests.size()) >= queueLimit)
				result->error = "The request queue is full.";
			const int userLimit = _wtoi(musicSetting(L"maxPerUser", L"2").c_str());
			if (result->error.empty() && !exempt && userLimit > 0) {
				const int userQueued = int(std::count_if(queuedRequests.begin(), queuedRequests.end(), [&](const HubTrack &track) {
					return !result->track.requesterId.empty() && track.requesterId == result->track.requesterId;
				}));
				if (userQueued >= userLimit) result->error = "You already have the maximum number of songs queued.";
			}
			const int maxMinutes = _wtoi(musicSetting(L"maxTrackLengthMinutes", L"10").c_str());
			if (maxMinutes > 0 && result->track.durationSeconds > maxMinutes * 60)
				result->error = "That track exceeds the maximum request length.";
			if (result->error.empty() && musicBool(L"playlistOnly", false)) {
				const auto fallback = g_hub.youtubeFallback();
				const bool present = std::any_of(fallback.begin(), fallback.end(), [&](const HubTrack &track) {
					return track.providerId == result->track.providerId;
				});
				if (!present) result->error = "Requests are currently limited to the fallback playlist.";
			}
			if (result->error.empty() && musicBool(L"preventDuplicates", true)) {
				bool duplicate = g_hub.hasCurrent() && g_hub.current().providerId == result->track.providerId;
				for (const HubTrack &track : g_hub.requests()) duplicate = duplicate || track.providerId == result->track.providerId;
				if (duplicate) result->error = "That track is already playing or waiting in the queue.";
			}
		}
		if (result && result->error.empty()) {
			if (result->track.provider == "spotify" && !g_spotify.addToQueue(result->track.providerId))
				result->error = "Spotify could not add that track. Make sure Spotify is playing on an active device.";
		}
		if (result && result->error.empty()) {
			const std::string pendingId = result->track.id;
			result->track.id = nextChatRequestId();
			const HubTrack accepted = result->track;
			g_hub.enqueueRequest(std::move(result->track)); saveHubState(); syncHubQueueView();
			if (accepted.provider == "spotify") {
				std::lock_guard<std::mutex> lock(g_recentSpotifyRequestMutex);
				g_recentSpotifyRequests[accepted.providerId] = GetTickCount64();
			}
			int position = int(g_hub.requests().size());
			if (accepted.provider == "spotify") {
				g_spotify.refreshQueue(); const auto spotify = g_spotify.state();
				for (size_t i = 0; i < spotify.queue.size(); ++i) if (spotify.queue[i].uri == accepted.providerId) { position = int(i + 1); break; }
			}
			{
				sendTwitchMessage(accepted.title + " - " + accepted.artist + ", requested by " + accepted.requestedBy +
					", was added as request #" + accepted.id + " (queue position " + std::to_string(position) + ").");
				std::lock_guard<std::mutex> lock(g_hostEventMutex);
				g_hostEvents.push_back("HOST\tREQUEST_ACCEPTED\t" + accepted.id + "\t" + accepted.title + "\t" +
					accepted.artist + "\t" + accepted.requestedBy + "\t" + std::to_string(position) + "\t" + pendingId + "\n");
			}
			if (accepted.provider != "spotify" && !g_hub.hasCurrent()) playHubNext();
		} else if (result) {
			sendTwitchMessage("Request rejected: " + result->error);
			std::lock_guard<std::mutex> lock(g_hostEventMutex);
			g_hostEvents.push_back("HOST\tREQUEST_REJECTED\t" + result->track.id + "\t" + result->error + "\n");
		}
		InvalidateRect(window, nullptr, FALSE); return 0;
	}
	if (message == WM_MOUSEMOVE) {
		if (!g_trackingMouseLeave) {
			TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window, 0};
			TrackMouseEvent(&tracking);
			g_trackingMouseLeave = true;
		}
		POINT point{static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam))};
		const bool overBetaNotice = PtInRect(&g_betaNoticeRect, point) != FALSE;
		if (overBetaNotice != g_betaTooltipVisible) {
			g_betaTooltipVisible = overBetaNotice;
			InvalidateRect(window, nullptr, FALSE);
		}
	}
	if (message == WM_MOUSELEAVE) {
		g_trackingMouseLeave = false;
		if (g_betaTooltipVisible) {
			g_betaTooltipVisible = false;
			InvalidateRect(window, nullptr, FALSE);
		}
	}
	if (!g_transportSeeking && g_transportPressed < 0 &&
	    (message == WM_MOUSEMOVE || message == WM_LBUTTONDOWN) &&
	    g_page == 0 && g_youtubePlayer &&
	    g_youtubePlayer->sendMouse(message, wParam, lParam)) return 0;
	if (message == WM_LBUTTONDOWN) {
		POINT point{static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam))};
		if (PtInRect(&g_transportProgress, point)) {
			const bool youtubeActive = g_youtubePlayer && g_youtubePlayer->active();
			const float duration = youtubeActive ? g_youtubePlayer->duration() : currentDuration();
			if (duration > 0.0f) {
				const int width = std::max(1L, g_transportProgress.right - g_transportProgress.left);
				const double fraction = std::clamp(double(point.x - g_transportProgress.left) / width, 0.0, 1.0);
				g_transportSeeking = true;
				g_transportSeekWasPlaying = youtubeActive ? g_youtubePlayer->playing() : currentPlaying();
				g_transportSeekTargetMs = int64_t(fraction * duration * 1000.0);
				SetCapture(window);
				InvalidateRect(window, nullptr, FALSE);
				return 0;
			}
		}
		for (int i = 0; i < 5; ++i) {
			if (!PtInRect(&g_transportButtons[i], point)) continue;
			g_transportPressed = i;
			g_transportPressedUntil = 0;
			SetCapture(window);
			InvalidateRect(window, nullptr, FALSE);
			return 0;
		}
	}
	if (message == WM_MOUSEMOVE && g_transportSeeking) {
		POINT point{static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam))};
		const bool youtubeActive = g_youtubePlayer && g_youtubePlayer->active();
		const float duration = youtubeActive ? g_youtubePlayer->duration() : currentDuration();
		const int width = std::max(1L, g_transportProgress.right - g_transportProgress.left);
		const double fraction = std::clamp(double(point.x - g_transportProgress.left) / width, 0.0, 1.0);
		g_transportSeekTargetMs = int64_t(fraction * std::max(0.0f, duration) * 1000.0);
		InvalidateRect(window, nullptr, FALSE);
		return 0;
	}
	if (message == WM_GETMINMAXINFO) {
		auto *limits = reinterpret_cast<MINMAXINFO *>(lParam);
		limits->ptMinTrackSize = POINT{760, 560};
		return 0;
	}
	if (message == WM_SETCURSOR && LOWORD(lParam) == HTCLIENT) {
		POINT point{};
		GetCursorPos(&point);
		ScreenToClient(window, &point);
		if (PtInRect(&g_transportProgress, point)) {
			SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32649)));
			return TRUE;
		}
	}
	if (message == WM_SIZE) {
		if (g_youtubePlayer) g_youtubePlayer->resize();
		positionLibraryControls(window);
		positionOverlayControls(window);
		InvalidateRect(window, nullptr, FALSE);
		return 0;
	}
	if (message == WM_LBUTTONUP) {
		if (g_transportSeeking) {
			g_transportSeeking = false;
			ReleaseCapture();
			g_transportSeekPending = true;
			g_transportSeekPendingSince = GetTickCount64();
			const std::string target = std::to_string(g_transportSeekTargetMs);
			if (externalActive()) {
				commandExternalPlayer(SystemMediaProvider::Action::Seek, g_transportSeekTargetMs);
				if (g_transportSeekWasPlaying) commandExternalPlayer(SystemMediaProvider::Action::Play);
			} else if (g_youtubePlayer && g_youtubePlayer->active()) {
				g_youtubePlayer->command("SEEK", target);
				if (g_transportSeekWasPlaying) g_youtubePlayer->command("PLAY");
			} else if (g_player) {
				g_player->command("SEEK\t" + target);
				if (g_transportSeekWasPlaying) g_player->command("PLAY");
			}
			InvalidateRect(window, nullptr, FALSE);
			return 0;
		}
		if (g_page == 0 && g_youtubePlayer && g_youtubePlayer->sendMouse(message, wParam, lParam)) return 0;
		POINT point{static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam))};
		if (g_transportPressed >= 0) {
			const bool releasedOnButton = PtInRect(&g_transportButtons[g_transportPressed], point) != FALSE;
			ReleaseCapture();
			if (releasedOnButton)
				g_transportPressedUntil = GetTickCount64() + 160;
			else {
				g_transportPressed = -1;
				g_transportPressedUntil = 0;
				InvalidateRect(window, nullptr, FALSE);
				return 0;
			}
		}
		for (int i = 0; i < 5; ++i) {
			if (!PtInRect(&g_transportButtons[i], point)) continue;
			runTransportAction(i);
			InvalidateRect(window, nullptr, FALSE);
			return 0;
		}
		RECT client{}; GetClientRect(window, &client);
		const int sidebar = sidebarWidthFor(client.right);
		if (PtInRect(&g_sidebarToggle, point)) {
			g_sidebarCollapsed = !g_sidebarCollapsed;
			if (g_youtubePlayer) g_youtubePlayer->resize();
			if (g_overlayDesigner) g_overlayDesigner->resize();
			InvalidateRect(window, nullptr, FALSE);
			return 0;
		}
		if (g_page == 1 && PtInRect(&g_queuePreviousPage, point)) { g_queuePage = std::max(0, g_queuePage - 1); InvalidateRect(window, nullptr, FALSE); return 0; }
		if (g_page == 1 && PtInRect(&g_queueNextPage, point)) { ++g_queuePage; InvalidateRect(window, nullptr, FALSE); return 0; }
		if (g_page == 1 && PtInRect(&g_queueShuffle, point)) {
			if (!externalActive()) {
				if (g_hub.fallbackShuffled()) g_hub.restoreFallbackOrder();
				else g_hub.shuffleFallback();
				saveHubState(); g_queuePage = 0; syncHubQueueView();
			}
			InvalidateRect(window, nullptr, FALSE); return 0;
		}
		if (g_page == 1 && PtInRect(&g_queuePlayFromBeginning, point)) {
			if (!externalActive()) {
				HubTrack first;
				if (g_hub.restartFallback(first)) startHubTrack(first);
				saveHubState(); g_queuePage = 0; syncHubQueueView();
			}
			InvalidateRect(window, nullptr, FALSE); return 0;
		}
		if (g_page == 3) {
			for (int i = 0; i < 2; ++i) if (PtInRect(&g_overlayTabs[i], point)) {
				g_overlaySection = i; positionOverlayControls(window); InvalidateRect(window, nullptr, FALSE); return 0;
			}
			if (g_overlaySection == 1) {
				return 0;
			}
			const wchar_t *boolKeys[] = {L"showArtwork", L"showTitle", L"showArtist", L"showAlbum", L"showRequester", L"showProgress"};
			for (int i = 0; i < 6; ++i) if (PtInRect(&g_overlayOptions[i], point)) {
				toggleOverlaySetting(boolKeys[i], i != 4); InvalidateRect(window, nullptr, FALSE); return 0;
			}
			if (PtInRect(&g_overlayOptions[6], point)) {
				setOverlaySetting(L"artworkPosition", overlaySetting(L"artworkPosition", L"left") == L"left" ? L"right" : L"left");
				InvalidateRect(window, nullptr, FALSE); return 0;
			}
			if (PtInRect(&g_overlayOptions[7], point)) {
				const std::wstring current = overlaySetting(L"timingMode", L"elapsedTotal");
				setOverlaySetting(L"timingMode", current == L"elapsedTotal" ? L"remaining" : (current == L"remaining" ? L"none" : L"elapsedTotal"));
				InvalidateRect(window, nullptr, FALSE); return 0;
			}
			if (PtInRect(&g_overlayOptions[8], point)) { toggleOverlaySetting(L"artworkBackground", false); InvalidateRect(window, nullptr, FALSE); return 0; }
			if (PtInRect(&g_overlayOptions[9], point)) { toggleOverlaySetting(L"backgroundTransparent", false); InvalidateRect(window, nullptr, FALSE); return 0; }
			if (PtInRect(&g_overlayReset, point)) {
				RegDeleteTreeW(HKEY_CURRENT_USER, kOverlayRegistry); if(g_overlayCustomTextEdit) SetWindowTextW(g_overlayCustomTextEdit,L""); InvalidateRect(window, nullptr, FALSE); return 0;
			}
		}
		const int navStart = 104;
		if (point.x < sidebar && point.y >= navStart && point.y < navStart + 416) {
			static const int navOrder[] = {7, 0, 1, 2, 3, 8, 6, 9};
			g_page = navOrder[std::clamp((static_cast<int>(point.y) - navStart) / 52, 0, 7)];
			positionLibraryControls(window);
			positionOverlayControls(window);
			if (g_youtubePlayer && g_youtubePlayer->active())
				g_youtubePlayer->resize();
			InvalidateRect(window, nullptr, FALSE);
		}
		return 0;
	}
	if (message == WM_LBUTTONDBLCLK && g_page == 1) {
		RECT client{}; GetClientRect(window, &client);
		const int sidebar = sidebarWidthFor(client.right);
		const int transportTop = std::max(400, int(client.bottom) - 106);
		const int y = static_cast<short>(HIWORD(lParam));
		const int x = static_cast<short>(LOWORD(lParam));
		if (x >= sidebar + 28 && y >= 200 && y < transportTop - 34) {
			const int pageSize = std::max(1, (transportTop - 292) / 48);
			const size_t displayIndex = size_t(g_queuePage * pageSize + (y - 200) / 48);
			if (displayIndex == 0 && g_hub.hasCurrent()) g_youtubePlayer->command("RESTART");
			else {
				const size_t playbackIndex = displayIndex - (g_hub.hasCurrent() ? 1u : 0u);
				HubTrack selected; if (g_hub.selectAt(playbackIndex, selected)) { startHubTrack(selected); saveHubState(); }
			}
			InvalidateRect(window, nullptr, FALSE);
		}
		return 0;
	}
	if (message == WM_ERASEBKGND) return 1;
	if (message != WM_PAINT) return DefWindowProcW(window, message, wParam, lParam);

	PAINTSTRUCT paint{};
	HDC dc = BeginPaint(window, &paint);
	RECT client{}; GetClientRect(window, &client);
	// CEF supplies video frames independently of the shell. When only the
	// video rectangle is dirty, present that frame directly just like the
	// proven CEF PCM player. Clearing/rebuilding the shell first causes a
	// visible flash and compositing a partial frame into a dirty-sized buffer
	// causes coordinate/layer corruption.
	if (g_page == 0 && g_youtubePlayer && g_youtubePlayer->active()) {
		const RECT video = youtubeVideoBounds(window);
		const bool videoOnly = paint.rcPaint.left >= video.left && paint.rcPaint.top >= video.top &&
			paint.rcPaint.right <= video.right && paint.rcPaint.bottom <= video.bottom;
		if (videoOnly) {
			g_youtubePlayer->paintTo(dc);
			EndPaint(window, &paint);
			return 0;
		}
	}
	const int dirtyWidth = std::max(1L, paint.rcPaint.right - paint.rcPaint.left);
	const int dirtyHeight = std::max(1L, paint.rcPaint.bottom - paint.rcPaint.top);
	HDC bufferDc = CreateCompatibleDC(dc);
	HBITMAP bufferBitmap = CreateCompatibleBitmap(dc, dirtyWidth, dirtyHeight);
	HGDIOBJ previousBitmap = SelectObject(bufferDc, bufferBitmap);
	SetViewportOrgEx(bufferDc, -paint.rcPaint.left, -paint.rcPaint.top, nullptr);
	Graphics graphics(bufferDc);
	graphics.SetSmoothingMode(SmoothingModeAntiAlias);
	graphics.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
	graphics.Clear(Color(255, 11, 15, 20));

	const Color primary(255, 230, 232, 235), secondary(255, 178, 189, 204), tertiary(255, 112, 130, 151);
	const Color accent(255, 0, 212, 255), accentSoft(56, 0, 212, 255), signalBlue(255, 10, 140, 255), gold(255, 255, 184, 0);
	const Color surface(255, 17, 24, 33), raised(255, 30, 36, 48), border(255, 48, 59, 74);
	FontFamily family(L"Sora");
	Font display(&family, 28, FontStyleBold, UnitPixel), heading(&family, 20, FontStyleBold, UnitPixel);
	Font body(&family, 15, FontStyleRegular, UnitPixel), bodyBold(&family, 15, FontStyleBold, UnitPixel);
	Font smallFont(&family, 12, FontStyleRegular, UnitPixel);
	const int width = client.right, height = client.bottom;
	const int sidebar = sidebarWidthFor(width);
	const bool expanded = sidebar > 150;
	const int transportTop = std::max(400, height - 106);

	SolidBrush sidebarBrush(Color(255, 8, 14, 22));
	graphics.FillRectangle(&sidebarBrush, 0, 0, sidebar, height);
	roundedPanel(graphics, RectF(14, 16, float(sidebar - 28), 62), 12, raised);
	if (expanded && g_brandHeaderImage && g_brandHeaderImage->GetLastStatus() == Ok) {
		const float boxX = 20.0f, boxY = 21.0f, boxWidth = 168.0f, boxHeight = 52.5f;
		const float imageWidth = static_cast<float>(g_brandHeaderImage->GetWidth());
		const float imageHeight = static_cast<float>(g_brandHeaderImage->GetHeight());
		const float scale = std::min(boxWidth / imageWidth, boxHeight / imageHeight);
		const float drawWidth = imageWidth * scale, drawHeight = imageHeight * scale;
		graphics.DrawImage(g_brandHeaderImage.get(), boxX + (boxWidth - drawWidth) / 2.0f,
			boxY + (boxHeight - drawHeight) / 2.0f, drawWidth, drawHeight);
	} else if (g_brandIconImage && g_brandIconImage->GetLastStatus() == Ok) {
		graphics.DrawImage(g_brandIconImage.get(), 22.0f, 24.0f, 46.0f, 46.0f);
	} else {
		label(graphics, L"RS", heading, RectF(20.0f, 18, 48.0f, 58), accent, StringAlignmentCenter);
	}
	g_sidebarToggle = expanded ? RECT{194, 31, 220, 57} : RECT{78, 31, 104, 57};
	label(graphics, expanded ? L"\u2039" : L"\u203A", heading,
		RectF(float(g_sidebarToggle.left), float(g_sidebarToggle.top), 26, 26), secondary, StringAlignmentCenter);
	const wchar_t *pages[] = {L"Now Playing", L"Queue & Requests", L"Library", L"Music Overlay", L"Settings", L"Accounts", L"Commands", L"Stream Tools", L"Suite Settings", L"Feedback & Diagnostics"};
	const wchar_t *icons[] = {L"\u25B6", L"\u2261", L"\u266B", L"\u25C7", L"\u2699", L"@", L"!", L"+", L"\u2637", L"\u24D8"};
	static const int navOrder[] = {7, 0, 1, 2, 3, 8, 6, 9};
	const float navStart = 104.0f;
	for (int i = 0; i < 8; ++i) {
		const float y = navStart + i * 52.0f;
		const int page = navOrder[i];
		if (page == g_page) {
			roundedPanel(graphics, RectF(13, y, float(sidebar - 26), 42), 9, accentSoft);
			SolidBrush active(accent); graphics.FillRectangle(&active, 13.0f, y + 9.0f, 3.0f, 24.0f);
		}
		label(graphics, icons[page], bodyBold, RectF(23, y, 34, 42), page == g_page ? accent : secondary, StringAlignmentCenter);
		if (expanded) label(graphics, pages[page], body, RectF(63, y, 143, 42), page == g_page ? primary : secondary);
	}
	const RsBeta::State betaState = RsBeta::currentState();
	const Color betaColour = betaState.expired ? Color(255, 255, 74, 87) : gold;
	const std::wstring betaExpiryText = RsBeta::kExpiryEnabled
		? std::wstring(L"Expires ") + utf8ToWide(RsBeta::kExpiryDisplay)
		: utf8ToWide(std::string("Version ") + RsBeta::kVersion);
	const std::wstring betaBadgeText = betaState.expired ? L"EXPIRED" : utf8ToWide(RsBeta::kChannel);
	if (expanded) {
		g_betaNoticeRect = RECT{20, height - 68, sidebar - 20, height - 18};
		roundedPanel(graphics, RectF(20, float(height - 68), float(sidebar - 40), 24), 7, accentSoft);
		label(graphics, betaBadgeText, smallFont,
			RectF(20, float(height - 68), float(sidebar - 40), 24), betaColour, StringAlignmentCenter);
		label(graphics, betaExpiryText, smallFont, RectF(20, float(height - 40), float(sidebar - 40), 20), tertiary,
			StringAlignmentCenter);
	} else {
		g_betaNoticeRect = RECT{18, height - 50, sidebar - 18, height - 20};
		label(graphics, betaState.expired ? L"EXP" : utf8ToWide(RsBeta::kChannel), smallFont,
			RectF(18, float(height - 50), float(sidebar - 36), 24), betaColour, StringAlignmentCenter);
	}
	if (g_betaTooltipVisible && expanded) {
		const float cardY = float(height - 198);
		roundedPanel(graphics, RectF(18, cardY, float(sidebar - 36), 118), 10, raised);
		label(graphics, utf8ToWide(RsBeta::kChannel), bodyBold, RectF(30, cardY + 10, float(sidebar - 60), 22), accent);
		label(graphics, utf8ToWide(std::string("Version ") + RsBeta::kVersion), smallFont,
			RectF(30, cardY + 35, float(sidebar - 60), 18), primary);
		label(graphics, utf8ToWide(std::string("Build ") + RsBeta::kBuildId), smallFont,
			RectF(30, cardY + 55, float(sidebar - 60), 18), secondary);
		label(graphics, utf8ToWide(std::string("Built ") + RsBeta::kBuildDate), smallFont,
			RectF(30, cardY + 75, float(sidebar - 60), 18), secondary);
		if (RsBeta::kExpiryEnabled)
			label(graphics, betaExpiryText, smallFont, RectF(30, cardY + 95, float(sidebar - 60), 18), secondary);
	}

	const float contentX = float(sidebar + 28), contentWidth = float(width - sidebar - 56);
	const wchar_t *subtitles[] = {L"Your stream soundtrack at a glance", L"Manage what plays next",
		L"Organise local music and provider playlists", L"Create a design that fits your stream",
		L"Playback, appearance and accessibility", L"Connect Twitch identities and choose the chat sender", L"Chat controls at a glance", L"Quality-of-life tools for live production",
		L"Guided setup and every Suite preference", L"Export feedback and privacy-redacted diagnostics"};
	label(graphics, pages[g_page], display, RectF(contentX, 22, contentWidth, 44), primary);
	label(graphics, subtitles[g_page], body, RectF(contentX, 64, contentWidth, 28), secondary);

	const bool videoVisible = g_youtubePlayer && g_youtubePlayer->active() && g_page == 0;
	if (g_page == 0 && !videoVisible) {
		const float cardY = 108, cardHeight = float(transportTop) - cardY - 20;
		roundedPanel(graphics, RectF(contentX, cardY, contentWidth, cardHeight), 16, surface);
		const float artSize = std::max(170.0f, std::min(420.0f, std::min(cardHeight - 48, contentWidth * 0.40f)));
		const float artX = contentX + 24, artY = cardY + (cardHeight - artSize) / 2;
		roundedPanel(graphics, RectF(artX, artY, artSize, artSize), 14, raised);
		if (g_player && g_player->artwork() && g_player->artwork()->GetLastStatus() == Ok)
			graphics.DrawImage(g_player->artwork(), artX, artY, artSize, artSize);
		else label(graphics, L"\u266B", display, RectF(artX, artY, artSize, artSize), tertiary, StringAlignmentCenter);
		const float infoX = artX + artSize + 32;
		const float infoWidth = std::max(120.0f, contentX + contentWidth - infoX - 24);
		label(graphics, L"NOW PLAYING", smallFont, RectF(infoX, artY + 14, infoWidth, 22), accent);
		label(graphics, g_player ? g_player->title() : L"No track playing", display, RectF(infoX, artY + 42, infoWidth, 48), primary);
		label(graphics, g_player ? g_player->artist() : L"", heading, RectF(infoX, artY + 92, infoWidth, 32), secondary);
		label(graphics, g_player ? g_player->album() : L"", body, RectF(infoX, artY + 125, infoWidth, 26), tertiary);
		const float queueHeight = std::min(178.0f, std::max(104.0f, artSize - 176.0f));
		const float queueY = artY + artSize - queueHeight;
		roundedPanel(graphics, RectF(infoX, queueY, infoWidth, queueHeight), 10, raised);
		label(graphics, L"UP NEXT", smallFont, RectF(infoX + 14, queueY + 8, 86, 20), accent);
		label(graphics, L"Queue & Requests", bodyBold, RectF(infoX + 14, queueY + 27, infoWidth - 28, 26), primary);
		SolidBrush divider(border);
		graphics.FillRectangle(&divider, infoX + 14, queueY + 57, infoWidth - 28, 1.0f);
		const std::vector<HubTrack> upcoming = g_hub.playbackOrder();
		if (!upcoming.empty()) {
			const HubTrack &next = upcoming.front();
			label(graphics, utf8ToWide(next.title), bodyBold, RectF(infoX + 14, queueY + 65, infoWidth - 28, 26), primary);
			if (queueHeight > 125) {
				std::wstring detail = utf8ToWide(next.artist);
				if (!detail.empty()) detail += L"  |  ";
				detail += next.request ? L"Requested by " + utf8ToWide(next.requestedBy) :
					(next.provider == "local" ? L"Local library" : L"Fallback playlist");
				label(graphics, detail, smallFont, RectF(infoX + 14, queueY + 91, infoWidth - 28, 24), tertiary);
			}
		} else {
			label(graphics, L"No tracks queued", body, RectF(infoX + 14, queueY + 65, infoWidth - 28, 26), secondary);
			if (queueHeight > 125) label(graphics, L"Add music from the Library page", smallFont,
				RectF(infoX + 14, queueY + 91, infoWidth - 28, 24), tertiary);
		}
	} else if (g_page == 1) {
		g_queuePreviousPage = {};
		g_queueNextPage = {};
		g_queueShuffle = {};
		g_queuePlayFromBeginning = {};
		const float panelY = 108, panelHeight = float(transportTop - 130);
		roundedPanel(graphics, RectF(contentX, panelY, contentWidth, panelHeight), 16, surface);
		label(graphics, L"Playback order", heading, RectF(contentX + 28, 128, contentWidth - 56, 34), primary);
		label(graphics, L"Requests play first, followed by the fallback playlist.", body,
			RectF(contentX + 28, 164, contentWidth - 56, 28), secondary);
		const int pageSize = std::max(1, int((panelHeight - 162) / 48));
		const int pageCount = std::max(1, (int(g_queue.size()) + pageSize - 1) / pageSize);
		g_queuePage = std::clamp(g_queuePage, 0, pageCount - 1);
		const int offset = g_queuePage * pageSize;
		const int visibleRows = std::min<int>(std::max(0, int(g_queue.size()) - offset), pageSize);
		for (int i = 0; i < visibleRows; ++i) {
			const QueueItem &item = g_queue[size_t(offset + i)]; const float y = 204.0f + i * 48.0f;
			if (i % 2) roundedPanel(graphics, RectF(contentX + 18, y - 4, contentWidth - 36, 44), 7, raised);
			label(graphics, std::to_wstring(offset + i + 1) + L". " + (item.title.empty() ? L"Untitled track" : item.title),
				bodyBold, RectF(contentX + 30, y, contentWidth * 0.48f, 24), primary);
			label(graphics, item.artist, smallFont, RectF(contentX + 30, y + 22, contentWidth * 0.48f, 20), secondary);
			label(graphics, item.source, item.current ? bodyBold : body, RectF(contentX + contentWidth * 0.56f, y, contentWidth * 0.25f, 24), item.current ? accent : secondary);
			label(graphics, clockText(float(item.durationSeconds)), body,
				RectF(contentX + contentWidth - 102, y, 70, 24), tertiary, StringAlignmentFar);
		}
		if (g_queue.empty()) label(graphics, L"No upcoming tracks", body,
			RectF(contentX + 28, 216, contentWidth - 56, 30), secondary);
		if (!g_queue.empty()) {
			const float pageY = float(transportTop - 58);
			g_queuePreviousPage = RECT{int(contentX + 28), int(pageY), int(contentX + 138), int(pageY + 34)};
			g_queueNextPage = RECT{int(contentX + contentWidth - 138), int(pageY), int(contentX + contentWidth - 28), int(pageY + 34)};
			const float centreX = contentX + contentWidth / 2;
			g_queueShuffle = externalActive() ? RECT{} : RECT{int(centreX - 166), int(pageY), int(centreX - 8), int(pageY + 34)};
			g_queuePlayFromBeginning = externalActive() ? RECT{} : RECT{int(centreX + 8), int(pageY), int(centreX + 166), int(pageY + 34)};
			roundedPanel(graphics, RectF(float(g_queuePreviousPage.left), pageY, 110, 34), 7, raised);
			roundedPanel(graphics, RectF(float(g_queueNextPage.left), pageY, 110, 34), 7, raised);
			label(graphics, L"Previous page", smallFont, RectF(float(g_queuePreviousPage.left), pageY, 110, 34), g_queuePage > 0 ? secondary : tertiary, StringAlignmentCenter);
			label(graphics, L"Next page", smallFont, RectF(float(g_queueNextPage.left), pageY, 110, 34), g_queuePage + 1 < pageCount ? secondary : tertiary, StringAlignmentCenter);
			if (!externalActive()) {
				roundedPanel(graphics, RectF(float(g_queueShuffle.left), pageY, 158, 34), 7, accentSoft);
				roundedPanel(graphics, RectF(float(g_queuePlayFromBeginning.left), pageY, 158, 34), 7, raised);
				label(graphics, g_hub.fallbackShuffled() ? L"Restore playlist order" : L"Shuffle playlist", smallFont,
					RectF(float(g_queueShuffle.left), pageY, 158, 34), accent, StringAlignmentCenter);
				label(graphics, L"Play from beginning", smallFont,
					RectF(float(g_queuePlayFromBeginning.left), pageY, 158, 34), secondary, StringAlignmentCenter);
			}
			label(graphics, L"Page " + std::to_wstring(g_queuePage + 1) + L" / " + std::to_wstring(pageCount),
				smallFont, RectF(centreX - 172, pageY - 30, 344, 24), tertiary, StringAlignmentCenter);
		}
	} else if (g_page == 2) {
		roundedPanel(graphics, RectF(contentX, 108, contentWidth, float(transportTop - 130)), 16, surface);
		label(graphics, L"YouTube fallback playlist", heading, RectF(contentX + 28, 132, contentWidth - 56, 34), primary);
		label(graphics, L"This playlist plays whenever the request queue is empty.", body,
			RectF(contentX + 28, 168, contentWidth - 56, 28), secondary);
		label(graphics, g_libraryStatus, body, RectF(contentX + 28, 294, contentWidth - 56, 48), secondary);
		const std::wstring playlistName = utf8ToWide(g_hub.fallbackLabel());
		if (!playlistName.empty()) label(graphics, L"Current: " + playlistName, bodyBold,
			RectF(contentX + 28, 350, contentWidth - 56, 28), primary);
		label(graphics, std::to_wstring(g_hub.youtubeFallback().size()) + L" YouTube tracks  |  " +
			std::to_wstring(g_hub.requests().size()) + L" requests", body,
			RectF(contentX + 28, 382, contentWidth - 56, 28), secondary);
		label(graphics, L"Local music library", heading, RectF(contentX + 28, 404, contentWidth - 56, 34), primary);
		label(graphics, std::to_wstring(g_hub.localLibrary().size()) + L" local tracks  |  Active source: " + utf8ToWide(g_hub.activeSource()),
			body, RectF(contentX + 28, 568, contentWidth - 56, 28), secondary);
	} else if (g_page == 3) {
		const float panelY = 108, panelHeight = float(transportTop - 130);
		roundedPanel(graphics, RectF(contentX, panelY, contentWidth, panelHeight), 16, surface);
		const float tabWidth = std::min(210.0f, (contentWidth - 72) / 2);
		for (int i=0;i<2;++i) { const float x=contentX+24+i*(tabWidth+12); g_overlayTabs[i]=RECT{int(x),126,int(x+tabWidth),164}; roundedPanel(graphics,RectF(x,126,tabWidth,38),8,i==g_overlaySection?accentSoft:raised); }
		label(graphics,L"Content",bodyBold,RectF(float(g_overlayTabs[0].left),126,tabWidth,38),g_overlaySection==0?accent:secondary,StringAlignmentCenter);
		label(graphics,L"Style & Canvas",bodyBold,RectF(float(g_overlayTabs[1].left),126,tabWidth,38),g_overlaySection==1?accent:secondary,StringAlignmentCenter);
		label(graphics, L"Changes save instantly and update the live OBS overlay.", body, RectF(contentX + 28, 170, contentWidth - 56, 28), secondary);
		if (g_overlaySection == 0) {
			const wchar_t *names[] = {L"Artwork",L"Track title",L"Artist",L"Album",L"Requested by",L"Progress bar"};
			const wchar_t *keys[] = {L"showArtwork",L"showTitle",L"showArtist",L"showAlbum",L"showRequester",L"showProgress"};
			for(int i=0;i<6;++i){const int column=i%2,row=i/2;const float cardWidth=(contentWidth-72)/2,x=contentX+24+column*(cardWidth+24),y=204.0f+row*50.0f;g_overlayOptions[i]=RECT{int(x),int(y),int(x+cardWidth),int(y+40)};roundedPanel(graphics,RectF(x,y,cardWidth,40),8,raised);const bool enabled=overlayBool(keys[i],i!=4);label(graphics,enabled?L"✓":L"○",bodyBold,RectF(x+10,y,28,40),enabled?accent:tertiary,StringAlignmentCenter);label(graphics,names[i],body,RectF(x+44,y,cardWidth-54,40),enabled?primary:tertiary);}
			const float optionY=362.0f;
			for(int i=6;i<10;++i){const int column=(i-6)%2,row=(i-6)/2;const float cardWidth=(contentWidth-72)/2,x=contentX+24+column*(cardWidth+24),y=optionY+row*50;g_overlayOptions[i]=RECT{int(x),int(y),int(x+cardWidth),int(y+40)};roundedPanel(graphics,RectF(x,y,cardWidth,40),8,raised);}
			const std::wstring artworkSide=overlaySetting(L"artworkPosition",L"left"),timingMode=overlaySetting(L"timingMode",L"elapsedTotal");
			label(graphics,std::wstring(L"Artwork: ")+artworkSide,body,RectF(float(g_overlayOptions[6].left+14),optionY,220,40),primary);label(graphics,std::wstring(L"Timing: ")+(timingMode==L"remaining"?L"remaining":(timingMode==L"none"?L"hidden":L"elapsed / total")),body,RectF(float(g_overlayOptions[7].left+14),optionY,250,40),primary);
			const bool artBackground=overlayBool(L"artworkBackground",false),transparent=overlayBool(L"backgroundTransparent",false);
			label(graphics,(artBackground?L"✓  ":L"○  ")+std::wstring(L"Blurred artwork background"),body,RectF(float(g_overlayOptions[8].left+14),optionY+50,280,40),artBackground?primary:tertiary);label(graphics,(transparent?L"✓  ":L"○  ")+std::wstring(L"Transparent background"),body,RectF(float(g_overlayOptions[9].left+14),optionY+50,260,40),transparent?primary:tertiary);
			label(graphics,L"Custom text",smallFont,RectF(contentX+28,488,160,22),tertiary);
		} else {
			const wchar_t *labels[]={L"Font",L"Title size",L"Body size",L"Background opacity",L"Background colour",L"Text colour",L"Accent colour",L"Canvas width",L"Canvas height"};
			const float cardWidth=(contentWidth-96)/3;
			for(int i=0;i<9;++i){const int column=i%3,row=i/3;const float x=contentX+24+column*(cardWidth+24),y=210.0f+row*74.0f;g_overlayStyleOptions[i]=RECT{int(x),int(y),int(x+cardWidth),int(y+64)};roundedPanel(graphics,RectF(x,y,cardWidth,64),9,raised);label(graphics,labels[i],smallFont,RectF(x+12,y+3,cardWidth-24,20),tertiary);}
			label(graphics,L"Enter any valid size or hex colour. Font choices use bundled and common Windows typefaces.",smallFont,RectF(contentX+28,444,contentWidth-56,28),tertiary);
		}
		g_overlayReset = RECT{int(contentX + contentWidth - 188), int(panelY + panelHeight - 52), int(contentX + contentWidth - 24), int(panelY + panelHeight - 18)};
		roundedPanel(graphics, RectF(float(g_overlayReset.left), float(g_overlayReset.top), 164, 34), 7, raised);
		label(graphics, L"Reset to defaults", smallFont, RectF(float(g_overlayReset.left), float(g_overlayReset.top), 164, 34), secondary, StringAlignmentCenter);
		label(graphics,L"OBS source creation remains in the dock until the player-to-OBS action bridge is verified.",smallFont,RectF(contentX+28,float(g_overlayReset.top),contentWidth-236,34),tertiary);
	} else if (g_page != 0) {
		roundedPanel(graphics, RectF(contentX, 108, contentWidth, float(transportTop - 130)), 16, surface);
		label(graphics, pages[g_page], heading, RectF(contentX + 28, 132, contentWidth - 56, 34), primary);
		label(graphics, L"This workspace is ready for its feature migration.", body,
			RectF(contentX + 28, 170, contentWidth - 56, 28), secondary);
	}

	roundedPanel(graphics, RectF(float(sidebar + 16), float(transportTop), float(width - sidebar - 32), 88), 14, raised);
	const bool youtubeActive = g_youtubePlayer && g_youtubePlayer->active();
	const std::wstring transportTitle = youtubeActive ? g_youtubePlayer->title() :
		(g_player ? g_player->title() : L"No track playing");
	const float reportedPlaybackPosition = youtubeActive ? g_youtubePlayer->position() : currentPosition();
	const float playbackDuration = youtubeActive ? g_youtubePlayer->duration() : currentDuration();
	const int64_t reportedPlaybackPositionMs = int64_t(reportedPlaybackPosition * 1000.0f);
	if (g_transportSeekPending &&
	    (std::abs(reportedPlaybackPositionMs - g_transportSeekTargetMs) <= 1500 ||
	     GetTickCount64() - g_transportSeekPendingSince >= 5000))
		g_transportSeekPending = false;
	const float playbackPosition = (g_transportSeeking || g_transportSeekPending) ?
		float(g_transportSeekTargetMs) / 1000.0f : reportedPlaybackPosition;
	const bool playbackPlaying = youtubeActive ? g_youtubePlayer->playing() : currentPlaying();
	const int controlX = width / 2 - 129;
	// Keep the title in its own responsive column. A fixed 260px title box
	// crossed underneath Previous and Restart in a restored/smaller window.
	const float titleX = float(sidebar + 34);
	const float titleWidth = std::max(0.0f, float(controlX) - titleX - 12.0f);
	if (titleWidth >= 40.0f)
		label(graphics, transportTitle, bodyBold,
			RectF(titleX, float(transportTop + 12), titleWidth, 28), primary);
	const int buttonX[] = {controlX, controlX + 52, controlX + 104, controlX + 166, controlX + 218};
	const ULONGLONG paintTick = GetTickCount64();
	if (g_transportPressed >= 0 && g_transportPressedUntil != 0 && paintTick >= g_transportPressedUntil) {
		g_transportPressed = -1;
		g_transportPressedUntil = 0;
	}
	for (int i = 0; i < 5; ++i) {
		const int size = i == 2 ? 50 : 42, y = transportTop + (i == 2 ? 8 : 12);
		g_transportButtons[i] = RECT{buttonX[i], y, buttonX[i] + size, y + size};
		const bool pressed = g_transportPressed == i;
		if (i == 2)
			roundedPanel(graphics, RectF(float(buttonX[i]), float(y), float(size), float(size)), 25,
				pressed ? Color(255, 84, 226, 255) : accent);
		else if (pressed)
			roundedPanel(graphics, RectF(float(buttonX[i]), float(y), float(size), float(size)), 21, accentSoft);
		drawTransportIcon(graphics, i, g_transportButtons[i], playbackPlaying,
			(i == 2 || pressed) ? Color(255,255,255,255) : secondary);
	}
	const float barX = float(sidebar + 34), barY = float(transportTop + 69), barWidth = float(width - sidebar - 68);
	g_transportProgress = RECT{LONG(barX), LONG(barY - 8), LONG(barX + barWidth), LONG(barY + 12)};
	SolidBrush track(border), progressBrush(accent);
	graphics.FillRectangle(&track, barX, barY, barWidth, 4.0f);
	const float progress = playbackDuration > 0 ? std::min(1.0f, playbackPosition / playbackDuration) : 0;
	graphics.FillRectangle(&progressBrush, barX, barY, barWidth * progress, 4.0f);
	const float handleX = barX + barWidth * progress;
	graphics.FillEllipse(&progressBrush, handleX - 6.0f, barY - 4.0f, 12.0f, 12.0f);
	const std::wstring timing = clockText(playbackPosition) + L" / " + clockText(playbackDuration);
	label(graphics, timing, smallFont, RectF(float(width - 150), float(transportTop + 14), 110, 24), tertiary, StringAlignmentFar);

	graphics.Flush();
	SetViewportOrgEx(bufferDc, 0, 0, nullptr);
	int destinationState = 0;
	if (g_page == 0 && g_youtubePlayer && g_youtubePlayer->active()) {
		// A CEF video-frame invalidation can be coalesced with the regularly
		// refreshed transport region. Do not present the shell background over
		// the existing video frame before paintTo() presents its replacement.
		const RECT video = youtubeVideoBounds(window);
		destinationState = SaveDC(dc);
		ExcludeClipRect(dc, video.left, video.top, video.right, video.bottom);
	}
	BitBlt(dc, paint.rcPaint.left, paint.rcPaint.top, dirtyWidth, dirtyHeight, bufferDc, 0, 0, SRCCOPY);
	if (destinationState)
		RestoreDC(dc, destinationState);
	SelectObject(bufferDc, previousBitmap); DeleteObject(bufferBitmap); DeleteDC(bufferDc);
	if (g_page == 0 && g_youtubePlayer && g_youtubePlayer->active()) g_youtubePlayer->paintTo(dc);
	EndPaint(window, &paint);
	return 0;
}

static void send(HANDLE pipe, const std::string &message) { if (pipe != INVALID_HANDLE_VALUE && !message.empty()) { DWORD written = 0; WriteFile(pipe, message.data(), DWORD(message.size()), &written, nullptr); } }

static LRESULT CALLBACK splashWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (message == WM_ERASEBKGND) return 1;
	if (message == WM_PAINT) {
		PAINTSTRUCT paint{}; HDC dc = BeginPaint(window, &paint); RECT client{}; GetClientRect(window, &client);
		Graphics graphics(dc); graphics.Clear(Color(255, 11, 15, 20));
		if (g_splashImage && g_splashImage->GetLastStatus() == Ok)
			graphics.DrawImage(g_splashImage.get(), 0, 0, client.right, client.bottom);
		EndPaint(window, &paint); return 0;
	}
	return DefWindowProcW(window, message, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
	lifecycleLog("process-start");
	// CEF still resolves a small number of packaged runtime files relative to
	// the process working directory. Explorer, OBS and autostart can each give
	// us a different directory, so normalise it before *any* CEF process runs.
	std::wstring executableDirectory = executableAssetPath(L"");
	if (!executableDirectory.empty() &&
		(executableDirectory.back() == L'\\' || executableDirectory.back() == L'/'))
		executableDirectory.pop_back();
	if (!executableDirectory.empty()) SetCurrentDirectoryW(executableDirectory.c_str());

	PROCESS_POWER_THROTTLING_STATE powerState{};
	powerState.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
	powerState.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED |
		PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
	powerState.StateMask = 0;
	SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &powerState, sizeof(powerState));
	CefMainArgs cefMainArgs(instance);
	CefRefPtr<SuiteCefApp> cefApp = new SuiteCefApp();
	// The packaged player uses the same executable for CEF child processes.
	// Keep this identical to the proven PCM player bootstrap: enabling CEF's
	// sandbox here caused child processes launched outside OBS to re-enter as a
	// second browser instance and trip a libcef process-singleton assertion.
	const int subprocessExit = CefExecuteProcess(cefMainArgs, cefApp, nullptr);
	if (subprocessExit >= 0) return subprocessExit;
	SetUnhandledExceptionFilter(traceUnhandledException);
	traceLog("process-start", wcsstr(GetCommandLineW(), L"--watchdog") ? "watchdog=1" : "watchdog=0");
	EnumDisplayMonitors(nullptr, nullptr, traceMonitor, 0);
	traceProcessMemory();

	// Keep the browser process and every Chromium child in one Windows job.
	// CEF normally tears its renderer/GPU/utility processes down during
	// CefShutdown, but a renderer that has lost its parent IPC connection can
	// otherwise survive as an invisible, CPU-consuming process and keep the
	// installation directory locked. JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE makes
	// the operating system the final authority: when the Hub process exits and
	// its job handle is closed, every remaining descendant is reaped as well.
	// The handle deliberately remains open for the lifetime of the process.
	HANDLE processJob = CreateJobObjectW(nullptr, nullptr);
	if (processJob) {
		JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobLimits{};
		jobLimits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
		if (!SetInformationJobObject(processJob, JobObjectExtendedLimitInformation,
			&jobLimits, sizeof(jobLimits)) ||
			!AssignProcessToJobObject(processJob, GetCurrentProcess())) {
			lifecycleLog("process-job-unavailable");
			CloseHandle(processJob);
			processJob = nullptr;
		} else {
			lifecycleLog("process-job-active");
		}
	}

	// OBS autostart and a manual launch can arrive almost simultaneously. A
	// second browser process would own a separate in-memory account state while
	// still displaying persisted queue data from the first player.
	HANDLE singleInstance = CreateMutexW(nullptr, TRUE, L"Local\\RearSilverStreamSuiteMediaPlayer");
	if (!singleInstance) return 4;
	if (GetLastError() == ERROR_ALREADY_EXISTS) {
		const bool watchdogLaunch = wcsstr(GetCommandLineW(), L"--watchdog") != nullptr;
		lifecycleLog(watchdogLaunch ? "duplicate-watchdog-exit" : "duplicate-user-activate");
		for (int attempt = 0; attempt < 30; ++attempt) {
			if (HWND existing = FindWindowW(L"RearSilverMusicPlayerWindow", nullptr)) {
				if (!watchdogLaunch) {
					ShowWindow(existing, SW_RESTORE);
					SetForegroundWindow(existing);
				}
				break;
			}
			Sleep(100);
		}
		CloseHandle(singleInstance);
		return 0;
	}
	const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	{
		std::ostringstream detail; detail << "hr=0x" << std::hex << static_cast<unsigned long>(comResult);
		traceLog("com-initialise", detail.str());
	}
	GdiplusStartupInput gdiplusInput; ULONG_PTR gdiplusToken = 0; GdiplusStartup(&gdiplusToken, &gdiplusInput, nullptr);
	const std::wstring soraPath = executableAssetPath(L"Sora-Variable.ttf");
	AddFontResourceExW(soraPath.c_str(), FR_PRIVATE, nullptr);
	g_brandIconImage.reset(Image::FromFile(executableAssetPath(L"suite-app-icon.png").c_str()));
	g_brandHeaderImage.reset(Image::FromFile(executableAssetPath(L"suite-header.png").c_str()));
	g_splashImage.reset(Image::FromFile(executableAssetPath(L"suite-splash.png").c_str()));
	WNDCLASSW splashClass{}; splashClass.lpfnWndProc = splashWindowProc; splashClass.hInstance = instance;
	splashClass.hCursor = LoadCursor(nullptr, IDC_ARROW); splashClass.lpszClassName = L"RearSilverSuiteSplashWindow";
	RegisterClassW(&splashClass);
	const int splashSize = 520, screenWidth = GetSystemMetrics(SM_CXSCREEN), screenHeight = GetSystemMetrics(SM_CYSCREEN);
	HWND splash = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, splashClass.lpszClassName, L"RearSilver Stream Suite",
		WS_POPUP, (screenWidth - splashSize) / 2, (screenHeight - splashSize) / 2, splashSize, splashSize,
		nullptr, nullptr, instance, nullptr);
	const ULONGLONG splashShownAt = GetTickCount64();
	if (splash) { ShowWindow(splash, SW_SHOW); UpdateWindow(splash); }
	CefSettings cefSettings;
	cefSettings.no_sandbox = true;
	cefSettings.multi_threaded_message_loop = true;
	cefSettings.windowless_rendering_enabled = true;
	cefSettings.log_severity = LOGSEVERITY_WARNING;
	CefString(&cefSettings.locale) = "en-GB";
	if (!executableDirectory.empty()) {
		CefString(&cefSettings.browser_subprocess_path) = executableDirectory + L"\\RearSilver-Stream-Suite-Control-Hub.exe";
		CefString(&cefSettings.resources_dir_path) = executableDirectory;
		CefString(&cefSettings.locales_dir_path) = executableDirectory + L"\\locales";
	}
	wchar_t localAppData[MAX_PATH]{};
	if (GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH) > 0) {
		const std::wstring suiteData = std::wstring(localAppData) + L"\\RearSilver Stream Suite";
		CreateDirectoryW(suiteData.c_str(), nullptr);
		const std::wstring cefData = suiteData + L"\\CEF";
		CreateDirectoryW(cefData.c_str(), nullptr);
		CefString(&cefSettings.root_cache_path) = cefData;
		CefString(&cefSettings.cache_path) = cefData;
		// CEF defaults to writing debug.log beside the executable. Installed
		// builds live under Program Files, where a normal user cannot create the
		// file; Chromium then terminates on LOG_TO_FILE with an empty path.
		CefString(&cefSettings.log_file) = cefData + L"\\cef.log";
	}
	traceLog("cef-initialise-begin");
	if (!CefInitialize(cefMainArgs, cefSettings, cefApp, nullptr)) {
		traceLog("cef-initialise-failed");
		if (splash) DestroyWindow(splash); RemoveFontResourceExW(soraPath.c_str(), FR_PRIVATE, nullptr);
		GdiplusShutdown(gdiplusToken); CoUninitialize(); return 1;
	}
	traceLog("cef-initialise-complete");
	auto playerOwner = std::make_unique<Player>();
	Player &player = *playerOwner;
	g_player = &player;
	if (!player.initialise()) return 2;
	HICON appIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(101), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
	WNDCLASSW wc{}; wc.style = CS_DBLCLKS; wc.lpfnWndProc = windowProc; wc.hInstance = instance; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hIcon = appIcon; wc.lpszClassName = L"RearSilverMusicPlayerWindow"; RegisterClassW(&wc);
	const std::wstring windowTitle = std::wstring(L"RearSilver Stream Suite | Control Hub — ") + utf8ToWide(RsBeta::kChannel);
	HWND window = CreateWindowExW(0, wc.lpszClassName, windowTitle.c_str(), WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
		CW_USEDEFAULT, CW_USEDEFAULT, 1120, 720, nullptr, nullptr, instance, nullptr);
	SendMessageW(window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(appIcon));
	SendMessageW(window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(appIcon));
	ShowWindow(window, SW_SHOW); UpdateWindow(window);
	lifecycleLog("window-shown");
	if (splash) {
		while (GetTickCount64() - splashShownAt < 1200) {
			MSG splashMessage{};
			while (PeekMessageW(&splashMessage, nullptr, 0, 0, PM_REMOVE)) {
				TranslateMessage(&splashMessage);
				DispatchMessageW(&splashMessage);
			}
			Sleep(10);
		}
		DestroyWindow(splash);
		splash = nullptr;
	}
	g_playlistEdit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL | WS_BORDER,
		0, 0, 0, 0, window, nullptr, instance, nullptr);
	g_importPlaylistButton = CreateWindowExW(0, L"BUTTON", L"Import fallback playlist",
		WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_IMPORT_PLAYLIST)), instance, nullptr);
	g_addFilesButton = CreateWindowExW(0, L"BUTTON", L"Add local files", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, window,
		reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_ADD_LOCAL_FILES)), instance, nullptr);
	g_addFolderButton = CreateWindowExW(0, L"BUTTON", L"Add local folder", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, window,
		reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_ADD_LOCAL_FOLDER)), instance, nullptr);
	g_useLocalButton = CreateWindowExW(0, L"BUTTON", L"Use local files", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, window,
		reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_USE_LOCAL)), instance, nullptr);
	g_useYouTubeButton = CreateWindowExW(0, L"BUTTON", L"Use YouTube", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, window,
		reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_USE_YOUTUBE)), instance, nullptr);
	g_clearLocalButton = CreateWindowExW(0, L"BUTTON", L"Clear local library", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, window,
		reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_CLEAR_LOCAL)), instance, nullptr);
	g_overlayCustomTextEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", overlaySetting(L"customText", L"").c_str(),
		WS_CHILD | ES_AUTOHSCROLL, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_OVERLAY_CUSTOM_TEXT)), instance, nullptr);
	g_overlayFontCombo = CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|CBS_DROPDOWNLIST|WS_VSCROLL,0,0,0,0,window,
		reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_OVERLAY_FONT)),instance,nullptr);
	const wchar_t *fonts[]={L"Sora",L"Arial",L"Segoe UI",L"Calibri",L"Verdana",L"Tahoma",L"Trebuchet MS",L"Georgia",L"Times New Roman",L"Impact",L"Comic Sans MS"};
	for(const wchar_t *font:fonts) SendMessageW(g_overlayFontCombo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(font));
	SendMessageW(g_overlayFontCombo,CB_SELECTSTRING,WPARAM(-1),reinterpret_cast<LPARAM>(overlaySetting(L"fontFamily",L"Sora").c_str()));
	auto createOverlayEdit=[&](int id,const std::wstring &value,bool numeric){return CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",value.c_str(),WS_CHILD|ES_AUTOHSCROLL|(numeric?ES_NUMBER:0),0,0,0,0,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),instance,nullptr);};
	g_overlayStyleEdits[0]=createOverlayEdit(ID_OVERLAY_TITLE_SIZE,std::to_wstring(overlayNumber(L"titleSize",34)),true);
	g_overlayStyleEdits[1]=createOverlayEdit(ID_OVERLAY_BODY_SIZE,std::to_wstring(overlayNumber(L"bodySize",20)),true);
	g_overlayStyleEdits[2]=createOverlayEdit(ID_OVERLAY_OPACITY,std::to_wstring(overlayNumber(L"backgroundOpacity",82)),true);
	g_overlayStyleEdits[3]=createOverlayEdit(ID_OVERLAY_BACKGROUND_COLOUR,overlaySetting(L"backgroundColour",L"#0b0f14"),false);
	g_overlayStyleEdits[4]=createOverlayEdit(ID_OVERLAY_TEXT_COLOUR,overlaySetting(L"textColour",L"#ffffff"),false);
	g_overlayStyleEdits[5]=createOverlayEdit(ID_OVERLAY_ACCENT_COLOUR,overlaySetting(L"accentColour",L"#00d4ff"),false);
	g_overlayStyleEdits[6]=createOverlayEdit(ID_OVERLAY_WIDTH,std::to_wstring(overlayNumber(L"width",800)),true);
	g_overlayStyleEdits[7]=createOverlayEdit(ID_OVERLAY_HEIGHT,std::to_wstring(overlayNumber(L"height",240)),true);
	g_controlBackgroundBrush = CreateSolidBrush(RGB(17, 24, 33));
	g_controlFont = CreateFontW(-18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Sora");
	HFONT uiFont = g_controlFont ? g_controlFont : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
	SendMessageW(g_playlistEdit, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont), TRUE);
	SendMessageW(g_overlayCustomTextEdit, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont), TRUE);
	SendMessageW(g_overlayFontCombo,WM_SETFONT,reinterpret_cast<WPARAM>(uiFont),TRUE);
	for(HWND edit:g_overlayStyleEdits) SendMessageW(edit,WM_SETFONT,reinterpret_cast<WPARAM>(uiFont),TRUE);
	for (HWND control : {g_importPlaylistButton, g_addFilesButton, g_addFolderButton, g_useLocalButton, g_useYouTubeButton, g_clearLocalButton})
		SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont), TRUE);
	loadHubState();
	const bool betaExpired = RsBeta::currentState().expired;
	if (g_hub.activeSource() == "local") disableSongRequestsForLocalSource();
	g_hub.setNonRequestLabel(wideToUtf8(musicSetting(L"nonRequestLabel", L"Stream DJ")));
	g_authSender = wideToUtf8(musicSetting(L"authSender", L"streamer"));
	if (!betaExpired) {
		g_systemMedia.start("spotify.exe");
		g_spotify.start();
		g_streamerTwitch.start(); g_botTwitch.start();
	}
	g_overlayDesigner = std::make_unique<OverlayDesignerSurface>();
	g_overlayDesigner->initialise(window);
	positionLibraryControls(window);
	positionOverlayControls(window);
	g_youtubePlayer = new CefYouTubePlayer();
	// The YouTube CEF browser is created lazily by CefYouTubePlayer::load().
	// Spotify and Local operation therefore keep no idle OSR browser alive.
	g_youtubePlayer->setParent(window);
	// Restoring the provider does not pass through the Library button handler.
	// Resume the persisted source explicitly so both YouTube and Local sessions
	// are immediately usable without asking the user to reselect their library.
	if (!betaExpired && g_hub.activeSource() == "youtube") {
		traceLog("youtube-restored-provider-init", g_hub.hasCurrent() ? "current=1" : "current=0");
		if (g_hub.hasCurrent() && g_hub.current().provider != "local")
			startHubTrack(g_hub.current(), false);
		else
			playHubNext();
	} else if (!betaExpired && g_hub.activeSource() == "local") {
		traceLog("local-restored-provider-init", g_hub.hasCurrent() ? "current=1" : "current=0");
		const bool restored = g_hub.hasCurrent() && g_hub.current().provider == "local" &&
			startHubTrack(g_hub.current(), false);
		if (!restored)
			playHubNext();
	}
	if (!betaExpired) updateHubMediaKeyRegistration(window);
	// OBS launches the Hub; the Hub is the sole owner of its optional apps.
	if (!betaExpired) launchManagedPrograms();
	HANDLE pipe = CreateNamedPipeW(L"\\\\.\\pipe\\RearSilverStreamSuiteMusicPlayer", PIPE_ACCESS_DUPLEX,
		PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_NOWAIT, 1, 1024 * 1024, 1024 * 1024, 0, nullptr);
	if (pipe == INVALID_HANDLE_VALUE) return 3;
	bool connected = false, running = true, lastReaderReady = false, chatAnnouncementPending = false; std::string input, lastSpotifyQueueSignature,lastChatSender,lastChatStreamerSignature,lastChatBotSignature; ULONGLONG lastStatus = 0, lastHubStatus = 0, lastYouTubePoll = 0, lastExternalPoll = 0, lastSpotifyUiRefresh = 0, lastTwitchValidation=GetTickCount64(), lastTraceHeartbeat = 0; uint64_t lastStreamerRevision=0,lastBotRevision=0,lastPublishedReaderRevision=0,lastPublishedSenderRevision=0;
	while (running) {
		MSG message{}; while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
		if (g_closeRequested) { running = false; continue; }
		if (!betaExpired && g_youtubePlayer->active() && GetTickCount64() - lastYouTubePoll >= 250) {
			g_youtubePlayer->pollStatus(); lastYouTubePoll = GetTickCount64();
		}
		if (!betaExpired && externalActive() && GetTickCount64() - lastExternalPoll >= 500) {
			const SpotifyClientState spotify=g_spotify.state();const SystemMediaState liveMedia=g_systemMedia.state();bool cancelledCurrent=false;
			for(const auto &request:g_hub.requests())if(request.provider=="spotify"&&request.cancelled&&(request.providerId==spotify.current.uri||(!liveMedia.title.empty()&&request.title==liveMedia.title&&request.artist==liveMedia.artist))){commandExternalPlayer(SystemMediaProvider::Action::Next);g_hub.removeRequest(request.id);saveHubState();syncHubQueueView();cancelledCurrent=true;break;}
			if(!cancelledCurrent)refreshExternalPlayer(window);
			bool requestStarted=false;if(!spotify.current.uri.empty())for(const auto &request:g_hub.requests())if(request.provider=="spotify"&&request.providerId==spotify.current.uri){requestStarted=g_hub.removeRequest(request.id)||requestStarted;}if(requestStarted)saveHubState();
			// Spotify owns its real playback queue. Reconcile our persistent request ledger
			// against a successfully checked live snapshot so old requests cannot cause false
			// duplicate rejections after Spotify has played, removed, or discarded them.
			if(spotify.connected&&spotify.queueChecked&&!spotify.current.uri.empty()){
				std::unordered_set<std::string> liveUris;liveUris.insert(spotify.current.uri);
				for(const auto &track:spotify.queue)if(!track.uri.empty())liveUris.insert(track.uri);
				const ULONGLONG now=GetTickCount64();bool ledgerChanged=false;
				for(const auto &request:g_hub.requests()){
					if(request.provider!="spotify"||request.providerId.empty())continue;
					bool protectedByGrace=false;
					{
						std::lock_guard<std::mutex> lock(g_recentSpotifyRequestMutex);
						auto recent=g_recentSpotifyRequests.find(request.providerId);
						if(liveUris.count(request.providerId)){
							if(recent!=g_recentSpotifyRequests.end())g_recentSpotifyRequests.erase(recent);
						}else if(recent!=g_recentSpotifyRequests.end()){
							protectedByGrace=now-recent->second<kSpotifyRequestPropagationGraceMs;
							if(!protectedByGrace)g_recentSpotifyRequests.erase(recent);
						}
					}
					if(!liveUris.count(request.providerId)&&!protectedByGrace)ledgerChanged=g_hub.removeRequest(request.id)||ledgerChanged;
				}
				if(ledgerChanged){saveHubState();syncHubQueueView();}
			}
			std::string signature=spotify.current.uri+'|'+std::to_string(spotify.queue.size());for(const auto&track:spotify.queue)signature+='|'+track.uri;if(signature!=lastSpotifyQueueSignature){lastSpotifyQueueSignature=std::move(signature);syncHubQueueView();if(g_page==1)InvalidateRect(window,nullptr,FALSE);}lastExternalPoll = GetTickCount64();
		}
		// Keep every WebView-backed page self-healing. This also retries the
		// page-ready handshake if a document's first message was lost in a rapid
		// Library/Accounts navigation or source switch.
		if(((g_page>=2&&g_page<=6)||g_page==8)&&g_overlayDesigner&&GetTickCount64()-lastSpotifyUiRefresh>=250){g_overlayDesigner->refresh();lastSpotifyUiRefresh=GetTickCount64();}
		if(!betaExpired&&GetTickCount64()-lastTwitchValidation>=3600000){g_streamerTwitch.reconnect(false);g_botTwitch.reconnect(false);lastTwitchValidation=GetTickCount64();}
		auto publishTwitch=[&](const char*name,TwitchAccount &auth,TwitchChatService &chat,bool chatRequired,uint64_t &seenAuth,uint64_t &seenChat){const uint64_t authRevision=auth.revision(),chatRevision=chat.revision();if(!connected||(authRevision==seenAuth&&chatRevision==seenChat))return;seenAuth=authRevision;seenChat=chatRevision;const auto state=auth.state();const std::string shownName=state.displayName.empty()?state.login:state.displayName;const std::string transportState=!state.connected?"disconnected":chatRequired&&!chat.connected()?"chat-reconnecting":"connected";std::lock_guard<std::mutex>lock(g_hostEventMutex);g_hostEvents.push_back(std::string("HOST\tACCOUNT_STATE\t")+name+'\t'+transportState+'\t'+shownName+"\n");};
		auto syncChat=[&]{
			const auto streamer=g_streamerTwitch.state();const auto bot=g_botTwitch.state();const std::string streamerToken=g_streamerTwitch.accessToken(),botToken=g_botTwitch.accessToken();const std::string streamerSignature=streamer.connected?streamer.login+'\n'+streamer.userId+'\n'+streamerToken:"disconnected";const std::string botSignature=bot.connected?bot.login+'\n'+bot.userId+'\n'+botToken:"disconnected";
			const bool streamerChanged=streamerSignature!=lastChatStreamerSignature;
			if(streamerChanged){lastChatStreamerSignature=streamerSignature;g_twitchReader.disconnect();if(streamer.connected){const std::string channel=streamer.login;g_twitchReader.connect(streamer.login,streamerToken,channel,streamer.userId,[window](const TwitchChatMessage&m){auto*copy=new TwitchChatMessage(m);if(!PostMessageW(window,WM_TWITCH_CHAT,0,reinterpret_cast<LPARAM>(copy)))delete copy;});}}
			if(botSignature!=lastChatBotSignature||streamerChanged||lastChatSender!=g_authSender){lastChatBotSignature=botSignature;lastChatSender=g_authSender;g_twitchSender.disconnect();if(streamer.connected&&bot.connected&&g_authSender=="bot")g_twitchSender.connect(bot.login,botToken,streamer.login,streamer.userId);}
		};
		if(!betaExpired){syncChat();
		g_twitchReader.tick();g_twitchSender.tick();const bool readerReady=g_twitchReader.connected();if(readerReady&&!lastReaderReady)chatAnnouncementPending=true;lastReaderReady=readerReady;if(chatAnnouncementPending&&sendTwitchMessage("RearSilver Stream Suite Hub Connected ⚡"))chatAnnouncementPending=false;}
		if (!connected) { connected = ConnectNamedPipe(pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED; g_hostPipeConnected=connected; if (connected) { g_obsStudioVersion.clear(); g_pluginVersion.clear(); PostMessageW(window,WM_OBS_CONNECTION_CHANGED,TRUE,0); send(pipe, player.status()); queueOverlayPlacementMode(); queueReplayConfiguration(); lastStreamerRevision=lastBotRevision=0; } }
		if(!betaExpired){publishTwitch("streamer",g_streamerTwitch,g_twitchReader,true,lastStreamerRevision,lastPublishedReaderRevision);publishTwitch("bot",g_botTwitch,g_twitchSender,g_authSender=="bot",lastBotRevision,lastPublishedSenderRevision);}
		if (connected) {
			{ std::lock_guard<std::mutex> lock(g_hostEventMutex); for(const auto &event:g_hostEvents)send(pipe,event); g_hostEvents.clear(); }
			char buffer[4096]; DWORD read = 0;
			if (ReadFile(pipe, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
				input.append(buffer, read); size_t newline = 0;
				while ((newline = input.find('\n')) != std::string::npos) {
					std::string line = input.substr(0, newline); input.erase(0, newline + 1); if (!line.empty() && line.back() == '\r') line.pop_back();
					if (line == "SHUTDOWN") { running = false; break; }
					if (line.rfind("HOST_INFO\t", 0) == 0) {
						const size_t tab=line.find('\t',10);if(tab!=std::string::npos){g_obsStudioVersion=line.substr(10,tab-10);g_pluginVersion=line.substr(tab+1);if(g_overlayDesigner)g_overlayDesigner->refresh();}
					} else if (betaExpired) {
						continue;
					} else if (line.rfind("SETUP_STATE\t", 0) == 0) {
						const size_t tab=line.find('\t',12);if(tab!=std::string::npos){g_captureExists=line.substr(12,tab-12)=="true";g_playerAutoStart=line.substr(tab+1)=="true";if(g_overlayDesigner)g_overlayDesigner->refresh();}
					} else if (line.rfind("REPLAY_STATE\t", 0) == 0) {
						CefRefPtr<CefValue>state=CefParseJSON(line.substr(13),JSON_PARSER_RFC);if(state&&state->GetType()==VTYPE_DICTIONARY){auto dictionary=state->GetDictionary();g_replayBufferActive=dictionary->GetBool("bufferActive");g_replaySceneExists=dictionary->GetBool("sceneExists");g_replayPlaced=dictionary->GetBool("placedInCurrentScene");g_replayVisible=dictionary->GetBool("visible");g_replayPlaying=dictionary->GetBool("playing");g_replayConflict=dictionary->GetBool("conflict");g_replaySetupComplete=dictionary->GetBool("setupComplete");g_replayMessage=dictionary->GetString("message").ToString();if(dictionary->HasKey("geometry")){auto geometry=dictionary->GetDictionary("geometry");if(geometry){g_replayPreviewGeometry.width=geometry->GetInt("width");g_replayPreviewGeometry.height=geometry->GetInt("height");g_replayPreviewGeometry.scalePercent=geometry->GetInt("scalePercent");g_replayPreviewGeometry.titlePixelSize=geometry->GetInt("titlePixelSize");g_replayPreviewGeometry.border=geometry->GetInt("border");g_replayPreviewGeometry.outerRadius=geometry->GetInt("outerRadius");g_replayPreviewGeometry.innerRadius=geometry->GetInt("innerRadius");g_replayPreviewGeometry.apertureX=geometry->GetInt("apertureX");g_replayPreviewGeometry.apertureY=geometry->GetInt("apertureY");g_replayPreviewGeometry.apertureWidth=geometry->GetInt("apertureWidth");g_replayPreviewGeometry.apertureHeight=geometry->GetInt("apertureHeight");g_replayPreviewGeometry.titleX=geometry->GetInt("titleX");g_replayPreviewGeometry.titleY=geometry->GetInt("titleY");g_replayPreviewGeometry.titleWidth=geometry->GetInt("titleWidth");g_replayPreviewGeometry.titleHeight=geometry->GetInt("titleHeight");g_replayPreviewGeometry.available=g_replayPreviewGeometry.width>0&&g_replayPreviewGeometry.height>0;}}if(g_overlayDesigner)g_overlayDesigner->refresh();}
					} else if (line.rfind("QUICK_TEXT_STATE\t", 0) == 0) {
						CefRefPtr<CefValue>state=CefParseJSON(line.substr(17),JSON_PARSER_RFC);if(state&&state->GetType()==VTYPE_DICTIONARY){auto dictionary=state->GetDictionary();g_quickTextSourceExists=dictionary->GetBool("sourceExists");g_quickTextPlaced=dictionary->GetBool("placedInCurrentScene");g_quickTextVisible=dictionary->GetBool("visibleInCurrentScene");g_quickTextConflict=dictionary->GetBool("conflict");g_quickTextSetupComplete=dictionary->GetBool("setupComplete");if(dictionary->HasKey("placementMode"))g_overlayPlacementMode=dictionary->GetString("placementMode").ToString();g_quickTextMessage=dictionary->GetString("message").ToString();if(g_overlayDesigner)g_overlayDesigner->refresh();}
					} else if (line.rfind("TIMER_STATE\t", 0) == 0) {
						CefRefPtr<CefValue>state=CefParseJSON(line.substr(12),JSON_PARSER_RFC);if(state&&state->GetType()==VTYPE_DICTIONARY){auto dictionary=state->GetDictionary();g_timerSourceExists=dictionary->GetBool("sourceExists");g_timerPlaced=dictionary->GetBool("placedInCurrentScene");g_timerVisible=dictionary->GetBool("visibleInCurrentScene");g_timerConflict=dictionary->GetBool("conflict");g_timerSetupComplete=dictionary->GetBool("setupComplete");if(dictionary->HasKey("placementMode"))g_overlayPlacementMode=dictionary->GetString("placementMode").ToString();g_timerMessage=dictionary->GetString("message").ToString();if(g_overlayDesigner)g_overlayDesigner->refresh();}
					} else if (line.rfind("MUSIC_OVERLAY_STATE\t", 0) == 0) {
						CefRefPtr<CefValue>state=CefParseJSON(line.substr(20),JSON_PARSER_RFC);if(state&&state->GetType()==VTYPE_DICTIONARY){auto dictionary=state->GetDictionary();g_musicOverlaySourceExists=dictionary->GetBool("sourceExists");g_musicOverlayPlaced=dictionary->GetBool("placedInCurrentScene");g_musicOverlayVisible=dictionary->GetBool("visibleInCurrentScene");g_musicOverlayConflict=dictionary->GetBool("conflict");g_musicOverlaySetupComplete=dictionary->GetBool("setupComplete");if(dictionary->HasKey("placementMode"))g_overlayPlacementMode=dictionary->GetString("placementMode").ToString();g_musicOverlayMessage=dictionary->GetString("message").ToString();if(g_overlayDesigner)g_overlayDesigner->refresh();}
					} else if (line.rfind("HUB_IMPORT\t", 0) == 0) {
						const std::string url = line.substr(11);
						std::thread([window, url] { auto *result = new HubPlaylistResult(resolveHubPlaylist(url));
							if (!PostMessageW(window, WM_HUB_PLAYLIST_RESULT, 0, reinterpret_cast<LPARAM>(result))) delete result; }).detach();
					} else if (line.rfind("HUB_REQUEST\t", 0) == 0) {
						std::vector<std::string> fields; size_t start = 12, tab = 0;
						while ((tab = line.find('\t', start)) != std::string::npos && fields.size() < 4) { fields.push_back(line.substr(start, tab - start)); start = tab + 1; }
						fields.push_back(line.substr(start));
						if (fields.size() < 5) continue;
						const std::string requestId = fields[0], requesterId = fields[1], requester = fields[2], query = fields[4];
						const int requesterLevel = std::atoi(fields[3].c_str());
						const bool spotifyLink=query.rfind("spotify:track:",0)==0||query.find("open.spotify.com/track/")!=std::string::npos;
						auto rejectRequest=[&](const std::string &reason){std::lock_guard<std::mutex>lock(g_hostEventMutex);g_hostEvents.push_back("HOST\tREQUEST_REJECTED\t"+requestId+'\t'+reason+"\n");};
						if(!musicBool(L"requestsEnabled",true)){rejectRequest("Song requests are turned off for this stream.");continue;}
						if(spotifyLink&&!externalActive()){rejectRequest("Spotify requests are only available when Spotify is selected in the Hub.");continue;}
						if(externalActive()&&!g_spotify.state().authorized){rejectRequest("Spotify requests require a connected Spotify Premium account in Suite Settings.");continue;}
						const bool spotifyRequest=externalActive()&&g_spotify.state().authorized;
						if(spotifyRequest){
							std::thread([window,requestId,requesterId,requester,requesterLevel,query]{auto *result=new HubSearchResult;SpotifyQueueTrack spotifyTrack;if(g_spotify.searchTrack(query,spotifyTrack)){result->track.id=requestId;result->track.provider="spotify";result->track.providerId=spotifyTrack.uri;result->track.title=spotifyTrack.title;result->track.artist=spotifyTrack.artist;result->track.album=spotifyTrack.album;result->track.artworkUrl=spotifyTrack.artworkUrl;result->track.durationSeconds=int(spotifyTrack.durationMs/1000);result->track.request=true;result->track.requestedBy=requester;result->track.requesterId=requesterId;result->track.requesterLevel=requesterLevel;}else{result->track.id=requestId;result->error="Spotify could not find that track. Try a more specific title and artist or paste a Spotify track link.";}if(!PostMessageW(window,WM_HUB_REQUEST_RESULT,0,reinterpret_cast<LPARAM>(result)))delete result;}).detach();continue;
						}
						const HubYouTubeSafetyOptions safety = youtubeSafetyOptions();
						std::thread([window, query, requesterId, requester, requesterLevel, requestId, safety] { auto *result = new HubSearchResult(resolveHubSearch(query, requester, safety));
							result->track.id = requestId;
							result->track.requesterId = requesterId; result->track.requesterLevel = requesterLevel;
							if (!PostMessageW(window, WM_HUB_REQUEST_RESULT, 0, reinterpret_cast<LPARAM>(result))) delete result; }).detach();
					} else if (line.rfind("HUB_REMOVE\t", 0) == 0) {
						std::string id = line.substr(11);
						if(!id.empty()&&id.front()=='#')id.erase(id.begin());
						if(!id.empty()&&(id.front()=='r'||id.front()=='R'))id.front()='R';
						else if(!id.empty()&&std::all_of(id.begin(),id.end(),[](unsigned char c){return std::isdigit(c)!=0;}))id='R'+id;
						HubTrack removed;
						for (const HubTrack &track : g_hub.requests()) if (track.id == id) { removed = track; break; }
						if (removed.provider == "spotify") {
							g_hub.cancelRequest(id); saveHubState(); syncHubQueueView();
							std::lock_guard<std::mutex> lock(g_hostEventMutex);
							g_hostEvents.push_back("HOST\tREQUEST_REMOVED\t" + id + "\t" + removed.title + "\t" + removed.artist + "\n");
							continue;
						}
						if (removed.id.empty() && g_hub.hasCurrent() && g_hub.current().request && g_hub.current().id == id)
							removed = g_hub.current();
						if (!removed.id.empty() && g_hub.removeRequest(id)) {
							syncHubQueueView(); saveHubState();
							std::lock_guard<std::mutex> lock(g_hostEventMutex);
							g_hostEvents.push_back("HOST\tREQUEST_REMOVED\t" + id + "\t" + removed.title + "\t" + removed.artist + "\n");
						} else if (!removed.id.empty() && g_hub.hasCurrent() && g_hub.current().id == id) {
							playHubNext();
							std::lock_guard<std::mutex> lock(g_hostEventMutex);
							g_hostEvents.push_back("HOST\tREQUEST_REMOVED\t" + id + "\t" + removed.title + "\t" + removed.artist + "\n");
						} else {
							std::lock_guard<std::mutex> lock(g_hostEventMutex);
							g_hostEvents.push_back("HOST\tREQUEST_REMOVE_FAILED\t" + id + "\tThat request is not waiting in the queue.\n");
						}
					} else if (line == "HUB_SHUFFLE") {
						if (!externalActive()) { g_hub.shuffleFallback(); saveHubState(); g_queuePage = 0; syncHubQueueView(); InvalidateRect(window, nullptr, FALSE); }
					} else if (line == "HUB_RESTORE_ORDER") {
						if (!externalActive()) { g_hub.restoreFallbackOrder(); saveHubState(); g_queuePage = 0; syncHubQueueView(); InvalidateRect(window, nullptr, FALSE); }
					} else if (line == "HUB_PLAY_FROM_BEGINNING") {
						if (!externalActive()) { HubTrack first; if (g_hub.restartFallback(first)) startHubTrack(first); saveHubState(); g_queuePage = 0; syncHubQueueView(); InvalidateRect(window, nullptr, FALSE); }
					} else if (line.rfind("META\t", 0) == 0) {
						player.setMetadata(line.substr(5));
						InvalidateRect(window, nullptr, FALSE);
					} else {
						const size_t tab = line.find('\t');
						const std::string action = line.substr(0, tab);
						const std::string argument = tab == std::string::npos ? std::string{} : line.substr(tab + 1);
						if (externalActive() && (action == "PLAY" || action == "PAUSE" || action == "SKIP" ||
							action == "PREVIOUS" || action == "RESTART" || action == "STOP" || action == "SEEK")) {
							if (action == "PLAY") commandExternalPlayer(SystemMediaProvider::Action::Play);
							else if (action == "PAUSE" || action == "STOP") commandExternalPlayer(SystemMediaProvider::Action::Pause);
							else if (action == "SKIP") commandExternalPlayer(SystemMediaProvider::Action::Next);
							else if (action == "PREVIOUS") commandExternalPlayer(SystemMediaProvider::Action::Previous);
							else if (action == "RESTART") commandExternalPlayer(SystemMediaProvider::Action::Restart);
							else commandExternalPlayer(SystemMediaProvider::Action::Seek, std::strtoll(argument.c_str(), nullptr, 10));
						} else if (action == "YOUTUBE") {
							player.suspend();
							g_youtubePlayer->load(argument);
						} else if (action == "LOAD") {
							g_youtubePlayer->command("STOP");
							g_youtubePlayer->hide();
							send(pipe, player.command(line));
						} else if (action == "PLAY" && !g_youtubePlayer->active() && !g_hub.hasCurrent()) {
							playHubNext(); InvalidateRect(window, nullptr, FALSE);
						} else if (action == "SKIP") {
							playHubNext(); InvalidateRect(window, nullptr, FALSE);
						} else if (action == "PREVIOUS") {
							HubTrack previous; if (g_hub.takePrevious(previous)) startHubTrack(previous, false);
							InvalidateRect(window, nullptr, FALSE);
						} else if (g_youtubePlayer->active()) {
							g_youtubePlayer->command(action, argument);
						} else {
							send(pipe, player.command(line));
						}
					}
				}
			} else {const DWORD error=GetLastError();if(error==ERROR_BROKEN_PIPE||error==ERROR_PIPE_NOT_CONNECTED){DisconnectNamedPipe(pipe);connected=false;g_hostPipeConnected=false;g_obsStudioVersion.clear();g_pluginVersion.clear();input.clear();PostMessageW(window,WM_OBS_CONNECTION_CHANGED,FALSE,0);}}
			if (connected && GetTickCount64() - lastHubStatus >= 500) {
				const std::string hubStatus = externalActive() ? wideToUtf8(currentState()) : (g_youtubePlayer->active() ?
					(g_youtubePlayer->playing() ? "playing" : "paused") : wideToUtf8(player.state()));
				const int64_t hubPosition = externalActive() ? currentExternalPositionMs() : int64_t((g_youtubePlayer->active() ? g_youtubePlayer->position() : player.position()) * 1000.0f);
				const int64_t hubDuration = externalActive() ? g_externalState.durationMs : int64_t((g_youtubePlayer->active() ? g_youtubePlayer->duration() : player.duration()) * 1000.0f);
				send(pipe, "HUB_STATE\t" + g_hub.snapshotJson(hubStatus, hubPosition, hubDuration, false) + "\n");
				lastHubStatus = GetTickCount64();
			}
		}
		if (player.takeEnded()) { if (connected) send(pipe, "EVENT\tended\t" + player.path() + "\n"); if (!betaExpired) playHubNext(); }
		for (const std::string &event : g_youtubePlayer->takeEvents()) {
			if (!betaExpired && (event.rfind("EVENT\tyoutube-ended\t", 0) == 0 || event.rfind("EVENT\tyoutube-error\t", 0) == 0)) playHubNext();
			if (connected) send(pipe, event);
		}
		if (GetTickCount64() - lastStatus >= 100) {
			const std::string status = player.status(); if (connected && !g_youtubePlayer->active() && !externalActive()) send(pipe, status);
			lastStatus = GetTickCount64(); RECT client{}; GetClientRect(window, &client);
			RECT playbackRegion{0, std::max(0L, client.bottom - 106), client.right, client.bottom};
			InvalidateRect(window, &playbackRegion, FALSE);
		}
		if (GetTickCount64() - lastTraceHeartbeat >= 5000) {
			std::ostringstream detail;
			detail << "provider=" << g_hub.activeSource()
				<< " youtube_active=" << (g_youtubePlayer && g_youtubePlayer->active() ? 1 : 0)
				<< " visible=" << (IsWindowVisible(window) ? 1 : 0)
				<< " iconic=" << (IsIconic(window) ? 1 : 0)
				<< " foreground=" << (GetForegroundWindow() == window ? 1 : 0);
			traceLog("heartbeat", detail.str());
			traceProcessMemory();
			lastTraceHeartbeat = GetTickCount64();
		}
		Sleep(10);
	}
	traceLog("shutdown-begin", "provider=" + g_hub.activeSource());
	releaseHubMediaKeys(window);
	// Managed applications close first while the Hub is still fully alive. This
	// preserves the successful ordering of the old OBS Auto-Start Manager and
	// prevents both abandoned apps and a competing GPU/compositor teardown.
	if (musicBool(L"tool.autoClose", true)) {
		traceLog("managed-apps-close-begin");
		closeManagedProgramsAndWait();
		traceLog("managed-apps-close-complete");
	}

	// Let DWM finish retiring the optional applications' surfaces before the
	// Hub begins its own Chromium teardown. OBS used to provide this separation
	// naturally because it remained alive after closing managed applications.
	tracedDwmFlush("after-managed-apps-1"); Sleep(200); tracedDwmFlush("after-managed-apps-2");
	lifecycleLog("browser-shutdown-begin");
	traceLog("network-stop-begin");
	// Keep the native Hub visible while its embedded browsers close. Hiding its
	// DirectComposition surface first can momentarily blank another display.
	g_twitchReader.disconnect();g_twitchSender.disconnect();g_streamerTwitch.stop();g_botTwitch.stop();g_spotify.stop(); g_systemMedia.stop();
	traceLog("network-stop-complete");
	if (g_overlayDesigner) { g_overlayDesigner->shutdown(); g_overlayDesigner.reset(); }
	if (g_youtubePlayer) { g_youtubePlayer->shutdown(); g_youtubePlayer = nullptr; }
	tracedDwmFlush("after-browser-close");
	traceLog("window-hide-begin");
	ShowWindow(window, SW_HIDE); UpdateWindow(window);
	traceLog("window-hide-complete");
	CloseHandle(pipe); traceLog("window-destroy-begin"); DestroyWindow(window); traceLog("window-destroy-complete"); g_player = nullptr;
	// Player owns miniaudio state and a GDI+ artwork Image. Destroy it before
	// the process tears down either subsystem instead of leaving its destructor
	// to run after GdiplusShutdown at the closing brace of wWinMain.
	playerOwner.reset();
	traceLog("native-player-destroyed");
	if (g_controlFont) { DeleteObject(g_controlFont); g_controlFont = nullptr; } if (g_controlBackgroundBrush) { DeleteObject(g_controlBackgroundBrush); g_controlBackgroundBrush = nullptr; }
	g_splashImage.reset(); g_brandHeaderImage.reset(); g_brandIconImage.reset(); RemoveFontResourceExW(soraPath.c_str(), FR_PRIVATE, nullptr);
	// CEF must be shut down on its initialising thread while COM is still alive.
	// Releasing COM first left Chromium's compositor to be dismantled by process
	// termination, which can briefly reset a second monitor's presentation path.
	// The CefApp is also reference-counted by the local browser-process owner.
	// Release that final owner before CefShutdown; allowing its destructor to run
	// after CefShutdown caused the repeatable 0xc0000005 recorded at process exit.
	cefApp = nullptr;
	traceLog("cef-app-reference-released");
	traceLog("cef-shutdown-begin");
	CefShutdown();
	traceLog("cef-shutdown-complete");
	lifecycleLog("clean-exit");
	traceProcessMemory();
	traceLog("process-exit-clean");
	GdiplusShutdown(gdiplusToken); CoUninitialize(); ReleaseMutex(singleInstance); CloseHandle(singleInstance); return 0;
}
