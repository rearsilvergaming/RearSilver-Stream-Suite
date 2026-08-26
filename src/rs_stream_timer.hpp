#pragma once

#include <QJsonObject>
#include <QObject>

class QTimer;

class RsStreamTimer : public QObject {
public:
	static RsStreamTimer &instance();
	void configure(const QJsonObject &configuration);
	QJsonObject setup();
	QJsonObject status() const;
	QJsonObject testSound();
	QJsonObject startTimer();
	QJsonObject pauseResume();
	QJsonObject reset();
	QJsonObject setVisible(bool visible);
	void shutdown();

private:
	explicit RsStreamTimer(QObject *parent = nullptr);
	void tick();
	void publishOverlayState();
	QJsonObject combinedStatus(const QJsonObject &sourceStatus) const;

	QTimer *m_tick = nullptr;
	QTimer *m_hideTimer = nullptr;
	QJsonObject m_configuration;
	int m_displaySeconds = 300;
	int m_soundRevision = 0;
	bool m_running = false;
	bool m_paused = false;
};
