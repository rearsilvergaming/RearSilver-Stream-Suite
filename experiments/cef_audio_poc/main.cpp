#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "include/cef_app.h"
#include "include/cef_audio_handler.h"
#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_render_handler.h"
#include "include/cef_request.h"
#include "rs_music/rs_music_pcm_ipc.hpp"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <atomic>
#include <cmath>
#include <chrono>
#include <deque>
#include <cstring>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {
constexpr wchar_t kWindowClass[] = L"RearSilverCefAudioPocWindow";
constexpr UINT kBrowserClosed = WM_APP + 1;
HWND g_window = nullptr;
std::atomic<bool> g_appActive{false};

class PocApp final : public CefApp, public CefBrowserProcessHandler {
public:
	CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override { return this; }

	void OnBeforeCommandLineProcessing(const CefString &,
		CefRefPtr<CefCommandLine> commandLine) override
	{
		// Apply to browser, renderer and utility processes. YouTube's media/MSE
		// pipeline and CEF's audio service must remain active while unfocused.
		commandLine->AppendSwitch("disable-background-timer-throttling");
		commandLine->AppendSwitch("disable-renderer-backgrounding");
		commandLine->AppendSwitch("disable-backgrounding-occluded-windows");
		commandLine->AppendSwitch("disable-background-media-suspend");
		commandLine->AppendSwitchWithValue(
			"disable-features",
			"CalculateNativeWinOcclusion,IntensiveWakeUpThrottling,"
			"ThrottleDisplayNoneAndVisibilityHiddenCrossOriginIframes,"
			"UseEcoQoSForBackgroundProcess");
	}

private:
	IMPLEMENT_REFCOUNTING(PocApp);
};

class PcmSharedWriter {
public:
	~PcmSharedWriter()
	{
		m_stop.store(true);
		if (m_clockThread.joinable()) m_clockThread.join();
		if (m_outputInitialised) ma_device_uninit(&m_output);
		if (m_buffer) UnmapViewOfFile(m_buffer);
		if (m_mapping) CloseHandle(m_mapping);
	}

	bool initialise()
	{
		ma_device_config outputConfig = ma_device_config_init(ma_device_type_playback);
		outputConfig.playback.format = ma_format_f32;
		outputConfig.playback.channels = 2;
		outputConfig.sampleRate = RsMusicPcmIpc::kSampleRate;
		outputConfig.periodSizeInFrames = 480;
		outputConfig.dataCallback = &PcmSharedWriter::outputCallback;
		outputConfig.pUserData = this;
		if (ma_device_init(nullptr, &outputConfig, &m_output) != MA_SUCCESS) return false;
		m_outputInitialised = true;
		if (ma_device_start(&m_output) != MA_SUCCESS) return false;

		m_mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
			static_cast<DWORD>(sizeof(RsMusicPcmIpc::SharedBuffer)), RsMusicPcmIpc::kMappingName);
		if (!m_mapping) return false;
		m_buffer = static_cast<RsMusicPcmIpc::SharedBuffer *>(
			MapViewOfFile(m_mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(RsMusicPcmIpc::SharedBuffer)));
		if (!m_buffer) return false;
		std::memset(m_buffer, 0, sizeof(*m_buffer));
		m_buffer->magic = RsMusicPcmIpc::kMagic;
		m_buffer->version = RsMusicPcmIpc::kVersion;
		m_buffer->sampleRate = RsMusicPcmIpc::kSampleRate;
		m_buffer->channels = RsMusicPcmIpc::kChannels;
		m_buffer->capacityFrames = RsMusicPcmIpc::kCapacityFrames;
		m_clockThread = std::thread([this] { clockLoop(); });
		return true;
	}

	void write(const float **data, int channels, int frames)
	{
		if (!m_buffer || !data || channels <= 0 || frames <= 0) return;
		std::lock_guard<std::mutex> lock(m_queueMutex);
		for (int frame = 0; frame < frames; ++frame) {
			const float left = data[0] ? data[0][frame] : 0.0f;
			const float right = channels > 1 && data[1] ? data[1][frame] : left;
			m_queue.push_back(left);
			m_queue.push_back(right);
		}
		// A resumed renderer may deliver stale audio in a burst. Keep at most
		// 500 ms and let the clock thread trim it to the target latency.
		constexpr size_t maxSamples = 48000;
		while (m_queue.size() > maxSamples) {
			m_queue.pop_front();
			m_queue.pop_front();
		}
		m_queuedSamples.store(m_queue.size());
	}

	uint64_t publishedFrames() const { return m_publishedFrames.load(); }
	uint64_t queuedSamples() const { return m_queuedSamples.load(); }
	uint64_t underruns() const { return m_underruns.load(); }
	uint64_t trims() const { return m_trims.load(); }

private:
	static void outputCallback(ma_device *device, void *output, const void *, ma_uint32 frames)
	{
		static_cast<PcmSharedWriter *>(device->pUserData)->readOutput(
			static_cast<float *>(output), frames);
	}

	void readOutput(float *output, uint32_t frames)
	{
		if (!output) return;
		std::fill(output, output + static_cast<size_t>(frames) * 2, 0.0f);
		std::lock_guard<std::mutex> lock(m_outputMutex);
		const size_t samples = std::min(m_outputQueue.size(), static_cast<size_t>(frames) * 2);
		for (size_t i = 0; i < samples; ++i) {
			output[i] = m_outputQueue.front();
			m_outputQueue.pop_front();
		}
	}

	void publish(const float *interleaved, uint32_t frames)
	{
		{
			std::lock_guard<std::mutex> lock(m_outputMutex);
			m_outputQueue.insert(m_outputQueue.end(), interleaved, interleaved + static_cast<size_t>(frames) * 2);
			constexpr size_t maxOutputSamples = 48000;
			while (m_outputQueue.size() > maxOutputSamples) {
				m_outputQueue.pop_front();
				m_outputQueue.pop_front();
			}
		}
		const uint64_t start = m_writeFrame;
		for (uint32_t frame = 0; frame < frames; ++frame) {
			const uint64_t index = (start + frame) % RsMusicPcmIpc::kCapacityFrames;
			m_buffer->interleaved[index * 2] = interleaved[frame * 2];
			m_buffer->interleaved[index * 2 + 1] = interleaved[frame * 2 + 1];
		}
		m_writeFrame += frames;
		m_publishedFrames.fetch_add(frames);
		MemoryBarrier();
		InterlockedExchange64(&m_buffer->writeFrame, static_cast<LONG64>(m_writeFrame));
	}

	void clockLoop()
	{
		constexpr uint32_t blockFrames = 480;
		constexpr size_t blockSamples = blockFrames * 2;
		constexpr size_t primeSamples = blockSamples * 6; // 60 ms
		constexpr size_t targetSamples = blockSamples * 10; // 100 ms
		float block[blockSamples]{};
		bool primed = false;
		auto next = std::chrono::steady_clock::now();
		while (!m_stop.load()) {
			next += std::chrono::milliseconds(10);
			std::fill(std::begin(block), std::end(block), 0.0f);
			{
				std::lock_guard<std::mutex> lock(m_queueMutex);
				if (m_queue.size() > targetSamples * 2) {
					while (m_queue.size() > targetSamples) m_queue.pop_front();
					m_trims.fetch_add(1);
				}
				if (!primed && m_queue.size() >= primeSamples) primed = true;
				if (primed && m_queue.size() >= blockSamples) {
					for (size_t i = 0; i < blockSamples; ++i) {
						block[i] = m_queue.front();
						m_queue.pop_front();
					}
				} else if (primed) {
					primed = false;
					m_underruns.fetch_add(1);
				}
				m_queuedSamples.store(m_queue.size());
			}
			publish(block, blockFrames);
			std::this_thread::sleep_until(next);
			if (std::chrono::steady_clock::now() - next > std::chrono::milliseconds(50))
				next = std::chrono::steady_clock::now();
		}
	}

	HANDLE m_mapping = nullptr;
	RsMusicPcmIpc::SharedBuffer *m_buffer = nullptr;
	uint64_t m_writeFrame = 0;
	std::atomic<bool> m_stop{false};
	std::thread m_clockThread;
	std::mutex m_queueMutex;
	std::deque<float> m_queue;
	std::atomic<uint64_t> m_publishedFrames{0};
	std::atomic<uint64_t> m_queuedSamples{0};
	std::atomic<uint64_t> m_underruns{0};
	std::atomic<uint64_t> m_trims{0};
	ma_device m_output{};
	bool m_outputInitialised = false;
	std::mutex m_outputMutex;
	std::deque<float> m_outputQueue;
};

class PocClient final : public CefClient,
			public CefLifeSpanHandler,
			public CefAudioHandler,
			public CefRenderHandler {
public:
	CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
	CefRefPtr<CefAudioHandler> GetAudioHandler() override { return this; }
	CefRefPtr<CefRenderHandler> GetRenderHandler() override { return this; }

	void GetViewRect(CefRefPtr<CefBrowser>, CefRect &rect) override
	{
		RECT area{};
		GetClientRect(g_window, &area);
		rect = CefRect(0, 0, std::max(1L, area.right), std::max(1L, area.bottom));
	}

	void OnPaint(CefRefPtr<CefBrowser>, PaintElementType type, const RectList &,
		const void *buffer, int width, int height) override
	{
		if (type != PET_VIEW || !buffer || width <= 0 || height <= 0) return;
		{
			std::lock_guard<std::mutex> lock(m_paintMutex);
			m_paintWidth = width;
			m_paintHeight = height;
			m_pixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height));
			std::memcpy(m_pixels.data(), buffer, m_pixels.size() * sizeof(uint32_t));
		}
		if (g_window) InvalidateRect(g_window, nullptr, FALSE);
	}

	void paintTo(HDC dc, const RECT &target)
	{
		std::lock_guard<std::mutex> lock(m_paintMutex);
		if (m_pixels.empty() || m_paintWidth <= 0 || m_paintHeight <= 0) return;
		BITMAPINFO info{};
		info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		info.bmiHeader.biWidth = m_paintWidth;
		info.bmiHeader.biHeight = -m_paintHeight;
		info.bmiHeader.biPlanes = 1;
		info.bmiHeader.biBitCount = 32;
		info.bmiHeader.biCompression = BI_RGB;
		StretchDIBits(dc, 0, 0, target.right, target.bottom, 0, 0, m_paintWidth, m_paintHeight,
			m_pixels.data(), &info, DIB_RGB_COLORS, SRCCOPY);
	}

	void resizeBrowser()
	{
		if (m_browser) m_browser->GetHost()->WasResized();
	}

	void keepBrowserActive()
	{
		if (!m_browser) return;
		// In windowless mode the host owns visibility/focus state. Playback is
		// intentionally continuous while the Suite window is covered or unfocused.
		m_browser->GetHost()->WasHidden(false);
		m_browser->GetHost()->SetFocus(true);
	}

	void sendMouse(UINT message, WPARAM wParam, LPARAM lParam)
	{
		if (!m_browser) return;
		CefMouseEvent event;
		event.x = static_cast<int>(static_cast<short>(LOWORD(lParam)));
		event.y = static_cast<int>(static_cast<short>(HIWORD(lParam)));
		event.modifiers = 0;
		if (wParam & MK_SHIFT) event.modifiers |= EVENTFLAG_SHIFT_DOWN;
		if (wParam & MK_CONTROL) event.modifiers |= EVENTFLAG_CONTROL_DOWN;
		if (wParam & MK_LBUTTON) event.modifiers |= EVENTFLAG_LEFT_MOUSE_BUTTON;
		if (message == WM_MOUSEMOVE) {
			m_browser->GetHost()->SendMouseMoveEvent(event, false);
		} else if (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP) {
			if (message == WM_LBUTTONDOWN) SetFocus(g_window);
			m_browser->GetHost()->SendMouseClickEvent(event, MBT_LEFT,
				message == WM_LBUTTONUP, 1);
		}
	}

	bool GetAudioParameters(CefRefPtr<CefBrowser>, CefAudioParameters &params) override
	{
		params.channel_layout = CEF_CHANNEL_LAYOUT_STEREO;
		params.sample_rate = 48000;
		params.frames_per_buffer = 480;
		return true;
	}

	void OnAudioStreamStarted(CefRefPtr<CefBrowser>, const CefAudioParameters &params, int channels) override
	{
		m_sampleRate.store(params.sample_rate);
		m_channels.store(channels);
		m_streaming.store(true);
	}

	void OnAudioStreamPacket(CefRefPtr<CefBrowser>, const float **data, int frames, int64_t pts) override
	{
		m_writer.write(data, m_channels.load(), frames);
		m_lastPts.store(pts);
		m_lastPacketTick.store(GetTickCount64());
		double peak = 0.0;
		const int channels = m_channels.load();
		for (int channel = 0; channel < channels; ++channel) {
			if (!data[channel]) continue;
			for (int frame = 0; frame < frames; ++frame)
				peak = std::max(peak, std::abs(static_cast<double>(data[channel][frame])));
		}
		m_peak.store(peak);
		m_frames.fetch_add(static_cast<uint64_t>(frames));
		m_packets.fetch_add(1);
	}

	void OnAudioStreamStopped(CefRefPtr<CefBrowser>) override { m_streaming.store(false); }
	void OnAudioStreamError(CefRefPtr<CefBrowser>, const CefString &) override { m_streaming.store(false); }

	void OnAfterCreated(CefRefPtr<CefBrowser> browser) override
	{
		m_browser = browser;
		keepBrowserActive();
		auto request = CefRequest::Create();
		request->SetURL(
			"https://www.youtube.com/embed/lDK9QqIzhwk?enablejsapi=1&autoplay=0&controls=1&playsinline=1&origin=https%3A%2F%2Frearsilver.local");
		request->SetMethod("GET");
		request->SetReferrer("https://rearsilver.local/", REFERRER_POLICY_DEFAULT);
		browser->GetMainFrame()->LoadRequest(request);
	}
	void OnBeforeClose(CefRefPtr<CefBrowser>) override
	{
		m_browser = nullptr;
		if (g_window) PostMessageW(g_window, kBrowserClosed, 0, 0);
	}

	void closeBrowser()
	{
		if (m_browser) m_browser->GetHost()->CloseBrowser(false);
	}

	HWND browserWindow() const
	{
		return nullptr;
	}

	std::wstring statusText() const
	{
		const double seconds = m_sampleRate.load() > 0
			? static_cast<double>(m_frames.load()) / static_cast<double>(m_sampleRate.load())
			: 0.0;
		wchar_t text[256]{};
		swprintf_s(text, L"CEF YouTube PCM test | %s | packets: %llu | captured: %.1fs | peak: %.3f",
			   m_streaming.load() ? L"PCM ACTIVE" : L"waiting for playback",
			   static_cast<unsigned long long>(m_packets.load()), seconds, m_peak.load());
		return text;
	}

	void appendDiagnostic(bool focused) const
	{
		char tempPath[MAX_PATH]{};
		GetTempPathA(MAX_PATH, tempPath);
		const std::string path = std::string(tempPath) + "RearSilver-CefAudioPoc.csv";
		std::ofstream out(path, std::ios::app);
		if (!out) return;
		out << GetTickCount64() << ',' << (focused ? 1 : 0) << ','
			<< m_packets.load() << ',' << m_frames.load() << ','
			<< m_writer.publishedFrames() << ',' << m_writer.queuedSamples() << ','
			<< m_writer.underruns() << ',' << m_writer.trims() << ','
			<< m_lastPts.load() << ',' << m_lastPacketTick.load() << '\n';
	}

private:
	PcmSharedWriter m_writer;
	CefRefPtr<CefBrowser> m_browser;
	std::atomic<bool> m_streaming{false};
	std::atomic<int> m_sampleRate{0};
	std::atomic<int> m_channels{0};
	std::atomic<uint64_t> m_packets{0};
	std::atomic<uint64_t> m_frames{0};
	std::atomic<double> m_peak{0.0};
	std::atomic<int64_t> m_lastPts{0};
	std::atomic<uint64_t> m_lastPacketTick{0};
	std::mutex m_paintMutex;
	std::vector<uint32_t> m_pixels;
	int m_paintWidth = 0;
	int m_paintHeight = 0;
	IMPLEMENT_REFCOUNTING(PocClient);

public:
	bool initialisePcmWriter() { return m_writer.initialise(); }
};

CefRefPtr<PocClient> g_client;

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message) {
	case WM_TIMER:
		if (g_client) {
			g_client->keepBrowserActive();
			SetWindowTextW(window, g_client->statusText().c_str());
			g_client->appendDiagnostic(g_appActive.load());
		}
		return 0;
	case WM_ACTIVATEAPP:
		g_appActive.store(wParam != FALSE);
		return 0;
	case WM_SIZE:
		if (g_client) g_client->resizeBrowser();
		return 0;
	case WM_PAINT: {
		PAINTSTRUCT paint{};
		HDC dc = BeginPaint(window, &paint);
		RECT area{};
		GetClientRect(window, &area);
		FillRect(dc, &area, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
		if (g_client) g_client->paintTo(dc, area);
		EndPaint(window, &paint);
		return 0;
	}
	case WM_MOUSEMOVE:
	case WM_LBUTTONDOWN:
	case WM_LBUTTONUP:
		if (g_client) g_client->sendMouse(message, wParam, lParam);
		return 0;
	case WM_CLOSE:
		if (g_client) {
			g_client->closeBrowser();
			return 0;
		}
		DestroyWindow(window);
		return 0;
	case kBrowserClosed:
		DestroyWindow(window);
		return 0;
	case WM_DESTROY:
		KillTimer(window, 1);
		PostQuitMessage(0);
		return 0;
	default:
		return DefWindowProcW(window, message, wParam, lParam);
	}
}
} // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, wchar_t *, int showCommand)
{
	// Explicitly request HighQoS and honour high-resolution timers in every CEF
	// process. Do this before CefExecuteProcess so renderer/audio utility
	// subprocesses configure themselves rather than relying on inheritance.
	PROCESS_POWER_THROTTLING_STATE powerState{};
	powerState.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
	powerState.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED |
		PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
	powerState.StateMask = 0;
	SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling,
		&powerState, sizeof(powerState));

	CefMainArgs mainArgs(instance);
	CefRefPtr<PocApp> app = new PocApp();
	const int subprocessExit = CefExecuteProcess(mainArgs, app, nullptr);
	if (subprocessExit >= 0) return subprocessExit;

	char tempPath[MAX_PATH]{};
	GetTempPathA(MAX_PATH, tempPath);
	{
		std::ofstream reset(std::string(tempPath) + "RearSilver-CefAudioPoc.csv", std::ios::trunc);
		reset << "tick_ms,focused,packets,captured_frames,published_frames,queued_samples,underruns,trims,last_pts,last_packet_tick\n";
	}

	CefSettings settings;
	settings.no_sandbox = true;
	settings.multi_threaded_message_loop = true;
	settings.windowless_rendering_enabled = true;
	settings.log_severity = LOGSEVERITY_WARNING;
	CefString(&settings.locale) = "en-GB";
	if (!CefInitialize(mainArgs, settings, app, nullptr)) return 1;

	WNDCLASSEXW windowClass{};
	windowClass.cbSize = sizeof(windowClass);
	windowClass.hInstance = instance;
	windowClass.lpfnWndProc = windowProc;
	windowClass.lpszClassName = kWindowClass;
	windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
	windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
	RegisterClassExW(&windowClass);

	g_window = CreateWindowExW(0, kWindowClass, L"CEF YouTube PCM test | waiting",
		WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 900, 560,
		nullptr, nullptr, instance, nullptr);
	if (!g_window) {
		CefShutdown();
		return 2;
	}
	ShowWindow(g_window, showCommand);
	UpdateWindow(g_window);
	SetTimer(g_window, 1, 250, nullptr);

	RECT area{};
	GetClientRect(g_window, &area);
	CefWindowInfo windowInfo;
	windowInfo.SetAsWindowless(g_window);
	CefBrowserSettings browserSettings;
	browserSettings.windowless_frame_rate = 30;
	g_client = new PocClient();
	g_client->initialisePcmWriter();
	CefBrowserHost::CreateBrowser(windowInfo, g_client, "about:blank", browserSettings, nullptr, nullptr);

	MSG message{};
	while (GetMessageW(&message, nullptr, 0, 0) > 0) {
		TranslateMessage(&message);
		DispatchMessageW(&message);
	}
	g_client = nullptr;
	g_window = nullptr;
	CefShutdown();
	return 0;
}
