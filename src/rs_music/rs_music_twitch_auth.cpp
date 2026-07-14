
#include "rs_music_twitch_auth.hpp"

#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QUrl>
#include <QUrlQuery>
#include <QSslSocket>

extern "C" {
#include <obs-module.h>
}

// Public client identifier for the RearSilver Stream Suite Twitch application.
// OAuth client IDs are identifiers, not secrets; no client secret is embedded.
static const char *TWITCH_CLIENT_ID = "6h5j0d7kfjaeyw6fejisawwqheeahd";

// Twitch endpoints
static const char *DEVICE_CODE_URL = "https://id.twitch.tv/oauth2/device";
static const char *TOKEN_URL = "https://id.twitch.tv/oauth2/token";

RsMusicTwitchAuth::RsMusicTwitchAuth(const QString &settingsRoot, QObject *parent)
	: QObject(parent),
	  m_settingsRoot(settingsRoot)
{
	loadFromSettings();

	connect(&m_pollTimer, &QTimer::timeout, this, &RsMusicTwitchAuth::pollForToken);
	m_deviceExpiryTimer.setSingleShot(true);
	connect(&m_deviceExpiryTimer, &QTimer::timeout, this, [this]() {
		m_pollTimer.stop();
		m_deviceCode.clear();
		emit authFailed("Twitch login request expired. Please try again.");
	});
}


bool RsMusicTwitchAuth::hasValidToken() const
{
	return !m_accessToken.isEmpty();
}

QString RsMusicTwitchAuth::accessToken() const
{
	return m_accessToken;
}

QString RsMusicTwitchAuth::userLogin() const
{
	return m_userLogin;
}

QString RsMusicTwitchAuth::userId() const
{
	return m_userId;
}

void RsMusicTwitchAuth::beginDeviceAuth()
{
	m_pollTimer.stop();
	m_deviceExpiryTimer.stop();
	m_deviceCode.clear();
	emit connecting();
	requestDeviceCode();
}

void RsMusicTwitchAuth::clearAuth()
{
	m_pollTimer.stop();
	m_deviceExpiryTimer.stop();
	clearSettings();

	m_accessToken.clear();
	m_refreshToken.clear();
	m_userLogin.clear();
	m_userId.clear();

	emit loggedOut();
}

void RsMusicTwitchAuth::requestDeviceCode()
{
	QNetworkAccessManager *net = new QNetworkAccessManager(this);

	QUrl url(DEVICE_CODE_URL);
	QUrlQuery q;
	q.addQueryItem("client_id", TWITCH_CLIENT_ID);
	q.addQueryItem("scope", "chat:read chat:edit");
	url.setQuery(q);

	QNetworkRequest req(url);

	QNetworkReply *reply = net->post(req, QByteArray());
	connect(reply, &QNetworkReply::finished, this, [this, reply, net]() {
		net->deleteLater();
		reply->deleteLater();

		if (reply->error() != QNetworkReply::NoError) {
			const QString err = reply->errorString();
			blog(LOG_ERROR, "[RS Music] Device code request failed: %s", err.toUtf8().constData());
			blog(LOG_ERROR, "[RS Music] QSslSocket::supportsSsl=%s",
			     QSslSocket::supportsSsl() ? "true" : "false");
			emit authFailed("Failed to request device code");
			return;
		}


		QJsonObject json = QJsonDocument::fromJson(reply->readAll()).object();

		m_deviceCode = json["device_code"].toString();
		m_userCode = json["user_code"].toString();
		m_verifyUrl = json["verification_uri"].toString();
		m_pollIntervalSec = json["interval"].toInt(5);
		const int expiresInSec = json["expires_in"].toInt(600);

		if (m_deviceCode.isEmpty() || m_userCode.isEmpty() || m_verifyUrl.isEmpty()) {
			emit authFailed("Twitch returned an incomplete login request");
			return;
		}

		emit deviceCodeReady(m_userCode, m_verifyUrl);

		QDesktopServices::openUrl(QUrl(m_verifyUrl));

		m_pollTimer.start(m_pollIntervalSec * 1000);
		m_deviceExpiryTimer.start(qMax(1, expiresInSec) * 1000);
	});
}

void RsMusicTwitchAuth::pollForToken()
{
	requestAccessToken();
}

void RsMusicTwitchAuth::requestAccessToken()
{
	QNetworkAccessManager *net = new QNetworkAccessManager(this);

	QUrl url(TOKEN_URL);
	QUrlQuery q;
	q.addQueryItem("client_id", TWITCH_CLIENT_ID);
	q.addQueryItem("device_code", m_deviceCode);
	q.addQueryItem("grant_type", "urn:ietf:params:oauth:grant-type:device_code");
	url.setQuery(q);

	QNetworkRequest req(url);

	QNetworkReply *reply = net->post(req, QByteArray());
	connect(reply, &QNetworkReply::finished, this, [this, reply, net]() {
		net->deleteLater();
		reply->deleteLater();

		const QJsonObject json = QJsonDocument::fromJson(reply->readAll()).object();
		if (reply->error() != QNetworkReply::NoError) {
			const QString reason = json["message"].toString(json["error"].toString());

			if (reason == "authorization_pending")
				return;

			if (reason == "slow_down") {
				m_pollIntervalSec += 5;
				m_pollTimer.start(m_pollIntervalSec * 1000);
				return;
			}

			m_pollTimer.stop();
			m_deviceExpiryTimer.stop();
			m_deviceCode.clear();

			if (reason == "access_denied")
				emit authFailed("Twitch login was denied");
			else if (reason == "expired_token")
				emit authFailed("Twitch login request expired. Please try again.");
			else
				emit authFailed("Twitch login failed");
			return;
		}

		if (!json.contains("access_token"))
			return;

		m_pollTimer.stop();
		m_deviceExpiryTimer.stop();
		m_deviceCode.clear();

		m_accessToken = json["access_token"].toString();
		m_refreshToken = json["refresh_token"].toString();

		saveToSettings();

		// ---------------------------------------------
		// Resolve Twitch user identity (who this token belongs to)
		// ---------------------------------------------
		QNetworkAccessManager *userNet = new QNetworkAccessManager(this);

		QNetworkRequest userReq(QUrl("https://api.twitch.tv/helix/users"));
		userReq.setRawHeader("Authorization", QByteArray("Bearer ") + m_accessToken.toUtf8());
		userReq.setRawHeader("Client-Id", TWITCH_CLIENT_ID);

		QNetworkReply *userReply = userNet->get(userReq);
		connect(userReply, &QNetworkReply::finished, this, [this, userReply, userNet]() {
			userNet->deleteLater();
			userReply->deleteLater();

			if (userReply->error() != QNetworkReply::NoError) {
				blog(LOG_ERROR, "[RS Music] Failed to fetch Twitch user identity: %s",
				     userReply->errorString().toUtf8().constData());
				emit authCompleted();
				return;
			}

			const QJsonObject obj = QJsonDocument::fromJson(userReply->readAll()).object();

			const QJsonArray data = obj["data"].toArray();
			if (data.isEmpty()) {
				emit authCompleted();
				return;
			}

			const QJsonObject user = data.first().toObject();

			m_userId = user["id"].toString();
			m_userLogin = user["login"].toString();
			const QString displayName = user["display_name"].toString();

			saveToSettings();

			emit identityResolved(displayName);
			emit authCompleted();
		});

	});
}
void RsMusicTwitchAuth::loadFromSettings()
{
	QSettings s;
	m_accessToken = s.value(m_settingsRoot + "/access_token").toString();
	m_refreshToken = s.value(m_settingsRoot + "/refresh_token").toString();
	m_userLogin = s.value(m_settingsRoot + "/login").toString();
	m_userId = s.value(m_settingsRoot + "/user_id").toString();
}

void RsMusicTwitchAuth::saveToSettings()
{
	QSettings s;
	s.setValue(m_settingsRoot + "/access_token", m_accessToken);
	s.setValue(m_settingsRoot + "/refresh_token", m_refreshToken);
	s.setValue(m_settingsRoot + "/login", m_userLogin);
	s.setValue(m_settingsRoot + "/user_id", m_userId);
}

void RsMusicTwitchAuth::clearSettings()
{
	QSettings s;
	s.remove(m_settingsRoot);
}

void RsMusicTwitchAuth::reconnect()
{
	emit connecting();
	// If we don't have a token, reconnect is impossible
	if (m_accessToken.isEmpty()) {
		emit authFailed("No saved Twitch login");
		return;
	}

	// Re-validate token by asking Twitch who this token belongs to
	QNetworkAccessManager *net = new QNetworkAccessManager(this);

	QNetworkRequest req(QUrl("https://api.twitch.tv/helix/users"));
	req.setRawHeader("Authorization", QByteArray("Bearer ") + m_accessToken.toUtf8());
	req.setRawHeader("Client-Id", TWITCH_CLIENT_ID);

	QNetworkReply *reply = net->get(req);
	connect(reply, &QNetworkReply::finished, this, [this, reply, net]() {
		net->deleteLater();
		reply->deleteLater();

		if (reply->error() != QNetworkReply::NoError) {
			clearAuth();
			emit authFailed("Saved Twitch login is no longer valid");
			return;
		}

		const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();

		const QJsonArray data = obj["data"].toArray();
		if (data.isEmpty()) {
			clearAuth();
			emit authFailed("Saved Twitch login is no longer valid");
			return;
		}

		const QJsonObject user = data.first().toObject();

		m_userId = user["id"].toString();
		m_userLogin = user["login"].toString();
		const QString displayName = user["display_name"].toString();

		saveToSettings();

		emit identityResolved(displayName);
		emit authCompleted();
	});
}
