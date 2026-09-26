# 同期サーバー（Cloudflare Workers + D1 + R2）

仕様書 §6 の同期サーバー。既存の疑似Google Drive とは別の、作業スペース専用の Worker / D1 / R2 を使う。

## 構成

| ファイル | 内容 |
|---|---|
| `src/index.ts` | API（認証、プロジェクト、リビジョン、ロック、実体） |
| `src/project.ts` | プロジェクト JSON の検証（`/shared/schema`）、スコープ単位の変更検出、参照している実体 |
| `src/blobs.ts` | R2 の署名付き URL、アップロード後のハッシュ検証 |
| `migrations/` | D1 スキーマ |
| `scripts/create-user.mjs` | ユーザー作成・トークン発行 |

## API

仕様書 §6.5 のとおり。すべて `Authorization: Bearer <token>`。追加したもの:

- `GET /me`、`GET /users`、`POST /projects/:id/members {userId}`
- `POST /blobs/:hash/complete` … 署名付き URL でアップロードした後に呼ぶ。サーバーがハッシュとサイズを検証して登録する
- `GET /projects/:id/lock-events` … ロックの履歴（強制解除の記録）
- `POST /projects/:id/revisions` は `releaseLocks: true` で、push したトラックのロックを解除する

push の検証（サーバー側）:
1. 親リビジョン = ヘッド（違えば 409 `not_head`）
2. プロジェクト JSON が JSON Schema に適合し、projectId が一致する
3. 参照しているオーディオがすべてアップロード済み（違えば 400 `missing_blobs` とハッシュ一覧）
4. 親と比べて変わったスコープ（トラック・テンポ・拍子・コード。削除を含む）のロックを持っている
   （新しいトラックは作成者が自動でロックを持つ。最初のリビジョンのテンポ・拍子・コードはロックしない）
5. リビジョン登録とヘッド更新をアトミックに行う

## セットアップ

```bash
cd server
npm install
npx wrangler login
npx wrangler d1 create sharedaw-sync          # 表示された database_id を wrangler.jsonc に書く
npx wrangler r2 bucket create sharedaw-sync-blobs
npx wrangler d1 migrations apply sharedaw-sync --remote
npx wrangler deploy
```

大きなオーディオを Worker を経由せずに送るため、R2 の S3 互換 API トークンを作成して設定する
（Cloudflare ダッシュボード → R2 → API トークンの管理 → 対象バケットの「オブジェクトの読み取りと書き込み」）:

```bash
# wrangler.jsonc の vars.R2_ACCOUNT_ID にアカウント ID を書いてから
npx wrangler secret put R2_ACCESS_KEY_ID
npx wrangler secret put R2_SECRET_ACCESS_KEY
```

設定しない場合は Worker 経由の転送（`/blobs/:hash/data`、開発用）になる。

## ユーザーの追加

```bash
node scripts/create-user.mjs Kimitake --remote
node scripts/create-user.mjs 友人の名前 --remote
```

表示されたトークンを本人に渡す。アプリでは「同期 → サーバー設定」でサーバー URL とトークンを入力する。

## 開発

```bash
npm test          # Workers ランタイム（Miniflare）上の結合テスト
npm run typecheck
npx wrangler dev  # ローカルで起動（--local の D1 / R2）
```
