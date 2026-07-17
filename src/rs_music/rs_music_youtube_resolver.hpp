#pragma once

#include <QObject>
#include <QJsonObject>
#include <QString>
#include <QUrl>
#include <QVector>
#include <functional>

#include "state/rs_music_track.hpp"

class QNetworkAccessManager;

class RsMusicYouTubeResolver final : public QObject {
public:
	using TrackCallback = std::function<void(const RsMusicTrack &, const QString &)>;
	using PlaylistCallback = std::function<void(const QVector<RsMusicTrack> &, const QString &, const QString &)>;
	using ArtworkCallback = std::function<void(const QString &)>;

	explicit RsMusicYouTubeResolver(QObject *parent = nullptr);
	void resolveSearch(const QString &query, TrackCallback callback);
	void importPlaylist(const QString &playlistUrl, PlaylistCallback callback);
	void cacheArtwork(const RsMusicTrack &track, ArtworkCallback callback);

private:
	QUrl requestUrl(const QString &operation) const;
	static RsMusicTrack parseTrack(const QJsonObject &object);
	QNetworkAccessManager *m_network = nullptr;
};
