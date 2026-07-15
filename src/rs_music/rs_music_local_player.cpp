#include "rs_music_local_player.hpp"
#include "rs_music.hpp"

#include <QFileInfo>
#include <QMetaObject>
#include <QTimer>

extern "C" {
#include <obs.h>
#include <obs-module.h>
}

static const char *kLocalPlaybackSourceName = "Music - Local Files";

RsMusicLocalPlayer &RsMusicLocalPlayer::instance()
{
	static RsMusicLocalPlayer player;
	return player;
}

RsMusicLocalPlayer::RsMusicLocalPlayer()
{
	m_progressTimer = new QTimer(this);
	m_progressTimer->setInterval(100);
	connect(m_progressTimer, &QTimer::timeout, this, [this]() {
		if (!m_source)
			return;
		const obs_media_state state = obs_source_media_get_state(m_source);
		if (state == OBS_MEDIA_STATE_PLAYING || state == OBS_MEDIA_STATE_PAUSED ||
		    state == OBS_MEDIA_STATE_BUFFERING) {
			const qint64 durationMs = obs_source_media_get_duration(m_source);
			if (m_pendingPausedSeekMs >= 0 && state == OBS_MEDIA_STATE_PAUSED) {
				emit playbackProgress(m_pendingPausedSeekMs, durationMs);
			} else {
				const qint64 positionMs = obs_source_media_get_time(m_source);
				if (m_pendingPausedSeekMs >= 0 && qAbs(positionMs - m_pendingPausedSeekMs) > 1500)
					emit playbackProgress(m_pendingPausedSeekMs, durationMs);
				else {
					m_pendingPausedSeekMs = -1;
					emit playbackProgress(positionMs, durationMs);
				}
			}
		} else if (state == OBS_MEDIA_STATE_ERROR) {
			emit playbackError("OBS reported an error while decoding the local music file.");
			m_progressTimer->stop();
		}
	});
}

RsMusicLocalPlayer::~RsMusicLocalPlayer()
{
	releaseSource();
}

bool RsMusicLocalPlayer::ensureSource()
{
	if (m_source)
		return true;

	m_source = obs_get_source_by_name(kLocalPlaybackSourceName);
	if (!m_source) {
		obs_data_t *settings = obs_data_create();
		obs_data_set_bool(settings, "is_local_file", true);
		obs_data_set_bool(settings, "looping", false);
		obs_data_set_bool(settings, "restart_on_activate", false);
		obs_data_set_bool(settings, "close_when_inactive", false);
		obs_data_set_bool(settings, "clear_on_media_end", true);
		m_source = obs_source_create("ffmpeg_source", kLocalPlaybackSourceName, settings, nullptr);
		obs_data_release(settings);

		if (m_source) {
			obs_source_t *sceneSource = ensureMusicSceneSource();
			obs_scene_t *scene = sceneSource ? obs_scene_from_source(sceneSource) : nullptr;
			if (scene)
				obs_scene_add(scene, m_source);
			if (sceneSource)
				obs_source_release(sceneSource);
		}
	}

	if (!m_source) {
		blog(LOG_ERROR, "[RS Music] Failed to create local-file playback source.");
		return false;
	}

	signal_handler_t *handler = obs_source_get_signal_handler(m_source);
	signal_handler_connect(handler, "media_started", onMediaStarted, this);
	signal_handler_connect(handler, "media_ended", onMediaEnded, this);
	signal_handler_connect(handler, "media_stopped", onMediaStopped, this);

	obs_source_inc_active(m_source);
	m_forcedActive = true;
	return true;
}

bool RsMusicLocalPlayer::playFile(const QString &filePath)
{
	const QFileInfo file(filePath);
	if (!file.exists() || !file.isFile()) {
		emit playbackError("The selected local music file no longer exists.");
		return false;
	}

	if (!ensureSource()) {
		emit playbackError("OBS could not create the local music playback source.");
		return false;
	}

	m_currentFile = file.absoluteFilePath();
	m_pendingPausedSeekMs = -1;
	obs_data_t *settings = obs_source_get_settings(m_source);
	obs_data_set_bool(settings, "is_local_file", true);
	obs_data_set_string(settings, "local_file", m_currentFile.toUtf8().constData());
	obs_data_set_bool(settings, "looping", false);
	obs_data_set_bool(settings, "restart_on_activate", false);
	obs_data_set_bool(settings, "close_when_inactive", false);
	obs_source_update(m_source, settings);
	obs_data_release(settings);

	obs_source_media_restart(m_source);
	m_progressTimer->start();
	blog(LOG_INFO, "[RS Music] Playing local file: %s", m_currentFile.toUtf8().constData());
	return true;
}

void RsMusicLocalPlayer::pause()
{
	if (m_source)
		obs_source_media_play_pause(m_source, true);
}

void RsMusicLocalPlayer::resume()
{
	if (m_source) {
		const qint64 pendingSeekMs = m_pendingPausedSeekMs;
		obs_source_media_play_pause(m_source, false);
		if (pendingSeekMs >= 0)
			obs_source_media_set_time(m_source, pendingSeekMs);
	}
}

void RsMusicLocalPlayer::stop()
{
	m_pendingPausedSeekMs = -1;
	if (m_source)
		obs_source_media_stop(m_source);
}

void RsMusicLocalPlayer::restart()
{
	m_pendingPausedSeekMs = -1;
	if (m_source)
		obs_source_media_restart(m_source);
}

void RsMusicLocalPlayer::seekTo(qint64 positionMs)
{
	if (!m_source)
		return;
	positionMs = qMax<qint64>(0, positionMs);
	const bool paused = obs_source_media_get_state(m_source) == OBS_MEDIA_STATE_PAUSED;
	obs_source_media_set_time(m_source, positionMs);
	if (paused) {
		m_pendingPausedSeekMs = positionMs;
		emit playbackProgress(positionMs, obs_source_media_get_duration(m_source));
	}
}

void RsMusicLocalPlayer::shutdown()
{
	stop();
	if (m_progressTimer)
		m_progressTimer->stop();
	releaseSource();
	m_currentFile.clear();
	m_pendingPausedSeekMs = -1;
}

QString RsMusicLocalPlayer::currentFile() const
{
	return m_currentFile;
}

void RsMusicLocalPlayer::releaseSource()
{
	if (!m_source)
		return;

	signal_handler_t *handler = obs_source_get_signal_handler(m_source);
	signal_handler_disconnect(handler, "media_started", onMediaStarted, this);
	signal_handler_disconnect(handler, "media_ended", onMediaEnded, this);
	signal_handler_disconnect(handler, "media_stopped", onMediaStopped, this);
	if (m_forcedActive)
		obs_source_dec_active(m_source);
	m_forcedActive = false;
	obs_source_release(m_source);
	m_source = nullptr;
}

void RsMusicLocalPlayer::onMediaStarted(void *data, calldata_t *)
{
	auto *player = static_cast<RsMusicLocalPlayer *>(data);
	QMetaObject::invokeMethod(player, [player]() { emit player->playbackStarted(); }, Qt::QueuedConnection);
}

void RsMusicLocalPlayer::onMediaEnded(void *data, calldata_t *)
{
	auto *player = static_cast<RsMusicLocalPlayer *>(data);
	QMetaObject::invokeMethod(player, [player]() { emit player->playbackEnded(); }, Qt::QueuedConnection);
}

void RsMusicLocalPlayer::onMediaStopped(void *data, calldata_t *)
{
	auto *player = static_cast<RsMusicLocalPlayer *>(data);
	QMetaObject::invokeMethod(player, [player]() { emit player->playbackStopped(); }, Qt::QueuedConnection);
}
