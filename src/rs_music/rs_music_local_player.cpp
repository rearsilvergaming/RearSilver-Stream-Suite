#include "rs_music_local_player.hpp"
#include "rs_music_metadata.hpp"
#include "state/rs_music_track.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QLocalSocket>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>

extern "C" {
#include <obs.h>
#include <obs-module.h>
}

RsMusicLocalPlayer &RsMusicLocalPlayer::instance()
{
	static RsMusicLocalPlayer player;
	return player;
}

RsMusicLocalPlayer::RsMusicLocalPlayer()
{
	if (obs_source_t *legacySource = obs_get_source_by_name("Music - Local Files")) {
		blog(LOG_INFO, "[RS Music] Removing obsolete OBS local-file playback source.");
		obs_source_remove(legacySource);
		obs_source_release(legacySource);
	}
	m_process = new QProcess(this);
	m_socket = new QLocalSocket(this);
	connect(m_socket, &QLocalSocket::readyRead, this, &RsMusicLocalPlayer::readMessages);
	connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
		if (!m_shuttingDown)
			emit playbackError("The bundled local music player could not be started.");
	});
}

RsMusicLocalPlayer::~RsMusicLocalPlayer()
{
	shutdown();
}

QString RsMusicLocalPlayer::companionPath() const
{
#ifdef Q_OS_WIN
	const QString executable = "RearSilver-Music-Player.exe";
#else
	const QString executable = "RearSilver-Music-Player";
#endif
	const QString applicationDir = QCoreApplication::applicationDirPath();
	const QStringList candidates = {
		QDir(applicationDir).filePath(executable),
		QDir(applicationDir).filePath(QString("../../obs-plugins/64bit/%1").arg(executable)),
		QDir(applicationDir).filePath(QString("../../obs-plugins/64bit/RearSilver-Stream-Suite/%1").arg(executable))};
	for (const QString &candidate : candidates) {
		if (QFileInfo::exists(candidate))
			return QFileInfo(candidate).absoluteFilePath();
	}
	return {};
}

bool RsMusicLocalPlayer::ensureCompanion()
{
	if (m_socket->state() == QLocalSocket::ConnectedState)
		return true;
	m_socket->abort();
	m_socket->connectToServer("RearSilverStreamSuiteMusicPlayer");
	if (m_socket->waitForConnected(250)) {
		blog(LOG_INFO, "[RS Music] Connected to an existing companion player.");
		return true;
	}

	const QString path = companionPath();
	if (path.isEmpty()) {
		blog(LOG_ERROR, "[RS Music] Companion player executable was not found.");
		emit playbackError("The bundled local music player is missing. Repair or reinstall the Suite.");
		return false;
	}
	if (m_process->state() == QProcess::NotRunning) {
		blog(LOG_INFO, "[RS Music] Starting companion player: %s", path.toUtf8().constData());
		m_process->setProgram(path);
		m_process->setWorkingDirectory(QFileInfo(path).absolutePath());
		m_process->start();
		if (!m_process->waitForStarted(3000)) {
			blog(LOG_ERROR, "[RS Music] Companion failed to start: %s", m_process->errorString().toUtf8().constData());
			return false;
		}
	}
	// Give fast launches a short synchronous window, then let playFile queue the first track asynchronously.
	for (int attempt = 0; attempt < 10; ++attempt) {
		m_socket->abort();
		m_socket->connectToServer("RearSilverStreamSuiteMusicPlayer");
		if (m_socket->waitForConnected(150)) {
			blog(LOG_INFO, "[RS Music] Connected to the companion player.");
			return true;
		}
	}
	blog(LOG_WARNING, "[RS Music] Companion is running but its control channel is not ready yet: %s",
	     m_socket->errorString().toUtf8().constData());
	return false;
}

void RsMusicLocalPlayer::sendCommand(const QString &command, const QString &argument)
{
	if (!ensureCompanion())
		return;
	QByteArray message = command.toUtf8();
	if (!argument.isEmpty()) {
		message.append('\t');
		message.append(argument.toUtf8());
	}
	message.append('\n');
	m_socket->write(message);
	m_socket->flush();
}

static QString protocolField(QString value)
{
	return value.replace('\t', ' ').replace('\r', ' ').replace('\n', ' ');
}

bool RsMusicLocalPlayer::playFile(const RsMusicTrack &track)
{
	const QFileInfo file(track.providerTrackId);
	if (!file.exists() || !file.isFile()) {
		emit playbackError("The selected local music file no longer exists.");
		return false;
	}
	m_currentFile = file.absoluteFilePath();
	QString artworkPath;
	QByteArray artwork = RsMusicMetadata::artworkBytes(track.artworkUri);
	if (artwork.isEmpty())
		artwork = RsMusicMetadata::artworkBytes(":/rs/music/music-fallback-vinyl.png");
	if (!artwork.isEmpty()) {
		const QString cacheFolder = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
		QDir().mkpath(cacheFolder);
		const QString artworkId = QString::fromLatin1(QCryptographicHash::hash(artwork, QCryptographicHash::Sha256).toHex());
		artworkPath = QDir(cacheFolder).filePath(QString("music-player-artwork-%1.img").arg(artworkId));
		if (QFileInfo::exists(artworkPath)) {
			// GDI+ keeps the selected image file open; immutable per-image files avoid replacement races.
		} else {
		QSaveFile output(artworkPath);
		if (output.open(QIODevice::WriteOnly)) { output.write(artwork); if (!output.commit()) artworkPath.clear(); }
		else artworkPath.clear();
		}
	}
	const QString metadata = QString("%1\t%2\t%3\t%4").arg(protocolField(track.title), protocolField(track.artist),
		protocolField(track.album), protocolField(artworkPath));
	if (!ensureCompanion()) {
		if (m_process->state() == QProcess::NotRunning)
			return false;
		m_pendingMetadata = metadata;
		m_pendingFile = m_currentFile;
		m_pendingConnectionAttempts = 0;
		QTimer::singleShot(100, this, &RsMusicLocalPlayer::connectPendingPlayback);
		return true;
	}
	m_pendingMetadata.clear();
	m_pendingFile.clear();
	sendCommand("META", metadata);
	sendCommand("LOAD", m_currentFile);
	return true;
}

void RsMusicLocalPlayer::connectPendingPlayback()
{
	if (m_shuttingDown || m_pendingFile.isEmpty())
		return;
	if (m_socket->state() != QLocalSocket::ConnectedState) {
		m_socket->abort();
		m_socket->connectToServer("RearSilverStreamSuiteMusicPlayer");
		if (!m_socket->waitForConnected(200)) {
			if (++m_pendingConnectionAttempts < 40) {
				QTimer::singleShot(250, this, &RsMusicLocalPlayer::connectPendingPlayback);
				return;
			}
			blog(LOG_ERROR, "[RS Music] Companion control channel did not become ready after startup.");
			m_pendingMetadata.clear();
			m_pendingFile.clear();
			emit playbackError("The Suite could not connect to its bundled local music player.");
			return;
		}
	}
	QByteArray commands = "META\t" + m_pendingMetadata.toUtf8() + "\nLOAD\t" + m_pendingFile.toUtf8() + "\n";
	m_socket->write(commands);
	m_socket->flush();
	m_pendingMetadata.clear();
	m_pendingFile.clear();
	blog(LOG_INFO, "[RS Music] Delivered the pending first track after companion startup.");
}

void RsMusicLocalPlayer::pause() { sendCommand("PAUSE"); }
void RsMusicLocalPlayer::resume() { sendCommand("PLAY"); }
void RsMusicLocalPlayer::stop() { sendCommand("STOP"); emit playbackStopped(); }
void RsMusicLocalPlayer::restart() { sendCommand("RESTART"); }
void RsMusicLocalPlayer::seekTo(qint64 positionMs) { sendCommand("SEEK", QString::number(qMax<qint64>(0, positionMs))); }

void RsMusicLocalPlayer::readMessages()
{
	while (m_socket->canReadLine()) {
		const QString line = QString::fromUtf8(m_socket->readLine()).trimmed();
		const QStringList parts = line.split('\t');
		if (parts.value(0) == "STATUS") {
			emit playbackProgress(parts.value(2).toLongLong(), parts.value(3).toLongLong());
			if (parts.value(1) == "playing") emit playbackStarted();
		} else if (parts.value(0) == "EVENT" && parts.value(1) == "ended") {
			if (parts.mid(2).join('\t').compare(m_currentFile, Qt::CaseInsensitive) == 0)
				emit playbackEnded();
		} else if (parts.value(0) == "ERROR") {
			emit playbackError(parts.mid(1).join('\t'));
		}
	}
}

void RsMusicLocalPlayer::shutdown()
{
	if (m_shuttingDown)
		return;
	m_shuttingDown = true;
	if (m_socket && m_socket->state() == QLocalSocket::ConnectedState) {
		m_socket->write("SHUTDOWN\n");
		m_socket->flush();
		m_socket->waitForBytesWritten(500);
	}
	if (m_process && m_process->state() != QProcess::NotRunning && !m_process->waitForFinished(2000)) {
		m_process->kill();
		m_process->waitForFinished(1000);
	}
	m_currentFile.clear();
}

QString RsMusicLocalPlayer::currentFile() const { return m_currentFile; }
