#include "rs_main_dock.hpp"

#include <QResizeEvent>
#include <QApplication>
#include <QDockWidget>
#include <QMainWindow>
#include <QComboBox>
#include <QGridLayout>
#include <QToolButton>

#include "rs_music/ui/rs_music_now_playing.hpp"

// --------------------------------------------------
// Minimum usable sizes (tuned for RearSilver UI)
// --------------------------------------------------
static constexpr int kMinCompactWidthPx = 280;
static constexpr int kMinHorizontalHeightPx = 220; // stops “nothing visible” collapse
static constexpr int kEnterCompactWidthPx = 340;
static constexpr int kLeaveCompactWidthPx = 360;

// --------------------------------------------------
// Nav "hard stop" (soft mins only; do NOT lock resizing)
// --------------------------------------------------
static constexpr int kNavMinHeightVerticalPx = 200;
static constexpr int kNavMinWidthHorizontalPx = 280;

static QDockWidget *findWrapperDock(QWidget *w)
{
	QWidget *p = w;
	while (p) {
		if (auto *dock = qobject_cast<QDockWidget *>(p))
			return dock;
		p = p->parentWidget();
	}
	return nullptr;
}

static Qt::DockWidgetArea dockAreaForWidget(QWidget *w)
{
	auto *dock = findWrapperDock(w);
	if (!dock)
		return Qt::NoDockWidgetArea;

	auto *mw = qobject_cast<QMainWindow *>(QApplication::activeWindow());
	if (!mw)
		return Qt::NoDockWidgetArea;

	return mw->dockWidgetArea(dock);
}

void RsMainDock::resizeEvent(QResizeEvent *event)
{
	// FIX: RsMainDock no longer inherits QDockWidget.
	// Call QWidget version instead.
	QWidget::resizeEvent(event);

	if (m_layoutMode == LayoutMode::Auto)
		updateEffectiveLayout();
	updateMusicStatusInfo();
}

void RsMainDock::updateEffectiveLayout()
{
	LayoutMode target = m_layoutMode;
	bool compact = m_layoutMode == LayoutMode::Vertical;

	if (m_layoutMode == LayoutMode::Auto) {
		const int availableWidth = m_central ? m_central->width() : width();
		compact = m_compactLayout ? availableWidth < kLeaveCompactWidthPx
					  : availableWidth < kEnterCompactWidthPx;
		// Auto remains a top-to-bottom dock layout and only reflows its grids.
		// Horizontal is the explicit two-pane mode.
		target = LayoutMode::Vertical;
	}

	const Qt::Orientation expectedOrientation = target == LayoutMode::Vertical ? Qt::Vertical : Qt::Horizontal;
	const bool targetChanged = target != m_effectiveLayout;
	const bool orientationChanged = !m_splitter || m_splitter->orientation() != expectedOrientation;
	const bool compactChanged = compact != m_compactLayout;
	if (!targetChanged && !orientationChanged && !compactChanged)
		return;

	m_effectiveLayout = target;
	setUpdatesEnabled(false);
	if (compactChanged || targetChanged)
		setCompactLayout(compact);
	if (targetChanged || orientationChanged)
		applyOrientation();
	else
		updateGeometry();
	setUpdatesEnabled(true);
	update();
}

void RsMainDock::setCompactLayout(bool compact)
{
	m_compactLayout = compact;
	applyNavigationLayout();
	applyStreamToolActionLayout();
	updateCompactTabNavigation();

	if (auto *nowPlaying = qobject_cast<RsMusicNowPlaying *>(m_pageMusicNowPlaying))
		nowPlaying->setCompactLayout(compact);

	if (m_contentCard)
		m_contentCard->setMinimumWidth(compact ? 0 : 300);
	if (m_splitter)
		m_splitter->setMinimumWidth(compact ? kMinCompactWidthPx
						 : m_layoutMode == LayoutMode::Horizontal ? 370 : 300);
}

void RsMainDock::updateCompactTabNavigation()
{
	if (!m_tabBar || !m_btnPreviousTabPage || !m_btnNextTabPage)
		return;

	if (!m_compactLayout) {
		for (int index = 0; index < m_tabBar->count(); ++index)
			m_tabBar->setTabVisible(index, true);
		m_btnPreviousTabPage->hide();
		m_btnNextTabPage->hide();
		return;
	}

	const int currentIndex = m_tabBar->currentIndex();
	if (currentIndex == 0)
		m_compactTabPage = 0;
	else if (currentIndex == 2)
		m_compactTabPage = 1;

	const bool showFirstPage = m_compactTabPage == 0;
	m_tabBar->setTabVisible(0, showFirstPage);
	m_tabBar->setTabVisible(1, true);
	m_tabBar->setTabVisible(2, !showFirstPage);
	m_btnPreviousTabPage->setVisible(!showFirstPage);
	m_btnNextTabPage->setVisible(showFirstPage);
}

void RsMainDock::applyNavigationLayout()
{
	auto resetGrid = [](QGridLayout *grid, const QList<QToolButton *> &buttons, bool compact) {
		if (!grid)
			return;
		for (QToolButton *button : buttons) {
			grid->removeWidget(button);
			button->setMinimumWidth(compact ? 0 : 140);
		}
		grid->setColumnStretch(0, 1);
		grid->setColumnStretch(1, compact ? 0 : 1);
		for (int index = 0; index < buttons.size(); ++index) {
			const int row = compact ? index : index / 2;
			const int column = compact ? 0 : index % 2;
			grid->addWidget(buttons.at(index), row, column);
		}
	};

	resetGrid(m_systemMenuGrid, {m_btnControls, m_btnScenesSources, m_btnStats, m_btnObsSettings}, m_compactLayout);
	resetGrid(m_enhMenuLayout, {m_btnStreamToolsQuickActions, m_btnUiSettings}, m_compactLayout);
	resetGrid(m_musicMenuLayout, {m_btnMusicNowPlaying, m_btnMusicQueue}, m_compactLayout);

	if (m_systemMenu)
		m_systemMenu->setMinimumWidth(m_compactLayout ? 0 : 240);
	if (m_enhMenu)
		m_enhMenu->setMinimumWidth(m_compactLayout ? 0 : 300);
	if (m_musicMenu)
		m_musicMenu->setMinimumWidth(m_compactLayout ? 0 : 300);
}

void RsMainDock::applyStreamToolActionLayout()
{
	for (QGridLayout *grid : m_streamToolActionLayouts) {
		QList<QWidget *> buttons;
		while (grid && grid->count() > 0) {
			QLayoutItem *item = grid->takeAt(0);
			if (item->widget())
				buttons.append(item->widget());
			delete item;
		}
		if (!grid)
			continue;

		grid->setColumnStretch(0, 1);
		grid->setColumnStretch(1, m_compactLayout ? 0 : 1);
		for (int index = 0; index < buttons.size(); ++index) {
			QWidget *button = buttons.at(index);
			button->setMinimumWidth(m_compactLayout ? 0 : 140);
			if (m_compactLayout)
				grid->addWidget(button, index, 0);
			else if (index == buttons.size() - 1 && buttons.size() % 2 == 1)
				grid->addWidget(button, index / 2, 0, 1, 2);
			else
				grid->addWidget(button, index / 2, index % 2);
		}
	}
}

void RsMainDock::applyOrientation()
{
	if (!m_splitter || !m_topContainer || !m_bottomContainer)
		return;

	// Remove existing widgets safely
	while (m_splitter->count() > 0) {
		QWidget *w = m_splitter->widget(0);
		w->setParent(nullptr);
	}

	// Default: allow resizing in both directions.
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	setMaximumHeight(QWIDGETSIZE_MAX);
	setMaximumWidth(QWIDGETSIZE_MAX);

	// IMPORTANT:
	// Do NOT stack multiple minimums (dock + containers + internal widgets).
	// Keep containers mostly free so OBS can resize the wrapper dock naturally.
	m_bottomContainer->setMinimumSize(0, 0);
	m_bottomContainer->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);

	m_topContainer->setMinimumWidth(0);
	setMinimumWidth(0);

if (m_effectiveLayout == LayoutMode::Vertical) {
		// Vertical dock layout (nav above content)
		m_splitter->setOrientation(Qt::Vertical);

		m_splitter->addWidget(m_topContainer);
		m_splitter->addWidget(m_bottomContainer);

		// Top does not resize, bottom does
		m_splitter->setStretchFactor(0, 0);
		m_splitter->setStretchFactor(1, 1);
	}

else {
		// Horizontal dock layout (nav left, content right)
		m_splitter->setOrientation(Qt::Horizontal);

		// Soft minimum: nav must not be fully obscured, but MUST remain resizable.
		m_topContainer->setMinimumWidth(kNavMinWidthHorizontalPx);

		// Do NOT enforce a wide minimum width here; width should stay flexible.
		setMinimumSize(0, 0);
		// Horizontal layout should be 2-pane; remove spacer if present
		if (QWidget *bottomSpacer = m_splitter->findChild<QWidget *>("rs-bottom-spacer")) {
			bottomSpacer->setParent(nullptr);
		}

		m_splitter->addWidget(m_topContainer);
		m_splitter->addWidget(m_bottomContainer);

		// Default split: nav readable, content dominant
		m_splitter->setSizes({300, 700});
	}

	// Warn if the chosen layout doesn't match the dock area
	if (m_layoutCombo) {
		const Qt::DockWidgetArea area = dockAreaForWidget(this);

		const bool dockIsLeftRight = (area == Qt::LeftDockWidgetArea || area == Qt::RightDockWidgetArea);
		const bool dockIsTopBottom = (area == Qt::TopDockWidgetArea || area == Qt::BottomDockWidgetArea);

		bool mismatch = false;
		QString tip;

		if (m_layoutMode == LayoutMode::Horizontal && dockIsLeftRight) {
			mismatch = true;
			tip = tr("Horizontal layout works best when docked Top/Bottom.\n"
				 "Left/Right docks cannot be resized shorter (height is controlled by OBS).");
		} else if (m_layoutMode == LayoutMode::Vertical && dockIsTopBottom) {
			mismatch = true;
			tip = tr("Vertical layout works best when docked Left/Right.\n"
				 "Top/Bottom docks are typically used as a short strip.");
		} else {
			tip = tr("Choose a layout that fits how you dock this panel in OBS.");
		}

		m_layoutCombo->setToolTip(tip);
		m_layoutCombo->setProperty("layoutMismatch", mismatch);
		m_layoutCombo->style()->unpolish(m_layoutCombo);
		m_layoutCombo->style()->polish(m_layoutCombo);
		m_layoutCombo->update();
	}

	// Enforce minimums on the wrapper QDockWidget (OBS actually resizes this)
	if (auto *dock = findWrapperDock(this)) {
		// Clear first to avoid accumulating constraints when switching modes
		dock->setMinimumSize(0, 0);

		if (m_effectiveLayout == LayoutMode::Vertical) {
			// Vertical: enforce minimum WIDTH only (height must remain flexible)
			dock->setMinimumWidth(kMinCompactWidthPx);
			this->setMinimumWidth(kMinCompactWidthPx);
			// leave height unconstrained (0)
		} else {
			// Horizontal: enforce minimum HEIGHT only (width must remain flexible)
			dock->setMinimumHeight(kMinHorizontalHeightPx);
			// leave width unconstrained (0)
		}
	}

	updateGeometry();
}
