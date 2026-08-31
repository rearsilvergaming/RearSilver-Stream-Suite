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
		m_btnStreamToolsQuickActions,
		m_btnUiSettings,

		// MUSIC
		m_btnMusicNowPlaying,
		m_btnMusicQueue,
		m_btnMusicRequests,
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

void RsMainDock::showStreamToolsQuickActions()
{
	if (m_stack && m_pageStreamToolsQuickActions) {
		m_stack->setCurrentWidget(m_pageStreamToolsQuickActions);
		m_lastEnhancementsPage = m_stack->currentIndex();
	}
	setActiveButton(m_btnStreamToolsQuickActions);
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
	auto restorePage = [this](int pageIndex) {
		m_stack->setCurrentIndex(pageIndex);
		QWidget *page = m_stack->currentWidget();
		if (page == m_pageControls) setActiveButton(m_btnControls);
		else if (page == m_pageScenesSources) setActiveButton(m_btnScenesSources);
		else if (page == m_pageStats) setActiveButton(m_btnStats);
		else if (page == m_pageStreamToolsQuickActions) setActiveButton(m_btnStreamToolsQuickActions);
		else if (page == m_pageUiSettings) setActiveButton(m_btnUiSettings);
		else if (page == m_pageMusicNowPlaying) setActiveButton(m_btnMusicNowPlaying);
		else if (page == m_pageMusicQueue) setActiveButton(m_btnMusicQueue);
		else if (page == m_pageMusicRequests) setActiveButton(m_btnMusicRequests);
		else if (page == m_pageMusicSettings) setActiveButton(m_btnMusicSettings);
		else if (page == m_pageMusicSetup) setActiveButton(m_btnMusicSetup);
		else if (page == m_pageMusicOverlay) setActiveButton(m_btnMusicOverlay);
		else setActiveButton(nullptr);
	};

	if (index == 0) {
		// SYSTEM tab
		restorePage(m_lastSystemPage);
		return;
	}

	if (index == 1) {
		// ENHANCEMENTS tab
		restorePage(m_lastEnhancementsPage);
		return;
	}

	// MUSIC tab (index 2)
	restorePage(m_lastMusicPage);
}
