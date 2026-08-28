#pragma once
#include <QWidget>
class QLabel;
class QSpinBox;
class RsMusicOverlay : public QWidget {
	Q_OBJECT
public:
	explicit RsMusicOverlay(QWidget *parent = nullptr);
private:
	QLabel *m_url = nullptr;
	QLabel *m_status = nullptr;
	QSpinBox *m_width = nullptr;
	QSpinBox *m_height = nullptr;
};
