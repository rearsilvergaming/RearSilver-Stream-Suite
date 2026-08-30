#include "rs_music_controller.hpp"
#include "state/rs_music_state.hpp"
#include "rs_music_local_player.hpp"
#include "rs_music_helpers.hpp"
#include "rs_music_metadata.hpp"
#include "rs_music_youtube_resolver.hpp"
#include "enhancements/rs_auto_start.hpp"
#include "enhancements/rs_browser_refresh.hpp"
#include "rs_instant_replay.hpp"
#include "rs_stream_overlay_manager.hpp"
#include "rs_stream_overlay_server.hpp"
#include "rs_stream_timer.hpp"

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

void rsPublishStreamOverlayPlacementState()
{
	const QByteArray quickText = QJsonDocument(RsStreamOverlayManager::quickTextStatus()).toJson(QJsonDocument::Compact);
	RsMusicLocalPlayer::instance().sendUiCommand("QUICK_TEXT_STATE", QString::fromUtf8(quickText));
	const QByteArray timer = QJsonDocument(RsStreamTimer::instance().status()).toJson(QJsonDocument::Compact);
	RsMusicLocalPlayer::instance().sendUiCommand("TIMER_STATE", QString::fromUtf8(timer));
	const QByteArray replay = QJsonDocument(hub_replay::RsInstantReplay::replayState()).toJson(QJsonDocument::Compact);
	RsMusicLocalPlayer::instance().sendUiCommand("REPLAY_STATE", QString::fromUtf8(replay));
	const QByteArray musicOverlay = QJsonDocument(RsStreamOverlayManager::musicOverlayStatus()).toJson(QJsonDocument::Compact);
	RsMusicLocalPlayer::instance().sendUiCommand("MUSIC_OVERLAY_STATE", QString::fromUtf8(musicOverlay));
}

static void publishReplayState(const QJsonObject &state)
{
	const QByteArray json = QJsonDocument(state).toJson(QJsonDocument::Compact);
	RsMusicLocalPlayer::instance().sendUiCommand("REPLAY_STATE", QString::fromUtf8(json));
}

static void publishReplayState()
{
	publishReplayState(hub_replay::RsInstantReplay::replayState());
}

static void publishTimerState(const QJsonObject &state)
{
	const QByteArray json = QJsonDocument(state).toJson(QJsonDocument::Compact);
	RsMusicLocalPlayer::instance().sendUiCommand("TIMER_STATE", QString::fromUtf8(json));
}

static void publishQuickTextState(const QJsonObject &state)
{
	const QByteArray json = QJsonDocument(state).toJson(QJsonDocument::Compact);
	RsMusicLocalPlayer::instance().sendUiCommand("QUICK_TEXT_STATE", QString::fromUtf8(json));
}

static void publishMusicOverlayState(const QJsonObject &state)
{
	const QByteArray json = QJsonDocument(state).toJson(QJsonDocument::Compact);
	RsMusicLocalPlayer::instance().sendUiCommand("MUSIC_OVERLAY_STATE", QString::fromUtf8(json));
}

void rsExecuteStreamToolQuickAction(RsStreamToolQuickAction action)
{
	switch (action) {
	case RsStreamToolQuickAction::RefreshCurrentBrowsers:
		RsBrowserRefresh::refreshCurrentScene();
		RsMusicLocalPlayer::instance().sendUiCommand("BROWSER_REFRESH_STATE", "current");
		break;
	case RsStreamToolQuickAction::RefreshAllBrowsers:
		RsBrowserRefresh::refreshAllScenes();
		RsMusicLocalPlayer::instance().sendUiCommand("BROWSER_REFRESH_STATE", "all");
		break;
	case RsStreamToolQuickAction::TriggerReplay:
		publishReplayState(hub_replay::RsInstantReplay::triggerReplay());
		break;
	case RsStreamToolQuickAction::ShowReplay:
		publishReplayState(hub_replay::RsInstantReplay::showReplaySource());
		break;
	case RsStreamToolQuickAction::HideReplay:
		publishReplayState(hub_replay::RsInstantReplay::hideReplaySource());
		break;
	case RsStreamToolQuickAction::ShowQuickText:
		publishQuickTextState(RsStreamOverlayManager::showQuickTextInCurrentScene());
		break;
	case RsStreamToolQuickAction::HideQuickText:
		publishQuickTextState(RsStreamOverlayManager::clearQuickTextInCurrentScene());
		break;
	case RsStreamToolQuickAction::StartTimer: {
		QJsonObject state = RsStreamTimer::instance().setup();
		if (state.value("setupComplete").toBool() && !state.value("conflict").toBool())
			state = RsStreamTimer::instance().startTimer();
		publishTimerState(state);
		break;
	}
	case RsStreamToolQuickAction::PauseTimer:
		publishTimerState(RsStreamTimer::instance().pauseResume());
		break;
	case RsStreamToolQuickAction::ResetTimer:
		publishTimerState(RsStreamTimer::instance().reset());
		break;
	case RsStreamToolQuickAction::ShowTimer: {
		QJsonObject state = RsStreamTimer::instance().setup();
		if (state.value("setupComplete").toBool() && !state.value("conflict").toBool())
			state = RsStreamTimer::instance().setVisible(true);
		publishTimerState(state);
		break;
	}
	case RsStreamToolQuickAction::HideTimer:
		publishTimerState(RsStreamTimer::instance().setVisible(false));
		break;
	case RsStreamToolQuickAction::ShowMusicOverlay:
		publishMusicOverlayState(RsStreamOverlayManager::showMusicOverlayInCurrentScene());
		break;
	case RsStreamToolQuickAction::HideMusicOverlay:
		publishMusicOverlayState(RsStreamOverlayManager::hideMusicOverlayInCurrentScene());
		break;
	}
}

RsMusicController::RsMusicController(RsMusicState *state, QObject *parent) : QObject(parent), m_state(state)
{
	hub_replay::RsInstantReplay::setStateChangedCallback(publishReplayState);
	auto &localPlayer = RsMusicLocalPlayer::instance();
	connect(&localPlayer, &RsMusicLocalPlayer::hubConnectionChanged, this, [this](bool connected) {
		if (connected) {
			rsPublishStreamOverlayPlacementState();
			return;
		}
		m_replayBufferConfigurationReceived = false;
		m_replayFrameConfigurationReceived = false;
	});
	connect(&localPlayer, &RsMusicLocalPlayer::hostCommandReceived, this, [this](const QString &command) {
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
			if (parts.size() >= 4) emit twitchAccountStateReceived(parts[1], parts[2], parts[3]);
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
			else if (key == "overlayPlacementMode") {
				RsStreamOverlayManager::setPlacementMode(value);
				hub_replay::RsInstantReplay::reconcilePlacementMode();
				rsPublishStreamOverlayPlacementState();
			}
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
			else if (action == "refreshCurrent") rsExecuteStreamToolQuickAction(RsStreamToolQuickAction::RefreshCurrentBrowsers);
			else if (action == "refreshAll") rsExecuteStreamToolQuickAction(RsStreamToolQuickAction::RefreshAllBrowsers);
			else if (action == "quickTextConfig") { const QJsonObject o=value.toObject(); RsStreamOverlayServer::instance().setQuickTextState(o); emit quickTextConfigurationReady(!o.value("text").toString().trimmed().isEmpty()); }
			else if (action == "quickTextShow") { const QJsonObject o=value.toObject(); RsStreamOverlayServer::instance().setQuickTextState(o); emit quickTextConfigurationReady(!o.value("text").toString().trimmed().isEmpty()); publishQuickTextState(RsStreamOverlayManager::showQuickTextInCurrentScene()); }
			else if (action == "quickTextHide") rsExecuteStreamToolQuickAction(RsStreamToolQuickAction::HideQuickText);
			else if (action == "quickTextClear") { QJsonObject state=RsStreamOverlayServer::instance().quickTextState(); state["text"]=""; RsStreamOverlayServer::instance().setQuickTextState(state); emit quickTextConfigurationReady(false); publishQuickTextState(RsStreamOverlayManager::clearQuickTextInCurrentScene()); }
			else if (action == "quickTextStatus") publishQuickTextState(RsStreamOverlayManager::quickTextStatus());
			else if (action == "timerConfig") { const QJsonObject o=value.toObject(); RsStreamTimer::instance().configure(o); emit timerConfigurationReady(o.value("mode").toString("countdown")); publishTimerState(RsStreamTimer::instance().status()); }
			else if (action == "timerSetup") publishTimerState(RsStreamTimer::instance().setup());
			else if (action == "timerStatus") publishTimerState(RsStreamTimer::instance().status());
			else if (action == "timerSoundTest") publishTimerState(RsStreamTimer::instance().testSound());
			else if (action == "timerStart") rsExecuteStreamToolQuickAction(RsStreamToolQuickAction::StartTimer);
			else if (action == "timerPause") rsExecuteStreamToolQuickAction(RsStreamToolQuickAction::PauseTimer);
			else if (action == "timerReset") rsExecuteStreamToolQuickAction(RsStreamToolQuickAction::ResetTimer);
			else if (action == "timerShow") rsExecuteStreamToolQuickAction(RsStreamToolQuickAction::ShowTimer);
			else if (action == "timerHide") rsExecuteStreamToolQuickAction(RsStreamToolQuickAction::HideTimer);
			else if (action == "musicOverlayStatus") publishMusicOverlayState(RsStreamOverlayManager::musicOverlayStatus());
			else if (action == "musicOverlayRefresh") publishMusicOverlayState(RsStreamOverlayManager::refreshMusicOverlaySettings());
			else if (action == "musicOverlayShow") rsExecuteStreamToolQuickAction(RsStreamToolQuickAction::ShowMusicOverlay);
			else if (action == "musicOverlayHide") rsExecuteStreamToolQuickAction(RsStreamToolQuickAction::HideMusicOverlay);
			else if (action == "triggerReplay") rsExecuteStreamToolQuickAction(RsStreamToolQuickAction::TriggerReplay);
			else if (action == "hideReplay") rsExecuteStreamToolQuickAction(RsStreamToolQuickAction::HideReplay);
			else if (action == "replayShow") rsExecuteStreamToolQuickAction(RsStreamToolQuickAction::ShowReplay);
			else if (action == "replayRepair") publishReplayState(hub_replay::RsInstantReplay::repairReplaySource());
			else if (action == "replayStart") hub_replay::RsInstantReplay::startReplayBuffer();
			else if (action == "replayStop") hub_replay::RsInstantReplay::stopReplayBuffer();
			else if (action == "openReplayFolder") hub_replay::RsInstantReplay::openReplayFolder();
			else if (action == "replayBufferConfig") { const QJsonObject o=value.toObject(); hub_replay::RsInstantReplay::configureReplayBuffer(o.value("seconds").toInt(10),o.value("autoStart").toBool(false),o.value("autoHide").toBool(true)); m_replayBufferConfigurationReceived=true; if(m_replayFrameConfigurationReceived) emit replayConfigurationReady(); }
			else if (action == "replayFrameConfig") { const QJsonObject o=value.toObject(); hub_replay::RsInstantReplay::configureReplayFrame(o.value("title").toString("INSTANT REPLAY"),o.value("font").toString("Sora"),o.value("fontWeight").toString("bold"),o.value("alignment").toString("left"),o.value("background").toString("#0b0f14"),o.value("accent").toString("#00d4ff"),o.value("textColour").toString("#e6e8eb"),o.value("opacity").toInt(92),o.value("sizeStep").toString("large"),o.value("borderStep").toString("medium"),o.value("radiusStep").toString("rounded")); m_replayFrameConfigurationReceived=true; if(m_replayBufferConfigurationReceived) emit replayConfigurationReady(); }
			if ((action.startsWith("replay") || action == "hideReplay") && action != "triggerReplay" && action != "replayShow" && action != "hideReplay") publishReplayState();
			return;
		}
		if (command.startsWith("REQUEST_ACCEPTED\t")) {
			const QStringList parts = command.split('\t');
			const QString pendingId = parts.value(6);
			if (!pendingId.isEmpty())
				rsMusicRemoveRequestByTrackId(pendingId);
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
				const QString provider = value.value("provider").toString();
				if (provider == "local") track.provider = RsMusicProvider::LocalFile;
				else if (provider == "spotify") track.provider = RsMusicProvider::Spotify;
				else if (provider == "external" || provider == "system") track.provider = RsMusicProvider::SystemMedia;
				else if (provider == "youtube") track.provider = RsMusicProvider::YouTube;
				track.providerTrackId = value.value("providerId").toString();
				if (track.provider == RsMusicProvider::YouTube)
					track.providerUri = QString("https://www.youtube.com/watch?v=%1").arg(track.providerTrackId);
				else
					track.providerUri = track.providerTrackId;
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
			const QString activeSource = root.value("activeSource").toString();
			RsMusicProvider activeProvider = RsMusicProvider::YouTube;
			if (activeSource == "local") activeProvider = RsMusicProvider::LocalFile;
			else if (activeSource == "external") {
				activeProvider = m_state->hasCurrentTrack() ? m_state->currentTrack().provider : RsMusicProvider::SystemMedia;
				if (activeProvider != RsMusicProvider::Spotify && activeProvider != RsMusicProvider::SystemMedia)
					activeProvider = RsMusicProvider::SystemMedia;
			}
			m_state->setActiveProvider(activeProvider);
			m_state->setPlaylistLabel(activeSource == "local" ? "Local files" :
				(activeSource == "external" ? rsMusicProviderDisplayName(activeProvider) :
				 root.value("fallbackLabel").toString("YouTube fallback")));
			m_state->setRequestsEnabled(activeSource == "local" ? false : rsMusicRequestsEnabled());
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
