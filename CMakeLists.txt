cmake_minimum_required(VERSION 3.20)
project("RearSilver-Stream-Suite" LANGUAGES CXX)

set(RS_DEFAULT_EDITION "pro" CACHE STRING "Default product edition: free or pro")
set_property(CACHE RS_DEFAULT_EDITION PROPERTY STRINGS free pro)
if(NOT RS_DEFAULT_EDITION STREQUAL "free" AND NOT RS_DEFAULT_EDITION STREQUAL "pro")
    message(FATAL_ERROR "RS_DEFAULT_EDITION must be either 'free' or 'pro'.")
endif()

set(OBS_SOURCE_DIR "" CACHE PATH "Path to the OBS Studio source tree")
set(OBS_BUILD_DIR "" CACHE PATH "Path to the configured OBS Studio build tree")
set(DEPS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.deps")

if(NOT OBS_SOURCE_DIR OR NOT EXISTS "${OBS_SOURCE_DIR}/libobs/obs.h")
    message(FATAL_ERROR "Set OBS_SOURCE_DIR to an OBS Studio source tree (for example: cmake -DOBS_SOURCE_DIR=C:/path/to/obs-studio).")
endif()

if(NOT OBS_BUILD_DIR OR NOT EXISTS "${OBS_BUILD_DIR}/config/obsconfig.h")
    message(FATAL_ERROR "Set OBS_BUILD_DIR to the matching configured OBS build tree (for example: cmake -DOBS_BUILD_DIR=C:/path/to/obs-studio/build_x64).")
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

if(WIN32)
	add_executable(RearSilver-Music-Player WIN32 src/music_player/main.cpp src/music_player/app.rc)
	target_include_directories(RearSilver-Music-Player PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniaudio")
	target_link_libraries(RearSilver-Music-Player PRIVATE gdiplus)
	set_target_properties(RearSilver-Music-Player PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)
	if(MSVC)
		target_compile_options(RearSilver-Music-Player PRIVATE /utf-8)
	endif()
endif()

# ----- Plugin target -----
add_library(${PROJECT_NAME} MODULE
	resources.qrc
    src/plugin-main.cpp
    src/rs_main_dock.cpp
    src/rs_main_dock.hpp
    src/rs_scenes_sources.cpp
    src/rs_scenes_sources.hpp
    src/rs_helpers.cpp
    src/rs_panels.cpp
    src/rs_settings.cpp
    src/rs_navigation.cpp
    src/rs_menu.cpp
    src/rs_theme.cpp
    src/rs_controls.cpp
    src/rs_layout.cpp
    src/rs_stats.cpp
    src/rs_stats.hpp
    src/rs_entitlements.cpp
    src/rs_entitlements.hpp

    src/enhancements/rs_browser_refresh.cpp
    src/enhancements/rs_browser_refresh.hpp
    src/enhancements/rs_quick_text.cpp
    src/enhancements/rs_quick_text.hpp
    src/enhancements/rs_timer.cpp
    src/enhancements/rs_timer.hpp
    src/enhancements/rs_auto_start.cpp
    src/enhancements/rs_auto_start.hpp
    src/enhancements/rs_instant_replay.cpp
    src/enhancements/rs_instant_replay.hpp

    src/rs_music/rs_music.cpp
    src/rs_music/rs_music.hpp
    src/rs_music/rs_music_helpers.cpp
    src/rs_music/rs_music_helpers.hpp
    src/rs_music/rs_music_local_player.cpp
    src/rs_music/rs_music_local_player.hpp
	 src/rs_music/rs_music_metadata.cpp
	 src/rs_music/rs_music_metadata.hpp
    src/rs_music/rs_music_server.cpp
    src/rs_music/rs_music_server.hpp
    src/rs_music/rs_music_ws.cpp
    src/rs_music/rs_music_ws.hpp
    src/rs_music/ui/rs_music_now_playing.cpp
    src/rs_music/ui/rs_music_now_playing.hpp
    src/rs_music/ui/rs_music_queue.cpp
    src/rs_music/ui/rs_music_queue.hpp
    src/rs_music/ui/rs_music_requests.cpp
    src/rs_music/ui/rs_music_requests.hpp
    src/rs_music/ui/rs_music_playlist.cpp
    src/rs_music/ui/rs_music_playlist.hpp
    src/rs_music/ui/rs_music_settings.cpp
    src/rs_music/ui/rs_music_settings.hpp
	 src/rs_music/ui/rs_music_setup.cpp
	 src/rs_music/ui/rs_music_setup.hpp
    src/rs_music/state/rs_music_state.cpp
    src/rs_music/state/rs_music_state.hpp
    src/rs_music/state/rs_music_track.hpp

    src/rs_music/rs_music_twitch_auth.cpp
    src/rs_music/rs_music_twitch_auth.hpp
    src/rs_music/rs_music_twitch_irc_sender.cpp
    src/rs_music/rs_music_twitch_irc_sender.hpp
    src/rs_music/rs_music_twitch_irc_reader.cpp
    src/rs_music/rs_music_twitch_irc_reader.hpp
    src/rs_music/rs_music_command_router.cpp
    src/rs_music/rs_music_command_router.hpp
    src/rs_music/rs_music_controller.cpp
    src/rs_music/rs_music_controller.hpp

    src/rs_music/rs_music_tls_probe.cpp
    src/rs_music/rs_music_tls_probe.hpp
    )

target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${OBS_SOURCE_DIR}/libobs"
    "${OBS_SOURCE_DIR}/frontend/api"
    "${OBS_BUILD_DIR}/libobs"
    "${OBS_BUILD_DIR}/frontend/api"
    "${OBS_BUILD_DIR}/config"
    "${DEPS_DIR}/include"
)

# ----- Link OBS from actual build (FIXED PATH) -----
target_link_libraries(${PROJECT_NAME}
    PRIVATE
        "${OBS_BUILD_DIR}/libobs/RelWithDebInfo/obs.lib"
        "${OBS_BUILD_DIR}/frontend/api/RelWithDebInfo/obs-frontend-api.lib"
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Network
)

if(RS_DEFAULT_EDITION STREQUAL "free")
    target_compile_definitions(${PROJECT_NAME} PRIVATE RS_DEFAULT_EDITION_FREE=1)
else()
    target_compile_definitions(${PROJECT_NAME} PRIVATE RS_DEFAULT_EDITION_PRO=1)
endif()

set_target_properties(${PROJECT_NAME}
    PROPERTIES
        OUTPUT_NAME "RearSilver-Stream-Suite"
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        AUTOMOC ON
        AUTOUIC ON
        AUTORCC ON
)

if(WIN32)
	add_dependencies(${PROJECT_NAME} RearSilver-Music-Player)
	add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
		COMMAND "${CMAKE_COMMAND}" -E copy_if_different
			"$<TARGET_FILE:RearSilver-Music-Player>" "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
		COMMENT "Package RearSilver Music Player companion"
		VERBATIM)
endif()

# Force UTF-8 for MSVC (fixes C4828 warnings)
if (MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8)
endif()


