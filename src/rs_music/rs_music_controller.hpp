#pragma once

#include <QObject>
#include <QString>

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

	// ---- song requests ----
	RsMusicRequestResult actionSongRequest(const QString &userId, const QString &displayName, const QString &query,
					       bool isModOrBroadcaster = false);

private:
	void syncQueueFromBackend();

	RsMusicState *m_state = nullptr; // non-owning
};
