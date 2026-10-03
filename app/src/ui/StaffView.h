#pragma once

#include "Common.h"

class PianoRollView;

/**
    ピアノロールの代わりに出す五線譜（大譜表: ト音記号とヘ音記号）。見るための表示で、編集はピアノロールで行う。

    - 横の位置はピアノロールと同じ（ルーラー・スクロール・拡大がそのまま使える）。音符の間隔は譜面の清書ではなく時間どおり
    - 音名はその小節のキーの調号に合わせて書き、調号と違う音にだけ臨時記号を付ける（小節の中では同じ高さの音に引き継ぐ）
    - 音の長さは、符頭（白・黒）・符尾・旗と、後ろに伸びる薄い帯で表す
*/
class StaffView  : public juce::Component
{
public:
    explicit StaffView (PianoRollView& o) : owner (o) {}

    /** 左の音部記号と調号を描く幅（ピアノロールの鍵盤の幅と同じにする）。 */
    void setLeftWidth (int w)    { leftWidth = w; }

    void paint (juce::Graphics&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    PianoRollView& owner;
    int leftWidth = 70;
};
