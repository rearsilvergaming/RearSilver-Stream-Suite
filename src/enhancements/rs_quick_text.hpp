#pragma once
#include <QWidget>

class RsMainDock;

class RsQuickText : public QWidget {
	Q_OBJECT

public:
	static QWidget *createPage(RsMainDock *dock, QWidget *parent = nullptr);
};
