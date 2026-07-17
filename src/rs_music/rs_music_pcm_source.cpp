#include "rs_music_pcm_source.hpp"
#include "rs_music_pcm_ipc.hpp"

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/platform.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

namespace {
constexpr const char *kSourceId = "rear_silver_music_pcm_test";
constexpr const char *kSourceName = "Music — Suite PCM Test";
obs_source_t *g_testSource = nullptr;
std::atomic<bool> g_outputAllowed{true};
std::atomic<uint32_t> g_activeWorkers{0};

class PcmSourceContext {
public:
	explicit PcmSourceContext(obs_source_t *source) : m_source(source), m_thread([this] { run(); }) {}
	~PcmSourceContext()
	{
		m_stop.store(true);
		if (m_thread.joinable()) m_thread.join();
		closeMapping();
	}

private:
	void closeMapping()
	{
		if (m_buffer) UnmapViewOfFile(m_buffer);
		if (m_mapping) CloseHandle(m_mapping);
		m_buffer = nullptr;
		m_mapping = nullptr;
		m_readFrame = 0;
	}

	bool connectMapping()
	{
		m_mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, RsMusicPcmIpc::kMappingName);
		if (!m_mapping) return false;
		m_buffer = static_cast<const RsMusicPcmIpc::SharedBuffer *>(
			MapViewOfFile(m_mapping, FILE_MAP_READ, 0, 0, sizeof(RsMusicPcmIpc::SharedBuffer)));
		if (!m_buffer || m_buffer->magic != RsMusicPcmIpc::kMagic ||
		    m_buffer->version != RsMusicPcmIpc::kVersion) {
			closeMapping();
			return false;
		}
		MemoryBarrier();
		m_readFrame = static_cast<uint64_t>(m_buffer->writeFrame);
		blog(LOG_INFO, "[RS Music PCM] Connected to Suite PCM test stream");
		return true;
	}

	void run()
	{
		g_activeWorkers.fetch_add(1);
		constexpr uint32_t blockFrames = 480;
		constexpr uint64_t targetBufferedFrames = blockFrames * 3;
		constexpr auto blockDuration = std::chrono::milliseconds(10);
		float left[blockFrames]{};
		float right[blockFrames]{};
		auto nextOutput = std::chrono::steady_clock::now();
		while (!m_stop.load() && g_outputAllowed.load()) {
			if (!m_buffer && !connectMapping()) {
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				continue;
			}

			MemoryBarrier();
			const uint64_t writeFrame = static_cast<uint64_t>(m_buffer->writeFrame);
			if (writeFrame < m_readFrame) {
				m_readFrame = writeFrame;
				continue;
			}
			uint64_t available = writeFrame - m_readFrame;
			if (available > RsMusicPcmIpc::kCapacityFrames) {
				m_readFrame = writeFrame - RsMusicPcmIpc::kCapacityFrames;
				available = RsMusicPcmIpc::kCapacityFrames;
			}
			// Never replay a delayed Chromium burst at high speed. Keep a small,
			// stable latency and discard stale audio after a stall or focus change.
			if (available > targetBufferedFrames + blockFrames * 4) {
				m_readFrame = writeFrame - targetBufferedFrames;
				available = targetBufferedFrames;
				nextOutput = std::chrono::steady_clock::now();
			}
			if (available < blockFrames) {
				std::this_thread::sleep_for(std::chrono::milliseconds(2));
				continue;
			}

			const auto now = std::chrono::steady_clock::now();
			if (nextOutput > now) std::this_thread::sleep_until(nextOutput);
			else if (now - nextOutput > blockDuration * 2) nextOutput = now;

			for (uint32_t frame = 0; frame < blockFrames; ++frame) {
				const uint64_t index = (m_readFrame + frame) % RsMusicPcmIpc::kCapacityFrames;
				left[frame] = m_buffer->interleaved[index * 2];
				right[frame] = m_buffer->interleaved[index * 2 + 1];
			}
			m_readFrame += blockFrames;

			obs_source_audio audio{};
			audio.data[0] = reinterpret_cast<uint8_t *>(left);
			audio.data[1] = reinterpret_cast<uint8_t *>(right);
			audio.frames = blockFrames;
			audio.speakers = SPEAKERS_STEREO;
			audio.samples_per_sec = RsMusicPcmIpc::kSampleRate;
			audio.format = AUDIO_FORMAT_FLOAT_PLANAR;
			audio.timestamp = os_gettime_ns();
			if (g_outputAllowed.load()) obs_source_output_audio(m_source, &audio);
			nextOutput += blockDuration;
		}
		g_activeWorkers.fetch_sub(1);
	}

	obs_source_t *m_source = nullptr;
	std::atomic<bool> m_stop{false};
	std::thread m_thread;
	HANDLE m_mapping = nullptr;
	const RsMusicPcmIpc::SharedBuffer *m_buffer = nullptr;
	uint64_t m_readFrame = 0;
};

const char *sourceDisplayName(void *) { return "RearSilver Suite PCM test source"; }
void *sourceCreate(obs_data_t *, obs_source_t *source) { return new PcmSourceContext(source); }
void sourceDestroy(void *data) { delete static_cast<PcmSourceContext *>(data); }

obs_source_info sourceInfo = [] {
	obs_source_info info{};
	info.id = kSourceId;
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_AUDIO;
	info.get_name = sourceDisplayName;
	info.create = sourceCreate;
	info.destroy = sourceDestroy;
	return info;
}();
} // namespace

void rsMusicPcmRegisterSource() { obs_register_source(&sourceInfo); }

void rsMusicPcmStopOutput()
{
	g_outputAllowed.store(false);
	for (int attempt = 0; attempt < 200 && g_activeWorkers.load() != 0; ++attempt)
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
}

void rsMusicPcmRemoveLegacyTestSource()
{
	rsMusicPcmStopOutput();
	if (!g_testSource) g_testSource = obs_get_source_by_name(kSourceName);
	if (!g_testSource) return;
	blog(LOG_INFO, "[RS Music PCM] Removing obsolete Suite PCM test source");
	obs_source_remove(g_testSource);
}

void rsMusicPcmShutdownSource()
{
	if (!g_testSource) return;
	obs_source_release(g_testSource);
	g_testSource = nullptr;
}
