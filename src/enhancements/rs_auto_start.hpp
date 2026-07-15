// src/enhancements/rs_auto_start.hpp
#pragma once
#include <QString>

class QWidget;
class RsMainDock;

class RsAutoStart {
public:
	static QWidget *createPage(RsMainDock *dock, QWidget *parent);
	static void ensureObsEventHook();
	static void shutdown();
	static bool containsProgram(const QString &path);
	static void addProgram(const QString &path);
	static void removeProgram(const QString &path);
};

