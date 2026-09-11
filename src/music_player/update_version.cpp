#include "update_version.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <utility>
#include <vector>

namespace {
struct Identifier {
	bool numeric = false;
	unsigned long long number = 0;
	std::string text;
};

struct Version {
	unsigned long long major = 0, minor = 0, patch = 0;
	std::vector<Identifier> prerelease;
};

bool number(const std::string &text, unsigned long long &value)
{
	if (text.empty() || (text.size() > 1 && text.front() == '0')) return false;
	value = 0;
	for (const unsigned char character : text) {
		if (!std::isdigit(character)) return false;
		const unsigned digit = character - '0';
		if (value > (std::numeric_limits<unsigned long long>::max() - digit) / 10) return false;
		value = value * 10 + digit;
	}
	return true;
}

std::vector<std::string> split(const std::string &text, char delimiter)
{
	std::vector<std::string> output;
	size_t begin = 0;
	for (;;) {
		const size_t end = text.find(delimiter, begin);
		output.push_back(text.substr(begin, end == std::string::npos ? end : end - begin));
		if (end == std::string::npos) return output;
		begin = end + 1;
	}
}

bool parse(const std::string &raw, Version &version)
{
	if (raw.empty()) return false;
	const size_t plus = raw.find('+');
	if (plus != std::string::npos) {
		const std::string build = raw.substr(plus + 1);
		if (build.empty() || raw.find('+', plus + 1) != std::string::npos) return false;
		for (const std::string &part : split(build, '.')) {
			if (part.empty()) return false;
			for (const unsigned char character : part)
				if (!std::isalnum(character) && character != '-') return false;
		}
	}
	const std::string withoutBuild = raw.substr(0, plus);
	const size_t dash = withoutBuild.find('-');
	const std::string core = withoutBuild.substr(0, dash);
	const auto coreParts = split(core, '.');
	if (coreParts.size() != 3 || !number(coreParts[0], version.major) ||
		!number(coreParts[1], version.minor) || !number(coreParts[2], version.patch)) return false;
	if (dash == std::string::npos) return true;
	const std::string prerelease = withoutBuild.substr(dash + 1);
	if (prerelease.empty()) return false;
	for (const std::string &part : split(prerelease, '.')) {
		if (part.empty()) return false;
		for (const unsigned char character : part)
			if (!std::isalnum(character) && character != '-') return false;
		Identifier identifier; identifier.text = part;
		const bool allDigits = std::all_of(part.begin(), part.end(), [](const unsigned char value) { return std::isdigit(value) != 0; });
		identifier.numeric = number(part, identifier.number);
		if (allDigits && !identifier.numeric) return false;
		version.prerelease.push_back(std::move(identifier));
	}
	return true;
}

int compareNumber(unsigned long long left, unsigned long long right)
{
	return left < right ? -1 : left > right ? 1 : 0;
}
}

bool compareUpdateVersions(const std::string &left, const std::string &right, int &comparison)
{
	Version a, b;
	if (!parse(left, a) || !parse(right, b)) return false;
	comparison = compareNumber(a.major, b.major);
	if (!comparison) comparison = compareNumber(a.minor, b.minor);
	if (!comparison) comparison = compareNumber(a.patch, b.patch);
	if (comparison) return true;
	if (a.prerelease.empty() || b.prerelease.empty()) {
		comparison = a.prerelease.empty() == b.prerelease.empty() ? 0 : a.prerelease.empty() ? 1 : -1;
		return true;
	}
	const size_t shared = std::min(a.prerelease.size(), b.prerelease.size());
	for (size_t index = 0; index < shared; ++index) {
		const Identifier &x = a.prerelease[index], &y = b.prerelease[index];
		if (x.numeric && y.numeric) comparison = compareNumber(x.number, y.number);
		else if (x.numeric != y.numeric) comparison = x.numeric ? -1 : 1;
		else comparison = x.text < y.text ? -1 : x.text > y.text ? 1 : 0;
		if (comparison) return true;
	}
	comparison = compareNumber(a.prerelease.size(), b.prerelease.size());
	return true;
}

std::string updateChannelSlug(const std::string &channel)
{
	std::string slug;
	for (const unsigned char character : channel) {
		if (std::isalnum(character)) slug.push_back(static_cast<char>(std::tolower(character)));
		else if (!slug.empty() && slug.back() != '-') slug.push_back('-');
	}
	while (!slug.empty() && slug.back() == '-') slug.pop_back();
	return slug;
}
