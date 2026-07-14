#pragma once

#include <QWidget>

class QLabel;
class RsMusicState;

class RsMusicPlaylist : public QWidget {
public:
	explicit RsMusicPlaylist(RsMusicState *state, QWidget *parent = nullptr);

private:
	void updateFromState();

	RsMusicState *m_state = nullptr; // non-owning
	QLabel *m_status = nullptr;
};
