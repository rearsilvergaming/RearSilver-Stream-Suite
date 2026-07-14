#include "rs_quick_text.hpp"
#include "../rs_main_dock.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QSlider>
#include <QColorDialog>
#include <QFontDialog>
#include <QFont>
#include <QCheckBox>
#include <QStyle>
#include <QToolButton>
#include <QInputDialog>
#include <QScrollArea>

#include <obs-frontend-api.h>
#include <obs.h>
#include <util/base.h>

namespace {

static std::string lastQuickTextName = "";
static QFont chosenFont("Arial", 120);
static uint32_t chosenColor = 0xFFFFFFFF;
static QLabel *color_preview = nullptr;
static QLabel *text_preview = nullptr;

// --- Helpers ---
void set_color_preview(uint32_t color)
{
	if (!color_preview)
		return;
	QColor c((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF, (color >> 24) & 0xFF);
	color_preview->setStyleSheet(
		QString("background-color: rgba(%1,%2,%3,%4); border:1px solid #555; border-radius:4px;")
			.arg(c.red())
			.arg(c.green())
			.arg(c.blue())
			.arg(c.alphaF()));
}

void set_font_preview()
{
	if (!text_preview)
		return;
	text_preview->setFont(chosenFont);
	text_preview->setFixedHeight(qBound(40, QFontMetrics(chosenFont).height() + 10, 80));
}

uint32_t swap_rb_bytes(uint32_t rgb)
{
	return ((rgb & 0xFF) << 16) | (rgb & 0xFF00) | ((rgb >> 16) & 0xFF);
}

// --- OBS Logic ---
static obs_sceneitem_t *ensure_group(obs_scene_t *scene)
{
	obs_sceneitem_t *found = nullptr;
	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
			obs_source_t *src = obs_sceneitem_get_source(item);
			if (src && strcmp(obs_source_get_unversioned_id(src), "group") == 0 &&
			    strcmp(obs_source_get_name(src), "Quick Text") == 0) {
				*(obs_sceneitem_t **)param = item;
				return false;
			}
			return true;
		},
		&found);
	return found ? found : obs_scene_add_group(scene, "Quick Text");
}

void clean_quicktext_group()
{
	obs_source_t *sceneSrc = obs_frontend_get_current_scene();
	if (!sceneSrc)
		return;
	obs_scene_t *scene = obs_scene_from_source(sceneSrc);
	obs_sceneitem_t *groupItem = nullptr;
	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
			obs_source_t *src = obs_sceneitem_get_source(item);
			if (src && strcmp(obs_source_get_name(src), "Quick Text") == 0) {
				*(obs_sceneitem_t **)param = item;
				return false;
			}
			return true;
		},
		&groupItem);

	if (groupItem) {
		obs_scene_t *groupScene = obs_group_or_scene_from_source(obs_sceneitem_get_source(groupItem));
		if (groupScene)
			obs_scene_enum_items(
				groupScene,
				[](obs_scene_t *, obs_sceneitem_t *i, void *) {
					obs_sceneitem_remove(i);
					return true;
				},
				nullptr);
	}
	lastQuickTextName.clear();
	obs_source_release(sceneSrc);
}

static void drop_text_into_scene(const QString &text, int fontSize, uint32_t color, bool replaceExisting)
{
	if (text.isEmpty())
		return;
	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "text", text.toUtf8().constData());
	obs_data_set_bool(settings, "custom_color", true);
	obs_data_set_int(settings, "color", swap_rb_bytes(color & 0x00FFFFFF));
	obs_data_t *font = obs_data_create();
	obs_data_set_string(font, "face", chosenFont.family().toUtf8().constData());
	obs_data_set_int(font, "size", fontSize);
	obs_data_set_obj(settings, "font", font);

	obs_source_t *sceneSrc = obs_frontend_get_current_scene();
	obs_scene_t *scene = obs_scene_from_source(sceneSrc);
	obs_sceneitem_t *groupItem = ensure_group(scene);
	obs_scene_t *groupScene = obs_group_or_scene_from_source(obs_sceneitem_get_source(groupItem));

	if (replaceExisting && !lastQuickTextName.empty()) {
		obs_sceneitem_t *existing = obs_scene_find_source(groupScene, lastQuickTextName.c_str());
		if (existing) {
			obs_source_update(obs_sceneitem_get_source(existing), settings);
			goto cleanup;
		}
	}

	{
		obs_source_t *textSrc = obs_source_create(
#ifdef _WIN32
			"text_gdiplus",
#else
			"text_ft2_source",
#endif
			text.toUtf8().constData(), settings, nullptr);
		obs_scene_add(groupScene, textSrc);
		lastQuickTextName = obs_source_get_name(textSrc);
		obs_source_release(textSrc);
	}

cleanup:
	obs_data_release(font);
	obs_data_release(settings);
	obs_source_release(sceneSrc);
}

static void add_preset_row(QWidget *page, QVBoxLayout *presetsLayout, const QString &label, QSlider *fontSize,
			   QCheckBox *chkReplace)
{
	QWidget *row = new QWidget(page);
	auto *layout = new QHBoxLayout(row);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(4);

	QPushButton *btn = new QPushButton(label);
	btn->setObjectName("rs-secondary-button");
	btn->setMinimumHeight(32); // Explicit height to help calculate scroll area

	QToolButton *btnEdit = new QToolButton();
	btnEdit->setIcon(page->style()->standardIcon(QStyle::SP_FileDialogContentsView));
	btnEdit->setAutoRaise(true);

	QToolButton *btnDel = new QToolButton();
	btnDel->setIcon(page->style()->standardIcon(QStyle::SP_TrashIcon));
	btnDel->setAutoRaise(true);

	layout->addWidget(btn, 1);
	layout->addWidget(btnEdit);
	layout->addWidget(btnDel);
	presetsLayout->addWidget(row);

	QObject::connect(btn, &QPushButton::clicked, page, [=]() {
		drop_text_into_scene(btn->text(), fontSize->value(), chosenColor, chkReplace->isChecked());
	});
	QObject::connect(btnEdit, &QToolButton::clicked, page, [=]() {
		bool ok;
		QString t = QInputDialog::getText(page, "Edit", "Text:", QLineEdit::Normal, btn->text(), &ok);
		if (ok && !t.trimmed().isEmpty())
			btn->setText(t.trimmed());
	});
	QObject::connect(btnDel, &QToolButton::clicked, row, &QWidget::deleteLater);
}

} // namespace


// ────────────────────────────────────────────────────────────────
// UI CREATION
// ────────────────────────────────────────────────────────────────
QWidget *RsQuickText::createPage(RsMainDock *, QWidget *parent)
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
	root->setContentsMargins(4, 0, 4, 0);
	root->setSpacing(10);
	// root->setAlignment(Qt::AlignTop); // leave OFF for now

	// --- Constants ---
	constexpr int kTopSectionHeight = 80;
	constexpr int kPresetsSectionHeight = 275;
	constexpr int kControlsSectionHeight = 250;

	// ────────────────────────────────────────────────────────────────
	// 1. TOP SECTION (Fixed Height)
	// ────────────────────────────────────────────────────────────────
	QWidget *topSection = new QWidget();
	auto *topLayout = new QVBoxLayout(topSection);
	topLayout->setContentsMargins(0, 0, 0, 0);
	topLayout->setSpacing(4);

	auto *title = new QLabel("Quick Text Maker");
	title->setMaximumHeight(title->sizeHint().height());
	title->setStyleSheet("font-weight: bold; font-size: 13px;");
	topLayout->addWidget(title);

	auto *desc = new QLabel("Items are placed inside the “Quick Text” group.");
	desc->setStyleSheet("font-size: 11px; opacity: 0.7;");
	topLayout->addWidget(desc);

	QCheckBox *chkReplace = new QCheckBox("Replace last instead of stacking");
	topLayout->addWidget(chkReplace);

	root->addWidget(topSection);
	topSection->setFixedHeight(kTopSectionHeight);

	// ────────────────────────────────────────────────────────────────
	// 2. PRESETS SECTION (Fixed height, internal scroll)
	// ────────────────────────────────────────────────────────────────
	QWidget *presetsCard = new QWidget();
	presetsCard->setObjectName("rs-card");
	presetsCard->setMinimumHeight(kPresetsSectionHeight);
	presetsCard->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

	auto *cardLayout = new QVBoxLayout(presetsCard);
	cardLayout->setContentsMargins(4, 2, 4, 2);
	cardLayout->setSpacing(4);

	cardLayout->addWidget(new QLabel("Presets:"));

	// IMPORTANT: this must NOT be named "scroll" because outerScroll already exists
	QScrollArea *presetsScroll = new QScrollArea();
	presetsScroll->setWidgetResizable(true);
	presetsScroll->setFrameShape(QFrame::NoFrame);
	presetsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	presetsScroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

	QWidget *container = new QWidget();
	auto *listLayout = new QVBoxLayout(container);
	listLayout->setContentsMargins(0, 0, 0, 0);
	listLayout->setSpacing(4);
	listLayout->setAlignment(Qt::AlignTop);

	presetsScroll->setWidget(container);

	// Presets list takes remaining space inside the card
	cardLayout->addWidget(presetsScroll, 1);

	QToolButton *btnAdd = new QToolButton();
	btnAdd->setIcon(page->style()->standardIcon(QStyle::SP_FileDialogNewFolder));
	btnAdd->setText(" Add New Preset");
	btnAdd->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	btnAdd->setAutoRaise(true);
	btnAdd->setFixedHeight(28);

	cardLayout->addWidget(btnAdd, 0, Qt::AlignRight);

	// NOTE: NO stretch factor here, because we don't want the card to grow and “eat” the void
	root->addWidget(presetsCard);

	// ────────────────────────────────────────────────────────────────
	// 3. CONTROLS SECTION (Fixed Height)
	// ────────────────────────────────────────────────────────────────
	QWidget *bottomSection = new QWidget();
	auto *bottomLayout = new QVBoxLayout(bottomSection);
	bottomLayout->setContentsMargins(0, 0, 0, 0);
	bottomLayout->setSpacing(4);
	bottomSection->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

	QLineEdit *txtInput = new QLineEdit();
	txtInput->setPlaceholderText("Custom message…");
	bottomLayout->addWidget(txtInput);

	text_preview = new QLabel("(Preview)");
	text_preview->setAlignment(Qt::AlignCenter);
	text_preview->setStyleSheet("background:#222; border-radius:4px; border:1px solid #444;");
	set_font_preview();
	bottomLayout->addWidget(text_preview);

	QSlider *fontSize = new QSlider(Qt::Horizontal);
	fontSize->setRange(20, 200);
	fontSize->setValue(120);
	auto *sizeRow = new QHBoxLayout();
	sizeRow->addWidget(new QLabel("Size:"));
	sizeRow->addWidget(fontSize);
	bottomLayout->addLayout(sizeRow);

	auto *pickerRow = new QHBoxLayout();
	color_preview = new QLabel();
	color_preview->setFixedSize(20, 20);
	set_color_preview(chosenColor);
	pickerRow->addWidget(color_preview);
	QPushButton *btnColor = new QPushButton("Colour");
	QPushButton *btnFont = new QPushButton("Font");
	pickerRow->addWidget(btnColor);
	pickerRow->addWidget(btnFont);
	bottomLayout->addLayout(pickerRow);

	auto *actionRow = new QHBoxLayout();
	QPushButton *btnDrop = new QPushButton("Drop Text");
	btnDrop->setObjectName("rs-primary-button");
	btnDrop->setMinimumHeight(34);
	QPushButton *btnClean = new QPushButton("Clear All");
	btnClean->setObjectName("rs-secondary-button");
	btnClean->setMinimumHeight(34);
	actionRow->addWidget(btnDrop);
	actionRow->addWidget(btnClean);
	bottomLayout->addLayout(actionRow);

	root->addWidget(bottomSection);
	bottomSection->setFixedHeight(kControlsSectionHeight);

	// --- Logic ---
	add_preset_row(page, listLayout, "BRB", fontSize, chkReplace);
	add_preset_row(page, listLayout, "Coffee Break", fontSize, chkReplace);
	add_preset_row(page, listLayout, "Back Soon", fontSize, chkReplace);

	QObject::connect(txtInput, &QLineEdit::textChanged, page,
			 [=](const QString &t) { text_preview->setText(t.isEmpty() ? "(Preview)" : t); });

	QObject::connect(fontSize, &QSlider::valueChanged, page, [=](int v) {
		chosenFont.setPointSize(v);
		set_font_preview();
	});

	QObject::connect(btnAdd, &QToolButton::clicked, page, [=]() {
		bool ok;
		QString l = QInputDialog::getText(page, "New Preset", "Text:", QLineEdit::Normal, "", &ok);
		if (ok && !l.trimmed().isEmpty())
			add_preset_row(page, listLayout, l.trimmed(), fontSize, chkReplace);
	});

	QObject::connect(btnColor, &QPushButton::clicked, page, [=]() {
		QColor c = QColorDialog::getColor(Qt::white, page);
		if (c.isValid()) {
			chosenColor = (c.alpha() << 24) | (c.red() << 16) | (c.green() << 8) | c.blue();
			set_color_preview(chosenColor);
		}
	});

	QObject::connect(btnFont, &QPushButton::clicked, page, [=]() {
		bool ok;
		QFont f = QFontDialog::getFont(&ok, chosenFont, page);
		if (ok) {
			chosenFont = f;
			fontSize->setValue(f.pointSize());
			set_font_preview();
		}
	});

	QObject::connect(btnDrop, &QPushButton::clicked, page, [=]() {
		drop_text_into_scene(txtInput->text(), fontSize->value(), chosenColor, chkReplace->isChecked());
	});

	QObject::connect(btnClean, &QPushButton::clicked, page, clean_quicktext_group);

return scroll;
}
