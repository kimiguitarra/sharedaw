#pragma once

// オートメーションのレーンの表示（値 ↔ 高さ、名前、値の文字）。タイムラインとトラックヘッダーで使う。

#include <cmath>

#include "Common.h"
#include "collab/Automation.h"

namespace AutomationView
{
    /** 値を 0〜1（下〜上）にする。音量はフェーダーと同じように、0 dB が上から 2 割くらいの所。パンは真ん中が C。 */
    inline double toNorm (const std::string& param, double value)
    {
        if (param == "pan")
            return (juce::jlimit (-1.0, 1.0, value) + 1.0) / 2.0;

        const double x = (juce::jlimit (-60.0, 6.0, value) + 60.0) / 66.0;
        return std::pow (x, 2.3);
    }

    inline double fromNorm (const std::string& param, double norm)
    {
        norm = juce::jlimit (0.0, 1.0, norm);

        if (param == "pan")
            return std::round ((norm * 2.0 - 1.0) * 100.0) / 100.0;

        return std::round ((66.0 * std::pow (norm, 1.0 / 2.3) - 60.0) * 10.0) / 10.0;
    }

    inline juce::String paramName (const std::string& param)
    {
        return param == "pan" ? juce::String::fromUTF8 ("パン") : juce::String::fromUTF8 ("音量");   // utf8-std
    }

    inline juce::String valueText (const std::string& param, double value)
    {
        if (param == "pan")
        {
            const int v = juce::roundToInt (value * 100.0);
            return v == 0 ? juce::String ("C") : (v < 0 ? "L" + juce::String (-v) : "R" + juce::String (v));
        }

        return value <= -59.95 ? juce::String ("-inf dB") : (value > 0 ? "+" : "") + juce::String (value, 1) + " dB";
    }
}
