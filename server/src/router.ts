// ルーティングと認証（§6.2）。各ルートは routes/*.ts で route() を呼んで登録する。

import { Env, HttpError, User, errorResponse, json, sha256Hex } from "./util";

export interface Context {
  env: Env;
  request: Request;
  url: URL;
  user: User;
}

type Handler = (ctx: Context, params: Record<string, string>) => Promise<Response>;

const routes: Array<{ method: string; pattern: RegExp; keys: string[]; handler: Handler }> = [];

export function route(method: string, path: string, handler: Handler) {
  const keys: string[] = [];
  const pattern = new RegExp("^" + path.replace(/:([a-zA-Z]+)/g, (_, k: string) => (keys.push(k), "([^/]+)")) + "$");
  routes.push({ method, pattern, keys, handler });
}

//==============================================================================
// 認証: Authorization: Bearer <token>。D1 にはトークンのハッシュのみ保存する。

async function authenticate(env: Env, request: Request): Promise<User> {
  const header = request.headers.get("authorization") ?? "";
  const match = /^Bearer\s+(.+)$/i.exec(header);
  if (!match) throw new HttpError(401, "unauthorized", "トークンがありません");

  const tokenHash = await sha256Hex(match[1].trim());

  // アプリの更新を配信する CI（RELEASE_KEY はダッシュボードのシークレット）。ハッシュ同士で比べる
  if (env.RELEASE_KEY && tokenHash === (await sha256Hex(env.RELEASE_KEY)))
    return { id: "release", displayName: "release", isRelease: true };

  const row = await env.DB.prepare("SELECT id, display_name FROM users WHERE token_hash = ?")
    .bind(tokenHash)
    .first<{ id: string; display_name: string }>();

  if (!row) throw new HttpError(401, "unauthorized", "トークンが正しくありません");
  return { id: row.id, displayName: row.display_name };
}

/** 参加している曲を返す（参加していなければ 404）。 */
export async function requireMember(ctx: Context, projectId: string) {
  const project = await ctx.env.DB.prepare(
    `SELECT p.id, p.name, p.head_revision, p.created_by, p.created_at FROM projects p
     JOIN project_members m ON m.project_id = p.id AND m.user_id = ? WHERE p.id = ?`,
  )
    .bind(ctx.user.id, projectId)
    .first<{ id: string; name: string; head_revision: number; created_by: string; created_at: string }>();

  if (!project) throw new HttpError(404, "project_not_found", "プロジェクトが見つからないか、参加していません");
  return project;
}

function decodeParam(value: string): string {
  try {
    return decodeURIComponent(value);
  } catch {
    throw new HttpError(400, "bad_request", "URL の形式が正しくありません");
  }
}

/** 登録したルートから探して実行する（見つからなければ null）。 */
export async function dispatch(request: Request, env: Env, url: URL): Promise<Response | null> {
  for (const r of routes) {
    if (r.method !== request.method) continue;
    const m = r.pattern.exec(url.pathname);
    if (!m) continue;

    const params: Record<string, string> = {};
    r.keys.forEach((k, i) => (params[k] = decodeParam(m[i + 1])));

    const user = await authenticate(env, request);

    // リリース用のキー（CI）で使えるのは、実体のアップロードと更新の登録だけ
    if (user.isRelease && !url.pathname.startsWith("/blobs") && !url.pathname.startsWith("/app/"))
      throw new HttpError(403, "forbidden", "リリース用のキーで使えるのは更新の配信だけです");

    return await r.handler({ env, request, url, user }, params);
  }

  return null;
}

export function handleError(e: unknown): Response {
  if (e instanceof HttpError) return errorResponse(e);
  console.error(e);
  return json({ error: "internal", message: "サーバーエラー" }, 500);
}
