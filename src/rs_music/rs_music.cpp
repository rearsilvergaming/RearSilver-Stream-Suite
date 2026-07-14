#include "rs_music.hpp"
#include "rs_music_ws.hpp"
#include "rs_music_helpers.hpp"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QUuid>

#include <deque>
#include <vector>

extern "C" {
#include <obs.h>
#include <obs-module.h>
}

static const char *kMusicSceneName = "RS Music System";
static const char *kMusicPlaybackSrcName = "Music - Playback";

// -----------------------
// Hosted configuration
// -----------------------
static QString g_channelName;
static QString g_wsUrl;
static QString g_dockToken;
static QString g_playerToken;
static QString g_basePlayerUrl = "https://music.rearsilver.com";
static QString g_sessionId;

// -----------------------
// Authoritative settings (Phase 6A)
// -----------------------
static bool g_requestsEnabled = true;
static int g_maxQueueTotal = 50;
static int g_maxPerUser = 2;
static int g_maxTrackLengthSec = 600; // 10m default

static QString g_fallbackPlaylistUrl;
static QStringList g_fallbackVideoIds; // resolved ids (Phase 6B)
static int g_fallbackIndex = 0;

// -----------------------
// Playback state
// -----------------------
static QString g_currentTrackId;
static QString g_currentYoutubeId;
static QString g_currentTitle;
static QString g_currentRequesterId;
static QString g_currentRequesterDisplay;
static QString g_currentSource; // "chat" | "fallback"
static int g_currentDurationSec = 0;
static int g_volume = 70;

// History (minimal for now)
struct HistoryItem {
	QString trackId;
	QString youtubeId;
	QString title;
	QString requesterDisplay;
	QString source;
	QString outcome; // "played" | "skipped" | "rejected" | "error"
	QString note;
	qint64 ts = 0;
};
static std::deque<HistoryItem> g_history;

// Request queue item
struct QueueItem {
	QString trackId;
	QString youtubeId;    // if known
	QString pendingQuery; // if youtubeId not resolved yet
	QString title;        // optional
	QString requesterId;
	QString requesterDisplay;
	int durationSec = 0; // optional
	qint64 enqueuedTs = 0;
};
static std::deque<QueueItem> g_requestQueue;

static qint64 nowMs()
{
	return QDateTime::currentMSecsSinceEpoch();
}

static QString roomId(const QString &channelName)
{
	return rsMusicNormaliseChannelName(channelName);
}

static QString makeTrackId()
{
	return QString("trk_%1").arg(QString::number(nowMs()));
}

static void historyPush(const HistoryItem &h)
{
	g_history.push_front(h);
	while ((int)g_history.size() > 50)
		g_history.pop_back();
}

// -----------------------
// State snapshot (Phase 6A proper)
// -----------------------
static QJsonObject buildStateFullPayload()
{
	QJsonObject meta;
	meta["channelName"] = roomId(g_channelName);
	meta["sessionId"] = g_sessionId;
	meta["protocolVersion"] = 1;

	QJsonObject playback;
	QString status = "stopped";
	if (!g_currentYoutubeId.isEmpty())
		status = "playing"; // Phase 6A doesn't track paused state yet (telemetry will later)

	playback["status"] = status;
	playback["trackId"] = g_currentTrackId;
	playback["youtubeId"] = g_currentYoutubeId;
	playback["title"] = g_currentTitle;
	playback["requester"] = g_currentRequesterDisplay;
	playback["requesterId"] = g_currentRequesterId;
	playback["source"] = g_currentSource;
	playback["durationSec"] = g_currentDurationSec;
	playback["positionSec"] = 0;
	playback["volume"] = g_volume;
	playback["startedAtTs"] = (double)nowMs();

	QJsonArray queue;
	for (const auto &it : g_requestQueue) {
		QJsonObject q;
		q["trackId"] = it.trackId;
		q["youtubeId"] = it.youtubeId;
		q["pendingQuery"] = it.pendingQuery;
		q["title"] = it.title;
		q["requester"] = it.requesterDisplay;
		q["requesterId"] = it.requesterId;
		q["durationSec"] = it.durationSec;
		q["enqueuedTs"] = (double)it.enqueuedTs;
		queue.append(q);
	}

	QJsonArray history;
	for (const auto &h : g_history) {
		QJsonObject o;
		o["trackId"] = h.trackId;
		o["youtubeId"] = h.youtubeId;
		o["title"] = h.title;
		o["requester"] = h.requesterDisplay;
		o["source"] = h.source;
		o["outcome"] = h.outcome;
		o["note"] = h.note;
		o["ts"] = (double)h.ts;
		history.append(o);
	}

	QJsonObject fallback;
	fallback["enabled"] = !g_fallbackPlaylistUrl.trimmed().isEmpty();
	fallback["playlistUrl"] = g_fallbackPlaylistUrl;
	fallback["nextIndex"] = g_fallbackIndex;
	QJsonArray fallbackIds;
	for (const auto &id : g_fallbackVideoIds)
		fallbackIds.append(id);
	fallback["videoIds"] = fallbackIds;

	QJsonObject rules;
	rules["requestsEnabled"] = g_requestsEnabled;
	rules["maxQueueTotal"] = g_maxQueueTotal;
	rules["maxPerUser"] = g_maxPerUser;
	rules["maxTrackLengthSec"] = g_maxTrackLengthSec;
	rules["requireMusicCategory"] = true; // enforced in Phase 6B when metadata is available
	rules["modsBypass"] = true;

	QJsonObject payload;
	payload["meta"] = meta;
	payload["playback"] = playback;
	payload["queue"] = queue;
	payload["history"] = history;
	payload["fallback"] = fallback;
	payload["rules"] = rules;

	return payload;
}

// -----------------------
// OBS helpers
// -----------------------
obs_source_t *ensureMusicSceneSource()
{
	obs_source_t *src = obs_get_source_by_name(kMusicSceneName);
	if (src)
		return src;

	obs_scene_t *scene = obs_scene_create(kMusicSceneName);
	return obs_scene_get_source(scene);
}

static void applyHostedBrowserSettings(obs_source_t *browserSrc)
{
	obs_data_t *settings = obs_source_get_settings(browserSrc);

	const QString url = QString("%1/player/%2?mode=player&token=%3")
				    .arg(g_basePlayerUrl)
				    .arg(roomId(g_channelName))
				    .arg(g_playerToken);

	obs_data_set_string(settings, "url", url.toUtf8().constData());
	obs_data_set_bool(settings, "is_local_file", false);
	obs_data_set_bool(settings, "reroute_audio", true);
	obs_data_set_bool(settings, "background_audio", true);

	obs_source_update(browserSrc, settings);
	obs_data_release(settings);

	blog(LOG_INFO, "[RS Music] Browser source URL set to hosted player for '%s'",
	     roomId(g_channelName).toUtf8().constData());
}

obs_source_t *ensureMusicPlaybackBrowserSource()
{
	obs_source_t *sceneSrc = ensureMusicSceneSource();
	obs_scene_t *scene = obs_scene_from_source(sceneSrc);

	obs_source_t *browser = obs_get_source_by_name(kMusicPlaybackSrcName);

	if (!browser) {
		obs_data_t *s = obs_data_create();
		browser = obs_source_create("browser_source", kMusicPlaybackSrcName, s, nullptr);
		obs_scene_add(scene, browser);
		obs_data_release(s);
	}

	applyHostedBrowserSettings(browser);

	obs_source_release(sceneSrc);
	return browser;
}

// -----------------------
// Internal: stop current
// -----------------------
static void clearCurrent()
{
	g_currentTrackId.clear();
	g_currentYoutubeId.clear();
	g_currentTitle.clear();
	g_currentRequesterId.clear();
	g_currentRequesterDisplay.clear();
	g_currentSource.clear();
	g_currentDurationSec = 0;
}

// -----------------------
// Internal: choose next track (Nightbot-accurate)
// -----------------------
static bool playSelected(const QueueItem &it, const QString &source)
{
	// If not resolved yet, we cannot actually play it in Phase 6A.
	if (it.youtubeId.trimmed().isEmpty()) {
		blog(LOG_WARNING, "[RS Music] Skipping unresolved request (trackId=%s query='%s')",
		     it.trackId.toUtf8().constData(), it.pendingQuery.toUtf8().constData());

		HistoryItem h;
		h.trackId = it.trackId;
		h.youtubeId = "";
		h.title = it.title;
		h.requesterDisplay = it.requesterDisplay;
		h.source = "chat";
		h.outcome = "skipped";
		h.note = "Unresolved request (Phase 6B will resolve search queries)";
		h.ts = nowMs();
		historyPush(h);

		return false;
	}

	g_currentTrackId = it.trackId;
	g_currentYoutubeId = it.youtubeId;
	g_currentTitle = it.title;
	g_currentRequesterId = it.requesterId;
	g_currentRequesterDisplay = it.requesterDisplay;
	g_currentSource = source;
	g_currentDurationSec = it.durationSec;

	// Snapshot first, then command
	RsMusicWsClient::instance().sendStateFull(buildStateFullPayload());
	RsMusicWsClient::instance().sendCmdPlay(g_currentTrackId, g_currentYoutubeId, 0, g_volume);

	return true;
}

static bool selectAndPlayNext(const QString &why)
{
	Q_UNUSED(why);

	// Priority 1: request queue (FIFO)
	while (!g_requestQueue.empty()) {
		const QueueItem it = g_requestQueue.front();
		g_requestQueue.pop_front();

		if (playSelected(it, "chat"))
			return true;

		// If we skipped due to unresolved, continue to next request.
	}

	// Priority 2: fallback playlist cursor (resume where left off)
	if (!g_fallbackVideoIds.isEmpty()) {
		if (g_fallbackIndex < 0)
			g_fallbackIndex = 0;
		if (g_fallbackIndex >= g_fallbackVideoIds.size())
			g_fallbackIndex = 0;

		QueueItem it;
		it.trackId = makeTrackId();
		it.youtubeId = g_fallbackVideoIds[g_fallbackIndex];
		it.pendingQuery = "";
		it.title = ""; // Phase 6B will resolve titles
		it.requesterId = "";
		it.requesterDisplay = "";
		it.durationSec = 0;
		it.enqueuedTs = nowMs();

		// Advance cursor ONLY when we actually choose a fallback track to play
		g_fallbackIndex = (g_fallbackIndex + 1) % g_fallbackVideoIds.size();

		if (playSelected(it, "fallback"))
			return true;
	}

	// Nothing available -> stop
	blog(LOG_INFO, "[RS Music] No next track available. Stopping.");
	clearCurrent();
	RsMusicWsClient::instance().sendStateFull(buildStateFullPayload());
	RsMusicWsClient::instance().sendCmdStop();
	return false;
}

// -----------------------
// Public API
// -----------------------
void rsMusicConfigureHosted(const QString &channelName, const QString &wsUrl, const QString &dockToken,
			    const QString &playerToken, const QString &basePlayerUrl)
{
	g_channelName = channelName.trimmed();
	g_wsUrl = wsUrl.trimmed();
	g_dockToken = dockToken.trimmed();
	g_playerToken = playerToken.trimmed();
	g_basePlayerUrl = basePlayerUrl.trimmed();
	if (g_basePlayerUrl.isEmpty())
		g_basePlayerUrl = "https://music.rearsilver.com";

	if (g_sessionId.isEmpty())
		g_sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);

	RsMusicWsClient::instance().configure(g_wsUrl, roomId(g_channelName), g_dockToken);
	RsMusicWsClient::instance().connectIfNeeded();

	blog(LOG_INFO, "[RS Music] Hosted config set. room='%s'", roomId(g_channelName).toUtf8().constData());
}

void rsMusicEnsureSystem()
{
	if (g_channelName.isEmpty() || g_wsUrl.isEmpty() || g_dockToken.isEmpty() || g_playerToken.isEmpty()) {
		blog(LOG_ERROR,
		     "[RS Music] Not configured. Call rsMusicConfigureHosted(...) before rsMusicEnsureSystem().");
		return;
	}

	obs_source_t *src = ensureMusicPlaybackBrowserSource();
	if (src)
		obs_source_release(src);

	// Wire telemetry once
	static bool wired = false;
	if (!wired) {
		wired = true;

		auto &ws = RsMusicWsClient::instance();

		QObject::connect(&ws, &RsMusicWsClient::evtReady, &ws, []() {
			blog(LOG_INFO, "[RS Music] evt_ready -> sending state_full");
			RsMusicWsClient::instance().sendStateFull(buildStateFullPayload());
		});

		QObject::connect(&ws, &RsMusicWsClient::evtEnded, &ws, [](const QString &trackId, const QString &) {
			blog(LOG_INFO, "[RS Music] evt_ended (%s) -> advance", trackId.toUtf8().constData());

			HistoryItem h;
			h.trackId = trackId;
			h.youtubeId = g_currentYoutubeId;
			h.title = g_currentTitle;
			h.requesterDisplay = g_currentRequesterDisplay;
			h.source = g_currentSource;
			h.outcome = "played";
			h.note = "";
			h.ts = nowMs();
			historyPush(h);

			clearCurrent();
			RsMusicWsClient::instance().sendStateFull(buildStateFullPayload());
			selectAndPlayNext("ended");
		});

		QObject::connect(&ws, &RsMusicWsClient::evtSkipped, &ws, [](const QString &trackId) {
			blog(LOG_INFO, "[RS Music] evt_skipped (%s) -> advance", trackId.toUtf8().constData());

			HistoryItem h;
			h.trackId = trackId;
			h.youtubeId = g_currentYoutubeId;
			h.title = g_currentTitle;
			h.requesterDisplay = g_currentRequesterDisplay;
			h.source = g_currentSource;
			h.outcome = "skipped";
			h.note = "Player skipped";
			h.ts = nowMs();
			historyPush(h);

			clearCurrent();
			RsMusicWsClient::instance().sendStateFull(buildStateFullPayload());
			selectAndPlayNext("skipped");
		});

		QObject::connect(&ws, &RsMusicWsClient::evtError, &ws,
				 [](const QString &trackId, const QString &youtubeId, const QString &code,
				    const QString &message) {
					 blog(LOG_ERROR, "[RS Music] evt_error track=%s vid=%s code=%s msg=%s",
					      trackId.toUtf8().constData(), youtubeId.toUtf8().constData(),
					      code.toUtf8().constData(), message.toUtf8().constData());

					 HistoryItem h;
					 h.trackId = trackId;
					 h.youtubeId = youtubeId;
					 h.title = g_currentTitle;
					 h.requesterDisplay = g_currentRequesterDisplay;
					 h.source = g_currentSource;
					 h.outcome = "error";
					 h.note = code + ": " + message;
					 h.ts = nowMs();
					 historyPush(h);

					 clearCurrent();
					 RsMusicWsClient::instance().sendStateFull(buildStateFullPayload());
					 selectAndPlayNext("error");
				 });
	}

	RsMusicWsClient::instance().connectIfNeeded();
	RsMusicWsClient::instance().sendStateFull(buildStateFullPayload());

	blog(LOG_INFO, "[RS Music] System ensured (hosted browser + ws).");
}

void rsMusicShutdown()
{
	RsMusicWsClient::instance().disconnectNow();
}

// -----------------------
// Settings
// -----------------------
void rsMusicSetRequestsEnabled(bool enabled)
{
	g_requestsEnabled = enabled;
	rsMusicPushStateFull();
}
bool rsMusicRequestsEnabled()
{
	return g_requestsEnabled;
}

void rsMusicSetMaxQueueTotal(int maxTotal)
{
	if (maxTotal < 0)
		maxTotal = 0;
	g_maxQueueTotal = maxTotal;
	rsMusicPushStateFull();
}
int rsMusicMaxQueueTotal()
{
	return g_maxQueueTotal;
}

void rsMusicSetMaxPerUser(int maxPerUser)
{
	if (maxPerUser < 0)
		maxPerUser = 0;
	g_maxPerUser = maxPerUser;
	rsMusicPushStateFull();
}
int rsMusicMaxPerUser()
{
	return g_maxPerUser;
}

void rsMusicSetMaxTrackLengthSec(int maxLenSec)
{
	if (maxLenSec < 0)
		maxLenSec = 0;
	g_maxTrackLengthSec = maxLenSec;
	rsMusicPushStateFull();
}
int rsMusicMaxTrackLengthSec()
{
	return g_maxTrackLengthSec;
}

// Fallback
void rsMusicSetFallbackPlaylistUrl(const QString &playlistUrl)
{
	g_fallbackPlaylistUrl = playlistUrl.trimmed();
	rsMusicPushStateFull();
}
QString rsMusicFallbackPlaylistUrl()
{
	return g_fallbackPlaylistUrl;
}

void rsMusicSetFallbackVideoIds(const QStringList &youtubeIds)
{
	g_fallbackVideoIds = youtubeIds;
	if (g_fallbackIndex < 0)
		g_fallbackIndex = 0;
	if (!g_fallbackVideoIds.isEmpty() && g_fallbackIndex >= g_fallbackVideoIds.size())
		g_fallbackIndex = 0;

	rsMusicPushStateFull();
}
QStringList rsMusicFallbackVideoIds()
{
	return g_fallbackVideoIds;
}

void rsMusicSetFallbackIndex(int index)
{
	g_fallbackIndex = index;
	if (g_fallbackIndex < 0)
		g_fallbackIndex = 0;
	if (!g_fallbackVideoIds.isEmpty() && g_fallbackIndex >= g_fallbackVideoIds.size())
		g_fallbackIndex = 0;

	rsMusicPushStateFull();
}
int rsMusicFallbackIndex()
{
	return g_fallbackIndex;
}

// -----------------------
// Request entry
// -----------------------
static int countQueuedByUser(const QString &requesterId)
{
	int count = 0;
	for (const auto &it : g_requestQueue) {
		if (it.requesterId == requesterId)
			count++;
	}
	return count;
}

RsMusicRequestResult rsMusicRequestSong(const QString &requesterId, const QString &requesterDisplay,
					const QString &input, bool isModOrBroadcaster)
{
	RsMusicRequestResult out;
	const QString trimmed = input.trimmed();

	if (trimmed.isEmpty()) {
		out.accepted = false;
		out.reason = "No request text provided.";
		return out;
	}

	if (!g_requestsEnabled && !isModOrBroadcaster) {
		out.accepted = false;
		out.reason = "Song requests are currently disabled.";
		return out;
	}

	if (g_maxQueueTotal > 0 && (int)g_requestQueue.size() >= g_maxQueueTotal && !isModOrBroadcaster) {
		out.accepted = false;
		out.reason = "The request queue is full.";
		return out;
	}

	if (g_maxPerUser > 0 && !isModOrBroadcaster) {
		const int already = countQueuedByUser(requesterId);
		if (already >= g_maxPerUser) {
			out.accepted = false;
			out.reason = "You already have the maximum number of songs queued.";
			return out;
		}
	}

	QueueItem it;
	it.trackId = makeTrackId();
	it.requesterId = requesterId;
	it.requesterDisplay = requesterDisplay;
	it.enqueuedTs = nowMs();

	// URL -> extract video id
	const QString extracted = rsMusicExtractYoutubeVideoId(trimmed);
	if (!extracted.isEmpty()) {
		it.youtubeId = extracted;
		it.pendingQuery.clear();
		it.title = "";
	} else {
		// Free text query (Phase 6B resolves to YouTube ID + metadata)
		it.youtubeId.clear();
		it.pendingQuery = trimmed;
		it.title = trimmed;
	}

	g_requestQueue.push_back(it);

	out.accepted = true;
	out.reason.clear();
	out.trackId = it.trackId;

	// Push state update immediately for viewer queue display
	rsMusicPushStateFull();

	return out;
}

// -----------------------
// Queue ops
// -----------------------
void rsMusicClearRequestsQueue()
{
	g_requestQueue.clear();
	rsMusicPushStateFull();
}

bool rsMusicRemoveRequestByTrackId(const QString &trackId)
{
	for (auto it = g_requestQueue.begin(); it != g_requestQueue.end(); ++it) {
		if (it->trackId == trackId) {
			g_requestQueue.erase(it);
			rsMusicPushStateFull();
			return true;
		}
	}
	return false;
}

// -----------------------
// Playback controls
// -----------------------
void rsMusicPlayVideo(const QString &youtubeVideoId)
{
	const QString vid = youtubeVideoId.trimmed();
	if (vid.isEmpty())
		return;

	QueueItem it;
	it.trackId = makeTrackId();
	it.youtubeId = vid;
	it.pendingQuery.clear();
	it.title = "";
	it.requesterId = "";
	it.requesterDisplay = "";
	it.enqueuedTs = nowMs();

	RsMusicWsClient::instance().connectIfNeeded();
	playSelected(it, "manual");
}

void rsMusicPause()
{
	RsMusicWsClient::instance().connectIfNeeded();
	RsMusicWsClient::instance().sendCmdPause();
}

void rsMusicResume()
{
	RsMusicWsClient::instance().connectIfNeeded();
	RsMusicWsClient::instance().sendCmdResume();
}

void rsMusicStop()
{
	clearCurrent();
	RsMusicWsClient::instance().connectIfNeeded();
	RsMusicWsClient::instance().sendStateFull(buildStateFullPayload());
	RsMusicWsClient::instance().sendCmdStop();
}

void rsMusicRestart()
{
	if (g_currentTrackId.isEmpty())
		return;

	RsMusicWsClient::instance().connectIfNeeded();
	RsMusicWsClient::instance().sendCmdRestart(g_currentTrackId);
}

void rsMusicSkip(const QString &reason)
{
	Q_UNUSED(reason);

	if (!g_currentTrackId.isEmpty()) {
		RsMusicWsClient::instance().connectIfNeeded();
		RsMusicWsClient::instance().sendCmdSkip(g_currentTrackId, reason);

		HistoryItem h;
		h.trackId = g_currentTrackId;
		h.youtubeId = g_currentYoutubeId;
		h.title = g_currentTitle;
		h.requesterDisplay = g_currentRequesterDisplay;
		h.source = g_currentSource;
		h.outcome = "skipped";
		h.note = "Manual skip";
		h.ts = nowMs();
		historyPush(h);
	}

	clearCurrent();
	rsMusicPushStateFull();
	selectAndPlayNext("manual_skip");
}

void rsMusicSetVolume(int volume0to100)
{
	if (volume0to100 < 0 || volume0to100 > 100)
		return;

	g_volume = volume0to100;

	RsMusicWsClient::instance().connectIfNeeded();
	RsMusicWsClient::instance().sendCmdVolume(g_volume);
	rsMusicPushStateFull();
}

void rsMusicPushStateFull()
{
	RsMusicWsClient::instance().connectIfNeeded();
	RsMusicWsClient::instance().sendStateFull(buildStateFullPayload());
}
