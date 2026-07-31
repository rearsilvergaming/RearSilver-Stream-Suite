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
static const char *TOKEN_VALIDATE_URL = "https://id.twitch.tv/oauth2/validate";
static const char *CHAT_SCOPES = "chat:read chat:edit";

static bool hasRequiredChatScopes(const QJsonArray &scopes)
{
	bool canRead = false;
	bool canEdit = false;
	for (const QJsonValue &scope : scopes) {
		canRead = canRead || scope.toString() == "chat:read";
		canEdit = canEdit || scope.toString() == "chat:edit";
	}
	return canRead && canEdit;
}

RsMusicTwitchAuth::RsMusicTwitchAuth(const QString &settingsRoot, QObject *parent)
	: QObject(parent),
	  m_settingsRoot(settingsRoot)
{
	// OAuth persistence and refresh are owned by the companion Media Player.
	// The OBS plugin receives only a validated transient access session over IPC.

	connect(&m_pollTimer, &QTimer::timeout, this, &RsMusicTwitchAuth::pollForToken);
	m_deviceExpiryTimer.setSingleShot(true);
	connect(&m_deviceExpiryTimer, &QTimer::timeout, this, [this]() {
		m_pollTimer.stop();
		m_deviceCode.clear();
		emit authFailed("Twitch login request expired. Please try again.");
	});
	m_validationTimer.setInterval(60 * 60 * 1000);
	connect(&m_validationTimer, &QTimer::timeout, this, &RsMusicTwitchAuth::reconnect);
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
	m_validationTimer.stop();
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
	// Twitch's device-code flow deliberately uses `scopes` (plural), unlike
	// the implicit/authorization-code flows which use `scope`.
	q.addQueryItem("scopes", CHAT_SCOPES);
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
	q.addQueryItem("scopes", CHAT_SCOPES);
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
		if (!hasRequiredChatScopes(json["scope"].toArray())) {
			blog(LOG_ERROR, "[RS Music] Twitch device login returned without chat:read/chat:edit scopes");
			m_accessToken.clear();
			m_refreshToken.clear();
			clearSettings();
			emit authFailed("Twitch did not grant the required chat permissions. Please log in again.");
			return;
		}

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
	QSettings s("RearSilver", "RearSilver-Stream-Suite");
	m_accessToken = s.value(m_settingsRoot + "/access_token").toString();
	m_refreshToken = s.value(m_settingsRoot + "/refresh_token").toString();
	m_userLogin = s.value(m_settingsRoot + "/login").toString();
	m_userId = s.value(m_settingsRoot + "/user_id").toString();
}

void RsMusicTwitchAuth::saveToSettings()
{
	QSettings s("RearSilver", "RearSilver-Stream-Suite");
	s.setValue(m_settingsRoot + "/access_token", m_accessToken);
	s.setValue(m_settingsRoot + "/refresh_token", m_refreshToken);
	s.setValue(m_settingsRoot + "/login", m_userLogin);
	s.setValue(m_settingsRoot + "/user_id", m_userId);
	s.sync();
}

void RsMusicTwitchAuth::clearSettings()
{
	QSettings s("RearSilver", "RearSilver-Stream-Suite");
	s.remove(m_settingsRoot);
	s.sync();
}

void RsMusicTwitchAuth::reconnect()
{
	emit connecting();
	// If we don't have a token, reconnect is impossible
	if (m_accessToken.isEmpty()) {
		emit authFailed("No saved Twitch login");
		return;
	}

	// Validate the token itself so account identity, expiry and granted scopes
	// are checked before the UI claims chat is connected.
	QNetworkAccessManager *net = new QNetworkAccessManager(this);

	QNetworkRequest req{QUrl(TOKEN_VALIDATE_URL)};
	req.setRawHeader("Authorization", QByteArray("OAuth ") + m_accessToken.toUtf8());

	QNetworkReply *reply = net->get(req);
	connect(reply, &QNetworkReply::finished, this, [this, reply, net]() {
		net->deleteLater();
		const QByteArray responseBody = reply->readAll();
		const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		const QNetworkReply::NetworkError networkError = reply->error();
		const QString networkErrorText = reply->errorString();
		reply->deleteLater();

		if (networkError != QNetworkReply::NoError) {
			const QJsonObject error = QJsonDocument::fromJson(responseBody).object();
			blog(LOG_WARNING, "[RS Music] Twitch token validation failed (HTTP %d): %s",
			     httpStatus, error["message"].toString(networkErrorText).toUtf8().constData());
			if (httpStatus == 401 && !m_refreshToken.isEmpty()) {
				refreshAccessToken();
				return;
			}
			if (httpStatus == 401) {
				// Keep the saved identity and token material. A user should only be
				// logged out explicitly or after Twitch rejects the refresh token;
				// an access-token validation failure is recoverable.
				emit authFailed("Saved Twitch login needs reconnecting.");
			} else {
				emit authFailed("Could not reach Twitch. Use Reconnect to try again.");
			}
			return;
		}

		const QJsonObject obj = QJsonDocument::fromJson(responseBody).object();
		if (!hasRequiredChatScopes(obj["scopes"].toArray())) {
			clearAuth();
			emit authFailed("Saved Twitch login lacks chat permissions. Please log in again.");
			return;
		}

		m_userId = obj["user_id"].toString();
		m_userLogin = obj["login"].toString();
		const QString displayName = m_userLogin;

		saveToSettings();

		emit identityResolved(displayName);
		emit authCompleted();
	});
}

void RsMusicTwitchAuth::adoptSession(const QString &accessToken, const QString &login, const QString &userId)
{
	m_pollTimer.stop(); m_deviceExpiryTimer.stop(); m_validationTimer.stop();
	m_accessToken = accessToken; m_refreshToken.clear(); m_userLogin = login; m_userId = userId;
	emit identityResolved(login); emit authCompleted();
}

void RsMusicTwitchAuth::clearTransientSession()
{
	m_pollTimer.stop(); m_deviceExpiryTimer.stop(); m_validationTimer.stop();
	m_accessToken.clear(); m_refreshToken.clear(); m_userLogin.clear(); m_userId.clear();
	emit loggedOut();
}

void RsMusicTwitchAuth::refreshAccessToken()
{
	if (m_refreshToken.isEmpty()) {
		clearAuth();
		emit authFailed("Saved Twitch login expired. Please log in again.");
		return;
	}

	blog(LOG_INFO, "[RS Music] Refreshing expired Twitch device-code access token.");
	QNetworkAccessManager *net = new QNetworkAccessManager(this);
	QUrl url(TOKEN_URL);
	QUrlQuery query;
	query.addQueryItem("client_id", TWITCH_CLIENT_ID);
	query.addQueryItem("grant_type", "refresh_token");
	query.addQueryItem("refresh_token", m_refreshToken);
	url.setQuery(query);
	QNetworkRequest request(url);
	QNetworkReply *reply = net->post(request, QByteArray());
	connect(reply, &QNetworkReply::finished, this, [this, reply, net]() {
		net->deleteLater();
		const QByteArray responseBody = reply->readAll();
		const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		const QNetworkReply::NetworkError replyError = reply->error();
		const QString networkError = reply->errorString();
		reply->deleteLater();
		const QJsonObject json = QJsonDocument::fromJson(responseBody).object();
		if (replyError != QNetworkReply::NoError || !json.contains("access_token")) {
			blog(LOG_WARNING, "[RS Music] Twitch token refresh failed (HTTP %d): %s", httpStatus,
			     json["message"].toString(networkError).toUtf8().constData());
			const QString oauthError = json["error"].toString();
			const QString message = json["message"].toString();
			const bool refreshRejected = httpStatus == 400 || httpStatus == 401 ||
				oauthError == "invalid_grant" || message.contains("refresh", Qt::CaseInsensitive);
			if (refreshRejected) {
				clearAuth();
				emit authFailed("Saved Twitch login expired. Please log in again.");
			} else {
				// Preserve the refresh token across temporary DNS/TLS/server errors.
				// Reconnect remains available and the next OBS launch retries it.
				emit authFailed("Could not refresh Twitch right now. Use Reconnect to try again.");
			}
			return;
		}
		if (!hasRequiredChatScopes(json["scope"].toArray())) {
			clearAuth();
			emit authFailed("Refreshed Twitch login lacks chat permissions. Please log in again.");
			return;
		}

		// Twitch device-code refresh tokens are single-use. Persist the newly
		// rotated pair together before validating the replacement access token.
		m_accessToken = json["access_token"].toString();
		const QString rotatedRefreshToken = json["refresh_token"].toString();
		if (!rotatedRefreshToken.isEmpty()) m_refreshToken = rotatedRefreshToken;
		saveToSettings();
		m_validationTimer.start();
		blog(LOG_INFO, "[RS Music] Twitch access token refreshed successfully.");
		reconnect();
	});
}

