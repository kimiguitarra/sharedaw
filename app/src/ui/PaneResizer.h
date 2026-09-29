#pragma once

#include "Common.h"
#include "Theme.h"

/**
    区切りの細い帯。左右にドラッグして隣の領域の幅を変える（インスペクター・トラックヘッダー・同期パネル）。
    onResize には押したときからの横の移動量（ピクセル）が来る。離したら onResizeEnd（幅の保存など）。
*/
class PaneResizer  : public juce::Component
{
public:
    PaneResizer()
    {
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
        setRepaintsOnMouseActivity (true);
    }

    std::function<void (int deltaX)> onResize;
    std::function<void()> onResizeEnd;

    void paint (juce::Graphics& g) override
    {
        if (isMouseOverOrDragging())
        {
            g.setColour (Theme::accent.withAlpha (0.6f));
            g.fillRect (getLocalBounds().withSizeKeepingCentre (2, getHeight()));
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override    { if (onResize) onResize (e.getDistanceFromDragStartX()); }
    void mouseUp (const juce::MouseEvent&) override         { if (onResizeEnd) onResizeEnd(); }
};
