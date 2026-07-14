// ---------------------------------------------------------------
// src/enhancements/rs_browser_refresh.cpp
// OBS 32–compatible browser refresh tool (SAFE)
// Refresh method: scene item visibility toggle (NO settings/URL writes)
// ---------------------------------------------------------------

#include "rs_browser_refresh.hpp"
#include "../rs_main_dock.hpp"

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QFrame>
#include <QTime>
#include <QTimer>
#include <QScrollArea>

#include <vector>
#include <unordered_set>
#include <cstdint>

#include <obs-frontend-api.h>
#include <obs.h>

namespace {

static constexpr const char *kBuildMarker = "SAFE_VIS_TOGGLE_v1";

// ---------------------------------------------------------------
// Logging helper
// ---------------------------------------------------------------
void append_log(QPlainTextEdit *log, const QString &icon, const QString &text)
{
	if (!log)
		return;

	const QString ts = QTime::currentTime().toString("HH:mm:ss");
	log->appendPlainText(QString("[%1] %2 %3").arg(ts, icon, text));
}

// ---------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------
static inline bool is_browser_source(obs_source_t *src)
{
	if (!src)
		return false;
	const char *id = obs_source_get_unversioned_id(src);
	return (id && strcmp(id, "browser_source") == 0);
}

struct ItemRef {
	obs_sceneitem_t *item = nullptr;
	bool wasVisible = false;
	QString sourceName;
};

// ---------------------------------------------------------------
// Collect browser scene-items recursively (handles groups + nested scenes)
// NOTE: We collect the *scene items*, not just sources, so we can toggle
// visibility without touching source settings.
// ---------------------------------------------------------------
static void collect_browser_items_recursive(obs_scene_t *scene, std::vector<ItemRef> &out)
{
	if (!scene)
		return;

	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
			auto *out = static_cast<std::vector<ItemRef> *>(param);
			if (!item)
				return true;

			obs_source_t *src = obs_sceneitem_get_source(item);
			if (!src)
				return true;

			const char *id = obs_source_get_unversioned_id(src);

			// Browser source item
			if (id && strcmp(id, "browser_source") == 0) {
				ItemRef r;
				r.item = item;
				r.wasVisible = obs_sceneitem_visible(item);
				r.sourceName = obs_source_get_name(src);
				out->push_back(r);
				return true;
			}

			// Group: recurse into the group's scene
			if (id && strcmp(id, "group") == 0) {
				if (obs_scene_t *grp = obs_group_from_source(src)) {
					collect_browser_items_recursive(grp, *out);
				}
				return true;
			}

			// Nested scene source: recurse into that scene
			if (id && strcmp(id, "scene") == 0) {
				if (obs_scene_t *nested = obs_scene_from_source(src)) {
					collect_browser_items_recursive(nested, *out);
				}
				return true;
			}

			return true;
		},
		&out);
}

// ---------------------------------------------------------------
// Current Scene items
// ---------------------------------------------------------------
static std::vector<ItemRef> get_current_scene_browser_items()
{
	std::vector<ItemRef> out;

	obs_source_t *sceneSrc = obs_frontend_get_current_scene();
	if (!sceneSrc)
		return out;

	if (obs_scene_t *scene = obs_scene_from_source(sceneSrc)) {
		collect_browser_items_recursive(scene, out);
	}

	obs_source_release(sceneSrc);
	return out;
}

// ---------------------------------------------------------------
// All Scenes items
// Uses obs_frontend_get_scenes() and walks each scene.
// ---------------------------------------------------------------
static std::vector<ItemRef> get_all_scenes_browser_items()
{
	std::vector<ItemRef> out;

	obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);

	// OBS frontend api list layout (your version):
	// scenes.sources.num
	// scenes.sources.array[i]
	const size_t n = scenes.sources.num;

	for (size_t i = 0; i < n; i++) {
		obs_source_t *scene_src = scenes.sources.array[i];
		if (!scene_src)
			continue;

		if (obs_scene_t *scene = obs_scene_from_source(scene_src)) {
			collect_browser_items_recursive(scene, out);
		}
	}

	obs_frontend_source_list_free(&scenes);
	return out;
}

// ---------------------------------------------------------------
// Refresh: toggle visible items off then back on next tick
// Dedupe by sceneitem pointer to avoid double-toggling.
// ---------------------------------------------------------------
static int refresh_items(std::vector<ItemRef> items, QPlainTextEdit *log)
{
	if (items.empty())
		return 0;

	std::unordered_set<std::uintptr_t> seen;
	std::vector<ItemRef> toggled;
	toggled.reserve(items.size());

	// Step 1: hide anything that is currently visible
	for (auto &it : items) {
		if (!it.item)
			continue;

		const std::uintptr_t key = (std::uintptr_t)it.item;
		if (seen.find(key) != seen.end())
			continue;
		seen.insert(key);

		if (!it.wasVisible) {
			// Not visible -> nothing to toggle
			continue;
		}

		obs_sceneitem_set_visible(it.item, false);
		toggled.push_back(it);
	}

	if (toggled.empty()) {
		append_log(log, "ℹ️", "No visible browser sources to refresh (nothing toggled).");
		return 0;
	}

	// Step 2: restore on next tick (a little longer than 1 frame for safety)
	QTimer::singleShot(50, [toggled, log]() mutable {
		for (auto &it : toggled) {
			if (it.item) {
				obs_sceneitem_set_visible(it.item, true);
				append_log(log, "🔄", QString("Refreshed '%1'").arg(it.sourceName));
			}
		}
	});

	return (int)toggled.size();
}

// ---------------------------------------------------------------
// Actions
// ---------------------------------------------------------------
static int refresh_current_scene(QPlainTextEdit *log)
{
	auto items = get_current_scene_browser_items();

	if (items.empty()) {
		append_log(log, "ℹ️", "No browser sources found in current scene.");
		return 0;
	}

	const int refreshed = refresh_items(std::move(items), log);
	append_log(log, "📊",
		   QString("Refreshed %1 browser source%2 in current scene.")
			   .arg(refreshed)
			   .arg(refreshed == 1 ? "" : "s"));
	return refreshed;
}

static int refresh_all_scenes(QPlainTextEdit *log)
{
	auto items = get_all_scenes_browser_items();

	if (items.empty()) {
		append_log(log, "ℹ️", "No browser sources found across scenes.");
		return 0;
	}

	const int refreshed = refresh_items(std::move(items), log);
	append_log(log, "📊",
		   QString("Refreshed %1 browser source%2 across all scenes.")
			   .arg(refreshed)
			   .arg(refreshed == 1 ? "" : "s"));
	return refreshed;
}

} // namespace

// ---------------------------------------------------------------
// UI factory (keeps your buttons/logs)
// ---------------------------------------------------------------
QWidget *RsBrowserRefresh::createPage(RsMainDock *, QWidget *parent)
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
	// Tight top, normal sides & bottom
	root->setContentsMargins(8, 2, 8, 8);
	root->setSpacing(8);


	// Header
	auto *title = new QLabel("Browser Refresh", page);
	QFont f = title->font();
	f.setBold(true);
	f.setPointSize(f.pointSize() + 1);
	title->setFont(f);
	root->addWidget(title);
	title->setMaximumHeight(title->sizeHint().height());
	// Description + buttons
	// Explainer text (row 1)
	auto *desc = new QLabel(
		"Reload browser sources when they freeze or desync.<br><br>"
		"<b>Current Scene</b>: refresh browser sources in the active scene (including nested/groups)<br>"
		"<b>All Scenes</b>: refresh browser sources across ALL scenes.",
		page);

	desc->setWordWrap(true);

	// ✅ Treat as “content-sized”, not expandable
	desc->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

	root->addWidget(desc);


	// Buttons (full width, rows 2 & 3)
	auto *btnCurrent = new QPushButton("Refresh current scene", page);
	auto *btnAll = new QPushButton("Refresh all scenes", page);

	btnCurrent->setObjectName("rs-primary-button");
	btnAll->setObjectName("rs-secondary-button");

	btnCurrent->setMinimumHeight(36);
	btnAll->setMinimumHeight(36);

	root->addWidget(btnCurrent);
	root->addWidget(btnAll);

	// Divider
	auto *divider = new QFrame(page);
	divider->setFrameShape(QFrame::HLine);
	root->addWidget(divider);

	// Log
	auto *log = new QPlainTextEdit(page);
	log->setReadOnly(true);

	// ✅ Make the log a fixed “panel”, not a stretchy sponge
	log->setFixedHeight(200);
	log->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

	root->addWidget(log);


	auto *clearRow = new QHBoxLayout();
	auto *btnClear = new QPushButton("Clear log", page);
	clearRow->addWidget(btnClear);
	clearRow->addStretch();
	root->addLayout(clearRow);

	// Connections
	QObject::connect(btnClear, &QPushButton::clicked, log, [log]() { log->clear(); });

	QObject::connect(btnCurrent, &QPushButton::clicked, page, [log]() {
		append_log(log, "🔁", QString("Refreshing browser sources in CURRENT scene... (%1)").arg(kBuildMarker));
		refresh_current_scene(log);
	});

	QObject::connect(btnAll, &QPushButton::clicked, page, [log]() {
		append_log(log, "🌐",
			   QString("Refreshing browser sources across ALL scenes... (%1)").arg(kBuildMarker));
		refresh_all_scenes(log);
	});

	append_log(log, "ℹ️",
		   QString("Ready. Use the buttons above to refresh browser sources. (%1)").arg(kBuildMarker));

return scroll;
}
