#pragma once

#include <QObject>
#include <QByteArray>
#include <QString>
#include <QVector>

class QLocalSocket;
class QProcess;
struct RsMusicTrack;

class RsMusicLocalPlayer : public QObject {
	Q_OBJECT

public:
	static RsMusicLocalPlayer &instance();
	bool playFile(const RsMusicTrack &track);
	bool playYouTubeVideo(const RsMusicTrack &track);
	void pause();
	void resume();
	void stop();
	void restart();
	void seekTo(qint64 positionMs);
	void importYouTubePlaylist(const QString &url);
	void requestYouTubeTrack(const QString &requester, const QString &query);
	void skip();
	void previous();
	void shuffleFallback();
	void shutdown();
	QString currentFile() const;
	QString executablePath() const { return companionPath(); }

signals:
	void playbackStarted();
	void playbackEnded();
	void playbackStopped();
	void playbackError(const QString &message);
	void playbackProgress(qint64 positionMs, qint64 durationMs);
	void playbackMetadata(const QString &title, const QString &artist, qint64 durationMs);
	void hubStateReceived(const QByteArray &json);

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
	void connectToHub();

	QProcess *m_process = nullptr;
	QLocalSocket *m_socket = nullptr;
	QString m_currentFile;
	QString m_pendingMetadata;
	QString m_pendingFile;
	QString m_pendingPlaybackCommand;
	int m_pendingConnectionAttempts = 0;
	int m_hubConnectionAttempts = 0;
	bool m_shuttingDown = false;
};
