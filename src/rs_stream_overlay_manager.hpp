#pragma once

#include <QJsonObject>
#include <QString>

extern "C" {
#include <obs.h>
}

class RsStreamOverlayManager {
public:
	static void setPlacementMode(const QString &mode);
	static QString placementMode();
	static bool simplePlacementEnabled();
	// Returned sources own one reference and must be released by the caller.
	static obs_source_t *managedSimpleSceneSource(bool create, bool *conflict = nullptr);
	static obs_sceneitem_t *ensureManagedSimpleSceneInCurrentScene(obs_source_t *simpleSource,
		obs_source_t **activeSourceOut = nullptr, bool *cycleBlocked = nullptr);
	static bool wouldCreateSceneCycle(obs_scene_t *parentScene, obs_source_t *childSource);
	static QJsonObject quickTextStatus();
	static QJsonObject showQuickTextInCurrentScene();
	static QJsonObject clearQuickTextInCurrentScene();
	static QJsonObject timerStatus();
	static QJsonObject setupTimerInCurrentScene();
	static QJsonObject setTimerVisibleInCurrentScene(bool visible);
};
