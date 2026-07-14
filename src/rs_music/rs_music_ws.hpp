#pragma once

#include <QObject>
#include <QString>
#include <QJsonObject>

/*
 * RsMusicWsClient (STUB)
 *
 * Cloudflare hosted-player websocket bridge is intentionally disabled
 * during recovery so the OBS plugin can load in a clean install without
 * Qt6WebSockets.dll.
 *
 * No fake behaviour: these methods are no-ops and do not connect.
 */

class RsMusicWsClient : public QObject {
	Q_OBJECT

public:
	static RsMusicWsClient &instance();

	void configure(const QString &wsUrl, const QString &channelNameLower, const QString &dockToken);

	void connectIfNeeded();
	void disconnectNow();

	void sendStateFull(const QJsonObject &payloadState);

	void sendCmdPlay(const QString &trackId, const QString &youtubeId, int startSec, int volume);

	void sendCmdPause();
	void sendCmdResume();
	void sendCmdStop();
	void sendCmdRestart(const QString &trackId);
	void sendCmdSkip(const QString &trackId, const QString &reason);
	void sendCmdVolume(int volume);

signals:
	void evtReady();
	void evtStarted(const QString &trackId, const QString &youtubeId);
	void evtPaused();
	void evtStopped();
	void evtEnded(const QString &trackId, const QString &youtubeId);
	void evtSkipped(const QString &trackId);
	void evtError(const QString &trackId, const QString &youtubeId, const QString &code, const QString &message);

	void connected();
	void disconnected();
	void errorText(const QString &msg);

private:
	explicit RsMusicWsClient(QObject *parent = nullptr);

private:
	QString m_wsUrl;
	QString m_room;
	QString m_dockToken;
};
