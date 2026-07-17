#pragma once

#include <QWidget>
#include <QRadioButton>

class QLabel;
class RsMusicState;
class RsMusicController;

class RsMusicTwitchAuth;
class QPushButton;

class RsMusicSettings : public QWidget {
	Q_OBJECT

public:
	explicit RsMusicSettings(RsMusicState *state, RsMusicController *controller, RsMusicTwitchAuth *streamerAuth, RsMusicTwitchAuth *botAuth,
				 QWidget *parent = nullptr);

signals:
	void senderPreferenceChanged();

private slots:
	void updateAuthUi();

private:
	RsMusicState *m_state = nullptr;
	RsMusicController *m_controller = nullptr;

	// ---- Streamer account UI ----
	QLabel *m_streamerStatus = nullptr;

	QPushButton *m_loginButton = nullptr;
	QPushButton *m_reconnectButton = nullptr;
	QPushButton *m_logoutButton = nullptr;

	RsMusicTwitchAuth *m_streamerAuth = nullptr;

	// ---- Bot account UI ----
	QLabel *m_botStatus = nullptr;

	QPushButton *m_botLoginButton = nullptr;
	QPushButton *m_botReconnectButton = nullptr;
	QPushButton *m_botLogoutButton = nullptr;

	RsMusicTwitchAuth *m_botAuth = nullptr;

	// ---- Sender selection ----
	QLabel *m_senderLabel = nullptr;
	QRadioButton *m_senderStreamerRadio = nullptr;
	QRadioButton *m_senderBotRadio = nullptr;
};
