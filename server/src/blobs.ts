// 実体（オーディオ、プロジェクト JSON）の置き場。R2 の blobs/<sha256>。コンテンツアドレスで不変（§6.4）。
//
// 本番: R2 の S3 互換 API の署名付き URL を返し、アプリは Worker を経由せず直接 PUT / GET する（§6.5）。
// 開発・テスト: R2_ACCESS_KEY_ID が未設定なら、Worker の /blobs/:hash/data を経由する URL を返す。

import { AwsClient } from "aws4fetch";
import { Env, HttpError, blobKey, hex, nowIso, sha256Hex } from "./util";

export interface TransferUrl {
  hash: string;
  url: string;
  method: "PUT" | "GET";
  /** true なら Authorization ヘッダー（Bearer トークン）を付けて送る（開発用の直接転送） */
  authRequired: boolean;
}

const presignExpirySeconds = 3600;

export function presignEnabled(env: Env): boolean {
  return !!(env.R2_ACCESS_KEY_ID && env.R2_SECRET_ACCESS_KEY && env.R2_ACCOUNT_ID && env.R2_BUCKET_NAME);
}

export async function transferUrl(env: Env, origin: string, hash: string, method: "PUT" | "GET"): Promise<TransferUrl> {
  if (!presignEnabled(env)) {
    return { hash, url: `${origin}/blobs/${hash}/data`, method, authRequired: true };
  }

  const client = new AwsClient({
    accessKeyId: env.R2_ACCESS_KEY_ID!,
    secretAccessKey: env.R2_SECRET_ACCESS_KEY!,
    service: "s3",
    region: "auto",
  });

  const url = new URL(`https://${env.R2_ACCOUNT_ID}.r2.cloudflarestorage.com/${env.R2_BUCKET_NAME}/${blobKey(hash)}`);
  url.searchParams.set("X-Amz-Expires", String(presignExpirySeconds));
  const signed = await client.sign(new Request(url, { method }), { aws: { signQuery: true } });
  return { hash, url: signed.url, method, authRequired: false };
}

export async function registeredHashes(env: Env, hashes: string[]): Promise<Set<string>> {
  const found = new Set<string>();

  // SQLite の変数上限を避けるため分割する
  for (let i = 0; i < hashes.length; i += 50) {
    const chunk = hashes.slice(i, i + 50);
    const placeholders = chunk.map(() => "?").join(",");
    const rows = await env.DB.prepare(`SELECT hash FROM blobs WHERE hash IN (${placeholders})`)
      .bind(...chunk)
      .all<{ hash: string }>();
    for (const r of rows.results) found.add(r.hash);
  }

  return found;
}

/** R2 にある実体のハッシュとサイズを検証して blobs に登録する（アップロード完了時）。 */
export async function verifyAndRegister(env: Env, hash: string): Promise<{ hash: string; size: number }> {
  const existing = await env.DB.prepare("SELECT size FROM blobs WHERE hash = ?").bind(hash).first<{ size: number }>();
  if (existing) return { hash, size: existing.size };

  const obj = await env.BLOBS.get(blobKey(hash));
  if (!obj) throw new HttpError(404, "blob_not_uploaded", "アップロードされていません");

  const digestStream = new crypto.DigestStream("SHA-256");
  await obj.body.pipeTo(digestStream);
  const actual = hex(await digestStream.digest);

  if (actual !== hash) {
    await env.BLOBS.delete(blobKey(hash));
    throw new HttpError(400, "hash_mismatch", "アップロードされた内容のハッシュが一致しません", { actual });
  }

  await env.DB.prepare("INSERT OR IGNORE INTO blobs (hash, size, created_at) VALUES (?, ?, ?)")
    .bind(hash, obj.size, nowIso())
    .run();

  return { hash, size: obj.size };
}

/** 開発用: Worker 経由のアップロード（その場で検証して登録）。 */
export async function directUpload(env: Env, hash: string, request: Request) {
  const data = await request.arrayBuffer();
  const actual = await sha256Hex(data);

  if (actual !== hash) throw new HttpError(400, "hash_mismatch", "アップロードされた内容のハッシュが一致しません", { actual });

  await env.BLOBS.put(blobKey(hash), data);
  await env.DB.prepare("INSERT OR IGNORE INTO blobs (hash, size, created_at) VALUES (?, ?, ?)")
    .bind(hash, data.byteLength, nowIso())
    .run();

  return { hash, size: data.byteLength };
}

export async function directDownload(env: Env, hash: string): Promise<Response> {
  const obj = await env.BLOBS.get(blobKey(hash));
  if (!obj) throw new HttpError(404, "not_found", "見つかりません");
  return new Response(obj.body, { headers: { "content-type": "application/octet-stream", "content-length": String(obj.size) } });
}
