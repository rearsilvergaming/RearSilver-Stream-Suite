#pragma once

#ifdef _WIN32
#include <windows.h>
#endif

#include <cstdint>

namespace RsMusicPcmIpc {
constexpr wchar_t kMappingName[] = L"Local\\RearSilverSuiteMusicPcmPocV1";
constexpr uint32_t kMagic = 0x52534d50; // RSMP
constexpr uint32_t kVersion = 1;
constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kChannels = 2;
constexpr uint32_t kCapacityFrames = 48000 * 4;

struct SharedBuffer {
	uint32_t magic;
	uint32_t version;
	uint32_t sampleRate;
	uint32_t channels;
	uint32_t capacityFrames;
	uint32_t reserved;
	alignas(8) volatile LONG64 writeFrame;
	float interleaved[kCapacityFrames * kChannels];
};
} // namespace RsMusicPcmIpc
