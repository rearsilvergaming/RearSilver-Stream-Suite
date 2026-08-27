#include "rs_stream_overlay_manager.hpp"
#include "rs_stream_overlay_server.hpp"

#include <cstring>
#include <obs-module.h>
#include <obs-frontend-api.h>

namespace {
constexpr const char *kSourceName = "RearSilver Stream Suite | Quick Text";
constexpr const char *kTimerSourceName = "RearSilver Stream Suite | Timer";
constexpr const char *kSimpleSceneName = "RearSilver Stream Suite | Stream Overlays";
constexpr const char *kQuickTextGroupName = "Quick Text Overlay";
constexpr const char *kTimerGroupName = "Timer Overlay";
constexpr const char *kOwnerKey = "rearsilver_stream_suite_owner";
constexpr const char *kFeatureKey = "rearsilver_stream_suite_feature";
constexpr const char *kRoleKey = "rearsilver_stream_suite_role";
constexpr const char *kOwnerValue = "stream_overlays";
constexpr const char *kFeatureValue = "quick_text";
constexpr int kQuickTextWidth = 1920;
constexpr int kQuickTextHeight = 720;
constexpr const char *kRoleValue = "browser_source";
constexpr const char *kSimpleSceneFeature = "stream_overlays_scene";
constexpr const char *kSimpleSceneRole = "managed_scene";
constexpr const char *kSimpleParentRole = "simple_scene_instance";
constexpr const char *kFeatureGroupRole = "feature_group";
QString s_placementMode = QStringLiteral("advanced");

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

bool isManagedSimpleScene(obs_source_t *source)
{
	if (!source || !obs_scene_from_source(source)) return false;
	obs_data_t *settings = obs_source_get_settings(source);
	const bool managed = hasValue(settings, kOwnerKey, kOwnerValue) &&
		hasValue(settings, kFeatureKey, kSimpleSceneFeature) && hasValue(settings, kRoleKey, kSimpleSceneRole);
	obs_data_release(settings);
	return managed;
}

void markManagedSimpleScene(obs_source_t *source)
{
	obs_data_t *settings = obs_source_get_settings(source);
	obs_data_set_string(settings, kOwnerKey, kOwnerValue);
	obs_data_set_string(settings, kFeatureKey, kSimpleSceneFeature);
	obs_data_set_string(settings, kRoleKey, kSimpleSceneRole);
	obs_source_update(source, settings);
	obs_data_release(settings);
}

void markSimpleParentItem(obs_sceneitem_t *item)
{
	obs_data_t *settings = obs_sceneitem_get_private_settings(item);
	obs_data_set_string(settings, kOwnerKey, kOwnerValue);
	obs_data_set_string(settings, kFeatureKey, kSimpleSceneFeature);
	obs_data_set_string(settings, kRoleKey, kSimpleParentRole);
	obs_data_release(settings);
}

struct SceneItemMatch {
	obs_sceneitem_t *item = nullptr;
	bool effectivelyVisible = false;
};

SceneItemMatch findSourceItemRecursive(obs_scene_t *scene, obs_source_t *source, bool ancestorsVisible)
{
	if (!scene || !source) return {};
	struct Search {
		obs_source_t *source;
		bool ancestorsVisible;
		SceneItemMatch match;
	} search{source, ancestorsVisible, {}};
	obs_scene_enum_items(scene, [](obs_scene_t *, obs_sceneitem_t *item, void *parameter) {
		auto *search = static_cast<Search *>(parameter);
		const bool itemVisible = search->ancestorsVisible && obs_sceneitem_visible(item);
		if (obs_sceneitem_get_source(item) == search->source) {
			search->match = {item, itemVisible};
			return false;
		}
		if (!obs_sceneitem_is_group(item)) return true;
		obs_scene_t *groupScene = obs_sceneitem_group_get_scene(item);
		search->match = findSourceItemRecursive(groupScene, search->source, itemVisible);
		return search->match.item == nullptr;
	}, &search);
	return search.match;

}

obs_sceneitem_t *findSourceItem(obs_scene_t *scene, obs_source_t *source, bool *effectivelyVisible = nullptr)
{
	const SceneItemMatch match = findSourceItemRecursive(scene, source, true);
	if (effectivelyVisible) *effectivelyVisible = match.effectivelyVisible;
	return match.item;
}

bool isManagedFeatureGroup(obs_source_t *source, const char *feature)
{
	if (!source || !obs_group_from_source(source)) return false;
	obs_data_t *settings = obs_source_get_settings(source);
	const bool managed = hasValue(settings, kOwnerKey, kOwnerValue) &&
		hasValue(settings, kFeatureKey, feature) && hasValue(settings, kRoleKey, kFeatureGroupRole);
	obs_data_release(settings);
	return managed;
}

void markManagedFeatureGroup(obs_sceneitem_t *item, const char *feature)
{
	if (!item) return;
	obs_source_t *source = obs_sceneitem_get_source(item);
	obs_data_t *sourceSettings = obs_source_get_settings(source);
	obs_data_set_string(sourceSettings, kOwnerKey, kOwnerValue);
	obs_data_set_string(sourceSettings, kFeatureKey, feature);
	obs_data_set_string(sourceSettings, kRoleKey, kFeatureGroupRole);
	obs_source_update(source, sourceSettings);
	obs_data_release(sourceSettings);
	obs_data_t *itemSettings = obs_sceneitem_get_private_settings(item);
	obs_data_set_string(itemSettings, kOwnerKey, kOwnerValue);
	obs_data_set_string(itemSettings, kFeatureKey, feature);
	obs_data_set_string(itemSettings, kRoleKey, kFeatureGroupRole);
	obs_data_release(itemSettings);
}

obs_sceneitem_t *findManagedFeatureGroup(obs_scene_t *scene, const char *feature)
{
	if (!scene) return nullptr;
	struct Search { const char *feature; obs_sceneitem_t *item; } search{feature, nullptr};
	obs_scene_enum_items(scene, [](obs_scene_t *, obs_sceneitem_t *item, void *parameter) {
		auto *search = static_cast<Search *>(parameter);
		if (!isManagedFeatureGroup(obs_sceneitem_get_source(item), search->feature)) return true;
		search->item = item;
		return false;
	}, &search);
	return search.item;
}

obs_sceneitem_t *ensureManagedFeatureGroup(obs_scene_t *scene, const char *name, const char *feature)
{
	if (!scene) return nullptr;
	if (obs_sceneitem_t *managed = findManagedFeatureGroup(scene, feature)) return managed;
	obs_source_t *named = obs_get_source_by_name(name);
	if (named) {
		obs_source_release(named);
		return nullptr;
	}
	obs_sceneitem_t *group = obs_scene_add_group(scene, name);
	if (group) markManagedFeatureGroup(group, feature);
	return group;
}

obs_sceneitem_t *ensureSimpleSceneInstance(obs_source_t *simpleSource, obs_source_t **activeSourceOut = nullptr)
{
	obs_source_t *activeSource = obs_frontend_get_current_scene();
	if (activeSourceOut) *activeSourceOut = activeSource;
	if (!activeSource || activeSource == simpleSource) return nullptr;
	obs_scene_t *activeScene = obs_scene_from_source(activeSource);
	obs_sceneitem_t *parent = findSourceItem(activeScene, simpleSource);
	if (!parent && activeScene) parent = obs_scene_add(activeScene, simpleSource);
	if (parent) {
		markSimpleParentItem(parent);
		obs_sceneitem_set_visible(parent, true);
	}
	return parent;
}

obs_source_t *findManagedSimpleSceneSource()
{
	obs_frontend_source_list scenes{};
	obs_frontend_get_scenes(&scenes);
	obs_source_t *result = nullptr;
	for (size_t index = 0; index < scenes.sources.num; ++index) {
		obs_source_t *source = scenes.sources.array[index];
		if (!isManagedSimpleScene(source)) continue;
		result = obs_source_get_ref(source);
		break;
	}
	obs_frontend_source_list_free(&scenes);
	return result;
}

obs_source_t *ensureManagedSimpleSceneSource()
{
	if (obs_source_t *managed = findManagedSimpleSceneSource()) return managed;
	obs_source_t *named = obs_get_source_by_name(kSimpleSceneName);
	if (named) {
		if (isManagedSimpleScene(named)) return named;
		obs_source_release(named);
		return nullptr;
	}
	obs_scene_t *scene = obs_scene_create(kSimpleSceneName);
	if (!scene) return nullptr;
	obs_source_t *source = obs_scene_get_source(scene);
	markManagedSimpleScene(source);
	obs_source_t *result = obs_source_get_ref(source);
	obs_scene_release(scene);
	return result;
}

obs_sceneitem_t *timerSceneItem(obs_source_t *source, obs_source_t **sceneSourceOut = nullptr,
				bool *effectivelyVisible = nullptr)
{
	obs_source_t *sceneSource = obs_frontend_get_current_scene();
	if (sceneSourceOut) *sceneSourceOut = sceneSource;
	if (!sceneSource) return nullptr;
	obs_scene_t *scene = obs_scene_from_source(sceneSource);
	return scene ? findSourceItem(scene, source, effectivelyVisible) : nullptr;
}

QJsonObject timerStatusFor(obs_source_t *source, bool conflict, const QString &message = {})
{
	bool placed = false, visible = false;
	obs_source_t *sceneSource = nullptr;
	if (source && !conflict) {
		if (obs_sceneitem_t *item = timerSceneItem(source, &sceneSource, &visible)) {
			placed = obs_sceneitem_get_source(item) == source;
			visible = placed && visible;
		}
	} else {
		timerSceneItem(nullptr, &sceneSource);
	}
	if (sceneSource) obs_source_release(sceneSource);
	QJsonObject result{{"sourceExists", source != nullptr}, {"placedInCurrentScene", placed},
		{"visibleInCurrentScene", visible}, {"conflict", conflict},
		{"setupComplete", source != nullptr && !conflict}, {"placementMode", "advanced"}};
	result["message"] = message.isEmpty()
		? (conflict ? QString("A source named %1 already exists but is not managed by the Suite.").arg(kTimerSourceName)
			: !source ? QString("Connected to OBS. Timer will be created automatically when started or shown.")
			: !placed ? QString("Connected to OBS. Timer will be added to this scene when started or shown.")
			: visible ? QString("Timer is visible in the current scene.")
			: QString("Timer is ready in the current scene."))
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

obs_sceneitem_t *currentSceneItem(obs_source_t *source, obs_source_t **sceneSourceOut = nullptr,
				  bool *effectivelyVisible = nullptr)
{
	obs_source_t *sceneSource = obs_frontend_get_current_scene();
	if (sceneSourceOut) *sceneSourceOut = sceneSource;
	if (!sceneSource) return nullptr;
	obs_scene_t *scene = obs_scene_from_source(sceneSource);
	if (!scene || obs_scene_get_source(scene) == source) return nullptr;
	return findSourceItem(scene, source, effectivelyVisible);
}

QJsonObject statusFor(obs_source_t *source, bool conflict, const QString &message = {})
{
	bool placed = false, visible = false;
	obs_source_t *sceneSource = nullptr;
	if (source && !conflict) {
		if (obs_sceneitem_t *item = currentSceneItem(source, &sceneSource, &visible)) {
			placed = obs_sceneitem_get_source(item) == source;
			visible = placed && visible;
		}
	} else {
		currentSceneItem(nullptr, &sceneSource);
	}
	if (sceneSource) obs_source_release(sceneSource);
	QJsonObject result{{"sourceExists", source != nullptr}, {"placedInCurrentScene", placed},
		{"visibleInCurrentScene", visible}, {"conflict", conflict},
		{"setupComplete", source != nullptr && !conflict}, {"placementMode", "advanced"}};
	result["message"] = message.isEmpty()
		? (conflict ? QString("A source named %1 already exists but is not managed by the Suite.").arg(kSourceName)
			: !source ? QString("Connected to OBS. Quick Text will be created automatically when shown.")
			: !placed ? QString("Connected to OBS. Quick Text will be added to this scene when shown.")
			: visible ? QString("Quick Text is visible in the current scene.")
			: QString("Quick Text is ready in the current scene."))
		: message;
	return result;
}

QJsonObject simpleQuickTextStatusFor(obs_source_t *source, bool sourceConflict, const QString &message = {})
{
	obs_source_t *simpleSource = findManagedSimpleSceneSource();
	bool sceneConflict = false;
	if (!simpleSource) {
		obs_source_t *named = obs_get_source_by_name(kSimpleSceneName);
		sceneConflict = named && !isManagedSimpleScene(named);
		if (named) obs_source_release(named);
	}
	bool setupComplete = false, placed = false, visible = false;
	if (source && simpleSource && !sourceConflict && !sceneConflict) {
		obs_scene_t *simpleScene = obs_scene_from_source(simpleSource);
		obs_sceneitem_t *group = findManagedFeatureGroup(simpleScene, kFeatureValue);
		obs_scene_t *groupScene = group ? obs_sceneitem_group_get_scene(group) : nullptr;
		bool innerVisible = false;
		obs_sceneitem_t *inner = findSourceItem(groupScene, source, &innerVisible);
		setupComplete = group && inner;
		obs_source_t *activeSource = obs_frontend_get_current_scene();
		obs_scene_t *activeScene = activeSource ? obs_scene_from_source(activeSource) : nullptr;
		bool parentVisible = false;
		obs_sceneitem_t *parent = activeSource == simpleSource ? nullptr :
			findSourceItem(activeScene, simpleSource, &parentVisible);
		placed = group && inner && (activeSource == simpleSource || parent);
		visible = placed && obs_sceneitem_visible(group) && innerVisible &&
			(activeSource == simpleSource || parentVisible);
		if (activeSource) obs_source_release(activeSource);
	}
	const bool conflict = sourceConflict || sceneConflict;
	QJsonObject result{{"sourceExists", source != nullptr}, {"placedInCurrentScene", placed},
		{"visibleInCurrentScene", visible}, {"conflict", conflict}, {"setupComplete", setupComplete},
		{"placementMode", "simple"}};
	result["message"] = message.isEmpty()
		? (sourceConflict ? QString("A source named %1 already exists but is not managed by the Suite.").arg(kSourceName)
			: sceneConflict ? QString("A source named %1 already exists but is not managed by the Suite.").arg(kSimpleSceneName)
			: !source || !simpleSource ? QString("Connected to OBS. Quick Text will be created automatically when shown.")
			: !placed ? QString("Connected to OBS. Quick Text will be added to this scene when shown.")
			: visible ? QString("Quick Text is visible in the current scene.")
			: QString("Quick Text is ready in the current scene."))
		: message;
	if (simpleSource) obs_source_release(simpleSource);
	return result;
}

QJsonObject simpleTimerStatusFor(obs_source_t *source, bool sourceConflict, const QString &message = {})
{
	obs_source_t *simpleSource = findManagedSimpleSceneSource();
	bool sceneConflict = false;
	if (!simpleSource) {
		obs_source_t *named = obs_get_source_by_name(kSimpleSceneName);
		sceneConflict = named && !isManagedSimpleScene(named);
		if (named) obs_source_release(named);
	}
	bool setupComplete = false, placed = false, visible = false;
	if (source && simpleSource && !sourceConflict && !sceneConflict) {
		obs_scene_t *simpleScene = obs_scene_from_source(simpleSource);
		obs_sceneitem_t *group = findManagedFeatureGroup(simpleScene, "timer");
		obs_scene_t *groupScene = group ? obs_sceneitem_group_get_scene(group) : nullptr;
		bool innerVisible = false;
		obs_sceneitem_t *inner = findSourceItem(groupScene, source, &innerVisible);
		setupComplete = group && inner;
		obs_source_t *activeSource = obs_frontend_get_current_scene();
		obs_scene_t *activeScene = activeSource ? obs_scene_from_source(activeSource) : nullptr;
		bool parentVisible = false;
		obs_sceneitem_t *parent = activeSource == simpleSource ? nullptr :
			findSourceItem(activeScene, simpleSource, &parentVisible);
		placed = group && inner && (activeSource == simpleSource || parent);
		visible = placed && obs_sceneitem_visible(group) && innerVisible &&
			(activeSource == simpleSource || parentVisible);
		if (activeSource) obs_source_release(activeSource);
	}
	const bool conflict = sourceConflict || sceneConflict;
	QJsonObject result{{"sourceExists", source != nullptr}, {"placedInCurrentScene", placed},
		{"visibleInCurrentScene", visible}, {"conflict", conflict}, {"setupComplete", setupComplete},
		{"placementMode", "simple"}};
	result["message"] = message.isEmpty()
		? (sourceConflict ? QString("A source named %1 already exists but is not managed by the Suite.").arg(kTimerSourceName)
			: sceneConflict ? QString("A source named %1 already exists but is not managed by the Suite.").arg(kSimpleSceneName)
			: !source || !simpleSource ? QString("Connected to OBS. Timer will be created automatically when started or shown.")
			: !placed ? QString("Connected to OBS. Timer will be added to this scene when started or shown.")
			: visible ? QString("Timer is visible in the current scene.")
			: QString("Timer is ready in the current scene."))
		: message;
	if (simpleSource) obs_source_release(simpleSource);
	return result;
}

void applyBrowserSettings(obs_source_t *source)
{
	obs_data_t *settings = obs_source_get_settings(source);
	obs_data_set_string(settings, "url", RsStreamOverlayServer::instance().quickTextUrl().toUtf8().constData());
	obs_data_set_bool(settings, "is_local_file", false);
	obs_data_set_int(settings, "width", kQuickTextWidth);
	obs_data_set_int(settings, "height", kQuickTextHeight);
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

void RsStreamOverlayManager::setPlacementMode(const QString &mode)
{
	s_placementMode = mode.compare(QStringLiteral("simple"), Qt::CaseInsensitive) == 0
		? QStringLiteral("simple") : QStringLiteral("advanced");
}

QString RsStreamOverlayManager::placementMode()
{
	return s_placementMode;
}

bool RsStreamOverlayManager::simplePlacementEnabled()
{
	return s_placementMode == QStringLiteral("simple");
}

QJsonObject RsStreamOverlayManager::quickTextStatus()
{
	obs_source_t *source = obs_get_source_by_name(kSourceName);
	const bool conflict = source && !isManagedQuickText(source);
	const QJsonObject result = simplePlacementEnabled()
		? simpleQuickTextStatusFor(source, conflict) : statusFor(source, conflict);
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
		obs_data_set_int(settings, "width", kQuickTextWidth);
		obs_data_set_int(settings, "height", kQuickTextHeight);
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
	if (simplePlacementEnabled()) {
		obs_source_t *simpleSource = ensureManagedSimpleSceneSource();
		if (!simpleSource) {
			const QJsonObject result = simpleQuickTextStatusFor(source, false);
			obs_source_release(source);
			return result;
		}
		obs_scene_t *simpleScene = obs_scene_from_source(simpleSource);
		obs_sceneitem_t *group = ensureManagedFeatureGroup(simpleScene, kQuickTextGroupName, kFeatureValue);
		obs_scene_t *groupScene = group ? obs_sceneitem_group_get_scene(group) : nullptr;
		obs_sceneitem_t *inner = findSourceItem(groupScene, source);
		if (!inner && groupScene) inner = obs_scene_add(groupScene, source);
		if (inner) obs_sceneitem_set_visible(inner, true);
		if (group) obs_sceneitem_set_visible(group, true);
		obs_source_t *activeSource = nullptr;
		obs_sceneitem_t *parent = ensureSimpleSceneInstance(simpleSource, &activeSource);
		const bool ready = group && inner && (activeSource == simpleSource || parent);
		const QJsonObject result = ready ? simpleQuickTextStatusFor(source, false)
			: simpleQuickTextStatusFor(source, false, "Quick Text exists, but OBS could not place the managed Stream Overlays scene here.");
		if (activeSource) obs_source_release(activeSource);
		obs_source_release(simpleSource);
		obs_source_release(source);
		return result;
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
	if (simplePlacementEnabled()) {
		obs_source_t *simpleSource = findManagedSimpleSceneSource();
		if (simpleSource) {
			obs_scene_t *simpleScene = obs_scene_from_source(simpleSource);
			if (obs_sceneitem_t *group = findManagedFeatureGroup(simpleScene, kFeatureValue))
				obs_sceneitem_set_visible(group, false);
			obs_source_release(simpleSource);
		}
		const QJsonObject result = simpleQuickTextStatusFor(source, false);
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
	const QJsonObject result = simplePlacementEnabled()
		? simpleTimerStatusFor(source, conflict) : timerStatusFor(source, conflict);
	if (source) obs_source_release(source);
	return result;
}

QJsonObject RsStreamOverlayManager::setupTimerInCurrentScene()
{
	if (!RsStreamOverlayServer::instance().port())
		return simplePlacementEnabled()
			? simpleTimerStatusFor(nullptr, false, "Timer cannot start because the stream-overlay service is unavailable.")
			: timerStatusFor(nullptr, false, "Timer cannot start because the stream-overlay service is unavailable.");
	obs_source_t *source = obs_get_source_by_name(kTimerSourceName);
	if (source && !isManagedTimer(source)) {
		const QJsonObject result = simplePlacementEnabled()
			? simpleTimerStatusFor(source, true) : timerStatusFor(source, true);
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
		if (!source)
			return simplePlacementEnabled()
				? simpleTimerStatusFor(nullptr, false, "OBS could not create the managed Timer browser source.")
				: timerStatusFor(nullptr, false, "OBS could not create the managed Timer browser source.");
		obs_source_set_monitoring_type(source, OBS_MONITORING_TYPE_MONITOR_AND_OUTPUT);
	} else {
		applyTimerSettings(source);
	}
	if (simplePlacementEnabled()) {
		obs_source_t *simpleSource = ensureManagedSimpleSceneSource();
		if (!simpleSource) {
			const QJsonObject result = simpleTimerStatusFor(source, false);
			obs_source_release(source);
			return result;
		}
		obs_scene_t *simpleScene = obs_scene_from_source(simpleSource);
		obs_sceneitem_t *group = ensureManagedFeatureGroup(simpleScene, kTimerGroupName, "timer");
		obs_scene_t *groupScene = group ? obs_sceneitem_group_get_scene(group) : nullptr;
		obs_sceneitem_t *inner = findSourceItem(groupScene, source);
		if (!inner && groupScene) inner = obs_scene_add(groupScene, source);
		if (inner) obs_sceneitem_set_visible(inner, true);
		if (group) obs_sceneitem_set_visible(group, false);
		obs_source_t *activeSource = nullptr;
		obs_sceneitem_t *parent = ensureSimpleSceneInstance(simpleSource, &activeSource);
		const bool ready = group && inner && (activeSource == simpleSource || parent);
		const QJsonObject result = ready ? simpleTimerStatusFor(source, false)
			: simpleTimerStatusFor(source, false, "Timer exists, but OBS could not place the managed Stream Overlays scene here.");
		if (activeSource) obs_source_release(activeSource);
		obs_source_release(simpleSource);
		obs_source_release(source);
		return result;
	}
	obs_source_t *sceneSource = nullptr;
	obs_sceneitem_t *item = timerSceneItem(source, &sceneSource);
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
	if (!source) return simplePlacementEnabled() ? simpleTimerStatusFor(nullptr, false) : timerStatusFor(nullptr, false);
	if (!isManagedTimer(source)) {
		const QJsonObject result = simplePlacementEnabled()
			? simpleTimerStatusFor(source, true) : timerStatusFor(source, true);
		obs_source_release(source);
		return result;
	}
	if (simplePlacementEnabled()) {
		obs_source_t *simpleSource = findManagedSimpleSceneSource();
		if (simpleSource) {
			obs_scene_t *simpleScene = obs_scene_from_source(simpleSource);
			obs_sceneitem_t *group = findManagedFeatureGroup(simpleScene, "timer");
			obs_scene_t *groupScene = group ? obs_sceneitem_group_get_scene(group) : nullptr;
			obs_sceneitem_t *inner = findSourceItem(groupScene, source);
			if (visible) {
				obs_source_t *activeSource = nullptr;
				obs_sceneitem_t *parent = ensureSimpleSceneInstance(simpleSource, &activeSource);
				if (inner) obs_sceneitem_set_visible(inner, true);
				if (group && (activeSource == simpleSource || parent)) obs_sceneitem_set_visible(group, true);
				if (activeSource) obs_source_release(activeSource);
			} else if (group) {
				obs_sceneitem_set_visible(group, false);
			}
			obs_source_release(simpleSource);
		}
		const QJsonObject result = simpleTimerStatusFor(source, false);
		obs_source_release(source);
		return result;
	}
	obs_source_t *sceneSource = nullptr;
	obs_sceneitem_t *item = timerSceneItem(source, &sceneSource);
	obs_scene_t *scene = sceneSource ? obs_scene_from_source(sceneSource) : nullptr;
	if (visible && !item && scene) item = obs_scene_add(scene, source);
	if (item && obs_sceneitem_get_source(item) == source) obs_sceneitem_set_visible(item, visible);
	if (sceneSource) obs_source_release(sceneSource);
	const QJsonObject result = timerStatusFor(source, false);
	obs_source_release(source);
	return result;
}
