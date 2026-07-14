#include "rs_stats.hpp"
#include "rs_main_dock.hpp"

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTimer>
#include <QFont>
#include <QFrame>

#include <obs-frontend-api.h>
#include <obs.h>
#include <util/platform.h>

QString RsStats::formatPercent(int part, int total)
{
	if (total <= 0)
		return "0.0%";
	double pct = double(part) * 100.0 / double(total);
	return QString::number(pct, 'f', 1) + "%";
}

QString RsStats::formatBytesMB(uint64_t bytes)
{
	double mb = double(bytes) / (1024.0 * 1024.0);
	return QString::number(mb, 'f', 1) + " MiB";
}

QString RsStats::formatMs(double ms)
{
	return QString::number(ms, 'f', 1) + " ms";
}

static QLabel *makeRow(QVBoxLayout *layout, const QString &label)
{
	QHBoxLayout *row = new QHBoxLayout();
	QLabel *lbl = new QLabel(label);
	QLabel *val = new QLabel("—");

	lbl->setMinimumWidth(140); // keeps text aligned
	row->addWidget(lbl);
	row->addWidget(val, 1);
	layout->addLayout(row);

	return val;
}

QWidget *RsStats::createStatsPage(RsMainDock *, QWidget *parent)
{
	QWidget *page = new QWidget(parent);
	page->setObjectName("rs-card");

	auto *layout = new QVBoxLayout(page);
	layout->setContentsMargins(8, 8, 8, 8);
	layout->setSpacing(6);

	// ---- Title ----
	auto *title = new QLabel("Stats");
	QFont tf = title->font();
	tf.setBold(true);
	tf.setPointSize(tf.pointSize() + 1);
	title->setFont(tf);
	layout->addWidget(title);

	// ---- Rendering Stats Section ----
	auto *renderHeader = new QLabel("Rendering");
	QFont hf = renderHeader->font();
	hf.setBold(true);
	renderHeader->setFont(hf);
	layout->addWidget(renderHeader);

	QLabel *valFps = makeRow(layout, "FPS:");
	QLabel *valRenderTime = makeRow(layout, "Render time:");
	QLabel *valRenderMiss = makeRow(layout, "Missed frames:");
	QLabel *valSkipped = makeRow(layout, "Encoding skipped:");

	// Divider
	auto *div1 = new QFrame();
	div1->setFrameShape(QFrame::HLine);
	div1->setFrameShadow(QFrame::Sunken);
	layout->addWidget(div1);

	// ---- Output Stats ----
	auto *outHeader = new QLabel("Outputs");
	outHeader->setFont(hf);
	layout->addWidget(outHeader);

	auto *streamTitle = new QLabel("Stream");
	streamTitle->setFont(hf);
	layout->addWidget(streamTitle);

	QLabel *streamStatus = makeRow(layout, "Status:");
	QLabel *streamDrop = makeRow(layout, "Dropped:");
	QLabel *streamData = makeRow(layout, "Data out:");
	QLabel *streamRate = makeRow(layout, "Bitrate:");

	auto *recTitle = new QLabel("Recording");
	recTitle->setFont(hf);
	layout->addWidget(recTitle);

	QLabel *recStatus = makeRow(layout, "Status:");
	QLabel *recDrop = makeRow(layout, "Dropped:");
	QLabel *recData = makeRow(layout, "Data out:");
	QLabel *recRate = makeRow(layout, "Bitrate:");

	layout->addStretch();

	// ---- Timer ----
	auto *timer = new QTimer(page);
	timer->setInterval(1000);

	struct BitrateState {
		uint64_t lastBytes = 0;
		uint64_t lastTime = 0;
	};
	static BitrateState streamSt;
	static BitrateState recordSt;

	QObject::connect(timer, &QTimer::timeout, page, [=]() mutable {
		// ---- Rendering stats ----
		if (video_t *video = obs_get_video()) {

			int skipped = video_output_get_skipped_frames(video);
			int total = video_output_get_total_frames(video);

			valSkipped->setText(
				QString("%1 / %2 (%3)").arg(skipped).arg(total).arg(formatPercent(skipped, total)));

			double fps = obs_get_active_fps();
			valFps->setText(QString::number(fps, 'f', 1));

			if (fps > 0.0)
				valRenderTime->setText(formatMs(1000.0 / fps));
			else
				valRenderTime->setText("—");

			valRenderMiss->setText("N/A");
		} else {
			valFps->setText("—");
			valRenderTime->setText("—");
			valRenderMiss->setText("—");
			valSkipped->setText("—");
		}

		// ---- Output helper ----
		auto update = [&](obs_output_t *output, QLabel *st, QLabel *dr, QLabel *dt, QLabel *br,
				  BitrateState &bs) {
			if (!output) {
				st->setText("Inactive");
				dr->setText("0 / 0 (0.0%)");
				dt->setText("0 MiB");
				br->setText("0 kb/s");
				bs.lastBytes = 0;
				bs.lastTime = 0;
				return;
			}

			st->setText("Active");

			uint32_t dropped = obs_output_get_frames_dropped(output);
			uint32_t total = obs_output_get_total_frames(output);

			dr->setText(QString("%1 / %2 (%3)").arg(dropped).arg(total).arg(formatPercent(dropped, total)));

			uint64_t bytes = obs_output_get_total_bytes(output);
			dt->setText(formatBytesMB(bytes));

			uint64_t now = os_gettime_ns();
			if (!bs.lastTime || bytes < bs.lastBytes) {
				bs.lastTime = now;
				bs.lastBytes = bytes;
				br->setText("0 kb/s");
			} else {
				uint64_t bits = (bytes - bs.lastBytes) * 8ULL;
				double sec = double(now - bs.lastTime) / 1e9;
				double kbps = sec > 0 ? (bits / sec) / 1000.0 : 0.0;
				br->setText(QString::number(kbps, 'f', 0) + " kb/s");
				bs.lastTime = now;
				bs.lastBytes = bytes;
			}
		};

		// ---- Stream ----
		if (auto *o = obs_frontend_get_streaming_output()) {
			update(o, streamStatus, streamDrop, streamData, streamRate, streamSt);
			obs_output_release(o);
		} else {
			update(nullptr, streamStatus, streamDrop, streamData, streamRate, streamSt);
		}

		// ---- Recording ----
		if (auto *o = obs_frontend_get_recording_output()) {
			update(o, recStatus, recDrop, recData, recRate, recordSt);
			obs_output_release(o);
		} else {
			update(nullptr, recStatus, recDrop, recData, recRate, recordSt);
		}
	});

	timer->start();
	return page;
}
