# RearSilver Stream Suite — Launch Product Audit

Status: first source-backed launch audit  
Audit date: 31 August 2026  
Scope: Windows Control Hub, OBS dock/plugin, stream tools, music system, packaging, documentation and commercial readiness

## 1. Audit rules

This document treats the current source as the authority. Existing README text, historical audit notes and UI labels are supporting evidence only because several are now out of date.

The status terms used here are:

- **Implemented** — the production path exists in the current source.
- **Developer production-used** — the Hub and dock have been used regularly during the developer's own live streams and are proven in that established workflow.
- **User confirmed** — the behaviour has also been exercised successfully during recent focused testing.
- **Needs release testing** — implemented, but not yet proven through the full clean-install and failure-recovery matrix.
- **Launch blocker** — must be resolved before a paid public release.
- **Post-launch** — intentionally outside the first release.

This is a product and engineering audit, not legal advice. Licensing conflicts require a deliberate licensing decision and, if necessary, qualified legal review.

The Hub and OBS dock are not unproven prototypes. They have been used regularly in real streams by the developer, who knows the product and its navigation thoroughly. Private beta testing is therefore intended primarily to validate clean installation, unfamiliar-user discoverability, compatibility across other streamers' environments, failure recovery and usability without developer guidance.

## 2. Product definition

RearSilver Stream Suite is currently best described as:

> A Windows companion and OBS Studio plugin that brings live stream controls, managed on-stream tools, music playback, viewer song requests and configurable overlays into one connected workflow.

The product has two cooperating surfaces:

- **Control Hub** — the main setup, management and design application.
- **OBS dock** — the compact live-operation surface inside OBS.

That distinction should drive the UI overhaul, wiki and selling page. The Hub is where a creator configures the stream. The dock is where they operate it without leaving OBS.

## 3. Launch feature inventory

### 3.1 Control Hub shell and setup

Status: **Implemented; needs full UI and release testing**

- Dedicated Windows Control Hub companion application.
- Collapsible navigation with Now Playing, Queue & Requests, Library, Music Overlay, Stream Tools, Suite Settings and Commands.
- Guided five-step setup with saved progress:
  - Hub & Startup
  - OBS & Overlays
  - Accounts
  - Music & Requests
  - Commands
- Optional launch of the Hub after OBS finishes loading.
- Hub and OBS remain independently closable.
- App Manager for saved optional applications, individual/all launch and close actions.
- Locally persisted settings and setup progress.
- Local diagnostics and lifecycle logs.

Launch notes:

- “Settings” and “Accounts” remain internal page names, while Suite Settings is the visible consolidated route. Remove or fully retire migration residue during the Hub UI audit.
- App Manager appears both in Suite Settings and Stream Tools and is explicitly labelled as being retained during migration. Decide its permanent home.

### 3.2 OBS dock — system controls

Status: **Implemented; needs dock overhaul and release testing**

- Start and stop streaming.
- Start and stop recording.
- Start and stop Replay Buffer.
- Start and stop Virtual Camera.
- Toggle Studio Mode.
- Optional hold-to-stop safety protection for destructive stop actions.
- Live state-aware button labels.
- Current scene display.
- Native OBS settings shortcut.
- Responsive Auto, Horizontal and Vertical dock layouts.
- Persistent theme and layout selection.
- Default, OBS-style, dark/calm/night and colour-vision-accessible themes.

### 3.3 OBS dock — scenes, sources and statistics

Status: **Implemented; needs wider scenario testing**

- Embed the native OBS Scenes and Sources panels inside the Suite dock.
- Return panels to OBS or bring them back into the Suite.
- Connecting, failure and retry states.
- Preserve the selected panel tab while moving the native docks.
- Rendering statistics including FPS, frame/render timing and missed/skipped frames.
- Streaming and recording output state, dropped frames, transferred data and bitrate.

Required release scenarios:

- Scene collection changes.
- Native dock destruction and recreation.
- Source and scene deletion/rename while the Suite is open.
- Very narrow, short, floating and multi-monitor dock arrangements.

### 3.4 Stream Tools — Browser Refresh

Status: **Implemented; needs release testing**

- Refresh browser sources in the current scene.
- Refresh browser sources across all scenes.
- Recursive discovery inside nested groups.
- Duplicate prevention.
- Uses browser-source refresh procedures without toggling visibility.
- Available from the Hub and compact OBS dock quick actions.

### 3.5 Stream Tools — Quick Text

Status: **Implemented; needs release testing**

- Create and repair a managed OBS Quick Text overlay when used.
- Enter, preview, show and hide a message.
- Configure font, font size, weight and colour.
- Save reusable text presets.
- Apply current styling to a preset.
- Remove presets with confirmation.
- Hub configuration with dock show/hide quick actions.
- Loopback-only managed overlay server.

Required release scenarios:

- Source deletion and rename recovery.
- Scene collection changes.
- Unicode, emoji, multiline and very long text.
- Font fallback and clean-machine font availability.

### 3.6 Stream Tools — Timer and Countdown

Status: **Implemented; needs release testing**

- Countdown and stopwatch modes.
- Configurable title, duration and live preview.
- Font, font weights, title/time sizes and text colour.
- Optional shadow and configurable background, opacity and corner radius.
- Start, pause/resume, reset, show and hide controls.
- Optional hide-on-finish behaviour.
- Configurable linger-at-zero duration.
- Optional completion sound with test and clear actions.
- Managed OBS browser source creation/repair.
- Hub configuration with compact OBS dock controls.
- Loopback-only overlay and media delivery.

Required release scenarios:

- Supported audio formats and missing/moved completion sound files.
- OBS restart/crash while running or paused.
- Source deletion/rename and scene collection changes.
- Timer completion while its scene is inactive.

### 3.7 Stream Tools — Instant Replay

Status: **Implemented; needs intensive release testing**

- Configure Replay Buffer duration.
- Optionally start Replay Buffer with OBS.
- Start and stop Replay Buffer from Hub or dock.
- Save and immediately play an instant replay.
- Show and hide the managed replay display.
- Optional automatic hide after playback.
- Open the OBS recording/replay folder.
- Managed source creation and repair.
- Simple shared-overlay-scene or advanced direct-per-scene placement.
- Frame designer with heading, font, weight and alignment.
- Frame, border and heading colours.
- Frame opacity, replay size, border width and corner radius.
- Live frame preview.
- Scene-aware delayed hiding that does not alter a later active scene.

Required release scenarios:

- Empty, invalid, inaccessible and changing replay folders.
- Replay Buffer disabled or unavailable in OBS.
- Source/scene rename and deletion.
- Scene collection changes and nesting-cycle prevention.
- Shutdown while replay timers or playback are active.
- Repeated saves and overlapping replay actions.

### 3.8 Managed overlay placement

Status: **Implemented; needs release testing**

- **Simple mode** keeps managed tools in `RearSilver Stream Suite | Stream Overlays` and nests that scene into the active scene.
- **Advanced mode** places managed tools directly into active scenes.
- Existing placements are not destructively deleted when changing mode.
- Individual tools create or repair their own infrastructure when used.
- Collision and OBS scene-nesting-cycle errors have explicit handling.

### 3.9 Music providers and playback

Status: **Implemented; YouTube/local/media-key paths user confirmed; Spotify partially confirmed**

- Built-in YouTube and YouTube Music playlist playback.
- Local audio file and recursive folder libraries.
- Local metadata extraction including artwork, album, disc and track numbers.
- Natural filename fallback ordering.
- Multi-disc canonical album order.
- Spotify desktop app integration through the Spotify Web API.
- Spotify browser sessions and unrelated Windows media sessions are deliberately excluded.
- Play, pause, previous, next/skip, restart and stop controls where supported.
- Polished matching transport icons with click feedback.
- Keyboard media-key ownership for built-in YouTube/local playback while focused or minimised.
- Media keys released when Spotify is selected or the Hub closes.
- OBS dock and Hub controls remain connected through local IPC.
- Neutral OBS Application Audio Capture setup; the Suite does not silently alter stream, recording or VOD routing.

Spotify limitations that must be prominent:

- Spotify Premium is required for Hub playback, queue and request control.
- The Spotify desktop app is required.
- Each user must create/connect a Spotify developer application and enter its Client ID.
- A Client Secret must never be requested.
- Free-account media-key behaviour was confirmed, but complete Premium queue/control behaviour still needs a real Premium test account.

### 3.10 Queue, requests, history and fallback rotation

Status: **Implemented and focused tests passing; core behaviours user confirmed for YouTube/local**

- Waiting viewer requests are session-only and always take priority.
- Accepted requests never interrupt the currently playing track.
- Every accepted request receives a stable session ID such as `#R12`.
- Session history records every track that actually starts, including completed requests.
- Previous walks backwards through session history across provider changes.
- After Previous, Next walks forwards through history before normal fallback playback.
- A newly waiting request still outranks forward history.
- Fallback rotation is separate from waiting requests and history.
- Canonical YouTube playlist/local library order is preserved.
- Shuffle affects only the active fallback rotation.
- Restore Order continues from the current track to its canonical successor.
- Play From Beginning immediately starts canonical track one.
- Waiting, current and forward-history requests survive Play From Beginning correctly.
- Independent YouTube and local continuation positions survive provider switching.
- Startup preferences support:
  - Continue where I left off or Start from beginning.
  - In library order or Shuffle on Hub launch.
- Startup preferences apply to YouTube and local libraries, not Spotify.
- Session requests/history are excluded from persisted JSON.
- The Queue page supports pagination and double-click playback selection.

Automated coverage currently exists for the core queue model, provider switching, persistence exclusions, canonical order, shuffle/restore/restart and natural local ordering. It does not yet cover the full Hub UI, OBS bridge, Twitch, Spotify or installer.

### 3.11 Viewer song requests and moderation

Status: **Implemented; needs end-to-end release testing under live Twitch conditions**

- One global Enable Song Requests setting.
- Requests follow the active provider:
  - YouTube source accepts YouTube search and supported YouTube/YouTube Music links.
  - Spotify source accepts Spotify searches/links when Premium is connected.
  - Local source automatically disables requests.
- Disabled requests reply: “Song requests are turned off for this stream.”
- Minimum requester role.
- Total queue limit.
- Per-user queue limit.
- Maximum track duration.
- Optional fallback-playlist-only requests.
- Duplicate prevention.
- Independent Subscriber, VIP, Moderator and Broadcaster limit exemptions.
- YouTube strict/moderate safe search.
- Optional music-category-only filter.
- Optional age-restricted-video rejection.
- Stable-ID request removal.
- Configurable sender account with automatic streamer fallback when the bot is unavailable.
- Optional now-playing announcements and selectable symbols.
- Configurable non-requested-track label.

Safety wording must remain honest: search/category/age checks reduce risk but cannot guarantee that unsuitable material will never be submitted or played.

### 3.12 Twitch accounts and chat commands

Status: **Implemented; needs clean-account, token-expiry and outage testing**

- Streamer Twitch account for chat reading, viewer roles and fallback replies.
- Optional bot account for confirmations and now-playing messages.
- Device-code login, reconnect and logout flows.
- Twitch access/refresh credentials stored in Windows Credential Manager by the Hub.
- The OBS plugin receives a transient validated session over local IPC.
- Current command catalogue:
  - `!sr <title + artist | supported music link>`
  - `!play`
  - `!pause`
  - `!skip`
  - `!restart`
  - `!previous` / `!prev`
  - `!remove #R<number>`
- Per-command additive permissions for Everyone, Subscribers, VIPs and Moderators.
- Broadcaster access is always retained.
- Standalone command quick-reference page reflects configured roles.

Required release scenarios:

- First login, denied login and expired device codes.
- Rotated refresh tokens.
- Twitch outage, DNS failure and temporary TLS failure.
- Bot disconnected while streamer remains connected.
- Account switch and channel identity mismatch.
- Unicode display names and messages.
- Rate limiting and repeated commands.

### 3.13 Music overlay and text output

Status: **Implemented; needs release testing**

- Managed OBS music overlay with Hub show/hide controls.
- Toggle artwork, title, artist, album, requester and progress.
- Optional blurred-artwork background and transparent background.
- Artwork on left or right.
- Elapsed, total, remaining or hidden timing modes.
- Custom heading/stream name.
- Font, title size and body size.
- Ellipsis, automatic fit or continuous title scrolling.
- Scroll direction and speed.
- Background, text and accent colours with custom colour picker.
- Background opacity and configurable canvas size.
- Local reset to defaults.
- Optional now-playing text file with title, artist, album and requester tokens.
- Loopback-only OBS overlay endpoints.

### 3.14 Theme and accessibility foundations

Status: **Implemented foundations; needs UI audit**

- Persistent themes.
- High-contrast option.
- Protanopia-, deuteranopia- and tritanopia-conscious themes.
- Responsive Hub HTML layouts and dock orientation modes.
- Sora bundled for branded surfaces.

The launch audit must still verify keyboard navigation, focus visibility, accessible names, colour contrast, scaling, Windows text scaling and screen-reader behaviour. Theme names containing “Pro” are presentation labels, not evidence of working entitlements.

## 4. Commercial and release blockers

### P0 — must be resolved before public release

1. **Installer does not install the complete Hub runtime.**
   - It currently copies the plugin DLL, Hub EXE, Qt TLS backend and locale only.
   - It omits required Hub HTML surfaces, WebView2 loader, CEF binaries/resources/locales, bundled font and branding assets.
   - A clean installation is therefore not reliable.

2. **No uninstall, repair or upgrade path.**
   - The NSIS script has no uninstaller registration or uninstall section.
   - Settings/credentials preservation and removal policy is undefined.

3. **Contradictory licensing terms.**
   - Root `LICENSE` is GPLv2.
   - Installer `license.txt` is a proprietary EULA forbidding redistribution, modification and reverse engineering.
   - A single coherent distribution/licensing position is required before sales.

4. **Free/Pro enforcement does not exist as a commercial system.**
   - Edition is selected at compile time.
   - Feature keys exist, but runtime checks are effectively unwired.
   - There is no licence key/account activation, device policy, offline grace, upgrade, refund/revocation or recovery flow.

5. **Clean-machine release validation is absent.**
   - Must cover supported Windows/OBS versions, missing WebView2, no prior Suite files, no developer runtimes and non-admin user operation after install.

6. **Code signing and publisher trust are unresolved for Windows launch.**
   - Final EXE, DLL and installer signing/version metadata need a defined process.

7. **Legacy dock music/auth implementation remains compiled.**
   - It contains obsolete phase/stub logic and dormant `QSettings` token-persistence methods.
   - The launch build should remove dead paths or prove they are unreachable and harmless.

### P1 — required for a credible 1.0 experience

1. Full Hub UI audit and consolidation of migration residue.
2. OBS dock visual overhaul to match the Hub.
3. Full setup, error, empty, loading, disconnected and recovery-state audit.
4. Real Spotify Premium end-to-end test.
5. Live Twitch soak and failure testing.
6. Scene collection/source rename/delete and OBS restart/crash matrix for every managed tool.
7. Diagnostics retention/cleanup policy; current trace files can accumulate.
8. Version information must come from one source rather than duplicated `1.0.0` values.
9. Replace the outdated README and destructive historical `Commands.txt` instructions.
10. Decide whether launch is Windows-only and remove misleading macOS/Linux packaging promises if so.
11. Audit third-party notices and redistribution obligations for OBS/Qt/CEF/WebView2/miniaudio/fonts and other bundled components.
12. Add privacy policy and plain-language local-data/credential documentation.

### P2 — acceptable after 1.0 if clearly scoped

- Additional saved timer presets and completion actions.
- Scene/source search and counts inside the dock.
- Richer per-program running/error presentation.
- Additional automated UI/integration coverage.
- Further UI density and copy refinements after the main audit.

## 5. Free vs Pro decisions required

The existing source suggests a provisional Free baseline of:

- Core OBS dock.
- Stream controls.
- Scenes and Sources integration.
- Stream statistics.

Everything else is currently catalogued as a potential Pro feature, but that split has not been product-tested and may make Free feel like a demo rather than a useful product.

Before implementation, decide:

1. Is Free permanently useful, a time-limited trial, or both?
2. Which single workflow should make Free valuable on its own?
3. Is Pro a one-time purchase, subscription or both?
4. Does a licence cover one person, one Windows account or a fixed number of devices?
5. Is account creation mandatory?
6. How long must Pro work offline?
7. What happens when activation servers are unavailable?
8. How are device changes, refunds, chargebacks and revoked keys handled?
9. Can users move between Free and Pro without losing configuration?
10. Which locked controls remain visible, and what explanation/upgrade action appears?
11. Are overlays exported for use on a second streaming machine?
12. What telemetry, if any, is needed for licensing, and how is consent/privacy handled?

Recommended product principle:

> Free should complete a real streaming workflow; Pro should add depth, automation and audience interaction rather than removing basic safety or reliability.

No final tier matrix should be published until these questions and the licensing architecture are approved.

## 6. Safe launch-page claims

The following claims are supported by the current implementation, subject to final release testing:

- Control your stream and managed tools from a connected Windows Hub and OBS dock.
- Keep essential live controls inside OBS.
- Refresh browser sources across one scene or every scene.
- Create styled Quick Text messages without manually building sources.
- Run configurable countdowns and stopwatches on stream.
- Save and present framed instant replays.
- Play YouTube playlists and ordered local music libraries.
- Keep viewer requests separate from fallback playlists and playback history.
- Accept moderated Twitch song requests with role, queue, duration and content-safety controls.
- Preserve canonical album and playlist order while shuffling only the active session rotation.
- Design a configurable now-playing overlay.
- Use keyboard media controls for built-in playback even while the Hub is minimised.
- Connect an optional Twitch bot account while retaining streamer fallback.
- Store Hub Twitch credentials in Windows Credential Manager and protect Spotify refresh tokens with Windows encryption.

Claims that must not be made yet:

- “Works on macOS/Linux.”
- “Guaranteed safe” or “copyright safe” music.
- “Works with Spotify Free.”
- “Works with Spotify Web Player or any desktop media player.”
- “Automatic VOD music separation.”
- “Secure licensing,” “lifetime licence,” “offline activation” or similar Free/Pro promises.
- “One-click installer” until clean-install/upgrade/uninstall validation passes.
- “Never loses settings” or other absolute recovery claims.
- Any claim that all features have passed complete production testing.

## 7. Initial selling-page structure

### Hero

**Run your stream. Shape the experience. Stay inside the moment.**

RearSilver Stream Suite connects a dedicated Control Hub with a compact OBS dock, giving streamers one place to operate essential controls, on-stream tools, music and viewer requests.

Primary action: **Get RearSilver Stream Suite**  
Secondary action: **See everything it can do**

### Value sections

1. **A calmer live workflow**
   - Stream, recording, replay buffer, virtual camera, scenes, sources and live statistics inside OBS.

2. **On-stream tools without source-building busywork**
   - Quick Text, timers, browser refresh and framed instant replay with managed OBS setup.

3. **Music built for a live queue**
   - YouTube playlists, ordered local libraries, session shuffle, history-aware Previous/Next and request-first playback.

4. **Viewer requests with guardrails**
   - Twitch roles, limits, stable request IDs, duplicate protection and configurable YouTube safety checks.

5. **Make it look like your stream**
   - Branded themes and a configurable music overlay with artwork, progress, colours, fonts and responsive title handling.

6. **Hub depth, dock speed**
   - Configure in the Hub; operate the essentials from OBS.

### Required compatibility block

- Windows only for version 1.0, unless cross-platform support is completed and validated.
- Supported OBS Studio versions to be established by the release matrix.
- Microsoft Edge WebView2 Runtime requirement or installer handling to be documented.
- Spotify features require Spotify Premium, the desktop app and a connected Spotify developer application.
- YouTube and Twitch features require internet access.
- Broadcasting music remains the creator’s legal responsibility.

## 8. Wiki information architecture

### Start here

1. What RearSilver Stream Suite is
2. System requirements and supported versions
3. Download, install and first launch
4. Guided setup walkthrough
5. Updating, repairing and uninstalling
6. Free vs Pro and licence management

### Control Hub

1. Hub navigation
2. Starting the Hub with OBS
3. App Manager
4. Suite Settings
5. Diagnostics and log locations
6. Backing up or resetting local settings

### OBS dock

1. Adding and docking RearSilver Stream Suite
2. Controls and Safety Lock
3. Scenes & Sources
4. Statistics
5. Quick Actions
6. Layout modes and themes
7. Hub connection states

### Stream Tools

1. Managed overlay placement: Simple vs Advanced
2. Browser Refresh
3. Quick Text and presets
4. Timer and Countdown
5. Completion sounds
6. Instant Replay setup
7. Instant Replay Frame Designer
8. Repairing missing or renamed sources

### Music

1. Music system overview
2. Creating OBS Music Capture
3. YouTube playlist playback
4. Local files, folders and album order
5. Spotify Premium setup
6. Choosing the active provider
7. Playback controls and keyboard media keys
8. Queue, requests, history and fallback rotation
9. Shuffle, Restore Order and Play From Beginning
10. Startup playback preferences
11. Music Overlay designer
12. Now-playing text-file output
13. Music, copyright and Twitch VOD audio routing

### Twitch and requests

1. Connecting the streamer account
2. Connecting an optional bot account
3. Choosing the chat sender
4. Enabling and disabling requests
5. Provider-specific request behaviour
6. Role and command permissions
7. Queue, duration and per-user limits
8. YouTube content-safety settings
9. Stable request IDs and moderation
10. Complete command reference

### Troubleshooting

1. Hub not connected to OBS
2. Hub will not open or closes immediately
3. WebView2/CEF runtime problems
4. Managed source missing or conflicting
5. Browser sources do not refresh
6. Timer sound does not play
7. Replay Buffer or replay playback fails
8. YouTube/local music has no OBS audio
9. Spotify shows no playback or queue
10. Twitch account or bot will not connect
11. Requests are rejected or disabled
12. Collecting diagnostics for support

### Policies and reference

1. Privacy and locally stored data
2. Credential storage
3. Licence terms
4. Third-party notices
5. Accessibility
6. Release notes and known issues

## 9. Recommended next sequence

1. Approve this feature inventory and correct any product-intent mismatches.
2. Decide the coherent software licence and commercial model.
3. Design the Free/Pro tier matrix and runtime entitlement architecture.
4. Perform the Hub UI audit using this inventory as the required-state checklist.
5. Overhaul the OBS dock as the compact live-operation counterpart.
6. Fix installer/package completeness, upgrade and uninstall behaviour.
7. Run the full clean-install and integration release matrix.
8. Write the wiki against the stable UI.
9. Produce final selling-page copy and screenshots from the release candidate.
10. Merge the long-running Codex branch at a controlled integration checkpoint, then cut a focused release-candidate branch.

## 10. Explicitly post-launch

The following remain intentionally outside version 1.0 unless a launch-critical dependency emerges:

- Playing With Viewers.
- Raffles and giveaways.
- Additional command-driven stream games or audience systems.
- New music providers.
- General feature expansion beyond the audited launch set.
