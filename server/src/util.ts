export interface Env {
  DB: D1Database;
  BLOBS: R2Bucket;
  R2_BUCKET_NAME?: string;
  R2_ACCOUNT_ID?: string;
  R2_ACCESS_KEY_ID?: string;
  R2_SECRET_ACCESS_KEY?: string;
  ADMIN_PASSWORD?: string;
  /** アプリの更新を配信する CI（GitHub Actions）用のキー。これを Bearer トークンとして送ると release ユーザーになる */
  RELEASE_KEY?: string;
}

export interface User {
  id: string;
  displayName: string;
  /** RELEASE_KEY で認証した CI。実体のアップロードとリリースの登録だけができる */
  isRelease?: boolean;
}

export class HttpError extends Error {
  constructor(
    public status: number,
    public code: string,
    message: string,
    public extra: Record<string, unknown> = {},
  ) {
    super(message);
  }
}

export function json(data: unknown, status = 200): Response {
  return new Response(JSON.stringify(data), {
    status,
    headers: { "content-type": "application/json; charset=utf-8" },
  });
}

export function errorResponse(e: HttpError): Response {
  return json({ error: e.code, message: e.message, ...e.extra }, e.status);
}

export function nowIso(): string {
  return new Date().toISOString();
}

export async function sha256Hex(data: string | ArrayBuffer | Uint8Array): Promise<string> {
  const bytes = typeof data === "string" ? new TextEncoder().encode(data) : data;
  const digest = await crypto.subtle.digest("SHA-256", bytes);
  return hex(digest);
}

export function hex(buffer: ArrayBuffer): string {
  return [...new Uint8Array(buffer)].map((b) => b.toString(16).padStart(2, "0")).join("");
}

export const isSha256 = (s: unknown): s is string => typeof s === "string" && /^[0-9a-f]{64}$/.test(s);

export const isUuid = (s: unknown): s is string =>
  typeof s === "string" && /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/.test(s);

export async function readJson<T>(request: Request): Promise<T> {
  try {
    return (await request.json()) as T;
  } catch {
    throw new HttpError(400, "bad_request", "JSON を読み込めません");
  }
}

export const blobKey = (hash: string) => `blobs/${hash}`;

export const releasePlatforms = ["windows", "mac", "linux"] as const;
export type ReleasePlatform = (typeof releasePlatforms)[number];
export const isReleasePlatform = (s: unknown): s is ReleasePlatform => releasePlatforms.includes(s as ReleasePlatform);
export const releaseKey = (platform: ReleasePlatform) => `app-releases/${platform}/latest.json`;

/** ビルドごとに残しておくリリース情報（app-releases/<platform>/<build>.json）。 */
export const releaseBuildKey = (platform: ReleasePlatform, build: number) => `app-releases/${platform}/${build}.json`;

//==============================================================================
// 入力の検証（形が違えば 400。D1 に渡す前に止める）

export const limits = {
  projectName: 200,
  message: 2000,
  trackId: 200,
  memberIds: 100,
  hashesPerCheck: 1000,
} as const;

/** 前後の空白を取った文字列。空、または max 文字を超えたら 400。 */
export function requireString(value: unknown, field: string, max: number): string {
  if (typeof value !== "string" || !value.trim()) throw new HttpError(400, "bad_request", `${field} が必要です`);
  const s = value.trim();
  if (s.length > max) throw new HttpError(400, "bad_request", `${field} が長すぎます（${max} 文字まで）`);
  return s;
}

/** 省略できる文字列（なければ fallback）。文字列でない、または長すぎれば 400。 */
export function optionalString(value: unknown, field: string, max: number, fallback = ""): string {
  if (value === undefined || value === null) return fallback;
  if (typeof value !== "string") throw new HttpError(400, "bad_request", `${field} は文字列です`);
  if (value.length > max) throw new HttpError(400, "bad_request", `${field} が長すぎます（${max} 文字まで）`);
  return value;
}

/** URL の中の 1 以上の整数（リビジョン番号など）。 */
export function requirePositiveInt(value: string, field: string): number {
  const n = Number(value);
  if (!Number.isSafeInteger(n) || n < 1) throw new HttpError(400, "bad_request", `${field} は 1 以上の整数です`);
  return n;
}

export function requireSha256(value: unknown, field = "hash"): string {
  if (!isSha256(value)) throw new HttpError(400, "bad_request", `${field} の形式が正しくありません`);
  return value;
}

/** IN (...) を SQLite の変数上限に収まるよう分けて実行し、結果をまとめて返す。sql の "{in}" が "?,?,…" になる。 */
export async function queryInChunks<T>(db: D1Database, sql: string, values: string[], chunkSize = 50): Promise<T[]> {
  const rows: T[] = [];

  for (let i = 0; i < values.length; i += chunkSize) {
    const chunk = values.slice(i, i + chunkSize);
    const result = await db.prepare(sql.replace("{in}", chunk.map(() => "?").join(","))).bind(...chunk).all<T>();
    rows.push(...result.results);
  }

  return rows;
}
