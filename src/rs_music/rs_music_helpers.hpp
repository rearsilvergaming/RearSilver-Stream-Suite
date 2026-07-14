#pragma once

#include <QString>

// Normalise a channel name for use as a room id.
QString rsMusicNormaliseChannelName(const QString &input);

// Extract YouTube video ID from common URL forms.
// Returns empty string if input is not a recognised YouTube video URL.
QString rsMusicExtractYoutubeVideoId(const QString &input);

// Lightweight heuristic to detect URLs (used by !sr).
bool rsMusicLooksLikeUrl(const QString &input);
