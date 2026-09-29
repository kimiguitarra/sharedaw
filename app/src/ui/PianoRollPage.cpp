#include "ui/PianoRollPage.h"
#include "ui/TimelineView.h"
#include "ui/Theme.h"

PianoTopLanes::PianoTopLanes (AppContext& c)
    : keyLane (c), chordLane (c), markerLane (c)
{
    for (auto* lane : std::initializer_list<juce::Component*> { &keyLane, &chordLane, &markerLane })
        addAndMakeVisible (lane);

    keyLane.setAxis (&c.state.pianoRoll);
    chordLane.setAxis (&c.state.pianoRoll);
    markerLane.setAxis (&c.state.pianoRoll);
}

void PianoTopLanes::setLeftWidth (int w)
{
    if (w != leftWidth)
    {
        leftWidth = w;
        resized();
        repaint();
    }
}

void PianoTopLanes::paint (juce::Graphics& g)
{
    g.setColour (Theme::panel);
    g.fillRect (0, 0, leftWidth, getHeight());

    // 左に段の名前（小さめ）
    const std::pair<const char*, juce::Component*> rows[] = { { "key", &keyLane }, { "chord", &chordLane }, { "marker", &markerLane } };
    g.setFont (juce::FontOptions (12.5f, juce::Font::bold));

    for (auto& [key, lane] : rows)
    {
        g.setColour (TimelineView::laneColour (key));
        g.drawText (TimelineView::laneTitle (key), 6, lane->getY(), leftWidth - 8, lane->getHeight(), juce::Justification::centredLeft, true);
        g.setColour (Theme::overlay (0.08f));
        g.drawHorizontalLine (lane->getBottom() - 1, 0.0f, (float) getWidth());
    }
}

void PianoTopLanes::resized()
{
    auto area = getLocalBounds().withTrimmedLeft (leftWidth);
    keyLane.setBounds (area.removeFromTop (keyHeight));
    chordLane.setBounds (area.removeFromTop (chordHeight));
    markerLane.setBounds (area.removeFromTop (markerHeight));
}
