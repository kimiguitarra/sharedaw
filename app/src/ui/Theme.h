#pragma once

#include "Common.h"

namespace Theme
{
    const juce::Colour background     { 0xff1e2126 };
    const juce::Colour panel          { 0xff262a30 };
    const juce::Colour panelLight     { 0xff30353c };
    const juce::Colour lane           { 0xff22262b };
    const juce::Colour laneAlt        { 0xff1f2227 };
    const juce::Colour gridBar        { 0xff4a515b };
    const juce::Colour gridBeat       { 0xff343a42 };
    const juce::Colour gridSub        { 0xff2a2f35 };
    const juce::Colour text           { 0xffe6e8eb };
    const juce::Colour textDim        { 0xff9aa3ad };
    const juce::Colour accent         { 0xff4fc3f7 };
    const juce::Colour selection      { 0xffffd54f };
    const juce::Colour playhead       { 0xffff5252 };
    const juce::Colour loopRange      { 0x3355c1ff };
    const juce::Colour warning        { 0xffffb74d };
    const juce::Colour tempo          { 0xffba68c8 };
    const juce::Colour meter          { 0xff4db6ac };

    /** トラックの既定の色（追加順に使う）。 */
    juce::Colour trackColour (int index);
    juce::String trackColourHex (int index);

    juce::Colour parseColour (const std::string& hex, juce::Colour fallback = juce::Colours::grey);

    class LookAndFeel  : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel();
    };
}
