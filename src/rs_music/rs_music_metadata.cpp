#include "rs_music_metadata.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>

static quint32 syncSafe(const uchar *value)
{
	return (quint32(value[0] & 0x7f) << 21) | (quint32(value[1] & 0x7f) << 14) |
	       (quint32(value[2] & 0x7f) << 7) | quint32(value[3] & 0x7f);
}

static QString decodeText(const QByteArray &value)
{
	if (value.isEmpty())
		return {};
	const uchar encoding = uchar(value.at(0));
	const QByteArray text = value.mid(1);
	if (encoding == 0)
		return QString::fromLatin1(text).remove(QChar('\0')).trimmed();
	if (encoding == 3)
		return QString::fromUtf8(text).remove(QChar('\0')).trimmed();
	if (text.size() < 2)
		return {};
	const bool littleEndian = uchar(text.at(0)) == 0xff && uchar(text.at(1)) == 0xfe;
	const int start = (uchar(text.at(0)) == 0xff || uchar(text.at(0)) == 0xfe) ? 2 : 0;
	QString result;
	for (int i = start; i + 1 < text.size(); i += 2) {
		const ushort code = littleEndian
			? ushort(uchar(text.at(i)) | (ushort(uchar(text.at(i + 1))) << 8))
			: ushort((ushort(uchar(text.at(i))) << 8) | uchar(text.at(i + 1)));
		if (code)
			result.append(QChar(code));
	}
	return result.trimmed();
}

static QByteArray apicImage(const QByteArray &frame)
{
	if (frame.size() < 5)
		return {};
	const int encoding = uchar(frame.at(0));
	int cursor = frame.indexOf('\0', 1);
	if (cursor < 0 || ++cursor >= frame.size())
		return {};
	++cursor; // picture type
	if (encoding == 0 || encoding == 3) {
		cursor = frame.indexOf('\0', cursor);
		if (cursor < 0)
			return {};
		++cursor;
	} else {
		while (cursor + 1 < frame.size() && (frame.at(cursor) != 0 || frame.at(cursor + 1) != 0))
			cursor += 2;
		cursor += 2;
	}
	return cursor < frame.size() ? frame.mid(cursor) : QByteArray{};
}

void RsMusicMetadata::enrichLocalTrack(RsMusicTrack &track, const QString &filePath)
{
	QFile file(filePath);
	if (file.open(QIODevice::ReadOnly)) {
		const QByteArray header = file.read(10);
		if (header.size() == 10 && header.startsWith("ID3")) {
			const int version = uchar(header.at(3));
			const quint32 tagSize = syncSafe(reinterpret_cast<const uchar *>(header.constData() + 6));
			const QByteArray tag = file.read(tagSize);
			int offset = 0;
			while (offset + 10 <= tag.size()) {
				const QByteArray id = tag.mid(offset, 4);
				if (id.at(0) == 0)
					break;
				const uchar *sizeBytes = reinterpret_cast<const uchar *>(tag.constData() + offset + 4);
				const quint32 size = version == 4 ? syncSafe(sizeBytes)
					: (quint32(sizeBytes[0]) << 24) | (quint32(sizeBytes[1]) << 16) |
					  (quint32(sizeBytes[2]) << 8) | quint32(sizeBytes[3]);
				offset += 10;
				if (!size || offset + int(size) > tag.size())
					break;
				const QByteArray contents = tag.mid(offset, size);
				if (id == "TIT2") track.title = decodeText(contents);
				else if (id == "TPE1") track.artist = decodeText(contents);
				else if (id == "TALB") track.album = decodeText(contents);
				else if (id == "APIC" && track.artworkUri.isEmpty()) {
					const QByteArray image = apicImage(contents);
					if (!image.isEmpty())
						track.artworkUri = "data:image;base64," + QString::fromLatin1(image.toBase64());
				}
				offset += size;
			}
		}
	}

	if (track.artworkUri.isEmpty()) {
		const QDir folder = QFileInfo(filePath).absoluteDir();
		for (const QString &name : {"cover.jpg", "cover.png", "folder.jpg", "folder.png", "album.jpg", "album.png"}) {
			const QString candidate = folder.filePath(name);
			if (QFileInfo::exists(candidate)) {
				track.artworkUri = QUrl::fromLocalFile(candidate).toString();
				break;
			}
		}
	}
	if (track.artworkUri.isEmpty())
		track.artworkUri = ":/rs/music/music-fallback-vinyl.png";
}

QByteArray RsMusicMetadata::artworkBytes(const QString &artworkUri)
{
	if (artworkUri.startsWith("data:image")) {
		const int comma = artworkUri.indexOf(',');
		return comma >= 0 ? QByteArray::fromBase64(artworkUri.mid(comma + 1).toLatin1()) : QByteArray{};
	}
	const QString path = artworkUri.startsWith("file:") ? QUrl(artworkUri).toLocalFile() : artworkUri;
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
