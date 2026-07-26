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
	connect(m_controller, &RsMusicController::songRequestAccepted, this,
		[this](const QString &id, const QString &title, const QString &artist, const QString &requester, int position) {
			emit feedbackMessage(QString("🎵 %1 — %2, requested by %3, was added as request #%4 (queue position %5).")
				.arg(title, artist, requester, id).arg(position));
		});
	connect(m_controller, &RsMusicController::songRequestRejected, this,
		[this](const QString &, const QString &message) { emit feedbackMessage(QString("Request rejected: %1").arg(message)); });
	connect(m_controller, &RsMusicController::songRequestRemoved, this,
		[this](const QString &id, const QString &title, const QString &artist) {
			emit feedbackMessage(QString("🗑️ Removed request #%1: %2 — %3").arg(id, title, artist));
		});
	connect(m_controller, &RsMusicController::songRequestRemoveFailed, this,
		[this](const QString &id, const QString &message) {
			emit feedbackMessage(QString("Could not remove request #%1: %2").arg(id, message));
		});
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

	if (lower == "!previous" || lower == "!prev") {
		handlePrevious(ctx);
		return;
	}

	if (lower == "!remove" || lower.startsWith("!remove ")) {
		handleRemove(ctx, commandArgs(msg, 7));
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

	const RsMusicRequestResult result =
		m_controller->actionSongRequest(ctx.userId, ctx.displayName, args, isControlAllowed(ctx));
	if (!result.accepted) {
		emit feedbackMessage(QString("%1: %2").arg(ctx.displayName, result.reason));
		return;
	}

	// Resolution is asynchronous. The final title, artist, stable request ID and
	// queue position are announced only after the Media Player accepts the track.
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

void RsMusicCommandRouter::handlePrevious(const RsMusicChatContext &ctx)
{
	if (!isControlAllowed(ctx)) {
		emit feedbackMessage(QString("%1: you don't have permission to control playback.").arg(ctx.displayName));
		return;
	}
	m_controller->actionPrevious();
	emit feedbackMessage("⏮️ Playing the previous track");
}

void RsMusicCommandRouter::handleRemove(const RsMusicChatContext &ctx, const QString &args)
{
	if (!isControlAllowed(ctx)) {
		emit feedbackMessage(QString("%1: you don't have permission to remove requests.").arg(ctx.displayName));
		return;
	}
	QString id = args.trimmed();
	if (id.startsWith('#')) id.remove(0, 1);
	if (id.isEmpty()) {
		emit feedbackMessage("Usage: !remove #<request ID>");
		return;
	}
	m_controller->actionRemoveRequest(id);
}
