// アプリの更新（CI がファイルごとに実体としてアップロードし、一覧（マニフェスト）を登録する）

import { registeredHashes, transferUrl } from "../blobs";
import { route } from "../router";
import { HttpError, blobKey, isReleasePlatform, isSha256, json, nowIso, optionalString, readJson, releaseBuildKey, releaseKey, requireSha256 } from "../util";

interface ReleaseInfo {
  platform: string;
  build: number;
  version: string;
  manifestHash: string;
  notes: string;
  createdAt: string;
}

route("GET", "/app/latest", async (ctx) => {
  const platform = ctx.url.searchParams.get("platform");
  if (!isReleasePlatform(platform)) throw new HttpError(400, "bad_request", "platform は windows / mac / linux のいずれかです");

  const obj = await ctx.env.BLOBS.get(releaseKey(platform));
  if (!obj) throw new HttpError(404, "no_release", "配信されている更新がありません");

  const info = (await obj.json()) as ReleaseInfo;
  const manifest = await transferUrl(ctx.env, ctx.url.origin, info.manifestHash, "GET");
  return json({ ...info, manifest });
});

route("POST", "/app/releases", async (ctx) => {
  if (!ctx.user.isRelease) throw new HttpError(403, "forbidden", "リリース用のキーが必要です");

  const body = await readJson<{ platform?: unknown; build?: unknown; version?: unknown; manifestHash?: unknown; notes?: unknown }>(ctx.request);
  if (!isReleasePlatform(body.platform)) throw new HttpError(400, "bad_request", "platform が正しくありません");
  const platform = body.platform;
  const build = body.build;
  if (typeof build !== "number" || !Number.isSafeInteger(build) || build <= 0) throw new HttpError(400, "bad_request", "build（正の整数）が必要です");
  const manifestHash = requireSha256(body.manifestHash, "manifestHash");

  // いま配っているものより古いビルドで上書きしない（全員のアプリが古い版に戻ってしまう）。
  // 同じビルドは受け付ける（配信だけやり直す「Re-run failed jobs」のため）
  const current = await ctx.env.BLOBS.get(releaseKey(platform));
  if (current) {
    const currentBuild = ((await current.json()) as Partial<ReleaseInfo>).build ?? 0;
    if (build < currentBuild)
      throw new HttpError(409, "older_build", `配信中のビルド（${currentBuild}）より古いビルドです`, { current: currentBuild });
  }

  // マニフェストと、そこに書かれたファイルがすべてアップロード済み（検証して登録済み）であること
  if (!(await registeredHashes(ctx.env, [manifestHash])).has(manifestHash))
    throw new HttpError(400, "missing_blobs", "マニフェストがアップロードされていません", { hashes: [manifestHash] });

  const manifestObj = await ctx.env.BLOBS.get(blobKey(manifestHash));
  if (!manifestObj) throw new HttpError(400, "missing_blobs", "マニフェストがアップロードされていません", { hashes: [manifestHash] });

  let files: Array<{ path?: string; hash?: string }>;
  try {
    files = ((await manifestObj.json()) as { files?: Array<{ path?: string; hash?: string }> }).files ?? [];
  } catch {
    throw new HttpError(400, "bad_request", "マニフェストを読み込めません");
  }

  const hashes = files.map((f) => f.hash ?? "");
  if (files.length === 0 || !hashes.every(isSha256)) throw new HttpError(400, "bad_request", "マニフェストのファイル一覧が正しくありません");

  const present = await registeredHashes(ctx.env, [...new Set(hashes)]);
  const missing = hashes.filter((h) => !present.has(h));
  if (missing.length) throw new HttpError(400, "missing_blobs", "アップロードされていないファイルがあります", { hashes: missing });

  const info: ReleaseInfo = {
    platform,
    build,
    version: optionalString(body.version, "version", 100),
    manifestHash,
    notes: optionalString(body.notes, "notes", 4000),
    createdAt: nowIso(),
  };

  await ctx.env.BLOBS.put(releaseKey(platform), JSON.stringify(info), { httpMetadata: { contentType: "application/json" } });
  await ctx.env.BLOBS.put(releaseBuildKey(platform, build), JSON.stringify(info));
  return json(info, 201);
});
