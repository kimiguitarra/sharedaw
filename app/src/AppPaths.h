#pragma once

#include "Common.h"

namespace AppPaths
{
    /** 同梱アセット（内蔵音源など）のフォルダ。見つからなければ存在しない File。 */
    juce::File getAssetsDir();

    /** アプリの設定・自動保存・セッション情報を置くフォルダ（ユーザーごと）。 */
    juce::File getAppDataDir();

    /**
        フォルダの中を指す相対パスか（絶対パス・ドライブ名・".."・空の要素を含まない）。
        曲や更新の一覧など、外から来たパスでフォルダの外に書かないために使う。
    */
    bool isSafeRelativePath (const juce::String& path);

    /**
        アプリのデータフォルダの記録ファイル（sync.log など）に 1 行足す（時刻付き。どのスレッドからでもよい）。
        1 MB を超えたら作り直す。落ちたり失敗したりしたときに、どこまで進んだか分かるように。
    */
    void appendLog (const juce::String& fileName, const juce::String& text);

    /** 一時ファイルに書いてから置き換える（途中で落ちても壊れたファイルを残さない）。 */
    juce::Result writeFileAtomically (const juce::File& target, const void* data, size_t size);
}
