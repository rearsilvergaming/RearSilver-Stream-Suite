#pragma once

#include <QWidget>

class QLabel;
class QListWidget;
class QPushButton;
class RsMusicController;
class RsMusicState;

class RsMusicPlaylist : public QWidget {
public:
	explicit RsMusicPlaylist(RsMusicState *state, RsMusicController *controller, QWidget *parent = nullptr);

private:
	void updateFromState();
	void addLocalFiles();
	void addLocalFolder();
	void playSelected();
	void removeSelected();
	void clearLibrary();
	void loadLibrary();
	void saveLibrary() const;

	RsMusicState *m_state = nullptr; // non-owning
	RsMusicController *m_controller = nullptr; // non-owning
	QListWidget *m_localFiles = nullptr;
	QPushButton *m_playSelected = nullptr;
	QPushButton *m_removeSelected = nullptr;
	QPushButton *m_clearLibrary = nullptr;
	QPushButton *m_shuffleButton = nullptr;
	QLabel *m_status = nullptr;
};
