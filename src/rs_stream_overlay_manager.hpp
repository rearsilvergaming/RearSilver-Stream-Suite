#pragma once

#include <QJsonObject>

class RsStreamOverlayManager {
public:
	static QJsonObject quickTextStatus();
	static QJsonObject showQuickTextInCurrentScene();
	static QJsonObject clearQuickTextInCurrentScene();
};
