#include "rs_music_youtube_resolver.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QUrlQuery>

RsMusicYouTubeResolver::RsMusicYouTubeResolver(QObject *parent) : QObject(parent)
{
	m_network = new QNetworkAccessManager(this);
}

QUrl RsMusicYouTubeResolver::requestUrl(const QString &operation) const
{
	QSettings settings("RearSilver", "RearSilver-Stream-Suite");
	QString base = settings.value("music/youtube/resolverEndpoint",
		"https://rearsilver-youtube-resolver.rearsilver.workers.dev/v1/youtube").toString().trimmed();
	while (base.endsWith('/')) base.chop(1);
	return QUrl(base + "/" + operation);
}

RsMusicTrack RsMusicYouTubeResolver::parseTrack(const QJsonObject &object)
{
	RsMusicTrack track;
	track.provider = RsMusicProvider::YouTube;
	track.providerTrackId = object.value("videoId").toString().trimmed();
	track.trackId = QString("youtube_%1").arg(track.providerTrackId);
	track.providerUri = QString("https://www.youtube.com/watch?v=%1").arg(track.providerTrackId);
	track.title = object.value("title").toString();
	track.artist = object.value("artist").toString();
	track.artworkUri = object.value("thumbnail").toString();
	track.durationSeconds = object.value("durationSeconds").toInt();
	return track;
}

void RsMusicYouTubeResolver::resolveSearch(const QString &query, TrackCallback callback)
{
	QUrl url = requestUrl("search");
	QUrlQuery parameters; parameters.addQueryItem("q", query.trimmed()); url.setQuery(parameters);
	QNetworkReply *reply = m_network->get(QNetworkRequest(url));
	connect(reply, &QNetworkReply::finished, this, [reply, callback = std::move(callback)]() mutable {
		QString error; RsMusicTrack track;
		const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
		error = root.value("error").toString();
		if (error.isEmpty() && reply->error() != QNetworkReply::NoError) error = reply->errorString();
		if (error.isEmpty()) track = parseTrack(root.value("track").toObject());
		if (track.providerTrackId.isEmpty() && error.isEmpty()) error = "No playable YouTube result was returned.";
		reply->deleteLater(); callback(track, error);
	});
}

void RsMusicYouTubeResolver::importPlaylist(const QString &playlistUrl, PlaylistCallback callback)
{
	QUrl url = requestUrl("playlist");
	QUrlQuery parameters; parameters.addQueryItem("url", playlistUrl.trimmed()); url.setQuery(parameters);
	QNetworkReply *reply = m_network->get(QNetworkRequest(url));
	connect(reply, &QNetworkReply::finished, this, [reply, callback = std::move(callback)]() mutable {
		QString error, label; QVector<RsMusicTrack> tracks;
		const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
		error = root.value("error").toString();
		if (error.isEmpty() && reply->error() != QNetworkReply::NoError) error = reply->errorString();
		if (error.isEmpty()) {
			label = root.value("title").toString();
			for (const QJsonValue &value : root.value("tracks").toArray()) {
				RsMusicTrack track = parseTrack(value.toObject());
				if (!track.providerTrackId.isEmpty()) { track.isFromPlaylist = true; tracks.append(track); }
			}
			if (tracks.isEmpty() && error.isEmpty()) error = "The playlist contained no playable videos.";
		}
		reply->deleteLater(); callback(tracks, label, error);
	});
}

void RsMusicYouTubeResolver::cacheArtwork(const RsMusicTrack &track, ArtworkCallback callback)
{
	const QUrl source(track.artworkUri);
	if (!source.isValid() || (source.scheme() != "http" && source.scheme() != "https")) {
		callback(track.artworkUri);
		return;
	}

	const QString folder = QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
		.filePath("youtube-artwork");
	QDir().mkpath(folder);
	const QString key = QString::fromLatin1(QCryptographicHash::hash(
		track.providerTrackId.toUtf8(), QCryptographicHash::Sha256).toHex());
	const QString path = QDir(folder).filePath(key + ".img");
	if (QFileInfo::exists(path)) {
		callback(QUrl::fromLocalFile(path).toString());
		return;
	}

	QNetworkReply *reply = m_network->get(QNetworkRequest(source));
	connect(reply, &QNetworkReply::finished, this, [reply, path, callback = std::move(callback)]() mutable {
		QString result;
		const QByteArray bytes = reply->readAll();
		if (reply->error() == QNetworkReply::NoError && !bytes.isEmpty()) {
			QSaveFile output(path);
			if (output.open(QIODevice::WriteOnly) && output.write(bytes) == bytes.size() && output.commit())
				result = QUrl::fromLocalFile(path).toString();
		}
		reply->deleteLater();
		callback(result);
	});
}
