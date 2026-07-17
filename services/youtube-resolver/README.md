# RearSilver Stream Suite YouTube resolver

This Worker keeps the YouTube Data API key outside the distributed desktop executable.

1. Create a Google Cloud project and enable YouTube Data API v3.
2. Run `npm install`.
3. Store the key with `npx wrangler secret put YOUTUBE_API_KEY`.
4. Deploy with `npm run deploy`.
5. Set `music/youtube/resolverEndpoint` to the deployed URL plus `/v1/youtube`.

Never add the API key to `wrangler.toml`, source control, or the desktop application.
