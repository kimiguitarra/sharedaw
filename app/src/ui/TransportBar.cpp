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
    loopButton.setTooltip ("ループ再生（範囲はルーラーをドラッグして指定）"_ju);
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

    addAndMakeVisible (metronomeVolume);

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
    loopButton.setToggleState (ctx.state.loopEnabled, juce::dontSendNotification);
    metronomeButton.setToggleState (ctx.state.metronomeEnabled, juce::dontSendNotification);
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

    area.removeFromLeft (12);
    barBeatLabel.setBounds (area.removeFromLeft (150));
    area.removeFromLeft (6);
    timeLabel.setBounds (area.removeFromLeft (120));
    area.removeFromLeft (6);
    tempoLabel.setBounds (area.removeFromLeft (150));
    area.removeFromLeft (16);

    loopButton.setBounds (area.removeFromLeft (70));
    area.removeFromLeft (4);
    metronomeButton.setBounds (area.removeFromLeft (110));
    area.removeFromLeft (4);
    metronomeVolume.setBounds (area.removeFromLeft (90));

    settingsButton.setBounds (area.removeFromRight (130));
}
