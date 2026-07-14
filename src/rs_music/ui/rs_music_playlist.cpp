#include "rs_music_playlist.hpp"
#include "rs_music/state/rs_music_state.hpp"

#include <QVBoxLayout>
#include <QLabel>
#include <QFont>
#include <QLineEdit>
#include <QFormLayout>
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

RsMusicPlaylist::RsMusicPlaylist(RsMusicState *state, QWidget *parent) : QWidget(parent), m_state(state)
{
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(8, 8, 8, 8);
	layout->setSpacing(10);

	layout->addWidget(makeTitle("Music — Playlist"));

	auto *hint =
		new QLabel("Skeleton fallback playlist panel.\n\n"
			   "Later this will accept a YouTube playlist URL and show current playlist position.\n"
			   "Requests always take priority; when requests run out, playlist resumes where it left off.");
	hint->setWordWrap(true);
	hint->setStyleSheet("opacity: 0.75;");
	layout->addWidget(hint);

	auto *form = new QFormLayout();

	auto *url = new QLineEdit();
	url->setPlaceholderText("https://www.youtube.com/playlist?list=...");
	url->setEnabled(false);
	form->addRow("Fallback playlist URL:", url);

	layout->addLayout(form);

	auto *divider = new QFrame();
	divider->setObjectName("rs-divider");
	layout->addWidget(divider);

	m_status = new QLabel("Status: —");
	m_status->setStyleSheet("opacity: 0.7; font-size: 11px;");
	layout->addWidget(m_status);

	layout->addStretch(1);

	// 🔌 State binding
	if (m_state) {
		connect(m_state, &RsMusicState::stateChanged, this, &RsMusicPlaylist::updateFromState);
	}

	updateFromState();
}

void RsMusicPlaylist::updateFromState()
{
	if (!m_status)
		return;

	if (!m_state) {
		m_status->setText("Status: No state");
		return;
	}

	m_status->setText(QString("Status: Connected — Label: %1").arg(m_state->playlistLabel()));
}
