#include "rs_music_queue.hpp"
#include "rs_music/state/rs_music_state.hpp"

#include <QVBoxLayout>
#include <QLabel>
#include <QFont>
#include <QListWidget>
#include <QFrame>

static QLabel *makeTitle(const QString &text)
{
	auto *lbl = new QLabel(text);
	QFont f = lbl->font();
	f.setBold(true);
	f.setPointSize(f.pointSize() + 2);
	lbl->setFont(f);
	return lbl;
}

RsMusicQueue::RsMusicQueue(RsMusicState *state, QWidget *parent) : QWidget(parent), m_state(state)
{
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(8, 8, 8, 8);
	layout->setSpacing(10);

	layout->addWidget(makeTitle("Music — Queue"));

	auto *hint = new QLabel("Skeleton queue view.\n\n"
				"Later this will list upcoming request tracks (and allow removal/clear).");
	hint->setWordWrap(true);
	hint->setStyleSheet("opacity: 0.75;");
	layout->addWidget(hint);

	auto *list = new QListWidget();
	list->addItem("1) (placeholder) Song Title — Requested by User");
	list->addItem("2) (placeholder) Song Title — Requested by User");
	list->addItem("3) (placeholder) Song Title — Requested by User");
	list->setEnabled(false);
	layout->addWidget(list);

	auto *divider = new QFrame();
	divider->setObjectName("rs-divider");
	layout->addWidget(divider);

	m_status = new QLabel("Status: —");
	m_status->setStyleSheet("opacity: 0.7; font-size: 11px;");
	layout->addWidget(m_status);

	layout->addStretch(1);

	updateFromState();
}

void RsMusicQueue::updateFromState()
{
	if (!m_status)
		return;

	if (!m_state) {
		m_status->setText("Status: No state");
		return;
	}

	m_status->setText("Status: Connected");
}
