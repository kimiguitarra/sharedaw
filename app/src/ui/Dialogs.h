#pragma once

#include "Common.h"

namespace Dialogs
{
    /** 1行のテキストを入力してもらう（非同期）。OK のときだけ onOk が呼ばれる。 */
    void askText (const juce::String& title, const juce::String& message, const juce::String& initial,
                  std::function<void (const juce::String&)> onOk, juce::Component* associated = nullptr);

    /** OK / キャンセルの確認（非同期）。 */
    void confirm (const juce::String& title, const juce::String& message, const juce::String& okText,
                  std::function<void()> onOk, juce::Component* associated = nullptr);

    /**
        ボタンを選んでもらう（非同期）。result はボタンの番号（1, 2, …）。
        Enter・スペースでは押されない（押し間違いで、更新・再起動などが始まらないように）。Esc は最後のボタン。
    */
    void askChoice (const juce::String& title, const juce::String& message, const juce::StringArray& buttons,
                    std::function<void (int)> onResult, juce::Component* associated = nullptr);

    /** 3択（保存 / 保存しない / キャンセル）。result: 1 = 保存, 2 = 保存しない, 0 = キャンセル */
    void askSaveChanges (const juce::String& message, std::function<void (int)> onResult);

    void showError (const juce::String& title, const juce::String& message);
    void showInfo (const juce::String& title, const juce::String& message);
}
