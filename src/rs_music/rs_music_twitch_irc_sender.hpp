#pragma once

#include <QObject>
#include <QString>

/*
 * RsMusicTwitchIrcSender
 *
 * Send-only Twitch IRC client.
 * - NEVER reads chat
 * - NEVER parses messages
 * - Used only for feedback / confirmations
 *
 * This is intentionally minimal for now.
 */

class RsMusicTwitchIrcSender : public QObject {
	Q_OBJECT

public:
	explicit RsMusicTwitchIrcSender(QObject *parent = nullptr);

	// Connect using a specific identity (streamer or bot)
	void connectSender(const QString &loginName, const QString &oauthToken, const QString &channelName);

	void disconnect();

	// Send a raw chat message
	void sendMessage(const QString &text);
};
