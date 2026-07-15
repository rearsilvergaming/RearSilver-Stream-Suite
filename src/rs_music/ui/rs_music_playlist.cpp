#include "rs_music_playlist.hpp"
#include "rs_music/state/rs_music_state.hpp"
#include "rs_music/rs_music_controller.hpp"

#include <QFileDialog>
#include <QFileInfo>
#include <QDirIterator>
#include <QFont>
#include <QGridLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

static QLabel *makeTitle(const QString &text)
{
	auto *label = new QLabel(text);
	QFont font = label->font();
	font.setBold(true);
	font.setPointSize(font.pointSize() + 2);
	label->setFont(font);
	return label;
}

RsMusicPlaylist::RsMusicPlaylist(RsMusicState *state, RsMusicController *controller, QWidget *parent)
	: QWidget(parent),
	  m_state(state),
	  m_controller(controller)
{
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(8, 8, 8, 8);
	layout->setSpacing(10);

	layout->addWidget(makeTitle("Music — Local Library"));

	auto *hint = new QLabel(
		"Add audio files stored on this computer. Double-click a track or select it and press Play. "
		"Playback is created as an OBS audio source named ‘Music - Local Files’ for normal mixer and VOD-track routing.");
	hint->setWordWrap(true);
	hint->setStyleSheet("opacity: 0.78;");
	layout->addWidget(hint);

	auto *buttons = new QGridLayout();
	buttons->setHorizontalSpacing(8);
	buttons->setVerticalSpacing(8);
	auto *addFiles = new QPushButton("Add files");
	auto *addFolder = new QPushButton("Add folder");
	m_playSelected = new QPushButton("Play selected");
	m_removeSelected = new QPushButton("Remove");
	m_shuffleButton = new QPushButton("Shuffle order");
	buttons->addWidget(addFiles, 0, 0);
	buttons->addWidget(addFolder, 0, 1);
	buttons->addWidget(m_playSelected, 1, 0);
	buttons->addWidget(m_removeSelected, 1, 1);
	buttons->addWidget(m_shuffleButton, 2, 0, 1, 2);
	layout->addLayout(buttons);

	m_status = new QLabel("No local files added");
	m_status->setWordWrap(true);
	m_status->setStyleSheet("opacity: 0.72; font-size: 11px;");
	layout->addWidget(m_status);

	m_localFiles = new QListWidget();
	m_localFiles->setSelectionMode(QAbstractItemView::SingleSelection);
	m_localFiles->setAlternatingRowColors(true);
	m_localFiles->setMinimumHeight(120);
	layout->addWidget(m_localFiles, 1);

	connect(addFiles, &QPushButton::clicked, this, [this]() { addLocalFiles(); });
	connect(addFolder, &QPushButton::clicked, this, [this]() { addLocalFolder(); });
	connect(m_playSelected, &QPushButton::clicked, this, [this]() { playSelected(); });
	connect(m_removeSelected, &QPushButton::clicked, this, [this]() { removeSelected(); });
	connect(m_localFiles, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) { playSelected(); });
	connect(m_localFiles, &QListWidget::itemSelectionChanged, this, &RsMusicPlaylist::updateFromState);
	connect(m_shuffleButton, &QPushButton::clicked, this, [this]() {
		if (m_controller)
			m_controller->shuffleLocalLibrary();
	});
	if (m_controller) {
		connect(m_controller, &RsMusicController::localLibraryChanged, this, [this]() {
			loadLibrary();
			updateFromState();
		});
	}

	if (m_state)
		connect(m_state, &RsMusicState::stateChanged, this, &RsMusicPlaylist::updateFromState);

	loadLibrary();
	updateFromState();
}

void RsMusicPlaylist::addLocalFiles()
{
	QSettings settings("RearSilver", "RearSilver-Stream-Suite");
	const QString startFolder = settings.value("music/local/lastFolder").toString();
	const QStringList files = QFileDialog::getOpenFileNames(
		this, "Add local music files", startFolder,
		"Audio files (*.mp3 *.flac *.wav *.ogg *.m4a *.aac *.opus *.wma)");
	if (files.isEmpty())
		return;

	settings.setValue("music/local/lastFolder", QFileInfo(files.first()).absolutePath());
	for (const QString &path : files) {
		bool duplicate = false;
		for (int index = 0; index < m_localFiles->count(); ++index) {
			if (m_localFiles->item(index)->data(Qt::UserRole).toString() == path) {
				duplicate = true;
				break;
			}
		}
		if (duplicate)
			continue;

		auto *item = new QListWidgetItem(QFileInfo(path).completeBaseName(), m_localFiles);
		item->setData(Qt::UserRole, QFileInfo(path).absoluteFilePath());
		item->setToolTip(QFileInfo(path).absoluteFilePath());
	}

	saveLibrary();
	updateFromState();
}

void RsMusicPlaylist::addLocalFolder()
{
	QSettings settings("RearSilver", "RearSilver-Stream-Suite");
	const QString folder = QFileDialog::getExistingDirectory(
		this, "Add local music folder", settings.value("music/local/lastFolder").toString());
	if (folder.isEmpty())
		return;

	settings.setValue("music/local/lastFolder", folder);
	const QStringList filters = {"*.mp3", "*.flac", "*.wav", "*.ogg", "*.m4a", "*.aac", "*.opus", "*.wma"};
	QDirIterator files(folder, filters, QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
	while (files.hasNext()) {
		const QString path = QFileInfo(files.next()).absoluteFilePath();
		bool duplicate = false;
		for (int index = 0; index < m_localFiles->count(); ++index) {
			if (m_localFiles->item(index)->data(Qt::UserRole).toString().compare(path, Qt::CaseInsensitive) == 0) {
				duplicate = true;
				break;
			}
		}
		if (!duplicate) {
			auto *item = new QListWidgetItem(QFileInfo(path).completeBaseName(), m_localFiles);
			item->setData(Qt::UserRole, path);
			item->setToolTip(path);
		}
	}
	saveLibrary();
	updateFromState();
}

void RsMusicPlaylist::playSelected()
{
	QListWidgetItem *item = m_localFiles ? m_localFiles->currentItem() : nullptr;
	if (!item || !m_controller)
		return;

	const QString path = item->data(Qt::UserRole).toString();
	if (!m_controller->actionPlayLocalFile(path)) {
		m_status->setText("Could not play this file. It may have been moved, removed, or use an unsupported format.");
	}
}

void RsMusicPlaylist::removeSelected()
{
	if (!m_localFiles)
		return;
	delete m_localFiles->takeItem(m_localFiles->currentRow());
	saveLibrary();
	updateFromState();
}

void RsMusicPlaylist::loadLibrary()
{
	if (!m_controller)
		return;
	m_localFiles->clear();
	const QStringList files = m_controller->localLibrary();
	for (const QString &path : files) {
		auto *item = new QListWidgetItem(QFileInfo(path).completeBaseName(), m_localFiles);
		item->setData(Qt::UserRole, path);
		item->setToolTip(path);
		if (!QFileInfo::exists(path)) {
			item->setText(QString("%1 (missing)").arg(item->text()));
			item->setForeground(Qt::gray);
		}
	}
}

void RsMusicPlaylist::saveLibrary() const
{
	QStringList files;
	for (int index = 0; m_localFiles && index < m_localFiles->count(); ++index)
		files.append(m_localFiles->item(index)->data(Qt::UserRole).toString());
	if (m_controller)
		m_controller->setLocalLibrary(files);
}

void RsMusicPlaylist::updateFromState()
{
	const bool hasSelection = m_localFiles && m_localFiles->currentItem();
	if (m_playSelected)
		m_playSelected->setEnabled(hasSelection && m_controller);
	if (m_removeSelected)
		m_removeSelected->setEnabled(hasSelection);

	if (!m_status || !m_localFiles)
		return;

	if (m_state && m_state->hasCurrentTrack() &&
	    m_state->currentTrack().provider == RsMusicProvider::LocalFile) {
		m_status->setText(QString("Now playing: %1").arg(m_state->currentTrack().title));
	} else if (m_localFiles->count() == 0) {
		m_status->setText("No local files added");
	} else {
		m_status->setText(QString("%1 local track(s) in playback order").arg(m_localFiles->count()));
	}

	const QString currentPath = m_state && m_state->hasCurrentTrack() &&
					    m_state->currentTrack().provider == RsMusicProvider::LocalFile
					? m_state->currentTrack().providerTrackId
					: QString();
	for (int index = 0; index < m_localFiles->count(); ++index) {
		QListWidgetItem *item = m_localFiles->item(index);
		const QString path = item->data(Qt::UserRole).toString();
		const bool current = !currentPath.isEmpty() && path.compare(currentPath, Qt::CaseInsensitive) == 0;
		item->setText(QString("%1%2").arg(current ? "▶ " : "").arg(QFileInfo(path).completeBaseName()));
		QFont font = item->font();
		font.setBold(current);
		item->setFont(font);
	}
}
