#include "ChannelStripEditor.h"

#include "Theme.h"

namespace
{
    juce::String formatHz (double hz)
    {
        return hz >= 1000.0 ? juce::String (hz / 1000.0, hz >= 10000.0 ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz";
    }

    juce::String formatDb (double db)     { return (db > 0.05 ? "+" : "") + juce::String (db, 1) + " dB"; }
    juce::String formatMs (double ms)     { return ms < 10.0 ? juce::String (ms, 2) + " ms" : juce::String (juce::roundToInt (ms)) + " ms"; }

    constexpr int knobWidth = 74;
    constexpr int knobHeight = 96;
}

//==============================================================================
ChannelStripEditor::Knob::Knob (const juce::String& name, std::function<juce::String (double)> format)
    : formatValue (std::move (format))
{
    label.setText (name, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::FontOptions (12.0f));
    label.setColour (juce::Label::textColourId, Theme::textDim);
    addAndMakeVisible (label);

    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, knobWidth, 18);
    slider.textFromValueFunction = [this] (double v) { return formatValue (v); };
    slider.setColour (juce::Slider::rotarySliderFillColourId, Theme::accent);
    addAndMakeVisible (slider);
}

void ChannelStripEditor::Knob::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (16));
    slider.setBounds (r);
}

void ChannelStripEditor::GainReductionMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Theme::background);
    g.fillRoundedRectangle (r, 2.0f);

    // 上から下へ伸びる（0〜20 dB）
    const float h = r.getHeight() * juce::jlimit (0.0f, 1.0f, db / 20.0f);
    g.setColour (juce::Colour (0xffffb74d));
    g.fillRect (r.withHeight (h).reduced (1.0f, 0.0f));
}

//==============================================================================
ChannelStripEditor::ChannelStripEditor (AppContext& c, std::string id)
    : ctx (c), trackId (std::move (id)),
      lowCut ("ローカット"_ju, [] (double v) { return v < 20.0 ? "オフ"_ju : formatHz (v); }),
      lowGain ("Low"_ju, formatDb), lowFreq ("Low 周波数"_ju, formatHz),
      midGain ("Mid"_ju, formatDb), midFreq ("Mid 周波数"_ju, formatHz),
      midQ ("Mid Q"_ju, [] (double v) { return juce::String (v, 2); }),
      highGain ("High"_ju, formatDb), highFreq ("High 周波数"_ju, formatHz),
      threshold ("スレッショルド"_ju, formatDb),
      ratio ("レシオ"_ju, [] (double v) { return juce::String (v, 1) + ":1"; }),
      attack ("アタック"_ju, formatMs), release ("リリース"_ju, formatMs),
      makeup ("メイクアップ"_ju, formatDb)
{
    for (auto* b : { &eqEnabled, &compEnabled })
    {
        b->setColour (juce::ToggleButton::tickColourId, Theme::accent);
        addAndMakeVisible (b);
    }

    eqEnabled.setTooltip ("EQ のオン・オフ"_ju);
    compEnabled.setTooltip ("コンプのオン・オフ"_ju);
    eqEnabled.onClick = [this]
    {
        const bool on = eqEnabled.getToggleState();
        edit (on ? "EQ をオン"_ju : "EQ をオフ"_ju, [on] (collab::ChannelStrip& s) { s.eq.enabled = on; }, false);
    };
    compEnabled.onClick = [this]
    {
        const bool on = compEnabled.getToggleState();
        edit (on ? "コンプをオン"_ju : "コンプをオフ"_ju, [on] (collab::ChannelStrip& s) { s.comp.enabled = on; }, false);
    };

    compType.addItem ("FET（速い・パンチ）"_ju, 1);
    compType.addItem ("オプティカル（なめらか）"_ju, 2);
    compType.setTooltip ("FET: 1176 のように速く反応し、少し歪んで前に出る。オプティカル: LA-2A のようにゆっくり自然にかかる"_ju);
    compType.onChange = [this]
    {
        const auto type = compType.getSelectedId() == 2 ? collab::CompType::opto : collab::CompType::fet;

        if (auto* t = getTrack(); t != nullptr && t->strip.comp.type != type)
            edit ("コンプの種類"_ju, [type] (collab::ChannelStrip& s)
            {
                s.comp.type = type;

                // 種類ごとの定番の値に寄せる
                if (type == collab::CompType::opto)
                    s.comp.ratio = 3.0;
                else
                    s.comp.ratio = 4.0;
            }, false);
    };
    addAndMakeVisible (compType);

    resetEq.onClick = [this] { edit ("EQ をリセット"_ju, [] (collab::ChannelStrip& s) { s.eq = {}; }, false); };
    resetComp.onClick = [this] { edit ("コンプをリセット"_ju, [] (collab::ChannelStrip& s) { s.comp = {}; }, false); };
    addAndMakeVisible (resetEq);
    addAndMakeVisible (resetComp);

    const collab::ChannelStrip d;
    bind (lowCut, 0.0, 400.0, d.eq.lowCutHz, 80.0, "ローカット"_ju, [] (auto& s, double v) { s.eq.lowCutHz = v < 20.0 ? 0.0 : v; });
    bind (lowGain, -15.0, 15.0, d.eq.lowGainDb, 0.0, "EQ Low"_ju, [] (auto& s, double v) { s.eq.lowGainDb = v; });
    bind (lowFreq, 30.0, 600.0, d.eq.lowFreqHz, 150.0, "EQ Low 周波数"_ju, [] (auto& s, double v) { s.eq.lowFreqHz = v; });
    bind (midGain, -15.0, 15.0, d.eq.midGainDb, 0.0, "EQ Mid"_ju, [] (auto& s, double v) { s.eq.midGainDb = v; });
    bind (midFreq, 150.0, 12000.0, d.eq.midFreqHz, 1200.0, "EQ Mid 周波数"_ju, [] (auto& s, double v) { s.eq.midFreqHz = v; });
    bind (midQ, 0.3, 6.0, d.eq.midQ, 1.2, "EQ Mid Q"_ju, [] (auto& s, double v) { s.eq.midQ = v; });
    bind (highGain, -15.0, 15.0, d.eq.highGainDb, 0.0, "EQ High"_ju, [] (auto& s, double v) { s.eq.highGainDb = v; });
    bind (highFreq, 1500.0, 16000.0, d.eq.highFreqHz, 6000.0, "EQ High 周波数"_ju, [] (auto& s, double v) { s.eq.highFreqHz = v; });

    bind (threshold, -50.0, 0.0, d.comp.thresholdDb, -18.0, "コンプ スレッショルド"_ju, [] (auto& s, double v) { s.comp.thresholdDb = v; });
    bind (ratio, 1.0, 20.0, d.comp.ratio, 4.0, "コンプ レシオ"_ju, [] (auto& s, double v) { s.comp.ratio = v; });
    bind (attack, 0.05, 50.0, d.comp.attackMs, 3.0, "コンプ アタック"_ju, [] (auto& s, double v) { s.comp.attackMs = v; });
    bind (release, 20.0, 1500.0, d.comp.releaseMs, 200.0, "コンプ リリース"_ju, [] (auto& s, double v) { s.comp.releaseMs = v; });
    bind (makeup, 0.0, 24.0, d.comp.makeupDb, 6.0, "コンプ メイクアップ"_ju, [] (auto& s, double v) { s.comp.makeupDb = v; });

    addAndMakeVisible (grMeter);
    grLabel.setText ("GR"_ju, juce::dontSendNotification);
    grLabel.setJustificationType (juce::Justification::centred);
    grLabel.setFont (juce::FontOptions (11.0f));
    grLabel.setColour (juce::Label::textColourId, Theme::textDim);
    addAndMakeVisible (grLabel);

    optoNote.setText ("オプティカルのアタック・リリースは音に合わせて自動で変わります"_ju, juce::dontSendNotification);
    optoNote.setFont (juce::FontOptions (11.0f));
    optoNote.setColour (juce::Label::textColourId, Theme::textDim);
    addChildComponent (optoNote);

    ctx.document.addChangeListener (this);
    update();
    startTimerHz (30);
    setSize (knobWidth * 8 + 40, 360);
}

ChannelStripEditor::~ChannelStripEditor()
{
    ctx.document.removeChangeListener (this);
}

void ChannelStripEditor::setTrack (std::string id)
{
    trackId = std::move (id);
    update();
}

juce::String ChannelStripEditor::getTitle() const
{
    auto* t = getTrack();
    return (t != nullptr ? toJuce (t->name) : juce::String()) + " - EQ / コンプ"_ju;
}

const collab::Track* ChannelStripEditor::getTrack() const
{
    return ctx.document.getProject().findTrack (trackId);
}

void ChannelStripEditor::edit (const juce::String& description, std::function<void (collab::ChannelStrip&)> fn, bool merge)
{
    auto id = trackId;
    ctx.document.perform (description, [id, fn] (collab::Project& p)
    {
        if (auto* t = p.findTrack (id))
            fn (t->strip);
    }, merge ? mergeId : juce::String());
}

void ChannelStripEditor::bind (Knob& knob, double min, double max, double defaultValue, double skewMid,
                               const juce::String& description, std::function<void (collab::ChannelStrip&, double)> apply)
{
    auto& s = knob.slider;
    s.setRange (min, max, 0.0);

    if (skewMid > min && skewMid < max)
        s.setSkewFactorFromMidPoint (skewMid);

    s.setDoubleClickReturnValue (true, defaultValue);
    s.setTooltip (description + "（ダブルクリックで既定値）"_ju);
    s.onDragStart = [this] { mergeId = juce::Uuid().toString(); };
    s.onDragEnd = [this] { ctx.document.endMerge(); mergeId = {}; };
    s.onValueChange = [this, &s, description, apply]
    {
        const double v = s.getValue();
        edit (description, [apply, v] (collab::ChannelStrip& strip) { apply (strip, v); }, mergeId.isNotEmpty());
    };
    addAndMakeVisible (knob);
}

void ChannelStripEditor::update()
{
    auto* t = getTrack();
    setEnabled (t != nullptr);

    if (t == nullptr)
        return;

    const auto& s = t->strip;
    eqEnabled.setToggleState (s.eq.enabled, juce::dontSendNotification);
    compEnabled.setToggleState (s.comp.enabled, juce::dontSendNotification);
    compType.setSelectedId (s.comp.type == collab::CompType::opto ? 2 : 1, juce::dontSendNotification);

    const std::pair<Knob*, double> values[] = {
        { &lowCut, s.eq.lowCutHz }, { &lowGain, s.eq.lowGainDb }, { &lowFreq, s.eq.lowFreqHz },
        { &midGain, s.eq.midGainDb }, { &midFreq, s.eq.midFreqHz }, { &midQ, s.eq.midQ },
        { &highGain, s.eq.highGainDb }, { &highFreq, s.eq.highFreqHz },
        { &threshold, s.comp.thresholdDb }, { &ratio, s.comp.ratio }, { &attack, s.comp.attackMs },
        { &release, s.comp.releaseMs }, { &makeup, s.comp.makeupDb }
    };

    for (auto& [knob, v] : values)
        knob->slider.setValue (v, juce::dontSendNotification);

    for (auto* k : eqKnobs())
        k->setAlpha (s.eq.enabled ? 1.0f : 0.5f);

    const bool opto = s.comp.type == collab::CompType::opto;

    for (auto* k : compKnobs())
        k->setAlpha (s.comp.enabled ? 1.0f : 0.5f);

    attack.setEnabled (! opto);
    release.setEnabled (! opto);
    optoNote.setVisible (opto);

    if (onTitleChanged)
        onTitleChanged();

    repaint();
}

void ChannelStripEditor::timerCallback()
{
    const float db = ctx.engine.getTrackGainReductionDb (trackId);

    if (std::abs (db - grMeter.db) > 0.05f)
    {
        grMeter.db = db;
        grMeter.repaint();
    }
}

void ChannelStripEditor::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);

    for (auto r : { eqArea, compArea })
    {
        g.setColour (Theme::panel);
        g.fillRoundedRectangle (r.toFloat(), 6.0f);
    }
}

void ChannelStripEditor::resized()
{
    auto area = getLocalBounds().reduced (10);
    const int half = (area.getHeight() - 10) / 2;

    eqArea = area.removeFromTop (half);
    area.removeFromTop (10);
    compArea = area;

    {
        auto r = eqArea.reduced (8, 6);
        auto header = r.removeFromTop (24);
        eqEnabled.setBounds (header.removeFromLeft (80));
        resetEq.setBounds (header.removeFromRight (120));
        r.removeFromTop (4);

        for (auto* k : eqKnobs())
            k->setBounds (r.removeFromLeft (knobWidth).withHeight (knobHeight));
    }

    {
        auto r = compArea.reduced (8, 6);
        auto header = r.removeFromTop (24);
        compEnabled.setBounds (header.removeFromLeft (80));
        compType.setBounds (header.removeFromLeft (200));
        resetComp.setBounds (header.removeFromRight (120));
        header.removeFromLeft (10);
        optoNote.setBounds (header);
        r.removeFromTop (4);

        for (auto* k : compKnobs())
            k->setBounds (r.removeFromLeft (knobWidth).withHeight (knobHeight));

        r.removeFromLeft (16);
        auto meterArea = r.removeFromLeft (30).withHeight (knobHeight);
        grLabel.setBounds (meterArea.removeFromTop (16));
        grMeter.setBounds (meterArea.reduced (6, 2));
    }
}
