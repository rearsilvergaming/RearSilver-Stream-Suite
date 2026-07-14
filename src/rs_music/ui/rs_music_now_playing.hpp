#pragma once

#include <QWidget>

class QLabel;
class QPushButton;

class RsMusicState;

class RsMusicNowPlaying : public QWidget {
	Q_OBJECT

public:
	explicit RsMusicNowPlaying(RsMusicState *state, QWidget *parent = nullptr);

private slots:
	void updateFromState();

private:
	RsMusicState *m_state = nullptr;

	QLabel *m_lblTitle = nullptr;
	QLabel *m_lblArtist = nullptr;
	QLabel *m_lblRequester = nullptr;
	QLabel *m_lblStatus = nullptr;

	QPushButton *m_btnPlay = nullptr;
	QPushButton *m_btnPause = nullptr;
	QPushButton *m_btnSkip = nullptr;
	QPushButton *m_btnRestart = nullptr;
	QPushButton *m_btnStop = nullptr;
};
