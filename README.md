# CollabDAW（仮称）

友人と楽曲を共同制作するための、アイデア出しとラフ録音に特化した軽量 DAW。
仕様は [docs/spec.md](docs/spec.md)、実装メモは [docs/architecture.md](docs/architecture.md)。

## 進捗

| マイルストーン | 状態 |
|---|---|
| M1 ローカル基盤 | 実装中（下記） |
| M2 録音・編集・プラグイン | 未着手 |
| M3 コードトラック | 未着手（プロジェクト JSON にはコードトラックの形式のみ定義済み） |
| M4 同期 | 未着手 |
| M5 配布・実運用 | 未着手 |

M1 でできること:

- MIDI トラック（内蔵音源: ドラム / ベース / ピアノの仮音源）の追加、名前・色・音量・パン・ミュート・ソロ
- タイムライン: クリップの作成（ダブルクリック）・移動（別トラックへも）・長さ変更・複製・削除
- テンポトラック・拍子トラック（ダブルクリックで追加・編集、ドラッグで移動、右クリックで削除）
- ピアノロール: ノートの追加・削除・移動・長さ変更・範囲選択・移調、ベロシティ、グリッド（1/1〜1/32、3連）、
  スナップ、クオンタイズ、ドラムトラックは GM ドラムマップ表示
- 内蔵音源の調整（音量・パン・トーン、ドラムはキット・パーツごとのサンプル差し替え・音量・パン・チューニング）
- 再生・停止・ループ（ルーラーをドラッグして範囲指定）・メトロノーム・小節.拍.tick と時間の表示
- 保存・読み込み（project.json、JSON Schema で検証）、元に戻す／やり直し、自動保存、異常終了からの復旧
- 文字サイズの拡大（表示 → 文字サイズ、画面共有用）

主なショートカット: Space 再生/停止、Home 先頭へ、L ループ、C メトロノーム、Q クオンタイズ、
Ctrl/Cmd+S 保存、Ctrl/Cmd+Z 元に戻す、Ctrl/Cmd+D クリップ複製、Delete 削除。

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

成果物は `build/app/CollabDAW_artefacts/Release/` にできる（内蔵音源の `assets` フォルダも隣にコピーされる。
Mac はアプリの `Contents/Resources/assets`）。

単体テストだけなら JUCE なしでビルドできる:

```bash
cmake -S . -B build-core -DCOLLAB_BUILD_APP=OFF
cmake --build build-core
ctest --test-dir build-core --output-on-failure
```

Windows で ASIO を使う場合は Steinberg の ASIO SDK を用意し、`-DCOLLAB_ASIO_SDK_DIR=<SDKのフォルダ>` を付けて
構成する（付けない場合は WASAPI のみ）。

## CI

GitHub Actions（`.github/workflows/build.yml`）で Windows / macOS（Universal）/ Linux をビルドし、
単体テストと、Linux ではデモプロジェクトの書き出しによるスモークテストを行う。ビルド済みのアプリは
各ジョブの Artifacts からダウンロードできる。

## 初回起動（署名なしの配布物）

- **Mac**: 公証していないため、初回は Finder でアプリを右クリック →「開く」。それでも開けない場合は
  「システム設定 → プライバシーとセキュリティ」で「このまま開く」を押す。録音時にマイクの許可を求められたら許可する。
- **Windows**: 署名していないため SmartScreen の警告が出る。「詳細情報」→「実行」で起動する。

## ライセンス

当面は GPL v3（JUCE / Tracktion Engine の GPL 版を使用）。sfizz は BSD-2-Clause。
