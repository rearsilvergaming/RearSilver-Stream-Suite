#include "rs_music_scheduler.hpp"

#include <QtGlobal>

void RsMusicScheduler::setDefaultPlaylist(const QVector<RsMusicTrack> &tracks)
{
	QString nextTrackId;
	if (!m_defaultPlaylist.isEmpty() && m_defaultCursor >= 0 && m_defaultCursor < m_defaultPlaylist.size())
		nextTrackId = m_defaultPlaylist.at(m_defaultCursor).trackId;

	m_defaultPlaylist = tracks;
	m_defaultCursor = 0;
	if (!nextTrackId.isEmpty()) {
		for (int index = 0; index < m_defaultPlaylist.size(); ++index) {
			if (m_defaultPlaylist.at(index).trackId == nextTrackId) {
				m_defaultCursor = index;
				break;
			}
		}
	}
}

const QVector<RsMusicTrack> &RsMusicScheduler::defaultPlaylist() const
{
	return m_defaultPlaylist;
}

int RsMusicScheduler::defaultCursor() const
{
	return m_defaultCursor;
}

void RsMusicScheduler::setDefaultCursor(int index)
{
	if (m_defaultPlaylist.isEmpty()) {
		m_defaultCursor = 0;
		return;
	}
	m_defaultCursor = qBound(0, index, m_defaultPlaylist.size() - 1);
}

void RsMusicScheduler::enqueueRequest(const RsMusicTrack &track)
{
	m_requests.append(track);
}

bool RsMusicScheduler::resolveRequest(const QString &trackId, const RsMusicTrack &resolved)
{
	for (RsMusicTrack &queued : m_requests) {
		if (queued.trackId != trackId)
			continue;
		const QString requesterId = queued.requestedById;
		const QString requester = queued.requestedBy;
		const qint64 enqueued = queued.enqueuedTimestampMs;
		queued = resolved;
		queued.trackId = trackId;
		queued.requestedById = requesterId;
		queued.requestedBy = requester;
		queued.enqueuedTimestampMs = enqueued;
		queued.isFromPlaylist = false;
		return true;
	}
	return false;
}

bool RsMusicScheduler::removeRequest(const QString &trackId)
{
	for (int index = 0; index < m_requests.size(); ++index) {
		if (m_requests.at(index).trackId == trackId) {
			m_requests.removeAt(index);
			return true;
		}
	}
	return false;
}

void RsMusicScheduler::clearRequests()
{
	m_requests.clear();
}

const QVector<RsMusicTrack> &RsMusicScheduler::requests() const
{
	return m_requests;
}

bool RsMusicScheduler::takeNext(RsMusicTrack &track)
{
	// Pending search queries remain in the queue until the provider resolver
	// supplies an actual provider track ID. A later direct-link request may
	// still play, and the fallback playlist keeps running while resolution is
	// pending, without silently discarding the unresolved request.
	for (int index = 0; index < m_requests.size(); ++index) {
		const RsMusicTrack &candidate = m_requests.at(index);
		if (candidate.provider == RsMusicProvider::Unknown || candidate.providerTrackId.trimmed().isEmpty())
			continue;
		track = m_requests.takeAt(index);
		return true;
	}

	if (m_defaultPlaylist.isEmpty())
		return false;
	if (m_defaultCursor < 0 || m_defaultCursor >= m_defaultPlaylist.size())
		m_defaultCursor = 0;

	track = m_defaultPlaylist.at(m_defaultCursor);
	m_defaultCursor = (m_defaultCursor + 1) % m_defaultPlaylist.size();
	return true;
}

void RsMusicScheduler::recordStarted(const RsMusicTrack &track)
{
	if (m_hasCurrentTrack && m_currentTrack.trackId != track.trackId) {
		m_history.append(m_currentTrack);
		while (m_history.size() > kMaximumHistory)
			m_history.removeFirst();
	}
	m_currentTrack = track;
	m_hasCurrentTrack = true;
}

bool RsMusicScheduler::takePrevious(RsMusicTrack &track)
{
	if (m_history.isEmpty())
		return false;
	track = m_history.takeLast();
	m_currentTrack = track;
	m_hasCurrentTrack = true;
	return true;
}

const QVector<RsMusicTrack> &RsMusicScheduler::history() const
{
	return m_history;
}
