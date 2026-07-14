#pragma once

#include <QObject>
#include <QString>
#include <QTcpSocket>
#include <QByteArray>

/*
 * RsMusicTwitchIrcReader
 *
 * Streamer-only Twitch IRC reader.
 * - Reads chat
 * - Emits parsed messages
 * - NEVER sends messages
 */

struct RsMusicChatMessage {
	QString userId;
	QString displayName;
	QString message;
	bool isMod = false;
	bool isBroadcaster = false;
};

class RsMusicTwitchIrcReader : public QObject {
	Q_OBJECT

public:
	explicit RsMusicTwitchIrcReader(QObject *parent = nullptr);

	void connectToChat(const QString &channelName, const QString &oauthToken, const QString &loginName);

	void disconnect();

signals:
	void chatMessageReceived(const RsMusicChatMessage &msg);
	void connectionStateChanged(bool connected);

private:
	void handleLine(const QString &line);

private:
	QTcpSocket m_socket;
	QByteArray m_rxBuffer;
	QString m_channel;
	QString m_oauthToken;
	QString m_loginName;

	void sendRaw(const QString &line);
	void onSocketConnected();
	void onSocketReadyRead();
	void onSocketDisconnected();
};
