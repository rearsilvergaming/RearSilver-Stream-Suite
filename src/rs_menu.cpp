#include "rs_main_dock.hpp"

#include <QToolButton>
#include <QIcon>
#include <QPixmap>
#include <QPainter>
#include <QFont>
#include <QSize>
#include <QVBoxLayout>
#include <QGridLayout>
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

	// --- ENHANCEMENTS menu buttons ---
	m_btnBrowserRefresh =
		makeButton("Browser Refresh", "Refresh browser sources (placeholder)", "view-refresh", "🔄");
	m_btnQuickText = makeButton("Quick Text", "Drop BRB / Coffee / custom text", "insert-text", "💬");
	m_btnInstantReplay =
		makeButton("Instant Replay", "Replay the last moments on stream", "rs-instant-replay", "🎥");
	m_btnAutoStart =
		makeButton("Auto-Start Manager", "Launch/close tools with OBS (placeholder)", "system-run", "🚀");
	m_btnTimer = makeButton("Timer / Countdown", "Quick overlay timer (placeholder)", "appointment-new", "⏱");
	m_btnUiSettings =
		makeButton("UI Settings", "RearSilver Stream Suite layout settings", "preferences-desktop-theme", "🎛");

	m_enhMenuLayout->addWidget(m_btnBrowserRefresh, 0, 0);
	m_enhMenuLayout->addWidget(m_btnQuickText, 0, 1);

	m_enhMenuLayout->addWidget(m_btnInstantReplay, 1, 0);
	m_enhMenuLayout->addWidget(m_btnAutoStart, 1, 1);

	m_enhMenuLayout->addWidget(m_btnTimer, 2, 0);
	m_enhMenuLayout->addWidget(m_btnUiSettings, 2, 1);
	m_enhMenu->setMinimumWidth(300);

	// Ensure both columns expand evenly in horizontal layouts
	m_enhMenuLayout->setColumnStretch(0, 1);
	m_enhMenuLayout->setColumnStretch(1, 1);
	// Prevent rows from stretching vertically
	m_enhMenuLayout->setRowStretch(0, 0);
	m_enhMenuLayout->setRowStretch(1, 0);
	m_enhMenuLayout->setRowStretch(2, 0);

	// --- MUSIC menu buttons ---
	// NOTE: m_musicMenu and m_musicMenuLayout are created in rs_main_dock.cpp (see instructions below).
	m_btnMusicNowPlaying =
		makeButton("Now Playing", "Current track and playback controls", "media-playback-start", "🎵");
	m_btnMusicQueue = makeButton("Queue", "Upcoming requests and order", "view-list-details", "📜");
	m_btnMusicRequests = makeButton("Requests", "Request rules and toggles", "emblem-favorite", "🎫");
	m_btnMusicPlaylist = makeButton("Playlist", "Fallback playlist settings", "folder-music", "🎧");
	m_btnMusicSettings = makeButton("Settings", "Music system configuration", "preferences-system", "⚙");
	m_btnMusicSetup = makeButton("Setup", "Audio capture and player setup", "configure", "🔧");

	// Keep the same 2-column grid discipline as other menus
	m_musicMenuLayout->addWidget(m_btnMusicNowPlaying, 0, 0);
	m_musicMenuLayout->addWidget(m_btnMusicQueue, 0, 1);

	m_musicMenuLayout->addWidget(m_btnMusicRequests, 1, 0);
	m_musicMenuLayout->addWidget(m_btnMusicPlaylist, 1, 1);

	m_musicMenuLayout->addWidget(m_btnMusicSettings, 2, 0);
	m_musicMenuLayout->addWidget(m_btnMusicSetup, 2, 1);

	m_musicMenuLayout->setColumnStretch(0, 1);
	m_musicMenuLayout->setColumnStretch(1, 1);
	m_musicMenuLayout->setRowStretch(0, 0);
	m_musicMenuLayout->setRowStretch(1, 0);
	m_musicMenuLayout->setRowStretch(2, 0);

	m_musicMenu->setMinimumWidth(300);

	// Wire up menu button signals
	connect(m_btnControls, &QToolButton::clicked, this, &RsMainDock::showControls);
	connect(m_btnScenesSources, &QToolButton::clicked, this, &RsMainDock::showScenesSources);
	connect(m_btnStats, &QToolButton::clicked, this, &RsMainDock::showStats);
	connect(m_btnObsSettings, &QToolButton::clicked, this, &RsMainDock::openNativeSettings);

	connect(m_btnBrowserRefresh, &QToolButton::clicked, this, &RsMainDock::showBrowserRefresh);
	connect(m_btnQuickText, &QToolButton::clicked, this, &RsMainDock::showQuickText);
	connect(m_btnInstantReplay, &QToolButton::clicked, this, &RsMainDock::showInstantReplay);
	connect(m_btnAutoStart, &QToolButton::clicked, this, &RsMainDock::showAutoStart);
	connect(m_btnTimer, &QToolButton::clicked, this, &RsMainDock::showTimer);
	connect(m_btnUiSettings, &QToolButton::clicked, this, &RsMainDock::showUiSettings);

	connect(m_btnMusicNowPlaying, &QToolButton::clicked, this, &RsMainDock::showMusicNowPlaying);
	connect(m_btnMusicQueue, &QToolButton::clicked, this, &RsMainDock::showMusicQueue);
	connect(m_btnMusicRequests, &QToolButton::clicked, this, &RsMainDock::showMusicRequests);
	connect(m_btnMusicPlaylist, &QToolButton::clicked, this, &RsMainDock::showMusicPlaylist);
	connect(m_btnMusicSettings, &QToolButton::clicked, this, &RsMainDock::showMusicSettings);
	connect(m_btnMusicSetup, &QToolButton::clicked, this, &RsMainDock::showMusicSetup);
}
