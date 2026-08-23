#include "rs_instant_replay.hpp"
#include "rs_main_dock.hpp"

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
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QFont>
#include <QStandardPaths>
#include <QColor>
#include <QRect>
#include <QCoreApplication>
#include <QHash>

#include <atomic>
#include <vector>
#include <cstring>
#include <obs.h>
#include <obs-frontend-api.h>
#include <util/platform.h>
#include <util/config-file.h>

namespace hub_replay {

// ------------------------------------------------------------
// Constants
// ------------------------------------------------------------
static const char *kReplaySourceName = "RS Instant Replay";
static const char *kReplayBgSourceName = "RS Instant Replay Background";
static const char *kReplayBgImageKey = "RSInstantReplayBgImage";

// ------------------------------------------------------------S
// Replay state
// ------------------------------------------------------------
static QString s_lastReplayFile;
static qint64 s_lastReplayRequestTime = 0;
static qint64 s_replayBufferStartTime = 0;
static bool s_waitingForRequestedReplay = false;
static QString s_visibleReplaySceneName;
static std::atomic<quint64> s_replayPlaybackGeneration{0};
static std::atomic<quint64> s_replayStartedGeneration{0};
static obs_source_t *s_replaySignalSource = nullptr;
static QString s_replaySizeStep = QStringLiteral("large");
static QString s_replayBorderStep = QStringLiteral("medium");
static QString s_replayRadiusStep = QStringLiteral("rounded");
static QString s_replayAlignment = QStringLiteral("left");
static QString s_replayFolderOverride;
static int s_replaySeconds = 10;
static bool s_replayAutoStart = false;
static bool s_replayAutoHide = true;

struct ReplayFrameLayout {
	int width = 1280;
	int height = 720;
	int x = 320;
	int y = 180;
	int border = 12;
	int outerRadius = 28;
	int innerRadius = 16;
	QRect aperture;
	QRect titleRect;
};

static ReplayFrameLayout s_replayFrameLayout;

static ReplayFrameLayout makeReplayFrameLayout(int requestedScale, int requestedBorder,
						 int requestedRadius, int titleSize)
{
	obs_video_info ovi = {};
	const bool haveVideo = obs_get_video_info(&ovi);
	const int canvasWidth = haveVideo && ovi.base_width ? static_cast<int>(ovi.base_width) : 1920;
	const int canvasHeight = haveVideo && ovi.base_height ? static_cast<int>(ovi.base_height) : 1080;

	ReplayFrameLayout layout;
	int fullWidth = canvasWidth;
	int fullHeight = (fullWidth * 9) / 16;
	if (fullHeight > canvasHeight) {
		fullHeight = canvasHeight;
		fullWidth = (fullHeight * 16) / 9;
	}
	const double scale = qBound(25, requestedScale, 100) / 100.0;
	layout.width = qMax(1, qRound(fullWidth * scale));
	layout.height = qMax(1, (layout.width * 9) / 16);
	layout.x = qMax(0, (canvasWidth - layout.width) / 2);
	layout.y = qMax(0, (canvasHeight - layout.height) / 2);
	layout.border = qBound(0, requestedBorder, 64);
	const int maxRadius = qMax(0, qMin(layout.width, layout.height) / 2);
	// Keep the inner edge rounded even when a thick border is selected.
	layout.outerRadius = qBound(0, qMax(requestedRadius, layout.border + 8), maxRadius);
	layout.innerRadius = qMax(0, layout.outerRadius - layout.border);

	const int shortEdge = qMin(layout.width, layout.height);
	const int padding = qMax(layout.border + 16, qRound(shortEdge * 0.035));
	const int header = qMax(qRound(qBound(12, titleSize, 180) * 1.45), qRound(layout.height * 0.11));
	const int apertureTop = qMin(layout.height - padding - 1, padding + header);
	const int availableWidth = qMax(1, layout.width - (padding * 2));
	const int availableHeight = qMax(1, layout.height - apertureTop - padding);
	int apertureWidth = availableWidth;
	int apertureHeight = (apertureWidth * 9) / 16;
	if (apertureHeight > availableHeight) {
		apertureHeight = availableHeight;
		apertureWidth = (apertureHeight * 16) / 9;
	}
	layout.aperture = QRect(padding + (availableWidth - apertureWidth) / 2,
		apertureTop + (availableHeight - apertureHeight) / 2,
		qMax(1, apertureWidth), qMax(1, apertureHeight));
	layout.titleRect = QRect(padding, padding, layout.width - (padding * 2),
		qMax(1, apertureTop - padding));
	return layout;
}

// ------------------------------------------------------------
// Hotkey
// ------------------------------------------------------------
static obs_hotkey_id s_replayHotkey = OBS_INVALID_HOTKEY_ID;
static bool s_frontendCallbacksRegistered = false;


// ------------------------------------------------------------
// Replay settings are supplied by the Control Hub for this process lifetime.
// ------------------------------------------------------------
QString RsInstantReplay::replayFolderOverride()
{
	return s_replayFolderOverride;
}

void RsInstantReplay::setReplayFolderOverride(const QString &path)
{
	s_replayFolderOverride = path;
}

// ------------------------------------------------------------
// Load / Save replay seconds
// ------------------------------------------------------------

static int loadReplaySeconds()
{
	return s_replaySeconds;
}

static void saveReplaySeconds(int seconds)
{
	s_replaySeconds = qBound(2, seconds, 300);
}
// ------------------------------------------------------------
// Load / Save replay auto-start
// ------------------------------------------------------------

static bool loadReplayAutoStart()
{
	return s_replayAutoStart;
}

static void saveReplayAutoStart(bool enabled)
{
	s_replayAutoStart = enabled;
}

// ------------------------------------------------------------
// Load / Save replay auto-hide
// ------------------------------------------------------------

static bool loadReplayAutoHide()
{
	return s_replayAutoHide;
}

static void saveReplayAutoHide(bool enabled)
{
	s_replayAutoHide = enabled;
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
static bool trySetReplayBufferSeconds(int seconds);
static bool tryStartReplayBuffer();

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

static void disconnectReplayMediaSignals();

static void onReplayMediaStarted(void *, calldata_t *data)
{
	if (calldata_ptr(data, "source") == s_replaySignalSource)
		s_replayStartedGeneration.store(s_replayPlaybackGeneration.load());
}

static void onReplayMediaEnded(void *, calldata_t *data)
{
	if (calldata_ptr(data, "source") != s_replaySignalSource)
		return;
	const quint64 generation = s_replayStartedGeneration.exchange(0);
	if (!generation || generation != s_replayPlaybackGeneration.load())
		return;
	if (QObject *context = QCoreApplication::instance()) {
		QTimer::singleShot(0, context, [generation]() {
			if (generation == s_replayPlaybackGeneration.load())
				RsInstantReplay::hideReplaySource();
		});
	}
}

static void connectReplayMediaSignals(obs_source_t *source)
{
	if (source == s_replaySignalSource)
		return;
	disconnectReplayMediaSignals();
	if (!source)
		return;
	s_replaySignalSource = obs_source_get_ref(source);
	signal_handler_t *handler = obs_source_get_signal_handler(s_replaySignalSource);
	signal_handler_connect(handler, "media_started", onReplayMediaStarted, nullptr);
	signal_handler_connect(handler, "media_ended", onReplayMediaEnded, nullptr);
}

static void disconnectReplayMediaSignals()
{
	if (!s_replaySignalSource)
		return;
	signal_handler_t *handler = obs_source_get_signal_handler(s_replaySignalSource);
	signal_handler_disconnect(handler, "media_started", onReplayMediaStarted, nullptr);
	signal_handler_disconnect(handler, "media_ended", onReplayMediaEnded, nullptr);
	obs_source_release(s_replaySignalSource);
	s_replaySignalSource = nullptr;
	s_replayStartedGeneration.store(0);
}

static void initReplayGroupTransform(obs_sceneitem_t *group)
{
	if (!group)
		return;
	obs_sceneitem_set_alignment(group, OBS_ALIGN_LEFT | OBS_ALIGN_TOP);
	obs_sceneitem_set_bounds_type(group, OBS_BOUNDS_NONE);
	vec2 scale = {1.0f, 1.0f};
	obs_sceneitem_set_scale(group, &scale);
	vec2 pos = {(float)s_replayFrameLayout.x, (float)s_replayFrameLayout.y};
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

int RsInstantReplay::replaySeconds() { return loadReplaySeconds(); }
bool RsInstantReplay::replayAutoStart() { return loadReplayAutoStart(); }
bool RsInstantReplay::replayAutoHide() { return loadReplayAutoHide(); }
bool RsInstantReplay::replayBufferActive() { return obs_frontend_replay_buffer_active(); }

void RsInstantReplay::configureReplayBuffer(int seconds, bool autoStart, bool autoHide)
{
	seconds = qBound(2, seconds, 300);
	saveReplaySeconds(seconds);
	saveReplayAutoStart(autoStart);
	saveReplayAutoHide(autoHide);
	trySetReplayBufferSeconds(seconds);
	if (autoStart && !obs_frontend_replay_buffer_active())
		tryStartReplayBuffer();
}

void RsInstantReplay::startReplayBuffer()
{
	trySetReplayBufferSeconds(loadReplaySeconds());
	tryStartReplayBuffer();
}

void RsInstantReplay::stopReplayBuffer()
{
	if (obs_frontend_replay_buffer_active())
		obs_frontend_replay_buffer_stop();
}

void RsInstantReplay::showReplaySource()
{
	obs_source_t *sceneSrc = obs_frontend_get_current_scene();
	if (!sceneSrc)
		return;
	obs_scene_t *scene = obs_scene_from_source(sceneSrc);
	if (scene) {
		obs_sceneitem_t *group = findReplayGroup(scene);
		if (group) {
			obs_sceneitem_set_visible(group, true);
			obs_scene_t *groupScene = obs_sceneitem_group_get_scene(group);
			if (groupScene) {
				if (obs_sceneitem_t *background = obs_scene_find_source(groupScene, kReplayBgSourceName))
					obs_sceneitem_set_visible(background, true);
				if (obs_sceneitem_t *replay = obs_scene_find_source(groupScene, kReplaySourceName))
					obs_sceneitem_set_visible(replay, true);
			}
			s_visibleReplaySceneName = QString::fromUtf8(obs_source_get_name(sceneSrc));
		}
	}
	obs_source_release(sceneSrc);
}

static void fitReplayInsideBackground(obs_scene_t *groupScene);

void RsInstantReplay::repairReplaySource()
{
	ensureReplayBgSource();
	ensureReplaySource();

	obs_source_t *sceneSource = obs_frontend_get_current_scene();
	if (!sceneSource)
		return;

	obs_scene_t *scene = obs_scene_from_source(sceneSource);
	obs_sceneitem_t *group = findReplayGroup(scene);
	if (group) {
		obs_source_t *groupSource = obs_sceneitem_get_source(group);
		obs_scene_t *groupScene = groupSource ? obs_group_from_source(groupSource) : nullptr;
		fitReplayInsideBackground(groupScene);
		initReplayGroupTransform(group);
	}

	obs_source_release(sceneSource);
}

void RsInstantReplay::configureReplayFrame(const QString &title, const QString &font, const QString &alignment,
					     const QString &background, const QString &accent,
					     const QString &textColour, int opacity,
					     const QString &sizeStep, const QString &borderStep, const QString &radiusStep)
{
	static const QHash<QString, int> sizeScales{{"small", 40}, {"medium", 60}, {"large", 80}, {"fullscreen", 100}};
	static const QHash<QString, int> titleSizes{{"small", 28}, {"medium", 36}, {"large", 46}, {"fullscreen", 56}};
	static const QHash<QString, int> borderWidths{{"none", 0}, {"thin", 4}, {"medium", 8}, {"thick", 12}};
	static const QHash<QString, int> radii{{"square", 0}, {"subtle", 10}, {"rounded", 20}, {"soft", 32}};
	s_replaySizeStep = sizeScales.contains(sizeStep) ? sizeStep : QStringLiteral("large");
	s_replayBorderStep = borderWidths.contains(borderStep) ? borderStep : QStringLiteral("medium");
	s_replayRadiusStep = radii.contains(radiusStep) ? radiusStep : QStringLiteral("rounded");
	s_replayAlignment = alignment == "center" || alignment == "right" ? alignment : QStringLiteral("left");
	const int titleSize = titleSizes.value(s_replaySizeStep);
	const int borderWidth = borderWidths.value(s_replayBorderStep);
	const int radius = radii.value(s_replayRadiusStep);
	const int frameScale = sizeScales.value(s_replaySizeStep);
	s_replayFrameLayout = makeReplayFrameLayout(frameScale, borderWidth, radius, titleSize);
	const ReplayFrameLayout &layout = s_replayFrameLayout;
	QColor bg(background);
	QColor edge(accent);
	QColor headingColour(textColour);
	if (!bg.isValid()) bg = QColor("#0b0f14");
	if (!edge.isValid()) edge = QColor("#00d4ff");
	if (!headingColour.isValid()) headingColour = QColor("#e6e8eb");
	bg.setAlpha(qRound(qBound(0, opacity, 100) * 2.55));

	QImage image(layout.width, layout.height, QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	QPainter painter(&image);
	painter.setRenderHint(QPainter::Antialiasing, true);
	const QRectF outerRect(layout.border / 2.0, layout.border / 2.0,
			       layout.width - layout.border, layout.height - layout.border);
	QPainterPath outer;
	outer.addRoundedRect(outerRect, layout.outerRadius, layout.outerRadius);
	QPainterPath inner;
	inner.addRoundedRect(QRectF(layout.aperture), layout.innerRadius, layout.innerRadius);
	painter.fillPath(outer.subtracted(inner), bg);
	painter.setPen(QPen(edge, layout.border));
	painter.drawRoundedRect(outerRect, layout.outerRadius, layout.outerRadius);

	const QString heading = title.trimmed().isEmpty() ? QStringLiteral("INSTANT REPLAY") : title.trimmed();
	QFont headingFont(font.trimmed().isEmpty() ? QStringLiteral("Sora") : font, qBound(12, titleSize, 180));
	headingFont.setBold(true);
	painter.setFont(headingFont);
	painter.setPen(headingColour);
	Qt::Alignment titleAlignment = Qt::AlignVCenter;
	if (s_replayAlignment == QStringLiteral("center"))
		titleAlignment |= Qt::AlignHCenter;
	else if (s_replayAlignment == QStringLiteral("right"))
		titleAlignment |= Qt::AlignRight;
	else
		titleAlignment |= Qt::AlignLeft;
	painter.drawText(layout.titleRect, titleAlignment, heading);
	painter.end();

	const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/Replay";
	QDir().mkpath(dir);
	static bool alternateFrameFile = false;
	alternateFrameFile = !alternateFrameFile;
	const QString path = dir + (alternateFrameFile ? "/replay-frame-a.png" : "/replay-frame-b.png");
	if (!image.save(path, "PNG"))
		return;
	saveReplayBgImage(path);
	ensureReplayBgSource();
	obs_source_t *sceneSource = obs_frontend_get_current_scene();
	if (sceneSource) {
		obs_scene_t *scene = obs_scene_from_source(sceneSource);
		obs_sceneitem_t *group = findReplayGroup(scene);
		if (group) {
			obs_source_t *groupSource = obs_sceneitem_get_source(group);
			obs_scene_t *groupScene = groupSource ? obs_group_from_source(groupSource) : nullptr;
			fitReplayInsideBackground(groupScene);
			initReplayGroupTransform(group);
		}
		obs_source_release(sceneSource);
	}
}

QJsonObject RsInstantReplay::replayState()
{
	return {{"bufferActive", replayBufferActive()},
		{"seconds", replaySeconds()}, {"autoStart", replayAutoStart()}, {"autoHide", replayAutoHide()},
		{"sizeStep", s_replaySizeStep}, {"borderStep", s_replayBorderStep},
		{"radiusStep", s_replayRadiusStep}, {"alignment", s_replayAlignment}};
}

static void fitReplayInsideBackground(obs_scene_t *groupScene)
{
	if (!groupScene)
		return;
	obs_sceneitem_t *replay = obs_scene_find_source(groupScene, kReplaySourceName);
	obs_sceneitem_t *background = obs_scene_find_source(groupScene, kReplayBgSourceName);
	if (!replay || !background)
		return;

	obs_sceneitem_set_alignment(background, OBS_ALIGN_LEFT | OBS_ALIGN_TOP);
	obs_sceneitem_set_bounds_type(background, OBS_BOUNDS_NONE);
	vec2 backgroundScale = {1.0f, 1.0f};
	obs_sceneitem_set_scale(background, &backgroundScale);
	vec2 backgroundPos = {0.0f, 0.0f};
	obs_sceneitem_set_pos(background, &backgroundPos);

	const QRect &aperture = s_replayFrameLayout.aperture;
	vec2 bounds = {(float)aperture.width(), (float)aperture.height()};
	obs_sceneitem_set_alignment(replay, OBS_ALIGN_LEFT | OBS_ALIGN_TOP);
	obs_sceneitem_set_bounds_alignment(replay, OBS_ALIGN_CENTER);
	obs_sceneitem_set_bounds_type(replay, OBS_BOUNDS_SCALE_INNER);
	obs_sceneitem_set_bounds(replay, &bounds);
	vec2 pos = {(float)aperture.x(), (float)aperture.y()};
	obs_sceneitem_set_pos(replay, &pos);
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

	const quint64 playbackGeneration = s_replayPlaybackGeneration.fetch_add(1) + 1;
	s_replayStartedGeneration.store(0);

	ensureReplayBgSource(); // must exist first (goes underneath)
	ensureReplaySource();   // created second (goes on top)



	obs_source_t *src = getReplaySource();
	if (!src)
		return;
	connectReplayMediaSignals(src);

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
		const char *sceneName = obs_source_get_name(sceneSrc);
		s_visibleReplaySceneName = sceneName ? QString::fromUtf8(sceneName) : QString();
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
						fitReplayInsideBackground(groupScene);
					}
				}
			}

			// Save & Play owns playback visibility regardless of any earlier
			// manual Show/Hide action. Restart explicitly because activating an
			// already-visible media source does not trigger restart_on_activate.
			obs_source_media_restart(src);

			// Auto-hide after replay duration
			if (loadReplayAutoHide()) {
				const int replayMilliseconds = qMax(1, loadReplaySeconds()) * 1000;
				QTimer::singleShot(replayMilliseconds + 250, [playbackGeneration]() {
					if (playbackGeneration == s_replayPlaybackGeneration)
						RsInstantReplay::hideReplaySource();
				});
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
	s_replayStartedGeneration.store(0);
	obs_source_t *sceneSrc = s_visibleReplaySceneName.isEmpty()
				 ? obs_frontend_get_current_scene()
				 : obs_get_source_by_name(s_visibleReplaySceneName.toUtf8().constData());
	if (!sceneSrc) {
		s_visibleReplaySceneName.clear();
		disconnectReplayMediaSignals();
		return;
	}

	obs_scene_t *scene = obs_scene_from_source(sceneSrc);
	if (!scene) {
		obs_source_release(sceneSrc);
		return;
	}

	// Hiding must never create replay sources in a different scene.
	obs_sceneitem_t *group = findReplayGroup(scene);
	if (group)
		obs_sceneitem_set_visible(group, false);

	obs_source_release(sceneSrc);
	s_visibleReplaySceneName.clear();
	disconnectReplayMediaSignals();
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
	// OBS knows the exact final replay path, including any profile folder and
	// filename formatting. Prefer it over guessing from a configured directory.
	char *lastReplay = obs_frontend_get_last_replay();
	if (lastReplay && *lastReplay) {
		const QString exactPath = QString::fromUtf8(lastReplay);
		bfree(lastReplay);
		if (QFileInfo::exists(exactPath)) {
			s_lastReplayFile = exactPath;
			RsInstantReplay::playReplay(exactPath);
			return;
		}
	} else if (lastReplay) {
		bfree(lastReplay);
	}

	const QString folder = RsInstantReplay::replayFolderOverride();
	if (folder.isEmpty()) {
		if (attempt < 40)
			QTimer::singleShot(250, [attempt]() { playReplayAfterRename(attempt + 1); });
		else
			blog(LOG_WARNING, "[RS Instant Replay] OBS did not report a saved replay path");
		return;
	}

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
	if (!s_waitingForRequestedReplay)
		return;

	s_waitingForRequestedReplay = false;

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
	if (s_frontendCallbacksRegistered)
		return;

	obs_frontend_add_event_callback(onFrontendEvent, nullptr);

	// Register hotkey
	s_replayHotkey = obs_hotkey_register_frontend("rs_instant_replay_trigger", "RearSilver: Trigger Instant Replay",
						      replayHotkeyCallback, nullptr);
	s_frontendCallbacksRegistered = true;
}

void RsInstantReplay::shutdown()
{
	if (s_frontendCallbacksRegistered) {
		obs_frontend_remove_event_callback(onFrontendEvent, nullptr);
		s_frontendCallbacksRegistered = false;
	}

	if (s_replayHotkey != OBS_INVALID_HOTKEY_ID) {
		obs_hotkey_unregister(s_replayHotkey);
		s_replayHotkey = OBS_INVALID_HOTKEY_ID;
	}
	disconnectReplayMediaSignals();
	s_replayPlaybackGeneration.fetch_add(1);
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
	if (!obs_frontend_replay_buffer_active()) {
		obs_frontend_replay_buffer_start();
		s_replayBufferStartTime = QDateTime::currentMSecsSinceEpoch();
	}

	s_waitingForRequestedReplay = true;
	const qint64 requestTime = s_lastReplayRequestTime;
	QTimer::singleShot(45000, [requestTime]() {
		if (s_lastReplayRequestTime == requestTime)
			s_waitingForRequestedReplay = false;
	});

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
} // namespace hub_replay
