# RearSilver Stream Suite — Private Beta Release Plan

Status: approved beta model  
Locked in: 31 August 2026  
Applies before: final Free/Pro design and public launch

## 1. Purpose

The private beta will test the complete, fully unlocked RearSilver Stream Suite with a small number of trusted streamers before the final Free/Pro product and licensing system is designed.

The Hub and OBS dock are already used during the developer's own real streams and are known to be in a solid working state within that familiar environment. The beta is intended to reveal what an unfamiliar user experiences:

- Can they install and start the Suite without developer files or manual copying?
- Can they understand the relationship between the Control Hub and OBS dock?
- Can they find features without being told where they live?
- Do labels, disabled states and errors explain what will happen and how to recover?
- Does the Suite work across different OBS layouts, scenes, sources, accounts and Windows systems?
- Can useful diagnostics and reproduction details be collected when something fails?

The beta is not a commercial trial, Free edition or final licensing prototype.

## 2. Approved beta model

### Stage A — internal alpha

Testers:

- Developer.
- Optionally one or two highly trusted technical testers.

Purpose:

- Validate the renamed Control Hub.
- Validate the complete installer on a clean Windows environment.
- Catch crashes, missing runtime files and destructive upgrade/uninstall behaviour.
- Verify expiry and diagnostic export before broader distribution.

Exit gate:

- No known installation, startup, data-loss or credential-exposure blocker.
- Complete runtime installs without manual file copying.
- Upgrade preserves settings and credentials.
- Uninstall follows the approved preservation/removal choices.
- Feedback report exports successfully and contains no secrets.

### Stage B — trusted streamer private beta

Testers:

- A small invited group of trusted streamers.
- No public download link.
- No licence keys or account activation.

Purpose:

- Test real streams and unfamiliar-user workflows.
- Identify navigation and terminology problems.
- Exercise different OBS and Windows environments.
- Collect structured usability reports, faults and reproduction steps.

Exit gate:

- No unresolved P0 beta defect.
- Common tester tasks are discoverable without direct developer instruction.
- Repeated usability failures are converted into product changes or documented onboarding.
- Known limitations are written clearly enough for public launch planning.

## 3. Beta identity

Every beta build must identify itself consistently as pre-release software.

Required identity:

- Product: `RearSilver Stream Suite`.
- Companion application: `RearSilver Stream Suite | Control Hub`.
- Channel: `Private Beta`.
- Version format: `1.0.0-beta.<number>` until a different versioning policy is approved.
- Display the version, build identifier, build date and expiry date.

The Private Beta label must appear in:

- Control Hub title/about or account area.
- OBS dock.
- Feedback & Diagnostics page.
- Installer.
- Exported diagnostic report.
- Executable and DLL version metadata.

## 4. Required change 1 — rename Music Player to Control Hub

The companion is no longer accurately described as a music player. Its canonical executable must become:

`RearSilver-Stream-Suite-Control-Hub.exe`

Required rename audit:

- CMake target and output name.
- Build and packaging dependencies.
- OBS plugin launch path.
- Process detection and lifecycle/job management.
- Hub single-instance handling.
- Window title, application metadata, icon resources and diagnostics.
- Installer copy, upgrade and uninstall paths.
- OBS Application Audio Capture instructions and application matching.
- Media-session identity and media-key ownership where applicable.
- Firewall, crash-log and support references.
- Existing settings or paths containing the old executable name.
- README, beta guide, wiki and product copy.

Migration requirement:

- An upgrade must not leave both old and new executables active or selectable as competing music-capture targets.
- The installer must safely remove the obsolete installed executable after confirming the new Hub is installed.
- User settings, credentials, libraries and managed OBS sources must remain intact.

Acceptance checks:

- OBS launches the renamed Hub when configured.
- The Hub launches independently.
- Only one intended Hub instance runs.
- Hub/OBS IPC reconnects after either side restarts.
- YouTube/local audio capture guidance identifies the renamed Hub.
- Keyboard media controls retain the approved provider behaviour.

## 5. Required change 2 — Private Beta expiry

The private beta uses a simple embedded expiry date. It does not require licence keys, online activation or sophisticated anti-tamper logic.

Required behaviour:

- Expiry date supplied at build/configuration time rather than scattered through source files.
- Exact expiry date visible before installation and inside the Hub.
- Fully unlocked feature set before expiry.
- Warnings at 14, 7, 3 and 1 day remaining.
- Clear expired state after the deadline.
- A replacement beta can be installed over the expired beta without losing configuration.

After expiry, the Suite must still allow:

- Opening the Control Hub.
- Viewing Feedback & Diagnostics.
- Previewing, copying and exporting a report.
- Viewing version and expiry information.
- Backing up or locating settings where supported.
- Uninstalling or installing a newer build.

After expiry, the Suite must not:

- Delete or reset settings.
- Delete credentials.
- Delete local libraries or playlists.
- Delete or modify managed OBS scenes and sources merely because the build expired.
- Pretend that an operation succeeded when it was blocked by expiry.

The dock and Hub must show a clear explanation rather than leaving controls silently disabled.

The beta expiry system is deliberately separate from the eventual Free/Pro entitlement system.

## 6. Required change 3 — Feedback & Diagnostics page

Add a first-class Control Hub navigation page named:

`Feedback & Diagnostics`

### 6.1 Tester feedback fields

Required structured fields:

- Summary.
- What were you trying to do?
- Where did you first look?
- Did you find it without help?
- What did you expect to happen?
- What actually happened?
- Can you reproduce it?
- Reproduction steps.
- What wording or navigation did you expect?
- Did you abandon a setup step or require help?
- Additional feedback.

Useful categorisation:

- Bug or crash.
- Installation or update.
- Navigation or discoverability.
- Confusing wording or state.
- OBS dock.
- Stream Tools.
- Music or requests.
- Twitch or Spotify.
- Performance.
- Accessibility.
- Suggestion.

Feedback remains local until the tester explicitly copies or exports it. The beta must not silently upload reports.

### 6.2 Automatically collected diagnostic summary

Include where available:

- Product version, beta number and build identifier.
- Build date and expiry date.
- Report creation time and local timezone.
- Windows version and architecture.
- OBS Studio version.
- Installed plugin and Control Hub version match/mismatch.
- Control Hub and OBS connection state.
- Hub process/IPC state.
- Active music provider.
- YouTube/local/Spotify availability state.
- Twitch streamer and optional bot connection states.
- Spotify configured/authorised/playback-available state.
- Song-request enabled state and relevant provider availability.
- Managed overlay placement mode.
- Managed Quick Text, Timer, Instant Replay and Music Overlay status.
- Relevant source conflicts or repair errors.
- Recent warnings and errors.
- Diagnostic log file locations.

Do not claim a connection is healthy merely because credentials exist. Distinguish configured, authorised, connected, available and actively playing states.

### 6.3 Privacy and redaction

Reports must never contain:

- Twitch or Spotify access tokens.
- Refresh tokens.
- OAuth device codes or authorisation codes.
- Spotify Client ID.
- Embedded secrets or credentials.
- Raw Windows Credential Manager data.

Redact by default:

- Windows username inside paths.
- Twitch numeric account IDs unless specifically required.
- Local music filenames, folders and metadata.
- Optional application paths.
- Chat message contents.
- Stream keys, server URLs containing credentials or other OBS secrets.

If potentially personal material is useful, it must be separately selectable and clearly previewed before export.

### 6.4 Report actions

Required actions:

- `Refresh diagnostics`.
- `Preview report`.
- `Copy report`.
- `Export report…`.
- `Open logs folder`.

Recommended export name:

`RearSilver-Stream-Suite-Beta-Feedback-<version>-<date-time>.txt`

The exported text must be human-readable and divided into:

1. Beta/build information.
2. Tester feedback.
3. System and OBS information.
4. Connection and feature states.
5. Recent warnings/errors.
6. Included log excerpts.
7. Redaction notice.

Acceptance checks:

- Export works while connected, disconnected and expired.
- Export works without Twitch or Spotify accounts.
- Preview exactly matches the exported content.
- Secrets planted in test logs/settings are redacted.
- Missing or inaccessible logs do not break report creation.
- Report creation cannot freeze the Hub on large logs.

## 7. Required change 4 — complete beta installer and packaging

The existing installer is historical and must be rebuilt around the current product.

### 7.1 Complete runtime payload

Package every required current build artifact, including:

- `RearSilver-Stream-Suite.dll`.
- `RearSilver-Stream-Suite-Control-Hub.exe`.
- Control Hub HTML surfaces.
- WebView2 loader and an approved WebView2 Runtime strategy.
- CEF binaries, resources, snapshots and locales required by the Hub.
- Private Qt TLS backend.
- Bundled fonts and font licence.
- Branding and fallback artwork used at runtime.
- OBS plugin locale data.
- Required third-party notices and licences.

The installer payload must be generated from an explicit release staging directory or install target, not a manually curated list that can drift from the CMake runtime output.

### 7.2 Installer presentation

Replace historical installer material with current:

- Logos and icons.
- Header and welcome artwork.
- Product description and Control Hub language.
- Private Beta identity, version and expiry date.
- Supported Windows/OBS requirements.
- Beta agreement and privacy/diagnostic explanation.
- Publisher and support information.

### 7.3 Installation, upgrade, repair and uninstall

Required behaviour:

- Detect the OBS installation rather than assuming an unchecked path.
- Validate supported architecture.
- Refuse or clearly warn when OBS is running before replacing binaries.
- Clean installation.
- In-place beta upgrade.
- Repair/reinstall of missing runtime files.
- Registered uninstaller.
- Removal of obsolete installed binaries such as the old Music Player executable.
- Preserve user settings and credentials by default during upgrade and uninstall.
- Offer an explicit separate choice to remove personal settings/credentials during uninstall, with clear consequences.
- Never remove unrelated OBS files, sources, scenes or plugins.
- Log installation failures clearly.

### 7.4 Clean-machine acceptance matrix

At minimum test:

- Supported Windows versions.
- Supported OBS Studio versions.
- No previous Suite installation.
- Upgrade from the immediately previous beta.
- Repair after deliberately removing one runtime file.
- Uninstall while preserving data.
- Uninstall while explicitly removing Suite data.
- Missing WebView2 Runtime.
- Non-default OBS install directory.
- OBS running during install/update.
- Standard user running the installed product after administrator installation.
- Paths containing spaces and non-ASCII characters.

Pass condition:

- No manual copying, developer tools, Qt installation, CEF checkout or build directory is required on the tester's machine.

## 8. Beta usability tasks

Give testers goals without telling them the navigation route.

Core tasks:

1. Find the Suite in OBS and identify whether the Hub is connected.
2. Enable Safety Lock and explain what it changes.
3. Create and show a BRB message, then save it as a preset.
4. Create a five-minute countdown and show it in OBS.
5. Refresh browser sources in only the current scene.
6. Configure Instant Replay and trigger it from the OBS dock.
7. Change between Simple and Advanced managed-overlay placement and explain the difference.
8. Import a YouTube fallback playlist.
9. Add a local album/folder and verify its track order.
10. Shuffle music, restore canonical order and start again from track one.
11. Enable viewer requests and change who may request songs.
12. Find and remove a request using its stable ID.
13. Configure or explain why Spotify controls are unavailable.
14. Design and show the Music Overlay.
15. Find the diagnostic page and export a report.

For each task, collect:

- Completed without help: yes/no.
- First place they looked.
- Time or perceived difficulty.
- Confusing label/state.
- Whether documentation was required.
- Whether they would have abandoned the task during a real setup.

Repeated failure by multiple testers is product evidence, not user error.

## 9. Beta issue priorities

### P0 — blocks further distribution

- Installer cannot complete or product cannot start.
- Crash, hang or OBS instability in normal use.
- Settings, credentials, playlists or OBS data lost/corrupted.
- Credentials or personal data exposed in reports/logs.
- Expiry prevents diagnostics, upgrade or uninstall.
- Upgrade leaves incompatible duplicate Hub executables.
- Core Hub/dock connection cannot recover after restart.

### P1 — fix before public launch

- Common task cannot be discovered without direct help.
- Misleading success/error state.
- Managed source cannot repair safely.
- Provider/request rules behave inconsistently.
- Major accessibility or scaling failure.
- Material performance issue during a live stream.

### P2 — polish or documented limitation

- Minor visual inconsistency.
- Copy refinement.
- Low-frequency workflow improvement.
- Feature suggestion outside the approved launch scope.

## 10. Pre-distribution checklist

### Product identity

- [ ] Music Player renamed to Control Hub everywhere.
- [ ] No stale user-facing “Music Player” or incorrect “external player” references.
- [ ] Private Beta version/build/expiry visible consistently.

### Expiry

- [ ] Build-time expiry configured.
- [ ] Warning thresholds tested.
- [ ] Expired state tested.
- [ ] Settings and credentials survive expiry and update.
- [ ] Diagnostics/export remain available after expiry.

### Feedback and diagnostics

- [ ] Feedback & Diagnostics page implemented.
- [ ] Structured usability and bug fields implemented.
- [ ] Preview, copy, export and logs-folder actions implemented.
- [ ] Redaction tests pass.
- [ ] Report contains matching Hub/plugin versions and connection states.
- [ ] No automatic upload.

### Installer/package

- [ ] Release staging directory contains the complete runtime.
- [ ] Clean install succeeds without manual dependencies.
- [ ] Upgrade succeeds and removes obsolete installed executable safely.
- [ ] Repair succeeds.
- [ ] Uninstaller registered and tested.
- [ ] Settings/credential preservation choices tested.
- [ ] Current branding, copy, beta agreement and notices included.
- [ ] Windows signing strategy decided for distributed beta binaries.

### Functional smoke test

- [ ] Hub launches independently and from OBS.
- [ ] Hub/dock reconnect after either restarts.
- [ ] Stream controls and Safety Lock operate correctly.
- [ ] Scenes/Sources and statistics load.
- [ ] Browser Refresh operates.
- [ ] Quick Text operates.
- [ ] Timer/Countdown operates including optional sound.
- [ ] Instant Replay operates.
- [ ] YouTube and local playback operate.
- [ ] Queue/history/shuffle/restore/start-from-beginning operate.
- [ ] Twitch login/chat/commands/requests operate.
- [ ] Music Overlay and text-file output operate.
- [ ] Spotify Premium path tested when an appropriate account is available.

### Distribution

- [ ] Tester list approved.
- [ ] Expiry date gives adequate testing time.
- [ ] Beta agreement supplied.
- [ ] Known issues supplied.
- [ ] Scenario tasks supplied separately from navigation instructions.
- [ ] Secure method for receiving exported reports agreed.
- [ ] Replacement/update procedure explained.

## 11. Implementation order

1. Rename the executable and all connected product references.
2. Add the beta build identity and expiry state model.
3. Add Feedback & Diagnostics with safe local export.
4. Create a canonical release staging/package target.
5. Replace the historical installer and add upgrade/repair/uninstall.
6. Resolve the beta distribution agreement and third-party notices.
7. Run internal clean-machine alpha testing.
8. Fix alpha blockers.
9. Produce the first trusted-streamer beta.
10. Review reports and convert repeated findings into Hub, dock, onboarding and wiki changes.

## 12. Relationship to Free and Pro

The private beta is fully unlocked. No beta feature placement should be interpreted as the final Free/Pro split.

Reusable work:

- Control Hub naming.
- Version/build metadata.
- Feedback & Diagnostics.
- Redaction and report export.
- Complete packaging.
- Upgrade, repair and uninstall.
- Clean-machine test matrix.

Temporary work:

- Simple embedded expiry gate.
- Private Beta labels and warning schedule.

After beta evidence is reviewed, the Free/Pro feature matrix and real entitlement/licensing architecture can be designed around observed user value rather than assumptions.
