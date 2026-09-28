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

    status.setFont (juce::FontOptions (13.0f));
    status.setColour (juce::Label::textColourId, Theme::textDim);
    status.setText ("トークンは OS の資格情報ストア（Windows 資格情報マネージャー / macOS キーチェーン）に保存されます。"_ju,
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
    g.setFont (juce::FontOptions (13.0f));
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
DiffView::DiffView (Mode m, const collab::ProjectDiff& diff, const juce::String& headline,
                    std::function<void (const collab::Change&)> jump,
                    std::function<void (const juce::String&, bool)> onConfirm,
                    std::function<void()> onClose)
    : mode (m), onJump (std::move (jump))
{
    // スコープごとにまとめる
    std::string lastScope;

    for (auto& c : diff.changes)
    {
        if (c.scopeId != lastScope)
        {
            rows.push_back ({ true, toJuce (c.scopeName), c });
            lastScope = c.scopeId;
        }

        rows.push_back ({ false, toJuce (c.summary), c });
    }

    if (rows.empty())
        rows.push_back ({ true, "変更はありません"_ju, {} });

    headlineLabel.setText (headline, juce::dontSendNotification);
    headlineLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    addAndMakeVisible (headlineLabel);

    list.setModel (this);
    list.setRowHeight (24);
    list.setColour (juce::ListBox::backgroundColourId, Theme::background);
    addAndMakeVisible (list);

    if (mode == Mode::push)
    {
        comment.setTextToShowWhenEmpty ("コメント（任意）: 何を変えたか"_ju, Theme::textDim);
        comment.setMultiLine (false);
        addAndMakeVisible (comment);

        releaseLocks.setButtonText ("push したらロックを解除する"_ju);
        releaseLocks.setToggleState (true, juce::dontSendNotification);
        addAndMakeVisible (releaseLocks);
    }

    confirmButton.setButtonText (mode == Mode::push ? "アップロード（push）"_ju : "取り込む（pull）"_ju);
    confirmButton.onClick = [this, onConfirm]
    {
        if (onConfirm)
            onConfirm (comment.getText().trim(), releaseLocks.getToggleState());
    };
    addAndMakeVisible (confirmButton);

    cancelButton.setButtonText ("閉じる"_ju);
    cancelButton.onClick = [onClose] { if (onClose) onClose(); };
    addAndMakeVisible (cancelButton);

    setSize (620, 520);
}

void DiffView::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panel);
}

void DiffView::resized()
{
    auto area = getLocalBounds().reduced (10);
    headlineLabel.setBounds (area.removeFromTop (26));
    area.removeFromTop (4);

    auto buttons = area.removeFromBottom (30);
    cancelButton.setBounds (buttons.removeFromRight (100));
    buttons.removeFromRight (8);
    confirmButton.setBounds (buttons.removeFromRight (180));

    if (mode == Mode::push)
    {
        area.removeFromBottom (6);
        releaseLocks.setBounds (area.removeFromBottom (24));
        area.removeFromBottom (4);
        comment.setBounds (area.removeFromBottom (28));
    }

    area.removeFromBottom (6);
    list.setBounds (area);
}

void DiffView::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (row < 0 || row >= (int) rows.size())
        return;

    const auto& r = rows[(size_t) row];

    if (selected && ! r.header)
        g.fillAll (Theme::accent.withAlpha (0.25f));

    if (r.header)
    {
        g.setColour (Theme::panelLight);
        g.fillRect (0, 0, width, height);
        g.setColour (Theme::text);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText (r.text, 8, 0, width - 16, height, juce::Justification::centredLeft);
    }
    else
    {
        g.setColour (Theme::text);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (juce::String::fromUTF8 ("\xE3\x83\xBB") + r.text, 24, 0, width - 32, height, juce::Justification::centredLeft);
    }
}

void DiffView::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    if (row >= 0 && row < (int) rows.size() && ! rows[(size_t) row].change.scopeId.empty() && onJump)
        onJump (rows[(size_t) row].change);
}

//==============================================================================
std::unique_ptr<juce::Component> createHistoryView (const nlohmann::json& revisions)
{
    auto editor = std::make_unique<juce::TextEditor>();
    editor->setMultiLine (true);
    editor->setReadOnly (true);
    editor->setFont (juce::FontOptions (14.0f));

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
