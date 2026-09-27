#include "MixerView.h"

#include "Theme.h"

#include <collab/ChannelStripDsp.h>

namespace
{
    constexpr int stripWidth = 118;
    constexpr int headerHeight = 15;
    constexpr int rowHeight = 17;
    constexpr int insertRows = 4, sendRows = 4;
    constexpr float meterFloorDb = -60.0f;
    const std::string metronomeId = "#metronome", masterId = "#master";   // トラック以外のストリップ

    juce::String formatDb (double db)
    {
        return db <= -59.9 ? juce::String ("-inf") : (db > 0.05 ? "+" : "") + juce::String (db, 1);
    }

    /** センドの量（dB）とバーの長さ（0〜1）の対応。左端はオフ（-inf）。 */
    constexpr double sendMinDb = -48.0, sendMaxDb = 6.0;
    double sendDbForRatio (double t)   { return t < 0.02 ? -100.0 : juce::jmap (t, 0.02, 1.0, sendMinDb, sendMaxDb); }
    double sendRatioForDb (double db)  { return db <= sendMinDb ? 0.0 : juce::jmap (db, sendMinDb, sendMaxDb, 0.02, 1.0); }

    void drawPower (juce::Graphics& g, juce::Rectangle<float> r, bool on)
    {
        const auto c = r.withSizeKeepingCentre (9.0f, 9.0f);
        g.setColour (on ? Theme::accent : Theme::gridBar);
        g.fillEllipse (c);

        if (on)
        {
            g.setColour (Theme::accent.withAlpha (0.3f));
            g.fillEllipse (c.expanded (2.0f));
        }
    }
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
        g.setColour (juce::Colour (0xff15171b));
        g.fillRect (r);

        auto toY = [&] (float db) { return juce::jmap (db, meterFloorDb, 6.0f, r.getBottom(), r.getY()); };
        const float y = toY (shown);
        const auto bar = juce::Rectangle<float> (r.getX() + 1.0f, y, r.getWidth() - 2.0f, r.getBottom() - y);

        juce::ColourGradient gradient (juce::Colour (0xffff5252), 0.0f, toY (6.0f), juce::Colour (0xff66bb6a), 0.0f, toY (-18.0f), false);
        gradient.addColour (juce::jmap (-6.0, (double) meterFloorDb, 6.0, 1.0, 0.0), juce::Colour (0xffffd54f));
        g.setGradientFill (gradient);
        g.fillRect (bar);

        g.setColour (peakHold > 0.0f ? juce::Colour (0xffff5252) : Theme::text.withAlpha (0.7f));
        g.fillRect (juce::Rectangle<float> (r.getX() + 1.0f, toY (peakHold), r.getWidth() - 2.0f, 1.5f));
    }

private:
    float shown = meterFloorDb, peakHold = meterFloorDb;
};

//==============================================================================
/** パン（中央から左右へ伸びるバー。L50・C・R50 の表示）。 */
struct PanBar  : public juce::Slider
{
    PanBar() : juce::Slider (juce::Slider::LinearBar, juce::Slider::NoTextBox) {}

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff15171b));
        g.fillRoundedRectangle (r, 2.0f);

        const float centre = r.getCentreX();
        const float x = r.getX() + r.getWidth() * (float) valueToProportionOfLength (getValue());
        g.setColour (Theme::accent.withAlpha (0.6f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (centre, x), r.getY() + 2.0f, juce::jmax (centre, x) + 1.0f, r.getBottom() - 2.0f));

        g.setColour (Theme::gridBar);
        g.drawVerticalLine (juce::roundToInt (centre), r.getY(), r.getBottom());
        g.setColour (Theme::text);
        g.setFont (juce::FontOptions (10.5f));
        g.drawText (getTextFromValue (getValue()), r, juce::Justification::centred);
    }
};

//==============================================================================
/** ストリップの中の 1 区画（見出し ＋ 中身）。見出しの右に電源ボタンを付けられる。 */
class MixSection  : public juce::Component
{
public:
    MixSection (AppContext& c, std::string id, juce::String t) : ctx (c), trackId (std::move (id)), title (std::move (t)) {}

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        auto header = r.removeFromTop (headerHeight);
        g.setColour (Theme::background.withAlpha (0.6f));
        g.fillRect (header);
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        g.drawText (title, header.reduced (4, 0), juce::Justification::centredLeft);

        if (auto on = powerState())
            drawPower (g, header.removeFromRight (16).toFloat(), *on);

        g.setColour (juce::Colour (0xff1b1e22));
        g.fillRect (r);
        paintBody (g, r);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.y < headerHeight)
        {
            if (powerState() && e.x >= getWidth() - 18)
                togglePower();

            return;
        }

        bodyMouseDown (e, e.getPosition().translated (0, -headerHeight));
    }

    /** ドキュメントや状態が変わったとき。 */
    virtual void update()                               { repaint(); }

protected:
    AppContext& ctx;
    std::string trackId;
    juce::String title;

    const collab::Track* track() const                  { return ctx.document.getProject().findTrack (trackId); }
    juce::Rectangle<int> body() const                   { return getLocalBounds().withTrimmedTop (headerHeight); }

    virtual std::optional<bool> powerState() const      { return std::nullopt; }
    virtual void togglePower()                          {}
    virtual void paintBody (juce::Graphics&, juce::Rectangle<int>) {}
    virtual void bodyMouseDown (const juce::MouseEvent&, juce::Point<int>) {}

    void drawRow (juce::Graphics& g, juce::Rectangle<int> row, const juce::String& text, bool active, bool filled)
    {
        g.setColour (filled ? Theme::panelLight : Theme::panel.withAlpha (0.5f));
        g.fillRect (row.reduced (1, 1));
        g.setColour (active ? Theme::text : Theme::textDim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (text, row.reduced (4, 0), juce::Justification::centredLeft, true);
    }

    void editTrack (const juce::String& description, std::function<void (collab::Track&)> fn, const juce::String& mergeId = {})
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
/** インサート（外部プラグイン）。左の丸でバイパス、名前で画面を開く、空き枠で追加、右クリックでメニュー。 */
class InsertSection  : public MixSection
{
public:
    InsertSection (AppContext& c, std::string id) : MixSection (c, std::move (id), "INSERTS") {}

    void paintBody (juce::Graphics& g, juce::Rectangle<int> r) override
    {
        auto* t = track();

        for (int i = 0; i < insertRows; ++i)
        {
            auto row = r.removeFromTop (rowHeight);

            if (t != nullptr && i < (int) t->effects.size())
            {
                auto& e = t->effects[(size_t) i];
                drawRow (g, row.withTrimmedLeft (14), toJuce (e.plugin.name), ! e.bypass, true);
                drawPower (g, row.removeFromLeft (14).toFloat(), ! e.bypass);
            }
            else if (t != nullptr && i == (int) t->effects.size())
            {
                drawRow (g, row, "+", false, false);
            }
            else
            {
                drawRow (g, row, {}, false, false);
            }
        }

        if (t != nullptr && (int) t->effects.size() > insertRows)
        {
            g.setColour (Theme::textDim);
            g.drawText ("+" + juce::String ((int) t->effects.size() - insertRows), r, juce::Justification::centredRight);
        }
    }

    void bodyMouseDown (const juce::MouseEvent& e, juce::Point<int> p) override
    {
        auto* t = track();

        if (t == nullptr)
            return;

        const int i = p.y / rowHeight;

        if (i < (int) t->effects.size())
        {
            const auto effectId = t->effects[(size_t) i].id;

            if (e.mods.isPopupMenu())
            {
                juce::PopupMenu m;
                m.addItem ("画面を開く"_ju, [this, effectId] { if (ctx.openPluginEditor) ctx.openPluginEditor (trackId, effectId); });
                m.addItem ("バイパス"_ju, true, t->effects[(size_t) i].bypass, [this, effectId] { ctx.toggleEffectBypass (trackId, effectId); });
                m.addItem ("削除"_ju, [this, effectId] { ctx.removeEffect (trackId, effectId); });
                m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
            }
            else if (p.x < 14)
                ctx.toggleEffectBypass (trackId, effectId);
            else if (ctx.openPluginEditor)
                ctx.openPluginEditor (trackId, effectId);
        }
        else
        {
            ctx.addEffectMenu (trackId).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
        }
    }
};

//==============================================================================
/** EQ の小さなグラフ。クリックで EQ 画面、見出しの丸でオン・オフ。 */
class EqSection  : public MixSection
{
public:
    EqSection (AppContext& c, std::string id) : MixSection (c, std::move (id), "EQ") {}

    std::optional<bool> powerState() const override
    {
        if (auto* t = track())
            return t->strip.eq.enabled;

        return std::nullopt;
    }

    void togglePower() override
    {
        editTrack ("EQ のオン・オフ"_ju, [] (collab::Track& t) { t.strip.eq.enabled = ! t.strip.eq.enabled; });
    }

    void paintBody (juce::Graphics& g, juce::Rectangle<int> area) override
    {
        auto* t = track();

        if (t == nullptr)
            return;

        const auto r = area.reduced (3).toFloat();
        g.setColour (Theme::gridBeat);
        g.drawHorizontalLine (juce::roundToInt (r.getCentreY()), r.getX(), r.getRight());

        auto eq = t->strip.eq;
        eq.enabled = true;
        juce::Path curve;
        bool first = true;

        for (float x = r.getX(); x <= r.getRight(); x += 1.0f)
        {
            const double hz = 20.0 * std::pow (1000.0, (x - r.getX()) / r.getWidth());
            const double db = juce::jlimit (-18.0, 18.0, collab::eqResponseDb (eq, 48000.0, hz));
            const float y = r.getCentreY() - (float) (db / 18.0) * r.getHeight() * 0.5f;

            if (first)
                curve.startNewSubPath (x, y);
            else
                curve.lineTo (x, y);

            first = false;
        }

        g.setColour (t->strip.eq.enabled ? Theme::selection : Theme::textDim.withAlpha (0.5f));
        g.strokePath (curve, juce::PathStrokeType (1.5f));
    }

    void bodyMouseDown (const juce::MouseEvent&, juce::Point<int>) override
    {
        if (ctx.openChannelStrip)
            ctx.openChannelStrip (trackId);
    }
};

//==============================================================================
/** コンプ（種類・スレッショルドとゲインリダクション）。クリックで画面、見出しの丸でオン・オフ。 */
class CompSection  : public MixSection
{
public:
    CompSection (AppContext& c, std::string id) : MixSection (c, std::move (id), "COMP") {}

    float gainReduction = 0.0f;

    std::optional<bool> powerState() const override
    {
        if (auto* t = track())
            return t->strip.comp.enabled;

        return std::nullopt;
    }

    void togglePower() override
    {
        editTrack ("コンプのオン・オフ"_ju, [] (collab::Track& t) { t.strip.comp.enabled = ! t.strip.comp.enabled; });
    }

    void paintBody (juce::Graphics& g, juce::Rectangle<int> r) override
    {
        auto* t = track();

        if (t == nullptr)
            return;

        const auto& c = t->strip.comp;
        auto row = r.removeFromTop (rowHeight);
        drawRow (g, row, (c.type == collab::CompType::opto ? "OPTO " : "FET ") + juce::String (juce::roundToInt (c.thresholdDb)) + " dB",
                 c.enabled, true);

        // ゲインリダクション（右から左へ伸びる）
        auto bar = r.reduced (3, 2).toFloat();
        g.setColour (juce::Colour (0xff15171b));
        g.fillRect (bar);
        g.setColour (juce::Colour (0xffffb74d));
        g.fillRect (bar.withLeft (bar.getRight() - bar.getWidth() * juce::jlimit (0.0f, 1.0f, gainReduction / 20.0f)));
    }

    void bodyMouseDown (const juce::MouseEvent&, juce::Point<int>) override
    {
        if (ctx.openChannelStrip)
            ctx.openChannelStrip (trackId);
    }
};

//==============================================================================
/** センド（バスへ送る量）。バーを左右にドラッグで量、ダブルクリックで 0 dB、右クリックでプリ/ポスト・外す、空き枠で追加。 */
class SendSection  : public MixSection
{
public:
    SendSection (AppContext& c, std::string id) : MixSection (c, std::move (id), "SENDS") {}

    void paintBody (juce::Graphics& g, juce::Rectangle<int> r) override
    {
        auto* t = track();
        const auto& project = ctx.document.getProject();

        for (int i = 0; i < sendRows; ++i)
        {
            auto row = r.removeFromTop (rowHeight);

            if (t != nullptr && i < (int) t->sends.size())
            {
                auto& s = t->sends[(size_t) i];
                auto* bus = project.findTrack (s.busId);
                auto inner = row.reduced (1, 1).toFloat();

                g.setColour (Theme::panelLight);
                g.fillRect (inner);
                g.setColour ((bus != nullptr ? Theme::parseColour (bus->color) : Theme::accent).withAlpha (0.55f));
                g.fillRect (inner.withWidth (inner.getWidth() * (float) sendRatioForDb (s.levelDb)));

                g.setColour (Theme::text);
                g.setFont (juce::FontOptions (10.5f));
                auto text = inner.reduced (3.0f, 0.0f);
                g.drawText (formatDb (s.levelDb), text, juce::Justification::centredRight);
                g.drawText ((s.preFader ? "PRE " : "") + (bus != nullptr ? toJuce (bus->name) : juce::String ("?")),
                            text.withTrimmedRight (30.0f), juce::Justification::centredLeft, true);
            }
            else if (t != nullptr && i == (int) t->sends.size())
            {
                drawRow (g, row, "+", false, false);
            }
            else
            {
                drawRow (g, row, {}, false, false);
            }
        }
    }

    void bodyMouseDown (const juce::MouseEvent& e, juce::Point<int> p) override
    {
        auto* t = track();
        dragBus.clear();

        if (t == nullptr)
            return;

        const int i = p.y / rowHeight;

        if (i >= (int) t->sends.size())
        {
            ctx.sendMenu (trackId).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
            return;
        }

        const auto& s = t->sends[(size_t) i];

        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu m;
            m.addItem ("プリフェーダー（音量の前から送る）"_ju, true, s.preFader,
                       [this, id = s.busId, pre = s.preFader] { ctx.setSend (trackId, id, std::nullopt, ! pre); });
            m.addItem ("センドを外す"_ju, [this, id = s.busId] { ctx.removeSend (trackId, id); });
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
            return;
        }

        dragBus = s.busId;
        mergeId = juce::Uuid().toString();
        setLevelFromX (e.x);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragBus.empty())
            setLevelFromX (e.x);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (! dragBus.empty())
            ctx.document.endMerge();

        dragBus.clear();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        auto* t = track();
        const int i = (e.y - headerHeight) / rowHeight;

        if (t != nullptr && e.y >= headerHeight && i < (int) t->sends.size())
            ctx.setSend (trackId, t->sends[(size_t) i].busId, 0.0, std::nullopt);
    }

private:
    std::string dragBus;
    juce::String mergeId;

    void setLevelFromX (int x)
    {
        const double t = juce::jlimit (0.0, 1.0, (x - 1.0) / (getWidth() - 2.0));
        ctx.setSend (trackId, dragBus, std::round (sendDbForRatio (t) * 10.0) / 10.0, std::nullopt, mergeId);
    }
};

//==============================================================================
/** 1 トラック分（trackId が空ならコードトラック、#metronome / #master はメトロノームとマスター）。 */
class MixerView::Strip  : public juce::Component
{
public:
    Strip (AppContext& c, std::string id)
        : ctx (c), trackId (std::move (id)),
          inserts (c, trackId), eq (c, trackId), comp (c, trackId), sends (c, trackId)
    {
        routingTitle.setText ("ROUTING", juce::dontSendNotification);
        routingTitle.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        routingTitle.setColour (juce::Label::textColourId, Theme::textDim);
        routingTitle.setColour (juce::Label::backgroundColourId, Theme::background.withAlpha (0.6f));
        addAndMakeVisible (routingTitle);

        output.setTooltip ("出力先（マスターかバス）"_ju);
        output.onClick = [this] { ctx.outputMenu (trackId).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&output)); };
        output.setEnabled (isTrack());
        addAndMakeVisible (output);

        for (auto* s : std::initializer_list<MixSection*> { &inserts, &eq, &comp, &sends })
            addChildComponent (s);

        inserts.setVisible (isTrack());
        eq.setVisible (isTrack());
        comp.setVisible (isTrack());
        sends.setVisible (isTrack());

        pan.setRange (-1.0, 1.0, 0.01);
        pan.setDoubleClickReturnValue (true, 0.0);
        pan.textFromValueFunction = [] (double v)
        {
            const int n = juce::roundToInt (std::abs (v) * 100.0);
            return n == 0 ? juce::String ("C") : (v < 0 ? "L" : "R") + juce::String (n);
        };
        pan.setTooltip ("パン（ダブルクリックで中央）"_ju);
        pan.setVisible (isTrack());
        addChildComponent (pan);

        fader.setSliderStyle (juce::Slider::LinearVertical);
        fader.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        fader.setRange (-60.0, 6.0, 0.1);
        fader.setSkewFactorFromMidPoint (-12.0);
        fader.setDoubleClickReturnValue (true, isChord() || isMetronome() ? -6.0 : 0.0);
        fader.setTooltip ("音量（ダブルクリックで既定値）"_ju);
        addAndMakeVisible (fader);

        value.setJustificationType (juce::Justification::centred);
        value.setFont (juce::FontOptions (11.0f));
        value.setColour (juce::Label::backgroundColourId, juce::Colour (0xff15171b));
        addAndMakeVisible (value);
        addAndMakeVisible (meter);

        mute.setButtonText (isChord() ? "発音"_ju : isMetronome() ? "オン"_ju : juce::String ("M"));
        mute.setTooltip (isChord() ? "コードトラックを鳴らす"_ju : isMetronome() ? "メトロノームを鳴らす（C）"_ju : "ミュート"_ju);
        mute.setColour (juce::TextButton::buttonOnColourId, isChord() || isMetronome() ? Theme::accent.darker (0.3f) : juce::Colour (0xffe57373));
        solo.setButtonText ("S");
        solo.setTooltip ("ソロ"_ju);
        solo.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffffd54f).darker (0.2f));
        mute.setVisible (! isMaster());
        addChildComponent (mute);
        solo.setVisible (isTrack());
        addChildComponent (solo);

        for (auto* s : std::initializer_list<juce::Slider*> { &fader, &pan })
        {
            s->onDragStart = [this] { mergeId = juce::Uuid().toString(); };
            s->onDragEnd = [this] { ctx.document.endMerge(); mergeId = {}; };
        }

        fader.onValueChange = [this]
        {
            const double v = fader.getValue();
            value.setText (formatDb (v), juce::dontSendNotification);

            if (isMetronome())
            {
                ctx.state.metronomeVolumeDb = (float) v;
                ctx.state.changed();
            }
            else if (isMaster())
            {
                ctx.state.masterVolumeDb = (float) v;
                ctx.state.changed();
            }
            else if (isChord())
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
            if (isMetronome())
            {
                ctx.state.metronomeEnabled = ! ctx.state.metronomeEnabled;
                ctx.state.changed();
            }
            else if (isChord())
                ctx.document.perform ("コードトラックの発音"_ju, [] (collab::Project& p) { p.chordTrack.playback.enabled = ! p.chordTrack.playback.enabled; });
            else
                editTrack ("ミュート"_ju, [] (collab::Track& t) { t.mute = ! t.mute; });
        };

        solo.onClick = [this] { editTrack ("ソロ"_ju, [] (collab::Track& t) { t.solo = ! t.solo; }); };

        update();
    }

    const std::string& getTrackId() const noexcept     { return trackId; }
    bool isChord() const noexcept                      { return trackId.empty(); }
    bool isMetronome() const noexcept                  { return trackId == metronomeId; }
    bool isMaster() const noexcept                     { return trackId == masterId; }
    bool isTrack() const noexcept                      { return ! isChord() && ! isMetronome() && ! isMaster(); }

    void update()
    {
        const auto& project = ctx.document.getProject();

        if (isMetronome())
        {
            name = "メトロノーム"_ju;
            number = {};
            detail = "この PC だけ"_ju;
            output.setButtonText ("マスター"_ju);
            colour = Theme::textDim;
            fader.setValue (ctx.state.metronomeVolumeDb, juce::dontSendNotification);
            mute.setToggleState (ctx.state.metronomeEnabled, juce::dontSendNotification);
        }
        else if (isMaster())
        {
            name = "マスター"_ju;
            number = {};
            detail = "この PC だけ"_ju;
            output.setButtonText ("オーディオ出力"_ju);
            colour = Theme::text;
            fader.setValue (ctx.state.masterVolumeDb, juce::dontSendNotification);
        }
        else if (isChord())
        {
            name = "コード"_ju;
            number = {};
            detail = "ピアノ"_ju;
            output.setButtonText ("マスター"_ju);
            colour = juce::Colour (0xffffb74d);
            fader.setValue (project.chordTrack.playback.volumeDb, juce::dontSendNotification);
            mute.setToggleState (project.chordTrack.playback.enabled, juce::dontSendNotification);
        }
        else if (auto* t = project.findTrack (trackId))
        {
            name = toJuce (t->name);
            number = juce::String (project.indexOfTrack (trackId) + 1);
            detail = t->type == collab::TrackType::audio ? "オーディオ"_ju
                   : t->type == collab::TrackType::bus   ? "バス"_ju
                                                         : instrumentName (*t);
            output.setButtonText (ctx.outputName (*t));
            colour = Theme::parseColour (t->color);
            fader.setValue (t->volumeDb, juce::dontSendNotification);
            pan.setValue (t->pan, juce::dontSendNotification);
            mute.setToggleState (t->mute, juce::dontSendNotification);
            solo.setToggleState (t->solo, juce::dontSendNotification);
        }

        for (auto* s : std::initializer_list<MixSection*> { &inserts, &eq, &comp, &sends })
            s->update();

        value.setText (formatDb (fader.getValue()), juce::dontSendNotification);
        repaint();
    }

    void updateMeter()
    {
        meter.push (isMetronome() ? ctx.engine.getMetronomePeakDb()
                    : isMaster()  ? ctx.engine.getMasterPeakDb()
                                  : ctx.engine.getTrackPeakDb (trackId));

        if (isTrack())
        {
            const float gr = ctx.engine.getTrackGainReductionDb (trackId);

            if (std::abs (gr - comp.gainReduction) > 0.05f)
            {
                comp.gainReduction = gr;
                comp.repaint();
            }
        }
    }

    void paint (juce::Graphics& g) override
    {
        const bool selected = isTrack() && ctx.state.selectedTrackId == trackId;
        g.setColour (selected ? Theme::panelLight : Theme::panel);
        g.fillRect (getLocalBounds().reduced (1, 0));
        g.setColour (colour);
        g.fillRect (getLocalBounds().reduced (1, 0).removeFromTop (3));

        // フェーダーの目盛り
        g.setFont (juce::FontOptions (9.0f));

        for (double db : { 6.0, 0.0, -6.0, -12.0, -24.0, -36.0, -60.0 })
        {
            const int y = fader.getY() + juce::roundToInt (fader.getPositionOfValue (db));
            g.setColour (Theme::gridBar);
            g.drawHorizontalLine (y, (float) scaleArea.getRight() - 4.0f, (float) scaleArea.getRight());
            g.setColour (Theme::textDim);
            g.drawText (db <= -59.9 ? juce::String ("-inf") : juce::String ((int) db), scaleArea.withY (y - 6).withHeight (12).withTrimmedRight (5),
                        juce::Justification::centredRight);
        }

        // 下の名前（番号と色）
        g.setColour (colour.withAlpha (0.85f));
        g.fillRoundedRectangle (nameArea.toFloat(), 3.0f);
        const auto textColour = colour.getPerceivedBrightness() > 0.55f ? juce::Colours::black : juce::Colours::white;
        auto r = nameArea.reduced (4, 0);
        g.setColour (textColour);

        if (number.isNotEmpty())
        {
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText (number, r.removeFromLeft (16), juce::Justification::centredLeft);
        }

        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (name, r, juce::Justification::centred, true);

        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (10.0f));
        g.drawText (detail, detailArea, juce::Justification::centred, true);

        g.setColour (Theme::background);
        g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (4, 0);
        area.removeFromTop (5);

        routingTitle.setBounds (area.removeFromTop (headerHeight));
        output.setBounds (area.removeFromTop (20).reduced (1, 1));
        area.removeFromTop (3);

        inserts.setBounds (area.removeFromTop (headerHeight + rowHeight * insertRows + 2));
        area.removeFromTop (3);
        eq.setBounds (area.removeFromTop (headerHeight + 36));
        area.removeFromTop (3);
        comp.setBounds (area.removeFromTop (headerHeight + rowHeight + 8));
        area.removeFromTop (3);
        sends.setBounds (area.removeFromTop (headerHeight + rowHeight * sendRows + 2));
        area.removeFromTop (5);

        pan.setBounds (area.removeFromTop (18));
        area.removeFromTop (5);

        nameArea = area.removeFromBottom (22);
        area.removeFromBottom (2);
        detailArea = area.removeFromBottom (14);
        area.removeFromBottom (3);

        auto buttons = area.removeFromBottom (22);

        if (solo.isVisible())
        {
            mute.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2 - 2));
            buttons.removeFromLeft (4);
            solo.setBounds (buttons);
        }
        else if (mute.isVisible())
        {
            mute.setBounds (buttons);
        }

        area.removeFromBottom (4);
        value.setBounds (area.removeFromBottom (16).reduced (8, 0));
        area.removeFromBottom (4);

        meter.setBounds (area.removeFromRight (10));
        area.removeFromRight (4);
        scaleArea = area.removeFromLeft (30);
        fader.setBounds (area);
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        if (isTrack())
        {
            ctx.state.selectedTrackId = trackId;
            ctx.state.changed();
        }
    }

private:
    AppContext& ctx;
    std::string trackId;
    juce::Label routingTitle;
    juce::TextButton output;
    InsertSection inserts;
    EqSection eq;
    CompSection comp;
    SendSection sends;
    juce::Label value;
    PanBar pan;
    juce::Slider fader;
    juce::TextButton mute, solo;
    LevelMeter meter;
    juce::Colour colour = Theme::accent;
    juce::String name, number, detail, mergeId;
    juce::Rectangle<int> scaleArea, nameArea, detailArea;

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
    addBusButton.setButtonText ("+ バスを追加"_ju);
    addBusButton.setTooltip ("バストラック（グループ・FX）を追加する。トラックの出力先やセンドの送り先にできます"_ju);
    addBusButton.onClick = [this] { ctx.addBusTrack ("Bus"_ju); };
    addAndMakeVisible (addBusButton);

    hint.setText ("INSERTS・SENDS の空き枠（+）をクリックで追加、右クリックでメニュー。センドは左右にドラッグで量"_ju, juce::dontSendNotification);
    hint.setFont (juce::FontOptions (11.0f));
    hint.setColour (juce::Label::textColourId, Theme::textDim);
    addAndMakeVisible (hint);

    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (false, true);
    addAndMakeVisible (viewport);

    ctx.document.addChangeListener (this);
    ctx.state.addChangeListener (this);
    rebuild();
    startTimerHz (30);
    setSize (stripWidth * 7, 700);
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
    auto area = getLocalBounds();
    auto top = area.removeFromTop (30).reduced (6, 4);
    addBusButton.setBounds (top.removeFromLeft (110));
    top.removeFromLeft (10);
    hint.setBounds (top);

    viewport.setBounds (area);
    const int height = viewport.getHeight() - (content.getWidth() > viewport.getWidth() ? viewport.getScrollBarThickness() : 0);
    content.setSize (juce::jmax (viewport.getWidth(), strips.size() * stripWidth), juce::jmax (560, height));

    for (int i = 0; i < strips.size(); ++i)
        strips[i]->setBounds (i * stripWidth, 0, stripWidth, content.getHeight());
}

void MixerView::rebuild()
{
    std::vector<std::string> ids { std::string() };   // 先頭はコードトラック

    for (auto& t : ctx.document.getProject().tracks)
        ids.push_back (t.id);

    ids.push_back (metronomeId);   // 右端にメトロノームとマスター
    ids.push_back (masterId);

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
