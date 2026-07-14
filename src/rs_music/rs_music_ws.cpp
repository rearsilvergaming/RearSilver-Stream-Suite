#include "rs_music_ws.hpp"

RsMusicWsClient &RsMusicWsClient::instance()
{
	static RsMusicWsClient s(nullptr);
	return s;
}

RsMusicWsClient::RsMusicWsClient(QObject *parent) : QObject(parent) {}

void RsMusicWsClient::configure(const QString &wsUrl, const QString &channelNameLower, const QString &dockToken)
{
	m_wsUrl = wsUrl;
	m_room = channelNameLower;
	m_dockToken = dockToken;
}

void RsMusicWsClient::connectIfNeeded() {}
void RsMusicWsClient::disconnectNow() {}

void RsMusicWsClient::sendStateFull(const QJsonObject &) {}

void RsMusicWsClient::sendCmdPlay(const QString &, const QString &, int, int) {}

void RsMusicWsClient::sendCmdPause() {}
void RsMusicWsClient::sendCmdResume() {}
void RsMusicWsClient::sendCmdStop() {}
void RsMusicWsClient::sendCmdRestart(const QString &) {}
void RsMusicWsClient::sendCmdSkip(const QString &, const QString &) {}
void RsMusicWsClient::sendCmdVolume(int) {}
