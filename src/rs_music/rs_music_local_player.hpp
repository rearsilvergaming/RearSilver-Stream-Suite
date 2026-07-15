#pragma once

#include <QObject>
#include <QString>

class QLocalSocket;
class QProcess;
struct RsMusicTrack;

class RsMusicLocalPlayer : public QObject {
	Q_OBJECT

public:
	static RsMusicLocalPlayer &instance();
	bool playFile(const RsMusicTrack &track);
	void pause();
	void resume();
	void stop();
	void restart();
	void seekTo(qint64 positionMs);
	void shutdown();
	QString currentFile() const;
	QString executablePath() const { return companionPath(); }

signals:
	void playbackStarted();
	void playbackEnded();
	void playbackStopped();
	void playbackError(const QString &message);
	void playbackProgress(qint64 positionMs, qint64 durationMs);

private:
	RsMusicLocalPlayer();
	~RsMusicLocalPlayer() override;
	RsMusicLocalPlayer(const RsMusicLocalPlayer &) = delete;
	RsMusicLocalPlayer &operator=(const RsMusicLocalPlayer &) = delete;
	bool ensureCompanion();
	QString companionPath() const;
	void sendCommand(const QString &command, const QString &argument = {});
	void readMessages();
	void connectPendingPlayback();

	QProcess *m_process = nullptr;
	QLocalSocket *m_socket = nullptr;
	QString m_currentFile;
	QString m_pendingMetadata;
	QString m_pendingFile;
	int m_pendingConnectionAttempts = 0;
	bool m_shuttingDown = false;
};
