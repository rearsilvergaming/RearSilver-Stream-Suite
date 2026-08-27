#include "rs_main_dock.hpp"

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QFont>
#include <QStackedWidget>
#include <QGridLayout>
#include <QTimer>
#include <QMainWindow>
#include <QAction>
#include <QScrollArea>
#include <QFrame>
#include <QCheckBox>
#include <QToolButton>
#include <QIcon>
#include <QPixmap>
#include <QPainter>
#include <QSizePolicy>

#include <utility>

#include "rs_scenes_sources.hpp"
#include "rs_stats.hpp"
#include "enhancements/rs_browser_refresh.hpp"
#include "enhancements/rs_auto_start.hpp"
#include "rs_music/rs_music_controller.hpp"

static QIcon makeStreamToolFallbackIcon(const QString &emoji)
{
	QPixmap pixmap(32, 32);
	pixmap.fill(Qt::transparent);
	QPainter painter(&pixmap);
	QFont font;
	font.setPointSize(18);
	painter.setFont(font);
	painter.setPen(Qt::white);
	painter.drawText(pixmap.rect(), Qt::AlignCenter, emoji);
	return QIcon(pixmap);
}

// MUSIC UI panels (NEW)
#include "rs_music/state/rs_music_state.hpp"

#include "rs_music/ui/rs_music_now_playing.hpp"
#include "rs_music/ui/rs_music_queue.hpp"
#include "rs_music/ui/rs_music_requests.hpp"
#include "rs_music/ui/rs_music_settings.hpp"
#include "rs_music/ui/rs_music_setup.hpp"
#include "rs_music/ui/rs_music_overlay.hpp"
#include "rs_music/rs_music_server.hpp"


// OBS
#include <obs-frontend-api.h>
#include <obs.h>
#include <util/platform.h>

void RsMainDock::createPanels()
{
	// --------------------
	// Shared Music State (authoritative)
	// Must use the ONE dock-owned state (Phase 6D rule)
	// --------------------
	RsMusicState *musicState = m_musicState;

	// --------------------
	// Controls Page
	// --------------------
	m_pageControls = new QWidget(m_contentCard);

	// Scroll area
	auto *scroll = new QScrollArea(m_pageControls);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);

	// Container inside scroll
	auto *container = new QWidget();
	auto *layout = new QVBoxLayout(container);
	layout->setContentsMargins(8, 8, 8, 8);
	layout->setSpacing(12);

	// Title
	auto *title = new QLabel("Controls");
	QFont tf = title->font();
	tf.setBold(true);
	tf.setPointSize(tf.pointSize() + 1);
	title->setFont(tf);
	layout->addWidget(title);

	// Micro section label (quiet hierarchy)
	auto addSectionLabel = [&](const QString &text) {
		auto *lbl = new QLabel(text);
		lbl->setObjectName("rs-section-label");
		layout->addWidget(lbl);
	};

	// Divider
	auto addDivider = [&]() {
		layout->addSpacing(10); // space ABOVE
		auto *d = new QFrame();
		d->setObjectName("rs-divider");
		layout->addWidget(d);
		layout->addSpacing(10); // space BELOW
	};

	// Streaming
	addSectionLabel("STREAMING");

	m_btnStartStream = makeObsButton("Start Streaming");
	m_btnStopStream = makeObsButton("Stop Streaming");
	connect(m_btnStartStream, &QPushButton::clicked, this, &RsMainDock::startStreaming);
	// STOP STREAM (hold-to-confirm)
	connect(m_btnStopStream, &QPushButton::pressed, this, [this]() {
		beginStopHold(m_btnStopStream, [this]() {
			if (obs_frontend_streaming_active())
				obs_frontend_streaming_stop();
			updateControlStates();
		});
	});

	connect(m_btnStopStream, &QPushButton::released, this, &RsMainDock::cancelStopHold);
	layout->addWidget(m_btnStartStream);
	layout->addWidget(m_btnStopStream);

	addDivider();

	// Recording
	addSectionLabel("RECORDING");

	m_btnStartRecord = makeObsButton("Start Recording");
	m_btnStopRecord = makeObsButton("Stop Recording");
	connect(m_btnStartRecord, &QPushButton::clicked, this, &RsMainDock::startRecording);
	connect(m_btnStopRecord, &QPushButton::pressed, this, [this]() {
		beginStopHold(m_btnStopRecord, [this]() {
			if (obs_frontend_recording_active())
				obs_frontend_recording_stop();
			updateControlStates();
		});
	});

	connect(m_btnStopRecord, &QPushButton::released, this, &RsMainDock::cancelStopHold);
	layout->addWidget(m_btnStartRecord);
	layout->addWidget(m_btnStopRecord);

	addDivider();

	// Replay Buffer
	addSectionLabel("REPLAY BUFFER");

	m_btnStartReplay = makeObsButton("Start Replay Buffer");
	m_btnStopReplay = makeObsButton("Stop Replay Buffer");
	connect(m_btnStartReplay, &QPushButton::clicked, this, &RsMainDock::startReplayBuffer);
	connect(m_btnStopReplay, &QPushButton::pressed, this, [this]() {
		beginStopHold(m_btnStopReplay, [this]() {
			if (obs_frontend_replay_buffer_active())
				obs_frontend_replay_buffer_stop();
			updateControlStates();
		});
	});

	connect(m_btnStopReplay, &QPushButton::released, this, &RsMainDock::cancelStopHold);
	layout->addWidget(m_btnStartReplay);
	layout->addWidget(m_btnStopReplay);

	addDivider();

	// Virtual Camera
	addSectionLabel("VIRTUAL CAMERA");

	m_btnStartVCam = makeObsButton("Start Virtual Camera");
	m_btnStopVCam = makeObsButton("Stop Virtual Camera");
	connect(m_btnStartVCam, &QPushButton::clicked, this, &RsMainDock::startVirtualCamera);
	connect(m_btnStopVCam, &QPushButton::pressed, this, [this]() {
		beginStopHold(m_btnStopVCam, [this]() {
			if (obs_frontend_virtualcam_active())
				obs_frontend_stop_virtualcam();
			updateControlStates();
		});
	});

	connect(m_btnStopVCam, &QPushButton::released, this, &RsMainDock::cancelStopHold);
	layout->addWidget(m_btnStartVCam);
	layout->addWidget(m_btnStopVCam);

	addDivider();

	// Studio Mode
	addSectionLabel("STUDIO MODE");

	m_btnStudioMode = makeObsButton("Studio Mode");
	connect(m_btnStudioMode, &QPushButton::clicked, this, &RsMainDock::toggleStudioMode);
	layout->addWidget(m_btnStudioMode);

	layout->addStretch();

	// Final wiring
	scroll->setWidget(container);

	auto *outer = new QVBoxLayout(m_pageControls);
	outer->setContentsMargins(0, 0, 0, 0);
	outer->addWidget(scroll);

	// Scenes & Sources
	m_pageScenesSources = new RsScenesSourcesPage(m_contentCard);

	// Stats (NEW modular version)
	m_pageStats = RsStats::createStatsPage(this, m_contentCard);

	// Browser Refresh (Enhancements tab)
	m_pageBrowserRefresh = RsBrowserRefresh::createPage(this, m_contentCard);

	// Auto Start (Enhancements tab)
	m_pageAutoStart = RsAutoStart::createPage(this, m_contentCard);

	// Stream Tools quick actions
	m_pageStreamToolsQuickActions = new QWidget(m_contentCard);
	{
		auto *layout = new QVBoxLayout(m_pageStreamToolsQuickActions);
		layout->setContentsMargins(8, 8, 8, 8);
		layout->setSpacing(4);

		auto makeButton = [&](const QString &text, const QString &tooltip, const QString &iconName,
			const QString &fallbackEmoji) {
			auto *button = new QToolButton(m_pageStreamToolsQuickActions);
			button->setText(text);
			button->setToolTip(tooltip);
			button->setObjectName("SidebarButton");
			QIcon icon = QIcon::fromTheme(iconName);
			if (icon.isNull() || icon.availableSizes().isEmpty())
				icon = makeStreamToolFallbackIcon(fallbackEmoji);
			button->setIcon(icon);
			button->setIconSize(QSize(20, 20));
			button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
			button->setMinimumWidth(140);
			button->setFixedHeight(32);
			button->setMaximumWidth(QWIDGETSIZE_MAX);
			button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
			button->setAutoRaise(true);
			return button;
		};

		auto makeSection = [&](const QString &title, QLabel **heading = nullptr) {
			auto *section = new QWidget(m_pageStreamToolsQuickActions);
			auto *sectionLayout = new QVBoxLayout(section);
			sectionLayout->setContentsMargins(0, 2, 0, 2);
			sectionLayout->setSpacing(4);
			auto *label = new QLabel(title, section);
			QFont font = label->font();
			font.setBold(true);
			label->setFont(font);
			label->setContentsMargins(0, 0, 0, 2);
			label->setMinimumHeight(label->fontMetrics().height() + 4);
			sectionLayout->addWidget(label);
			auto *grid = new QGridLayout();
			grid->setContentsMargins(0, 0, 0, 0);
			grid->setHorizontalSpacing(6);
			grid->setVerticalSpacing(6);
			grid->setColumnStretch(0, 1);
			grid->setColumnStretch(1, 1);
			sectionLayout->addLayout(grid);
			if (heading) *heading = label;
			return std::pair<QWidget *, QGridLayout *>(section, grid);
		};

		auto *refreshCurrent = makeButton("Refresh browsers", "Refresh browser sources in the current scene", "view-refresh", "🔄");
		auto *refreshAll = makeButton("Refresh all browsers", "Refresh browser sources across every scene", "view-refresh", "🔄");
		auto *showReplay = makeButton("Show", "Show the managed Instant Replay layout", "view-visible", "👁");
		auto *hideReplay = makeButton("Hide", "Hide the managed Instant Replay layout", "view-hidden", "◉");
		auto *triggerReplay = makeButton("Save & play replay", "Save and play the current Replay Buffer clip", "media-playback-start", "🎬");
		m_btnStreamToolQuickTextShow = makeButton("Show message", "Show the current Hub Quick Text message", "view-visible", "💬");
		auto *hideQuickText = makeButton("Hide", "Hide Quick Text without clearing its configured message", "view-hidden", "◉");
		m_btnStreamToolTimerStart = makeButton("Start countdown", "Start the Hub-configured Timer or Countdown in the current scene", "chronometer", "⏱");
		m_btnStreamToolTimerPause = makeButton("Pause", "Pause or resume the current Timer or Countdown", "media-playback-pause", "⏯");
		auto *resetTimer = makeButton("Reset timer", "Reset the Timer to its Hub-configured duration", "edit-undo", "↺");
		auto *showTimer = makeButton("Show", "Show the managed Timer or Countdown overlay", "view-visible", "👁");
		auto *hideTimer = makeButton("Hide", "Hide the managed Timer or Countdown overlay", "view-hidden", "◉");

		QLabel *browserRefreshHeading = nullptr;
		auto browserSection = makeSection("Browser Refresh", &browserRefreshHeading);
		browserRefreshHeading->setObjectName("browserRefreshState");
		browserSection.second->addWidget(refreshCurrent, 0, 0);
		browserSection.second->addWidget(refreshAll, 0, 1);
		auto replaySection = makeSection("Instant Replay — waiting for Hub", &m_lblStreamToolReplay);
		replaySection.second->addWidget(showReplay, 0, 0);
		replaySection.second->addWidget(hideReplay, 0, 1);
		replaySection.second->addWidget(triggerReplay, 1, 0, 1, 2);
		auto quickTextSection = makeSection("Quick Text — waiting for Hub", &m_lblStreamToolQuickText);
		quickTextSection.second->addWidget(m_btnStreamToolQuickTextShow, 0, 0);
		quickTextSection.second->addWidget(hideQuickText, 0, 1);
		auto timerSection = makeSection("Countdown — waiting for Hub", &m_lblStreamToolTimer);
		timerSection.second->addWidget(showTimer, 0, 0);
		timerSection.second->addWidget(hideTimer, 0, 1);
		timerSection.second->addWidget(m_btnStreamToolTimerStart, 1, 0);
		timerSection.second->addWidget(m_btnStreamToolTimerPause, 1, 1);
		timerSection.second->addWidget(resetTimer, 2, 0, 1, 2);

		layout->addWidget(browserSection.first);
		layout->addWidget(replaySection.first);
		layout->addWidget(quickTextSection.first);
		layout->addWidget(timerSection.first);
		layout->addStretch();

		for (QToolButton *button : {showReplay, hideReplay, triggerReplay})
			button->setProperty("requiresReplayConfiguration", true);
		m_btnStreamToolQuickTextShow->setProperty("requiresQuickTextMessage", true);
		hideQuickText->setProperty("requiresQuickTextConfiguration", true);
		for (QToolButton *button : {m_btnStreamToolTimerStart, m_btnStreamToolTimerPause, resetTimer, showTimer, hideTimer})
			button->setProperty("requiresTimerConfiguration", true);

		connect(refreshCurrent, &QToolButton::clicked, this, []() { rsExecuteStreamToolQuickAction(RsStreamToolQuickAction::RefreshCurrentBrowsers); });
		connect(refreshAll, &QToolButton::clicked, this, []() { rsExecuteStreamToolQuickAction(RsStreamToolQuickAction::RefreshAllBrowsers); });
		connect(showReplay, &QToolButton::clicked, this, []() { rsExecuteStreamToolQuickAction(RsStreamToolQuickAction::ShowReplay); });
		connect(hideReplay, &QToolButton::clicked, this, []() { rsExecuteStreamToolQuickAction(RsStreamToolQuickAction::HideReplay); });
		connect(triggerReplay, &QToolButton::clicked, this, []() { rsExecuteStreamToolQuickAction(RsStreamToolQuickAction::TriggerReplay); });
		connect(m_btnStreamToolQuickTextShow, &QToolButton::clicked, this, []() { rsExecuteStreamToolQuickAction(RsStreamToolQuickAction::ShowQuickText); });
		connect(hideQuickText, &QToolButton::clicked, this, []() { rsExecuteStreamToolQuickAction(RsStreamToolQuickAction::HideQuickText); });
		connect(m_btnStreamToolTimerStart, &QToolButton::clicked, this, []() { rsExecuteStreamToolQuickAction(RsStreamToolQuickAction::StartTimer); });
		connect(m_btnStreamToolTimerPause, &QToolButton::clicked, this, []() { rsExecuteStreamToolQuickAction(RsStreamToolQuickAction::PauseTimer); });
		connect(resetTimer, &QToolButton::clicked, this, []() { rsExecuteStreamToolQuickAction(RsStreamToolQuickAction::ResetTimer); });
		connect(showTimer, &QToolButton::clicked, this, []() { rsExecuteStreamToolQuickAction(RsStreamToolQuickAction::ShowTimer); });
		connect(hideTimer, &QToolButton::clicked, this, []() { rsExecuteStreamToolQuickAction(RsStreamToolQuickAction::HideTimer); });
	}

	// UI Settings
	m_pageUiSettings = new QWidget(m_contentCard);
	{
		auto *layout = new QVBoxLayout(m_pageUiSettings);
		layout->setContentsMargins(8, 8, 8, 8);
		layout->setSpacing(10);

		auto *titleLbl = new QLabel("UI Settings");
		QFont f = titleLbl->font();
		f.setBold(true);
		f.setPointSize(f.pointSize() + 1);
		titleLbl->setFont(f);
		layout->addWidget(titleLbl);

		// Layout dropdown
		auto *layoutLbl = new QLabel("Dock layout mode:");
		m_layoutCombo = new QComboBox();
		m_layoutCombo->addItem("Auto", (int)LayoutMode::Auto);
		m_layoutCombo->addItem("Horizontal", (int)LayoutMode::Horizontal);
		m_layoutCombo->addItem("Vertical", (int)LayoutMode::Vertical);
		layout->addWidget(layoutLbl);
		layout->addWidget(m_layoutCombo);

		layout->addSpacing(8);

		// Theme dropdown
		auto *themeLbl = new QLabel("Theme:");
		m_themeCombo = new QComboBox();
		m_themeCombo->addItem("RearSilver (Default)", "brand");
		m_themeCombo->addItem("OBS Default", "default");
		m_themeCombo->addItem("Pro (Twitch Dark)", "twitch_dark");
		m_themeCombo->addItem("Pro (Calm)", "pro_calm");
		m_themeCombo->addItem("Pro (Night)", "pro_night");
		m_themeCombo->addItem("High Contrast", "high_contrast");
		m_themeCombo->addItem("Protanopia Safe", "protanopia");
		m_themeCombo->addItem("Deuteranopia Safe", "deuteranopia");
		m_themeCombo->addItem("Tritanopia Safe", "tritanopia");

		layout->addWidget(themeLbl);
		layout->addWidget(m_themeCombo);
		layout->addSpacing(16);
		auto *safetyLabel = new QLabel("Safety Lock");
		safetyLabel->setObjectName("rs-section-label");
		layout->addWidget(safetyLabel);
		auto *safetyToggle = new QCheckBox("Prevent accidental stop actions");
		safetyToggle->setChecked(m_safetyLockEnabled);
		safetyToggle->setToolTip("When enabled, stopping streaming or recording requires confirmation.");
		layout->addWidget(safetyToggle);

		auto *safetyHint =
			new QLabel("Adds an extra layer of protection against accidentally ending your stream.");
		safetyHint->setWordWrap(true);
		safetyHint->setStyleSheet("opacity: 0.7; font-size: 11px;");
		layout->addWidget(safetyHint);

		connect(safetyToggle, &QCheckBox::toggled, this, [this](bool enabled) {
			m_safetyLockEnabled = enabled;
			saveSettings();
			updateControlStates();
		});

		layout->addStretch();

		connect(m_layoutCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
			&RsMainDock::onLayoutModeChanged);

		connect(m_themeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
			&RsMainDock::onThemeChanged);
	}

	// --------------------
	// MUSIC skeleton pages (NEW)
	// --------------------
	m_pageMusicNowPlaying = new RsMusicNowPlaying(musicState, m_musicController, m_contentCard);
	m_pageMusicQueue = new RsMusicQueue(musicState, m_contentCard);
	m_pageMusicRequests = new RsMusicRequests(musicState, m_contentCard);
	auto *musicSettings = new RsMusicSettings(musicState, m_musicController, m_streamerAuth, m_botAuth, m_contentCard);
	m_pageMusicSettings = musicSettings;
	m_pageMusicSetup = new RsMusicSetup(m_contentCard);
	RsMusicServer::instance().start(musicState);
	m_pageMusicOverlay = new RsMusicOverlay(m_contentCard);

	// Stack registration (order determines page index)
	m_stack->addWidget(m_pageControls);       // 0
	m_stack->addWidget(m_pageScenesSources);  // 1
	m_stack->addWidget(m_pageStats);          // 2
	m_stack->addWidget(m_pageBrowserRefresh); // 3
	m_stack->addWidget(m_pageAutoStart);      // 4
	m_stack->addWidget(m_pageStreamToolsQuickActions); // 5
	m_stack->addWidget(m_pageUiSettings);     // 6

	// MUSIC (append only)
	m_stack->addWidget(m_pageMusicNowPlaying); // 7
	m_stack->addWidget(m_pageMusicQueue);      // 8
	m_stack->addWidget(m_pageMusicRequests);   // 9
	m_stack->addWidget(m_pageMusicSettings);   // 10
	m_stack->addWidget(m_pageMusicSetup);      // 11
	m_stack->addWidget(m_pageMusicOverlay);    // 12
	m_lastEnhancementsPage = m_stack->indexOf(m_pageStreamToolsQuickActions);

	applyTheme();
}
