#pragma once

#include <QString>

enum class RsMusicProvider { Unknown, YouTube, Spotify, LocalFile, SystemMedia };

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

/* Provider-neutral track shared by playback, queues, overlays and UI. */
struct RsMusicTrack {
	QString trackId;
	RsMusicProvider provider = RsMusicProvider::Unknown;
	QString providerTrackId;
	QString providerUri;
	QString title;
	QString artist;
	QString artworkUri;
	int durationSeconds = 0;
	QString requestedById;
	QString requestedBy;
	bool isFromPlaylist = false;
	qint64 enqueuedTimestampMs = 0;
};
