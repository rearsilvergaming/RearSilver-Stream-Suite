#pragma once

#include <QString>

enum class RsProductEdition { Free, Pro };

enum class RsFeature {
	CoreDock,
	SceneAndSourceTools,
	StreamControls,
	StreamStatistics,
	BrowserRefresh,
	QuickText,
	Timer,
	AutoStart,
	InstantReplay,
	MusicCore,
	MusicRequests,
	MusicSpotify,
	MusicYouTube,
	MusicLocalFiles,
	MusicDesktopMedia,
	MusicOverlays
};

class RsEntitlements {
public:
	static RsProductEdition edition();
	static QString editionKey();
	static bool has(RsFeature feature);
	static QString featureKey(RsFeature feature);
};
