# Avatar Suite companion integration contract

This document defines the optional launch contract between RearSilver Stream Suite and RearSilver Avatar Suite. The products remain independently installable and independently owned processes.

## Capability discovery

The 64-bit RearSilver Stream Suite installer registration is:

`HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\RearSilver Stream Suite`

Stream Suite installers that support the named Avatar companion write:

- Value: `AvatarCompanionSchema`
- Type: `REG_DWORD`
- Data: `1`

Avatar Suite must require a registered Stream Suite installation and `AvatarCompanionSchema >= 1` before enabling its companion-launch preference. Normal Stream Suite uninstall removes the complete product registration, including this marker.

## Avatar installation discovery

Avatar Suite's future 64-bit installer registration is authoritative:

`HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\RearSilver Avatar Suite`

It provides `InstallLocation` as an absolute installation directory. Stream Suite forms the executable path as:

`<InstallLocation>\RearSilver Avatar Suite.exe`

Stream Suite does not search Program Files, PATH, development checkouts, build folders, the desktop, or sibling repositories. A missing registration, non-absolute location, or missing executable is treated as unavailable without preventing the Control Hub from running.

## Launch preference

Avatar Suite owns the user preference in:

`%LOCALAPPDATA%\RearSilver Avatar\settings.ini`

```ini
[Avatar]
OpenWithStreamSuite=1
```

Only the numeric value `1` enables startup launching. A missing file, section, key, or any other value is disabled. Stream Suite presents this state read-only; users change it in Avatar Suite General settings.

## Process ownership

When the Control Hub starts in a product state that permits its normal optional-app startup, Stream Suite may launch the registered Avatar executable if the preference is enabled. It compares the registered executable's normalized full path with running process image paths before launching.

Avatar Suite is not stored in `tool.programs`, is not part of App Manager, and is never included in `closeManagedPrograms()` or `closeManagedProgramsAndWait()`. Stream Suite launches it with `CREATE_BREAKAWAY_FROM_JOB`. If Windows refuses that independent launch, Stream Suite records the Win32 error and does not retry with a dependent launch mode.

The Suite Settings manual **Open Avatar Suite** action uses the same registered-path validation, exact-process detection, and independent launch function. Manual opening does not require the automatic-launch preference to be enabled.

Avatar Suite remains responsible for its own definitive single-instance guard because process discovery and process creation cannot form an atomic operation across two applications.

## Diagnostics and privacy

Stream Suite records whether the preference is disabled, registration is absent, the registered installation is invalid, the registered executable is already running, launch succeeds, or launch fails. Normal diagnostics expose only detected, preference-enabled, and installation-valid states. The registered path is not included in the Suite Settings UI or exported text report.