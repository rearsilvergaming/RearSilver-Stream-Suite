#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <wrl.h>

#include "WebView2.h"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace Gdiplus;
using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

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

class YouTubePlayer {
public:
	void initialise(HWND parent)
	{
		m_parent = parent;
		wchar_t executable[MAX_PATH]{};
		GetModuleFileNameW(nullptr, executable, MAX_PATH);
		std::wstring folder(executable);
		const size_t separator = folder.find_last_of(L"\\/");
		if (separator != std::wstring::npos) folder.resize(separator);

		CreateCoreWebView2EnvironmentWithOptions(
			nullptr, nullptr, nullptr,
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
											if (SUCCEEDED(args->get_WebMessageAsJson(&raw)) && raw) {
												const std::wstring message(raw);
												CoTaskMemFree(raw);
												if (message.find(L"\"type\":\"ready\"") != std::wstring::npos) {
													m_ready = true;
													loadPending();
												} else if (message.find(L"\"state\":\"ended\"") != std::wstring::npos) {
													m_events.emplace_back("EVENT\tyoutube-ended\t" + wideToUtf8(m_currentVideoId) + "\n");
												} else if (message.find(L"\"type\":\"error\"") != std::wstring::npos) {
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
		const int availableWidth = std::max(200L, client.right - 32);
		const int videoHeight = std::max(200, std::min(270, availableWidth * 9 / 16));
		RECT bounds{16, 16, 16 + availableWidth, 16 + videoHeight};
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
		if (m_controller) m_controller->put_IsVisible(TRUE);
		loadPending();
	}

	void hide()
	{
		m_active = false;
		if (m_controller) m_controller->put_IsVisible(FALSE);
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
	bool initialise() { return ma_engine_init(nullptr, &m_engine) == MA_SUCCESS; }
	~Player() { unload(); ma_engine_uninit(&m_engine); }

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
		if (ma_sound_init_from_file_w(&m_engine, utf8ToWide(path).c_str(), 0, nullptr, nullptr, &m_sound) != MA_SUCCESS)
			return "ERROR\tThe companion player could not decode this file.\n";
		m_ready = true; m_paused = false; m_endedSent = false; m_path = path;
		ma_sound_start(&m_sound);
		return "EVENT\tloaded\t" + m_path + "\n";
	}
	void stop() { if (m_ready) { ma_sound_stop(&m_sound); ma_sound_seek_to_pcm_frame(&m_sound, 0); m_paused = false; m_endedSent = false; } }
	void unload() { if (m_ready) { ma_sound_stop(&m_sound); ma_sound_uninit(&m_sound); m_ready = false; m_path.clear(); } }
	ma_engine m_engine{}; ma_sound m_sound{};
	bool m_ready = false, m_paused = false, m_endedSent = false;
	float m_position = 0, m_duration = 0;
	std::string m_path;
	std::wstring m_title = L"No track playing", m_artist, m_album, m_state = L"Stopped";
	std::unique_ptr<Image> m_artwork;
};

static Player *g_player = nullptr;
static YouTubePlayer *g_youtubePlayer = nullptr;

static std::wstring clockText(float seconds)
{
	const int total = std::max(0, int(seconds));
	wchar_t text[32]; swprintf_s(text, L"%d:%02d", total / 60, total % 60); return text;
}

static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (message == WM_CLOSE) { ShowWindow(window, SW_MINIMIZE); return 0; }
	if (message == WM_SIZE) {
		if (g_youtubePlayer) g_youtubePlayer->resize();
		return 0;
	}
	if (message == WM_ERASEBKGND) return 1;
	if (message == WM_PAINT) {
		PAINTSTRUCT paint{}; HDC dc = BeginPaint(window, &paint); RECT client{}; GetClientRect(window, &client);
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

static void send(HANDLE pipe, const std::string &message) { if (pipe != INVALID_HANDLE_VALUE && !message.empty()) { DWORD written = 0; WriteFile(pipe, message.data(), DWORD(message.size()), &written, nullptr); } }

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
	CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	GdiplusStartupInput gdiplusInput; ULONG_PTR gdiplusToken = 0; GdiplusStartup(&gdiplusToken, &gdiplusInput, nullptr);
	Player player; g_player = &player; if (!player.initialise()) return 2;
	HICON appIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(101), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
	WNDCLASSW wc{}; wc.lpfnWndProc = windowProc; wc.hInstance = instance; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hIcon = appIcon; wc.lpszClassName = L"RearSilverMusicPlayerWindow"; RegisterClassW(&wc);
	HWND window = CreateWindowExW(0, wc.lpszClassName, L"RearSilver Stream Suite | Media Player", WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX,
		CW_USEDEFAULT, CW_USEDEFAULT, 500, 620, nullptr, nullptr, instance, nullptr);
	SendMessageW(window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(appIcon));
	SendMessageW(window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(appIcon));
	ShowWindow(window, SW_SHOW); UpdateWindow(window);
	YouTubePlayer youtubePlayer; g_youtubePlayer = &youtubePlayer; youtubePlayer.initialise(window);
	HANDLE pipe = CreateNamedPipeW(L"\\\\.\\pipe\\RearSilverStreamSuiteMusicPlayer", PIPE_ACCESS_DUPLEX,
		PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_NOWAIT, 1, 65536, 65536, 0, nullptr);
	if (pipe == INVALID_HANDLE_VALUE) return 3;
	bool connected = false, running = true; std::string input; ULONGLONG lastStatus = 0;
	while (running) {
		MSG message{}; while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
		if (!connected) { connected = ConnectNamedPipe(pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED; if (connected) send(pipe, player.status()); }
		if (connected) {
			char buffer[4096]; DWORD read = 0;
			if (ReadFile(pipe, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
				input.append(buffer, read); size_t newline = 0;
				while ((newline = input.find('\n')) != std::string::npos) {
					std::string line = input.substr(0, newline); input.erase(0, newline + 1); if (!line.empty() && line.back() == '\r') line.pop_back();
					if (line == "SHUTDOWN") { running = false; break; }
					if (line.rfind("META\t", 0) == 0) {
						player.setMetadata(line.substr(5));
						InvalidateRect(window, nullptr, FALSE);
					} else {
						const size_t tab = line.find('\t');
						const std::string action = line.substr(0, tab);
						const std::string argument = tab == std::string::npos ? std::string{} : line.substr(tab + 1);
						if (action == "YOUTUBE") {
							player.command("STOP");
							youtubePlayer.load(argument);
						} else if (action == "LOAD") {
							youtubePlayer.command("STOP");
							youtubePlayer.hide();
							send(pipe, player.command(line));
						} else if (youtubePlayer.active()) {
							youtubePlayer.command(action, argument);
						} else {
							send(pipe, player.command(line));
						}
					}
				}
			} else if (GetLastError() == ERROR_BROKEN_PIPE) { DisconnectNamedPipe(pipe); connected = false; input.clear(); }
			if (connected && player.takeEnded()) send(pipe, "EVENT\tended\t" + player.path() + "\n");
			if (connected) {
				for (const std::string &event : youtubePlayer.takeEvents()) send(pipe, event);
			}
			if (connected && GetTickCount64() - lastStatus >= 100) {
				send(pipe, player.status()); lastStatus = GetTickCount64();
				RECT client{}; GetClientRect(window, &client);
				RECT playbackRegion{0, std::max(0L, client.bottom - 86), client.right, client.bottom};
				InvalidateRect(window, &playbackRegion, FALSE);
			}
		}
		Sleep(10);
	}
	CloseHandle(pipe); g_youtubePlayer = nullptr; DestroyWindow(window); g_player = nullptr;
	GdiplusShutdown(gdiplusToken); CoUninitialize(); return 0;
}
