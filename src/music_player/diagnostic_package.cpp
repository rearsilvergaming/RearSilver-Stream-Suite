#include "diagnostic_package.hpp"

#include <shobjidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <set>
#include <sstream>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {
constexpr uint64_t kMaximumPackagePayload = 18ull * 1024ull * 1024ull;
constexpr uint64_t kManifestReserve = 256ull * 1024ull;

struct ZipEntry {
	std::string name;
	uint32_t crc = 0;
	uint32_t size = 0;
	uint32_t offset = 0;
	uint16_t dosTime = 0;
	uint16_t dosDate = 0;
};

void write16(std::ofstream &output, uint16_t value)
{
	const char bytes[] = {char(value & 0xff), char((value >> 8) & 0xff)};
	output.write(bytes, sizeof(bytes));
}

void write32(std::ofstream &output, uint32_t value)
{
	const char bytes[] = {char(value & 0xff), char((value >> 8) & 0xff),
		char((value >> 16) & 0xff), char((value >> 24) & 0xff)};
	output.write(bytes, sizeof(bytes));
}

uint32_t crc32Update(uint32_t crc, const uint8_t *data, size_t size)
{
	static std::array<uint32_t, 256> table = [] {
		std::array<uint32_t, 256> values{};
		for (uint32_t index = 0; index < values.size(); ++index) {
			uint32_t value = index;
			for (int bit = 0; bit < 8; ++bit)
				value = (value >> 1) ^ (0xedb88320u & (0u - (value & 1u)));
			values[index] = value;
		}
		return values;
	}();
	for (size_t index = 0; index < size; ++index)
		crc = table[(crc ^ data[index]) & 0xff] ^ (crc >> 8);
	return crc;
}

void currentDosTime(uint16_t &time, uint16_t &date)
{
	SYSTEMTIME now{}; GetLocalTime(&now);
	time = uint16_t((now.wHour << 11) | (now.wMinute << 5) | (now.wSecond / 2));
	const unsigned year = now.wYear < 1980 ? 0 : (std::min<unsigned>)(127, now.wYear - 1980);
	date = uint16_t((year << 9) | (now.wMonth << 5) | now.wDay);
}

std::string utf8(const std::wstring &value)
{
	if (value.empty()) return {};
	const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
		value.data(), int(value.size()), nullptr, 0, nullptr, nullptr);
	if (size <= 0) return {};
	std::string result(size_t(size), '\0');
	if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
		value.data(), int(value.size()), result.data(), size, nullptr, nullptr) != size) return {};
	return result;
}

std::wstring environmentPath(const wchar_t *name)
{
	wchar_t value[32768]{};
	const DWORD length = GetEnvironmentVariableW(name, value, DWORD(std::size(value)));
	return length && length < std::size(value) ? std::wstring(value, length) : std::wstring{};
}

std::string safeZipName(std::string name)
{
	std::replace(name.begin(), name.end(), '\\', '/');
	while (!name.empty() && name.front() == '/') name.erase(name.begin());
	if (name.find("../") != std::string::npos || name == "..") return {};
	return name;
}

class ZipWriter {
public:
	explicit ZipWriter(const std::wstring &path) : m_output(path, std::ios::binary | std::ios::trunc) {}
	bool ready() const { return bool(m_output); }
	bool canFit(uint64_t size, uint64_t reserve = 0) const {
		return m_payloadSize + size + reserve <= kMaximumPackagePayload;
	}

	bool addMemory(const std::string &rawName, const std::string &contents)
	{
		const std::string name = safeZipName(rawName);
		if (name.empty() || contents.size() > UINT32_MAX || !canFit(contents.size())) return false;
		const uint32_t crc = crc32Update(0xffffffffu,
			reinterpret_cast<const uint8_t *>(contents.data()), contents.size()) ^ 0xffffffffu;
		return beginEntry(name, crc, uint32_t(contents.size())) &&
			(writeBytes(contents.data(), contents.size()), bool(m_output));
	}

	bool addFile(const std::string &rawName, const std::filesystem::path &path)
	{
		const std::string name = safeZipName(rawName);
		if (name.empty()) return false;
		std::error_code error;
		const uintmax_t fileSize = std::filesystem::file_size(path, error);
		if (error || fileSize > UINT32_MAX || !canFit(fileSize)) return false;
		std::ifstream input(path, std::ios::binary);
		if (!input) return false;
		std::array<char, 64 * 1024> buffer{};
		uint32_t crc = 0xffffffffu;
		while (input) {
			input.read(buffer.data(), std::streamsize(buffer.size()));
			const std::streamsize count = input.gcount();
			if (count > 0) crc = crc32Update(crc,
				reinterpret_cast<const uint8_t *>(buffer.data()), size_t(count));
		}
		if (!input.eof()) return false;
		crc ^= 0xffffffffu;
		if (!beginEntry(name, crc, uint32_t(fileSize))) return false;
		input.clear(); input.seekg(0);
		while (input) {
			input.read(buffer.data(), std::streamsize(buffer.size()));
			const std::streamsize count = input.gcount();
			if (count > 0) writeBytes(buffer.data(), size_t(count));
		}
		return input.eof() && bool(m_output);
	}

	bool finish()
	{
		if (m_finished || !m_output || m_entries.size() > UINT16_MAX) return false;
		const std::streampos centralStartPosition = m_output.tellp();
		const std::streamoff centralStartOffset = centralStartPosition;
		if (centralStartOffset < 0 || uint64_t(centralStartOffset) > UINT32_MAX) return false;
		for (const ZipEntry &entry : m_entries) {
			write32(m_output, 0x02014b50); write16(m_output, 20); write16(m_output, 20);
			write16(m_output, 0x0800); write16(m_output, 0); write16(m_output, entry.dosTime);
			write16(m_output, entry.dosDate); write32(m_output, entry.crc); write32(m_output, entry.size);
			write32(m_output, entry.size); write16(m_output, uint16_t(entry.name.size()));
			write16(m_output, 0); write16(m_output, 0); write16(m_output, 0); write16(m_output, 0);
			write32(m_output, 0); write32(m_output, entry.offset); writeBytes(entry.name.data(), entry.name.size());
		}
		const std::streampos centralEndPosition = m_output.tellp();
		const std::streamoff centralEndOffset = centralEndPosition;
		if (centralEndOffset < centralStartOffset || uint64_t(centralEndOffset) > UINT32_MAX) return false;
		const uint32_t centralStart = uint32_t(centralStartOffset);
		const uint32_t centralSize = uint32_t(centralEndOffset - centralStartOffset);
		write32(m_output, 0x06054b50); write16(m_output, 0); write16(m_output, 0);
		write16(m_output, uint16_t(m_entries.size())); write16(m_output, uint16_t(m_entries.size()));
		write32(m_output, centralSize); write32(m_output, centralStart); write16(m_output, 0);
		m_output.flush(); m_finished = bool(m_output); return m_finished;
	}

private:
	bool beginEntry(const std::string &name, uint32_t crc, uint32_t size)
	{
		const std::streampos position = m_output.tellp();
		const std::streamoff offset = position;
		if (!m_output || offset < 0 || uint64_t(offset) > UINT32_MAX || name.size() > UINT16_MAX) return false;
		ZipEntry entry; entry.name = name; entry.crc = crc; entry.size = size; entry.offset = uint32_t(offset);
		currentDosTime(entry.dosTime, entry.dosDate);
		write32(m_output, 0x04034b50); write16(m_output, 20); write16(m_output, 0x0800);
		write16(m_output, 0); write16(m_output, entry.dosTime); write16(m_output, entry.dosDate);
		write32(m_output, crc); write32(m_output, size); write32(m_output, size);
		write16(m_output, uint16_t(name.size())); write16(m_output, 0); writeBytes(name.data(), name.size());
		if (!m_output) return false;
		m_payloadSize += size;
		m_entries.push_back(std::move(entry)); return true;
	}

	void writeBytes(const void *data, size_t size)
	{
		m_output.write(static_cast<const char *>(data), std::streamsize(size));
	}

	std::ofstream m_output;
	std::vector<ZipEntry> m_entries;
	uint64_t m_payloadSize = 0;
	bool m_finished = false;
};

bool readTextFile(const std::filesystem::path &path, std::string &contents)
{
	std::ifstream input(path, std::ios::binary);
	if (!input) return false;
	contents.assign(std::istreambuf_iterator<char>(input), {});
	return input.good() || input.eof();
}

bool containsHubExecutable(const std::string &text)
{
	std::string lower = text;
	std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char value) { return char(std::tolower(value)); });
	return lower.find("rearsilver-stream-suite-control-hub.exe") != std::string::npos ||
		lower.find("rearsilver-stream-suite-system-media-helper.exe") != std::string::npos;
}

std::wstring lowerExtension(const std::filesystem::path &path)
{
	std::wstring extension = path.extension().wstring();
	std::transform(extension.begin(), extension.end(), extension.begin(),
		[](wchar_t value) { return wchar_t(std::towlower(value)); });
	return extension;
}

bool validIssueDate(const std::string &date)
{
	if (date.size() != 10 || date[4] != '-' || date[7] != '-') return false;
	for (size_t index = 0; index < date.size(); ++index)
		if (index != 4 && index != 7 && !std::isdigit(static_cast<unsigned char>(date[index]))) return false;
	const int year = std::stoi(date.substr(0, 4));
	const int month = std::stoi(date.substr(5, 2));
	const int day = std::stoi(date.substr(8, 2));
	if (year < 2000 || month < 1 || month > 12 || day < 1) return false;
	static const int daysPerMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	int maximum = daysPerMonth[month - 1];
	if (month == 2 && (year % 400 == 0 || (year % 4 == 0 && year % 100 != 0))) maximum = 29;
	return day <= maximum;
}

bool fileMatchesIssueDate(const std::filesystem::path &path, const std::string &issueDate)
{
	const std::string compact = issueDate.substr(0, 4) + issueDate.substr(5, 2) + issueDate.substr(8, 2);
	const std::string filename = utf8(path.filename().wstring());
	if (filename.find(compact) != std::string::npos) return true;
	WIN32_FILE_ATTRIBUTE_DATA attributes{};
	if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes)) return false;
	FILETIME localTime{};
	SYSTEMTIME systemTime{};
	if (!FileTimeToLocalFileTime(&attributes.ftLastWriteTime, &localTime) ||
		!FileTimeToSystemTime(&localTime, &systemTime)) return false;
	char fileDate[11]{};
	sprintf_s(fileDate, "%04u-%02u-%02u", systemTime.wYear, systemTime.wMonth, systemTime.wDay);
	return issueDate == fileDate;
}

std::string uniqueName(const std::string &folder, const std::filesystem::path &path,
	std::set<std::string> &used)
{
	std::string leaf = utf8(path.filename().wstring());
	if (leaf.empty()) leaf = "unnamed-file";
	std::string candidate = folder + "/" + leaf;
	for (unsigned suffix = 2; !used.insert(candidate).second; ++suffix) {
		const std::string stem = utf8(path.stem().wstring());
		const std::string extension = utf8(path.extension().wstring());
		candidate = folder + "/" + stem + "-" + std::to_string(suffix) + extension;
	}
	return candidate;
}

void addRedactedLog(ZipWriter &zip, const std::filesystem::path &path, const std::string &folder,
	std::set<std::string> &used, DiagnosticPackageResult &result, std::ostringstream &manifest,
	const std::string &issueDate = {})
{
	std::string contents;
	if (!readTextFile(path, contents)) return;
	if (!issueDate.empty()) {
		std::istringstream input(contents); std::ostringstream selected; std::string line;
		while (std::getline(input, line))
			if (diagnosticLineMatchesDate(line, issueDate)) selected << line << '\n';
		contents = selected.str();
		if (contents.empty()) return;
	}
	const std::string name = uniqueName(folder, path, used);
	contents = redactDiagnosticText(std::move(contents));
	if (!zip.canFit(contents.size(), kManifestReserve)) {
		++result.skippedFiles; manifest << "- SKIPPED " << name << " (package size limit)\n"; return;
	}
	if (zip.addMemory(name, contents)) {
		++result.textFiles; manifest << "- " << name << " (text redacted)\n";
	}
}

void addDump(ZipWriter &zip, const std::filesystem::path &path, const std::string &folder,
	std::set<std::string> &used, DiagnosticPackageResult &result, std::ostringstream &manifest)
{
	const std::string name = uniqueName(folder, path, used);
	std::error_code error;
	const uintmax_t size = std::filesystem::file_size(path, error);
	if (error || !zip.canFit(size, kManifestReserve)) {
		++result.skippedFiles; manifest << "- SKIPPED " << name << " (package size limit or unreadable)\n"; return;
	}
	if (zip.addFile(name, path)) {
		++result.dumpFiles; manifest << "- " << name << " (binary dump; not redacted)\n";
	}
}

void forEachRegularFile(const std::filesystem::path &folder, bool recursive,
	const std::function<void(const std::filesystem::path &)> &callback)
{
	std::error_code error;
	if (!std::filesystem::exists(folder, error)) return;
	if (recursive) {
		for (std::filesystem::recursive_directory_iterator iterator(folder,
			std::filesystem::directory_options::skip_permission_denied, error), end;
			iterator != end; iterator.increment(error)) {
			if (error) { error.clear(); continue; }
			if (iterator->is_regular_file(error) && !error) callback(iterator->path());
		}
	} else {
		for (std::filesystem::directory_iterator iterator(folder,
			std::filesystem::directory_options::skip_permission_denied, error), end;
			iterator != end; iterator.increment(error)) {
			if (error) { error.clear(); continue; }
			if (iterator->is_regular_file(error) && !error) callback(iterator->path());
		}
	}
}
}

std::string redactDiagnosticText(std::string text)
{
	wchar_t userName[256]{}; DWORD userNameLength = DWORD(std::size(userName));
	if (GetUserNameW(userName, &userNameLength)) {
		const std::string value = utf8(userName);
		if (!value.empty()) for (size_t position = 0; (position = text.find(value, position)) != std::string::npos;)
			text.replace(position, value.size(), "<windows-user>");
	}
	const std::wstring profileWide = environmentPath(L"USERPROFILE");
	const std::string profile = utf8(profileWide);
	if (!profile.empty()) for (size_t position = 0; (position = text.find(profile, position)) != std::string::npos;)
		text.replace(position, profile.size(), "<user-profile>");
	std::istringstream input(text); std::ostringstream output; std::string line;
	while (std::getline(input, line)) {
		std::string lower = line;
		std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char value) { return char(std::tolower(value)); });
		const bool credentialValue = lower.find('=') != std::string::npos || lower.find(':') != std::string::npos;
		const bool sensitive = credentialValue && (lower.find("access_token") != std::string::npos ||
			lower.find("refresh_token") != std::string::npos || lower.find("client_secret") != std::string::npos ||
			lower.find("client id") != std::string::npos || lower.find("client_id") != std::string::npos ||
			lower.find("oauth code") != std::string::npos || lower.find("device code") != std::string::npos ||
			lower.find("stream key") != std::string::npos || lower.find("authorization:") != std::string::npos);
		output << (sensitive ? "[REDACTED SENSITIVE LINE]" : line) << '\n';
	}
	return output.str();
}

bool diagnosticLineMatchesDate(const std::string &line, const std::string &issueDate)
{
	if (!validIssueDate(issueDate)) return false;
	if (line.rfind(issueDate, 0) == 0) return true;
	const int year = std::stoi(issueDate.substr(0, 4));
	const int month = std::stoi(issueDate.substr(5, 2));
	const int day = std::stoi(issueDate.substr(8, 2));
	const std::string unpadded = std::to_string(year) + '-' + std::to_string(month) + '-' + std::to_string(day);
	if (line.rfind(unpadded, 0) == 0) return true;
	const std::string chromiumDate = "[" + issueDate.substr(5, 2) + issueDate.substr(8, 2) + "/";
	return line.find(chromiumDate) != std::string::npos;
}

DiagnosticPackageResult exportDiagnosticPackage(HWND owner, const std::wstring &suggestedName,
	const std::string &feedbackReport, const std::string &issueDate)
{
	DiagnosticPackageResult result;
	if (!validIssueDate(issueDate)) {
		result.error = "Select a valid issue date before exporting diagnostics."; return result;
	}
	ComPtr<IFileSaveDialog> dialog;
	if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
		result.error = "The diagnostic package save window could not be opened."; return result;
	}
	dialog->SetTitle(L"Export RearSilver Stream Suite diagnostic package");
	dialog->SetFileName(suggestedName.c_str());
	const COMDLG_FILTERSPEC filters[] = {{L"ZIP packages", L"*.zip"}};
	dialog->SetFileTypes(1, filters); dialog->SetDefaultExtension(L"zip");
	const HRESULT show = dialog->Show(owner);
	if (show == HRESULT_FROM_WIN32(ERROR_CANCELLED)) { result.cancelled = true; return result; }
	if (FAILED(show)) { result.error = "The diagnostic package save window failed."; return result; }
	ComPtr<IShellItem> item; PWSTR rawPath = nullptr;
	if (FAILED(dialog->GetResult(&item)) || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &rawPath))) {
		result.error = "The selected package path could not be read."; return result;
	}
	result.path = rawPath; CoTaskMemFree(rawPath);
	ZipWriter zip(result.path);
	if (!zip.ready()) { result.error = "The diagnostic package file could not be created."; return result; }
	std::set<std::string> used;
	std::set<std::wstring> includedDumpPaths;
	std::ostringstream manifest;
	manifest << "RearSilver Stream Suite diagnostic package\n"
		<< "==============================================\n\n"
		<< "This package was created locally after the user selected Export diagnostic package.\n"
		<< "Issue date: " << issueDate << " (local calendar date)\n"
		<< "Session logs and crash records are limited to that date.\n"
		<< "Text reports and logs are privacy-redacted. Binary crash dumps cannot be redacted and may contain fragments of process memory.\n\n"
		<< "Included files:\n";
	const std::string reportName = "feedback-report.txt";
	std::string reportWithBom("\xef\xbb\xbf", 3); reportWithBom += feedbackReport;
	if (!zip.addMemory(reportName, reportWithBom)) { result.error = "The feedback report could not be added to the package."; return result; }
	used.insert(reportName); ++result.textFiles; manifest << "- " << reportName << " (text redacted)\n";

	const std::wstring localBase = environmentPath(L"LOCALAPPDATA");
	const std::wstring roamingBase = environmentPath(L"APPDATA");
	const std::wstring programDataBase = environmentPath(L"PROGRAMDATA");
	const std::filesystem::path localSuite = localBase.empty() ? std::filesystem::path{} :
		std::filesystem::path(localBase) / L"RearSilver Stream Suite";
	const std::filesystem::path roamingSuite = roamingBase.empty() ? std::filesystem::path{} :
		std::filesystem::path(roamingBase) / L"RearSilver Stream Suite";
	forEachRegularFile(localSuite / L"Logs", false, [&](const auto &path) {
		if (lowerExtension(path) == L".log" && fileMatchesIssueDate(path, issueDate))
			addRedactedLog(zip, path, "logs/control-hub", used, result, manifest);
	});
	for (const auto &path : {localSuite / L"control-hub-lifecycle.log", localSuite / L"CEF" / L"cef.log",
		roamingSuite / L"spotify-diagnostics.log", roamingSuite / L"twitch-diagnostics.log"})
		if (fileMatchesIssueDate(path, issueDate)) addRedactedLog(zip, path, "logs", used, result, manifest, issueDate);
	forEachRegularFile(localSuite / L"Crash Dumps", false, [&](const auto &path) {
		if (lowerExtension(path) == L".dmp" && fileMatchesIssueDate(path, issueDate) &&
			includedDumpPaths.insert(path.wstring()).second)
			addDump(zip, path, "crash-dumps/suite", used, result, manifest);
	});
	forEachRegularFile(localBase.empty() ? std::filesystem::path{} : std::filesystem::path(localBase) / L"CrashDumps", false, [&](const auto &path) {
		std::string lower = utf8(path.filename().wstring());
		std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char value) { return char(std::tolower(value)); });
		if (lowerExtension(path) == L".dmp" && fileMatchesIssueDate(path, issueDate) &&
			(lower.find("rearsilver-stream-suite-control-hub") != std::string::npos ||
			lower.find("rearsilver-stream-suite-system-media-helper") != std::string::npos))
			if (includedDumpPaths.insert(path.wstring()).second)
				addDump(zip, path, "crash-dumps/windows", used, result, manifest);
	});
	auto collectWerRoot = [&](const std::filesystem::path &werRoot) {
		forEachRegularFile(werRoot, true, [&](const auto &path) {
			if (lowerExtension(path) != L".wer" || !fileMatchesIssueDate(path, issueDate)) return;
			std::string contents;
			if (!readTextFile(path, contents) || !containsHubExecutable(contents)) return;
			const std::string reportEntry = uniqueName("windows-error-reports", path, used);
			if (zip.addMemory(reportEntry, redactDiagnosticText(contents))) {
				++result.textFiles; manifest << "- " << reportEntry << " (text redacted)\n";
			}
			std::error_code error;
			for (const auto &sibling : std::filesystem::directory_iterator(path.parent_path(),
				std::filesystem::directory_options::skip_permission_denied, error)) {
				const std::wstring extension = lowerExtension(sibling.path());
				if (!error && sibling.is_regular_file(error) && (extension == L".dmp" || extension == L".hdmp") &&
					includedDumpPaths.insert(sibling.path().wstring()).second)
					addDump(zip, sibling.path(), "crash-dumps/windows-error-reporting", used, result, manifest);
			}
		});
	};
	if (!localBase.empty()) collectWerRoot(std::filesystem::path(localBase) / L"Microsoft" / L"Windows" / L"WER");
	if (!programDataBase.empty()) collectWerRoot(std::filesystem::path(programDataBase) / L"Microsoft" / L"Windows" / L"WER");
	manifest << "\nSummary: " << result.textFiles << " text file(s), " << result.dumpFiles << " dump file(s), "
		<< result.skippedFiles << " file(s) skipped.\n"
		<< "Package payload is capped at 18 MiB so the ZIP remains below Discord's 20 MB upload limit.\n";
	if (!zip.addMemory("manifest.txt", manifest.str()) || !zip.finish()) {
		result.error = "The diagnostic ZIP could not be completed.";
		DeleteFileW(result.path.c_str()); return result;
	}
	result.exported = true;
	return result;
}
