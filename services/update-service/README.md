# RearSilver update service — Phases 1–2

This Worker exposes only the Owner Build manifest used to prove update detection. It does not serve installers and it is not part of the active Private Beta update path.

The Worker has a private R2 binding named `RELEASES` connected to the `rearsilver-releases` bucket. Public bucket access must remain disabled.

The desktop client requests:

```text
GET /v1/updates/owner-build/windows-x64
```

Set `OWNER_BUILD_MANIFEST` to a complete schema-1 JSON manifest before deploying. During local development, place it in `.dev.vars`; that file remains untracked. For a deployed test Worker, set the binding without committing its value:

```powershell
npx wrangler secret put OWNER_BUILD_MANIFEST
```

Use the contract documented in `UPDATE_SYSTEM_PLAN.md`, with `channel` set to `owner-build` and `platform` set to `windows-x64`. An equal version exercises the up-to-date path. A newer Semantic Versioning value such as `1.0.0-owner.1` exercises update availability without enabling downloads.

`owner-manifest.example.json` is an inert starting point. For local Worker testing, copy its JSON onto one line as the value of `OWNER_BUILD_MANIFEST` in `.dev.vars`.

Do not add Private Beta, Free or Pro routes until their authentication and entitlement rules are implemented. Deployment remains a separate, deliberate operation.

## Phase 2 Owner download-route test

Upload a small plain-text object to the private R2 bucket using this exact object key:

```text
tests/owner-build/download-route.txt
```

Set a strong Worker secret named `OWNER_DOWNLOAD_TOKEN`. The test route is:

```text
GET /v1/download/owner-build/test
Authorization: Bearer <OWNER_DOWNLOAD_TOKEN>
```

The route returns only that fixed test object. It does not accept an object key from the request and cannot expose another R2 object. Missing or incorrect credentials return HTTP 401. A missing test object returns HTTP 404.

After the fixed-object test passes, the Owner release route is:

```text
GET /v1/download/owner-build/<manifest-version>/windows-x64
Authorization: Bearer <OWNER_DOWNLOAD_TOKEN>
```

The requested version must exactly match the current Owner manifest. The Worker derives the immutable R2 key from the validated manifest and only accepts the expected Owner installer filename format:

```text
releases/owner-build/<version>/windows-x64/<manifest installer filename>
```

The caller cannot supply a bucket key or filename. Old manifests therefore stop authorising new downloads as soon as the manifest is advanced, while already installed files remain private in R2.
