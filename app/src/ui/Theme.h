#pragma once

#include "Common.h"
#include "EditorState.h"

namespace Theme
{
    // ポップな配色: 藍色がかった暗い下地に、キャンディのような鮮やかな差し色
    const juce::Colour background     { 0xff191a2c };
    const juce::Colour panel          { 0xff23243c };
    const juce::Colour panelLight     { 0xff30325a };
    const juce::Colour lane           { 0xff1f2036 };
    const juce::Colour laneAlt        { 0xff1b1c30 };
    const juce::Colour gridBar        { 0xff4d5184 };
    const juce::Colour gridBeat       { 0xff373a60 };
    const juce::Colour gridSub        { 0xff2a2c4a };
    const juce::Colour text           { 0xfff5f5ff };
    const juce::Colour textDim        { 0xffa6a9d6 };
    const juce::Colour accent         { 0xff5ee7ff };
    const juce::Colour selection      { 0xffffe066 };
    const juce::Colour playhead       { 0xffff5c8a };
    const juce::Colour loopRange      { 0x3366e0ff };
    const juce::Colour warning        { 0xffffa94d };
    const juce::Colour tempo          { 0xffd98cff };
    const juce::Colour meter          { 0xff4de0c2 };

    // 差し色（同期・ボタンなど）
    const juce::Colour pink           { 0xffff6fb5 };
    const juce::Colour orange         { 0xffff9f43 };
    const juce::Colour green          { 0xff4ade80 };
    const juce::Colour blue           { 0xff60a5fa };
    const juce::Colour purple         { 0xffa78bfa };
    const juce::Colour red            { 0xffff5c6c };

    /** 角丸の半径（ボタン・カード）。 */
    constexpr float cornerRadius = 7.0f;

    /** 人ごとの色（名前・ID から決める。アバターの丸など）。 */
    juce::Colour personColour (const juce::String& key);

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

    /** 角丸のカード（同期パネルなど）。上に色の帯を付ける。 */
    void drawCard (juce::Graphics&, juce::Rectangle<float> area, juce::Colour stripe);

    /** 丸いアバター（頭文字）。 */
    void drawAvatar (juce::Graphics&, juce::Rectangle<float> area, const juce::String& name);

    /** 色付きの丸いバッジ（「↑ 3」など）。 */
    void drawPill (juce::Graphics&, juce::Rectangle<float> area, juce::Colour fill, const juce::String& label, float fontSize = 12.0f);

    class LookAndFeel  : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel();

        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                                   bool highlighted, bool down) override;
        void drawComboBox (juce::Graphics&, int width, int height, bool down, int buttonX, int buttonY, int buttonW, int buttonH,
                           juce::ComboBox&) override;
        void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;
        void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
        void drawProgressBar (juce::Graphics&, juce::ProgressBar&, int width, int height, double progress, const juce::String& text) override;
        void drawTickBox (juce::Graphics&, juce::Component&, float x, float y, float w, float h, bool ticked, bool enabled,
                          bool highlighted, bool down) override;
        void drawAlertBox (juce::Graphics&, juce::AlertWindow&, const juce::Rectangle<int>& textArea, juce::TextLayout&) override;
    };
}
