#pragma once

#include <QObject>
#include <QString>

struct obs_source;
typedef struct obs_source obs_source_t;
struct calldata;
typedef struct calldata calldata_t;

class RsMusicLocalPlayer : public QObject {
	Q_OBJECT

public:
	static RsMusicLocalPlayer &instance();

	bool playFile(const QString &filePath);
	void pause();
	void resume();
	void stop();
	void restart();
	void shutdown();
	QString currentFile() const;

signals:
	void playbackStarted();
	void playbackEnded();
	void playbackStopped();
	void playbackError(const QString &message);

private:
	RsMusicLocalPlayer() = default;
	~RsMusicLocalPlayer() override;
	RsMusicLocalPlayer(const RsMusicLocalPlayer &) = delete;
	RsMusicLocalPlayer &operator=(const RsMusicLocalPlayer &) = delete;

	bool ensureSource();
	void releaseSource();
	static void onMediaStarted(void *data, calldata_t *);
	static void onMediaEnded(void *data, calldata_t *);
	static void onMediaStopped(void *data, calldata_t *);

	obs_source_t *m_source = nullptr;
	QString m_currentFile;
	bool m_forcedActive = false;
};
