#include "rs_main_dock.hpp"

#include <QWidget>
#include <QVBoxLayout>
#include <QStackedWidget>
#include <QSplitter>
#include <QSplitterHandle>
#include <QToolButton>
#include <QComboBox>
#include <QTabBar>
#include <QLabel>
#include <QFrame>
#include <QMainWindow>
#include <QAction>
#include <QApplication>
#include <QScrollArea>
#include <QSettings>

#include <obs-frontend-api.h>
#include "enhancements/rs_auto_start.hpp"
#include "rs_music/rs_music.hpp"
#include "rs_music/state/rs_music_state.hpp"
#include "rs_music/rs_music_controller.hpp"
#include "rs_music/rs_music_command_router.hpp"
#include "rs_music/rs_music_twitch_irc_reader.hpp"
#include "rs_music/rs_music_twitch_irc_sender.hpp"
#include "rs_music/rs_music_twitch_auth.hpp"

// Set Twitch status dot colour
static void RsSetTwitchDot(QLabel *dot, const char *hexColour)
{
	if (!dot)
		return;
	dot->setStyleSheet(QString("color: %1;").arg(hexColour));
}

// Update Twitch status label
void RsMainDock::updateTwitchStatus(QLabel *label, const QString &accountName, const QString &state,
				    const QString &detail)
{
	if (!label)
		return;

	// state: "disconnected", "connecting", "connected", "error"
	label->setProperty("state", state);

	QString text = QString("● %1 — ").arg(accountName);

	if (state == "connected") {
		text += detail.isEmpty() ? "Connected" : QString("Connected as %1").arg(detail);
	} else if (state == "connecting") {
		text += "Connecting…";
	} else if (state == "error") {
		text += "Connection error";
	} else {
		text += "Not logged in";
	}

	label->setText(text);

	label->style()->unpolish(label);
	label->style()->polish(label);
}


// --- Stack that sizes to the CURRENT page, not the largest page ---
class RsCurrentSizeStack : public QStackedWidget {
public:
	using QStackedWidget::QStackedWidget;

	QSize sizeHint() const override
	{
		if (QWidget *w = currentWidget())
			return w->sizeHint();
		return QStackedWidget::sizeHint();
	}

	QSize minimumSizeHint() const override
	{
		if (QWidget *w = currentWidget())
			return w->minimumSizeHint();
		return QStackedWidget::minimumSizeHint();
	}
};

/* -------------------------------------------------------
 * Constructor — now a QWidget, OBS wraps it as a dock
 * ------------------------------------------------------*/
RsMainDock::RsMainDock(QWidget *parent) : QWidget(parent)
{
	setObjectName("RearSilverStreamSuiteDock");

	RsAutoStart::ensureObsEventHook(); // ✅ EARLY registration

	m_central = new QWidget(this);

	// 🔒 Long-press timer (FIXED LOCATION)
	m_stopHoldTimer = new QTimer(this);
	m_stopHoldTimer->setSingleShot(true);
	m_stopHoldTimer->setInterval(1500); // 1.5s hold

	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(0, 0, 0, 0);
	root->setSpacing(0);
	root->addWidget(m_central);

	loadSettings();

// Create ONE authoritative music state for the entire dock
	m_musicState = new RsMusicState(this);

	// Music controller (shared action surface)
	m_musicController = new RsMusicController(m_musicState, this);

	// --------------------------------------------------
	// Twitch auth ownership (Streamer + Bot)
	// MUST exist before createUi() so settings can bind to them
	// --------------------------------------------------
	m_streamerAuth = new RsMusicTwitchAuth("music/twitch/streamer", this);
	m_botAuth = new RsMusicTwitchAuth("music/twitch/bot", this);
	m_musicCommandRouter = new RsMusicCommandRouter(m_musicController, this);
	m_musicIrcReader = new RsMusicTwitchIrcReader(this);
	m_musicIrcSender = new RsMusicTwitchIrcSender(this);

	connect(m_musicIrcReader, &RsMusicTwitchIrcReader::chatMessageReceived, this,
		[this](const RsMusicChatMessage &message) {
			RsMusicChatContext context;
			context.userId = message.userId;
			context.displayName = message.displayName;
			context.isMod = message.isMod;
			context.isBroadcaster = message.isBroadcaster;
			m_musicCommandRouter->ingestChatMessage(context, message.message);
		});
	connect(m_musicCommandRouter, &RsMusicCommandRouter::feedbackMessage, m_musicIrcSender,
		&RsMusicTwitchIrcSender::sendMessage);
	connect(m_streamerAuth, &RsMusicTwitchAuth::authCompleted, this, &RsMainDock::connectMusicChat);
	connect(m_botAuth, &RsMusicTwitchAuth::authCompleted, this, &RsMainDock::connectMusicChat);
	connect(m_streamerAuth, &RsMusicTwitchAuth::loggedOut, m_musicIrcReader, &RsMusicTwitchIrcReader::disconnect);
	connect(m_streamerAuth, &RsMusicTwitchAuth::loggedOut, m_musicIrcSender, &RsMusicTwitchIrcSender::disconnect);
	connect(m_botAuth, &RsMusicTwitchAuth::loggedOut, m_musicIrcSender, &RsMusicTwitchIrcSender::disconnect);

	// Auto-resume if tokens already exist
	if (m_streamerAuth->hasValidToken())
		m_streamerAuth->reconnect();

	if (m_botAuth->hasValidToken())
		m_botAuth->reconnect();

	// Build UI AFTER auth exists
	createUi();

	// --------------------------------------------------
	// Streamer auth → GLOBAL STATUS BAR (CONNECTED)
	// --------------------------------------------------
	connect(m_streamerAuth, &RsMusicTwitchAuth::authCompleted, this, [this]() {
		// GREEN = authenticated & usable
		if (m_lblStreamerDot)
			m_lblStreamerDot->setStyleSheet("color: #00C853;");

		if (m_lblStreamerText) {
			const QString name = m_streamerAuth->userLogin();
			m_lblStreamerText->setText(name.isEmpty() ? "Streamer: Connected"
								  : QString("Streamer: %1").arg(name));
			m_lblStreamerText->setToolTip("Streamer account: Connected");
		}
	});

	// --------------------------------------------------
	// Streamer auth → global status indicator (dot + text)
	// --------------------------------------------------
	if (m_lblStreamerDot && m_lblStreamerText) {

				// STARTING: connecting (before device flow begins)
		connect(m_streamerAuth, &RsMusicTwitchAuth::connecting, this, [this]() {
			RsSetTwitchDot(m_lblStreamerDot, "#FFA500"); // orange
			m_lblStreamerText->setToolTip("Streamer account: Connecting…");
		});

		// START: connecting (device flow begins)
		connect(m_streamerAuth, &RsMusicTwitchAuth::deviceCodeReady, this,
			[this](const QString &, const QString &) {
				RsSetTwitchDot(m_lblStreamerDot, "#FFA500"); // orange
				m_lblStreamerText->setToolTip("Streamer account: Connecting…");
			});

		// DONE: connected
		connect(m_streamerAuth, &RsMusicTwitchAuth::authCompleted, this, [this]() {
			RsSetTwitchDot(m_lblStreamerDot, "#00C853"); // green
			const QString name = m_streamerAuth->userLogin();
			m_lblStreamerText->setToolTip(name.isEmpty()
							      ? "Streamer account: Connected"
							      : QString("Streamer account: Connected as %1").arg(name));
		});

		// LOGOUT: disconnected
		connect(m_streamerAuth, &RsMusicTwitchAuth::loggedOut, this, [this]() {
			RsSetTwitchDot(m_lblStreamerDot, "#FF3B30"); // red
			m_lblStreamerText->setToolTip("Streamer account: Not connected");
		});

		// FAIL: disconnected (error still maps to red per your rules)
		connect(m_streamerAuth, &RsMusicTwitchAuth::authFailed, this, [this](const QString &) {
			RsSetTwitchDot(m_lblStreamerDot, "#FF3B30"); // red
			m_lblStreamerText->setToolTip("Streamer account: Connection error");
		});

	}

// --------------------------------------------------
	// Bot auth → global status indicator (dot + text)
	// --------------------------------------------------
	if (m_lblBotDot && m_lblBotText) {

		// STARTING: connecting (before device flow begins)	
		connect(m_botAuth, &RsMusicTwitchAuth::connecting, this, [this]() {
			RsSetTwitchDot(m_lblBotDot, "#FFA500"); // orange
			m_lblBotText->setToolTip("Bot account: Connecting…");
		});

		// START: connecting (device flow begins)
		connect(m_botAuth, &RsMusicTwitchAuth::deviceCodeReady, this, [this](const QString &, const QString &) {
			RsSetTwitchDot(m_lblBotDot, "#FFA500"); // orange
			m_lblBotText->setToolTip("Bot account: Connecting…");
		});

		// DONE: connected
		connect(m_botAuth, &RsMusicTwitchAuth::authCompleted, this, [this]() {
			m_botAuthResolved = true;
			RsSetTwitchDot(m_lblBotDot, "#00C853"); // green
			const QString name = m_botAuth->userLogin();
			m_lblBotText->setText(name.isEmpty() ? "Bot: Connected" : QString("Bot: %1").arg(name));
			m_lblBotText->setToolTip("Bot account: Connected");
		});

		// LOGOUT: disconnected
		connect(m_botAuth, &RsMusicTwitchAuth::loggedOut, this, [this]() {
			RsSetTwitchDot(m_lblBotDot, "#FF3B30"); // red
			m_lblBotText->setToolTip("Bot account: Not connected");
		});

		// FAIL: disconnected (error maps to red per your rules)
		connect(m_botAuth, &RsMusicTwitchAuth::authFailed, this, [this](const QString &) {
			RsSetTwitchDot(m_lblBotDot, "#FF3B30"); // red
			m_lblBotText->setToolTip("Bot account: Connection error");
		});
	}
	
	// --------------------------------------------------
	// Startup sync: force theme dropdown to reflect
	// the already-loaded m_currentTheme
	// --------------------------------------------------
	if (m_themeCombo) {
		const int index = m_themeCombo->findData(m_currentTheme);
		if (index >= 0) {
			QSignalBlocker blocker(m_themeCombo); // prevent onThemeChanged firing
			m_themeCombo->setCurrentIndex(index);
		}
	}

	applyTheme();

	// --------------------------------------------------
	// Twitch status AFTER theme (theme wipes colours)
	// --------------------------------------------------
	if (m_lblStreamerDot && m_streamerAuth) {
		if (m_streamerAuth->hasValidToken()) {
			// Token exists → reconnecting
			RsSetTwitchDot(m_lblStreamerDot, "#FFA500"); // orange
			m_lblStreamerText->setText("Streamer — Reconnecting…");
		} else {
			// No token → logged out
			RsSetTwitchDot(m_lblStreamerDot, "#FF3B30"); // red
			m_lblStreamerText->setText("Streamer — Not connected");
		}
	}

if (m_lblBotDot && m_botAuth && !m_botAuthResolved) {
		if (m_botAuth->hasValidToken()) {
			RsSetTwitchDot(m_lblBotDot, "#FFA500"); // orange
			m_lblBotText->setText("Bot — Reconnecting…");
		} else {
			RsSetTwitchDot(m_lblBotDot, "#FF3B30"); // red
			m_lblBotText->setText("Bot — Not connected");
		}
	}

	obs_frontend_add_event_callback(RsMainDock::onFrontendEvent, this);

	updateControlStates();
	updateSceneSourceInfo();
	updateMusicStatusInfo();


	updateEffectiveLayout();
	showControls();

}

void RsMainDock::connectMusicChat()
{
	if (!m_streamerAuth || !m_streamerAuth->hasValidToken() || m_streamerAuth->userLogin().isEmpty())
		return;

	const QString channel = m_streamerAuth->userLogin();
	m_musicIrcReader->connectToChat(channel, m_streamerAuth->accessToken(), channel);

	QSettings settings("RearSilver", "RearSilver-Stream-Suite");
	const bool sendFromBot = settings.value("music/twitch/send_from_bot", false).toBool();

	if (sendFromBot && m_botAuth && m_botAuth->hasValidToken() && !m_botAuth->userLogin().isEmpty()) {
		m_musicIrcSender->connectSender(m_botAuth->userLogin(), m_botAuth->accessToken(), channel);
	} else {
		m_musicIrcSender->connectSender(channel, m_streamerAuth->accessToken(), channel);
	}
}

RsMainDock::~RsMainDock()
{
	obs_frontend_remove_event_callback(RsMainDock::onFrontendEvent, this);
}

/* -------------------------------------------------------
 * UI Construction
 * ------------------------------------------------------*/
void RsMainDock::createUi()
{
	auto *root = new QVBoxLayout(m_central);
	root->setContentsMargins(0, 0, 0, 0);
	root->setSpacing(0);

	// NAVIGATION CARD
	m_navCard = new QWidget();
	m_navCard->setObjectName("rs-nav-card");
	// Nav block should NOT stretch its contents; scroll area handles overflow
	m_navCard->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
	m_navCard->setMinimumHeight(0);

	m_sidebarLayout = new QVBoxLayout(m_navCard);
	m_sidebarLayout->setContentsMargins(4, 4, 4, 4);
	m_sidebarLayout->setSpacing(4);

	// TAB BAR
	m_tabBar = new QTabBar();
	m_tabBar->addTab("SYSTEM");
	m_tabBar->addTab("ENHANCEMENTS");
	m_tabBar->addTab("Music");
	m_tabBar->setExpanding(true);
	connect(m_tabBar, &QTabBar::currentChanged, this, &RsMainDock::onTabChanged);
	m_sidebarLayout->addWidget(m_tabBar);

	// DIVIDER (theme-aware)
	QFrame *divider = new QFrame();
	divider->setObjectName("rs-divider");
	divider->setProperty("class", "rs-divider");
	m_sidebarLayout->addSpacing(4);
	m_sidebarLayout->addWidget(divider);
	m_sidebarLayout->addSpacing(4);

	// ----------------------------------------------------
	// Twitch Connection Status Bar (always visible)
	// ----------------------------------------------------
	m_twitchStatusBar = new QWidget();
	m_twitchStatusBar->setObjectName("rs-twitch-status-bar");

	// Visual strength comes from theme, not hard-coded colours
	m_twitchStatusBar->setStyleSheet("border-radius: 4px;");

	auto *twitchStatusLayout = new QHBoxLayout(m_twitchStatusBar);
	twitchStatusLayout->setContentsMargins(8, 4, 8, 4);
	twitchStatusLayout->setSpacing(12);

// Streamer dot + text (dot colour is universal; text is theme-aware)
	m_lblStreamerDot = new QLabel("●");
	m_lblStreamerDot->setObjectName("rs-twitch-streamer-dot");

	m_lblStreamerText = new QLabel("Streamer");
	m_lblStreamerText->setObjectName("rs-twitch-streamer-text");
	m_lblStreamerText->setToolTip("Streamer account: Not connected");

	// Bot dot + text
	m_lblBotDot = new QLabel("●");
	m_lblBotDot->setObjectName("rs-twitch-bot-dot");

	m_lblBotText = new QLabel("Bot");
	m_lblBotText->setObjectName("rs-twitch-bot-text");
	m_lblBotText->setToolTip("Bot account: Not connected");

	twitchStatusLayout->addWidget(m_lblStreamerDot);
	twitchStatusLayout->addWidget(m_lblStreamerText);
	twitchStatusLayout->addSpacing(10);
	twitchStatusLayout->addWidget(m_lblBotDot);
	twitchStatusLayout->addWidget(m_lblBotText);
	twitchStatusLayout->addStretch(1);

	m_sidebarLayout->addWidget(m_twitchStatusBar);
	m_twitchStatusBar->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
	m_twitchStatusBar->setMaximumHeight(m_twitchStatusBar->sizeHint().height());

	m_sidebarLayout->addSpacing(4);

	// ----------------------------------------------------
	// Scene Info Bar (always visible)
	// ----------------------------------------------------
	m_infoBar = new QWidget();
	m_infoBar->setObjectName("rs-info-bar");

	m_infoBar->setStyleSheet("background-color: rgba(255,255,255,0.06);"
				 "border-radius: 4px;");

	auto *infoLayout = new QHBoxLayout(m_infoBar);
	infoLayout->setContentsMargins(8, 4, 8, 4);
	infoLayout->setSpacing(8);
	infoLayout->setStretch(0, 0);

	// CREATE BOTH LABELS BEFORE USING THEM
	m_lblScene = new QLabel("Scene: -");
	m_lblSource = new QLabel(""); // ← dummy but must exist

	m_lblScene->setStyleSheet("font-size: 15px; font-weight: bolder;");
	// Don't style m_lblSource since you're not using it, but it prevents a crash

	infoLayout->addWidget(m_lblScene);

	m_sidebarLayout->addWidget(m_infoBar);
	m_infoBar->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
	m_infoBar->setMaximumHeight(m_infoBar->sizeHint().height());

		// ----------------------------------------------------
	// Music Status Bar (always visible)
	// ----------------------------------------------------
	m_musicInfoBar = new QWidget();
	m_musicInfoBar->setObjectName("rs-music-info-bar");
	m_musicInfoBar->setStyleSheet("background-color: rgba(255,255,255,0.04);"
				      "border-radius: 4px;");

	auto *musicInfoLayout = new QHBoxLayout(m_musicInfoBar);
	musicInfoLayout->setContentsMargins(8, 4, 8, 4);
	musicInfoLayout->setSpacing(8);

	m_lblMusicStatus = new QLabel("Music: —");
	m_lblMusicStatus->setStyleSheet("font-size: 12px; opacity: 0.85;");

	musicInfoLayout->addWidget(m_lblMusicStatus);

	m_sidebarLayout->addWidget(m_musicInfoBar);
	m_musicInfoBar->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
	m_musicInfoBar->setMaximumHeight(m_musicInfoBar->sizeHint().height());


	{
		m_sidebarLayout->addSpacing(8);
		auto *divider = new QFrame();
		divider->setObjectName("rs-divider");
		divider->setProperty("class", "rs-divider");
		m_sidebarLayout->addWidget(divider);
		m_sidebarLayout->addSpacing(8);
	}

// MENU STACK
	m_menuStack = new QStackedWidget(m_navCard);

	// Give these a parent so lifetime is clean and predictable
	m_systemMenu = new QWidget(m_navCard);
	m_systemMenuLayout = new QVBoxLayout(m_systemMenu);

	m_enhMenu = new QWidget(m_navCard);
	m_enhMenuLayout = new QGridLayout(m_enhMenu);
	m_enhMenuLayout->setHorizontalSpacing(6);
	m_enhMenuLayout->setVerticalSpacing(6);
	m_enhMenuLayout->setContentsMargins(0, 0, 0, 0);

	// --------------------
	// MUSIC menu (NEW)
	// --------------------
	m_musicMenu = new QWidget(m_navCard);
	m_musicMenuLayout = new QGridLayout(m_musicMenu);
	m_musicMenuLayout->setContentsMargins(0, 0, 0, 0);
	m_musicMenuLayout->setHorizontalSpacing(6);
	m_musicMenuLayout->setVerticalSpacing(6);

	// IMPORTANT: only add AFTER creation, and only once
	m_menuStack->addWidget(m_systemMenu); // index 0
	m_menuStack->addWidget(m_enhMenu);    // index 1
	m_menuStack->addWidget(m_musicMenu);  // index 2

	m_sidebarLayout->addWidget(m_menuStack);
	m_menuStack->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);

	m_sidebarLayout->addSpacing(4);

	// CONTENT AREA
	m_contentCard = new QWidget();
	m_contentCard->setObjectName("rs-content-card");
	// 🔒 Prevent content from forcing dock width
	m_contentCard->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
	m_contentCard->setMinimumWidth(300);

	m_contentLayout = new QVBoxLayout(m_contentCard);
	m_contentLayout->setContentsMargins(8, 8, 8, 8);
	m_contentLayout->setSpacing(6);

m_stack = new RsCurrentSizeStack();
	m_contentLayout->addWidget(m_stack);

	connect(m_stack, &QStackedWidget::currentChanged, this, [this]() {
		m_stack->updateGeometry();
		if (m_contentCard) {
			m_contentCard->updateGeometry();
			m_contentCard->adjustSize();
		}
	});



// SPLITTER
	m_splitter = new QSplitter(Qt::Vertical);
	m_splitter->setChildrenCollapsible(false);
	m_splitter->setMinimumWidth(350);

	// Top container exists (required by rs_layout.cpp)
	m_topContainer = new QWidget();

	// Bottom container (content)
	m_bottomContainer = new QWidget();

// Wrap nav in a scroll area so the dock can be made shorter without losing access
	auto *navScroll = new QScrollArea(m_topContainer);
	navScroll->setObjectName("rs-nav-scroll");
	navScroll->setWidgetResizable(true);
	navScroll->setFrameShape(QFrame::NoFrame);
	navScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	navScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	navScroll->setWidget(m_navCard);

	auto *topLayout = new QVBoxLayout(m_topContainer);
	topLayout->setContentsMargins(0, 0, 0, 0);
	// Hard stop: top container must never shrink below menu UI
	topLayout->addWidget(navScroll);

auto *bottomLayout = new QVBoxLayout(m_bottomContainer);
	bottomLayout->setContentsMargins(0, 0, 0, 0);

	 auto *contentScroll = new QScrollArea(m_bottomContainer);
	contentScroll->setWidgetResizable(true);
	contentScroll->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

	contentScroll->setFrameShape(QFrame::NoFrame);
	contentScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	contentScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	contentScroll->setWidget(m_contentCard);

	bottomLayout->addWidget(contentScroll);

m_splitter->addWidget(m_topContainer);
	m_splitter->addWidget(m_bottomContainer);

	// We want ONLY the bottom content to be resizable.
	// The user drags handle #2 (between bottom content and spacer).
	m_splitter->setStretchFactor(0, 0); // top keeps its minimum; can grow if dock grows (blank space)
	m_splitter->setStretchFactor(1, 1); // bottom is the main resizable area

	// Make resizing feel smooth (optional but helps)
	m_splitter->setOpaqueResize(true);

	root->addWidget(m_splitter);

	// Build panels & menu
	createSidebarMenus();
	createPanels();

	// HARD STOP (correct timing): now that menus exist, lock the minimum top height
	const int navMin = m_navCard->sizeHint().height();
	m_topContainer->setMinimumHeight(navMin);

	// Set initial splitter position: handle sits directly under the menu block
	m_splitter->setSizes({navMin, 1});

	if (m_musicState) {
		connect(m_musicState, &RsMusicState::stateChanged, this, &RsMainDock::updateMusicStatusInfo);
	}
}

/* -------------------------------------------------------
 * Event Handling
 * ------------------------------------------------------*/
void RsMainDock::onFrontendEvent(obs_frontend_event event, void *data)
{
	auto *dock = static_cast<RsMainDock *>(data);
	if (!dock)
		return;

	switch (event) {
	// Scene changes we care about
	case OBS_FRONTEND_EVENT_SCENE_CHANGED:
	case OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED:
	case OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED:
		dock->updateSceneSourceInfo();
		break;

	// Control states
	case OBS_FRONTEND_EVENT_STREAMING_STARTED:
	case OBS_FRONTEND_EVENT_STREAMING_STOPPED:
	case OBS_FRONTEND_EVENT_RECORDING_STARTED:
	case OBS_FRONTEND_EVENT_RECORDING_STOPPED:
	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STARTED:
	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STOPPED:
	case OBS_FRONTEND_EVENT_VIRTUALCAM_STARTED:
	case OBS_FRONTEND_EVENT_VIRTUALCAM_STOPPED:
	case OBS_FRONTEND_EVENT_STUDIO_MODE_ENABLED:
	case OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED:
		dock->updateControlStates();
		break;

	default:
		break;
	}
}

/* -------------------------------------------------------
 * NEW — Update info bar (scene only for this OBS version)
 * ------------------------------------------------------*/
void RsMainDock::updateSceneSourceInfo()
{
	// Current scene
	obs_source_t *scene = obs_frontend_get_current_scene();
	if (scene) {
		m_lblScene->setText(QString("Scene: %1").arg(obs_source_get_name(scene)));
		obs_source_release(scene);
	} else {
		m_lblScene->setText("Scene: -");
	}
}

void RsMainDock::updateMusicStatusInfo()
{
	if (!m_lblMusicStatus)
		return;

	RsMusicState *state = m_musicState;
	if (!state) {
		m_lblMusicStatus->setText("Music: —");
		return;
	}

	// Status (always shown, even if no track yet)
	QString status;
	switch (state->playbackStatus()) {
	case RsMusicState::PlaybackStatus::Playing:
		status = "Playing";
		break;
	case RsMusicState::PlaybackStatus::Paused:
		status = "Paused";
		break;
	case RsMusicState::PlaybackStatus::Stopped:
	default:
		status = "Stopped";
		break;
	}

	// If no current track, still show status (this is the key fix)
	if (!state->hasCurrentTrack()) {
		m_lblMusicStatus->setText(QString("Music: %1").arg(status));
		return;
	}

	// Track details (only when available)
	const auto &track = state->currentTrack();

	QString source;
	if (track.isFromPlaylist) {
		source = QString("Playlist: %1").arg(state->playlistLabel());
	} else {
		source = QString("Requested by %1").arg(track.requestedBy);
	}

	m_lblMusicStatus->setText(
		QString("Music: %1 — %2 – %3 (%4)").arg(status).arg(track.artist).arg(track.title).arg(source));
}


/* -------------------------------------------------------
 * OBS Settings Launcher
 * ------------------------------------------------------*/
void RsMainDock::openNativeSettings()
{
	blog(LOG_INFO, "[RearSilver] Searching for OBS Settings QAction…");

	QWidget *mainWinWidget = reinterpret_cast<QWidget *>(obs_frontend_get_main_window());
	if (!mainWinWidget) {
		blog(LOG_WARNING, "[RearSilver] Could not get OBS main window.");
		return;
	}

	QMainWindow *mw = qobject_cast<QMainWindow *>(mainWinWidget);
	if (!mw) {
		blog(LOG_WARNING, "[RearSilver] OBS main window cast failed.");
		return;
	}

	for (QAction *act : mw->findChildren<QAction *>()) {
		if (!act)
			continue;

		QString txt = act->text().trimmed().toLower();
		if (txt == "settings" || txt == "&settings" || txt.contains("settings")) {
			blog(LOG_INFO, "[RearSilver] Found settings action — triggering!");
			act->trigger();
			return;
		}
	}

	blog(LOG_WARNING, "[RearSilver] Could not locate OBS Settings QAction.");
}
