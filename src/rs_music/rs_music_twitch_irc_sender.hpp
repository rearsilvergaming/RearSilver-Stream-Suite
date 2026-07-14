#pragma once

#include <QObject>
#include <QString>
#include <QSslSocket>
#include <QByteArray>
#include <QStringList>

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

	// Send a chat message. Messages are queued until Twitch confirms the connection.
	void sendMessage(const QString &text);

signals:
	void connectionStateChanged(bool connected);

private:
	void onSocketEncrypted();
	void onSocketReadyRead();
	void onSocketDisconnected();
	void sendRaw(const QString &line);
	void flushPendingMessages();
	QString sanitiseMessage(const QString &text) const;

	QSslSocket m_socket;
	QByteArray m_rxBuffer;
	QString m_loginName;
	QString m_oauthToken;
	QString m_channelName;
	QStringList m_pendingMessages;
	bool m_joined = false;
};
