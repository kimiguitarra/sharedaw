# 実装メモ（アーキテクチャ）

仕様書（[spec.md](spec.md)）を実装するうえで決めた、コードを読むときに知っておくべきこと。

## 構成

```
/app
  /core      collab_core: JUCE に依存しない C++20 ライブラリ（モデル、JSON、テンポマップ、グリッド、
             内蔵音源の SFZ 生成、SHA-256、UUID、NFC 正規化）。単体テストは /app/core/tests（doctest）
  /src       JUCE + Tracktion Engine のアプリ本体
    ProjectDocument   編集中の Project（正）・元に戻す履歴・保存・自動保存
    EngineBridge      Project → Tracktion Edit の変換層
    SfizzPlugin       sfizz を Tracktion の内部プラグインとして鳴らす
    InstrumentLibrary 同梱の内蔵音源マニフェストの読み込み
    SessionGuard      異常終了の検知
    /ui               画面（タイムライン、テンポ・拍子レーン、ピアノロール、音源パネル、トランスポート）
  /external  git submodule: tracktion_engine（JUCE 同梱）、sfizz
/shared
  /schema    project.schema.json（アプリはビルド時に埋め込んで読み込み時に検証する）
  /fixtures  テスト用のプロジェクト JSON
/assets      内蔵音源（instruments/<id>/<version>/）、メトロノーム
/server      同期サーバー（M4）
```

## データの流れ

- **正は `collab::Project`（= project.json）**。UI の編集はすべて `ProjectDocument::perform()` で
  Project を書き換える。変更は `ChangeBroadcaster` で通知され、`EngineBridge` が Edit へ差分を反映し、UI が再描画する。
- 元に戻す／やり直しは Project のスナップショットで行う（プロジェクトは小さいので単純さを優先）。
  ドラッグ中の連続した変更は `mergeId` で1つの履歴にまとめる。
- Tracktion の Edit は保存しない。Edit 上の編集 → JSON の反映（録音の取り込みなど）は M2 で追加する。

## 時間の扱い

- モデルは tick（PPQ 960、四分音符基準）とサンプル数（48kHz）。変換は `collab::TempoMap`。
  拍子の「拍」は分母の音符（6/8 なら八分音符）、BPM は四分音符基準（MIDI の慣例）。
- **Tracktion の Edit は 60BPM・4/4 に固定し「1拍 = 1秒」として使う。** tick → 秒の変換は TempoMap で行い、
  その秒数を Tracktion の拍として置く。Tracktion の拍子・テンポ解釈（分母の扱いなど）に依存せず、
  テンポ・拍子の変更は Edit のクリップを作り直すだけで済む。
- そのため Tracktion 内蔵のクリック（メトロノーム）は使わず、TempoMap からクリック音の MIDI を生成して
  専用の隠しトラック（sfizz、`assets/metronome/click.sfz`）で鳴らしている。

## 内蔵音源

- `assets/instruments/<id>/<version>/manifest.json` にパーツ・キット・既定パラメータを書く。
- `instrument.params`（JSON）から `collab::generateSfz()` で SFZ テキストを生成し、sfizz の `loadSfzString()` に渡す。
  パーツごとのサンプル差し替え・音量・パン・チューニングとトーン（高域シェルフ）は SFZ に反映し、
  全体の音量・パンは SFZ を読み直さずにプラグイン側で掛ける。
- M1 の仮音源（0.1.0）は sfizz の内蔵ジェネレーター（`*sine` 等）だけで作っており、サンプルファイルを持たない。

## 文字列

- 内部の文字列は UTF-8（`std::string`）。保存前・読み込み時に NFC に正規化する（`collab::toNfc`）。
- JUCE の `juce::String (const char*)` は ASCII として扱うため、日本語のリテラルは必ず `"..."_ju`
  （`Common.h`）で書く。

## 保存・自動保存・クラッシュ復旧

- 保存は一時ファイルに書いてから置き換える（`TemporaryFile::overwriteTargetFileWithTemporary`）。
- 自動保存は、編集が 3 秒途切れたとき、または編集が続いていても 1 分ごとに
  `autosave/project.autosave.json`（未保存のプロジェクトはアプリのデータフォルダの `unsaved/`）へ書く。
- 起動中はアプリのデータフォルダに `session.json` を置き、正常終了時に消す。起動時に残っていれば
  異常終了とみなし、自動保存から復旧するか確認する。

## 動作確認用のコマンドライン

```
CollabDAW --render <プロジェクトフォルダ> <出力.wav>
```

プロジェクトを読み込み、先頭から末尾＋2秒を 48kHz / 32bit float WAV に書き出して終了する
（無音なら終了コード 5）。CI の Linux ジョブでスモークテストとして使っている。
