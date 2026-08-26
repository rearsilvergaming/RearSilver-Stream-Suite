#pragma once

#include <QObject>
#include <QJsonObject>

class QTcpServer;
class QTcpSocket;

class RsStreamOverlayServer : public QObject {
public:
	static RsStreamOverlayServer &instance();

	bool start();
	void stop();
	quint16 port() const;
	QString quickTextUrl() const;
	QString timerUrl() const;
	void setQuickTextState(const QJsonObject &state);
	QJsonObject quickTextState() const;
	void setTimerState(const QJsonObject &state);
	QJsonObject timerState() const;

private:
	explicit RsStreamOverlayServer(QObject *parent = nullptr);
	void acceptConnections();
	void readRequest(QTcpSocket *socket);
	QByteArray response(const QByteArray &contentType, const QByteArray &body, int status = 200) const;

	QTcpServer *m_server = nullptr;
	QJsonObject m_quickTextState;
	QJsonObject m_timerState;
};
