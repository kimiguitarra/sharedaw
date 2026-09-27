#!/usr/bin/env node
// アプリの更新を同期サーバーに配信する（GitHub Actions から呼ぶ）。
//
//   node tools/publish-release.mjs <platform> <rootDir> <build> <version>
//
// rootDir 以下のファイルを 1 つずつ実体（SHA-256）としてアップロードし（サーバーにあるものは送らない）、
// ファイル一覧（マニフェスト）を登録する。アプリは一覧と手元のファイルを比べて、変わったものだけを取ってくる。
//
// 環境変数: SHAREDAW_SERVER_URL（同期サーバーの URL）、SHAREDAW_RELEASE_KEY（Worker のシークレット RELEASE_KEY と同じ値）

import { createHash } from "node:crypto";
import { createReadStream } from "node:fs";
import { lstat, readdir, readFile } from "node:fs/promises";
import path from "node:path";

const [platform, rootDir, buildText, version = ""] = process.argv.slice(2);
const server = (process.env.SHAREDAW_SERVER_URL ?? "").replace(/\/+$/, "");
const key = process.env.SHAREDAW_RELEASE_KEY ?? "";

if (!platform || !rootDir || !buildText) {
  console.error("usage: publish-release.mjs <platform> <rootDir> <build> <version>");
  process.exit(2);
}

if (!server || !key) {
  console.log("SHAREDAW_SERVER_URL / SHAREDAW_RELEASE_KEY が未設定のため、配信をスキップします");
  process.exit(0);
}

const build = Number(buildText);
const auth = { authorization: `Bearer ${key}` };

async function api(method, p, body) {
  const res = await fetch(server + p, {
    method,
    headers: { ...auth, ...(body !== undefined ? { "content-type": "application/json" } : {}) },
    body: body !== undefined ? JSON.stringify(body) : undefined,
  });
  const text = await res.text();
  let data;
  try {
    data = JSON.parse(text);
  } catch {
    data = text;
  }
  if (!res.ok) throw new Error(`${method} ${p}: HTTP ${res.status} ${typeof data === "string" ? data : JSON.stringify(data)}`);
  return data;
}

async function walk(dir, rel = "") {
  const out = [];
  for (const entry of await readdir(dir, { withFileTypes: true })) {
    const full = path.join(dir, entry.name);
    const relPath = rel ? `${rel}/${entry.name}` : entry.name;
    const st = await lstat(full);
    if (st.isSymbolicLink()) {
      console.warn(`skip symlink: ${relPath}`);
    } else if (st.isDirectory()) {
      out.push(...(await walk(full, relPath)));
    } else if (st.isFile()) {
      out.push({ full, path: relPath, size: st.size, executable: platform !== "windows" && (st.mode & 0o111) !== 0 });
    }
  }
  return out;
}

function hashFile(file) {
  return new Promise((resolve, reject) => {
    const h = createHash("sha256");
    createReadStream(file).on("data", (d) => h.update(d)).on("error", reject).on("end", () => resolve(h.digest("hex")));
  });
}

async function uploadData(transfer, data) {
  const res = await fetch(transfer.url, {
    method: "PUT",
    headers: { "content-type": "application/octet-stream", ...(transfer.authRequired ? auth : {}) },
    body: data,
  });
  if (!res.ok) throw new Error(`upload ${transfer.hash}: HTTP ${res.status} ${await res.text()}`);
  if (!transfer.authRequired) await api("POST", `/blobs/${transfer.hash}/complete`, {});
}

async function uploadMissing(items) {
  // items: [{ hash, read: () => Promise<Buffer> }]
  const byHash = new Map(items.map((i) => [i.hash, i]));
  const hashes = [...byHash.keys()];
  const missing = [];

  for (let i = 0; i < hashes.length; i += 200) {
    const r = await api("POST", "/blobs/check", { hashes: hashes.slice(i, i + 200) });
    missing.push(...r.missing);
  }

  console.log(`${hashes.length} files, ${missing.length} to upload`);
  let done = 0;

  const queue = [...missing];
  const workers = Array.from({ length: 4 }, async () => {
    for (let t = queue.shift(); t; t = queue.shift()) {
      await uploadData(t, await byHash.get(t.hash).read());
      if (++done % 25 === 0) console.log(`  uploaded ${done}/${missing.length}`);
    }
  });
  await Promise.all(workers);
}

const files = await walk(path.resolve(rootDir));
for (const f of files) f.hash = await hashFile(f.full);

await uploadMissing(files.map((f) => ({ hash: f.hash, read: () => readFile(f.full) })));

const manifest = JSON.stringify({
  platform,
  build,
  version,
  files: files.map(({ path: p, hash, size, executable }) => ({ path: p, hash, size, executable })),
});
const manifestHash = createHash("sha256").update(manifest).digest("hex");
await uploadMissing([{ hash: manifestHash, read: async () => Buffer.from(manifest) }]);

const info = await api("POST", "/app/releases", { platform, build, version, manifestHash, notes: process.env.SHAREDAW_RELEASE_NOTES ?? "" });
console.log(`published ${platform} build ${info.build} (${version})`);
