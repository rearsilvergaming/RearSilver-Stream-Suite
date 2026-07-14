#include "rs_main_dock.hpp"

#include <QSettings>
#include <QResizeEvent>
#include <QString>


void RsMainDock::loadSettings()
{
	QSettings s("RearSilver", "RearSilver-Stream-Suite");
	int mode = s.value("layoutMode", static_cast<int>(LayoutMode::Auto)).toInt();
	m_layoutMode = static_cast<LayoutMode>(mode);
	m_currentTheme = s.value("theme", "default").toString();
	m_safetyLockEnabled = s.value("safetyLockEnabled", false).toBool();
}

void RsMainDock::saveSettings()
{
	QSettings s("RearSilver", "RearSilver-Stream-Suite");
	s.setValue("layoutMode", static_cast<int>(m_layoutMode));
	s.setValue("theme", m_currentTheme);
	s.setValue("safetyLockEnabled", m_safetyLockEnabled);
}
