// トラックのロック（§4.2。いまのアプリは使っていないが、古いアプリとの互換のため残す）

import { requireMember, route } from "../router";
import { HttpError, json, limits, nowIso, readJson, requireString } from "../util";

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
  const body = await readJson<{ trackId?: unknown }>(ctx.request);
  const trackId = requireString(body.trackId, "trackId", limits.trackId);

  const now = nowIso();
  const inserted = await ctx.env.DB.prepare("INSERT OR IGNORE INTO locks (project_id, track_id, user_id, acquired_at) VALUES (?, ?, ?, ?)")
    .bind(id, trackId, ctx.user.id, now)
    .run();

  const lock = await ctx.env.DB.prepare(
    "SELECT l.user_id, u.display_name, l.acquired_at FROM locks l LEFT JOIN users u ON u.id = l.user_id WHERE l.project_id = ? AND l.track_id = ?",
  )
    .bind(id, trackId)
    .first<{ user_id: string; display_name: string | null; acquired_at: string }>();

  if (lock && lock.user_id !== ctx.user.id) {
    throw new HttpError(409, "locked", `${lock.display_name ?? "他の人"} がロックしています`, {
      holder: { userId: lock.user_id, displayName: lock.display_name, acquiredAt: lock.acquired_at },
    });
  }

  if (inserted.meta.changes === 1) {
    await ctx.env.DB.prepare("INSERT INTO lock_events (project_id, track_id, user_id, action, created_at) VALUES (?, ?, ?, 'acquire', ?)")
      .bind(id, trackId, ctx.user.id, now)
      .run();
  }

  return json({ trackId, userId: ctx.user.id, displayName: ctx.user.displayName, acquiredAt: lock?.acquired_at ?? now });
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
