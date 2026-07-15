#pragma once
#include <QWidget>
class QComboBox;
class QLabel;
class QCheckBox;
class RsMusicSetup : public QWidget {
	Q_OBJECT
public:
	explicit RsMusicSetup(QWidget *parent = nullptr);
private:
	void refreshSources();
	void createCaptureSource();
	void openSelectedProperties();
	QComboBox *m_sources = nullptr;
	QLabel *m_status = nullptr;
	QCheckBox *m_autoStart = nullptr;
};
