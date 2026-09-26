# 内蔵音源

`instruments/<id>/<version>/` にバージョン固定で配置する（仕様書 §3.3）。
一度配布したバージョンの中身は変更しない。音を変えるときは新しいバージョンのフォルダを作る。

| ID | バージョン | 内容 |
|---|---|---|
| builtin.drums | 0.1.0 | 仮音源（sfizz の内蔵ジェネレーター `*sine` / `*noise` 等で合成。サンプルファイルなし） |
| builtin.bass  | 0.1.0 | 仮音源（同上） |
| builtin.piano | 0.1.0 | 仮音源（同上） |

仮音源はサンプルファイルを使わないため、Windows と Mac で同じ音が鳴る。
本番音源（Salamander Grand Piano など、CC0 / CC-BY のもの）は M5 で `1.0.0` として追加する。

## manifest.json

- `type`: `drums`（パーツごとにサンプル差し替え・音量・パン・チューニング）または `melodic`
- `defaultParams`: トラック作成時の `instrument.params`
- drums の `samples/<id>.sfz` は `<region>` のみを書き、`key` は書かない（パーツのノート番号はアプリが `<master> key=` で指定する）
