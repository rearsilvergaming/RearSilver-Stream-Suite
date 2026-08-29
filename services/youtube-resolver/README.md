# RearSilver Stream Suite YouTube resolver

This Worker keeps the YouTube Data API key outside the distributed desktop executable.

1. Create a Google Cloud project and enable YouTube Data API v3.
2. Run `npm install`.
3. Store the key with `npx wrangler secret put YOUTUBE_API_KEY`.
4. Deploy with `npm run deploy`.
5. Set `music/youtube/resolverEndpoint` to the deployed URL plus `/v1/youtube`.

Never add the API key to `wrangler.toml`, source control, or the desktop application.

## Search safeguards

`/v1/youtube/search` accepts these optional query parameters:

- `safeSearch=strict|moderate` (defaults to `strict`)
- `musicOnly=true|false` (defaults to `false`)
- `rejectAgeRestricted=true|false` (defaults to `true`)

Text searches require embeddable and syndicated results. Direct YouTube video URLs are resolved by ID and receive the same
availability, regional, embed, live-status, duration, category and age-rating checks. The request country supplied by
Cloudflare is used only for the current availability check and is not stored. YouTube does not expose a definitive syndicated
flag through `videos.list`, so external playback of a direct URL can still be refused later by the embedded player.
