// dist/worker.js を作る: Cloudflare のダッシュボードの「コードを編集」に貼り付けて使う 1 ファイル版（npm run bundle）。
// Cloudflare の GitHub 連携はアプリのサブモジュール（JUCE 等）を取得できずに失敗するため、この方法でデプロイする。
import { mkdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";

const code = readFileSync("dist-tmp/index.js", "utf8").replace(/\n\/\/# sourceMappingURL=.*\n?$/, "\n");
// @ts-nocheck: ダッシュボードのエディタが型の警告（ライブラリ内の location 等。実行には影響しない）を出さないように
const header = `// @ts-nocheck
// ShareDAW 同期サーバー（Cloudflare Worker）— 自動生成ファイル。直接編集しないこと（server/ で npm run bundle）。
// Cloudflare のダッシュボードで Worker の「コードを編集」を開き、このファイルの中身をすべて貼り付けてデプロイする。
`;
mkdirSync("dist", { recursive: true });
writeFileSync("dist/worker.js", header + code);
rmSync("dist-tmp", { recursive: true, force: true });
console.log("wrote dist/worker.js");
