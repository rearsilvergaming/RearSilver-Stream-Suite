# RearSilver Hub image library

Captured 7 September 2026 for the product page, wiki, Getting Started guide, and beta Discord welcome. Open `index.html` to browse; `publication-candidates.zip` contains the 31 PNG exports and this index.

## What each folder contains

- `native/`: eight real captures from the installed **1.0.0-owner** Control Hub. Cropped to the content panel to exclude window chrome, the Owner badge, and the saved music/player strip. These are not beta-build screenshots. Some panels continue below the captured area; use the complete HTML versions for detailed documentation.
- `rendered/` images 01–21: the current Hub's original HTML and CSS rendered in a browser with a documentation-only native bridge replacement. Demo settings and counters are injected through the pages' existing configuration functions. The footer explicitly identifies demo state. No live OBS or account connections are claimed. The command catalogue and default permissions are read from the current source catalogue.
- `rendered/` images 22–23: **layout illustrations**, adapted from the Now Playing and Queue drawing code in `main.cpp`. These are HTML reconstructions of native layouts, not screenshots or pixel-perfect native renders. Fictional track names, artist, durations, and a simple original music-note graphic are used. Do not describe these as running-app captures.
- `native-originals/`: internal reference captures, **not for publication**. They still contain the previously selected music title in the bottom strip. They are excluded from the ZIP and gallery.
- `preview/`: local, disconnected interface fixtures used to produce the HTML exports. Buttons do not control the real Hub or OBS. Do not ship these as application code.

## Coverage

Native: App Manager, Browser Refresh, Instant Replay, Quick Text, Timer, Commands, Music & Requests, connected Twitch accounts.

Rendered: Library; all five Suite Settings tabs; all five Guided Setup steps; command quick reference; all five Stream Tools panels; both Music Overlay tabs; Feedback & Diagnostics empty and example form; Now Playing and Queue layout illustrations.

OBS screenshots are intentionally left to Ben. Native file-pickers, authentication dialogs, live provider pages, and actual report contents are not included. The set covers the main pages and sections, not every possible popup or state.

## Privacy and demo choices

- The native account image shows the approved public names RearSilver and FrontSilver with their real Connected indicators.
- The rendered accounts show DemoStreamer and DemoStreamBot; these are simulated examples, not actual authenticated accounts.
- Spotify is left unconfigured in the demo. No Client ID, secret, token, email, authentication code, or user-specific path is supplied.
- No commercial album covers, YouTube thumbnails, video frames, or third-party music recordings are used in publication exports.
- Library counters are illustrative; no demo music was imported into Ben's real library.
- OBS remains disconnected in the renders. Disabled controls and unavailable diagnostics accurately represent this fixture state. No OBS scene/source action was performed.
- Opening the installed Hub resumed previously selected music; playback was stopped. Navigation changed the currently selected page/tab. No library, account, or tool configuration was intentionally changed.

## Suggested use

- Getting Started: images 07–11 (Guided Setup).
- Account help: native 08 for real connection status; rendered 04 for the complete account settings.
- Wiki: rendered settings and tool pages; full-page captures retain the detailed controls.
- Discord welcome: native 08, rendered 07 and 20, as useful supporting images.
- Product page: native feature crops and selected demo views. Keep the demo labels. Treat player illustrations as illustrations.

## Source and reproducibility

Source checkout: `C:\Users\Ben\Desktop\Streaming\Streamer Tools\obs-streamer-tools\obs-streamer-tools\RearSilver-Stream-Suite`.

Original HTML/CSS comes from `src/music_player`; the Sora font comes from `assets/fonts`. `prepare.py` copies these into the isolated preview, changes the local font URL, replaces native communication with a no-op, and supplies `demo.js`. The Library preview explicitly binds two element IDs that conflict with browser globals; this change exists only in the fixture.

`package.py` checks image integrity, creates the manifest, gallery and review sheet, and excludes internal originals from the ZIP. The application source and installed executable were not edited or rebuilt. Nothing has been published to Shopify or Discord.
