// ---------------------------------------------------------------
// src/enhancements/rs_auto_start.cpp
// Auto-Start Manager
// - Persistent program list + toggles (OBS config_t)
// - Manual: Launch Selected / Launch All / Close Selected / Close All
// - Auto: launch on OBS start (finished loading), close on OBS exit
// - UI: checkbox-per-row with row highlight selection
// ---------------------------------------------------------------

#include "rs_auto_start.hpp"
#include "../rs_main_dock.hpp"

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QFrame>
#include <QListWidget>
#include <QFileDialog>
#include <QCheckBox>
#include <QTime>
#include <QAbstractItemView>
#include <QScrollArea>
#include <QPointer>

#include <vector>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <obs-frontend-api.h>
#include <obs.h>
#include <util/config-file.h>

namespace {

// -----------------------------
// Config keys
// -----------------------------
static constexpr const char *kCfgSection = "RearSilver.StreamSuite.AutoStart";
static constexpr const char *kCfgPrograms = "Programs";     // newline-separated paths
static constexpr const char *kCfgAutoLaunch = "AutoLaunch"; // bool
static constexpr const char *kCfgAutoClose = "AutoClose";   // bool

// -----------------------------
// UI log helper
// -----------------------------
static void append_log(QPlainTextEdit *log, const QString &icon, const QString &text)
{
	if (!log)
		return;

	const QString ts = QTime::currentTime().toString("HH:mm:ss");
	log->appendPlainText(QString("[%1] %2 %3").arg(ts, icon, text));
}

// -----------------------------
// Config helpers (config_t*)
// -----------------------------
static config_t *get_global_cfg()
{
	return obs_frontend_get_global_config();
}

static QStringList cfg_load_programs()
{
	config_t *cfg = get_global_cfg();
	if (!cfg)
		return {};

	const char *raw = config_get_string(cfg, kCfgSection, kCfgPrograms);
	if (!raw || !*raw)
		return {};

	QString joined = QString::fromUtf8(raw);
	QStringList lines = joined.split('\n', Qt::SkipEmptyParts);
	for (QString &s : lines)
		s = s.trimmed();

	lines.removeAll(QString());
	return lines;
}

static bool cfg_load_bool(const char *key, bool defValue)
{
	config_t *cfg = get_global_cfg();
	if (!cfg)
		return defValue;
	return config_get_bool(cfg, kCfgSection, key);
}

static void cfg_save_programs(const QStringList &paths)
{
	config_t *cfg = get_global_cfg();
	if (!cfg)
		return;

	QStringList cleaned;
	cleaned.reserve(paths.size());
	for (const QString &p : paths) {
		QString t = p.trimmed();
		if (!t.isEmpty())
			cleaned.push_back(t);
	}

	const QString joined = cleaned.join('\n');
	config_set_string(cfg, kCfgSection, kCfgPrograms, joined.toUtf8().constData());
	config_save(cfg);
}

static void cfg_save_bool(const char *key, bool value)
{
	config_t *cfg = get_global_cfg();
	if (!cfg)
		return;

	config_set_bool(cfg, kCfgSection, key, value);
	config_save(cfg);
}

// -----------------------------
// Process tracking (what WE launched)
// -----------------------------
struct LaunchedProc {
	DWORD pid = 0;
	HANDLE hProcess = nullptr; // keep open so we can query + close
};

static std::unordered_map<std::wstring, LaunchedProc> g_launched; // key: exe path

static bool is_running(HANDLE hProcess)
{
	if (!hProcess)
		return false;

	DWORD code = 0;
	if (!GetExitCodeProcess(hProcess, &code))
		return false;

	return code == STILL_ACTIVE;
}

// -----------------------------
// Launch (CreateProcessW)
// -----------------------------
static bool launch_process(const QString &path, QString &errorOut, DWORD &pidOut, HANDLE &hProcOut)
{
	errorOut.clear();
	pidOut = 0;
	hProcOut = nullptr;

	std::wstring exe = path.toStdWString();

	STARTUPINFOW si{};
	si.cb = sizeof(si);

	PROCESS_INFORMATION pi{};
	BOOL ok = CreateProcessW(exe.c_str(), nullptr, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi);

	if (!ok) {
		DWORD err = GetLastError();
		errorOut = QString("Windows error %1").arg(err);
		return false;
	}

	// keep process handle for tracking; close thread handle
	pidOut = pi.dwProcessId;
	hProcOut = pi.hProcess;
	CloseHandle(pi.hThread);

	return true;
}

// -----------------------------
// Close (best-effort, safe)
// - Try WM_CLOSE for top-level windows of that PID
// - Do NOT force-kill
// -----------------------------
struct EnumWinCtx {
	DWORD pid = 0;
	int sent = 0;
};

static BOOL CALLBACK enum_windows_cb(HWND hwnd, LPARAM lp)
{
	auto *ctx = reinterpret_cast<EnumWinCtx *>(lp);
	if (!ctx)
		return TRUE;

	DWORD winPid = 0;
	GetWindowThreadProcessId(hwnd, &winPid);

	if (winPid != ctx->pid)
		return TRUE;

	// Only visible top-level windows usually respond properly
	if (!IsWindowVisible(hwnd))
		return TRUE;

	PostMessageW(hwnd, WM_CLOSE, 0, 0);
	ctx->sent++;
	return TRUE;
}

static int request_close_pid(DWORD pid)
{
	EnumWinCtx ctx;
	ctx.pid = pid;
	ctx.sent = 0;
	EnumWindows(enum_windows_cb, reinterpret_cast<LPARAM>(&ctx));
	return ctx.sent;
}

// -----------------------------
// List widget helpers
// -----------------------------
static QListWidgetItem *make_program_item(QListWidget *list, const QString &path, bool checked)
{
	auto *it = new QListWidgetItem(path, list);
	it->setFlags(it->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable | Qt::ItemIsEnabled);
	it->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
	return it;
}

static QStringList list_all_paths(QListWidget *list)
{
	QStringList out;
	if (!list)
		return out;

	for (int i = 0; i < list->count(); ++i) {
		auto *it = list->item(i);
		if (!it)
			continue;
		out.push_back(it->text().trimmed());
	}
	out.removeAll(QString());
	return out;
}

static QStringList list_checked_paths(QListWidget *list)
{
	QStringList out;
	if (!list)
		return out;

	for (int i = 0; i < list->count(); ++i) {
		auto *it = list->item(i);
		if (!it)
			continue;
		if (it->checkState() == Qt::Checked)
			out.push_back(it->text().trimmed());
	}
	out.removeAll(QString());
	return out;
}

// -----------------------------
// Action helpers
// -----------------------------
static void do_launch_paths(const QStringList &paths, QPlainTextEdit *log)
{
	if (paths.isEmpty()) {
		append_log(log, "⚠️", "No programs to launch.");
		return;
	}

	append_log(log, "▶️", "Launch requested.");

	for (const QString &path : paths) {
		const std::wstring key = path.toStdWString();

		// If we already launched it and it's still running, don't spawn duplicates.
		auto found = g_launched.find(key);
		if (found != g_launched.end() && is_running(found->second.hProcess)) {
			append_log(log, "ℹ️", QString("Already running (launched by Suite): %1").arg(path));
			continue;
		}

		QString err;
		DWORD pid = 0;
		HANDLE hProc = nullptr;

		if (launch_process(path, err, pid, hProc)) {
			// replace stored handle if existed
			if (found != g_launched.end()) {
				if (found->second.hProcess)
					CloseHandle(found->second.hProcess);
			}

			g_launched[key] = LaunchedProc{pid, hProc};
			append_log(log, "🚀", QString("Launched: %1").arg(path));
		} else {
			append_log(log, "❌", QString("Failed to launch %1 (%2)").arg(path, err));
		}
	}
}

static void do_close_paths(const QStringList &paths, QPlainTextEdit *log)
{
	if (paths.isEmpty()) {
		append_log(log, "⚠️", "No programs to close.");
		return;
	}

	append_log(log, "⏹", "Close requested.");

	for (const QString &path : paths) {
		const std::wstring key = path.toStdWString();
		auto it = g_launched.find(key);

		if (it == g_launched.end() || !it->second.hProcess) {
			append_log(log, "⚠️", QString("Not tracked (wasn't launched by Suite): %1").arg(path));
			continue;
		}

		LaunchedProc &p = it->second;

		if (!is_running(p.hProcess)) {
			append_log(log, "ℹ️", QString("Already closed: %1").arg(path));
			CloseHandle(p.hProcess);
			p.hProcess = nullptr;
			p.pid = 0;
			continue;
		}

		const int sent = request_close_pid(p.pid);
		if (sent > 0) {
			append_log(log, "📨", QString("Sent close request to %1 window(s): %2").arg(sent).arg(path));
		} else {
			append_log(log, "⚠️",
				   QString("No window to send WM_CLOSE (may be tray/console app): %1").arg(path));
		}

		// We DO NOT force-kill. User can close manually if needed.
	}
}

// -----------------------------
// OBS event callback (auto launch / auto close)
// -----------------------------
struct UiRefs {
	QPointer<QListWidget> list;
	QPointer<QCheckBox> chkAutoLaunch;
	QPointer<QCheckBox> chkAutoClose;
	QPointer<QPlainTextEdit> log;
};

static UiRefs g_ui;
static bool g_event_hooked = false;

static void on_obs_event(enum obs_frontend_event event, void *)
{
	// Only act if UI exists (otherwise just do nothing)
	const bool uiReady = (g_ui.list && g_ui.log);

if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
		const bool autoLaunch = uiReady && g_ui.chkAutoLaunch ? g_ui.chkAutoLaunch->isChecked()
								      : cfg_load_bool(kCfgAutoLaunch, false);

		if (autoLaunch) {
			if (uiReady)
				append_log(g_ui.log, "🚀", "OBS finished loading — auto-launch enabled.");

			do_launch_paths(uiReady ? list_all_paths(g_ui.list) : cfg_load_programs(),
					uiReady ? g_ui.log : nullptr);
		}
	}

if (event == OBS_FRONTEND_EVENT_EXIT) {
		const bool autoClose = uiReady && g_ui.chkAutoClose ? g_ui.chkAutoClose->isChecked()
								    : cfg_load_bool(kCfgAutoClose, false);

		if (autoClose) {
			if (uiReady)
				append_log(g_ui.log, "⏹", "OBS exiting — auto-close enabled.");

			do_close_paths(uiReady ? list_all_paths(g_ui.list) : cfg_load_programs(),
				       uiReady ? g_ui.log : nullptr);
		}
	}
}

} // namespace

// ---------------------------------------------------------------
// Ensure OBS event hook is registered only once
// ---------------------------------------------------------------

void RsAutoStart::ensureObsEventHook()
{
	if (g_event_hooked)
		return;

	obs_frontend_add_event_callback(on_obs_event, nullptr);
	g_event_hooked = true;

	// -------------------------------------------------
	// 🔁 SAFETY NET:
	// If OBS is already fully initialised (main window exists),
	// manually trigger auto-launch once.
	// -------------------------------------------------
	if (obs_frontend_get_main_window() != nullptr) {
		const bool autoLaunch = cfg_load_bool(kCfgAutoLaunch, false);
		if (autoLaunch) {
			do_launch_paths(cfg_load_programs(), nullptr);
		}
	}
}

void RsAutoStart::shutdown()
{
	if (g_event_hooked) {
		obs_frontend_remove_event_callback(on_obs_event, nullptr);
		g_event_hooked = false;
	}

	g_ui = {};
}

bool RsAutoStart::containsProgram(const QString &path)
{
	for (const QString &program : cfg_load_programs()) if (program.compare(path, Qt::CaseInsensitive) == 0) return true;
	return false;
}

void RsAutoStart::addProgram(const QString &path)
{
	QStringList programs=cfg_load_programs();
	if (!containsProgram(path)) {
		programs.append(path); cfg_save_programs(programs);
		if (g_ui.list) make_program_item(g_ui.list, path, true);
	}
}

void RsAutoStart::removeProgram(const QString &path)
{
	QStringList programs=cfg_load_programs();
	programs.erase(std::remove_if(programs.begin(),programs.end(),[&](const QString &p){return p.compare(path,Qt::CaseInsensitive)==0;}),programs.end());
	cfg_save_programs(programs);
	if (g_ui.list) for (int i=g_ui.list->count()-1;i>=0;--i)
		if (g_ui.list->item(i)->text().compare(path,Qt::CaseInsensitive)==0) delete g_ui.list->takeItem(i);
}

// ---------------------------------------------------------------
// UI factory
// ---------------------------------------------------------------
QWidget *RsAutoStart::createPage(RsMainDock *, QWidget *parent)
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
	root->setContentsMargins(8, 8, 8, 8);
	root->setSpacing(8);

	// Header
	auto *title = new QLabel("Auto-Start Manager", page);
	QFont tf = title->font();
	tf.setBold(true);
	tf.setPointSize(tf.pointSize() + 1);
	title->setFont(tf);
	root->addWidget(title);
	title->setMaximumHeight(title->sizeHint().height());


	auto *desc = new QLabel("Launch and optionally close external programs alongside OBS.\n"
				"Useful for bots, overlays, controllers, and helper tools.",
				page);
	desc->setWordWrap(true);
	desc->setStyleSheet("opacity: 0.8;");
	root->addWidget(desc);

	// Program list
	auto *list = new QListWidget(page);
	list->setSelectionMode(QAbstractItemView::SingleSelection);
	list->setAlternatingRowColors(true);
	root->addWidget(list, 1);

	// Row 1: Add/Remove
	auto *row1 = new QHBoxLayout();
	auto *btnAdd = new QPushButton("Add program...", page);
	auto *btnRemove = new QPushButton("Remove selected", page);
	row1->addWidget(btnAdd);
	row1->addWidget(btnRemove);
	root->addLayout(row1);

	// Row 2: Launch buttons
	auto *row2 = new QHBoxLayout();
	auto *btnLaunchSel = new QPushButton("Launch selected", page);
	auto *btnLaunchAll = new QPushButton("Launch all", page);
	btnLaunchSel->setObjectName("rs-primary-button");
	btnLaunchAll->setObjectName("rs-primary-button");
	row2->addWidget(btnLaunchSel);
	row2->addWidget(btnLaunchAll);
	root->addLayout(row2);

	// Row 3: Close buttons
	auto *row3 = new QHBoxLayout();
	auto *btnCloseSel = new QPushButton("Close selected", page);
	auto *btnCloseAll = new QPushButton("Close all", page);
	btnCloseSel->setObjectName("rs-secondary-button");
	btnCloseAll->setObjectName("rs-secondary-button");
	row3->addWidget(btnCloseSel);
	row3->addWidget(btnCloseAll);
	root->addLayout(row3);

	// Options (auto)
	auto *chkAutoLaunch = new QCheckBox("Automatically launch programs when OBS starts", page);
	auto *chkAutoClose = new QCheckBox("Automatically close programs when OBS exits", page);
	root->addWidget(chkAutoLaunch);
	root->addWidget(chkAutoClose);

	// Divider
	auto *div = new QFrame(page);
	div->setFrameShape(QFrame::HLine);
	root->addWidget(div);

	// Log
	auto *log = new QPlainTextEdit(page);
	log->setReadOnly(true);
	log->setMinimumHeight(140);
	root->addWidget(log);

	// -----------------------------
	// Load persisted state
	// -----------------------------
	{
		const QStringList programs = cfg_load_programs();
		for (const QString &p : programs) {
			make_program_item(list, p, true); // default checked
		}

		// config_get_bool returns false if not set, so we emulate defaults:
		const bool autoLaunch = cfg_load_bool(kCfgAutoLaunch, false);
		const bool autoClose = cfg_load_bool(kCfgAutoClose, false);

		chkAutoLaunch->setChecked(autoLaunch);
		chkAutoClose->setChecked(autoClose);

		append_log(log, "💾", "Auto-start configuration loaded.");
		append_log(log, "ℹ️", "Auto-Start Manager ready.");
	}

	// -----------------------------
	// Ensure: checkbox click selects/highlights row
	// and row click toggles checkbox (nice UX)
	// -----------------------------
	QObject::connect(list, &QListWidget::itemChanged, page, [=](QListWidgetItem *item) {
		// when a checkbox is toggled, also select the row
		if (!item)
			return;
		list->setCurrentItem(item);
	});

	// -----------------------------
	// Persist helpers
	// -----------------------------
	auto save_all = [=]() {
		cfg_save_programs(list_all_paths(list));
		cfg_save_bool(kCfgAutoLaunch, chkAutoLaunch->isChecked());
		cfg_save_bool(kCfgAutoClose, chkAutoClose->isChecked());
	};

	// Persist toggles
	QObject::connect(chkAutoLaunch, &QCheckBox::toggled, page, [=](bool) { save_all(); });
	QObject::connect(chkAutoClose, &QCheckBox::toggled, page, [=](bool) { save_all(); });

	// -----------------------------
	// Wiring: Add / Remove
	// -----------------------------
	QObject::connect(btnAdd, &QPushButton::clicked, page, [=]() {
		QString path = QFileDialog::getOpenFileName(page, "Select program", QString(), "Executables (*.exe)");
		if (path.isEmpty())
			return;

		// de-dupe
		for (int i = 0; i < list->count(); ++i) {
			if (list->item(i)->text() == path) {
				append_log(log, "ℹ️", QString("Already in list: %1").arg(path));
				return;
			}
		}

		make_program_item(list, path, true);
		append_log(log, "➕", QString("Added program: %1").arg(path));
		save_all();
	});

	QObject::connect(btnRemove, &QPushButton::clicked, page, [=]() {
		auto *item = list->currentItem();
		if (!item)
			return;

		append_log(log, "➖", QString("Removed program: %1").arg(item->text()));
		delete item;
		save_all();
	});

	// -----------------------------
	// Wiring: Launch / Close
	// -----------------------------
	QObject::connect(btnLaunchAll, &QPushButton::clicked, page,
			 [=]() { do_launch_paths(list_all_paths(list), log); });

	QObject::connect(btnLaunchSel, &QPushButton::clicked, page,
			 [=]() { do_launch_paths(list_checked_paths(list), log); });

	QObject::connect(btnCloseAll, &QPushButton::clicked, page,
			 [=]() { do_close_paths(list_all_paths(list), log); });

	QObject::connect(btnCloseSel, &QPushButton::clicked, page,
			 [=]() { do_close_paths(list_checked_paths(list), log); });

	// -----------------------------
	// Hook OBS events once
	// -----------------------------
	g_ui.list = list;
	g_ui.chkAutoLaunch = chkAutoLaunch;
	g_ui.chkAutoClose = chkAutoClose;
	g_ui.log = log;

return scroll;
}

