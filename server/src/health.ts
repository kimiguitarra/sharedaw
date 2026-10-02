// 動作確認（ブラウザでサーバー URL を開いたとき）。設定の抜けを表示する（値そのものは出さない）

import { presignEnabled } from "./blobs";
import { Env } from "./util";

const tables = ["users", "projects", "project_members", "revisions", "locks", "lock_events", "blobs"];

export async function healthCheck(env: Env): Promise<Response> {
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
    for (const table of tables)
      await env.DB.prepare(`SELECT COUNT(*) AS n FROM ${table}`).first().catch(() => {
        throw new Error(`テーブル ${table} がありません（migrations/ の SQL を D1 のコンソールで実行）`);
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
