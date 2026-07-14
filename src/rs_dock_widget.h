#pragma once
#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>

class RSDockWidget : public QWidget {
	Q_OBJECT

public:
	explicit RSDockWidget(QWidget *parent = nullptr) : QWidget(parent)
	{
		auto *layout = new QVBoxLayout(this);

		auto *label = new QLabel("RearSilver Stream Suite Dock");
		label->setStyleSheet("color: white; font-size: 16px;");

		layout->addWidget(label);
		setLayout(layout);
	}
};
