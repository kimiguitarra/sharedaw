#pragma once

#include "EngineBridge.h"

/**
    MIDI キーボード（MIDI 入力）の一覧（オーディオ設定の中）。機器ごとのオン・オフと、
    信号が来ているかを示すレベルバーを表示する。
*/
class MidiInputPanel  : public juce::Component,
                        private juce::Timer
{
public:
    explicit MidiInputPanel (EngineBridge&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Row  : public juce::Component
    {
        juce::ToggleButton toggle;
        float activity = 0.0f;
        void paint (juce::Graphics&) override;
        void resized() override;
    };

    EngineBridge& engine;
    juce::Label title, note, empty;
    juce::OwnedArray<Row> rows;
    juce::StringArray shownNames;
    int ticks = 0;

    void rebuild();
    void timerCallback() override;
};
