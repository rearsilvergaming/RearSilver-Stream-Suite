#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

struct HubTrack {
	std::string id;
	std::string providerId;
	std::string title;
	std::string artist;
	std::string artworkUrl;
	std::string requestedBy;
	int durationSeconds = 0;
	bool request = false;
};

class MusicHubModel {
public:
	void replaceFallback(std::vector<HubTrack> tracks, std::string label, std::string sourceUrl);
	void enqueueRequest(HubTrack track);
	bool removeRequest(const std::string &id);
	void clearRequests();
	void shuffleFallback();

	bool takeNext(HubTrack &track);
	bool trackAt(size_t playbackIndex, HubTrack &track) const;
	bool takePrevious(HubTrack &track);
	void recordStarted(const HubTrack &track);
	void clearCurrent();

	bool hasCurrent() const;
	HubTrack current() const;
	std::vector<HubTrack> playbackOrder() const;
	std::vector<HubTrack> requests() const;
	std::vector<HubTrack> fallback() const;
	std::string fallbackLabel() const;
	std::string fallbackUrl() const;
	std::string snapshotJson(const std::string &status, int64_t positionMs, int64_t durationMs) const;

private:
	static std::string json(const std::string &value);
	static std::string trackJson(const HubTrack &track);

	mutable std::mutex m_mutex;
	std::vector<HubTrack> m_fallback;
	std::deque<HubTrack> m_requests;
	std::deque<HubTrack> m_history;
	std::deque<HubTrack> m_replayNext;
	HubTrack m_current;
	std::string m_fallbackLabel;
	std::string m_fallbackUrl;
	size_t m_cursor = 0;
	bool m_hasCurrent = false;
};
