// src/enhancements/rs_auto_start.hpp
#pragma once

class QWidget;
class RsMainDock;

class RsAutoStart {
public:
	static QWidget *createPage(RsMainDock *dock, QWidget *parent);
	static void ensureObsEventHook();
	static void shutdown();
};

