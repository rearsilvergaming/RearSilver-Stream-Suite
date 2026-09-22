#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#include <shellapi.h>

#include "rs_beta_config.hpp"
#include "system_media_provider.hpp"
#include "system_media_shared.hpp"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

namespace {
wchar_t g_dumpDirectory[32768]{};

void initialiseDumpDirectory()
{
	wchar_t localAppData[32768]{};
	if (!GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, DWORD(std::size(localAppData)))) return;
	wchar_t suite[32768]{};
	swprintf_s(suite, L"%s\\RearSilver Stream Suite", localAppData);
	CreateDirectoryW(suite, nullptr);
	swprintf_s(g_dumpDirectory, L"%s\\Crash Dumps", suite);
	CreateDirectoryW(g_dumpDirectory, nullptr);
}

LONG WINAPI writeHelperDump(EXCEPTION_POINTERS *exception)
{
	if (!g_dumpDirectory[0]) return EXCEPTION_CONTINUE_SEARCH;
	SYSTEMTIME now{}; GetLocalTime(&now);
	wchar_t path[32768]{};
	swprintf_s(path, L"%s\\system-media-%04u%02u%02u-%02u%02u%02u-PID%lu.dmp",
		g_dumpDirectory, now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
		now.wSecond, GetCurrentProcessId());
	HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file != INVALID_HANDLE_VALUE) {
		MINIDUMP_EXCEPTION_INFORMATION information{};
		information.ThreadId = GetCurrentThreadId();
		information.ExceptionPointers = exception;
		information.ClientPointers = FALSE;
		const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithThreadInfo |
			MiniDumpWithUnloadedModules | MiniDumpWithIndirectlyReferencedMemory);
		MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, type, &information, nullptr, nullptr);
		CloseHandle(file);
	}
	TerminateProcess(GetCurrentProcess(), exception && exception->ExceptionRecord ?
		exception->ExceptionRecord->ExceptionCode : 1);
	return EXCEPTION_EXECUTE_HANDLER;
}

bool copyText(char *destination, size_t capacity, const std::string &source)
{
	if (!destination || !capacity) return false;
	size_t count = (std::min)(capacity - 1, source.size());
	while (count > 0 && count < source.size() &&
		MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source.data(), int(count), nullptr, 0) == 0 &&
		GetLastError() == ERROR_NO_UNICODE_TRANSLATION) --count;
	std::memcpy(destination, source.data(), count);
	destination[count] = '\0';
	return count == source.size();
}

std::wstring argumentValue(int argc, wchar_t **argv, const wchar_t *name)
{
	for (int index = 1; index + 1 < argc; ++index)
		if (wcscmp(argv[index], name) == 0) return argv[index + 1];
	return {};
}

std::string wideToUtf8(const std::wstring &value)
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
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
	int argc = 0;
	wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (!argv) return 2;
	const std::wstring mappingName = argumentValue(argc, argv, L"--mapping");
	const std::wstring mutexName = argumentValue(argc, argv, L"--mutex");
	const std::wstring eventName = argumentValue(argc, argv, L"--event");
	const std::wstring preferredWide = argumentValue(argc, argv, L"--preferred");
	const std::wstring parentPidText = argumentValue(argc, argv, L"--parent-pid");
	LocalFree(argv);
	if (mappingName.empty() || mutexName.empty() || eventName.empty() || parentPidText.empty()) return 2;

	initialiseDumpDirectory();
	SetUnhandledExceptionFilter(writeHelperDump);
	HANDLE mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, mappingName.c_str());
	HANDLE mutex = OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, mutexName.c_str());
	HANDLE event = OpenEventW(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, eventName.c_str());
	HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, wcstoul(parentPidText.c_str(), nullptr, 10));
	if (!mapping || !mutex || !event || !parent) {
		if (parent) CloseHandle(parent); if (event) CloseHandle(event); if (mutex) CloseHandle(mutex); if (mapping) CloseHandle(mapping);
		return 3;
	}
	auto *shared = static_cast<SystemMediaSharedState *>(
		MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SystemMediaSharedState)));
	if (!shared || shared->magic != kSystemMediaSharedMagic || shared->version != kSystemMediaSharedVersion) {
		if (shared) UnmapViewOfFile(shared); CloseHandle(parent); CloseHandle(event); CloseHandle(mutex); CloseHandle(mapping);
		return 4;
	}
	std::string preferred = wideToUtf8(preferredWide);
	SystemMediaProvider provider;
	provider.start(preferred.empty() ? "spotify.exe" : preferred);
	for (;;) {
		std::vector<SystemMediaSharedCommand> commands;
		bool stop = false;
		const DWORD commandWait = WaitForSingleObject(mutex, 1000);
		if (commandWait == WAIT_ABANDONED) { ReleaseMutex(mutex); break; }
		if (commandWait == WAIT_OBJECT_0) {
			stop = shared->stopRequested != 0;
			while (shared->commandRead != shared->commandWrite) {
				commands.push_back(shared->commands[shared->commandRead]);
				shared->commandRead = (shared->commandRead + 1) % kSystemMediaCommandCapacity;
			}
			ReleaseMutex(mutex);
		}
		if (stop) break;
		for (const auto &command : commands) {
			if (command.action < int32_t(SystemMediaProvider::Action::Play) ||
				command.action > int32_t(SystemMediaProvider::Action::Seek)) continue;
			provider.command(static_cast<SystemMediaProvider::Action>(command.action), command.positionMs);
		}
		const SystemMediaState state = provider.state();
		const DWORD stateWait = WaitForSingleObject(mutex, 1000);
		if (stateWait == WAIT_ABANDONED) { ReleaseMutex(mutex); break; }
		if (stateWait == WAIT_OBJECT_0) {
			shared->available = state.available; shared->playing = state.playing;
			shared->canPlay = state.canPlay; shared->canPause = state.canPause;
			shared->canNext = state.canNext; shared->canPrevious = state.canPrevious;
			shared->canSeek = state.canSeek; shared->positionMs = state.positionMs;
			shared->durationMs = state.durationMs;
			copyText(shared->sourceAppId, std::size(shared->sourceAppId), state.sourceAppId);
			copyText(shared->title, std::size(shared->title), state.title);
			copyText(shared->artist, std::size(shared->artist), state.artist);
			copyText(shared->album, std::size(shared->album), state.album);
			copyText(shared->artworkPath, std::size(shared->artworkPath), state.artworkPath);
			ReleaseMutex(mutex);
		}
		HANDLE waits[] = {event, parent};
		if (WaitForMultipleObjects(2, waits, FALSE, 100) == WAIT_OBJECT_0 + 1) break;
	}
	provider.stop();
	UnmapViewOfFile(shared); CloseHandle(parent); CloseHandle(event); CloseHandle(mutex); CloseHandle(mapping);
	return 0;
}
