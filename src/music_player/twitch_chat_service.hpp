#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

struct TwitchChatMessage {
	std::string userId;
	std::string displayName;
	std::string text;
	bool subscriber = false;
	bool vip = false;
	bool moderator = false;
	bool broadcaster = false;
};

class TwitchChatService {
public:
	using Handler = std::function<void(const TwitchChatMessage &)>;
	TwitchChatService();
	~TwitchChatService();
	void connect(std::string login, std::string token, std::string channel,
		std::string broadcasterId, Handler handler = {});
	void disconnect();
	bool sendMessage(const std::string &message);
	bool connected() const { return m_connected.load(); }
	std::string diagnostics() const;

private:
	void run(std::string login, std::string token, std::string channel,
		std::string broadcasterId, Handler handler);
	bool sendRaw(const std::string &line);
	void note(const std::string &message);

	std::atomic<bool> m_stop{false};
	std::atomic<bool> m_connected{false};
	mutable std::mutex m_mutex;
	void *m_socket = nullptr;
	std::thread m_thread;
	std::string m_channel;
	std::string m_diagnostics;
};
