#pragma once

#include <QJsonObject>
#include <QString>

class RsStreamOverlayManager {
public:
	static void setPlacementMode(const QString &mode);
	static QString placementMode();
	static bool simplePlacementEnabled();
	static QJsonObject quickTextStatus();
	static QJsonObject showQuickTextInCurrentScene();
	static QJsonObject clearQuickTextInCurrentScene();
	static QJsonObject timerStatus();
	static QJsonObject setupTimerInCurrentScene();
	static QJsonObject setTimerVisibleInCurrentScene(bool visible);
};
