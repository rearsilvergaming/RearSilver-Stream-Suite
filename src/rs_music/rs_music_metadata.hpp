#pragma once

#include "state/rs_music_track.hpp"
#include <QByteArray>

class RsMusicMetadata {
public:
	static void enrichLocalTrack(RsMusicTrack &track, const QString &filePath);
	static QByteArray artworkBytes(const QString &artworkUri);
};
