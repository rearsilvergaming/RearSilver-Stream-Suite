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
#include <QSettings>
#include <QPointer>
#include <QUuid>
#include <QTimer>
#include <QVector>

#include <obs-frontend-api.h>
#include <obs.h>
#include <util/base.h>

namespace {

static QFont chosenFont("Sora", 120);
static uint32_t chosenColor = 0xFFFFFFFF;
static QPointer<QLabel> color_preview;
static QPointer<QLabel> text_preview;
static QPointer<QLabel> text_preview_scale;

static QStringList saved_presets()
{
	QSettings settings("RearSilver", "RearSilver Stream Suite");
	return settings.value("quickText/presets", QStringList{"BRB", "Coffee Break", "Back Soon"})
		.toStringList();
}

static void save_presets(const QVBoxLayout *layout)
{
	QStringList presets;
	for (int i = 0; i < layout->count(); ++i) {
		if (auto *row = layout->itemAt(i)->widget()) {
			const auto buttons = row->findChildren<QPushButton *>();
			for (auto *button : buttons) {
				if (button->property("quickTextPreset").toBool()) {
					presets.push_back(button->text());
					break;
				}
			}
		}
	}
	QSettings settings("RearSilver", "RearSilver Stream Suite");
	settings.setValue("quickText/presets", presets);
}

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

void update_text_preview()
{
	if (!text_preview)
		return;
	QColor c((chosenColor >> 16) & 0xFF, (chosenColor >> 8) & 0xFF, chosenColor & 0xFF,
		 (chosenColor >> 24) & 0xFF);
	// Keep the preview stage fixed and scale the text inside it. The dock's global
	// stylesheet sets label fonts, so the preview size must be explicit CSS rather
	// than relying on QLabel::setFont alone.
	const int outputSize = qMax(1, chosenFont.pointSize());
	const double sliderPosition = qBound(0.0, (outputSize - 20.0) / 180.0, 1.0);
	const int previewPixels = qRound(12.0 + sliderPosition * 50.0);
	QString family = chosenFont.family();
	family.replace('"', "\\\"");
	text_preview->setStyleSheet(
		QString("color: rgba(%1,%2,%3,%4); background:#222; border-radius:4px; "
			"border:1px solid #444; padding:4px; font-family:\"%5\"; "
			"font-size:%6px; font-weight:%7; font-style:%8;")
			.arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alphaF())
			.arg(family).arg(previewPixels).arg(chosenFont.weight())
			.arg(chosenFont.italic() ? "italic" : "normal"));
	text_preview->setFixedHeight(96);
	if (text_preview_scale)
		text_preview_scale->setText(QString("Fixed preview stage • OBS output: %1 pt").arg(outputSize));
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

static int clean_quicktext_group()
{
	obs_source_t *sceneSrc = obs_frontend_get_current_scene();
	if (!sceneSrc)
		return 0;
	obs_scene_t *scene = obs_scene_from_source(sceneSrc);
	if (!scene) {
		obs_source_release(sceneSrc);
		return 0;
	}
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
		if (groupScene) {
			QVector<obs_sceneitem_t *> items;
			obs_scene_enum_items(
				groupScene,
				[](obs_scene_t *, obs_sceneitem_t *i, void *data) {
					auto *items = static_cast<QVector<obs_sceneitem_t *> *>(data);
					obs_sceneitem_addref(i);
					items->push_back(i);
					return true;
				},
				&items);
			for (auto *item : items) {
				obs_sceneitem_remove(item);
				obs_sceneitem_release(item);
			}
			const int removed = items.size();
			obs_source_release(sceneSrc);
			return removed;
		}
	}
	obs_source_release(sceneSrc);
	return 0;
}

static bool drop_text_into_scene(const QString &text, int fontSize, uint32_t color, bool replaceExisting,
				 QString *error = nullptr)
{
	if (text.trimmed().isEmpty()) {
		if (error)
			*error = "Enter some text first.";
		return false;
	}

	obs_source_t *sceneSrc = obs_frontend_get_current_scene();
	if (!sceneSrc) {
		if (error)
			*error = "No active OBS scene is available.";
		return false;
	}
	obs_scene_t *scene = obs_scene_from_source(sceneSrc);
	if (!scene) {
		obs_source_release(sceneSrc);
		if (error)
			*error = "The active OBS source is not a scene.";
		return false;
	}

	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "text", text.toUtf8().constData());
	obs_data_set_bool(settings, "custom_color", true);
	obs_data_set_int(settings, "color", swap_rb_bytes(color & 0x00FFFFFF));
	obs_data_t *font = obs_data_create();
	obs_data_set_string(font, "face", chosenFont.family().toUtf8().constData());
	obs_data_set_int(font, "size", fontSize);
	obs_data_set_obj(settings, "font", font);

	obs_sceneitem_t *groupItem = ensure_group(scene);
	if (!groupItem) {
		obs_data_release(font);
		obs_data_release(settings);
		obs_source_release(sceneSrc);
		if (error)
			*error = "OBS could not create the Quick Text group.";
		return false;
	}
	// Quick Text is an action, not just an editor. If the user hid the group
	// after its previous use, pressing a preset or Drop Text must show it again.
	obs_sceneitem_set_visible(groupItem, true);
	obs_scene_t *groupScene = obs_group_or_scene_from_source(obs_sceneitem_get_source(groupItem));
	if (!groupScene) {
		obs_data_release(font);
		obs_data_release(settings);
		obs_source_release(sceneSrc);
		if (error)
			*error = "OBS could not open the Quick Text group.";
		return false;
	}
	const QString sceneName = QString::fromUtf8(obs_source_get_name(sceneSrc));
	const QString activeName = QString("Quick Text - %1 - Active").arg(sceneName);

	if (replaceExisting) {
		obs_sceneitem_t *existing = obs_scene_find_source(groupScene, activeName.toUtf8().constData());
		if (existing) {
			obs_source_update(obs_sceneitem_get_source(existing), settings);
			// The item itself may have been hidden independently of its group.
			obs_sceneitem_set_visible(existing, true);
			goto cleanup;
		}
	}

	{
		const QString sourceName = replaceExisting
					   ? activeName
					   : QString("Quick Text - %1").arg(QUuid::createUuid().toString(QUuid::Id128));
		obs_source_t *textSrc = obs_source_create(
#ifdef _WIN32
			"text_gdiplus",
#else
			"text_ft2_source",
#endif
			sourceName.toUtf8().constData(), settings, nullptr);
		if (!textSrc) {
			if (error)
				*error = "OBS could not create a text source.";
			goto failed;
		}
		obs_sceneitem_t *textItem = obs_scene_add(groupScene, textSrc);
		if (textItem)
			obs_sceneitem_set_visible(textItem, true);
		obs_source_release(textSrc);
	}

cleanup:
	obs_data_release(font);
	obs_data_release(settings);
	obs_source_release(sceneSrc);
	return true;

failed:
	obs_data_release(font);
	obs_data_release(settings);
	obs_source_release(sceneSrc);
	return false;
}

static void add_preset_row(QWidget *page, QVBoxLayout *presetsLayout, const QString &label, QSlider *fontSize,
			   QCheckBox *chkReplace, QLabel *status)
{
	QWidget *row = new QWidget(page);
	auto *layout = new QHBoxLayout(row);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(4);

	QPushButton *btn = new QPushButton(label);
	btn->setObjectName("rs-secondary-button");
	btn->setProperty("quickTextPreset", true);
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
		QString error;
		const bool ok = drop_text_into_scene(btn->text(), fontSize->value(), chosenColor,
						     chkReplace->isChecked(), &error);
		status->setText(ok ? QString("Added \"%1\" to the current scene.").arg(btn->text()) : error);
	});
	QObject::connect(btnEdit, &QToolButton::clicked, page, [=]() {
		bool ok;
		QString t = QInputDialog::getText(page, "Edit", "Text:", QLineEdit::Normal, btn->text(), &ok);
		if (ok && !t.trimmed().isEmpty()) {
			btn->setText(t.trimmed());
			save_presets(presetsLayout);
		}
	});
	QObject::connect(btnDel, &QToolButton::clicked, row, [=]() {
		row->deleteLater();
		QTimer::singleShot(0, page, [=]() { save_presets(presetsLayout); });
	});
}

} // namespace

bool RsQuickText::showText(const QString &text, int size, const QString &colour, const QString &font)
{
	QColor c(colour); if (!c.isValid()) c = Qt::white;
	const int boundedSize = qBound(8, size, 300);
	chosenFont = QFont(font.trimmed().isEmpty() ? QStringLiteral("Sora") : font, boundedSize);
	QSettings settings("RearSilver", "RearSilver-Stream-Suite");
	settings.setValue("quickText/fontFamily", chosenFont.family());
	settings.setValue("quickText/fontSize", boundedSize);
	const uint32_t packed = (uint32_t(c.alpha()) << 24) | (uint32_t(c.red()) << 16) |
		(uint32_t(c.green()) << 8) | uint32_t(c.blue());
	chosenColor = packed;
	return drop_text_into_scene(text, boundedSize, packed, true, nullptr);
}

int RsQuickText::clearAll() { return clean_quicktext_group(); }


// ────────────────────────────────────────────────────────────────
// UI CREATION
// ────────────────────────────────────────────────────────────────
QWidget *RsQuickText::createPage(RsMainDock *, QWidget *parent)
{
	QSettings settings("RearSilver", "RearSilver Stream Suite");
	chosenFont = QFont(settings.value("quickText/fontFamily", "Sora").toString(),
			   settings.value("quickText/fontSize", 120).toInt());
	chosenColor = settings.value("quickText/color", static_cast<qulonglong>(0xFFFFFFFF)).toULongLong();
	auto *status = new QLabel();
	status->setWordWrap(true);
	status->setProperty("muted", true);

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
	chkReplace->setChecked(settings.value("quickText/replace", true).toBool());
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
	update_text_preview();
	bottomLayout->addWidget(text_preview);
	text_preview_scale = new QLabel();
	text_preview_scale->setAlignment(Qt::AlignCenter);
	text_preview_scale->setStyleSheet("color:#8fa6c4; font-size:10px;");
	bottomLayout->addWidget(text_preview_scale);
	update_text_preview();

	QSlider *fontSize = new QSlider(Qt::Horizontal);
	fontSize->setRange(20, 200);
	fontSize->setValue(qBound(20, chosenFont.pointSize(), 200));
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
	bottomLayout->addWidget(status);

	root->addWidget(bottomSection);
	bottomSection->setFixedHeight(kControlsSectionHeight);

	// --- Logic ---
	for (const auto &preset : saved_presets())
		add_preset_row(page, listLayout, preset, fontSize, chkReplace, status);

	QObject::connect(txtInput, &QLineEdit::textChanged, page, [=](const QString &t) {
		text_preview->setText(t.isEmpty() ? "(Preview)" : t);
		update_text_preview();
	});

	QObject::connect(fontSize, &QSlider::valueChanged, page, [=](int v) {
		chosenFont.setPointSize(v);
		update_text_preview();
		QSettings settings("RearSilver", "RearSilver Stream Suite");
		settings.setValue("quickText/fontSize", v);
	});
	QObject::connect(chkReplace, &QCheckBox::toggled, page, [](bool checked) {
		QSettings settings("RearSilver", "RearSilver Stream Suite");
		settings.setValue("quickText/replace", checked);
	});

	QObject::connect(btnAdd, &QToolButton::clicked, page, [=]() {
		bool ok;
		QString l = QInputDialog::getText(page, "New Preset", "Text:", QLineEdit::Normal, "", &ok);
		if (ok && !l.trimmed().isEmpty()) {
			add_preset_row(page, listLayout, l.trimmed(), fontSize, chkReplace, status);
			save_presets(listLayout);
			status->setText(QString("Saved preset \"%1\".").arg(l.trimmed()));
		}
	});

	QObject::connect(btnColor, &QPushButton::clicked, page, [=]() {
		QColor c = QColorDialog::getColor(Qt::white, page);
		if (c.isValid()) {
			chosenColor = (c.alpha() << 24) | (c.red() << 16) | (c.green() << 8) | c.blue();
			set_color_preview(chosenColor);
			update_text_preview();
			QSettings settings("RearSilver", "RearSilver Stream Suite");
			settings.setValue("quickText/color", static_cast<qulonglong>(chosenColor));
		}
	});

	QObject::connect(btnFont, &QPushButton::clicked, page, [=]() {
		bool ok;
		QFont f = QFontDialog::getFont(&ok, chosenFont, page);
		if (ok) {
			chosenFont = f;
			fontSize->setValue(f.pointSize());
			update_text_preview();
			QSettings settings("RearSilver", "RearSilver Stream Suite");
			settings.setValue("quickText/fontFamily", f.family());
		}
	});

	QObject::connect(btnDrop, &QPushButton::clicked, page, [=]() {
		QString error;
		const bool ok = drop_text_into_scene(txtInput->text(), fontSize->value(), chosenColor,
						     chkReplace->isChecked(), &error);
		status->setText(ok ? "Quick Text added to the current scene." : error);
	});

	QObject::connect(btnClean, &QPushButton::clicked, page, [=]() {
		const int removed = clean_quicktext_group();
		status->setText(removed > 0 ? QString("Cleared %1 Quick Text source(s).").arg(removed)
						    : "There are no Quick Text sources in the current scene.");
	});

	return scroll;
}
