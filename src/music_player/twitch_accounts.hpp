#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

struct TwitchAccountState {
	bool authorized = false;
	bool connected = false;
	bool busy = false;
	std::string login;
	std::string userId;
	std::string error;
};

class TwitchAccount {
public:
	explicit TwitchAccount(std::string account);
	~TwitchAccount();
	void start();
	void stop();
	void beginLogin();
	void reconnect();
	void logout();
	TwitchAccountState state() const;
	std::string accessToken() const;
	uint64_t revision() const { return m_revision.load(); }
	std::string diagnostics() const;

private:
	void loginWorker();
	void reconnectWorker();
	bool validate();
	bool refresh();
	bool load();
	void save();
	void clear();
	void changed();

	std::string m_account;
	mutable std::mutex m_mutex;
	TwitchAccountState m_state;
	std::string m_accessToken, m_refreshToken;
	std::thread m_worker;
	std::atomic<bool> m_stop{false};
	std::atomic<uint64_t> m_revision{0};
};

