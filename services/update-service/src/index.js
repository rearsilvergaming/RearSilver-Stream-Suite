const json = (body, status = 200) => new Response(JSON.stringify(body), {
  status,
  headers: {
    "content-type": "application/json; charset=utf-8",
    "cache-control": status === 200 ? "public, max-age=300" : "no-store",
    "x-content-type-options": "nosniff"
  }
});

const manifests = {
  "/v1/updates/owner-build/windows-x64": "OWNER_BUILD_MANIFEST"
};

const ownerTestObjectKey = "tests/owner-build/download-route.txt";

const unauthorized = () => json({ error: "Unauthorised" }, 401);

const hasOwnerDownloadAccess = (request, env) => {
  const expected = env.OWNER_DOWNLOAD_TOKEN;
  if (!expected) return false;
  const authorization = request.headers.get("authorization") || "";
  return authorization === `Bearer ${expected}`;
};

const configuredManifest = (env, binding) => {
  const raw = env[binding];
  if (!raw) return null;
  try {
    const manifest = JSON.parse(raw);
    if (manifest?.schema !== 1 || manifest?.product !== "rearsilver-stream-suite" ||
        manifest?.channel !== "owner-build" || manifest?.platform !== "windows-x64") return null;
    return manifest;
  } catch {
    return null;
  }
};

const safeReleasePart = value => typeof value === "string" && /^[0-9A-Za-z][0-9A-Za-z.-]{0,95}$/.test(value);
const safeFilename = value => typeof value === "string" &&
  /^RearSilver-Stream-Suite-Owner-[0-9A-Za-z.-]+-Setup\.exe$/.test(value) && !value.includes("..");

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
      if (!hasOwnerDownloadAccess(request, env)) return unauthorized();
      if (!env.RELEASES) return json({ error: "Release storage is not configured" }, 503);
      const object = request.method === "HEAD" ? await env.RELEASES.head(ownerTestObjectKey) :
        await env.RELEASES.get(ownerTestObjectKey, { range: request.headers });
      return serveR2Object(object, "RearSilver-Update-Service-Test.txt", request.method === "HEAD");
    }

    const ownerDownload = pathname.match(/^\/v1\/download\/owner-build\/([^/]+)\/windows-x64\/?$/);
    if (ownerDownload) {
      if (!hasOwnerDownloadAccess(request, env)) return unauthorized();
      if (!env.RELEASES) return json({ error: "Release storage is not configured" }, 503);
      const manifest = configuredManifest(env, "OWNER_BUILD_MANIFEST");
      const installer = manifest?.installer;
      const version = ownerDownload[1];
      if (!manifest || !safeReleasePart(version) || version !== manifest.version ||
          !safeFilename(installer?.filename)) return json({ error: "Download not found" }, 404);
      const key = `releases/owner-build/${version}/windows-x64/${installer.filename}`;
      const object = request.method === "HEAD" ? await env.RELEASES.head(key) :
        await env.RELEASES.get(key, { range: request.headers });
      return serveR2Object(object, installer.filename, request.method === "HEAD");
    }

    const binding = manifests[pathname];
    if (!binding) return json({ error: "Not found" }, 404);
    const raw = env[binding];
    if (!raw) return json({ error: "This update channel is not configured" }, 503);
    try {
      const manifest = configuredManifest(env, binding);
      if (!manifest) return json({ error: "Configured manifest is invalid" }, 500);
      return json(manifest);
    } catch {
      return json({ error: "Configured manifest is invalid" }, 500);
    }
  }
};
