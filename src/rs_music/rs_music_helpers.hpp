#pragma once

#include <QString>
#include "state/rs_music_track.hpp"

struct RsMusicRequestTarget {
	RsMusicProvider provider = RsMusicProvider::Unknown;
	QString providerTrackId;
	QString providerUri;
	QString searchQuery;
	bool directTrack = false;
	bool unsupportedUrl = false;
};

// Normalise a channel name for use as a room id.
QString rsMusicNormaliseChannelName(const QString &input);

// Extract YouTube video ID from common URL forms.
// Returns empty string if input is not a recognised YouTube video URL.
QString rsMusicExtractYoutubeVideoId(const QString &input);

// Extract a Spotify track ID from open.spotify.com/track links or spotify:track URIs.
QString rsMusicExtractSpotifyTrackId(const QString &input);

// Parse a request without assuming which provider will ultimately resolve it.
RsMusicRequestTarget rsMusicParseRequestTarget(const QString &input);

// Lightweight heuristic to detect URLs (used by !sr).
bool rsMusicLooksLikeUrl(const QString &input);
