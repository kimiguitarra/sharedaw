// 実体（§6.4, §6.5）: オーディオとプロジェクト JSON。SHA-256 で名前が決まる

import { directDownload, directUpload, registeredHashes, transferUrl, verifyAndRegister } from "../blobs";
import { route } from "../router";
import { HttpError, isSha256, json, limits, readJson, requireSha256 } from "../util";

route("POST", "/blobs/check", async (ctx) => {
  const body = await readJson<{ hashes?: unknown }>(ctx.request);
  if (body.hashes !== undefined && !Array.isArray(body.hashes)) throw new HttpError(400, "bad_request", "hashes は配列です");

  const hashes = [...new Set(((body.hashes as unknown[]) ?? []).filter(isSha256))];
  if (hashes.length > limits.hashesPerCheck)
    throw new HttpError(400, "bad_request", `一度に確認できるのは ${limits.hashesPerCheck} 個までです`);

  const present = await registeredHashes(ctx.env, hashes);
  const missing = hashes.filter((h) => !present.has(h));
  return json({ missing: await Promise.all(missing.map((h) => transferUrl(ctx.env, ctx.url.origin, h, "PUT"))) });
});

route("POST", "/blobs/:hash/complete", async (ctx, { hash }) => json(await verifyAndRegister(ctx.env, requireSha256(hash))));

route("GET", "/blobs/:hash", async (ctx, { hash }) => {
  requireSha256(hash);
  if (!(await registeredHashes(ctx.env, [hash])).has(hash)) throw new HttpError(404, "not_found", "見つかりません");
  return json(await transferUrl(ctx.env, ctx.url.origin, hash, "GET"));
});

route("PUT", "/blobs/:hash/data", async (ctx, { hash }) => json(await directUpload(ctx.env, requireSha256(hash), ctx.request)));

route("GET", "/blobs/:hash/data", async (ctx, { hash }) => directDownload(ctx.env, requireSha256(hash)));
