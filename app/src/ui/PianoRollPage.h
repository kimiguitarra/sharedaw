#pragma once

#include "ui/PianoRoll.h"
#include "ui/KeyLane.h"
#include "ui/ChordLane.h"
#include "ui/MarkerLane.h"

/**
    ピアノロールの画面（ミキサーと同じく別のウィンドウで画面いっぱいに開く）。
    ピアノロールで編集するためだけの画面なので、インスペクターやトラックは出さない。
    ルーラーの下に、キー・コード・マーカーの段を小さく出す（ピアノロールと同じ横の位置・拡大率）。
*/
class PianoTopLanes  : public PianoRollView::TopStrip
{
public:
    explicit PianoTopLanes (AppContext&);

    void setLeftWidth (int) override;
    int preferredHeight() const override    { return keyHeight + chordHeight + markerHeight; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    static constexpr int keyHeight = 20, chordHeight = 30, markerHeight = 20;
    KeyLane keyLane;
    ChordLane chordLane;
    MarkerLane markerLane;
    int leftWidth = 70;
};
