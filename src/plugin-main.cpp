/*
    RearSilver Stream Suite
*/

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <obs.h>
#include <util/base.h>

#include <QMainWindow>
#include <QWidget>
#include <QCoreApplication>
#include <QSettings>
#include <QDir>
#include <QFileInfo>

#include "rs_main_dock.hpp"
#include "rs_entitlements.hpp"
#include "rs_music/rs_music.hpp"
#include "rs_music/rs_music_tls_probe.hpp"
#include "rs_music/rs_music_pcm_source.hpp"
#include "enhancements/rs_auto_start.hpp"
#include "enhancements/rs_instant_replay.hpp"

// ---------------------------------------------
// OBS module boilerplate
// ---------------------------------------------
OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("RearSilver-Stream-Suite", "en-GB")

#define LOG_INFO_MSG(fmt, ...)  blog(LOG_INFO,  "[RearSilver-Stream-Suite] " fmt, ##__VA_ARGS__)
#define LOG_ERR_MSG(fmt, ...)   blog(LOG_ERROR, "[RearSilver-Stream-Suite] " fmt, ##__VA_ARGS__)

static RsMainDock *g_dock = nullptr;

const char *obs_module_description(void)
{
	return "RearSilver Stream Suite dock with custom controls and scenes/sources UI.";
}

// ---------------------------------------------
// Create and register the dock correctly
// ---------------------------------------------
static void create_rs_dock(void)
{
	if (g_dock)
		return;

	// Create our QWidget-based dock content
	g_dock = new RsMainDock(nullptr);
	g_dock->setObjectName("RearSilverStreamSuiteDock");

	// OBS wraps the QWidget into a real QDockWidget
	bool ok = obs_frontend_add_dock_by_id("RearSilverStreamSuiteDock",  // Unique ID
					      "RearSilver Stream Suite",    // Menu label
					      static_cast<void *>(g_dock)); // Widget OBS wraps

	if (!ok)
		LOG_ERR_MSG("Failed to register dock via obs_frontend_add_dock_by_id()");
	else
		LOG_INFO_MSG("Dock registered successfully via obs_frontend_add_dock_by_id().");
}

// ---------------------------------------------
// Frontend event
// ---------------------------------------------
static void frontend_event_callback(enum obs_frontend_event event, void *)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		rsMusicPcmRemoveLegacyTestSource();
		create_rs_dock();
		break;

	case OBS_FRONTEND_EVENT_EXIT:
		LOG_INFO_MSG("OBS exiting — shutting down RS Music");
		rsMusicPcmStopOutput();
		rsMusicShutdown();
		break;

	default:
		break;
	}
}


// ---------------------------------------------
// Log Qt library paths (debugging)
// ---------------------------------------------
static void rs_log_qt_library_paths()
{
	const QStringList paths = QCoreApplication::libraryPaths();

	blog(LOG_INFO, "[RS Music] Qt library paths:");
	for (const QString &p : paths) {
		blog(LOG_INFO, "  - %s", p.toUtf8().constData());
	}
}
// ---------------------------------------------
// Ensure Qt can see OBS Qt plugin folders (tls backend lives under plugins/tls)
// ---------------------------------------------
static void rs_try_add_obs_qt_plugins_path()
{
	const QString appDir = QCoreApplication::applicationDirPath();
	blog(LOG_INFO, "[RS Music] Qt applicationDirPath = %s", appDir.toUtf8().constData());

// Prefer a plugin-local Qt plugins folder (sellable + does not depend on OBS shipping Qt plugins)
	// Expected layout:
	//   obs-plugins/64bit/RearSilver-Stream-Suite/qt-plugins/tls/...
	const QString pluginLocalPluginsDir =
		QDir(appDir).filePath("../../obs-plugins/64bit/RearSilver-Stream-Suite/qt-plugins");

	const QString pluginsDir = pluginLocalPluginsDir;
	blog(LOG_INFO, "[RS Music] Checking for Qt plugins dir: %s", pluginsDir.toUtf8().constData());

	if (QDir(pluginsDir).exists()) {
		QCoreApplication::addLibraryPath(pluginsDir);
		blog(LOG_INFO, "[RS Music] Added Qt library path: %s", pluginsDir.toUtf8().constData());
	} else {
		blog(LOG_WARNING, "[RS Music] Qt plugins dir NOT found at: %s", pluginsDir.toUtf8().constData());
	}

	// Log TLS backend DLL presence if directory exists
	const QString tlsDir = QDir(pluginsDir).filePath("tls");
	blog(LOG_INFO, "[RS Music] Checking for TLS plugins dir: %s", tlsDir.toUtf8().constData());

	if (QDir(tlsDir).exists()) {
		const QString schannel = QDir(tlsDir).filePath("qschannelbackend.dll");
		const QString openssl = QDir(tlsDir).filePath("qopensslbackend.dll");

		blog(LOG_INFO, "[RS Music] TLS backend present? qschannelbackend.dll = %s",
		     QFileInfo::exists(schannel) ? "YES" : "NO");
		blog(LOG_INFO, "[RS Music] TLS backend present? qopensslbackend.dll = %s",
		     QFileInfo::exists(openssl) ? "YES" : "NO");
	} else {
		blog(LOG_WARNING, "[RS Music] TLS plugins dir NOT found at: %s", tlsDir.toUtf8().constData());
	}
}


// ---------------------------------------------
// Load / Unload
// ---------------------------------------------
bool obs_module_load(void)
{
	LOG_INFO_MSG("plugin load started");
	LOG_INFO_MSG("product edition: %s", RsEntitlements::editionKey().toUtf8().constData());

		// Ensure QSettings uses a stable namespace for this plugin (consistent across OBS sessions)
	QCoreApplication::setOrganizationName("RearSilver");
	QCoreApplication::setApplicationName("RearSilver-Stream-Suite");
	QSettings::setDefaultFormat(QSettings::NativeFormat);


	// Log current Qt paths, then try to add OBS appDir/plugins (where tls backend should be),
	// then log again and probe TLS.
	rs_log_qt_library_paths();
	rs_try_add_obs_qt_plugins_path();
	rs_log_qt_library_paths();

	rs_music_tls_probe();
	rsMusicPcmRegisterSource();

	obs_frontend_add_event_callback(frontend_event_callback, nullptr);

	LOG_INFO_MSG("plugin load finished");
	return true;
}


void obs_module_unload(void)
{
	LOG_INFO_MSG("plugin unload");

	obs_frontend_remove_event_callback(frontend_event_callback, nullptr);
	RsInstantReplay::shutdown();
	RsAutoStart::shutdown();
	rsMusicPcmStopOutput();
	rsMusicShutdown();
	rsMusicPcmShutdownSource();

	// Tell OBS to remove the dock
	obs_frontend_remove_dock("RearSilverStreamSuiteDock");

	// DO NOT delete g_dock — OBS owns it
	g_dock = nullptr;
}

