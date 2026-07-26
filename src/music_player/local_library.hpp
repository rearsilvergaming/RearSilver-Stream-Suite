#pragma once

#include "music_hub.hpp"

#include <string>
#include <vector>

std::vector<std::wstring> chooseLocalAudioFiles(void *owner);
std::wstring chooseLocalAudioFolder(void *owner);
std::vector<HubTrack> scanLocalAudioFiles(const std::vector<std::wstring> &files);
std::vector<HubTrack> scanLocalAudioFolder(const std::wstring &folder);

