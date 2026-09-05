#pragma once

#include <QDir>
#include <QSettings>
#include <QString>

namespace RsInstallPaths {
inline QString controlHubDirectory()
{
#ifdef Q_OS_WIN
	QSettings registration(
		QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\RearSilver Stream Suite"),
		QSettings::Registry64Format);
	const QString installedRoot = registration.value(QStringLiteral("InstallLocation")).toString().trimmed();
	if (!installedRoot.isEmpty() && QDir::isAbsolutePath(installedRoot))
		return QDir(installedRoot).filePath(QStringLiteral("Control Hub"));
	const QString programFiles = qEnvironmentVariable("ProgramFiles");
	if (!programFiles.isEmpty())
		return QDir(programFiles).filePath(QStringLiteral("RearSilver Stream Suite/Control Hub"));
#endif
	return {};
}
} // namespace RsInstallPaths
