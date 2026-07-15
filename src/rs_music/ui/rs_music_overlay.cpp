#include "rs_music_overlay.hpp"
#include "rs_music/rs_music_server.hpp"
#include <QDesktopServices>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFontComboBox>
#include <QFont>
#include <QFrame>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QWheelEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QUrl>
#include <QVBoxLayout>
#include <obs-frontend-api.h>
#include <obs.h>

class RsOverlayComboBox : public QComboBox { public: using QComboBox::QComboBox; protected: void wheelEvent(QWheelEvent *event) override { event->ignore(); } };
class RsOverlayFontComboBox : public QFontComboBox { public: using QFontComboBox::QFontComboBox; protected: void wheelEvent(QWheelEvent *event) override { event->ignore(); } };
class RsOverlaySpinBox : public QSpinBox { public: using QSpinBox::QSpinBox; protected: void wheelEvent(QWheelEvent *event) override { event->ignore(); } };

RsMusicOverlay::RsMusicOverlay(QWidget *parent) : QWidget(parent)
{
	auto *root=new QVBoxLayout(this); root->setContentsMargins(8,8,8,8); root->setSpacing(10);
	auto *scroll=new QScrollArea(); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame); scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); auto *content=new QWidget(); content->setMinimumWidth(0); auto *contentLayout=new QVBoxLayout(content); contentLayout->setContentsMargins(0,0,0,0); contentLayout->setSpacing(10); scroll->setWidget(content); root->addWidget(scroll); root=contentLayout;
	auto *title=new QLabel("Music — Overlay"); QFont font=title->font(); font.setBold(true); font.setPointSize(font.pointSize()+2); title->setFont(font); root->addWidget(title);
	auto *intro=new QLabel("Create a browser overlay driven by the Suite’s live provider-neutral music state. This first foundation uses a fixed layout; visual customisation and presets will be added here next."); intro->setWordWrap(true); root->addWidget(intro);
	auto *canvasTitle=new QLabel("Design canvas"); font=canvasTitle->font(); font.setBold(true); canvasTitle->setFont(font); root->addWidget(canvasTitle);
	QSettings settings("RearSilver","RearSilver-Stream-Suite");
	m_width=new RsOverlaySpinBox(); m_width->setRange(320,3840); m_width->setValue(settings.value("music/overlay/main/width",800).toInt());
	m_height=new RsOverlaySpinBox(); m_height->setRange(120,2160); m_height->setValue(settings.value("music/overlay/main/height",240).toInt());
	auto *form=new QFormLayout(); form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow); form->setRowWrapPolicy(QFormLayout::WrapLongRows); form->addRow("Width:",m_width); form->addRow("Height:",m_height); root->addLayout(form);
	m_url=new QLabel(); m_url->setWordWrap(true); m_url->setTextInteractionFlags(Qt::TextSelectableByMouse); root->addWidget(m_url);
	auto *designerTitle=new QLabel("Content and appearance"); designerTitle->setFont(font); root->addWidget(designerTitle);
	auto *showArtwork=new QCheckBox("Show artwork"); auto *showTitle=new QCheckBox("Show title"); auto *showArtist=new QCheckBox("Show artist"); auto *showAlbum=new QCheckBox("Show album"); auto *showRequester=new QCheckBox("Show requested by"); auto *showProgress=new QCheckBox("Show progress bar");
	showArtwork->setChecked(settings.value("music/overlay/main/showArtwork",true).toBool()); showTitle->setChecked(settings.value("music/overlay/main/showTitle",true).toBool()); showArtist->setChecked(settings.value("music/overlay/main/showArtist",true).toBool()); showAlbum->setChecked(settings.value("music/overlay/main/showAlbum",true).toBool()); showRequester->setChecked(settings.value("music/overlay/main/showRequester",false).toBool()); showProgress->setChecked(settings.value("music/overlay/main/showProgress",true).toBool());
	for(auto *box:{showArtwork,showTitle,showArtist,showAlbum,showRequester,showProgress})root->addWidget(box);
	auto *showCustomText=new QCheckBox("Show custom text"); showCustomText->setChecked(settings.value("music/overlay/main/showCustomText",false).toBool()); root->addWidget(showCustomText);
	auto *customText=new QLineEdit(settings.value("music/overlay/main/customText","").toString()); customText->setPlaceholderText("Stream name or custom label"); root->addWidget(customText);
	auto *artPosition=new RsOverlayComboBox(); artPosition->addItem("Artwork on left","left"); artPosition->addItem("Artwork on right","right"); artPosition->setCurrentIndex(qMax(0,artPosition->findData(settings.value("music/overlay/main/artworkPosition","left"))));
	auto *timing=new RsOverlayComboBox(); timing->addItem("Elapsed / total","elapsedTotal"); timing->addItem("Elapsed only","elapsed"); timing->addItem("Remaining only","remaining"); timing->addItem("Elapsed and remaining","elapsedRemaining"); timing->addItem("Hide timing","none"); timing->setCurrentIndex(qMax(0,timing->findData(settings.value("music/overlay/main/timingMode","elapsedTotal"))));
	auto *fontFamily=new RsOverlayFontComboBox(); fontFamily->setCurrentFont(QFont(settings.value("music/overlay/main/fontFamily","Arial").toString()));
	auto *titleSize=new RsOverlaySpinBox(); titleSize->setRange(10,96); titleSize->setValue(settings.value("music/overlay/main/titleSize",34).toInt()); auto *bodySize=new RsOverlaySpinBox(); bodySize->setRange(8,72); bodySize->setValue(settings.value("music/overlay/main/bodySize",20).toInt());
	auto *appearance=new QFormLayout(); appearance->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow); appearance->setRowWrapPolicy(QFormLayout::WrapLongRows); appearance->addRow("Artwork position:",artPosition); appearance->addRow("Timing:",timing); appearance->addRow("Font:",fontFamily); appearance->addRow("Title size:",titleSize); appearance->addRow("Body size:",bodySize); root->addLayout(appearance);
	auto *transparent=new QCheckBox("Transparent background"); transparent->setChecked(settings.value("music/overlay/main/backgroundTransparent",false).toBool()); root->addWidget(transparent);
	auto *artworkBackground=new QCheckBox("Use faded album artwork as background"); artworkBackground->setChecked(settings.value("music/overlay/main/artworkBackground",false).toBool()); root->addWidget(artworkBackground);
	auto colourButton=[&](const QString &label,const QString &key,const QString &fallback){auto *button=new QPushButton(label); connect(button,&QPushButton::clicked,this,[button,key,fallback]{QSettings s("RearSilver","RearSilver-Stream-Suite"); QColor chosen=QColorDialog::getColor(QColor(s.value("music/overlay/main/"+key,fallback).toString()),button); if(chosen.isValid())s.setValue("music/overlay/main/"+key,chosen.name());}); root->addWidget(button);}; colourButton("Choose background colour","backgroundColour","#0c0c12"); colourButton("Choose text colour","textColour","#ffffff"); colourButton("Choose accent colour","accentColour","#9147ff");
	auto *opacity=new RsOverlaySpinBox(); opacity->setRange(0,100); opacity->setSuffix("%"); opacity->setValue(settings.value("music/overlay/main/backgroundOpacity",82).toInt()); auto *opacityForm=new QFormLayout(); opacityForm->setRowWrapPolicy(QFormLayout::WrapLongRows); opacityForm->addRow("Background opacity:",opacity); root->addLayout(opacityForm);
	auto saveBool=[](const QString &key,bool value){QSettings("RearSilver","RearSilver-Stream-Suite").setValue("music/overlay/main/"+key,value);};
	connect(showArtwork,&QCheckBox::toggled,this,[=](bool v){saveBool("showArtwork",v);}); connect(showTitle,&QCheckBox::toggled,this,[=](bool v){saveBool("showTitle",v);}); connect(showArtist,&QCheckBox::toggled,this,[=](bool v){saveBool("showArtist",v);}); connect(showAlbum,&QCheckBox::toggled,this,[=](bool v){saveBool("showAlbum",v);}); connect(showRequester,&QCheckBox::toggled,this,[=](bool v){saveBool("showRequester",v);}); connect(showProgress,&QCheckBox::toggled,this,[=](bool v){saveBool("showProgress",v);}); connect(transparent,&QCheckBox::toggled,this,[=](bool v){saveBool("backgroundTransparent",v);});
	connect(showCustomText,&QCheckBox::toggled,this,[=](bool v){saveBool("showCustomText",v);}); connect(artworkBackground,&QCheckBox::toggled,this,[=](bool v){saveBool("artworkBackground",v);});
	auto saveText=[](const QString &key,const QVariant &value){QSettings("RearSilver","RearSilver-Stream-Suite").setValue("music/overlay/main/"+key,value);}; connect(artPosition,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[=]{saveText("artworkPosition",artPosition->currentData());}); connect(timing,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[=]{saveText("timingMode",timing->currentData());}); connect(fontFamily,&QFontComboBox::currentFontChanged,this,[=](const QFont &f){saveText("fontFamily",f.family());}); connect(titleSize,QOverload<int>::of(&QSpinBox::valueChanged),this,[=](int v){saveText("titleSize",v);}); connect(bodySize,QOverload<int>::of(&QSpinBox::valueChanged),this,[=](int v){saveText("bodySize",v);}); connect(opacity,QOverload<int>::of(&QSpinBox::valueChanged),this,[=](int v){saveText("backgroundOpacity",v);});
	connect(customText,&QLineEdit::textChanged,this,[=](const QString &v){saveText("customText",v);});
	auto *reset=new QPushButton("Reset overlay to defaults"); auto *preview=new QPushButton("Open full preview"); auto *source=new QPushButton("Create or update Music Overlay source"); source->setObjectName("rs-primary-button"); reset->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred); preview->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred); source->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred); root->addWidget(reset); root->addWidget(preview); root->addWidget(source);
	m_status=new QLabel(); m_status->setWordWrap(true); m_status->setStyleSheet("opacity:0.75; font-size:11px;"); root->addWidget(m_status); root->addStretch();
	connect(preview,&QPushButton::clicked,this,[]{QDesktopServices::openUrl(QUrl(RsMusicServer::instance().overlayUrl()));});
	connect(source,&QPushButton::clicked,this,&RsMusicOverlay::createOrUpdateSource);
	connect(reset,&QPushButton::clicked,this,[=]{
		if(QMessageBox::question(this,"Reset music overlay","Reset the main music overlay design to its default appearance?")!=QMessageBox::Yes)return;
		QSettings s("RearSilver","RearSilver-Stream-Suite"); s.beginGroup("music/overlay/main"); s.remove(""); s.endGroup();
		m_width->setValue(800); m_height->setValue(240); showArtwork->setChecked(true); showTitle->setChecked(true); showArtist->setChecked(true); showAlbum->setChecked(true); showRequester->setChecked(false); showProgress->setChecked(true); showCustomText->setChecked(false); customText->clear(); artPosition->setCurrentIndex(artPosition->findData("left")); timing->setCurrentIndex(timing->findData("elapsedTotal")); fontFamily->setCurrentFont(QFont("Arial")); titleSize->setValue(34); bodySize->setValue(20); transparent->setChecked(false); artworkBackground->setChecked(false); opacity->setValue(82); m_status->setText("Overlay appearance reset to defaults. Visual changes are already live; update the source only if its canvas dimensions also need resetting.");
	});
	refreshStatus();
}

void RsMusicOverlay::refreshStatus()
{
	m_url->setText(QString("Overlay URL: %1").arg(RsMusicServer::instance().overlayUrl()));
	obs_source_t *source=obs_get_source_by_name("Music Overlay");
	m_status->setText(source?"Music Overlay exists. Updating will preserve its position and transform in OBS.":"No Music Overlay Browser Source exists yet.");
	if(source)obs_source_release(source);
}

void RsMusicOverlay::createOrUpdateSource()
{
	const QString url=RsMusicServer::instance().overlayUrl(); if(url.isEmpty()){m_status->setText("The local overlay service is not running.");return;}
	QSettings settings("RearSilver","RearSilver-Stream-Suite"); settings.setValue("music/overlay/main/width",m_width->value()); settings.setValue("music/overlay/main/height",m_height->value());
	obs_source_t *source=obs_get_source_by_name("Music Overlay"); bool created=false;
	if(!source){obs_data_t *initial=obs_data_create(); source=obs_source_create("browser_source","Music Overlay",initial,nullptr); obs_data_release(initial); created=source!=nullptr;}
	if(!source){m_status->setText("OBS Browser Source support is unavailable.");return;}
	obs_data_t *data=obs_source_get_settings(source); obs_data_set_string(data,"url",url.toUtf8().constData()); obs_data_set_bool(data,"is_local_file",false); obs_data_set_int(data,"width",m_width->value()); obs_data_set_int(data,"height",m_height->value()); obs_source_update(source,data); obs_data_release(data);
	if(created){obs_source_t *sceneSource=obs_frontend_get_current_scene(); obs_scene_t *scene=sceneSource?obs_scene_from_source(sceneSource):nullptr; if(scene)obs_scene_add(scene,source); if(sceneSource)obs_source_release(sceneSource);}
	obs_source_release(source); refreshStatus(); m_status->setText(created?"Music Overlay created in the current scene.":"Music Overlay settings updated without changing its OBS transform.");
}
