#pragma once

#include "music_hub.hpp"

#include <string>
#include <vector>

struct HubPlaylistResult {
	std::string label;
	std::string sourceUrl;
	std::vector<HubTrack> tracks;
	std::string error;
};

struct HubSearchResult {
	HubTrack track;
	std::string error;
};

HubPlaylistResult resolveHubPlaylist(const std::string &playlistUrl);
HubSearchResult resolveHubSearch(const std::string &query, const std::string &requestedBy);
