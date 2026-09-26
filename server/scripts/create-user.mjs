#!/usr/bin/env node
// ユーザーを作成し、API トークンを発行する（仕様書 §6.2）。
//   node scripts/create-user.mjs <表示名> [--remote]
// トークンは一度だけ表示される。D1 にはハッシュのみ保存する。友人にはトークンとサーバー URL を渡す。
import { createHash, randomBytes, randomUUID } from "node:crypto";
import { execFileSync } from "node:child_process";

const args = process.argv.slice(2);
const name = args.find((a) => !a.startsWith("--"));
const remote = args.includes("--remote");

if (!name) {
  console.error("使い方: node scripts/create-user.mjs <表示名> [--remote]");
  process.exit(1);
}

const id = randomUUID();
const token = randomBytes(32).toString("base64url");
const hash = createHash("sha256").update(token).digest("hex");
const now = new Date().toISOString();
const escaped = name.replaceAll("'", "''");
const sql = `INSERT INTO users (id, display_name, token_hash, created_at) VALUES ('${id}', '${escaped}', '${hash}', '${now}');`;

execFileSync("npx", ["wrangler", "d1", "execute", "sharedaw-sync", remote ? "--remote" : "--local", "--command", sql], { stdio: "inherit" });

console.log("\nユーザーを作成しました");
console.log(`  ID:     ${id}`);
console.log(`  名前:   ${name}`);
console.log(`  トークン（この画面でしか表示されません）: ${token}`);
