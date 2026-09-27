# ShareDAW

友人と楽曲を共同制作するための、アイデア出しとラフ録音に特化した軽量 DAW。
仕様は [docs/spec.md](docs/spec.md)、実装メモは [docs/architecture.md](docs/architecture.md)。

## 進捗

| マイルストーン | 状態 |
|---|---|
| M1 ローカル基盤 | 実装済み |
| M2 録音・編集・プラグイン | 実装済み（実機のオーディオインターフェースでの確認待ち） |
| M3 コードトラック | 実装済み |
| M4 同期 | 実装済み（サーバーは `server/`、手順は [server/README.md](server/README.md)） |
| M5 配布・実運用 | 未着手（本番の内蔵音源・インストーラーは §11 の未決事項） |

### M1 ローカル基盤

- MIDI トラック（内蔵音源: ドラム / ベース / ピアノの仮音源）の追加、名前・色・音量・パン・ミュート・ソロ
- タイムライン: クリップの作成（鉛筆ツールでクリック／ドラッグ）・移動（別トラックへも）・長さ変更・複製・削除
- テンポトラック・拍子トラック・コードトラック（鉛筆ツールでクリックして追加、選択ツールで選択・ドラッグ移動・ダブルクリック編集・Delete 削除、右クリックでメニュー）
- ピアノロール: ノートの追加・削除・移動・長さ変更・範囲選択・移調、ベロシティ、グリッド（1/1〜1/32、3連）、
  スナップ、クオンタイズ、ドラムトラックは GM ドラムマップ表示
- 内蔵音源の調整（音量・パン・トーン、ドラムはキット・パーツごとのサンプル差し替え・音量・パン・チューニング）
- 再生・停止・ループ（クリップやノートを選んで P で範囲指定。ルーラーのクリック／ドラッグは再生位置の移動のみ）・メトロノーム・小節.拍.tick と時間の表示
- 保存・読み込み（project.json、JSON Schema で検証）、元に戻す／やり直し、自動保存、異常終了からの復旧
- 文字サイズの拡大（表示 → 文字サイズ、画面共有用）

### M2 録音・編集・プラグイン

- オーディオトラック、オーディオの読み込み（ドラッグ＆ドロップ可。48kHz / 32bit float に変換、SHA-256 のファイル名）
- 録音: オーディオトラックの ● で録音待機、入力（モノラルのチャンネル）の選択、トラックごとのソフトウェアモニタリング、
  R キーまたは ● ボタンで録音・停止。カウントイン（なし / 1 / 2 小節、トランスポートメニュー）はテンポマップどおりに鳴り、
  1 小節目からでも使える
- レイテンシ補正: ドライバが報告する入出力レイテンシで自動補正し、オーディオ設定でデバイスごとに手動オフセット（サンプル）を設定できる
- 非破壊のオーディオ編集: 移動、先頭・末尾のトリム、フェードイン・アウト、クリップ音量、再生位置で分割（S）
- VST3 / AU のホスト: スキャンは別プロセスで行い、クラッシュしたプラグインはブラックリストに入る（ファイル → プラグイン…）。
  音源・エフェクトとして使え、画面を開ける。状態は `plugins-state/` に保存
- バウンス（トラックを右クリック → バウンス）と「要バウンス」「バウンスが古い」の表示。外部プラグインのトラックは
  バウンスが最新でないと push できない。プラグイン（または持ち主の状態ファイル）がない環境ではバウンスした音で再生する
- Windows は ASIO（ASIO SDK 付きでビルドした場合）と WASAPI、Mac は CoreAudio

### M3 コードトラック

- コードの入力（`CM7(9)`、`Am7/G`、`X` など）、コードレーンでの移動・長さ変更、コード名のエディタ
- 内蔵ピアノでのコード再生（ボイシングは前のコードからの動きが少なくなるように選ぶ）

### M4 同期

- Cloudflare Workers + D1 + R2 のサーバー、トークン認証（OS の資格情報ストアに保存）
- トラック・テンポ・拍子・コード単位のロック、ロックのないところは編集できない
- pull / push（ID ベースの差分を一覧表示、変更箇所へジャンプ）、必要なオーディオだけを転送、履歴
- 競合時はローカルの内容を `.collab/conflicts/` に退避

### 操作（Cubase 寄せ）

- 操作モード: 表示 → 操作モード で「Cubase」「Studio One」を選べる。現在 Studio One モードは Cubase と同じ操作
  （`EditorState.h` の `EditBehaviour::forMode()` で後から作り込む予定）
- ツール: テンキー 1 = 選択ツール（標準）、テンキー 2 = 鉛筆ツール。トランスポートバーのボタンでも切り替えられる。
  鉛筆ではテンポ・拍子・コード・MIDI クリップ・ノートを任意の場所に置いていく（ピアノロールではクリックで追加、
  ドラッグで長さ、既存ノートをクリックで削除）。選択ツールでは選択・移動・長さ変更・範囲選択
- トラックの追加: トラック一覧（左側の領域）やトラックの下の空き領域を右クリック →「オーディオトラックを追加」
  または「音源トラックを追加 ▶ ドラム / ベース / ピアノ」（トラック メニューからも可）
- ミキサー: F3
- クオンタイズ値: トランスポートバー（またはピアノロール）で 1/1〜1/32 と 3 連符（1/3, 1/6, 1/12, 1/24, 1/48）を選ぶ。
  ノートの配置・クオンタイズ・ルーラーでの再生位置の移動がこの値に合う。J でスナップのオン／オフ（フリー）、Alt で一時的にフリー
- 自動スクロール: F で切り替え（再生中にタイムラインとピアノロールが再生位置を追う）
- EQ / コンプ: すべてのトラックに標準で付いている。ミキサーの「EQ」「COMP」ボタン、またはトラックの右クリック →「EQ / コンプ…」。
  EQ はローカット・Low（シェルフ）・Mid（ピーキング）・High（シェルフ）、コンプは FET（1176 風: 速い・少し歪む）と
  オプティカル（LA-2A 風: RMS 検出・音に応じてゆっくり戻る）から選べる。バウンスには含めない（受け取った側でも同じ設定がかかる）

主なショートカット: Space 再生/停止、テンキー 0 停止、R / テンキー * 録音、Home / テンキー . 先頭へ、
L / テンキー / ループ、P 選択範囲をループ範囲に、G / H ズームアウト / イン、J スナップ、F 自動スクロール、
C メトロノーム、Q クオンタイズ、S 分割、F3 ミキサー、Ctrl/Cmd+S 保存、Ctrl/Cmd+Z 元に戻す、Ctrl/Cmd+D クリップ複製、Delete 削除。

### アップデート

アプリは手元に置いたまま新しくできる（ヘルプ → アップデートを確認…。CI でビルドしたアプリは起動時にも確認する）。
更新は同期サーバーから配信され、変わったファイルだけをダウンロードして置き換える（Windows は exe のフォルダ、Mac は .app の中身）。
同期サーバーの URL とトークンを設定しておく必要がある。

配信のしかた:
1. 最初に一度だけ、推測されにくい文字列（リリースキー）を決めて、次の 2 か所に同じ値を登録する（チャットなどには書かない）
   - Cloudflare: Worker の「設定 → 変数とシークレット」にシークレット `RELEASE_KEY`
   - GitHub: リポジトリの Settings → Secrets and variables → Actions に `SHAREDAW_RELEASE_KEY`、
     あわせて `SHAREDAW_SERVER_URL`（同期サーバーの URL）
2. Actions → Build → Run workflow（「配信する」にチェック）。ビルドが終わると各 OS のアプリにアップデートが届く
   （ビルド番号 = Actions の実行番号で新旧を判断する）

最初の 1 回だけは、これまでどおり Artifacts からアプリを手で入れる（アップデート機能を含むビルドにするため）。

## ビルド

必要なもの: CMake 3.22 以上、C++20 コンパイラ（Windows は Visual Studio 2022）、Git。

```bash
git clone <このリポジトリ>
cd sharedaw
# Tracktion Engine の .gitmodules は JUCE を SSH の URL で参照しているため、HTTPS で取る場合は読み替える
git config --global url."https://github.com/".insteadOf "git@github.com:"
git submodule update --init --recursive

# Windows
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release

# macOS（Universal Binary）
cmake -S . -B build -G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build --config Release
```

成果物は `build/app/ShareDAW_artefacts/Release/` にできる（内蔵音源の `assets` フォルダも隣にコピーされる。
Mac はアプリの `Contents/Resources/assets`）。

単体テストだけなら JUCE なしでビルドできる:

```bash
cmake -S . -B build-core -DCOLLAB_BUILD_APP=OFF
cmake --build build-core
ctest --test-dir build-core --output-on-failure
```

Windows で ASIO を使う場合は Steinberg の ASIO SDK を用意し、`-DCOLLAB_ASIO_SDK_DIR=<SDKのフォルダ>` を付けて
構成する（付けない場合は WASAPI のみ）。CI は SDK を Steinberg のサイトから取得できたときだけ ASIO を有効にする
（ジョブの警告に出る）。ASIO SDK は Steinberg のライセンスに従うこと。

## CI

GitHub Actions（`.github/workflows/build.yml`）で Windows / macOS（Universal）/ Linux をビルドし、
単体テストと、Linux ではデモプロジェクトの書き出しと、テスト用プラグイン（`tools/test-plugins`）での
スキャン・バウンスのスモークテスト（`tools/plugin-smoke-test.sh`）を行う。ビルド済みのアプリは
各ジョブの Artifacts からダウンロードできる。シークレットを設定していれば、同期サーバーへの配信
（`tools/publish-release.mjs`、上の「アップデート」）も行う。

## 初回起動（署名なしの配布物）

Apple Developer には登録していないので、Mac 版は公証なしのアドホック署名、Windows 版は署名なしで配布する。

- **Mac**:
  1. `ShareDAW.dmg` を開き、`ShareDAW.app` を「アプリケーション」フォルダへドラッグする。
  2. 一度アプリを開こうとする（「開けません」と表示されるので「完了」で閉じる）。
  3. 「システム設定 → プライバシーとセキュリティ」の下のほうにある「このまま開く」を押し、確認でもう一度「このまま開く」。
     - macOS 14 以前は、Finder でアプリを右クリック →「開く」でもよい。
     - ターミナルで `xattr -dr com.apple.quarantine /Applications/ShareDAW.app` を実行しても開けるようになる。
  4. 録音するときにマイクの許可を求められたら「許可」する。新しいビルドに入れ替えたときは、もう一度聞かれることがある
     （アドホック署名はビルドごとに変わるため）。聞かれずに無音で録音される場合は「システム設定 → プライバシーとセキュリティ →
     マイク」で ShareDAW をオンにする。
- **Windows**: 署名していないため SmartScreen の警告が出る。「詳細情報」→「実行」で起動する。

## 内蔵音源のクレジット

- ドラム: [Big Rusty Drums](https://github.com/sfzinstruments/karoryfer.big-rusty-drums) by Karoryfer Samples x bigcat（CC0 1.0）
- ピアノ: [Salamander Grand Piano V3](https://github.com/sfzinstruments/SalamanderGrandPiano) by Alexander Holm（CC BY 3.0）。
  ShareDAW 用に縮小（8 ベロシティレイヤー、16bit、減衰を 10 秒で打ち切り、ノイズ類なし）
- ベース: sfizz の内蔵オシレーターによるシンセ（サンプルなし）

作り方は `tools/build-instruments/` を参照。

## ライセンス

当面は GPL v3（JUCE / Tracktion Engine の GPL 版を使用）。sfizz は BSD-2-Clause。
