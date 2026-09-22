#pragma once

#include "system_media_provider.hpp"

#include <windows.h>

#include <mutex>
#include <string>

struct SystemMediaSharedState;

class SystemMediaClient {
public:
	using Action = SystemMediaProvider::Action;

	SystemMediaClient() = default;
	~SystemMediaClient();
	SystemMediaClient(const SystemMediaClient &) = delete;
	SystemMediaClient &operator=(const SystemMediaClient &) = delete;

	void start(std::string preferredApplication = "spotify.exe");
	void stop();
	void setPreferredApplication(std::string preferredApplication);
	void command(Action action, int64_t positionMs = 0);
	SystemMediaState state() const;

private:
	bool helperRunning() const;
	void closeHandles();

	mutable std::mutex m_lifecycleMutex;
	HANDLE m_mapping = nullptr;
	HANDLE m_sharedMutex = nullptr;
	HANDLE m_wakeEvent = nullptr;
	HANDLE m_process = nullptr;
	SystemMediaSharedState *m_shared = nullptr;
	std::string m_preferredApplication = "spotify.exe";
	mutable SystemMediaState m_cachedState;
};
