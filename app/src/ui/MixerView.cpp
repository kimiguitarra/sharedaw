#include "MixerView.h"

#include "Theme.h"

namespace
{
    constexpr int stripWidth = 96;
    constexpr float meterFloorDb = -60.0f;
}

//==============================================================================
/** レベルメーター（ピークを少しずつ下げて表示する）。 */
class LevelMeter  : public juce::Component
{
public:
    void push (float db)
    {
        const float level = juce::jlimit (meterFloorDb, 6.0f, db);
        shown = juce::jmax (level, shown - 1.5f);
        peakHold = level >= peakHold ? level : juce::jmax (meterFloorDb, peakHold - 0.3f);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Theme::background);
        g.fillRoundedRectangle (r, 2.0f);

        auto toY = [&] (float db) { return juce::jmap (db, meterFloorDb, 6.0f, r.getBottom(), r.getY()); };
        const float y = toY (shown);
        const auto bar = juce::Rectangle<float> (r.getX() + 1.0f, y, r.getWidth() - 2.0f, r.getBottom() - y);

        juce::ColourGradient gradient (juce::Colour (0xffff5252), 0.0f, toY (6.0f), juce::Colour (0xff66bb6a), 0.0f, toY (-18.0f), false);
        gradient.addColour (juce::jmap (-6.0, (double) meterFloorDb, 6.0, 1.0, 0.0), juce::Colour (0xffffd54f));
        g.setGradientFill (gradient);
        g.fillRect (bar);

        g.setColour (peakHold > 0.0f ? juce::Colour (0xffff5252) : Theme::text.withAlpha (0.7f));
        g.fillRect (juce::Rectangle<float> (r.getX() + 1.0f, toY (peakHold), r.getWidth() - 2.0f, 1.5f));

        g.setColour (Theme::gridBar);
        g.drawHorizontalLine ((int) toY (0.0f), r.getX(), r.getRight());
    }

private:
    float shown = meterFloorDb, peakHold = meterFloorDb;
};

//==============================================================================
/** 1 トラック分（trackId が空ならコードトラック）。 */
class MixerView::Strip  : public juce::Component
{
public:
    Strip (AppContext& c, std::string id) : ctx (c), trackId (std::move (id))
    {
        name.setJustificationType (juce::Justification::centred);
        name.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        addAndMakeVisible (name);

        detail.setJustificationType (juce::Justification::centred);
        detail.setFont (juce::FontOptions (10.5f));
        detail.setColour (juce::Label::textColourId, Theme::textDim);
        addAndMakeVisible (detail);

        pan.setSliderStyle (juce::Slider::LinearHorizontal);
        pan.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        pan.setRange (-1.0, 1.0, 0.01);
        pan.setDoubleClickReturnValue (true, 0.0);
        pan.setPopupDisplayEnabled (true, true, nullptr);
        pan.setTooltip ("パン（ダブルクリックで中央）"_ju);
        pan.setVisible (! isChord());
        addChildComponent (pan);

        fader.setSliderStyle (juce::Slider::LinearVertical);
        fader.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        fader.setRange (-60.0, 6.0, 0.1);
        fader.setSkewFactorFromMidPoint (-12.0);
        fader.setDoubleClickReturnValue (true, isChord() ? -6.0 : 0.0);
        fader.setTooltip ("音量（ダブルクリックで既定値）"_ju);
        addAndMakeVisible (fader);

        value.setJustificationType (juce::Justification::centred);
        value.setFont (juce::FontOptions (11.0f));
        addAndMakeVisible (value);
        addAndMakeVisible (meter);

        mute.setButtonText (isChord() ? "発音"_ju : juce::String ("M"));
        mute.setTooltip (isChord() ? "コードトラックを鳴らす"_ju : "ミュート"_ju);
        mute.setColour (juce::TextButton::buttonOnColourId, isChord() ? Theme::accent.darker (0.3f) : juce::Colour (0xffe57373));
        solo.setButtonText ("S");
        solo.setTooltip ("ソロ"_ju);
        solo.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffffd54f).darker (0.2f));
        addAndMakeVisible (mute);
        solo.setVisible (! isChord());
        addChildComponent (solo);

        for (auto* s : { &fader, &pan })
        {
            s->onDragStart = [this] { mergeId = juce::Uuid().toString(); };
            s->onDragEnd = [this] { ctx.document.endMerge(); mergeId = {}; };
        }

        fader.onValueChange = [this]
        {
            const double v = fader.getValue();
            value.setText (formatDb (v), juce::dontSendNotification);

            if (isChord())
                ctx.document.perform ("コードトラックの音量"_ju, [v] (collab::Project& p) { p.chordTrack.playback.volumeDb = v; }, mergeId);
            else
                editTrack ("音量"_ju, [v] (collab::Track& t) { t.volumeDb = v; });
        };

        pan.onValueChange = [this]
        {
            const double v = pan.getValue();
            editTrack ("パン"_ju, [v] (collab::Track& t) { t.pan = v; });
        };

        mute.onClick = [this]
        {
            if (isChord())
                ctx.document.perform ("コードトラックの発音"_ju, [] (collab::Project& p) { p.chordTrack.playback.enabled = ! p.chordTrack.playback.enabled; });
            else
                editTrack ("ミュート"_ju, [] (collab::Track& t) { t.mute = ! t.mute; });
        };

        solo.onClick = [this] { editTrack ("ソロ"_ju, [] (collab::Track& t) { t.solo = ! t.solo; }); };

        // EQ・コンプ（クリックで画面を開く。点灯はオン）
        eq.setButtonText ("EQ");
        comp.setButtonText ("COMP");
        eq.setTooltip ("EQ（クリックで開く）"_ju);
        comp.setTooltip ("コンプ（クリックで開く）"_ju);

        for (auto* b : { &eq, &comp })
        {
            b->setClickingTogglesState (false);
            b->setColour (juce::TextButton::buttonOnColourId, Theme::accent.darker (0.2f));
            b->onClick = [this] { if (ctx.openChannelStrip) ctx.openChannelStrip (trackId); };
            b->setVisible (! isChord());
            addChildComponent (b);
        }

        update();
    }

    const std::string& getTrackId() const noexcept     { return trackId; }
    bool isChord() const noexcept                      { return trackId.empty(); }

    void update()
    {
        const auto& project = ctx.document.getProject();

        if (isChord())
        {
            name.setText ("コード"_ju, juce::dontSendNotification);
            detail.setText ("コードトラック（ピアノ）"_ju, juce::dontSendNotification);
            colour = juce::Colour (0xffffb74d);
            fader.setValue (project.chordTrack.playback.volumeDb, juce::dontSendNotification);
            mute.setToggleState (project.chordTrack.playback.enabled, juce::dontSendNotification);
        }
        else if (auto* t = project.findTrack (trackId))
        {
            name.setText (toJuce (t->name), juce::dontSendNotification);
            detail.setText (t->type == collab::TrackType::audio ? "オーディオ"_ju : instrumentName (*t), juce::dontSendNotification);
            colour = Theme::parseColour (t->color);
            fader.setValue (t->volumeDb, juce::dontSendNotification);
            pan.setValue (t->pan, juce::dontSendNotification);
            mute.setToggleState (t->mute, juce::dontSendNotification);
            solo.setToggleState (t->solo, juce::dontSendNotification);
            eq.setToggleState (t->strip.eq.enabled, juce::dontSendNotification);
            comp.setToggleState (t->strip.comp.enabled, juce::dontSendNotification);
            comp.setButtonText (t->strip.comp.enabled ? (t->strip.comp.type == collab::CompType::opto ? "OPTO" : "FET") : "COMP");
        }

        value.setText (formatDb (fader.getValue()), juce::dontSendNotification);
        repaint();
    }

    void updateMeter()      { meter.push (ctx.engine.getTrackPeakDb (trackId)); }

    void paint (juce::Graphics& g) override
    {
        const bool selected = ! isChord() && ctx.state.selectedTrackId == trackId;
        g.setColour (selected ? Theme::panelLight : Theme::panel);
        g.fillRect (getLocalBounds().reduced (1, 0));
        g.setColour (colour);
        g.fillRect (getLocalBounds().reduced (1, 0).removeFromTop (4));
        g.setColour (Theme::background);
        g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (6, 6);
        area.removeFromTop (2);
        name.setBounds (area.removeFromTop (20));
        detail.setBounds (area.removeFromTop (16));
        area.removeFromTop (6);

        if (eq.isVisible())
        {
            auto row = area.removeFromTop (22);
            eq.setBounds (row.removeFromLeft (row.getWidth() / 2 - 2));
            row.removeFromLeft (4);
            comp.setBounds (row);
            area.removeFromTop (4);
        }

        if (pan.isVisible())
            pan.setBounds (area.removeFromTop (22));

        area.removeFromTop (6);
        auto buttons = area.removeFromBottom (24);

        if (solo.isVisible())
        {
            mute.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2 - 2));
            buttons.removeFromLeft (4);
            solo.setBounds (buttons);
        }
        else
        {
            mute.setBounds (buttons);
        }

        area.removeFromBottom (6);
        value.setBounds (area.removeFromBottom (18));
        area.removeFromBottom (4);

        meter.setBounds (area.removeFromRight (12));
        area.removeFromRight (6);
        fader.setBounds (area);
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        if (! isChord())
        {
            ctx.state.selectedTrackId = trackId;
            ctx.state.changed();
        }
    }

private:
    AppContext& ctx;
    std::string trackId;
    juce::Label name, detail, value;
    juce::Slider pan, fader;
    juce::TextButton mute, solo, eq, comp;
    LevelMeter meter;
    juce::Colour colour = Theme::accent;
    juce::String mergeId;

    static juce::String formatDb (double db)
    {
        return db <= -59.9 ? juce::String ("-inf dB") : juce::String (db, 1) + " dB";
    }

    juce::String instrumentName (const collab::Track& t) const
    {
        if (! t.instrument)
            return "音源なし"_ju;

        if (t.instrument->kind == collab::Instrument::Kind::external)
            return toJuce (t.instrument->plugin.name);

        auto* m = ctx.library.find (t.instrument->id, t.instrument->version);
        return m != nullptr ? toJuce (m->displayName) : toJuce (t.instrument->id);
    }

    void editTrack (const juce::String& description, std::function<void (collab::Track&)> fn)
    {
        auto id = trackId;
        ctx.document.perform (description, [id, fn] (collab::Project& p)
        {
            if (auto* t = p.findTrack (id))
                fn (*t);
        }, mergeId);
    }
};

//==============================================================================
MixerView::MixerView (AppContext& c) : ctx (c)
{
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (false, true);
    addAndMakeVisible (viewport);

    ctx.document.addChangeListener (this);
    ctx.state.addChangeListener (this);
    rebuild();
    startTimerHz (30);
    setSize (stripWidth * 6, 420);
}

MixerView::~MixerView()
{
    ctx.document.removeChangeListener (this);
    ctx.state.removeChangeListener (this);
}

void MixerView::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);
}

void MixerView::resized()
{
    viewport.setBounds (getLocalBounds());
    const int height = viewport.getHeight() - (content.getWidth() > viewport.getWidth() ? viewport.getScrollBarThickness() : 0);
    content.setSize (juce::jmax (viewport.getWidth(), strips.size() * stripWidth), juce::jmax (260, height));

    for (int i = 0; i < strips.size(); ++i)
        strips[i]->setBounds (i * stripWidth, 0, stripWidth, content.getHeight());
}

void MixerView::rebuild()
{
    std::vector<std::string> ids { std::string() };   // 先頭はコードトラック

    for (auto& t : ctx.document.getProject().tracks)
        ids.push_back (t.id);

    bool same = (int) ids.size() == strips.size();

    for (int i = 0; same && i < strips.size(); ++i)
        same = strips[i]->getTrackId() == ids[(size_t) i];

    if (! same)
    {
        strips.clear();

        for (auto& id : ids)
            content.addAndMakeVisible (strips.add (new Strip (ctx, id)));

        resized();
    }

    for (auto* s : strips)
        s->update();
}

void MixerView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    rebuild();
}

void MixerView::timerCallback()
{
    for (auto* s : strips)
        s->updateMeter();
}
