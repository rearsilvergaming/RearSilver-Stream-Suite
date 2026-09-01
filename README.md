# RearSilver Stream Suite

RearSilver Stream Suite is a custom dock plugin for OBS Studio. It brings common stream controls, scene and source access, live statistics, and several creator tools into one dockable interface.

The project is under active development. Core OBS controls and most enhancement tools are functional; the music and Twitch song-request system is still experimental and incomplete.

## Current features

- Streaming, recording, replay-buffer, virtual-camera, and Studio Mode controls
- Optional hold-to-stop safety lock
- Embedded access to OBS Scenes and Sources docks
- Live stream and recording statistics
- Responsive horizontal, vertical, and automatic layouts
- Custom themes with persistent UI settings
- Browser-source refresh tools
- Quick Text source creation and presets
- Countdown and count-up browser overlay
- External-program auto-start manager for Windows
- Instant Replay creation, playback, hotkey, and frame background
- Control Hub music, queue, playlist, and Twitch integration UI

## Project status

This is pre-release software. In particular:

- The music WebSocket transport and playback controller are not yet connected.
- Song search, validation, queue processing, and Twitch chat replies are unfinished.
- Some Instant Replay frame controls are placeholders.
- Packaging and clean installation still need broader testing.
- Windows is currently the only supported platform because Auto-Start uses Windows APIs.

## Building on Windows

Requirements:

- Visual Studio 2022 with C++ development tools
- CMake 3.20 or newer
- Qt 6 with Core, Gui, Widgets, and Network components
- A configured OBS Studio source and build tree

Configure the project by supplying the matching OBS source and build locations. If Qt is not discoverable automatically, also supply `Qt6_DIR` or `CMAKE_PREFIX_PATH`.

```powershell
cmake -S . -B build_x64 `
  -DOBS_SOURCE_DIR="C:/path/to/obs-studio" `
  -DOBS_BUILD_DIR="C:/path/to/obs-studio/build_x64" `
  -DQt6_DIR="C:/Qt/6.8.3/msvc2022_64/lib/cmake/Qt6"

cmake --build build_x64 --config RelWithDebInfo
```

The resulting plugin DLL is written beneath `build_x64/RelWithDebInfo`.

## Repository layout

- `src/` — plugin, dock UI, enhancements, and music integration
- `data/` — localisation and plugin data
- `cmake/` — shared OBS plugin-template build support
- `.github/` — build and formatting workflows inherited from the OBS plugin template

## Safety and privacy

The Twitch integration stores access tokens in the operating system's Qt settings storage. Secure credential storage and encrypted Twitch IRC are planned before a public release. Do not treat the current music integration as production-ready.

## Licence

This project is distributed under the terms in [LICENSE](LICENSE).

