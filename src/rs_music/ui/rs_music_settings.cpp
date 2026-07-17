#include "rs_music_settings.hpp"
#include "rs_music/state/rs_music_state.hpp"
#include "rs_music/rs_music_controller.hpp"

#include "rs_music/rs_music_twitch_auth.hpp"
#include "rs_entitlements.hpp"

#include <QPushButton>
#include <QVBoxLayout>
#include <QLabel>
#include <QFont>
#include <QFrame>
#include <QRadioButton>
#include <QSettings>
#include <QLineEdit>

static bool loadSendFromBotSetting()
{
	QSettings s("RearSilver", "RearSilver-Stream-Suite");
	return s.value("music/twitch/send_from_bot", false).toBool();
}

static void saveSendFromBotSetting(bool sendFromBot)
{
	QSettings s("RearSilver", "RearSilver-Stream-Suite");
	s.setValue("music/twitch/send_from_bot", sendFromBot);
}


static QLabel *makeTitle(const QString &text)
{
	auto *lbl = new QLabel(text);
	QFont f = lbl->font();
	f.setBold(true);
	f.setPointSize(f.pointSize() + 2);
	lbl->setFont(f);
	return lbl;
}

static QLabel *makeProviderStatus(const QString &provider, const QString &status, bool ready)
{
	auto *label = new QLabel(QString("%1  —  %2").arg(provider, status));
	label->setWordWrap(true);
	label->setStyleSheet(ready ? "padding: 6px 8px; border-left: 3px solid #35b76f;"
				   : "padding: 6px 8px; border-left: 3px solid #727a86; opacity: 0.78;");
	return label;
}

RsMusicSettings::RsMusicSettings(RsMusicState *state, RsMusicController *controller, RsMusicTwitchAuth *streamerAuth, RsMusicTwitchAuth *botAuth,
				 QWidget *parent)
	: QWidget(parent),
	  m_state(state),
	  m_controller(controller),
	  m_streamerAuth(streamerAuth),
	  m_botAuth(botAuth)
{
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(8, 8, 8, 8);
	layout->setSpacing(10);

	layout->addWidget(makeTitle("Music — Providers"));

	auto *edition = new QLabel(QString("Suite edition: %1")
				   .arg(RsEntitlements::edition() == RsProductEdition::Pro ? "Pro" : "Free"));
	edition->setStyleSheet("font-weight: bold;");
	layout->addWidget(edition);

	auto *providerDesc = new QLabel(
		"Music uses one shared queue and overlay format while each provider supplies the controls it supports. "
		"Connections will become available here as their integrations are completed.");
	providerDesc->setWordWrap(true);
	providerDesc->setStyleSheet("opacity: 0.8;");
	layout->addWidget(providerDesc);

	layout->addWidget(makeProviderStatus("YouTube / YouTube Music", "Playback foundation ready; search setup pending", true));
	layout->addWidget(makeProviderStatus("Spotify", "External-player connection not configured", false));
	layout->addWidget(makeProviderStatus("Local files", "Available — manage your library in Playlist", true));
	layout->addWidget(makeProviderStatus("Desktop media", "Player detection not configured", false));

	layout->addWidget(makeTitle("Music — YouTube Test"));
	auto *youtubeHint = new QLabel(
		"Paste a YouTube or YouTube Music video link to test playback in the Suite companion player. "
		"Playlist importing and search requests are added after this playback test is stable.");
	youtubeHint->setWordWrap(true);
	youtubeHint->setStyleSheet("opacity: 0.8;");
	layout->addWidget(youtubeHint);
	auto *youtubeUrl = new QLineEdit();
	youtubeUrl->setPlaceholderText("https://www.youtube.com/watch?v=...");
	youtubeUrl->setText(QSettings("RearSilver", "RearSilver-Stream-Suite").value("music/youtube/testUrl").toString());
	youtubeUrl->setMinimumWidth(0);
	layout->addWidget(youtubeUrl);
	auto *youtubePlay = new QPushButton("Play video in Suite media player");
	layout->addWidget(youtubePlay);
	auto *youtubeStatus = new QLabel();
	youtubeStatus->setWordWrap(true);
	youtubeStatus->setStyleSheet("opacity: 0.75; font-size: 11px;");
	layout->addWidget(youtubeStatus);
	connect(youtubePlay, &QPushButton::clicked, this, [this, youtubeUrl, youtubeStatus]() {
		const QString url = youtubeUrl->text().trimmed();
		QSettings("RearSilver", "RearSilver-Stream-Suite").setValue("music/youtube/testUrl", url);
		if (!m_controller || !m_controller->actionPlayYouTubeVideo(url)) {
			youtubeStatus->setText("Enter a direct YouTube or YouTube Music video link.");
			return;
		}
		youtubeStatus->setText("Video sent to the Suite media player. The normal Now Playing controls now control it.");
	});

	layout->addWidget(makeTitle("Music — Twitch Chat"));

	// --- Streamer / Reader account ---
	auto *desc = new QLabel("Link your Twitch account to enable chat commands for music control.\n\n"
				"This account is used to read chat messages and determine viewer permissions "
				"(viewer, moderator, broadcaster).\n\n"
				"When you click Login, Twitch will open in your browser so you can approve access. "
				"This is a one-time setup — you will stay logged in between OBS sessions.");

	desc->setWordWrap(true);
	desc->setStyleSheet("opacity: 0.8;");
	layout->addWidget(desc);

	m_streamerStatus = new QLabel("Status: Not logged in");
	m_streamerStatus->setStyleSheet("opacity: 0.7;");
	layout->addWidget(m_streamerStatus);

	m_loginButton = new QPushButton("Login with Twitch");
	layout->addWidget(m_loginButton);

	m_reconnectButton = new QPushButton("Reconnect");
	layout->addWidget(m_reconnectButton);

	m_logoutButton = new QPushButton("Log out");
	layout->addWidget(m_logoutButton);

	// --- Bot / Sender account (optional) ---
	layout->addWidget(makeTitle("Music — Twitch Bot (Optional)"));

	auto *botDesc = new QLabel("Optionally connect a separate Twitch bot account.\n\n"
				   "This account is only used to send chat responses and confirmations. "
				   "It never reads chat and does not affect permissions.");

	botDesc->setWordWrap(true);
	botDesc->setStyleSheet("opacity: 0.8;");
	layout->addWidget(botDesc);

	m_botStatus = new QLabel("Status: Not logged in");
	m_botStatus->setStyleSheet("opacity: 0.7;");
	layout->addWidget(m_botStatus);

	m_botLoginButton = new QPushButton("Login bot account");
	layout->addWidget(m_botLoginButton);

	m_botReconnectButton = new QPushButton("Reconnect bot");
	layout->addWidget(m_botReconnectButton);

	m_botLogoutButton = new QPushButton("Log out bot");
	layout->addWidget(m_botLogoutButton);

	connect(m_botLoginButton, &QPushButton::clicked, m_botAuth, &RsMusicTwitchAuth::beginDeviceAuth);

	connect(m_botLogoutButton, &QPushButton::clicked, m_botAuth, &RsMusicTwitchAuth::clearAuth);

	connect(m_botReconnectButton, &QPushButton::clicked, m_botAuth, &RsMusicTwitchAuth::reconnect);

	connect(m_botAuth, &RsMusicTwitchAuth::authCompleted, this, &RsMusicSettings::updateAuthUi);

	connect(m_botAuth, &RsMusicTwitchAuth::loggedOut, this, &RsMusicSettings::updateAuthUi);

	// --- Chat sender selection ---
	m_senderLabel = new QLabel("Send chat confirmations from:");
	m_senderLabel->setStyleSheet("margin-top: 12px; font-weight: bold;");
	layout->addWidget(m_senderLabel);

	m_senderStreamerRadio = new QRadioButton("Streamer account");
	m_senderBotRadio = new QRadioButton("Bot account");
	m_senderStreamerRadio->setToolTip("Send confirmations using the connected streamer identity.");
	m_senderBotRadio->setToolTip("Send confirmations using the connected bot identity.");

	layout->addWidget(m_senderStreamerRadio);
	layout->addWidget(m_senderBotRadio);

	// Load saved sender preference
	const bool sendFromBot = loadSendFromBotSetting();
	m_senderBotRadio->setChecked(sendFromBot);
	m_senderStreamerRadio->setChecked(!sendFromBot);

	layout->addStretch(1);

	connect(m_loginButton, &QPushButton::clicked, m_streamerAuth, &RsMusicTwitchAuth::beginDeviceAuth);
	connect(m_logoutButton, &QPushButton::clicked, m_streamerAuth, &RsMusicTwitchAuth::clearAuth);

	connect(m_reconnectButton, &QPushButton::clicked, m_streamerAuth, &RsMusicTwitchAuth::reconnect);

	connect(m_streamerAuth, &RsMusicTwitchAuth::authCompleted, this, &RsMusicSettings::updateAuthUi);
	connect(m_streamerAuth, &RsMusicTwitchAuth::loggedOut, this, &RsMusicSettings::updateAuthUi);

	connect(m_streamerAuth, &RsMusicTwitchAuth::authFailed, this, [this](const QString &err) {
		m_streamerStatus->setText("Status: Login failed");
		Q_UNUSED(err);
	});

	// Persist sender selection changes
	connect(m_senderStreamerRadio, &QRadioButton::toggled, this, [this](bool checked) {
		if (checked) {
			saveSendFromBotSetting(false);
			emit senderPreferenceChanged();
		}
	});

	connect(m_senderBotRadio, &QRadioButton::toggled, this, [this](bool checked) {
		if (checked) {
			saveSendFromBotSetting(true);
			emit senderPreferenceChanged();
		}
	});

	updateAuthUi();
}

void RsMusicSettings::updateAuthUi()
{
	// ---- Streamer ----
	const bool streamerLoggedIn = m_streamerAuth && m_streamerAuth->hasValidToken();

	if (m_streamerStatus) {
		if (streamerLoggedIn) {
			m_streamerStatus->setText(QString("Status: Logged in as %1").arg(m_streamerAuth->userLogin()));
		} else {
			m_streamerStatus->setText("Status: Not logged in");
		}
	}

	if (m_loginButton)
		m_loginButton->setVisible(!streamerLoggedIn);
	if (m_reconnectButton)
		m_reconnectButton->setVisible(streamerLoggedIn);
	if (m_logoutButton)
		m_logoutButton->setVisible(streamerLoggedIn);

	// ---- Bot ----
	const bool botLoggedIn = m_botAuth && m_botAuth->hasValidToken();

	// ---- Sender toggle availability ----
	if (m_senderBotRadio)
		m_senderBotRadio->setEnabled(botLoggedIn);

	// Auto-fallback if bot becomes unavailable
	if (!botLoggedIn && m_senderStreamerRadio)
		m_senderStreamerRadio->setChecked(true);


	if (m_botStatus) {
		if (botLoggedIn) {
			m_botStatus->setText(QString("Status: Logged in as %1").arg(m_botAuth->userLogin()));
		} else {
			m_botStatus->setText("Status: Not logged in");
		}
	}

	if (m_botLoginButton)
		m_botLoginButton->setVisible(!botLoggedIn);
	if (m_botReconnectButton)
		m_botReconnectButton->setVisible(botLoggedIn);
	if (m_botLogoutButton)
		m_botLogoutButton->setVisible(botLoggedIn);
}
