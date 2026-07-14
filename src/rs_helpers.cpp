#include "rs_main_dock.hpp"

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFont>
#include <QSizePolicy>

QWidget *RsMainDock::makeTextCard(const QString &title, const QString &body)
{
	QWidget *card = new QWidget(m_contentCard);
	card->setObjectName("rs-card");

	auto *layout = new QVBoxLayout(card);
	layout->setContentsMargins(8, 8, 8, 8);
	layout->setSpacing(4);

	if (!title.isEmpty()) {
		auto *titleLbl = new QLabel(title, card);
		QFont f = titleLbl->font();
		f.setPointSize(f.pointSize() + 1);
		f.setBold(true);
		titleLbl->setFont(f);
		layout->addWidget(titleLbl);
	}

	if (!body.isEmpty()) {
		auto *bodyLbl = new QLabel(body, card);
		bodyLbl->setWordWrap(true);
		layout->addWidget(bodyLbl);
	}

	layout->addStretch();
	return card;
}

QPushButton *RsMainDock::makeObsButton(const QString &text)
{
	QPushButton *btn = new QPushButton(text);

	btn->setObjectName("ControlButton"); // REQUIRED so theme CSS matches
	btn->setMinimumHeight(32);
	btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

	// IMPORTANT:
	// REMOVE inline styles so theme stylesheet can take over.
	btn->setStyleSheet("");

	return btn;
}


QHBoxLayout *RsMainDock::makeRow(QWidget *left, QWidget *right)
{
	QHBoxLayout *hl = new QHBoxLayout();
	hl->setSpacing(6);
	hl->addWidget(left);
	if (right)
		hl->addWidget(right);
	hl->addStretch();
	return hl;
}