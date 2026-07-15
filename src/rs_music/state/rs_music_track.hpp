#pragma once

#include <QString>

enum class RsMusicProvider { Unknown, YouTube, Spotify, LocalFile, SystemMedia };

struct RsMusicProviderCapabilities {
	bool canSearch = false;
	bool canQueue = false;
	bool canPlayPause = false;
	bool canSkip = false;
	bool canPrevious = false;
	bool canSeek = false;
	bool canSetVolume = false;
	bool providesMetadata = false;
	bool usesExternalAudio = false;
	bool requiresAuthentication = false;
};

inline QString rsMusicProviderKey(RsMusicProvider provider)
{
	switch (provider) {
	case RsMusicProvider::YouTube: return "youtube";
	case RsMusicProvider::Spotify: return "spotify";
	case RsMusicProvider::LocalFile: return "local";
	case RsMusicProvider::SystemMedia: return "system";
	case RsMusicProvider::Unknown: break;
	}
	return "unknown";
}

inline QString rsMusicProviderDisplayName(RsMusicProvider provider)
{
	switch (provider) {
	case RsMusicProvider::YouTube: return "YouTube";
	case RsMusicProvider::Spotify: return "Spotify";
	case RsMusicProvider::LocalFile: return "Local files";
	case RsMusicProvider::SystemMedia: return "Desktop media";
	case RsMusicProvider::Unknown: break;
	}
	return "Unresolved";
}

inline RsMusicProviderCapabilities rsMusicProviderCapabilities(RsMusicProvider provider)
{
	RsMusicProviderCapabilities capabilities;
	switch (provider) {
	case RsMusicProvider::YouTube:
		capabilities = {true, true, true, true, false, true, true, true, false, false};
		break;
	case RsMusicProvider::Spotify:
		capabilities = {true, true, true, true, true, true, true, true, true, true};
		break;
	case RsMusicProvider::LocalFile:
		capabilities = {true, true, true, true, true, true, true, true, false, false};
		break;
	case RsMusicProvider::SystemMedia:
		capabilities = {false, false, true, true, true, true, false, true, true, false};
		break;
	case RsMusicProvider::Unknown:
		break;
	}
	return capabilities;
}

/* Provider-neutral track shared by playback, queues, overlays and UI. */
struct RsMusicTrack {
	QString trackId;
	RsMusicProvider provider = RsMusicProvider::Unknown;
	QString providerTrackId;
	QString providerUri;
	QString title;
	QString artist;
	QString album;
	QString artworkUri;
	int durationSeconds = 0;
	QString requestedById;
	QString requestedBy;
	bool isFromPlaylist = false;
	qint64 enqueuedTimestampMs = 0;
};
