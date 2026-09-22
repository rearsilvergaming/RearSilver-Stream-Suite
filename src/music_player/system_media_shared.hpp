#pragma once

#include "system_media_provider.hpp"

#include <cstdint>

inline constexpr uint32_t kSystemMediaSharedMagic = 0x52534d53; // RSMS
inline constexpr uint32_t kSystemMediaSharedVersion = 1;
inline constexpr uint32_t kSystemMediaCommandCapacity = 32;
inline constexpr uint32_t kSystemMediaTextCapacity = 2048;

struct SystemMediaSharedCommand {
	int32_t action = 0;
	int64_t positionMs = 0;
};

struct SystemMediaSharedState {
	uint32_t magic = kSystemMediaSharedMagic;
	uint32_t version = kSystemMediaSharedVersion;
	volatile long stopRequested = 0;
	uint32_t commandRead = 0;
	uint32_t commandWrite = 0;
	SystemMediaSharedCommand commands[kSystemMediaCommandCapacity]{};
	uint32_t available = 0;
	uint32_t playing = 0;
	uint32_t canPlay = 0;
	uint32_t canPause = 0;
	uint32_t canNext = 0;
	uint32_t canPrevious = 0;
	uint32_t canSeek = 0;
	int64_t positionMs = 0;
	int64_t durationMs = 0;
	char sourceAppId[kSystemMediaTextCapacity]{};
	char title[kSystemMediaTextCapacity]{};
	char artist[kSystemMediaTextCapacity]{};
	char album[kSystemMediaTextCapacity]{};
	char artworkPath[kSystemMediaTextCapacity]{};
};

