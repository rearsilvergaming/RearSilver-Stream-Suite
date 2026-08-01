#include "rs_scenes_sources.hpp"

#include <QTabWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QDockWidget>
#include <QMainWindow>
#include <QTimer>
#include <QAction>
#include <QSizePolicy>
#include <QPushButton>
#include <QHBoxLayout>

// OBS
#include <obs-frontend-api.h>

RsScenesSourcesPage::RsScenesSourcesPage(QWidget *parent) : QWidget(parent)
{
	buildUi();

	QTimer::singleShot(0, this, &RsScenesSourcesPage::tryEmbedNativeDocks);

	// ✅ Register REAL callback
	obs_frontend_add_event_callback(&RsScenesSourcesPage::frontendEventCb, this);
}

RsScenesSourcesPage::~RsScenesSourcesPage()
{
	// ✅ Properly remove the SAME callback
	obs_frontend_remove_event_callback(&RsScenesSourcesPage::frontendEventCb, this);

	// ❌ Do NOT touch OBS UI here
}

// ------------------------------------------------------------
// STATIC CALLBACK
// ------------------------------------------------------------
void RsScenesSourcesPage::frontendEventCb(obs_frontend_event event, void *data)
{
	auto *self = static_cast<RsScenesSourcesPage *>(data);
	if (self)
		self->onFrontendEvent(event);
}

// ------------------------------------------------------------
// EVENT HANDLER
// ------------------------------------------------------------
void RsScenesSourcesPage::onFrontendEvent(obs_frontend_event event)
{
	if (event == OBS_FRONTEND_EVENT_EXIT && !m_restored) {
		m_restored = true;
		restoreNativeDocks(false);
	}
}

// ------------------------------------------------------------
// UI
// ------------------------------------------------------------
void RsScenesSourcesPage::buildUi()
{
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(0, 0, 0, 0);
	root->setSpacing(6);

	m_tabs = new QTabWidget(this);

	auto *scenesStub = new QWidget(m_tabs);
	auto *sourcesStub = new QWidget(m_tabs);

	m_tabs->addTab(scenesStub, "Scenes");
	m_tabs->addTab(sourcesStub, "Sources");

	root->addWidget(m_tabs);

	auto *footer = new QHBoxLayout();
	footer->setContentsMargins(0, 0, 0, 0);
	footer->setSpacing(8);

	m_status = new QLabel("Connecting to OBS scene and source panels…", this);
	m_status->setWordWrap(true);
	m_status->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	footer->addWidget(m_status, 1);

	m_toggleButton = new QPushButton("Retry connection", this);
	m_toggleButton->setToolTip(
		"Move OBS's native Scenes and Sources panels into or out of the Suite.");
	connect(m_toggleButton, &QPushButton::clicked, this,
		&RsScenesSourcesPage::toggleNativeDocks);
	footer->addWidget(m_toggleButton);

	root->addLayout(footer);
}

void RsScenesSourcesPage::setStatus(const QString &text)
{
	if (m_status) {
		m_status->setText(text);
		m_status->setVisible(true);
	}
}

// ------------------------------------------------------------
// EMBED LOGIC (unchanged, safe)
// ------------------------------------------------------------
void RsScenesSourcesPage::tryEmbedNativeDocks()
{
	if (m_embedded && m_nativeScenesWidget && m_nativeSourcesWidget)
		return;

	auto scheduleRetry = [this]() {
		if (++m_retryCount <= 20) {
			QTimer::singleShot(250, this, &RsScenesSourcesPage::tryEmbedNativeDocks);
		} else {
			setStatus("OBS's Scenes and Sources panels were not found. They remain in their normal OBS docks.");
			if (m_toggleButton) {
				m_toggleButton->setText("Retry connection");
				m_toggleButton->setEnabled(true);
			}
		}
	};

	auto *mw = qobject_cast<QMainWindow *>(reinterpret_cast<QWidget *>(obs_frontend_get_main_window()));
	if (!mw) {
		scheduleRetry();
		return;
	}

	m_nativeScenesDock = mw->findChild<QDockWidget *>("scenesDock");
	m_nativeSourcesDock = mw->findChild<QDockWidget *>("sourcesDock");

	if (!m_nativeScenesDock || !m_nativeSourcesDock) {
		scheduleRetry();
		return;
	}

	m_nativeScenesWidget = m_nativeScenesDock->widget();
	m_nativeSourcesWidget = m_nativeSourcesDock->widget();

	if (!m_nativeScenesWidget || !m_nativeSourcesWidget) {
		scheduleRetry();
		return;
	}

	m_prevScenesDockVisible = m_nativeScenesDock->isVisible();
	m_prevSourcesDockVisible = m_nativeSourcesDock->isVisible();

	m_scenesPlaceholder = new QWidget();
	m_sourcesPlaceholder = new QWidget();
	m_nativeScenesDock->setWidget(m_scenesPlaceholder);
	m_nativeSourcesDock->setWidget(m_sourcesPlaceholder);

	m_nativeScenesDock->setVisible(false);
	m_nativeSourcesDock->setVisible(false);

	m_nativeScenesWidget->setParent(nullptr);
	m_nativeSourcesWidget->setParent(nullptr);

	// QTabWidget::clear() removes pages but does not delete them. Dispose only
	// our empty placeholder pages; native OBS widgets belong to their docks.
	while (m_tabs->count() > 0) {
		QWidget *page = m_tabs->widget(0);
		m_tabs->removeTab(0);
		if (page && page != m_nativeScenesWidget && page != m_nativeSourcesWidget)
			page->deleteLater();
	}
	m_tabs->addTab(m_nativeScenesWidget, "Scenes");
	m_tabs->addTab(m_nativeSourcesWidget, "Sources");
	m_tabs->setCurrentIndex(qBound(0, m_lastTabIndex, m_tabs->count() - 1));

	// Let both native panels use all available dock width. A fixed horizontal
	// policy made the Scenes list clip when the Suite was resized narrowly.
	m_nativeScenesWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	m_nativeSourcesWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

	m_embedded = true;
	m_retryCount = 0;
	m_status->setVisible(false);
	if (m_toggleButton) {
		m_toggleButton->setText("Return panels to OBS");
		m_toggleButton->setEnabled(true);
	}
}

void RsScenesSourcesPage::toggleNativeDocks()
{
	if (m_embedded) {
		m_lastTabIndex = m_tabs ? m_tabs->currentIndex() : 0;
		restoreNativeDocks(true);
		showPlaceholders();
		setStatus("Scenes and Sources are available in their normal OBS docks.");
		if (m_toggleButton)
			m_toggleButton->setText("Bring panels into Suite");
		return;
	}

	m_retryCount = 0;
	setStatus("Connecting to OBS scene and source panels…");
	if (m_toggleButton) {
		m_toggleButton->setText("Connecting…");
		m_toggleButton->setEnabled(false);
	}
	tryEmbedNativeDocks();
}

void RsScenesSourcesPage::showPlaceholders()
{
	if (!m_tabs)
		return;

	// QTabWidget::clear() removes pages but does not delete them. Dispose only
	// our empty placeholder pages; the native OBS widgets are owned by their
	// original docks and must never be deleted here.
	while (m_tabs->count() > 0) {
		QWidget *page = m_tabs->widget(0);
		m_tabs->removeTab(0);
		if (page && page != m_nativeScenesWidget && page != m_nativeSourcesWidget)
			page->deleteLater();
	}
	auto *scenesStub = new QWidget(m_tabs);
	auto *sourcesStub = new QWidget(m_tabs);
	m_tabs->addTab(scenesStub, "Scenes");
	m_tabs->addTab(sourcesStub, "Sources");
	m_tabs->setCurrentIndex(qBound(0, m_lastTabIndex, m_tabs->count() - 1));
}

// ------------------------------------------------------------
// SAFE RESTORE
// ------------------------------------------------------------
void RsScenesSourcesPage::restoreNativeDocks(bool makeVisible)
{
	if (!m_nativeScenesDock || !m_nativeSourcesDock)
		return;

	if (m_tabs)
		m_tabs->clear();

	if (m_nativeScenesWidget)
		m_nativeScenesDock->setWidget(m_nativeScenesWidget);
	if (m_nativeSourcesWidget)
		m_nativeSourcesDock->setWidget(m_nativeSourcesWidget);

	m_nativeScenesDock->setVisible(makeVisible || m_prevScenesDockVisible);
	m_nativeSourcesDock->setVisible(makeVisible || m_prevSourcesDockVisible);
	m_embedded = false;

	m_nativeScenesDock.clear();
	m_nativeSourcesDock.clear();
	m_nativeScenesWidget.clear();
	m_nativeSourcesWidget.clear();
}
