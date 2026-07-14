#pragma once

#include <QWidget>

class RsMainDock;

class RsTimer {
public:
	static QWidget *createPage(RsMainDock *dock, QWidget *parent);
};
