#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

/*
 * RsMusicTwitchAuth
 *
 * Handles Twitch Device Code OAuth for the STREAMER account.
 * - Read chat + (optionally later) send chat
 * - Stores token via QSettings
 * - No UI ownership
 * - No IRC ownership
 */

class RsMusicTwitchAuth : public QObject {
	Q_OBJECT

public:
	explicit RsMusicTwitchAuth(const QString &settingsRoot, QObject *parent = nullptr);

	// ---- lifecycle ----
	bool hasValidToken() const;
	QString accessToken() const;
	QString userLogin() const;
	QString userId() const;

// ---- OAuth ----
	void beginDeviceAuth(); // starts device flow
	void clearAuth();       // forget token
	void reconnect();       // reuse stored token if possible

signals:

	// Emitted when starting connection process
	void connecting();

	// Emitted when browser login is required
	void deviceCodeReady(const QString &userCode, const QString &verificationUrl);

	// Emitted when authentication fully completes
	void authCompleted();

	// Emitted when the Twitch user identity has been resolved
	void identityResolved(const QString &displayName);

	// Emitted when the user explicitly logs out
	void loggedOut();

	// Emitted on fatal auth error
	void authFailed(const QString &reason);

private slots:
	void pollForToken();

private:
	void loadFromSettings();
	void saveToSettings();
	void clearSettings();

	// OAuth helpers
	void requestDeviceCode();
	void requestAccessToken();

private:
	// ---- persisted ----
	QString m_settingsRoot;
	QString m_accessToken;
	QString m_refreshToken;
	QString m_userLogin;
	QString m_userId;

	// ---- device flow ----
	QString m_deviceCode;
	QString m_userCode;
	QString m_verifyUrl;
	int m_pollIntervalSec = 5;

	QTimer m_pollTimer;
};
