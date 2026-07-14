#include "rs_main_dock.hpp"

#include <QComboBox>
#include <QSettings>
#include <QString>
#include <QWidget>

/* ---------------------------
 * Theme Selector Slot
 * -------------------------- */
void RsMainDock::onThemeChanged(int index)
{
	if (!m_themeCombo)
		return;

	m_currentTheme = m_themeCombo->itemData(index).toString();

	saveSettings();
	applyTheme();
}

/* ---------------------------
 * APPLY THEME (THEME-AWARE CONTROLS)
 * -------------------------- */
void RsMainDock::applyTheme()
{
	// --------------------------------------------------
	// HARD RESET PASS (prevents "sticky" Qt stylesheet caching)
	// --------------------------------------------------
	// We clear per-widget stylesheet side-effects (especially WA_StyledBackground)
	// before either returning to Default or applying a new theme.
	setUpdatesEnabled(false);

	// Clear this widget first
	setStyleSheet("");
	setAttribute(Qt::WA_StyledBackground, false);
	setAutoFillBackground(false);
	style()->unpolish(this);
	style()->polish(this);

	// Clear ALL child widgets
	for (QWidget *w : findChildren<QWidget *>(QString(), Qt::FindChildrenRecursively)) {
		if (!w)
			continue;

		w->setStyleSheet("");
		w->setAttribute(Qt::WA_StyledBackground, false);
		w->setAutoFillBackground(false);

		w->style()->unpolish(w);
		w->style()->polish(w);
		w->update();
	}

	update();
	setUpdatesEnabled(true);

	// TRUE OBS DEFAULT: no stylesheet at all
	if (m_currentTheme == "default") {
		// Nothing else to apply. We want OBS/OS styling only.
		return;
	}

	QString css;

	/* =================================================
	 * Base style shared by ALL themes
	 * (Professional polish: typography, focus, lists, logs, buttons)
	 * ================================================= */
	css += R"(

/* ---------- Global app defaults ---------- */
QWidget {
	font-size: 12px;
}

/* Make focus visible (keyboard + accessibility) */
*:focus {
	outline: none;
}
QAbstractButton:focus,
QComboBox:focus,
QListWidget:focus,
QPlainTextEdit:focus,
QLineEdit:focus {
    border: 1px solid rgba(255,255,255,0.25);
}

/* ---------- Cards ---------- */
#rs-content-card {
	border-radius: 10px;
}

#rs-card {
	border-radius: 10px;
	padding: 8px;
}

/* ---------- Control Buttons (existing) ---------- */
QPushButton#ControlButton {
	min-height: 32px;
	border-radius: 8px;
	padding: 6px 10px;
	letter-spacing: 0.1px;
}

QPushButton#ControlButton[active="true"] {
	background-color: #9146ff;
	color: #ffffff;
	border: 1px solid #b388ff;
}

/* ---------- NEW: Primary / Secondary action buttons ---------- */
QPushButton#rs-primary-button,
QPushButton#rs-secondary-button {
	min-height: 32px;
	border-radius: 8px;
	padding: 6px 10px;
	font-weight: 600;
}

QPushButton#rs-primary-button:disabled,
QPushButton#rs-secondary-button:disabled,
QPushButton#ControlButton:disabled {
	opacity: 0.55;
}

/* ---------- Inputs ---------- */
QLineEdit, QComboBox {
	min-height: 30px;
	border-radius: 8px;
	padding: 4px 8px;
}

/* ---------- Global Divider ---------- */
QFrame#rs-divider {
	min-height: 1px;
	max-height: 1px;
	border: none;
	margin: 0;
	background-color: transparent;
}

/* ---------- Micro Section Labels ---------- */
QLabel#rs-section-label {
    font-size: 10px;
    font-weight: 600;
    letter-spacing: 1px;
    opacity: 0.65;
}


/* ---------- List polish (Auto-Start etc.) ---------- */
QListWidget {
	border-radius: 10px;
	padding: 2px;
}

QListWidget::item {
	padding: 8px 8px;
	border-radius: 8px;
	margin: 2px 2px;
}

QListWidget::item:selected {
	color: #ffffff;
}

/* ---------- Checkbox affordance ---------- */
QCheckBox {
	spacing: 8px;
}

QCheckBox::indicator {
	width: 16px;
	height: 16px;
	border-radius: 4px;
}

/* ---------- Logs (Browser Refresh / Auto-Start) ---------- */
QPlainTextEdit {
	border-radius: 10px;
	padding: 8px;
	font-family: Consolas, "JetBrains Mono", "Cascadia Mono", monospace;
	font-size: 11px;
}

/* ---------- Scrollbars (subtle, premium) ---------- */
QScrollBar:vertical {
	width: 10px;
	margin: 2px;
	background: transparent;
}
QScrollBar::handle:vertical {
	border-radius: 5px;
	min-height: 24px;
	background: rgba(255,255,255,0.18);
}
QScrollBar::handle:vertical:hover {
	background: rgba(255,255,255,0.26);
}
QScrollBar::add-line:vertical,
QScrollBar::sub-line:vertical {
	height: 0px;
}
QScrollBar::add-page:vertical,
QScrollBar::sub-page:vertical {
	background: transparent;
}

QScrollBar:horizontal {
	height: 10px;
	margin: 2px;
	background: transparent;
}
QScrollBar::handle:horizontal {
	border-radius: 5px;
	min-width: 24px;
	background: rgba(255,255,255,0.18);
}
QScrollBar::handle:horizontal:hover {
	background: rgba(255,255,255,0.26);
}
QScrollBar::add-line:horizontal,
QScrollBar::sub-line:horizontal {
	width: 0px;
}
QScrollBar::add-page:horizontal,
QScrollBar::sub-page:horizontal {
	background: transparent;
}

)";


/* -------------------------------------------------
 * Safety Lock icon must ONLY show when locked
 * ------------------------------------------------- */
	css += R"(

	/* Default: NO lock icon */
	QPushButton #ControlButton::before, QPushButton #ControlButton::after
	{
	content:
		none;
	}

	/* Locked state: allow icon */
	QPushButton #ControlButton[safetyLocked = "true"] ::before,
		QPushButton #ControlButton[safetyLocked = "true"] ::after
	{
	content:
		""; /* or your existing icon content */
	}
)";

	/* =================================================
	 * Safety Lock – CORE AFFORDANCE (Qt-safe)
	 * ================================================= */
	css += R"(
QPushButton#ControlButton[safetyLocked="true"] {
	cursor: not-allowed;
	opacity: 0.78;
}

QPushButton#ControlButton[safetyLocked="true"]:hover {
	opacity: 0.88;
}
)";

	/* ==============================
	   SAFETY LOCK HOLD PROGRESS (Qt-safe)
	   ============================== */
	css += R"(

QPushButton#ControlButton[holding="true"] {
	color: #ffffff;
	border-color: #ff5555;

	background-color: qlineargradient(
		x1:0, y1:0, x2:1, y2:0,
		stop:0   rgba(255, 60, 60, 0.90),
		stop:0.5 rgba(255, 60, 60, 0.90),
		stop:1   rgba(255, 60, 60, 0.35)
	);
}

)";

	/* Hold Overlay */
	css += R"(
QWidget#HoldOverlay {
	background-color: rgba(255, 60, 60, 0.55);
	border-radius: 8px;
}
)";

	/* ==============================
	   HOLD COMPLETION PULSE (BASE)
	   ============================== */
	css += R"(

QPushButton#ControlButton[pulse="true"] {
	border-width: 3px;
	animation: rs-pulse-confirm 300ms ease-out;
}

@keyframes rs-pulse-confirm {
	0% {
		box-shadow: 0 0 0 0 rgba(255, 80, 80, 0.9);
	}
	100% {
		box-shadow: 0 0 0 10px rgba(255, 80, 80, 0.0);
	}
}

)";

	/* ===============================
   PRO: Calm Studio Theme
   =============================== */
	if (m_currentTheme == "pro_calm")
	{
		css += R"(


QWidget {
	background-color: #0e1015;
	color: #d8dbe2;
}

/* --- Cards --- */
#rs-card,
#rs-content-card {
	background-color: rgba(255,255,255,0.025);
	border-radius: 12px;
	border: none;
}

/* --- Section labels --- */
QLabel#rs-section-label {
	font-size: 10px;
	letter-spacing: 1px;
	opacity: 0.6;
}

/* --- Sidebar --- */
#SidebarButton {
	background-color: transparent;
	border: none;
	padding: 8px 10px;
	border-radius: 10px;
	color: rgba(255,255,255,0.75);
}

#SidebarButton:hover {
	background-color: rgba(255,255,255,0.04);
}

#SidebarButton[active="true"] {
	background-color: rgba(255,255,255,0.08);
	color: #ffffff;
}

/* --- Inputs --- */
QLineEdit, QComboBox {
	background-color: rgba(255,255,255,0.04);
	border: 1px solid rgba(255,255,255,0.06);
	color: #e6e8ee;
}

QLineEdit:hover, QComboBox:hover {
	border-color: rgba(255,255,255,0.12);
}

QLineEdit:focus, QComboBox:focus {
	border-color: rgba(145,70,255,0.55);
}

/* --- Buttons --- */
QPushButton#rs-primary-button {
	background-color: rgba(145,70,255,0.75);
	border: none;
	color: #ffffff;
}

QPushButton#rs-primary-button:hover {
	background-color: rgba(145,70,255,0.95);
}

QPushButton#rs-secondary-button {
	background-color: rgba(255,255,255,0.04);
	border: none;
	color: rgba(255,255,255,0.85);
}

QPushButton#rs-secondary-button:hover {
	background-color: rgba(255,255,255,0.08);
}

/* --- Control Buttons --- */
QPushButton#ControlButton {
	background-color: rgba(255,255,255,0.035);
	border: none;
	color: rgba(255,255,255,0.85);
}

QPushButton#ControlButton:hover {
	background-color: rgba(255,255,255,0.08);
}

/* --- Lists --- */
QListWidget {
	background-color: rgba(0,0,0,0.25);
	border: none;
}

QListWidget::item:hover {
	background-color: rgba(255,255,255,0.05);
}

QListWidget::item:selected {
	background-color: rgba(145,70,255,0.22);
}

/* --- Scrollbars (extra subtle) --- */
QScrollBar::handle {
	background: rgba(255,255,255,0.15);
}

QScrollBar::handle:hover {
	background: rgba(255,255,255,0.25);
}

/* --- Logs --- */
QPlainTextEdit {
	background-color: rgba(0,0,0,0.35);
	border: none;
	color: #dcdfea;
}

)";
	}

/* ===============================
   PRO: Calm Studio Theme
   =============================== */
	else if (m_currentTheme == "pro_night") {
		css += R"(

QWidget {
    background-color: #0e1117;
    color: #d6d9e0;
}

/* Cards */
#rs-card, #rs-content-card {
    background-color: #151a22;
    border: none;
    border-radius: 12px;
    box-shadow: 0 6px 22px rgba(0,0,0,0.45);
}

/* Section labels */
QLabel#rs-section-label {
    color: #9aa1ad;
    opacity: 0.75;
}

/* Inputs */
QLineEdit, QComboBox {
    background-color: #1a1f29;
    border: 1px solid rgba(255,255,255,0.06);
    color: #d6d9e0;
}
QLineEdit:hover, QComboBox:hover {
    border-color: rgba(111,108,255,0.45);
}

/* Buttons */
QPushButton#rs-primary-button {
    background-color: #6f6cff;
    color: #ffffff;
    border-radius: 10px;
}
QPushButton#rs-primary-button:hover {
    background-color: #817eff;
}

QPushButton#rs-secondary-button {
    background-color: rgba(255,255,255,0.04);
    color: #b8bdc9;
    border: 1px solid rgba(255,255,255,0.06);
}
QPushButton#rs-secondary-button:hover {
    background-color: rgba(255,255,255,0.08);
}

/* Lists */
QListWidget {
    background-color: #121620;
    border: none;
}
QListWidget::item:selected {
    background-color: rgba(111,108,255,0.22);
}

/* Logs */
QPlainTextEdit {
    background-color: #10141d;
    border: none;
    color: #cfd3dc;
}

/* Scrollbars */
QScrollBar::handle {
    background: rgba(255,255,255,0.10);
}
QScrollBar::handle:hover {
    background: rgba(255,255,255,0.22);
}

)";
	}


	/* =================================================
	 * THEME: Twitch Dark (rich, premium, native-feeling)
	 * ================================================= */
	 else if (m_currentTheme == "twitch_dark") {
		css += R"(

QWidget {
	background-color: #0b0b10;
	color: #e6e4ea;
}

/* Cards */
#rs-content-card {
	background-color: rgba(255,255,255,0.03);
	border-radius: 10px;
border: 1px solid rgba(255,255,255,0.04);
box-shadow: 0 6px 20px rgba(0,0,0,0.35);
}

#rs-card {
	background-color: #14141a;
	border-radius: 10px;
	border: 1px solid rgba(255,255,255,0.07);
}

/* Sidebar */
#SidebarButton {
	background-color: rgba(255,255,255,0.04);
	color: #ffffff;
	border-radius: 8px;
	padding: 6px 8px;
	border: 1px solid rgba(255,255,255,0.06);
}

#SidebarButton[active="true"] {
	background-color: rgba(145,70,255,0.95);
	color: #ffffff;
	border: 1px solid rgba(179,136,255,0.95);
}

#SidebarButton:hover {
	background-color: rgba(145,70,255,0.22);
}

/* Control buttons */
QPushButton#ControlButton {
background-color: rgba(255,255,255,0.045);
	color: #f5f5f7;
	border: 1px solid rgba(255,255,255,0.10);
}

QPushButton#ControlButton:hover {
background-color: rgba(255,255,255,0.075);
}

/* Primary/Secondary */
QPushButton#rs-primary-button {
	background-color: #9146ff;
	color: #ffffff;
	    border: none;
    box-shadow: 0 4px 12px rgba(145,70,255,0.45);
}
QPushButton#rs-primary-button:hover {
	background-color: #a970ff;
}

QPushButton#rs-secondary-button {
	background-color: rgba(255,255,255,0.05);
	color: #f0eef4;
	border: 1px solid rgba(255,255,255,0.10);
}
QPushButton#rs-secondary-button:hover {
	background-color: rgba(255,255,255,0.09);
}

/* Divider */
QFrame#rs-divider {
	background-color: rgba(255,255,255,0.10);
}

/* Inputs */
QLineEdit, QComboBox {
	background-color: rgba(255,255,255,0.05);
	border: 1px solid rgba(255,255,255,0.10);
	color: #f5f5f7;
}
QLineEdit:hover, QComboBox:hover {
	border: 1px solid rgba(179,136,255,0.45);
}

/* Lists */
QListWidget {
	background-color: rgba(0,0,0,0.22);
	border: 1px solid rgba(255,255,255,0.10);
}
QListWidget::item:hover {
	background-color: rgba(255,255,255,0.06);
}
QListWidget::item:selected {
	background-color: rgba(145,70,255,0.28);
}

/* Checkbox */
QCheckBox::indicator:unchecked {
	border: 1px solid rgba(255,255,255,0.45);
	background-color: rgba(255,255,255,0.03);
}
QCheckBox::indicator:checked {
	background-color: #9146ff;
	border: 1px solid rgba(179,136,255,0.95);
}

/* Logs */
QPlainTextEdit {
	background-color: rgba(0,0,0,0.35);
	border: 1px solid rgba(255,255,255,0.10);
	color: #f0eef4;
}

/* Safety Lock – Twitch */
QPushButton#ControlButton[safetyLocked="true"] {
	border-color: rgba(179,136,255,0.95);
}

QPushButton#ControlButton[holding="true"] {
	border-color: rgba(179,136,255,0.95);
	background-color: qlineargradient(
		x1:0, y1:0, x2:1, y2:0,
		stop:0   rgba(145, 70, 255, 0.95),
		stop:1   rgba(145, 70, 255, 0.35)
	);
}

QPushButton#ControlButton[pulse="true"] {
	background-color: #b388ff;
	color: #ffffff;
}

@keyframes rs-pulse-confirm {
	0% {
		box-shadow: 0 0 0 0 rgba(145, 70, 255, 0.9);
	}
	100% {
		box-shadow: 0 0 0 10px rgba(145, 70, 255, 0.0);
	}
}

)";
	}

	/* =================================================
	 * THEME: High Contrast (kept strict, still polished)
	 * ================================================= */
	else if (m_currentTheme == "high_contrast") {
		css += R"(

QWidget {
	background-color: #000000;
	color: #ffffff;
}

#SidebarButton {
	background-color: #000000;
	color: #ffffff;
	border: 2px solid #ffffff;
	border-radius: 6px;
	padding: 6px 8px;
}

#SidebarButton[active="true"] {
	background-color: #ffffff;
	color: #000000;
}

#rs-content-card, #rs-card {
    background: qlineargradient(
        x1:0, y1:0, x2:0, y2:1,
        stop:0 rgba(255,255,255,0.035),
        stop:1 rgba(255,255,255,0.02)
	border: 2px solid #ffffff;
	border-radius: 10px;
}

QPushButton#ControlButton,
QPushButton#rs-primary-button,
QPushButton#rs-secondary-button {
	background-color: #000000;
	color: #ffffff;
	border: 2px solid #ffffff;
}

QPushButton#ControlButton:hover,
QPushButton#rs-primary-button:hover,
QPushButton#rs-secondary-button:hover {
	background-color: #151515;
}

QFrame#rs-divider {
	background-color: #ffffff;
}

QLabel#rs-section-label {
	opacity: 1.0;
}

QLineEdit, QComboBox, QListWidget, QPlainTextEdit {
	background-color: #000000;
	color: #ffffff;
	border: 2px solid #ffffff;
	border-radius: 8px;
}

QListWidget::item:selected {
	background-color: #ffffff;
	color: #000000;
}

QCheckBox::indicator:unchecked {
	border: 2px solid #ffffff;
	background-color: #000000;
}
QCheckBox::indicator:checked {
	border: 2px solid #ffffff;
	background-color: #ffffff;
}

/* Safety Lock – High Contrast */
QPushButton#ControlButton[safetyLocked="true"] {
	border-style: dashed;
	outline: 2px dashed #ffffff;
	opacity: 1.0;
}
QPushButton#ControlButton[holding="true"] {
	background-color: #ffffff;
	color: #000000;
	border: 4px solid #ffffff;
}
QPushButton#ControlButton[pulse="true"] {
	animation: rs-pulse-contrast 250ms linear;
}

@keyframes rs-pulse-contrast {
	0% {
		outline: 4px solid #ffffff;
	}
	100% {
		outline: 0 solid transparent;
	}
}

)";
	}

	/* =================================================
	 * THEME: Protanopia Safe (blue + gold, high clarity)
	 * ================================================= */
	else if (m_currentTheme == "protanopia") {
		css += R"(

QWidget {
	background-color: #0f1422;
	color: #f4f7ff;
}

#SidebarButton {
	background-color: rgba(27,38,59,0.90);
	color: #f4f7ff;
	border-radius: 8px;
	padding: 6px 8px;
	border: 1px solid rgba(252,202,70,0.25);
}

#SidebarButton[active="true"] {
	background-color: #fcca46;
	color: #0f1422;
	border: 1px solid rgba(252,202,70,0.95);
}

#SidebarButton:hover {
	background-color: rgba(252,202,70,0.18);
}

#rs-content-card, #rs-card {
	background-color: rgba(27,38,59,0.92);
	border-radius: 10px;
	border: 1px solid rgba(252,202,70,0.35);
}

QPushButton#ControlButton {
	background-color: rgba(255,255,255,0.05);
	color: #f4f7ff;
	border: 1px solid rgba(255,255,255,0.12);
}
QPushButton#ControlButton:hover {
	background-color: rgba(255,255,255,0.09);
}

QPushButton#rs-primary-button {
	background-color: #fcca46;
	color: #0f1422;
	border: 1px solid rgba(252,202,70,0.95);
}
QPushButton#rs-primary-button:hover {
	background-color: rgba(252,202,70,0.85);
}

QPushButton#rs-secondary-button {
	background-color: rgba(255,255,255,0.06);
	color: #f4f7ff;
	border: 1px solid rgba(255,255,255,0.14);
}
QPushButton#rs-secondary-button:hover {
	background-color: rgba(255,255,255,0.10);
}

QFrame#rs-divider {
	background-color: rgba(252,202,70,0.55);
}

/* Inputs */
QLineEdit, QComboBox {
	background-color: rgba(0,0,0,0.22);
	border: 1px solid rgba(252,202,70,0.35);
	color: #f4f7ff;
}
QLineEdit:hover, QComboBox:hover {
	border: 1px solid rgba(252,202,70,0.60);
}

/* Lists */
QListWidget {
	background-color: rgba(0,0,0,0.20);
	border: 1px solid rgba(252,202,70,0.35);
}
QListWidget::item:hover {
	background-color: rgba(252,202,70,0.14);
}
QListWidget::item:selected {
	background-color: rgba(252,202,70,0.30);
	color: #0f1422;
}

/* Checkbox */
QCheckBox::indicator:unchecked {
	border: 1px solid rgba(252,202,70,0.80);
	background-color: rgba(0,0,0,0.15);
}
QCheckBox::indicator:checked {
	background-color: #fcca46;
	border: 1px solid rgba(252,202,70,0.95);
}

/* Logs */
QPlainTextEdit {
	background-color: rgba(0,0,0,0.30);
	border: 1px solid rgba(252,202,70,0.35);
	color: #f4f7ff;
}

/* Safety Lock – Protanopia */
QPushButton#ControlButton[safetyLocked="true"] {
	border-color: rgba(252,202,70,0.95);
}
QPushButton#ControlButton[holding="true"] {
	border-color: rgba(252,202,70,0.95);
	background-color: qlineargradient(
		x1:0, y1:0, x2:1, y2:0,
		stop:0   rgba(252, 202, 70, 0.95),
		stop:1   rgba(252, 202, 70, 0.35)
	);
}
QPushButton#ControlButton[pulse="true"] {
	animation: rs-pulse-safe 300ms ease-out;
}

@keyframes rs-pulse-safe {
	0% { box-shadow: 0 0 0 0 currentColor; }
	100% { box-shadow: 0 0 0 10px transparent; }
}

)";
	}

	/* =================================================
	 * THEME: Deuteranopia Safe (navy + amber, crisp)
	 * ================================================= */
	else if (m_currentTheme == "deuteranopia") {
		css += R"(

QWidget {
	background-color: #0b1220;
	color: #f7fafc;
}

#SidebarButton {
	background-color: rgba(17,24,39,0.92);
	color: #f7fafc;
	border-radius: 8px;
	padding: 6px 8px;
	border: 1px solid rgba(245,158,11,0.22);
}

#SidebarButton[active="true"] {
	background-color: #f59e0b;
	color: #0b1220;
	border: 1px solid rgba(245,158,11,0.95);
}

#SidebarButton:hover {
	background-color: rgba(245,158,11,0.16);
}

#rs-card, #rs-content-card {
	background-color: rgba(17,24,39,0.92);
	border-radius: 10px;
	border: 1px solid rgba(245,158,11,0.30);
}

QPushButton#ControlButton {
	background-color: rgba(255,255,255,0.05);
	color: #f7fafc;
	border: 1px solid rgba(255,255,255,0.12);
}
QPushButton#ControlButton:hover {
	background-color: rgba(255,255,255,0.10);
}

QPushButton#rs-primary-button {
	background-color: #f59e0b;
	color: #0b1220;
	border: 1px solid rgba(245,158,11,0.95);
}
QPushButton#rs-primary-button:hover {
	background-color: rgba(245,158,11,0.85);
}

QPushButton#rs-secondary-button {
	background-color: rgba(255,255,255,0.06);
	color: #f7fafc;
	border: 1px solid rgba(255,255,255,0.14);
}
QPushButton#rs-secondary-button:hover {
	background-color: rgba(255,255,255,0.10);
}

QFrame#rs-divider {
	background-color: rgba(245,158,11,0.55);
}

QLineEdit, QComboBox {
	background-color: rgba(0,0,0,0.22);
	border: 1px solid rgba(245,158,11,0.30);
	color: #f7fafc;
}
QLineEdit:hover, QComboBox:hover {
	border: 1px solid rgba(245,158,11,0.55);
}

QListWidget {
	background-color: rgba(0,0,0,0.20);
	border: 1px solid rgba(245,158,11,0.30);
}
QListWidget::item:hover {
	background-color: rgba(245,158,11,0.12);
}
QListWidget::item:selected {
	background-color: rgba(245,158,11,0.28);
	color: #0b1220;
}

QCheckBox::indicator:unchecked {
	border: 1px solid rgba(245,158,11,0.80);
	background-color: rgba(0,0,0,0.15);
}
QCheckBox::indicator:checked {
	background-color: #f59e0b;
	border: 1px solid rgba(245,158,11,0.95);
}

QPlainTextEdit {
	background-color: rgba(0,0,0,0.30);
	border: 1px solid rgba(245,158,11,0.30);
	color: #f7fafc;
}

/* Safety Lock – Deuteranopia */
QPushButton#ControlButton[safetyLocked="true"] {
	border-color: rgba(245,158,11,0.95);
}
QPushButton#ControlButton[holding="true"] {
	border-color: rgba(245,158,11,0.95);
	background-color: qlineargradient(
		x1:0, y1:0, x2:1, y2:0,
		stop:0   rgba(245, 158, 11, 0.95),
		stop:1   rgba(245, 158, 11, 0.35)
	);
}
QPushButton#ControlButton[pulse="true"] {
	animation: rs-pulse-safe 300ms ease-out;
}

@keyframes rs-pulse-safe {
	0% { box-shadow: 0 0 0 0 currentColor; }
	100% { box-shadow: 0 0 0 10px transparent; }
}

)";
	}

	/* =================================================
	 * THEME: Tritanopia Safe (charcoal + orange, bold)
	 * ================================================= */
	else if (m_currentTheme == "tritanopia") {
		css += R"(

QWidget {
	background-color: #0f0f10;
	color: #f5f5f7;
}

#SidebarButton {
	background-color: rgba(30,30,30,0.92);
	color: #f5f5f7;
	border-radius: 8px;
	padding: 6px 8px;
	border: 1px solid rgba(255,140,66,0.22);
}

#SidebarButton[active="true"] {
	background-color: #ff8c42;
	color: #0f0f10;
	border: 1px solid rgba(255,140,66,0.95);
}

#SidebarButton:hover {
	background-color: rgba(255,140,66,0.16);
}

#rs-card, #rs-content-card {
	background-color: rgba(30,30,30,0.92);
	border-radius: 10px;
	border: 1px solid rgba(255,140,66,0.28);
}

QPushButton#ControlButton {
	background-color: rgba(255,255,255,0.05);
	color: #f5f5f7;
	border: 1px solid rgba(255,255,255,0.12);
}
QPushButton#ControlButton:hover {
	background-color: rgba(255,255,255,0.10);
}

QPushButton#rs-primary-button {
	background-color: #ff8c42;
	color: #0f0f10;
	border: 1px solid rgba(255,140,66,0.95);
}
QPushButton#rs-primary-button:hover {
	background-color: rgba(255,140,66,0.85);
}

QPushButton#rs-secondary-button {
	background-color: rgba(255,255,255,0.06);
	color: #f5f5f7;
	border: 1px solid rgba(255,255,255,0.14);
}
QPushButton#rs-secondary-button:hover {
	background-color: rgba(255,255,255,0.10);
}

QFrame#rs-divider {
	background-color: rgba(255,140,66,0.55);
}

QLineEdit, QComboBox {
	background-color: rgba(0,0,0,0.22);
	border: 1px solid rgba(255,140,66,0.28);
	color: #f5f5f7;
}
QLineEdit:hover, QComboBox:hover {
	border: 1px solid rgba(255,140,66,0.55);
}

QListWidget {
	background-color: rgba(0,0,0,0.20);
	border: 1px solid rgba(255,140,66,0.28);
}
QListWidget::item:hover {
	background-color: rgba(255,140,66,0.12);
}
QListWidget::item:selected {
	background-color: rgba(255,140,66,0.28);
	color: #0f0f10;
}

QCheckBox::indicator:unchecked {
	border: 1px solid rgba(255,140,66,0.80);
	background-color: rgba(0,0,0,0.15);
}
QCheckBox::indicator:checked {
	background-color: #ff8c42;
	border: 1px solid rgba(255,140,66,0.95);
}

QPlainTextEdit {
	background-color: rgba(0,0,0,0.30);
	border: 1px solid rgba(255,140,66,0.28);
	color: #f5f5f7;
}

/* Safety Lock – Tritanopia */
QPushButton#ControlButton[safetyLocked="true"] {
	border-color: rgba(255,140,66,0.95);
}
QPushButton#ControlButton[holding="true"] {
	border-color: rgba(255,140,66,0.95);
	background-color: qlineargradient(
		x1:0, y1:0, x2:1, y2:0,
		stop:0   rgba(255, 140, 66, 0.95),
		stop:1   rgba(255, 140, 66, 0.35)
	);
}
QPushButton#ControlButton[pulse="true"] {
	animation: rs-pulse-safe 300ms ease-out;
}

@keyframes rs-pulse-safe {
	0% { box-shadow: 0 0 0 0 currentColor; }
	100% { box-shadow: 0 0 0 10px transparent; }
}

)";
	}

	/* =================================================
	 * APPLY
	 * ================================================= */
	setStyleSheet(css);

	style()->unpolish(this);
	style()->polish(this);
	update();

	for (QWidget *w : findChildren<QWidget *>()) {
		w->style()->unpolish(w);
		w->style()->polish(w);
		w->update();
	}
}
