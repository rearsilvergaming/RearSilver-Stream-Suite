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

enum class RsStreamToolQuickAction {
	RefreshCurrentBrowsers,
	RefreshAllBrowsers,
	TriggerReplay,
	ShowReplay,
	HideReplay,
	ShowQuickText,
	HideQuickText,
	StartTimer,
	PauseTimer,
	ResetTimer,
	ShowTimer,
	HideTimer,
	ShowMusicOverlay,
	HideMusicOverlay,
};

void rsPublishStreamOverlayPlacementState();
void rsExecuteStreamToolQuickAction(RsStreamToolQuickAction action);

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
	void setLocalLibrary(const QStringList &files);
	QStringList localLibrary() const;
	void shuffleLocalLibrary();

	// ---- song requests ----
	RsMusicRequestResult actionSongRequest(const QString &userId, const QString &displayName, const QString &query,
					       int requesterLevel = 0);
	void actionRemoveRequest(const QString &requestId);

signals:
	void quickTextConfigurationReady(bool hasMessage);
	void timerConfigurationReady(const QString &mode);
	void replayConfigurationReady();
	void localLibraryChanged();
	void youtubeRequestResolutionFailed(const QString &trackId, const QString &message);
	void songRequestAccepted(const QString &requestId, const QString &title, const QString &artist,
				 const QString &requester, int queuePosition);
	void songRequestRejected(const QString &requestId, const QString &message);
	void songRequestRemoved(const QString &requestId, const QString &title, const QString &artist);
	void songRequestRemoveFailed(const QString &requestId, const QString &message);
	void nowPlayingAnnounced(const QString &title, const QString &artist, const QString &requester);
	void twitchAuthActionRequested(const QString &account, const QString &action);
	void twitchSenderPreferenceRequested(bool useBot);
	void twitchAuthStatusRequested();
	void twitchSessionReceived(const QString &account, const QString &accessToken, const QString &login,
				  const QString &userId);
	void twitchSessionCleared(const QString &account);
	void twitchAccountStateReceived(const QString &account, bool connected, const QString &login);

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
	bool m_replayBufferConfigurationReceived = false;
	bool m_replayFrameConfigurationReceived = false;
};
