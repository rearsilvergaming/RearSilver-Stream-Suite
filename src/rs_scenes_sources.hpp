#pragma once

#include <QWidget>
#include <QPointer>
#include <obs-frontend-api.h>

class QTabWidget;
class QLabel;
class QDockWidget;
class QAction;

class RsScenesSourcesPage : public QWidget {
	Q_OBJECT

public:
	explicit RsScenesSourcesPage(QWidget *parent = nullptr);
	~RsScenesSourcesPage() override;

private slots:
	void tryEmbedNativeDocks();

private:
	// ✅ OBS frontend callback (MUST be a real function)
	static void frontendEventCb(obs_frontend_event event, void *data);
	void onFrontendEvent(obs_frontend_event event);

	bool m_restored = false;

	// UI
	QTabWidget *m_tabs = nullptr;
	QLabel *m_status = nullptr;

	int m_retryCount = 0;

	// Native OBS docks + widgets
	QPointer<QDockWidget> m_nativeScenesDock;
	QPointer<QDockWidget> m_nativeSourcesDock;
	QPointer<QWidget> m_nativeScenesWidget;
	QPointer<QWidget> m_nativeSourcesWidget;

	// Placeholders
	QPointer<QWidget> m_scenesPlaceholder;
	QPointer<QWidget> m_sourcesPlaceholder;

	bool m_prevScenesDockVisible = false;
	bool m_prevSourcesDockVisible = false;

	void buildUi();
	void setStatus(const QString &text);
	void adoptDockActions(QDockWidget *dock, QWidget *targetHost);
	void restoreNativeDocks();
};
