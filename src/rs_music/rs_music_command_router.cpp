#include "rs_music_command_router.hpp"
#include "rs_music_controller.hpp" // interface only, implementation elsewhere

#include <QString>

extern "C" {
#include <obs-module.h>
}

// Utility: returns remainder after command token
static QString commandArgs(const QString &msg, int cmdLen)
{
	if (msg.length() <= cmdLen)
		return QString();

	return msg.mid(cmdLen).trimmed();
}

RsMusicCommandRouter::RsMusicCommandRouter(RsMusicController *controller, QObject *parent)
	: QObject(parent),
	  m_controller(controller)
{
}

void RsMusicCommandRouter::ingestChatMessage(const RsMusicChatContext &ctx, const QString &messageText)
{
	if (!m_controller)
		return;

	const QString msg = messageText.trimmed();
	if (msg.isEmpty())
		return;

	const QString lower = msg.toLower();

	// ---- Song request (everyone allowed) ----
	if (lower.startsWith("!sr")) {
		handleSongRequest(ctx, commandArgs(msg, 3));
		return;
	}

	// ---- Control commands (mods/broadcaster only) ----
	if (lower == "!play") {
		handlePlay(ctx);
		return;
	}

	if (lower == "!pause") {
		handlePause(ctx);
		return;
	}

	if (lower == "!skip") {
		handleSkip(ctx);
		return;
	}

	if (lower == "!restart") {
		handleRestart(ctx);
		return;
	}
}

bool RsMusicCommandRouter::isControlAllowed(const RsMusicChatContext &ctx) const
{
	return ctx.isMod || ctx.isBroadcaster;
}

void RsMusicCommandRouter::handleSongRequest(const RsMusicChatContext &ctx, const QString &args)
{
	if (args.isEmpty()) {
		emit feedbackMessage(QString("%1: usage is !sr <song name or url>").arg(ctx.displayName));
		return;
	}

	// ---- STUB ----
	// Queue / validation happens later.
	// For now we only forward intent.
	m_controller->actionSongRequest(ctx.userId, ctx.displayName, args);

	emit feedbackMessage(QString("🎵 %1 requested: %2").arg(ctx.displayName, args));
}

void RsMusicCommandRouter::handlePlay(const RsMusicChatContext &ctx)
{
	if (!isControlAllowed(ctx)) {
		emit feedbackMessage(
			QString("%1: you don't have permission to control playback.").arg(ctx.displayName));
		return;
	}

	m_controller->actionPlay();
	emit feedbackMessage("▶️ Playback resumed");
}

void RsMusicCommandRouter::handlePause(const RsMusicChatContext &ctx)
{
	if (!isControlAllowed(ctx)) {
		emit feedbackMessage(
			QString("%1: you don't have permission to control playback.").arg(ctx.displayName));
		return;
	}

	m_controller->actionPause();
	emit feedbackMessage("⏸️ Playback paused");
}

void RsMusicCommandRouter::handleSkip(const RsMusicChatContext &ctx)
{
	if (!isControlAllowed(ctx)) {
		emit feedbackMessage(QString("%1: you don't have permission to skip tracks.").arg(ctx.displayName));
		return;
	}

	// ---- STUB ----
	// Skip does NOT change state yet (backend required)
	m_controller->actionSkip("chat");

	emit feedbackMessage("⏭️ Track skipped");
}

void RsMusicCommandRouter::handleRestart(const RsMusicChatContext &ctx)
{
	if (!isControlAllowed(ctx)) {
		emit feedbackMessage(
			QString("%1: you don't have permission to restart playback.").arg(ctx.displayName));
		return;
	}

	m_controller->actionRestart();
	emit feedbackMessage("🔁 Track restarted");
}
