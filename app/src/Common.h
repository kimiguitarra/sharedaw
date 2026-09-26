#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion;

/** std::string (UTF-8) <-> juce::String */
inline juce::String toJuce (const std::string& s)       { return juce::String::fromUTF8 (s.data(), (int) s.size()); }
inline std::string toStd (const juce::String& s)        { return s.toStdString(); }

/** UTF-8 の文字列リテラルから juce::String を作る（"日本語"_ju）。
    juce::String (const char*) は ASCII として扱うため、日本語は必ずこれを使う。 */
inline juce::String operator""_ju (const char* s, std::size_t n)   { return juce::String::fromUTF8 (s, (int) n); }
