#include "rs_main_dock.hpp"

#include <QResizeEvent>
#include <QApplication>
#include <QDockWidget>
#include <QMainWindow>
#include <QComboBox>

// --------------------------------------------------
// Minimum usable sizes (tuned for RearSilver UI)
// --------------------------------------------------
static constexpr int kMinVerticalWidthPx = 420;
static constexpr int kMinHorizontalHeightPx = 220; // stops “nothing visible” collapse

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
}

void RsMainDock::updateEffectiveLayout()
{
	LayoutMode target = m_layoutMode;

	if (m_layoutMode == LayoutMode::Auto) {
		const QSize sz = m_central->size();
		// Simple heuristic: wide => horizontal, tall => vertical
		if (sz.height() > sz.width() * 0.8)
			target = LayoutMode::Vertical;
		else
			target = LayoutMode::Horizontal;
	}

	if (target == m_effectiveLayout)
		return;

	m_effectiveLayout = target;
	applyOrientation();
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
			dock->setMinimumWidth(kMinVerticalWidthPx);
			this->setMinimumWidth(kMinVerticalWidthPx);
			// leave height unconstrained (0)
		} else {
			// Horizontal: enforce minimum HEIGHT only (width must remain flexible)
			dock->setMinimumHeight(kMinHorizontalHeightPx);
			// leave width unconstrained (0)
		}
	}

	updateGeometry();
}
