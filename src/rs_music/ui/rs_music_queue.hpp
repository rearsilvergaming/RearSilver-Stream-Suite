#pragma once

#include <QWidget>

class QLabel;
class RsMusicState;

class RsMusicQueue : public QWidget {
public:
	explicit RsMusicQueue(RsMusicState *state, QWidget *parent = nullptr);

private:
	void updateFromState();

	RsMusicState *m_state = nullptr; // non-owning
	QLabel *m_status = nullptr;
};
