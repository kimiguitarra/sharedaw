import { SELF, env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import fullFixture from "../../shared/fixtures/full.project.json";
import minimalFixture from "../../shared/fixtures/minimal.project.json";

const ORIGIN = "https://sync.example";

async function sha256(text: string | Uint8Array) {
  const data = typeof text === "string" ? new TextEncoder().encode(text) : text;
  const d = await crypto.subtle.digest("SHA-256", data);
  return [...new Uint8Array(d)].map((b) => b.toString(16).padStart(2, "0")).join("");
}

async function addUser(id: string, name: string, token: string) {
  await env.DB.prepare("INSERT INTO users (id, display_name, token_hash, created_at) VALUES (?, ?, ?, ?)")
    .bind(id, name, await sha256(token), new Date().toISOString())
    .run();
}

function api(token: string) {
  return async (method: string, path: string, body?: unknown, raw?: BodyInit) => {
    const res = await SELF.fetch(ORIGIN + path, {
      method,
      headers: { authorization: `Bearer ${token}`, ...(body !== undefined ? { "content-type": "application/json" } : {}) },
      body: raw ?? (body !== undefined ? JSON.stringify(body) : undefined),
    });
    const text = await res.text();
    let data: any = text;
    try {
      data = JSON.parse(text);
    } catch {}
    return { status: res.status, data };
  };
}

const alice = api("alice-token");
const bob = api("bob-token");

/** 実体をアップロードする（開発用の直接転送）。ハッシュを返す。 */
async function upload(call: ReturnType<typeof api>, content: string | Uint8Array) {
  const hash = await sha256(content);
  const check = await call("POST", "/blobs/check", { hashes: [hash] });
  expect(check.status).toBe(200);

  for (const m of check.data.missing) {
    expect(m.authRequired).toBe(true);
    const put = await call("PUT", `/blobs/${m.hash}/data`, undefined, content);
    expect(put.status).toBe(200);
  }

  return hash;
}

async function push(call: ReturnType<typeof api>, projectId: string, project: unknown, parentNumber: number, extra: object = {}) {
  const hash = await upload(call, JSON.stringify(project));
  return call("POST", `/projects/${projectId}/revisions`, { parentNumber, message: "test", projectJsonHash: hash, ...extra });
}

const clone = <T>(x: T): T => JSON.parse(JSON.stringify(x));

beforeEach(async () => {
  for (const table of ["users", "projects", "project_members", "revisions", "locks", "lock_events", "blobs"])
    await env.DB.prepare(`DELETE FROM ${table}`).run();

  const listed = await env.BLOBS.list();
  for (const o of listed.objects) await env.BLOBS.delete(o.key);

  await addUser("u-alice", "Alice", "alice-token");
  await addUser("u-bob", "Bob", "bob-token");
});

describe("auth", () => {
  it("rejects missing or wrong tokens", async () => {
    expect((await SELF.fetch(ORIGIN + "/me")).status).toBe(401);
    expect((await api("nope")("GET", "/me")).status).toBe(401);
    const me = await alice("GET", "/me");
    expect(me.data).toEqual({ id: "u-alice", displayName: "Alice" });
  });
});

describe("projects and revisions", () => {
  const pid = minimalFixture.projectId;

  beforeEach(async () => {
    const r = await alice("POST", "/projects", { id: pid, name: "テスト曲", memberIds: ["u-bob"] });
    expect(r.status).toBe(201);
  });

  it("lists projects for members", async () => {
    const list = await bob("GET", "/projects");
    expect(list.data.map((p: any) => p.id)).toEqual([pid]);
    const info = await alice("GET", `/projects/${pid}`);
    expect(info.data.headRevision).toBe(0);
    expect(info.data.members.map((m: any) => m.displayName)).toEqual(["Alice", "Bob"]);
  });

  it("pushes the first revision and serves it back", async () => {
    const r = await push(alice, pid, minimalFixture, 0);
    expect(r.status).toBe(201);
    expect(r.data.number).toBe(1);

    const listed = await bob("GET", "/projects");
    expect(listed.data[0]).toMatchObject({ id: pid, headRevision: 1, updatedBy: "Alice" });
    expect(typeof listed.data[0].updatedAt).toBe("string");

    const revs = await bob("GET", `/projects/${pid}/revisions`);
    expect(revs.data[0]).toMatchObject({ number: 1, parentNumber: 0, authorName: "Alice", message: "test" });

    const rev = await bob("GET", `/projects/${pid}/revisions/1`);
    const dl = await bob("GET", new URL(rev.data.download.url).pathname);
    expect(dl.data.projectId).toBe(pid);

    // 最初の push ではテンポ・拍子・コードのロックは持たない
    const locks = await alice("GET", `/projects/${pid}/locks`);
    expect(locks.data).toEqual([]);
  });

  it("lets any member delete a song, and removes its revisions and project JSON", async () => {
    const r = await push(alice, pid, minimalFixture, 0);
    expect(r.status).toBe(201);
    const rev = await alice("GET", `/projects/${pid}/revisions/1`);
    const jsonHash = rev.data.projectJsonHash;

    const ok = await bob("DELETE", `/projects/${pid}`);
    expect(ok.status).toBe(200);
    expect(ok.data).toMatchObject({ ok: true, deletedRevisions: 1, deletedBlobs: 1 });

    expect((await alice("GET", "/projects")).data).toEqual([]);
    expect((await alice("GET", `/projects/${pid}`)).status).toBe(404);
    expect(await env.BLOBS.head(`blobs/${jsonHash}`)).toBeNull();
    const row = await env.DB.prepare("SELECT hash FROM blobs WHERE hash = ?").bind(jsonHash).first();
    expect(row).toBeNull();

    // 同じ ID でまた作れる
    expect((await alice("POST", "/projects", { id: pid, name: "again" })).status).toBe(201);
  });

  it("renames a song", async () => {
    const r = await bob("PATCH", `/projects/${pid}`, { name: "  新しい名前  " });
    expect(r.status).toBe(200);
    expect((await alice("GET", "/projects")).data[0].name).toBe("新しい名前");
    expect((await alice("PATCH", `/projects/${pid}`, { name: " " })).status).toBe(400);
  });

  it("lets a member leave a song, but not its creator", async () => {
    expect((await alice("DELETE", `/projects/${pid}/members/me`)).status).toBe(400);

    const left = await bob("DELETE", `/projects/${pid}/members/me`);
    expect(left.status).toBe(200);
    expect((await bob("GET", "/projects")).data).toEqual([]);
    expect((await alice("GET", "/projects")).data.length).toBe(1);
  });

  it("rejects a push whose parent is not the head", async () => {
    await push(alice, pid, minimalFixture, 0);
    const r = await push(bob, pid, { ...minimalFixture, name: "changed" }, 0);
    expect(r.status).toBe(409);
    expect(r.data).toMatchObject({ error: "not_head", head: 1 });
  });

  it("rejects invalid project JSON and mismatched projectId", async () => {
    const bad = clone(minimalFixture) as any;
    bad.ppq = 480;
    expect((await push(alice, pid, bad, 0)).data.error).toBe("invalid_project");

    const other = clone(minimalFixture);
    other.projectId = "11111111-1111-4111-8111-111111111199";
    expect((await push(alice, pid, other, 0)).data.error).toBe("invalid_project");
  });

  it("lets any member change the tempo", async () => {
    await push(alice, pid, minimalFixture, 0);

    const changed = clone(minimalFixture);
    changed.tempoTrack.events[0].bpm = 90;

    // ロックはない。親がヘッドなら誰でも変えられる
    const r = await push(bob, pid, changed, 1);
    expect(r.status).toBe(201);
    expect(r.data.changedTrackIds).toEqual([minimalFixture.tempoTrack.id]);
  });

  it("treats the key track as its own scope", async () => {
    const keyId = "33333333-3333-4333-8333-333333333333";
    const withKey = clone(minimalFixture) as any;
    withKey.keyTrack = { id: keyId, events: [{ id: "33333333-3333-4333-8333-3333333333aa", bar: 1, tonic: 7, mode: "major" }] };
    expect((await push(alice, pid, withKey, 0)).status).toBe(201);

    const changed = clone(withKey);
    changed.keyTrack.events[0].mode = "minor";
    const r = await push(bob, pid, changed, 1);
    expect(r.status).toBe(201);
    expect(r.data.changedTrackIds).toEqual([keyId]);

    const bad = clone(withKey);
    bad.keyTrack.events[0].tonic = 12;
    expect((await push(alice, pid, bad, 2)).data.error).toBe("invalid_project");
  });

  it("treats the master limiter as its own scope", async () => {
    const masterId = "22222222-2222-4222-8222-222222222222";
    const withMaster = clone(minimalFixture) as any;
    withMaster.master = { id: masterId, limiter: { enabled: true, thresholdDb: -6, ceilingDb: -1, character: 5, mode: "tube" } };
    expect((await push(alice, pid, withMaster, 0)).status).toBe(201);

    const changed = clone(withMaster);
    changed.master.limiter.thresholdDb = -8;
    expect((await push(bob, pid, changed, 1)).status).toBe(201);

    const bad = clone(changed);
    bad.master.limiter.mode = "brickwall";
    expect((await push(bob, pid, bad, 2)).data.error).toBe("invalid_project");
  });
});

describe("tracks, blobs and locks", () => {
  const pid = fullFixture.projectId;
  const drums = fullFixture.tracks[0].id;
  const gt = fullFixture.tracks[2].id;

  beforeEach(async () => {
    await alice("POST", "/projects", { id: pid, name: "フル", memberIds: ["u-bob"] });
  });

  it("requires referenced audio to be uploaded first", async () => {
    const r = await push(alice, pid, fullFixture, 0);
    expect(r.status).toBe(400);
    expect(r.data.error).toBe("missing_blobs");
    expect(r.data.hashes.sort()).toEqual(["a".repeat(64), "c".repeat(64)]);
  });

  it("lets any member change and delete tracks without locks", async () => {
    // フィクスチャが参照するハッシュに合う内容は用意できないので、参照を実際の実体に差し替える
    const project = clone(fullFixture) as any;
    const audio = await upload(alice, new Uint8Array([1, 2, 3, 4]));
    const render = await upload(alice, new Uint8Array([5, 6, 7]));
    project.tracks[2].clips[0].audioHash = audio;
    project.tracks[1].render.audioHash = render;

    expect((await push(alice, pid, project, 0)).status).toBe(201);
    expect((await alice("GET", `/projects/${pid}/locks`)).data).toEqual([]);

    const bobs = clone(project);
    bobs.tracks[0].volumeDb = -10;
    const changed = await push(bob, pid, bobs, 1);
    expect(changed.status).toBe(201);
    expect(changed.data.changedTrackIds).toEqual([drums]);

    const deleted = clone(bobs);
    deleted.tracks = deleted.tracks.filter((t: any) => t.id !== gt);
    expect((await push(bob, pid, deleted, 2)).status).toBe(201);

    // 古い親からの push は競合（アプリがトラックごとに選んでもらってから、ヘッドを親にして push し直す）
    expect((await push(alice, pid, project, 2)).status).toBe(409);
  });

  it("verifies uploaded blob hashes", async () => {
    const wrongHash = "0".repeat(64);
    const put = await alice("PUT", `/blobs/${wrongHash}/data`, undefined, "hello");
    expect(put.status).toBe(400);
    expect(put.data.error).toBe("hash_mismatch");

    // 署名付き URL で直接アップロードした場合の完了通知
    const content = "direct upload";
    const hash = await sha256(content);
    await env.BLOBS.put(`blobs/${hash}`, content);
    const done = await alice("POST", `/blobs/${hash}/complete`);
    expect(done.data).toEqual({ hash, size: content.length });
    expect((await alice("POST", "/blobs/check", { hashes: [hash] })).data.missing).toEqual([]);

    await env.BLOBS.put(`blobs/${wrongHash}`, "tampered");
    expect((await alice("POST", `/blobs/${wrongHash}/complete`)).data.error).toBe("hash_mismatch");
    expect(await env.BLOBS.get(`blobs/${wrongHash}`)).toBeNull();
  });
});

describe("admin page", () => {
  const post = (fields: Record<string, string>) =>
    SELF.fetch(ORIGIN + "/admin", { method: "POST", body: new URLSearchParams(fields) });

  it("answers the health check without a token", async () => {
    const res = await SELF.fetch(ORIGIN + "/");
    expect(res.status).toBe(200);
    expect(await res.text()).toContain("OK");
  });

  it("rejects a wrong password", async () => {
    const res = await post({ password: "nope", name: "Carol" });
    expect(res.status).toBe(403);
    const count = await env.DB.prepare("SELECT COUNT(*) AS n FROM users").first<{ n: number }>();
    expect(count?.n).toBe(2);
  });

  it("creates a user whose token works and joins existing projects", async () => {
    const project = clone(minimalFixture) as any;
    expect((await alice("POST", "/projects", { id: project.projectId, name: "Song", memberIds: [] })).status).toBe(201);

    const res = await post({ password: "test-admin-password", name: "Carol", joinAll: "on" });
    expect(res.status).toBe(200);
    const html = await res.text();
    const token = /<div class="token">([^<]+)<\/div>/.exec(html)?.[1];
    expect(token).toBeTruthy();

    const carol = api(token!);
    const me = await carol("GET", "/me");
    expect(me.status).toBe(200);
    expect(me.data.displayName).toBe("Carol");
    expect((await carol("GET", "/projects")).data.map((p: any) => p.id)).toContain(project.projectId);
  });
});

describe("app updates", () => {
  const release = api("test-release-key");

  it("lets only the release key publish, and users fetch the latest", async () => {
    // まだ何もない
    expect((await alice("GET", "/app/latest?platform=windows")).status).toBe(404);

    // ファイルとマニフェストをアップロード（リリース用のキーでも実体は送れる）
    const exe = await upload(release, "fake exe v2");
    const manifest = JSON.stringify({ build: 2, files: [{ path: "ShareDAW.exe", hash: exe, size: 11 }] });
    const manifestHash = await upload(release, manifest);

    const body = { platform: "windows", build: 2, version: "0.2.0", manifestHash };
    expect((await alice("POST", "/app/releases", body)).status).toBe(403);
    expect((await release("POST", "/app/releases", body)).status).toBe(201);

    const latest = await alice("GET", "/app/latest?platform=windows");
    expect(latest.status).toBe(200);
    expect(latest.data.build).toBe(2);
    expect(latest.data.manifest.hash).toBe(manifestHash);
    expect((await alice("GET", "/app/latest?platform=mac")).status).toBe(404);
  });

  it("rejects releases whose files are missing, and keeps the key away from projects", async () => {
    const manifest = JSON.stringify({ build: 3, files: [{ path: "a", hash: "0".repeat(64) }] });
    const manifestHash = await upload(release, manifest);
    const r = await release("POST", "/app/releases", { platform: "mac", build: 3, manifestHash });
    expect(r.status).toBe(400);
    expect(r.data.error).toBe("missing_blobs");

    expect((await release("GET", "/projects")).status).toBe(403);
    expect((await release("GET", "/users")).status).toBe(403);
  });
});

describe("input validation", () => {
  const release = api("test-release-key");

  it("answers malformed input with 400 instead of a server error", async () => {
    const id = "11111111-2222-4333-8444-555555555555";
    expect((await alice("GET", "/projects/%E0%A4%A")).status).toBe(400);
    expect((await alice("POST", "/projects", { id, name: "x".repeat(201) })).status).toBe(400);
    expect((await alice("POST", "/projects", { id, name: "Song", memberIds: "u-bob" })).status).toBe(400);
    expect((await alice("POST", "/projects", { id, name: "Song", memberIds: [1] })).status).toBe(400);
    expect((await alice("POST", "/projects", { id, name: "Song" })).status).toBe(201);
    expect((await alice("GET", `/projects/${id}/revisions/abc`)).status).toBe(400);
    expect((await alice("POST", `/projects/${id}/locks`, { trackId: "t".repeat(201) })).status).toBe(400);
    expect((await alice("POST", "/blobs/check", { hashes: "nope" })).status).toBe(400);

    const many = Array.from({ length: 1001 }, (_, i) => i.toString(16).padStart(64, "0"));
    expect((await alice("POST", "/blobs/check", { hashes: many })).status).toBe(400);

    const project = clone(minimalFixture) as any;
    project.projectId = id;
    const hash = await upload(alice, JSON.stringify(project));
    const bad = await alice("POST", `/projects/${id}/revisions`, { parentNumber: 0, message: 42, projectJsonHash: hash });
    expect(bad.status).toBe(400);
  });

  it("does not let an older build replace the release, but accepts the same build again", async () => {
    const publish = async (build: number) => {
      const exe = await upload(release, `exe ${build}`);
      const manifestHash = await upload(release, JSON.stringify({ build, files: [{ path: "ShareDAW", hash: exe }] }));
      return release("POST", "/app/releases", { platform: "linux", build, version: "x", manifestHash });
    };

    expect((await publish(5)).status).toBe(201);
    expect((await publish(5)).status).toBe(201);   // 配信だけやり直す
    const older = await publish(4);
    expect(older.status).toBe(409);
    expect(older.data.current).toBe(5);
    expect((await alice("GET", "/app/latest?platform=linux")).data.build).toBe(5);
  });
});
