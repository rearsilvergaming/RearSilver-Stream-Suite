# RearSilver Stream Suite — Dock Functionality Audit

This audit intentionally excludes the Media Player, music system, and account/authentication code.

## Completed hardening batch

### Scenes & Sources

- Added clear connecting, failed, embedded, and detached states.
- Added **Retry connection**, **Return panels to OBS**, and **Bring panels into Suite** actions.
- Made both native OBS panels expand with the available width.
- Preserved the selected tab while moving panels.
- Protected native widgets from accidental deletion and cleaned Suite-owned placeholders.

### Browser Refresh

- Replaced the visibility-toggle workaround with OBS browser-source reload procedures.
- Preserved source visibility and avoided retaining scene-item pointers across delayed callbacks.
- Added recursive discovery inside nested groups, duplicate prevention, and per-source feedback.

### Quick Text

- Persisted presets and styling.
- Added safer active-scene and source validation.
- Replaced fragile global “last source” tracking with stable, per-scene identifiers.
- Made group clearing safe and scoped to the current scene.

### Auto-Start Manager

- Removed the behaviour that launched programs simply because the feature registered after OBS opened.
- Auto-start now runs only from OBS's actual finished-loading event.
- Suite-launched programs now inherit their executable directory as the working directory.

### Timer

- Persisted timer label, mode, duration, colours, font sizes, shadow, background, and completion behaviour.
- Restored the selected mode across sessions.
- Added an explicit **Hide timer source when countdown finishes** option.
- Existing timer sources are repaired and added to the current scene when missing.
- Added actionable status feedback and clearer source-management wording.

### Instant Replay

- Replay display is now tied to the scene in which it was shown, so delayed auto-hide cannot alter another scene.
- Removed accidental empty replay-group creation during auto-hide.
- Suite playback now responds only to replay saves requested by the Suite, rather than every native OBS replay-buffer save.
- Correctly records a replay-buffer start initiated by the Suite and expires stale pending requests.

## Remaining commercial-product work

### Scenes & Sources

- Test scene-collection changes and native dock destruction/recreation.
- Consider lightweight search and source counts without duplicating OBS's own editing controls.

### Auto-Start Manager

- Distinguish Suite-launched processes from programs already running.
- Add per-program running/error state and an explicit **Launch now** action.
- Repair remaining legacy character-encoding issues.

### Timer

- Test source deletion/rename recovery, scene-collection changes, and OBS crash/restart semantics.
- Consider reusable saved timer presets and optional completion actions after the reliability pass.

### Instant Replay

- Complete the unfinished visual controls: frame colour, border, padding, and label styling.
- Add clearer replay-folder validation and per-step status feedback.
- Test source rename/delete recovery, replay discovery, scene-collection changes, and shutdown while replay timers are active.

## Cross-cutting requirements

- Show **Ready**, **Working**, **Succeeded**, or an actionable **Failed** state.
- Persist settings unless clearly described as session-only.
- Never assume a current scene, source type, dock, or external process exists.
- Never retain unsafe raw OBS pointers in delayed callbacks.
- Survive scene-collection changes and deletion or renaming of Suite-created sources.
- Make labels accurately describe the operation performed.
- Remove mojibake and preserve accessibility across themes.
