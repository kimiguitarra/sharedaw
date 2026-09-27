// ShareDAW 同期サーバー（仕様書 §6）。Cloudflare Workers + D1 + R2。

import { handleAdmin } from "./admin";
import { directDownload, directUpload, presignEnabled, registeredHashes, transferUrl, verifyAndRegister } from "./blobs";
import { ProjectJson, changedScopes, referencedBlobs, validateProject } from "./project";
import {
  Env,
  HttpError,
  User,
  blobKey,
  errorResponse,
  isReleasePlatform,
  isSha256,
  isUuid,
  json,
  nowIso,
  readJson,
  releaseKey,
  sha256Hex,
} from "./util";

type Handler = (ctx: Context, params: Record<string, string>) => Promise<Response>;

interface Context {
  env: Env;
  request: Request;
  url: URL;
  user: User;
}

const routes: Array<{ method: string; pattern: RegExp; keys: string[]; handler: Handler }> = [];

function route(method: string, path: string, handler: Handler) {
  const keys: string[] = [];
  const pattern = new RegExp(
    "^" + path.replace(/:([a-zA-Z]+)/g, (_, k: string) => (keys.push(k), "([^/]+)")) + "$",
  );
  routes.push({ method, pattern, keys, handler });
}

//==============================================================================
// 認証（§6.2）: Authorization: Bearer <token>。D1 にはトークンのハッシュのみ保存する。

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

async function requireMember(ctx: Context, projectId: string) {
  if (ctx.user.isRelease) throw new HttpError(403, "forbidden", "リリース用のキーではプロジェクトを操作できません");

  const project = await ctx.env.DB.prepare(
    `SELECT p.id, p.name, p.head_revision, p.created_by, p.created_at FROM projects p
     JOIN project_members m ON m.project_id = p.id AND m.user_id = ? WHERE p.id = ?`,
  )
    .bind(ctx.user.id, projectId)
    .first<{ id: string; name: string; head_revision: number; created_by: string; created_at: string }>();

  if (!project) throw new HttpError(404, "project_not_found", "プロジェクトが見つからないか、参加していません");
  return project;
}

async function loadProjectJson(env: Env, hash: string): Promise<unknown> {
  const obj = await env.BLOBS.get(blobKey(hash));
  if (!obj) throw new HttpError(400, "missing_blobs", "プロジェクト JSON がアップロードされていません", { hashes: [hash] });

  try {
    return JSON.parse(await obj.text());
  } catch {
    throw new HttpError(400, "invalid_project", "プロジェクト JSON を読み込めません");
  }
}

//==============================================================================
route("GET", "/me", async (ctx) => json({ id: ctx.user.id, displayName: ctx.user.displayName }));

route("GET", "/users", async (ctx) => {
  const rows = await ctx.env.DB.prepare("SELECT id, display_name FROM users ORDER BY display_name").all<{ id: string; display_name: string }>();
  return json(rows.results.map((r) => ({ id: r.id, displayName: r.display_name })));
});

route("GET", "/projects", async (ctx) => {
  const rows = await ctx.env.DB.prepare(
    `SELECT p.id, p.name, p.head_revision, p.created_by, p.created_at FROM projects p
     JOIN project_members m ON m.project_id = p.id WHERE m.user_id = ? ORDER BY p.created_at DESC`,
  )
    .bind(ctx.user.id)
    .all<{ id: string; name: string; head_revision: number; created_by: string; created_at: string }>();

  return json(rows.results.map((p) => ({ id: p.id, name: p.name, headRevision: p.head_revision, createdBy: p.created_by, createdAt: p.created_at })));
});

route("POST", "/projects", async (ctx) => {
  const body = await readJson<{ id?: string; name?: string; memberIds?: string[] }>(ctx.request);

  if (!isUuid(body.id)) throw new HttpError(400, "bad_request", "id（プロジェクト JSON の projectId）が必要です");
  if (typeof body.name !== "string" || !body.name.trim()) throw new HttpError(400, "bad_request", "name が必要です");

  const exists = await ctx.env.DB.prepare("SELECT id FROM projects WHERE id = ?").bind(body.id).first();
  if (exists) throw new HttpError(409, "project_exists", "同じ ID のプロジェクトが既にあります");

  const now = nowIso();
  const members = new Set([ctx.user.id, ...(body.memberIds ?? [])]);
  const statements = [
    ctx.env.DB.prepare("INSERT INTO projects (id, name, head_revision, created_by, created_at) VALUES (?, ?, 0, ?, ?)").bind(
      body.id,
      body.name.trim(),
      ctx.user.id,
      now,
    ),
    ...[...members].map((m) => ctx.env.DB.prepare("INSERT OR IGNORE INTO project_members (project_id, user_id) SELECT ?, id FROM users WHERE id = ?").bind(body.id, m)),
  ];
  await ctx.env.DB.batch(statements);

  return json({ id: body.id, name: body.name.trim(), headRevision: 0, createdBy: ctx.user.id, createdAt: now }, 201);
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

route("POST", "/projects/:id/members", async (ctx, { id }) => {
  await requireMember(ctx, id);
  const body = await readJson<{ userId?: string }>(ctx.request);
  const user = await ctx.env.DB.prepare("SELECT id FROM users WHERE id = ?").bind(body.userId ?? "").first();
  if (!user) throw new HttpError(404, "user_not_found", "ユーザーが見つかりません");

  await ctx.env.DB.prepare("INSERT OR IGNORE INTO project_members (project_id, user_id) VALUES (?, ?)").bind(id, body.userId).run();
  return json({ ok: true });
});

//==============================================================================
// リビジョン

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

route("GET", "/projects/:id/revisions/:n", async (ctx, { id, n }) => {
  await requireMember(ctx, id);
  const row = await ctx.env.DB.prepare("SELECT number, project_json_hash FROM revisions WHERE project_id = ? AND number = ?")
    .bind(id, Number(n))
    .first<{ number: number; project_json_hash: string }>();

  if (!row) throw new HttpError(404, "revision_not_found", "リビジョンが見つかりません");

  const download = await transferUrl(ctx.env, ctx.url.origin, row.project_json_hash, "GET");
  return json({ number: row.number, projectJsonHash: row.project_json_hash, download });
});

route("POST", "/projects/:id/revisions", async (ctx, { id }) => {
  const project = await requireMember(ctx, id);
  const body = await readJson<{ parentNumber?: number; message?: string; projectJsonHash?: string; changedTrackIds?: string[]; releaseLocks?: boolean }>(ctx.request);

  // 1. 親 = ヘッド
  if (body.parentNumber !== project.head_revision) {
    throw new HttpError(409, "not_head", "サーバーに新しいリビジョンがあります。先に取り込んでください", { head: project.head_revision });
  }

  if (!isSha256(body.projectJsonHash)) throw new HttpError(400, "bad_request", "projectJsonHash が必要です");

  const registered = await registeredHashes(ctx.env, [body.projectJsonHash]);
  if (!registered.has(body.projectJsonHash)) {
    throw new HttpError(400, "missing_blobs", "プロジェクト JSON がアップロードされていません", { hashes: [body.projectJsonHash] });
  }

  // 2. プロジェクト JSON の検証（JSON Schema、projectId）
  const next = (await loadProjectJson(ctx.env, body.projectJsonHash)) as ProjectJson;
  const schemaError = validateProject(next);
  if (schemaError) throw new HttpError(400, "invalid_project", `スキーマに適合しません: ${schemaError}`);
  if (next.projectId !== id) throw new HttpError(400, "invalid_project", "projectId が一致しません");

  // 3. 参照される実体がすべて存在すること
  const refs = referencedBlobs(next);
  const present = await registeredHashes(ctx.env, refs);
  const missing = refs.filter((h) => !present.has(h));
  if (missing.length > 0) throw new HttpError(400, "missing_blobs", "アップロードされていないオーディオがあります", { hashes: missing });

  // 4. 変更したスコープのロック（サーバー側で親と比較して判定する）
  let parent: ProjectJson | null = null;

  if (project.head_revision > 0) {
    const head = await ctx.env.DB.prepare("SELECT project_json_hash FROM revisions WHERE project_id = ? AND number = ?")
      .bind(id, project.head_revision)
      .first<{ project_json_hash: string }>();
    if (head) parent = (await loadProjectJson(ctx.env, head.project_json_hash)) as ProjectJson;
  }

  const changes = changedScopes(parent, next);
  const locks = await ctx.env.DB.prepare("SELECT track_id, user_id FROM locks WHERE project_id = ?").bind(id).all<{ track_id: string; user_id: string }>();
  const holder = new Map(locks.results.map((l) => [l.track_id, l.user_id]));

  const notLocked = changes.filter((c) => c.existedInParent && holder.get(c.id) !== ctx.user.id).map((c) => c.id);
  if (notLocked.length > 0) throw new HttpError(403, "lock_required", "ロックを持っていないトラックが変更されています", { trackIds: notLocked });

  const lockedByOthers = changes.filter((c) => !c.existedInParent && holder.has(c.id) && holder.get(c.id) !== ctx.user.id).map((c) => c.id);
  if (lockedByOthers.length > 0) throw new HttpError(409, "locked", "他の人がロックしているトラックがあります", { trackIds: lockedByOthers });

  // 5. リビジョンの登録とヘッドの更新（アトミック）
  const number = project.head_revision + 1;
  const now = nowIso();
  const [insert] = await ctx.env.DB.batch([
    ctx.env.DB.prepare(
      `INSERT INTO revisions (project_id, number, parent_number, author_id, message, project_json_hash, created_at)
       SELECT ?, ?, ?, ?, ?, ?, ? WHERE (SELECT head_revision FROM projects WHERE id = ?) = ?`,
    ).bind(id, number, project.head_revision, ctx.user.id, body.message ?? "", body.projectJsonHash, now, id, project.head_revision),
    ctx.env.DB.prepare("UPDATE projects SET head_revision = ? WHERE id = ? AND head_revision = ?").bind(number, id, project.head_revision),
  ]);

  if (insert.meta.changes !== 1) {
    throw new HttpError(409, "not_head", "サーバーに新しいリビジョンがあります。先に取り込んでください");
  }

  // 6. ロックの後始末: 新規トラックは作成者がロックを持つ（§4.2）。削除したトラックのロックは消す。必要なら解除する。
  const lockStatements: D1PreparedStatement[] = [];
  const isFirstRevision = parent === null;

  for (const c of changes) {
    if (c.deleted) {
      lockStatements.push(ctx.env.DB.prepare("DELETE FROM locks WHERE project_id = ? AND track_id = ?").bind(id, c.id));
      continue;
    }

    const release = body.releaseLocks === true || (isFirstRevision && c.kind !== "track");

    if (release) {
      if (holder.get(c.id) === ctx.user.id) {
        lockStatements.push(ctx.env.DB.prepare("DELETE FROM locks WHERE project_id = ? AND track_id = ? AND user_id = ?").bind(id, c.id, ctx.user.id));
        lockStatements.push(
          ctx.env.DB.prepare("INSERT INTO lock_events (project_id, track_id, user_id, action, created_at) VALUES (?, ?, ?, 'release', ?)").bind(id, c.id, ctx.user.id, now),
        );
      }
    } else if (!holder.has(c.id)) {
      lockStatements.push(ctx.env.DB.prepare("INSERT OR IGNORE INTO locks (project_id, track_id, user_id, acquired_at) VALUES (?, ?, ?, ?)").bind(id, c.id, ctx.user.id, now));
      lockStatements.push(
        ctx.env.DB.prepare("INSERT INTO lock_events (project_id, track_id, user_id, action, created_at) VALUES (?, ?, ?, 'acquire', ?)").bind(id, c.id, ctx.user.id, now),
      );
    }
  }

  if (lockStatements.length > 0) await ctx.env.DB.batch(lockStatements);

  return json({ number, head: number, changedTrackIds: changes.map((c) => c.id) }, 201);
});

//==============================================================================
// ロック（§4.2）

route("GET", "/projects/:id/locks", async (ctx, { id }) => {
  await requireMember(ctx, id);
  const rows = await ctx.env.DB.prepare(
    "SELECT l.track_id, l.user_id, u.display_name, l.acquired_at FROM locks l LEFT JOIN users u ON u.id = l.user_id WHERE l.project_id = ?",
  )
    .bind(id)
    .all<{ track_id: string; user_id: string; display_name: string | null; acquired_at: string }>();

  return json(rows.results.map((l) => ({ trackId: l.track_id, userId: l.user_id, displayName: l.display_name, acquiredAt: l.acquired_at })));
});

route("POST", "/projects/:id/locks", async (ctx, { id }) => {
  await requireMember(ctx, id);
  const body = await readJson<{ trackId?: string }>(ctx.request);
  if (typeof body.trackId !== "string" || !body.trackId) throw new HttpError(400, "bad_request", "trackId が必要です");

  const now = nowIso();
  const inserted = await ctx.env.DB.prepare("INSERT OR IGNORE INTO locks (project_id, track_id, user_id, acquired_at) VALUES (?, ?, ?, ?)")
    .bind(id, body.trackId, ctx.user.id, now)
    .run();

  const lock = await ctx.env.DB.prepare(
    "SELECT l.user_id, u.display_name, l.acquired_at FROM locks l LEFT JOIN users u ON u.id = l.user_id WHERE l.project_id = ? AND l.track_id = ?",
  )
    .bind(id, body.trackId)
    .first<{ user_id: string; display_name: string | null; acquired_at: string }>();

  if (lock && lock.user_id !== ctx.user.id) {
    throw new HttpError(409, "locked", `${lock.display_name ?? "他の人"} がロックしています`, {
      holder: { userId: lock.user_id, displayName: lock.display_name, acquiredAt: lock.acquired_at },
    });
  }

  if (inserted.meta.changes === 1) {
    await ctx.env.DB.prepare("INSERT INTO lock_events (project_id, track_id, user_id, action, created_at) VALUES (?, ?, ?, 'acquire', ?)")
      .bind(id, body.trackId, ctx.user.id, now)
      .run();
  }

  return json({ trackId: body.trackId, userId: ctx.user.id, displayName: ctx.user.displayName, acquiredAt: lock?.acquired_at ?? now });
});

route("DELETE", "/projects/:id/locks/:trackId", async (ctx, { id, trackId }) => {
  await requireMember(ctx, id);
  const force = ctx.url.searchParams.get("force") === "true";
  const lock = await ctx.env.DB.prepare("SELECT user_id FROM locks WHERE project_id = ? AND track_id = ?").bind(id, trackId).first<{ user_id: string }>();

  if (!lock) return json({ ok: true });

  if (lock.user_id !== ctx.user.id && !force) {
    throw new HttpError(403, "not_lock_holder", "他の人のロックです（強制解除するには force=true）");
  }

  const action = lock.user_id === ctx.user.id ? "release" : "force_release";
  await ctx.env.DB.batch([
    ctx.env.DB.prepare("DELETE FROM locks WHERE project_id = ? AND track_id = ?").bind(id, trackId),
    ctx.env.DB.prepare("INSERT INTO lock_events (project_id, track_id, user_id, action, created_at) VALUES (?, ?, ?, ?, ?)").bind(id, trackId, ctx.user.id, action, nowIso()),
  ]);

  return json({ ok: true, action });
});

route("GET", "/projects/:id/lock-events", async (ctx, { id }) => {
  await requireMember(ctx, id);
  const rows = await ctx.env.DB.prepare(
    `SELECT e.id, e.track_id, e.user_id, u.display_name, e.action, e.created_at FROM lock_events e
     LEFT JOIN users u ON u.id = e.user_id WHERE e.project_id = ? ORDER BY e.id DESC LIMIT 200`,
  )
    .bind(id)
    .all<{ id: number; track_id: string; user_id: string; display_name: string | null; action: string; created_at: string }>();

  return json(rows.results.map((e) => ({ id: e.id, trackId: e.track_id, userId: e.user_id, displayName: e.display_name, action: e.action, createdAt: e.created_at })));
});

//==============================================================================
// 実体（§6.4, §6.5）

route("POST", "/blobs/check", async (ctx) => {
  const body = await readJson<{ hashes?: string[] }>(ctx.request);
  const hashes = [...new Set((body.hashes ?? []).filter(isSha256))];
  const present = await registeredHashes(ctx.env, hashes);
  const missing = hashes.filter((h) => !present.has(h));

  return json({ missing: await Promise.all(missing.map((h) => transferUrl(ctx.env, ctx.url.origin, h, "PUT"))) });
});

route("POST", "/blobs/:hash/complete", async (ctx, { hash }) => {
  if (!isSha256(hash)) throw new HttpError(400, "bad_request", "ハッシュの形式が正しくありません");
  return json(await verifyAndRegister(ctx.env, hash));
});

route("GET", "/blobs/:hash", async (ctx, { hash }) => {
  if (!isSha256(hash)) throw new HttpError(400, "bad_request", "ハッシュの形式が正しくありません");
  const present = await registeredHashes(ctx.env, [hash]);
  if (!present.has(hash)) throw new HttpError(404, "not_found", "見つかりません");
  return json(await transferUrl(ctx.env, ctx.url.origin, hash, "GET"));
});

route("PUT", "/blobs/:hash/data", async (ctx, { hash }) => {
  if (!isSha256(hash)) throw new HttpError(400, "bad_request", "ハッシュの形式が正しくありません");
  return json(await directUpload(ctx.env, hash, ctx.request));
});

route("GET", "/blobs/:hash/data", async (ctx, { hash }) => {
  if (!isSha256(hash)) throw new HttpError(400, "bad_request", "ハッシュの形式が正しくありません");
  return directDownload(ctx.env, hash);
});

//==============================================================================
// アプリの更新（CI がファイルごとに実体としてアップロードし、一覧（マニフェスト）を登録する）

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

  const body = await readJson<{ platform?: string; build?: number; version?: string; manifestHash?: string; notes?: string }>(ctx.request);
  if (!isReleasePlatform(body.platform)) throw new HttpError(400, "bad_request", "platform が正しくありません");
  if (!Number.isInteger(body.build) || (body.build ?? 0) <= 0) throw new HttpError(400, "bad_request", "build（正の整数）が必要です");
  if (!isSha256(body.manifestHash)) throw new HttpError(400, "bad_request", "manifestHash が必要です");

  // マニフェストと、そこに書かれたファイルがすべてアップロード済みであること
  const manifestObj = await ctx.env.BLOBS.get(blobKey(body.manifestHash));
  if (!manifestObj) throw new HttpError(400, "missing_blobs", "マニフェストがアップロードされていません", { hashes: [body.manifestHash] });

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
    platform: body.platform,
    build: body.build!,
    version: body.version ?? "",
    manifestHash: body.manifestHash,
    notes: body.notes ?? "",
    createdAt: nowIso(),
  };

  await ctx.env.BLOBS.put(releaseKey(body.platform), JSON.stringify(info), { httpMetadata: { contentType: "application/json" } });
  await ctx.env.BLOBS.put(`app-releases/${body.platform}/${info.build}.json`, JSON.stringify(info));
  return json(info, 201);
});

//==============================================================================
// 動作確認（ブラウザでサーバー URL を開いたとき）。設定の抜けを表示する（値そのものは出さない）

async function healthCheck(env: Env): Promise<Response> {
  const lines: string[] = [];
  let ok = true;
  const check = async (label: string, fn: () => Promise<string | void>) => {
    try {
      const note = await fn();
      lines.push(`OK  ${label}${note ? `（${note}）` : ""}`);
    } catch (e) {
      ok = false;
      lines.push(`NG  ${label}: ${e instanceof Error ? e.message : String(e)}`);
    }
  };

  await check("D1 バインディング DB", async () => {
    if (!env.DB) throw new Error("バインディング DB がありません（Worker の「バインディング」で D1 を変数名 DB で追加）");
  });
  await check("D1 のテーブル", async () => {
    if (!env.DB) throw new Error("DB がないため確認できません");
    for (const table of ["users", "projects", "revisions", "locks", "blobs"])
      await env.DB.prepare(`SELECT COUNT(*) AS n FROM ${table}`).first().catch(() => {
        throw new Error(`テーブル ${table} がありません（migrations/0001_init.sql を D1 のコンソールで実行）`);
      });
  });
  await check("R2 バインディング BLOBS", async () => {
    if (!env.BLOBS) throw new Error("バインディング BLOBS がありません（Worker の「バインディング」で R2 を変数名 BLOBS で追加）");
    await env.BLOBS.head("health-check");
  });
  lines.push(`--  署名付き URL（R2 の API キー）: ${presignEnabled(env) ? "設定あり" : "なし（Worker 経由で転送。1 ファイル 100MB まで）"}`);
  lines.push(`--  ADMIN_PASSWORD: ${env.ADMIN_PASSWORD ? "設定あり" : "なし"}`);
  lines.push(`--  RELEASE_KEY: ${env.RELEASE_KEY ? "設定あり" : "なし"}`);

  const text = `ShareDAW sync server: ${ok ? "OK" : "設定に問題があります"}\n\n${lines.join("\n")}\n`;
  return new Response(text, { status: ok ? 200 : 500, headers: { "content-type": "text/plain; charset=utf-8" } });
}

//==============================================================================
export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    const url = new URL(request.url);

    try {
      // 動作確認用（ブラウザでサーバー URL を開いたとき）と、ユーザー作成の管理ページ
      if (url.pathname === "/" && request.method === "GET") return await healthCheck(env);

      if (url.pathname === "/admin") return await handleAdmin(request, env);

      for (const r of routes) {
        if (r.method !== request.method) continue;
        const m = r.pattern.exec(url.pathname);
        if (!m) continue;

        const params: Record<string, string> = {};
        r.keys.forEach((k, i) => (params[k] = decodeURIComponent(m[i + 1])));

        const user = await authenticate(env, request);

        if (user.isRelease && !url.pathname.startsWith("/blobs") && !url.pathname.startsWith("/app/"))
          throw new HttpError(403, "forbidden", "リリース用のキーで使えるのは更新の配信だけです");

        return await r.handler({ env, request, url, user }, params);
      }

      return json({ error: "not_found", message: "見つかりません" }, 404);
    } catch (e) {
      if (e instanceof HttpError) return errorResponse(e);
      console.error(e);
      return json({ error: "internal", message: "サーバーエラー" }, 500);
    }
  },
} satisfies ExportedHandler<Env>;
