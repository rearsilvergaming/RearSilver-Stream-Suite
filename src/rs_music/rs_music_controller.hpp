#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include "rs_music.hpp"

/*
 * RsMusicController
 *
 * Single authoritative action surface for music control.
 *
 * - UI buttons call this
 * - Chat commands call this
 * - Mutates RsMusicState
 * - Contains explicit STUBS for playback backend hooks
 *
 * DOES NOT:
 * - Read chat
 * - Send chat
 * - Own UI
 * - Own state
 */

class RsMusicState;
class RsMusicYouTubeResolver;

class RsMusicController : public QObject {
	Q_OBJECT

public:
	explicit RsMusicController(RsMusicState *state, QObject *parent = nullptr);

	// ---- playback controls ----
	void actionPlay();
	void actionPause();
	void actionStop();
	void actionRestart();
	void actionSkip(const QString &source); // "ui" / "chat"
	void actionPrevious();
	void actionSeek(qint64 positionMs);
	bool actionPlayLocalFile(const QString &filePath);
	bool actionPlayYouTubeVideo(const QString &url);
	void actionImportYouTubePlaylist(const QString &url);
	void setLocalLibrary(const QStringList &files);
	QStringList localLibrary() const;
	void shuffleLocalLibrary();

	// ---- song requests ----
	RsMusicRequestResult actionSongRequest(const QString &userId, const QString &displayName, const QString &query,
					       bool isModOrBroadcaster = false);

signals:
	void localLibraryChanged();
	void youtubePlaylistImported(int trackCount, const QString &label);
	void youtubePlaylistError(const QString &message);
	void youtubeRequestResolutionFailed(const QString &trackId, const QString &message);

private:
	void syncQueueFromBackend();
	bool currentTrackIsLocal() const;
	bool currentTrackUsesCompanion() const;
	bool playLocalIndex(int index);
	void playNextLocalTrack();
	bool playNextScheduledTrack();
	bool playScheduledTrack(const RsMusicTrack &track);
	void hydrateCurrentYouTubeArtwork(const RsMusicTrack &track);
	void refreshYouTubeCaptureSource();

	RsMusicState *m_state = nullptr; // non-owning
	RsMusicYouTubeResolver *m_youtubeResolver = nullptr;
	QStringList m_localLibrary;
	bool m_youtubeCaptureRefreshed = false;
};
