#pragma once

#include <QWidget>
#include <QSplitter>
#include <QTimer>
#include <QPropertyAnimation>
#include <functional>
#include <obs-frontend-api.h>
#include "rs_music/state/rs_music_state.hpp"

class QWidget;
class QVBoxLayout;
class QHBoxLayout;
class QGridLayout;
class QStackedWidget;
class QToolButton;
class QComboBox;
class QTabBar;
class QPushButton;
class QResizeEvent;
class QLabel;
// Music chat / command plumbing
class RsMusicController;
class RsMusicCommandRouter;
class RsMusicTwitchIrcReader;
class RsMusicTwitchIrcSender;
class RsMusicTwitchAuth;


class RsMainDock : public QWidget {
	Q_OBJECT

public:
	explicit RsMainDock(QWidget *parent = nullptr);
	~RsMainDock() override;

protected:
	void resizeEvent(QResizeEvent *event) override;

private slots:
	// Page switching
	void showControls();
	void showScenesSources();
	void showStats();
	void showObsSettings() { openNativeSettings(); }

	void showBrowserRefresh();
	void showQuickText();
	void showInstantReplay();
	void showAutoStart();
	void showTimer();
	void showUiSettings();

	// MUSIC navigation slots
	void showMusicNowPlaying();
	void showMusicQueue();
	void showMusicRequests();
	void showMusicPlaylist();
	void showMusicSettings();

	// Layout & tabs
	void onLayoutModeChanged(int index);
	void onTabChanged(int index);

	// OBS control actions
	void startStreaming();
	void stopStreaming();
	void startRecording();
	void stopRecording();
	void startReplayBuffer();
	void stopReplayBuffer();
	void startVirtualCamera();
	void stopVirtualCamera();
	void toggleStudioMode();

	// Theme dropdown
	void onThemeChanged(int index);

	// OBS settings
	void openNativeSettings();

private:
	enum class LayoutMode { Auto = 0, Horizontal = 1, Vertical = 2 };

	void createUi();
	void createSidebarMenus();
	void createPanels();

	QWidget *makeTextCard(const QString &title, const QString &body);
	QPushButton *makeObsButton(const QString &text);
	QHBoxLayout *makeRow(QWidget *left, QWidget *right = nullptr);

	void updateEffectiveLayout();
	void applyOrientation();

	void loadSettings();
	void saveSettings();

	void applyTheme();
	void setActiveButton(QToolButton *button);

	// Info bar
	void updateSceneSourceInfo();
	void updateMusicStatusInfo();
	void connectMusicChat();

	// 🔒 Safety Lock helpers
	void beginStopHold(QPushButton *btn, std::function<void()> action);
	void cancelStopHold();

private:
	// Core containers
	QWidget *m_central = nullptr;

	RsMusicState *m_musicState = nullptr;
	// Music control & chat plumbing (Phase 6A)
	RsMusicController *m_musicController = nullptr;
	RsMusicCommandRouter *m_musicCommandRouter = nullptr;
	RsMusicTwitchIrcReader *m_musicIrcReader = nullptr;
	RsMusicTwitchIrcSender *m_musicIrcSender = nullptr;

// Twitch auth (owned by dock)
	RsMusicTwitchAuth *m_streamerAuth = nullptr;
	RsMusicTwitchAuth *m_botAuth = nullptr;
	bool m_streamerAuthResolved = false;
	bool m_botAuthResolved = false;

// Sidebar / navigation
	QWidget *m_navCard = nullptr;
	QVBoxLayout *m_sidebarLayout = nullptr;

	QTabBar *m_tabBar = nullptr;
	QStackedWidget *m_menuStack = nullptr;
	QWidget *m_systemMenu = nullptr;
	QVBoxLayout *m_systemMenuLayout = nullptr;
	QWidget *m_enhMenu = nullptr;
	QGridLayout *m_enhMenuLayout = nullptr;

	// MUSIC menu widget
	QWidget *m_musicMenu = nullptr;
	QGridLayout *m_musicMenuLayout = nullptr;

	QWidget *m_contentCard = nullptr;
	QVBoxLayout *m_contentLayout = nullptr;
	QStackedWidget *m_stack = nullptr;

	// Layout / theme
	LayoutMode m_layoutMode = LayoutMode::Auto;
	LayoutMode m_effectiveLayout = LayoutMode::Horizontal;

	QComboBox *m_layoutCombo = nullptr;
	QComboBox *m_themeCombo = nullptr;
	QString m_currentTheme = "default";

	// Remember last pages
	int m_lastSystemPage = 0;
	int m_lastEnhancementsPage = 0;
	int m_lastMusicPage = 9; // default to first music page

	// 🔒 Safety Lock
	bool m_safetyLockEnabled = false;

	// Long-press confirmation
	QTimer *m_stopHoldTimer = nullptr;
	QPushButton *m_pendingStopButton = nullptr;

	QSplitter *m_splitter = nullptr;
	QWidget *m_topContainer = nullptr;
	QWidget *m_bottomContainer = nullptr;
	QPropertyAnimation *m_holdAnim = nullptr;
	QWidget *m_holdOverlay = nullptr;

	// Info bar
	// Info bar
	QWidget *m_infoBar = nullptr;
	QLabel *m_lblScene = nullptr;
	QLabel *m_lblSource = nullptr;

	// Music status bar
	QWidget *m_musicInfoBar = nullptr;
	QLabel *m_lblMusicStatus = nullptr;

// ---- Twitch connection status (universal dot + theme-aware text) ----
	QWidget *m_twitchStatusBar = nullptr;

	// Streamer
	QLabel *m_lblStreamerDot = nullptr;  // universal colour
	QLabel *m_lblStreamerText = nullptr; // theme-controlled text

	// Bot
	QLabel *m_lblBotDot = nullptr;  // universal colour
	QLabel *m_lblBotText = nullptr; // theme-controlled text
	void updateTwitchStatus(QLabel *label, const QString &accountName, const QString &state,
				const QString &detail = QString());


	// Sidebar buttons
	QToolButton *m_btnControls = nullptr;
	QToolButton *m_btnScenesSources = nullptr;
	QToolButton *m_btnStats = nullptr;
	QToolButton *m_btnObsSettings = nullptr;

	QToolButton *m_btnBrowserRefresh = nullptr;
	QToolButton *m_btnQuickText = nullptr;
	QToolButton *m_btnInstantReplay = nullptr;
	QToolButton *m_btnAutoStart = nullptr;
	QToolButton *m_btnTimer = nullptr;
	QToolButton *m_btnUiSettings = nullptr;

	// MUSIC menu buttons
	QToolButton *m_btnMusicNowPlaying = nullptr;
	QToolButton *m_btnMusicQueue = nullptr;
	QToolButton *m_btnMusicRequests = nullptr;
	QToolButton *m_btnMusicPlaylist = nullptr;
	QToolButton *m_btnMusicSettings = nullptr;

	// Pages
	QWidget *m_pageControls = nullptr;
	QWidget *m_pageScenesSources = nullptr;
	QWidget *m_pageStats = nullptr;
	QWidget *m_pageBrowserRefresh = nullptr;
	QWidget *m_pageQuickText = nullptr;
	QWidget *m_pageInstantReplay = nullptr;
	QWidget *m_pageAutoStart = nullptr;
	QWidget *m_pageTimer = nullptr;
	QWidget *m_pageUiSettings = nullptr;

	// MUSIC pages (stack widgets)
	QWidget *m_pageMusicNowPlaying = nullptr;
	QWidget *m_pageMusicQueue = nullptr;
	QWidget *m_pageMusicRequests = nullptr;
	QWidget *m_pageMusicPlaylist = nullptr;
	QWidget *m_pageMusicSettings = nullptr;


	// Control buttons
	QPushButton *m_btnStartStream = nullptr;
	QPushButton *m_btnStopStream = nullptr;
	QPushButton *m_btnStartRecord = nullptr;
	QPushButton *m_btnStopRecord = nullptr;
	QPushButton *m_btnStartReplay = nullptr;
	QPushButton *m_btnStopReplay = nullptr;
	QPushButton *m_btnStartVCam = nullptr;
	QPushButton *m_btnStopVCam = nullptr;
	QPushButton *m_btnStudioMode = nullptr;

	// OBS state sync
	void updateControlStates();
	static void onFrontendEvent(obs_frontend_event event, void *data);
};
