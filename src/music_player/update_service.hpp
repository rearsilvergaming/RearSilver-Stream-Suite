#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class UpdateCheckStatus { Disabled, Checking, UpToDate, Available, Error };

struct UpdateCheckResult {
	UpdateCheckStatus status = UpdateCheckStatus::Disabled;
	bool manual = false;
	bool mandatory = false;
	bool currentVersionSupported = true;
	std::string availableVersion;
	std::string publishedAt;
	std::string releaseNotesUrl;
	std::vector<std::string> releaseNotes;
	std::string installerFilename;
	std::string installerSha256;
	std::string downloadRequestUrl;
	std::string message;
	std::uint64_t installerSize = 0;
	bool downloadAvailable = false;
};

UpdateCheckResult checkForSuiteUpdate(const std::string &baseUrl, const std::string &channel,
	const std::string &currentVersion, bool manual);
