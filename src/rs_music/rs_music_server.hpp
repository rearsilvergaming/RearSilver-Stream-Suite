#pragma once

#include <QObject>

// ============================================================================
// DEPRECATED - DO NOT USE
// ----------------------------------------------------------------------------
// This file previously implemented a local HTTP server for music playback
// (127.0.0.1 polling + embedded HTML).
//
// That architecture has been permanently abandoned in favour of:
//   - Hosted player (Cloudflare Pages)
//   - WebSocket control plane (Cloudflare Workers)
//
// This class is intentionally inert.
// It exists only to avoid breaking old includes while the project migrates.
//
// Any attempt to use this class will log an error and do nothing.
// ============================================================================

class RsMusicServer : public QObject {
	Q_OBJECT

public:
	static RsMusicServer &instance();

	// All methods are no-ops and return safe defaults.
	bool start();
	void stop();

	unsigned short port() const;

	void setState(const void *);
	void *state() const;

	void setPlayerHtml(const QString &);
	QString playerHtml() const;

private:
	explicit RsMusicServer(QObject *parent = nullptr);
};
