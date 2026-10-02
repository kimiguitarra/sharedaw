#include "CompressorPanel.h"

#include "ValueText.h"
#include "Theme.h"

namespace
{
    // FET（1176 風）: 黒いパネルに銀の文字。Optical（LA-2A 風）: 明るい灰色のパネルに黒い文字
    const juce::Colour fetPanelTop    { 0xff2c2d30 }, fetPanelBottom  { 0xff161719 }, fetText  { 0xffd9dadc };
    const juce::Colour optoPanelTop   { 0xffd9d5cb }, optoPanelBottom { 0xffb9b4a8 }, optoText { 0xff1d1d1f };
    const juce::Colour meterFace      { 0xfff2e3b5 };

    juce::String formatDb (double db)     { return ValueText::formatDbUnit (db); }
    using ValueText::formatMs;

    /** VU メーターの目盛りの位置（0〜1）。-20 VU が左端、+3 VU が右端。 */
    float vuPosition (float vu)
    {
        const float a = std::pow (10.0f, vu / 20.0f);
        const float lo = 0.1f, hi = std::pow (10.0f, 3.0f / 20.0f);
        return juce::jlimit (0.0f, 1.0f, (a - lo) / (hi - lo));
    }
}

//==============================================================================
/** つまみの描き方（FET は黒い小さめのつまみに銀の縁、Optical は大きな黒いつまみ）。 */
class CompressorPanel::KnobLook  : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        const bool light = (bool) s.getProperties()["light"];
        auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
        const auto centre = bounds.getCentre();

        // 目盛り
        g.setColour (light ? optoText.withAlpha (0.7f) : fetText.withAlpha (0.6f));

        for (int i = 0; i <= 10; ++i)
        {
            const float a = startAngle + (endAngle - startAngle) * (float) i / 10.0f;
            const auto p1 = centre.getPointOnCircumference (radius, a);
            const auto p2 = centre.getPointOnCircumference (radius - (i % 5 == 0 ? 6.0f : 3.5f), a);
            g.drawLine ({ p1, p2 }, i % 5 == 0 ? 1.6f : 1.0f);
        }

        const float knobRadius = radius - 8.0f;
        auto knob = juce::Rectangle<float> (knobRadius * 2.0f, knobRadius * 2.0f).withCentre (centre);

        // 影と縁（FET は銀のスカート）
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillEllipse (knob.translated (0.0f, 2.5f));

        if (! light)
        {
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe6e7e9), knob.getX(), knob.getY(),
                                                     juce::Colour (0xff6d6f73), knob.getRight(), knob.getBottom(), false));
            g.fillEllipse (knob);
            knob = knob.reduced (knobRadius * 0.22f);
        }

        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3b3e), knob.getX(), knob.getY(),
                                                 juce::Colour (0xff0c0c0d), knob.getRight(), knob.getBottom(), false));
        g.fillEllipse (knob);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawEllipse (knob.reduced (1.0f), 1.0f);

        // 指針
        const float angle = startAngle + pos * (endAngle - startAngle);
        const auto tip = centre.getPointOnCircumference (knob.getWidth() * 0.5f - 3.0f, angle);
        const auto base = centre.getPointOnCircumference (knob.getWidth() * 0.12f, angle);
        g.setColour (juce::Colours::white);
        g.drawLine ({ base, tip }, 2.5f);
    }
};

//==============================================================================
void CompressorPanel::VuMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();

    // 枠と文字盤（裏から照らされた黄色っぽい面）
    g.setColour (light ? juce::Colour (0xff2b2b2d) : juce::Colour (0xff0e0e0f));
    g.fillRoundedRectangle (r, 6.0f);
    auto face = r.reduced (6.0f);
    g.setGradientFill (juce::ColourGradient (meterFace.brighter (0.15f), face.getCentreX(), face.getY(),
                                             meterFace.darker (0.25f), face.getCentreX(), face.getBottom(), false));
    g.fillRoundedRectangle (face, 3.0f);

    const float halfSpan = 0.72f;   // 左右に振れる角度（ラジアン）
    const float radius = juce::jmin (face.getHeight() * 0.95f, (face.getWidth() * 0.5f - 24.0f) / std::sin (halfSpan));
    const auto pivot = juce::Point<float> (face.getCentreX(), face.getY() + 34.0f + radius);
    auto angleFor = [&] (float vu) { return -halfSpan + 2.0f * halfSpan * vuPosition (vu); };

    // 目盛りの弧（0 より上は赤）
    g.setColour (juce::Colours::black.withAlpha (0.85f));
    juce::Path arc;
    arc.addCentredArc (pivot.x, pivot.y, radius, radius, 0.0f, angleFor (-20.0f), angleFor (0.0f), true);
    g.strokePath (arc, juce::PathStrokeType (1.3f));
    juce::Path red;
    red.addCentredArc (pivot.x, pivot.y, radius, radius, 0.0f, angleFor (0.0f), angleFor (3.0f), true);
    g.setColour (juce::Colour (0xffc62828));
    g.strokePath (red, juce::PathStrokeType (3.0f));

    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));

    for (float vu : { -20.0f, -10.0f, -7.0f, -5.0f, -3.0f, -2.0f, -1.0f, 0.0f, 1.0f, 2.0f, 3.0f })
    {
        const float a = angleFor (vu);
        const auto p1 = pivot.getPointOnCircumference (radius, a);
        const auto p2 = pivot.getPointOnCircumference (radius + 6.0f, a);
        g.setColour (vu > 0.0f ? juce::Colour (0xffc62828) : juce::Colours::black);
        g.drawLine ({ p1, p2 }, 1.2f);

        const auto label = pivot.getPointOnCircumference (radius + 15.0f, a);
        const auto text = vu > 0.0f ? "+" + juce::String ((int) vu) : juce::String ((int) -vu);
        g.drawText (text, juce::Rectangle<float> (30.0f, 12.0f).withCentre (label), juce::Justification::centred);
    }

    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.setFont (juce::FontOptions (14.5f, juce::Font::bold));
    const float textY = face.getY() + 34.0f + radius * 0.35f;
    g.drawText ("VU", juce::Rectangle<float> (face.getX(), textY, face.getWidth(), 16.0f), juce::Justification::centred);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("GAIN REDUCTION (dB)", juce::Rectangle<float> (face.getX(), textY + 16.0f, face.getWidth(), 12.0f), juce::Justification::centred);

    // 針（リダクションがないときは 0 の位置、かかるほど左へ振れる）
    g.saveState();
    g.reduceClipRegion (face.toNearestInt());
    const float a = angleFor (-grDb);
    g.setColour (juce::Colour (0xff1a1a1a));
    g.drawLine ({ pivot, pivot.getPointOnCircumference (radius + 4.0f, a) }, 1.6f);
    g.restoreState();

    // ガラスの反射
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.18f), face.getX(), face.getY(),
                                             juce::Colours::transparentWhite, face.getX(), face.getCentreY(), false));
    g.fillRoundedRectangle (face.withHeight (face.getHeight() * 0.5f), 3.0f);
}

//==============================================================================
CompressorPanel::Knob::Knob (juce::String n, bool b) : name (std::move (n)), big (b)
{
    slider.setPopupDisplayEnabled (true, true, nullptr);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
    addAndMakeVisible (slider);
}

void CompressorPanel::Knob::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (16);
    r.removeFromBottom (16);
    slider.setBounds (r);
}

void CompressorPanel::Knob::paint (juce::Graphics& g)
{
    g.setColour (light ? optoText : fetText);
    g.setFont (juce::FontOptions (big ? 12.0f : 11.0f, juce::Font::bold));
    g.drawText (name, getLocalBounds().removeFromTop (16), juce::Justification::centred);
    g.setFont (juce::FontOptions (14.0f));
    g.setColour ((light ? optoText : fetText).withAlpha (0.75f));
    g.drawText (format ? format (slider.getValue()) : juce::String(), getLocalBounds().removeFromBottom (16), juce::Justification::centred);
}

//==============================================================================
CompressorPanel::CompressorPanel() : look (std::make_unique<KnobLook>())
{
    addAndMakeVisible (meter);

    // 1176 風: INPUT を上げるほど強くかかる（スレッショルドを下げる）
    bind (input, 0.0, 50.0, 18.0, 18.0, "Compressor INPUT"_ju,
          [] (auto& c) { return -c.thresholdDb; }, [] (auto& c, double v) { c.thresholdDb = -v; });
    bind (output, 0.0, 24.0, 6.0, 0.0, "Compressor OUTPUT"_ju,
          [] (auto& c) { return c.makeupDb; }, [] (auto& c, double v) { c.makeupDb = v; });
    bind (attack, 0.05, 50.0, 3.0, 1.0, "Compressor ATTACK"_ju,
          [] (auto& c) { return c.attackMs; }, [] (auto& c, double v) { c.attackMs = v; });
    bind (release, 20.0, 1500.0, 200.0, 150.0, "Compressor RELEASE"_ju,
          [] (auto& c) { return c.releaseMs; }, [] (auto& c, double v) { c.releaseMs = v; });

    // LA-2A 風: GAIN（出力）と PEAK REDUCTION（かかり具合）
    bind (gain, 0.0, 24.0, 6.0, 0.0, "Compressor GAIN"_ju,
          [] (auto& c) { return c.makeupDb; }, [] (auto& c, double v) { c.makeupDb = v; });
    bind (peakReduction, 0.0, 50.0, 18.0, 18.0, "Compressor PEAK REDUCTION"_ju,
          [] (auto& c) { return -c.thresholdDb; }, [] (auto& c, double v) { c.thresholdDb = -v; });

    // 低域のスルー: 検出だけ低域を削る（ベースやキックで必要以上にかからないように）
    bind (lowThru, 0.0, 500.0, 100.0, 0.0, "Compressor LOW THRU"_ju,
          [] (auto& c) { return c.sidechainHpHz; }, [] (auto& c, double v) { c.sidechainHpHz = v < 20.0 ? 0.0 : std::round (v); });
    lowThru.format = [] (double v) { return v < 20.0 ? juce::String ("OFF") : juce::String (juce::roundToInt (v)) + " Hz"; };
    lowThru.slider.setTooltip ("LOW THRU"_ju);

    input.format = peakReduction.format = [] (double v) { return "Thr "_ju + juce::String (-v, 1) + " dB"; };
    output.format = gain.format = formatDb;
    attack.format = release.format = formatMs;
    input.slider.setTooltip ({});
    peakReduction.slider.setTooltip ({});
    output.slider.setTooltip ({});
    gain.slider.setTooltip ({});

    for (int i = 0; i < 4; ++i)
    {
        auto& b = ratioButtons[i];
        b.setButtonText (juce::String ((int) ratios[i]));
        b.setTooltip ("レシオ "_ju + juce::String ((int) ratios[i]) + ":1");
        b.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff3a3b3f));
        b.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe8e3d3));
        b.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        b.onClick = [this, i] { setRatio (ratios[i], "Compressor レシオ"_ju); };
        addChildComponent (b);
    }

    for (auto* b : { &compressButton, &limitButton })
    {
        b->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff8f8a80));
        b->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2b2b2d));
        b->setColour (juce::TextButton::textColourOffId, optoText);
        b->setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        addChildComponent (b);
    }

    compressButton.setTooltip ("COMPRESS"_ju);
    limitButton.setTooltip ("LIMIT"_ju);
    compressButton.onClick = [this] { setRatio (3.0, "Compressor COMPRESS"_ju); };
    limitButton.onClick = [this] { setRatio (10.0, "Compressor LIMIT"_ju); };

    updateVisibility();
    startTimerHz (30);
}

CompressorPanel::~CompressorPanel()
{
    for (auto* k : { &input, &output, &attack, &release, &gain, &peakReduction, &lowThru })
        k->slider.setLookAndFeel (nullptr);
}

void CompressorPanel::bind (Knob& knob, double min, double max, double skewMid, double reset, const juce::String& description,
                            std::function<double (const collab::ChannelComp&)> get, std::function<void (collab::ChannelComp&, double)> set)
{
    auto& s = knob.slider;
    s.setLookAndFeel (look.get());
    s.setRange (min, max, 0.0);

    if (skewMid > min && skewMid < max)
        s.setSkewFactorFromMidPoint (skewMid);

    s.setDoubleClickReturnValue (true, reset);
    s.onDragStart = [this] { dragging = true; };
    s.onDragEnd = [this] { if (onEditEnd) onEditEnd(); dragging = false; };
    s.onValueChange = [this, &knob, description, set]
    {
        const double v = knob.slider.getValue();
        knob.repaint();

        if (onEdit)
            onEdit (description, [set, v] (collab::ChannelComp& c) { set (c, v); }, dragging);

        if (! dragging && onEditEnd)
            onEditEnd();
    };

    getters.emplace_back (&knob, std::move (get));
    addChildComponent (knob);
}

void CompressorPanel::setRatio (double ratio, const juce::String& description)
{
    if (onEdit)
        onEdit (description, [ratio] (collab::ChannelComp& c) { c.ratio = ratio; }, false);

    if (onEditEnd)
        onEditEnd();
}

void CompressorPanel::setComp (const collab::ChannelComp& c)
{
    comp = c;
    const bool nowOpto = c.type == collab::CompType::opto;

    if (nowOpto != opto)
    {
        opto = nowOpto;
        updateVisibility();
        resized();
    }

    for (auto& [knob, get] : getters)
    {
        knob->slider.setValue (get (c), juce::dontSendNotification);
        knob->repaint();
    }

    for (int i = 0; i < 4; ++i)
        ratioButtons[i].setToggleState (std::abs (c.ratio - ratios[i]) < 0.01, juce::dontSendNotification);

    compressButton.setToggleState (c.ratio < 6.0, juce::dontSendNotification);
    limitButton.setToggleState (c.ratio >= 6.0, juce::dontSendNotification);

    setAlpha (c.enabled ? 1.0f : 0.6f);
    repaint();
}

void CompressorPanel::updateVisibility()
{
    for (auto* k : { &input, &output, &attack, &release })
        k->setVisible (! opto);

    for (auto& b : ratioButtons)
        b.setVisible (! opto);

    for (auto* k : { &gain, &peakReduction })
    {
        k->setVisible (opto);
        k->light = true;
        k->slider.getProperties().set ("light", true);
    }

    compressButton.setVisible (opto);
    limitButton.setVisible (opto);
    meter.light = opto;
    lowThru.setVisible (true);
    lowThru.light = opto;
    lowThru.slider.getProperties().set ("light", opto);
    lowThru.repaint();
}

void CompressorPanel::timerCallback()
{
    // VU の針の動き（約 300 ms で追いつく）
    const float next = meter.grDb + (targetGr - meter.grDb) * 0.3f;

    if (std::abs (next - meter.grDb) > 0.01f)
    {
        meter.grDb = next;
        meter.repaint();
    }
}

//==============================================================================
void CompressorPanel::paint (juce::Graphics& g)
{
    const auto r = faceplate.toFloat();
    g.setGradientFill (juce::ColourGradient (opto ? optoPanelTop : fetPanelTop, 0.0f, r.getY(),
                                             opto ? optoPanelBottom : fetPanelBottom, 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, 6.0f);

    // ネジ
    for (auto p : { r.getTopLeft().translated (10.0f, 10.0f), r.getTopRight().translated (-10.0f, 10.0f),
                    r.getBottomLeft().translated (10.0f, -10.0f), r.getBottomRight().translated (-10.0f, -10.0f) })
    {
        g.setColour (juce::Colour (0xff9a9ca0));
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (p));
        g.setColour (juce::Colour (0xff4a4b4e));
        g.drawLine (p.x - 2.5f, p.y, p.x + 2.5f, p.y, 1.0f);
    }

    g.setColour (opto ? optoText : fetText);
    g.setFont (juce::FontOptions (14.5f, juce::Font::bold));
    auto title = r.reduced (24.0f, 8.0f).withHeight (18.0f);
    g.drawText (opto ? "OPTICAL LEVELING AMPLIFIER" : "FET LIMITING AMPLIFIER", title, juce::Justification::centredLeft);
    g.setFont (juce::FontOptions (13.5f));
    g.setColour ((opto ? optoText : fetText).withAlpha (0.6f));
    g.drawText (opto ? "LA-2A style" : "1176 style", title, juce::Justification::centredRight);

    if (! opto && ! ratioButtons[0].getBounds().isEmpty())
    {
        g.setColour (fetText);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText ("RATIO", ratioButtons[3].getBounds().withY (ratioButtons[3].getY() - 18).withHeight (16), juce::Justification::centred);
    }
}

void CompressorPanel::resized()
{
    faceplate = getLocalBounds();
    auto area = faceplate.reduced (20, 8);
    area.removeFromTop (22);

    if (! opto)
    {
        // INPUT OUTPUT | ATTACK RELEASE | RATIO | VU
        meter.setBounds (area.removeFromRight (juce::jmin (260, area.getWidth() / 3)).reduced (0, 4));
        area.removeFromRight (16);

        auto ratioColumn = area.removeFromRight (56);
        ratioColumn.removeFromTop (18);
        const int h = juce::jmin (26, ratioColumn.getHeight() / 4);

        for (int i = 3; i >= 0; --i)
            ratioButtons[i].setBounds (ratioColumn.removeFromTop (h).reduced (4, 2));

        area.removeFromRight (10);
        const int w = area.getWidth() / 5;

        for (auto* k : { &input, &output, &attack, &release, &lowThru })
            k->setBounds (area.removeFromLeft (w));
    }
    else
    {
        // COMPRESS/LIMIT | GAIN | VU | PEAK REDUCTION
        auto switchColumn = area.removeFromLeft (96).withSizeKeepingCentre (96, 60);
        compressButton.setBounds (switchColumn.removeFromTop (28).reduced (4, 1));
        switchColumn.removeFromTop (4);
        limitButton.setBounds (switchColumn.removeFromTop (28).reduced (4, 1));

        const int knobWidth = juce::jmin (150, area.getWidth() / 5);
        gain.setBounds (area.removeFromLeft (knobWidth));
        peakReduction.setBounds (area.removeFromRight (knobWidth));
        lowThru.setBounds (area.removeFromRight (juce::jmin (100, knobWidth)));
        meter.setBounds (area.reduced (16, 4));
    }
}
