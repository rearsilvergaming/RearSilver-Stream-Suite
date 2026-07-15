#include "rs_music_now_playing.hpp"
#include "rs_music/state/rs_music_state.hpp"
#include "rs_music/rs_music_controller.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFont>
#include <QPushButton>
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

RsMusicNowPlaying::RsMusicNowPlaying(RsMusicState *state, RsMusicController *controller, QWidget *parent)
	: QWidget(parent),
	  m_state(state),
	  m_controller(controller)
{
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(8, 8, 8, 8);
	layout->setSpacing(10);

	layout->addWidget(makeTitle("Music — Now Playing"));

	m_lblTitle = new QLabel("Title: —");
	m_lblArtist = new QLabel("Artist: —");
	m_lblRequester = new QLabel("Requested by: —");
	for (QLabel *label : {m_lblTitle, m_lblArtist, m_lblRequester}) {
		label->setWordWrap(true);
		label->setMinimumWidth(0);
		label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	}

	layout->addWidget(m_lblTitle);
	layout->addWidget(m_lblArtist);
	layout->addWidget(m_lblRequester);

// Controls grid (dock-consistent layout)
	auto *controlsGrid = new QGridLayout();
	controlsGrid->setHorizontalSpacing(8);
	controlsGrid->setVerticalSpacing(8);

	// Buttons
	m_btnPlay = new QPushButton("Play");
	m_btnPrevious = new QPushButton("Previous");
	m_btnPause = new QPushButton("Pause");
	m_btnSkip = new QPushButton("Skip");
	m_btnRestart = new QPushButton("Restart");
	m_btnStop = new QPushButton("Stop");

	if (m_controller) {
		connect(m_btnPlay, &QPushButton::clicked, m_controller, &RsMusicController::actionPlay);
		connect(m_btnPrevious, &QPushButton::clicked, m_controller, &RsMusicController::actionPrevious);
		connect(m_btnPause, &QPushButton::clicked, m_controller, &RsMusicController::actionPause);
		connect(m_btnStop, &QPushButton::clicked, m_controller, &RsMusicController::actionStop);
		connect(m_btnSkip, &QPushButton::clicked, this, [this]() { m_controller->actionSkip("ui"); });
		connect(m_btnRestart, &QPushButton::clicked, m_controller, &RsMusicController::actionRestart);
	} else {
		for (QPushButton *button : {m_btnPrevious, m_btnPlay, m_btnPause, m_btnSkip, m_btnRestart, m_btnStop})
			button->setEnabled(false);
	}


	// Ensure buttons expand nicely
	for (QPushButton *btn : {m_btnPrevious, m_btnPlay, m_btnPause, m_btnSkip, m_btnRestart, m_btnStop}) {
		btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	}

	// Grid placement (2 rows, balanced)
	controlsGrid->addWidget(m_btnPrevious, 0, 0);
	controlsGrid->addWidget(m_btnPlay, 0, 1);
	controlsGrid->addWidget(m_btnPause, 0, 2);
	controlsGrid->addWidget(m_btnRestart, 1, 0);
	controlsGrid->addWidget(m_btnSkip, 1, 1);
	controlsGrid->addWidget(m_btnStop, 1, 2);

	// Add to main layout
	layout->addLayout(controlsGrid);

	auto *divider = new QFrame();
	divider->setObjectName("rs-divider");
	layout->addSpacing(8);
	layout->addWidget(divider);
	layout->addSpacing(8);

	m_lblStatus = new QLabel("Status: —");
	m_lblStatus->setStyleSheet("opacity: 0.7; font-size: 11px;");
	layout->addWidget(m_lblStatus);

	layout->addStretch(1);

	// 🔌 State binding
	if (m_state)
		connect(m_state, &RsMusicState::stateChanged, this, &RsMusicNowPlaying::updateFromState);

	updateFromState();
}

void RsMusicNowPlaying::updateFromState()
{
	if (!m_state)
		return;

	// --- Track info (may be empty early on) ---
	if (!m_state->hasCurrentTrack()) {
		m_lblTitle->setText("Title: —");
		m_lblArtist->setText("Artist: —");
		m_lblRequester->setText("Requested by: —");
	} else {
		const auto &track = m_state->currentTrack();

		m_lblTitle->setText(QString("Title: %1").arg(track.title));
		m_lblTitle->setToolTip(track.title);
		m_lblArtist->setText(QString("Artist: %1").arg(track.artist));

		if (track.isFromPlaylist) {
			m_lblRequester->setText(QString("Requested by: %1").arg(m_state->playlistLabel()));
		} else {
			m_lblRequester->setText(QString("Requested by: %1").arg(track.requestedBy));
		}
	}

	// --- Playback status (ALWAYS shown) ---
	switch (m_state->playbackStatus()) {
	case RsMusicState::PlaybackStatus::Playing:
		m_lblStatus->setText("Status: Playing");
		break;
	case RsMusicState::PlaybackStatus::Paused:
		m_lblStatus->setText("Status: Paused");
		break;
	case RsMusicState::PlaybackStatus::Stopped:
	default:
		m_lblStatus->setText("Status: Stopped");
		break;
	}
}
