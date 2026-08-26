#include "rs_stream_overlay_manager.hpp"
#include "rs_stream_overlay_server.hpp"

#include <cstring>
#include <obs-module.h>
#include <obs-frontend-api.h>

namespace {
constexpr const char *kSourceName = "RearSilver Stream Suite | Quick Text";
constexpr const char *kTimerSourceName = "RearSilver Stream Suite | Timer";
constexpr const char *kOwnerKey = "rearsilver_stream_suite_owner";
constexpr const char *kFeatureKey = "rearsilver_stream_suite_feature";
constexpr const char *kRoleKey = "rearsilver_stream_suite_role";
constexpr const char *kOwnerValue = "stream_overlays";
constexpr const char *kFeatureValue = "quick_text";
constexpr const char *kRoleValue = "browser_source";

bool hasValue(obs_data_t *settings, const char *key, const char *expected)
{
	return std::strcmp(obs_data_get_string(settings, key), expected) == 0;
}

bool isManagedQuickText(obs_source_t *source)
{
	if (!source || std::strcmp(obs_source_get_unversioned_id(source), "browser_source") != 0) return false;
	obs_data_t *settings = obs_source_get_settings(source);
	const bool managed = hasValue(settings, kOwnerKey, kOwnerValue) &&
		hasValue(settings, kFeatureKey, kFeatureValue) && hasValue(settings, kRoleKey, kRoleValue);
	obs_data_release(settings);
	return managed;
}

bool isManagedTimer(obs_source_t *source)
{
	if (!source || std::strcmp(obs_source_get_unversioned_id(source), "browser_source") != 0) return false;
	obs_data_t *settings = obs_source_get_settings(source);
	const bool managed = hasValue(settings, kOwnerKey, kOwnerValue) &&
		hasValue(settings, kFeatureKey, "timer") && hasValue(settings, kRoleKey, kRoleValue);
	obs_data_release(settings);
	return managed;
}

obs_sceneitem_t *timerSceneItem(obs_source_t **sceneSourceOut = nullptr)
{
	obs_source_t *sceneSource = obs_frontend_get_current_scene();
	if (sceneSourceOut) *sceneSourceOut = sceneSource;
	if (!sceneSource) return nullptr;
	obs_scene_t *scene = obs_scene_from_source(sceneSource);
	return scene ? obs_scene_find_source(scene, kTimerSourceName) : nullptr;
}

QJsonObject timerStatusFor(obs_source_t *source, bool conflict, const QString &message = {})
{
	bool placed = false, visible = false;
	obs_source_t *sceneSource = nullptr;
	if (source && !conflict) {
		if (obs_sceneitem_t *item = timerSceneItem(&sceneSource)) {
			placed = obs_sceneitem_get_source(item) == source;
			visible = placed && obs_sceneitem_visible(item);
		}
	} else {
		timerSceneItem(&sceneSource);
	}
	if (sceneSource) obs_source_release(sceneSource);
	QJsonObject result{{"sourceExists", source != nullptr}, {"placedInCurrentScene", placed},
		{"visibleInCurrentScene", visible}, {"conflict", conflict}};
	result["message"] = message.isEmpty()
		? (conflict ? QString("A source named %1 already exists but is not managed by the Suite.").arg(kTimerSourceName)
			: !source ? QString("Set up the managed Timer source before starting it.")
			: placed ? QString("Timer is set up in the current scene.")
			: QString("Timer is set up and can be added to this scene with Set up timer."))
		: message;
	return result;
}

void applyTimerSettings(obs_source_t *source)
{
	obs_data_t *settings = obs_source_get_settings(source);
	obs_data_set_string(settings, "url", RsStreamOverlayServer::instance().timerUrl().toUtf8().constData());
	obs_data_set_bool(settings, "is_local_file", false);
	obs_data_set_int(settings, "width", 1920);
	obs_data_set_int(settings, "height", 500);
	obs_data_set_int(settings, "fps", 30);
	obs_data_set_bool(settings, "shutdown", false);
	obs_data_set_bool(settings, "restart_when_active", false);
	obs_data_set_bool(settings, "reroute_audio", true);
	obs_data_set_string(settings, kOwnerKey, kOwnerValue);
	obs_data_set_string(settings, kFeatureKey, "timer");
	obs_data_set_string(settings, kRoleKey, kRoleValue);
	obs_source_update(source, settings);
	obs_data_release(settings);
	obs_source_set_monitoring_type(source, OBS_MONITORING_TYPE_MONITOR_AND_OUTPUT);
}

obs_sceneitem_t *currentSceneItem(obs_source_t *source, obs_source_t **sceneSourceOut = nullptr)
{
	obs_source_t *sceneSource = obs_frontend_get_current_scene();
	if (sceneSourceOut) *sceneSourceOut = sceneSource;
	if (!sceneSource) return nullptr;
	obs_scene_t *scene = obs_scene_from_source(sceneSource);
	if (!scene || obs_scene_get_source(scene) == source) return nullptr;
	return obs_scene_find_source(scene, kSourceName);
}

QJsonObject statusFor(obs_source_t *source, bool conflict, const QString &message = {})
{
	bool placed = false, visible = false;
	obs_source_t *sceneSource = nullptr;
	if (source && !conflict) {
		if (obs_sceneitem_t *item = currentSceneItem(source, &sceneSource)) {
			placed = obs_sceneitem_get_source(item) == source;
			visible = placed && obs_sceneitem_visible(item);
		}
	} else {
		currentSceneItem(nullptr, &sceneSource);
	}
	if (sceneSource) obs_source_release(sceneSource);
	QJsonObject result{{"sourceExists", source != nullptr}, {"placedInCurrentScene", placed},
		{"visibleInCurrentScene", visible}, {"conflict", conflict}};
	result["message"] = message.isEmpty()
		? (conflict ? QString("A source named %1 already exists but is not managed by the Suite.").arg(kSourceName)
			: !source ? QString("Quick Text has not been added to OBS yet.")
			: !placed ? QString("Quick Text is ready and can be added to this scene with Show text.")
			: visible ? QString("Quick Text is visible in the current scene.")
			: QString("Quick Text is hidden in the current scene."))
		: message;
	return result;
}

void applyBrowserSettings(obs_source_t *source)
{
	obs_data_t *settings = obs_source_get_settings(source);
	obs_data_set_string(settings, "url", RsStreamOverlayServer::instance().quickTextUrl().toUtf8().constData());
	obs_data_set_bool(settings, "is_local_file", false);
	obs_data_set_int(settings, "width", 1920);
	obs_data_set_int(settings, "height", 500);
	obs_data_set_int(settings, "fps", 30);
	obs_data_set_bool(settings, "shutdown", false);
	obs_data_set_bool(settings, "restart_when_active", false);
	obs_data_set_string(settings, kOwnerKey, kOwnerValue);
	obs_data_set_string(settings, kFeatureKey, kFeatureValue);
	obs_data_set_string(settings, kRoleKey, kRoleValue);
	obs_source_update(source, settings);
	obs_data_release(settings);
}
}

QJsonObject RsStreamOverlayManager::quickTextStatus()
{
	obs_source_t *source = obs_get_source_by_name(kSourceName);
	const bool conflict = source && !isManagedQuickText(source);
	const QJsonObject result = statusFor(source, conflict);
	if (source) obs_source_release(source);
	return result;
}

QJsonObject RsStreamOverlayManager::showQuickTextInCurrentScene()
{
	if (!RsStreamOverlayServer::instance().port())
		return statusFor(nullptr, false, "Quick Text cannot start because the stream-overlay service is unavailable.");

	obs_source_t *source = obs_get_source_by_name(kSourceName);
	if (source && !isManagedQuickText(source)) {
		const QJsonObject result = statusFor(source, true);
		obs_source_release(source);
		return result;
	}
	if (!source) {
		obs_data_t *settings = obs_data_create();
		obs_data_set_string(settings, "url", RsStreamOverlayServer::instance().quickTextUrl().toUtf8().constData());
		obs_data_set_bool(settings, "is_local_file", false);
		obs_data_set_int(settings, "width", 1920);
		obs_data_set_int(settings, "height", 500);
		obs_data_set_int(settings, "fps", 30);
		obs_data_set_bool(settings, "shutdown", false);
		obs_data_set_bool(settings, "restart_when_active", false);
		obs_data_set_string(settings, kOwnerKey, kOwnerValue);
		obs_data_set_string(settings, kFeatureKey, kFeatureValue);
		obs_data_set_string(settings, kRoleKey, kRoleValue);
		source = obs_source_create("browser_source", kSourceName, settings, nullptr);
		obs_data_release(settings);
		if (!source) return statusFor(nullptr, false, "OBS could not create the managed Quick Text browser source.");
	} else {
		applyBrowserSettings(source);
	}

	obs_source_t *sceneSource = nullptr;
	obs_sceneitem_t *item = currentSceneItem(source, &sceneSource);
	obs_scene_t *scene = sceneSource ? obs_scene_from_source(sceneSource) : nullptr;
	if (!item && scene) item = obs_scene_add(scene, source);
	if (item && obs_sceneitem_get_source(item) == source) obs_sceneitem_set_visible(item, true);
	const QJsonObject result = item ? statusFor(source, false)
		: statusFor(source, false, "Quick Text exists, but OBS could not add it to the current scene.");
	if (sceneSource) obs_source_release(sceneSource);
	obs_source_release(source);
	return result;
}

QJsonObject RsStreamOverlayManager::clearQuickTextInCurrentScene()
{
	obs_source_t *source = obs_get_source_by_name(kSourceName);
	if (!source) return statusFor(nullptr, false);
	if (!isManagedQuickText(source)) {
		const QJsonObject result = statusFor(source, true);
		obs_source_release(source);
		return result;
	}
	obs_source_t *sceneSource = nullptr;
	if (obs_sceneitem_t *item = currentSceneItem(source, &sceneSource)) {
		if (obs_sceneitem_get_source(item) == source) obs_sceneitem_set_visible(item, false);
	}
	if (sceneSource) obs_source_release(sceneSource);
	const QJsonObject result = statusFor(source, false);
	obs_source_release(source);
	return result;
}

QJsonObject RsStreamOverlayManager::timerStatus()
{
	obs_source_t *source = obs_get_source_by_name(kTimerSourceName);
	const bool conflict = source && !isManagedTimer(source);
	const QJsonObject result = timerStatusFor(source, conflict);
	if (source) obs_source_release(source);
	return result;
}

QJsonObject RsStreamOverlayManager::setupTimerInCurrentScene()
{
	if (!RsStreamOverlayServer::instance().port())
		return timerStatusFor(nullptr, false, "Timer cannot start because the stream-overlay service is unavailable.");
	obs_source_t *source = obs_get_source_by_name(kTimerSourceName);
	if (source && !isManagedTimer(source)) {
		const QJsonObject result = timerStatusFor(source, true);
		obs_source_release(source);
		return result;
	}
	if (!source) {
		obs_data_t *settings = obs_data_create();
		obs_data_set_string(settings, "url", RsStreamOverlayServer::instance().timerUrl().toUtf8().constData());
		obs_data_set_bool(settings, "is_local_file", false);
		obs_data_set_int(settings, "width", 1920);
		obs_data_set_int(settings, "height", 500);
		obs_data_set_int(settings, "fps", 30);
		obs_data_set_bool(settings, "shutdown", false);
		obs_data_set_bool(settings, "restart_when_active", false);
		obs_data_set_string(settings, kOwnerKey, kOwnerValue);
		obs_data_set_string(settings, kFeatureKey, "timer");
		obs_data_set_bool(settings, "reroute_audio", true);
		obs_data_set_string(settings, kRoleKey, kRoleValue);
		source = obs_source_create("browser_source", kTimerSourceName, settings, nullptr);
		obs_data_release(settings);
		if (!source) return timerStatusFor(nullptr, false, "OBS could not create the managed Timer browser source.");
		obs_source_set_monitoring_type(source, OBS_MONITORING_TYPE_MONITOR_AND_OUTPUT);
	} else {
		applyTimerSettings(source);
	}
	obs_source_t *sceneSource = nullptr;
	obs_sceneitem_t *item = timerSceneItem(&sceneSource);
	obs_scene_t *scene = sceneSource ? obs_scene_from_source(sceneSource) : nullptr;
	if (!item && scene) {
		item = obs_scene_add(scene, source);
		if (item) obs_sceneitem_set_visible(item, false);
	}
	const QJsonObject result = item ? timerStatusFor(source, false)
		: timerStatusFor(source, false, "Timer exists, but OBS could not add it to the current scene.");
	if (sceneSource) obs_source_release(sceneSource);
	obs_source_release(source);
	return result;
}

QJsonObject RsStreamOverlayManager::setTimerVisibleInCurrentScene(bool visible)
{
	obs_source_t *source = obs_get_source_by_name(kTimerSourceName);
	if (!source) return timerStatusFor(nullptr, false);
	if (!isManagedTimer(source)) {
		const QJsonObject result = timerStatusFor(source, true);
		obs_source_release(source);
		return result;
	}
	obs_source_t *sceneSource = nullptr;
	if (obs_sceneitem_t *item = timerSceneItem(&sceneSource)) {
		if (obs_sceneitem_get_source(item) == source) obs_sceneitem_set_visible(item, visible);
	}
	if (sceneSource) obs_source_release(sceneSource);
	const QJsonObject result = timerStatusFor(source, false);
	obs_source_release(source);
	return result;
}
