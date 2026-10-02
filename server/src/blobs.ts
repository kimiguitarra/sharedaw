// 実体（オーディオ、プロジェクト JSON）の置き場。R2 の blobs/<sha256>。コンテンツアドレスで不変（§6.4）。
//
// 本番: R2 の S3 互換 API の署名付き URL を返し、アプリは Worker を経由せず直接 PUT / GET する（§6.5）。
// 開発・テスト: R2_ACCESS_KEY_ID が未設定なら、Worker の /blobs/:hash/data を経由する URL を返す。

import { AwsClient } from "aws4fetch";
import { Env, HttpError, blobKey, hex, nowIso, queryInChunks } from "./util";

export interface TransferUrl {
  hash: string;
  url: string;
  method: "PUT" | "GET";
  /** true なら Authorization ヘッダー（Bearer トークン）を付けて送る（開発用の直接転送） */
  authRequired: boolean;
  /** 送るときに付けるヘッダー（署名付き URL の PUT では SHA-256 のチェックサム。R2 が内容を検証する） */
  headers?: Record<string, string>;
}

/** 16 進の SHA-256 を base64 に（x-amz-checksum-sha256 の形式） */
export function hexToBase64(hash: string): string {
  const bytes = hash.match(/../g)!.map((h) => parseInt(h, 16));
  return btoa(String.fromCharCode(...bytes));
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

  // PUT はチェックサムを署名に含める。R2 が内容とハッシュの一致を確かめるので、Worker で大きなファイルをハッシュしなくてよい
  const headers: Record<string, string> = method === "PUT" ? { "x-amz-checksum-sha256": hexToBase64(hash) } : {};
  const signed = await client.sign(new Request(url, { method, headers }), { aws: { signQuery: true } });
  return { hash, url: signed.url, method, authRequired: false, ...(method === "PUT" ? { headers } : {}) };
}

export async function registeredHashes(env: Env, hashes: string[]): Promise<Set<string>> {
  const rows = await queryInChunks<{ hash: string }>(env.DB, "SELECT hash FROM blobs WHERE hash IN ({in})", hashes);
  return new Set(rows.map((r) => r.hash));
}

/** 検証済みの実体を blobs に登録する（同じものがあれば何もしない）。 */
async function registerBlob(env: Env, hash: string, size: number): Promise<{ hash: string; size: number }> {
  await env.DB.prepare("INSERT OR IGNORE INTO blobs (hash, size, created_at) VALUES (?, ?, ?)").bind(hash, size, nowIso()).run();
  return { hash, size };
}

/** 中身のハッシュが名前と違う実体を消して 400 にする。 */
async function rejectMismatch(env: Env, hash: string, actual?: string): Promise<never> {
  await env.BLOBS.delete(blobKey(hash));
  throw new HttpError(400, "hash_mismatch", "アップロードされた内容のハッシュが一致しません", actual ? { actual } : {});
}

/** R2 にある実体のハッシュとサイズを検証して blobs に登録する（アップロード完了時）。 */
export async function verifyAndRegister(env: Env, hash: string): Promise<{ hash: string; size: number }> {
  const existing = await env.DB.prepare("SELECT size FROM blobs WHERE hash = ?").bind(hash).first<{ size: number }>();
  if (existing) return { hash, size: existing.size };

  const head = await env.BLOBS.head(blobKey(hash));
  if (!head) throw new HttpError(404, "blob_not_uploaded", "アップロードされていません");

  // R2 がチェックサムを検証済みなら、それを使う（Worker の CPU 時間を使わない）
  const stored = head.checksums?.sha256;
  if (stored) {
    if (hex(stored) !== hash) return rejectMismatch(env, hash, hex(stored));
    return registerBlob(env, hash, head.size);
  }

  const obj = await env.BLOBS.get(blobKey(hash));
  if (!obj) throw new HttpError(404, "blob_not_uploaded", "アップロードされていません");

  const digestStream = new crypto.DigestStream("SHA-256");
  await obj.body.pipeTo(digestStream);
  const actual = hex(await digestStream.digest);

  if (actual !== hash) return rejectMismatch(env, hash, actual);
  return registerBlob(env, hash, obj.size);
}

/** 開発用: Worker 経由のアップロード（その場で検証して登録）。 */
export async function directUpload(env: Env, hash: string, request: Request) {
  // 本文はメモリに溜めずに R2 へ流し、SHA-256 の検証は R2 に任せる（大きなファイルでも Worker のメモリ・CPU を使わない）
  const length = Number(request.headers.get("content-length") ?? "");
  let size: number;

  try {
    if (request.body && Number.isFinite(length) && length > 0) {
      const obj = await env.BLOBS.put(blobKey(hash), request.body, { sha256: hash });
      size = obj?.size ?? length;
    } else {
      const data = await request.arrayBuffer();
      const obj = await env.BLOBS.put(blobKey(hash), data, { sha256: hash });
      size = obj?.size ?? data.byteLength;
    }
  } catch (e) {
    const message = e instanceof Error ? e.message : String(e);
    if (/sha|checksum|digest|hash/i.test(message))
      throw new HttpError(400, "hash_mismatch", "アップロードされた内容のハッシュが一致しません");
    throw e;
  }

  return registerBlob(env, hash, size);
}

export async function directDownload(env: Env, hash: string): Promise<Response> {
  const obj = await env.BLOBS.get(blobKey(hash));
  if (!obj) throw new HttpError(404, "not_found", "見つかりません");
  return new Response(obj.body, { headers: { "content-type": "application/octet-stream", "content-length": String(obj.size) } });
}
