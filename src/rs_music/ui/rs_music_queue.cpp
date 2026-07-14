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

	auto *hint = new QLabel("Upcoming tracks appear here in playback order.");
	hint->setWordWrap(true);
	hint->setStyleSheet("opacity: 0.75;");
	layout->addWidget(hint);

	m_list = new QListWidget();
	m_list->setAlternatingRowColors(true);
	m_list->setSelectionMode(QAbstractItemView::NoSelection);
	layout->addWidget(m_list);

	auto *divider = new QFrame();
	divider->setObjectName("rs-divider");
	layout->addWidget(divider);

	m_status = new QLabel("Status: —");
	m_status->setStyleSheet("opacity: 0.7; font-size: 11px;");
	layout->addWidget(m_status);

	layout->addStretch(1);

	if (m_state)
		connect(m_state, &RsMusicState::stateChanged, this, [this]() { updateFromState(); });

	updateFromState();
}

void RsMusicQueue::updateFromState()
{
	if (!m_status || !m_list)
		return;

	m_list->clear();

	if (!m_state) {
		m_list->addItem("Music state is unavailable.");
		m_list->setEnabled(false);
		m_status->setText("Queue unavailable");
		return;
	}

	const QVector<RsMusicTrack> &queue = m_state->queue();
	if (queue.isEmpty()) {
		m_list->addItem("The queue is empty.");
		m_list->setEnabled(false);
		m_status->setText(m_state->requestsEnabled() ? "Requests enabled · 0 tracks"
							      : "Requests disabled · 0 tracks");
		return;
	}

	m_list->setEnabled(true);
	for (int index = 0; index < queue.size(); ++index) {
		const RsMusicTrack &track = queue.at(index);
		const int minutes = track.durationSeconds / 60;
		const int seconds = track.durationSeconds % 60;
		const QString duration = track.durationSeconds > 0
						 ? QString("%1:%2").arg(minutes).arg(seconds, 2, 10, QLatin1Char('0'))
						 : QString("duration unknown");
		const QString artist = track.artist.trimmed().isEmpty() ? QString() : QString(" — %1").arg(track.artist);
		const QString source = track.isFromPlaylist
					       ? QString("Playlist")
					       : (track.requestedBy.trimmed().isEmpty()
							  ? QString("Request")
							  : QString("Requested by %1").arg(track.requestedBy));

		m_list->addItem(QString("%1. %2%3  ·  %4  ·  %5")
					    .arg(index + 1)
					    .arg(track.title.trimmed().isEmpty() ? QString("Untitled track") : track.title)
					    .arg(artist)
					    .arg(duration)
					    .arg(source));
	}

	m_status->setText(QString("%1 track%2 queued · Requests %3")
				  .arg(queue.size())
				  .arg(queue.size() == 1 ? "" : "s")
				  .arg(m_state->requestsEnabled() ? "enabled" : "disabled"));
}
