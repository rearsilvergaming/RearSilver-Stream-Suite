#include "rs_main_dock.hpp"

#include <obs-frontend-api.h>

#include <QPushButton>
#include <QStyle>
#include <QTimer>
#include <functional>

// --------------------------------------------------
// Original button labels (cached once)
// --------------------------------------------------

static QString s_startStreamLabel;
static QString s_startRecordLabel;
static QString s_startReplayLabel;
static QString s_startVCamLabel;


// --------------------------------------------------
// Long-press helpers (ACTIVE in Model B)
// --------------------------------------------------

static constexpr int kStopHoldDurationMs = 1500;


void RsMainDock::beginStopHold(QPushButton *btn, std::function<void()> action)
{
	if (!btn)
		return;

	// Safety Lock OFF → execute immediately
	if (!m_safetyLockEnabled) {
		action();
		return;
	}

	// Lazily create timer (RESTORED)
	if (!m_stopHoldTimer) {
		m_stopHoldTimer = new QTimer(this);
		m_stopHoldTimer->setSingleShot(true);
		m_stopHoldTimer->setInterval(kStopHoldDurationMs);
	}

	// Clear previous connections
	m_stopHoldTimer->disconnect();

	m_pendingStopButton = btn;

// Create overlay once
	if (!m_holdOverlay) {
		m_holdOverlay = new QWidget(btn);
		m_holdOverlay->setObjectName("HoldOverlay");
		m_holdOverlay->setAttribute(Qt::WA_TransparentForMouseEvents);
		m_holdOverlay->setGeometry(0, 0, 0, btn->height());
		m_holdOverlay->show();
	}

	// Reset overlay
	m_holdOverlay->setGeometry(0, 0, 0, btn->height());
	m_holdOverlay->raise();

	// Animate overlay width
	if (!m_holdAnim)
		m_holdAnim = new QPropertyAnimation(m_holdOverlay, "geometry", this);

	m_holdAnim->stop();
	m_holdAnim->setDuration(kStopHoldDurationMs);
	m_holdAnim->setStartValue(QRect(0, 0, 0, btn->height()));
	m_holdAnim->setEndValue(QRect(0, 0, btn->width(), btn->height()));
	m_holdAnim->start();


	btn->style()->unpolish(btn);
	btn->style()->polish(btn);
	btn->update();


connect(m_stopHoldTimer, &QTimer::timeout, this, [this, btn, action]() {
		if (m_pendingStopButton != btn)
			return;

		if (m_holdAnim)
			m_holdAnim->stop();

		// 🔴 Completion pulse ON
		btn->setProperty("pulse", true);
		btn->setProperty("holding", false);
		btn->style()->unpolish(btn);
		btn->style()->polish(btn);
		btn->update();

		// 🔴 Turn pulse OFF shortly after
		QTimer::singleShot(120, btn, [btn]() {
			btn->setProperty("pulse", false);
			btn->style()->unpolish(btn);
			btn->style()->polish(btn);
			btn->update();
		});

		action();
		m_pendingStopButton = nullptr;
	});


	m_stopHoldTimer->start();
}

void RsMainDock::cancelStopHold()
{
	if (m_stopHoldTimer)
		m_stopHoldTimer->stop();

	if (m_holdAnim)
		m_holdAnim->stop();

	if (m_holdOverlay)
		m_holdOverlay->setGeometry(0, 0, 0, m_holdOverlay->height());

if (m_pendingStopButton) {
		m_pendingStopButton->setProperty("holding", false);
		m_pendingStopButton->setProperty("pulse", false);
		m_pendingStopButton->style()->unpolish(m_pendingStopButton);
		m_pendingStopButton->style()->polish(m_pendingStopButton);
		m_pendingStopButton->update();
	}


	m_pendingStopButton = nullptr;
}


// --------------------------------------------------
// Helper: lock icon in button text
// --------------------------------------------------

static void applySafetyLockLabel(QPushButton *btn, bool locked)
{
	if (!btn)
		return;

	const QString lockIcon = QStringLiteral(" 🔒");
	QString text = btn->text();

	if (locked) {
		if (!text.endsWith(lockIcon))
			btn->setText(text + lockIcon);
	} else {
		if (text.endsWith(lockIcon))
			btn->setText(text.left(text.length() - lockIcon.length()));
	}
}

// --------------------------------------------------
// OBS Controls
// --------------------------------------------------

void RsMainDock::startStreaming()
{
	if (!obs_frontend_streaming_active())
		obs_frontend_streaming_start();

	updateControlStates();
}

void RsMainDock::stopStreaming()
{
	beginStopHold(m_btnStopStream, [this]() {
		if (obs_frontend_streaming_active())
			obs_frontend_streaming_stop();
		updateControlStates();
	});
}

void RsMainDock::startRecording()
{
	if (!obs_frontend_recording_active())
		obs_frontend_recording_start();

	updateControlStates();
}

void RsMainDock::stopRecording()
{
	beginStopHold(m_btnStopRecord, [this]() {
		if (obs_frontend_recording_active())
			obs_frontend_recording_stop();
		updateControlStates();
	});
}

void RsMainDock::startReplayBuffer()
{
	if (!obs_frontend_replay_buffer_active())
		obs_frontend_replay_buffer_start();

	updateControlStates();
}

void RsMainDock::stopReplayBuffer()
{
	beginStopHold(m_btnStopReplay, [this]() {
		if (obs_frontend_replay_buffer_active())
			obs_frontend_replay_buffer_stop();
		updateControlStates();
	});
}

void RsMainDock::startVirtualCamera()
{
	if (!obs_frontend_virtualcam_active())
		obs_frontend_start_virtualcam();

	updateControlStates();
}

void RsMainDock::stopVirtualCamera()
{
	beginStopHold(m_btnStopVCam, [this]() {
		if (obs_frontend_virtualcam_active())
			obs_frontend_stop_virtualcam();
		updateControlStates();
	});
}

void RsMainDock::toggleStudioMode()
{
	bool active = obs_frontend_preview_program_mode_active();
	obs_frontend_set_preview_program_mode(!active);

	updateControlStates();
}

// --------------------------------------------------
// State Sync + Safety Lock UI
// --------------------------------------------------

void RsMainDock::updateControlStates()
{

	// Cache original labels once
	if (s_startStreamLabel.isEmpty() && m_btnStartStream) {
		s_startStreamLabel = m_btnStartStream->text();
		s_startRecordLabel = m_btnStartRecord->text();
		s_startReplayLabel = m_btnStartReplay->text();
		s_startVCamLabel = m_btnStartVCam->text();
	}

	// STREAMING
	bool streaming = obs_frontend_streaming_active();
	m_btnStartStream->setEnabled(!streaming);
	m_btnStopStream->setEnabled(streaming);
	m_btnStartStream->setProperty("active", streaming);
	m_btnStopStream->setProperty("active", false);

	m_btnStartStream->setText(streaming ? tr("Streaming") : s_startStreamLabel);


	// RECORDING
	bool recording = obs_frontend_recording_active();
	m_btnStartRecord->setEnabled(!recording);
	m_btnStopRecord->setEnabled(recording);
	m_btnStartRecord->setProperty("active", recording);
	m_btnStopRecord->setProperty("active", false);

	m_btnStartRecord->setText(recording ? tr("Recording") : s_startRecordLabel);


	// REPLAY BUFFER
	bool replay = obs_frontend_replay_buffer_active();
	m_btnStartReplay->setEnabled(!replay);
	m_btnStopReplay->setEnabled(replay);
	m_btnStartReplay->setProperty("active", replay);
	m_btnStopReplay->setProperty("active", false);

	m_btnStartReplay->setText(replay ? tr("Replay Buffer Running") : s_startReplayLabel);


	// VIRTUAL CAMERA
	bool vcam = obs_frontend_virtualcam_active();
	m_btnStartVCam->setEnabled(!vcam);
	m_btnStopVCam->setEnabled(vcam);
	m_btnStartVCam->setProperty("active", vcam);
	m_btnStopVCam->setProperty("active", false);

	m_btnStartVCam->setText(vcam ? tr("Virtual Camera On") : s_startVCamLabel);


	// STUDIO MODE
	bool studio = obs_frontend_preview_program_mode_active();
	m_btnStudioMode->setProperty("active", studio);

	// SAFETY LOCK VISUALS
	applySafetyLockLabel(m_btnStopStream, m_safetyLockEnabled);
	applySafetyLockLabel(m_btnStopRecord, m_safetyLockEnabled);
	applySafetyLockLabel(m_btnStopReplay, m_safetyLockEnabled);
	applySafetyLockLabel(m_btnStopVCam, m_safetyLockEnabled);

	auto applyLockProp = [&](QPushButton *btn) {
		if (btn)
			btn->setProperty("safetyLocked", m_safetyLockEnabled);
	};

	applyLockProp(m_btnStopStream);
	applyLockProp(m_btnStopRecord);
	applyLockProp(m_btnStopReplay);
	applyLockProp(m_btnStopVCam);

	// TOOLTIP
	const QString lockTip = "Safety Lock is enabled.\n"
				"Hold to confirm stop action.\n"
				"Disable it in UI Settings to stop instantly.";

	if (m_btnStopStream)
		m_btnStopStream->setToolTip(m_safetyLockEnabled ? lockTip : "Stop streaming");
	if (m_btnStopRecord)
		m_btnStopRecord->setToolTip(m_safetyLockEnabled ? lockTip : "Stop recording");
	if (m_btnStopReplay)
		m_btnStopReplay->setToolTip(m_safetyLockEnabled ? lockTip : "Stop replay buffer");
	if (m_btnStopVCam)
		m_btnStopVCam->setToolTip(m_safetyLockEnabled ? lockTip : "Stop virtual camera");

	// FORCE STYLE REFRESH
	auto refresh = [&](QPushButton *btn) {
		if (!btn)
			return;
		btn->style()->unpolish(btn);
		btn->style()->polish(btn);
		btn->update();
	};

	refresh(m_btnStartStream);
	refresh(m_btnStopStream);
	refresh(m_btnStartRecord);
	refresh(m_btnStopRecord);
	refresh(m_btnStartReplay);
	refresh(m_btnStopReplay);
	refresh(m_btnStartVCam);
	refresh(m_btnStopVCam);
	refresh(m_btnStudioMode);
}
