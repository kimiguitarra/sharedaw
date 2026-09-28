#include "TransportBar.h"

#include "Theme.h"
#include "KeyLane.h"
#include "TimeGrid.h"
#include "TempoMeterLanes.h"

#include <collab/ChordPlayback.h>

namespace
{
    void styleValue (juce::Label& l, float size, bool mono, bool editable)
    {
        // 値はガラスのカプセルの上にそのまま書く（中に四角い箱を作らない）。入力中だけ入力欄の色になる
        l.setJustificationType (juce::Justification::centred);
        l.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        l.setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
        l.setColour (juce::Label::textColourId, Theme::text);
        l.setFont (mono ? juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), size, juce::Font::plain)
                        : juce::FontOptions (size));

        if (editable)
        {
            l.setEditable (true, false, false);
            l.setColour (juce::Label::backgroundWhenEditingColourId, Theme::field);
            l.setColour (juce::Label::outlineWhenEditingColourId, Theme::accent);
            l.setColour (juce::Label::textWhenEditingColourId, Theme::text);
            l.setMouseCursor (juce::MouseCursor::IBeamCursor);
        }
    }
}

ToolBar::ToolBar (AppContext& c) : ctx (c)
{
    // ツール（Cubase と同じ番号）
    selectTool.setTooltip ("選択（1）"_ju);
    pencilTool.setTooltip ("鉛筆（2）"_ju);
    splitTool.setTooltip ("はさみ（3）"_ju);

    for (auto* b : { &selectTool, &pencilTool, &splitTool })
    {
        b->onClick = [this, b] { ctx.state.tool = b->tool; ctx.state.changed(); };
        addAndMakeVisible (b);
    }

    // クオンタイズ値（Cubase のプロジェクトウィンドウのツールバーと同じく全体で 1 つ）
    TimeGrid::fillQuantiseBox (quantiseBox);

    quantiseBox.setTooltip ("クオンタイズ"_ju);
    quantiseBox.onChange = [this]
    {
        const auto presets = collab::Grid::presets();
        const int i = quantiseBox.getSelectedId() - 1;

        if (i >= 0 && i < (int) presets.size() && i != ctx.state.quantisePresetIndex())
            ctx.state.setQuantise (presets[(size_t) i]);
    };
    addAndMakeVisible (quantiseBox);


    snapButton.setTooltip ("スナップ（J）"_ju);
    snapButton.onClick = [this] { ctx.state.setSnapEnabled (! ctx.state.snapEnabled()); };


    autoScrollButton.setTooltip ("自動スクロール（F）"_ju);
    autoScrollButton.onClick = [this]
    {
        ctx.state.autoScroll = ! ctx.state.autoScroll;
        ctx.state.changed();
    };


    metronomeButton.setTooltip ("メトロノーム（C）"_ju);
    metronomeButton.onClick = [this]
    {
        ctx.state.metronomeEnabled = ! ctx.state.metronomeEnabled;
        ctx.state.changed();
    };

    for (auto* b : std::initializer_list<juce::TextButton*> { &snapButton, &autoScrollButton, &metronomeButton })
    {
        b->setClickingTogglesState (false);
        addAndMakeVisible (b);
    }

    metronomeVolume.setTooltip ("メトロノーム音量"_ju);
    metronomeVolume.setRange (-40.0, 6.0, 0.5);
    metronomeVolume.setValue (ctx.state.metronomeVolumeDb, juce::dontSendNotification);
    metronomeVolume.onValueChange = [this]
    {
        ctx.state.metronomeVolumeDb = (float) metronomeVolume.getValue();
        ctx.state.changed();
    };
    addAndMakeVisible (metronomeVolume);

    // 右: 曲のテンポ・拍子・キー（再生位置のもの）
    styleValue (bpmLabel, 16.0f, false, true);
    styleValue (meterLabel, 16.0f, false, true);
    styleValue (keyLabel, 16.0f, false, false);

    for (auto* l : { static_cast<juce::Label*> (&bpmLabel), static_cast<juce::Label*> (&meterLabel), &keyLabel })
        addAndMakeVisible (l);

    // テンポと拍子: 再生位置で有効な値を表示し、クリックで入力・ホイールで増減できる（テンポ・拍子トラックのイベントを書き換える）
    bpmLabel.setTooltip ("テンポ"_ju);
    meterLabel.setTooltip ("拍子"_ju);

    bpmLabel.onEditorShow = [this]
    {
        if (auto* ed = bpmLabel.getCurrentTextEditor())
        {
            ed->setText (bpmLabel.getText().upToFirstOccurrenceOf (" ", false, false), false);
            ed->setInputRestrictions (7, "0123456789.");
            ed->selectAll();
        }
    };
    bpmLabel.onTextChange = [this]
    {
        const double bpm = bpmLabel.getText().getDoubleValue();

        if (bpm >= 10.0 && bpm <= 999.0)
            setTempoAtPlayhead (bpm);
        else
            refreshTempo();
    };
    bpmLabel.onWheel = [this] (int dir)
    {
        // 続けて回した分は 1 つの「元に戻す」にまとめる
        const auto now = juce::Time::getMillisecondCounter();

        if (wheelMergeId.isEmpty() || now - lastWheelTime > 800)
            wheelMergeId = juce::Uuid().toString();

        lastWheelTime = now;
        const double bpm = std::round (ctx.document.getTempoMap().bpmAtTick (playheadTick())) + dir;
        setTempoAtPlayhead (juce::jlimit (10.0, 999.0, bpm), wheelMergeId);
    };

    meterLabel.onEditorShow = [this]
    {
        if (auto* ed = meterLabel.getCurrentTextEditor())
        {
            ed->setInputRestrictions (5, "0123456789/");
            ed->selectAll();
        }
    };
    meterLabel.onTextChange = [this]
    {
        if (auto m = MeterLane::parseMeter (meterLabel.getText()))
            setMeterAtPlayhead (m->first, m->second);
        else
            refreshTempo();
    };
    meterLabel.onWheel = [this] (int dir)
    {
        const auto sig = ctx.document.getTempoMap().timeSignatureAtTick (playheadTick());
        setMeterAtPlayhead (juce::jlimit (1, 64, sig.numerator + dir), sig.denominator);
    };

    keyLabel.setTooltip ("キー"_ju);
    keyLabel.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    keyLabel.addMouseListener (this, false);

    for (auto* b : { &selectTool, &pencilTool, &splitTool })
        b->setWantsKeyboardFocus (false);

    ctx.state.addChangeListener (this);
    ctx.document.addChangeListener (this);
    changeListenerCallback (nullptr);
}

ToolBar::~ToolBar()
{
    ctx.state.removeChangeListener (this);
    ctx.document.removeChangeListener (this);
}

void ToolBar::changeListenerCallback (juce::ChangeBroadcaster*)
{
    for (auto* b : { &selectTool, &pencilTool, &splitTool })
        b->setToggleState (ctx.state.tool == b->tool, juce::dontSendNotification);

    metronomeButton.setToggleState (ctx.state.metronomeEnabled, juce::dontSendNotification);
    metronomeVolume.setValue (ctx.state.metronomeVolumeDb, juce::dontSendNotification);
    snapButton.setToggleState (ctx.state.snapEnabled(), juce::dontSendNotification);
    autoScrollButton.setToggleState (ctx.state.autoScroll, juce::dontSendNotification);
    quantiseBox.setSelectedId (ctx.state.quantisePresetIndex() + 1, juce::dontSendNotification);
    refreshTempo();
}

void ToolBar::update()
{
    // 再生位置が動くとテンポ・拍子・キーが変わることがある
    const auto tick = playheadTick();

    if (tick != lastTick)
    {
        lastTick = tick;
        refreshTempo();
    }
}

void ToolBar::refreshTempo()
{
    const auto& map = ctx.document.getTempoMap();
    const auto t = playheadTick();
    const double bpm = map.bpmAtTick (t);
    const auto sig = map.timeSignatureAtTick (t);

    if (! bpmLabel.isBeingEdited())
        bpmLabel.setText (juce::String (bpm, std::abs (bpm - std::round (bpm)) < 0.005 ? 0 : 2) + " BPM", juce::dontSendNotification);

    if (! meterLabel.isBeingEdited())
        meterLabel.setText (juce::String (sig.numerator) + "/" + juce::String (sig.denominator), juce::dontSendNotification);

    const auto key = collab::keyAt (ctx.document.getProject(), map, t);
    keyLabel.setText (key ? "Key: "_ju + toJuce (collab::chord::keyName (*key)) : "Key: -"_ju, juce::dontSendNotification);
}

void ToolBar::mouseUp (const juce::MouseEvent& e)
{
    if (e.eventComponent != &keyLabel)
        return;

    // 再生位置で有効なキー（その小節より前で最後のもの）を変える。なければ 1 小節目に置く
    const auto& map = ctx.document.getTempoMap();
    const int playBar = map.tickToBar (playheadTick());
    int bar = 1;

    for (auto& k : ctx.document.getProject().keyTrack.events)
        if (k.bar <= playBar && k.bar > bar)
            bar = k.bar;

    const auto current = collab::keyAt (ctx.document.getProject(), map, map.barToTick (bar));
    KeyLane::keyMenu (current, [this, bar] (collab::chord::Key k) { KeyLane::setKey (ctx, bar, k); })
        .showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&keyLabel));
}

void ToolBar::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panel);
    g.setColour (Theme::background);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

    for (auto& r : groups)
        Theme::drawGlass (g, r.toFloat(), 12.0f);
}

void ToolBar::resized()
{
    auto area = getLocalBounds().reduced (8, 5);

    for (auto* b : { &selectTool, &pencilTool, &splitTool })
    {
        b->setBounds (area.removeFromLeft (32));
        area.removeFromLeft (2);
    }

    groups.clear();
    groups.push_back (selectTool.getBounds().getUnion (splitTool.getBounds()).expanded (4, 1));

    area.removeFromLeft (18);
    quantiseBox.setBounds (area.removeFromLeft (140));
    area.removeFromLeft (4);
    snapButton.setBounds (area.removeFromLeft (40));
    area.removeFromLeft (4);
    autoScrollButton.setBounds (area.removeFromLeft (40));
    groups.push_back (quantiseBox.getBounds().getUnion (autoScrollButton.getBounds()).expanded (4, 1));

    area.removeFromLeft (18);
    metronomeButton.setBounds (area.removeFromLeft (40));
    area.removeFromLeft (6);
    metronomeVolume.setBounds (area.removeFromLeft (100));
    groups.push_back (metronomeButton.getBounds().getUnion (metronomeVolume.getBounds()).expanded (4, 1));

    area.removeFromLeft (18);
    bpmLabel.setBounds (area.removeFromLeft (110));
    area.removeFromLeft (4);
    meterLabel.setBounds (area.removeFromLeft (60));
    area.removeFromLeft (4);
    keyLabel.setBounds (area.removeFromLeft (130));
    groups.push_back (bpmLabel.getBounds().getUnion (keyLabel.getBounds()).expanded (4, 1));
}

collab::Tick ToolBar::playheadTick() const
{
    return (collab::Tick) juce::jmax (0.0, ctx.engine.getPositionTick());
}

void ToolBar::ToolButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool on = getToggleState();

    g.setColour (on ? Theme::accent.withAlpha (0.35f) : (highlighted || down ? Theme::panelLight : Theme::panel));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (on ? Theme::accent : Theme::gridBar);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);

    auto iconArea = r.reduced (r.getWidth() * 0.26f, r.getHeight() * 0.22f);
    g.setColour (on ? Theme::text : Theme::textDim);

    switch (tool)
    {
        case EditTool::select: g.fillPath (Theme::selectToolIcon (iconArea)); break;
        case EditTool::pencil: g.fillPath (Theme::pencilToolIcon (iconArea)); break;
        case EditTool::split:  g.strokePath (Theme::splitToolIcon (iconArea), juce::PathStrokeType (1.4f)); break;
    }
}

//==============================================================================
TransportBar::TransportBar (AppContext& c) : ctx (c)
{
    // 左: 左右のロケーター（旗）。Cubase と同じく、サイクル再生はこの間を繰り返す
    loopStartFlag.setTooltip ("左ロケーター"_ju);
    loopEndFlag.setTooltip ("右ロケーター"_ju);

    for (auto* f : { &loopStartFlag, &loopEndFlag })
        addAndMakeVisible (f);

    for (auto* l : { &loopStartLabel, &loopEndLabel })
    {
        styleValue (*l, 17.0f, true, true);
        l->setTooltip (l == &loopStartLabel ? "左ロケーター"_ju : "右ロケーター"_ju);
        addAndMakeVisible (l);

        const bool isStart = l == &loopStartLabel;
        l->onEditorShow = [l]
        {
            if (auto* ed = l->getCurrentTextEditor())
            {
                ed->setInputRestrictions (16, "0123456789. ");
                ed->selectAll();
            }
        };
        l->onTextChange = [this, l, isStart]
        {
            if (auto t = parsePosition (l->getText()))
                setLoopEdge (isStart, *t);
            else
                refreshLoop();
        };
        // ホイール: マウスの下が小節なら 1 小節、拍なら 1 拍、tick ならクオンタイズ値ずつ
        l->onWheelPart = [this, isStart] (int dir, int part)
        {
            const auto current = isStart ? ctx.state.loopStart : ctx.state.loopEnd;
            setLoopEdge (isStart, stepPosition (current, dir, part));
        };
    }

    // 中央: サイクル・停止・再生・録音、その右に現在の位置
    loopButton.setTooltip ("サイクル（L）"_ju);
    stopButton.setTooltip ("停止"_ju);
    playButton.setTooltip ("再生（Space）"_ju);
    recordButton.setTooltip ("録音（*）"_ju);

    loopButton.setClickingTogglesState (false);
    loopButton.onClick = [this]
    {
        ctx.state.loopEnabled = ! ctx.state.loopEnabled;
        ctx.state.changed();
    };
    stopButton.onClick = [this]
    {
        if (! ctx.engine.isPlaying())
            ctx.engine.returnToStart();

        ctx.engine.stop();
    };
    playButton.onClick = [this] { ctx.engine.togglePlay(); };
    playButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff43a047));
    recordButton.onClick = [this] { if (ctx.toggleRecord) ctx.toggleRecord(); };
    recordButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffe57373));
    recordButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffc62828));
    recordButton.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    loopButton.setColour (juce::TextButton::buttonOnColourId, Theme::accent.darker (0.3f));

    for (auto* b : { &loopButton, &stopButton, &playButton, &recordButton })
        addAndMakeVisible (b);

    // 右下: ミキサー（F3）
    mixerButton.setTooltip ("ミキサー（F3）"_ju);
    mixerButton.setClickingTogglesState (false);
    mixerButton.onClick = [this] { if (onMixer) onMixer(); };
    addAndMakeVisible (mixerButton);

    styleValue (barBeatLabel, 22.0f, true, false);
    barBeatLabel.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 22.0f, juce::Font::bold));
    barBeatLabel.setTooltip ("再生位置"_ju);
    barBeatLabel.onWheelPart = [this] (int dir, int part)
    {
        ctx.engine.setPositionTick ((double) stepPosition ((collab::Tick) juce::jmax (0.0, ctx.engine.getPositionTick()), dir, part));
    };
    addAndMakeVisible (barBeatLabel);

    ctx.state.addChangeListener (this);
    ctx.document.addChangeListener (this);
    changeListenerCallback (nullptr);
    updatePosition (0, 0, false);
}

TransportBar::~TransportBar()
{
    ctx.state.removeChangeListener (this);
    ctx.document.removeChangeListener (this);
}

void TransportBar::changeListenerCallback (juce::ChangeBroadcaster*)
{
    loopButton.setToggleState (ctx.state.loopEnabled, juce::dontSendNotification);
    refreshLoop();
}

juce::String TransportBar::formatPosition (collab::Tick t) const
{
    const auto bb = ctx.document.getTempoMap().tickToBarBeat (juce::jmax<collab::Tick> (0, t));
    return juce::String (bb.bar) + ". " + juce::String (bb.beat) + ". " + juce::String (bb.tickInBeat);
}

std::optional<collab::Tick> TransportBar::parsePosition (const juce::String& text) const
{
    // 「小節」「小節.拍」「小節.拍.tick」
    auto parts = juce::StringArray::fromTokens (text.replaceCharacter (' ', '.'), ".", {});
    parts.removeEmptyStrings();

    if (parts.isEmpty() || parts.size() > 3)
        return std::nullopt;

    const auto& map = ctx.document.getTempoMap();
    const int bar = parts[0].getIntValue();

    if (bar < 1)
        return std::nullopt;

    const auto sig = map.timeSignatureAtBar (bar);
    const int beat = juce::jlimit (1, sig.numerator, parts.size() > 1 ? parts[1].getIntValue() : 1);
    const auto tick = juce::jlimit<collab::Tick> (0, sig.ticksPerBeat() - 1, parts.size() > 2 ? parts[2].getIntValue() : 0);
    return map.barToTick (bar) + (collab::Tick) (beat - 1) * sig.ticksPerBeat() + tick;
}

collab::Tick TransportBar::stepPosition (collab::Tick from, int direction, int part) const
{
    const auto& map = ctx.document.getTempoMap();
    const auto bb = map.tickToBarBeat (from);
    const auto barStart = map.barToTick (bb.bar);
    const auto sig = map.timeSignatureAtTick (from);

    if (part == 0)
    {
        // 小節だけ変える（拍・tick はそのまま。短い小節に入るときは小節の中に収める）
        const int bar = juce::jmax (1, bb.bar + direction);
        const auto start = map.barToTick (bar);
        return start + juce::jmin (from - barStart, map.timeSignatureAtTick (start).ticksPerBar() - 1);
    }

    const collab::Tick step = part == 1 ? sig.ticksPerBeat()
                                        : juce::jmax<collab::Tick> (1, (collab::Tick) std::llround (ctx.state.grid.stepExact()));
    return juce::jmax<collab::Tick> (0, from + direction * step);
}

void TransportBar::setLoopEdge (bool start, collab::Tick t)
{
    auto& s = ctx.state;
    const auto minLength = collab::kPpq / 4;

    if (start)
    {
        s.loopStart = juce::jmax<collab::Tick> (0, t);

        if (s.loopEnd <= s.loopStart)
            s.loopEnd = s.loopStart + ctx.document.getTempoMap().timeSignatureAtTick (s.loopStart).ticksPerBar();
    }
    else
    {
        s.loopEnd = juce::jmax (s.loopStart + minLength, t);
    }

    s.changed();
    refreshLoop();
}

void TransportBar::refreshLoop()
{
    if (! loopStartLabel.isBeingEdited())
        loopStartLabel.setText (formatPosition (ctx.state.loopStart), juce::dontSendNotification);

    if (! loopEndLabel.isBeingEdited())
        loopEndLabel.setText (formatPosition (ctx.state.loopEnd), juce::dontSendNotification);
}

void TransportBar::updatePosition (double tick, double, bool playing)
{
    const auto& map = ctx.document.getTempoMap();
    const auto t = (collab::Tick) juce::jmax (0.0, tick);
    const auto bb = map.tickToBarBeat (t);

    barBeatLabel.setText (juce::String (bb.bar).paddedLeft (' ', 3) + ". " + juce::String (bb.beat) + ". "
                            + juce::String (bb.tickInBeat).paddedLeft ('0', 3),
                          juce::dontSendNotification);

    recordButton.setToggleState (ctx.engine.isRecording(), juce::dontSendNotification);

    if (playing != wasPlaying)
    {
        wasPlaying = playing;
        playButton.setIcon (playing ? "pause" : "play");
        playButton.setToggleState (playing, juce::dontSendNotification);
    }
}

void TransportBar::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panel);
    g.setColour (Theme::background);
    g.drawHorizontalLine (0, 0.0f, (float) getWidth());

    // まとまりごとのガラスのカプセル
    for (auto& r : groups)
        Theme::drawGlass (g, r.toFloat(), 12.0f);
}

void TransportBar::resized()
{
    auto area = getLocalBounds().reduced (10, 6);

    // 右端: ミキサーのボタン（正方形）
    mixerButton.setBounds (area.removeFromRight (area.getHeight() + 8).withSizeKeepingCentre (area.getHeight() + 4, area.getHeight()));

    // 真ん中にまとめる: 左右のロケーター | サイクル・停止・再生・録音 | 現在の位置
    constexpr int buttonWidth = 48, gap = 4, flagWidth = 28, locatorWidth = 118, positionWidth = 170, groupGap = 22;
    const int locatorsWidth = 2 * (flagWidth + 2 + locatorWidth) + 12;
    const int buttonsWidth = 4 * buttonWidth + 3 * gap;
    const int total = locatorsWidth + groupGap + buttonsWidth + groupGap + positionWidth;
    auto row = area.withSizeKeepingCentre (juce::jmin (total, area.getWidth()), area.getHeight());
    groups.clear();

    auto locators = row.removeFromLeft (locatorsWidth);
    loopStartFlag.setBounds (locators.removeFromLeft (flagWidth));
    locators.removeFromLeft (2);
    loopStartLabel.setBounds (locators.removeFromLeft (locatorWidth));
    locators.removeFromLeft (12);
    loopEndFlag.setBounds (locators.removeFromLeft (flagWidth));
    locators.removeFromLeft (2);
    loopEndLabel.setBounds (locators.removeFromLeft (locatorWidth));
    groups.push_back (loopStartFlag.getBounds().getUnion (loopEndLabel.getBounds()).expanded (6, 3));
    row.removeFromLeft (groupGap);

    auto buttons = row.removeFromLeft (buttonsWidth);
    groups.push_back (buttons.expanded (6, 3));
    loopButton.setBounds (buttons.removeFromLeft (buttonWidth));

    for (auto* b : { &stopButton, &playButton, &recordButton })
    {
        buttons.removeFromLeft (gap);
        b->setBounds (buttons.removeFromLeft (buttonWidth));
    }

    row.removeFromLeft (groupGap);
    barBeatLabel.setBounds (row.removeFromLeft (positionWidth));
    groups.push_back (barBeatLabel.getBounds().expanded (6, 3));
}

//==============================================================================
void ToolBar::setTempoAtPlayhead (double bpm, const juce::String& mergeId)
{
    // 再生位置で有効なテンポ変更（直前のイベント）を書き換える
    const auto tick = playheadTick();

    ctx.document.perform ("テンポの変更"_ju, [tick, bpm] (collab::Project& p)
    {
        collab::TempoEvent* target = nullptr;

        for (auto& e : p.tempoTrack.events)
            if (e.tick <= tick && (target == nullptr || e.tick >= target->tick))
                target = &e;

        if (target == nullptr && ! p.tempoTrack.events.empty())
            target = &p.tempoTrack.events.front();

        if (target != nullptr)
            target->bpm = bpm;
    }, mergeId);
}

void ToolBar::setMeterAtPlayhead (int numerator, int denominator)
{
    const int bar = ctx.document.getTempoMap().tickToBar (playheadTick());

    ctx.document.perform ("拍子の変更"_ju, [bar, numerator, denominator] (collab::Project& p)
    {
        collab::MeterEvent* target = nullptr;

        for (auto& e : p.meterTrack.events)
            if (e.bar <= bar && (target == nullptr || e.bar >= target->bar))
                target = &e;

        if (target == nullptr && ! p.meterTrack.events.empty())
            target = &p.meterTrack.events.front();

        if (target != nullptr)
        {
            target->numerator = numerator;
            target->denominator = denominator;
        }
    });
}
