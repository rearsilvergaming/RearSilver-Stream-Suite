// src/enhancements/rs_browser_refresh.hpp
#pragma once

class QWidget;
class RsMainDock;

class RsBrowserRefresh {
public:
	// Build the Browser Refresh panel for the Enhancements tab
	static QWidget *createPage(RsMainDock *dock, QWidget *parent);
};
