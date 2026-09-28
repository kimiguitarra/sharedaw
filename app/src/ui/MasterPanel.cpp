#include "MasterPanel.h"

#include "Theme.h"

namespace
{
    // ヴィンテージ系: 炭色のパネルに琥珀色
    const juce::Colour panelTop { 0xff2a2724 }, panelBottom { 0xff171514 };
    const juce::Colour amber { 0xffffb347 }, amberDim { 0xff9c6a2a }, cream { 0xffeee3cf };
    const juce::Colour good { 0xff66bb6a }, over { 0xffef5350 };

    constexpr float meterFloor = -30.0f;   // IN / OUT メーターの下端
    constexpr double lufsMin = -30.0, lufsMax = -6.0;

    const char* modeNames[3] = { "ANALOG", "TUBE", "MODERN" };
    const collab::LimiterMode modes[3] = { collab::LimiterMode::analog, collab::LimiterMode::tube, collab::LimiterMode::modern };

    juce::String formatLufs (double v)    { return v <= -99.0 ? juce::String ("--.-") : juce::String (v, 1); }
}

//==============================================================================
/** THRESHOLD / CEILING のつまみ（メーターの横の三角）と CHARACTER のつまみ。 */
class MasterPanel::Look  : public juce::LookAndFeel_V4
{
public:
    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float, float,
                           juce::Slider::SliderStyle, juce::Slider& s) override
    {
        auto r = juce::Rectangle<int> (x, y, width, height).toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (r.withSizeKeepingCentre (4.0f, r.getHeight()), 2.0f);

        // 三角の印（左右どちらのメーターを指すかは slider の "pointLeft"）
        const bool left = (bool) s.getProperties()["pointLeft"];
        juce::Path tri;
        const float cx = r.getCentreX();

        if (left)
            tri.addTriangle (cx - 9.0f, sliderPos, cx + 7.0f, sliderPos - 7.0f, cx + 7.0f, sliderPos + 7.0f);
        else
            tri.addTriangle (cx + 9.0f, sliderPos, cx - 7.0f, sliderPos - 7.0f, cx - 7.0f, sliderPos + 7.0f);

        g.setColour (s.isEnabled() ? amber : amberDim);
        g.fillPath (tri);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                           float startAngle, float endAngle, juce::Slider&) override
    {
        auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (6.0f);
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
        const auto centre = bounds.getCentre();
        const float angle = startAngle + pos * (endAngle - startAngle);

        juce::Path track, value;
        track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
        value.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, angle, true);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.strokePath (track, juce::PathStrokeType (4.0f));
        g.setColour (amber);
        g.strokePath (value, juce::PathStrokeType (4.0f));

        const auto knob = juce::Rectangle<float> (radius * 1.5f, radius * 1.5f).withCentre (centre);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a4540), knob.getX(), knob.getY(),
                                                 juce::Colour (0xff141210), knob.getRight(), knob.getBottom(), false));
        g.fillEllipse (knob);
        g.setColour (cream);
        g.drawLine ({ centre.getPointOnCircumference (radius * 0.2f, angle), centre.getPointOnCircumference (radius * 0.7f, angle) }, 2.5f);
    }
};

//==============================================================================
MasterPanel::MasterPanel (AppContext& c) : look (std::make_unique<Look>()), ctx (c)
{
    enabled.setColour (juce::ToggleButton::tickColourId, amber);
    enabled.setColour (juce::ToggleButton::textColourId, cream);
    enabled.onClick = [this]
    {
        const bool on = enabled.getToggleState();
        edit (on ? "マスターのリミッターをオン"_ju : "マスターのリミッターをオフ"_ju, [on] (auto& l) { l.enabled = on; }, false);
    };
    addAndMakeVisible (enabled);

    resetLimiter.onClick = [this]
    {
        edit ("マスターのリミッターをリセット"_ju, [] (auto& l) { const bool on = l.enabled; l = {}; l.enabled = on; }, false);
    };
    addAndMakeVisible (resetLimiter);

    auto setupSlider = [this] (juce::Slider& s, double min, double max, double reset, bool pointLeft, const juce::String& description,
                               std::function<void (collab::MasterLimiter&, double)> apply)
    {
        s.setLookAndFeel (look.get());
        s.setRange (min, max, 0.1);
        s.setDoubleClickReturnValue (true, reset);
        s.setPopupDisplayEnabled (true, true, this);
        s.setTextValueSuffix (" dB");
        s.getProperties().set ("pointLeft", pointLeft);
        s.onDragStart = [this] { mergeId = juce::Uuid().toString(); };
        s.onDragEnd = [this] { ctx.document.endMerge(); mergeId = {}; };
        s.onValueChange = [this, &s, description, apply]
        {
            const double v = s.getValue();
            edit (description, [apply, v] (auto& l) { apply (l, v); l.enabled = true; }, mergeId.isNotEmpty());
            repaint();
        };
        addAndMakeVisible (s);
    };

    setupSlider (threshold, -20.0, 0.0, 0.0, true, "マスターのリミッター THRESHOLD"_ju, [] (auto& l, double v) { l.thresholdDb = v; });
    setupSlider (ceiling, -6.0, 0.0, -1.0, false, "マスターのリミッター CEILING"_ju, [] (auto& l, double v) { l.ceilingDb = v; });
    threshold.setTooltip ("THRESHOLD: 下げるほど音が大きくなり、強くかかる（ダブルクリックで 0 dB）"_ju);
    ceiling.setTooltip ("CEILING: 出力の上限（ダブルクリックで -1.0 dB。配信向けは -1.0 dB 前後）"_ju);

    character.setLookAndFeel (look.get());
    character.setRange (0.0, 10.0, 0.1);
    character.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    character.setDoubleClickReturnValue (true, 5.0);
    character.setPopupDisplayEnabled (true, true, this);
    character.setTooltip ("CHARACTER: 0 = ゆっくり・なめらか、10 = 速く・はっきり（ダブルクリックで 5）"_ju);
    character.onDragStart = [this] { mergeId = juce::Uuid().toString(); };
    character.onDragEnd = [this] { ctx.document.endMerge(); mergeId = {}; };
    character.onValueChange = [this]
    {
        const double v = character.getValue();
        edit ("マスターのリミッター CHARACTER"_ju, [v] (auto& l) { l.character = v; l.enabled = true; }, mergeId.isNotEmpty());
        repaint();
    };
    addAndMakeVisible (character);

    const juce::String modeTips[3] = { "ANALOG: やわらかくかかり、かかり続けるほど戻りがゆっくりになる"_ju,
                                       "TUBE: ANALOG に真空管のような温かい倍音を足す"_ju,
                                       "MODERN: 色付けがなく速い。音量を稼ぎたいとき向け"_ju };

    for (int i = 0; i < 3; ++i)
    {
        auto& b = modeButtons[i];
        b.setButtonText (modeNames[i]);
        b.setTooltip (modeTips[i]);
        b.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff3a3632));
        b.setColour (juce::TextButton::buttonOnColourId, amber);
        b.setColour (juce::TextButton::textColourOffId, cream);
        b.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        b.onClick = [this, i] { edit ("マスターのリミッターのモード"_ju, [m = modes[i]] (auto& l) { l.mode = m; l.enabled = true; }, false); };
        addAndMakeVisible (b);
    }

    matchButton.setTooltip ("いま測ったインテグレーテッドの値から、-14 LUFS に近づくように THRESHOLD を動かす（曲を最後まで再生してから押すと正確）"_ju);
    matchButton.onClick = [this] { matchTarget(); };
    addAndMakeVisible (matchButton);

    resetLoudness.setTooltip ("インテグレーテッド（全体の平均）を測り直す。再生を始めたときにも自動で測り直す"_ju);
    resetLoudness.onClick = [this] { ctx.engine.resetLoudness(); shortTermHistory.clear(); repaint(); };
    addAndMakeVisible (resetLoudness);

    ctx.document.addChangeListener (this);
    update();
    startTimerHz (30);
    setSize (880, 440);
}

MasterPanel::~MasterPanel()
{
    ctx.document.removeChangeListener (this);

    for (auto* s : { &threshold, &ceiling, &character })
        s->setLookAndFeel (nullptr);
}

void MasterPanel::edit (const juce::String& description, std::function<void (collab::MasterLimiter&)> fn, bool merge)
{
    ctx.document.perform (description, [fn] (collab::Project& p)
    {
        if (p.master.id.empty())
            p.master.id = collab::masterBusIdFor (p.projectId);

        fn (p.master.limiter);
    }, merge ? mergeId : juce::String());
}

void MasterPanel::update()
{
    const auto& l = ctx.document.getProject().master.limiter;
    enabled.setToggleState (l.enabled, juce::dontSendNotification);
    threshold.setValue (l.thresholdDb, juce::dontSendNotification);
    ceiling.setValue (l.ceilingDb, juce::dontSendNotification);
    character.setValue (l.character, juce::dontSendNotification);

    for (int i = 0; i < 3; ++i)
        modeButtons[i].setToggleState (l.mode == modes[i], juce::dontSendNotification);

    for (auto* s : { &threshold, &ceiling, &character })
        s->setAlpha (l.enabled ? 1.0f : 0.55f);

    repaint();
}

void MasterPanel::matchTarget()
{
    if (status.integratedLufs <= -99.0)
    {
        matchButton.setButtonText ("先に再生してください"_ju);
        juce::Timer::callAfterDelay (2000, [safe = juce::Component::SafePointer<MasterPanel> (this)]
        {
            if (safe != nullptr)
                safe->matchButton.setButtonText ("-14 LUFS に合わせる"_ju);
        });
        return;
    }

    // 足りない分だけ THRESHOLD を下げる（リミッターがかかるので実際の上がり方は少し小さい。もう一度押すと近づく）
    const double needed = targetLufs - status.integratedLufs;
    const double current = ctx.document.getProject().master.limiter.thresholdDb;
    const double next = juce::jlimit (-20.0, 0.0, std::round ((current - needed) * 10.0) / 10.0);

    edit ("マスターを -14 LUFS に合わせる"_ju, [next] (auto& l) { l.thresholdDb = next; l.enabled = true; }, false);
    ctx.engine.resetLoudness();
    shortTermHistory.clear();
}

void MasterPanel::timerCallback()
{
    status = ctx.engine.pollMaster();

    // メーターは速く上がり、ゆっくり下がる
    auto fall = [] (float& shown, float v) { shown = v > shown ? v : juce::jmax (v, shown - 1.2f); };
    fall (inShown, status.inputPeakDb);
    fall (outShown, status.outputPeakDb);
    grShown = status.gainReductionDb > grShown ? status.gainReductionDb : grShown + (status.gainReductionDb - grShown) * 0.25f;

    grHistory.push_back (grShown);

    if (grHistory.size() > 240)
        grHistory.erase (grHistory.begin());

    // ショートタームの推移（0.1 秒ごと、2 分ぶん）
    if (++frameCounter % 3 == 0 && ctx.engine.isPlaying())
    {
        shortTermHistory.push_back ((float) status.shortTermLufs);

        if (shortTermHistory.size() > 1200)
            shortTermHistory.erase (shortTermHistory.begin());
    }

    repaint();
}

//==============================================================================
void MasterPanel::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);

    for (auto r : { limiterArea, loudnessArea })
    {
        g.setGradientFill (juce::ColourGradient (panelTop, 0.0f, (float) r.getY(), panelBottom, 0.0f, (float) r.getBottom(), false));
        g.fillRoundedRectangle (r.toFloat(), 8.0f);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 8.0f, 1.0f);
    }

    paintLimiter (g);
    paintLoudness (g);
}

void MasterPanel::paintLimiter (juce::Graphics& g)
{
    g.setColour (amber);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText ("VINTAGE LIMITER", limiterArea.reduced (120, 10).withHeight (22), juce::Justification::centred);

    // IN / OUT メーター（-30〜0 dB）
    auto drawMeter = [&] (juce::Rectangle<int> area, float db, const juce::String& label, double markDb, bool markIsCeiling)
    {
        auto r = area.toFloat();
        g.setColour (juce::Colour (0xff0d0c0b));
        g.fillRoundedRectangle (r, 3.0f);
        auto toY = [&] (double v) { return juce::jmap ((float) v, meterFloor, 0.0f, r.getBottom() - 2.0f, r.getY() + 2.0f); };
        const float y = toY (juce::jlimit (meterFloor, 0.0f, db));
        juce::ColourGradient gr (over, 0.0f, toY (0.0), good, 0.0f, toY (-12.0), false);
        gr.addColour (0.3, amber);
        g.setGradientFill (gr);
        g.fillRect (r.reduced (3.0f, 2.0f).withTop (y));

        g.setColour (cream.withAlpha (0.35f));
        g.setFont (juce::FontOptions (9.0f));

        for (int v : { 0, -3, -6, -10, -15, -20, -30 })
            g.drawHorizontalLine ((int) toY (v), r.getX(), r.getX() + 4.0f);

        // THRESHOLD / CEILING の線
        g.setColour (markIsCeiling ? over.withAlpha (0.8f) : amber.withAlpha (0.8f));
        g.drawHorizontalLine ((int) toY (markDb), r.getX(), r.getRight());

        g.setColour (cream);
        g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        g.drawText (label, area.withY (area.getBottom() + 2).withHeight (14).expanded (10, 0), juce::Justification::centred);
        g.setFont (juce::FontOptions (11.5f));
        g.drawText (db <= -99.0f ? juce::String ("-inf") : juce::String (db, 1), area.withY (area.getY() - 16).withHeight (14).expanded (12, 0),
                    juce::Justification::centred);
    };

    const auto& l = ctx.document.getProject().master.limiter;
    drawMeter (inMeter, inShown, "IN", l.thresholdDb, false);
    drawMeter (outMeter, outShown, "OUT", l.ceilingDb, true);

    // 目盛り（メーターの間の数字）
    g.setColour (cream.withAlpha (0.5f));
    g.setFont (juce::FontOptions (9.5f));

    for (int v : { 0, -3, -6, -10, -15, -20, -30 })
    {
        const float y = juce::jmap ((float) v, meterFloor, 0.0f, (float) inMeter.getBottom() - 2.0f, (float) inMeter.getY() + 2.0f);
        g.drawText (juce::String (v), juce::Rectangle<float> ((float) inMeter.getX() - 30.0f, y - 6.0f, 26.0f, 12.0f), juce::Justification::centredRight);
    }

    // THRESHOLD / CEILING の値（下の段）
    {
        auto row = juce::Rectangle<int> (limiterArea.getX() + 14, inMeter.getBottom() + 20, limiterArea.getWidth() - 28, 16);
        g.setColour (amber);
        g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        g.drawText ("THRESHOLD  " + juce::String (l.thresholdDb, 1) + " dB", row, juce::Justification::centredLeft);
        g.drawText ("CEILING  " + juce::String (l.ceilingDb, 1) + " dB", row, juce::Justification::centredRight);
    }

    // ゲインリダクションの推移（上から下へ、0〜12 dB）
    {
        auto r = grGraph.toFloat();
        g.setColour (juce::Colour (0xff0d0c0b));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (cream.withAlpha (0.12f));

        for (int db : { 3, 6, 9 })
            g.drawHorizontalLine ((int) (r.getY() + r.getHeight() * (float) db / 12.0f), r.getX(), r.getRight());

        if (grHistory.size() > 1)
        {
            juce::Path p;
            p.startNewSubPath (r.getX(), r.getY());

            for (size_t i = 0; i < grHistory.size(); ++i)
            {
                const float x = r.getX() + r.getWidth() * (float) i / 239.0f;
                p.lineTo (x, r.getY() + r.getHeight() * juce::jlimit (0.0f, 1.0f, grHistory[i] / 12.0f));
            }

            p.lineTo (r.getX() + r.getWidth() * (float) (grHistory.size() - 1) / 239.0f, r.getY());
            p.closeSubPath();
            g.setColour (amber.withAlpha (0.35f));
            g.fillPath (p);
            g.setColour (amber);
            g.strokePath (p, juce::PathStrokeType (1.2f));
        }

        g.setColour (cream);
        g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.drawText ("GAIN REDUCTION", r.reduced (6.0f, 3.0f), juce::Justification::topLeft);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText ("-" + juce::String (grShown, 1) + " dB", r.reduced (6.0f, 3.0f), juce::Justification::topRight);
    }

    g.setColour (amber);
    g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
    g.drawText ("CHARACTER  " + juce::String (character.getValue(), 1), characterLabel, juce::Justification::centred);
}

void MasterPanel::paintLoudness (juce::Graphics& g)
{
    auto r = loudnessArea.reduced (16, 10);
    g.setColour (amber);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText ("LOUDNESS", r.removeFromTop (22), juce::Justification::centredLeft);
    g.setColour (cream.withAlpha (0.6f));
    g.setFont (juce::FontOptions (12.5f));
    g.drawText ("目標 -14 LUFS（リミッターの後、マスター音量の前で測定）"_ju, loudnessArea.reduced (16, 10).withHeight (22), juce::Justification::centredRight);

    // 数字
    {
        auto n = loudnessNumbers;
        auto big = n.removeFromLeft (n.getWidth() / 2);
        const double integrated = status.integratedLufs;
        const bool hasValue = integrated > -99.0;
        const double diff = integrated - targetLufs;
        const auto colour = ! hasValue ? cream : std::abs (diff) <= 1.0 ? good : diff > 0.0 ? over : amber;

        g.setColour (cream.withAlpha (0.7f));
        g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        g.drawText ("INTEGRATED", big.removeFromTop (16), juce::Justification::centredLeft);
        g.setColour (colour);
        g.setFont (juce::FontOptions (40.0f, juce::Font::bold));
        g.drawText (formatLufs (integrated), big.removeFromTop (46), juce::Justification::centredLeft);
        g.setFont (juce::FontOptions (13.5f));
        g.drawText (! hasValue ? "LUFS（再生すると測ります）"_ju
                    : std::abs (diff) <= 1.0 ? "LUFS  ちょうど良い（目標との差 "_ju + juce::String (diff, 1) + " LU）"_ju
                    : diff > 0.0 ? "LUFS  "_ju + juce::String (diff, 1) + " LU 大きい"_ju
                                 : "LUFS  あと "_ju + juce::String (-diff, 1) + " LU 足りない"_ju,
                    big.removeFromTop (16), juce::Justification::centredLeft);

        auto row = [&] (const juce::String& name, double v)
        {
            auto line = n.removeFromTop (30);
            g.setColour (cream.withAlpha (0.7f));
            g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
            g.drawText (name, line.removeFromLeft (100), juce::Justification::centredLeft);
            g.setColour (cream);
            g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
            g.drawText (formatLufs (v), line, juce::Justification::centredLeft);
        };

        row ("SHORT-TERM", status.shortTermLufs);
        row ("MOMENTARY", status.momentaryLufs);

        g.setColour (cream.withAlpha (0.6f));
        g.setFont (juce::FontOptions (12.5f));
        g.drawText ("測定 "_ju + juce::String (status.seconds, 0) + " 秒　出力ピーク "_ju
                    + (status.outputPeakDb <= -99.0f ? juce::String ("-inf") : juce::String (outShown, 1)) + " dB",
                    n.removeFromTop (16), juce::Justification::centredLeft);
    }

    // バー（-30〜-6 LUFS、目標の ±1 LU を緑に）
    {
        auto b = loudnessBar.toFloat();
        auto toX = [&] (double v) { return juce::jmap ((float) juce::jlimit (lufsMin, lufsMax, v), (float) lufsMin, (float) lufsMax, b.getX(), b.getRight()); };
        g.setColour (juce::Colour (0xff0d0c0b));
        g.fillRoundedRectangle (b, 3.0f);
        g.setColour (good.withAlpha (0.35f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (toX (targetLufs - 1.0), b.getY(), toX (targetLufs + 1.0), b.getBottom()));

        if (status.shortTermLufs > -99.0)
        {
            g.setColour (amber.withAlpha (0.8f));
            g.fillRect (b.reduced (0.0f, 5.0f).withRight (toX (status.shortTermLufs)));
        }

        if (status.integratedLufs > -99.0)
        {
            g.setColour (cream);
            g.fillRect (juce::Rectangle<float> (toX (status.integratedLufs) - 1.5f, b.getY() - 3.0f, 3.0f, b.getHeight() + 6.0f));
        }

        g.setColour (good);
        g.drawVerticalLine ((int) toX (targetLufs), b.getY() - 4.0f, b.getBottom() + 4.0f);

        g.setColour (cream.withAlpha (0.55f));
        g.setFont (juce::FontOptions (9.5f));

        for (int v = (int) lufsMin; v <= (int) lufsMax; v += 4)
            g.drawText (juce::String (v), juce::Rectangle<float> (toX (v) - 15.0f, b.getBottom() + 3.0f, 30.0f, 12.0f), juce::Justification::centred);
    }

    // ショートタームの推移
    {
        auto h = historyGraph.toFloat();
        g.setColour (juce::Colour (0xff0d0c0b));
        g.fillRoundedRectangle (h, 4.0f);
        auto toY = [&] (double v) { return juce::jmap ((float) juce::jlimit (lufsMin, lufsMax, v), (float) lufsMin, (float) lufsMax, h.getBottom() - 2.0f, h.getY() + 2.0f); };
        g.setColour (good.withAlpha (0.6f));
        g.drawHorizontalLine ((int) toY (targetLufs), h.getX(), h.getRight());

        if (shortTermHistory.size() > 1)
        {
            juce::Path p;
            bool started = false;
            const size_t n = shortTermHistory.size();

            for (size_t i = 0; i < n; ++i)
            {
                // まだ 3 秒たまっていないところ（値なし）は線を切る
                if (shortTermHistory[i] <= -99.0f)
                {
                    started = false;
                    continue;
                }

                const float x = h.getRight() - h.getWidth() * (float) (n - 1 - i) / 1199.0f;
                const float y = toY (shortTermHistory[i]);

                if (! started) { p.startNewSubPath (x, y); started = true; }
                else           p.lineTo (x, y);
            }

            g.setColour (amber);
            g.strokePath (p, juce::PathStrokeType (1.5f));
        }

        g.setColour (cream.withAlpha (0.6f));
        g.setFont (juce::FontOptions (11.5f));
        g.drawText ("SHORT-TERM（2 分）"_ju, h.reduced (6.0f, 3.0f), juce::Justification::topLeft);
    }
}

void MasterPanel::resized()
{
    auto area = getLocalBounds().reduced (10);
    limiterArea = area.removeFromLeft (440);
    area.removeFromLeft (10);
    loudnessArea = area;

    // リミッター
    {
        auto r = limiterArea.reduced (14, 10);
        auto header = r.removeFromTop (24);
        enabled.setBounds (header.removeFromLeft (100));
        resetLimiter.setBounds (header.removeFromRight (80));
        r.removeFromTop (26);
        r.removeFromBottom (40);

        // 0〜-30 dB のメーター。THRESHOLD（0〜-20）と CEILING（0〜-6）は同じ目盛りに合わせる
        auto left = r.removeFromLeft (100);
        left.removeFromLeft (34);
        inMeter = left.removeFromLeft (22);
        auto thresholdColumn = left.removeFromLeft (30);
        const float perDb = (float) inMeter.getHeight() / -meterFloor;
        threshold.setBounds (thresholdColumn.withTop (inMeter.getY() - 5).withHeight ((int) (perDb * 20.0f) + 10));

        auto right = r.removeFromRight (70);
        right.removeFromRight (10);
        outMeter = right.removeFromRight (22);
        ceiling.setBounds (right.removeFromRight (30).withTop (outMeter.getY() - 5).withHeight ((int) (perDb * 6.0f) + 10));

        r.reduce (12, 0);
        grGraph = r.removeFromTop (110);
        r.removeFromTop (12);
        auto modes = r.removeFromTop (28);
        const int w = modes.getWidth() / 3;

        for (auto& b : modeButtons)
            b.setBounds (modes.removeFromLeft (w).reduced (2, 0));

        r.removeFromTop (6);
        characterLabel = r.removeFromBottom (16);
        character.setBounds (r.withSizeKeepingCentre (juce::jmin (r.getWidth(), r.getHeight()), juce::jmin (r.getWidth(), r.getHeight())));
    }

    // ラウドネス
    {
        auto r = loudnessArea.reduced (16, 10);
        r.removeFromTop (30);
        loudnessNumbers = r.removeFromTop (110);
        r.removeFromTop (12);
        loudnessBar = r.removeFromTop (18);
        r.removeFromTop (22);
        auto buttons = r.removeFromBottom (28);
        matchButton.setBounds (buttons.removeFromLeft (170));
        buttons.removeFromLeft (8);
        resetLoudness.setBounds (buttons.removeFromLeft (100));
        r.removeFromBottom (8);
        historyGraph = r;
    }
}
