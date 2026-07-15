#include "rs_music_controller.hpp"
#include "state/rs_music_state.hpp"
#include "rs_music_local_player.hpp"

#include <QDateTime>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QSettings>

extern "C" {
#include <obs-module.h>
}

RsMusicController::RsMusicController(RsMusicState *state, QObject *parent) : QObject(parent), m_state(state)
{
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
		if (m_state && currentTrackIsLocal())
			m_state->setPlaybackStatus(RsMusicState::PlaybackStatus::Playing);
	});
	connect(&RsMusicLocalPlayer::instance(), &RsMusicLocalPlayer::playbackEnded, this, [this]() {
		if (m_state && currentTrackIsLocal())
			playNextLocalTrack();
	});
	syncQueueFromBackend();
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

	QVector<RsMusicTrack> tracks;
	const QVector<RsMusicQueueEntry> entries = rsMusicQueueSnapshot();
	tracks.reserve(entries.size());
	for (const RsMusicQueueEntry &entry : entries) {
		RsMusicTrack track;
		track.trackId = entry.trackId;
		track.provider = entry.provider;
		track.providerTrackId = entry.providerTrackId;
		track.providerUri = entry.providerUri;
		track.title = entry.title.trimmed();
		if (track.title.isEmpty())
			track.title = entry.pendingQuery.trimmed();
		if (track.title.isEmpty() && !entry.youtubeId.isEmpty())
			track.title = QString("YouTube video %1").arg(entry.youtubeId);
		track.artist = entry.artist;
		track.artworkUri = entry.artworkUri;
		track.durationSeconds = entry.durationSeconds;
		track.requestedBy = entry.requesterDisplay;
		track.isFromPlaylist = false;
		tracks.append(track);
	}

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

	if (currentTrackIsLocal())
		RsMusicLocalPlayer::instance().resume();
	else
		rsMusicResume();
	m_state->setPlaybackStatus(RsMusicState::PlaybackStatus::Playing);
}

void RsMusicController::actionPause()
{
	if (!m_state)
		return;

	if (currentTrackIsLocal())
		RsMusicLocalPlayer::instance().pause();
	else
		rsMusicPause();
	m_state->setPlaybackStatus(RsMusicState::PlaybackStatus::Paused);
}

void RsMusicController::actionStop()
{
	if (!m_state)
		return;

	if (currentTrackIsLocal()) {
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

	if (currentTrackIsLocal())
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

void RsMusicController::actionPrevious()
{
	if (!currentTrackIsLocal() || m_localLibrary.isEmpty())
		return;
	if (m_localLibrary.size() > 1)
		m_localLibrary.prepend(m_localLibrary.takeLast());
	QSettings("RearSilver", "RearSilver-Stream-Suite").setValue("music/local/library", m_localLibrary);
	emit localLibraryChanged();
	playLocalIndex(0);
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
	if (!RsMusicLocalPlayer::instance().playFile(file.absoluteFilePath()))
		return false;

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
	track.isFromPlaylist = true;
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
	return rsMusicRequestSong(userId, displayName, query, isModOrBroadcaster);
}
