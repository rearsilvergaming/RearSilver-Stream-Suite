#pragma once

#include <string>

// Compare Semantic Versioning-compatible release strings. Returns a negative
// value when left is older, zero when equal, and a positive value when newer.
// Build metadata is ignored. Invalid versions return false.
bool compareUpdateVersions(const std::string &left, const std::string &right, int &comparison);

// Stable identifier used by the update API for the compiled display channel.
std::string updateChannelSlug(const std::string &channel);
