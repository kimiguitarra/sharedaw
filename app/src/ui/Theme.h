#pragma once

#include "Common.h"
#include "EditorState.h"

namespace Theme
{
    const juce::Colour background     { 0xff1e2126 };
    const juce::Colour panel          { 0xff262a30 };
    const juce::Colour panelLight     { 0xff30353c };
    const juce::Colour lane           { 0xff22262b };
    const juce::Colour laneAlt        { 0xff1f2227 };
    const juce::Colour gridBar        { 0xff4a515b };
    const juce::Colour gridBeat       { 0xff3a414a };
    const juce::Colour gridSub        { 0xff2f353c };
    const juce::Colour text           { 0xffe6e8eb };
    const juce::Colour textDim        { 0xff9aa3ad };
    const juce::Colour accent         { 0xff4fc3f7 };
    const juce::Colour selection      { 0xffffd54f };
    const juce::Colour playhead       { 0xffff5252 };
    const juce::Colour loopRange      { 0x3355c1ff };
    const juce::Colour warning        { 0xffffb74d };
    const juce::Colour tempo          { 0xffba68c8 };
    const juce::Colour meter          { 0xff4db6ac };

    // 状態の色（落ち着いた色。同期の状況など）
    const juce::Colour ok             { 0xff66bb6a };
    const juce::Colour danger         { 0xffef5350 };

    /** トラックの既定の色（追加順に使う）。 */
    juce::Colour trackColour (int index);
    juce::String trackColourHex (int index);

    juce::Colour parseColour (const std::string& hex, juce::Colour fallback = juce::Colours::grey);

    /** ツールのアイコン（矢印・鉛筆）。area に収まるように描いた Path。 */
    juce::Path selectToolIcon (juce::Rectangle<float> area);
    juce::Path pencilToolIcon (juce::Rectangle<float> area);

    /** 鉛筆ツールのマウスカーソル（先端がホットスポット）。 */
    const juce::MouseCursor& pencilCursor();

    juce::Path splitToolIcon (juce::Rectangle<float> area);

    /** ツールごとのマウスカーソル（選択ツールは普通の矢印）。 */
    const juce::MouseCursor& toolCursor (EditTool);

    /** 見出しの帯（Cubase のインスペクターのセクション見出しのように、少し明るい帯に文字）。 */
    void drawSectionHeader (juce::Graphics&, juce::Rectangle<int> area, const juce::String& title, const juce::String& right = {});

    /** 状態の小さな丸（接続中・オフラインなど）。 */
    void drawStatusDot (juce::Graphics&, juce::Rectangle<float> area, juce::Colour);

    class LookAndFeel  : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel();
    };
}
