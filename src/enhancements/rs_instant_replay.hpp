#pragma once

#include <QWidget>
#include <QString>

extern "C" {
#include <obs.h>
#include <obs-frontend-api.h>
}

class RsMainDock;

// ------------------------------------------------------------
// Instant Replay helper + UI
// ------------------------------------------------------------
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
};

