#include "Ruler.h"

#include "TimeGrid.h"

Ruler::Ruler (ProjectDocument& d, EditorState& s, TimeAxis& a)
    : document (d), state (s), axis (a)
{
    document.addChangeListener (this);
    state.addChangeListener (this);
}

Ruler::~Ruler()
{
    document.removeChangeListener (this);
    state.removeChangeListener (this);
}

void Ruler::paint (juce::Graphics& g)
{
    const auto& map = document.getTempoMap();
    g.fillAll (Theme::panel);

    // ループ範囲
    if (state.loopEnd > state.loopStart)
    {
        const auto x1 = (float) axis.tickToX ((double) state.loopStart);
        const auto x2 = (float) axis.tickToX ((double) state.loopEnd);
        g.setColour (state.loopEnabled ? Theme::accent.withAlpha (0.45f) : Theme::textDim.withAlpha (0.25f));
        g.fillRect (juce::Rectangle<float> (x1, 0.0f, x2 - x1, (float) getHeight() * 0.35f));
    }

    const int step = TimeGrid::barLabelStep (axis, map, 36.0);
    g.setFont (juce::FontOptions (12.0f));

    TimeGrid::forEachVisibleBar (axis, map, getWidth(), [&] (int bar, collab::Tick start, collab::TimeSignature sig)
    {
        const int x = (int) axis.tickToX ((double) start);

        if ((bar - 1) % step == 0)
        {
            g.setColour (Theme::gridBar.brighter (0.4f));
            g.drawVerticalLine (x, (float) getHeight() * 0.35f, (float) getHeight());
            g.setColour (Theme::text);
            g.drawText (juce::String (bar), x + 3, getHeight() / 3, 40, getHeight() - getHeight() / 3,
                        juce::Justification::centredLeft, false);
        }

        if (step == 1 && sig.ticksPerBeat() * axis.pixelsPerTick() >= 10.0)
        {
            g.setColour (Theme::gridBar);

            for (int b = 1; b < sig.numerator; ++b)
                g.drawVerticalLine ((int) axis.tickToX ((double) (start + b * sig.ticksPerBeat())),
                                    (float) getHeight() * 0.75f, (float) getHeight());
        }
    });

    g.setColour (Theme::background);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
}

void Ruler::seekTo (float x)
{
    if (onSeek)
        onSeek (juce::jmax (0.0, axis.xToTick (x)));
}

void Ruler::mouseDown (const juce::MouseEvent& e)
{
    seekTo (e.position.x);
}

void Ruler::mouseDrag (const juce::MouseEvent& e)
{
    seekTo (e.position.x);
}

void Ruler::mouseUp (const juce::MouseEvent&)
{
}

void Ruler::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (onWheel)
        onWheel (e, w);
}
