#include "rs_music_requests.hpp"
#include "rs_music/state/rs_music_state.hpp"
#include "rs_music/rs_music.hpp"

#include <QVBoxLayout>
#include <QLabel>
#include <QFont>
#include <QCheckBox>
#include <QSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QSignalBlocker>
#include <QSettings>

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

	auto *hint = new QLabel("Control who can add songs and how many requests may wait in the queue. "
				"A value of 0 means unlimited.");
	hint->setWordWrap(true);
	hint->setStyleSheet("opacity: 0.75;");
	layout->addWidget(hint);

	auto *form = new QFormLayout();
	form->setLabelAlignment(Qt::AlignLeft);

	auto *toggle = new QCheckBox("Enable chat requests (!sr)");
	m_toggle = toggle;
	toggle->setEnabled(true);
	form->addRow("", toggle);

	m_perUser = new QSpinBox();
	m_perUser->setRange(0, 20);
	m_perUser->setSpecialValueText("Unlimited");
	form->addRow("Per-user queued limit:", m_perUser);

	m_maxQueue = new QSpinBox();
	m_maxQueue->setRange(0, 200);
	m_maxQueue->setSpecialValueText("Unlimited");
	form->addRow("Max total requests queued:", m_maxQueue);

	m_maxMinutes = new QSpinBox();
	m_maxMinutes->setRange(0, 60);
	m_maxMinutes->setSpecialValueText("Unlimited");
	m_maxMinutes->setToolTip("Applied when track metadata is available.");
	form->addRow("Max track length (minutes):", m_maxMinutes);

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

	if (m_state && m_toggle) {
		rsMusicSetRequestsEnabled(m_state->requestsEnabled());
		connect(m_toggle, &QCheckBox::toggled, this, [this](bool checked) {
			m_state->setRequestsEnabled(checked);
			rsMusicSetRequestsEnabled(checked);
		});
	}

	QSettings settings("RearSilver", "RearSilver-Stream-Suite");
	m_perUser->setValue(settings.value("music/maxPerUser", rsMusicMaxPerUser()).toInt());
	m_maxQueue->setValue(settings.value("music/maxQueueTotal", rsMusicMaxQueueTotal()).toInt());
	m_maxMinutes->setValue(settings.value("music/maxTrackLengthMinutes", rsMusicMaxTrackLengthSec() / 60).toInt());
	rsMusicSetMaxPerUser(m_perUser->value());
	rsMusicSetMaxQueueTotal(m_maxQueue->value());
	rsMusicSetMaxTrackLengthSec(m_maxMinutes->value() * 60);

	connect(m_perUser, &QSpinBox::valueChanged, this, [this](int value) {
		QSettings("RearSilver", "RearSilver-Stream-Suite").setValue("music/maxPerUser", value);
		rsMusicSetMaxPerUser(value);
		updateFromState();
	});
	connect(m_maxQueue, &QSpinBox::valueChanged, this, [this](int value) {
		QSettings("RearSilver", "RearSilver-Stream-Suite").setValue("music/maxQueueTotal", value);
		rsMusicSetMaxQueueTotal(value);
		updateFromState();
	});
	connect(m_maxMinutes, &QSpinBox::valueChanged, this, [](int value) {
		QSettings("RearSilver", "RearSilver-Stream-Suite").setValue("music/maxTrackLengthMinutes", value);
		rsMusicSetMaxTrackLengthSec(value * 60);
	});

	updateFromState();
}

void RsMusicRequests::updateFromState()
{
	if (!m_state || !m_status)
		return;

	const bool enabled = m_state->requestsEnabled();

	m_status->setText(QString("Requests %1 · %2 queued maximum · %3 per user")
				  .arg(enabled ? "enabled" : "disabled")
				  .arg(m_maxQueue && m_maxQueue->value() > 0 ? QString::number(m_maxQueue->value())
										 : QString("unlimited"))
				  .arg(m_perUser && m_perUser->value() > 0 ? QString::number(m_perUser->value())
									       : QString("unlimited")));

	if (m_toggle) {
		QSignalBlocker blocker(m_toggle);
		m_toggle->setChecked(enabled);
	}
}
