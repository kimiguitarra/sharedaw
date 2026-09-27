#include "MidiInputPanel.h"

#include "Theme.h"

namespace
{
    constexpr int rowHeight = 26;

    void drawBar (juce::Graphics& g, juce::Rectangle<float> r, float level)
    {
        g.setColour (Theme::background);
        g.fillRoundedRectangle (r, 3.0f);

        if (level > 0.01f)
        {
            g.setColour (juce::Colour (0xff66bb6a).interpolatedWith (juce::Colour (0xffffd54f), juce::jlimit (0.0f, 1.0f, (level - 0.6f) * 2.5f)));
            g.fillRoundedRectangle (r.withWidth (r.getWidth() * juce::jlimit (0.0f, 1.0f, level)), 3.0f);
        }
    }
}

MidiInputPanel::MidiInputPanel (EngineBridge& e) : engine (e)
{
    title.setText ("MIDI キーボード（MIDI 入力）"_ju, juce::dontSendNotification);
    title.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    addAndMakeVisible (title);

    note.setText ("鍵盤を弾くとバーが動きます。選択中の MIDI トラックの音源で鳴り、R で録音できます。"_ju, juce::dontSendNotification);
    note.setFont (juce::FontOptions (12.0f));
    note.setColour (juce::Label::textColourId, Theme::textDim);
    addAndMakeVisible (note);

    empty.setText ("MIDI 機器が見つかりません。つないだ後、少し待つと表示されます。"_ju, juce::dontSendNotification);
    empty.setFont (juce::FontOptions (12.0f));
    empty.setColour (juce::Label::textColourId, Theme::textDim);
    addChildComponent (empty);

    rebuild();
    startTimerHz (30);
}

void MidiInputPanel::rebuild()
{
    juce::StringArray names;

    for (auto& m : engine.getMidiInputs())
        names.add (m.name);

    if (names == shownNames)
        return;

    shownNames = names;
    rows.clear();

    for (auto& m : engine.getMidiInputs())
    {
        auto* row = rows.add (new Row());
        row->addAndMakeVisible (row->toggle);
        row->toggle.setButtonText (m.name);
        row->toggle.setToggleState (m.enabled, juce::dontSendNotification);
        row->toggle.setColour (juce::ToggleButton::tickColourId, Theme::accent);
        row->toggle.onClick = [this, row, name = m.name] { engine.setMidiInputEnabled (name, row->toggle.getToggleState()); };
        addAndMakeVisible (row);
    }

    empty.setVisible (rows.isEmpty());
    resized();
}

void MidiInputPanel::timerCallback()
{
    // 機器の抜き差しは 1 秒ごとに確認する
    if (++ticks % 30 == 0)
        rebuild();

    const auto inputs = engine.getMidiInputs();

    for (int i = 0; i < rows.size() && i < (int) inputs.size(); ++i)
    {
        rows[i]->toggle.setToggleState (inputs[(size_t) i].enabled, juce::dontSendNotification);

        if (std::abs (rows[i]->activity - inputs[(size_t) i].activity) > 0.005f)
        {
            rows[i]->activity = inputs[(size_t) i].activity;
            rows[i]->repaint();
        }
    }
}

void MidiInputPanel::paint (juce::Graphics& g)
{
    g.setColour (Theme::background);
    g.drawHorizontalLine (0, 8.0f, (float) getWidth() - 8.0f);
}

void MidiInputPanel::resized()
{
    auto area = getLocalBounds().reduced (8, 4);
    title.setBounds (area.removeFromTop (22));
    note.setBounds (area.removeFromTop (20));
    empty.setBounds (area.removeFromTop (rowHeight));

    for (auto* r : rows)
        r->setBounds (area.removeFromTop (rowHeight));
}

void MidiInputPanel::Row::resized()
{
    auto area = getLocalBounds();
    area.removeFromRight (190);
    toggle.setBounds (area);
}

void MidiInputPanel::Row::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().removeFromRight (180.0f).reduced (0.0f, 8.0f);
    drawBar (g, r, activity);
}

//==============================================================================
void MidiActivityLight::setLevel (float l)
{
    if (std::abs (l - level) > 0.01f)
    {
        level = l;
        repaint();
    }
}

void MidiActivityLight::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f, 6.0f);
    g.setColour (Theme::textDim);
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText ("MIDI", r.removeFromTop (12.0f), juce::Justification::centred);
    drawBar (g, r.reduced (0.0f, 4.0f), level);
}
