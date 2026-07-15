#include "rs_music_setup.hpp"
#include "enhancements/rs_auto_start.hpp"
#include "rs_music/rs_music_local_player.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <obs-frontend-api.h>
#include <obs.h>

static bool collectCaptureSources(void *data, obs_source_t *source)
{
	auto *combo = static_cast<QComboBox *>(data);
	if (QString::fromUtf8(obs_source_get_id(source)) == "wasapi_process_output_capture")
		combo->addItem(QString::fromUtf8(obs_source_get_name(source)));
	return true;
}

RsMusicSetup::RsMusicSetup(QWidget *parent) : QWidget(parent)
{
	auto *root = new QVBoxLayout(this); root->setContentsMargins(8,8,8,8); root->setSpacing(10);
	auto *title = new QLabel("Music — Setup"); QFont f=title->font(); f.setBold(true); f.setPointSize(f.pointSize()+2); title->setFont(f); root->addWidget(title);
	auto *intro = new QLabel("Follow these steps once to route music from the bundled player, Spotify, YouTube Music, or another application into OBS. The Suite will not change your audio tracks or VOD-track routing."); intro->setWordWrap(true); root->addWidget(intro);
	auto *step1 = new QLabel("1. Create or choose the OBS audio source"); QFont sf=step1->font(); sf.setBold(true); step1->setFont(sf); root->addWidget(step1);
	auto *createHelp = new QLabel("New setup: create a neutral Application Audio Capture source named ‘Music Capture’. Its OBS Properties window will open automatically."); createHelp->setWordWrap(true); createHelp->setStyleSheet("opacity:0.78;"); root->addWidget(createHelp);
	auto *create = new QPushButton("Create Music Capture and choose application"); create->setObjectName("rs-primary-button"); root->addWidget(create);
	auto *existingHelp = new QLabel("Already have an Application Audio Capture source for music? Choose that existing source here instead:"); existingHelp->setWordWrap(true); root->addWidget(existingHelp);
	m_sources = new QComboBox(); m_sources->setToolTip("Existing OBS Application Audio Capture sources"); root->addWidget(m_sources);
	auto *refresh = new QPushButton("Refresh existing source list"); root->addWidget(refresh);
	auto *step2 = new QLabel("2. Choose the application OBS should capture"); step2->setFont(sf); root->addWidget(step2);
	auto *applicationHelp = new QLabel("Open the selected source’s OBS Properties, then choose RearSilver Stream Suite | Media Player—or another music application—from the Window list."); applicationHelp->setWordWrap(true); applicationHelp->setStyleSheet("opacity:0.78;"); root->addWidget(applicationHelp);
	auto *properties = new QPushButton("Choose application for selected source"); root->addWidget(properties);
	m_status = new QLabel(); m_status->setWordWrap(true); m_status->setStyleSheet("opacity:0.75; font-size:11px;"); root->addWidget(m_status);
	auto *line = new QFrame(); line->setFrameShape(QFrame::HLine); root->addWidget(line);
	auto *step3 = new QLabel("3. Optional: start the bundled player with OBS"); step3->setFont(sf); root->addWidget(step3);
	m_autoStart = new QCheckBox("Add RearSilver Stream Suite Media Player to Auto-Start Manager"); root->addWidget(m_autoStart);
	auto *autoHint = new QLabel("Adds the bundled local player to the Suite’s existing Auto-Start Manager. The manager’s global ‘Automatically launch programs’ option must also be enabled."); autoHint->setWordWrap(true); autoHint->setStyleSheet("opacity:0.75; font-size:11px;"); root->addWidget(autoHint);
	auto *routing = new QLabel("VOD routing: open OBS Settings → Output and Advanced Audio Properties to choose your own stream/VOD tracks. The Suite deliberately leaves these settings untouched."); routing->setWordWrap(true); root->addWidget(routing); root->addStretch();
	connect(refresh,&QPushButton::clicked,this,&RsMusicSetup::refreshSources);
	connect(create,&QPushButton::clicked,this,&RsMusicSetup::createCaptureSource);
	connect(properties,&QPushButton::clicked,this,&RsMusicSetup::openSelectedProperties);
	connect(m_sources,&QComboBox::currentTextChanged,this,[this](const QString &name){
		if(!name.isEmpty()) m_status->setText(QString("Selected source: %1. Next, choose which application it captures.").arg(name));
	});
	const QString player = RsMusicLocalPlayer::instance().executablePath();
	m_autoStart->setChecked(!player.isEmpty() && RsAutoStart::containsProgram(player));
	connect(m_autoStart,&QCheckBox::toggled,this,[this,player](bool enabled){
		if (player.isEmpty()) { m_autoStart->setChecked(false); m_status->setText("The bundled player executable could not be found."); return; }
		if (enabled) RsAutoStart::addProgram(player); else RsAutoStart::removeProgram(player);
	});
	refreshSources();
}

void RsMusicSetup::refreshSources()
{
	const QString selected=m_sources->currentText(); m_sources->clear(); obs_enum_sources(collectCaptureSources,m_sources);
	int index=m_sources->findText(selected); if(index>=0)m_sources->setCurrentIndex(index);
	m_status->setText(m_sources->count()?QString("Selected source: %1. Next, choose which application it captures.").arg(m_sources->currentText()):"No existing Application Audio Capture source was found. Use the Create Music Capture button above.");
}

void RsMusicSetup::createCaptureSource()
{
	if(obs_source_t *existing=obs_get_source_by_name("Music Capture")){ obs_source_release(existing); refreshSources(); m_sources->setCurrentText("Music Capture"); m_status->setText("Music Capture already exists. Choose its music application in OBS Properties."); openSelectedProperties(); return; }
	obs_source_t *sceneSource=obs_frontend_get_current_scene(); obs_scene_t *scene=sceneSource?obs_scene_from_source(sceneSource):nullptr;
	obs_source_t *source=obs_source_create("wasapi_process_output_capture","Music Capture",nullptr,nullptr);
	if(source&&scene){ obs_scene_add(scene,source); m_status->setText("Music Capture created. Choose your music application in the OBS Properties window."); }
	else m_status->setText("Could not create Music Capture in the current scene.");
	if(source)obs_source_release(source); if(sceneSource)obs_source_release(sceneSource); refreshSources(); m_sources->setCurrentText("Music Capture"); openSelectedProperties();
}

void RsMusicSetup::openSelectedProperties()
{
	if(m_sources->currentText().isEmpty())return; obs_source_t *source=obs_get_source_by_name(m_sources->currentText().toUtf8().constData());
	if(source){obs_frontend_open_source_properties(source);obs_source_release(source);}
}
