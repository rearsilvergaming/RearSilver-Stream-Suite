#pragma once

#include <QWidget>

class RsMainDock;

class RsTimer {
public:
	static QWidget *createPage(RsMainDock *dock, QWidget *parent);
	static void configure(const QString &label, const QString &mode, int seconds);
	static void configureStyle(const QString &textColour, int labelSize, int timeSize,
		bool shadow, bool background, const QString &backgroundColour,
		int backgroundOpacity, int backgroundRadius, bool hideWhenFinished);
	static void start();
	static void pauseResume();
	static void reset();
	static void setVisible(bool visible);
};
