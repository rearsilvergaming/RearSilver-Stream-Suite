#include "rs_main_dock.hpp"

#include <QStackedWidget>
#include <QToolButton>
#include <QComboBox>
#include <QStyle>

void RsMainDock::setActiveButton(QToolButton *button)
{
	QToolButton *buttons[] = {
		// SYSTEM
		m_btnControls,
		m_btnScenesSources,
		m_btnStats,

		// ENHANCEMENTS
		m_btnBrowserRefresh,
		m_btnQuickText,
		m_btnInstantReplay,
		m_btnAutoStart,
		m_btnTimer,
		m_btnUiSettings,

		// MUSIC
		m_btnMusicNowPlaying,
		m_btnMusicQueue,
		m_btnMusicRequests,
		m_btnMusicPlaylist,
		m_btnMusicSettings,
		m_btnMusicSetup,
		m_btnMusicOverlay,
	};

	for (QToolButton *btn : buttons) {
		if (!btn)
			continue;

		bool active = (btn == button);
		btn->setProperty("active", active);
		btn->style()->unpolish(btn);
		btn->style()->polish(btn);
	}
}

void RsMainDock::showControls()
{
	if (m_stack && m_pageControls) {
		m_stack->setCurrentWidget(m_pageControls);
		m_lastSystemPage = m_stack->currentIndex();
	}
	setActiveButton(m_btnControls);
}

void RsMainDock::showScenesSources()
{
	if (m_stack && m_pageScenesSources) {
		m_stack->setCurrentWidget(m_pageScenesSources);
		m_lastSystemPage = m_stack->currentIndex();
	}
	setActiveButton(m_btnScenesSources);
}

void RsMainDock::showStats()
{
	if (m_stack && m_pageStats) {
		m_stack->setCurrentWidget(m_pageStats);
		m_lastSystemPage = m_stack->currentIndex();
	}
	setActiveButton(m_btnStats);
}

void RsMainDock::showBrowserRefresh()
{
	if (m_stack && m_pageBrowserRefresh) {
		m_stack->setCurrentWidget(m_pageBrowserRefresh);
		m_lastEnhancementsPage = m_stack->currentIndex();
	}
	setActiveButton(m_btnBrowserRefresh);
}

void RsMainDock::showQuickText()
{
	if (m_stack && m_pageQuickText) {
		m_stack->setCurrentWidget(m_pageQuickText);
		m_lastEnhancementsPage = m_stack->currentIndex();
	}
	setActiveButton(m_btnQuickText);
}

void RsMainDock::showInstantReplay()
{
	if (m_stack && m_pageInstantReplay) {
		m_stack->setCurrentWidget(m_pageInstantReplay);
		m_lastEnhancementsPage = m_stack->currentIndex();
	}
	setActiveButton(m_btnInstantReplay);
}

void RsMainDock::showAutoStart()
{
	if (m_stack && m_pageAutoStart) {
		m_stack->setCurrentWidget(m_pageAutoStart);
		m_lastEnhancementsPage = m_stack->currentIndex();
	}
	setActiveButton(m_btnAutoStart);
}

void RsMainDock::showTimer()
{
	if (m_stack && m_pageTimer) {
		m_stack->setCurrentWidget(m_pageTimer);
		m_lastEnhancementsPage = m_stack->currentIndex();
	}
	setActiveButton(m_btnTimer);
}

void RsMainDock::showUiSettings()
{
	if (m_stack && m_pageUiSettings) {
		m_stack->setCurrentWidget(m_pageUiSettings);
		m_lastEnhancementsPage = m_stack->currentIndex();
	}
	setActiveButton(m_btnUiSettings);
}

// --------------------
// MUSIC pages
// --------------------
void RsMainDock::showMusicNowPlaying()
{
	if (m_stack && m_pageMusicNowPlaying) {
		m_stack->setCurrentWidget(m_pageMusicNowPlaying);
		m_lastMusicPage = m_stack->currentIndex();
	}
	setActiveButton(m_btnMusicNowPlaying);
}

void RsMainDock::showMusicQueue()
{
	if (m_stack && m_pageMusicQueue) {
		m_stack->setCurrentWidget(m_pageMusicQueue);
		m_lastMusicPage = m_stack->currentIndex();
	}
	setActiveButton(m_btnMusicQueue);
}

void RsMainDock::showMusicRequests()
{
	if (m_stack && m_pageMusicRequests) {
		m_stack->setCurrentWidget(m_pageMusicRequests);
		m_lastMusicPage = m_stack->currentIndex();
	}
	setActiveButton(m_btnMusicRequests);
}

void RsMainDock::showMusicPlaylist()
{
	if (m_stack && m_pageMusicPlaylist) {
		m_stack->setCurrentWidget(m_pageMusicPlaylist);
		m_lastMusicPage = m_stack->currentIndex();
	}
	setActiveButton(m_btnMusicPlaylist);
}

void RsMainDock::showMusicSettings()
{
	if (m_stack && m_pageMusicSettings) {
		m_stack->setCurrentWidget(m_pageMusicSettings);
		m_lastMusicPage = m_stack->currentIndex();
	}
	setActiveButton(m_btnMusicSettings);
}

void RsMainDock::showMusicSetup()
{
	if (m_stack && m_pageMusicSetup) { m_stack->setCurrentWidget(m_pageMusicSetup); m_lastMusicPage=m_stack->currentIndex(); }
	setActiveButton(m_btnMusicSetup);
}

void RsMainDock::showMusicOverlay()
{
	if (m_stack && m_pageMusicOverlay) { m_stack->setCurrentWidget(m_pageMusicOverlay); m_lastMusicPage=m_stack->currentIndex(); }
	setActiveButton(m_btnMusicOverlay);
}

void RsMainDock::onLayoutModeChanged(int index)
{
	if (!m_layoutCombo)
		return;

	int val = m_layoutCombo->itemData(index).toInt();
	m_layoutMode = static_cast<LayoutMode>(val);

	saveSettings();
	updateEffectiveLayout();
}

void RsMainDock::onTabChanged(int index)
{
	if (!m_menuStack || !m_stack)
		return;

	m_menuStack->setCurrentIndex(index);

	if (index == 0) {
		// SYSTEM tab
		m_stack->setCurrentIndex(m_lastSystemPage);
		return;
	}

	if (index == 1) {
		// ENHANCEMENTS tab
		m_stack->setCurrentIndex(m_lastEnhancementsPage);
		return;
	}

	// MUSIC tab (index 2)
	// If you have m_lastMusicPage, use it. Otherwise go to the first music page.
#ifdef __cpp_unused
	m_stack->setCurrentIndex(m_lastMusicPage);
#else
	if (m_pageMusicNowPlaying)
		m_stack->setCurrentWidget(m_pageMusicNowPlaying);
#endif
}
