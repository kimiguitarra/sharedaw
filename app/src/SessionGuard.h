#pragma once

#include "Common.h"

/**
    異常終了の検知（§3.10）。
    起動中はアプリのデータフォルダに session.json を置き、正常終了時に消す。
    起動時にこのファイルが残っていれば、前回は異常終了だったと判断する。
*/
class SessionGuard
{
public:
    struct PreviousSession
    {
        juce::File projectDir;     // 空ならフォルダ未定のプロジェクト
        juce::File autosaveFile;
    };

    SessionGuard();

    /** 前回が異常終了で、復旧できる自動保存が残っていればその情報を返す。 */
    std::optional<PreviousSession> findCrashedSession() const;

    /** 起動中の印を書く（プロジェクトの場所が変わるたびに呼ぶ）。 */
    void markRunning (const juce::File& projectDir, const juce::File& autosaveFile);

    /** 正常終了。 */
    void markCleanExit();

private:
    juce::File sessionFile;
};
