#include "Theme.h"

namespace Theme
{

namespace
{
    // トラックの色（色相の順に 24 色。メニューもこの順に並べる）
    struct Swatch { juce::uint32 argb; const char* name; };
    const Swatch palette[] = {
        { 0xffE8506E, "赤みのピンク" }, { 0xffE57373, "赤" },          { 0xffB23A3A, "暗い赤" },   // utf8-std
        { 0xffF06292, "ピンク" },       { 0xffC8611E, "暗いオレンジ" }, { 0xffFF8A50, "オレンジ" },   // utf8-std
        { 0xffF2A93B, "黄色みのオレンジ" }, { 0xffFFD54F, "黄色" },     { 0xffC9B037, "からし色" },   // utf8-std
        { 0xffAED581, "黄緑" },         { 0xff81C784, "緑" },          { 0xff3E8E5A, "深緑" },   // utf8-std
        { 0xff4DB6AC, "青緑" },         { 0xff26A69A, "暗い青緑" },     { 0xff5BB8E8, "水色" },   // utf8-std
        { 0xff64B5F6, "空色" },         { 0xff4A7FD4, "青" },          { 0xff2F5DA8, "暗い青" },   // utf8-std
        { 0xff7986CB, "藍" },           { 0xff9575CD, "紫" },          { 0xffBA68C8, "薄紫" },   // utf8-std
        { 0xff8D6E63, "茶色" },         { 0xffA1887F, "薄茶" },        { 0xff90A4AE, "灰色" },   // utf8-std
    };

    // 新しいトラックに順に付ける色（隣のトラックと見分けやすい順）
    const int defaultOrder[] = { 1, 15, 10, 5, 19, 12, 3, 9, 16, 7, 20, 13 };
}

int numTrackColours()     { return (int) std::size (palette); }

juce::Colour paletteColour (int i)
{
    return juce::Colour (palette[(size_t) juce::jlimit (0, numTrackColours() - 1, i)].argb);
}

juce::String paletteColourName (int i)
{
    return juce::String::fromUTF8 (palette[(size_t) juce::jlimit (0, numTrackColours() - 1, i)].name);
}

juce::Colour trackColour (int index)
{
    return paletteColour (defaultOrder[(size_t) juce::jmax (0, index) % std::size (defaultOrder)]);
}

juce::String colourForNewTrack (const juce::String& name, const std::string& instrumentId, int index)
{
    // 楽器ごとの色: ギターは暗いオレンジ、ドラムは黄色みのオレンジ、ベースは暗い青、ピアノは水色、シンセ・メロディは赤みのピンク
    const auto n = name.toLowerCase();
    auto has = [&n] (std::initializer_list<const char*> words)
    {
        for (auto* w : words)
            if (n.contains (juce::String::fromUTF8 (w)))
                return true;

        return false;
    };

    int swatch = -1;

    if (instrumentId == "builtin.drums" || has ({ "drum", "ドラム", "kick", "snare", "perc" }))   // utf8-std
        swatch = 6;
    else if (instrumentId == "builtin.bass" || has ({ "bass", "ベース" }))   // utf8-std
        swatch = 17;
    else if (instrumentId == "builtin.piano" || instrumentId == "builtin.epiano" || has ({ "piano", "ピアノ", "keys", "rhodes", "organ", "オルガン" }))   // utf8-std
        swatch = 14;
    else if (has ({ "guitar", "gtr", "ギター" }))   // utf8-std
        swatch = 4;
    else if (has ({ "synth", "シンセ", "lead", "melody", "メロディ", "serum", "vital", "massive", "pigments" }))   // utf8-std
        swatch = 0;

    const auto c = swatch >= 0 ? paletteColour (swatch) : trackColour (index);
    return "#" + c.toDisplayString (false).toUpperCase();
}

juce::String trackColourHex (int index)
{
    return "#" + trackColour (index).toDisplayString (false).toUpperCase();
}

juce::Colour parseColour (const std::string& hex, juce::Colour fallback)
{
    auto s = toJuce (hex).trimCharactersAtStart ("#");

    if (s.length() != 6 || ! s.containsOnly ("0123456789abcdefABCDEF"))
        return fallback;

    return juce::Colour ((juce::uint32) (0xff000000u | (juce::uint32) s.getHexValue32()));
}

void drawSectionHeader (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title, const juce::String& right)
{
    g.setColour (panelLight);
    g.fillRect (area);
    g.setColour (background);
    g.drawHorizontalLine (area.getBottom() - 1, (float) area.getX(), (float) area.getRight());

    g.setColour (text);
    g.setFont (juce::FontOptions (15.5f, juce::Font::bold));
    g.drawText (title, area.reduced (10, 0), juce::Justification::centredLeft, true);

    if (right.isNotEmpty())
    {
        g.setFont (juce::FontOptions (14.5f));
        g.drawText (right, area.reduced (10, 0), juce::Justification::centredRight, true);
    }
}

void applyPalette (bool useLight)
{
    light = useLight;

    if (! light)
    {
        // ダーク（Cubase のように落ち着いた灰色）
        background   = juce::Colour (0xff2c2f33);
        panel        = juce::Colour (0xff383b40);
        panelLight   = juce::Colour (0xff464a50);
        lane         = juce::Colour (0xff393c41);
        laneAlt      = juce::Colour (0xff34373c);
        gridBar      = juce::Colour (0xff676d75);
        gridBeat     = juce::Colour (0xff51565d);
        gridSub      = juce::Colour (0xff45494f);
        text         = juce::Colour (0xffeceef0);
        textDim      = juce::Colour (0xffb0b6bd);
        field        = juce::Colour (0xff232528);
        fieldOutline = juce::Colour (0x66ffffff);
        accent       = juce::Colour (0xff4fc3f7);
        selection    = juce::Colour (0xffffd54f);
        playhead     = juce::Colour (0xffff5252);
        loopRange    = juce::Colour (0x3355c1ff);
        warning      = juce::Colour (0xffffb74d);
        tempo        = juce::Colour (0xffba68c8);
        meter        = juce::Colour (0xff4db6ac);
        ok           = juce::Colour (0xff66bb6a);
        danger       = juce::Colour (0xffef5350);
        return;
    }

    // 白を基調にしたニューモーフィズム: 面は同じ明るい灰色で、影と光だけで凹凸を出す。文字は濃い灰色
    background   = juce::Colour (0xffe3e7ed);
    panel        = juce::Colour (0xffe8ecf1);
    panelLight   = juce::Colour (0xffeff2f6);
    lane         = juce::Colour (0xfff1f3f6);
    laneAlt      = juce::Colour (0xffe9ecf1);
    gridBar      = juce::Colour (0xffadb5c0);
    gridBeat     = juce::Colour (0xffcbd1d9);
    gridSub      = juce::Colour (0xffdce0e6);
    text         = juce::Colour (0xff2b313b);
    textDim      = juce::Colour (0xff6a7382);
    field        = juce::Colour (0xffdde2e9);
    fieldOutline = juce::Colour (0x26000000);
    accent       = juce::Colour (0xff2a86d1);
    selection    = juce::Colour (0xffd89a00);
    playhead     = juce::Colour (0xffe53935);
    loopRange    = juce::Colour (0x302a86d1);
    warning      = juce::Colour (0xffd98200);
    tempo        = juce::Colour (0xff9442c4);
    meter        = juce::Colour (0xff00897b);
    ok           = juce::Colour (0xff2e9d4a);
    danger       = juce::Colour (0xffd9363e);
}

void drawRaised (juce::Graphics& g, juce::Rectangle<float> r, float radius, bool pressed, juce::Colour fill, float depth)
{
    if (fill.isTransparent())
        fill = panel;

    if (! light)
    {
        g.setColour (pressed ? fill.darker (0.15f) : fill);
        g.fillRoundedRectangle (r, radius);
        return;
    }

    // DAW では凹んだ見た目は使わない。押している・オンのときは影を浅くして（少し沈んで）色で示す。
    // 影と光はぼかした画像ではなく、角丸の輪を少しずつずらして重ねて描く（部品の端で四角く切れないように、
    // 広がりは raisedInset() の内側に収まる）
    const float d = pressed ? depth * 0.45f : depth;
    const int rings = juce::jmax (1, juce::roundToInt (d * 1.5f));

    for (int k = rings; k >= 1; --k)
    {
        const float grow = (float) k;
        const float shift = (float) k * 0.5f;
        const float fade = 1.0f - (float) (k - 1) / (float) rings;
        const auto ring = r.expanded (grow);

        g.setColour (juce::Colour (0xffa3b1c6).withAlpha (0.16f * fade));
        g.fillRoundedRectangle (ring.translated (shift, shift), radius + grow);
        g.setColour (juce::Colours::white.withAlpha (0.55f * fade));
        g.fillRoundedRectangle (ring.translated (-shift, -shift), radius + grow);
    }

    g.setColour (fill);
    g.fillRoundedRectangle (r, radius);
}

void drawGlass (juce::Graphics& g, juce::Rectangle<float> r, float radius, juce::Colour tint)
{
    // ライト: ガラスの代わりにニューモーフィズムの浮き上がった面
    if (light)
    {
        // 影がまとまりの外で四角く切れないよう、浅めの影にする
        drawRaised (g, r.reduced (1.5f), radius, false, tint.isTransparent() ? panel : panel.interpolatedWith (tint.withAlpha (1.0f), 0.18f), 1.6f);
        return;
    }

    // 本体: 半透明の白（下地が透けて見える）
    g.setColour (juce::Colours::white.withAlpha (0.055f));
    g.fillRoundedRectangle (r, radius);

    if (! tint.isTransparent())
    {
        g.setColour (tint.withAlpha (juce::jmin (0.22f, tint.getFloatAlpha())));
        g.fillRoundedRectangle (r, radius);
    }

    // 上の縁の光（ガラスの厚みの反射）
    {
        juce::Graphics::ScopedSaveState save (g);
        juce::Path clip;
        clip.addRoundedRectangle (r, radius);
        g.reduceClipRegion (clip);
        const float h = juce::jmin (r.getHeight() * 0.5f, 22.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.09f), 0.0f, r.getY(),
                                                 juce::Colours::white.withAlpha (0.0f), 0.0f, r.getY() + h, false));
        g.fillRect (r.withHeight (h));
    }

    // 縁取り: 上が明るく下が暗い細い線
    g.setColour (juce::Colours::white.withAlpha (0.16f));
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.drawHorizontalLine ((int) r.getBottom(), r.getX() + radius, r.getRight() - radius);
}

void drawStatusDot (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
{
    g.setColour (colour);
    g.fillEllipse (area);
}

LookAndFeel::LookAndFeel()
{
    applyColours();
}

void LookAndFeel::applyColours()
{
    setColourScheme ({ panel, background, panelLight, gridBar, text, accent, juce::Colours::black, panelLight, text });

    setColour (juce::ResizableWindow::backgroundColourId, background);
    setColour (juce::TextButton::buttonColourId, panelLight);
    setColour (juce::TextButton::buttonOnColourId, accent.darker (0.3f));
    setColour (juce::ComboBox::backgroundColourId, panelLight);
    setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.3f));
    setColour (juce::Label::textColourId, text);
    setColour (juce::Slider::thumbColourId, accent);
    setColour (juce::Slider::trackColourId, accent.withAlpha (0.5f));
    setColour (juce::Slider::backgroundColourId, background);
    setColour (juce::ScrollBar::thumbColourId, gridBar);
    // ツールチップとスライダーの値の吹き出し: 明るい文字にする（黒い吹き出しに黒い文字にならないように）
    const auto bubble = light ? juce::Colour (0xfffbfcfd) : juce::Colour (0xff2b2f36);
    const auto bubbleOutline = light ? juce::Colours::black.withAlpha (0.15f) : juce::Colours::white.withAlpha (0.25f);
    setColour (juce::TooltipWindow::backgroundColourId, bubble);
    setColour (juce::TooltipWindow::textColourId, text);
    setColour (juce::TooltipWindow::outlineColourId, bubbleOutline);
    setColour (juce::BubbleComponent::backgroundColourId, bubble);
    setColour (juce::BubbleComponent::outlineColourId, bubbleOutline);
    setColour (juce::TextEditor::backgroundColourId, field);
    setColour (juce::TextEditor::outlineColourId, fieldOutline);
    setColour (juce::AlertWindow::backgroundColourId, panel);
    setColour (juce::AlertWindow::outlineColourId, bubbleOutline);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedTextColourId, text);
    setColour (juce::ListBox::backgroundColourId, panel);
    setColour (juce::ToggleButton::textColourId, text);
    setColour (juce::ToggleButton::tickColourId, accent);
    setColour (juce::ToggleButton::tickDisabledColourId, textDim);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, light ? text : juce::Colours::white);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::arrowColourId, textDim);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.3f));
    setColour (juce::CaretComponent::caretColourId, text);
    setColour (juce::AlertWindow::textColourId, text);
    setColour (juce::TextEditor::focusedOutlineColourId, accent);
}

namespace
{
    // ツールチップ（読みやすいように少し大きく、長い説明は折り返す）
    juce::TextLayout tooltipLayout (const juce::String& text, juce::Colour colour)
    {
        juce::AttributedString s;
        s.setJustification (juce::Justification::centredLeft);
        s.append (text, juce::FontOptions (15.0f), colour);

        juce::TextLayout tl;
        tl.createLayoutWithBalancedLineLengths (s, 420.0f);
        return tl;
    }
}

juce::Rectangle<int> LookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea)
{
    const auto tl = tooltipLayout (tipText, juce::Colours::black);
    const int w = (int) (tl.getWidth() + 18.0f), h = (int) (tl.getHeight() + 12.0f);

    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6, w, h)
        .constrainedWithin (parentArea);
}

void LookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    const juce::Rectangle<int> bounds (width, height);
    g.setColour (findColour (juce::TooltipWindow::backgroundColourId));
    g.fillRoundedRectangle (bounds.toFloat(), 6.0f);
    g.setColour (findColour (juce::TooltipWindow::outlineColourId));
    g.drawRoundedRectangle (bounds.toFloat().reduced (0.5f), 6.0f, 1.0f);
    tooltipLayout (text, findColour (juce::TooltipWindow::textColourId)).draw (g, bounds.reduced (9, 6).toFloat());
}

juce::Font LookAndFeel::getPopupMenuFont()
{
    return juce::FontOptions (17.0f);
}

juce::Font LookAndFeel::getMenuBarFont (juce::MenuBarComponent& bar, int, const juce::String&)
{
    return juce::FontOptions (juce::jmin (16.5f, (float) bar.getHeight() * 0.72f));
}

juce::Font LookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    // ボタンの文字は少し大きめ（読みやすさ優先）
    return juce::FontOptions (juce::jmin (16.5f, (float) buttonHeight * 0.68f));
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& backgroundColour,
                                        bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f, 1.5f);
    const float radius = juce::jmin (r.getHeight() * 0.5f, 10.0f);
    const bool plain = backgroundColour == findColour (juce::TextButton::buttonColourId);   // 色を指定していないボタン

    // ライト: ニューモーフィズム（浮き上がったボタン、押している・オンのときは凹む）。
    // 影がボタンの外にはみ出すと四角く切れるので、影の分だけ内側に描く
    if (light)
    {
        const bool on = b.getToggleState() || down;
        r = b.getLocalBounds().toFloat().reduced (raisedInset (1.3f));
        const float rr = juce::jmin (r.getHeight() * 0.5f, 10.0f);
        const auto fill = plain ? (on ? panel.interpolatedWith (accent, 0.18f) : panel) : backgroundColour.interpolatedWith (panel, on ? 0.0f : 0.15f);
        drawRaised (g, r, rr, down, fill, 1.3f);

        if (highlighted && b.isEnabled() && ! on)
        {
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.fillRoundedRectangle (r, rr);
        }

        return;
    }

    // ボタンは不透明（ガラスにすると色が薄く見えて押せるものか分かりにくい）。
    // ガラスはツールバー・トランスポートのまとまり（浮いている操作面）だけに使う
    const auto fill = plain ? panelLight : backgroundColour;
    g.setColour (b.isEnabled() ? fill : fill.withMultipliedAlpha (0.45f));
    g.fillRoundedRectangle (r, radius);

    // 上の縁に細い光、下に影（立体に見えて押せると分かる）
    g.setColour (juce::Colours::white.withAlpha (b.isEnabled() ? 0.14f : 0.05f));
    g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + radius * 0.6f, r.getRight() - radius * 0.6f);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawRoundedRectangle (r, radius, 1.0f);

    if (highlighted && b.isEnabled())
    {
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRoundedRectangle (r, radius);
    }

    if (down)
    {
        g.setColour (juce::Colours::black.withAlpha (0.18f));
        g.fillRoundedRectangle (r, radius);
    }
}

void LookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (1.0f, 1.5f);
    const float radius = juce::jmin (r.getHeight() * 0.5f, 10.0f);

    if (light)
    {
        drawRaised (g, r.reduced (raisedInset (1.3f) - 1.0f), juce::jmin (r.getHeight() * 0.5f, 10.0f), false, panel, 1.3f);
    }
    else
    {
        g.setColour (panelLight);
        g.fillRoundedRectangle (r, radius);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawRoundedRectangle (r, radius, 1.0f);
    }

    if (box.hasKeyboardFocus (true))
    {
        g.setColour (accent.withAlpha (0.6f));
        g.drawRoundedRectangle (r, radius, 1.2f);
    }

    juce::Path arrow;
    const float cx = (float) width - 15.0f, cy = (float) height * 0.5f;
    arrow.startNewSubPath (cx - 4.0f, cy - 2.0f);
    arrow.lineTo (cx, cy + 2.5f);
    arrow.lineTo (cx + 4.0f, cy - 2.0f);
    g.setColour (text.withAlpha (0.8f));
    g.strokePath (arrow, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void LookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    g.setColour (editor.findColour (juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle (juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height), 7.0f);
}

void LookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    if (! editor.isEnabled())
        return;

    const bool focused = editor.hasKeyboardFocus (true) && ! editor.isReadOnly();
    g.setColour (editor.findColour (focused ? juce::TextEditor::focusedOutlineColourId : juce::TextEditor::outlineColourId));
    g.drawRoundedRectangle (juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f), 7.0f, focused ? 1.5f : 1.0f);
}

void LookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float minSliderPos,
                                    float maxSliderPos, juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearHorizontal && style != juce::Slider::LinearVertical)
        return LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);

    const bool horizontal = style == juce::Slider::LinearHorizontal;
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);

    if (! horizontal && slider.getProperties().getWithDefault ("fader", false))
    {
        // ミキサーのフェーダー: 細い溝と、つまんで動かす四角いキャップ（中央に白い目印の線）
        auto slot = bounds.withSizeKeepingCentre (5.0f, bounds.getHeight() - 4.0f);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (slot, 2.5f);
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.drawRoundedRectangle (slot, 2.5f, 1.0f);

        const float capW = juce::jmin (34.0f, bounds.getWidth() - 4.0f), capH = 22.0f;
        auto cap = juce::Rectangle<float> (capW, capH).withCentre ({ bounds.getCentreX(), sliderPos });
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (cap.translated (0.0f, 2.0f), 3.0f);
        g.setColour (slider.isMouseButtonDown() ? juce::Colour (0xffe6e9ed) : juce::Colour (0xffc9cdd3));
        g.fillRoundedRectangle (cap, 3.0f);
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.drawHorizontalLine ((int) cap.getY() + 1, cap.getX() + 3.0f, cap.getRight() - 3.0f);

        // 指のかかる溝（上下）と、値を示す中央の線
        g.setColour (juce::Colour (0xff8a9098));

        for (float dy : { -7.0f, -4.0f, 4.0f, 7.0f })
            g.fillRect (juce::Rectangle<float> (cap.getX() + 4.0f, sliderPos + dy - 0.5f, cap.getWidth() - 8.0f, 1.0f));

        g.setColour (juce::Colour (0xff1c1e21));
        g.fillRect (juce::Rectangle<float> (cap.getX() + 1.0f, sliderPos - 1.0f, cap.getWidth() - 2.0f, 2.0f));
        return;
    }

    const float thickness = 4.0f;

    // 溝（ガラスの細い溝）と、値までの色
    auto track = horizontal ? bounds.withSizeKeepingCentre (bounds.getWidth(), thickness)
                            : bounds.withSizeKeepingCentre (thickness, bounds.getHeight());
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (track, thickness * 0.5f);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawRoundedRectangle (track, thickness * 0.5f, 1.0f);

    auto filled = horizontal ? track.withRight (sliderPos) : track.withTop (sliderPos);
    g.setColour (slider.findColour (juce::Slider::trackColourId));
    g.fillRoundedRectangle (filled, thickness * 0.5f);

    // つまみ: 小さなガラスの玉
    const float size = juce::jmin (15.0f, (horizontal ? bounds.getHeight() : bounds.getWidth()) - 2.0f);
    auto thumb = juce::Rectangle<float> (size, size).withCentre (horizontal ? juce::Point<float> (sliderPos, bounds.getCentreY())
                                                                            : juce::Point<float> (bounds.getCentreX(), sliderPos));
    g.setColour (juce::Colours::black.withAlpha (0.3f));
    g.fillEllipse (thumb.translated (0.0f, 1.0f));
    g.setColour (juce::Colour (0xffdfe3e8));
    g.fillEllipse (thumb);
    g.setColour (juce::Colours::white);
    g.fillEllipse (thumb.reduced (size * 0.28f).translated (0.0f, -size * 0.12f).withHeight (size * 0.3f));
}

void LookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height, bool isScrollbarVertical,
                                 int thumbStartPosition, int thumbSize, bool isMouseOver, bool isMouseDown)
{
    auto thumb = isScrollbarVertical ? juce::Rectangle<int> (x, thumbStartPosition, width, thumbSize)
                                     : juce::Rectangle<int> (thumbStartPosition, y, thumbSize, height);
    auto r = thumb.toFloat().reduced (2.0f);
    g.setColour (juce::Colours::white.withAlpha (isMouseDown ? 0.35f : isMouseOver ? 0.28f : 0.18f));
    g.fillRoundedRectangle (r, juce::jmin (r.getWidth(), r.getHeight()) * 0.5f);
}

void LookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (findColour (juce::PopupMenu::backgroundColourId));
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawRect (0, 0, width, height);
}

//==============================================================================
juce::Path iconPath (const juce::String& name, juce::Rectangle<float> area)
{
    // 24 x 24 の箱で作って、area に合わせる
    juce::Path p;
    auto stroke = [&] (const juce::Path& line, float width)
    {
        juce::Path out;
        juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath (out, line);
        p.addPath (out);
    };

    if (name == "snap")
    {
        // Cubase のスナップ: 真ん中の線に左右から矢印が吸い付く（→|←）
        juce::Path line;
        line.startNewSubPath (12.0f, 3.0f);
        line.lineTo (12.0f, 21.0f);
        stroke (line, 2.0f);

        juce::Path arrows;
        arrows.startNewSubPath (2.5f, 12.0f);
        arrows.lineTo (9.0f, 12.0f);
        arrows.startNewSubPath (5.5f, 8.0f);
        arrows.lineTo (9.5f, 12.0f);
        arrows.lineTo (5.5f, 16.0f);
        arrows.startNewSubPath (21.5f, 12.0f);
        arrows.lineTo (15.0f, 12.0f);
        arrows.startNewSubPath (18.5f, 8.0f);
        arrows.lineTo (14.5f, 12.0f);
        arrows.lineTo (18.5f, 16.0f);
        stroke (arrows, 2.2f);
    }
    else if (name == "piano")
    {
        // ピアノの鍵盤（白鍵の枠と黒鍵 3 つ）
        juce::Path frame;
        frame.addRoundedRectangle (3.0f, 5.0f, 18.0f, 14.0f, 1.5f);

        for (float x : { 9.0f, 15.0f })
        {
            frame.startNewSubPath (x, 12.0f);
            frame.lineTo (x, 19.0f);
        }

        stroke (frame, 1.6f);

        for (float x : { 7.5f, 13.5f, 17.0f })
            p.addRectangle (x - 1.2f, 5.0f, 2.8f, 7.5f);
    }
    else if (name == "mixer")
    {
        // ミキサー: フェーダー 3 本とつまみ
        juce::Path lines;

        for (float x : { 6.0f, 12.0f, 18.0f })
        {
            lines.startNewSubPath (x, 3.0f);
            lines.lineTo (x, 21.0f);
        }

        stroke (lines, 1.6f);
        p.addRoundedRectangle (3.5f, 12.0f, 5.0f, 4.0f, 1.0f);
        p.addRoundedRectangle (9.5f, 6.0f, 5.0f, 4.0f, 1.0f);
        p.addRoundedRectangle (15.5f, 14.0f, 5.0f, 4.0f, 1.0f);
    }
    else if (name == "flagL" || name == "flagR")
    {
        // ロケーターの旗（L = 開始は右向き、R = 終了は左向き）。Cubase の左右ロケーターと同じ考え方
        const bool left = name == "flagL";
        const float pole = left ? 6.0f : 18.0f;
        juce::Path stick;
        stick.startNewSubPath (pole, 3.0f);
        stick.lineTo (pole, 21.0f);
        stroke (stick, 2.0f);
        juce::Path flag;
        flag.startNewSubPath (pole, 3.5f);
        flag.lineTo (left ? 19.0f : 5.0f, 7.5f);
        flag.lineTo (pole, 11.5f);
        flag.closeSubPath();
        p.addPath (flag);
    }
    else if (name == "follow")
    {
        // 再生位置の線と、右へ送る矢印
        juce::Path l;
        l.startNewSubPath (7.0f, 3.0f);
        l.lineTo (7.0f, 21.0f);
        stroke (l, 2.4f);
        juce::Path a;
        a.startNewSubPath (11.0f, 12.0f);
        a.lineTo (20.0f, 12.0f);
        a.startNewSubPath (16.0f, 8.0f);
        a.lineTo (20.0f, 12.0f);
        a.lineTo (16.0f, 16.0f);
        stroke (a, 2.4f);
    }
    else if (name == "loop")
    {
        // 回る 2 本の矢印
        juce::Path top;
        top.startNewSubPath (5.0f, 13.0f);
        top.lineTo (5.0f, 10.0f);
        top.quadraticTo (5.0f, 7.0f, 8.0f, 7.0f);
        top.lineTo (19.0f, 7.0f);
        top.startNewSubPath (16.0f, 4.0f);
        top.lineTo (19.0f, 7.0f);
        top.lineTo (16.0f, 10.0f);
        stroke (top, 2.2f);
        juce::Path bottom;
        bottom.startNewSubPath (19.0f, 11.0f);
        bottom.lineTo (19.0f, 14.0f);
        bottom.quadraticTo (19.0f, 17.0f, 16.0f, 17.0f);
        bottom.lineTo (5.0f, 17.0f);
        bottom.startNewSubPath (8.0f, 14.0f);
        bottom.lineTo (5.0f, 17.0f);
        bottom.lineTo (8.0f, 20.0f);
        stroke (bottom, 2.2f);
    }
    else if (name == "metronome")
    {
        p.startNewSubPath (9.5f, 3.0f);
        p.lineTo (14.5f, 3.0f);
        p.lineTo (19.0f, 21.0f);
        p.lineTo (5.0f, 21.0f);
        p.closeSubPath();
        juce::Path hole;
        hole.addRectangle (7.0f, 16.5f, 10.0f, 1.8f);
        p.addPath (hole);
        p.setUsingNonZeroWinding (false);
        juce::Path arm;
        arm.startNewSubPath (12.0f, 15.0f);
        arm.lineTo (19.5f, 6.0f);
        stroke (arm, 1.8f);
    }
    else if (name == "stop")
    {
        p.addRoundedRectangle (6.0f, 6.0f, 12.0f, 12.0f, 1.5f);
    }
    else if (name == "play")
    {
        p.startNewSubPath (7.0f, 4.5f);
        p.lineTo (19.5f, 12.0f);
        p.lineTo (7.0f, 19.5f);
        p.closeSubPath();
    }
    else if (name == "pause")
    {
        p.addRoundedRectangle (6.5f, 5.0f, 4.0f, 14.0f, 1.0f);
        p.addRoundedRectangle (13.5f, 5.0f, 4.0f, 14.0f, 1.0f);
    }
    else if (name == "record")
    {
        p.addEllipse (5.5f, 5.5f, 13.0f, 13.0f);
    }

    p.applyTransform (juce::AffineTransform::scale (area.getWidth() / 24.0f, area.getHeight() / 24.0f)
                        .translated (area.getX(), area.getY()));
    return p;
}

void IconButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const bool on = getToggleState();

    // ライト: オンは凹んだ面に青いアイコン（色の塊にしない）
    if (light)
    {
        const auto r = getLocalBounds().toFloat().reduced (raisedInset (1.3f));
        drawRaised (g, r, juce::jmin (r.getHeight() * 0.5f, 10.0f), down, on ? panel.interpolatedWith (accent, 0.15f) : panel, 1.3f);
    }
    else
        getLookAndFeel().drawButtonBackground (g, *this, findColour (on ? buttonOnColourId : buttonColourId), highlighted, down);

    const float size = juce::jmin ((float) getHeight() - 12.0f, 22.0f);
    auto area = getLocalBounds().toFloat().withSizeKeepingCentre (size, size);
    const auto colour = light ? (on ? Theme::accent : Theme::text) : findColour (on ? textColourOnId : textColourOffId);
    g.setColour (colour.withMultipliedAlpha (isEnabled() ? 1.0f : 0.4f));
    g.fillPath (iconPath (icon, area));
}

}

namespace Theme
{

juce::Path selectToolIcon (juce::Rectangle<float> area)
{
    // 左上を先端にした矢印
    juce::Path p;
    p.startNewSubPath (0.0f, 0.0f);
    p.lineTo (0.0f, 14.0f);
    p.lineTo (3.6f, 10.6f);
    p.lineTo (6.2f, 16.0f);
    p.lineTo (8.6f, 15.0f);
    p.lineTo (6.0f, 9.6f);
    p.lineTo (11.0f, 9.6f);
    p.closeSubPath();
    p.applyTransform (p.getTransformToScaleToFit (area, true));
    return p;
}

juce::Path pencilToolIcon (juce::Rectangle<float> area)
{
    // 左下を先端にした鉛筆（45 度）
    juce::Path p;
    p.startNewSubPath (0.0f, 16.0f);      // 先端
    p.lineTo (1.2f, 11.4f);
    p.lineTo (11.6f, 1.0f);
    p.lineTo (15.0f, 4.4f);
    p.lineTo (4.6f, 14.8f);
    p.closeSubPath();
    p.startNewSubPath (10.0f, 2.6f);      // 消しゴムとの境目
    p.lineTo (13.4f, 6.0f);
    p.applyTransform (p.getTransformToScaleToFit (area, true));
    return p;
}

juce::Path splitToolIcon (juce::Rectangle<float> area)
{
    // はさみ（2 つの輪と交差する刃）
    juce::Path p;
    p.addEllipse (0.5f, 10.5f, 5.0f, 5.0f);
    p.addEllipse (10.5f, 10.5f, 5.0f, 5.0f);
    p.startNewSubPath (4.5f, 11.0f);
    p.lineTo (12.0f, 0.5f);
    p.startNewSubPath (11.5f, 11.0f);
    p.lineTo (4.0f, 0.5f);
    p.applyTransform (p.getTransformToScaleToFit (area, true));
    return p;
}

static juce::MouseCursor makeToolCursor (juce::Path (*icon) (juce::Rectangle<float>), int hotX, int hotY)
{
    constexpr int size = 24;
    juce::Image image (juce::Image::ARGB, size, size, true);
    juce::Graphics g (image);
    auto path = icon ({ 2.0f, 2.0f, (float) size - 4.0f, (float) size - 4.0f });
    g.setColour (juce::Colours::black);
    g.strokePath (path, juce::PathStrokeType (3.0f));
    g.setColour (juce::Colours::white);
    g.strokePath (path, juce::PathStrokeType (1.4f));
    return juce::MouseCursor (image, hotX, hotY);
}

const juce::MouseCursor& splitCursor()
{
    static const juce::MouseCursor cursor = makeToolCursor (splitToolIcon, 12, 2);
    return cursor;
}

static juce::MouseCursor makeOutlinedCursor (const juce::Path& path, int hotX, int hotY)
{
    constexpr int size = 24;
    juce::Image image (juce::Image::ARGB, size, size, true);
    juce::Graphics g (image);
    g.setColour (juce::Colours::black);
    g.strokePath (path, juce::PathStrokeType (3.2f, juce::PathStrokeType::mitered, juce::PathStrokeType::square));
    g.setColour (juce::Colours::white);
    g.strokePath (path, juce::PathStrokeType (1.4f, juce::PathStrokeType::mitered, juce::PathStrokeType::square));
    return juce::MouseCursor (image, hotX, hotY);
}

/** 左右の矢印（y の高さ、x1〜x2）。 */
static void addLeftRightArrow (juce::Path& p, float x1, float x2, float y)
{
    p.startNewSubPath (x1, y);
    p.lineTo (x2, y);
    p.startNewSubPath (x1 + 3.5f, y - 3.5f);
    p.lineTo (x1, y);
    p.lineTo (x1 + 3.5f, y + 3.5f);
    p.startNewSubPath (x2 - 3.5f, y - 3.5f);
    p.lineTo (x2, y);
    p.lineTo (x2 - 3.5f, y + 3.5f);
}

const juce::MouseCursor& fadeCursor (bool fadeIn)
{
    auto make = [] (bool in)
    {
        juce::Path p;
        // フェードの線（フェードインは左下から右上、アウトは左上から右下）
        p.startNewSubPath (3.0f, in ? 13.0f : 3.0f);
        p.lineTo (21.0f, in ? 3.0f : 13.0f);
        addLeftRightArrow (p, 4.0f, 20.0f, 19.0f);
        return makeOutlinedCursor (p, 12, 12);
    };

    static const juce::MouseCursor in = make (true), out = make (false);
    return fadeIn ? in : out;
}

const juce::MouseCursor& jointCursor()
{
    static const juce::MouseCursor cursor = []
    {
        juce::Path p;
        p.startNewSubPath (12.0f, 2.0f);
        p.lineTo (12.0f, 22.0f);
        // 左に向いた矢印と右に向いた矢印
        p.startNewSubPath (9.0f, 12.0f);
        p.lineTo (2.0f, 12.0f);
        p.startNewSubPath (5.5f, 8.5f);
        p.lineTo (2.0f, 12.0f);
        p.lineTo (5.5f, 15.5f);
        p.startNewSubPath (15.0f, 12.0f);
        p.lineTo (22.0f, 12.0f);
        p.startNewSubPath (18.5f, 8.5f);
        p.lineTo (22.0f, 12.0f);
        p.lineTo (18.5f, 15.5f);
        return makeOutlinedCursor (p, 12, 12);
    }();

    return cursor;
}

const juce::MouseCursor& toolCursor (EditTool tool)
{
    static const juce::MouseCursor normal;

    switch (tool)
    {
        case EditTool::pencil: return pencilCursor();
        case EditTool::split:  return splitCursor();
        case EditTool::select: break;
    }

    return normal;
}

const juce::MouseCursor& pencilCursor()
{
    static const juce::MouseCursor cursor = []
    {
        constexpr int size = 24;
        juce::Image image (juce::Image::ARGB, size, size, true);
        juce::Graphics g (image);
        auto icon = pencilToolIcon ({ 1.0f, 1.0f, (float) size - 4.0f, (float) size - 4.0f });
        g.setColour (juce::Colours::black);
        g.strokePath (icon, juce::PathStrokeType (2.5f));
        g.setColour (juce::Colours::white);
        g.fillPath (icon);
        g.setColour (juce::Colours::black);
        g.strokePath (icon, juce::PathStrokeType (1.0f));
        return juce::MouseCursor (image, 1, size - 3);
    }();

    return cursor;
}

}
