// リビジョン（push・pull）

import { registeredHashes, transferUrl } from "../blobs";
import { ProjectJson, changedScopes, referencedBlobs, validateProject } from "../project";
import { Context, requireMember, route } from "../router";
import { Env, HttpError, blobKey, json, limits, nowIso, optionalString, readJson, requirePositiveInt, requireSha256 } from "../util";

async function loadProjectJson(env: Env, hash: string): Promise<ProjectJson> {
  const obj = await env.BLOBS.get(blobKey(hash));
  if (!obj) throw new HttpError(400, "missing_blobs", "プロジェクト JSON がアップロードされていません", { hashes: [hash] });

  try {
    return JSON.parse(await obj.text()) as ProjectJson;
  } catch {
    throw new HttpError(400, "invalid_project", "プロジェクト JSON を読み込めません");
  }
}

/** アップロード済みで、スキーマに合い、この曲のもので、参照するオーディオがすべてあるプロジェクト JSON を読む。 */
async function loadValidProject(env: Env, projectId: string, hash: string): Promise<ProjectJson> {
  if (!(await registeredHashes(env, [hash])).has(hash))
    throw new HttpError(400, "missing_blobs", "プロジェクト JSON がアップロードされていません", { hashes: [hash] });

  const next = await loadProjectJson(env, hash);
  const schemaError = validateProject(next);
  if (schemaError) throw new HttpError(400, "invalid_project", `スキーマに適合しません: ${schemaError}`);
  if (next.projectId !== projectId) throw new HttpError(400, "invalid_project", "projectId が一致しません");

  const refs = referencedBlobs(next);
  const present = await registeredHashes(env, refs);
  const missing = refs.filter((h) => !present.has(h));
  if (missing.length > 0) throw new HttpError(400, "missing_blobs", "アップロードされていないオーディオがあります", { hashes: missing });

  return next;
}

/** いまのヘッドのプロジェクト JSON（まだリビジョンがなければ null）。 */
async function loadHead(ctx: Context, projectId: string, head: number): Promise<ProjectJson | null> {
  if (head <= 0) return null;

  const row = await ctx.env.DB.prepare("SELECT project_json_hash FROM revisions WHERE project_id = ? AND number = ?")
    .bind(projectId, head)
    .first<{ project_json_hash: string }>();

  // ヘッドの番号はあるのにリビジョンがない（データの不整合）。黙って null にすると変更の一覧が狂うので止める
  if (!row) throw new HttpError(500, "head_missing", "ヘッドのリビジョンが見つかりません");
  return loadProjectJson(ctx.env, row.project_json_hash);
}

route("GET", "/projects/:id/revisions", async (ctx, { id }) => {
  await requireMember(ctx, id);
  const rows = await ctx.env.DB.prepare(
    `SELECT r.number, r.parent_number, r.author_id, u.display_name, r.message, r.project_json_hash, r.created_at
     FROM revisions r LEFT JOIN users u ON u.id = r.author_id WHERE r.project_id = ? ORDER BY r.number DESC`,
  )
    .bind(id)
    .all<{ number: number; parent_number: number; author_id: string; display_name: string | null; message: string; project_json_hash: string; created_at: string }>();

  return json(
    rows.results.map((r) => ({
      number: r.number,
      parentNumber: r.parent_number,
      authorId: r.author_id,
      authorName: r.display_name,
      message: r.message,
      projectJsonHash: r.project_json_hash,
      createdAt: r.created_at,
    })),
  );
});

/**
 * そのトラック（など）を最後に変えたリビジョン。新しい順にプロジェクト JSON を比べて探す（R2 から読むのでアプリでたどるより速い）。
 * 見つからなければ found: false（古すぎる）。最新の版になければ onServer: false。
 */
route("GET", "/projects/:id/scopes/:scope/last-change", async (ctx, { id, scope }) => {
  await requireMember(ctx, id);
  const rows = await ctx.env.DB.prepare(
    `SELECT r.number, u.display_name, r.message, r.project_json_hash, r.created_at
     FROM revisions r LEFT JOIN users u ON u.id = r.author_id WHERE r.project_id = ? ORDER BY r.number DESC LIMIT 300`,
  )
    .bind(id)
    .all<{ number: number; display_name: string | null; message: string | null; project_json_hash: string; created_at: string }>();

  const list = rows.results;
  const none = { onServer: false, found: false };
  if (list.length === 0) return json(none);

  const loaded = new Map<string, ProjectJson>();
  const load = async (i: number) => {
    const hash = list[i].project_json_hash;
    let p = loaded.get(hash);
    if (!p) {
      p = await loadProjectJson(ctx.env, hash);
      loaded.set(hash, p);
    }
    return p;
  };

  if (!changedScopes(null, await load(0)).some((c) => c.id === scope)) return json(none);

  for (let i = 0; i < list.length; i++) {
    const current = await load(i);
    const older = i + 1 < list.length ? await load(i + 1) : null;
    const change = changedScopes(older, current).find((c) => c.id === scope && !c.deleted);

    if (change) {
      const r = list[i];
      return json({
        onServer: true,
        found: true,
        created: !change.existedInParent,
        revision: r.number,
        authorName: r.display_name,
        message: r.message ?? "",
        createdAt: r.created_at,
      });
    }

    // いちばん古い所までは見たが、そのリビジョンで初めてできたわけでもない（LIMIT で途中まで）
    if (older === null) break;
  }

  return json({ onServer: true, found: false });
});

route("GET", "/projects/:id/revisions/:n", async (ctx, { id, n }) => {
  await requireMember(ctx, id);
  const row = await ctx.env.DB.prepare("SELECT number, project_json_hash FROM revisions WHERE project_id = ? AND number = ?")
    .bind(id, requirePositiveInt(n, "リビジョン番号"))
    .first<{ number: number; project_json_hash: string }>();

  if (!row) throw new HttpError(404, "revision_not_found", "リビジョンが見つかりません");

  const download = await transferUrl(ctx.env, ctx.url.origin, row.project_json_hash, "GET");
  return json({ number: row.number, projectJsonHash: row.project_json_hash, download });
});

// アプリは changedTrackIds も送ってくるが、変わった所はサーバーで比べて決めるので使わない（古いアプリとの互換のため受け取るだけ）
route("POST", "/projects/:id/revisions", async (ctx, { id }) => {
  const project = await requireMember(ctx, id);
  const body = await readJson<{ parentNumber?: unknown; message?: unknown; projectJsonHash?: unknown }>(ctx.request);

  // 1. 親 = ヘッド
  if (body.parentNumber !== project.head_revision) {
    throw new HttpError(409, "not_head", "サーバーに新しいリビジョンがあります。先に取り込んでください", { head: project.head_revision });
  }

  const hash = requireSha256(body.projectJsonHash, "projectJsonHash");
  const message = optionalString(body.message, "message", limits.message);

  // 2. プロジェクト JSON の検証（アップロード済み、スキーマ、projectId、参照されるオーディオ）
  const next = await loadValidProject(ctx.env, id, hash);

  // 3. 変わったスコープ（応答用。ロックはない。競合はアプリがトラックごとに利用者に選んでもらって解決する）
  const changes = changedScopes(await loadHead(ctx, id, project.head_revision), next);

  // 4. リビジョンの登録とヘッドの更新（アトミック）
  const number = project.head_revision + 1;
  const [insert] = await ctx.env.DB.batch([
    ctx.env.DB.prepare(
      `INSERT INTO revisions (project_id, number, parent_number, author_id, message, project_json_hash, created_at)
       SELECT ?, ?, ?, ?, ?, ?, ? WHERE (SELECT head_revision FROM projects WHERE id = ?) = ?`,
    ).bind(id, number, project.head_revision, ctx.user.id, message, hash, nowIso(), id, project.head_revision),
    ctx.env.DB.prepare("UPDATE projects SET head_revision = ? WHERE id = ? AND head_revision = ?").bind(number, id, project.head_revision),
  ]);

  if (insert.meta.changes !== 1) {
    throw new HttpError(409, "not_head", "サーバーに新しいリビジョンがあります。先に取り込んでください");
  }

  return json({ number, head: number, changedTrackIds: changes.map((c) => c.id) }, 201);
});
