// ---------------------------------------------------------------
// src/enhancements/rs_browser_refresh.cpp
// OBS 32-compatible browser refresh tool.
// Refresh method: invoke obs-browser's native refresh procedure.
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
#include <QScrollArea>

#include <vector>
#include <unordered_set>

#include <obs-frontend-api.h>
#include <obs.h>

namespace {

static constexpr const char *kBuildMarker = "SAFE_BROWSER_RELOAD_v3";

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

struct SourceRef {
	obs_source_t *source = nullptr;
	QString sourceName;
};

static QString browser_url(obs_source_t *source)
{
	if (!source)
		return {};
	obs_data_t *settings = obs_source_get_settings(source);
	const QString url = settings ? QString::fromUtf8(obs_data_get_string(settings, "url")) : QString();
	if (settings)
		obs_data_release(settings);
	return url;
}

static bool requires_session_safe_reload(obs_source_t *source)
{
	const QString url = browser_url(source).toLower();
	// OBS exposes refreshnocache, not a normal cached reload. For services that
	// use anti-bot/session challenges, invoking it can replace a working widget
	// with a verification page that cannot be completed inside source preview.
	return url.contains("ko-fi.com") || url.contains("kofi.com") ||
	       url.contains("throne.com") || url.contains("twitch.tv");
}

// ---------------------------------------------------------------
// Collect browser sources recursively (handles groups + nested scenes).
// Each unique source receives one retained reference so it remains valid after
// the frontend scene list is released.
// ---------------------------------------------------------------
static void collect_browser_sources_recursive(obs_scene_t *scene, std::vector<SourceRef> &out,
					      std::unordered_set<obs_source_t *> &seen)
{
	if (!scene)
		return;

	struct CollectContext {
		std::vector<SourceRef> &out;
		std::unordered_set<obs_source_t *> &seen;
	} context{out, seen};

	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
			auto &context = *static_cast<CollectContext *>(param);
			auto &out = context.out;
			auto &seen = context.seen;
			if (!item)
				return true;

			obs_source_t *src = obs_sceneitem_get_source(item);
			if (!src)
				return true;

			const char *id = obs_source_get_unversioned_id(src);

			// Browser source item
			if (id && strcmp(id, "browser_source") == 0) {
				if (seen.insert(src).second) {
					SourceRef ref;
					ref.source = obs_source_get_ref(src);
					ref.sourceName = QString::fromUtf8(obs_source_get_name(src));
					out.push_back(std::move(ref));
				}
				return true;
			}

			// Group: recurse into the group's scene
			if (id && strcmp(id, "group") == 0) {
				if (obs_scene_t *grp = obs_group_from_source(src)) {
					collect_browser_sources_recursive(grp, out, seen);
				}
				return true;
			}

			// Nested scene source: recurse into that scene
			if (id && strcmp(id, "scene") == 0) {
				if (obs_scene_t *nested = obs_scene_from_source(src)) {
					collect_browser_sources_recursive(nested, out, seen);
				}
				return true;
			}

			return true;
		},
		&context);
}

// ---------------------------------------------------------------
// Current scene sources
// ---------------------------------------------------------------
static std::vector<SourceRef> get_current_scene_browser_sources()
{
	std::vector<SourceRef> out;
	std::unordered_set<obs_source_t *> seen;

	obs_source_t *sceneSrc = obs_frontend_get_current_scene();
	if (!sceneSrc)
		return out;

	if (obs_scene_t *scene = obs_scene_from_source(sceneSrc)) {
		collect_browser_sources_recursive(scene, out, seen);
	}

	obs_source_release(sceneSrc);
	return out;
}

// ---------------------------------------------------------------
// All scene sources
// Uses obs_frontend_get_scenes() and walks each scene.
// ---------------------------------------------------------------
static std::vector<SourceRef> get_all_scenes_browser_sources()
{
	std::vector<SourceRef> out;
	std::unordered_set<obs_source_t *> seen;

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
			collect_browser_sources_recursive(scene, out, seen);
		}
	}

	obs_frontend_source_list_free(&scenes);
	return out;
}

// ---------------------------------------------------------------
// Reload each unique browser source through obs-browser's native property
// callback. OBS's own Browser Toolbar uses the refreshnocache button this way;
// browser sources do not expose a proc-handler procedure named "refresh".
// ---------------------------------------------------------------
static int refresh_sources(std::vector<SourceRef> sources, QPlainTextEdit *log)
{
	if (sources.empty())
		return 0;

	int refreshed = 0;
	for (auto &ref : sources) {
		if (!ref.source)
			continue;

		if (requires_session_safe_reload(ref.source)) {
			append_log(log, "🛡️", QString("Skipped protected source '%1' (a forced no-cache reload can trigger account verification)")
						 .arg(ref.sourceName));
			obs_source_release(ref.source);
			ref.source = nullptr;
			continue;
		}

		obs_properties_t *properties = obs_source_properties(ref.source);
		obs_property_t *refresh = properties ? obs_properties_get(properties, "refreshnocache") : nullptr;
		const bool ok = refresh && obs_property_get_type(refresh) == OBS_PROPERTY_BUTTON;
		if (ok)
			obs_property_button_clicked(refresh, ref.source);
		if (properties)
			obs_properties_destroy(properties);
		append_log(log, ok ? "🔄" : "⚠️",
			   ok ? QString("Reloaded '%1'").arg(ref.sourceName)
			      : QString("'%1' did not accept a reload request").arg(ref.sourceName));
		if (ok)
			++refreshed;

		obs_source_release(ref.source);
		ref.source = nullptr;
	}

	return refreshed;
}

// ---------------------------------------------------------------
// Actions
// ---------------------------------------------------------------
static int refresh_current_scene(QPlainTextEdit *log)
{
	auto sources = get_current_scene_browser_sources();

	if (sources.empty()) {
		append_log(log, "ℹ️", "No browser sources found in current scene.");
		return 0;
	}

	const int refreshed = refresh_sources(std::move(sources), log);
	append_log(log, "📊",
		   QString("Refreshed %1 browser source%2 in current scene.")
			   .arg(refreshed)
			   .arg(refreshed == 1 ? "" : "s"));
	return refreshed;
}

static int refresh_all_scenes(QPlainTextEdit *log)
{
	auto sources = get_all_scenes_browser_sources();

	if (sources.empty()) {
		append_log(log, "ℹ️", "No browser sources found across scenes.");
		return 0;
	}

	const int refreshed = refresh_sources(std::move(sources), log);
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
