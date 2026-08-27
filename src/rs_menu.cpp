#include "rs_main_dock.hpp"

#include "rs_music/rs_music_controller.hpp"

#include <QToolButton>
#include <QIcon>
#include <QPixmap>
#include <QPainter>
#include <QFont>
#include <QSize>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QJsonDocument>
#include <QJsonObject>
#include <QWidget>

// Local helper: fallback emoji icon
static QIcon makeFallbackIcon(const QString &emoji)
{
	QPixmap pm(32, 32);
	pm.fill(Qt::transparent);

	QPainter p(&pm);
	QFont f;
	f.setPointSize(18);
	p.setFont(f);
	p.setPen(Qt::white);
	p.drawText(pm.rect(), Qt::AlignCenter, emoji);
	p.end();

	return QIcon(pm);
}

void RsMainDock::createSidebarMenus()
{
	auto makeButton = [&](const QString &text, const QString &tooltip, const QString &iconName,
			      const QString &fallbackEmoji) -> QToolButton * {
		QToolButton *btn = new QToolButton(m_navCard);
		btn->setText(text);
		btn->setToolTip(tooltip);
		btn->setObjectName("SidebarButton");
		btn->setStyleSheet(""); // ensure stylesheet applies

		QIcon icon = QIcon::fromTheme(iconName);
		if (icon.isNull() || icon.availableSizes().isEmpty())
			icon = makeFallbackIcon(fallbackEmoji);

		btn->setIcon(icon);
		btn->setIconSize(QSize(20, 20));
		btn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

		// Minimum size to keep text + icon readable
		btn->setMinimumWidth(140);
		btn->setFixedHeight(32);

		// Let layout control expansion instead of hard limits
		btn->setMaximumWidth(QWIDGETSIZE_MAX);

		// Horizontal growth allowed, vertical size preferred
		btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

		btn->setAutoRaise(true);

		return btn;
	};

	// --- SYSTEM menu buttons ---
	m_btnControls = makeButton("Controls", "Start/stop streaming and recording", "media-playback-start", "▶");
	m_btnScenesSources = makeButton("Scenes & Sources", "Manage scenes and sources", "view-list-tree", "🎬");
	m_btnStats = makeButton("Stats", "Stream performance metrics", "view-statistics", "📊");
	m_btnObsSettings =
		makeButton("OBS Settings", "Open or mirror OBS settings (placeholder)", "preferences-system", "⚙");

	// SYSTEM menu buttons laid out as a grid (prevents extra vertical space)
	auto *sysGrid = new QGridLayout();
	sysGrid->setContentsMargins(0, 0, 0, 0);
	sysGrid->setHorizontalSpacing(6);
	sysGrid->setVerticalSpacing(6);

	sysGrid->addWidget(m_btnControls, 0, 0);
	sysGrid->addWidget(m_btnScenesSources, 0, 1);
	sysGrid->addWidget(m_btnStats, 1, 0);
	sysGrid->addWidget(m_btnObsSettings, 1, 1);

	sysGrid->setColumnStretch(0, 1);
	sysGrid->setColumnStretch(1, 1);
	sysGrid->setRowStretch(0, 0);
	sysGrid->setRowStretch(1, 0);

	// Insert grid into the existing VBox layout
	m_systemMenuLayout->addLayout(sysGrid);

	// IMPORTANT: stop VBox from adding free space below
	m_systemMenuLayout->addStretch(0);

	m_systemMenu->setMinimumWidth(240);
	m_systemMenu->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

	// --- STREAM TOOLS navigation ---
	m_btnStreamToolsQuickActions = makeButton("Quick Actions", "Compact controls for Hub-owned Stream Tools",
		"system-run", "⚡");
	m_btnUiSettings =
		makeButton("UI Settings", "RearSilver Stream Suite layout settings", "preferences-desktop-theme", "🎛");
	m_enhMenuLayout->addWidget(m_btnStreamToolsQuickActions, 0, 0);
	m_enhMenuLayout->addWidget(m_btnUiSettings, 0, 1);
	m_enhMenu->setMinimumWidth(300);

	// Ensure both columns expand evenly in horizontal layouts
	m_enhMenuLayout->setColumnStretch(0, 1);
	m_enhMenuLayout->setColumnStretch(1, 1);
	// Prevent rows from stretching vertically
	m_enhMenuLayout->setRowStretch(0, 0);

	// --- MUSIC menu buttons ---
	// NOTE: m_musicMenu and m_musicMenuLayout are created in rs_main_dock.cpp (see instructions below).
	m_btnMusicNowPlaying =
		makeButton("Now Playing", "Current track and playback controls", "media-playback-start", "🎵");
	m_btnMusicQueue = makeButton("Queue", "Upcoming requests and order", "view-list-details", "📜");

	// Keep the same 2-column grid discipline as other menus
	m_musicMenuLayout->addWidget(m_btnMusicNowPlaying, 0, 0);
	m_musicMenuLayout->addWidget(m_btnMusicQueue, 0, 1);

	m_musicMenuLayout->setColumnStretch(0, 1);
	m_musicMenuLayout->setColumnStretch(1, 1);
	m_musicMenuLayout->setRowStretch(0, 0);

	m_musicMenu->setMinimumWidth(300);

	// Wire up menu button signals
	connect(m_btnControls, &QToolButton::clicked, this, &RsMainDock::showControls);
	connect(m_btnScenesSources, &QToolButton::clicked, this, &RsMainDock::showScenesSources);
	connect(m_btnStats, &QToolButton::clicked, this, &RsMainDock::showStats);
	connect(m_btnObsSettings, &QToolButton::clicked, this, &RsMainDock::openNativeSettings);

	connect(m_btnStreamToolsQuickActions, &QToolButton::clicked, this, &RsMainDock::showStreamToolsQuickActions);
	connect(m_btnUiSettings, &QToolButton::clicked, this, &RsMainDock::showUiSettings);

	connect(m_btnMusicNowPlaying, &QToolButton::clicked, this, &RsMainDock::showMusicNowPlaying);
	connect(m_btnMusicQueue, &QToolButton::clicked, this, &RsMainDock::showMusicQueue);
}

void RsMainDock::updateStreamToolActionAvailability(bool hubConnected)
{
	m_streamToolsHubConnected = hubConnected;
	if (!hubConnected) {
		m_streamToolsQuickTextReady = false;
		m_streamToolsQuickTextHasMessage = false;
		m_streamToolsQuickTextVisible = false;
		m_streamToolsTimerReady = false;
		m_streamToolsReplayReady = false;
		if (m_lblStreamToolReplay) m_lblStreamToolReplay->setText("Instant Replay — Hub unavailable");
		if (m_lblStreamToolQuickText) m_lblStreamToolQuickText->setText("Quick Text — Hub unavailable");
		if (m_lblStreamToolTimer) m_lblStreamToolTimer->setText(
			QString("%1 — Hub unavailable").arg(m_streamToolsTimerMode == "countdown" ? "Countdown" : "Timer"));
	}
	updateStreamToolActionButtons();
}

void RsMainDock::updateStreamToolActionButtons()
{
	if (!m_pageStreamToolsQuickActions) return;
	for (QToolButton *button : m_pageStreamToolsQuickActions->findChildren<QToolButton *>())
		if (button->property("requiresReplayConfiguration").toBool())
			button->setEnabled(m_streamToolsHubConnected && m_streamToolsReplayReady);
		else if (button->property("requiresQuickTextMessage").toBool())
			button->setEnabled(m_streamToolsHubConnected && m_streamToolsQuickTextReady &&
				m_streamToolsQuickTextHasMessage);
		else if (button->property("requiresQuickTextConfiguration").toBool())
			button->setEnabled(m_streamToolsHubConnected && m_streamToolsQuickTextReady);
		else if (button->property("requiresTimerConfiguration").toBool())
			button->setEnabled(m_streamToolsHubConnected && m_streamToolsTimerReady);
		else if (button->property("requiresHub").toBool())
			button->setEnabled(m_streamToolsHubConnected);
}

void RsMainDock::updateStreamToolTimerMode(const QString &mode)
{
	m_streamToolsTimerMode = mode == "stopwatch" ? "stopwatch" : "countdown";
	const QString name = m_streamToolsTimerMode == "countdown" ? "Countdown" : "Timer";
	if (m_btnStreamToolTimerStart) m_btnStreamToolTimerStart->setText(QString("Start %1").arg(name.toLower()));
}

void RsMainDock::updateStreamToolState(const QString &command, const QString &argument)
{
	if (command == "BROWSER_REFRESH_STATE") {
		if (QLabel *heading = m_pageStreamToolsQuickActions->findChild<QLabel *>("browserRefreshState"))
			heading->setText(argument == "all" ? "Browser Refresh — all scenes refreshed"
				: "Browser Refresh — current scene refreshed");
		return;
	}
	if (command != "QUICK_TEXT_STATE" && command != "TIMER_STATE" && command != "REPLAY_STATE") return;
	QJsonParseError error;
	const QJsonDocument document = QJsonDocument::fromJson(argument.toUtf8(), &error);
	if (error.error != QJsonParseError::NoError || !document.isObject()) return;
	const QJsonObject state = document.object();
	if (command == "QUICK_TEXT_STATE") {
		if (m_lblStreamToolQuickText) {
			m_streamToolsQuickTextVisible = state.value("visibleInCurrentScene").toBool();
			const QString visibility = m_streamToolsQuickTextVisible ? "Visible" : "Hidden";
			const QString activity = m_streamToolsQuickTextHasMessage ? "Message ready" : "No message selected";
			m_lblStreamToolQuickText->setText(QString("Quick Text — %1 | %2").arg(visibility, activity));
		}
		return;
	}
	if (command == "REPLAY_STATE") {
		if (m_lblStreamToolReplay) {
			const QString visibility = state.value("visible").toBool() ? "Visible" : "Hidden";
			const QString activity = state.value("playing").toBool() ? "Playing"
				: state.value("bufferActive").toBool() ? "Buffer active" : "Buffer inactive";
			m_lblStreamToolReplay->setText(QString("Instant Replay — %1 | %2").arg(visibility, activity));
		}
		return;
	}
	const QString name = m_streamToolsTimerMode == "countdown" ? "Countdown" : "Timer";
	const bool running = state.value("running").toBool();
	const bool paused = state.value("paused").toBool();
	if (m_lblStreamToolTimer) {
		const QString visibility = state.value("visibleInCurrentScene").toBool() ? "Visible" : "Hidden";
		const QString activity = paused ? "Paused" : running ? "Running" : "Ready";
		m_lblStreamToolTimer->setText(QString("%1 — %2 | %3").arg(name, visibility, activity));
	}
	if (m_btnStreamToolTimerPause) m_btnStreamToolTimerPause->setText(paused ? "Resume" : "Pause");
}
