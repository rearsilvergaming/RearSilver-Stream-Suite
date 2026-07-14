#include "rs_music_controller.hpp"
#include "state/rs_music_state.hpp"

extern "C" {
#include <obs-module.h>
}

RsMusicController::RsMusicController(RsMusicState *state, QObject *parent) : QObject(parent), m_state(state)
{
	connect(&rsMusicBackendEvents(), &RsMusicBackendEvents::queueChanged, this,
		&RsMusicController::syncQueueFromBackend);
	syncQueueFromBackend();
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
		track.title = entry.title.trimmed();
		if (track.title.isEmpty())
			track.title = entry.pendingQuery.trimmed();
		if (track.title.isEmpty() && !entry.youtubeId.isEmpty())
			track.title = QString("YouTube video %1").arg(entry.youtubeId);
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

	rsMusicResume();
	m_state->setPlaybackStatus(RsMusicState::PlaybackStatus::Playing);
}

void RsMusicController::actionPause()
{
	if (!m_state)
		return;

	rsMusicPause();
	m_state->setPlaybackStatus(RsMusicState::PlaybackStatus::Paused);
}

void RsMusicController::actionStop()
{
	if (!m_state)
		return;

	rsMusicStop();
	m_state->setPlaybackStatus(RsMusicState::PlaybackStatus::Stopped);
}

void RsMusicController::actionRestart()
{
	if (!m_state)
		return;

	rsMusicRestart();
	m_state->setPlaybackStatus(RsMusicState::PlaybackStatus::Playing);
}

void RsMusicController::actionSkip(const QString &source)
{
	rsMusicSkip(source);
}

RsMusicRequestResult RsMusicController::actionSongRequest(const QString &userId, const QString &displayName,
						   const QString &query, bool isModOrBroadcaster)
{
	return rsMusicRequestSong(userId, displayName, query, isModOrBroadcaster);
}
