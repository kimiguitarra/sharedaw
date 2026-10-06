#pragma once

// ピアノロールの部品（PianoRoll*.cpp）で共通に使う値と小さな関数

#include "Common.h"

namespace PianoRollDetail
{
    /** ドラムの音の仲間（行の区切り線用）: 0 キック, 1 スネア, 2 ハイハット, 3 タム, 4 シンバル, 5 その他 */
    inline int drumFamily (int pitch)
    {
        switch (pitch)
        {
            case 35: case 36:                                   return 0;
            case 37: case 38: case 39: case 40:                 return 1;
            case 42: case 44: case 46:                          return 2;
            case 41: case 43: case 45: case 47: case 48: case 50: return 3;
            case 49: case 51: case 52: case 53: case 55: case 57: case 59: return 4;
            default:                                            return 5;
        }
    }

    inline constexpr int toolbarHeight = 30;
    inline constexpr int rulerHeight = 24;
    inline constexpr int velocityHeight = 70;
    inline constexpr int pitchBendHeight = 120;
    inline constexpr int scrollBarSize = 12;
    inline constexpr float edgeGrab = 6.0f;

    inline bool isBlackKey (int pitch)
    {
        const int n = pitch % 12;
        return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
    }

    inline juce::Colour velocityColour (int velocity, juce::Colour base)
    {
        return base.withMultipliedBrightness (0.55f + 0.45f * (float) velocity / 127.0f);
    }
}
