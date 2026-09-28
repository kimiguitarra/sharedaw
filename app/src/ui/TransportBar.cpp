#include "TransportBar.h"

#include "Theme.h"
#include "TempoMeterLanes.h"

namespace
{
    void styleValue (juce::Label& l, float size, bool mono, bool editable)
    {
        l.setJustificationType (juce::Justification::centred);
        l.setColour (juce::Label::backgroundColourId, Theme::background);
        l.setColour (juce::Label::textColourId, Theme::text);
        l.setFont (mono ? juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), size, juce::Font::plain)
                        : juce::FontOptions (size));

        if (editable)
        {
            l.setEditable (true, false, false);
            l.setColour (juce::Label::backgroundWhenEditingColourId, Theme::panelLight);
            l.setColour (juce::Label::textWhenEditingColourId, Theme::text);
            l.setMouseCursor (juce::MouseCursor::IBeamCursor);
        }
    }
}

//==============================================================================
ToolBar::ToolBar (AppContext& c) : ctx (c)
{
    // ツール（Cubase と同じ番号。テンキーでも切り替えられる）
    selectTool.setTooltip ("選択ツール（1）: 選択・移動・長さの変更。Ctrl/Shift+クリックで追加、空いている所をドラッグで範囲選択"_ju);
    pencilTool.setTooltip ("鉛筆ツール（2）: テンポ・拍子・コード・マーカー・クリップ・ノートを置く"_ju);
    splitTool.setTooltip ("はさみツール（3）: クリックした位置でクリップ・ノートを分割"_ju);

    for (auto* b : { &selectTool, &pencilTool, &splitTool })
    {
        b->onClick = [this, b] { ctx.state.tool = b->tool; ctx.state.changed(); };
        addAndMakeVisible (b);
    }

    // クオンタイズ値（Cubase のプロジェクトウィンドウのツールバーと同じく全体で 1 つ）
    int id = 1;
    for (auto& g : collab::Grid::presets())
        quantiseBox.addItem (toJuce (g.label()), id++);

    quantiseBox.setTooltip ("クオンタイズ値。スナップ・クオンタイズ・再生位置の移動の単位"_ju);
    quantiseBox.onChange = [this]
    {
        const auto presets = collab::Grid::presets();
        const int i = quantiseBox.getSelectedId() - 1;

        if (i >= 0 && i < (int) presets.size() && i != ctx.state.quantisePresetIndex())
            ctx.state.setQuantise (presets[(size_t) i]);
    };
    addAndMakeVisible (quantiseBox);

    snapButton.setButtonText ("スナップ"_ju);
    snapButton.setTooltip ("スナップ（J）: オンでクオンタイズ値に合わせる、オフでフリー"_ju);
    snapButton.onClick = [this] { ctx.state.setSnapEnabled (! ctx.state.snapEnabled()); };

    autoScrollButton.setButtonText ("自動スクロール"_ju);
    autoScrollButton.setTooltip ("自動スクロール（F）: 再生中に再生位置を追って表示を送る"_ju);
    autoScrollButton.onClick = [this]
    {
        ctx.state.autoScroll = ! ctx.state.autoScroll;
        ctx.state.changed();
    };

    metronomeButton.setButtonText ("メトロノーム"_ju);
    metronomeButton.setTooltip ("メトロノーム（C）"_ju);
    metronomeButton.onClick = [this]
    {
        ctx.state.metronomeEnabled = ! ctx.state.metronomeEnabled;
        ctx.state.changed();
    };

    settingsButton.setButtonText ("オーディオ設定"_ju);
    settingsButton.onClick = [this] { if (onAudioSettings) onAudioSettings(); };

    for (auto* b : { &snapButton, &autoScrollButton, &metronomeButton, &settingsButton })
    {
        b->setClickingTogglesState (false);
        addAndMakeVisible (b);
    }

    metronomeVolume.setTooltip ("メトロノームの音量"_ju);
    metronomeVolume.setRange (-40.0, 6.0, 0.5);
    metronomeVolume.setValue (ctx.state.metronomeVolumeDb, juce::dontSendNotification);
    metronomeVolume.onValueChange = [this]
    {
        ctx.state.metronomeVolumeDb = (float) metronomeVolume.getValue();
        ctx.state.changed();
    };
    addAndMakeVisible (metronomeVolume);

    midiLight.setTooltip ("MIDI 入力（キーボードを弾くと動きます。機器の設定はオーディオ設定）"_ju);
    addAndMakeVisible (midiLight);

    ctx.state.addChangeListener (this);
    changeListenerCallback (nullptr);
}

ToolBar::~ToolBar()
{
    ctx.state.removeChangeListener (this);
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
}

void ToolBar::update()
{
    midiLight.setLevel (ctx.engine.getMidiActivity());
}

void ToolBar::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panel);
    g.setColour (Theme::background);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
}

void ToolBar::resized()
{
    auto area = getLocalBounds().reduced (8, 5);

    for (auto* b : { &selectTool, &pencilTool, &splitTool })
    {
        b->setBounds (area.removeFromLeft (32));
        area.removeFromLeft (2);
    }

    area.removeFromLeft (14);
    quantiseBox.setBounds (area.removeFromLeft (150));
    area.removeFromLeft (4);
    snapButton.setBounds (area.removeFromLeft (80));
    area.removeFromLeft (4);
    autoScrollButton.setBounds (area.removeFromLeft (110));
    area.removeFromLeft (14);
    metronomeButton.setBounds (area.removeFromLeft (110));
    area.removeFromLeft (4);
    metronomeVolume.setBounds (area.removeFromLeft (90));

    settingsButton.setBounds (area.removeFromRight (juce::jmin (130, area.getWidth())));
    area.removeFromRight (8);

    if (syncBadge != nullptr)
    {
        syncBadge->setBounds (area.removeFromRight (juce::jmin (150, area.getWidth())));
        area.removeFromRight (8);
    }

    midiLight.setBounds (area.removeFromRight (juce::jmin (44, area.getWidth())));
}

void ToolBar::setSyncBadge (juce::Component* c)
{
    syncBadge = c;
    addAndMakeVisible (c);
    resized();
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
    // 左: ループ範囲
    loopStartTitle.setText ("ループ開始"_ju, juce::dontSendNotification);
    loopEndTitle.setText ("ループ終了"_ju, juce::dontSendNotification);

    for (auto* l : { &loopStartTitle, &loopEndTitle })
    {
        l->setFont (juce::FontOptions (11.0f));
        l->setColour (juce::Label::textColourId, Theme::textDim);
        l->setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (l);
    }

    for (auto* l : { &loopStartLabel, &loopEndLabel })
    {
        styleValue (*l, 16.0f, true, true);
        l->setTooltip ("クリックで入力（例: 5 または 5.3.0 = 小節.拍.tick）、ホイールで 1 小節ずつ。"_ju
                       "クリップやノートを選んで P でも設定できます"_ju);
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
        l->onWheel = [this, isStart] (int dir)
        {
            const auto& map = ctx.document.getTempoMap();
            const auto current = isStart ? ctx.state.loopStart : ctx.state.loopEnd;
            const int bar = map.tickToBar (current);
            const bool onBar = map.barToTick (bar) == current;
            setLoopEdge (isStart, map.barToTick (juce::jmax (1, dir > 0 ? bar + 1 : (onBar ? bar - 1 : bar))));
        };
    }

    // 中央: ループ・停止・再生・録音
    loopButton.setButtonText ("ループ"_ju);
    stopButton.setButtonText ("■"_ju);
    playButton.setButtonText ("▶"_ju);
    recordButton.setButtonText ("●"_ju);

    loopButton.setTooltip ("ループ再生（L / テンキー /）"_ju);
    stopButton.setTooltip ("停止（テンキー 0）。停止中に押すと先頭へ"_ju);
    playButton.setTooltip ("再生／一時停止（Space）"_ju);
    recordButton.setTooltip ("録音（R / テンキー *）。録音待機（●）のオーディオトラックと、選択中の MIDI トラックに録音します"_ju);

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

    for (auto* b : { &loopButton, &stopButton, &playButton, &recordButton })
        addAndMakeVisible (b);

    // 右: 現在の位置とテンポ・拍子
    styleValue (barBeatLabel, 20.0f, true, false);
    barBeatLabel.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 20.0f, juce::Font::bold));
    barBeatLabel.setTooltip ("現在の位置（小節.拍.tick）"_ju);
    styleValue (timeLabel, 15.0f, true, false);
    timeLabel.setTooltip ("現在の位置（分:秒）"_ju);
    styleValue (bpmLabel, 16.0f, false, true);
    styleValue (meterLabel, 16.0f, false, true);

    for (auto* l : { &barBeatLabel, &timeLabel, static_cast<juce::Label*> (&bpmLabel), static_cast<juce::Label*> (&meterLabel) })
        addAndMakeVisible (l);

    // テンポと拍子: 再生位置で有効な値を表示し、クリックで入力・ホイールで増減できる（テンポ・拍子トラックのイベントを書き換える）
    bpmLabel.setTooltip ("テンポ（クリックで入力、ホイールで ±1）。再生位置のテンポを変えます"_ju);
    meterLabel.setTooltip ("拍子（クリックで入力、ホイールで分子を ±1）。再生位置の拍子を変えます"_ju);

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
            updatePosition (ctx.engine.getPositionTick(), ctx.engine.getPositionSeconds(), ctx.engine.isPlaying());
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
            updatePosition (ctx.engine.getPositionTick(), ctx.engine.getPositionSeconds(), ctx.engine.isPlaying());
    };
    meterLabel.onWheel = [this] (int dir)
    {
        const auto sig = ctx.document.getTempoMap().timeSignatureAtTick (playheadTick());
        setMeterAtPlayhead (juce::jlimit (1, 64, sig.numerator + dir), sig.denominator);
    };

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

void TransportBar::updatePosition (double tick, double seconds, bool playing)
{
    const auto& map = ctx.document.getTempoMap();
    const auto t = (collab::Tick) juce::jmax (0.0, tick);
    const auto bb = map.tickToBarBeat (t);
    const auto sig = map.timeSignatureAtTick (t);

    barBeatLabel.setText (juce::String (bb.bar).paddedLeft (' ', 3) + ". " + juce::String (bb.beat) + ". "
                            + juce::String (bb.tickInBeat).paddedLeft ('0', 3),
                          juce::dontSendNotification);

    const auto s = juce::jmax (0.0, seconds);
    const int minutes = (int) (s / 60.0);
    timeLabel.setText (juce::String (minutes).paddedLeft ('0', 2) + ":" + juce::String (s - minutes * 60.0, 3).paddedLeft ('0', 6),
                       juce::dontSendNotification);

    const double bpm = map.bpmAtTick (t);

    if (! bpmLabel.isBeingEdited())
        bpmLabel.setText (juce::String (bpm, std::abs (bpm - std::round (bpm)) < 0.005 ? 0 : 2) + " BPM", juce::dontSendNotification);

    if (! meterLabel.isBeingEdited())
        meterLabel.setText (juce::String (sig.numerator) + "/" + juce::String (sig.denominator), juce::dontSendNotification);

    recordButton.setToggleState (ctx.engine.isRecording(), juce::dontSendNotification);

    if (playing != wasPlaying)
    {
        wasPlaying = playing;
        playButton.setButtonText (playing ? "❚❚"_ju : "▶"_ju);
        playButton.setToggleState (playing, juce::dontSendNotification);
    }
}

void TransportBar::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panel);
    g.setColour (Theme::background);
    g.drawHorizontalLine (0, 0.0f, (float) getWidth());
}

void TransportBar::resized()
{
    auto area = getLocalBounds().reduced (10, 6);

    // 中央のボタン
    constexpr int buttonWidth = 50, gap = 4;
    const int centreWidth = 70 + 3 * buttonWidth + 3 * gap;
    auto centre = area.withSizeKeepingCentre (centreWidth, area.getHeight());
    loopButton.setBounds (centre.removeFromLeft (70));

    for (auto* b : { &stopButton, &playButton, &recordButton })
    {
        centre.removeFromLeft (gap);
        b->setBounds (centre.removeFromLeft (buttonWidth));
    }

    // 左: ループ範囲
    auto left = area.withRight (loopButton.getX() - 20);
    loopStartTitle.setBounds (left.removeFromLeft (70));
    left.removeFromLeft (4);
    loopStartLabel.setBounds (left.removeFromLeft (120));
    left.removeFromLeft (10);
    loopEndTitle.setBounds (left.removeFromLeft (70));
    left.removeFromLeft (4);
    loopEndLabel.setBounds (left.removeFromLeft (120));

    // 右: 位置とテンポ・拍子
    auto right = area.withLeft (recordButton.getRight() + 20);
    meterLabel.setBounds (right.removeFromRight (56));
    right.removeFromRight (4);
    bpmLabel.setBounds (right.removeFromRight (100));
    right.removeFromRight (10);
    timeLabel.setBounds (right.removeFromRight (110));
    right.removeFromRight (4);
    barBeatLabel.setBounds (right.removeFromRight (150));
}

//==============================================================================
collab::Tick TransportBar::playheadTick() const
{
    return (collab::Tick) juce::jmax (0.0, ctx.engine.getPositionTick());
}

void TransportBar::setTempoAtPlayhead (double bpm, const juce::String& mergeId)
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

void TransportBar::setMeterAtPlayhead (int numerator, int denominator)
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
