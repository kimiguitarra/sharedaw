// @ts-nocheck
// ShareDAW 同期サーバー（Cloudflare Worker）— 自動生成ファイル。直接編集しないこと（server/ で npm run bundle）。
// Cloudflare のダッシュボードで Worker の「コードを編集」を開き、このファイルの中身をすべて貼り付けてデプロイする。
var __defProp = Object.defineProperty;
var __name = (target, value) => __defProp(target, "name", { value, configurable: true });

// src/util.ts
var HttpError = class extends Error {
  constructor(status, code, message, extra = {}) {
    super(message);
    this.status = status;
    this.code = code;
    this.extra = extra;
  }
  status;
  code;
  extra;
  static {
    __name(this, "HttpError");
  }
};
function json(data, status = 200) {
  return new Response(JSON.stringify(data), {
    status,
    headers: { "content-type": "application/json; charset=utf-8" }
  });
}
__name(json, "json");
function errorResponse(e) {
  return json({ error: e.code, message: e.message, ...e.extra }, e.status);
}
__name(errorResponse, "errorResponse");
function nowIso() {
  return (/* @__PURE__ */ new Date()).toISOString();
}
__name(nowIso, "nowIso");
async function sha256Hex(data) {
  const bytes = typeof data === "string" ? new TextEncoder().encode(data) : data;
  const digest = await crypto.subtle.digest("SHA-256", bytes);
  return hex(digest);
}
__name(sha256Hex, "sha256Hex");
function hex(buffer) {
  return [...new Uint8Array(buffer)].map((b) => b.toString(16).padStart(2, "0")).join("");
}
__name(hex, "hex");
var isSha256 = /* @__PURE__ */ __name((s) => typeof s === "string" && /^[0-9a-f]{64}$/.test(s), "isSha256");
var isUuid = /* @__PURE__ */ __name((s) => typeof s === "string" && /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/.test(s), "isUuid");
async function readJson(request) {
  try {
    return await request.json();
  } catch {
    throw new HttpError(400, "bad_request", "JSON \u3092\u8AAD\u307F\u8FBC\u3081\u307E\u305B\u3093");
  }
}
__name(readJson, "readJson");
var blobKey = /* @__PURE__ */ __name((hash2) => `blobs/${hash2}`, "blobKey");
var releasePlatforms = ["windows", "mac", "linux"];
var isReleasePlatform = /* @__PURE__ */ __name((s) => releasePlatforms.includes(s), "isReleasePlatform");
var releaseKey = /* @__PURE__ */ __name((platform) => `app-releases/${platform}/latest.json`, "releaseKey");

// src/admin.ts
var escapeHtml = /* @__PURE__ */ __name((s) => s.replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[c]), "escapeHtml");
function page(body, status = 200) {
  const html = `<!doctype html>
<html lang="ja"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>ShareDAW \u540C\u671F\u30B5\u30FC\u30D0\u30FC \u7BA1\u7406</title>
<style>
  body { font-family: system-ui, sans-serif; max-width: 640px; margin: 32px auto; padding: 0 16px; line-height: 1.6; }
  label { display: block; margin-top: 12px; }
  input[type=text], input[type=password] { width: 100%; padding: 8px; font-size: 16px; box-sizing: border-box; }
  button { margin-top: 16px; padding: 8px 16px; font-size: 16px; }
  .token { font-family: monospace; font-size: 15px; background: #f3f3f3; padding: 12px; word-break: break-all; }
  .error { color: #b00020; }
  table { border-collapse: collapse; margin-top: 8px; } td, th { border-bottom: 1px solid #ddd; padding: 4px 12px 4px 0; text-align: left; }
</style></head><body>
<h1>ShareDAW \u540C\u671F\u30B5\u30FC\u30D0\u30FC</h1>
${body}
</body></html>`;
  return new Response(html, { status, headers: { "content-type": "text/html; charset=utf-8", "cache-control": "no-store" } });
}
__name(page, "page");
function form(message = "") {
  return `${message}
<h2>\u30E6\u30FC\u30B6\u30FC\u3092\u8FFD\u52A0</h2>
<form method="post" action="/admin">
  <label>\u7BA1\u7406\u30D1\u30B9\u30EF\u30FC\u30C9\uFF08Worker \u306E\u30B7\u30FC\u30AF\u30EC\u30C3\u30C8 ADMIN_PASSWORD\uFF09<input type="password" name="password" required autocomplete="current-password"></label>
  <label>\u8868\u793A\u540D\uFF08\u30A2\u30D7\u30EA\u306E\u300C\u25CB\u25CB \u304C\u7DE8\u96C6\u4E2D\u300D\u306A\u3069\u306B\u51FA\u308B\u540D\u524D\uFF09<input type="text" name="name" required maxlength="40"></label>
  <label><input type="checkbox" name="joinAll" checked> \u65E2\u5B58\u306E\u30D7\u30ED\u30B8\u30A7\u30AF\u30C8\u3059\u3079\u3066\u306B\u53C2\u52A0\u3055\u305B\u308B</label>
  <button type="submit">\u30E6\u30FC\u30B6\u30FC\u3092\u4F5C\u3063\u3066\u30C8\u30FC\u30AF\u30F3\u3092\u767A\u884C</button>
</form>`;
}
__name(form, "form");
async function passwordMatches(env, given) {
  if (!env.ADMIN_PASSWORD) return false;
  const [a, b] = await Promise.all([sha256Hex(given), sha256Hex(env.ADMIN_PASSWORD)]);
  let diff = 0;
  for (let i = 0; i < a.length; ++i) diff |= a.charCodeAt(i) ^ b.charCodeAt(i);
  return diff === 0;
}
__name(passwordMatches, "passwordMatches");
function newToken() {
  const bytes = crypto.getRandomValues(new Uint8Array(32));
  return btoa(String.fromCharCode(...bytes)).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
}
__name(newToken, "newToken");
async function handleAdmin(request, env) {
  if (!env.ADMIN_PASSWORD)
    return page(`<p class="error">\u7BA1\u7406\u30DA\u30FC\u30B8\u3092\u4F7F\u3046\u306B\u306F\u3001Cloudflare \u306E\u30C0\u30C3\u30B7\u30E5\u30DC\u30FC\u30C9\u3067\u3053\u306E Worker \u306E\u30B7\u30FC\u30AF\u30EC\u30C3\u30C8
      <code>ADMIN_PASSWORD</code> \u3092\u8A2D\u5B9A\u3057\u3066\u304F\u3060\u3055\u3044\u3002</p>`, 503);
  if (request.method === "GET") return page(form());
  if (request.method !== "POST") return page("<p>\u5BFE\u5FDC\u3057\u3066\u3044\u306A\u3044\u64CD\u4F5C\u3067\u3059\u3002</p>", 405);
  const data = await request.formData();
  const password = String(data.get("password") ?? "");
  const name = String(data.get("name") ?? "").trim().normalize("NFC");
  if (!await passwordMatches(env, password))
    return page(form(`<p class="error">\u7BA1\u7406\u30D1\u30B9\u30EF\u30FC\u30C9\u304C\u9055\u3044\u307E\u3059\u3002</p>`), 403);
  if (!name || name.length > 40) return page(form(`<p class="error">\u8868\u793A\u540D\u3092 40 \u6587\u5B57\u4EE5\u5185\u3067\u5165\u529B\u3057\u3066\u304F\u3060\u3055\u3044\u3002</p>`), 400);
  const id = crypto.randomUUID();
  const token = newToken();
  const statements = [
    env.DB.prepare("INSERT INTO users (id, display_name, token_hash, created_at) VALUES (?, ?, ?, ?)").bind(id, name, await sha256Hex(token), nowIso())
  ];
  if (data.get("joinAll"))
    statements.push(env.DB.prepare("INSERT OR IGNORE INTO project_members (project_id, user_id) SELECT id, ? FROM projects").bind(id));
  await env.DB.batch(statements);
  const users = await env.DB.prepare("SELECT display_name, created_at FROM users ORDER BY created_at").all();
  const rows = users.results.map((u) => `<tr><td>${escapeHtml(u.display_name)}</td><td>${escapeHtml(u.created_at.slice(0, 10))}</td></tr>`).join("");
  return page(`<p>\u30E6\u30FC\u30B6\u30FC\u300C${escapeHtml(name)}\u300D\u3092\u4F5C\u308A\u307E\u3057\u305F\u3002\u6B21\u306E\u30C8\u30FC\u30AF\u30F3\u3092\u672C\u4EBA\u306B\u6E21\u3057\u3066\u304F\u3060\u3055\u3044\u3002
<strong>\u3053\u306E\u753B\u9762\u3092\u9589\u3058\u308B\u3068\u4E8C\u5EA6\u3068\u8868\u793A\u3067\u304D\u307E\u305B\u3093</strong>\uFF08\u306A\u304F\u3057\u305F\u3089\u65B0\u3057\u3044\u30E6\u30FC\u30B6\u30FC\u3092\u4F5C\u308A\u76F4\u3057\u307E\u3059\uFF09\u3002</p>
<div class="token">${escapeHtml(token)}</div>
<p>\u30A2\u30D7\u30EA\u306E\u300C\u540C\u671F \u2192 \u30B5\u30FC\u30D0\u30FC\u8A2D\u5B9A\u300D\u3067\u3001\u30B5\u30FC\u30D0\u30FC URL\uFF08<code>${escapeHtml(new URL(request.url).origin)}</code>\uFF09\u3068\u3053\u306E\u30C8\u30FC\u30AF\u30F3\u3092\u5165\u529B\u3057\u307E\u3059\u3002</p>
<h2>\u30E6\u30FC\u30B6\u30FC\u4E00\u89A7</h2><table><tr><th>\u8868\u793A\u540D</th><th>\u4F5C\u6210\u65E5</th></tr>${rows}</table>
<p><a href="/admin">\u7D9A\u3051\u3066\u30E6\u30FC\u30B6\u30FC\u3092\u8FFD\u52A0\u3059\u308B</a></p>`);
}
__name(handleAdmin, "handleAdmin");

// node_modules/aws4fetch/dist/aws4fetch.esm.mjs
var encoder = new TextEncoder();
var HOST_SERVICES = {
  appstream2: "appstream",
  cloudhsmv2: "cloudhsm",
  email: "ses",
  marketplace: "aws-marketplace",
  mobile: "AWSMobileHubService",
  pinpoint: "mobiletargeting",
  queue: "sqs",
  "git-codecommit": "codecommit",
  "mturk-requester-sandbox": "mturk-requester",
  "personalize-runtime": "personalize"
};
var UNSIGNABLE_HEADERS = /* @__PURE__ */ new Set([
  "authorization",
  "content-type",
  "content-length",
  "user-agent",
  "presigned-expires",
  "expect",
  "x-amzn-trace-id",
  "range",
  "connection"
]);
var AwsClient = class {
  static {
    __name(this, "AwsClient");
  }
  constructor({ accessKeyId, secretAccessKey, sessionToken, service, region, cache, retries, initRetryMs }) {
    if (accessKeyId == null) throw new TypeError("accessKeyId is a required option");
    if (secretAccessKey == null) throw new TypeError("secretAccessKey is a required option");
    this.accessKeyId = accessKeyId;
    this.secretAccessKey = secretAccessKey;
    this.sessionToken = sessionToken;
    this.service = service;
    this.region = region;
    this.cache = cache || /* @__PURE__ */ new Map();
    this.retries = retries != null ? retries : 10;
    this.initRetryMs = initRetryMs || 50;
  }
  async sign(input, init) {
    if (input instanceof Request) {
      const { method, url, headers, body } = input;
      init = Object.assign({ method, url, headers }, init);
      if (init.body == null && headers.has("Content-Type")) {
        init.body = body != null && headers.has("X-Amz-Content-Sha256") ? body : await input.clone().arrayBuffer();
      }
      input = url;
    }
    const signer = new AwsV4Signer(Object.assign({ url: input.toString() }, init, this, init && init.aws));
    const signed = Object.assign({}, init, await signer.sign());
    delete signed.aws;
    try {
      return new Request(signed.url.toString(), signed);
    } catch (e) {
      if (e instanceof TypeError) {
        return new Request(signed.url.toString(), Object.assign({ duplex: "half" }, signed));
      }
      throw e;
    }
  }
  async fetch(input, init) {
    for (let i = 0; i <= this.retries; i++) {
      const fetched = fetch(await this.sign(input, init));
      if (i === this.retries) {
        return fetched;
      }
      const res = await fetched;
      if (res.status < 500 && res.status !== 429) {
        return res;
      }
      await new Promise((resolve) => setTimeout(resolve, Math.random() * this.initRetryMs * Math.pow(2, i)));
    }
    throw new Error("An unknown error occurred, ensure retries is not negative");
  }
};
var AwsV4Signer = class {
  static {
    __name(this, "AwsV4Signer");
  }
  constructor({ method, url, headers, body, accessKeyId, secretAccessKey, sessionToken, service, region, cache, datetime, signQuery, appendSessionToken, allHeaders, singleEncode }) {
    if (url == null) throw new TypeError("url is a required option");
    if (accessKeyId == null) throw new TypeError("accessKeyId is a required option");
    if (secretAccessKey == null) throw new TypeError("secretAccessKey is a required option");
    this.method = method || (body ? "POST" : "GET");
    this.url = new URL(url);
    this.headers = new Headers(headers || {});
    this.body = body;
    this.accessKeyId = accessKeyId;
    this.secretAccessKey = secretAccessKey;
    this.sessionToken = sessionToken;
    let guessedService, guessedRegion;
    if (!service || !region) {
      [guessedService, guessedRegion] = guessServiceRegion(this.url, this.headers);
    }
    this.service = service || guessedService || "";
    this.region = region || guessedRegion || "us-east-1";
    this.cache = cache || /* @__PURE__ */ new Map();
    this.datetime = datetime || (/* @__PURE__ */ new Date()).toISOString().replace(/[:-]|\.\d{3}/g, "");
    this.signQuery = signQuery;
    this.appendSessionToken = appendSessionToken || this.service === "iotdevicegateway";
    this.headers.delete("Host");
    if (this.service === "s3" && !this.signQuery && !this.headers.has("X-Amz-Content-Sha256")) {
      this.headers.set("X-Amz-Content-Sha256", "UNSIGNED-PAYLOAD");
    }
    const params = this.signQuery ? this.url.searchParams : this.headers;
    params.set("X-Amz-Date", this.datetime);
    if (this.sessionToken && !this.appendSessionToken) {
      params.set("X-Amz-Security-Token", this.sessionToken);
    }
    this.signableHeaders = ["host", ...this.headers.keys()].filter((header) => allHeaders || !UNSIGNABLE_HEADERS.has(header)).sort();
    this.signedHeaders = this.signableHeaders.join(";");
    this.canonicalHeaders = this.signableHeaders.map((header) => header + ":" + (header === "host" ? this.url.host : (this.headers.get(header) || "").replace(/\s+/g, " "))).join("\n");
    this.credentialString = [this.datetime.slice(0, 8), this.region, this.service, "aws4_request"].join("/");
    if (this.signQuery) {
      if (this.service === "s3" && !params.has("X-Amz-Expires")) {
        params.set("X-Amz-Expires", "86400");
      }
      params.set("X-Amz-Algorithm", "AWS4-HMAC-SHA256");
      params.set("X-Amz-Credential", this.accessKeyId + "/" + this.credentialString);
      params.set("X-Amz-SignedHeaders", this.signedHeaders);
    }
    if (this.service === "s3") {
      try {
        this.encodedPath = decodeURIComponent(this.url.pathname.replace(/\+/g, " "));
      } catch (e) {
        this.encodedPath = this.url.pathname;
      }
    } else {
      this.encodedPath = this.url.pathname.replace(/\/+/g, "/");
    }
    if (!singleEncode) {
      this.encodedPath = encodeURIComponent(this.encodedPath).replace(/%2F/g, "/");
    }
    this.encodedPath = encodeRfc3986(this.encodedPath);
    const seenKeys = /* @__PURE__ */ new Set();
    this.encodedSearch = [...this.url.searchParams].filter(([k]) => {
      if (!k) return false;
      if (this.service === "s3") {
        if (seenKeys.has(k)) return false;
        seenKeys.add(k);
      }
      return true;
    }).map((pair) => pair.map((p) => encodeRfc3986(encodeURIComponent(p)))).sort(([k1, v1], [k2, v2]) => k1 < k2 ? -1 : k1 > k2 ? 1 : v1 < v2 ? -1 : v1 > v2 ? 1 : 0).map((pair) => pair.join("=")).join("&");
  }
  async sign() {
    if (this.signQuery) {
      this.url.searchParams.set("X-Amz-Signature", await this.signature());
      if (this.sessionToken && this.appendSessionToken) {
        this.url.searchParams.set("X-Amz-Security-Token", this.sessionToken);
      }
    } else {
      this.headers.set("Authorization", await this.authHeader());
    }
    return {
      method: this.method,
      url: this.url,
      headers: this.headers,
      body: this.body
    };
  }
  async authHeader() {
    return [
      "AWS4-HMAC-SHA256 Credential=" + this.accessKeyId + "/" + this.credentialString,
      "SignedHeaders=" + this.signedHeaders,
      "Signature=" + await this.signature()
    ].join(", ");
  }
  async signature() {
    const date2 = this.datetime.slice(0, 8);
    const cacheKey = [this.secretAccessKey, date2, this.region, this.service].join();
    let kCredentials = this.cache.get(cacheKey);
    if (!kCredentials) {
      const kDate = await hmac("AWS4" + this.secretAccessKey, date2);
      const kRegion = await hmac(kDate, this.region);
      const kService = await hmac(kRegion, this.service);
      kCredentials = await hmac(kService, "aws4_request");
      this.cache.set(cacheKey, kCredentials);
    }
    return buf2hex(await hmac(kCredentials, await this.stringToSign()));
  }
  async stringToSign() {
    return [
      "AWS4-HMAC-SHA256",
      this.datetime,
      this.credentialString,
      buf2hex(await hash(await this.canonicalString()))
    ].join("\n");
  }
  async canonicalString() {
    return [
      this.method.toUpperCase(),
      this.encodedPath,
      this.encodedSearch,
      this.canonicalHeaders + "\n",
      this.signedHeaders,
      await this.hexBodyHash()
    ].join("\n");
  }
  async hexBodyHash() {
    let hashHeader = this.headers.get("X-Amz-Content-Sha256") || (this.service === "s3" && this.signQuery ? "UNSIGNED-PAYLOAD" : null);
    if (hashHeader == null) {
      if (this.body && typeof this.body !== "string" && !("byteLength" in this.body)) {
        throw new Error("body must be a string, ArrayBuffer or ArrayBufferView, unless you include the X-Amz-Content-Sha256 header");
      }
      hashHeader = buf2hex(await hash(this.body || ""));
    }
    return hashHeader;
  }
};
async function hmac(key, string) {
  const cryptoKey = await crypto.subtle.importKey(
    "raw",
    typeof key === "string" ? encoder.encode(key) : key,
    { name: "HMAC", hash: { name: "SHA-256" } },
    false,
    ["sign"]
  );
  return crypto.subtle.sign("HMAC", cryptoKey, encoder.encode(string));
}
__name(hmac, "hmac");
async function hash(content) {
  return crypto.subtle.digest("SHA-256", typeof content === "string" ? encoder.encode(content) : content);
}
__name(hash, "hash");
var HEX_CHARS = ["0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "a", "b", "c", "d", "e", "f"];
function buf2hex(arrayBuffer) {
  const buffer = new Uint8Array(arrayBuffer);
  let out = "";
  for (let idx = 0; idx < buffer.length; idx++) {
    const n = buffer[idx];
    out += HEX_CHARS[n >>> 4 & 15];
    out += HEX_CHARS[n & 15];
  }
  return out;
}
__name(buf2hex, "buf2hex");
function encodeRfc3986(urlEncodedStr) {
  return urlEncodedStr.replace(/[!'()*]/g, (c) => "%" + c.charCodeAt(0).toString(16).toUpperCase());
}
__name(encodeRfc3986, "encodeRfc3986");
function guessServiceRegion(url, headers) {
  const { hostname, pathname } = url;
  if (hostname.endsWith(".on.aws")) {
    const match2 = hostname.match(/^[^.]{1,63}\.lambda-url\.([^.]{1,63})\.on\.aws$/);
    return match2 != null ? ["lambda", match2[1] || ""] : ["", ""];
  }
  if (hostname.endsWith(".r2.cloudflarestorage.com")) {
    return ["s3", "auto"];
  }
  if (hostname.endsWith(".backblazeb2.com")) {
    const match2 = hostname.match(/^(?:[^.]{1,63}\.)?s3\.([^.]{1,63})\.backblazeb2\.com$/);
    return match2 != null ? ["s3", match2[1] || ""] : ["", ""];
  }
  const match = hostname.replace("dualstack.", "").match(/([^.]{1,63})\.(?:([^.]{0,63})\.)?amazonaws\.com(?:\.cn)?$/);
  let service = match && match[1] || "";
  let region = match && match[2];
  if (region === "us-gov") {
    region = "us-gov-west-1";
  } else if (region === "s3" || region === "s3-accelerate") {
    region = "us-east-1";
    service = "s3";
  } else if (service === "iot") {
    if (hostname.startsWith("iot.")) {
      service = "execute-api";
    } else if (hostname.startsWith("data.jobs.iot.")) {
      service = "iot-jobs-data";
    } else {
      service = pathname === "/mqtt" ? "iotdevicegateway" : "iotdata";
    }
  } else if (service === "autoscaling") {
    const targetPrefix = (headers.get("X-Amz-Target") || "").split(".")[0];
    if (targetPrefix === "AnyScaleFrontendService") {
      service = "application-autoscaling";
    } else if (targetPrefix === "AnyScaleScalingPlannerFrontendService") {
      service = "autoscaling-plans";
    }
  } else if (region == null && service.startsWith("s3-")) {
    region = service.slice(3).replace(/^fips-|^external-1/, "");
    service = "s3";
  } else if (service.endsWith("-fips")) {
    service = service.slice(0, -5);
  } else if (region && /-\d$/.test(service) && !/-\d$/.test(region)) {
    [service, region] = [region, service];
  }
  return [HOST_SERVICES[service] || service, region || ""];
}
__name(guessServiceRegion, "guessServiceRegion");

// src/blobs.ts
var presignExpirySeconds = 3600;
function presignEnabled(env) {
  return !!(env.R2_ACCESS_KEY_ID && env.R2_SECRET_ACCESS_KEY && env.R2_ACCOUNT_ID && env.R2_BUCKET_NAME);
}
__name(presignEnabled, "presignEnabled");
async function transferUrl(env, origin, hash2, method) {
  if (!presignEnabled(env)) {
    return { hash: hash2, url: `${origin}/blobs/${hash2}/data`, method, authRequired: true };
  }
  const client = new AwsClient({
    accessKeyId: env.R2_ACCESS_KEY_ID,
    secretAccessKey: env.R2_SECRET_ACCESS_KEY,
    service: "s3",
    region: "auto"
  });
  const url = new URL(`https://${env.R2_ACCOUNT_ID}.r2.cloudflarestorage.com/${env.R2_BUCKET_NAME}/${blobKey(hash2)}`);
  url.searchParams.set("X-Amz-Expires", String(presignExpirySeconds));
  const signed = await client.sign(new Request(url, { method }), { aws: { signQuery: true } });
  return { hash: hash2, url: signed.url, method, authRequired: false };
}
__name(transferUrl, "transferUrl");
async function registeredHashes(env, hashes) {
  const found = /* @__PURE__ */ new Set();
  for (let i = 0; i < hashes.length; i += 50) {
    const chunk = hashes.slice(i, i + 50);
    const placeholders = chunk.map(() => "?").join(",");
    const rows = await env.DB.prepare(`SELECT hash FROM blobs WHERE hash IN (${placeholders})`).bind(...chunk).all();
    for (const r of rows.results) found.add(r.hash);
  }
  return found;
}
__name(registeredHashes, "registeredHashes");
async function verifyAndRegister(env, hash2) {
  const existing = await env.DB.prepare("SELECT size FROM blobs WHERE hash = ?").bind(hash2).first();
  if (existing) return { hash: hash2, size: existing.size };
  const obj = await env.BLOBS.get(blobKey(hash2));
  if (!obj) throw new HttpError(404, "blob_not_uploaded", "\u30A2\u30C3\u30D7\u30ED\u30FC\u30C9\u3055\u308C\u3066\u3044\u307E\u305B\u3093");
  const digestStream = new crypto.DigestStream("SHA-256");
  await obj.body.pipeTo(digestStream);
  const actual = hex(await digestStream.digest);
  if (actual !== hash2) {
    await env.BLOBS.delete(blobKey(hash2));
    throw new HttpError(400, "hash_mismatch", "\u30A2\u30C3\u30D7\u30ED\u30FC\u30C9\u3055\u308C\u305F\u5185\u5BB9\u306E\u30CF\u30C3\u30B7\u30E5\u304C\u4E00\u81F4\u3057\u307E\u305B\u3093", { actual });
  }
  await env.DB.prepare("INSERT OR IGNORE INTO blobs (hash, size, created_at) VALUES (?, ?, ?)").bind(hash2, obj.size, nowIso()).run();
  return { hash: hash2, size: obj.size };
}
__name(verifyAndRegister, "verifyAndRegister");
async function directUpload(env, hash2, request) {
  const data = await request.arrayBuffer();
  const actual = await sha256Hex(data);
  if (actual !== hash2) throw new HttpError(400, "hash_mismatch", "\u30A2\u30C3\u30D7\u30ED\u30FC\u30C9\u3055\u308C\u305F\u5185\u5BB9\u306E\u30CF\u30C3\u30B7\u30E5\u304C\u4E00\u81F4\u3057\u307E\u305B\u3093", { actual });
  await env.BLOBS.put(blobKey(hash2), data);
  await env.DB.prepare("INSERT OR IGNORE INTO blobs (hash, size, created_at) VALUES (?, ?, ?)").bind(hash2, data.byteLength, nowIso()).run();
  return { hash: hash2, size: data.byteLength };
}
__name(directUpload, "directUpload");
async function directDownload(env, hash2) {
  const obj = await env.BLOBS.get(blobKey(hash2));
  if (!obj) throw new HttpError(404, "not_found", "\u898B\u3064\u304B\u308A\u307E\u305B\u3093");
  return new Response(obj.body, { headers: { "content-type": "application/octet-stream", "content-length": String(obj.size) } });
}
__name(directDownload, "directDownload");

// node_modules/@cfworker/json-schema/dist/esm/deep-compare-strict.js
function deepCompareStrict(a, b) {
  const typeofa = typeof a;
  if (typeofa !== typeof b) {
    return false;
  }
  if (Array.isArray(a)) {
    if (!Array.isArray(b)) {
      return false;
    }
    const length = a.length;
    if (length !== b.length) {
      return false;
    }
    for (let i = 0; i < length; i++) {
      if (!deepCompareStrict(a[i], b[i])) {
        return false;
      }
    }
    return true;
  }
  if (typeofa === "object") {
    if (!a || !b) {
      return a === b;
    }
    const aKeys = Object.keys(a);
    const bKeys = Object.keys(b);
    const length = aKeys.length;
    if (length !== bKeys.length) {
      return false;
    }
    for (const k of aKeys) {
      if (!deepCompareStrict(a[k], b[k])) {
        return false;
      }
    }
    return true;
  }
  return a === b;
}
__name(deepCompareStrict, "deepCompareStrict");

// node_modules/@cfworker/json-schema/dist/esm/pointer.js
function encodePointer(p) {
  return encodeURI(escapePointer(p));
}
__name(encodePointer, "encodePointer");
function escapePointer(p) {
  return p.replace(/~/g, "~0").replace(/\//g, "~1");
}
__name(escapePointer, "escapePointer");

// node_modules/@cfworker/json-schema/dist/esm/dereference.js
var schemaArrayKeyword = {
  prefixItems: true,
  items: true,
  allOf: true,
  anyOf: true,
  oneOf: true
};
var schemaMapKeyword = {
  $defs: true,
  definitions: true,
  properties: true,
  patternProperties: true,
  dependentSchemas: true
};
var ignoredKeyword = {
  id: true,
  $id: true,
  $ref: true,
  $schema: true,
  $anchor: true,
  $vocabulary: true,
  $comment: true,
  default: true,
  enum: true,
  const: true,
  required: true,
  type: true,
  maximum: true,
  minimum: true,
  exclusiveMaximum: true,
  exclusiveMinimum: true,
  multipleOf: true,
  maxLength: true,
  minLength: true,
  pattern: true,
  format: true,
  maxItems: true,
  minItems: true,
  uniqueItems: true,
  maxProperties: true,
  minProperties: true
};
var initialBaseURI = typeof self !== "undefined" && self.location && self.location.origin !== "null" ? new URL(self.location.origin + self.location.pathname + location.search) : new URL("https://github.com/cfworker");
function dereference(schema, lookup = /* @__PURE__ */ Object.create(null), baseURI = initialBaseURI, basePointer = "") {
  if (schema && typeof schema === "object" && !Array.isArray(schema)) {
    const id = schema.$id || schema.id;
    if (id) {
      const url = new URL(id, baseURI.href);
      if (url.hash.length > 1) {
        lookup[url.href] = schema;
      } else {
        url.hash = "";
        if (basePointer === "") {
          baseURI = url;
        } else {
          dereference(schema, lookup, baseURI);
        }
      }
    }
  } else if (schema !== true && schema !== false) {
    return lookup;
  }
  const schemaURI = baseURI.href + (basePointer ? "#" + basePointer : "");
  if (lookup[schemaURI] !== void 0) {
    throw new Error(`Duplicate schema URI "${schemaURI}".`);
  }
  lookup[schemaURI] = schema;
  if (schema === true || schema === false) {
    return lookup;
  }
  if (schema.__absolute_uri__ === void 0) {
    Object.defineProperty(schema, "__absolute_uri__", {
      enumerable: false,
      value: schemaURI
    });
  }
  if (schema.$ref && schema.__absolute_ref__ === void 0) {
    const url = new URL(schema.$ref, baseURI.href);
    url.hash = url.hash;
    Object.defineProperty(schema, "__absolute_ref__", {
      enumerable: false,
      value: url.href
    });
  }
  if (schema.$recursiveRef && schema.__absolute_recursive_ref__ === void 0) {
    const url = new URL(schema.$recursiveRef, baseURI.href);
    url.hash = url.hash;
    Object.defineProperty(schema, "__absolute_recursive_ref__", {
      enumerable: false,
      value: url.href
    });
  }
  if (schema.$anchor) {
    const url = new URL("#" + schema.$anchor, baseURI.href);
    lookup[url.href] = schema;
  }
  for (let key in schema) {
    if (ignoredKeyword[key]) {
      continue;
    }
    const keyBase = `${basePointer}/${encodePointer(key)}`;
    const subSchema = schema[key];
    if (Array.isArray(subSchema)) {
      if (schemaArrayKeyword[key]) {
        const length = subSchema.length;
        for (let i = 0; i < length; i++) {
          dereference(subSchema[i], lookup, baseURI, `${keyBase}/${i}`);
        }
      }
    } else if (schemaMapKeyword[key]) {
      for (let subKey in subSchema) {
        dereference(subSchema[subKey], lookup, baseURI, `${keyBase}/${encodePointer(subKey)}`);
      }
    } else {
      dereference(subSchema, lookup, baseURI, keyBase);
    }
  }
  return lookup;
}
__name(dereference, "dereference");

// node_modules/@cfworker/json-schema/dist/esm/format.js
var DATE = /^(\d\d\d\d)-(\d\d)-(\d\d)$/;
var DAYS = [0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31];
var TIME = /^(\d\d):(\d\d):(\d\d)(\.\d+)?(z|[+-]\d\d(?::?\d\d)?)?$/i;
var HOSTNAME = /^(?=.{1,253}\.?$)[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?(?:\.[a-z0-9](?:[-0-9a-z]{0,61}[0-9a-z])?)*\.?$/i;
var URIREF = /^(?:[a-z][a-z0-9+\-.]*:)?(?:\/?\/(?:(?:[a-z0-9\-._~!$&'()*+,;=:]|%[0-9a-f]{2})*@)?(?:\[(?:(?:(?:(?:[0-9a-f]{1,4}:){6}|::(?:[0-9a-f]{1,4}:){5}|(?:[0-9a-f]{1,4})?::(?:[0-9a-f]{1,4}:){4}|(?:(?:[0-9a-f]{1,4}:){0,1}[0-9a-f]{1,4})?::(?:[0-9a-f]{1,4}:){3}|(?:(?:[0-9a-f]{1,4}:){0,2}[0-9a-f]{1,4})?::(?:[0-9a-f]{1,4}:){2}|(?:(?:[0-9a-f]{1,4}:){0,3}[0-9a-f]{1,4})?::[0-9a-f]{1,4}:|(?:(?:[0-9a-f]{1,4}:){0,4}[0-9a-f]{1,4})?::)(?:[0-9a-f]{1,4}:[0-9a-f]{1,4}|(?:(?:25[0-5]|2[0-4]\d|[01]?\d\d?)\.){3}(?:25[0-5]|2[0-4]\d|[01]?\d\d?))|(?:(?:[0-9a-f]{1,4}:){0,5}[0-9a-f]{1,4})?::[0-9a-f]{1,4}|(?:(?:[0-9a-f]{1,4}:){0,6}[0-9a-f]{1,4})?::)|[Vv][0-9a-f]+\.[a-z0-9\-._~!$&'()*+,;=:]+)\]|(?:(?:25[0-5]|2[0-4]\d|[01]?\d\d?)\.){3}(?:25[0-5]|2[0-4]\d|[01]?\d\d?)|(?:[a-z0-9\-._~!$&'"()*+,;=]|%[0-9a-f]{2})*)(?::\d*)?(?:\/(?:[a-z0-9\-._~!$&'"()*+,;=:@]|%[0-9a-f]{2})*)*|\/(?:(?:[a-z0-9\-._~!$&'"()*+,;=:@]|%[0-9a-f]{2})+(?:\/(?:[a-z0-9\-._~!$&'"()*+,;=:@]|%[0-9a-f]{2})*)*)?|(?:[a-z0-9\-._~!$&'"()*+,;=:@]|%[0-9a-f]{2})+(?:\/(?:[a-z0-9\-._~!$&'"()*+,;=:@]|%[0-9a-f]{2})*)*)?(?:\?(?:[a-z0-9\-._~!$&'"()*+,;=:@/?]|%[0-9a-f]{2})*)?(?:#(?:[a-z0-9\-._~!$&'"()*+,;=:@/?]|%[0-9a-f]{2})*)?$/i;
var URITEMPLATE = /^(?:(?:[^\x00-\x20"'<>%\\^`{|}]|%[0-9a-f]{2})|\{[+#./;?&=,!@|]?(?:[a-z0-9_]|%[0-9a-f]{2})+(?::[1-9][0-9]{0,3}|\*)?(?:,(?:[a-z0-9_]|%[0-9a-f]{2})+(?::[1-9][0-9]{0,3}|\*)?)*\})*$/i;
var URL_ = /^(?:(?:https?|ftp):\/\/)(?:\S+(?::\S*)?@)?(?:(?!10(?:\.\d{1,3}){3})(?!127(?:\.\d{1,3}){3})(?!169\.254(?:\.\d{1,3}){2})(?!192\.168(?:\.\d{1,3}){2})(?!172\.(?:1[6-9]|2\d|3[0-1])(?:\.\d{1,3}){2})(?:[1-9]\d?|1\d\d|2[01]\d|22[0-3])(?:\.(?:1?\d{1,2}|2[0-4]\d|25[0-5])){2}(?:\.(?:[1-9]\d?|1\d\d|2[0-4]\d|25[0-4]))|(?:(?:[a-z\u{00a1}-\u{ffff}0-9]+-?)*[a-z\u{00a1}-\u{ffff}0-9]+)(?:\.(?:[a-z\u{00a1}-\u{ffff}0-9]+-?)*[a-z\u{00a1}-\u{ffff}0-9]+)*(?:\.(?:[a-z\u{00a1}-\u{ffff}]{2,})))(?::\d{2,5})?(?:\/[^\s]*)?$/iu;
var UUID = /^(?:urn:uuid:)?[0-9a-f]{8}-(?:[0-9a-f]{4}-){3}[0-9a-f]{12}$/i;
var JSON_POINTER = /^(?:\/(?:[^~/]|~0|~1)*)*$/;
var JSON_POINTER_URI_FRAGMENT = /^#(?:\/(?:[a-z0-9_\-.!$&'()*+,;:=@]|%[0-9a-f]{2}|~0|~1)*)*$/i;
var RELATIVE_JSON_POINTER = /^(?:0|[1-9][0-9]*)(?:#|(?:\/(?:[^~/]|~0|~1)*)*)$/;
var EMAIL = /* @__PURE__ */ __name((input) => {
  if (input[0] === '"')
    return false;
  const [name, host, ...rest] = input.split("@");
  if (!name || !host || rest.length !== 0 || name.length > 64 || host.length > 253)
    return false;
  if (name[0] === "." || name.endsWith(".") || name.includes(".."))
    return false;
  if (!/^[a-z0-9.-]+$/i.test(host) || !/^[a-z0-9.!#$%&'*+/=?^_`{|}~-]+$/i.test(name))
    return false;
  return host.split(".").every((part) => /^[a-z0-9]([a-z0-9-]{0,61}[a-z0-9])?$/i.test(part));
}, "EMAIL");
var IPV4 = /^(?:(?:25[0-5]|2[0-4]\d|[01]?\d\d?)\.){3}(?:25[0-5]|2[0-4]\d|[01]?\d\d?)$/;
var IPV6 = /^((([0-9a-f]{1,4}:){7}([0-9a-f]{1,4}|:))|(([0-9a-f]{1,4}:){6}(:[0-9a-f]{1,4}|((25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)(\.(25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)){3})|:))|(([0-9a-f]{1,4}:){5}(((:[0-9a-f]{1,4}){1,2})|:((25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)(\.(25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)){3})|:))|(([0-9a-f]{1,4}:){4}(((:[0-9a-f]{1,4}){1,3})|((:[0-9a-f]{1,4})?:((25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)(\.(25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)){3}))|:))|(([0-9a-f]{1,4}:){3}(((:[0-9a-f]{1,4}){1,4})|((:[0-9a-f]{1,4}){0,2}:((25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)(\.(25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)){3}))|:))|(([0-9a-f]{1,4}:){2}(((:[0-9a-f]{1,4}){1,5})|((:[0-9a-f]{1,4}){0,3}:((25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)(\.(25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)){3}))|:))|(([0-9a-f]{1,4}:){1}(((:[0-9a-f]{1,4}){1,6})|((:[0-9a-f]{1,4}){0,4}:((25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)(\.(25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)){3}))|:))|(:(((:[0-9a-f]{1,4}){1,7})|((:[0-9a-f]{1,4}){0,5}:((25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)(\.(25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)){3}))|:)))$/i;
var DURATION = /* @__PURE__ */ __name((input) => input.length > 1 && input.length < 80 && (/^P\d+([.,]\d+)?W$/.test(input) || /^P[\dYMDTHS]*(\d[.,]\d+)?[YMDHS]$/.test(input) && /^P([.,\d]+Y)?([.,\d]+M)?([.,\d]+D)?(T([.,\d]+H)?([.,\d]+M)?([.,\d]+S)?)?$/.test(input)), "DURATION");
function bind(r) {
  return r.test.bind(r);
}
__name(bind, "bind");
var format = {
  date,
  time: time.bind(void 0, false),
  "date-time": date_time,
  duration: DURATION,
  uri,
  "uri-reference": bind(URIREF),
  "uri-template": bind(URITEMPLATE),
  url: bind(URL_),
  email: EMAIL,
  hostname: bind(HOSTNAME),
  ipv4: bind(IPV4),
  ipv6: bind(IPV6),
  regex,
  uuid: bind(UUID),
  "json-pointer": bind(JSON_POINTER),
  "json-pointer-uri-fragment": bind(JSON_POINTER_URI_FRAGMENT),
  "relative-json-pointer": bind(RELATIVE_JSON_POINTER)
};
function isLeapYear(year) {
  return year % 4 === 0 && (year % 100 !== 0 || year % 400 === 0);
}
__name(isLeapYear, "isLeapYear");
function date(str) {
  const matches = str.match(DATE);
  if (!matches)
    return false;
  const year = +matches[1];
  const month = +matches[2];
  const day = +matches[3];
  return month >= 1 && month <= 12 && day >= 1 && day <= (month == 2 && isLeapYear(year) ? 29 : DAYS[month]);
}
__name(date, "date");
function time(full, str) {
  const matches = str.match(TIME);
  if (!matches)
    return false;
  const hour = +matches[1];
  const minute = +matches[2];
  const second = +matches[3];
  const timeZone = !!matches[5];
  return (hour <= 23 && minute <= 59 && second <= 59 || hour == 23 && minute == 59 && second == 60) && (!full || timeZone);
}
__name(time, "time");
var DATE_TIME_SEPARATOR = /t|\s/i;
function date_time(str) {
  const dateTime = str.split(DATE_TIME_SEPARATOR);
  return dateTime.length == 2 && date(dateTime[0]) && time(true, dateTime[1]);
}
__name(date_time, "date_time");
var NOT_URI_FRAGMENT = /\/|:/;
var URI_PATTERN = /^(?:[a-z][a-z0-9+\-.]*:)(?:\/?\/(?:(?:[a-z0-9\-._~!$&'()*+,;=:]|%[0-9a-f]{2})*@)?(?:\[(?:(?:(?:(?:[0-9a-f]{1,4}:){6}|::(?:[0-9a-f]{1,4}:){5}|(?:[0-9a-f]{1,4})?::(?:[0-9a-f]{1,4}:){4}|(?:(?:[0-9a-f]{1,4}:){0,1}[0-9a-f]{1,4})?::(?:[0-9a-f]{1,4}:){3}|(?:(?:[0-9a-f]{1,4}:){0,2}[0-9a-f]{1,4})?::(?:[0-9a-f]{1,4}:){2}|(?:(?:[0-9a-f]{1,4}:){0,3}[0-9a-f]{1,4})?::[0-9a-f]{1,4}:|(?:(?:[0-9a-f]{1,4}:){0,4}[0-9a-f]{1,4})?::)(?:[0-9a-f]{1,4}:[0-9a-f]{1,4}|(?:(?:25[0-5]|2[0-4]\d|[01]?\d\d?)\.){3}(?:25[0-5]|2[0-4]\d|[01]?\d\d?))|(?:(?:[0-9a-f]{1,4}:){0,5}[0-9a-f]{1,4})?::[0-9a-f]{1,4}|(?:(?:[0-9a-f]{1,4}:){0,6}[0-9a-f]{1,4})?::)|[Vv][0-9a-f]+\.[a-z0-9\-._~!$&'()*+,;=:]+)\]|(?:(?:25[0-5]|2[0-4]\d|[01]?\d\d?)\.){3}(?:25[0-5]|2[0-4]\d|[01]?\d\d?)|(?:[a-z0-9\-._~!$&'()*+,;=]|%[0-9a-f]{2})*)(?::\d*)?(?:\/(?:[a-z0-9\-._~!$&'()*+,;=:@]|%[0-9a-f]{2})*)*|\/(?:(?:[a-z0-9\-._~!$&'()*+,;=:@]|%[0-9a-f]{2})+(?:\/(?:[a-z0-9\-._~!$&'()*+,;=:@]|%[0-9a-f]{2})*)*)?|(?:[a-z0-9\-._~!$&'()*+,;=:@]|%[0-9a-f]{2})+(?:\/(?:[a-z0-9\-._~!$&'()*+,;=:@]|%[0-9a-f]{2})*)*)(?:\?(?:[a-z0-9\-._~!$&'()*+,;=:@/?]|%[0-9a-f]{2})*)?(?:#(?:[a-z0-9\-._~!$&'()*+,;=:@/?]|%[0-9a-f]{2})*)?$/i;
function uri(str) {
  return NOT_URI_FRAGMENT.test(str) && URI_PATTERN.test(str);
}
__name(uri, "uri");
var Z_ANCHOR = /[^\\]\\Z/;
function regex(str) {
  if (Z_ANCHOR.test(str))
    return false;
  try {
    new RegExp(str, "u");
    return true;
  } catch (e) {
    return false;
  }
}
__name(regex, "regex");

// node_modules/@cfworker/json-schema/dist/esm/types.js
var OutputFormat;
(function(OutputFormat2) {
  OutputFormat2[OutputFormat2["Flag"] = 1] = "Flag";
  OutputFormat2[OutputFormat2["Basic"] = 2] = "Basic";
  OutputFormat2[OutputFormat2["Detailed"] = 4] = "Detailed";
})(OutputFormat || (OutputFormat = {}));

// node_modules/@cfworker/json-schema/dist/esm/ucs2-length.js
function ucs2length(s) {
  let result = 0;
  let length = s.length;
  let index = 0;
  let charCode;
  while (index < length) {
    result++;
    charCode = s.charCodeAt(index++);
    if (charCode >= 55296 && charCode <= 56319 && index < length) {
      charCode = s.charCodeAt(index);
      if ((charCode & 64512) == 56320) {
        index++;
      }
    }
  }
  return result;
}
__name(ucs2length, "ucs2length");

// node_modules/@cfworker/json-schema/dist/esm/validate.js
function validate(instance, schema, draft = "2019-09", lookup = dereference(schema), shortCircuit = true, recursiveAnchor = null, instanceLocation = "#", schemaLocation = "#", evaluated = /* @__PURE__ */ Object.create(null)) {
  if (schema === true) {
    return { valid: true, errors: [] };
  }
  if (schema === false) {
    return {
      valid: false,
      errors: [
        {
          instanceLocation,
          keyword: "false",
          keywordLocation: instanceLocation,
          error: "False boolean schema."
        }
      ]
    };
  }
  const rawInstanceType = typeof instance;
  let instanceType;
  switch (rawInstanceType) {
    case "boolean":
    case "number":
    case "string":
      instanceType = rawInstanceType;
      break;
    case "object":
      if (instance === null) {
        instanceType = "null";
      } else if (Array.isArray(instance)) {
        instanceType = "array";
      } else {
        instanceType = "object";
      }
      break;
    default:
      throw new Error(`Instances of "${rawInstanceType}" type are not supported.`);
  }
  const { $ref, $recursiveRef, $recursiveAnchor, type: $type, const: $const, enum: $enum, required: $required, not: $not, anyOf: $anyOf, allOf: $allOf, oneOf: $oneOf, if: $if, then: $then, else: $else, format: $format, properties: $properties, patternProperties: $patternProperties, additionalProperties: $additionalProperties, unevaluatedProperties: $unevaluatedProperties, minProperties: $minProperties, maxProperties: $maxProperties, propertyNames: $propertyNames, dependentRequired: $dependentRequired, dependentSchemas: $dependentSchemas, dependencies: $dependencies, prefixItems: $prefixItems, items: $items, additionalItems: $additionalItems, unevaluatedItems: $unevaluatedItems, contains: $contains, minContains: $minContains, maxContains: $maxContains, minItems: $minItems, maxItems: $maxItems, uniqueItems: $uniqueItems, minimum: $minimum, maximum: $maximum, exclusiveMinimum: $exclusiveMinimum, exclusiveMaximum: $exclusiveMaximum, multipleOf: $multipleOf, minLength: $minLength, maxLength: $maxLength, pattern: $pattern, __absolute_ref__, __absolute_recursive_ref__ } = schema;
  const errors = [];
  if ($recursiveAnchor === true && recursiveAnchor === null) {
    recursiveAnchor = schema;
  }
  if ($recursiveRef === "#") {
    const refSchema = recursiveAnchor === null ? lookup[__absolute_recursive_ref__] : recursiveAnchor;
    const keywordLocation = `${schemaLocation}/$recursiveRef`;
    const result = validate(instance, recursiveAnchor === null ? schema : recursiveAnchor, draft, lookup, shortCircuit, refSchema, instanceLocation, keywordLocation, evaluated);
    if (!result.valid) {
      errors.push({
        instanceLocation,
        keyword: "$recursiveRef",
        keywordLocation,
        error: "A subschema had errors."
      }, ...result.errors);
    }
  }
  if ($ref !== void 0) {
    const uri2 = __absolute_ref__ || $ref;
    const refSchema = lookup[uri2];
    if (refSchema === void 0) {
      let message = `Unresolved $ref "${$ref}".`;
      if (__absolute_ref__ && __absolute_ref__ !== $ref) {
        message += `  Absolute URI "${__absolute_ref__}".`;
      }
      message += `
Known schemas:
- ${Object.keys(lookup).join("\n- ")}`;
      throw new Error(message);
    }
    const keywordLocation = `${schemaLocation}/$ref`;
    const result = validate(instance, refSchema, draft, lookup, shortCircuit, recursiveAnchor, instanceLocation, keywordLocation, evaluated);
    if (!result.valid) {
      errors.push({
        instanceLocation,
        keyword: "$ref",
        keywordLocation,
        error: "A subschema had errors."
      }, ...result.errors);
    }
    if (draft === "4" || draft === "7") {
      return { valid: errors.length === 0, errors };
    }
  }
  if (Array.isArray($type)) {
    let length = $type.length;
    let valid = false;
    for (let i = 0; i < length; i++) {
      if (instanceType === $type[i] || $type[i] === "integer" && instanceType === "number" && instance % 1 === 0 && instance === instance) {
        valid = true;
        break;
      }
    }
    if (!valid) {
      errors.push({
        instanceLocation,
        keyword: "type",
        keywordLocation: `${schemaLocation}/type`,
        error: `Instance type "${instanceType}" is invalid. Expected "${$type.join('", "')}".`
      });
    }
  } else if ($type === "integer") {
    if (instanceType !== "number" || instance % 1 || instance !== instance) {
      errors.push({
        instanceLocation,
        keyword: "type",
        keywordLocation: `${schemaLocation}/type`,
        error: `Instance type "${instanceType}" is invalid. Expected "${$type}".`
      });
    }
  } else if ($type !== void 0 && instanceType !== $type) {
    errors.push({
      instanceLocation,
      keyword: "type",
      keywordLocation: `${schemaLocation}/type`,
      error: `Instance type "${instanceType}" is invalid. Expected "${$type}".`
    });
  }
  if ($const !== void 0) {
    if (instanceType === "object" || instanceType === "array") {
      if (!deepCompareStrict(instance, $const)) {
        errors.push({
          instanceLocation,
          keyword: "const",
          keywordLocation: `${schemaLocation}/const`,
          error: `Instance does not match ${JSON.stringify($const)}.`
        });
      }
    } else if (instance !== $const) {
      errors.push({
        instanceLocation,
        keyword: "const",
        keywordLocation: `${schemaLocation}/const`,
        error: `Instance does not match ${JSON.stringify($const)}.`
      });
    }
  }
  if ($enum !== void 0) {
    if (instanceType === "object" || instanceType === "array") {
      if (!$enum.some((value) => deepCompareStrict(instance, value))) {
        errors.push({
          instanceLocation,
          keyword: "enum",
          keywordLocation: `${schemaLocation}/enum`,
          error: `Instance does not match any of ${JSON.stringify($enum)}.`
        });
      }
    } else if (!$enum.some((value) => instance === value)) {
      errors.push({
        instanceLocation,
        keyword: "enum",
        keywordLocation: `${schemaLocation}/enum`,
        error: `Instance does not match any of ${JSON.stringify($enum)}.`
      });
    }
  }
  if ($not !== void 0) {
    const keywordLocation = `${schemaLocation}/not`;
    const result = validate(instance, $not, draft, lookup, shortCircuit, recursiveAnchor, instanceLocation, keywordLocation);
    if (result.valid) {
      errors.push({
        instanceLocation,
        keyword: "not",
        keywordLocation,
        error: 'Instance matched "not" schema.'
      });
    }
  }
  let subEvaluateds = [];
  if ($anyOf !== void 0) {
    const keywordLocation = `${schemaLocation}/anyOf`;
    const errorsLength = errors.length;
    let anyValid = false;
    for (let i = 0; i < $anyOf.length; i++) {
      const subSchema = $anyOf[i];
      const subEvaluated = Object.create(evaluated);
      const result = validate(instance, subSchema, draft, lookup, shortCircuit, $recursiveAnchor === true ? recursiveAnchor : null, instanceLocation, `${keywordLocation}/${i}`, subEvaluated);
      errors.push(...result.errors);
      anyValid = anyValid || result.valid;
      if (result.valid) {
        subEvaluateds.push(subEvaluated);
      }
    }
    if (anyValid) {
      errors.length = errorsLength;
    } else {
      errors.splice(errorsLength, 0, {
        instanceLocation,
        keyword: "anyOf",
        keywordLocation,
        error: "Instance does not match any subschemas."
      });
    }
  }
  if ($allOf !== void 0) {
    const keywordLocation = `${schemaLocation}/allOf`;
    const errorsLength = errors.length;
    let allValid = true;
    for (let i = 0; i < $allOf.length; i++) {
      const subSchema = $allOf[i];
      const subEvaluated = Object.create(evaluated);
      const result = validate(instance, subSchema, draft, lookup, shortCircuit, $recursiveAnchor === true ? recursiveAnchor : null, instanceLocation, `${keywordLocation}/${i}`, subEvaluated);
      errors.push(...result.errors);
      allValid = allValid && result.valid;
      if (result.valid) {
        subEvaluateds.push(subEvaluated);
      }
    }
    if (allValid) {
      errors.length = errorsLength;
    } else {
      errors.splice(errorsLength, 0, {
        instanceLocation,
        keyword: "allOf",
        keywordLocation,
        error: `Instance does not match every subschema.`
      });
    }
  }
  if ($oneOf !== void 0) {
    const keywordLocation = `${schemaLocation}/oneOf`;
    const errorsLength = errors.length;
    const matches = $oneOf.filter((subSchema, i) => {
      const subEvaluated = Object.create(evaluated);
      const result = validate(instance, subSchema, draft, lookup, shortCircuit, $recursiveAnchor === true ? recursiveAnchor : null, instanceLocation, `${keywordLocation}/${i}`, subEvaluated);
      errors.push(...result.errors);
      if (result.valid) {
        subEvaluateds.push(subEvaluated);
      }
      return result.valid;
    }).length;
    if (matches === 1) {
      errors.length = errorsLength;
    } else {
      errors.splice(errorsLength, 0, {
        instanceLocation,
        keyword: "oneOf",
        keywordLocation,
        error: `Instance does not match exactly one subschema (${matches} matches).`
      });
    }
  }
  if (instanceType === "object" || instanceType === "array") {
    Object.assign(evaluated, ...subEvaluateds);
  }
  if ($if !== void 0) {
    const keywordLocation = `${schemaLocation}/if`;
    const conditionResult = validate(instance, $if, draft, lookup, shortCircuit, recursiveAnchor, instanceLocation, keywordLocation, evaluated).valid;
    if (conditionResult) {
      if ($then !== void 0) {
        const thenResult = validate(instance, $then, draft, lookup, shortCircuit, recursiveAnchor, instanceLocation, `${schemaLocation}/then`, evaluated);
        if (!thenResult.valid) {
          errors.push({
            instanceLocation,
            keyword: "if",
            keywordLocation,
            error: `Instance does not match "then" schema.`
          }, ...thenResult.errors);
        }
      }
    } else if ($else !== void 0) {
      const elseResult = validate(instance, $else, draft, lookup, shortCircuit, recursiveAnchor, instanceLocation, `${schemaLocation}/else`, evaluated);
      if (!elseResult.valid) {
        errors.push({
          instanceLocation,
          keyword: "if",
          keywordLocation,
          error: `Instance does not match "else" schema.`
        }, ...elseResult.errors);
      }
    }
  }
  if (instanceType === "object") {
    if ($required !== void 0) {
      for (const key of $required) {
        if (!(key in instance)) {
          errors.push({
            instanceLocation,
            keyword: "required",
            keywordLocation: `${schemaLocation}/required`,
            error: `Instance does not have required property "${key}".`
          });
        }
      }
    }
    const keys = Object.keys(instance);
    if ($minProperties !== void 0 && keys.length < $minProperties) {
      errors.push({
        instanceLocation,
        keyword: "minProperties",
        keywordLocation: `${schemaLocation}/minProperties`,
        error: `Instance does not have at least ${$minProperties} properties.`
      });
    }
    if ($maxProperties !== void 0 && keys.length > $maxProperties) {
      errors.push({
        instanceLocation,
        keyword: "maxProperties",
        keywordLocation: `${schemaLocation}/maxProperties`,
        error: `Instance does not have at least ${$maxProperties} properties.`
      });
    }
    if ($propertyNames !== void 0) {
      const keywordLocation = `${schemaLocation}/propertyNames`;
      for (const key in instance) {
        const subInstancePointer = `${instanceLocation}/${encodePointer(key)}`;
        const result = validate(key, $propertyNames, draft, lookup, shortCircuit, recursiveAnchor, subInstancePointer, keywordLocation);
        if (!result.valid) {
          errors.push({
            instanceLocation,
            keyword: "propertyNames",
            keywordLocation,
            error: `Property name "${key}" does not match schema.`
          }, ...result.errors);
        }
      }
    }
    if ($dependentRequired !== void 0) {
      const keywordLocation = `${schemaLocation}/dependantRequired`;
      for (const key in $dependentRequired) {
        if (key in instance) {
          const required = $dependentRequired[key];
          for (const dependantKey of required) {
            if (!(dependantKey in instance)) {
              errors.push({
                instanceLocation,
                keyword: "dependentRequired",
                keywordLocation,
                error: `Instance has "${key}" but does not have "${dependantKey}".`
              });
            }
          }
        }
      }
    }
    if ($dependentSchemas !== void 0) {
      for (const key in $dependentSchemas) {
        const keywordLocation = `${schemaLocation}/dependentSchemas`;
        if (key in instance) {
          const result = validate(instance, $dependentSchemas[key], draft, lookup, shortCircuit, recursiveAnchor, instanceLocation, `${keywordLocation}/${encodePointer(key)}`, evaluated);
          if (!result.valid) {
            errors.push({
              instanceLocation,
              keyword: "dependentSchemas",
              keywordLocation,
              error: `Instance has "${key}" but does not match dependant schema.`
            }, ...result.errors);
          }
        }
      }
    }
    if ($dependencies !== void 0) {
      const keywordLocation = `${schemaLocation}/dependencies`;
      for (const key in $dependencies) {
        if (key in instance) {
          const propsOrSchema = $dependencies[key];
          if (Array.isArray(propsOrSchema)) {
            for (const dependantKey of propsOrSchema) {
              if (!(dependantKey in instance)) {
                errors.push({
                  instanceLocation,
                  keyword: "dependencies",
                  keywordLocation,
                  error: `Instance has "${key}" but does not have "${dependantKey}".`
                });
              }
            }
          } else {
            const result = validate(instance, propsOrSchema, draft, lookup, shortCircuit, recursiveAnchor, instanceLocation, `${keywordLocation}/${encodePointer(key)}`);
            if (!result.valid) {
              errors.push({
                instanceLocation,
                keyword: "dependencies",
                keywordLocation,
                error: `Instance has "${key}" but does not match dependant schema.`
              }, ...result.errors);
            }
          }
        }
      }
    }
    const thisEvaluated = /* @__PURE__ */ Object.create(null);
    let stop = false;
    if ($properties !== void 0) {
      const keywordLocation = `${schemaLocation}/properties`;
      for (const key in $properties) {
        if (!(key in instance)) {
          continue;
        }
        const subInstancePointer = `${instanceLocation}/${encodePointer(key)}`;
        const result = validate(instance[key], $properties[key], draft, lookup, shortCircuit, recursiveAnchor, subInstancePointer, `${keywordLocation}/${encodePointer(key)}`);
        if (result.valid) {
          evaluated[key] = thisEvaluated[key] = true;
        } else {
          stop = shortCircuit;
          errors.push({
            instanceLocation,
            keyword: "properties",
            keywordLocation,
            error: `Property "${key}" does not match schema.`
          }, ...result.errors);
          if (stop)
            break;
        }
      }
    }
    if (!stop && $patternProperties !== void 0) {
      const keywordLocation = `${schemaLocation}/patternProperties`;
      for (const pattern in $patternProperties) {
        const regex2 = new RegExp(pattern, "u");
        const subSchema = $patternProperties[pattern];
        for (const key in instance) {
          if (!regex2.test(key)) {
            continue;
          }
          const subInstancePointer = `${instanceLocation}/${encodePointer(key)}`;
          const result = validate(instance[key], subSchema, draft, lookup, shortCircuit, recursiveAnchor, subInstancePointer, `${keywordLocation}/${encodePointer(pattern)}`);
          if (result.valid) {
            evaluated[key] = thisEvaluated[key] = true;
          } else {
            stop = shortCircuit;
            errors.push({
              instanceLocation,
              keyword: "patternProperties",
              keywordLocation,
              error: `Property "${key}" matches pattern "${pattern}" but does not match associated schema.`
            }, ...result.errors);
          }
        }
      }
    }
    if (!stop && $additionalProperties !== void 0) {
      const keywordLocation = `${schemaLocation}/additionalProperties`;
      for (const key in instance) {
        if (thisEvaluated[key]) {
          continue;
        }
        const subInstancePointer = `${instanceLocation}/${encodePointer(key)}`;
        const result = validate(instance[key], $additionalProperties, draft, lookup, shortCircuit, recursiveAnchor, subInstancePointer, keywordLocation);
        if (result.valid) {
          evaluated[key] = true;
        } else {
          stop = shortCircuit;
          errors.push({
            instanceLocation,
            keyword: "additionalProperties",
            keywordLocation,
            error: `Property "${key}" does not match additional properties schema.`
          }, ...result.errors);
        }
      }
    } else if (!stop && $unevaluatedProperties !== void 0) {
      const keywordLocation = `${schemaLocation}/unevaluatedProperties`;
      for (const key in instance) {
        if (!evaluated[key]) {
          const subInstancePointer = `${instanceLocation}/${encodePointer(key)}`;
          const result = validate(instance[key], $unevaluatedProperties, draft, lookup, shortCircuit, recursiveAnchor, subInstancePointer, keywordLocation);
          if (result.valid) {
            evaluated[key] = true;
          } else {
            errors.push({
              instanceLocation,
              keyword: "unevaluatedProperties",
              keywordLocation,
              error: `Property "${key}" does not match unevaluated properties schema.`
            }, ...result.errors);
          }
        }
      }
    }
  } else if (instanceType === "array") {
    if ($maxItems !== void 0 && instance.length > $maxItems) {
      errors.push({
        instanceLocation,
        keyword: "maxItems",
        keywordLocation: `${schemaLocation}/maxItems`,
        error: `Array has too many items (${instance.length} > ${$maxItems}).`
      });
    }
    if ($minItems !== void 0 && instance.length < $minItems) {
      errors.push({
        instanceLocation,
        keyword: "minItems",
        keywordLocation: `${schemaLocation}/minItems`,
        error: `Array has too few items (${instance.length} < ${$minItems}).`
      });
    }
    const length = instance.length;
    let i = 0;
    let stop = false;
    if ($prefixItems !== void 0) {
      const keywordLocation = `${schemaLocation}/prefixItems`;
      const length2 = Math.min($prefixItems.length, length);
      for (; i < length2; i++) {
        const result = validate(instance[i], $prefixItems[i], draft, lookup, shortCircuit, recursiveAnchor, `${instanceLocation}/${i}`, `${keywordLocation}/${i}`);
        evaluated[i] = true;
        if (!result.valid) {
          stop = shortCircuit;
          errors.push({
            instanceLocation,
            keyword: "prefixItems",
            keywordLocation,
            error: `Items did not match schema.`
          }, ...result.errors);
          if (stop)
            break;
        }
      }
    }
    if ($items !== void 0) {
      const keywordLocation = `${schemaLocation}/items`;
      if (Array.isArray($items)) {
        const length2 = Math.min($items.length, length);
        for (; i < length2; i++) {
          const result = validate(instance[i], $items[i], draft, lookup, shortCircuit, recursiveAnchor, `${instanceLocation}/${i}`, `${keywordLocation}/${i}`);
          evaluated[i] = true;
          if (!result.valid) {
            stop = shortCircuit;
            errors.push({
              instanceLocation,
              keyword: "items",
              keywordLocation,
              error: `Items did not match schema.`
            }, ...result.errors);
            if (stop)
              break;
          }
        }
      } else {
        for (; i < length; i++) {
          const result = validate(instance[i], $items, draft, lookup, shortCircuit, recursiveAnchor, `${instanceLocation}/${i}`, keywordLocation);
          evaluated[i] = true;
          if (!result.valid) {
            stop = shortCircuit;
            errors.push({
              instanceLocation,
              keyword: "items",
              keywordLocation,
              error: `Items did not match schema.`
            }, ...result.errors);
            if (stop)
              break;
          }
        }
      }
      if (!stop && $additionalItems !== void 0) {
        const keywordLocation2 = `${schemaLocation}/additionalItems`;
        for (; i < length; i++) {
          const result = validate(instance[i], $additionalItems, draft, lookup, shortCircuit, recursiveAnchor, `${instanceLocation}/${i}`, keywordLocation2);
          evaluated[i] = true;
          if (!result.valid) {
            stop = shortCircuit;
            errors.push({
              instanceLocation,
              keyword: "additionalItems",
              keywordLocation: keywordLocation2,
              error: `Items did not match additional items schema.`
            }, ...result.errors);
          }
        }
      }
    }
    if ($contains !== void 0) {
      if (length === 0 && $minContains === void 0) {
        errors.push({
          instanceLocation,
          keyword: "contains",
          keywordLocation: `${schemaLocation}/contains`,
          error: `Array is empty. It must contain at least one item matching the schema.`
        });
      } else if ($minContains !== void 0 && length < $minContains) {
        errors.push({
          instanceLocation,
          keyword: "minContains",
          keywordLocation: `${schemaLocation}/minContains`,
          error: `Array has less items (${length}) than minContains (${$minContains}).`
        });
      } else {
        const keywordLocation = `${schemaLocation}/contains`;
        const errorsLength = errors.length;
        let contained = 0;
        for (let j = 0; j < length; j++) {
          const result = validate(instance[j], $contains, draft, lookup, shortCircuit, recursiveAnchor, `${instanceLocation}/${j}`, keywordLocation);
          if (result.valid) {
            evaluated[j] = true;
            contained++;
          } else {
            errors.push(...result.errors);
          }
        }
        if (contained >= ($minContains || 0)) {
          errors.length = errorsLength;
        }
        if ($minContains === void 0 && $maxContains === void 0 && contained === 0) {
          errors.splice(errorsLength, 0, {
            instanceLocation,
            keyword: "contains",
            keywordLocation,
            error: `Array does not contain item matching schema.`
          });
        } else if ($minContains !== void 0 && contained < $minContains) {
          errors.push({
            instanceLocation,
            keyword: "minContains",
            keywordLocation: `${schemaLocation}/minContains`,
            error: `Array must contain at least ${$minContains} items matching schema. Only ${contained} items were found.`
          });
        } else if ($maxContains !== void 0 && contained > $maxContains) {
          errors.push({
            instanceLocation,
            keyword: "maxContains",
            keywordLocation: `${schemaLocation}/maxContains`,
            error: `Array may contain at most ${$maxContains} items matching schema. ${contained} items were found.`
          });
        }
      }
    }
    if (!stop && $unevaluatedItems !== void 0) {
      const keywordLocation = `${schemaLocation}/unevaluatedItems`;
      for (i; i < length; i++) {
        if (evaluated[i]) {
          continue;
        }
        const result = validate(instance[i], $unevaluatedItems, draft, lookup, shortCircuit, recursiveAnchor, `${instanceLocation}/${i}`, keywordLocation);
        evaluated[i] = true;
        if (!result.valid) {
          errors.push({
            instanceLocation,
            keyword: "unevaluatedItems",
            keywordLocation,
            error: `Items did not match unevaluated items schema.`
          }, ...result.errors);
        }
      }
    }
    if ($uniqueItems) {
      for (let j = 0; j < length; j++) {
        const a = instance[j];
        const ao = typeof a === "object" && a !== null;
        for (let k = 0; k < length; k++) {
          if (j === k) {
            continue;
          }
          const b = instance[k];
          const bo = typeof b === "object" && b !== null;
          if (a === b || ao && bo && deepCompareStrict(a, b)) {
            errors.push({
              instanceLocation,
              keyword: "uniqueItems",
              keywordLocation: `${schemaLocation}/uniqueItems`,
              error: `Duplicate items at indexes ${j} and ${k}.`
            });
            j = Number.MAX_SAFE_INTEGER;
            k = Number.MAX_SAFE_INTEGER;
          }
        }
      }
    }
  } else if (instanceType === "number") {
    if (draft === "4") {
      if ($minimum !== void 0 && ($exclusiveMinimum === true && instance <= $minimum || instance < $minimum)) {
        errors.push({
          instanceLocation,
          keyword: "minimum",
          keywordLocation: `${schemaLocation}/minimum`,
          error: `${instance} is less than ${$exclusiveMinimum ? "or equal to " : ""} ${$minimum}.`
        });
      }
      if ($maximum !== void 0 && ($exclusiveMaximum === true && instance >= $maximum || instance > $maximum)) {
        errors.push({
          instanceLocation,
          keyword: "maximum",
          keywordLocation: `${schemaLocation}/maximum`,
          error: `${instance} is greater than ${$exclusiveMaximum ? "or equal to " : ""} ${$maximum}.`
        });
      }
    } else {
      if ($minimum !== void 0 && instance < $minimum) {
        errors.push({
          instanceLocation,
          keyword: "minimum",
          keywordLocation: `${schemaLocation}/minimum`,
          error: `${instance} is less than ${$minimum}.`
        });
      }
      if ($maximum !== void 0 && instance > $maximum) {
        errors.push({
          instanceLocation,
          keyword: "maximum",
          keywordLocation: `${schemaLocation}/maximum`,
          error: `${instance} is greater than ${$maximum}.`
        });
      }
      if ($exclusiveMinimum !== void 0 && instance <= $exclusiveMinimum) {
        errors.push({
          instanceLocation,
          keyword: "exclusiveMinimum",
          keywordLocation: `${schemaLocation}/exclusiveMinimum`,
          error: `${instance} is less than ${$exclusiveMinimum}.`
        });
      }
      if ($exclusiveMaximum !== void 0 && instance >= $exclusiveMaximum) {
        errors.push({
          instanceLocation,
          keyword: "exclusiveMaximum",
          keywordLocation: `${schemaLocation}/exclusiveMaximum`,
          error: `${instance} is greater than or equal to ${$exclusiveMaximum}.`
        });
      }
    }
    if ($multipleOf !== void 0) {
      const remainder = instance % $multipleOf;
      if (Math.abs(0 - remainder) >= 11920929e-14 && Math.abs($multipleOf - remainder) >= 11920929e-14) {
        errors.push({
          instanceLocation,
          keyword: "multipleOf",
          keywordLocation: `${schemaLocation}/multipleOf`,
          error: `${instance} is not a multiple of ${$multipleOf}.`
        });
      }
    }
  } else if (instanceType === "string") {
    const length = $minLength === void 0 && $maxLength === void 0 ? 0 : ucs2length(instance);
    if ($minLength !== void 0 && length < $minLength) {
      errors.push({
        instanceLocation,
        keyword: "minLength",
        keywordLocation: `${schemaLocation}/minLength`,
        error: `String is too short (${length} < ${$minLength}).`
      });
    }
    if ($maxLength !== void 0 && length > $maxLength) {
      errors.push({
        instanceLocation,
        keyword: "maxLength",
        keywordLocation: `${schemaLocation}/maxLength`,
        error: `String is too long (${length} > ${$maxLength}).`
      });
    }
    if ($pattern !== void 0 && !new RegExp($pattern, "u").test(instance)) {
      errors.push({
        instanceLocation,
        keyword: "pattern",
        keywordLocation: `${schemaLocation}/pattern`,
        error: `String does not match pattern.`
      });
    }
    if ($format !== void 0 && format[$format] && !format[$format](instance)) {
      errors.push({
        instanceLocation,
        keyword: "format",
        keywordLocation: `${schemaLocation}/format`,
        error: `String does not match format "${$format}".`
      });
    }
  }
  return { valid: errors.length === 0, errors };
}
__name(validate, "validate");

// node_modules/@cfworker/json-schema/dist/esm/validator.js
var Validator = class {
  static {
    __name(this, "Validator");
  }
  schema;
  draft;
  shortCircuit;
  lookup;
  constructor(schema, draft = "2019-09", shortCircuit = true) {
    this.schema = schema;
    this.draft = draft;
    this.shortCircuit = shortCircuit;
    this.lookup = dereference(schema);
  }
  validate(instance) {
    return validate(instance, this.schema, this.draft, this.lookup, this.shortCircuit);
  }
  addSchema(schema, id) {
    if (id) {
      schema = { ...schema, $id: id };
    }
    dereference(schema, this.lookup);
  }
};

// ../shared/schema/project.schema.json
var project_schema_default = {
  $schema: "http://json-schema.org/draft-07/schema#",
  $id: "https://sharedaw.local/schema/project.schema.json",
  title: "ShareDAW project",
  description: "\u30D7\u30ED\u30B8\u30A7\u30AF\u30C8JSON\uFF08\u6B63\uFF09\u3002\u4ED5\u69D8\u66F8 \xA77 \u3092\u53C2\u7167\u3002",
  type: "object",
  required: [
    "schemaVersion",
    "projectId",
    "name",
    "sampleRate",
    "ppq",
    "tempoTrack",
    "meterTrack",
    "chordTrack",
    "tracks"
  ],
  additionalProperties: false,
  properties: {
    schemaVersion: {
      const: 1
    },
    projectId: {
      $ref: "#/definitions/uuid"
    },
    name: {
      type: "string"
    },
    sampleRate: {
      const: 48e3
    },
    ppq: {
      const: 960
    },
    tempoTrack: {
      type: "object",
      required: [
        "id",
        "events"
      ],
      additionalProperties: false,
      properties: {
        id: {
          $ref: "#/definitions/uuid"
        },
        events: {
          type: "array",
          minItems: 1,
          items: {
            type: "object",
            required: [
              "id",
              "tick",
              "bpm"
            ],
            additionalProperties: false,
            properties: {
              id: {
                $ref: "#/definitions/uuid"
              },
              tick: {
                $ref: "#/definitions/tick"
              },
              bpm: {
                type: "number",
                minimum: 10,
                maximum: 999
              }
            }
          }
        }
      }
    },
    meterTrack: {
      type: "object",
      required: [
        "id",
        "events"
      ],
      additionalProperties: false,
      properties: {
        id: {
          $ref: "#/definitions/uuid"
        },
        events: {
          type: "array",
          minItems: 1,
          items: {
            type: "object",
            required: [
              "id",
              "bar",
              "numerator",
              "denominator"
            ],
            additionalProperties: false,
            properties: {
              id: {
                $ref: "#/definitions/uuid"
              },
              bar: {
                type: "integer",
                minimum: 1
              },
              numerator: {
                type: "integer",
                minimum: 1,
                maximum: 64
              },
              denominator: {
                enum: [
                  1,
                  2,
                  4,
                  8,
                  16,
                  32
                ]
              }
            }
          }
        }
      }
    },
    chordTrack: {
      type: "object",
      required: [
        "id",
        "playback",
        "events"
      ],
      additionalProperties: false,
      properties: {
        id: {
          $ref: "#/definitions/uuid"
        },
        playback: {
          type: "object",
          required: [
            "enabled",
            "volumeDb",
            "instrument"
          ],
          additionalProperties: false,
          properties: {
            enabled: {
              type: "boolean"
            },
            volumeDb: {
              type: "number"
            },
            instrument: {
              $ref: "#/definitions/instrumentRef"
            }
          }
        },
        events: {
          type: "array",
          items: {
            type: "object",
            required: [
              "id",
              "tick"
            ],
            additionalProperties: false,
            properties: {
              id: {
                $ref: "#/definitions/uuid"
              },
              tick: {
                $ref: "#/definitions/tick"
              },
              noChord: {
                type: "boolean"
              },
              text: {
                type: "string"
              },
              chord: {
                type: "object",
                required: [
                  "root",
                  "quality",
                  "tensions",
                  "bass"
                ],
                additionalProperties: false,
                properties: {
                  root: {
                    $ref: "#/definitions/pitchName"
                  },
                  quality: {
                    type: "string"
                  },
                  tensions: {
                    type: "array",
                    items: {
                      type: "string"
                    }
                  },
                  bass: {
                    anyOf: [
                      {
                        $ref: "#/definitions/pitchName"
                      },
                      {
                        type: "null"
                      }
                    ]
                  }
                }
              }
            }
          }
        }
      }
    },
    tracks: {
      type: "array",
      items: {
        $ref: "#/definitions/track"
      }
    }
  },
  definitions: {
    uuid: {
      type: "string",
      pattern: "^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$"
    },
    sha256: {
      type: "string",
      pattern: "^[0-9a-f]{64}$"
    },
    tick: {
      type: "integer",
      minimum: 0
    },
    pitchName: {
      type: "string",
      pattern: "^[A-G](#|b)?$"
    },
    color: {
      type: "string",
      pattern: "^#[0-9A-Fa-f]{6}$"
    },
    instrumentRef: {
      type: "object",
      required: [
        "id",
        "version"
      ],
      additionalProperties: false,
      properties: {
        id: {
          type: "string",
          pattern: "^builtin\\.[a-z0-9_]+$"
        },
        version: {
          type: "string",
          pattern: "^[0-9]+\\.[0-9]+\\.[0-9]+$"
        }
      }
    },
    externalPlugin: {
      type: "object",
      required: [
        "format",
        "name",
        "vendor",
        "uid",
        "os"
      ],
      additionalProperties: false,
      properties: {
        format: {
          enum: [
            "VST3",
            "AU"
          ]
        },
        name: {
          type: "string"
        },
        vendor: {
          type: "string"
        },
        uid: {
          type: "string"
        },
        os: {
          enum: [
            "windows",
            "mac",
            "linux"
          ]
        }
      }
    },
    instrument: {
      oneOf: [
        {
          type: "object",
          required: [
            "kind",
            "id",
            "version",
            "params"
          ],
          additionalProperties: false,
          properties: {
            kind: {
              const: "builtin"
            },
            id: {
              type: "string",
              pattern: "^builtin\\.[a-z0-9_]+$"
            },
            version: {
              type: "string",
              pattern: "^[0-9]+\\.[0-9]+\\.[0-9]+$"
            },
            params: {
              type: "object"
            }
          }
        },
        {
          type: "object",
          required: [
            "kind",
            "plugin"
          ],
          additionalProperties: false,
          properties: {
            kind: {
              const: "external"
            },
            plugin: {
              $ref: "#/definitions/externalPlugin"
            },
            stateRef: {
              type: "string",
              pattern: '^plugins-state/[^\\\\:*?"<>|]+$'
            }
          }
        }
      ]
    },
    effect: {
      type: "object",
      required: [
        "id",
        "plugin"
      ],
      additionalProperties: false,
      properties: {
        id: {
          $ref: "#/definitions/uuid"
        },
        plugin: {
          $ref: "#/definitions/externalPlugin"
        },
        stateRef: {
          type: "string",
          pattern: '^plugins-state/[^\\\\:*?"<>|]+$'
        },
        bypass: {
          type: "boolean"
        }
      }
    },
    render: {
      type: "object",
      required: [
        "audioHash",
        "renderedAt",
        "sourceFingerprint",
        "tailSeconds"
      ],
      additionalProperties: false,
      properties: {
        audioHash: {
          $ref: "#/definitions/sha256"
        },
        renderedAt: {
          type: "string",
          pattern: "^\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2}(\\.\\d+)?Z$"
        },
        sourceFingerprint: {
          $ref: "#/definitions/sha256"
        },
        tailSeconds: {
          type: "number",
          minimum: 0
        }
      }
    },
    note: {
      type: "object",
      required: [
        "id",
        "tick",
        "lengthTick",
        "pitch",
        "velocity"
      ],
      additionalProperties: false,
      properties: {
        id: {
          $ref: "#/definitions/uuid"
        },
        tick: {
          $ref: "#/definitions/tick"
        },
        lengthTick: {
          type: "integer",
          minimum: 1
        },
        pitch: {
          type: "integer",
          minimum: 0,
          maximum: 127
        },
        velocity: {
          type: "integer",
          minimum: 1,
          maximum: 127
        }
      }
    },
    midiClip: {
      type: "object",
      required: [
        "id",
        "startTick",
        "lengthTick",
        "notes"
      ],
      additionalProperties: false,
      properties: {
        id: {
          $ref: "#/definitions/uuid"
        },
        startTick: {
          $ref: "#/definitions/tick"
        },
        lengthTick: {
          type: "integer",
          minimum: 1
        },
        notes: {
          type: "array",
          items: {
            $ref: "#/definitions/note"
          }
        }
      }
    },
    audioClip: {
      type: "object",
      required: [
        "id",
        "startTick",
        "audioHash",
        "displayName",
        "sourceOffsetSamples",
        "lengthSamples",
        "gainDb",
        "fadeInSamples",
        "fadeOutSamples"
      ],
      additionalProperties: false,
      properties: {
        id: {
          $ref: "#/definitions/uuid"
        },
        startTick: {
          $ref: "#/definitions/tick"
        },
        audioHash: {
          $ref: "#/definitions/sha256"
        },
        displayName: {
          type: "string"
        },
        sourceOffsetSamples: {
          type: "integer",
          minimum: 0
        },
        lengthSamples: {
          type: "integer",
          minimum: 1
        },
        gainDb: {
          type: "number"
        },
        fadeInSamples: {
          type: "integer",
          minimum: 0
        },
        fadeOutSamples: {
          type: "integer",
          minimum: 0
        }
      }
    },
    track: {
      oneOf: [
        {
          type: "object",
          required: [
            "id",
            "type",
            "name",
            "color",
            "volumeDb",
            "pan",
            "mute",
            "solo",
            "instrument",
            "clips"
          ],
          additionalProperties: false,
          properties: {
            id: {
              $ref: "#/definitions/uuid"
            },
            type: {
              const: "midi"
            },
            name: {
              type: "string"
            },
            color: {
              $ref: "#/definitions/color"
            },
            volumeDb: {
              type: "number"
            },
            pan: {
              type: "number",
              minimum: -1,
              maximum: 1
            },
            mute: {
              type: "boolean"
            },
            solo: {
              type: "boolean"
            },
            instrument: {
              $ref: "#/definitions/instrument"
            },
            effects: {
              type: "array",
              items: {
                $ref: "#/definitions/effect"
              }
            },
            strip: {
              $ref: "#/definitions/channelStrip"
            },
            render: {
              $ref: "#/definitions/render"
            },
            clips: {
              type: "array",
              items: {
                $ref: "#/definitions/midiClip"
              }
            }
          }
        },
        {
          type: "object",
          required: [
            "id",
            "type",
            "name",
            "color",
            "volumeDb",
            "pan",
            "mute",
            "solo",
            "clips"
          ],
          additionalProperties: false,
          properties: {
            id: {
              $ref: "#/definitions/uuid"
            },
            type: {
              const: "audio"
            },
            name: {
              type: "string"
            },
            color: {
              $ref: "#/definitions/color"
            },
            volumeDb: {
              type: "number"
            },
            pan: {
              type: "number",
              minimum: -1,
              maximum: 1
            },
            mute: {
              type: "boolean"
            },
            solo: {
              type: "boolean"
            },
            effects: {
              type: "array",
              items: {
                $ref: "#/definitions/effect"
              }
            },
            strip: {
              $ref: "#/definitions/channelStrip"
            },
            render: {
              $ref: "#/definitions/render"
            },
            clips: {
              type: "array",
              items: {
                $ref: "#/definitions/audioClip"
              }
            }
          }
        }
      ]
    },
    channelStrip: {
      type: "object",
      additionalProperties: false,
      properties: {
        eq: {
          type: "object",
          additionalProperties: false,
          properties: {
            enabled: {
              type: "boolean"
            },
            lowCutHz: {
              type: "number",
              minimum: 0,
              maximum: 1e3
            },
            lowGainDb: {
              type: "number",
              minimum: -24,
              maximum: 24
            },
            lowFreqHz: {
              type: "number",
              minimum: 20,
              maximum: 1e3
            },
            midGainDb: {
              type: "number",
              minimum: -24,
              maximum: 24
            },
            midFreqHz: {
              type: "number",
              minimum: 100,
              maximum: 16e3
            },
            midQ: {
              type: "number",
              minimum: 0.1,
              maximum: 10
            },
            highGainDb: {
              type: "number",
              minimum: -24,
              maximum: 24
            },
            highFreqHz: {
              type: "number",
              minimum: 1e3,
              maximum: 2e4
            }
          }
        },
        comp: {
          type: "object",
          additionalProperties: false,
          properties: {
            enabled: {
              type: "boolean"
            },
            type: {
              enum: [
                "fet",
                "opto"
              ]
            },
            thresholdDb: {
              type: "number",
              minimum: -60,
              maximum: 0
            },
            ratio: {
              type: "number",
              minimum: 1,
              maximum: 100
            },
            attackMs: {
              type: "number",
              minimum: 0.01,
              maximum: 500
            },
            releaseMs: {
              type: "number",
              minimum: 1,
              maximum: 5e3
            },
            makeupDb: {
              type: "number",
              minimum: -24,
              maximum: 24
            }
          }
        }
      }
    }
  }
};

// src/project.ts
var validator = new Validator(project_schema_default, "7", false);
function validateProject(data) {
  const result = validator.validate(data);
  if (result.valid) return null;
  const first = result.errors[result.errors.length - 1];
  return first ? `${first.instanceLocation}: ${first.error}` : "invalid";
}
__name(validateProject, "validateProject");
function canonical(value) {
  if (Array.isArray(value)) return `[${value.map(canonical).join(",")}]`;
  if (value !== null && typeof value === "object") {
    const keys = Object.keys(value).sort();
    return `{${keys.map((k) => `${JSON.stringify(k)}:${canonical(value[k])}`).join(",")}}`;
  }
  return JSON.stringify(value);
}
__name(canonical, "canonical");
function scopes(p) {
  const m = /* @__PURE__ */ new Map();
  m.set(p.tempoTrack.id, { kind: "tempo", content: canonical(p.tempoTrack) });
  m.set(p.meterTrack.id, { kind: "meter", content: canonical(p.meterTrack) });
  m.set(p.chordTrack.id, { kind: "chord", content: canonical(p.chordTrack) });
  for (const t of p.tracks) m.set(t.id, { kind: "track", content: canonical(t) });
  return m;
}
__name(scopes, "scopes");
function changedScopes(parent, next) {
  const before = parent ? scopes(parent) : /* @__PURE__ */ new Map();
  const after = scopes(next);
  const result = [];
  for (const [id, s] of after) {
    const b = before.get(id);
    if (!b || b.content !== s.content) result.push({ id, kind: s.kind, existedInParent: !!b, deleted: false });
  }
  for (const [id, s] of before) {
    if (!after.has(id)) result.push({ id, kind: s.kind, existedInParent: true, deleted: true });
  }
  return result;
}
__name(changedScopes, "changedScopes");
function referencedBlobs(p) {
  const hashes = /* @__PURE__ */ new Set();
  for (const t of p.tracks) {
    if (t.render?.audioHash) hashes.add(t.render.audioHash);
    for (const c of t.clips) if (c.audioHash) hashes.add(c.audioHash);
  }
  return [...hashes];
}
__name(referencedBlobs, "referencedBlobs");

// src/index.ts
var routes = [];
function route(method, path, handler) {
  const keys = [];
  const pattern = new RegExp(
    "^" + path.replace(/:([a-zA-Z]+)/g, (_, k) => (keys.push(k), "([^/]+)")) + "$"
  );
  routes.push({ method, pattern, keys, handler });
}
__name(route, "route");
async function authenticate(env, request) {
  const header = request.headers.get("authorization") ?? "";
  const match = /^Bearer\s+(.+)$/i.exec(header);
  if (!match) throw new HttpError(401, "unauthorized", "\u30C8\u30FC\u30AF\u30F3\u304C\u3042\u308A\u307E\u305B\u3093");
  const tokenHash = await sha256Hex(match[1].trim());
  if (env.RELEASE_KEY && tokenHash === await sha256Hex(env.RELEASE_KEY))
    return { id: "release", displayName: "release", isRelease: true };
  const row = await env.DB.prepare("SELECT id, display_name FROM users WHERE token_hash = ?").bind(tokenHash).first();
  if (!row) throw new HttpError(401, "unauthorized", "\u30C8\u30FC\u30AF\u30F3\u304C\u6B63\u3057\u304F\u3042\u308A\u307E\u305B\u3093");
  return { id: row.id, displayName: row.display_name };
}
__name(authenticate, "authenticate");
async function requireMember(ctx, projectId) {
  if (ctx.user.isRelease) throw new HttpError(403, "forbidden", "\u30EA\u30EA\u30FC\u30B9\u7528\u306E\u30AD\u30FC\u3067\u306F\u30D7\u30ED\u30B8\u30A7\u30AF\u30C8\u3092\u64CD\u4F5C\u3067\u304D\u307E\u305B\u3093");
  const project = await ctx.env.DB.prepare(
    `SELECT p.id, p.name, p.head_revision, p.created_by, p.created_at FROM projects p
     JOIN project_members m ON m.project_id = p.id AND m.user_id = ? WHERE p.id = ?`
  ).bind(ctx.user.id, projectId).first();
  if (!project) throw new HttpError(404, "project_not_found", "\u30D7\u30ED\u30B8\u30A7\u30AF\u30C8\u304C\u898B\u3064\u304B\u3089\u306A\u3044\u304B\u3001\u53C2\u52A0\u3057\u3066\u3044\u307E\u305B\u3093");
  return project;
}
__name(requireMember, "requireMember");
async function loadProjectJson(env, hash2) {
  const obj = await env.BLOBS.get(blobKey(hash2));
  if (!obj) throw new HttpError(400, "missing_blobs", "\u30D7\u30ED\u30B8\u30A7\u30AF\u30C8 JSON \u304C\u30A2\u30C3\u30D7\u30ED\u30FC\u30C9\u3055\u308C\u3066\u3044\u307E\u305B\u3093", { hashes: [hash2] });
  try {
    return JSON.parse(await obj.text());
  } catch {
    throw new HttpError(400, "invalid_project", "\u30D7\u30ED\u30B8\u30A7\u30AF\u30C8 JSON \u3092\u8AAD\u307F\u8FBC\u3081\u307E\u305B\u3093");
  }
}
__name(loadProjectJson, "loadProjectJson");
route("GET", "/me", async (ctx) => json({ id: ctx.user.id, displayName: ctx.user.displayName }));
route("GET", "/users", async (ctx) => {
  const rows = await ctx.env.DB.prepare("SELECT id, display_name FROM users ORDER BY display_name").all();
  return json(rows.results.map((r) => ({ id: r.id, displayName: r.display_name })));
});
route("GET", "/projects", async (ctx) => {
  const rows = await ctx.env.DB.prepare(
    `SELECT p.id, p.name, p.head_revision, p.created_by, p.created_at FROM projects p
     JOIN project_members m ON m.project_id = p.id WHERE m.user_id = ? ORDER BY p.created_at DESC`
  ).bind(ctx.user.id).all();
  return json(rows.results.map((p) => ({ id: p.id, name: p.name, headRevision: p.head_revision, createdBy: p.created_by, createdAt: p.created_at })));
});
route("POST", "/projects", async (ctx) => {
  const body = await readJson(ctx.request);
  if (!isUuid(body.id)) throw new HttpError(400, "bad_request", "id\uFF08\u30D7\u30ED\u30B8\u30A7\u30AF\u30C8 JSON \u306E projectId\uFF09\u304C\u5FC5\u8981\u3067\u3059");
  if (typeof body.name !== "string" || !body.name.trim()) throw new HttpError(400, "bad_request", "name \u304C\u5FC5\u8981\u3067\u3059");
  const exists = await ctx.env.DB.prepare("SELECT id FROM projects WHERE id = ?").bind(body.id).first();
  if (exists) throw new HttpError(409, "project_exists", "\u540C\u3058 ID \u306E\u30D7\u30ED\u30B8\u30A7\u30AF\u30C8\u304C\u65E2\u306B\u3042\u308A\u307E\u3059");
  const now = nowIso();
  const members = /* @__PURE__ */ new Set([ctx.user.id, ...body.memberIds ?? []]);
  const statements = [
    ctx.env.DB.prepare("INSERT INTO projects (id, name, head_revision, created_by, created_at) VALUES (?, ?, 0, ?, ?)").bind(
      body.id,
      body.name.trim(),
      ctx.user.id,
      now
    ),
    ...[...members].map((m) => ctx.env.DB.prepare("INSERT OR IGNORE INTO project_members (project_id, user_id) SELECT ?, id FROM users WHERE id = ?").bind(body.id, m))
  ];
  await ctx.env.DB.batch(statements);
  return json({ id: body.id, name: body.name.trim(), headRevision: 0, createdBy: ctx.user.id, createdAt: now }, 201);
});
route("GET", "/projects/:id", async (ctx, { id }) => {
  const p = await requireMember(ctx, id);
  const members = await ctx.env.DB.prepare(
    "SELECT u.id, u.display_name FROM project_members m JOIN users u ON u.id = m.user_id WHERE m.project_id = ? ORDER BY u.display_name"
  ).bind(id).all();
  return json({
    id: p.id,
    name: p.name,
    headRevision: p.head_revision,
    createdBy: p.created_by,
    createdAt: p.created_at,
    members: members.results.map((m) => ({ id: m.id, displayName: m.display_name }))
  });
});
route("POST", "/projects/:id/members", async (ctx, { id }) => {
  await requireMember(ctx, id);
  const body = await readJson(ctx.request);
  const user = await ctx.env.DB.prepare("SELECT id FROM users WHERE id = ?").bind(body.userId ?? "").first();
  if (!user) throw new HttpError(404, "user_not_found", "\u30E6\u30FC\u30B6\u30FC\u304C\u898B\u3064\u304B\u308A\u307E\u305B\u3093");
  await ctx.env.DB.prepare("INSERT OR IGNORE INTO project_members (project_id, user_id) VALUES (?, ?)").bind(id, body.userId).run();
  return json({ ok: true });
});
route("GET", "/projects/:id/revisions", async (ctx, { id }) => {
  await requireMember(ctx, id);
  const rows = await ctx.env.DB.prepare(
    `SELECT r.number, r.parent_number, r.author_id, u.display_name, r.message, r.project_json_hash, r.created_at
     FROM revisions r LEFT JOIN users u ON u.id = r.author_id WHERE r.project_id = ? ORDER BY r.number DESC`
  ).bind(id).all();
  return json(
    rows.results.map((r) => ({
      number: r.number,
      parentNumber: r.parent_number,
      authorId: r.author_id,
      authorName: r.display_name,
      message: r.message,
      projectJsonHash: r.project_json_hash,
      createdAt: r.created_at
    }))
  );
});
route("GET", "/projects/:id/revisions/:n", async (ctx, { id, n }) => {
  await requireMember(ctx, id);
  const row = await ctx.env.DB.prepare("SELECT number, project_json_hash FROM revisions WHERE project_id = ? AND number = ?").bind(id, Number(n)).first();
  if (!row) throw new HttpError(404, "revision_not_found", "\u30EA\u30D3\u30B8\u30E7\u30F3\u304C\u898B\u3064\u304B\u308A\u307E\u305B\u3093");
  const download = await transferUrl(ctx.env, ctx.url.origin, row.project_json_hash, "GET");
  return json({ number: row.number, projectJsonHash: row.project_json_hash, download });
});
route("POST", "/projects/:id/revisions", async (ctx, { id }) => {
  const project = await requireMember(ctx, id);
  const body = await readJson(ctx.request);
  if (body.parentNumber !== project.head_revision) {
    throw new HttpError(409, "not_head", "\u30B5\u30FC\u30D0\u30FC\u306B\u65B0\u3057\u3044\u30EA\u30D3\u30B8\u30E7\u30F3\u304C\u3042\u308A\u307E\u3059\u3002\u5148\u306B\u53D6\u308A\u8FBC\u3093\u3067\u304F\u3060\u3055\u3044", { head: project.head_revision });
  }
  if (!isSha256(body.projectJsonHash)) throw new HttpError(400, "bad_request", "projectJsonHash \u304C\u5FC5\u8981\u3067\u3059");
  const registered = await registeredHashes(ctx.env, [body.projectJsonHash]);
  if (!registered.has(body.projectJsonHash)) {
    throw new HttpError(400, "missing_blobs", "\u30D7\u30ED\u30B8\u30A7\u30AF\u30C8 JSON \u304C\u30A2\u30C3\u30D7\u30ED\u30FC\u30C9\u3055\u308C\u3066\u3044\u307E\u305B\u3093", { hashes: [body.projectJsonHash] });
  }
  const next = await loadProjectJson(ctx.env, body.projectJsonHash);
  const schemaError = validateProject(next);
  if (schemaError) throw new HttpError(400, "invalid_project", `\u30B9\u30AD\u30FC\u30DE\u306B\u9069\u5408\u3057\u307E\u305B\u3093: ${schemaError}`);
  if (next.projectId !== id) throw new HttpError(400, "invalid_project", "projectId \u304C\u4E00\u81F4\u3057\u307E\u305B\u3093");
  const refs = referencedBlobs(next);
  const present = await registeredHashes(ctx.env, refs);
  const missing = refs.filter((h) => !present.has(h));
  if (missing.length > 0) throw new HttpError(400, "missing_blobs", "\u30A2\u30C3\u30D7\u30ED\u30FC\u30C9\u3055\u308C\u3066\u3044\u306A\u3044\u30AA\u30FC\u30C7\u30A3\u30AA\u304C\u3042\u308A\u307E\u3059", { hashes: missing });
  let parent = null;
  if (project.head_revision > 0) {
    const head = await ctx.env.DB.prepare("SELECT project_json_hash FROM revisions WHERE project_id = ? AND number = ?").bind(id, project.head_revision).first();
    if (head) parent = await loadProjectJson(ctx.env, head.project_json_hash);
  }
  const changes = changedScopes(parent, next);
  const locks = await ctx.env.DB.prepare("SELECT track_id, user_id FROM locks WHERE project_id = ?").bind(id).all();
  const holder = new Map(locks.results.map((l) => [l.track_id, l.user_id]));
  const notLocked = changes.filter((c) => c.existedInParent && holder.get(c.id) !== ctx.user.id).map((c) => c.id);
  if (notLocked.length > 0) throw new HttpError(403, "lock_required", "\u30ED\u30C3\u30AF\u3092\u6301\u3063\u3066\u3044\u306A\u3044\u30C8\u30E9\u30C3\u30AF\u304C\u5909\u66F4\u3055\u308C\u3066\u3044\u307E\u3059", { trackIds: notLocked });
  const lockedByOthers = changes.filter((c) => !c.existedInParent && holder.has(c.id) && holder.get(c.id) !== ctx.user.id).map((c) => c.id);
  if (lockedByOthers.length > 0) throw new HttpError(409, "locked", "\u4ED6\u306E\u4EBA\u304C\u30ED\u30C3\u30AF\u3057\u3066\u3044\u308B\u30C8\u30E9\u30C3\u30AF\u304C\u3042\u308A\u307E\u3059", { trackIds: lockedByOthers });
  const number = project.head_revision + 1;
  const now = nowIso();
  const [insert] = await ctx.env.DB.batch([
    ctx.env.DB.prepare(
      `INSERT INTO revisions (project_id, number, parent_number, author_id, message, project_json_hash, created_at)
       SELECT ?, ?, ?, ?, ?, ?, ? WHERE (SELECT head_revision FROM projects WHERE id = ?) = ?`
    ).bind(id, number, project.head_revision, ctx.user.id, body.message ?? "", body.projectJsonHash, now, id, project.head_revision),
    ctx.env.DB.prepare("UPDATE projects SET head_revision = ? WHERE id = ? AND head_revision = ?").bind(number, id, project.head_revision)
  ]);
  if (insert.meta.changes !== 1) {
    throw new HttpError(409, "not_head", "\u30B5\u30FC\u30D0\u30FC\u306B\u65B0\u3057\u3044\u30EA\u30D3\u30B8\u30E7\u30F3\u304C\u3042\u308A\u307E\u3059\u3002\u5148\u306B\u53D6\u308A\u8FBC\u3093\u3067\u304F\u3060\u3055\u3044");
  }
  const lockStatements = [];
  const isFirstRevision = parent === null;
  for (const c of changes) {
    if (c.deleted) {
      lockStatements.push(ctx.env.DB.prepare("DELETE FROM locks WHERE project_id = ? AND track_id = ?").bind(id, c.id));
      continue;
    }
    const release = body.releaseLocks === true || isFirstRevision && c.kind !== "track";
    if (release) {
      if (holder.get(c.id) === ctx.user.id) {
        lockStatements.push(ctx.env.DB.prepare("DELETE FROM locks WHERE project_id = ? AND track_id = ? AND user_id = ?").bind(id, c.id, ctx.user.id));
        lockStatements.push(
          ctx.env.DB.prepare("INSERT INTO lock_events (project_id, track_id, user_id, action, created_at) VALUES (?, ?, ?, 'release', ?)").bind(id, c.id, ctx.user.id, now)
        );
      }
    } else if (!holder.has(c.id)) {
      lockStatements.push(ctx.env.DB.prepare("INSERT OR IGNORE INTO locks (project_id, track_id, user_id, acquired_at) VALUES (?, ?, ?, ?)").bind(id, c.id, ctx.user.id, now));
      lockStatements.push(
        ctx.env.DB.prepare("INSERT INTO lock_events (project_id, track_id, user_id, action, created_at) VALUES (?, ?, ?, 'acquire', ?)").bind(id, c.id, ctx.user.id, now)
      );
    }
  }
  if (lockStatements.length > 0) await ctx.env.DB.batch(lockStatements);
  return json({ number, head: number, changedTrackIds: changes.map((c) => c.id) }, 201);
});
route("GET", "/projects/:id/locks", async (ctx, { id }) => {
  await requireMember(ctx, id);
  const rows = await ctx.env.DB.prepare(
    "SELECT l.track_id, l.user_id, u.display_name, l.acquired_at FROM locks l LEFT JOIN users u ON u.id = l.user_id WHERE l.project_id = ?"
  ).bind(id).all();
  return json(rows.results.map((l) => ({ trackId: l.track_id, userId: l.user_id, displayName: l.display_name, acquiredAt: l.acquired_at })));
});
route("POST", "/projects/:id/locks", async (ctx, { id }) => {
  await requireMember(ctx, id);
  const body = await readJson(ctx.request);
  if (typeof body.trackId !== "string" || !body.trackId) throw new HttpError(400, "bad_request", "trackId \u304C\u5FC5\u8981\u3067\u3059");
  const now = nowIso();
  const inserted = await ctx.env.DB.prepare("INSERT OR IGNORE INTO locks (project_id, track_id, user_id, acquired_at) VALUES (?, ?, ?, ?)").bind(id, body.trackId, ctx.user.id, now).run();
  const lock = await ctx.env.DB.prepare(
    "SELECT l.user_id, u.display_name, l.acquired_at FROM locks l LEFT JOIN users u ON u.id = l.user_id WHERE l.project_id = ? AND l.track_id = ?"
  ).bind(id, body.trackId).first();
  if (lock && lock.user_id !== ctx.user.id) {
    throw new HttpError(409, "locked", `${lock.display_name ?? "\u4ED6\u306E\u4EBA"} \u304C\u30ED\u30C3\u30AF\u3057\u3066\u3044\u307E\u3059`, {
      holder: { userId: lock.user_id, displayName: lock.display_name, acquiredAt: lock.acquired_at }
    });
  }
  if (inserted.meta.changes === 1) {
    await ctx.env.DB.prepare("INSERT INTO lock_events (project_id, track_id, user_id, action, created_at) VALUES (?, ?, ?, 'acquire', ?)").bind(id, body.trackId, ctx.user.id, now).run();
  }
  return json({ trackId: body.trackId, userId: ctx.user.id, displayName: ctx.user.displayName, acquiredAt: lock?.acquired_at ?? now });
});
route("DELETE", "/projects/:id/locks/:trackId", async (ctx, { id, trackId }) => {
  await requireMember(ctx, id);
  const force = ctx.url.searchParams.get("force") === "true";
  const lock = await ctx.env.DB.prepare("SELECT user_id FROM locks WHERE project_id = ? AND track_id = ?").bind(id, trackId).first();
  if (!lock) return json({ ok: true });
  if (lock.user_id !== ctx.user.id && !force) {
    throw new HttpError(403, "not_lock_holder", "\u4ED6\u306E\u4EBA\u306E\u30ED\u30C3\u30AF\u3067\u3059\uFF08\u5F37\u5236\u89E3\u9664\u3059\u308B\u306B\u306F force=true\uFF09");
  }
  const action = lock.user_id === ctx.user.id ? "release" : "force_release";
  await ctx.env.DB.batch([
    ctx.env.DB.prepare("DELETE FROM locks WHERE project_id = ? AND track_id = ?").bind(id, trackId),
    ctx.env.DB.prepare("INSERT INTO lock_events (project_id, track_id, user_id, action, created_at) VALUES (?, ?, ?, ?, ?)").bind(id, trackId, ctx.user.id, action, nowIso())
  ]);
  return json({ ok: true, action });
});
route("GET", "/projects/:id/lock-events", async (ctx, { id }) => {
  await requireMember(ctx, id);
  const rows = await ctx.env.DB.prepare(
    `SELECT e.id, e.track_id, e.user_id, u.display_name, e.action, e.created_at FROM lock_events e
     LEFT JOIN users u ON u.id = e.user_id WHERE e.project_id = ? ORDER BY e.id DESC LIMIT 200`
  ).bind(id).all();
  return json(rows.results.map((e) => ({ id: e.id, trackId: e.track_id, userId: e.user_id, displayName: e.display_name, action: e.action, createdAt: e.created_at })));
});
route("POST", "/blobs/check", async (ctx) => {
  const body = await readJson(ctx.request);
  const hashes = [...new Set((body.hashes ?? []).filter(isSha256))];
  const present = await registeredHashes(ctx.env, hashes);
  const missing = hashes.filter((h) => !present.has(h));
  return json({ missing: await Promise.all(missing.map((h) => transferUrl(ctx.env, ctx.url.origin, h, "PUT"))) });
});
route("POST", "/blobs/:hash/complete", async (ctx, { hash: hash2 }) => {
  if (!isSha256(hash2)) throw new HttpError(400, "bad_request", "\u30CF\u30C3\u30B7\u30E5\u306E\u5F62\u5F0F\u304C\u6B63\u3057\u304F\u3042\u308A\u307E\u305B\u3093");
  return json(await verifyAndRegister(ctx.env, hash2));
});
route("GET", "/blobs/:hash", async (ctx, { hash: hash2 }) => {
  if (!isSha256(hash2)) throw new HttpError(400, "bad_request", "\u30CF\u30C3\u30B7\u30E5\u306E\u5F62\u5F0F\u304C\u6B63\u3057\u304F\u3042\u308A\u307E\u305B\u3093");
  const present = await registeredHashes(ctx.env, [hash2]);
  if (!present.has(hash2)) throw new HttpError(404, "not_found", "\u898B\u3064\u304B\u308A\u307E\u305B\u3093");
  return json(await transferUrl(ctx.env, ctx.url.origin, hash2, "GET"));
});
route("PUT", "/blobs/:hash/data", async (ctx, { hash: hash2 }) => {
  if (!isSha256(hash2)) throw new HttpError(400, "bad_request", "\u30CF\u30C3\u30B7\u30E5\u306E\u5F62\u5F0F\u304C\u6B63\u3057\u304F\u3042\u308A\u307E\u305B\u3093");
  return json(await directUpload(ctx.env, hash2, ctx.request));
});
route("GET", "/blobs/:hash/data", async (ctx, { hash: hash2 }) => {
  if (!isSha256(hash2)) throw new HttpError(400, "bad_request", "\u30CF\u30C3\u30B7\u30E5\u306E\u5F62\u5F0F\u304C\u6B63\u3057\u304F\u3042\u308A\u307E\u305B\u3093");
  return directDownload(ctx.env, hash2);
});
route("GET", "/app/latest", async (ctx) => {
  const platform = ctx.url.searchParams.get("platform");
  if (!isReleasePlatform(platform)) throw new HttpError(400, "bad_request", "platform \u306F windows / mac / linux \u306E\u3044\u305A\u308C\u304B\u3067\u3059");
  const obj = await ctx.env.BLOBS.get(releaseKey(platform));
  if (!obj) throw new HttpError(404, "no_release", "\u914D\u4FE1\u3055\u308C\u3066\u3044\u308B\u66F4\u65B0\u304C\u3042\u308A\u307E\u305B\u3093");
  const info = await obj.json();
  const manifest = await transferUrl(ctx.env, ctx.url.origin, info.manifestHash, "GET");
  return json({ ...info, manifest });
});
route("POST", "/app/releases", async (ctx) => {
  if (!ctx.user.isRelease) throw new HttpError(403, "forbidden", "\u30EA\u30EA\u30FC\u30B9\u7528\u306E\u30AD\u30FC\u304C\u5FC5\u8981\u3067\u3059");
  const body = await readJson(ctx.request);
  if (!isReleasePlatform(body.platform)) throw new HttpError(400, "bad_request", "platform \u304C\u6B63\u3057\u304F\u3042\u308A\u307E\u305B\u3093");
  if (!Number.isInteger(body.build) || (body.build ?? 0) <= 0) throw new HttpError(400, "bad_request", "build\uFF08\u6B63\u306E\u6574\u6570\uFF09\u304C\u5FC5\u8981\u3067\u3059");
  if (!isSha256(body.manifestHash)) throw new HttpError(400, "bad_request", "manifestHash \u304C\u5FC5\u8981\u3067\u3059");
  const manifestObj = await ctx.env.BLOBS.get(blobKey(body.manifestHash));
  if (!manifestObj) throw new HttpError(400, "missing_blobs", "\u30DE\u30CB\u30D5\u30A7\u30B9\u30C8\u304C\u30A2\u30C3\u30D7\u30ED\u30FC\u30C9\u3055\u308C\u3066\u3044\u307E\u305B\u3093", { hashes: [body.manifestHash] });
  let files;
  try {
    files = (await manifestObj.json()).files ?? [];
  } catch {
    throw new HttpError(400, "bad_request", "\u30DE\u30CB\u30D5\u30A7\u30B9\u30C8\u3092\u8AAD\u307F\u8FBC\u3081\u307E\u305B\u3093");
  }
  const hashes = files.map((f) => f.hash ?? "");
  if (files.length === 0 || !hashes.every(isSha256)) throw new HttpError(400, "bad_request", "\u30DE\u30CB\u30D5\u30A7\u30B9\u30C8\u306E\u30D5\u30A1\u30A4\u30EB\u4E00\u89A7\u304C\u6B63\u3057\u304F\u3042\u308A\u307E\u305B\u3093");
  const present = await registeredHashes(ctx.env, [...new Set(hashes)]);
  const missing = hashes.filter((h) => !present.has(h));
  if (missing.length) throw new HttpError(400, "missing_blobs", "\u30A2\u30C3\u30D7\u30ED\u30FC\u30C9\u3055\u308C\u3066\u3044\u306A\u3044\u30D5\u30A1\u30A4\u30EB\u304C\u3042\u308A\u307E\u3059", { hashes: missing });
  const info = {
    platform: body.platform,
    build: body.build,
    version: body.version ?? "",
    manifestHash: body.manifestHash,
    notes: body.notes ?? "",
    createdAt: nowIso()
  };
  await ctx.env.BLOBS.put(releaseKey(body.platform), JSON.stringify(info), { httpMetadata: { contentType: "application/json" } });
  await ctx.env.BLOBS.put(`app-releases/${body.platform}/${info.build}.json`, JSON.stringify(info));
  return json(info, 201);
});
async function healthCheck(env) {
  const lines = [];
  let ok = true;
  const check = /* @__PURE__ */ __name(async (label, fn) => {
    try {
      const note = await fn();
      lines.push(`OK  ${label}${note ? `\uFF08${note}\uFF09` : ""}`);
    } catch (e) {
      ok = false;
      lines.push(`NG  ${label}: ${e instanceof Error ? e.message : String(e)}`);
    }
  }, "check");
  await check("D1 \u30D0\u30A4\u30F3\u30C7\u30A3\u30F3\u30B0 DB", async () => {
    if (!env.DB) throw new Error("\u30D0\u30A4\u30F3\u30C7\u30A3\u30F3\u30B0 DB \u304C\u3042\u308A\u307E\u305B\u3093\uFF08Worker \u306E\u300C\u30D0\u30A4\u30F3\u30C7\u30A3\u30F3\u30B0\u300D\u3067 D1 \u3092\u5909\u6570\u540D DB \u3067\u8FFD\u52A0\uFF09");
  });
  await check("D1 \u306E\u30C6\u30FC\u30D6\u30EB", async () => {
    if (!env.DB) throw new Error("DB \u304C\u306A\u3044\u305F\u3081\u78BA\u8A8D\u3067\u304D\u307E\u305B\u3093");
    for (const table of ["users", "projects", "revisions", "locks", "blobs"])
      await env.DB.prepare(`SELECT COUNT(*) AS n FROM ${table}`).first().catch(() => {
        throw new Error(`\u30C6\u30FC\u30D6\u30EB ${table} \u304C\u3042\u308A\u307E\u305B\u3093\uFF08migrations/0001_init.sql \u3092 D1 \u306E\u30B3\u30F3\u30BD\u30FC\u30EB\u3067\u5B9F\u884C\uFF09`);
      });
  });
  await check("R2 \u30D0\u30A4\u30F3\u30C7\u30A3\u30F3\u30B0 BLOBS", async () => {
    if (!env.BLOBS) throw new Error("\u30D0\u30A4\u30F3\u30C7\u30A3\u30F3\u30B0 BLOBS \u304C\u3042\u308A\u307E\u305B\u3093\uFF08Worker \u306E\u300C\u30D0\u30A4\u30F3\u30C7\u30A3\u30F3\u30B0\u300D\u3067 R2 \u3092\u5909\u6570\u540D BLOBS \u3067\u8FFD\u52A0\uFF09");
    await env.BLOBS.head("health-check");
  });
  lines.push(`--  \u7F72\u540D\u4ED8\u304D URL\uFF08R2 \u306E API \u30AD\u30FC\uFF09: ${presignEnabled(env) ? "\u8A2D\u5B9A\u3042\u308A" : "\u306A\u3057\uFF08Worker \u7D4C\u7531\u3067\u8EE2\u9001\u30021 \u30D5\u30A1\u30A4\u30EB 100MB \u307E\u3067\uFF09"}`);
  lines.push(`--  ADMIN_PASSWORD: ${env.ADMIN_PASSWORD ? "\u8A2D\u5B9A\u3042\u308A" : "\u306A\u3057"}`);
  lines.push(`--  RELEASE_KEY: ${env.RELEASE_KEY ? "\u8A2D\u5B9A\u3042\u308A" : "\u306A\u3057"}`);
  const text = `ShareDAW sync server: ${ok ? "OK" : "\u8A2D\u5B9A\u306B\u554F\u984C\u304C\u3042\u308A\u307E\u3059"}

${lines.join("\n")}
`;
  return new Response(text, { status: ok ? 200 : 500, headers: { "content-type": "text/plain; charset=utf-8" } });
}
__name(healthCheck, "healthCheck");
var index_default = {
  async fetch(request, env) {
    const url = new URL(request.url);
    try {
      if (url.pathname === "/" && request.method === "GET") return await healthCheck(env);
      if (url.pathname === "/admin") return await handleAdmin(request, env);
      for (const r of routes) {
        if (r.method !== request.method) continue;
        const m = r.pattern.exec(url.pathname);
        if (!m) continue;
        const params = {};
        r.keys.forEach((k, i) => params[k] = decodeURIComponent(m[i + 1]));
        const user = await authenticate(env, request);
        if (user.isRelease && !url.pathname.startsWith("/blobs") && !url.pathname.startsWith("/app/"))
          throw new HttpError(403, "forbidden", "\u30EA\u30EA\u30FC\u30B9\u7528\u306E\u30AD\u30FC\u3067\u4F7F\u3048\u308B\u306E\u306F\u66F4\u65B0\u306E\u914D\u4FE1\u3060\u3051\u3067\u3059");
        return await r.handler({ env, request, url, user }, params);
      }
      return json({ error: "not_found", message: "\u898B\u3064\u304B\u308A\u307E\u305B\u3093" }, 404);
    } catch (e) {
      if (e instanceof HttpError) return errorResponse(e);
      console.error(e);
      return json({ error: "internal", message: "\u30B5\u30FC\u30D0\u30FC\u30A8\u30E9\u30FC" }, 500);
    }
  }
};
export {
  index_default as default
};
/*! Bundled license information:

aws4fetch/dist/aws4fetch.esm.mjs:
  (**
   * @license MIT <https://opensource.org/licenses/MIT>
   * @copyright Michael Hart 2024
   *)
*/
