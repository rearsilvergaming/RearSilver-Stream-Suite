#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
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
#include "youtube_resolver.hpp"

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
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace Gdiplus;
using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

static bool g_sidebarCollapsed = false;
static int sidebarWidthFor(int clientWidth) { return (g_sidebarCollapsed || clientWidth < 820) ? 104 : 220; }
struct QueueItem { std::wstring title, artist, source; int durationSeconds = 0; bool current = false; };
static std::vector<QueueItem> g_queue;
static MusicHubModel g_hub;
static HWND g_playlistEdit = nullptr, g_importPlaylistButton = nullptr;
static std::wstring g_libraryStatus = L"Paste a YouTube or YouTube Music playlist URL to import it into the Suite Media Player.";
static constexpr UINT WM_HUB_PLAYLIST_RESULT = WM_APP + 40;
static constexpr UINT WM_HUB_REQUEST_RESULT = WM_APP + 41;
static constexpr int ID_IMPORT_PLAYLIST = 4101;
static int g_queuePage = 0;
static RECT g_queuePreviousPage{}, g_queueNextPage{}, g_queueShuffle{};

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

class WebViewYouTubePlayer {
public:
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
		environmentOptions->put_AdditionalBrowserArguments(L"--disable-features=AudioServiceOutOfProcess");

		CreateCoreWebView2EnvironmentWithOptions(
			nullptr, webViewData.c_str(), environmentOptions.Get(),
			Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
				[this, folder](HRESULT result, ICoreWebView2Environment *environment) -> HRESULT {
					if (FAILED(result) || !environment) {
						m_events.emplace_back("ERROR\tMicrosoft WebView2 Runtime is unavailable. Repair the Suite installation.\n");
						return result;
					}
					return environment->CreateCoreWebView2Controller(
						m_parent,
						Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
							[this, folder](HRESULT controllerResult, ICoreWebView2Controller *controller) -> HRESULT {
								if (FAILED(controllerResult) || !controller) return controllerResult;
								m_controller = controller;
								m_controller->get_CoreWebView2(&m_webView);
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
								m_controller->put_IsVisible(FALSE);
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
		m_controller->put_Bounds(bounds);
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
		if (m_controller) m_controller->put_IsVisible(TRUE);
		loadPending();
	}

	void hide()
	{
		m_active = false;
		if (m_controller) m_controller->put_IsVisible(FALSE);
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
		m_parent = parent;
		CefWindowInfo info; info.SetAsWindowless(parent);
		CefBrowserSettings settings; settings.windowless_frame_rate = 30;
		return CefBrowserHost::CreateBrowser(info, this, "about:blank", settings, nullptr, nullptr);
	}
	void shutdown()
	{
		if (m_browser) m_browser->GetHost()->CloseBrowser(true);
		for (int i = 0; i < 200 && m_browser; ++i) Sleep(5);
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
		if (canReusePlayer) command("LOAD_VIDEO", safe);
		else if (m_browser) loadCurrent();
		else m_pendingLoad = true;
		if (m_parent) InvalidateRect(m_parent, nullptr, FALSE);
	}
	void hide() { m_active.store(false); if (m_browser) m_browser->GetMainFrame()->LoadURL("about:blank"); if (m_parent) InvalidateRect(m_parent, nullptr, FALSE); }
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
		m_browser = browser; browser->GetHost()->WasHidden(false); browser->GetHost()->SetFocus(true);
		m_devToolsRegistration = browser->GetHost()->AddDevToolsMessageObserver(this);
		if (m_pendingLoad) { m_pendingLoad = false; loadCurrent(); }
	}
	void OnBeforeClose(CefRefPtr<CefBrowser>) override { m_devToolsRegistration = nullptr; m_browser = nullptr; }
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
static bool g_closeRequested = false;
static int g_page = 0;

static void syncHubQueueView()
{
	g_queue.clear();
	if (g_hub.hasCurrent()) {
		const HubTrack current = g_hub.current();
		g_queue.push_back({utf8ToWide(current.title), utf8ToWide(current.artist), L"Now playing", current.durationSeconds, true});
	}
	for (const HubTrack &track : g_hub.playbackOrder())
		g_queue.push_back({utf8ToWide(track.title), utf8ToWide(track.artist),
			track.request ? L"Request" : L"Fallback playlist", track.durationSeconds, false});
}

static void saveHubState();

static bool startHubTrack(const HubTrack &track, bool record = true)
{
	if (track.providerId.empty()) return false;
	if (g_player) g_player->suspend();
	if (record) g_hub.recordStarted(track);
	g_youtubePlayer->load(track.providerId);
	g_queuePage = 0; syncHubQueueView();
	return true;
}

static bool playHubNext()
{
	HubTrack track;
	if (!g_hub.takeNext(track)) { g_hub.clearCurrent(); syncHubQueueView(); return false; }
	const bool started = startHubTrack(track); saveHubState(); return started;
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
	if (output) output << g_hub.snapshotJson("stopped", 0, 0);
}

static void loadHubState()
{
	const std::wstring path = hubStatePath(); std::ifstream input(path, std::ios::binary);
	if (!input) return; const std::string json((std::istreambuf_iterator<char>(input)), {});
	CefRefPtr<CefValue> root = CefParseJSON(json, JSON_PARSER_RFC);
	if (!root || root->GetType() != VTYPE_DICTIONARY) return;
	CefRefPtr<CefDictionaryValue> object = root->GetDictionary(); CefRefPtr<CefListValue> queue = object->GetList("queue");
	std::vector<HubTrack> fallback;
	if (queue) for (size_t i = 0; i < queue->GetSize(); ++i) {
		CefRefPtr<CefDictionaryValue> value = queue->GetDictionary(i); if (!value || value->GetBool("request")) continue;
		HubTrack track; track.id = value->GetString("id").ToString(); track.providerId = value->GetString("providerId").ToString();
		track.title = value->GetString("title").ToString(); track.artist = value->GetString("artist").ToString();
		track.artworkUrl = value->GetString("artworkUrl").ToString(); track.durationSeconds = value->GetInt("durationSeconds");
		if (!track.providerId.empty()) fallback.push_back(std::move(track));
	}
	if (!fallback.empty()) {
		g_hub.replaceFallback(std::move(fallback), object->GetString("fallbackLabel").ToString(), object->GetString("fallbackUrl").ToString());
		syncHubQueueView(); g_libraryStatus = L"Restored the saved fallback playlist.";
	}
}

static void positionLibraryControls(HWND window)
{
	if (!g_playlistEdit || !g_importPlaylistButton) return;
	RECT client{}; GetClientRect(window, &client);
	const bool visible = g_page == 2;
	ShowWindow(g_playlistEdit, visible ? SW_SHOW : SW_HIDE);
	ShowWindow(g_importPlaylistButton, visible ? SW_SHOW : SW_HIDE);
	if (!visible) return;
	const int sidebar = sidebarWidthFor(client.right);
	const int x = sidebar + 56, width = std::max(240, int(client.right) - x - 56);
	MoveWindow(g_playlistEdit, x, 205, width, 34, TRUE);
	MoveWindow(g_importPlaylistButton, x, 249, std::min(260, width), 36, TRUE);
}

static std::wstring clockText(float seconds)
{
	const int total = std::max(0, int(seconds));
	wchar_t text[32]; swprintf_s(text, L"%d:%02d", total / 60, total % 60); return text;
}

static RECT g_transportButtons[5]{};
static RECT g_sidebarToggle{};

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

static LRESULT CALLBACK legacyWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (message == WM_CLOSE) { g_closeRequested = true; return 0; }
	if (message == WM_SIZE) {
		if (g_youtubePlayer) g_youtubePlayer->resize();
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
		SolidBrush white(Color(255, 245, 245, 248)), muted(Color(255, 175, 175, 188)), accent(Color(255, 143, 61, 242));
		FontFamily family(L"Segoe UI"); Font titleFont(&family, 22, FontStyleBold, UnitPixel), bodyFont(&family, 16, FontStyleRegular, UnitPixel), smallFont(&family, 13, FontStyleRegular, UnitPixel);
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
		const float progress = g_player && g_player->duration() > 0 ? std::min(1.0f, g_player->position() / g_player->duration()) : 0;
		graphics.FillRectangle(&accent, barX, barY, barWidth * progress, 6.0f);
		const std::wstring timing = (g_player ? clockText(g_player->position()) : L"0:00") + L" / " + (g_player ? clockText(g_player->duration()) : L"0:00");
		graphics.DrawString(timing.c_str(), -1, &smallFont, PointF(24, barY + 13), &muted);
		if (g_player) graphics.DrawString(g_player->state().c_str(), -1, &smallFont, PointF(float(width - 90), barY + 13), &muted);
		graphics.Flush();
		SetViewportOrgEx(bufferDc, 0, 0, nullptr);
		BitBlt(dc, paint.rcPaint.left, paint.rcPaint.top, dirtyWidth, dirtyHeight, bufferDc, 0, 0, SRCCOPY);
		SelectObject(bufferDc, previousBitmap); DeleteObject(bufferBitmap); DeleteDC(bufferDc);
		EndPaint(window, &paint); return 0;
	}
	return DefWindowProcW(window, message, wParam, lParam);
}

static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (message == WM_CLOSE) { g_closeRequested = true; return 0; }
	if (message == WM_COMMAND && LOWORD(wParam) == ID_IMPORT_PLAYLIST && HIWORD(wParam) == BN_CLICKED) {
		const int length = GetWindowTextLengthW(g_playlistEdit);
		std::wstring value(size_t(length + 1), L'\0');
		if (length > 0) GetWindowTextW(g_playlistEdit, value.data(), length + 1);
		value.resize(size_t(length));
		const std::string url = wideToUtf8(value);
		if (url.empty()) { g_libraryStatus = L"Enter a playlist URL first."; InvalidateRect(window, nullptr, FALSE); return 0; }
		g_libraryStatus = L"Importing playlist into the Suite Media Player...";
		EnableWindow(g_importPlaylistButton, FALSE); InvalidateRect(window, nullptr, FALSE);
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
		InvalidateRect(window, nullptr, FALSE); return 0;
	}
	if (message == WM_HUB_REQUEST_RESULT) {
		std::unique_ptr<HubSearchResult> result(reinterpret_cast<HubSearchResult *>(lParam));
		if (result && result->error.empty()) {
			g_hub.enqueueRequest(std::move(result->track)); syncHubQueueView();
			if (!g_hub.hasCurrent()) playHubNext();
		}
		InvalidateRect(window, nullptr, FALSE); return 0;
	}
	if ((message == WM_MOUSEMOVE || message == WM_LBUTTONDOWN) && g_page == 0 && g_youtubePlayer &&
	    g_youtubePlayer->sendMouse(message, wParam, lParam)) return 0;
	if (message == WM_GETMINMAXINFO) {
		auto *limits = reinterpret_cast<MINMAXINFO *>(lParam);
		limits->ptMinTrackSize = POINT{760, 560};
		return 0;
	}
	if (message == WM_SIZE) {
		if (g_youtubePlayer) g_youtubePlayer->resize();
		positionLibraryControls(window);
		InvalidateRect(window, nullptr, FALSE);
		return 0;
	}
	if (message == WM_LBUTTONUP) {
		if (g_page == 0 && g_youtubePlayer && g_youtubePlayer->sendMouse(message, wParam, lParam)) return 0;
		POINT point{static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam))};
		for (int i = 0; i < 5; ++i) {
			if (!PtInRect(&g_transportButtons[i], point)) continue;
			const bool youtubeActive = g_youtubePlayer && g_youtubePlayer->active();
			const bool currentlyPlaying = youtubeActive ? g_youtubePlayer->playing() :
				(g_player && g_player->state() == L"playing");
			const char *command = i == 0 ? "PREVIOUS" : (i == 1 ? "RESTART" :
				(i == 3 ? "SKIP" : (i == 4 ? "STOP" : (currentlyPlaying ? "PAUSE" : "PLAY"))));
			if (i == 2 && !youtubeActive && (!g_player || g_player->state() == L"stopped") && g_hub.hasCurrent() == false)
				playHubNext();
			else if (i == 0) { HubTrack previous; if (g_hub.takePrevious(previous)) startHubTrack(previous, false); }
			else if (i == 3) playHubNext();
			else if (youtubeActive) g_youtubePlayer->command(command);
			else if (g_player) g_player->command(command);
			InvalidateRect(window, nullptr, FALSE);
			return 0;
		}
		RECT client{}; GetClientRect(window, &client);
		const int sidebar = sidebarWidthFor(client.right);
		if (PtInRect(&g_sidebarToggle, point)) {
			g_sidebarCollapsed = !g_sidebarCollapsed;
			if (g_youtubePlayer) g_youtubePlayer->resize();
			InvalidateRect(window, nullptr, FALSE);
			return 0;
		}
		if (g_page == 1 && PtInRect(&g_queuePreviousPage, point)) { g_queuePage = std::max(0, g_queuePage - 1); InvalidateRect(window, nullptr, FALSE); return 0; }
		if (g_page == 1 && PtInRect(&g_queueNextPage, point)) { ++g_queuePage; InvalidateRect(window, nullptr, FALSE); return 0; }
		if (g_page == 1 && PtInRect(&g_queueShuffle, point)) {
			g_hub.shuffleFallback(); g_queuePage = 0; syncHubQueueView(); saveHubState();
			InvalidateRect(window, nullptr, FALSE); return 0;
		}
		const int navStart = 104;
		if (point.x < sidebar && point.y >= navStart && point.y < navStart + 260) {
			g_page = std::clamp((static_cast<int>(point.y) - navStart) / 52, 0, 4);
			positionLibraryControls(window);
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
				HubTrack selected; if (g_hub.trackAt(playbackIndex, selected)) startHubTrack(selected);
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
	graphics.Clear(Color(255, 9, 14, 23));

	const Color primary(255, 241, 244, 249), secondary(255, 164, 175, 194), tertiary(255, 105, 120, 143);
	const Color accent(255, 139, 86, 246), accentSoft(72, 139, 86, 246);
	const Color surface(255, 18, 26, 39), raised(255, 24, 34, 50), border(255, 40, 52, 70);
	FontFamily family(L"Segoe UI");
	Font display(&family, 28, FontStyleBold, UnitPixel), heading(&family, 20, FontStyleBold, UnitPixel);
	Font body(&family, 15, FontStyleRegular, UnitPixel), bodyBold(&family, 15, FontStyleBold, UnitPixel);
	Font smallFont(&family, 12, FontStyleRegular, UnitPixel);
	const int width = client.right, height = client.bottom;
	const int sidebar = sidebarWidthFor(width);
	const bool expanded = sidebar > 150;
	const int transportTop = std::max(400, height - 106);

	SolidBrush sidebarBrush(Color(255, 12, 19, 30));
	graphics.FillRectangle(&sidebarBrush, 0, 0, sidebar, height);
	roundedPanel(graphics, RectF(14, 16, float(sidebar - 28), 62), 12, raised);
	label(graphics, L"RS", heading, RectF(expanded ? 22.0f : 15.0f, 18, expanded ? 44.0f : 48.0f, 58), accent, StringAlignmentCenter);
	g_sidebarToggle = expanded ? RECT{sidebar - 34, 31, sidebar - 8, 57} : RECT{64, 31, 90, 57};
	label(graphics, expanded ? L"\u2039" : L"\u203A", heading,
		RectF(float(g_sidebarToggle.left), float(g_sidebarToggle.top), 26, 26), secondary, StringAlignmentCenter);
	if (expanded) {
		label(graphics, L"RearSilver", bodyBold, RectF(68, 23, 132, 25), primary);
		label(graphics, L"STREAM SUITE", smallFont, RectF(68, 46, 132, 20), tertiary);
	}
	const wchar_t *pages[] = {L"Now Playing", L"Queue & Requests", L"Library", L"Overlay Designer", L"Settings"};
	const wchar_t *icons[] = {L"\u25B6", L"\u2261", L"\u266B", L"\u25C7", L"\u2699"};
	const float navStart = 104.0f;
	for (int i = 0; i < 5; ++i) {
		const float y = navStart + i * 52.0f;
		if (i == g_page) {
			roundedPanel(graphics, RectF(13, y, float(sidebar - 26), 42), 9, accentSoft);
			SolidBrush active(accent); graphics.FillRectangle(&active, 13.0f, y + 9.0f, 3.0f, 24.0f);
		}
		label(graphics, icons[i], bodyBold, RectF(23, y, 34, 42), i == g_page ? accent : secondary, StringAlignmentCenter);
		if (expanded) label(graphics, pages[i], body, RectF(63, y, 143, 42), i == g_page ? primary : secondary);
	}
	if (expanded) {
		roundedPanel(graphics, RectF(20, float(height - 58), 44, 24), 7, accentSoft);
		label(graphics, L"PRO", smallFont, RectF(20, float(height - 58), 44, 24), accent, StringAlignmentCenter);
		label(graphics, L"Media Player", smallFont, RectF(72, float(height - 58), 128, 24), tertiary);
	}

	const float contentX = float(sidebar + 28), contentWidth = float(width - sidebar - 56);
	const wchar_t *subtitles[] = {L"Your stream soundtrack at a glance", L"Manage what plays next",
		L"Organise local music and provider playlists", L"Create a design that fits your stream",
		L"Playback, appearance and accessibility"};
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
		label(graphics, L"No tracks queued", body, RectF(infoX + 14, queueY + 65, infoWidth - 28, 26), secondary);
		if (queueHeight > 125)
			label(graphics, L"Song requests will appear here first", smallFont,
				RectF(infoX + 14, queueY + 91, infoWidth - 28, 24), tertiary);
	} else if (g_page == 1) {
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
		else {
			const float pageY = float(transportTop - 58);
			g_queuePreviousPage = RECT{int(contentX + 28), int(pageY), int(contentX + 138), int(pageY + 34)};
			g_queueNextPage = RECT{int(contentX + contentWidth - 138), int(pageY), int(contentX + contentWidth - 28), int(pageY + 34)};
			g_queueShuffle = RECT{int(contentX + contentWidth / 2 - 72), int(pageY), int(contentX + contentWidth / 2 + 72), int(pageY + 34)};
			roundedPanel(graphics, RectF(float(g_queuePreviousPage.left), pageY, 110, 34), 7, raised);
			roundedPanel(graphics, RectF(float(g_queueNextPage.left), pageY, 110, 34), 7, raised);
			roundedPanel(graphics, RectF(float(g_queueShuffle.left), pageY, 144, 34), 7, accentSoft);
			label(graphics, L"Previous page", smallFont, RectF(float(g_queuePreviousPage.left), pageY, 110, 34), g_queuePage > 0 ? secondary : tertiary, StringAlignmentCenter);
			label(graphics, L"Next page", smallFont, RectF(float(g_queueNextPage.left), pageY, 110, 34), g_queuePage + 1 < pageCount ? secondary : tertiary, StringAlignmentCenter);
			label(graphics, L"Shuffle playlist", smallFont, RectF(float(g_queueShuffle.left), pageY, 144, 34), accent, StringAlignmentCenter);
			label(graphics, L"Page " + std::to_wstring(g_queuePage + 1) + L" / " + std::to_wstring(pageCount),
				smallFont, RectF(float(g_queueShuffle.left - 100), pageY - 30, 344, 24), tertiary, StringAlignmentCenter);
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
		label(graphics, std::to_wstring(g_hub.fallback().size()) + L" fallback tracks  |  " +
			std::to_wstring(g_hub.requests().size()) + L" requests", body,
			RectF(contentX + 28, 382, contentWidth - 56, 28), secondary);
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
	const float playbackPosition = youtubeActive ? g_youtubePlayer->position() : (g_player ? g_player->position() : 0.0f);
	const float playbackDuration = youtubeActive ? g_youtubePlayer->duration() : (g_player ? g_player->duration() : 0.0f);
	const bool playbackPlaying = youtubeActive ? g_youtubePlayer->playing() :
		(g_player && g_player->state() == L"playing");
	label(graphics, transportTitle, bodyBold,
		RectF(float(sidebar + 34), float(transportTop + 12), 260, 28), primary);
	const int controlX = width / 2 - 129;
	const int buttonX[] = {controlX, controlX + 52, controlX + 104, controlX + 166, controlX + 218};
	const wchar_t *buttonLabels[] = {L"|\u25C0", L"\u21BA", playbackPlaying ? L"II" : L"\u25B6", L"\u25B6|", L"\u25A0"};
	for (int i = 0; i < 5; ++i) {
		const int size = i == 2 ? 50 : 42, y = transportTop + (i == 2 ? 8 : 12);
		g_transportButtons[i] = RECT{buttonX[i], y, buttonX[i] + size, y + size};
		if (i == 2) roundedPanel(graphics, RectF(float(buttonX[i]), float(y), float(size), float(size)), 25, accent);
		label(graphics, buttonLabels[i], bodyBold, RectF(float(buttonX[i]), float(y), float(size), float(size)),
			i == 2 ? Color(255,255,255,255) : secondary, StringAlignmentCenter);
	}
	const float barX = float(sidebar + 34), barY = float(transportTop + 69), barWidth = float(width - sidebar - 68);
	SolidBrush track(border), progressBrush(accent);
	graphics.FillRectangle(&track, barX, barY, barWidth, 4.0f);
	const float progress = playbackDuration > 0 ? std::min(1.0f, playbackPosition / playbackDuration) : 0;
	graphics.FillRectangle(&progressBrush, barX, barY, barWidth * progress, 4.0f);
	const std::wstring timing = clockText(playbackPosition) + L" / " + clockText(playbackDuration);
	label(graphics, timing, smallFont, RectF(float(width - 150), float(transportTop + 14), 110, 24), tertiary, StringAlignmentFar);

	graphics.Flush();
	SetViewportOrgEx(bufferDc, 0, 0, nullptr);
	BitBlt(dc, paint.rcPaint.left, paint.rcPaint.top, dirtyWidth, dirtyHeight, bufferDc, 0, 0, SRCCOPY);
	SelectObject(bufferDc, previousBitmap); DeleteObject(bufferBitmap); DeleteDC(bufferDc);
	if (g_page == 0 && g_youtubePlayer && g_youtubePlayer->active()) g_youtubePlayer->paintTo(dc);
	EndPaint(window, &paint);
	return 0;
}

static void send(HANDLE pipe, const std::string &message) { if (pipe != INVALID_HANDLE_VALUE && !message.empty()) { DWORD written = 0; WriteFile(pipe, message.data(), DWORD(message.size()), &written, nullptr); } }

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
	PROCESS_POWER_THROTTLING_STATE powerState{};
	powerState.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
	powerState.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED |
		PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
	powerState.StateMask = 0;
	SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &powerState, sizeof(powerState));
	CefMainArgs cefMainArgs(instance);
	CefRefPtr<SuiteCefApp> cefApp = new SuiteCefApp();
	CefScopedSandboxInfo sandboxInfo;
	const int subprocessExit = CefExecuteProcess(cefMainArgs, cefApp, sandboxInfo.sandbox_info());
	if (subprocessExit >= 0) return subprocessExit;
	CefSettings cefSettings;
	cefSettings.multi_threaded_message_loop = true;
	cefSettings.windowless_rendering_enabled = true;
	cefSettings.log_severity = LOGSEVERITY_WARNING;
	CefString(&cefSettings.locale) = "en-GB";
	if (!CefInitialize(cefMainArgs, cefSettings, cefApp, sandboxInfo.sandbox_info())) return 1;

	CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	GdiplusStartupInput gdiplusInput; ULONG_PTR gdiplusToken = 0; GdiplusStartup(&gdiplusToken, &gdiplusInput, nullptr);
	Player player; g_player = &player; if (!player.initialise()) return 2;
	HICON appIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(101), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
	WNDCLASSW wc{}; wc.style = CS_DBLCLKS; wc.lpfnWndProc = windowProc; wc.hInstance = instance; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hIcon = appIcon; wc.lpszClassName = L"RearSilverMusicPlayerWindow"; RegisterClassW(&wc);
	HWND window = CreateWindowExW(0, wc.lpszClassName, L"RearSilver Stream Suite | Media Player", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
		CW_USEDEFAULT, CW_USEDEFAULT, 1120, 720, nullptr, nullptr, instance, nullptr);
	SendMessageW(window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(appIcon));
	SendMessageW(window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(appIcon));
	ShowWindow(window, SW_SHOW); UpdateWindow(window);
	g_playlistEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL,
		0, 0, 0, 0, window, nullptr, instance, nullptr);
	g_importPlaylistButton = CreateWindowExW(0, L"BUTTON", L"Import fallback playlist",
		WS_CHILD | BS_PUSHBUTTON, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_IMPORT_PLAYLIST)), instance, nullptr);
	HFONT uiFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
	SendMessageW(g_playlistEdit, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont), TRUE);
	SendMessageW(g_importPlaylistButton, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont), TRUE);
	loadHubState();
	positionLibraryControls(window);
	g_youtubePlayer = new CefYouTubePlayer();
	g_youtubePlayer->initialise(window);
	HANDLE pipe = CreateNamedPipeW(L"\\\\.\\pipe\\RearSilverStreamSuiteMusicPlayer", PIPE_ACCESS_DUPLEX,
		PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_NOWAIT, 1, 65536, 65536, 0, nullptr);
	if (pipe == INVALID_HANDLE_VALUE) return 3;
	bool connected = false, running = true; std::string input; ULONGLONG lastStatus = 0, lastHubStatus = 0, lastYouTubePoll = 0;
	while (running) {
		MSG message{}; while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
		if (g_closeRequested) { running = false; continue; }
		if (g_youtubePlayer->active() && GetTickCount64() - lastYouTubePoll >= 250) {
			g_youtubePlayer->pollStatus(); lastYouTubePoll = GetTickCount64();
		}
		if (!connected) { connected = ConnectNamedPipe(pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED; if (connected) send(pipe, player.status()); }
		if (connected) {
			char buffer[4096]; DWORD read = 0;
			if (ReadFile(pipe, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
				input.append(buffer, read); size_t newline = 0;
				while ((newline = input.find('\n')) != std::string::npos) {
					std::string line = input.substr(0, newline); input.erase(0, newline + 1); if (!line.empty() && line.back() == '\r') line.pop_back();
					if (line == "SHUTDOWN") { running = false; break; }
					if (line.rfind("HUB_IMPORT\t", 0) == 0) {
						const std::string url = line.substr(11);
						std::thread([window, url] { auto *result = new HubPlaylistResult(resolveHubPlaylist(url));
							if (!PostMessageW(window, WM_HUB_PLAYLIST_RESULT, 0, reinterpret_cast<LPARAM>(result))) delete result; }).detach();
					} else if (line.rfind("HUB_REQUEST\t", 0) == 0) {
						const size_t split = line.find('\t', 12);
						const std::string requester = split == std::string::npos ? std::string{} : line.substr(12, split - 12);
						const std::string query = split == std::string::npos ? line.substr(12) : line.substr(split + 1);
						std::thread([window, query, requester] { auto *result = new HubSearchResult(resolveHubSearch(query, requester));
							if (!PostMessageW(window, WM_HUB_REQUEST_RESULT, 0, reinterpret_cast<LPARAM>(result))) delete result; }).detach();
					} else if (line == "HUB_SHUFFLE") {
						g_hub.shuffleFallback(); g_queuePage = 0; syncHubQueueView(); saveHubState(); InvalidateRect(window, nullptr, FALSE);
					} else if (line.rfind("META\t", 0) == 0) {
						player.setMetadata(line.substr(5));
						InvalidateRect(window, nullptr, FALSE);
					} else {
						const size_t tab = line.find('\t');
						const std::string action = line.substr(0, tab);
						const std::string argument = tab == std::string::npos ? std::string{} : line.substr(tab + 1);
						if (action == "YOUTUBE") {
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
			} else if (GetLastError() == ERROR_BROKEN_PIPE) { DisconnectNamedPipe(pipe); connected = false; input.clear(); }
			if (connected && player.takeEnded()) send(pipe, "EVENT\tended\t" + player.path() + "\n");
			if (connected) {
				for (const std::string &event : g_youtubePlayer->takeEvents()) {
					if (event.rfind("EVENT\tyoutube-ended\t", 0) == 0 || event.rfind("EVENT\tyoutube-error\t", 0) == 0) playHubNext();
					send(pipe, event);
				}
			}
			if (connected && GetTickCount64() - lastStatus >= 100) {
				if (!g_youtubePlayer->active()) send(pipe, player.status());
				lastStatus = GetTickCount64();
				RECT client{}; GetClientRect(window, &client);
				RECT playbackRegion{0, std::max(0L, client.bottom - 86), client.right, client.bottom};
				InvalidateRect(window, &playbackRegion, FALSE);
			}
			if (connected && GetTickCount64() - lastHubStatus >= 500) {
				const std::string hubStatus = g_youtubePlayer->active() ?
					(g_youtubePlayer->playing() ? "playing" : "paused") : wideToUtf8(player.state());
				const int64_t hubPosition = int64_t((g_youtubePlayer->active() ? g_youtubePlayer->position() : player.position()) * 1000.0f);
				const int64_t hubDuration = int64_t((g_youtubePlayer->active() ? g_youtubePlayer->duration() : player.duration()) * 1000.0f);
				send(pipe, "HUB_STATE\t" + g_hub.snapshotJson(hubStatus, hubPosition, hubDuration) + "\n");
				lastHubStatus = GetTickCount64();
			}
		}
		Sleep(10);
	}
	CloseHandle(pipe); g_youtubePlayer->shutdown(); g_youtubePlayer = nullptr; DestroyWindow(window); g_player = nullptr;
	GdiplusShutdown(gdiplusToken); CoUninitialize(); CefShutdown(); return 0;
}
