#pragma once

#include <array>
#include <string_view>
#include <utility>

enum RsChatRoleFlag : unsigned {
	RsRoleEveryone = 1u << 0,
	RsRoleSubscriber = 1u << 1,
	RsRoleVip = 1u << 2,
	RsRoleModerator = 1u << 3,
};

struct RsChatCommandDefinition {
	std::string_view id;
	std::string_view category;
	std::string_view label;
	std::string_view syntax;
	std::string_view description;
	unsigned defaultRoles;
};

inline constexpr std::array<RsChatCommandDefinition, 7> kRsChatCommands{{
	{"sr", "Song requests", "Song request", "!sr <title + artist | supported link>", "Search for a track or request a supported YouTube, YouTube Music or Spotify link.", RsRoleEveryone},
	{"play", "Playback controls", "Play", "!play", "Resume the active music player.", RsRoleModerator},
	{"pause", "Playback controls", "Pause", "!pause", "Pause the active music player.", RsRoleModerator},
	{"skip", "Playback controls", "Skip", "!skip", "Skip the current track and continue with the next available track.", RsRoleModerator},
	{"restart", "Playback controls", "Restart", "!restart", "Restart the current track from the beginning.", RsRoleModerator},
	{"previous", "Playback controls", "Previous", "!previous / !prev", "Return to the previously played track.", RsRoleModerator},
	{"remove", "Queue moderation", "Remove request", "!remove #R<number>", "Remove a waiting request by its stable request ID.", RsRoleModerator},
}};

inline constexpr std::array<std::pair<std::string_view, unsigned>, 4> kRsChatRoles{{
	{"everyone", RsRoleEveryone},
	{"subscriber", RsRoleSubscriber},
	{"vip", RsRoleVip},
	{"moderator", RsRoleModerator},
}};

inline bool rsChatCommandDefault(std::string_view command, std::string_view role)
{
	for (const auto &definition : kRsChatCommands) {
		if (definition.id != command)
			continue;
		for (const auto &[roleId, flag] : kRsChatRoles)
			if (roleId == role)
				return (definition.defaultRoles & flag) != 0;
	}
	return false;
}
