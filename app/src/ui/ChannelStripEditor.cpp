#include "ChannelStripEditor.h"

#include "Theme.h"

//==============================================================================
ChannelStripEditor::ChannelStripEditor (AppContext& c, std::string id, Section s)
    : ctx (c), trackId (std::move (id)), section (s)
{
    const bool isEq = section == Section::eq;

    enabled.setButtonText (isEq ? "EQ" : "Compressor");
    enabled.setColour (juce::ToggleButton::tickColourId, Theme::accent);
    enabled.setTooltip (isEq ? "EQ のオン・オフ"_ju : "Compressor のオン・オフ"_ju);
    enabled.onClick = [this, isEq]
    {
        const bool on = enabled.getToggleState();
        edit (isEq ? (on ? "EQ をオン"_ju : "EQ をオフ"_ju) : (on ? "Compressor をオン"_ju : "Compressor をオフ"_ju),
              [on, isEq] (collab::ChannelStrip& st) { (isEq ? st.eq.enabled : st.comp.enabled) = on; }, false);
    };
    addAndMakeVisible (enabled);

    orderButton.setTooltip ("順番"_ju);
    orderButton.onClick = [this]
    {
        edit ("EQ と Compressor の順番"_ju, [] (collab::ChannelStrip& st) { st.compFirst = ! st.compFirst; }, false);
    };
    addAndMakeVisible (orderButton);

    resetButton.setButtonText ("リセット"_ju);
    resetButton.onClick = [this, isEq]
    {
        if (isEq)
        {
            edit ("EQ をリセット"_ju, [] (collab::ChannelStrip& st) { const bool on = st.eq.enabled; st.eq = {}; st.eq.enabled = on; }, false);
            return;
        }

        edit ("Compressor をリセット"_ju, [] (collab::ChannelStrip& st)
        {
            const auto type = st.comp.type;
            const bool on = st.comp.enabled;
            st.comp = {};
            st.comp.type = type;
            st.comp.enabled = on;

            if (type == collab::CompType::opto)
                st.comp.ratio = 3.0;
        }, false);
    };
    addAndMakeVisible (resetButton);

    if (isEq)
    {
        eqGraph = std::make_unique<EqGraph> (ctx);
        eqGraph->onEdit = [this] (const juce::String& description, std::function<void (collab::ChannelEq&)> fn, bool merge)
        {
            if (! merge || mergeId.isEmpty())
                mergeId = juce::Uuid().toString();

            edit (description, [fn] (collab::ChannelStrip& st) { fn (st.eq); }, true);
        };
        eqGraph->onEditEnd = [this] { ctx.document.endMerge(); mergeId = {}; };
        addAndMakeVisible (*eqGraph);

        eqHint.setText ("点をドラッグ: 周波数・ゲイン　ホイール: Q（LM・M）　ダブルクリック: リセット　LC・HC は端まで動かすとオフ"_ju,
                        juce::dontSendNotification);
        eqHint.setFont (juce::FontOptions (15.0f));
        eqHint.setColour (juce::Label::textColourId, Theme::textDim);
        addAndMakeVisible (eqHint);
        ctx.engine.setSpectrumTrack (trackId);
    }
    else
    {
        compType.addItem ("FET", 1);
        compType.addItem ("Optical", 2);
        compType.onChange = [this]
        {
            const auto type = compType.getSelectedId() == 2 ? collab::CompType::opto : collab::CompType::fet;

            if (auto* t = getTrack(); t != nullptr && t->strip.comp.type != type)
                edit ("Compressor の種類"_ju, [type] (collab::ChannelStrip& st)
                {
                    st.comp.type = type;
                    st.comp.ratio = type == collab::CompType::opto ? 3.0 : 4.0;   // 種類ごとの定番の値に寄せる
                }, false);
        };
        addAndMakeVisible (compType);

        compressor = std::make_unique<CompressorPanel>();
        compressor->onEdit = [this] (const juce::String& description, std::function<void (collab::ChannelComp&)> fn, bool merge)
        {
            if (! merge || mergeId.isEmpty())
                mergeId = juce::Uuid().toString();

            // つまみを触ったら Compressor をオンにする
            edit (description, [fn] (collab::ChannelStrip& st) { fn (st.comp); st.comp.enabled = true; }, true);
        };
        compressor->onEditEnd = [this] { ctx.document.endMerge(); mergeId = {}; };
        addAndMakeVisible (*compressor);
        startTimerHz (30);
    }

    ctx.document.addChangeListener (this);
    update();
    setSize (800, isEq ? 440 : 300);
}

ChannelStripEditor::~ChannelStripEditor()
{
    ctx.document.removeChangeListener (this);

    if (section == Section::eq)
        ctx.engine.setSpectrumTrack ({});
}

void ChannelStripEditor::setTrack (std::string id)
{
    trackId = std::move (id);

    if (section == Section::eq)
        ctx.engine.setSpectrumTrack (trackId);

    update();
}

juce::String ChannelStripEditor::getTitle() const
{
    auto* t = getTrack();
    return (t != nullptr ? toJuce (t->name) : juce::String()) + (section == Section::eq ? " - EQ" : " - Compressor");
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
    const auto arrow = juce::String::fromUTF8 (" \xE2\x86\x92 ");   // →
    orderButton.setButtonText (s.compFirst ? "Comp" + arrow + "EQ" : "EQ" + arrow + "Comp");

    if (eqGraph != nullptr)
    {
        enabled.setToggleState (s.eq.enabled, juce::dontSendNotification);
        eqGraph->setEq (s.eq);
    }

    if (compressor != nullptr)
    {
        enabled.setToggleState (s.comp.enabled, juce::dontSendNotification);
        compType.setSelectedId (s.comp.type == collab::CompType::opto ? 2 : 1, juce::dontSendNotification);
        compressor->setComp (s.comp);
    }

    if (onTitleChanged)
        onTitleChanged();

    repaint();
}

void ChannelStripEditor::timerCallback()
{
    if (compressor != nullptr)
        compressor->setGainReduction (ctx.engine.getTrackGainReductionDb (trackId));
}

void ChannelStripEditor::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);
    g.setColour (Theme::panel);
    g.fillRoundedRectangle (getLocalBounds().reduced (10).toFloat(), 6.0f);
}

void ChannelStripEditor::resized()
{
    auto r = getLocalBounds().reduced (18, 16);
    auto header = r.removeFromTop (30);
    enabled.setBounds (header.removeFromLeft (section == Section::eq ? 80 : 140));

    if (section == Section::comp)
    {
        compType.setBounds (header.removeFromLeft (130).reduced (0, 1));
        header.removeFromLeft (12);
    }

    orderButton.setBounds (header.removeFromLeft (140).reduced (0, 1));
    resetButton.setBounds (header.removeFromRight (100).reduced (0, 1));
    r.removeFromTop (8);

    if (eqGraph != nullptr)
    {
        eqHint.setBounds (r.removeFromBottom (20));
        eqGraph->setBounds (r);
    }

    if (compressor != nullptr)
        compressor->setBounds (r);
}
