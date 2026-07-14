#include "rs_music_twitch_irc_sender.hpp"

#include <QSslError>

extern "C" {
#include <obs-module.h>
}

static const char *IRC_HOST = "irc.chat.twitch.tv";
static const quint16 IRC_TLS_PORT = 6697;

RsMusicTwitchIrcSender::RsMusicTwitchIrcSender(QObject *parent) : QObject(parent)
{
	connect(&m_socket, &QSslSocket::encrypted, this, &RsMusicTwitchIrcSender::onSocketEncrypted);
	connect(&m_socket, &QSslSocket::readyRead, this, &RsMusicTwitchIrcSender::onSocketReadyRead);
	connect(&m_socket, &QSslSocket::disconnected, this, &RsMusicTwitchIrcSender::onSocketDisconnected);

	connect(&m_socket, &QSslSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
		blog(LOG_ERROR, "[RS Music] IRC sender socket error: %s", m_socket.errorString().toUtf8().constData());
		m_joined = false;
		emit connectionStateChanged(false);
	});

	connect(&m_socket, &QSslSocket::sslErrors, this, [this](const QList<QSslError> &errors) {
		for (const QSslError &error : errors)
			blog(LOG_ERROR, "[RS Music] IRC sender TLS error: %s", error.errorString().toUtf8().constData());

		// Never send OAuth credentials when the server certificate cannot be verified.
		m_socket.abort();
		m_joined = false;
		emit connectionStateChanged(false);
	});
}

void RsMusicTwitchIrcSender::connectSender(const QString &loginName, const QString &oauthToken,
					   const QString &channelName)
{
	m_loginName = loginName.trimmed().toLower();
	m_oauthToken = oauthToken.trimmed();
	m_channelName = channelName.trimmed().toLower();
	if (m_channelName.startsWith('#'))
		m_channelName.remove(0, 1);

	if (m_loginName.isEmpty() || m_oauthToken.isEmpty() || m_channelName.isEmpty()) {
		blog(LOG_ERROR, "[RS Music] IRC sender refused because login, token, or channel is missing");
		emit connectionStateChanged(false);
		return;
	}

	m_joined = false;
	m_rxBuffer.clear();
	if (m_socket.state() != QAbstractSocket::UnconnectedState)
		m_socket.abort();

	m_socket.connectToHostEncrypted(IRC_HOST, IRC_TLS_PORT);
}

void RsMusicTwitchIrcSender::disconnect()
{
	m_pendingMessages.clear();
	m_joined = false;
	m_socket.disconnectFromHost();
}

void RsMusicTwitchIrcSender::sendMessage(const QString &text)
{
	const QString message = sanitiseMessage(text);
	if (message.isEmpty())
		return;

	if (!m_joined || m_socket.state() != QAbstractSocket::ConnectedState) {
		// Bound the queue so a prolonged outage cannot grow memory indefinitely.
		if (m_pendingMessages.size() >= 20)
			m_pendingMessages.removeFirst();
		m_pendingMessages.append(message);
		return;
	}

	sendRaw(QString("PRIVMSG #%1 :%2").arg(m_channelName, message));
}

void RsMusicTwitchIrcSender::onSocketEncrypted()
{
	sendRaw(QString("PASS oauth:%1").arg(m_oauthToken));
	sendRaw(QString("NICK %1").arg(m_loginName));
	sendRaw(QString("JOIN #%1").arg(m_channelName));
}

void RsMusicTwitchIrcSender::onSocketReadyRead()
{
	m_rxBuffer += m_socket.readAll();
	while (true) {
		const int eol = m_rxBuffer.indexOf("\r\n");
		if (eol < 0)
			break;

		const QString line = QString::fromUtf8(m_rxBuffer.left(eol));
		m_rxBuffer.remove(0, eol + 2);

		if (line.startsWith("PING")) {
			sendRaw("PONG :tmi.twitch.tv");
			continue;
		}

		if (line.contains(" NOTICE * :Login authentication failed") ||
		    line.contains(" NOTICE * :Improperly formatted auth")) {
			blog(LOG_ERROR, "[RS Music] IRC sender authentication failed");
			m_pendingMessages.clear();
			m_socket.abort();
			continue;
		}

		// 366 is Twitch's end-of-names response and confirms that JOIN completed.
		if (line.contains(" 366 ") && line.contains(QString(" #%1 ").arg(m_channelName))) {
			m_joined = true;
			emit connectionStateChanged(true);
			flushPendingMessages();
		}
	}
}

void RsMusicTwitchIrcSender::onSocketDisconnected()
{
	m_joined = false;
	emit connectionStateChanged(false);
}

void RsMusicTwitchIrcSender::sendRaw(const QString &line)
{
	if (m_socket.state() != QAbstractSocket::ConnectedState)
		return;
	m_socket.write((line + "\r\n").toUtf8());
}

void RsMusicTwitchIrcSender::flushPendingMessages()
{
	const QStringList pending = m_pendingMessages;
	m_pendingMessages.clear();
	for (const QString &message : pending)
		sendMessage(message);
}

QString RsMusicTwitchIrcSender::sanitiseMessage(const QString &text) const
{
	QString message = text;
	message.replace('\r', ' ');
	message.replace('\n', ' ');
	message = message.simplified();

	// Twitch IRC lines are limited to 512 bytes including command and CRLF.
	const int overhead = QString("PRIVMSG #%1 :\r\n").arg(m_channelName).toUtf8().size();
	const int maxPayloadBytes = qMax(0, 512 - overhead);
	while (!message.isEmpty() && message.toUtf8().size() > maxPayloadBytes)
		message.chop(1);

	return message.trimmed();
}
