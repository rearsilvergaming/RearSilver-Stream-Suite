#include "rs_music_controller.hpp"
#include "state/rs_music_state.hpp"
#include "rs_music_local_player.hpp"
#include "rs_music_helpers.hpp"
#include "rs_music_metadata.hpp"
#include "rs_music_youtube_resolver.hpp"
#include "enhancements/rs_auto_start.hpp"
#include "enhancements/rs_browser_refresh.hpp"
#include "enhancements/rs_quick_text.hpp"
#include "enhancements/rs_timer.hpp"
#include "enhancements/rs_instant_replay.hpp"

#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSettings>
#include <QTimer>

extern "C" {
#include <obs-module.h>
#include <obs-frontend-api.h>
}

RsMusicController::RsMusicController(RsMusicState *state, QObject *parent) : QObject(parent), m_state(state)
{
	connect(&RsMusicLocalPlayer::instance(), &RsMusicLocalPlayer::hostCommandReceived, this, [this](const QString &command) {
		auto publishSetupState = []() {
			obs_source_t *capture = obs_get_source_by_name("Music Capture");
			const bool captureExists = capture != nullptr; if (capture) obs_source_release(capture);
			const QString player = RsMusicLocalPlayer::instance().executablePath();
			// The Control Hub is core infrastructure, not an optional autostart item.
			// Keep the legacy setup surface truthful while it remains in the dock.
			const bool autoStart = !player.isEmpty();
			RsMusicLocalPlayer::instance().sendUiCommand("SETUP_STATE",
				QString("%1\t%2").arg(captureExists ? "true" : "false", autoStart ? "true" : "false"));
		};
		if (command == "SETUP_STATUS") { publishSetupState(); return; }
		// The Control Hub is mandatory infrastructure now. Legacy AUTOSTART
		// messages are acknowledged but no longer add it to the optional list.
		if (command.startsWith("AUTOSTART\t")) { publishSetupState(); return; }
		if (command.startsWith("AUTH_ACTION\t")) {
			const QStringList parts = command.split('\t');
			if (parts.size() >= 3) emit twitchAuthActionRequested(parts[1], parts[2]);
			return;
		}
		if (command.startsWith("AUTH_SENDER\t")) {
			emit twitchSenderPreferenceRequested(command.section('\t', 1, 1) == "bot");
			return;
		}
		if (command == "AUTH_STATUS") { emit twitchAuthStatusRequested(); return; }
		if (command.startsWith("ACCOUNT_STATE\t")) {
			const QStringList parts = command.split('\t');
			if (parts.size() >= 4) emit twitchAccountStateReceived(parts[1], parts[2] == "connected", parts[3]);
			return;
		}
		if (command.startsWith("SETTING\t")) {
			const QStringList parts = command.split('\t');
			if (parts.size() < 3) return;
			const QString key = parts[1], value = parts[2];
			if (key == "requestsEnabled") rsMusicSetRequestsEnabled(value == "true" || value == "1");
			else if (key == "queueLimit") rsMusicSetMaxQueueTotal(value.toInt());
			else if (key == "userLimit") rsMusicSetMaxPerUser(value.toInt());
			else if (key == "maxTrackMinutes") rsMusicSetMaxTrackLengthSec(value.toInt() * 60);
			else if (key.startsWith("command.")) {
				const QStringList bits = key.split('.');
				if (bits.size() == 3) QSettings("RearSilver", "RearSilver-Stream-Suite").setValue(
					QString("music/commands/%1/%2").arg(bits[1], bits[2]), value == "true" || value == "1");
			}
			return;
		}
		if (command.startsWith("TOOL\t")) {
			const QString action = command.section('\t', 1, 1);
			const QByteArray raw = command.section('\t', 2).toUtf8();
			QJsonParseError error;
			const QJsonDocument json = QJsonDocument::fromJson("{\"value\":" + raw + "}", &error);
			const QJsonValue value = error.error == QJsonParseError::NoError
				? json.object().value("value") : QJsonValue(command.section('\t', 2));
			if (action == "setAutoLaunch") RsAutoStart::setAutoLaunchEnabled(value.toBool());
			else if (action == "setAutoClose") RsAutoStart::setAutoCloseEnabled(value.toBool());
			else if (action == "launchPrograms") RsAutoStart::launchPrograms();
			else if (action == "closePrograms") RsAutoStart::closePrograms();
			else if (action == "addProgram") RsAutoStart::addProgram(value.toString());
			else if (action == "removeProgram") RsAutoStart::removeProgram(value.toString());
			else if (action == "launchProgram") RsAutoStart::launchProgram(value.toString());
			else if (action == "closeProgram") RsAutoStart::closeProgram(value.toString());
			else if (action == "refreshCurrent") RsBrowserRefresh::refreshCurrentScene();
			else if (action == "refreshAll") RsBrowserRefresh::refreshAllScenes();
			else if (action == "dropText") { const QJsonObject o=value.toObject(); RsQuickText::showText(o.value("text").toString(),o.value("size").toInt(120),o.value("colour").toString("#ffffff"),o.value("font").toString("Sora")); }
			else if (action == "clearText") RsQuickText::clearAll();
			else if (action == "timerEnsure") { const QJsonObject o=value.toObject(); RsTimer::configure(o.value("label").toString("Timer"),o.value("mode").toString("countdown"),o.value("seconds").toInt(300)); }
			else if (action == "timerStyle") { const QJsonObject o=value.toObject(); RsTimer::configureStyle(o.value("textColour").toString("#ffffff"),o.value("labelSize").toInt(28),o.value("timeSize").toInt(84),o.value("shadow").toBool(true),o.value("background").toBool(false),o.value("backgroundColour").toString("#000000"),o.value("backgroundOpacity").toInt(70),o.value("backgroundRadius").toInt(48),o.value("hideWhenFinished").toBool(false)); }
			else if (action == "timerStart") RsTimer::start();
			else if (action == "timerPause") RsTimer::pauseResume();
			else if (action == "timerReset") RsTimer::reset();
			else if (action == "timerShow") RsTimer::setVisible(true);
			else if (action == "timerHide") RsTimer::setVisible(false);
			else if (action == "triggerReplay") RsInstantReplay::triggerReplay();
			else if (action == "hideReplay") RsInstantReplay::hideReplaySource();
			else if (action == "saveReplayFolder") RsInstantReplay::setReplayFolderOverride(value.toString());
			return;
		}
		if (command.startsWith("REQUEST_ACCEPTED\t")) {
			const QStringList parts = command.split('\t');
			emit songRequestAccepted(parts.value(1), parts.value(2), parts.value(3), parts.value(4), parts.value(5).toInt());
			return;
		}
		if (command.startsWith("REQUEST_REJECTED\t")) {
			const QStringList parts = command.split('\t');
			const QString id = parts.value(1);
			rsMusicRemoveRequestByTrackId(id);
			emit songRequestRejected(id, parts.mid(2).join(" "));
			return;
		}
		if (command.startsWith("REQUEST_REMOVED\t")) {
			const QStringList parts = command.split('\t');
			rsMusicRemoveRequestByTrackId(parts.value(1));
			emit songRequestRemoved(parts.value(1), parts.value(2), parts.value(3));
			return;
		}
		if (command.startsWith("REQUEST_REMOVE_FAILED\t")) {
			const QStringList parts = command.split('\t');
			emit songRequestRemoveFailed(parts.value(1), parts.mid(2).join(" "));
			return;
		}
		if (command.startsWith("NOW_PLAYING\t")) {
			const QStringList parts = command.split('\t');
			emit nowPlayingAnnounced(parts.value(1), parts.value(2), parts.value(3));
			return;
		}
		if (command != "CREATE_CAPTURE") return;
		obs_source_t *source = obs_get_source_by_name("Music Capture");
		if (!source) {
			obs_source_t *sceneSource = obs_frontend_get_current_scene(); obs_scene_t *scene = sceneSource ? obs_scene_from_source(sceneSource) : nullptr;
			source = obs_source_create("wasapi_process_output_capture", "Music Capture", nullptr, nullptr);
			if (source && scene) obs_scene_add(scene, source);
			if (sceneSource) obs_source_release(sceneSource);
		}
		if (source) { obs_frontend_open_source_properties(source); obs_source_release(source); }
		publishSetupState();
	});
	m_youtubeResolver = new RsMusicYouTubeResolver(this);
	QSettings settings("RearSilver", "RearSilver-Stream-Suite");
	m_localLibrary = settings.value("music/local/library").toStringList();
	if (m_state && !m_localLibrary.isEmpty()) {
		m_state->setActiveProvider(RsMusicProvider::LocalFile);
		m_state->setRequestsEnabled(false);
		rsMusicSetRequestsEnabled(false);
	}
	connect(&rsMusicBackendEvents(), &RsMusicBackendEvents::queueChanged, this,
		&RsMusicController::syncQueueFromBackend);
	connect(&RsMusicLocalPlayer::instance(), &RsMusicLocalPlayer::playbackStarted, this, [this]() {
		if (m_state && currentTrackUsesCompanion()) {
			m_state->setPlaybackStatus(RsMusicState::PlaybackStatus::Playing);
			if (m_state->currentTrack().provider == RsMusicProvider::YouTube && !m_youtubeCaptureRefreshed) {
				m_youtubeCaptureRefreshed = true;
				QTimer::singleShot(750, this, &RsMusicController::refreshYouTubeCaptureSource);
			}
		}
	});
	connect(&RsMusicLocalPlayer::instance(), &RsMusicLocalPlayer::playbackEnded, this, [this]() {
		// Automatic advancement is owned by the Suite Media Player.
	});
	connect(&RsMusicLocalPlayer::instance(), &RsMusicLocalPlayer::playbackProgress, this,
		[this](qint64 positionMs, qint64 durationMs) {
			if (m_state && currentTrackUsesCompanion())
				m_state->setPlaybackProgress(positionMs, durationMs);
		});
	connect(&RsMusicLocalPlayer::instance(), &RsMusicLocalPlayer::playbackMetadata, this,
		[this](const QString &title, const QString &artist, qint64 durationMs) {
			if (!m_state || !m_state->hasCurrentTrack() ||
			    m_state->currentTrack().provider != RsMusicProvider::YouTube)
				return;
			RsMusicTrack track = m_state->currentTrack();
			track.title = title.trimmed().isEmpty() ? track.title : title.trimmed();
			track.artist = artist.trimmed();
			track.durationSeconds = static_cast<int>((qMax<qint64>(0, durationMs) + 999) / 1000);
			m_state->setCurrentTrack(track);
		});
	connect(&RsMusicLocalPlayer::instance(), &RsMusicLocalPlayer::hubStateReceived, this,
		[this](const QByteArray &payload) {
			if (!m_state) return;
			const QJsonObject root = QJsonDocument::fromJson(payload).object();
			auto trackFromJson = [](const QJsonObject &value) {
				RsMusicTrack track;
				track.trackId = value.value("id").toString();
				track.provider = value.value("provider").toString() == "local" ? RsMusicProvider::LocalFile : RsMusicProvider::YouTube;
				track.providerTrackId = value.value("providerId").toString();
				track.providerUri = track.provider == RsMusicProvider::LocalFile ? track.providerTrackId :
					QString("https://www.youtube.com/watch?v=%1").arg(track.providerTrackId);
				track.title = value.value("title").toString();
				track.artist = value.value("artist").toString();
				track.album = value.value("album").toString();
				track.artworkUri = value.value("artworkUrl").toString();
				track.requestedBy = value.value("requestedBy").toString();
				track.durationSeconds = value.value("durationSeconds").toInt();
				track.isFromPlaylist = !value.value("request").toBool();
				return track;
			};
			const QJsonValue currentValue = root.value("current");
			if (currentValue.isObject()) {
				RsMusicTrack track = trackFromJson(currentValue.toObject());
				const bool changed = !m_state->hasCurrentTrack() || m_state->currentTrack().trackId != track.trackId;
				if (!changed && m_state->currentTrack().artworkUri.startsWith("file:", Qt::CaseInsensitive))
					track.artworkUri = m_state->currentTrack().artworkUri;
				m_state->setCurrentTrack(track);
				if (changed && track.provider == RsMusicProvider::YouTube) hydrateCurrentYouTubeArtwork(track);
			}
			else m_state->clearCurrentTrack();
			QVector<RsMusicTrack> queue;
			for (const QJsonValue &value : root.value("queue").toArray())
				if (value.isObject()) queue.append(trackFromJson(value.toObject()));
			m_state->setQueue(queue);
			const bool localSource = root.value("activeSource").toString() == "local";
			m_state->setActiveProvider(localSource ? RsMusicProvider::LocalFile : RsMusicProvider::YouTube);
			m_state->setPlaylistLabel(localSource ? "Local files" : root.value("fallbackLabel").toString("YouTube fallback"));
			m_state->setRequestsEnabled(localSource ? false : rsMusicRequestsEnabled());
			m_state->setPlaybackProgress(root.value("positionMs").toVariant().toLongLong(),
				root.value("durationMs").toVariant().toLongLong());
			const QString status = root.value("status").toString();
			m_state->setPlaybackStatus(status == "playing" ? RsMusicState::PlaybackStatus::Playing :
				(status == "paused" ? RsMusicState::PlaybackStatus::Paused : RsMusicState::PlaybackStatus::Stopped));
		});
	syncQueueFromBackend();
}

bool RsMusicController::currentTrackUsesCompanion() const
{
	if (!m_state || !m_state->hasCurrentTrack())
		return false;
	const RsMusicProvider provider = m_state->currentTrack().provider;
	return provider == RsMusicProvider::LocalFile || provider == RsMusicProvider::YouTube;
}

bool RsMusicController::currentTrackIsLocal() const
{
	return m_state && m_state->hasCurrentTrack() &&
	       m_state->currentTrack().provider == RsMusicProvider::LocalFile;
}

void RsMusicController::syncQueueFromBackend()
{
	if (!m_state)
		return;

	const QVector<RsMusicTrack> tracks = rsMusicPlaybackQueueSnapshot();
	m_state->setQueue(tracks);
}

void RsMusicController::actionPlay()
{
	if (!m_state)
		return;

	if (!m_state->hasCurrentTrack() && m_state->activeProvider() == RsMusicProvider::LocalFile &&
	    !m_localLibrary.isEmpty()) {
		playLocalIndex(0);
		return;
	}
	if (!m_state->hasCurrentTrack() && m_state->activeProvider() == RsMusicProvider::YouTube) {
		RsMusicLocalPlayer::instance().resume(); return;
	}

	if (currentTrackUsesCompanion())
		RsMusicLocalPlayer::instance().resume();
	else
		rsMusicResume();
	m_state->setPlaybackStatus(RsMusicState::PlaybackStatus::Playing);
}

void RsMusicController::actionPause()
{
	if (!m_state)
		return;

	if (currentTrackUsesCompanion())
		RsMusicLocalPlayer::instance().pause();
	else
		rsMusicPause();
	m_state->setPlaybackStatus(RsMusicState::PlaybackStatus::Paused);
}

void RsMusicController::actionStop()
{
	if (!m_state)
		return;

	if (currentTrackUsesCompanion()) {
		RsMusicLocalPlayer::instance().stop();
		m_state->clearCurrentTrack();
	} else {
		rsMusicStop();
	}
	m_state->setPlaybackStatus(RsMusicState::PlaybackStatus::Stopped);
}

void RsMusicController::actionRestart()
{
	if (!m_state)
		return;

	if (currentTrackUsesCompanion())
		RsMusicLocalPlayer::instance().restart();
	else
		rsMusicRestart();
	m_state->setPlaybackStatus(RsMusicState::PlaybackStatus::Playing);
}

void RsMusicController::actionSkip(const QString &source)
{
	if (currentTrackUsesCompanion()) {
		RsMusicLocalPlayer::instance().skip();
		return;
	}
	rsMusicSkip(source);
}

bool RsMusicController::actionPlayLocalFile(const QString &filePath)
{
	const QString absolutePath = QFileInfo(filePath).absoluteFilePath();
	int index = m_localLibrary.indexOf(absolutePath);
	if (index < 0) {
		m_localLibrary.append(absolutePath);
		setLocalLibrary(m_localLibrary);
		index = m_localLibrary.indexOf(absolutePath);
	}
	return playLocalIndex(index);
}

bool RsMusicController::actionPlayYouTubeVideo(const QString &url)
{
	if (!m_state)
		return false;
	const QString videoId = rsMusicExtractYoutubeVideoId(url);
	if (videoId.isEmpty())
		return false;
	RsMusicTrack track;
	track.trackId = QString("youtube_%1").arg(videoId);
	track.provider = RsMusicProvider::YouTube;
	track.providerTrackId = videoId;
	track.providerUri = QString("https://www.youtube.com/watch?v=%1").arg(videoId);
	track.title = QString("YouTube video %1").arg(videoId);
	m_youtubeCaptureRefreshed = false;
	if (!RsMusicLocalPlayer::instance().playYouTubeVideo(track))
		return false;
	m_state->setActiveProvider(RsMusicProvider::YouTube);
	m_state->setPlaylistLabel("YouTube");
	m_state->setCurrentTrack(track);
	m_state->setPlaybackStatus(RsMusicState::PlaybackStatus::Playing);
	m_state->setRequestsEnabled(true);
	rsMusicSetRequestsEnabled(true);
	return true;
}

bool RsMusicController::playScheduledTrack(const RsMusicTrack &track)
{
	if (!m_state || track.provider != RsMusicProvider::YouTube || track.providerTrackId.trimmed().isEmpty())
		return false;
	if (!RsMusicLocalPlayer::instance().playYouTubeVideo(track))
		return false;
	m_youtubeCaptureRefreshed = false;
	rsMusicRecordScheduledTrackStarted(track);
	m_state->setActiveProvider(RsMusicProvider::YouTube);
	m_state->setPlaylistLabel(track.isFromPlaylist ? "YouTube fallback" : "YouTube requests");
	m_state->setCurrentTrack(track);
	m_state->setPlaybackStatus(RsMusicState::PlaybackStatus::Playing);
	m_state->setRequestsEnabled(true);
	hydrateCurrentYouTubeArtwork(track);
	return true;
}

void RsMusicController::hydrateCurrentYouTubeArtwork(const RsMusicTrack &track)
{
	if (!m_youtubeResolver || !m_state || track.provider != RsMusicProvider::YouTube)
		return;
	const QString trackId = track.trackId;
	m_youtubeResolver->cacheArtwork(track, [this, trackId](const QString &localUri) {
		if (localUri.isEmpty() || !m_state || !m_state->hasCurrentTrack() ||
		    m_state->currentTrack().trackId != trackId)
			return;
		RsMusicTrack current = m_state->currentTrack();
		current.artworkUri = localUri;
		m_state->setCurrentTrack(current);
	});
}

bool RsMusicController::playNextScheduledTrack()
{
	RsMusicTrack track;
	while (rsMusicTakeNextScheduledTrack(track)) {
		if (playScheduledTrack(track))
			return true;
	}
	return false;
}

void RsMusicController::refreshYouTubeCaptureSource()
{
	obs_source_t *source = obs_get_source_by_name("Music Capture");
	if (!source || QString::fromUtf8(obs_source_get_id(source)) != "wasapi_process_output_capture") {
		if (source)
			obs_source_release(source);
		return;
	}
	obs_data_t *settings = obs_source_get_settings(source);
	const long long originalPriority = obs_data_get_int(settings, "priority");
	obs_data_set_int(settings, "priority", originalPriority == 0 ? 1 : 0);
	obs_source_update(source, settings);
	obs_data_release(settings);
	obs_source_release(source);

	QTimer::singleShot(200, this, [originalPriority]() {
		obs_source_t *capture = obs_get_source_by_name("Music Capture");
		if (!capture)
			return;
		obs_data_t *captureSettings = obs_source_get_settings(capture);
		obs_data_set_int(captureSettings, "priority", originalPriority);
		obs_source_update(capture, captureSettings);
		obs_data_release(captureSettings);
		obs_source_release(capture);
	});
}

void RsMusicController::actionPrevious()
{
	if (currentTrackUsesCompanion()) {
		RsMusicLocalPlayer::instance().previous(); return;
	}
	if (!currentTrackIsLocal() || m_localLibrary.isEmpty()) return;
	if (m_localLibrary.size() > 1)
		m_localLibrary.prepend(m_localLibrary.takeLast());
	QSettings("RearSilver", "RearSilver-Stream-Suite").setValue("music/local/library", m_localLibrary);
	emit localLibraryChanged();
	playLocalIndex(0);
}

void RsMusicController::actionSeek(qint64 positionMs)
{
	if (currentTrackUsesCompanion())
		RsMusicLocalPlayer::instance().seekTo(positionMs);
}

void RsMusicController::setLocalLibrary(const QStringList &files)
{
	QStringList normalised;
	for (const QString &path : files) {
		const QString absolutePath = QFileInfo(path).absoluteFilePath();
		if (!absolutePath.isEmpty() && !normalised.contains(absolutePath, Qt::CaseInsensitive))
			normalised.append(absolutePath);
	}
	m_localLibrary = normalised;
	QSettings("RearSilver", "RearSilver-Stream-Suite").setValue("music/local/library", m_localLibrary);
	if (m_state && !m_localLibrary.isEmpty()) {
		m_state->setActiveProvider(RsMusicProvider::LocalFile);
		m_state->setRequestsEnabled(false);
		rsMusicSetRequestsEnabled(false);
	} else if (m_state) {
		m_state->setActiveProvider(RsMusicProvider::Unknown);
	}
	emit localLibraryChanged();
}

QStringList RsMusicController::localLibrary() const
{
	return m_localLibrary;
}

void RsMusicController::shuffleLocalLibrary()
{
	if (m_localLibrary.size() < 2)
		return;

	const int firstShuffledIndex = currentTrackIsLocal() ? 1 : 0;
	for (int index = m_localLibrary.size() - 1; index > firstShuffledIndex; --index) {
		const int swapIndex = firstShuffledIndex +
			QRandomGenerator::global()->bounded(index - firstShuffledIndex + 1);
		m_localLibrary.swapItemsAt(index, swapIndex);
	}
	QSettings("RearSilver", "RearSilver-Stream-Suite").setValue("music/local/library", m_localLibrary);
	emit localLibraryChanged();
}

bool RsMusicController::playLocalIndex(int index)
{
	if (!m_state || index < 0 || index >= m_localLibrary.size())
		return false;

	const QFileInfo file(m_localLibrary.at(index));
	if (!file.exists() || !file.isFile())
		return false;

	if (!currentTrackIsLocal() && m_state->hasCurrentTrack())
		rsMusicStop();
	if (index > 0) {
		m_localLibrary.prepend(m_localLibrary.takeAt(index));
		QSettings("RearSilver", "RearSilver-Stream-Suite").setValue("music/local/library", m_localLibrary);
		emit localLibraryChanged();
	}
	RsMusicTrack track;
	track.trackId = QString("local_%1").arg(QDateTime::currentMSecsSinceEpoch());
	track.provider = RsMusicProvider::LocalFile;
	track.providerTrackId = file.absoluteFilePath();
	track.providerUri = file.absoluteFilePath();
	track.title = file.completeBaseName();
	RsMusicMetadata::enrichLocalTrack(track, file.absoluteFilePath());
	track.isFromPlaylist = true;
	if (!RsMusicLocalPlayer::instance().playFile(track))
		return false;
	m_state->setActiveProvider(RsMusicProvider::LocalFile);
	m_state->setPlaylistLabel("Local files");
	m_state->setCurrentTrack(track);
	m_state->setPlaybackStatus(RsMusicState::PlaybackStatus::Playing);
	return true;
}

void RsMusicController::playNextLocalTrack()
{
	if (!m_state || m_localLibrary.isEmpty()) {
		if (m_state) {
			m_state->setPlaybackStatus(RsMusicState::PlaybackStatus::Stopped);
			m_state->clearCurrentTrack();
		}
		return;
	}

	if (m_localLibrary.size() > 1)
		m_localLibrary.append(m_localLibrary.takeFirst());
	QSettings("RearSilver", "RearSilver-Stream-Suite").setValue("music/local/library", m_localLibrary);
	emit localLibraryChanged();

	for (int attempt = 0; attempt < m_localLibrary.size(); ++attempt) {
		if (playLocalIndex(0))
			return;
		m_localLibrary.append(m_localLibrary.takeFirst());
	}

	m_state->setPlaybackStatus(RsMusicState::PlaybackStatus::Stopped);
	m_state->clearCurrentTrack();
}

RsMusicRequestResult RsMusicController::actionSongRequest(const QString &userId, const QString &displayName,
						   const QString &query, int requesterLevel)
{
	// The companion Media Player owns request admission. Bypass the legacy
	// dock-side limits here so its persisted role and limit settings are the
	// single source of truth.
	const RsMusicRequestResult result = rsMusicRequestSong(userId, displayName, query, true);
	if (result.accepted)
		RsMusicLocalPlayer::instance().requestYouTubeTrack(result.trackId, userId, displayName, requesterLevel,
			query.trimmed());
	return result;
}

void RsMusicController::actionRemoveRequest(const QString &requestId)
{
	RsMusicLocalPlayer::instance().removeRequest(requestId.trimmed());
}
