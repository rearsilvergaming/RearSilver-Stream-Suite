#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct SpotifyQueueTrack {
	std::string uri;
	std::string title;
	std::string artist;
	std::string album;
	std::string artworkUrl;
	int64_t durationMs = 0;
};

struct SpotifyClientState {
	std::string clientId;
	bool authorized = false;
	bool connected = false;
	bool busy = false;
	bool queueChecked = false;
	bool playbackAvailable = false;
	std::string displayName;
	std::string error;
	SpotifyQueueTrack current;
	std::vector<SpotifyQueueTrack> queue;
};

class SpotifyClient {
public:
	SpotifyClient();
	~SpotifyClient();
	SpotifyClient(const SpotifyClient &) = delete;
	SpotifyClient &operator=(const SpotifyClient &) = delete;

	void start();
	void stop();
	void beginLogin();
	void logout();
	void refreshQueue();
	void refreshQueueAsync();
	bool searchTrack(const std::string &query, SpotifyQueueTrack &track);
	bool addToQueue(const std::string &uri);
	bool toggleShuffle();
	void setClientId(const std::string &clientId);
	void setClientIdAsync(std::string clientId);
	void logoutAsync();
	std::string clientId() const;
	std::string diagnostics() const;
	SpotifyClientState state() const;

private:
	void loginWorker();
	void queueWorker();
	void actionWorker();
	void enqueue(std::function<void()> action);
	bool exchangeCode(const std::string &code, const std::string &verifier);
	bool refreshAccessToken();
	bool loadProfile();
	bool loadQueue();
	bool loadCredentials();
	void saveCredentials();
	void clearCredentials();

	mutable std::mutex m_mutex;
	// Spotify rotates refresh tokens. All token/profile/queue operations must be
	// serialised so the UI refresh button cannot race the background poller.
	mutable std::mutex m_operationMutex;
	SpotifyClientState m_state;
	std::string m_accessToken;
	std::string m_refreshToken;
	std::string m_clientId;
	int64_t m_expiresAt = 0;
	std::thread m_loginThread;
	std::thread m_queueThread;
	std::thread m_actionThread;
	std::mutex m_actionMutex;
	std::condition_variable m_actionReady;
	std::deque<std::function<void()>> m_actions;
	std::atomic<bool> m_stop{false};
	size_t m_lastLoggedQueueCount = static_cast<size_t>(-1);
	bool m_shuffleEnabled = false;
};
