#include "rs_music_overlay.hpp"
#include "rs_music/rs_music_server.hpp"
#include <QDesktopServices>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QUrl>
#include <QVBoxLayout>
#include <obs-frontend-api.h>
#include <obs.h>

RsMusicOverlay::RsMusicOverlay(QWidget *parent) : QWidget(parent)
{
	auto *root=new QVBoxLayout(this); root->setContentsMargins(8,8,8,8); root->setSpacing(10);
	auto *title=new QLabel("Music — Overlay"); QFont font=title->font(); font.setBold(true); font.setPointSize(font.pointSize()+2); title->setFont(font); root->addWidget(title);
	auto *intro=new QLabel("Create a browser overlay driven by the Suite’s live provider-neutral music state. This first foundation uses a fixed layout; visual customisation and presets will be added here next."); intro->setWordWrap(true); root->addWidget(intro);
	auto *canvasTitle=new QLabel("Design canvas"); font=canvasTitle->font(); font.setBold(true); canvasTitle->setFont(font); root->addWidget(canvasTitle);
	QSettings settings("RearSilver","RearSilver-Stream-Suite");
	m_width=new QSpinBox(); m_width->setRange(320,3840); m_width->setValue(settings.value("music/overlay/main/width",800).toInt());
	m_height=new QSpinBox(); m_height->setRange(120,2160); m_height->setValue(settings.value("music/overlay/main/height",240).toInt());
	auto *form=new QFormLayout(); form->addRow("Width:",m_width); form->addRow("Height:",m_height); root->addLayout(form);
	m_url=new QLabel(); m_url->setWordWrap(true); m_url->setTextInteractionFlags(Qt::TextSelectableByMouse); root->addWidget(m_url);
	auto *row=new QHBoxLayout(); auto *preview=new QPushButton("Open full preview"); auto *source=new QPushButton("Create or update Music Overlay source"); source->setObjectName("rs-primary-button"); row->addWidget(preview); row->addWidget(source); root->addLayout(row);
	m_status=new QLabel(); m_status->setWordWrap(true); m_status->setStyleSheet("opacity:0.75; font-size:11px;"); root->addWidget(m_status); root->addStretch();
	connect(preview,&QPushButton::clicked,this,[]{QDesktopServices::openUrl(QUrl(RsMusicServer::instance().overlayUrl()));});
	connect(source,&QPushButton::clicked,this,&RsMusicOverlay::createOrUpdateSource);
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
