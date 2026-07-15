#pragma once
#include <QObject>
#include <QPointer>

class QTcpServer;
class QTcpSocket;
class RsMusicState;

class RsMusicServer : public QObject {
	Q_OBJECT
public:
	static RsMusicServer &instance();
	bool start(RsMusicState *state);
	void stop();
	quint16 port() const;
	QString overlayUrl(const QString &preset = "main") const;
private:
	explicit RsMusicServer(QObject *parent = nullptr);
	void acceptConnections();
	void readRequest(QTcpSocket *socket);
	QByteArray stateJson() const;
	QByteArray configJson() const;
	QByteArray response(const QByteArray &contentType, const QByteArray &body, int status = 200) const;
	QTcpServer *m_server = nullptr;
	QPointer<RsMusicState> m_state;
};
