#include "rs_instant_replay.hpp"
#include "../rs_main_dock.hpp"

#include <QFileInfo>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QSpinBox>
#include <QPushButton>
#include <QGroupBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QDir>  
#include <QFileDialog>
#include <QTimer>
#include <QDateTime>
#include <QScrollArea>

#include <vector>
#include <cstring>
#include <obs.h>
#include <obs-frontend-api.h>
#include <util/platform.h>
#include <util/config-file.h>

// ------------------------------------------------------------
// Constants
// ------------------------------------------------------------
static const char *kReplaySourceName = "RS Instant Replay";
static const char *kReplaySecondsKey = "RSInstantReplaySeconds";
static const char *kReplayAutoStartKey = "RSInstantReplayAutoStart";
static const char *kReplayAutoHideKey = "RSInstantReplayAutoHide";
static const char *kReplayBgSourceName = "RS Instant Replay Background";
static const char *kReplayBgImageKey = "RSInstantReplayBgImage";

// ------------------------------------------------------------S
// Replay state
// ------------------------------------------------------------
static QString s_lastReplayFile;
static qint64 s_lastReplayRequestTime = 0;
static qint64 s_replayBufferStartTime = 0;

// ------------------------------------------------------------
// Hotkey
// ------------------------------------------------------------
static obs_hotkey_id s_replayHotkey = OBS_INVALID_HOTKEY_ID;


// ------------------------------------------------------------
// Replay folder override (profile config)
// ------------------------------------------------------------
static const char *kReplayFolderKey = "RSInstantReplayFolder";

QString RsInstantReplay::replayFolderOverride()
{
	config_t *cfg = obs_frontend_get_profile_config();
	if (!cfg)
		return QString();

	const char *path = config_get_string(cfg, "RearSilver", kReplayFolderKey);
	return path ? QString::fromUtf8(path) : QString();
}

void RsInstantReplay::setReplayFolderOverride(const QString &path)
{
	config_t *cfg = obs_frontend_get_profile_config();
	if (!cfg)
		return;

	config_set_string(cfg, "RearSilver", kReplayFolderKey, path.toUtf8().constData());
	config_save(cfg);
}

// ------------------------------------------------------------
// Load / Save replay seconds
// ------------------------------------------------------------

static int loadReplaySeconds()
{
	config_t *cfg = obs_frontend_get_profile_config();
	if (!cfg)
		return 10;

	int v = (int)config_get_int(cfg, "RearSilver", kReplaySecondsKey);
	return v > 0 ? v : 10;
}

static void saveReplaySeconds(int seconds)
{
	config_t *cfg = obs_frontend_get_profile_config();
	if (!cfg)
		return;

	config_set_int(cfg, "RearSilver", kReplaySecondsKey, seconds);
	config_save(cfg);
}
// ------------------------------------------------------------
// Load / Save replay auto-start
// ------------------------------------------------------------

static bool loadReplayAutoStart()
{
	config_t *cfg = obs_frontend_get_profile_config();
	if (!cfg)
		return false;

	return config_get_bool(cfg, "RearSilver", kReplayAutoStartKey);
}

static void saveReplayAutoStart(bool enabled)
{
	config_t *cfg = obs_frontend_get_profile_config();
	if (!cfg)
		return;

	config_set_bool(cfg, "RearSilver", kReplayAutoStartKey, enabled);
	config_save(cfg);
}

// ------------------------------------------------------------
// Load / Save replay auto-hide
// ------------------------------------------------------------

static bool loadReplayAutoHide()
{
	config_t *cfg = obs_frontend_get_profile_config();
	if (!cfg)
		return true; // sensible default: ON

	return config_get_bool(cfg, "RearSilver", kReplayAutoHideKey);
}

static void saveReplayAutoHide(bool enabled)
{
	config_t *cfg = obs_frontend_get_profile_config();
	if (!cfg)
		return;

	config_set_bool(cfg, "RearSilver", kReplayAutoHideKey, enabled);
	config_save(cfg);
}

// ------------------------------------------------------------
// Load / Save replay background image
// ------------------------------------------------------------

static QString loadReplayBgImage()
{
	config_t *cfg = obs_frontend_get_profile_config();
	if (!cfg)
		return QString();

	const char *path = config_get_string(cfg, "RearSilver", kReplayBgImageKey);
	return path ? QString::fromUtf8(path) : QString();
}

static void saveReplayBgImage(const QString &path)
{
	config_t *cfg = obs_frontend_get_profile_config();
	if (!cfg)
		return;

	config_set_string(cfg, "RearSilver", kReplayBgImageKey, path.toUtf8().constData());
	config_save(cfg);
}

static void refreshReplayBufferStatus(QLabel *label)
{
	if (!label)
		return;

	label->setText(QString("Status: %1").arg(obs_frontend_replay_buffer_active() ? "Active" : "Inactive"));
}


// ------------------------------------------------------------
// Helpers: scene/source
// ------------------------------------------------------------
static obs_sceneitem_t *findReplayGroup(obs_scene_t *scene);

static obs_sceneitem_t *findReplayItem(obs_scene_t *scene)
{
	if (!scene)
		return nullptr;

	// Look for the replay item inside the group scene
	obs_sceneitem_t *groupItem = findReplayGroup(scene);
	if (!groupItem)
		return nullptr;

	obs_source_t *groupSrc = obs_sceneitem_get_source(groupItem);
	if (!groupSrc)
		return nullptr;

	obs_scene_t *groupScene = obs_group_from_source(groupSrc);
	if (!groupScene)
		return nullptr;

	return obs_scene_find_source(groupScene, kReplaySourceName);
}

static obs_sceneitem_t *findReplayBgItem(obs_scene_t *scene)
{
	if (!scene)
		return nullptr;

	// Look for the background item inside the group scene
	obs_sceneitem_t *groupItem = findReplayGroup(scene);
	if (!groupItem)
		return nullptr;

	obs_source_t *groupSrc = obs_sceneitem_get_source(groupItem);
	if (!groupSrc)
		return nullptr;

	obs_scene_t *groupScene = obs_group_from_source(groupSrc);
	if (!groupScene)
		return nullptr;

	return obs_scene_find_source(groupScene, kReplayBgSourceName);
}

static obs_source_t *getReplaySource()
{
	return obs_get_source_by_name(kReplaySourceName);
}

static void initReplayGroupTransform(obs_sceneitem_t *group)
{
	if (!group)
		return;

	obs_video_info ovi;
	if (!obs_get_video_info(&ovi))
		return;

	// 1️⃣ FORCE the group to have real bounds (THIS is the missing piece)
	vec2 bounds = {(float)ovi.base_width, (float)ovi.base_height};

	obs_sceneitem_set_bounds_type(group, OBS_BOUNDS_SCALE_INNER);
	obs_sceneitem_set_bounds(group, &bounds);

	// 2️⃣ Anchor from centre
	obs_sceneitem_set_alignment(group, OBS_ALIGN_CENTER);

	// 3️⃣ NOW positioning works correctly
	vec2 pos = {(float)ovi.base_width * 0.5f, (float)ovi.base_height * 0.5f};

	obs_sceneitem_set_pos(group, &pos);
}

// ------------------------------------------------------------
// Replay Group helpers (OBS-safe, no hidden APIs)
// ------------------------------------------------------------

static obs_sceneitem_t *findReplayGroup(obs_scene_t *scene)
{
	if (!scene)
		return nullptr;

	obs_sceneitem_t *item = nullptr;

	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
			const char *name = obs_source_get_name(obs_sceneitem_get_source(item));
			if (name && strcmp(name, "RS Instant Replay Group") == 0) {
				*static_cast<obs_sceneitem_t **>(param) = item;
				return false; // stop enumeration
			}
			return true;
		},
		&item);

	return item;
}

static obs_sceneitem_t *findOrCreateReplayGroup(obs_scene_t *scene)
{
	if (!scene)
		return nullptr;

	obs_sceneitem_t *existing = findReplayGroup(scene);
	if (existing)
		return existing;

	// Create a group source
	obs_source_t *groupSrc = obs_source_create("group", "RS Instant Replay Group", nullptr, nullptr);
	if (!groupSrc)
		return nullptr;

	obs_sceneitem_t *groupItem = obs_scene_add(scene, groupSrc);
	obs_source_release(groupSrc);

	if (!groupItem)
		return nullptr;

	// 🔧 CRITICAL: initialise transform ON CREATION
	initReplayGroupTransform(groupItem);

	// Start hidden (playback will show it)
	obs_sceneitem_set_visible(groupItem, false);

	return groupItem;
}

static void centreItemInGroup(obs_sceneitem_t *item)
{
	if (!item)
		return;

	obs_sceneitem_set_alignment(item, OBS_ALIGN_CENTER);

	vec2 pos = {0.0f, 0.0f};
	obs_sceneitem_set_pos(item, &pos);

	obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_NONE);
}


// ------------------------------------------------------------
// Helpers: Replay Buffer configuration (best-effort)
// ------------------------------------------------------------
// OBS stores output settings in the *profile* config.
// Keys can vary between Simple/Advanced modes and versions.
// We'll do a best-effort write to the most common keys.
//
// If config access fails or keys differ, we must inform the user.
static bool trySetReplayBufferSeconds(int seconds)
{
	if (seconds < 2)
		seconds = 2;

	config_t *cfg = obs_frontend_get_profile_config();
	if (!cfg)
		return false;

	// Best-effort across both output modes:
	// SimpleOutput: RecRBTime (seconds), RecRB (bool)
	// AdvOut:       RecRBTime (seconds), RecRB (bool)
	//
	// If these keys are not present, config_set_* still writes them,
	// but OBS may or may not read them depending on version/mode.
	config_set_bool(cfg, "SimpleOutput", "RecRB", true);
	config_set_int(cfg, "SimpleOutput", "RecRBTime", seconds);

	config_set_bool(cfg, "AdvOut", "RecRB", true);
	config_set_int(cfg, "AdvOut", "RecRBTime", seconds);

	config_save(cfg);
	return true;
}

static bool tryStartReplayBuffer()
{
	// Start replay buffer if not running
	if (!obs_frontend_replay_buffer_active()) {
		obs_frontend_replay_buffer_start();
		s_replayBufferStartTime = QDateTime::currentMSecsSinceEpoch();
	}

	return obs_frontend_replay_buffer_active();
}

static QString replayBufferStatusText()
{
	// We can reliably detect active/inactive.
	// "Configured correctly" is harder without reading exact OBS settings schema,
	// so we don't pretend we know more than we do.
	return obs_frontend_replay_buffer_active() ? "Active" : "Inactive";
}

// ------------------------------------------------------------
// Ensure replay Media Source exists
// ------------------------------------------------------------
void RsInstantReplay::ensureReplaySource()
{
	obs_source_t *sceneSrc = obs_frontend_get_current_scene();
	if (!sceneSrc)
		return;

	obs_scene_t *scene = obs_scene_from_source(sceneSrc);
	if (!scene) {
		obs_source_release(sceneSrc);
		return;
	}

// If replay exists, nothing to do
	if (findReplayItem(scene)) {
		obs_source_release(sceneSrc);
		return;
	}

	// IMPORTANT:
	// Background MUST exist before replay so replay is top-most
	ensureReplayBgSource();


	// Create empty media source (file set later)
	obs_data_t *settings = obs_data_create();

	obs_data_set_bool(settings, "looping", false);
	obs_data_set_bool(settings, "restart_on_activate", true);
	obs_data_set_bool(settings, "close_when_inactive", false);

	obs_source_t *src = obs_source_create("ffmpeg_source", kReplaySourceName, settings, nullptr);
	obs_data_release(settings);

	if (!src) {
		obs_source_release(sceneSrc);
		return;
	}

	// Add to current scene
	obs_sceneitem_t *groupItem = findOrCreateReplayGroup(scene);
	if (!groupItem) {
		obs_source_release(src);
		obs_source_release(sceneSrc);
		return;
	}

	obs_source_t *groupSrc = obs_sceneitem_get_source(groupItem);
	if (!groupSrc) {
		obs_source_release(src);
		obs_source_release(sceneSrc);
		return;
	}

	obs_scene_t *groupScene = obs_group_from_source(groupSrc);
	if (!groupScene) {
		obs_source_release(src);
		obs_source_release(sceneSrc);
		return;
	}

	// Add the replay source INSIDE the group scene
	obs_sceneitem_t *item = obs_scene_add(groupScene, src);
	if (item) {
		centreItemInGroup(item);
		obs_sceneitem_set_visible(item, false);
	}

	obs_source_release(src);
	obs_source_release(sceneSrc);
}

void RsInstantReplay::ensureReplayBgSource()
{
	QString imgPath = loadReplayBgImage();
	if (imgPath.isEmpty())
		return;

	obs_source_t *sceneSrc = obs_frontend_get_current_scene();
	if (!sceneSrc)
		return;

	obs_scene_t *scene = obs_scene_from_source(sceneSrc);
	if (!scene) {
		obs_source_release(sceneSrc);
		return;
	}

	// If background already exists → UPDATE its file
	obs_sceneitem_t *existingItem = findReplayBgItem(scene);
	if (existingItem) {
		obs_source_t *src = obs_sceneitem_get_source(existingItem);
		if (src) {
			obs_data_t *settings = obs_source_get_settings(src);
			obs_data_set_string(settings, "file", imgPath.toUtf8().constData());
			obs_source_update(src, settings);
			obs_data_release(settings);
		}

		obs_source_release(sceneSrc);
		return;
	}

	// Otherwise → create it
	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "file", imgPath.toUtf8().constData());

	obs_source_t *src = obs_source_create("image_source", kReplayBgSourceName, settings, nullptr);
	obs_data_release(settings);

	if (!src) {
		obs_source_release(sceneSrc);
		return;
	}

	obs_sceneitem_t *groupItem = findOrCreateReplayGroup(scene);
	if (!groupItem) {
		obs_source_release(src);
		obs_source_release(sceneSrc);
		return;
	}

	obs_source_t *groupSrc = obs_sceneitem_get_source(groupItem);
	if (!groupSrc) {
		obs_source_release(src);
		obs_source_release(sceneSrc);
		return;
	}

	obs_scene_t *groupScene = obs_group_from_source(groupSrc);
	if (!groupScene) {
		obs_source_release(src);
		obs_source_release(sceneSrc);
		return;
	}

	// Add the background source INSIDE the group scene
	obs_sceneitem_t *bgItem = obs_scene_add(groupScene, src);
	if (bgItem) {
		centreItemInGroup(bgItem);
		obs_sceneitem_set_visible(bgItem, false);
	}


	obs_source_release(src);
	obs_source_release(sceneSrc);
}

// ------------------------------------------------------------
// Play replay file
// ------------------------------------------------------------
void RsInstantReplay::playReplay(const QString &filePath)
{
	if (filePath.isEmpty())
		return;

	QFileInfo fi(filePath);
	if (!fi.exists())
		return;

	ensureReplayBgSource(); // must exist first (goes underneath)
	ensureReplaySource();   // created second (goes on top)



	obs_source_t *src = getReplaySource();
	if (!src)
		return;

	obs_data_t *settings = obs_source_get_settings(src);

	// NOTE: ffmpeg_source uses "local_file" as the path setting.
	obs_data_set_string(settings, "local_file", filePath.toUtf8().constData());

	// Ensure clean playback every time
	obs_data_set_bool(settings, "restart_on_activate", true);
	obs_data_set_bool(settings, "looping", false);

	obs_source_update(src, settings);
	obs_data_release(settings);

	// Show source
	obs_source_t *sceneSrc = obs_frontend_get_current_scene();
	if (sceneSrc) {
		obs_scene_t *scene = obs_scene_from_source(sceneSrc);
		if (scene) {

obs_sceneitem_t *group = findOrCreateReplayGroup(scene);
			if (group) {
				initReplayGroupTransform(group);

				// Force group visible
				obs_sceneitem_set_visible(group, true);

				// ALSO force child items visible (OBS does NOT auto-propagate)
				obs_source_t *groupSrc = obs_sceneitem_get_source(group);
				if (groupSrc) {
					obs_scene_t *groupScene = obs_group_from_source(groupSrc);
					if (groupScene) {
						obs_sceneitem_t *bg =
							obs_scene_find_source(groupScene, kReplayBgSourceName);
						if (bg)
							obs_sceneitem_set_visible(bg, true);

						obs_sceneitem_t *replay =
							obs_scene_find_source(groupScene, kReplaySourceName);
						if (replay)
							obs_sceneitem_set_visible(replay, true);
					}
				}
			}


			// Auto-hide after replay duration
			if (loadReplayAutoHide()) {
				int replaySeconds = loadReplaySeconds();

				// OBS replay files are ~2 seconds shorter than requested
				int effectiveSeconds = qMax(1, replaySeconds - 2);

				QTimer::singleShot(effectiveSeconds * 1000,
						   []() { RsInstantReplay::hideReplaySource(); });
			}

		}
		obs_source_release(sceneSrc);
	}

	obs_source_release(src);
}

// ------------------------------------------------------------
// Hide replay source
// ------------------------------------------------------------
void RsInstantReplay::hideReplaySource()
{
	obs_source_t *sceneSrc = obs_frontend_get_current_scene();
	if (!sceneSrc)
		return;

	obs_scene_t *scene = obs_scene_from_source(sceneSrc);
	if (!scene) {
		obs_source_release(sceneSrc);
		return;
	}

	obs_sceneitem_t *group = findOrCreateReplayGroup(scene);
	if (group)
		obs_sceneitem_set_visible(group, false);

	obs_source_release(sceneSrc);
}


static QString findReplayAfterRequest(const QString &folder)
{
	QDir dir(folder);
	if (!dir.exists())
		return QString();

	const QFileInfoList files = dir.entryInfoList(QStringList() << "*.mp4" << "*.mkv", QDir::Files, QDir::Time);

	for (const QFileInfo &fi : files) {
		qint64 modified = fi.lastModified().toMSecsSinceEpoch();

		// Ignore files created too quickly (rename dialog still open)
		if (modified >= s_lastReplayRequestTime + 500) {
			return fi.absoluteFilePath();
		}
	}

	return QString();
}

static void playReplayAfterRename(int attempt)
{
	const QString folder = RsInstantReplay::replayFolderOverride();
	if (folder.isEmpty())
		return;

	QString file = findReplayAfterRequest(folder);

	// Keep polling until the file ACTUALLY exists (OBS + mover plugin delay)
	if (file.isEmpty()) {
		if (attempt < 40) {
			QTimer::singleShot(250, [attempt]() { playReplayAfterRename(attempt + 1); });
		} else {
			blog(LOG_WARNING, "[RS Instant Replay] Replay file never appeared in folder: %s",
			     folder.toUtf8().constData());
		}
		return;
	}

	// File finally exists → play it
	s_lastReplayFile = file;
	RsInstantReplay::playReplay(file);
}


// ------------------------------------------------------------
// Frontend event handling
// ------------------------------------------------------------
static void onFrontendEvent(enum obs_frontend_event event, void *)
{
	if (event != OBS_FRONTEND_EVENT_REPLAY_BUFFER_SAVED)
		return;

	// ALWAYS ensure the group & sources exist immediately
	RsInstantReplay::ensureReplayBgSource();
	RsInstantReplay::ensureReplaySource();

	// Then poll for the final file location
	QTimer::singleShot(500, []() { playReplayAfterRename(0); });
}

// ------------------------------------------------------------
// Hotkey callback
// ------------------------------------------------------------
static void replayHotkeyCallback(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	// Only trigger on key DOWN, not release
	if (!pressed)
		return;

	RsInstantReplay::triggerReplay();
}


// ------------------------------------------------------------
// Register the callbacks
// ------------------------------------------------------------
	void RsInstantReplay::registerFrontendCallbacks()
{
	static bool registered = false;
	if (registered)
		return;

	obs_frontend_add_event_callback(onFrontendEvent, nullptr);

	// Register hotkey
	s_replayHotkey = obs_hotkey_register_frontend("rs_instant_replay_trigger", "RearSilver: Trigger Instant Replay",
						      replayHotkeyCallback, nullptr);
	registered = true;
	if (loadReplayAutoStart()) {
		QTimer::singleShot(500, []() {
			if (!obs_frontend_replay_buffer_active())
				obs_frontend_replay_buffer_start();
		});
	}
}

// ------------------------------------------------------------
// Trigger replay buffer save
// ------------------------------------------------------------
void RsInstantReplay::triggerReplay()
{
	// Mark the moment THIS replay was requested
	s_lastReplayRequestTime = QDateTime::currentMSecsSinceEpoch();
	s_lastReplayFile.clear();

	// Ensure Replay Buffer is running
	if (!obs_frontend_replay_buffer_active())
		obs_frontend_replay_buffer_start();

	// Ask OBS to save the replay buffer
	qint64 now = QDateTime::currentMSecsSinceEpoch();
	qint64 bufferAgeMs = now - s_replayBufferStartTime;

	// If buffer hasn't filled long enough, delay save
	if (bufferAgeMs < 1000) {
		// Buffer literally just started – avoid garbage clips
		QTimer::singleShot(1000, []() { obs_frontend_replay_buffer_save(); });
	} else {
		obs_frontend_replay_buffer_save();
	}
}

// ------------------------------------------------------------
// UI Page
// ------------------------------------------------------------
QWidget *RsInstantReplay::createPage(RsMainDock *dock, QWidget *parent)
{
	// Outer scroll wrapper (matches System pages behaviour)
	auto *scroll = new QScrollArea(parent);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setWidgetResizable(true);

	// Actual page content
	QWidget *page = new QWidget();
	page->setObjectName("rs-card");

	scroll->setWidget(page);

	// Register OBS replay callback ONCE (safe here)
	RsInstantReplay::registerFrontendCallbacks();

	auto *root = new QVBoxLayout(page);
	root->setContentsMargins(10, 10, 10, 10);
	root->setSpacing(12);

	// --------------------------------------------------------
	// Top: What this is + primary action FIRST
	// --------------------------------------------------------
	QPushButton *btnReplay = nullptr;
	QLabel *statusLine = nullptr;

	{

			// Header
		auto *title = new QLabel("Instant Replay", page);
		QFont f = title->font();
		f.setBold(true);
		f.setPointSize(f.pointSize() + 1);
		title->setFont(f);
		root->addWidget(title);
		title->setMaximumHeight(title->sizeHint().height());


		auto *box = new QGroupBox("How it works:");
		box->setObjectName("rs-card");

		auto *v = new QVBoxLayout(box);
		v->setContentsMargins(10, 10, 10, 10);
		v->setSpacing(10);

		auto *desc = new QLabel("Replay the last X seconds of your stream back on stream.\n"
					"\n"
					"• Uses OBS Replay Buffer to continuously keep the last moments.\n"
					"• When triggered, the latest clip is saved and played back on stream.\n"
					"• Playback uses a dedicated replay source (styling comes later).");
		desc->setWordWrap(true);
		desc->setStyleSheet("opacity: 0.9;");
		v->addWidget(desc);

		btnReplay = new QPushButton("Play Instant Replay");
		btnReplay->setObjectName("rs-primary-button");
		btnReplay->setMinimumHeight(32);
		v->addWidget(btnReplay);

		// Small, helpful note directly under the action button
		auto *actionHint = new QLabel("Tip: For best results, keep Replay Buffer running while you stream.");
		actionHint->setWordWrap(true);
		actionHint->setStyleSheet("opacity: 0.65; font-size: 11px;");
		v->addWidget(actionHint);

		root->addWidget(box);
	}

	// --------------------------------------------------------
	// Replay Buffer Setup
	// --------------------------------------------------------
	QSpinBox *seconds = nullptr;
	QCheckBox *chkAutoEnable = nullptr;
	QCheckBox *chkAutoDuration = nullptr;
	QLineEdit *replayFolderEdit = nullptr;

	{
		auto *box = new QGroupBox("Replay Buffer");
		box->setObjectName("rs-card");

		auto *grid = new QGridLayout(box);
		grid->setContentsMargins(10, 10, 10, 10);
		grid->setHorizontalSpacing(8);
		grid->setVerticalSpacing(8);
		grid->setColumnStretch(0, 0);
		grid->setColumnStretch(1, 1);

		int row = 0;

		// Status (full width)
		statusLine = new QLabel(QString("Status: %1").arg(replayBufferStatusText()));
		statusLine->setStyleSheet("opacity: 0.85;");
		statusLine->setWordWrap(true);
		grid->addWidget(statusLine, row++, 0, 1, 2);

		chkAutoEnable = new QCheckBox("Auto-enable Replay Buffer when needed");
		chkAutoEnable->setChecked(loadReplayAutoStart());
		QObject::connect(chkAutoEnable, &QCheckBox::toggled, page, [](bool on) { saveReplayAutoStart(on); });
		grid->addWidget(chkAutoEnable, row++, 0, 1, 2);

		// Replay last (label + control)
		grid->addWidget(new QLabel("Replay last:"), row, 0);

		seconds = new QSpinBox();
		seconds->setRange(2, 300);
		int savedSeconds = loadReplaySeconds();
		seconds->setValue(savedSeconds);
		trySetReplayBufferSeconds(savedSeconds);
		seconds->setSuffix(" seconds");
		grid->addWidget(seconds, row++, 1);

		chkAutoDuration = new QCheckBox("Match Replay Buffer duration to “Replay last”");
		chkAutoDuration->setChecked(true);
		grid->addWidget(chkAutoDuration, row++, 0, 1, 2);

		auto *hint = new QLabel("If auto-setup fails, enable it manually in:\n"
					"OBS Settings → Output → Replay Buffer\n"
					"Then set Maximum Replay Time to match “Replay last”.");
		hint->setWordWrap(true);
		hint->setStyleSheet("opacity: 0.65; font-size: 11px;");
		grid->addWidget(hint, row++, 0, 1, 2);

		// Replay folder explanation
		auto *folderExplainer = new QLabel("Replay folder\n"
						   "This must match the folder where OBS saves Replay Buffer clips.\n"
						   "Instant Replay looks here to find the latest saved clip.");
		folderExplainer->setWordWrap(true);
		folderExplainer->setStyleSheet("opacity: 0.85;");
		grid->addWidget(folderExplainer, row++, 0, 1, 2);

		auto *folderLabel = new QLabel("Replay clip location");
		folderLabel->setStyleSheet("font-weight: 600;");
		grid->addWidget(folderLabel, row++, 0, 1, 2);

		// Folder picker: label is above (explainer), then control row
		{
			auto *h = new QHBoxLayout();
			h->setSpacing(8);

			replayFolderEdit = new QLineEdit();
			replayFolderEdit->setText(RsInstantReplay::replayFolderOverride());
			replayFolderEdit->setPlaceholderText("Select replay save folder…");
			replayFolderEdit->setMinimumHeight(30);
			h->addWidget(replayFolderEdit, 1);

			auto *btnBrowse = new QPushButton("Browse…");
			btnBrowse->setObjectName("rs-secondary-button");
			btnBrowse->setMinimumHeight(30);
			h->addWidget(btnBrowse);

			grid->addLayout(h, row++, 0, 1, 2);

			QObject::connect(btnBrowse, &QPushButton::clicked, page, [=]() {
				QString dir = QFileDialog::getExistingDirectory(page, "Select Replay Folder",
										replayFolderEdit->text());
				if (!dir.isEmpty()) {
					replayFolderEdit->setText(dir);
					RsInstantReplay::setReplayFolderOverride(dir);
				}
			});

			QObject::connect(replayFolderEdit, &QLineEdit::editingFinished, page,
					 [=]() { RsInstantReplay::setReplayFolderOverride(replayFolderEdit->text()); });
		}

		// Apply button
		auto *btnApply = new QPushButton("Apply Replay Buffer Settings Now");
		btnApply->setObjectName("rs-secondary-button");
		btnApply->setMinimumHeight(30);
		grid->addWidget(btnApply, row++, 0, 1, 2);

		QObject::connect(btnApply, &QPushButton::clicked, page, [=]() {
			if (chkAutoEnable && chkAutoEnable->isChecked())
				tryStartReplayBuffer();

			if (statusLine)
				statusLine->setText(QString("Status: %1").arg(replayBufferStatusText()));
		});

		// Keep status fresh
		QTimer::singleShot(1000, page, [=]() { refreshReplayBufferStatus(statusLine); });

		root->addWidget(box);
	}

	// --------------------------------------------------------
	// Wire up behaviour (kept identical, just organised)
	// --------------------------------------------------------
	QObject::connect(seconds, QOverload<int>::of(&QSpinBox::valueChanged), page, [=](int v) {
		saveReplaySeconds(v);

		if (chkAutoDuration && chkAutoDuration->isChecked()) {
			trySetReplayBufferSeconds(v);
		}
	});

	QObject::connect(btnReplay, &QPushButton::clicked, page, [=]() {
		if (chkAutoEnable && chkAutoEnable->isChecked()) {
			if (chkAutoDuration && chkAutoDuration->isChecked() && seconds)
				trySetReplayBufferSeconds(seconds->value());

			tryStartReplayBuffer();

			if (statusLine)
				statusLine->setText(QString("Status: %1").arg(replayBufferStatusText()));
		}

		RsInstantReplay::triggerReplay();
	});

	// --------------------------------------------------------
	// Hotkey Setup (info only; NO settings button)
	// --------------------------------------------------------
	{
		auto *box = new QGroupBox("Hotkey");
		box->setObjectName("rs-card");

		auto *v = new QVBoxLayout(box);
		v->setContentsMargins(10, 10, 10, 10);
		v->setSpacing(6);

		auto *desc = new QLabel("Instant Replay supports a global hotkey.\n"
					"\n"
					"To assign it:\n"
					"OBS Settings → Hotkeys → RearSilver Stream Suite → Trigger Instant Replay");
		desc->setWordWrap(true);
		desc->setStyleSheet("opacity: 0.8;");
		v->addWidget(desc);

		root->addWidget(box);
	}

	// --------------------------------------------------------
	// Options (behaviour toggles)
	// --------------------------------------------------------
	{
		auto *box = new QGroupBox("Options");
		box->setObjectName("rs-card");

		auto *v = new QVBoxLayout(box);
		v->setContentsMargins(10, 10, 10, 10);
		v->setSpacing(6);

		auto *chkAutoHide = new QCheckBox("Hide replay source when finished");
		chkAutoHide->setChecked(loadReplayAutoHide());
		QObject::connect(chkAutoHide, &QCheckBox::toggled, page, [](bool on) { saveReplayAutoHide(on); });

		v->addWidget(chkAutoHide);

		root->addWidget(box);
	}

	// --------------------------------------------------------
	// Replay Frame Styling (Background image is REAL, not a placeholder)
	// --------------------------------------------------------
	{
		auto *box = new QGroupBox("Replay Frame");
		box->setObjectName("rs-card");

		auto *grid = new QGridLayout(box);
		grid->setContentsMargins(10, 10, 10, 10);
		grid->setHorizontalSpacing(8);
		grid->setVerticalSpacing(8);
		grid->setColumnStretch(0, 1);

		int row = 0;

		auto *explainer = new QLabel("Background image\n"
					     "This image is shown behind the replay video (inside the replay group).\n"
					     "Use it for a frame, branding, or a themed replay backdrop.");
		explainer->setWordWrap(true);
		explainer->setStyleSheet("opacity: 0.85;");
		grid->addWidget(explainer, row++, 0, 1, 1);

		auto *bgLabel = new QLabel("Replay background image");
		bgLabel->setStyleSheet("font-weight: 600;");
		grid->addWidget(bgLabel, row++, 0, 1, 1);


		// Background image picker (label above via explainer)
		QLineEdit *bgPathEdit = nullptr;
		{
			auto *h = new QHBoxLayout();
			h->setSpacing(8);

			bgPathEdit = new QLineEdit();
			bgPathEdit->setText(loadReplayBgImage());
			bgPathEdit->setPlaceholderText("Select image file…");
			bgPathEdit->setMinimumHeight(30);
			h->addWidget(bgPathEdit, 1);

			auto *btnBgBrowse = new QPushButton("Browse…");
			btnBgBrowse->setObjectName("rs-secondary-button");
			btnBgBrowse->setMinimumHeight(30);
			h->addWidget(btnBgBrowse);

			grid->addLayout(h, row++, 0, 1, 1);

			QObject::connect(btnBgBrowse, &QPushButton::clicked, page, [=]() {
				QString file = QFileDialog::getOpenFileName(page, "Select Replay Background Image",
									    bgPathEdit->text(),
									    "Images (*.png *.jpg *.jpeg *.webp)");

				if (!file.isEmpty()) {
					bgPathEdit->setText(file);
					saveReplayBgImage(file);
					RsInstantReplay::ensureReplayBgSource();
				}
			});

			QObject::connect(bgPathEdit, &QLineEdit::editingFinished, page, [=]() {
				saveReplayBgImage(bgPathEdit->text());
				RsInstantReplay::ensureReplayBgSource();
			});
		}

		// Other controls are still placeholders (honestly labelled)
		auto *placeholderNote = new QLabel(
			"More frame styling controls will be added here later (colour, border, padding, label, etc.).");
		placeholderNote->setWordWrap(true);
		placeholderNote->setStyleSheet("opacity: 0.65; font-size: 11px;");
		grid->addWidget(placeholderNote, row++, 0, 1, 1);

		root->addWidget(box);
	}

	// ========================================================
	// FOOTER
	// ========================================================
	auto *hint = new QLabel("Tip: Keep Replay Buffer running while streaming for best results.");
	hint->setWordWrap(true);
	hint->setStyleSheet("opacity: 0.65; font-size: 11px;");
	root->addWidget(hint);

	root->addStretch(1);
	(void)dock;
	return scroll;
}
