# RearSilver Stream Suite update system

## Decision

Use the Shopify download as a bootstrap installer. The Control Hub checks for updates after launch and offers the newest complete installer for its compiled release channel. Updates do not need to be installed in sequence.

Host immutable, versioned installers in a private Cloudflare R2 bucket. Put a small Cloudflare Worker in front of the bucket for update checks, entitlement decisions and short-lived download authorisation. This fits the existing Cloudflare Worker deployment used by the YouTube resolver while keeping commercial installers out of the desktop executable and public Shopify Files URLs.

## User experience

- Start the update check asynchronously after the Hub has painted and its normal startup work has settled. It must never delay the first usable screen.
- Check once shortly after each Hub launch and provide **Check for updates** in Suite Settings and the native Help menu.
- Stay quiet when offline during an automatic check. A manual check may report that the service could not be reached.
- Show the available version, download size, release notes and whether the update is recommended or required.
- Download in the background with WinHTTP byte-range resume, visible progress and a cancel control. A network interruption should resume rather than restart the full download.
- After verification, check only whether OBS is running. If it is, warn that OBS and any active outputs will close, then request a normal OBS close after the user confirms. Do not treat an active replay buffer as a reason to block installation or attempt to duplicate OBS's output-state logic.
- Launch the existing complete NSIS installer through Windows elevation, then relaunch OBS when installation succeeds. The OBS plugin starts the updated Hub through its normal startup path, avoiding a duplicate Hub process.

An old Shopify bootstrap installation follows the same path on first launch. Shopify only needs a replacement bootstrap when the updater protocol, supported Windows prerequisites or signing trust can no longer bridge directly to the latest release.

## Components

### Control Hub update client

Add an update service outside `main.cpp`, with UI integration kept in Suite Settings and a small launch notification. Responsibilities:

1. Read the compiled channel and version from `RsBeta::kChannel` and `RsBeta::kVersion`.
2. Request the channel manifest over HTTPS with WinHTTP.
3. Parse and validate a versioned manifest schema.
4. Compare stable and prerelease versions correctly. String comparison is not sufficient for values such as `1.0.0-beta.10`.
5. Reject channel changes, downgrades and unsupported manifest schemas.
6. Download the installer with WinHTTP on a background thread, retaining a bounded `.partial` file and using an authenticated byte-range request to resume an interruption.
7. Hash the completed temporary file with Windows CNG/BCrypt.
8. Verify its Authenticode signature with `WinVerifyTrust` and confirm the expected RearSilver publisher identity when code signing is available.
9. Hand the verified installer to the updater helper.

Store update state under `%LOCALAPPDATA%\RearSilver Stream Suite\Updates`. Keep partial downloads separate from verified installers and remove abandoned files after a bounded retention period.

### Updater helper

Bundle a small `RearSilver-Stream-Suite-Updater.exe` beside the Hub. It must be simpler than the Hub and must not load CEF or WebView2.

The helper receives only a local verified installer path, expected version, Hub process ID, OBS process ID and relaunch preference. After the user confirms the Hub's update warning, it requests the same graceful window close as pressing OBS's **X**. OBS remains responsible for warning about and stopping an active stream, recording or replay buffer. If OBS presents its own confirmation, the helper waits while the user responds. It does not infer output safety from OBS recording flags and does not force-terminate OBS. Once OBS and the plugin-driven Hub have exited, it launches the installer with `ShellExecuteEx(..., "runas", ...)`, waits for its exit code and relaunches OBS after success. OBS loads the updated plugin, which starts the updated Hub normally. If the graceful close is cancelled or does not complete within a reasonable timeout, the helper offers Retry and Cancel.

The complete NSIS installer remains the sole component that writes application and OBS plugin files. This avoids maintaining a second patching format and allows any supported installed version to jump directly to the current release.

### Update API and storage

Recommended layout:

```text
Cloudflare Worker: updates.rearsilver.example
Private R2 bucket: rearsilver-releases

releases/
  private-beta/1.0.0-beta.2/windows-x64/RearSilver-Stream-Suite-Private-Beta-1.0.0-beta.2-Setup.exe
  free/1.0.0/windows-x64/RearSilver-Stream-Suite-Free-1.0.0-Setup.exe
  pro/1.0.0/windows-x64/RearSilver-Stream-Suite-Pro-1.0.0-Setup.exe
manifests/
  private-beta/windows-x64.json
  free/windows-x64.json
  pro/windows-x64.json
```

Versioned objects are immutable. Upload and verify the installer first, then publish the manifest last. A client can therefore never discover a release whose installer is still being uploaded.

The Worker should return or redirect to a short-lived R2 download URL after authorisation. It should not proxy the full installer through Worker code. R2 presigned URLs act as bearer tokens, so keep their lifetime short and never write them to diagnostics.

## Manifest

Example response:

```json
{
  "schema": 1,
  "product": "rearsilver-stream-suite",
  "channel": "private-beta",
  "platform": "windows-x64",
  "version": "1.0.0-beta.2",
  "minimum_supported_version": "1.0.0-beta.1",
  "minimum_updater_schema": 1,
  "mandatory": false,
  "published_at": "2026-09-12T18:00:00Z",
  "installer": {
    "filename": "RearSilver-Stream-Suite-Private-Beta-1.0.0-beta.2-Setup.exe",
    "size": 380292474,
    "sha256": "lowercase-hex-digest",
    "download_request_url": "https://updates.rearsilver.example/v1/download/private-beta/1.0.0-beta.2/windows-x64"
  },
  "release_notes_url": "https://imperialinfusions.com/pages/rearsilver-release-notes"
}
```

Do not place a permanent commercial download URL or service secret in the manifest. Consider signing the canonical manifest as defence beyond HTTPS. Installer verification remains mandatory even when the manifest is signed.

## Channels and entitlement

- **Free:** public manifest and public or rate-limited download are acceptable.
- **Private Beta:** retain Shopify/Discord distribution initially. Add authenticated in-Hub downloads only after a tester identity mechanism exists; do not embed the page password or a shared permanent bearer token in the executable.
- **Pro:** require an authenticated account with an active entitlement before the Worker issues a short-lived installer URL.
- **Owner Build:** use a separate non-public channel and owner credential. Never ship access to this channel in customer builds.

For Pro, the Hub should open the system browser for account authentication. The backend maps that account to a Shopify purchase or licence record and returns revocable tokens. Store refresh credentials in Windows Credential Manager, following the existing treatment of provider credentials. A Shopify paid/refunded-order webhook can update the entitlement database later; its exact licence, refund, device and offline-grace rules belong to the commercial licensing design after beta evidence is reviewed.

No static secret inside the Hub can protect paid downloads because a user can extract it. Entitlement must be decided by the server.

## Security and failure handling

- Require HTTPS and accept only the configured update host.
- Enforce sensible response, manifest and filename size limits.
- Download to a `.partial` file and rename only after size, SHA-256 and signature checks succeed.
- Never execute a path or URL supplied outside the validated manifest flow.
- Verify the expected signed publisher in addition to asking Windows whether the signature chain is valid.
- Do not log access tokens, presigned URLs or query strings.
- If verification fails, delete the file, retain the installed version and show a clear error.
- If installation fails, preserve the downloaded verified installer so the user can retry.
- Keep the last known good release available in R2. Publish a forward hotfix rather than silently directing installed clients to a lower version.
- Support a server-side emergency switch that disables a bad release manifest without disabling the installed Suite.

Update checks disclose the minimum operational data: product, channel, version, updater schema and platform. Treat any account or entitlement request as personal data and document it in the privacy page before commercial release.

## Release tooling

Extend the release scripts only after the client design is proven. A publishing command should:

1. Build into the existing versioned artifact folder.
2. Produce the versioned installer filename.
3. Calculate size and SHA-256.
4. Verify Authenticode once signing exists.
5. Generate the manifest from `CMakePresets.json` and the built file rather than accepting duplicated version input.
6. Upload the immutable installer object.
7. Read it back or validate its metadata.
8. Publish the channel manifest last.

Keep publication separate from compilation and installer creation so a local build can never become a live update accidentally.

## Delivery phases

### Phase 1: update detection in Owner Build

- Add semantic/prerelease version comparison and manifest parsing tests.
- Add asynchronous automatic and manual checks.
- Display update availability and release notes without downloading anything.
- Exercise malformed manifests, offline startup and older/newer/equal versions.

Implementation status as of 10 September 2026: Phase 1 is runtime-validated in the Owner build. The source contains the channel-isolated HTTPS client, strict schema-1 manifest validation, Semantic Versioning comparison, one quiet automatic check shortly after each launch, manual checks from Suite Settings and the native Help menu, and a clickable sidebar availability notice. The isolated Owner Worker is deployed. Live tests passed for service errors, equal/up-to-date versions, recommended updates, required updates, the automatic sidebar notification and navigation from that notification to the Software Updates card. No download or installation behavior is enabled yet.

### Phase 2: Private Beta updater

- Add WinHTTP ranged download, progress, cancellation, SHA-256 verification and the updater helper.
- Use a test update channel and complete installers.
- Test fresh install, upgrade, interrupted download, insufficient disk space, OBS running, Hub running, cancelled elevation and failed installer cases on the external laptop and main PC.
- Keep Shopify update email delivery as recovery while beta evidence is gathered.

### Phase 3: signed production updates

- Obtain a Windows code-signing certificate and sign the Hub, plugin, updater and installer.
- Enforce Authenticode publisher verification.
- Add a signed manifest and key-rotation policy.
- Publish privacy wording and support recovery instructions.

### Phase 4: commercial entitlement

- Implement browser-based RearSilver account authentication.
- Synchronise Shopify purchase/refund state into an entitlement service.
- Issue short-lived Pro download URLs and revocable credentials.
- Define device, offline grace, refund, transfer and account-recovery policies before advertising licensing behaviour.

## External references

- Cloudflare R2 presigned URLs: <https://developers.cloudflare.com/r2/api/s3/presigned-urls/>
- Cloudflare Workers R2 bindings: <https://developers.cloudflare.com/r2/api/workers/workers-api-usage/>
- Microsoft WinHTTP: <https://learn.microsoft.com/windows/win32/winhttp/about-winhttp>
- Microsoft `WinVerifyTrust`: <https://learn.microsoft.com/windows/win32/api/wintrust/nf-wintrust-winverifytrust>
- Microsoft CNG hashing: <https://learn.microsoft.com/windows/win32/secauthn/creating-a-hash-with-cng>
- Microsoft Credential Manager: <https://learn.microsoft.com/windows/win32/api/wincred/>
- Shopify webhooks: <https://shopify.dev/docs/apps/build/webhooks>
