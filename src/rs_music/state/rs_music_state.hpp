#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include "rs_music_track.hpp"

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
	void setPlaybackProgress(qint64 positionMs, qint64 durationMs);
	qint64 playbackPositionMs() const;

	// --- Current track ---
	void setCurrentTrack(const RsMusicTrack &track);
	void clearCurrentTrack();
	bool hasCurrentTrack() const;
	const RsMusicTrack &currentTrack() const;
	void setActiveProvider(RsMusicProvider provider);
	RsMusicProvider activeProvider() const;

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
	qint64 m_playbackPositionMs = 0;

	bool m_hasCurrentTrack = false;
	RsMusicTrack m_currentTrack;
	RsMusicProvider m_activeProvider = RsMusicProvider::Unknown;

	QVector<RsMusicTrack> m_queue;

	bool m_requestsEnabled = true;
	QString m_playlistLabel = "Playlist";
};
