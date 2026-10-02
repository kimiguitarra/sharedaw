// 曲（プロジェクト）とメンバー

import { route, requireMember } from "../router";
import { HttpError, blobKey, isUuid, json, limits, nowIso, queryInChunks, readJson, requireString } from "../util";

route("GET", "/me", async (ctx) => json({ id: ctx.user.id, displayName: ctx.user.displayName }));

route("GET", "/users", async (ctx) => {
  const rows = await ctx.env.DB.prepare("SELECT id, display_name FROM users ORDER BY display_name").all<{ id: string; display_name: string }>();
  return json(rows.results.map((r) => ({ id: r.id, displayName: r.display_name })));
});

route("GET", "/projects", async (ctx) => {
  // 最新リビジョンの日時と作者も返す（アプリの「楽曲を選ぶ」画面で、サーバーの状況を見せるため）
  const rows = await ctx.env.DB.prepare(
    `SELECT p.id, p.name, p.head_revision, p.created_by, p.created_at, r.created_at AS updated_at, u.display_name AS updated_by
     FROM projects p
     JOIN project_members m ON m.project_id = p.id
     LEFT JOIN revisions r ON r.project_id = p.id AND r.number = p.head_revision
     LEFT JOIN users u ON u.id = r.author_id
     WHERE m.user_id = ? ORDER BY COALESCE(r.created_at, p.created_at) DESC`,
  )
    .bind(ctx.user.id)
    .all<{ id: string; name: string; head_revision: number; created_by: string; created_at: string; updated_at: string | null; updated_by: string | null }>();

  return json(
    rows.results.map((p) => ({
      id: p.id,
      name: p.name,
      headRevision: p.head_revision,
      createdBy: p.created_by,
      createdAt: p.created_at,
      updatedAt: p.updated_at ?? p.created_at,
      updatedBy: p.updated_by ?? null,
    })),
  );
});

route("POST", "/projects", async (ctx) => {
  const body = await readJson<{ id?: unknown; name?: unknown; memberIds?: unknown }>(ctx.request);

  if (!isUuid(body.id)) throw new HttpError(400, "bad_request", "id（プロジェクト JSON の projectId）が必要です");
  const id = body.id;
  const name = requireString(body.name, "name", limits.projectName);

  const memberIds = body.memberIds ?? [];
  if (!Array.isArray(memberIds) || memberIds.length > limits.memberIds || !memberIds.every((m) => typeof m === "string"))
    throw new HttpError(400, "bad_request", "memberIds は文字列の配列です");

  const exists = await ctx.env.DB.prepare("SELECT id FROM projects WHERE id = ?").bind(id).first();
  if (exists) throw new HttpError(409, "project_exists", "同じ ID のプロジェクトが既にあります");

  const now = nowIso();
  const members = new Set<string>([ctx.user.id, ...(memberIds as string[])]);
  await ctx.env.DB.batch([
    ctx.env.DB.prepare("INSERT INTO projects (id, name, head_revision, created_by, created_at) VALUES (?, ?, 0, ?, ?)").bind(id, name, ctx.user.id, now),
    ...[...members].map((m) =>
      ctx.env.DB.prepare("INSERT OR IGNORE INTO project_members (project_id, user_id) SELECT ?, id FROM users WHERE id = ?").bind(id, m),
    ),
  ]);

  return json({ id, name, headRevision: 0, createdBy: ctx.user.id, createdAt: now }, 201);
});

route("GET", "/projects/:id", async (ctx, { id }) => {
  const p = await requireMember(ctx, id);
  const members = await ctx.env.DB.prepare(
    "SELECT u.id, u.display_name FROM project_members m JOIN users u ON u.id = m.user_id WHERE m.project_id = ? ORDER BY u.display_name",
  )
    .bind(id)
    .all<{ id: string; display_name: string }>();

  return json({
    id: p.id,
    name: p.name,
    headRevision: p.head_revision,
    createdBy: p.created_by,
    createdAt: p.created_at,
    members: members.results.map((m) => ({ id: m.id, displayName: m.display_name })),
  });
});

// 曲の削除（参加している人なら誰でも）。リビジョン・ロック・メンバーを消し、この曲だけが使っていたプロジェクト JSON の実体も消す。
// オーディオの実体は他の曲と共有している可能性があるので残す（コンテンツアドレスで重複しない）。
route("DELETE", "/projects/:id", async (ctx, { id }) => {
  await requireMember(ctx, id);

  const revs = await ctx.env.DB.prepare("SELECT project_json_hash FROM revisions WHERE project_id = ?").bind(id).all<{ project_json_hash: string }>();
  const jsonHashes = [...new Set(revs.results.map((r) => r.project_json_hash))];

  await ctx.env.DB.batch([
    ctx.env.DB.prepare("DELETE FROM lock_events WHERE project_id = ?").bind(id),
    ctx.env.DB.prepare("DELETE FROM locks WHERE project_id = ?").bind(id),
    ctx.env.DB.prepare("DELETE FROM revisions WHERE project_id = ?").bind(id),
    ctx.env.DB.prepare("DELETE FROM project_members WHERE project_id = ?").bind(id),
    ctx.env.DB.prepare("DELETE FROM projects WHERE id = ?").bind(id),
  ]);

  // ほかのリビジョンから参照されていないプロジェクト JSON だけ消す
  const stillUsed = await queryInChunks<{ h: string }>(
    ctx.env.DB,
    "SELECT DISTINCT project_json_hash AS h FROM revisions WHERE project_json_hash IN ({in})",
    jsonHashes,
  );
  const used = new Set(stillUsed.map((r) => r.h));
  const orphans = jsonHashes.filter((h) => !used.has(h));

  for (let i = 0; i < orphans.length; i += 50) {
    const chunk = orphans.slice(i, i + 50);
    await ctx.env.BLOBS.delete(chunk.map(blobKey));
    await ctx.env.DB.prepare(`DELETE FROM blobs WHERE hash IN (${chunk.map(() => "?").join(",")})`).bind(...chunk).run();
  }

  return json({ ok: true, deletedRevisions: revs.results.length, deletedBlobs: orphans.length });
});

// 曲名の変更（参加している人なら誰でも）
route("PATCH", "/projects/:id", async (ctx, { id }) => {
  await requireMember(ctx, id);
  const body = await readJson<{ name?: unknown }>(ctx.request);
  const name = requireString(body.name, "name", limits.projectName);

  await ctx.env.DB.prepare("UPDATE projects SET name = ? WHERE id = ?").bind(name, id).run();
  return json({ id, name });
});

// 参加をやめる（自分をメンバーから外す。作った人は削除を使う）
route("DELETE", "/projects/:id/members/me", async (ctx, { id }) => {
  const project = await requireMember(ctx, id);
  if (project.created_by === ctx.user.id) {
    throw new HttpError(400, "owner_cannot_leave", "作った人は参加をやめられません（曲を削除してください）");
  }

  await ctx.env.DB.batch([
    ctx.env.DB.prepare("DELETE FROM locks WHERE project_id = ? AND user_id = ?").bind(id, ctx.user.id),
    ctx.env.DB.prepare("DELETE FROM project_members WHERE project_id = ? AND user_id = ?").bind(id, ctx.user.id),
  ]);

  return json({ ok: true });
});

route("POST", "/projects/:id/members", async (ctx, { id }) => {
  await requireMember(ctx, id);
  const body = await readJson<{ userId?: unknown }>(ctx.request);
  const userId = typeof body.userId === "string" ? body.userId : "";
  const user = await ctx.env.DB.prepare("SELECT id FROM users WHERE id = ?").bind(userId).first();
  if (!user) throw new HttpError(404, "user_not_found", "ユーザーが見つかりません");

  await ctx.env.DB.prepare("INSERT OR IGNORE INTO project_members (project_id, user_id) VALUES (?, ?)").bind(id, userId).run();
  return json({ ok: true });
});
