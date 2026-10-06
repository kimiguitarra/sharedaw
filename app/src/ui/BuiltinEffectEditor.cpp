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
                               float startAngle, float endAngle, juce::Slider&) override
        {
            auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
            const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
            const auto centre = bounds.getCentre();
            const float angle = startAngle + pos * (endAngle - startAngle);

            juce::Path track, value;
            track.addCentredArc (centre.x, centre.y, radius - 2.0f, radius - 2.0f, 0.0f, startAngle, endAngle, true);
            value.addCentredArc (centre.x, centre.y, radius - 2.0f, radius - 2.0f, 0.0f, startAngle, angle, true);
            g.setColour (Theme::field);
            g.strokePath (track, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (Theme::accent);
            g.strokePath (value, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            const float knobRadius = radius - 9.0f;
            auto knob = juce::Rectangle<float> (knobRadius * 2.0f, knobRadius * 2.0f).withCentre (centre);

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
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (utf8 (spec.label), getLocalBounds().removeFromTop (18), juce::Justification::centred);
    }

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

    const bool meter = type == collab::fx::Type::busComp || type == collab::fx::Type::noiseGate;
    setSize (juce::jmax (360, knobs.size() * knobWidth + 24 + (meter ? meterWidth + 12 : 0)), headerHeight + knobHeight + 24);

    ctx.document.addChangeListener (this);
    refresh();

    if (meter)
        startTimerHz (30);
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

void BuiltinEffectEditor::timerCallback()
{
    const float gr = ctx.engine.getEffectGainReductionDb (trackId, effectId);
    const float next = gr > shownGr ? gr : juce::jmax (gr, shownGr - 0.4f);

    if (std::abs (next - shownGr) > 0.01f)
    {
        shownGr = next;
        repaint (meterArea);
    }
}

void BuiltinEffectEditor::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panel);

    // 見出し: 種類の名前
    g.setColour (Theme::text);
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));

    if (auto* e = effect())
        g.drawText (AppContext::effectName (*e), titleArea, juce::Justification::centredLeft);

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

    if (type == collab::fx::Type::busComp || type == collab::fx::Type::noiseGate)
    {
        meterArea = area.removeFromRight (meterWidth);
        area.removeFromRight (12);
    }
    else
    {
        meterArea = {};
    }

    for (auto* k : knobs)
        k->setBounds (area.removeFromLeft (knobWidth).withHeight (knobHeight));
}
