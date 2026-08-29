const API = "https://www.googleapis.com/youtube/v3";
const json = (body, status = 200, origin = "*") => new Response(JSON.stringify(body), { status, headers: {
  "content-type": "application/json; charset=utf-8", "access-control-allow-origin": origin,
  "cache-control": status === 200 ? "public, max-age=300" : "no-store" } });
const playlistId = value => { try { return new URL(value).searchParams.get("list") || ""; }
  catch { return /^[A-Za-z0-9_-]{10,}$/.test(value) ? value : ""; } };
const videoId = value => { try { const url = new URL(value); const host = url.hostname.toLowerCase().replace(/^www\./, "");
    if (host === "youtu.be") return url.pathname.split("/").filter(Boolean)[0] || "";
    if (host !== "youtube.com" && host !== "m.youtube.com" && host !== "music.youtube.com") return "";
    if (url.pathname === "/watch") return url.searchParams.get("v") || "";
    const parts = url.pathname.split("/").filter(Boolean); return ["shorts", "embed", "live"].includes(parts[0]) ? parts[1] || "" : "";
  } catch { return ""; } };
const youtubeUrl = value => { try { const host = new URL(value).hostname.toLowerCase().replace(/^www\./, "");
    return host === "youtu.be" || host === "youtube.com" || host === "m.youtube.com" || host === "music.youtube.com"; } catch { return false; } };
const durationSeconds = value => { const m = /^PT(?:(\d+)H)?(?:(\d+)M)?(?:(\d+)S)?$/.exec(value || "");
  return m ? Number(m[1] || 0) * 3600 + Number(m[2] || 0) * 60 + Number(m[3] || 0) : 0; };
async function youtube(path, parameters, key) {
  const url = new URL(`${API}/${path}`); Object.entries({ ...parameters, key }).forEach(([n, v]) => url.searchParams.set(n, v));
  const response = await fetch(url); const body = await response.json();
  if (!response.ok) throw new Error(body?.error?.message || `YouTube returned ${response.status}`); return body;
}
const thumbnail = s => s?.thumbnails?.maxres?.url || s?.thumbnails?.high?.url || s?.thumbnails?.medium?.url || s?.thumbnails?.default?.url || "";
async function details(ids, key) {
  const output = new Map();
  for (let offset = 0; offset < ids.length; offset += 50) {
    const body = await youtube("videos", { part: "snippet,contentDetails,status", id: ids.slice(offset, offset + 50).join(",") }, key);
    for (const item of body.items || []) output.set(item.id, { videoId: item.id, title: item.snippet?.title || "YouTube video",
      artist: item.snippet?.channelTitle || "YouTube", thumbnail: thumbnail(item.snippet),
      durationSeconds: durationSeconds(item.contentDetails?.duration), categoryId: item.snippet?.categoryId || "",
      liveStatus: item.snippet?.liveBroadcastContent || "none", ageRestricted: item.contentDetails?.contentRating?.ytRating === "ytAgeRestricted",
      allowedRegions: item.contentDetails?.regionRestriction?.allowed || [], blockedRegions: item.contentDetails?.regionRestriction?.blocked || [],
      embeddable: item.status?.embeddable !== false, privacyStatus: item.status?.privacyStatus || "",
      uploadStatus: item.status?.uploadStatus || "" });
  }
  return output;
}
const publicTrack = item => { const { categoryId, liveStatus, ageRestricted, allowedRegions, blockedRegions, embeddable, privacyStatus, uploadStatus, ...track } = item; return track; };
function rejection(item, options = {}) {
  if (!item) return "That YouTube video is unavailable.";
  if (item.privacyStatus !== "public" || item.uploadStatus !== "processed") return "That YouTube video is not publicly available.";
  if (!item.embeddable) return "That YouTube video does not allow embedded playback.";
  if (item.liveStatus !== "none") return "Live and upcoming YouTube broadcasts cannot be requested.";
  if (!item.durationSeconds) return "That YouTube video does not report a playable duration.";
  if (options.region && ((item.allowedRegions.length && !item.allowedRegions.includes(options.region)) || item.blockedRegions.includes(options.region)))
    return "That YouTube video is unavailable in the streamer's region.";
  if (options.rejectAgeRestricted && item.ageRestricted) return "Age-restricted YouTube videos are disabled by the streamer.";
  if (options.musicOnly && item.categoryId !== "10") return "That video is not listed in YouTube's Music category.";
  return "";
}
async function search(query, key, options) {
  const directId = videoId(query);
  if (youtubeUrl(query) && !directId) throw new Error("That is not a supported YouTube video URL.");
  if (directId) {
    const item = (await details([directId], key)).get(directId); const error = rejection(item, options);
    if (error) throw new Error(error); return publicTrack(item);
  }
  const parameters = { part: "snippet", type: "video", maxResults: "10", q: query,
    videoEmbeddable: "true", videoSyndicated: "true", safeSearch: options.safeSearch };
  if (options.region) parameters.regionCode = options.region;
  if (options.musicOnly) parameters.videoCategoryId = "10";
  const found = await youtube("search", parameters, key);
  const ids = (found.items || []).map(item => item.id?.videoId).filter(Boolean); const videos = await details(ids, key);
  const track = ids.map(id => videos.get(id)).find(item => !rejection(item, options));
  if (!track) throw new Error("No YouTube result matched the streamer's safety settings."); return publicTrack(track);
}
async function playlist(value, key, region) {
  const id = playlistId(value); if (!id) throw new Error("That is not a valid YouTube playlist URL or ID.");
  const info = await youtube("playlists", { part: "snippet", id }, key); const title = info.items?.[0]?.snippet?.title || "YouTube fallback";
  const ids = []; let pageToken = "";
  do { const page = await youtube("playlistItems", { part: "contentDetails", playlistId: id, maxResults: "50", pageToken }, key);
    ids.push(...(page.items || []).map(item => item.contentDetails?.videoId).filter(Boolean)); pageToken = page.nextPageToken || "";
  } while (pageToken && ids.length < 500);
  const videos = await details(ids, key); const tracks = ids.map(videoId => videos.get(videoId)).filter(item => !rejection(item, { region }))
    .map(publicTrack); return { title, tracks };
}
export default { async fetch(request, env) {
  const origin = env.ALLOWED_ORIGIN || "*";
  if (request.method === "OPTIONS") return new Response(null, { headers: { "access-control-allow-origin": origin,
    "access-control-allow-methods": "GET,OPTIONS", "access-control-allow-headers": "content-type" } });
  if (request.method !== "GET") return json({ error: "Method not allowed" }, 405, origin);
  if (!env.YOUTUBE_API_KEY) return json({ error: "Resolver is not configured." }, 503, origin);
  const url = new URL(request.url);
  try {
    if (url.pathname.endsWith("/v1/youtube/search")) { const q = (url.searchParams.get("q") || "").trim();
      if (!q) return json({ error: "Search text is required." }, 400, origin);
      const options = { safeSearch: url.searchParams.get("safeSearch") === "moderate" ? "moderate" : "strict",
        region: /^[A-Z]{2}$/.test(request.cf?.country || "") ? request.cf.country : "",
        musicOnly: url.searchParams.get("musicOnly") === "true", rejectAgeRestricted: url.searchParams.get("rejectAgeRestricted") !== "false" };
      return json({ track: await search(q, env.YOUTUBE_API_KEY, options) }, 200, origin); }
    if (url.pathname.endsWith("/v1/youtube/playlist")) { const region = /^[A-Z]{2}$/.test(request.cf?.country || "") ? request.cf.country : "";
      return json(await playlist((url.searchParams.get("url") || "").trim(), env.YOUTUBE_API_KEY, region), 200, origin); }
    return json({ error: "Not found" }, 404, origin);
  } catch (error) { return json({ error: error instanceof Error ? error.message : "YouTube request failed." }, 502, origin); }
} };
