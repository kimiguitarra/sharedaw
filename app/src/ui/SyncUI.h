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

    /**
        差分の一覧（§4.4）。トラックごとに変更点を並べ、項目を選ぶとタイムラインの該当箇所へジャンプする。
        push のときはコメント欄と「push 後にロックを解除する」を表示する。
    */
    class DiffView  : public juce::Component,
                      private juce::ListBoxModel
    {
    public:
        enum class Mode { pull, push };

        DiffView (Mode, const collab::ProjectDiff&, const juce::String& headline,
                  std::function<void (const collab::Change&)> onJump,
                  std::function<void (const juce::String& message, bool releaseLocks)> onConfirm,
                  std::function<void()> onClose);

        void resized() override;
        void paint (juce::Graphics&) override;

    private:
        struct Row
        {
            bool header = false;
            juce::String text;
            collab::Change change;
        };

        Mode mode;
        std::vector<Row> rows;
        juce::Label headlineLabel;
        juce::ListBox list;
        juce::TextEditor comment;
        juce::ToggleButton releaseLocks;
        juce::TextButton confirmButton, cancelButton;
        std::function<void (const collab::Change&)> onJump;

        int getNumRows() override                         { return (int) rows.size(); }
        void paintListBoxItem (int, juce::Graphics&, int, int, bool) override;
        void listBoxItemClicked (int, const juce::MouseEvent&) override;
    };

    /** リビジョン履歴の一覧。 */
    std::unique_ptr<juce::Component> createHistoryView (const nlohmann::json& revisions);
}
