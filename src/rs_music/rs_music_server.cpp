#include "rs_music_server.hpp"
#include "rs_music_metadata.hpp"
#include "state/rs_music_state.hpp"
#include <QFile>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <obs-module.h>

RsMusicServer &RsMusicServer::instance() { static RsMusicServer server; return server; }
RsMusicServer::RsMusicServer(QObject *parent) : QObject(parent) {}

bool RsMusicServer::start(RsMusicState *state)
{
	m_state = state;
	if (m_server && m_server->isListening()) return true;
	if (!m_server) {
		m_server = new QTcpServer(this);
		connect(m_server, &QTcpServer::newConnection, this, &RsMusicServer::acceptConnections);
	}
	for (quint16 candidate = 18245; candidate < 18255; ++candidate) {
		if (m_server->listen(QHostAddress::LocalHost, candidate)) {
			blog(LOG_INFO, "[RS Music Overlay] Listening on 127.0.0.1:%u", unsigned(candidate));
			return true;
		}
	}
	blog(LOG_ERROR, "[RS Music Overlay] Could not bind a loopback port.");
	return false;
}

void RsMusicServer::stop() { if (m_server) m_server->close(); m_state.clear(); }
quint16 RsMusicServer::port() const { return m_server && m_server->isListening() ? m_server->serverPort() : 0; }
QString RsMusicServer::overlayUrl(const QString &preset) const
{
	return port() ? QString("http://127.0.0.1:%1/music-overlay?preset=%2").arg(port()).arg(QString::fromUtf8(QUrl::toPercentEncoding(preset))) : QString();
}

void RsMusicServer::acceptConnections()
{
	while (m_server && m_server->hasPendingConnections()) {
		QTcpSocket *socket = m_server->nextPendingConnection();
		connect(socket, &QTcpSocket::readyRead, this, [this, socket] { readRequest(socket); });
		connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
	}
}

QByteArray RsMusicServer::response(const QByteArray &type, const QByteArray &body, int status) const
{
	const QByteArray label = status == 200 ? "OK" : "Not Found";
	return "HTTP/1.1 " + QByteArray::number(status) + " " + label + "\r\nContent-Type: " + type +
	       "\r\nContent-Length: " + QByteArray::number(body.size()) +
	       "\r\nCache-Control: no-store\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n" + body;
}

QByteArray RsMusicServer::stateJson() const
{
	QJsonObject root, track;
	QString status = "stopped";
	if (m_state) {
		if (m_state->playbackStatus() == RsMusicState::PlaybackStatus::Playing) status = "playing";
		else if (m_state->playbackStatus() == RsMusicState::PlaybackStatus::Paused) status = "paused";
		if (m_state->hasCurrentTrack()) {
			const RsMusicTrack &current = m_state->currentTrack();
			track["title"] = current.title; track["artist"] = current.artist; track["album"] = current.album;
			track["requestedBy"] = current.requestedBy; track["provider"] = rsMusicProviderKey(current.provider);
			track["durationMs"] = qint64(current.durationSeconds) * 1000;
			track["hasArtwork"] = !current.artworkUri.isEmpty();
		}
		root["positionMs"] = m_state->playbackPositionMs();
	}
	root["status"] = status; root["track"] = track;
	return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QByteArray RsMusicServer::configJson() const
{
	QSettings s("RearSilver", "RearSilver-Stream-Suite"); QJsonObject c;
	auto value=[&](const char *key,const QVariant &fallback){return s.value(QString("music/overlay/main/")+key,fallback);};
	c["showArtwork"]=value("showArtwork",true).toBool(); c["showTitle"]=value("showTitle",true).toBool();
	c["showArtist"]=value("showArtist",true).toBool(); c["showAlbum"]=value("showAlbum",true).toBool();
	c["showRequester"]=value("showRequester",false).toBool(); c["showProgress"]=value("showProgress",true).toBool();
	c["showCustomText"]=value("showCustomText",false).toBool(); c["customText"]=value("customText","").toString();
	c["timingMode"]=value("timingMode","elapsedTotal").toString(); c["artworkPosition"]=value("artworkPosition","left").toString();
	c["backgroundTransparent"]=value("backgroundTransparent",false).toBool(); c["backgroundColour"]=value("backgroundColour","#0c0c12").toString();
	c["backgroundOpacity"]=value("backgroundOpacity",82).toInt(); c["textColour"]=value("textColour","#ffffff").toString();
	c["artworkBackground"]=value("artworkBackground",false).toBool();
	c["accentColour"]=value("accentColour","#9147ff").toString(); c["fontFamily"]=value("fontFamily","Arial").toString();
	c["titleSize"]=value("titleSize",34).toInt(); c["bodySize"]=value("bodySize",20).toInt();
	return QJsonDocument(c).toJson(QJsonDocument::Compact);
}

void RsMusicServer::readRequest(QTcpSocket *socket)
{
	const QByteArray request = socket->readAll();
	if (!request.contains("\r\n\r\n")) return;
	const QByteArray target = request.split(' ').value(1).split('?').value(0);
	QByteArray payload;
	if (target == "/music-overlay" || target == "/") {
		QFile file(":/rs/music/overlay/music-overlay.html");
		if (file.open(QIODevice::ReadOnly)) payload = response("text/html; charset=utf-8", file.readAll());
	} else if (target == "/api/state") {
		payload = response("application/json", stateJson());
	} else if (target == "/api/config") {
		payload = response("application/json", configJson());
	} else if (target == "/artwork") {
		QByteArray artwork;
		if (m_state && m_state->hasCurrentTrack()) artwork = RsMusicMetadata::artworkBytes(m_state->currentTrack().artworkUri);
		if (artwork.isEmpty()) artwork = RsMusicMetadata::artworkBytes(":/rs/music/music-fallback-vinyl.png");
		payload = response("image/png", artwork);
	} else payload = response("text/plain", "Not found", 404);
	if (payload.isEmpty()) payload = response("text/plain", "Not found", 404);
	socket->write(payload); socket->disconnectFromHost();
}
