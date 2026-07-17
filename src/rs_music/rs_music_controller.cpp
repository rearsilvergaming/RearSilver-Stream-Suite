#include "rs_music_controller.hpp"
#include "state/rs_music_state.hpp"
#include "rs_music_local_player.hpp"
#include "rs_music_helpers.hpp"
#include "rs_music_metadata.hpp"
#include "rs_music_youtube_resolver.hpp"

#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSettings>
#include <QTimer>

extern "C" {
#include <obs-module.h>
}

RsMusicController::RsMusicController(RsMusicState *state, QObject *parent) : QObject(parent), m_state(state)
{
	m_youtubeResolver = new RsMusicYouTubeResolver(this);
	QSettings settings("RearSilver", "RearSilver-Stream-Suite");
	m_localLibrary = settings.value("music/local/library").toStringList();
	if (m_state && !m_localLibrary.isEmpty()) {
		m_state->setActiveProvider(RsMusicProvider::LocalFile);
		m_state->setRequestsEnabled(false);
		rsMusicSetRequestsEnabled(false);
	}
	connect(&rsMusicBackendEvents(), &RsMusicBackendEvents::queueChanged, this,
		&RsMusicController::syncQueueFromBackend);
	connect(&RsMusicLocalPlayer::instance(), &RsMusicLocalPlayer::playbackStarted, this, [this]() {
		if (m_state && currentTrackUsesCompanion()) {
			m_state->setPlaybackStatus(RsMusicState::PlaybackStatus::Playing);
			if (m_state->currentTrack().provider == RsMusicProvider::YouTube && !m_youtubeCaptureRefreshed) {
				m_youtubeCaptureRefreshed = true;
				QTimer::singleShot(750, this, &RsMusicController::refreshYouTubeCaptureSource);
			}
		}
	});
	connect(&RsMusicLocalPlayer::instance(), &RsMusicLocalPlayer::playbackEnded, this, [this]() {
		if (m_state && currentTrackIsLocal())
			playNextLocalTrack();
		else if (m_state && currentTrackUsesCompanion() &&
			 m_state->currentTrack().provider != RsMusicProvider::YouTube) {
			m_state->setPlaybackStatus(RsMusicState::PlaybackStatus::Stopped);
			m_state->clearCurrentTrack();
		}
	});
	connect(&RsMusicLocalPlayer::instance(), &RsMusicLocalPlayer::playbackProgress, this,
		[this](qint64 positionMs, qint64 durationMs) {
			if (m_state && currentTrackUsesCompanion())
				m_state->setPlaybackProgress(positionMs, durationMs);
		});
	connect(&RsMusicLocalPlayer::instance(), &RsMusicLocalPlayer::playbackMetadata, this,
		[this](const QString &title, const QString &artist, qint64 durationMs) {
			if (!m_state || !m_state->hasCurrentTrack() ||
			    m_state->currentTrack().provider != RsMusicProvider::YouTube)
				return;
			RsMusicTrack track = m_state->currentTrack();
			track.title = title.trimmed().isEmpty() ? track.title : title.trimmed();
			track.artist = artist.trimmed();
			track.durationSeconds = static_cast<int>((qMax<qint64>(0, durationMs) + 999) / 1000);
			m_state->setCurrentTrack(track);
		});
	connect(&RsMusicLocalPlayer::instance(), &RsMusicLocalPlayer::hubStateReceived, this,
		[this](const QByteArray &payload) {
			if (!m_state) return;
			const QJsonObject root = QJsonDocument::fromJson(payload).object();
			auto trackFromJson = [](const QJsonObject &value) {
				RsMusicTrack track;
				track.trackId = value.value("id").toString();
				track.provider = RsMusicProvider::YouTube;
				track.providerTrackId = value.value("providerId").toString();
				track.providerUri = QString("https://www.youtube.com/watch?v=%1").arg(track.providerTrackId);
				track.title = value.value("title").toString();
				track.artist = value.value("artist").toString();
				track.artworkUri = value.value("artworkUrl").toString();
				track.requestedBy = value.value("requestedBy").toString();
				track.durationSeconds = value.value("durationSeconds").toInt();
				track.isFromPlaylist = !value.value("request").toBool();
				return track;
			};
			const QJsonValue currentValue = root.value("current");
			if (currentValue.isObject()) {
				RsMusicTrack track = trackFromJson(currentValue.toObject());
				const bool changed = !m_state->hasCurrentTrack() || m_state->currentTrack().trackId != track.trackId;
				if (!changed && m_state->currentTrack().artworkUri.startsWith("file:", Qt::CaseInsensitive))
					track.artworkUri = m_state->currentTrack().artworkUri;
				m_state->setCurrentTrack(track);
				if (changed) hydrateCurrentYouTubeArtwork(track);
			}
			else m_state->clearCurrentTrack();
			QVector<RsMusicTrack> queue;
			for (const QJsonValue &value : root.value("queue").toArray())
				if (value.isObject()) queue.append(trackFromJson(value.toObject()));
			m_state->setQueue(queue);
			m_state->setActiveProvider(RsMusicProvider::YouTube);
			m_state->setPlaylistLabel(root.value("fallbackLabel").toString("YouTube fallback"));
			m_state->setPlaybackProgress(root.value("positionMs").toVariant().toLongLong(),
				root.value("durationMs").toVariant().toLongLong());
			const QString status = root.value("status").toString();
			m_state->setPlaybackStatus(status == "playing" ? RsMusicState::PlaybackStatus::Playing :
				(status == "paused" ? RsMusicState::PlaybackStatus::Paused : RsMusicState::PlaybackStatus::Stopped));
		});
	syncQueueFromBackend();
}

bool RsMusicController::currentTrackUsesCompanion() const
{
	if (!m_state || !m_state->hasCurrentTrack())
		return false;
	const RsMusicProvider provider = m_state->currentTrack().provider;
	return provider == RsMusicProvider::LocalFile || provider == RsMusicProvider::YouTube;
}

bool RsMusicController::currentTrackIsLocal() const
{
	return m_state && m_state->hasCurrentTrack() &&
	       m_state->currentTrack().provider == RsMusicProvider::LocalFile;
}

void RsMusicController::syncQueueFromBackend()
{
	if (!m_state)
		return;

	const QVector<RsMusicTrack> tracks = rsMusicPlaybackQueueSnapshot();
	m_state->setQueue(tracks);
}

void RsMusicController::actionPlay()
{
	if (!m_state)
		return;

	if (!m_state->hasCurrentTrack() && m_state->activeProvider() == RsMusicProvider::LocalFile &&
	    !m_localLibrary.isEmpty()) {
		playLocalIndex(0);
		return;
	}
	if (!m_state->hasCurrentTrack() && m_state->activeProvider() == RsMusicProvider::YouTube) {
		RsMusicLocalPlayer::instance().resume(); return;
	}

	if (currentTrackUsesCompanion())
		RsMusicLocalPlayer::instance().resume();
	else
		rsMusicResume();
	m_state->setPlaybackStatus(RsMusicState::PlaybackStatus::Playing);
}

void RsMusicController::actionPause()
{
	if (!m_state)
		return;

	if (currentTrackUsesCompanion())
		RsMusicLocalPlayer::instance().pause();
	else
		rsMusicPause();
	m_state->setPlaybackStatus(RsMusicState::PlaybackStatus::Paused);
}

void RsMusicController::actionStop()
{
	if (!m_state)
		return;

	if (currentTrackUsesCompanion()) {
		RsMusicLocalPlayer::instance().stop();
		m_state->clearCurrentTrack();
	} else {
		rsMusicStop();
	}
	m_state->setPlaybackStatus(RsMusicState::PlaybackStatus::Stopped);
}

void RsMusicController::actionRestart()
{
	if (!m_state)
		return;

	if (currentTrackUsesCompanion())
		RsMusicLocalPlayer::instance().restart();
	else
		rsMusicRestart();
	m_state->setPlaybackStatus(RsMusicState::PlaybackStatus::Playing);
}

void RsMusicController::actionSkip(const QString &source)
{
	if (currentTrackIsLocal()) {
		playNextLocalTrack();
		return;
	}
	if (currentTrackUsesCompanion()) {
		RsMusicLocalPlayer::instance().skip();
		return;
	}
	rsMusicSkip(source);
}

bool RsMusicController::actionPlayLocalFile(const QString &filePath)
{
	const QString absolutePath = QFileInfo(filePath).absoluteFilePath();
	int index = m_localLibrary.indexOf(absolutePath);
	if (index < 0) {
		m_localLibrary.append(absolutePath);
		setLocalLibrary(m_localLibrary);
		index = m_localLibrary.indexOf(absolutePath);
	}
	return playLocalIndex(index);
}

bool RsMusicController::actionPlayYouTubeVideo(const QString &url)
{
	if (!m_state)
		return false;
	const QString videoId = rsMusicExtractYoutubeVideoId(url);
	if (videoId.isEmpty())
		return false;
	RsMusicTrack track;
	track.trackId = QString("youtube_%1").arg(videoId);
	track.provider = RsMusicProvider::YouTube;
	track.providerTrackId = videoId;
	track.providerUri = QString("https://www.youtube.com/watch?v=%1").arg(videoId);
	track.title = QString("YouTube video %1").arg(videoId);
	m_youtubeCaptureRefreshed = false;
	if (!RsMusicLocalPlayer::instance().playYouTubeVideo(track))
		return false;
	m_state->setActiveProvider(RsMusicProvider::YouTube);
	m_state->setPlaylistLabel("YouTube");
	m_state->setCurrentTrack(track);
	m_state->setPlaybackStatus(RsMusicState::PlaybackStatus::Playing);
	m_state->setRequestsEnabled(true);
	rsMusicSetRequestsEnabled(true);
	return true;
}

void RsMusicController::actionImportYouTubePlaylist(const QString &url)
{
	if (url.trimmed().isEmpty()) {
		emit youtubePlaylistError("Enter a YouTube or YouTube Music playlist URL.");
		return;
	}
	RsMusicLocalPlayer::instance().importYouTubePlaylist(url.trimmed());
	QSettings("RearSilver", "RearSilver-Stream-Suite").setValue("music/youtube/fallbackPlaylistUrl", url.trimmed());
	emit youtubePlaylistImported(0, "Import sent to Suite Media Player");
}

bool RsMusicController::playScheduledTrack(const RsMusicTrack &track)
{
	if (!m_state || track.provider != RsMusicProvider::YouTube || track.providerTrackId.trimmed().isEmpty())
		return false;
	if (!RsMusicLocalPlayer::instance().playYouTubeVideo(track))
		return false;
	m_youtubeCaptureRefreshed = false;
	rsMusicRecordScheduledTrackStarted(track);
	m_state->setActiveProvider(RsMusicProvider::YouTube);
	m_state->setPlaylistLabel(track.isFromPlaylist ? "YouTube fallback" : "YouTube requests");
	m_state->setCurrentTrack(track);
	m_state->setPlaybackStatus(RsMusicState::PlaybackStatus::Playing);
	m_state->setRequestsEnabled(true);
	hydrateCurrentYouTubeArtwork(track);
	return true;
}

void RsMusicController::hydrateCurrentYouTubeArtwork(const RsMusicTrack &track)
{
	if (!m_youtubeResolver || !m_state || track.provider != RsMusicProvider::YouTube)
		return;
	const QString trackId = track.trackId;
	m_youtubeResolver->cacheArtwork(track, [this, trackId](const QString &localUri) {
		if (localUri.isEmpty() || !m_state || !m_state->hasCurrentTrack() ||
		    m_state->currentTrack().trackId != trackId)
			return;
		RsMusicTrack current = m_state->currentTrack();
		current.artworkUri = localUri;
		m_state->setCurrentTrack(current);
	});
}

bool RsMusicController::playNextScheduledTrack()
{
	RsMusicTrack track;
	while (rsMusicTakeNextScheduledTrack(track)) {
		if (playScheduledTrack(track))
			return true;
	}
	return false;
}

void RsMusicController::refreshYouTubeCaptureSource()
{
	obs_source_t *source = obs_get_source_by_name("Music Capture");
	if (!source || QString::fromUtf8(obs_source_get_id(source)) != "wasapi_process_output_capture") {
		if (source)
			obs_source_release(source);
		return;
	}
	obs_data_t *settings = obs_source_get_settings(source);
	const long long originalPriority = obs_data_get_int(settings, "priority");
	obs_data_set_int(settings, "priority", originalPriority == 0 ? 1 : 0);
	obs_source_update(source, settings);
	obs_data_release(settings);
	obs_source_release(source);

	QTimer::singleShot(200, this, [originalPriority]() {
		obs_source_t *capture = obs_get_source_by_name("Music Capture");
		if (!capture)
			return;
		obs_data_t *captureSettings = obs_source_get_settings(capture);
		obs_data_set_int(captureSettings, "priority", originalPriority);
		obs_source_update(capture, captureSettings);
		obs_data_release(captureSettings);
		obs_source_release(capture);
	});
}

void RsMusicController::actionPrevious()
{
	if (m_state && m_state->hasCurrentTrack() && m_state->currentTrack().provider == RsMusicProvider::YouTube) {
		RsMusicLocalPlayer::instance().previous(); return;
	}
	if (!currentTrackIsLocal() || m_localLibrary.isEmpty()) return;
	if (m_localLibrary.size() > 1)
		m_localLibrary.prepend(m_localLibrary.takeLast());
	QSettings("RearSilver", "RearSilver-Stream-Suite").setValue("music/local/library", m_localLibrary);
	emit localLibraryChanged();
	playLocalIndex(0);
}

void RsMusicController::actionSeek(qint64 positionMs)
{
	if (currentTrackUsesCompanion())
		RsMusicLocalPlayer::instance().seekTo(positionMs);
}

void RsMusicController::setLocalLibrary(const QStringList &files)
{
	QStringList normalised;
	for (const QString &path : files) {
		const QString absolutePath = QFileInfo(path).absoluteFilePath();
		if (!absolutePath.isEmpty() && !normalised.contains(absolutePath, Qt::CaseInsensitive))
			normalised.append(absolutePath);
	}
	m_localLibrary = normalised;
	QSettings("RearSilver", "RearSilver-Stream-Suite").setValue("music/local/library", m_localLibrary);
	if (m_state && !m_localLibrary.isEmpty()) {
		m_state->setActiveProvider(RsMusicProvider::LocalFile);
		m_state->setRequestsEnabled(false);
		rsMusicSetRequestsEnabled(false);
	} else if (m_state) {
		m_state->setActiveProvider(RsMusicProvider::Unknown);
	}
	emit localLibraryChanged();
}

QStringList RsMusicController::localLibrary() const
{
	return m_localLibrary;
}

void RsMusicController::shuffleLocalLibrary()
{
	if (m_localLibrary.size() < 2)
		return;

	const int firstShuffledIndex = currentTrackIsLocal() ? 1 : 0;
	for (int index = m_localLibrary.size() - 1; index > firstShuffledIndex; --index) {
		const int swapIndex = firstShuffledIndex +
			QRandomGenerator::global()->bounded(index - firstShuffledIndex + 1);
		m_localLibrary.swapItemsAt(index, swapIndex);
	}
	QSettings("RearSilver", "RearSilver-Stream-Suite").setValue("music/local/library", m_localLibrary);
	emit localLibraryChanged();
}

bool RsMusicController::playLocalIndex(int index)
{
	if (!m_state || index < 0 || index >= m_localLibrary.size())
		return false;

	const QFileInfo file(m_localLibrary.at(index));
	if (!file.exists() || !file.isFile())
		return false;

	if (!currentTrackIsLocal() && m_state->hasCurrentTrack())
		rsMusicStop();
	if (index > 0) {
		m_localLibrary.prepend(m_localLibrary.takeAt(index));
		QSettings("RearSilver", "RearSilver-Stream-Suite").setValue("music/local/library", m_localLibrary);
		emit localLibraryChanged();
	}
	RsMusicTrack track;
	track.trackId = QString("local_%1").arg(QDateTime::currentMSecsSinceEpoch());
	track.provider = RsMusicProvider::LocalFile;
	track.providerTrackId = file.absoluteFilePath();
	track.providerUri = file.absoluteFilePath();
	track.title = file.completeBaseName();
	RsMusicMetadata::enrichLocalTrack(track, file.absoluteFilePath());
	track.isFromPlaylist = true;
	if (!RsMusicLocalPlayer::instance().playFile(track))
		return false;
	m_state->setActiveProvider(RsMusicProvider::LocalFile);
	m_state->setPlaylistLabel("Local files");
	m_state->setCurrentTrack(track);
	m_state->setPlaybackStatus(RsMusicState::PlaybackStatus::Playing);
	return true;
}

void RsMusicController::playNextLocalTrack()
{
	if (!m_state || m_localLibrary.isEmpty()) {
		if (m_state) {
			m_state->setPlaybackStatus(RsMusicState::PlaybackStatus::Stopped);
			m_state->clearCurrentTrack();
		}
		return;
	}

	if (m_localLibrary.size() > 1)
		m_localLibrary.append(m_localLibrary.takeFirst());
	QSettings("RearSilver", "RearSilver-Stream-Suite").setValue("music/local/library", m_localLibrary);
	emit localLibraryChanged();

	for (int attempt = 0; attempt < m_localLibrary.size(); ++attempt) {
		if (playLocalIndex(0))
			return;
		m_localLibrary.append(m_localLibrary.takeFirst());
	}

	m_state->setPlaybackStatus(RsMusicState::PlaybackStatus::Stopped);
	m_state->clearCurrentTrack();
}

RsMusicRequestResult RsMusicController::actionSongRequest(const QString &userId, const QString &displayName,
						   const QString &query, bool isModOrBroadcaster)
{
	const RsMusicRequestResult result = rsMusicRequestSong(userId, displayName, query, isModOrBroadcaster);
	if (result.accepted)
		RsMusicLocalPlayer::instance().requestYouTubeTrack(displayName, query.trimmed());
	return result;
}
