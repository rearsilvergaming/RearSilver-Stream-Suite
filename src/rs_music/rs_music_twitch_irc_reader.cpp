#include "rs_music_twitch_irc_reader.hpp"

#include <QStringList>

extern "C" {
#include <obs-module.h>
}

static const char *IRC_HOST = "irc.chat.twitch.tv";
static const quint16 IRC_PORT = 6667; // Plain IRC (no TLS). TLS comes later once Qt TLS backend is stable.

RsMusicTwitchIrcReader::RsMusicTwitchIrcReader(QObject *parent) : QObject(parent)
{
	connect(&m_socket, &QTcpSocket::connected, this, &RsMusicTwitchIrcReader::onSocketConnected);
	connect(&m_socket, &QTcpSocket::readyRead, this, &RsMusicTwitchIrcReader::onSocketReadyRead);
	connect(&m_socket, &QTcpSocket::disconnected, this, &RsMusicTwitchIrcReader::onSocketDisconnected);

	connect(&m_socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
		blog(LOG_ERROR, "[RS Music] IRC socket error: %s", m_socket.errorString().toUtf8().constData());
		emit connectionStateChanged(false);
	});
}

void RsMusicTwitchIrcReader::connectToChat(const QString &channelName, const QString &oauthToken,
					   const QString &loginName)
{
	m_channel = channelName.toLower();
	m_oauthToken = oauthToken;
	m_loginName = loginName;

	m_rxBuffer.clear();

	if (m_socket.state() != QAbstractSocket::UnconnectedState)
		m_socket.abort();

	m_socket.connectToHost(IRC_HOST, IRC_PORT);
}

void RsMusicTwitchIrcReader::disconnect()
{
	m_socket.disconnectFromHost();
}

void RsMusicTwitchIrcReader::onSocketConnected()
{
	emit connectionStateChanged(true);

	sendRaw("CAP REQ :twitch.tv/tags twitch.tv/commands");
	sendRaw(QString("PASS oauth:%1").arg(m_oauthToken));
	sendRaw(QString("NICK %1").arg(m_loginName));
	sendRaw(QString("JOIN #%1").arg(m_channel));
}

void RsMusicTwitchIrcReader::onSocketDisconnected()
{
	emit connectionStateChanged(false);
}

void RsMusicTwitchIrcReader::sendRaw(const QString &line)
{
	const QByteArray bytes = (line + "\r\n").toUtf8();
	m_socket.write(bytes);
}

static QString tagValue(const QString &tags, const QString &key)
{
	const QString needle = key + "=";
	int idx = tags.indexOf(needle);
	if (idx < 0)
		return "";

	int start = idx + needle.length();
	int end = tags.indexOf(';', start);
	if (end < 0)
		end = tags.length();

	return tags.mid(start, end - start);
}

void RsMusicTwitchIrcReader::handleLine(const QString &line)
{
	if (line.startsWith("PING")) {
		sendRaw("PONG :tmi.twitch.tv");
		return;
	}

	if (!line.contains("PRIVMSG"))
		return;

	int tagEnd = line.indexOf(' ');
	if (tagEnd < 0)
		return;

	const QString tags = line.left(tagEnd);

	const QString displayName = tagValue(tags, "display-name");
	const QString userId = tagValue(tags, "user-id");

	const bool isMod = (tagValue(tags, "mod") == "1");
	const bool isBroadcaster = tagValue(tags, "badges").contains("broadcaster");

	const int msgIdx = line.indexOf(" :");
	if (msgIdx < 0)
		return;

	const QString msgText = line.mid(msgIdx + 2);

	RsMusicChatMessage msg;
	msg.userId = userId;
	msg.displayName = displayName;
	msg.message = msgText;
	msg.isMod = isMod;
	msg.isBroadcaster = isBroadcaster;

	emit chatMessageReceived(msg);
}

void RsMusicTwitchIrcReader::onSocketReadyRead()
{
	m_rxBuffer += m_socket.readAll();

	while (true) {
		int eol = m_rxBuffer.indexOf("\r\n");
		if (eol < 0)
			break;

		const QByteArray lineBytes = m_rxBuffer.left(eol);
		m_rxBuffer.remove(0, eol + 2);

		const QString line = QString::fromUtf8(lineBytes);
		handleLine(line);
	}
}
