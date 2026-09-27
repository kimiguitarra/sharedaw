#include "TransportBar.h"

#include "Theme.h"

TransportBar::TransportBar (AppContext& c) : ctx (c)
{
    toStartButton.setButtonText ("|◀"_ju);
    playButton.setButtonText ("▶"_ju);
    stopButton.setButtonText ("■"_ju);
    recordButton.setButtonText ("●"_ju);
    loopButton.setButtonText ("ループ"_ju);
    metronomeButton.setButtonText ("メトロノーム"_ju);
    settingsButton.setButtonText ("オーディオ設定"_ju);

    toStartButton.setTooltip ("先頭へ（Home）"_ju);
    playButton.setTooltip ("再生／一時停止（Space）"_ju);
    stopButton.setTooltip ("停止"_ju);
    recordButton.setTooltip ("録音（R）。録音待機（●）にしたオーディオトラックに録音します"_ju);
    loopButton.setTooltip ("ループ再生（L）。範囲はクリップやノートを選んで P（トランスポート → ループ範囲を選択範囲に合わせる）"_ju);
    metronomeButton.setTooltip ("メトロノーム"_ju);
    metronomeVolume.setTooltip ("メトロノームの音量"_ju);

    recordButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffe57373));
    recordButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffc62828));
    recordButton.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    recordButton.onClick = [this] { if (ctx.toggleRecord) ctx.toggleRecord(); };

    loopButton.setClickingTogglesState (false);
    metronomeButton.setClickingTogglesState (false);

    toStartButton.onClick = [this] { ctx.engine.returnToStart(); };
    playButton.onClick = [this] { ctx.engine.togglePlay(); };
    stopButton.onClick = [this]
    {
        if (! ctx.engine.isPlaying())
            ctx.engine.returnToStart();

        ctx.engine.stop();
    };
    loopButton.onClick = [this]
    {
        ctx.state.loopEnabled = ! ctx.state.loopEnabled;
        ctx.state.changed();
    };
    metronomeButton.onClick = [this]
    {
        ctx.state.metronomeEnabled = ! ctx.state.metronomeEnabled;
        ctx.state.changed();
    };
    settingsButton.onClick = [this] { if (onAudioSettings) onAudioSettings(); };

    metronomeVolume.setRange (-40.0, 6.0, 0.5);
    metronomeVolume.setValue (ctx.state.metronomeVolumeDb, juce::dontSendNotification);
    metronomeVolume.onValueChange = [this]
    {
        ctx.state.metronomeVolumeDb = (float) metronomeVolume.getValue();
        ctx.state.changed();
    };

    for (auto* b : { &toStartButton, &playButton, &stopButton, &recordButton, &loopButton, &metronomeButton, &settingsButton })
        addAndMakeVisible (b);

    // ツール（Cubase と同じくテンキーの 1 / 2 でも切り替えられる）
    selectTool.setTooltip ("選択ツール（テンキー 1）: 選択・移動・長さの変更"_ju);
    pencilTool.setTooltip ("鉛筆ツール（テンキー 2）: テンポ・拍子・コード・クリップ・ノートを置く"_ju);
    selectTool.onClick = [this] { ctx.state.tool = EditTool::select; ctx.state.changed(); };
    pencilTool.onClick = [this] { ctx.state.tool = EditTool::pencil; ctx.state.changed(); };
    addAndMakeVisible (selectTool);
    addAndMakeVisible (pencilTool);

    addAndMakeVisible (metronomeVolume);

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
    snapButton.setClickingTogglesState (false);
    snapButton.onClick = [this] { ctx.state.setSnapEnabled (! ctx.state.snapEnabled()); };
    addAndMakeVisible (snapButton);

    autoScrollButton.setButtonText ("自動スクロール"_ju);
    autoScrollButton.setTooltip ("自動スクロール（F）: 再生中に再生位置を追って表示を送る"_ju);
    autoScrollButton.setClickingTogglesState (false);
    autoScrollButton.onClick = [this]
    {
        ctx.state.autoScroll = ! ctx.state.autoScroll;
        ctx.state.changed();
    };
    addAndMakeVisible (autoScrollButton);

    for (auto* l : { &barBeatLabel, &timeLabel, &tempoLabel })
    {
        l->setJustificationType (juce::Justification::centred);
        l->setColour (juce::Label::backgroundColourId, Theme::background);
        l->setColour (juce::Label::textColourId, Theme::text);
        addAndMakeVisible (l);
    }

    barBeatLabel.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 20.0f, juce::Font::bold));
    timeLabel.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 15.0f, juce::Font::plain));
    tempoLabel.setFont (juce::FontOptions (15.0f));
    barBeatLabel.setTooltip ("小節.拍.tick"_ju);

    ctx.state.addChangeListener (this);
    changeListenerCallback (nullptr);
    updatePosition (0, 0, false);
}

TransportBar::~TransportBar()
{
    ctx.state.removeChangeListener (this);
}

void TransportBar::changeListenerCallback (juce::ChangeBroadcaster*)
{
    selectTool.setToggleState (ctx.state.tool == EditTool::select, juce::dontSendNotification);
    pencilTool.setToggleState (ctx.state.tool == EditTool::pencil, juce::dontSendNotification);
    loopButton.setToggleState (ctx.state.loopEnabled, juce::dontSendNotification);
    metronomeButton.setToggleState (ctx.state.metronomeEnabled, juce::dontSendNotification);
    snapButton.setToggleState (ctx.state.snapEnabled(), juce::dontSendNotification);
    autoScrollButton.setToggleState (ctx.state.autoScroll, juce::dontSendNotification);
    quantiseBox.setSelectedId (ctx.state.quantisePresetIndex() + 1, juce::dontSendNotification);
}

void TransportBar::updatePosition (double tick, double seconds, bool playing)
{
    const auto& map = ctx.document.getTempoMap();
    const auto t = (collab::Tick) juce::jmax (0.0, tick);
    const auto bb = map.tickToBarBeat (t);
    const auto sig = map.timeSignatureAtTick (t);

    barBeatLabel.setText (juce::String (bb.bar).paddedLeft (' ', 3) + "." + juce::String (bb.beat) + "."
                            + juce::String (bb.tickInBeat).paddedLeft ('0', 3),
                          juce::dontSendNotification);

    const auto s = juce::jmax (0.0, seconds);
    const int minutes = (int) (s / 60.0);
    timeLabel.setText (juce::String (minutes).paddedLeft ('0', 2) + ":" + juce::String (s - minutes * 60.0, 3).paddedLeft ('0', 6),
                       juce::dontSendNotification);

    const double bpm = map.bpmAtTick (t);
    tempoLabel.setText (juce::String (bpm, std::abs (bpm - std::round (bpm)) < 0.005 ? 0 : 2) + " BPM   "
                          + juce::String (sig.numerator) + "/" + juce::String (sig.denominator),
                        juce::dontSendNotification);

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
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
}

void TransportBar::resized()
{
    auto area = getLocalBounds().reduced (8, 6);

    for (auto* b : { &toStartButton, &playButton, &stopButton, &recordButton })
    {
        b->setBounds (area.removeFromLeft (44));
        area.removeFromLeft (4);
    }

    area.removeFromLeft (10);
    selectTool.setBounds (area.removeFromLeft (34));
    area.removeFromLeft (2);
    pencilTool.setBounds (area.removeFromLeft (34));

    area.removeFromLeft (12);
    barBeatLabel.setBounds (area.removeFromLeft (130));
    area.removeFromLeft (6);
    timeLabel.setBounds (area.removeFromLeft (110));
    area.removeFromLeft (6);
    tempoLabel.setBounds (area.removeFromLeft (120));
    area.removeFromLeft (16);

    loopButton.setBounds (area.removeFromLeft (70));
    area.removeFromLeft (4);
    metronomeButton.setBounds (area.removeFromLeft (110));
    area.removeFromLeft (4);
    metronomeVolume.setBounds (area.removeFromLeft (70));
    area.removeFromLeft (12);
    quantiseBox.setBounds (area.removeFromLeft (128));
    area.removeFromLeft (4);
    snapButton.setBounds (area.removeFromLeft (72));
    area.removeFromLeft (4);
    autoScrollButton.setBounds (area.removeFromLeft (104));
    area.removeFromLeft (8);

    settingsButton.setBounds (area.removeFromRight (juce::jmin (110, area.getWidth())));
}

void TransportBar::ToolButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool on = getToggleState();

    g.setColour (on ? Theme::accent.withAlpha (0.35f) : (highlighted || down ? Theme::panelLight : Theme::panel));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (on ? Theme::accent : Theme::gridBar);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);

    auto iconArea = r.reduced (r.getWidth() * 0.26f, r.getHeight() * 0.22f);
    auto icon = pencil ? Theme::pencilToolIcon (iconArea) : Theme::selectToolIcon (iconArea);
    g.setColour (on ? Theme::text : Theme::textDim);
    g.fillPath (icon);
}
