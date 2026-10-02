#pragma once

#include <optional>

#include <juce_core/juce_core.h>

/** 音量（dB）・パンの表示と、打ち込んだ文字の読み取り（ミキサーとトラックヘッダーで共通）。 */
namespace ValueText
{
    inline juce::String formatDb (double db)
    {
        return db <= -59.9 ? juce::String ("-inf") : (db > 0.05 ? "+" : "") + juce::String (db, 1);
    }

    /** dB を単位付きで（「+3.0 dB」）。EQ・コンプの値の表示。 */
    inline juce::String formatDbUnit (double db)
    {
        return (db > 0.05 ? "+" : "") + juce::String (db, 1) + " dB";
    }

    /** 周波数（「120 Hz」「2.50 kHz」「12.0 kHz」）。 */
    inline juce::String formatHz (double hz)
    {
        return hz >= 1000.0 ? juce::String (hz / 1000.0, hz >= 10000.0 ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz";
    }

    /** 時間（10 ms 未満は小数 2 桁）。 */
    inline juce::String formatMs (double ms)
    {
        return ms < 10.0 ? juce::String (ms, 2) + " ms" : juce::String (juce::roundToInt (ms)) + " ms";
    }

    inline juce::String formatPan (double v)
    {
        const int n = juce::roundToInt (std::abs (v) * 100.0);
        return n == 0 ? juce::String ("C") : (v < 0 ? "L" : "R") + juce::String (n);
    }

    /** 打ち込んだ dB（「-6」「+3.5」「-inf」「6dB」など）。数字でなければ nullopt。 */
    inline std::optional<double> parseDb (juce::String text, double minDb, double maxDb)
    {
        text = text.trim().toLowerCase().removeCharacters ("db ");

        if (text.contains ("inf") || text == "off")
            return minDb;

        if (! text.containsAnyOf ("0123456789"))
            return std::nullopt;

        return juce::jlimit (minDb, maxDb, text.getDoubleValue());
    }

    /** 打ち込んだパン（「L30」「R20」「C」「-30」「30」）を -1〜1 に。 */
    inline std::optional<double> parsePan (juce::String text)
    {
        text = text.trim().toUpperCase();

        if (text == "C" || text == "0")
            return 0.0;

        double sign = 1.0;

        if (text.startsWithChar ('L'))       { sign = -1.0; text = text.substring (1); }
        else if (text.startsWithChar ('R'))  { text = text.substring (1); }

        if (! text.containsAnyOf ("0123456789"))
            return std::nullopt;

        return juce::jlimit (-1.0, 1.0, sign * text.getDoubleValue() / 100.0);
    }
}
