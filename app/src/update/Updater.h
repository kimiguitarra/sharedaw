#pragma once

#include <optional>

#include "Common.h"
#include "sync/SyncClient.h"

/**
    アプリの更新（手元に置いたアプリをそのまま新しくする）。

    CI（GitHub Actions の手動ビルド）が、アプリのファイルを 1 つずつ同期サーバーに実体としてアップロードし、
    ファイル一覧（マニフェスト: パスと SHA-256）を登録する（tools/publish-release.mjs）。
    アプリは一覧と手元のファイルを比べて、変わったファイルだけをダウンロードして置き換える。

    置き換え方:
      Windows  実行中の exe は上書きできないが名前は変えられるので、古いファイルを *.old にずらして新しいファイルを置く。
               *.old は次の起動時に消す。
      Mac      .app の中のファイルを rename で置き換える（実行中のプロセスは古い中身を使い続ける）。
*/
namespace Updater
{
    struct Info
    {
        int build = 0;
        juce::String version, notes;
        TransferUrl manifest;
    };

    /** 実行ファイルの名前（起動時のもの）。 */
    juce::String executableName();

    /** このアプリのビルド番号（CI のビルド。手元でビルドしたものは 0）。 */
    int currentBuild();

    /**
        バージョンの表示（V0.1.123 のように。末尾が CI のビルド番号）。build が 0 なら「開発版」。
        更新の比較はビルド番号で行う。
    */
    juce::String versionText (int build);

    /** "windows" / "mac" / "linux" */
    juce::String platformName();

    /** 置き換える対象のフォルダ（Windows / Linux は exe のフォルダ、Mac は .app）。 */
    juce::File installRoot();

    /** サーバーの最新版を問い合わせる（バックグラウンドスレッドから）。配信がなければ nullopt で ok。 */
    juce::Result fetchLatest (const SyncClient&, std::optional<Info>& out);

    /**
        更新をダウンロードして置き換える（バックグラウンドスレッドから）。
        progress (0〜1, 説明) が false を返したら中止する（置き換えの前なら何も変わらない）。
    */
    juce::Result downloadAndInstall (const SyncClient&, const Info&, std::function<bool (double, const juce::String&)> progress);

    /** 前回の更新で残った古いファイルを消す（起動時に呼ぶ）。 */
    void cleanUpPreviousUpdate();

    /** 終了時に新しいアプリを起動するよう予約する。 */
    void requestRelaunch();

    /** 予約されていれば新しいアプリを起動する（アプリの終了処理の最後に呼ぶ）。 */
    void relaunchIfRequested();
}
