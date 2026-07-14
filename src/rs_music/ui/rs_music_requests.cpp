#include "rs_music_requests.hpp"
#include "rs_music/state/rs_music_state.hpp"

#include <QVBoxLayout>
#include <QLabel>
#include <QFont>
#include <QCheckBox>
#include <QSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QSignalBlocker>

static QLabel *makeTitle(const QString &text)
{
	auto *lbl = new QLabel(text);
	QFont f = lbl->font();
	f.setBold(true);
	f.setPointSize(f.pointSize() + 2);
	lbl->setFont(f);
	return lbl;
}

RsMusicRequests::RsMusicRequests(RsMusicState *state, QWidget *parent) : QWidget(parent), m_state(state)
{
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(8, 8, 8, 8);
	layout->setSpacing(10);

	layout->addWidget(makeTitle("Music — Requests"));

	auto *hint = new QLabel("Skeleton request rules panel.\n\n"
				"Later this will control:\n"
				"• Requests enabled\n"
				"• Per-user limit\n"
				"• Max queued requests\n"
				"• Max track length\n"
				"• Category checks\n");
	hint->setWordWrap(true);
	hint->setStyleSheet("opacity: 0.75;");
	layout->addWidget(hint);

	auto *form = new QFormLayout();
	form->setLabelAlignment(Qt::AlignLeft);

	auto *toggle = new QCheckBox("Enable chat requests (!sr)");
	m_toggle = toggle;
	toggle->setEnabled(true);
	form->addRow("", toggle);

	auto *perUser = new QSpinBox();
	perUser->setRange(1, 20);
	perUser->setEnabled(false);
	form->addRow("Per-user queued limit:", perUser);

	auto *maxQueue = new QSpinBox();
	maxQueue->setRange(0, 200);
	maxQueue->setEnabled(false);
	form->addRow("Max total requests queued:", maxQueue);

	auto *maxMinutes = new QSpinBox();
	maxMinutes->setRange(1, 60);
	maxMinutes->setEnabled(false);
	form->addRow("Max track length (minutes):", maxMinutes);

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
		connect(m_state, &RsMusicState::stateChanged, this, &RsMusicRequests::updateFromState);
	}

	// 🔁 UI → State
	if (m_state && m_toggle) {
		connect(m_toggle, &QCheckBox::toggled, this,
			[this](bool checked) { m_state->setRequestsEnabled(checked); });
	}

	updateFromState();
}

void RsMusicRequests::updateFromState()
{
	if (!m_state || !m_status)
		return;

	const bool enabled = m_state->requestsEnabled();

	m_status->setText(QString("Status: %1").arg(enabled ? "Enabled" : "Disabled"));

	if (m_toggle) {
		QSignalBlocker blocker(m_toggle);
		m_toggle->setChecked(enabled);
	}
}
