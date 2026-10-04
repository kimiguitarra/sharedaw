#include "SyncUI.h"

#include "Theme.h"

namespace SyncUI
{

juce::Result runWithProgress (const juce::String& title, std::function<juce::Result()> work)
{
    struct Task  : public juce::ThreadWithProgressWindow
    {
        Task (const juce::String& t, std::function<juce::Result()> w)
            : ThreadWithProgressWindow (t, true, false), fn (std::move (w)) {}

        void run() override
        {
            setProgress (-1.0);   // 不定
            result = fn();
        }

        std::function<juce::Result()> fn;
        juce::Result result = juce::Result::ok();
    };

    Task task (title, std::move (work));
    task.runThread();
    return task.result;
}

juce::Result runWithProgress (const juce::String& title, std::function<juce::Result (const SyncProgress&)> work)
{
    struct Task  : public juce::ThreadWithProgressWindow
    {
        Task (const juce::String& t, std::function<juce::Result (const SyncProgress&)> w)
            : ThreadWithProgressWindow (t, true, true, 30000, "中止"_ju), fn (std::move (w)) {}

        void run() override
        {
            setProgress (-1.0);
            setStatusMessage ("サーバーに接続しています…"_ju);

            result = fn ([this] (const juce::String& status, double fraction)
            {
                setStatusMessage (status);
                setProgress (fraction);
                return ! threadShouldExit();
            });

            if (threadShouldExit() && result.wasOk())
                result = juce::Result::fail ("中止しました"_ju);
        }

        std::function<juce::Result (const SyncProgress&)> fn;
        juce::Result result = juce::Result::ok();
    };

    Task task (title, std::move (work));

    if (! task.runThread())
        return juce::Result::fail ("中止しました"_ju);

    return task.result;
}

//==============================================================================
ServerSettings::ServerSettings (const juce::String& url, bool hasToken,
                                std::function<juce::String (const juce::String&, const juce::String&)> cb)
    : onTestAndSave (std::move (cb))
{
    urlEditor.setText (url);
    urlEditor.setTextToShowWhenEmpty ("https://sharedaw-sync.<アカウント>.workers.dev"_ju, Theme::textDim);
    tokenEditor.setPasswordCharacter ((juce::juce_wchar) 0x2022);
    tokenEditor.setTextToShowWhenEmpty (hasToken ? "（保存済み。変更するときだけ入力）"_ju : "オーナーから受け取ったトークン"_ju, Theme::textDim);

    status.setFont (juce::FontOptions (14.5f));
    status.setColour (juce::Label::textColourId, Theme::textDim);
    status.setText ("トークンはこの PC に安全に保存されます。"_ju,
                    juce::dontSendNotification);

    saveButton.setButtonText ("接続テストして保存"_ju);
    saveButton.onClick = [this]
    {
        const auto message = onTestAndSave (urlEditor.getText(), tokenEditor.getText());
        status.setText (message, juce::dontSendNotification);
    };

    closeButton.setButtonText ("閉じる"_ju);
    closeButton.onClick = [this]
    {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState (0);
    };

    for (juce::Component* c : std::initializer_list<juce::Component*> { &urlEditor, &tokenEditor, &status, &saveButton, &closeButton })
        addAndMakeVisible (c);

    setSize (560, 220);
}

void ServerSettings::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panel);
    g.setColour (Theme::text);
    g.setFont (juce::FontOptions (14.5f));
    g.drawText ("サーバー URL"_ju, 12, 12, 120, 26, juce::Justification::centredLeft);
    g.drawText ("トークン"_ju, 12, 48, 120, 26, juce::Justification::centredLeft);
}

void ServerSettings::resized()
{
    urlEditor.setBounds (130, 12, getWidth() - 142, 26);
    tokenEditor.setBounds (130, 48, getWidth() - 142, 26);
    status.setBounds (12, 84, getWidth() - 24, 80);
    closeButton.setBounds (getWidth() - 112, getHeight() - 40, 100, 28);
    saveButton.setBounds (getWidth() - 290, getHeight() - 40, 170, 28);
}

//==============================================================================
std::unique_ptr<juce::Component> createHistoryView (const nlohmann::json& revisions)
{
    auto editor = std::make_unique<juce::TextEditor>();
    editor->setMultiLine (true);
    editor->setReadOnly (true);
    editor->setFont (juce::FontOptions (15.5f));

    juce::String text;

    for (auto& r : revisions)
    {
        auto when = juce::Time::fromISO8601 (toJuce (r.value ("createdAt", std::string()))).toString (true, true, false);
        auto author = r.contains ("authorName") && r["authorName"].is_string() ? toJuce (r["authorName"].get<std::string>()) : juce::String ("?");

        text << "#" << r.value ("number", 0) << "  " << when << "  " << author << "\n";

        if (auto msg = toJuce (r.value ("message", std::string())); msg.isNotEmpty())
            text << "    " << msg << "\n";

        text << "\n";
    }

    if (text.isEmpty())
        text = "リビジョンはまだありません"_ju;

    editor->setText (text);
    editor->setSize (520, 420);
    return editor;
}

}
