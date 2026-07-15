#include "rs_music_helpers.hpp"

#include <QRegularExpression>
#include <QString>

// ---------------------------------------------
// Channel / Room helpers
// ---------------------------------------------

QString rsMusicNormaliseChannelName(const QString &input)
{
	// Lowercase, trim, no spaces
	QString out = input.trimmed().toLower();
	out.replace(' ', '_');
	return out;
}

// ---------------------------------------------
// YouTube URL helpers
// ---------------------------------------------

// Extract a YouTube video ID from common URL forms.
// Returns empty string if not a recognised video URL.
QString rsMusicExtractYoutubeVideoId(const QString &input)
{
	const QString s = input.trimmed();

	// youtu.be/<id>
	static const QRegularExpression shortRe(R"(https?:\/\/youtu\.be\/([A-Za-z0-9_-]{6,}))",
						QRegularExpression::CaseInsensitiveOption);

	// youtube.com/watch?v=<id>
	static const QRegularExpression watchRe(R"(https?:\/\/(www\.)?youtube\.com\/watch\?[^#]*v=([A-Za-z0-9_-]{6,}))",
						QRegularExpression::CaseInsensitiveOption);

	// music.youtube.com/watch?v=<id>
	static const QRegularExpression musicRe(R"(https?:\/\/music\.youtube\.com\/watch\?[^#]*v=([A-Za-z0-9_-]{6,}))",
						QRegularExpression::CaseInsensitiveOption);

	QRegularExpressionMatch m;

	m = shortRe.match(s);
	if (m.hasMatch())
		return m.captured(1);

	m = watchRe.match(s);
	if (m.hasMatch())
		return m.captured(2);

	m = musicRe.match(s);
	if (m.hasMatch())
		return m.captured(1);

	return QString();
}

QString rsMusicExtractSpotifyTrackId(const QString &input)
{
	const QString s = input.trimmed();
	static const QRegularExpression webRe(
		R"(https?:\/\/(?:open\.)?spotify\.com\/track\/([A-Za-z0-9]+))",
		QRegularExpression::CaseInsensitiveOption);
	static const QRegularExpression uriRe(R"(^spotify:track:([A-Za-z0-9]+)$)",
					       QRegularExpression::CaseInsensitiveOption);

	QRegularExpressionMatch match = webRe.match(s);
	if (match.hasMatch())
		return match.captured(1);

	match = uriRe.match(s);
	if (match.hasMatch())
		return match.captured(1);

	return QString();
}

RsMusicRequestTarget rsMusicParseRequestTarget(const QString &input)
{
	RsMusicRequestTarget target;
	const QString trimmed = input.trimmed();

	const QString youtubeId = rsMusicExtractYoutubeVideoId(trimmed);
	if (!youtubeId.isEmpty()) {
		target.provider = RsMusicProvider::YouTube;
		target.providerTrackId = youtubeId;
		target.providerUri = QString("https://www.youtube.com/watch?v=%1").arg(youtubeId);
		target.directTrack = true;
		return target;
	}

	const QString spotifyId = rsMusicExtractSpotifyTrackId(trimmed);
	if (!spotifyId.isEmpty()) {
		target.provider = RsMusicProvider::Spotify;
		target.providerTrackId = spotifyId;
		target.providerUri = QString("spotify:track:%1").arg(spotifyId);
		target.directTrack = true;
		return target;
	}

	if (rsMusicLooksLikeUrl(trimmed)) {
		target.unsupportedUrl = true;
		return target;
	}

	target.searchQuery = trimmed;
	return target;
}

// ---------------------------------------------
// Search query helpers
// ---------------------------------------------

bool rsMusicLooksLikeUrl(const QString &input)
{
	const QString s = input.trimmed().toLower();
	return s.startsWith("http://") || s.startsWith("https://");
}
