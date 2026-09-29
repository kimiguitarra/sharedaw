# 実装メモ（アーキテクチャ）

仕様書（[spec.md](spec.md)）を実装するうえで決めた、コードを読むときに知っておくべきこと。

## 構成

```
/app
  /core      collab_core: JUCE に依存しない C++20 ライブラリ（モデル、JSON、テンポマップ、グリッド、
             内蔵音源の SFZ 生成、SHA-256、UUID、NFC 正規化、差分・マージ、クリップ編集、
             バウンスのフィンガープリント、カウントイン）。単体テストは /app/core/tests（doctest）
  /chord     collab_chord: コード名の解析とボイシング
  /src       JUCE + Tracktion Engine のアプリ本体
    ProjectDocument   編集中の Project（正）・元に戻す履歴・保存・自動保存
    EngineBridge      Project → Tracktion Edit の変換層
    SfizzPlugin       sfizz を Tracktion の内部プラグインとして鳴らす
    InstrumentLibrary 同梱の内蔵音源マニフェストの読み込み
    SessionGuard      異常終了の検知
    /audio            オーディオの取り込み（AudioFiles）、録音テイクの取り込み（Takes）、カウントイン（CountInPlugin）
    /plugins          外部プラグイン（スキャン、状態ファイル、エディタのウィンドウ）
    /sync             同期（SyncManager、HTTP クライアント、資格情報ストア）
    /ui               画面（タイムライン、テンポ・拍子・コードレーン、ピアノロール、音源パネル、トランスポート、同期）
  /external  git submodule: tracktion_engine（JUCE 同梱）、sfizz
/shared
  /schema    project.schema.json（アプリはビルド時に埋め込んで読み込み時に検証する）
  /fixtures  テスト用のプロジェクト JSON
/assets      内蔵音源（instruments/<id>/<version>/）、メトロノーム
/server      同期サーバー（Cloudflare Workers + D1 + R2）
/tools       リテラルの確認、テスト用プラグイン、プラグインのスモークテスト
```

## データの流れ

- **正は `collab::Project`（= project.json）**。UI の編集はすべて `ProjectDocument::perform()` で
  Project を書き換える。変更は `ChangeBroadcaster` で通知され、`EngineBridge` が Edit へ差分を反映し、UI が再描画する。
- 元に戻す／やり直しは Project のスナップショットで行う（プロジェクトは小さいので単純さを優先）。
  ドラッグ中の連続した変更は `mergeId` で1つの履歴にまとめる。
- Tracktion の Edit は保存しない。Edit から JSON への反映は録音の取り込みだけで、`recordingFinished` で
  受け取ったクリップを Edit から消し、ファイルを取り込んで JSON にクリップを足す（→ Edit はそこから作り直される）。

## 時間の扱い

- モデルは tick（PPQ 960、四分音符基準）とサンプル数（48kHz）。変換は `collab::TempoMap`。
  拍子の「拍」は分母の音符（6/8 なら八分音符）、BPM は四分音符基準（MIDI の慣例）。
- **Tracktion の Edit は 60BPM・4/4 に固定し「1拍 = 1秒」として使う。** tick → 秒の変換は TempoMap で行い、
  その秒数を Tracktion の拍として置く。Tracktion の拍子・テンポ解釈（分母の扱いなど）に依存せず、
  テンポ・拍子の変更は Edit のクリップを作り直すだけで済む。
- そのため Tracktion 内蔵のクリック（メトロノーム）は使わず、TempoMap からクリック音の MIDI を生成して
  専用の隠しトラック（sfizz、`assets/metronome/click.sfz`）で鳴らしている。

## 録音

- 録音は Tracktion の仕組みを使う。入力はモノラルのチャンネルごと（`setStereoPair(false)`）。
  トラックへの入力の割り当て・録音待機・モニタリングはこの環境だけの設定なので JSON には入れない（`EngineBridge::TrackInput`）。
- テイクはアプリの一時フォルダに書かれ、停止後に 48kHz / 32bit float に変換して `audio/<sha256>.wav` にし、元のファイルは消す。
- レイテンシは Tracktion がドライバの報告する入出力レイテンシで補正する。手動オフセット（サンプル）は
  デバイスごとにアプリの設定へ保存し、`WaveInputDevice::setRecordAdjustmentMs` で掛ける。
- **カウントイン**: Edit は 60BPM なので Tracktion のカウントイン（Edit の拍子 × 拍）は曲のテンポと合わない。
  そこで Edit の拍子を一時的に「ceil(カウントインの秒数)/4」にして 1 小節のカウントインでプリロールさせ（負の時刻から再生される）、
  クリックはマスターの `CountInPlugin` が TempoMap から計算した時刻に鳴らす。Tracktion 自身のクリックは
  `setClickTrackRange({})` で消す。プリロール中に録れた部分は録音開始位置でクリップから切り落とす。

## 外部プラグインとバウンス

- スキャンは別プロセス（同じ実行ファイルを子プロセスとして起動）。`EngineBehaviour::canScanPluginsOutOfProcess()` を
  true にしないと Tracktion は本体でスキャンするので注意。子プロセスがクラッシュしたファイルはブラックリストに入る。
- 状態は `plugins-state/<uuid>.bin`（`stateRef`）。保存・バウンス・push の前に書き出す。状態ファイルは同期しない（持ち主専用）。
- `render.sourceFingerprint` はトラックの音の元（MIDI、音源・エフェクトの設定、状態ファイルの中身のハッシュ）のハッシュ。
  音量・パン・ミュートは含めない（受け取った側でも掛かるため、バウンスはそれらを掛ける前の音）。
- プラグインが見つからない、またはバウンスがあるのに状態ファイルがない（= 他の人の設定）トラックはバウンスした音を再生する。
  その環境では音の元の変更は push できないが、音量などはベースのバウンスがそのまま使えるので push できる。

## 内蔵音源

- `assets/instruments/<id>/<version>/manifest.json` にパーツ・キット・既定パラメータを書く。
  サンプルは変えずにマップだけ変える版は `"samplesFrom": "1.0.0"` のように前の版のサンプルを使う（サンプルを複製しない）。
  新しいサンプルを足しつつ前の版のサンプルも使う版（ドラム 1.2.0、ピアノ 1.1.0）は、SFZ の `sample=../1.0.0/audio/...` で前の版を指す。
  ドラムの `kitOrder` は画面に並べる順、`kitAliases` は名前を変えたキットの旧名 → 新名。
- `instrument.params`（JSON）から `collab::generateSfz()` で SFZ テキストを生成し、sfizz の `loadSfzString()` に渡す。
  パーツごとのサンプル差し替え・音量・パン・チューニングとトーン（高域シェルフ）は SFZ に反映し、
  全体の音量・パンは SFZ を読み直さずにプラグイン側で掛ける。
- M1 の仮音源（0.1.0）は sfizz の内蔵ジェネレーター（`*sine` 等）だけで作っており、サンプルファイルを持たない。

## 文字列

- 内部の文字列は UTF-8（`std::string`）。保存前・読み込み時に NFC に正規化する（`collab::toNfc`）。
- JUCE の `juce::String (const char*)` は ASCII として扱うため、日本語のリテラルは必ず `"..."_ju`
  （`Common.h`）で書く。CI の `tools/check_literals.py` が確認する（`std::string` として使う行は行末に `// utf8-std`）。

## 保存・自動保存・クラッシュ復旧

- 保存は一時ファイルに書いてから置き換える（`TemporaryFile::overwriteTargetFileWithTemporary`）。
- 自動保存は、編集が 3 秒途切れたとき、または編集が続いていても 1 分ごとに
  `autosave/project.autosave.json`（未保存のプロジェクトはアプリのデータフォルダの `unsaved/`）へ書く。
- 起動中はアプリのデータフォルダに `session.json` を置き、正常終了時に消す。起動時に残っていれば
  異常終了とみなし、自動保存から復旧するか確認する。

## 動作確認用のコマンドライン

```
ShareDAW --render <プロジェクトフォルダ> <出力.wav>
```

プロジェクトを読み込み、先頭から末尾＋2秒を 48kHz / 32bit float WAV に書き出して終了する
（無音なら終了コード 5）。CI の Linux ジョブでスモークテストとして使っている。

ほかに動作確認用として `--import-audio`、`--scan-plugins`、`--bounce`、`--render-status`、`--record-test`、
`--sync-*`（`Main.cpp` の `initialise` と `runSyncCommand` 参照）がある。
