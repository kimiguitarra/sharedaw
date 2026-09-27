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
