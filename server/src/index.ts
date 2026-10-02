// ShareDAW 同期サーバー（仕様書 §6）。Cloudflare Workers + D1 + R2。
// ルートは routes/*.ts（読み込むと登録される）、認証とルーティングは router.ts。

import { handleAdmin } from "./admin";
import { healthCheck } from "./health";
import { dispatch, handleError } from "./router";
import { Env, json } from "./util";

import "./routes/projects";
import "./routes/revisions";
import "./routes/locks";
import "./routes/blobs";
import "./routes/app";

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    const url = new URL(request.url);

    try {
      // 動作確認用（ブラウザでサーバー URL を開いたとき）と、ユーザー作成の管理ページ
      if (url.pathname === "/" && request.method === "GET") return await healthCheck(env);
      if (url.pathname === "/admin") return await handleAdmin(request, env);

      return (await dispatch(request, env, url)) ?? json({ error: "not_found", message: "見つかりません" }, 404);
    } catch (e) {
      return handleError(e);
    }
  },
} satisfies ExportedHandler<Env>;
