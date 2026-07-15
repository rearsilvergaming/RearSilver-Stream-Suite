#pragma once

#include <QWidget>

class QLabel;
class QPushButton;
class QSlider;

class RsMusicState;
class RsMusicController;

class RsMusicNowPlaying : public QWidget {
	Q_OBJECT

public:
	explicit RsMusicNowPlaying(RsMusicState *state, RsMusicController *controller, QWidget *parent = nullptr);

private slots:
	void updateFromState();

private:
	RsMusicState *m_state = nullptr;
	RsMusicController *m_controller = nullptr;

	QLabel *m_lblTitle = nullptr;
	QLabel *m_lblArtist = nullptr;
	QLabel *m_lblRequester = nullptr;
	QLabel *m_lblStatus = nullptr;
	QLabel *m_lblTime = nullptr;
	QSlider *m_progress = nullptr;
	bool m_userSeeking = false;

	QPushButton *m_btnPlay = nullptr;
	QPushButton *m_btnPrevious = nullptr;
	QPushButton *m_btnPause = nullptr;
	QPushButton *m_btnSkip = nullptr;
	QPushButton *m_btnRestart = nullptr;
	QPushButton *m_btnStop = nullptr;
};
