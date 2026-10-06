#include "BuiltinEffectEditor.h"

#include "ValueText.h"
#include "Theme.h"

namespace
{
    using collab::fx::ParamSpec;

    juce::String utf8 (const std::string& s)    { return juce::String::fromUTF8 (s.c_str()); }

    /** 値の表示（段階のあるつまみは選んだ名前）。 */
    juce::String formatValue (const ParamSpec& spec, double v)
    {
        if (! spec.choices.empty())
        {
            const auto& c = spec.choices[(size_t) juce::jlimit (0, (int) spec.choices.size() - 1, (int) std::round (v))];
            return utf8 (c) + (spec.unit.empty() || c == "AUTO" || c == "OFF" ? juce::String() : " " + utf8 (spec.unit));
        }

        if (spec.unit == "Hz")
            return ValueText::formatHz (v);

        if (spec.unit == "%" || spec.unit == "ms")
            return juce::String (juce::roundToInt (v)) + " " + utf8 (spec.unit);

        if (spec.unit == "s")
            return juce::String (v, 2) + " s";

        return (v > 0.05 && spec.unit == "dB" && spec.min < 0 ? "+" : "") + juce::String (v, 1) + " " + utf8 (spec.unit);
    }

    /** 落ち着いた黒いつまみ（値の所まで青い弧）。 */
    class KnobLook  : public juce::LookAndFeel_V4
    {
    public:
        void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                               float startAngle, float endAngle, juce::Slider& slider) override
        {
            auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
            const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
            const auto centre = bounds.getCentre();
            const float angle = startAngle + pos * (endAngle - startAngle);

            juce::Path track, value;
            track.addCentredArc (centre.x, centre.y, radius - 2.0f, radius - 2.0f, 0.0f, startAngle, endAngle, true);
            value.addCentredArc (centre.x, centre.y, radius - 2.0f, radius - 2.0f, 0.0f, startAngle, angle, true);
            // つまみの色（SSL のバスコンプのように、つまみごとに色を変えるとき）
            const auto capVar = slider.getProperties()["capColour"];
            const bool hasCap = ! capVar.isVoid();
            const auto cap = hasCap ? juce::Colour ((juce::uint32) (juce::int64) capVar) : Theme::accent;

            g.setColour (Theme::field);
            g.strokePath (track, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (hasCap ? cap.brighter (0.2f) : Theme::accent);
            g.strokePath (value, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            const float knobRadius = radius - 9.0f;
            auto knob = juce::Rectangle<float> (knobRadius * 2.0f, knobRadius * 2.0f).withCentre (centre);

            if (hasCap)
            {
                // 色付きのつまみ（SSL のように、色の付いた丸い頭）
                g.setColour (juce::Colours::black.withAlpha (0.45f));
                g.fillEllipse (knob.translated (0.0f, 2.0f));
                g.setGradientFill (juce::ColourGradient (cap.brighter (0.35f), knob.getX(), knob.getY(), cap.darker (0.35f), knob.getRight(), knob.getBottom(), false));
                g.fillEllipse (knob);
                g.setColour (juce::Colours::white.withAlpha (0.18f));
                g.drawEllipse (knob.reduced (0.5f), 1.0f);
                g.setColour (cap.getPerceivedBrightness() > 0.6f ? juce::Colours::black : juce::Colours::white);
                g.drawLine ({ centre.getPointOnCircumference (knobRadius * 0.3f, angle), centre.getPointOnCircumference (knobRadius - 2.0f, angle) }, 2.4f);
                return;
            }

            if (Theme::light)
            {
                // ライト: 面と同じ色の浮き上がったつまみ
                Theme::drawRaised (g, knob, knobRadius, false, Theme::panel, 2.5f);
            }
            else
            {
                g.setColour (juce::Colours::black.withAlpha (0.45f));
                g.fillEllipse (knob.translated (0.0f, 2.0f));
                g.setColour (juce::Colour (0xff26282b));
                g.fillEllipse (knob);
                g.setColour (juce::Colours::white.withAlpha (0.12f));
                g.drawEllipse (knob.reduced (0.5f), 1.0f);
            }

            g.setColour (Theme::text);
            g.drawLine ({ centre.getPointOnCircumference (knobRadius * 0.3f, angle), centre.getPointOnCircumference (knobRadius - 2.0f, angle) }, 2.2f);
        }
    };

    KnobLook& knobLook()
    {
        static KnobLook look;
        return look;
    }

    constexpr int knobWidth = 92, knobHeight = 118, headerHeight = 44, meterWidth = 150;
}

//==============================================================================
struct BuiltinEffectEditor::Knob  : public juce::Component
{
    Knob (BuiltinEffectEditor& o, ParamSpec s) : owner (o), spec (std::move (s))
    {
        slider.setLookAndFeel (&knobLook());
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);

        if (spec.choices.empty())
        {
            slider.setRange (spec.min, spec.max, 0.0);

            if (spec.skewMid > spec.min && spec.skewMid < spec.max)
                slider.setSkewFactorFromMidPoint (spec.skewMid);
        }
        else
        {
            slider.setRange (0.0, (double) spec.choices.size() - 1.0, 1.0);   // 段階に吸い付く
        }

        slider.setDoubleClickReturnValue (true, spec.def);
        slider.setMouseDragSensitivity (spec.choices.empty() ? 220 : 90);
        slider.onDragStart = [this] { owner.mergeId = juce::Uuid().toString(); };
        slider.onDragEnd = [this] { owner.ctx.document.endMerge(); owner.mergeId = {}; };
        slider.onValueChange = [this]
        {
            owner.setParam (spec.key, slider.getValue());
            valueLabel.setText (formatValue (spec, slider.getValue()), juce::dontSendNotification);
        };
        addAndMakeVisible (slider);

        // 数字をクリックして打ち込む
        valueLabel.setJustificationType (juce::Justification::centred);
        valueLabel.setFont (juce::FontOptions (14.0f));
        valueLabel.setColour (juce::Label::textColourId, Theme::text);
        valueLabel.setColour (juce::Label::backgroundWhenEditingColourId, Theme::field);
        valueLabel.setColour (juce::Label::outlineWhenEditingColourId, Theme::accent);
        valueLabel.setEditable (spec.choices.empty(), spec.choices.empty(), false);
        valueLabel.onTextChange = [this]
        {
            const auto text = valueLabel.getText().retainCharacters ("0123456789.-+");
            double v = text.getDoubleValue();

            if (spec.unit == "Hz" && valueLabel.getText().containsIgnoreCase ("k"))
                v *= 1000.0;

            if (text.isNotEmpty())
                slider.setValue (juce::jlimit (spec.min, spec.max, v), juce::sendNotificationSync);

            valueLabel.setText (formatValue (spec, slider.getValue()), juce::dontSendNotification);
        };
        addAndMakeVisible (valueLabel);
    }

    ~Knob() override    { slider.setLookAndFeel (nullptr); }

    void setValue (double v)
    {
        if (! slider.isMouseButtonDown())
            slider.setValue (v, juce::dontSendNotification);

        if (! valueLabel.isBeingEdited())
            valueLabel.setText (formatValue (spec, slider.getValue()), juce::dontSendNotification);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (labelColour);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (utf8 (spec.label), getLocalBounds().removeFromTop (18), juce::Justification::centred);
    }

    juce::Colour labelColour = Theme::textDim;

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromTop (18);
        valueLabel.setBounds (r.removeFromBottom (22).reduced (4, 0));
        slider.setBounds (r);
    }

    BuiltinEffectEditor& owner;
    ParamSpec spec;
    juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::Label valueLabel;
};

//==============================================================================
BuiltinEffectEditor::BuiltinEffectEditor (AppContext& c, std::string track, std::string fx)
    : ctx (c), trackId (std::move (track)), effectId (std::move (fx))
{
    if (auto* e = effect())
        type = collab::fx::typeFromId (e->builtin);

    if (type)
        for (auto& spec : collab::fx::paramSpecs (*type))
            addAndMakeVisible (knobs.add (new Knob (*this, spec)));

    presetButton.setButtonText ("プリセット"_ju);
    presetButton.setTooltip ("用途ごとの設定"_ju);
    presetButton.onClick = [this] { showPresets(); };
    addAndMakeVisible (presetButton);

    bypassButton.setButtonText ("BYPASS");
    bypassButton.setTooltip ("バイパス"_ju);
    bypassButton.setClickingTogglesState (false);
    bypassButton.setColour (juce::TextButton::buttonOnColourId, Theme::warning.darker (0.3f));
    bypassButton.onClick = [this] { ctx.toggleEffectBypass (trackId, effectId); };
    addAndMakeVisible (bypassButton);

    const bool meter = isBusComp() || isGate();

    if (isBusComp())
    {
        // SSL のバスコンプのつまみの色（スレッショルドは赤、アタック・リリース・レシオは黒、メイクアップは白、ほかは灰色）
        const std::pair<const char*, juce::uint32> caps[] = {
            { "threshold", 0xffc8312c }, { "attack", 0xff2b2d31 }, { "release", 0xff2b2d31 }, { "ratio", 0xff2b2d31 },
            { "makeup", 0xffe9e6df }, { "sidechainHpf", 0xff7d8188 }, { "mix", 0xff7d8188 } };

        for (auto& [key, colour] : caps)
            if (auto* k = knobFor (key))
            {
                k->slider.getProperties().set ("capColour", (juce::int64) colour);
                k->labelColour = juce::Colour (0xffe4e2dc);
                k->valueLabel.setColour (juce::Label::textColourId, juce::Colour (0xffe4e2dc));
            }

        bypassButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff4a4d53));
        presetButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff4a4d53));
        setSize (4 * knobWidth + 24 + 250, headerHeight + 2 * knobHeight + 32);
    }
    else if (isGate())
    {
        setSize (640, headerHeight + 250 + knobHeight + 28);
    }
    else
    {
        setSize (juce::jmax (360, knobs.size() * knobWidth + 24), headerHeight + knobHeight + 24);
    }

    ctx.document.addChangeListener (this);
    refresh();

    if (meter)
        startTimerHz (60);
}

BuiltinEffectEditor::~BuiltinEffectEditor()
{
    ctx.document.removeChangeListener (this);
}

const collab::Effect* BuiltinEffectEditor::effect() const
{
    if (auto* fx = ctx.document.getProject().effectsFor (trackId))
        for (auto& e : *fx)
            if (e.id == effectId)
                return &e;

    return nullptr;
}

juce::String BuiltinEffectEditor::getTitle() const
{
    auto* t = ctx.document.getProject().findTrack (trackId);
    auto* e = effect();
    const auto owner = t != nullptr ? toJuce (t->name) : trackId == ctx.document.getProject().master.id ? "マスター"_ju : juce::String();
    return (e != nullptr ? AppContext::effectName (*e) : juce::String()) + (owner.isNotEmpty() ? " - " + owner : juce::String());
}

void BuiltinEffectEditor::setParam (const std::string& key, double value)
{
    auto track = trackId, fx = effectId;
    ctx.document.perform ("エフェクトの設定"_ju, [track, fx, key, value] (collab::Project& p)
    {
        if (auto* list = p.effectsFor (track))
            for (auto& e : *list)
                if (e.id == fx)
                    e.params[key] = value;
    }, mergeId);
}

void BuiltinEffectEditor::showPresets()
{
    if (! type)
        return;

    juce::PopupMenu m;

    for (auto& preset : collab::fx::factoryPresets (*type))
        m.addItem (juce::String::fromUTF8 (preset.name.c_str()), [this, params = preset.params]
        {
            auto track = trackId, fx = effectId;
            ctx.document.perform ("エフェクトのプリセット"_ju, [track, fx, params] (collab::Project& p)
            {
                if (auto* list = p.effectsFor (track))
                    for (auto& e : *list)
                        if (e.id == fx)
                            for (auto it = params.begin(); it != params.end(); ++it)   // MSVC は入れ子のラムダで構造化束縛を使えない
                                e.params[it.key()] = it.value();
            });
        });

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButton));
}

void BuiltinEffectEditor::refresh()
{
    auto* e = effect();

    if (e == nullptr || ! type)
        return;

    for (auto* k : knobs)
        k->setValue (collab::fx::paramValue (e->params, k->spec));

    bypassButton.setToggleState (e->bypass, juce::dontSendNotification);
    repaint (titleArea);
}

bool BuiltinEffectEditor::isBusComp() const   { return type == collab::fx::Type::busComp; }
bool BuiltinEffectEditor::isGate() const      { return type == collab::fx::Type::noiseGate; }

BuiltinEffectEditor::Knob* BuiltinEffectEditor::knobFor (const std::string& key) const
{
    for (auto* k : knobs)
        if (k->spec.key == key)
            return k;

    return nullptr;
}

void BuiltinEffectEditor::timerCallback()
{
    const auto m = ctx.engine.takeEffectMeter (trackId, effectId);
    pushMeter (m.input, m.output, m.gainReductionDb);
}

void BuiltinEffectEditor::pushMeter (float inputPeak, float outputPeak, float gainReductionDb)
{
    if (isBusComp())
    {
        // VU の針: 300 ms で目標の 99% に届く速さ（上がるときも戻るときも同じ。本物の VU メーターと同じ動き）
        const float step = 1.0f - std::exp (-1.0f / (0.065f * 60.0f));
        const float next = vuGr + (gainReductionDb - vuGr) * step;

        if (std::abs (next - vuGr) > 0.005f)
        {
            vuGr = next;
            repaint (meterArea.expanded (0, 26));
        }

        shownGr = gainReductionDb;
        return;
    }

    if (isGate())
    {
        // 1 列ずつ左へ流す
        auto toDb = [] (float p) { return p > 1.0e-5f ? juce::Decibels::gainToDecibels (p) : -100.0f; };
        history[historyPos] = { toDb (inputPeak), toDb (outputPeak), gainReductionDb };
        historyPos = (historyPos + 1) % history.size();
        shownGr = gainReductionDb;
        repaint (displayArea);
    }
}

double BuiltinEffectEditor::thresholdDb() const
{
    auto* e = effect();
    return e != nullptr ? collab::fx::paramValue (collab::fx::Type::noiseGate, e->params, "threshold") : -50.0;
}

float BuiltinEffectEditor::gateY (float db) const
{
    // 上が 0 dBFS、下が -80 dBFS
    const auto r = displayArea.toFloat().reduced (0.0f, 8.0f);
    return r.getY() + r.getHeight() * juce::jlimit (0.0f, 1.0f, -db / 80.0f);
}

void BuiltinEffectEditor::mouseMove (const juce::MouseEvent& e)
{
    const bool onLine = isGate() && displayArea.contains (e.getPosition()) && std::abs ((float) e.y - gateY ((float) thresholdDb())) < 6.0f;
    setMouseCursor (onLine ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
}

void BuiltinEffectEditor::mouseDown (const juce::MouseEvent& e)
{
    // スレッショルドの線をつかむ（線の近くでなくても、画面の中を押したらその高さへ）
    draggingThreshold = isGate() && displayArea.contains (e.getPosition());

    if (draggingThreshold)
    {
        mergeId = juce::Uuid().toString();
        mouseDrag (e);
    }
}

void BuiltinEffectEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (! draggingThreshold)
        return;

    const auto r = displayArea.toFloat().reduced (0.0f, 8.0f);
    const double db = juce::jlimit (-80.0, 0.0, std::round (-80.0 * ((double) e.y - r.getY()) / r.getHeight() * 2.0) / 2.0);
    setParam ("threshold", db);
}

void BuiltinEffectEditor::mouseUp (const juce::MouseEvent&)
{
    if (std::exchange (draggingThreshold, false))
    {
        ctx.document.endMerge();
        mergeId = {};
    }
}

void BuiltinEffectEditor::paint (juce::Graphics& g)
{
    if (isBusComp())
        return paintBusComp (g);

    g.fillAll (Theme::panel);

    // 見出し: 種類の名前
    g.setColour (Theme::text);
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));

    if (auto* e = effect())
        g.drawText (AppContext::effectName (*e), titleArea, juce::Justification::centredLeft);

    if (isGate())
        return paintGate (g);

    if (meterArea.isEmpty())
        return;

    // ゲインリダクション（右から左へ伸びるバーと数字。-20 dB まで）
    auto r = meterArea.toFloat();
    g.setColour (Theme::textDim);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("GAIN REDUCTION", r.removeFromTop (18.0f), juce::Justification::centred);

    auto bar = r.removeFromTop (22.0f).reduced (4.0f, 2.0f);
    g.setColour (Theme::field);
    g.fillRoundedRectangle (bar, 3.0f);
    g.setColour (juce::Colour (0xffffb74d));
    g.fillRoundedRectangle (bar.withLeft (bar.getRight() - bar.getWidth() * juce::jlimit (0.0f, 1.0f, shownGr / 20.0f)), 3.0f);

    g.setColour (Theme::textDim);
    g.setFont (juce::FontOptions (11.0f));

    for (int db : { 0, 4, 8, 12, 16, 20 })
    {
        const float x = bar.getRight() - bar.getWidth() * (float) db / 20.0f;
        g.drawText (juce::String (db), juce::Rectangle<float> (x - 12.0f, bar.getBottom() + 1.0f, 24.0f, 12.0f), juce::Justification::centred);
    }

    r.removeFromTop (16.0f);
    g.setColour (Theme::text);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ((shownGr >= 0.05f ? "-" : "") + juce::String (shownGr, 1) + " dB", r.removeFromTop (30.0f), juce::Justification::centred);
}

void BuiltinEffectEditor::resized()
{
    auto area = getLocalBounds().reduced (12);
    auto header = area.removeFromTop (headerHeight - 12);
    bypassButton.setBounds (header.removeFromRight (90).reduced (0, 2));
    header.removeFromRight (8);
    presetButton.setBounds (header.removeFromRight (110).reduced (0, 2));
    titleArea = header;

    meterArea = displayArea = {};

    if (isBusComp())
    {
        // SSL の並び: 左に VU メーター、右に 2 段のつまみ（上: スレッショルド・メイクアップ・ミックス、下: アタック・リリース・レシオ・HPF）
        meterArea = area.removeFromLeft (236).withHeight (2 * knobHeight - 30).withTrimmedTop (10);
        area.removeFromLeft (14);
        auto top = area.removeFromTop (knobHeight);
        area.removeFromTop (8);
        auto bottom = area.removeFromTop (knobHeight);

        for (auto key : { "threshold", "makeup", "mix" })
            if (auto* k = knobFor (key))
                k->setBounds (top.removeFromLeft (knobWidth));

        for (auto key : { "attack", "release", "ratio", "sidechainHpf" })
            if (auto* k = knobFor (key))
                k->setBounds (bottom.removeFromLeft (knobWidth));

        return;
    }

    if (isGate())
    {
        displayArea = area.removeFromTop (240);
        area.removeFromTop (10);
    }

    for (auto* k : knobs)
        k->setBounds (area.removeFromLeft (knobWidth).withHeight (knobHeight));
}

void BuiltinEffectEditor::paintBusComp (juce::Graphics& g)
{
    // SSL のバスコンプのような灰色の面
    const auto panel = juce::Colour (0xff3d4046);
    g.setGradientFill (juce::ColourGradient (panel.brighter (0.12f), 0.0f, 0.0f, panel.darker (0.2f), 0.0f, (float) getHeight(), false));
    g.fillAll();
    g.setColour (juce::Colour (0xffe4e2dc));
    g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
    g.drawText ("BUS COMPRESSOR", titleArea, juce::Justification::centredLeft);

    // VU メーター（クリーム色の面、0 が右、20 が左。針はゲインリダクションの分だけ左へ）
    auto box = meterArea.toFloat();
    g.setColour (juce::Colour (0xff1c1d20));
    g.fillRoundedRectangle (box, 6.0f);
    auto face = box.reduced (8.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff6edd2), face.getCentreX(), face.getY(),
                                             juce::Colour (0xffe2d3a8), face.getCentreX(), face.getBottom(), false));
    g.fillRoundedRectangle (face, 4.0f);

    // 針の軸は面の下の方、目盛りの弧が面の幅いっぱいに収まる大きさ
    const float radius = juce::jmin (face.getWidth() * 0.56f, face.getHeight() * 0.95f);
    const juce::Point<float> pivot (face.getCentreX(), face.getY() + face.getHeight() * 0.22f + radius);
    const float leftAngle = -0.8f, rightAngle = 0.8f;   // 上を 0 とした角度（ラジアン）
    auto angleFor = [&] (float gr) { return rightAngle + (leftAngle - rightAngle) * juce::jlimit (0.0f, 1.0f, gr / 20.0f); };

    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (face.toNearestInt());

        // 目盛りの弧と数字
        juce::Path arc;
        arc.addCentredArc (pivot.x, pivot.y, radius * 0.86f, radius * 0.86f, 0.0f, leftAngle, rightAngle, true);
        g.setColour (juce::Colour (0xff2a2a2a));
        g.strokePath (arc, juce::PathStrokeType (1.4f));

        g.setFont (juce::FontOptions (12.5f, juce::Font::bold));

        for (int db : { 0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20 })
        {
            const float a = angleFor ((float) db);
            const bool major = db % 4 == 0;
            g.setColour (db == 0 ? juce::Colour (0xffb3261e) : juce::Colour (0xff2a2a2a));
            g.drawLine ({ pivot.getPointOnCircumference (radius * 0.86f, a), pivot.getPointOnCircumference (radius * (major ? 0.95f : 0.91f), a) },
                        major ? 1.6f : 1.0f);

            if (major)
                g.drawText (juce::String (db), juce::Rectangle<float> (26.0f, 14.0f).withCentre (pivot.getPointOnCircumference (radius * 1.05f, a)),
                            juce::Justification::centred);
        }

        g.setColour (juce::Colour (0xff2a2a2a));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("GAIN REDUCTION dB", juce::Rectangle<float> (face.getX(), pivot.y - radius * 0.55f, face.getWidth(), 16.0f), juce::Justification::centred);

        // 針
        const float a = angleFor (vuGr);
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.drawLine ({ pivot.translated (2.0f, 2.0f), pivot.getPointOnCircumference (radius * 0.93f, a).translated (2.0f, 2.0f) }, 1.6f);
        g.setColour (juce::Colour (0xff151515));
        g.drawLine ({ pivot, pivot.getPointOnCircumference (radius * 0.93f, a) }, 1.6f);
    }

    // 数字（いまのゲインリダクション）
    g.setColour (juce::Colour (0xffe4e2dc));
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText ((vuGr >= 0.05f ? "-" : "") + juce::String (vuGr, 1) + " dB",
                juce::Rectangle<int> (meterArea.getX(), meterArea.getBottom() + 4, meterArea.getWidth(), 20), juce::Justification::centred);
}

void BuiltinEffectEditor::paintGate (juce::Graphics& g)
{
    auto r = displayArea.toFloat();
    g.setColour (juce::Colour (0xff15171b));
    g.fillRoundedRectangle (r, 6.0f);

    // dB の目盛り（0〜-80）
    g.setFont (juce::FontOptions (11.0f));

    for (int db = 0; db >= -80; db -= 20)
    {
        const float y = gateY ((float) db);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawHorizontalLine ((int) y, r.getX() + 30.0f, r.getRight() - 4.0f);
        g.setColour (juce::Colours::white.withAlpha (0.4f));
        g.drawText (juce::String (db), juce::Rectangle<float> (r.getX() + 2.0f, y - 7.0f, 26.0f, 14.0f), juce::Justification::centredRight);
    }

    const auto plot = r.withTrimmedLeft (32.0f).reduced (2.0f, 0.0f);
    const float dx = plot.getWidth() / (float) history.size();

    // 入ってきた音（灰色）・出ていく音（水色）を下から塗る。ゲインリダクション（赤）は上から下へ
    auto area = [&] (auto value, bool fromTop)
    {
        juce::Path p;
        p.startNewSubPath (plot.getX(), fromTop ? gateY (0.0f) : gateY (-80.0f));

        for (size_t k = 0; k < history.size(); ++k)
        {
            const auto& c = history[(historyPos + k) % history.size()];
            const float x = plot.getX() + (float) k * dx;
            const float y = fromTop ? gateY (-juce::jmin (80.0f, value (c))) : gateY (juce::jmax (-80.0f, value (c)));
            p.lineTo (x, y);
            p.lineTo (x + dx, y);
        }

        p.lineTo (plot.getRight(), fromTop ? gateY (0.0f) : gateY (-80.0f));
        p.closeSubPath();
        return p;
    };

    g.setColour (juce::Colours::white.withAlpha (0.22f));
    g.fillPath (area ([] (const Column& c) { return c.inputDb; }, false));
    g.setColour (juce::Colour (0xff4fc3f7).withAlpha (0.75f));
    g.fillPath (area ([] (const Column& c) { return c.outputDb; }, false));
    g.setColour (juce::Colour (0xffef5350).withAlpha (0.45f));
    g.fillPath (area ([] (const Column& c) { return c.reductionDb; }, true));

    // スレッショルドの線（ドラッグで動かす）と、閉じる所（4 dB 下）
    const float ty = gateY ((float) thresholdDb());
    g.setColour (juce::Colour (0xffffd54f));
    g.drawHorizontalLine ((int) ty, plot.getX(), plot.getRight());
    const float dashes[] = { 4.0f, 4.0f };
    g.setColour (juce::Colour (0xffffd54f).withAlpha (0.35f));
    g.drawDashedLine ({ plot.getX(), gateY ((float) thresholdDb() - 4.0f), plot.getRight(), gateY ((float) thresholdDb() - 4.0f) }, dashes, 2, 1.0f);
    g.setColour (juce::Colour (0xffffd54f));
    g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
    g.drawText ("THRESHOLD " + juce::String (thresholdDb(), 1) + " dB", juce::Rectangle<float> (plot.getRight() - 170.0f, ty - 18.0f, 164.0f, 16.0f),
                juce::Justification::centredRight);

    // 凡例と、いまのゲインリダクション
    auto legend = r.reduced (40.0f, 6.0f).removeFromTop (16.0f);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));

    for (auto [text, colour] : { std::pair<const char*, juce::Colour> { "IN", juce::Colours::white.withAlpha (0.55f) },
                                 { "OUT", juce::Colour (0xff4fc3f7) }, { "GR", juce::Colour (0xffef5350) } })
    {
        g.setColour (colour);
        g.fillRect (legend.removeFromLeft (10.0f).withSizeKeepingCentre (10.0f, 10.0f));
        legend.removeFromLeft (4.0f);
        g.drawText (text, legend.removeFromLeft (34.0f), juce::Justification::centredLeft);
    }

    g.setColour (juce::Colour (0xffef5350));
    g.drawText ((shownGr >= 0.05f ? "-" : "") + juce::String (shownGr, 1) + " dB", legend.removeFromRight (80.0f), juce::Justification::centredRight);
}
