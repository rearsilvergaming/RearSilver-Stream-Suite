#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

struct SystemMediaState {
	bool available = false;
	bool playing = false;
	bool canPlay = false;
	bool canPause = false;
	bool canNext = false;
	bool canPrevious = false;
	bool canSeek = false;
	std::string sourceAppId;
	std::string title;
	std::string artist;
	std::string album;
	std::string artworkPath;
	int64_t positionMs = 0;
	int64_t durationMs = 0;
};

class SystemMediaProvider {
public:
	enum class Action { Play, Pause, Next, Previous, Restart, Seek };

	SystemMediaProvider() = default;
	~SystemMediaProvider();
	SystemMediaProvider(const SystemMediaProvider &) = delete;
	SystemMediaProvider &operator=(const SystemMediaProvider &) = delete;

	void start(std::string preferredApplication = "spotify");
	void stop();
	void setPreferredApplication(std::string preferredApplication);
	void command(Action action, int64_t positionMs = 0);
	SystemMediaState state() const;

private:
	struct PendingAction { Action action; int64_t positionMs; };
	void worker();

	mutable std::mutex m_mutex;
	std::condition_variable m_wake;
	std::thread m_thread;
	std::atomic<bool> m_stop{false};
	std::string m_preferredApplication = "spotify";
	std::deque<PendingAction> m_actions;
	SystemMediaState m_state;
};
