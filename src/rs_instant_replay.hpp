#pragma once

#include <QWidget>
#include <QString>
#include <QJsonObject>

extern "C" {
#include <obs.h>
#include <obs-frontend-api.h>
}

class RsMainDock;

// ------------------------------------------------------------
// Instant Replay helper + UI
// ------------------------------------------------------------
namespace hub_replay {

class RsInstantReplay {
public:
	// Ensure the replay media source exists in the current scene
	static void ensureReplaySource();

	// Play the most recent replay file
	static void playReplay(const QString &filePath);

	// Hide replay source (safe no-op if missing)
	static void hideReplaySource();

	// UI page (matches Timer / AutoStart pattern)
	static QWidget *createPage(RsMainDock *dock, QWidget *parent);

	// Register OBS frontend callbacks (called once)
	static void registerFrontendCallbacks();
	static void shutdown();

	// Trigger a replay save (Play button / hotkey)
	static void triggerReplay();

	// Add storage path override
	static QString replayFolderOverride();
	static void setReplayFolderOverride(const QString &path);
	static void ensureReplayBgSource();
	static int replaySeconds();
	static bool replayAutoStart();
	static bool replayAutoHide();
	static bool replayBufferActive();
	static void configureReplayBuffer(int seconds, bool autoStart, bool autoHide);
	static void startReplayBuffer();
	static void stopReplayBuffer();
	static void showReplaySource();
	static void repairReplaySource();
	static void configureReplayFrame(const QString &title, const QString &font, const QString &alignment,
					 const QString &background, const QString &accent,
					 const QString &textColour, int opacity,
					 const QString &sizeStep, const QString &borderStep, const QString &radiusStep);
	static QJsonObject replayState();
};

} // namespace hub_replay
