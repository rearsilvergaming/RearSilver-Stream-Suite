const json = (body, status = 200) => new Response(JSON.stringify(body), {
  status,
  headers: {
    "content-type": "application/json; charset=utf-8",
    "cache-control": status === 200 ? "public, max-age=300" : "no-store",
    "x-content-type-options": "nosniff"
  }
});

const channels = {
  "owner-build": {
    manifestBinding: "OWNER_BUILD_MANIFEST",
    downloadTokenBinding: "OWNER_DOWNLOAD_TOKEN",
    installerFilename: /^RearSilver-Stream-Suite-Owner-[0-9A-Za-z.-]+-Setup\.exe$/
  },
  "private-beta": {
    manifestBinding: "PRIVATE_BETA_MANIFEST",
    downloadTokenBinding: "PRIVATE_BETA_DOWNLOAD_TOKEN",
    installerFilename: /^RearSilver-Stream-Suite-Private-Beta-[0-9A-Za-z.-]+-Setup\.exe$/
  }
};

const ownerTestObjectKey = "tests/owner-build/download-route.txt";

const unauthorized = () => json({ error: "Unauthorised" }, 401);

const hasDownloadAccess = (request, env, channel) => {
  const binding = channels[channel]?.downloadTokenBinding;
  const expected = binding ? env[binding] : null;
  if (!expected) return false;
  const authorization = request.headers.get("authorization") || "";
  return authorization === `Bearer ${expected}`;
};

const configuredManifest = (env, channel) => {
  const binding = channels[channel]?.manifestBinding;
  const raw = binding ? env[binding] : null;
  if (!raw) return null;
  try {
    const manifest = JSON.parse(raw);
    if (manifest?.schema !== 1 || manifest?.product !== "rearsilver-stream-suite" ||
        manifest?.channel !== channel || manifest?.platform !== "windows-x64") return null;
    return manifest;
  } catch {
    return null;
  }
};

const safeReleasePart = value => typeof value === "string" && /^[0-9A-Za-z][0-9A-Za-z.-]{0,95}$/.test(value);
const safeFilename = (value, channel) => typeof value === "string" &&
  channels[channel]?.installerFilename.test(value) && !value.includes("..");

const serveR2Object = async (object, filename, headOnly = false) => {
  if (!object) return json({ error: "Download not found" }, 404);
  const headers = new Headers();
  object.writeHttpMetadata(headers);
  headers.set("content-type", headers.get("content-type") || "application/octet-stream");
  const range = object.range;
  const length = range && "length" in range ? range.length : object.size;
  headers.set("content-length", String(length));
  headers.set("content-disposition", `attachment; filename="${filename}"`);
  headers.set("cache-control", "private, no-store");
  headers.set("etag", object.httpEtag);
  headers.set("accept-ranges", "bytes");
  headers.set("x-content-type-options", "nosniff");
  let status = 200;
  if (range && "offset" in range && "length" in range) {
    status = 206;
    headers.set("content-range", `bytes ${range.offset}-${range.offset + range.length - 1}/${object.size}`);
  }
  return new Response(headOnly ? null : object.body, { status, headers });
};

export default {
  async fetch(request, env) {
    if (request.method !== "GET" && request.method !== "HEAD") return json({ error: "Method not allowed" }, 405);
    const pathname = new URL(request.url).pathname;

    if (pathname === "/v1/download/owner-build/test") {
      if (!hasDownloadAccess(request, env, "owner-build")) return unauthorized();
      if (!env.RELEASES) return json({ error: "Release storage is not configured" }, 503);
      const object = request.method === "HEAD" ? await env.RELEASES.head(ownerTestObjectKey) :
        await env.RELEASES.get(ownerTestObjectKey, { range: request.headers });
      return serveR2Object(object, "RearSilver-Update-Service-Test.txt", request.method === "HEAD");
    }

    const download = pathname.match(/^\/v1\/download\/(owner-build|private-beta)\/([^/]+)\/windows-x64\/?$/);
    if (download) {
      const channel = download[1];
      const version = download[2];
      if (!hasDownloadAccess(request, env, channel)) return unauthorized();
      if (!env.RELEASES) return json({ error: "Release storage is not configured" }, 503);
      const manifest = configuredManifest(env, channel);
      const installer = manifest?.installer;
      if (!manifest || !safeReleasePart(version) || version !== manifest.version ||
          !safeFilename(installer?.filename, channel)) return json({ error: "Download not found" }, 404);
      const key = `releases/${channel}/${version}/windows-x64/${installer.filename}`;
      const object = request.method === "HEAD" ? await env.RELEASES.head(key) :
        await env.RELEASES.get(key, { range: request.headers });
      return serveR2Object(object, installer.filename, request.method === "HEAD");
    }

    const update = pathname.match(/^\/v1\/updates\/(owner-build|private-beta)\/windows-x64\/?$/);
    if (!update) return json({ error: "Not found" }, 404);
    const channel = update[1];
    const binding = channels[channel].manifestBinding;
    const raw = env[binding];
    if (!raw) return json({ error: "This update channel is not configured" }, 503);
    try {
      const manifest = configuredManifest(env, channel);
      if (!manifest) return json({ error: "Configured manifest is invalid" }, 500);
      return json(manifest);
    } catch {
      return json({ error: "Configured manifest is invalid" }, 500);
    }
  }
};
