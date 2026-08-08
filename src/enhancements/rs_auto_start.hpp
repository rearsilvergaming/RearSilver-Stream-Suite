// src/enhancements/rs_auto_start.hpp
#pragma once
#include <QString>
#include <QStringList>

class QWidget;
class RsMainDock;

class RsAutoStart {
public:
	static QWidget *createPage(RsMainDock *dock, QWidget *parent);
	static void ensureObsEventHook();
	static void shutdown();
	static bool containsProgram(const QString &path);
	static bool autoLaunchEnabled();
	static bool autoCloseEnabled();
	static QStringList programs();
	static void setAutoLaunchEnabled(bool enabled);
	static void setAutoCloseEnabled(bool enabled);
	static void launchPrograms();
	static void closePrograms();
	static void launchProgram(const QString &path);
	static void closeProgram(const QString &path);
	static void addProgram(const QString &path);
	static void removeProgram(const QString &path);
};

