// 管理ページ（/admin）: ブラウザからユーザーを作ってトークンを発行する。
// Cloudflare のダッシュボードで Worker のシークレット ADMIN_PASSWORD を設定したときだけ使える。
// トークンは発行したときに一度だけ表示し、D1 にはハッシュのみ保存する（仕様書 §6.2）。

import { Env, nowIso, sha256Hex } from "./util";

const escapeHtml = (s: string) =>
  s.replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[c]!);

function page(body: string, status = 200): Response {
  const html = `<!doctype html>
<html lang="ja"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>ShareDAW 同期サーバー 管理</title>
<style>
  body { font-family: system-ui, sans-serif; max-width: 640px; margin: 32px auto; padding: 0 16px; line-height: 1.6; }
  label { display: block; margin-top: 12px; }
  input[type=text], input[type=password] { width: 100%; padding: 8px; font-size: 16px; box-sizing: border-box; }
  button { margin-top: 16px; padding: 8px 16px; font-size: 16px; }
  .token { font-family: monospace; font-size: 15px; background: #f3f3f3; padding: 12px; word-break: break-all; }
  .error { color: #b00020; }
  table { border-collapse: collapse; margin-top: 8px; } td, th { border-bottom: 1px solid #ddd; padding: 4px 12px 4px 0; text-align: left; }
</style></head><body>
<h1>ShareDAW 同期サーバー</h1>
${body}
</body></html>`;
  return new Response(html, { status, headers: { "content-type": "text/html; charset=utf-8", "cache-control": "no-store" } });
}

function form(message = ""): string {
  return `${message}
<h2>ユーザーを追加</h2>
<form method="post" action="/admin">
  <label>管理パスワード（Worker のシークレット ADMIN_PASSWORD）<input type="password" name="password" required autocomplete="current-password"></label>
  <label>表示名（アプリの「○○ が編集中」などに出る名前）<input type="text" name="name" required maxlength="40"></label>
  <label><input type="checkbox" name="joinAll" checked> 既存のプロジェクトすべてに参加させる</label>
  <button type="submit">ユーザーを作ってトークンを発行</button>
</form>`;
}

async function passwordMatches(env: Env, given: string): Promise<boolean> {
  if (!env.ADMIN_PASSWORD) return false;
  // 長さや内容で比較時間が変わらないよう、ハッシュどうしを比べる
  const [a, b] = await Promise.all([sha256Hex(given), sha256Hex(env.ADMIN_PASSWORD)]);
  let diff = 0;
  for (let i = 0; i < a.length; ++i) diff |= a.charCodeAt(i) ^ b.charCodeAt(i);
  return diff === 0;
}

function newToken(): string {
  const bytes = crypto.getRandomValues(new Uint8Array(32));
  return btoa(String.fromCharCode(...bytes)).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
}

export async function handleAdmin(request: Request, env: Env): Promise<Response> {
  if (!env.ADMIN_PASSWORD)
    return page(`<p class="error">管理ページを使うには、Cloudflare のダッシュボードでこの Worker のシークレット
      <code>ADMIN_PASSWORD</code> を設定してください。</p>`, 503);

  if (request.method === "GET") return page(form());

  if (request.method !== "POST") return page("<p>対応していない操作です。</p>", 405);

  const data = await request.formData();
  const password = String(data.get("password") ?? "");
  const name = String(data.get("name") ?? "").trim().normalize("NFC");

  if (!(await passwordMatches(env, password)))
    return page(form(`<p class="error">管理パスワードが違います。</p>`), 403);

  if (!name || name.length > 40) return page(form(`<p class="error">表示名を 40 文字以内で入力してください。</p>`), 400);

  const id = crypto.randomUUID();
  const token = newToken();
  const statements = [
    env.DB.prepare("INSERT INTO users (id, display_name, token_hash, created_at) VALUES (?, ?, ?, ?)").bind(id, name, await sha256Hex(token), nowIso()),
  ];

  if (data.get("joinAll"))
    statements.push(env.DB.prepare("INSERT OR IGNORE INTO project_members (project_id, user_id) SELECT id, ? FROM projects").bind(id));

  await env.DB.batch(statements);

  const users = await env.DB.prepare("SELECT display_name, created_at FROM users ORDER BY created_at").all<{ display_name: string; created_at: string }>();
  const rows = users.results.map((u) => `<tr><td>${escapeHtml(u.display_name)}</td><td>${escapeHtml(u.created_at.slice(0, 10))}</td></tr>`).join("");

  return page(`<p>ユーザー「${escapeHtml(name)}」を作りました。次のトークンを本人に渡してください。
<strong>この画面を閉じると二度と表示できません</strong>（なくしたら新しいユーザーを作り直します）。</p>
<div class="token">${escapeHtml(token)}</div>
<p>アプリの「同期 → サーバー設定」で、サーバー URL（<code>${escapeHtml(new URL(request.url).origin)}</code>）とこのトークンを入力します。</p>
<h2>ユーザー一覧</h2><table><tr><th>表示名</th><th>作成日</th></tr>${rows}</table>
<p><a href="/admin">続けてユーザーを追加する</a></p>`);
}
