#include "rs_stream_overlay_server.hpp"

#include <QFile>
#include <QHostAddress>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>
#include <obs-module.h>

RsStreamOverlayServer &RsStreamOverlayServer::instance()
{
	static RsStreamOverlayServer server;
	return server;
}

RsStreamOverlayServer::RsStreamOverlayServer(QObject *parent) : QObject(parent)
{
	m_quickTextState = {{"text", ""}, {"size", 120}, {"colour", "#ffffff"},
		{"font", "Sora"}, {"fontWeight", 700}};
}

bool RsStreamOverlayServer::start()
{
	if (m_server && m_server->isListening()) return true;
	if (!m_server) {
		m_server = new QTcpServer(this);
		connect(m_server, &QTcpServer::newConnection, this, [this] { acceptConnections(); });
	}
	for (quint16 candidate = 18255; candidate < 18265; ++candidate) {
		if (m_server->listen(QHostAddress::LocalHost, candidate)) {
			blog(LOG_INFO, "[RearSilver Stream Suite] Stream overlays listening on 127.0.0.1:%u", unsigned(candidate));
			return true;
		}
	}
	blog(LOG_ERROR, "[RearSilver Stream Suite] Could not bind a stream-overlay loopback port.");
	return false;
}

void RsStreamOverlayServer::stop()
{
	if (m_server) m_server->close();
}

quint16 RsStreamOverlayServer::port() const
{
	return m_server && m_server->isListening() ? m_server->serverPort() : 0;
}

QString RsStreamOverlayServer::quickTextUrl() const
{
	return port() ? QString("http://127.0.0.1:%1/overlays/quick-text").arg(port()) : QString();
}

void RsStreamOverlayServer::setQuickTextState(const QJsonObject &state)
{
	m_quickTextState = state;
}

QJsonObject RsStreamOverlayServer::quickTextState() const
{
	return m_quickTextState;
}

void RsStreamOverlayServer::acceptConnections()
{
	while (m_server && m_server->hasPendingConnections()) {
		QTcpSocket *socket = m_server->nextPendingConnection();
		connect(socket, &QTcpSocket::readyRead, this, [this, socket] { readRequest(socket); });
		connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
	}
}

QByteArray RsStreamOverlayServer::response(const QByteArray &type, const QByteArray &body, int status) const
{
	const QByteArray label = status == 200 ? "OK" : "Not Found";
	return "HTTP/1.1 " + QByteArray::number(status) + " " + label + "\r\nContent-Type: " + type +
		"\r\nContent-Length: " + QByteArray::number(body.size()) +
		"\r\nCache-Control: no-store\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n" + body;
}

void RsStreamOverlayServer::readRequest(QTcpSocket *socket)
{
	const QByteArray request = socket->readAll();
	if (!request.contains("\r\n\r\n")) return;
	const QByteArray target = request.split(' ').value(1).split('?').value(0);
	QByteArray payload;
	if (target == "/overlays/quick-text") {
		QFile file(":/rs/stream-overlays/quick-text.html");
		if (file.open(QIODevice::ReadOnly)) payload = response("text/html; charset=utf-8", file.readAll());
	} else if (target == "/api/overlays/quick-text") {
		payload = response("application/json", QJsonDocument(m_quickTextState).toJson(QJsonDocument::Compact));
	} else {
		payload = response("text/plain", "Not found", 404);
	}
	if (payload.isEmpty()) payload = response("text/plain", "Not found", 404);
	socket->write(payload);
	socket->disconnectFromHost();
}
