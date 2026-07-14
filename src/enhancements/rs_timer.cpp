#include "rs_timer.hpp"
#include "../rs_main_dock.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>
#include <QTimer>
#include <QFile>
#include <QDir>
#include <QDateTime>
#include <QUrl>
#include <QUrlQuery>
#include <QFont>
#include <QGroupBox>
#include <QCheckBox>
#include <QScrollArea>

extern "C" {
#include <obs.h>
#include <obs-frontend-api.h>
#include <obs-module.h>
}
#include <util/platform.h>

// ------------------------------------------------------------
// Constants
// ------------------------------------------------------------
static const char *kTimerSourceName = "RS Timer";

// ------------------------------------------------------------
// Helpers: paths
// ------------------------------------------------------------
static QString timerBaseDir()
{
	// Guaranteed OBS config root:
	// C:\Users\<user>\AppData\Roaming\obs-studio
	char cfg[512] = {};
	if (os_get_config_path(cfg, sizeof(cfg), "obs-studio")) {
		QDir base(QString::fromUtf8(cfg));
		base.mkpath("plugin_config/RearSilver-Stream-Suite/timer_overlay");
		return base.filePath("plugin_config/RearSilver-Stream-Suite/timer_overlay");
	}

	// Absolute last-resort fallback
	QDir d(QDir::currentPath());
	d.mkpath("timer_overlay");
	return d.absoluteFilePath("timer_overlay");
}

static QString overlayHtmlPath()
{
	return timerBaseDir() + "/index.html";
}


// ------------------------------------------------------------
// Helpers: source visibility
// ------------------------------------------------------------
static bool isTimerSourceVisible()
{
	obs_source_t *sceneSrc = obs_frontend_get_current_scene();
	if (!sceneSrc)
		return true; // default to visible if unknown

	obs_scene_t *scene = obs_scene_from_source(sceneSrc);
	if (!scene) {
		obs_source_release(sceneSrc);
		return true;
	}

	obs_sceneitem_t *item = obs_scene_find_source(scene, kTimerSourceName);
	bool visible = true;

	if (item)
		visible = obs_sceneitem_visible(item);

	obs_source_release(sceneSrc);
	return visible;
}


// ------------------------------------------------------------
// State model
// ------------------------------------------------------------
enum class TimerMode { Stopwatch, Countdown };

struct TimerState {
	QString label = "Timer";
	TimerMode mode = TimerMode::Countdown;

	int totalSeconds = 300;
	int remainingSeconds = 300;
	int elapsedSeconds = 0;

	bool running = false;
	bool paused = false;
	bool visibleOnStream = true;

	qint64 lastTickMs = 0;

	// ----------------------------
	// Overlay styling
	// ----------------------------
	QString textColor = "#ffffff";

	int labelFontSize = 28;
	int timeFontSize = 84;

	bool shadowEnabled = true;

	bool bgEnabled = false;
};


static QString modeToString(TimerMode m)
{
	return (m == TimerMode::Stopwatch) ? "stopwatch" : "countdown";
}

static QString fmtTime(int seconds)
{
	if (seconds < 0)
		seconds = 0;

	const int mm = seconds / 60;
	const int ss = seconds % 60;
	return QString("%1:%2").arg(mm, 2, 10, QChar('0')).arg(ss, 2, 10, QChar('0'));
}

// ------------------------------------------------------------
// Overlay deployment
// - Writes a URL-param overlay into Roaming folder
//   (pure plug-and-play; no state.json)
// ------------------------------------------------------------
static void ensureOverlayFilesExist()
{
	const QString dir = timerBaseDir();
	QDir().mkpath(dir);

	const QString htmlPath = overlayHtmlPath();

	const char *html = R"HTML(
<!doctype html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <title>RS Timer</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">

    <style>
        html, body {
            width: 100%;
            height: 100%;
            margin: 0;
            background: transparent;
            font-family: system-ui, -apple-system, Segoe UI, Roboto, Arial, sans-serif;
        }

        .wrap {
            width: 100%;
            height: 100%;
            display: flex;
            flex-direction: column;
            justify-content: center;
            align-items: center;
            text-shadow: 0 2px 10px rgba(0,0,0,0.55);
        }

        .label {
            font-size: 28px;
            font-weight: 600;
            opacity: 0.95;
        }

        .time {
            font-size: 84px;
            font-weight: 800;
            line-height: 1.05;
            letter-spacing: 0.02em;
        }
    </style>
</head>

<body>
    <div class="wrap">
        <div id="label" class="label">Timer</div>
        <div id="time" class="time">00:00</div>
    </div>

    <script>
        const params = new URLSearchParams(window.location.search);

        const label = params.get("label") || "Timer";
        const mode = params.get("mode") || "countdown";
        const secs = parseInt(params.get("seconds") || "0", 10);
        const run = params.get("running") === "1";
        const start = parseInt(params.get("start") || "0", 10);

        const labelEl = document.getElementById("label");
        const timeEl = document.getElementById("time");

        labelEl.textContent = label;

const textColor = params.get("textColor") || "#ffffff";
const labelSize = parseInt(params.get("labelSize") || "28", 10);
const timeSize  = parseInt(params.get("timeSize") || "84", 10);
const shadow    = params.get("shadow") === "1";
const bg        = params.get("bg") === "1";

labelEl.style.color = textColor;
timeEl.style.color = textColor;

labelEl.style.fontSize = labelSize + "px";
timeEl.style.fontSize  = timeSize + "px";

const shadowValue = shadow ? "0 2px 10px rgba(0,0,0,0.55)" : "none";
labelEl.style.textShadow = shadowValue;
timeEl.style.textShadow  = shadowValue;

const wrap = document.querySelector(".wrap");
if (bg) {
	wrap.style.background = "rgba(0,0,0,0.55)";
	wrap.style.padding = "12px";
	wrap.style.borderRadius = "10px";
} else {
	wrap.style.background = "transparent";
	wrap.style.padding = "0";
}


        function fmt(s) {
            s = Math.max(0, s | 0);
            const mm = String(Math.floor(s / 60)).padStart(2, "0");
            const ss = String(s % 60).padStart(2, "0");
            return mm + ":" + ss;
        }

        let baseSeconds = secs;
        let startMs = start || Date.now();

        function update() {
            if (!run) {
                timeEl.textContent = fmt(baseSeconds);
                return;
            }

            const elapsed = Math.floor((Date.now() - startMs) / 1000);

            let shown = 0;
            if (mode === "stopwatch") {
                shown = baseSeconds + elapsed;
            } else {
                shown = baseSeconds - elapsed;
            }

            timeEl.textContent = fmt(shown);
        }

        update();
        setInterval(update, 250);
    </script>
</body>
</html>
)HTML";

	QFile f(htmlPath);
	if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		f.write(html);
		f.close();
		blog(LOG_INFO, "[RS Timer] Overlay HTML written to: %s", htmlPath.toUtf8().constData());
	} else {
		blog(LOG_ERROR, "[RS Timer] Failed to write overlay HTML: %s", htmlPath.toUtf8().constData());
	}
}

// ------------------------------------------------------------
// Browser source URL update (ONLY on transitions)
// ------------------------------------------------------------
static void updateTimerBrowserUrl(const TimerState &s)
{
	obs_source_t *src = obs_get_source_by_name(kTimerSourceName);
	if (!src)
		return;

	obs_data_t *settings = obs_source_get_settings(src);

	const qint64 now = QDateTime::currentMSecsSinceEpoch();

	QUrl url = QUrl::fromLocalFile(overlayHtmlPath());
	QUrlQuery q;

	const int secondsParam = (s.mode == TimerMode::Countdown) ? s.remainingSeconds : s.elapsedSeconds;

	// Overlay "running" is false when paused so the JS freezes.
	const bool overlayRunning = (s.running && !s.paused);

	q.addQueryItem("label", s.label);
	q.addQueryItem("mode", modeToString(s.mode));
	q.addQueryItem("seconds", QString::number(secondsParam));
	q.addQueryItem("running", overlayRunning ? "1" : "0");
	q.addQueryItem("paused", s.paused ? "1" : "0");

	// JS uses start as its baseline for elapsed time.
	// We ONLY bump this on transitions (start/resume/etc).
	q.addQueryItem("start", QString::number(now));

	// cache-buster so OBS actually reloads the URL when it changes
	q.addQueryItem("v", QString::number(now));

	// Styling params
	q.addQueryItem("textColor", s.textColor);
	q.addQueryItem("labelSize", QString::number(s.labelFontSize));
	q.addQueryItem("timeSize", QString::number(s.timeFontSize));
	q.addQueryItem("shadow", s.shadowEnabled ? "1" : "0");
	q.addQueryItem("bg", s.bgEnabled ? "1" : "0");


	url.setQuery(q);

	obs_data_set_bool(settings, "local_file", false);
	obs_data_set_string(settings, "url", url.toString().toUtf8().constData());

	// Reasonable defaults (user can override after)
	obs_data_set_int(settings, "width", 600);
	obs_data_set_int(settings, "height", 200);
	obs_data_set_int(settings, "fps", 30);
	obs_data_set_bool(settings, "shutdown", false);
	obs_data_set_bool(settings, "restart_when_active", false);

	obs_source_update(src, settings);

	obs_data_release(settings);
	obs_source_release(src);
}

// ------------------------------------------------------------
// OBS: ensure browser source exists (URL mode)
// ------------------------------------------------------------

// ------------------------------------------------------------
// OBS: toggle timer source visibility in current scene
// ------------------------------------------------------------
static void setTimerSourceVisible(bool visible)
{
	obs_source_t *sceneSrc = obs_frontend_get_current_scene();
	if (!sceneSrc)
		return;

	obs_scene_t *scene = obs_scene_from_source(sceneSrc);
	if (!scene) {
		obs_source_release(sceneSrc);
		return;
	}

	obs_sceneitem_t *item = obs_scene_find_source(scene, kTimerSourceName);
	if (item) {
		obs_sceneitem_set_visible(item, visible);
	}

	obs_source_release(sceneSrc);
}

static void ensureTimerBrowserSource(const TimerState &initialState)
{
	ensureOverlayFilesExist();

	blog(LOG_INFO, "[RS Timer] Overlay HTML (roaming): %s", overlayHtmlPath().toUtf8().constData());

	obs_source_t *existing = obs_get_source_by_name(kTimerSourceName);
	if (existing) {
		obs_source_release(existing);
		updateTimerBrowserUrl(initialState);
		return;
	}

	obs_data_t *settings = obs_data_create();

	QUrl url = QUrl::fromLocalFile(overlayHtmlPath());
	obs_data_set_bool(settings, "local_file", false);
	obs_data_set_string(settings, "url", url.toString().toUtf8().constData());

	obs_data_set_int(settings, "width", 600);
	obs_data_set_int(settings, "height", 200);
	obs_data_set_int(settings, "fps", 30);
	obs_data_set_bool(settings, "shutdown", false);
	obs_data_set_bool(settings, "restart_when_active", false);

	obs_source_t *src = obs_source_create("browser_source", kTimerSourceName, settings, nullptr);
	obs_data_release(settings);

	if (!src)
		return;

	obs_source_t *sceneSrc = obs_frontend_get_current_scene();
	if (sceneSrc) {
		obs_scene_t *scene = obs_scene_from_source(sceneSrc);
		if (scene) {
			obs_sceneitem_t *item = obs_scene_find_source(scene, kTimerSourceName);
			if (!item) {
				obs_scene_add(scene, src);
			}
		}
		obs_source_release(sceneSrc);
	}

	obs_source_release(src);

	updateTimerBrowserUrl(initialState);
}

// ------------------------------------------------------------
// FIX: Apply any pending whole-seconds before transitions
// (prevents "pause for 60s then resume jumps by 60s")
// ------------------------------------------------------------
static void applyPendingTick(TimerState *s)
{
	if (!s || !s->running || s->paused)
		return;

	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	qint64 deltaMs = now - s->lastTickMs;
	if (deltaMs < 0)
		deltaMs = 0;

	const int deltaSec = (int)(deltaMs / 1000);
	if (deltaSec <= 0)
		return;

	s->lastTickMs += (qint64)deltaSec * 1000;

	if (s->mode == TimerMode::Stopwatch) {
		s->elapsedSeconds += deltaSec;
	} else {
		s->remainingSeconds -= deltaSec;
		if (s->remainingSeconds < 0)
			s->remainingSeconds = 0;
	}
}

// ------------------------------------------------------------
// UI Page
// ------------------------------------------------------------
QWidget *RsTimer::createPage(RsMainDock *dock, QWidget *parent)
{
	// Outer scroll wrapper (matches System pages behaviour)
	auto *scroll = new QScrollArea(parent);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setWidgetResizable(true);

	// Actual page content
	QWidget *page = new QWidget();
	page->setObjectName("rs-card");

	scroll->setWidget(page);

	auto *root = new QVBoxLayout(page);
	root->setContentsMargins(8, 2, 8, 8);
	root->setSpacing(8);

	// Title
	auto *title = new QLabel("Timer / Countdown");
	title->setObjectName("rs-section-label");
	root->addWidget(title);
	title->setMaximumHeight(title->sizeHint().height());


	// State
	auto *state = new TimerState();
	state->label = "Timer";
	state->mode = TimerMode::Countdown;
	state->totalSeconds = 300;
	state->remainingSeconds = 300;
	state->elapsedSeconds = 0;
	state->running = false;
	state->paused = false;
	state->lastTickMs = QDateTime::currentMSecsSinceEpoch();

	// Ensure browser source exists immediately
	ensureTimerBrowserSource(*state);
	updateTimerBrowserUrl(*state);

	// Sync visibility state from OBS (OBS remembers this between launches)
	state->visibleOnStream = isTimerSourceVisible();


	// Label row
	QLineEdit *labelEdit = nullptr;
	{
		auto *row = new QHBoxLayout();
		auto *lbl = new QLabel("Label (shown on stream):");
		labelEdit = new QLineEdit();
		labelEdit->setText(state->label);

		row->addWidget(lbl);
		row->addWidget(labelEdit, 1);
		root->addLayout(row);

		QObject::connect(labelEdit, &QLineEdit::textChanged, page, [=](const QString &t) {
			state->label = t.trimmed().isEmpty() ? "Timer" : t.trimmed();
			updateTimerBrowserUrl(*state);
		});
	}

	// Mode + duration
	QComboBox *modeCombo = nullptr;
	QSpinBox *mins = nullptr;
	QSpinBox *secs = nullptr;

// Mode
	{
		auto *lbl = new QLabel("Mode:");
		modeCombo = new QComboBox();
		modeCombo->addItem("Countdown", (int)TimerMode::Countdown);
		modeCombo->addItem("Stopwatch", (int)TimerMode::Stopwatch);

		root->addWidget(lbl);
		root->addWidget(modeCombo);
	}

	// Duration (Countdown only)
	{
		auto *lbl = new QLabel("Duration (countdown):");
		root->addWidget(lbl);

		auto *row = new QHBoxLayout();
		row->setContentsMargins(0, 0, 0, 0);

		mins = new QSpinBox();
		mins->setRange(0, 999);
		mins->setValue(5);
		mins->setSuffix(" min");

		secs = new QSpinBox();
		secs->setRange(0, 59);
		secs->setValue(0);
		secs->setSuffix(" sec");

		row->addWidget(mins);
		row->addWidget(secs);

		auto *wrap = new QWidget();
		wrap->setLayout(row);

		root->addWidget(wrap);
	}


	// ------------------------------------------------------------
	// Overlay Styling
	// ------------------------------------------------------------
	{
		auto *box = new QGroupBox("Overlay Styling");
		box->setObjectName("rs-card");

		auto *grid = new QGridLayout(box);
		grid->setContentsMargins(10, 10, 10, 10);
		grid->setVerticalSpacing(8);
		grid->setHorizontalSpacing(8);
		int row = 0;

		// Text colour
		grid->addWidget(new QLabel("Text colour (hex):"), row, 0);
		auto *textColour = new QLineEdit(state->textColor);
		textColour->setPlaceholderText("#ffffff");
		grid->addWidget(textColour, row++, 1);

		// Label size
		grid->addWidget(new QLabel("Label size:"), row, 0);
		auto *labelSize = new QSpinBox();
		labelSize->setRange(10, 72);
		labelSize->setValue(state->labelFontSize);
		labelSize->setSuffix(" px");
		grid->addWidget(labelSize, row++, 1);

		// Time size
		grid->addWidget(new QLabel("Time size:"), row, 0);
		auto *timeSize = new QSpinBox();
		timeSize->setRange(32, 200);
		timeSize->setValue(state->timeFontSize);
		timeSize->setSuffix(" px");
		grid->addWidget(timeSize, row++, 1);

		// Shadow toggle
		auto *shadowToggle = new QCheckBox("Text shadow");
		shadowToggle->setChecked(state->shadowEnabled);
		grid->addWidget(shadowToggle, row++, 0, 1, 2);

		// Background toggle
		auto *bgToggle = new QCheckBox("Background pill");
		bgToggle->setChecked(state->bgEnabled);
		grid->addWidget(bgToggle, row++, 0, 1, 2);

		root->addWidget(box);

		// ---- Connections ----
		QObject::connect(textColour, &QLineEdit::editingFinished, page, [=]() {
			QString c = textColour->text().trimmed();

			// Very simple hex sanity check
			if (!c.startsWith("#") || (c.length() != 7 && c.length() != 4)) {
				c = "#ffffff";
				textColour->setText(c);
			}

			state->textColor = c;
			updateTimerBrowserUrl(*state);
		});


		QObject::connect(labelSize, QOverload<int>::of(&QSpinBox::valueChanged), page, [=](int v) {
			state->labelFontSize = v;
			updateTimerBrowserUrl(*state);
		});

		QObject::connect(timeSize, QOverload<int>::of(&QSpinBox::valueChanged), page, [=](int v) {
			state->timeFontSize = v;
			updateTimerBrowserUrl(*state);
		});

		QObject::connect(shadowToggle, &QCheckBox::toggled, page, [=](bool on) {
			state->shadowEnabled = on;
			updateTimerBrowserUrl(*state);
		});

		QObject::connect(bgToggle, &QCheckBox::toggled, page, [=](bool on) {
			state->bgEnabled = on;
			updateTimerBrowserUrl(*state);
		});
	}


	// Live preview
	QLabel *preview = new QLabel();
	preview->setObjectName("rs-card");
	preview->setText(QString("%1\n%2").arg(state->label, fmtTime(state->remainingSeconds)));
	preview->setAlignment(Qt::AlignCenter);
	preview->setMinimumHeight(60);
	preview->setWordWrap(true);
	root->addWidget(preview);

	auto updatePreview = [=]() {
		const int shownSeconds = (state->mode == TimerMode::Countdown) ? state->remainingSeconds
									       : state->elapsedSeconds;
		preview->setText(QString("%1\n%2").arg(state->label, fmtTime(shownSeconds)));
	};

	// Buttons row
	QPushButton *btnStart = new QPushButton("Start");
	QPushButton *btnPause = new QPushButton("Pause");
	QPushButton *btnReset = new QPushButton("Reset");
	QPushButton *btnEnsure = new QPushButton("Ensure Browser Source");
	QPushButton *btnVisibility = new QPushButton("Hide from Stream");

	btnStart->setObjectName("rs-primary-button");

	btnPause->setObjectName("rs-secondary-button");
	btnReset->setObjectName("rs-secondary-button");
	btnEnsure->setObjectName("rs-secondary-button");
	btnVisibility->setObjectName("rs-secondary-button");

	btnStart->setMinimumHeight(28);
	btnPause->setMinimumHeight(28);
	btnReset->setMinimumHeight(28);
	btnVisibility->setMinimumHeight(28);
	btnEnsure->setMinimumHeight(28);

	QObject::connect(btnVisibility, &QPushButton::clicked, page, [=]() {
		state->visibleOnStream = !state->visibleOnStream;

		setTimerSourceVisible(state->visibleOnStream);

		btnVisibility->setText(state->visibleOnStream ? "Hide from Stream" : "Show on Stream");
	});


	btnPause->setEnabled(false);

{
		auto *grid = new QGridLayout();
		grid->setHorizontalSpacing(8);
		grid->setVerticalSpacing(8);

		// Row 0
		grid->addWidget(btnStart, 0, 0);
		grid->addWidget(btnPause, 0, 1);

		// Row 1
		grid->addWidget(btnReset, 1, 0);
		grid->addWidget(btnVisibility, 1, 1);

		// Row 2 (full width)
		grid->addWidget(btnEnsure, 2, 0, 1, 2);

		root->addLayout(grid);
	}


	// Helper: set enabled states based on mode/running
	auto refreshUiEnabled = [=]() {
		const bool isCountdown = (state->mode == TimerMode::Countdown);

		// Duration only meaningful for countdown.
		mins->setEnabled(isCountdown && !state->running);
		secs->setEnabled(isCountdown && !state->running);

		// Mode switching while running causes weird transitions: lock it.
		modeCombo->setEnabled(!state->running);

		// Start is only available when not running.
		btnStart->setEnabled(!state->running);

		// Pause only available while running.
		btnPause->setEnabled(state->running);
		btnPause->setText(state->paused ? "Resume" : "Pause");
	};

	// Duration recalculation (countdown only)
	auto recalcCountdownFromSpin = [=]() {
		if (state->mode != TimerMode::Countdown)
			return;

		const int t = mins->value() * 60 + secs->value();
		state->totalSeconds = t;
		state->remainingSeconds = t;
		state->elapsedSeconds = 0;

		updateTimerBrowserUrl(*state);
		updatePreview();
	};

	QObject::connect(mins, QOverload<int>::of(&QSpinBox::valueChanged), page,
			 [=](int) { recalcCountdownFromSpin(); });
	QObject::connect(secs, QOverload<int>::of(&QSpinBox::valueChanged), page,
			 [=](int) { recalcCountdownFromSpin(); });

	QObject::connect(modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), page, [=](int idx) {
		const auto m = (TimerMode)modeCombo->itemData(idx).toInt();


		// FIX: switching modes always stops + resets appropriately
		state->mode = m;
		state->running = false;
		state->paused = false;
		state->lastTickMs = QDateTime::currentMSecsSinceEpoch();

		if (state->mode == TimerMode::Countdown) {
			// countdown resets to the current duration setting
			recalcCountdownFromSpin();
			updatePreview();
		} else {
			// stopwatch resets to 0 and ignores duration inputs
			state->totalSeconds = 0;
			state->remainingSeconds = 0;
			state->elapsedSeconds = 0;

			updateTimerBrowserUrl(*state);
			updatePreview();
		}

		refreshUiEnabled();
	});

	// Tick timer (drives ONLY the UI model; overlay is JS-driven)
	QTimer *tick = new QTimer(page);
	tick->setInterval(250);
	tick->start();

	QObject::connect(btnEnsure, &QPushButton::clicked, page, [=]() {
		ensureTimerBrowserSource(*state);
		updateTimerBrowserUrl(*state);
		updatePreview();
	});

	QObject::connect(btnStart, &QPushButton::clicked, page, [=]() {
		ensureTimerBrowserSource(*state);

		// FIX: Start always starts fresh for stopwatch
		if (state->mode == TimerMode::Stopwatch) {
			state->elapsedSeconds = 0;
			state->remainingSeconds = 0;
		} else {
			// Countdown starts from whatever duration is currently set (remainingSeconds already set)
			if (state->remainingSeconds <= 0)
				state->remainingSeconds = state->totalSeconds;
		}

		state->running = true;
		state->paused = false;
		state->lastTickMs = QDateTime::currentMSecsSinceEpoch();

		updateTimerBrowserUrl(*state);
		updatePreview();
		refreshUiEnabled();
	});

	QObject::connect(btnPause, &QPushButton::clicked, page, [=]() {
		if (!state->running)
			return;

		// FIX: Bake pending time BEFORE toggling pause state
		applyPendingTick(state);

		// Toggle paused
		state->paused = !state->paused;

		// FIX: On resume, reset baseline so no "catch up" happens.
		if (!state->paused) {
			state->lastTickMs = QDateTime::currentMSecsSinceEpoch();
		}

		updateTimerBrowserUrl(*state);
		updatePreview();
		refreshUiEnabled();
	});

	QObject::connect(btnReset, &QPushButton::clicked, page, [=]() {
		state->running = false;
		state->paused = false;
		state->lastTickMs = QDateTime::currentMSecsSinceEpoch();

		// FIX: Stopwatch reset -> 0; Countdown reset -> duration
		state->elapsedSeconds = 0;
		if (state->mode == TimerMode::Countdown) {
			state->remainingSeconds = state->totalSeconds;
		} else {
			state->remainingSeconds = 0;
		}

		updateTimerBrowserUrl(*state);
		updatePreview();
		refreshUiEnabled();
	});

	QObject::connect(tick, &QTimer::timeout, page, [=]() {
		if (!state->running || state->paused)
			return;

		const qint64 now = QDateTime::currentMSecsSinceEpoch();
		qint64 deltaMs = now - state->lastTickMs;
		if (deltaMs < 0)
			deltaMs = 0;

		const int deltaSec = (int)(deltaMs / 1000);
		if (deltaSec <= 0)
			return;

		state->lastTickMs += (qint64)deltaSec * 1000;

		if (state->mode == TimerMode::Stopwatch) {
			state->elapsedSeconds += deltaSec;
		} else {
			state->remainingSeconds -= deltaSec;
			if (state->remainingSeconds <= 0) {
				state->remainingSeconds = 0;
				state->running = false;
				state->paused = false;

				// Countdown completion is a transition -> update overlay once
				updateTimerBrowserUrl(*state);
				refreshUiEnabled();
			}
		}

		updatePreview();
	});

	// Initial UI state
	refreshUiEnabled();
	updatePreview();

	btnVisibility->setText(state->visibleOnStream ? "Hide from Stream" : "Show on Stream");


	root->addStretch(1);

	(void)dock;
	return scroll;
}
