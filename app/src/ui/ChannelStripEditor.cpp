#include "ChannelStripEditor.h"

#include "Theme.h"

//==============================================================================
ChannelStripEditor::ChannelStripEditor (AppContext& c, std::string id)
    : ctx (c), trackId (std::move (id)), eqGraph (c)
{
    for (auto* b : { &eqEnabled, &compEnabled })
    {
        b->setColour (juce::ToggleButton::tickColourId, Theme::accent);
        addAndMakeVisible (b);
    }

    eqEnabled.setTooltip ("EQ のオン・オフ"_ju);
    compEnabled.setTooltip ("Compressor のオン・オフ"_ju);
    eqEnabled.onClick = [this]
    {
        const bool on = eqEnabled.getToggleState();
        edit (on ? "EQ をオン"_ju : "EQ をオフ"_ju, [on] (collab::ChannelStrip& s) { s.eq.enabled = on; }, false);
    };
    compEnabled.onClick = [this]
    {
        const bool on = compEnabled.getToggleState();
        edit (on ? "Compressor をオン"_ju : "Compressor をオフ"_ju, [on] (collab::ChannelStrip& s) { s.comp.enabled = on; }, false);
    };

    compType.addItem ("FET（1176 風: 速い・パンチ）"_ju, 1);
    compType.addItem ("Optical（LA-2A 風: なめらか）"_ju, 2);
    compType.setTooltip ("FET: 1176 のように速く反応し、少し歪んで前に出る。Optical: LA-2A のようにゆっくり自然にかかる"_ju);
    compType.onChange = [this]
    {
        const auto type = compType.getSelectedId() == 2 ? collab::CompType::opto : collab::CompType::fet;

        if (auto* t = getTrack(); t != nullptr && t->strip.comp.type != type)
            edit ("Compressor の種類"_ju, [type] (collab::ChannelStrip& s)
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
    resetComp.onClick = [this]
    {
        edit ("Compressor をリセット"_ju, [] (collab::ChannelStrip& s)
        {
            const auto type = s.comp.type;
            const bool on = s.comp.enabled;
            s.comp = {};
            s.comp.type = type;
            s.comp.enabled = on;

            if (type == collab::CompType::opto)
                s.comp.ratio = 3.0;
        }, false);
    };
    addAndMakeVisible (resetEq);
    addAndMakeVisible (resetComp);

    eqGraph.onEdit = [this] (const juce::String& description, std::function<void (collab::ChannelEq&)> fn, bool merge)
    {
        if (! merge || mergeId.isEmpty())
            mergeId = juce::Uuid().toString();

        edit (description, [fn] (collab::ChannelStrip& s) { fn (s.eq); }, true);
    };
    eqGraph.onEditEnd = [this] { ctx.document.endMerge(); mergeId = {}; };
    addAndMakeVisible (eqGraph);

    eqHint.setText ("点をドラッグ: 周波数・ゲイン　ホイール: Q（LM・M）　ダブルクリック: リセット　LC・HC は端まで動かすとオフ"_ju,
                    juce::dontSendNotification);
    eqHint.setFont (juce::FontOptions (11.0f));
    eqHint.setColour (juce::Label::textColourId, Theme::textDim);
    addAndMakeVisible (eqHint);

    compressor.onEdit = [this] (const juce::String& description, std::function<void (collab::ChannelComp&)> fn, bool merge)
    {
        if (! merge || mergeId.isEmpty())
            mergeId = juce::Uuid().toString();

        // つまみを触ったら Compressor をオンにする
        edit (description, [fn] (collab::ChannelStrip& s) { fn (s.comp); s.comp.enabled = true; }, true);
    };
    compressor.onEditEnd = [this] { ctx.document.endMerge(); mergeId = {}; };
    addAndMakeVisible (compressor);

    ctx.document.addChangeListener (this);
    ctx.engine.setSpectrumTrack (trackId);
    update();
    startTimerHz (30);
    setSize (780, 600);
}

ChannelStripEditor::~ChannelStripEditor()
{
    ctx.document.removeChangeListener (this);
    ctx.engine.setSpectrumTrack ({});
}

void ChannelStripEditor::setTrack (std::string id)
{
    trackId = std::move (id);
    ctx.engine.setSpectrumTrack (trackId);
    update();
}

juce::String ChannelStripEditor::getTitle() const
{
    auto* t = getTrack();
    return (t != nullptr ? toJuce (t->name) : juce::String()) + " - EQ / Compressor"_ju;
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

    eqGraph.setEq (s.eq);
    compressor.setComp (s.comp);

    if (onTitleChanged)
        onTitleChanged();

    repaint();
}

void ChannelStripEditor::timerCallback()
{
    compressor.setGainReduction (ctx.engine.getTrackGainReductionDb (trackId));
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
    compArea = area.removeFromBottom (250);
    area.removeFromBottom (10);
    eqArea = area;

    {
        auto r = eqArea.reduced (8, 6);
        auto header = r.removeFromTop (24);
        eqEnabled.setBounds (header.removeFromLeft (80));
        resetEq.setBounds (header.removeFromRight (120));
        r.removeFromTop (4);
        eqHint.setBounds (r.removeFromBottom (16));
        eqGraph.setBounds (r);
    }

    {
        auto r = compArea.reduced (8, 6);
        auto header = r.removeFromTop (24);
        compEnabled.setBounds (header.removeFromLeft (120));
        compType.setBounds (header.removeFromLeft (260));
        resetComp.setBounds (header.removeFromRight (160));
        r.removeFromTop (6);
        compressor.setBounds (r);
    }
}
