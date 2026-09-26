#pragma once

#include "ProjectDocument.h"
#include "EditorState.h"

/** 小節番号のルーラー。クリックで再生位置の移動、ドラッグでループ範囲の設定。 */
class Ruler  : public juce::Component,
               private juce::ChangeListener
{
public:
    Ruler (ProjectDocument&, EditorState&, TimeAxis&);
    ~Ruler() override;

    std::function<void (double tick)> onSeek;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    std::function<void (const juce::MouseEvent&, const juce::MouseWheelDetails&)> onWheel;

private:
    ProjectDocument& document;
    EditorState& state;
    TimeAxis& axis;
    collab::Tick dragStartTick = 0;
    bool draggingLoop = false;

    collab::Tick snapToBeat (double tick) const;
    void changeListenerCallback (juce::ChangeBroadcaster*) override    { repaint(); }
};

/** 再生位置の縦線（マウス操作は透過）。 */
class PlayheadOverlay  : public juce::Component
{
public:
    explicit PlayheadOverlay (TimeAxis& a) : axis (a)
    {
        setInterceptsMouseClicks (false, false);
    }

    void setTick (double tick)
    {
        const int newX = (int) std::floor (axis.tickToX (tick));

        if (newX != lastX)
        {
            repaint (lastX - 1, 0, 3, getHeight());
            repaint (newX - 1, 0, 3, getHeight());
            lastX = newX;
        }
    }

    void refresh()      { lastX = -100000; repaint(); }

    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colour (0xffff5252));
        g.fillRect (lastX, 0, 1, getHeight());
    }

private:
    TimeAxis& axis;
    int lastX = -100000;
};
