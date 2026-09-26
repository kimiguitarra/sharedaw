#pragma once

#include "ui/AppContext.h"

/** トランスポート（§3.11）: 先頭へ、再生、停止、録音、ループ、メトロノーム、位置表示。 */
class TransportBar  : public juce::Component,
                      private juce::ChangeListener
{
public:
    explicit TransportBar (AppContext&);
    ~TransportBar() override;

    /** 定期的に呼んで位置表示を更新する。 */
    void updatePosition (double tick, double seconds, bool playing);

    std::function<void()> onAudioSettings;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    AppContext& ctx;
    juce::TextButton toStartButton, playButton, stopButton, recordButton, loopButton, metronomeButton, settingsButton;
    juce::Slider metronomeVolume { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Label barBeatLabel, timeLabel, tempoLabel;
    bool wasPlaying = false;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
};
