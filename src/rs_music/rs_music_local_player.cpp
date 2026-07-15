#include "rs_music_local_player.hpp"
#include "rs_music.hpp"

#include <QFileInfo>
#include <QMetaObject>

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
	obs_data_t *settings = obs_source_get_settings(m_source);
	obs_data_set_bool(settings, "is_local_file", true);
	obs_data_set_string(settings, "local_file", m_currentFile.toUtf8().constData());
	obs_data_set_bool(settings, "looping", false);
	obs_data_set_bool(settings, "restart_on_activate", false);
	obs_data_set_bool(settings, "close_when_inactive", false);
	obs_source_update(m_source, settings);
	obs_data_release(settings);

	obs_source_media_restart(m_source);
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
	if (m_source)
		obs_source_media_play_pause(m_source, false);
}

void RsMusicLocalPlayer::stop()
{
	if (m_source)
		obs_source_media_stop(m_source);
}

void RsMusicLocalPlayer::restart()
{
	if (m_source)
		obs_source_media_restart(m_source);
}

void RsMusicLocalPlayer::shutdown()
{
	stop();
	releaseSource();
	m_currentFile.clear();
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
