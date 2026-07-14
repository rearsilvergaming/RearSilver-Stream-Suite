#pragma once

#include <QObject>
#include <QString>

/*
 * RsMusicCommandRouter
 *
 * - Parses chat commands (!sr, !play, !pause, !skip, !restart)
 * - Enforces permissions (mods/broadcaster only for control)
 * - Calls RsMusicController actions
 * - Emits optional feedback text (sending handled elsewhere)
 *
 * DOES NOT:
 * - Read IRC
 * - Send IRC
 * - Touch UI
 * - Own state
 */

class RsMusicController;

// Normalised chat context coming from IRC reader
struct RsMusicChatContext {
	QString userId;
	QString displayName;
	bool isMod = false;
	bool isBroadcaster = false;
};

class RsMusicCommandRouter : public QObject {
	Q_OBJECT

public:
	explicit RsMusicCommandRouter(RsMusicController *controller, QObject *parent = nullptr);

	// Entry point from IRC reader
	void ingestChatMessage(const RsMusicChatContext &ctx, const QString &messageText);

signals:
	// Optional feedback text for chat (sending is handled elsewhere)
	void feedbackMessage(const QString &text);

private:
	// ---- helpers ----
	bool isControlAllowed(const RsMusicChatContext &ctx) const;

	void handleSongRequest(const RsMusicChatContext &ctx, const QString &args);

	void handlePlay(const RsMusicChatContext &ctx);
	void handlePause(const RsMusicChatContext &ctx);
	void handleSkip(const RsMusicChatContext &ctx);
	void handleRestart(const RsMusicChatContext &ctx);

private:
	RsMusicController *m_controller = nullptr; // non-owning
};
