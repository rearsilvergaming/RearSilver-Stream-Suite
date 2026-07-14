#include "rs_music/state/rs_music_state.hpp"
#include <QSettings>

RsMusicState::RsMusicState(QObject *parent) : QObject(parent)
{
	loadSettings();
}

// ---------------- Playback ----------------

void RsMusicState::setPlaybackStatus(PlaybackStatus status)
{
	if (m_playbackStatus == status)
		return;

	m_playbackStatus = status;
	emit stateChanged();
}

RsMusicState::PlaybackStatus RsMusicState::playbackStatus() const
{
	return m_playbackStatus;
}

// ---------------- Current Track ----------------

void RsMusicState::setCurrentTrack(const RsMusicTrack &track)
{
	m_currentTrack = track;
	m_hasCurrentTrack = true;
	emit stateChanged();
}

bool RsMusicState::hasCurrentTrack() const
{
	return m_hasCurrentTrack;
}

const RsMusicTrack &RsMusicState::currentTrack() const
{
	return m_currentTrack;
}

// ---------------- Queue ----------------

void RsMusicState::setQueue(const QVector<RsMusicTrack> &queue)
{
	m_queue = queue;
	emit stateChanged();
}

const QVector<RsMusicTrack> &RsMusicState::queue() const
{
	return m_queue;
}

// ---------------- Requests ----------------

void RsMusicState::setRequestsEnabled(bool enabled)
{
	if (m_requestsEnabled == enabled)
		return;

	m_requestsEnabled = enabled;
	saveSettings();
	emit stateChanged();
}


bool RsMusicState::requestsEnabled() const
{
	return m_requestsEnabled;
}

// ---------------- Playlist Label ----------------

void RsMusicState::setPlaylistLabel(const QString &label)
{
	if (m_playlistLabel == label)
		return;

	m_playlistLabel = label;
	emit stateChanged();
}

QString RsMusicState::playlistLabel() const
{
	return m_playlistLabel;
}

// ---------------- Persistence ----------------

void RsMusicState::loadSettings()
{
	QSettings s("RearSilver", "RearSilver-Stream-Suite");
	m_requestsEnabled = s.value("music/requestsEnabled", true).toBool();
}

void RsMusicState::saveSettings() const
{
	QSettings s("RearSilver", "RearSilver-Stream-Suite");
	s.setValue("music/requestsEnabled", m_requestsEnabled);
}
