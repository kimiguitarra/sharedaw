#include "ChordEditor.h"

#include "Theme.h"

namespace
{
    const char* roots[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };

    struct QualityLabel { const char* quality; const char* label; };

    const QualityLabel qualities[] = {
        { "maj", "M" }, { "m", "m" }, { "dim", "dim" }, { "aug", "aug" }, { "sus2", "sus2" }, { "sus4", "sus4" },
        { "add9", "add9" }, { "6", "6" }, { "m6", "m6" }, { "7", "7" }, { "maj7", "M7" }, { "m7", "m7" },
        { "mM7", "mM7" }, { "m7b5", "m7(b5)" }, { "dim7", "dim7" }, { "7sus4", "7sus4" }
    };

    constexpr int buttonH = 28;
}

ChordEditor::ChordEditor (const juce::String& initialText, std::function<void (Result)> ok, std::function<void()> del)
    : onOk (std::move (ok)), onDelete (std::move (del))
{
    textEditor.setFont (juce::FontOptions (20.0f));
    textEditor.setJustification (juce::Justification::centredLeft);
    textEditor.setTextToShowWhenEmpty ("例: G7(9,13)、Am7/G、X"_ju, Theme::textDim);
    textEditor.onTextChange = [this] { textChanged(); };
    textEditor.onReturnKey = [this] { okButton.triggerClick(); };
    textEditor.onEscapeKey = [this] { close(); };
    addAndMakeVisible (textEditor);

    preview.setFont (juce::FontOptions (13.0f));
    addAndMakeVisible (preview);

    for (auto* r : roots)
    {
        auto* b = rootButtons.add (new juce::TextButton (r));
        b->setClickingTogglesState (false);
        b->onClick = [this, r]
        {
            current.root = r;
            currentNoChord = false;
            setFromStructure();
        };
        addAndMakeVisible (b);
    }

    for (auto& q : qualities)
    {
        auto* b = qualityButtons.add (new juce::TextButton (q.label));
        auto quality = std::string (q.quality);
        b->onClick = [this, quality]
        {
            current.quality = quality;
            currentNoChord = false;
            setFromStructure();
        };
        addAndMakeVisible (b);
    }

    for (auto& t : collab::chord::allTensions())
    {
        auto* b = tensionButtons.add (new juce::TextButton (toJuce (t)));
        b->onClick = [this, t]
        {
            auto& ts = current.tensions;

            if (auto it = std::find (ts.begin(), ts.end(), t); it != ts.end())
                ts.erase (it);
            else
                ts.push_back (t);

            // 並び順をそろえる
            std::vector<std::string> sorted;
            for (auto& x : collab::chord::allTensions())
                if (std::find (ts.begin(), ts.end(), x) != ts.end())
                    sorted.push_back (x);
            ts = sorted;

            currentNoChord = false;
            setFromStructure();
        };
        addAndMakeVisible (b);
    }

    bassBox.addItem ("ベースなし（ルート）"_ju, 1);

    for (int i = 0; i < 12; ++i)
        bassBox.addItem ("/" + juce::String (roots[i]), i + 2);

    bassBox.onChange = [this]
    {
        if (updating)
            return;

        const int id = bassBox.getSelectedId();
        current.bass = id >= 2 ? std::optional<std::string> (roots[id - 2]) : std::nullopt;
        currentNoChord = false;
        setFromStructure();
    };
    addAndMakeVisible (bassBox);

    noChordButton.setButtonText ("X（ノーコード）"_ju);
    noChordButton.onClick = [this]
    {
        currentNoChord = true;
        setFromStructure();
    };
    addAndMakeVisible (noChordButton);

    okButton.setButtonText ("OK");
    okButton.onClick = [this]
    {
        if (! valid)
            return;

        Result r;
        r.noChord = currentNoChord;

        if (! currentNoChord)
            r.chord = current;

        r.text = currentNoChord ? juce::String ("X") : toJuce (collab::chord::format (current));
        auto cb = onOk;
        close();

        if (cb)
            cb (r);
    };
    addAndMakeVisible (okButton);

    cancelButton.setButtonText ("キャンセル"_ju);
    cancelButton.onClick = [this] { close(); };
    addAndMakeVisible (cancelButton);

    deleteButton.setButtonText ("削除"_ju);
    deleteButton.setVisible (onDelete != nullptr);
    deleteButton.onClick = [this]
    {
        auto cb = onDelete;
        close();

        if (cb)
            cb();
    };
    addChildComponent (deleteButton);

    textEditor.setText (initialText.isEmpty() ? juce::String ("C") : initialText, juce::sendNotification);
    textChanged();
    setSize (560, 330);
}

void ChordEditor::show (const juce::String& initialText, std::function<void (Result)> onOk, std::function<void()> onDelete)
{
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new ChordEditor (initialText, std::move (onOk), std::move (onDelete)));
    o.dialogTitle = "コードの編集"_ju;
    o.dialogBackgroundColour = Theme::panel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;

    if (auto* w = o.launchAsync())
        if (auto* editor = dynamic_cast<ChordEditor*> (w->getContentComponent()))
            editor->textEditor.grabKeyboardFocus();
}

void ChordEditor::close()
{
    if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
        dw->exitModalState (0);
}

void ChordEditor::textChanged()
{
    if (updating)
        return;

    auto r = collab::chord::parse (toStd (textEditor.getText()));
    valid = r.ok();

    if (r.noChord)
    {
        currentNoChord = true;
        preview.setText ("→ X（ノーコード: この区間は発音しません）"_ju, juce::dontSendNotification);
        preview.setColour (juce::Label::textColourId, Theme::text);
    }
    else if (r.chord)
    {
        currentNoChord = false;
        current = *r.chord;
        preview.setText ("→ "_ju + toJuce (collab::chord::format (current)), juce::dontSendNotification);
        preview.setColour (juce::Label::textColourId, Theme::text);
    }
    else
    {
        preview.setText (toJuce (r.error), juce::dontSendNotification);
        preview.setColour (juce::Label::textColourId, Theme::warning);
    }

    okButton.setEnabled (valid);
    refreshButtons();
}

void ChordEditor::setFromStructure()
{
    const juce::ScopedValueSetter<bool> svs (updating, true);
    textEditor.setText (currentNoChord ? juce::String ("X") : toJuce (collab::chord::format (current)), false);
    valid = true;
    okButton.setEnabled (true);
    preview.setColour (juce::Label::textColourId, Theme::text);
    preview.setText (currentNoChord ? "→ X（ノーコード: この区間は発音しません）"_ju
                                    : "→ "_ju + toJuce (collab::chord::format (current)),
                     juce::dontSendNotification);
    refreshButtons();
}

void ChordEditor::refreshButtons()
{
    const juce::ScopedValueSetter<bool> svs (updating, true);
    const bool on = ! currentNoChord && valid;

    for (int i = 0; i < rootButtons.size(); ++i)
        rootButtons[i]->setToggleState (on && collab::chord::pitchClass (current.root) == collab::chord::pitchClass (roots[i]),
                                        juce::dontSendNotification);

    for (int i = 0; i < qualityButtons.size(); ++i)
        qualityButtons[i]->setToggleState (on && current.quality == qualities[i].quality, juce::dontSendNotification);

    const auto& allT = collab::chord::allTensions();

    for (int i = 0; i < tensionButtons.size(); ++i)
        tensionButtons[i]->setToggleState (on && std::find (current.tensions.begin(), current.tensions.end(), allT[(size_t) i])
                                                    != current.tensions.end(),
                                           juce::dontSendNotification);

    int bassId = 1;

    if (on && current.bass)
        for (int i = 0; i < 12; ++i)
            if (collab::chord::pitchClass (*current.bass) == collab::chord::pitchClass (roots[i]))
                bassId = i + 2;

    bassBox.setSelectedId (bassId, juce::dontSendNotification);
    noChordButton.setToggleState (currentNoChord, juce::dontSendNotification);
}

void ChordEditor::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panel);
    g.setColour (Theme::textDim);
    g.setFont (juce::FontOptions (12.0f));

    auto label = [&] (juce::Component& c, const juce::String& text)
    {
        g.drawText (text, 10, c.getY(), 80, buttonH, juce::Justification::centredLeft);
    };

    label (*rootButtons[0], "ルート"_ju);
    label (*qualityButtons[0], "タイプ"_ju);
    label (*tensionButtons[0], "テンション"_ju);
    label (bassBox, "ベース音"_ju);
}

void ChordEditor::resized()
{
    auto area = getLocalBounds().reduced (10);
    textEditor.setBounds (area.removeFromTop (34));
    preview.setBounds (area.removeFromTop (22));
    area.removeFromTop (6);

    auto layoutRow = [&] (juce::OwnedArray<juce::TextButton>& buttons, int from, int count)
    {
        auto row = area.removeFromTop (buttonH).withTrimmedLeft (80);
        const int w = row.getWidth() / 8;

        for (int i = from; i < from + count && i < buttons.size(); ++i)
            buttons[i]->setBounds (row.removeFromLeft (w).reduced (1));

        area.removeFromTop (2);
    };

    layoutRow (rootButtons, 0, 8);
    layoutRow (rootButtons, 8, 4);
    area.removeFromTop (4);
    layoutRow (qualityButtons, 0, 8);
    layoutRow (qualityButtons, 8, 8);
    area.removeFromTop (4);
    layoutRow (tensionButtons, 0, 7);
    area.removeFromTop (4);

    auto bassRow = area.removeFromTop (buttonH).withTrimmedLeft (80);
    bassBox.setBounds (bassRow.removeFromLeft (180));
    bassRow.removeFromLeft (10);
    noChordButton.setBounds (bassRow.removeFromLeft (140));

    auto bottom = area.removeFromBottom (30);
    okButton.setBounds (bottom.removeFromRight (100));
    bottom.removeFromRight (6);
    cancelButton.setBounds (bottom.removeFromRight (100));
    deleteButton.setBounds (bottom.removeFromLeft (80));
}
