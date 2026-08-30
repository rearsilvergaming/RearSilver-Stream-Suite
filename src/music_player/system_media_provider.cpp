#include "system_media_provider.hpp"

#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace winrt;
using namespace Windows::Media::Control;
using namespace Windows::Storage::Streams;

namespace {
std::string utf8(const hstring &value)
{
	if (value.empty()) return {};
	const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), int(value.size()), nullptr, 0, nullptr, nullptr);
	std::string output(size_t(size), '\0');
	WideCharToMultiByte(CP_UTF8, 0, value.c_str(), int(value.size()), output.data(), size, nullptr, nullptr);
	return output;
}

std::string lower(std::string value)
{
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return char(std::tolower(c)); });
	return value;
}

std::filesystem::path artworkPath(const GlobalSystemMediaTransportControlsSessionMediaProperties &media)
{
	wchar_t appData[MAX_PATH]{};
	if (!GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH)) return {};
	const std::filesystem::path folder = std::filesystem::path(appData) / L"RearSilver Stream Suite" / L"external-media";
	std::error_code error; std::filesystem::create_directories(folder, error);
	const std::string key = utf8(media.Title()) + "\n" + utf8(media.Artist()) + "\n" + utf8(media.AlbumTitle());
	return folder / (L"artwork-" + std::to_wstring(std::hash<std::string>{}(key)) + L".bin");
}

std::string cacheArtwork(const GlobalSystemMediaTransportControlsSessionMediaProperties &media)
{
	try {
		const auto reference = media.Thumbnail();
		if (!reference) return {};
		const auto stream = reference.OpenReadAsync().get();
		if (!stream || stream.Size() == 0 || stream.Size() > 20 * 1024 * 1024) return {};
		DataReader reader(stream);
		reader.LoadAsync(uint32_t(stream.Size())).get();
		std::vector<uint8_t> bytes(size_t(stream.Size()));
		reader.ReadBytes(bytes);
		const auto path = artworkPath(media);
		if (path.empty()) return {};
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		output.write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size()));
		if (!output) return {};
		const std::wstring wide = path.wstring();
		const int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), int(wide.size()), nullptr, 0, nullptr, nullptr);
		std::string result(size_t(size), '\0');
		WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), int(wide.size()), result.data(), size, nullptr, nullptr);
		return result;
	} catch (...) { return {}; }
}
}

SystemMediaProvider::~SystemMediaProvider() { stop(); }

void SystemMediaProvider::start(std::string preferredApplication)
{
	stop();
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_preferredApplication = lower(std::move(preferredApplication));
		m_state = {};
	}
	m_stop.store(false);
	m_thread = std::thread(&SystemMediaProvider::worker, this);
}

void SystemMediaProvider::stop()
{
	m_stop.store(true); m_wake.notify_all();
	if (m_thread.joinable()) m_thread.join();
}

void SystemMediaProvider::setPreferredApplication(std::string preferredApplication)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_preferredApplication = lower(std::move(preferredApplication));
	m_wake.notify_all();
}

void SystemMediaProvider::command(Action action, int64_t positionMs)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_actions.push_back({action, positionMs}); m_wake.notify_all();
}

SystemMediaState SystemMediaProvider::state() const
{
	std::lock_guard<std::mutex> lock(m_mutex); return m_state;
}

void SystemMediaProvider::worker()
{
	init_apartment(apartment_type::multi_threaded);
	GlobalSystemMediaTransportControlsSessionManager manager{nullptr};
	try { manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get(); } catch (...) {}
	while (!m_stop.load()) {
		SystemMediaState next;
		try {
			if (!manager) manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
			GlobalSystemMediaTransportControlsSession selected{nullptr};
			std::string preferred;
			{ std::lock_guard<std::mutex> lock(m_mutex); preferred = m_preferredApplication; }
			for (const auto &session : manager.GetSessions()) {
				const std::string source = utf8(session.SourceAppUserModelId());
				if (!preferred.empty() && lower(source) == preferred) {
					selected = session;
					break;
				}
			}
			std::deque<PendingAction> actions;
			{ std::lock_guard<std::mutex> lock(m_mutex); actions.swap(m_actions); }
			if (selected) {
				for (const auto &action : actions) {
					switch (action.action) {
					case Action::Play: selected.TryPlayAsync().get(); break;
					case Action::Pause: selected.TryPauseAsync().get(); break;
					case Action::Next: selected.TrySkipNextAsync().get(); break;
					case Action::Previous: selected.TrySkipPreviousAsync().get(); break;
					case Action::Restart: selected.TryChangePlaybackPositionAsync(0).get(); break;
					case Action::Seek: selected.TryChangePlaybackPositionAsync(action.positionMs * 10000).get(); break;
					}
				}
				const auto media = selected.TryGetMediaPropertiesAsync().get();
				const auto playback = selected.GetPlaybackInfo();
				const auto controls = playback.Controls();
				const auto timeline = selected.GetTimelineProperties();
				next.available = true;
				next.playing = playback.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
				next.canPlay = controls.IsPlayEnabled(); next.canPause = controls.IsPauseEnabled();
				next.canNext = controls.IsNextEnabled(); next.canPrevious = controls.IsPreviousEnabled();
				next.canSeek = controls.IsPlaybackPositionEnabled();
				next.sourceAppId = utf8(selected.SourceAppUserModelId());
				next.title = utf8(media.Title()); next.artist = utf8(media.Artist()); next.album = utf8(media.AlbumTitle());
				next.positionMs = timeline.Position().count() / 10000;
				// Position() is the position at LastUpdatedTime(), not necessarily
				// at the instant we read it. Spotify commonly republishes this
				// snapshot only every few seconds. Project the timestamped sample
				// to now so consumers receive one current clock rather than a
				// stale value that repeatedly pulls an interpolated UI backwards.
				if (next.playing) {
					const auto elapsed = winrt::clock::now() - timeline.LastUpdatedTime();
					const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
					if (elapsedMs > 0 && elapsedMs < 60000) next.positionMs += elapsedMs;
				}
				next.durationMs = std::max<int64_t>(0, (timeline.EndTime() - timeline.StartTime()).count() / 10000);
				if (next.durationMs > 0) next.positionMs = (std::min<int64_t>)(next.positionMs, next.durationMs);
				SystemMediaState previous;
				{ std::lock_guard<std::mutex> lock(m_mutex); previous = m_state; }
				next.artworkPath = previous.title == next.title && previous.artist == next.artist ? previous.artworkPath : cacheArtwork(media);
			}
		} catch (...) { next = {}; }
		{ std::lock_guard<std::mutex> lock(m_mutex); m_state = std::move(next); }
		std::unique_lock<std::mutex> lock(m_mutex);
		m_wake.wait_for(lock, std::chrono::milliseconds(500), [this] { return m_stop.load() || !m_actions.empty(); });
	}
	uninit_apartment();
}
