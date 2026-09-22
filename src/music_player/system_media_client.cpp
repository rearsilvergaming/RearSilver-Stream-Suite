#include "system_media_client.hpp"

#include "system_media_shared.hpp"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <vector>

namespace {
std::wstring executableDirectory()
{
	wchar_t path[32768]{};
	const DWORD length = GetModuleFileNameW(nullptr, path, DWORD(std::size(path)));
	if (!length || length >= std::size(path)) return {};
	std::wstring result(path, length);
	const size_t slash = result.find_last_of(L"\\/");
	return slash == std::wstring::npos ? std::wstring{} : result.substr(0, slash);
}

std::wstring utf8ToWide(const std::string &value)
{
	if (value.empty()) return {};
	const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
		value.data(), int(value.size()), nullptr, 0);
	if (size <= 0) return {};
	std::wstring result(size_t(size), L'\0');
	if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
		value.data(), int(value.size()), result.data(), size) != size) return {};
	return result;
}

std::string boundedString(const char *value, size_t capacity)
{
	return std::string(value, strnlen_s(value, capacity));
}
}

SystemMediaClient::~SystemMediaClient() { stop(); }

void SystemMediaClient::start(std::string preferredApplication)
{
	stop();
	std::lock_guard<std::mutex> lifecycleLock(m_lifecycleMutex);
	m_preferredApplication = std::move(preferredApplication);

	const std::wstring token = std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
	const std::wstring mappingName = L"Local\\RearSilverSystemMediaMapping-" + token;
	const std::wstring mutexName = L"Local\\RearSilverSystemMediaMutex-" + token;
	const std::wstring eventName = L"Local\\RearSilverSystemMediaWake-" + token;

	m_mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
		DWORD(sizeof(SystemMediaSharedState)), mappingName.c_str());
	if (!m_mapping) { closeHandles(); return; }
	m_shared = static_cast<SystemMediaSharedState *>(
		MapViewOfFile(m_mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SystemMediaSharedState)));
	if (!m_shared) { closeHandles(); return; }
	std::memset(m_shared, 0, sizeof(*m_shared));
	m_shared->magic = kSystemMediaSharedMagic;
	m_shared->version = kSystemMediaSharedVersion;
	m_sharedMutex = CreateMutexW(nullptr, FALSE, mutexName.c_str());
	m_wakeEvent = CreateEventW(nullptr, FALSE, FALSE, eventName.c_str());
	if (!m_sharedMutex || !m_wakeEvent) { closeHandles(); return; }

	const std::wstring folder = executableDirectory();
	const std::wstring helper = folder.empty() ? L"RearSilver-Stream-Suite-System-Media-Helper.exe" :
		folder + L"\\RearSilver-Stream-Suite-System-Media-Helper.exe";
	std::wstring commandLine = L"\"" + helper + L"\" --mapping \"" + mappingName +
		L"\" --mutex \"" + mutexName + L"\" --event \"" + eventName +
		L"\" --preferred \"" + utf8ToWide(m_preferredApplication) + L"\" --parent-pid " +
		std::to_wstring(GetCurrentProcessId());
	std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
	mutableCommand.push_back(L'\0');
	STARTUPINFOW startup{}; startup.cb = sizeof(startup);
	PROCESS_INFORMATION process{};
	if (!CreateProcessW(helper.c_str(), mutableCommand.data(), nullptr, nullptr, FALSE,
		CREATE_NO_WINDOW, nullptr, folder.empty() ? nullptr : folder.c_str(), &startup, &process)) {
		closeHandles(); return;
	}
	CloseHandle(process.hThread);
	m_process = process.hProcess;
}

void SystemMediaClient::stop()
{
	std::lock_guard<std::mutex> lifecycleLock(m_lifecycleMutex);
	if (m_shared && m_sharedMutex) {
		const DWORD wait = WaitForSingleObject(m_sharedMutex, 1000);
		if (wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED) {
			InterlockedExchange(&m_shared->stopRequested, 1);
			ReleaseMutex(m_sharedMutex);
		}
	}
	if (m_wakeEvent) SetEvent(m_wakeEvent);
	if (m_process) {
		const DWORD wait = WaitForSingleObject(m_process, 2000);
		if (wait == WAIT_TIMEOUT) {
			TerminateProcess(m_process, 0);
			WaitForSingleObject(m_process, 2000);
		}
	}
	closeHandles();
}

void SystemMediaClient::setPreferredApplication(std::string preferredApplication)
{
	// The helper is intentionally restarted because its provider owns the
	// preferred-session selection state.
	start(std::move(preferredApplication));
}

void SystemMediaClient::command(Action action, int64_t positionMs)
{
	std::lock_guard<std::mutex> lifecycleLock(m_lifecycleMutex);
	if (!helperRunning() || !m_shared || !m_sharedMutex) return;
	const DWORD wait = WaitForSingleObject(m_sharedMutex, 250);
	if (wait == WAIT_ABANDONED) { ReleaseMutex(m_sharedMutex); return; }
	if (wait != WAIT_OBJECT_0) return;
	const uint32_t next = (m_shared->commandWrite + 1) % kSystemMediaCommandCapacity;
	if (next != m_shared->commandRead) {
		auto &entry = m_shared->commands[m_shared->commandWrite];
		entry.action = static_cast<int32_t>(action);
		entry.positionMs = positionMs;
		m_shared->commandWrite = next;
	}
	ReleaseMutex(m_sharedMutex);
	if (m_wakeEvent) SetEvent(m_wakeEvent);
}

SystemMediaState SystemMediaClient::state() const
{
	std::lock_guard<std::mutex> lifecycleLock(m_lifecycleMutex);
	if (!helperRunning() || !m_shared || !m_sharedMutex) { m_cachedState = {}; return m_cachedState; }
	const DWORD wait = WaitForSingleObject(m_sharedMutex, 0);
	if (wait == WAIT_ABANDONED) {
		ReleaseMutex(m_sharedMutex);
		m_cachedState = {};
		return m_cachedState;
	}
	if (wait != WAIT_OBJECT_0) return m_cachedState;
	m_cachedState.available = m_shared->available != 0;
	m_cachedState.playing = m_shared->playing != 0;
	m_cachedState.canPlay = m_shared->canPlay != 0;
	m_cachedState.canPause = m_shared->canPause != 0;
	m_cachedState.canNext = m_shared->canNext != 0;
	m_cachedState.canPrevious = m_shared->canPrevious != 0;
	m_cachedState.canSeek = m_shared->canSeek != 0;
	m_cachedState.positionMs = m_shared->positionMs;
	m_cachedState.durationMs = m_shared->durationMs;
	m_cachedState.sourceAppId = boundedString(m_shared->sourceAppId, std::size(m_shared->sourceAppId));
	m_cachedState.title = boundedString(m_shared->title, std::size(m_shared->title));
	m_cachedState.artist = boundedString(m_shared->artist, std::size(m_shared->artist));
	m_cachedState.album = boundedString(m_shared->album, std::size(m_shared->album));
	m_cachedState.artworkPath = boundedString(m_shared->artworkPath, std::size(m_shared->artworkPath));
	ReleaseMutex(m_sharedMutex);
	return m_cachedState;
}

bool SystemMediaClient::helperRunning() const
{
	if (!m_process) return false;
	DWORD exitCode = 0;
	return GetExitCodeProcess(m_process, &exitCode) && exitCode == STILL_ACTIVE;
}

void SystemMediaClient::closeHandles()
{
	if (m_shared) { UnmapViewOfFile(m_shared); m_shared = nullptr; }
	if (m_process) { CloseHandle(m_process); m_process = nullptr; }
	if (m_wakeEvent) { CloseHandle(m_wakeEvent); m_wakeEvent = nullptr; }
	if (m_sharedMutex) { CloseHandle(m_sharedMutex); m_sharedMutex = nullptr; }
	if (m_mapping) { CloseHandle(m_mapping); m_mapping = nullptr; }
	m_cachedState = {};
}
