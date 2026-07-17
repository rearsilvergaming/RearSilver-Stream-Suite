#pragma once

#include <QVector>

#include "state/rs_music_track.hpp"

/*
 * Provider-neutral playback ordering.
 *
 * The default playlist owns a persistent cursor. Requests live in a separate
 * FIFO queue and are selected first at track boundaries without disturbing
 * that cursor. Playback history records the tracks that actually started,
 * rather than trying to infer Previous from either queue.
 */
class RsMusicScheduler {
public:
	void setDefaultPlaylist(const QVector<RsMusicTrack> &tracks);
	const QVector<RsMusicTrack> &defaultPlaylist() const;
	int defaultCursor() const;
	void setDefaultCursor(int index);

	void enqueueRequest(const RsMusicTrack &track);
	bool resolveRequest(const QString &trackId, const RsMusicTrack &resolved);
	bool removeRequest(const QString &trackId);
	void clearRequests();
	const QVector<RsMusicTrack> &requests() const;

	bool takeNext(RsMusicTrack &track);
	void recordStarted(const RsMusicTrack &track);
	bool takePrevious(RsMusicTrack &track);
	const QVector<RsMusicTrack> &history() const;

private:
	static constexpr int kMaximumHistory = 50;

	QVector<RsMusicTrack> m_defaultPlaylist;
	QVector<RsMusicTrack> m_requests;
	QVector<RsMusicTrack> m_history;
	int m_defaultCursor = 0;
	bool m_hasCurrentTrack = false;
	RsMusicTrack m_currentTrack;
};
