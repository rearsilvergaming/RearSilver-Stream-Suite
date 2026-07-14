#pragma once

#include <QString>

class QWidget;
class RsMainDock;

class RsStats {
public:
	static QString formatPercent(int part, int total);
	static QString formatBytesMB(uint64_t bytes);
	static QString formatMs(double ms);

	static QWidget *createStatsPage(RsMainDock *dock, QWidget *parent);
};
