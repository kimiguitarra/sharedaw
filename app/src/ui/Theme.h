#pragma once

#include "Common.h"
#include "EditorState.h"

namespace Theme
{
    // 色はダーク（既定）とライト（白を基調にしたニューモーフィズム）の 2 通り。起動時に applyPalette() で決める
    // （部品は作るときに色を読むので、切り替えは再起動で反映する）
    // Cubase のように、真っ黒ではなく落ち着いた灰色
    inline juce::Colour background     { 0xff2c2f33 };
    inline juce::Colour panel          { 0xff383b40 };
    inline juce::Colour panelLight     { 0xff464a50 };
    inline juce::Colour lane           { 0xff393c41 };
    inline juce::Colour laneAlt        { 0xff34373c };
    inline juce::Colour gridBar        { 0xff676d75 };
    inline juce::Colour gridBeat       { 0xff51565d };
    inline juce::Colour gridSub        { 0xff45494f };
    inline juce::Colour text           { 0xffeceef0 };
    inline juce::Colour textDim        { 0xffb0b6bd };

    // 入力欄: 周りより暗くして、はっきりした枠を付ける（どこに打てるか分かるように）
    inline juce::Colour field          { 0xff232528 };
    inline juce::Colour fieldOutline   { 0x66ffffff };
    inline juce::Colour accent         { 0xff4fc3f7 };
    inline juce::Colour selection      { 0xffffd54f };
    inline juce::Colour playhead       { 0xffff5252 };
    inline juce::Colour loopRange      { 0x3355c1ff };
    inline juce::Colour warning        { 0xffffb74d };
    inline juce::Colour tempo          { 0xffba68c8 };
    inline juce::Colour meter          { 0xff4db6ac };

    // 状態の色（落ち着いた色。同期の状況など）
    inline juce::Colour ok             { 0xff66bb6a };
    inline juce::Colour danger         { 0xffef5350 };


    inline bool light = false;

    /** 背景に重ねる薄い色（ホバー・区切り）。ダークは白、ライトは黒を薄く（明るい面では白が見えないので）。 */
    /** オーディオクリップの地と波形の色（ダークは暗い地に明るい波形、ライトは明るい地に濃い波形）。 */
    inline juce::Colour clipBody (juce::Colour c)
    {
        return light ? c.withMultipliedSaturation (0.45f).interpolatedWith (juce::Colours::white, 0.62f)
                     : c.withMultipliedSaturation (0.55f).withMultipliedBrightness (0.42f);
    }

    inline juce::Colour clipWave (juce::Colour c)
    {
        return light ? c.withMultipliedSaturation (1.1f).withMultipliedBrightness (0.62f) : c.brighter (0.55f);
    }

    inline juce::Colour overlay (float alpha)   { return light ? juce::Colours::black.withAlpha (alpha * 0.7f) : juce::Colours::white.withAlpha (alpha); }

    /** 色を切り替える（アプリの起動時、部品を作る前に呼ぶ）。 */
    void applyPalette (bool useLight);

    /**
        ニューモーフィズムの面（ライト）: 左上に白い光、右下に柔らかい影で浮き上がって見える。pressed なら凹んで見える。
        ダークのときは不透明の板。
    */
    void drawRaised (juce::Graphics&, juce::Rectangle<float> area, float radius, bool pressed = false, juce::Colour fill = {},
                     float depth = 3.0f);

    /** ボタンの中で影が切れないように面を内側に寄せる量（ライト）。影は部品の外に描けないので、角が四角く切れて見えないように。 */
    inline float raisedInset (float depth = 2.0f)  { return light ? depth * 1.6f + 1.0f : 1.0f; }

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

    /**
        Liquid Glass 風の面。下地の上に半透明の白を重ね、上の縁に光、細い明るい縁取りを付ける（色は付けない）。
        tint を渡すと、その色をごく薄く混ぜる（選択中・状態の色分け）。
    */
    void drawGlass (juce::Graphics&, juce::Rectangle<float> area, float radius, juce::Colour tint = {});

    /** 状態の小さな丸（接続中・オフラインなど）。 */
    void drawStatusDot (juce::Graphics&, juce::Rectangle<float> area, juce::Colour);

    /** アイコンの形（塗りつぶし用）。name: snap / flagL / flagR / follow / loop / metronome / stop / play / pause / record */
    juce::Path iconPath (const juce::String& name, juce::Rectangle<float> area);

    /** アイコンだけのボタン（TextButton と同じように使える。文字は描かず、ツールチップで説明する）。 */
    class IconButton  : public juce::TextButton
    {
    public:
        // キーボードのフォーカスを取らない（Space が再生ではなくボタンを押してしまうので）
        explicit IconButton (juce::String iconName) : icon (std::move (iconName)) { setWantsKeyboardFocus (false); }

        void setIcon (const juce::String& name)     { icon = name; repaint(); }
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    private:
        juce::String icon;
    };

    /** Liquid Glass 風の見た目（半透明の白のカプセル、明るい縁、上の光。色は落ち着いたまま）。 */
    class LookAndFeel  : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel();

        /** 今の色（Theme::applyPalette の後）で部品の既定の色を設定し直す。 */
        void applyColours();

        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                                   bool highlighted, bool down) override;
        juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
        juce::Font getPopupMenuFont() override;
        juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;
        void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
        juce::Font getMenuBarFont (juce::MenuBarComponent&, int itemIndex, const juce::String& itemText) override;
        void drawComboBox (juce::Graphics&, int width, int height, bool down, int buttonX, int buttonY, int buttonW, int buttonH,
                           juce::ComboBox&) override;
        void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
        void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;
        void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos, float minSliderPos,
                               float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;
        void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height, bool isScrollbarVertical,
                            int thumbStartPosition, int thumbSize, bool isMouseOver, bool isMouseDown) override;
        void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    };
}
