#include "rs_stream_timer.hpp"

#include "rs_stream_overlay_manager.hpp"
#include "rs_stream_overlay_server.hpp"

#include <QFileInfo>
#include <QTimer>

RsStreamTimer &RsStreamTimer::instance()
{
	static RsStreamTimer timer;
	return timer;
}

RsStreamTimer::RsStreamTimer(QObject *parent) : QObject(parent), m_tick(new QTimer(this)), m_hideTimer(new QTimer(this))
{
	m_configuration = {{"label", "Timer"}, {"mode", "countdown"}, {"seconds", 300},
		{"textColour", "#ffffff"}, {"font", "Segoe UI"}, {"labelWeight", 600},
		{"timeWeight", 800}, {"labelSize", 28}, {"timeSize", 84}, {"shadow", true},
		{"background", false}, {"backgroundColour", "#000000"},
		{"backgroundOpacity", 70}, {"backgroundRadius", 48}, {"hideWhenFinished", false},
		{"lingerSeconds", 3}, {"soundPath", ""}};
	m_tick->setInterval(1000);
	m_hideTimer->setSingleShot(true);
	connect(m_tick, &QTimer::timeout, this, [this] { tick(); });
	connect(m_hideTimer, &QTimer::timeout, this, [] {
		RsStreamOverlayManager::setTimerVisibleInCurrentScene(false);
	});
	publishOverlayState();
}

void RsStreamTimer::configure(const QJsonObject &configuration)
{
	for (auto it = configuration.begin(); it != configuration.end(); ++it) m_configuration[it.key()] = it.value();
	if (!m_configuration.value("hideWhenFinished").toBool()) m_hideTimer->stop();
	if (!m_running) m_displaySeconds = m_configuration.value("mode").toString("countdown") == "stopwatch"
		? 0 : qMax(0, m_configuration.value("seconds").toInt(300));
	publishOverlayState();
}

QJsonObject RsStreamTimer::setup()
{
	return combinedStatus(RsStreamOverlayManager::setupTimerInCurrentScene());
}

QJsonObject RsStreamTimer::status() const
{
	return combinedStatus(RsStreamOverlayManager::timerStatus());
}

QJsonObject RsStreamTimer::testSound()
{
	const QString path = m_configuration.value("soundPath").toString();
	QJsonObject result = status();
	const bool available = !path.isEmpty() && QFileInfo(path).isFile();
	result["soundTestAccepted"] = available;
	if (available) {
		++m_soundRevision;
		publishOverlayState();
	}
	return result;
}

QJsonObject RsStreamTimer::startTimer()
{
	const QJsonObject source = RsStreamOverlayManager::timerStatus();
	if (!source.value("setupComplete").toBool() || source.value("conflict").toBool()) return combinedStatus(source);
	m_displaySeconds = m_configuration.value("mode").toString("countdown") == "stopwatch"
		? 0 : qMax(0, m_configuration.value("seconds").toInt(300));
	m_running = true;
	m_paused = false;
	m_hideTimer->stop();
	m_tick->start();
	publishOverlayState();
	return combinedStatus(RsStreamOverlayManager::setTimerVisibleInCurrentScene(true));
}

QJsonObject RsStreamTimer::pauseResume()
{
	if (m_running) {
		m_paused = !m_paused;
		m_paused ? m_tick->stop() : m_tick->start();
	}
	publishOverlayState();
	return status();
}

QJsonObject RsStreamTimer::reset()
{
	m_tick->stop();
	m_hideTimer->stop();
	m_running = false;
	m_paused = false;
	m_displaySeconds = m_configuration.value("mode").toString("countdown") == "stopwatch"
		? 0 : qMax(0, m_configuration.value("seconds").toInt(300));
	publishOverlayState();
	return status();
}

QJsonObject RsStreamTimer::setVisible(bool visible)
{
	m_hideTimer->stop();
	return combinedStatus(RsStreamOverlayManager::setTimerVisibleInCurrentScene(visible));
}

void RsStreamTimer::shutdown()
{
	m_tick->stop();
	m_hideTimer->stop();
}

void RsStreamTimer::tick()
{
	if (!m_running || m_paused) return;
	if (m_configuration.value("mode").toString("countdown") == "stopwatch") {
		++m_displaySeconds;
	} else if (m_displaySeconds > 0) {
		--m_displaySeconds;
		if (m_displaySeconds == 0) {
			m_running = false;
			m_tick->stop();
			if (!m_configuration.value("soundPath").toString().isEmpty()) ++m_soundRevision;
			if (m_configuration.value("hideWhenFinished").toBool()) {
				const int lingerMs = qBound(0, m_configuration.value("lingerSeconds").toInt(3), 10) * 1000;
				if (lingerMs == 0) RsStreamOverlayManager::setTimerVisibleInCurrentScene(false);
				else m_hideTimer->start(lingerMs);
			}
		}
	}
	publishOverlayState();
}

void RsStreamTimer::publishOverlayState()
{
	QJsonObject state = m_configuration;
	state["displaySeconds"] = m_displaySeconds;
	state["running"] = m_running;
	state["paused"] = m_paused;
	state["soundRevision"] = m_soundRevision;
	RsStreamOverlayServer::instance().setTimerState(state);
}

QJsonObject RsStreamTimer::combinedStatus(const QJsonObject &sourceStatus) const
{
	QJsonObject result = sourceStatus;
	result["running"] = m_running;
	result["paused"] = m_paused;
	result["displaySeconds"] = m_displaySeconds;
	return result;
}
