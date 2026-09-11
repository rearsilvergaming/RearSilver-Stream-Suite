#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {
struct Arguments {
	DWORD hubPid = 0;
	DWORD obsPid = 0;
	std::wstring hubPath;
	std::wstring obsPath;
	std::wstring installerPath;
	std::wstring version;
	bool handoffTest = false;
};

std::wstring logPath()
{
	wchar_t value[32768]{};
	const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", value, DWORD(std::size(value)));
	if (!length || length >= std::size(value)) return {};
	const std::filesystem::path folder = std::filesystem::path(std::wstring(value, length)) / L"RearSilver Stream Suite" / L"Updates";
	std::error_code error; std::filesystem::create_directories(folder, error);
	return (folder / L"updater.log").wstring();
}

void log(const std::wstring &message)
{
	const std::wstring path = logPath(); if (path.empty()) return;
	SYSTEMTIME now{}; GetLocalTime(&now);
	std::wofstream output(std::filesystem::path(path), std::ios::app);
	if (output) output << now.wYear << L'-' << now.wMonth << L'-' << now.wDay << L' '
		<< now.wHour << L':' << now.wMinute << L':' << now.wSecond << L" PID=" << GetCurrentProcessId()
		<< L' ' << message << L"\n";
}

bool parse(Arguments &arguments)
{
	int count = 0; LPWSTR *values = CommandLineToArgvW(GetCommandLineW(), &count);
	if (!values) return false;
	for (int index = 1; index < count; ++index) {
		const std::wstring key = values[index];
		auto next = [&]() -> std::wstring { return index + 1 < count ? values[++index] : L""; };
		if (key == L"--hub-pid") arguments.hubPid = wcstoul(next().c_str(), nullptr, 10);
		else if (key == L"--hub-path") arguments.hubPath = next();
		else if (key == L"--obs-pid") arguments.obsPid = wcstoul(next().c_str(), nullptr, 10);
		else if (key == L"--obs-path") arguments.obsPath = next();
		else if (key == L"--installer") arguments.installerPath = next();
		else if (key == L"--version") arguments.version = next();
		else if (key == L"--handoff-test") arguments.handoffTest = true;
		else { LocalFree(values); return false; }
	}
	LocalFree(values);
	return arguments.hubPid != 0 && !arguments.hubPath.empty() &&
		(arguments.handoffTest || !arguments.installerPath.empty());
}

bool processExists(DWORD processId)
{
	if (!processId) return false;
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snapshot == INVALID_HANDLE_VALUE) return true;
	bool found = false; PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
	if (Process32FirstW(snapshot, &entry)) do {
		if (entry.th32ProcessID == processId) { found = true; break; }
	} while (Process32NextW(snapshot, &entry));
	CloseHandle(snapshot); return found;
}

bool waitForProcess(DWORD processId, DWORD timeoutMs)
{
	if (!processId) return true;
	const ULONGLONG deadline = GetTickCount64() + timeoutMs;
	bool stopped = false;
	while (GetTickCount64() < deadline) {
		if (!processExists(processId)) { stopped = true; break; }
		Sleep(100);
	}
	return stopped;
}

bool relaunchApplication(const std::wstring &path)
{
	if (path.empty() || GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
	const std::filesystem::path executable(path);
	const std::wstring folder = executable.parent_path().wstring();
	return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", path.c_str(), nullptr,
		folder.empty() ? nullptr : folder.c_str(), SW_SHOWNORMAL)) > 32;
}

std::wstring installedHubPath()
{
	HKEY key = nullptr;
	constexpr wchar_t productKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\RearSilver Stream Suite";
	if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, productKey, 0, KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS) return {};
	wchar_t location[32768]{}; DWORD type = 0; DWORD bytes = sizeof(location);
	const LSTATUS result = RegQueryValueExW(key, L"InstallLocation", nullptr, &type,
		reinterpret_cast<BYTE *>(location), &bytes);
	RegCloseKey(key);
	if (result != ERROR_SUCCESS || type != REG_SZ || !location[0]) return {};
	return (std::filesystem::path(location) / L"Control Hub" / L"RearSilver-Stream-Suite-Control-Hub.exe").wstring();
}

void restoreSuiteSession(const Arguments &arguments, bool installationCompleted)
{
	const std::wstring registeredHub = installationCompleted ? installedHubPath() : std::wstring{};
	const std::wstring &hubPath = registeredHub.empty() ? arguments.hubPath : registeredHub;
	log(L"hub-relaunch-path=" + hubPath);
	const bool hubStarted = relaunchApplication(hubPath);
	log(hubStarted ? L"hub-relaunch-requested" : L"hub-relaunch-failed");
	// Start the Hub first so its single-instance guard is active before the OBS
	// plugin applies the user's Open with OBS preference.
	Sleep(750);
	const bool obsStarted = relaunchApplication(arguments.obsPath);
	log(obsStarted ? L"obs-relaunch-requested" : L"obs-relaunch-failed");
}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
	Arguments arguments;
	if (!parse(arguments)) { MessageBoxW(nullptr, L"The updater handoff was invalid.", L"RearSilver Stream Suite Updater", MB_OK | MB_ICONERROR); return 2; }
	log(L"handoff-start version=" + arguments.version + (arguments.handoffTest ? L" mode=test" : L" mode=install"));
	if (arguments.obsPid) {
		log(L"obs-close-request-owned-by-plugin");
	}
	if (!waitForProcess(arguments.hubPid, 120000) || !waitForProcess(arguments.obsPid, 600000)) {
		log(L"process-wait-timeout");
		MessageBoxW(nullptr, L"OBS Studio or the Control Hub did not close. The update was not installed.",
			L"RearSilver Stream Suite Updater", MB_OK | MB_ICONWARNING);
		return 3;
	}
	if (arguments.handoffTest) {
		log(L"handoff-test-complete"); restoreSuiteSession(arguments, false); Sleep(750); return 0;
	}
	if (GetFileAttributesW(arguments.installerPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
		log(L"installer-missing"); return 4;
	}
	SHELLEXECUTEINFOW launch{}; launch.cbSize = sizeof(launch); launch.fMask = SEE_MASK_NOCLOSEPROCESS;
	launch.lpVerb = L"runas"; launch.lpFile = arguments.installerPath.c_str(); launch.lpParameters = L"/S /UPDATEHANDOFF"; launch.nShow = SW_SHOWNORMAL;
	if (!ShellExecuteExW(&launch) || !launch.hProcess) { log(L"installer-launch-failed"); return 5; }
	WaitForSingleObject(launch.hProcess, INFINITE); DWORD exitCode = 1; GetExitCodeProcess(launch.hProcess, &exitCode); CloseHandle(launch.hProcess);
	if (exitCode != 0) { log(L"installer-failed exit=" + std::to_wstring(exitCode)); return 6; }
	log(L"installer-complete"); restoreSuiteSession(arguments, true); return 0;
}
