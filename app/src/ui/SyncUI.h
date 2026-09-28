#pragma once

#include "Common.h"
#include "collab/ProjectDiff.h"
#include "sync/SyncClient.h"

namespace SyncUI
{
    /** 進捗ウィンドウを出してバックグラウンドで実行する（完了まで待つ）。 */
    juce::Result runWithProgress (const juce::String& title, std::function<juce::Result()> work);

    /** 進み具合（バー・何を送っているか）を出し、「中止」できる版。 */
    juce::Result runWithProgress (const juce::String& title, std::function<juce::Result (const SyncProgress&)> work);

    /** サーバー URL とトークンの入力（§6.2）。 */
    class ServerSettings  : public juce::Component
    {
    public:
        ServerSettings (const juce::String& url, bool hasToken,
                        std::function<juce::String (const juce::String& url, const juce::String& token)> onTestAndSave);
        void resized() override;
        void paint (juce::Graphics&) override;

    private:
        juce::TextEditor urlEditor, tokenEditor;
        juce::Label status;
        juce::TextButton saveButton, closeButton;
        std::function<juce::String (const juce::String&, const juce::String&)> onTestAndSave;
    };

    /** リビジョン履歴の一覧。 */
    std::unique_ptr<juce::Component> createHistoryView (const nlohmann::json& revisions);
}
