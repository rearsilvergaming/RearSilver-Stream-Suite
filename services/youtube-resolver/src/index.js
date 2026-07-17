const API = "https://www.googleapis.com/youtube/v3";
const json = (body, status = 200, origin = "*") => new Response(JSON.stringify(body), { status, headers: {
  "content-type": "application/json; charset=utf-8", "access-control-allow-origin": origin,
  "cache-control": status === 200 ? "public, max-age=300" : "no-store" } });
const playlistId = value => { try { return new URL(value).searchParams.get("list") || ""; }
  catch { return /^[A-Za-z0-9_-]{10,}$/.test(value) ? value : ""; } };
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
      durationSeconds: durationSeconds(item.contentDetails?.duration), embeddable: item.status?.embeddable !== false });
  }
  return output;
}
async function search(query, key) {
  const found = await youtube("search", { part: "snippet", type: "video", maxResults: "10", q: query,
    videoEmbeddable: "true", safeSearch: "moderate" }, key);
  const ids = (found.items || []).map(item => item.id?.videoId).filter(Boolean); const videos = await details(ids, key);
  const track = ids.map(id => videos.get(id)).find(item => item?.embeddable); if (!track) throw new Error("No playable YouTube result was found.");
  delete track.embeddable; return track;
}
async function playlist(value, key) {
  const id = playlistId(value); if (!id) throw new Error("That is not a valid YouTube playlist URL or ID.");
  const info = await youtube("playlists", { part: "snippet", id }, key); const title = info.items?.[0]?.snippet?.title || "YouTube fallback";
  const ids = []; let pageToken = "";
  do { const page = await youtube("playlistItems", { part: "contentDetails", playlistId: id, maxResults: "50", pageToken }, key);
    ids.push(...(page.items || []).map(item => item.contentDetails?.videoId).filter(Boolean)); pageToken = page.nextPageToken || "";
  } while (pageToken && ids.length < 500);
  const videos = await details(ids, key); const tracks = ids.map(videoId => videos.get(videoId)).filter(item => item?.embeddable)
    .map(item => { const copy = { ...item }; delete copy.embeddable; return copy; }); return { title, tracks };
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
      if (!q) return json({ error: "Search text is required." }, 400, origin); return json({ track: await search(q, env.YOUTUBE_API_KEY) }, 200, origin); }
    if (url.pathname.endsWith("/v1/youtube/playlist")) return json(await playlist((url.searchParams.get("url") || "").trim(), env.YOUTUBE_API_KEY), 200, origin);
    return json({ error: "Not found" }, 404, origin);
  } catch (error) { return json({ error: error instanceof Error ? error.message : "YouTube request failed." }, 502, origin); }
} };
