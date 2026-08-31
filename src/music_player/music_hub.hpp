#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

struct HubTrack {
	std::string id;
	std::string providerId;
	std::string provider = "youtube";
	std::string title;
	std::string artist;
	std::string album;
	std::string artworkUrl;
	std::string requestedBy;
	std::string requesterId;
	int requesterLevel = 0;
	int durationSeconds = 0;
	int discNumber = 0;
	int trackNumber = 0;
	bool request = false;
	bool cancelled = false;
};

class MusicHubModel {
public:
	void replaceFallback(std::vector<HubTrack> tracks, std::string label, std::string sourceUrl);
	void replaceLocalLibrary(std::vector<HubTrack> tracks);
	void clearLocalLibrary();
	void activateSource(std::string source);
	void setNonRequestLabel(std::string label);
	void enqueueRequest(HubTrack track);
	void replaceRequests(std::vector<HubTrack> tracks);
	bool removeRequest(const std::string &id);
	bool cancelRequest(const std::string &id);
	void clearRequests();
	void shuffleFallback();
	void restoreFallbackOrder();
	void prepareFallbackForLaunch(bool shuffle, const HubTrack *continuation);
	bool restartFallback(HubTrack &track);
	bool fallbackShuffled() const;

	bool takeNext(HubTrack &track);
	bool trackAt(size_t playbackIndex, HubTrack &track) const;
	bool selectAt(size_t playbackIndex, HubTrack &track);
	bool takePrevious(HubTrack &track);
	void recordStarted(const HubTrack &track);
	void restoreCurrent(const HubTrack &track);
	void restoreFallbackPosition(const HubTrack &track);
	void clearCurrent();

	bool hasCurrent() const;
	HubTrack current() const;
	std::vector<HubTrack> playbackOrder() const;
	std::vector<HubTrack> requests() const;
	std::vector<HubTrack> fallback() const;
	std::vector<HubTrack> youtubeFallback() const;
	std::vector<HubTrack> localLibrary() const;
	std::string activeSource() const;
	std::string fallbackLabel() const;
	std::string fallbackUrl() const;
	std::string snapshotJson(const std::string &status, int64_t positionMs, int64_t durationMs,
				 bool includeLibraries = true) const;
	std::string persistentJson() const;

private:
	static std::string json(const std::string &value);
	static std::string trackJson(const HubTrack &track);

	mutable std::mutex m_mutex;
	std::vector<HubTrack> m_youtubeFallback;
	std::vector<HubTrack> m_localLibrary;
	std::deque<HubTrack> m_requests;
	std::deque<HubTrack> m_history;
	std::deque<HubTrack> m_forwardHistory;
	HubTrack m_current;
	std::string m_fallbackLabel;
	std::string m_fallbackUrl;
	std::string m_activeSource = "youtube";
	std::string m_nonRequestLabel = "Stream DJ";
	std::vector<HubTrack> m_youtubeRotation;
	std::vector<HubTrack> m_localRotation;
	size_t m_youtubeCursor = 0;
	size_t m_localCursor = 0;
	bool m_youtubeShuffled = false;
	bool m_localShuffled = false;
	bool m_hasCurrent = false;

	std::vector<HubTrack> &activeRotationLocked();
	const std::vector<HubTrack> &activeRotationLocked() const;
	size_t &activeCursorLocked();
	size_t activeCursorLocked() const;
	bool &activeShuffledLocked();
	bool activeShuffledLocked() const;
	const std::vector<HubTrack> &activeCanonicalLocked() const;
	const HubTrack *mostRecentFallbackLocked() const;
	void positionAfterTrackLocked(const HubTrack *track);
	void adoptTrackSourceLocked(const HubTrack &track);
};
