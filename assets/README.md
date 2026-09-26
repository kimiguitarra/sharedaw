# 内蔵音源

`instruments/<id>/<version>/` にバージョン固定で配置する（仕様書 §3.3）。
一度配布したバージョンの中身は変更しない。音を変えるときは新しいバージョンのフォルダを作る。

| ID | バージョン | 内容 |
|---|---|---|
| builtin.drums | 0.1.0 | 仮音源（sfizz の内蔵ジェネレーター `*sine` / `*noise` 等で合成。サンプルファイルなし） |
| builtin.bass  | 0.1.0 | 仮音源（同上） |
| builtin.piano | 0.1.0 | 仮音源（同上） |
| builtin.drums | 1.0.0 | キット「acoustic」: Big Rusty Drums（Karoryfer x bigcat、CC0）の近接マイクとオーバーヘッドを作者の既定のバランスで 1 本のステレオにまとめたもの（`tools/build-instruments/drums_rusty.py`）。キット「electronic」: 0.1.0 の合成音 |
| builtin.bass  | 1.0.0 | ソフトシンセ（sfizz の内蔵オシレーター）。音色: 指弾き風・ピック弾き風・シンセベース・サブベース |
| builtin.piano | 1.0.0 | Salamander Grand Piano V3（Alexander Holm、CC BY 3.0）を 8 レイヤー・16bit に縮小（`tools/build-instruments/piano_salamander.py`） |

古いバージョンも残す（そのバージョンで作ったプロジェクトが同じ音で鳴るように）。新しく作るトラックは最新のバージョンを使う。

## manifest.json

- `type`: `drums`（パーツごとにサンプル差し替え・音量・パン・チューニング）または `melodic`
- melodic の `presets`: 音色の一覧（`key`・`name`・`sfz`）。`instrument.params.preset` で選ぶ
- `defaultParams`: トラック作成時の `instrument.params`
- drums の `samples/<id>.sfz` は `<group>` / `<region>` のみを書き、`key` は書かない（パーツのノート番号はアプリが `<master> key=` で指定する）
- 読み込まれる SFZ の中では `<global>` / `<master>` を使わない（アプリが書く `<master>` の音量・トーン等が消えるため）。
  音量の補正は `group_volume`（`volume` に足される）で行う
- `sample=` のパスは manifest.json のあるフォルダからの相対パス
