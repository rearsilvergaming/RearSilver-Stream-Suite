#include "rs_scenes_sources.hpp"

#include <QTabWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QDockWidget>
#include <QMainWindow>
#include <QTimer>
#include <QAction>
#include <QSizePolicy>

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
		restoreNativeDocks();
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

	m_status = new QLabel("Connecting to OBS native docks…", this);
	m_status->setWordWrap(true);
	root->addWidget(m_status);
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
	if (m_nativeScenesWidget && m_nativeSourcesWidget)
		return;

	auto scheduleRetry = [this]() {
		if (++m_retryCount <= 20)
			QTimer::singleShot(250, this, &RsScenesSourcesPage::tryEmbedNativeDocks);
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

	m_tabs->clear();
	m_tabs->addTab(m_nativeScenesWidget, "Scenes");
	m_tabs->addTab(m_nativeSourcesWidget, "Sources");

	// 🔒 Force single-column scenes
	//m_nativeScenesWidget->setMinimumWidth(220);
	//m_nativeScenesWidget->setMaximumWidth(220);
	m_nativeScenesWidget->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

	m_status->setVisible(false);
}

// ------------------------------------------------------------
// SAFE RESTORE
// ------------------------------------------------------------
void RsScenesSourcesPage::restoreNativeDocks()
{
	if (!m_nativeScenesDock || !m_nativeSourcesDock)
		return;

	if (m_nativeScenesWidget)
		m_nativeScenesDock->setWidget(m_nativeScenesWidget);
	if (m_nativeSourcesWidget)
		m_nativeSourcesDock->setWidget(m_nativeSourcesWidget);

	m_nativeScenesDock->setVisible(m_prevScenesDockVisible);
	m_nativeSourcesDock->setVisible(m_prevSourcesDockVisible);

	m_nativeScenesDock.clear();
	m_nativeSourcesDock.clear();
	m_nativeScenesWidget.clear();
	m_nativeSourcesWidget.clear();
}
