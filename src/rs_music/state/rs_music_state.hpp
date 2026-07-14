#pragma once

#include <QObject>
#include <QString>
#include <QVector>

/*
 * A single track entry used by:
 * - current track
 * - queue entries
 */
struct RsMusicTrack {
	QString title;
	QString artist;
	int durationSeconds = 0; // REQUIRED (locked)
	QString requestedBy;     // empty if playlist
	bool isFromPlaylist = false;
};

/*
 * Authoritative music state for the entire dock.
 * UI panels, WebSocket output, and OBS text will all read from this.
 */
class RsMusicState : public QObject {
	Q_OBJECT

public:
	enum class PlaybackStatus { Stopped, Playing, Paused };

	explicit RsMusicState(QObject *parent = nullptr);

	// --- Playback ---
	void setPlaybackStatus(PlaybackStatus status);
	PlaybackStatus playbackStatus() const;

	// --- Current track ---
	void setCurrentTrack(const RsMusicTrack &track);
	bool hasCurrentTrack() const;
	const RsMusicTrack &currentTrack() const;

	// --- Queue ---
	void setQueue(const QVector<RsMusicTrack> &queue);
	const QVector<RsMusicTrack> &queue() const;

	// --- Requests ---
	void setRequestsEnabled(bool enabled);
	bool requestsEnabled() const;

	// --- Playlist label ---
	void setPlaylistLabel(const QString &label);
	QString playlistLabel() const;

	// --- Persistence ---
		void loadSettings();
	void saveSettings() const;


signals:
	// Emitted whenever ANY part of the state changes
	void stateChanged();

private:
	PlaybackStatus m_playbackStatus = PlaybackStatus::Stopped;

	bool m_hasCurrentTrack = false;
	RsMusicTrack m_currentTrack;

	QVector<RsMusicTrack> m_queue;

	bool m_requestsEnabled = true;
	QString m_playlistLabel = "Playlist";
};
