#pragma once

#include <QWidget>

class QLabel;
class QCheckBox;
class RsMusicState;

class RsMusicRequests : public QWidget {
public:
	explicit RsMusicRequests(RsMusicState *state, QWidget *parent = nullptr);

private:
	void updateFromState();

	RsMusicState *m_state = nullptr; // non-owning
	QLabel *m_status = nullptr;
	QCheckBox *m_toggle = nullptr;
};
