#include "rs_entitlements.hpp"

RsProductEdition RsEntitlements::edition()
{
#ifdef RS_DEFAULT_EDITION_FREE
	return RsProductEdition::Free;
#else
	return RsProductEdition::Pro;
#endif
}

QString RsEntitlements::editionKey()
{
	return edition() == RsProductEdition::Pro ? "pro" : "free";
}

bool RsEntitlements::has(RsFeature feature)
{
	if (edition() == RsProductEdition::Pro)
		return true;

	switch (feature) {
	case RsFeature::CoreDock:
	case RsFeature::SceneAndSourceTools:
	case RsFeature::StreamControls:
	case RsFeature::StreamStatistics:
		return true;
	default:
		return false;
	}
}

QString RsEntitlements::featureKey(RsFeature feature)
{
	switch (feature) {
	case RsFeature::CoreDock: return "core.dock";
	case RsFeature::SceneAndSourceTools: return "core.scenes_sources";
	case RsFeature::StreamControls: return "core.stream_controls";
	case RsFeature::StreamStatistics: return "core.stream_statistics";
	case RsFeature::BrowserRefresh: return "enhancement.browser_refresh";
	case RsFeature::QuickText: return "enhancement.quick_text";
	case RsFeature::Timer: return "enhancement.timer";
	case RsFeature::AutoStart: return "enhancement.auto_start";
	case RsFeature::InstantReplay: return "enhancement.instant_replay";
	case RsFeature::MusicCore: return "music.core";
	case RsFeature::MusicRequests: return "music.requests";
	case RsFeature::MusicSpotify: return "music.spotify";
	case RsFeature::MusicYouTube: return "music.youtube";
	case RsFeature::MusicLocalFiles: return "music.local_files";
	case RsFeature::MusicDesktopMedia: return "music.desktop_media";
	case RsFeature::MusicOverlays: return "music.overlays";
	}
	return "unknown";
}
