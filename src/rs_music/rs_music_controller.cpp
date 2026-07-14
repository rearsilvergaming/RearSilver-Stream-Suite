#include "rs_music_controller.hpp"
#include "state/rs_music_state.hpp"

extern "C" {
#include <obs-module.h>
}

RsMusicController::RsMusicController(RsMusicState *state, QObject *parent) : QObject(parent), m_state(state) {}

void RsMusicController::actionPlay()
{
	if (!m_state)
		return;

	// ---- STATE ----
	m_state->setPlaybackStatus(RsMusicState::PlaybackStatus::Playing);

	// ---- BACKEND STUB ----
	// rs_music_ws: send PLAY command to browser player
	// (Not implemented in this phase)
	//
	// Example (future):
	// m_wsClient->sendPlay();
}

void RsMusicController::actionPause()
{
	if (!m_state)
		return;

	// ---- STATE ----
	m_state->setPlaybackStatus(RsMusicState::PlaybackStatus::Paused);

	// ---- BACKEND STUB ----
	// rs_music_ws: send PAUSE command
}

void RsMusicController::actionRestart()
{
	if (!m_state)
		return;

	// Restart semantics:
	// - treated as re-playing current track
	// - UI already expects Playing state
	m_state->setPlaybackStatus(RsMusicState::PlaybackStatus::Playing);

	// ---- BACKEND STUB ----
	// rs_music_ws: send RESTART command
}

void RsMusicController::actionSkip(const QString &source)
{
	Q_UNUSED(source);

	// IMPORTANT:
	// Skip is a command, NOT a state mutation.
	// We do NOT fake track advancement.
	// State remains unchanged until backend exists.

	blog(LOG_INFO, "[RS Music] Skip requested (%s) - backend not wired yet", source.toUtf8().constData());

	// ---- BACKEND STUB ----
	// rs_music_ws: send SKIP command
}

void RsMusicController::actionSongRequest(const QString &userId, const QString &displayName, const QString &query)
{
	Q_UNUSED(userId);
	Q_UNUSED(displayName);
	Q_UNUSED(query);

	// IMPORTANT:
	// This phase ONLY records intent.
	// We do NOT validate, search, queue, or lie to the UI.

	blog(LOG_INFO, "[RS Music] Song request received (stub): %s", query.toUtf8().constData());

	// ---- FUTURE ----
	// - validation rules
	// - queue insertion
	// - per-user limits
	// - backend search / resolve
}
