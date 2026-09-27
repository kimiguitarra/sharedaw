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
    /** ツール（選択・鉛筆）のボタン。アイコンだけを描く。 */
    struct ToolButton  : public juce::Button
    {
        ToolButton (bool pencilIcon) : juce::Button ({}), pencil (pencilIcon) {}
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
        bool pencil;
    };

    AppContext& ctx;
    ToolButton selectTool { false }, pencilTool { true };
    juce::TextButton toStartButton, playButton, stopButton, recordButton, loopButton, metronomeButton, settingsButton;
    juce::ComboBox quantiseBox;
    juce::TextButton snapButton, autoScrollButton;
    juce::Slider metronomeVolume { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Label barBeatLabel, timeLabel, tempoLabel;
    bool wasPlaying = false;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
};
