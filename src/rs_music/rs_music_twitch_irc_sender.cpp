#include "rs_music_twitch_irc_sender.hpp"

extern "C" {
#include <obs-module.h>
}

RsMusicTwitchIrcSender::RsMusicTwitchIrcSender(QObject *parent) : QObject(parent) {}

void RsMusicTwitchIrcSender::connectSender(const QString &loginName, const QString &oauthToken,
					   const QString &channelName)
{
	Q_UNUSED(loginName);
	Q_UNUSED(oauthToken);
	Q_UNUSED(channelName);

	// STUB:
	// Real IRC connection will be implemented later.
	// This exists purely so the project builds cleanly.
}

void RsMusicTwitchIrcSender::disconnect()
{
	// STUB
}

void RsMusicTwitchIrcSender::sendMessage(const QString &text)
{
	Q_UNUSED(text);

	// STUB
	// Sending chat messages is not implemented yet.
}
