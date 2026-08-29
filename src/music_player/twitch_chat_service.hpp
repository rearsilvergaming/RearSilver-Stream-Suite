#pragma once

#include <atomic>
#include <cstdint>
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
	explicit TwitchChatService(std::string label = "chat");
	~TwitchChatService();
	void connect(std::string login, std::string token, std::string channel,
		std::string broadcasterId, Handler handler = {});
	void disconnect();
	bool sendMessage(const std::string &message);
	bool connected() const { return m_connected.load(); }
	uint64_t revision() const { return m_revision.load(); }
	void tick();
	std::string diagnostics() const;

private:
	void run(std::string login, std::string token, std::string channel,
		std::string broadcasterId, Handler handler);
	bool sendRaw(const std::string &line);
	void note(const std::string &message);
	void setConnected(bool connected, const std::string &reason);
	void interrupt(const std::string &reason);

	std::atomic<bool> m_stop{false};
	std::atomic<bool> m_connected{false};
	std::atomic<bool> m_interrupting{false};
	std::atomic<uint64_t> m_revision{0};
	std::atomic<uint64_t> m_lastActivity{0};
	mutable std::mutex m_mutex;
	void *m_socket = nullptr;
	std::thread m_thread;
	std::string m_channel;
	std::string m_diagnostics;
	std::string m_label;
};
