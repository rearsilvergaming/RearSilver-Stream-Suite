#pragma once

#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QObject>
#include <QVector>

// Forward declarations (OBS)
struct obs_source;

// ===========================
// OBS helpers
// ===========================
obs_source *ensureMusicSceneSource();           // scene SOURCE (not obs_scene*)
obs_source *ensureMusicPlaybackBrowserSource(); // hosted player browser source

// ===========================
// Hosted Player Configuration
// ===========================
void rsMusicConfigureHosted(const QString &channelName, const QString &wsUrl, const QString &dockToken,
			    const QString &playerToken, const QString &basePlayerUrl = "https://music.rearsilver.com");

// Initialise music system (safe to call multiple times)
void rsMusicEnsureSystem();

// Shutdown hook (disconnect WebSocket cleanly)
void rsMusicShutdown();

// ===========================
// Phase 6A: Authoritative Settings
// ===========================
void rsMusicSetRequestsEnabled(bool enabled);
bool rsMusicRequestsEnabled();

void rsMusicSetMaxQueueTotal(int maxTotal);
int rsMusicMaxQueueTotal();

void rsMusicSetMaxPerUser(int maxPerUser);
int rsMusicMaxPerUser();

void rsMusicSetMaxTrackLengthSec(int maxLenSec);
int rsMusicMaxTrackLengthSec();

// Fallback playlist behaviour (Nightbot-accurate):
// - Fallback has a persistent cursor.
// - Requests temporarily interrupt.
// - When requests end, fallback resumes where it left off.
void rsMusicSetFallbackPlaylistUrl(const QString &playlistUrl);
QString rsMusicFallbackPlaylistUrl();

// Provide resolved fallback video IDs (Phase 6B will populate this).
// If empty, fallback effectively does nothing.
void rsMusicSetFallbackVideoIds(const QStringList &youtubeIds);
QStringList rsMusicFallbackVideoIds();

// Optional: set fallback cursor (useful later for persistence)
void rsMusicSetFallbackIndex(int index);
int rsMusicFallbackIndex();

// ===========================
// Phase 6A: Request Entry
// ===========================
struct RsMusicRequestResult {
	bool accepted = false;
	QString reason;  // human-readable rejection reason
	QString trackId; // set if accepted
};

struct RsMusicQueueEntry {
	QString trackId;
	QString youtubeId;
	QString pendingQuery;
	QString title;
	QString requesterDisplay;
	int durationSeconds = 0;
};

class RsMusicBackendEvents : public QObject {
	Q_OBJECT

public:
	using QObject::QObject;

signals:
	void queueChanged();
};

RsMusicBackendEvents &rsMusicBackendEvents();
QVector<RsMusicQueueEntry> rsMusicQueueSnapshot();

// Add a song request.
// - input can be a YouTube URL or free text like "Never Gonna Give You Up Rick Astley".
// - role enforcement happens upstream; this function just applies queue rules.
// - If input is URL -> playable immediately.
// - If input is text -> stored as pendingQuery (Phase 6B resolves).
RsMusicRequestResult rsMusicRequestSong(const QString &requesterId, const QString &requesterDisplay,
					const QString &input, bool isModOrBroadcaster);

// ===========================
// Phase 6A: Queue Operations (logic only; UI hooks later)
// ===========================
void rsMusicClearRequestsQueue();
bool rsMusicRemoveRequestByTrackId(const QString &trackId);

// ===========================
// Playback controls (Dock authority)
// ===========================
void rsMusicPlayVideo(const QString &youtubeVideoId); // low-level, immediate play by id (used for tests or internal)
void rsMusicPause();
void rsMusicResume();
void rsMusicStop();
void rsMusicRestart();
void rsMusicSkip(const QString &reason = "manual");
void rsMusicSetVolume(int volume0to100);

// Push a fresh authoritative snapshot (state_full).
void rsMusicPushStateFull();
