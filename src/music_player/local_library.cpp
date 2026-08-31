#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include "local_library.hpp"
#include "local_order.hpp"
#include "miniaudio.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

namespace {
std::string utf8(const std::wstring &value)
{
	if (value.empty()) return {};
	const int count = WideCharToMultiByte(CP_UTF8, 0, value.data(), int(value.size()), nullptr, 0, nullptr, nullptr);
	std::string output(size_t(count), '\0');
	WideCharToMultiByte(CP_UTF8, 0, value.data(), int(value.size()), output.data(), count, nullptr, nullptr);
	return output;
}

bool supported(const fs::path &path)
{
	std::wstring extension = path.extension().wstring();
	std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
	static const std::set<std::wstring> extensions{L".mp3", L".wav", L".flac", L".ogg", L".m4a", L".aac", L".wma"};
	return extensions.count(extension) != 0;
}

uint32_t syncSafe(const unsigned char *value)
{
	return (uint32_t(value[0] & 0x7f) << 21) | (uint32_t(value[1] & 0x7f) << 14) |
		(uint32_t(value[2] & 0x7f) << 7) | uint32_t(value[3] & 0x7f);
}

std::string textFrame(const std::vector<unsigned char> &value)
{
	if (value.size() < 2) return {};
	if (value[0] == 0 || value[0] == 3) return std::string(value.begin() + 1, value.end());
	if ((value[0] == 1 || value[0] == 2) && value.size() >= 3) {
		const bool little = value[0] == 1 && value[1] == 0xff && value[2] == 0xfe;
		size_t offset = value[0] == 1 ? 3 : 1; std::wstring wide;
		for (; offset + 1 < value.size(); offset += 2) {
			const wchar_t character = little ? wchar_t(value[offset] | value[offset + 1] << 8) : wchar_t(value[offset] << 8 | value[offset + 1]);
			if (!character) break; wide.push_back(character);
		}
		return utf8(wide);
	}
	return {};
}

int numberedFrame(const std::vector<unsigned char> &value)
{
	const std::string text = textFrame(value);
	size_t offset = 0;
	while (offset < text.size() && std::isspace(static_cast<unsigned char>(text[offset]))) ++offset;
	int number = 0;
	bool found = false;
	while (offset < text.size() && std::isdigit(static_cast<unsigned char>(text[offset]))) {
		found = true;
		number = number * 10 + (text[offset++] - '0');
	}
	return found ? number : 0;
}

void readId3(const fs::path &path, HubTrack &track)
{
	std::ifstream input(path, std::ios::binary); unsigned char header[10]{};
	if (!input.read(reinterpret_cast<char *>(header), 10) || memcmp(header, "ID3", 3) != 0) return;
	const int version = header[3]; const uint32_t tagSize = syncSafe(header + 6); uint32_t consumed = 0;
	while (consumed + 10 <= tagSize && input) {
		unsigned char frameHeader[10]{}; if (!input.read(reinterpret_cast<char *>(frameHeader), 10)) break;
		if (!frameHeader[0]) break;
		const std::string id(reinterpret_cast<char *>(frameHeader), 4);
		const uint32_t size = version == 4 ? syncSafe(frameHeader + 4) :
			(uint32_t(frameHeader[4]) << 24 | uint32_t(frameHeader[5]) << 16 | uint32_t(frameHeader[6]) << 8 | frameHeader[7]);
		if (!size || size > tagSize - consumed - 10 || size > 16 * 1024 * 1024) break;
		std::vector<unsigned char> value(size); if (!input.read(reinterpret_cast<char *>(value.data()), size)) break;
		if (id == "TIT2") track.title = textFrame(value);
		else if (id == "TPE1") track.artist = textFrame(value);
		else if (id == "TALB") track.album = textFrame(value);
		else if (id == "TRCK") track.trackNumber = numberedFrame(value);
		else if (id == "TPOS") track.discNumber = numberedFrame(value);
		else if (id == "APIC" && track.artworkUrl.empty()) {
			size_t position = 1; while (position < value.size() && value[position]) ++position; ++position;
			if (position < value.size()) ++position;
			const bool wideDescription = !value.empty() && (value[0] == 1 || value[0] == 2);
			if (wideDescription) {
				while (position + 1 < value.size() && (value[position] || value[position + 1])) position += 2;
				position = std::min(value.size(), position + 2);
			} else { while (position < value.size() && value[position]) ++position; ++position; }
			if (position < value.size()) {
				wchar_t appData[MAX_PATH]{}; GetEnvironmentVariableW(L"LOCALAPPDATA", appData, MAX_PATH);
				const fs::path cache = fs::path(appData) / L"RearSilver Stream Suite" / L"artwork"; fs::create_directories(cache);
				const fs::path artwork = cache / (std::to_wstring(std::hash<std::wstring>{}(path.wstring())) + L".img");
				std::ofstream output(artwork, std::ios::binary | std::ios::trunc);
				output.write(reinterpret_cast<const char *>(value.data() + position), std::streamsize(value.size() - position));
				if (output) track.artworkUrl = utf8(artwork.wstring());
			}
		}
		consumed += 10 + size;
	}
}

HubTrack makeTrack(const fs::path &path)
{
	HubTrack track; const fs::path absolute = fs::absolute(path);
	track.provider = "local"; track.providerId = utf8(absolute.wstring());
	track.id = "local_" + std::to_string(std::hash<std::wstring>{}(absolute.wstring()));
	track.title = utf8(absolute.stem().wstring()); track.requestedBy = "Local files";
	if (absolute.extension() == L".mp3" || absolute.extension() == L".MP3") readId3(absolute, track);
	ma_decoder decoder{};
	if (ma_decoder_init_file_w(absolute.wstring().c_str(), nullptr, &decoder) == MA_SUCCESS) {
		ma_uint64 frames = 0; ma_uint32 rate = 0; ma_format format{}; ma_uint32 channels = 0;
		if (ma_decoder_get_length_in_pcm_frames(&decoder, &frames) == MA_SUCCESS &&
			ma_decoder_get_data_format(&decoder, &format, &channels, &rate, nullptr, 0) == MA_SUCCESS && rate)
			track.durationSeconds = int((frames + rate - 1) / rate);
		ma_decoder_uninit(&decoder);
	}
	if (track.title.empty()) track.title = utf8(absolute.stem().wstring());
	if (track.artworkUrl.empty()) {
		for (const wchar_t *name : {L"cover.jpg", L"cover.png", L"folder.jpg", L"folder.png"}) {
			const fs::path candidate = absolute.parent_path() / name;
			if (fs::exists(candidate)) { track.artworkUrl = utf8(candidate.wstring()); break; }
		}
	}
	return track;
}

bool localTrackLess(const HubTrack &left, const HubTrack &right, const fs::path &root)
{
	const fs::path leftPath = fs::u8path(left.providerId);
	const fs::path rightPath = fs::u8path(right.providerId);
	const fs::path leftRelative = root.empty() ? leftPath : leftPath.lexically_relative(root);
	const fs::path rightRelative = root.empty() ? rightPath : rightPath.lexically_relative(root);
	const fs::path leftAlbum = left.album.empty() ? leftRelative.parent_path() : fs::u8path(left.album);
	const fs::path rightAlbum = right.album.empty() ? rightRelative.parent_path() : fs::u8path(right.album);
	if (naturalLocalPathLess(leftAlbum, rightAlbum)) return true;
	if (naturalLocalPathLess(rightAlbum, leftAlbum)) return false;
	const int leftDisc = left.discNumber > 0 ? left.discNumber : 1;
	const int rightDisc = right.discNumber > 0 ? right.discNumber : 1;
	if (leftDisc != rightDisc) return leftDisc < rightDisc;
	const bool leftHasTrack = left.trackNumber > 0;
	const bool rightHasTrack = right.trackNumber > 0;
	if (leftHasTrack != rightHasTrack) return leftHasTrack;
	if (leftHasTrack && left.trackNumber != right.trackNumber) return left.trackNumber < right.trackNumber;
	return naturalLocalPathLess(leftRelative, rightRelative);
}
}

std::vector<std::wstring> chooseLocalAudioFiles(void *owner)
{
	ComPtr<IFileOpenDialog> dialog; std::vector<std::wstring> files;
	if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return files;
	DWORD options = 0; dialog->GetOptions(&options); dialog->SetOptions(options | FOS_ALLOWMULTISELECT | FOS_FILEMUSTEXIST);
	COMDLG_FILTERSPEC filters[]{{L"Audio files", L"*.mp3;*.wav;*.flac;*.ogg;*.m4a;*.aac;*.wma"}, {L"All files", L"*.*"}};
	dialog->SetFileTypes(2, filters); if (FAILED(dialog->Show(static_cast<HWND>(owner)))) return files;
	ComPtr<IShellItemArray> results; if (FAILED(dialog->GetResults(&results))) return files; DWORD count = 0; results->GetCount(&count);
	for (DWORD i = 0; i < count; ++i) { ComPtr<IShellItem> item; PWSTR path = nullptr; if (SUCCEEDED(results->GetItemAt(i, &item)) && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) { files.emplace_back(path); CoTaskMemFree(path); } }
	return files;
}

std::wstring chooseLocalAudioFolder(void *owner)
{
	ComPtr<IFileOpenDialog> dialog; if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return {};
	DWORD options = 0; dialog->GetOptions(&options); dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_PATHMUSTEXIST);
	if (FAILED(dialog->Show(static_cast<HWND>(owner)))) return {}; ComPtr<IShellItem> item; PWSTR path = nullptr;
	if (FAILED(dialog->GetResult(&item)) || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) return {};
	std::wstring result(path); CoTaskMemFree(path); return result;
}

std::vector<HubTrack> scanLocalAudioFiles(const std::vector<std::wstring> &files)
{
	std::vector<HubTrack> tracks;
	for (const std::wstring &file : files)
		if (supported(file) && fs::exists(file)) tracks.push_back(makeTrack(file));
	std::sort(tracks.begin(), tracks.end(), [](const HubTrack &left, const HubTrack &right) {
		return localTrackLess(left, right, {});
	});
	return tracks;
}

std::vector<HubTrack> scanLocalAudioFolder(const std::wstring &folder)
{
	std::vector<fs::path> files; std::error_code error;
	for (fs::recursive_directory_iterator item(folder, fs::directory_options::skip_permission_denied, error), end; item != end; item.increment(error))
		if (!error && item->is_regular_file() && supported(item->path())) files.push_back(item->path());
	const fs::path root(folder);
	std::sort(files.begin(), files.end(), [&](const fs::path &left, const fs::path &right) {
		return naturalLocalPathLess(left.lexically_relative(root), right.lexically_relative(root));
	});
	std::vector<HubTrack> tracks;
	tracks.reserve(files.size());
	for (const fs::path &file : files) tracks.push_back(makeTrack(file));
	std::sort(tracks.begin(), tracks.end(), [&](const HubTrack &left, const HubTrack &right) {
		return localTrackLess(left, right, root);
	});
	return tracks;
}
