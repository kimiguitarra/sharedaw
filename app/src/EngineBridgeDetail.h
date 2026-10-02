#pragma once

// EngineBridge*.cpp で共通に使う小さな関数

#include "Common.h"

namespace EngineBridgeDetail
{
    inline te::TimePosition secondsToTime (double s)     { return te::TimePosition::fromSeconds (juce::jmax (0.0, s)); }
    inline te::BeatPosition secondsToBeats (double s)    { return te::BeatPosition::fromBeats (s); }   // 60BPM 固定なので 1拍 = 1秒
}
